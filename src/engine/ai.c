#include "pd_engine.h"
#include <math.h>
#include <string.h>

#define PD_WIN_SCORE 1000000
#define PD_INF 1000000000

void pd_rng_init(pd_rng_t *rng, uint32_t seed) {
    if (!rng) {
        return;
    }
    rng->state = seed;
}

double pd_rng_next(pd_rng_t *rng) {
    rng->state += 0x6d2b79f5u;
    uint32_t a = rng->state;
    uint32_t t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (double)((t ^ (t >> 14)) >> 0) / 4294967296.0;
}

static int pick_index(int len, pd_rng_t *rng) {
    return (int)floor(pd_rng_next(rng) * (double)len);
}

int pd_hard_budget_for(pd_variant_t variant) {
    return variant == PD_VARIANT_MISERE ? 100000 : 200000;
}

typedef struct {
    int nodes;
    int budget;
} search_ctx_t;

static int heuristic(const pd_game_state_t *state) {
    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(state->variant, &line_count);
    return -10 * pd_open_threats(state->board, lines, line_count);
}

static int candidate_moves(const pd_game_state_t *state, pd_move_t out_moves[256]) {
    if (state->piece_in_hand == PD_NONE) {
        for (int i = 0; i < state->available_count; i++) {
            out_moves[i] = (pd_move_t){
                .kind = PD_MOVE_OPEN,
                .cell = PD_NONE,
                .give = state->available[i]
            };
        }
        return state->available_count;
    }

    int piece = state->piece_in_hand;
    int board[PD_BOARD_SIZE];
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        board[i] = state->board[i];
    }

    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(state->variant, &line_count);
    int empties[PD_BOARD_SIZE];
    int empties_count = pd_empty_cells(state->board, empties);
    bool single_empty = (empties_count == 1);

    pd_move_t terminal[PD_BOARD_SIZE];
    int terminal_count = 0;
    pd_move_t rest[256];
    int rest_count = 0;

    for (int i = 0; i < empties_count; i++) {
        int cell = empties[i];
        board[cell] = piece;
        if (pd_is_win(board, lines, line_count) || single_empty) {
            board[cell] = PD_NONE;
            terminal[terminal_count++] = (pd_move_t){
                .kind = PD_MOVE_PLACE,
                .cell = cell,
                .give = PD_NONE
            };
            continue;
        }

        if (state->variant == PD_VARIANT_MISERE) {
            board[cell] = PD_NONE;
            for (int j = 0; j < state->available_count; j++) {
                rest[rest_count++] = (pd_move_t){
                    .kind = PD_MOVE_PLACE,
                    .cell = cell,
                    .give = state->available[j]
                };
            }
        } else {
            uint16_t unsafe = pd_winning_pieces_mask(board, lines, line_count);
            board[cell] = PD_NONE;
            int safe[PD_PIECE_COUNT];
            int safe_count = 0;
            for (int j = 0; j < state->available_count; j++) {
                int g = state->available[j];
                if ((unsafe & (uint16_t)(1u << g)) == 0) {
                    safe[safe_count++] = g;
                }
            }
            const int *gives = (safe_count > 0) ? safe : state->available;
            int gives_count = (safe_count > 0) ? safe_count : state->available_count;
            for (int j = 0; j < gives_count; j++) {
                rest[rest_count++] = (pd_move_t){
                    .kind = PD_MOVE_PLACE,
                    .cell = cell,
                    .give = gives[j]
                };
            }
        }
    }

    int total = 0;
    for (int i = 0; i < terminal_count; i++) {
        out_moves[total++] = terminal[i];
    }
    for (int i = 0; i < rest_count; i++) {
        out_moves[total++] = rest[i];
    }
    return total;
}

static int negamax(const pd_game_state_t *state, int depth, int alpha, int beta, search_ctx_t *ctx) {
    ctx->nodes += 1;
    int best = -PD_INF;
    int a = alpha;
    pd_move_t moves[256];
    int move_count = candidate_moves(state, moves);

    for (int i = 0; i < move_count; i++) {
        pd_game_state_t next;
        pd_apply_move(state, moves[i], &next);
        int score;
        if (next.status == PD_STATUS_WIN) {
            score = (state->variant == PD_VARIANT_MISERE)
                        ? -(PD_WIN_SCORE + depth)
                        : (PD_WIN_SCORE + depth);
        } else if (next.status == PD_STATUS_DRAW) {
            score = 0;
        } else if (depth <= 1 || ctx->nodes >= ctx->budget) {
            score = -heuristic(&next);
        } else {
            score = -negamax(&next, depth - 1, -beta, -a, ctx);
        }
        if (score > best) {
            best = score;
        }
        if (best > a) {
            a = best;
        }
        if (a >= beta) {
            break;
        }
    }
    return best;
}

static pd_move_t choose_by_search(const pd_game_state_t *state, int max_depth, int budget, pd_rng_t *rng) {
    search_ctx_t ctx = { .nodes = 0, .budget = budget };
    int best = -PD_INF;
    pd_move_t best_moves[256];
    int best_count = 0;

    pd_move_t moves[256];
    int move_count = candidate_moves(state, moves);

    for (int i = 0; i < move_count; i++) {
        pd_game_state_t next;
        pd_apply_move(state, moves[i], &next);
        int score;
        if (next.status == PD_STATUS_WIN) {
            score = (state->variant == PD_VARIANT_MISERE)
                        ? -(PD_WIN_SCORE + max_depth)
                        : (PD_WIN_SCORE + max_depth);
        } else if (next.status == PD_STATUS_DRAW) {
            score = 0;
        } else if (max_depth <= 1) {
            score = -heuristic(&next);
        } else {
            score = -negamax(&next, max_depth - 1, -PD_INF, PD_INF, &ctx);
        }
        if (score > best) {
            best = score;
            best_moves[0] = moves[i];
            best_count = 1;
        } else if (score == best) {
            best_moves[best_count++] = moves[i];
        }
    }
    return best_moves[pick_index(best_count, rng)];
}

static pd_move_t choose_easy(const pd_game_state_t *state, pd_rng_t *rng) {
    if (state->piece_in_hand == PD_NONE) {
        int give = state->available[pick_index(state->available_count, rng)];
        return (pd_move_t){ .kind = PD_MOVE_OPEN, .cell = PD_NONE, .give = give };
    }

    int piece = state->piece_in_hand;
    int empties[PD_BOARD_SIZE];
    int empties_count = pd_empty_cells(state->board, empties);
    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(state->variant, &line_count);
    bool misere = (state->variant == PD_VARIANT_MISERE);

    if (!misere) {
        for (int i = 0; i < empties_count; i++) {
            if (pd_would_win(state->board, empties[i], piece, lines, line_count)) {
                return (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = empties[i], .give = PD_NONE };
            }
        }
    }

    int cells[PD_BOARD_SIZE];
    int cells_count = 0;
    if (misere) {
        for (int i = 0; i < empties_count; i++) {
            if (!pd_would_win(state->board, empties[i], piece, lines, line_count)) {
                cells[cells_count++] = empties[i];
            }
        }
    } else {
        for (int i = 0; i < empties_count; i++) {
            cells[cells_count++] = empties[i];
        }
    }

    const int *cell_pool = (cells_count > 0) ? cells : empties;
    int cell_pool_count = (cells_count > 0) ? cells_count : empties_count;
    int cell = cell_pool[pick_index(cell_pool_count, rng)];

    int board[PD_BOARD_SIZE];
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        board[i] = state->board[i];
    }
    board[cell] = piece;

    if (pd_empty_cells(board, NULL) == 0) {
        return (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = cell, .give = PD_NONE };
    }

    if (misere) {
        int give = state->available[pick_index(state->available_count, rng)];
        return (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = cell, .give = give };
    }

    uint16_t unsafe = pd_winning_pieces_mask(board, lines, line_count);
    int safe[PD_PIECE_COUNT];
    int safe_count = 0;
    for (int i = 0; i < state->available_count; i++) {
        int g = state->available[i];
        if ((unsafe & (uint16_t)(1u << g)) == 0) {
            safe[safe_count++] = g;
        }
    }
    const int *give_pool = (safe_count > 0) ? safe : state->available;
    int give_pool_count = (safe_count > 0) ? safe_count : state->available_count;
    int give = give_pool[pick_index(give_pool_count, rng)];
    return (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = cell, .give = give };
}

bool pd_choose_move(const pd_game_state_t *state, pd_difficulty_t level, pd_rng_t *rng, pd_move_t *out_move) {
    if (!state || !out_move || state->status != PD_STATUS_PLAYING) {
        return false;
    }

    pd_rng_t fallback_rng;
    if (!rng) {
        pd_rng_init(&fallback_rng, 0x9e3779b9u);
        rng = &fallback_rng;
    }

    if (level == PD_DIFF_EASY) {
        *out_move = choose_easy(state, rng);
        return true;
    }
    if (level == PD_DIFF_MEDIUM) {
        *out_move = choose_by_search(state, 2, 80000, rng);
        return true;
    }
    int empties = pd_empty_cells(state->board, NULL);
    int depth = (empties <= 6) ? empties : 3;
    *out_move = choose_by_search(state, depth, pd_hard_budget_for(state->variant), rng);
    return true;
}

const char *pd_difficulty_to_str(pd_difficulty_t diff) {
    switch (diff) {
        case PD_DIFF_EASY:
            return "easy";
        case PD_DIFF_MEDIUM:
            return "medium";
        case PD_DIFF_HARD:
        default:
            return "hard";
    }
}

pd_difficulty_t pd_difficulty_from_str(const char *str) {
    if (!str) {
        return PD_DIFF_EASY;
    }
    if (strcmp(str, "medium") == 0) {
        return PD_DIFF_MEDIUM;
    }
    if (strcmp(str, "hard") == 0) {
        return PD_DIFF_HARD;
    }
    return PD_DIFF_EASY;
}
