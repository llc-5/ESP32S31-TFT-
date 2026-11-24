# ESP32-S31 + ILI9341 图片 / 视频显示

用 ESP32-S31 驱动 ILI9341 TFT LCD（240×320），硬件 SPI，ESP-IDF v6.2.0。

## 现在能干什么

`main.c` 里只剩两段，按顺序跑：**图片 → 视频（带声音，循环）**。

| 功能 | 函数 | 数据来源 |
|---|---|---|
| 显示图片 | `demo_show_image()` | `image_avatar.h`（你的网站生成） |
| 播放视频 | `demo_show_video()` | `video_data.c` + `audio_data.c` |

`app_main()` 里每一行调用上面都有 `↓↓↓ 这一行 = 显示图片 ↓↓↓` 这样的注释，
想关掉某一段把 `STEP_IMAGE / STEP_VIDEO` 改成 0 就行；`STEP_SOUND` 单独控制声音，
`STEP_SELFTEST` 控制开机那三下红绿蓝闪烁（查白屏时有用，平时可以关掉）。

> `image_avatar.h` 是**你自己替换的图片库**，工程里任何代码都不改它。
> 要换图片就用网站重新生成这个头文件、整文件覆盖，前提是变量前缀保持默认的 `IMG_AVATAR`。

---

## 一、改颜色（RGB）

颜色都集中在 `main.c` 顶部，用 `RGB(r, g, b)`，三个分量 0~255：

```c
#define COLOR_BG         RGB(0,   0,   0)     /* 全屏背景 */
#define COLOR_IMAGE_BG   RGB(0,   0,   0)     /* 显示图片时的底色 */
```

图片本身的颜色由它自己的调色板决定（在 `image_avatar.h` 里），想改图片颜色就在网站上重新生成。
`RGB()` 宏在 `main/font.h` 里，原理是把 8 位分量截成屏幕的 5-6-5：

```c
#define RGB(r, g, b)  ((uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | \
                                  (((uint16_t)(g) & 0xFCu) << 3) | \
                                   ((uint16_t)(b) >> 3)))
```

所以会有极轻微的舍入误差（红色只剩 32 级、绿色 64 级），肉眼看不出来，是正常的。

### 画字的两个函数（`main/font.c`）

```c
font_draw_char(x, y, 'A', fg, bg, scale);          /* 单个字符 */
font_draw_text(x, y, "HELLO", fg, bg, scale);      /* 一串 */
uint16_t w = font_text_width("HELLO", scale);      /* 算宽度，用来居中 */
```

- `scale` 是放大倍数：1 = 8×16，2 = 16×32，5 = 40×80
- `bg` 填屏幕背景色就等于「透明底」
- 支持 `0~9`、`A~Z`，小写会自动按大写画

字库在 `font.c` 顶部：`FONT_09[10][16]` 是数字，`FONT_AZ[26][16]` 是字母。
A~Z 逐字搬自你 STM32 那边验证过的 `04_RealHardware/ili9341.c`，不要改。

---

## 二、显示图片（和你那个网站对接）

### 换图：一条命令，别手动复制

最容易出事的就是「以为换了、其实没换」：文件存到别的目录、编辑器没保存、或者根本没重编。
所以用这个脚本装图，它会校验格式、报告这份图长什么样、还会挡住"和现有的一模一样"这种误操作：

```bash
python tools/install_image.py C:\Users\你\Downloads\image_avatar.h   # 安装（自动备份旧的）
python tools/install_image.py --show                                # 看现在装的是哪张
python tools/install_image.py --check 某个文件.h                    # 只检查格式，不安装
```

装完重新烧录，**看串口这一行就能确认固件里装的是新图**：

```
APP: image: 源图 48x48 -> 屏上 240x240, 16色/4bpp, 1184 字节
APP: image: 数据校验和 0x420001D5（换了图这个值一定会变）
```

那个校验和就是脚本打印的「数据校验和」。**两边对不上 = 烧的不是这份代码**，
不用再怀疑屏幕或接线。

### 网站生成的 `image_avatar.h` 直接能用

驱动里已经按你网站的约定实现了两个函数，签名一模一样：

```c
void LCD_ShowImage256(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint16_t *palette, const uint8_t *indices);

void LCD_ShowImage256Fit(uint16_t x, uint16_t y,
                         uint16_t sw, uint16_t sh,
                         uint16_t dw, uint16_t dh,
                         const uint16_t *palette, const uint8_t *indices);
```

所以网站生成的头文件**原样丢进 `main/` 覆盖 `image_avatar.h`**，`main.c` 一个字都不用改。

`main.c` 里就是网站文档写的那句：

```c
LCD_ShowImage256Fit(IMG_AVATAR_X, IMG_AVATAR_Y,
                    IMG_AVATAR_W, IMG_AVATAR_H,     /* 源图尺寸（存 Flash 的） */
                    IMG_AVATAR_DW, IMG_AVATAR_DH,   /* 屏上尺寸（最近邻放大） */
                    IMG_AVATAR_PALETTE, IMG_AVATAR);
```

**256 色档和 16 色档都能用同一句**：16/4/2 色档时网站头文件末尾会用宏把这句接到它自带的
`IMG_Draw()`，所以 `IMG_AVATAR` 这个标识符在 16 色档其实是不存在的 —— 但它出现在宏参数里，
预处理阶段就被丢弃了，不会报错。这是网站刻意设计的，别去「修」它。

### 命令行也能生成（不用开浏览器）

`tools/img2header.py` 是那个网站的等价实现，**输出格式逐字一致**，两边产物可以互换：

```bash
# 48x48 头像，16 色 4bpp，放大到屏上 240x240
python tools/img2header.py 头像.png main/image_avatar.h --sw 48 --sh 48 --dw 240 --dh 240 --bpp 4

# 全屏铺满，256 色 8bpp（网站默认档）
python tools/img2header.py 照片.jpg main/image_avatar.h --sw 240 --sh 320 --dw 240 --dh 320 --bpp 8
```

参数：`--sw/--sh` 存 Flash 的源图尺寸，`--dw/--dh` 屏上显示尺寸，`--bpp 1/2/4/8`，
`--rot 90` 旋转（像素在数据里就转好），`--no-dither` 关抖动。

### 容量参考（240×320）

| 色数 | bpp | 一屏数据 | 说明 |
|---|---|---|---|
| 256 色 | 8 | 76800 B | 最忠实 |
| 16 色 | 4 | 38400 B + 32 B 调色板 | 照片够用，推荐 |
| 4 色 | 2 | 19200 B + 8 B | 线条图/文字截图 |
| 2 色 | 1 | 9600 B + 4 B | 二维码、黑白画 |

---

## 三、显示视频

### 一键转换

```bash
python tools/video2c.py 你的视频.mp4 --fps 8 --duration 10
```

输出三个文件：

- `main/video_data.bin` —— 帧数据（二进制，MB 级）
- `main/video_data.c` —— 调色板 + `video_t` 结构（只有 1KB）
- `main/video_data.h`

帧数据**没有写成 C 数组**，而是由 `CMakeLists.txt` 的 `EMBED_FILES` 直接嵌进固件。
原因：3MB 数据写成 `0x..` 文本要 18MB 的 `.c` 文件，编辑器打开就卡死，编译也要多花几分钟。

### 参数

| 参数 | 默认 | 说明 |
|---|---|---|
| `--width/--height` | 240/320 | 帧尺寸 |
| `--fps` | 8 | 屏上 SPI 10MHz 时 8~20fps 都没问题 |
| `--colors` | 16 | 16 色=4bpp 省一半，256 色=8bpp 最清楚 |
| `--start/--duration` | 0/10 | 从第几秒开始、取几秒 |
| `--max-bytes` | 5000000 | 总量上限，超了自动砍帧数 |
| `--fit` | cover | `cover` 裁边不变形 / `stretch` 拉伸 / `contain` 加黑边 |

### 播放

```c
video_play_loop(&VID_DEMO);       /* 一直循环，不返回 */
video_play_once(&VID_DEMO);       /* 播一遍就返回 */
video_show_frame(&VID_DEMO, 40);  /* 定格在第 40 帧 */
```

播放器在 `main/video_show.c`。用「按时间轴排程」而不是每帧固定 delay，
某帧画慢了后面不会累积延迟。

性能：240×320 / 4bpp 一帧 38400 字节，SPI 10MHz 下约 **31ms**，加解码约 33ms，
所以理论上能跑 30fps，实际选 8fps 是为了压数据量。

### 关于「视频声音」

现在**画面和声音都有了**。声音走独立的 I2S 通道，代码在 `main/i2s_sound.c`，
音轨数据在 `main/audio_data.bin`（由 `tools/audio2c.py` 从同一个 mp4 抽出来的）。

#### 硬件前提：必须外接 I2S 功放模块

ESP32-S31 **本身没有模拟 DAC**，不能直接接喇叭，必须买一个 I2S 数字功放模块：

| 模块 | 价格 | 需要几根线 | 备注 |
|---|---|---|---|
| **MAX98357A** | 十几块 | 5 根（VIN/GND/LRC/BCLK/DIN） | 最省事，3W 直推小喇叭，推荐 |
| PCM5102A | 二十来块 | 7 根（多 SCK/FMT/XSMT） | 音质好，但要多拉两个脚到 GND/3V3 |

接线（默认脚号，改 `main/i2s_sound.h` 顶部即可）：

| 模块脚 | ESP32-S31 | 说明 |
|---|---|---|
| BCLK / BCK | **GPIO47** | 位时钟 |
| LRC / LRCK | **GPIO48** | 声道时钟 |
| DIN | **GPIO46** | 数据 |
| VIN | 3.3V（PCM5102 可用 5V） | |
| GND | GND，必须共地 | |
| GAIN / SD | 悬空 | MAX98357A：默认 9dB、左声道 |
| SCK / FMT / XSMT | SCK→GND，FMT→GND，XSMT→3V3 | 仅 PCM5102 需要 |

> ⚠ **ESP32-S31 的 GPIO 编号不连续**：只有 0~28、30~40、42~61。
> **没有 GPIO 29，也没有 GPIO 41** —— 写 `GPIO_NUM_41` 会直接编译报
> `'GPIO_NUM_41' undeclared`。你之前说「41 号脚」其实这颗芯片上没有。

#### 抽音轨

```bash
python tools/audio2c.py 你的视频.mp4 --duration 10 --rate 16000
```

输出三个文件：

- `main/audio_data.bin` —— 16bit 单声道 PCM 裸数据（10 秒 16kHz = 320KB）
- `main/audio_data.c` —— 只有采样率/样本数/指针，1KB
- `main/audio_data.h`

参数：

| 参数 | 默认 | 说明 |
|---|---|---|
| `--rate` | 16000 | 采样率。16000 够听且省空间，22050 音质更好 |
| `--duration` | 10 | 取几秒，**要和 `video2c.py --duration` 对上** |
| `--start` | 0 | 从第几秒开始 |
| `--gain` | 0 | 0 = 自动峰值归一化到 -1dB（手机视频音量通常很小，建议保留） |
| `--max-bytes` | 900000 | 数据上限，超了自动截断 |

#### 播放（`main/i2s_sound.c`）

```c
sound_play_async(&AUDIO_DEMO);     /* 起后台任务喂 I2S，立刻返回 */
sound_play_blocking(&AUDIO_DEMO);  /* 阻塞到播完，放提示音用 */
sound_stop();                      /* 立刻停 */
while (sound_is_playing()) { ... } /* 查状态 */
```

`demo_show_video()` 里的循环是这样的：

```c
while (1) {
    sound_play_async(&AUDIO_DEMO);   /* 1. 声音先起（非阻塞） */
    video_play_once(&VID_DEMO);      /* 2. 画面刷一遍，约 10 秒 */
    while (sound_is_playing()) { }   /* 3. 等声音收尾，避免两轮叠音 */
}
```

为什么不会互相拖慢：音频走 **I2S DMA**、画面走 **SPI DMA**，两条硬件通路独立；
音频任务固定在**核 1**（`SOUND_TASK_CORE`），刷屏在 `app_main`/核 0，谁都饿不死谁。

> 音画同步精度：声音严格按 16000Hz 实时播（10.000 秒），画面是 8fps×80 帧
> 但每帧实际耗时约 138ms（> 125ms 周期），一轮大约 11.1 秒，**每轮会慢约 1 秒**。
> 想让两者更贴，把 `video2c.py --fps` 降到 **7**（周期 143ms ≈ 实际耗时），
> 或者把 `LCD_SPI_HZ` 提到 20MHz。

---

## 四、分区表（重要）

默认的 `singleapp` 只给 app 分区 **1MB**，放图片没问题，视频 3MB 直接塞不下。
所以工程自带 `partitions.csv`：

```
# Name,     Type, SubType,  Offset,  Size
nvs,        data, nvs,      ,        0x6000
phy_init,   data, phy,      ,        0x1000
factory,    app,  factory,  ,        0x600000     ← 6MB
```

对应 `sdkconfig`：

```
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"
```

芯片实际是 **16MB Flash**，之前配成 2MB 会浪费掉 14MB。现在改对了。

> 改了分区表之后**必须整片擦除一次**再烧，否则旧的分区表数据会残留：
> `.\flash.ps1 -Full`

想放更多视频就改 `0x600000` 到 `0xC00000`（12MB），还剩得下 4MB 给别的东西。

---

## 五、一键编译烧录

```powershell
.\flash.ps1                 # 编译 + 烧录 + 读串口验证（默认 COM10）
.\flash.ps1 COM12           # 指定串口
.\flash.ps1 -Full           # 先整片擦除再烧（改过分区表就用这个）
.\flash.ps1 -NObuild        # 跳过编译，直接烧现有产物
```

脚本做的事：搭环境 → 比对 bin 与源码时间戳（旧就强制重编）→ 编译 → 断言产物更新
→ 写 bootloader/分区表 → 调 `flash_chunked.py` 分块写 3.7MB 的 app（每块 hash 校验）
→ 读串口打印 `FIRMWARE BUILD` 时间，让你亲眼确认跑的是新程序。

整轮大约 **1 分半**（以前一次写完要 4 分钟，还经常中途断）。

只想单独重烧 app：
```powershell
python flash_chunked.py --port COM10 --bin build\sample_project.bin
```

### 「烧完还是旧程序」的四个真实原因

| # | 原因 | 本脚本怎么治 |
|---|---|---|
| 1 | 只烧 app，没烧 bootloader/分区表 | 固定写 `0x2000/0x8000/0x10000` |
| 2 | ninja 复用旧目标（`.h` 改了没重编） | 比对时间戳，旧了就把 build 挪开重编 |
| 3 | otadata / 旧分区表残留 | `-Full` 整片擦除 |
| 4 | 波特率太高，stub 加载失败但以为是成功 | 固定 `460800` + `--connect-attempts 5` |

### 3.7MB 固件必须分块烧（这是踩得最深的一个坑）

app 里有 3.07MB 视频 + 320KB 音频，固件 3.7MB。**一次性写进去一定会失败**：

| 方式 | 结果 |
|---|---|
| `--no-stub` 走 ROM 加载器 | 传到 **80%** 报 `No more data to read from the serial port` |
| stub 模式（自动压缩） | 压到 1.13MB 后直接 `Failed to start stub flasher. There was no response` |
| 降到 230400 波特率 | 一模一样，和速度无关 |

而 bootloader（21KB）、分区表（3KB）这种小块**从来没有失败过**。所以问题是大块传输本身。

解法：`flash_chunked.py` 把 app 切成 1MB 一块，第 i 块写到 `0x10000 + i*0x100000`。
app.bin 本来就是一段连续的 Flash 镜像，分块写和一次写完在 Flash 上完全等价。
实测 4 块各 14~17 秒，**总共 70 秒**，比原来 4 分钟还快，而且每块都有 hash 校验。

> ⚠ **块大小必须是 4096 的整数倍**（Flash 一个扇区）。
> esptool 写入前会把涉及的扇区整片擦掉，切点如果落在扇区中间，
> 擦这一块的时候会把上一块的尾巴一起抹掉 —— 症状是**烧完板子无限重启**
> （串口刷 `rst:0x3 (SW_SYS_RESET)`，一行 APP 日志都没有）。
> 我第一次用 1,000,000 字节做块大小就踩了这个，`0x104240` 不是 4KB 对齐的。

### 环境坑（都踩过了，记下来）

- **`.ps1` 必须存成 UTF-8 带 BOM**，否则 PowerShell 5.1 按 ANSI 读，中文变乱码直接解析失败
- 全量编译时 `esp-idf-configdep` 会报 `Permission denied (13)`，用 `--no-configdep --no-ccache` 绕开
- `ESP_ROM_ELF_DIR` 要指向 `tools\esp-rom-elfs\20260528`，指错会导致编译假死
- `idf.py build` 有时会在打印编译错误时自己崩掉（`UnicodeDecodeError: 0xb2`），
  真正的错误信息被吞了。**绕开办法：直接跑 `ninja -C build all`**，能看到完整报错
- `erase-flash` 在这颗芯片上不可用（`ESP32-S31 ROM does not support function erase_flash`），
  只要分区偏移没变就不需要整片擦
- 烧录中途失败后，CP210x **会卡死**，之后打开串口报
  `PermissionError(13) 连到系统上的设备没有发挥作用`。
  这时**拔掉 USB 等 3 秒再插回来**才能恢复，软件层面重启不了（要管理员权限）

---

## 六、接线

| 屏幕丝印 | ESP32-S31 GPIO | 说明 |
|---|---|---|
| CS | **GPIO40** | strapping 脚，CS 空闲高，正常 |
| RESET | **GPIO42** | |
| DC | **GPIO43** | |
| SDI(MOSI) | **GPIO44** | |
| SCK | **GPIO45** | |
| LED(背光) | **直接接电源常亮** | 不占 IO |
| VCC / GND | 3.3V / GND，必须共地 | |
| SDO(MISO) | 不接 | 只写不读 |
| T_* 五根 | 不接 | 触摸没用 |

启动日志实测确认（`I (146) cpu_start: GPIO 59 and 58 are used as console UART I/O pins`）：
S31 的 UART0 在 **GPIO58/59**，所以 43/44/45 当数字 GPIO 完全不会被抢，串口日志也照常能看。

### 再挂一个 I2S 功放模块（想让视频出声才需要）

| 模块脚（MAX98357A） | ESP32-S31 | 说明 |
|---|---|---|
| BCLK | **GPIO47** | 位时钟 |
| LRC | **GPIO48** | 声道时钟 |
| DIN | **GPIO46** | 音频数据 |
| VIN | 3.3V | 5V 也行，但别超过模块标称 |
| GND | GND | **必须和屏幕共地** |
| GAIN / SD | 悬空 | 9dB / 左声道 |
| 喇叭 + / - | 接 4Ω 或 8Ω 小喇叭 | 别接成短路 |

脚号想换直接改 `main/i2s_sound.h` 顶部的 `SOUND_PIN_BCLK / WS / DOUT`。
手头没模块就把 `main.c` 里 `STEP_SOUND` 改成 0，一个 GPIO 都不会被占。

---

## 七、驱动层要点（改之前先看）

### CS 必须软件控制

```c
devcfg.spics_io_num = -1;   /* 不要交给 SPI 驱动 */
```

ILI9341 要求命令和参数在同一个 CS 低电平周期内。SPI 驱动自动控制 CS 会在两次
`spi_device_transmit` 之间把 CS 拉高，参数全丢，初始化等于没做。

### 单次传输上限 4092 字节

```c
#define SPI_CHUNK_SIZE  4092   /* DMA 单次上限 4095 */
```

≤4 字节的包用 `SPI_TRANS_USE_TXDATA` 内联发送，避免小包 DMA 发不出去。

### 字节序

屏幕要**高字节先发**。RISC-V 是小端，`uint16_t` 数组不能直接强转成 `uint8_t*`，
否则红蓝互换。所有写屏函数内部都先拆高低字节。

### 一次开窗，不要每行开一次

`blit_indexed()` 是整个显示性能的核心：一次 `set_window` + 一次 `0x2C`，
之后逐行连续 `spi_tx`。视频 30ms/帧靠的就是这个。逐行 `set_window` 会慢十倍。

### 其他

- `mode = 0`（CPOL=0 CPHA=0），跟 STM32 源码 `LCD_SCK_L()` 一致
- `LCD_SPI_HZ = 10MHz`，杜邦线接的屏 40MHz 常花屏，稳定后可试 20MHz
- 初始化序列逐条照抄 STM32 的 `04_RealHardware/ili9341.c`

---

## 八、全白屏排查

1. **看串口**：`[1] gpio ok` / `[2] spi ok` / `[3] reset done` / `[4] init seq done` / `[5] clear done`
   五步全打印 → 固件没问题，看第 2 步
2. **看红绿蓝有没有闪**：闪了 → SPI 通了；不闪 → 数据没进屏
3. **查线**：CS(40)、DC(43)、SCK(45)、SDI(44) 逐根对丝印，这四个最容易接反
4. **电源**：VCC 必须 3.3V，GND 共地
5. **降速**：`LCD_SPI_HZ` 降到 1MHz 试
6. **换 SPI 模式**：`devcfg.mode = 0` 改 `3`

---

## 九、工程结构

```
sample_project/
├── partitions.csv            # 6MB app 分区（装得下视频 + 音频）
├── sdkconfig                 # 16MB Flash + 自定义分区表
├── flash.ps1 / flash.bat     # 一键编译+烧录+验证
├── flash_chunked.py          # 3.7MB 固件分块烧写（必须用，见第五节）
├── read_serial.py            # 复位并抓串口日志
├── tools/
│   ├── img2header.py         # 图片 -> image_avatar.h（与网站格式一致）
│   ├── video2c.py            # 视频画面 -> video_data.bin/.c/.h
│   └── audio2c.py            # 视频音轨 -> audio_data.bin/.c/.h
└── main/
    ├── CMakeLists.txt        # 源文件清单 + EMBED_FILES 视频/音频
    ├── main.c                # 四段演示 + 颜色配置 + 声音开关
    ├── font.c/.h             # RGB 宏 + 0~9/A~Z 字库 + 画字函数
    ├── ili9341.c/.h          # 驱动 + LCD_ShowImage256 / Fit / Packed
    ├── image_avatar.h        # 图片数据（网站格式，可被网站产物覆盖）
    ├── video_show.c/.h       # 视频播放器
    ├── video_data.bin/.c/.h  # 视频帧数据（video2c.py 生成）
    ├── i2s_sound.c/.h        # I2S 音频输出（改脚号在这里）
    ├── audio_data.bin/.c/.h  # 音轨 PCM（audio2c.py 生成）
    └── legacy/               # 旧版 img_t 图片库和 RLE 视频解码器，已不参与编译
```

`legacy/` 里是上一版的东西（`img_t` 结构、RLE 视频解码），已经被网站格式和
新的 `video_show` 取代，留着只是备查，不在 `CMakeLists.txt` 里，不会被编译。
