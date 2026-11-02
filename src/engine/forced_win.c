#include "pd_engine.h"
#include <math.h>

static int pick_int(const int *items, int len, pd_rng_t *rng) {
    int idx = (int)floor(pd_rng_next(rng) * (double)len);
    return items[idx];
}

bool pd_has_win_in_one(const pd_game_state_t *state) {
    if (!state || state->status != PD_STATUS_PLAYING || state->piece_in_hand == PD_NONE) {
        return false;
    }
    int piece = state->piece_in_hand;
    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(state->variant, &line_count);
    int empties[PD_BOARD_SIZE];
    int empties_count = pd_empty_cells(state->board, empties);
    for (int i = 0; i < empties_count; i++) {
        if (pd_would_win(state->board, empties[i], piece, lines, line_count)) {
            return true;
        }
    }
    return false;
}

int pd_forced_win_in_two_moves(const pd_game_state_t *state, pd_first_move_t out_moves[256]) {
    if (!state || state->status != PD_STATUS_PLAYING || state->piece_in_hand == PD_NONE) {
        return 0;
    }
    pd_move_t moves[256];
    int moves_count = pd_legal_moves(state, moves);
    int out_count = 0;

    for (int i = 0; i < moves_count; i++) {
        pd_move_t m1 = moves[i];
        if (m1.kind != PD_MOVE_PLACE || m1.give == PD_NONE) {
            continue;
        }
        pd_game_state_t after_first;
        if (!pd_apply_move(state, m1, &after_first) || after_first.status != PD_STATUS_PLAYING) {
            continue;
        }
        pd_move_t replies[256];
        int replies_count = pd_legal_moves(&after_first, replies);
        if (replies_count == 0) {
            continue;
        }
        bool forced = true;
        for (int j = 0; j < replies_count; j++) {
            pd_game_state_t after_reply;
            if (!pd_apply_move(&after_first, replies[j], &after_reply) ||
                after_reply.status != PD_STATUS_PLAYING ||
                !pd_has_win_in_one(&after_reply)) {
                forced = false;
                break;
            }
        }
        if (forced) {
            if (out_moves) {
                out_moves[out_count] = (pd_first_move_t){ .cell = m1.cell, .give = m1.give };
            }
            out_count++;
        }
    }
    return out_count;
}

bool pd_has_forced_win_in_two(const pd_game_state_t *state) {
    return pd_forced_win_in_two_moves(state, NULL) > 0;
}

static bool try_generate_win_in_two(uint32_t seed, pd_variant_t variant, pd_win_in_two_t *out) {
    const int min_pieces = 9;
    const int max_pieces = 13;

    pd_rng_t rng;
    pd_rng_init(&rng, seed);

    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(variant, &line_count);
    pd_game_state_t state;
    pd_create_game(&state, 0, variant);
    int open_give = pick_int(state.available, state.available_count, &rng);
    pd_apply_move(&state, (pd_move_t){ .kind = PD_MOVE_OPEN, .cell = PD_NONE, .give = open_give }, &state);

    for (int guard = 0; guard < 60; guard++) {
        int piece = state.piece_in_hand;
        if (piece == PD_NONE || state.status != PD_STATUS_PLAYING) {
            return false;
        }

        int empties[PD_BOARD_SIZE];
        int empties_count = pd_empty_cells(state.board, empties);
        int placed = PD_BOARD_SIZE - empties_count;
        if (placed >= min_pieces && placed <= max_pieces && !pd_has_win_in_one(&state)) {
            pd_first_move_t first_moves[256];
            int fm_count = pd_forced_win_in_two_moves(&state, first_moves);
            if (fm_count > 0) {
                for (int i = 0; i < PD_BOARD_SIZE; i++) {
                    out->board[i] = state.board[i];
                }
                out->piece_in_hand = piece;
                for (int i = 0; i < state.available_count; i++) {
                    out->available[i] = state.available[i];
                }
                out->available_count = state.available_count;
                for (int i = 0; i < fm_count; i++) {
                    out->first_moves[i] = first_moves[i];
                }
                out->first_moves_count = fm_count;
                out->pieces = placed;
                return true;
            }
        }

        int non_winning[PD_BOARD_SIZE];
        int non_winning_count = 0;
        for (int i = 0; i < empties_count; i++) {
            int c = empties[i];
            if (!pd_would_win(state.board, c, piece, lines, line_count)) {
                non_winning[non_winning_count++] = c;
            }
        }
        if (non_winning_count == 0) {
            return false;
        }
        int cell = pick_int(non_winning, non_winning_count, &rng);

        int after_place[PD_BOARD_SIZE];
        for (int i = 0; i < PD_BOARD_SIZE; i++) {
            after_place[i] = state.board[i];
        }
        after_place[cell] = piece;
        if (pd_empty_cells(after_place, NULL) == 0) {
            return false;
        }

        uint16_t unsafe = pd_winning_pieces_mask(after_place, lines, line_count);
        int safe[PD_PIECE_COUNT];
        int safe_count = 0;
        for (int i = 0; i < state.available_count; i++) {
            int g = state.available[i];
            if ((unsafe & (uint16_t)(1u << g)) == 0) {
                safe[safe_count++] = g;
            }
        }
        int give = (safe_count > 0)
                       ? pick_int(safe, safe_count, &rng)
                       : pick_int(state.available, state.available_count, &rng);
        pd_apply_move(&state, (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = cell, .give = give }, &state);
    }
    return false;
}

void pd_generate_win_in_two(uint32_t seed, pd_variant_t variant, pd_win_in_two_t *out) {
    if (!out) {
        return;
    }
    for (uint32_t attempt = 0; attempt < 256; attempt++) {
        uint32_t s = seed ^ (attempt * 0x9e3779b1u);
        if (try_generate_win_in_two(s, variant, out)) {
            return;
        }
    }
    static const int fallback_board[PD_BOARD_SIZE] = {
        4, PD_NONE, 2, 3,
        PD_NONE, 6, 5, 15,
        7, 8, PD_NONE, 10,
        13, 9, 14, 1
    };
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        out->board[i] = fallback_board[i];
    }
    out->piece_in_hand = 11;
    out->available[0] = 0;
    out->available[1] = 12;
    out->available_count = 2;
    out->first_moves[0] = (pd_first_move_t){ .cell = 4, .give = 12 };
    out->first_moves_count = 1;
    out->pieces = 13;
}
