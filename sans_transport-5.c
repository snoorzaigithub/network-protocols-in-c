#include "sans.h"
#include "rudp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_PAYLOAD 512
#define SWND_SIZE   20

extern struct sockaddr_in peer_addr[1024];
extern socklen_t          peer_len[1024];

void enqueue_packet(int sock, const rudp_packet_t *pkt, size_t pktlen);
int  init_rudp_backend(void);

static int expected_seq[1024];

__attribute__((constructor))
static void __init_transport_state(void) {
    for (int i = 0; i < 1024; ++i) expected_seq[i] = 0;
    init_rudp_backend();
}

int sans_send_pkt(int fd, const char *data, int length) {
    int socktype; socklen_t optlen = sizeof(socktype);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socktype, &optlen) < 0) return -1;
    if (socktype == SOCK_STREAM) return send(fd, data, length, 0);

    if (fd < 0 || fd >= 1024 || !data || length <= 0 || length > MAX_PAYLOAD) return -1;

    size_t pktlen = sizeof(rudp_packet_t) + (size_t)length;
    rudp_packet_t *pkt = malloc(pktlen);
    pkt->type = DAT;
    pkt->seqnum = 0;
    memcpy(pkt->payload, data, (size_t)length);
    enqueue_packet(fd, pkt, pktlen);
    free(pkt);
    return length;
}

int sans_recv_pkt(int fd, char *out, int cap) {
    int socktype; socklen_t optlen = sizeof(socktype);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socktype, &optlen) < 0) return -1;
    if (socktype == SOCK_STREAM) return recv(fd, out, cap, 0);

    const size_t bufcap = sizeof(rudp_packet_t) + MAX_PAYLOAD;
    rudp_packet_t *rx = malloc(bufcap);
    if (!rx) return -1;

    for (;;) {
        socklen_t fromlen = peer_len[fd];
        ssize_t n = recvfrom(fd, rx, bufcap, 0,
                             (struct sockaddr*)&peer_addr[fd], &fromlen);
        if (n <= 0) { free(rx); return -1; }

        ssize_t paylen = n - (ssize_t)sizeof(rudp_packet_t);
        if (paylen < 0) paylen = 0;
        if (paylen > MAX_PAYLOAD) paylen = MAX_PAYLOAD;

        if (rx->type == DAT) {
            int seq = rx->seqnum;
            rudp_packet_t ack = { .type = ACK,
                                  .seqnum = (seq == expected_seq[fd]) ? seq : expected_seq[fd]-1 };
            sendto(fd, &ack, sizeof(ack), 0,
                   (struct sockaddr*)&peer_addr[fd], peer_len[fd]);

            if (seq == expected_seq[fd]) {
                expected_seq[fd]++;
                int copy = (paylen <= cap) ? (int)paylen : cap;
                memcpy(out, rx->payload, (size_t)copy);
                free(rx);
                return copy;
            }
        }
    }
}
