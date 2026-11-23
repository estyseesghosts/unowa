#!/usr/bin/env python3
"""Build the flat DS main-RAM image from the player's ROM extract.

The game reads and writes DS data by fixed address: Aletheia's sources declare
almost every data object as an extern at its DS address (`* name 0xADDR data`
rows in the file headers), and the bytes live in the ROM, not in the C. unowa
keeps those bytes where the DS kept them: in a flat copy of DS main RAM,
sinjoh's `NitroPort_MainMem` (engine/compat/nitro/src/port_memory.c:19-22).
DS address `0x02xxxxxx` is `NitroPort_MainMem + (addr & 0x7FFFFF)`, the same
mapping sinjoh's shared area uses (`HW_MAIN_MEM + 0x7ffxxx`, mmap_shared.h).

This tool writes three files:

  flat_names.ld   `PROVIDE(name = NitroPort_MainMem + off);` for every data
                  name in main RAM. A name is a linker symbol at its byte, so
                  there is no per-object size to guess, aliases are one value,
                  and code reading past a declared size reads the real
                  neighbour (docs/eidos-review.md section 1). PROVIDE defines a
                  name only when no object defines it, so a port or SDK
                  definition still wins.
  flat_image.bin  The data bytes of every module, copied from the ROM extract:
                  each module's data tail (Aletheia layout/data_objects.json
                  `range`) and any named data inside its code range.
  flat_image.c    Tables for engine/runtime/flat_memory.c: per module, its DS
                  window, the spans to copy, the pointer words to relocate.

Pointer words are taken from Aletheia's typed object map, not guessed from
values: a word is a pointer when data_objects.json types it as a `ptr` field
or the object is a `pointer_table` (eidos-review.md section 3). Words in a
struct, or in an object with a `targets` list, are "holder" words: they may
name a function in any overlay that resolves uniquely, as `ptr` words may
(save table entry 51 names ov010). A value-based rule covers words the map
leaves untyped (`raw`/`zero`, `u32`/`s32` numerics, which the map assigns from
literal-pool loads alone), as Eidos's armdata.md does, and it accepts exact
matches only: the entry of a function in the same module or ARM9, or the
start of a named data object of the same module or ARM9 inside the DS module
windows (`_MainSaveDesc` -> `_MainSaveTable`). Objects the source declares as
halfword arrays are excluded (narrow_arrays). A pointer is then one of

  * flat: the target is DS main RAM with no native definition. The runtime
    rewrites the word in place to `MainMem + (value & 0x7FFFFF)`, so the
    table only lists the word's address;
  * native: the target is a function, or data the port defines natively. The
    table stores the native address (a symbol reference resolved by ld).

Each module's bytes stop at its `ram_end`; the generator refuses an image
with a span or relocated word outside that (check_module_bounds).

The output lives in the gitignored build tree: it is derived from the
player's own ROM and is never committed.

Usage:
    python tools/gen_flat_image.py --aletheia DIR --native FILE --out-dir DIR
"""

import argparse
import bisect
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_port_ioreg_header import REGIONS as IOREG_REGIONS  # noqa: E402
from patch_sources import overlay_namespaces  # noqa: E402

SYMBOL_RE = re.compile(r"\*\s+([A-Za-z_]\w*)\s+0x([0-9A-Fa-f]{8})\s+(data|arm|thumb)")
# Placeholder names spell their address, sometimes with a suffix:
# `g_021EE724_ref` (ov021/scrcmd_c03center.c:81) is gCameraTurnAngle.
AUTONAME_RE = re.compile(r"\b(?:d|g|data)_([0-9A-Fa-f]{8})(?:_\w+)?\b")
MAIN_RAM_LOW = 0x02000000
MAIN_RAM_HIGH = 0x03000000
FLAT_MASK = 0x7FFFFF
# The game's DTCM window of 16 KB (crt0.c:43, crt0.c:278); see host_location.
DTCM_LOW = 0x02FE0000
DTCM_HIGH = 0x02FE4000
# OS structures inside it that sinjoh's libnitro also owns. There is one
# hardware table/queue, so both names must resolve to the same memory.
IRQ_TABLE_LOW = 0x02FE0020   # OS_IRQTable, 32 entries (os_interrupt.c:25)
IRQ_TABLE_HIGH = 0x02FE00A0
IRQ_QUEUE_LOW = 0x02FE00A0   # OSi_IrqThreadQueue (os_interrupt.c:51)
# A named object inside a code range is copied up to the next function entry,
# never further than this. Every such name found so far is a crt0 parameter
# block, a version string or an SDK table well under it.
CODE_AREA_SPAN_MAX = 0x1000

BLOCKLISTS = ("game_blocklist.txt", "game_overlay_blocklist.txt")
ENGINE = pathlib.Path(__file__).resolve().parent.parent / "engine"

# Names the port defines on purpose even though the ROM has bytes for them;
# see engine/stubs/port_stubs.c for each reason.
PORT_DEFINED = {
    "MATHi_SHA1ProcessMessageBlockFunc",
    "_wifiDeviceTable",
}

# Object kinds whose words are never pointers. A string or a typed numeric
# array can hold any bit pattern, including one equal to a function entry.
# A `u32` numeric object is not one of them: the object map gives that type
# from literal-pool evidence alone, and it covers real pointer tables, such
# as entry 0 of the save block table (0x0209DA2C -> BOX_SAVEDATA_GetSize),
# the ov025 weather callbacks and the ov093 battle process (BTL_MAIN_Proc*).
# Its words are untyped, so only the exact-match rules apply.
NEVER_POINTER_KINDS = {"string", "numeric"}
WORD_NUMERIC_ELEMENTS = {"u32", "s32"}
# Byte/halfword arrays that Aletheia typed from literal-pool evidence but
# which can hold 32-bit function pointers (map-attr and battle-step tables).
NARROW_NUMERIC_ELEMENTS = {"u8", "s8", "u16", "s16"}

# An array declaration whose element is a 16-bit scalar:
# `extern const u16 g_0209EAA0[];`. Byte arrays are not included: Aletheia
# declares opaque data as `u8 name[]` (3381 objects matched with them, among
# them typed pointer tables), so only a halfword element is evidence of type.
NARROW_ARRAY_RE = re.compile(
    r"\b(?:u16|s16|vu16|short|unsigned\s+short)\s+(?:const\s+)?([A-Za-z_]\w*)\s*\[")


def ds_flat_offset(address):
    """Offset into NitroPort_MainMem for a DS main-RAM address."""
    return address & FLAT_MASK


def in_main_ram(value):
    return MAIN_RAM_LOW <= value < MAIN_RAM_HIGH


def load_layout(aletheia):
    """Return (modules, data_map).

    `modules` is a list of dicts in table order: index 0 is ARM9, index 1 + id
    is overlay id. Each has the DS window (`ram_start`, `ram_end`, `bss_end`),
    the start of its data tail, and the decompressed image bytes (None when the
    extract lacks it).
    """
    layout = json.loads((aletheia / "build" / "black" / "layout" / "layout.json").read_text())
    data_map = json.loads((aletheia / "layout" / "data_objects.json").read_text())["regions"]
    extract = aletheia / "extract"

    arm9 = layout["arm9"]
    static = arm9["static"]
    modules = [{
        "key": "arm9",
        "ram_start": static["ram_start"],
        "ram_end": static["ram_end"],
        "bss_end": static["bss_end"],
        "sinit_start": 0,
        "sinit_end": 0,
        "data_start": int(data_map["arm9"]["range"][0], 16),
        "image": (extract / arm9["decompressed_file"]).read_bytes(),
    }]
    for index, overlay in enumerate(layout["overlays"]):
        if overlay["id"] != index:
            sys.exit("overlay table is not dense at id %d" % overlay["id"])
        key = "ov%03d" % overlay["id"]
        path = extract / overlay["decompressed_file"]
        modules.append({
            "key": key,
            "ram_start": overlay["ram_start"],
            "ram_end": overlay["ram_end"],
            "bss_end": overlay["bss_end"] if overlay.get("bss_size") else overlay["ram_end"],
            "sinit_start": overlay.get("static_init_start", 0),
            "sinit_end": overlay.get("static_init_end", 0),
            "data_start": int(data_map[key]["range"][0], 16) if key in data_map
                          else overlay["ram_end"],
            "image": path.read_bytes() if path.exists() else None,
        })
    return modules, data_map


def module_of(relative):
    """Module key for a `src/game/` relative path: `arm9` or `ovNNN`."""
    top = relative.split("/")[0]
    return "arm9" if top in ("arm9", "arm9_autoload0_ITCM") else top


def data_rows(aletheia):
    """Every `* name 0xADDR data` header row as (name, address, module, file).

    `file` is the `src/game/`-relative path of the row's file. The row belongs
    to that file: Aletheia writes the rows into the header comment of the TU
    (or header) whose code uses the name.
    """
    rows = []
    root = aletheia / "src" / "game"
    for path in root.rglob("*"):
        if path.suffix not in (".c", ".h"):
            continue
        relative = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        named = set()
        for match in SYMBOL_RE.finditer(text):
            named.add(match.group(1))
            if match.group(3) == "data":
                rows.append((match.group(1), int(match.group(2), 16),
                             module_of(relative), relative))
        # Aletheia's placeholder names spell their address (`d_02186E84`,
        # used in ov023/oekaki.c:347 with no header row). Bind those too.
        for match in AUTONAME_RE.finditer(text):
            if match.group(0) not in named:
                named.add(match.group(0))
                rows.append((match.group(0), int(match.group(1), 16),
                             module_of(relative), relative))
    return rows


def narrow_arrays(aletheia, rows):
    """DS addresses of objects the source declares as 8- or 16-bit arrays.

    Such an array cannot hold a 32-bit pointer, whatever the object map says:
    `g_0209EAA0` is `raw` in data_objects.json but `extern const u16
    g_0209EAA0[]` in arm9/file_util.c:213, a TM-to-move table whose halfword
    pairs read as `0x020A000E` and would otherwise match a named object. The
    declaration and the header row are taken from the same file, because a
    name can mean different storage in different files.
    """
    by_file = {}
    for name, address, _module, relative in rows:
        by_file.setdefault(relative, {}).setdefault(name, set()).add(address)
    root = aletheia / "src" / "game"
    addresses = set()
    for relative, names in by_file.items():
        text = (root / relative).read_text(encoding="utf-8", errors="replace")
        for match in NARROW_ARRAY_RE.finditer(text):
            addresses |= names.get(match.group(1), set())
    return addresses


def data_names(rows):
    """Map name -> sorted distinct DS addresses."""
    names = {}
    for name, address, _module, _file in rows:
        names.setdefault(name, set()).add(address)
    return {name: sorted(addresses) for name, addresses in names.items()}


def per_file_name(name, address):
    """Linker name for one address of a name that several files bind differently.

    tools/patch_sources.py maps the name to this in each TU whose header row
    gives that address, so `sVramBanks` in ov109/idle_screen.c and in
    ov171/dendou_view.c stay two objects.
    """
    return "%s__ds_%08X" % (name, address)


def compiled_sources(aletheia):
    """`src/game/...` paths the port compiles (engine/game.mk GAME_SRCS)."""
    root = aletheia / "src" / "game"
    blocked = set()
    for name in BLOCKLISTS:
        for line in (ENGINE / name).read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                blocked.add(line)
    return {
        "src/game/" + path.relative_to(root).as_posix()
        for path in root.glob("*/*.c")
        if path.relative_to(root).as_posix() not in blocked
    }


def function_table(aletheia, compiled, native):
    """Map module key -> {stored value: port symbol} for linkable functions.

    The stored value carries the ISA bit, as a DS pointer does. A function is
    linkable when its file is compiled and the final link defines its name
    (`native`); a static function has no external name and is reported, not
    guessed.
    """
    table = {}
    statics = {}
    text = (aletheia / "layout" / "function_addrs.tsv").read_text(encoding="utf-8")
    rows = [row.split("\t") for row in text.splitlines()[1:]]
    # The same rule tools/patch_sources.py applies to the sources: a name two
    # overlays define is `<region>_<name>` in every region but the first.
    namespaces = overlay_namespaces((row[0], row[2], row[6] == "yes") for row in rows)
    for region, address, name, isa, _size, source, static in rows:
        value = int(address, 16) | (1 if isa == "thumb" else 0)
        key = module_of(region)
        name = namespaces.get((key, name), name)
        if static != "no":
            if source in compiled:
                statics.setdefault(key, {})[value] = name
            continue
        # The final link defines the name (`native`), so the DS pointer can be
        # rewritten to it whether the Aletheia source is compiled or the
        # function comes from sinjoh's NitroSDK. Requiring `source in compiled`
        # left every SDK function out of the table (gx_load2d.c and friends are
        # provided by libnitro), so pointers to them fell through to the
        # data-object spans and resolved to a neighbouring symbol plus a byte
        # offset: ARC_UTIL_gPaletteLoadFns[4] became GX_LoadOBJPltt+8 instead
        # of GX_LoadOBJPltt, and calling it entered the function's second
        # instruction with r12 = 1.
        if name not in native:
            continue
        table.setdefault(key, {})[value] = name
    return table, statics


def typed_words(objects, narrow):
    """Yield (word address, how) for words the object map types as pointers.

    `how` is "ptr" for a typed pointer slot, "holder" for another word of an
    object the map shows holds pointers (a `struct`, an object with a
    `targets` list, or one whose `ev` evidence names `ptr`), and "maybe" for
    a word in an untyped object. A holder word accepts an overlay function
    the way a typed slot does: the map lists only ARM9 targets for the save
    block table at 0x0209DA30, and entry 51 there holds the ov010 pair
    FLDMMDL_GetMovedPosSaveSize/InitMovedPosSave in fields it types `u32`.
    The `ev` check matters for proc tables like g_021D58A8: Aletheia types it
    `numeric`/`u32` with `ev=ptr` and no `targets`, so without it the word is
    a conservative "maybe" whose same-module/ARM9 rule misses the ov010
    functions it points at, and the game jumps to the raw DS address. An
    untyped (`raw`/`zero`) object whose start is in `narrow` (narrow_arrays)
    is skipped; a typed object keeps the map's typing.
    """
    for obj in objects:
        start = int(obj["addr"], 16)
        size = int(obj["size"])
        kind = obj["kind"]
        if kind in NEVER_POINTER_KINDS and obj.get("elem") not in WORD_NUMERIC_ELEMENTS:
            if kind == "numeric" and obj.get("elem") in NARROW_NUMERIC_ELEMENTS:
                # Aletheia types some 32-bit function tables as byte/halfword
                # arrays from literal-pool evidence: the map-attribute table
                # at 0x021D3CE8 is `numeric`/`u16` but holds 0x021AB1AD =
                # MAP_ATTR_IsGimmickD7, and ov093's battle step table is
                # `numeric`/`u16` but holds BTL_CLIENT_Step* entries. The word
                # is reported "maybe_fn": build() relocates it only on an
                # exact resolve_function() match, never on a value guess.
                for offset in range((-start) % 4, size - 3, 4):
                    yield start + offset, "maybe_fn"
            continue
        if start in narrow and kind not in ("pointer_table", "struct"):
            continue
        if kind == "pointer_table":
            for offset in range((-start) % 4, size - 3, 4):
                yield start + offset, "ptr"
            continue
        ev = obj.get("ev") or ""
        other = ("holder" if kind == "struct" or obj.get("targets")
                 or "ptr" in ev.split(",") else "maybe")
        if kind == "struct":
            offset = 0
            for field, count in obj["fields"]:
                for _ in range(count):
                    yield start + offset, "ptr" if field == "ptr" else other
                    offset += 4
            continue
        for offset in range((-start) % 4, size - 3, 4):
            yield start + offset, other


def native_objects(names, native, modules):
    """Sorted (start, end, name) for data the port defines natively.

    Such an object lives in the ELF, not in the flat image, so a pointer to it
    must be rewritten to the native symbol. Its extent is the gap to the next
    named address, which is enough to attribute a pointer to it.
    """
    addresses = sorted({a for alist in names.values() for a in alist})
    spans = []
    for name, alist in names.items():
        if name not in native:
            continue
        for address in alist:
            position = bisect.bisect_right(addresses, address)
            end = addresses[position] if position < len(addresses) else address + 4
            spans.append((address, end, name))
    spans.sort()
    return spans


def find_native(spans, starts, value):
    index = bisect.bisect_right(starts, value) - 1
    if index >= 0 and spans[index][0] <= value < spans[index][1]:
        return spans[index][2], value - spans[index][0]
    return None


class Image:
    """Collects the blob, spans and relocations for every module."""

    def __init__(self):
        self.blob = bytearray()
        self.spans = []          # (module index, ds address, blob offset, size)
        self.flat = []           # (module index, ds address)
        self.native = []         # (module index, ds address, symbol, addend)
        self.sinit = []          # (module index, symbol) run at overlay load
        self.stats = {}

    def count(self, key, amount=1):
        self.stats[key] = self.stats.get(key, 0) + amount

    def add_span(self, module_index, address, data):
        while len(self.blob) % 4:
            self.blob.append(0)
        self.spans.append((module_index, address, len(self.blob), len(data)))
        self.blob.extend(data)


def code_area_spans(module, rows, function_starts):
    """DS ranges inside a module's code that hold data the module names.

    Only the module's own rows count: overlays share windows, so another
    overlay's data address can fall inside this one's code. Returns merged
    (start, end) pairs; each runs from the named address to the next function
    entry, capped at CODE_AREA_SPAN_MAX.
    """
    ranges = []
    for _name, address, module_key, _file in rows:
        if module_key != module["key"]:
            continue
        if not module["ram_start"] <= address < module["data_start"]:
            continue
        position = bisect.bisect_right(function_starts, address)
        end = function_starts[position] if position < len(function_starts) else address + 4
        end = min(end, address + CODE_AREA_SPAN_MAX, module["data_start"])
        ranges.append((address, max(end, address + 4)))
    ranges.sort()
    merged = []
    for start, end in ranges:
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def resolve_function(value, module_key, functions, window=None):
    """Port symbol for a stored function pointer, or None.

    The module's own functions win, then ARM9 (its addresses are unique); an
    address in another overlay's window is accepted only when exactly one
    overlay has a function there.

    A value inside the current module's own [ram_start, bss_end) window is
    never another overlay's function: overlays share address windows, so the
    one-overlay fallback would rewrite the module's own data pointer to a
    same-numbered function of a different overlay. ov088's title animation
    table at 0x021BD3E4 is exactly that shape (DWCi_FOCUSlInit is at
    0x021BD3E4 in ov016). The word then belongs to the data pass, which
    rebases it into this module's image.
    """
    for key in (module_key, "arm9"):
        name = functions.get(key, {}).get(value)
        if name is not None:
            return name
    if window is not None and window[0] <= value < window[1]:
        return None
    found = {table[value] for key, table in functions.items()
             if key not in (module_key, "arm9") and value in table}
    if len(found) == 1:
        return found.pop()
    return None


def data_starts(data_map, rows):
    """{module key: set of DS addresses where a named data object starts}.

    Names come from Aletheia's header rows and its object map. An untyped
    word is treated as a data pointer only when it equals one of these, the
    same exact-entry rule the function path uses: `_MainSaveDesc` (typed
    `raw`) holds `&_MainSaveTable`, while a coincidental number in a raw
    blob almost never lands exactly on a named object.
    """
    starts = {}
    for _name, address, module_key, _file in rows:
        starts.setdefault(module_key, set()).add(address)
    for module_key, region in data_map.items():
        for obj in region.get("objects", []):
            starts.setdefault(module_key, set()).add(int(obj["addr"], 16))
    return starts


def build(modules, data_map, rows, functions, statics, native_spans, narrow):
    image = Image()
    named_data = data_starts(data_map, rows)
    # A cross-overlay data pointer can target the interior of another module's
    # struct, not a named start: gWeatherTable[].taskParam (ov021) points into
    # ov024's WeatherKindDef at 0x021EE8C0. The exact named-start rule misses
    # it and leaves a raw DS address the ARM11 process cannot read. A value
    # inside a struct/pointer_table object is structural evidence of a pointer;
    # only that shape is accepted, so numeric values landing in opaque blobs
    # are still left alone.
    strong_objects = []
    for key, region in data_map.items():
        for obj in region.get("objects", []):
            if obj.get("kind") in ("struct", "pointer_table"):
                start = int(obj["addr"], 16)
                strong_objects.append((start, start + max(int(obj["size"]), 4)))
    windows_low = modules[0]["ram_start"]
    windows_high = max(module["bss_end"] for module in modules
                       if in_main_ram(module["ram_start"]))
    native_starts = [span[0] for span in native_spans]
    all_function_starts = {}
    for key, table in functions.items():
        all_function_starts[key] = sorted(value & ~1 for value in table)
    for key, table in statics.items():
        all_function_starts.setdefault(key, [])
        all_function_starts[key] = sorted(set(all_function_starts[key])
                                          | {value & ~1 for value in table})

    def sinit_symbol(value, module, functions):
        """Native name for one static-initialiser entry, or None.

        Aletheia files the decrypted copy of an overlay's listing under a
        `ovNNNd` region (function_addrs.tsv, ov230's loader is one), so the
        suffix is accepted alongside the module's own table.
        """
        key = module["key"]
        if key in functions and value in functions[key]:
            return functions[key][value]
        if key + "d" in functions and value in functions[key + "d"]:
            return functions[key + "d"][value]
        return None

    for index, module in enumerate(modules):
        image_bytes = module["image"]
        if image_bytes is None:
            image.count("modules without an extracted image")
            continue
        base = module["ram_start"]
        # The ARM9 file carries the autoload blocks (ITCM code, DTCM data)
        # after the static image: 0x020A9E80..0x020AA7C0 in Black. The DS
        # copies them to the TCMs, so main RAM above ram_end is BSS, which
        # those bytes would overwrite (_SndSlots at 0x020AA1B8).
        tail_end = min(base + len(image_bytes), module["ram_end"])
        ranges = code_area_spans(module, rows, all_function_starts.get(module["key"], []))
        if module["data_start"] < tail_end:
            ranges.append((module["data_start"], tail_end))
        for start, end in ranges:
            image.add_span(index, start, image_bytes[start - base:end - base])
            image.count("bytes copied", end - start)

        def word_at(address):
            offset = address - base
            return int.from_bytes(image_bytes[offset:offset + 4], "little")

        objects = data_map.get(module["key"], {}).get("objects", [])
        for address, how in typed_words(objects, narrow):
            if not module["data_start"] <= address <= tail_end - 4:
                continue
            value = word_at(address)
            if value == 0:
                continue
            if how == "maybe_fn":
                # A word from a narrow (`u8`/`u16`) numeric blob. Relocate only
                # an exact function entry; resolve_function's window guard
                # keeps a same-numbered data word of this module from being
                # rewritten to another overlay's function. Every other word
                # stays exactly as Aletheia copied it from the ROM.
                function = resolve_function(value, module["key"], functions,
                                            (module["ram_start"], module["bss_end"]))
                if function is not None:
                    image.native.append((index, address, function, 0))
                    image.count("function pointers in narrow numeric tables")
                continue
            if how in ("ptr", "holder"):
                function = resolve_function(value, module["key"], functions,
                                            (module["ram_start"], module["bss_end"]))
            elif module["key"] == "arm9":
                # Untyped ARM9 word: only an exact entry of ARM9 itself. 6228
                # ARM9 data words happen to equal some overlay's entry, so the
                # cross-overlay fallback is not safe here (docs/status.md).
                function = functions.get("arm9", {}).get(value)
            else:
                # An untyped word in an overlay may still be a function pointer
                # into another overlay: ov010's map-attr slope table at
                # 0x02171490 holds MAP_ATTR_IsSlopeXA (ov021), and ov021's
                # field-gimmick tables hold functions in ov057. The same
                # resolve_function guard as ptr/holder applies — exact entry,
                # no match inside this module's own window, and uniqueness
                # across overlays — so a numeric word cannot be guessed into
                # a pointer. Measured 81 new matches, all named function
                # tables; ARM9 stays conservative above.
                function = resolve_function(value, module["key"], functions,
                                            (module["ram_start"], module["bss_end"]))
            if function is not None:
                image.native.append((index, address, function, 0))
                image.count("function pointers")
                continue
            if how != "ptr":
                # Only targets inside the module windows: a header row can name
                # a linker address outside them (`SDK_AUTOLOAD_DTCM_START`,
                # 0x02FE0000, arm9/crt0.c:43), and a glyph bitmap at 0x020A0DB8
                # holds exactly that value.
                if not windows_low <= value < windows_high:
                    continue
                own = named_data.get(module["key"], set())
                if value in own or value in named_data.get("arm9", set()):
                    image.count("untyped words naming data (relocated)")
                elif module["ram_start"] <= value < module["bss_end"]:
                    image.count("untyped words inside own module, not a named start (left)")
                    continue
                elif value & 3 == 0 and any(start <= value < end
                                           for start, end in strong_objects):
                    image.count("flat pointers into another module's struct")
                else:
                    continue
            static =(statics.get(module["key"], {}).get(value)
                      or statics.get("arm9", {}).get(value))
            if static is not None:
                image.count("pointers to static functions (left as DS values)")
                print("  static target %s at 0x%08X in %s" % (static, address, module["key"]))
                continue
            if not in_main_ram(value):
                image.count("typed pointers outside main RAM (left)")
                continue
            target = find_native(native_spans, native_starts, value)
            if target is not None:
                image.native.append((index, address, target[0], target[1]))
                image.count("pointers to native data")
                continue
            if value & 1 and value - 1 in set(all_function_starts.get(module["key"], [])):
                image.count("pointers to unlinked functions (left)")
                continue
            image.flat.append((index, address))
            image.count("flat data pointers")

        # Static initialisers are code the SDK calls on every load
        # (FS_StartOverlay's sinit loop, fs_overlay.c:403, calls through
        # sinit_init..sinit_init_end). The flat image is a copy of DS main
        # RAM, not executable memory, so an entry is usable only when it
        # resolves to a native function. A table with any unresolved entry is
        # refused whole: the entries run in order and may depend on each
        # other, so a partial run is not the DS's initial state.
        if module["sinit_end"] > module["sinit_start"]:
            resolved = []
            missing = []
            for address in range(module["sinit_start"], module["sinit_end"], 4):
                if not module["ram_start"] <= address <= tail_end - 4:
                    continue
                value = word_at(address)
                if value == 0:
                    continue
                symbol = sinit_symbol(value, module, functions)
                if symbol is None:
                    missing.append(value)
                else:
                    resolved.append(symbol)
            if missing:
                image.count("modules whose static initialisers are not run")
                print("  %s static initialisers not run, no native code: %s"
                      % (module["key"],
                         " ".join("0x%08X" % value for value in missing)))
            else:
                for symbol in resolved:
                    image.sinit.append((index, symbol))
                image.count("static initialisers", len(resolved))
    return image


def check_module_bounds(modules, image):
    """Refuse an image that writes outside a module's loaded bytes.

    load_module (engine/runtime/flat_memory.c) zeroes ram_end..bss_end and
    then copies the spans, so a span past ram_end overwrites BSS the game
    expects to be zero. That happened with the ARM9 autoload blocks:
    _SndSlots[0].handle took 0xDFECD4CA from ITCM code and
    GFL_USE_InitSndStack (gfl_use.c:1363) stored through it at frame 25.
    Relocated words outside the spans would patch bytes the module never
    owned.
    """
    problems = []
    for index, address, _offset, size in image.spans:
        module = modules[index]
        if not module["ram_start"] <= address <= address + size <= module["ram_end"]:
            problems.append("%s span 0x%08X+0x%X outside 0x%08X..0x%08X"
                            % (module["key"], address, size,
                               module["ram_start"], module["ram_end"]))
    covered = {}
    for index, address, _offset, size in image.spans:
        covered.setdefault(index, []).append((address, address + size))
    words = [(index, address) for index, address in image.flat]
    words += [(index, address) for index, address, _symbol, _addend in image.native]
    for index, address in words:
        if not any(start <= address <= end - 4 for start, end in covered.get(index, [])):
            problems.append("%s relocated word 0x%08X outside its spans"
                            % (modules[index]["key"], address))
    if problems:
        raise SystemExit("flat image out of bounds:\n  " + "\n  ".join(problems[:20]))


def host_location(address):
    """(array symbol, offset) backing DS address `address`, or None.

    Main RAM and its mirrors are the flat image. The DS hardware ranges use
    the same arrays the port's I/O header redirects the SDK to
    (tools/gen_port_ioreg_header.py REGIONS): an extern named at an I/O
    register or in LCDC VRAM, where ov095 loads, must be the same storage.
    """
    # This game maps DTCM at 0x02FE0000 (arm9/crt0.c:43 names it
    # SDK_AUTOLOAD_DTCM_START; INITi_InitRegion at crt0.c:278 writes that base
    # into the DTCM base register). The same block is also called
    # SDK_AUTOLOAD_MAIN_TWL_START by the game (gfl_use.c:35, gf_overlay.c:29).
    # sinjoh's port has exactly one 16 KB block for it (port_memory.c:28), so
    # every DS name in [0x02FE0000, 0x02FE4000) must resolve there. Binding
    # it into MainMem instead splits one word in two: GFL_USE_VBlankHandler
    # sets the IRQ check flag through MAIN_TWL_START + 0x3FF8 (gfl_use.c:578)
    # while OS_GetIrqCheckFlag reads DTCM_START + 0x3FF8 (interrupt.h:155),
    # so OS_WaitIrq never wakes.
    #
    # Two structures inside the block are owned natively by libnitro, and the
    # game's reimplemented OS functions share them with it: NitroMain installs
    # GFL_USE_VBlankHandler through the game's OS_SetIrqFunction
    # (os_interrupt.c:67), which writes the table named g_02FE0020; the linked
    # OS_IrqHandler reads OS_IRQTable. Binding the table to the DTCM array
    # would leave the SDK's table all OS_IrqDummy.
    if IRQ_TABLE_LOW <= address < IRQ_TABLE_HIGH:
        return "OS_IRQTable", address - IRQ_TABLE_LOW
    if address == IRQ_QUEUE_LOW:
        return "OSi_IrqThreadQueue", 0
    if DTCM_LOW <= address < DTCM_HIGH:
        return "SDK_AUTOLOAD_DTCM_START", address - DTCM_LOW
    if in_main_ram(address):
        return "NitroPort_MainMem", ds_flat_offset(address)
    for base, array, size in IOREG_REGIONS:
        if base <= address < base + size:
            return array, address - base
    return None


def write_linker_names(path, names, native):
    """PROVIDE a symbol at its backing byte for every DS data name."""
    lines = [
        "/* Generated by tools/gen_flat_image.py. DS data names bound to their",
        " * bytes in the flat main-RAM image; see the tool's header. */",
    ]
    conflicts = []
    for name in sorted(names):
        if name in PORT_DEFINED or name in native:
            continue
        located = [(a, host_location(a)) for a in names[name]]
        located = [(a, where) for a, where in located if where is not None]
        if not located:
            continue
        if len(located) > 1:
            conflicts.append((name, [a for a, _ in located]))
            for address, (array, offset) in located:
                lines.append("PROVIDE(%s = %s + 0x%06X);"
                             % (per_file_name(name, address), array, offset))
        array, offset = located[0][1]
        lines.append("PROVIDE(%s = %s + 0x%06X);" % (name, array, offset))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return conflicts


def write_tables(path, blob_path, modules, image):
    """Emit the module, span and relocation tables as C."""
    by_module = {}
    for entry in image.spans:
        by_module.setdefault(entry[0], {"spans": [], "flat": [], "native": []})["spans"].append(entry)
    for entry in image.flat:
        by_module.setdefault(entry[0], {"spans": [], "flat": [], "native": []})["flat"].append(entry)
    for entry in image.native:
        by_module.setdefault(entry[0], {"spans": [], "flat": [], "native": []})["native"].append(entry)

    symbols = sorted({entry[2] for entry in image.native})
    sinit_symbols = sorted({entry[1] for entry in image.sinit})
    lines = [
        "/* Generated by tools/gen_flat_image.py from the player's ROM extract.",
        " * Never edit, never commit. See engine/runtime/flat_memory.h. */",
        "#include \"runtime/flat_memory.h\"",
        "",
        "__asm__(\".section .rodata.unowa_flat_blob,\\\"a\\\"\\n\"",
        "        \".balign 4\\n\"",
        "        \".global unowa_flat_blob\\n\"",
        "        \"unowa_flat_blob:\\n\"",
        "        \".incbin \\\"%s\\\"\\n\"" % blob_path.as_posix(),
        "        \".previous\\n\");",
        "",
    ]
    for symbol in symbols:
        if symbol in sinit_symbols:
            continue
        lines.append("extern char %s[];" % symbol)
    for symbol in sinit_symbols:
        lines.append("extern void %s(void);" % symbol)
    lines.append("")

    spans, flats, natives, sinits, descriptors = [], [], [], [], []
    for index, module in enumerate(modules):
        entry = by_module.get(index, {"spans": [], "flat": [], "native": []})
        first_span, first_flat, first_native = len(spans), len(flats), len(natives)
        first_sinit = len(sinits)
        module_sinits = [symbol for owner, symbol in image.sinit if owner == index]
        spans.extend(entry["spans"])
        flats.extend(sorted(entry["flat"]))
        natives.extend(sorted(entry["native"], key=lambda item: item[1]))
        sinits.extend(module_sinits)
        descriptors.append((module, first_span, len(entry["spans"]), first_flat,
                            len(entry["flat"]), first_native, len(entry["native"]),
                            first_sinit, len(module_sinits)))

    lines.append("const unowa_flat_span unowa_flat_spans[] = {")
    for _index, address, offset, size in spans:
        lines.append("    { 0x%08Xu, 0x%06Xu, 0x%06Xu }," % (address, offset, size))
    lines.append("};")
    lines.append("const uint32_t unowa_flat_pointer_words[] = {")
    for row in range(0, len(flats), 6):
        lines.append("    " + " ".join("0x%08Xu," % entry[1] for entry in flats[row:row + 6]))
    lines.append("};")
    lines.append("const unowa_flat_native unowa_flat_native_words[] = {")
    for _index, address, symbol, addend in natives:
        target = symbol if addend == 0 else "%s + %d" % (symbol, addend)
        lines.append("    { 0x%08Xu, %s }," % (address, target))
    lines.append("};")
    # FS_StartOverlay calls these in order on every load. An empty table is
    # written as one null entry so the array has a definition; the module's
    # count is zero, so the SDK never reads it.
    lines.append("void (*unowa_flat_sinit[])(void) = {")
    if sinits:
        for symbol in sinits:
            lines.append("    %s," % symbol)
    else:
        lines.append("    0,")
    lines.append("};")
    lines.append("const unowa_flat_module unowa_flat_modules[] = {")
    for (module, span0, nspan, flat0, nflat, native0, nnative,
         sinit0, nsinit) in descriptors:
        lines.append("    { 0x%08Xu, 0x%08Xu, 0x%08Xu, %d, %d,"
                     " %d, %d, %d, %d, %d, %d }, /* %s */"
                     % (module["ram_start"], module["ram_end"], module["bss_end"],
                        sinit0, nsinit, span0, nspan, flat0, nflat, native0, nnative,
                        module["key"]))
    lines.append("};")
    lines.append("const uint32_t unowa_flat_module_count = %d;" % len(descriptors))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--aletheia", required=True, type=pathlib.Path)
    parser.add_argument("--native", required=True, type=pathlib.Path,
                        help="defined symbol names of every other link input, one per line")
    parser.add_argument("--out-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()

    native = set(args.native.read_text().split())
    modules, data_map = load_layout(args.aletheia)
    rows = data_rows(args.aletheia)
    names = data_names(rows)
    compiled = compiled_sources(args.aletheia)
    functions, statics = function_table(args.aletheia, compiled, native)
    native_spans = native_objects(names, native, modules)

    narrow = narrow_arrays(args.aletheia, rows)

    image = build(modules, data_map, rows, functions, statics, native_spans, narrow)
    check_module_bounds(modules, image)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    # Relative to the build directory: the assembler resolves .incbin against
    # its working directory, and the msys and Windows tools disagree on
    # absolute paths.
    blob_path = args.out_dir / "flat_image.bin"
    blob_path.write_bytes(bytes(image.blob))
    conflicts = write_linker_names(args.out_dir / "flat_names.ld", names, native)
    write_tables(args.out_dir / "flat_image.c", blob_path, modules, image)

    print("flat image: %d bytes in %d spans over %d modules"
          % (len(image.blob), len(image.spans), len(modules)))
    for key in sorted(image.stats):
        print("  %s: %d" % (key, image.stats[key]))
    for name, addresses in conflicts:
        print("  name with several addresses, bound per file: %s %s"
              % (name, " ".join("0x%08X" % a for a in addresses)))


if __name__ == "__main__":
    main()
