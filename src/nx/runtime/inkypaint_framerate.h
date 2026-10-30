/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_FRAMERATE_H
#define INKYPAINT_FRAMERATE_H
/* Exact-version development hooks, inactive by default. INKYPAINT_FRAME_TRACE enables graph diagnostics.
   INKYPAINT_RENDER_HZ=61..240 enables experimental task gating and render-camera interpolation. Skeletal interpolation is unfinished. */
void inkypaint_framerate_init(void);
int inkypaint_framerate_target(void);
/* what the calling thread runs: 1 a hooked job in a frame that steps the game, 2 one in an extra frame drawn
   between steps; outside the hooked jobs 3 while the frame being built steps, 4 while it is an extra one (other
   threads' work can straddle frames); 0 without the experiment */
int inkypaint_framerate_phase(void);
#endif
