#include "pd_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_attributes(void) {
    assert(PD_ATTRIBUTE_COUNT == 4);
    assert(PD_PIECE_COUNT == 16);

    assert(pd_is_piece_id(0));
    assert(pd_is_piece_id(15));
    assert(!pd_is_piece_id(-1));
    assert(!pd_is_piece_id(16));

    pd_attributes_t a;
    assert(!pd_decode_piece(-1, &a));
    assert(!pd_decode_piece(16, &a));

    assert(pd_decode_piece(0, &a));
    assert(!a.tall && !a.light && !a.square && !a.solid);

    assert(pd_decode_piece(15, &a));
    assert(a.tall && a.light && a.square && a.solid);

    assert(pd_decode_piece(0xa, &a));
    assert(a.tall && !a.light && a.square && !a.solid);

    for (int p = 0; p < PD_PIECE_COUNT; p++) {
        assert(pd_decode_piece(p, &a));
        assert(pd_encode_piece(a) == p);
    }

    char buf[8];
    assert(strcmp(pd_piece_short_label(0, buf), "SDRH") == 0);
    assert(strcmp(pd_piece_short_label(15, buf), "TLQS") == 0);
    assert(strcmp(pd_piece_short_label(-1, buf), "----") == 0);
}

static void test_board_and_rules(void) {
    int count = 0;
    assert(pd_lines_for_variant(PD_VARIANT_STANDARD, &count) == PD_LINES && count == 10);
    assert(pd_lines_for_variant(PD_VARIANT_MISERE, &count) == PD_LINES && count == 10);
    assert(pd_lines_for_variant(PD_VARIANT_ADVANCED, &count) == PD_ADVANCED_LINES && count == 19);

    assert(strcmp(pd_variant_to_str(PD_VARIANT_STANDARD), "standard") == 0);
    assert(strcmp(pd_variant_to_str(PD_VARIANT_MISERE), "misere") == 0);
    assert(strcmp(pd_variant_to_str(PD_VARIANT_ADVANCED), "advanced") == 0);
    assert(pd_variant_from_str("standard") == PD_VARIANT_STANDARD);
    assert(pd_variant_from_str("misere") == PD_VARIANT_MISERE);
    assert(pd_variant_from_str("advanced") == PD_VARIANT_ADVANCED);

    int b[PD_BOARD_SIZE];
    pd_empty_board(b);
    int empties[PD_BOARD_SIZE];
    assert(pd_empty_cells(b, empties) == 16);
    assert(!pd_is_board_full(b));

    b[0] = 8;
    b[1] = 9;
    b[2] = 10;
    assert(!pd_is_win(b, PD_LINES, PD_STD_LINES_COUNT));
    assert(pd_would_win(b, 3, 11, PD_LINES, PD_STD_LINES_COUNT));
    assert(!pd_would_win(b, 3, 7, PD_LINES, PD_STD_LINES_COUNT));
    assert(!pd_would_win(b, 0, 7, PD_LINES, PD_STD_LINES_COUNT));

    int hot[PD_BOARD_SIZE];
    assert(pd_threat_cells(b, PD_LINES, PD_STD_LINES_COUNT, hot) == 1);
    assert(hot[0] == 3);
    assert(pd_open_threats(b, PD_LINES, PD_STD_LINES_COUNT) == 1);

    b[3] = 11;
    pd_line_t line;
    assert(pd_find_winning_line(b, PD_LINES, PD_STD_LINES_COUNT, &line));
    assert(line.cells[0] == 0 && line.cells[1] == 1 && line.cells[2] == 2 && line.cells[3] == 3);

    /* Advanced 2x2 square threat & win */
    pd_empty_board(b);
    b[0] = 8;
    b[1] = 9;
    b[4] = 10;
    assert(pd_threat_cells(b, PD_LINES, PD_STD_LINES_COUNT, hot) == 0);
    assert(pd_threat_cells(b, PD_ADVANCED_LINES, PD_ADV_LINES_COUNT, hot) == 1);
    assert(hot[0] == 5);
    assert(pd_would_win(b, 5, 11, PD_ADVANCED_LINES, PD_ADV_LINES_COUNT));
}

static void test_state_and_variants(void) {
    pd_game_state_t g;
    pd_create_game(&g, 0, PD_VARIANT_STANDARD);
    assert(g.current == 0 && g.piece_in_hand == PD_NONE && g.available_count == 16);

    int cells[PD_BOARD_SIZE];
    assert(pd_legal_cells(&g, cells) == 0);
    pd_move_t moves[256];
    assert(pd_legal_moves(&g, moves) == 16);

    /* Reject place at open */
    assert(!pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 0, 1 }, &g));

    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_OPEN, PD_NONE, 0 }, &g));
    assert(g.piece_in_hand == 0 && g.current == 1 && g.available_count == 15);
    assert(pd_legal_cells(&g, cells) == 16);

    /* Reject giving already-given piece 0 */
    assert(!pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 0, 0 }, &g));

    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 0, 8 }, &g));
    /* Reject occupied or out-of-range cell */
    assert(!pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 0, 4 }, &g));
    assert(!pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 99, 4 }, &g));

    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 1, 4 }, &g));
    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 2, 12 }, &g));
    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 3, PD_NONE }, &g));
    assert(g.status == PD_STATUS_WIN && g.winner == 0 && g.has_winning_line);
    assert(pd_legal_cells(&g, cells) == 0);
    assert(pd_legal_moves(&g, moves) == 0);

    /* Misere variant credits opponent on line completion */
    pd_create_game(&g, 0, PD_VARIANT_MISERE);
    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_OPEN, PD_NONE, 0 }, &g));
    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 0, 8 }, &g));
    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 1, 4 }, &g));
    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 2, 12 }, &g));
    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 3, PD_NONE }, &g));
    assert(g.status == PD_STATUS_WIN && g.winner == 1);

    /* Full board draw detection */
    static const int draw_board[PD_BOARD_SIZE] = {
        0, 5, 10, 15, 11, 14, 1, 4, 13, 8, 7, 2, 6, 3, 12, PD_NONE
    };
    pd_create_game(&g, 1, PD_VARIANT_STANDARD);
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        g.board[i] = draw_board[i];
    }
    g.piece_in_hand = 9;
    g.available_count = 0;
    g.move_count = 31;
    assert(pd_apply_move(&g, (pd_move_t){ PD_MOVE_PLACE, 15, PD_NONE }, &g));
    assert(g.status == PD_STATUS_DRAW && g.winner == PD_NONE);
}

static void test_ai(void) {
    assert(pd_hard_budget_for(PD_VARIANT_MISERE) == 100000);
    assert(pd_hard_budget_for(PD_VARIANT_STANDARD) == 200000);
    assert(pd_hard_budget_for(PD_VARIANT_ADVANCED) == 200000);

    for (int seed = 1; seed <= 4; seed++) {
        pd_rng_t rng;
        pd_rng_init(&rng, (uint32_t)seed);
        pd_game_state_t st;
        pd_create_game(&st, 0, PD_VARIANT_STANDARD);
        while (st.status == PD_STATUS_PLAYING) {
            pd_difficulty_t diff = (st.current == 0) ? PD_DIFF_HARD : PD_DIFF_EASY;
            pd_move_t mv;
            assert(pd_choose_move(&st, diff, &rng, &mv));
            assert(pd_apply_move(&st, mv, &st));
        }
        assert(st.winner != 1);
    }
}

static void test_puzzles_and_forced_win(void) {
    assert(pd_date_seed("2026-06-10") == pd_date_seed("2026-06-10"));
    assert(pd_date_seed("2026-06-10") != pd_date_seed("2026-06-11"));

    for (uint32_t seed = 0; seed < 20; seed++) {
        pd_daily_puzzle_t dp;
        pd_generate_daily_puzzle(seed, PD_VARIANT_STANDARD, &dp);
        assert(dp.solution_count >= 1);
        assert(dp.pieces >= 5 && dp.pieces <= 11);
        for (int i = 0; i < dp.solution_count; i++) {
            assert(pd_would_win(dp.board, dp.solution[i], dp.piece_in_hand, PD_LINES, PD_STD_LINES_COUNT));
        }

        pd_safe_give_t sg;
        pd_generate_safe_give(seed, PD_VARIANT_STANDARD, &sg);
        assert(sg.safe_count >= 1 && sg.safe_count < sg.candidates_count);

        pd_threat_drill_t td;
        pd_generate_threat_drill(seed, PD_VARIANT_STANDARD, &td);
        assert(td.hot_count >= 1 && td.threats == td.hot_count);
    }

    for (uint32_t seed = 0; seed < 10; seed++) {
        pd_win_in_two_t w2;
        pd_generate_win_in_two(seed, PD_VARIANT_STANDARD, &w2);
        assert(w2.first_moves_count > 0);
    }
}

static void test_review_and_tutorial(void) {
    pd_game_state_t before;
    pd_create_game(&before, 0, PD_VARIANT_STANDARD);
    before.board[0] = 0;
    before.board[1] = 6;
    before.board[2] = 10;
    before.piece_in_hand = 8;
    before.available[0] = 1;
    before.available[1] = 3;
    before.available[2] = 5;
    before.available_count = 3;

    pd_move_review_t rev;
    pd_review_move(&before, 5, 1, &rev);
    assert(rev.missed_win_cell == 3);

    /* Advanced 2x2 square missed win */
    pd_game_state_t adv_before;
    pd_create_game(&adv_before, 0, PD_VARIANT_ADVANCED);
    adv_before.board[0] = 8;
    adv_before.board[1] = 9;
    adv_before.board[4] = 10;
    adv_before.piece_in_hand = 11;
    adv_before.available[0] = 0;
    adv_before.available[1] = 1;
    adv_before.available_count = 2;

    pd_move_review_t adv_rev;
    pd_review_move(&adv_before, 15, 0, &adv_rev);
    assert(adv_rev.missed_win_cell == 5);

    /* Misere variant does not flag line completion as a missed win */
    pd_game_state_t mis_before = before;
    mis_before.variant = PD_VARIANT_MISERE;
    pd_move_review_t mis_rev;
    pd_review_move(&mis_before, 5, 1, &mis_rev);
    assert(mis_rev.missed_win_cell == PD_NONE);

    pd_tutorial_data_t tut;
    pd_get_tutorial_data(&tut);
    assert(tut.find_win_piece == 9 && tut.find_win_cell == 3);
    assert(strcmp(tut.safe_give_shared_trait, "solid") == 0);
}

int main(void) {
    test_attributes();
    test_board_and_rules();
    test_state_and_variants();
    test_ai();
    test_puzzles_and_forced_win();
    test_review_and_tutorial();
    printf("PASS: test_engine all checks succeeded.\n");
    return 0;
}
