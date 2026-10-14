# -*- coding: utf-8 -*-
"""gbk2utf8.py — 把桌面版（GBK + CRLF）的源码转成 Web 版用的 UTF-8（仍是 CRLF）。

Emscripten / clang 默认按 UTF-8 读源码，GBK 会直接报错或把中文字符串变乱码，
所以迁移第一步就是把全部 .cpp / .h 转码。

用法:
    python gbk2utf8.py <源目录> <目标目录>
    python gbk2utf8.py --inplace <目录>

规则:
    - 只处理 .cpp / .h / .hpp，其他文件原样复制
    - 目标文件已经是合法 UTF-8 的，原地跳过（复制过去，不做二次解码）
    - 行尾统一保持 CRLF，不改变行数
"""
import os
import shutil
import sys

SRC_EXT = {'.cpp', '.h', '.hpp'}


def decode_source(raw: bytes):
    """返回 (文本, 原始编码名)。优先按 UTF-8 试，失败退回 GBK。"""
    try:
        return raw.decode('utf-8'), 'utf-8'
    except UnicodeDecodeError:
        pass
    return raw.decode('gbk'), 'gbk'


def normalize_eol(text: str) -> str:
    """统一成 CRLF，且不产生 CR CR LF。"""
    return text.replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\r\n')


def convert_file(src_path: str, dst_path: str):
    with open(src_path, 'rb') as f:
        raw = f.read()
    text, enc = decode_source(raw)
    text = normalize_eol(text)
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, 'wb') as f:
        f.write(text.encode('utf-8'))
    return enc, len(text.split('\r\n'))


def walk(src_dir: str, dst_dir: str, inplace: bool):
    stats = {'utf-8': 0, 'gbk': 0, 'copied': 0}
    for dirpath, dirnames, filenames in os.walk(src_dir):
        dirnames[:] = [d for d in dirnames if d not in ('.git', 'x64', '.vs', '__pycache__')]
        for name in filenames:
            src_path = os.path.join(dirpath, name)
            rel = os.path.relpath(src_path, src_dir)
            dst_path = src_path if inplace else os.path.join(dst_dir, rel)
            ext = os.path.splitext(name)[1].lower()
            if ext in SRC_EXT:
                enc, lines = convert_file(src_path, dst_path)
                stats[enc] += 1
                print('[%s] %s  (%d lines)' % (enc, rel, lines))
            else:
                if not inplace:
                    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
                    shutil.copy2(src_path, dst_path)
                stats['copied'] += 1
    return stats


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    inplace = '--inplace' in argv
    if inplace:
        if len(args) != 1:
            print(__doc__)
            return 2
        stats = walk(args[0], args[0], True)
    else:
        if len(args) != 2:
            print(__doc__)
            return 2
        stats = walk(args[0], args[1], False)
    print('---')
    print('utf-8 源码: %d   gbk 源码: %d   其他复制: %d' % (
        stats['utf-8'], stats['gbk'], stats['copied']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))