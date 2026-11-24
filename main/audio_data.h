/* 由 tools/audio2c.py 自动生成，请勿手工修改 */
#ifndef __AUDIO_DATA_H
#define __AUDIO_DATA_H

#include <stdint.h>

typedef struct {
    uint32_t       sample_rate;   /* 采样率 Hz */
    uint32_t       samples;       /* 样本个数（单声道 16bit） */
    const int16_t *data;          /* 指向固件里内嵌的 PCM */
} audio_t;

/* 16000Hz · 单声道 · 16bit · 159992 个样本 · 10.00 秒 */
extern const audio_t AUDIO_DEMO;

#endif /* __AUDIO_DATA_H */
