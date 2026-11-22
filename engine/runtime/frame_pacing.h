/* unowa: emulated DS V-blank pacing, locked to the physical 3DS V-blank. */
#ifndef UNOWA_RUNTIME_FRAME_PACING_H
#define UNOWA_RUNTIME_FRAME_PACING_H

#include <stdint.h>

/* Capture citro3d's V-blank counter as the pacing origin. Call once, after
 * the renderer has initialized citro3d. */
void unowa_frame_pacing_init(void);

/* Wait for the next emulated DS V-blank. Called from NitroPort_OnFrame, which
 * the game reaches once per OS_WaitIrq. One call is one DS V-blank: the game
 * tick is never skipped, and presentation is paced separately. */
void unowa_frame_pacing_vblank(void);

/* Observation counters, readable from the guest in LOTUS. */
extern uint32_t unowa_pacing_vblanks;
extern uint32_t unowa_pacing_late_vblanks;
extern uint32_t unowa_pacing_resyncs;

#endif
