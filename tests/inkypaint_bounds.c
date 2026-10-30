/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/inkypaint_bounds.h"
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
/* Independent eight-corner reference, matching the guest's multiply/FMA order. */
static void reference(float out[6], const float box[6], const float matrix[12]) {
    for (unsigned row = 0; row < 3; row++) {
        out[row] = FLT_MAX;
        out[row + 3] = -FLT_MAX;
        for (unsigned i = 0; i < 8; i++) {
            const float* m = matrix + row * 4;
            float v = m[1] * box[i & 2 ? 4 : 1];
            v = fmaf(m[0], box[i & 1 ? 3 : 0], v);
            v = fmaf(m[2], box[i & 4 ? 5 : 2], v) + m[3];
            if (v < out[row]) out[row] = v;
            if (v > out[row + 3]) out[row + 3] = v;
        }
    }
}
int main(void) {
    uint32_t rng = 7;
    for (unsigned n = 0; n < 10000; n++) {
        float matrix[12], box[6], out[6], want[6];
        int32_t missing[6] = {-1, INT32_MIN, INT32_MAX, 8, 0x6131acb0, -12};
        for (unsigned i = 0; i < 12; i++) {
            rng = rng * 1664525u + 1013904223u;
            matrix[i] = ((int)(rng % 1025) - 512) / 16.f;
        }
        for (unsigned i = 0; i < 3; i++) {
            box[i] = -(float)(n % (i + 31));
            box[i + 3] = (float)(n % (i + 57));
        }
        reference(want, box, matrix);
        assert(inkypaint_bounds_transform(out, box, matrix, missing) == 6);
        for (unsigned i = 0; i < 6; i++) assert(out[i] == want[i]);
        /* Valid cached tie choices must retain the guest's exact result. */
        int32_t cached[6] = {0, 1, 2, 3, 4, 7};
        assert(!inkypaint_bounds_transform(out, box, matrix, cached));
        for (unsigned at = 0; at < 6; at++) {
            const float* m = matrix + (at % 3) * 4;
            unsigned i = (unsigned)cached[at];
            float v = m[1] * box[i & 2 ? 4 : 1];
            v = fmaf(m[0], box[i & 1 ? 3 : 0], v);
            v = fmaf(m[2], box[i & 4 ? 5 : 2], v) + m[3];
            assert(!memcmp(&v, &out[at], sizeof v));
        }
    }
    puts("PASS bounds: 10000 transforms, cached choices and invalid selector recovery");
    return 0;
}
