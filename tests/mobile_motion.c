/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/platform/mobile_motion_math.h"
#include <assert.h>
#include <stdio.h>
static int near(float a, float b) { return fabsf(a-b) < 0.00001f; }
int main(void) {
    const float turn = 6.28318530718f;
    float a[3] = {0, 0, 9.80665f}, w[3] = {turn, 2*turn, 3*turn}, g[3], v[3];
    assert(mobile_motion_convert(a,w,0,g,v));
    assert(near(g[0],0) && near(g[1],1) && near(g[2],0));
    assert(near(v[0],-2) && near(v[1],-3) && near(v[2],1));
    assert(mobile_motion_convert(a,w,1,g,v));
    assert(near(v[0],2) && near(v[1],-3) && near(v[2],-1));
    a[0] = 9.80665f; a[2] = 0;
    assert(mobile_motion_convert(a,w,0,g,v) && near(g[2],-1));
    assert(mobile_motion_convert(a,w,1,g,v) && near(g[2],1));
    a[0] = 0;
    assert(!mobile_motion_convert(a,w,0,g,v));
    a[2] = NAN;
    assert(!mobile_motion_convert(a,w,0,g,v));
    a[2] = 9.80665f; w[0] = INFINITY;
    assert(!mobile_motion_convert(a,w,0,g,v));
    puts("PASS phone sensor units, landscape rotation, gravity and invalid samples");
}
