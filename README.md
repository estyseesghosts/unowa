# unowa

Native Old 3DS port of Pokémon Black and White. See `UNOWA.md` for scope,
milestones and engineering rules. This repository contains port code only; the
player supplies their own verified ROM for content packaging.

The game build boots the original Aletheia Gen V code on ARM11, renders through
the PICA200, and currently plays through the title, start menu, new-game intro
and the first field map (Nuvema Town) in 3D. It is a strong vertical slice, not
a shippable port. `docs/handoff.md` is the best starting point for state and
`docs/status.md` holds the evidence chain.

## Layout

```text
engine/          game-agnostic runtime and platform code
  app/           3DS entry, lifecycle, pacing
  runtime/       flat DS memory image, overlay loader, frame pacing, 2D bridge
  graphics/      kj 2D/3D backends, atlas, citro3d targets
  audio/         NDSP/silent-clock audio layer
  stubs/         fixed addresses, MSL/BIOS replacements, save device
tools/           build scripts and ROM-derived generators
  gen_flat_image.py   flat main-RAM image and pointer tables (needs the extract)
  patch_sources.py    build-tree rewrites of DS literals and address macros
  pack_game_data.py   PLATPAK v1 content pack from the player's NitroFS files
docs/status.md   milestone state and test evidence
docs/handoff.md  whole-project state, lessons and next moves
docs/reuse-audit.md  what unowa reuses from kohjoh/sinjoh and why
upstream.lock    pinned source revisions and toolchain versions
PROVENANCE.md    what is imported and from where
```

## Build the game

The game build needs sibling checkouts of Aletheia (with the player's own ROMs
extracted) and a bootstrapped sinjoh. See `docs/handoff.md` sections 9 and 10 for
the exact layout, toolchain and machine-local dependencies. From devkitPro's
MSYS2 shell, in this repository's root:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM
make -f engine/game.mk verify-pins verify-rom
make -f engine/game.mk link-final -j8
```

This produces `build/game/3ds/m1-test.3dsx` (~5.3 MiB) and its unstripped
`m1-test.elf`. A full rebuild takes several minutes; relinks take about 20
seconds. Symbols move on every link — re-run
`arm-none-eabi-nm build/game/3ds/m1-test.elf` before any LOTUS memory read.

The content pack is built separately from the player's Black ROM and written
into the LOTUS card's SD root as `3ds/unowa/data/black.pak` plus
`black.manifest.json`. Without it the game stops at frame 3. See
`docs/handoff.md` section 9.7.

## Build the M0 shell (toolchain smoke test)

`tools/build.ps1` and `tools/build.sh` build only the clear-screen bootstrap,
not the game:

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
tree `mkdir` lock refuses concurrent builds of the same tree. M0 produces four
`.3dsx` files under `build/<edition>/3ds[-prof]/`.

## Test

LOTUS is the integration debugger and is required for all verification. Add the
`.3dsx` as an approved asset handle in the LOTUS host config, create a fresh
secondary session from the `unowa` card, then drive it with the LOTUS MCP tools.
Registering assets is host-side; the MCP cannot do it. Do not treat emulator
timing as Old 3DS timing. See `docs/handoff.md` section 9.8 for input mapping,
counter addresses and expected values, and `docs/status.md` for what has
actually been run.

## Provenance and legal

The engine pieces are GPL-3.0 (Sinjoh/Kohjoh). Aletheia's `src/` has no
license grant; Eidos is MIT. Game data, ROMs, saves and captures are never
committed. See `PROVENANCE.md` and `UNOWA.md` section 15.
