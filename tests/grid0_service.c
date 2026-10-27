/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the inkypaint contributors */
/* GRID0+ (grid0.c) and the friend service on it (friends.c), against a server in a thread of the test on the host's
   loopback that answers as GRID0+'s does (its /login, /emulator/v1/friends and /presence, and a picture). Its
   certificates come from a CA made here, which stands in for the SwitchNet Local CA: the client must verify the
   server's name, present the console's host as the server name, sign in with the form escaped, read the token's
   subject and expiry, read a chunked answer, and send the presence. A server whose certificate names another host is
   refused */
#include "nx/hle/services/network/network.h"
#include <stdarg.h>
#include <time.h>

#include "nx/hle/services/network/grid0.c"
#include "nx/hle/services/network/friends.c"
#include "nx/hle/hle.c"

#include <pthread.h>

u8* g_mem;
char g_exedir[1024], g_exename[256], g_datadir[1024];
static void test_fail(const char* expression, int line) {
    fprintf(stderr, "grid0_service.c:%d: check failed: %s\n", line, expression);
    exit(1);
}
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) test_fail(#expr, __LINE__);                                                                       \
    } while (0)

/* ============================================================ the platform, for grid0.c */
void pm_init(PMutex* m) { CHECK(!pthread_mutex_init(&m->l, NULL)); }
void pm_lock(PMutex* m) { CHECK(!pthread_mutex_lock(&m->l)); }
void pm_unlock(PMutex* m) { CHECK(!pthread_mutex_unlock(&m->l)); }
void pc_init(PCond* c) { CHECK(!pthread_cond_init(&c->c, NULL)); }
void pc_signal(PCond* c) { pthread_cond_signal(&c->c); }
void pc_broadcast(PCond* c) { pthread_cond_broadcast(&c->c); }
u64 plat_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u64)t.tv_sec * 1000000000 + (u64)t.tv_nsec;
}
int pc_wait(PCond* c, PMutex* m, i64 timeout_ns) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += timeout_ns / 1000000000;
    t.tv_nsec += timeout_ns % 1000000000;
    if (t.tv_nsec >= 1000000000) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000;
    }
    return pthread_cond_timedwait(&c->c, &m->l, &t);
}
typedef struct {
    PThreadFn fn;
    void* arg;
} Start;
static void* start(void* p) {
    Start s = *(Start*)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}
int plat_thread(PThreadFn fn, void* arg, size_t stack) {
    pthread_t t;
    Start* s = malloc(sizeof *s);
    (void)stack;
    s->fn = fn;
    s->arg = arg;
    if (pthread_create(&t, NULL, start, s)) return -1;
    pthread_detach(t);
    return 0;
}
void logf_(int level, const char* format, ...) {
    va_list a;
    if (level) return;
    va_start(a, format);
    vfprintf(stderr, format, a);
    va_end(a);
}
void fatal(const char* format, ...) {
    va_list a;
    va_start(a, format);
    vfprintf(stderr, format, a);
    va_end(a);
    exit(1);
}

/* ============================================================ the IPC around friends.c */
SvcObj* svcobj_new(const char* name, int (*dispatch)(SvcObj*, IpcCtx*)) {
    SvcObj* obj = calloc(1, sizeof *obj);
    CHECK(obj != NULL);
    obj->name = name;
    obj->dispatch = dispatch;
    obj->refs = 1;
    return obj;
}
#include "svcobj_data.h"
void svcobj_unref(SvcObj* obj) {
    if (!obj || --obj->refs) return;
    if (obj->destroy) obj->destroy(obj);
    test_svcobj_data_free(obj);
    free(obj);
}
void out_obj(IpcCtx* c, SvcObj* obj) { c->out_objs[c->nout_objs++] = obj; }
void out_copy_h(IpcCtx* c, u32 handle) { c->out_copy[c->nout_copy++] = handle; }
void ipc_out_overflow(IpcCtx* c) {
    (void)c;
    test_fail("IPC output overflow", __LINE__);
}
int ipc_warn_once(const char* name, u32 cmd) {
    (void)name, (void)cmd;
    return 0;
}
int ipc_bad_request(SvcObj* o, IpcCtx* c) {
    (void)o, (void)c;
    test_fail("a request that doesn't match its declaration", __LINE__);
    return -1;
}
int ipc_bad_objects(SvcObj* o, IpcCtx* c) {
    (void)o, (void)c;
    return -1;
}
int stub_ok(SvcObj* o, IpcCtx* c) {
    (void)o, (void)c;
    return 0;
}
void kobj_ref(KObj* o) { o->refs++; }
void kobj_unref(KObj* o) {
    if (!--o->refs) free(o);
}
KEvent* kevent_new(const char* name) {
    KEvent* e = calloc(1, sizeof *e);
    CHECK(e != NULL);
    e->o.name = name;
    e->o.refs = 1;
    return e;
}
static int signals;
void kevent_signal(KEvent* e) {
    e->signaled = 1;
    signals++;
}
u32 ev_handle(KEvent* e) {
    (void)e;
    return 0x1234;
}
int new_event(u32* event, const char* name, int signaled) {
    (void)name, (void)signaled;
    *event = 0x1235;
    return 0;
}
int kobj_never_signaled(KObj* o) {
    (void)o;
    return 0;
}
KEvent* owned_event(const char* name) {
    KEvent* e = kevent_new(name);
    kobj_ref(&e->o);
    return e;
}

/* ============================================================ the CA and the server's certificates */
static EVP_PKEY* key;
static X509* ca;
static X509* make_cert(const char* cn, const char* san, X509* issuer) {
    X509* x = X509_new();
    X509_NAME* name = X509_NAME_new();
    X509V3_CTX v3;
    X509_EXTENSION* ext;
    CHECK(x && name && X509_set_version(x, X509_VERSION_3));
    ASN1_INTEGER_set(X509_get_serialNumber(x), issuer ? 7 : 1);
    X509_gmtime_adj(X509_getm_notBefore(x), -3600);
    X509_gmtime_adj(X509_getm_notAfter(x), 86400);
    CHECK(X509_set_pubkey(x, key));
    CHECK(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)cn, -1, -1, 0));
    CHECK(X509_set_subject_name(x, name));
    CHECK(X509_set_issuer_name(x, issuer ? X509_get_subject_name(issuer) : name));
    X509_NAME_free(name);
    X509V3_set_ctx(&v3, issuer ? issuer : x, x, NULL, NULL, 0);
    ext = issuer ? X509V3_EXT_conf_nid(NULL, &v3, NID_subject_alt_name, san)
                 : X509V3_EXT_conf_nid(NULL, &v3, NID_basic_constraints, "critical,CA:TRUE");
    CHECK(ext && X509_add_ext(x, ext, -1));
    X509_EXTENSION_free(ext);
    CHECK(X509_sign(x, key, EVP_sha256()) > 0);
    return x;
}

/* ============================================================ the server */
static SnxSocket* listener;
static X509* server_cert;
static char sni[256], presence[1024], login_form[256], friends_auth[256];
static int presences;
static int s_write(BIO* b, const char* d, size_t n, size_t* w) {
    int64_t r = snx_socket_send(BIO_get_data(b), d, n, 0, NULL);
    BIO_clear_retry_flags(b);
    if (r <= 0) return 0;
    *w = (size_t)r;
    return 1;
}
static int s_read(BIO* b, char* d, size_t n, size_t* got) {
    int64_t r = snx_socket_recv(BIO_get_data(b), d, n, 0, NULL, NULL);
    BIO_clear_retry_flags(b);
    if (r <= 0) return 0;
    *got = (size_t)r;
    return 1;
}
static long s_ctrl(BIO* b, int cmd, long num, void* ptr) {
    (void)b, (void)num, (void)ptr;
    return cmd == BIO_CTRL_FLUSH;
}
static int s_create(BIO* b) {
    BIO_set_init(b, 1);
    return 1;
}
static char* b64url(const char* text) {
    static char out[512];
    size_t n = (size_t)EVP_EncodeBlock((unsigned char*)out, (const unsigned char*)text, (int)strlen(text)), i;
    for (i = 0; i < n; i++) out[i] = out[i] == '+' ? '-' : out[i] == '/' ? '_' : out[i];
    while (n && out[n - 1] == '=') out[--n] = 0;
    return out;
}
static void answer(SSL* ssl, const char* request, const char* body) {
    char reply[4096], payload[256], jwt[1024];
    const char* friends = "{\"friends\":[{\"id\":\"00000000000000a1\",\"nickname\":\"Callie\",\"login\":\"callie\","
                          "\"friendCode\":\"SW-1111-2222-3333\",\"state\":\"PLAYING\",\"appId\":\"0100c2500fc20000\","
                          "\"appField\":\"AQID\",\"imageUrl\":\"https://cdn.example.nintendo.net/p/a1.jpg\","
                          "\"isFavorite\":true},{\"id\":\"00000000000000b2\",\"nickname\":\"\",\"login\":\"marie\","
                          "\"state\":\"\",\"appId\":\"\",\"imageUrl\":\"\"}]}";
    size_t w;
    if (!strncmp(request, "POST /login ", 12)) {
        snprintf(login_form, sizeof login_form, "%s", body);
        snprintf(payload, sizeof payload, "{\"sub\":\"00000000deadbeef\",\"exp\":%lld}", (long long)time(NULL) + 3600);
        snprintf(jwt, sizeof jwt, "eyJhbGciOiJIUzI1NiJ9.%s.c2ln", b64url(payload));
        snprintf(reply, sizeof reply,
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %u\r\n\r\n"
                 "{\"idToken\":\"%s\",\"accessToken\":\"acc123\"}",
                 (unsigned)(strlen(jwt) + 40), jwt);
    } else if (!strncmp(request, "GET /emulator/v1/friends ", 25)) {
        const char* auth = strstr(request, "Authorization: ");
        size_t half = strlen(friends) / 2;
        snprintf(friends_auth, sizeof friends_auth, "%.*s", auth ? (int)strcspn(auth, "\r") : 0, auth ? auth : "");
        /* chunked, in two chunks */
        snprintf(reply, sizeof reply,
                 "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%zx\r\n%.*s\r\n%zx\r\n%s\r\n0\r\n\r\n", half,
                 (int)half, friends, strlen(friends) - half, friends + half);
    } else if (!strncmp(request, "GET /emulator/v1/bcat/0100c2500fc20000/News/test ", 45)) {
        snprintf(reply, sizeof reply, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nnews");
    } else if (!strncmp(request, "GET /emulator/v1/bcat/0100c2500fc20000 ", 36)) {
        const char* manifest = "{\"directories\":[{\"name\":\"News\",\"files\":[{\"name\":\"test\","
            "\"sha256\":\"19fba0e995b9794fc2c26217bf3b725c2f0d9eeda16719fe75e3ba23ca73bfc4\"}]}]}";
        snprintf(reply, sizeof reply, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s", strlen(manifest), manifest);
    } else if (!strncmp(request, "POST /emulator/v1/presence ", 27)) {
        snprintf(presence, sizeof presence, "%s", body);
        presences++;
        snprintf(reply, sizeof reply, "HTTP/1.1 204 No Content\r\n\r\n");
    } else if (!strncmp(request, "GET /p/a1.jpg ", 14)) {
        snprintf(reply, sizeof reply, "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\n\xFF\xD8jpeg");
    } else {
        snprintf(reply, sizeof reply, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
    }
    SSL_write_ex(ssl, reply, strlen(reply), &w);
}
static void* server(void* arg) {
    BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "test server");
    (void)arg;
    CHECK(m != NULL);
    BIO_meth_set_write_ex(m, s_write);
    BIO_meth_set_read_ex(m, s_read);
    BIO_meth_set_ctrl(m, s_ctrl);
    BIO_meth_set_create(m, s_create);
    for (;;) {
        SnxSocket* s;
        SnxSockAddr peer;
        SSL* ssl;
        BIO* bio;
        char request[8192];
        size_t n = 0, got;
        const char *end, *length;
        SSL_CTX* ctx;
        if (snx_socket_accept(listener, &s, &peer, 0)) break;
        ctx = SSL_CTX_new(TLS_server_method()); /* the certificate the test holds now */
        CHECK(ctx && SSL_CTX_use_certificate(ctx, server_cert) && SSL_CTX_use_PrivateKey(ctx, key));
        ssl = SSL_new(ctx);
        bio = BIO_new(m);
        BIO_set_data(bio, s);
        SSL_set_bio(ssl, bio, bio);
        if (SSL_accept(ssl) == 1) {
            const char* name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
            snprintf(sni, sizeof sni, "%s", name ? name : "");
            /* the head, then the body its Content-Length says */
            while (n + 1 < sizeof request && SSL_read_ex(ssl, request + n, sizeof request - n - 1, &got) == 1) {
                n += got;
                request[n] = 0;
                if ((end = strstr(request, "\r\n\r\n")) != NULL) {
                    length = strstr(request, "Content-Length: ");
                    if (!length || n >= (size_t)(end - request) + 4 + (size_t)atoi(length + 16)) break;
                }
            }
            request[n] = 0;
            end = strstr(request, "\r\n\r\n");
            if (end) answer(ssl, request, end + 4);
            SSL_shutdown(ssl);
        }
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        snx_socket_unref(s);
    }
    return NULL;
}

int main(void) {
    SnxSockAddr lo;
    pthread_t thread;
    Grid0Friend list[4];
    char token[GRID0_TOKEN_MAX], jpeg[16];
    unsigned char* p;
    u8 ip[4];
    size_t size;
    hle_init();
    g_mem = calloc(1, 0x40000);
    CHECK(g_mem != NULL);
    CHECK(snx_net_init() == 0);
    snx_net_set_log(NULL);
    key = EVP_EC_gen("P-256");
    CHECK(key != NULL);
    ca = make_cert("Test Local CA", NULL, NULL);
    server_cert = make_cert(GRID0_BAAS_HOST, "DNS:" GRID0_BAAS_HOST ",DNS:*.example.nintendo.net", ca);

    CHECK(snx_socket_open(&listener, SNX_AF_INET, SNX_SOCK_STREAM, 0) == 0);
    memset(&lo, 0, sizeof lo);
    lo.family = SNX_AF_INET;
    lo.addr[0] = 127;
    lo.addr[3] = 1;
    CHECK(snx_socket_bind(listener, &lo) == 0);
    CHECK(snx_socket_listen(listener, 8) == 0);
    CHECK(snx_socket_name(listener, &lo) == 0);
    CHECK(!pthread_create(&thread, NULL, server, NULL));

    /* GRID0+ on, as grid0_init leaves it, with this CA for the SwitchNet one and the loopback for the server */
    pc_init(&g_hle.grid0.cv);
    g_hle.grid0.on = 1;
    memcpy(g_hle.grid0.ip, lo.addr, 4);
    memcpy(g_hle.grid0.ip2, lo.addr, 4);
    g_hle.grid0.ip2[3] = 2;
    g_hle.grid0.port = (u16)(((u8*)&lo.port)[0] << 8 | ((u8*)&lo.port)[1]);
    strcpy(g_hle.grid0.server, "127.0.0.1");
    strcpy(g_hle.grid0.password, "p@ss word&=");
    strcpy(g_settings.grid0_user, "tester");
    p = g_hle.grid0.ca;
    g_hle.grid0.ca_size = (size_t)i2d_X509(ca, &p);

    /* names: the console's servers to the server, nncs2 to the second address, the rest left alone */
    CHECK(grid0_redirect("e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com", ip) && ip[3] == 1);
    CHECK(grid0_redirect("nncs2-lp1.n.n.srv.nintendo.net.", ip) && ip[3] == 2);
    CHECK(!grid0_redirect("example.com", ip) && !grid0_redirect("notnintendo.com", ip));
    CHECK(grid0_host_for(lo.addr, token, sizeof token) &&
          !strcmp(token, "e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com"));
    CHECK(grid0_ca(&size) == g_hle.grid0.ca && size == g_hle.grid0.ca_size);

    /* the sign-in: the form escaped, the console's host as the server name, the token's subject the account */
    CHECK(grid0_sign_in() == 0);
    CHECK(!strcmp(login_form, "username=tester&password=p%40ss%20word%26%3D"));
    CHECK(!strcmp(sni, GRID0_BAAS_HOST));
    CHECK(grid0_account_id() == 0xdeadbeef);
    CHECK(grid0_id_token(token, sizeof token) > 20 && !strncmp(token, "eyJhbGciOiJIUzI1NiJ9.", 21));
    CHECK(g_hle.grid0.expiry > (i64)time(NULL) + 3000);
    CHECK(grid0_sign_in() == 0); /* fresh: no second request */

    /* the friend list, the session's first waited for, chunked; the bearer token the access token */
    worker_start();
    CHECK(grid0_friends(list, 4, 5000) == 2);
    CHECK(!strcmp(friends_auth, "Authorization: Bearer acc123"));
    CHECK(list[0].nsa_id == 0xa1 && !strcmp(list[0].nickname, "Callie") && list[0].status == 2);
    CHECK(list[0].title_id == 0x0100c2500fc20000ull && list[0].favorite && list[0].field_size == 3);
    CHECK(list[0].field[0] == 1 && list[0].field[2] == 3);
    CHECK(list[1].nsa_id == 0xb2 && !strcmp(list[1].nickname, "marie") && list[1].status == 0);

    /* the friend service's records: the same-game flag set for a friend in this game */
    g_hle.kernel.title_id = 0x0100C2500FC20000ull;
    {
        SvcObj* creator = new_friend();
        SvcObj* fs = NULL;
        IpcBuf ids = {0x1000, 0x100, 0}, records = {0x2000, 0x400, 0};
        i32 count = 0;
        nn_friends_detail_ipc_SizedFriendFilter filter = {{0}};
        nn_account_Uid uid = {{0}};
        CHECK(nn_friends_detail_ipc_IServiceCreator_CreateFriendService(creator, &fs) == 0);
        signals = 0;
        CHECK(nn_friends_detail_ipc_IFriendService_GetFriendListIds(fs, 0, &uid, &filter, 0, ids, &count) == 0);
        CHECK(count == 2 && snx_ld_le64(GP(0x1000)) == 0xa1 && snx_ld_le64(GP(0x1008)) == 0xb2 && signals == 1);
        CHECK(nn_friends_detail_ipc_IFriendService_GetFriendList(fs, 1, &uid, &filter, 0, records, &count) == 0);
        CHECK(count == 1 && snx_ld_le64(GP(0x2010)) == 0xb2 && GP(0x2000)[0x128] == 1);
        /* UpdateFriendInfo: by position, an id that isn't a friend an invalid record */
        snx_st_le64(GP(0x1000), 0x99);
        snx_st_le64(GP(0x1008), 0xa1);
        ids.size = 16;
        CHECK(nn_friends_detail_ipc_IFriendService_UpdateFriendInfo(fs, &uid, 0, ids, records) == 0);
        CHECK(GP(0x2000)[0x128] == 0 && snx_ld_le64(GP(0x2200 + 0x10)) == 0xa1);
        CHECK(!strcmp((const char*)GP(0x2200 + 0x18), "Callie") && snx_ld_le32(GP(0x2200 + 0x58)) == 2);
        CHECK(GP(0x2200)[0x5C] == 1 && GP(0x2200)[0x60] == 1 && GP(0x2200)[0x120] == 1);
        /* the presence: status 2, this title, the storage's used part */
        memset(GP(0x3000), 0, 0xE0);
        snx_st_le32(GP(0x3018), 2);
        GP(0x3020)[0] = 0xAB;
        {
            IpcBuf pres = {0x3000, 0xE0, 0};
            CHECK(nn_friends_detail_ipc_IFriendService_UpdateUserPresence(fs, &uid, 0, pres) == 0);
        }
        for (int i = 0; i < 100 && !presences; i++) {
            struct timespec t = {0, 50000000};
            nanosleep(&t, NULL);
        }
        CHECK(!strcmp(presence, "{\"state\":\"PLAYING\",\"appId\":\"0100c2500fc20000\",\"appField\":\"qw==\"}"));
        /* a friend's picture, from its URL's host on the server */
        {
            IpcBuf image = {0x4000, 0x100, 0};
            u32 n = 0;
            CHECK(nn_friends_detail_ipc_IFriendService_GetFriendProfileImage(fs, &uid, 0xa1, image, &n) == 0);
            CHECK(n == 6 && !memcmp(GP(0x4000), "\xFF\xD8jpeg", 6));
            CHECK(!strcmp(sni, "cdn.example.nintendo.net"));
        }
        svcobj_unref(fs);
        svcobj_unref(creator);
    }

    /* Verified BCAT manifest and file, including a cache hit and rejected path/hash inputs. */
    {
        char* temporary = snx_temp_dir(NULL);
        char *path, *bytes;
        size_t n;
        CHECK(temporary != NULL);
        snprintf(g_datadir, sizeof g_datadir, "%s/", temporary);
        CHECK(grid0_bcat_sync("News") == 0);
        path = grid0_bcat_path("News", "test");
        bytes = (char*)snx_read_file(path, &n);
        CHECK(bytes && n == 4 && !memcmp(bytes, "news", 4));
        CHECK(grid0_bcat_sync("News") == 0); /* verified cached file */
        CHECK(!grid0_bcat_path("..", "test") && !grid0_bcat_path("News", "../test"));
        CHECK(!bcat_hash("bad", 3, "0000000000000000000000000000000000000000000000000000000000000000"));
        free(bytes);
        free(path);
        CHECK(snx_remove_tree(temporary) == 0);
        free(temporary);
        g_datadir[0] = 0;
    }

    /* a certificate for another host is refused, whatever the CA */
    X509_free(server_cert);
    server_cert = make_cert("someone.else", "DNS:someone.else", ca);
    {
        Response r;
        request(GRID0_BAAS_HOST, "GET", "/", NULL, NULL, NULL, &r);
        CHECK(r.status == 0 && strstr(r.why, "TLS handshake"));
        CHECK(grid0_friend_image(0xb2, jpeg, sizeof jpeg, 0) == 0); /* no URL: none */
    }
    printf("grid0_service: ok\n");
    return 0;
}
