I would split the work into **10 actionable issues**, ordered by dependency. Each should have a bounded scope, explicit acceptance criteria, and instructions for what the implementer must avoid.

## **P0 — Correctness blockers**

### **1. Establish one authoritative filesystem implementation**

**Files:** `engine/game.mk`, `engine/gv_sdk_sources.txt`, `engine/stubs/port_stubs.c`

**Deliverables**

- Establish whether every game-facing `FS_*` operation uses Sinjoh's filesystem backend.
- Remove or explicitly adapt incompatible Gen V filesystem implementations.
- Verify archive opening, file IDs, file lengths, seeking, reading, closing, and error handling.
- Add integration tests comparing returned bytes against the original ROM.
- Reproduce the previously failing archive access successfully in LOTUS.

**Avoidables**

- Do not add allocation workarounds to conceal failed filesystem operations.
- Do not mix filesystem implementations through automatic symbol renaming.
- Do not consider pack mounting proof that game-side reads work.

**Acceptance:** The game successfully reads known ROM assets, with matching IDs, lengths, and contents.

### **2. Replace heuristic data generation with verified memory layouts**

**Files:** `tools/gen_data_image.py`, `engine/game.mk`

**Deliverables**

- Derive actual symbol sizes, addresses, and ownership from Aletheia metadata wherever available.
- Correct BSS allocation sizes and initialization.
- Introduce explicit relocation records for data and function pointers.
- Detect ambiguous, missing, and overlapping symbols.
- Fail generation when required symbol layout information cannot be established.
- Add host-side tests for every supported relocation type.

**Avoidables**

- Do not retain arbitrary 64-byte BSS placeholders as a production solution.
- Do not guess object sizes from adjacent symbols without checking.
- Do not interpret every DS-address-looking integer as a pointer.
- Do not prioritize producing a linkable binary over preserving correct data.

**Acceptance:** Generated layouts and relocations pass independent tests using known ROM-derived data.

### **3. Implement overlay-aware loading and relocation**

**Files:** `tools/gen_data_image.py`, overlay integration, runtime bridge

**Deliverables**

- Give every overlay a unique identity independent of its DS load address.
- Track loaded overlays, memory residency, and lifetime.
- Support correct initialization, BSS clearing, relocation, unloading, and reloading.
- Ensure code and data references resolve against the correct overlay.
- Add synthetic regression tests for overlays occupying identical memory windows.
- Validate real overlay transitions in LOTUS.

**Avoidables**

- Do not select overlays by the first matching address.
- Do not assume overlays sharing addresses contain interchangeable data.
- Do not load all overlays permanently merely to avoid implementing residency.
- Do not implement a second, unrelated overlay system if Aletheia/Sinjoh functionality can be reused.

**Acceptance:** Multiple overlays sharing an original DS memory window can load in sequence without corrupting one another.

### **4. Fix SHA-1 and HMAC-SHA1 correctness**

**File:** `engine/stubs/port_stubs.c`

**Deliverables**

- Correct completed-block length accounting.
- Verify SHA-1 against standard vectors, including 55-, 56-, 63-, 64-, 65-, and 1,000-byte inputs.
- Verify HMAC-SHA1 against standard vectors.
- Validate incremental updates and finalization.
- Add reusable host-side tests.
- Check related handwritten cryptographic routines for equivalent problems.

**Avoidables**

- Do not write special-case fixes for individual failing input sizes.
- Do not change public SDK structures or calling conventions without ABI verification.
- Do not assume compilation or simple smoke tests establish cryptographic correctness.

**Acceptance:** Standard test vectors pass, including incremental and multi-block inputs.

## **P1 — Integration and reproducibility**

### **5. Correct Black/White ROM identity and content packaging**

**Files:** `tools/pack_game_data.py`, `engine/game.mk`, `engine/runtime/unowa_edition.c`

**Deliverables**

- Validate input ROMs using their actual documented hashes.
- Generate separate Black and White content packs.
- Verify extracted data belongs to the selected edition.
- Record the actual source ROM hash in manifests.
- Make edition selection consistent across binaries, data generation, and runtime paths.
- Add tests for mismatched ROMs, editions, manifests, and pack contents.

**Avoidables**

- Do not use the Platinum SHA-1 as the reported Black/White identity.
- Do not bypass Sinjoh's pack validation to make unsupported hashes work.
- Do not let `--edition white` silently consume Black's extracted data.
- Do not duplicate the entire build pipeline per edition.

**Acceptance:** Each edition builds independently and rejects content from the wrong ROM.

### **6. Replace unrestricted SDK symbol collision handling with explicit ownership**

**Files:** `tools/rename_genv.py`, `engine/gv_sdk_sources.txt`, `engine/game.mk`

**Deliverables**

- Inventory conflicting symbols between Aletheia, Gen V SDK, Sinjoh, and devkitPro.
- Define which implementation owns each subsystem.
- Introduce an explicit, reviewable override/renaming policy.
- Detect accidental ownership changes during builds.
- Document required compatibility adapters and their semantics.
- Add focused linkage and behaviour tests for critical interfaces.

**Avoidables**

- Do not treat every symbol collision as an equivalent implementation.
- Do not silently prefer Gen V or Sinjoh implementations based on link order.
- Do not create redundant SDK implementations where adapters are sufficient.
- Do not destabilize working subsystems through broad renaming changes.

**Acceptance:** Every substituted or renamed SDK symbol has a documented reason and an explicit owner.

### **7. Consolidate native game builds and repair incremental compilation**

**Files:** `Makefile`, `engine/game.mk`, `tools/build.sh`, `tools/build.ps1`

**Deliverables**

- Provide one supported entry point for native Black and White builds.
- Retain isolated development/test targets where useful.
- Make source patching, generated data, and linking proper build dependencies.
- Ensure changes to generators trigger relevant rebuilds.
- Verify clean and incremental builds.
- Preserve pinned Aletheia/Sinjoh revisions and build reproducibility.

**Avoidables**

- Do not maintain competing build pipelines with inconsistent configuration.
- Do not require full clean builds to apply ordinary source changes.
- Do not modify pinned upstream source trees in place.
- Do not silently reuse stale generated objects or archives.

**Acceptance:** Clean and incremental builds produce consistent binaries, and touching any generator rebuilds its outputs.

### **8. Establish measurable M2 acceptance tests**

**Files:** `docs/status.md`, new test harness, LOTUS integration

**Deliverables**

- Define concrete native-boot milestones.
- Automate assertions for filesystem access, memory initialization, overlays, threads, and game-state progression.
- Record reproducible LOTUS test inputs and expected results.
- Distinguish emulator-only validation from physical Old 3DS validation.
- Capture diagnostic counters and failures in machine-readable output.
- Add regression tests covering previously fixed boot failures.

**Avoidables**

- Do not treat 1,800 error-free emulator frames as proof of a functional game.
- Do not equate clear-frame presentation with game rendering.
- Do not mark M2 complete based only on compilation and successful linking.
- Do not optimize performance before basic execution correctness is established.

**Acceptance:** A repeatable LOTUS test proves meaningful game-state progression and critical initialization success.

## **P2 — Maintainability and future regression prevention**

### **9. Audit and classify every port stub**

**File:** `engine/stubs/port_stubs.c`

**Deliverables**

- Inventory all stubs and replacement routines.
- Classify each as implemented, intentionally unsupported, simulated, or temporary.
- Document expected semantics and callers.
- Add explicit diagnostics for unexpected calls to unsupported services.
- Replace unsafe no-op implementations where execution depends on their side effects.
- Add unit tests for important replacements.

**Avoidables**

- Do not silently return success for operations that did nothing.
- Do not let boot-only placeholders become permanent implementations unnoticed.
- Do not implement unused DS-specific hardware behaviour without a demonstrated need.
- Do not remove diagnostics merely because they produce noisy logs.

**Acceptance:** Every stub has documented behaviour, and unexpected unsupported calls can be detected.

### **10. Correct agent instructions and introduce concise engineering handoffs**

**Files:** `AGENTS.md`, `docs/status.md`, transcript documentation

**Deliverables**

- Replace stale Kohjoh-specific instructions and references with Unowa-specific guidance.
- Document architecture boundaries and which components come from Aletheia, Sinjoh, and Unowa.
- Maintain a concise verified-status document.
- Record known failures, attempted fixes, rejected hypotheses, and reproducible tests.
- Keep original agent transcripts as historical records.
- Require implementers to report tests actually run, not merely proposed.

**Avoidables**

- Do not repeatedly feed complete multi-megabyte transcripts to new agents.
- Do not present unverified agent conclusions as established facts.
- Do not duplicate contradictory project instructions.
- Do not let agents modify unrelated subsystems while resolving a bounded issue.
- Do not claim a milestone is complete without its acceptance checks.

**Acceptance:** A new agent can identify the current implementation state, remaining problems, and relevant prior failures without reading every historical transcript.

## **Recommended execution order**

1. **Immediate correctness**

   Issues 1, 4, and 9 — stabilize filesystem integration, cryptography, and identify unsafe stubs.
2. **Memory and overlays**

   Issues 2 and 3 — establish correct data layout and overlay behaviour before expanding renderer integration.
3. **Build-system reliability**

   Issues 5, 6, and 7 — fix edition isolation, SDK ownership, and reproducibility.
4. **Milestone validation**

   Issue 8 — demonstrate meaningful native game execution, not merely successful boot.
5. **Documentation and continuity**

   Issue 10 — update agent guidance and preserve verified results throughout the preceding work.

**Suggested gate before M3:** Issues 1–8 must have passing acceptance checks for the Black baseline. White-specific validation can follow immediately afterward, but the architecture must already support both editions.

I would not require perfect support for every DS service before beginning graphics work. I would require correct filesystem access, memory layout, overlay handling, and repeatable execution tests. Those are the foundations on which the renderer will depend.
