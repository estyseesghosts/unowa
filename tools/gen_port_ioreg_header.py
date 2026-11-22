#!/usr/bin/env python3
"""Generate a build-tree sdk_ioreg.h that maps DS memory onto port RAM.

Aletheia's game source names DS hardware by absolute address through
sdk_ioreg.h: IOREG_ADDR(0x04000208) is IME, IOREG_ADDR(0x06000000) is BG VRAM,
and so on. Those addresses are not mapped in an ARM11 user process. sinjoh's
compat layer already supplies a shadow array for each region (port_ioreg.c,
port_memory.c) and redirects the SDK's own HW_* macros to them in
patches/NitroSDK/0001 and 0005.

The generated header has the same include guard as Aletheia's, so a forced
-include makes every later `#include "../sdk_ioreg.h"` a no-op. Each address
macro becomes a pointer into the matching shadow array at the same offset,
which is the port's equivalent of sinjoh's HW_* redirection. The original
checkout is never modified.
"""

import argparse
import pathlib
import re

# (base, shadow array, size) copied from sinjoh's NitroSDK patch 0001/0005.
REGIONS = [
    (0x04000000, "NitroPort_IoReg", 0x00110000),
    (0x05000000, "NitroPort_Pltt", 0x00000800),
    (0x06000000, "NitroPort_BgVram", 0x00080000),
    (0x06200000, "NitroPort_DbBgVram", 0x00020000),
    (0x06400000, "NitroPort_ObjVram", 0x00040000),
    (0x06600000, "NitroPort_DbObjVram", 0x00020000),
    (0x06800000, "NitroPort_LcdcVram", 0x000A4000),
    (0x07000000, "NitroPort_Oam", 0x00000800),
]

ADDR = re.compile(r"IOREG_ADDR\((0x[0-9A-Fa-f]+)\)")


def redirect(match):
    """Rewrite one address literal as shadow-array base plus offset."""
    address = int(match.group(1), 16)
    for base, array, size in REGIONS:
        if base <= address < base + size:
            return "((uptr)%s + 0x%X)" % (array, address - base)
    raise SystemExit("address %s is outside every port memory region" %
                     match.group(1))


def generate(text):
    """Return the port header text derived from Aletheia's sdk_ioreg.h."""
    # The prelude runs before every source. Including types.h here would fix
    # u32/s32 before a source's GAME_TYPES_* overrides take effect, so drop it;
    # the macro bodies expand later, after the source includes types.h itself.
    text = text.replace('#include "types.h"', "")
    text = ADDR.sub(redirect, text)
    marker = "#define GAME_SDK_IOREG_H"
    if text.count(marker) != 1:
        raise SystemExit("expected one include guard in sdk_ioreg.h")
    externs = "\n".join("extern unsigned char %s[];" % array
                        for _, array, _ in REGIONS)
    return text.replace(marker, marker + "\n\n" + externs + "\n", 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path,
                        help="Aletheia src/game directory")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()

    text = (args.source / "sdk_ioreg.h").read_text(encoding="utf-8")
    output = generate(text)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(output, encoding="utf-8", newline="\n")
    print("generated %s (%d redirected addresses)" %
          (args.out, len(ADDR.findall(text))))


if __name__ == "__main__":
    main()
