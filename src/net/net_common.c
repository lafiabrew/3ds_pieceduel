#include "pd_net.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Internal mode-specific hooks implemented in net_p2p.c, net_uds.c, net_web.c */
extern void pd_net_p2p_close(pd_net_ctx_t *ctx);
extern bool pd_net_p2p_poll(pd_net_ctx_t *ctx);
extern bool pd_net_p2p_send_line(pd_net_ctx_t *ctx, const char *line);

extern void pd_net_uds_close(pd_net_ctx_t *ctx);
extern bool pd_net_uds_poll(pd_net_ctx_t *ctx);
extern bool pd_net_uds_send_line(pd_net_ctx_t *ctx, const char *line);

extern void pd_net_web_close(pd_net_ctx_t *ctx);
extern bool pd_net_web_poll(pd_net_ctx_t *ctx);
extern bool pd_net_web_send_move(pd_net_ctx_t *ctx, pd_move_t move);
extern bool pd_net_web_send_preview(pd_net_ctx_t *ctx, int cell);
extern bool pd_net_web_send_rematch(pd_net_ctx_t *ctx);
extern bool pd_net_web_send_resign(pd_net_ctx_t *ctx);
extern bool pd_net_web_send_draw_offer(pd_net_ctx_t *ctx);
extern bool pd_net_web_send_draw_respond(pd_net_ctx_t *ctx, bool accept);
extern bool pd_net_web_send_chat(pd_net_ctx_t *ctx, const char *text);

/*
 * Weak fallback implementations of pd_create_game and pd_apply_move so
 * src/net/ files can be linked standalone in isolated network tests even if
 * src/engine/ files are not linked. When linked with src/engine/, the strong
 * engine symbols override these automatically.
 */
static const int s_fallback_lines[PD_ADV_LINES_COUNT][4] = {
    /* 10 standard lines */
    {0, 1, 2, 3},   {4, 5, 6, 7},   {8, 9, 10, 11}, {12, 13, 14, 15},
    {0, 4, 8, 12},  {1, 5, 9, 13},  {2, 6, 10, 14}, {3, 7, 11, 15},
    {0, 5, 10, 15}, {3, 6, 9, 12},
    /* 9 2x2 squares (advanced) */
    {0, 1, 4, 5},   {1, 2, 5, 6},   {2, 3, 6, 7},
    {4, 5, 8, 9},   {5, 6, 9, 10},  {6, 7, 10, 11},
    {8, 9, 12, 13}, {9, 10, 13, 14},{10, 11, 14, 15}
};

__attribute__((weak)) void pd_create_game(pd_game_state_t *out, int starting_player, pd_variant_t variant) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        out->board[i] = PD_NONE;
        out->available[i] = i;
    }
    out->available_count = PD_PIECE_COUNT;
    out->current = (starting_player == 1) ? 1 : 0;
    out->piece_in_hand = PD_NONE;
    out->status = PD_STATUS_PLAYING;
    out->winner = PD_NONE;
    out->has_winning_line = false;
    out->move_count = 0;
    out->variant = variant;
}

__attribute__((weak)) bool pd_apply_move(const pd_game_state_t *state, pd_move_t move, pd_game_state_t *out) {
    if (!state || !out || state->status != PD_STATUS_PLAYING) return false;

    pd_game_state_t next = *state;

    if (state->piece_in_hand == PD_NONE) {
        if (move.kind != PD_MOVE_OPEN) return false;
        if (move.give < 0 || move.give >= PD_PIECE_COUNT) return false;
        int found = -1;
        for (int i = 0; i < next.available_count; i++) {
            if (next.available[i] == move.give) {
                found = i;
                break;
            }
        }
        if (found < 0) return false;
        for (int i = found; i < next.available_count - 1; i++) {
            next.available[i] = next.available[i + 1];
        }
        next.available_count--;
        next.piece_in_hand = move.give;
        next.current = (state->current == 0) ? 1 : 0;
        next.move_count = state->move_count + 1;
        *out = next;
        return true;
    }

    if (move.kind != PD_MOVE_PLACE) return false;
    if (move.cell < 0 || move.cell >= PD_BOARD_SIZE) return false;
    if (next.board[move.cell] != PD_NONE) return false;

    next.board[move.cell] = state->piece_in_hand;

    int line_count = (state->variant == PD_VARIANT_ADVANCED) ? PD_ADV_LINES_COUNT : PD_STD_LINES_COUNT;
    bool won = false;
    pd_line_t win_line = {{0, 0, 0, 0}};
    for (int i = 0; i < line_count; i++) {
        int a = s_fallback_lines[i][0];
        int b = s_fallback_lines[i][1];
        int c = s_fallback_lines[i][2];
        int d = s_fallback_lines[i][3];
        int pa = next.board[a];
        int pb = next.board[b];
        int pc = next.board[c];
        int pd = next.board[d];
        if (pa == PD_NONE || pb == PD_NONE || pc == PD_NONE || pd == PD_NONE) continue;
        int share_ones = pa & pb & pc & pd;
        int share_zeros = (~pa) & (~pb) & (~pc) & (~pd) & 0xF;
        if (share_ones != 0 || share_zeros != 0) {
            won = true;
            win_line.cells[0] = a;
            win_line.cells[1] = b;
            win_line.cells[2] = c;
            win_line.cells[3] = d;
            break;
        }
    }

    if (won) {
        next.piece_in_hand = PD_NONE;
        next.status = PD_STATUS_WIN;
        next.winner = (state->variant == PD_VARIANT_MISERE)
                          ? ((state->current == 0) ? 1 : 0)
                          : state->current;
        next.has_winning_line = true;
        next.winning_line = win_line;
        next.move_count = state->move_count + 1;
        *out = next;
        return true;
    }

    bool full = true;
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        if (next.board[i] == PD_NONE) {
            full = false;
            break;
        }
    }
    if (full) {
        next.piece_in_hand = PD_NONE;
        next.status = PD_STATUS_DRAW;
        next.winner = PD_NONE;
        next.has_winning_line = false;
        next.move_count = state->move_count + 1;
        *out = next;
        return true;
    }

    if (move.give < 0 || move.give >= PD_PIECE_COUNT) return false;
    int found = -1;
    for (int i = 0; i < next.available_count; i++) {
        if (next.available[i] == move.give) {
            found = i;
            break;
        }
    }
    if (found < 0) return false;
    for (int i = found; i < next.available_count - 1; i++) {
        next.available[i] = next.available[i + 1];
    }
    next.available_count--;
    next.piece_in_hand = move.give;
    next.current = (state->current == 0) ? 1 : 0;
    next.move_count = state->move_count + 1;
    *out = next;
    return true;
}

static const char *variant_name(pd_variant_t v) {
    switch (v) {
        case PD_VARIANT_MISERE:
            return "misere";
        case PD_VARIANT_ADVANCED:
            return "advanced";
        case PD_VARIANT_STANDARD:
        default:
            return "standard";
    }
}

static pd_variant_t parse_variant_name(const char *s) {
    if (!s) return PD_VARIANT_STANDARD;
    if (strncmp(s, "misere", 6) == 0) return PD_VARIANT_MISERE;
    if (strncmp(s, "advanced", 8) == 0) return PD_VARIANT_ADVANCED;
    return PD_VARIANT_STANDARD;
}

static const char *status_name(pd_status_t st) {
    switch (st) {
        case PD_STATUS_WIN:
            return "win";
        case PD_STATUS_DRAW:
            return "draw";
        case PD_STATUS_PLAYING:
        default:
            return "playing";
    }
}

void pd_net_init(pd_net_ctx_t *ctx, const char *local_name) {
    if (!ctx) return;
    char saved_name[32] = {0};
    if (local_name && local_name[0] != '\0') {
        snprintf(saved_name, sizeof(saved_name), "%s", local_name);
    } else {
        snprintf(saved_name, sizeof(saved_name), "3DS_Player");
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->mode = PD_NET_MODE_NONE;
    ctx->state = PD_NET_STATE_IDLE;
    ctx->udp_fd = -1;
    ctx->listen_fd = -1;
    ctx->peer_fd = -1;
    ctx->preview_cell = PD_NONE;
    ctx->preview_piece = PD_NONE;
    snprintf(ctx->local_name, sizeof(ctx->local_name), "%s", saved_name);
    snprintf(ctx->server_host, sizeof(ctx->server_host), "127.0.0.1");
    ctx->server_port = PD_WEB_DEFAULT_PORT;
    pd_create_game(&ctx->game_state, 0, PD_VARIANT_STANDARD);
}

void pd_net_close(pd_net_ctx_t *ctx) {
    if (!ctx) return;
    pd_net_p2p_close(ctx);
    pd_net_uds_close(ctx);
    pd_net_web_close(ctx);
    if (ctx->udp_fd >= 0) {
        close(ctx->udp_fd);
        ctx->udp_fd = -1;
    }
    if (ctx->listen_fd >= 0) {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
    }
    if (ctx->peer_fd >= 0) {
        close(ctx->peer_fd);
        ctx->peer_fd = -1;
    }
    ctx->mode = PD_NET_MODE_NONE;
    ctx->state = PD_NET_STATE_IDLE;
    ctx->ws_handshake_done = false;
    ctx->sio_connected = false;
    ctx->rx_len = 0;
}

bool pd_net_poll(pd_net_ctx_t *ctx) {
    if (!ctx) return false;
    switch (ctx->mode) {
        case PD_NET_MODE_UDS:
            return pd_net_uds_poll(ctx);
        case PD_NET_MODE_LAN_P2P:
            return pd_net_p2p_poll(ctx);
        case PD_NET_MODE_WEB:
            return pd_net_web_poll(ctx);
        case PD_NET_MODE_NONE:
        default:
            return false;
    }
}

/* JSON helpers */

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Find `"key":` in a JSON object string and return pointer to the value start. */
static const char *find_json_key(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pattern[64];
    int plen = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (plen <= 0 || (size_t)plen >= sizeof(pattern)) return NULL;

    const char *p = json;
    while ((p = strstr(p, pattern)) != NULL) {
        const char *after = skip_ws(p + plen);
        if (*after == ':') {
            return skip_ws(after + 1);
        }
        p += plen;
    }
    return NULL;
}

static bool parse_json_int_or_null(const char *val, int *out_val, bool *is_null, const char **end_ptr) {
    val = skip_ws(val);
    if (!val || !*val) return false;
    if (strncmp(val, "null", 4) == 0) {
        if (is_null) *is_null = true;
        if (out_val) *out_val = PD_NONE;
        if (end_ptr) *end_ptr = val + 4;
        return true;
    }
    char *end = NULL;
    long v = strtol(val, &end, 10);
    if (end == val) return false;
    if (is_null) *is_null = false;
    if (out_val) *out_val = (int)v;
    if (end_ptr) *end_ptr = end;
    return true;
}

int pd_net_format_move_json(pd_move_t move, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return -1;
    int written = -1;
    if (move.kind == PD_MOVE_OPEN) {
        written = snprintf(buf, buf_size, "{\"kind\":\"open\",\"give\":%d}", move.give);
    } else if (move.kind == PD_MOVE_PLACE) {
        if (move.give == PD_NONE || move.give < 0) {
            written = snprintf(buf, buf_size, "{\"kind\":\"place\",\"cell\":%d,\"give\":null}", move.cell);
        } else {
            written = snprintf(buf, buf_size, "{\"kind\":\"place\",\"cell\":%d,\"give\":%d}", move.cell, move.give);
        }
    }
    if (written < 0 || (size_t)written >= buf_size) return -1;
    return written;
}

bool pd_net_parse_move_json(const char *json, pd_move_t *out_move) {
    if (!json || !out_move) return false;
    const char *kind_val = find_json_key(json, "kind");
    if (!kind_val || *kind_val != '"') return false;
    kind_val++;

    if (strncmp(kind_val, "open\"", 5) == 0) {
        const char *give_val = find_json_key(json, "give");
        if (!give_val) return false;
        int give = PD_NONE;
        bool is_null = false;
        if (!parse_json_int_or_null(give_val, &give, &is_null, NULL) || is_null) return false;
        if (give < 0 || give >= PD_PIECE_COUNT) return false;
        out_move->kind = PD_MOVE_OPEN;
        out_move->cell = PD_NONE;
        out_move->give = give;
        return true;
    } else if (strncmp(kind_val, "place\"", 6) == 0) {
        const char *cell_val = find_json_key(json, "cell");
        const char *give_val = find_json_key(json, "give");
        if (!cell_val || !give_val) return false;
        int cell = PD_NONE;
        bool cell_null = false;
        if (!parse_json_int_or_null(cell_val, &cell, &cell_null, NULL) || cell_null) return false;
        if (cell < 0 || cell >= PD_BOARD_SIZE) return false;

        int give = PD_NONE;
        bool give_null = false;
        if (!parse_json_int_or_null(give_val, &give, &give_null, NULL)) return false;
        if (!give_null && (give < 0 || give >= PD_PIECE_COUNT)) return false;

        out_move->kind = PD_MOVE_PLACE;
        out_move->cell = cell;
        out_move->give = give_null ? PD_NONE : give;
        return true;
    }

    return false;
}

int pd_net_format_state_json(const pd_game_state_t *state, char *buf, size_t buf_size) {
    if (!state || !buf || buf_size == 0) return -1;
    size_t pos = 0;
    int n = snprintf(buf + pos, buf_size - pos, "{\"board\":[");
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += (size_t)n;

    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        if (state->board[i] == PD_NONE) {
            n = snprintf(buf + pos, buf_size - pos, "%snull", (i > 0) ? "," : "");
        } else {
            n = snprintf(buf + pos, buf_size - pos, "%s%d", (i > 0) ? "," : "", state->board[i]);
        }
        if (n < 0 || (size_t)n >= buf_size - pos) return -1;
        pos += (size_t)n;
    }

    if (state->piece_in_hand == PD_NONE) {
        n = snprintf(buf + pos, buf_size - pos, "],\"current\":%d,\"pieceInHand\":null,\"available\":[", state->current);
    } else {
        n = snprintf(buf + pos, buf_size - pos, "],\"current\":%d,\"pieceInHand\":%d,\"available\":[",
                     state->current, state->piece_in_hand);
    }
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += (size_t)n;

    for (int i = 0; i < state->available_count; i++) {
        n = snprintf(buf + pos, buf_size - pos, "%s%d", (i > 0) ? "," : "", state->available[i]);
        if (n < 0 || (size_t)n >= buf_size - pos) return -1;
        pos += (size_t)n;
    }

    n = snprintf(buf + pos, buf_size - pos, "],\"status\":\"%s\",\"winner\":", status_name(state->status));
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += (size_t)n;

    if (state->winner == PD_NONE) {
        n = snprintf(buf + pos, buf_size - pos, "null,\"winningLine\":");
    } else {
        n = snprintf(buf + pos, buf_size - pos, "%d,\"winningLine\":", state->winner);
    }
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += (size_t)n;

    if (!state->has_winning_line) {
        n = snprintf(buf + pos, buf_size - pos, "null");
    } else {
        n = snprintf(buf + pos, buf_size - pos, "[%d,%d,%d,%d]",
                     state->winning_line.cells[0],
                     state->winning_line.cells[1],
                     state->winning_line.cells[2],
                     state->winning_line.cells[3]);
    }
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += (size_t)n;

    n = snprintf(buf + pos, buf_size - pos, ",\"moveCount\":%d,\"variant\":\"%s\"}",
                 state->move_count, variant_name(state->variant));
    if (n < 0 || (size_t)n >= buf_size - pos) return -1;
    pos += (size_t)n;

    return (int)pos;
}

bool pd_net_parse_state_json(const char *json, pd_game_state_t *out_state) {
    if (!json || !out_state) return false;

    pd_game_state_t st;
    pd_create_game(&st, 0, PD_VARIANT_STANDARD);

    /* Parse board */
    const char *board_val = find_json_key(json, "board");
    if (!board_val || *board_val != '[') return false;
    const char *p = board_val + 1;
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        p = skip_ws(p);
        int cell_v = PD_NONE;
        bool is_null = false;
        const char *next_p = NULL;
        if (!parse_json_int_or_null(p, &cell_v, &is_null, &next_p)) return false;
        st.board[i] = is_null ? PD_NONE : cell_v;
        p = skip_ws(next_p);
        if (i < PD_BOARD_SIZE - 1) {
            if (*p != ',') return false;
            p++;
        }
    }
    p = skip_ws(p);
    if (*p != ']') return false;

    /* Parse current */
    const char *cur_val = find_json_key(json, "current");
    if (!cur_val) return false;
    int cur = 0;
    bool cur_null = false;
    if (!parse_json_int_or_null(cur_val, &cur, &cur_null, NULL) || cur_null || (cur != 0 && cur != 1)) {
        return false;
    }
    st.current = cur;

    /* Parse pieceInHand */
    const char *pih_val = find_json_key(json, "pieceInHand");
    if (!pih_val) return false;
    int pih = PD_NONE;
    bool pih_null = false;
    if (!parse_json_int_or_null(pih_val, &pih, &pih_null, NULL)) return false;
    st.piece_in_hand = pih_null ? PD_NONE : pih;

    /* Parse available */
    const char *avail_val = find_json_key(json, "available");
    if (!avail_val || *avail_val != '[') return false;
    p = skip_ws(avail_val + 1);
    st.available_count = 0;
    while (*p && *p != ']') {
        if (st.available_count >= PD_PIECE_COUNT) return false;
        int piece_id = 0;
        bool is_null = false;
        const char *next_p = NULL;
        if (!parse_json_int_or_null(p, &piece_id, &is_null, &next_p) || is_null) return false;
        st.available[st.available_count++] = piece_id;
        p = skip_ws(next_p);
        if (*p == ',') {
            p = skip_ws(p + 1);
        }
    }
    if (*p != ']') return false;

    /* Parse status */
    const char *status_val = find_json_key(json, "status");
    if (!status_val || *status_val != '"') return false;
    status_val++;
    if (strncmp(status_val, "playing\"", 8) == 0) {
        st.status = PD_STATUS_PLAYING;
    } else if (strncmp(status_val, "win\"", 4) == 0) {
        st.status = PD_STATUS_WIN;
    } else if (strncmp(status_val, "draw\"", 5) == 0) {
        st.status = PD_STATUS_DRAW;
    } else {
        return false;
    }

    /* Parse winner */
    const char *winner_val = find_json_key(json, "winner");
    if (!winner_val) return false;
    int winner = PD_NONE;
    bool winner_null = false;
    if (!parse_json_int_or_null(winner_val, &winner, &winner_null, NULL)) return false;
    st.winner = winner_null ? PD_NONE : winner;

    /* Parse winningLine */
    const char *wline_val = find_json_key(json, "winningLine");
    if (!wline_val) return false;
    if (strncmp(wline_val, "null", 4) == 0) {
        st.has_winning_line = false;
    } else if (*wline_val == '[') {
        p = wline_val + 1;
        for (int i = 0; i < 4; i++) {
            int c = 0;
            bool c_null = false;
            const char *next_p = NULL;
            if (!parse_json_int_or_null(p, &c, &c_null, &next_p) || c_null) return false;
            st.winning_line.cells[i] = c;
            p = skip_ws(next_p);
            if (i < 3) {
                if (*p != ',') return false;
                p++;
            }
        }
        st.has_winning_line = true;
    } else {
        return false;
    }

    /* Parse moveCount */
    const char *mc_val = find_json_key(json, "moveCount");
    if (!mc_val) return false;
    int mc = 0;
    bool mc_null = false;
    if (!parse_json_int_or_null(mc_val, &mc, &mc_null, NULL) || mc_null) return false;
    st.move_count = mc;

    /* Parse optional variant */
    const char *var_val = find_json_key(json, "variant");
    if (var_val && *var_val == '"') {
        st.variant = parse_variant_name(var_val + 1);
    } else {
        st.variant = PD_VARIANT_STANDARD;
    }

    *out_state = st;
    return true;
}

/* Shared line-framed packet handler for UDS and LAN P2P */
static bool send_lockstep_line(pd_net_ctx_t *ctx, const char *line) {
    if (ctx->mode == PD_NET_MODE_LAN_P2P) {
        return pd_net_p2p_send_line(ctx, line);
    }
    if (ctx->mode == PD_NET_MODE_UDS) {
        return pd_net_uds_send_line(ctx, line);
    }
    return false;
}

void pd_net_handle_packet_line(pd_net_ctx_t *ctx, const char *line) {
    if (!ctx || !line || line[0] == '\0') return;

    if (strncmp(line, "HELLO|", 6) == 0) {
        snprintf(ctx->opponent_name, sizeof(ctx->opponent_name), "%s", line + 6);
        ctx->state = PD_NET_STATE_ACTIVE;
        ctx->state_updated = true;
        if (ctx->is_host) {
            char hello_pkt[128];
            snprintf(hello_pkt, sizeof(hello_pkt), "HELLO|%s\n", ctx->local_name);
            send_lockstep_line(ctx, hello_pkt);

            char state_json[1024];
            if (pd_net_format_state_json(&ctx->game_state, state_json, sizeof(state_json)) > 0) {
                char state_pkt[1152];
                snprintf(state_pkt, sizeof(state_pkt), "STATE|%s\n", state_json);
                send_lockstep_line(ctx, state_pkt);
            }
        }
        return;
    }

    if (strncmp(line, "STATE|", 6) == 0) {
        if (pd_net_parse_state_json(line + 6, &ctx->game_state)) {
            ctx->variant = ctx->game_state.variant;
            ctx->preview_cell = PD_NONE;
            ctx->preview_piece = PD_NONE;
            ctx->state = (ctx->game_state.status == PD_STATUS_PLAYING)
                             ? PD_NET_STATE_ACTIVE
                             : PD_NET_STATE_FINISHED;
            ctx->state_updated = true;
        }
        return;
    }

    if (strncmp(line, "MOVE|", 5) == 0) {
        pd_move_t move;
        if (pd_net_parse_move_json(line + 5, &move)) {
            pd_game_state_t next;
            if (pd_apply_move(&ctx->game_state, move, &next)) {
                ctx->game_state = next;
                ctx->preview_cell = PD_NONE;
                ctx->preview_piece = PD_NONE;
                ctx->draw_offered_by_me = false;
                ctx->draw_offered_by_peer = false;
                ctx->state_updated = true;
                if (ctx->game_state.status != PD_STATUS_PLAYING) {
                    ctx->state = PD_NET_STATE_FINISHED;
                }
            }
        }
        return;
    }

    if (strncmp(line, "PREVIEW|", 8) == 0) {
        const char *p = line + 8;
        const char *sep = strchr(p, '|');
        if (sep) {
            int cell = atoi(p);
            int piece = atoi(sep + 1);
            ctx->preview_cell = (cell >= 0 && cell < PD_BOARD_SIZE) ? cell : PD_NONE;
            ctx->preview_piece = (piece >= 0 && piece < PD_PIECE_COUNT) ? piece : PD_NONE;
        }
        return;
    }

    if (strcmp(line, "REMATCH") == 0) {
        ctx->rematch_offered_by_peer = true;
        if (ctx->rematch_offered_by_me && ctx->rematch_offered_by_peer) {
            ctx->rematch_offered_by_me = false;
            ctx->rematch_offered_by_peer = false;
            ctx->draw_offered_by_me = false;
            ctx->draw_offered_by_peer = false;
            ctx->preview_cell = PD_NONE;
            ctx->preview_piece = PD_NONE;
            pd_create_game(&ctx->game_state, 0, ctx->variant);
            ctx->state = PD_NET_STATE_ACTIVE;
            ctx->state_updated = true;
        }
        return;
    }

    if (strncmp(line, "RESIGN|", 7) == 0) {
        int seat = atoi(line + 7);
        ctx->game_state.status = PD_STATUS_WIN;
        ctx->game_state.winner = (seat == 0) ? 1 : 0;
        ctx->game_state.piece_in_hand = PD_NONE;
        ctx->draw_offered_by_me = false;
        ctx->draw_offered_by_peer = false;
        ctx->state = PD_NET_STATE_FINISHED;
        ctx->state_updated = true;
        return;
    }

    if (strncmp(line, "DRAW_OFFER|", 11) == 0) {
        ctx->draw_offered_by_peer = true;
        return;
    }

    if (strncmp(line, "DRAW_RESPOND|", 13) == 0) {
        int accept = atoi(line + 13);
        ctx->draw_offered_by_me = false;
        ctx->draw_offered_by_peer = false;
        if (accept == 1) {
            ctx->game_state.status = PD_STATUS_DRAW;
            ctx->game_state.winner = PD_NONE;
            ctx->game_state.piece_in_hand = PD_NONE;
            ctx->state = PD_NET_STATE_FINISHED;
            ctx->state_updated = true;
        }
        return;
    }

    if (strncmp(line, "CHAT|", 5) == 0) {
        const char *p = line + 5;
        const char *sep = strchr(p, '|');
        if (sep) {
            size_t slen = (size_t)(sep - p);
            if (slen >= sizeof(ctx->last_chat_sender)) {
                slen = sizeof(ctx->last_chat_sender) - 1;
            }
            memcpy(ctx->last_chat_sender, p, slen);
            ctx->last_chat_sender[slen] = '\0';
            snprintf(ctx->last_chat, sizeof(ctx->last_chat), "%s", sep + 1);
        }
        return;
    }
}

/* Mode-agnostic in-game action dispatchers */

bool pd_net_send_move(pd_net_ctx_t *ctx, pd_move_t move) {
    if (!ctx) return false;
    if (ctx->mode == PD_NET_MODE_WEB) {
        return pd_net_web_send_move(ctx, move);
    }
    if (ctx->mode == PD_NET_MODE_LAN_P2P || ctx->mode == PD_NET_MODE_UDS) {
        pd_game_state_t next;
        if (!pd_apply_move(&ctx->game_state, move, &next)) {
            return false;
        }
        char move_json[128];
        if (pd_net_format_move_json(move, move_json, sizeof(move_json)) < 0) {
            return false;
        }
        char pkt[256];
        snprintf(pkt, sizeof(pkt), "MOVE|%s\n", move_json);
        if (!send_lockstep_line(ctx, pkt)) {
            return false;
        }
        ctx->game_state = next;
        ctx->preview_cell = PD_NONE;
        ctx->preview_piece = PD_NONE;
        ctx->draw_offered_by_me = false;
        ctx->draw_offered_by_peer = false;
        ctx->state_updated = true;
        if (ctx->game_state.status != PD_STATUS_PLAYING) {
            ctx->state = PD_NET_STATE_FINISHED;
        }
        return true;
    }
    return false;
}

bool pd_net_send_preview(pd_net_ctx_t *ctx, int cell) {
    if (!ctx) return false;
    if (ctx->mode == PD_NET_MODE_WEB) {
        return pd_net_web_send_preview(ctx, cell);
    }
    if (ctx->mode == PD_NET_MODE_LAN_P2P || ctx->mode == PD_NET_MODE_UDS) {
        int valid_cell = (cell >= 0 && cell < PD_BOARD_SIZE) ? cell : PD_NONE;
        int piece = (valid_cell != PD_NONE) ? ctx->game_state.piece_in_hand : PD_NONE;
        char pkt[64];
        snprintf(pkt, sizeof(pkt), "PREVIEW|%d|%d\n", valid_cell, piece);
        return send_lockstep_line(ctx, pkt);
    }
    return false;
}

bool pd_net_send_rematch(pd_net_ctx_t *ctx) {
    if (!ctx) return false;
    if (ctx->mode == PD_NET_MODE_WEB) {
        return pd_net_web_send_rematch(ctx);
    }
    if (ctx->mode == PD_NET_MODE_LAN_P2P || ctx->mode == PD_NET_MODE_UDS) {
        ctx->rematch_offered_by_me = true;
        if (!send_lockstep_line(ctx, "REMATCH\n")) {
            return false;
        }
        if (ctx->rematch_offered_by_me && ctx->rematch_offered_by_peer) {
            ctx->rematch_offered_by_me = false;
            ctx->rematch_offered_by_peer = false;
            ctx->draw_offered_by_me = false;
            ctx->draw_offered_by_peer = false;
            ctx->preview_cell = PD_NONE;
            ctx->preview_piece = PD_NONE;
            pd_create_game(&ctx->game_state, 0, ctx->variant);
            ctx->state = PD_NET_STATE_ACTIVE;
            ctx->state_updated = true;
        }
        return true;
    }
    return false;
}

bool pd_net_send_resign(pd_net_ctx_t *ctx) {
    if (!ctx) return false;
    if (ctx->mode == PD_NET_MODE_WEB) {
        return pd_net_web_send_resign(ctx);
    }
    if (ctx->mode == PD_NET_MODE_LAN_P2P || ctx->mode == PD_NET_MODE_UDS) {
        char pkt[64];
        snprintf(pkt, sizeof(pkt), "RESIGN|%d\n", ctx->my_seat);
        if (!send_lockstep_line(ctx, pkt)) {
            return false;
        }
        ctx->game_state.status = PD_STATUS_WIN;
        ctx->game_state.winner = (ctx->my_seat == 0) ? 1 : 0;
        ctx->game_state.piece_in_hand = PD_NONE;
        ctx->draw_offered_by_me = false;
        ctx->draw_offered_by_peer = false;
        ctx->state = PD_NET_STATE_FINISHED;
        ctx->state_updated = true;
        return true;
    }
    return false;
}

bool pd_net_send_draw_offer(pd_net_ctx_t *ctx) {
    if (!ctx) return false;
    if (ctx->mode == PD_NET_MODE_WEB) {
        return pd_net_web_send_draw_offer(ctx);
    }
    if (ctx->mode == PD_NET_MODE_LAN_P2P || ctx->mode == PD_NET_MODE_UDS) {
        ctx->draw_offered_by_me = true;
        char pkt[64];
        snprintf(pkt, sizeof(pkt), "DRAW_OFFER|%d\n", ctx->my_seat);
        return send_lockstep_line(ctx, pkt);
    }
    return false;
}

bool pd_net_send_draw_respond(pd_net_ctx_t *ctx, bool accept) {
    if (!ctx) return false;
    if (ctx->mode == PD_NET_MODE_WEB) {
        return pd_net_web_send_draw_respond(ctx, accept);
    }
    if (ctx->mode == PD_NET_MODE_LAN_P2P || ctx->mode == PD_NET_MODE_UDS) {
        ctx->draw_offered_by_me = false;
        ctx->draw_offered_by_peer = false;
        char pkt[64];
        snprintf(pkt, sizeof(pkt), "DRAW_RESPOND|%d\n", accept ? 1 : 0);
        if (!send_lockstep_line(ctx, pkt)) {
            return false;
        }
        if (accept) {
            ctx->game_state.status = PD_STATUS_DRAW;
            ctx->game_state.winner = PD_NONE;
            ctx->game_state.piece_in_hand = PD_NONE;
            ctx->state = PD_NET_STATE_FINISHED;
            ctx->state_updated = true;
        }
        return true;
    }
    return false;
}

bool pd_net_send_chat(pd_net_ctx_t *ctx, const char *text) {
    if (!ctx || !text || text[0] == '\0') return false;
    if (ctx->mode == PD_NET_MODE_WEB) {
        return pd_net_web_send_chat(ctx, text);
    }
    if (ctx->mode == PD_NET_MODE_LAN_P2P || ctx->mode == PD_NET_MODE_UDS) {
        snprintf(ctx->last_chat_sender, sizeof(ctx->last_chat_sender), "%s", ctx->local_name);
        snprintf(ctx->last_chat, sizeof(ctx->last_chat), "%s", text);
        char pkt[300];
        snprintf(pkt, sizeof(pkt), "CHAT|%s|%s\n", ctx->local_name, text);
        return send_lockstep_line(ctx, pkt);
    }
    return false;
}
