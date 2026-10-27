/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The emulated network stack (src/common/network) over the host's loopback: blocking emulated on non-blocking host
   sockets, timeouts, poll, options, eventfd, cancellation and the ICMP emulation's fallback. */
#include "common/network/net.h"
#include "common/utilities/endian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

static unsigned failures, checks;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "network_stack.c:%d: check failed: %s\n", __LINE__, #x);                                   \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

static double now(void) {
#ifdef _WIN32
    return GetTickCount64() / 1000.0;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
#endif
}
static void quiet(const char* m) { (void)m; }

static SnxSockAddr loopback(void) {
    SnxSockAddr a;
    memset(&a, 0, sizeof a);
    a.family = SNX_AF_INET;
    a.addr[0] = 127;
    a.addr[3] = 1;
    return a;
}

static void tcp(void) {
    SnxSocket *l, *c, *s;
    SnxSockAddr a = loopback(), peer;
    int one = (int)snx_le32(1); /* option values are the guest's, little-endian (net.h) */
    CHECK(!snx_socket_open(&l, SNX_AF_INET, SNX_SOCK_STREAM, 0));
    CHECK(!snx_socket_setsockopt(l, SNX_SOL_SOCKET, SNX_SO_REUSEADDR, &one, sizeof one));
    CHECK(!snx_socket_bind(l, &a));
    CHECK(!snx_socket_listen(l, 4));
    CHECK(!snx_socket_name(l, &a) && a.port);
    /* nothing to accept: a non-blocking listener says so, a blocking one waits out its timeout */
    snx_socket_set_nonblocking(l, 1);
    CHECK(snx_socket_accept(l, &s, NULL, 0) == -SNX_EAGAIN);
    snx_socket_set_nonblocking(l, 0);
    SnxTimeval tv = {0, (int64_t)snx_le64(100000)};
    CHECK(!snx_socket_setsockopt(l, SNX_SOL_SOCKET, SNX_SO_RCVTIMEO, &tv, sizeof tv));
    double t0 = now();
    CHECK(snx_socket_accept(l, &s, NULL, 0) == -SNX_EAGAIN);
    CHECK(now() - t0 >= 0.09);

    CHECK(!snx_socket_open(&c, SNX_AF_INET, SNX_SOCK_STREAM, 0));
    CHECK(!snx_socket_connect(c, &a)); /* blocking: waits for the handshake */
    CHECK(!snx_socket_accept(l, &s, &peer, 0));
    CHECK(peer.family == SNX_AF_INET && peer.addr[0] == 127);
    CHECK(snx_socket_send(c, "hello world", 11, 0, NULL) == 11);
    char buf[64];
    CHECK(snx_socket_recv(s, buf, 5, SNX_MSG_PEEK, NULL, NULL) == 5);
    CHECK(snx_socket_recv(s, buf, 11, SNX_MSG_WAITALL, NULL, NULL) == 11 && !memcmp(buf, "hello world", 11));
    CHECK(snx_socket_recv(s, buf, 1, SNX_MSG_DONTWAIT, NULL, NULL) == -SNX_EAGAIN);

    SnxPollEntry e[2] = {{s, -1, SNX_POLLIN, 0}, {c, -1, SNX_POLLOUT, 0}};
    CHECK(snx_net_poll(e, 2, 0) == 1 && !e[0].revents && (e[1].revents & SNX_POLLOUT));
    snx_socket_send(c, "x", 1, 0, NULL);
    CHECK(snx_net_poll(e, 1, 1000) == 1 && (e[0].revents & SNX_POLLIN));
    int bytes = 0;
    CHECK(!snx_socket_readable_bytes(s, &bytes) && bytes == 1);
    snx_socket_recv(s, buf, 1, 0, NULL, NULL);

    int32_t v = 0;
    size_t len = sizeof v;
    CHECK(!snx_socket_getsockopt(s, SNX_SOL_SOCKET, SNX_SO_TYPE, &v, &len) &&
          (int32_t)snx_le32((uint32_t)v) == SNX_SOCK_STREAM && len == 4);
    len = sizeof v;
    CHECK(!snx_socket_getsockopt(s, SNX_SOL_SOCKET, SNX_SO_ERROR, &v, &len) && v == 0);
    CHECK(!snx_socket_setsockopt(s, SNX_SOL_TCP, SNX_TCP_NODELAY, &one, sizeof one));
    len = sizeof v;
    CHECK(!snx_socket_getsockopt(s, SNX_SOL_TCP, SNX_TCP_NODELAY, &v, &len) && v);
    SnxLinger lg = {(int32_t)snx_le32(1), (int32_t)snx_le32(3)}, lg2;
    len = sizeof lg2;
    CHECK(!snx_socket_setsockopt(s, SNX_SOL_SOCKET, SNX_SO_LINGER, &lg, sizeof lg));
    CHECK(!snx_socket_getsockopt(s, SNX_SOL_SOCKET, SNX_SO_LINGER, &lg2, &len) && lg2.onoff &&
          snx_le32((uint32_t)lg2.linger) == 3);
    CHECK(snx_socket_setsockopt(s, SNX_SOL_SOCKET, 12345, &one, sizeof one) == -SNX_ENOPROTOOPT);

    CHECK(!snx_socket_shutdown(c, SNX_SHUT_WR));
    CHECK(snx_socket_recv(s, buf, sizeof buf, 0, NULL, NULL) == 0);
    snx_socket_unref(c);
    snx_socket_unref(s);
    snx_socket_unref(l);

    /* a refused connection: in progress without blocking, then the error */
    CHECK(!snx_socket_open(&c, SNX_AF_INET, SNX_SOCK_STREAM, 0));
    snx_socket_set_nonblocking(c, 1);
    int r = snx_socket_connect(c, &a); /* the listener is gone */
    CHECK(r == -SNX_EINPROGRESS || r == -SNX_ECONNREFUSED);
    if (r == -SNX_EINPROGRESS) {
        SnxPollEntry w = {c, -1, SNX_POLLOUT, 0};
        /* Windows retries a refused TCP handshake before reporting failure, which can exceed two seconds.
           SO_ERROR may still be zero while connecting: require completion, not just an expired poll. */
        CHECK(snx_net_poll(&w, 1, 10000) == 1);
        CHECK(w.revents & (SNX_POLLOUT | SNX_POLLERR | SNX_POLLHUP));
        len = sizeof v;
        CHECK(!snx_socket_getsockopt(c, SNX_SOL_SOCKET, SNX_SO_ERROR, &v, &len) &&
              (int32_t)snx_le32((uint32_t)v) == SNX_ECONNREFUSED);
    }
    snx_socket_unref(c);
}

static void udp(void) {
    SnxSocket *a, *b;
    SnxSockAddr la = loopback(), from;
    CHECK(!snx_socket_open(&a, SNX_AF_INET, SNX_SOCK_DGRAM, SNX_IPPROTO_UDP));
    CHECK(!snx_socket_open(&b, SNX_AF_INET, SNX_SOCK_DGRAM, SNX_IPPROTO_UDP));
    CHECK(!snx_socket_bind(a, &la) && !snx_socket_name(a, &la));
    CHECK(snx_socket_send(b, "datagram", 8, 0, &la) == 8);
    char buf[4];
    int flags = 0;
    CHECK(snx_socket_recv(a, buf, sizeof buf, 0, &from, &flags) == 4 && !memcmp(buf, "data", 4));
    CHECK(from.family == SNX_AF_INET && from.port);
#ifndef _WIN32
    CHECK(flags & SNX_MSG_TRUNC);
#endif
    snx_socket_unref(a);
    snx_socket_unref(b);
}

static void pause_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000u);
#endif
}

/* a datagram to a port nobody listens on: the ICMP "port unreachable" that comes back fails no later receive, and the
   socket goes on working (Windows failed the next receive with WSAECONNRESET unless told not to) */
static void udp_unreachable(void) {
    SnxSocket *a, *b, *gone;
    SnxSockAddr la = loopback(), lg = loopback(), from;
    char buf[8];
    CHECK(!snx_socket_open(&a, SNX_AF_INET, SNX_SOCK_DGRAM, SNX_IPPROTO_UDP));
    CHECK(!snx_socket_open(&b, SNX_AF_INET, SNX_SOCK_DGRAM, SNX_IPPROTO_UDP));
    CHECK(!snx_socket_open(&gone, SNX_AF_INET, SNX_SOCK_DGRAM, SNX_IPPROTO_UDP));
    CHECK(!snx_socket_bind(a, &la) && !snx_socket_name(a, &la));
    CHECK(!snx_socket_bind(gone, &lg) && !snx_socket_name(gone, &lg));
    snx_socket_unref(gone); /* its port closed */
    CHECK(snx_socket_send(a, "nobody", 6, 0, &lg) == 6);
    pause_ms(100); /* the ICMP answer is back */
    CHECK(snx_socket_recv(a, buf, sizeof buf, SNX_MSG_DONTWAIT, &from, NULL) == -SNX_EAGAIN);
    CHECK(snx_socket_send(b, "still", 5, 0, &la) == 5);
    pause_ms(20);
    CHECK(snx_socket_recv(a, buf, sizeof buf, SNX_MSG_DONTWAIT, &from, NULL) == 5 && !memcmp(buf, "still", 5));
    snx_socket_unref(a);
    snx_socket_unref(b);
}

static void pair_and_event(void) {
    SnxSocket* p[2];
    CHECK(!snx_socket_pair(p, SNX_AF_UNIX, SNX_SOCK_STREAM, 0));
    char buf[8];
    CHECK(snx_socket_send(p[0], "pair", 4, 0, NULL) == 4);
    CHECK(snx_socket_recv(p[1], buf, sizeof buf, 0, NULL, NULL) == 4 && !memcmp(buf, "pair", 4));
    snx_socket_unref(p[0]);
    snx_socket_unref(p[1]);

    SnxSocket* e;
    uint8_t v[8] = {2};
    CHECK(!snx_socket_eventfd(&e, 0, 0));
    SnxPollEntry w = {e, -1, SNX_POLLIN | SNX_POLLOUT, 0};
    CHECK(snx_net_poll(&w, 1, 0) == 1 && w.revents == SNX_POLLOUT);
    CHECK(snx_socket_send(e, v, 8, 0, NULL) == 8 && snx_socket_send(e, v, 8, 0, NULL) == 8);
    CHECK(snx_net_poll(&w, 1, 0) == 1 && (w.revents & SNX_POLLIN));
    memset(v, 0, sizeof v);
    CHECK(snx_socket_recv(e, v, 8, 0, NULL, NULL) == 8 && v[0] == 4);
    snx_socket_set_nonblocking(e, 1);
    CHECK(snx_socket_recv(e, v, 8, 0, NULL, NULL) == -SNX_EAGAIN);
    snx_socket_unref(e);
}

#ifdef _WIN32
static DWORD WINAPI canceller(LPVOID arg) {
    (void)arg;
    Sleep(100);
    snx_net_cancel();
    return 0;
}
#else
static void* canceller(void* arg) {
    (void)arg;
    usleep(100000);
    snx_net_cancel();
    return NULL;
}
#endif
static void cancel(void) {
    SnxSocket* s;
    SnxSockAddr a = loopback();
    CHECK(!snx_socket_open(&s, SNX_AF_INET, SNX_SOCK_DGRAM, 0));
    CHECK(!snx_socket_bind(s, &a));
#ifdef _WIN32
    HANDLE t = CreateThread(NULL, 0, canceller, NULL, 0, NULL);
#else
    pthread_t t;
    pthread_create(&t, NULL, canceller, NULL);
#endif
    char buf[4];
    CHECK(snx_socket_recv(s, buf, sizeof buf, 0, NULL, NULL) == -SNX_EINTR); /* a blocking wait, forever */
#ifdef _WIN32
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
#else
    pthread_join(t, NULL);
#endif
    CHECK(snx_socket_recv(s, buf, sizeof buf, 0, NULL, NULL) == -SNX_EINTR);
    snx_net_restart();
    snx_socket_set_nonblocking(s, 1);
    CHECK(snx_socket_recv(s, buf, sizeof buf, 0, NULL, NULL) == -SNX_EAGAIN);
    snx_socket_unref(s);
}

static void icmp(void) {
    /* a raw ICMP socket: the host's when the test runs with privileges, the emulation otherwise. Either answers an
       echo to loopback when the host can ping, and neither crashes or blocks past the timeout when it can't */
    SnxSocket* s;
    CHECK(!snx_socket_open(&s, SNX_AF_INET, SNX_SOCK_RAW, SNX_IPPROTO_ICMP));
    if (!s) return;
    SnxSockAddr to = loopback();
    uint8_t echo[12] = {8, 0, 0, 0, 0x12, 0x34, 0, 1, 't', 'e', 's', 't'};
    if (!snx_socket_emulated(s)) {
        uint16_t c = 0;
        uint32_t sum = 0;
        for (int i = 0; i < 12; i += 2) sum += (uint32_t)echo[i] << 8 | echo[i + 1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        c = (uint16_t)~sum;
        echo[2] = (uint8_t)(c >> 8);
        echo[3] = (uint8_t)c;
    }
    SnxTimeval tv = {(int64_t)snx_le64(2), 0};
    snx_socket_setsockopt(s, SNX_SOL_SOCKET, SNX_SO_RCVTIMEO, &tv, sizeof tv);
    CHECK(snx_socket_send(s, echo, sizeof echo, 0, &to) == sizeof echo);
    uint8_t buf[128];
    SnxSockAddr from;
    double t0 = now();
    int64_t n;
    /* Privileged raw sockets see our outbound request as well as the reply, and they and macOS's ping socket every
       ICMP message to the host: a port unreachable for another test's UDP among them. An echo reply is checked */
    for (;;) {
        n = snx_socket_recv(s, buf, sizeof buf, 0, &from, NULL);
        size_t ip = n > 0 ? (size_t)(buf[0] & 15) * 4 : 0;
        if (n <= (int64_t)ip || buf[ip] == 0) break;
        if (now() - t0 >= 4) {
            n = -SNX_EAGAIN; /* other messages alone: no reply, as a timeout */
            break;
        }
    }
    CHECK(now() - t0 < 6);
    if (n > 0) {
        size_t ip = (size_t)(buf[0] & 15) * 4;
        CHECK(ip >= 20 && n >= (int64_t)ip + 12 && buf[9] == 1 && buf[ip] == 0 && buf[ip + 4] == 0x12 &&
              buf[ip + 5] == 0x34);
        CHECK(!memcmp(buf + ip + 8, "test", 4) && from.addr[0] == 127);
    } else {
        CHECK(n == -SNX_EAGAIN);
        printf("network_stack: SKIP live ICMP reply (host ping unavailable or timed out)\n");
    }
    snx_socket_unref(s);
}

static void names(void) {
    SnxAddrInfo hints = {0, SNX_AF_INET, SNX_SOCK_STREAM, 0, {0}, NULL, NULL}, *list = NULL;
    CHECK(!snx_net_getaddrinfo("127.0.0.1", "80", &hints, &list) && list);
    if (list) {
        const uint8_t* port = (const uint8_t*)&list->addr.port;
        CHECK(list->addr.addr[0] == 127 && port[0] == 0 && port[1] == 80);
    }
    snx_net_freeaddrinfo(list);
    hints.flags = SNX_AI_NUMERICHOST;
    CHECK(snx_net_getaddrinfo("not-an-address", NULL, &hints, &list) != 0);
    CHECK(!strcmp(snx_net_gai_strerror(SNX_EAI_AGAIN), "Temporary failure in name resolution"));
    uint8_t ip[4];
    CHECK(snx_net_parse_ipv4("10.1.2.3", ip) && ip[0] == 10 && ip[3] == 3 && !snx_net_parse_ipv4("x", ip));
    SnxNetInterface i[8];
    int n = snx_net_interfaces(i, 8);
    CHECK(n >= 0 && n <= 8);
    /* lan_adapter's lookup: each listed adapter by its name, its address and a part of its name */
    for (int k = 0; k < n; k++) {
        SnxNetInterface f;
        char ip[16];
        snprintf(ip, sizeof ip, "%u.%u.%u.%u", i[k].ip[0], i[k].ip[1], i[k].ip[2], i[k].ip[3]);
        CHECK(!snx_net_find_interface(i[k].name, &f) && !strcmp(f.name, i[k].name));
        CHECK(!snx_net_find_interface(ip, &f) && !memcmp(f.ip, i[k].ip, 4));
        if (strlen(i[k].name) > 2) CHECK(!snx_net_find_interface(i[k].name + 1, &f));
    }
    SnxNetInterface none;
    CHECK(snx_net_find_interface("no-such-adapter-here", &none) == -SNX_ENETDOWN);
    CHECK(snx_net_find_interface("203.0.113.254", &none) == -SNX_ENETDOWN);
    SnxNetInterface zt = {.name = "feth2190"}, win = {.name = "ZeroTier One [8056c2e21c000001]"}, wifi = {.name = "en0"};
    CHECK(snx_net_virtual_lan(&zt) && snx_net_virtual_lan(&win) && !snx_net_virtual_lan(&wifi));
}

int main(void) {
    snx_net_set_log(quiet);
    CHECK(!snx_net_init());
    tcp();
    udp();
    udp_unreachable();
    pair_and_event();
    cancel();
    icmp();
    names();
    if (failures) {
        fprintf(stderr, "network_stack: %u of %u checks failed\n", failures, checks);
        return 1;
    }
    printf("network_stack: %u checks passed\n", checks);
    return 0;
}
