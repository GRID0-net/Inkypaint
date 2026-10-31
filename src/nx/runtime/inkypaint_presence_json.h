/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#ifndef INKYPAINT_PRESENCE_JSON_H
#define INKYPAINT_PRESENCE_JSON_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* What the runtime's <id>.presence.json says, for the launcher's Discord rich presence. Pure, so tests/ checks it:
   the guest memory reads are inkypaint_presence.c's. -1 is unknown for rule, mode and stage_id. */
typedef struct InkypaintPresence {
    char scene[65];
    int content, rule, mode, stage_id;
} InkypaintPresence;

/* The JSON, NUL-terminated in out. The length written, or 0 when it does not fit. */
static inline size_t inkypaint_presence_json(char* out, size_t size, uint64_t title, long pid, uint64_t unix_ms,
                                             const InkypaintPresence* p) {
    char scene[2 * sizeof p->scene];
    size_t n = 0;
    int len;
    for (size_t i = 0; p->scene[i] && i < sizeof p->scene - 1; i++) {
        unsigned char c = (unsigned char)p->scene[i];
        if (c < 32 || c > 126) continue; /* the reader only keeps printable ASCII */
        if (c == '"' || c == '\\') scene[n++] = '\\';
        scene[n++] = (char)c;
    }
    scene[n] = 0;
    len = snprintf(out, size,
                   "{\"version\":1,\"title\":\"%016llX\",\"pid\":%ld,\"updated_ms\":%llu,\"scene\":\"%s\","
                   "\"content\":%d,\"rule\":%d,\"mode\":%d,\"stage_id\":%d}\n",
                   (unsigned long long)title, pid, (unsigned long long)unix_ms, scene, p->content, p->rule, p->mode,
                   p->stage_id);
    return len > 0 && (size_t)len < size ? (size_t)len : 0;
}
#endif
