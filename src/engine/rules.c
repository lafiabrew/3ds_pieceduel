#include "pd_engine.h"

static void resolve_lines(const pd_line_t **lines, int *line_count) {
    if (!*lines || *line_count <= 0) {
        *lines = PD_LINES;
        *line_count = PD_STD_LINES_COUNT;
    }
}

bool pd_find_winning_line(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count, pd_line_t *out_line) {
    if (!board) {
        return false;
    }
    resolve_lines(&lines, &line_count);
    for (int i = 0; i < line_count; i++) {
        int a = lines[i].cells[0];
        int b = lines[i].cells[1];
        int c = lines[i].cells[2];
        int d = lines[i].cells[3];
        int pa = board[a];
        int pb = board[b];
        int pc = board[c];
        int pd = board[d];
        if (pa == PD_NONE || pb == PD_NONE || pc == PD_NONE || pd == PD_NONE) {
            continue;
        }
        int share_ones = pa & pb & pc & pd;
        int share_zeros = (~pa) & (~pb) & (~pc) & (~pd) & 0xf;
        if (share_ones != 0 || share_zeros != 0) {
            if (out_line) {
                *out_line = lines[i];
            }
            return true;
        }
    }
    return false;
}

bool pd_is_win(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count) {
    return pd_find_winning_line(board, lines, line_count, NULL);
}

bool pd_would_win(const int board[PD_BOARD_SIZE], int cell, int piece, const pd_line_t *lines, int line_count) {
    if (!board || !pd_is_cell_index(cell) || board[cell] != PD_NONE) {
        return false;
    }
    int next[PD_BOARD_SIZE];
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        next[i] = board[i];
    }
    next[cell] = piece;
    return pd_is_win(next, lines, line_count);
}

static uint16_t complete_mask(int ones, int zeros) {
    uint16_t mask = 0;
    for (int g = 0; g < PD_PIECE_COUNT; g++) {
        if ((g & ones) != 0 || (((~g) & 0xf) & zeros) != 0) {
            mask |= (uint16_t)(1u << g);
        }
    }
    return mask;
}

uint16_t pd_winning_pieces_mask(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count) {
    if (!board) {
        return 0;
    }
    resolve_lines(&lines, &line_count);
    uint16_t mask = 0;
    for (int i = 0; i < line_count; i++) {
        int empty_count = 0;
        int ones = 0xf;
        int zeros = 0xf;
        for (int j = 0; j < 4; j++) {
            int idx = lines[i].cells[j];
            int p = board[idx];
            if (p == PD_NONE) {
                empty_count += 1;
                continue;
            }
            ones &= p;
            zeros &= (~p) & 0xf;
        }
        if (empty_count == 1) {
            mask |= complete_mask(ones, zeros);
        }
    }
    return mask;
}

int pd_winning_pieces(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count, int out_pieces[PD_PIECE_COUNT]) {
    uint16_t mask = pd_winning_pieces_mask(board, lines, line_count);
    int count = 0;
    for (int g = 0; g < PD_PIECE_COUNT; g++) {
        if ((mask & (uint16_t)(1u << g)) != 0) {
            if (out_pieces) {
                out_pieces[count] = g;
            }
            count++;
        }
    }
    return count;
}

int pd_open_threats(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count) {
    if (!board) {
        return 0;
    }
    resolve_lines(&lines, &line_count);
    int count = 0;
    for (int i = 0; i < line_count; i++) {
        int empty_count = 0;
        int ones = 0xf;
        int zeros = 0xf;
        for (int j = 0; j < 4; j++) {
            int idx = lines[i].cells[j];
            int p = board[idx];
            if (p == PD_NONE) {
                empty_count += 1;
                continue;
            }
            ones &= p;
            zeros &= (~p) & 0xf;
        }
        if (empty_count == 1 && (ones != 0 || zeros != 0)) {
            count += 1;
        }
    }
    return count;
}

int pd_threat_cells(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count, int out_cells[PD_BOARD_SIZE]) {
    if (!board) {
        return 0;
    }
    resolve_lines(&lines, &line_count);
    uint16_t seen = 0;
    int count = 0;
    for (int i = 0; i < line_count; i++) {
        int empty = -1;
        int empty_count = 0;
        int ones = 0xf;
        int zeros = 0xf;
        for (int j = 0; j < 4; j++) {
            int idx = lines[i].cells[j];
            int p = board[idx];
            if (p == PD_NONE) {
                empty_count += 1;
                empty = idx;
                continue;
            }
            ones &= p;
            zeros &= (~p) & 0xf;
        }
        if (empty_count == 1 && (ones != 0 || zeros != 0)) {
            uint16_t bit = (uint16_t)(1u << empty);
            if ((seen & bit) == 0) {
                seen |= bit;
                if (out_cells) {
                    out_cells[count] = empty;
                }
                count++;
            }
        }
    }
    return count;
}
