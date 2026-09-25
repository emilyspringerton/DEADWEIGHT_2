/* Minimal blocking HTTP/1.1 client + tiny JSON field helpers, for talking to IDUNA. Plain HTTP
 * only (same-box/trusted-network scope decision as ECOWAR's own http_client.h) -- ported from
 * DEADWEIGHT/core/http.h, trimmed of its TLS opt-in (dw2_server is not internet-facing in D2;
 * revisit alongside DEADWEIGHT's own S508e precedent if that ever changes). */
#ifndef DW2_HTTP_H
#define DW2_HTTP_H
#include <stddef.h>
/* Returns 0 with *status/resp filled (body NUL-terminated, truncated to resp_n-1), -1 on any
 * socket-level failure. */
int dw2_http(const char *method, const char *host, int port, const char *path, const char *bearer,
             const char *json_body, char *resp, size_t resp_n, int *status, int timeout_ms);
/* Extract a simple (escape-free) JSON string value: "key":"value". 1 if found and fits, else 0. */
int dw2_json_str(const char *json, const char *key, char *out, size_t n);
/* Extract a JSON integer/bool value: "key":123 or "key":true/false (true/false -> 1/0). 1 if found, else 0. */
int dw2_json_int(const char *json, const char *key, int *out);
/* Parse http://host[:port][/...] -> host/port. 0 ok, -1 unrecognized scheme (https:// is not
 * supported here -- see the doc comment above). */
int dw2_parse_url(const char *url, char *host, size_t hn, int *port);
#endif
