#ifndef RUDP_REWRITE_H
#define RUDP_REWRITE_H

/* Packet type constants */
enum { DAT = 0, SYN = 1, ACK = 2, FIN = 4 };

/* Reliable UDP packet layout */
typedef struct {
    char type;
    int  seqnum;
    char payload[];
} rudp_packet_t;

#endif /* RUDP_REWRITE_H */
