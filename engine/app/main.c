/*
 * main.c: unowa M0 bootstrap.
 *
 * Native ARM11 entry, Old 3DS baseline (268 MHz, ordinary 64 MiB application
 * memory, no stereo, no L2/speed-up). The game owns its frame loop in M2+;
 * M0 runs a minimal presentation loop to prove the platform skeleton, the
 * asynchronous SD log, 30 Hz pacing and the GPU-native render path.
 *
 * Modelled on sinjoh's engine/app/main.c (GPL-3.0, via kohjoh). The 3DS is the
 * platform side of kj_bridge.h; no NitroSDK header is included here.
 */

#include "build_id.h"
#include "diagnostics/log.h"
#include "graphics/renderer.h"
#include "runtime/kj_bridge.h"
#include "storage/paths.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The game thread is this one. DS stacks were sized for 16 KB of DTCM, but
 * GCC code needs far more (kohjoh/hgss-3ds measured 512 KB insufficient). */
u32 __stacksize__ = 1024 * 1024;

static volatile bool s_exiting;

static void fatal_screen(const char *title, const char *detail)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    printf("\x1b[1;1Hunowa %s\n\n%s\n\n%s\n\nPress START to exit.\n", kj_ds_edition_name(),
           title, detail != NULL ? detail : "");
    kj_log("[fatal] %s: %s", title, detail != NULL ? detail : "");
    kj_log_flush_now();
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    exit(1);
}

static void log_environment(void)
{
    bool is_new = false;
    APT_CheckNew3DS(&is_new);
    const u32 app_kib = osGetMemRegionSize(MEMREGION_APPLICATION) / 1024;

    kj_log("[app] %s, %s hardware held at Old 3DS baseline", UNOWA_BUILD_ID,
           is_new ? "New 3DS" : "Old 3DS");
    kj_log("[app] edition=%s, application region=%lu KiB, linear free=%lu KiB, VRAM free=%lu KiB",
           kj_ds_edition_name(), (unsigned long)app_kib,
           (unsigned long)(linearSpaceFree() / 1024), (unsigned long)(vramSpaceFree() / 1024));
#ifdef UNOWA_PROFILE
    kj_log("[app] instrumented build: [pace] counters enabled");
#else
    kj_log("[app] release build: instrumentation compiled out");
#endif
}

int main(void)
{
    /* O3DS baseline: never request the New 3DS clock/L2 even on a New 3DS. */
    osSetSpeedupEnable(false);

    const unowa_paths *paths = unowa_paths_init();
    kj_log_init(paths->log_file);
    log_environment();

    gfxInitDefault();
    gfxSet3D(false);

    if (!unowa_renderer_init()) {
        gfxExit();
        fatal_screen("The GPU renderer could not start.", "See the log for details.");
    }

    u64 loop_start = svcGetSystemTick();
    u32 total_iterations = 0;
    u32 total_presented = 0;
    u32 total_skipped = 0;
#ifdef UNOWA_PROFILE
    u64 last_report = loop_start;
    u32 iterations = 0;
    u32 presented = 0;
    u32 skipped = 0;
#endif

    kj_log("[app] starting M0 presentation loop (30 Hz)");

    while (aptMainLoop()) {
        hidScanInput();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) {
            s_exiting = true;
            break;
        }

        /* Two clear colours alternate so a capture proves pictures change. */
        const uint32_t shade = (total_presented & 1u) != 0 ? 0x1B2A4AFFu : 0x101830FFu;
        if (unowa_renderer_present(shade, 0x181018FFu)) {
            ++total_presented;
#ifdef UNOWA_PROFILE
            ++presented;
#endif
        } else {
            ++total_skipped;
#ifdef UNOWA_PROFILE
            ++skipped;
#endif
        }
        ++total_iterations;

#ifdef UNOWA_PROFILE
        ++iterations;
        const u64 now = svcGetSystemTick();
        if (now - last_report >= SYSCLOCK_ARM11 / 2) {
            const double seconds = (double)(now - last_report) / (double)SYSCLOCK_ARM11;
            kj_log("[pace] iterations=%lu presented=%lu skipped=%lu, %.2f present/s",
                   (unsigned long)iterations, (unsigned long)presented, (unsigned long)skipped,
                   (double)presented / seconds);
            last_report = now;
            iterations = 0;
            presented = 0;
            skipped = 0;
        }
#endif
    }

    const u64 elapsed = svcGetSystemTick() - loop_start;
    kj_log("[app] loop ended after %lu iterations, %lu presented, %lu skipped, %.2f s%s",
           (unsigned long)total_iterations,
           (unsigned long)total_presented, (unsigned long)total_skipped,
           (double)elapsed / (double)SYSCLOCK_ARM11, s_exiting ? " (START)" : "");

    unowa_renderer_exit();
    kj_log_shutdown();
    romfsExit();
    gfxExit();
    return 0;
}
