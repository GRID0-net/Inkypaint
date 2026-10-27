/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A motion sensor's SixAxisSensorState: Eden's MotionInput (src/hid_core/frontend/motion_input.cpp, Copyright 2026
   Eden Emulator Project, GPL-3.0-or-later; Copyright 2020 yuzu Emulator Project, GPL-2.0-or-later), ported to C with its
   constants and its order of steps, as Eden reports motion to games that aim with it.

   Angular velocity is in revolutions a second and the angle in revolutions, as the console reports them. The sensor's
   resting reading (its bias) is learned while it rests and taken off every reading, and readings under a threshold are
   none: a host pad's bias, unremoved, turned the camera on its own. The orientation is Mahony's filter over the gyro,
   corrected toward gravity, and the direction matrix is made from it the way Eden's GetOrientation makes it. */
#include "nx/hle/services/hid_motion.h"
#include "common/utilities/endian.h"
#include <math.h>
#include <string.h>

#define THRESHOLD       0.007f /* ThresholdStandard: a smaller reading is none, in revolutions a second */
#define AT_REST         0.01f  /* IsAtRestStandard: the console's resting flag */
#define AT_REST_RELAXED 0.05f  /* IsAtRestRelaxed: still enough to learn the bias from */
#define GYRO_MAX        5.0f
#define ACCEL_MAX       7.0f
#define TWO_PI          6.28318530718f
static const float kp = 0.3f, ki = 0.005f, kd = 0.0f; /* the gravity correction's gains (SetPID's defaults) */

static float length3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
static float clampf(float v, float limit) { return v < -limit ? -limit : v > limit ? limit : v; }
static int moving(const float gyro[3], const float accel[3], float sensitivity) {
    float a = length3(accel);
    return length3(gyro) >= sensitivity || a <= 0.9f || a >= 1.1f;
}
static int calibrated(const HidMotionState* m, float sensitivity) { return length3(m->real_error) < sensitivity; }
/* the resting orientation: flat, face up */
static void reset_quaternion(HidMotionState* m) {
    m->quat[0] = m->quat[1] = m->quat[3] = 0.0f;
    m->quat[2] = -1.0f;
}
static void normalize4(float q[4]) {
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n > 0.0f)
        for (int i = 0; i < 4; i++) q[i] /= n;
}

/* SetGyroscope: the bias taken off, clamped, the bias learned at rest, a reading under the threshold none */
static void set_gyroscope(HidMotionState* m, const float raw[3], const float accel[3], float gyro[3]) {
    for (int i = 0; i < 3; i++) gyro[i] = clampf(raw[i] - m->gyro_bias[i], GYRO_MAX);
    if (!moving(gyro, accel, AT_REST_RELAXED))
        for (int i = 0; i < 3; i++) m->gyro_bias[i] = m->gyro_bias[i] * 0.9999f + raw[i] * 0.0001f;
    if (length3(gyro) < THRESHOLD)
        gyro[0] = gyro[1] = gyro[2] = 0.0f;
    else
        m->only_accelerometer = 0;
}
static void update_rotation(HidMotionState* m, const float gyro[3], float period) {
    if (period > 0.1f) return;
    for (int i = 0; i < 3; i++) m->rotations[i] += gyro[i] * period;
}

/* the quaternion's step under rad (radians a second, MotionInput's axes) over period */
static void integrate(HidMotionState* m, const float rad[3], float period) {
    float q1 = m->quat[3], q2 = m->quat[0], q3 = m->quat[1], q4 = m->quat[2];
    const float gx = rad[1], gy = rad[0], gz = rad[2], pa = q2, pb = q3, pc = q4;
    /* q1 is updated first and read by the others, as Eden has it */
    q1 = q1 + (-q2 * gx - q3 * gy - q4 * gz) * (0.5f * period);
    q2 = pa + (q1 * gx + pb * gz - pc * gy) * (0.5f * period);
    q3 = pb + (q1 * gy - pa * gz + pc * gx) * (0.5f * period);
    q4 = pc + (q1 * gz + pa * gy - pb * gx) * (0.5f * period);
    m->quat[3] = q1;
    m->quat[0] = q2;
    m->quat[1] = q3;
    m->quat[2] = q4;
    normalize4(m->quat);
}
/* the error between the gravity the orientation expects and the gravity measured (na, normalized) */
static void gravity_error(HidMotionState* m, const float na[3]) {
    const float q1 = m->quat[3], q2 = m->quat[0], q3 = m->quat[1], q4 = m->quat[2];
    const float ax = -na[0], ay = na[1], az = -na[2];
    const float vx = 2.0f * (q2 * q4 - q1 * q3), vy = 2.0f * (q1 * q2 + q3 * q4),
                vz = q1 * q1 - q2 * q2 - q3 * q3 + q4 * q4;
    const float e[3] = {az * vx - ax * vz, ay * vz - az * vy, ax * vy - ay * vx};
    for (int i = 0; i < 3; i++) {
        m->derivative_error[i] = e[i] - m->real_error[i];
        m->real_error[i] = e[i];
    }
}
static void orientation_from_accelerometer(HidMotionState* m, const float accel[3]) {
    float a = length3(accel), na[3];
    if (a <= 0.0f) return;
    for (int i = 0; i < 3; i++) na[i] = accel[i] / a;
    gravity_error(m, na); /* measured first: a sensor just started has none yet, which would read as settled */
    for (int iterations = 0; !calibrated(m, 0.01f) && ++iterations < 100;) {
        float rad[3];
        gravity_error(m, na);
        for (int i = 0; i < 3; i++)
            rad[i] = 10.0f * kp * m->real_error[i] + 5.0f * ki * m->integral_error[i] + 10.0f * kd * m->derivative_error[i];
        integrate(m, rad, 0.015f);
    }
}
/* ResetOrientation: flat and still long enough, the orientation starts again from gravity */
static void reset_orientation(HidMotionState* m, const float gyro[3], const float accel[3]) {
    if (m->only_accelerometer) return;
    if (!moving(gyro, accel, AT_REST_RELAXED) && accel[2] <= -0.9f) {
        if (++m->reset_counter > 900) {
            reset_quaternion(m);
            orientation_from_accelerometer(m, accel);
            memset(m->integral_error, 0, sizeof m->integral_error);
            m->reset_counter = 0;
        }
    } else
        m->reset_counter = 0;
}
/* UpdateOrientation: Mahony's filter (Madgwick's implementation), gyro in revolutions a second. Without a gyro, the
   accelerometer's correction stands in for it, and gyro gets what it emulates */
static void update_orientation(HidMotionState* m, const float accel[3], float gyro[3], float period) {
    float a = length3(accel), rad[3], swap;
    if (!calibrated(m, 0.1f)) reset_orientation(m, gyro, accel);
    if (period > 0.1f) return;
    for (int i = 0; i < 3; i++) rad[i] = gyro[i] * TWO_PI;
    swap = rad[0];
    rad[0] = rad[1];
    rad[1] = -swap;
    rad[2] = -rad[2];
    if (m->only_accelerometer) rad[0] = rad[1] = rad[2] = 0.0f;
    if (a >= 0.75f && a <= 1.25f) {
        float na[3] = {accel[0] / a, accel[1] / a, accel[2] / a};
        gravity_error(m, na);
        if (ki != 0.0f && !calibrated(m, 0.05f))
            for (int i = 0; i < 3; i++) m->integral_error[i] += m->real_error[i];
        else
            memset(m->integral_error, 0, sizeof m->integral_error);
        if (!m->only_accelerometer)
            for (int i = 0; i < 3; i++)
                rad[i] += kp * m->real_error[i] + ki * m->integral_error[i] + kd * m->derivative_error[i];
        else {
            for (int i = 0; i < 3; i++)
                rad[i] += 35.0f * kp * m->real_error[i] + 10.0f * ki * m->integral_error[i] +
                          10.0f * kd * m->derivative_error[i];
            gyro[0] = -rad[1];
            gyro[1] = rad[0];
            gyro[2] = -rad[2];
            update_rotation(m, gyro, period);
        }
    }
    integrate(m, rad, period);
}
/* GetOrientation: the HID direction matrix's rows from the quaternion, in the console's frame */
static void direction(const HidMotionState* m, float dir[9]) {
    const float x = -m->quat[1], y = -m->quat[0], z = -m->quat[3], w = -m->quat[2];
    const float x2 = x * x, y2 = y * y, z2 = z * z, xy = x * y, wz = w * z, xz = x * z, wy = w * y, yz = y * z,
                wx = w * x;
    dir[0] = 1.0f - 2.0f * (y2 + z2);
    dir[1] = 2.0f * (xy + wz);
    dir[2] = -2.0f * (xz - wy);
    dir[3] = 2.0f * (xy - wz);
    dir[4] = 1.0f - 2.0f * (x2 + z2);
    dir[5] = -2.0f * (yz + wx);
    dir[6] = -2.0f * (xz + wy);
    dir[7] = -2.0f * (yz - wx);
    dir[8] = 1.0f - 2.0f * (x2 + y2);
}

int hid_motion_at_rest(const PadMotion* motion) {
    if (!motion || !motion->valid) return 1;
    return !moving(motion->angular_velocity, motion->acceleration, AT_REST);
}

void hid_motion_sample(HidMotionState* state, const PadMotion* motion, u64 dt_ns, u64 sampling_number,
                       u8 out[HS_HidSixAxisSensorState]) {
    float acc[3] = {0.0f, 0.0f, -1.0f}, gyro[3] = {0.0f, 0.0f, 0.0f}, dir[9];
    float period = (float)dt_ns * 0.000000001f;
    u32 attr = 1; /* HidSixAxisSensorAttribute_IsConnected */
    memset(out, 0, HS_HidSixAxisSensorState);
    if (motion && motion->valid) {
        for (int i = 0; i < 3; i++) acc[i] = clampf(motion->acceleration[i], ACCEL_MAX);
        if (!state->started) {
            /* a sensor's first reading: its tilt from gravity at once (Eden's own reset), where Eden starts flat and
               turns to the tilt over seconds */
            memset(state, 0, sizeof *state);
            reset_quaternion(state);
            orientation_from_accelerometer(state, acc);
            state->only_accelerometer = 1;
            state->started = 1;
        }
        set_gyroscope(state, motion->angular_velocity, acc, gyro);
        update_rotation(state, gyro, period);
        update_orientation(state, acc, gyro, period);
        direction(state, dir);
    } else {
        /* no sensor: a pad flat and still, and a sensor that comes back starts over */
        memset(state, 0, sizeof *state);
        reset_quaternion(state);
        direction(state, dir);
    }
    memcpy(out, &dt_ns, 8);
    memcpy(out + 8, &sampling_number, 8);
    memcpy(out + HO_HidSixAxisSensorState_acceleration, acc, sizeof acc);
    memcpy(out + HO_HidSixAxisSensorState_acceleration + 12, gyro, sizeof gyro);
    memcpy(out + HO_HidSixAxisSensorState_acceleration + 24, state->rotations, sizeof state->rotations);
    memcpy(out + HO_HidSixAxisSensorState_direction, dir, sizeof dir);
    memcpy(out + HO_HidSixAxisSensorState_attributes, &attr, 4);
    /* the game reads it little-endian: the two counts, then words (floats and the attributes) to the end */
    snx_le64_array(out, 2);
    snx_le32_array(out + 16, (HS_HidSixAxisSensorState - 16) / 4);
}
