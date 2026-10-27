/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* grid0.h with GRID0+ off, for the service tests that build acc.c, dns.c or ssl.c without grid0.c: every service then
   answers as it does offline */
#include "nx/hle/services/network/grid0.h"

void grid0_init(void) {}
int grid0_on(void) { return 0; }
int grid0_redirect(const char* host, uint8_t ip[4]) {
    (void)host, (void)ip;
    return 0;
}
int grid0_host_for(const uint8_t ip[4], char* host, size_t size) {
    (void)ip, (void)host, (void)size;
    return 0;
}
const unsigned char* grid0_ca(size_t* size) {
    *size = 0;
    return NULL;
}
int grid0_sign_in(void) { return -1; }
size_t grid0_id_token(char* out, size_t size) {
    if (size) out[0] = 0;
    return 0;
}
uint64_t grid0_account_id(void) { return 0; }

char* grid0_bcat_path(const char* directory, const char* file) { (void)directory, (void)file; return NULL; }
int grid0_bcat_sync(const char* directory) { (void)directory; return 0; }
