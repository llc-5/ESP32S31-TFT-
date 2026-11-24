#include "image.h"
#include "ili9341.h"
#include "esp_log.h"

static const char *TAG = "image";

/* 行缓冲：LCD_WIDTH 像素 x 2 字节 = 480 字节，静态分配不占栈 */
static uint8_t s_line[LCD_WIDTH * 2];

void image_size(const img_t *img, uint8_t scale, uint16_t *w, uint16_t *h)
{
    if (scale == 0) {
        scale = 1;
    }
    if (w) {
        *w = (uint16_t)(img->w * scale);
    }
    if (h) {
        *h = (uint16_t)(img->h * scale);
    }
}

void image_draw(const img_t *img, uint16_t x, uint16_t y, uint8_t scale)
{
    uint16_t out_w;
    uint16_t sy;
    uint16_t k;

    if (img == NULL || img->px == NULL || img->w == 0 || img->h == 0) {
        ESP_LOGE(TAG, "bad image handle");
        return;
    }
    if (scale == 0) {
        scale = 1;
    }
    if (y >= LCD_HEIGHT || x >= LCD_WIDTH) {
        return;
    }

    /* 宽度超出屏幕时自动降倍率，避免越界 */
    while ((uint32_t)img->w * scale > LCD_WIDTH && scale > 1) {
        scale--;
    }
    out_w = (uint16_t)(img->w * scale);
    if (x + out_w > LCD_WIDTH) {
        out_w = (uint16_t)(LCD_WIDTH - x);
    }

    for (sy = 0; sy < img->h; sy++) {
        const uint16_t *src = img->px + (uint32_t)sy * img->w;
        uint32_t o = 0;
        uint16_t sx;

        /* 1) 把这一行源像素按倍率展开成显示字节流（高字节在前） */
        for (sx = 0; sx < img->w; sx++) {
            uint16_t c = src[sx];
            uint8_t hi = (uint8_t)(c >> 8);
            uint8_t lo = (uint8_t)(c & 0xFF);
            uint16_t rep;
            for (rep = 0; rep < scale; rep++) {
                s_line[o++] = hi;
                s_line[o++] = lo;
                if (o >= sizeof(s_line)) {
                    break;
                }
            }
            if (o >= sizeof(s_line)) {
                break;
            }
        }

        /* 2) 纵向重复 scale 次 */
        for (k = 0; k < scale; k++) {
            uint16_t dy = (uint16_t)(y + sy * scale + k);
            if (dy >= LCD_HEIGHT) {
                return;
            }
            ili9341_draw_row(x, dy, out_w, s_line);
        }
    }
}

void image_draw_center(const img_t *img, uint16_t ax, uint16_t ay,
                       uint16_t aw, uint16_t ah)
{
    uint8_t scale = 1;
    uint16_t w;
    uint16_t h;

    if (img == NULL || img->w == 0 || img->h == 0) {
        return;
    }

    /* 找能完整放进 aw x ah 的最大整数倍率 */
    while ((uint32_t)img->w * (scale + 1) <= aw &&
           (uint32_t)img->h * (scale + 1) <= ah) {
        scale++;
    }

    image_size(img, scale, &w, &h);
    image_draw(img,
               (uint16_t)(ax + (aw - w) / 2),
               (uint16_t)(ay + (ah - h) / 2),
               scale);
}
