/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* 11.3.0 reads its graphics heap budgets (Heaps.Graphics, 173568 KiB, and its child DynamicTextureAllocatorMem,
   98304 KiB) from the HeapDef BYML in its RomFS. They hold native-resolution render targets only: at a larger render
   scale a GPU-memory pool allocation fails and c61be60 dereferences the null it returns. The budgets scaled by the
   pixel count fit only in the console's larger application pools (memory_gb 6 or 8) */
#include "nx/runtime/inkypaint_heapscale.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/hle/hle.h"
#include "nx/hle/filesystem/fs_tree.h"
#include "common/utilities/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <zstd.h>

#define HEAPDEF "System/HeapSize/Product.Nin_NX_NVN.HeapDef.byml.zs"

static struct {
    int failed; /* the heaps could not be enlarged: the render scale stays 1 */
} g_heapscale;

float inkypaint_heapscale_clamp(float scale) { return g_heapscale.failed && scale > 1 ? 1.0f : scale; }

/* the game's HeapDef as its RomFS folder has it, malloc'd, NULL when there is none (tree.lock held) */
static unsigned char* read_original(size_t* size) {
    FsNode* node;
    FILE* f;
    unsigned char* data;
    if (fs_tree_lookup(&g_hle.fs.tree, NULL, "romfs/" HEAPDEF, &node) || node->is_dir || !node->size ||
        node->size > ((u64)1 << 24))
        return NULL;
    data = malloc((size_t)node->size);
    if (!data) return NULL;
    if (node->map) {
        memcpy(data, node->map, (size_t)node->size);
    } else {
        f = fs_tree_open(&g_hle.fs.tree, node, "rb");
        if (!f || fread(data, 1, (size_t)node->size, f) != node->size) {
            if (f) fclose(f);
            free(data);
            return NULL;
        }
        fclose(f);
    }
    *size = (size_t)node->size;
    return data;
}

/* the patched, compressed HeapDef, malloc'd, or NULL with why said */
static unsigned char* patched(unsigned factor, size_t* out) {
    size_t size = 0, plain, bound;
    unsigned long long content;
    unsigned char *original = read_original(&size), *decoded = NULL, *packed = NULL;
    if (!original) {
        WARN("heapscale: the game's RomFS folder has no romfs/%s to read\n", HEAPDEF);
        return NULL;
    }
    content = ZSTD_getFrameContentSize(original, size);
    if (content == ZSTD_CONTENTSIZE_ERROR || content == ZSTD_CONTENTSIZE_UNKNOWN || content > ((u64)1 << 24) ||
        !content || !(decoded = malloc((size_t)content))) {
        WARN("heapscale: %s is not a zstd frame of known size\n", HEAPDEF);
        goto done;
    }
    plain = ZSTD_decompress(decoded, (size_t)content, original, size);
    if (ZSTD_isError(plain) || plain != content) {
        WARN("heapscale: %s does not decompress: %s\n", HEAPDEF,
             ZSTD_isError(plain) ? ZSTD_getErrorName(plain) : "short");
        goto done;
    }
    if (inkypaint_heapscale_patch(decoded, plain, factor)) {
        WARN("heapscale: %s has no Heaps.Graphics.Size and Heaps.DynamicTextureAllocatorMem.Size to scale\n",
             HEAPDEF);
        goto done;
    }
    bound = ZSTD_compressBound(plain);
    if (!(packed = malloc(bound))) goto done;
    *out = ZSTD_compress(packed, bound, decoded, plain, 3);
    if (ZSTD_isError(*out)) {
        WARN("heapscale: compressing the patched %s failed: %s\n", HEAPDEF, ZSTD_getErrorName(*out));
        free(packed);
        packed = NULL;
    }
done:
    free(decoded);
    free(original);
    return packed;
}

/* writes data to path unless it already holds it, through a staged file renamed over it. 0 on success */
static int store(const char* path, const unsigned char* data, size_t size) {
    size_t had = 0;
    unsigned char* old = snx_read_file(path, &had);
    char* staged;
    int rc = -1;
    if (old && had == size && !memcmp(old, data, size)) rc = 0;
    free(old);
    if (!rc) return 0;
    staged = snx_format("%s.tmp", path);
    if (!staged) return -1;
    rc = snx_write_file(staged, data, size) || snx_replace(staged, path);
    if (rc) (void)remove(staged);
    free(staged);
    return rc ? -1 : 0;
}

void inkypaint_heapscale_graft(void) {
    float scale = g_settings.resolution_scale;
    unsigned factor = inkypaint_heapscale_factor(scale);
    int need = factor > 4 ? 8 : 6;
    size_t size = 0;
    unsigned char* data;
    char *top, *file = NULL, *dir = NULL;
    if (factor < 2) return;
    top = snx_format("%sheapscale/%u/romfs", g_cachedir, factor);
    data = top ? patched(factor, &size) : NULL;
    if (data) {
        file = snx_format("%s/%s", top, HEAPDEF);
        dir = snx_format("%s/System/HeapSize", top);
    }
    g_heapscale.failed = 1;
    if (!data || !file || !dir) {
        if (data) WARN("heapscale: out of memory for the patched HeapDef's path\n");
    } else if (snx_mkdirs(dir) || store(file, data, size)) {
        WARN("heapscale: could not write %s\n", file);
    } else {
        FsTreeResult r = fs_tree_graft(&g_hle.fs.tree, INKYPAINT_HEAPSCALE_LAYER, top);
        if (r != FS_TREE_OK) WARN("heapscale: %s can't be indexed (error %d)\n", top, r);
        else {
            g_heapscale.failed = 0;
            LOG("heapscale: graphics heaps x%u for render scale %.2f, laid over the game's RomFS under the mods'\n",
                factor, (double)scale);
        }
    }
    if (g_heapscale.failed) {
        WARN("heapscale: the game's graphics heaps stay at their native size, so the render scale is 1 instead of "
             "%.2f\n",
             (double)scale);
    } else if (g_settings.memory_gb < need) {
        LOG("heapscale: scale %.2f needs %d GB of game memory, memory_gb was %d\n", (double)scale, need,
            g_settings.memory_gb);
        g_settings.memory_gb = need;
    }
    free(data);
    free(dir);
    free(file);
    free(top);
}
