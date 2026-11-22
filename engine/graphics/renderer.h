/* unowa: minimal M0 GPU renderer, GPU-native only (no CPU pixel raster). */
#ifndef UNOWA_GRAPHICS_RENDERER_H
#define UNOWA_GRAPHICS_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#define UNOWA_TOP_WIDTH 400
#define UNOWA_TOP_HEIGHT 240
#define UNOWA_BOTTOM_WIDTH 320
#define UNOWA_BOTTOM_HEIGHT 240

/* Initialise citro3d and the two physical render targets. No stereo target:
 * stereo is excluded from the Old 3DS baseline (UNOWA.md section 1). */
bool unowa_renderer_init(void);

void unowa_renderer_exit(void);

/* Clear and submit one picture. Non-synchronising: this waits for the GPU
 * queue, not for the display, so the caller's game tick is never slowed by
 * presentation. Returns false if the queue was not ready and the picture had
 * to be skipped (the game tick must not be skipped with it). */
bool unowa_renderer_present(uint32_t top_color, uint32_t bottom_color);

#endif
