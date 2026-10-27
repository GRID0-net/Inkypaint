/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Splatoon 3 11.3.0: adapt agl's camera matrix/frustum copy and the final UI draws, leaving shared projection
   utilities and CPU UI coordinates alone. UI attachment positions (including the selector squid) are projected
   before being drawn through a UI camera; adapting both steps would apply the correction twice. */
#include "nx/runtime/inkypaint_display.h"
#include "nx/runtime/driver/inkypaint.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/guest_jit.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/backend.h"
#include "nx/runtime/platform/log.h"
#include "nx/hle/hle.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/memory/mem.h"
#include <math.h>
#include <string.h>

#define BASE_ASPECT (16.f / 9.f)
#define CONVERT     0xc5a46d4ull
#define CAMERA      0xc64978cull
static GuestFn convert_original, camera_original;
static int enabled;

int inkypaint_display_fill(void) { return enabled; }

static float read_float(u64 a) {
    u32 bits = gr32(a);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static void write_float(u64 a, float f) {
    u32 bits;
    memcpy(&bits, &f, sizeof bits);
    gw32(a, bits);
}
static float argument(CPU* c, int reg) {
    u32 bits = SNX_VS(c->v[reg], 0);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static void set_argument(CPU* c, int reg, float f) {
    u32 bits;
    memcpy(&bits, &f, sizeof bits);
    SNX_VSET_S(c->v[reg], 0, bits);
}
float inkypaint_display_aspect(void) {
    int left, top, w, h;
    backend_window_safe(&left, &top, &w, &h);
    float aspect;
    __atomic_load(&g_settings.aspect_ratio_override, &aspect, __ATOMIC_RELAXED);
    if (aspect <= 0) aspect = w > 0 && h > 0 ? (float)w / h : BASE_ASPECT;
    return fminf(4, fmaxf(.25f, aspect));
}
static void factors(float* x, float* y) {
    float ratio = inkypaint_display_aspect() / BASE_ASPECT;
    *x = ratio > 1 ? 1 / ratio : 1;
    *y = ratio < 1 ? ratio : 1;
}
static void correct(u64 matrix) {
    float x, y;
    factors(&x, &y);
    for (int i = 0; i < 4; i++) {
        write_float(matrix + i * 4, read_float(matrix + i * 4) * x);
        write_float(matrix + 16 + i * 4, read_float(matrix + 16 + i * 4) * y);
    }
}
int inkypaint_display_ui_scale(uint64_t vertex_shader, float* x, float* y) {
    if (!enabled) return 0;
    /* nn::ui2d's pane, textured pane, text and masking vertex programs in this build. Apply the safe area to
       their final screen draws, leaving intermediate UI textures and CPU attachment positions in logical space. */
    switch (vertex_shader) {
        case 0x11635f97e469d30full:
        case 0x8f1e51aa858551bdull:
        case 0xd6138ec5d79e1854ull:
        case 0xd0dcc4f07e498ce4ull:
        case 0x9a4814444d3a39dfull:
        case 0x07671a65ff56d239ull: break;
        default: return 0;
    }
    factors(x, y);
    return 1;
}
static int adaptive_projection(u64 object) {
    /* The world camera uses a near plane below one. The temporary UI perspective camera uses near=1 and also
       renders intermediate textures: adapting it would move its contents relative to their masks. */
    return gr64(object) == 0xded7918 && read_float(object + 0x98) < 1 &&
           fabsf(read_float(object + 0xb0) - BASE_ASPECT) < .001f;
}
static void convert(CPU* c, u64 entry) {
    u64 object = c->x[0];
    int adaptive = adaptive_projection(object);
    convert_original(c, entry);
    if (!adaptive) return;
    /* The caller clears GPU-dirty (byte 9) afterwards. Leave CPU-dirty (byte 8) set, so a cached world projection
       follows a resize on its next use, without keeping pointers to objects the game can free. */
    *(u8*)GP(object + 8) = 1;
}
static void camera(CPU* c, u64 entry) {
    float aspect = argument(c, 3), fovy = argument(c, 2);
    if (fovy <= 0 || fabsf(aspect - BASE_ASPECT) >= .001f) {
        camera_original(c, entry);
        return;
    }
    if (inkypaint_display_aspect() == BASE_ASPECT) {
        camera_original(c, entry);
        return;
    }
    /* This function copies the CPU projection and builds its inverse and culling frustum. Use a guest-stack copy
       so CPU UI projection queries keep their logical coordinates and other threads never see a temporary edit. */
    u64 sp = c->x[31];
    c->x[31] -= 64;
    memcpy(GP(c->x[31]), GP(c->x[1]), 64);
    c->x[1] = c->x[31];
    correct(c->x[1]);
    float x, y;
    factors(&x, &y);
    set_argument(c, 3, inkypaint_display_aspect());
    if (y < 1) set_argument(c, 2, 2 * atanf(tanf(fovy * .5f) / y));
    camera_original(c, entry);
    c->x[31] = sp;
}
void inkypaint_display_init(void) {
    if (inkypaint_any_title() || !g_settings.adapt_to_display || g_hle.kernel.title_id != INKYPAINT_TITLE_ID) return;
    if (CONVERT < g_code_lo || CAMERA >= g_code_hi || gr32(CONVERT) != 0x3dc00042 || gr32(CAMERA) != 0xd10203ff) {
        WARN("display: projection hooks do not match this executable\n");
        return;
    }
    /* Retire the AOT bodies too: direct calls and inlined copies must enter these wrappers. The originals come
       from the JIT, which has no AOT stale guard to send them back into the wrapper. Before any guest thread runs. */
    jit_code_changed(CONVERT, 4);
    jit_code_changed(CAMERA, 4);
    convert_original = jit_wait(CONVERT);
    camera_original = jit_wait(CAMERA);
    if (!convert_original || !camera_original) {
        WARN("display: could not compile projection hooks\n");
        return;
    }
    g_disp[(CONVERT - g_code_lo) / 4] = convert;
    g_disp[(CAMERA - g_code_lo) / 4] = camera;
    enabled = 1;
    LOG("display: adapting world projections and 16:9 UI to the window\n");
}
