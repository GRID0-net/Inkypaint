/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_UI_H
#define INKYPAINT_UI_H
#include "common/cpu/aarch64/cpu.h"
/* Splatoon 3 11.3.0's menus, read from nn::ui2d and the game's buttons as it draws them. After loader_load and
   jit_init, before any guest thread starts: hooks them when menu input, the controller layout option or ui_census needs them */
void inkypaint_ui_init(void);
/* the census (ui_census=1, F9): every layout drawn in the next frame, with its visible panes (name, place in the
   1920x1080 layout space, alpha and a text box's text), appended to uicensus.txt in the
   game's folder */
void inkypaint_ui_census_request(void);
/* A supported cursor, X-menu tab bar or lobby rooms/friends browser was drawn recently. */
int inkypaint_ui_menu_active(void);
/* A visible A-button interaction guide, separate from full menu navigation. */
int inkypaint_ui_interaction_active(void);
/* the pointer at (x, y) in the layout space (1920x1080 around the center, y up), inside the picture or not, moved
   since the last call, clicked: the Npad buttons to hold now, the game's own navigation to the button under it
   (and A on a click). X-menu tab clicks use L/R without A. Called once a poll; 0 when no menu is up */
u64 inkypaint_ui_pointer(float x, float y, int inside, int moved, int click);
#endif
