#!/usr/bin/env python3
"""Write build-tree copies of Aletheia sources with GCC-compatible rewrites.

Aletheia keeps its original Metrowerks inline assembly. This tool rewrites the
small, identified patterns that devkitARM GCC cannot parse, without editing the
pinned checkout. Only listed files are copied, and only into the gitignored
build tree. Every rewrite is reported so the transformation stays reviewable.

Usage:
    python tools/patch_sources.py --source DIR --out DIR FILE [FILE ...]
"""

import argparse
import pathlib
import re

# Metrowerks `asm { clz x, x }` counts leading zeros in place. GCC's builtin
# returns the count, so assign it back to the same variable.
CLZ = re.compile(r"asm\s*\{\s*clz\s+([A-Za-z_]\w*)\s*,\s*\1\s*\}")

# DS hardware registers that the ARM11 process does not map. The port replaces
# the read with a neutral value; the surrounding control flow is preserved.
PORT_FIXUPS = {
    "scfg_no_scfgclk": [
        ("#define reg_SCFG_CLK (*(vu16 *)REG_SCFG_CLK_ADDR)",
         "#define reg_SCFG_CLK 0  /* port: no DS SCFG_CLK on ARM11 */", 1),
    ],
    # The port maps HW_DB_OBJ_VRAM to a shadow-array address, which is not an
    # integer constant expression in C. These locals used it as an enum value.
    # OS_CreateExtraHeap hardcodes the DS main-RAM base as its Cell backing
    # store. That address is unmapped in an ARM11 3DSX. A dedicated buffer is
    # used rather than NitroPort_MainMem, whose first bytes are the SDK arena
    # and would be overwritten by the extra heap's 0x4000-byte region.
    "extra_heap_mainmem": [
        ("                Cell *cell = (Cell *)0x02000000;",
         "                extern unsigned char unowa_extra_heap[];\n"
         "                Cell *cell = (Cell *)(void *)unowa_extra_heap;", 1),
    ],
    # M2 acceptance asks that overlay loads and initialisers be logged. The
    # game loads every overlay through this one entry point, so a call at its
    # head observes each load without changing any overlay state.
    "overlay_load_log": [
        ("int GF_OVERLAY_Load(s32 handle)\n{\n",
         "extern void NitroPort_Log(const char *fmt, ...);\n"
         "extern void unowa_log_overlay(int handle);\n\n"
         "int GF_OVERLAY_Load(s32 handle)\n{\n"
         "    NitroPort_Log(\"[unowa] overlay load %d\", (int)handle);\n"
         "    unowa_log_overlay((int)handle);\n", 1),
    ],
    # Aletheia records ov020_id as the absolute DS value 0x14
    # (src/matched/ov/ov010_func_0215FA00.c:13) and the only two uses are in
    # this file. GNU ld relocates `(uptr)ov020_id` with R_ARM_ABS32 against
    # that ABS symbol; 3dsxtool refuses an absolute target below the 3DSX
    # image base ("Relocation to invalid address"), so inline the constant.
    "ov020_id_constant": [
        ("extern unsigned char ov020_id[];",
         "#define ov020_id 0x14  /* port: absolute DS symbol, see port_stubs.c */",
         1),
    ],
    # sinjoh emulates DS interrupts by calling handlers directly, never by
    # entering ARM IRQ mode, so the DS's mode-gated FIFO drain in PMi_WaitBusy
    # cannot run. Drain the reply queue on every iteration instead.
    "pm_waitbusy_drain": [
        ("        if (((g_020A8724 & 2) && OS_GetProcMode() == 0x12)\n"
         "            || ((g_020A8724 & 4) && OS_GetCpsrIrq() == 0x80)\n"
         "            || ((g_020A8724 & 8) && reg_OS_IME == 0)) {\n"
         "            PXIi_HandlerRecvFifoNotEmpty();\n"
         "        }",
         "        PXIi_HandlerRecvFifoNotEmpty();", 1),
        # sinjoh's os_init calls its own PM_Init, not Aletheia's. Initialise
        # the Gen V PM state before the first wait so the lock can be cleared.
        ("void PMi_WaitBusy(void)\n{\n",
         "extern void unowa_pm_ensure_ready(void);\n\n"
         "void PMi_WaitBusy(void)\n{\n"
         "    unowa_pm_ensure_ready();\n", 1),
    ],
    # The switch is on NDMA[ch].cnt & 0x0F000000, so this case is the DS I/O
    # destination-region code 0x04000000, not a pointer to DISPCNT.
    "pm_ndma_case": [
        ("case REG_DISPCNT_ADDR:", "case 0x04000000:", 1),
    ],
    "vram_enum_base": [
        ("enum { VRAM_BASE = HW_DB_OBJ_VRAM } vram;",
         "uptr vram = HW_DB_OBJ_VRAM;", None),
        ("enum { VRAM_BASE = HW_DB_OBJ_VRAM };", "", 0),
        ("VRAM_BASE", "HW_DB_OBJ_VRAM", None),
    ],
}


def remove_asm_body(text, name):
    """Remove one `asm TYPE name(...) { ... }` definition from the text."""
    head = re.compile(r"asm\s+[A-Za-z_]\w*\s+" + re.escape(name) +
                      r"\s*\([^)]*\)\s*\{")
    count = 0
    while True:
        match = head.search(text)
        if match is None:
            return text, count
        depth = 1
        end = match.end()
        while end < len(text) and depth:
            if text[end] == "{":
                depth += 1
            elif text[end] == "}":
                depth -= 1
            end += 1
        if depth:
            raise SystemExit("unbalanced asm body for %s" % name)
        text = text[:match.start()] + text[end:]
        count += 1


def patch_text(text, removed=(), fixups=()):
    """Return the rewritten text and a count of applied rewrites."""
    text, count = CLZ.subn(lambda m: "%s = __builtin_clzl(%s);" % (m.group(1), m.group(1)), text)
    for name in removed:
        text, n = remove_asm_body(text, name)
        count += n
    for name in fixups:
        if name not in PORT_FIXUPS:
            raise SystemExit("unknown fixup %s" % name)
        for old, new, expected in PORT_FIXUPS[name]:
            found = text.count(old)
            if found == 0 and expected == 0:
                continue
            if found == 0:
                raise SystemExit("fixup %s: no match for %r" % (name, old))
            if expected is not None and expected != 0 and found != expected:
                raise SystemExit("fixup %s: expected %d matches for %r, found %d"
                                 % (name, expected, old, found))
            text = text.replace(old, new)
            count += found
    return text, count


def main():
    """Copy each requested source into the output tree with rewrites applied."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument("--remove-asm", action="append", default=[],
                        help="delete the named Metrowerks asm body; its C "
                             "definition is supplied by the port")
    parser.add_argument("--fixup", action="append", default=[],
                        help="apply a named DS-hardware fixup from PORT_FIXUPS")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()

    for name in args.files:
        source = args.source / name
        text = source.read_text(encoding="utf-8")
        patched, count = patch_text(text, args.remove_asm, args.fixup)
        target = args.out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(patched, encoding="utf-8", newline="\n")
        print("patched %s (%d rewrite%s)" % (name, count, "" if count == 1 else "s"))


if __name__ == "__main__":
    main()
