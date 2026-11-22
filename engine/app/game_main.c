/*
 * The game's 3DS entry point.
 *
 * Order matters:
 *   1. the M0 renderer and SD paths, so a failure can still be reported, and
 *      the V-blank pacing clock that keeps the game tick at the display rate;
 *   2. sinjoh's virtual ARM7 services, plus the Gen V NVRAM handler (PXI tag
 *      4), before any game code issues a PXI request (kohjoh kj_boot.c:112);
 *   3. the flat DS main-RAM image, before any game code reads DS data;
 *   4. the DS firmware state (engine/runtime/ds_boot.c), before the game's
 *      first key read, which takes an all-zero key register as every button
 *      held;
 *   5. the content pack, which FS reads open from;
 *   6. NitroMain, which does not return.
 */
#include "runtime/flat_memory.h"
#include "runtime/frame_pacing.h"

#include <stddef.h>

typedef unsigned int u32;
typedef int BOOL;

extern void gfxInitDefault(void);
extern void *unowa_paths_init(void);
extern int unowa_renderer_init(void);
extern void NitroPort_InitArm7Services(void);
extern void NitroPort_SetArm7Handler(int tag, void (*handler)(int, u32, BOOL));
extern void unowa_handle_nvram(int tag, u32 data, BOOL err);
extern void unowa_boot_firmware(void);
extern const char *kj_ds_open_pack(const char *pak, const char *manifest);
extern void kj_log(const char *format, ...);
extern void NitroMain(void);

#define NVRAM_PXI_TAG 4

int main(void)
{
    const char *pack_error;

    gfxInitDefault();
    unowa_paths_init();
    if (!unowa_renderer_init()) {
        return 1;
    }
    unowa_frame_pacing_init();
    NitroPort_InitArm7Services();
    NitroPort_SetArm7Handler(NVRAM_PXI_TAG, unowa_handle_nvram);
    unowa_flat_boot();
    unowa_boot_firmware();
    pack_error = kj_ds_open_pack("sdmc:/3ds/unowa/data/black.pak",
                                 "sdmc:/3ds/unowa/data/black.manifest.json");
    if (pack_error != NULL) {
        kj_log("[pack] %s", pack_error);
        return 2;
    }
    NitroMain();
    return 0;
}
