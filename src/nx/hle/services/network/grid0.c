/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors
   Ported from GRID0-Citrosis (GRID0-net), files of the citron Emulator Project (GPL-2.0-or-later):
     src/core/hle/service/acc/switchnet_account.cpp, the sign-in and its tokens
     src/core/hle/service/friend/grid0_friends.cpp, the friend list, pictures and presence
     src/core/hle/service/sockets/private_server.cpp, which names go to the server
     src/core/hle/service/ssl/switchnet_ca.h, the SwitchNet Local CA
     SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
   GRID0+ (grid0.h). Its server is dialled at its address and named by the console's host it stands in for: SwitchNet's
   edge routes on the TLS server name and the Host header. Every request is a connection of its own (Connection:
   close), verified against the SwitchNet Local CA alone: the sign-in carries the password, and nothing is sent to a
   server whose certificate doesn't check out. The password never reaches the log.

   Work the game shouldn't wait for (the friend list, pictures, the presence) is the worker's, one thread started at
   the first need, which sleeps on g_hle.grid0.cv. A sign-in is the caller's: the account service waits for its token,
   and grid0_init starts the first one on the worker so the game's first ask finds one */
#include "nx/hle/services/network/grid0.h"
#include "nx/hle/services/network/network.h"
#include "nx/hle/hle.h"
#include "nx/runtime/driver/hactool/cJSON.h"
#include "common/utilities/ini_lines.h"
#include "common/utilities/users.h"
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#define GRID0_DEFAULT_SERVER   "89.168.58.206"
#define GRID0_DEFAULT_NAT      "145.241.167.178"
/* the host a console signs in on (BAAS): /login and the emulator API are served there */
#define GRID0_BAAS_HOST        "e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com"
#define GRID0_TIMEOUT_S        10
#define GRID0_RESPONSE_MAX     (8u << 20)
#define GRID0_EXPIRY_MARGIN_S  300                     /* a token is renewed this long before it expires */
#define GRID0_RETRY_NS         (30ull * 1000000000ull) /* after a refused sign-in: the server limits them */
#define GRID0_REFRESH_NS       (20ull * 1000000000ull) /* a friend coming online shows this soon */
#define GRID0_PRESENCE_MIN_NS  (3ull * 1000000000ull)  /* however often the game sets it */
#define GRID0_PRESENCE_KEEP_NS (60ull * 1000000000ull) /* the server drops a presence after minutes without one */
#define GRID0_ONLINE_FILE      "online.ini"

/* The public certificate of the SwitchNet Local CA, the one switchnet-nro installs on a console, which signs a
   SwitchNet server's certificates for the console's server names. A certificate, not a key: it grants nothing on its
   own */
static const char grid0_ca_pem[] = "-----BEGIN CERTIFICATE-----\n"
                                   "MIIFUzCCAzugAwIBAgIUKALTNIY52NJHmZslx9FR6x3uCJQwDQYJKoZIhvcNAQEL\n"
                                   "BQAwMTEbMBkGA1UEAwwSU3dpdGNoTmV0IExvY2FsIENBMRIwEAYDVQQKDAlTd2l0\n"
                                   "Y2hOZXQwHhcNMjYwODI5MTYxOTAyWhcNMzYwODI2MTYxOTAyWjAxMRswGQYDVQQD\n"
                                   "DBJTd2l0Y2hOZXQgTG9jYWwgQ0ExEjAQBgNVBAoMCVN3aXRjaE5ldDCCAiIwDQYJ\n"
                                   "KoZIhvcNAQEBBQADggIPADCCAgoCggIBAKxiGpG1hj2ntGoulDQ0MKcxPHdfZIbQ\n"
                                   "u2JWvlJ3lJBT3bmxM6QfbJoSUp1q+PfYP8p/LDN6+otF1wDN+OKavnMVcbOLzdAL\n"
                                   "WMCkBq2w4rXfHVtR4qle3k3Av+2MvT5amUXh21lzpqRjf3CJ0jO3F2+ZAGvnf5cM\n"
                                   "jG4LOu1zaGoSHnUBCtFJlo04Nr/KKDQDr3NV+vE0Rmg4fCZ2WzM3jDx3HJJnovw+\n"
                                   "tA+06yqF0qrYcbuo3Xp7b8NDVv6oygw/l/ndwRMX7RIonFxc0KxRGNRHhjGhZG46\n"
                                   "Evw/tiz3FRCUO6kbYB7cfGz6WqoHiwbdlYmwHzFmEO+wUbgyPFaHpEj2RlP0Q58n\n"
                                   "3ZcgWYVWE0JVPbbIhyIBvG0KTPGz03pzKxqM56qz5po/+BXi1Lv75QAjLe/uBIEC\n"
                                   "Kj4ET51jy1gPw3IBWp50udftbKZaHbKzlbEvop89qL5YLyItVhq2FRV7fHaft3X4\n"
                                   "B6MoN0Z0IZmLt2pMo1iHOWcoEoii94VryS0BQi+2yvg1DaCpNI2+Kfo28qjqZMQM\n"
                                   "zJDmqQH1ceXxUNWAmwjOAJhRrDrf9G0aifUodGZeTyklNdNPyV++nIi9i47R4BQs\n"
                                   "GCH4+g5cpAicHX/a0GKyon5k1vhNsv1bvG1VdWPHr+DoTQUYWxKoO2POsygJoX3t\n"
                                   "pwpaiumgfpQRAgMBAAGjYzBhMB0GA1UdDgQWBBQcDW/S/H3CGXan5U5WMz82ZLf8\n"
                                   "sjAfBgNVHSMEGDAWgBQcDW/S/H3CGXan5U5WMz82ZLf8sjAPBgNVHRMBAf8EBTAD\n"
                                   "AQH/MA4GA1UdDwEB/wQEAwIBBjANBgkqhkiG9w0BAQsFAAOCAgEAkNDRdkiLNya/\n"
                                   "G4hXy9+wzup/LEnOY/ixInrqX8O2wP/N1iq1dTuhv0gZkGfk1VxIKa6nxyyjJksk\n"
                                   "pREcH3ZHNAGaoW/d0D33NjlE8PwzzGAcegF6Lk7Vo+ByEazdfOUvp62GOCqvCzHd\n"
                                   "bOyFQ4kzcaDU5Ou0zd9j75T1ornkCZWUA0GzWLHyi2hpLgA901jeii7AqmTdP4TD\n"
                                   "dO98cSJEbMoFY7Mvr5DI1/2FuGdpace23I2HXRy9LuXm4XLBNqAcj4ta3Ua1esV6\n"
                                   "TUzvTeewq33K4cAdjii2Lo5M9KktB36tpy2p7RlbkSK391fkaz07hhIRAV+SYyYu\n"
                                   "apyK2em6ZvYiCW2b5GcAELmkes/lks5sJQXq+uXOnS82/gVYlohCU4CZn3upC2La\n"
                                   "+k8Pu68fszaPXjLd+WeH1m4KxIvfXtMFzPehpUjSeoVNzufwiDMcvY+AE3q2mnmW\n"
                                   "bt8nn9Itds8E92RvBNgnZEjC7FLlOxrXbXk4j7IL4ISTeKx0BKBhhtCOJ2flxT6F\n"
                                   "ah61nwBiiyTFcJ2Z0H4YvaEjiBklPc5Jpat5q8ClFJ2WBfmpSNYjTNCTGlTm7TKt\n"
                                   "h2icuQYxjWBevUqrpjVZiRlJTuAZEpuXohi9M9gglFTiAbtr0+QeEGAxnlXBhYZi\n"
                                   "qLd+MwP25gjGF4afbc5D5HEhRO145t4=\n"
                                   "-----END CERTIFICATE-----\n";

/* The domains of the console's servers: SwitchNet's own list (internal/nintendo/hosts.go), so a console behind
   SwitchNet's resolver and this one agree on which names are the server's. Every name under them goes to the server,
   the ones it doesn't serve too: those then fail there rather than reach the console's own */
static const char* const grid0_domains[] = {
    "nintendo.com",        "nintendo.net",          "nintendo.jp",       "nintendo.co.jp", "nintendo.co.uk",
    "nintendo-europe.com", "nintendowifi.net",      "nintendo.es",       "nintendo.co.kr", "nintendo.tw",
    "nintendo.com.hk",     "nintendo.com.au",       "nintendo.co.nz",    "nintendo.at",    "nintendo.be",
    "nintendods.cz",       "nintendo.dk",           "nintendo.de",       "nintendo.fi",    "nintendo.fr",
    "nintendo.gr",         "nintendo.hu",           "nintendo.it",       "nintendo.nl",    "nintendo.no",
    "nintendo.pt",         "nintendo.ru",           "nintendo.co.za",    "nintendo.se",    "nintendo.ch",
    "nintendoswitch.com",  "nintendoswitch.com.cn", "nintendoswitch.cn",
};

static void worker_start(void);

/* ============================================================ setup */
int grid0_on(void) { return g_hle.grid0.on; }

/* host or host:port into host and the port (dflt without one): 0, or -1 */
static int split_server(const char* text, char* host, size_t size, u16* port, u16 dflt) {
    const char* colon = strrchr(text, ':');
    size_t n = colon ? (size_t)(colon - text) : strlen(text);
    *port = dflt;
    if (!n || n >= size || (colon && strchr(text, ':') != colon)) return -1;
    memcpy(host, text, n);
    host[n] = 0;
    if (colon) {
        char* end;
        unsigned long p = strtoul(colon + 1, &end, 10);
        if (!colon[1] || *end || !p || p > 65535) return -1;
        *port = (u16)p;
    }
    return 0;
}
/* host's IPv4 address, a number or a name the host's resolver knows: 0, or -1 */
static int address_of(const char* host, u8 ip[4]) {
    SnxAddrInfo hints = {0, SNX_AF_INET, SNX_SOCK_STREAM, 0, {0}, NULL, NULL}, *list = NULL;
    if (snx_net_parse_ipv4(host, ip)) return 0;
    if (snx_net_getaddrinfo(host, NULL, &hints, &list) || !list) return -1;
    memcpy(ip, list->addr.addr, 4);
    snx_net_freeaddrinfo(list);
    return 0;
}

/* online.ini's grid0_password in the users folder, into g_hle.grid0.password; the file holds secrets, so one other
   users of this machine can read is refused (room.c reads its room_password the same way) */
static void password_from_file(void) {
    char *dir = snx_users_dir(g_exedir[0] ? g_exedir : "."), *path = dir ? snx_path_join(dir, GRID0_ONLINE_FILE) : NULL;
    char *text = NULL, *key, *value;
    size_t length;
    SnxIni ini;
    if (!path || !snx_is_file(path)) goto done;
#ifndef _WIN32
    {
        struct stat st;
        if (stat(path, &st) || (st.st_mode & 077)) {
            WARN("GRID0+: %s can be read by other users of this machine, so its password isn't used: chmod 600 it\n",
                 path);
            goto done;
        }
    }
#endif
    text = (char*)snx_read_file(path, &length);
    if (!text) goto done;
    snx_ini_start(&ini, text);
    while (snx_ini_next(&ini, &key, &value))
        if (!ini.section[0] && !strcmp(key, "grid0_password") && strlen(value) < sizeof g_hle.grid0.password)
            strcpy(g_hle.grid0.password, value);
done:
    free(text);
    free(path);
    free(dir);
}
/* the launcher's: INKYPAINT_GRID0_STDIN=1 says the password is the first line on stdin, which it then closes. The
   variable goes, so a process the game starts doesn't wait on a stdin with nothing more */
static void password_from_launcher(void) {
    char line[sizeof g_hle.grid0.password + 2];
    const char* flag = getenv("INKYPAINT_GRID0_STDIN");
    size_t n;
    if (!flag || strcmp(flag, "1")) return;
#ifdef _WIN32
    _putenv("INKYPAINT_GRID0_STDIN=");
#else
    unsetenv("INKYPAINT_GRID0_STDIN");
#endif
    if (!fgets(line, sizeof line, stdin)) return;
    n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (n < sizeof g_hle.grid0.password) memcpy(g_hle.grid0.password, line, n + 1);
    OPENSSL_cleanse(line, sizeof line);
}

void grid0_init(void) {
    HleGrid0* g = &g_hle.grid0;
    const char* nat = g_settings.grid0_nat_address;
    BIO* bio;
    X509* ca;
    if (!g_settings.grid0) return;
    pc_init(&g->cv); /* used only while GRID0+ is on */
    password_from_launcher();
    if (!g->password[0]) password_from_file();
    if (!g_settings.network || g_settings.airplane_mode) {
        WARN("GRID0+ is on, but the game is offline (network=0 or airplane mode): no online play\n");
        return;
    }
    if (!g_settings.grid0_user[0] || !g->password[0]) {
        WARN("GRID0+ is on, but it has no %s: sign in from the launcher's Online page (or set grid0_user, and "
             "grid0_password in users/online.ini)\n",
             g_settings.grid0_user[0] ? "password" : "user name");
        return;
    }
    if (split_server(g_settings.grid0_server, g->server, sizeof g->server, &g->port, 443) ||
        address_of(g->server, g->ip)) {
        WARN("GRID0+: grid0_server=%s is no host or host:port this computer can resolve: no online play\n",
             g_settings.grid0_server);
        return;
    }
    if (!nat[0] && !strcmp(g->server, GRID0_DEFAULT_SERVER)) nat = GRID0_DEFAULT_NAT;
    memcpy(g->ip2, g->ip, 4);
    if (nat[0] && address_of(nat, g->ip2))
        WARN("GRID0+: grid0_nat_address=%s can't be resolved: Pia's NAT check goes to the server, and won't finish\n",
             nat);
    /* the CA, as DER for the ssl service's certificate list */
    bio = BIO_new_mem_buf(grid0_ca_pem, (int)sizeof grid0_ca_pem - 1);
    ca = bio ? PEM_read_bio_X509(bio, NULL, NULL, NULL) : NULL;
    if (ca) {
        int n = i2d_X509(ca, NULL);
        unsigned char* p = g->ca;
        if (n > 0 && (size_t)n <= sizeof g->ca && i2d_X509(ca, &p) == n) g->ca_size = (size_t)n;
    }
    X509_free(ca);
    BIO_free(bio);
    ERR_clear_error();
    if (!g->ca_size) fatal("GRID0+: the SwitchNet Local CA can't be read\n");
    g->on = 1;
    LOG("GRID0+: on, server %u.%u.%u.%u:%u, NAT check at %u.%u.%u.%u, user %s\n", g->ip[0], g->ip[1], g->ip[2],
        g->ip[3], g->port, g->ip2[0], g->ip2[1], g->ip2[2], g->ip2[3], g_settings.grid0_user);
    worker_start(); /* its first job is the sign-in */
}

/* ============================================================ names */
static int is_console_host(const char* name) {
    size_t n = strlen(name), i;
    while (n && name[n - 1] == '.') n--;
    for (i = 0; i < sizeof grid0_domains / sizeof grid0_domains[0]; i++) {
        size_t k = strlen(grid0_domains[i]);
        if (n >= k && !strncasecmp(name + n - k, grid0_domains[i], k) && (n == k || name[n - k - 1] == '.')) return 1;
    }
    return 0;
}
int grid0_redirect(const char* host, uint8_t ip[4]) {
    HleGrid0* g = &g_hle.grid0;
    int second;
    if (!g->on || !is_console_host(host)) return 0;
    /* Pia probes nncs1 and nncs2 as two servers, and one NAT check message must come back from another address:
       from the same one the console takes it for a duplicate, and matchmaking finds matches that never start */
    second = !strncasecmp(host, "nncs2", 5);
    memcpy(ip, second ? g->ip2 : g->ip, 4);
    pm_lock(&g->lk);
    snprintf(g->answered[second], sizeof g->answered[second], "%s", host);
    pm_unlock(&g->lk);
    LOG_LIMIT(64, "GRID0+: %s answered with %u.%u.%u.%u\n", host, ip[0], ip[1], ip[2], ip[3]);
    return 1;
}
int grid0_host_for(const uint8_t ip[4], char* host, size_t size) {
    HleGrid0* g = &g_hle.grid0;
    int i, found = 0;
    if (!g->on) return 0;
    pm_lock(&g->lk);
    for (i = 1; i >= 0 && !found; i--) /* the NAT address first: it is the server's own unless set */
        if (!memcmp(ip, i ? g->ip2 : g->ip, 4) && g->answered[i][0]) {
            snprintf(host, size, "%s", g->answered[i]);
            found = 1;
        }
    pm_unlock(&g->lk);
    return found;
}
const unsigned char* grid0_ca(size_t* size) {
    *size = g_hle.grid0.on ? g_hle.grid0.ca_size : 0;
    return *size ? g_hle.grid0.ca : NULL;
}

/* ============================================================ HTTPS */
/* the socket under OpenSSL, blocking within its timeouts */
static int bio_write(BIO* b, const char* data, size_t size, size_t* written) {
    int64_t r = snx_socket_send((SnxSocket*)BIO_get_data(b), data, size, 0, NULL);
    BIO_clear_retry_flags(b);
    if (r <= 0) return 0;
    *written = (size_t)r;
    return 1;
}
static int bio_read(BIO* b, char* data, size_t size, size_t* got) {
    int64_t r = snx_socket_recv((SnxSocket*)BIO_get_data(b), data, size, 0, NULL, NULL);
    BIO_clear_retry_flags(b);
    if (r <= 0) return 0;
    *got = (size_t)r;
    return 1;
}
static long bio_ctrl(BIO* b, int cmd, long num, void* ptr) {
    (void)b, (void)num, (void)ptr;
    return cmd == BIO_CTRL_FLUSH;
}
static int bio_create(BIO* b) {
    BIO_set_init(b, 1);
    return 1;
}
/* the context and BIO method, made once: 0, or -1 */
static int tls_setup(void) {
    HleGrid0* g = &g_hle.grid0;
    int rc = 0;
    pm_lock(&g->lk);
    if (!g->ctx) {
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        X509_STORE* store = ctx ? SSL_CTX_get_cert_store(ctx) : NULL;
        const unsigned char* p = g->ca;
        X509* ca = d2i_X509(NULL, &p, (long)g->ca_size);
        BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "grid0 socket");
        if (!ctx || !store || !ca || !m || X509_STORE_add_cert(store, ca) != 1 ||
            !BIO_meth_set_write_ex(m, bio_write) || !BIO_meth_set_read_ex(m, bio_read) ||
            !BIO_meth_set_ctrl(m, bio_ctrl) || !BIO_meth_set_create(m, bio_create)) {
            SSL_CTX_free(ctx);
            BIO_meth_free(m);
            rc = -1;
        } else {
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
            g->ctx = ctx;
            g->bio = m;
        }
        X509_free(ca);
        ERR_clear_error();
    }
    pm_unlock(&g->lk);
    return rc;
}

typedef struct Response {
    int status; /* the HTTP status, 0 when there was no answer */
    char* body; /* NUL-terminated, the caller's to free */
    size_t size;
    char why[160]; /* without an answer, why */
} Response;

static int ascii_prefix(const char* s, const char* prefix) { return !strncasecmp(s, prefix, strlen(prefix)); }
/* the head (status line and headers, ending at the blank line) and the body of a whole answer into r */
static void parse_answer(char* data, size_t size, Response* r) {
    char *end = NULL, *line, *body;
    size_t i, head;
    int chunked = 0;
    long long length = -1;
    for (i = 0; i + 3 < size; i++)
        if (!memcmp(data + i, "\r\n\r\n", 4)) {
            end = data + i;
            break;
        }
    if (!end || size < 12 || strncmp(data, "HTTP/1.", 7)) {
        snprintf(r->why, sizeof r->why, "the server's answer isn't HTTP");
        return;
    }
    *end = 0;
    head = (size_t)(end - data) + 4;
    r->status = atoi(data + 9);
    for (line = strstr(data, "\r\n"); line; line = strstr(line, "\r\n")) {
        line += 2;
        if (ascii_prefix(line, "transfer-encoding:") && strstr(line, "chunked")) chunked = 1;
        if (ascii_prefix(line, "content-length:")) length = atoll(line + 15);
    }
    body = data + head;
    size -= head;
    if (chunked) { /* the chunks' data moved together in place */
        size_t in = 0, out = 0;
        for (;;) {
            char* stop;
            unsigned long n = strtoul(body + in, &stop, 16);
            const char* crlf = (const char*)memchr(body + in, '\n', size - in);
            if (!crlf || stop == body + in) break;
            in = (size_t)(crlf - body) + 1;
            if (!n || n > size - in) break;
            memmove(body + out, body + in, n);
            out += n;
            in += n + 2;
            if (in > size) break;
        }
        size = out;
    } else if (length >= 0 && (size_t)length < size) {
        size = (size_t)length;
    }
    r->body = (char*)malloc(size + 1);
    if (!r->body) {
        r->status = 0;
        snprintf(r->why, sizeof r->why, "out of memory");
        return;
    }
    memcpy(r->body, body, size);
    r->body[size] = 0;
    r->size = size;
}
/* One request to host (one of the console's server names, which the server answers for) on the server, verified
   against the CA: the answer in *r (status 0 and why without one). auth is a bearer token or NULL, type the body's
   content type */
static void request(const char* host, const char* method, const char* path, const char* auth, const char* type,
                    const char* body, Response* r) {
    HleGrid0* g = &g_hle.grid0;
    SnxSocket* s = NULL;
    SnxSockAddr a;
    SnxTimeval tv = {GRID0_TIMEOUT_S, 0};
    SSL* ssl = NULL;
    BIO* bio;
    char* head = NULL;
    char* data = NULL;
    size_t size = 0, cap = 0, done;
    int e;
    memset(r, 0, sizeof *r);
    if (tls_setup()) {
        snprintf(r->why, sizeof r->why, "OpenSSL can't make a TLS context");
        return;
    }
    if ((e = snx_socket_open(&s, SNX_AF_INET, SNX_SOCK_STREAM, SNX_IPPROTO_TCP)) != 0) {
        snprintf(r->why, sizeof r->why, "no socket (error %d)", -e);
        return;
    }
    snx_socket_setsockopt(s, SNX_SOL_SOCKET, SNX_SO_RCVTIMEO, &tv, sizeof tv);
    snx_socket_setsockopt(s, SNX_SOL_SOCKET, SNX_SO_SNDTIMEO, &tv, sizeof tv);
    memset(&a, 0, sizeof a);
    a.family = SNX_AF_INET;
    ((u8*)&a.port)[0] = (u8)(g->port >> 8);
    ((u8*)&a.port)[1] = (u8)g->port;
    memcpy(a.addr, g->ip, 4);
    if ((e = snx_socket_connect(s, &a)) != 0) {
        snprintf(r->why, sizeof r->why, "%s:%u can't be reached (error %d)", g->server, g->port, -e);
        goto done;
    }
    ssl = SSL_new(g->ctx);
    bio = ssl ? BIO_new(g->bio) : NULL;
    if (!bio) {
        snprintf(r->why, sizeof r->why, "OpenSSL can't make a connection");
        goto done;
    }
    BIO_set_data(bio, s);
    SSL_set_bio(ssl, bio, bio);
    SSL_set_tlsext_host_name(ssl, host);
    SSL_set1_dnsname(ssl, host);
    if (SSL_connect(ssl) != 1) {
        long v = SSL_get_verify_result(ssl);
        snprintf(r->why, sizeof r->why, "the TLS handshake with %s failed%s%s", host, v != X509_V_OK ? ": " : "",
                 v != X509_V_OK ? X509_verify_cert_error_string(v) : "");
        goto done;
    }
    head = snx_format("%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: inkypaint\r\nAccept: application/json\r\n"
                      "Connection: close\r\n%s%s%s%s%s%sContent-Length: %u\r\n\r\n",
                      method, path, host, auth ? "Authorization: Bearer " : "", auth ? auth : "", auth ? "\r\n" : "",
                      type ? "Content-Type: " : "", type ? type : "", type ? "\r\n" : "",
                      (unsigned)(body ? strlen(body) : 0));
    if (!head || SSL_write_ex(ssl, head, strlen(head), &done) != 1 ||
        (body && *body && SSL_write_ex(ssl, body, strlen(body), &done) != 1)) {
        snprintf(r->why, sizeof r->why, "the request to %s couldn't be sent", host);
        goto done;
    }
    for (;;) { /* the whole answer: the server closes when it is done */
        if (size + 4096 + 1 > cap) {
            char* grown;
            cap = cap ? cap * 2 : 16384;
            if (cap > GRID0_RESPONSE_MAX || !(grown = (char*)realloc(data, cap))) {
                snprintf(r->why, sizeof r->why, "the answer from %s is too large", host);
                goto done;
            }
            data = grown;
        }
        if (SSL_read_ex(ssl, data + size, cap - size - 1, &done) != 1) break;
        size += done;
    }
    if (!size) {
        snprintf(r->why, sizeof r->why, "%s closed the connection without an answer", host);
        goto done;
    }
    data[size] = 0;
    parse_answer(data, size, r);
done:
    if (head && body) OPENSSL_cleanse(head, strlen(head));
    free(head);
    free(data);
    SSL_free(ssl); /* and the BIO */
    if (s) snx_socket_unref(s);
    ERR_clear_error();
}

/* ============================================================ the sign-in */
/* base64 or base64url text into out (size bytes, with a NUL): the bytes' count, or -1 */
static long base64_decode(const char* in, size_t n, unsigned char* out, size_t size) {
    unsigned buffer = 0;
    int bits = 0;
    size_t o = 0, i;
    for (i = 0; i < n; i++) {
        int c = (unsigned char)in[i], v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+' || c == '-')
            v = 62;
        else if (c == '/' || c == '_')
            v = 63;
        else if (c == '=')
            break;
        else
            continue;
        buffer = (buffer << 6 | (unsigned)v) & 0xFFFFFF;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o + 1 >= size) return -1;
            out[o++] = (unsigned char)(buffer >> bits);
        }
    }
    out[o] = 0;
    return (long)o;
}
/* a JWT's claims, read without its signature checked: the token came over a verified connection to be handed back
   to the server that signed it, and only when to renew it and whom it names are read */
static cJSON* jwt_claims(const char* jwt) {
    const char *a = strchr(jwt, '.'), *b = a ? strchr(a + 1, '.') : NULL;
    unsigned char text[GRID0_TOKEN_MAX];
    if (!b || base64_decode(a + 1, (size_t)(b - a - 1), text, sizeof text) < 0) return NULL;
    return cJSON_Parse((const char*)text);
}
static void form_escape(char* out, size_t size, const char* in) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < size; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = 0;
}
static const char* json_text(const cJSON* o, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}
/* POST /login with the user name and password: the tokens into g_hle.grid0, under sign_lk: 0, or -1 with
   g_hle.grid0.failure saying why */
static int sign_in_now(void) {
    HleGrid0* g = &g_hle.grid0;
    char user[3 * sizeof g_settings.grid0_user], password[3 * sizeof g->password], *form;
    const char *id, *access, *error;
    cJSON *json, *claims;
    Response r;
    i64 expiry = (i64)time(NULL) + 1800;
    u64 account = 0;
    form_escape(user, sizeof user, g_settings.grid0_user);
    form_escape(password, sizeof password, g->password);
    form = snx_format("username=%s&password=%s", user, password);
    OPENSSL_cleanse(password, sizeof password);
    request(GRID0_BAAS_HOST, "POST", "/login", NULL, "application/x-www-form-urlencoded", form, &r);
    if (form) OPENSSL_cleanse(form, strlen(form));
    free(form);
    json = r.body ? cJSON_Parse(r.body) : NULL;
    id = json_text(json, "idToken");
    access = json_text(json, "accessToken");
    error = json_text(json, "error");
    pm_lock(&g->lk);
    if (!r.status)
        snprintf(g->failure, sizeof g->failure, "%s", r.why);
    else if (r.status != 200)
        snprintf(g->failure, sizeof g->failure, "the server refused the sign-in (HTTP %d): %.160s", r.status,
                 error    ? error
                 : r.body ? r.body
                          : "");
    else if (!id || !*id || strlen(id) >= sizeof g->id_token)
        snprintf(g->failure, sizeof g->failure, "the server's answer to the sign-in has no id token");
    pm_unlock(&g->lk);
    if (r.status == 200 && id && *id && strlen(id) < sizeof g->id_token) {
        claims = jwt_claims(id);
        if (claims) {
            const cJSON* exp = cJSON_GetObjectItemCaseSensitive(claims, "exp");
            const char* sub = json_text(claims, "sub");
            if (cJSON_IsNumber(exp)) expiry = (i64)exp->valuedouble;
            if (sub && strlen(sub) <= 16 && strspn(sub, "0123456789abcdefABCDEF") == strlen(sub))
                account = strtoull(sub, NULL, 16);
            cJSON_Delete(claims);
        }
        pm_lock(&g->lk);
        strcpy(g->id_token, id);
        snprintf(g->access_token, sizeof g->access_token, "%s", access ? access : "");
        g->expiry = expiry;
        g->account_id = account;
        g->failed_ns = 0;
        pm_unlock(&g->lk);
        cJSON_Delete(json);
        free(r.body);
        WARN("GRID0+: signed in as %s\n", g_settings.grid0_user);
        return 0;
    }
    pm_lock(&g->lk);
    g->failed_ns = plat_ns();
    g->id_token[0] = g->access_token[0] = 0;
    pm_unlock(&g->lk);
    WARN("GRID0+: the sign-in failed: %s. The game gets no id token, and fails to go online (2321-4992)\n", g->failure);
    cJSON_Delete(json);
    free(r.body);
    return -1;
}
int grid0_sign_in(void) {
    HleGrid0* g = &g_hle.grid0;
    int rc = -1, fresh, cooling;
    if (!g->on) return -1;
    pm_lock(&g->sign_lk);
    pm_lock(&g->lk);
    fresh = g->id_token[0] && (i64)time(NULL) + GRID0_EXPIRY_MARGIN_S < g->expiry;
    cooling = g->failed_ns && plat_ns() - g->failed_ns < GRID0_RETRY_NS;
    pm_unlock(&g->lk);
    if (fresh)
        rc = 0;
    else if (cooling) /* a refused password, again on every call, would only be refused again: the server limits them */
        LOG_LIMIT(8, "GRID0+: no sign-in again yet, the last failed: %s\n", g->failure);
    else
        rc = sign_in_now();
    pm_unlock(&g->sign_lk);
    return rc;
}
size_t grid0_id_token(char* out, size_t size) {
    size_t n;
    pm_lock(&g_hle.grid0.lk);
    n = strlen(g_hle.grid0.id_token);
    if (n >= size) n = 0;
    memcpy(out, g_hle.grid0.id_token, n);
    out[n] = 0;
    pm_unlock(&g_hle.grid0.lk);
    return n;
}
u64 grid0_account_id(void) {
    u64 id;
    pm_lock(&g_hle.grid0.lk);
    id = g_hle.grid0.account_id;
    pm_unlock(&g_hle.grid0.lk);
    return id;
}

/* ============================================================ the emulator API */
/* a request to the emulator API (/emulator/v1/...) with the access token, signing in first when needed */
static void api(const char* method, const char* path, const char* json, Response* r) {
    char token[GRID0_TOKEN_MAX];
    memset(r, 0, sizeof *r);
    if (grid0_sign_in()) {
        snprintf(r->why, sizeof r->why, "not signed in");
        return;
    }
    pm_lock(&g_hle.grid0.lk);
    strcpy(token, g_hle.grid0.access_token);
    pm_unlock(&g_hle.grid0.lk);
    if (!token[0]) {
        snprintf(r->why, sizeof r->why, "the sign-in gave no access token");
        return;
    }
    request(GRID0_BAAS_HOST, method, path, token, json ? "application/json" : NULL, json, r);
    if (r->status && (r->status < 200 || r->status >= 300))
        LOG_LIMIT(16, "GRID0+: %s %s: HTTP %d\n", method, path, r->status);
}

/* a friend of the API's list ({id, nickname, login, state, appId, appField, imageUrl, isFavorite}) into f: 0, or -1 */
static int parse_friend(const cJSON* j, Grid0Friend* f) {
    const char *id = json_text(j, "id"), *nick = json_text(j, "nickname"), *login = json_text(j, "login");
    const char *state = json_text(j, "state"), *app = json_text(j, "appId"), *field = json_text(j, "appField");
    const char* image = json_text(j, "imageUrl");
    unsigned char bytes[GRID0_FIELD_MAX + 4];
    long n;
    memset(f, 0, sizeof *f);
    if (!id || !(f->nsa_id = strtoull(id, NULL, 16))) return -1;
    snprintf(f->nickname, sizeof f->nickname, "%s", nick && *nick ? nick : login ? login : "");
    f->status = state && !strcmp(state, "PLAYING") ? 2 : state && !strcmp(state, "ONLINE") ? 1 : 0;
    f->title_id = app && *app ? strtoull(app, NULL, 16) : 0;
    f->favorite = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "isFavorite"));
    if (field && (n = base64_decode(field, strlen(field), bytes, sizeof bytes)) > 0) {
        f->field_size = (u32)(n < GRID0_FIELD_MAX ? n : GRID0_FIELD_MAX);
        memcpy(f->field, bytes, f->field_size);
    }
    snprintf(f->image_url, sizeof f->image_url, "%s", image ? image : "");
    return 0;
}
/* GET /emulator/v1/friends into the cache, on the worker */
static void fetch_friends(void) {
    HleGrid0* g = &g_hle.grid0;
    Grid0Friend* list = (Grid0Friend*)malloc(GRID0_FRIENDS_MAX * sizeof *list);
    Response r;
    cJSON *json, *friends, *e;
    u32 n = 0;
    int ok = 0;
    api("GET", "/emulator/v1/friends", NULL, &r);
    json = r.status == 200 && r.body ? cJSON_Parse(r.body) : NULL;
    friends = cJSON_GetObjectItemCaseSensitive(json, "friends");
    if (cJSON_IsArray(friends) && list) {
        cJSON_ArrayForEach(e, friends) if (n < GRID0_FRIENDS_MAX && !parse_friend(e, &list[n])) n++;
        ok = 1;
    } else if (!r.status) {
        LOG_LIMIT(8, "GRID0+: the friend list: %s\n", r.why);
    }
    cJSON_Delete(json);
    free(r.body);
    pm_lock(&g->lk);
    if (ok) {
        if (!g->have_friends || n != g->friend_count) LOG("GRID0+: %u friends\n", n);
        memcpy(g->friends, list, n * sizeof *list);
        g->friend_count = n;
        g->have_friends = 1;
    }
    g->fetched_ns = plat_ns();
    g->fetches++;
    pc_broadcast(&g->cv);
    pm_unlock(&g->lk);
    free(list);
}
/* a picture's https URL on a console server's host, which the server answers for, fetched into the image's slot */
static void fetch_image(u64 nsa_id, const char* url) {
    HleGrid0* g = &g_hle.grid0;
    char host[256];
    const char *rest, *slash;
    Response r;
    int i;
    memset(&r, 0, sizeof r);
    if (!strncmp(url, "https://", 8)) {
        rest = url + 8;
        slash = strchr(rest, '/');
        snprintf(host, sizeof host, "%.*s", slash ? (int)(slash - rest) : (int)strlen(rest), rest);
        if (is_console_host(host)) request(host, "GET", slash ? slash : "/", NULL, NULL, NULL, &r);
    }
    pm_lock(&g->lk);
    for (i = 0; i < GRID0_IMAGES_MAX; i++)
        if (g->images[i].state == 1 && g->images[i].nsa_id == nsa_id) {
            if (r.status == 200 && r.size && r.size <= GRID0_IMAGE_MAX) {
                g->images[i].jpeg = (unsigned char*)r.body;
                g->images[i].size = (u32)r.size;
                g->images[i].state = 2;
                r.body = NULL;
            } else {
                g->images[i].state = 3;
            }
        }
    pc_broadcast(&g->cv);
    pm_unlock(&g->lk);
    free(r.body);
}

/* ============================================================ the worker */
static void worker(void* arg) {
    HleGrid0* g = &g_hle.grid0;
    (void)arg;
    grid0_sign_in(); /* the first: the game's own then finds a token */
    pm_lock(&g->lk);
    for (;;) {
        u64 now = plat_ns(), image = 0;
        int friends = g->want_friends && (!g->fetched_ns || now - g->fetched_ns > GRID0_REFRESH_NS);
        int presence = g->presence[0] && (!g->presence_ns || now - g->presence_ns > GRID0_PRESENCE_MIN_NS) &&
                       (strcmp(g->presence, g->presence_sent) || now - g->presence_ns > GRID0_PRESENCE_KEEP_NS);
        char url[256], body[sizeof g->presence];
        int i;
        url[0] = 0;
        for (i = 0; i < GRID0_IMAGES_MAX && !image; i++)
            if (g->images[i].state == 1) image = g->images[i].nsa_id;
        for (i = 0; image && i < (int)g->friend_count; i++) /* the friend's picture's URL, from the list */
            if (g->friends[i].nsa_id == image) snprintf(url, sizeof url, "%s", g->friends[i].image_url);
        if (!friends && !presence && !image) {
            pc_wait(&g->cv, &g->lk, 5000000000ll);
            continue;
        }
        if (presence) strcpy(body, g->presence);
        pm_unlock(&g->lk);
        if (friends) fetch_friends();
        if (image) fetch_image(image, url);
        if (presence) {
            Response r;
            api("POST", "/emulator/v1/presence", body, &r);
            free(r.body);
            pm_lock(&g->lk);
            g->presence_ns = plat_ns();
            if (r.status >= 200 && r.status < 300) strcpy(g->presence_sent, body);
            pm_unlock(&g->lk);
        }
        pm_lock(&g->lk);
    }
}
static void worker_start(void) {
    HleGrid0* g = &g_hle.grid0;
    int start;
    pm_lock(&g->lk);
    start = !g->worker;
    g->worker = 1;
    pm_unlock(&g->lk);
    if (start && plat_thread(worker, NULL, 0)) {
        WARN("GRID0+: no thread for the friend list and the presence\n");
        pm_lock(&g->lk);
        g->worker = 0;
        pm_unlock(&g->lk);
    }
}

/* ============================================================ friends */
u32 grid0_friends(Grid0Friend* out, u32 max, int wait_ms) {
    HleGrid0* g = &g_hle.grid0;
    u32 n;
    if (!g->on) return 0;
    pm_lock(&g->lk);
    if (!g->want_friends) {
        g->want_friends = 1;
        pc_broadcast(&g->cv);
    }
    if (wait_ms > 0 && !g->have_friends && !g->warm_waited) {
        u64 before = g->fetches, deadline = plat_ns() + (u64)wait_ms * 1000000u;
        g->warm_waited = 1;
        while (g->fetches == before && plat_ns() < deadline) pc_wait(&g->cv, &g->lk, (i64)(deadline - plat_ns()));
    }
    n = g->friend_count < max ? g->friend_count : max;
    memcpy(out, g->friends, n * sizeof *out);
    pm_unlock(&g->lk);
    return n;
}
u32 grid0_friend_image(u64 nsa_id, void* out, u32 size, int wait_ms) {
    HleGrid0* g = &g_hle.grid0;
    u64 deadline = plat_ns() + (u64)(wait_ms > 0 ? wait_ms : 0) * 1000000u;
    u32 got = 0;
    int i, slot = -1;
    if (!g->on) return 0;
    pm_lock(&g->lk);
    for (i = 0; i < GRID0_IMAGES_MAX && slot < 0; i++)
        if (g->images[i].state && g->images[i].nsa_id == nsa_id) slot = i;
    if (slot < 0) { /* a free slot, else the first one's, its picture let go */
        for (i = 0; i < GRID0_IMAGES_MAX && slot < 0; i++)
            if (!g->images[i].state) slot = i;
        if (slot < 0) {
            slot = (int)(nsa_id % GRID0_IMAGES_MAX);
            if (g->images[slot].state == 1) slot = -1; /* a download in progress keeps its slot */
        }
        if (slot >= 0) {
            free(g->images[slot].jpeg);
            memset(&g->images[slot], 0, sizeof g->images[slot]);
            g->images[slot].nsa_id = nsa_id;
            g->images[slot].state = 1;
            pc_broadcast(&g->cv);
        }
    }
    while (slot >= 0 && g->images[slot].state == 1 && g->images[slot].nsa_id == nsa_id && plat_ns() < deadline)
        pc_wait(&g->cv, &g->lk, (i64)(deadline - plat_ns()));
    if (slot >= 0 && g->images[slot].state == 2 && g->images[slot].nsa_id == nsa_id && g->images[slot].size <= size) {
        got = g->images[slot].size;
        memcpy(out, g->images[slot].jpeg, got);
    }
    pm_unlock(&g->lk);
    return got;
}
void grid0_presence(u32 status, u64 title_id, const u8* field, size_t field_size) {
    HleGrid0* g = &g_hle.grid0;
    char encoded[4 * ((GRID0_FIELD_MAX + 2) / 3) + 1], app[17] = "";
    const char* state = status == 2 ? "PLAYING" : status == 1 ? "ONLINE" : "OFFLINE";
    if (!g->on) return;
    if (field_size > GRID0_FIELD_MAX) field_size = GRID0_FIELD_MAX;
    while (field_size && !field[field_size - 1]) field_size--; /* the storage's unused end, not data */
    EVP_EncodeBlock((unsigned char*)encoded, field, (int)field_size);
    if (title_id) snprintf(app, sizeof app, "%016llx", (unsigned long long)title_id);
    pm_lock(&g->lk);
    snprintf(g->presence, sizeof g->presence, "{\"state\":\"%s\",\"appId\":\"%s\",\"appField\":\"%s\"}", state, app,
             encoded);
    pc_broadcast(&g->cv);
    pm_unlock(&g->lk);
}
