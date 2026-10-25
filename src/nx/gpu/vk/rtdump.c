/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* SNX_RT_DUMP=dir: one frame's rendering passes as pictures, to find the pass where a frame goes wrong. At the end
   of each pass of frame SNX_RT_DUMP_AT (presents counted from the start, 1500 by default), each color target is
   blitted to RGBA16F at up to 960 pixels wide and copied into a buffer of its own; at the next present the frame is
   waited for and every picture written as dir/<pass>_<slot>_<address>_<w>x<h>_f<format>.ppm, with index.txt naming
   each pass's targets, draws and programs. A float target (HDR, values far from 0..1) is scaled so its brightest
   pixels but one in 200 are white, then gamma encoded, and index.txt gives the scale and the range of each picture;
   UNORM targets are written as they are. A debugging tool: the pictures are the game's frame and stay local */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/vk/vk.h"
#include "nx/runtime/platform/log.h"

#define RTD_MAX   768 /* pictures a frame */
#define RTD_PROGS 8   /* programs listed for a pass */
#define RTD_TEXS  24  /* textures listed for a pass */

typedef struct {
    const Surf *found, *bound;
    int stage, slot;
    u32 crop_gen, found_wgen;
    int took_drawn;
} RtdTex;
typedef struct {
    VkImage img;
    VmaAllocation img_mem;
    VkBuffer buf;
    VmaAllocation buf_mem;
    u8* px;
    u32 w, h, pass, slot;
    u64 gva;
    u32 sw, sh, fmt;
    u32 layer, level; /* a texture's subresource (SNX_RT_DUMP_TEX), else 0 */
} RtdPic;

static struct {
    int on; /* -1 not decided, 0 off, 1 on */
    int fill;  /* SNX_RT_DUMP_FILL: full-size color targets start the dumped frame magenta, so what no draw wrote shows */
    const Surf* filled[16];
    u32 nfilled;
    int split; /* SNX_RT_DUMP_SPLIT: each draw ends its pass, so the pictures are the main target after each draw */
    const char* dir;
    u32 at;      /* the present whose frame is dumped */
    int done;
    u32 pass;    /* passes ended in the frame */
    u32 npic;
    RtdPic pic[RTD_MAX];
    /* the open pass's draws and programs */
    u32 draws;
    u64 vs[RTD_PROGS], fs[RTD_PROGS];
    u32 nprog;
    /* the textures the draws of the pass sampled, and those bound for the next draw: its setup comes before the
       pass it is drawn in begins */
    RtdTex tex[RTD_TEXS], next[RTD_TEXS];
    u32 ntex, nnext;
    FILE* index;
} g_rtd = {.on = -1};

static int rtd_on(void) {
    if (g_rtd.on < 0) {
        g_rtd.dir = getenv("SNX_RT_DUMP");
        const char* at = getenv("SNX_RT_DUMP_AT");
        g_rtd.at = at && *at ? (u32)strtoul(at, NULL, 10) : 1500;
        g_rtd.on = g_rtd.dir && *g_rtd.dir;
        const char* split = getenv("SNX_RT_DUMP_SPLIT");
        g_rtd.split = split && *split && *split != '0';
        const char* fill = getenv("SNX_RT_DUMP_FILL");
        g_rtd.fill = fill && *fill && *fill != '0';
        if (g_rtd.on) LOG("vulkan: the passes of frame %u go to %s as pictures (SNX_RT_DUMP)\n", g_rtd.at, g_rtd.dir);
    }
    int on = g_rtd.on && !g_rtd.done && g_vk_present == g_rtd.at;
    if (on && !g_rtd.index) {
        char path[1100];
        snprintf(path, sizeof path, "%s/index.txt", g_rtd.dir);
        g_rtd.index = fopen(path, "w");
    }
    return on;
}

static void tex_add(RtdTex* list, u32* n, const RtdTex* t);
int vk_rtdump_split(void) { return g_rtd.split && rtd_on(); }
int vk_rtdump_fill(const Surf* s) {
    if (!rtd_on() || !g_rtd.fill || !s || s->w < 1280 || s->f.depth || s->f.is_int) return 0;
    for (u32 i = 0; i < g_rtd.nfilled; i++)
        if (g_rtd.filled[i] == s) return 0;
    if (g_rtd.nfilled == 16) return 0;
    g_rtd.filled[g_rtd.nfilled++] = s;
    if (g_rtd.index)
        fprintf(g_rtd.index, "  pass %u fills %llx magenta\n", g_rtd.pass, (unsigned long long)s->gva);
    return 1;
}
void vk_rtdump_draw(u64 vs, u64 fs) {
    if (!rtd_on()) return;
    g_rtd.draws++;
    for (u32 i = 0; i < g_rtd.nnext; i++) tex_add(g_rtd.tex, &g_rtd.ntex, &g_rtd.next[i]);
    g_rtd.nnext = 0;
    for (u32 i = 0; i < g_rtd.nprog; i++)
        if (g_rtd.vs[i] == vs && g_rtd.fs[i] == fs) return;
    if (g_rtd.nprog == RTD_PROGS) return;
    g_rtd.vs[g_rtd.nprog] = vs;
    g_rtd.fs[g_rtd.nprog++] = fs;
}

static void tex_add(RtdTex* list, u32* n, const RtdTex* t) {
    for (u32 i = 0; i < *n; i++)
        if (list[i].found == t->found && list[i].bound == t->bound && list[i].stage == t->stage &&
            list[i].slot == t->slot)
            return;
    if (*n < RTD_TEXS) list[(*n)++] = *t;
}
static void rtd_take_sub(Surf* s, u32 slot, u32 layer, u32 level);
/* SNX_RT_DUMP_TEX=address[,address] or cube: those textures (cube: every cube texture), as the dumped frame first
   binds them, every layer (up to 24) of their first 4 levels. Addresses move from run to run */
static void tex_dump(const Surf* bound) {
    static u64 want[8], done[8];
    static int nwant = -1, cubes;
    static const Surf* cube_done[16];
    static u32 ncube;
    if (nwant < 0) {
        nwant = 0;
        const char* e = getenv("SNX_RT_DUMP_TEX");
        cubes = e && strstr(e, "cube") != NULL;
        if (cubes) e = NULL;
        while (e && *e && nwant < 8) {
            char* end;
            want[nwant] = strtoull(e, &end, 16);
            if (end == e) break;
            nwant++;
            e = *end == ',' ? end + 1 : end;
        }
    }
    int pick = 0;
    if (cubes && (bound->type == VK_IMAGE_VIEW_TYPE_CUBE || bound->type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)) {
        pick = 1;
        for (u32 i = 0; i < ncube; i++) pick &= cube_done[i] != bound;
        if (pick && ncube < 16) cube_done[ncube++] = bound;
        else pick = 0;
    }
    for (int i = 0; i < nwant && !pick; i++)
        if (!done[i] && want[i] == bound->gva) done[i] = pick = 1;
    if (pick) {
        if (g_rtd.index)
            fprintf(g_rtd.index, "  texture %llx: %ux%u, %u layers, %u levels, view type %d, format %u, took drawn %d\n",
                    (unsigned long long)bound->gva, bound->w, bound->h, bound->layers, bound->levels, (int)bound->type,
                    (u32)bound->f.vk, bound->took_drawn);
        for (u32 l = 0; l < bound->levels && l < 4; l++)
            for (u32 k = 0; k < bound->layers && k < 24; k++) rtd_take_sub((Surf*)bound, 100, k, l);
    }
}
void vk_rtdump_tex(int stage, int slot, const Surf* found, const Surf* bound) {
    if (!rtd_on()) return;
    if (bound) tex_dump(bound);
    RtdTex t = {found, bound, stage, slot, bound ? bound->crop_gen : 0, found ? found->wgen : 0,
                bound ? bound->took_drawn : 0};
    tex_add(g_rtd.next, &g_rtd.nnext, &t);
}

void vk_rtdump_event(const char* what, const Surf* s) {
    if (!rtd_on() || !g_rtd.index) return;
    fprintf(g_rtd.index, "  before pass %u: %llx %ux%u format %u %s\n", g_rtd.pass, (unsigned long long)s->gva, s->w,
            s->h, (u32)s->f.vk, what);
}

static int blittable(VkFormat f) {
    VkFormatProperties p = {0};
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, f, &p);
    return (p.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;
}

/* one color target as it is at the end of the pass */
static void rtd_take(Surf* s, u32 slot) {
    if (g_rtd.split && s && s->w < 1280) return; /* a picture a draw: the full-size targets alone, small */
    rtd_take_sub(s, slot, 0, 0);
}
static void rtd_take_sub(Surf* s, u32 slot, u32 layer, u32 level) {
    if (g_rtd.npic == RTD_MAX || !s || s->f.depth || s->f.is_int || !blittable(s->f.vk)) return;
    u32 lw = s->sw >> level ? s->sw >> level : 1, lh = s->sh >> level ? s->sh >> level : 1;
    RtdPic* p = &g_rtd.pic[g_rtd.npic];
    memset(p, 0, sizeof *p);
    u32 most = g_rtd.split ? 480 : 960;
    p->w = lw > most ? most : lw;
    p->h = (u32)((u64)lh * p->w / lw);
    if (!p->h) p->h = 1;
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                            .extent = {p->w, p->h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo aci = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (!VK_OK(vmaCreateImage(g_vk.vma, &ii, &aci, &p->img, &p->img_mem, NULL))) return;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = (u64)p->w * p->h * 8,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo bai = {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                            VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                   .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST};
    VmaAllocationInfo info;
    if (!VK_OK(vmaCreateBuffer(g_vk.vma, &bci, &bai, &p->buf, &p->buf_mem, &info))) {
        vmaDestroyImage(g_vk.vma, p->img, p->img_mem);
        return;
    }
    p->px = info.pMappedData;
    p->pass = g_rtd.pass;
    p->slot = slot;
    p->gva = s->gva;
    p->sw = s->w;
    p->sh = s->h;
    p->fmt = (u32)s->f.vk;
    p->layer = layer;
    p->level = level;
    VkCommandBuffer cb = vk_cmd();
    vk_surf_use(cb, s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
    vk_barrier(cb, p->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1},
                        .srcOffsets = {{0, 0, 0}, {(i32)lw, (i32)lh, 1}},
                        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .dstOffsets = {{0, 0, 0}, {(i32)p->w, (i32)p->h, 1}}};
    rec_CmdBlitImage(cb, s->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                     &blit, VK_FILTER_LINEAR);
    vk_barrier(cb, p->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {p->w, p->h, 1}};
    rec_CmdCopyImageToBuffer(cb, p->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p->buf, 1, &c);
    g_rtd.npic++;
}

void vk_rtdump_pass_end(Surf* const* rt, u32 n, const Surf* zt) {
    if (!rtd_on()) return;
    if (!g_rtd.index) {
        char path[1100];
        snprintf(path, sizeof path, "%s/index.txt", g_rtd.dir);
        g_rtd.index = fopen(path, "w");
    }
    if (g_rtd.index) {
        fprintf(g_rtd.index, "pass %u: %u draws", g_rtd.pass, g_rtd.draws);
        for (u32 i = 0; i < n; i++)
            if (rt[i])
                fprintf(g_rtd.index, ", color %u %llx %ux%u format %u", i, (unsigned long long)rt[i]->gva, rt[i]->w,
                        rt[i]->h, (u32)rt[i]->f.vk);
        if (zt)
            fprintf(g_rtd.index, ", depth %llx %ux%u format %u", (unsigned long long)zt->gva, zt->w, zt->h,
                    (u32)zt->f.vk);
        for (u32 i = 0; i < g_rtd.nprog; i++)
            fprintf(g_rtd.index, "%s vs %016llx fs %016llx", i ? "," : "; programs", (unsigned long long)g_rtd.vs[i],
                    (unsigned long long)g_rtd.fs[i]);
        fprintf(g_rtd.index, "\n");
        /* what the pass's draws sampled: a texture's address, size and format, whether it is a render target itself
           (rt), a copy of one (copy, with the target's generation it was taken at), or a texture whose texels
           were copied from targets (took), and the generations that say whether it is current */
        for (u32 i = 0; i < g_rtd.ntex; i++) {
            const Surf *f = g_rtd.tex[i].found, *b = g_rtd.tex[i].bound;
            fprintf(g_rtd.index, "  stage %d slot %d: ", g_rtd.tex[i].stage, g_rtd.tex[i].slot);
            if (!f) {
                fprintf(g_rtd.index, "no texture\n");
                continue;
            }
            fprintf(g_rtd.index, "%llx %ux%u format %u%s wgen %u", (unsigned long long)f->gva, f->w, f->h,
                    (u32)f->f.vk, f->is_rt ? " rt" : "", g_rtd.tex[i].found_wgen);
            if (!b)
                fprintf(g_rtd.index, ", bound the null texture");
            else if (b != f)
                fprintf(g_rtd.index, ", bound a copy %ux%u taken at wgen %u", b->w, b->h, g_rtd.tex[i].crop_gen);
            if (b && g_rtd.tex[i].took_drawn) fprintf(g_rtd.index, ", took drawn texels");
            fprintf(g_rtd.index, "\n");
        }
    }
    for (u32 i = 0; i < n; i++) rtd_take(rt[i], i);
    g_rtd.pass++;
    g_rtd.draws = 0;
    g_rtd.nprog = 0;
    g_rtd.ntex = 0;
}

static float half_float(u16 h) {
    u32 e = (h >> 10) & 31, m = h & 1023, sign = (u32)(h >> 15) << 31, bits;
    if (!e) {
        float v = (float)m / 16777216.0f; /* subnormal: m * 2^-24 */
        return sign ? -v : v;
    }
    bits = sign | (e == 31 ? 0x7F800000u | (m << 13) : ((e + 112) << 23) | (m << 13));
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
static int cmp_float(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y;
}
static int is_unorm(u32 f) {
    return (f >= VK_FORMAT_R8_UNORM && f <= VK_FORMAT_A2B10G10R10_SINT_PACK32 && f != VK_FORMAT_B10G11R11_UFLOAT_PACK32) &&
           f != VK_FORMAT_R16G16B16A16_SFLOAT;
}
/* the picture as PPM: HDR scaled and gamma encoded, UNORM as it is */
static int rtd_write(const RtdPic* p, const char* path) {
    u64 n = (u64)p->w * p->h;
    float* v = malloc(n * 3 * sizeof *v);
    float* sorted = malloc(n * sizeof *sorted);
    if (!v || !sorted) {
        free(v);
        free(sorted);
        return 0;
    }
    float lo = 1e30f, hi = -1e30f;
    u32 nans = 0;
    for (u64 k = 0; k < n; k++) {
        float m = 0;
        for (int c = 0; c < 3; c++) {
            u16 h;
            memcpy(&h, p->px + k * 8 + (u64)c * 2, 2);
            float x = half_float(h);
            if (x != x) {
                nans++;
                x = 0;
            }
            v[k * 3 + (u64)c] = x;
            if (x < lo) lo = x;
            if (x > hi) hi = x;
            if (x > m) m = x;
        }
        sorted[k] = m;
    }
    int hdr = !is_unorm(p->fmt);
    float scale = 1.0f;
    if (hdr) {
        qsort(sorted, n, sizeof *sorted, cmp_float);
        float top = sorted[n - 1 - n / 200];
        scale = top > 0 ? 1.0f / top : 1.0f;
    }
    free(sorted);
    if (g_rtd.index)
        fprintf(g_rtd.index, "picture %u_%u: values %g to %g, %u NaN, %s %g\n", p->pass, p->slot, (double)lo,
                (double)hi, nans, hdr ? "scaled by" : "as they are,", (double)scale);
    FILE* f = fopen(path, "wb");
    int ok = 0;
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", p->w, p->h);
        for (u64 k = 0; k < n * 3; k++) {
            float x = v[k] * scale;
            if (x < 0) x = 0;
            if (x > 1) x = 1;
            if (hdr) x = powf(x, 1.0f / 2.2f);
            u8 b = (u8)(x * 255.0f + 0.5f);
            fwrite(&b, 1, 1, f);
        }
        ok = fclose(f) == 0;
    }
    free(v);
    return ok;
}

/* at a present: once the dumped frame is presented, its pictures are written */
void vk_rtdump_present(void) {
    if (g_rtd.on <= 0 || g_rtd.done || g_vk_present <= g_rtd.at) return;
    g_rtd.done = 1;
    vk_finish();
    u32 written = 0;
    for (u32 i = 0; i < g_rtd.npic; i++) {
        RtdPic* p = &g_rtd.pic[i];
        vmaInvalidateAllocation(g_vk.vma, p->buf_mem, 0, VK_WHOLE_SIZE);
        char path[1200];
        if (p->slot >= 100)
            snprintf(path, sizeof path, "%s/tex_%llx_layer%02u_level%u_%ux%u_f%u.ppm", g_rtd.dir,
                     (unsigned long long)p->gva, p->layer, p->level, p->sw, p->sh, p->fmt);
        else
        snprintf(path, sizeof path, "%s/%03u_%u_%llx_%ux%u_f%u.ppm", g_rtd.dir, p->pass, p->slot,
                 (unsigned long long)p->gva, p->sw, p->sh, p->fmt);
        written += rtd_write(p, path);
        vmaDestroyBuffer(g_vk.vma, p->buf, p->buf_mem);
        vmaDestroyImage(g_vk.vma, p->img, p->img_mem);
    }
    if (g_rtd.index) fclose(g_rtd.index);
    g_rtd.index = NULL;
    LOG("vulkan: %u pictures of %u passes of frame %u written to %s\n", written, g_rtd.pass, g_rtd.at, g_rtd.dir);
}
