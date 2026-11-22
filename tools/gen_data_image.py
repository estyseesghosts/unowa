#!/usr/bin/env python3
"""Generate DS data-symbol definitions from a verified user ROM dump.

Aletheia's game sources declare external data at fixed DS addresses, but the
bytes live in the player's ROM, not in the decomp. This tool reads those bytes
out of Aletheia's extracted region images and emits C definitions so the M1
object closure can be linked. It never copies ROM data into the repository:
the output is written to the gitignored build tree.

Address and symbol names come from Aletheia's own source headers and
layout/layout.json. Region bytes come from extract/. The generated arrays use
the real ROM bytes up to the next known data symbol, so their contents are
faithful even though their runtime addresses are assigned by the ARM11 linker
at this milestone.

ROM data also stores absolute DS addresses (pointer tables, path tables). The
bytes are emitted verbatim, so those words point into unmapped DS space on the
3DS. The generator therefore emits a relocation pass, `*_relocate()`, that
rewrites stored pointers:

  * data pointers become the emitted array that holds the target bytes;
  * function pointers become the *port* symbol for the function, taken from
    Aletheia's layout/function_addrs.tsv (address -> name, source file, ISA).
    The stored value is the symbol's ARM11 address, so its low bit follows how
    the port compiled the function, not the DS ISA: the port builds these TUs
    as ARM, and an ARM address is what `blx` must receive. Referencing the
    symbol from this data image also keeps it through `--gc-sections`, which
    previously dropped format handlers reachable only through such a table.

Both passes cover only the ARM9 static region, whose addresses are unique.
Overlays share 58 load slots across 237 files, so an overlay address can name
several different functions; overlay-internal pointers wait for the overlay
packaging work. A function pointer is only rewritten when its source file is
in the port's compile set and the function is not file-static, so the
reference always resolves. Odd values that are not a known Thumb function
start are left alone.

Usage:
    python tools/gen_data_image.py --undefined FILE --out FILE
"""

import argparse
import bisect
import json
import pathlib
import re

SYMBOL_RE = re.compile(r"\*\s+([A-Za-z_]\w*)\s+0x([0-9A-Fa-f]{8})\s+(data|arm|thumb)")
MIN_BYTES = 4
MAX_BYTES = 8192

# Aletheia's function table is authoritative for DS code addresses. The port
# compiles the same TUs, skipping the files listed in these blocklists; a
# reference to a function from a skipped file would not link.
BLOCKLISTS = ("game_blocklist.txt", "game_overlay_blocklist.txt")

# Data symbols the port defines itself. Emitting the ROM bytes for these would
# store raw DS addresses that no ARM11 process can call; see port_stubs.c.
# MATHi_SHA1ProcessMessageBlockFunc is a function pointer: its ROM value is the
# DS address 0x02081e7c (MATHi_SHA1Transform), so it must be initialised from
# the port's C implementation instead.
PORT_DEFINED = {
    "MATHi_SHA1ProcessMessageBlockFunc",
    # ROM table of DS Thumb function pointers into ov009. Those functions are
    # not linked; port_stubs.c supplies an inert table so boot can continue.
    "_wifiDeviceTable",
}


def symbol_addresses(aletheia):
    """Map name -> (address, kind, source) from Aletheia's symbol headers.

    `source` is the `src/game/...` file whose header carries the declaration.
    It names the module that owns the object, which fixes both the bytes to
    copy and the object's extent: an ARM9-declared object at or above the end
    of the ARM9 image is BSS, even though an overlay may load over that
    address range later.
    """
    result = {}
    root = aletheia / "src" / "game"
    for path in root.rglob("*"):
        if path.suffix not in (".c", ".h"):
            continue
        source = "src/game/" + path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in SYMBOL_RE.finditer(text):
            name, addr, kind = match.group(1), int(match.group(2), 16), match.group(3)
            result.setdefault(name, (addr, kind, source))
    return result


def compiled_sources(aletheia):
    """Set of `src/game/...` TU paths the port compiles (engine/game.mk:72-75).

    Mirrors the makefile rule: every `src/game/<dir>/*.c` except the blocklist
    entries. A function from a file outside this set has no port object, so its
    address must not be emitted as a relocation target.
    """
    root = aletheia / "src" / "game"
    blocked = set()
    engine = pathlib.Path(__file__).resolve().parent.parent / "engine"
    for name in BLOCKLISTS:
        path = engine / name
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                blocked.add(line)
    return {
        "src/game/" + path.relative_to(root).as_posix()
        for path in root.glob("*/*.c")
        if path.relative_to(root).as_posix() not in blocked
    }


def function_addresses(aletheia, compiled):
    """Map stored pointer value -> port symbol name, for uniquely named code.

    Aletheia's function_addrs.tsv gives one row per function: region, address,
    name, ISA, size, source file, static flag. Only ARM9-region, non-static
    functions from compiled files are usable: overlay addresses are shared
    between files, static functions have no external symbol, and a skipped
    file has no port object.
    """
    table = aletheia / "layout" / "function_addrs.tsv"
    result = {}
    conflicts = set()
    lines = table.read_text(encoding="utf-8").splitlines()
    for line in lines[1:]:
        region, address, name, isa, _size, source, static = line.split("\t")
        if region != "arm9" or static != "no" or source not in compiled:
            continue
        value = int(address, 16) | (1 if isa == "thumb" else 0)
        if value in result and result[value] != name:
            conflicts.add(value)
            continue
        result[value] = name
    for value in conflicts:
        del result[value]
    return result


def region_images(aletheia):
    """Return ((base, bytes) regions, arm9_load_address, modules).

    `modules` maps a module key (`arm9`, `ovNNN`) to
    (image start, image end, module end, bytes or None). The image end is
    where BSS begins; the module end is the last byte the module owns
    (layout.json's bss_end). Bytes are None when the extract has no image for
    the module (an overlay that was not decompressed, or `twl`, which needs
    the DSi key).
    """
    layout = json.loads((aletheia / "build" / "black" / "layout" / "layout.json").read_text())
    extract = aletheia / "extract"
    arm9 = layout["arm9"]
    path = extract / arm9["decompressed_file"]
    image = path.read_bytes()
    regions = [(arm9["load_address"], image)]
    modules = {
        "arm9": (arm9["load_address"], arm9["load_address"] + len(image),
                 arm9["static"]["bss_end"], image),
    }
    for overlay in layout["overlays"]:
        key = "ov%03d" % overlay["id"]
        path = extract / overlay["decompressed_file"]
        data = path.read_bytes() if path.exists() else None
        if data is not None:
            regions.append((overlay["ram_start"], data))
        end = overlay["bss_end"] if overlay.get("bss_size") else (
            overlay["ram_start"] + len(data) if data is not None else overlay["ram_end"])
        modules.setdefault(key, (overlay["ram_start"],
                                 overlay["ram_start"] + (len(data) if data is not None else 0),
                                 end, data))
    return sorted(regions), arm9["load_address"], modules


def bytes_at(regions, address, length):
    """Return (bytes, region_index) for `length` bytes at DS address, or None.

    Duplicate overlay load addresses resolve to the first region, which is the
    same choice the emission below makes; relocation stays inside that region.
    """
    for index, (base, data) in enumerate(regions):
        if base <= address < base + len(data):
            offset = address - base
            if offset + length <= len(data):
                return data[offset:offset + length], index
            return None
    return None


def pointer_target(spans, starts, value):
    """(name, base, length) for the emitted array holding `value`, or None."""
    index = bisect.bisect_right(starts, value) - 1
    if index < 0:
        return None
    low, high, name, base, length = spans[index]
    if low <= value < high:
        return name, base, length
    return None


def module_of(source):
    """Module key for a `src/game/...` path: arm9 or the overlay directory."""
    parts = source.split("/")
    if len(parts) < 4:
        return None
    if parts[2] in ("arm9", "arm9_autoload0_ITCM"):
        return "arm9"
    return parts[2]


# Aletheia's autogenerated names for objects it has not identified yet. They
# are legitimate aliases, but a semantic name is the better symbol to hang the
# object on.
AUTONAME_RE = re.compile(r"^(?:d|g|data)_[0-9A-Fa-f]{8}$")


def alias_order(entry):
    """Sort key that puts a semantic name before an autogenerated one."""
    name = entry[0]
    return (bool(AUTONAME_RE.match(name)), len(name), name)


def object_groups(datas):
    """Group `(address, name, source)` rows into one object per address.

    Returns a list of `(module, address, [(name, source), ...])` ordered by
    address, with the primary name first.

    Aliases are scoped to a module. The same numeric address in two different
    modules is not one object: overlays reuse ARM9 RAM slots and reuse the same
    slot among themselves, so `0x0214616C` is `pBattleRec` in ARM9 and
    `d_0214616C` in ov125, holding different content at different times. Only
    names that live in the same module and reach the same address describe the
    same storage, as `sOverlayLaunchWork`/`sOverlayLaunchWorkArg` do.
    """
    groups = {}
    for address, name, source in datas:
        key = (module_of(source), address)
        groups.setdefault(key, []).append((name, source))
    return [
        (module, address, sorted(entries, key=alias_order))
        for (module, address), entries in sorted(groups.items(), key=lambda item: item[0][1])
    ]


def generate(undefined, symbols, regions, arm9_base, modules, functions,
             out_path, reloc_name):
    """Write C definitions for data symbols found in the ROM images."""
    datas = sorted(
        (addr, name, source) for name, (addr, kind, source) in symbols.items()
        if name in undefined and kind == "data" and addr >= 0x02000000
    )
    # Objects are bounded by the next symbol of the owning module, not by the
    # next symbol anywhere: overlays load over ARM9 BSS addresses, so a global
    # address list would let an overlay symbol truncate an ARM9 object.
    module_addresses = {}
    for address, _name, source in datas:
        module = module_of(source)
        module_addresses.setdefault(module, []).append(address)
    for addresses in module_addresses.values():
        addresses.sort()

    lines = [
        "/* Generated from the player's own ROM by tools/gen_data_image.py.",
        " * Never edit. Never commit ROM bytes. See docs/status.md. */",
        "",
    ]
    written = []
    zero_filled = []
    rom_arrays = []          # (name, ds address, bytes, in_arm9)
    for module, address, entries in object_groups(datas):
        name, source = entries[0]
        addresses = module_addresses.get(module, [address])
        position = bisect.bisect_right(addresses, address)
        if position < len(addresses):
            next_addr = addresses[position]
        elif module in modules:
            next_addr = modules[module][2]
        else:
            next_addr = address + MAX_BYTES
        length = max(MIN_BYTES, min(MAX_BYTES, next_addr - address))

        # Read the bytes from the module that owns the object. An ARM9 object
        # at or above the end of the ARM9 image is BSS: an overlay that maps
        # that address later does not own it. 0x0214655C (_PrintJobWork) is
        # one, and reading overlay bytes there filled the print job with
        # glyph tiles.
        found = None
        info = modules.get(module)
        if info is not None and info[3] is not None:
            start, image_end, _end, image = info
            if start <= address < image_end:
                data = image[address - start:]
                probe = min(length, len(data))
                if probe >= MIN_BYTES:
                    found = (data[:probe], start)
        if found is not None:
            data, base = found
            rows = [
                "    " + ", ".join("0x%02X" % byte for byte in data[i:i + 16]) + ","
                for i in range(0, len(data), 16)
            ]
            # Data the DS copied from ROM into RAM is writable, and the
            # relocation below writes into these arrays. aligned(8) lets the
            # game read them through double/u64 lvalues without a fault.
            lines.append("unsigned char %s[%d] __attribute__((aligned(8))) = {"
                         % (name, len(data)))
            lines.extend(rows)
            lines.append("};")
            lines.append("")
            for alias, _source in entries[1:]:
                lines.append("/* Aliased name for the same storage; keep it one"
                             " symbol or writes through either name are lost. */")
                lines.append("extern unsigned char %s[%d]"
                             " __attribute__((alias(\"%s\")));"
                             % (alias, len(data), name))
                lines.append("")
            written.append(name)
            rom_arrays.append((name, address, data, base == arm9_base))
            continue
        # BSS or runtime state: zero at boot, so a zero array is the faithful
        # definition. The size is the gap to the next object of the same
        # module, capped so one very distant neighbour cannot inflate it.
        lines.append("unsigned char %s[%d] __attribute__((aligned(8))) = {0};"
                     % (name, length))
        lines.append("")
        for alias, _source in entries[1:]:
            lines.append("/* Aliased name for the same storage; keep it one"
                         " symbol or writes through either name are lost. */")
            lines.append("extern unsigned char %s[%d]"
                         " __attribute__((alias(\"%s\")));"
                         % (alias, length, name))
            lines.append("")
        zero_filled.append(name)

    # Relocate stored pointers. Only the ARM9 static region is handled: its
    # addresses are unique, whereas overlays share 58 load slots across 237
    # files and cannot be disambiguated until overlay packaging.
    #
    # Walk 4-byte words at addresses aligned in DS space, not at array-relative
    # offsets: 254 of the emitted objects start at a non-multiple of 4, and a
    # pointer inside such an object does not sit at a multiple of 4 from the
    # array base.
    arm9_spans = sorted(
        (addr, addr + len(data), name, addr, len(data))
        for name, addr, data, in_arm9 in rom_arrays
        if in_arm9
    )
    arm9_starts = [span[0] for span in arm9_spans]
    relocs = []
    skipped = 0
    fnptrs = []
    fn_unresolved = 0
    for name, address, data, in_arm9 in rom_arrays:
        if not in_arm9:
            continue
        for offset in range((-address) % 4, len(data) - 3, 4):
            value = int.from_bytes(data[offset:offset + 4], "little")
            function = functions.get(value)
            if function is not None:
                fnptrs.append((name, offset, function, value))
                continue
            if not 0x02000000 <= value < 0x03000000:
                continue
            if value & 1:
                # Odd values are Thumb code pointers. A known Thumb function
                # was handled above; anything else is code this image cannot
                # name, so it stays as the ROM word.
                fn_unresolved += 1
                continue
            target = pointer_target(arm9_spans, arm9_starts, value)
            if target is None:
                skipped += 1
                continue
            target_name, target_base, target_length = target
            if value + 4 > target_base + target_length:
                skipped += 1
                continue
            relocs.append((name, offset, target_name, value - target_base))

    lines.append("/* DS data pointers: rewrite each stored DS address to the"
                 " emitted array that holds the bytes it names. See the header. */")
    lines.append("unsigned int %s_count = %d;" % (reloc_name, len(relocs)))
    lines.append("unsigned int %s_skipped = %d;" % (reloc_name, skipped))
    lines.append("static unsigned int *const %s_slots[%d] = {"
                 % (reloc_name, max(1, len(relocs))))
    for name, offset, _target, _delta in relocs:
        lines.append("    (unsigned int *)(void *)(%s + %d)," % (name, offset))
    if not relocs:
        lines.append("    0,")
    lines.append("};")
    lines.append("static const unsigned char *const %s_values[%d] = {"
                 % (reloc_name, max(1, len(relocs))))
    for _name, _offset, target, delta in relocs:
        lines.append("    %s + %d," % (target, delta))
    if not relocs:
        lines.append("    0,")
    lines.append("};")

    lines.append("/* DS function pointers: each matched a uniquely named ARM9"
                 " function in Aletheia's function_addrs.tsv whose source file the"
                 " port compiles. The value is the port symbol, so the ISA bit is"
                 " whatever the port built. */")
    fn_name = reloc_name + "_functions"
    lines.append("unsigned int %s_count = %d;" % (fn_name, len(fnptrs)))
    lines.append("unsigned int %s_unresolved = %d;" % (fn_name, fn_unresolved))
    # The prototypes are unknown here and irrelevant: only the addresses are
    # taken. Declaring each name keeps this file self-contained.
    for function in sorted({entry[2] for entry in fnptrs}):
        lines.append("extern void %s(void);" % function)
    lines.append("static unsigned int *const %s_slots[%d] = {"
                 % (fn_name, max(1, len(fnptrs))))
    for name, offset, _function, _value in fnptrs:
        lines.append("    (unsigned int *)(void *)(%s + %d)," % (name, offset))
    if not fnptrs:
        lines.append("    0,")
    lines.append("};")
    lines.append("static void (*const %s_values[%d])(void) = {"
                 % (fn_name, max(1, len(fnptrs))))
    for _name, _offset, function, _value in fnptrs:
        lines.append("    (void (*)(void))%s," % function)
    if not fnptrs:
        lines.append("    0,")
    lines.append("};")

    lines.append("void %s(void)" % reloc_name)
    lines.append("{")
    lines.append("    unsigned int i;")
    lines.append("    for (i = 0; i < %s_count; i++)" % reloc_name)
    lines.append("        *%s_slots[i] = (unsigned int)%s_values[i];" % (reloc_name, reloc_name))
    lines.append("    for (i = 0; i < %s_count; i++)" % fn_name)
    lines.append("        *%s_slots[i] = (unsigned int)%s_values[i];" % (fn_name, fn_name))
    lines.append("}")
    lines.append("")

    # These are linker-symbol *values* (e.g. (uptr)ov009_id), so a data variable
    # would make &symbol the value instead. `.set` would fix that, but 3dsxtool
    # rejects relocations against absolute low addresses, so keep addressable
    # data here and normalise the handle at the GF_OVERLAY_Load boundary (M3).
    small = sorted(n for n in undefined if 0 < symbols.get(n, (0, "", ""))[0] < 0x100)
    if small:
        lines.append("/* Linker-symbol values: read through, do not take the address. */")
        for name in small:
            lines.append("const unsigned int %s = %d;" % (name, symbols[name][0]))
        lines.append("")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")
    return written, zero_filled, len(relocs), skipped, len(fnptrs)


def main():
    """Parse arguments and write the data image."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--aletheia", required=True, type=pathlib.Path)
    parser.add_argument("--undefined", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--reloc-name", required=True,
                        help="C function name for the generated relocation pass")
    args = parser.parse_args()

    undefined = set(args.undefined.read_text().split()) - PORT_DEFINED
    symbols = symbol_addresses(args.aletheia)
    regions, arm9_base, bounds = region_images(args.aletheia)
    compiled = compiled_sources(args.aletheia)
    functions = function_addresses(args.aletheia, compiled)
    written, zero_filled, relocs, skipped, fnptrs = generate(
        undefined, symbols, regions, arm9_base, bounds, functions, args.out,
        args.reloc_name)
    print("generated %d ROM-backed and %d zero-filled data symbols in %s"
          % (len(written), len(zero_filled), args.out))
    print("  %s: %d data pointers relocated, %d non-data/out-of-range words skipped"
          % (args.reloc_name, relocs, skipped))
    print("  %s: %d function pointers resolved from %d candidate symbols"
          % (args.reloc_name, fnptrs, len(functions)))


if __name__ == "__main__":
    main()
