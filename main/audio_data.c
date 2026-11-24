/* 由 tools/audio2c.py 自动生成，请勿手工修改
 * 源文件: c135393a5849ad0b8ceed417ed89b6a7.mp4
 * 16000Hz 单声道 16bit · 159992 个样本 · 10.00 秒 · 319,984 字节
 *
 * PCM 裸数据放在同目录 audio_data.bin，由 CMakeLists.txt 的
 * EMBED_FILES 直接嵌进固件（和视频一个套路）。
 */
#include "audio_data.h"

extern const uint8_t audio_data_bin_start[] asm("_binary_audio_data_bin_start");

const audio_t AUDIO_DEMO = {
    16000,          /* sample_rate */
    159992,        /* samples */
    (const int16_t *)audio_data_bin_start,
};
