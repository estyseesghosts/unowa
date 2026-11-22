/*
 * renderer.c: M0 GPU-native presentation path for unowa.
 *
 * Created for unowa, modelled on sinjoh's pica/targets.c and renderer.c
 * (GPL-3.0, via kohjoh). This is intentionally only a clear path: it proves
 * the citro3d targets, 30 Hz pacing and frame submission work on the target
 * hardware. The tile atlas / retained G3D renderer is imported at M3/M4; a
 * software framebuffer is never added as a fallback.
 *
 * Render targets are the physical panel sizes (240x400 top, 240x320 bottom).
 * The logical screens are 400x240 and 320x240; the display transfer rotates
 * them. See coffeepot ctr/engineering/porting.md section 3.3 and
 * ctr/engineering/PICA200/frame-pacing.md.
 */

#include "graphics/renderer.h"

#include "diagnostics/log.h"

#include <3ds.h>
#include <citro3d.h>

#define CMDBUF_BYTES (256 * 1024)

/* Same transfer flags as sinjoh's pica/targets.c: RGBA8 render target to an
 * RGB8 scanout, no scaling, no vertical flip. */
#define DISPLAY_TRANSFER_FLAGS                                                                  \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |              \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static C3D_RenderTarget *s_top;
static C3D_RenderTarget *s_bottom;
static bool s_ready;

bool unowa_renderer_init(void)
{
    if (!C3D_Init(CMDBUF_BYTES)) {
        kj_log("[gpu] C3D_Init failed");
        return false;
    }

    s_top = C3D_RenderTargetCreate(UNOWA_TOP_HEIGHT, UNOWA_TOP_WIDTH, GPU_RB_RGBA8,
                                   GPU_RB_DEPTH24_STENCIL8);
    s_bottom = C3D_RenderTargetCreate(UNOWA_BOTTOM_HEIGHT, UNOWA_BOTTOM_WIDTH, GPU_RB_RGBA8,
                                      GPU_RB_DEPTH24_STENCIL8);
    if (s_top == NULL || s_bottom == NULL) {
        kj_log("[gpu] render target allocation failed (VRAM free %lu KiB)",
               (unsigned long)(vramSpaceFree() / 1024));
        unowa_renderer_exit();
        return false;
    }
    C3D_RenderTargetSetOutput(s_top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    C3D_RenderTargetSetOutput(s_bottom, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    /* Keep citro3d's frame counter at the physical 60 Hz display rate.
     * unowa_frame_pacing uses C3D_FrameCounter(0) as the emulated V-blank
     * clock, and C3D_FrameRate(30) would make it advance every second
     * V-blank, halving the game tick (coffeepot frame-pacing.md, "Locking the
     * frame rate"). Presentation is throttled to 30 Hz by the caller. */

    s_ready = true;
    kj_log("[gpu] targets ready (linear free %lu KiB, VRAM free %lu KiB)",
           (unsigned long)(linearSpaceFree() / 1024), (unsigned long)(vramSpaceFree() / 1024));
    return true;
}

void unowa_renderer_exit(void)
{
    if (s_top != NULL) {
        C3D_RenderTargetDelete(s_top);
        s_top = NULL;
    }
    if (s_bottom != NULL) {
        C3D_RenderTargetDelete(s_bottom);
        s_bottom = NULL;
    }
    s_ready = false;
    C3D_Fini();
}

bool unowa_renderer_present(uint32_t top_color, uint32_t bottom_color)
{
    if (!s_ready) {
        return false;
    }
    /* No SYNCDRAW: waiting for the display here would slow the game tick.
     * This only waits for the previous frame's GPU work (coffeepot
     * frame-pacing.md, "The timing model"). Presentation is throttled to 30 Hz
     * by the caller and may be skipped; the game tick is not. */
    if (!C3D_FrameBegin(0)) {
        return false;
    }
    /* A target must be declared drawn this frame or citro3d does not transfer
     * it to its screen at queue finish (sinjoh engine/graphics/g2d/g2d.c does
     * the same through C3D_FrameDrawOn); a clear alone leaves the LCD black. */
    if (!C3D_FrameDrawOn(s_top)) {
        C3D_FrameEnd(0);
        return false;
    }
    C3D_RenderTargetClear(s_top, C3D_CLEAR_ALL, top_color, 0);
    if (!C3D_FrameDrawOn(s_bottom)) {
        C3D_FrameEnd(0);
        return false;
    }
    C3D_RenderTargetClear(s_bottom, C3D_CLEAR_ALL, bottom_color, 0);
    C3D_FrameEnd(0);
    return true;
}
