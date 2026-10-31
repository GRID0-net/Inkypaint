/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_FRAME_POLICY_H
#define INKYPAINT_FRAME_POLICY_H
#include <stdint.h>
#include <string.h>
/* 60 is an intentional cap, not an unset value. Unlimited/invalid settings
   use 120 for smooth motion; the fixed simulation clock remains 60 either way. */
static inline int inkypaint_frame_render_cap(int configured_hz) {
    return configured_hz >= 60 && configured_hz <= 240 ? configured_hz : 120;
}
/* A render-only frame must not consume controller edges or advance UI state.
   Unknown callbacks are simulation, including delegates in the post-calc graph.
   Only identified graphics maintenance may run without a fixed simulation step. */
static inline int inkypaint_frame_simulation_job(uint32_t role, const char* name, uint64_t executor) {
    (void)role;
    /* GSYS render tasks and GameGfx's graphics delegates can have no name. */
    if (executor == 0xc7b56f0ull || executor == 0x9366800ull) return 0;
    if (!strcmp(name, "Graphics") || !strcmp(name, "Resource") || !strcmp(name, "GameGfx") ||
        !strcmp(name, "GameGfx:PostActorCalc") || !strcmp(name, "GameGfx:UpdateDynamicLight") ||
        !strcmp(name, "EntryJobsBgGraphics")) return 0;
    /* GameCamera solves the camera pose and effects the renderer's temporal reprojection reads. Skipped on a render-only
       frame, everything that moves (the reticle, the player, the ink) was drawn twice, a bright copy and a faint one.
       Run on every frame it ends that. The player's hair then trails the head while moving (not understood yet). */
    if (!strcmp(name, "GameCamera")) return 0;
    /* The particle effects (steam, ink, footsteps): one frame in which the player moved near a steam object showed a blue
       wash over the whole 3D scene (the HUD untouched), with fog-like blobs. Their jobs skipped on a render-only frame, a
       particle spawned in the step was drawn in the next frame before its data existed. Run on every frame, the flash is
       gone (the player's own test, and a capture of 13 of them before it). */
    if (!strcmp(name, "Effect") || !strcmp(name, "GameVfx") || !strcmp(name, "XLink")) return 0;
    return 1;
}
/* Only a simulation sample may replace the authoritative interpolation endpoint.
   Render-only work may consume a pose already interpolated on the previous frame. */
static inline int inkypaint_frame_capture(unsigned steps, int history_present) {
    return steps != 0 || !history_present;
}
#endif
