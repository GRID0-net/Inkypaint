/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* SNX_RT_DUMP=dir: one frame's rendering passes as pictures, to find the pass where a frame goes wrong. At the end
   of each pass of frame SNX_RT_DUMP_AT (presents counted from the start, 1500 by default), each color target is
   blitted to RGBA8 at up to 960 pixels wide and copied into a buffer of its own; at the next present the frame is
   waited for and every picture written as dir/<pass>_<slot>_<address>_<w>x<h>_f<format>.ppm, with index.txt naming
   each pass's targets, draws and programs. A debugging tool: the pictures are the game's frame and stay local */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/vk/vk.h"
#include "nx/runtime/platform/log.h"

#define RTD_MAX   192 /* pictures a frame */
#define RTD_PROGS 8   /* programs listed for a pass */

typedef struct {
    VkImage img;
    VmaAllocation img_mem;
    VkBuffer buf;
    VmaAllocation buf_mem;
    u8* px;
    u32 w, h, pass, slot;
    u64 gva;
    u32 sw, sh, fmt;
} RtdPic;

static struct {
    int on; /* -1 not decided, 0 off, 1 on */
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
    FILE* index;
} g_rtd = {.on = -1};

static int rtd_on(void) {
    if (g_rtd.on < 0) {
        g_rtd.dir = getenv("SNX_RT_DUMP");
        const char* at = getenv("SNX_RT_DUMP_AT");
        g_rtd.at = at && *at ? (u32)strtoul(at, NULL, 10) : 1500;
        g_rtd.on = g_rtd.dir && *g_rtd.dir;
        if (g_rtd.on) LOG("vulkan: the passes of frame %u go to %s as pictures (SNX_RT_DUMP)\n", g_rtd.at, g_rtd.dir);
    }
    return g_rtd.on && !g_rtd.done && g_vk_present == g_rtd.at;
}

void vk_rtdump_draw(u64 vs, u64 fs) {
    if (!rtd_on()) return;
    g_rtd.draws++;
    for (u32 i = 0; i < g_rtd.nprog; i++)
        if (g_rtd.vs[i] == vs && g_rtd.fs[i] == fs) return;
    if (g_rtd.nprog == RTD_PROGS) return;
    g_rtd.vs[g_rtd.nprog] = vs;
    g_rtd.fs[g_rtd.nprog++] = fs;
}

static int blittable(VkFormat f) {
    VkFormatProperties p = {0};
    vkGetPhysicalDeviceFormatProperties(g_vk.phys, f, &p);
    return (p.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;
}

/* one color target as it is at the end of the pass */
static void rtd_take(Surf* s, u32 slot) {
    if (g_rtd.npic == RTD_MAX || !s || s->f.depth || s->f.is_int || !blittable(s->f.vk)) return;
    RtdPic* p = &g_rtd.pic[g_rtd.npic];
    memset(p, 0, sizeof *p);
    p->w = s->sw > 960 ? 960 : s->sw;
    p->h = (u32)((u64)s->sh * p->w / (s->sw ? s->sw : 1));
    if (!p->h) p->h = 1;
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = VK_FORMAT_R8G8B8A8_UNORM,
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
                              .size = (u64)p->w * p->h * 4,
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
    VkCommandBuffer cb = vk_cmd();
    vk_surf_use(cb, s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
    vk_barrier(cb, p->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                        .srcOffsets = {{0, 0, 0}, {(i32)s->sw, (i32)s->sh, 1}},
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
    }
    for (u32 i = 0; i < n; i++) rtd_take(rt[i], i);
    g_rtd.pass++;
    g_rtd.draws = 0;
    g_rtd.nprog = 0;
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
        snprintf(path, sizeof path, "%s/%03u_%u_%llx_%ux%u_f%u.ppm", g_rtd.dir, p->pass, p->slot,
                 (unsigned long long)p->gva, p->sw, p->sh, p->fmt);
        FILE* f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", p->w, p->h);
            for (u64 k = 0; k < (u64)p->w * p->h; k++) fwrite(p->px + k * 4, 1, 3, f);
            written += fclose(f) == 0;
        }
        vmaDestroyBuffer(g_vk.vma, p->buf, p->buf_mem);
        vmaDestroyImage(g_vk.vma, p->img, p->img_mem);
    }
    if (g_rtd.index) fclose(g_rtd.index);
    g_rtd.index = NULL;
    LOG("vulkan: %u pictures of %u passes of frame %u written to %s\n", written, g_rtd.pass, g_rtd.at, g_rtd.dir);
}
