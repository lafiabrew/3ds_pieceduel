#include "pd_engine.h"

static int other_player(int player) {
    return player == 0 ? 1 : 0;
}

static bool is_piece_available(const pd_game_state_t *state, int piece) {
    if (!pd_is_piece_id(piece)) {
        return false;
    }
    for (int i = 0; i < state->available_count; i++) {
        if (state->available[i] == piece) {
            return true;
        }
    }
    return false;
}

static void remove_available_piece(pd_game_state_t *state, int piece) {
    int count = 0;
    for (int i = 0; i < state->available_count; i++) {
        if (state->available[i] != piece) {
            state->available[count++] = state->available[i];
        }
    }
    state->available_count = count;
}

void pd_create_game(pd_game_state_t *out, int starting_player, pd_variant_t variant) {
    if (!out) {
        return;
    }
    pd_empty_board(out->board);
    out->current = (starting_player == 1) ? 1 : 0;
    out->piece_in_hand = PD_NONE;
    for (int p = 0; p < PD_PIECE_COUNT; p++) {
        out->available[p] = p;
    }
    out->available_count = PD_PIECE_COUNT;
    out->status = PD_STATUS_PLAYING;
    out->winner = PD_NONE;
    out->has_winning_line = false;
    for (int i = 0; i < 4; i++) {
        out->winning_line.cells[i] = PD_NONE;
    }
    out->move_count = 0;
    out->variant = variant;
}

bool pd_apply_move(const pd_game_state_t *state, pd_move_t move, pd_game_state_t *out) {
    if (!state || !out || state->status != PD_STATUS_PLAYING) {
        return false;
    }

    pd_game_state_t next = *state;

    if (state->piece_in_hand == PD_NONE) {
        if (move.kind != PD_MOVE_OPEN) {
            return false;
        }
        if (!is_piece_available(state, move.give)) {
            return false;
        }
        next.piece_in_hand = move.give;
        remove_available_piece(&next, move.give);
        next.current = other_player(state->current);
        next.move_count = state->move_count + 1;
        *out = next;
        return true;
    }

    if (move.kind != PD_MOVE_PLACE) {
        return false;
    }
    if (!pd_is_cell_index(move.cell) || state->board[move.cell] != PD_NONE) {
        return false;
    }

    next.board[move.cell] = state->piece_in_hand;

    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(state->variant, &line_count);
    pd_line_t winning_line;
    if (pd_find_winning_line(next.board, lines, line_count, &winning_line)) {
        next.piece_in_hand = PD_NONE;
        next.status = PD_STATUS_WIN;
        next.winner = (state->variant == PD_VARIANT_MISERE)
                          ? other_player(state->current)
                          : state->current;
        next.has_winning_line = true;
        next.winning_line = winning_line;
        next.move_count = state->move_count + 1;
        *out = next;
        return true;
    }

    if (pd_is_board_full(next.board)) {
        next.piece_in_hand = PD_NONE;
        next.status = PD_STATUS_DRAW;
        next.winner = PD_NONE;
        next.has_winning_line = false;
        next.move_count = state->move_count + 1;
        *out = next;
        return true;
    }

    if (move.give == PD_NONE || !is_piece_available(state, move.give)) {
        return false;
    }

    next.piece_in_hand = move.give;
    remove_available_piece(&next, move.give);
    next.current = other_player(state->current);
    next.move_count = state->move_count + 1;
    *out = next;
    return true;
}

int pd_legal_cells(const pd_game_state_t *state, int out_cells[PD_BOARD_SIZE]) {
    if (!state || state->status != PD_STATUS_PLAYING || state->piece_in_hand == PD_NONE) {
        return 0;
    }
    return pd_empty_cells(state->board, out_cells);
}

int pd_legal_moves(const pd_game_state_t *state, pd_move_t out_moves[256]) {
    if (!state || state->status != PD_STATUS_PLAYING) {
        return 0;
    }
    int count = 0;
    if (state->piece_in_hand == PD_NONE) {
        for (int i = 0; i < state->available_count; i++) {
            if (out_moves) {
                out_moves[count] = (pd_move_t){
                    .kind = PD_MOVE_OPEN,
                    .cell = PD_NONE,
                    .give = state->available[i]
                };
            }
            count++;
        }
        return count;
    }

    int piece = state->piece_in_hand;
    int empties[PD_BOARD_SIZE];
    int empties_count = pd_empty_cells(state->board, empties);
    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(state->variant, &line_count);

    for (int i = 0; i < empties_count; i++) {
        int cell = empties[i];
        if (pd_would_win(state->board, cell, piece, lines, line_count) || empties_count == 1) {
            if (out_moves) {
                out_moves[count] = (pd_move_t){
                    .kind = PD_MOVE_PLACE,
                    .cell = cell,
                    .give = PD_NONE
                };
            }
            count++;
            continue;
        }
        for (int j = 0; j < state->available_count; j++) {
            if (out_moves) {
                out_moves[count] = (pd_move_t){
                    .kind = PD_MOVE_PLACE,
                    .cell = cell,
                    .give = state->available[j]
                };
            }
            count++;
        }
    }
    return count;
}
