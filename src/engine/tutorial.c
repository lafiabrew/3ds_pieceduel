#include "pd_engine.h"

void pd_get_tutorial_data(pd_tutorial_data_t *out) {
    if (!out) {
        return;
    }

    static const int trait_samples[6] = { 8, 4, 2, 1, 0, 15 };
    static const int solid_row[PD_BOARD_SIZE] = {
        1, 7, 13, PD_NONE,
        PD_NONE, PD_NONE, PD_NONE, PD_NONE,
        PD_NONE, PD_NONE, PD_NONE, PD_NONE,
        PD_NONE, PD_NONE, PD_NONE, PD_NONE
    };
    static const int candidates[4] = { 3, 4, 9, 8 };
    static const int safe[2] = { 4, 8 };
    static const int unsafe[2] = { 3, 9 };

    for (int i = 0; i < 6; i++) {
        out->trait_samples[i] = trait_samples[i];
    }
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        out->find_win_board[i] = solid_row[i];
        out->safe_give_board[i] = solid_row[i];
    }
    out->find_win_piece = 9;
    out->find_win_cell = 3;
    out->safe_give_shared_trait = "solid";
    for (int i = 0; i < 4; i++) {
        out->safe_give_candidates[i] = candidates[i];
    }
    for (int i = 0; i < 2; i++) {
        out->safe_give_safe[i] = safe[i];
        out->safe_give_unsafe[i] = unsafe[i];
    }
}
