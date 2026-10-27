/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_DISPLAY_H
#define INKYPAINT_DISPLAY_H
#include <stdint.h>
/* After loader_load and jit_init, before any guest thread starts. Validates the 11.3.0 hook sites. */
void inkypaint_display_init(void);
/* Whether the projection hooks are installed, and the matching output aspect (0.25 through 4). */
int inkypaint_display_fill(void);
float inkypaint_display_aspect(void);
/* Scale final UI draws' viewport and scissor together; intermediate UI textures keep logical coordinates. */
int inkypaint_display_ui_scale(uint64_t vertex_shader, float* x, float* y);
#endif
