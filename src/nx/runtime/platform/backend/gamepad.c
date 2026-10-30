/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "nx/runtime/platform/gamepad.h"
#include "nx/runtime/options.h"
#include "nx/runtime/main.h"
#include "nx/runtime/platform/backend.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/ui.h"
#include "common/utilities/common.h"
#include "common/utilities/pad_sdl.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Gamepad* g_pads[SNX_GAMEPADS];
/* each pad's sensor readings since the game's last sample (gamepad_sensor_event) */
typedef struct {
    float sum[3];
    int n;
} SensorSum;
enum { SUM_MAIN, SUM_LEFT, SUM_RIGHT, SUM_SENSORS };
static SensorSum g_accel_sum[SNX_GAMEPADS][SUM_SENSORS], g_gyro_sum[SNX_GAMEPADS][SUM_SENSORS];
static char g_names[SNX_GAMEPADS][64]; /* theirs, for the game's menu, which the renderer draws on its thread */
static int g_labels[SNX_GAMEPADS][4], g_symmetric[SNX_GAMEPADS]; /* and how its button map screen draws them */
static struct { int menu; unsigned blocked[SNX_GAMEPADS]; } g_menu_layout;
static unsigned g_held[SNX_GAMEPADS]; /* the controls each holds, a bit each, as last polled */
#define UNSET SNX_PAD_UNSET
/* what the settings' maps read into (their values in g_settings) */
static SnxGamepadConfig g_cfg = {
    {{UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET}}};

/* SDL drops a gamepad's presses while no window of the program has the focus, unless told otherwise; set before it
   starts its gamepads (the .ini is read first), and at once on a change */
void gamepad_background_set(const IniOpt* o, const char* v) {
    (void)o;
    (void)v;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, g_settings.background_input ? "1" : "0");
}
/* SDL decides whether two Joy-Cons form one gamepad when its joystick subsystem starts. */
void gamepad_joycon_pair_set(const IniOpt* o, const char* v) {
    (void)o;
    (void)v;
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, g_settings.pair_joy_cons ? "1" : "0");
}
/* a map that cannot be read: the word that cannot, and the map that stays, as the .ini writes it (whose of, when it is
   another key's) */
static void say_map_kept(const char* key, const char* word, const char* of, const SnxPadMap* kept) {
    char text[256];
    snx_pad_map_write(kept, text, sizeof text);
    WARN("settings: %s's \"%s\" is not a control and the button it sends (south:B): it keeps %s\"%s\"%s\n", key, word,
         of, text, text[0] ? "" : ", each control sending the button in its place");
}
/* the player's map: what it does not name sends the button in its place. One that cannot be read is said, and changes
   nothing */
void gamepad_map_set(const IniOpt* o, const char* v) {
    SnxPadMap map;
    char word[64];
    (void)o;
    snx_pad_map_unset(&map);
    if (snx_pad_map_read(&map, v, word, sizeof word))
        say_map_kept("gamepad_map", word, "", &g_cfg.own);
    else
        g_cfg.own = map;
}
/* the players' own (gamepad.h), from their keys gamepad_p<N>_...: which player a key is */
#define PLAYER_DEFAULT                                                                                                 \
    {                                                                                                                  \
        0, {                                                                                                           \
            {                                                                                                          \
                UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET, UNSET,      \
                    UNSET, UNSET                                                                                       \
            }                                                                                                          \
        }                                                                                                              \
    }
static SnxPlayerConfig g_players[SNX_GAMEPADS] = {PLAYER_DEFAULT, PLAYER_DEFAULT, PLAYER_DEFAULT, PLAYER_DEFAULT,
                                                  PLAYER_DEFAULT, PLAYER_DEFAULT, PLAYER_DEFAULT, PLAYER_DEFAULT};
static int player_of(const IniOpt* o) { return o->key[sizeof "gamepad_p" - 1] - '1'; }
typedef char players_counted[sizeof g_settings.player_connected / sizeof *g_settings.player_connected == SNX_GAMEPADS
                                 ? 1
                                 : -1];
/* a player's map, as gamepad_map's: none ("") is gamepad_map's; one that cannot be read is said, and changes nothing */
void gamepad_player_map_set(const IniOpt* o, const char* v) {
    SnxPlayerConfig* p = &g_players[player_of(o)];
    SnxPadMap map;
    char word[64];
    snx_pad_map_unset(&map);
    if (!v || !*v) {
        p->own = map;
        p->has_map = 0;
    } else if (snx_pad_map_read(&map, v, word, sizeof word))
        say_map_kept(o->key, word, p->has_map ? "" : "gamepad_map's ", p->has_map ? &p->own : &g_cfg.own);
    else {
        p->own = map;
        p->has_map = 1;
    }
}
SnxPlayerConfig* gamepad_player(int player) { return player >= 0 && player < SNX_GAMEPADS ? &g_players[player] : NULL; }
int gamepad_player_seen(int slot) { return !slot || (slot > 0 && slot < SNX_GAMEPADS && g_settings.player_connected[slot]); }
int gamepad_player_kind(int slot, int kind) {
    return slot >= 0 && slot < SNX_GAMEPADS && g_settings.player_type[slot] ? g_settings.player_type[slot] - 1 : kind;
}
int gamepad_ini(const char* key, const char* value) {
    const IniOpt* o = ini_find(g_gamepad_ini, key);
    if (!o) o = ini_find(g_gamepad_map_ini, key);
    if (!o) o = ini_find(g_gamepad_players_ini, key);
    if (o) ini_read(o, value);
    return o != NULL;
}
SnxGamepadConfig* gamepad_config(void) { return &g_cfg; }
void gamepad_store(const int* setting) {
    const IniOpt* o;
    char v[16];
    int t;
    for (t = 0; t < 2; t++) /* every player's, then each player's own */
        for (o = t ? g_gamepad_players_ini : g_gamepad_ini; o->key; o++)
            if (o->var == setting) {
                ini_format(o, v, sizeof v);
                ini_set(o->key, v);
                if (o->set) o->set(o, v);
            }
}
void gamepad_store_player_map(int player) {
    char key[32], text[SNX_PAD_MAP_TEXT];
    if (player < 0 || player >= SNX_GAMEPADS) return;
    snprintf(key, sizeof key, "gamepad_p%d_map", player + 1);
    snx_pad_map_write(&g_players[player].own, text, sizeof text);
    ini_set(key, text);
}
void gamepad_store_map(void) {
    char text[SNX_PAD_MAP_TEXT];
    snx_pad_map_write(&g_cfg.own, text, sizeof text);
    ini_set("gamepad_map", text);
}
int gamepad_layout(int i, int labels[4], int* symmetric) {
    int k, c;
    for (k = 0; k < SNX_GAMEPADS; k++)
        if (g_names[k][0] && !i--) {
            for (c = 0; c < 4; c++) labels[c] = g_labels[k][c];
            *symmetric = g_symmetric[k];
            return 1;
        }
    return 0;
}
unsigned gamepad_held(int i) {
    unsigned held = 0;
    int k;
    for (k = 0; k < SNX_GAMEPADS; k++)
        if (i < 0)
            held |= __atomic_load_n(&g_held[k], __ATOMIC_RELAXED);
        else if (g_names[k][0] && !i--)
            return __atomic_load_n(&g_held[k], __ATOMIC_RELAXED);
    return held;
}
const char* gamepad_name(int i) {
    int k;
    for (k = 0; k < SNX_GAMEPADS; k++)
        if (g_names[k][0] && !i--) return g_names[k];
    return NULL;
}
int gamepad_of_player(int slot) {
    int k, i = 0;
    if (slot < 0 || slot >= SNX_GAMEPADS || !g_names[slot][0]) return -1;
    for (k = 0; k < slot; k++) i += g_names[k][0] != 0;
    return i;
}

static int is_open(SDL_JoystickID id) {
    int i;
    for (i = 0; i < SNX_GAMEPADS; i++)
        if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) return 1;
    return 0;
}
static void enable_sensor(SDL_Gamepad* g, SDL_SensorType type, const char* name) {
    if (!SDL_GamepadHasSensor(g, type)) return;
    if (SDL_SetGamepadSensorEnabled(g, type, true))
        LOG("input: enabled %s sensor on %s\n", name, SDL_GetGamepadName(g));
    else
        WARN("input: cannot enable %s sensor on %s: %s\n", name, SDL_GetGamepadName(g), SDL_GetError());
}
static void enable_motion(SDL_Gamepad* g) {
    enable_sensor(g, SDL_SENSOR_ACCEL, "accelerometer");
    enable_sensor(g, SDL_SENSOR_GYRO, "gyroscope");
    enable_sensor(g, SDL_SENSOR_ACCEL_L, "left accelerometer");
    enable_sensor(g, SDL_SENSOR_GYRO_L, "left gyroscope");
    enable_sensor(g, SDL_SENSOR_ACCEL_R, "right accelerometer");
    enable_sensor(g, SDL_SENSOR_GYRO_R, "right gyroscope");
    if (!SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL) && !SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL_L) &&
        !SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL_R))
        LOG("input: %s exposes no motion sensors through SDL\n", SDL_GetGamepadName(g));
}
/* SDL reads SDL_GAMECONTROLLERCONFIG_FILE during initialization. Without that override, load the user's mappings
   from the same configuration directory as the game's .ini. Mapping a connected joystick raises GAMEPAD_ADDED. */
static void load_mappings(void) {
    char* folder;
    char* path;
    int count;
    const char* override = SDL_GetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE);
    if (override && *override) {
        LOG("input: SDL mappings from %s\n", override);
        return;
    }
    /* the launcher's, beside its settings.ini: one mapping database for every game it opens */
    {
        char* launcher = backend_launcher_folder();
        folder = launcher ? snx_menu_dir(launcher, SNX_USER_CONFIG) : NULL;
        free(launcher);
    }
    path = folder ? snx_path_join(folder, "gamecontrollerdb.txt") : NULL;
    if (!path) path = snx_path_join(g_confdir, "gamecontrollerdb.txt");
    if (!path || !snx_is_file(path)) {
        free(path);
        goto done;
    }
    count = SDL_AddGamepadMappingsFromFile(path);
    if (count < 0)
        WARN("input: cannot read SDL mappings from %s: %s\n", path, SDL_GetError());
    else
        LOG("input: loaded %d SDL mappings from %s\n", count, path);
    free(path);
done:
    free(folder);
}
void gamepad_detect(u32 id) {
    const char* name = SDL_GetJoystickNameForID(id);
    char guid[33];
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof guid);
    if (SDL_IsGamepad(id)) {
        gamepad_open(id);
    } else {
        LOG("input: joystick %s (id %lu, GUID %s) has no SDL gamepad mapping; add one to gamecontrollerdb.txt\n",
            name ? name : "Unknown", (unsigned long)id, guid);
    }
}
void gamepad_scan(void) {
    SDL_JoystickID* ids;
    int i, n = 0;
    load_mappings();
    ids = SDL_GetJoysticks(&n);
    if (!ids) {
        LOG("input: no SDL joysticks detected\n");
        return;
    }
    for (i = 0; i < n; i++) gamepad_detect(ids[i]);
    SDL_free(ids);
}
/* the slot a pad takes: the player that names it its controller, else the first that takes the next one; -1 for none */
static int slot_for(u32 id) {
    char want[48], one[33];
    int i, alike = 0;
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), want, 33);
    for (i = 0; i < SNX_GAMEPADS; i++)
        if (g_pads[i]) {
            SDL_GUIDToString(SDL_GetJoystickGUIDForID(SDL_GetGamepadID(g_pads[i])), one, sizeof one);
            alike += !SDL_strcasecmp(one, want);
        }
    if (alike) snprintf(want + strlen(want), sizeof want - strlen(want), "/%d", alike + 1);
    for (i = 0; i < SNX_GAMEPADS; i++)
        if (!g_pads[i] && !SDL_strcasecmp(g_settings.player_controller[i], want)) return i;
    for (i = 0; i < SNX_GAMEPADS; i++)
        if (!g_pads[i] && !g_settings.player_controller[i][0]) return i;
    return -1;
}
void gamepad_open(u32 id) {
    int i;
    char guid[33];
    if (is_open(id)) return;
    i = slot_for(id);
    if (i < 0) {
        LOG("input: %s takes no player: every free one names another controller\n", SDL_GetGamepadNameForID(id));
        return;
    }
    g_pads[i] = SDL_OpenGamepad(id);
    if (!g_pads[i]) {
        WARN("input: cannot open SDL gamepad %s (id %lu): %s\n",
             SDL_GetGamepadNameForID(id) ? SDL_GetGamepadNameForID(id) : "Unknown", (unsigned long)id, SDL_GetError());
        return;
    }
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof guid);
    LOG("input: gamepad %s (id %lu, GUID %s, %s)\n", SDL_GetGamepadName(g_pads[i]), (unsigned long)id, guid,
        SDL_GetGamepadStringForType(SDL_GetGamepadType(g_pads[i])));
    {
        SDL_Joystick* stick = SDL_GetGamepadJoystick(g_pads[i]);
        char* mapping = SDL_GetGamepadMapping(g_pads[i]);
        LOG("input: device path %s, %d axes, %d buttons, mapping %s\n",
            SDL_GetGamepadPath(g_pads[i]) ? SDL_GetGamepadPath(g_pads[i]) : "unknown",
            stick ? SDL_GetNumJoystickAxes(stick) : 0, stick ? SDL_GetNumJoystickButtons(stick) : 0,
            mapping ? mapping : "unavailable");
        SDL_free(mapping);
    }
    enable_motion(g_pads[i]);
    ui_lock();
    snprintf(g_names[i], sizeof g_names[i], "%s",
             SDL_GetGamepadName(g_pads[i]) ? SDL_GetGamepadName(g_pads[i]) : "Gamepad");
    snx_pad_labels(g_pads[i], g_labels[i]);
    g_symmetric[i] = snx_pad_symmetric(g_pads[i]);
    ui_unlock();
}
void gamepad_close(u32 id) {
    SDL_JoystickID* ids;
    int i, n = 0;
    for (i = 0; i < SNX_GAMEPADS; i++)
        if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) {
            SDL_CloseGamepad(g_pads[i]);
            g_pads[i] = NULL;
            g_menu_layout.blocked[i] = 0;
            memset(g_accel_sum[i], 0, sizeof g_accel_sum[i]);
            memset(g_gyro_sum[i], 0, sizeof g_gyro_sum[i]);
            ui_lock();
            g_names[i][0] = 0;
            ui_unlock();
        }
    /* a waiting pad takes the freed slot when all eight were occupied */
    ids = SDL_GetGamepads(&n);
    if (!ids) return;
    for (i = 0; i < n; i++) gamepad_open(ids[i]);
    SDL_free(ids);
}

/* the dead zone as a circle, so a stick pushed along one axis keeps the small part of the other, and the travel past
   it spread over the whole range, so the stick starts from nothing and still reaches the edge (a square gate's corner
   is past it) */
static void dz(i32* x, i32* y) {
    double r = sqrt((double)*x * *x + (double)*y * *y), z = g_settings.stick_dead_zone * 32767.0 / 100, k;
    if (r <= z) {
        *x = *y = 0;
        return;
    }
    k = ((r < 32767 ? r : 32767) - z) / (32767 - z) * 32767 / r;
    *x = (i32)(*x * k);
    *y = (i32)(*y * k);
}
/* SDL's Y axes point down, the Switch's up */
static i32 up(i32 v) { return v == -32768 ? 32767 : -v; }

static void map_of(int slot, SnxPadMap* map);
/* 0: gameplay, 1: menu (AB/XY), 2: interaction guide (AB only). */
void gamepad_set_context(int menu, int interaction) {
    int next = g_settings.xbox_play_station_layout_in_menus ? (menu ? 1 : interaction ? 2 : 0) : 0;
    int previous = g_menu_layout.menu;
    SnxPadMap before[SNX_GAMEPADS];
    if (next == previous) return;
    for (int i = 0; i < SNX_GAMEPADS; i++)
        if (g_pads[i]) map_of(i, &before[i]);
    g_menu_layout.menu = next;
    for (int i = 0; i < SNX_GAMEPADS; i++)
        if (g_pads[i]) {
            SnxPadMap after;
            map_of(i, &after);
            for (int c = 0; c < SNX_PAD_CONTROLS; c++)
                if ((before[i].bit[c] != after.bit[c] || ((previous == 1 || next == 1) && c < 4)) &&
                    snx_pad_held(g_pads[i], c)) g_menu_layout.blocked[i] |= 1u << c;
        }
}
void gamepad_set_menu(int active) { gamepad_set_context(active, 0); }
int gamepad_active(void) {
    for (int i = 0; i < SNX_GAMEPADS; i++) {
        SDL_Gamepad* g = g_pads[i];
        if (!g) continue;
        for (int c = 0; c < SNX_PAD_CONTROLS; c++)
            if (snx_pad_held(g, c)) return 1;
        i32 x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        dz(&x, &y);
        if (x || y) return 1;
        x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX); y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY);
        dz(&x, &y);
        if (x || y) return 1;
    }
    return 0;
}

/* what each control of the pad in slot sends (-1: no player's): the button in its place, the map over that (its
   player's, else every player's), then A and B swapped as its player says, else as every player's */
static void map_of(int slot, SnxPadMap* map) {
    const SnxPlayerConfig* p = slot >= 0 && slot < SNX_GAMEPADS ? &g_players[slot] : NULL;
    SnxPadMap placed;
    snx_pad_map_position(&placed);
    *map = p && p->has_map ? p->own : g_cfg.own;
    snx_pad_map_fill(map, &placed);
    int swap = p && g_settings.player_swap_a_b[slot] >= 0 ? g_settings.player_swap_a_b[slot] : g_settings.swap_a_b;
    if (g_menu_layout.menu || swap) snx_pad_map_swap_ab(map);
    if (g_menu_layout.menu == 1)
        for (int c = 0; c < SNX_PAD_CONTROLS; c++)
            if (map->bit[c] == 2 || map->bit[c] == 3) map->bit[c] = (signed char)(5 - map->bit[c]);
}
/* how strongly the pad in slot vibrates, in percent: its player's, else every player's */
static u32 strength(int slot) { return (u32)(g_settings.player_vibration[slot] >= 0 ? g_settings.player_vibration[slot] : g_settings.vibration); }
static int joycon_button_bit(int button, int right) {
    static const int LEFT_FACE[4] = {12, 15, 13, 14};
    static const int RIGHT_FACE[4] = {0, 2, 1, 3};
    if (button >= SDL_GAMEPAD_BUTTON_SOUTH && button <= SDL_GAMEPAD_BUTTON_NORTH)
        return right ? RIGHT_FACE[button] : LEFT_FACE[button];
    if (button == SDL_GAMEPAD_BUTTON_START) return right ? 10 : 11;
    if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) return right ? 5 : 4;
    if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) return right ? 26 : 24;
    if (button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) return right ? 27 : 25;
    if (button == (right ? SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 : SDL_GAMEPAD_BUTTON_LEFT_PADDLE1)) return right ? 7 : 6;
    if (button == (right ? SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2 : SDL_GAMEPAD_BUTTON_LEFT_PADDLE2)) return right ? 9 : 8;
    return -1;
}
u64 gamepad_button(u32 id, int button) {
    SDL_Gamepad* g = SDL_GetGamepadFromID(id);
    int control = snx_pad_of_button(button);
    int type, bit, slot;
    SnxPadMap map;
    if (!g) return 0;
    type = SDL_GetGamepadType(g);
    if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT) {
        bit = joycon_button_bit(button, type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT);
        return bit < 0 ? 0 : 1ull << bit;
    }
    if (control < 0) return 0;
    for (slot = 0; slot < SNX_GAMEPADS && g_pads[slot] != g; slot++) {}
    if (slot < SNX_GAMEPADS && (g_menu_layout.blocked[slot] & (1u << control))) return 0;
    map_of(slot < SNX_GAMEPADS ? slot : -1, &map);
    return map.bit[control] < 0 ? 0 : 1ull << map.bit[control];
}
int gamepad_slot_type(int slot) {
    if (slot < 0 || slot >= SNX_GAMEPADS || !g_pads[slot]) return SDL_GAMEPAD_TYPE_UNKNOWN;
    return SDL_GetGamepadType(g_pads[slot]);
}
int gamepad_slot_connected(int slot) { return slot >= 0 && slot < SNX_GAMEPADS && g_pads[slot] != NULL; }
u32 gamepad_slot_id(int slot) { return gamepad_slot_connected(slot) ? SDL_GetGamepadID(g_pads[slot]) : 0; }
static u64 joycon_buttons(SDL_Gamepad* g, int right) {
    u64 bits = 0;
    int button, bit;
    for (button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++) {
        bit = joycon_button_bit(button, right);
        if (bit >= 0 && SDL_GetGamepadButton(g, (SDL_GamepadButton)button)) bits |= 1ull << bit;
    }
    return bits;
}
void gamepad_state_slot(int slot, u64* buttons, i32 sticks[4]) {
    SDL_Gamepad* g;
    SnxPadMap map;
    int c, type;
    *buttons = 0;
    sticks[0] = sticks[1] = sticks[2] = sticks[3] = 0;
    if (slot < 0 || slot >= SNX_GAMEPADS || !(g = g_pads[slot])) return;
    type = SDL_GetGamepadType(g);
    if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT) {
        int right = type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT;
        *buttons = joycon_buttons(g, right);
        sticks[right ? 2 : 0] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
        sticks[right ? 3 : 1] = up(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY));
        dz(&sticks[right ? 2 : 0], &sticks[right ? 3 : 1]);
        return;
    }
    map_of(slot, &map);
    for (c = 0; c < SNX_PAD_CONTROLS; c++) {
        int held = snx_pad_held(g, c);
        if (!held) g_menu_layout.blocked[slot] &= ~(1u << c);
        if (held && !(g_menu_layout.blocked[slot] & (1u << c)) && map.bit[c] >= 0)
            *buttons |= 1ull << map.bit[c];
    }
    sticks[0] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    sticks[1] = up(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY));
    dz(&sticks[0], &sticks[1]);
    sticks[2] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX);
    sticks[3] = up(SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY));
    dz(&sticks[2], &sticks[3]);
}
/* Every sensor reading between two of the game's samples, summed: a pad's IMU reports at 200 to 1000 Hz and the
   game samples at 60. Taking the latest reading alone (SDL_GetGamepadSensorData) held one instant for a whole frame:
   a button press's knock, or the rumble it starts, is a vibration of a few milliseconds whose one sample, held,
   read as a fast turn and a tilt that weren't there, the aim jumping and gravity's correction pulling it back over
   the next second. The console's own sampling gives the game every reading; the mean over the frame is the turn the
   frame actually saw (Eden runs its MotionInput on every SDL sensor event for the same reason) */
void gamepad_sensor_event(u32 id, int type, const float data[3]) {
    SensorSum* to;
    int slot, i;
    for (slot = 0; slot < SNX_GAMEPADS; slot++)
        if (g_pads[slot] && SDL_GetGamepadID(g_pads[slot]) == id) break;
    if (slot == SNX_GAMEPADS) return;
    switch (type) {
    case SDL_SENSOR_ACCEL: to = &g_accel_sum[slot][SUM_MAIN]; break;
    case SDL_SENSOR_GYRO: to = &g_gyro_sum[slot][SUM_MAIN]; break;
    case SDL_SENSOR_ACCEL_L: to = &g_accel_sum[slot][SUM_LEFT]; break;
    case SDL_SENSOR_GYRO_L: to = &g_gyro_sum[slot][SUM_LEFT]; break;
    case SDL_SENSOR_ACCEL_R: to = &g_accel_sum[slot][SUM_RIGHT]; break;
    case SDL_SENSOR_GYRO_R: to = &g_gyro_sum[slot][SUM_RIGHT]; break;
    default: return;
    }
    for (i = 0; i < 3; i++) to->sum[i] += data[i];
    to->n++;
}
/* the frame's mean reading, and the sum started over; the latest one when none came in this frame */
static int frame_reading(SDL_Gamepad* g, SDL_SensorType type, SensorSum* sum, float out[3]) {
    int i, ok = 1;
    if (sum->n > 0)
        for (i = 0; i < 3; i++) out[i] = sum->sum[i] / (float)sum->n;
    else
        ok = SDL_GetGamepadSensorData(g, type, out, 3);
    memset(sum, 0, sizeof *sum);
    return ok;
}
static int sample_sensor(SDL_Gamepad* g, int slot, int which, SDL_SensorType accel_type, SDL_SensorType gyro_type,
                         PadMotion* out) {
    float a[3], w[3];
    int have_a, have_w;
    if (!SDL_GamepadSensorEnabled(g, accel_type) || !SDL_GamepadSensorEnabled(g, gyro_type)) return 0;
    have_a = frame_reading(g, accel_type, &g_accel_sum[slot][which], a);
    have_w = frame_reading(g, gyro_type, &g_gyro_sum[slot][which], w);
    if (!have_a || !have_w) return 0;
    /* SDL acceleration is m/s^2. Horizon uses g, with Y up and Z toward the player. */
    out->acceleration[0] = -a[0] / 9.80665f;
    out->acceleration[1] = a[2] / 9.80665f;
    out->acceleration[2] = -a[1] / 9.80665f;
    /* SDL's gyroscope reads radians a second, the console's revolutions a second (Eden's SDL driver divides by 2 pi
       the same way): taken as they were, turns were 6.3 times what they are */
    const float turn = 6.28318530718f;
    out->angular_velocity[0] = w[0] / turn;
    out->angular_velocity[1] = -w[2] / turn;
    out->angular_velocity[2] = w[1] / turn;
    out->valid = 1;
    return 1;
}
void gamepad_motion(int slot, PadMotion out[2]) {
    SDL_Gamepad* g;
    int left, right;
    memset(out, 0, 2 * sizeof *out);
    if (slot < 0 || slot >= SNX_GAMEPADS || !(g = g_pads[slot])) return;
    left = sample_sensor(g, slot, SUM_LEFT, SDL_SENSOR_ACCEL_L, SDL_SENSOR_GYRO_L, &out[0]);
    right = sample_sensor(g, slot, SUM_RIGHT, SDL_SENSOR_ACCEL_R, SDL_SENSOR_GYRO_R, &out[1]);
    if (!left && !right) sample_sensor(g, slot, SUM_MAIN, SDL_SENSOR_ACCEL, SDL_SENSOR_GYRO, &out[0]);
}

void gamepad_state(u64* buttons, i32 sticks[4]) {
    u64 b = 0;
    int i;
    sticks[0] = sticks[1] = sticks[2] = sticks[3] = 0;
    for (i = 0; i < SNX_GAMEPADS; i++) {
        SDL_Gamepad* g = g_pads[i];
        u64 one;
        i32 axes[4];
        unsigned held = 0;
        int c;
        if (g)
            for (c = 0; c < SNX_PAD_CONTROLS; c++)
                if (snx_pad_held(g, c)) held |= 1u << c;
        __atomic_store_n(&g_held[i], held, __ATOMIC_RELAXED); /* the renderer's thread draws them */
        if (!g) continue;
        gamepad_state_slot(i, &one, axes);
        b |= one;
        if (!sticks[0] && !sticks[1]) {
            sticks[0] = axes[0];
            sticks[1] = axes[1];
        }
        if (!sticks[2] && !sticks[3]) {
            sticks[2] = axes[2];
            sticks[3] = axes[3];
        }
    }
    *buttons = b;
}

/* A pad with HD rumble (Switch controllers, a DualSense, Valve's): it plays an amplitude as the console's does */
static int hd_rumble(SDL_Gamepad* g) {
    SDL_GamepadType t = SDL_GetGamepadType(g);
    return t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
           t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR ||
           t == SDL_GAMEPAD_TYPE_PS5 || SDL_GetGamepadVendor(g) == 0x28DE;
}
/* One band of the game's vibration (an amplitude and its softening, 0 to 65535 each, hid.h's PadState.rumble) as a
   motor's strength, as Eden plays it (emulated_controller.cpp's SetVibration, sdl_driver.cpp, GPL-3.0-or-later): the
   player's strength first; then a curve that lifts small amplitudes, which an ordinary pad's motors didn't feel, while
   1.0 stays 1.0 (flatter under 70% strength, none for HD rumble); then softened for the band's frequency. Played as
   they came, a game's light touches were nothing and its strong ones all the motor had */
static Uint16 rumble_motor(u32 amplitude, u32 softening, u32 percent, int hd) {
    float a = (float)amplitude / 65535.0f * (float)percent / 100.0f, factor = hd ? 1.0f : percent > 70 ? 0.35f : 0.5f;
    float v;
    if (a > 1.0f) a = 1.0f;
    if (!(a > 0.0f)) return 0;
    v = (a + powf(a, factor)) * 0.5f * 65535.0f * (1.0f - (float)softening / 65535.0f);
    return v >= 65535.0f ? 65535 : (Uint16)v;
}
/* each motor as the stronger of the two devices' bands: the heavy one low, the light one high. SDL sends a pad only
   what changed, and stops it when the time runs out: the game's thread may stop sending */
static void rumble_play(int slot, u64 left, u64 right) {
    u32 percent = strength(slot);
    int hd = hd_rumble(g_pads[slot]);
    Uint16 ll = rumble_motor((u32)(left & 0xFFFF), (u32)(left >> 32 & 0xFFFF), percent, hd);
    Uint16 rl = rumble_motor((u32)(right & 0xFFFF), (u32)(right >> 32 & 0xFFFF), percent, hd);
    Uint16 lh = rumble_motor((u32)(left >> 16 & 0xFFFF), (u32)(left >> 48), percent, hd);
    Uint16 rh = rumble_motor((u32)(right >> 16 & 0xFFFF), (u32)(right >> 48), percent, hd);
    SDL_RumbleGamepad(g_pads[slot], ll > rl ? ll : rl, lh > rh ? lh : rh, 100);
}
void gamepad_rumble(u64 left, u64 right) {
    for (int i = 0; i < SNX_GAMEPADS; i++)
        if (g_pads[i]) rumble_play(i, left, right);
}
void gamepad_rumble_slot(int slot, u64 left, u64 right) {
    if (slot < 0 || slot >= SNX_GAMEPADS || !g_pads[slot]) return;
    rumble_play(slot, left, right);
}
