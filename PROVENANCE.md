# Provenance

unowa is a native Old 3DS port of Pokémon Black and White. This file records
what each part of the tree is derived from. Game data is never imported.

## Engine: Sinjoh / Kohjoh (GPL-3.0)

The game-agnostic runtime pieces imported in M0 come from
[sinjoh](https://github.com/estyseesghosts/sinjoh) at `1774be6` (pinned in
`upstream.lock`), which is itself adapted from
[kohjoh](https://github.com/estyseesghosts/kohjoh) at `7a98ace`. Both are
GPL-3.0. Every imported file keeps its original notice and gains a header line
naming the change. Imported so far:

- `engine/runtime/kj_bridge.h` — the fixed-width, no-enum DS/3DS boundary.
- `engine/diagnostics/log.c`, `engine/diagnostics/log.h` — asynchronous SD log.

Adapted, not imported verbatim:

- `engine/storage/paths.c`, `engine/storage/paths.h` — unowa edition paths.
- `engine/app/main.c` — new minimal bootstrap (M0), modelled on sinjoh's entry.
- `engine/graphics/renderer.c`, `engine/graphics/renderer.h` — minimal
  GPU-native citro3d clear path for M0; the full atlas/G3D renderer is M3/M4.

The full renderer, geometry interpreter, audio and content pack are **not** in
the M0 build. They will be imported from Sinjoh with their notices when their
milestones start, not pre-emptively vendored as dead code.

## Game source and Gen V compatibility (not yet imported)

- [Aletheia](https://github.com/Project-Holon/aletheia) at `50731dd` supplies
  the Gen V game code. Its `src/` has no license grant; it is not redistributed.
- [Eidos](https://github.com/Project-Holon/eidos) at `2319b45` supplies Gen V
  execution knowledge. Its code is MIT-licensed. Per the unowa brief, import as
  little of it as possible; derive the rest from Aletheia and the DS hardware
  model.

## Hardware and platform references

- [Coffeepot](https://github.com/estyseesghosts/coffeepot) at `209c8ed`: CTR
  hardware, PICA200, engineering pages, and the C style guide.
- libctru / citro3d / 3dstools API documentation at the pinned devkitPro
  versions in `upstream.lock`.

## Not used

- Any ROM, extracted asset, save, capture or game text.
- A second frame scheduler, graphics state or audio pipeline.
- A software frame renderer in the shipping path.
