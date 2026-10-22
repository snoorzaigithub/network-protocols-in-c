// include/rudp.h  (append if missing)
#ifndef RUDP_WINDOW
#define RUDP_WINDOW 20
#endif

#ifndef DAT
#define DAT 0
#endif
#ifndef SYN
#define SYN 1
#endif
#ifndef ACK
#define ACK 2
#endif
#ifndef FIN
#define FIN 4
#endif

typedef struct {
  char type;
  int  seqnum;
  char payload[];
} rudp_packet_t;
