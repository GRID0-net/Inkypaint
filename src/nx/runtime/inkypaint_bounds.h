/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_BOUNDS_H
#define INKYPAINT_BOUNDS_H
#include <math.h>
#include <stdint.h>
/* MatrixClipper's eight corners are indexed by x/y/z high bits. Preserve the
   game's cached choices when valid (including its tie-breaking), but derive a
   missing choice from the authoritative matrix instead of indexing off-stack.
   This also avoids building a 96-byte corner array for every bounds query. */
static inline unsigned inkypaint_bounds_transform(float out[6], const float box[6], const float matrix[12],
                                                 const int32_t cached[6]) {
    unsigned repaired = 0;
    for (unsigned end = 0; end < 2; end++)
        for (unsigned row = 0; row < 3; row++) {
            const float* m = matrix + row * 4;
            unsigned at = end * 3 + row;
            uint32_t corner = (uint32_t)cached[at];
            if (corner >= 8) {
                corner = 0;
                for (unsigned axis = 0; axis < 3; axis++)
                    if ((m[axis] < 0) != (end != 0)) corner |= 1u << axis;
                repaired++;
            }
            float x = box[corner & 1 ? 3 : 0], y = box[corner & 2 ? 4 : 1], z = box[corner & 4 ? 5 : 2];
            float v = m[1] * y;
            v = fmaf(m[0], x, v);
            v = fmaf(m[2], z, v);
            out[at] = v + m[3];
        }
    return repaired;
}
#endif
