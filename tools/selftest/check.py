# -*- coding: utf-8 -*-
"""check.py —— 用 Python 的 hashlib/hmac 复核 selftest.exe 的输出。

用法：
    selftest.exe > out.txt
    python tools/selftest/check.py out.txt
"""
import hashlib
import hmac
import io
import sys

KEY = b'maze-web-2f8a1c9d4e6b7a30-change-me-on-deploy'
# 真实密钥在 C++ 里：和 src/Leaderboard.cpp 的 kSignKey 一致
SIGN_KEY = 'maze-web-2f8a1c9d4e6b7a30-change-me-on-deploy'


def expect():
    lines = {}
    lines['SHA  empty'] = hashlib.sha256(b'').hexdigest()
    lines['SHA  abc'] = hashlib.sha256(b'abc').hexdigest()
    lines['SHA  block56'] = hashlib.sha256(
        b'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq').hexdigest()
    lines['SHA  a1000'] = hashlib.sha256(b'a' * 1000).hexdigest()

    def hm(key, msg):
        return hmac.new(key, msg, hashlib.sha256).hexdigest()

    lines['HMAC fox'] = hm(b'key', b'The quick brown fox jumps over the lazy dog')
    lines['HMAC jefe'] = hm(b'Jefe', b'what do ya want for nothing?')
    lines['HMAC key20'] = hm(b'a' * 20, b'Hi There')
    lines['HMAC key131'] = hm(b'a' * 131,
                              b'Test Using Larger Than Block-Size Key - Hash Key First')
    lines['HMAC utf8'] = hm(b'maze', '奶龙|神圣模式|排行榜'.encode('utf-8'))

    canonical = 'JJC|holy|score|nailong|4242|%.3f' % 123.4567
    lines['CANON'] = canonical
    lines['SIGN'] = hmac.new(SIGN_KEY.encode('utf-8'), canonical.encode('utf-8'),
                             hashlib.sha256).hexdigest()
    return lines


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'out.txt'
    want = expect()
    got = {}
    raw = open(path, 'rb').read()
    # PowerShell 的 `>` 默认写 UTF-16LE，手滑重定向时别让解析全挂
    if raw[:2] in (b'\xff\xfe', b'\xfe\xff'):
        text = raw.decode('utf-16')
    else:
        text = raw.decode('utf-8', 'replace')
    for line in text.splitlines():
        line = line.rstrip('\r\n')
        if not line:
            continue
        # 行格式：<名字(14列)> <值>
        name = line[:20].strip()
        val = line[20:].strip()
        got[name] = val

    bad = 0
    for name, w in want.items():
        g = got.get(name)
        ok = (g == w)
        print('[%s] %-12s %s' % ('ok ' if ok else 'BAD', name, g if g else '(缺)'))
        if not ok:
            print('      期望 %s' % w)
            bad += 1
    print('---')
    print('%d 项全部一致' % len(want) if bad == 0 else '%d 项不一致' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())