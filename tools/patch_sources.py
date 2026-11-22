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


# The DS shared work area. The DS ARM7 and boot ROM keep system state in the
# last 4 KiB of main RAM: the V-blank counter (+0xC3C), X/Y buttons and lid
# (+0xFA8), the cartridge header copy (+0xE00), owner info (+0xC80), lock
# words (+0xFB0..). Aletheia's game sources name it by literal, at the TWL
# address 0x02FFFxxx or its NTR mirror 0x027FFxxx.
#
# A 3DS process cannot map memory there: 0x00100000-0x04000000 is the
# executable window and MapMemoryBlock only accepts the heap window at
# 0x08000000 (coffeepot ctr/hardware/memory.md, ARM11 userland virtual map).
# sinjoh already shadows this area as NitroPort_MainMem + 0x7FF000
# (NitroSDK mmap_shared.h, HW_*_BUF = HW_MAIN_MEM + 0x007FFxxx) and fills it:
# port_boot.c writes HW_BUTTON_XY_BUF and HW_ROM_HEADER_BUF. Every literal is
# rewritten to that shadow so the game and the SDK read the same words.
# Coffeepot ctr/engineering/porting.md:92 requires literals to be redirected
# as well as SDK macros.
SHARED_LITERAL = re.compile(r"(?<![\w.])0x02[7Ff][Ff][Ff]([0-9A-Fa-f]{3})(?![\w])")
SHARED_MACRO = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+"
                          r"(0x02[7Ff][Ff][Ff][0-9A-Fa-f]{3})[ \t]*$", re.M)
SHARED_DECL = "extern unsigned char NitroPort_MainMem[];  /* port: DS shared area */\n"
INCLUDE_LINE = re.compile(r"^[ \t]*#[ \t]*include\b.*$", re.M)
# Comments and string/char literals; the rewrite applies only between them.
NON_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'',
                      re.S)


def shared_address(offset_hex):
    """Port expression for DS shared-area offset 0xFFFxxx -> shadow + 0x7FFxxx."""
    return "((unsigned int)NitroPort_MainMem + 0x7FF%s)" % offset_hex.upper()


def code_spans(text):
    """Yield (start, end) of the code between comments and literals."""
    pos = 0
    for match in NON_CODE.finditer(text):
        yield pos, match.start()
        pos = match.end()
    yield pos, len(text)


def header_shared_macros(source_root):
    """Map NAME -> literal for `#define NAME 0x02FFFxxx` in Aletheia headers."""
    macros = {}
    for header in sorted(source_root.rglob("*.h")):
        text = header.read_text(encoding="utf-8", errors="replace")
        for name, value in SHARED_MACRO.findall(text):
            previous = macros.setdefault(name, value.upper())
            if previous != value.upper():
                raise SystemExit("header macro %s has two shared-area values: %s, %s"
                                 % (name, previous, value))
    return macros


def rewrite_shared_area(text, header_macros):
    """Redirect DS shared-area literals in code to sinjoh's shadow.

    Returns the new text and the number of rewritten sites. Header macros are
    overridden after the file's last #include, which is before any use."""
    pieces = []
    count = 0
    last = 0
    used = set()
    for start, end in code_spans(text):
        pieces.append(text[last:start])
        code, n = SHARED_LITERAL.subn(lambda m: shared_address(m.group(1)),
                                      text[start:end])
        count += n
        for name in header_macros:
            if re.search(r"\b%s\b" % re.escape(name), code):
                used.add(name)
        pieces.append(code)
        last = end
    text = "".join(pieces)
    overrides = [name for name in sorted(used)
                 if not re.search(r"#[ \t]*define[ \t]+%s\b" % re.escape(name), text)]
    if overrides:
        includes = list(INCLUDE_LINE.finditer(text))
        at = includes[-1].end() if includes else 0
        block = "\n/* port: DS shared-area macros from Aletheia headers */\n"
        for name in overrides:
            value = header_macros[name]
            block += "#undef %s\n#define %s %s\n" % (name, name, shared_address(value[-3:]))
        text = text[:at] + block + text[at:]
        count += len(overrides)
    if count:
        text = SHARED_DECL + text
    return text, count


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


def patch_text(text, removed=(), fixups=(), header_macros=None):
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
    if header_macros is not None:
        text, n = rewrite_shared_area(text, header_macros)
        count += n
    return text, count


def main():
    """Copy each requested source into the output tree with rewrites applied."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path)
    parser.add_argument("--remove-asm", action="append", default=[],
                        help="delete the named Metrowerks asm body; its C "
                             "definition is supplied by the port")
    parser.add_argument("--fixup", action="append", default=[],
                        help="apply a named DS-hardware fixup from PORT_FIXUPS")
    parser.add_argument("--list-shared", action="store_true",
                        help="print every .c under --source that names the DS "
                             "shared area, and exit")
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()

    header_macros = header_shared_macros(args.source)
    if args.list_shared:
        for path in sorted(args.source.rglob("*.c")):
            text = path.read_text(encoding="utf-8", errors="replace")
            if rewrite_shared_area(text, header_macros)[1]:
                print(path.relative_to(args.source).as_posix())
        return
    if not args.files or args.out is None:
        parser.error("--out and at least one file are required")

    for name in args.files:
        source = args.source / name
        text = source.read_text(encoding="utf-8")
        patched, count = patch_text(text, args.remove_asm, args.fixup, header_macros)
        target = args.out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(patched, encoding="utf-8", newline="\n")
        print("patched %s (%d rewrite%s)" % (name, count, "" if count == 1 else "s"))


if __name__ == "__main__":
    main()
