#include "pd_engine.h"
#include "pd_net.h"
#include "pd_platform.h"
#include "pd_storage.h"
#include "pd_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int count_non_clear_pixels(const uint32_t *fb, int count) {
    if (fb == NULL || count <= 0) {
        return 0;
    }
    uint32_t bg = fb[0];
    int non_clear = 0;
    for (int i = 0; i < count; i++) {
        if (fb[i] != bg) {
            non_clear++;
        }
    }
    return non_clear;
}

static int count_stereo_diff_pixels(const uint32_t *left_fb, const uint32_t *right_fb, int count) {
    int diffs = 0;
    for (int i = 0; i < count; i++) {
        if (left_fb[i] != right_fb[i]) {
            diffs++;
        }
    }
    return diffs;
}

static void test_platform_host_helpers(void) {
    assert(pd_platform_init());
    assert(pd_platform_main_loop());

    char date_buf[11] = {0};
    pd_platform_get_utc_date(date_buf);
    assert(strlen(date_buf) == 10);
    assert(date_buf[4] == '-' && date_buf[7] == '-');

    uint64_t t0 = pd_platform_ticks_ms();
    assert(t0 > 0);
    assert(strcmp(pd_platform_default_save_path(), "build/host/save.dat") == 0);

    char text_buf[64] = "initial";
    pd_platform_host_queue_prompt_response("player_3ds");
    assert(pd_platform_prompt_text("Enter name", text_buf, sizeof(text_buf), false));
    assert(strcmp(text_buf, "player_3ds") == 0);
    assert(!pd_platform_prompt_text("Enter name", text_buf, sizeof(text_buf), false));

    pd_input_t inj;
    memset(&inj, 0, sizeof(inj));
    inj.down = PD_BTN_A;
    inj.touching = true;
    inj.touch_just_pressed = true;
    inj.touch_x = 42;
    inj.touch_y = 84;
    inj.slider_3d = 0.75f;
    pd_platform_host_inject_input(&inj);

    pd_input_t polled;
    pd_platform_poll_input(&polled);
    assert(polled.down == PD_BTN_A);
    assert(polled.touch_just_pressed);
    assert(polled.touch_x == 42 && polled.touch_y == 84);

    /* Subsequent poll clears one-shot edges */
    pd_platform_poll_input(&polled);
    assert(polled.down == 0);
    assert(!polled.touch_just_pressed);
}

static void test_hit_testing(void) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int tx = 12 + col * 36 + 15;
            int ty = 36 + row * 36 + 15;
            assert(pd_ui_hit_test_board(tx, ty) == row * 4 + col);
        }
    }
    assert(pd_ui_hit_test_board(4, 4) == PD_NONE);
    assert(pd_ui_hit_test_board(164, 80) == PD_NONE);
    assert(pd_ui_hit_test_board(50, 190) == PD_NONE);
    /* 2px inter-cell gutter check on board */
    assert(pd_ui_hit_test_board(12 + 34, 36 + 10) == PD_NONE);

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int tx = 172 + col * 34 + 12;
            int ty = 36 + row * 34 + 12;
            assert(pd_ui_hit_test_tray(tx, ty) == row * 4 + col);
        }
    }
    assert(pd_ui_hit_test_tray(160, 60) == PD_NONE);
    assert(pd_ui_hit_test_tray(315, 60) == PD_NONE);
    /* 2px inter-cell gutter check on tray */
    assert(pd_ui_hit_test_tray(172 + 32, 36 + 10) == PD_NONE);

    for (int i = 0; i < 4; i++) {
        int tx = 12 + i * 74 + 20;
        int ty = 205;
        assert(pd_ui_hit_test_bottom_bar(tx, ty) == i);
    }
    assert(pd_ui_hit_test_bottom_bar(4, 205) == PD_NONE);
    assert(pd_ui_hit_test_bottom_bar(315, 205) == PD_NONE);
    assert(pd_ui_hit_test_bottom_bar(50, 180) == PD_NONE);
}

static void test_match_input_and_stereoscopic_render(void) {
    pd_app_t app;
    memset(&app, 0, sizeof(app));
    app.running = true;
    app.screen = PD_SCREEN_MATCH;
    app.selected_mode = PD_GAME_MODE_PASS_PLAY;
    app.selected_difficulty = PD_DIFF_MEDIUM;
    app.selected_variant = PD_VARIANT_STANDARD;
    app.pending_cell = PD_NONE;
    app.net.preview_cell = PD_NONE;
    app.save.show_threat_hints = true;
    pd_create_game(&app.game, 0, PD_VARIANT_STANDARD);

    /* Step 1: Opening give via stylus touch on tray piece #5 */
    pd_input_t in;
    memset(&in, 0, sizeof(in));
    in.touching = true;
    in.touch_just_pressed = true;
    in.touch_x = 172 + (5 % 4) * 34 + 10;
    in.touch_y = 36 + (5 / 4) * 34 + 10;
    pd_ui_handle_match_input(&app, &in);

    assert(app.game.piece_in_hand == 5);
    assert(app.game.current == 1);
    assert(app.current_record.move_count == 1);
    assert(app.current_record.moves[0].given_piece == 5);

    /* Step 2: Tap board cell #6 to stage placement (with online mode enabled to verify preview) */
    app.net.mode = PD_NET_MODE_LAN_P2P;
    memset(&in, 0, sizeof(in));
    in.touching = true;
    in.touch_just_pressed = true;
    in.touch_x = 12 + (6 % 4) * 36 + 10;
    in.touch_y = 36 + (6 / 4) * 36 + 10;
    pd_ui_handle_match_input(&app, &in);
    assert(app.pending_cell == 6);
    assert(app.cursor_on_tray);

    /* Cancel staged placement with B button */
    memset(&in, 0, sizeof(in));
    in.down = PD_BTN_B;
    pd_ui_handle_match_input(&app, &in);
    assert(app.pending_cell == PD_NONE);
    assert(!app.cursor_on_tray);

    /* Re-stage cell #6 using A button */
    app.selected_cell = 6;
    memset(&in, 0, sizeof(in));
    in.down = PD_BTN_A;
    pd_ui_handle_match_input(&app, &in);
    assert(app.pending_cell == 6);

    /* Give piece #10 via touch on tray */
    memset(&in, 0, sizeof(in));
    in.touching = true;
    in.touch_just_pressed = true;
    in.touch_x = 172 + (10 % 4) * 34 + 10;
    in.touch_y = 36 + (10 / 4) * 34 + 10;
    pd_ui_handle_match_input(&app, &in);

    assert(app.game.board[6] == 5);
    assert(app.game.piece_in_hand == 10);
    assert(app.pending_cell == PD_NONE);
    assert(app.current_record.move_count == 2);
    assert(app.current_record.moves[1].cell == 6);
    assert(app.current_record.moves[1].placed_piece == 5);
    assert(app.current_record.moves[1].given_piece == 10);

    /* Place a tall piece (#15) at cell #0 and give #0 so both tall & short exist on board */
    memset(&in, 0, sizeof(in));
    in.touching = true;
    in.touch_just_pressed = true;
    in.touch_x = 12 + 10;
    in.touch_y = 36 + 10;
    pd_ui_handle_match_input(&app, &in);
    assert(app.pending_cell == 0);

    memset(&in, 0, sizeof(in));
    in.touching = true;
    in.touch_just_pressed = true;
    in.touch_x = 172 + (15 % 4) * 34 + 10;
    in.touch_y = 36 + (15 / 4) * 34 + 10;
    pd_ui_handle_match_input(&app, &in);
    assert(app.game.board[0] == 10);
    assert(app.game.piece_in_hand == 15);

    /* Render 2D (slider_3d = 0.0f) */
    pd_ui_render(&app, 0.0f);
    const uint32_t *fb_left = pd_platform_host_get_framebuffer(PD_TARGET_TOP_LEFT);
    const uint32_t *fb_right = pd_platform_host_get_framebuffer(PD_TARGET_TOP_RIGHT);
    const uint32_t *fb_bot = pd_platform_host_get_framebuffer(PD_TARGET_BOTTOM);
    assert(count_non_clear_pixels(fb_left, PD_TOP_WIDTH * PD_TOP_HEIGHT) > 2000);
    assert(count_non_clear_pixels(fb_bot, PD_BOT_WIDTH * PD_BOT_HEIGHT) > 2000);

    /* Render Stereoscopic 3D (slider_3d = 1.0f) and verify parallax disparity */
    pd_ui_render(&app, 1.0f);
    int left_pixels = count_non_clear_pixels(fb_left, PD_TOP_WIDTH * PD_TOP_HEIGHT);
    int right_pixels = count_non_clear_pixels(fb_right, PD_TOP_WIDTH * PD_TOP_HEIGHT);
    int stereo_diffs = count_stereo_diff_pixels(fb_left, fb_right, PD_TOP_WIDTH * PD_TOP_HEIGHT);
    assert(left_pixels > 2000);
    assert(right_pixels > 2000);
    assert(stereo_diffs > 500);

    /* Verify rendering across all screens & colorblind mode */
    app.save.colorblind_mode = true;
    pd_screen_t screens[] = {
        PD_SCREEN_TITLE,
        PD_SCREEN_DAILY,
        PD_SCREEN_TRAINERS,
        PD_SCREEN_TUTORIAL,
        PD_SCREEN_REPLAY,
        PD_SCREEN_MULTIPLAYER,
        PD_SCREEN_STATS
    };
    for (size_t i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) {
        app.screen = screens[i];
        pd_ui_render(&app, 0.8f);
        assert(count_non_clear_pixels(fb_left, PD_TOP_WIDTH * PD_TOP_HEIGHT) > 1000);
        assert(count_non_clear_pixels(fb_bot, PD_BOT_WIDTH * PD_BOT_HEIGHT) > 500);
    }

    /* Verify Puzzle Rush updates top-screen framebuffer after solving Puzzle #1 */
    pd_app_t rush_app;
    pd_app_init(&rush_app, "build/host/test_rush_save.dat");
    /* Put a piece on rush_app.game.board first to ensure resolve_active_view does not get stuck on stale game.board */
    rush_app.game.board[0] = 15;
    pd_app_load_trainer(&rush_app, PD_TRAINER_RUSH, 1001u);
    assert(rush_app.rush_active);
    assert(rush_app.daily_puzzle.solution_count > 0);

    static uint32_t fb_puzzle1[PD_TOP_WIDTH * PD_TOP_HEIGHT];
    pd_ui_render(&rush_app, 0.0f);
    memcpy(fb_puzzle1, fb_left, sizeof(fb_puzzle1));

    rush_app.selected_cell = rush_app.daily_puzzle.solution[0];
    memset(&in, 0, sizeof(in));
    in.down = PD_BTN_A;
    pd_app_update(&rush_app, &in);
    assert(rush_app.rush_score == 1);
    assert(rush_app.rush_streak == 1);

    pd_ui_render(&rush_app, 0.0f);
    assert(count_stereo_diff_pixels(fb_puzzle1, fb_left, PD_TOP_WIDTH * PD_TOP_HEIGHT) > 0);

    /* Verify exiting a networked match with B closes app->net */
    pd_app_start_match(&rush_app, PD_GAME_MODE_P2P_LAN, PD_DIFF_MEDIUM, PD_VARIANT_STANDARD);
    rush_app.net.mode = PD_NET_MODE_LAN_P2P;
    rush_app.net.state = PD_NET_STATE_ACTIVE;
    rush_app.net.my_seat = 0;
    rush_app.pending_cell = PD_NONE;
    memset(&in, 0, sizeof(in));
    in.down = PD_BTN_B;
    pd_app_update(&rush_app, &in);
    assert(rush_app.screen == PD_SCREEN_TITLE);
    assert(rush_app.net.mode == PD_NET_MODE_NONE);

    pd_app_shutdown(&rush_app);
    remove("build/host/test_rush_save.dat");
    pd_platform_shutdown();
}

int main(void) {
    test_platform_host_helpers();
    test_hit_testing();
    test_match_input_and_stereoscopic_render();
    printf("test_ui_sim: ALL TESTS PASSED\n");
    return 0;
}
