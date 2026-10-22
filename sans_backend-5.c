#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "include/rudp.h"
#include "sans.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define MAX_PAYLOAD 512
const unsigned int swnd_size = 1;

typedef struct {
    int socket;
    size_t packetlen;
    rudp_packet_t *packet;
    int seqnum;
} swnd_entry_t;

static swnd_entry_t *send_window = NULL;
static int head = 0;
static int count = 0;

static pthread_mutex_t swnd_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t swnd_not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t swnd_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t swnd_acked = PTHREAD_COND_INITIALIZER;

static int next_seq[1024];
static int last_acked[1024];

__attribute__((constructor))
static void init_backend_arrays(void) {
    for (int i = 0; i < 1024; ++i) {
        last_acked[i] = -1;
        next_seq[i] = 0;
    }
}

void enqueue_packet(int sock, const rudp_packet_t *pkt, size_t pktlen) {
    if (!pkt) return;
    pthread_mutex_lock(&swnd_lock);
    while (count == (int)swnd_size) pthread_cond_wait(&swnd_not_full, &swnd_lock);

    int tail = (head + count) % swnd_size;
    if (!send_window) {
        send_window = malloc(sizeof(swnd_entry_t) * swnd_size);
        if (!send_window) {
            perror("malloc send_window");
            pthread_mutex_unlock(&swnd_lock);
            return;
        }
        for (unsigned int i = 0; i < swnd_size; ++i) {
            send_window[i].socket = -1;
            send_window[i].packetlen = 0;
            send_window[i].packet = NULL;
            send_window[i].seqnum = -1;
        }
    }

    send_window[tail].socket = sock;
    send_window[tail].packetlen = pktlen;
    send_window[tail].packet = malloc(pktlen);
    if (!send_window[tail].packet) {
        perror("malloc entry packet");
        pthread_mutex_unlock(&swnd_lock);
        return;
    }
    memcpy(send_window[tail].packet, pkt, pktlen);
    send_window[tail].seqnum = next_seq[sock];
    send_window[tail].packet->seqnum = send_window[tail].seqnum;

    next_seq[sock] ^= 1;

    count++;
    pthread_cond_signal(&swnd_not_empty);
    pthread_mutex_unlock(&swnd_lock);
}

static void dequeue_packet(void) {
    if (!send_window || count == 0) return;
    if (send_window[head].packet) free(send_window[head].packet);
    send_window[head].packetlen = 0;
    send_window[head].socket = -1;
    send_window[head].seqnum = -1;

    head = (head + 1) % swnd_size;
    count--;
    pthread_cond_signal(&swnd_not_full);
}

void wait_for_ack_and_remove_internal(int sock, int seq_filter) {
    pthread_mutex_lock(&swnd_lock);
    for (;;) {
        int outstanding = 0;
        if (count > 0 && send_window[head].socket == sock) {
            if (seq_filter >= 0) {
                if (send_window[head].seqnum == seq_filter) outstanding = 1;
            } else {
                outstanding = 1;
            }
        }

        if (!outstanding) {
            pthread_mutex_unlock(&swnd_lock);
            return;
        }
        pthread_cond_wait(&swnd_acked, &swnd_lock);
    }
}

void wait_for_ack_and_remove(int sock, int seq_filter) {
    wait_for_ack_and_remove_internal(sock, seq_filter);
}

int is_packet_acked_for_socket(int sock, int seqnum) {
    return (last_acked[sock] == seqnum) ? 1 : 0;
}

void* rudp_backend(void* unused) {
    (void)unused;
    pthread_mutex_lock(&swnd_lock);
    if (!send_window) {
        send_window = malloc(sizeof(swnd_entry_t) * swnd_size);
        if (!send_window) {
            perror("malloc send_window");
            pthread_mutex_unlock(&swnd_lock);
            return NULL;
        }
        for (unsigned int i = 0; i < swnd_size; ++i) {
            send_window[i].socket = -1;
            send_window[i].packetlen = 0;
            send_window[i].packet = NULL;
            send_window[i].seqnum = -1;
        }
    }
    pthread_mutex_unlock(&swnd_lock);

    extern struct sockaddr_in peer_addr[1024];
    extern socklen_t peer_len[1024];

    while (1) {
        pthread_mutex_lock(&swnd_lock);
        while (count == 0) pthread_cond_wait(&swnd_not_empty, &swnd_lock);

        int sock = send_window[head].socket;
        size_t pktlen = send_window[head].packetlen;
        rudp_packet_t *pkt = send_window[head].packet;
        int pkt_seq = send_window[head].seqnum;
        pthread_mutex_unlock(&swnd_lock);

        if (!pkt || sock < 0) { usleep(1000); continue; }

        struct sockaddr_in dest = peer_addr[sock];
        socklen_t destlen = peer_len[sock];

        struct timeval tv = {1, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        ssize_t sent_bytes = sendto(sock, pkt, pktlen, 0, (struct sockaddr*)&dest, destlen);
        if (sent_bytes < 0) { usleep(100000); continue; }

        while (1) {
            size_t rxbuf_size = sizeof(rudp_packet_t) + MAX_PAYLOAD;
            rudp_packet_t *rxbuf = malloc(rxbuf_size);
            if (!rxbuf) { usleep(500000); continue; }
            memset(rxbuf, 0, rxbuf_size);

            socklen_t fromlen = destlen;
            if (fromlen == 0) fromlen = sizeof(struct sockaddr_in);

            ssize_t rn = recvfrom(sock, rxbuf, rxbuf_size, 0, (struct sockaddr*)&dest, &fromlen);
            if (rn > 0) {
                if (rxbuf->type == ACK && rxbuf->seqnum == pkt_seq) {
                    last_acked[sock] = pkt_seq;

                    pthread_mutex_lock(&swnd_lock);
                    if (send_window[head].packet == pkt) dequeue_packet();
                    pthread_cond_broadcast(&swnd_acked);
                    pthread_mutex_unlock(&swnd_lock);

                    free(rxbuf);
                    break;
                } else {
                    free(rxbuf);
                    continue;
                }
            } else {
                free(rxbuf);
                sendto(sock, pkt, pktlen, 0, (struct sockaddr*)&dest, destlen);
            }
        }
    }
    return NULL;
}

int init_rudp_backend(void) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, rudp_backend, NULL) != 0) return -1;
    pthread_detach(tid);
    return 0;
}
