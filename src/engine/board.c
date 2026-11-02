#include "pd_engine.h"
#include <string.h>

const pd_line_t PD_LINES[PD_STD_LINES_COUNT] = {
    {{0, 1, 2, 3}},
    {{4, 5, 6, 7}},
    {{8, 9, 10, 11}},
    {{12, 13, 14, 15}},
    {{0, 4, 8, 12}},
    {{1, 5, 9, 13}},
    {{2, 6, 10, 14}},
    {{3, 7, 11, 15}},
    {{0, 5, 10, 15}},
    {{3, 6, 9, 12}}
};

const pd_line_t PD_SQUARES[PD_SQUARES_COUNT] = {
    {{0, 1, 4, 5}},
    {{1, 2, 5, 6}},
    {{2, 3, 6, 7}},
    {{4, 5, 8, 9}},
    {{5, 6, 9, 10}},
    {{6, 7, 10, 11}},
    {{8, 9, 12, 13}},
    {{9, 10, 13, 14}},
    {{10, 11, 14, 15}}
};

const pd_line_t PD_ADVANCED_LINES[PD_ADV_LINES_COUNT] = {
    {{0, 1, 2, 3}},
    {{4, 5, 6, 7}},
    {{8, 9, 10, 11}},
    {{12, 13, 14, 15}},
    {{0, 4, 8, 12}},
    {{1, 5, 9, 13}},
    {{2, 6, 10, 14}},
    {{3, 7, 11, 15}},
    {{0, 5, 10, 15}},
    {{3, 6, 9, 12}},
    {{0, 1, 4, 5}},
    {{1, 2, 5, 6}},
    {{2, 3, 6, 7}},
    {{4, 5, 8, 9}},
    {{5, 6, 9, 10}},
    {{6, 7, 10, 11}},
    {{8, 9, 12, 13}},
    {{9, 10, 13, 14}},
    {{10, 11, 14, 15}}
};

const pd_line_t *pd_lines_for_variant(pd_variant_t variant, int *out_count) {
    if (variant == PD_VARIANT_ADVANCED) {
        if (out_count) {
            *out_count = PD_ADV_LINES_COUNT;
        }
        return PD_ADVANCED_LINES;
    }
    if (out_count) {
        *out_count = PD_STD_LINES_COUNT;
    }
    return PD_LINES;
}

void pd_empty_board(int board[PD_BOARD_SIZE]) {
    if (!board) {
        return;
    }
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        board[i] = PD_NONE;
    }
}

bool pd_is_cell_index(int value) {
    return value >= 0 && value < PD_BOARD_SIZE;
}

int pd_empty_cells(const int board[PD_BOARD_SIZE], int out_cells[PD_BOARD_SIZE]) {
    if (!board) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        if (board[i] == PD_NONE) {
            if (out_cells) {
                out_cells[count] = i;
            }
            count++;
        }
    }
    return count;
}

bool pd_is_board_full(const int board[PD_BOARD_SIZE]) {
    if (!board) {
        return false;
    }
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        if (board[i] == PD_NONE) {
            return false;
        }
    }
    return true;
}

const char *pd_variant_to_str(pd_variant_t variant) {
    switch (variant) {
        case PD_VARIANT_MISERE:
            return "misere";
        case PD_VARIANT_ADVANCED:
            return "advanced";
        case PD_VARIANT_STANDARD:
        default:
            return "standard";
    }
}

pd_variant_t pd_variant_from_str(const char *str) {
    if (!str) {
        return PD_VARIANT_STANDARD;
    }
    if (strcmp(str, "misere") == 0) {
        return PD_VARIANT_MISERE;
    }
    if (strcmp(str, "advanced") == 0) {
        return PD_VARIANT_ADVANCED;
    }
    return PD_VARIANT_STANDARD;
}
