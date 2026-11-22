# Reuse decisions: kohjoh and sinjoh

Read-only audits of the pinned kohjoh (`7a98ace`) and sinjoh (`1774be6`)
checkouts, done to decide what unowa imports and what it must write. Both
audits were source-checked; no code was copied from either tree in this step.
Coffeepot's porting guide (`ctr/engineering/porting.md:147-200`) sets the
boundary: reuse the platform skeleton and the hardware model, not the old
game's presentation rules.

## What unowa already has

- `engine/runtime/kj_bridge.h` — verbatim from sinjoh, with provenance.
- `engine/storage/paths.c` — adapted to `sdmc:/3ds/unowa`.
- `engine/graphics/renderer.c` — M0 clear-only path, not sinjoh's renderer.
- `engine/runtime/abi_assert.c` — new: compile-time proof the bridge ABI is
  still 32-bit and the documented struct sizes have not drifted.

## Reuse now

| Source | Decision | Reason |
|---|---|---|
| sinjoh `engine/compat/nitro/`, `engine/compat/nitrosystem/` | Build against, do not copy | Only proven ARM11 NitroSDK layer. `engine/game.mk` points at its built `libnitro.a` / `libnitrosystem.a`. Gen V differences go in unowa's own layer. |
| sinjoh `engine/compat/pak/src/pak.c`, `include/pak.h` | Reuse reader; new packer | Indexed by original file ID, no game-name literals (`pak.h:14-45`). Build ID comes from `kj_boot.c:25` and is Platinum-specific. |
| kohjoh `tools/lotus/compare.py`, staged-session sequence | Adopt pattern, not an MCP harness script | Byte/region baseline comparison and create → stage → reset ordering (`kohjoh/docs/lotus.md:53-59`). LOTUS is driven through the MCP tools directly; no wrapper script is committed. |
| kohjoh per-output `.build.lock` + freshness stamp | Adopt | Two builds in one tree "usually still boots, so the only symptom is measurement fiction" (`kohjoh/tools/game/hgss_game.py:1665-1703`). |
| sinjoh build guard `verify` and provenance calls | Adopt | `sinjoh/engine/game.mk:391-393,413-414`. unowa already has `verify-pins` in `engine/game.mk:28-33`. |
| sinjoh `engine/graphics/g2d/wideart.c` | Reuse unchanged | `SJWA` v1 blob parser only, no game names (`wideart.c:47-86,144-196`). Corpus and keys are game-specific. |
| kohjoh `src/graphics/pica/gpu.c`, `targets.c`, `src/graphics/layout/layout.c` | Reuse at M3 | Generic citro3d setup and row executor; no HGSS data in the code, only an origin comment (`layout.c:4`). |

## Adapt, do not copy

- `kj_frame.c` — publish mechanics transfer; the two-VBlank HGSS cadence,
  patch-2001 publish hook and `EncodeData` weak symbol do not
  (`sinjoh/engine/runtime/kj_frame.c:5-13,62-65,243-250`). Measure Black/White
  VBlank behaviour first (`UNOWA.md:250-256`).
- `g2d.c` / `atlas.c` — DS register semantics transfer; the 320-px canvas,
  32-px side bands, `BgKey` FNV-1a fingerprint and Platinum window edge 255 do
  not (`sinjoh/engine/graphics/g2d/g2d.c:858-865,987-993,1244-1254`).
- `g3d.c` / `kj_g3.c` — fixed-point matrix, texgen and readable tests transfer;
  wide-view constants (320, clip x 0.8), HGSS W-mode and direct-command
  patches do not (`sinjoh/engine/graphics/g3d/g3d.c:33-34,100-101,118-133`).
- `renderer.c` — frame ownership, skip-never-twice and counters transfer;
  lower menu, 320/1.25x layout and cipher-line dump do not
  (`sinjoh/engine/graphics/renderer.c:10-13,181-187,260-263`).
- `audio/audio.c` — NDSP plumbing transfers; the sequencer in
  `compat/nitro/src/port_snd.c` / `port_sound_chip.c` is Gen IV and must be
  replaced, never run beside a Gen V mixer (`UNOWA.md:562-564`).

## Replace or omit

- `kj_boot.c`, `kj_layouts.c`, `kj_screen.c`, scene tables, WideArt corpus,
  script `battle:` hooks, edition names, build IDs, save containers and all
  HGSS/Platinum patches. Concrete examples:
  `kohjoh/src/runtime/kj_layouts.c:27-153`,
  `kohjoh/src/runtime/kj_screen.c:37-70`,
  `kohjoh/src/app/script.c:135-149,283-305`,
  `sinjoh/engine/runtime/kj_boot.c:19-25,89-91`.
- sinjoh `engine/storage/save_raw.c:1-4` is an interim single-file backend;
  the architecture doc's two-generation claim is not what the code does.
- kohjoh's HGSS compat data (~3,800 lines in `src/compat/hgss/`) and 106
  patches are wrong for Gen V.

## Lessons carried forward

1. Publish at one coherent point on the game thread. Adding a platform-side
   scheduler would recreate the race kohjoh removed
   (`kohjoh/docs/lessons.md:55-57`).
2. Skip presentation, never the game tick; cap consecutive skips. An uncapped
   skip produced half-speed gameplay that looked like a renderer bug
   (`kohjoh/docs/lessons.md:129-132`).
3. Get a pixel oracle before changing rendering. A wrong theory about a
   "washed out" field lasted days until an independent renderer localised the
   pass-order fault (`sinjoh/docs/LESSONS.md:47-60`). Port the stencil-writer
   invariant, not a memorised draw order (`sinjoh/engine/graphics/g2d/g2d.c:792-806`).
4. Never run two builds into one object tree; with four edition/profile
   artifacts unowa is exposed to the same measurement fiction
   (`sinjoh/docs/LESSONS.md:312-335`).
5. GPU-visible buffers use `linearAlloc`, not `malloc`
   (`sinjoh/docs/LESSONS.md:62-86`).

## Verification note

No real Old 3DS is available. Sinjoh's own status marks Azahar/LOTUS as
integration debuggers, not timing authorities (`sinjoh/docs/STATUS.md:493-498`).
All unowa evidence so far is compile or emulator evidence.
