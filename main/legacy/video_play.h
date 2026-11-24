#ifndef __VIDEO_PLAY_H
#define __VIDEO_PLAY_H

#include <stdint.h>

#define VFRAME_FLAG_LITERAL  0x80

typedef struct {
    const uint16_t *palette;
    uint16_t data_len;
    const uint8_t *data;
} VFrame;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t scale_x;
    uint16_t scale_y;
    uint16_t frame_count;
    uint16_t fps;
    const VFrame *frames;
} Video;

void video_init(const Video *v);
void video_play_once(void);
void video_play_loop(uint32_t loops);

#endif
