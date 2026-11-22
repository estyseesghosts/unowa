#!/usr/bin/env python3
"""Resolve Gen V / Gen IV SDK symbol collisions by selective renaming.

Aletheia's Gen V SDK translation units define functions with the same names as
sinjoh's Gen IV libnitro.a and libnitrosystem.a. Linking both archives reports
duplicate definitions, even though the game needs symbols from each.

This tool prefixes every Gen V definition that collides with a Gen IV library
name, and applies the same rewrite to the game archive. The game therefore
calls the Gen V implementation for those functions, while sinjoh's libraries
keep their original names for everything the Gen V TUs do not define.

Usage:
    python tools/rename_genv.py \
        --gv-dir DIR --gv-out LIB \
        --game LIB --game-out LIB \
        --lib LIB [--lib LIB ...] \
        --extra OBJ [--extra OBJ ...] --extra-out DIR \
        --redefs FILE
"""

import argparse
import pathlib
import subprocess
import sys


PREFIX = "unowa_gv_"
PORT_PREFIX = "unowa_port_"


def defined_symbols(paths, nm):
    """Global defined symbols of the given objects or archives."""
    result = set()
    for path in paths:
        proc = subprocess.run([nm, "-g", "--defined-only", str(path)],
                              capture_output=True, text=True)
        if proc.returncode:
            raise SystemExit("nm failed on %s: %s" % (path, proc.stderr[:200]))
        for line in proc.stdout.splitlines():
            fields = line.split()
            if len(fields) >= 3 and fields[1] in ("T", "D", "B", "R"):
                result.add(fields[2])
    return result


def objcopy(src, dst, redefs, objcopy_bin):
    """Write a copy of src with the symbol redefinitions applied."""
    pathlib.Path(dst).parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([objcopy_bin, "--redefine-syms", str(redefs), str(src), str(dst)],
                   check=True)


def main():
    """Compute the collision set and rewrite the objects that use it."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--binutils", required=True, type=pathlib.Path)
    parser.add_argument("--gv-dir", required=True, type=pathlib.Path)
    parser.add_argument("--objects-file", type=pathlib.Path,
                        help="explicit list of GV objects; ignores other files in --gv-dir")
    parser.add_argument("--gv-out", required=True, type=pathlib.Path)
    parser.add_argument("--game", required=True, type=pathlib.Path)
    parser.add_argument("--game-out", required=True, type=pathlib.Path)
    parser.add_argument("--lib", action="append", required=True, type=pathlib.Path)
    parser.add_argument("--extra", action="append", default=[], type=pathlib.Path)
    parser.add_argument("--extra-out", type=pathlib.Path)
    parser.add_argument("--rename", action="append", default=[],
                        help="redirect NAME to unowa_port_NAME; the definition "
                             "lives in the port's stubs")
    parser.add_argument("--redefs", required=True, type=pathlib.Path)
    args = parser.parse_args()

    nm = str(args.binutils / "arm-none-eabi-nm")
    objcopy_bin = str(args.binutils / "arm-none-eabi-objcopy")
    ar = str(args.binutils / "arm-none-eabi-ar")

    if args.objects_file:
        gv_objects = [pathlib.Path(p) for p in args.objects_file.read_text().split()]
    else:
        gv_objects = sorted(args.gv_dir.rglob("*.o"))
    if not gv_objects:
        raise SystemExit("no Gen V objects in %s" % args.gv_dir)
    for obj in gv_objects:
        if not obj.is_file():
            raise SystemExit("missing Gen V object %s" % obj)

    gv_defs = defined_symbols(gv_objects, nm)
    game_defs = defined_symbols([args.game], nm)
    lib_defs = defined_symbols(args.lib, nm)
    # Collisions can come from either side: Gen V SDK names against sinjoh's
    # Gen IV libs, and MSL runtime names in the game against devkitPro's libc.
    collisions = sorted((gv_defs | game_defs) & lib_defs)

    args.redefs.parent.mkdir(parents=True, exist_ok=True)
    # --redefine-syms takes a whitespace-separated old/new pair per line.
    # A few SDK functions need a port implementation rather than the game's or
    # sinjoh's: their DS hardware reads cannot run on ARM11. Redirect those
    # references in the game archive to the port's stubs.
    redirects = {name: PORT_PREFIX + name for name in args.rename}
    redirects.update({name: PREFIX + name for name in collisions})
    args.redefs.write_text(
        "".join("%s %s\n" % (old, new) for old, new in sorted(redirects.items())),
        encoding="utf-8")

    renamed = []
    gv_obj_dir = args.gv_out.parent / "gv_obj"
    for obj in gv_objects:
        dst = gv_obj_dir / obj.name
        objcopy(obj, dst, args.redefs, objcopy_bin)
        renamed.append(dst)

    args.game_out.parent.mkdir(parents=True, exist_ok=True)
    objcopy(args.game, args.game_out, args.redefs, objcopy_bin)

    extra_out = []
    if args.extra_out:
        for obj in args.extra:
            dst = args.extra_out / obj.name
            objcopy(obj, dst, args.redefs, objcopy_bin)
            extra_out.append(dst)

    args.gv_out.parent.mkdir(parents=True, exist_ok=True)
    # `ar rcs` only adds and replaces members; it never removes them. Without
    # this unlink, dropping a TU from gv_sdk_sources.txt leaves its old object
    # in the archive and its now-unrenamed symbols collide at final link.
    if args.gv_out.exists():
        args.gv_out.unlink()
    subprocess.run([ar, "rcs", str(args.gv_out)] + [str(o) for o in renamed],
                   check=True)

    print("renamed %d colliding symbols across %d Gen V objects"
          % (len(collisions), len(renamed)))
    print("rewrote game archive -> %s" % args.game_out.name)
    if extra_out:
        print("rewrote %d extra objects -> %s" % (len(extra_out), args.extra_out))


if __name__ == "__main__":
    main()
