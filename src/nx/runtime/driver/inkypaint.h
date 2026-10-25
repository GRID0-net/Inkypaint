/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* inkypaint targets one game and one build of it: Splatoon 3 with its 11.3.0 update. Every address, hook and
   workaround inkypaint adds is written against that executable, so the driver refuses anything else up front
   instead of producing a port that half works */
#ifndef INKYPAINT_TARGET_H
#define INKYPAINT_TARGET_H

#include "nx/runtime/driver/container.h"

#define INKYPAINT_TITLE_ID 0x0100C2500FC20000ull     /* the base game */
#define INKYPAINT_UPDATE_ID 0x0100C2500FC20800ull    /* its update */
#define INKYPAINT_UPDATE_VERSION 2752512ull          /* v2752512, the update's content meta for 11.3.0 */
#define INKYPAINT_UPDATE_NAME "11.3.0"

/* 1 when INKYPAINT_ANY_TITLE is set in the environment: the test suite's synthetic programs and homebrew go through
   the driver as they do upstream. Nothing a player does sets it */
int inkypaint_any_title(void);
/* 0 when dump is Splatoon 3's base with the 11.3.0 update over it, else nonzero with a line saying what is wrong
   and what to give instead */
int inkypaint_check_dump(const SnxDump* dump);

#endif
