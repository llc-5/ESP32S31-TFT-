# -*- coding: utf-8 -*-
"""
把 mp4 / 视频文件转成 ESP32 能直接播放的帧数据（video_data.c + video_data.h）。

原理和图片一样：全部帧共用一张 RGB565 调色板，每帧存成「位打包索引图」。
默认 16 色 4bpp，一帧 240x320 只要 38400 字节。

用法：
    python tools/video2c.py <视频> [选项]

常用示例：
    # 240x320 全屏、8fps、16 色、取前 10 秒
    python tools/video2c.py demo.mp4 --fps 8 --duration 10

    # 想更省空间：降到 160x213、6fps
    python tools/video2c.py demo.mp4 --width 160 --height 213 --fps 6

    # 只限制总字节数（超出就自动砍帧数）
    python tools/video2c.py demo.mp4 --max-bytes 3000000

参数：
    --width/--height  帧尺寸，默认 240x320（屏幕原生分辨率）
    --fps             帧率，默认 8。屏上 SPI 10MHz 时 8~12fps 比较稳
    --colors          颜色数，默认 16（4bpp）。8 色=3bpp 不常用，4 色=2bpp 更省
    --start/--duration 从第几秒开始、取多少秒
    --max-bytes       数据总量上限，超了自动减少帧数
    --name            C 里的变量名前缀，默认 VID_DEMO
"""
import argparse
import os
import subprocess
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("缺少 Pillow：请先执行  pip install pillow")

COLORS_OF_BPP = {8: 256, 4: 16, 2: 4, 1: 2}
NAME_OF_COLORS = {256: 8, 16: 4, 4: 2, 2: 1}


def find_ffmpeg():
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


def grab_frames(ff, src, w, h, fps, start, duration, limit, fit):
    """让 ffmpeg 直接吐 rawvideo rgb24，边读边处理，不落一堆临时文件"""
    if fit == "cover":
        # 等比放大到铺满，再把多出来的边裁掉（不变形，推荐）
        vf = ("fps={},scale={}:{}:force_original_aspect_ratio=increase,"
              "crop={}:{}").format(fps, w, h, w, h)
    elif fit == "contain":
        # 等比缩小到能放下，四周补黑边
        vf = ("fps={},scale={}:{}:force_original_aspect_ratio=decrease,"
              "pad={}:{}:(ow-iw)/2:(oh-ih)/2:color=black").format(fps, w, h, w, h)
    else:
        # 直接拉伸铺满（会变形，但画面一点不丢）
        vf = "fps={},scale={}:{}:flags=lanczos".format(fps, w, h)

    cmd = [
        ff, "-hide_banner", "-loglevel", "error",
        "-ss", str(start), "-t", str(duration),
        "-i", src,
        "-vf", vf,
        "-pix_fmt", "rgb24",
        "-f", "rawvideo", "-",
    ]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    fsize = w * h * 3
    frames = []
    while True:
        buf = p.stdout.read(fsize)
        if not buf or len(buf) < fsize:
            break
        frames.append(Image.frombytes("RGB", (w, h), buf))
        if limit and len(frames) >= limit:
            break
    if p.poll() is None:
        p.stdout.close()
        p.kill()
    err = p.stderr.read().decode("utf-8", "ignore")
    p.stderr.close()
    p.wait()
    if not frames:
        sys.exit("一帧都没读出来。ffmpeg 输出：\n" + err[:800])
    return frames


def build_palette(frames, colors):
    """从若干帧里按原始像素抽样，做中位切分，得到共用调色板

    关键：绝对不能先把帧缩小再量化。缩小=相邻像素取平均，
    火焰、高光这类饱和色会被灰底色拉平，出来的调色板会变成一条灰阶，
    画面直接褪色。这里改成等间隔抽真实像素，一个字节都不改。
    """
    n = len(frames)
    pick = min(8, n)
    idxs = [int(i * (n - 1) / max(1, pick - 1)) for i in range(pick)] if pick > 1 else [0]

    samples = []
    for i in idxs:
        im = frames[i]
        w, h = im.size
        # 每帧大约抽 6000 个点：总采样量 5 万左右，量化够准且很快
        step = max(1, int(((w * h) / 6000.0) ** 0.5))
        for y in range(0, h, step):
            for x in range(0, w, step):
                samples.append(im.getpixel((x, y)))

    per_row = 64
    rows = (len(samples) + per_row - 1) // per_row
    sheet = Image.new("RGB", (per_row, rows), (0, 0, 0))
    sheet.putdata(samples + [(0, 0, 0)] * (per_row * rows - len(samples)))

    q = sheet.quantize(colors=colors, method=Image.Quantize.MEDIANCUT,
                       dither=Image.Dither.NONE)
    raw = q.getpalette() or []
    pal = []
    for i in range(colors):
        r = raw[i * 3] if i * 3 < len(raw) else 0
        g = raw[i * 3 + 1] if i * 3 + 1 < len(raw) else 0
        b = raw[i * 3 + 2] if i * 3 + 2 < len(raw) else 0
        pal.append((r, g, b))
    return pal


def build_lut(pal):
    """5-5-5 索引空间 -> 调色板下标，一次建好，之后每像素只查表"""
    lut = bytearray(32768)
    n = len(pal)
    for i in range(32768):
        r = (i >> 10) & 31
        g = (i >> 5) & 31
        b = i & 31
        best = 0
        bd = 1 << 30
        for k in range(n):
            pr, pg, pb = pal[k]
            dr = r - (pr >> 3)
            dg = g - (pg >> 3)
            db = b - (pb >> 3)
            d = dr * dr * 4 + dg * dg * 2 + db * db
            if d < bd:
                bd = d
                best = k
        lut[i] = best
    return lut


def frame_to_packed(im, w, h, bpp, lut):
    """RGB 帧 -> 位打包索引（每行对齐，高位优先，与网站/图片一致）"""
    px = im.load()
    row_bytes = (w * bpp + 7) // 8
    out = bytearray(row_bytes * h)
    mask = (1 << bpp) - 1
    for y in range(h):
        base = y * row_bytes
        for x in range(w):
            r, g, b = px[x, y]
            k = lut[((r & 0xF8) << 7) | ((g & 0xF8) << 2) | (b >> 3)]
            bit = x * bpp
            out[base + (bit >> 3)] |= (k & mask) << (8 - bpp - (bit & 7))
    return bytes(out)


def rgb565(p):
    r, g, b = p
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def hx(v, n):
    return format(v & 0xFFFFFFFF, "0{}X".format(n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--width", type=int, default=240)
    ap.add_argument("--height", type=int, default=320)
    ap.add_argument("--fps", type=int, default=8)
    ap.add_argument("--colors", type=int, default=16, choices=[256, 16, 4, 2])
    ap.add_argument("--start", type=float, default=0.0)
    ap.add_argument("--duration", type=float, default=10.0)
    ap.add_argument("--max-bytes", type=int, default=5000000)
    ap.add_argument("--fit", default="cover",
                    choices=["cover", "stretch", "contain"],
                    help="画面怎么填满屏幕：cover=裁边不变形(默认)，stretch=拉伸，contain=加黑边")
    ap.add_argument("--name", default="VID_DEMO")
    ap.add_argument("--out-dir", default="main")
    args = ap.parse_args()

    w, h = args.width, args.height
    bpp = NAME_OF_COLORS[args.colors]
    row_bytes = (w * bpp + 7) // 8
    fbytes = row_bytes * h
    limit = max(1, args.max_bytes // fbytes)

    print("读取视频 …")
    ff = find_ffmpeg()
    frames = grab_frames(ff, args.src, w, h, args.fps,
                         args.start, args.duration, limit, args.fit)
    frames = frames[:limit]
    n = len(frames)
    total = n * fbytes
    print("  抽到 {} 帧（上限 {} 帧），每帧 {} 字节，合计 {:,} 字节".format(
        n, limit, fbytes, total))

    print("生成调色板 …")
    pal = build_palette(frames, args.colors)
    lut = build_lut(pal)

    print("逐帧量化打包 …")
    blobs = []
    for i, f in enumerate(frames):
        blobs.append(frame_to_packed(f, w, h, bpp, lut))
        if (i + 1) % 10 == 0 or i + 1 == n:
            print("  {}/{}".format(i + 1, n))
    data = b"".join(blobs)

    name = args.name

    h_lines = []
    h_lines.append("/* 由 tools/video2c.py 自动生成，请勿手工修改 */")
    h_lines.append("#ifndef __VIDEO_DATA_H")
    h_lines.append("#define __VIDEO_DATA_H")
    h_lines.append("")
    h_lines.append('#include "video_show.h"')
    h_lines.append("")
    h_lines.append("/* {}x{} · {}fps · {}色 {}bpp · {} 帧 · {:,} 字节 */".format(
        w, h, args.fps, args.colors, bpp, n, len(data)))
    h_lines.append("extern const video_t {};".format(name))
    h_lines.append("")
    h_lines.append("#endif /* __VIDEO_DATA_H */")
    h_lines.append("")

    c_lines = []
    c_lines.append("/* 由 tools/video2c.py 自动生成，请勿手工修改")
    c_lines.append(" * 源文件: {}".format(os.path.basename(args.src)))
    c_lines.append(" * 尺寸 {}x{} · {}fps · {}色/{}bpp · 共 {} 帧 · {:,} 字节".format(
        w, h, args.fps, args.colors, bpp, n, len(data)))
    c_lines.append(" *")
    c_lines.append(" * 帧数据没有写成 C 数组，而是放在同目录的 video_data.bin 里，")
    c_lines.append(" * 由 CMakeLists.txt 的 EMBED_FILES 直接嵌进固件。")
    c_lines.append(" * 原因：{:,} 字节写成 0x.. 文本要 {:.0f}MB 的 .c 文件，".format(
        len(data), len(data) * 6 / 1024 / 1024))
    c_lines.append(" * 编辑器打开会卡死，编译器也要多花好几分钟解析。")
    c_lines.append(" * 想换视频：重跑 tools/video2c.py 覆盖 video_data.bin 即可。")
    c_lines.append(" */")
    c_lines.append('#include "video_data.h"')
    c_lines.append("")
    c_lines.append("/* 这两个符号由链接器根据 EMBED_FILES 自动生成，指向 .bin 的首尾 */")
    c_lines.append('extern const uint8_t video_data_bin_start[] asm("_binary_video_data_bin_start");')
    c_lines.append('extern const uint8_t video_data_bin_end[]   asm("_binary_video_data_bin_end");')
    c_lines.append("")
    c_lines.append("/* 全局调色板：{} 项 RGB565 */".format(args.colors))
    c_lines.append("static const uint16_t {}_PALETTE[{}] = {{".format(name, args.colors))
    pal565 = [rgb565(p) for p in pal]
    for i in range(0, len(pal565), 8):
        chunk = ["0x" + hx(v, 4) for v in pal565[i:i + 8]]
        c_lines.append("    " + ",".join(chunk) + ("," if i + 8 < len(pal565) else ""))
    c_lines.append("};")
    c_lines.append("")
    c_lines.append("const video_t {} = {{".format(name))
    c_lines.append("    /* w, h, frames, fps, bpp, colors */")
    c_lines.append("    {}, {}, {}, {}, {}, {},".format(w, h, n, args.fps, bpp, args.colors))
    c_lines.append("    {}_PALETTE,".format(name))
    c_lines.append("    video_data_bin_start");
    c_lines.append("};")
    c_lines.append("")

    hpath = os.path.join(args.out_dir, "video_data.h")
    cpath = os.path.join(args.out_dir, "video_data.c")
    bpath = os.path.join(args.out_dir, "video_data.bin")

    with open(bpath, "wb") as f:
        f.write(data)
    with open(hpath, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(h_lines))
    with open(cpath, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(c_lines))

    print("已生成 {}, {} 和 {}".format(hpath, cpath, bpath))
    print("  二进制 {:,} 字节 · 播放时长约 {:.1f} 秒".format(len(data), n / args.fps))


if __name__ == "__main__":
    main()
