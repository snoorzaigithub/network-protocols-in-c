#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include "include/sans.h"

#define EMAIL_LEN 254
#define BUF_SIZE 1024

int str_len(const char* s) {
    int len = 0;
    while (s[len] != '\0') len++;
    return len;
}

char* str_read(char* buf, int sz, FILE* f) {
    int i = 0, c;
    while (i < sz - 1) {
        if (fread(&c, 1, 1, f) != 1) {
            if (i == 0) return NULL;
            break;
        }
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return buf;
}

int smtp_agent(const char* server, int port) {
    char email[EMAIL_LEN];
    char path[EMAIL_LEN];
    char buf[BUF_SIZE];
    int conn;
    FILE* f;

    printf("Enter your email address: ");
    if (scanf("%253s", email) != 1) return -1;
    printf("Enter the file path: ");
    if (scanf("%253s", path) != 1) return -1;

    conn = sans_connect(server, port, IPPROTO_TCP);
    if (conn < 0) return -1;

    if (sans_recv_pkt(conn, buf, sizeof(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }

    snprintf(buf, sizeof(buf), "HELO %s\r\n", server);
    if (sans_send_pkt(conn, buf, str_len(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }
    if (sans_recv_pkt(conn, buf, sizeof(buf)) <= 0 || strncmp(buf, "250", 3) != 0) {
        sans_disconnect(conn);
        return -1;
    }

    snprintf(buf, sizeof(buf), "MAIL FROM: <%s>\r\n", email);
    if (sans_send_pkt(conn, buf, str_len(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }
    if (sans_recv_pkt(conn, buf, sizeof(buf)) <= 0 || strncmp(buf, "250", 3) != 0) {
        sans_disconnect(conn);
        return -1;
    }

    snprintf(buf, sizeof(buf), "RCPT TO: <%s>\r\n", email);
    if (sans_send_pkt(conn, buf, str_len(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }
    if (sans_recv_pkt(conn, buf, sizeof(buf)) <= 0 || strncmp(buf, "250", 3) != 0) {
        sans_disconnect(conn);
        return -1;
    }

    snprintf(buf, sizeof(buf), "DATA\r\n");
    if (sans_send_pkt(conn, buf, str_len(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }
    if (sans_recv_pkt(conn, buf, sizeof(buf)) <= 0 || strncmp(buf, "354", 3) != 0) {
        sans_disconnect(conn);
        return -1;
    }

    f = fopen(path, "r");
    if (!f) {
        sans_disconnect(conn);
        return -1;
    }

    while (str_read(buf, sizeof(buf), f)) {
        int len = str_len(buf);
        if (sans_send_pkt(conn, buf, len) <= 0) {
            fclose(f);
            sans_disconnect(conn);
            return -1;
        }
    }
    fclose(f);

    snprintf(buf, sizeof(buf), "\r\n.\r\n");
    if (sans_send_pkt(conn, buf, str_len(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }

    if (sans_recv_pkt(conn, buf, sizeof(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }
    printf("%s", buf);

    snprintf(buf, sizeof(buf), "QUIT\r\n");
    if (sans_send_pkt(conn, buf, str_len(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }
    if (sans_recv_pkt(conn, buf, sizeof(buf)) <= 0) {
        sans_disconnect(conn);
        return -1;
    }
    printf("%s", buf);

    sans_disconnect(conn);
    return 0;
}
