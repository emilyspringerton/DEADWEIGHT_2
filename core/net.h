/* Tiny portability shim: POSIX sockets vs winsock (mingw). Include before any other system
 * header. Ported near-verbatim from DEADWEIGHT/core/net.h (same real infra, dw2_ prefix so a
 * reader never mistakes this repo's socket layer for the sibling's). */
#ifndef DW2_NET_H
#define DW2_NET_H
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET dw2_sock;
#define DW2_BAD_SOCK INVALID_SOCKET
static inline int dw2_net_init(void) { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d); }
static inline void dw2_close(dw2_sock s) { closesocket(s); }
static inline int dw2_nonblock(dw2_sock s) { u_long v = 1; return ioctlsocket(s, FIONBIO, &v); }
static inline int dw2_wouldblock(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static inline int dw2_poll(struct pollfd *f, unsigned n, int ms) { return WSAPoll(f, (ULONG)n, ms); }
static inline uint64_t dw2_now_ms(void) { return (uint64_t)GetTickCount64(); }
static inline long dw2_send(dw2_sock s, const void *b, size_t n) { return send(s, (const char *)b, (int)n, 0); }
static inline long dw2_recv(dw2_sock s, void *b, size_t n) { return recv(s, (char *)b, (int)n, 0); }
static inline void dw2_sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
typedef int dw2_sock;
#define DW2_BAD_SOCK (-1)
static inline int dw2_net_init(void) { return 0; }
static inline void dw2_close(dw2_sock s) { close(s); }
static inline int dw2_nonblock(dw2_sock s) { int f = fcntl(s, F_GETFL, 0); return f < 0 ? -1 : fcntl(s, F_SETFL, f | O_NONBLOCK); }
static inline int dw2_wouldblock(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
static inline int dw2_poll(struct pollfd *f, unsigned n, int ms) { return poll(f, (nfds_t)n, ms); }
static inline uint64_t dw2_now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u; }
static inline long dw2_send(dw2_sock s, const void *b, size_t n) { return send(s, b, n, MSG_NOSIGNAL); }
static inline long dw2_recv(dw2_sock s, void *b, size_t n) { return recv(s, b, n, 0); }
static inline void dw2_sleep_ms(unsigned ms) { struct timespec t; t.tv_sec = ms / 1000; t.tv_nsec = (long)(ms % 1000) * 1000000L; nanosleep(&t, NULL); }
#endif

/* Blocking TCP connect to host:port. Returns DW2_BAD_SOCK on failure. */
static inline dw2_sock dw2_connect(const char *host, int port) {
    struct addrinfo hints, *res, *ai; char ps[16]; dw2_sock s = DW2_BAD_SOCK;
    memset(&hints, 0, sizeof hints); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hints, &res) != 0) return DW2_BAD_SOCK;
    for (ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == DW2_BAD_SOCK) continue;
        if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        dw2_close(s); s = DW2_BAD_SOCK;
    }
    freeaddrinfo(res);
    return s;
}
#endif
