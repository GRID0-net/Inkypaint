/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/driver/inkypaint.h"

#include <stdio.h>
#include <stdlib.h>

int inkypaint_any_title(void) {
    const char* any = getenv("INKYPAINT_ANY_TITLE");
    return any && *any && *any != '0';
}

int inkypaint_check_dump(const SnxDump* dump) {
    char title[17];
    if (inkypaint_any_title()) return 0;
    if (!dump->program.found || dump->program.title_id != INKYPAINT_TITLE_ID) {
        snx_title_text(dump->program.found ? dump->program.title_id : 0, title);
        return snx_error("inkypaint only runs Splatoon 3 (0100C2500FC20000), and this dump is %s. Give the base "
                         "game's NSP or XCI, with the %s update as --update",
                         dump->program.found ? title : "not a game", INKYPAINT_UPDATE_NAME);
    }
    if (!dump->patch.found)
        return snx_error("Splatoon 3 needs its %s update: give its NSP with --update", INKYPAINT_UPDATE_NAME);
    if (dump->version != INKYPAINT_UPDATE_VERSION) {
        if (dump->version == SNX_NO_VERSION)
            return snx_error("the update's version cannot be read. inkypaint needs Splatoon 3 %s (v%llu)",
                             INKYPAINT_UPDATE_NAME, INKYPAINT_UPDATE_VERSION);
        return snx_error("this is Splatoon 3's update v%llu, and inkypaint is built for %s (v%llu) alone",
                         (unsigned long long)dump->version, INKYPAINT_UPDATE_NAME, INKYPAINT_UPDATE_VERSION);
    }
    return 0;
}
