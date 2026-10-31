/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* 11.3.0: gsys's resolution update publishes the primary scene's viewport scale before render-buffer preparation.
   Its native parameter bindings control optional post-processing. Irradiance CPU reads land GPU results.
   GPU data textures and simulation stay native. */
#include "nx/runtime/inkypaint_graphics.h"
#include "nx/runtime/inkypaint_bounds.h"
#include "nx/runtime/inkypaint_graphics_policy.h"
#include "nx/runtime/inkypaint_heapscale.h"
#include "nx/runtime/driver/inkypaint.h"
#include "nx/runtime/guest_jit.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/hle/hle.h"
#include "nx/memory/mem.h"
#include "nx/gpu/renderer.h"
#include <math.h>
#include <string.h>

typedef struct {
    u64 address;
    GuestFn original;
    const char* name;
} QualityHook;
static struct {
    QualityHook hook[8];
    GuestFn irradiance_original;
    u64 gpu_sum, gpu_max;
    u32 gpu_samples;
    int enabled;
    float scale;
    InkypaintResolutionPolicy policy;
    char pad[64]; /* GPU timing writes scale/policy, game render preparation writes logged */
    u32 logged;
} g_graphics;

int inkypaint_graphics_engine_scale(void) { return g_graphics.enabled; }
void inkypaint_graphics_gpu_time(uint64_t ns) {
    if (!g_graphics.enabled || ns < 100000 || ns > 500000000) return;
    g_graphics.gpu_sum += ns;
    if (ns > g_graphics.gpu_max) g_graphics.gpu_max = ns;
    if (++g_graphics.gpu_samples == 300) {
        LOG("graphics: completed GPU work %.2f ms average, %.2f ms peak over 300 samples\n",
            g_graphics.gpu_sum / 300e6, g_graphics.gpu_max / 1e6);
        g_graphics.gpu_sum = g_graphics.gpu_max = g_graphics.gpu_samples = 0;
    }
    float scale;
    __atomic_load(&g_graphics.scale, &scale, __ATOMIC_RELAXED);
    if (!g_settings.dynamic_resolution) return;
    float next = inkypaint_resolution_sample(&g_graphics.policy, ns, scale, g_settings.minimum_render_scale,
                                             inkypaint_heapscale_clamp(g_settings.resolution_scale),
                                             g_settings.dynamic_resolution_target);
    if (next != scale) {
        __atomic_store(&g_graphics.scale, &next, __ATOMIC_RELAXED);
        LOG("graphics: dynamic render scale %.3fx (GPU %.2f ms, target %d FPS)\n", next,
            g_graphics.policy.filtered_ns / 1e6, g_settings.dynamic_resolution_target);
    }
}
static void put_float(u64 address, float value) {
    u32 bits;
    memcpy(&bits, &value, 4);
    gw32(address, bits);
}
static float get_float(u64 address) {
    u32 bits = gr32(address);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}
static void quality(CPU* c, u64 entry) {
    unsigned i;
    for (i = 0; i < 8 && g_graphics.hook[i].address != entry; i++) {}
    if (i == 8) return;
    if (!i) {
        g_graphics.hook[0].original(c, entry);
        float scale;
        __atomic_load(&g_graphics.scale, &scale, __ATOMIC_RELAXED);
        if (scale == 1 && g_settings.bloom && g_settings.depth_of_field && g_settings.shadows &&
            g_settings.vignette && g_settings.fog) return;
        /* The same live singleton chain read by the original at 0x936acf0. No scene object is retained. */
        u64 slot = gr64(0xdf25258), system = slot ? gr64(slot) : 0;
        u64 holder = system ? gr64(system + 0x48) : 0, scene = holder ? gr64(holder) : 0;
        u64 buffers = scene ? gr64(scene + 0x3088) : 0;
        u64 context = buffers && gr32(buffers + 8) ? gr64(buffers + 0x10) : 0;
        if (!context) return;
        float x = get_float(context + 0x22b0), y = get_float(context + 0x22b4);
        if (scale != 1 && isfinite(x) && isfinite(y) && x > 0 && y > 0 && x <= 1 && y <= 1) {
            put_float(context + 0x22b0, x * scale);
            put_float(context + 0x22b4, y * scale);
            if (!(__atomic_fetch_or(&g_graphics.logged, 1, __ATOMIC_RELAXED) & 1))
                LOG("graphics: primary scene viewport scaled %.3fx (native %.3f x %.3f)\n", scale, x, y);
        }
        /* Defaults often bypass the parameter manager's setters. Apply disabled effects here too, through those
           very bindings, once the original has resolved the live scene. Never retain its pointers across frames. */
        for (unsigned effect = 1; effect < 8; effect++) {
            int off = effect == 1 ? !g_settings.bloom : effect == 2 ? !g_settings.depth_of_field :
                      effect < 5 ? !g_settings.shadows : effect == 5 ? !g_settings.vignette : !g_settings.fog;
            if (!off) continue;
            CPU call = *c;
            call.x[31] -= 16;
            call.x[1] = scene;
            call.x[2] = call.x[31];
            gw32(call.x[2], 0);
            g_graphics.hook[effect].original(&call, g_graphics.hook[effect].address);
            if (!(__atomic_fetch_or(&g_graphics.logged, 1u << effect, __ATOMIC_RELAXED) & (1u << effect)))
                LOG("graphics: engine %s disabled\n", g_graphics.hook[effect].name);
        }
        return;
    }
    int off = i == 1 ? !__atomic_load_n(&g_settings.bloom, __ATOMIC_RELAXED) :
              i == 2 ? !__atomic_load_n(&g_settings.depth_of_field, __ATOMIC_RELAXED) :
              i < 5 ? !__atomic_load_n(&g_settings.shadows, __ATOMIC_RELAXED) :
              i == 5 ? !__atomic_load_n(&g_settings.vignette, __ATOMIC_RELAXED) :
                       !__atomic_load_n(&g_settings.fog, __ATOMIC_RELAXED);
    if (!off) { g_graphics.hook[i].original(c, entry); return; }
    u64 sp = c->x[31];
    c->x[31] -= 16;
    c->x[2] = c->x[31];
    gw32(c->x[2], 0);
    if (!(__atomic_fetch_or(&g_graphics.logged, 1u << i, __ATOMIC_RELAXED) & (1u << i)))
        LOG("graphics: engine %s disabled\n", g_graphics.hook[i].name);
    g_graphics.hook[i].original(c, entry);
    c->x[31] = sp;
}
/* IrradianceSHTexture::read reads one texel directly from the CPU mapping after
   the game's fence. Render targets live in host images, so land their latest
   pixels before the original routine decodes FP16/FP32 and packs the light UBO.
   This runs when the game updates irradiance, not for every material draw. */
static void irradiance_read(CPU* c, u64 entry) {
    u32 index = (u32)c->x[2];
    u64 texture = c->x[0] + 8 + (index < 7 ? (u64)index * 0xb0 : 0);
    u32 format = gr32(texture + 0xa8) & 0xffff;
    if ((format == 0x2b || format == 0x2e) && g_renderer && g_renderer->rt_cpu_read) {
        u64 pool = gr64(texture + 0x10);
        u64 impl = pool ? gr64(pool + 0x18) : 0;
        u64 base = impl ? gr64(impl + 0x100) : 0;
        if (base) g_renderer->rt_cpu_read(base + gr32(texture), format == 0x2b ? 8 : 16);
    }
    g_graphics.irradiance_original(c, entry);
}
/* MatrixClipper's indirect callback transforms a model AABB. Read the matrix and
   bounds before writing: the caller may overlap input/output. The original builds
   eight stack corners and indexes them through six cached selectors; a scene
   transition with invalid selectors can address far outside that stack. */
static void bounds_transform(CPU* c, u64 entry) {
    (void)entry;
    float matrix[12], box[6], out[6];
    int32_t cached[6];
    for (unsigned i = 0; i < 12; i++) matrix[i] = get_float(c->x[0] + 0x150 + i * 4);
    for (unsigned i = 0; i < 6; i++) {
        box[i] = get_float(c->x[2] + i * 4);
        cached[i] = (int32_t)gr32(c->x[0] + 0x130 + (i < 3 ? i * 4 : 0x10 + (i - 3) * 4));
    }
    unsigned repaired = inkypaint_bounds_transform(out, box, matrix, cached);
    if (repaired)
        LOG_LIMIT(8, "graphics: rebuilt %u invalid matrix-clipper corner selectors at %llx\n", repaired,
                  (unsigned long long)c->x[0]);
    for (unsigned i = 0; i < 6; i++) put_float(c->x[1] + i * 4, out[i]);
    c->pc = c->x[30];
}
static void bounds_init(void) {
    const u64 address = 0xc77b184;
    if (address < g_code_lo || address + 16 >= g_code_hi || gr32(address) != 0xd10183ff ||
        gr32(address + 4) != 0xb981300a || gr32(address + 8) != 0x52800188 ||
        gr32(address + 12) != 0x910003e9) {
        WARN("graphics: matrix-clipper hook does not match this executable\n");
        return;
    }
    jit_code_changed(address, 4);
    if (!jit_wait(address)) return;
    g_disp[(address - g_code_lo) / 4] = bounds_transform;
    LOG("graphics: native matrix-clipper bounds transform ready\n");
}
static void irradiance_init(void) {
    const u64 address = 0x926bc94;
    if (address < g_code_lo || address + 16 >= g_code_hi || gr32(address) != 0x91002008 ||
        gr32(address + 4) != 0x52801609 || gr32(address + 8) != 0x9ba92049 || gr32(address + 12) != 0x71001c5f) {
        WARN("graphics: irradiance read hook does not match this executable\n");
        return;
    }
    jit_code_changed(address, 4);
    g_graphics.irradiance_original = jit_wait(address);
    if (!g_graphics.irradiance_original) return;
    g_disp[(address - g_code_lo) / 4] = irradiance_read;
    LOG("graphics: CPU irradiance readback hook ready\n");
}
void inkypaint_graphics_init(void) {
    if (inkypaint_any_title() || g_hle.kernel.title_id != INKYPAINT_TITLE_ID) return;
    bounds_init();
    irradiance_init();
    static const struct { u64 address; u32 first, binding; const char* name; } sites[] = {
        {0x936ab30, 0xd10103ff, 0xa9027bfd, "resolution update"},
        {0x9286210, 0xf9556028, 0xb4000068, "bloom"},
        {0x9286d7c, 0xf9556828, 0xb4000088, "depth of field"},
        {0x9285d08, 0xf940e028, 0xb4000068, "depth shadows"},
        {0x9285d30, 0xf940e028, 0xb4000068, "shadow post-processing"},
        {0x9286fe0, 0xf9556828, 0xb40000a8, "vignette"},
        {0x9285e80, 0x90026ea8, 0x912c4108, "depth fog"},
        {0x9286100, 0xf0026e88, 0x912c4108, "height fog"}
    };
    for (unsigned i = 0; i < 8; i++)
        if (sites[i].address < g_code_lo || sites[i].address + 0x20 >= g_code_hi ||
            gr32(sites[i].address) != sites[i].first || gr32(sites[i].address + 4) != sites[i].binding) {
            WARN("graphics: quality hook %s does not match this executable\n", sites[i].name);
            return;
        }
    for (unsigned i = 0; i < 8; i++) {
        g_graphics.hook[i].address = sites[i].address;
        g_graphics.hook[i].name = sites[i].name;
        jit_code_changed(sites[i].address, 4);
        g_graphics.hook[i].original = jit_wait(sites[i].address);
        if (!g_graphics.hook[i].original) return;
    }
    g_graphics.scale = inkypaint_heapscale_clamp(g_settings.resolution_scale);
    for (unsigned i = 0; i < 8; i++) g_disp[(sites[i].address - g_code_lo) / 4] = quality;
    g_graphics.enabled = 1;
    LOG("graphics: engine quality hooks ready, render scale %.3fx%s\n", g_graphics.scale,
        g_settings.dynamic_resolution ? " (dynamic)" : "");
}
