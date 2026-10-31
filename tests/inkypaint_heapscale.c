/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* inkypaint_heapscale_patch on a synthetic HeapDef-shaped BYML, never the game's */
#include "nx/runtime/inkypaint_heapscale.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <zstd.h>

static size_t put32(uint8_t* d, size_t at, uint32_t v) {
    d[at] = (uint8_t)v;
    d[at + 1] = (uint8_t)(v >> 8);
    d[at + 2] = (uint8_t)(v >> 16);
    d[at + 3] = (uint8_t)(v >> 24);
    return at + 4;
}
static size_t node(uint8_t* d, size_t at, uint8_t type, uint32_t count) {
    return put32(d, at, type | count << 8);
}
static size_t entry(uint8_t* d, size_t at, uint32_t key, uint8_t type, uint32_t value) {
    at = put32(d, at, key | (uint32_t)type << 24);
    return put32(d, at, value);
}

/* keys sorted as BYML has them: DynamicTextureAllocatorMem 0, Graphics 1, Heaps 2, Size 3 */
static size_t build(uint8_t* d) {
    static const char* const keys[] = {"DynamicTextureAllocatorMem", "Graphics", "Heaps", "Size"};
    size_t at = 16, kt = at, s = 4 + 5 * 4, root, heaps, gfx, dyn, end;
    unsigned i;
    memset(d, 0, 256);
    d[0] = 'Y';
    d[1] = 'B';
    d[2] = 3;
    at = node(d, at, 0xc2, 4);
    for (i = 0; i < 4; i++) {
        put32(d, at + i * 4, (uint32_t)s);
        memcpy(d + kt + s, keys[i], strlen(keys[i]) + 1);
        s += strlen(keys[i]) + 1;
    }
    put32(d, at + 16, (uint32_t)s);
    at = (kt + s + 3) & ~(size_t)3;
    root = at;
    heaps = root + 12;
    gfx = heaps + 20;
    dyn = gfx + 12;
    end = dyn + 12;
    at = node(d, root, 0xc1, 1);
    entry(d, at, 2, 0xc1, (uint32_t)heaps);
    at = node(d, heaps, 0xc1, 2);
    at = entry(d, at, 0, 0xc1, (uint32_t)dyn);
    entry(d, at, 1, 0xc1, (uint32_t)gfx);
    entry(d, node(d, gfx, 0xc1, 1), 3, 0xd1, 173568);
    entry(d, node(d, dyn, 0xc1, 1), 3, 0xd3, 98304);
    put32(d, 4, (uint32_t)kt);
    put32(d, 8, 0);
    put32(d, 12, (uint32_t)root);
    return end;
}

int main(void) {
    uint8_t d[256], copy[256];
    size_t n = build(d), i, bound, packed, plain;
    uint8_t *z, *back;
    assert(inkypaint_heapscale_factor(0.5f) == 1);
    assert(inkypaint_heapscale_factor(1.0f) == 1);
    assert(inkypaint_heapscale_factor(1.1f) == 2);
    assert(inkypaint_heapscale_factor(1.5f) == 3);
    assert(inkypaint_heapscale_factor(2.0f) == 4);
    assert(inkypaint_heapscale_factor(3.0f) == 9);
    assert(inkypaint_heapscale_factor(4.0f) == 16);
    assert(inkypaint_heapscale_factor(9.0f) == 16);

    memcpy(copy, d, n);
    assert(!inkypaint_heapscale_patch(d, n, 4));
    assert(inkypaint_heapscale_u32(d + n - 4) == 98304u * 4);  /* DynamicTextureAllocatorMem, 0xd3 */
    assert(inkypaint_heapscale_u32(d + n - 16) == 173568u * 4); /* Graphics, 0xd1 */
    for (i = 0; i < n - 16; i++) assert(d[i] == copy[i]);

    /* every truncation fails, inside its buffer (the copies are malloc'd to the length, for sanitizers) */
    for (i = 0; i < n; i++) {
        uint8_t* t = malloc(i ? i : 1);
        memcpy(t, copy, i);
        assert(inkypaint_heapscale_patch(t, i, 4) == -1);
        free(t);
    }
    /* garbage: offsets anywhere, the patch reading nothing outside the buffer */
    srand(1);
    for (i = 0; i < 20000; i++) {
        uint8_t* t = malloc(n);
        size_t k;
        memcpy(t, copy, n);
        for (k = 0; k < 4; k++) t[2 + (size_t)rand() % (n - 2)] = (uint8_t)rand();
        (void)inkypaint_heapscale_patch(t, n, 4);
        free(t);
    }
    {
        uint8_t* t = malloc(n);
        memcpy(t, copy, n);
        put32(t, 12, 0xfffffff0u); /* root outside */
        assert(inkypaint_heapscale_patch(t, n, 4) == -1);
        memcpy(t, copy, n);
        t[n - 8] = 4; /* the last heap's Size names a key that doesn't exist */
        assert(inkypaint_heapscale_patch(t, n, 4) == -1);
        assert(!memcmp(t + 16, copy + 16, n - 24)); /* left unchanged */
        free(t);
    }
    assert(inkypaint_heapscale_patch(copy, n, 0) == -1);
    assert(inkypaint_heapscale_patch(copy, n, 17) == -1);

    /* the patched bytes through zstd, as the runtime writes them */
    bound = ZSTD_compressBound(n);
    z = malloc(bound);
    back = malloc(n);
    packed = ZSTD_compress(z, bound, d, n, 3);
    assert(!ZSTD_isError(packed));
    assert(ZSTD_getFrameContentSize(z, packed) == n);
    plain = ZSTD_decompress(back, n, z, packed);
    assert(plain == n && !memcmp(back, d, n));
    free(z);
    free(back);
    puts("inkypaint_heapscale: ok");
    return 0;
}
