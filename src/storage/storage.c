#include "pd_storage.h"
#include "pd_net.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

uint32_t pd_crc32(const void *data, size_t len) {
    if (!data && len > 0) {
        return 0;
    }
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static uint32_t pd_storage_compute_crc(const pd_save_data_t *save) {
    size_t off = offsetof(pd_save_data_t, streak_current);
    const uint8_t *base = (const uint8_t *)save;
    return pd_crc32(base + off, sizeof(pd_save_data_t) - off);
}

void pd_storage_init_default(pd_save_data_t *save) {
    if (!save) {
        return;
    }
    memset(save, 0, sizeof(*save));
    save->magic = PD_SAVE_MAGIC;
    save->version = PD_SAVE_VERSION;
    save->elo_rating = 1200;
    save->show_threat_hints = true;
    strcpy(save->nickname, "3DSDuelist");
    strcpy(save->server_host, "127.0.0.1");
    save->server_port = PD_WEB_DEFAULT_PORT;
    save->crc32 = pd_storage_compute_crc(save);
}

static void pd_mkdir_parents(const char *path) {
    if (!path) {
        return;
    }
    char buf[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) {
        return;
    }
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/') {
            if (buf[i - 1] == ':') {
                continue;
            }
            buf[i] = '\0';
            (void)mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
}

bool pd_storage_save(pd_save_data_t *save, const char *path) {
    if (!save || !path || path[0] == '\0') {
        return false;
    }
    save->magic = PD_SAVE_MAGIC;
    save->version = PD_SAVE_VERSION;
    save->crc32 = pd_storage_compute_crc(save);

    pd_mkdir_parents(path);

    char tmp_path[512];
    int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp_path)) {
        return false;
    }

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        return false;
    }
    size_t written = fwrite(save, sizeof(pd_save_data_t), 1, fp);
    int flush_rc = fflush(fp);
    int close_rc = fclose(fp);
    if (written != 1 || flush_rc != 0 || close_rc != 0) {
        (void)remove(tmp_path);
        return false;
    }
    char bak_path[512];
    int bn = snprintf(bak_path, sizeof(bak_path), "%s.bak", path);
    bool moved_to_bak = false;
    if (bn > 0 && (size_t)bn < sizeof(bak_path)) {
        struct stat st;
        if (stat(path, &st) == 0) {
            (void)remove(bak_path);
            if (rename(path, bak_path) == 0) {
                moved_to_bak = true;
            }
        }
    }

    if (rename(tmp_path, path) != 0) {
        (void)remove(tmp_path);
        if (moved_to_bak) {
            (void)rename(bak_path, path);
        }
        return false;
    }
    return true;
}

static bool pd_storage_try_load_file(pd_save_data_t *save, const char *file_path) {
    if (!save || !file_path || file_path[0] == '\0') {
        return false;
    }
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        return false;
    }

    pd_save_data_t tmp;
    size_t n = fread(&tmp, 1, sizeof(pd_save_data_t), fp);
    int extra = fgetc(fp);
    fclose(fp);

    if (n != sizeof(pd_save_data_t) || extra != EOF) {
        return false;
    }
    if (tmp.magic != PD_SAVE_MAGIC || tmp.version != PD_SAVE_VERSION) {
        return false;
    }
    uint32_t expected_crc = pd_storage_compute_crc(&tmp);
    if (tmp.crc32 != expected_crc) {
        return false;
    }

    *save = tmp;
    return true;
}

bool pd_storage_load(pd_save_data_t *save, const char *path) {
    if (!save) {
        return false;
    }
    if (!path || path[0] == '\0') {
        pd_storage_init_default(save);
        return false;
    }

    if (pd_storage_try_load_file(save, path)) {
        return true;
    }

    char bak_path[512];
    int bn = snprintf(bak_path, sizeof(bak_path), "%s.bak", path);
    if (bn > 0 && (size_t)bn < sizeof(bak_path)) {
        if (pd_storage_try_load_file(save, bak_path)) {
            return true;
        }
    }

    pd_storage_init_default(save);
    return false;
}

static bool pd_parse_iso_date(const char *iso, int *out_y, int *out_m, int *out_d) {
    if (!iso || strlen(iso) < 10) {
        return false;
    }
    if (iso[4] != '-' || iso[7] != '-') {
        return false;
    }
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (iso[i] < '0' || iso[i] > '9') {
            return false;
        }
    }
    int y = (iso[0] - '0') * 1000 + (iso[1] - '0') * 100 + (iso[2] - '0') * 10 + (iso[3] - '0');
    int m = (iso[5] - '0') * 10 + (iso[6] - '0');
    int d = (iso[8] - '0') * 10 + (iso[9] - '0');
    if (m < 1 || m > 12 || d < 1 || d > 31) {
        return false;
    }
    *out_y = y;
    *out_m = m;
    *out_d = d;
    return true;
}

static int64_t pd_days_from_civil(int y, int m, int d) {
    y -= (m <= 2) ? 1 : 0;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

void pd_storage_record_daily_solve(pd_save_data_t *save, const char *iso_date) {
    if (!save || !iso_date || strlen(iso_date) < 10) {
        return;
    }
    if (strncmp(save->last_solved_date, iso_date, 10) == 0 && save->last_solved_date[10] == '\0') {
        return;
    }

    int y1 = 0, m1 = 0, d1 = 0;
    int y2 = 0, m2 = 0, d2 = 0;
    bool has_prev = pd_parse_iso_date(save->last_solved_date, &y1, &m1, &d1);
    bool has_curr = pd_parse_iso_date(iso_date, &y2, &m2, &d2);

    if (has_prev && has_curr &&
        (pd_days_from_civil(y2, m2, d2) - pd_days_from_civil(y1, m1, d1) == 1)) {
        save->streak_current += 1;
    } else {
        save->streak_current = 1;
    }

    if (save->streak_current > save->streak_best) {
        save->streak_best = save->streak_current;
    }
    save->streak_total += 1;
    if (iso_date != save->last_solved_date) {
        strncpy(save->last_solved_date, iso_date, 10);
    }
    save->last_solved_date[10] = '\0';
    save->crc32 = pd_storage_compute_crc(save);
}

/* Pure C 10^x helper for Elo expected score without requiring -lm */
static double pd_pow10(double x) {
    if (x > 4.0) {
        x = 4.0;
    } else if (x < -4.0) {
        x = -4.0;
    }
    double y = x * 2.30258509299404568402; /* x * ln(10) */
    y *= 0.0625;                           /* y / 16 */
    double term = 1.0;
    double sum = 1.0;
    for (int i = 1; i <= 10; i++) {
        term *= y / (double)i;
        sum += term;
    }
    for (int i = 0; i < 4; i++) {
        sum *= sum;
    }
    return sum;
}

void pd_storage_record_game(pd_save_data_t *save, const pd_saved_game_t *game) {
    if (!save || !game) {
        return;
    }

    int head = save->replay_head;
    if (head < 0 || head >= PD_MAX_REPLAYS) {
        head = 0;
    }
    save->replays[head] = *game;
    save->replays[head].valid = true;
    save->replay_head = (head + 1) % PD_MAX_REPLAYS;
    if (save->replay_count < PD_MAX_REPLAYS) {
        save->replay_count += 1;
    }

    int player_seat = (game->player_seat == 1) ? 1 : 0;
    double actual = 0.5;
    if (game->winner < 0) {
        actual = 0.5;
    } else if (game->winner == player_seat) {
        actual = 1.0;
    } else {
        actual = 0.0;
    }

    double opp_rating = 1200.0;
    if (game->mode == PD_GAME_MODE_AI) {
        int lvl = (int)game->ai_level;
        if (lvl < 0) {
            lvl = 0;
        } else if (lvl > 2) {
            lvl = 2;
        }
        if (game->winner < 0) {
            save->ai_draws[lvl] += 1;
        } else if (game->winner == player_seat) {
            save->ai_wins[lvl] += 1;
        } else {
            save->ai_losses[lvl] += 1;
        }
        if (lvl == PD_DIFF_EASY) {
            opp_rating = 1000.0;
        } else if (lvl == PD_DIFF_MEDIUM) {
            opp_rating = 1200.0;
        } else {
            opp_rating = 1450.0;
        }
    } else {
        if (game->winner < 0) {
            save->pvp_draws += 1;
        } else if (game->winner == player_seat) {
            save->pvp_wins += 1;
        } else {
            save->pvp_losses += 1;
        }
        opp_rating = 1200.0;
    }

    double expected = 1.0 / (1.0 + pd_pow10((opp_rating - (double)save->elo_rating) / 400.0));
    double delta = 32.0 * (actual - expected);
    int delta_int = (delta >= 0.0) ? (int)(delta + 0.5) : (int)(delta - 0.5);
    save->elo_rating += delta_int;
    if (save->elo_rating < 100) {
        save->elo_rating = 100;
    }

    save->crc32 = pd_storage_compute_crc(save);
}

bool pd_storage_reconstruct_state(const pd_saved_game_t *game, int up_to_ply, pd_game_state_t *out_state) {
    if (!game || !out_state) {
        return false;
    }
    pd_create_game(out_state, 0, game->variant);

    int limit = up_to_ply;
    if (limit < 0) {
        limit = 0;
    }
    if (limit > (int)game->move_count) {
        limit = (int)game->move_count;
    }
    if (limit > PD_MAX_REPLAY_MOVES) {
        limit = PD_MAX_REPLAY_MOVES;
    }

    for (int i = 0; i < limit; i++) {
        pd_move_t move;
        move.kind = (game->moves[i].cell < 0) ? PD_MOVE_OPEN : PD_MOVE_PLACE;
        move.cell = (int)game->moves[i].cell;
        move.give = (int)game->moves[i].given_piece;

        pd_game_state_t next_state;
        if (!pd_apply_move(out_state, move, &next_state)) {
            return false;
        }
        *out_state = next_state;
    }
    return true;
}
