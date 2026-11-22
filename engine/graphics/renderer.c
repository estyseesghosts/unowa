/*
 * renderer.c: the 3DS side of the frame boundary.
 *
 * Frame ownership and pacing stay as the M0 renderer had them: one
 * C3D_FrameBegin(0) submission per presented DS V-blank, never waiting for the
 * display, and a skipped picture never skips the game tick (coffeepot
 * ctr/engineering/PICA200/frame-pacing.md, "The timing model" and "Skip the
 * picture, keep the tick").
 *
 * The picture itself is now sinjoh's DS 2D engine on the PICA
 * (engine/graphics/g2d/g2d.c, GPL-3.0, sinjoh 1774be6). g2d translates one
 * engine's registers, palettes, BG/OBJ VRAM and OAM per scanline span into
 * ordered GPU passes; the tile atlas (engine/graphics/resources/atlas.c)
 * turns DS tiles into GPU texels with exact-compare caching. unowa supplies
 * the input (runtime/unowa_frame.c) and the targets (pica/targets.c).
 *
 * Scale is the DS picture at 1.25x: 256x192 -> 320x240. The top screen is
 * 400 px wide, so the picture is centred with 40 px on each side (the same
 * geometry sinjoh uses, targets.c:33-39). Those side bands stay black until
 * M6 adds the wide art; g2d's wide path is compiled out by wideart_stub.c.
 *
 * A clear is still drawn when the game has not configured the 2D engine
 * (DISPCNT == 0), so a boot failure shows the known navy/purple diagnostic
 * picture rather than an undefined framebuffer.
 */

#include "graphics/renderer.h"

#include "diagnostics/log.h"
#include "graphics/g2d/g2d.h"
#include "graphics/pica/gpu.h"
#include "graphics/pica/targets.h"
#include "graphics/resources/atlas.h"
#include "runtime/kj_bridge.h"

#include <3ds.h>
#include <citro3d.h>
#include <stddef.h>

#define CMDBUF_BYTES (512 * 1024)

/* The DS-side counter (engine/stubs/port_stubs.c). It is the emulated DS
 * V-blank clock, not the presentation count. */
extern unsigned int unowa_pacing_vblanks;
extern void unowa_frame_input(kj_frame_input *in, uint32_t vblanks, uint32_t game_frame);
/* sinjoh's H-blank walk: fills the per-line register shadow for this frame
 * (nitro_port.h:191). The DS runs it once per V-blank after the game armed
 * the effects; the renderer is the last reader, so it runs it here. */
extern int NitroPort_HBlankLinesOnce(void);

static bool s_ready;
static uint32_t s_presents;

bool unowa_renderer_init(void)
{
    if (!C3D_Init(CMDBUF_BYTES)) {
        kj_log("[gpu] C3D_Init failed");
        return false;
    }

    if (!kj_targets_init()) {
        kj_log("[gpu] render target allocation failed (VRAM free %lu KiB)",
               (unsigned long)(vramSpaceFree() / 1024));
        C3D_Fini();
        return false;
    }
    if (!kj_g2d_init()) {
        kj_log("[g2d] init failed");
        kj_targets_exit();
        C3D_Fini();
        return false;
    }

    s_ready = true;
    kj_log("[gpu] targets and 2D engine ready (linear free %lu KiB, VRAM free %lu KiB)",
           (unsigned long)(linearSpaceFree() / 1024), (unsigned long)(vramSpaceFree() / 1024));
    return true;
}

void unowa_renderer_exit(void)
{
    if (!s_ready) {
        return;
    }
    kj_g2d_exit();
    kj_targets_exit();
    C3D_Fini();
    s_ready = false;
}

bool unowa_renderer_present(uint32_t top_color, uint32_t bottom_color)
{
    if (!s_ready) {
        return false;
    }

    /* No SYNCDRAW: wait for the previous frame's GPU work only, never for
     * the display. Presentation is throttled to 30 Hz by the caller and may
     * be skipped; the game tick is not. */
    if (!C3D_FrameBegin(0)) {
        return false;
    }

    const kj_ds_memory *mem = kj_ds_get_memory();
    const uint32_t dispcnt = (uint32_t)mem->io[0] | ((uint32_t)mem->io[1] << 8) |
                             ((uint32_t)mem->io[2] << 16) | ((uint32_t)mem->io[3] << 24);

    if (dispcnt == 0) {
        /* The game has not brought the 2D engine up yet. */
        C3D_FrameDrawOn(kj_target_get(KJ_SCREEN_TOP)->rt);
        C3D_RenderTargetClear(kj_target_get(KJ_SCREEN_TOP)->rt, C3D_CLEAR_ALL, top_color, 0);
        C3D_FrameDrawOn(kj_target_get(KJ_SCREEN_BOTTOM)->rt);
        C3D_RenderTargetClear(kj_target_get(KJ_SCREEN_BOTTOM)->rt, C3D_CLEAR_ALL, bottom_color, 0);
        C3D_FrameEnd(0);
        ++s_presents;
        return true;
    }

    kj_frame_input in;
    (void)NitroPort_HBlankLinesOnce();
    unowa_frame_input(&in, unowa_pacing_vblanks, s_presents + 1u);

    kj_gpu_invalidate();
    kj_atlas_begin_frame();
    kj_g2d_begin_frame();

    /* Engine A is on the screen POWCNT1 says; the other engine is the other
     * screen (the DS can swap them). */
    const int top_engine = in.engine_a_on_top ? 0 : 1;
    kj_screen_draw(KJ_SCREEN_TOP, top_engine, &in);
    kj_screen_draw(KJ_SCREEN_BOTTOM, top_engine ^ 1, &in);

    C3D_FrameEnd(0);
    ++s_presents;
    return true;
}
