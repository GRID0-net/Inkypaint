/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_MOUSELOOK_H
#define INKYPAINT_MOUSELOOK_H
/* After loader_load and jit_init, before any guest thread starts. With native_mouse_camera on, hooks the player
   camera's per-frame aim update so the taken mouse turns it directly. Off, nothing is hooked. */
void inkypaint_mouselook_init(void);
#endif
