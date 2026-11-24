/* ============================================================================
 *  i2s_sound.h —— I2S 音频输出（让视频带上声音）
 *
 *  硬件前提：ESP32-S31 芯片本身没有模拟 DAC，不能直接接喇叭。
 *  必须外接一个 I2S 数字功放模块，最常见两种都能用，接法一样：
 *
 *    ┌ MAX98357A（3W 单声道，最常见，十几块）
 *    │   VIN -> 3V3        GND -> GND
 *    │   LRC -> 本文件 SOUND_PIN_WS
 *    │   BCLK-> 本文件 SOUND_PIN_BCLK
 *    │   DIN -> 本文件 SOUND_PIN_DOUT
 *    │   GAIN-> 悬空（默认 9dB）        SD -> 悬空（默认左声道）
 *    │   喇叭接 + / -
 *    │
 *    └ PCM5102A（音质好，要接 5 个脚）
 *        VIN -> 5V/3V3     GND -> GND
 *        LRCK-> SOUND_PIN_WS     BCK -> SOUND_PIN_BCLK
 *        DIN -> SOUND_PIN_DOUT   SCK -> GND（让它自己从 BCK 恢复时钟）
 *        FMT -> GND    DEMP -> GND    XSMT -> 3V3（静音脚，必须拉高）
 *
 *  ※ 只有 PCM5102 那种才需要 MCLK；把 SOUND_PIN_MCLK 填上脚号即可，
 *    MAX98357A 不需要 MCLK，保持 I2S_GPIO_UNUSED。
 *
 *  不想出声 / 手头没模块：把 main.c 里的 STEP_SOUND 改成 0，
 *  整个音频就不会初始化，也不会占用任何 GPIO。
 * ========================================================================== */
#ifndef __I2S_SOUND_H
#define __I2S_SOUND_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "audio_data.h"

/* ============================================================================
 * 【配置区】音频模块接在哪几个 GPIO —— 换成你自己的脚号
 *
 * 你屏幕已经占了 40(CS) 42(RES) 43(DC) 44(SDI) 45(SCK)，
 * 剩下的 46 / 47 / 48 正好够接一个 I2S 模块，默认就按这个排的。
 * 三个脚必须是不重复、且没被别的东西占用的 GPIO。
 *
 * ⚠ ESP32-S31 的 GPIO 编号不是连着的：只有 0~28、30~40、42~61，
 *   没有 GPIO 29，也没有 GPIO 41。写 41 会直接编译报
 *   "GPIO_NUM_41 undeclared"，别踩这个坑。
 * ========================================================================== */
#define SOUND_PIN_BCLK    GPIO_NUM_47    /* 位时钟  -> 模块 BCLK / BCK */
#define SOUND_PIN_WS      GPIO_NUM_48    /* 声道时钟 -> 模块 LRC / LRCK */
#define SOUND_PIN_DOUT    GPIO_NUM_46    /* 数据    -> 模块 DIN */
#define SOUND_PIN_MCLK    I2S_GPIO_UNUSED /* 主时钟：MAX98357A 不接；PCM5102 可填脚号 */

/* 播放任务跑在哪个核、什么优先级。
   ESP32-S31 是双核：视频刷屏在 app_main（核 0），
   音频单独丢到核 1，这样 SPI 连续发 15 万字节时不会把声音饿出杂音。 */
#define SOUND_TASK_CORE   1
#define SOUND_TASK_PRIO   6
#define SOUND_TASK_STACK  4096

/* ============================================================================
 * 接口
 * ========================================================================== */

/* 初始化 I2S（按给定采样率）。重复调用是安全的；采样率变了会自动重建。
   返回 true = 成功。 */
bool sound_init(uint32_t sample_rate_hz);

/* 异步播放一段 PCM，立刻返回，不阻塞调用者。
   适合"和视频同时开始"：先调这个，紧接着去刷视频帧。
   返回 false = 初始化失败。 */
bool sound_play_async(const audio_t *a);

/* 同步播放（一直阻塞到播完）。单独放一段提示音时用这个。 */
bool sound_play_blocking(const audio_t *a);

/* 立刻停止播放（会打断正在进行的播放）。 */
void sound_stop(void);

/* 当前是否正在出声。 */
bool sound_is_playing(void);

#endif /* __I2S_SOUND_H */
