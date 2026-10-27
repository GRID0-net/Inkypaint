/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* The indirect-call census (inkypaint_calltrace.h). Every function entry the dispatch table holds is wrapped by one
   wrapper, which finds the original by its entry in a hash table. Guest code reaches the table for indirect calls
   (BLR, BR) only: direct calls between compiled functions are untouched, and so is the game's speed outside the
   window but for one atomic load per indirect call */
#include "nx/runtime/inkypaint_calltrace.h"
#include "nx/runtime/inkypaint_framerate.h"
#include "nx/runtime/driver/inkypaint.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/guest_jit.h"
#include "nx/runtime/clock.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/hle/hle.h"
#include "nx/memory/mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDGE_SLOTS (1u << 20)
#define WINDOW_NS 2000000000ull
#define REPORT 200

typedef struct {
    u64 pc;
    GuestFn fn;
} TraceFn;
typedef struct {
    u64 caller, target, vtable, count;
    u64 phase;           /* inkypaint_framerate_phase() of the calls counted */
    u64 last, objects;   /* the first argument of the last call, and how often it changed: about the objects */
} TraceEdge;
static struct {
    TraceFn* fns;
    u32 fn_mask; /* the table's size less one, a power of two at least twice the functions */
    TraceEdge* edges;
    PMutex lock;
    u64 start_ns, end_ns;
    int state; /* 0 waiting, 1 counting, 2 reported */
    u64 lost;
} g_trace = {.lock = PMUTEX_INIT};

static u32 slot_of(u64 pc) { return (u32)((pc * 0x9e3779b97f4a7c15ull) >> 32) & g_trace.fn_mask; }
static GuestFn original(u64 pc) {
    for (u32 i = slot_of(pc), n = 0; n <= g_trace.fn_mask; n++, i = (i + 1) & g_trace.fn_mask) {
        if (g_trace.fns[i].pc == pc) return g_trace.fns[i].fn;
        if (!g_trace.fns[i].pc) break;
    }
    return NULL;
}
static int readable(u64 a, u64 size) {
    MemBlock b;
    if (!a || a >= AS_SIZE || size > AS_SIZE - a) return 0;
    mem_query(a, &b);
    return (b.perm & PERM_R) && a >= b.addr && size <= b.size - (a - b.addr);
}
static int by_count(const void* a, const void* b) {
    u64 x = ((const TraceEdge*)a)->count, y = ((const TraceEdge*)b)->count;
    return x < y ? 1 : x > y ? -1 : 0;
}
static void report(void) {
    u32 n = 0;
    for (u32 i = 0; i < EDGE_SLOTS; i++)
        if (g_trace.edges[i].count) g_trace.edges[n++] = g_trace.edges[i];
    qsort(g_trace.edges, n, sizeof *g_trace.edges, by_count);
    /* every site into calltrace.txt beside the log (it stays in the game's folder), the busiest into the log */
    FILE* f = fopen("calltrace.txt", "w");
    WARN("calltrace: %u call sites over %.2f s (%llu not counted: table full), all in calltrace.txt%s\n", n,
         (double)(g_trace.end_ns - g_trace.start_ns) / 1e9, (unsigned long long)g_trace.lost, f ? "" : " (not written)");
    for (u32 i = 0; i < n; i++) {
        const TraceEdge* e = &g_trace.edges[i];
        if (i < REPORT)
            WARN("calltrace: %8llu phase=%llu caller=%llx target=%llx vtable=%llx objects~%llu\n",
                 (unsigned long long)e->count, (unsigned long long)e->phase, (unsigned long long)e->caller,
                 (unsigned long long)e->target, (unsigned long long)e->vtable, (unsigned long long)e->objects);
        if (f)
            fprintf(f, "%llu %llu %llx %llx %llx %llu\n", (unsigned long long)e->count, (unsigned long long)e->phase,
                    (unsigned long long)e->caller, (unsigned long long)e->target, (unsigned long long)e->vtable,
                    (unsigned long long)e->objects);
    }
    if (f) fclose(f);
}
static void wrapper(CPU* c, u64 entry) {
    int state = __atomic_load_n(&g_trace.state, __ATOMIC_RELAXED);
    u64 now = state != 2 ? guest_ns() : 0;
    if (state == 1 || (state == 0 && now >= g_trace.start_ns)) {
        pm_lock(&g_trace.lock);
        if (g_trace.state == 0 && now >= g_trace.start_ns) {
            g_trace.state = 1;
            g_trace.start_ns = now;
            WARN("calltrace: counting indirect calls for %.1f s\n", WINDOW_NS / 1e9);
        }
        if (g_trace.state == 1) {
            if (now >= g_trace.start_ns + WINDOW_NS) {
                g_trace.end_ns = now;
                report();
                __atomic_store_n(&g_trace.state, 2, __ATOMIC_RELAXED);
            } else {
                u64 caller = c->x[30], phase = (u64)inkypaint_framerate_phase(), vt = readable(c->x[0], 8) ? gr64(c->x[0]) : 0;
                if (vt < g_code_lo || vt >= AS_SIZE) vt = 0;
                u64 h = (caller * 0x9e3779b97f4a7c15ull) ^ (entry * 0xc2b2ae3d27d4eb4full) ^ (vt * 0x165667b19e3779f9ull) ^ phase;
                u32 i = (u32)(h >> 40) & (EDGE_SLOTS - 1), k;
                for (k = 0; k < 64; k++, i = (i + 1) & (EDGE_SLOTS - 1)) {
                    TraceEdge* e = &g_trace.edges[i];
                    if (!e->count) {
                        e->caller = caller, e->target = entry, e->vtable = vt, e->phase = phase, e->count = 1;
                        e->last = c->x[0], e->objects = 1;
                        break;
                    }
                    if (e->caller == caller && e->target == entry && e->vtable == vt && e->phase == phase) {
                        e->count++;
                        if (e->last != c->x[0]) e->last = c->x[0], e->objects++;
                        break;
                    }
                }
                if (k == 64) g_trace.lost++;
            }
        }
        pm_unlock(&g_trace.lock);
    }
    original(entry)(c, entry);
}
/* direct calls reach a function through the table only once its compiled code is stale: the JIT compiles it again
   and callers' direct branches look it up in the table (the frame-rate hooks rely on the same). Done before any slot
   holds the wrapper: making a function stale clears only the slots that still hold its compiled code, and a stale
   function left in a slot sends its callers back to the slot, to the wrapper, to itself, until the stack ends */
static u64 recompile_range(u64 lo, u64 hi) {
    u64 n = 0;
    if (lo < g_code_lo) lo = g_code_lo;
    if (hi > g_code_hi) hi = g_code_hi;
    lo &= ~3ull;
    if (lo >= hi) return 0;
    u8* entry = calloc((size_t)((hi - lo) / 4), 1);
    if (!entry) fatal("calltrace: out of memory");
    for (u64 pc = lo; pc < hi; pc += 4) entry[(pc - lo) / 4] = g_disp[(pc - g_code_lo) / 4] != NULL;
    for (u64 pc = lo; pc < hi; pc += 4) {
        if (!entry[(pc - lo) / 4]) continue;
        if (g_disp[(pc - g_code_lo) / 4]) jit_code_changed(pc, 4);
        GuestFn fn = jit_wait(pc);
        if (!fn) continue;
        if (!g_disp[(pc - g_code_lo) / 4]) g_disp[(pc - g_code_lo) / 4] = fn;
        n++;
    }
    free(entry);
    return n;
}
void inkypaint_calltrace_init(void) {
    const char* at = getenv("INKYPAINT_CALL_TRACE");
    const char* direct = getenv("INKYPAINT_CALL_TRACE_DIRECT");
    u64 slots = (g_code_hi - g_code_lo) / 4, wrapped = 0, recompiled = 0;
    if (!at || !*at || inkypaint_any_title() || g_hle.kernel.title_id != INKYPAINT_TITLE_ID) return;
    g_trace.edges = calloc(EDGE_SLOTS, sizeof *g_trace.edges);
    if (!g_trace.fns || !g_trace.edges) fatal("calltrace: out of memory");
    g_trace.start_ns = strtoull(at, NULL, 10) * 1000000000ull;
    while (direct && *direct) {
        char* end;
        u64 lo = strtoull(direct, &end, 16), hi = lo;
        if (*end == '-') hi = strtoull(end + 1, &end, 16);
        recompiled += recompile_range(lo, hi);
        direct = *end == ',' ? end + 1 : NULL;
    }
    u64 count = 0, size = 1;
    for (u64 i = 0; i < slots; i++) count += g_disp[i] != NULL;
    while (size < count * 2) size <<= 1;
    g_trace.fn_mask = (u32)(size - 1);
    g_trace.fns = calloc((size_t)size, sizeof *g_trace.fns);
    if (!g_trace.fns) fatal("calltrace: out of memory");
    for (u64 i = 0; i < slots; i++) {
        GuestFn fn = g_disp[i];
        if (!fn) continue;
        u64 pc = g_code_lo + i * 4;
        u32 s = slot_of(pc);
        while (g_trace.fns[s].pc) s = (s + 1) & g_trace.fn_mask;
        g_trace.fns[s].pc = pc;
        g_trace.fns[s].fn = fn;
        g_disp[i] = wrapper;
        wrapped++;
    }
    WARN("calltrace: %llu functions wrapped (%llu of them recompiled for their direct calls), counting from %s s for "
         "2 s\n", (unsigned long long)wrapped, (unsigned long long)recompiled, at);
}
