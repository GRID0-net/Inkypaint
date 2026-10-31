/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Graphics heap budgets for render scales above 1. The game sizes its graphics heaps from
   romfs/System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs, budgets made for native resolution, and a larger scene
   fails a GPU-memory pool allocation. Above 1x the runtime lays a copy with Graphics and DynamicTextureAllocatorMem
   scaled by the pixel count over the game's RomFS, below the player's mods, and raises the game's memory to hold it */
#ifndef INKYPAINT_HEAPSCALE_H
#define INKYPAINT_HEAPSCALE_H
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* the tree's top-level name of the patched RomFS layer, grafted by inkypaint_heapscale_graft */
#define INKYPAINT_HEAPSCALE_LAYER "inkypaint.heapscale"

/* The heaps' multiplier for a render scale: its pixel count rounded up, at least 2 above 1x, at most 16. 1 (no
   patch) at 1x or below, or for a scale that is not a number */
static inline unsigned inkypaint_heapscale_factor(float scale) {
    double f;
    if (!(scale > 1.0f)) return 1;
    f = ceil((double)scale * (double)scale - 1e-6);
    if (f < 2) f = 2;
    if (f > 16) f = 16;
    return (unsigned)f;
}

static inline uint32_t inkypaint_heapscale_u24(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
}
static inline uint32_t inkypaint_heapscale_u32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* the offset of the value of key in the dict node at node whose type is type, 0 when there is none or the node
   is not a dict inside the buffer. Key indices are matched against the key table's strings, all bounds checked */
static inline size_t inkypaint_heapscale_find(const uint8_t* d, size_t n, size_t keys, size_t node, const char* key,
                                              uint8_t type) {
    uint32_t count, nkeys, i;
    size_t len = strlen(key);
    if (node > n || n - node < 4 || d[node] != 0xc1) return 0;
    count = inkypaint_heapscale_u24(d + node + 1);
    if (count > (n - node - 4) / 8) return 0;
    nkeys = inkypaint_heapscale_u24(d + keys + 1);
    for (i = 0; i < count; i++) {
        const uint8_t* e = d + node + 4 + (size_t)i * 8;
        uint32_t k = inkypaint_heapscale_u24(e), start, end;
        if (e[3] != type || k >= nkeys) continue;
        start = inkypaint_heapscale_u32(d + keys + 4 + (size_t)k * 4);
        end = inkypaint_heapscale_u32(d + keys + 8 + (size_t)k * 4);
        if (start >= end || end > n - keys || end - start != len + 1) continue;
        if (!memcmp(d + keys + start, key, len) && !d[keys + start + len]) return (size_t)(e + 4 - d);
    }
    return 0;
}
/* Multiplies Heaps.Graphics.Size and Heaps.DynamicTextureAllocatorMem.Size in a little-endian BYML (decompressed
   HeapDef) in place by factor, each clamped to INT32_MAX / 2. 0 when both were patched, -1 when the data is not such
   a BYML or a heap or its size is missing, the buffer then left unchanged */
static inline int inkypaint_heapscale_patch(uint8_t* d, size_t n, unsigned factor) {
    static const char* const heaps[2] = {"Graphics", "DynamicTextureAllocatorMem"};
    size_t keys, root, at, sizes[2];
    uint8_t types[2];
    uint32_t nkeys;
    unsigned i;
    if (!d || n < 16 || d[0] != 'Y' || d[1] != 'B' || factor < 1 || factor > 16) return -1;
    keys = inkypaint_heapscale_u32(d + 4);
    root = inkypaint_heapscale_u32(d + 12);
    if (keys > n || n - keys < 8 || d[keys] != 0xc2) return -1;
    nkeys = inkypaint_heapscale_u24(d + keys + 1);
    if (nkeys > (n - keys - 8) / 4) return -1; /* offsets[nkeys + 1] */
    at = inkypaint_heapscale_find(d, n, keys, root, "Heaps", 0xc1);
    if (!at) return -1;
    root = inkypaint_heapscale_u32(d + at);
    for (i = 0; i < 2; i++) {
        size_t heap = inkypaint_heapscale_find(d, n, keys, root, heaps[i], 0xc1), size = 0;
        if (!heap) return -1;
        heap = inkypaint_heapscale_u32(d + heap);
        types[i] = 0xd1;
        size = inkypaint_heapscale_find(d, n, keys, heap, "Size", 0xd1);
        if (!size) {
            types[i] = 0xd3;
            size = inkypaint_heapscale_find(d, n, keys, heap, "Size", 0xd3);
        }
        if (!size) return -1;
        sizes[i] = size;
    }
    for (i = 0; i < 2; i++) {
        uint32_t raw = inkypaint_heapscale_u32(d + sizes[i]);
        uint64_t value = types[i] == 0xd1 && (raw & 0x80000000u) ? 0 : raw, scaled = value * factor;
        if (scaled > INT32_MAX / 2) scaled = INT32_MAX / 2;
        if (scaled < value) scaled = value; /* a size already above the clamp stays as it was */
        d[sizes[i]] = (uint8_t)scaled;
        d[sizes[i] + 1] = (uint8_t)(scaled >> 8);
        d[sizes[i] + 2] = (uint8_t)(scaled >> 16);
        d[sizes[i] + 3] = (uint8_t)(scaled >> 24);
    }
    return 0;
}

/* Startup, from the filesystem's start under g_hle.fs.tree.lock, before the game's memory is made: above 1x, writes the
   patched HeapDef into the cache folder when it isn't there, grafts its romfs/ folder as INKYPAINT_HEAPSCALE_LAYER,
   and raises memory_gb to what the factor needs. When it can't, the render scale falls back to 1, said */
void inkypaint_heapscale_graft(void);
/* the render scale the game can take: scale, or 1 when the heaps could not be enlarged for it */
float inkypaint_heapscale_clamp(float scale);
#endif
