# What Eidos solved for the Gen V engine, and what unowa takes from it

Eidos (`../eidos`, pinned at `2319b45`) is a native PC/mobile port of Black
built on Aletheia. It boots natively to the title menu (eidos
`docs/boot.md`, "How far it gets"). This review reads its design documents
for the Gen V problems unowa keeps hitting. Per `CLAUDE.md`, nothing here
is ported: Eidos targets desktop hosts with memory and indirection to spare,
while unowa targets an Old 3DS. Each item says what Eidos found, whether it
holds on 3DS, and what unowa does instead.

## 1. Data: one flat main RAM, not one object per symbol

**Eidos** (`docs/armdata.md`). At reset it copies the player's
`arm9_dec.bin` into a flat main-RAM block at each byte's DS offset, zeroes
the `.bss` ranges, and binds every data name to its address. Its section
"Why not one host global per object" rejects unowa's current model for three
reasons:

- `.bss` objects have no sizes in the metadata, and matched code indexes past
  declared sizes on purpose (the 235-entry `_ArcPathTable` is two objects in
  `data_objects.json`). Every guessed size either overruns or overlaps.
- SDK code relies on contiguity (`SDK_AUTOLOAD_DTCM_START + 0x3FF8`, the
  stack arithmetic in `OS_InitThread`).
- Pointers in data reach bytes no symbol names (the archive table points at
  235 strings inside `PIo2`'s neighbourhood).

Every unowa fault since the data image existed is one of these: the 64-byte
BSS placeholders (`gGlyphExpandTable` spilling into `gFontState`), the alias
fix (`e25f128`), the module-ownership fix (`d022706`), `_ArcPathTable`.

**On 3DS.** sinjoh already allocates the flat block: `NitroPort_MainMem`,
8 MiB, aligned (`engine/compat/nitro/src/port_memory.c:19-22`). DS address
`0x02xxxxxx` maps to `NitroPort_MainMem + (addr & 0x7FFFFF)`. That covers
the 4 MiB NTR main RAM, the TWL DTCM autoload at `0x02FE0000` (+`0x7E0000`)
and the shared area at `0x02FFF000`/`0x027FF000` (+`0x7FF000`), which is the
mapping sinjoh's own `mmap_shared.h` uses. No new memory is needed.

**What unowa does differently.** Eidos binds names through a pointer
variable (`#define d_X (*bw_dsref_d_X)`), which costs a load per access, to
stay portable to MSVC. unowa links with GNU ld, so a name can be a linker
symbol defined *at* its byte (`d_020A6C58 = NitroPort_MainMem + 0xA6C58;`
in a generated linker script). Accesses compile to the same code as any
global, there are no per-object arrays to size, and aliases are just two
symbols with one value.

**One conflict to resolve first.** sinjoh's port sets the main arena's low
end to the start of `NitroPort_MainMem` (NitroSDK `os_arena.c:75-76`, under
`SDK_PORT_3DS`). The image must own the bottom of the block, so the arena has
to start where the DS starts it: after the static module and the overlay
windows. That also gives the game the heap size it was built for.

## 2. Literal addresses

**Eidos** (`docs/boot.md`, "Fixed addresses"; `docs/dsaddr-residue.md`).
`tools/dsaddr.py` rewrites every integer-to-pointer cast of a DS address into
`bw_ds_ptr(addr)`, including through file-local defines and register macros.
It classifies every literal it leaves alone: 857 sites in the matched
corpus, 272 needing a port answer.

**unowa.** Under the flat model a main-RAM literal becomes a constant offset
from `NitroPort_MainMem`, the same rewrite this session applied to the shared
area (`tools/patch_sources.py`, `SHARED_LITERAL`). Widening it from
`0x02FFFxxx` to all of `0x02000000-0x02FFFFFF` covers the next fault,
`net_err.c:468`'s `(const void *)0x020A7234`, and the 193 pointer casts of
game main-RAM addresses in 93 files (count over `src/game`, this session).
No runtime translation function is needed. Eidos's residue categories
(addresses passed as integers, range checks, masks) are a useful checklist
for the sites that are not casts.

## 3. Pointers stored in data

**Eidos.** Relocations come from metadata, not from scanning values:
`data_objects.json` pointer words plus the `-> symbol` annotations in
Aletheia's reassembly listings (`asm/arm9/*.s`). Before rewriting, each word
is checked against the listed target, and a mismatch panics. Code pointers
the metadata misses are found by exact equality with a function entry
(Thumb bit included) inside data ranges only. Eidos also identity-maps DS
addresses on 32-bit hosts (reserving host `0x02000000`) so unlisted data
pointers still work.

**On 3DS.** Identity mapping is impossible (coffeepot
`ctr/hardware/memory.md`). In exchange the flat model makes *data* pointer
relocation a constant: every main-RAM target is `NitroPort_MainMem +
(target & 0x7FFFFF)`, so no table has to know which emitted array holds the
target. The open question is only which words are pointers. The listings are
not on disk in the pinned checkout, so finding the Aletheia tool that
generates them is a prerequisite. unowa's `gen_data_image.py` already uses
the exact function-entry rule (`function_addrs.tsv`).

## 4. Overlays sharing load windows

**Eidos** (`docs/overlays.md`):

- Each overlay gets its own namespace through a generated forced-include
  header (`#define func_02155244 ov010_func_02155244`); matched files are not
  edited.
- A call into a window from outside it goes through a generated trampoline
  that asks the loader which overlay is resident and panics if the wrong one
  or none is.
- Overlay data is the overlay file copied into its window in flat main RAM at
  every load, `.bss` zeroed, then relocated. Whatever loaded last is what the
  window holds, as on the DS.

**On 3DS.** The flat model gives the data side for free (UNOWA-2 #3's "no
corruption between overlays sharing a window" falls out). Trampolines on
every cross-window call cost an indirect branch plus a residency check;
unowa should keep them to the calls that actually cross windows, which
Eidos's generator already identifies, rather than wrapping every overlay
function.

## 5. Arguments passed in registers (not yet checked in unowa)

**Eidos** (`docs/boot.md`, "Arguments in registers"). mwcc lets `f(); g();`
hand `f`'s result to `g` in r0, and a call with fewer arguments than the
callee takes passes the caller's r1-r3 through. Aletheia's C matches the
bytes either way, but GCC passes what the C says, so the callee reads
garbage. Eidos's `tools/regargs.py` found 2306 sites originally. Run
read-only against the pinned Aletheia this session, it reports **77** (rows
in the scratch run; mostly DWC/network error setters, `CURSOR_MOVE_Delete`,
`CARD_ReadRomHashImage`). Separately, about 290 functions are tail-call
thunks `void f(void) { g(); }` that rely on registers passing straight
through. GCC at `-O2` usually emits `b g` without touching r0-r3, but that
is not guaranteed.

This is a candidate for the unexplained frame-16 store of `1` into
`gGFLNetMain.session` (status.md): a net-library call reading an argument
it was never given would produce that pattern. The 77 sites should be
compared against what unowa compiles before more guessing.

## 6. The virtual ARM7 Gen V needs

**Eidos** (`docs/firmware.md`, `sdk/arm7/`) answers the PXI tags Gen V uses:
OS, PM, RTC, CTRDG, CARD, FS, SND, NVRAM (tag 4, firmware flash), MIC; it
deliberately leaves wireless unanswered. Before the game starts it writes
the user settings into the shared area (`0x02FFFC80`, 0x70 bytes), the MAC
at `0x02FFFCF4` and the channel mask at `0x02FFFCFA`, as the DS ARM7's boot
code does.

**unowa.** sinjoh's Gen IV `port_arm7.c` registers FS/RTC/PM/TP/SOUND only;
NVRAM is the logged unimplemented service. With the shared area now shadowed,
filling the owner-info block at boot is a small, cited change. The values
belong to the player's console, so they come from the 3DS's own system
settings (CFG), not a config file.

## Recommended order

1. Flat data model in `NitroPort_MainMem`: image copy at boot, linker-symbol
   names, arena moved to its DS position, literal rewrite widened to all main
   RAM. Retires the BSS sizing, alias and per-object relocation logic in
   `gen_data_image.py` (UNOWA-2 #2).
2. Relocation words from metadata, verified against the ROM word before
   rewriting (find Aletheia's listing generator first).
3. Audit the 77 register-argument sites against unowa's compiled set.
4. Overlay load: copy into the window and relocate on each load; cross-window
   trampolines only where calls cross (UNOWA-2 #3).
5. Owner info and NVRAM answers from 3DS CFG.
