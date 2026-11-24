/* 由 tools/video2c.py 自动生成，请勿手工修改
 * 源文件: c135393a5849ad0b8ceed417ed89b6a7.mp4
 * 尺寸 240x320 · 8fps · 16色/4bpp · 共 80 帧 · 3,072,000 字节
 *
 * 帧数据没有写成 C 数组，而是放在同目录的 video_data.bin 里，
 * 由 CMakeLists.txt 的 EMBED_FILES 直接嵌进固件。
 * 原因：3,072,000 字节写成 0x.. 文本要 18MB 的 .c 文件，
 * 编辑器打开会卡死，编译器也要多花好几分钟解析。
 * 想换视频：重跑 tools/video2c.py 覆盖 video_data.bin 即可。
 */
#include "video_data.h"

/* 这两个符号由链接器根据 EMBED_FILES 自动生成，指向 .bin 的首尾 */
extern const uint8_t video_data_bin_start[] asm("_binary_video_data_bin_start");
extern const uint8_t video_data_bin_end[]   asm("_binary_video_data_bin_end");

/* 全局调色板：16 项 RGB565 */
static const uint16_t VID_DEMO_PALETTE[16] = {
    0xEF7E,0xE738,0xDE97,0xC67A,0xC5F6,0xC553,0xA535,0x94D5,
    0x8BCF,0x6B4E,0x52ED,0x4A6B,0x3A2A,0x29A8,0x2145,0x10C3
};

const video_t VID_DEMO = {
    /* w, h, frames, fps, bpp, colors */
    240, 320, 80, 8, 4, 16,
    VID_DEMO_PALETTE,
    video_data_bin_start
};
