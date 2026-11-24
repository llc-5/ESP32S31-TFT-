# -*- coding: utf-8 -*-
"""安装 / 校验网站生成的图片头文件。

为什么不让你直接复制粘贴
------------------------
这个工程踩过好几次「以为换了图、其实没换」：
  - 存到了别的目录（比如自己另存的工程副本）
  - 编辑器里改了但没保存
  - 保存了但编译没重跑，烧进去的还是旧 bin
所以做成一条命令：它会把文件装到位、顺便报出这份图到底是什么、和原来那份是不是同一份。

用法
----
    # 装一份新生成的图（会先备份现有的）
    python tools/install_image.py C:\\Users\\你\\Downloads\\image_avatar.h

    # 只看现在工程里装的是哪张图，不动任何文件
    python tools/install_image.py --show

    # 只检查一份文件能不能用，不安装
    python tools/install_image.py --check 某个文件.h
"""
import argparse
import hashlib
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
TARGET = os.path.join(PROJ, "main", "image_avatar.h")

# 网站会生成这些宏，缺一个就说明这份文件不是他要的格式
REQUIRED = ["IMG_AVATAR_W", "IMG_AVATAR_H", "IMG_AVATAR_DW", "IMG_AVATAR_DH"]
OPTIONAL = ["IMG_AVATAR_BPP", "IMG_AVATAR_COLORS", "IMG_AVATAR_BYTES", "IMG_AVATAR_ROT"]


def read_defines(path):
    """把 #define XXX 数字 的宏全抓出来"""
    txt = open(path, "r", encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+(\d+)\s*$", txt, re.M):
        out[m.group(1)] = int(m.group(2))
    return out, txt


def signature(path):
    """返回这份图片头文件的特征：文件 md5 + 关键参数 + 数据区校验和"""
    md5 = hashlib.md5(open(path, "rb").read()).hexdigest()
    defs, txt = read_defines(path)

    # 抓位打包数组里的十六进制字节，算 FNV-1a，和固件里打的那条日志对得上
    m = re.search(r"IMG_AVATAR_PACKED\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;", txt, re.S)
    checksum = None
    if m:
        body = m.group(1)
        try:
            vals = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{1,2})", body)]
            h = 2166136261
            for b in vals:
                h = ((h ^ b) * 16777619) & 0xFFFFFFFF
            checksum = h
        except ValueError:
            checksum = None
    return md5, defs, checksum, txt


def describe(path, label):
    md5, defs, ck, txt = signature(path)
    print("── %s ──" % label)
    print("  路径    : %s" % path)
    print("  大小    : {:,} 字节".format(os.path.getsize(path)))
    print("  文件MD5 : %s" % md5)
    miss = [k for k in REQUIRED if k not in defs]
    if miss:
        print("  [!!] 缺少必需宏: %s" % ", ".join(miss))
    w = defs.get("IMG_AVATAR_W")
    h = defs.get("IMG_AVATAR_H")
    dw = defs.get("IMG_AVATAR_DW")
    dh = defs.get("IMG_AVATAR_DH")
    if w and h:
        print("  源图    : %d x %d" % (w, h))
    if dw and dh:
        print("  屏上    : %d x %d" % (dw, dh))
    if "IMG_AVATAR_COLORS" in defs and "IMG_AVATAR_BPP" in defs:
        print("  格式    : %d 色 / %d bpp" % (defs["IMG_AVATAR_COLORS"],
                                             defs["IMG_AVATAR_BPP"]))
    if "IMG_AVATAR_BYTES" in defs:
        print("  数据    : {:,} 字节".format(defs["IMG_AVATAR_BYTES"]))
    if ck is not None:
        print("  数据校验和: 0x%08X   <- 烧完串口里应该打这个值" % ck)
    else:
        print("  数据校验和: （这份不是 4bpp 位打包格式，串口不会打印校验和）")

    # 结构自检
    if "IMG_Draw" not in txt and "LCD_ShowImage256Fit" not in txt:
        print("  [!!] 文件里找不到 IMG_Draw，也不含写屏调用，可能不是网站生成的")
    if "IMG_AVATAR_PALETTE" not in txt:
        print("  [!!] 找不到调色板数组 IMG_AVATAR_PALETTE")
    if '#include "ili9341.h"' not in txt.replace("'", '"'):
        print("  [!] 没有 #include \"ili9341.h\"，编译可能会报缺少 LCD_ShowImage256")
    return md5, miss, defs, ck


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", nargs="?", help="要安装的 image_avatar.h 路径")
    ap.add_argument("--show", action="store_true", help="只看工程里现在装的是哪张")
    ap.add_argument("--check", metavar="文件", help="只检查，不安装")
    ap.add_argument("--force", action="store_true", help="内容和现有的一样也照样安装")
    args = ap.parse_args()

    if args.check:
        describe(os.path.abspath(args.check), "待检查文件")
        return

    if args.show or not args.src:
        if not os.path.exists(TARGET):
            sys.exit("工程里还没有 %s" % TARGET)
        describe(TARGET, "工程当前编译进固件的图片")
        print("───────────────────────────────────────────")
        print("要换图：python tools/install_image.py <网站生成的 .h 路径>")
        return

    src = os.path.abspath(args.src)
    if not os.path.exists(src):
        sys.exit("找不到文件: %s" % src)
    if not os.path.exists(TARGET):
        sys.exit("工程里找不到 %s，先确认工程路径对不对" % TARGET)

    new_md5, miss, new_defs, new_ck = describe(src, "准备安装的图")
    old_md5 = hashlib.md5(open(TARGET, "rb").read()).hexdigest()

    print()
    if miss:
        sys.exit("这份文件缺 %s，装进去也编译不过，已中止。" % ", ".join(miss))

    if new_md5 == old_md5 and not args.force:
        print("[!!] 这份文件和工程里现有的**一模一样**，装进去等于没换。")
        print("     确认你真的重新生成过图片（在网站上换了图/改了参数再导出）。")
        print("     如果确实就要这份，加 --force 强制安装。")
        sys.exit(1)

    # 备份 + 安装
    stamp = time.strftime("%Y%m%d_%H%M%S")
    backup = TARGET + ".bak_" + stamp
    shutil.copy2(TARGET, backup)
    shutil.copy2(src, TARGET)
    print("[OK] 已备份旧文件 -> %s" % os.path.basename(backup))
    print("[OK] 已安装新文件 -> %s" % TARGET)
    print()
    print("下一步：")
    print("  1) 重新编译烧录：  .\\flash.bat")
    print("  2) 看串口里这一行，确认里面装的是新图：")
    if new_ck is not None:
        print("       APP: image: 数据校验和 0x%08X" % new_ck)
    print("       应该是 源图 %sx%s -> 屏上 %sx%s" % (
        new_defs.get("IMG_AVATAR_W"), new_defs.get("IMG_AVATAR_H"),
        new_defs.get("IMG_AVATAR_DW"), new_defs.get("IMG_AVATAR_DH")))
    print("  3) 校验和/尺寸对不上 = 烧的不是这份代码，别怀疑屏幕")


if __name__ == "__main__":
    main()
