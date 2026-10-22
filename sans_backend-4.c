#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "include/rudp.h"
#include "sans.h"

#define MAX_PAYLOAD 512
#define SWND_SIZE   20
#define TIMEOUT_MS  300

extern struct sockaddr_in peer_addr[1024];
extern socklen_t          peer_len[1024];

typedef struct {
    int socket;
    size_t packetlen;
    rudp_packet_t *packet;
    int seqnum;
    long long last_tx;
    int sent;
} swnd_entry_t;

/* ------------- required global symbol for tests ------------- */
swnd_entry_t *send_window = NULL;
/* ------------------------------------------------------------ */

static pthread_mutex_t swnd_lock = PTHREAD_MUTEX_INITIALIZER;
static int base_seq[1024];
static int next_seq[1024];
static int win_count[1024];

static long long now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec/1000;
}

__attribute__((constructor))
static void init_backend_arrays(void) {
    send_window = calloc(SWND_SIZE, sizeof(swnd_entry_t));
    for (int i = 0; i < 1024; ++i) {
        base_seq[i] = 0;
        next_seq[i] = 0;
        win_count[i] = 0;
    }
}

/* enqueue new packet into sliding window */
void enqueue_packet(int sock, const rudp_packet_t *pkt, size_t pktlen) {
    if (!pkt) return;
    pthread_mutex_lock(&swnd_lock);
    while (win_count[sock] >= SWND_SIZE) {
        pthread_mutex_unlock(&swnd_lock);
        usleep(5000);
        pthread_mutex_lock(&swnd_lock);
    }

    int tail = (base_seq[sock] + win_count[sock]) % SWND_SIZE;
    swnd_entry_t *e = &send_window[tail];
    if (e->packet) free(e->packet);
    e->socket = sock;
    e->packetlen = pktlen;
    e->packet = malloc(pktlen);
    memcpy(e->packet, pkt, pktlen);
    e->seqnum = next_seq[sock];
    e->packet->seqnum = next_seq[sock];
    e->last_tx = 0;
    e->sent = 0;

    next_seq[sock]++;
    win_count[sock]++;
    pthread_mutex_unlock(&swnd_lock);
}

/* cumulative ACK removal */
static void remove_acked(int sock, int ackseq) {
    while (win_count[sock] > 0) {
        int idx = base_seq[sock] % SWND_SIZE;
        swnd_entry_t *e = &send_window[idx];
        if (e->packet && e->seqnum <= ackseq) {
            free(e->packet);
            e->packet = NULL;
            base_seq[sock]++;
            win_count[sock]--;
        } else break;
    }
}

void* rudp_backend(void* unused) {
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&swnd_lock);
        for (int sock = 0; sock < 1024; ++sock) {
            if (win_count[sock] == 0) continue;
            struct sockaddr_in dest = peer_addr[sock];
            socklen_t destlen = peer_len[sock];

            long long t = now_ms();
            for (int i = 0; i < SWND_SIZE; ++i) {
                swnd_entry_t *e = &send_window[i];
                if (!e->packet || e->socket != sock) continue;
                if (!e->sent || (t - e->last_tx) > TIMEOUT_MS) {
                    sendto(sock, e->packet, e->packetlen, 0,
                           (struct sockaddr*)&dest, destlen);
                    e->sent = 1;
                    e->last_tx = t;
                }
            }

            rudp_packet_t ack;
            ssize_t n = recvfrom(sock, &ack, sizeof(ack), MSG_DONTWAIT,
                                 (struct sockaddr*)&dest, &destlen);
            if (n > 0 && ack.type == ACK) remove_acked(sock, ack.seqnum);
        }
        pthread_mutex_unlock(&swnd_lock);
        usleep(5000);
    }
    return NULL;
}

int init_rudp_backend(void) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, rudp_backend, NULL) != 0) return -1;
    pthread_detach(tid);
    return 0;
}
