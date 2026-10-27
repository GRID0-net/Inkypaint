/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Exact-build task scheduling and render-camera interpolation experiment. Off by default. */
#include "nx/runtime/inkypaint_framerate.h"
#include "nx/runtime/inkypaint_frame_clock.h"
#include "nx/runtime/inkypaint_camera_math.h"
#include "nx/runtime/driver/inkypaint.h"
#include "nx/runtime/guest_jit.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/hle/hle.h"
#include "nx/memory/mem.h"

#define BUILD_GRAPH 0xcad8520ull
#define MODULE_CALC 0xcda80d0ull
#define FRAME 0xc5316d4ull
#define CALC 0x925b1b0ull
#define CAMERA_VIEW 0xc64c570ull
#define GAME_GFX_CALC 0x9364490ull
/* The actor manager's base-proc scheduling: (manager, frame info, phase mask w2, ...). The Actor module's per-frame
   job calls it with every phase (0xf). The game's own split mode (actor module byte +0x133 bit 0) calls phase 0 alone
   once per simulation step (0xccddbb0, between catch-up steps) and phase 1 alone once per frame (0xccdc740) */
#define ACTOR_SCHEDULE 0xcc87220ull
#define ACTOR_MODULE_VTABLE 0xdf10cb0ull
/* gsys's model, its virtual at +0x2b8: (model, const sead::Matrix34f* rotation and translation, row major, const
   sead::Vector3f* scale). It scales the rotation, stores the result as the g3d model's base matrix (four columns)
   and calculates the skeleton's world matrices from it at once (nn::g3d's CalculateWorld, 0x8ae4e70). The actors
   call it on every frame, extra ones included (2026-10-26 call census: 72 calls a frame, 61 skeletons, in step
   and extra frames alike), with the transform of their last step */
#define MODEL_SET_MTX 0xc6a350cull
#define MODEL_SLOTS 4096u
/* gsys's animation advance: (the model's animations, the model, s0 the frames to advance). The model's per-frame
   calculation passes its frame delta, one 60 Hz step a frame since the 30 FPS alternation is cleared; the binds
   pass 0. Every frame drawn advances the skeletal, material and visibility animations by it, extra frames too:
   at 100 frames a second they played 100/60 times too fast (2026-10-26, the shoal) */
#define ANIM_ADVANCE 0xc68f330ull
#define JOB_SLOTS 2048
static const u64 executors[] = {
    0xcda24e0, 0x9367870, 0xc7b56f0, 0xcda23a0, 0xcce1080, 0xcce119c, 0xccdd6d0,
    0xccdd0a0, 0xccdd7a0, 0xce02050, 0x9366800, 0x95fb100, 0x9600cac, 0xcda2440
};
typedef struct {
    u64 object, calls, skips;
    int simulation, run;
    InkypaintFrameSample sample;
    u64 frame;
    char name[65];
} FrameJob;
typedef struct {
    u64 object, projection, frame, tick;
    u32 index;
    u64 projection_hash;
    float previous[12], current[12];
    int history;
} FrameCamera;
typedef struct {
    u64 model, vtable, tick, frame;
    float previous[12], current[12], previous_scale[3], current_scale[3];
    int history;
} FrameModel;
static __thread struct {
    InkypaintFrameSample sample;
    u64 owner, frame;
} t_frame;
static struct {
    GuestFn build_graph, module_calc, frame, calc, camera_view, game_gfx_calc, actor_schedule, model_set_mtx, anim_advance;
    GuestFn execute[sizeof executors / sizeof *executors];
    PMutex lock;
    FrameJob jobs[JOB_SLOTS];
    InkypaintFrameClock clock;
    InkypaintFrameSample sample;
    FrameCamera cameras[128];
    u64 frame_serial, camera_calls, camera_blends, camera_changes;
    u64 report_ns;
    int target, trace, simulating, camera_enabled;
    /* bisection switches for the plaza's horizontal line (environment, see inkypaint_framerate_init) */
    int keep_alternation, no_gate, no_gfx_hold;
    char allow[1024]; /* INKYPAINT_GATE_ALLOW: ",name,name," of gated jobs that run on every frame anyway ("*": all) */
    char extra[1024]; /* INKYPAINT_GATE_EXTRA: ",name,name," of jobs gated though their graph's aren't */
    unsigned max_steps; /* INKYPAINT_MAX_STEPS: a frame's simulation steps at most, 0 for the clock's (up to 6) */
    unsigned logic_hz;  /* INKYPAINT_LOGIC_HZ: the game's clock slowed to this many steps a second (test only) */
    int actor_render;   /* INKYPAINT_ACTOR_RENDER=1: the Actor module job runs on extra frames (wrong: actor logic) */
    u64 actor_phase_frames, actor_split_frames;
    int model_enabled; /* models' transforms interpolated between steps (INKYPAINT_MODEL_INTERP=0: off) */
    int model_replay;  /* extra frames set last frame's models again (INKYPAINT_MODEL_REPLAY=0: off) */
    u64 model_replays;
    u64 model_calls, model_blends, model_jumps;
    /* each animated model's game time at its last advance (tick and part of one): what it advances by next */
    struct { u64 anim, tick; float alpha; } anims[MODEL_SLOTS];
    int anim_enabled; /* INKYPAINT_ANIM_ADVANCE=0: animations advance a step every frame, as the game asks */
    u64 anim_calls;
    double anim_scale_sum;
    FrameModel models[MODEL_SLOTS];
    u64 module;
    struct { u64 address, hash; } seen[128];
    u32 nseen;
} g_framerate = {.lock = PMUTEX_INIT};

int inkypaint_framerate_target(void) { return g_framerate.target; }
int inkypaint_framerate_phase(void) {
    if (t_frame.owner) return t_frame.sample.steps ? 1 : 2;
    if (!g_framerate.target || g_framerate.no_gate) return 0;
    return __atomic_load_n(&g_framerate.simulating, __ATOMIC_RELAXED) ? 3 : 4;
}
static FrameJob* find_job(u64 object) {
    u32 i = (u32)((object * 0x9e3779b97f4a7c15ull) >> 32) & (JOB_SLOTS - 1);
    for (u32 n = 0; n < JOB_SLOTS; n++, i = (i + 1) & (JOB_SLOTS - 1))
        if (!g_framerate.jobs[i].object || g_framerate.jobs[i].object == object) return &g_framerate.jobs[i];
    return NULL;
}
static void execute_job(CPU* c, u64 entry) {
    int run = 1;
    InkypaintFrameSample sample = {0};
    u64 owner = 0, frame = 0;
    pm_lock(&g_framerate.lock);
    FrameJob* job = find_job(c->x[0]);
    if (job && job->object) {
        run = !g_framerate.target || job->run;
        sample = job->sample;
        owner = job->object;
        frame = job->frame;
        if (run) job->calls++; else job->skips++;
    }
    pm_unlock(&g_framerate.lock);
    if (!run) { c->pc = c->x[30]; return; }
    InkypaintFrameSample before = t_frame.sample;
    u64 old_owner = t_frame.owner, old_frame = t_frame.frame;
    t_frame.sample = sample;
    t_frame.owner = owner;
    t_frame.frame = frame;
    for (u32 i = 0; i < sizeof executors / sizeof *executors; i++) {
        if (executors[i] != entry) continue;
        g_framerate.execute[i](c, entry);
        t_frame.sample = before;
        t_frame.owner = old_owner;
        t_frame.frame = old_frame;
        return;
    }
    fatal("Unknown task executor in frame diagnostics");
}
static void frame(CPU* c, u64 entry) {
    pm_lock(&g_framerate.lock);
    g_framerate.frame_serial++;
    pm_unlock(&g_framerate.lock);
    if (g_framerate.target && !g_framerate.keep_alternation) {
        *(u8*)GP(c->x[0] + 0x222) &= (u8)~3u;
        gw32(c->x[0] + 0x88, 0);
    }
    g_framerate.frame(c, entry);
    u64 now = plat_ns();
    if (!g_framerate.report_ns) g_framerate.report_ns = now;
    if (now - g_framerate.report_ns >= 5000000000ull) {
        double seconds = (double)(now - g_framerate.report_ns) / 1e9;
        pm_lock(&g_framerate.lock);
        for (u32 i = 0; i < JOB_SLOTS; i++) {
            FrameJob* job = &g_framerate.jobs[i];
            if (job->object && (job->calls || job->skips) &&
                (!strcmp(job->name, "Actor") || !strcmp(job->name, "Physics") ||
                 !strcmp(job->name, "GameFrame") || !strcmp(job->name, "GameNet") ||
                 !strcmp(job->name, "GamePhysics") || !strcmp(job->name, "Graphics") ||
                 !strcmp(job->name, "GameGfx") || !strcmp(job->name, "GameGfx:PostActorCalc")))
                LOG("frame-audit: %s role=%s %.2f calls/s, %llu gated\n", job->name,
                    job->simulation ? "gated graph task" : "render preparation", job->calls / seconds,
                    (unsigned long long)job->skips);
            job->calls = job->skips = 0;
        }
        if (g_framerate.actor_render)
            LOG("frame-actors: extra frames: %llu cut to actor phase 1, %llu in the split mode's phase 1 already\n",
                (unsigned long long)g_framerate.actor_phase_frames, (unsigned long long)g_framerate.actor_split_frames);
        g_framerate.actor_phase_frames = g_framerate.actor_split_frames = 0;
        if (g_framerate.target)
            LOG("frame-camera: %llu copies, %llu interpolated, %llu conflicting samples\n",
                (unsigned long long)g_framerate.camera_calls, (unsigned long long)g_framerate.camera_blends,
                (unsigned long long)g_framerate.camera_changes);
        g_framerate.camera_calls = g_framerate.camera_blends = g_framerate.camera_changes = 0;
        if (g_framerate.model_enabled)
            LOG("frame-models: %llu transforms (%llu set again on extra frames), %llu interpolated, %llu too far apart "
                "to\n", (unsigned long long)g_framerate.model_calls, (unsigned long long)g_framerate.model_replays,
                (unsigned long long)g_framerate.model_blends, (unsigned long long)g_framerate.model_jumps);
        g_framerate.model_calls = g_framerate.model_blends = g_framerate.model_jumps = g_framerate.model_replays = 0;
        if (g_framerate.anim_enabled && g_framerate.anim_calls)
            LOG("frame-anims: %llu advances, by %.3f of the game's delta on average\n",
                (unsigned long long)g_framerate.anim_calls, g_framerate.anim_scale_sum / (double)g_framerate.anim_calls);
        g_framerate.anim_calls = 0;
        g_framerate.anim_scale_sum = 0;
        pm_unlock(&g_framerate.lock);
        g_framerate.report_ns = now;
    }
}
static void calc(CPU* c, u64 entry) {
    if (g_framerate.target) gw32(c->x[0] + 0x2cc, 1);
    g_framerate.calc(c, entry);
}
static int readable(u64 a, u64 size) {
    MemBlock b;
    if (!a || a >= AS_SIZE || size > AS_SIZE - a) return 0;
    mem_query(a, &b);
    return (b.perm & PERM_R) && a >= b.addr && size <= b.size - (a - b.addr);
}
static int name_at(u64 a, char out[65]) {
    if (!readable(a, 64)) return 0;
    for (unsigned i = 0; i < 64; i++) {
        u8 c = *(const u8*)GP(a + i);
        out[i] = (char)c;
        if (!c) return i > 2;
        if (c < 32 || c > 126) return 0;
    }
    out[64] = 0;
    return 1;
}
static void inspect_graph(u64 graph, u64 caller) {
    if (!readable(graph, 0x40)) return;
    u32 count = gr32(graph + 8);
    u64 array = gr64(graph + 0x10), hash = 0xcbf29ce484222325ull;
    if (count > 512 || !readable(array, (u64)count * 8)) return;
    for (u32 i = 0; i < count; i++) hash = (hash ^ gr64(array + i * 8)) * 0x100000001b3ull;
    int seen = 0;
    u32 role = 0;
    if (g_framerate.module && readable(g_framerate.module, 0x4f8))
        for (u32 off = 0x4c8; off <= 0x4e0; off += 8)
            if (gr64(g_framerate.module + off) == graph) role = off;
    if (g_framerate.target && !role && count) fatal("Unrecognized graph in experimental frame scheduler");
    for (u32 i = 0; i < count; i++) {
        u64 desc = gr64(array + i * 8);
        if (!readable(desc, 24)) continue;
        u64 object = gr64(desc + 8);
        if (!readable(object, 16)) continue;
        if (g_framerate.target) {
            u64 vt = gr64(object);
            int known = 0;
            if (readable(vt, 24))
                for (u32 n = 0; n < sizeof executors / sizeof *executors; n++)
                    if (gr64(vt + 16) == executors[n]) known = 1;
            if (!known) fatal("Unrecognized task executor in experimental frame scheduler");
        }
        FrameJob* job = find_job(object);
        if (!job) fatal("Frame task registry exhausted");
        if (!job->object) job->object = object;
        /* Refresh after address reuse. A name is diagnostic, never an object-lifetime identifier. */
        if (!name_at(gr64(object + 8), job->name)) job->name[0] = 0;
        job->simulation = role == 0x4c8 || role == 0x4d0 || !strcmp(job->name, "GameNet:Recv");
        /* The graphics module maintains render resources even when no simulation step is due. */
        if (!strcmp(job->name, "Graphics") || !strcmp(job->name, "Resource") ||
            !strcmp(job->name, "GameGfx") || !strcmp(job->name, "GameGfx:PostActorCalc")) job->simulation = 0;
        /* The Actor module job fills each frame's actor work, which the render graph then runs: an extra frame
           without it draws without the actors' per-frame work (the plaza's flash). It runs on every frame, and on an
           extra one actor_schedule keeps it to phase 1, the way the game's split mode runs a frame */
        if (g_framerate.actor_render && readable(object, 8) && gr64(object) == ACTOR_MODULE_VTABLE) job->simulation = 0;
        if (job->name[0] && (g_framerate.allow[0] || g_framerate.extra[0])) {
            char key[68];
            snprintf(key, sizeof key, ",%s,", job->name);
            if (g_framerate.extra[0] && strstr(g_framerate.extra, key)) job->simulation = 1;
            if (g_framerate.allow[0] && (!strcmp(g_framerate.allow, ",*,") || strstr(g_framerate.allow, key)))
                job->simulation = 0;
        }
        job->run = !job->simulation || g_framerate.simulating;
        job->sample = g_framerate.sample;
        job->frame = g_framerate.frame_serial;
    }
    if (!g_framerate.trace) return;
    for (u32 i = 0; i < g_framerate.nseen; i++)
        if (g_framerate.seen[i].address == graph && g_framerate.seen[i].hash == hash) seen = 1;
    if (seen || g_framerate.nseen == 128) return;
    g_framerate.seen[g_framerate.nseen].address = graph;
    g_framerate.seen[g_framerate.nseen++].hash = hash;
    LOG("frame-graph: graph=%llx caller=%llx jobs=%u edges=%u module=%llx\n",
        (unsigned long long)graph, (unsigned long long)caller, count, gr32(graph + 0x30),
        (unsigned long long)g_framerate.module);
    if (g_framerate.module && readable(g_framerate.module, 0x4f8))
        for (u32 off = 0x4c8; off <= 0x4e0; off += 8)
            if (gr64(g_framerate.module + off) == graph) LOG("frame-graph-role: module+%x\n", off);
    for (u32 i = 0; i < count; i++) {
        u64 desc = gr64(array + i * 8);
        if (!readable(desc, 24)) continue;
        u64 job = gr64(desc + 8), vt = readable(job, 16) ? gr64(job) : 0;
        LOG("frame-job: %u desc=%llx id=%u flags=%x job=%llx vt=%llx", i,
            (unsigned long long)desc, gr16(desc), gr32(desc + 4), (unsigned long long)job,
            (unsigned long long)vt);
        if (readable(vt, 32)) LOG(" methods=%llx,%llx,%llx,%llx",
            (unsigned long long)gr64(vt), (unsigned long long)gr64(vt + 8),
            (unsigned long long)gr64(vt + 16), (unsigned long long)gr64(vt + 24));
        LOG("\n");
        if (!readable(job, 0x80)) continue;
        for (u32 off = 8; off < 0x80; off += 8) {
            char name[65];
            if (name_at(gr64(job + off), name)) LOG("frame-job-name: %u +%x %s\n", i, off, name);
        }
    }
}
static void build_graph(CPU* c, u64 entry) {
    pm_lock(&g_framerate.lock);
    inspect_graph(c->x[1], c->x[30]);
    pm_unlock(&g_framerate.lock);
    g_framerate.build_graph(c, entry);
}
static void model_set_mtx(CPU* c, u64 entry);
/* An extra frame holds the actors back, and with them their models' transforms: set each model the last frame set
   again, through the hook, so it interpolates. Without a transform a frame's model update finds nothing new
   (the flash the actor job's holding showed). Only models set on the last frame, still of the class they were then
   (a model freed since has another vtable) */
static void model_replay(CPU* c, u64 frame) {
    static u64 list[MODEL_SLOTS];
    static float matrices[MODEL_SLOTS][15];
    u32 n = 0;
    pm_lock(&g_framerate.lock);
    for (u32 i = 0; i < MODEL_SLOTS; i++) {
        FrameModel* m = &g_framerate.models[i];
        if (!m->model || m->frame + 1 != frame || !m->vtable) continue;
        if (!readable(m->model, 0x310) || gr64(m->model) != m->vtable) continue;
        list[n] = m->model;
        memcpy(matrices[n], m->current, 48);
        memcpy(&matrices[n][12], m->current_scale, 12);
        n++;
    }
    pm_unlock(&g_framerate.lock);
    if (!n) return;
    u64 x[31], pc = c->pc, sp = c->x[31];
    V128 v[32];
    u32 nzcv[4] = {c->n, c->z, c->cf, c->vf}, fpsr = c->fpsr;
    memcpy(x, c->x, sizeof x);
    memcpy(v, c->v, sizeof v);
    for (u32 i = 0; i < n; i++) {
        c->x[31] = (sp - 0x100) & ~15ull;
        u64 data = c->x[31] + 0x80;
        for (u32 k = 0; k < 15; k++) {
            u32 bits;
            memcpy(&bits, &matrices[i][k], 4);
            gw32(data + k*4, bits);
        }
        c->x[0] = list[i];
        c->x[1] = data;
        c->x[2] = data + 48;
        c->x[30] = pc;
        model_set_mtx(c, MODEL_SET_MTX);
    }
    memcpy(c->x, x, sizeof x);
    memcpy(c->v, v, sizeof v);
    c->x[31] = sp;
    c->pc = pc;
    c->n = nzcv[0], c->z = nzcv[1], c->cf = nzcv[2], c->vf = nzcv[3];
    c->fpsr = fpsr;
    pm_lock(&g_framerate.lock);
    g_framerate.model_replays += n;
    pm_unlock(&g_framerate.lock);
}
static void module_calc(CPU* c, u64 entry) {
    pm_lock(&g_framerate.lock);
    g_framerate.module = c->x[0];
    g_framerate.simulating = 1;
    if (g_framerate.target && !g_framerate.no_gate) {
        /* INKYPAINT_LOGIC_HZ=30 slows the game to half speed so that every other frame is an extra one: the
           extra frames' problems show at once instead of a few times a minute */
        u64 now = guest_ns();
        if (g_framerate.logic_hz) now = now / 60 * g_framerate.logic_hz;
        InkypaintFrameSample sample = inkypaint_frame_sample(&g_framerate.clock, now);
        if (g_framerate.max_steps && sample.steps > g_framerate.max_steps) sample.steps = g_framerate.max_steps;
        g_framerate.sample = sample;
        g_framerate.simulating = sample.steps > 0;
        /* Preserve the native graph joins and six-step catch-up path. An extra render frame still builds the
           graphs, but their registered simulation callbacks return without advancing the game. */
        gw32(c->x[0] + 0x4fc, sample.steps ? sample.steps : 1);
    }
    int replay = g_framerate.target && !g_framerate.no_gate && !g_framerate.simulating && g_framerate.model_replay &&
                 g_framerate.model_enabled && !g_framerate.actor_render;
    u64 frame = g_framerate.frame_serial;
    pm_unlock(&g_framerate.lock);
    if (replay) model_replay(c, frame);
    g_framerate.module_calc(c, entry);
}
/* Sample the authoritative view input, before the engine builds inverse matrices, GPU camera data and culling
   planes. Editing the completed view-info object leaves some of those products from a different camera pose. */
static int camera_sample(u64 object, u32 index, u64 projection, u64 source, float result[12]) {
    if (!g_framerate.target || !g_framerate.camera_enabled || !readable(source, 48) || !readable(projection, 64))
        return 0;
    float current[12], view[12], interpolated[12], q[4];
    for (u32 i = 0; i < 12; i++) {
        u32 bits = gr32(source + i*4);
        memcpy(&view[i], &bits, 4);
    }
    if (!inkypaint_camera_rotation(view, q)) return 0;
    inkypaint_camera_inverse(current, view);
    u64 hash = 0xcbf29ce484222325ull;
    for (u32 i = 0; i < 16; i++) hash = (hash ^ gr32(projection + i*4)) * 0x100000001b3ull;
    pm_lock(&g_framerate.lock);
    /* a hooked job's sample, else the frame's (some scenes set their camera from threads no hooked job runs on) */
    InkypaintFrameSample sample = t_frame.owner ? t_frame.sample : g_framerate.sample;
    u64 now_frame = t_frame.owner ? t_frame.frame : g_framerate.frame_serial;
    g_framerate.camera_calls++;
    FrameCamera* camera = NULL;
    FrameCamera* oldest = &g_framerate.cameras[0];
    for (u32 i = 0; i < sizeof g_framerate.cameras / sizeof *g_framerate.cameras; i++) {
        FrameCamera* slot = &g_framerate.cameras[i];
        if (slot->object == object && slot->index == index && slot->projection == projection) camera = slot;
        if (slot->frame < oldest->frame) oldest = slot;
    }
    if (!camera) {
        camera = oldest;
        memset(camera, 0, sizeof *camera);
        camera->object = object;
        camera->index = index;
        camera->projection = projection;
    }
    if (camera->projection_hash != hash || sample.reset || camera->frame + 1 < now_frame ||
        camera->tick > sample.tick || camera->tick + 1 < sample.tick) camera->history = 0;
    if (camera->tick != sample.tick) {
        memcpy(camera->previous, camera->current, sizeof current);
        camera->history = camera->frame && camera->projection_hash == hash &&
                          camera->tick + 1 == sample.tick && !sample.reset &&
                          camera->frame + 1 >= now_frame;
        camera->tick = sample.tick;
        memcpy(camera->current, current, sizeof current);
    } else if (memcmp(camera->current, current, sizeof current)) {
        camera->history = 0;
        g_framerate.camera_changes++;
        memcpy(camera->current, current, sizeof current);
    }
    camera->frame = now_frame;
    camera->projection_hash = hash;
    int blend = camera->history && inkypaint_camera_blend(interpolated, camera->previous, current, sample.alpha);
    if (blend) g_framerate.camera_blends++;
    pm_unlock(&g_framerate.lock);
    if (blend) inkypaint_camera_inverse(result, interpolated);
    return blend;
}
/* A model's transform between its last two steps' (row-major 3x4, scale apart). Rotations go the short way when
   both are rotations; a transform that isn't one (sheared, reflected) only moves, when its 3x3 part stayed the same.
   Abrupt changes (a respawn, a teleport) show the current transform: inkypaint_camera_blend's limits */
static int model_blend(float out[12], float out_scale[3], const FrameModel* m, float alpha) {
    if (inkypaint_camera_blend(out, m->previous, m->current, alpha)) {
    } else {
        float distance = 0;
        for (u32 i = 0; i < 12; i++)
            if (i % 4 != 3 && m->previous[i] != m->current[i]) return 0;
        for (u32 i = 3; i < 12; i += 4) distance += (m->current[i] - m->previous[i]) * (m->current[i] - m->previous[i]);
        if (!isfinite(distance) || distance > 10000) return 0;
        for (u32 i = 0; i < 12; i++)
            out[i] = i % 4 == 3 ? m->previous[i] + (m->current[i] - m->previous[i]) * alpha : m->current[i];
    }
    for (u32 i = 0; i < 3; i++) {
        out_scale[i] = m->previous_scale[i] + (m->current_scale[i] - m->previous_scale[i]) * alpha;
        if (!isfinite(out_scale[i])) return 0;
    }
    return 1;
}
static FrameModel* find_model(u64 model) {
    u32 start = (u32)((model * 0x9e3779b97f4a7c15ull) >> 40) & (MODEL_SLOTS - 1);
    FrameModel* oldest = NULL;
    for (u32 n = 0; n < 16; n++) {
        FrameModel* slot = &g_framerate.models[(start + n) & (MODEL_SLOTS - 1)];
        if (slot->model == model) return slot;
        if (!oldest || !slot->model || (oldest->model && slot->frame < oldest->frame)) oldest = slot;
    }
    memset(oldest, 0, sizeof *oldest);
    oldest->model = model;
    return oldest;
}
/* The frame's own sample (module_calc's, set before any of its jobs run): the actors' workers aren't hooked jobs */
static void model_set_mtx(CPU* c, u64 entry) {
    float matrix[12], scale[3], out[12], out_scale[3];
    int blend = 0;
    if (!g_framerate.target || !g_framerate.model_enabled || !readable(c->x[1], 48) || !readable(c->x[2], 12)) {
        g_framerate.model_set_mtx(c, entry);
        return;
    }
    for (u32 i = 0; i < 12; i++) {
        u32 bits = gr32(c->x[1] + i*4);
        memcpy(&matrix[i], &bits, 4);
    }
    for (u32 i = 0; i < 3; i++) {
        u32 bits = gr32(c->x[2] + i*4);
        memcpy(&scale[i], &bits, 4);
    }
    pm_lock(&g_framerate.lock);
    InkypaintFrameSample sample = g_framerate.sample;
    u64 frame = g_framerate.frame_serial;
    FrameModel* m = find_model(c->x[0]);
    g_framerate.model_calls++;
    m->vtable = readable(c->x[0], 8) ? gr64(c->x[0]) : 0;
    if (m->tick != sample.tick) {
        /* a new step: its transform becomes the current one, the last one the previous, if they're consecutive */
        m->history = m->tick && m->tick + 1 == sample.tick && !sample.reset && m->frame + 8 >= frame;
        memcpy(m->previous, m->current, sizeof matrix);
        memcpy(m->previous_scale, m->current_scale, sizeof scale);
        m->tick = sample.tick;
    }
    /* the same step's transform set again (attachments set after their parents) replaces the current one */
    memcpy(m->current, matrix, sizeof matrix);
    memcpy(m->current_scale, scale, sizeof scale);
    m->frame = frame;
    if (m->history) {
        blend = model_blend(out, out_scale, m, sample.alpha);
        if (blend) g_framerate.model_blends++; else g_framerate.model_jumps++;
    }
    pm_unlock(&g_framerate.lock);
    if (!blend) {
        g_framerate.model_set_mtx(c, entry);
        return;
    }
    /* copies on the guest stack: the actor's own transform stays as its step left it. The function copies both
       into the model and keeps no reference to them */
    u64 sp = c->x[31];
    c->x[31] -= 64;
    c->x[1] = c->x[31];
    c->x[2] = c->x[31] + 48;
    for (u32 i = 0; i < 12; i++) {
        u32 bits;
        memcpy(&bits, &out[i], 4);
        gw32(c->x[1] + i*4, bits);
    }
    for (u32 i = 0; i < 3; i++) {
        u32 bits;
        memcpy(&bits, &out_scale[i], 4);
        gw32(c->x[2] + i*4, bits);
    }
    g_framerate.model_set_mtx(c, entry);
    c->x[31] = sp;
}
static float float_argument(CPU* c, u32 index) {
    u32 bits = SNX_VS(c->v[index], 0);
    float value;
    memcpy(&value, &bits, 4);
    return value;
}
static void anim_advance(CPU* c, u64 entry) {
    float delta = float_argument(c, 0);
    if (g_framerate.target && g_framerate.anim_enabled && delta > 0 && isfinite(delta)) {
        pm_lock(&g_framerate.lock);
        /* by the game time since this model last advanced: right whichever frames call it, extra ones or not */
        InkypaintFrameSample sample = g_framerate.sample;
        u32 start = (u32)((c->x[0] * 0x9e3779b97f4a7c15ull) >> 40) & (MODEL_SLOTS - 1), slot = start;
        for (u32 n = 0; n < 16; n++) {
            u32 i = (start + n) & (MODEL_SLOTS - 1);
            if (g_framerate.anims[i].anim == c->x[0]) { slot = i; break; }
            if (!g_framerate.anims[i].anim || g_framerate.anims[i].tick + 120 < sample.tick) slot = i;
        }
        float scale = 1;
        if (g_framerate.anims[slot].anim == c->x[0] && !sample.reset) {
            float since = (float)(sample.tick - g_framerate.anims[slot].tick) + sample.alpha - g_framerate.anims[slot].alpha;
            scale = since < 0 ? 0 : since > 6 ? 6 : since;
        }
        g_framerate.anims[slot].anim = c->x[0];
        g_framerate.anims[slot].tick = sample.tick;
        g_framerate.anims[slot].alpha = sample.alpha;
        g_framerate.anim_calls++;
        g_framerate.anim_scale_sum += scale;
        pm_unlock(&g_framerate.lock);
        float scaled = delta * scale;
        u32 bits;
        memcpy(&bits, &scaled, 4);
        SNX_VSET_S(c->v[0], 0, bits);
    }
    g_framerate.anim_advance(c, entry);
}
/* GameGfx also retires render resources and updates render queues. Those operations run every render frame.
   Its visual-time counter is integer ticks at 60 Hz, so an extra render frame must not increment that counter. */
static void game_gfx_calc(CPU* c, u64 entry) {
    if (!g_framerate.target || g_framerate.no_gfx_hold || !t_frame.owner || t_frame.sample.steps) {
        g_framerate.game_gfx_calc(c, entry);
        return;
    }
    u64 counter = c->x[0] + 0x470;
    u32 before = gr32(counter);
    gw32(counter, before - 1);
    g_framerate.game_gfx_calc(c, entry);
    gw32(counter, before);
}
static void actor_schedule(CPU* c, u64 entry) {
    /* an extra frame (no simulation step due) of the Actor module job: phase 1 alone, never the simulation's phase 0 */
    if (g_framerate.actor_render && g_framerate.target && t_frame.owner && !t_frame.sample.steps) {
        int whole = (c->x[2] & 0xf) == 0xf;
        if (whole) c->x[2] = 2;
        pm_lock(&g_framerate.lock);
        /* the plaza runs the split mode: its per-frame call is phase 1 already (mask 2) */
        if (whole) g_framerate.actor_phase_frames++;
        else g_framerate.actor_split_frames++;
        pm_unlock(&g_framerate.lock);
    }
    g_framerate.actor_schedule(c, entry);
}
static void camera_view(CPU* c, u64 entry) {
    float view[12];
    float z_near = float_argument(c, 0), fovy = float_argument(c, 2), aspect = float_argument(c, 3);
    if (z_near <= 0 || z_near >= 1 || fovy <= 0 || fabsf(aspect - 16.f/9.f) > .001f ||
        !camera_sample(c->x[0], (u32)c->x[1], c->x[3], c->x[2], view)) {
        g_framerate.camera_view(c, entry);
        return;
    }
    /* A stack input copy leaves the simulation camera unchanged. The native routine copies all 48 input bytes
       into its render entry and retains no reference to this temporary matrix. */
    u64 sp = c->x[31];
    c->x[31] -= 64;
    c->x[2] = c->x[31];
    for (u32 i = 0; i < 12; i++) {
        u32 bits;
        memcpy(&bits, &view[i], 4);
        gw32(c->x[2] + i*4, bits);
    }
    g_framerate.camera_view(c, entry);
    c->x[31] = sp;
}
static GuestFn hook(u64 a, GuestFn wrapper) {
    jit_code_changed(a, 4);
    GuestFn original = jit_wait(a);
    if (original) g_disp[(a - g_code_lo) / 4] = wrapper;
    return original;
}
void inkypaint_framerate_init(void) {
    const char* trace = getenv("INKYPAINT_FRAME_TRACE");
    const char* target = getenv("INKYPAINT_RENDER_HZ");
    int tracing = trace && *trace && *trace != '0';
    /* the setting (smooth_motion), or INKYPAINT_RENDER_HZ=120 for a run from the command line */
    int requested = (target && !strcmp(target, "120")) || g_settings.smooth_motion;
    if ((!tracing && !requested) || inkypaint_any_title() ||
        g_hle.kernel.title_id != INKYPAINT_TITLE_ID) return;
    if (BUILD_GRAPH < g_code_lo || MODULE_CALC >= g_code_hi ||
        gr32(BUILD_GRAPH) != 0xa9bc7bfd || gr32(MODULE_CALC) != 0xa9bd7bfd ||
        CALC < g_code_lo || CAMERA_VIEW >= g_code_hi || gr32(CAMERA_VIEW) != 0x39416008 || gr32(GAME_GFX_CALC) != 0xd10183ff || gr32(FRAME) != 0xa9be7bfd || gr32(CALC) != 0xfc1c0fea) {
        WARN("frame-graph: this executable does not match the graph inspection hooks\n");
        return;
    }
    static const u32 prologues[] = {
        0xf9400801, 0xa9c12408, 0xd101c3ff, 0xf9400800, 0xb000a8a8, 0xb000a8a8, 0xd10083ff,
        0xd101c3ff, 0xd10203ff, 0xf0009fc8, 0xf9400809, 0xa9ba7bfd, 0xf00252a8, 0xf9400800
    };
    for (u32 i = 0; i < sizeof executors / sizeof *executors; i++) {
        if (executors[i] < g_code_lo || executors[i] >= g_code_hi || gr32(executors[i]) != prologues[i]) {
            WARN("framerate: task executor hooks do not match this executable\n");
            return;
        }
    }
    g_framerate.target = requested ? 120 : 0;
    const char* comparison = getenv("INKYPAINT_CAMERA_COMPARE");
    g_framerate.camera_enabled = !comparison || strcmp(comparison, "off");
    /* Bisection of the plaza's horizontal line, one part of the experiment left native each:
       INKYPAINT_KEEP_ALTERNATION=1 keeps the framework's half-rate render alternation (+0x222) and vblank wait,
       INKYPAINT_NO_GATE=1 runs the simulation on every frame (the game runs fast: for looking only),
       INKYPAINT_NO_GFX_HOLD=1 lets GameGfx's visual-time counter advance on every frame */
    g_framerate.keep_alternation = getenv("INKYPAINT_KEEP_ALTERNATION") && !strcmp(getenv("INKYPAINT_KEEP_ALTERNATION"), "1");
    g_framerate.no_gate = getenv("INKYPAINT_NO_GATE") && !strcmp(getenv("INKYPAINT_NO_GATE"), "1");
    g_framerate.no_gfx_hold = getenv("INKYPAINT_NO_GFX_HOLD") && !strcmp(getenv("INKYPAINT_NO_GFX_HOLD"), "1");
    {
        const char* allow = getenv("INKYPAINT_GATE_ALLOW");
        if (allow && *allow && strlen(allow) + 3 < sizeof g_framerate.allow) {
            snprintf(g_framerate.allow, sizeof g_framerate.allow, ",%s,", allow);
            WARN("framerate: bisection: these gated jobs run on every frame: %s\n", allow);
        }
    }
    {
        const char* extra = getenv("INKYPAINT_GATE_EXTRA");
        const char* steps = getenv("INKYPAINT_MAX_STEPS");
        if (extra && *extra && strlen(extra) + 3 < sizeof g_framerate.extra) {
            snprintf(g_framerate.extra, sizeof g_framerate.extra, ",%s,", extra);
            WARN("framerate: bisection: these jobs are gated too: %s\n", extra);
        }
        const char* logic = getenv("INKYPAINT_LOGIC_HZ");
        if (logic && *logic) {
            unsigned hz = (unsigned)strtoul(logic, NULL, 10);
            if (hz >= 1 && hz < 60) {
                g_framerate.logic_hz = hz;
                WARN("framerate: test: the game's clock runs at %u steps a second (slow motion)\n", hz);
            }
        }
        if (steps && *steps) {
            g_framerate.max_steps = (unsigned)strtoul(steps, NULL, 10);
            WARN("framerate: bisection: at most %u simulation steps a frame\n", g_framerate.max_steps);
        }
    }
    if (g_framerate.keep_alternation || g_framerate.no_gate || g_framerate.no_gfx_hold || !g_framerate.camera_enabled)
        WARN("framerate: bisection: alternation %s, gate %s, GameGfx hold %s, camera interpolation %s\n",
             g_framerate.keep_alternation ? "native" : "cleared", g_framerate.no_gate ? "off" : "on",
             g_framerate.no_gfx_hold ? "off" : "on", g_framerate.camera_enabled ? "on" : "off");
    g_framerate.trace = tracing;
    g_framerate.frame = hook(FRAME, frame);
    g_framerate.calc = hook(CALC, calc);
    if (requested) {
        g_framerate.camera_view = hook(CAMERA_VIEW, camera_view);
        g_framerate.game_gfx_calc = hook(GAME_GFX_CALC, game_gfx_calc);
        /* on by default: without it an extra frame has no actor work and flashes (2026-10-26 bisection) */
        /* opt-in since it turned out to be actor logic: split mode's per-frame phase (mask 2) is a step's at 60 FPS,
           so running it on extra frames ran the actors (moving, shooting, swimming, hair physics) at the frame rate */
        g_framerate.actor_render = getenv("INKYPAINT_ACTOR_RENDER") && !strcmp(getenv("INKYPAINT_ACTOR_RENDER"), "1");
        if (g_framerate.actor_render) {
            /* code only: the vtable is data, which the game's own start-up relocates after this runs */
            if (gr32(ACTOR_SCHEDULE) != 0xd10583ff || gr32(0xccdc630ull) != 0xd10203ff)
                fatal("INKYPAINT_ACTOR_RENDER: the actor hooks do not match this executable");
            g_framerate.actor_schedule = hook(ACTOR_SCHEDULE, actor_schedule);
            if (!g_framerate.actor_schedule) fatal("Could not install the actor schedule hook");
            LOG("framerate: the Actor module job runs on extra frames, with actor phase 1 alone\n");
        }
        g_framerate.model_enabled = !getenv("INKYPAINT_MODEL_INTERP") || strcmp(getenv("INKYPAINT_MODEL_INTERP"), "0");
        if (g_framerate.model_enabled) {
            if (gr32(MODEL_SET_MTX) != 0xd10183ff || gr32(MODEL_SET_MTX + 0x128) != 0x9711060f)
                fatal("INKYPAINT_MODEL_INTERP: the model hook does not match this executable");
            g_framerate.model_set_mtx = hook(MODEL_SET_MTX, model_set_mtx);
            if (!g_framerate.model_set_mtx) fatal("Could not install the model transform hook");
            g_framerate.model_replay = !getenv("INKYPAINT_MODEL_REPLAY") || strcmp(getenv("INKYPAINT_MODEL_REPLAY"), "0");
            LOG("framerate: models' transforms interpolated between steps%s\n",
                g_framerate.model_replay ? ", and set again on extra frames" : "");
        }
        g_framerate.anim_enabled = !getenv("INKYPAINT_ANIM_ADVANCE") || strcmp(getenv("INKYPAINT_ANIM_ADVANCE"), "0");
        if (g_framerate.anim_enabled) {
            if (gr32(ANIM_ADVANCE) != 0x39411008 || gr32(ANIM_ADVANCE + 0x34) != 0x1e200828)
                fatal("INKYPAINT_ANIM_ADVANCE: the animation hook does not match this executable");
            g_framerate.anim_advance = hook(ANIM_ADVANCE, anim_advance);
            if (!g_framerate.anim_advance) fatal("Could not install the animation advance hook");
            LOG("framerate: animations advance by the game time each frame shows\n");
        }
    }
    for (u32 i = 0; i < sizeof executors / sizeof *executors; i++) {
        g_framerate.execute[i] = hook(executors[i], execute_job);
        if (!g_framerate.execute[i]) fatal("Could not install a frame task executor hook");
    }
    g_framerate.build_graph = hook(BUILD_GRAPH, build_graph);
    g_framerate.module_calc = hook(MODULE_CALC, module_calc);
    if (!g_framerate.frame || !g_framerate.calc || !g_framerate.build_graph || !g_framerate.module_calc ||
        (requested && (!g_framerate.camera_view || !g_framerate.game_gfx_calc)))
        fatal("Could not install task graph diagnostics");
    if (g_framerate.target)
        LOG("framerate: experimental 120 Hz task scheduling and render-camera interpolation\n");
    else
        LOG("frame-graph: inspecting task descriptors without changing game timing\n");
}
