#!/usr/bin/env python3
"""Run one of Aletheia's own tools against the pinned checkout, unmodified.

Usage:
    python tools/aletheia_run.py ALETHEIA_ROOT TOOL [ARGS...]
    e.g. python tools/aletheia_run.py ../aletheia/aletheia gamelink arm9_main

Aletheia's tools/match.py calls gendata.generate_all() before compiling any TU
that includes build/gen/ data.  generate_all() writes every SPECS entry, and
one of them, twl_0272EB9C, comes from the modcrypt-encrypted arm9i.bin, which
cannot be decrypted without the DSi AES key-scrambler constant
(aletheia tools/modcrypt.py:26-46).  Only src/game/twl/card_rom.c consumes it,
and that TU is part of the TWL module, not the arm9_main or overlay links unowa
needs (unowa runs as a non-TWL console, docs/status.md "M1 source closure").

This runner removes that one entry from gendata.SPECS in-process and then runs
the requested tool's main().  Nothing in the Aletheia checkout is edited.
"""
import os
import runpy
import sys

TWL_ONLY_SPECS = ("twl_0272EB9C",)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    root = os.path.abspath(sys.argv[1])
    tool = sys.argv[2]
    tools_dir = os.path.join(root, "tools")
    script = os.path.join(tools_dir, tool + ".py")
    if not os.path.isfile(script):
        sys.exit("no such Aletheia tool: %s" % script)
    sys.path.insert(0, tools_dir)
    os.chdir(root)
    import gendata
    for name in TWL_ONLY_SPECS:
        gendata.SPECS.pop(name, None)
    sys.argv = [script] + sys.argv[3:]
    runpy.run_path(script, run_name="__main__")


if __name__ == "__main__":
    main()
