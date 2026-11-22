/* unowa: adapted from sinjoh 1774be6 (GPL-3.0, via kohjoh) for two editions. */
#ifndef UNOWA_STORAGE_PATHS_H
#define UNOWA_STORAGE_PATHS_H

#include <stdbool.h>

typedef struct unowa_paths {
    char root[128];
    char log_file[160];
    char data_dir[160];
    char save_dir[160];
    char pack_file[192];
    char manifest_file[192];
    char save_file[192];
    bool bundled;
} unowa_paths;

/* Create the SD directory tree and fill in every path. Safe to call once at
 * startup; the returned pointer is owned by this module. */
const unowa_paths *unowa_paths_init(void);

const unowa_paths *unowa_paths_get(void);

#endif
