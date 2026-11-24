# -*- coding: utf-8 -*-
"""
把视频里的声音抽出来，转成 ESP32 能直接丢给 I2S 播放的 PCM 数据。

产品是三个文件：
    main/audio_data.bin   纯 16bit 单声道 PCM 裸数据（给 EMBED_FILES 嵌进固件）
    main/audio_data.c     只有采样率/长度/指针，很小
    main/audio_data.h     声明 AUDIO_DEMO

为什么不像图片那样写成 C 数组：10 秒 16kHz 单声道就是 320000 字节，
写成 "0x12,0x34,..." 文本要 2MB，编辑器打开直接卡死。所以走二进制嵌入。

用法：
    python tools/audio2c.py <视频或音频> [选项]

常用示例：
    # 取前 10 秒、16kHz 单声道（和视频默认播放长度对上）
    python tools/audio2c.py demo.mp4 --duration 10

    # 音质好一点：22.05kHz（数据量 +38%）
    python tools/audio2c.py demo.mp4 --duration 10 --rate 22050

    # 只留画面不要声音 / 反过来只要声音都可以，这个是独立工具

参数：
    --rate      采样率，默认 16000。I2S 播放时按这个值配置
    --duration  取多少秒，默认 10（和 video2c.py 的默认帧数对应）
    --start     从第几秒开始，默认 0
    --gain      音量倍数，默认自动峰值归一化到 -1dB
    --max-bytes 数据上限，超了自动截断（默认 900000，约 28 秒@16k）
"""
import argparse
import array
import os
import subprocess
import sys


def find_ffmpeg():
    """和 video2c.py 一样，优先用 imageio-ffmpeg 自带的那个，不折腾 PATH"""
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        pass
    from shutil import which
    exe = which("ffmpeg")
    if not exe:
        sys.exit("找不到 ffmpeg。请 pip install imageio-ffmpeg，或把 ffmpeg 加进 PATH")
    return exe


def grab_pcm(ff, src, rate, start, duration):
    """让 ffmpeg 直接吐 s16le 裸流：转单声道 + 重采样 + 剥掉视频轨"""
    cmd = [
        ff, "-hide_banner", "-loglevel", "error",
        "-ss", str(start), "-t", str(duration),
        "-i", src,
        "-vn",                    # 不要视频
        "-ac", "1",               # 单声道
        "-ar", str(rate),         # 重采样到目标采样率
        "-f", "s16le", "-acodec", "pcm_s16le",
        "-",
    ]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    data = p.stdout.read()
    err = p.stderr.read().decode("utf-8", "ignore")
    p.stdout.close()
    p.stderr.close()
    p.wait()
    if not data:
        sys.exit("没读到音频。可能这个文件没有音轨，或者 ffmpeg 报错：\n" + err[:800])
    return data


def normalize(data, target_peak=0.89):
    """峰值归一化。手机录的视频音量常常只有满量程的 1/4，
    直接播在 MAX98357A + 小喇叭上几乎听不见，所以默认拉到 -1dB 左右。"""
    a = array.array("h")
    a.frombytes(data[:len(data) // 2 * 2])
    peak = 0
    for v in a:
        if v > peak:
            peak = v
        elif -v > peak:
            peak = -v
    if peak == 0:
        return data
    want = int(32767 * target_peak)
    if peak >= want:
        return data
    g = want / float(peak)
    for i, v in enumerate(a):
        s = int(v * g)
        if s > 32767:
            s = 32767
        elif s < -32768:
            s = -32768
        a[i] = s
    return a.tobytes()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--rate", type=int, default=16000)
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--duration", type=float, default=10.0)
    ap.add_argument("--gain", type=float, default=0.0,
                    help="手动音量倍数，0 = 自动峰值归一化")
    ap.add_argument("--max-bytes", type=int, default=900000)
    ap.add_argument("--name", default="AUDIO_DEMO")
    ap.add_argument("--out-dir", default="main")
    args = ap.parse_args()

    print("读取音轨 …")
    ff = find_ffmpeg()
    pcm = grab_pcm(ff, args.src, args.rate, args.start, args.duration)

    pcm = pcm[:len(pcm) // 2 * 2]                 # 保证是整数个 16bit 样本
    if len(pcm) > args.max_bytes:
        pcm = pcm[:args.max_bytes // 2 * 2]
        print("  超出上限，已截断到 {} 字节".format(len(pcm)))

    if args.gain > 0:
        a = array.array("h")
        a.frombytes(pcm)
        for i, v in enumerate(a):
            s = int(v * args.gain)
            a[i] = 32767 if s > 32767 else (-32768 if s < -32768 else s)
        pcm = a.tobytes()
        print("  已按 {:.1f}x 放大".format(args.gain))
    else:
        pcm = normalize(pcm)

    samples = len(pcm) // 2
    seconds = samples / float(args.rate)
    print("  {} 个样本 / {} 字节 / {:.2f} 秒 @ {}Hz 单声道 16bit".format(
        samples, len(pcm), seconds, args.rate))

    name = args.name

    h_lines = [
        "/* 由 tools/audio2c.py 自动生成，请勿手工修改 */",
        "#ifndef __AUDIO_DATA_H",
        "#define __AUDIO_DATA_H",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint32_t       sample_rate;   /* 采样率 Hz */",
        "    uint32_t       samples;       /* 样本个数（单声道 16bit） */",
        "    const int16_t *data;          /* 指向固件里内嵌的 PCM */",
        "} audio_t;",
        "",
        "/* {}Hz · 单声道 · 16bit · {} 个样本 · {:.2f} 秒 */".format(
            args.rate, samples, seconds),
        "extern const audio_t {};".format(name),
        "",
        "#endif /* __AUDIO_DATA_H */",
        "",
    ]

    c_lines = [
        "/* 由 tools/audio2c.py 自动生成，请勿手工修改",
        " * 源文件: {}".format(os.path.basename(args.src)),
        " * {}Hz 单声道 16bit · {} 个样本 · {:.2f} 秒 · {:,} 字节".format(
            args.rate, samples, seconds, len(pcm)),
        " *",
        " * PCM 裸数据放在同目录 audio_data.bin，由 CMakeLists.txt 的",
        " * EMBED_FILES 直接嵌进固件（和视频一个套路）。",
        " */",
        '#include "audio_data.h"',
        "",
        'extern const uint8_t audio_data_bin_start[] asm("_binary_audio_data_bin_start");',
        "",
        "const audio_t {} = {{".format(name),
        "    {},          /* sample_rate */".format(args.rate),
        "    {},        /* samples */".format(samples),
        "    (const int16_t *)audio_data_bin_start,",
        "};",
        "",
    ]

    hpath = os.path.join(args.out_dir, "audio_data.h")
    cpath = os.path.join(args.out_dir, "audio_data.c")
    bpath = os.path.join(args.out_dir, "audio_data.bin")

    with open(bpath, "wb") as f:
        f.write(pcm)
    with open(hpath, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(h_lines))
    with open(cpath, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(c_lines))

    print("已生成 {}, {} 和 {}".format(hpath, cpath, bpath))


if __name__ == "__main__":
    main()
