# -*- coding: utf-8 -*-
"""
把任意图片转换成 ESP32 可直接显示的 RGB565 C 数组。

用法：
    python tools/img2c.py <输入图片> <输出.c文件> <数组名>

例子：
    python tools/img2c.py avatar.png main/img_avatar.c IMG_AVATAR

生成的文件里是一个 img_t 结构体，配合 main/image.c 的 image_draw() 使用。

参数说明：
    图片会自动缩放到 <= 240x320（屏幕尺寸），保持比例，不会变形。
    想保留原始尺寸请用 --no-resize。
"""
import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("缺少 Pillow：请先执行  pip install pillow")

MAX_W = 240
MAX_H = 320


def to_rgb565(r, g, b):
    """RGB888 -> RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("name")
    ap.add_argument("--no-resize", action="store_true")
    ap.add_argument("--bg", default="0,0,0",
                    help="透明像素替换成的背景色，默认 0,0,0（黑）")
    args = ap.parse_args()

    im = Image.open(args.src)
    has_alpha = im.mode in ("RGBA", "LA") or \
        (im.mode == "P" and "transparency" in im.info)

    # 透明部分先铺一层背景色，否则转 RGB 会变黑或发白
    if has_alpha:
        im = im.convert("RGBA")
        bg = tuple(int(v) for v in args.bg.split(","))
        canvas = Image.new("RGBA", im.size, bg + (255,))
        canvas.alpha_composite(im)
        im = canvas.convert("RGB")
    else:
        im = im.convert("RGB")

    if not args.no_resize and (im.width > MAX_W or im.height > MAX_H):
        im.thumbnail((MAX_W, MAX_H), Image.LANCZOS)

    w, h = im.size
    px = im.load()

    values = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            values.append(to_rgb565(r, g, b))

    guard = os.path.basename(args.dst).upper().replace(".", "_") + "_"
    lines = []
    lines.append("/* 由 tools/img2c.py 自动生成，请勿手工修改")
    lines.append(" * 源图: %s" % os.path.basename(args.src))
    lines.append(" * 尺寸: %dx%d  RGB565  %d 字节" % (w, h, w * h * 2))
    lines.append(" */")
    lines.append('#include "img_data.h"')
    lines.append("")
    lines.append("static const uint16_t %s_PX[%d] = {" % (guard.rstrip("_"), w * h))
    for i in range(0, len(values), 12):
        row = ", ".join("0x%04X" % v for v in values[i:i + 12])
        lines.append("    " + row + ",")
    lines.append("};")
    lines.append("")
    lines.append("const img_t %s = { %d, %d, %s_PX };"
                 % (args.name, w, h, guard.rstrip("_")))
    lines.append("")

    with open(args.dst, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    print("已生成 %s  (%dx%d, %d 字节)" % (args.dst, w, h, w * h * 2))


if __name__ == "__main__":
    main()
