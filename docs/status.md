# unowa status

Status values: implemented / compiled / tested headlessly / tested in emulator /
verified on Old 3DS. Do not collapse them.

## Milestones (UNOWA.md section 12)

| Milestone | State | Evidence | Notes |
|---|---|---|---|
| M0 — Bootstrap | implemented, compiled, tested in emulator | see below | native `.3dsx`, release/profile, async SD log, 30 Hz GPU presentation; both screens render the clear colour |
| M1 — Game compilation | implemented; compiled; linked; tested headlessly | `link-final` builds `m1-test.3dsx`; see below | boots, then faults on DS I/O in M2 territory |
| M2 - Native boot | in progress; tested headlessly | clean 1800-frame LOTUS boot; overlay loads and unsupported-service failures observable; clear-frame presentation reaches both screens | game graphics need M3/M4; input forwarding not yet exercised |
| M3 — 2D | not started | | G2D bridge, VRAM/OAM, palette, windows |
| M4 — 3D | not started | | geometry commands, model path, PICA renderer |
| M5 — Gameplay | not started | | input, scripts, audio, resources, saves |
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
gitignored build tree.

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
and the strong `NitroPort_OnFrame` calls `unowa_renderer_present()` on each
emulated V-blank (counter `unowa_present_count`). This is the clear-only path
from `renderer.c:1-8`, not the G2D/PICA game bridge: it proves the frame loop
reaches the display, which is a prerequisite for M3/M4. LOTUS at 1800 frames:
`last_error` empty, `unowa_present_count == 130`, top screen RGB (16,24,48)
and bottom (24,16,24) — the renderer's clear colours. Game graphics still need
M3/M4.

### Data pointer relocation

ROM-backed data is emitted verbatim, so its stored DS pointers point into
unmapped DS space on the 3DS. The first concrete fault was `_ArcPathTable`
(`gfl_use.c:861`, 235 entries), whose entries name the `a/0/0/N` archive paths
held inside the `PIo2` array; `FS_NormalizePath` dereferenced the raw
`0x020A6BB0` and stopped the guest. `tools/gen_data_image.py` now emits
`unowa_ds_relocate()` / `unowa_gv_relocate()`: a table of (slot, target array +
offset) pairs that rewrites each stored *data* pointer to the emitted array that
holds the bytes it names. It runs in `main()` before `NitroMain()`.

The pass covers the ARM9 static region only, whose addresses are unique. The
generated image reports 292 relocations (235 of them `_ArcPathTable`) and 895
skipped words, which are Thumb/ARM code pointers (bit 0 set, or no data target)
and are deliberately left alone: function-pointer relocation needs a DS address
to compiled-symbol map. Overlay-internal pointers are also untouched, because
237 overlays share 58 load slots and cannot be disambiguated until the overlay
packaging work. Verified in LOTUS: `_ArcPathTable[0]` reads `PIo2 + 0x708` at
runtime, exactly `0x020A6BC0 - 0x020A64B8`, and the boot no longer stops on the
`0x020A6BB0` unmapped read.

Result: `lotus_run_until` reaches 1200 frames with `last_error` empty, four
live guest threads, and `unowa_overlay_log_count == 6`. The log records the
`GF_OVERLAY_Load` calls reached during boot. Those handles are the *addresses*
of the `const unsigned int ov*_id` data variables rather than the small
numeric IDs, because `.set` absolute symbols make `3dsxtool` reject the image
(`Relocation to invalid address`). Normalising the handle at the overlay
boundary is M3 work. The HID hook is linked but not exercised: the guest is
still in startup with threads blocked on 3DS synchronisation objects.

Kohjoh and sinjoh were audited for reusable work; the decisions and citations
are in `docs/reuse-audit.md`.

## Open blockers

- No physical Old 3DS in this environment. LOTUS and Azahar are integration
  debuggers, not timing authorities. All M0 claims are emulator-tested at most.
- The generated data image is a link-time artifact. Zero-filled definitions
  cover BSS/runtime addresses; M2 must place all data at its real DS addresses
  before the image can be considered game-correct.
- Aletheia's `arm9data.py` needs the DSi modcrypt key-scrambler constant to
  decrypt `arm9i.bin` and generate `twl_0272EB9C.inc`. `tools/modcrypt.py:26-46`
  documents that it lives in AES hardware, not in BIOS or firmware ROM. The
  supplied `biosdsi9.bin`, `DSi_Firmware.bin`, `dsifirmware.bin` and
  `dsi_nand.bin` were all scanned with `tools/modcrypt.py check --dsi-bios`
  and none contains it. A 16-byte little-endian dump via `--dsi-key` is the
  only accepted form. unowa's own generator remains the fallback.
