#ifdef __3DS__

#include "pd_platform.h"
#include "pd_storage.h"

#include <3ds.h>
#include <citro2d.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define PD_SOC_ALIGN 0x1000
#define PD_SOC_BUFFERSIZE 0x100000

static u32 *g_soc_buffer = NULL;
static C3D_RenderTarget *g_target_top_left = NULL;
static C3D_RenderTarget *g_target_top_right = NULL;
static C3D_RenderTarget *g_target_bottom = NULL;
static C2D_TextBuf g_text_buf = NULL;
static bool g_prev_touching = false;
static int g_last_touch_x = 0;
static int g_last_touch_y = 0;

static uint32_t map_3ds_keys(u32 k) {
    uint32_t out = 0;
    if (k & KEY_A) out |= PD_BTN_A;
    if (k & KEY_B) out |= PD_BTN_B;
    if (k & KEY_SELECT) out |= PD_BTN_SELECT;
    if (k & KEY_START) out |= PD_BTN_START;
    if (k & (KEY_DRIGHT | KEY_CPAD_RIGHT)) out |= PD_BTN_DRIGHT;
    if (k & (KEY_DLEFT | KEY_CPAD_LEFT)) out |= PD_BTN_DLEFT;
    if (k & (KEY_DUP | KEY_CPAD_UP)) out |= PD_BTN_DUP;
    if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) out |= PD_BTN_DDOWN;
    if (k & KEY_R) out |= PD_BTN_R;
    if (k & KEY_L) out |= PD_BTN_L;
    if (k & KEY_X) out |= PD_BTN_X;
    if (k & KEY_Y) out |= PD_BTN_Y;
    return out;
}

bool pd_platform_init(void) {
    gfxInitDefault();
    gfxSet3D(true);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    romfsInit();
    sdmcInit();
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/pieceduel", 0777);

    g_soc_buffer = (u32 *)memalign(PD_SOC_ALIGN, PD_SOC_BUFFERSIZE);
    if (g_soc_buffer != NULL) {
        if (socInit(g_soc_buffer, PD_SOC_BUFFERSIZE) != 0) {
            free(g_soc_buffer);
            g_soc_buffer = NULL;
        }
    }

    g_target_top_left = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_target_top_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    g_target_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_text_buf = C2D_TextBufNew(4096);
    g_prev_touching = false;
    g_last_touch_x = 0;
    g_last_touch_y = 0;

    return g_target_top_left != NULL && g_target_bottom != NULL && g_text_buf != NULL;
}

void pd_platform_shutdown(void) {
    if (g_text_buf != NULL) {
        C2D_TextBufDelete(g_text_buf);
        g_text_buf = NULL;
    }
    C2D_Fini();
    C3D_Fini();

    if (g_soc_buffer != NULL) {
        socExit();
        free(g_soc_buffer);
        g_soc_buffer = NULL;
    }

    sdmcExit();
    romfsExit();
    gfxExit();
}

bool pd_platform_main_loop(void) {
    return aptMainLoop();
}

void pd_platform_poll_input(pd_input_t *out_input) {
    if (out_input == NULL) {
        return;
    }
    memset(out_input, 0, sizeof(*out_input));

    hidScanInput();
    u32 k_down = hidKeysDown();
    u32 k_held = hidKeysHeld();
    u32 k_up = hidKeysUp();

    out_input->down = map_3ds_keys(k_down);
    out_input->held = map_3ds_keys(k_held);
    out_input->up = map_3ds_keys(k_up);

    bool touching = (k_held & KEY_TOUCH) != 0;
    out_input->touching = touching;
    out_input->touch_just_pressed = touching && !g_prev_touching;

    if (touching) {
        touchPosition touch;
        hidTouchRead(&touch);
        g_last_touch_x = (int)touch.px;
        g_last_touch_y = (int)touch.py;
    }

    out_input->touch_x = g_last_touch_x;
    out_input->touch_y = g_last_touch_y;
    g_prev_touching = touching;

    out_input->slider_3d = osGet3DSliderState();
}

void pd_platform_get_utc_date(char out_iso_date[11]) {
    if (out_iso_date == NULL) {
        return;
    }
    time_t now = time(NULL);
    struct tm *tm_utc = gmtime(&now);
    if (tm_utc != NULL && tm_utc->tm_year >= 120) {
        char tmp[32];
        int year = (tm_utc->tm_year + 1900) % 10000;
        int mon = (tm_utc->tm_mon + 1) % 100;
        int day = tm_utc->tm_mday % 100;
        snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d", year, mon, day);
        memcpy(out_iso_date, tmp, 10);
        out_iso_date[10] = '\0';
    } else {
        memcpy(out_iso_date, "2026-01-01", 11);
    }
}

uint64_t pd_platform_ticks_ms(void) {
    return (uint64_t)osGetTime();
}

const char *pd_platform_default_save_path(void) {
    return PD_DEFAULT_SAVE_PATH_3DS;
}

bool pd_platform_prompt_text(const char *hint, char *inout_buf, size_t max_len, bool is_password) {
    if (inout_buf == NULL || max_len < 2) {
        return false;
    }

    SwkbdState swkbd;
    int max_chars = (int)(max_len - 1);
    if (max_chars > 255) {
        max_chars = 255;
    }

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, max_chars);
    swkbdSetInitialText(&swkbd, inout_buf);
    if (hint != NULL) {
        swkbdSetHintText(&swkbd, hint);
    }
    if (is_password) {
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
    }
    swkbdSetFeatures(&swkbd, SWKBD_DEFAULT_QWERTY);

    char temp[256];
    memset(temp, 0, sizeof(temp));
    size_t cap = max_len < sizeof(temp) ? max_len : sizeof(temp);
    SwkbdButton button = swkbdInputText(&swkbd, temp, cap);
    if (button == SWKBD_BUTTON_RIGHT) {
        snprintf(inout_buf, max_len, "%s", temp);
        return true;
    }
    return false;
}

void pd_render_begin_frame(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    if (g_text_buf != NULL) {
        C2D_TextBufClear(g_text_buf);
    }
}

void pd_render_begin_target(pd_render_target_t target, uint32_t clear_color) {
    C3D_RenderTarget *rt = g_target_top_left;
    if (target == PD_TARGET_TOP_RIGHT) {
        rt = g_target_top_right;
    } else if (target == PD_TARGET_BOTTOM) {
        rt = g_target_bottom;
    }
    if (rt != NULL) {
        C2D_TargetClear(rt, clear_color);
        C2D_SceneBegin(rt);
    }
}

void pd_render_end_frame(void) {
    C3D_FrameEnd(0);
}

void pd_draw_rect(float x, float y, float w, float h, uint32_t color) {
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    C2D_DrawRectSolid(x, y, 0.5f, w, h, color);
}

void pd_draw_rect_outline(float x, float y, float w, float h, float thickness, uint32_t color) {
    if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f) {
        return;
    }
    float t = thickness;
    if (t * 2.0f > w) t = w * 0.5f;
    if (t * 2.0f > h) t = h * 0.5f;
    C2D_DrawRectSolid(x, y, 0.5f, w, t, color);
    C2D_DrawRectSolid(x, y + h - t, 0.5f, w, t, color);
    if (h > 2.0f * t) {
        C2D_DrawRectSolid(x, y + t, 0.5f, t, h - 2.0f * t, color);
        C2D_DrawRectSolid(x + w - t, y + t, 0.5f, t, h - 2.0f * t, color);
    }
}

void pd_draw_circle(float cx, float cy, float radius, uint32_t color) {
    if (radius <= 0.0f) {
        return;
    }
    C2D_DrawCircleSolid(cx, cy, 0.5f, radius, color);
}

void pd_draw_line(float x1, float y1, float x2, float y2, float thickness, uint32_t color) {
    if (thickness <= 0.0f) {
        return;
    }
    C2D_DrawLine(x1, y1, color, x2, y2, color, thickness, 0.5f);
}

void pd_draw_text(float x, float y, float scale, uint32_t color, const char *text) {
    if (text == NULL || text[0] == '\0' || g_text_buf == NULL) {
        return;
    }
    C2D_Text c2d_text;
    if (C2D_TextParse(&c2d_text, g_text_buf, text) != NULL) {
        C2D_TextOptimize(&c2d_text);
        C2D_DrawText(&c2d_text, C2D_WithColor, x, y, 0.5f, scale, scale, color);
    }
}

const uint32_t *pd_platform_host_get_framebuffer(pd_render_target_t target) {
    (void)target;
    return NULL;
}

void pd_platform_host_inject_input(const pd_input_t *input) {
    (void)input;
}

void pd_platform_host_queue_prompt_response(const char *text) {
    (void)text;
}

#else
typedef int pd_platform_3ds_unused_t;
#endif /* __3DS__ */
