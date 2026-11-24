/* ============================================================================
 *  i2s_sound.c —— I2S 标准模式（Philips）音频输出实现
 *
 *  数据流：audio_data.bin（内嵌在 Flash 里）
 *          -> i2s_channel_write() 分块写
 *          -> DMA 自动搬 -> BCLK/WS/DIN 三根线
 *          -> 外部 I2S 功放模块 -> 喇叭
 * ========================================================================== */
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "i2s_sound.h"

static const char *TAG = "SND";

/* 每块写多少字节。太小 -> 中断/DMA 频繁，CPU 忙；太大 -> 停止响应慢。
   2048 字节 = 1024 个样本，16kHz 下正好 64ms 一块。 */
#define SOUND_CHUNK_BYTES   2048

static i2s_chan_handle_t s_tx       = NULL;
static uint32_t          s_rate     = 0;
static volatile bool     s_playing  = false;
static volatile bool     s_abort    = false;

/* ---------------------------------------------------------------------------
 * 建 I2S 通道：主机模式，只发不收
 * ------------------------------------------------------------------------- */
static bool create_channel(uint32_t rate)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num      = 6;      /* 6 个 DMA 描述符，够缓冲 6 块 */
    chan_cfg.dma_frame_num     = 256;    /* 每个描述符 256 个样本 */
    chan_cfg.auto_clear_after_cb = true; /* 播完后自动补静音，避免爆音 */

    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        s_tx = NULL;
        return false;
    }

    /* Philips 标准格式，16bit 样本，单声道
       （单声道在 I2S 上表现为只填左声道，MAX98357A / PCM5102 都正常出声） */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = SOUND_PIN_MCLK,
            .bclk = SOUND_PIN_BCLK,
            .ws   = SOUND_PIN_WS,
            .dout = SOUND_PIN_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init_std_mode failed: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return false;
    }

    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "channel_enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return false;
    }

    s_rate = rate;
    ESP_LOGI(TAG, "I2S ready: %luHz mono 16bit  BCLK=%d WS=%d DIN=%d",
             (unsigned long)rate, SOUND_PIN_BCLK, SOUND_PIN_WS, SOUND_PIN_DOUT);
    return true;
}

static void destroy_channel(void)
{
    if (!s_tx) {
        return;
    }
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx    = NULL;
    s_rate  = 0;
    s_playing = false;
}

bool sound_init(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0) {
        return false;
    }
    if (s_tx && s_rate == sample_rate_hz) {
        return true;                      /* 已经按这个采样率建好了 */
    }
    if (s_tx) {
        destroy_channel();                /* 采样率变了，重建 */
    }
    return create_channel(sample_rate_hz);
}

/* ---------------------------------------------------------------------------
 * 分块把整段 PCM 写进 I2S
 *
 * i2s_channel_write 在 DMA 满的时候会阻塞，阻塞时长由采样率决定，
 * 所以这里天然就是"按实时速度"播出去的，不用自己算延时。
 * ------------------------------------------------------------------------- */
static bool write_all(const audio_t *a)
{
    const uint8_t *p   = (const uint8_t *)a->data;
    uint32_t       rem = a->samples * 2;   /* 字节数 */

    s_abort = false;

    while (rem > 0) {
        if (s_abort) {
            break;
        }
        size_t chunk   = (rem > SOUND_CHUNK_BYTES) ? SOUND_CHUNK_BYTES : rem;
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, p, chunk, &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "write failed: %s", esp_err_to_name(err));
            return false;
        }
        if (written == 0) {
            break;                          /* 通道被 disable 了 */
        }
        p   += written;
        rem -= written;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * 后台播放任务
 * ------------------------------------------------------------------------- */
static void sound_task(void *arg)
{
    const audio_t *a = (const audio_t *)arg;

    s_playing = true;
    write_all(a);
    s_playing = false;

    ESP_LOGI(TAG, "play done, %lu samples", (unsigned long)a->samples);
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------------------
 * 对外接口
 * ------------------------------------------------------------------------- */
bool sound_play_async(const audio_t *a)
{
    if (!a || !a->data || a->samples == 0) {
        return false;
    }
    sound_stop();                       /* 之前的先停掉 */

    if (!sound_init(a->sample_rate)) {
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(sound_task, "snd", SOUND_TASK_STACK,
                                            (void *)a, SOUND_TASK_PRIO, NULL,
                                            SOUND_TASK_CORE);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate failed");
        return false;
    }
    return true;
}

bool sound_play_blocking(const audio_t *a)
{
    if (!a || !a->data || a->samples == 0) {
        return false;
    }
    sound_stop();
    if (!sound_init(a->sample_rate)) {
        return false;
    }
    s_playing = true;
    bool ok = write_all(a);
    s_playing = false;
    return ok;
}

void sound_stop(void)
{
    if (!s_tx) {
        return;
    }
    if (s_playing) {
        s_abort = true;
        /* disable 会让正阻塞在 write 里的任务立刻返回，避免卡死 */
        i2s_channel_disable(s_tx);
        /* 给任务一点时间退出循环，再重新使能通道 */
        vTaskDelay(pdMS_TO_TICKS(20));
        i2s_channel_enable(s_tx);
        s_playing = false;
    }
    s_abort = false;
}

bool sound_is_playing(void)
{
    return s_playing;
}
