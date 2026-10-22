#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

int rudp_get_peer(int fd, struct sockaddr_storage *sa, socklen_t *slen);

ssize_t sans_send_pkt(int fd, const void *buf, size_t len) {
    struct sockaddr_storage sa; socklen_t sl = sizeof(sa);
    if (rudp_get_peer(fd, &sa, &sl) != 0) { errno = ENOTCONN; return -1; }
    return sendto(fd, buf, len, 0, (struct sockaddr*)&sa, sl);
}

ssize_t sans_recv_pkt(int fd, void *buf, size_t len) {
    struct sockaddr_storage from; socklen_t fromlen = sizeof(from);
    return recvfrom(fd, buf, len, 0, (struct sockaddr*)&from, &fromlen);
}
