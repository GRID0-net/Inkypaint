/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* Splatoon 3 11.3.0: what the player is doing, for the launcher's Discord rich presence. About once a second the
   frontend's thread reads the scene's name, the content mode and, in a battle, its versus info from guest memory and
   writes <state>/<id>.presence.json (inkypaint_presence_json.h) when it changed, or every 5 seconds as a heartbeat.

   Each address is a GOT slot in main that holds a pointer to the variable (as inkypaint_framerate.c's 0xdf25148):
   - the scene manager at [[0xdf24a40]], its current scene's name at [mgr+0x2a0] (Plaza, Vss_Hiagari04, Cop_Shakeup)
   - the content mode at [[[[0xdf28f60]]+0xd0]+0x48]+0x134c (0 the base game, 1 and 2 the DLC's)
   - the versus info V at [[[0xdf3c1a0]]+0xd0]+0x6688, its mode at +0x38, rule at +0x3c and stage at +0x40, read
     only in a Vss_ scene. Which rule each number is stays the launcher's guess.
   Every read is best effort: a pointer that is null or not readable is unknown, never a fault. */
#include "nx/runtime/inkypaint_presence.h"
#include "nx/runtime/inkypaint_presence_json.h"
#include "nx/runtime/driver/inkypaint.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/hle/hle.h"
#include "nx/memory/mem.h"
#include "common/utilities/common.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#define SCENE_SLOT   0xdf24a40ull
#define CONTENT_SLOT 0xdf28f60ull
#define VERSUS_SLOT  0xdf3c1a0ull
#define HEARTBEAT_MS 5000ull

static struct {
    int on;
    InkypaintPresence last;
    u64 written_ms;
    char path[1400];
} g_presence;

static int readable(u64 a, u64 size) {
    MemBlock b;
    if (!a || a >= AS_SIZE || size > AS_SIZE - a) return 0;
    mem_query(a, &b);
    return (b.perm & PERM_R) && a >= b.addr && size <= b.size - (a - b.addr);
}
static u64 follow(u64 a, u64 off) {
    return a && readable(a + off, 8) ? gr64(a + off) : 0;
}
/* the int at a + off, a an object that may be null */
static int read_int(u64 a, u64 off, int unknown) {
    return a && readable(a + off, 4) ? (int)gr32(a + off) : unknown;
}
/* A NUL-terminated printable name, at most 64 bytes, or empty */
static void read_name(u64 a, char out[65]) {
    out[0] = 0;
    for (unsigned i = 0; i < 64; i++) {
        u8 c;
        if (!readable(a + i, 1)) break;
        c = *(const u8*)GP(a + i);
        if (!c) {
            out[i] = 0;
            return;
        }
        if (c < 32 || c > 126) break;
        out[i] = (char)c;
    }
    out[0] = 0;
}

static void sample(InkypaintPresence* p) {
    u64 v;
    read_name(follow(follow(follow(SCENE_SLOT, 0), 0), 0x2a0), p->scene);
    p->content = read_int(follow(follow(follow(follow(CONTENT_SLOT, 0), 0), 0xd0), 0x48), 0x134c, -1);
    p->rule = p->mode = p->stage_id = -1;
    if (!strncmp(p->scene, "Vss_", 4)) {
        v = follow(follow(follow(follow(VERSUS_SLOT, 0), 0), 0xd0), 0x6688);
        p->mode = read_int(v, 0x38, -1);
        p->rule = read_int(v, 0x3c, -1);
        p->stage_id = read_int(v, 0x40, -1);
    }
}

static long own_pid(void) {
#if defined(_WIN32)
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}

static void write_file(const InkypaintPresence* p, u64 now_ms) {
    char json[512], staged[1410];
    size_t n = inkypaint_presence_json(json, sizeof json, g_hle.kernel.title_id, own_pid(), now_ms, p);
    FILE* f;
    if (!n) return;
    snprintf(staged, sizeof staged, "%s.tmp", g_presence.path);
    f = fopen(staged, "wb");
    if (!f) return;
    if (fwrite(json, 1, n, f) != n) {
        fclose(f);
        (void)remove(staged);
        return;
    }
    if (fclose(f) || snx_replace(staged, g_presence.path)) (void)remove(staged);
}

void inkypaint_presence_init(void) {
    if (inkypaint_any_title() || g_hle.kernel.title_id != INKYPAINT_TITLE_ID) return;
    snprintf(g_presence.path, sizeof g_presence.path, "%s%s.presence.json", g_statedir, g_exename);
    g_presence.last.rule = g_presence.last.mode = g_presence.last.stage_id = g_presence.last.content = -1;
    g_presence.on = 1;
}

void inkypaint_presence_tick(void) {
    InkypaintPresence now;
    u64 now_ms;
    int changed;
    if (!g_presence.on) return;
    if (!g_settings.discord_presence_status) {
        /* turned off while running: the file goes, so the launcher does not show a stale scene */
        if (g_presence.written_ms) (void)remove(g_presence.path);
        g_presence.written_ms = 0;
        g_presence.last.scene[0] = 0;
        return;
    }
    sample(&now);
    now_ms = (u64)plat_unix_time() * 1000ull; /* a heartbeat: the second is precise enough */
    if (strcmp(now.scene, g_presence.last.scene))
        LOG("presence: scene %s rule %d\n", now.scene[0] ? now.scene : "(unknown)", now.rule);
    changed = strcmp(now.scene, g_presence.last.scene) || now.content != g_presence.last.content ||
              now.rule != g_presence.last.rule || now.mode != g_presence.last.mode ||
              now.stage_id != g_presence.last.stage_id;
    if (!changed && g_presence.written_ms && now_ms >= g_presence.written_ms &&
        now_ms - g_presence.written_ms < HEARTBEAT_MS)
        return;
    write_file(&now, now_ms);
    g_presence.last = now;
    g_presence.written_ms = now_ms;
}

void inkypaint_presence_exit(void) {
    if (g_presence.on && g_presence.written_ms) (void)remove(g_presence.path);
    g_presence.on = 0;
}
