#ifndef KJ_GRAPHICS_G2D_G2D_H
#define KJ_GRAPHICS_G2D_G2D_H

#include "runtime/kj_bridge.h"

#include <stdbool.h>
#include <stdint.h>

bool kj_g2d_init(void);
void kj_g2d_exit(void);

/* Draw one DS engine's display onto a screen target: backdrop, BG and OBJ
 * layers in DS priority order with the 3D layer at BG0's slot (engine A),
 * colour effects and master brightness. */
void kj_screen_draw(int screen, int engine, const kj_frame_input *in);

/* Where a draw goes on a target: DS pixel (x, y) maps to
 * (x * sx + ox, y * sy + oy), clipped to the physical rectangle. The whole
 * target view is `sx = sy = scale, ox = offset_x, oy = 0`, clipped to the
 * target; the field menu's panel is a smaller view inside the lower screen. */
typedef struct kj_g2d_view {
    float sx, sy;
    float ox, oy;
    int clip_x0, clip_y0, clip_x1, clip_y1;
} kj_g2d_view;

/* Draw one DS engine's display through `view` without clearing the target:
 * the pixels outside the view are left as they are (the menu's chrome). */
void kj_screen_draw_view(int screen, int engine, const kj_frame_input *in, const kj_g2d_view *view);

/* Solid rectangles in the target's physical pixels (no DS scaling), drawn
 * in one pass: the menu's chrome is a dozen rects a frame. */
typedef struct kj_g2d_rect {
    int x0, y0, x1, y1;
    uint32_t rgba; /* r | g << 8 | b << 16 | a << 24 */
} kj_g2d_rect;
void kj_g2d_fill_rects(int screen, const kj_g2d_rect *rects, int count);

/* Atlas tiles at physical-pixel rectangles (nearest), in one pass. The
 * menu's icons and small font are procedural DS tiles, so they take the
 * same atlas path as the game's own layers. `rgba` tints the tile, the same
 * way a DS BG or OBJ is modulated by its palette. */
typedef struct kj_g2d_tile_rect {
    int x0, y0, x1, y1;
    uint16_t slot;
    uint32_t rgba; /* r | g << 8 | b << 16 | a << 24 */
} kj_g2d_tile_rect;
void kj_g2d_tiles(int screen, const kj_g2d_tile_rect *rects, int count);

typedef struct kj_g2d_stats {
    uint32_t bg_quads, obj_quads, draws, spans;
    uint32_t unsupported_modes; /* modes drawn as nothing; must be zero in the corpus */
    uint32_t wide_windows;      /* window side rectangles over the wide columns */
    uint32_t dropped_quads;
    uint32_t skipped_screens, reused_layers, rebuilt_layers;
    uint32_t char_bytes_compared; /* retained layers' character-range checks */
} kj_g2d_stats;

/* Start of a frame (after kj_atlas_begin_frame): pins retained layers. */
void kj_g2d_begin_frame(void);

void kj_g2d_take_stats(kj_g2d_stats *out);

#endif
