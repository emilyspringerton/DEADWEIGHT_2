/* DEADWEIGHT_2 wire protocol v1 codec (D2 -- server-authoritative 1v1). Little-endian, explicit
 * byte ops (no struct punning, no alignment assumptions). Frame = u16 len (bytes after this
 * field) + u8 type + payload. Shape closely follows DEADWEIGHT/core/protocol.h's own real,
 * shipped precedent, but the match lifecycle itself is different: DW resolves discrete
 * simultaneous-lock rounds, DW2 has a packing phase (PLACE/PANIC_CUT/READY) followed by a
 * fixed-tick real-time combat sim driven purely by the server clock (core/combat.h's
 * dw2_ship_tick) -- the only live player input during combat is an async PANIC_CUT, not a
 * per-tick lock. */
#ifndef DW2_PROTOCOL_H
#define DW2_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define DW2_PROTO_VERSION 1
#define DW2_MAX_FRAME_LEN 512
#define DW2_MAX_AUTH_TOKEN 900   /* IDUNA ES256 JWTs are ~400-500 bytes */
#define DW2_MAX_TOKEN 200
#define DW2_NAME_LEN 16

enum {
    DW2_C_HELLO = 0x01, DW2_C_AUTH = 0x02, DW2_C_QUEUE = 0x03, DW2_C_PLACE = 0x04,
    DW2_C_PANIC_CUT = 0x05, DW2_C_READY = 0x06, DW2_C_LEAVE = 0x07, DW2_C_PING = 0x08,
    DW2_S_WELCOME = 0x81, DW2_S_QUEUED = 0x82, DW2_S_MATCH_FOUND = 0x83, DW2_S_PLACE_ACK = 0x84,
    DW2_S_PLACE_REJECT = 0x85, DW2_S_CUT_ACK = 0x86, DW2_S_CUT_REJECT = 0x87,
    DW2_S_COMBAT_START = 0x88, DW2_S_TICK = 0x89, DW2_S_MATCH_END = 0x8A, DW2_S_PONG = 0x8B,
    DW2_S_ERROR = 0x8F
};
enum { DW2_KIND_HUMAN = 0, DW2_KIND_BOT = 1 };
enum { DW2_FLAG_AUTH_REQUIRED = 1 };
enum { DW2_PL_REJ_BAD_STATE = 1, DW2_PL_REJ_ILLEGAL = 2 };
enum { DW2_CUT_REJ_BAD_STATE = 1, DW2_CUT_REJ_ILLEGAL = 2 };
enum { DW2_RES_LOSS = 0, DW2_RES_WIN = 1, DW2_RES_TIE = 2 };
enum { DW2_END_HULL = 0, DW2_END_TIMEOUT = 1, DW2_END_FORFEIT = 2, DW2_END_SERVER = 3 };
enum { DW2_ERR_BAD_PROTO = 1, DW2_ERR_AUTH = 2, DW2_ERR_BAD_FRAME = 3, DW2_ERR_BAD_STATE = 4 };

typedef struct {
    uint8_t type;
    union {
        struct { uint8_t proto, kind; char name[DW2_NAME_LEN + 1]; uint8_t token_len; uint8_t token[DW2_MAX_TOKEN]; } hello;
        struct { uint16_t token_len; uint8_t token[DW2_MAX_AUTH_TOKEN]; } auth;
        struct { uint8_t item_id, anchor_row, anchor_col, rotation; } place;
        struct { uint8_t placement_idx; } cut;
        struct { uint32_t nonce; } ping;             /* PING and PONG */
        struct { uint32_t session_id; uint8_t flags; } welcome;
        struct { uint16_t waiting; } queued;
        struct { uint32_t match_id, seed; uint8_t seat; char opp_name[DW2_NAME_LEN + 1]; uint8_t opp_kind; uint16_t pack_ms; } match_found;
        struct { uint8_t placement_idx; } place_ack;
        struct { uint8_t reason; } place_reject;
        struct { uint8_t placement_idx; } cut_ack;
        struct { uint8_t reason; } cut_reject;
        struct { uint8_t hull_you, hull_opp; uint16_t armor_you, armor_opp, cargo_you, cargo_opp; } combat_start;
        struct { uint8_t elapsed_ticks, hull_you, hull_opp; uint16_t armor_you, armor_opp; uint8_t shatter_you, shatter_opp; } tick;
        struct { uint32_t match_id; uint8_t result, reason, ticks; } match_end;
        struct { uint8_t code; } error;
    } u;
} Dw2WireMsg;

/* Encode one message. Returns total bytes written (incl. the 2-byte len), or -1 on bad input / small buffer. */
int dw2_encode(const Dw2WireMsg *m, uint8_t *buf, size_t cap);
/* Decode one frame from buf[0..len). Returns 1 = decoded (*consumed set), 0 = need more bytes, -1 = malformed
 * (oversized len, unknown type, payload size mismatch, invalid token_len). After -1 the connection must close. */
int dw2_decode(const uint8_t *buf, size_t len, Dw2WireMsg *out, size_t *consumed);
#endif
