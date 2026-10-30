/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Keyboard and mouse as player 1's controller (backend/kbm.c): each control pressed by the keys and mouse buttons the
   game's .ini binds to it (key_<control>, [Keyboard and mouse]), and the mouse, once a click in the window has taken
   it, turned into the controller's motion sensor, so the game's own motion aiming aims with it */
#ifndef SNX_KBM_H
#define SNX_KBM_H
#include "common/cpu/aarch64/cpu.h"
#include "nx/hle/services/hid.h" /* PadMotion */

/* an input's code: an SDL scancode, or a mouse button (SDL's 1 to 5) or the wheel after them */
#define KBM_MOUSE      512
#define KBM_WHEEL_UP   (KBM_MOUSE + 8)
#define KBM_WHEEL_DOWN (KBM_MOUSE + 9)
#define KBM_CODES      (KBM_MOUSE + 10)

/* the bit each control sets: the Npad's buttons (0 to 15) and the stick keys (40 to 47, input_key_sticks) */
u64 kbm_control_bit(int control); /* control: 0 to SETTINGS_KBM_COUNT - 1, in settings.txt's list kbm */
/* the code of an input's name (W, Left Shift, Mouse Left, Wheel Up), -1 for none; and a code's name */
int kbm_code(const char* name);
const char* kbm_code_name(int code);
/* the bindings of a control by default, as key_<control> holds them */
const char* kbm_default_binding(int control);
/* in the game's menus (menu_key_<control>): the bindings by default, and the menus' set on or off. The frontend
   turns it on while inkypaint_ui.c says a menu is up */
const char* kbm_menu_default_binding(int control);
void kbm_set_menu(int on);
/* Physical buttons/sticks take priority, with a short idle grace. Releases old
   keys and mouse aim on handoff; held keys never reappear when control returns. */
void kbm_controller_activity(int active, u64 now_ns);
int kbm_enabled(void);
/* a key of the .ini's Keyboard and mouse section and its value: 1 when it is one of that section's */
int kbm_ini(const char* key, const char* value);

/* an input going down or up. 1 when it was Free the mouse's going down: the caller lets the mouse go (or takes it) */
int kbm_input(int code, int down);
void kbm_wheel(float y);   /* a wheel's turn: its direction's binding pressed for a few samples */
void kbm_release_all(void); /* every input up: the window lost the focus */
/* the controls pressed now, as keys_held's bits; a wheel's press counts down one sample */
u64 kbm_buttons(void);

/* the mouse's movement while it is taken, in its counts */
void kbm_mouse_moved(float dx, float dy);
/* the aim the mouse moved since the last call, as a motion sensor over dt_ns: m becomes the controller held flat and
   pitched as the vertical aim has turned it, turning as the mouse did, when it has no sensor of its own; a real
   sensor gets the mouse's turn added. Nothing when mouse aim is off or nothing moved and there is no aim to keep */
void kbm_motion(PadMotion* m, u64 dt_ns);
void kbm_aim_reset(void); /* the aim's pitch and what it still had to turn, forgotten */

#endif
