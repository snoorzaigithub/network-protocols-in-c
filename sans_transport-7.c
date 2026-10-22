#include "sans.h"
#include "rudp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_PAYLOAD 512

extern struct sockaddr_in peer_addr[1024];
extern socklen_t          peer_len[1024];

void enqueue_packet(int sock, const rudp_packet_t *pkt, size_t pktlen);
int  is_packet_acked_for_socket(int sock, int seqnum);
void wait_for_ack_and_remove(int sock, int seq_filter);

static int next_expected[1024];
static int last_acknowledged[1024];

__attribute__((constructor))
static void __init_transport_state(void) {
    for (int i = 0; i < 1024; ++i) {
        next_expected[i]     = 0;
        last_acknowledged[i] = -1;
    }
}

int sans_send_pkt(int fd, const char *data, int length) {
    int socktype;
    socklen_t optlen = sizeof(socktype);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socktype, &optlen) < 0) return -1;
    if (socktype == SOCK_STREAM) return send(fd, data, length, 0);

    if (fd < 0 || fd >= 1024 || !data || length < 0 || length > MAX_PAYLOAD) return -1;

    size_t frame_len = sizeof(rudp_packet_t) + (size_t)length;
    rudp_packet_t *frame = (rudp_packet_t*)malloc(frame_len);
    if (!frame) return -1;

    memset(frame, 0, frame_len);
    frame->type   = DAT;
    frame->seqnum = 0;
    memcpy(frame->payload, data, (size_t)length);

    enqueue_packet(fd, frame, frame_len);
    free(frame);

    wait_for_ack_and_remove(fd, -1);
    return length;
}

int sans_recv_pkt(int fd, char *out, int cap) {
    int socktype;
    socklen_t optlen = sizeof(socktype);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socktype, &optlen) < 0) return -1;
    if (socktype == SOCK_STREAM) return recv(fd, out, cap, 0);

    if (fd < 0 || fd >= 1024 || !out) return -1;

    const size_t bufcap = sizeof(rudp_packet_t) + MAX_PAYLOAD;
    rudp_packet_t *rx = (rudp_packet_t*)malloc(bufcap);
    if (!rx) return -1;

    for (;;) {
        memset(rx, 0, bufcap);
        socklen_t fromlen = peer_len[fd];
        if (!fromlen) fromlen = sizeof(struct sockaddr_in);

        ssize_t n = recvfrom(fd, rx, bufcap, 0,
                             (struct sockaddr*)&peer_addr[fd], &fromlen);
        if (n <= 0) { free(rx); return -1; }

        ssize_t payload_n = n - (ssize_t)sizeof(rudp_packet_t);
        if (payload_n < 0) payload_n = 0;
        if (payload_n > MAX_PAYLOAD) payload_n = MAX_PAYLOAD;

        int  seq = rx->seqnum;
        char typ = rx->type;

        if (typ == DAT && seq == next_expected[fd]) {
            int copy = (payload_n <= cap) ? (int)payload_n : cap;
            if (copy > 0) memcpy(out, rx->payload, (size_t)copy);

            rudp_packet_t ack = { .type = ACK, .seqnum = seq };
            (void)sendto(fd, &ack, sizeof(ack), 0,
                         (struct sockaddr*)&peer_addr[fd], peer_len[fd]);

            last_acknowledged[fd] = seq;
            next_expected[fd] ^= 1;
            free(rx);
            return copy;
        } else {
            if (last_acknowledged[fd] >= 0) {
                rudp_packet_t ack = { .type = ACK, .seqnum = last_acknowledged[fd] };
                (void)sendto(fd, &ack, sizeof(ack), 0,
                             (struct sockaddr*)&peer_addr[fd], peer_len[fd]);
            }
        }
    }
}
