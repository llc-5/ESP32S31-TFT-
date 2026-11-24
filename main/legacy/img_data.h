#ifndef __IMG_DATA_H
#define __IMG_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 图片数据结构
 *
 * 每张图片就是一个 img_t：宽、高、指向 RGB565 像素数组的指针。
 * 像素数组用 const 修饰，编译器会把它放进 Flash（.rodata），
 * 不占 RAM —— 所以多少张图都不会吃内存，只吃 Flash 空间。
 *
 * 想加新图：用 tools/img2c.py 生成一个 .c 文件，在这里 extern
 * 声明一下，再加进 CMakeLists 的 SRCS 就行了。
 * ============================================================ */
typedef struct {
    uint16_t        w;      /* 宽（像素） */
    uint16_t        h;      /* 高（像素） */
    const uint16_t *px;     /* RGB565 数据，行优先，从左到右从上到下 */
} img_t;

/* ---------- 已注册的图片 ---------- */

/* 48x48 圆形头像（源图：屏幕截图 2026-11-06 152459.png） */
extern const img_t IMG_AVATAR;

#ifdef __cplusplus
}
#endif

#endif /* __IMG_DATA_H */
