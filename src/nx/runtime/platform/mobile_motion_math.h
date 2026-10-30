/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_MOBILE_MOTION_MATH_H
#define INKYPAINT_MOBILE_MOTION_MATH_H
#include <math.h>
/* SDL phone sensors remain in portrait axes after a display rotation. Turn them
   into landscape screen axes, then into the same Horizon frame as an SDL pad.
   flipped=0: device top at the screen's left; flipped=1: top at the right. */
static inline int mobile_motion_convert(const float a[3], const float w[3], int flipped,
                                       float acceleration[3], float angular_velocity[3]) {
    float sign = flipped ? -1.f : 1.f;
    for (int i = 0; i < 3; i++) if (!isfinite(a[i]) || !isfinite(w[i])) return 0;
    /* CoreMotion has no reading immediately after its sensors open. */
    if (a[0]*a[0] + a[1]*a[1] + a[2]*a[2] < 0.0001f) return 0;
    acceleration[0] = sign * a[1] / 9.80665f;
    acceleration[1] = a[2] / 9.80665f;
    acceleration[2] = -sign * a[0] / 9.80665f;
    angular_velocity[0] = -sign * w[1] / 6.28318530718f;
    angular_velocity[1] = -w[2] / 6.28318530718f;
    angular_velocity[2] = sign * w[0] / 6.28318530718f;
    return 1;
}
#endif
