/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Keyboard and mouse as player 1's controller (runtime/platform/backend/kbm.c): the default and the .ini's bindings,
   mouse buttons and the wheel, Free the mouse's key, and the mouse's aim as a motion sensor, through hid_motion as
   the game reads it: the turn it reports is the mouse's, to the last count, spread over samples when it is faster
   than the sensor reads, and the controller it makes is held as the turn has tilted it */
#include "nx/runtime/platform/kbm.h"
#include "nx/hle/services/hid_motion.h"
#include "common/utilities/endian.h"
#include "snx_settings.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void logf_(int lvl, const char* fmt, ...) {
    (void)lvl, (void)fmt;
}

static int checks;
#define CHECK(e)                                                                                                       \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(e)) {                                                                                                    \
            fprintf(stderr, "kbm.c:%d: check failed: %s\n", __LINE__, #e);                                             \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

enum { A, B, X, Y, L, R, ZL, ZR, PLUS, MINUS, L3, R3, UP, DOWN, LEFT, RIGHT, LS_UP, LS_DOWN, LS_LEFT, LS_RIGHT };
#define BIT(c) kbm_control_bit(c)
#define CURSOR (SETTINGS_KBM_COUNT - 1)

static void bind_text(int control, const char* text) {
    snprintf(g_settings.keyboard_binding[control], sizeof g_settings.keyboard_binding[control], "%s", text);
    kbm_bindings_set(NULL, text);
}
static float f32_at(const u8* state, int offset) {
    u32 bits = snx_ld_le32(state + offset);
    float v;
    memcpy(&v, &bits, 4);
    return v;
}
#define ANGLE (HO_HidSixAxisSensorState_acceleration + 24)

int main(void) {
    PadMotion m;
    HidMotionState state;
    u8 out[HS_HidSixAxisSensorState];
    u64 n = 0;

    /* the names, both ways */
    CHECK(kbm_code("W") == SDL_SCANCODE_W && kbm_code("left shift") == SDL_SCANCODE_LSHIFT);
    CHECK(kbm_code("Mouse Left") == KBM_MOUSE + SDL_BUTTON_LEFT && kbm_code("Mouse Right") == KBM_MOUSE + SDL_BUTTON_RIGHT);
    CHECK(kbm_code("Wheel Down") == KBM_WHEEL_DOWN && kbm_code("Nothing Such") == -1);
    /* the keys SDL names by the system (Left Option on a Mac, Left Windows on Windows) go by every name, on any */
    CHECK(kbm_code("Left Alt") == SDL_SCANCODE_LALT && kbm_code("Left Option") == SDL_SCANCODE_LALT);
    CHECK(kbm_code("Left GUI") == SDL_SCANCODE_LGUI && kbm_code("Left Command") == SDL_SCANCODE_LGUI &&
          kbm_code("Left Windows") == SDL_SCANCODE_LGUI && kbm_code("Right Ctrl") == SDL_SCANCODE_RCTRL);
    CHECK(!strcmp(kbm_code_name(SDL_SCANCODE_LALT), "Left Alt"));
    CHECK(!strcmp(kbm_code_name(KBM_MOUSE + SDL_BUTTON_X2), "Mouse X2") && !strcmp(kbm_code_name(SDL_SCANCODE_SPACE), "Space"));
    /* every default names keys there are */
    for (int c = 0; c < SETTINGS_KBM_COUNT; c++) {
        char text[64], *at;
        snprintf(text, sizeof text, "%s", kbm_default_binding(c));
        for (at = strtok(text, ","); at; at = strtok(NULL, ","))
            CHECK(kbm_code(at + strspn(at, " ")) >= 0);
    }

    /* the defaults: WASD moves, the mouse's left button shoots, Shift swims, Space jumps */
    CHECK(kbm_buttons() == 0);
    kbm_input(SDL_SCANCODE_W, 1);
    kbm_input(KBM_MOUSE + SDL_BUTTON_LEFT, 1);
    kbm_input(SDL_SCANCODE_LSHIFT, 1);
    CHECK(kbm_buttons() == (BIT(LS_UP) | BIT(ZR) | BIT(ZL)));
    CHECK(BIT(LS_UP) == 1ull << 41 && BIT(ZR) == 1ull << 9 && BIT(ZL) == 1ull << 8 && BIT(A) == 1);
    kbm_input(SDL_SCANCODE_W, 0);
    CHECK(kbm_buttons() == (BIT(ZR) | BIT(ZL)));
    kbm_release_all();
    CHECK(kbm_buttons() == 0);
    /* two keys of one control: either presses it, and letting one go keeps it pressed */
    kbm_input(SDL_SCANCODE_SPACE, 1);
    kbm_input(SDL_SCANCODE_BACKSPACE, 1);
    kbm_input(SDL_SCANCODE_SPACE, 0);
    CHECK(kbm_buttons() == BIT(B));
    kbm_release_all();

    /* the .ini's: a key moved, a mouse button and the wheel bound, None for none, an unknown name left out */
    bind_text(ZR, "J, Mouse X1");
    bind_text(R3, "Wheel Up");
    bind_text(A, "None");
    bind_text(B, "Space, Not A Key");
    kbm_input(KBM_MOUSE + SDL_BUTTON_LEFT, 1);
    CHECK(kbm_buttons() == 0);
    kbm_input(KBM_MOUSE + SDL_BUTTON_X1, 1);
    kbm_input(SDL_SCANCODE_F, 1);
    kbm_input(SDL_SCANCODE_SPACE, 1);
    CHECK(kbm_buttons() == (BIT(ZR) | BIT(B)));
    kbm_release_all();
    /* a wheel's notch holds its binding for a few samples, then lets go */
    kbm_wheel(1.0f);
    CHECK(kbm_buttons() == BIT(R3) && kbm_buttons() == BIT(R3) && kbm_buttons() == BIT(R3));
    CHECK(kbm_buttons() == 0);
    kbm_wheel(-1.0f);
    CHECK(kbm_buttons() == 0);
    bind_text(ZR, "");
    bind_text(R3, "");
    bind_text(A, "");
    bind_text(B, "");

    /* Free the mouse's key says so when it goes down, once, and presses no control */
    CHECK(kbm_input(SDL_SCANCODE_LALT, 1) == 1 && kbm_input(SDL_SCANCODE_LALT, 1) == 0 && kbm_buttons() == 0);
    CHECK(kbm_input(SDL_SCANCODE_LALT, 0) == 0);
    bind_text(CURSOR, "Mouse Middle");
    CHECK(kbm_input(KBM_MOUSE + SDL_BUTTON_MIDDLE, 1) == 1);
    kbm_release_all();
    bind_text(CURSOR, "");

    /* in the menus, their own keys: Return picks, the right button and Backspace go back, Q and E are L and R, WASD the
       D-pad; out of them again, the play's */
    for (int c = 0; c < SETTINGS_KBM_COUNT; c++) {
        char text[64], *at;
        snprintf(text, sizeof text, "%s", kbm_menu_default_binding(c));
        for (at = strtok(text, ","); at; at = strtok(NULL, ","))
            CHECK(!SDL_strcasecmp(at + strspn(at, " "), "None") || kbm_code(at + strspn(at, " ")) >= 0);
    }
    kbm_set_menu(1);
    kbm_input(SDL_SCANCODE_W, 1);
    kbm_input(KBM_MOUSE + SDL_BUTTON_RIGHT, 1);
    kbm_input(SDL_SCANCODE_E, 1);
    CHECK(kbm_buttons() == (BIT(UP) | BIT(B) | BIT(R)));
    kbm_set_menu(0);
    CHECK(kbm_buttons() == (BIT(LS_UP) | BIT(R) | BIT(R3)));
    kbm_release_all();
    snprintf(g_settings.menu_binding[B], sizeof g_settings.menu_binding[B], "%s", "Q");
    kbm_bindings_set(NULL, "Q");
    kbm_set_menu(1);
    kbm_input(SDL_SCANCODE_Q, 1);
    CHECK(kbm_buttons() == (BIT(B) | BIT(L)));
    kbm_set_menu(0);
    kbm_release_all();
    g_settings.menu_binding[B][0] = 0;
    kbm_bindings_set(NULL, "");

    /* ---- the aim. Off, the mouse is no sensor */
    g_settings.mouse_sensitivity = 100;
    g_settings.mouse_vertical_sensitivity = 100;
    memset(&m, 0, sizeof m);
    g_settings.mouse_aim = 0;
    kbm_mouse_moved(100, 0);
    kbm_motion(&m, 16666667u);
    CHECK(m.angular_velocity[0] == 0 && m.angular_velocity[1] == 0 && m.angular_velocity[2] == 0);
    g_settings.mouse_aim = 1;
    /* on, still: a controller held flat, still */
    kbm_motion(&m, 16666667u);
    CHECK(m.valid && m.exact && fabsf(m.acceleration[2] + 1.0f) < 1e-6f && m.angular_velocity[2] == 0.0f);
    /* right: turns the aim right, clockwise from above (negative about the vertical), 0.06 degrees a count */
    memset(&m, 0, sizeof m);
    kbm_mouse_moved(100, 0);
    kbm_motion(&m, 10000000u);
    CHECK(m.angular_velocity[2] < 0.0f && fabsf(m.angular_velocity[2] * 0.01f * 360.0f + 6.0f) < 1e-3f);
    CHECK(m.angular_velocity[0] == 0.0f);
    /* up: the far end up, positive about the crosswise axis, and the accelerometer reads the tilt */
    memset(&m, 0, sizeof m);
    kbm_mouse_moved(0, -500);
    kbm_motion(&m, 50000000u);
    CHECK(m.angular_velocity[0] > 0.0f && fabsf(m.angular_velocity[0] * 0.05f * 360.0f - 30.0f) < 1e-3f);
    CHECK(fabsf(m.acceleration[1] + sinf(30.0f * 3.14159265f / 180.0f)) < 1e-4f);
    /* turning left and right while tilted is about the vertical still: part of it about the controller's own long
       axis */
    memset(&m, 0, sizeof m);
    kbm_mouse_moved(-100, 0);
    kbm_motion(&m, 10000000u);
    CHECK(m.angular_velocity[1] > 0.0f && m.angular_velocity[2] > 0.0f &&
          fabsf(m.angular_velocity[1] / m.angular_velocity[2] - tanf(30.0f * 3.14159265f / 180.0f)) < 1e-3f);
    /* inverted, up aims down; the vertical sensitivity scales it */
    g_settings.mouse_invert_vertical = 1;
    g_settings.mouse_vertical_sensitivity = 50;
    memset(&m, 0, sizeof m);
    kbm_mouse_moved(0, -500);
    kbm_motion(&m, 50000000u);
    CHECK(fabsf(m.angular_velocity[0] * 0.05f * 360.0f + 15.0f) < 1e-3f);
    g_settings.mouse_invert_vertical = 0;
    g_settings.mouse_vertical_sensitivity = 100;
    /* a controller with a sensor of its own: the mouse's turn on top of its */
    memset(&m, 0, sizeof m);
    m.valid = 1;
    m.angular_velocity[2] = 0.5f;
    kbm_mouse_moved(100, 0);
    kbm_motion(&m, 10000000u);
    CHECK(!m.exact && fabsf(m.angular_velocity[2] - (0.5f - 6.0f / 360.0f / 0.01f)) < 1e-3f);
    kbm_aim_reset();

    /* ---- through hid_motion as the game reads it: a flick of 3000 counts (180 degrees) in one sample, faster than
       the sensor reads, comes out over the next samples, and the angle the game is told sums to half a turn */
    memset(&state, 0, sizeof state);
    memset(&m, 0, sizeof m);
    kbm_motion(&m, 16666667u);
    hid_motion_sample(&state, &m, 16666667u, ++n, out);
    kbm_mouse_moved(3000, 0);
    for (int i = 0; i < 30; i++) {
        memset(&m, 0, sizeof m);
        kbm_motion(&m, 16666667u);
        CHECK(fabsf(m.angular_velocity[2]) <= 4.76f);
        hid_motion_sample(&state, &m, 16666667u, ++n, out);
    }
    CHECK(fabsf(f32_at(out, ANGLE + 8) + 0.5f) < 1e-3f);
    /* a slow, small turn (half a count a sample, under the threshold a real sensor's noise is cut at) still turns */
    {
        float before = f32_at(out, ANGLE + 8);
        for (int i = 0; i < 60; i++) {
            memset(&m, 0, sizeof m);
            kbm_mouse_moved(0.5f, 0);
            kbm_motion(&m, 16666667u);
            hid_motion_sample(&state, &m, 16666667u, ++n, out);
        }
        CHECK(fabsf((f32_at(out, ANGLE + 8) - before) + 60 * 0.03f / 360.0f) < 1e-4f);
    }
    /* Controller takeover clears holds and accumulated aim; neither reappears after the idle grace. */
    kbm_release_all();
    kbm_set_menu(0);
    kbm_input(SDL_SCANCODE_W, 1);
    CHECK(kbm_buttons() != 0);
    kbm_mouse_moved(300, 0);
    kbm_controller_activity(1, 1000000000ull);
    CHECK(!kbm_enabled() && !kbm_buttons());
    kbm_input(SDL_SCANCODE_W, 1);
    kbm_wheel(1);
    kbm_controller_activity(0, 2499999999ull);
    CHECK(!kbm_enabled());
    kbm_controller_activity(0, 2500000000ull);
    CHECK(kbm_enabled() && !kbm_buttons());
    memset(&m, 0, sizeof m);
    kbm_motion(&m, 16666667u);
    CHECK(m.angular_velocity[0] == 0 && m.angular_velocity[1] == 0 && m.angular_velocity[2] == 0);
    kbm_input(SDL_SCANCODE_W, 1);
    CHECK(kbm_buttons() != 0);
    g_settings.keyboard_and_mouse_input = 0;
    kbm_controller_activity(0, 3000000000ull);
    CHECK(!kbm_enabled() && !kbm_buttons());
    g_settings.keyboard_and_mouse_input = 1;
    kbm_controller_activity(0, 3000000001ull);
    CHECK(kbm_enabled() && !kbm_buttons());
    printf("kbm: %d checks\n", checks);
    return 0;
}
