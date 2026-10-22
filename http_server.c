#include <netinet/in.h>
#include "include/sans.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#define BUF_SIZE 1024

static void send_404(int sock) {
    const char *body = "<html><body>404 Not Found</body></html>\n";
    char hdr[BUF_SIZE];
    int hlen = snprintf(
        hdr, sizeof(hdr),
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: %ld\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n",
        (long)strlen(body)
    );
    if (hlen > 0) sans_send_pkt(sock, hdr, hlen);
    sans_send_pkt(sock, body, (int)strlen(body));
}

int http_server(const char* host, int port) {
    int sock = sans_accept(host, port, IPPROTO_TCP);
    if (sock < 0) return -1;

    char req[BUF_SIZE] = {0};
    int r = sans_recv_pkt(sock, req, (int)sizeof(req));
    if (r <= 0) { sans_disconnect(sock); return -1; }

    char method[16] = {0};
    char rawpath[512] = {0};
    if (sscanf(req, "%15s %511s", method, rawpath) != 2) {
        send_404(sock);
        sans_disconnect(sock);
        return 0;
    }
    if (strncmp(method, "GET", 3) != 0) {
        send_404(sock);
        sans_disconnect(sock);
        return 0;
    }

    char path[512] = {0};
    if (rawpath[0] == '/') {
        size_t i = 0, j = 1;
        while (rawpath[j] && i + 1 < sizeof(path)) path[i++] = rawpath[j++];
        path[i] = '\0';
    } else {
        snprintf(path, sizeof(path), "%s", rawpath);
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_404(sock);
        sans_disconnect(sock);
        return 0;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        send_404(sock);
        sans_disconnect(sock);
        return 0;
    }

    char hdr[BUF_SIZE];
    long clen = (long)st.st_size;
    int hlen = snprintf(
        hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %ld\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n",
        clen
    );
    if (hlen <= 0 || hlen >= BUF_SIZE) {
        fclose(fp);
        sans_disconnect(sock);
        return -1;
    }
    if (sans_send_pkt(sock, hdr, hlen) < 0) {
        fclose(fp);
        sans_disconnect(sock);
        return -1;
    }

    char buf[BUF_SIZE];
    while (1) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        if (sans_send_pkt(sock, buf, (int)n) < 0) {
            fclose(fp);
            sans_disconnect(sock);
            return -1;
        }
        if (n < sizeof(buf)) break;
    }

    fclose(fp);
    sans_disconnect(sock);
    return 0;
}
