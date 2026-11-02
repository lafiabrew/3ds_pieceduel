#include "pd_ui.h"

#include <stdio.h>
#include <string.h>

static bool pd_app_parse_iso_date(const char *iso, int *out_y, int *out_m, int *out_d) {
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

static int64_t pd_app_days_from_civil(int y, int m, int d) {
    y -= (m <= 2) ? 1 : 0;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void pd_app_civil_from_days(int64_t z, int *out_y, int *out_m, int *out_d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned m = (mp < 10u) ? (mp + 3u) : (mp - 9u);
    y += (m <= 2u) ? 1 : 0;
    *out_y = y;
    *out_m = (int)m;
    *out_d = (int)d;
}

static void pd_app_step_iso_date(const char *iso_in, int delta_days, char out_iso[11]) {
    int y = 2026, m = 1, d = 1;
    if (!pd_app_parse_iso_date(iso_in, &y, &m, &d)) {
        y = 2026;
        m = 1;
        d = 1;
    }
    int64_t days = pd_app_days_from_civil(y, m, d) + (int64_t)delta_days;
    pd_app_civil_from_days(days, &y, &m, &d);
    if (y < 1970) {
        y = 1970;
    } else if (y > 9999) {
        y = 9999;
    }
    if (m < 1) {
        m = 1;
    } else if (m > 12) {
        m = 12;
    }
    if (d < 1) {
        d = 1;
    } else if (d > 31) {
        d = 31;
    }
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%04u-%02u-%02u",
             (unsigned)y % 10000u, (unsigned)m % 100u, (unsigned)d % 100u);
    memcpy(out_iso, tmp, 10);
    out_iso[10] = '\0';
}

static void pd_app_record_move_transition(pd_saved_game_t *rec,
                                          const pd_game_state_t *before,
                                          const pd_game_state_t *after) {
    if (!rec || !before || !after) {
        return;
    }
    if (rec->move_count >= PD_MAX_REPLAY_MOVES) {
        return;
    }
    pd_replay_move_t *rm = &rec->moves[rec->move_count];
    rm->ply = rec->move_count;
    rm->seat = (uint8_t)before->current;
    if (before->piece_in_hand == PD_NONE) {
        rm->cell = -1;
        rm->placed_piece = -1;
        rm->given_piece = (int8_t)after->piece_in_hand;
    } else {
        int placed_cell = -1;
        for (int i = 0; i < PD_BOARD_SIZE; i++) {
            if (before->board[i] == PD_NONE && after->board[i] != PD_NONE) {
                placed_cell = i;
                break;
            }
        }
        rm->cell = (int8_t)placed_cell;
        rm->placed_piece = (int8_t)before->piece_in_hand;
        rm->given_piece = (after->status == PD_STATUS_PLAYING)
                              ? (int8_t)after->piece_in_hand
                              : -1;
    }
    rec->move_count += 1;
}

static bool g_match_recorded = false;

static void pd_app_finalize_match_if_done(pd_app_t *app) {
    if (!app || app->game.status == PD_STATUS_PLAYING || g_match_recorded) {
        return;
    }
    g_match_recorded = true;
    app->current_record.valid = true;
    if (app->game.status == PD_STATUS_WIN) {
        app->current_record.winner = (int8_t)app->game.winner;
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "Player %d wins! [X] Coaching [SELECT] Rematch",
                 app->game.winner + 1);
    } else {
        app->current_record.winner = -1;
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "Draw! [X] Coaching [SELECT] Rematch");
    }
    pd_storage_record_game(&app->save, &app->current_record);
    if (app->save_path[0] != '\0') {
        pd_storage_save(&app->save, app->save_path);
    }
}

void pd_app_init(pd_app_t *app, const char *save_path) {
    if (!app) {
        return;
    }
    memset(app, 0, sizeof(*app));
    g_match_recorded = false;
    app->screen = PD_SCREEN_TITLE;
    app->running = true;
    app->menu_index = 0;
    app->selected_mode = PD_GAME_MODE_AI;
    app->selected_difficulty = PD_DIFF_MEDIUM;
    app->selected_variant = PD_VARIANT_STANDARD;
    app->selected_cell = 0;
    app->selected_piece = 0;
    app->pending_cell = PD_NONE;
    app->cursor_on_tray = true;
    app->replay_review.missed_win_cell = PD_NONE;
    app->replay_review.safer_give = PD_NONE;

    const char *resolved_path = (save_path && save_path[0] != '\0')
                                    ? save_path
                                    : pd_platform_default_save_path();
    if (resolved_path && resolved_path[0] != '\0') {
        snprintf(app->save_path, sizeof(app->save_path), "%s", resolved_path);
    } else {
        snprintf(app->save_path, sizeof(app->save_path), "%s", PD_DEFAULT_SAVE_PATH_3DS);
    }

    pd_storage_load(&app->save, app->save_path);

    uint32_t seed = (uint32_t)pd_platform_ticks_ms();
    if (seed == 0) {
        seed = 0x12345678u;
    }
    pd_rng_init(&app->rng, seed);

    pd_platform_get_utc_date(app->daily_date);
    if (app->daily_date[0] == '\0') {
        strcpy(app->daily_date, "2026-01-01");
    }
    app->puzzle_seed = pd_date_seed(app->daily_date);

    pd_get_tutorial_data(&app->tutorial_data);
    pd_create_game(&app->game, 0, app->selected_variant);

    pd_net_init(&app->net, app->save.nickname[0] ? app->save.nickname : "3DSDuelist");
    pd_net_web_configure(&app->net, app->save.server_host, app->save.server_port, app->save.session_cookie);

    snprintf(app->status_banner, sizeof(app->status_banner), "PieceDuel 3DS Ready");
}

void pd_app_shutdown(pd_app_t *app) {
    if (!app) {
        return;
    }
    app->running = false;
    pd_net_close(&app->net);
    if (app->save_path[0] != '\0') {
        pd_storage_save(&app->save, app->save_path);
    }
}

void pd_app_start_match(pd_app_t *app, pd_game_mode_t mode, pd_difficulty_t diff, pd_variant_t variant) {
    if (!app) {
        return;
    }
    app->screen = PD_SCREEN_MATCH;
    app->selected_mode = mode;
    app->selected_difficulty = diff;
    app->selected_variant = variant;
    app->selected_cell = 0;
    app->selected_piece = 0;
    app->pending_cell = PD_NONE;
    app->cursor_on_tray = true;

    pd_create_game(&app->game, 0, variant);
    if ((mode == PD_GAME_MODE_P2P_UDS || mode == PD_GAME_MODE_P2P_LAN || mode == PD_GAME_MODE_WEB_PVP) &&
        app->net.state == PD_NET_STATE_ACTIVE && app->net.game_state.available_count > 0) {
        app->game = app->net.game_state;
        app->cursor_on_tray = (app->game.piece_in_hand == PD_NONE);
    }

    memset(&app->current_record, 0, sizeof(app->current_record));
    g_match_recorded = false;
    app->current_record.valid = false;
    app->current_record.variant = variant;
    app->current_record.mode = mode;
    app->current_record.ai_level = diff;
    app->current_record.player_seat =
        (mode == PD_GAME_MODE_P2P_UDS || mode == PD_GAME_MODE_P2P_LAN || mode == PD_GAME_MODE_WEB_PVP)
            ? (int8_t)app->net.my_seat
            : 0;
    app->current_record.winner = -1;
    app->current_record.move_count = 0;

    if (mode == PD_GAME_MODE_AI) {
        snprintf(app->current_record.opponent, sizeof(app->current_record.opponent),
                 "AI (%s)", pd_difficulty_to_str(diff));
    } else if (mode == PD_GAME_MODE_PASS_PLAY) {
        snprintf(app->current_record.opponent, sizeof(app->current_record.opponent), "Player 2");
    } else {
        snprintf(app->current_record.opponent, sizeof(app->current_record.opponent), "%s",
                 app->net.opponent_name[0] ? app->net.opponent_name : "Remote Peer");
    }

    if (app->daily_date[0] != '\0') {
        snprintf(app->current_record.date, sizeof(app->current_record.date), "%s", app->daily_date);
    } else {
        pd_platform_get_utc_date(app->current_record.date);
    }

    snprintf(app->status_banner, sizeof(app->status_banner),
             "Select opening piece to give opponent");
}

void pd_app_load_daily(pd_app_t *app, const char *iso_date) {
    if (!app) {
        return;
    }
    app->screen = PD_SCREEN_DAILY;
    if (iso_date && iso_date[0] != '\0') {
        if (iso_date != app->daily_date) {
            strncpy(app->daily_date, iso_date, 10);
            app->daily_date[10] = '\0';
        }
    } else if (app->daily_date[0] == '\0') {
        pd_platform_get_utc_date(app->daily_date);
    }

    app->puzzle_seed = pd_date_seed(app->daily_date);
    pd_generate_daily_puzzle(app->puzzle_seed, app->selected_variant, &app->daily_puzzle);
    pd_empty_board(app->game.board);
    app->game.piece_in_hand = app->daily_puzzle.piece_in_hand;
    app->daily_attempts = 0;
    app->daily_solved = false;
    app->daily_revealed = false;
    app->selected_cell = 0;
    app->cursor_on_tray = false;

    snprintf(app->status_banner, sizeof(app->status_banner),
             "Daily Puzzle: %s (L/R: Archive)", app->daily_date);
}

void pd_app_load_trainer(pd_app_t *app, pd_trainer_kind_t kind, uint32_t seed) {
    if (!app) {
        return;
    }
    app->screen = PD_SCREEN_TRAINERS;
    app->trainer_kind = kind;
    if (seed == 0) {
        seed = (uint32_t)(pd_rng_next(&app->rng) * 4294967295.0) + 1u;
    }
    app->puzzle_seed = seed;
    app->trainer_solved = false;
    app->trainer_revealed = false;
    app->selected_cell = 0;
    app->selected_piece = 0;
    app->pending_cell = PD_NONE;

    switch (kind) {
    case PD_TRAINER_PRACTICE:
        pd_generate_daily_puzzle(seed, app->selected_variant, &app->daily_puzzle);
        memcpy(app->game.board, app->daily_puzzle.board, sizeof(app->game.board));
        app->game.piece_in_hand = app->daily_puzzle.piece_in_hand;
        app->cursor_on_tray = false;
        snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                 "Practice: Place piece %d to win in 1", app->daily_puzzle.piece_in_hand);
        break;

    case PD_TRAINER_WIN_IN_TWO:
        pd_generate_win_in_two(seed, app->selected_variant, &app->win2_puzzle);
        pd_empty_board(app->game.board);
        memcpy(app->game.available, app->win2_puzzle.available, sizeof(app->game.available));
        app->game.available_count = app->win2_puzzle.available_count;
        app->win2_step = 0;
        app->cursor_on_tray = false;
        snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                 "Win in Two: Step 1 - Pick cell & give piece");
        break;

    case PD_TRAINER_SAFE_GIVE:
        pd_generate_safe_give(seed, app->selected_variant, &app->safe_give_puzzle);
        pd_empty_board(app->game.board);
        memcpy(app->game.available, app->safe_give_puzzle.candidates, sizeof(app->game.available));
        app->game.available_count = app->safe_give_puzzle.candidates_count;
        app->cursor_on_tray = true;
        if (app->safe_give_puzzle.candidates_count > 0) {
            app->selected_piece = app->safe_give_puzzle.candidates[0];
        }
        snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                 "Safe Give: Select a safe piece to give");
        break;

    case PD_TRAINER_HOT_SQUARES:
        pd_generate_threat_drill(seed, app->selected_variant, &app->threat_drill_puzzle);
        pd_empty_board(app->game.board);
        app->threat_found_mask = 0;
        app->cursor_on_tray = false;
        if (app->threat_drill_puzzle.hot_count == 0) {
            app->trainer_solved = true;
        }
        snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                 "Hot Squares: Toggle all %d threat cells", app->threat_drill_puzzle.hot_count);
        break;

    case PD_TRAINER_RUSH:
        pd_generate_daily_puzzle(seed, app->selected_variant, &app->daily_puzzle);
        memcpy(app->game.board, app->daily_puzzle.board, sizeof(app->game.board));
        app->game.piece_in_hand = app->daily_puzzle.piece_in_hand;
        app->cursor_on_tray = false;
        if (!app->rush_active) {
            app->rush_active = true;
            app->rush_score = 0;
            app->rush_streak = 0;
            app->rush_strikes = 0;
            app->rush_end_ms = pd_platform_ticks_ms() + 180000ULL;
        }
        snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                 "Puzzle Rush! Score: %d Streak: %d", app->rush_score, app->rush_streak);
        break;
    }

    snprintf(app->status_banner, sizeof(app->status_banner), "%s", app->trainer_feedback);
}

void pd_app_open_replay(pd_app_t *app, int replay_index) {
    if (!app) {
        return;
    }
    app->screen = PD_SCREEN_REPLAY;

    if (app->save.replay_count <= 0) {
        app->replay_game_index = 0;
        app->replay_ply = 0;
        pd_create_game(&app->replay_state, 0, app->selected_variant);
        app->replay_review.missed_win_cell = PD_NONE;
        app->replay_review.unsafe_give = false;
        app->replay_review.safer_give = PD_NONE;
        app->replay_review.forced_loss = false;
        app->replay_blunders = 0;
        app->replay_missed_wins = 0;
        app->replay_forced_losses = 0;
        snprintf(app->status_banner, sizeof(app->status_banner), "No saved replays available");
        return;
    }

    if (replay_index < 0) {
        replay_index = 0;
    } else if (replay_index >= app->save.replay_count) {
        replay_index = app->save.replay_count - 1;
    }

    app->replay_game_index = replay_index;
    app->replay_ply = 0;
    pd_app_step_replay(app, 0);
}

void pd_app_step_replay(pd_app_t *app, int delta_ply) {
    if (!app) {
        return;
    }
    if (app->save.replay_count <= 0) {
        return;
    }
    int idx = app->replay_game_index;
    if (idx < 0 || idx >= PD_MAX_REPLAYS || !app->save.replays[idx].valid) {
        idx = 0;
        app->replay_game_index = 0;
    }
    const pd_saved_game_t *rec = &app->save.replays[idx];
    app->current_record = *rec;
    int max_ply = (int)rec->move_count;

    int next_ply = app->replay_ply + delta_ply;
    if (next_ply < 0) {
        next_ply = 0;
    } else if (next_ply > max_ply) {
        next_ply = max_ply;
    }
    app->replay_ply = next_ply;

    pd_storage_reconstruct_state(rec, app->replay_ply, &app->replay_state);

    app->replay_review.missed_win_cell = PD_NONE;
    app->replay_review.unsafe_give = false;
    app->replay_review.safer_give = PD_NONE;
    app->replay_review.forced_loss = false;

    if (app->replay_ply < max_ply) {
        const pd_replay_move_t *mv = &rec->moves[app->replay_ply];
        pd_review_move(&app->replay_state, (int)mv->cell, (int)mv->given_piece, &app->replay_review);
    } else if (max_ply > 0) {
        pd_game_state_t before_last;
        pd_storage_reconstruct_state(rec, max_ply - 1, &before_last);
        const pd_replay_move_t *mv = &rec->moves[max_ply - 1];
        pd_review_move(&before_last, (int)mv->cell, (int)mv->given_piece, &app->replay_review);
    }

    int blunders = 0;
    int missed_wins = 0;
    int forced_losses = 0;
    pd_game_state_t scan_state;
    pd_create_game(&scan_state, 0, rec->variant);
    for (int i = 0; i < max_ply && i < PD_MAX_REPLAY_MOVES; i++) {
        pd_move_review_t rev;
        pd_review_move(&scan_state, (int)rec->moves[i].cell, (int)rec->moves[i].given_piece, &rev);
        if (rev.missed_win_cell != PD_NONE) {
            missed_wins += 1;
        }
        if (rev.unsafe_give) {
            blunders += 1;
        }
        if (rev.forced_loss) {
            forced_losses += 1;
        }
        pd_move_t m;
        m.kind = (rec->moves[i].cell < 0) ? PD_MOVE_OPEN : PD_MOVE_PLACE;
        m.cell = (int)rec->moves[i].cell;
        m.give = (int)rec->moves[i].given_piece;
        pd_game_state_t next_state;
        if (!pd_apply_move(&scan_state, m, &next_state)) {
            break;
        }
        scan_state = next_state;
    }

    app->replay_blunders = blunders;
    app->replay_missed_wins = missed_wins;
    app->replay_forced_losses = forced_losses;

    snprintf(app->status_banner, sizeof(app->status_banner),
             "Replay #%d Ply %d/%d (Blunders:%d Missed:%d)",
             app->replay_game_index + 1, app->replay_ply, max_ply,
             app->replay_blunders, app->replay_missed_wins);
}

static void pd_app_enter_title_selection(pd_app_t *app) {
    switch (app->menu_index) {
    case 0:
        pd_app_start_match(app, PD_GAME_MODE_AI, app->selected_difficulty, app->selected_variant);
        break;
    case 1:
        pd_app_start_match(app, PD_GAME_MODE_PASS_PLAY, app->selected_difficulty, app->selected_variant);
        break;
    case 2:
        if (app->daily_date[0] == '\0') {
            pd_platform_get_utc_date(app->daily_date);
        }
        pd_app_load_daily(app, app->daily_date);
        break;
    case 3:
        pd_app_load_trainer(app, app->trainer_kind, app->puzzle_seed ? app->puzzle_seed : 1001u);
        break;
    case 4:
        app->screen = PD_SCREEN_MULTIPLAYER;
        app->mp_menu_index = 0;
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "Multiplayer: UDS / LAN P2P / Web Cross-Play");
        break;
    case 5:
        app->screen = PD_SCREEN_TUTORIAL;
        app->tutorial_step = 0;
        app->tutorial_step_done = false;
        pd_get_tutorial_data(&app->tutorial_data);
        pd_empty_board(app->game.board);
        app->selected_cell = 0;
        app->selected_piece = 0;
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "Tutorial 1/3: Four Binary Traits (Press A)");
        break;
    case 6:
    default:
        app->screen = PD_SCREEN_STATS;
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "Stats & Replays (A: Replays, X: Colorblind, Y: Hints)");
        break;
    }
}

static void pd_app_update_title(pd_app_t *app, const pd_input_t *input) {
    if (input->down & PD_BTN_START) {
        app->running = false;
        return;
    }
    if (input->down & PD_BTN_DUP) {
        app->menu_index = (app->menu_index + 6) % 7;
    }
    if (input->down & PD_BTN_DDOWN) {
        app->menu_index = (app->menu_index + 1) % 7;
    }
    if (input->down & PD_BTN_L) {
        app->selected_variant = (pd_variant_t)(((int)app->selected_variant + 2) % 3);
    }
    if (input->down & PD_BTN_R) {
        app->selected_variant = (pd_variant_t)(((int)app->selected_variant + 1) % 3);
    }
    if (input->down & PD_BTN_X) {
        app->selected_difficulty = (pd_difficulty_t)(((int)app->selected_difficulty + 1) % 3);
    }
    if (input->down & PD_BTN_Y) {
        app->selected_difficulty = (pd_difficulty_t)(((int)app->selected_difficulty + 2) % 3);
    }

    if (input->touch_just_pressed) {
        if (input->touch_y >= 34 && input->touch_y < 188) {
            int idx = (input->touch_y - 34) / 22;
            if (idx < 0) {
                idx = 0;
            } else if (idx > 6) {
                idx = 6;
            }
            if (idx == app->menu_index) {
                pd_app_enter_title_selection(app);
                return;
            }
            app->menu_index = idx;
        }
    }

    if (input->down & PD_BTN_A) {
        pd_app_enter_title_selection(app);
    }
}

static void pd_app_update_match(pd_app_t *app, const pd_input_t *input) {
    bool is_net = (app->selected_mode == PD_GAME_MODE_P2P_UDS ||
                   app->selected_mode == PD_GAME_MODE_P2P_LAN ||
                   app->selected_mode == PD_GAME_MODE_WEB_PVP);

    if (is_net) {
        pd_net_poll(&app->net);
        if (app->net.state_updated) {
            if (app->net.game_state.move_count > app->game.move_count &&
                app->current_record.move_count < (uint8_t)app->net.game_state.move_count) {
                pd_app_record_move_transition(&app->current_record, &app->game, &app->net.game_state);
            }
            app->game = app->net.game_state;
            app->net.state_updated = false;
        }
    }

    if (input->down & PD_BTN_SELECT) {
        if (is_net) {
            pd_net_send_rematch(&app->net);
        }
        pd_app_start_match(app, app->selected_mode, app->selected_difficulty, app->selected_variant);
        return;
    }

    if (app->game.status != PD_STATUS_PLAYING && (input->down & PD_BTN_X)) {
        pd_app_finalize_match_if_done(app);
        int latest = (app->save.replay_head + PD_MAX_REPLAYS - 1) % PD_MAX_REPLAYS;
        if (latest >= app->save.replay_count) {
            latest = app->save.replay_count - 1;
        }
        pd_app_open_replay(app, latest);
        return;
    }

    bool had_pending = (app->pending_cell != PD_NONE);
    pd_game_state_t before_input = app->game;
    uint8_t rec_before = app->current_record.move_count;

    pd_ui_handle_match_input(app, input);

    if (app->screen != PD_SCREEN_MATCH) {
        return;
    }

    if ((input->down & PD_BTN_B) && !had_pending) {
        if (is_net) {
            pd_net_close(&app->net);
        }
        app->screen = PD_SCREEN_TITLE;
        return;
    }

    if (app->game.move_count > before_input.move_count &&
        app->current_record.move_count == rec_before) {
        pd_app_record_move_transition(&app->current_record, &before_input, &app->game);
    }

    if (app->selected_mode == PD_GAME_MODE_AI &&
        app->game.status == PD_STATUS_PLAYING &&
        app->game.current == 1) {
        pd_move_t ai_move;
        if (pd_choose_move(&app->game, app->selected_difficulty, &app->rng, &ai_move)) {
            pd_game_state_t before_ai = app->game;
            pd_game_state_t after_ai;
            if (pd_apply_move(&app->game, ai_move, &after_ai)) {
                app->game = after_ai;
                pd_app_record_move_transition(&app->current_record, &before_ai, &app->game);
                app->pending_cell = PD_NONE;
                app->cursor_on_tray = (app->game.piece_in_hand == PD_NONE);
            }
        }
    }

    pd_app_finalize_match_if_done(app);
}

static void pd_app_dpad_move_grid(int *cursor, const pd_input_t *input) {
    if (!cursor || !input) {
        return;
    }
    if (input->down & PD_BTN_DRIGHT) {
        *cursor = (*cursor + 1) & 15;
    }
    if (input->down & PD_BTN_DLEFT) {
        *cursor = (*cursor + 15) & 15;
    }
    if (input->down & PD_BTN_DDOWN) {
        *cursor = (*cursor + 4) & 15;
    }
    if (input->down & PD_BTN_DUP) {
        *cursor = (*cursor + 12) & 15;
    }
}

static void pd_app_submit_daily_cell(pd_app_t *app, int cell) {
    if (!app || cell < 0 || cell >= PD_BOARD_SIZE) {
        return;
    }
    app->daily_attempts += 1;
    bool is_sol = false;
    for (int i = 0; i < app->daily_puzzle.solution_count; i++) {
        if (app->daily_puzzle.solution[i] == cell) {
            is_sol = true;
            break;
        }
    }
    if (is_sol) {
        app->daily_solved = true;
        app->daily_puzzle.board[cell] = app->daily_puzzle.piece_in_hand;
        pd_storage_record_daily_solve(&app->save, app->daily_date);
        if (app->save_path[0] != '\0') {
            pd_storage_save(&app->save, app->save_path);
        }
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "Solved! Streak: %d (Best: %d)",
                 app->save.streak_current, app->save.streak_best);
    } else {
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "Not a winning cell (Attempt %d)", app->daily_attempts);
    }
}

static void pd_app_update_daily(pd_app_t *app, const pd_input_t *input) {
    if (input->down & PD_BTN_B) {
        app->screen = PD_SCREEN_TITLE;
        return;
    }
    if (input->down & PD_BTN_L) {
        char prev_date[11];
        pd_app_step_iso_date(app->daily_date, -1, prev_date);
        pd_app_load_daily(app, prev_date);
        return;
    }
    if (input->down & PD_BTN_R) {
        char next_date[11];
        pd_app_step_iso_date(app->daily_date, 1, next_date);
        pd_app_load_daily(app, next_date);
        return;
    }
    if (input->down & PD_BTN_Y) {
        app->daily_revealed = true;
        if (app->daily_puzzle.solution_count > 0) {
            app->selected_cell = app->daily_puzzle.solution[0];
        }
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "Solution revealed: Cell %d", app->selected_cell);
        return;
    }

    pd_app_dpad_move_grid(&app->selected_cell, input);

    bool submit = (input->down & PD_BTN_A) != 0;
    if (input->touch_just_pressed) {
        int hit = pd_ui_hit_test_board(input->touch_x, input->touch_y);
        if (hit >= 0 && hit < PD_BOARD_SIZE) {
            app->selected_cell = hit;
            submit = true;
        }
    }

    if (submit && !app->daily_solved) {
        pd_app_submit_daily_cell(app, app->selected_cell);
    }
}

static void pd_app_advance_win2_opponent(pd_app_t *app, int first_cell, int first_give) {
    app->win2_puzzle.board[first_cell] = app->win2_puzzle.piece_in_hand;
    int new_avail[PD_PIECE_COUNT];
    int new_count = 0;
    for (int i = 0; i < app->win2_puzzle.available_count; i++) {
        if (app->win2_puzzle.available[i] != first_give) {
            new_avail[new_count++] = app->win2_puzzle.available[i];
        }
    }
    for (int i = 0; i < new_count; i++) {
        app->win2_puzzle.available[i] = new_avail[i];
    }
    app->win2_puzzle.available_count = new_count;

    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(app->selected_variant, &line_count);

    int opp_cell = -1;
    int opp_give = (new_count > 0) ? new_avail[0] : 0;
    for (int c = 0; c < PD_BOARD_SIZE; c++) {
        if (app->win2_puzzle.board[c] != PD_NONE) {
            continue;
        }
        if (pd_would_win(app->win2_puzzle.board, c, first_give, lines, line_count)) {
            continue;
        }
        int temp_board[PD_BOARD_SIZE];
        memcpy(temp_board, app->win2_puzzle.board, sizeof(temp_board));
        temp_board[c] = first_give;
        for (int g = 0; g < new_count; g++) {
            int cand_give = new_avail[g];
            for (int wc = 0; wc < PD_BOARD_SIZE; wc++) {
                if (temp_board[wc] == PD_NONE &&
                    pd_would_win(temp_board, wc, cand_give, lines, line_count)) {
                    opp_cell = c;
                    opp_give = cand_give;
                    break;
                }
            }
            if (opp_cell != -1) {
                break;
            }
        }
        if (opp_cell != -1) {
            break;
        }
    }

    if (opp_cell != -1) {
        app->win2_puzzle.board[opp_cell] = first_give;
    }
    app->win2_puzzle.piece_in_hand = opp_give;
    app->win2_step = 1;
    app->pending_cell = PD_NONE;
    app->cursor_on_tray = false;
}

static void pd_app_update_trainers(pd_app_t *app, const pd_input_t *input) {
    if (input->down & PD_BTN_B) {
        if (app->trainer_kind == PD_TRAINER_WIN_IN_TWO &&
            app->win2_step == 0 && app->pending_cell != PD_NONE) {
            app->pending_cell = PD_NONE;
            app->cursor_on_tray = false;
            return;
        }
        app->rush_active = false;
        app->screen = PD_SCREEN_TITLE;
        return;
    }

    if (input->down & PD_BTN_L) {
        pd_trainer_kind_t prev_k = (pd_trainer_kind_t)(((int)app->trainer_kind + 4) % 5);
        app->rush_active = false;
        pd_app_load_trainer(app, prev_k, app->puzzle_seed + 1u);
        return;
    }
    if (input->down & PD_BTN_R) {
        pd_trainer_kind_t next_k = (pd_trainer_kind_t)(((int)app->trainer_kind + 1) % 5);
        app->rush_active = false;
        pd_app_load_trainer(app, next_k, app->puzzle_seed + 1u);
        return;
    }
    if (input->down & PD_BTN_X) {
        if (app->trainer_kind == PD_TRAINER_RUSH) {
            app->rush_active = false;
        }
        pd_app_load_trainer(app, app->trainer_kind, app->puzzle_seed + 1u);
        return;
    }

    if (input->down & PD_BTN_Y) {
        app->trainer_revealed = true;
        if (app->trainer_kind == PD_TRAINER_PRACTICE || app->trainer_kind == PD_TRAINER_RUSH) {
            if (app->daily_puzzle.solution_count > 0) {
                app->selected_cell = app->daily_puzzle.solution[0];
            }
            snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                     "Hint: Winning cell is %d", app->selected_cell);
        } else if (app->trainer_kind == PD_TRAINER_WIN_IN_TWO) {
            if (app->win2_puzzle.first_moves_count > 0) {
                app->selected_cell = app->win2_puzzle.first_moves[0].cell;
                app->selected_piece = app->win2_puzzle.first_moves[0].give;
            }
            snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                     "Hint: Cell %d, Give %d", app->selected_cell, app->selected_piece);
        } else if (app->trainer_kind == PD_TRAINER_SAFE_GIVE) {
            if (app->safe_give_puzzle.safe_count > 0) {
                app->selected_piece = app->safe_give_puzzle.safe[0];
            }
            snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                     "Hint: Safe piece is %d", app->selected_piece);
        } else if (app->trainer_kind == PD_TRAINER_HOT_SQUARES) {
            uint16_t mask = 0;
            for (int i = 0; i < app->threat_drill_puzzle.hot_count; i++) {
                int c = app->threat_drill_puzzle.hot[i];
                if (c >= 0 && c < PD_BOARD_SIZE) {
                    mask |= (uint16_t)(1u << c);
                }
            }
            app->threat_found_mask = mask;
            app->trainer_solved = true;
            snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                     "Revealed all %d hot squares", app->threat_drill_puzzle.hot_count);
        }
        snprintf(app->status_banner, sizeof(app->status_banner), "%s", app->trainer_feedback);
        return;
    }

    int hit_board = -1;
    int hit_tray = -1;
    if (input->touch_just_pressed) {
        hit_board = pd_ui_hit_test_board(input->touch_x, input->touch_y);
        hit_tray = pd_ui_hit_test_tray(input->touch_x, input->touch_y);
    }

    switch (app->trainer_kind) {
    case PD_TRAINER_PRACTICE: {
        pd_app_dpad_move_grid(&app->selected_cell, input);
        bool submit = (input->down & PD_BTN_A) != 0;
        if (hit_board >= 0) {
            app->selected_cell = hit_board;
            submit = true;
        }
        if (submit) {
            bool ok = false;
            for (int i = 0; i < app->daily_puzzle.solution_count; i++) {
                if (app->daily_puzzle.solution[i] == app->selected_cell) {
                    ok = true;
                    break;
                }
            }
            if (ok) {
                app->trainer_solved = true;
                app->daily_puzzle.board[app->selected_cell] = app->daily_puzzle.piece_in_hand;
                snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                         "Solved! Cell %d wins immediately.", app->selected_cell);
            } else {
                snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                         "Cell %d does not win. Try again!", app->selected_cell);
            }
        }
        break;
    }

    case PD_TRAINER_WIN_IN_TWO: {
        if (app->win2_step == 0) {
            if (app->pending_cell == PD_NONE) {
                pd_app_dpad_move_grid(&app->selected_cell, input);
                bool pick_cell = (input->down & PD_BTN_A) != 0;
                if (hit_board >= 0) {
                    app->selected_cell = hit_board;
                    pick_cell = true;
                }
                if (pick_cell && app->win2_puzzle.board[app->selected_cell] == PD_NONE) {
                    app->pending_cell = app->selected_cell;
                    app->cursor_on_tray = true;
                    snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                             "Cell %d chosen. Now pick piece to give.", app->pending_cell);
                }
            } else {
                pd_app_dpad_move_grid(&app->selected_piece, input);
                bool pick_give = (input->down & PD_BTN_A) != 0;
                if (hit_tray >= 0) {
                    app->selected_piece = hit_tray;
                    pick_give = true;
                }
                if (pick_give) {
                    bool valid_first = false;
                    for (int i = 0; i < app->win2_puzzle.first_moves_count; i++) {
                        if (app->win2_puzzle.first_moves[i].cell == app->pending_cell &&
                            app->win2_puzzle.first_moves[i].give == app->selected_piece) {
                            valid_first = true;
                            break;
                        }
                    }
                    if (valid_first) {
                        pd_app_advance_win2_opponent(app, app->pending_cell, app->selected_piece);
                        snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                                 "Good! Opponent replied. Now place piece %d to win!",
                                 app->win2_puzzle.piece_in_hand);
                    } else {
                        app->pending_cell = PD_NONE;
                        app->cursor_on_tray = false;
                        snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                                 "Not a forced win in two. Try another first move!");
                    }
                }
            }
        } else {
            pd_app_dpad_move_grid(&app->selected_cell, input);
            bool submit_win = (input->down & PD_BTN_A) != 0;
            if (hit_board >= 0) {
                app->selected_cell = hit_board;
                submit_win = true;
            }
            if (submit_win && app->win2_puzzle.board[app->selected_cell] == PD_NONE) {
                int line_count = 0;
                const pd_line_t *lines = pd_lines_for_variant(app->selected_variant, &line_count);
                if (pd_would_win(app->win2_puzzle.board, app->selected_cell,
                                 app->win2_puzzle.piece_in_hand, lines, line_count)) {
                    app->win2_puzzle.board[app->selected_cell] = app->win2_puzzle.piece_in_hand;
                    app->trainer_solved = true;
                    snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                             "Solved! Win in Two complete!");
                } else {
                    snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                             "That cell does not complete the winning line.");
                }
            }
        }
        break;
    }

    case PD_TRAINER_SAFE_GIVE: {
        pd_app_dpad_move_grid(&app->selected_piece, input);
        bool submit = (input->down & PD_BTN_A) != 0;
        if (hit_tray >= 0) {
            app->selected_piece = hit_tray;
            submit = true;
        }
        if (submit) {
            bool is_safe = false;
            for (int i = 0; i < app->safe_give_puzzle.safe_count; i++) {
                if (app->safe_give_puzzle.safe[i] == app->selected_piece) {
                    is_safe = true;
                    break;
                }
            }
            if (is_safe) {
                app->trainer_solved = true;
                snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                         "Solved! Piece %d is safe to give.", app->selected_piece);
            } else {
                snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                         "Blunder! Piece %d allows an immediate win.", app->selected_piece);
            }
        }
        break;
    }

    case PD_TRAINER_HOT_SQUARES: {
        pd_app_dpad_move_grid(&app->selected_cell, input);
        bool toggle = (input->down & PD_BTN_A) != 0;
        if (hit_board >= 0) {
            app->selected_cell = hit_board;
            toggle = true;
        }
        if (toggle) {
            app->threat_found_mask ^= (uint16_t)(1u << app->selected_cell);
            uint16_t expected_mask = 0;
            for (int i = 0; i < app->threat_drill_puzzle.hot_count; i++) {
                int c = app->threat_drill_puzzle.hot[i];
                if (c >= 0 && c < PD_BOARD_SIZE) {
                    expected_mask |= (uint16_t)(1u << c);
                }
            }
            if (app->threat_found_mask == expected_mask) {
                app->trainer_solved = true;
                snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                         "Solved! All %d hot squares marked.", app->threat_drill_puzzle.hot_count);
            } else {
                app->trainer_solved = false;
                snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                         "Toggled cell %d. Find all %d threat cells.",
                         app->selected_cell, app->threat_drill_puzzle.hot_count);
            }
        }
        break;
    }

    case PD_TRAINER_RUSH: {
        uint64_t now_ms = pd_platform_ticks_ms();
        if (app->rush_active && now_ms >= app->rush_end_ms) {
            app->rush_active = false;
            snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                     "Time's Up! Rush Score: %d", app->rush_score);
            break;
        }
        pd_app_dpad_move_grid(&app->selected_cell, input);
        bool submit = (input->down & PD_BTN_A) != 0;
        if (hit_board >= 0) {
            app->selected_cell = hit_board;
            submit = true;
        }
        if (submit && app->rush_active) {
            bool ok = false;
            for (int i = 0; i < app->daily_puzzle.solution_count; i++) {
                if (app->daily_puzzle.solution[i] == app->selected_cell) {
                    ok = true;
                    break;
                }
            }
            if (ok) {
                app->rush_score += 1;
                app->rush_streak += 1;
                if (app->rush_score > app->save.rush_best_score) {
                    app->save.rush_best_score = app->rush_score;
                }
                if (app->rush_streak > app->save.rush_best_streak) {
                    app->save.rush_best_streak = app->rush_streak;
                }
                app->trainer_solved = true;
                app->puzzle_seed += 1u;
                pd_generate_daily_puzzle(app->puzzle_seed, app->selected_variant, &app->daily_puzzle);
                if (app->save_path[0] != '\0') {
                    pd_storage_save(&app->save, app->save_path);
                }
                snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                         "Hit! Score: %d Streak: %d", app->rush_score, app->rush_streak);
            } else {
                app->rush_streak = 0;
                app->rush_strikes += 1;
                if (app->rush_strikes >= 3) {
                    app->rush_active = false;
                    snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                             "3 Strikes! Final Score: %d", app->rush_score);
                } else {
                    snprintf(app->trainer_feedback, sizeof(app->trainer_feedback),
                             "Strike %d/3! Keep going!", app->rush_strikes);
                }
            }
        }
        break;
    }
    }

    snprintf(app->status_banner, sizeof(app->status_banner), "%s", app->trainer_feedback);
}

static void pd_app_update_tutorial(pd_app_t *app, const pd_input_t *input) {
    if (input->down & PD_BTN_B) {
        app->screen = PD_SCREEN_TITLE;
        return;
    }

    int hit_board = -1;
    int hit_tray = -1;
    if (input->touch_just_pressed) {
        hit_board = pd_ui_hit_test_board(input->touch_x, input->touch_y);
        hit_tray = pd_ui_hit_test_tray(input->touch_x, input->touch_y);
    }

    if (app->tutorial_step == 0) {
        pd_app_dpad_move_grid(&app->selected_piece, input);
        if ((input->down & PD_BTN_A) || input->touch_just_pressed) {
            app->tutorial_step = 1;
            app->tutorial_step_done = false;
            app->cursor_on_tray = false;
            app->selected_cell = 0;
            snprintf(app->status_banner, sizeof(app->status_banner),
                     "Tutorial 2/3: Complete the Solid Line at cell %d",
                     app->tutorial_data.find_win_cell);
        }
    } else if (app->tutorial_step == 1) {
        pd_app_dpad_move_grid(&app->selected_cell, input);
        bool submit = (input->down & PD_BTN_A) != 0;
        if (hit_board >= 0) {
            app->selected_cell = hit_board;
            submit = true;
        }
        if (submit) {
            if (app->selected_cell == app->tutorial_data.find_win_cell) {
                app->tutorial_step = 2;
                app->tutorial_step_done = false;
                app->cursor_on_tray = true;
                app->selected_piece = 0;
                snprintf(app->status_banner, sizeof(app->status_banner),
                         "Tutorial 3/3: Pick a Safe Gift (%d or %d)",
                         app->tutorial_data.safe_give_safe[0],
                         app->tutorial_data.safe_give_safe[1]);
            } else {
                snprintf(app->status_banner, sizeof(app->status_banner),
                         "Try cell %d to complete the solid row!",
                         app->tutorial_data.find_win_cell);
            }
        }
    } else if (app->tutorial_step == 2) {
        pd_app_dpad_move_grid(&app->selected_piece, input);
        bool submit = (input->down & PD_BTN_A) != 0;
        if (hit_tray >= 0) {
            app->selected_piece = hit_tray;
            submit = true;
        }
        if (submit) {
            if (app->selected_piece == app->tutorial_data.safe_give_safe[0] ||
                app->selected_piece == app->tutorial_data.safe_give_safe[1]) {
                app->tutorial_step = 3;
                app->tutorial_step_done = true;
                app->save.tutorial_completed = 1;
                if (app->save_path[0] != '\0') {
                    pd_storage_save(&app->save, app->save_path);
                }
                snprintf(app->status_banner, sizeof(app->status_banner),
                         "Tutorial Complete! Press A or B to return.");
            } else {
                snprintf(app->status_banner, sizeof(app->status_banner),
                         "Unsafe! Pick piece %d or %d instead.",
                         app->tutorial_data.safe_give_safe[0],
                         app->tutorial_data.safe_give_safe[1]);
            }
        }
    } else {
        if ((input->down & PD_BTN_A) || input->touch_just_pressed) {
            app->screen = PD_SCREEN_TITLE;
        }
    }
}

static void pd_app_update_replay(pd_app_t *app, const pd_input_t *input) {
    if (input->down & PD_BTN_B) {
        app->screen = PD_SCREEN_TITLE;
        return;
    }
    if (input->down & (PD_BTN_L | PD_BTN_DLEFT)) {
        pd_app_step_replay(app, -1);
    }
    if (input->down & (PD_BTN_R | PD_BTN_DRIGHT)) {
        pd_app_step_replay(app, 1);
    }
    if ((input->down & PD_BTN_DUP) && app->save.replay_count > 0) {
        int next_idx = (app->replay_game_index + app->save.replay_count - 1) % app->save.replay_count;
        pd_app_open_replay(app, next_idx);
    }
    if ((input->down & PD_BTN_DDOWN) && app->save.replay_count > 0) {
        int next_idx = (app->replay_game_index + 1) % app->save.replay_count;
        pd_app_open_replay(app, next_idx);
    }
}

static void pd_app_update_multiplayer(pd_app_t *app, const pd_input_t *input) {
    if (app->net.mode != PD_NET_MODE_NONE && app->net.state != PD_NET_STATE_IDLE) {
        pd_net_poll(&app->net);
        if (app->net.state == PD_NET_STATE_ACTIVE) {
            pd_game_mode_t gmode = PD_GAME_MODE_P2P_LAN;
            if (app->net.mode == PD_NET_MODE_UDS) {
                gmode = PD_GAME_MODE_P2P_UDS;
            } else if (app->net.mode == PD_NET_MODE_WEB) {
                gmode = PD_GAME_MODE_WEB_PVP;
            }
            pd_app_start_match(app, gmode, app->selected_difficulty, app->net.variant);
            return;
        }
    }

    if (input->down & PD_BTN_B) {
        pd_net_close(&app->net);
        app->screen = PD_SCREEN_TITLE;
        return;
    }

    if (input->down & PD_BTN_DUP) {
        app->mp_menu_index = (app->mp_menu_index + 5) % 6;
    }
    if (input->down & PD_BTN_DDOWN) {
        app->mp_menu_index = (app->mp_menu_index + 1) % 6;
    }

    if (input->down & PD_BTN_X) {
        if (pd_platform_prompt_text("Server Host / IP", app->save.server_host,
                                    sizeof(app->save.server_host), false)) {
            pd_net_web_configure(&app->net, app->save.server_host,
                                 app->save.server_port, app->save.session_cookie);
            if (app->save_path[0] != '\0') {
                pd_storage_save(&app->save, app->save_path);
            }
            snprintf(app->status_banner, sizeof(app->status_banner),
                     "Server set to %.64s:%u", app->save.server_host, (unsigned)app->save.server_port);
        }
        return;
    }

    if (input->down & PD_BTN_Y) {
        char code[64];
        memset(code, 0, sizeof(code));
        if (pd_platform_prompt_text("Enter Invite Code", code, sizeof(code), false) && code[0] != '\0') {
            pd_net_web_configure(&app->net, app->save.server_host,
                                 app->save.server_port, app->save.session_cookie);
            if (pd_net_web_join_game(&app->net, code)) {
                pd_net_web_connect_socket(&app->net, app->net.game_id);
            }
        }
        return;
    }

    if (input->down & PD_BTN_A) {
        switch (app->mp_menu_index) {
        case 0:
            pd_net_uds_host(&app->net, app->selected_variant);
            snprintf(app->status_banner, sizeof(app->status_banner), "Hosting 3DS Local Wireless...");
            break;
        case 1:
            pd_net_uds_scan(&app->net);
            if (app->net.beacon_count > 0) {
                pd_net_uds_connect(&app->net, 0);
            }
            snprintf(app->status_banner, sizeof(app->status_banner),
                     "Scanning 3DS Wireless (%d found)", app->net.beacon_count);
            break;
        case 2:
            pd_net_p2p_host(&app->net, PD_P2P_DEFAULT_UDP_PORT, PD_P2P_DEFAULT_TCP_PORT,
                            app->selected_variant);
            snprintf(app->status_banner, sizeof(app->status_banner),
                     "Hosting LAN P2P on TCP %u", PD_P2P_DEFAULT_TCP_PORT);
            break;
        case 3: {
            char ip_buf[64];
            snprintf(ip_buf, sizeof(ip_buf), "%.63s", app->save.server_host);
            if (pd_platform_prompt_text("Host LAN IP", ip_buf, sizeof(ip_buf), false) && ip_buf[0] != '\0') {
                pd_net_p2p_connect(&app->net, ip_buf, PD_P2P_DEFAULT_TCP_PORT);
            } else {
                pd_net_p2p_start_scan(&app->net, PD_P2P_DEFAULT_UDP_PORT);
                pd_net_poll(&app->net);
                if (app->net.beacon_count > 0) {
                    pd_net_p2p_connect(&app->net, app->net.beacons[0].ip_addr,
                                       app->net.beacons[0].tcp_port);
                }
            }
            snprintf(app->status_banner, sizeof(app->status_banner), "Connecting LAN P2P...");
            break;
        }
        case 4:
            pd_net_web_configure(&app->net, app->save.server_host,
                                 app->save.server_port, app->save.session_cookie);
            if (pd_net_web_create_game(&app->net, "pvp", NULL)) {
                pd_net_web_connect_socket(&app->net, app->net.game_id);
                snprintf(app->status_banner, sizeof(app->status_banner),
                         "Web Room Created (Code: %.15s)", app->net.invite_code);
            } else {
                snprintf(app->status_banner, sizeof(app->status_banner),
                         "Web Room Failed: %.64s", app->net.error_msg);
            }
            break;
        case 5:
        default: {
            char invite_buf[64];
            memset(invite_buf, 0, sizeof(invite_buf));
            if (!pd_platform_prompt_text("Invite Code or Game ID", invite_buf, sizeof(invite_buf), false)) {
                snprintf(invite_buf, sizeof(invite_buf), "%s", app->net.invite_code);
            }
            pd_net_web_configure(&app->net, app->save.server_host,
                                 app->save.server_port, app->save.session_cookie);
            if (invite_buf[0] != '\0' && pd_net_web_join_game(&app->net, invite_buf)) {
                pd_net_web_connect_socket(&app->net, app->net.game_id);
                snprintf(app->status_banner, sizeof(app->status_banner),
                         "Joined Web Game %s", app->net.game_id);
            } else if (pd_net_web_matchmake(&app->net)) {
                pd_net_web_connect_socket(&app->net, app->net.game_id);
                snprintf(app->status_banner, sizeof(app->status_banner),
                         "Matched Web Game %s", app->net.game_id);
            }
            break;
        }
        }

        if (app->net.state == PD_NET_STATE_ACTIVE) {
            pd_game_mode_t gmode = PD_GAME_MODE_P2P_LAN;
            if (app->net.mode == PD_NET_MODE_UDS) {
                gmode = PD_GAME_MODE_P2P_UDS;
            } else if (app->net.mode == PD_NET_MODE_WEB) {
                gmode = PD_GAME_MODE_WEB_PVP;
            }
            pd_app_start_match(app, gmode, app->selected_difficulty, app->net.variant);
        }
    }
}

static void pd_app_update_stats(pd_app_t *app, const pd_input_t *input) {
    if (input->down & PD_BTN_B) {
        app->screen = PD_SCREEN_TITLE;
        return;
    }
    if (input->down & PD_BTN_X) {
        app->save.colorblind_mode = !app->save.colorblind_mode;
        if (app->save_path[0] != '\0') {
            pd_storage_save(&app->save, app->save_path);
        }
    }
    if (input->down & PD_BTN_Y) {
        app->save.show_threat_hints = !app->save.show_threat_hints;
        if (app->save_path[0] != '\0') {
            pd_storage_save(&app->save, app->save_path);
        }
    }
    if ((input->down & PD_BTN_A) && app->save.replay_count > 0) {
        int latest = (app->save.replay_head + PD_MAX_REPLAYS - 1) % PD_MAX_REPLAYS;
        if (latest >= app->save.replay_count) {
            latest = app->save.replay_count - 1;
        }
        pd_app_open_replay(app, latest);
    }
}

void pd_app_update(pd_app_t *app, const pd_input_t *input) {
    if (!app || !input) {
        return;
    }

    switch (app->screen) {
    case PD_SCREEN_TITLE:
        pd_app_update_title(app, input);
        break;
    case PD_SCREEN_MATCH:
        pd_app_update_match(app, input);
        break;
    case PD_SCREEN_DAILY:
        pd_app_update_daily(app, input);
        break;
    case PD_SCREEN_TRAINERS:
        pd_app_update_trainers(app, input);
        break;
    case PD_SCREEN_TUTORIAL:
        pd_app_update_tutorial(app, input);
        break;
    case PD_SCREEN_REPLAY:
        pd_app_update_replay(app, input);
        break;
    case PD_SCREEN_MULTIPLAYER:
        pd_app_update_multiplayer(app, input);
        break;
    case PD_SCREEN_STATS:
        pd_app_update_stats(app, input);
        break;
    }
}
