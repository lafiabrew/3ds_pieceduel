#ifndef PD_PLATFORM_H
#define PD_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PD_TOP_WIDTH 400
#define PD_TOP_HEIGHT 240
#define PD_BOT_WIDTH 320
#define PD_BOT_HEIGHT 240

/* Portable button bitmasks (mapped from libctru KEY_* on 3DS) */
#define PD_BTN_A      (1u << 0)
#define PD_BTN_B      (1u << 1)
#define PD_BTN_SELECT (1u << 2)
#define PD_BTN_START  (1u << 3)
#define PD_BTN_DRIGHT (1u << 4)
#define PD_BTN_DLEFT  (1u << 5)
#define PD_BTN_DUP    (1u << 6)
#define PD_BTN_DDOWN  (1u << 7)
#define PD_BTN_R      (1u << 8)
#define PD_BTN_L      (1u << 9)
#define PD_BTN_X      (1u << 10)
#define PD_BTN_Y      (1u << 11)

typedef enum {
    PD_TARGET_TOP_LEFT = 0,
    PD_TARGET_TOP_RIGHT = 1,
    PD_TARGET_BOTTOM = 2
} pd_render_target_t;

typedef struct {
    uint32_t down;
    uint32_t held;
    uint32_t up;
    bool touching;
    bool touch_just_pressed;
    int touch_x;
    int touch_y;
    float slider_3d; /* 0.0f .. 1.0f */
} pd_input_t;

#define PD_RGBA8(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

/* Platform lifecycle & I/O */
bool pd_platform_init(void);
void pd_platform_shutdown(void);
bool pd_platform_main_loop(void);
void pd_platform_poll_input(pd_input_t *out_input);
void pd_platform_get_utc_date(char out_iso_date[11]);
uint64_t pd_platform_ticks_ms(void);
const char *pd_platform_default_save_path(void);
bool pd_platform_prompt_text(const char *hint, char *inout_buf, size_t max_len, bool is_password);

/* Rendering primitives (citro2d on 3DS, software framebuffer on host) */
void pd_render_begin_frame(void);
void pd_render_begin_target(pd_render_target_t target, uint32_t clear_color);
void pd_render_end_frame(void);

void pd_draw_rect(float x, float y, float w, float h, uint32_t color);
void pd_draw_rect_outline(float x, float y, float w, float h, float thickness, uint32_t color);
void pd_draw_circle(float cx, float cy, float radius, uint32_t color);
void pd_draw_line(float x1, float y1, float x2, float y2, float thickness, uint32_t color);
void pd_draw_text(float x, float y, float scale, uint32_t color, const char *text);

/* Host test inspection helpers */
const uint32_t *pd_platform_host_get_framebuffer(pd_render_target_t target);
void pd_platform_host_inject_input(const pd_input_t *input);
void pd_platform_host_queue_prompt_response(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* PD_PLATFORM_H */
