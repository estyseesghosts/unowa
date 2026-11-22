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
    """Return (base_address, bytes) for every region the ROM provides."""
    layout = json.loads((aletheia / "build" / "black" / "layout" / "layout.json").read_text())
    extract = aletheia / "extract"
    arm9 = layout["arm9"]
    path = extract / arm9["decompressed_file"]
    regions = [(arm9["load_address"], path.read_bytes())]
    for overlay in layout["overlays"]:
        path = extract / overlay["decompressed_file"]
        if path.exists():
            regions.append((overlay["ram_start"], path.read_bytes()))
    return sorted(regions)


def bytes_at(regions, address, length):
    """Read `length` ROM bytes starting at DS address `address`, or None."""
    for base, data in regions:
        if base <= address < base + len(data):
            offset = address - base
            if offset + length <= len(data):
                return data[offset:offset + length]
            return None
    return None


def generate(undefined, symbols, regions, out_path):
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
    for address, name in datas:
        position = bisect.bisect_right(all_addresses, address)
        next_addr = (
            all_addresses[position] if position < len(all_addresses) else address + MAX_BYTES
        )
        length = max(MIN_BYTES, min(MAX_BYTES, next_addr - address))
        data = None
        while length >= MIN_BYTES:
            data = bytes_at(regions, address, length)
            if data is not None:
                break
            length //= 2
        if data is None:
            # Addresses outside the extracted ROM regions are BSS or runtime
            # state: they are zero at boot, so a zero array is the faithful
            # definition. The size is a link-time placeholder; M2 places these
            # at their real DS addresses and uses their real extents.
            lines.append("unsigned char %s[%d] = {0};" % (name, MIN_BYTES * 16))
            lines.append("")
            zero_filled.append(name)
            continue
        rows = [
            "    " + ", ".join("0x%02X" % byte for byte in data[i:i + 16]) + ","
            for i in range(0, len(data), 16)
        ]
        lines.append("const unsigned char %s[%d] = {" % (name, len(data)))
        lines.extend(rows)
        lines.append("};")
        lines.append("")
        written.append(name)

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
    return written, zero_filled


def main():
    """Parse arguments and write the data image."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--aletheia", required=True, type=pathlib.Path)
    parser.add_argument("--undefined", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    undefined = set(args.undefined.read_text().split()) - PORT_DEFINED
    symbols = symbol_addresses(args.aletheia)
    regions = region_images(args.aletheia)
    written, zero_filled = generate(undefined, symbols, regions, args.out)
    print("generated %d ROM-backed and %d zero-filled data symbols in %s"
          % (len(written), len(zero_filled), args.out))


if __name__ == "__main__":
    main()
