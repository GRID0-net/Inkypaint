/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Splatoon 3 11.3.0: the mouse turns the player's camera directly (native_mouse_camera), instead of through the
   controller's motion sensor (kbm.c, which feeds no gyro while this is on).

   0xa8fbe20 is the player camera's main update (x0 the PlayerCamera). Before it runs, the game has stored the gyro's
   yaw rate in camera+0x166c and +0x1670 (0xa8f5530), and it takes the pitch, in degrees, from the controller's
   attitude matrix at [[camera+0x1970]+0x60]+0x3f50 (atan2 of its entries at +0x10 and +0x1c, 0xa8fe2c8..0xa8fe30c,
   stored at camera+0x162c). The wrapper adds the mouse's yaw to the rate and sets the attitude to the pitch the mouse
   has turned to, then runs the original, so the camera's own gains, smoothing and limits still apply. */
#include "nx/runtime/inkypaint_mouselook.h"
#include "nx/runtime/driver/inkypaint.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/guest_jit.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/kbm.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/hle/hle.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/memory/mem.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CAMERA_UPDATE 0xa8fbe20ull
#define CAMERA_WORD   0xd104c3ffu /* sub sp, sp, #0x130 */
#define BASE_DEG      0.06f       /* degrees per count at 100%, as kbm.c's motion aim */
#define PI_F          3.14159265358979f
/* The gyro's yaw rate as the game stores it at camera+0x166c: the SDK's angular velocity (1.0 is a turn a second)
   times 0.0573 (0xa8f54d8 reads it scaled so, 0xab02008). To be calibrated on a real run: YAW_SIGN, PITCH_SIGN, and
   whether this scale is right. */
#define YAW_RATE_PER_DEG_S (0.0572957795f / 360.0f)
#define YAW_SIGN           (-1.0f)
#define PITCH_SIGN         (1.0f)
#define PITCH_LIMIT        80.0f /* INKYPAINT_MOUSELOOK_UP and INKYPAINT_MOUSELOOK_DOWN set the two ends, in degrees */
#define SENTINEL_PITCH     -90.0f /* what the camera holds as its pitch while it ignores the attitude (a screen being entered) */
/* Measured on a real run: the camera turns about this many times the yaw sent as a gyro rate (the game's gain from its
   motion sensitivity option, which differs a little by movement size: its gyro ramp). Fixed, to be replaced by turning
   the camera directly. The pitch is the user's feel, vertical felt larger than horizontal. */
#define YAW_CORR   1.5f
#define PITCH_GAIN 0.85f

static GuestFn camera_original;
static float pitch_deg;
static u64 last_ns;
static int ignored; /* the camera held its sentinel pitch at its last update, so it was not reading our attitude */
/* The camera stops looking up before 80: with that as the end, the mouse went past where the view stopped and had to come
   back the difference before it looked down again. 60 removed it in play (INKYPAINT_MOUSELOOK_UP sets it) */
static float limit_up = 60.0f, limit_down = -PITCH_LIMIT;

static float read_float(u64 a) {
    u32 bits = gr32(a);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static void write_float(u64 a, float f) {
    u32 bits;
    memcpy(&bits, &f, sizeof bits);
    gw32(a, bits);
}

static void camera_update(CPU* c, u64 entry) {
    u64 camera = c->x[0], now = plat_ns(), pad;
    float dx, dy, dt, yaw_rate, rad;
    if (kbm_look_take(&dx, &dy) && camera) {
        float scale = BASE_DEG * (float)g_settings.mouse_sensitivity / 100.0f;
        float sy = (float)g_settings.mouse_vertical_sensitivity / 100.0f;
        dt = last_ns && now > last_ns ? (float)(now - last_ns) * 1e-9f : 1.0f / 30.0f;
        dt = fminf(fmaxf(dt, 0.001f), 0.1f);
        /* the yaw as a rate for this frame, in the unit the game stored the gyro's */
        yaw_rate = YAW_SIGN * dx * scale / dt * YAW_RATE_PER_DEG_S / YAW_CORR;
        pitch_deg -= PITCH_GAIN * PITCH_SIGN * dy * scale * sy * (g_settings.mouse_invert_vertical ? -1.0f : 1.0f);
        pitch_deg = fminf(fmaxf(pitch_deg, limit_down), limit_up);
        /* While the camera ignores the attitude (a screen being entered, it holds -90 as its pitch), the mouse's pitch
           starts again from level, as a controller held at rest does: otherwise the stale pitch of the last screen is
           what the camera jumps to when it takes the attitude again */
        if (ignored) pitch_deg = 0;
        if (dx != 0) {
            write_float(camera + 0x166c, read_float(camera + 0x166c) + yaw_rate);
            write_float(camera + 0x1670, read_float(camera + 0x1670) + yaw_rate);
        }
        pad = gr64(camera + 0x1970);
        if (pad) pad = gr64(pad + 0x60);
        if (pad) {
            /* pitch = -57.29578 * atan2(m4, m7) in degrees: set the matrix to the pitch the mouse has */
            rad = -pitch_deg * PI_F / 180.0f;
            write_float(pad + 0x3f50 + 0x10, sinf(rad));
            write_float(pad + 0x3f50 + 0x1c, cosf(rad));
        }
    }
    last_ns = now;
    camera_original(c, entry);
    ignored = camera && fabsf(read_float(camera + 0x162c) - SENTINEL_PITCH) < 0.01f && fabsf(pitch_deg - SENTINEL_PITCH) > 1.0f;
    /* INKYPAINT_MOUSELOOK_TRACE=1: the pitch we set and what the camera holds after its update, to find the angle the
       view really has and the limits it stops at (about every third of a second) */
    {
        static int trace = -1;
        static unsigned n;
        if (trace < 0) trace = getenv("INKYPAINT_MOUSELOOK_TRACE") && !strcmp(getenv("INKYPAINT_MOUSELOOK_TRACE"), "1");
        if (trace && camera && ++n % 20 == 0)
            LOG("mouselook: ours %.1f | 162c %.1f 152c %.1f | head %.3f %.3f %.3f | 1b0 %.3f 1b4 %.3f 1b8 %.3f | 1774 %.1f 1788 %.1f\n",
                pitch_deg, read_float(camera + 0x162c), read_float(camera + 0x152c), read_float(camera + 0x18c),
                read_float(camera + 0x190), read_float(camera + 0x194), read_float(camera + 0x1b0),
                read_float(camera + 0x1b4), read_float(camera + 0x1b8), read_float(camera + 0x1774),
                read_float(camera + 0x1788));
    }
}

void inkypaint_mouselook_init(void) {
    /* The setting is not read yet here (the keyboard and mouse table is applied after startup), so the hook goes in
       on this executable and kbm_look_take is asked each frame: off, the wrapper only runs the original */
    if (inkypaint_any_title() || g_hle.kernel.title_id != INKYPAINT_TITLE_ID)
        return;
    if (CAMERA_UPDATE < g_code_lo || CAMERA_UPDATE >= g_code_hi || gr32(CAMERA_UPDATE) != CAMERA_WORD) {
        WARN("mouselook: the camera hook does not match this executable\n");
        return;
    }
    /* as inkypaint_display.c: retire the AOT body so every call enters the wrapper, before any guest thread runs */
    jit_code_changed(CAMERA_UPDATE, 4);
    camera_original = jit_wait(CAMERA_UPDATE);
    if (!camera_original) {
        WARN("mouselook: could not compile the camera hook\n");
        return;
    }
    g_disp[(CAMERA_UPDATE - g_code_lo) / 4] = camera_update;
    if (getenv("INKYPAINT_MOUSELOOK_UP")) limit_up = fminf(fmaxf((float)atof(getenv("INKYPAINT_MOUSELOOK_UP")), 1.0f), PITCH_LIMIT);
    if (getenv("INKYPAINT_MOUSELOOK_DOWN")) limit_down = fminf(fmaxf((float)atof(getenv("INKYPAINT_MOUSELOOK_DOWN")), -PITCH_LIMIT), -1.0f);
    LOG("mouselook: camera hook ready, used while native_mouse_camera is on\n");
}
