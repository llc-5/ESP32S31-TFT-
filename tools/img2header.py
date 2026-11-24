# -*- coding: utf-8 -*-
"""
把图片转换成 image_avatar.h —— 输出格式与「图片转数组网站
image2header_quality.html」完全一致，两边产物可以任意互换。

也就是说：你随时可以用网站重新生成一个 image_avatar.h 覆盖掉本文件，
main.c 和驱动一个字都不用改。

用法：
    python tools/img2header.py <图片> main/image_avatar.h [选项]

常用示例：
    # 48x48 头像，16 色 4bpp，放大到屏上 240x240
    python tools/img2header.py avatar.png main/image_avatar.h \
        --sw 48 --sh 48 --dw 240 --dh 240 --bpp 4

    # 全屏铺满，256 色 8bpp（网站默认档，驱动原生支持）
    python tools/img2header.py photo.jpg main/image_avatar.h \
        --sw 240 --sh 320 --dw 240 --dh 320 --bpp 8

参数：
    --sw/--sh  存进 Flash 的源图尺寸（越小越省空间）
    --dw/--dh  在屏幕上占的尺寸（最近邻放大，整数倍最清晰）
    --bpp      1 / 2 / 4 / 8，对应 2 / 4 / 16 / 256 色
    --dither   开 Floyd-Steinberg 抖动（低色数下观感明显更好）
    --rot      0 / 90 / 180 / 270，像素在数据里就转好
    --name     头文件里的前缀，默认 IMG_AVATAR
"""
import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("缺少 Pillow：请先执行  pip install pillow")

SCREEN_W, SCREEN_H = 240, 320
BLOCK_ROWS = 8
COLORS_OF_BPP = {8: 256, 4: 16, 2: 4, 1: 2}


def load_rgba(path, sw, sh, rot):
    """读图 → 缩放到 sw x sh → 按需旋转，返回 (RGBA 数据, 宽, 高)"""
    im = Image.open(path)
    if im.mode != "RGBA":
        im = im.convert("RGBA")
    im = im.resize((sw, sh), Image.LANCZOS)
    if rot:
        # PIL 的 rotate 是逆时针，这里换算成「顺时针 rot 度」
        im = im.rotate(-rot, expand=True)
    return im


def quantize(im, colors, dither):
    """量化到指定色数，返回 (索引列表, 调色板 RGB 三元组列表)"""
    method = Image.Quantize.MEDIANCUT
    dm = Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE
    q = im.convert("RGB").quantize(colors=colors, method=method, dither=dm)
    pal_raw = q.getpalette() or []
    pal = []
    for i in range(colors):
        r = pal_raw[i * 3] if i * 3 < len(pal_raw) else 0
        g = pal_raw[i * 3 + 1] if i * 3 + 1 < len(pal_raw) else 0
        b = pal_raw[i * 3 + 2] if i * 3 + 2 < len(pal_raw) else 0
        pal.append((r, g, b))
    return list(q.getdata()), pal


def pack(indices, w, h, bpp):
    """按 bpp 位打包：每行字节对齐，MSB 优先（与网站 pack() 一致）"""
    if bpp == 8:
        return bytes(i & 0xFF for i in indices)
    row_bytes = (w * bpp + 7) // 8
    out = bytearray(row_bytes * h)
    mask = (1 << bpp) - 1
    for y in range(h):
        base = y * w
        for x in range(w):
            bit = x * bpp
            shift = 8 - bpp - (bit & 7)
            out[y * row_bytes + (bit >> 3)] |= (indices[base + x] & mask) << shift
    return bytes(out)


def rgb565(p):
    r, g, b = p
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def hx(v, n):
    return format(v & 0xFFFFFFFF, "0{}X".format(n))


def make_header(name, sw, sh, dw, dh, bpp, colors, packed, pal565, dither, rot):
    L = []
    row_bytes = (sw * bpp + 7) // 8
    data_bytes = row_bytes * sh + colors * 2
    base_bytes = SCREEN_W * SCREEN_H * 2
    is_packed = bpp < 8

    L.append("/* ============================================================================")
    L.append(" * image_avatar.h  --  图片数据 + 解码（格式与 image2header_quality.html 完全一致）")
    L.append(" * {} 色 / {}bpp · 源图 {}x{} -> 屏上 {}x{} · 方向 {}°".format(colors, bpp, sw, sh, dw, dh, rot))
    L.append(" * 图片数据 {} 字节（未压缩全屏 16 位是 {} 字节，压缩比 {:.1f}x）".format(
        data_bytes, base_bytes, base_bytes / data_bytes))
    L.append(" * 只允许被 1 个 .c 文件 include（本工程是 main.c）。必须整文件替换。")
    if is_packed:
        L.append(" * 【本档为位打包格式】文件内自带 IMG_Draw() 解码，main.c / 驱动均无需改动。")
    L.append(" * ============================================================================ */")
    L.append("")
    L.append("#ifndef __IMAGE_AVATAR_H")
    L.append("#define __IMAGE_AVATAR_H")
    L.append("")
    L.append("#include <stdint.h>")
    L.append('#include "ili9341.h"   /* IMG_Draw 需要用 LCD_ShowImage256 写屏 */')
    L.append("")
    L.append("#define {}_W  {}".format(name, sw))
    L.append("#define {}_H  {}".format(name, sh))
    L.append("#define {}_DW  {}".format(name, dw))
    L.append("#define {}_DH  {}".format(name, dh))
    L.append("#define {}_IS_256  1".format(name))
    L.append("#define {}_ROT  {}   /* 数据已按此角度旋转，仅作记录 */".format(name, rot))
    L.append("#define {}_BPP  {}".format(name, bpp))
    L.append("#define {}_COLORS  {}".format(name, colors))
    L.append("#define {}_ROW_BYTES  {}".format(name, row_bytes))
    L.append("#define {}_PACKED_BYTES  {}".format(name, row_bytes * sh))
    L.append("#define {}_BYTES  {}".format(name, data_bytes))
    L.append("#define IMG_SCREEN_W  {}".format(SCREEN_W))
    L.append("#define IMG_SCREEN_H  {}".format(SCREEN_H))
    L.append("#define {}_X  ((IMG_SCREEN_W - {}_DW) / 2)".format(name, name))
    L.append("#define {}_Y  ((IMG_SCREEN_H - {}_DH) / 2)".format(name, name))
    L.append("")
    L.append("/* 调色板：{} 项 RGB565，共 {} 字节 */".format(colors, colors * 2))
    L.append("static const uint16_t {}_PALETTE[{}] = {{".format(name, colors))
    for i in range(0, len(pal565), 8):
        chunk = ["0x" + hx(v, 4) for v in pal565[i:i + 8]]
        L.append("    " + ",".join(chunk) + ("," if i + 8 < len(pal565) else ""))
    L.append("};")
    L.append("")

    if not is_packed:
        L.append("/* 8bpp：1 像素 1 字节，驱动 LCD_ShowImage256Fit 原生支持，无需解码 */")
        L.append("static const uint8_t {}[] = {{".format(name))
        for i in range(0, len(packed), 16):
            chunk = ["0x" + hx(v, 2) for v in packed[i:i + 16]]
            L.append("    " + ",".join(chunk) + ("," if i + 16 < len(packed) else ""))
        L.append("};")
        L.append("")
        L.append("/* 编译期完整性校验（不要删）：复制漏行会直接编译报错，而不是花屏 */")
        L.append("typedef char img_avatar_size_guard[(sizeof({})=={}_W*{}_H)?1:-1];".format(name, name, name))
    else:
        L.append("/* {}bpp 位打包：每行 {} 字节，行对齐，MSB 优先；共 {} 字节 */".format(
            bpp, row_bytes, row_bytes * sh))
        L.append("static const uint8_t {}_PACKED[] = {{".format(name))
        for i in range(0, len(packed), 16):
            chunk = ["0x" + hx(v, 2) for v in packed[i:i + 16]]
            L.append("    " + ",".join(chunk) + ("," if i + 16 < len(packed) else ""))
        L.append("};")
        L.append("")
        L.append("/* 编译期完整性校验（不要删）：复制漏行会直接编译报错，而不是花屏 */")
        L.append("typedef char img_avatar_size_guard[(sizeof({}_PACKED)=={}_PACKED_BYTES)?1:-1];".format(name, name))
        L.append("")
        L.append("/* ---------- 解码 + 写屏（只用驱动公开的 LCD_ShowImage256） ----------")
        L.append(" * 逐块把位打包数据还原成 8 位索引，放进 {} 字节的静态行缓冲，".format(SCREEN_W * BLOCK_ROWS))
        L.append(" * 再整块交给 LCD_ShowImage256 写 GRAM。块高 {} 行：只开窗 {} 次，".format(
            BLOCK_ROWS, (SCREEN_H + BLOCK_ROWS - 1) // BLOCK_ROWS))
        L.append(" * RAM 占用小、速度也够。源图 -> 屏上用整数最近邻映射，与驱动的 blit 完全一致。")
        L.append(" */")
        L.append("#define IMG_BLOCK_ROWS  {}".format(BLOCK_ROWS))
        L.append("static void IMG_Draw(void)")
        L.append("{")
        L.append("    static uint8_t line[{}_DW * IMG_BLOCK_ROWS];".format(name))
        L.append("    uint16_t y0, rows, r, x, sy, sx;")
        L.append("    uint32_t bit;")
        L.append("    const uint8_t *src;")
        L.append("    for (y0 = 0; y0 < {}_DH; y0 += IMG_BLOCK_ROWS) {{".format(name))
        L.append("        rows = {}_DH - y0;".format(name))
        L.append("        if (rows > IMG_BLOCK_ROWS) rows = IMG_BLOCK_ROWS;")
        L.append("        for (r = 0; r < rows; r++) {")
        L.append("            sy = (uint16_t)(((uint32_t)(y0 + r) * {}_H) / {}_DH);".format(name, name))
        L.append("            src = {}_PACKED + (uint32_t)sy * {}_ROW_BYTES;".format(name, name))
        L.append("            for (x = 0; x < {}_DW; x++) {{".format(name))
        L.append("                sx  = (uint16_t)(((uint32_t)x * {}_W) / {}_DW);".format(name, name))
        L.append("                bit = (uint32_t)sx * {}_BPP;".format(name))
        L.append("                line[(uint32_t)r * {}_DW + x] =".format(name))
        L.append("                    (uint8_t)((src[bit >> 3] >> (8 - {}_BPP - (bit & 7)))".format(name))
        L.append("                             & ({}_COLORS - 1));".format(name))
        L.append("            }")
        L.append("        }")
        L.append("        /* 加括号阻止同名函数式宏展开；main.c 的调用要走解码，这里要走真实驱动 */")
        L.append("        (LCD_ShowImage256)({}_X, {}_Y + y0,".format(name, name))
        L.append("                           {}_DW, rows, {}_PALETTE, line);".format(name, name))
        L.append("    }")
        L.append("}")
        L.append("")
        L.append("/* 自动适配：把 main.c 里原有的调用接到 IMG_Draw()，因此 main.c 一个字都不用改。")
        L.append(" * 宏放在文件末尾，IMG_Draw 内部又用 (LCD_ShowImage256)(...) 绕过宏，二者互不干扰。")
        L.append(" * 新版 main.c 走 LCD_ShowImage256Fit，旧版走 LCD_ShowImage256，两条都覆盖。 */")
        L.append("#define LCD_ShowImage256Fit(a,b,c,d,e,f,g,h)  IMG_Draw()")
        L.append("#define LCD_ShowImage256(a,b,c,d,e,f)         IMG_Draw()")

    L.append("")
    L.append("#endif /* __IMAGE_AVATAR_H */")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--sw", type=int, default=240)
    ap.add_argument("--sh", type=int, default=320)
    ap.add_argument("--dw", type=int, default=None)
    ap.add_argument("--dh", type=int, default=None)
    ap.add_argument("--bpp", type=int, default=4, choices=[1, 2, 4, 8])
    ap.add_argument("--dither", action="store_true", default=True)
    ap.add_argument("--no-dither", dest="dither", action="store_false")
    ap.add_argument("--rot", type=int, default=0, choices=[0, 90, 180, 270])
    ap.add_argument("--name", default="IMG_AVATAR")
    args = ap.parse_args()

    colors = COLORS_OF_BPP[args.bpp]
    im = load_rgba(args.src, args.sw, args.sh, args.rot)
    rw, rh = im.size

    dw = args.dw if args.dw else rw
    dh = args.dh if args.dh else rh

    indices, pal = quantize(im, colors, args.dither)
    packed = pack(indices, rw, rh, args.bpp)
    pal565 = [rgb565(p) for p in pal]

    text = make_header(args.name, rw, rh, dw, dh, args.bpp, colors,
                       packed, pal565, args.dither, args.rot)

    with open(args.dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    row_bytes = (rw * args.bpp + 7) // 8
    print("已生成 {}".format(args.dst))
    print("  源图(ROM) {}x{} · 屏上 {}x{} · {}色/{}bpp{}".format(
        rw, rh, dw, dh, colors, args.bpp, " + 抖动" if args.dither else ""))
    print("  每行 {} 字节，位打包 {} 字节，加调色板共 {} 字节".format(
        row_bytes, row_bytes * rh, row_bytes * rh + colors * 2))


if __name__ == "__main__":
    main()
