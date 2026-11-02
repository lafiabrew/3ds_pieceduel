#ifndef PD_STORAGE_H
#define PD_STORAGE_H

#include "pd_engine.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PD_SAVE_MAGIC 0x56534450u /* "PDSV" little-endian */
#define PD_SAVE_VERSION 1u
#define PD_MAX_REPLAYS 20
#define PD_MAX_REPLAY_MOVES 17
#define PD_DEFAULT_SAVE_PATH_3DS "sdmc:/3ds/pieceduel/save.dat"

typedef enum {
    PD_GAME_MODE_AI = 0,
    PD_GAME_MODE_PASS_PLAY = 1,
    PD_GAME_MODE_P2P_UDS = 2,
    PD_GAME_MODE_P2P_LAN = 3,
    PD_GAME_MODE_WEB_PVP = 4
} pd_game_mode_t;

typedef struct {
    uint8_t ply;
    uint8_t seat;
    int8_t cell;         /* 0..15 or -1 for open */
    int8_t placed_piece; /* 0..15 or -1 for open */
    int8_t given_piece;  /* 0..15 or -1 for terminal */
} pd_replay_move_t;

typedef struct {
    bool valid;
    pd_variant_t variant;
    pd_game_mode_t mode;
    pd_difficulty_t ai_level;
    int8_t player_seat;  /* 0 or 1 */
    int8_t winner;       /* 0, 1, or -1 for draw */
    uint8_t move_count;
    pd_replay_move_t moves[PD_MAX_REPLAY_MOVES];
    char opponent[32];
    char date[11];       /* YYYY-MM-DD */
} pd_saved_game_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;

    /* Daily streak */
    int streak_current;
    int streak_best;
    int streak_total;
    char last_solved_date[11];

    /* Match & Trainer statistics */
    int ai_wins[3];
    int ai_losses[3];
    int ai_draws[3];
    int pvp_wins;
    int pvp_losses;
    int pvp_draws;
    int elo_rating;
    int rush_best_score;
    int rush_best_streak;
    int tutorial_completed;

    /* User preferences & Web Cross-Play config */
    bool colorblind_mode;
    bool show_threat_hints;
    char nickname[32];
    char server_host[128];
    uint16_t server_port;
    char web_username[64];
    char session_cookie[512];

    /* Ring buffer of recent games for post-game Replay & Move Coaching */
    int replay_head;
    int replay_count;
    pd_saved_game_t replays[PD_MAX_REPLAYS];
} pd_save_data_t;

uint32_t pd_crc32(const void *data, size_t len);
void pd_storage_init_default(pd_save_data_t *save);
bool pd_storage_load(pd_save_data_t *save, const char *path);
bool pd_storage_save(pd_save_data_t *save, const char *path);

void pd_storage_record_daily_solve(pd_save_data_t *save, const char *iso_date);
void pd_storage_record_game(pd_save_data_t *save, const pd_saved_game_t *game);
bool pd_storage_reconstruct_state(const pd_saved_game_t *game, int up_to_ply, pd_game_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* PD_STORAGE_H */
