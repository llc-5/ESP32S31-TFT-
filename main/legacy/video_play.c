#include "video_play.h"
#include "ili9341.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const Video *g_video = NULL;
static uint8_t *g_fb = NULL;   // 整帧 RGB565 缓冲（高字节在前）

static uint32_t decode_frame(const VFrame *f)
{
    uint16_t w = g_video->width;
    uint16_t h = g_video->height;
    uint16_t sx = g_video->scale_x;
    uint16_t sy = g_video->scale_y;
    uint16_t x = 0, y = 0;
    const uint8_t *p = f->data;
    const uint8_t *end = p + f->data_len;
    uint32_t fb_idx = 0;

    while (p < end && y < h) {
        uint8_t b = *p++;

        if (b & VFRAME_FLAG_LITERAL) {
            uint16_t n = (uint16_t)((b & 0x7F) + 1);
            if (p + n > end) {
                n = (uint16_t)(end - p);
            }
            while (n-- && y < h) {
                uint8_t idx = (uint8_t)(*p++ & 0x07);
                uint16_t color = f->palette[idx];
                uint16_t ry, rx;
                for (ry = 0; ry < sy; ry++) {
                    for (rx = 0; rx < sx; rx++) {
                        g_fb[fb_idx++] = (uint8_t)(color >> 8);
                        g_fb[fb_idx++] = (uint8_t)(color & 0xFF);
                    }
                }
                x++;
                if (x >= w) {
                    x = 0;
                    y++;
                }
            }
        } else {
            uint16_t n = (uint16_t)((b & 0x7F) + 1);
            uint8_t idx;
            if (p >= end) {
                break;
            }
            idx = (uint8_t)(*p++ & 0x07);
            {
                uint16_t color = f->palette[idx];
                uint8_t hi = (uint8_t)(color >> 8);
                uint8_t lo = (uint8_t)(color & 0xFF);
                while (n-- && y < h) {
                    uint16_t ry, rx;
                    for (ry = 0; ry < sy; ry++) {
                        for (rx = 0; rx < sx; rx++) {
                            g_fb[fb_idx++] = hi;
                            g_fb[fb_idx++] = lo;
                        }
                    }
                    x++;
                    if (x >= w) {
                        x = 0;
                        y++;
                    }
                }
            }
        }
    }
    return fb_idx;
}

void video_init(const Video *v)
{
    g_video = v;
    if (g_fb == NULL) {
        g_fb = (uint8_t *)malloc((size_t)LCD_WIDTH * LCD_HEIGHT * 2);
    }
}

void video_play_once(void)
{
    uint16_t f;
    uint32_t frame_ms;
    uint32_t total_px = (uint32_t)LCD_WIDTH * LCD_HEIGHT * 2;

    if (g_video == NULL || g_video->frame_count == 0) {
        return;
    }
    frame_ms = (g_video->fps > 0) ? (1000U / g_video->fps) : 100U;

    for (f = 0; f < g_video->frame_count; f++) {
        decode_frame(&g_video->frames[f]);
        ili9341_flush(g_fb, total_px);
        vTaskDelay(pdMS_TO_TICKS(frame_ms));
    }
}

void video_play_loop(uint32_t loops)
{
    while (loops--) {
        video_play_once();
    }
}
