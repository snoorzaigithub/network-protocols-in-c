#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include "sans.h"
#include "rudp.h"


#ifndef SYN
#define SYN 1
#endif
#ifndef ACK
#define ACK 2
#endif
#ifndef SYNACK
#define SYNACK 3
#endif

static int g_last_fd = -1;

struct rudp_rec {
    int fd;
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int used;
};
static struct rudp_rec rudp_tbl[1024];

static void port_to_service(int port, char buf[16]) { snprintf(buf, 16, "%d", port); }

int rudp_set_peer(int fd, const struct sockaddr *sa, socklen_t slen) {
    int i, freei = -1;
    for (i = 0; i < (int)(sizeof(rudp_tbl)/sizeof(rudp_tbl[0])); i++) {
        if (rudp_tbl[i].used && rudp_tbl[i].fd == fd) {
            memcpy(&rudp_tbl[i].addr, sa, slen);
            rudp_tbl[i].addrlen = slen;
            return 0;
        }
        if (!rudp_tbl[i].used && freei < 0) freei = i;
    }
    if (freei < 0) { errno = ENOSPC; return -1; }
    rudp_tbl[freei].fd = fd;
    memcpy(&rudp_tbl[freei].addr, sa, slen);
    rudp_tbl[freei].addrlen = slen;
    rudp_tbl[freei].used = 1;
    return 0;
}

int rudp_get_peer(int fd, struct sockaddr_storage *sa, socklen_t *slen) {
    int i;
    for (i = 0; i < (int)(sizeof(rudp_tbl)/sizeof(rudp_tbl[0])); i++) {
        if (rudp_tbl[i].used && rudp_tbl[i].fd == fd) {
            if (sa && slen) {
                memcpy(sa, &rudp_tbl[i].addr, rudp_tbl[i].addrlen);
                *slen = rudp_tbl[i].addrlen;
            }
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

static int ensure_proto_any(int protocol) {
    if (protocol != IPPROTO_TCP && protocol != IPPROTO_RUDP) { errno = EPROTONOSUPPORT; return -1; }
    return 0;
}

static int connect_one_tcp(struct addrinfo *ai) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) return -1;
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) return fd;
    errno = ECONNREFUSED;
    return -1;
}

static int listen_on_first_tcp(const char *addr, const char *service, int backlog) {
    struct addrinfo hints, *res = NULL, *it = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(addr, service, &hints, &res) != 0) { errno = EINVAL; return -1; }
    int fd = -1, one = 1;
    for (it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(fd, it->ai_addr, it->ai_addrlen) == 0 && listen(fd, backlog) == 0) { freeaddrinfo(res); return fd; }
    }
    freeaddrinfo(res);
    errno = EADDRNOTAVAIL;
    return -1;
}

/* -------- RUDP handshake (one-byte control codes from rudp.h) -------- */

static int rudp_handshake_client(int fd, const struct sockaddr *sa, socklen_t slen) {
    uint8_t b;
    struct sockaddr_storage from; socklen_t fromlen = sizeof(from);

    b = (uint8_t)SYN;
    if (sendto(fd, &b, 1, 0, sa, slen) < 0) return -1;

    ssize_t r = recvfrom(fd, &b, 1, 0, (struct sockaddr*)&from, &fromlen);
    if (r <= 0) {
        b = (uint8_t)SYN;
        if (sendto(fd, &b, 1, 0, sa, slen) < 0) return -1;
        r = recvfrom(fd, &b, 1, 0, (struct sockaddr*)&from, &fromlen);
        if (r <= 0) return -1;
    }
    if (!(r == 1 && b == (uint8_t)SYNACK)) return -1;

    b = (uint8_t)ACK;
    if (sendto(fd, &b, 1, 0, sa, slen) < 0) return -1;
    return 0;
}

static int rudp_handshake_server(int fd, struct sockaddr_storage *peer, socklen_t *plen) {
    uint8_t b;
    socklen_t alen = sizeof(*peer);

    for (;;) {
        ssize_t r = recvfrom(fd, &b, 1, 0, (struct sockaddr*)peer, &alen);
        if (r <= 0) return -1;
        if (r == 1 && b == (uint8_t)SYN) break; /* discard anything not SYN */
    }

    b = (uint8_t)SYNACK;
    if (sendto(fd, &b, 1, 0, (struct sockaddr*)peer, alen) < 0) return -1;

    for (int attempt = 0; attempt < 2; attempt++) {
        struct sockaddr_storage from; socklen_t fromlen = sizeof(from);
        ssize_t r = recvfrom(fd, &b, 1, 0, (struct sockaddr*)&from, &fromlen);
        if (r == 1 && b == (uint8_t)ACK) { *plen = alen; return 0; }
        b = (uint8_t)SYNACK;
        if (sendto(fd, &b, 1, 0, (struct sockaddr*)peer, alen) < 0) return -1;
    }
    return -1;
}

/* ------------------------- public entry points ----------------- */

int sans_connect(const char* addr, int port, int protocol) {
    if (ensure_proto_any(protocol) < 0) return -1;

    if (protocol == IPPROTO_TCP) {
        char service[16]; port_to_service(port, service);
        struct addrinfo hints, *res = NULL, *it = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if (getaddrinfo(addr, service, &hints, &res) != 0) { errno = EINVAL; return -1; }
        int fd = -1;
        for (it = res; it; it = it->ai_next) { fd = connect_one_tcp(it); if (fd >= 0) break; }
        freeaddrinfo(res);
        if (fd >= 0) g_last_fd = fd; else errno = ECONNREFUSED;
        return fd;
    } else {
        char service[16]; port_to_service(port, service);
        struct addrinfo hints, *res = NULL, *it = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        if (getaddrinfo(addr, service, &hints, &res) != 0) { errno = EINVAL; return -1; }
        int fd = -1;
        for (it = res; it; it = it->ai_next) {
            fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (fd < 0) continue;
            if (rudp_handshake_client(fd, it->ai_addr, it->ai_addrlen) == 0) {
                rudp_set_peer(fd, it->ai_addr, it->ai_addrlen);
                g_last_fd = fd;
                freeaddrinfo(res);
                return fd;
            }
            /* if handshake failed, try next addrinfo */
        }
        freeaddrinfo(res);
        errno = ECONNREFUSED;
        return -1;
    }
}

int sans_accept(const char* addr, int port, int protocol) {
    if (ensure_proto_any(protocol) < 0) return -1;

    if (protocol == IPPROTO_TCP) {
        char service[16]; port_to_service(port, service);
        int lfd = listen_on_first_tcp(addr, service, SOMAXCONN);
        if (lfd < 0) return -1;
        for (;;) {
            int cfd = accept(lfd, NULL, NULL);
            if (cfd >= 0) { close(lfd); g_last_fd = cfd; return cfd; }
            if (errno == EINTR) continue;
            int e = errno; close(lfd); errno = e; return -1;
        }
    } else {
        char service[16]; port_to_service(port, service);
        struct addrinfo hints, *res = NULL, *it = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        hints.ai_flags = AI_PASSIVE;
        if (getaddrinfo(addr, service, &hints, &res) != 0) { errno = EINVAL; return -1; }
        int fd = -1;
        for (it = res; it; it = it->ai_next) {
            fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if (fd < 0) continue;
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            if (bind(fd, it->ai_addr, it->ai_addrlen) == 0) {
                struct sockaddr_storage peer; socklen_t plen = 0;
                if (rudp_handshake_server(fd, &peer, &plen) == 0) {
                    rudp_set_peer(fd, (struct sockaddr*)&peer, plen);
                    g_last_fd = fd;
                    freeaddrinfo(res);
                    return fd;
                }
            }
            /* else try next addrinfo */
        }
        freeaddrinfo(res);
        errno = EAGAIN;
        return -1;
    }
}

int sans_disconnect(int fd) {
    if (fd == g_last_fd && fd >= 0) {
        close(fd);
        g_last_fd = -1;
    }
    return 0;
}
