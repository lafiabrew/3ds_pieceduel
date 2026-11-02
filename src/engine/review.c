#include "pd_engine.h"

#define FORCED_LOSS_EMPTIES_GATE 9

static bool handed_forced_loss(const pd_game_state_t *before, int played_cell, int played_give) {
    if (before->piece_in_hand == PD_NONE || played_give == PD_NONE) {
        return false;
    }
    if (pd_empty_cells(before->board, NULL) > FORCED_LOSS_EMPTIES_GATE) {
        return false;
    }

    pd_move_t move;
    if (played_cell == PD_NONE) {
        move = (pd_move_t){ .kind = PD_MOVE_OPEN, .cell = PD_NONE, .give = played_give };
    } else {
        move = (pd_move_t){ .kind = PD_MOVE_PLACE, .cell = played_cell, .give = played_give };
    }

    pd_game_state_t after;
    if (!pd_apply_move(before, move, &after)) {
        return false;
    }
    if (after.status != PD_STATUS_PLAYING || !pd_has_forced_win_in_two(&after)) {
        return false;
    }

    pd_move_t alts[256];
    int alts_count = pd_legal_moves(before, alts);
    for (int i = 0; i < alts_count; i++) {
        pd_game_state_t next;
        if (pd_apply_move(before, alts[i], &next)) {
            if (next.status != PD_STATUS_PLAYING || !pd_has_forced_win_in_two(&next)) {
                return true;
            }
        }
    }
    return false;
}

static void with_piece(const int board[PD_BOARD_SIZE], int cell, int piece, int out_board[PD_BOARD_SIZE]) {
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        out_board[i] = (i == cell) ? piece : board[i];
    }
}

static bool has_non_losing_move(const pd_game_state_t *before, int held) {
    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(before->variant, &line_count);
    int empties[PD_BOARD_SIZE];
    int empties_count = pd_empty_cells(before->board, empties);
    for (int i = 0; i < empties_count; i++) {
        int cell = empties[i];
        int board[PD_BOARD_SIZE];
        with_piece(before->board, cell, held, board);
        if (before->variant == PD_VARIANT_MISERE) {
            if (!pd_is_win(board, lines, line_count)) {
                return true;
            }
            continue;
        }
        if (pd_is_win(board, lines, line_count)) {
            return true;
        }
        if (pd_empty_cells(board, NULL) == 0) {
            return true;
        }
        if (pd_safe_gives_for(board, before->available, before->available_count, lines, line_count, NULL) > 0) {
            return true;
        }
    }
    return false;
}

void pd_review_move(const pd_game_state_t *before, int played_cell, int played_give, pd_move_review_t *out) {
    if (!before || !out) {
        return;
    }
    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(before->variant, &line_count);
    int held = before->piece_in_hand;

    int missed_win_cell = PD_NONE;
    if (before->variant != PD_VARIANT_MISERE &&
        held != PD_NONE && played_cell != PD_NONE &&
        !pd_would_win(before->board, played_cell, held, lines, line_count)) {
        int empties[PD_BOARD_SIZE];
        int empties_count = pd_empty_cells(before->board, empties);
        for (int i = 0; i < empties_count; i++) {
            int c = empties[i];
            if (pd_would_win(before->board, c, held, lines, line_count)) {
                missed_win_cell = c;
                break;
            }
        }
    }

    bool unsafe_give = false;
    int safer_give = PD_NONE;
    if (before->variant != PD_VARIANT_MISERE &&
        played_give != PD_NONE && held != PD_NONE && played_cell != PD_NONE) {
        int after_board[PD_BOARD_SIZE];
        with_piece(before->board, played_cell, held, after_board);
        uint16_t mask = pd_winning_pieces_mask(after_board, lines, line_count);
        bool opponent_wins_with_gift = (mask & (uint16_t)(1u << played_give)) != 0;
        if (opponent_wins_with_gift) {
            int safe_same_cell[PD_PIECE_COUNT];
            int safe_count = pd_safe_gives_for(after_board, before->available, before->available_count,
                                               lines, line_count, safe_same_cell);
            if (safe_count > 0) {
                unsafe_give = true;
                safer_give = safe_same_cell[0];
            } else if (has_non_losing_move(before, held)) {
                unsafe_give = true;
                safer_give = PD_NONE;
            }
        }
    }

    bool forced_loss = (before->variant != PD_VARIANT_MISERE) &&
                       !unsafe_give && handed_forced_loss(before, played_cell, played_give);

    out->missed_win_cell = missed_win_cell;
    out->unsafe_give = unsafe_give;
    out->safer_give = safer_give;
    out->forced_loss = forced_loss;
}
