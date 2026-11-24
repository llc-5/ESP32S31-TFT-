#include "ili9341.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ili9341";

// ============================================================
// 引脚表 —— 按用户实物接线（2026-11-24 确认）
//   屏幕 CS       -> GPIO40
//   屏幕 RESET    -> GPIO42
//   屏幕 DC       -> GPIO43
//   屏幕 SDI(MOSI)-> GPIO44
//   屏幕 SCK      -> GPIO45
//   屏幕 LED      -> 电源常亮（不占 IO）
//   屏幕 SDO / 触摸 5 根 -> 不接
//
// 芯片是 ESP32-S31（不是 ESP32-S3），已查官方 GPIO 文档：
// 1) S31 的 UART0 在 GPIO58/59，所以 43/44 是普通 GPIO，不抢串口也能用。
// 2) GPIO40 是 strapping 脚（S31 的 strapping 是 GPIO36~40、60~61）。
//    作 CS 使用：CS 空闲为高，只要上电时不被外部拉低就正常。
//    若出现反复重启/无法烧录，把 CS 换到 GPIO42 等非 strapping 脚。
// 3) GPIO42~45 兼作 ADC1 模拟输入，作数字 GPIO 无需额外配置。
// 4) GPIO26~32 被 SPI flash 占用，不要碰。
// ============================================================
#define PIN_CS    40
#define PIN_RST   42
#define PIN_DC    43
#define PIN_MOSI  44
#define PIN_SCK   45

/* ESP32-S3 SPI-DMA 单次传输上限 4095 字节，取 4092（4 的倍数）确保不越界 */
#define SPI_CHUNK_SIZE  4092

/* 先低速跑通，稳定后再往上调（20/40MHz）。杜邦线接的屏 40MHz 常花屏 */
#define LCD_SPI_HZ      (10 * 1000 * 1000)

static spi_device_handle_t g_spi;

static inline void cs_low(void)  { gpio_set_level(PIN_CS, 0); }
static inline void cs_high(void) { gpio_set_level(PIN_CS, 1); }

/* 一条 SPI 传输的封装
 * 注意：≤4 字节必须用 SPI_TRANS_USE_TXDATA（内联发送），
 * 否则走 DMA 的小包在部分 IDF 版本上会发不出去。 */
static void spi_tx(const uint8_t *buf, uint32_t len)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    if (len <= 4) {
        t.flags = SPI_TRANS_USE_TXDATA;
        memcpy(t.tx_data, buf, len);
    } else {
        t.tx_buffer = buf;
    }
    esp_err_t err = spi_device_transmit(g_spi, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi tx fail: %s", esp_err_to_name(err));
    }
}

/* 单独发命令（无参数） */
static void lcd_write_cmd(uint8_t cmd)
{
    cs_low();
    gpio_set_level(PIN_DC, 0);
    spi_tx(&cmd, 1);
    cs_high();
}

/* 命令 + 参数：必须在同一个 CS 低电平周期内完成
 * ——这是 ILI9341 的硬要求，CS 中途拉高参数会丢。 */
static void lcd_write_cmd_data(uint8_t cmd, const uint8_t *data, uint32_t len)
{
    cs_low();
    gpio_set_level(PIN_DC, 0);
    spi_tx(&cmd, 1);
    if (len) {
        gpio_set_level(PIN_DC, 1);
        spi_tx(data, len);
    }
    cs_high();
}

void ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];

    buf[0] = (uint8_t)(x0 >> 8);
    buf[1] = (uint8_t)(x0 & 0xFF);
    buf[2] = (uint8_t)(x1 >> 8);
    buf[3] = (uint8_t)(x1 & 0xFF);
    lcd_write_cmd_data(0x2A, buf, 4);

    buf[0] = (uint8_t)(y0 >> 8);
    buf[1] = (uint8_t)(y0 & 0xFF);
    buf[2] = (uint8_t)(y1 >> 8);
    buf[3] = (uint8_t)(y1 & 0xFF);
    lcd_write_cmd_data(0x2B, buf, 4);
}

void ili9341_flush(const uint8_t *data, uint32_t len)
{
    uint32_t sent = 0;
    uint8_t cmd = 0x2C;

    ili9341_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    // 0x2C 之后整段像素数据保持在同一个 CS 低电平内，分块连续发
    cs_low();
    gpio_set_level(PIN_DC, 0);
    spi_tx(&cmd, 1);
    gpio_set_level(PIN_DC, 1);

    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > SPI_CHUNK_SIZE) {
            chunk = SPI_CHUNK_SIZE;
        }
        spi_tx(data + sent, chunk);
        sent += chunk;
    }
    cs_high();
}

void ili9341_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    static uint8_t line[LCD_WIDTH * 2];
    uint32_t i;

    if (w > LCD_WIDTH) {
        w = LCD_WIDTH;
    }

    for (i = 0; i < w; i++) {
        line[i * 2]     = (uint8_t)(color >> 8);
        line[i * 2 + 1] = (uint8_t)(color & 0xFF);
    }

    ili9341_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));

    {
        uint8_t cmd = 0x2C;
        cs_low();
        gpio_set_level(PIN_DC, 0);
        spi_tx(&cmd, 1);
        gpio_set_level(PIN_DC, 1);
        while (h--) {
            spi_tx(line, w * 2);
        }
        cs_high();
    }
}

void ili9341_fill_screen(uint16_t color)
{
    ili9341_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

/* 画一行像素：x/y 起点，w 像素宽，be_bytes 为高字节在前的 RGB565 数据 */
void ili9341_draw_row(uint16_t x, uint16_t y, uint16_t w, const uint8_t *be_bytes)
{
    uint8_t cmd = 0x2C;
    uint32_t len = (uint32_t)w * 2;
    uint32_t sent = 0;

    if (w == 0) {
        return;
    }
    if (x >= LCD_WIDTH) {
        return;
    }
    if (x + w > LCD_WIDTH) {
        w = (uint16_t)(LCD_WIDTH - x);
        len = (uint32_t)w * 2;
    }

    ili9341_set_window(x, y, (uint16_t)(x + w - 1), y);

    cs_low();
    gpio_set_level(PIN_DC, 0);
    spi_tx(&cmd, 1);
    gpio_set_level(PIN_DC, 1);
    while (sent < len) {
        uint32_t chunk = len - sent;
        if (chunk > SPI_CHUNK_SIZE) {
            chunk = SPI_CHUNK_SIZE;
        }
        spi_tx(be_bytes + sent, chunk);
        sent += chunk;
    }
    cs_high();
}

/* ============================================================================
 * 索引图 / 位打包图 -> 屏幕
 *
 * 三个公开函数共用下面这个内部实现：一次开窗、一次发 0x2C，
 * 然后逐行把「颜色索引」查表成 RGB565 字节流连续发出。
 * 只开一次窗，比每行都 set_window 快得多 —— 视频逐帧刷新靠的就是这一点。
 * ========================================================================== */
static uint8_t s_blit_line[LCD_WIDTH * 2];   /* 480 字节，整行 RGB565 字节流 */

static void blit_indexed(uint16_t x, uint16_t y,
                         uint16_t sw, uint16_t sh,
                         uint16_t dw, uint16_t dh,
                         const uint16_t *palette,
                         const uint8_t *idx, uint8_t bpp)
{
    uint16_t r, i;
    uint8_t  cmd = 0x2C;
    uint32_t row_bytes;
    int      direct;

    if (palette == NULL || idx == NULL || sw == 0 || sh == 0 || dw == 0 || dh == 0) {
        return;
    }
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) {
        return;
    }
    /* 越界就裁掉，绝不写到窗口外面去 */
    if (x + dw > LCD_WIDTH) {
        dw = (uint16_t)(LCD_WIDTH - x);
    }
    if (y + dh > LCD_HEIGHT) {
        dh = (uint16_t)(LCD_HEIGHT - y);
    }

    row_bytes = ((uint32_t)sw * bpp + 7u) / 8u;   /* 源图每行占多少字节（位打包） */
    if (bpp == 8) {
        row_bytes = sw;
    }

    /* 源图尺寸 == 屏上尺寸时不需要缩放，走这条路。
     * 这不是微优化：RISC-V 没有硬件除法，下面那两个 (r*sh)/dh、(i*sw)/dw
     * 每像素要跑两次软件除法，240x320 一帧就是 15 万次，实测多花 100ms 以上，
     * 视频直接掉到 7fps。视频播放正好是 1:1，一定要走快速路径。 */
    direct = (sw == dw && sh == dh);

    ili9341_set_window(x, y, (uint16_t)(x + dw - 1), (uint16_t)(y + dh - 1));

    cs_low();
    gpio_set_level(PIN_DC, 0);
    spi_tx(&cmd, 1);
    gpio_set_level(PIN_DC, 1);

    for (r = 0; r < dh; r++) {
        const uint8_t *srow;

        if (direct) {
            srow = idx + (uint32_t)r * row_bytes;
        } else {
            /* 纵向最近邻：屏上第 r 行取源图第 sy 行 */
            uint16_t sy = (uint16_t)(((uint32_t)r * sh) / dh);
            srow = idx + (uint32_t)sy * row_bytes;
        }

        if (bpp == 8) {
            /* 8bpp：1 像素 1 字节，直接查表 */
            if (direct) {
                for (i = 0; i < dw; i++) {
                    uint16_t color = palette[srow[i]];
                    s_blit_line[i * 2]     = (uint8_t)(color >> 8);
                    s_blit_line[i * 2 + 1] = (uint8_t)(color & 0xFF);
                }
            } else {
                for (i = 0; i < dw; i++) {
                    uint16_t sx = (uint16_t)(((uint32_t)i * sw) / dw);
                    uint16_t color = palette[srow[sx]];
                    s_blit_line[i * 2]     = (uint8_t)(color >> 8);
                    s_blit_line[i * 2 + 1] = (uint8_t)(color & 0xFF);
                }
            }
        } else {
            /* 位打包：从字节里抠出 bpp 位 */
            uint8_t  mask  = (uint8_t)((1u << bpp) - 1u);
            uint8_t  shift0 = (uint8_t)(8 - bpp);
            if (direct) {
                for (i = 0; i < dw; i++) {
                    uint32_t bit = (uint32_t)i * bpp;
                    uint8_t  ci = (uint8_t)((srow[bit >> 3] >> (shift0 - (bit & 7))) & mask);
                    uint16_t color = palette[ci];
                    s_blit_line[i * 2]     = (uint8_t)(color >> 8);
                    s_blit_line[i * 2 + 1] = (uint8_t)(color & 0xFF);
                }
            } else {
                for (i = 0; i < dw; i++) {
                    uint16_t sx = (uint16_t)(((uint32_t)i * sw) / dw);
                    uint32_t bit = (uint32_t)sx * bpp;
                    uint8_t  ci = (uint8_t)((srow[bit >> 3] >> (shift0 - (bit & 7))) & mask);
                    uint16_t color = palette[ci];
                    s_blit_line[i * 2]     = (uint8_t)(color >> 8);
                    s_blit_line[i * 2 + 1] = (uint8_t)(color & 0xFF);
                }
            }
        }
        spi_tx(s_blit_line, (uint32_t)dw * 2);
    }

    cs_high();
}

void LCD_ShowImage256(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint16_t *palette, const uint8_t *indices)
{
    /* 8bpp：1 像素 1 字节，源图与屏上 1:1 */
    blit_indexed(x, y, w, h, w, h, palette, indices, 8);
}

void LCD_ShowImage256Fit(uint16_t x, uint16_t y,
                         uint16_t sw, uint16_t sh,
                         uint16_t dw, uint16_t dh,
                         const uint16_t *palette, const uint8_t *indices)
{
    /* 8bpp + 最近邻缩放 */
    blit_indexed(x, y, sw, sh, dw, dh, palette, indices, 8);
}

void LCD_ShowPacked(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    const uint16_t *palette, const uint8_t *packed, uint8_t bpp)
{
    /* 位打包，源图与屏上 1:1（视频逐帧走这里） */
    blit_indexed(x, y, w, h, w, h, palette, packed, bpp);
}

void ili9341_init(void)
{
    gpio_config_t io;

    /* 先把三个控制脚复位成默认 GPIO 状态，清掉上电残留配置。
     * （S31 的 UART0 在 58/59，43/44 这里不是串口，所以不涉及抢引脚） */
    gpio_reset_pin(PIN_CS);
    gpio_reset_pin(PIN_DC);
    gpio_reset_pin(PIN_RST);

    io.pin_bit_mask = (1ULL << PIN_CS) | (1ULL << PIN_DC) | (1ULL << PIN_RST);
    io.mode = GPIO_MODE_INPUT_OUTPUT;   /* 输入输出模式：便于读回验证 */
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io);

    gpio_set_level(PIN_CS, 1);
    gpio_set_level(PIN_DC, 1);
    gpio_set_level(PIN_RST, 1);

    ESP_LOGI(TAG, "[1] gpio ok  CS=%d DC=%d RST=%d  (expect 1 1 1)",
             gpio_get_level(PIN_CS), gpio_get_level(PIN_DC), gpio_get_level(PIN_RST));

    /* SPI 总线：CS 交给软件控制，所以 spics_io_num 设 -1 */
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_CHUNK_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = -1,          /* CS 手动控制，保证命令+参数连续 */
        .queue_size = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &g_spi));
    ESP_LOGI(TAG, "[2] spi ok  SCK=%d MOSI=%d CS=%d DC=%d RST=%d @%dMHz",
             PIN_SCK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST, LCD_SPI_HZ / 1000000);

    /* 复位：照抄 STM32 的 20/20/120ms */
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "[3] reset done");

    /* 初始化序列：逐条照抄 04_RealHardware/ili9341.c 的 LCD_Init() */
    lcd_write_cmd(0x01);            /* SWRESET */
    vTaskDelay(pdMS_TO_TICKS(100));
    lcd_write_cmd(0x11);            /* SLPOUT  */
    vTaskDelay(pdMS_TO_TICKS(120));

    {
        uint8_t p1 = 0x55;
        lcd_write_cmd_data(0x3A, &p1, 1);   /* COLMOD RGB565 */
        p1 = 0x48;                          /* MADCTL 正向 + BGR */
        lcd_write_cmd_data(0x36, &p1, 1);
        p1 = 0x01;
        lcd_write_cmd_data(0x26, &p1, 1);   /* GAMSET */
    }

    {
        static const uint8_t B1[] = {0x00, 0x18};
        static const uint8_t B6[] = {0x08, 0x82, 0x27};
        static const uint8_t C0[] = {0x23};
        static const uint8_t C1[] = {0x10};
        static const uint8_t C5[] = {0x3E, 0x28};
        static const uint8_t C7[] = {0x86};
        static const uint8_t E0[] = {
            0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
            0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00
        };
        static const uint8_t E1[] = {
            0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
            0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F
        };

        lcd_write_cmd_data(0xB1, B1, 2);
        lcd_write_cmd_data(0xB6, B6, 3);
        lcd_write_cmd_data(0xC0, C0, 1);
        lcd_write_cmd_data(0xC1, C1, 1);
        lcd_write_cmd_data(0xC5, C5, 2);
        lcd_write_cmd_data(0xC7, C7, 1);
        lcd_write_cmd_data(0xE0, E0, 15);
        lcd_write_cmd_data(0xE1, E1, 15);
    }

    lcd_write_cmd(0x20);            /* INVOFF */
    lcd_write_cmd(0x13);            /* NORON  */
    lcd_write_cmd(0x29);            /* DISPON */
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_LOGI(TAG, "[4] init seq done");

    ili9341_fill_screen(LCD_RGB(0, 0, 0));
    ESP_LOGI(TAG, "[5] clear done - if screen still white, it's wiring or panel");
}
