#include "pd_engine.h"
#include <math.h>

static int pick_int(const int *items, int len, pd_rng_t *rng) {
    int idx = (int)floor(pd_rng_next(rng) * (double)len);
    return items[idx];
}

static bool array_contains(const int *arr, int len, int val) {
    for (int i = 0; i < len; i++) {
        if (arr[i] == val) {
            return true;
        }
    }
    return false;
}

static int winning_cells(const int board[PD_BOARD_SIZE], int piece, const pd_line_t *lines, int line_count, int out_cells[PD_BOARD_SIZE]) {
    int empties[PD_BOARD_SIZE];
    int empties_count = pd_empty_cells(board, empties);
    int count = 0;
    for (int i = 0; i < empties_count; i++) {
        int cell = empties[i];
        if (pd_would_win(board, cell, piece, lines, line_count)) {
            if (out_cells) {
                out_cells[count] = cell;
            }
            count++;
        }
    }
    return count;
}

uint32_t pd_date_seed(const char *iso_date) {
    uint32_t h = 2166136261u;
    if (!iso_date) {
        return h;
    }
    for (size_t i = 0; iso_date[i] != '\0'; i++) {
        h ^= (uint8_t)iso_date[i];
        h *= 16777619u;
    }
    return h;
}

static bool try_generate_daily(uint32_t seed, pd_variant_t variant, pd_daily_puzzle_t *out) {
    const int min_pieces = 5;
    const int max_pieces = 11;

    pd_rng_t rng;
    pd_rng_init(&rng, seed);

    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(variant, &line_count);
    int target_fill = min_pieces + (int)floor(pd_rng_next(&rng) * (double)(max_pieces - min_pieces + 1));

    pd_game_state_t state;
    pd_create_game(&state, 0, variant);
    int open_give = pick_int(state.available, state.available_count, &rng);
    pd_apply_move(&state, (pd_move_t){ .kind = PD_MOVE_OPEN, .cell = PD_NONE, .give = open_give }, &state);

    for (int guard = 0; guard < 40; guard++) {
        int piece = state.piece_in_hand;
        if (piece == PD_NONE) {
            return false;
        }

        int empties[PD_BOARD_SIZE];
        int empties_count = pd_empty_cells(state.board, empties);
        int placed = PD_BOARD_SIZE - empties_count;

        int wins[PD_BOARD_SIZE];
        int wins_count = winning_cells(state.board, piece, lines, line_count, wins);
        if (wins_count >= 1 && placed >= min_pieces && placed <= max_pieces) {
            for (int i = 0; i < PD_BOARD_SIZE; i++) {
                out->board[i] = state.board[i];
            }
            out->piece_in_hand = piece;
            for (int i = 0; i < wins_count; i++) {
                out->solution[i] = wins[i];
            }
            out->solution_count = wins_count;
            out->pieces = placed;
            return true;
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

        int fill_after = placed + 1;
        int raw_win_pieces[PD_PIECE_COUNT];
        int raw_win_count = pd_winning_pieces(after_place, lines, line_count, raw_win_pieces);
        int win_pieces[PD_PIECE_COUNT];
        int win_pieces_count = 0;
        for (int i = 0; i < raw_win_count; i++) {
            if (array_contains(state.available, state.available_count, raw_win_pieces[i])) {
                win_pieces[win_pieces_count++] = raw_win_pieces[i];
            }
        }

        int give;
        if (fill_after >= target_fill && win_pieces_count > 0) {
            int best = win_pieces[0];
            int best_len = winning_cells(after_place, best, lines, line_count, NULL);
            for (int i = 0; i < win_pieces_count; i++) {
                int g = win_pieces[i];
                int g_len = winning_cells(after_place, g, lines, line_count, NULL);
                if (g_len < best_len) {
                    best = g;
                    best_len = g_len;
                }
            }
            give = best;
        } else {
            int safe[PD_PIECE_COUNT];
            int safe_count = 0;
            for (int i = 0; i < state.available_count; i++) {
                int g = state.available[i];
                if (!array_contains(win_pieces, win_pieces_count, g)) {
                    safe[safe_count++] = g;
                }
            }
            if (safe_count > 0) {
                give = pick_int(safe, safe_count, &rng);
            } else {
                give = pick_int(state.available, state.available_count, &rng);
            }
        }

        pd_apply_move(&state, (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = cell, .give = give }, &state);
    }

    return false;
}

void pd_generate_daily_puzzle(uint32_t seed, pd_variant_t variant, pd_daily_puzzle_t *out) {
    if (!out) {
        return;
    }
    for (uint32_t attempt = 0; attempt < 64; attempt++) {
        uint32_t s = seed ^ (attempt * 0x9e3779b1u);
        if (try_generate_daily(s, variant, out)) {
            return;
        }
    }
    pd_empty_board(out->board);
    out->board[0] = 0;
    out->board[1] = 2;
    out->board[2] = 4;
    out->piece_in_hand = 6;
    out->solution[0] = 3;
    out->solution_count = 1;
    out->pieces = 3;
}

int pd_safe_gives_for(const int board[PD_BOARD_SIZE], const int *candidates, int candidates_count,
                      const pd_line_t *lines, int line_count, int out_safe[PD_PIECE_COUNT]) {
    if (!board || !candidates || candidates_count <= 0) {
        return 0;
    }
    uint16_t unsafe = pd_winning_pieces_mask(board, lines, line_count);
    int count = 0;
    for (int i = 0; i < candidates_count; i++) {
        int piece = candidates[i];
        if ((unsafe & (uint16_t)(1u << piece)) == 0) {
            if (out_safe) {
                out_safe[count] = piece;
            }
            count++;
        }
    }
    return count;
}

static bool try_generate_safe_give(uint32_t seed, pd_variant_t variant, pd_safe_give_t *out) {
    const int min_pieces = 9;
    const int max_pieces = 12;

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
        int after_empties = pd_empty_cells(after_place, NULL);
        if (after_empties == 0) {
            return false;
        }

        int candidates[PD_PIECE_COUNT];
        int candidates_count = state.available_count;
        for (int i = 0; i < candidates_count; i++) {
            candidates[i] = state.available[i];
        }

        int placed = PD_BOARD_SIZE - after_empties;
        if (placed >= min_pieces && placed <= max_pieces) {
            int safe[PD_PIECE_COUNT];
            int safe_count = pd_safe_gives_for(after_place, candidates, candidates_count, lines, line_count, safe);
            if (safe_count >= 1 && safe_count < candidates_count) {
                for (int i = 0; i < PD_BOARD_SIZE; i++) {
                    out->board[i] = after_place[i];
                }
                for (int i = 0; i < candidates_count; i++) {
                    out->candidates[i] = candidates[i];
                }
                out->candidates_count = candidates_count;
                for (int i = 0; i < safe_count; i++) {
                    out->safe[i] = safe[i];
                }
                out->safe_count = safe_count;
                out->pieces = placed;
                out->threats = pd_open_threats(after_place, lines, line_count);
                return true;
            }
        }

        int safe_now[PD_PIECE_COUNT];
        int safe_now_count = pd_safe_gives_for(after_place, candidates, candidates_count, lines, line_count, safe_now);
        int give = (safe_now_count > 0)
                       ? pick_int(safe_now, safe_now_count, &rng)
                       : pick_int(candidates, candidates_count, &rng);
        pd_apply_move(&state, (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = cell, .give = give }, &state);
    }
    return false;
}

void pd_generate_safe_give(uint32_t seed, pd_variant_t variant, pd_safe_give_t *out) {
    if (!out) {
        return;
    }
    for (uint32_t attempt = 0; attempt < 256; attempt++) {
        uint32_t s = seed ^ (attempt * 0x9e3779b1u);
        if (try_generate_safe_give(s, variant, out)) {
            return;
        }
    }
    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(variant, &line_count);
    pd_empty_board(out->board);
    out->board[0] = 0;
    out->board[1] = 2;
    out->board[2] = 4;
    out->candidates[0] = 6;
    out->candidates[1] = 7;
    out->candidates[2] = 8;
    out->candidates[3] = 9;
    out->candidates_count = 4;
    out->safe_count = pd_safe_gives_for(out->board, out->candidates, out->candidates_count, lines, line_count, out->safe);
    out->pieces = 3;
    out->threats = pd_open_threats(out->board, lines, line_count);
}

static bool try_generate_threat_drill(uint32_t seed, pd_variant_t variant, pd_threat_drill_t *out) {
    const int min_pieces = 6;
    const int max_pieces = 12;

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
        if (placed >= min_pieces && placed <= max_pieces) {
            int hot[PD_BOARD_SIZE];
            int hot_count = pd_threat_cells(state.board, lines, line_count, hot);
            if (hot_count >= 1 && hot_count < empties_count) {
                for (int i = 0; i < PD_BOARD_SIZE; i++) {
                    out->board[i] = state.board[i];
                }
                for (int i = 0; i < hot_count; i++) {
                    out->hot[i] = hot[i];
                }
                out->hot_count = hot_count;
                out->threats = hot_count;
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

        int give = pick_int(state.available, state.available_count, &rng);
        pd_apply_move(&state, (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = cell, .give = give }, &state);
    }
    return false;
}

void pd_generate_threat_drill(uint32_t seed, pd_variant_t variant, pd_threat_drill_t *out) {
    if (!out) {
        return;
    }
    for (uint32_t attempt = 0; attempt < 256; attempt++) {
        uint32_t s = seed ^ (attempt * 0x9e3779b1u);
        if (try_generate_threat_drill(s, variant, out)) {
            return;
        }
    }
    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(variant, &line_count);
    pd_empty_board(out->board);
    out->board[0] = 0;
    out->board[1] = 2;
    out->board[2] = 4;
    out->hot_count = pd_threat_cells(out->board, lines, line_count, out->hot);
    out->threats = out->hot_count;
    out->pieces = 3;
}
