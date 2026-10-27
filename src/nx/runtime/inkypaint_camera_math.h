/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_CAMERA_MATH_H
#define INKYPAINT_CAMERA_MATH_H
#include <math.h>
#include <string.h>

/* Camera poses are row-major affine 3x4 matrices. Reflected/scaled/invalid views are left to the game. */
static inline int inkypaint_camera_rotation(const float m[12], float q[4]) {
    for (unsigned i = 0; i < 12; i++) if (!isfinite(m[i])) return 0;
    for (unsigned i = 0; i < 3; i++) {
        float n = 0;
        for (unsigned j = 0; j < 3; j++) n += m[4*i+j] * m[4*i+j];
        if (fabsf(n - 1) > .002f) return 0;
        for (unsigned k = i+1; k < 3; k++) {
            float d = 0;
            for (unsigned j = 0; j < 3; j++) d += m[4*i+j] * m[4*k+j];
            if (fabsf(d) > .002f) return 0;
        }
    }
    float det = m[0]*(m[5]*m[10]-m[6]*m[9]) - m[1]*(m[4]*m[10]-m[6]*m[8])
              + m[2]*(m[4]*m[9]-m[5]*m[8]);
    if (det < .998f) return 0;
    float trace = m[0] + m[5] + m[10];
    if (trace > 0) {
        float s = 2 * sqrtf(1 + trace);
        q[0] = (m[9]-m[6])/s; q[1] = (m[2]-m[8])/s; q[2] = (m[4]-m[1])/s; q[3] = s*.25f;
    } else {
        unsigned i = m[0] > m[5] ? 0 : 1;
        if (m[10] > m[4*i+i]) i = 2;
        unsigned j = (i+1)%3, k = (i+2)%3;
        float s = 2 * sqrtf(1 + m[4*i+i] - m[4*j+j] - m[4*k+k]);
        q[i] = s*.25f; q[j] = (m[4*i+j]+m[4*j+i])/s; q[k] = (m[4*i+k]+m[4*k+i])/s;
        q[3] = (m[4*k+j]-m[4*j+k])/s;
    }
    return 1;
}
static inline void inkypaint_camera_inverse(float out[12], const float pose[12]) {
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < 3; j++) out[4*i+j] = pose[4*j+i];
        out[4*i+3] = -(out[4*i]*pose[3] + out[4*i+1]*pose[7] + out[4*i+2]*pose[11]);
    }
}
static inline int inkypaint_camera_blend(float out[12], const float prev[12], const float curr[12], float alpha) {
    float p[4], q[4];
    if (!isfinite(alpha) || alpha < 0 || alpha > 1 || !inkypaint_camera_rotation(prev,p) ||
        !inkypaint_camera_rotation(curr,q)) return 0;
    float dot = 0, distance = 0;
    for (unsigned i = 0; i < 4; i++) dot += p[i]*q[i];
    for (unsigned i = 3; i < 12; i += 4) distance += (curr[i]-prev[i])*(curr[i]-prev[i]);
    /* Abrupt viewpoint changes use the current camera rather than travelling between unrelated shots. */
    if (fabsf(dot) < .5f || distance > 10000) return 0;
    float n = 0;
    for (unsigned i = 0; i < 4; i++) { q[i] = p[i]*(1-alpha) + q[i]*(dot < 0 ? -alpha : alpha); n += q[i]*q[i]; }
    n = 1/sqrtf(n);
    for (unsigned i = 0; i < 4; i++) q[i] *= n;
    float x=q[0], y=q[1], z=q[2], w=q[3];
    out[0]=1-2*(y*y+z*z); out[1]=2*(x*y-z*w); out[2]=2*(x*z+y*w);
    out[4]=2*(x*y+z*w); out[5]=1-2*(x*x+z*z); out[6]=2*(y*z-x*w);
    out[8]=2*(x*z-y*w); out[9]=2*(y*z+x*w); out[10]=1-2*(x*x+y*y);
    for (unsigned i = 3; i < 12; i += 4) out[i] = prev[i] + (curr[i]-prev[i])*alpha;
    return 1;
}
#endif
