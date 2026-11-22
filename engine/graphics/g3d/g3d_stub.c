/*
 * g3d_stub.c: no 3D scene yet (M4).
 *
 * sinjoh's g2d calls the 3D layer only when the frame input carries a decoded
 * geometry frame and DISPCNT enables 3D (g2d.c: `has_3d = engine == 0 &&
 * (dispcnt & 8) && in->g3 != NULL`). unowa's DS side does not produce one
 * until the geometry port lands, so these are the only entry points the 2D
 * path can reach. Keeping the real g3d.h signatures means M4 replaces this
 * file with the imported backend without touching g2d.
 *
 * GPL-3.0, same provenance as the g2d.c it serves: sinjoh 1774be6.
 */

#include "graphics/g3d/g3d.h"

void kj_g3d_set_frame(const kj_g3_frame *frame)
{
    (void)frame;
}

void kj_g3d_draw(const kj_target *target, bool wide, uint8_t stencil_flags, uint8_t window_on,
                 uint8_t window_active)
{
    (void)target;
    (void)wide;
    (void)stencil_flags;
    (void)window_on;
    (void)window_active;
}

bool kj_g3d_wbuffer(void)
{
    return false;
}
