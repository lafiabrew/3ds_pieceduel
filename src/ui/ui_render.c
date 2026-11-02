#include "pd_ui.h"

#include <stdio.h>
#include <string.h>

/* Color palette (RGBA8) */
#define COL_BG_TOP       PD_RGBA8(15, 20, 34, 255)
#define COL_BG_BOT       PD_RGBA8(18, 24, 40, 255)
#define COL_PANEL        PD_RGBA8(26, 34, 56, 255)
#define COL_PANEL_ALT    PD_RGBA8(34, 45, 72, 255)
#define COL_BORDER       PD_RGBA8(72, 92, 136, 255)
#define COL_PEDESTAL     PD_RGBA8(10, 14, 24, 255)
#define COL_BOARD_BASE   PD_RGBA8(28, 38, 62, 255)
#define COL_CELL_BG      PD_RGBA8(38, 50, 80, 255)
#define COL_CELL_ALT     PD_RGBA8(44, 58, 92, 255)
#define COL_TEXT         PD_RGBA8(238, 242, 252, 255)
#define COL_TEXT_MUTED   PD_RGBA8(158, 172, 204, 255)
#define COL_ACCENT       PD_RGBA8(92, 184, 255, 255)
#define COL_WIN_GLOW     PD_RGBA8(72, 236, 148, 255)
#define COL_THREAT       PD_RGBA8(255, 184, 48, 255)
#define COL_DANGER       PD_RGBA8(255, 86, 92, 255)
#define COL_PENDING      PD_RGBA8(116, 224, 255, 255)
#define COL_PREVIEW      PD_RGBA8(196, 128, 255, 190)

/* Standard Piece Palette (Light = Warm Ivory/Gold, Dark = Indigo/Slate) */
#define COL_LIGHT_MAIN   PD_RGBA8(242, 214, 138, 255)
#define COL_LIGHT_SIDE   PD_RGBA8(184, 150, 78, 255)
#define COL_DARK_MAIN    PD_RGBA8(82, 114, 196, 255)
#define COL_DARK_SIDE    PD_RGBA8(46, 68, 128, 255)

/* Colorblind High-Contrast Palette (Light = Bright Orange, Dark = Electric Cyan) */
#define COL_CB_LIGHT_MAIN PD_RGBA8(255, 158, 44, 255)
#define COL_CB_LIGHT_SIDE PD_RGBA8(192, 104, 18, 255)
#define COL_CB_DARK_MAIN  PD_RGBA8(44, 218, 236, 255)
#define COL_CB_DARK_SIDE  PD_RGBA8(22, 142, 164, 255)

static bool board_has_any_piece(const int board[PD_BOARD_SIZE]) {
    for (int i = 0; i < PD_BOARD_SIZE; i++) {
        if (pd_is_piece_id(board[i])) {
            return true;
        }
    }
    return false;
}

static bool cell_in_line(int cell, const pd_line_t *line) {
    if (line == NULL) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (line->cells[i] == cell) {
            return true;
        }
    }
    return false;
}

static bool cell_in_list(int cell, const int *cells, int count) {
    for (int i = 0; i < count; i++) {
        if (cells[i] == cell) {
            return true;
        }
    }
    return false;
}

static bool is_piece_in_available(const pd_game_state_t *g, int piece) {
    for (int i = 0; i < g->available_count; i++) {
        if (g->available[i] == piece) {
            return true;
        }
    }
    return false;
}

static void resolve_active_view(const pd_app_t *app,
                                const int **out_board,
                                int *out_piece_in_hand,
                                pd_variant_t *out_variant,
                                bool *out_has_win,
                                pd_line_t *out_win_line) {
    const int *board = app->game.board;
    int hand = app->game.piece_in_hand;
    pd_variant_t variant = app->game.variant;
    bool has_win = app->game.has_winning_line;
    pd_line_t win_line = app->game.winning_line;

    if (app->screen == PD_SCREEN_REPLAY) {
        board = app->replay_state.board;
        hand = app->replay_state.piece_in_hand;
        variant = app->replay_state.variant;
        has_win = app->replay_state.has_winning_line;
        win_line = app->replay_state.winning_line;
    } else if (app->screen == PD_SCREEN_DAILY && !board_has_any_piece(app->game.board) && app->daily_puzzle.pieces > 0) {
        board = app->daily_puzzle.board;
        hand = app->daily_puzzle.piece_in_hand;
        variant = app->selected_variant;
    } else if (app->screen == PD_SCREEN_TRAINERS && !board_has_any_piece(app->game.board)) {
        variant = app->selected_variant;
        if (app->trainer_kind == PD_TRAINER_WIN_IN_TWO && app->win2_puzzle.pieces > 0) {
            board = app->win2_puzzle.board;
            hand = app->win2_puzzle.piece_in_hand;
        } else if (app->trainer_kind == PD_TRAINER_SAFE_GIVE && app->safe_give_puzzle.pieces > 0) {
            board = app->safe_give_puzzle.board;
            hand = (app->safe_give_puzzle.candidates_count > 0) ? app->safe_give_puzzle.candidates[0] : PD_NONE;
        } else if (app->trainer_kind == PD_TRAINER_HOT_SQUARES && app->threat_drill_puzzle.pieces > 0) {
            board = app->threat_drill_puzzle.board;
            hand = PD_NONE;
        }
    } else if (app->screen == PD_SCREEN_TUTORIAL && !board_has_any_piece(app->game.board)) {
        if (app->tutorial_step == 1) {
            board = app->tutorial_data.find_win_board;
            hand = app->tutorial_data.find_win_piece;
        } else if (app->tutorial_step == 2) {
            board = app->tutorial_data.safe_give_board;
            hand = app->tutorial_data.safe_give_candidates[0];
        }
    }

    *out_board = board;
    *out_piece_in_hand = hand;
    *out_variant = variant;
    *out_has_win = has_win;
    *out_win_line = win_line;
}

static void draw_piece_3d(int piece,
                          float cx,
                          float cy,
                          float base_radius,
                          float eye_sign,
                          float slider_3d,
                          bool colorblind) {
    pd_attributes_t attrs;
    if (!pd_decode_piece(piece, &attrs)) {
        return;
    }

    float depth_px = attrs.tall ? 6.0f : 2.0f;
    float dx = eye_sign * slider_3d * depth_px;
    float lift_y = attrs.tall ? -4.0f : 0.0f;

    uint32_t col_main = attrs.light
                            ? (colorblind ? COL_CB_LIGHT_MAIN : COL_LIGHT_MAIN)
                            : (colorblind ? COL_CB_DARK_MAIN : COL_DARK_MAIN);
    uint32_t col_side = attrs.light
                            ? (colorblind ? COL_CB_LIGHT_SIDE : COL_LIGHT_SIDE)
                            : (colorblind ? COL_CB_DARK_SIDE : COL_DARK_SIDE);

    float px = cx + dx;
    float py = cy + lift_y;
    float r = attrs.tall ? base_radius : (base_radius * 0.82f);

    /* 3D Extrusion / Pedestal column for Tall vs Short pieces */
    if (attrs.tall) {
        float col_h = 6.0f;
        if (attrs.square) {
            pd_draw_rect(cx + dx * 0.5f - r, cy - r + 2.0f, r * 2.0f, r * 2.0f + col_h * 0.5f, col_side);
        } else {
            pd_draw_circle(cx + dx * 0.5f, cy + 2.0f, r, col_side);
        }
    } else {
        if (attrs.square) {
            pd_draw_rect(px - r + 1.0f, py - r + 2.0f, r * 2.0f, r * 2.0f, col_side);
        } else {
            pd_draw_circle(px + 1.0f, py + 2.0f, r, col_side);
        }
    }

    /* Main piece body: Square vs Round */
    if (attrs.square) {
        pd_draw_rect(px - r, py - r, r * 2.0f, r * 2.0f, col_main);
        pd_draw_rect_outline(px - r, py - r, r * 2.0f, r * 2.0f, 1.5f, COL_PEDESTAL);
    } else {
        pd_draw_circle(px, py, r, col_main);
    }

    /* Solid vs Hollow top center */
    if (!attrs.solid) {
        float hole_r = r * 0.48f;
        if (attrs.square) {
            pd_draw_rect(px - hole_r, py - hole_r, hole_r * 2.0f, hole_r * 2.0f, COL_PEDESTAL);
        } else {
            pd_draw_circle(px, py, hole_r, COL_PEDESTAL);
        }
    } else {
        float cap_r = r * 0.25f;
        pd_draw_circle(px, py, cap_r, col_side);
    }

    /* Tall crown indicator bar & Colorblind stripe */
    if (attrs.tall) {
        pd_draw_rect(px - r * 0.55f, py - r + 2.0f, r * 1.1f, 2.0f, COL_TEXT);
    }
    if (colorblind && attrs.light) {
        pd_draw_rect(px - r * 0.6f, py + r - 4.0f, r * 1.2f, 2.0f, COL_PEDESTAL);
    }
}

static const char *screen_title_str(pd_screen_t screen) {
    switch (screen) {
    case PD_SCREEN_TITLE:       return "PIECE DUEL 3DS";
    case PD_SCREEN_MATCH:       return "MATCH ARENA";
    case PD_SCREEN_DAILY:       return "DAILY PUZZLE";
    case PD_SCREEN_TRAINERS:    return "TACTICS TRAINER";
    case PD_SCREEN_TUTORIAL:    return "INTERACTIVE TUTORIAL";
    case PD_SCREEN_REPLAY:      return "REPLAY & COACHING";
    case PD_SCREEN_MULTIPLAYER: return "MULTIPLAYER LOBBY";
    case PD_SCREEN_STATS:       return "PLAYER PROFILE";
    default:                    return "PIECE DUEL";
    }
}

static void render_top_stereoscopic(const pd_app_t *app, float eye_sign, float slider_3d) {
    const int *board = NULL;
    int piece_in_hand = PD_NONE;
    pd_variant_t variant = PD_VARIANT_STANDARD;
    bool has_win = false;
    pd_line_t win_line;
    resolve_active_view(app, &board, &piece_in_hand, &variant, &has_win, &win_line);

    int line_count = 0;
    const pd_line_t *lines = pd_lines_for_variant(variant, &line_count);
    int threats[PD_BOARD_SIZE];
    int threat_count = 0;
    if (app->save.show_threat_hints || app->screen == PD_SCREEN_TRAINERS) {
        threat_count = pd_threat_cells(board, lines, line_count, threats);
    }

    /* Stereoscopic 3D Board Pedestal on Left Side of 400x240 Top Screen */
    float ped_dx = eye_sign * slider_3d * (-2.0f);
    float grid_dx = eye_sign * slider_3d * 0.5f;
    float ui_dx = eye_sign * slider_3d * 2.5f;

    float board_x = 16.0f + grid_dx;
    float board_y = 24.0f;
    float cell_stride = 48.0f;
    float cell_size = 44.0f;

    pd_draw_rect(12.0f + ped_dx, 20.0f, 198.0f, 198.0f, COL_PEDESTAL);
    pd_draw_rect(14.0f + grid_dx, 22.0f, 194.0f, 194.0f, COL_BOARD_BASE);
    pd_draw_rect_outline(14.0f + grid_dx, 22.0f, 194.0f, 194.0f, 2.0f, COL_BORDER);

    for (int cell = 0; cell < PD_BOARD_SIZE; cell++) {
        int row = cell / 4;
        int col = cell % 4;
        float cx = board_x + (float)col * cell_stride;
        float cy = board_y + (float)row * cell_stride;

        uint32_t tile_col = ((row + col) & 1) ? COL_CELL_ALT : COL_CELL_BG;
        if (has_win && cell_in_line(cell, &win_line)) {
            tile_col = PD_RGBA8(32, 96, 68, 255);
        }
        pd_draw_rect(cx, cy, cell_size, cell_size, tile_col);

        /* Threat / Hot-square outline */
        if (app->save.show_threat_hints && cell_in_list(cell, threats, threat_count)) {
            pd_draw_rect_outline(cx + 1.0f, cy + 1.0f, cell_size - 2.0f, cell_size - 2.0f, 2.0f, COL_THREAT);
        }

        /* Winning line highlight */
        if (has_win && cell_in_line(cell, &win_line)) {
            pd_draw_rect_outline(cx, cy, cell_size, cell_size, 2.5f, COL_WIN_GLOW);
        }

        /* Selected / Pending cell cursor with 3D pop-out */
        if (cell == app->pending_cell) {
            float cur_dx = eye_sign * slider_3d * 3.5f;
            pd_draw_rect_outline(cx + cur_dx, cy, cell_size, cell_size, 2.5f, COL_PENDING);
        } else if (!app->cursor_on_tray && cell == app->selected_cell) {
            float cur_dx = eye_sign * slider_3d * 3.0f;
            pd_draw_rect_outline(cx + cur_dx, cy, cell_size, cell_size, 2.0f, COL_ACCENT);
        }

        /* Opponent live preview cell */
        if (app->net.preview_cell == cell && board[cell] == PD_NONE) {
            pd_draw_rect(cx + 8.0f, cy + 8.0f, cell_size - 16.0f, cell_size - 16.0f, COL_PREVIEW);
        }

        /* Placed 3D Piece */
        if (pd_is_piece_id(board[cell])) {
            draw_piece_3d(board[cell],
                          cx + cell_size * 0.5f,
                          cy + cell_size * 0.5f,
                          15.0f,
                          eye_sign,
                          slider_3d,
                          app->save.colorblind_mode);
        } else if (cell == app->pending_cell && pd_is_piece_id(piece_in_hand)) {
            /* Ghost preview of staged piece */
            draw_piece_3d(piece_in_hand,
                          cx + cell_size * 0.5f,
                          cy + cell_size * 0.5f,
                          13.0f,
                          eye_sign,
                          slider_3d,
                          app->save.colorblind_mode);
        }
    }

    /* Victory laser beam across winning line */
    if (has_win) {
        int c0 = win_line.cells[0];
        int c3 = win_line.cells[3];
        float x0 = board_x + (float)(c0 % 4) * cell_stride + cell_size * 0.5f + eye_sign * slider_3d * 7.0f;
        float y0 = board_y + (float)(c0 / 4) * cell_stride + cell_size * 0.5f;
        float x1 = board_x + (float)(c3 % 4) * cell_stride + cell_size * 0.5f + eye_sign * slider_3d * 7.0f;
        float y1 = board_y + (float)(c3 / 4) * cell_stride + cell_size * 0.5f;
        pd_draw_line(x0, y0, x1, y1, 3.0f, COL_WIN_GLOW);
    }

    /* Right-side Stereoscopic HUD Panel (x = 216..392) */
    float hud_x = 216.0f + ui_dx;
    pd_draw_rect(hud_x, 12.0f, 172.0f, 216.0f, COL_PANEL);
    pd_draw_rect_outline(hud_x, 12.0f, 172.0f, 216.0f, 1.5f, COL_BORDER);

    pd_draw_text(hud_x + 8.0f, 20.0f, 0.6f, COL_ACCENT, screen_title_str(app->screen));

    char var_line[48];
    snprintf(var_line, sizeof(var_line), "RULES: %s", pd_variant_to_str(variant));
    pd_draw_text(hud_x + 8.0f, 34.0f, 0.5f, COL_TEXT_MUTED, var_line);

    /* Piece-in-Hand 3D Showcase & Decoded 4-Trait Badges */
    pd_draw_rect(hud_x + 8.0f, 48.0f, 156.0f, 76.0f, COL_PANEL_ALT);
    pd_draw_text(hud_x + 14.0f, 54.0f, 0.5f, COL_TEXT, "PIECE IN HAND");

    if (pd_is_piece_id(piece_in_hand)) {
        draw_piece_3d(piece_in_hand,
                      hud_x + 38.0f,
                      92.0f,
                      16.0f,
                      eye_sign,
                      slider_3d,
                      app->save.colorblind_mode);

        pd_attributes_t attrs;
        pd_decode_piece(piece_in_hand, &attrs);
        char lbl[8];
        pd_piece_short_label(piece_in_hand, lbl);
        pd_draw_text(hud_x + 68.0f, 68.0f, 0.55f, COL_ACCENT, lbl);
        pd_draw_text(hud_x + 68.0f, 80.0f, 0.5f, COL_TEXT,
                     attrs.tall ? "TALL  (+3D)" : "SHORT (LOW)");
        pd_draw_text(hud_x + 68.0f, 90.0f, 0.5f, COL_TEXT,
                     attrs.light ? "LIGHT" : "DARK");
        pd_draw_text(hud_x + 68.0f, 100.0f, 0.5f, COL_TEXT,
                     attrs.square ? "SQUARE" : "ROUND");
        pd_draw_text(hud_x + 68.0f, 110.0f, 0.5f, COL_TEXT,
                     attrs.solid ? "SOLID" : "HOLLOW");
    } else {
        pd_draw_text(hud_x + 14.0f, 82.0f, 0.5f, COL_TEXT_MUTED, "OPENING: GIVE 1ST PIECE");
    }

    /* Mode-specific coaching / status section */
    pd_draw_rect(hud_x + 8.0f, 130.0f, 156.0f, 90.0f, COL_PANEL_ALT);
    if (app->screen == PD_SCREEN_REPLAY) {
        char ply_buf[48];
        snprintf(ply_buf, sizeof(ply_buf), "PLY %d / %u",
                 app->replay_ply, (unsigned)app->current_record.move_count);
        pd_draw_text(hud_x + 14.0f, 138.0f, 0.5f, COL_ACCENT, ply_buf);

        if (app->replay_review.missed_win_cell != PD_NONE) {
            char msg[48];
            snprintf(msg, sizeof(msg), "MISSED WIN AT CELL %d!", app->replay_review.missed_win_cell);
            pd_draw_text(hud_x + 14.0f, 154.0f, 0.5f, COL_DANGER, msg);
        } else if (app->replay_review.unsafe_give) {
            char msg[48];
            snprintf(msg, sizeof(msg), "BLUNDER GIVE! SAFER: #%d", app->replay_review.safer_give);
            pd_draw_text(hud_x + 14.0f, 154.0f, 0.5f, COL_THREAT, msg);
        } else if (app->replay_review.forced_loss) {
            pd_draw_text(hud_x + 14.0f, 154.0f, 0.5f, COL_DANGER, "HANDED FORCED MATE IN 2!");
        } else {
            pd_draw_text(hud_x + 14.0f, 154.0f, 0.5f, COL_WIN_GLOW, "SOUND MOVE");
        }
    } else if (app->screen == PD_SCREEN_DAILY) {
        char d_buf[48];
        snprintf(d_buf, sizeof(d_buf), "DATE: %s", app->daily_date[0] ? app->daily_date : "TODAY");
        pd_draw_text(hud_x + 14.0f, 138.0f, 0.5f, COL_ACCENT, d_buf);
        snprintf(d_buf, sizeof(d_buf), "STREAK: %d (BEST %d)",
                 app->save.streak_current, app->save.streak_best);
        pd_draw_text(hud_x + 14.0f, 152.0f, 0.5f, COL_TEXT, d_buf);
    } else if (app->screen == PD_SCREEN_TRAINERS && app->rush_active) {
        char r_buf[48];
        snprintf(r_buf, sizeof(r_buf), "RUSH SCORE: %d  STRK: %d", app->rush_score, app->rush_strikes);
        pd_draw_text(hud_x + 14.0f, 138.0f, 0.5f, COL_THREAT, r_buf);
    } else {
        char turn_buf[48];
        snprintf(turn_buf, sizeof(turn_buf), "SEAT: P%d   THREATS: %d",
                 app->game.current + 1, threat_count);
        pd_draw_text(hud_x + 14.0f, 138.0f, 0.5f, COL_TEXT, turn_buf);
    }

    if (app->status_banner[0] != '\0') {
        pd_draw_text(hud_x + 14.0f, 188.0f, 0.5f, COL_TEXT, app->status_banner);
    }
    if (app->trainer_feedback[0] != '\0' && app->screen == PD_SCREEN_TRAINERS) {
        pd_draw_text(hud_x + 14.0f, 202.0f, 0.5f, COL_THREAT, app->trainer_feedback);
    }
}

static void render_bottom_board_and_tray(const pd_app_t *app) {
    const int *board = NULL;
    int piece_in_hand = PD_NONE;
    pd_variant_t variant = PD_VARIANT_STANDARD;
    bool has_win = false;
    pd_line_t win_line;
    resolve_active_view(app, &board, &piece_in_hand, &variant, &has_win, &win_line);

    /* Top Header Bar on Bottom Touch Screen */
    pd_draw_rect(0.0f, 0.0f, 320.0f, 28.0f, COL_PANEL);
    pd_draw_text(12.0f, 8.0f, 0.55f, COL_ACCENT, screen_title_str(app->screen));
    pd_draw_text(172.0f, 8.0f, 0.5f, COL_TEXT_MUTED, "16-PIECE TRAY");

    /* Left 4x4 Touch Board Grid (x=12, y=36, stride=36, size=34) */
    for (int cell = 0; cell < PD_BOARD_SIZE; cell++) {
        int row = cell / 4;
        int col = cell % 4;
        float x = 12.0f + (float)col * 36.0f;
        float y = 36.0f + (float)row * 36.0f;

        uint32_t bg = ((row + col) & 1) ? COL_CELL_ALT : COL_CELL_BG;
        if (cell == app->pending_cell) {
            bg = PD_RGBA8(42, 84, 126, 255);
        }
        pd_draw_rect(x, y, 34.0f, 34.0f, bg);

        if (cell == app->selected_cell && !app->cursor_on_tray) {
            pd_draw_rect_outline(x, y, 34.0f, 34.0f, 2.0f, COL_ACCENT);
        } else if (cell == app->pending_cell) {
            pd_draw_rect_outline(x, y, 34.0f, 34.0f, 2.0f, COL_PENDING);
        }

        if (pd_is_piece_id(board[cell])) {
            draw_piece_3d(board[cell], x + 17.0f, y + 17.0f, 11.0f, 0.0f, 0.0f, app->save.colorblind_mode);
        }
    }

    /* Right 4x4 Piece Selection Tray (x=172, y=36, stride=34, size=32) */
    for (int p = 0; p < PD_PIECE_COUNT; p++) {
        int row = p / 4;
        int col = p % 4;
        float x = 172.0f + (float)col * 34.0f;
        float y = 36.0f + (float)row * 34.0f;

        bool avail = is_piece_in_available(&app->game, p);
        uint32_t slot_bg = avail ? COL_PANEL_ALT : PD_RGBA8(14, 18, 30, 255);
        pd_draw_rect(x, y, 32.0f, 32.0f, slot_bg);

        if (p == app->selected_piece && app->cursor_on_tray) {
            pd_draw_rect_outline(x, y, 32.0f, 32.0f, 2.0f, COL_ACCENT);
        }

        if (avail) {
            draw_piece_3d(p, x + 16.0f, y + 16.0f, 9.5f, 0.0f, 0.0f, app->save.colorblind_mode);
        } else if (p == piece_in_hand) {
            pd_draw_rect_outline(x + 2.0f, y + 2.0f, 28.0f, 28.0f, 1.5f, COL_THREAT);
        }
    }

    /* Bottom Action Bar Buttons (y=194, h=36, 4 buttons across x=12..308) */
    const char *btn_labels[4] = { "A:ACTION", "X:HINTS", "Y:COLOR", "B:BACK" };
    for (int i = 0; i < 4; i++) {
        float bx = 12.0f + (float)i * 74.0f;
        float by = 194.0f;
        pd_draw_rect(bx, by, 70.0f, 36.0f, COL_PANEL_ALT);
        pd_draw_rect_outline(bx, by, 70.0f, 36.0f, 1.5f, COL_BORDER);
        pd_draw_text(bx + 8.0f, by + 13.0f, 0.5f, COL_TEXT, btn_labels[i]);
    }
}

static void render_bottom_title(const pd_app_t *app) {
    pd_draw_rect(0.0f, 0.0f, 320.0f, 28.0f, COL_PANEL);
    pd_draw_text(12.0f, 8.0f, 0.55f, COL_ACCENT, "SELECT MODE (TOUCH OR D-PAD)");

    const char *items[7] = {
        "1. VS AI MATCH",
        "2. PASS & PLAY (2P)",
        "3. DAILY PUZZLE",
        "4. TACTICS TRAINERS",
        "5. MULTIPLAYER (UDS/LAN/WEB)",
        "6. INTERACTIVE TUTORIAL",
        "7. REPLAYS & STATS"
    };

    for (int i = 0; i < 7; i++) {
        float y = 34.0f + (float)i * 22.0f;
        bool sel = (app->menu_index == i);
        pd_draw_rect(12.0f, y, 296.0f, 19.0f, sel ? COL_PANEL_ALT : COL_PANEL);
        pd_draw_rect_outline(12.0f, y, 296.0f, 19.0f, 1.0f, sel ? COL_ACCENT : COL_BORDER);
        pd_draw_text(20.0f, y + 5.0f, 0.5f, sel ? COL_ACCENT : COL_TEXT, items[i]);
    }

    /* Variant & AI Difficulty selector pills at bottom */
    char pill_buf[64];
    snprintf(pill_buf, sizeof(pill_buf), "L/R VARIANT: %s   X/Y AI: %s",
             pd_variant_to_str(app->selected_variant),
             pd_difficulty_to_str(app->selected_difficulty));
    pd_draw_rect(12.0f, 194.0f, 296.0f, 34.0f, COL_PANEL_ALT);
    pd_draw_rect_outline(12.0f, 194.0f, 296.0f, 34.0f, 1.5f, COL_BORDER);
    pd_draw_text(20.0f, 206.0f, 0.5f, COL_TEXT, pill_buf);
}

static void render_bottom_multiplayer_or_stats(const pd_app_t *app) {
    pd_draw_rect(0.0f, 0.0f, 320.0f, 28.0f, COL_PANEL);
    pd_draw_text(12.0f, 8.0f, 0.55f, COL_ACCENT, screen_title_str(app->screen));

    if (app->screen == PD_SCREEN_MULTIPLAYER) {
        const char *mp_opts[6] = {
            "1. HOST 3DS LOCAL WIRELESS (UDS)",
            "2. JOIN 3DS LOCAL WIRELESS (UDS)",
            "3. HOST LAN P2P ROOM (UDP/TCP)",
            "4. SCAN & JOIN LAN P2P ROOM",
            "5. WEB SERVER CREATE ROOM (LOGIN)",
            "6. WEB SERVER JOIN / MATCHMAKE"
        };
        for (int i = 0; i < 6; i++) {
            float y = 32.0f + (float)i * 22.0f;
            bool sel = (app->mp_menu_index == i);
            pd_draw_rect(12.0f, y, 296.0f, 19.0f, sel ? COL_PANEL_ALT : COL_PANEL);
            pd_draw_rect_outline(12.0f, y, 296.0f, 19.0f, 1.0f, sel ? COL_ACCENT : COL_BORDER);
            pd_draw_text(18.0f, y + 5.0f, 0.5f, sel ? COL_ACCENT : COL_TEXT, mp_opts[i]);
        }
        char net_info[256];
        snprintf(net_info, sizeof(net_info), "BEACONS:%d HOST:%.20s:%u (X:IP Y:CODE)",
                 app->net.beacon_count, app->save.server_host, (unsigned)app->save.server_port);
        pd_draw_text(12.0f, 172.0f, 0.48f, COL_TEXT_MUTED, net_info);

        const char *mp_bar[4] = { "A:SELECT", "X:HOST IP", "Y:INVITE", "B:BACK" };
        for (int i = 0; i < 4; i++) {
            float bx = 12.0f + (float)i * 74.0f;
            pd_draw_rect(bx, 194.0f, 70.0f, 36.0f, COL_PANEL_ALT);
            pd_draw_rect_outline(bx, 194.0f, 70.0f, 36.0f, 1.5f, COL_BORDER);
            pd_draw_text(bx + 6.0f, 207.0f, 0.48f, COL_TEXT, mp_bar[i]);
        }
    } else {
        char line[64];
        snprintf(line, sizeof(line), "ELO RATING: %d", app->save.elo_rating);
        pd_draw_text(16.0f, 40.0f, 0.55f, COL_ACCENT, line);

        snprintf(line, sizeof(line), "AI EASY   W:%d L:%d D:%d",
                 app->save.ai_wins[0], app->save.ai_losses[0], app->save.ai_draws[0]);
        pd_draw_text(16.0f, 62.0f, 0.5f, COL_TEXT, line);

        snprintf(line, sizeof(line), "AI MEDIUM W:%d L:%d D:%d",
                 app->save.ai_wins[1], app->save.ai_losses[1], app->save.ai_draws[1]);
        pd_draw_text(16.0f, 78.0f, 0.5f, COL_TEXT, line);

        snprintf(line, sizeof(line), "AI HARD   W:%d L:%d D:%d",
                 app->save.ai_wins[2], app->save.ai_losses[2], app->save.ai_draws[2]);
        pd_draw_text(16.0f, 94.0f, 0.5f, COL_TEXT, line);

        snprintf(line, sizeof(line), "PVP       W:%d L:%d D:%d",
                 app->save.pvp_wins, app->save.pvp_losses, app->save.pvp_draws);
        pd_draw_text(16.0f, 112.0f, 0.5f, COL_TEXT, line);

        snprintf(line, sizeof(line), "DAILY STREAK: %d (BEST %d)",
                 app->save.streak_current, app->save.streak_best);
        pd_draw_text(16.0f, 132.0f, 0.5f, COL_THREAT, line);

        snprintf(line, sizeof(line), "SAVED REPLAYS: %d (A: REVIEW)",
                 app->save.replay_count);
        pd_draw_text(16.0f, 152.0f, 0.5f, COL_WIN_GLOW, line);

        const char *st_bar[4] = { "A:REPLAYS", "X:COLOR", "Y:HINTS", "B:BACK" };
        for (int i = 0; i < 4; i++) {
            float bx = 12.0f + (float)i * 74.0f;
            pd_draw_rect(bx, 194.0f, 70.0f, 36.0f, COL_PANEL_ALT);
            pd_draw_rect_outline(bx, 194.0f, 70.0f, 36.0f, 1.5f, COL_BORDER);
            pd_draw_text(bx + 6.0f, 207.0f, 0.48f, COL_TEXT, st_bar[i]);
        }
    }
}

void pd_ui_render(const pd_app_t *app, float slider_3d) {
    if (app == NULL) {
        return;
    }

    if (slider_3d < 0.0f) slider_3d = 0.0f;
    if (slider_3d > 1.0f) slider_3d = 1.0f;

    pd_render_begin_frame();

    /* Left Eye Top Screen (400x240) */
    pd_render_begin_target(PD_TARGET_TOP_LEFT, COL_BG_TOP);
    render_top_stereoscopic(app, -1.0f, slider_3d);

    /* Right Eye Top Screen (400x240) when 3D slider is engaged */
    if (slider_3d > 0.0f) {
        pd_render_begin_target(PD_TARGET_TOP_RIGHT, COL_BG_TOP);
        render_top_stereoscopic(app, +1.0f, slider_3d);
    }

    /* Bottom Touch Screen (320x240) */
    pd_render_begin_target(PD_TARGET_BOTTOM, COL_BG_BOT);
    if (app->screen == PD_SCREEN_TITLE) {
        render_bottom_title(app);
    } else if (app->screen == PD_SCREEN_MULTIPLAYER || app->screen == PD_SCREEN_STATS) {
        render_bottom_multiplayer_or_stats(app);
    } else {
        render_bottom_board_and_tray(app);
    }

    pd_render_end_frame();
}
