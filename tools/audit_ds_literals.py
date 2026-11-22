#!/usr/bin/env python3
"""Find DS address literals in game sources that bypass sdk_ioreg.h.

Coffeepot's porting guide warns that redirecting an SDK register macro does not
redirect a source file's own pointer literal (`ctr/engineering/porting.md:92`).
`gen_port_ioreg_header.py` covers every address named through `IOREG_ADDR`, but
a source that writes `*(volatile u32 *)0x04000208` directly is unaffected.

This tool reports numeric literals in the DS's externally visible ranges so
they can be reviewed. It is an audit, not a proof: a literal may be a data
value, and an address may be computed indirectly.
"""

import argparse
import pathlib
import re

# DS address ranges from Aletheia sdk_ioreg.h and sinjoh's mmap_global.h.
REGIONS = [
    (0x02000000, 0x03000000, "main RAM / game data"),
    (0x03000000, 0x04000000, "shared WRAM"),
    (0x04000000, 0x05000000, "I/O registers"),
    (0x05000000, 0x06000000, "palette"),
    (0x06000000, 0x07000000, "VRAM"),
    (0x07000000, 0x07100000, "OAM"),
]

LITERAL = re.compile(r"(?<![\w.])0x([0-9A-Fa-f]{6,8})(?![\w])")


def classify(value):
    for start, end, name in REGIONS:
        if start <= value < end:
            return name
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    counts = {}
    hits = []
    for path in sorted(args.source.rglob("*.c")):
        for number, line in enumerate(path.read_text(
                encoding="utf-8", errors="replace").splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith(("*", "//", "/*")):
                continue
            for match in LITERAL.finditer(line):
                region = classify(int(match.group(1), 16))
                if region is None:
                    continue
                counts[region] = counts.get(region, 0) + 1
                hits.append((str(path.relative_to(args.source)), number,
                             match.group(0), region, line.strip()))

    print("DS address literals by region:")
    for region, count in sorted(counts.items()):
        print("  %-20s %d" % (region, count))
    print("total: %d" % len(hits))
    print("\nfirst %d hits:" % args.top)
    for name, number, literal, region, line in hits[:args.top]:
        print("%s:%d  %s  [%s]  %s" % (name, number, literal, region, line[:100]))


if __name__ == "__main__":
    main()
