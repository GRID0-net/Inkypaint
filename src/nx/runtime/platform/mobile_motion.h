/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_MOBILE_MOTION_H
#define INKYPAINT_MOBILE_MOTION_H
#include "nx/hle/services/hid.h"
#include <SDL3/SDL.h>
/* Main-thread poll. A disabled/unavailable device sensor leaves physical-pad
   motion untouched. No physical controller: phone motion supplies player 1. */
void mobile_motion_sample(SDL_Window* window, int active, PadMotion* out);
#endif
