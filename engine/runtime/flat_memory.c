/*
 * The DS main-RAM image, kept flat. See flat_memory.h.
 *
 * Overlay loading. sinjoh's SDK calls four port seams instead of reading the
 * cartridge (sinjoh build/NitroSDK/libraries/fs/src/fs_overlay.c:79-80,
 * FS_LoadOverlayInfo and FS_LoadOverlayImage under SDK_PORT_3DS). sinjoh's own
 * implementations (engine/compat/nitro/src/port_overlay.c) serve Gen IV, whose
 * overlay data the build links natively; they report label addresses and know
 * no Gen V overlay. unowa replaces the three it needs with `ld --wrap` so the
 * SDK sees each overlay's real DS window, inside the flat image:
 *
 *   NitroPort_OverlayInfo       the overlay table header, as the DS ROM has it
 *                               (FSOverlayInfoHeader, nitro/fs/overlay.h:30-40)
 *   NitroPort_OverlayLoadImage  copy the overlay's data in and relocate it
 *   NitroPort_OverlayClearBss   zero the BSS past the image, as the DS
 *                               FS_ClearOverlayImage does (fs_overlay.c:90)
 *
 * Code is native and linked once; only data moves, so a "load" is a copy of
 * the overlay's data tail and a pass over its pointer words. The overlay's
 * static initialisers run from the relocated table, through FS_StartOverlay.
 */
#include "runtime/flat_memory.h"

#include <nitro.h>
#include <string.h>

extern unsigned char NitroPort_MainMem[];
extern unsigned char NitroPort_LcdcVram[];
extern void NitroPort_Log(const char *format, ...);

#define DS_MAIN_RAM_START 0x02000000u
#define DS_MAIN_RAM_END 0x03000000u
#define DS_FLAT_MASK 0x007FFFFFu

/*
 * The DS memory ranges sinjoh keeps in shadow arrays. Taken from
 * tools/gen_port_ioreg_header.py REGIONS, which redirects the game's
 * `IOREG_ADDR(...)` names to the same storage; a pointer the game forms from a
 * NitroSDK macro instead (`HW_LCDC_VRAM`, `HW_BG_VRAM`) reaches the port as a
 * raw DS address and lands here through unowa_ds_to_host (see
 * engine/runtime/ds_memory_ops.c).
 */
extern unsigned char NitroPort_IoReg[];
extern unsigned char NitroPort_Pltt[];
extern unsigned char NitroPort_BgVram[];
extern unsigned char NitroPort_DbBgVram[];
extern unsigned char NitroPort_ObjVram[];
extern unsigned char NitroPort_DbObjVram[];
extern unsigned char NitroPort_LcdcVram[];
extern unsigned char NitroPort_Oam[];

static const struct {
    uint32_t base;
    uint32_t size;
    unsigned char *array;
} s_ds_regions[] = {
    { 0x04000000u, 0x00110000u, NitroPort_IoReg },
    { 0x05000000u, 0x00000800u, NitroPort_Pltt },
    { 0x06000000u, 0x00080000u, NitroPort_BgVram },
    { 0x06200000u, 0x00020000u, NitroPort_DbBgVram },
    { 0x06400000u, 0x00040000u, NitroPort_ObjVram },
    { 0x06600000u, 0x00020000u, NitroPort_DbObjVram },
    { 0x06800000u, 0x000A4000u, NitroPort_LcdcVram },
    { 0x07000000u, 0x00000800u, NitroPort_Oam },
};

void *unowa_ds_to_host(uint32_t ds_address)
{
    uint32_t i;

    if (ds_address >= DS_MAIN_RAM_START && ds_address < DS_MAIN_RAM_END) {
        return NitroPort_MainMem + (ds_address & DS_FLAT_MASK);
    }
    for (i = 0; i < sizeof(s_ds_regions) / sizeof(s_ds_regions[0]); ++i) {
        if (ds_address - s_ds_regions[i].base < s_ds_regions[i].size) {
            return s_ds_regions[i].array + (ds_address - s_ds_regions[i].base);
        }
    }
    return NULL;
}

/*
 * Aletheia src/game/arm9/os_arena.c:30 lists the DS main arena's low end,
 * after the static module and every overlay window. The generated linker
 * script binds the name to its flat-image byte.
 */
extern char SDK_MAIN_ARENA_LO[];

/* Rewrite one stored DS data pointer to its flat-image address. */
static void relocate_flat_word(uint32_t ds_address)
{
    uint32_t *word = unowa_ds_to_host(ds_address);
    void *target;

    if (word == NULL) {
        return;
    }
    target = unowa_ds_to_host(*word);
    if (target != NULL) {
        *word = (uint32_t)target;
    }
}

static void load_module(const unowa_flat_module *module)
{
    uint32_t i;
    unsigned char *bss = unowa_ds_to_host(module->ram_end);

    if (bss != NULL && module->bss_end > module->ram_end) {
        memset(bss, 0, module->bss_end - module->ram_end);
    }
    for (i = 0; i < module->span_count; ++i) {
        const unowa_flat_span *span = &unowa_flat_spans[module->span_first + i];
        unsigned char *destination = unowa_ds_to_host(span->ds_address);

        if (destination != NULL) {
            memcpy(destination, unowa_flat_blob + span->blob_offset, span->size_bytes);
        }
    }
    for (i = 0; i < module->pointer_count; ++i) {
        relocate_flat_word(unowa_flat_pointer_words[module->pointer_first + i]);
    }
    for (i = 0; i < module->native_count; ++i) {
        const unowa_flat_native *entry = &unowa_flat_native_words[module->native_first + i];
        uint32_t *word = unowa_ds_to_host(entry->ds_address);

        if (word != NULL) {
            *word = (uint32_t)entry->target;
        }
    }
}

void unowa_flat_boot(void)
{
    load_module(&unowa_flat_modules[0]);
}

bool unowa_flat_load_overlay(uint32_t id)
{
    if (id + 1 >= unowa_flat_module_count) {
        return false;
    }
    load_module(&unowa_flat_modules[id + 1]);
    return true;
}

BOOL __wrap_NitroPort_OverlayInfo(FSOverlayInfo *p_ovi, MIProcessor target, FSOverlayID id);
BOOL __wrap_NitroPort_OverlayLoadImage(const FSOverlayInfo *p_ovi);
void __wrap_NitroPort_OverlayClearBss(const FSOverlayInfo *p_ovi);
void __real_OS_InitArena(void);
void __wrap_OS_InitArena(void);

BOOL __wrap_NitroPort_OverlayInfo(FSOverlayInfo *p_ovi, MIProcessor target, FSOverlayID id)
{
    const unowa_flat_module *module;

    if (target != MI_PROCESSOR_ARM9 || (uint32_t)id + 1 >= unowa_flat_module_count) {
        return FALSE;
    }
    module = &unowa_flat_modules[(uint32_t)id + 1];
    memset(p_ovi, 0, sizeof(*p_ovi));
    p_ovi->header.id = (u32)id;
    p_ovi->header.ram_address = unowa_ds_to_host(module->ram_start);
    p_ovi->header.ram_size = module->ram_end - module->ram_start;
    p_ovi->header.bss_size = module->bss_end - module->ram_end;
    if (module->sinit_count) {
        /* Native initialisers only (flat_memory.h). Pointing the SDK at the
         * DS table in the flat image would call ROM bytes: FS_StartOverlay
         * runs sinit_init..sinit_init_end, and the flat image is data. */
        p_ovi->header.sinit_init = &unowa_flat_sinit[module->sinit_first];
        p_ovi->header.sinit_init_end = &unowa_flat_sinit[module->sinit_first
                                                        + module->sinit_count];
    }
    p_ovi->header.file_id = (u32)id;
    /* The image is already decompressed: no FS_OVERLAY_FLAG_COMP, so
     * FS_StartOverlay does not run MIi_UncompressBackward over it. */
    p_ovi->header.flag = 0;
    p_ovi->target = MI_PROCESSOR_ARM9;
    return p_ovi->header.ram_address != NULL;
}

BOOL __wrap_NitroPort_OverlayLoadImage(const FSOverlayInfo *p_ovi)
{
    if (!unowa_flat_load_overlay(p_ovi->header.id)) {
        return FALSE;
    }
    NitroPort_Log("[unowa] overlay %u loaded at DS 0x%08X",
                  (unsigned)p_ovi->header.id,
                  (unsigned)unowa_flat_modules[p_ovi->header.id + 1].ram_start);
    return TRUE;
}

void __wrap_NitroPort_OverlayClearBss(const FSOverlayInfo *p_ovi)
{
    unsigned char *bss;

    if (p_ovi->header.ram_address == NULL) {
        return;
    }
    bss = p_ovi->header.ram_address + p_ovi->header.ram_size;
    memset(bss, 0, p_ovi->header.bss_size);
}

/*
 * sinjoh starts the main arena at the bottom of NitroPort_MainMem
 * (build/NitroSDK/libraries/os/src/os_arena.c:75-76, SDK_PORT_3DS), where
 * unowa now keeps the DS image. Start it where the DS does instead: above the
 * static module and the overlay windows. The high end stays sinjoh's
 * HW_MAIN_MEM_MAIN_END, the DS value (mmap_main.h:57-64).
 */
void __wrap_OS_InitArena(void)
{
    __real_OS_InitArena();
    OS_SetArenaLo(OS_ARENA_MAIN, SDK_MAIN_ARENA_LO);
}
