#ifndef __ILI9341_H
#define __ILI9341_H

#include <stdint.h>

#define LCD_WIDTH   240
#define LCD_HEIGHT  320

#define LCD_RGB(r, g, b)  \
    (uint16_t)(((uint16_t)(r) >> 3) << 11 | \
               ((uint16_t)(g) >> 2) << 5 |  \
               ((uint16_t)(b) >> 3))

void ili9341_init(void);
void ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void ili9341_flush(const uint8_t *data, uint32_t len);
void ili9341_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ili9341_fill_screen(uint16_t color);

/* 画一行任意颜色像素。
 * be_bytes 必须是「高字节在前」的 RGB565 字节流，长度 = w * 2。
 * 屏幕要求高字节先发，所以 uint16_t 数组必须转成字节序再传，
 * 不能直接把 uint16_t* 强转过来（小端芯片会红蓝互换）。 */
void ili9341_draw_row(uint16_t x, uint16_t y, uint16_t w, const uint8_t *be_bytes);

/* ============================================================================
 * 以下是「图片转数组网站」约定的接口，函数名/参数顺序必须一模一样，
 * 网站上生成的头文件（image_avatar.h）里那句 IMG_Draw / main.c 的调用
 * 才能直接编译通过。改名前先想一想会不会让网站生成的代码失效。
 *
 * 数据格式统一是：RGB565 调色板 palette[colors] + 每像素颜色索引。
 * 索引可以按 bpp 位打包（1/2/4 位），也可以 1 字节 1 像素（8 位）。
 * ========================================================================== */

/* 显示一张索引图（1 字节 1 像素，8bpp / 256 色）。
 * 网站头文件里的 IMG_Draw() 解码后就是调这个函数写屏。 */
void LCD_ShowImage256(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint16_t *palette, const uint8_t *indices);

/* 显示一张索引图，并把 sw*sh 的源图最近邻缩放到 dw*dh 输出。
 * 网站 8bpp 档生成的头文件用的就是这一句：
 *   LCD_ShowImage256Fit(IMG_AVATAR_X, IMG_AVATAR_Y, IMG_AVATAR_W, IMG_AVATAR_H,
 *                       IMG_AVATAR_DW, IMG_AVATAR_DH, IMG_AVATAR_PALETTE, IMG_AVATAR);
 */
void LCD_ShowImage256Fit(uint16_t x, uint16_t y,
                         uint16_t sw, uint16_t sh,
                         uint16_t dw, uint16_t dh,
                         const uint16_t *palette, const uint8_t *indices);

/* 显示一张「按 bpp 位打包」的索引图（bpp = 1/2/4/8），行对齐、高位优先。
 * 视频播放器逐帧调这个函数，一次开窗整帧发完，比逐行调用快一个数量级。 */
void LCD_ShowPacked(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    const uint16_t *palette, const uint8_t *packed, uint8_t bpp);

#endif
