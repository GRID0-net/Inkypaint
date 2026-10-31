/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_LAUNCHER_H
#define INKYPAINT_LAUNCHER_H
/* Escape in the game window, started by the launcher (INKYPAINT_LAUNCHER=1): instead of quitting, the runtime creates
   <state>/<id>.quit-request beside the presence file and the launcher asks the player whether to close the game.
   Returns 1 when the launcher has the request (pending or just made), 0 when the game should quit at once (no launcher,
   or the file could not be created). */
int inkypaint_launcher_quit_request(void);
#endif
