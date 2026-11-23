# **Unowa — actionable issue inventory**

Based on the latest `unowa(1).zip`, including the Git working tree, implementation documents, and agent transcripts, I would open **27 issues**.

These fall into three categories:

- **P0 — Immediate correctness:** Problems that can invalidate the current field-rendering work, corrupt data, or make debugging unreliable.
- **P1 — Integration and engineering quality:** Problems that will increasingly obstruct development as more game systems become active.
- **P2 — Completion and release readiness:** Missing functionality that should be tracked now, without necessarily being implemented immediately.

The first priority remains getting Nuvema Town rendering consistently. I would not interrupt that work for a large architectural rewrite, but I would introduce tests and fix dangerous correctness bugs alongside it.

## **P0 — Immediate correctness**

### **1. Diagnose and resolve the Nuvema Town field stall**

**Evidence:** `docs/status.md:1124–1195`, `engine/runtime/kj_g3.c`, `engine/graphics/renderer.c`

The game continues running, but the field becomes white, geometry submissions stop, and gameplay input appears ineffective. This is the immediate M4/M5 blocker.

**Deliverables**

- Trace the entire field geometry path: game model submission → geometry FIFO → interpreter → 3D frame → PICA draw.
- Record box tests, culled boxes, emitted vertices, batches, geometry swaps, and dropped geometry independently.
- Compare the same scene with melonDS at an equivalent game state.
- Determine whether the failure is missing model data, incorrect scene initialization, culling, or a blocked field-state transition.
- Restore field geometry and demonstrate walking and camera movement.
- Preserve a reproducible failing and passing LOTUS test.

**Avoidables**

- Don't disable culling globally to make geometry appear.
- Don't hardcode an identity matrix or model coordinates.
- Don't mistake a continuing game loop for functioning gameplay.
- Don't rewrite Sinjoh's rendering backend before locating the failing stage.

**Regression traps:** Title geometry, intro animations, lower-screen 3D, matrix stack push/pop, weather geometry, and transition back into the field.

### **2. Remove or isolate the unverified matrix recovery workaround**

**File:** `engine/runtime/kj_g3.c:143–166`

`unowa_pos_check()` replaces a degenerate position matrix with identity. The transcripts show that this did not resolve the field stall.

**Deliverables**

- Make matrix recovery diagnostic-only, disabled by default.
- Retain counters and writer identification without changing game behaviour.
- Trace the actual source of zero `MTX_LOAD_4x3` parameters.
- Verify expected matrix semantics against the original DS command stream.
- Remove the workaround if the root cause lies elsewhere.

**Avoidables**

- Don't normalize valid DS matrix states based on assumptions.
- Don't confuse a zero matrix during the intro with the separate field-stall state.
- Don't turn a debugging experiment into a permanent compatibility layer.

**Regression traps:** Matrix multiply, scale, translation, stack restores, box tests, and the title's existing geometry.

### **3. Restore trustworthy graphics diagnostics**

**Files:** `engine/game.mk:75–81,195`, `engine/runtime/kj_g3.c`

The agent dismissed the display-list cache using zero hit/miss counters. Those counters are compiled out by `HGSS_PROFILE_OFF`.

**Deliverables**

- Separate essential correctness counters from expensive performance profiling.
- Provide explicit diagnostic build configurations.
- Verify every reported counter can actually increment in that configuration.
- Run cache-enabled and cache-disabled builds against identical scene states.
- Compare geometry output and CPU/GPU behaviour.
- Record configuration flags alongside each diagnostic capture.

**Avoidables**

- Don't interpret disabled counters as evidence of no activity.
- Don't permanently enable expensive profiling in release builds.
- Don't change cache, culling, and matrix recovery simultaneously.
- Don't use debug-build performance as a release-performance measurement.

**Regression traps:** Display-list reuse, invalidation, geometry ordering, retained vertex data, and frame-to-frame state changes.

### **4. Make the experimental geometry interpreter reproducible**

**Files:** `engine/game.mk:189–198`, `engine/runtime/kj_g3.c`

The current makefile references the local `kj_g3.c`, but that file is untracked. The Git commit alone cannot reproduce the current renderer.

**Deliverables**

- Commit the validated local geometry implementation.
- Preserve its relationship to pinned Sinjoh source, including changes and attribution.
- Separate instrumentation from behavioural changes.
- Ensure a clean checkout builds without relying on an untracked source file.
- Preserve a way to test the original Sinjoh implementation.

**Avoidables**

- Don't commit every experimental workaround simply because it builds.
- Don't modify the pinned Sinjoh checkout.
- Don't maintain an unexplained 135 KB fork with no upstream comparison.
- Don't claim an experiment is validated until its rebuilt binary has been run.

**Regression traps:** Fresh checkout, clean rebuild, cached versus uncached geometry, and existing title/intro visuals.

### **5. Implement a deterministic LOTUS gameplay replay**

**Evidence:** Latest agent transcripts; `docs/handoff.md:880–905`

Agents repeatedly replay the complete intro manually to reach the field. This consumes considerable time and introduces inconsistent test states.

**Deliverables**

- Record the input sequence from boot through name entry and Nuvema Town.
- Support a fixed starting ROM, save state, build identity, and input timeline.
- Assert game-state checkpoints, not only frame counts.
- Automatically capture failure information and screenshots.
- Provide separate smoke tests for title, new game, first field, and field interaction.
- Require checkpoints to identify their exact binary hash.

**Avoidables**

- Don't rely exclusively on wall-clock delays between inputs.
- Don't reuse symbol addresses from an earlier binary.
- Don't use screenshots as the only success criterion.
- Don't build an elaborate testing framework before implementing a reliable basic replay.

**Regression traps:** Text speed, input polling, startup timing, title changes, and LOTUS version differences.

### **6. Complete and verify cross-overlay pointer relocation**

**Files:** `tools/gen_flat_image.py:429–649`, `engine/runtime/flat_memory.c`

The agents fixed several important pointer failures. Four known `gWeatherTable` pointers still contain raw DS addresses, and more unresolved pointer types may exist.

**Deliverables**

- Generate an auditable report of unresolved pointer candidates.
- Include source module, destination module, address, object type, and reason a pointer was not relocated.
- Correct the four documented weather pointers using actual field-type evidence.
- Add synthetic tests covering pointers between overlays sharing address ranges.
- Detect ambiguous resolutions and report them explicitly.
- Test real weather and map transitions that use these pointers.

**Avoidables**

- Don't reintroduce a broad pointer-looking-integer scan.
- Don't assume every exact function-address match is a valid function pointer.
- Don't silently choose one overlay when multiple candidates exist.
- Don't resolve this class through individual gameplay crash patches.

**Regression traps:** Cross-overlay function tables, weather definitions, overlay unload/reload, similarly addressed data, and values that happen to resemble pointers.

### **7. Verify overlay static initializer completeness and lifetime**

**Files:** `tools/gen_flat_image.py:620–649`, `engine/runtime/flat_memory.c:104–201`

The generator deliberately skips an entire static-initializer table if an entry cannot resolve to native code. That is safer than executing a partial table, but the runtime can subsequently load the overlay without those initializers.

**Deliverables**

- Report every overlay whose initializer table is missing or incomplete.
- Distinguish intentionally unused overlays from overlays needed for gameplay.
- Verify initializer order, repeated loading, BSS clearing, and unload/reload semantics.
- Make loading a required overlay fail explicitly when necessary initialization is unavailable.
- Add tests for state surviving or resetting across map transitions.

**Avoidables**

- Don't silently mark an overlay successfully initialized when required initializers were skipped.
- Don't execute copied DS machine code from the flat data image.
- Don't permanently retain overlay state merely to avoid reload problems.
- Don't run initialization routines more often than the original game.

**Regression traps:** Map transitions, battle entry/exit, weather tasks, script subsystems, and repeated visits to the same location.

### **8. Correct save-flash command semantics and bounds**

**File:** `engine/stubs/card_backup_port.c:156–205,233–249`

I found two particularly concrete bugs:

- `CARD_REQ_VERIFY_BACKUP` is grouped with write commands and calls `save_store()`. A verification operation therefore **writes data instead of comparing it**.
- `CARD_IdentifyBackup()` accepts size exponents up to 20, which permits a 1 MiB device, although `s_image` is only 512 KiB.

**Deliverables**

- Implement verification as a non-mutating comparison.
- Restrict accepted flash configurations to supported backing capacity or allocate the correct capacity dynamically.
- Validate read, write, program, erase, and verification semantics.
- Add bounds and invalid-argument tests for every command.
- Compare expected flash behaviour against the original game.

**Avoidables**

- Don't treat verification as another write operation.
- Don't accept unsupported flash sizes.
- Don't assume every write is legal regardless of flash erase/program state.
- Don't silently reinterpret unsupported commands.

**Regression traps:** New-game save initialization, repeated saving, checksum verification, erased storage, and invalid flash addresses.

### **9. Make save-file persistence failure-safe**

**File:** `engine/stubs/card_backup_port.c:83–148`

`save_open()` sets `s_opened` before confirming the file opened successfully. Failed file operations are ignored, and writes can report success when no file is open.

**Deliverables**

- Propagate file-open, seek, write, flush, and close failures to the save subsystem.
- Prevent a failed initial open from permanently suppressing retries.
- Verify full write lengths.
- Protect existing saves against interrupted writes, following the project's intended transactional storage model.
- Test cold boot, save, exit, restart, and continue.

**Avoidables**

- Don't report successful saving when data exists only in RAM.
- Don't overwrite the sole good save before validating its replacement.
- Don't change Gen V's game-side save format.
- Don't assume a successful `fflush()` proves power-loss durability.

**Regression traps:** Missing SD card, read-only filesystem, short writes, interrupted saves, corrupt existing saves, and edition-specific save paths.

### **10. Fix the SHA-1/HMAC-SHA1 implementation**

**File:** `engine/stubs/port_stubs.c:865–906`

The custom SHA-1 update routine doesn't correctly account for completed 64-byte blocks. The earlier host-side tests demonstrated failures with multi-block inputs and HMAC vectors.

**Deliverables**

- Correct total-message-length accounting.
- Test empty, short, boundary-length, and multi-block messages.
- Verify HMAC against published test vectors.
- Test multiple incremental updates.
- Audit related cryptographic replacements, including RC4, for correctness.

**Avoidables**

- Don't patch individual failing lengths.
- Don't replace the entire game crypto subsystem unnecessarily.
- Don't alter DS-visible structure layouts.
- Don't assume these routines are correct because ordinary boot succeeds.

**Regression traps:** Save integrity, authentication code, future network compatibility, and cryptographic routines sharing the same state structures.

## **P1 — Integration, graphics, and engineering quality**

### **11. Correct content-pack identity and validation**

**File:** `tools/pack_game_data.py:27–97`

The pack contains Gen V data, but its validation header uses a borrowed Platinum SHA-1. The manifest incorrectly reports that digest as the source ROM identity.

**Deliverables**

- Store the real source ROM hash in the manifest.
- Extend the pack validation layer to recognize supported Black/White ROM identities.
- Verify extracted files against the selected ROM, not merely the FNT filenames.
- Reject mismatched editions, invalid files, incomplete packs, and unsupported ROM revisions.
- Validate file IDs, offsets, lengths, alignment, and pack boundaries.

**Avoidables**

- Don't continue masquerading as a supported Platinum revision.
- Don't disable validation just to make the pack load.
- Don't assume files under `extract/data` belong to the requested ROM.
- Don't distribute ROM-derived packs.

**Regression traps:** Existing Black pack compatibility, filesystem file IDs, archive member reads, and incorrect Black/White data combinations.

### **12. Make the real game build edition-aware**

**Files:** `engine/game.mk`, `engine/app/game_main.c`, `tools/gen_flat_image.py`

The bootstrap supports Black and White, but the compiled native game is still Black-specific.

**Deliverables**

- Add an explicit edition build parameter.
- Select the appropriate Aletheia layout, ROM-derived data, pack and save paths.
- Produce distinct native Black/White binaries with separate output directories.
- Verify both editions independently in LOTUS.
- Prevent a White binary from silently loading Black data or saves.

**Avoidables**

- Don't duplicate the entire engine for White.
- Don't assume the two editions have identical data or overlay layouts.
- Don't make edition selection purely cosmetic.
- Don't make a Black-only build depend unnecessarily on the presence of a White ROM.

**Regression traps:** Startup, overlay loading, edition-specific scripts, save paths, and content-pack validation.

### **13. Replace automatic SDK symbol collision resolution with explicit ownership**

**Files:** `tools/rename_genv.py:87–120`, `engine/gv_sdk_sources.txt`

The collision renamer resolves linking conflicts by renaming matching global symbols. It does not establish whether those implementations have compatible behaviour.

That distinction matters: the filesystem integration previously failed because the game and content pack were using incompatible implementations.

**Deliverables**

- Generate a report of every conflicting SDK symbol.
- Assign each subsystem to an explicit implementation owner.
- Use an allowlist of intentional renames and compatibility adapters.
- Fail the build when an unexpected collision appears.
- Test critical API boundaries: FS, OS, memory, sound, PM, interrupts, and overlays.
- Document the assumptions behind every adapter.

**Avoidables**

- Don't equate identical C symbol names with identical behaviour.
- Don't use link order as an implementation-selection mechanism.
- Don't duplicate existing SDK services without a reason.
- Don't replace working interfaces during a cosmetic cleanup.

**Regression traps:** Filesystem access, ARM7 messaging, memory allocation, interrupt handling, audio sequencing, and overlay startup.

### **14. Consolidate and verify the build pipeline**

**Files:** `Makefile`, `engine/game.mk`, `tools/build.ps1`, `tools/build.sh`

The repository has a working M0 build path and a separate M1–M5 game build. Several prerequisites also depend on machine-local paths and prebuilt external trees.

**Deliverables**

- Provide one documented entry point for normal native builds.
- Retain separate bootstrap and diagnostic targets.
- Require or clearly expose verification of pinned upstream revisions and ROM identity.
- Audit every generated file for proper dependency tracking.
- Test clean, incremental, and forced rebuilds.
- Document Windows and Linux prerequisites.
- Make build outputs identify their edition, revision, flags, and dependency versions.

**Avoidables**

- Don't silently compile against an arbitrary Sinjoh or Aletheia revision.
- Don't use stale generated objects to hide broken source dependencies.
- Don't require every developer to reproduce one machine's directory structure.
- Don't replace working Make rules with a complicated new build system without evidence.

**Regression traps:** Generated source patches, flat data images, changed SDK source lists, shader builds, and parallel compilation.

### **15. Implement coherent DS VRAM bank mapping**

**Files:** `engine/runtime/unowa_frame.c`, `engine/runtime/flat_memory.c`, `engine/graphics/g2d/g2d.c`

VRAMCNT registers are decoded, and parts of the renderer already use the bank configuration. However, the primary BG/OBJ views still rely on flat shadow arrays, which do not fully model remapping between LCDC and graphics consumers.

**Deliverables**

- Establish one authoritative backing store for every DS VRAM bank.
- Resolve BG, OBJ, texture, palette and LCDC accesses through the current bank configuration.
- Handle bank enable/disable and mapping changes.
- Verify that writes through one mapped view are observable through another valid view.
- Add tests for the title's observed VRAM configurations and map transitions.

**Avoidables**

- Don't introduce independent copies of the same VRAM contents without synchronization rules.
- Don't make bank mapping a renderer-only illusion.
- Don't special-case the title screen's particular bank configuration.
- Don't invalidate unrelated graphics caches on every register read.

**Regression traps:** Extended palettes, texture uploads, direct-colour backgrounds, captured screens, OBJ rendering, and dynamic bank reassignment.

### **16. Implement actual&#x20;**`DISPCAPCNT`**&#x20;capture semantics**

**File:** `engine/graphics/g2d/g2d.c:2025–2032`

The renderer substitutes live layers for the contents of a captured VRAM display bank. That is not equivalent to the DS capture mechanism.

**Deliverables**

- Implement the relevant DS capture sources and destination-bank behaviour.
- Support source A/B blending and EVA/EVB coefficients.
- Preserve capture contents across frames until overwritten.
- Integrate capture memory with VRAM bank mapping.
- Verify capture timing and display routing against melonDS.
- Test the title and any transitions using capture effects.

**Avoidables**

- Don't redraw current layers when the game requests an earlier captured image.
- Don't overwrite the capture on frames where no capture occurs.
- Don't hardcode one specific title-screen configuration.
- Don't simulate the effect through a costly full-frame software renderer.

**Regression traps:** Title phase transitions, lower-screen 3D, fade effects, alpha blending, and rendering after bank remapping.

### **17. Complete missing DS 2D display modes**

**File:** `engine/graphics/g2d/g2d.c`

The 2D implementation supports a substantial portion of the DS graphics system, but several cases remain incomplete, including 256-colour bitmap backgrounds, bitmap OBJ paths, and certain display modes.

**Deliverables**

- Inventory unimplemented 2D modes with their actual game call sites.
- Implement modes demonstrated to be required by Black/White.
- Preserve palette, transparency, affine, window and blending semantics.
- Add test scenes for supported BG and OBJ formats.
- Generate explicit diagnostics for unsupported modes in diagnostic builds.

**Avoidables**

- Don't treat an unsupported mode as an empty transparent layer without reporting it.
- Don't add expensive generic implementations for modes the game never uses.
- Don't compromise the existing fast tiled-background path.
- Don't special-case individual sprites instead of their underlying format.

**Regression traps:** Text boxes, menus, affine backgrounds, animated OBJ, palette banks, and the current intro sequence.

### **18. Establish 3D visual parity and address unsupported effects**

**Files:** `engine/graphics/g3d/g3d.c`, `engine/runtime/kj_g3.c`, shaders

There are known incomplete effects: fog, edge marking, some shadow behaviour, and toon/highlight approximations. The title's later shading also remains unverified.

**Deliverables**

- Create a scene-based inventory of effects Black/White actually uses.
- Compare geometry, depth, lighting, texture colours, alpha and rasterization with the DS original.
- Prioritize differences affecting overworld and battle rendering.
- Implement missing effects only when their game use is established.
- Test CPU lighting and GPU lighting paths for equivalent visible output.
- Record performance costs on physical hardware.

**Avoidables**

- Don't implement every obscure DS 3D feature before establishing whether the game uses it.
- Don't change lighting solely to match a screenshot without examining the source state.
- Don't sacrifice correct transparency ordering for batching performance.
- Don't treat visually similar output as proof that the underlying geometry state is correct.

**Regression traps:** Translucent polygons, shadow geometry, polygon IDs, depth tests, toon materials, battle effects, and widescreen culling.

### **19. Prove real audio output and correct sound timing**

**Files:** `engine/audio/audio.c`, sound compatibility layer

The ARM7 sound driver now advances far enough to release the title-screen wait. That proves sound-clock progression, not audible output. LOTUS has reported zero audio samples.

**Deliverables**

- Determine whether NDSP is active or the silent fallback is driving sound state.
- Verify nonzero PCM generation from known music and sound-effect sequences.
- Confirm correct sample rate, channel routing, pitch and synchronization.
- Measure underruns and mixing cost.
- Test audio during transitions, loading, battles and sustained gameplay.
- Verify the fallback still advances required sound events when output is unavailable.

**Avoidables**

- Don't confuse sequencer progression with actual sound output.
- Don't report silence as success simply because the game no longer hangs.
- Don't tie sound generation to the 30 Hz presentation rate.
- Don't move work to the shared system core without a measured reason.

**Regression traps:** Title exit, Pokémon cries, music transitions, sound-effect completion waits, sleep/wake, and frame-time spikes.

### **20. Correct frame reporting and prove timing behaviour**

**Files:** `engine/stubs/port_stubs.c:571–628`, `engine/graphics/renderer.c`, `engine/runtime/frame_pacing.c`

`unowa_present_count` increments regardless of `unowa_renderer_present()` returning success. Some counters also report activity without proving a frame reached the display.

**Deliverables**

- Separate simulation ticks, presentation requests, successful GPU submissions, skipped frames, and observed display updates.
- Report frame-time distributions and long-frame spikes.
- Verify that simulation remains at the original game's required 60 Hz while 3DS presentation targets 30 Hz.
- Test failure and over-budget conditions.
- Confirm skipping presentation never skips required game simulation, audio or graphics side effects.
- Preserve the intended game clock during loading and stalls.

**Avoidables**

- Don't use presentation counts as FPS measurements.
- Don't slow the game's simulation to 30 Hz to achieve visual smoothness.
- Don't allow unlimited catch-up work after a stall.
- Don't optimize away geometry commands whose effects the game later reads.

**Regression traps:** Script timers, animation cadence, audio synchronization, geometry FIFO state, skipped frames, and transitions from expensive scenes to cheap ones.

## **P2 — Gameplay coverage, maintainability, and release readiness**

### **21. Establish physical Old 3DS memory and performance budgets**

**Files:** `engine/runtime/kj_g3.c`, `engine/graphics/g3d/textures.c`, `engine/graphics/renderer.c`, `engine/audio/audio.c`

The implementation inherits substantial caching and geometry infrastructure from Sinjoh. For example, the display-list cache has a 6 MiB allocation limit and the texture cache a 5 MiB budget. These are reasonable starting values, but have not been validated for Gen V's workload on stock hardware.

**Deliverables**

- Measure actual CPU, GPU, linear-memory, heap and VRAM usage on an Old 3DS.
- Record peak memory during scene changes, not just idle gameplay.
- Measure geometry decoding, texture conversion, GPU submission, 2D composition and audio separately.
- Establish measurable 30 FPS presentation and 60 Hz game-clock budgets.
- Identify expensive hot paths before optimizing.
- Add diagnostics for cache exhaustion, allocation failures and dropped geometry.

**Avoidables**

- Don't use LOTUS frame rates as physical hardware performance evidence.
- Don't assume cache limits appropriate for HGSS are appropriate for Black/White.
- Don't begin large optimization rewrites without profiling.
- Don't silently drop polygons when capacity is exhausted.
- Don't introduce New 3DS-only requirements.

**Regression traps:** Busy towns, weather effects, battles, large animations, repeated map transitions, memory fragmentation, and audio underruns.

### **22. Establish the first complete playable gameplay slice**

**Files:** Game runtime integration, filesystem, graphics and save backend

The current M5 evidence reaches field initialization, but does not yet establish ordinary gameplay.

**Deliverables**

- Start a new game and reach Nuvema Town.
- Move the player with correct collision and camera behaviour.
- Advance dialogue and scripted events.
- Enter and exit buildings or other maps.
- Open and close the ordinary game menu.
- Save, restart and continue.
- Enter and complete one wild battle and one trainer battle.
- Return to field gameplay without corrupted state.

**Avoidables**

- Don't implement a separate gameplay interpreter.
- Don't hardcode a playable demonstration disconnected from original game logic.
- Don't skip scripts or collision processing to avoid crashes.
- Don't expand to every map before completing one reliable slice.
- Don't introduce widescreen changes while debugging original gameplay mechanics.

**Regression traps:** Overlay transitions, map resources, script state, save flags, battle initialization, sound waits, and returning to the overworld.

### **23. Audit platform stubs and define unsupported-service behaviour**

**Files:** `engine/stubs/port_stubs.c`, `engine/stubs/wireless_stubs.c`, firmware compatibility code

Many hardware services are deliberately replaced with stubs. That is appropriate for initial bootstrapping, but their contracts need to be explicit.

**Deliverables**

- Inventory every compatibility stub and its game-facing callers.
- Classify each as implemented, simulated, intentionally unavailable, or temporary.
- Verify return values and observable side effects.
- Assert or log unexpected use of incomplete services.
- Test firmware settings, RTC, input, power/lifecycle and wireless initialization.
- Define the intended offline behaviour of C-Gear and Nintendo WFC features.

**Avoidables**

- Don't silently report success from a service that did nothing.
- Don't implement an entire obsolete wireless stack just to enable ordinary offline gameplay.
- Don't make startup dependent on internet connectivity.
- Don't replace an accurate simulation with an arbitrary constant without verifying callers.

**Regression traps:** C-Gear startup, time-dependent events, save settings, system sleep, soft reset, network menus, and game shutdown.

### **24. Introduce host-side tests and automated quality gates**

**Files:** `tools/`, `engine/stubs/`, `engine/runtime/`, new tests

There is no dedicated automated test suite in the supplied tree. This is particularly risky because the project relies on custom data generation, symbol rewriting and compatibility implementations.

**Deliverables**

- Build ROM-independent test fixtures for the flat-memory generator.
- Test pointer classification, relocation, overlapping overlays and ambiguous symbols.
- Test content-pack parsing and validation.
- Add save-flash, SHA-1/HMAC, memory helper and SDK adapter tests.
- Add static checks, formatting and compiler warnings for project-owned code.
- Run the tests automatically for each change.
- Document tests that cannot run without ROM-derived files or physical hardware.

**Avoidables**

- Don't require a complete ROM for tests that can use synthetic data.
- Don't create tests that only assert today's implementation output without checking correctness.
- Don't treat a clean compiler run as a complete test pass.
- Don't indiscriminately apply style refactors to imported Sinjoh code.
- Don't weaken tests when a regression occurs.

**Regression traps:** Generator changes, changed Aletheia metadata, source patches, compiler upgrades, ABI differences and build configuration changes.

### **25. Repair stale documentation, agent instructions and handoff practices**

**Files:** `AGENTS.md`, `CLAUDE.md`, `PROVENANCE.md`, `docs/status.md`, `docs/handoff.md`, `docs/UNOWA-2.md`

Several documents describe earlier milestones or even the wrong project:

- `AGENTS.md` identifies the repository as Kohjoh.
- It references Kohjoh-specific files not present in Unowa.
- `docs/handoff.md` reports an older HEAD and working-tree state.
- `PROVENANCE.md` still describes the renderer as a future import.
- `docs/UNOWA-2.md` references the obsolete `gen_data_image.py` rather than the current flat-image generator.
- Multiple overlapping transcripts are stored without a clear authoritative continuation record.

**Deliverables**

- Make `AGENTS.md` accurately describe Unowa and its subsystem ownership.
- Correct outdated file paths, commands and milestone descriptions.
- Maintain one concise current-status document.
- Separate verified results from hypotheses and incomplete experiments.
- Record failed approaches and why they failed.
- Keep full agent transcripts archived, while producing compact handoffs.
- Require agents to identify changed files, tests run, results, remaining limitations and build identity.

**Avoidables**

- Don't silently rewrite historical transcript evidence.
- Don't declare a milestone complete based on a previous agent's summary alone.
- Don't maintain contradictory milestone definitions across documents.
- Don't require every agent to reread tens of megabytes of transcripts.
- Don't discard useful failed-experiment history.
- Don't let documentation cleanup modify runtime behaviour.

**Regression traps:** New-agent onboarding, post-compaction continuity, build commands, source ownership, previous failure reproduction, and stale diagnostic addresses.

### **26. Implement widened presentation and WideArt as an independent milestone**

**Files:** `engine/graphics/g2d/wideart_stub.c`, `engine/graphics/renderer.c`, `engine/graphics/g3d/g3d.c`

Unowa currently scales the original DS image to 320×240 and centres it on the 400-pixel top screen. That is a correct baseline, but the intended widescreen experience is not implemented.

**Deliverables**

- Preserve the existing original-aspect-ratio rendering path.
- Extend the 3D projection and culling to use the wider top-screen area.
- Verify whether field and battle cameras expose appropriate additional world geometry.
- Adapt Sinjoh's WideArt infrastructure for Black/White-specific scenes.
- Handle 2D compositions, menus, transitions and screen swaps deliberately.
- Add side-by-side original-region comparisons against melonDS.
- Preserve correct bottom-screen touch coordinates.

**Avoidables**

- Don't stretch the original 256×192 image across 400 pixels.
- Don't invent scene-specific camera offsets without checking original engine behaviour.
- Don't reuse Platinum-specific WideArt fingerprints blindly.
- Don't compromise original-screen composition to fill the extra width.
- Don't start widescreen implementation before the field renderer is stable.

**Regression traps:** Camera clipping, invisible objects, field culling, battle framing, 2D overlays, scripted camera motion, and touch hitboxes.

### **27. Complete release packaging, installation and provenance checks**

**Files:** `Makefile`, `tools/`, `PROVENANCE.md`, distribution documentation

The project currently requires manual native builds and content-pack placement. The final installation flow is not implemented.

There is also a legal uncertainty: Aletheia's reconstructed source lacks an explicit license grant, even though the reused Sinjoh/Kohjoh engine has an identified license.

**Deliverables**

- Provide reproducible Black/White `.3dsx` release builds.
- Implement optional CIA forwarders using the proven project infrastructure.
- Build a user-supplied-ROM preparation workflow.
- Verify output hashes, edition identity, manifests and required game files.
- Test clean installation, first launch, cold boot, save persistence and removal.
- Document licenses and provenance for all imported or derived components.
- Obtain a rights review before distributing binaries containing reconstructed game code.
- Ensure release archives contain no ROM dumps, extracted proprietary game assets or private saves.

**Avoidables**

- Don't package Nintendo assets into public releases.
- Don't assume GPL compatibility settles rights to unlicensed reconstructed code.
- Don't distribute ROM-derived content packs.
- Don't require undocumented local paths or build tools for end users.
- Don't design a second installer architecture if the Kohjoh/Sinjoh approach is reusable.

**Regression traps:** Clean SD installation, missing or malformed packs, edition switching, save retention, CIA lifecycle, and distribution contents.

## **What I would actually prioritize**

### **Recommended development sequence**

1. **First: unblock the field.**

   Issues 1–5. Fix the rendering stall, remove unverified geometry workarounds, restore diagnostics and automate the LOTUS reproduction.
2. **Second: establish reliable gameplay state.**

   Issues 6–10, then 22. Validate overlay pointers, initialization, save behaviour and cryptography while completing a basic playable field.
3. **Third: close graphics correctness gaps.**

   Issues 15–20. Implement VRAM mapping and capture semantics, verify audio, and establish accurate timing and graphics measurements.
4. **Fourth: consolidate the implementation.**

   Issues 11–14 and 23–25. Fix content identity, edition support, SDK ownership, builds, tests and instructions.
5. **Finally: expand and release.**

   Issues 17–18 and 21–27 as applicable. Complete required rendering effects, measure physical O3DS performance, broaden gameplay coverage, implement widescreen and prepare packaging.

Some of these issues overlap deliberately in the components they test, but they should not all be implemented simultaneously.

The **most important new discovery compared with the earlier audit is issue 8**: the save-flash implementation's verification command actually writes data, and its accepted capacities can exceed its allocated buffer. That is a concrete correctness and potential memory-safety problem, independent of whether Nuvema Town renders successfully.

The second significant concern is the accumulation of unverified compatibility changes. The field work has made real progress, but the current combination of heuristic relocation, silently omitted initializers, geometry recovery and disabled instrumentation makes it increasingly difficult to determine *why* something works.

I would keep the current native-port architecture and concentrate on closing those correctness gaps with repeatable tests. None of the findings establishes that a major rewrite is necessary.

**Audit scope:** These findings are grounded in the supplied tree and agent transcripts. I did not independently run the ARM11 binary in LOTUS or on physical hardware.
