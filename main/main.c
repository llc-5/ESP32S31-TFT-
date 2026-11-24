/* ============================================================================
 *  ESP32-S31 + ILI9341 (240x320) —— 只做两件事：显示图片、播放视频
 *
 *  ┌──────────────────────────────────────────────────────────────────────┐
 *  │  显示图片   ->  demo_show_image()   （image_avatar.h，你的网站生成）   │
 *  │  播放视频   ->  demo_show_video()   （video_data.c + audio_data.c）    │
 *  └──────────────────────────────────────────────────────────────────────┘
 *
 *  ※ image_avatar.h 是你替换过的图片库，本文件只 include 它、按它给的宏调用，
 *    一个字节都不改。要换图片，直接用网站重新生成这个头文件整文件替换即可。
 *
 *  app_main() 里每一行调用上方都有注释，标出这一行是干嘛的。
 * ========================================================================== */

#include "esp_log.h"
#include "ili9341.h"
#include "font.h"            /* 只为了拿 RGB(r,g,b) 颜色宏 */
#include "image_avatar.h"    /* ← 你的图片库（网站生成，自带 IMG_Draw 解码） */
#include "video_data.h"      /* 视频画面（tools/video2c.py 生成） */
#include "video_show.h"
#include "audio_data.h"      /* 视频音轨（tools/audio2c.py 生成） */
#include "i2s_sound.h"       /* I2S 音频输出，让视频出声 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "APP";

/* ============================================================================
 * 【配置区 1】颜色 —— 全部用 RGB(r, g, b)，三个分量 0~255
 * ========================================================================== */
#define COLOR_BG         RGB(0,   0,   0)     /* 全屏背景色（黑） */
#define COLOR_IMAGE_BG   RGB(0,   0,   0)     /* 显示图片时的底色（黑） */

/* 颜色自检：开机先闪红→绿→蓝各 150ms。
 * 闪了说明 SPI 通了；一直是白屏说明数据没进屏（查接线），
 * 跟后面的图片/视频代码没关系。不想看闪烁就把这里改成 0。 */
#define STEP_SELFTEST    1

/* ============================================================================
 * 【配置区 2】要跑哪一段 —— 1 = 跑，0 = 跳过
 * ========================================================================== */
#define STEP_IMAGE       1    /* ← 显示图片 */
#define STEP_VIDEO       1    /* ← 播放视频（一直循环） */
#define STEP_SOUND       1    /* ← 视频带声音（没接 I2S 功放模块就改 0） */

/* ============================================================================
 * 下面是具体实现，一般不用动
 * ========================================================================== */

/* ----------------------------------------------------------------------------
 * 把「当前编译进固件的到底是哪张图」打到串口上
 *
 * 换了 image_avatar.h 之后，最怕的就是"以为换了、其实没换"——
 * 文件没保存、存到别的目录、或者编译没重跑，表现都是屏幕还是老图。
 * 这里把源图尺寸、屏上尺寸、色数、数据字节数、校验和全部打出来，
 * 烧完看一眼串口就知道里面装的是哪一份，不用靠猜。
 * -------------------------------------------------------------------------- */
static void log_image_info(void)
{
#if defined(IMG_AVATAR_BPP) && defined(IMG_AVATAR_COLORS) && defined(IMG_AVATAR_BYTES)
    ESP_LOGI(TAG, "image: 源图 %dx%d -> 屏上 %dx%d, %d色/%dbpp, %d 字节",
             IMG_AVATAR_W, IMG_AVATAR_H,
             IMG_AVATAR_DW, IMG_AVATAR_DH,
             IMG_AVATAR_COLORS, IMG_AVATAR_BPP, IMG_AVATAR_BYTES);
#else
    ESP_LOGI(TAG, "image: 源图 %dx%d -> 屏上 %dx%d",
             IMG_AVATAR_W, IMG_AVATAR_H, IMG_AVATAR_DW, IMG_AVATAR_DH);
#endif

    /* 4bpp 位打包格式才有 IMG_AVATAR_PACKED，算个校验和方便你比对 */
#if defined(IMG_AVATAR_BPP) && (IMG_AVATAR_BPP == 4) && defined(IMG_AVATAR_PACKED_BYTES)
    {
        uint32_t sum = 2166136261u;
        uint32_t i;
        for (i = 0; i < (uint32_t)IMG_AVATAR_PACKED_BYTES; i++) {
            sum = (sum ^ IMG_AVATAR_PACKED[i]) * 16777619u;
        }
        ESP_LOGI(TAG, "image: 数据校验和 0x%08lX（换了图这个值一定会变）",
                 (unsigned long)sum);
    }
#endif
}

/* ----------------------------------------------------------------------------
 * 【1】显示图片
 *
 * 这一句就是网站约定的标准调用，参数意义：
 *   IMG_AVATAR_X / Y         图片左上角在屏幕上的坐标
 *   IMG_AVATAR_W / H         源图尺寸（存在 Flash 里的那份数据）
 *   IMG_AVATAR_DW / DH       屏上显示尺寸（驱动做最近邻放大）
 *   IMG_AVATAR_PALETTE       调色板（RGB565）
 *   IMG_AVATAR               索引数据
 *
 * 16 色/4 色/2 色档位时，你的头文件末尾会把这一句用宏接到它自带的 IMG_Draw()，
 * 所以不管是 256 色档还是 16 色档，写法都是这一句，不用分情况。
 * -------------------------------------------------------------------------- */
static void demo_show_image(void)
{
    /* 先把自己编进去的是哪张图报出来（烧完看串口就知道换没换成功） */
    log_image_info();

    /* 先把背景刷成底色，避免上一段画面残留 */
    ili9341_fill_screen(COLOR_IMAGE_BG);

    /* ↓↓↓ 这一行 = 把图片画到屏幕上 ↓↓↓ */
    LCD_ShowImage256Fit(IMG_AVATAR_X, IMG_AVATAR_Y,
                        IMG_AVATAR_W, IMG_AVATAR_H,
                        IMG_AVATAR_DW, IMG_AVATAR_DH,
                        IMG_AVATAR_PALETTE, IMG_AVATAR);
}

/* ----------------------------------------------------------------------------
 * 【2】播放视频（带声音）
 *
 * 画面：video_data.c，tools/video2c.py 从 mp4 转出来的
 *   python tools/video2c.py 你的视频.mp4 --fps 8 --duration 10
 * 声音：audio_data.c，tools/audio2c.py 从同一个 mp4 抽出来的
 *   python tools/audio2c.py 你的视频.mp4 --duration 10 --rate 16000
 *
 * 每轮循环：
 *   1. sound_play_async()  起后台任务喂 I2S DMA，立刻返回
 *   2. video_play_once()   主任务一帧一帧刷屏，约 10 秒
 *   3. 等声音播完，避免两轮叠在一起
 * 音频在核 1、刷屏在核 0，I2S 与 SPI 各有自己的 DMA，互不拖累。
 * -------------------------------------------------------------------------- */
static void demo_show_video(void)
{
    ESP_LOGI(TAG, "video: %ux%u %u frames @%ufps, %u colors, %u bytes/frame",
             VID_DEMO.w, VID_DEMO.h, VID_DEMO.frames,
             VID_DEMO.fps, VID_DEMO.colors, video_frame_bytes(&VID_DEMO));

#if STEP_SOUND
    ESP_LOGI(TAG, "sound: %luhz mono 16bit, %.2f s",
             (unsigned long)AUDIO_DEMO.sample_rate,
             (double)AUDIO_DEMO.samples / AUDIO_DEMO.sample_rate);
#endif

    while (1) {
#if STEP_SOUND
        /* ↓↓↓ 这一行 = 开始播放视频的声音（非阻塞）↓↓↓ */
        sound_play_async(&AUDIO_DEMO);
#endif

        /* ↓↓↓ 这一行 = 播放一遍视频画面 ↓↓↓ */
        video_play_once(&VID_DEMO);

#if STEP_SOUND
        /* 声音万一比画面长，等它收尾再进下一轮 */
        while (sound_is_playing()) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
#endif
    }
}

/* ----------------------------------------------------------------------------
 * 入口
 * -------------------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "FIRMWARE BUILD: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "========================================");

    ili9341_init();

#if STEP_SELFTEST
    /* 红→绿→蓝 各 150ms，用来确认 SPI 通路正常 */
    ili9341_fill_screen(RGB(255, 0, 0));
    vTaskDelay(pdMS_TO_TICKS(150));
    ili9341_fill_screen(RGB(0, 255, 0));
    vTaskDelay(pdMS_TO_TICKS(150));
    ili9341_fill_screen(RGB(0, 0, 255));
    vTaskDelay(pdMS_TO_TICKS(150));
#endif

#if STEP_IMAGE
    /* ↓↓↓ 这一行 = 显示图片 ↓↓↓ */
    ESP_LOGI(TAG, "step: IMAGE");
    demo_show_image();
    vTaskDelay(pdMS_TO_TICKS(3000));     /* 图片停在屏上 3 秒，方便看 */
#endif

#if STEP_VIDEO
    /* ↓↓↓ 这一行 = 播放视频（循环，不会往下走）↓↓↓ */
    ESP_LOGI(TAG, "step: VIDEO");
    demo_show_video();
#endif

    /* 两段都关掉的话停在这里，不退出 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
