#ifndef __VIDEO_SHOW_H
#define __VIDEO_SHOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 视频数据结构
 *
 * 所有帧共用同一张调色板（省空间），每帧是一张「位打包索引图」，
 * 和图片用的是同一套 4bpp 打包规则（行对齐、高位优先）。
 *
 * 数据由 tools/video2c.py 从 mp4 生成，放在 video_data.c 里，
 * 用 const 修饰所以整段在 Flash 上，不占 RAM。
 * ============================================================ */
typedef struct {
    uint16_t        w;        /* 帧宽，像素 */
    uint16_t        h;        /* 帧高，像素 */
    uint16_t        frames;   /* 总帧数 */
    uint8_t         fps;      /* 播放帧率 */
    uint8_t         bpp;      /* 每像素位数，1/2/4/8 */
    uint8_t         colors;   /* 调色板颜色数 */
    const uint16_t *palette;  /* colors 项 RGB565 */
    const uint8_t  *data;     /* frames 帧连续存放 */
} video_t;

/* 从头完整播放一遍，播完返回 */
void video_play_once(const video_t *v);

/* 循环播放，永不返回 */
void video_play_loop(const video_t *v);

/* 只播某一帧（调试 / 定格用） */
void video_show_frame(const video_t *v, uint16_t index);

/* 一帧占多少字节 */
uint32_t video_frame_bytes(const video_t *v);

#ifdef __cplusplus
}
#endif

#endif /* __VIDEO_SHOW_H */
