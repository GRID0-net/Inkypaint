/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_FRAME_CLOCK_H
#define INKYPAINT_FRAME_CLOCK_H
#include <stdint.h>

/* A render sample owns its tick and fraction. Never reconstruct these later on a worker from wall time. */
typedef struct {
    uint64_t origin_ns, ticks;
    int started;
} InkypaintFrameClock;
typedef struct {
    uint64_t tick, discarded;
    unsigned steps;
    float alpha;
    int reset;
} InkypaintFrameSample;

static inline InkypaintFrameSample inkypaint_frame_sample(InkypaintFrameClock* clock, uint64_t now_ns) {
    InkypaintFrameSample sample = {0};
    if (!clock->started || now_ns < clock->origin_ns) {
        clock->started = 1;
        clock->origin_ns = now_ns;
        clock->ticks = 0;
        sample.reset = 1;
    }
    uint64_t elapsed = now_ns - clock->origin_ns;
    uint64_t phase = elapsed % 1000000000ull * 60;
    uint64_t wanted = 1 + elapsed / 1000000000ull * 60 + phase / 1000000000ull;
    if (wanted < clock->ticks) {
        clock->origin_ns = now_ns;
        wanted = 1;
        clock->ticks = phase = 0;
        sample.reset = 1;
    }
    uint64_t due = wanted - clock->ticks;
    /* Match the framework's six-step catch-up bound. A longer suspension invalidates visual history. */
    if (due > 6) {
        sample.discarded = due - 6;
        due = 6;
        sample.reset = 1;
    }
    sample.steps = (unsigned)due;
    sample.tick = clock->ticks = wanted;
    sample.alpha = (float)(phase % 1000000000ull) / 1000000000.f;
    if (sample.alpha >= 1) sample.alpha = 0x1.fffffep-1f;
    return sample;
}
#endif
