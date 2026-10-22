#include "include/sans.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUF 1024

static int headers_end(const char *b, int n) {
  if (n < 4) return -1;
  for (int i = 3; i < n; ++i)
    if (b[i-3]=='\r' && b[i-2]=='\n' && b[i-1]=='\r' && b[i]=='\n') return i + 1;
  return -1;
}

static long parse_content_length(const char *b, int n) {
  const char *U = "Content-Length:", *L = "content-length:";
  int Llen = (int)strlen(U);
  int i = 0;
  while (i < n) {
    int j = i;
    while (j < n && !(b[j]=='\r' && j+1<n && b[j+1]=='\n')) j++;
    int linelen = j - i;
    if (linelen >= Llen && (strncmp(b+i,U,Llen)==0 || strncmp(b+i,L,Llen)==0)) {
      int k = i + Llen;
      while (k < j && (b[k]==' '||b[k]=='\t')) k++;
      char num[32]; int m = 0;
      while (k < j && m < (int)sizeof(num)-1 && b[k]>='0' && b[k]<='9') num[m++] = b[k++];
      num[m] = '\0';
      if (m > 0) return strtol(num,NULL,10);
    }
    i = (j+2<=n) ? j+2 : n;
  }
  return -1;
}

int http_client(const char* host, int port) {
  char method_in[16] = {0}, path[512] = {0};
  if (scanf("%15s %511s", method_in, path) != 2) return -1;
  if (path[0] == '/') {
    size_t i=0,j=1;
    while (path[j] && i+1<sizeof(path)) path[i++] = path[j++];
    path[i] = '\0';
  }
  int sock = sans_connect(host, port, 6);
  if (sock < 0) return -1;
  char req[BUF];
  int req_len = snprintf(req, sizeof(req),
    "GET /%s HTTP/1.1\r\n"
    "Host: %s:%d\r\n"
    "User-Agent: sans/1.0\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: close\r\n"
    "Accept: */*\r\n"
    "\r\n",
    path, host, port);
  if (req_len <= 0 || req_len >= (int)sizeof(req)) { sans_disconnect(sock); return -1; }
  if (sans_send_pkt(sock, req, req_len) < 0) { sans_disconnect(sock); return -1; }
  char buf[BUF];
  int r = sans_recv_pkt(sock, buf, sizeof(buf));
  if (r <= 0) { sans_disconnect(sock); return -1; }
  printf("%.*s", r, buf);
  int hdr_end = headers_end(buf, r);
  long content_len = -1, body_read = 0;
  if (hdr_end >= 0) { content_len = parse_content_length(buf, hdr_end); body_read = r - hdr_end; }
  if (content_len >= 0) {
    while (body_read < content_len) {
      r = sans_recv_pkt(sock, buf, sizeof(buf));
      if (r <= 0) break;
      printf("%.*s", r, buf);
      body_read += r;
    }
  } else {
    while ((r = sans_recv_pkt(sock, buf, sizeof(buf))) > 0) printf("%.*s", r, buf);
  }
  sans_disconnect(sock);
  return 0;
}
