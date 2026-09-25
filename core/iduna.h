/* IDUNA client for the D2 game contract (game='d2', internal/games.Registry -- renamed from
 * deadweight_2, IDUNA/migrations/truestore/202609251200_rename_deadweight2_to_d2.sql):
 * agent login, token verify, match-result reporting, guest register/login. Blocking HTTP with a
 * short timeout -- callers that must not block (dw2_server) run these on a worker thread.
 *
 * V0 (D2) scope only, ported and trimmed from DEADWEIGHT/core/iduna.c: no friends/duels/steam/
 * draft-run/redeem -- this game has none of those systems yet, and speculative bindings for
 * endpoints nothing calls would be scope creep, not infra. */
#ifndef DW2_IDUNA_H
#define DW2_IDUNA_H
#include <stddef.h>

typedef struct {
    char host[128]; int port; int configured;
    char agent_name[64]; char agent_secret[256];
    char token[1536];                 /* cached agent JWT */
} Dw2Iduna;

typedef struct { char player_id[48]; char display_name[48]; int kind; /* 0 human, 1 bot */ } Dw2Identity;

typedef struct {
    unsigned match_id, seed; char seat_pid[2][48];
    int winner;                       /* 0 / 1 / 2 = tie */
    int ticks, reason;                /* reason: DW2_END_* */
} Dw2MatchReport;

/* url = http://host:port (plain HTTP only -- see http.h). secret_file: raw secret, or an
 * env-style file containing IDUNA_SECRET_<NAME>=... 0 ok. */
int dwi2_configure(Dw2Iduna *d, const char *url, const char *agent_name, const char *secret_file);
int dwi2_agent_login(Dw2Iduna *d);                                   /* 0 ok; fills d->token */
/* 0 verified, -1 IDUNA unreachable/5xx, -2 rejected (401/403/other 4xx). token is passed as a Bearer header. */
int dwi2_verify(Dw2Iduna *d, const char *token, Dw2Identity *out);
/* POST match-result as the server agent (logs in / re-logs in on 401). 0 ok (incl. duplicate), -1 failure. */
int dwi2_report(Dw2Iduna *d, const Dw2MatchReport *r);
/* Guest accounts. Return 0 ok; token written to tok (>= 1536 bytes). display_name may be "" or
 * NULL -- IDUNA auto-assigns a lore-friendly one, readable back via out_name (may be NULL).
 * -1 network/config error (out_status set 0), -2 IDUNA rejected the request (out_status set to
 * the real HTTP status; may be NULL). */
int dwi2_guest_register(Dw2Iduna *d, const char *display_name, char *out_pid, size_t pn, char *out_secret, size_t sn,
                         char *out_tok, size_t tn, char *out_name, size_t on, int *out_status);
int dwi2_guest_login(Dw2Iduna *d, const char *player_id, const char *secret, char *out_tok, size_t tn,
                      char *out_name, size_t on, int *out_status);
#endif
