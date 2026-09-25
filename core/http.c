#include "net.h"
#include <stdlib.h>
#include "http.h"

int dw2_parse_url(const char *url, char *host, size_t hn, int *port) {
    const char *p = url;
    if (!strncmp(p, "http://", 7)) p += 7;
    else if (strstr(p, "://")) return -1;
    size_t i = 0;
    while (*p && *p != ':' && *p != '/' && i + 1 < hn) host[i++] = *p++;
    host[i] = 0;
    if (i == 0) return -1;
    *port = 80;
    if (*p == ':') { *port = atoi(p + 1); if (*port <= 0 || *port > 65535) return -1; }
    return 0;
}

int dw2_json_str(const char *json, const char *key, char *out, size_t n) {
    char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"') { if (*p == '\\' || i + 1 >= n) return 0; out[i++] = *p++; }
    if (*p != '"') return 0;
    out[i] = 0;
    return 1;
}

int dw2_json_int(const char *json, const char *key, int *out) {
    char pat[80]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (!strncmp(p, "true", 4)) { *out = 1; return 1; }
    if (!strncmp(p, "false", 5)) { *out = 0; return 1; }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = (int)v;
    return 1;
}

static int wait_readable(dw2_sock s, int timeout_ms) {
    struct pollfd p; p.fd = s; p.events = POLLIN; p.revents = 0;
    return dw2_poll(&p, 1, timeout_ms);
}

int dw2_http(const char *method, const char *host, int port, const char *path, const char *bearer,
             const char *json_body, char *resp, size_t resp_n, int *status, int timeout_ms) {
    enum { REQ_MAX = 4096, RAW_MAX = 16384 };
    char *req = malloc(REQ_MAX), *raw = malloc(RAW_MAX);
    int rc = -1; dw2_sock s = DW2_BAD_SOCK;
    if (!req || !raw) goto done;
    const char *body = json_body ? json_body : "";
    int n = snprintf(req, REQ_MAX, "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\n%s%s%sContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                     method, path, host, bearer && *bearer ? "Authorization: Bearer " : "", bearer && *bearer ? bearer : "",
                     bearer && *bearer ? "\r\n" : "", (int)strlen(body), body);
    if (n < 0 || n >= REQ_MAX) goto done;
    size_t total = 0;
    s = dw2_connect(host, port);
    if (s == DW2_BAD_SOCK) goto done;
    for (int off = 0; off < n;) { long w = dw2_send(s, req + off, (size_t)(n - off)); if (w <= 0) goto done; off += (int)w; }
    while (total < RAW_MAX - 1) {
        if (wait_readable(s, timeout_ms) <= 0) break;
        long r = dw2_recv(s, raw + total, RAW_MAX - 1 - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    if (total == 0) goto done;
    raw[total] = 0;
    int st = 0;
    if (sscanf(raw, "HTTP/%*d.%*d %d", &st) != 1) goto done;
    *status = st;
    char *b = strstr(raw, "\r\n\r\n");
    if (!b) { if (resp_n) resp[0] = 0; rc = 0; goto done; }
    b += 4;
    if (strstr(raw, "Transfer-Encoding: chunked") || strstr(raw, "transfer-encoding: chunked")) {
        size_t o = 0; char *q = b;
        while (*q) {
            unsigned long len = strtoul(q, &q, 16);
            while (*q && *q != '\n') q++;
            if (*q) q++;
            if (len == 0) break;
            for (unsigned long i = 0; i < len && *q && o + 1 < resp_n; i++) resp[o++] = *q++;
            if (*q == '\r') q++;
            if (*q == '\n') q++;
        }
        if (resp_n) resp[o] = 0;
    } else if (resp_n) {
        strncpy(resp, b, resp_n - 1); resp[resp_n - 1] = 0;
    }
    rc = 0;
done:
    if (s != DW2_BAD_SOCK) dw2_close(s);
    free(req); free(raw);
    return rc;
}
