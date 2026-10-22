#include "sans.h"
#include "rudp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>

struct sockaddr_in peer_addr[1024];
socklen_t          peer_len[1024];

__attribute__((constructor))
static void __init_peer_bookkeeping(void) {
    for (int i = 0; i < 1024; ++i) {
        peer_len[i] = 0;
        memset(&peer_addr[i], 0, sizeof(peer_addr[i]));
    }
}

static int tcp_connect(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct hostent *hp = gethostbyname(host);
    if (!hp) { close(fd); return -1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    memcpy(&sa.sin_addr.s_addr, hp->h_addr_list[0], hp->h_length);

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
    return fd;
}

static int rudp_connect(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(port);
    dst.sin_addr.s_addr = inet_addr(host);
    socklen_t dstlen = sizeof(dst);

    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    rudp_packet_t syn = { .type = SYN, .seqnum = 0 };
    rudp_packet_t rx;

    for (int attempt = 0; attempt < 3; ++attempt) {
        (void)sendto(fd, &syn, sizeof(syn), 0, (struct sockaddr*)&dst, dstlen);
        int n = recvfrom(fd, &rx, sizeof(rx), 0, (struct sockaddr*)&dst, &dstlen);
        if (n > 0 && rx.type == (SYN | ACK)) {
            rudp_packet_t ack = { .type = ACK, .seqnum = rx.seqnum + 1 };
            (void)sendto(fd, &ack, sizeof(ack), 0, (struct sockaddr*)&dst, dstlen);
            peer_addr[fd] = dst;
            peer_len[fd]  = dstlen;
            return fd;
        }
    }
    close(fd);
    return -1;
}

int sans_connect(const char *addr, int port, int protocol) {
    if (protocol == IPPROTO_TCP)  return tcp_connect(addr, port);
    if (protocol == IPPROTO_RUDP) return rudp_connect(addr, port);
    return -1;
}

static int tcp_accept(const char *addr, int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(port);
    sa.sin_addr.s_addr = inet_addr(addr);

    if (bind(listen_fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) { close(listen_fd); return -1; }
    if (listen(listen_fd, 5) < 0) { close(listen_fd); return -1; }

    int fd = accept(listen_fd, NULL, NULL);
    close(listen_fd);
    return fd;
}

static int rudp_accept(const char *addr, int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    struct sockaddr_in local, remote;
    memset(&local, 0, sizeof(local));
    memset(&remote, 0, sizeof(remote));
    local.sin_family      = AF_INET;
    local.sin_port        = htons(port);
    local.sin_addr.s_addr = inet_addr(addr);

    if (bind(fd, (struct sockaddr*)&local, sizeof(local)) < 0) { close(fd); return -1; }

    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    socklen_t rlen = sizeof(remote);
    rudp_packet_t in, synack, ack;

    for (;;) {
        int n = recvfrom(fd, &in, sizeof(in), 0, (struct sockaddr*)&remote, &rlen);
        if (n > 0 && in.type == SYN) {
            synack.type  = SYN | ACK;
            synack.seqnum = in.seqnum + 1;

            for (int tries = 0; tries < 3; ++tries) {
                (void)sendto(fd, &synack, sizeof(synack), 0, (struct sockaddr*)&remote, rlen);
                int m = recvfrom(fd, &ack, sizeof(ack), 0, (struct sockaddr*)&remote, &rlen);
                if (m > 0 && ack.type == ACK) {
                    peer_addr[fd] = remote;
                    peer_len[fd]  = rlen;
                    return fd;
                }
            }
        }
    }
}

int sans_accept(const char *addr, int port, int protocol) {
    if (protocol == IPPROTO_TCP)  return tcp_accept(addr, port);
    if (protocol == IPPROTO_RUDP) return rudp_accept(addr, port);
    return -1;
}

int sans_disconnect(int socket_fd) {
    return close(socket_fd);
}

int sans_send_data(int socket_fd, const char *buf, int len) {
    return sendto(socket_fd, buf, len, 0,
                  (struct sockaddr*)&peer_addr[socket_fd], peer_len[socket_fd]);
}

int sans_recv_data(int socket_fd, char *buf, int len) {
    return recvfrom(socket_fd, buf, len, 0,
                    (struct sockaddr*)&peer_addr[socket_fd], &peer_len[socket_fd]);
}
