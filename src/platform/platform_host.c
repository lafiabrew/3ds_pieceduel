#ifndef __3DS__

#define _POSIX_C_SOURCE 200809L

#include "pd_platform.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static uint32_t fb_top_left[PD_TOP_WIDTH * PD_TOP_HEIGHT];
static uint32_t fb_top_right[PD_TOP_WIDTH * PD_TOP_HEIGHT];
static uint32_t fb_bot[PD_BOT_WIDTH * PD_BOT_HEIGHT];

static uint32_t *g_active_fb = fb_top_left;
static int g_active_w = PD_TOP_WIDTH;
static int g_active_h = PD_TOP_HEIGHT;

static bool g_host_running = true;
static pd_input_t g_injected_input;
static char g_prompt_response[512];
static bool g_has_prompt_response = false;
static uint64_t g_fallback_ticks = 1000;

static void blend_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= g_active_w || y < 0 || y >= g_active_h || g_active_fb == NULL) {
        return;
    }
    uint32_t a = (color >> 24) & 0xFFu;
    if (a == 0u) {
        return;
    }
    int idx = y * g_active_w + x;
    if (a == 255u) {
        g_active_fb[idx] = color;
        return;
    }
    uint32_t dst = g_active_fb[idx];
    uint32_t sr = color & 0xFFu;
    uint32_t sg = (color >> 8) & 0xFFu;
    uint32_t sb = (color >> 16) & 0xFFu;

    uint32_t dr = dst & 0xFFu;
    uint32_t dg = (dst >> 8) & 0xFFu;
    uint32_t db = (dst >> 16) & 0xFFu;

    uint32_t inv_a = 255u - a;
    uint32_t rr = (sr * a + dr * inv_a) / 255u;
    uint32_t rg = (sg * a + dg * inv_a) / 255u;
    uint32_t rb = (sb * a + db * inv_a) / 255u;

    g_active_fb[idx] = PD_RGBA8(rr, rg, rb, 255u);
}

/* Compact 5x7 bitmap font for printable ASCII (5 columns, 7 rows per glyph) */
static void get_glyph_5x7(char ch, uint8_t cols[5]) {
    unsigned char uc = (unsigned char)toupper((unsigned char)ch);
    switch (uc) {
    case 'A': cols[0] = 0x7E; cols[1] = 0x11; cols[2] = 0x11; cols[3] = 0x11; cols[4] = 0x7E; break;
    case 'B': cols[0] = 0x7F; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x36; break;
    case 'C': cols[0] = 0x3E; cols[1] = 0x41; cols[2] = 0x41; cols[3] = 0x41; cols[4] = 0x22; break;
    case 'D': cols[0] = 0x7F; cols[1] = 0x41; cols[2] = 0x41; cols[3] = 0x22; cols[4] = 0x1C; break;
    case 'E': cols[0] = 0x7F; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x41; break;
    case 'F': cols[0] = 0x7F; cols[1] = 0x09; cols[2] = 0x09; cols[3] = 0x09; cols[4] = 0x01; break;
    case 'G': cols[0] = 0x3E; cols[1] = 0x41; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x7A; break;
    case 'H': cols[0] = 0x7F; cols[1] = 0x08; cols[2] = 0x08; cols[3] = 0x08; cols[4] = 0x7F; break;
    case 'I': cols[0] = 0x00; cols[1] = 0x41; cols[2] = 0x7F; cols[3] = 0x41; cols[4] = 0x00; break;
    case 'J': cols[0] = 0x20; cols[1] = 0x40; cols[2] = 0x41; cols[3] = 0x3F; cols[4] = 0x01; break;
    case 'K': cols[0] = 0x7F; cols[1] = 0x08; cols[2] = 0x14; cols[3] = 0x22; cols[4] = 0x41; break;
    case 'L': cols[0] = 0x7F; cols[1] = 0x40; cols[2] = 0x40; cols[3] = 0x40; cols[4] = 0x40; break;
    case 'M': cols[0] = 0x7F; cols[1] = 0x02; cols[2] = 0x0C; cols[3] = 0x02; cols[4] = 0x7F; break;
    case 'N': cols[0] = 0x7F; cols[1] = 0x04; cols[2] = 0x08; cols[3] = 0x10; cols[4] = 0x7F; break;
    case 'O': cols[0] = 0x3E; cols[1] = 0x41; cols[2] = 0x41; cols[3] = 0x41; cols[4] = 0x3E; break;
    case 'P': cols[0] = 0x7F; cols[1] = 0x09; cols[2] = 0x09; cols[3] = 0x09; cols[4] = 0x06; break;
    case 'Q': cols[0] = 0x3E; cols[1] = 0x41; cols[2] = 0x51; cols[3] = 0x21; cols[4] = 0x5E; break;
    case 'R': cols[0] = 0x7F; cols[1] = 0x09; cols[2] = 0x19; cols[3] = 0x29; cols[4] = 0x46; break;
    case 'S': cols[0] = 0x46; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x31; break;
    case 'T': cols[0] = 0x01; cols[1] = 0x01; cols[2] = 0x7F; cols[3] = 0x01; cols[4] = 0x01; break;
    case 'U': cols[0] = 0x3F; cols[1] = 0x40; cols[2] = 0x40; cols[3] = 0x40; cols[4] = 0x3F; break;
    case 'V': cols[0] = 0x1F; cols[1] = 0x20; cols[2] = 0x40; cols[3] = 0x20; cols[4] = 0x1F; break;
    case 'W': cols[0] = 0x3F; cols[1] = 0x40; cols[2] = 0x38; cols[3] = 0x40; cols[4] = 0x3F; break;
    case 'X': cols[0] = 0x63; cols[1] = 0x14; cols[2] = 0x08; cols[3] = 0x14; cols[4] = 0x63; break;
    case 'Y': cols[0] = 0x07; cols[1] = 0x08; cols[2] = 0x70; cols[3] = 0x08; cols[4] = 0x07; break;
    case 'Z': cols[0] = 0x61; cols[1] = 0x51; cols[2] = 0x49; cols[3] = 0x45; cols[4] = 0x43; break;
    case '0': cols[0] = 0x3E; cols[1] = 0x51; cols[2] = 0x49; cols[3] = 0x45; cols[4] = 0x3E; break;
    case '1': cols[0] = 0x00; cols[1] = 0x42; cols[2] = 0x7F; cols[3] = 0x40; cols[4] = 0x00; break;
    case '2': cols[0] = 0x42; cols[1] = 0x61; cols[2] = 0x51; cols[3] = 0x49; cols[4] = 0x46; break;
    case '3': cols[0] = 0x21; cols[1] = 0x41; cols[2] = 0x45; cols[3] = 0x4B; cols[4] = 0x31; break;
    case '4': cols[0] = 0x18; cols[1] = 0x14; cols[2] = 0x12; cols[3] = 0x7F; cols[4] = 0x10; break;
    case '5': cols[0] = 0x27; cols[1] = 0x45; cols[2] = 0x45; cols[3] = 0x45; cols[4] = 0x39; break;
    case '6': cols[0] = 0x3C; cols[1] = 0x4A; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x30; break;
    case '7': cols[0] = 0x01; cols[1] = 0x71; cols[2] = 0x09; cols[3] = 0x05; cols[4] = 0x03; break;
    case '8': cols[0] = 0x36; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x49; cols[4] = 0x36; break;
    case '9': cols[0] = 0x06; cols[1] = 0x49; cols[2] = 0x49; cols[3] = 0x29; cols[4] = 0x1E; break;
    case '-': cols[0] = 0x08; cols[1] = 0x08; cols[2] = 0x08; cols[3] = 0x08; cols[4] = 0x08; break;
    case '+': cols[0] = 0x08; cols[1] = 0x08; cols[2] = 0x3E; cols[3] = 0x08; cols[4] = 0x08; break;
    case ':': cols[0] = 0x00; cols[1] = 0x36; cols[2] = 0x36; cols[3] = 0x00; cols[4] = 0x00; break;
    case '.': cols[0] = 0x00; cols[1] = 0x60; cols[2] = 0x60; cols[3] = 0x00; cols[4] = 0x00; break;
    case ',': cols[0] = 0x00; cols[1] = 0x50; cols[2] = 0x30; cols[3] = 0x00; cols[4] = 0x00; break;
    case '/': cols[0] = 0x20; cols[1] = 0x10; cols[2] = 0x08; cols[3] = 0x04; cols[4] = 0x02; break;
    case '!': cols[0] = 0x00; cols[1] = 0x00; cols[2] = 0x5F; cols[3] = 0x00; cols[4] = 0x00; break;
    case '?': cols[0] = 0x02; cols[1] = 0x01; cols[2] = 0x51; cols[3] = 0x09; cols[4] = 0x06; break;
    case '#': cols[0] = 0x14; cols[1] = 0x7F; cols[2] = 0x14; cols[3] = 0x7F; cols[4] = 0x14; break;
    case '(': cols[0] = 0x00; cols[1] = 0x1C; cols[2] = 0x22; cols[3] = 0x41; cols[4] = 0x00; break;
    case ')': cols[0] = 0x00; cols[1] = 0x41; cols[2] = 0x22; cols[3] = 0x1C; cols[4] = 0x00; break;
    case '[': cols[0] = 0x00; cols[1] = 0x7F; cols[2] = 0x41; cols[3] = 0x41; cols[4] = 0x00; break;
    case ']': cols[0] = 0x00; cols[1] = 0x41; cols[2] = 0x41; cols[3] = 0x7F; cols[4] = 0x00; break;
    case '>': cols[0] = 0x00; cols[1] = 0x41; cols[2] = 0x22; cols[3] = 0x14; cols[4] = 0x08; break;
    case '<': cols[0] = 0x08; cols[1] = 0x14; cols[2] = 0x22; cols[3] = 0x41; cols[4] = 0x00; break;
    case '=': cols[0] = 0x14; cols[1] = 0x14; cols[2] = 0x14; cols[3] = 0x14; cols[4] = 0x14; break;
    case '_': cols[0] = 0x40; cols[1] = 0x40; cols[2] = 0x40; cols[3] = 0x40; cols[4] = 0x40; break;
    case '%': cols[0] = 0x23; cols[1] = 0x13; cols[2] = 0x08; cols[3] = 0x64; cols[4] = 0x62; break;
    case '*': cols[0] = 0x14; cols[1] = 0x08; cols[2] = 0x3E; cols[3] = 0x08; cols[4] = 0x14; break;
    case ' ': cols[0] = 0x00; cols[1] = 0x00; cols[2] = 0x00; cols[3] = 0x00; cols[4] = 0x00; break;
    default:  cols[0] = 0x00; cols[1] = 0x3E; cols[2] = 0x22; cols[3] = 0x3E; cols[4] = 0x00; break;
    }
}

bool pd_platform_init(void) {
    memset(fb_top_left, 0, sizeof(fb_top_left));
    memset(fb_top_right, 0, sizeof(fb_top_right));
    memset(fb_bot, 0, sizeof(fb_bot));
    memset(&g_injected_input, 0, sizeof(g_injected_input));
    memset(g_prompt_response, 0, sizeof(g_prompt_response));
    g_has_prompt_response = false;
    g_host_running = true;
    g_active_fb = fb_top_left;
    g_active_w = PD_TOP_WIDTH;
    g_active_h = PD_TOP_HEIGHT;

    mkdir("build", 0777);
    mkdir("build/host", 0777);
    return true;
}

void pd_platform_shutdown(void) {
    g_host_running = false;
}

bool pd_platform_main_loop(void) {
    return g_host_running;
}

void pd_platform_poll_input(pd_input_t *out_input) {
    if (out_input == NULL) {
        return;
    }
    *out_input = g_injected_input;
    g_injected_input.down = 0;
    g_injected_input.up = 0;
    g_injected_input.touch_just_pressed = false;
}

void pd_platform_get_utc_date(char out_iso_date[11]) {
    if (out_iso_date == NULL) {
        return;
    }
    time_t now = time(NULL);
    struct tm *tm_utc = gmtime(&now);
    if (tm_utc != NULL) {
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
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000l);
    }
    g_fallback_ticks += 16;
    return g_fallback_ticks;
}

const char *pd_platform_default_save_path(void) {
    return "build/host/save.dat";
}

bool pd_platform_prompt_text(const char *hint, char *inout_buf, size_t max_len, bool is_password) {
    (void)hint;
    (void)is_password;
    if (inout_buf == NULL || max_len < 2 || !g_has_prompt_response) {
        return false;
    }
    snprintf(inout_buf, max_len, "%s", g_prompt_response);
    g_has_prompt_response = false;
    return true;
}

void pd_render_begin_frame(void) {
    g_active_fb = fb_top_left;
    g_active_w = PD_TOP_WIDTH;
    g_active_h = PD_TOP_HEIGHT;
}

void pd_render_begin_target(pd_render_target_t target, uint32_t clear_color) {
    if (target == PD_TARGET_TOP_LEFT) {
        g_active_fb = fb_top_left;
        g_active_w = PD_TOP_WIDTH;
        g_active_h = PD_TOP_HEIGHT;
    } else if (target == PD_TARGET_TOP_RIGHT) {
        g_active_fb = fb_top_right;
        g_active_w = PD_TOP_WIDTH;
        g_active_h = PD_TOP_HEIGHT;
    } else {
        g_active_fb = fb_bot;
        g_active_w = PD_BOT_WIDTH;
        g_active_h = PD_BOT_HEIGHT;
    }

    int count = g_active_w * g_active_h;
    for (int i = 0; i < count; i++) {
        g_active_fb[i] = clear_color;
    }
}

void pd_render_end_frame(void) {
    const char *dump_path = getenv("PK_DUMP_FRAMEBUFFER");
    if (dump_path != NULL && dump_path[0] != '\0') {
        FILE *fp = fopen(dump_path, "wb");
        if (fp != NULL) {
            fwrite(fb_top_left, sizeof(uint32_t), PD_TOP_WIDTH * PD_TOP_HEIGHT, fp);
            fwrite(fb_bot, sizeof(uint32_t), PD_BOT_WIDTH * PD_BOT_HEIGHT, fp);
            fclose(fp);
        }
        g_host_running = false;
    }
}

void pd_draw_rect(float x, float y, float w, float h, uint32_t color) {
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = (int)ceilf(x + w);
    int y1 = (int)ceilf(y + h);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_active_w) x1 = g_active_w;
    if (y1 > g_active_h) y1 = g_active_h;

    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            blend_pixel(px, py, color);
        }
    }
}

void pd_draw_rect_outline(float x, float y, float w, float h, float thickness, uint32_t color) {
    if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f) {
        return;
    }
    float t = thickness < 1.0f ? 1.0f : thickness;
    pd_draw_rect(x, y, w, t, color);
    pd_draw_rect(x, y + h - t, w, t, color);
    if (h > 2.0f * t) {
        pd_draw_rect(x, y + t, t, h - 2.0f * t, color);
        pd_draw_rect(x + w - t, y + t, t, h - 2.0f * t, color);
    }
}

void pd_draw_circle(float cx, float cy, float radius, uint32_t color) {
    if (radius <= 0.0f) {
        return;
    }
    int x0 = (int)floorf(cx - radius);
    int y0 = (int)floorf(cy - radius);
    int x1 = (int)ceilf(cx + radius);
    int y1 = (int)ceilf(cy + radius);
    float r2 = radius * radius;

    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx = ((float)px + 0.5f) - cx;
            float dy = ((float)py + 0.5f) - cy;
            if (dx * dx + dy * dy <= r2) {
                blend_pixel(px, py, color);
            }
        }
    }
}

void pd_draw_line(float x1, float y1, float x2, float y2, float thickness, uint32_t color) {
    if (thickness <= 0.0f) {
        return;
    }
    float dx = x2 - x1;
    float dy = y2 - y1;
    float steps = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    if (steps < 1.0f) {
        steps = 1.0f;
    }
    int n_steps = (int)ceilf(steps);
    int half_t = (int)(thickness * 0.5f);

    for (int i = 0; i <= n_steps; i++) {
        float t = (float)i / (float)n_steps;
        int px = (int)roundf(x1 + dx * t);
        int py = (int)roundf(y1 + dy * t);
        for (int oy = -half_t; oy <= half_t; oy++) {
            for (int ox = -half_t; ox <= half_t; ox++) {
                blend_pixel(px + ox, py + oy, color);
            }
        }
    }
}

void pd_draw_text(float x, float y, float scale, uint32_t color, const char *text) {
    if (text == NULL || text[0] == '\0') {
        return;
    }
    int px_scale = (int)roundf(scale * 1.6f);
    if (px_scale < 1) {
        px_scale = 1;
    }
    int cur_x = (int)roundf(x);
    int base_y = (int)roundf(y);

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            cur_x = (int)roundf(x);
            base_y += 8 * px_scale;
            continue;
        }
        uint8_t cols[5];
        get_glyph_5x7(text[i], cols);
        for (int col = 0; col < 5; col++) {
            uint8_t bits = cols[col];
            for (int row = 0; row < 7; row++) {
                if ((bits >> row) & 1u) {
                    for (int sy = 0; sy < px_scale; sy++) {
                        for (int sx = 0; sx < px_scale; sx++) {
                            blend_pixel(cur_x + col * px_scale + sx,
                                        base_y + row * px_scale + sy,
                                        color);
                        }
                    }
                }
            }
        }
        cur_x += 6 * px_scale;
    }
}

const uint32_t *pd_platform_host_get_framebuffer(pd_render_target_t target) {
    if (target == PD_TARGET_TOP_LEFT) {
        return fb_top_left;
    }
    if (target == PD_TARGET_TOP_RIGHT) {
        return fb_top_right;
    }
    return fb_bot;
}

void pd_platform_host_inject_input(const pd_input_t *input) {
    if (input != NULL) {
        g_injected_input = *input;
    }
}

void pd_platform_host_queue_prompt_response(const char *text) {
    if (text != NULL) {
        snprintf(g_prompt_response, sizeof(g_prompt_response), "%s", text);
        g_has_prompt_response = true;
    } else {
        g_prompt_response[0] = '\0';
        g_has_prompt_response = false;
    }
}

#else
typedef int pd_platform_host_unused_t;
#endif /* !__3DS__ */
