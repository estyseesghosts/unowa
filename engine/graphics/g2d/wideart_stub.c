/*
 * wideart_stub.c: no wide-art pictures in unowa (yet).
 *
 * sinjoh's g2d draws hand-drawn 32-pixel bands beside the DS picture for
 * HGSS's 320-pixel field. unowa's M3 target is the DS canvas itself, 256x192
 * at the same 1.25x scale sinjoh uses (targets.c: scale 1.25, top offset 40),
 * so the wide path must be absent, not approximated. Returning zero pictures
 * makes g2d.c's `wide_art_on()` false; every band/edge branch is dead and the
 * renderer draws exactly the DS output. M6 adds the real pictures.
 *
 * GPL-3.0, same provenance as the g2d.c it serves: sinjoh 1774be6.
 */

#include "graphics/g2d/wideart.h"

bool kj_wideart_init(void)
{
    return true;
}

void kj_wideart_exit(void)
{
}

const kj_wideart_pic *kj_wideart_find(uint64_t key)
{
    (void)key;
    return NULL;
}

const kj_wideart_pic *kj_wideart_find_near(uint64_t key, int cur_h, int cur_v, int period_h,
                                           int period_v, int max_h, int max_v, int *dx_out)
{
    (void)key;
    (void)cur_h;
    (void)cur_v;
    (void)period_h;
    (void)period_v;
    (void)max_h;
    (void)max_v;
    if (dx_out != NULL) {
        *dx_out = 0;
    }
    return NULL;
}

C3D_Tex *kj_wideart_texture(void)
{
    return NULL;
}

uint32_t kj_wideart_texture_width(void)
{
    return 0;
}

uint32_t kj_wideart_texture_height(void)
{
    return 0;
}

uint32_t kj_wideart_count(void)
{
    return 0;
}
