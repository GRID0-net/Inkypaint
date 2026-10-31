/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_PRESENCE_H
#define INKYPAINT_PRESENCE_H
/* Splatoon 3 11.3.0's scene, battle rule and stage, written to <state>/<id>.presence.json for the launcher's Discord
   rich presence (discord_presence_status). init after loader_load, tick about once a second on the frontend's
   thread, exit at a clean exit (removes the file). Another title, or the setting off: nothing is written. */
void inkypaint_presence_init(void);
void inkypaint_presence_tick(void);
void inkypaint_presence_exit(void);
#endif
