/*
 * paths.c: storage roots for the two unowa editions.
 *
 * Adapted from sinjoh 1774be6 (GPL-3.0, via kohjoh). A .3dsx keeps its data on
 * SD under sdmc:/3ds/unowa/<edition>/; a future CIA may use RomFS for the pack
 * and the title save archive for saves. No ROM-derived data is committed.
 */

#include "storage/paths.h"

#include "diagnostics/log.h"
#include "runtime/kj_bridge.h"

#include <3ds.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static unowa_paths s_paths;

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void ensure_dir(const char *path)
{
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        kj_log("[paths] cannot create %s (errno %d)", path, errno);
    }
}

const unowa_paths *unowa_paths_init(void)
{
    const char *edition = kj_ds_edition_name();

    snprintf(s_paths.root, sizeof(s_paths.root), "sdmc:/3ds/unowa");
    ensure_dir("sdmc:/3ds");
    ensure_dir(s_paths.root);
    ensure_dir("sdmc:/3ds/unowa/data");
    ensure_dir("sdmc:/3ds/unowa/saves");
    ensure_dir("sdmc:/3ds/unowa/logs");

    snprintf(s_paths.data_dir, sizeof(s_paths.data_dir), "%s/data", s_paths.root);
    snprintf(s_paths.save_dir, sizeof(s_paths.save_dir), "%s/saves", s_paths.root);
    snprintf(s_paths.log_file, sizeof(s_paths.log_file), "%s/logs/%s.log", s_paths.root, edition);

    if (R_SUCCEEDED(romfsInit()) && file_exists("romfs:/data/game.pak")) {
        s_paths.bundled = true;
        snprintf(s_paths.pack_file, sizeof(s_paths.pack_file), "romfs:/data/game.pak");
        snprintf(s_paths.manifest_file, sizeof(s_paths.manifest_file),
                 "romfs:/data/game.manifest.json");
    } else {
        s_paths.bundled = false;
        snprintf(s_paths.pack_file, sizeof(s_paths.pack_file), "%s/data/%s.pak",
                 s_paths.data_dir, edition);
        snprintf(s_paths.manifest_file, sizeof(s_paths.manifest_file),
                 "%s/data/%s.manifest.json", s_paths.data_dir, edition);
    }
    snprintf(s_paths.save_file, sizeof(s_paths.save_file), "%s/saves/%s.sav",
             s_paths.save_dir, edition);
    return &s_paths;
}

const unowa_paths *unowa_paths_get(void)
{
    return &s_paths;
}
