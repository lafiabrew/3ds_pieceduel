#ifndef PD_UI_H
#define PD_UI_H

#include "pd_engine.h"
#include "pd_net.h"
#include "pd_platform.h"
#include "pd_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PD_SCREEN_TITLE = 0,
    PD_SCREEN_MATCH = 1,
    PD_SCREEN_DAILY = 2,
    PD_SCREEN_TRAINERS = 3,
    PD_SCREEN_TUTORIAL = 4,
    PD_SCREEN_REPLAY = 5,
    PD_SCREEN_MULTIPLAYER = 6,
    PD_SCREEN_STATS = 7
} pd_screen_t;

typedef enum {
    PD_TRAINER_PRACTICE = 0,
    PD_TRAINER_WIN_IN_TWO = 1,
    PD_TRAINER_SAFE_GIVE = 2,
    PD_TRAINER_HOT_SQUARES = 3,
    PD_TRAINER_RUSH = 4
} pd_trainer_kind_t;

typedef struct {
    pd_screen_t screen;
    bool running;

    /* Title & Mode selection state */
    int menu_index;
    pd_game_mode_t selected_mode;
    pd_difficulty_t selected_difficulty;
    pd_variant_t selected_variant;

    /* Active match state */
    pd_game_state_t game;
    pd_saved_game_t current_record;
    int selected_cell;  /* 0..15 cursor on 4x4 board */
    int selected_piece; /* 0..15 cursor on 16-piece tray */
    int pending_cell;   /* cell chosen in step 1 of place+give, or PD_NONE */
    bool cursor_on_tray;
    pd_rng_t rng;
    char status_banner[128];

    /* Daily & Trainers state */
    char daily_date[11]; /* YYYY-MM-DD */
    uint32_t puzzle_seed;
    pd_daily_puzzle_t daily_puzzle;
    int daily_attempts;
    bool daily_solved;
    bool daily_revealed;

    pd_trainer_kind_t trainer_kind;
    pd_win_in_two_t win2_puzzle;
    int win2_step; /* 0 = choose first place+give, 1 = choose winning finish */
    pd_safe_give_t safe_give_puzzle;
    pd_threat_drill_t threat_drill_puzzle;
    uint16_t threat_found_mask;
    bool trainer_solved;
    bool trainer_revealed;
    char trainer_feedback[128];

    /* Puzzle Rush state */
    bool rush_active;
    int rush_score;
    int rush_streak;
    int rush_strikes;
    uint64_t rush_end_ms;

    /* Interactive Tutorial state */
    int tutorial_step; /* 0 = Traits, 1 = Find Win, 2 = Safe Give, 3 = Complete */
    pd_tutorial_data_t tutorial_data;
    bool tutorial_step_done;

    /* Replay & Move Coaching state */
    int replay_game_index;
    int replay_ply;
    pd_game_state_t replay_state;
    pd_move_review_t replay_review;
    int replay_blunders;
    int replay_missed_wins;
    int replay_forced_losses;

    /* Persistence & Networking */
    char save_path[256];
    pd_save_data_t save;
    pd_net_ctx_t net;
    int mp_menu_index;
} pd_app_t;

/* app.c */
void pd_app_init(pd_app_t *app, const char *save_path);
void pd_app_shutdown(pd_app_t *app);
void pd_app_start_match(pd_app_t *app, pd_game_mode_t mode, pd_difficulty_t diff, pd_variant_t variant);
void pd_app_load_daily(pd_app_t *app, const char *iso_date);
void pd_app_load_trainer(pd_app_t *app, pd_trainer_kind_t kind, uint32_t seed);
void pd_app_open_replay(pd_app_t *app, int replay_index);
void pd_app_step_replay(pd_app_t *app, int delta_ply);
void pd_app_update(pd_app_t *app, const pd_input_t *input);

/* ui_input.c */
int pd_ui_hit_test_board(int touch_x, int touch_y);
int pd_ui_hit_test_tray(int touch_x, int touch_y);
int pd_ui_hit_test_bottom_bar(int touch_x, int touch_y);
void pd_ui_handle_match_input(pd_app_t *app, const pd_input_t *input);

/* ui_render.c */
void pd_ui_render(const pd_app_t *app, float slider_3d);

#ifdef __cplusplus
}
#endif

#endif /* PD_UI_H */
