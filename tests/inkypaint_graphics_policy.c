/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/inkypaint_graphics_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    InkypaintResolutionPolicy p = {0};
    float scale = 1;
    for (unsigned i = 0; i < 29; i++) {
        scale = inkypaint_resolution_sample(&p, 20000000, scale, .5f, 1, 60);
        assert(scale == 1); /* no resource reallocations every frame */
    }
    scale = inkypaint_resolution_sample(&p, 20000000, scale, .5f, 1, 60);
    assert(scale == .875f);
    for (unsigned i = 0; i < 3000; i++) scale = inkypaint_resolution_sample(&p, 20000000, scale, .5f, 1, 60);
    assert(scale == .5f);
    for (unsigned i = 0; i < 3000; i++) scale = inkypaint_resolution_sample(&p, 5000000, scale, .5f, 1, 60);
    assert(scale == 1);
    for (unsigned i = 0; i < 3000; i++) scale = inkypaint_resolution_sample(&p, 14000000, scale, .5f, 1, 60);
    assert(scale == 1); /* dead band: stable without quality oscillation */
    assert(inkypaint_resolution_sample(&p, 1000000000, scale, .5f, 1, 60) == scale);
    assert(inkypaint_resolution_sample(&p, 0, scale, .5f, 1, 60) == scale);
    for (unsigned i = 0; i < 3000; i++) scale = inkypaint_resolution_sample(&p, 20000000, scale, 1, .5f, 60);
    assert(scale == .5f); /* a minimum above the ceiling never raises it */
    p = (InkypaintResolutionPolicy){0};
    scale = 1;
    for (unsigned i = 0; i < 3000; i++) scale = inkypaint_resolution_sample(&p, 20000000, scale, .5f, 1, 30);
    assert(scale == 1); /* 30 FPS target has a distinct budget */
    puts("PASS graphics policy: bounded steps, hysteresis, minimum/maximum, bad samples, frame budgets");
    return 0;
}
