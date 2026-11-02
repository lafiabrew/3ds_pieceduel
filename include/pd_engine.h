#ifndef PD_ENGINE_H
#define PD_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PD_ATTRIBUTE_COUNT 4
#define PD_PIECE_COUNT 16
#define PD_BOARD_DIM 4
#define PD_BOARD_SIZE 16
#define PD_STD_LINES_COUNT 10
#define PD_SQUARES_COUNT 9
#define PD_ADV_LINES_COUNT 19
#define PD_NONE (-1)

typedef enum {
    PD_VARIANT_STANDARD = 0,
    PD_VARIANT_MISERE = 1,
    PD_VARIANT_ADVANCED = 2
} pd_variant_t;

typedef enum {
    PD_STATUS_PLAYING = 0,
    PD_STATUS_WIN = 1,
    PD_STATUS_DRAW = 2
} pd_status_t;

typedef enum {
    PD_MOVE_OPEN = 0,
    PD_MOVE_PLACE = 1
} pd_move_kind_t;

typedef enum {
    PD_DIFF_EASY = 0,
    PD_DIFF_MEDIUM = 1,
    PD_DIFF_HARD = 2
} pd_difficulty_t;

typedef struct {
    bool tall;
    bool light;
    bool square;
    bool solid;
} pd_attributes_t;

typedef struct {
    int cells[4];
} pd_line_t;

typedef struct {
    pd_move_kind_t kind;
    int cell; /* 0..15 for PD_MOVE_PLACE, PD_NONE for PD_MOVE_OPEN */
    int give; /* 0..15, or PD_NONE for terminal placement */
} pd_move_t;

typedef struct {
    int board[PD_BOARD_SIZE]; /* 0..15 or PD_NONE */
    int current;              /* 0 or 1 */
    int piece_in_hand;        /* 0..15 or PD_NONE */
    int available[PD_PIECE_COUNT];
    int available_count;      /* 0..16 */
    pd_status_t status;
    int winner;               /* 0, 1, or PD_NONE */
    bool has_winning_line;
    pd_line_t winning_line;
    int move_count;
    pd_variant_t variant;
} pd_game_state_t;

typedef struct {
    uint32_t state;
} pd_rng_t;

typedef struct {
    int board[PD_BOARD_SIZE];
    int piece_in_hand;
    int solution[PD_BOARD_SIZE];
    int solution_count;
    int pieces;
} pd_daily_puzzle_t;

typedef struct {
    int cell;
    int give;
} pd_first_move_t;

typedef struct {
    int board[PD_BOARD_SIZE];
    int piece_in_hand;
    int available[PD_PIECE_COUNT];
    int available_count;
    pd_first_move_t first_moves[256];
    int first_moves_count;
    int pieces;
} pd_win_in_two_t;

typedef struct {
    int board[PD_BOARD_SIZE];
    int candidates[PD_PIECE_COUNT];
    int candidates_count;
    int safe[PD_PIECE_COUNT];
    int safe_count;
    int pieces;
    int threats;
} pd_safe_give_t;

typedef struct {
    int board[PD_BOARD_SIZE];
    int hot[PD_BOARD_SIZE];
    int hot_count;
    int threats;
    int pieces;
} pd_threat_drill_t;

typedef struct {
    int missed_win_cell; /* 0..15 or PD_NONE */
    bool unsafe_give;
    int safer_give;      /* 0..15 or PD_NONE */
    bool forced_loss;
} pd_move_review_t;

typedef struct {
    int trait_samples[6];
    int find_win_board[PD_BOARD_SIZE];
    int find_win_piece;
    int find_win_cell;
    int safe_give_board[PD_BOARD_SIZE];
    const char *safe_give_shared_trait;
    int safe_give_candidates[4];
    int safe_give_safe[2];
    int safe_give_unsafe[2];
} pd_tutorial_data_t;

/* attributes.c */
bool pd_is_piece_id(int value);
bool pd_decode_piece(int piece, pd_attributes_t *out);
int pd_encode_piece(pd_attributes_t attrs);
const char *pd_piece_short_label(int piece, char buf[8]);

/* board.c */
extern const pd_line_t PD_LINES[PD_STD_LINES_COUNT];
extern const pd_line_t PD_SQUARES[PD_SQUARES_COUNT];
extern const pd_line_t PD_ADVANCED_LINES[PD_ADV_LINES_COUNT];

const pd_line_t *pd_lines_for_variant(pd_variant_t variant, int *out_count);
void pd_empty_board(int board[PD_BOARD_SIZE]);
bool pd_is_cell_index(int value);
int pd_empty_cells(const int board[PD_BOARD_SIZE], int out_cells[PD_BOARD_SIZE]);
bool pd_is_board_full(const int board[PD_BOARD_SIZE]);
const char *pd_variant_to_str(pd_variant_t variant);
pd_variant_t pd_variant_from_str(const char *str);

/* rules.c */
bool pd_find_winning_line(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count, pd_line_t *out_line);
bool pd_is_win(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count);
bool pd_would_win(const int board[PD_BOARD_SIZE], int cell, int piece, const pd_line_t *lines, int line_count);
uint16_t pd_winning_pieces_mask(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count);
int pd_winning_pieces(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count, int out_pieces[PD_PIECE_COUNT]);
int pd_open_threats(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count);
int pd_threat_cells(const int board[PD_BOARD_SIZE], const pd_line_t *lines, int line_count, int out_cells[PD_BOARD_SIZE]);

/* state.c */
void pd_create_game(pd_game_state_t *out, int starting_player, pd_variant_t variant);
bool pd_apply_move(const pd_game_state_t *state, pd_move_t move, pd_game_state_t *out);
int pd_legal_cells(const pd_game_state_t *state, int out_cells[PD_BOARD_SIZE]);
int pd_legal_moves(const pd_game_state_t *state, pd_move_t out_moves[256]);

/* ai.c */
void pd_rng_init(pd_rng_t *rng, uint32_t seed);
double pd_rng_next(pd_rng_t *rng);
int pd_hard_budget_for(pd_variant_t variant);
bool pd_choose_move(const pd_game_state_t *state, pd_difficulty_t level, pd_rng_t *rng, pd_move_t *out_move);
const char *pd_difficulty_to_str(pd_difficulty_t diff);
pd_difficulty_t pd_difficulty_from_str(const char *str);

/* puzzle.c */
uint32_t pd_date_seed(const char *iso_date);
void pd_generate_daily_puzzle(uint32_t seed, pd_variant_t variant, pd_daily_puzzle_t *out);
int pd_safe_gives_for(const int board[PD_BOARD_SIZE], const int *candidates, int candidates_count,
                      const pd_line_t *lines, int line_count, int out_safe[PD_PIECE_COUNT]);
void pd_generate_safe_give(uint32_t seed, pd_variant_t variant, pd_safe_give_t *out);
void pd_generate_threat_drill(uint32_t seed, pd_variant_t variant, pd_threat_drill_t *out);

/* forced_win.c */
bool pd_has_win_in_one(const pd_game_state_t *state);
int pd_forced_win_in_two_moves(const pd_game_state_t *state, pd_first_move_t out_moves[256]);
bool pd_has_forced_win_in_two(const pd_game_state_t *state);
void pd_generate_win_in_two(uint32_t seed, pd_variant_t variant, pd_win_in_two_t *out);

/* review.c */
void pd_review_move(const pd_game_state_t *before, int played_cell, int played_give, pd_move_review_t *out);

/* tutorial.c */
void pd_get_tutorial_data(pd_tutorial_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PD_ENGINE_H */
