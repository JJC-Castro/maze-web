# -*- coding: utf-8 -*-
"""make_font_subset.py —— 按 Config.h 的 UI_GLYPHS 裁剪中文字体。

Web 版必须自带字体（浏览器里没有"系统字体文件"），但完整的思源黑体有 10MB+，
里面 99% 的字这个游戏一辈子都用不到。这里把 UI_GLYPHS 里列出的汉字全部提出来，
再外加一份"运行时可能出现的字"（用户名 / 日期），用 fontTools 裁一个子集出来。

产物：assets/fonts/NotoSansSC-Subset.ttf（通常几百 KB）

用法：
    python tools/make_font_subset.py
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_FONT = os.path.join(ROOT, 'assets', 'fonts', 'NotoSansSC-VF.ttf')
OUT_FONT = os.path.join(ROOT, 'assets', 'fonts', 'NotoSansSC-Subset.ttf')
CONFIG = os.path.join(ROOT, 'src', 'Config.h')

# 用户名是玩家自己敲的、日期是系统给的，这些字不在 UI_GLYPHS 里也要带上。
# 全量收进来会让子集膨胀，所以只放最常见的那一批。
EXTRA = (
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ "
    "年月日时分秒"
    "赵钱孙李周吴郑王冯陈褚卫蒋沈韩杨朱秦尤许何吕施张孔曹严华金魏陶姜"
    "一乙二十丁厂七卜人入八九几儿了力乃刀又三于干亏士土才寸下大丈与万上小口巾山千乞川亿个勺久凡及夕丸么广亡门义之尸弓己已子卫也女飞刃习叉马乡"
)


def collect_glyphs():
    text = io.open(CONFIG, encoding='utf-8').read()
    # UI_GLYPHS 是个续行宏：把所有 "..." 拼起来
    m = re.search(r'#define\s+UI_GLYPHS\s*(.*?)\n\n', text, re.S)
    if not m:
        raise SystemExit('Config.h 里没找到 UI_GLYPHS')
    body = m.group(1)
    chars = set()
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', body):
        chars.update(lit)
    chars.update(EXTRA)
    return chars


def main():
    if not os.path.isfile(SRC_FONT):
        raise SystemExit('缺源字体：%s\n先下载 NotoSansSC-VF.ttf 放进 assets/fonts/' % SRC_FONT)

    try:
        from fontTools import subset
        from fontTools.ttLib import TTFont
    except ImportError:
        raise SystemExit('需要 fontTools：pip install fonttools')

    chars = sorted(c for c in collect_glyphs() if ord(c) >= 32)
    print('UI_GLYPHS + 运行时常用字 共 %d 个不同字符' % len(chars))

    # 变量字体先取默认实例再裁，否则 stb_truetype 读到的可能是错的轮廓
    font = TTFont(SRC_FONT)

    opts = subset.Options()
    opts.layout_features = ['*']
    opts.name_IDs = ['*']
    opts.notdef_outline = True
    opts.recalc_bounds = True
    opts.drop_tables += ['DSIG']
    # 关键：把变量字体实例化成静态字形，raylib 的 stb_truetype 不解 fvar
    opts.retain_gids = False

    subsetter = subset.Subsetter(options=opts)
    subsetter.populate(text=''.join(chars))
    subsetter.subset(font)

    # 丢掉 fvar / gvar / avar，避免 stb 按"默认轴"渲染出歪掉的字
    for tag in ('fvar', 'gvar', 'avar', 'STAT', 'HVAR', 'VVAR', 'MVAR', 'cvar'):
        if tag in font:
            del font[tag]

    font.save(OUT_FONT)

    src_kb = os.path.getsize(SRC_FONT) / 1024.0
    out_kb = os.path.getsize(OUT_FONT) / 1024.0
    print('源字体 %.0f KB -> 子集 %.0f KB (%.1f%%)' % (src_kb, out_kb, out_kb * 100.0 / src_kb))
    print('输出：%s' % OUT_FONT)


if __name__ == '__main__':
    main()