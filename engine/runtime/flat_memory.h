/*
 * The DS main-RAM image, kept flat.
 *
 * The game addresses its data by DS address. unowa keeps that data at the same
 * offsets inside sinjoh's 8 MB `NitroPort_MainMem`, so DS address `0x02xxxxxx`
 * is `NitroPort_MainMem + (address & 0x7FFFFF)`. Names are bound to their bytes
 * by the generated linker script (tools/gen_flat_image.py), the ARM9 module's
 * data is copied in at boot, and each overlay's data is copied into its DS
 * window when the game loads it, as FS_LoadOverlay does on the DS.
 *
 * The tables below are generated per build from the player's ROM extract.
 */
#ifndef UNOWA_RUNTIME_FLAT_MEMORY_H
#define UNOWA_RUNTIME_FLAT_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

/* Bytes of one module that come from the ROM, at DS address `ds_address`. */
typedef struct {
    uint32_t ds_address;
    uint32_t blob_offset;
    uint32_t size_bytes;
} unowa_flat_span;

/* A stored pointer whose target is native code or native data. */
typedef struct {
    uint32_t ds_address;
    const void *target;
} unowa_flat_native;

/*
 * One loadable module: index 0 is the ARM9 static module, index 1 + id is
 * overlay `id`. Addresses are DS addresses. The `*_first`/`*_count` pairs
 * index the span, pointer-word and native-word tables.
 */
typedef struct {
    uint32_t ram_start;
    uint32_t ram_end;
    uint32_t bss_end;
    uint32_t sinit_start;
    uint32_t sinit_end;
    uint32_t span_first;
    uint32_t span_count;
    uint32_t pointer_first;
    uint32_t pointer_count;
    uint32_t native_first;
    uint32_t native_count;
} unowa_flat_module;

extern const unsigned char unowa_flat_blob[];
extern const unowa_flat_span unowa_flat_spans[];
extern const uint32_t unowa_flat_pointer_words[];
extern const unowa_flat_native unowa_flat_native_words[];
extern const unowa_flat_module unowa_flat_modules[];
extern const uint32_t unowa_flat_module_count;

/*
 * Host address of a DS address the port backs, or NULL. Main RAM and its
 * shared-area mirrors map into NitroPort_MainMem; LCDC VRAM maps into
 * NitroPort_LcdcVram (one overlay, ov095, loads there).
 */
void *unowa_ds_to_host(uint32_t ds_address);

/* Copy the ARM9 module's data into the flat image. Call once, before NitroMain. */
void unowa_flat_boot(void);

/*
 * Load overlay `id`'s data into its window: copy its spans, zero its BSS and
 * relocate its stored pointers. Returns false for an unknown id.
 */
bool unowa_flat_load_overlay(uint32_t id);

#endif
