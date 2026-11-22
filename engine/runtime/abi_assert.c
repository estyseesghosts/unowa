/*
 * abi_assert.c: compile-time checks for the DS/3DS boundary contract.
 *
 * The bridge is imported verbatim from sinjoh, so its layout rules cannot be
 * changed here. These assertions make the rules observable at build time:
 * a toolchain or flag change that alters pointer or struct width fails the
 * build instead of producing a silently corrupt save or geometry packet.
 *
 * Source for the rules: UNOWA.md section 3 "Critical ABI rules" and
 * engine/runtime/kj_bridge.h lines 9-14. kj_g3_vertex's 16-byte size is
 * documented at kj_bridge.h:116.
 */

#include "runtime/kj_bridge.h"

#include <stddef.h>
#include <stdint.h>

/* Both the DS ARM9 and the 3DS ARM11 use 32-bit pointers. */
_Static_assert(sizeof(void *) == 4, "unowa requires 32-bit pointers");
_Static_assert(sizeof(uintptr_t) == 4, "uintptr_t must be 32 bits");
_Static_assert(sizeof(size_t) == 4, "size_t must be 32 bits");
_Static_assert(sizeof(uint32_t) == 4, "uint32_t must be exactly 4 bytes");

/* Game settings: a fixed UTF-16 name followed by four single-byte fields. */
_Static_assert(offsetof(kj_game_settings, nickname) == 0, "nickname must lead");
_Static_assert(offsetof(kj_game_settings, language) == 22, "UTF-16 name is 11 words");
_Static_assert(offsetof(kj_game_settings, birth_month) == 23, "packed byte follows language");
_Static_assert(offsetof(kj_game_settings, birth_day) == 24, "packed byte follows month");
_Static_assert(offsetof(kj_game_settings, favorite_color) == 25, "packed byte follows day");
_Static_assert(sizeof(kj_game_settings) == 26, "settings must stay packed");

/* The DS memory view is eight pointers followed by one 32-bit length. */
_Static_assert(offsetof(kj_ds_memory, io_size) == 32, "eight 32-bit pointers precede io_size");
_Static_assert(sizeof(kj_ds_memory) == 36, "DS memory view must stay 36 bytes");

/* VRAM bank record: two words then three bytes, padded to a word. */
_Static_assert(offsetof(kj_vram_bank, size) == 4, "lcdc_offset is one word");
_Static_assert(offsetof(kj_vram_bank, enabled) == 8, "byte fields follow two words");
_Static_assert(sizeof(kj_vram_bank) == 12, "VRAM bank record must stay 12 bytes");

/* The geometry vertex ABI is part of the PICA shader contract. */
_Static_assert(sizeof(kj_g3_vertex) == 16, "geometry vertex must stay 16 bytes");
_Static_assert(offsetof(kj_g3_vertex, mtx) == 6, "position occupies three words");
_Static_assert(offsetof(kj_g3_vertex, s) == 12, "colour occupies four bytes");
