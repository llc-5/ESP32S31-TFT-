#ifndef __IMAGE_H
#define __IMAGE_H

#include <stdint.h>
#include "img_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 图片绘制库
 *
 * 只依赖 ili9341 驱动，不占大块 RAM：
 * 内部只有一条 LCD_WIDTH 像素宽的临时行缓冲（480 字节）。
 * ============================================================ */

/* 以左上角 (x, y) 为起点绘制图片，整数倍放大 scale 倍（1=原始大小）。
 * 放大用最近邻，像素画放大后边缘是硬的，不会糊。
 * 超出屏幕右边或下边的部分自动裁掉，不会越界。 */
void image_draw(const img_t *img, uint16_t x, uint16_t y, uint8_t scale);

/* 把图片画在指定区域内居中（区域左上角 ax,ay，宽 aw，高 ah）。
 * 会自动选一个能放下的最大整数倍率；放不下就按 1 倍裁切。 */
void image_draw_center(const img_t *img, uint16_t ax, uint16_t ay,
                       uint16_t aw, uint16_t ah);

/* 计算某个倍率下图片占用的宽/高（方便排版） */
void image_size(const img_t *img, uint8_t scale, uint16_t *w, uint16_t *h);

#ifdef __cplusplus
}
#endif

#endif /* __IMAGE_H */
