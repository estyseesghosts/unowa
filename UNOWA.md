# Pokémon Black & White — Native Old 3DS Port
## Agent Engineering Implementation Guide

**Target:** Original Nintendo 3DS / 2DS, stock clocks, 64 MiB application memory, native ARM11 executable, PICA200 rendering, WideArt, Pokémon Black and White.

**Baseline repositories, pinned 2026-11-22:**

| Repository | Revision | Responsibility |
|---|---|---|
| [Aletheia](https://github.com/Project-Holon/aletheia/tree/50731dd) | `50731dd` | Complete Gen V game source |
| [Eidos](https://github.com/Project-Holon/eidos/tree/2319b45) | `2319b45` | Black/White compatibility and execution |
| [Kohjoh](https://github.com/estyseesghosts/kohjoh/tree/7a98ace) | `7a98ace` | Original 3DS GPU optimization reference |
| [Sinjoh](https://github.com/estyseesghosts/sinjoh/tree/1774be6) | `1774be6` | Primary runtime, renderer, WideArt |
| [Coffeepot](https://github.com/estyseesghosts/coffeepot/tree/209c8ed) | `209c8ed` | Hardware and engineering guidance |

These are the inspected revisions, not claims that every existing feature is hardware-verified. Recheck upstream changes before importing fixes.

---

# 1. Objective and scope

Produce two native homebrew applications, one for **Pokémon Black** and one for **Pokémon White**.

Both compile the original game logic for the 3DS's ARM11. DS graphics hardware is represented by compatibility code and rendered through the PICA200; this is not NDS emulation.

### Required experience

- **30 FPS presentation** on stock Old 3DS, with correct original game timing.
- Top display at **400×240**, bottom at **320×240**.
- Widened 3D world view, with corrected projection and culling.
- GPU-native 2D backgrounds, sprites, text, windows and effects.
- WideArt for suitable 2D screens and scene elements.
- Original game mechanics, scripts, saves, audio and input.
- Support for both Black and White, including version-specific content.
- `.3dsx` builds and optional HOME Menu CIA forwarders.
- ROM-derived content prepared locally from the user's own verified dump.
- Release and instrumented builds, independently reproducible.

The performance contract is the original game's actual simulation cadence, with a maximum target of 30 presented pictures per second. Do not assume that Black/White has the same number of game updates per VBlank as HeartGold/SoulSilver or Platinum. Measure and reproduce its timing first.

### Initial exclusions

Do not implement stereo 3D, 60 FPS interpolation, software-rendering fallback, desktop-style graphics effects, xBRZ, live WideArt generation, or New 3DS-only optimizations.

Online services, C-Gear communications and multiplayer need separate compatibility decisions. They must not block offline single-player boot and normal gameplay. Native local wireless is a future research milestone, not part of initial bring-up.

Do not make a self-contained ROM-containing CIA. Use the proven SD content-pack and forwarder arrangement initially.

---

# 2. How much existing work carries over?

This is principally an integration project, not a new graphics engine project.

The percentages below are **engineering planning estimates, not measured code-diff statistics**. They describe likely reuse of implementations and algorithms, not the proportion of time automatically saved.

| Component | Reuse potential | Primary source | Work required |
|---|---|---|---|
| 3DS boot, APT, input, frame submission | Very high | Sinjoh | Adapt app IDs, paths and game entry |
| DS 2D hardware compositor | Very high | Sinjoh | Extend unsupported modes and validate Gen V effects |
| GPU tile atlas, texture cache | Very high | Sinjoh/Kohjoh | Retain, profile new workload |
| DS geometry FIFO and matrix interpreter | High | Kohjoh/Sinjoh | Connect Gen V hardware writes and NNS usage |
| PICA200 geometry backend | Very high | Sinjoh | Correct Gen V materials, cameras and effects |
| Retained meshes, culling, producer skipping | High | Kohjoh/Sinjoh | New producer hooks and safety validation |
| Audio output and scheduling | High | Sinjoh | Integrate Gen V-specific sound commands/driver |
| WideArt packing and GPU display | Very high | Sinjoh | New keys, art and layout rules |
| Game source and original mechanics | Complete source exists | Aletheia | Cross-compilation and host integration |
| Overlay handling and fixed-address data | High reference value | Eidos | Port/adapt to ARM11 and constrained memory |
| NitroSDK compatibility | Medium | Eidos/Sinjoh | Merge Gen V requirements into existing bridge |
| Asset extraction and archive parsing | High | Aletheia/Eidos | New indexed pack and packaging integration |
| Save storage infrastructure | High | Sinjoh | New Gen V save protocol and validation |
| Battle/field/menu layout integration | Low | Eidos/Sinjoh | New scene rules and WideArt composition |
| Test harness and profiling | Very high | Kohjoh/Sinjoh | New Gen V corpus, fixtures and acceptance gates |

**Working estimate:** around 70–85% of the hardware-facing renderer/platform implementation should be reusable or adaptable without architectural replacement. Game-specific integration, scene layouts, compatibility fixes and validation will still account for much of the engineering work.

The biggest mistake would be equating source-code reuse with a comparable reduction in testing effort.

Sinjoh should supply the initial project skeleton. Eidos should supply the Gen V-specific code that makes Aletheia runnable. Do not create a second independent implementation of either of those responsibilities.

**References:** [Sinjoh architecture](https://github.com/estyseesghosts/sinjoh/blob/1774be6/docs/ARCHITECTURE.md), [Kohjoh architecture](https://github.com/estyseesghosts/kohjoh/blob/7a98ace/docs/architecture.md), [Eidos architecture](https://github.com/Project-Holon/eidos/blob/2319b45/docs/architecture.md).

---

# 3. Runtime architecture

Preserve Sinjoh's separation of the original game's environment from the 3DS environment.

### Game side

Compiled with the original game's 32-bit type assumptions:

- Aletheia `src/game/`.
- Gen V NitroSDK-compatible services.
- Game heap, OS tick, VBlank, alarms and scheduler.
- ARM9 data-image bindings and overlay residency.
- Emulated DS register and memory state.
- DS geometry command interpreter.
- Resource access and the original game-owned main loop.

### Platform side

Compiled with libctru/citro3d conventions:

- 3DS entry point, APT and HID.
- Rendering and frame presentation.
- PICA200 texture/geometry resources.
- Audio output through NDSP.
- SD-card content and saves.
- Diagnostics, crash reporting and instrumentation.

### Boundary contract

Use Sinjoh's [`engine/runtime/kj_bridge.h`](https://github.com/estyseesghosts/sinjoh/blob/1774be6/engine/runtime/kj_bridge.h).

Retain its design:

- Fixed-width integers only.
- Plain explicitly sized structs.
- No enums across the boundary.
- No NitroSDK or libctru headers in the common interface.
- Game functions under `kj_ds_*`.
- Platform functions under `kj_ctr_*`.

Introduce Gen V-specific adapters rather than another independent port API.

The flow should be:

**Aletheia game → Eidos-derived Gen V SDK/data compatibility → `kj_*` bridge → Sinjoh renderer/audio/storage → 3DS hardware.**

Do not run Eidos's desktop host and Sinjoh's runtime simultaneously. They solve overlapping problems. Keep exactly one authoritative frame scheduler, one graphics state, one audio pipeline and one save backend.

### Critical ABI rules

The DS ARM9 and 3DS ARM11 both use 32-bit pointers, which is a major advantage over Eidos's 64-bit host configurations. Nevertheless:

- Assert `sizeof(void *) == 4`.
- Match the original game's enum sizes explicitly.
- Assert layouts for ABI-sensitive structs.
- Audit function pointer casts and Thumb entry bits.
- Replace original ARM9-specific assembly with verified portable implementations.
- Check compiler-dependent integer promotion, packing, signed shifts and alignment.
- Keep every compiler-specific fix isolated from the upstream matching source.

The game's DS-visible addresses must not become literal 3DS memory accesses. Use the address-rewriting and data-binding work Eidos already provides.

**Read:** [Eidos pointer-width analysis](https://github.com/Project-Holon/eidos/blob/2319b45/docs/pointer-width.md), [game-tree integration](https://github.com/Project-Holon/eidos/blob/2319b45/docs/game-tree.md), [DS memory model](https://github.com/Project-Holon/eidos/blob/2319b45/sdk/dsmem.c).

---

# 4. Aletheia game-code integration

Aletheia's README reports a byte-identical match of its supported game-code regions, including 237 overlays. Its `src/game/` contains the reconstructed translation units. That eliminates the need to decompile the game again.

It does **not** mean those functions can all be compiled directly by devkitARM without platform work.

Eidos has already addressed substantial parts of that problem. Import its solutions selectively.

## 4.1 Source selection

Use Aletheia's `src/game/`, not the older individually matched function layout.

Reuse Eidos's:

- `tools/gametree.py` — source inventory.
- `tools/gamecut.py` — omit/replace functions requiring platform implementations.
- `tools/dsaddr.py` — DS fixed-address access transformation.
- `tools/armdata.py` — static data-image generation.
- `tools/bootlink.py` — boot linkage.
- `tools/ovlink.py` — overlay linkage and symbol namespaces.

Make the output configurable for the devkitARM compiler and the Sinjoh build.

Prefer generated wrapper code and forced includes to editing Aletheia source files.

## 4.2 Overlay model

Black/White uses 237 overlays. Many occupy overlapping DS memory windows, with identical addresses representing different code depending on the resident overlay.

Eidos solves this by:

1. Renaming symbols into per-overlay namespaces.
2. Compiling the overlay code into native executable code.
3. Keeping original DS overlay data in its expected logical address space.
4. Copying, binding and relocating overlay data on load.
5. Dispatching cross-overlay calls through controlled entry points.
6. Tracking residency so an unloaded overlay cannot continue being called as though it were present.

Adapt that model rather than implementing executable overlays by copying original ARM9 machine code into 3DS memory.

The 3DS executes recompiled ARM11 code, not the original ARM9 bytes.

Keep overlay data residency and initialization faithful even when code is permanently linked into the executable. Code availability does not imply the corresponding game-data state is active.

A full 237-overlay native link must also be evaluated for binary size, RAM footprint, relocation overhead and cold-boot cost. If necessary, reduce the initial linked closure to the boot and first gameplay paths, then expand it deterministically.

**Reference:** [Eidos overlay design](https://github.com/Project-Holon/eidos/blob/2319b45/docs/overlays.md), [`overlay_image.c`](https://github.com/Project-Holon/eidos/blob/2319b45/sdk/overlay_image.c).

## 4.3 Fixed-address state

Eidos's DS memory representation is particularly valuable.

Retain its distinction between:

- DS main RAM and static game data.
- I/O registers.
- VRAM banks and their current mappings.
- Palette/OAM memory.
- Overlay data.
- ARM7-shared state.
- Addresses requiring read/write hooks instead of ordinary storage.

For performance, avoid passing every elementary register operation through an expensive generic dispatch if the call-site pattern allows a verified cheaper path.

But implement the correct semantics first.

The geometry FIFO, display registers, DMA writes and memory-backed state must all resolve into **one coherent DS hardware model** consumed by Sinjoh.

### Milestone acceptance

- Native ARM11 link containing Aletheia's boot path.
- Program starts its own `NitroMain`.
- First meaningful OS/VBlank activity executes.
- Overlay loads and initializers are logged.
- Unsupported service calls fail explicitly.
- Host32 reference and 3DS build agree on early game-state checkpoints.

No rendering is required to pass this milestone.

---

# 5. Main loop and frame scheduling

Reuse Sinjoh's [`kj_frame.c`](https://github.com/estyseesghosts/sinjoh/blob/1774be6/engine/runtime/kj_frame.c), [`renderer.c`](https://github.com/estyseesghosts/sinjoh/blob/1774be6/engine/graphics/renderer.c) and main-thread architecture.

Do not add a second game-driving loop.

The game owns its frame progression; the platform services real-time I/O and display work when the game reaches the required synchronization points.

### Required sequence

1. Execute the game's pending work and scheduled tasks.
2. Preserve original VBlank callback and interrupt ordering.
3. Commit DS shadow state and process relevant DMA.
4. Capture a coherent graphics snapshot after the required game-side writes.
5. Hand the completed snapshot to the PICA200 backend.
6. Present according to the chosen 30 Hz cadence.
7. Continue running game updates according to their original timing.

**The first task is to measure Black/White's actual synchronization behavior.**

Sinjoh's inherited code comments and documentation describe different contexts for DS VBlank and game-picture cadence. Do not transplant assumptions about Platinum's loop.

Use Eidos's frame tests and original gameplay recordings to determine which game-side updates must execute every VBlank.

Skipping an expensive *presentation* must never mean skipping a required script update, animation state transition, timer, audio command or geometry side effect that gameplay reads.

### Producer-side frame skipping

Kohjoh's strongest optimization is avoiding unnecessary work before constructing a frame that cannot be displayed.

Reuse this cautiously.

Prove that skipped producers do not affect:

- Game-visible geometry tests.
- Matrix and material state.
- Resource lifetime and texture uploads.
- Readable GX status.
- Animated model state.
- Framebuffer capture.
- Commands consumed by subsequent frames.

Implement capture decisions before expensive scene traversal only after the corresponding producer is characterized.

**References:** [Kohjoh architecture](https://github.com/estyseesghosts/kohjoh/blob/7a98ace/docs/architecture.md), [Coffeepot frame pacing](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/PICA200/frame-pacing.md).

---

# 6. 2D rendering

The DS 2D hardware did not fundamentally change between Gen IV and Gen V. This is one of the most reusable parts of the port.

Start with Sinjoh's:

- `engine/graphics/g2d/g2d.c`
- `engine/graphics/resources/atlas.c`
- `engine/graphics/renderer.c`

The existing renderer uses an RGBA5551 tile atlas, native GPU composition and retained layer state. Preserve it.

### Required behavior

Validate the original DS semantics for:

- Text and affine backgrounds.
- Bitmap/extended backgrounds.
- 4bpp/8bpp tiles and extended palettes.
- OAM sprites, flips, priority and affine transforms.
- Window masks, including OBJ windows.
- Alpha-blend first/second target selection.
- Brightness increase/decrease and master brightness.
- HBlank/per-scanline state changes.
- BG/OBJ mosaic.
- VRAM bank mapping and remapping.
- Display capture and screens using captured VRAM.
- Display-engine-to-physical-screen assignment.

Several of these features are missing or only partially covered in Kohjoh/Sinjoh. Gen V will expose different combinations of them.

Do not declare a graphics mode implemented because a similar menu in Platinum renders.

### GPU composition

Keep the original DS palette/tile semantics, not a flattened 256×192 framebuffer as the normal renderer input.

Retain:

- Palette-expanded tile atlas.
- Dirty-content and palette validation.
- Retained layers.
- GPU-side priority and blending.
- Scissoring and line-span handling.
- No full-frame CPU pixel composition in the shipping renderer.

Use Eidos's independent 2D compositor as the correctness oracle, not as a second shipping implementation.

It already documents affine BG, OBJ, window and blend behavior and maintains pixel-level tests.

**References:** [Sinjoh G2D](https://github.com/estyseesghosts/sinjoh/blob/1774be6/engine/graphics/g2d/g2d.c), [Eidos 2D engine](https://github.com/Project-Holon/eidos/blob/2319b45/docs/engine-2d.md), [Coffeepot rendering-2d](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/PICA200/rendering-2d.md), [GBATEK](https://mgba-emu.github.io/gbatek/).

### Important regression

Sinjoh previously had a blending-pass ordering error that erased the underlying BG when a full-screen first target was active.

The fix was to draw the opaque pass before the blended pass so stencil state was preserved.

Port that fix with a regression test. Do not rediscover it by modifying the game's palettes.

[Sinjoh lessons](https://github.com/estyseesghosts/sinjoh/blob/1774be6/docs/LESSONS.md)

---

# 7. 3D rendering and Gen V-specific work

The PICA200 geometry backend is reusable, but this is the area with the greatest performance and visual-correctness risk.

Reuse:

- Sinjoh's `engine/runtime/kj_g3.c`.
- Sinjoh's `engine/graphics/g3d/g3d.c`.
- Geometry/display-list caches.
- Object-space retained batches.
- GPU vertex transforms.
- Per-draw matrix palettes.
- Stable-lit-list baking.
- Texture conversion/cache.
- Bounding-volume culling.
- Material-state filtering.

The DS command set and hardware matrix semantics are common across both generations. The work is connecting Gen V's submission paths and validating its use of them.

## 7.1 Geometry entry points

Eidos already identifies Gen V geometry FIFO and command-register behavior.

Feed those writes into the existing `kj_g3` interpreter.

Account for:

- Packed FIFO command words.
- Direct geometry command-register writes.
- DMA submissions.
- Matrix stack operations.
- Texture-coordinate generation.
- Lighting/material state.
- `BOX_TEST`, `POS_TEST`, `VEC_TEST`.
- Buffer swaps and depth behavior.
- NNS model/display-list submission.

Do not implement one path for direct commands and another incompatible path for FIFO commands. They must update the same interpreter state.

**References:** [Eidos `g3_fifo.c`](https://github.com/Project-Holon/eidos/blob/2319b45/sdk/g3_fifo.c), [Eidos 3D engine](https://github.com/Project-Holon/eidos/blob/2319b45/docs/engine-3d.md), [Kohjoh `kj_g3.c`](https://github.com/estyseesghosts/kohjoh/blob/7a98ace/src/runtime/kj_g3.c).

## 7.2 Gen V scene characteristics

Black/White makes greater use of dynamic 3D camera presentation and scene transitions than the Gen IV projects used to establish this renderer.

Explicitly validate:

- Camera rotations and unusual perspective.
- Terrain and buildings viewed from multiple angles.
- Billboarded characters and objects.
- Moving platforms and scripted geometry.
- Seasonal model and texture changes.
- Weather and fog.
- Large cities, bridges and camera-heavy routes.
- Battle arenas and transitions between 2D/3D scene elements.
- Cutscenes with rapid viewport or render-state changes.

Do not assume an optimization safe for Goldenrod remains safe for Castelia City or Skyarrow Bridge.

## 7.3 Geometry optimization

Reuse Kohjoh's existing approach:

**CPU:** preserve DS matrix/test semantics, command decoding, material changes and geometry resource tracking.

**PICA200:** transform object-space vertices, apply texture/material state and render retained indexed geometry.

The current renderer uses a palette of up to 16 clip matrices per batch; lit draws reserve uniform space and therefore use fewer matrices.

Preserve the compact vertex representation and the existing uniform-budget constraints.

Prioritize, in order:

1. Avoid producing geometry that will not be displayed, when semantics permit.
2. Retain unchanged display-list geometry.
3. Cull offscreen retained geometry.
4. Reuse converted textures.
5. Remove redundant state changes.
6. Reduce CPU-side geometry traversal.
7. Investigate meaningful material-compatible batch merges.

Do not merge batches merely because they are adjacent. Sinjoh documented a case where consecutive batches had no shared state and a proposed merge produced no benefit.

## 7.4 Unsupported PICA behaviors

Inventory these explicitly:

- Fog.
- Toon/highlight materials.
- Edge marking.
- Shadow volumes.
- Wireframe modes.
- DS auto-sorting of translucent polygons.
- Specialized depth/W-buffer behavior.
- Display capture and dependencies on previous 3D frames.

Implement features that Black/White actually exercises and which materially affect correctness.

For every unsupported operation, count it and record the triggering scene. Never silently discard commands.

**References:** [Sinjoh G3D backend](https://github.com/estyseesghosts/sinjoh/blob/1774be6/engine/graphics/g3d/g3d.c), [Coffeepot 3D scene optimisation](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/PICA200/3d-scene-optimisation.md), [3dbrew GPU registers](https://3dbrew.org/wiki/GPU/Internal_Registers).

---

# 8. Widescreen and WideArt

Treat widescreen as two related but distinct features:

1. **Wider real 3D rendering.**
2. **Recomposing 2D UI and adding art outside the original DS frame.**

Do not confuse adding side-band artwork with revealing more game world.

## 8.1 Display geometry

The original DS view is 256×192.

At 1.25× scaling, it becomes 320×240.

The 3DS top screen is 400×240, leaving 40 physical pixels on each side, equivalent to 32 DS pixels per side.

A full-width logical 2D composition is therefore 320×192 before scaling.

Use this coordinate model consistently across graphics, UI and hit testing.

### Widened 3D

Retain Sinjoh's viewport/projection fixup and widened bounding tests.

Ensure the extra world is genuinely rendered, rather than scaling the original 256-pixel image horizontally.

Do not blindly apply the same camera modification to every scene. Some scripted cameras and cutscenes require their own framing.

## 8.2 Reuse Sinjoh's WideArt

Start with:

- [`pack_wideart.py`](https://github.com/estyseesghosts/sinjoh/blob/1774be6/tools/wideart/pack_wideart.py)
- [`wideart.c`](https://github.com/estyseesghosts/sinjoh/blob/1774be6/engine/graphics/g2d/wideart.c)

The existing system:

- Associates art with layer fingerprints.
- Packs images offline.
- Converts to GPU-native RGBA5551 and PICA tile order.
- Stores lookup records and texture data.
- Loads the result without runtime PNG decoding.
- Draws retained edge geometry where appropriate.

Most of this can transfer directly.

What cannot transfer automatically is the **art itself, scene identification and game-specific layout logic**.

Create a new Black/White WideArt corpus.

## 8.3 Scene identification

Use two levels of identification:

**Semantic scene identity:** known game state, active screen, overlay, menu or scene transition supplied through narrow game-side hooks.

**Visual identity:** existing layer-art fingerprints, used to select image-specific layouts and distinguish variants.

Combine them where needed. A layer fingerprint alone cannot reliably describe how an entire UI should be rearranged.

Support version, language and season differences without treating every captured frame as a brand-new screen.

## 8.4 Layout operations

For individual elements, support operations equivalent to:

- Move/re-anchor.
- Stretch or repeat a compatible background.
- Copy existing game pixels.
- Wrap or fill background areas.
- Reposition OBJ groups.
- Resize/reframe a 3D viewport.
- Draw package-time WideArt.
- Preserve or relocate touch-sensitive regions.

Perform these operations on game elements when possible, rather than cropping and stretching flattened output.

### Scene priorities

Implement WideArt for:

1. Title and introductory screens.
2. Overworld UI.
3. Dialogues and text boxes.
4. Battle command menus and health boxes.
5. Party and Pokémon summary.
6. Bag, Pokédex and PC boxes.
7. Save/options/trainer card.
8. Special cutscenes and minigames.

For unsupported layouts, fall back to a correct centered original DS composition with suitable side fills. **Never use a broken widescreen layout merely to avoid showing the fallback.**

Keep any software image analysis and WideArt generation on the host packaging side.

---

# 9. Audio

The existing 3DS audio plumbing can remain largely intact.

Reuse Sinjoh's NDSP output, worker scheduling, ring/wave buffers and cache-maintenance rules.

However, Black/White's own ARM7 sound interface and sequencing behavior must be validated against Gen V.

Eidos already provides:

- SDAT parsing.
- SSEQ playback.
- SBNK and SWAR/SWAV support.
- PCM/ADPCM decoding.
- Instrument/track handling.
- A testable 16-channel sound model.

Use Eidos's Gen V implementation as the primary compatibility reference; reuse Kohjoh/Sinjoh's optimized sound-output approach wherever semantics match.

Do **not** run two independent sequencers or mixers.

Verify command timing, voice management, loop points, pitch, envelopes, PSG/noise and channel priority against a DS reference.

Retain Kohjoh's successful techniques:

- Block audio mixing.
- Predecoded reusable ADPCM where safe.
- Reduced expensive integer operations.
- Avoiding redundant resampling work.
- Proper DSP buffer cache flushing.
- Profiling the actual audio thread.

The game must not slow down because audio generation is tied to rendering.

**References:** [Eidos audio](https://github.com/Project-Holon/eidos/blob/2319b45/docs/audio.md), [Sinjoh audio output](https://github.com/estyseesghosts/sinjoh/blob/1774be6/engine/audio/audio.c), [Coffeepot audio](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/audio.md).

---

# 10. Content, saves and platform services

## Content packaging

Reuse Sinjoh's versioned indexed content-pack architecture, but create a Black/White-specific packer using Aletheia's NitroFS extraction and Eidos's file/archive knowledge.

Requirements:

- Verify ROM hash and game identity.
- Preserve original file IDs and archive indexes.
- Support edition-specific files.
- Preserve compression and data ordering where required.
- Avoid unnecessary unpacking at runtime.
- Support random-access reads and bounded caching.
- Detect corrupted/incomplete packs before gameplay.
- Keep source-ROM assets outside the repository and public releases.

Initial supported dumps: the documented USA/Europe English Pokémon Black and White cartridges. Expand languages after edition handling is verified.

[Supported ROM hashes](https://github.com/Project-Holon/aletheia/blob/50731dd/docs/ROMS.md), [Eidos file layer](https://github.com/Project-Holon/eidos/blob/2319b45/docs/files.md).

## Save handling

Reuse Sinjoh's transactional/two-generation storage design, not Platinum's save interpretation.

Black/White has its own save block layout and integrity rules. Eidos already implements Gen V save-related services and game-specific initialization.

Implement:

- Original save payload and block structure.
- Correct checksums, counters and active-copy selection.
- Import of verified raw cartridge/emulator saves.
- Atomic writes with recoverable backup generations.
- Validation after cold boot.
- Correct RTC/season behavior.
- Edition-specific identity and compatibility checks.

Do not assume that two games having the same physical save capacity makes their internal save structures interchangeable.

## Firmware and OS

Reuse Eidos's Gen V firmware/user-setting emulation where required, connecting it to the 3DS's actual settings, clock and touch calibration as appropriate.

Reuse Sinjoh for APT, HOME/sleep, input and exit handling.

Network-dependent systems should have explicit unsupported or offline behavior. Avoid fake successful communication responses that might corrupt the game's state.

**References:** [Eidos firmware](https://github.com/Project-Holon/eidos/blob/2319b45/sdk/firmware.c), [3dbrew APT](https://www.3dbrew.org/wiki/NS_and_APT_Services), [Coffeepot filesystem and saves](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/filesystem-and-saves.md).

---

# 11. Old 3DS performance contract

Use actual Old 3DS hardware as the performance authority.

An emulator may verify game execution and rendering, but cannot reliably reproduce ARM11 cache behavior, GPU contention, SD stalls or system-service scheduling.

### Frame budgets

- Presentation target: **30 Hz**, approximately 33.33 ms per presented picture.
- Game simulation: preserve measured original timing; a subsystem requiring 60 Hz execution cannot be arbitrarily reduced to 30 Hz.
- Audio: continuous, independently buffered, with no starvation.

Do not measure only completed pictures. Count missed deadlines and producer work as well.

### Required counters

| Area | Measurements |
|---|---|
| Game | Update time, scheduler tasks, overlays, resource loads |
| Graphics producer | FIFO words, decoded commands, cache hits/misses |
| Geometry | Vertices, triangles, batches, culled blocks, retained blocks |
| GPU | Draw calls, state changes, command-buffer usage, waits |
| 2D | Dirty tiles, atlas uploads, retained layers, blending passes |
| Audio | Mix duration, active voices, underruns, queue depth |
| Memory | Free heap, peak allocations, VRAM, cache eviction, fragmentation |
| Timing | Missed presentations, frame-age, game VBlanks, drift |
| Storage | Bytes read, seeks, file latency, save duration |

Implement release and profiling builds with isolated object directories. Profiling must be removable from the release binary.

### Optimization order

1. Identify the actual hot path from hardware.
2. Determine whether work is necessary.
3. Remove unnecessary traversal, copying and decoding.
4. Cache stable results.
5. Reduce state changes and draw submissions.
6. Improve locality and memory access.
7. Optimize arithmetic only where counters justify it.

Do not introduce a generic software compositor as a performance fallback.

Do not move the audio system to core 1 simply because it appears to be a spare CPU. Kohjoh/Sinjoh deliberately leave that shared system core alone.

### Memory

Maintain a measured memory ledger for:

- Compiled executable/code and static data.
- DS-visible RAM/state and overlay images.
- Game heap.
- Converted geometry and textures.
- 2D atlas.
- WideArt atlas.
- Audio buffers.
- Asset cache.
- Framebuffers and transient command buffers.

Budget against the actual usable application memory, not total physical FCRAM. Track peaks during scene transitions, not merely a stationary field.

**References:** [3dbrew memory layout](https://www.3dbrew.org/wiki/Memory_layout), [Coffeepot memory/cache](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/memory-and-cache.md), [Coffeepot CPU optimization](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/cpu-optimization.md).

---

# 12. Implementation milestones

Each milestone requires concrete evidence. A successful compile is not equivalent to a successful execution, and emulator execution is not equivalent to physical-hardware verification.

| Milestone | Work | Exit criteria |
|---|---|---|
| **M0 — Bootstrap** | Pin dependencies, import Sinjoh engine, prepare project build | Native minimal `.3dsx`, release/profile outputs, working logs |
| **M1 — Game compilation** | Aletheia source, Eidos build generators, ABI/SDK compatibility | Black and White source closures compile and link |
| **M2 — Native boot** | DS memory, firmware, overlays, game-owned main loop | Logos/title execute through original game code |
| **M3 — 2D** | G2D bridge, VRAM/OAM, palette, windows, effects | Representative boot/menu frames match oracle in original region |
| **M4 — 3D** | Geometry commands, model path, GPU-native rendering | First playable field with correct camera and geometry |
| **M5 — Gameplay** | Input, scripts, audio, resource loading, save flow | New game, save/continue, walking, wild battle, trainer battle |
| **M6 — Widescreen** | 400px view, camera/culling, 2D layouts, WideArt | Representative scene corpus correct in both editions |
| **M7 — Optimization** | Hardware profiling, hot-path removal, resource budgets | Target frame pacing on stock O3DS in measured scenes |
| **M8 — Coverage** | Major locations, screens, events, battles and effects | Full story and postgame validation corpus passes |
| **M9 — Packaging** | ROM preparation, releases, forwarders and documentation | Clean install, successful cold boot, save integrity and lifecycle tests |

Do not start with the largest field map or full WideArt implementation. First prove native boot, original behavior and a correct representative 2D/3D frame.

---

# 13. Regression and playtesting corpus

Build deterministic test cases from Black/White saves and recorded inputs.

For every case, record edition, build revision, starting save, frame number, expected game state, reference captures and hardware measurements.

### Coverage groups

**Boot and early game:** introductory cinematic, title, language-specific text, player naming, starters, first battles, initial saving, tutorial.

**Overworld:** Nuvema Town, Route 1, Accumula, Castelia City, Nimbasa, bridges, indoor maps, caves, surf/water scenes, seasonal variants and weather.

**Battles:** wild encounters, trainer fights, Gym Leaders, double/triple/rotation battles, status effects, animated Pokémon sprites, battle backgrounds, camera transitions, particle effects and legendary encounters.

**Menus:** party, summary, bag, Pokédex, PC storage, trainer card, options, save/load, shops, text entry and C-Gear behavior.

**Story:** major Plasma events, gym progression, Elite Four, N's Castle, Reshiram/Zekrom and ending sequence.

**Edition-specific:** Black City/White Forest, encounters, version-specific scripts, assets and presentation.

**Lifecycle:** HOME, sleep/wake, soft reset, repeated saves, long play sessions, low-memory situations and transitions between costly scenes.

Use melonDS or another validated NDS reference for original hardware behavior, Eidos for independent host-side oracles, LOTUS/CITnLO for controllable 3DS integration, and physical Old 3DS for performance signoff.

Comparisons should separate the **original 256×192-equivalent game region** from intentional widescreen additions. Original-region visual parity must not be sacrificed by unrelated remaster changes.

---

# 14. Agent workflow and engineering rules

Agents must make changes that can be reviewed and reproduced.

### Before implementation

- Identify the exact source revision and relevant functions.
- Read existing implementations before creating replacements.
- Identify whether work belongs to Aletheia, the Gen V compatibility layer, the shared 3DS engine or the game's presentation glue.
- Establish a reference test or at least a failing observable condition.
- Preserve upstream source rather than embedding unexplained edits.

### Each change must report

1. Files/functions changed and why.
2. Original behavior and new behavior.
3. Tests executed, including their commands or harness inputs.
4. Emulator evidence, if applicable.
5. Physical-hardware evidence, if available.
6. Performance before/after, when the change claims an optimization.
7. Outstanding unsupported behaviors and regressions.

Maintain an explicit status distinction between **implemented**, **compiled**, **tested headlessly**, **tested in emulator** and **verified on O3DS**.

### Things not to do

- Do not restart a proven renderer because a new game has rendering bugs.
- Do not reimplement Eidos's overlay loader without first identifying an actual incompatibility.
- Do not use a software frame renderer as a permanent bridge to the PICA200.
- Do not introduce a second game clock or independent frame loop.
- Do not hide unsupported graphics commands.
- Do not incorrectly reuse Platinum's save structures or WideArt fingerprints.
- Do not treat desktop performance or Azahar timing as O3DS performance.
- Do not commit ROMs, extracted Nintendo assets, saves or private capture data.
- Do not run concurrent builds into the same object directory.
- Do not claim a feature fixed without reproducing the original failure and testing it again.

When an optimization fails, preserve a concise record of the tested hypothesis, evidence, outcome and reason for rejection. This prevents repeated unproductive approaches without filling the runtime with experimental code.

**References:** [Kohjoh lessons](https://github.com/estyseesghosts/kohjoh/blob/7a98ace/docs/lessons.md), [Sinjoh lessons](https://github.com/estyseesghosts/sinjoh/blob/1774be6/docs/LESSONS.md), [Coffeepot testing on hardware](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/testing-on-hardware.md).

---

# 15. Distribution and provenance

Maintain separate provenance for the original game source, Eidos-derived code and the shared Kohjoh/Sinjoh engine.

**Important legal distinction:**

- Eidos's original code is MIT-licensed.
- Kohjoh/Sinjoh use GPL-3.0, with their own notices and provenance.
- Aletheia's tooling/documentation are MIT-licensed, but its reverse-engineered `src/` explicitly has **no license grant**.

Therefore, a user-supplied asset pack alone does not resolve the rights issues associated with distributing a precompiled executable containing Aletheia-derived game code. Review release legality and source provenance separately before shipping binaries.

Reference: [Aletheia LEGAL.md](https://github.com/Project-Holon/aletheia/blob/50731dd/LEGAL.md).

For the technical packaging flow, initially provide two distinct `.3dsx` launchers and two optional CIA forwarders. Keep game data on SD, with per-edition manifests and save directories. Adapt the existing Kohjoh/Sinjoh forwarder and packaging infrastructure instead of introducing another installer architecture.

---

# 16. Primary technical references

### Game and port internals

- [Aletheia — game source](https://github.com/Project-Holon/aletheia/tree/50731dd/src/game)
- [Aletheia — assets and archive documentation](https://github.com/Project-Holon/aletheia/blob/50731dd/docs/ASSETS.md)
- [Eidos — native boot](https://github.com/Project-Holon/eidos/blob/2319b45/docs/boot.md)
- [Eidos — overlays](https://github.com/Project-Holon/eidos/blob/2319b45/docs/overlays.md)
- [Eidos — 3D engine](https://github.com/Project-Holon/eidos/blob/2319b45/docs/engine-3d.md)
- [Eidos — scene composition](https://github.com/Project-Holon/eidos/blob/2319b45/docs/unified-view.md)
- [Sinjoh — architecture](https://github.com/estyseesghosts/sinjoh/blob/1774be6/docs/ARCHITECTURE.md)
- [Kohjoh — architecture](https://github.com/estyseesghosts/kohjoh/blob/7a98ace/docs/architecture.md)

### Nintendo 3DS hardware

- [3dbrew — Hardware](https://3dbrew.org/wiki/Hardware)
- [3dbrew — GPU internal registers](https://3dbrew.org/wiki/GPU/Internal_Registers)
- [3dbrew — GPU programming guide](https://3dbrew.org/wiki/GPU/Programming_Guide)
- [3dbrew — Memory layout](https://www.3dbrew.org/wiki/Memory_layout)
- [3dbrew — GSP services](https://www.3dbrew.org/wiki/GSP_Services)
- [devkitPro — libctru](https://github.com/devkitPro/libctru)
- [devkitPro — 3DS examples](https://github.com/devkitPro/3ds-examples)

### DS hardware and Coffeepot

- [GBATEK — DS hardware reference](https://mgba-emu.github.io/gbatek/)
- [Coffeepot — porting](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/porting.md)
- [Coffeepot — PICA200 renderer architecture](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/PICA200/renderer-architecture.md)
- [Coffeepot — GPU rendering](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/PICA200/gpu-rendering.md)
- [Coffeepot — asset pipeline](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/PICA200/asset-pipeline.md)
- [Coffeepot — performance](https://github.com/estyseesghosts/coffeepot/blob/209c8ed/ctr/engineering/performance.md)

---

## Final implementation decision

**Begin by porting Eidos's Aletheia integration into Sinjoh, not by porting Eidos's complete desktop engine to the 3DS and not by rewriting Black/White's engine.**

Aletheia provides the game. Eidos provides the missing Gen V-specific execution knowledge. Sinjoh provides the hardware-conscious native renderer. Kohjoh provides proven optimization approaches.

The main remaining unknown is not whether Black/White can be compiled into native code. It is whether its busiest scenes, renderer effects and game-side integration can operate within the stock Old 3DS CPU, GPU and memory budgets while preserving correct gameplay timing.

The first decisive proof is therefore a **Black/White native boot, followed by a correct field scene with instrumentation on actual O3DS hardware**. Every subsequent engineering decision should be measured against that baseline.
