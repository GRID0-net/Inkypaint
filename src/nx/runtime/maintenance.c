/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "nx/runtime/maintenance.h"
#include "nx/runtime/rtapi.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/tracy.h"
#include "nx/gpu/renderer.h"

static struct {
    PMutex lock;
    PCond wake;
    int started, requested, stopping, done;
} g_maintenance = {.lock = PMUTEX_INIT};

static void maintenance_worker(void* arg) {
    (void)arg;
    SNX_THREAD_NAME("Cache persistence");
    pm_lock(&g_maintenance.lock);
    for (;;) {
        u64 began, elapsed;
        while (!g_maintenance.requested && !g_maintenance.stopping)
            pc_wait(&g_maintenance.wake, &g_maintenance.lock, -1);
        if (g_maintenance.stopping) break;
        g_maintenance.requested = 0;
        pm_unlock(&g_maintenance.lock);
        began = plat_ns();
        save_hints();
        if (g_renderer && g_renderer->save) g_renderer->save(0);
        elapsed = plat_ns() - began;
        if (elapsed >= 100000000ull)
            LOG("maintenance: cache persistence took %llu ms off the input thread\n",
                (unsigned long long)(elapsed / 1000000ull));
        pm_lock(&g_maintenance.lock);
    }
    g_maintenance.done = 1;
    pc_broadcast(&g_maintenance.wake);
    pm_unlock(&g_maintenance.lock);
}

void maintenance_request(void) {
    pm_lock(&g_maintenance.lock);
    if (!g_maintenance.stopping) {
        if (!g_maintenance.started) {
            pc_init(&g_maintenance.wake);
            if (plat_thread(maintenance_worker, NULL, (size_t)1 << 20)) {
                /* Preserve responsive input even if the OS cannot start a worker. Exit still saves. */
                WARN_LIMIT(1, "maintenance: cannot start cache worker; caches will save at exit\n");
                pm_unlock(&g_maintenance.lock);
                return;
            }
            g_maintenance.started = 1;
        }
        g_maintenance.requested = 1;
        pc_signal(&g_maintenance.wake);
    }
    pm_unlock(&g_maintenance.lock);
}

void maintenance_stop(void) {
    pm_lock(&g_maintenance.lock);
    g_maintenance.stopping = 1;
    if (g_maintenance.started) {
        pc_broadcast(&g_maintenance.wake);
        while (!g_maintenance.done) pc_wait(&g_maintenance.wake, &g_maintenance.lock, -1);
    }
    pm_unlock(&g_maintenance.lock);
}
