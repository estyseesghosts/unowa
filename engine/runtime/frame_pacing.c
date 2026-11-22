/*
 * frame_pacing.c: one emulated DS V-blank per physical 3DS V-blank.
 *
 * The game waits for a V-blank once per top-level step (Black/White's
 * NitroMain loop, Aletheia arm9/gfl_use.c:521). That wait reaches
 * NitroPort_OnFrame through sinjoh's NitroPort_Halt. If the presentation call
 * blocks on the display, it slows the game tick with it: the M0 path used
 * C3D_FrameBegin(C3D_FRAME_SYNCDRAW) under C3D_FrameRate(30), so each game
 * V-blank waited for the rate-limited frame counter to advance every second
 * V-blank and the game ticked at ~31 Hz instead of ~60 Hz (measured in LOTUS:
 * 635 game V-blanks over 1200 guest frames).
 *
 * Pacing and presentation are split here, as in sinjoh's kj_ctr_vblank and
 * kj_frame.c publish (reuse-audit.md, "Adapt, do not copy"):
 *
 *  - This module owns the emulated V-blank clock. It locks that clock to
 *    citro3d's physical V-blank counter and waits with gspWaitForVBlank when
 *    the game is early. A frame that ran long is not punished: the next DS
 *    frame starts at once and catches up, so game time does not drift away
 *    from audio, which runs on the real clock. A stall longer than
 *    RESYNC_VBLANKS resynchronises instead of fast-forwarding (coffeepot
 *    ctr/engineering/PICA200/frame-pacing.md, "Lock emulated V-blanks to the
 *    real counter" and "Catching up without a cap"; sinjoh engine/app/
 *    main.c:39-46,96-135).
 *  - The renderer presents at the frame boundary, separately, with a
 *    non-synchronising C3D_FrameBegin. Presentation may be skipped; the game
 *    tick never is.
 *
 * C3D_FrameCounter(0) must tick at the physical display rate for this to
 * work. C3D_FrameRate(30) makes it advance every second V-blank (coffeepot
 * frame-pacing.md, "Locking the frame rate"), so renderer.c leaves the frame
 * rate at 60 and the caller throttles presentation instead.
 */

#include "runtime/frame_pacing.h"

#include <3ds.h>
#include <citro3d.h>

#define UNOWA_RESYNC_VBLANKS 30u

static uint32_t s_base;
static uint32_t s_emulated;

uint32_t unowa_pacing_vblanks;
uint32_t unowa_pacing_late_vblanks;
uint32_t unowa_pacing_resyncs;

void unowa_frame_pacing_init(void)
{
    s_base = C3D_FrameCounter(0);
    s_emulated = 0;
}

void unowa_frame_pacing_vblank(void)
{
    uint32_t real = C3D_FrameCounter(0) - s_base;

    if (real > s_emulated + UNOWA_RESYNC_VBLANKS) {
        s_base += real - s_emulated - 1u;
        ++unowa_pacing_resyncs;
        real = C3D_FrameCounter(0) - s_base;
    }

    if (real <= s_emulated) {
        while ((C3D_FrameCounter(0) - s_base) <= s_emulated) {
            gspWaitForVBlank();
        }
    } else {
        ++unowa_pacing_late_vblanks;
    }

    ++s_emulated;
    ++unowa_pacing_vblanks;
}
