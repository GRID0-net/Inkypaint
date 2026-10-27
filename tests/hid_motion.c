/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* hid_motion.c, Eden's MotionInput: the console's units (revolutions a second, revolutions turned), the bias learned
   at rest and taken off, readings under the threshold none, and a direction matrix that stays a rotation */
#include "nx/hle/services/hid_motion.h"
#include "common/utilities/endian.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define VELOCITY (HO_HidSixAxisSensorState_acceleration + 12)
#define ANGLE    (HO_HidSixAxisSensorState_acceleration + 24)

static float f32_at(const u8* state, int offset) {
    u32 bits = snx_ld_le32(state + offset);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}
static int close_to(float a, float b, float within) { return fabsf(a - b) < within; }
static float dir_at(const u8* state, int i) { return f32_at(state, HO_HidSixAxisSensorState_direction + 4 * i); }
/* the rows orthonormal, and a rotation, not a reflection */
static void check_rotation(const u8* state) {
    float r[9];
    for (int i = 0; i < 9; i++) r[i] = dir_at(state, i);
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) {
            float dot = r[a * 3] * r[b * 3] + r[a * 3 + 1] * r[b * 3 + 1] + r[a * 3 + 2] * r[b * 3 + 2];
            assert(close_to(dot, a == b ? 1.0f : 0.0f, 0.001f));
        }
    float det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                r[2] * (r[3] * r[7] - r[4] * r[6]);
    assert(close_to(det, 1.0f, 0.001f));
}
static void check_identity(const u8* state) {
    for (int i = 0; i < 9; i++) assert(close_to(dir_at(state, i), i % 4 == 0 ? 1.0f : 0.0f, 0.001f));
}

int main(void) {
    HidMotionState state;
    PadMotion motion;
    u8 out[HS_HidSixAxisSensorState];
    u64 n = 0;
    memset(&state, 0, sizeof state);
    memset(&motion, 0, sizeof motion);

    /* no sensor: flat and still */
    hid_motion_sample(&state, NULL, 0, ++n, out);
    assert(snx_ld_le64(out + 8) == n && close_to(f32_at(out, HO_HidSixAxisSensorState_acceleration + 8), -1.0f, 1e-6f));
    check_identity(out);
    assert(snx_ld_le32(out + HO_HidSixAxisSensorState_attributes) == 1);
    assert(hid_motion_at_rest(NULL));

    /* resting flat: still flat */
    motion.valid = 1;
    motion.acceleration[2] = -1.0f;
    for (int i = 0; i < 10; i++) hid_motion_sample(&state, &motion, 5000000, ++n, out);
    check_identity(out);
    assert(hid_motion_at_rest(&motion));

    /* a quarter turn about the up axis, in revolutions: 0.25 a second for one second. The velocity reads as given,
       the angle counts the quarter, and the up row stays up while the others turn */
    motion.angular_velocity[2] = 0.25f;
    for (int i = 0; i < 100; i++) {
        hid_motion_sample(&state, &motion, 10000000, ++n, out);
        assert(close_to(f32_at(out, VELOCITY + 8), 0.25f, 1e-4f));
        check_rotation(out);
    }
    assert(!hid_motion_at_rest(&motion));
    assert(close_to(f32_at(out, ANGLE + 8), 0.25f, 0.002f));
    assert(close_to(dir_at(out, 8), 1.0f, 0.01f));
    assert(close_to(fabsf(dir_at(out, 1)), 1.0f, 0.02f) && close_to(dir_at(out, 0), 0.0f, 0.02f));
    motion.angular_velocity[2] = 0.0f;

    /* a reading under the threshold (0.007 revolutions a second) is none: nothing turns */
    {
        float before = f32_at(out, ANGLE + 8);
        motion.angular_velocity[0] = 0.005f;
        for (int i = 0; i < 100; i++) hid_motion_sample(&state, &motion, 10000000, ++n, out);
        assert(f32_at(out, VELOCITY) == 0.0f && f32_at(out, ANGLE) == 0.0f && f32_at(out, ANGLE + 8) == before);
    }

    /* a pad whose resting reading is 0.03 revolutions a second (over the threshold, under the relaxed rest): turning at
       first, the bias learned while it lies still, until it reads none and the angle stops */
    memset(&state, 0, sizeof state);
    motion.angular_velocity[0] = 0.0f;
    motion.angular_velocity[1] = 0.03f;
    hid_motion_sample(&state, &motion, 5000000, ++n, out);
    assert(close_to(f32_at(out, VELOCITY + 4), 0.03f, 1e-4f));
    for (int i = 0; i < 20000; i++) hid_motion_sample(&state, &motion, 5000000, ++n, out);
    assert(f32_at(out, VELOCITY + 4) == 0.0f);
    {
        float settled = f32_at(out, ANGLE + 4);
        for (int i = 0; i < 1000; i++) hid_motion_sample(&state, &motion, 5000000, ++n, out);
        assert(f32_at(out, ANGLE + 4) == settled);
    }
    check_rotation(out);

    /* a sensor's first reading sets its tilt from gravity at once, in Eden's convention (GetOrientation): on its side,
       gravity along +Y, the third row points along +Y; on its other side, along -Y. The old matrix was this one
       transposed, the inverse rotation, which turned vertical aiming the wrong way */
    {
        static const float sides[2] = {1.0f, -1.0f};
        for (int k = 0; k < 2; k++) {
            memset(&state, 0, sizeof state);
            memset(&motion, 0, sizeof motion);
            motion.valid = 1;
            motion.acceleration[1] = sides[k];
            hid_motion_sample(&state, &motion, 5000000, ++n, out);
            assert(close_to(dir_at(out, 7), sides[k], 0.01f) && close_to(dir_at(out, 5), -sides[k], 0.01f));
            check_rotation(out);
        }
    }

    /* an accelerometer alone (no gyro reading ever): the orientation follows gravity, numbers stay finite */
    memset(&state, 0, sizeof state);
    memset(&motion, 0, sizeof motion);
    motion.valid = 1;
    motion.acceleration[1] = 1.0f; /* on its side */
    for (int i = 0; i < 400; i++) hid_motion_sample(&state, &motion, 5000000, ++n, out);
    for (int i = 0; i < 9; i++) assert(isfinite(dir_at(out, i)));
    check_rotation(out);

    /* a delayed sample (over 100 ms) turns nothing, and the delta time still says how long it was */
    memset(&state, 0, sizeof state);
    memset(&motion, 0, sizeof motion);
    motion.valid = 1;
    motion.acceleration[2] = -1.0f;
    motion.angular_velocity[2] = 1.0f;
    hid_motion_sample(&state, &motion, 1000000000ull, ++n, out);
    assert(snx_ld_le64(out) == 1000000000ull && f32_at(out, ANGLE + 8) == 0.0f);

    /* a sensor that goes away starts over flat */
    hid_motion_sample(&state, NULL, 5000000, ++n, out);
    check_identity(out);
    puts("hid_motion: Eden's MotionInput, in the console's units");
    return 0;
}
