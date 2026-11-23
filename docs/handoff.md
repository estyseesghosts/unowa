# unowa: progress, lessons and handoff

Status date: 2026-11-22. This document distils 25 commits (`ffbb45b..cdbb933`),
the uncommitted field-map work in the working tree, `docs/status.md`,
`UNOWA.md`, `UNOWA-2.md`, `docs/reuse-audit.md`, `docs/eidos-review.md`,
`docs/frame25-crash.md`, and the transcripts in `docs/transcripts/`.

Status vocabulary is the one `docs/status.md` uses: implemented / compiled /
tested headlessly / tested in emulator / verified on Old 3DS. Nothing in this
project has reached the last one.

## 0. The short version

- The port boots the original game, renders the Game Freak/Pokémon/Nintendo
  logos, the copyright screen, the whole intro movie, the Pokémon Black title,
  the start menu, the new-game intro, name entry, and the first field map
  (Nuvema Town) in 3D.
- M0, M1 and M2 are complete for their exit criteria. M3 is substantially
  working. M4 and M5 are in progress. M6 (widescreen), M7 (optimization),
  M8 (coverage) and M9 (packaging) have not started.
- The architectural unknowns are mostly resolved. The remaining work is
  breadth (all the game's scenes and systems), correctness (display capture,
  VRAM banking, shading), performance on real hardware, and packaging.
- The port is not close to shippable. There is no physical Old 3DS in this
  environment, so the 30 FPS target, audio cost, memory headroom and thermal
  behaviour are all unmeasured. LOTUS is an integration debugger, not a timing
  authority.
- The two biggest technical risks are the heuristic flat-image generator and
  the source-rewriting layer that feeds it. Both work, but both encode many
  special cases and will be expensive to maintain. The principled replacement
  (Aletheia's own data companions) is now unblocked but not adopted.
- The two biggest handoff risks are machine-local dependencies (sibling
  checkouts, generated sinjoh libraries, ROM extract, LOTUS host config and
  content pack) and the uncommitted field-map work in the working tree.

---

## 1. What unowa is

unowa is a native ARM11 port of Pokémon Black and White for the original
Nintendo 3DS / 2DS. It is not an NDS emulator. The game logic is Aletheia's
reconstructed Gen V source, compiled for ARM11 with devkitARM. The DS hardware
is modelled by compatibility code and rendered through the PICA200. The
platform skeleton, DS memory model, 2D/3D renderer, audio output and content
pack reader come from sinjoh, itself adapted from kohjoh. Coffeepot is the
hardware and engineering reference. Eidos is a design reference only; it is not
a build dependency.

Target experience, from `UNOWA.md`: 30 FPS on stock Old 3DS, 400×240 top and
320×240 bottom, widened 3D view, GPU-native 2D, WideArt, original mechanics,
both editions, `.3dsx`/CIA packaging, user-supplied ROM data. Initial
exclusions: stereo 3D, 60 FPS interpolation, software rendering, xBRZ, live
WideArt generation, New 3DS-only optimizations, and a self-contained
ROM-containing CIA.

Current build artifacts:

- `build/game/3ds/m1-test.3dsx` — about 5.3 MiB, the game build.
- `build/game/3ds/m1-test.elf` — about 52 MiB, unstripped debug ELF.
- `build/black/3ds/unowa-black.3dsx`, `build/black/3ds-prof/unowa-black-dbg.3dsx`,
  and the White equivalents — the four M0 bootstrap builds (150052 / 150332
  bytes each).

The game build is Black only. The White ROM is verified and pinned, but there
is no White content pack and no White game build/test yet.

---

## 2. Roadmap status

The milestone table in `docs/status.md` lags the detailed evidence below it.
This is the corrected reading.

| Milestone | State | What exists | What is missing |
|---|---|---|---|
| **M0 — Bootstrap** | Complete. Implemented, compiled, tested in emulator. | Four `.3dsx` (Black/White × release/profile), separate object trees, build locks, async SD log, 30 Hz clear presentation, ABI assertions, pinned toolchain. | Nothing for M0. The clear path is deliberately not the game renderer. |
| **M1 — Game compilation** | Complete. Implemented, compiled, linked, tested headlessly and in emulator. | Aletheia `src/game` closure: 1,196 objects, 58 MiB archive. Gen V/Gen IV and MSL/newlib symbol collisions resolved by `objcopy --redefine-sym`. Metrowerks asm, MSL runtime, BIOS SVCs and fixed-address symbols replaced in C. `link-final` produces a bootable `.3dsx`. | White game build/pack; authoritative data companions instead of the heuristic generator. |
| **M2 — Native boot** | Complete in practice, emulator only. | Game's own `NitroMain` loop runs one tick per DS V-blank (60 Hz). 15–18 overlays load at runtime. DS firmware state, shared work area, PM, PXI, NVRAM, save device, input, V-blank pacing and unsupported-service logging work. The title and intro execute original code. | Host32/3DS checkpoint agreement was never built. Hardware verification. The table still says "in progress"; the exit criterion is met. |
| **M3 — 2D** | Substantially complete, not formally closed. | sinjoh's `g2d.c`, `atlas.c`, `pica/gpu.c`, `pica/targets.c` adapted. Text, affine, extended and direct-colour BGs; 4bpp/8bpp tiles; OBJ; windows; alpha blending; BLDY/master brightness; mosaic state; per-line HBlank shadow; VRAMCNT decode. Logos, copyright, full intro movie, title, start menu, name-entry keyboard. | 256-colour bitmap BGs remain unsupported. WideArt side bands are a stub. Some effects/edge cases are unvalidated. The intro's vertical striping is unresolved (may be the game's film-strip motif). No full oracle corpus. |
| **M4 — 3D** | In progress; major slice working. | `kj_g3.c`, `g3d.c`, `textures.c`, PICA backend imported. GPU-visible geometry storage (`linearAlloc`), geometry swap seam, 3D publishing, title 3D on top, title phase 2 3D on the lower screen via `GX_DISPMODE_VRAM_D`, Nuvema Town field in 3D. | `DISPCAPCNT` is not modelled as a real 256×192 capture with EVA/EVB A+B blend. VRAM bank remapping is approximated by a flat BG VRAM view. Title phase-2 shading is not oracle-verified. Battle/field 3D coverage and culling/retained geometry are not exercised. |
| **M5 — Gameplay** | Early in progress. | Title → start menu → NEW GAME → gender/name entry → Professor Juniper intro → Cheren/Bianca intro → first field-map entry. D-pad and touch input; text advancing; save device exists; ARM7 sound clock runs. | Walking/collision/camera interaction, save/continue flow, wild/trainer battles, menus beyond the start menu, audio output (LOTUS still reports zero samples), script coverage, resource loading at scale. The next field stop after Nuvema Town is not yet characterized. |
| **M6 — Widescreen** | Not started. | Nothing. The picture is the DS 256×192 composition at 1.25× (320×240), centred at +40 px on the 400 px top screen. | Wider 3D camera/culling; widened 2D logical composition; B/W WideArt corpus; side-band art; touch-region relocation. |
| **M7 — Optimization** | Not started. | Instrumentation counters exist (mainloop, V-blanks, presents, g3 frames/verts/batches, per-target draws, direct-BG cells). | Real O3DS profiling; frame-time budgets; memory ledger; audio mixer cost; GPU contention; hot-path removal; target 30 FPS signoff. |
| **M8 — Coverage** | Not started. | Ad hoc LOTUS/ melonDS runs only. | Deterministic saves, recorded inputs, scene corpus, regression comparisons, edition/language/season coverage, lifecycle tests. |
| **M9 — Packaging** | Not started. | Manual content pack; manual LOTUS asset registration. | Release build flow, CIA forwarders, installer, clean install/cold boot tests, save integrity, documentation, legal/provenance review. |

### How far along is it, honestly?

By milestone count, four of ten are effectively complete (M0–M3), M4 is more
than half done and M5 has begun. That sounds like 50%+. It is not. The milestone
list is ordered by dependency, not by effort. M6–M9 contain the long tail:
every remaining scene and battle, all of the widescreen art and layout work,
all hardware performance work, a full-game regression corpus, and packaging.
The current state is a strong vertical slice through the first hours of the
game, not a nearly finished port.

A fair one-sentence estimate: **the port is at the M4/M5 boundary — the engine
and the first playable slice exist, but the game is not yet broadly playable,
and none of the shipping work has started.**

---

## 3. What has been accomplished, milestone by milestone

### M0 — Bootstrap

- Pinned Aletheia `50731dd`, sinjoh `1774be6`, kohjoh `7a98ace`, Eidos
  `2319b45`, coffeepot `209c8ed` in `upstream.lock`; toolchain pins are
  devkitARM r68-1 / GCC 16.1.0, libctru 2.7.1, citro3d 1.7.1, 3dstools 1.3.1,
  picasso, tex3ds.
- Four build variants with isolated object trees and a per-tree `mkdir` lock.
- Asynchronous SD log, edition identity, path roots, ABI assertions
  (`engine/runtime/abi_assert.c`).
- The first LOTUS runs captured pure black; the cause was a missing
  `C3D_FrameDrawOn` in the clear path. Fixed and re-verified. This was a real
  bug, not a harness artefact.
- `tools/build.ps1` and `tools/build.sh` build the M0 shell. The M0 shell is
  still useful as a toolchain/LOTUS smoke test, but it is not the game build.

### M1 — Game compilation

- Aletheia's `src/game` is compiled with devkitARM against sinjoh's NitroSDK /
  NitroSystem compatibility libraries. The first probe compiled 360 of 411
  core TUs; 938 of 981 overlay TUs. Failures are Metrowerks asm, missing
  generated `.inc` data, GCC struct-size assertions, or files whose symbols
  sinjoh already provides. They are listed with reasons in
  `engine/game_blocklist.txt` and `engine/game_overlay_blocklist.txt`.
- The final archive holds 1,196 objects (58 MiB). The data image covers 238
  modules in 247 spans; the pointer tables hold roughly 7,200 function
  pointers and 3,000 flat data pointers.
- `tools/rename_genv.py` resolves the Gen V/Gen IV and MSL/newlib collision
  problem: it computes collisions with `nm`, rewrites both archives with
  `objcopy --redefine-sym`, and keeps the game's own implementation without a
  duplicate definition. `--start-group` is required because the archives call
  each other.
- `engine/stubs/port_stubs.c` supplies what the DS got from linker symbols,
  BIOS SVCs or the MSL runtime: fixed-address values, `bw_limit_get`, OV230
  call-through stubs, memory helpers, RC4, SHA-1, HMAC-SHA1, the `CPSi_big_*`
  limb readers, non-TWL console identity, and DS hardware no-ops.
- `engine/stubs/card_backup_port.c` replaces the DS card-bus backup device with
  a 512 KiB flash image backed by `sdmc:/3ds/unowa/saves/<edition>.sav`.
- `tools/pack_game_data.py` builds a B/W content pack in sinjoh's PLATPAK v1
  format from Aletheia's FNT file-ID mapping. The Black pack is 247 files /
  198,011,852 bytes, build id `unowa-nitrofs-1`.
- `make -f engine/game.mk link-final` produces a real ARM11 ELF and `.3dsx`.
  `verify-pins` and `verify-rom` are reproducible checks.

### M2 — Native boot

The important result is that the game's own loop now runs at 60 Hz, not that
the screens are no longer black. The fault chain, in the order it was found:

| Symptom | Root cause | Fix |
|---|---|---|
| Fault at `0x04004000` in `OS_IsRunOnTwl` | DS SCFG register read | Port returns non-TWL; `OS_GetConsoleType` redirected to `0x80000001`. |
| Fault at `0x04004004` in `SCFG_SetCpuSpeed` | `reg_SCFG_CLK` read | Patcher fixup replaces the read with 0. |
| Fault at `0x04000208` (`REG_IME`) | Bare DS I/O register access in game source | `tools/gen_port_ioreg_header.py` rewrites all 170 `IOREG_ADDR(...)` macros onto sinjoh's `NitroPort_*` shadow arrays; forced include with the original guard. |
| Boot spins in `PMi_WaitBusy` | `NitroPort_InitArm7Services()` never called; PXI FIFO drain gated on real ARM IRQ mode | Call `NitroPort_InitArm7Services()` before `NitroMain`; make the drain unconditional; initialize the Gen V PM state directly (`unowa_pm_ensure_ready`) because sinjoh's `os_init.o` initializes a different PM state. |
| Unmapped write at `0x02000008` | `OS_CreateExtraHeap` hardcodes `(Cell *)0x02000000` | Dedicated `unowa_extra_heap[0x4000]`, not `NitroPort_MainMem`, which overlaps the SDK arena at `0x4e1000`. |
| Jump to `0x02081e7c` | `MATHi_SHA1ProcessMessageBlockFunc` holds a raw ROM function address | Port defines the pointer to the C transform and excludes it from the data image. |
| Jump to `0x02155160` | `_wifiDeviceTable` holds DS Thumb pointers into ov009 | Inert 51-slot stub table in `port_stubs.c`; excluded from the data image. |
| Frame-25 write to `0xDFECD4CA` | ARM9 autoload bytes (ITCM/DTCM) copied over BSS after zeroing | Each module's copy stops at `ram_end`; `check_module_bounds` fails the build on an overrun. |
| Soft reset at frame 25 | Key register active-low and never initialized, so every button read as held | `engine/runtime/ds_boot.c` runs `NitroPort_BootFirmware`: no buttons, lid open, cartridge header, owner info, reset parameter. |
| Game thread never leaves `OS_WaitIrq` | `SDK_AUTOLOAD_MAIN_TWL_START + 0x3FF8` and `SDK_AUTOLOAD_DTCM_START + 0x3FF8` were split into different blocks; game `OS_SetIrqFunction` wrote `g_02FE0020` while the SDK read `OS_IRQTable` | Bind the whole `[0x02FE0000, 0x02FE4000)` DTCM window to one block; alias `OS_IRQTable` and `OSi_IrqThreadQueue` to libnitro's copies. |
| Game tick half speed | `C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` under `C3D_FrameRate(30)` blocked the V-blank that the game waits on | `engine/runtime/frame_pacing.c` locks one emulated DS V-blank to one physical V-blank with `C3D_FrameCounter(0)` and `gspWaitForVBlank`; presentation every other V-blank. Measured 635 → 1196 ticks over 1200 frames, 598 presents. |

Observability matters as much as the fixes: `NitroPort_Log` writes to stderr,
which LOTUS does not capture. Overlay loads, unsupported services and
terminations are recorded in guest RAM and read back through LOTUS memory
reads. `-Wl,--wrap=NitroPort_Unimplemented` and `--wrap=OS_Terminate` provide
the bounded logs.

### M3 — 2D

sinjoh's proven 2D stack was imported and adapted, not rewritten:

- `engine/graphics/g2d/g2d.c` translates DISPCNT, BG registers, palettes,
  BG/OBJ VRAM and OAM into ordered GPU passes, with windows, alpha blending,
  BLDY and master brightness.
- `engine/graphics/resources/atlas.c` expands DS tiles into a 1024×1024
  RGBA5551 atlas with exact-compare caching.
- `engine/graphics/pica/gpu.c` owns citro3d state and the 2D shader;
  `pica/targets.c` implements the 1.25× geometry (256×192 → 320×240, centred at
  +40 px on the 400 px top screen).
- `engine/runtime/unowa_frame.c` builds the `kj_frame_input` from the DS memory
  view, the VRAMCNT bank decode and sinjoh's per-line register shadow, and
  runs `NitroPort_HBlankLinesOnce()` before reading it.

Gen V gaps that sinjoh's HGSS path never needed:

- **Direct-colour affine BGs.** The intro is mode 5 with 16-bit BGR555
  bitmaps (`BG2CNT/BG3CNT` bit 7 + bit 2, `GX_SetGraphicsMode(1,5,0)`), not 3D.
  `g2d` now converts each 8×8 cell into an atlas slot, uses bit 15 as the
  alpha test, and addresses it per `G2_GetBG2ScrPtr`.
- **Per-frame quad-arena reset.** The M0 renderer never used the 2D arena, so
  `kj_gpu_quads_reset()` was missing; after the logos the arena filled and
  every intro allocation failed. It is now called where sinjoh calls it.
- **Overlay-resident data.** `objTbl[0].anmTbl` at `0x021BD3E4` is data in
  ov088, but ov016 has a function at the same numeric address. The flat
  resolver now refuses another overlay's function when the value lies inside
  the current module's own data window; 58 words moved from function pointers
  to flat data pointers.

Evidence: 1200 frames, `mainloop 1194`, `pacing_vblanks 1194`, `presents 597`,
18 overlays, no stop. Captures show the Game Freak/Pokémon Company/Nintendo
logos, copyright, the comet, the GAME FREAK title, the castle bridge and the
Pokémon Black logo at the correct scale.

Still open in M3: 256-colour bitmap BGs, WideArt side bands, the intro's
vertical striping, and a systematic oracle comparison for menu/text frames.

### M4 — 3D

- sinjoh's `g3d.c`, `textures.c`, `kj_g3.h` and `kj_lighting.h` are imported.
  Geometry storage is allocated with `linearAlloc` (~1.25 MB for the two
  vertex/index sets), `kj_g3_init` runs, and `kj_g3_take_frame()` feeds the
  frame input.
- The title's geometry was built but never published because
  `G3D_SYSTEM_SwapBuffersMain` stores straight into `reg_G3_SWAP_BUFFERS`,
  while the port's engine is a command stream. The patcher rewrites that store
  to `NitroPort_G3Swap(value)`, which forwards `G3OP_SWAP_BUFFERS` to the same
  seam. Result: `unowa_g3_frames` 0 → 1067, `verts` ~4,900–5,300, and the
  title logo renders as a 3D scene.
- The lower title screen was black because engine A's `DISPCNT 0x1E0918` has
  bits 16–19 = `0xE`, `GX_DISPMODE_VRAM_D`. The title swaps engine A to the
  lower screen with `BG_SYS_SetLcdSwap(0)` and displays the VRAM bank that
  `DISPCAPCNT` fills from the main graphics screen. `g2d` now draws the live
  layers for VRAM-display modes; `bottom3d` reached 573 draws / 56,655 batches.
- The field map runs: Nuvema Town in 3D at frame 13,480, `mainloop 13,021`,
  `g3_frames 5,549`, 42 overlay-log entries.

Still open in M4: `DISPCAPCNT` is an approximation (live layers, no real
256×192 capture and EVA/EVB A+B blend), the flat BG VRAM view does not model
bank remapping, title phase-2 shading is not oracle-verified, and battle/field
3D coverage is untouched.

### M5 — Gameplay

- The title-to-menu transition was blocked on audio, not graphics:
  `PM_VOICE_IsSlotActive(fanfareHandle)` waited for a wave-out channel that
  never retired because nothing mixed. Importing sinjoh's
  `engine/audio/audio.c` and calling `kj_audio_init()`, then pumping
  `NitroPort_SoundPumpReplies` and `kj_audio_service` from
  `NitroPort_OnFrame`, made the title demo advance 3→4→5→6→7 and exit to the
  start menu.
- The start menu lists NEW GAME / MYSTERY GIFT / NINTENDO WFC SETTINGS. A on
  NEW GAME starts Juniper's intro; text advances; name entry works with D-pad
  and touch; the intro runs through Cheren and Bianca to the first field map.
- The save device is real: a 512 KiB flash image at
  `sdmc:/3ds/unowa/saves/<edition>.sav`, erased-flash initial state, reads and
  writes at the requested addresses.
- The first field map exposed three DS assumptions:
  - ov230's static-initialiser table ran out of the flat image as data. The
    flat generator now emits a native initialiser table per module, or refuses
    the whole table when an entry has no native function.
  - DS VRAM pointers reached libnitro's block clears
    (`MIi_CpuClearFast(..., HW_LCDC_VRAM, ...)`). `unowa_ds_to_host` now covers
    every DS region sinjoh shadows, and `ds_memory_ops.c` wraps the SDK's
    block clear/copy helpers at the final link.
  - A local `#define ... 0x021DE334` hid a DS address from the literal
    rewrite. `rewrite_address_macros` now applies the pointer-cast rule to
    local address macros.
- The next stop after Nuvema Town is not yet characterized. The field is
  running, not yet playable.

### What is notably *not* done

- White at the game level: no White pack, no White `m1-test` build, no White
  test run. Only the M0 shell builds for White.
- A real `DISPCAPCNT` capture path, VRAM bank remapping, and verified title
  phase-2 composition.
- Walking, collisions, camera control, save/continue, wild/trainer battles,
  most menus, most scripts, C-Gear/WFC, and the full audio path.
- 256-colour bitmap BGs, WideArt, widescreen camera/culling.
- Any real-hardware measurement, performance work, memory ledger, or 30 FPS
  proof.
- The regression corpus (saves, recorded inputs, scene captures).
- Release packaging, forwarders, install/cold-boot tests, legal review.
- Automated tests for the generators, crypto helpers, relocation rules or
  stubs. Verification is manual LOTUS runs.
- The authoritative Aletheia data-companion path. The plain ARM9 link now
  passes and Aletheia's `arm9data.py`/`datagen.py` can in principle emit typed
  companions, but unowa still uses its own heuristic flat generator.
- A real B/W ROM hash in the pack header/manifest. The pack currently borrows a
  supported Platinum SHA-1 because sinjoh's `pak_open` accepts only three
  Platinum hashes.

---

## 4. What went well

**The architecture survived contact with the game.** The flat main-RAM model
(`NitroPort_MainMem + (addr & 0x7FFFFF)`, linker `PROVIDE` names, data loaded
per module) retired an entire class of bugs that had produced a new crash every
session: guessed BSS sizes, aliases drifting apart, per-object relocation, and
overlay data corruption. The decision to keep code native and flat data
per-module is the single most important design choice in the port.

**Reuse was real, not rhetorical.** M3 was mostly plumbing because sinjoh's 2D
stack already handled text/affine BGs, OBJ, windows, blending, brightness and
atlas caching. The audio fix was a proven sinjoh component, not a new mixer.
The frame bridge followed sinjoh's `kj_ctr_vblank` and coffeepot's frame-pacing
article. This is exactly the "integration project, not a new engine" thesis of
`UNOWA.md`, and it held.

**Diagnosis was measurement-driven.** Almost every fix is tied to a number or
an artifact: `nm`/`objdump`/`addr2line`, LOTUS register and memory reads,
conditional breakpoints, `citrace`, generated-source inspection, and frame
counters. The frame-25 crash is the model: a reproducible SHA-256, a
conditional breakpoint that captured the second faulting address before the
worker aborted, a stack and register capture, and a build-time regression check
that fails on the old generator. The team did not settle for "it boots now".

**Fixes were general, not per-site, once the pattern was recognized.** The
shared-work-area rewrite covered 59 files; the DS I/O literal rewrite covered
all pointer-cast sites; the DTCM alias rule covered the whole window; the
block-clear wrapper covered five other overlays that would each have produced a
separate crash; the `unowa_ds_to_host` region table now matches the header
generator and the flat relocator. This is why the port advanced from frame 2 to
the field map in a day.

**Documentation and commits are unusually good for a bring-up.** Every commit
message states the fault, the cause, the fix and the LOTUS evidence. `status.md`
records failed hypotheses, negative results and exact source citations. That is
why this handoff can be written at all. The 25-commit history is bisectable.

**The collaboration worked.** The user supplied the ROMs, mwccarm, DSi dumps,
LOTUS host configuration, and two crucial corrections: do not build on Eidos,
and use the real ROM in melonDS as the visual oracle rather than Eidos. The
agents stopped when LOTUS was unavailable, asked for asset registration instead
of hacking around it, and reported honestly when a build was unverified.

**The speed is real.** In one day the project went from an empty repo to a
native build that renders the intro, title, start menu, new-game intro and first
field map. That is not normal; it is the payoff of importing proven components
and refusing to rewrite them.

---

## 5. What went poorly, why, and how it was recovered

These are the failures that cost the most time or created the most risk. They
are recorded because the same shapes will recur.

### 5.1 The heuristic data model was a death spiral

**What happened.** `tools/gen_data_image.py` emitted one C array per DS data
symbol, with guessed sizes. Every boot found a new failure: 64-byte BSS
placeholders spilling into neighbours, aliases becoming separate arrays, ARM9
and overlay objects colliding at the same address, `_ArcPathTable` pointing at
bytes no symbol named, a glyph table sized 64 instead of 512. Each fix made the
generator more elaborate. It took the project from frame 2 to frame 25, then
stalled.

**Why.** Aletheia's metadata has complete coverage but no reliable sizes for
BSS, code deliberately reads past declared object sizes, and data contains
pointers to bytes no symbol names. The model assumed DS memory is a set of
separately sized C objects. It is not.

**Recovery.** Eidos's `armdata.md` described the correct model: one flat block
with the game's data at its real DS offsets. sinjoh already allocates that block
(`NitroPort_MainMem`, 8 MiB). The flat model replaced per-object arrays with
linker symbols at each byte, loaded module data into its DS window, and
relocated only the words that are actually pointers, using Aletheia's typed
object map plus exact-match guards. The old generator was deleted.

**Lesson.** When the same class of bug recurs after every fix, the model is
wrong, not the latest site. Read the reference port's design document before
making the generator smarter.

### 5.2 Per-site patching instead of class-level fixes

**What happened.** `0x02FFFC3C` (the DS V-blank register) was patched in
`mystery_data.c`, then `field_g3d_mapper.c`, then `p_status.c`, then `record.c`,
then `pm.c`; the next boot stopped in a sixth file. SCFG, IME, BLDCNT and
`HW_LCDC_VRAM` each produced their own crash-and-rebuild cycle. The field map
produced three more one-off stops.

**Why.** The first fix was local and plausible, so the next identical fault
looked like a new problem. The cost of a full audit felt larger than one more
patch. That was wrong every time.

**Recovery.** Audit the whole tree, classify the sites, and write one rule:
the shared-work-area rewrite, the `IOREG_ADDR` header redirect, the pointer-cast
DS I/O literal rewrite, the address-macro rewrite, the `unowa_ds_to_host`
region table, the block-clear wrappers. `tools/audit_ds_literals.py` exists to
keep the remaining exposure visible. The build-tree patcher is applied to every
compiled TU that matches, not a hand-maintained list.

**Lesson.** A repeated fault is a pattern. Stop, count the sites, read the
platform constraint, and fix the class. One general rule is cheaper than five
rebuilds.

### 5.3 Eidos was both the only Gen V reference and untrusted

**What happened.** `UNOWA.md` names Eidos as the source of Gen V execution
knowledge and build generators, but the user explicitly distrusted it. Early
agents oscillated between "use Eidos's tools" and "do not"; one session spent
time on Eidos's identity-mapped host model before confirming it cannot work on
3DS. Another used Eidos's title documentation as a visual oracle and was
corrected.

**Why.** There was no written decision that separated "read Eidos for engine
semantics" from "do not build on Eidos". The roadmap and the user's instruction
were in tension, and the tension was resolved ad hoc.

**Recovery.** `docs/eidos-review.md` records what Eidos solved and what unowa
takes from it, with no code copied: the flat data model, the pointer-word
typing problem, the literal-address residue categories, the register-argument
hazard, the overlay namespace model, and the virtual ARM7 service list. The
build has no Eidos dependency. For visual correctness, the project switched to
the real ROM in melonDS.

**Lesson.** Write the boundary down. "Reference, not dependency" is a real
architectural decision and belongs in the docs, not in a chat aside.

### 5.4 mwccarm was mistaken for a linker/compiler solution

**What happened.** The user suggested mwccarm after seeing undefined-symbol
work. The agent initially treated it as a way to compile the game; the
follow-up question ("why do you need to wire for undefined symbols?") exposed
the confusion. mwccarm emits DS ARM9 ABI objects; sinjoh's libraries are
ARM11 GCC objects. They cannot link. The 247 undefined symbols were a link
closure problem, not a compilation problem.

**Why.** The tool was supplied for Aletheia's own data tools, but the
conversation blurred "compile the game" with "run Aletheia's generators".

**Recovery.** The ABI argument was documented from `sinjoh/engine/compat/
common.mk`. The game stays on devkitARM GCC. mwccarm is used only to enable
Aletheia's data-companion path. The link closure was closed with blocklists,
selective renaming, C reimplementations and port stubs.

**Lesson.** A better compiler cannot define missing functions. Check the ABI
before changing the compiler; check `nm` before assuming a library owns a
symbol.

### 5.5 Overlay loads were counted but never worked

**What happened.** Early M2 evidence reported six overlay loads. They were all
failures: unowa never called `NitroPort_SetOverlays`, so `FS_LoadOverlayInfo`
returned false and the "loads" were attempts. This was only discovered when
the flat model made overlay data loading real and the count changed behavior.
Similarly, the first overlay handles were addresses of `ov*_id` variables, not
numeric IDs.

**Why.** The counter measured "the hook ran", not "the overlay loaded". The
observable was convenient, not semantic.

**Recovery.** The flat overlay loader copies each module's data into its real
DS window, relocates pointer words, zeroes BSS, and hands the SDK a native
static-initialiser table. `unowa_overlay_log_count` now records real loads
(15–42 depending on the scene), and `NitroPort_OverlayInfo` is wrapped.

**Lesson.** A counter is only evidence if its semantics match the claim. Verify
the success path, not just the call.

### 5.6 Build-system traps cost rebuild after rebuild

**What happened.**

- `rename_genv.py` used `ar rcs` on an existing archive; removed objects stayed
  alive. A stale `os_emulator.o` produced duplicate definitions and misleading
  link results.
- `gv_sdk_sources.txt` and the blocklists were not make prerequisites, so a
  rebuild after editing them silently did nothing ("Nothing to be done").
- `$(file >...)` expanded before the recipe's `mkdir`, so a clean build had no
  `ren/` directory.
- Patched build-tree copies lost their original directory for quoted includes;
  `pm.c` picked up sinjoh's `mi_dma.h` and conflicted on `u32`. Fixed with
  `-iquote` and include ordering.
- GCC 16 recognized the MSL `strlen` definition as the builtin and compiled it
  to `b .` (an infinite self-branch), freezing the main thread. `-fno-builtin`
  fixed it; `-fno-builtin-strlen` alone did not.
- Explicit-rule `$*.c` expanded to the build path; PowerShell's `rm` is
  `Remove-Item`; Python heredocs wrote literal `\n`, `\b` and backslashes into
  `game.mk` and the generator.

**Why.** The build has four layers (M0 Makefile, game archive, Gen V archive,
generated flat image) and each has its own dependency graph. The tools were
written incrementally, so the graph was repaired only after a failure.

**Recovery.** Unlink archives before rebuilding; make source lists and
generators explicit prerequisites; use order-only prerequisites for
directories; use `-iquote`; add `-fno-builtin`; use `make -k` with a log and
check `$LASTEXITCODE`; use the Edit tool for all multi-line source/make edits.
These are now in the history and in the transcript lessons.

**Lesson.** Generated files, archive members and include search order are part
of the build's correctness. If a change "did nothing", check the dependency
graph before assuming the code is wrong.

### 5.7 Stale symbol addresses created measurement fiction

**What happened.** Symbol addresses shift on every relink. Counters were read
at the previous build's addresses and produced conclusions like "the scene
ended", "pack result is `0x1F0000`", "input is dead", "no progress". In one
case a stale `KEYINPUT` read at `IoReg + 0x130` looked like a dead input path.
The same mistake recurred across sessions.

**Why.** The addresses were cached in notes and reused after a rebuild. The
read succeeded, so the wrong address produced plausible-looking data.

**Recovery.** Re-run `arm-none-eabi-nm build/game/3ds/m1-test.elf` before any
memory read or breakpoint, after every build. This is now written in several
places, including this document.

**Lesson.** In an environment where every build relocates BSS, a symbol address
is build-scoped. Treat cached addresses as invalid by default.

### 5.8 Fault PCs, hex conversion and struct offsets were misread

**What happened.** LOTUS sometimes reports the PC at the start of a conditional
block or a return address, not the faulting instruction. Agents chased
`CLACT_ApplyActorTransform` when the real fault was a data read in
`NNS_G2dGetOamSoftEmuAutoZOffsetStep`. Decimal-to-hex conversions in the head
produced wrong breakpoints and wrong symbols. `TitleDemoWork` fields were read
at guessed offsets and returned nonsense (`result = 0x160003`).

**Why.** The tools' stop reports are not uniformly instruction-accurate, and
manual conversion is error-prone. The correct source of truth (the access
record, the stack, the SDK header offsets) was available but slower to consult.

**Recovery.** Trust the access record (`type`, `address`, `width`) and the
stack chain; use `addr2line` on every frame; convert numbers with
`printf`/Python; read the type header (`startmenu_types.h`, `gx.h`) before
reading fields; use SDK enums instead of bit arithmetic. The `0x20A8218` vs
`0x020A8218` case also showed that literal regexes must accept both spellings.

**Lesson.** When a debugger report and a source line disagree, believe the
memory access record and the bytes, not the single PC.

### 5.9 LOTUS process failures

**What happened.**

- One agent lost the LOTUS MCP namespace, concluded LOTUS was unavailable, and
  wrote a custom MCP harness script in the repo. The user rejected this
  emphatically; LOTUS was working for other agents. The script and stray
  artefacts were removed.
- Across sessions, MCP host restarts invalidated every session handle; the
  four-session limit blocked new sessions until old ones were stopped.
- Reloading an asset into a live primary session silently broke content-pack
  opening (`pack_result` stayed `-1`, no overlays). Fresh secondary sessions
  copied from the `unowa` card reproduce the boot deterministically.
- GDB attaches but does not drive the emulated CPU; its breakpoints and
  hardware watchpoints do not fire while LOTUS advances the guest. LOTUS write
  watchpoints work but switch to single-instruction dispatch, making a
  one-frame window take tens of minutes.

**Why.** The MCP bridge is host-side and session-scoped; the emulator's debug
surface is not a full in-circuit debugger. The agents treated both as stable
local tools.

**Recovery.** Drive LOTUS directly through the MCP tools; never commit a
harness. Register assets by editing `~/lotus-data/citnlo/host.json` and asking
the user to reload the MCP. Use fresh secondary sessions with `card: "unowa"`.
Use execute breakpoints and memory reads as the primary debug path; reserve GDB
for parked stacks. Stop and report when LOTUS cannot be driven.

**Lesson.** The project rule "NEVER WORK WITHOUT LOTUS" is not bureaucracy. A
port that cannot be observed is not being verified. The same applies to the
user's environment: do not invent a substitute.

### 5.10 Commits came late, and work sat uncommitted

**What happened.** The flat data model was held uncommitted until a clean boot,
against the explicit "commit as you go" instruction. The M4 scaffolding was
uncommitted for several turns. In both cases a server restart or context loss
could have destroyed the only copy of verified work. The final history is
clean, but the process was not.

**Why.** The agent wanted each commit to represent a verified, booting state.
That is a good instinct, but it turned into waiting for a perfect state.

**Recovery.** Commit after the link succeeds and the evidence is recorded, with
an honest message about what is verified and what is still open. Keep
experimental probes in separate commits or remove them. The field-map work is
currently in exactly this state and must be committed before handoff.

**Lesson.** A commit is a checkpoint, not a claim of completion. An honest
"in progress, boot verified to frame N" commit is more valuable than a perfect
uncommitted tree.

### 5.11 Tooling dead ends

**What happened.** Time was spent scanning DSi BIOS/firmware/NAND dumps for the
modcrypt key even though `modcrypt.py` says it lives in AES hardware. Hardware
watchpoints in CITnLO did not report. GDB's hardware watchpoints did not drive
the guest. A blanket overlay-function-pointer scan matched 6,228 words,
including a certificate blob and a version string; a typing-gated rule reduced
it to exactly two real pointers. A "unported code" guard flagged 564 words and
had to be reverted.

**Why.** The tools looked like they should work, and the cost of the detour was
not obvious until it was paid. The blanket rules were written before measuring
their hit counts.

**Recovery.** Stop when the documentation says a source cannot contain the
answer. Measure a heuristic's matches by hand before shipping it. Prefer exact
typing/entry matches over value-range scans. Treat a large hit count as a
signal that the rule is wrong.

**Lesson.** A rule that produces hundreds of hits is not a discovery; it is a
bug in the rule. Count first, inspect a sample, then commit.

### 5.12 The title was misdiagnosed repeatedly

**What happened.** The title was assumed to be 2D, then assumed to be missing
`DISPCAPCNT` capture, then blamed on engine B's sprites, then on CLACT. The
eventual causes were three separate things: a 7-digit main-RAM literal
(`0x20A8218`) that the patcher's regex missed, a direct store to
`reg_G3_SWAP_BUFFERS` that bypassed the command seam, and `GX_DISPMODE_VRAM_D`
being treated as unsupported by `g2d`. The user's kohjoh hint ("both games put a
3D render of the box character on the bottom") was the key that led to the
display-mode branch.

**Why.** The title changes display mode, screen assignment and capture state
within a few hundred frames. A single capture or a source-level reading does
not describe the whole sequence. The first hypothesis was treated as the frame.

**Recovery.** Add per-target counters (top/bottom draws, batches, skip
reasons, direct cells), read the actual `DISPCNT`/`POWCNT1`/`DISPCAPCNT`
values at the render point, and check the real ROM in melonDS. The oracle
confirmed the white/silver Reshiram over a checkerboard is the game's art, not
a port bug.

**Lesson.** For a scene that changes state, instrument the state transition,
not one frame. Check the SDK enum before arguing about bit polarity. Use the
real game as the visual oracle.

### 5.13 Claims occasionally outran the evidence

**What happened.** The status doc at one point implied the M0 clear-frame
"1800 frames, 130 presents" result was the game; it was the M0 harness. The
frame-16 stray store was described as a symptom of the old data model before
its writer was identified. A repro note claimed steps that had not been run.
The docs were corrected, but the pattern is worth recording.

**Why.** A plausible narrative is easy to write after a fix; it is harder to
separate "observed", "inferred" and "not tested".

**Recovery.** The status vocabulary (implemented / compiled / headless /
emulator / hardware) and the convention of citing the exact command and counter
values. Negative results are recorded explicitly.

**Lesson.** Write only what was run. Mark inferences as inferences. The next
agent will treat the doc as ground truth.

---

## 6. What we learned about unowa

### 6.1 The architecture

unowa is three things linked together:

1. **Aletheia's game code**, compiled natively for ARM11. Most data is
   `extern` at a DS address, not defined in C. Function pointers and data
   pointers are stored as DS addresses in ROM.
2. **sinjoh's NitroSDK/NitroSystem compatibility layer**, which models DS
   memory, I/O, PXI, ARM7 services, GX, sound and the card/FS interfaces.
3. **unowa's port layer**: the flat main-RAM image, DS boot firmware, save
   device, input forwarding, frame pacing, 2D/3D bridge and diagnostics.

There is one authoritative frame loop (`NitroMain`), one graphics state, one
audio pipeline, one save backend. That is deliberate and must stay true.

### 6.2 DS addresses reach the port by exactly three routes

This is the most useful architectural fact for debugging:

1. **Named data symbols** are bound by `flat_names.ld` to
   `NitroPort_MainMem + offset`. A `PROVIDE` name is a linker symbol at the
   byte, so sizes are never guessed and aliases share storage.
2. **`IOREG_ADDR(...)` macros** are rewritten by the generated
   `build/game/3ds/gen/port_sdk_ioreg.h` to sinjoh's `NitroPort_*` arrays. A
   forced `-include` with the original guard catches relative includes.
3. **Bare literals and local `#define`s** are rewritten only by
   `tools/patch_sources.py`. A TU is patched only if a rule matches. A literal
   in an unusual spelling (`0x20A8218`), a local macro, or a pointer formed
   inside SDK code can escape all three.

When a fault address is unmapped, identify which route should have handled it.
If the answer is "none", the fix is a new general rule, not a one-off patch.

### 6.3 Symbol identity is the central hazard of the hybrid link

On the DS, one address can have several names, and one name can mean different
storage in different overlays or translation units. In unowa, each name can
become a different buffer or function. The failures all have the same shape:

- `OS_SetIrqFunction` vs `OS_IRQTable` / `g_02FE0020`.
- `SDK_AUTOLOAD_MAIN_TWL_START` vs `SDK_AUTOLOAD_DTCM_START`.
- `PM_Init` in sinjoh's `os_init.o` vs the Gen V PM state at `g_02153000`.
- `GX_LoadOBJPltt` vs `GX_LoadOBJPltt + 8` from a mis-resolved table.
- `0x021BD3E4` as ov088 data vs a function in ov016.

The reliable test is not "does the symbol exist" but **"do the writer and the
reader resolve to the same address?"**

### 6.4 Overlay model

- 237 overlays share 58 load slots. The same numeric address means different
  code and data at different times. Address-based ownership across modules is
  unsafe.
- Code is native and linked once. Data is flat and copied into the module's DS
  window on each load, with pointer words relocated and BSS zeroed.
- Static-initialiser tables are code. The flat image is data, so an initialiser
  table must be emitted as native function pointers or refused as a whole.
- `layout.json` is the authority for `ram_start`/`ram_end`/`bss_end`/`sinit`
  ranges. `function_addrs.tsv` is the authority for function starts; the
  `ovNNNd` region (decrypted overlay listing) is a separate view.

### 6.5 The DS boot state the game assumes

The game was written for a DS whose ARM7 and firmware had already done work.
The port must reproduce that state:

- Zeroed BSS. `_SndSlots` is released before initialization because the DS
  guarantees zeros.
- Active-low key registers, with "no buttons held" written at boot. Otherwise
  L+R+Start+Select looks held and the game soft-resets.
- The firmware shared work area at `0x02FFFxxx`: V-blank counter, buttons/lid,
  cartridge header, owner info, lock words.
- A V-blank counter that advances every frame (the ARM7 did it on hardware).
- A backup device that identifies as 4 Mbit flash and returns erased `0xFF`
  for a never-saved cartridge.
- A sound driver clock. The ARM7 mixer tick is what retires wave-out channels;
  without it, `PM_VOICE_IsSlotActive` never clears.
- Non-TWL console identity. sinjoh's `0x82000001` sets the TWL bits that
  Aletheia's Gen V code tests, so unowa redirects the game's
  `OS_GetConsoleType` to `0x80000001`.

### 6.6 Frame model

- Black/White ticks once per V-blank (60 Hz). It does not have HGSS/Platinum's
  two-V-blank cadence.
- Presentation is separate: 30 Hz, every other DS V-blank. Skipping a picture
  must never skip a game tick.
- `NitroPort_OnFrame` is the proven publish point. The source-level
  `GFL_USE_PostIrqUpdate` hook does not fire early in boot; presentation was
  restored there.
- The idle thread drives `NitroPort_Halt` before the game thread reaches its
  main loop. A counter in `NitroMain` is the only reliable way to distinguish
  "V-blanks advance" from "the game runs".

### 6.7 Rendering model

- 2D: DS register/tile/palette/OAM semantics are preserved; sinjoh's atlas and
  per-line shadow do the work. Direct-colour affine BGs are Gen V-specific and
  now supported. 256-colour bitmaps are not.
- 3D: geometry is interpreted into PICA draws; the swap register is a command
  seam, not a plain I/O store. `G3D_SYSTEM_SwapBuffersMain` needed a rewrite.
- Display path: `POWCNT1` bit 15 selects which engine is on top; `DISPCNT`
  bits 16–19 select graphics / VRAM A–D / main memory; `DISPCAPCNT` blends
  source A and B with EVA/EVB into a VRAM bank. The title uses VRAM_D and a
  capture; the current port draws the live layers as an approximation.

### 6.8 Where the port is fragile

- The flat generator's heuristics (function-entry matching, holder words, u32
  numeric typing, module windows) are load-bearing. A wrong rule can corrupt
  data silently; a missed rule produces a boot fault far away.
- `patch_sources.py` rewrites 291 compiled files. Its regexes must understand
  every literal spelling, local macro and pointer-cast form. The `0x20A8218`
  and `port_sdk_ioreg.h` cases show how easy it is to miss one.
- `port_stubs.c` is a large collection of reimplementations and stubs. Some
  are faithful (SHA-1, HMAC, `MATH_QSort`, save device); some are deliberately
  inert (wireless, DSi DSP). The boundary must stay documented.
- The data path is Black-specific in practice. White requires its own extract,
  pack and test run.

---

## 7. What we learned about Aletheia

### 7.1 Two source trees, two purposes

- `src/matched/` is the byte-verified record, one function or unit per file.
  Eidos compiles this.
- `src/game/` is the same code merged into likely original translation units,
  verified by `gameverify.py`. unowa compiles this.
- Both inherit the original compiler's assumptions. `src/game` is the right
  base for a port because it preserves unit boundaries and static linkage, but
  it still needs per-file bindings and build-tree patches.

### 7.2 Data is extern-at-address, not C definitions

Almost no game data is defined natively (31 globals and 188 file statics in the
whole archive). Everything else is an `extern` at a DS address. The metadata:

- `layout/data_objects.json` covers every byte of every data tail, but its
  types come from weak evidence. `u32 numeric` often means "loaded as a word
  from a literal pool", not "is a number". `targets` lists are incomplete for
  objects holding overlay functions, and boundaries can split a record.
- `layout/function_addrs.tsv` is the authoritative DS address → name/file/ISA/
  static map. The ITCM region key is `arm9_autoload0_ITCM`, not `itcm`.
- `layout.json` (per edition) gives each module's `ram_start`, `ram_end`,
  `bss_end` and static-initialiser range.

A safe relocation rule combines several independent facts: the map's type, an
exact value match against a function/data start, the source's own declaration,
and the DS memory layout. Exact matching is what makes a loose typing rule
safe.

### 7.3 The code is not portable C

- **Literal addresses.** Hardware and data addresses are written as numbers,
  because mwcc produced the same bytes either way. Redirecting SDK macros is
  never enough. Coffeepot's `ctr/engineering/porting.md:92` warns about this;
  the intro's BLDCNT write and `HW_LCDC_VRAM` clears are concrete examples.
- **Register-passed arguments.** mwcc lets a callee receive arguments the C
  never names, and lets tail-call thunks pass registers through. GCC passes
  only what the C says. Eidos's `regargs.py` found 2,306 sites originally; at
  this pin, 77 remain (mostly DWC/network error setters), none on the current
  boot path. About 290 `void f(void) { g(); }` thunks rely on the same
  behavior but usually compile to `b g` at `-O2`.
- **Metrowerks asm and MSL runtime.** `clz`, `MATH_QSort`, `CPSi_big_*`,
  SHA-1/HMAC, `_dsub`, `_ffix`, `__memcpy` and the string routines are
  hand-written or compiler-specific. They need C translations, not syntax
  rewrites. `-fno-builtin` is mandatory because GCC otherwise replaces the
  MSL `strlen` definition with a self-call.
- **Split units.** `ov016/sceneoptmovconfirm.c` and `...1.c` share a static
  function that mwcc resolved within one unit. The port gives the two split
  functions external linkage.
- **Duplicate names across overlays.** `pokemontrade_demo_common.c` exists in
  ov117 and ov118; ov118's copy is namespaced.
- **TWL/DSi code.** `twl/dsp_process.c` needs WRAM SDK functions absent from
  sinjoh's Gen IV headers. The DSi modcrypt key lives in AES hardware, not in
  any BIOS/firmware/NAND dump; the `twl_0272EB9C` data cannot be generated from
  the user's dumps.

### 7.4 Aletheia's tooling can do better than unowa's heuristic generator

The important discovery from the Claude session: Aletheia's own plain mwcc link
of `arm9_main` now passes (`build/gamelink/arm9_main/ov.elf`, 407 files,
626,352 code bytes, no diffs). With that link in place, `tools/arm9data.py` and
`datagen.py` can emit typed data companions with real object boundaries,
aliases, symbolic pointer targets, and BSS extents. This is exactly the
principled replacement for `gen_flat_image.py`'s heuristics.

Two things blocked it earlier:

- `gendata.generate_all()` insists on `twl_0272EB9C`, which needs the DSi key.
  `tools/aletheia_run.py` removes that one SPEC in-process, without editing the
  checkout. Only `twl/card_rom.c` consumes it, and unowa is non-TWL.
- Aletheia's tools expect a native Windows Python; devkitPro's MSYS Python
  reports as Cygwin and looks for wine. `C:\Program Files\Python315\python.exe`
  was used successfully.

This path is **unfinished**, not disproven. It is the highest-value
maintainability project in the repo.

### 7.5 Legal and provenance

Aletheia's `src/` has no license grant. Its tooling/docs are MIT. sinjoh and
kohjoh are GPL-3.0. Eidos is MIT. A user-supplied asset pack does not resolve
the rights to distribute a precompiled executable containing Aletheia-derived
game code. Review this before any release. Never commit ROMs, extracted assets,
packs, saves or captures.

---

## 8. Lessons for future implementers

### 8.1 Read these first, in this order

1. `AGENTS.md` and `CLAUDE.md` — especially "NEVER WORK WITHOUT LOTUS" and the
   git identity rule.
2. `docs/status.md` — the evidence chain. Read the latest sections, not just
   the milestone table; the table lags.
3. `UNOWA.md` — scope, milestones and the performance contract.
4. `docs/UNOWA-2.md` — the 10-issue correctness/maintainability plan.
5. `docs/reuse-audit.md` — what to import from kohjoh/sinjoh and what not to.
6. `docs/eidos-review.md` — what Eidos solved and what not to copy.
7. `docs/frame25-crash.md` — the model for a real root-cause investigation.
8. This document, then the transcripts only when you need the detailed story.

### 8.2 The debugging playbook

1. **Start LOTUS and confirm you can drive it.** If you cannot, stop and tell
   the user. Do not write a harness. Do not guess.
2. **Build and record the exact commit and binary hash.** `git status`,
   `git rev-parse`, `.3dsx` SHA-256.
3. **Re-resolve symbols after every link.** `arm-none-eabi-nm
   build/game/3ds/m1-test.elf`. Never reuse an address from a note.
4. **Reproduce the stop, then preserve the context.** Pause immediately; do not
   run past the exception. Read the access record (`type`, `address`, `width`),
   registers, stack and `citrace`.
5. **Symbolize every frame.** `addr2line -e build/game/3ds/m1-test.elf`. If the
   PC is a return address, scan the stack for words in `.text`.
6. **Ask which address route should have handled it.** Named symbol, I/O macro,
   or literal/macro? If none, the fix is a general rule.
7. **Check the generated artefact.** Inspect `build/game/3ds/gen/flat/`
   (`flat_image.c`, `flat_names.ld`), patched sources, and the relocation
   table. A `symbol + N` addend is a fallback, not an exact function.
8. **Measure the rule before shipping it.** Count matches, inspect a sample,
   and require exact entry/start matches. A rule with hundreds of hits is
   probably wrong.
9. **Verify twice.** Static (`nm`, `objdump`, `readelf`, generated C) and
   dynamic (LOTUS counters, captures, guest memory).
10. **Record the evidence and the negative results.** Commit after the link and
    the run, not after a perfect boot.

### 8.3 Change discipline

- Fix classes, not sites. Audit the tree, write one rule, and document the
  remaining exposure.
- Never edit the pinned Aletheia/sinjoh/kohjoh checkouts. Patch build-tree
  copies; keep the original bytes and provenance.
- Prefer sinjoh's proven components over new implementations. If you must
  adapt, keep the diff small and cite the original line.
- Keep one frame loop, one graphics state, one audio pipeline, one save
  backend. Do not add a second scheduler or a software renderer.
- Label deliberate stubs and approximations in the code and in `status.md`.
  Clear-only, live-layer VRAM display, silent audio and wireless stubs are not
  finished features.
- Do not claim a milestone without the exit criterion. "Compiled" is not
  "boots"; "boots" is not "renders"; "renders" is not "hardware-verified".

### 8.4 Build hygiene

- Use the Edit tool for multi-line `game.mk`/source edits. Python heredocs have
  corrupted files repeatedly (`\n`, `\b`, backslashes).
- Make generators and source lists prerequisites. If a change "did nothing",
  check the dependency graph.
- Unlink archives before rebuilding; `ar rcs` does not remove dropped members.
- Use order-only prerequisites for directories created in recipes.
- Keep release and profile object trees separate. Never run two builds into one
  tree; the only symptom is measurement fiction.
- `-fno-builtin` is required for the decompiled MSL runtime.
- `make -k` with a log and `$LASTEXITCODE` beats one-error-at-a-time builds.
- A full rebuild after a patcher/global-flag change takes minutes; batch
  related fixes.

### 8.5 What not to waste time on

- Do not try to build on Eidos's host engine or its identity-mapped
  `0x02000000` model. It cannot work on 3DS.
- Do not try to compile the game with mwccarm. The ABI does not match sinjoh's
  ARM11 libraries. mwccarm is for Aletheia's data tools only.
- Do not scan DSi BIOS/firmware/NAND dumps for the modcrypt key. It is in AES
  hardware.
- Do not use Eidos as a visual oracle. Run the real ROM in melonDS.
- Do not assume the title is 2D, or that a black screen is a missing capture.
  Read `DISPCNT`, `POWCNT1`, `DISPCAPCNT` and the VRAM bank state at the render
  point.
- Do not patch one address at a time when the same address appears in many
  files. Audit the class.
- Do not trust a single PC sample, a cached symbol address, or a plausible
  narrative. Trust the access record, the bytes, and the counters.
- Do not use GDB to drive the guest or LOTUS write watchpoints for a one-frame
  window. Use execute breakpoints, conditional breakpoints and memory reads.
- Do not leave verified work uncommitted while waiting for a perfect state.

### 8.6 Handoff checklist

- [ ] Commit the field-map work (9 modified files) with its LOTUS evidence.
- [ ] Update the milestone table in `status.md` to match the detailed sections.
- [ ] Record the next field stop, or state that it is uncharacterized.
- [ ] Record the exact build command, expected artifact size and counter values.
- [ ] Note the White edition status and the pack-digest limitation.
- [ ] Note the machine-local dependencies in section 10 below.
- [ ] Keep temporary probes out of the commit, or label them clearly.
- [ ] Refresh `README.md` so it describes the game build, not only M0.

---

## 9. How to build unowa

### 9.1 Expected layout

`engine/game.mk` defaults to sibling checkouts:

```text
workdir/
  nintendo-homebrew/
    unowa/                 this repository
    aletheia/aletheia/     pinned 50731dd, ROMs + extract + layout
    sinjoh/                pinned 1774be6, bootstrapped
    kohjoh/                reference only
    eidos/                 reference only
    coffeepot/             hardware/engineering wiki
    mwccarm/dsi/1.1p1/     only for Aletheia's data tools
    melonDS.exe            optional visual oracle
    dsi/, DSi2/            DSi dumps (not useful for the modcrypt key)
```

Override `ALETHEIA_DIR` or `SINJOH_DIR` on the make command line if your
layout differs.

### 9.2 Toolchain

- devkitPro with devkitARM r68-1 / GCC 16.1.0, libctru 2.7.1, citro3d 1.7.1,
  3dstools 1.3.1, picasso, tex3ds. The pins are in `upstream.lock`.
- Python 3. `make` invokes `python3`; Aletheia's tools need a native Windows
  Python, not the MSYS build.
- Git for Windows on `PATH` ahead of MSYS2's git.
- PowerShell for `tools/build.ps1`; the devkitPro MSYS2 shell for make.

### 9.3 Bootstrap sinjoh

A plain sinjoh clone is not enough. From `sinjoh/`:

```sh
python tools/game/bootstrap.py      # pinned decomp + SDKs, apply patches
pwsh tools/build.ps1                # or -All for release + profile
```

This produces `sinjoh/build/NitroSDK`, `sinjoh/build/NitroSystem`,
`sinjoh/engine/compat/nitro/build/3ds/libnitro.a` and
`sinjoh/engine/compat/nitrosystem/build/3ds/libnitrosystem.a`. `engine/game.mk`
checks for `sinjoh/build/NitroSDK/include/nitro.h` and fails early if it is
missing.

### 9.4 Prepare Aletheia

Place your own verified `black.nds` and `white.nds` at the Aletheia root, then:

```sh
cd aletheia/aletheia
python tools/ndsrom.py extract black.nds -o extract
python tools/delink.py -e extract -l build/black/layout
```

This creates `extract/arm9_dec.bin`, `extract/data/`, `extract/overlays*`,
`extract/overlays_dec/` and `build/black/layout/layout.json`. The ROM hashes
are checked by unowa's `verify-rom` against Aletheia's documented No-Intro
SHA-1s:

- Black `26ad0b9967aa279c4a266ee69f52b9b2332399a5`
- White `bc696a0dfb448c7b3a8a206f0f8214411a039208`

For the authoritative data-companion route (not yet adopted), also make the
plain link available. Run these from the unowa root; `tools/aletheia_run.py`
takes the Aletheia root as its first argument:

```sh
python tools/aletheia_run.py ../aletheia/aletheia gamelink arm9_main
python tools/aletheia_run.py ../aletheia/aletheia arm9data
```

`tools/aletheia_run.py` removes the TWL-only `twl_0272EB9C` spec in-process.
It requires mwccarm at `aletheia/tools/mwccarm/mwccarm` (a junction to
`../mwccarm/dsi/1.1p1` works) and a native Windows Python.

### 9.5 Build the M0 shell (optional smoke test)

From the unowa root:

```sh
pwsh tools/build.ps1 -AllProfiles
```

or, from devkitPro MSYS2:

```sh
sh tools/build.sh 0 0    # Black release
sh tools/build.sh 0 1    # Black instrumented
sh tools/build.sh 1 0    # White release
sh tools/build.sh 1 1    # White instrumented
```

Outputs are under `build/<edition>/3ds[-prof]/`. This builds the clear-screen
bootstrap, not the game. The README currently documents only this path; do not
mistake it for the game build.

### 9.6 Build the game

From devkitPro MSYS2, in the unowa root:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
make -f engine/game.mk verify-pins verify-rom
make -f engine/game.mk link-final -j8
```

The output is `build/game/3ds/m1-test.3dsx` (about 5.3 MiB) and
`build/game/3ds/m1-test.elf` (about 52 MiB). `TARGET=3ds` is accepted in older
notes but is not used by the current makefile.

A clean rebuild is `rm -rf build/game/3ds && make -f engine/game.mk link-final
-j8`. Full builds take roughly 5–6 minutes here; relinks are about 20 seconds.
Changing `tools/patch_sources.py` re-patches every matched TU; changing
`tools/gen_flat_image.py` regenerates the flat image and relinks. Batch related
fixes before rebuilding.

The build uses:
- `build/game/3ds/gen/port_sdk_ioreg.h` (generated I/O redirect),
- `build/game/3ds/src/` (patched source copies),
- `build/game/3ds/gv_src/` (patched Gen V sources),
- `build/game/3ds/gen/flat/` (flat image, linker names, pointer tables),
- `build/game/3ds/libunowa_game.a`, `libunowa_gv.a`, `libunowa_game_ren.a`.

### 9.7 Build the content pack

The game expects `sdmc:/3ds/unowa/data/black.pak` and
`black.manifest.json`. For a LOTUS card, the SD root is
`~/lotus-data/citnlo/sessions/cards/unowa/sdmc`.

```sh
python tools/pack_game_data.py \
  --aletheia ../aletheia/aletheia \
  --rom ../aletheia/aletheia/black.nds \
  --edition black \
  --out <sd-root>/3ds/unowa/data/black.pak \
  --manifest <sd-root>/3ds/unowa/data/black.manifest.json
```

The Black pack is 247 files, 198,011,852 bytes, build id `unowa-nitrofs-1`.
The manifest's `rom_sha1` and the pack header currently hold a borrowed
Platinum digest (`0862ec35...`) because sinjoh's `pak_open` accepts only three
Platinum hashes. The file bytes and IDs are the user's Black NitroFS files.
Do not present this as a real B/W ROM hash. White has no pack yet.

### 9.8 Run in LOTUS

1. Register the asset in `~/lotus-data/citnlo/host.json`:

   ```json
   "unowa-m1": "C:\\...\\unowa\\build\\game\\3ds\\m1-test.3dsx"
   ```

   This is host-side; the MCP cannot register assets. Ask the user to reload
   the MCP after editing.
2. Create a **secondary** session with `card: "unowa"` and asset `unowa-m1`.
   A bare secondary has an empty SD and pack open fails at frame 3. A fresh
   secondary copied from the card reproduces the boot deterministically.
   Avoid reloading an asset into a live primary; it can break pack open.
3. `lotus_run_until` accepts 1–600 frames per call. Run multiple calls.
4. Read counters with `arm-none-eabi-nm` after every link. Useful symbols:
   `unowa_mainloop_count`, `unowa_pacing_vblanks`, `unowa_present_count`,
   `unowa_overlay_log_count`, `unowa_unimplemented_log_count`,
   `unowa_g3_frames`, `unowa_g3_verts`, `unowa_bottom_draws`,
   `unowa_direct_norun`, `unowa_pack_result`.
5. Capture with `lotus_capture_image`. Input uses the measured button order:
   A=0x001, B=0x002, X=0x004, Y=0x008, Up=0x010, Down=0x020, Left=0x040,
   Right=0x080, L=0x100, R=0x200, Start=0x400, Select=0x800. This is not
   libctru's key order.
6. Touch: `lotus_input` coordinates are 3DS bottom-screen 320×240; the port
   scales by 4/5 to DS 256×192, so a DS-space tap must be multiplied by 5/4.
7. GDB: `lotus_gdb start`, then `arm-none-eabi-gdb` with
   `file build/game/3ds/m1-test.elf`, `target remote 127.0.0.1:<port>`,
   `thread apply all bt`. It reads a parked view; it does not drive the CPU.
   LOTUS execute breakpoints are cheap; write watchpoints are impractically
   slow.
8. Real-ROM oracle: run `black.nds` in melonDS (`../melonDS.exe`), capture the
   same scene. Eidos is not a visual oracle. The title's white/silver Reshiram
   over a checkerboard is the game's art, not a port bug.

### 9.9 Expected numbers

For a healthy boot at the current stage:

- `mainloop_count` and `pacing_vblanks` equal, or within a few frames, over
  1200 host frames (~60 Hz).
- `present_count` about half the V-blank count (~30 Hz).
- `overlay_log_count` grows from 15 to 40+ as scenes load.
- `pack_result == 0`.
- `unimplemented_log_count` small; NVRAM is handled.
- `g3_frames` and `g3_verts` nonzero once the title/field 3D starts.

If `mainloop_count` is zero while V-blanks advance, the game thread is not
waking; check the DTCM/IRQ-table aliases. If `present_count` is zero, check
whether the game reached `NitroPort_OnFrame`; the idle thread can drive
V-blanks before the game thread reaches its loop.

---

## 10. Machine-local dependencies and portability risks

This is the answer to "what is only on this machine?" Everything below is
required to reproduce the current state and is not in the repository.

| Dependency | Where it lives | Why it matters | Risk / mitigation |
|---|---|---|---|
| **Uncommitted field-map work** | 9 modified files in the working tree (`docs/status.md`, `engine/game.mk`, `flat_memory.*`, `gen_flat_image.py`, `patch_sources.py`, plus the user's `AGENTS.md`/`CLAUDE.md`/`UNOWA-2.md`) | The field map only runs with these changes. `HEAD` (`cdbb933`) still stops at ov021/VRAM entry. | **Highest risk.** Commit the field work before handoff. Do not `git checkout` the tree. |
| **Aletheia checkout + ROMs + extract** | `../aletheia/aletheia` at `50731dd`; `black.nds`, `white.nds`, `extract/`, `build/black/layout/`, `build/gen/*.inc`, `build/gamelink/arm9_main/ov.elf` | `gen_flat_image.py` reads the extract and layout; `verify-rom` checks the ROMs; the data-companion path reads the plain link. | Not in git, ROM-derived. A fresh checkout cannot build the game without the user's own dump and extraction. |
| **Bootstrapped sinjoh** | `../sinjoh` at `1774be6`; `build/NitroSDK`, `build/NitroSystem`, `engine/compat/nitro/build/3ds/libnitro.a`, `engine/compat/nitrosystem/build/3ds/libnitrosystem.a` | `engine/game.mk` links these libraries and checks `build/NitroSDK/include/nitro.h`. | Large generated tree, not in unowa's git. A plain sinjoh clone is insufficient; run its bootstrap/build first. |
| **Content pack** | `~/lotus-data/citnlo/sessions/cards/unowa/sdmc/3ds/unowa/data/black.pak` + manifest | The game's FS reads all content from the pack; without it, boot stops at frame 3. | 198 MB, ROM-derived, not in git. Rebuild from the user's Black ROM. White pack does not exist. |
| **LOTUS host config** | `~/lotus-data/citnlo/host.json` (asset `unowa-m1`, worker path `LOTUS-v0.1.2-windows-x64`) | The MCP only accepts registered handles. | Absolute paths; version drift (`upstream.lock` still says v0.0.2). A new machine needs its own host config and a manual MCP reload. |
| **mwccarm + junction** | `../mwccarm/dsi/1.1p1`, junctioned at `aletheia/tools/mwccarm/mwccarm` | Required only for Aletheia's data-companion tools, not for the normal unowa build. | Version must be CodeWarrior 4.0 build 1024 (`dsi/1.1p1`). A junction is not portable; recreate it or pass the path. |
| **Native Windows Python** | `C:\Program Files\Python315\python.exe` (used by Aletheia tools) | Aletheia's tools detect MSYS Python as Cygwin and look for wine. | Document the interpreter; do not assume `python3` works for both build and Aletheia tools. |
| **devkitPro path** | `C:\devkitPro` (build.ps1 hardcodes `msys2\usr\bin\bash.exe`); make defaults to `/opt/devkitpro` | The wrapper and make must agree. | A non-standard install path needs edits to `tools/build.ps1` or environment overrides. |
| **melonDS oracle** | `../melonDS.exe` | The only trusted visual oracle for the real game. | Not pinned in `upstream.lock`; may be a custom/prebuilt binary. Record its version and hash. |
| **Style guide** | `CLAUDE.md` points to `~/workdir/style-guide/README.md` | It does not exist on this machine. | Contributors fall back to coffeepot's C style guide. Fix the path or vendor the guide. |
| **Stale artefacts** | `unowa.zip` (M0 snapshot), `dbg5.gdb` (old watchpoint), `docs/transcripts/` (untracked) | `unowa.zip` contains an obsolete `gen_data_image.py` and old sources; `dbg5.gdb` points at an old address/port. | Do not treat them as current source. Either commit the transcripts deliberately or leave them untracked with a README note. |
| **No physical Old 3DS** | — | All performance and rendering evidence is emulator-only. | The 30 FPS target, audio cost, memory headroom and thermal behavior remain unverified. M7/M9 cannot be signed off here. |
| **No automated tests** | — | Generators, crypto, relocation rules and stubs are verified by manual LOTUS runs. | Add host-side tests for flat-image rules, SHA-1/HMAC vectors, pack format and relocation; add a recorded-input LOTUS harness. |

### The top five things that would break a fresh contributor

1. Checking out `HEAD` without the uncommitted field-map work: the field
   regression returns.
2. Cloning sinjoh but not bootstrapping it: the game link fails for missing
   `libnitro.a`/`libnitrosystem.a`.
3. Not having the Aletheia extract: `gen_flat_image.py` cannot build the flat
   image, and `verify-rom` fails.
4. Not having the content pack on the LOTUS card: boot stops at frame 3 with
   `pack_result != 0`.
5. Using the README's `build.ps1` as "the build": it builds the M0 shell, not
   the game. The game is `make -f engine/game.mk link-final`.

---

## 11. Current tree and next moves

### 11.1 Current state

- `HEAD` is `cdbb933` ("Clock the ARM7 sound driver: the title exits to the
  start menu"), 25 commits, all authored by `terezi <jules@foxtails.me>`.
- The working tree has 9 modified files and 4 untracked paths:
  - modified: `AGENTS.md`, `CLAUDE.md`, `docs/UNOWA-2.md`, `docs/status.md`,
    `engine/game.mk`, `engine/runtime/flat_memory.c`,
    `engine/runtime/flat_memory.h`, `tools/gen_flat_image.py`,
    `tools/patch_sources.py`
  - untracked: `dbg5.gdb`, `docs/transcripts/`, `engine/runtime/ds_memory_ops.c`,
    `unowa.zip`
- `build/game/3ds/m1-test.3dsx` was built at 20:09 and includes the field-map
  fixes. It is 5,305,564 bytes.
- The field map runs: LOTUS shows Nuvema Town in 3D at frame 13,480,
  `mainloop 13,021`, `g3_frames 5,549`, 42 overlay loads.
- The next field stop is not characterized in the committed docs. The last
  transcript ends with the field run and the third fix built but not replayed.

### 11.2 Immediate next moves, in order

1. **Commit the field-map work** in logical commits (overlay static
   initialisers; DS memory wrappers; local address macros; status update).
   Include the LOTUS evidence and the exact binary hash. Do not commit ROMs,
   packs, saves or captures. Leave the user's `AGENTS.md`, `CLAUDE.md` and
   `docs/UNOWA-2.md` changes alone unless the user asks for them to be
   committed.
2. **Characterize and fix the next field stop.** Reproduce from a fresh
   secondary session, preserve the fault context, symbolize the stack, and
   classify the address route. The field is the first real 3D world; expect
   map model loading, camera, weather, billboards and script transitions to
   expose new classes.
3. **Make the field interactive.** Walking, collision, camera, map transitions,
   and the save/continue flow. That is the M5 gate.
4. **Verify audio actually mixes.** LOTUS reports `audio_samples 0`; determine
   whether the silent fallback thread is doing the work, and measure its cost.
   The title-transition result proves the clock, not the output.
5. **Implement `DISPCAPCNT` honestly.** Render the main graphics screen to a
   256×192 target, keep the previous capture, blend EVA/EVB, display the bank.
   That closes the last known M4 correctness gap and removes the live-layer
   approximation.
6. **Model VRAM bank remapping.** The flat `NitroPort_BgVram` view works for
   HGSS but does not translate Gen V's `VRAMCNT_A=0x82`, `B=0x81`, `C=0x89`,
   `E=0x84`, `I=0x81` assignments. Title/capture scenes need this.
7. **Build White.** Separate extract/pack/build/test. The edition identity and
   path roots exist; the game build is Black-only today.
8. **Replace the heuristic data path** with Aletheia's typed companions once
   the field work stabilizes. This is a large, high-value refactor; do it
   behind a build switch, compare generated data against the current image,
   and keep the boot bisectable.
9. **Add regression coverage.** Host tests for flat relocation rules, SHA-1/
   HMAC vectors, pack format and `MATH_QSort` ordering; a LOTUS harness with
   recorded inputs and expected counters/captures.
10. **Start M6/M7 only after M5 is playable.** Widescreen and optimization
    depend on a stable scene corpus and real hardware; doing them earlier will
    encode the wrong assumptions.

### 11.3 Open questions to resolve

- What is the next field stop after Nuvema Town? (Unknown; reproduce and
  symbolize.)
- Is the intro/menu vertical striping the game's film-strip motif or a port
  bug? (Needs a matching melonDS frame.)
- Is the title phase-2 shading correct, or is the white scene a TEV/texture
  gap? (Phase 1 matches the oracle; phase 2 needs the capture blend and a
  same-frame comparison.)
- Does audio actually produce samples, and what does the mixer cost?
- Does the save flow work end to end (write, cold boot, continue), not just
  the boot-time flash read?
- Does White boot with the same code and its own data?
- Can the Aletheia data companions replace the heuristic image without
  regressing the boot?
- What is the real memory headroom on O3DS at the busiest scene?

---

## Appendix A — Commit map

```text
ffbb45b  Initial unowa implementation: M0 bootstrap, M1 link closure, M2 native boot
5929130  Wire clear-frame presentation into M2 harness
194f18c  Handle Gen V NVRAM ARM7 service at boot
c70fb15  Relocate DS data pointers and open the content pack
a9584ea  Relocate ROM function pointers and fix the boot path before M2
d022706  Bound data objects by their owning module and stop guessing BSS sizes
e25f128  Make aliased data names share one object
b884501  Record what the frame-16 read actually is
f076f4b  Own the DS save device, overlay save pointers and the shared work area
8bcf10f  Review Eidos's Gen V solutions against the 3DS constraints
1642c8f  Keep DS data in a flat main-RAM image and boot past frame 25
34ae75a  Document the flat data model, the frame-25 crash and boot state
1151656  Name the commit the frame-25 repro builds from
4a0301f  Pace the game tick to 60 Hz and decouple 30 Hz presentation
ddf3aa7  Keep presentation in NitroPort_OnFrame; make the clear steady
5966e39  Boot the game's own NitroMain loop: fix DTCM aliases and IRQ table
f0c873e  Render the DS 2D engine on the PICA
6c955a2  Render direct-colour BGs and the intro movie; wire the 3D backend
8905cd5  status: M2 input is exercised, M3 covers direct-colour bitmaps
6e778a4  Accept 7-digit DS main-RAM literals; the title screen renders
9a069a9  Publish the title's 3D scene: route the geometry swap register
9c6489e  status: the title's picture is the 3D scene itself
b7b3300  status: START reaches demo state 4; the menu waits on the voice seam
8a5241d  Draw VRAM-display screens: the title's 3D on the lower screen
cdbb933  Clock the ARM7 sound driver: the title exits to the start menu
```

## Appendix B — Transcript map

The cleaned transcripts used for this handoff are copies; the originals are in
`docs/transcripts/`. Several are duplicate exports of the same session.

| Transcript | What it covers |
|---|---|
| `2026-11-22-143326-...txt` | Claude Code orientation; Aletheia data companions; no LOTUS. |
| `2026-11-22-143411-...txt` | Aletheia plain `arm9_main` link passes; `tools/aletheia_run.py`; native Python requirement; no LOTUS. |
| `session-35027660-...json` | First OpenCode M0 bootstrap; `C3D_FrameDrawOn` black-screen fix; Eidos distrust; the custom MCP harness incident; LOTUS bridge disconnect. |
| `session-17818731-...md` / `session-b3292571-...json` | M1 link closure (247→30 undefined); mwccarm ABI; Gen V rename; port stubs; first M2 faults; overlays; first clean 1200-frame boot. Duplicate exports. |
| `session-f675161b-...md` | M2 observability; clear-frame presentation; NVRAM handler; content pack; `_ArcPathTable` discovery. |
| `session-2cb14e8c-...md` / `session-6ec533a4-...json` | Retries of the f675 continuation; FS/Gen V FS issues. |
| `session-b0d2c0e3-...md` / `session-a17aa56d-...json` | DS data/function pointer relocation; alias fix; save bridge; V-blank literals. Duplicate exports. |
| `unowa-03.txt` | Flat data model; frame-25 crash; boot firmware; wireless stubs; frame-pacing diagnosis; Eidos review. |
| `session-7cf9a8e2-...json` | Frame bridge; DTCM/IRQ boot fix; M3 g2d; direct-colour BGs; title 3D; VRAM_D lower screen; audio clock; new game; field map. Latest state. |
| `session-b12e98ef-...json` | Early M2 retry ending at `_ArcPathTable`; same arc as f675. |

## Appendix C — Key files

- `UNOWA.md` — scope, milestones, engineering rules.
- `docs/status.md` — the evidence chain (read the latest sections).
- `docs/UNOWA-2.md` — the 10-issue correctness/maintainability plan.
- `docs/reuse-audit.md` — kohjoh/sinjoh reuse decisions.
- `docs/eidos-review.md` — Eidos findings and the flat-model recommendation.
- `docs/frame25-crash.md` — the full crash investigation.
- `engine/game.mk` — the real game build.
- `engine/runtime/flat_memory.c`, `flat_memory.h` — flat image load, overlay hooks, arena.
- `engine/runtime/ds_memory_ops.c` — DS pointer translation for block clears/copies.
- `engine/runtime/frame_pacing.c` — 60 Hz tick / 30 Hz present.
- `engine/runtime/unowa_frame.c` — `kj_frame_input`, VRAMCNT decode, HBlank shadow.
- `engine/graphics/g2d/g2d.c` — DS 2D engine on PICA.
- `engine/graphics/g3d/g3d.c` — 3D backend.
- `engine/audio/audio.c` — NDSP/silent-clock audio layer.
- `engine/stubs/port_stubs.c` — fixed addresses, MSL/BIOS replacements, NVRAM, input, logs.
- `engine/stubs/card_backup_port.c` — 512 KiB save flash.
- `engine/stubs/wireless_stubs.c` — explicitly unsupported wireless/DSi DSP.
- `tools/gen_flat_image.py` — flat image and pointer tables.
- `tools/patch_sources.py` — build-tree source rewrites.
- `tools/rename_genv.py` — Gen V/Gen IV symbol collision handling.
- `tools/gen_port_ioreg_header.py` — DS I/O macro redirect.
- `tools/pack_game_data.py` — PLATPAK v1 content pack.
- `tools/aletheia_run.py` — run Aletheia tools while skipping the TWL-only spec.
- `tools/audit_ds_literals.py` — remaining DS literal exposure.
