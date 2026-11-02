#include "pd_ui.h"

#include <stdio.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) bool pd_net_send_move(pd_net_ctx_t *ctx, pd_move_t move) {
    (void)ctx;
    (void)move;
    return true;
}

__attribute__((weak)) bool pd_net_send_preview(pd_net_ctx_t *ctx, int cell) {
    if (ctx != NULL) {
        ctx->preview_cell = cell;
    }
    return true;
}
#endif

#define PD_BOT_BOARD_X 12
#define PD_BOT_BOARD_Y 36
#define PD_BOT_BOARD_STRIDE 36
#define PD_BOT_BOARD_CELL_SIZE 34

#define PD_BOT_TRAY_X 172
#define PD_BOT_TRAY_Y 36
#define PD_BOT_TRAY_STRIDE 34
#define PD_BOT_TRAY_CELL_SIZE 32

#define PD_BOT_BAR_X 12
#define PD_BOT_BAR_Y 194
#define PD_BOT_BAR_W 296
#define PD_BOT_BAR_H 36
#define PD_BOT_BAR_BTN_STRIDE 74

int pd_ui_hit_test_board(int touch_x, int touch_y) {
    if (touch_x < PD_BOT_BOARD_X || touch_y < PD_BOT_BOARD_Y) {
        return PD_NONE;
    }
    int rel_x = touch_x - PD_BOT_BOARD_X;
    int rel_y = touch_y - PD_BOT_BOARD_Y;
    int col = rel_x / PD_BOT_BOARD_STRIDE;
    int row = rel_y / PD_BOT_BOARD_STRIDE;
    if (col < 0 || col >= PD_BOARD_DIM || row < 0 || row >= PD_BOARD_DIM) {
        return PD_NONE;
    }
    if ((rel_x % PD_BOT_BOARD_STRIDE) > PD_BOT_BOARD_CELL_SIZE ||
        (rel_y % PD_BOT_BOARD_STRIDE) > PD_BOT_BOARD_CELL_SIZE) {
        return PD_NONE;
    }
    return row * PD_BOARD_DIM + col;
}

int pd_ui_hit_test_tray(int touch_x, int touch_y) {
    if (touch_x < PD_BOT_TRAY_X || touch_y < PD_BOT_TRAY_Y) {
        return PD_NONE;
    }
    int rel_x = touch_x - PD_BOT_TRAY_X;
    int rel_y = touch_y - PD_BOT_TRAY_Y;
    int col = rel_x / PD_BOT_TRAY_STRIDE;
    int row = rel_y / PD_BOT_TRAY_STRIDE;
    if (col < 0 || col >= 4 || row < 0 || row >= 4) {
        return PD_NONE;
    }
    if ((rel_x % PD_BOT_TRAY_STRIDE) > PD_BOT_TRAY_CELL_SIZE ||
        (rel_y % PD_BOT_TRAY_STRIDE) > PD_BOT_TRAY_CELL_SIZE) {
        return PD_NONE;
    }
    return row * 4 + col;
}

int pd_ui_hit_test_bottom_bar(int touch_x, int touch_y) {
    if (touch_y < PD_BOT_BAR_Y || touch_y >= PD_BOT_BAR_Y + PD_BOT_BAR_H) {
        return PD_NONE;
    }
    if (touch_x < PD_BOT_BAR_X || touch_x >= PD_BOT_BAR_X + PD_BOT_BAR_W) {
        return PD_NONE;
    }
    int idx = (touch_x - PD_BOT_BAR_X) / PD_BOT_BAR_BTN_STRIDE;
    if (idx < 0 || idx >= 4) {
        return PD_NONE;
    }
    return idx;
}

static bool is_piece_available(const pd_game_state_t *game, int piece) {
    if (game == NULL || !pd_is_piece_id(piece)) {
        return false;
    }
    for (int i = 0; i < game->available_count; i++) {
        if (game->available[i] == piece) {
            return true;
        }
    }
    return false;
}

static int first_available_piece(const pd_game_state_t *game) {
    if (game == NULL || game->available_count <= 0) {
        return PD_NONE;
    }
    return game->available[0];
}

static void move_grid_cursor(int *cursor, uint32_t down) {
    if (cursor == NULL) {
        return;
    }
    int c = *cursor;
    if (c < 0 || c >= 16) {
        c = 0;
    }
    int row = c / 4;
    int col = c % 4;
    if (down & PD_BTN_DUP) {
        row = (row + 3) % 4;
    }
    if (down & PD_BTN_DDOWN) {
        row = (row + 1) % 4;
    }
    if (down & PD_BTN_DLEFT) {
        col = (col + 3) % 4;
    }
    if (down & PD_BTN_DRIGHT) {
        col = (col + 1) % 4;
    }
    *cursor = row * 4 + col;
}

static bool commit_match_move(pd_app_t *app, pd_move_t move) {
    if (app == NULL) {
        return false;
    }
    int ply = app->game.move_count;
    int seat = app->game.current;
    int placed_piece = (move.kind == PD_MOVE_PLACE) ? app->game.piece_in_hand : PD_NONE;

    pd_game_state_t next_state;
    if (!pd_apply_move(&app->game, move, &next_state)) {
        snprintf(app->status_banner, sizeof(app->status_banner), "ILLEGAL MOVE");
        return false;
    }

    app->current_record.valid = true;
    app->current_record.variant = app->game.variant;
    app->current_record.mode = app->selected_mode;
    app->current_record.ai_level = app->selected_difficulty;
    if (ply >= 0 && ply < PD_MAX_REPLAY_MOVES) {
        app->current_record.moves[ply].ply = (uint8_t)ply;
        app->current_record.moves[ply].seat = (uint8_t)seat;
        app->current_record.moves[ply].cell = (int8_t)((move.kind == PD_MOVE_PLACE) ? move.cell : PD_NONE);
        app->current_record.moves[ply].placed_piece = (int8_t)placed_piece;
        app->current_record.moves[ply].given_piece =
            (int8_t)((next_state.status == PD_STATUS_PLAYING) ? move.give : PD_NONE);
    }
    app->current_record.move_count = (uint8_t)next_state.move_count;
    if (next_state.status == PD_STATUS_WIN) {
        app->current_record.winner = (int8_t)next_state.winner;
    } else if (next_state.status == PD_STATUS_DRAW) {
        app->current_record.winner = (int8_t)PD_NONE;
    }

    app->game = next_state;
    app->pending_cell = PD_NONE;
    app->cursor_on_tray = (app->game.piece_in_hand == PD_NONE);
    if (!is_piece_available(&app->game, app->selected_piece)) {
        int next_avail = first_available_piece(&app->game);
        if (next_avail != PD_NONE) {
            app->selected_piece = next_avail;
        }
    }

    if (app->net.mode != PD_NET_MODE_NONE) {
        pd_net_send_move(&app->net, move);
    }

    if (app->game.status == PD_STATUS_WIN) {
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "PLAYER %d WINS!", app->game.winner + 1);
    } else if (app->game.status == PD_STATUS_DRAW) {
        snprintf(app->status_banner, sizeof(app->status_banner), "MATCH DRAW!");
    } else {
        snprintf(app->status_banner, sizeof(app->status_banner),
                 "P%d TURN - PLACE PIECE #%d", app->game.current + 1, app->game.piece_in_hand);
    }
    return true;
}

static bool stage_or_finish_board_cell(pd_app_t *app, int cell) {
    if (app == NULL || !pd_is_cell_index(cell)) {
        return false;
    }
    if (app->game.piece_in_hand == PD_NONE) {
        app->cursor_on_tray = true;
        snprintf(app->status_banner, sizeof(app->status_banner), "SELECT OPENING PIECE IN TRAY");
        return false;
    }
    if (app->game.board[cell] != PD_NONE) {
        snprintf(app->status_banner, sizeof(app->status_banner), "CELL %d IS OCCUPIED", cell);
        return false;
    }

    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(app->game.variant, &line_count);
    bool immediate_line = pd_would_win(app->game.board, cell, app->game.piece_in_hand, lines, line_count);
    if (immediate_line || app->game.available_count == 0) {
        pd_move_t terminal_move = { PD_MOVE_PLACE, cell, PD_NONE };
        return commit_match_move(app, terminal_move);
    }

    app->selected_cell = cell;
    app->pending_cell = cell;
    app->cursor_on_tray = true;
    if (!is_piece_available(&app->game, app->selected_piece)) {
        int next_avail = first_available_piece(&app->game);
        if (next_avail != PD_NONE) {
            app->selected_piece = next_avail;
        }
    }
    if (app->net.mode != PD_NET_MODE_NONE) {
        pd_net_send_preview(&app->net, cell);
    }
    snprintf(app->status_banner, sizeof(app->status_banner),
             "CELL %d STAGED - PICK PIECE TO GIVE", cell);
    return true;
}

static bool submit_tray_piece(pd_app_t *app, int piece) {
    if (app == NULL || !is_piece_available(&app->game, piece)) {
        if (app != NULL) {
            snprintf(app->status_banner, sizeof(app->status_banner), "PIECE unavailable");
        }
        return false;
    }
    app->selected_piece = piece;
    if (app->game.piece_in_hand == PD_NONE) {
        pd_move_t open_move = { PD_MOVE_OPEN, PD_NONE, piece };
        return commit_match_move(app, open_move);
    }
    if (app->pending_cell != PD_NONE) {
        pd_move_t place_move = { PD_MOVE_PLACE, app->pending_cell, piece };
        return commit_match_move(app, place_move);
    }
    snprintf(app->status_banner, sizeof(app->status_banner), "SELECT A BOARD CELL FIRST");
    return false;
}

void pd_ui_handle_match_input(pd_app_t *app, const pd_input_t *input) {
    if (app == NULL || input == NULL) {
        return;
    }

    int hit_bar = PD_NONE;
    int hit_cell = PD_NONE;
    int hit_tray = PD_NONE;
    if (input->touch_just_pressed) {
        hit_bar = pd_ui_hit_test_bottom_bar(input->touch_x, input->touch_y);
        hit_cell = pd_ui_hit_test_board(input->touch_x, input->touch_y);
        hit_tray = pd_ui_hit_test_tray(input->touch_x, input->touch_y);
    }

    bool btn_a = ((input->down & PD_BTN_A) != 0) || (hit_bar == 0);
    bool btn_x = ((input->down & PD_BTN_X) != 0) || (hit_bar == 1);
    bool btn_y = ((input->down & PD_BTN_Y) != 0) || (hit_bar == 2);
    bool btn_b = ((input->down & PD_BTN_B) != 0) || (hit_bar == 3);

    if (app->game.status != PD_STATUS_PLAYING) {
        if (btn_b) {
            app->screen = PD_SCREEN_TITLE;
        }
        return;
    }

    if ((app->selected_mode == PD_GAME_MODE_P2P_UDS ||
         app->selected_mode == PD_GAME_MODE_P2P_LAN ||
         app->selected_mode == PD_GAME_MODE_WEB_PVP) &&
        app->net.state == PD_NET_STATE_ACTIVE &&
        app->game.current != app->net.my_seat) {
        if (btn_b) {
            app->screen = PD_SCREEN_TITLE;
        }
        return;
    }

    if (app->game.piece_in_hand == PD_NONE) {
        app->cursor_on_tray = true;
    }

    if ((input->down & (PD_BTN_L | PD_BTN_R)) != 0) {
        if (app->game.piece_in_hand != PD_NONE) {
            app->cursor_on_tray = !app->cursor_on_tray;
        }
    }

    if (btn_x) {
        if (hit_bar == 1) {
            app->save.show_threat_hints = !app->save.show_threat_hints;
        } else if (app->game.piece_in_hand != PD_NONE) {
            app->cursor_on_tray = !app->cursor_on_tray;
        }
    }

    if (btn_y) {
        app->save.colorblind_mode = !app->save.colorblind_mode;
    }

    if (btn_b) {
        if (app->pending_cell != PD_NONE) {
            app->pending_cell = PD_NONE;
            app->cursor_on_tray = false;
            if (app->net.mode != PD_NET_MODE_NONE) {
                pd_net_send_preview(&app->net, PD_NONE);
            }
            snprintf(app->status_banner, sizeof(app->status_banner), "PLACEMENT CANCELED");
        } else {
            app->screen = PD_SCREEN_TITLE;
        }
        return;
    }

    if (input->down & (PD_BTN_DUP | PD_BTN_DDOWN | PD_BTN_DLEFT | PD_BTN_DRIGHT)) {
        if (app->cursor_on_tray) {
            move_grid_cursor(&app->selected_piece, input->down);
        } else {
            move_grid_cursor(&app->selected_cell, input->down);
        }
    }

    if (hit_cell != PD_NONE) {
        stage_or_finish_board_cell(app, hit_cell);
        return;
    }

    if (hit_tray != PD_NONE) {
        submit_tray_piece(app, hit_tray);
        return;
    }

    if (btn_a) {
        if (app->game.piece_in_hand == PD_NONE || (app->cursor_on_tray && app->pending_cell != PD_NONE)) {
            submit_tray_piece(app, app->selected_piece);
        } else {
            stage_or_finish_board_cell(app, app->selected_cell);
        }
    }
}
