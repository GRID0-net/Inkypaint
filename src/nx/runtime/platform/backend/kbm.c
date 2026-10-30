/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Keyboard and mouse as player 1's controller (kbm.h).

   The keys: each control's bindings come from key_<control> in the game's .ini, empty for the defaults below, which
   are a PC shooter's (WASD to move, the mouse's buttons to shoot and throw, Shift to swim, Space to jump).

   The mouse aims through the controller's motion sensor. The game aims with the controller's turn when its motion
   controls are on, so the mouse turning the aim is the controller turning as far: left and right about the vertical,
   up and down about the controller's own crosswise axis. The controller it makes is held flat and pitched up or down
   as far as the vertical aim has turned it, its accelerometer reading gravity where that controller would, so the
   game's (and hid_motion's) fusion of the two finds nothing to correct and nothing drifts. Its readings are exact:
   no resting bias is learned from them and no small turn is taken for noise. A turn faster than the console's sensor
   reads (5 revolutions a second) is spread over the next samples instead of lost, so a flick turns as far as the
   mouse went */
#include "nx/runtime/platform/kbm.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "snx_settings.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#define BINDINGS   4      /* inputs a control takes */
#define WHEEL_HOLD 3      /* samples a wheel's turn holds its binding down */
#define MAX_REVS   4.75f  /* the fastest turn a sample reports, under hid_motion's 5 revolutions a second */
#define BASE_DEG   0.06f  /* degrees of turn per count at 100% */
#define MAX_PITCH  80.0f  /* the most the controller tilts, in degrees: the game's own aim stops long before */
#define PI_F       3.14159265358979f

static const char* const defaults[SETTINGS_KBM_COUNT] = {
    "F, Return",        /* A: interact, confirm */
    "Space, Backspace", /* B: jump, back */
    "Tab",              /* X: the map */
    "R",                /* Y: the camera behind the player */
    "Q",                /* L */
    "Mouse Right",      /* R: the sub weapon */
    "Left Shift",       /* ZL: swim */
    "Mouse Left",       /* ZR: shoot */
    "P",                /* Plus */
    "M",                /* Minus */
    "C",                /* L3 */
    "E",                /* R3: the special weapon */
    "Up",               /* the D-pad: the signals */
    "Down",
    "Left",
    "Right",
    "W", /* the left stick */
    "S",
    "A",
    "D",
    "I", /* the right stick */
    "K",
    "J",
    "L",
    "Left Alt", /* the mouse, let go and taken again */
};
/* in the game's menus (kbm_set_menu): a PC game's menu keys, Return to pick, Backspace or the right mouse button
   back, Q and E the shoulder buttons that turn the tabs, the arrows or WASD to move the cursor */
static const char* const menu_defaults[SETTINGS_KBM_COUNT] = {
    "Return, Space, F",          /* A */
    "Backspace, Mouse Right",    /* B */
    "X, Tab",                    /* X */
    "Y",                         /* Y */
    "Q",                         /* L */
    "E",                         /* R */
    "Z",                         /* ZL */
    "C",                         /* ZR */
    "P",                         /* Plus */
    "M",                         /* Minus */
    "Left Shift",                /* L3 */
    "Left Ctrl",                 /* R3 */
    "Up, W",                     /* the D-pad */
    "Down, S",
    "Left, A",
    "Right, D",
    "None", /* the left stick: the D-pad moves the cursor */
    "None",
    "None",
    "None",
    "I", /* the right stick */
    "K",
    "J",
    "L",
    "Left Alt",
};
/* each control's bit, in the list's order: A B X Y L R ZL ZR Plus Minus L3 R3, the D-pad up down left right, the
   left stick's up down left right and the right one's (input_key_sticks: left, up, right, down from bit 40) */
static const signed char bits[SETTINGS_KBM_COUNT] = {0,  1,  2,  3,  6,  7,  8,  9,  10, 11, 4, 5, 13,
                                                     15, 12, 14, 41, 43, 40, 42, 45, 47, 44, 46, -1};
#define CURSOR (SETTINGS_KBM_COUNT - 1)

static const char* const mouse_names[] = {"Mouse Left", "Mouse Middle", "Mouse Right", "Mouse X1", "Mouse X2"};

static short bound[2][SETTINGS_KBM_COUNT][BINDINGS]; /* codes, -1 for none: in play, and in the menus */
static int bound_ready, in_menu;
static unsigned char held[KBM_CODES];
static int wheel_left[2]; /* samples the wheel's up and down bindings stay down */
static float moved_x, moved_y;
static float owed_yaw, owed_pitch; /* degrees the mouse turned that no sample has reported yet */
static float pitch;                /* degrees the controller is tilted up */

u64 kbm_control_bit(int control) {
    return control >= 0 && control < SETTINGS_KBM_COUNT && bits[control] >= 0 ? 1ull << bits[control] : 0;
}
const char* kbm_default_binding(int control) {
    return control >= 0 && control < SETTINGS_KBM_COUNT ? defaults[control] : "";
}
const char* kbm_menu_default_binding(int control) {
    return control >= 0 && control < SETTINGS_KBM_COUNT ? menu_defaults[control] : "";
}
void kbm_set_menu(int on) {
    in_menu = on != 0;
}

/* The names key_<control> holds, as the launchers write them: SDL's names of its scancodes before SDL 3 gave some of
   them the system's own words (Left Option on a Mac, Left Windows on Windows), whose SDL_GetScancodeFromName then
   knows only those. So the keys whose names differ between systems are looked up here first, under every name they
   go by, and the rest by SDL */
static const struct {
    const char* name;
    SDL_Scancode code;
} named[] = {
    {"Left Alt", SDL_SCANCODE_LALT},       {"Left Option", SDL_SCANCODE_LALT},   {"Right Alt", SDL_SCANCODE_RALT},
    {"Right Option", SDL_SCANCODE_RALT},   {"AltGr", SDL_SCANCODE_RALT},         {"Left GUI", SDL_SCANCODE_LGUI},
    {"Left Command", SDL_SCANCODE_LGUI},   {"Left Windows", SDL_SCANCODE_LGUI},  {"Left Super", SDL_SCANCODE_LGUI},
    {"Left Meta", SDL_SCANCODE_LGUI},      {"Right GUI", SDL_SCANCODE_RGUI},     {"Right Command", SDL_SCANCODE_RGUI},
    {"Right Windows", SDL_SCANCODE_RGUI},  {"Right Super", SDL_SCANCODE_RGUI},   {"Right Meta", SDL_SCANCODE_RGUI},
    {"Left Ctrl", SDL_SCANCODE_LCTRL},     {"Left Control", SDL_SCANCODE_LCTRL}, {"Right Ctrl", SDL_SCANCODE_RCTRL},
    {"Right Control", SDL_SCANCODE_RCTRL}, {"Left Shift", SDL_SCANCODE_LSHIFT},  {"Right Shift", SDL_SCANCODE_RSHIFT},
    {"Return", SDL_SCANCODE_RETURN},       {"Enter", SDL_SCANCODE_RETURN},       {"Backspace", SDL_SCANCODE_BACKSPACE},
    {"Delete", SDL_SCANCODE_DELETE},       {"Escape", SDL_SCANCODE_ESCAPE},      {"Space", SDL_SCANCODE_SPACE},
    {"Tab", SDL_SCANCODE_TAB},             {"CapsLock", SDL_SCANCODE_CAPSLOCK},  {"Numlock", SDL_SCANCODE_NUMLOCKCLEAR},
    {"Keypad Enter", SDL_SCANCODE_KP_ENTER},
};
int kbm_code(const char* name) {
    size_t i;
    SDL_Scancode s;
    for (i = 0; i < sizeof mouse_names / sizeof *mouse_names; i++)
        if (!SDL_strcasecmp(name, mouse_names[i])) return KBM_MOUSE + 1 + (int)i;
    if (!SDL_strcasecmp(name, "Wheel Up")) return KBM_WHEEL_UP;
    if (!SDL_strcasecmp(name, "Wheel Down")) return KBM_WHEEL_DOWN;
    for (i = 0; i < sizeof named / sizeof *named; i++)
        if (!SDL_strcasecmp(name, named[i].name)) return (int)named[i].code;
    s = SDL_GetScancodeFromName(name);
    return s > SDL_SCANCODE_UNKNOWN && s < KBM_MOUSE ? (int)s : -1;
}
const char* kbm_code_name(int code) {
    size_t i;
    if (code > KBM_MOUSE && code <= KBM_MOUSE + 5) return mouse_names[code - KBM_MOUSE - 1];
    if (code == KBM_WHEEL_UP) return "Wheel Up";
    if (code == KBM_WHEEL_DOWN) return "Wheel Down";
    for (i = 0; i < sizeof named / sizeof *named; i++)
        if ((int)named[i].code == code) return named[i].name;
    if (code > 0 && code < KBM_MOUSE) return SDL_GetScancodeName((SDL_Scancode)code);
    return "";
}

/* one control's bindings from text: names with a comma between two, None for none */
static void bind(int menu, int control, const char* text) {
    char name[64];
    int n = 0;
    const char* at = text;
    for (int i = 0; i < BINDINGS; i++) bound[menu][control][i] = -1;
    while (*at && n < BINDINGS) {
        size_t len = strcspn(at, ",");
        const char *start = at, *end = at + len;
        while (start < end && (*start == ' ' || *start == '\t')) start++;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if (end > start && (size_t)(end - start) < sizeof name) {
            int code;
            memcpy(name, start, (size_t)(end - start));
            name[end - start] = 0;
            code = SDL_strcasecmp(name, "None") ? kbm_code(name) : -1;
            if (code >= 0)
                bound[menu][control][n++] = (short)code;
            else if (SDL_strcasecmp(name, "None"))
                WARN("input: %skey_%d's \"%s\" names no key or mouse button: left out\n", menu ? "menu_" : "", control,
                     name);
        }
        at += len;
        if (*at == ',') at++;
    }
}
static void bind_all(void) {
    for (int c = 0; c < SETTINGS_KBM_COUNT; c++) {
        bind(0, c, g_settings.keyboard_binding[c][0] ? g_settings.keyboard_binding[c] : defaults[c]);
        bind(1, c, g_settings.menu_binding[c][0] ? g_settings.menu_binding[c] : menu_defaults[c]);
    }
    bound_ready = 1;
}
/* settings.txt's set for every key_<control>: the bindings read again, all of them */
void kbm_bindings_set(const IniOpt* o, const char* v) {
    (void)o, (void)v;
    bind_all();
}

static int bound_to(int control, int code) {
    for (int i = 0; i < BINDINGS; i++)
/* a setting of the Keyboard and mouse section, read from the .ini or the command line: options.c hands it here through
   backend_ini, which nothing else in this section was reading */
int kbm_ini(const char* key, const char* value) {
    const IniOpt* o = ini_find(g_kbm_ini, key);
    if (o) ini_read(o, value);
    return o != NULL;
}
        if (bound[in_menu][control][i] == code) return 1;
    return 0;
}

static struct { u64 last_pad; int pad_seen, blocked; } g_priority;
int kbm_enabled(void) { return g_settings.keyboard_and_mouse_input && !g_priority.blocked; }
void kbm_controller_activity(int active, u64 now_ns) {
    if (active) { g_priority.last_pad = now_ns; g_priority.pad_seen = 1; }
    int blocked = !g_settings.keyboard_and_mouse_input ||
                  (g_priority.pad_seen && now_ns - g_priority.last_pad < 1500000000ull);
    if (blocked != g_priority.blocked) {
        kbm_release_all();
        kbm_aim_reset();
    }
    g_priority.blocked = blocked;
}

int kbm_input(int code, int down) {
    if (!kbm_enabled()) return 0;
    if (!bound_ready) bind_all();
    if (code < 0 || code >= KBM_CODES) return 0;
    int was = held[code];
    held[code] = (unsigned char)(down != 0);
    return down && !was && bound_to(CURSOR, code);
}
void kbm_wheel(float y) {
    if (!kbm_enabled()) return;
    if (y > 0) wheel_left[0] = WHEEL_HOLD;
    if (y < 0) wheel_left[1] = WHEEL_HOLD;
}
void kbm_release_all(void) {
    memset(held, 0, sizeof held);
    wheel_left[0] = wheel_left[1] = 0;
    moved_x = moved_y = 0;
}

u64 kbm_buttons(void) {
    if (!kbm_enabled()) return 0;
    u64 out = 0;
    if (!bound_ready) bind_all();
    held[KBM_WHEEL_UP] = wheel_left[0] > 0;
    held[KBM_WHEEL_DOWN] = wheel_left[1] > 0;
    for (int c = 0; c < SETTINGS_KBM_COUNT; c++)
        for (int i = 0; i < BINDINGS; i++)
            if (bound[in_menu][c][i] >= 0 && held[bound[in_menu][c][i]]) {
                out |= kbm_control_bit(c);
                break;
            }
    for (int w = 0; w < 2; w++)
        if (wheel_left[w] > 0) wheel_left[w]--;
    return out;
}

void kbm_mouse_moved(float dx, float dy) {
    if (!kbm_enabled()) return;
    moved_x += dx;
    moved_y += dy;
}
void kbm_aim_reset(void) {
    owed_yaw = owed_pitch = pitch = 0;
    moved_x = moved_y = 0;
}

void kbm_motion(PadMotion* m, u64 dt_ns) {
    float dt, scale, share, yaw, up, size, before, rp, revs_yaw, revs_pitch;
    if (!kbm_enabled() || !g_settings.mouse_aim) {
        kbm_aim_reset();
        return;
    }
    dt = (float)(dt_ns < 1000000u ? 1000000u : dt_ns > 50000000u ? 50000000u : dt_ns) * 1e-9f;
    scale = BASE_DEG * (float)g_settings.mouse_sensitivity / 100.0f;
    /* right turns the aim right: a turn clockwise seen from above, negative about the vertical. Up tilts the
       controller's far end up: positive about its crosswise axis */
    owed_yaw -= moved_x * scale;
    owed_pitch -= moved_y * scale * (float)g_settings.mouse_vertical_sensitivity / 100.0f * (g_settings.mouse_invert_vertical ? -1.0f : 1.0f);
    moved_x = moved_y = 0;
    /* this sample's share: all of it, up to the fastest turn the sensor reports */
    size = sqrtf(owed_yaw * owed_yaw + owed_pitch * owed_pitch);
    share = size > MAX_REVS * 360.0f * dt ? MAX_REVS * 360.0f * dt / size : 1.0f;
    yaw = owed_yaw * share;
    up = owed_pitch * share;
    owed_yaw -= yaw;
    owed_pitch -= up;
    before = pitch;
    pitch = fminf(fmaxf(pitch + up, -MAX_PITCH), MAX_PITCH);
    if (pitch != before + up) owed_pitch = 0; /* tilted as far as it goes: the rest of the way up or down is lost */
    up = pitch - before;
    revs_yaw = yaw / 360.0f / dt;
    revs_pitch = up / 360.0f / dt;
    rp = pitch * PI_F / 180.0f;
    /* the vertical, in the tilted controller's axes: (0, sin p, cos p); its crosswise axis is its own x */
    if (m->valid) {
        /* a controller with a sensor of its own: the mouse's turn on top of its, about its own axes */
        m->angular_velocity[0] += revs_pitch;
        m->angular_velocity[2] += revs_yaw;
        return;
    }
    m->valid = 1;
    m->exact = 1;
    m->acceleration[0] = 0.0f;
    m->acceleration[1] = -sinf(rp);
    m->acceleration[2] = -cosf(rp);
    m->angular_velocity[0] = revs_pitch;
    m->angular_velocity[1] = revs_yaw * sinf(rp);
    m->angular_velocity[2] = revs_yaw * cosf(rp);
}
