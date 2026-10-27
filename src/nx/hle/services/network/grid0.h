/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* GRID0+, the community's replacement for the console's servers (a SwitchNet server), as inkypaint's services reach
   it (grid0.c). With grid0=1 in the .ini, a user name (grid0_user) and its password, the game's lookups of the
   console's server names answer with the server's address, the guest's TLS trusts the server's CA, the account
   service hands the game the id token the server signed, and the friend service shows the account's friends. Off,
   nothing here does anything and every service answers as it did. The state is g_hle.grid0 (HleGrid0, in network.h) */
#ifndef SNX_HLE_GRID0_H
#define SNX_HLE_GRID0_H
#include <stddef.h>
#include <stdint.h>

/* at startup, after the users: the settings, the password (the launcher's on stdin, else users/online.ini's), and a
   first sign-in in the background so the game's first ask finds a token */
void grid0_init(void);
int grid0_on(void);

/* ---- names (dns.c, ssl.c) */
/* 1 and the IPv4 address a lookup of host answers with, when host is one of the console's server domains; 0
   otherwise */
int grid0_redirect(const char* host, uint8_t ip[4]);
/* 1 and the console's server name last answered with ip, for a TLS connection the game names by its address */
int grid0_host_for(const uint8_t ip[4], char* host, size_t size);
/* the SwitchNet Local CA, the server's certificates' issuer: DER, or NULL (with *size 0) when GRID0+ is off */
const unsigned char* grid0_ca(size_t* size);

/* ---- the account (acc.c) */
/* signs in when there is no id token or it is about to expire, waiting for it: 0, or -1 with why logged */
int grid0_sign_in(void);
/* the id token, into out (size bytes, with its NUL): its length, 0 without one */
size_t grid0_id_token(char* out, size_t size);
/* the signed-in account's network service account id (the token's subject), 0 without one */
uint64_t grid0_account_id(void);

/* ---- BCAT delivery data (news and schedules): safe local cache paths, and a verified API sync. */
char* grid0_bcat_path(const char* directory, const char* file); /* caller frees, NULL for unsafe names */
int grid0_bcat_sync(const char* directory); /* NULL syncs all directories; -1 keeps the old cache on failure */

/* ---- friends (friends.c) */
typedef struct Grid0Friend Grid0Friend;
/* the cached friend list, refreshed in the background when stale: up to max into out, the count returned. wait_ms > 0
   waits that long for the session's first fetch, once a session: a game that asks once, early, keeps what it got */
uint32_t grid0_friends(Grid0Friend* out, uint32_t max, int wait_ms);
/* the profile picture (a JPEG) of friend nsa_id: its size, copied into out when it fits, 0 for none. Waits up to
   wait_ms for a first download */
uint32_t grid0_friend_image(uint64_t nsa_id, void* out, uint32_t size, int wait_ms);
/* this player's presence: nn::friends' status (0 offline, 1 online, 2 online play), the title, and the game's
   key-value blob (0xC0 bytes), sent in the background, again whenever it changes and every minute */
void grid0_presence(uint32_t status, uint64_t title_id, const uint8_t* field, size_t field_size);
#endif
