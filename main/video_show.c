#include "video_show.h"
#include "ili9341.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "video";

uint32_t video_frame_bytes(const video_t *v)
{
    uint32_t row;
    if (v == NULL) {
        return 0;
    }
    row = ((uint32_t)v->w * v->bpp + 7u) / 8u;
    return row * v->h;
}

void video_show_frame(const video_t *v, uint16_t index)
{
    const uint8_t *p;

    if (v == NULL || v->data == NULL || index >= v->frames) {
        return;
    }
    p = v->data + (uint32_t)index * video_frame_bytes(v);
    LCD_ShowPacked(0, 0, v->w, v->h, v->palette, p, v->bpp);
}

void video_play_once(const video_t *v)
{
    uint32_t fb;
    uint16_t i;
    uint32_t t0;
    TickType_t period;

    if (v == NULL || v->data == NULL || v->frames == 0) {
        return;
    }

    fb = video_frame_bytes(v);
    period = pdMS_TO_TICKS(v->fps ? (1000u / v->fps) : 100u);

    ESP_LOGI(TAG, "play %ux%u %u frames @%ufps, %u bytes/frame, total %u KB",
             v->w, v->h, v->frames, v->fps, (unsigned)fb,
             (unsigned)((fb * v->frames) / 1024u));

    t0 = xTaskGetTickCount();
    for (i = 0; i < v->frames; i++) {
        LCD_ShowPacked(0, 0, v->w, v->h, v->palette,
                       v->data + (uint32_t)i * fb, v->bpp);

        /* 用「按时间轴排程」而不是「每帧固定 delay」，
         * 这样即使某一帧画得慢，后面的帧也不会累积延迟，音画不同步的问题会小很多 */
        {
            TickType_t due = t0 + (TickType_t)(i + 1) * period;
            TickType_t now = xTaskGetTickCount();
            if (due > now) {
                vTaskDelay(due - now);
            }
        }
    }

    ESP_LOGI(TAG, "play done, %u frames", v->frames);
}

void video_play_loop(const video_t *v)
{
    while (1) {
        video_play_once(v);
    }
}
