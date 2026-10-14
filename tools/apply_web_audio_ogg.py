# -*- coding: utf-8 -*-
"""apply_web_audio_ogg.py —— 把 Web 版源码里的 .wav 引用改成 .ogg。

背景：tools/make_web_assets.py 把 assets/audio/*.wav 转成了
assets_web/audio/*.ogg（10.5MB -> 约 0.7MB）。raylib 的构建里
SUPPORT_FILEFORMAT_OGG=1，LoadSound/LoadMusicStream 按扩展名选解码器，
所以源码里那 26 个字面量得跟着改。

只改 Web 副本 src/，桌面仓库 codes/ 一个字都不动（那边继续用 wav 原版）。

用法：
    python tools/apply_web_audio_ogg.py            # 打补丁（可重复跑）
    python tools/apply_web_audio_ogg.py --check    # 只检查
"""
import argparse
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = ['src/SfxAudio.cpp', 'src/FootstepAudio.cpp']
WAV_RE = re.compile(r'"([A-Za-z0-9_]+)\.wav"')
OGG_RE = re.compile(r'"([A-Za-z0-9_]+)\.ogg"')
DST_AUDIO = os.path.join(ROOT, 'assets_web', 'audio')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()

    bad = 0
    total = 0
    for rel in TARGETS:
        path = os.path.join(ROOT, rel.replace('/', os.sep))
        text = io.open(path, encoding='utf-8', newline='').read()
        n_wav = len(WAV_RE.findall(text))
        n_ogg = len(OGG_RE.findall(text))
        total += n_wav + n_ogg

        if n_wav == 0 and n_ogg:
            print('  %-20s 已是 .ogg（%d 处）' % (rel, n_ogg))
            continue
        if n_wav == 0:
            print('  %-20s 没有任何 wav/ogg 引用 —— 锚点丢了？' % rel)
            bad += 1
            continue

        names = WAV_RE.findall(text)
        missing = [n for n in names if not os.path.isfile(os.path.join(DST_AUDIO, n + '.ogg'))]
        if missing:
            print('  %-20s 缺 ogg：%s' % (rel, ', '.join(missing)))
            bad += 1
            continue

        if args.check:
            print('  %-20s 待改 %d 处 -> .ogg' % (rel, n_wav))
            continue

        io.open(path, 'w', encoding='utf-8', newline='').write(WAV_RE.sub(r'"\1.ogg"', text))
        print('  %-20s 改了 %d 处 -> .ogg' % (rel, n_wav))

    if bad:
        print('有 %d 个文件没打成，先看上面' % bad)
        return 1
    print('OK：共 %d 处音频引用' % total)
    return 0


if __name__ == '__main__':
    sys.exit(main())