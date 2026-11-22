# The frame-25 crash (LOTUS #155)

`m1-test.3dsx` stopped LOTUS at frame 25 with an unmapped 32-bit write, and
the CITnLO worker later aborted on a write to an unknown GPU address. Both
writes come from one unowa bug, now fixed: the flat-image generator copied the
ARM9 module's autoload bytes into ARM9 BSS. The emulator's handling of the
second address is a separate defect (LOTUS #155).

## Observed

```text
HW.Memory <Error> unmapped Write32 0x00000000 @ 0xDFECD4CA PC 0x0028D540
Debug <Critical> video_core/gpu.cpp:420: Write to unknown GPU address 0x271483CA
worker exit 0xC000001D
```

## Reproduction

The crashing build was overwritten, so the repro rebuilds it exactly. The tree
was `1642c8f` without `engine/runtime/ds_boot.c` and its call in
`game_main.c`, and without `check_module_bounds`. That is the tree as it stood
before those two were added. One line of `tools/gen_flat_image.py` was then
reverted:

```python
tail_end = base + len(image_bytes)   # was min(..., module["ram_end"])
```

and build with `make -f engine/game.mk TARGET=3ds link-final`. The generator
then reports `bytes copied: 382964`, which is 0x940 more than the fixed build's
380596. That difference is the ARM9 autoload size (below). The resulting
`m1-test.3dsx` has SHA-256
`cde231fc1f16eadc88ed54b3448819b317c7545418d4a4beea818383a461b9f4`.

LOTUS: secondary session on card `unowa`, asset `unowa-m1`, CITnLO source
`a3994c1d`, worker SHA-256 `501a2a80…`, Vulkan renderer. No input. Run
`lotus_run_until(600)`; it stops at frame 25 every time (two sessions:
`0d00…e4fe…`, `1300…0c13…`):

```
stop: unmapped_memory, write, width 4, address 0xDFECD4CA, pc 0x0028D540
```

Not tested: standard Azahar (only CITnLO is configured in LOTUS here), real
hardware, and Old versus New 3DS settings. The fault comes from a fixed data
copy at boot, with no timing or model dependence, so every configuration should
see the same two stores. On hardware, both addresses are unmapped in the
application's address space (coffeepot `ctr/hardware/memory.md`, ARM11
userland map), so the first store would be a data abort. That is an inference,
not a run.

## The faulting instruction

devkitARM's `3dsx.ld` links at `0x00100000` and the 3DSX loader places the
code there, so guest PCs look up in the ELF unchanged (coffeepot
`ctr/engineering/debugging.md:77`).

```
0028d538 <unowa_gv_NNS_SndHandleReleaseSeq>:      nns_snd_player.c:211
  28d538: ldr   r3, [r0]          @ handle->player
  28d53c: cmp   r3, #0
  28d540: movne r3, #0
  28d544: ldrne r2, [r0]
  28d548: strne r3, [r2]          @ player->handle = NULL   <- both faults
  28d54c: strne r3, [r0]
```

LOTUS reports the first stop's PC as `0x28D540`, the start of the conditional
block. The store is `0x28D548`. Registers after the first stop: r0 = `0x00A631C0`,
r2 = `0xDFECD4CA`, lr = `0x00101A24` (`GFL_USE_InitSndSlot`). `NitroPort_MainMem`
is `0x009B9000` in that build, so r0 is DS `0x020AA1C0`, which is
`&_SndSlots[0].handle` (`_SndSlots` = `0x020AA1B8`; `struct SndSlot` is
0x1C bytes and `handle` is at +8).

Call path: `NitroMain` → … → `GFL_USE_InitSndStack` (`gfl_use.c:1363`, the
loop over six slots) → `GFL_USE_InitSndSlot` (`gfl_use.c:1327`) →
`NNS_SndHandleReleaseSeq`. On the DS `_SndSlots` is BSS, zero at boot, so
`handle->player` is NULL and the release does nothing. The game relies on that:
it releases before it calls `NNS_SndHandleInit`.

## The second address

`0x986483CA` was inferred from CITnLO's translation. It was then captured
directly. In the same session, an execute breakpoint at `0x28D548` with the
condition r2 == `0x986483CA` hit on the next loop iteration, before the store:

```
pc 0x0028D548   r0 0x00A631DC (DS 0x020AA1DC = &_SndSlots[1].handle)
r2 0x986483CA   r3 0   lr 0x00101A24 (GFL_USE_InitSndSlot)
```

The session was stopped there, so the store that crashes the worker never ran.
The GPU-address abort is therefore the same instruction as the first fault, in
the next iteration (slot 1), not a graphics access. Unowa generated the address
and the emulator then mishandled it. Execution was not corrupted.

## Where the values come from

Both handles hold bytes from the ROM's ARM9 binary.
`aletheia/extract/arm9_dec.bin` is `0xA67C0` bytes, loaded at `0x02004000`:

```
0x020AA1C0: dfecd4ca   (_SndSlots[0].handle)
0x020AA1DC: 986483ca   (_SndSlots[1].handle)
```

The ARM9 static module ends at `ram_end` `0x020A9E80`. The `0x940` bytes after
it in the file are the autoload blocks, ITCM code and DTCM data, which the DS
copies to the TCMs, not to main RAM. From `0x020A9E80` to `bss_end`
`0x021542C0`, main RAM is ARM9 BSS. `load_module`
(`engine/runtime/flat_memory.c`) zeroes BSS and then copies the spans. The
ARM9 data span ran to the end of the file, so it rewrote
`0x020A9E80..0x020AA7C0` with autoload bytes after the zeroing.
`_SndStack`/`_SndSlots` sit in that range.

## Graphics memory audit

The renderer took no part. `engine/graphics/renderer.c` owns two citro3d render
targets (`C3D_RenderTargetCreate`, 240x400 and 240x320, RGBA8 plus
D24S8), clears them, and submits them through `C3D_FrameBegin`/`FrameEnd`.
It does no pointer arithmetic on VRAM or linear memory, and no texture uploads.
The DS-side VRAM backing is sinjoh's static `NitroPort_LcdcVram[0xA4000]`,
which `unowa_ds_to_host` bounds-checks
(`ds_address - DS_LCDC_VRAM_START < DS_LCDC_VRAM_SIZE`). Neither fault address
is near either buffer.

## Indirect corruption audit

This is the class the bug belongs to: initialised data overwriting memory that
the game expects to be zero. Three more of the same kind were found while the
flat model was brought up. All are fixed in the generator, not per address:
untyped data pointers left as DS addresses, `u32`-typed pointer tables, and
overlay function pointers in holder words (`docs/status.md`, "The flat data
model"). The stray `1` in `gGFLNetMain` (`docs/status.md`, frame 16) was seen
under the old per-object model. Its writer was never identified. It has not
recurred under the flat model, whose boot now runs 1200 frames.

## Fix and regression check

- `tools/gen_flat_image.py` stops each module's tail at `ram_end`.
- The new `check_module_bounds` makes the generator exit with an error if any
  span or relocated word falls outside its module's `[ram_start, ram_end)`.
  Run against the unclamped generator, it fails the build:

  ```
  flat image out of bounds:
    arm9 span 0x0209D86C+0xCF54 outside 0x02004000..0x020A9E80
  ```

  The check runs at build time, so it costs nothing at runtime.
- Verified in LOTUS with the fixed build: frame 25 passes with no stop.

Not added: a per-frame runtime check. The overrun happens in one place, at
module load, and the build-time check covers that place completely.

## Emulator instrumentation that would have helped

- Report the store's exact PC for unmapped accesses, not the PC at the start of
  the block. The first stop said `0x28D540`; the store is `0x28D548`.
- After an unmapped-access stop, the registers show the state after the
  handler returned. In the first session PC and LR were both `0x101A24`, so the
  handler had run on to the return. A snapshot taken at the faulting
  instruction would avoid a second breakpoint run.
