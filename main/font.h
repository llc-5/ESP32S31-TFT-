#ifndef __FONT_H
#define __FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * RGB 颜色宏 —— 你直接改这三个数字就能换颜色
 *
 *   RGB(r, g, b)   三个分量都是 0 ~ 255
 *
 *   常用颜色：
 *     RGB(255,0,0)    红
 *     RGB(0,255,0)    绿
 *     RGB(0,0,255)    蓝
 *     RGB(255,255,0)  黄
 *     RGB(255,0,255)  品红
 *     RGB(0,255,255)  青
 *     RGB(255,255,255) 白
 *     RGB(0,0,0)      黑
 *     RGB(255,128,0)  橙
 *     RGB(128,0,255)  紫
 *
 * 原理：屏幕要 16 位色，红 5 位、绿 6 位、蓝 5 位。
 * 这里把 8 位的分量截掉低位拼成 RGB565，所以颜色会有极轻微误差，
 * 肉眼看不出，属正常。
 * ============================================================ */
#define RGB(r, g, b)  ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | \
                                  (((uint16_t)(g) & 0xFCu) << 3) | \
                                   ((uint16_t)(b) >> 3)))

/* 字模尺寸：宽 8、高 16。放大 N 倍就是 (8*N) x (16*N) 像素 */
#define FONT_W  8
#define FONT_H  16

/* 画一个字符，左上角 (x, y)
 *   c     : 字符，支持 '0'~'9'、'A'~'Z'、空格（其他字符按空格处理）
 *   fg    : 字的颜色（RGB565，用 RGB() 宏）
 *   bg    : 字的底色（RGB565）。想让字"透明"就直接传屏幕背景色
 *   scale : 放大倍数，1 = 8x16，2 = 16x32，4 = 32x64 */
void font_draw_char(uint16_t x, uint16_t y, char c,
                    uint16_t fg, uint16_t bg, uint8_t scale);

/* 画一串字符，从 (x, y) 开始往右排 */
void font_draw_text(uint16_t x, uint16_t y, const char *s,
                    uint16_t fg, uint16_t bg, uint8_t scale);

/* 算一串字符在屏上占多宽（用来居中） */
uint16_t font_text_width(const char *s, uint8_t scale);

#ifdef __cplusplus
}
#endif

#endif /* __FONT_H */
