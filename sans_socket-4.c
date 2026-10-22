#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

static int g_last_fd = -1;

static int ensure_tcp(int protocol) {
    if (protocol != IPPROTO_TCP) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    return 0;
}

static void port_to_service(int port, char buf[16]) {
    snprintf(buf, 16, "%d", port);
}

static void my_freeaddrinfo(struct addrinfo *res) {
    freeaddrinfo(res);
}

static int my_close(int fd) {
    return close(fd);
}

static int connect_one(struct addrinfo *ai) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) return -1;
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) return fd;
    errno = ECONNREFUSED;
    return -1;
}

int sans_connect(const char* addr, int port, int protocol) {
    if (ensure_tcp(protocol) < 0) return -1;

    char service[16];
    port_to_service(port, service);

    struct addrinfo hints, *res = NULL, *it = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int rc = getaddrinfo(addr, service, &hints, &res);
    if (rc != 0) { errno = EINVAL; return -1; }

    int fd = -1;
    for (it = res; it; it = it->ai_next) {
        fd = connect_one(it);
        if (fd >= 0) break;
    }

    my_freeaddrinfo(res);

    if (fd >= 0) g_last_fd = fd;
    else errno = ECONNREFUSED;

    return fd;
}

static int listen_on_first(const char *addr, const char *service, int backlog) {
    struct addrinfo hints, *res = NULL, *it = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_PASSIVE;

    int rc = getaddrinfo(addr, service, &hints, &res);
    if (rc != 0) { errno = EINVAL; return -1; }

    int fd = -1;
    for (it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(fd, it->ai_addr, it->ai_addrlen) == 0 &&
            listen(fd, backlog) == 0) {
            my_freeaddrinfo(res);
            return fd;
        }
    }

    my_freeaddrinfo(res);
    errno = EADDRNOTAVAIL;
    return -1;
}

int sans_accept(const char* addr, int port, int protocol) {
    if (ensure_tcp(protocol) < 0) return -1;

    char service[16];
    port_to_service(port, service);

    int lfd = listen_on_first(addr, service, SOMAXCONN);
    if (lfd < 0) return -1;

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd >= 0) { g_last_fd = cfd; return cfd; }
        if (errno == EINTR) continue;
        return -1;
    }
}

int sans_disconnect(int fd) {
    if (fd == g_last_fd && fd >= 0) {
        my_close(fd);
        g_last_fd = -1;
    }
    return 0;
}
