#include "pd_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_board_json(const int board[PD_BOARD_SIZE]) {
    printf("[");
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        if (i > 0) {
            printf(",");
        }
        if (board[i] == PD_NONE) {
            printf("null");
        } else {
            printf("%d", board[i]);
        }
    }
    printf("]");
}

static void print_int_array_json(const int *arr, int count) {
    printf("[");
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            printf(",");
        }
        printf("%d", arr[i]);
    }
    printf("]");
}

static const char *status_to_str(pd_status_t status) {
    switch (status) {
        case PD_STATUS_WIN:
            return "win";
        case PD_STATUS_DRAW:
            return "draw";
        case PD_STATUS_PLAYING:
        default:
            return "playing";
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: pd_cli <subcommand> [args...]\n");
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "rng") == 0 && argc >= 4) {
        uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 0);
        int count = atoi(argv[3]);
        pd_rng_t rng;
        pd_rng_init(&rng, seed);
        printf("[");
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                printf(",");
            }
            printf("%.17g", pd_rng_next(&rng));
        }
        printf("]\n");
        return 0;
    }

    if (strcmp(cmd, "dateseed") == 0 && argc >= 3) {
        uint32_t seed = pd_date_seed(argv[2]);
        printf("{\"seed\":%u}\n", seed);
        return 0;
    }

    if (strcmp(cmd, "play") == 0 && argc >= 6) {
        uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 0);
        pd_variant_t variant = pd_variant_from_str(argv[3]);
        pd_difficulty_t diff0 = pd_difficulty_from_str(argv[4]);
        pd_difficulty_t diff1 = pd_difficulty_from_str(argv[5]);

        pd_rng_t rng;
        pd_rng_init(&rng, seed);
        pd_game_state_t st;
        pd_create_game(&st, 0, variant);

        pd_move_t moves[64];
        int moves_count = 0;
        while (st.status == PD_STATUS_PLAYING && moves_count < 64) {
            pd_difficulty_t diff = (st.current == 0) ? diff0 : diff1;
            pd_move_t mv;
            if (!pd_choose_move(&st, diff, &rng, &mv)) {
                break;
            }
            moves[moves_count++] = mv;
            if (!pd_apply_move(&st, mv, &st)) {
                break;
            }
        }

        printf("{\"status\":\"%s\",\"winner\":", status_to_str(st.status));
        if (st.winner == PD_NONE) {
            printf("null");
        } else {
            printf("%d", st.winner);
        }
        printf(",\"moveCount\":%d,\"board\":", st.move_count);
        print_board_json(st.board);
        printf(",\"moves\":[");
        for (int i = 0; i < moves_count; i++) {
            if (i > 0) {
                printf(",");
            }
            const char *kind_str = (moves[i].kind == PD_MOVE_OPEN) ? "open" : "place";
            printf("{\"kind\":\"%s\",\"cell\":", kind_str);
            if (moves[i].kind == PD_MOVE_PLACE && moves[i].cell != PD_NONE) {
                printf("%d", moves[i].cell);
            } else {
                printf("null");
            }
            printf(",\"give\":");
            if (moves[i].give == PD_NONE) {
                printf("null");
            } else {
                printf("%d", moves[i].give);
            }
            printf("}");
        }
        printf("]}\n");
        return 0;
    }

    if (strcmp(cmd, "daily") == 0 && argc >= 4) {
        uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 0);
        pd_variant_t variant = pd_variant_from_str(argv[3]);
        pd_daily_puzzle_t p;
        pd_generate_daily_puzzle(seed, variant, &p);
        printf("{\"board\":");
        print_board_json(p.board);
        printf(",\"pieceInHand\":%d,\"solution\":", p.piece_in_hand);
        print_int_array_json(p.solution, p.solution_count);
        printf(",\"pieces\":%d}\n", p.pieces);
        return 0;
    }

    if (strcmp(cmd, "win2") == 0 && argc >= 4) {
        uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 0);
        pd_variant_t variant = pd_variant_from_str(argv[3]);
        pd_win_in_two_t p;
        pd_generate_win_in_two(seed, variant, &p);
        printf("{\"board\":");
        print_board_json(p.board);
        printf(",\"pieceInHand\":%d,\"available\":", p.piece_in_hand);
        print_int_array_json(p.available, p.available_count);
        printf(",\"firstMoves\":[");
        for (int i = 0; i < p.first_moves_count; i++) {
            if (i > 0) {
                printf(",");
            }
            printf("{\"cell\":%d,\"give\":%d}", p.first_moves[i].cell, p.first_moves[i].give);
        }
        printf("],\"pieces\":%d}\n", p.pieces);
        return 0;
    }

    if (strcmp(cmd, "safegive") == 0 && argc >= 4) {
        uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 0);
        pd_variant_t variant = pd_variant_from_str(argv[3]);
        pd_safe_give_t p;
        pd_generate_safe_give(seed, variant, &p);
        printf("{\"board\":");
        print_board_json(p.board);
        printf(",\"candidates\":");
        print_int_array_json(p.candidates, p.candidates_count);
        printf(",\"safe\":");
        print_int_array_json(p.safe, p.safe_count);
        printf(",\"pieces\":%d,\"threats\":%d}\n", p.pieces, p.threats);
        return 0;
    }

    if (strcmp(cmd, "threatdrill") == 0 && argc >= 4) {
        uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 0);
        pd_variant_t variant = pd_variant_from_str(argv[3]);
        pd_threat_drill_t p;
        pd_generate_threat_drill(seed, variant, &p);
        printf("{\"board\":");
        print_board_json(p.board);
        printf(",\"hot\":");
        print_int_array_json(p.hot, p.hot_count);
        printf(",\"threats\":%d,\"pieces\":%d}\n", p.threats, p.pieces);
        return 0;
    }

    if (strcmp(cmd, "review") == 0 && argc >= 4) {
        uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 0);
        pd_variant_t variant = pd_variant_from_str(argv[3]);
        pd_rng_t rng;
        pd_rng_init(&rng, seed);
        pd_game_state_t st;
        pd_create_game(&st, 0, variant);
        int ply = 0;
        printf("[");
        while (st.status == PD_STATUS_PLAYING && ply < 64) {
            pd_move_t mv;
            if (!pd_choose_move(&st, PD_DIFF_EASY, &rng, &mv)) {
                break;
            }
            int played_cell = (mv.kind == PD_MOVE_PLACE) ? mv.cell : PD_NONE;
            int played_give = mv.give;
            pd_move_review_t rev;
            pd_review_move(&st, played_cell, played_give, &rev);
            if (ply > 0) {
                printf(",");
            }
            printf("{\"ply\":%d,\"missedWinCell\":", ply);
            if (rev.missed_win_cell == PD_NONE) {
                printf("null");
            } else {
                printf("%d", rev.missed_win_cell);
            }
            printf(",\"unsafeGive\":%s,\"saferGive\":", rev.unsafe_give ? "true" : "false");
            if (rev.safer_give == PD_NONE) {
                printf("null");
            } else {
                printf("%d", rev.safer_give);
            }
            printf(",\"forcedLoss\":%s}", rev.forced_loss ? "true" : "false");

            if (!pd_apply_move(&st, mv, &st)) {
                break;
            }
            ply++;
        }
        printf("]\n");
        return 0;
    }

    fprintf(stderr, "Unknown or incomplete command: %s\n", cmd);
    return 1;
}
