/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/inkypaint_frame_clock.h"
#include <assert.h>
#include <stdio.h>

static void cadence(unsigned hz) {
    InkypaintFrameClock clock = {0};
    uint64_t steps = 0;
    for (unsigned frame = 0; frame <= hz * 10; frame++) {
        InkypaintFrameSample sample = inkypaint_frame_sample(&clock, (uint64_t)frame * 1000000000ull / hz);
        steps += sample.steps;
        assert(sample.alpha >= 0 && sample.alpha < 1);
        assert(!sample.discarded);
        assert(sample.reset == (frame == 0));
        assert(sample.tick == steps);
    }
    assert(steps == 601);
}
int main(void) {
    cadence(60);
    cadence(90);
    cadence(100);
    cadence(120);
    cadence(144);
    InkypaintFrameClock clock = {0};
    InkypaintFrameSample sample = inkypaint_frame_sample(&clock, 1000000000ull);
    assert(sample.steps == 1 && sample.reset && sample.alpha == 0);
    sample = inkypaint_frame_sample(&clock, 1008333333ull);
    assert(!sample.steps && sample.alpha > .49f && sample.alpha < .51f);
    sample = inkypaint_frame_sample(&clock, 1050000000ull);
    assert(sample.steps == 3 && !sample.reset && !sample.discarded);
    sample = inkypaint_frame_sample(&clock, 2050000000ull);
    assert(sample.steps == 6 && sample.reset && sample.discarded == 54);
    sample = inkypaint_frame_sample(&clock, 2050000000ull);
    assert(!sample.steps && !sample.reset);
    sample = inkypaint_frame_sample(&clock, 1500000000ull);
    assert(sample.steps == 1 && sample.reset && sample.tick == 1);
    sample = inkypaint_frame_sample(&clock, 999999999ull);
    assert(sample.steps == 1 && sample.reset && sample.tick == 1);
    /* A large monotonic value must not overflow when converted to ticks. */
    sample = inkypaint_frame_sample(&clock, UINT64_MAX);
    assert(sample.steps == 6 && sample.reset);
    puts("fixed 60 Hz clock: cadence, interpolation fraction, catch-up and reset passed");
    return 0;
}
