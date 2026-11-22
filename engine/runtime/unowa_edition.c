/*
 * unowa_edition.c: edition identity for the platform side.
 *
 * M0 placeholder. In M1 this becomes a thin adapter over the Aletheia/Eidos
 * game-side identity; the bridge declaration and the string values do not
 * change. UNOWA_EDITION is set by the Makefile to 0 (Black) or 1 (White).
 */

#include "runtime/kj_bridge.h"

#ifndef UNOWA_EDITION
#error "UNOWA_EDITION must be 0 (black) or 1 (white)"
#endif

#if UNOWA_EDITION == 0
#define UNOWA_EDITION_NAME "black"
#elif UNOWA_EDITION == 1
#define UNOWA_EDITION_NAME "white"
#else
#error "UNOWA_EDITION must be 0 (black) or 1 (white)"
#endif

const char *kj_ds_edition_name(void)
{
    return UNOWA_EDITION_NAME;
}
