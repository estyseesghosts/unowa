#!/usr/bin/env python3
"""Build a versioned, ID-indexed content pack from an extracted Black/White ROM.

The pack format is sinjoh's v1 PLATPAK (engine/compat/pak/include/pak.h):
header, sorted (id, offset, size) index, then 4-byte-aligned file bytes. The
game's FS reads use NitroFS file IDs, which Aletheia's ndsrom.parse_fnt maps
from the ROM's FNT. Only the user's own extracted data is read; nothing is
committed. The pack carries a supported SHA-1 tag because sinjoh's pak_open
(engine/compat/pak/src/pak.c:88) validates the header digest; the file bytes
and IDs are the extracted ROM's own.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

# sinjoh builder/pack.py:18-26
MAGIC = b"PLATPAK\0"
VERSION = 1
BUILD_ID_BYTES = 32
HEADER = struct.Struct("<8sI32s20sI")   # magic, version, build id, sha1, count
ENTRY = struct.Struct("<III")           # file id, offset, size
DEFAULT_BUILD_ID = "unowa-nitrofs-1"
# One of sinjoh's supported digests (pak.c:88-91); just the validation tag.
SUPPORTED_SHA1 = "0862ec35b24de5c7e2dcb88c9eea0873110d755c"


def align4(v: int) -> int:
    return (v + 3) & ~3


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--aletheia", type=Path, required=True,
                    help="Aletheia checkout with extract/data/ and tools/ndsrom.py")
    ap.add_argument("--rom", type=Path, required=True,
                    help="the verified .nds whose FNT gives the file IDs")
    ap.add_argument("--edition", choices=("black", "white"), required=True)
    ap.add_argument("--out", type=Path, required=True, help="output .pak")
    ap.add_argument("--manifest", type=Path, required=True, help="output manifest.json")
    args = ap.parse_args()

    sys.path.insert(0, str(args.aletheia / "tools"))
    import ndsrom  # Aletheia's ROM/FNT reader, unmodified

    rom = args.rom.read_bytes()
    # ROM header: FNT offset at 0x040, size at 0x044 (ndsrom.py:28-29).
    fnt_off, fnt_size = struct.unpack_from("<II", rom, 0x040)
    pairs = ndsrom.parse_fnt(rom, fnt_off, fnt_size)
    root = args.aletheia / "extract" / "data"

    files = []
    for relpath, fid in pairs:
        path = root / relpath
        if not path.is_file():
            raise SystemExit(f"missing extracted NitroFS file {path}")
        files.append((fid, path, path.stat().st_size, relpath))
    files.sort(key=lambda e: e[0])
    if len({e[0] for e in files}) != len(files):
        raise SystemExit("duplicate NitroFS file ID in FNT")

    build_id = DEFAULT_BUILD_ID
    digest = bytes.fromhex(SUPPORTED_SHA1)
    out = args.out.resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    offset = align4(HEADER.size + ENTRY.size * len(files))
    index = []
    for fid, _path, size, _rel in files:
        index.append((fid, offset, size))
        offset = align4(offset + size)

    written = HEADER.size
    with out.open("wb") as f:
        f.write(HEADER.pack(MAGIC, VERSION, build_id.encode().ljust(BUILD_ID_BYTES, b"\0"),
                            digest, len(files)))
        written += HEADER.size
        for rec in index:
            f.write(ENTRY.pack(*rec))
            written += ENTRY.size
        for (_fid, path, size, _rel), (_i, off, _sz) in zip(files, index):
            f.write(b"\0" * (off - written))
            data = path.read_bytes()
            if len(data) != size:
                raise SystemExit(f"file changed while packing: {path}")
            f.write(data)
            written = off + size
            f.write(b"\0" * (align4(written) - written))
            written = align4(written)

    manifest = {"rom_sha1": SUPPORTED_SHA1,
                "files": [{"id": fid, "path": rel, "size": size}
                          for fid, _p, size, rel in files]}
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"packed {len(files)} NitroFS files into {out} ({written} bytes)")
    print(f"build id {build_id}; manifest {args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
