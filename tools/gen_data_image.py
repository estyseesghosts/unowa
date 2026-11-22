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
rewrites every stored *data* pointer to the emitted array that holds the bytes
it names. It is restricted to the ARM9 static region, whose addresses are
unique; overlay load addresses overlap (237 overlays share 58 slots), so
overlay-internal pointers are left for the overlay packaging work. Odd values
are Thumb code pointers and are never rewritten as data.

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
    """Map name -> (address, kind) from Aletheia's own symbol headers."""
    result = {}
    root = aletheia / "src" / "game"
    for path in root.rglob("*"):
        if path.suffix not in (".c", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in SYMBOL_RE.finditer(text):
            name, addr, kind = match.group(1), int(match.group(2), 16), match.group(3)
            result.setdefault(name, (addr, kind))
    return result


def region_images(aletheia):
    """Return ((base, bytes) regions, arm9_load_address) from the ROM extract."""
    layout = json.loads((aletheia / "build" / "black" / "layout" / "layout.json").read_text())
    extract = aletheia / "extract"
    arm9 = layout["arm9"]
    path = extract / arm9["decompressed_file"]
    regions = [(arm9["load_address"], path.read_bytes())]
    for overlay in layout["overlays"]:
        path = extract / overlay["decompressed_file"]
        if path.exists():
            regions.append((overlay["ram_start"], path.read_bytes()))
    return sorted(regions), arm9["load_address"]


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


def generate(undefined, symbols, regions, arm9_base, out_path, reloc_name):
    """Write C definitions for data symbols found in the ROM images."""
    all_addresses = sorted(
        addr for addr, _kind in symbols.values() if addr >= 0x02000000
    )
    datas = sorted(
        (addr, name) for name, (addr, kind) in symbols.items()
        if name in undefined and kind == "data" and addr >= 0x02000000
    )
    lines = [
        "/* Generated from the player's own ROM by tools/gen_data_image.py.",
        " * Never edit. Never commit ROM bytes. See docs/status.md. */",
        "",
    ]
    written = []
    zero_filled = []
    rom_arrays = []          # (name, ds address, bytes, region index)
    for address, name in datas:
        position = bisect.bisect_right(all_addresses, address)
        next_addr = (
            all_addresses[position] if position < len(all_addresses) else address + MAX_BYTES
        )
        length = max(MIN_BYTES, min(MAX_BYTES, next_addr - address))
        found = None
        while length >= MIN_BYTES:
            found = bytes_at(regions, address, length)
            if found is not None:
                break
            length //= 2
        if found is not None:
            data, region = found
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
            written.append(name)
            rom_arrays.append((name, address, data, region))
            continue
        # Addresses outside the extracted ROM regions are BSS or runtime
        # state: they are zero at boot, so a zero array is the faithful
        # definition. The size is a link-time placeholder; M2 places these
        # at their real DS addresses and uses their real extents.
        lines.append("unsigned char %s[%d] __attribute__((aligned(8))) = {0};"
                     % (name, MIN_BYTES * 16))
        lines.append("")
        zero_filled.append(name)

    # Relocate stored data pointers. Only the ARM9 static region is handled:
    # its addresses are unique, whereas overlays share 58 load slots across
    # 237 files and cannot be disambiguated until overlay packaging.
    arm9_spans = sorted(
        (addr, addr + len(data), name, addr, len(data))
        for name, addr, data, region in rom_arrays
        if regions[region][0] == arm9_base
    )
    arm9_starts = [span[0] for span in arm9_spans]
    relocs = []
    skipped = 0
    for name, address, data, region in rom_arrays:
        if regions[region][0] != arm9_base:
            continue
        for offset in range(0, len(data) - 3, 4):
            value = int.from_bytes(data[offset:offset + 4], "little")
            if not 0x02000000 <= value < 0x03000000:
                continue
            if value & 1:
                skipped += 1   # Thumb code pointer, not data
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
    lines.append("void %s(void)" % reloc_name)
    lines.append("{")
    lines.append("    unsigned int i;")
    lines.append("    for (i = 0; i < %s_count; i++)" % reloc_name)
    lines.append("        *%s_slots[i] = (unsigned int)%s_values[i];" % (reloc_name, reloc_name))
    lines.append("}")
    lines.append("")

    # These are linker-symbol *values* (e.g. (uptr)ov009_id), so a data variable
    # would make &symbol the value instead. `.set` would fix that, but 3dsxtool
    # rejects relocations against absolute low addresses, so keep addressable
    # data here and normalise the handle at the GF_OVERLAY_Load boundary (M3).
    small = sorted(n for n in undefined if 0 < symbols.get(n, (0, ""))[0] < 0x100)
    if small:
        lines.append("/* Linker-symbol values: read through, do not take the address. */")
        for name in small:
            lines.append("const unsigned int %s = %d;" % (name, symbols[name][0]))
        lines.append("")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")
    return written, zero_filled, len(relocs), skipped


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
    regions, arm9_base = region_images(args.aletheia)
    written, zero_filled, relocs, skipped = generate(
        undefined, symbols, regions, arm9_base, args.out, args.reloc_name)
    print("generated %d ROM-backed and %d zero-filled data symbols in %s"
          % (len(written), len(zero_filled), args.out))
    print("  %s: %d data pointers relocated, %d non-data/out-of-range words skipped"
          % (args.reloc_name, relocs, skipped))


if __name__ == "__main__":
    main()
