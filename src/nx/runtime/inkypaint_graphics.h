/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_GRAPHICS_H
#define INKYPAINT_GRAPHICS_H
#include <stdint.h>
void inkypaint_graphics_init(void);
/* Validated engine hooks own resolution, leaving GPU data textures at their intended size. */
int inkypaint_graphics_engine_scale(void);
/* Completed GPU work only. Never waits for a query or changes the simulation rate. */
void inkypaint_graphics_gpu_time(uint64_t ns);
#endif
