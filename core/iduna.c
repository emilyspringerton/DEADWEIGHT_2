#include "net.h"
#include <ctype.h>
#include <stdlib.h>
#include "iduna.h"
#include "http.h"

#define TMO 3000
#define GAME "/api/v1/games/d2" /* renamed from deadweight_2 -- IDUNA/migrations/truestore/202609251200_rename_deadweight2_to_d2.sql */

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    size_t i = 0; while (isspace((unsigned char)s[i])) i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
}

int dwi2_configure(Dw2Iduna *d, const char *url, const char *agent_name, const char *secret_file) {
    memset(d, 0, sizeof *d);
    if (dw2_parse_url(url, d->host, sizeof d->host, &d->port) != 0) return -1;
    snprintf(d->agent_name, sizeof d->agent_name, "%s", agent_name ? agent_name : "");
    if (secret_file && *secret_file) {
        FILE *f = fopen(secret_file, "r"); if (!f) return -2;
        char key[96] = "IDUNA_SECRET_", line[512], whole[256] = ""; size_t k = strlen(key);
        for (const char *p = d->agent_name; *p && k + 1 < sizeof key; p++) key[k++] = *p == '-' ? '_' : (char)toupper((unsigned char)*p);
        key[k] = 0;
        int found = 0, lines = 0;
        while (fgets(line, sizeof line, f)) {
            lines++;
            trim(line);
            if (!strncmp(line, "export ", 7)) memmove(line, line + 7, strlen(line + 7) + 1);   /* agent-secrets.env uses `export KEY=...` */
            if (!strncmp(line, key, k) && line[k] == '=') { snprintf(d->agent_secret, sizeof d->agent_secret, "%.255s", line + k + 1); found = 1; }
            else if (lines == 1) snprintf(whole, sizeof whole, "%.255s", line);
        }
        fclose(f);
        if (!found && lines == 1 && !strchr(whole, '=')) snprintf(d->agent_secret, sizeof d->agent_secret, "%s", whole), found = 1;
        if (!found) return -3;
        size_t n = strlen(d->agent_secret);
        if (n >= 2 && (d->agent_secret[0] == '"' || d->agent_secret[0] == '\'') && d->agent_secret[n - 1] == d->agent_secret[0]) { memmove(d->agent_secret, d->agent_secret + 1, n - 2); d->agent_secret[n - 2] = 0; }
    }
    d->configured = 1;
    return 0;
}

int dwi2_agent_login(Dw2Iduna *d) {
    char body[512], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"agent_name\":\"%s\",\"agent_secret\":\"%s\"}", d->agent_name, d->agent_secret);
    if (dw2_http("POST", d->host, d->port, "/api/v1/auth/agent", NULL, body, resp, sizeof resp, &st, TMO) != 0 || st != 200) return -1;
    if (!dw2_json_str(resp, "access_token", d->token, sizeof d->token)) return -1;
    return 0;
}

int dwi2_verify(Dw2Iduna *d, const char *token, Dw2Identity *out) {
    char resp[2048], kind[16]; int st = 0;
    if (dw2_http("POST", d->host, d->port, GAME "/verify", token, "", resp, sizeof resp, &st, TMO) != 0) return -1;
    if (st >= 500) return -1;
    if (st != 200) return -2;
    memset(out, 0, sizeof *out);
    if (!dw2_json_str(resp, "player_id", out->player_id, sizeof out->player_id)) return -2;
    dw2_json_str(resp, "display_name", out->display_name, sizeof out->display_name);
    out->kind = (dw2_json_str(resp, "kind", kind, sizeof kind) && !strcmp(kind, "bot")) ? 1 : 0;
    return 0;
}

int dwi2_report(Dw2Iduna *d, const Dw2MatchReport *r) {
    static const char *const reasons[] = { "hull", "timeout", "forfeit", "server" };
    char body[512], resp[2048]; int st = 0;
    snprintf(body, sizeof body, "{\"match_id\":%u,\"seed\":%u,\"seat0_player_id\":\"%s\",\"seat1_player_id\":\"%s\",\"winner\":%d,\"rounds\":%d,\"reason\":\"%s\"}",
             r->match_id, r->seed, r->seat_pid[0], r->seat_pid[1], r->winner, r->ticks, reasons[r->reason & 3]);
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!d->token[0] && dwi2_agent_login(d) != 0) return -1;
        if (dw2_http("POST", d->host, d->port, GAME "/match-result", d->token, body, resp, sizeof resp, &st, TMO) != 0) return -1;
        if (st == 401) { d->token[0] = 0; continue; }
        return st == 200 ? 0 : -1;
    }
    return -1;
}

int dwi2_guest_register(Dw2Iduna *d, const char *display_name, char *pid, size_t pn, char *secret, size_t sn,
                         char *tok, size_t tn, char *out_name, size_t on, int *out_status) {
    char body[128], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"display_name\":\"%s\"}", display_name ? display_name : "");
    if (dw2_http("POST", d->host, d->port, GAME "/guest-register", NULL, body, resp, sizeof resp, &st, TMO) != 0) { if (out_status) *out_status = 0; return -1; }
    if (out_status) *out_status = st;
    if (st != 201) return -2;
    if (!dw2_json_str(resp, "player_id", pid, pn) || !dw2_json_str(resp, "guest_secret", secret, sn) || !dw2_json_str(resp, "token", tok, tn)) return -1;
    if (out_name) dw2_json_str(resp, "display_name", out_name, on);
    return 0;
}

int dwi2_guest_login(Dw2Iduna *d, const char *pid, const char *secret, char *tok, size_t tn, char *out_name, size_t on, int *out_status) {
    char body[256], resp[4096]; int st = 0;
    snprintf(body, sizeof body, "{\"player_id\":\"%s\",\"guest_secret\":\"%s\"}", pid, secret);
    if (dw2_http("POST", d->host, d->port, GAME "/guest-login", NULL, body, resp, sizeof resp, &st, TMO) != 0) { if (out_status) *out_status = 0; return -1; }
    if (out_status) *out_status = st;
    if (st != 200) return -2;
    if (!dw2_json_str(resp, "token", tok, tn)) return -1;
    if (out_name) dw2_json_str(resp, "display_name", out_name, on);
    return 0;
}
