# unowa status

Status values: implemented / compiled / tested headlessly / tested in emulator /
verified on Old 3DS. Do not collapse them.

## Milestones (UNOWA.md section 12)

| Milestone | State | Evidence | Notes |
|---|---|---|---|
| M0 — Bootstrap | implemented, compiled, tested in emulator | see below | native `.3dsx`, release/profile, async SD log, 30 Hz GPU presentation; both screens render the clear colour |
| M1 — Game compilation | implemented; compiled; linked; tested headlessly | `link-final` builds `m1-test.3dsx`; see below | boots, then faults on DS I/O in M2 territory |
| M2 — Native boot | complete in practice; tested in emulator | game's own `NitroMain` runs one tick per DS V-blank; 15–51 overlays load; the intro, title, new-game flow and first field map all execute original code. See the fault-chain table below. | Hardware verification and host32/3DS checkpoints absent; no exit-criterion blocker remains |
| M3 — 2D | substantially complete; rendering in LOTUS | DS 2D engine on the PICA: logos, copyright, full intro movie, title, start menu, name entry and field text/banners captured. Direct-colour affine BGs, OBJ, windows, blending and brightness work. | 256-colour bitmap BGs, WideArt side bands, intro vertical striping unvalidated; no full oracle corpus |
| M4 — 3D | in progress; major slice working in LOTUS | title 3D scene and lower-screen VRAM-display mode render; Nuvema Town field map loads and the game runs stably to `mainloop` 32,154 with the overlay log at 51, but the top screen is stuck in a full white fade with no 3D geometry (OPEN "Field stall" below). | `DISPCAPCNT` is approximated by live layers; flat BG VRAM view ignores bank remapping; title phase-2 shading not oracle-verified; battle/field coverage minimal |
| M5 — Gameplay | early in progress | title → start menu → NEW GAME → gender/name entry → Juniper/Cheren/Bianca intro → first field map, all driven by LOTUS D-pad/touch. Save device and ARM7 sound clock work. | Walking/collision/camera, save/continue, battles, most menus/scripts, real audio output. Next known limitation: 4 of 15 `gWeatherTable` taskParams for other weather kinds remain raw; recorded below |
| M6 — Widescreen | not started | | 400px, camera/culling, WideArt |
| M7 — Optimization | not started | | hardware profiling, budgets |
| M8 — Coverage | not started | | full regression corpus |
| M9 — Packaging | not started | | ROM prep, forwarders, docs |

## M0 evidence

Pins recorded in `upstream.lock`; git identity set locally to
`terezi <jules@foxtails.me>`. Aletheia clone at `50731dd`, Eidos clone at
`2319b45` are local references, not part of this repository.

Build (devkitARM r68-1 / gcc 16.1.0, libctru 2.7.1, citro3d 1.7.1), from
`tools/build.ps1 -AllProfiles`:

- `build/black/3ds/unowa-black.3dsx` (150052 bytes)
- `build/black/3ds-prof/unowa-black-dbg.3dsx` (150332 bytes)
- `build/white/3ds/unowa-white.3dsx` (150052 bytes)
- `build/white/3ds-prof/unowa-white-dbg.3dsx` (150332 bytes)

All four were rebuilt after adding `engine/runtime/abi_assert.c`, which proves
at compile time that the imported `kj_bridge.h` still has 32-bit pointers and
the documented struct layouts. The rebuilt instrumented Black binary was
reloaded into the LOTUS session (`lotus_load`) and run another 60 frames with
no error; both screens still showed the expected clear colours.

Emulator run (LOTUS v0.1.2 / CITnLO, windowed, primary card `unowa`,
session `01000000000000001860e84d5054ea18`, 120 guest frames, no error):

```text
--- new run ---
     0.000 [app] unowa-black-unknown-dirty-dbg, Old 3DS hardware held at Old 3DS baseline
     0.000 [app] edition=black, application region=98304 KiB, linear free=0 KiB, VRAM free=0 KiB
     0.000 [app] instrumented build: [pace] counters enabled
     0.021 [gpu] targets ready (linear free 30937 KiB, VRAM free 4794 KiB)
     0.021 [app] starting M0 presentation loop (30 Hz)
     0.539 [pace] iterations=16 presented=16 skipped=0, 30.87 present/s
     1.040 [pace] iterations=15 presented=15 skipped=0, 29.92 present/s
```

Two visible defects were found from that run:

1. Screen captures were pure black. Cause: the clear-only path never called
   `C3D_FrameDrawOn`, so citro3d did not transfer the targets at queue finish
   (sinjoh calls it in `engine/graphics/g2d/g2d.c`). Fixed in
   `engine/graphics/renderer.c`; all four `.3dsx` rebuilt.

   Re-verified in LOTUS v0.1.2, windowed, control enabled, primary card
   `unowa`, session `01000000000000008c51edc6cd54ea18`, asset `unowa`
   (`build/black/3ds-prof/unowa-black-dbg.3dsx`). `lotus_run_until` advanced
   120 frames with no error. `lotus_capture_image` returned a 400x240 top
   screen at `#1B2A4A`-style navy and a 320x240 bottom screen at
   `#181018`-style purple, matching the two alternating clear colours in
   `engine/app/main.c:108-109` and the bottom clear in `engine/app/main.c:109`.
   The guest log appended a second `--- new run ---` block with the same
   30.87/29.92 present/s counters. This closes the black-screen defect; it is
   emulator evidence only, not Old 3DS hardware evidence.
2. The log stops after 1.04 s although the session ran 120 frames. The host
   killed the guest at session stop, so the final `[app] loop ended` line was
   never flushed. Needs a clean shutdown observation in a later run.

## M1 source closure

Eidos is deliberately **not** used as a build dependency. Its tools are not a
foundation unowa is willing to inherit. The M1 path is Aletheia `src/game/`
plus sinjoh's NitroSDK/NitroSystem compatibility layer, both pinned in
`upstream.lock`.

Probe: devkitARM r68 `arm-none-eabi-gcc` compiled every `.c` under Aletheia
`src/game/arm9` and `src/game/arm9_autoload0_ITCM` with sinjoh's include paths
and SDK defines (`SDK_PORT_3DS`, `-fno-short-enums`, `-fsigned-char`,
`-march=armv6k -mtune=mpcore`, flags from sinjoh `engine/compat/common.mk:34-55,
75-91`). Result: **360 of 411 translation units compiled**. The 51 failures are
Metrowerks inline assembly in SDK/SVC/CW-runtime files that sinjoh's
`libnitro.a` already implements, plus two files needing generated `.inc` data.
They are excluded by name in `engine/game_blocklist.txt`, with reasons. The
build rule is `engine/game.mk`; it verifies the Aletheia and sinjoh revisions
before compiling.

A whole-archive link probe then found 107 further Aletheia SDK/NitroSystem
objects that compiled but duplicated symbols in sinjoh's `libnitro.a` and
`libnitrosystem.a` (card, FS, FX, GX, NNS, OS, PXI, sound, wireless). They are
blocklisted too; the archive now holds **255 game objects** (9.4 MiB).
`make -f engine/game.mk link-probe` first reports **882** undefined symbols.

Both user-supplied ROM dumps are present and match Aletheia's pinned SHA-1
(`docs/ROMS.md:12-13`): Black `26ad0b99…399a5`, White `bc696a0d…39208`.
They are hard-linked into the Aletheia checkout as `black.nds` / `white.nds`
and Black was extracted with `tools/ndsrom.py` and `tools/delink.py`.
`mwccarm` is now installed at `nintendo-homebrew/mwccarm/dsi/1.1p1` (CodeWarrior
4.0 build 1024), matching Aletheia's pin (`docs/FRONTEND-PINS.md:3`,
`docs/AGENT-BRIEF.md:152`), and is junctioned into
`aletheia/tools/mwccarm/mwccarm`. With `BW_ARM_TOOLCHAIN` set, `tools/env.py`
finds both binutils and the compiler. `arm9data.py` still stops: its link step
calls `gendata.generate_all()`, and the one missing entry is
`twl_0272EB9C` (`tools/gendata.py:33`), whose bytes come from the modcrypt-
encrypted `arm9i.bin`. Decrypting that needs a DSi key or BIOS dump.

unowa's `tools/gen_data_image.py` maps undefined symbols to DS addresses from
Aletheia's own source headers and reads the bytes from Aletheia's extracted
region images. It emits ROM-backed and zero-filled definitions into the
gitignored build tree. (Superseded by `tools/gen_flat_image.py` and removed;
see "The flat data model".)

Data fidelity spot-check: `_ArcPathTable` was emitted as exactly 940 bytes,
which is `0xEB` entries × 4 bytes, matching `ARC_TOOL_SetTable(_ArcPathTable,
0xEB)` in Aletheia `src/game/arm9/gfl_use.c:861`. ROM identity and repository
pins are re-checkable with `make -f engine/game.mk verify-pins verify-rom`.

The overlay translation units were then added. A probe of all 981 non-arm9
TUs compiled 938; the 43 failures (Metrowerks inline asm, missing generated
`.inc` files, GCC struct-size assertions in the GDS headers) are listed with
reasons in `engine/game_overlay_blocklist.txt`. Two further files were dropped
because their object names collide at whole-archive time (ov117/ov118 share
`pokemontrade_demo_common.c`, and ov029's `c_gear.c` duplicates a libnitro
symbol). The archive holds **1,191 objects** (58 MiB) and the data image
defines 6,548 ROM-backed and 194 zero-filled symbols. With sinjoh's
`engine/runtime/kj_g3.c` compiled in, the whole-archive probe reported 321
undefined symbols; `make -f engine/game.mk link-probe-real` uses normal archive
semantics (a root object calls `NitroMain`) and reported **247**. I verified
that removing the SDK blocklist does not help: even a normal-order link pulls
conflicting members from both archives, so those exclusions are required.

`tools/patch_sources.py` then rewrote the Metrowerks `asm { clz x, x }` pattern
in build-tree copies for four overlay TUs, and Aletheia's `tools/gendata.py`
produced the missing `h16_ov119_021E8190.inc`, so `ov119/pokemontrade_3d.c`
also compiles. `twl/dsp_process.c` was kept blocked: after the `clz` rewrite it
still needs TWL WRAM SDK functions (`MI_GetWramMapStart`,
`MI_AllocWramSlot`, …) absent from sinjoh's Gen IV headers. The archive now
holds **1,196 objects**, the data image 6,576 ROM-backed and 200 zero-filled
symbols, and `link-probe-real` reports **210**.

Of those 210, 27 are supplied by the final link: 7 from libgcc (`__aeabi_*`),
10 from newlib (`malloc`, `memset`, `fopen`, …), one from libm (`sqrt`), and 9
from libctru (`svcGetSystemTick`, `threadCreate`, `LightSemaphore_*`). That
left **183 genuinely undefined** functions/data.

`tools/rename_genv.py` resolved the Gen V / Gen IV collision. 104 blocked arm9
SDK TUs compile under devkitARM; the 40 selected ones define the symbols the
game needs. The tool prefixes the symbols they share with sinjoh's libs and
with devkitPro's libc/libm/libgcc/libctru (358 pairs) in both the Gen V objects
and the game archive, so the game selects its own implementations without a
duplicate definition. `--start-group` is needed because the two archives call
each other. A supplemental data image covers data references introduced by the
Gen V objects, filtered against the game-only image.

`engine/stubs/port_stubs.c` supplies what the DS got from linker symbols, BIOS
SVCs or MSL runtime: fixed-address values, `bw_limit_get`, the four `OV230`
call-through stubs, memory helpers, RC4, SHA-1 compression and HMAC-SHA1, the
`CPSi_big_*` limb readers, non-TWL console identity, and DS hardware no-ops.
`tools/patch_sources.py` removes the Metrowerks asm bodies that those C
definitions replace.

`make -f engine/game.mk TARGET=3ds link-probe-real` now reports **30 undefined**;
all 30 are supplied by libgcc, newlib, libm and libctru at final link.
`make -f engine/game.mk TARGET=3ds link-final` links a real ARM11 ELF
(`m1-test.elf`, 5.5 MiB) and `.3dsx` (`m1-test.3dsx`, 515 KiB) with GCC and
devkitPro's runtime. M1 is closed.

Headless boot is tested with the `unowa-m1` LOTUS asset (registered in
`~/lotus-data/citnlo/host.json`). The guest boots and runs two frames, then
stops on an unmapped DS I/O read. Each fault was symbolized with
`arm-none-eabi-addr2line` against `m1-test.elf`:

| Address | Function | Source | Fix |
|---|---|---|---|
| `0x04004000` | `OS_IsRunOnTwl` | `arm9/os_emulator.c:82` | port stubs return non-TWL; game's `OS_GetConsoleType` redirected to return `0x80000001` |
| `0x04004004` | `SCFG_SetCpuSpeed` | `arm9/scfg_proc.c:66` | patcher replaces `reg_SCFG_CLK` read with 0 |
| `0x04004008` | `GFL_USE_InitSdk` | `arm9/gfl_use.c:729` | non-TWL console type skips the SCFG block |
| `0x04000208` | `GFL_USE_InitSdk` | `arm9/gfl_use.c:732` | systematic DS memory redirect (below) |

sinjoh's SDK patch (`patches/NitroSDK/0001` and `0005`) redirects each DS
memory region to a shadow array in `port_ioreg.c` / `port_memory.c`. Aletheia's
game source names the same regions through `sdk_ioreg.h:17-23`. Coffeepot's
porting guide explicitly warns that redirecting an SDK macro does not catch a
source file's own address literals (`ctr/engineering/porting.md:92`). Therefore
`tools/gen_port_ioreg_header.py` derives a build-tree copy of Aletheia's header
with all 170 `IOREG_ADDR(...)` macros rewritten to the corresponding
`NitroPort_*` shadow array plus offset. A forced `-include` gives that copy the
original include guard, so every relative `#include "../sdk_ioreg.h"` in the
game sources resolves to the port mapping. The pinned checkout is untouched.

One consequence of the address rewrite is that a shadow-array pointer is not
an integer constant expression. Four sites in `ov094/btlv_input.c` and
`ov119/pokemontrade_2d.c` used `HW_DB_OBJ_VRAM` as an enum enumerator, so the
`vram_enum_base` fixup changes those locals to `uptr` initializers. The one
case label, `twl/pm.c:88`, switches on `NDMA[ch].cnt & 0x0F000000` and uses
`REG_DISPCNT_ADDR` as the DS I/O destination-region code, so the `pm_ndma_case`
fixup restores the literal `0x04000000`. No other file-scope, enum or case use
of a redirected macro exists in the game sources.

A clean build also exposed a make ordering bug: `$(file >...)`, which writes
the explicit GV object list, expands before its recipe's `mkdir`. The `ren`
directory is now an order-only prerequisite of the rename stamp.

`tools/audit_ds_literals.py` records the remaining exposure: 701 numeric DS
address literals in game source outside comments, including 31 in the I/O
range. Most are bit masks or SDK data addresses, so the raw count is an audit
list rather than a fault list; each reported site needs review when the boot
path reaches it.

After the memory redirect and those compile fixes, the first clean boot
succeeded: `lotus_run_until` reached 1200 frames with `last_error` empty and no
stop event. The PC now changes between samples, resolving to
`PXIi_HandlerRecvFifoNotEmpty` (`sinjoh/engine/compat/nitro/src/pxi.c:155`) and
`NitroPort_GxRefresh` (`port_gx.c:28`), and the guest has three live threads.
A 30-frame citrace captured 123,082 elements, so the game is doing sustained
OS/GX work rather than spinning. The screens remain black; rendering is not
wired up, which M2 does not require.

Two further fixes were needed to get there. `NitroPort_InitArm7Services()` —
the only registration of sinjoh's virtual ARM7 handlers — is called by kohjoh's
`kj_boot.c:112` but was absent from unowa's test entry; it now runs before
`NitroMain`. Then `PMi_WaitBusy` (`arm9/pm.c:160-171`) still spun because its
FIFO drain is gated on real ARM IRQ mode, which sinjoh never enters; the
`pm_waitbusy_drain` fixup makes the drain unconditional. That TU is a GV
source, so the patched copy is compiled with Aletheia's include paths first —
otherwise quoted includes resolve to sinjoh's SDK `mi_dma.h` and its
conflicting `u32` typedef.

### Input and overlay observation

The `PMi_WaitBusy` fix above was necessary but not sufficient: sinjoh's
`os_init.o` calls sinjoh's `PM_Init`, while the game's PM callers use the Gen V
state at `g_02153000`. Without initialising that state the game's lock is
never cleared. Because Aletheia's `PM_Init` reads the unmapped DS address
`0x02FFFC3C`, `unowa_pm_ensure_ready()` in `port_stubs.c` initialises the
documented layout directly and registers the Gen V callback; the patched
`PMi_WaitBusy` calls it before waiting.

With PM live the game reached further raw DS-address faults, each fixed by a
named fixup or port-owned definition:

- `OS_CreateExtraHeap` (`arm9/os_alloc.c:291`) hardcodes
  `(Cell *)0x02000000`. The `extra_heap_mainmem` fixup points it at a
  dedicated `unowa_extra_heap[0x4000]`. `NitroPort_MainMem` cannot be used: it
  starts at `0x4e1000`, exactly where sinjoh's SDK arena begins, and the
  0x4000-byte extra heap overwrites the arena records.
- `MATHi_SHA1ProcessMessageBlockFunc` has a ROM value of `0x02081e7c`, a raw
  DS Thumb address. `gen_data_image.py` now excludes it and `port_stubs.c`
  defines it as a pointer to the C `MATHi_SHA1Transform`, which also keeps
  that transform reachable under `--gc-sections`.
- `_wifiDeviceTable` is a ROM table of DS function pointers into ov009.
  `gen_data_image.py` excludes it and `port_stubs.c` supplies an inert
  51-slot table of stubs; `GFL_NET_SetupHeaps` calls slot 0 during boot.

A strong `NitroPort_OnFrame` in `port_stubs.c` forwards 3DS HID to the DS pad
and touch registers; sinjoh's weak version is empty and kohjoh's strong one is
not linked. The final link places the late objects inside `--start-group` so
`libnitro.a` is rescanned for those references.

**The LOTUS `buttons` mask is not libctru's key order.** It is the JSM/3DS
button order used by the input presets (`lotus_input_presets`): A=0x001,
B=0x002, X=0x004, Y=0x008, Up=0x010, Down=0x020, Left=0x040, Right=0x080,
L=0x100, R=0x200, Start=0x400, Select=0x800. Measured against the guest's
`KEYINPUT` (`NitroPort_IoReg + 0x130`): 0x001 -> A (0x3FE), 0x008 -> Y
(`held 0x800`), 0x040 -> Left (`0x3DF`), 0x400 -> Start. Injecting libctru's
0x008 for START is what made every earlier START test press Y.

Unsupported-service failures are observable too. `NitroPort_Unimplemented`
writes only to stderr, which LOTUS does not capture. The final link passes
`-Wl,--wrap=NitroPort_Unimplemented`; `__wrap_NitroPort_Unimplemented` in
`port_stubs.c` copies the service name into a bounded guest-RAM pool
(`unowa_unimplemented_log_names`, 64 slots x 32 bytes) and still calls
`__real_` so sinjoh's once-per-name behaviour is unchanged. Verified in the
emulator: after a clean 1200-frame boot `unowa_unimplemented_log_count == 6`,
each entry reading `ARM7 service: NVRAM (user settings)`. Those are the
explicit failures for the unimplemented NVRAM ARM7 service, not silent hangs.

The M0 renderer is wired into the M2 harness. `engine/game.mk` compiles
`renderer.c`/`log.c`/`paths.c` with the root Makefile's ARM11 flags; the test
entry calls `gfxInitDefault()` and `unowa_renderer_init()` before `NitroMain`,
and the strong `NitroPort_OnFrame` calls `unowa_renderer_present()` on every
other emulated V-blank (counter `unowa_present_count`); "The frame bridge"
below describes the pacing that owns the tick. This is the clear-only path
from `renderer.c:1-8`, not the G2D/PICA game bridge: it proves the frame loop
reaches the display, which is a prerequisite for M3/M4. LOTUS at 1800 frames:
`last_error` empty, `unowa_present_count == 130`, top screen RGB (16,24,48)
and bottom (24,16,24) — the renderer's clear colours. Game graphics still need
M3/M4.

### The flat data model

The game addresses its data by DS address, and Aletheia declares nearly every
data object as an extern at that address. The first data model emitted each
object as its own C array (`tools/gen_data_image.py`). That model needed a
guessed size for every object, one array per alias, and per-word relocation
into whichever array held the target. Each of those guesses caused a boot
fault: BSS placeholders spilled into neighbours, and aliased names went out of
sync. Overlays could not be loaded at all, because 237 overlays share 58 load
slots.

The flat model replaces it (`docs/eidos-review.md` sections 1 and 3). DS main
RAM is sinjoh's 8 MB `NitroPort_MainMem`, and DS `0x02xxxxxx` is
`NitroPort_MainMem + (address & 0x7FFFFF)`, the mapping sinjoh's shared area
already uses (`mmap_shared.h:40`). `tools/gen_flat_image.py` writes:

- `flat_names.ld`: `PROVIDE(name = NitroPort_MainMem + offset)` for every data
  name. A name is a linker symbol at its byte, so sizes are never guessed,
  aliases share one value, and a port definition still wins.
- `flat_image.bin`: each module's ROM bytes. That is its data tail
  `data_start..ram_end`, plus named data inside its code range.
- `flat_image.c`: per module (ARM9 is index 0, overlay `id` is `1 + id`), its
  DS window, the spans to copy, and the pointer words to relocate.

`engine/runtime/flat_memory.c` `load_module` zeroes `ram_end..bss_end`,
copies the spans and relocates the words. It runs for ARM9 in `main()`, and
for an overlay when the game loads it. The game loads through sinjoh's
overlay seams, which unowa wraps (`--wrap=NitroPort_OverlayInfo`,
`NitroPort_OverlayLoadImage`, `NitroPort_OverlayClearBss`), as
`FS_LoadOverlay` does on the DS. Code is native and linked once; only data
moves. `OS_InitArena` is wrapped so the main arena starts above every module
window.

**Pointer words.** Typing comes from Aletheia's `layout/data_objects.json`,
checked against exact values. The map's coverage is complete, but its types
come from weak evidence, so each rule requires an exact match:

| Class | Which words | Accepted value |
| --- | --- | --- |
| `ptr` | `ptr` fields, `pointer_table` objects | a function in the own module, ARM9, or one other overlay that resolves uniquely; any main-RAM data |
| holder | non-`ptr` words in a struct or in an object with a `targets` list | as `ptr`. Save table entry 51 holds ov010's `FLDMMDL_GetMovedPosSaveSize`/`InitMovedPosSave` in fields typed `u32` |
| untyped | `raw`/`zero` and `u32`/`s32` numerics | an exact function entry in the own module or ARM9, or the exact start of a named data object there, inside the module windows `0x02004000..0x02400060` |

Three facts from the boot set the untyped rule:
- `_MainSaveDesc` is typed `raw` and holds `_MainSaveTable`.
- The save table's first record lies in an object typed `numeric u32`
  (`BOX_SAVEDATA_GetSize`). The map types a word `u32` from literal-pool loads
  alone, which hides weather callback tables, process records and CLACT
  setters.
- Two lookalike values must be refused. The first is the u16 TM table
  `g_0209EAA0` (`file_util.c:213`), whose halfword pairs read `0x020A000E`.
  Objects the source declares as 16-bit arrays are skipped; byte arrays are
  not skipped, because `u8 name[]` is how Aletheia declares opaque data, and
  skipping them dropped 332 real function pointers. The second is a glyph
  word equal to the DTCM base `0x02FE0000`, which the window bound refuses.

Never pointers: `string` objects and `u8`/`u16` numerics.

A resolved word is either native (a function, or data the port defines; the
table stores the symbol) or flat (the runtime rewrites it to
`MainMem + (value & 0x7FFFFF)`).

**Module bounds.** A module's bytes stop at its `ram_end`. The ARM9 file has
`0x940` bytes of autoload blocks (ITCM code, DTCM data) after
`ram_end` `0x020A9E80`. Copied into main RAM, they overwrote ARM9 BSS after it
was zeroed, and `_SndSlots[0].handle` became `0xDFECD4CA`. That caused the
frame-25 fault and the CITnLO GPU abort (`docs/frame25-crash.md`).
`check_module_bounds` fails the build if any span or relocated word leaves its
module.

Current image: 380,596 bytes in 247 spans over 238 modules. It holds 7,205
function pointers, 2,982 flat data pointers and 17 pointers to native data,
and 53 untyped words name data. Two groups are left as they are: 210 typed
pointers whose value lies outside main RAM (VRAM, I/O and TCM addresses), and
109 untyped words inside their own module that name no data start.

One absolute symbol could not stay a `.set`. `ov020_id` is the constant `0x14`
(`src/matched/ov/ov010_func_0215FA00.c:13`). GNU ld relocates `(uptr)ov020_id`
with `R_ARM_ABS32` against the ABS symbol, and 3dsxtool refuses an absolute
target below the image base ("Relocation to invalid address").
`tools/patch_sources.py` inlines that one constant in `ov010/event_wifi_proc.c`.

### Compiler builtin substitution

The game archive compiles Aletheia's MSL runtime, including
`arm9/msl_string_str.c`, which defines `strlen`. devkitARM GCC 16 recognises the
definition as the `strlen` builtin and replaces its body with a call to
`strlen`; `-O2` tail-calls it, so the linked function is a single `b .`
infinite loop. `GF_STANDARD_GetStringLength` calls it, and the boot hung with
the main thread Ready and its PC frozen at `0x112804`. `PORT_CFLAGS` now passes
`-fno-builtin`: a decompiled runtime must keep its own implementations instead
of the compiler's libc substitutions. `-fno-builtin-strlen` alone does not fix
it, and the same substitution can hit any other builtin the MSL files define.

### The frame-16 stop under the old model

With the function table relocated the earlier `blx r12 = 0x0201DAA5` fault is
gone. `lotus_run_until(600)` now stops at frame 16 with an unmapped read at
`0x341`, reached through `s->ops->isActive()` in `GFL_NET_GetRunState`
(`net.c:438`), which the shutdown screen calls. The stack below it is
`APP_POWER_ClearCallback` <- `FLASH_ACCESS_Read(on_error)` <-
`BACKUP_SYSTEM_ReadFlash` <- `SAVE_OUTSIDE_Load` <- `SAVE_CONTROL_Create` <-
`GFL_USE_InitGameSystems`, so the shutdown screen is the response to a failed
save read, not an independent fault. Counters at the stop: `terminate_count`
0, `present_count` 0, `unowa_overlay_log_count` 7, `unimplemented` 0,
`pack_result` 0. The print-job overwrite is fixed; the network-state read is
the next failure on the same path.

Reading that fault exactly: `GFL_NET_GetRunState` compiles to

```
103234: ldr r0, [r3, #8]        @ gGFLNetMain.session
103238: cmp r0, #0
10323c: bxeq lr
103240: ldr r3, [r0, #832]      @ session->ops
103244: ldr r3, [r3, #108]      @ ops->isActive
```

`0x341` is `1 + 0x340`, so the singleton holds the value **1**, not a session
pointer and not null. Reading the guest confirms it: `gGFLNetMain` is
`0c 0 1 1` at the stop and all zero at frame 15, so the word is written during
frame 16, the same frame the shutdown path runs. `0xc` in `baseHeapId` and the
`1` in `ircParamA` are explained: `GFL_NET_SetupHeaps` stores the new base heap
id (it is called; an execute breakpoint at `0x102fd0` hits), and `bg_sys.c`
stores a layer scroll pair through `GFL_NET_SetIrcParams`.

The session field is not explained that way. An execute breakpoint at
`GFL_NET_StartSession` (`0x103050`) never hits before the stop, and that is
the only function in ARM9 that stores a pointer to `+8`; `GFL_NET_EndSession`
stores 0 there and does not run either. So the `1` is a stray store into the
singleton, not a net-lifecycle write. LOTUS write watchpoints are available
(`lotus_debug breakpoint_set ... kind:"write"`) but they switch the worker to
single-instruction dispatch, which makes the window from frame 15 to the fault
take tens of minutes, so the writer is still unidentified.

The save read itself was the next thing to fix, and it is fixed (below).

### The backup device

`CARD_LockBackup`, `CARD_IdentifyBackup`, `CARDi_RequestStreamCommand` and
`CARD_WaitBackupAsync` resolved into sinjoh's real NitroSDK `card_backup.c`,
which drives the DS card bus. ARM11 has no such bus, so identification and the
read failed and `FLASH_ACCESS_Read` called `on_error`, which is
`APP_POWER_ClearCallback`: the power-off screen.

`engine/stubs/card_backup_port.c` replaces every export of that object (eleven
functions) with the same device: a 512 KiB flash image mirrored to the port's
own save path. `SAVE_CONTROL` asks for `CARD_BACKUP_TYPE_FLASH` 4 Mbit, any
vendor (`0x00ff1302`), and reads its last page (`0x7F800`, 884 bytes), so 512
KiB is the size and a missing file starts as erased flash (`0xFF`) — which is
what a cartridge that was never saved to looks like. Reads and writes are
applied at the address the request names, and each write range is written back
to the file, so a save does not need a whole-image flush.

The object is the first object in the link group (engine/game.mk), so its
`CARD_*` definitions are visible before the archives are scanned; otherwise ld
pulls the DS version out of `libnitro.a` for the same symbols.

Verified in LOTUS: `SAVE_CONTROL_Create` no longer opens the shutdown screen.
The stack now reads

```
SAVE_CONTROL_Create -> BACKUP_SYSTEM_Create -> BACKUP_SYSTEM_CreateCore
  -> SAVEDATA_CreateTable -> SAVEDATA_BuildLayout -> SAVEDATA_QueryBlockSize
```

and the next failure was a call through the main save block table: the
untyped and holder rules in "The flat data model".

### The DS shared work area

`MYSTERY_DATA_EncryptBuffer` first faulted reading the DS V-blank counter at
`0x02FFFC3C`. The previous session patched readers one file at a time
(`mystery_data.c`, `field_g3d_mapper.c`, `p_status.c`, `record.c`, `pm.c`);
the next boot stopped at a sixth, `SYMBOL_SAVE_UpdateChecksum`. Aletheia has
about 130 code sites in 59 files that name the same 4 KiB area by literal
(`0x02FFFxxx`, or its NTR mirror `0x027FFxxx`): the V-blank counter, X/Y and
lid bits (`0x02FFFFA8`), the cartridge header copy (`0x02FFFE00`), owner info
(`0x02FFFC80`), lock words.

The area cannot be mapped at its DS address: `0x00100000-0x04000000` is the
3DS executable window and `MapMemoryBlock` only maps at `0x08000000`
(coffeepot `ctr/hardware/memory.md`, ARM11 userland virtual map). sinjoh
already shadows it at `NitroPort_MainMem + 0x7FF000` (NitroSDK
`mmap_shared.h:40`) and fills it: `NitroPort_SetButtons` writes
`HW_BUTTON_XY_BUF` (`port_boot.c:93`), boot copies the ROM header into
`HW_ROM_HEADER_BUF` (`port_boot.c:33-40`).

`tools/patch_sources.py` now rewrites every such literal in code (comments and
strings skipped) to `((unsigned int)NitroPort_MainMem + 0x7FFxxx)`. Header
macros that expand to one (`HW_BUTTON_XY_BUF` in `touchpanel_shared_types.h`
and `ov013/vct_types.h`) are overridden after the including file's last
`#include`. `engine/game.mk` asks the tool (`--list-shared`) for the affected
files and patches every compiled one, game and Gen V; the five per-file
V-blank fixups are gone. Patched copies are compiled with `-iquote` on their
original directory so quoted includes resolve as they did before the copy,
and depend on `patch_sources.py`, so a change to the tool rebuilds them.

The counter itself: the DS ARM7 advances it (NitroSDK `os_irqTable.c:168` is
`SDK_ARM7` only), so nothing on the port did. `NitroPort_OnFrame` now
increments the shadow word, and `unowa_vblank_count` is removed: sinjoh's
`OS_GetVBlankCount` and the game read one counter.

Verified: no `0x02FFFxxx` constant remains in `m1-test.elf` code
(`SYMBOL_SAVE_UpdateChecksum` loads through the shadow). In LOTUS the boot
passes the counter readers, `SAVE_CONTROL_Create`, `CONFIG_ApplyTextLanguage`
and `GFL_NET_ERR_Init`, and stops at frame 16 in `GFL_HEAP_Alloc` reading
`0x020A7234`: `net_err.c:468` passes the ARM9 *data* address of its file-name
string as `(const void *)0x020A7234`. That is a main-RAM literal outside the
shared area; `docs/eidos-review.md` section 2 is the general fix.

The save device is not a stub: `sdmc:/3ds/unowa/saves/<edition>.sav` (the path
`storage/paths.c` already defines) is created at 512 KiB of `0xFF` on first use
and grows the game's own blocks as it writes them. A secondary LOTUS session
keeps its own copy of that file, which is why a test run does not touch the
primary card's save.

Kohjoh and sinjoh were audited for reusable work; the decisions and citations
are in `docs/reuse-audit.md`.

### The frame-25 crash

The flat boot stopped at frame 25 on an unmapped write to `0xDFECD4CA`, and
CITnLO then aborted on a GPU address (LOTUS #155). Both writes come from
`NNS_SndHandleReleaseSeq` releasing `_SndSlots[0]` and `[1]`. Their handles
held ARM9 autoload bytes that had been copied over BSS. The generator now stops
at `ram_end` and checks module bounds. The full investigation, repro and
register captures are in `docs/frame25-crash.md`.

### Boot firmware state

Past frame 25 the boot ran `main()` to its end. An execute breakpoint on
`abort` showed the path:
`NitroMain` → `GFL_USE_PollFrameInput` → `UI_Main` → `UI_SoftReset` →
`OS_ResetSystem` → sinjoh `NitroPort_ResetSystem` → `abort`. No button was
held. `KEY_PadRead` (Aletheia `arm9/key.c:111-114`) reads `REG_KEYINPUT` and
`HW_BUTTON_XY_BUF` directly. Both are active-low (sinjoh `port_boot.c:81-82`),
and nothing had written them, so zero read as every button held. `UI_Main`
(`arm9/ui.c:272`) treats L+R+Start+Select as a soft reset.
`NitroPort_OnFrame` writes the real pad state each frame, but it had not run
when the game first read the keys (`unowa_present_count` was 0 at the reset).

kohjoh runs sinjoh's `NitroPort_BootFirmware` before `NitroMain`
(`kj_boot.c:105`); unowa's entry never did. `engine/runtime/ds_boot.c` now
does, right after the flat ARM9 load. It writes:
- the key register and X/Y/lid word at "nothing held, lid open";
- a cartridge header with Black's title, game code, maker and device size
  (from the extracted `header.bin`);
- owner settings and a fixed MAC address;
- reset parameter 0.

Verified in LOTUS: 1200 frames with no stop, no terminate and no
unimplemented call. The overlay log shows 15 runtime loads: 10, 11, 9, 13,
10, 11, 228, 13, 9, 10, 11, 13, 9, 10, 11. At this commit presentation was
paced by `C3D_FrameRate(30.0f)` and measured 587 presents; that also halved the
game tick and is fixed in "The frame bridge" below.

Still open: the owner settings are kohjoh's defaults ("Player", English,
1 January; kohjoh `src/app/main.c:218-228`). Reading them from the 3DS system
configuration needs a source for the CFG block layout, and coffeepot has no
article on it yet.

### The frame bridge

The boot runs, but the game tick was paced by presentation. Black/White waits
once per V-blank in its top-level loop (Aletheia `arm9/gfl_use.c:521`), and that
wait reaches `NitroPort_OnFrame`. The M2 clear path called
`C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` under `C3D_FrameRate(30.0f)` there; the
V-blank counter citro3d waits on advances every second V-blank at 30 Hz
(coffeepot `ctr/engineering/PICA200/frame-pacing.md`, "Locking the frame rate"),
so each game V-blank blocked for two physical ones. Measured in LOTUS at
`1151656`: **635 game V-blanks over 1200 guest frames (~31.7 Hz)**, with the
same 635 presents. That is half-speed gameplay, not a renderer fault.

Pacing and presentation are now separate, following sinjoh's `kj_ctr_vblank`
and `kj_frame.c` publish (reuse-audit.md, "Adapt, do not copy"):

- `engine/runtime/frame_pacing.c` owns the emulated V-blank clock. It locks the
  count to `C3D_FrameCounter(0)`, waits with `gspWaitForVBlank` when the game is
  early, lets a late frame catch up instead of losing DS time against audio,
  and resynchronises after a stall over 30 V-blanks. Coffeepot frame-pacing.md,
  "Lock emulated V-blanks to the real counter" and "Catching up without a cap";
  sinjoh `engine/app/main.c:39-46,96-135`.
- `renderer.c` leaves the citro3d frame rate at 60 (so `C3D_FrameCounter(0)`
  ticks every V-blank) and submits with `C3D_FrameBegin(0)`, which waits only
  for GPU work, never for the display (coffeepot frame-pacing.md, "The timing
  model").
- `NitroPort_OnFrame` calls the pacing wait once, then presents every *other*
  DS V-blank, so the picture is still 30 Hz while the game ticks at 60 Hz.
  Skipping a picture never skips a tick (reuse-audit.md lesson 2).

Verified in LOTUS, secondary session copied from the `unowa` card,
1200 frames, no error: `unowa_pacing_vblanks == 1196`, the DS shared counter
`NitroPort_MainMem+0x7FFC3C == 1196`, `unowa_present_count == 598`,
`unowa_pacing_late_vblanks == 22`, `unowa_pacing_resyncs == 0`. Both screens
still capture the expected navy/purple clears. The tick is ~59.8 Hz against
LOTUS's 59.83 Hz guest refresh. Emulator evidence only; the display timing is
unmeasured on Old 3DS.

Still to do for M3: find the game's actual coherent frame boundary before
moving the publish off `NitroPort_OnFrame`. The obvious candidate, the end of
`GFL_USE_PostIrqUpdate` (Aletheia `arm9/gfl_use.c:582-586`), was tried with a
build-time hook and **does not fire**: in LOTUS the DS V-blank counter reached
2204 over 2208 frames, the overlay log stayed at 15 loads, but
`unowa_present_count` stayed 0 and both screens went black. The game therefore
reaches `OS_WaitIrq` (hence `NitroPort_OnFrame`) without reaching the
`NitroMain` loop's `GFL_USE_PostIrqUpdate` call at this stage. Presentation was
restored to `NitroPort_OnFrame`; the hook was removed. A real M3 bridge must
identify the boundary the game actually uses, not assume the source-level loop
is running.

### The main-loop stall: two DTCM aliases

That "does not fire" observation had a cause, found with a build-time counter on
the `NitroMain` loop: the game thread never left `OS_WaitIrq`, so
`unowa_mainloop_count` stayed 0 while the idle thread drove V-blanks through
`NitroPort_Halt`. The game was not stuck in a particular `OS_WaitIrq`; the wait
itself could never succeed.

On the DS the V-blank handler sets the interrupt check flag by address. The
game's `GFL_USE_VBlankHandler` writes
`SDK_AUTOLOAD_MAIN_TWL_START + 0x3FF8` (Aletheia `arm9/gfl_use.c:578`), while
NitroSDK's `OS_GetIrqCheckFlag` reads `SDK_AUTOLOAD_DTCM_START + 0x3FF8`
(`nitro/os/common/interrupt.h:155-158`). Both symbols are `0x02FE0000` on the
DS: it is the 16 KB DTCM, which `INITi_InitRegion` maps there
(`arm9/crt0.c:278`). unowa's flat-image generator bound
`SDK_AUTOLOAD_MAIN_TWL_START` into `NitroPort_MainMem` (a main-RAM mirror) and
left `SDK_AUTOLOAD_DTCM_START` at sinjoh's separate 16 KB block, so the write
and the read went to different words.

Second mismatch: `NitroMain` installs its handler through the game's own
`OS_SetIrqFunction` (`arm9/os_interrupt.c:67`), which writes the table named
`g_02FE0020`, while the linked `OS_IrqHandler` reads NitroSDK's `OS_IRQTable`.
The table was still all `OS_IrqDummy`.

`tools/gen_flat_image.py` now resolves `[0x02FE0000, 0x02FE4000)` to the one
DTCM block (`SDK_AUTOLOAD_DTCM_START`), and maps the two shared OS structures
inside it to libnitro's copies: `[0x02FE0020, 0x02FE00A0)` is `OS_IRQTable`, and
`0x02FE00A0` is `OSi_IrqThreadQueue`. LOTUS then showed
`OS_IRQTable[0] == GFL_USE_VBlankHandler` and the main loop advancing.

### Function pointers into SDK-provided functions

With the loop running, the game faulted on `ARC_UTIL_gPaletteLoadFns[4]`: the
intro cutscene called `GX_LoadOBJPltt + 8` (the function's second instruction)
with `r12 = 1`, from a table whose other entries were `GX_LoadBG2Char + 732`
and similar. The generator's DS-to-native function table skipped every function
whose Aletheia source is not compiled. Files such as `gx_load2d.c` are provided
by sinjoh's NitroSDK instead, so those DS addresses fell through to the
*native-data* spans and resolved to a neighbouring symbol plus a byte offset.
The table now accepts any name the final link defines (`native`), whether the
Aletheia source or libnitro supplies it; the eight palette loaders resolve to
their exact functions, with no addends. The same change gives `code_area_spans`
correct function boundaries for those SDK files, which is why the copied image
shrank from 380596 to 373292 bytes.

### DS I/O literals in game source

The forced port header rewrites Aletheia's `REG_*_ADDR` macros, but matched
sources also name registers by literal. The cutscene hit
`*(volatile unsigned short *)0x04000050 = 0` (`ov179/cdemo_scene.c:319`,
BLDCNT) and stopped with an unmapped 16-bit write to `0x04000050`. Coffeepot
`ctr/engineering/porting.md:92` warns about exactly this. `patch_sources.py`
now rewrites a literal in the DS `0x04000000..0x07FFFFFF` window when it
directly follows a pointer cast, using the same `REGIONS` table as the header
generator; control words such as `0x04000000 | (size / 4)` in `mi_dma.c` and
region comparisons keep their values. This is the same rule the main-RAM
literal rewrite already applied.

### M3: the DS 2D engine on the PICA

The renderer is no longer a clear. `engine/graphics/g2d/g2d.c` is sinjoh's DS
2D engine (GPL-3.0, 1774be6), adapted not rewritten: it translates one
engine's DISPCNT, BG registers, palettes, BG/OBJ VRAM and OAM per scanline
span into ordered GPU passes, with windows, alpha blending, BLDY and master
brightness. `engine/graphics/resources/atlas.c` expands DS tiles into a
1024x1024 RGBA5551 atlas with exact-compare caching, `pica/gpu.c` owns the
citro3d state and the 2D shader, and `pica/targets.c` is the same 1.25x
geometry sinjoh uses (256x192 DS pixels -> 320x240, centred at +40 px on the
400 px top screen).

Two pieces are deliberately absent, because they belong to later milestones
and pretending otherwise would be a half step:

- `g2d/wideart_stub.c` returns zero pictures. The side bands beyond the DS
  picture stay black until M6 supplies real artwork; g2d's wide branches are
  dead rather than approximated.
- `g3d/g3d_stub.c` supplies `kj_g3d_set_frame`/`draw`/`wbuffer`. g2d only
  calls the 3D layer when the frame input carries a decoded scene
  (`has_3d` requires `in->g3 != NULL`), which unowa does not produce until M4.

`engine/runtime/unowa_frame.c` builds the `kj_frame_input`: the DS memory
view, the VRAMCNT bank decode and sinjoh's per-line register shadow. It
publishes from `NitroPort_OnFrame`, the boundary proven in "The frame
bridge". The renderer runs sinjoh's `NitroPort_HBlankLinesOnce()` before
reading that shadow, so H-blank effects are walked once per frame.

Verified in LOTUS, secondary session on card `unowa`, asset `unowa-m1`,
1200 frames with no stop: `unowa_mainloop_count == 1194`,
`unowa_pacing_vblanks == 1194`, `unowa_present_count == 597`, 18 overlays.
Captures show the Game Freak/Pokémon Company and Nintendo logo screens, the
copyright screen, and then the whole intro: the comet, the GAME FREAK title,
the castle bridge with the player character, and the Pokémon Black Version
logo. All of it is drawn on the top screen at the expected 1.25x scale; the
bottom screen is the game's own black output.

Two more pieces were needed for the intro, and both are M3 correctness, not
workarounds:

- **Direct-colour affine BGs.** The intro is not 3D. `CDEMO_MAIN_SetupScreen`
  calls `GX_SetGraphicsMode(1, 5, 0)` (`ov179/cdemo_main.c:110`) and sets
  `BG2CNT`/`BG3CNT` to `0x4084`/`0x4884`: mode 5, extended affine, bit 7
  (bitmap) and bit 2 (direct colour) set. The engine then loads 256x192
  frames into a 16-bit BGR555 bitmap with `GX_LoadBG2Scr`/`GX_LoadBG3Scr`
  (`cdemo_scene.c:205-216`). `g2d` rejected every bitmap BG as unsupported;
  it now draws direct-colour bitmaps: an 8x8 cell is converted into an atlas
  slot with bit 15 as the alpha test, and the existing per-cell affine quad
  path places it. The addressing follows NitroSDK `G2_GetBG2ScrPtr`
  (`gx_bgcnt.c:50-70`, mode 5 with colour mode: base `screen_base * 0x4000`,
  linear rows of the BG's pixel size) and eidos `hwcompose.c:596-660`,
  `RAS_DIRECT` on `cnt & 0x84 == 0x84`, bit 15 clear = transparent.
  256-colour bitmaps (`cnt & 0x80` without bit 2) are still reported as
  unsupported.
- **The per-frame quad arena reset.** `kj_gpu_quads_reset()` was missing
  from the M0 renderer, which never used the arena. Without it the arena
  filled up after the logo screens and every later allocation failed
  (`unowa_direct_norun == 2` per frame, 832 cells each), which is why the
  intro was black even after the bitmap path worked. The renderer now calls
  it exactly where sinjoh does (`renderer.c:172-173`).

The title demo's model resources now load. Its animation table pointer at
`objTbl[0].anmTbl` (DS `0x021BD3E4`) was being rewritten to native
`DWCi_FOCUSlInit`: overlay 16 has a function at the same numeric address and
the flat generator accepted another overlay's function for a word inside
the current module's own data window. `resolve_function` now refuses that
fallback when the value lies in the current module's `[ram_start, bss_end)`,
so the word is rebased as data (58 words moved from function pointers to
flat data pointers; `0x021BD4E0` now holds `0xB813E4`, whose table is
`{1,0},{6,0}`).

The title screen then follows and renders: the Pokémon Black Version logo,
the animated background and the blinking "PRESS START", and it survives
START presses (LOTUS: 3336 frames, no stop, `mainloop 3330`,
`presents 1665`, DISPCNT `0x1E0918`). The earlier crash there was a bare
DS literal: `NNS_G2dGetOamSoftEmuAutoZOffsetStep`
(`nns_g2d_renderer_core.c:411`) reads `*(int *)0x20A8218`, written with the
leading zero dropped. `MAIN_LITERAL` in `patch_sources.py` only matched
`0x02xxxxxx`, so the read stayed a DS address and faulted natively at
`0x020A8218` as soon as the title's cell actors were drawn. The pattern now
accepts both spellings; the patched line is
`*(int *)(((unsigned int)NitroPort_MainMem + 0x0A8218))`, the same address
`flat_names.ld` gives `sOamSoftEmuAutoZOffsetStep`. The same rule also
catches `gf_standard.c`'s `(u32 *)(0x2146A1C)`, which had the same latent
bug.

The title's 3D is now produced and published. `G3D_SYSTEM_SwapBuffersMain`
(`g3d_system.c:1194`) stores the packed parameter straight into
`reg_G3_SWAP_BUFFERS`, but the port's geometry engine is a command stream:
the SDK's `G3_SwapBuffers` reaches it through `NitroPort_GxDirectWrite`
(`g3imm.h:441`), and a plain store to the I/O shadow never does. No scene
was ever swapped (`unowa_g3_frames` 0, `s_have_display` 0) although
`G3D_SYSTEM_DrawObject` was being called from `STARTMENU_Demo3dUpdate`.
`patch_sources.py` now rewrites stores to that one register to
`NitroPort_G3Swap(value)`, which forwards to the same command seam
(`port_stubs.c`). LOTUS at the title: `unowa_g3_frames` 467 and
`verts` 5332, no stop at 2892 frames, `mainloop 2674` / `presents 1337`,
and the captured title logo is fully coloured.

The title then switches to DISPCNT `0x1E0118`: only BG0 is enabled, with the
3D layer on it. The logo, its background and the black field are all the
3D scene (the logo is a flat 3D model), so the title's picture is the
port's geometry output. LOTUS: `unowa_g3_frames` 1067 and `verts` 4881 at
4092 frames, `mainloop 4056`, no stop; the captures show the fully
coloured logo. The scene is stable for thousands of frames.

`DISPCAPCNT` (`0x04000064`) is still not modelled. The title demo arms it
in its V-blank task (`STARTMENU_CaptureVblankTask`), and later phases use
it to blend the 3D into a direct-colour BG; those frames will keep the
scene's clear colour until the capture exists.

### The lower screen: `GX_DISPMODE_VRAM_D`

The title's 3D is on the **lower** screen, the same arrangement kohjoh has
for HGSS's box character. `POWCNT1` bit 15 is 1 = main engine on top: the
SDK's `GX_Init` sets that bit (`gx.c:24`) and `GX_DISP_SELECT_MAIN_SUB` is
1 (`gx.h:207-208`), so the port's `NitroPort_EngineAOnTop` is not inverted.
`STARTMENU_TitleDemoUpdateTimer` calls `BG_SYS_SetLcdSwap(1)` at
`frameCount == 0x29A` (main on top), and the phase-2 input branch calls
`SetLcdSwap(0)`: main engine A, with the 3D, moves to the lower screen.

At that point engine A's DISPCNT is `0x1E0918`: bits 16-19 = `0xE`, which
is `GX_DISPMODE_VRAM_D` (`gx.h:83-91`, `VRAM_A` 2, `VRAM_B` 6, `VRAM_C` 0xA,
`VRAM_D` 0xE). The screen shows the VRAM bank the title's `DISPCAPCNT`
fills from the main graphics screen, i.e. the 3D picture. `g2d` treated
every display mode except `GX_DISPMODE_GRAPHICS` as unsupported and
cleared the screen, which is why the lower screen was black.

`g2d` now draws the engine's live layers for display mode 2 (VRAM A..D).
The capture's source is those same layers, so this is the same picture
without the VRAM round trip; the A+B afterimage blend (`DISPCAPCNT`
EVA/EVB against a VRAM source) is still not modelled. LOTUS at the title's
phase 2: `unowa_g3d_bottom_draws` 573 and 56,655 batches over the run, the
lower screen shows the checkered 3D floor with the Game Freak mark and the
light burst, no stop at 3652 frames.


The title now responds to START. With LOTUS `buttons` 0x400 the guest's
`KEYINPUT` reads `0x3F7` and `TitleDemoWork.state` moves 3 -> 4
(`startmenu.c:1119`), but the demo then waits there for the intro fanfare
to finish: `PM_VOICE_IsSlotActive(work->fanfareHandle)` stays 1 because
`NNS_SndWaveOutIsPlaying` never sees its wave-out channel go idle
(`nns_snd_waveout.c:254-268`, channel bit from `SND_GetChannelStatus`).

### Audio: the ARM7 driver clock (the title's exit)

The port already links sinjoh's virtual ARM7 sound stack: `NitroMain`'s
`NitroPort_InitArm7Services()` (`engine/app/game_main.c:47`) runs the original
ARM7 `N7_SND_Init`, and `NitroPort_HandleSound` executes the game's command
lists. What was missing was the clock. `SND_UpdateSharedWork` only runs inside
the mixer tick, so with nothing mixing, the started wave-out channel never
retired: the shared `channelStatus` stayed `0x8000` (channel 15, the title's
fanfare) and `PM_VOICE_StopFinished` (`gfl_use.c:652`) never cleared the voice.

The fix is sinjoh's proven audio layer, `engine/audio/audio.c` (copied from
sinjoh 1774be6, GPL-3.0): `kj_audio_init()` opens 32768 Hz stereo NDSP output
on its own thread, and when NDSP is unavailable it runs the same mixer on a
silent thread so the chip still advances. `NitroPort_OnFrame` pumps the
driver's alarm replies (`NitroPort_SoundPumpReplies`, sinjoh
`engine/runtime/kj_frame.c:162`) and calls `kj_audio_service()`.

LOTUS, 1200 frames after boot: the title demo goes 3 -> 4 -> 5 -> 6 -> 7 and
`STARTMENU_TitleDemoExit` replaces it, the start menu lists NEW GAME, MYSTERY
GIFT, NINTENDO WFC SETTINGS, and pressing A on NEW GAME starts the new-game
script: "Hi there!" / "Welcome to the world of Pokémon!" / "My name is
Professor Juniper. Everyone calls me the Pokémon Professor!" advance with A
through frame 4900. Selecting NINTENDO WFC SETTINGS instead faults in
`DWCi_TSKlInit` (`ov016/task.c:119`) on a NULL task at +0x78, which is the
unported Nintendo WFC stack (see Wireless stubs), not the menu.

### The field map: three DS assumptions the intro never reached

The first field map (Nuvema Town, after the intro) is the first real 3D world,
and it found three places where the port still handed the guest a DS address
the ARM11 process does not map. All three are fixed; the evidence below is
LOTUS, one stop at a time.

**Overlay static initialisers ran out of the flat image.**
`GAMESYSTEM_ProcInit` (`gamesystem.c:449`) loads ov230 (the anti-tamper
loader), runs the sealed-body checks and unloads it again. `FS_StartOverlay`
runs an overlay's static-initialiser table on every load
(`fs_overlay.c:403`), and `__wrap_NitroPort_OverlayInfo` pointed the SDK at the
table's DS address in the flat image. ov230's entries are its decryption
routines (`0x02188408..0x02189908`, `LOADER_ENTRY_DecryptCallStubs*`), but ov230
is blocklisted (`engine/game_overlay_blocklist.txt`) and its six sealed bodies
are stubs (`engine/stubs/port_stubs.c:85-128`), so no native function exists.
The flat image is a copy of DS main RAM, not executable memory: the first
entry ran as data and the stop was a prefetch into `0x00B54058`
(DS `0x02189058`), with `FS_LoadOverlay -> FS_StartOverlay` on the stack.

`tools/gen_flat_image.py` now emits a native initialiser table per module
(`unowa_flat_sinit`) and the overlay info hands the SDK that table, or a count
of zero when an entry has no native definition. A table is refused whole, never
partially, because the entries run in order. The generator states the decision:

```
ov230 static initialisers not run, no native code: 0x02188408 0x021889B0 0x02188CB0 0x02189448 0x02189908
```

**DS VRAM pointers reached the SDK's block clears.** The next stop was an
unmapped write to `0x06800000`:
`MIi_CpuClearFast(0, (void *)HW_LCDC_VRAM, 0xA4000)`
(`bg_sys.c:3438`; `ov020/event_mapchange.c:776` is the same clear in the
map-change event that starts the field). `HW_LCDC_VRAM` is a NitroSDK macro, so
the generated `sdk_ioreg.h` cannot redirect it; the address reached libnitro's
loop unchanged. `unowa_ds_to_host` now covers every DS region sinjoh shadows
(the same table as `tools/gen_port_ioreg_header.py:23-32`: I/O, palette, BG/OBJ
VRAM, LCDC VRAM, OAM), and `engine/runtime/ds_memory_ops.c` wraps
`MIi_CpuClear/Copy{16,32,Fast}`, `MI_CpuFill8` and `MI_CpuCopy8` at the final
link to translate their pointer arguments. The same fix covers clears already
compiled into other overlays (`HW_BG_VRAM`/`HW_OBJ_VRAM` in ov071, ov109,
ov174, ov203, ov205) before their code runs.

**A local address macro hid the address from the literal rewrites.** The field
then stopped on an unmapped read of `0x021DE7C4` inside
`FIELD_PLACE_NAME_Create` -> `BG_SYS_InitBgLayer`, from
`FIELDMAP_SeqCreateSubsystems` (`fieldmap.c:891`). The banner passes its BG
template as
`(const struct BgTemplate *)FIELD_PLACE_NAME_BG_TEMPLATE_ADDR`
(`field_place_name.c:299`), and the address is a local
`#define ... 0x021DE334` (`field_place_name.c:127`). The main-RAM literal
rewrite (`tools/patch_sources.py rewrite_main_ram`) only sees numbers, so the
TU was compiled unpatched and the raw DS pointer was dereferenced.
`rewrite_address_macros` now applies the same rule to a local
`#define NAME 0xADDR` used directly behind a pointer cast (a use as a value
keeps its value). Nine TUs, `ov021/field_place_name.c` among them, are patched
by that rule.

After those three, the field map runs: LOTUS shows Nuvema Town in 3D
("GAME FREAK PRESENTS") at frame 13,480, `unowa_mainloop_count` 13,021,
`unowa_g3_frames` 5,549, and the overlay log below. The next stop is later in
the field (see Open blockers).








### Wireless stubs

**DS wireless and the DSi DSP are stubbed, not ported.** Revisit this before
any multiplayer, Download Play, Wi-Fi Connection or DSi-camera feature.

The 3DS local wireless service is UDS, which "is not DS Wireless" (coffeepot
`ctr/glossary.md:429`). There is nothing to forward the DS `WM`/`MB` protocol
to, and Eidos has no answer either (`docs/eidos-review.md` section 6). Once the
flat image referenced every typed function pointer, the linker pulled in the
callers of these entry points, and their real implementations are blocked from
the build: `ov107/mb_wm_base.c` and `ov114/crypto_bobj2.c` are Metrowerks
inline assembly, `twl/dsp_process.c` needs DSi-keyed generated data
(`engine/game_overlay_blocklist.txt`), and the arm9 FS/CARD/NNS-font files
duplicate or conflict with sinjoh's libraries (`engine/game_blocklist.txt`).

`engine/stubs/wireless_stubs.c` defines each of them. Every stub reports its
name once through `NitroPort_Unimplemented` (so it lands in
`unowa_unimplemented_log_names`, readable in LOTUS) and returns the value its
callers already treat as failure:

| Group | Entry points | Callers | Stub result |
| --- | --- | --- | --- |
| Download Play | `MB_Init`, `MB_SetParentCommParam`, `MB_StartParentFromIdle` | `ov107/mbp.c`, `ov016/dwc_mov_mbp.c` | `MB_ERRCODE_INVALID_STATE` (2); `MB_Init` failure panics in both callers |
| | `MB_EndToIdle`, `MB_DisconnectChild`, `DWCi_DwcMoveidMB_EndToIdle` | same, `ov016/dwc_moveid.c` | no-op |
| | `MBi_IsStarted` | `ov107/mb_fileinfo.c:392` | 0, so `MB_RegisterFile` fails |
| | `CARD_GetOwnRomHeader`, `CARDi_GetRomCacheBuffer`, `_start_AutoloadDoneCallback` | `MB_ReadSegment` | NULL / no-op |
| WFC setup, Download Play files | `FS_OpenFileEx`, `FS_GetFileLength`, `FS_GetFilePosition`, `FS_GetFileImageTop`, `FS_GetArchiveBase` | `ov016/archive.c`, `ov107/mb_fileinfo.c`, `mb_gameinfo.c` | FALSE / 0 / NULL |
| WFC setup font | `NNS_G2dCharCanvasInitForVRAM`, `NNSi_G2dCharCanvasDrawChar`, `NNSi_G2dInitPenSimple`, `NNSi_G2dCalcCharAreaSize`, `NNSi_G2dMakeCharObjArea`, `NNSi_G2dCalcTextBox`, `NNSi_G2dAlignTextVertical` | `ov016/font.c` | no-op / 0 |
| WFC network (ov114) | `CRYPTOi__bn_{add,sub,mul,mul_add,sqr,div}_words` | `ov114/crypto_bobj1.c` | 0 (a real WFC session would compute wrong bignums; it cannot start without a connection) |
| DSi DSP | `DSP_InitProcessContext`, `DSP_ExecuteProcess`, `DSP_QuitProcess`, `DSP_FindProcess` | `twl/dsp_g711.c`, `twl/dsp_util.c` | 0 / NULL; callers return early (`dsp_util.c:120-123`, `dsp_g711.c:53`) |

The FS functions are real NitroSDK functions on the DS that sinjoh provides
only as header inlines (`nitro/fs/file.h:267`, `nitro/fs/archive.h:155`) or not
at all. Today only wireless code calls them. If a non-wireless caller appears,
it shows up in the unimplemented log, and the fix is a real implementation over
sinjoh's `FSFile`, not a wider stub.

A future port of these features needs, at minimum:
- a UDS-backed `WM`/`MB` layer, or a design decision that local multiplayer is
  3DS-to-3DS only;
- an HTTPS replacement for the DWC stack (ov009, ov016, ov114), since the
  original Nintendo WFC servers are gone;
- a C translation of the `crypto_bobj2.c` bignum words, in the same way as
  `MATH_QSort`.

## Open blockers

- No physical Old 3DS in this environment. LOTUS and Azahar are integration
  debuggers, not timing authorities. All M0 claims are emulator-tested at most.
- Aletheia's `arm9data.py` needs the DSi modcrypt key-scrambler constant to
  decrypt `arm9i.bin` and generate `twl_0272EB9C.inc`. `tools/modcrypt.py:26-46`
  documents that it lives in AES hardware, not in BIOS or firmware ROM. The
  supplied `biosdsi9.bin`, `DSi_Firmware.bin`, `dsifirmware.bin` and
  `dsi_nand.bin` were all scanned with `tools/modcrypt.py check --dsi-bios`
  and none contains it. A 16-byte little-endian dump via `--dsi-key` is the
  only accepted form. unowa's own generator remains the fallback.
- `tools/pack_game_data.py` writes a supported Gen IV digest into the pack
  header because sinjoh's `pak_open` (`pak.c:88-91,160`) accepts only three
  Platinum revision hashes and the file is a pinned dependency. The index and
  file bytes are the player's Black/White NitroFS files; only the header's
  validation tag is borrowed. Replacing it needs a build-tree `pak.c` override
  or an upstream entry for the Black/White hashes.

### Field stop 4 — cross-overlay function pointers in `ev=ptr` data (FIXED)

Reproduced from a fresh secondary session (`card: unowa`, asset `unowa-m1`,
build at the time `m1-test.3dsx` 5,305,564 bytes). After tapping through NEW
GAME, name entry and Juniper/Cheren/Bianca's intro with LOTUS input
(A=0x001, touch OK at 3DS `(290,204)`), the game reaches Nuvema Town and then
halts:

- `mainloop_count` freezes at 18,716, `pacing_vblanks` 18,717,
  `present_count` 9,358, `overlay_log_count` jumps 42→44, screen white.
- Core 0 stop: `exception 12`, `unmapped Read32 @ 0x0216E464 at PC 0x0216E464`.
- `addr2line` of the stack: `FIELDMAP_SetupMmdlSys`
  (`ov021/fieldmap.c:1645`) → `FIELDMAP_SeqCreateSubsystems` (`:927`) →
  `FLDMMDL_CheckMoveBit` (`ov010/fldmmdl.c:911`).
- `function_addrs.tsv`: `0x0216E464 = FLDMMDL_MoveInitProcDummy` (ov010).
  The game jumped to the raw DS address of a static dummy proc and fetched
  unmapped memory.

Root cause is in `tools/gen_flat_image.py`. `gWeatherTable`-style proc tables
in ov021 data (`g_021D58A8`, `g_021D5A24`) are Aletheia `numeric`/`u32`
objects with `ev=ptr` and no `targets`. `typed_words()` labelled their words
`"maybe"`, so `build()` used the conservative same-module/ARM9 lookup. The
pointers target functions in **ov010**, so they were left as raw DS values.
The four neighbouring proc-list words in `struct` objects with `targets`
(`0x021D56E8` etc.) already relocated, which is why only the init/move pair
crashed.

Measured before changing the rule: 1995 objects carry `ev=ptr`; of their
currently-`"maybe"` words, exactly **8** match a function under
`resolve_function()` — the 2×4 proc-list entries in `g_021D58A8`/`g_021D5A24`,
all resolving to `FLDMMDL_Move{Init,Proc,Delete,Recover}ProcDummy`. Zero false
positives. Fix: `typed_words()` now marks an object whose `ev` list contains
`ptr` as a `"holder"`, so `build()` uses `resolve_function()` for it. This is
the object map's own type evidence, not a value scan.

Rebuilt with `make -f engine/game.mk link-final -j8`:
`function pointers` 7,208 → 7,216; the flat image now has all eight
relocations, and the ELF defines all four dummy procs (`0x001a323c..48`)
instead of only two. New `m1-test.3dsx`: 5,305,668 bytes, SHA-256
`41695026bb1a143be1960e9b3ff27f125595a90ef920bb88ba5547de140185a9`.

LOTUS verification (fresh secondary session): the same input sequence reaches
Nuvema Town at `mainloop` 20,045 and keeps advancing to 20,866 — **2,150 ticks
past the old 18,716 stop** — with `overlay_log_count` 47→51, `pack_result=0`
and `unimplemented_log_count=0`. The first field stop is closed.

### Field stop 5 — cross-overlay data pointer in `gWeatherTable` (FIXED, with a known tail)

In the build that fixed stop 4 the game halts later, at `mainloop` 20,866:

- Core 0 stop: `exception 12`, `unmapped Read32 @ 0x021EE8F8`. A run of
  native_log reads precedes it: `0x021EE8C2` (u16), `0x021EE8E8`, `0x021EE8EC`,
  `0x021EE8F4`, `0x021EE8F8` (u32).
- PCs symbolize to `WEATHER_TASK_Update` (`ov021/weather_task.c:265,279,285,308`)
  and `WEATHER_TASK_AllocParticles` (`:994,998`), reading `task->kind->...`.
- `WEATHER_TASK_Start` stores a `WeatherKindDef *` passed from
  `gWeatherTable[weatherId].taskParam` (`ov021/weather.c:198,288,306`).
  `gWeatherTable` is at `0x021D8868`; its raw first word is `0x021EE8C0`, an
  address inside **ov024**'s data window (`data_start 0x021EE8A0`), and the
  flat generator emits **no relocation** for `0x021D8868`. The game therefore
  dereferences the raw DS address, which ARM11 does not map.
- `gWeatherTable` is 8-byte-strided (`taskParam`, `overlayId`); nine `taskParam`
  words (`0x021D8868..0x021D88D8`) point into ov024–ov028 data windows. Their
  Aletheia objects are `numeric`/`u32`, `ev=pool` — weaker evidence than the
  `ev=ptr` tables fixed above, so the `ev` rule does not apply.

A blanket "rebase aligned `maybe` words into any other module's data window"
rule would touch 652 words (35 after alignment and excluding functions/named
starts), with clear numeric constants among them, so it is **not** shipped.

**Fix shipped.** `gen_flat_image.py` now collects `strong_objects` — every
Aletheia object typed `struct` or `pointer_table` — and rebases a word in the
`how != "ptr"` data branch when its value is aligned and lands inside one of
those objects in another module. The target need not be the object's named
start: the crash value `0x021EE8C0` sits 0x20 bytes into ov024's `g_021EE8A0`
(`struct`, with seven `targets` listed). This is the object map's structural
typing, not a numeric value scan.

Counting the rule before shipping found **15** `maybe` words. The generator
reports 44 `flat pointers into another module's struct`: tracing every hit
showed 15 `maybe` plus 29 `holder` words. The gap is a measurement-script
scope error, not a rule change — the generator's data branch runs for every
`how != "ptr"` word whose function lookup fails, including the `holder` words
that already use `resolve_function()` first. All 15 predicted addresses are in
the 44; the 29 extras are in objects the map already marks as pointer holders
(`struct`/`targets`/`ev=ptr`) and are cross-overlay data pointers too.

Rebuilt 5,305,844 bytes, SHA-256
`467457da26c43566d5ae2c7a7f4e6b308b83b8c8518ce8ae4bfdbf6f7c6e03fe`;
`flat data pointers` 2,993 → 3,037, `function pointers` stays 7,216. LOTUS
fresh secondary session, same input: the game now reaches `mainloop` 22,159
and keeps advancing to 25,759 (+3,600 frames observed) with no stop,
`overlay_log_count` steady at 51, `unimplemented_log_count=0`, `pack_result=0`.
Direct guest-memory read confirms the mechanism: DS `0x021D8868` maps to host
`0x00BA4868`, whose word is now `0x00BBA8C0` (the rebased flat address), not
raw `0x021EE8C0`.

**Known tail, not fixed.** 4 of the 15 `gWeatherTable` taskParams
(`0x021D8870/8888/8890/88A8` → `0x021EF9E4/EF900/EF94C/EF998`) still hold raw
DS values. Their targets are Aletheia `numeric`/`u32`/`ev=pool` objects
(ov025's `g_021EF790` and small ov170 blocks) with no `targets` list, so the
structural rule deliberately does not touch them. They are other weather
kinds and have not been reached in LOTUS; a weather transition to one will
fault and must be fixed with its own evidence (Aletheia declares
`WeatherEntry.taskParam` as `struct WeatherKindDef *` in `ov021/weather.c:84`,
but the object map does not encode that type). Left as a documented limitation
rather than guessing.

### Field stop 6 — function tables Aletheia typed as narrow arrays (FIXED)

Driving past stop 5 found two more raw DS code addresses, both function tables
whose Aletheia object type hides them from the generator.

- At `mainloop` 26,290, after the intro dialogue, `exception 12` at PC
  `0x021AB1AC` = `MAP_ATTR_IsGimmickD7` (ov021). The raw word is at
  `0x021D3CE8`, typed `numeric`/`u16`, `count=3`, `ev=pool`; `typed_words()`
  skipped it because `u16` is not a `WORD_NUMERIC_ELEMENT`. The surrounding
  bytes are an 8-byte-strided table of function pointer + attribute mask
  (`0x021AB1A1/0x276A0000`, `0x021AB1AD/0xFFFF0009`, `0x021AB1B9/...`), so the
  u16 typing is from literal-pool evidence, not the real layout.
- After that was fixed, holding South in the field hit `exception 12` at PC
  `0x021AB4A4` = `MAP_ATTR_IsSlopeXA` (ov021). Its raw words are at
  `0x02171490..0x021714AC`, typed `numeric`/`u32`, `ev=pool`: the `maybe`
  branch only looked up this module and ARM9, but the target is ov021 and the
  word lives in ov010.

**Two measured rules.** `gen_flat_image.py` now:

1. Yields `"maybe_fn"` words from `numeric` objects whose `elem` is
   `u8/s8/u16/s16` (`NARROW_NUMERIC_ELEMENTS`). `build()` relocates one only on
   an exact `resolve_function()` match. Measured 206 matches in 17,242 aligned
   words; samples are real vtables/dispatch tables (`GFL_NET_STATE_*`,
   `MSL_CONSOLE_IO_*`, the full `MAP_ATTR_IsGimmick*` table, ov093's
   `BTL_CLIENT_Step*` command table, `FIELD_GIMMICK_MODEL_SCENE_*`).
2. Uses `resolve_function()` with its full window/uniqueness guard for `maybe`
   words in overlays, keeping ARM9 conservative. The documented reason for the
   conservative rule is 6,228 ARM9 data words that coincide with overlay
   entries; overlays do not have that problem. Measured 81 new matches in
   14,824 overlay `maybe` words: `SCRCMD_*` cleanup tables (ov010), the
   `MAP_ATTR_IsSlope*` table (ov010), ov021's `FIELD_GIMMICK_*` and `ENCEFF_*`
   tables, all named function tables. ARM9 is unchanged.

Rebuilt 5,439,448 bytes, SHA-256
`66ce357a177ed882b21c71718bd88dc2e3465eea71b2da5a50055b2c4ed97ab5`:
`function pointers` 7,216 → 7,297 (+81), plus 206 narrow-table pointers,
`flat data pointers` and the 44 struct-interior data pointers unchanged.
`addr2line`/`nm` confirm `MAP_ATTR_IsGimmickD7` and
`BTL_CLIENT_StepPlayMultiMonAnim` are now in the ELF.

LOTUS fresh secondary session: the same input reaches `mainloop` 22,158, then
runs without a stop to 32,154 under continuous dialogue and movement input.
The two new faults are gone: PC `0x021AB1AC` no longer occurs, and holding
South for 600 frames at `mainloop` 32,244+ no longer faults at
`0x021AB4A4`. `overlay_log_count` 51, `unimplemented_log_count=0`,
`pack_result=0`.

### Field stall — the field 3D scene draws no geometry (OPEN)

The game is stable but does not become playable. Once the intro dialogue ends,
the top screen is solid white, input is ignored (A/B/START/D-pad/touch do not
open the menu or move the player), and no native errors are logged. It stays
this way over 3,600 frames with no input and under continuous input.

A first hypothesis, that this was a stuck white fade, was tested and rejected:
`BLDCNT_A` is `0x1B44` (fade-to-white mode) and `BLDY` `0x0010` (full
intensity) in `NitroPort_IoReg`, but writing zero to both in a disposable
LOTUS session left the top screen white. `g_wipeSys` (DS `0x021467A4`, the
game's wipe state) is all zeros — no wipe is active. The white is the 3D
clear colour with nothing drawn on top.

The real measurement is in the renderer counters (`engine/graphics/renderer.c:
160-164`): `unowa_g3_frames` keeps rising (14,277 at the last sample) while
`unowa_g3_verts` and `unowa_g3_batches` are both 0 and `g3_serial` advances.
So the game keeps presenting a 3D frame with zero geometry. The title's 3D
scene did render (`g3_verts` 5,412 at the start menu), and the field briefly
showed 5,390 vertices at `mainloop` 20,645 in the earlier weather-fix-only
build before the weather fault; after that point the field scene never
submits geometry again. This is an M4 field-scene issue, not a fade or a
pointer fault: the next step is to find why the field's 3D scene stops
submitting batches (camera/scene setup, map resource load, or a script step
that never advances) before walking, collision and map transitions can be
exercised.
