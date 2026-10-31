/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
#include "nx/runtime/inkypaint_presence_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char out[512], tiny[16];
    InkypaintPresence p = {"Vss_Hiagari04", 0, 1, 2, 7};
    size_t n = inkypaint_presence_json(out, sizeof out, 0x0100C2500FC20000ull, 4242, 1793404800000ull, &p);
    assert(n == strlen(out));
    assert(!strcmp(out, "{\"version\":1,\"title\":\"0100C2500FC20000\",\"pid\":4242,\"updated_ms\":1793404800000,"
                        "\"scene\":\"Vss_Hiagari04\",\"content\":0,\"rule\":1,\"mode\":2,\"stage_id\":7}\n"));
    InkypaintPresence unknown = {"", -1, -1, -1, -1};
    assert(inkypaint_presence_json(out, sizeof out, 1, 1, 0, &unknown));
    assert(strstr(out, "\"scene\":\"\",\"content\":-1,\"rule\":-1,\"mode\":-1,\"stage_id\":-1}"));
    InkypaintPresence odd = {"a\"b\\c\001d", 0, -1, -1, -1};
    assert(inkypaint_presence_json(out, sizeof out, 1, 1, 0, &odd));
    assert(strstr(out, "\"scene\":\"a\\\"b\\\\cd\""));
    assert(!inkypaint_presence_json(tiny, sizeof tiny, 1, 1, 0, &p));
    puts("PASS presence json: fields, unknowns, escaping and a buffer too small");
}
