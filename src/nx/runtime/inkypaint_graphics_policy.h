/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_GRAPHICS_POLICY_H
#define INKYPAINT_GRAPHICS_POLICY_H
#include <stdint.h>
#include <math.h>
typedef struct {
    double filtered_ns;
    unsigned samples;
} InkypaintResolutionPolicy;
/* GPU work excludes the game's 30/60 FPS pacing. Bounded steps and a dead band avoid resource churn. */
static inline float inkypaint_resolution_sample(InkypaintResolutionPolicy* p, uint64_t ns, float scale,
                                                float minimum, float maximum, unsigned target) {
    if (ns < 100000 || ns > 500000000 || !target) return scale;
    p->filtered_ns = p->filtered_ns ? p->filtered_ns * .9 + ns * .1 : (double)ns;
    if (++p->samples < 30) return scale;
    p->samples = 0;
    double budget = 1e9 / target;
    /* At most nine scale variants across the range, instead of accumulating dozens of differently sized targets.
       Raise quality only with room for the next step's pixel count, avoiding low-scale oscillation. */
    float step = maximum * .125f;
    float level = ceilf((maximum - scale) / step - .0001f);
    float next = fminf(maximum, maximum - fmaxf(0, level - 1) * step);
    double ratio = scale > 0 ? (double)next / scale : 1;
    if (p->filtered_ns > budget * .94) scale = maximum - (level + 1) * step;
    else if (p->filtered_ns < budget * fmin(.72, .85 / (ratio * ratio))) scale = next;
    return fminf(maximum, fmaxf(fminf(maximum, minimum), scale));
}
#endif
