/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/inkypaint_launcher.h"
#include "nx/runtime/main.h"
#include "nx/runtime/platform/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int inkypaint_launcher_quit_request(void) {
    const char* env = getenv("INKYPAINT_LAUNCHER");
    char path[1400];
    FILE* f;
    if (!env || strcmp(env, "1") != 0) {
        LOG("launcher: Escape closes the game at once (not started by the launcher)\n");
        return 0;
    }
    snprintf(path, sizeof path, "%s%s.quit-request", g_statedir, g_exename);
    f = fopen(path, "rb");
    if (f) { /* still pending: the launcher's popup has not taken it yet */
        fclose(f);
        LOG("launcher: Escape, a close request is still waiting for the launcher\n");
        return 1;
    }
    f = fopen(path, "wb");
    if (!f) {
        LOG("launcher: Escape closes the game, the request file could not be made\n");
        return 0; /* never trap the player in the game */
    }
    if (fclose(f)) {
        (void)remove(path);
        LOG("launcher: Escape closes the game, the request file could not be written\n");
        return 0;
    }
    LOG("launcher: Escape asked the launcher whether to close the game\n");
    return 1;
}
