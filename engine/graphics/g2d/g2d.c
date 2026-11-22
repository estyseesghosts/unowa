/*
 * g2d.c: DS 2D engines on the PICA (GPU_2D.md).
 *
 * Translates one engine's state per scanline span into ordered GPU passes:
 *   clear (backdrop colour; stencil = backdrop's effect flags)
 *   window regions -> stencil bits 4-5 (0 outside, 1 WIN0, 2 WIN1)
 *   for priority 3..0: BG3..BG0 of that priority (BG0 may be the 3D layer),
 *                      then the OBJs of that priority (lower OAM index on top)
 *   brightness (BLDY) pass on target-1 pixels; master brightness pass
 *
 * Stencil bits: bit 0 = the top pixel belongs to a 2nd blend target,
 * bit 1 = the top pixel is a 1st target with effects enabled (for BLDY),
 * bits 4-5 = window region. Alpha blending (BLDCNT mode 1, semi-transparent
 * OBJ) is two draws: blended where bit 0 is set, opaque where not; each
 * updates bit 0 to the drawn layer's own 2nd-target membership.
 *
 * Every tile reaches the GPU through the atlas (exact-compare cache). Modes
 * this file cannot draw are counted in unsupported_modes, never replaced by
 * a CPU picture.
 */

#include "graphics/g2d/g2d.h"

#include "diagnostics/log.h"
#include "diagnostics/perf.h"
#include "graphics/g2d/wideart.h"
#include "graphics/g3d/g3d.h"
#include "graphics/pica/gpu.h"
#include "graphics/pica/targets.h"
#include "graphics/resources/atlas.h"

#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LAYER_OBJ 4
#define LAYER_BD 5

/* ------------------------------------------------------------ palettes */

enum {
    PAL_4BPP = 0,        /* + engine*32 + kind*16 + bank   (64) */
    PAL_STD256 = 64,     /* + engine*2 + kind               (4) */
    PAL_EXT_BG = 128,    /* + engine*64 + slot*16 + index (128) */
    PAL_EXT_OBJ = 256,   /* + engine*16 + index            (32) */
    PAL_COUNT = 288
};

static kj_palette s_pal[PAL_COUNT];
static kj_g2d_stats s_stats;

/* Temporary direct-colour BG evidence (M3): calls, emitted cells, and cells
 * the atlas reported fully transparent. */
unsigned int unowa_direct_calls;
unsigned int unowa_direct_cells;
unsigned int unowa_direct_clear;
unsigned int unowa_direct_cells_n;
unsigned int unowa_direct_nofit;
unsigned int unowa_direct_norun;
int unowa_direct_box[4];

/* Temporary bottom-screen evidence: how often engine B is drawn, skipped as
 * unchanged, and how many OBJ quads it contributes. */
unsigned int unowa_bottom_calls;
unsigned int unowa_bottom_skips;
unsigned int unowa_bottom_draws;
unsigned int unowa_bottom_obj_quads;

static const uint16_t *pltt_ptr(const kj_ds_memory *mem, int engine, int kind)
{
    return (const uint16_t *)(mem->pltt + engine * 0x400 + kind * 0x200);
}

/* Find the LCDC-array address of an extended palette slot, from VRAMCNT.
 * NULL when no bank provides it. */
static const uint8_t *ext_bg_slot(const kj_frame_input *in, int engine, int slot)
{
    const kj_vram_bank *b = in->banks;
    const uint8_t *lcdc = in->mem->lcdc_vram;

    if (engine == 0) {
        if (b[4].enabled && b[4].mst == 4) { /* E: slots 0-3 */
            return lcdc + b[4].lcdc_offset + slot * 0x2000;
        }
        for (int i = 5; i <= 6; ++i) { /* F, G: OFS 0 -> slots 0-1, OFS 1 -> 2-3 */
            if (b[i].enabled && b[i].mst == 4) {
                const int first = (b[i].ofs & 1) ? 2 : 0;
                if (slot >= first && slot < first + 2) {
                    return lcdc + b[i].lcdc_offset + (slot - first) * 0x2000;
                }
            }
        }
        return NULL;
    }
    if (b[7].enabled && b[7].mst == 2) { /* H */
        return lcdc + b[7].lcdc_offset + slot * 0x2000;
    }
    return NULL;
}

static const uint8_t *ext_obj(const kj_frame_input *in, int engine)
{
    const kj_vram_bank *b = in->banks;
    const uint8_t *lcdc = in->mem->lcdc_vram;

    if (engine == 0) {
        for (int i = 5; i <= 6; ++i) {
            if (b[i].enabled && b[i].mst == 5) {
                return lcdc + b[i].lcdc_offset;
            }
        }
        return NULL;
    }
    if (b[8].enabled && b[8].mst == 3) {
        return lcdc + b[8].lcdc_offset;
    }
    return NULL;
}

static kj_palette *pal_4bpp(const kj_frame_input *in, int engine, int kind, int bank, uint32_t *id)
{
    *id = PAL_4BPP + engine * 32 + kind * 16 + bank;
    kj_palette *p = &s_pal[*id];
    p->colors = pltt_ptr(in->mem, engine, kind) + bank * 16;
    p->count = 16;
    kj_palette_check(p);
    return p;
}

static kj_palette *pal_256(const kj_frame_input *in, int engine, int kind, uint32_t *id)
{
    *id = PAL_STD256 + engine * 2 + kind;
    kj_palette *p = &s_pal[*id];
    p->colors = pltt_ptr(in->mem, engine, kind);
    p->count = 256;
    kj_palette_check(p);
    return p;
}

static kj_palette *pal_ext_bg(const kj_frame_input *in, int engine, int slot, int index,
                              uint32_t *id)
{
    const uint8_t *base = ext_bg_slot(in, engine, slot);
    if (base == NULL) {
        return pal_256(in, engine, 0, id);
    }
    *id = PAL_EXT_BG + engine * 64 + slot * 16 + index;
    kj_palette *p = &s_pal[*id];
    p->colors = (const uint16_t *)(base + index * 512);
    p->count = 256;
    kj_palette_check(p);
    return p;
}

static kj_palette *pal_ext_obj(const kj_frame_input *in, int engine, int index, uint32_t *id)
{
    const uint8_t *base = ext_obj(in, engine);
    if (base == NULL) {
        return pal_256(in, engine, 1, id);
    }
    *id = PAL_EXT_OBJ + engine * 16 + index;
    kj_palette *p = &s_pal[*id];
    p->colors = (const uint16_t *)(base + index * 512);
    p->count = 256;
    kj_palette_check(p);
    return p;
}

/* ------------------------------------------------------------ registers */

static uint16_t r16(const uint8_t *regs, unsigned off)
{
    return (uint16_t)(regs[off] | (regs[off + 1] << 8));
}

static uint32_t r32(const uint8_t *regs, unsigned off)
{
    return (uint32_t)regs[off] | ((uint32_t)regs[off + 1] << 8) | ((uint32_t)regs[off + 2] << 16) |
           ((uint32_t)regs[off + 3] << 24);
}

/* The context of one engine's span. */
typedef struct ctx {
    const kj_frame_input *in;
    const kj_target *target;
    int screen;
    int engine;
    const uint8_t *regs;
    uint32_t dispcnt;
    const uint8_t *bg_vram;
    uint32_t bg_vram_size;
    const uint8_t *obj_vram;
    uint32_t obj_vram_size;
    int y0, y1; /* span lines [y0, y1) */
    /* The view: DS pixel -> physical pixel, and the physical rectangle the
     * draw is clipped to. The screen draw uses the target's own scale, offset
     * and full rectangle; the lower-screen menu's panel passes its own. */
    float sx, sy, ox, oy;
    int clip_x0, clip_y0, clip_x1, clip_y1;
    bool full; /* the screen's own draw: whole target, master brightness to the edges */
} ctx;

/* ------------------------------------------------------------ quad emit */

static inline void put_quad(kj_v2d *v, int x, int y, int w, int h, uint16_t slot, bool hf, bool vf)
{
    int16_t u0, v0, u1, v1;
    kj_atlas_slot_uv(slot, &u0, &v0, &u1, &v1);
    if (hf) {
        const int16_t t = u0;
        u0 = u1;
        u1 = t;
    }
    if (vf) {
        const int16_t t = v0;
        v0 = v1;
        v1 = t;
    }
    v[0] = (kj_v2d){ (int16_t)x, (int16_t)y, u0, v0, 255, 255, 255, 255 };
    v[1] = (kj_v2d){ (int16_t)(x + w), (int16_t)y, u1, v0, 255, 255, 255, 255 };
    v[2] = (kj_v2d){ (int16_t)x, (int16_t)(y + h), u0, v1, 255, 255, 255, 255 };
    v[3] = (kj_v2d){ (int16_t)(x + w), (int16_t)(y + h), u1, v1, 255, 255, 255, 255 };
}

/* A run of quads: either in the frame arena (reserve a worst case, give the
 * tail back) or in a retained layer's own buffer (ext = true). Retained runs
 * also record each quad's atlas slot and the range of character bytes read,
 * which is what their reuse check compares next frame. */
typedef struct run {
    kj_v2d *v;
    uint32_t first;
    uint32_t count;
    uint32_t cap;
    bool ext;
    bool incomplete; /* atlas or quad capacity failure: retry the retained build */
    uint16_t *slots;
    uint32_t char_lo, char_hi;
} run;

static bool run_begin(run *r, uint32_t cap)
{
    r->count = 0;
    r->incomplete = false;
    r->char_lo = UINT32_MAX;
    r->char_hi = 0;
    if (r->ext) {
        return r->v != NULL;
    }
    r->cap = cap;
    r->slots = NULL;
    r->v = kj_gpu_quads_alloc(cap, &r->first);
    return r->v != NULL;
}

static void run_end(run *r)
{
    if (!r->ext) {
        kj_gpu_quads_reset_to(r->first + r->count);
    }
}

static inline void run_note(run *r, uint32_t off, uint32_t bytes)
{
    if (off < r->char_lo) {
        r->char_lo = off;
    }
    if (off + bytes > r->char_hi) {
        r->char_hi = off + bytes;
    }
}

/* ------------------------------------------------------------ text BGs */

static uint32_t text_bg(const ctx *c, int bg, run *out)
{
    const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
    const int hofs = r16(c->regs, 0x10 + 4 * bg) & 0x1FF;
    const int vofs = r16(c->regs, 0x12 + 4 * bg) & 0x1FF;
    const int size = cnt >> 14;
    const int w = (size & 1) ? 512 : 256;
    const int h = (size & 2) ? 512 : 256;
    const bool bpp8 = (cnt & 0x80) != 0;
    uint32_t char_base = ((cnt >> 2) & 15u) * 0x4000u;
    uint32_t scr_base = ((cnt >> 8) & 31u) * 0x800u;
    if (c->engine == 0) {
        char_base += ((c->dispcnt >> 24) & 7u) * 0x10000u;
        scr_base += ((c->dispcnt >> 27) & 7u) * 0x10000u;
    }
    const bool ext = bpp8 && (c->dispcnt & (1u << 30));
    const int ext_slot = bg < 2 ? bg + (((cnt >> 13) & 1) ? 2 : 0) : bg;
    const uint32_t tile_bytes = bpp8 ? 64u : 32u;
    const uint32_t mask = c->bg_vram_size - 1u;
    const uint32_t source = (uint32_t)(c->engine * 2);

    const int first_sy = c->y0 - ((c->y0 + vofs) & 7);
    if (!run_begin(out, 33u * (uint32_t)((c->y1 - first_sy + 7) / 8))) {
        return 0;
    }
    for (int sy = first_sy; sy < c->y1; sy += 8) {
        const int my = (sy + vofs) & (h - 1);
        const uint32_t row_base = scr_base + (uint32_t)((my >> 8) * (w >> 8)) * 0x800u +
                                  (uint32_t)((my & 255) >> 3) * 64u;
        for (int sx = -(hofs & 7); sx < 256; sx += 8) {
            const int mx = (sx + hofs) & (w - 1);
            const uint32_t addr = (row_base + (uint32_t)(mx >> 8) * 0x800u +
                                   (uint32_t)((mx & 255) >> 3) * 2u) & mask;
            const uint16_t entry = (uint16_t)(c->bg_vram[addr] | (c->bg_vram[addr + 1] << 8));
            const uint32_t toff = (char_base + (entry & 0x3FFu) * tile_bytes) & mask;
            const int pbank = entry >> 12;
            uint32_t pal_id;
            kj_palette *pal;
            if (!bpp8) {
                pal = pal_4bpp(c->in, c->engine, 0, pbank, &pal_id);
            } else if (ext) {
                pal = pal_ext_bg(c->in, c->engine, ext_slot, pbank, &pal_id);
            } else {
                pal = pal_256(c->in, c->engine, 0, &pal_id);
            }
            const kj_tile_ref t = kj_atlas_tile(source, c->bg_vram + toff, toff, bpp8, pal, pal_id);
            /* Blank tiles still affect reuse if the game later writes pixels. */
            run_note(out, toff, tile_bytes);
            if (!t.valid) {
                out->incomplete = true;
                continue;
            }
            if (t.transparent) {
                continue;
            }
            if (out->count >= out->cap) {
                out->incomplete = true;
#ifndef HGSS_PROFILE_OFF
                ++s_stats.dropped_quads;
#endif
                continue;
            }
            if (out->slots != NULL) {
                out->slots[out->count] = t.slot;
            }
            put_quad(out->v + out->count * 4u, sx, sy, 8, 8, t.slot, (entry >> 10) & 1,
                     (entry >> 11) & 1);
            ++out->count;
        }
    }
    return out->count;
}

/* ---------------------------------------------------------- affine BGs */

/* Affine and extended-affine (16-bit entry) BGs: map tiles are placed by
 * the inverse of the BG's matrix; only tiles whose texture-space position
 * falls inside the screen's texture-space bounding box are emitted. */
static uint32_t affine_bg(const ctx *c, int bg, bool ext16, run *out)
{
    const unsigned base = bg == 2 ? 0x20 : 0x30;
    const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
    const float pa = (int16_t)r16(c->regs, base + 0) / 256.0f;
    const float pb = (int16_t)r16(c->regs, base + 2) / 256.0f;
    const float pc = (int16_t)r16(c->regs, base + 4) / 256.0f;
    const float pd = (int16_t)r16(c->regs, base + 6) / 256.0f;
    int32_t rx = (int32_t)(r32(c->regs, base + 8) << 4) >> 4;
    int32_t ry = (int32_t)(r32(c->regs, base + 12) << 4) >> 4;
    const float ox = rx / 256.0f, oy = ry / 256.0f;
    const int size = ext16 ? (128 << (cnt >> 14)) : (128 << (cnt >> 14));
    const bool wrap = (cnt & 0x2000) != 0;
    const bool bpp8 = true;
    /* Extended affine BG with bit 7 set: a direct-colour bitmap, 16 bits a
     * pixel, so there is no map and no character data. Bit 2 selects direct
     * over 256-colour with the same bit (eidos hwcompose.c:732 chooses
     * RAS_DIRECT on `cnt & 0x4`). The bitmap base is the screen-base field
     * in 16 KB units (NitroSDK gx_bgcnt.c G2_GetBG2ScrPtr, GX_BGMODE_5). */
    const bool direct = ext16 && (cnt & 0x84u) == 0x84u;
    if (direct) {
        ++unowa_direct_calls;
    }
    uint32_t bitmap_base = ((cnt >> 8) & 31u) * 0x4000u;
    if (c->engine == 0) {
        bitmap_base += ((c->dispcnt >> 27) & 7u) * 0x10000u;
    }
    uint32_t char_base = ((cnt >> 2) & 15u) * 0x4000u;
    uint32_t scr_base = ((cnt >> 8) & 31u) * 0x800u;
    if (c->engine == 0) {
        char_base += ((c->dispcnt >> 24) & 7u) * 0x10000u;
        scr_base += ((c->dispcnt >> 27) & 7u) * 0x10000u;
    }
    const bool ext = ext16 && (c->dispcnt & (1u << 30));
    const uint32_t mask = c->bg_vram_size - 1u;
    const float det = pa * pd - pb * pc;
    if (fabsf(det) < 1e-6f) {
        return 0;
    }
    /* screen = inv * (tex - origin) */
    const float ia = pd / det, ib = -pb / det, ic = -pc / det, id = pa / det;

    /* texture-space bounds of the span */
    float tminx = 1e9f, tmaxx = -1e9f, tminy = 1e9f, tmaxy = -1e9f;
    const float cx[4] = { 0, 256, 0, 256 }, cy[4] = { (float)c->y0, (float)c->y0, (float)c->y1,
                                                      (float)c->y1 };
    for (int i = 0; i < 4; ++i) {
        const float tx = ox + pa * cx[i] + pb * cy[i];
        const float ty = oy + pc * cx[i] + pd * cy[i];
        tminx = fminf(tminx, tx);
        tmaxx = fmaxf(tmaxx, tx);
        tminy = fminf(tminy, ty);
        tmaxy = fmaxf(tmaxy, ty);
    }
    int tx0 = (int)floorf(tminx / 8.0f) - 1, tx1 = (int)floorf(tmaxx / 8.0f) + 1;
    int ty0 = (int)floorf(tminy / 8.0f) - 1, ty1 = (int)floorf(tmaxy / 8.0f) + 1;
    const int tiles = size / 8;
    if (!wrap) {
        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        if (tx1 > tiles - 1) tx1 = tiles - 1;
        if (ty1 > tiles - 1) ty1 = tiles - 1;
    }
    if (tx1 < tx0 || ty1 < ty0) {
        return 0;
    }
    uint32_t n = (uint32_t)((tx1 - tx0 + 1) * (ty1 - ty0 + 1));
    if (direct) {
        unowa_direct_cells_n = n;
        unowa_direct_box[0] = tx0;
        unowa_direct_box[1] = tx1;
        unowa_direct_box[2] = ty0;
        unowa_direct_box[3] = ty1;
    }
    if (n > 8192u) {
        if (direct) {
            ++unowa_direct_nofit;
        }
#ifndef HGSS_PROFILE_OFF
        ++s_stats.unsupported_modes;
#endif
        return 0;
    }
    if (!run_begin(out, n)) {
        if (direct) {
            ++unowa_direct_norun;
        }
        return 0;
    }
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const int mtx = ((tx % tiles) + tiles) % tiles;
            const int mty = ((ty % tiles) + tiles) % tiles;
            uint32_t tile;
            bool hf = false, vf = false;
            int pbank = 0;
            kj_tile_ref t;
            if (direct) {
                /* 16-bit direct-colour bitmap: the cell is 8x8 pixels in a
                 * linear `size`-wide BGR555 image, not an 8bpp tile. */
                const uint32_t cell =
                    (bitmap_base + ((uint32_t)mty * 8u * (uint32_t)size + (uint32_t)mtx * 8u) * 2u) &
                    mask;
                t = kj_atlas_direct((uint32_t)(c->engine * 2), c->bg_vram, cell,
                                    (uint32_t)size * 2u);
            } else {
                if (ext16) {
                    const uint32_t a = (scr_base + (uint32_t)(mty * tiles + mtx) * 2u) & mask;
                    const uint16_t e = (uint16_t)(c->bg_vram[a] | (c->bg_vram[a + 1] << 8));
                    tile = e & 0x3FFu;
                    hf = (e >> 10) & 1;
                    vf = (e >> 11) & 1;
                    pbank = e >> 12;
                } else {
                    tile = c->bg_vram[(scr_base + (uint32_t)(mty * tiles + mtx)) & mask];
                }
                const uint32_t toff = (char_base + tile * 64u) & mask;
                uint32_t pal_id;
                kj_palette *pal = ext ? pal_ext_bg(c->in, c->engine, bg, pbank, &pal_id)
                                      : pal_256(c->in, c->engine, 0, &pal_id);
                t = kj_atlas_tile((uint32_t)(c->engine * 2), c->bg_vram + toff, toff, bpp8, pal,
                                  pal_id);
            }
            if (!t.valid || t.transparent) {
                if (direct) {
                    ++unowa_direct_clear;
                }
                continue;
            }
            if (direct) {
                ++unowa_direct_cells;
            }
            /* corners in texture space -> screen */
            kj_v2d *v = out->v + out->count * 4u;
            int16_t u0, v0, u1, v1;
            kj_atlas_slot_uv(t.slot, &u0, &v0, &u1, &v1);
            if (hf) { const int16_t s = u0; u0 = u1; u1 = s; }
            if (vf) { const int16_t s = v0; v0 = v1; v1 = s; }
            const float px[4] = { tx * 8.0f, tx * 8.0f + 8, tx * 8.0f, tx * 8.0f + 8 };
            const float py[4] = { ty * 8.0f, ty * 8.0f, ty * 8.0f + 8, ty * 8.0f + 8 };
            const int16_t uu[4] = { u0, u1, u0, u1 }, vv[4] = { v0, v0, v1, v1 };
            for (int k = 0; k < 4; ++k) {
                const float dx = px[k] - ox, dy = py[k] - oy;
                v[k].x = (int16_t)lroundf(ia * dx + ib * dy);
                v[k].y = (int16_t)lroundf(ic * dx + id * dy);
                v[k].u = uu[k];
                v[k].v = vv[k];
                v[k].r = v[k].g = v[k].b = v[k].a = 255;
            }
            ++out->count;
        }
    }
    return out->count;
}

/* ---------------------------------------------------------------- OBJs */

static const uint8_t OBJ_W[4][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 }, { 0 } };
static const uint8_t OBJ_H[4][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 }, { 0 } };

/* Emit the OBJs of one priority and blend kind (semi = OBJ mode 1). */
static uint32_t objs(const ctx *c, int prio, bool semi, run *out)
{
    const uint8_t *oam = c->in->mem->oam + c->engine * 0x400;
    const bool map1d = (c->dispcnt & 0x10) != 0;
    const uint32_t boundary = 32u << ((c->dispcnt >> 20) & 3u);
    const bool ext = (c->dispcnt & (1u << 31)) != 0;
    const uint32_t mask = c->obj_vram_size - 1u;
    const uint32_t source = (uint32_t)(c->engine * 2 + 1);

    if (!run_begin(out, 2048)) {
        return 0;
    }
    if (!out->ext) {
        out->cap = 2048;
    }
    for (int i = 127; i >= 0; --i) {
        const uint16_t a0 = r16(oam, (unsigned)i * 8u);
        const uint16_t a1 = r16(oam, (unsigned)i * 8u + 2u);
        const uint16_t a2 = r16(oam, (unsigned)i * 8u + 4u);
        const bool affine = (a0 & 0x100) != 0;
        if (!affine && (a0 & 0x200)) {
            continue;
        }
        const int mode = (a0 >> 10) & 3;
        if (prio < 0) {
            if (mode != 2) {
                continue;
            }
        } else if (((a2 >> 10) & 3) != prio || mode == 2) {
            continue;
        } else if (mode == 3) {
#ifndef HGSS_PROFILE_OFF
            ++s_stats.unsupported_modes; /* bitmap OBJ */
#endif
            continue;
        } else if ((mode == 1) != semi) {
            continue;
        }
        const int shape = a0 >> 14, sz = a1 >> 14;
        const int w = OBJ_W[shape][sz], h = OBJ_H[shape][sz];
        if (w == 0) {
            continue;
        }
        int y = a0 & 0xFF;
        int x = a1 & 0x1FF;
        if (x >= 256) {
            x -= 512;
        }
        const bool dbl = affine && (a0 & 0x200);
        const int bw = dbl ? w * 2 : w, bh = dbl ? h * 2 : h;
        if (y + bh > 256) {
            y -= 256;
        }
        if (y >= c->y1 || y + bh <= c->y0 || x >= 256 || x + bw <= 0) {
            continue;
        }
        const bool bpp8 = (a0 & 0x2000) != 0;
        const uint32_t tile_bytes = bpp8 ? 64u : 32u;
        const uint32_t tnum = a2 & 0x3FFu;
        const int pbank = a2 >> 12;
        uint32_t pal_id;
        kj_palette *pal = !bpp8 ? pal_4bpp(c->in, c->engine, 1, pbank, &pal_id)
                          : ext ? pal_ext_obj(c->in, c->engine, pbank, &pal_id)
                                : pal_256(c->in, c->engine, 1, &pal_id);
        const int tw = w / 8, th = h / 8;
        bool hf = false, vf = false;
        float ia = 1, ib = 0, ic = 0, id = 1;
        if (affine) {
            const unsigned g = (a1 >> 9) & 31u;
            const float pa = (int16_t)r16(oam, g * 32u + 6u) / 256.0f;
            const float pb = (int16_t)r16(oam, g * 32u + 14u) / 256.0f;
            const float pc = (int16_t)r16(oam, g * 32u + 22u) / 256.0f;
            const float pd = (int16_t)r16(oam, g * 32u + 30u) / 256.0f;
            const float det = pa * pd - pb * pc;
            if (fabsf(det) < 1e-6f) {
                continue;
            }
            ia = pd / det;
            ib = -pb / det;
            ic = -pc / det;
            id = pa / det;
        } else {
            hf = (a1 >> 12) & 1;
            vf = (a1 >> 13) & 1;
        }
        if (out->count + (uint32_t)(tw * th) > out->cap) {
            out->incomplete = true;
#ifndef HGSS_PROFILE_OFF
            ++s_stats.dropped_quads;
#endif
            continue;
        }
        const float cxs = x + bw / 2.0f, cys = y + bh / 2.0f;
        for (int ty = 0; ty < th; ++ty) {
            for (int tx = 0; tx < tw; ++tx) {
                uint32_t off;
                if (map1d) {
                    off = tnum * boundary + (uint32_t)(ty * tw + tx) * tile_bytes;
                } else {
                    off = tnum * 32u + (uint32_t)ty * 1024u + (uint32_t)tx * tile_bytes;
                }
                off &= mask;
                const kj_tile_ref t =
                    kj_atlas_tile(source, c->obj_vram + off, off, bpp8, pal, pal_id);
                run_note(out, off, tile_bytes);
                if (!t.valid) {
                    out->incomplete = true;
                    continue;
                }
                if (t.transparent) {
                    continue;
                }
                if (out->slots != NULL) {
                    out->slots[out->count] = t.slot;
                }
                kj_v2d *v = out->v + out->count * 4u;
                if (!affine) {
                    const int qx = x + (hf ? (tw - 1 - tx) : tx) * 8;
                    const int qy = y + (vf ? (th - 1 - ty) : ty) * 8;
                    put_quad(v, qx, qy, 8, 8, t.slot, hf, vf);
                } else {
                    int16_t u0, v0, u1, v1;
                    kj_atlas_slot_uv(t.slot, &u0, &v0, &u1, &v1);
                    const float px[4] = { tx * 8.0f, tx * 8.0f + 8, tx * 8.0f, tx * 8.0f + 8 };
                    const float py[4] = { ty * 8.0f, ty * 8.0f, ty * 8.0f + 8, ty * 8.0f + 8 };
                    const int16_t uu[4] = { u0, u1, u0, u1 };
                    const int16_t vv[4] = { v0, v0, v1, v1 };
                    for (int k = 0; k < 4; ++k) {
                        const float dx = px[k] - w / 2.0f, dy = py[k] - h / 2.0f;
                        v[k].x = (int16_t)lroundf(cxs + ia * dx + ib * dy);
                        v[k].y = (int16_t)lroundf(cys + ic * dx + id * dy);
                        v[k].u = uu[k];
                        v[k].v = vv[k];
                        v[k].r = v[k].g = v[k].b = v[k].a = 255;
                    }
                }
                ++out->count;
            }
        }
    }
    return out->count;
}

/* ------------------------------------------------------------- passes */

static void set_xform(const ctx *c)
{
    /* clip.x = 1 - y_phys/120 ; clip.y = 1 - x_phys/(w/2)
     * y_phys = y * sy + oy ; x_phys = x * sx + ox */
    const float half_w = c->target->width / 2.0f;
    C3D_FVUnifSet(GPU_VERTEX_SHADER, kj_gpu_uloc_2d_xform(), -c->sy / 120.0f,
                  1.0f - c->oy / 120.0f, -c->sx / half_w, 1.0f - c->ox / half_w);
    kj_gpu_set_2d_uvscale(KJ_ATLAS_SIZE, KJ_ATLAS_SIZE);
}

/* The physical rectangle this span draws in: the view's clip, narrowed to
 * the span's own DS lines. */
static void scissor_span(const ctx *c)
{
    int y0 = (int)(c->y0 * c->sy + c->oy);
    int y1 = (int)(c->y1 * c->sy + c->oy);
    if (y0 < c->clip_y0) {
        y0 = c->clip_y0;
    }
    if (y1 > c->clip_y1) {
        y1 = c->clip_y1;
    }
    if (y1 < y0) {
        y1 = y0;
    }
    kj_gpu_scissor(c->target->width, c->clip_x0, y0, c->clip_x1, y1);
}

static void scissor_clip(const ctx *c)
{
    kj_gpu_scissor(c->target->width, c->clip_x0, c->clip_y0, c->clip_x1, c->clip_y1);
}

typedef struct fx {
    uint16_t bldcnt;
    int mode;
    float eva, evb, evy;
    bool windows;
    bool objwin;
    uint8_t region_enable[4]; /* bits 0-5: BG0-3, OBJ, effects */
    uint8_t active;           /* bit r: region r can occur (outside always) */
} fx;

static void read_fx(const ctx *c, fx *f)
{
    f->bldcnt = r16(c->regs, 0x50);
    f->mode = (f->bldcnt >> 6) & 3;
    const uint16_t alpha = r16(c->regs, 0x52);
    const int eva = alpha & 31, evb = (alpha >> 8) & 31;
    const int evy = r16(c->regs, 0x54) & 31;
    f->eva = (eva > 16 ? 16 : eva) / 16.0f;
    f->evb = (evb > 16 ? 16 : evb) / 16.0f;
    f->evy = (evy > 16 ? 16 : evy) / 16.0f;
    /* WIN0, WIN1, OBJ window (DISPCNT 13-15); the OBJ window also needs
     * the OBJ layer on (DISPCNT 12). */
    f->windows = (c->dispcnt & (7u << 13)) != 0;
    f->objwin = (c->dispcnt & (1u << 15)) && (c->dispcnt & (1u << 12));
    const uint16_t winin = r16(c->regs, 0x48), winout = r16(c->regs, 0x4A);
    f->region_enable[0] = f->windows ? (uint8_t)(winout & 0x3F) : 0x3F;
    f->region_enable[1] = (uint8_t)(winin & 0x3F);
    f->region_enable[2] = (uint8_t)((winin >> 8) & 0x3F);
    f->region_enable[3] = (uint8_t)((winout >> 8) & 0x3F);
    f->active = (uint8_t)(1u | ((c->dispcnt >> 12) & 6u) | (f->objwin ? 8u : 0u));
}

/* The window regions BG0 (the 3D layer) shows in, as a bit per region for
 * kj_g3d_draw; regions that cannot occur are left to `active`. */
static uint8_t bg0_window_on(const fx *f)
{
    uint8_t on = 0;
    for (int r = 0; r < 4; ++r) {
        if (f->region_enable[r] & 1u) {
            on |= (uint8_t)(1u << r);
        }
    }
    return on;
}

static void stencil_write_all(uint8_t ref, uint8_t test_mask)
{
    C3D_StencilTest(true, test_mask ? GPU_EQUAL : GPU_ALWAYS, ref, test_mask, 0x03);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
}

static void blend_off(void)
{
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
}

static void blend_alpha(float eva, float evb)
{
    const uint32_t ka = (uint32_t)(eva * 255.0f + 0.5f), kb = (uint32_t)(evb * 255.0f + 0.5f);
    C3D_BlendingColor(ka | (ka << 8) | (ka << 16) | (kb << 24));
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_COLOR, GPU_CONSTANT_ALPHA,
                   GPU_ONE, GPU_ZERO);
}

/* Draw quads of one layer in every window region where it is enabled. */
typedef struct drawable {
    const kj_v2d *base; /* NULL: frame arena at `first` */
    uint32_t first;
    uint32_t count;
} drawable;

static void draw_quads(const drawable *d)
{
    if (d->base != NULL) {
        kj_gpu_draw_quads_from(d->base, d->count);
    } else {
        kj_gpu_draw_quads(d->first, d->count);
    }
}

static void restore_2d_state(const ctx *c, int sx0);

/*
 * A layer's band of its picture (the wide art), drawn over the layer's own
 * pixels: the picture's texture is bound instead of the atlas's, and the
 * scissor has to let the band in (it is outside the DS's picture).
 */
static void draw_band(const ctx *c, const drawable *band)
{
    if (band == NULL || band->count == 0 || kj_wideart_texture() == NULL) {
        return;
    }
    kj_gpu_bind_texture(0, kj_wideart_texture());
    kj_gpu_set_2d_uvscale(kj_wideart_texture_width(), kj_wideart_texture_height());
    scissor_span(c);
    draw_quads(band);
#ifdef KJ_WIDEART_DEBUG
    {
        /* TEMPORARY: the wide art's texture over the whole canvas, to see
         * whether the texture reaches the screen at all. */
        uint32_t first;
        kj_v2d *v = kj_gpu_quads_alloc(1, &first);
        if (v != NULL) {
            v[0] = (kj_v2d){ -32, 0, 1, 1, 255, 255, 255, 255 };
            v[1] = (kj_v2d){ 288, 0, (int16_t)(512 * KJ_UV_UNIT - 1), 1, 255, 255, 255, 255 };
            v[2] = (kj_v2d){ -32, 192, 1, (int16_t)(192 * KJ_UV_UNIT - 1), 255, 255, 255, 255 };
            v[3] = (kj_v2d){ 288, 192, (int16_t)(512 * KJ_UV_UNIT - 1),
                             (int16_t)(192 * KJ_UV_UNIT - 1), 255, 255, 255, 255 };
            kj_gpu_draw_quads(first, 1);
        }
    }
#endif
    restore_2d_state(c, (int)(c->target->offset_x));
}

static void draw_layer(const ctx *c, const fx *f, int layer, const drawable *d, bool force_semi,
                       const drawable *band, const drawable *edge)
{
    const uint32_t count = d->count;
    if (count == 0 && (band == NULL || band->count == 0) && (edge == NULL || edge->count == 0)) {
        return;
    }
    const bool t2 = (f->bldcnt >> (8 + layer)) & 1;
    const bool t1 = force_semi || ((f->bldcnt >> layer) & 1);
    for (int r = 0; r < 4; ++r) {
        if (r > 0 && !f->windows) {
            break;
        }
        if (r == 3 && !f->objwin) {
            break;
        }
        const uint8_t en = f->region_enable[r];
        if (!(en & (1u << layer))) {
            continue;
        }
        const bool effects = (en & 0x20) != 0 || force_semi;
        const uint8_t region = (uint8_t)(f->windows ? (r << 4) : 0);
        const uint8_t rmask = f->windows ? 0x30 : 0x00;
        const bool alpha_blend = t1 && effects && (force_semi || f->mode == 1);
        /* EVA 0 with EVB 1 means the layer contributes nothing (the DS shows
         * the 2nd target or the backdrop): drawing it would only write the
         * same pixels again, and skipping keeps the 2nd target intact. */
        if (alpha_blend && !force_semi && f->eva <= 0.0f && f->evb >= 1.0f) {
            continue;
        }
        if (!alpha_blend) {
            const uint8_t flags = (uint8_t)((t2 ? 1 : 0) | (t1 && effects && f->mode >= 2 ? 2 : 0));
            blend_off();
            stencil_write_all((uint8_t)(region | flags), rmask);
            if (edge != NULL) {
                draw_quads(edge);
            }
            draw_quads(d);
            draw_band(c, band);
#ifndef HGSS_PROFILE_OFF
            ++s_stats.draws;
#endif
            continue;
        }
        /* The pass that changes bit 0 runs last, so its pixels cannot be
         * redrawn by the other pass. The opaque pass changes it when the
         * current layer is a 2nd target; otherwise the blended pass does. */
        for (int pass = 0; pass < 2; ++pass) {
            const bool blended = (pass == 0) == t2;
            if (blended) {
                blend_alpha(f->eva, f->evb);
                C3D_StencilTest(true, GPU_EQUAL, region | 1u, rmask | 1u, 0x03);
                C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP,
                              t2 ? GPU_STENCIL_KEEP : GPU_STENCIL_INVERT);
            } else {
                blend_off();
                C3D_StencilTest(true, GPU_EQUAL, region, rmask | 1u, 0x01);
                C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP,
                              t2 ? GPU_STENCIL_INVERT : GPU_STENCIL_KEEP);
            }
            if (edge != NULL) {
                draw_quads(edge);
            }
            draw_quads(d);
            draw_band(c, band);
        }
#ifndef HGSS_PROFILE_OFF
        s_stats.draws += 2;
#endif
    }
}

/* A rectangle (DS pixels) as one untextured quad in the arena. */
static bool rect_quad(int x0, int y0, int x1, int y1, uint32_t rgba, uint32_t *first){
    kj_v2d *v = kj_gpu_quads_alloc(1, first);
    if (v == NULL) {
        return false;
    }
    const uint8_t r = rgba & 255, g = (rgba >> 8) & 255, b = (rgba >> 16) & 255, a = rgba >> 24;
    v[0] = (kj_v2d){ (int16_t)x0, (int16_t)y0, 0, 0, r, g, b, a };
    v[1] = (kj_v2d){ (int16_t)x1, (int16_t)y0, 0, 0, r, g, b, a };
    v[2] = (kj_v2d){ (int16_t)x0, (int16_t)y1, 0, 0, r, g, b, a };
    v[3] = (kj_v2d){ (int16_t)x1, (int16_t)y1, 0, 0, r, g, b, a };
    return true;
}

/* Window rectangles of a span into stencil bits 4-5. WIN1 first, then
 * WIN0 (WIN0 has priority). Coordinates wrap when x1 > x2 / y1 > y2. */
static void write_windows(const ctx *c)
{
    kj_gpu_tev_vertex_color();
    C3D_DepthTest(false, GPU_ALWAYS, 0); /* stencil only: no colour or depth writes */
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    for (int w = 1; w >= 0; --w) {
        if (!(c->dispcnt & (1u << (13 + w)))) {
            continue;
        }
        const uint16_t hreg = r16(c->regs, 0x40 + 2 * w), vreg = r16(c->regs, 0x44 + 2 * w);
        int x1 = hreg >> 8, x2 = hreg & 255, y1 = vreg >> 8, y2 = vreg & 255;
        if (x2 == 0 && x1 != 0) {
            x2 = 256;
        }
        if (y2 > 192 || (y2 == 0 && y1 != 0)) {
            y2 = 192;
        }
        int xs[4][2], ys[2][2], nx = 0, ny = 0;
        if (x1 <= x2) { xs[nx][0] = x1; xs[nx++][1] = x2; }
        else { xs[nx][0] = x1; xs[nx++][1] = 256; xs[nx][0] = 0; xs[nx++][1] = x2; }
        if (y1 <= y2) { ys[ny][0] = y1; ys[ny++][1] = y2; }
        else { ys[ny][0] = y1; ys[ny++][1] = 192; ys[ny][0] = 0; ys[ny++][1] = y2; }
        /* Wide top screen: a window that reaches a DS screen edge runs on
         * across the side columns, so wipes and covers closing in from the
         * edges hide the widened picture too. The side columns get their
         * own rectangles; the DS columns keep exactly the registers'
         * window, except that a right edge of 255 counts as reaching the
         * edge: Platinum's full-width windows (encounter_effect.c) end at
         * 255, leaving DS column 255 outside - the DS screen's last column,
         * but mid-picture once widened - so that column joins the side. */
        if (c->screen == KJ_SCREEN_TOP) {
            const int n = nx;
            for (int i = 0; i < n; ++i) {
                if (xs[i][0] >= xs[i][1]) {
                    continue;
                }
                if (xs[i][0] == 0) {
                    xs[nx][0] = -64;
                    xs[nx++][1] = 0;
                }
                if (xs[i][1] >= 255) {
                    xs[i][1] = 256;
                    xs[nx][0] = 256;
                    xs[nx++][1] = 320;
                }
            }
#ifndef HGSS_PROFILE_OFF
            s_stats.wide_windows += (uint32_t)(nx - n);
#endif
        }
        C3D_StencilTest(true, GPU_ALWAYS, (u8)((w + 1) << 4), 0, 0x30);
        C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                uint32_t q;
                if (rect_quad(xs[i][0], ys[j][0], xs[i][1], ys[j][1], 0, &q)) {
                    kj_gpu_draw_quads(q, 1);
                }
            }
        }
    }
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(true, GPU_GREATER, 0);
}

/* BLDY: pixels whose top layer is an effect-enabled 1st target. */
static void brightness_pass(const fx *f)
{
    if (f->mode < 2 || f->evy <= 0.0f) {
        return;
    }
    const uint32_t k = (uint32_t)(f->evy * 255.0f + 0.5f);
    const uint32_t color = f->mode == 2 ? 0xFFFFFFFFu : 0xFF000000u;
    uint32_t q;
    if (!rect_quad(-64, 0, 320, 192, color, &q)) {
        return;
    }
    kj_gpu_tev_vertex_color();
    C3D_BlendingColor(k << 24);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA,
                   GPU_ZERO, GPU_ONE);
    C3D_StencilTest(true, GPU_EQUAL, 2, 2, 0);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    kj_gpu_draw_quads(q, 1);
#ifndef HGSS_PROFILE_OFF
    ++s_stats.draws;
#endif
}

/* MASTER_BRIGHT over the whole physical screen (sides included). */
static void master_brightness(const ctx *c, uint16_t mb)
{
    const int mode = mb >> 14;
    int factor = mb & 31;
    if (factor > 16) {
        factor = 16;
    }
    if ((mode != 1 && mode != 2) || factor == 0) {
        return;
    }
    const uint32_t k = (uint32_t)(factor * 255 / 16);
    uint32_t q;
    if (!rect_quad(-64, 0, 320, 192, mode == 1 ? 0xFFFFFFFFu : 0xFF000000u, &q)) {
        return;
    }
    if (c->full) {
        kj_gpu_scissor_off();
    } else {
        scissor_clip(c);
    }
    kj_gpu_tev_vertex_color();
    C3D_BlendingColor(k << 24);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA,
                   GPU_ZERO, GPU_ONE);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    kj_gpu_draw_quads(q, 1);
#ifndef HGSS_PROFILE_OFF
    ++s_stats.draws;
#endif
}

/* --------------------------------------------------------------- spans */

/* Lines [y0, y1) whose registers equal line y0's (the bytes the 2D
 * renderer reads: 0x00-0x6F except DISPSTAT/VCOUNT, already zeroed). */
static int span_end(const kj_line_regs *lines, int engine, int y0)
{
    if (!lines->active) {
        return 192;
    }
    const uint8_t *a = lines->regs[engine][y0];
    int y = y0 + 1;
    while (y < 192 && memcmp(lines->regs[engine][y], a, KJ_LINE_REG_BYTES) == 0) {
        ++y;
    }
    return y;
}

/* ------------------------------------------------------------- art keys */

static const uint8_t *text_ext_src(const ctx *c, int bg);

/* The extended palette slot a BG's art key reads (BG0 and BG1 can take slot
 * 2 or 3, BGxCNT bit 13; the others their own). */
static const uint8_t *bg_art_ext(const ctx *c, int bg)
{
    const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
    const int slot = bg < 2 ? bg + (((cnt >> 13) & 1) ? 2 : 0) : bg;
    return ext_bg_slot(c->in, c->engine, slot);
}

/*
 * A layer's art key: a hash of the layer's own art - its map, the tiles the
 * map uses, their palettes - not of where the game put it or how it is
 * scrolled. The wide art's pictures are named by it (a picture shows
 * whenever its layer does), so the hash has to be the one they were named
 * with: pokeplatinum-3ds's BgKey, so the pictures in wideart/ are found.
 * That is FNV-1a over the same words, in the same order.
 */

#define ART_KEY_START 0xcbf29ce484222325ull

/* The kinds, as the pictures' hash knows them. */
enum {
    ART_NONE = 0,
    ART_TEXT = 1,
    ART_AFFINE = 2,
    ART_EXTENDED = 3
};

static uint64_t art_key_add(uint64_t h, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        h = (h ^ (v & 0xFFu)) * 0x100000001b3ull;
        v >>= 8;
    }
    return h;
}

static uint16_t pltt_color(const uint8_t *pltt, uint32_t index)
{
    return (uint16_t)(r16(pltt, index * 2u) & 0x7FFFu);
}

static const int s_text_sizes[4][2] = { { 256, 256 }, { 512, 256 }, { 256, 512 }, { 512, 512 } };

/* The key of a BG's art, or 0 for a layer with none (a bitmap, the 3D). */
static uint64_t bg_art_key(const ctx *c, int bg)
{
    const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
    const uint32_t dispcnt = c->dispcnt;
    const int mode = (int)(dispcnt & 7u);
    int kind;

    if (bg == 0 && c->engine == 0 && (dispcnt & 8u)) {
        return 0; /* the 3D layer */
    }
    switch (mode) {
    case 0: kind = ART_TEXT; break;
    case 1: kind = bg < 3 ? ART_TEXT : ART_AFFINE; break;
    case 2: kind = bg < 2 ? ART_TEXT : ART_AFFINE; break;
    case 3: kind = bg < 3 ? ART_TEXT : ART_EXTENDED; break;
    case 4: kind = bg < 2 ? ART_TEXT : (bg == 2 ? ART_AFFINE : ART_EXTENDED); break;
    case 5: kind = bg < 2 ? ART_TEXT : ART_EXTENDED; break;
    default: return 0;
    }

    /* The kind decides the tile size and whether the map's entries are 16
     * bits wide, exactly as the pictures' hash has it (an affine BG is 8
     * bits a pixel whatever its CNT says; an extended BG with 8 bits a
     * pixel is a bitmap and has no key). */
    bool bpp8, wide16;
    uint32_t entries;
    if (kind == ART_TEXT) {
        bpp8 = (cnt & 0x80u) != 0;
        wide16 = true;
        entries = (uint32_t)(s_text_sizes[cnt >> 14][0] / 8) * (uint32_t)(s_text_sizes[cnt >> 14][1] / 8);
    } else if (kind == ART_AFFINE || (kind == ART_EXTENDED && !(cnt & 0x80u))) {
        bpp8 = true;
        wide16 = kind == ART_EXTENDED;
        const uint32_t side = (128u << (cnt >> 14)) / 8u;
        entries = side * side;
    } else {
        return 0; /* a bitmap */
    }

    const uint32_t mask = c->bg_vram_size - 1u;
    uint32_t char_base = ((cnt >> 2) & 15u) * 0x4000u;
    uint32_t screen_base = ((cnt >> 8) & 31u) * 0x800u;
    if (c->engine == 0) {
        char_base += ((dispcnt >> 24) & 7u) * 0x10000u;
        screen_base += ((dispcnt >> 27) & 7u) * 0x10000u;
    }

    uint64_t h = ART_KEY_START;
    h = art_key_add(h, (uint32_t)kind | ((uint32_t)(cnt & 0xC080u) << 8));
    uint32_t used[1024 / 32];
    uint32_t banks = 0;
    memset(used, 0, sizeof(used));
    for (uint32_t i = 0; i < entries; ++i) {
        const uint32_t entry = wide16 ? r16(c->bg_vram, (screen_base + i * 2u) & mask)
                                      : c->bg_vram[(screen_base + i) & mask];
        const uint32_t tile = wide16 ? entry & 0x3FFu : entry;
        h = art_key_add(h, entry);
        used[tile >> 5] |= 1u << (tile & 31u);
        if (wide16) {
            banks |= 1u << (entry >> 12);
        }
    }
    const uint32_t tile_bytes = bpp8 ? 64u : 32u;
    for (uint32_t i = 0; i < 1024; ++i) {
        if (!(used[i >> 5] & (1u << (i & 31u)))) {
            continue;
        }
        for (uint32_t k = 0; k < tile_bytes; k += 4) {
            h = art_key_add(h, r32(c->bg_vram, (char_base + i * tile_bytes + k) & mask));
        }
    }
    const uint8_t *bg_pltt = (const uint8_t *)pltt_ptr(c->in->mem, c->engine, 0);
    if (!bpp8) {
        for (uint32_t i = 0; i < 16; ++i) {
            if (banks & (1u << i)) {
                for (uint32_t k = 0; k < 16; ++k) {
                    h = art_key_add(h, pltt_color(bg_pltt, i * 16u + k));
                }
            }
        }
    } else {
        const uint8_t *ext = (dispcnt & (1u << 30)) && wide16 ? bg_art_ext(c, bg) : NULL;
        if (ext != NULL) {
            for (uint32_t i = 0; i < 16; ++i) {
                if (banks & (1u << i)) {
                    for (uint32_t k = 0; k < 256; ++k) {
                        h = art_key_add(h, pltt_color(ext + (i << 9), k));
                    }
                }
            }
        } else {
            for (uint32_t i = 0; i < 256; ++i) {
                h = art_key_add(h, pltt_color(bg_pltt, i));
            }
        }
    }
    return h != 0 ? h : 1;
}

/* -------------------------------------------------------------- bands */

/*
 * The band's fallback when no picture covers it: the layer's outermost
 * column carried on, pokeplatinum-3ds's "a plain edge goes on" (REMASTER.md,
 * The wider screen). The edge tile of each 8-line row is sampled at the
 * screen's edge column and stretched across the 32-pixel band, so the band
 * continues the picture's border, window frame or fade instead of showing
 * the backdrop. Atlas work only: no readback, no per-pixel CPU path.
 */
static void edge_band_quads(const ctx *c, int bg, kj_v2d *v, uint32_t cap, uint32_t *count)
{
    *count = 0;
    if (c->screen != KJ_SCREEN_TOP || bg < 0 || bg > 3) {
        return;
    }
    const int mode = (int)(c->dispcnt & 7u);
    bool text = false;
    switch (mode) {
    case 0: text = true; break;
    case 1: text = bg < 3; break;
    case 2: text = bg < 2; break;
    case 3: text = bg < 3; break;
    case 4: text = bg < 2; break;
    case 5: text = bg < 2; break;
    default: break;
    }
    if (!text) {
        return; /* affine and extended maps: the map's own placement decides */
    }
    const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
    const int hofs = r16(c->regs, 0x10 + 4 * bg) & 0x1FF;
    const int vofs = r16(c->regs, 0x12 + 4 * bg) & 0x1FF;
    const int size = cnt >> 14;
    const int w = (size & 1) ? 512 : 256;
    const int h = (size & 2) ? 512 : 256;
    const bool bpp8 = (cnt & 0x80) != 0;
    uint32_t char_base = ((cnt >> 2) & 15u) * 0x4000u;
    uint32_t scr_base = ((cnt >> 8) & 31u) * 0x800u;
    if (c->engine == 0) {
        char_base += ((c->dispcnt >> 24) & 7u) * 0x10000u;
        scr_base += ((c->dispcnt >> 27) & 7u) * 0x10000u;
    }
    const bool ext = bpp8 && (c->dispcnt & (1u << 30));
    const int ext_slot = bg < 2 ? bg + (((cnt >> 13) & 1) ? 2 : 0) : bg;
    const uint32_t tile_bytes = bpp8 ? 64u : 32u;
    const uint32_t mask = c->bg_vram_size - 1u;
    const uint32_t source = (uint32_t)(c->engine * 2);
    const int sy0 = c->y0 & ~7;
    const int sy1 = (c->y1 + 7) & ~7;
    const uint32_t rows = (uint32_t)((sy1 - sy0 + 7) / 8);
    if (rows == 0) {
        return;
    }
    if (rows * 2u > cap) {
        return;
    }
    uint32_t n = 0;
    for (int sy = sy0; sy < sy1; sy += 8) {
        const int my = (sy + vofs) & (h - 1);
        const uint32_t row_base = scr_base + (uint32_t)((my >> 8) * (w >> 8)) * 0x800u +
                                  (uint32_t)((my & 255) >> 3) * 64u;
        for (int side = 0; side < 2; ++side) {
            const int mx = ((side ? 248 : 0) + hofs) & (w - 1);
            const uint32_t addr = (row_base + (uint32_t)(mx >> 8) * 0x800u +
                                   (uint32_t)((mx & 255) >> 3) * 2u) & mask;
            const uint16_t entry = (uint16_t)(c->bg_vram[addr] | (c->bg_vram[addr + 1] << 8));
            const uint32_t toff = (char_base + (entry & 0x3FFu) * tile_bytes) & mask;
            const int pbank = entry >> 12;
            uint32_t pal_id;
            kj_palette *pal;
            if (!bpp8) {
                pal = pal_4bpp(c->in, c->engine, 0, pbank, &pal_id);
            } else if (ext) {
                pal = pal_ext_bg(c->in, c->engine, ext_slot, pbank, &pal_id);
            } else {
                pal = pal_256(c->in, c->engine, 0, &pal_id);
            }
            const kj_tile_ref t =
                kj_atlas_tile(source, c->bg_vram + toff, toff, bpp8, pal, pal_id);
            if (!t.valid || t.transparent) {
                continue;
            }
            int16_t u0, v0, u1, v1;
            kj_atlas_slot_uv(t.slot, &u0, &v0, &u1, &v1);
            const bool hf = ((entry >> 10) & 1) != 0;
            const bool vf = ((entry >> 11) & 1) != 0;
            /* the screen's edge column inside the tile (flips considered) */
            const int col = side ? (hf ? 0 : 7) : (hf ? 7 : 0);
            const int16_t u = (int16_t)(u0 + col * 16 + 8);
            const int16_t vt = (int16_t)(v0 + (vf ? 7 * 16 : 0) + 8);
            const int16_t vb = (int16_t)(v0 + (vf ? 0 : 7 * 16) + 8);
            const int x = side ? 256 : -32;
            kj_v2d *q = v + n * 4u;
            q[0] = (kj_v2d){ (int16_t)x, (int16_t)sy, u, vt, 255, 255, 255, 255 };
            q[1] = (kj_v2d){ (int16_t)(x + 32), (int16_t)sy, u, vt, 255, 255, 255, 255 };
            q[2] = (kj_v2d){ (int16_t)x, (int16_t)(sy + 8), u, vb, 255, 255, 255, 255 };
            q[3] = (kj_v2d){ (int16_t)(x + 32), (int16_t)(sy + 8), u, vb, 255, 255, 255, 255 };
            ++n;
        }
    }
    *count = n;
}

/* ---------------------------------------------------------- the wide art */

/*
 * The wide art: the hand-drawn bands beside the DS's picture (wideart.h).
 *
 * The remaster's upper screen is 32 DS pixels wider on each side than the
 * DS's picture, and the game draws nothing there. A layer that has a
 * picture shows it: the picture's own bands, moved with the layer's scroll.
 * The picture is found by the layer's art key (bg_art_key), so it shows
 * whenever that layer does, on any visit to that screen.
 *
 * The picture's middle (the DS's own picture, 256 pixels of it) is not
 * drawn: the layer's own tiles are there already, and where the picture
 * says "the game's own" (its marker) the texel is transparent, so the
 * layer's own pixel shows through it. What the game draws past the DS's
 * edge (a map going on) is not drawn into the band: that is the port's own
 * rule's work (docs/PORTING.md, the wide screen), and it is not done yet.
 *
 * The wide canvas: 320 DS pixels, the DS's picture at x 32..287, the bands
 * at 0..31 and 288..319.
 */

#define WIDE_CANVAS_W 320
#define WIDE_BAND_W 32
#define WIDE_ORIGIN 32 /* the DS's picture starts here */

/* How far the layer may have scrolled from where the picture was made and
 * still show its bands (pokeplatinum-3ds's ART_REST). */
#define ART_REST 8

/* The wide art is only on the upper screen, and only if the build made
 * any pictures. */
static bool wide_art_on(const ctx *c)
{
    return c->screen == KJ_SCREEN_TOP && kj_wideart_count() > 0;
}

/* The layers' art keys, and which of them have a picture: said once per
 * key, so the log shows what the pictures are looked up by. */
static uint64_t s_logged_key[2][4];
static bool s_logged[2][4];

/* The bands drawn, said once per key and scroll. */
static uint64_t s_band_key[2][4];
static int s_band_dx[2][4];
static bool s_band_logged[2][4];

static void log_art_key(const ctx *c, int bg, uint64_t key)
{
    if (key == 0) {
        return;
    }
    const bool known = s_logged[c->engine][bg] && s_logged_key[c->engine][bg] == key;
    if (known) {
        return; /* said already: the log shows each key once */
    }
    const bool has = kj_wideart_find(key) != NULL;
    s_logged[c->engine][bg] = true;
    s_logged_key[c->engine][bg] = key;
    kj_log("[wide art] engine %d (%s) bg %d key %016llx%s", c->engine,
           c->screen == KJ_SCREEN_TOP ? "top" : "bottom", bg, (unsigned long long)key,
           has ? " (a picture)" : "");
}

/* A layer's picture, drawn in its bands: said once per key and scroll. */
static void log_band(const ctx *c, int bg, uint64_t key, int dx)
{
    if (s_band_logged[c->engine][bg] && s_band_key[c->engine][bg] == key &&
        s_band_dx[c->engine][bg] == dx) {
        return;
    }
    s_band_logged[c->engine][bg] = true;
    s_band_key[c->engine][bg] = key;
    s_band_dx[c->engine][bg] = dx;
    kj_log("[wide art] engine %d bg %d band key %016llx scrolled %d", c->engine, bg,
           (unsigned long long)key, dx);
}

/* One band: a rectangle of the picture's texture, at the canvas's x. */
static void put_band_quad(kj_v2d *v, int x, int y, int w, int h, int tx, int ty)
{
    const int16_t u0 = (int16_t)(tx * KJ_UV_UNIT + 1);
    const int16_t u1 = (int16_t)((tx + w) * KJ_UV_UNIT - 1);
    const int16_t v0 = (int16_t)(ty * KJ_UV_UNIT + 1);
    const int16_t v1 = (int16_t)((ty + h) * KJ_UV_UNIT - 1);
    v[0] = (kj_v2d){ (int16_t)x, (int16_t)y, u0, v0, 255, 255, 255, 255 };
    v[1] = (kj_v2d){ (int16_t)(x + w), (int16_t)y, u1, v0, 255, 255, 255, 255 };
    v[2] = (kj_v2d){ (int16_t)x, (int16_t)(y + h), u0, v1, 255, 255, 255, 255 };
    v[3] = (kj_v2d){ (int16_t)(x + w), (int16_t)(y + h), u1, v1, 255, 255, 255, 255 };
}

/*
 * The bands of a layer's picture, as quads. The picture's content is at
 * the canvas's x + dx (dx: how far the layer has scrolled since the
 * picture was made, to the right), so the picture's left band (its x 0..31)
 * is at the canvas's x -dx, and the canvas's band shows the picture's band
 * where they meet. What is left of the canvas's band is the picture's
 * middle: the game's own, which the layer draws itself.
 */
static void band_quads(const kj_wideart_pic *pic, int dx, kj_v2d *v, uint32_t cap, uint32_t *count)
{
    *count = 0;
    if (cap < 2) {
        return;
    }
    const int bh = pic->band_h;
    uint32_t n = 0;

    /* the left band: the picture's x 0..31 */
    int lo = dx > 0 ? 0 : -dx;
    int hi = dx > 0 ? WIDE_BAND_W - dx : WIDE_BAND_W;
    if (hi > WIDE_BAND_W) {
        hi = WIDE_BAND_W;
    }
    if (hi > lo) {
        put_band_quad(v + n * 4u, lo - WIDE_ORIGIN, 0, hi - lo, bh, pic->left_x + (lo + dx), pic->left_y);
        ++n;
    }

    /* the right band: the picture's x 288..319 */
    const int rx0 = WIDE_CANVAS_W - WIDE_BAND_W;
    lo = dx > 0 ? rx0 : rx0 - dx;
    hi = dx > 0 ? WIDE_CANVAS_W - dx : WIDE_CANVAS_W;
    if (hi > WIDE_CANVAS_W) {
        hi = WIDE_CANVAS_W;
    }
    if (hi > lo) {
        put_band_quad(v + n * 4u, lo - WIDE_ORIGIN, 0, hi - lo, bh, pic->right_x + (lo + dx - rx0),
                      pic->right_y);
        ++n;
    }

    *count = n;
}

/*
 * A layer's picture: the one made nearest where the layer is now, and how
 * far it has scrolled since. The bands show while the layer is across from
 * where the picture was made (a few pixels either way); up and down doesn't
 * matter (a layer that bobs keeps its bands).
 */
static const kj_wideart_pic *layer_picture(const ctx *c, int bg, uint64_t key, int *dx_out)
{
    const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
    const int map_w = s_text_sizes[cnt >> 14][0];
    const int map_h = s_text_sizes[cnt >> 14][1];
    const int cur_h = r16(c->regs, 0x10 + 4 * bg) & 0x1FF;
    const int cur_v = r16(c->regs, 0x12 + 4 * bg) & 0x1FF;
    const kj_wideart_pic *pic = kj_wideart_find_near(key, cur_h, cur_v, map_w, map_h, 256, 192, dx_out);

    if (pic == NULL) {
        return NULL;
    }
    if (!(pic->flags & KJ_WIDE_ART_LOOP) && (*dx_out < -ART_REST || *dx_out > ART_REST)) {
        return NULL; /* the layer is elsewhere: nothing beyond the DS's edges */
    }
    return pic;
}

/* ------------------------------------------------------- retained layers */

/*
 * Retained layers (OPT-002/OPT-005). When a frame has no per-line register
 * changes, each text BG and the OBJ layer keep last frame's quads in their
 * own GPU buffer. They are reused only if everything they were built from
 * is byte-identical: the registers they read, the palette, the map (text
 * BG) or OAM (OBJ), extended palettes, and the exact range of character
 * bytes the build read. Comparing a few KiB is far cheaper than ~800 atlas
 * lookups per layer, and it is exact: no write tracking, no hashing.
 */

#define CACHE_QUADS 2304u
#define MAP_MAX 8192u
/* The edge bands: one 32x8 quad per 8-line row on each side of a 192-line
 * screen. The wide art: at most two bands. */
#define EDGE_QUADS 48u
#define BAND_QUADS 2u

typedef struct layer_cache {
    bool valid;
    uint8_t key[16];
    uint64_t art_key;  /* the layer's art key (the wide art's pictures) */
    uint16_t pal[256]; /* the layer's palette (BG or OBJ) as built */
    uint8_t map[MAP_MAX];
    uint32_t map_off, map_len;
    uint8_t *chars;
    uint32_t char_lo, char_len, char_cap;
    uint8_t ext[0x2000];
    const uint8_t *ext_src;
    kj_v2d *quads;
    uint16_t slots[CACHE_QUADS];
    uint32_t count;
    /* The layer's edge and wide-art band quads, retained beside it: they are
     * built from the same registers and art as the layer's own quads. */
    kj_v2d *edge, *band;
    uint32_t edge_count, band_count;
    int art_screen;    /* the edge and wide art depend on which screen this is */
    bool art_valid;
    /* OBJ: per (priority, semi) sub-ranges of quads */
    uint32_t sub_first[9], sub_count[9];
    uint8_t oam[0x400];
} layer_cache;

static layer_cache s_bg_cache[2][4];
static layer_cache s_obj_cache[2];

typedef struct screen_memo {
    bool drawn;
    int engine;
    uint8_t regs[KJ_LINE_REG_BYTES];
    uint16_t pal[512];
} screen_memo;

static screen_memo s_memo[2];
static uint32_t s_skipped_screens, s_reused_layers, s_rebuilt_layers;

static bool cache_alloc(layer_cache *lc)
{
    if (lc->quads == NULL) {
        lc->quads = linearAlloc(CACHE_QUADS * 4u * sizeof(kj_v2d));
    }
    return lc->quads != NULL;
}

static uint32_t s_char_bytes_compared;

static bool chars_same(const layer_cache *lc, const uint8_t *vram)
{
#ifndef HGSS_PROFILE_OFF
    s_char_bytes_compared += lc->char_len;
#endif
    return lc->char_len == 0 || memcmp(lc->chars, vram + lc->char_lo, lc->char_len) == 0;
}

static void chars_keep(layer_cache *lc, const uint8_t *vram, uint32_t lo, uint32_t hi)
{
    if (hi <= lo) {
        lc->char_len = 0;
        return;
    }
    const uint32_t len = hi - lo;
    if (len > lc->char_cap) {
        free(lc->chars);
        lc->chars = malloc(len);
        lc->char_cap = lc->chars != NULL ? len : 0;
    }
    if (lc->chars == NULL) {
        lc->valid = false;
        lc->char_len = 0;
        return;
    }
    memcpy(lc->chars, vram + lo, len);
    lc->char_lo = lo;
    lc->char_len = len;
}

/* Registers a text BG's quads depend on. */
static void text_key(const ctx *c, int bg, uint8_t key[16])
{
    memset(key, 0, 16);
    memcpy(key, c->regs + 0x08 + 2 * bg, 2);
    memcpy(key + 2, c->regs + 0x10 + 4 * bg, 4);
    const uint32_t d = c->dispcnt & 0x7F000000u; /* char/screen offsets, ext palettes */
    memcpy(key + 6, &d, 4);
    key[10] = (uint8_t)c->engine;
}

static void text_map_range(const ctx *c, int bg, uint32_t *off, uint32_t *len)
{
    const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
    uint32_t scr = ((cnt >> 8) & 31u) * 0x800u;
    if (c->engine == 0) {
        scr += ((c->dispcnt >> 27) & 7u) * 0x10000u;
    }
    const unsigned size = cnt >> 14;
    *off = scr & (c->bg_vram_size - 1u);
    *len = (size == 0 ? 1u : size == 3 ? 4u : 2u) * 0x800u;
    if (*off + *len > c->bg_vram_size) {
        *len = c->bg_vram_size - *off;
    }
}

static const uint8_t *text_ext_src(const ctx *c, int bg)
{
    const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
    if (!(cnt & 0x80) || !(c->dispcnt & (1u << 30))) {
        return NULL;
    }
    const int slot = bg < 2 ? bg + (((cnt >> 13) & 1) ? 2 : 0) : bg;
    return ext_bg_slot(c->in, c->engine, slot);
}

/* A text BG, retained when possible. */
static bool text_bg_retained(const ctx *c, int bg, drawable *out, uint64_t *art_key)
{
    layer_cache *lc = &s_bg_cache[c->engine][bg];
    const uint16_t *pal = pltt_ptr(c->in->mem, c->engine, 0);
    uint8_t key[16];
    uint32_t moff, mlen;

    text_key(c, bg, key);
    text_map_range(c, bg, &moff, &mlen);
    const uint8_t *ext = text_ext_src(c, bg);
    if (lc->valid && memcmp(lc->key, key, 16) == 0 && lc->map_off == moff && lc->map_len == mlen &&
        memcmp(lc->pal, pal, 512) == 0 && memcmp(lc->map, c->bg_vram + moff, mlen) == 0 &&
        lc->ext_src == ext && (ext == NULL || memcmp(lc->ext, ext, 0x2000) == 0) &&
        chars_same(lc, c->bg_vram)) {
        out->base = lc->quads;
        out->count = lc->count;
        *art_key = lc->art_key;
#ifndef HGSS_PROFILE_OFF
        ++s_reused_layers;
#endif
        return true;
    }
    if (!cache_alloc(lc)) {
        return false;
    }
    lc->art_valid = false;
    run r;
    memset(&r, 0, sizeof(r));
    r.ext = true;
    r.v = lc->quads;
    r.cap = CACHE_QUADS;
    r.slots = lc->slots;
    text_bg(c, bg, &r);
    lc->valid = !r.incomplete;
    memcpy(lc->key, key, 16);
    memcpy(lc->pal, pal, 512);
    lc->map_off = moff;
    lc->map_len = mlen;
    memcpy(lc->map, c->bg_vram + moff, mlen);
    lc->ext_src = ext;
    if (ext != NULL) {
        memcpy(lc->ext, ext, 0x2000);
    }
    chars_keep(lc, c->bg_vram, r.char_lo, r.char_hi);
    lc->count = r.count;
    /* The art key is a hash of the same art the layer was built from, so it
     * changes with it: it is worked out when the layer is built. The
     * cache is per engine, not per screen, so the key is worked out
     * whenever the build made any pictures. */
    lc->art_key = kj_wideart_count() > 0 ? bg_art_key(c, bg) : 0;
    *art_key = lc->art_key;
    out->base = lc->quads;
    out->count = r.count;
    ++s_rebuilt_layers;
    return true;
}

/* The layer's edge and wide-art band quads, retained beside the layer's own
 * quads: they are built from exactly the same registers and art, so they are
 * rebuilt only when the layer is. */
/* The PICA reads vertex buffers from the address it is given, so these are
 * linearAlloc exactly like the layer's own quads; heap memory would be stale
 * and the bands would draw nothing (the target's clear would show). */
static bool art_alloc(layer_cache *lc)
{
    if (lc->edge == NULL) {
        lc->edge = linearAlloc(EDGE_QUADS * 4u * sizeof(kj_v2d));
    }
    if (lc->band == NULL) {
        lc->band = linearAlloc(BAND_QUADS * 4u * sizeof(kj_v2d));
    }
    return lc->edge != NULL && lc->band != NULL;
}

static void art_quads(const ctx *c, int bg, layer_cache *lc, uint64_t art_key,
                      drawable *edge, drawable *band)
{
    if (!lc->art_valid || lc->art_key != art_key || lc->art_screen != c->screen) {
        lc->art_key = art_key;
        lc->art_screen = c->screen;
        edge_band_quads(c, bg, lc->edge, EDGE_QUADS, &lc->edge_count);
        lc->band_count = 0;
        if (wide_art_on(c)) {
            log_art_key(c, bg, art_key);
            int dx = 0;
            const kj_wideart_pic *pic = layer_picture(c, bg, art_key, &dx);
            if (pic != NULL) {
                band_quads(pic, dx, lc->band, BAND_QUADS, &lc->band_count);
                log_band(c, bg, art_key, dx);
            }
        }
        lc->art_valid = true;
    }
    edge->base = lc->edge;
    edge->count = lc->edge_count;
    band->base = lc->band;
    band->count = lc->band_count;
}

/* The OBJ layer (all priorities), retained when possible. */
static bool objs_retained(const ctx *c, drawable sub[9])
{
    layer_cache *lc = &s_obj_cache[c->engine];
    const uint8_t *oam = c->in->mem->oam + c->engine * 0x400;
    const uint16_t *pal = pltt_ptr(c->in->mem, c->engine, 1);
    const uint8_t *ext = (c->dispcnt & (1u << 31)) ? ext_obj(c->in, c->engine) : NULL;
    uint8_t key[16];

    memset(key, 0, sizeof(key));
    const uint32_t d = c->dispcnt & 0x80301010u; /* OBJ on, mapping, boundary, ext */
    memcpy(key, &d, 4);
    key[4] = (uint8_t)c->engine;
    if (lc->valid && memcmp(lc->key, key, 16) == 0 && memcmp(lc->oam, oam, 0x400) == 0 &&
        memcmp(lc->pal, pal, 512) == 0 && lc->ext_src == ext &&
        (ext == NULL || memcmp(lc->ext, ext, 0x2000) == 0) && chars_same(lc, c->obj_vram)) {
        for (int i = 0; i < 9; ++i) {
            sub[i].base = lc->quads + lc->sub_first[i] * 4u;
            sub[i].count = lc->sub_count[i];
        }
#ifndef HGSS_PROFILE_OFF
        ++s_reused_layers;
#endif
        return true;
    }
    if (!cache_alloc(lc)) {
        return false;
    }
    uint32_t used = 0, lo = UINT32_MAX, hi = 0;
    bool incomplete = false;
    for (int i = 0; i < 9; ++i) {
        run r;
        memset(&r, 0, sizeof(r));
        r.ext = true;
        r.v = lc->quads + used * 4u;
        r.cap = CACHE_QUADS - used;
        r.slots = lc->slots + used;
        objs(c, i < 8 ? i >> 1 : -1, (i & 1) != 0, &r);
        lc->sub_first[i] = used;
        lc->sub_count[i] = r.count;
        used += r.count;
        if (r.char_lo != UINT32_MAX) {
            lo = r.char_lo < lo ? r.char_lo : lo;
            hi = r.char_hi > hi ? r.char_hi : hi;
        }
        incomplete |= r.incomplete;
    }
    lc->valid = !incomplete;
    memcpy(lc->key, key, 16);
    memcpy(lc->oam, oam, 0x400);
    memcpy(lc->pal, pal, 512);
    lc->ext_src = ext;
    if (ext != NULL) {
        memcpy(lc->ext, ext, 0x2000);
    }
    chars_keep(lc, c->obj_vram, lo == UINT32_MAX ? 0 : lo, hi);
    lc->count = used;
    for (int i = 0; i < 9; ++i) {
        sub[i].base = lc->quads + lc->sub_first[i] * 4u;
        sub[i].count = lc->sub_count[i];
    }
    ++s_rebuilt_layers;
    return true;
}

void kj_g2d_begin_frame(void)
{
    unowa_direct_calls = 0;
    unowa_direct_cells = 0;
    unowa_direct_clear = 0;
    unowa_direct_cells_n = 0;
    unowa_direct_nofit = 0;
    unowa_direct_norun = 0;
    unowa_direct_box[0] = unowa_direct_box[1] = unowa_direct_box[2] = unowa_direct_box[3] = 0;
    /* Retained quads point at atlas slots: keep them alive before any
     * allocation this frame can evict them. */
    for (int e = 0; e < 2; ++e) {
        for (int b = 0; b < 4; ++b) {
            if (s_bg_cache[e][b].valid) {
                kj_atlas_touch(s_bg_cache[e][b].slots, s_bg_cache[e][b].count);
            }
        }
        if (s_obj_cache[e].valid) {
            kj_atlas_touch(s_obj_cache[e].slots, s_obj_cache[e].count);
        }
    }
}

/* --------------------------------------------------------------- spans */

typedef struct prepared {
    drawable bg[4];
    bool bg_ok[4];
    uint64_t bg_key[4];      /* the layer's art key (the wide art's pictures) */
    drawable bg_band[4];     /* the layer's picture's bands, if it has one */
    drawable bg_edge[4];     /* the layer's edge colour carried into the bands */
    drawable obj[9]; /* priority * 2 + semi; [8]: OBJ-window OBJs */
    bool obj_ok;
    bool all_reused;
} prepared;

static void prepare_span(const ctx *c, bool retain, prepared *pr)
{
    const int bgmode = (int)(c->dispcnt & 7u);
    const uint32_t layers_on = (c->dispcnt >> 8) & 0x1Fu;
    const bool bg0_3d = c->engine == 0 && (c->dispcnt & 8u);
    const uint32_t rebuilt_before = s_rebuilt_layers;

    memset(pr, 0, sizeof(*pr));
    for (int bg = 0; bg < 4; ++bg) {
        if (!(layers_on & (1u << bg)) || (bg == 0 && bg0_3d)) {
            continue;
        }
        bool affine = false, ext16 = false, unsupported = false;
        switch (bgmode) {
        case 0: break;
        case 1: affine = bg == 3; break;
        case 2: affine = bg >= 2; break;
        case 3: ext16 = bg == 3; break;
        case 4: affine = bg == 2; ext16 = bg == 3; break;
        case 5: ext16 = bg >= 2; break;
        default: unsupported = bg >= 2; break;
        }
        if (ext16) {
            const uint16_t cnt = r16(c->regs, 0x08 + 2 * bg);
            /* Bit 7 = bitmap, bit 2 = direct colour. A 256-colour bitmap
             * still has no path; direct colour is drawn by affine_bg. */
            if ((cnt & 0x80u) != 0 && (cnt & 0x4u) == 0) {
                unsupported = true;
            }
        }
        if (unsupported) {
#ifndef HGSS_PROFILE_OFF
            ++s_stats.unsupported_modes;
#endif
            continue;
        }
        bool art_kept = false;
        layer_cache *lc = &s_bg_cache[c->engine][bg];
        if (!affine && !ext16 && retain) {
            pr->bg_ok[bg] = text_bg_retained(c, bg, &pr->bg[bg], &pr->bg_key[bg]);
            art_kept = pr->bg_ok[bg] && art_alloc(lc);
        } else {
            run r;
            memset(&r, 0, sizeof(r));
            if (affine || ext16) {
                affine_bg(c, bg, ext16, &r);
            } else {
                text_bg(c, bg, &r);
            }
            if (r.v != NULL) {
                run_end(&r);
                pr->bg[bg].base = NULL;
                pr->bg[bg].first = r.first;
                pr->bg[bg].count = r.count;
                pr->bg_ok[bg] = true;
            }
            if (!affine && !ext16 && kj_wideart_count() > 0) {
                pr->bg_key[bg] = bg_art_key(c, bg);
            }
            ++s_rebuilt_layers;
        }
        if (art_kept) {
            art_quads(c, bg, lc, pr->bg_key[bg], &pr->bg_edge[bg], &pr->bg_band[bg]);
        } else if (pr->bg_ok[bg]) {
            uint32_t ef, ec;
            const uint32_t rows =
                (uint32_t)((((c->y1 + 7) & ~7) - (c->y0 & ~7)) / 8);
            kj_v2d *ev = rows != 0 ? kj_gpu_quads_alloc(rows * 2u, &ef) : NULL;
            if (ev != NULL) {
                edge_band_quads(c, bg, ev, rows * 2u, &ec);
                pr->bg_edge[bg].base = NULL;
                pr->bg_edge[bg].first = ef;
                pr->bg_edge[bg].count = ec;
            }
        }
        if (!art_kept && pr->bg_ok[bg] && wide_art_on(c)) {
            log_art_key(c, bg, pr->bg_key[bg]);
            int dx = 0;
            const kj_wideart_pic *pic = layer_picture(c, bg, pr->bg_key[bg], &dx);
            if (pic != NULL) {
                uint32_t bf, bc;
                kj_v2d *bv = kj_gpu_quads_alloc(BAND_QUADS, &bf);
                if (bv != NULL) {
                    band_quads(pic, dx, bv, BAND_QUADS, &bc);
                    pr->bg_band[bg].base = NULL;
                    pr->bg_band[bg].first = bf;
                    pr->bg_band[bg].count = bc;
                    log_band(c, bg, pr->bg_key[bg], dx);
                }
            }
        }
#ifndef HGSS_PROFILE_OFF
        s_stats.bg_quads += pr->bg[bg].count;
#endif
        kj_perf_count(KJ_COUNT_QUADS, pr->bg[bg].count);
    }
    if (layers_on & 0x10u) {
        if (retain) {
            pr->obj_ok = objs_retained(c, pr->obj);
        } else {
            pr->obj_ok = true;
            for (int i = 0; i < 9; ++i) {
                run r;
                memset(&r, 0, sizeof(r));
                objs(c, i < 8 ? i >> 1 : -1, (i & 1) != 0, &r);
                if (r.v != NULL) {
                    run_end(&r);
                }
                pr->obj[i].base = NULL;
                pr->obj[i].first = r.first;
                pr->obj[i].count = r.v != NULL ? r.count : 0;
            }
            ++s_rebuilt_layers;
        }
        for (int i = 0; i < 9; ++i) {
#ifndef HGSS_PROFILE_OFF
            s_stats.obj_quads += pr->obj[i].count;
#endif
            kj_perf_count(KJ_COUNT_QUADS, pr->obj[i].count);
        }
    }
    pr->all_reused = retain && s_rebuilt_layers == rebuilt_before;
}

static void restore_2d_state(const ctx *c, int sx0)
{
    (void)sx0;
    /* The wide canvas: the whole view rectangle, so the edge bands and the
     * wide art may draw beside the DS's picture (the DS's own quads still
     * stay inside it). */
    scissor_span(c);
    kj_gpu_use_program(KJ_PROGRAM_2D);
    set_xform(c);
    kj_gpu_bind_texture(0, kj_atlas_texture());
    kj_gpu_tev_texture_modulate();
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_CullFace(GPU_CULL_NONE);
}

static void draw_span(ctx *c, const prepared *pr, bool draw_3d, bool *drew_3d)
{
    fx f;
    read_fx(c, &f);
    const uint32_t layers_on = (c->dispcnt >> 8) & 0x1Fu;
    const bool bg0_3d = c->engine == 0 && (c->dispcnt & 8u);
    const int sx0 = (int)(c->target->offset_x);

    restore_2d_state(c, sx0);
    if (f.windows) {
        if (f.objwin && pr->obj_ok && pr->obj[8].count) {
            /* Opaque texels of the window OBJs (alpha test) stamp region 3
             * into stencil bits 4-5; no colour writes. WIN1/WIN0 follow
             * and override it, matching DS window priority. */
            C3D_DepthTest(false, GPU_ALWAYS, 0);
            blend_off();
            C3D_StencilTest(true, GPU_ALWAYS, 0x30, 0, 0x30);
            C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
            draw_quads(&pr->obj[8]);
        }
        write_windows(c);
        restore_2d_state(c, sx0);
    }
    for (int p = 3; p >= 0; --p) {
        for (int bg = 3; bg >= 0; --bg) {
            if (!(layers_on & (1u << bg)) || (r16(c->regs, 0x08 + 2 * bg) & 3) != p) {
                continue;
            }
            if (bg == 0 && bg0_3d) {
                if (draw_3d && !*drew_3d) {
                    const bool t2 = (f.bldcnt >> 8) & 1;
                    const bool t1 = f.bldcnt & 1;
                    kj_gpu_scissor_off();
                    kj_g3d_draw(c->target, c->screen == KJ_SCREEN_TOP,
                                (uint8_t)((t2 ? 1 : 0) | (t1 && f.mode >= 2 ? 2 : 0)),
                                bg0_window_on(&f), f.windows ? f.active : 0);
                    *drew_3d = true;
                    restore_2d_state(c, sx0);
                }
                continue;
            }
            if (pr->bg_ok[bg]) {
                draw_layer(c, &f, bg, &pr->bg[bg], false, &pr->bg_band[bg], &pr->bg_edge[bg]);
            }
        }
        if (pr->obj_ok) {
            draw_layer(c, &f, LAYER_OBJ, &pr->obj[p * 2], false, NULL, NULL);
            draw_layer(c, &f, LAYER_OBJ, &pr->obj[p * 2 + 1], true, NULL, NULL);
        }
    }
    brightness_pass(&f);
}

static uint32_t ds_to_rgba(uint16_t c)
{
    const uint32_t r = c & 31u, g = (c >> 5) & 31u, b = (c >> 10) & 31u;
    return ((r << 3) | (r >> 2)) << 24 | ((g << 3) | (g >> 2)) << 16 | ((b << 3) | (b >> 2)) << 8 |
           0xFFu;
}

static void screen_draw_impl(int screen, int engine, const kj_frame_input *in,
                             const kj_g2d_view *view, bool clear)
{
    const kj_target *target = kj_target_get(screen);
    const kj_ds_memory *mem = in->mem;
    screen_memo *memo = &s_memo[screen];
    ctx c;

    memset(&c, 0, sizeof(c));
    c.in = in;
    c.target = target;
    c.screen = screen;
    c.engine = engine;
    c.bg_vram = engine == 0 ? mem->bg_vram_a : mem->bg_vram_b;
    c.bg_vram_size = engine == 0 ? 0x80000u : 0x20000u;
    c.obj_vram = engine == 0 ? mem->obj_vram_a : mem->obj_vram_b;
    c.obj_vram_size = engine == 0 ? 0x40000u : 0x20000u;
    if (view != NULL) {
        c.sx = view->sx;
        c.sy = view->sy;
        c.ox = view->ox;
        c.oy = view->oy;
        c.clip_x0 = view->clip_x0;
        c.clip_y0 = view->clip_y0;
        c.clip_x1 = view->clip_x1;
        c.clip_y1 = view->clip_y1;
        c.full = false;
    } else {
        c.sx = c.sy = target->scale;
        c.ox = target->offset_x;
        c.oy = 0.0f;
        c.clip_x0 = 0;
        c.clip_y0 = 0;
        c.clip_x1 = target->width;
        c.clip_y1 = 240;
        c.full = true;
    }

    const uint8_t *io = mem->io + engine * 0x1000;
    const uint32_t dispcnt = r32(io, 0);
    if (screen == KJ_SCREEN_BOTTOM) {
        ++unowa_bottom_calls;
    }
    const uint16_t *pal = pltt_ptr(mem, engine, 0); /* BG then OBJ: 512 colours */
    const uint16_t bldcnt = r16(io, 0x50);
    const uint8_t bd_flags = (uint8_t)(((bldcnt >> 13) & 1) | ((bldcnt & 0x20) && ((bldcnt >> 6) & 3) >= 2 ? 2 : 0));
    const int display_mode = (int)((dispcnt >> 16) & 3u);
    /* GX_DISPMODE_VRAM_A..D (gx.h:84-87) select a captured bank instead of
     * the live layers; the capture source is this engine's own graphics
     * screen (DISPCAPCNT SRCA=0 in the title), so drawing the layers is the
     * same picture without the pass through VRAM. BW's title uses it to put
     * the 3D mascot on the lower screen (DISPCNT bits 16-19 = 0xE). */
    const bool vram_display = display_mode == 2;
    const int draw_mode = vram_display ? 1 : display_mode;
    const bool forced_blank = (dispcnt & 0x80u) != 0;
    const bool has_3d = engine == 0 && (dispcnt & 8u) && in->g3 != NULL;
    const bool single_span = !in->lines.active;

    /* Build or reuse the layers before touching the GPU: an unchanged
     * screen is then not drawn, cleared or transferred at all (its last
     * picture stays on the display). */
    prepared pr;
    bool prepared_ok = false;
    uint64_t t0 = kj_perf_now();
    if (single_span && draw_mode == 1 && !forced_blank) {
        c.y0 = 0;
        c.y1 = 192;
        c.regs = io;
        c.dispcnt = dispcnt;
        prepare_span(&c, true, &pr);
        prepared_ok = true;
        if (c.full && pr.all_reused && !has_3d && memo->drawn && memo->engine == engine &&
            memcmp(memo->regs, io, KJ_LINE_REG_BYTES) == 0 && memcmp(memo->pal, pal, 1024) == 0) {
#ifndef HGSS_PROFILE_OFF
            ++s_skipped_screens;
#endif
            if (screen == KJ_SCREEN_BOTTOM) {
                ++unowa_bottom_skips;
            }
            kj_perf_add(KJ_PERF_G2D, kj_perf_now() - t0);
            return;
        }
    }
    memo->drawn = false;
    if (screen == KJ_SCREEN_BOTTOM) {
        ++unowa_bottom_draws;
        if (prepared_ok) {
            for (int i = 0; i < 9; ++i) {
                unowa_bottom_obj_quads += pr.obj[i].count;
            }
            for (int i = 0; i < 4; ++i) {
                unowa_bottom_obj_quads += pr.bg[i].count;
            }
        }
    }

    kj_gpu_invalidate();
    C3D_FrameDrawOn(target->rt);

    if (forced_blank || display_mode == 0) {
        if (clear) {
            C3D_RenderTargetClear(target->rt, C3D_CLEAR_ALL, 0xFFFFFFFFu, 0);
        } else {
            kj_g2d_fill_rects(screen,
                              &(kj_g2d_rect){ c.clip_x0, c.clip_y0, c.clip_x1, c.clip_y1, 0xFFFFFFFFu },
                              1);
        }
        kj_gpu_use_program(KJ_PROGRAM_2D);
        set_xform(&c);
        master_brightness(&c, r16(io, 0x6C));
        kj_perf_add(KJ_PERF_G2D, kj_perf_now() - t0);
        return;
    }
    if (draw_mode != 1) {
#ifndef HGSS_PROFILE_OFF
        ++s_stats.unsupported_modes; /* VRAM / main-memory display: tracked blocker */
#endif
        if (clear) {
            C3D_RenderTargetClear(target->rt, C3D_CLEAR_ALL, 0x000000FFu, 0);
        } else {
            kj_g2d_fill_rects(screen,
                              &(kj_g2d_rect){ c.clip_x0, c.clip_y0, c.clip_x1, c.clip_y1, 0x000000FFu },
                              1);
        }
        kj_perf_add(KJ_PERF_G2D, kj_perf_now() - t0);
        return;
    }

    /* Depth clear: far is 0 in Z mode, 1 in W mode (g3d.c). */
    const u32 far_depth = has_3d && kj_g3d_wbuffer() ? 0xFFFFFFu : 0u;
    if (clear) {
        C3D_RenderTargetClear(target->rt, C3D_CLEAR_ALL, ds_to_rgba(pal[0]),
                              ((u32)bd_flags << 24) | far_depth);
    } else {
        /* A view keeps what is outside it: paint the DS backdrop in the
         * view's rectangle only (the menu's panel interior). */
        kj_g2d_fill_rects(
            screen,
            &(kj_g2d_rect){ c.clip_x0, c.clip_y0, c.clip_x1, c.clip_y1, ds_to_rgba(pal[0]) }, 1);
    }

    bool drew_3d = false;
    if (prepared_ok) {
        draw_span(&c, &pr, has_3d, &drew_3d);
#ifndef HGSS_PROFILE_OFF
        ++s_stats.spans;
#endif
        kj_perf_count(KJ_COUNT_SPANS, 1);
    } else {
        for (int y0 = 0; y0 < 192;) {
            const int y1 = span_end(&in->lines, engine, y0);
            c.y0 = y0;
            c.y1 = y1;
            c.regs = in->lines.active ? in->lines.regs[engine][y0] : io;
            c.dispcnt = r32(c.regs, 0);
            prepare_span(&c, false, &pr);
            draw_span(&c, &pr, has_3d, &drew_3d);
#ifndef HGSS_PROFILE_OFF
            ++s_stats.spans;
#endif
        kj_perf_count(KJ_COUNT_SPANS, 1);
            y0 = y1;
        }
    }
    if (c.full) {
        kj_gpu_scissor_off();
    } else {
        scissor_clip(&c);
    }
    kj_gpu_use_program(KJ_PROGRAM_2D);
    set_xform(&c);
    master_brightness(&c, r16(io, 0x6C));

    memo->drawn = single_span && c.full;
    memo->engine = engine;
    memcpy(memo->regs, io, KJ_LINE_REG_BYTES);
    memcpy(memo->pal, pal, 1024);
    kj_perf_add(KJ_PERF_G2D, kj_perf_now() - t0);
}

void kj_screen_draw(int screen, int engine, const kj_frame_input *in)
{
    screen_draw_impl(screen, engine, in, NULL, true);
}

void kj_screen_draw_view(int screen, int engine, const kj_frame_input *in,
                         const kj_g2d_view *view)
{
    screen_draw_impl(screen, engine, in, view, false);
}

/* Chrome drawn straight in physical pixels: DS pixel == physical pixel. */
static void identity_ctx(ctx *c, int screen, int x0, int y0, int x1, int y1)
{
    const kj_target *t = kj_target_get(screen);
    memset(c, 0, sizeof(*c));
    c->target = t;
    c->sx = c->sy = 1.0f;
    c->ox = c->oy = 0.0f;
    c->clip_x0 = x0 < 0 ? 0 : x0;
    c->clip_y0 = y0 < 0 ? 0 : y0;
    c->clip_x1 = x1 > t->width ? t->width : x1;
    c->clip_y1 = y1 > 240 ? 240 : y1;
    c->full = false;
}

void kj_g2d_fill_rects(int screen, const kj_g2d_rect *rects, int count)
{
    if (rects == NULL || count <= 0) {
        return;
    }
    const kj_target *t = kj_target_get(screen);
    ctx c;
    identity_ctx(&c, screen, 0, 0, t->width, 240);
    uint32_t first;
    kj_v2d *v = kj_gpu_quads_alloc((uint32_t)count, &first);
    if (v == NULL) {
        return;
    }
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const kj_g2d_rect *r = &rects[i];
        const int x0 = r->x0 < c.clip_x0 ? c.clip_x0 : r->x0;
        const int y0 = r->y0 < c.clip_y0 ? c.clip_y0 : r->y0;
        const int x1 = r->x1 > c.clip_x1 ? c.clip_x1 : r->x1;
        const int y1 = r->y1 > c.clip_y1 ? c.clip_y1 : r->y1;
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        const uint8_t cr = r->rgba & 255, cg = (r->rgba >> 8) & 255, cb = (r->rgba >> 16) & 255,
                      ca = r->rgba >> 24;
        kj_v2d *q = v + n * 4;
        q[0] = (kj_v2d){ (int16_t)x0, (int16_t)y0, 0, 0, cr, cg, cb, ca };
        q[1] = (kj_v2d){ (int16_t)x1, (int16_t)y0, 0, 0, cr, cg, cb, ca };
        q[2] = (kj_v2d){ (int16_t)x0, (int16_t)y1, 0, 0, cr, cg, cb, ca };
        q[3] = (kj_v2d){ (int16_t)x1, (int16_t)y1, 0, 0, cr, cg, cb, ca };
        ++n;
    }
    if (n == 0) {
        kj_gpu_quads_reset_to(first);
        return;
    }
    C3D_FrameDrawOn(c.target->rt);
    kj_gpu_use_program(KJ_PROGRAM_2D);
    set_xform(&c);
    kj_gpu_tev_vertex_color();
    blend_off();
    C3D_DepthTest(false, GPU_ALWAYS, 0);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    scissor_clip(&c);
    kj_gpu_draw_quads(first, (uint32_t)n);
    kj_gpu_quads_reset_to(first + (uint32_t)n);
}

void kj_g2d_tiles(int screen, const kj_g2d_tile_rect *rects, int count)
{
    if (rects == NULL || count <= 0) {
        return;
    }
    const kj_target *t = kj_target_get(screen);
    ctx c;
    identity_ctx(&c, screen, 0, 0, t->width, 240);
    uint32_t first;
    kj_v2d *v = kj_gpu_quads_alloc((uint32_t)count, &first);
    if (v == NULL) {
        return;
    }
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const kj_g2d_tile_rect *r = &rects[i];
        const int x0 = r->x0 < c.clip_x0 ? c.clip_x0 : r->x0;
        const int y0 = r->y0 < c.clip_y0 ? c.clip_y0 : r->y0;
        const int x1 = r->x1 > c.clip_x1 ? c.clip_x1 : r->x1;
        const int y1 = r->y1 > c.clip_y1 ? c.clip_y1 : r->y1;
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        int16_t u0, v0, u1, v1;
        kj_atlas_slot_uv(r->slot, &u0, &v0, &u1, &v1);
        const uint8_t cr = r->rgba & 255, cg = (r->rgba >> 8) & 255,
                      cb = (r->rgba >> 16) & 255, ca = r->rgba >> 24;
        kj_v2d *q = v + n * 4;
        q[0] = (kj_v2d){ (int16_t)x0, (int16_t)y0, u0, v0, cr, cg, cb, ca };
        q[1] = (kj_v2d){ (int16_t)x1, (int16_t)y0, u1, v0, cr, cg, cb, ca };
        q[2] = (kj_v2d){ (int16_t)x0, (int16_t)y1, u0, v1, cr, cg, cb, ca };
        q[3] = (kj_v2d){ (int16_t)x1, (int16_t)y1, u1, v1, cr, cg, cb, ca };
        ++n;
    }
    if (n == 0) {
        kj_gpu_quads_reset_to(first);
        return;
    }
    C3D_FrameDrawOn(c.target->rt);
    kj_gpu_use_program(KJ_PROGRAM_2D);
    set_xform(&c);
    kj_gpu_bind_texture(0, kj_atlas_texture());
    kj_gpu_tev_texture_modulate();
    kj_gpu_set_2d_uvscale(KJ_ATLAS_SIZE, KJ_ATLAS_SIZE);
    blend_off();
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(true, GPU_GREATER, 0);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
    scissor_clip(&c);
    kj_gpu_draw_quads(first, (uint32_t)n);
    kj_gpu_quads_reset_to(first + (uint32_t)n);
}

/* ---------------------------------------------------------------- init */

bool kj_g2d_init(void)
{
    memset(s_pal, 0, sizeof(s_pal));
    if (!kj_gpu_init() || !kj_atlas_init()) {
        return false;
    }
    /* The wide art: the build may have made no pictures (the wideart folder
     * empty), and then there are none to draw. */
    kj_wideart_init();
    return true;
}

void kj_g2d_exit(void)
{
    kj_wideart_exit();
    kj_atlas_exit();
    kj_gpu_exit();
}

void kj_g2d_take_stats(kj_g2d_stats *out)
{
    s_stats.skipped_screens = s_skipped_screens;
    s_stats.reused_layers = s_reused_layers;
    s_stats.rebuilt_layers = s_rebuilt_layers;
    s_stats.char_bytes_compared = s_char_bytes_compared;
    s_skipped_screens = s_reused_layers = s_rebuilt_layers = 0;
    s_char_bytes_compared = 0;
    *out = s_stats;
    out->dropped_quads += kj_gpu_quads_dropped();
    memset(&s_stats, 0, sizeof(s_stats));
}
