# unowa

Native Old 3DS port of Pokémon Black and White. See `UNOWA.md` for scope,
milestones and engineering rules. This repository contains port code only; the
player supplies their own verified ROM for content packaging.

## Layout

```text
engine/          game-agnostic runtime and platform code
  app/           3DS entry, lifecycle, pacing
  runtime/       kj_bridge.h boundary contract, edition identity
  diagnostics/   asynchronous SD log
  storage/       SD path roots
  graphics/      M0 GPU-native citro3d clear path
tools/           build scripts; gen_data_image.py reads the player's ROM
docs/status.md   milestone state and test evidence
docs/reuse-audit.md  what unowa reuses from kohjoh/sinjoh and why
upstream.lock    pinned source revisions and toolchain versions
PROVENANCE.md    what is imported and from where
```

## Build

Prerequisites: devkitPro (`devkitARM`, `libctru`, `citro3d`, `3dstools`) and
Git for Windows on PATH ahead of MSYS2's git. Pinned versions are in
`upstream.lock`.

```sh
pwsh tools/build.ps1 -AllProfiles
```

or, from devkitPro's MSYS2 shell:

```sh
sh tools/build.sh 0 0   # Black release
sh tools/build.sh 0 1   # Black instrumented
sh tools/build.sh 1 0   # White release
sh tools/build.sh 1 1   # White instrumented
```

Each edition/profile pair uses a separate object tree under `build/`, so a
release build cannot reuse objects compiled with instrumentation flags. A per
tree `mkdir` lock refuses concurrent builds of the same tree.

M0 produces four `.3dsx` files under `build/<edition>/3ds[-prof]/`. The
instrumented builds print `[pace]` counters to `sdmc:/3ds/unowa/logs/<edition>.log`
and are slower; use release builds for anything resembling performance work.

## Test

LOTUS is the integration debugger. Add the desired `.3dsx` as an approved
asset handle in the LOTUS host config, then drive a windowed session with the
LOTUS MCP tools. Do not treat emulator timing as Old 3DS timing. See
`docs/status.md` for what has actually been run.

## Provenance and legal

The engine pieces are GPL-3.0 (Sinjoh/Kohjoh). Aletheia's `src/` has no
license grant; Eidos is MIT. Game data, ROMs, saves and captures are never
committed. See `PROVENANCE.md` and `UNOWA.md` section 15.
