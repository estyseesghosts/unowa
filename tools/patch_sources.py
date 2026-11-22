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
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_port_ioreg_header import REGIONS  # noqa: E402

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
    # Aletheia splits one original ov016 unit across these files. closeFadeStep
    # (0x021C3794) is defined static in sceneoptmovconfirm1.c and called from
    # sceneoptmovconfirm.c; DWCi_SceneOptMovWait is global in
    # sceneoptmovwait.c:121 but prototyped static in sceneoptmovconfirm1.c.
    # mwcc's single unit resolved both; separate GCC units need external
    # linkage. Found by scanning every compiled TU for a static prototype with
    # no body in the same file.
    "ov016_split_unit_caller": [
        ("static void closeFadeStep(void);", "void closeFadeStep(void);", 1),
    ],
    "ov016_split_unit_owner": [
        ("static void closeFadeStep(void);", "void closeFadeStep(void);", 1),
        ("static void closeFadeStep(void)\n{", "void closeFadeStep(void)\n{", 1),
        ("static void DWCi_SceneOptMovWait(void);", "void DWCi_SceneOptMovWait(void);", 1),
    ],
    # Diagnostic: count iterations of the game's own top-level loop. If this
    # stays at 0 while the DS V-blank counter advances, the main thread is
    # stuck in OS_WaitIrq and the idle thread is driving V-blanks alone.
    "mainloop_probe": [
        ("        OS_WaitIrq(1, 1);\n        GFL_USE_PostIrqUpdate();\n",
         "        OS_WaitIrq(1, 1);\n"
         "        extern unsigned int unowa_mainloop_count;\n"
         "        ++unowa_mainloop_count;\n"
         "        GFL_USE_PostIrqUpdate();\n", 1),
    ],
    # The switch is on NDMA[ch].cnt & 0x0F000000, so this case is the DS I/O
    # destination-region code 0x04000000, not a pointer to DISPCNT.
    "pm_ndma_case": [
        ("case REG_DISPCNT_ADDR:", "case 0x04000000:", 1),
    ],
    # The port maps HW_DB_OBJ_VRAM to a shadow-array address, which is not an
    # integer constant expression in C. These locals used it as an enum value.
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
SHARED_DECL = "extern unsigned char NitroPort_MainMem[];  /* port: DS main RAM, flat */\n"
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


# DS main RAM, flat. unowa keeps the DS main-RAM image at its DS offsets in
# NitroPort_MainMem (tools/gen_flat_image.py, engine/runtime/flat_memory.h), so
# a literal main-RAM address in code becomes the same offset into it. Only a
# literal used as an address is rewritten: one directly after a pointer cast.
# Literals in the range are also PXI command words, DMA flags and MD5
# constants (`0x02000000 | 0x6200` in arm9/pm.c, `0x02441453` in ov009/md5c.c)
# and keep their value; Eidos's dsaddr.py draws the same line
# (docs/eidos-review.md section 2).
#
# A literal that equals a function entry is code, not data: matched C passes
# overlay callbacks by address (`(WMCallbackFunc)0x021D2AA8` in
# ov016/dwc_mov_wh.c) and compares a stored function pointer with one
# (`tickFn == 0x021CDC0D` in ov093/btl_client.c). Behind a cast or a
# comparison it becomes the native function: by name through an assembler
# label, or, for a file-static function, through a global alias appended to
# the same TU.
MAIN_LITERAL = re.compile(r"(?<![\w.])0x(?:02|2)([0-9A-Fa-f]{6})[uUlL]*(?![\w])")
# Matched sources write a main-RAM address either as 0x02xxxxxx or with the
# leading zero dropped (0x20A8218 in nns_g2d_renderer_core.c:411). Both name
# the same DS address; the pointer-cast test below is what decides that it is
# an address at all.
# I/O, palette, VRAM and OAM address literals in the DS 0x04000000..0x07FFFFFF
# window. gen_port_ioreg_header.REGIONS is the single mapping table; the same
# pointer-cast rule as MAIN_LITERAL keeps DMA control words (0x04000000 |
# size/4, mi_dma.c:98) and region comparisons (mi_dma.c:301) at their values.
REGION_LITERAL = re.compile(r"(?<![\w.])0x0([4-7])([0-9A-Fa-f]{6})[uUlL]*(?![\w])")
POINTER_CAST_BEFORE = re.compile(r"\(\s*[A-Za-z_][\w\s]*(?:\*\s*(?:const\s*)?)+\)\s*\(?\s*$")
ANY_CAST_BEFORE = re.compile(r"\)\s*\(?\s*$")
COMPARE_BEFORE = re.compile(r"[!=]=\s*\(?\s*$")
DATA_ROW = re.compile(r"\*\s+([A-Za-z_]\w*)\s+0x([0-9A-Fa-f]{8})\s+data")
OVERLAY_ID = re.compile(r"\b(ov\d+_id)\b(?!\s*\[\s*\])")
# ov010/scrcmd_symbol.c:292 builds the name by pasting:
# `#define FS_OVERLAY_ID(name) ((uptr)name##_id)`, used as FS_OVERLAY_ID(ov098).
PASTED_OVERLAY_ID = re.compile(r"\bFS_OVERLAY_ID\s*\(\s*(ov\d+)\s*\)")
# Absolute linker symbols below this are values (ids, stack sizes), not
# addresses: the lowest DS address in any header row is ITCM, 0x01FF8000.
LINKER_CONSTANT_LIMIT = 0x10000


def overlay_namespaces(function_rows):
    """Map (region, name) -> port name for functions defined in several regions.

    ov117 and ov118 each carry their own pokemontrade_demo_common.c with the
    same eleven external names; the DS linker kept them apart because each
    overlay was linked alone. The first region (in sort order) keeps the name,
    every later one gets `<region>_<name>`. `function_rows` yields
    (region, name, is_static).
    """
    regions = {}
    for region, name, is_static in function_rows:
        if not is_static:
            regions.setdefault(name, set()).add(module_of(region))
    renamed = {}
    for name, found in regions.items():
        for region in sorted(found)[1:]:
            renamed[(region, name)] = "%s_%s" % (region, name)
    return renamed


def flat_address(value):
    """Port expression for DS main-RAM address `value` in the flat image."""
    return "((unsigned int)NitroPort_MainMem + 0x%06X)" % (value & 0x7FFFFF)


def module_of(relative):
    """Module key of a `src/game/`-relative path, as function_addrs.tsv names it."""
    top = relative.split("/")[0]
    return "arm9" if top in ("arm9", "arm9_autoload0_ITCM") else top


class SourceIndex:
    """Facts about the whole Aletheia tree that the per-file rewrites need."""

    def __init__(self, source_root):
        self.header_macros = header_shared_macros(source_root)
        self.functions = {}
        table = source_root.parent.parent / "layout" / "function_addrs.tsv"
        function_rows = []
        for row in table.read_text(encoding="utf-8").splitlines()[1:]:
            region, address, name, isa, _size, source, static = row.split("\t")
            key = module_of(region)
            value = int(address, 16) | (1 if isa == "thumb" else 0)
            self.functions.setdefault(key, {})[value] = (name, static == "yes", source)
            function_rows.append((region, name, static == "yes"))
        self.namespaces = overlay_namespaces(function_rows)
        addresses = {}
        constants = {}
        for path in source_root.rglob("*"):
            if path.suffix not in (".c", ".h"):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for name, value in DATA_ROW.findall(text):
                value = int(value, 16)
                if value < LINKER_CONSTANT_LIMIT:
                    constants.setdefault(name, set()).add(value)
                elif value >= 0x02000000:
                    addresses.setdefault(name, set()).add(value)
        # name -> value, or None when files disagree (ov_id is 67 in one file
        # and 114 in another); a file's own row then decides.
        self.constants = {name: (min(values) if len(values) == 1 else None)
                          for name, values in constants.items()}
        self.constant_use = (re.compile(r"\b(%s)\b(?!\s*\[\s*\])"
                                        % "|".join(sorted(map(re.escape, self.constants),
                                                          key=len, reverse=True)))
                             if self.constants else None)
        self.multi = {name for name, values in addresses.items() if len(values) > 1}

    def function_at(self, relative, value):
        """(name, is_static, source) for an exact function entry, or None."""
        region = module_of(relative)
        found = self.functions.get(region, {}).get(value)
        if found is None:
            region = "arm9"
            found = self.functions.get(region, {}).get(value)
        if found is None:
            return None
        name, is_static, source = found
        return self.namespaces.get((region, name), name), is_static, source


def static_alias(relative, name):
    """Global alias name for a file-static function named by DS address."""
    return "unowa_static_%s_%s" % (re.sub(r"\W", "_", relative[:-2]), name)


def rewrite_main_ram(text, relative, index):
    """Redirect main-RAM address literals in code. Returns (text, count)."""
    pieces = []
    count = 0
    last = 0
    references = {}
    aliases = {}

    def replace(match, code):
        nonlocal count
        value = int("02" + match.group(1), 16)
        before = code[max(0, match.start() - 80):match.start()]
        function = index.function_at(relative, value)
        if function is not None and (ANY_CAST_BEFORE.search(before)
                                     or COMPARE_BEFORE.search(before)):
            name, is_static, source = function
            symbol = name
            if is_static:
                if source != "src/game/" + relative:
                    raise SystemExit("%s: literal 0x%08X names static %s of %s"
                                     % (relative, value, name, source))
                symbol = static_alias(relative, name)
                aliases[symbol] = name
            reference = "unowa_fnref_%s" % symbol
            references[reference] = symbol
            count += 1
            return "((unsigned int)%s)" % reference
        if POINTER_CAST_BEFORE.search(before):
            count += 1
            return flat_address(value)
        return match.group(0)

    for start, end in code_spans(text):
        pieces.append(text[last:start])
        code = text[start:end]
        pieces.append(MAIN_LITERAL.sub(lambda m, code=code: replace(m, code), code))
        last = end
    text = "".join(pieces)
    if references:
        head = "".join("extern char %s[] __asm__(\"%s\");  /* port: DS code address */\n"
                       % (reference, symbol)
                       for reference, symbol in sorted(references.items()))
        text = head + text
    for symbol, name in sorted(aliases.items()):
        text += ("\n/* port: a literal above names this static function by DS address */\n"
                 "extern __typeof__(%s) %s __attribute__((alias(\"%s\")));\n"
                 % (name, symbol, name))
    return text, count


SWAP_BUFFERS_WRITE = re.compile(r"\breg_G3_SWAP_BUFFERS\s*=\s*([^;]+);")


def rewrite_swap_buffers(text):
    """Route direct DS geometry-swap writes through the port seam.

    G3D_SYSTEM_SwapBuffersMain stores the packed parameter into
    reg_G3_SWAP_BUFFERS (g3d_system.c:1194). On the DS that write *is* the
    swap. The port's geometry engine is a command stream, and its SDK entry
    point G3_SwapBuffers calls NitroPort_GxDirectWrite (g3imm.h:441); a plain
    store to the I/O shadow never reaches the engine, so no scene is ever
    published. Only this register is rewritten; other G3 registers keep their
    stores.
    """
    count = 0

    def replace(match):
        nonlocal count
        count += 1
        return "NitroPort_G3Swap(%s);" % match.group(1).strip()

    text = SWAP_BUFFERS_WRITE.sub(replace, text)
    if count:
        text = ("/* port: the DS geometry swap register is a store; route it\n"
                " * through the same command seam the SDK's G3_SwapBuffers uses. */\n"
                "extern void NitroPort_G3Swap(unsigned int value);\n") + text
    return text, count


def rewrite_port_regions(text):
    """Redirect DS I/O, palette, VRAM and OAM literals to the port shadows.

    Aletheia's matched sources sometimes name a register by literal rather
    than through sdk_ioreg.h (`*(vu16 *)0x04000050 = 0` in
    ov179/cdemo_scene.c:315, BLDCNT). The forced port header rewrites the
    header macros but cannot touch a literal, and a DS I/O address is unmapped
    in the ARM11 process. Only a literal directly behind a pointer cast is an
    address; control words and region codes keep their value.
    """
    pieces = []
    count = 0
    last = 0
    for start, end in code_spans(text):
        pieces.append(text[last:start])
        code = text[start:end]

        def replace(match):
            nonlocal count
            before = code[max(0, match.start() - 80):match.start()]
            if not POINTER_CAST_BEFORE.search(before):
                return match.group(0)
            value = int(match.group(0).rstrip("uUlL"), 16)
            for base, array, size in REGIONS:
                if base <= value < base + size:
                    count += 1
                    return "((uptr)%s + 0x%X)" % (array, value - base)
            return match.group(0)

        pieces.append(REGION_LITERAL.sub(replace, code))
        last = end
    return "".join(pieces), count


def namespace_functions(text, relative, index):
    """Give this file's overlay its own copy of a name another overlay defines.

    See overlay_namespaces. The definitions and every caller in the overlay
    see the same macro, so the overlay links to its own copy.
    """
    region = module_of(relative)
    code = "".join(text[start:end] for start, end in code_spans(text))
    lines = ["#define %s %s" % (name, renamed)
             for (owner, name), renamed in sorted(index.namespaces.items())
             if owner == region and re.search(r"\b%s\b" % re.escape(name), code)]
    if not lines:
        return text, 0
    block = ("/* port: overlay namespace, see tools/patch_sources.py overlay_namespaces */\n"
             + "\n".join(lines) + "\n")
    return block + text, len(lines)


def bind_file_names(text, index):
    """Bind names that differ between files to this file's own address.

    `sVramBanks` is 0x021BBA5C in ov109/idle_screen.c and 0x021DEB20 in
    ov171/dendou_view.c. tools/gen_flat_image.py emits one linker name per
    address; this maps the name to the one the file's header row gives.
    """
    rows = {}
    for name, value in DATA_ROW.findall(text):
        if name in index.multi:
            rows.setdefault(name, set()).add(int(value, 16))
    lines = []
    for name in sorted(rows):
        if len(rows[name]) != 1:
            raise SystemExit("one file gives %s two addresses" % name)
        if re.search(r"#[ \t]*define[ \t]+%s\b" % re.escape(name), text):
            continue
        lines.append("#define %s %s__ds_%08X" % (name, name, rows[name].pop()))
    if not lines:
        return text, 0
    # At the top: a binding must precede every declaration and use, and some
    # files declare their externs above a later #include (ov070/itemmenu.c).
    block = ("/* port: names bound per file, see tools/gen_flat_image.py */\n"
             + "\n".join(lines) + "\n")
    return block + text, len(lines)


def inline_linker_constants(text, index):
    """Replace each use of a small absolute linker symbol with its value.

    Overlay ids and stack sizes are absolute linker symbols (ov020_id is 0x14,
    src/matched/ov/ov010_func_0215FA00.c:13; also `ov_id_0A`, `abs_id_14`,
    `SDK_OVERLAY_ov021_ID`), declared `name[]` and passed as `(uptr)name`.
    GNU ld relocates that with R_ARM_ABS32 against the ABS symbol, and 3dsxtool
    refuses a target below the image base ("Relocation to invalid address").
    The value comes from the file's own header row when it has one, then from
    the tree; an `ovNNN_id` with no row is overlay NNN (every row agrees:
    ov020_id 0x14, ov088_id 0x58, ov179_id 0xB3). Declarators stay.
    """
    own = {name: int(value, 16) for name, value in DATA_ROW.findall(text)
           if int(value, 16) < LINKER_CONSTANT_LIMIT}
    count = 0
    pieces = []
    last = 0

    def value_of(name):
        if name in own:
            return own[name]
        value = index.constants.get(name)
        if value is not None:
            return value
        if OVERLAY_ID.fullmatch(name):
            return int(name[2:-3], 10)
        raise SystemExit("no single value for linker constant %s" % name)

    def rewrite(code, pattern):
        """Substitute uses in `code`; an `extern` declarator keeps its name and a
        `&name` (ov215/egg_demo_main.c declares one as plain `char`) is the
        same value: the symbol's address is the constant."""
        nonlocal count
        out = []
        last = 0
        for match in pattern.finditer(code):
            name = match.group(1)
            if pattern is OVERLAY_ID and name in index.constants:
                continue
            statement = code[max(code.rfind(";", 0, match.start()),
                                 code.rfind("{", 0, match.start()),
                                 code.rfind("}", 0, match.start())) + 1:match.start()]
            if re.match(r"\s*(?:#[^\n]*\n\s*)*extern\b", statement):
                continue
            start = match.start()
            ampersand = re.search(r"&\s*$", code[last:start])
            if ampersand is not None:
                start = last + ampersand.start()
            out.append(code[last:start])
            out.append("((unsigned char *)0x%X)" % value_of(name))
            count += 1
            last = match.end()
        out.append(code[last:])
        return "".join(out)

    def pasted(match):
        nonlocal count
        count += 1
        return "((uptr)((unsigned char *)0x%X))" % value_of(match.group(1) + "_id")

    for start, end in code_spans(text):
        pieces.append(text[last:start])
        code = text[start:end]
        if index.constant_use is not None:
            code = rewrite(code, index.constant_use)
        code = rewrite(code, OVERLAY_ID)
        code = PASTED_OVERLAY_ID.sub(pasted, code)
        pieces.append(code)
        last = end
    return "".join(pieces), count


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


def patch_text(text, relative, removed=(), fixups=(), index=None):
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
    if index is not None:
        text, n = inline_linker_constants(text, index)
        count += n
        text, n = bind_file_names(text, index)
        count += n
        text, n = namespace_functions(text, relative, index)
        count += n
        text, shared = rewrite_shared_area(text, index.header_macros)
        count += shared
        text, main_ram = rewrite_main_ram(text, relative, index)
        count += main_ram
        text, regions = rewrite_port_regions(text)
        count += regions
        text, swap = rewrite_swap_buffers(text)
        count += swap
        if (main_ram or regions) and not shared:
            text = SHARED_DECL + text
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
    parser.add_argument("--list", action="store_true",
                        help="print every .c under --source that the port-wide "
                             "rewrites change, and exit")
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()

    index = SourceIndex(args.source)
    if args.list:
        for path in sorted(args.source.glob("*/*.c")):
            relative = path.relative_to(args.source).as_posix()
            text = path.read_text(encoding="utf-8", errors="replace")
            if patch_text(text, relative, index=index)[1]:
                print(relative)
        return
    if not args.files or args.out is None:
        parser.error("--out and at least one file are required")

    for name in args.files:
        source = args.source / name
        text = source.read_text(encoding="utf-8")
        patched, count = patch_text(text, name, args.remove_asm, args.fixup, index)
        target = args.out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(patched, encoding="utf-8", newline="\n")
        print("patched %s (%d rewrite%s)" % (name, count, "" if count == 1 else "s"))


if __name__ == "__main__":
    main()
