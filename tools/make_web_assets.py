# -*- coding: utf-8 -*-
"""make_web_assets.py —— 给 Web 版生成"瘦身资源"。

为什么需要：
    首屏时长 = 下载 index.data 的时长，国内到 GitHub Pages 实测只有 82KB/s，
    桌面版 assets/ 原样打进去是 18MB，首屏要三分半。这里只做"零感知"瘦身：

    1) 音频：assets/audio/*.wav（48kHz/16bit/立体声 PCM，10.5MB）
       -> assets_web/audio/*.ogg（22050Hz Vorbis，音效单声道、bgm 保留立体声）
    2) 模型：assets/models/nailong.glb 里内嵌的那张 JPEG（1.76MB）是**死重量** ——
       raylib 6.0 的 rmodels.c 根本不读 bufferView 里的图（日志是
       "IMAGE: Data format not supported"），真正生效的是独立的
       assets/models/nailong_color.png，而且 ApplySidecarTexture() 是无条件
       覆盖材质的。所以直接把内嵌图从 glb 里摘掉，几何/材质一点不动。

    其余（png 贴图 / 字体 / RL 权重）压不动也不该压，不碰。

输出：
    assets_web/audio/*.ogg          （CMakeLists Web 分支单独 preload）
    assets_web/models/nailong.glb   （覆盖同名文件）
    桌面版仍用原版 assets/，一个字都不改。

用法：
    python tools/make_web_assets.py            # 生成/更新
    python tools/make_web_assets.py --check    # 只报告，不写文件
"""
import argparse
import glob
import json
import os
import struct
import sys
import wave

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_AUDIO = os.path.join(ROOT, 'assets', 'audio')
DST_AUDIO = os.path.join(ROOT, 'assets_web', 'audio')
SRC_MODEL = os.path.join(ROOT, 'assets', 'models', 'nailong.glb')
DST_MODEL = os.path.join(ROOT, 'assets_web', 'models', 'nailong.glb')

TARGET_RATE = 22050
KEEP_STEREO = ('bgm_',)      # 这些保留立体声

GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942
COMPONENT_SIZE = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
TYPE_COUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT2': 4, 'MAT3': 9, 'MAT4': 16}


# ---------------------------------------------------------------- 音频
def slim_audio(path, dst):
    with wave.open(path, 'rb') as w:
        ch, sw, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(w.getnframes())

    if sw != 2:
        raise SystemExit('%s：只处理 16bit PCM，实际 %d bit' % (path, sw * 8))

    x = np.frombuffer(raw, dtype='<i2').reshape(-1, ch).astype(np.float32) / 32768.0
    if rate != TARGET_RATE:                                   # 多相重采样，比线性插值干净
        x = resample_poly(x, TARGET_RATE, rate, axis=0)

    name = os.path.basename(path)
    if ch >= 2 and not name.startswith(KEEP_STEREO):          # 音效：折叠成单声道
        x = x.mean(axis=1, keepdims=True)

    sf.write(dst, np.clip(x, -1.0, 1.0), TARGET_RATE, format='OGG', subtype='VORBIS')
    return os.path.getsize(path), os.path.getsize(dst)


# ---------------------------------------------------------------- 模型
def strip_glb_texture(src, dst):
    data = open(src, 'rb').read()
    magic, version, total = struct.unpack('<III', data[:12])
    if magic != GLB_MAGIC:
        raise SystemExit('%s：不是 glb' % src)

    gltf, binary = None, None
    off = 12
    while off < total:
        clen, ctype = struct.unpack('<II', data[off:off + 8])
        payload = data[off + 8:off + 8 + clen]
        if ctype == CHUNK_JSON:
            gltf = json.loads(payload.decode('utf-8'))
        elif ctype == CHUNK_BIN:
            binary = payload
        off += 8 + clen
    if gltf is None or binary is None:
        raise SystemExit('%s：缺 JSON/BIN 块' % src)

    # 只在"没有任何 mesh/accessor 引用"的 bufferView 才敢摘
    img_views = {i['bufferView'] for i in gltf.get('images', []) if 'bufferView' in i}
    used = {a['bufferView'] for a in gltf.get('accessors', []) if 'bufferView' in a}
    drop = sorted(img_views - used)
    if not img_views:
        return None                                    # 已经是瘦身版 -> 幂等
    if not drop:
        raise SystemExit('%s：内嵌图被几何共用了，不能摘' % src)

    keep = [i for i in range(len(gltf['bufferViews'])) if i not in drop]
    remap = {old: new for new, old in enumerate(keep)}

    new_bin = bytearray()                              # 逐个 bufferView 拷出来，4 字节对齐
    new_views = []
    for old in keep:
        bv = gltf['bufferViews'][old]
        start = bv.get('byteOffset', 0)
        new_bin += b'\0' * ((-len(new_bin)) % 4)
        nb = dict(bv)
        nb['byteOffset'] = len(new_bin)
        new_bin += binary[start:start + bv['byteLength']]
        new_views.append(nb)

    gltf['bufferViews'] = new_views
    for a in gltf.get('accessors', []):
        if 'bufferView' in a:
            a['bufferView'] = remap[a['bufferView']]
    for b in gltf.get('buffers', []):
        b['byteLength'] = len(new_bin)

    gltf.pop('images', None)
    gltf.pop('textures', None)
    gltf.pop('samplers', None)
    for m in gltf.get('materials', []):
        pbr = m.get('pbrMetallicRoughness')
        if pbr:
            pbr.pop('baseColorTexture', None)
            pbr.pop('metallicRoughnessTexture', None)
        m.pop('normalTexture', None)
        m.pop('occlusionTexture', None)
        m.pop('emissiveTexture', None)

    js = json.dumps(gltf, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
    js += b' ' * ((-len(js)) % 4)
    bin_pad = bytes(new_bin) + b'\0' * ((-len(new_bin)) % 4)

    out = struct.pack('<III', GLB_MAGIC, 2, 12 + 8 + len(js) + 8 + len(bin_pad))
    out += struct.pack('<II', len(js), CHUNK_JSON) + js
    out += struct.pack('<II', len(bin_pad), CHUNK_BIN) + bin_pad

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, 'wb') as fp:
        fp.write(out)
    verify_glb(dst)
    return os.path.getsize(src), len(out)


def verify_glb(path):
    """自检：每个 accessor 的字节区间都必须落在自己的 bufferView 里。"""
    data = open(path, 'rb').read()
    magic, version, total = struct.unpack('<III', data[:12])
    gltf, binary, off = None, None, 12
    while off < total:
        clen, ctype = struct.unpack('<II', data[off:off + 8])
        payload = data[off + 8:off + 8 + clen]
        if ctype == CHUNK_JSON:
            gltf = json.loads(payload.decode('utf-8'))
        elif ctype == CHUNK_BIN:
            binary = payload
        off += 8 + clen

    assert total == len(data), 'GLB 总长对不上'
    for b in gltf['buffers']:
        assert b['byteLength'] <= len(binary), 'buffer 超出 BIN 块'
    for a in gltf.get('accessors', []):
        bv = gltf['bufferViews'][a['bufferView']]
        stride = bv.get('byteStride') or (COMPONENT_SIZE[a['componentType']] * TYPE_COUNT[a['type']])
        need = a.get('byteOffset', 0) + stride * (a['count'] - 1) + COMPONENT_SIZE[a['componentType']] * TYPE_COUNT[a['type']]
        assert need <= bv['byteLength'], 'accessor 越界'
    assert 'images' not in gltf and 'textures' not in gltf, '图片没摘干净'
    return True


# ---------------------------------------------------------------- 主流程
def clean_stale(dst_dir, keep_exts):
    """清掉本脚本上一轮生成、这轮已经不产出的格式（例如 OGG 化之后的 .wav）。"""
    removed = []
    if not os.path.isdir(dst_dir):
        return removed
    for f in sorted(glob.glob(os.path.join(dst_dir, '*'))):
        if os.path.isfile(f) and os.path.splitext(f)[1].lower() not in keep_exts:
            os.remove(f)
            removed.append(os.path.basename(f))
    return removed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()

    src_files = sorted(glob.glob(os.path.join(SRC_AUDIO, '*.wav')))
    if not src_files:
        raise SystemExit('没找到 %s/*.wav' % SRC_AUDIO)

    if not args.check:
        os.makedirs(DST_AUDIO, exist_ok=True)

    print('== 音频 -> OGG (22050Hz Vorbis) ==')
    tot_src = tot_dst = 0
    for f in src_files:
        dst = os.path.join(DST_AUDIO, os.path.splitext(os.path.basename(f))[0] + '.ogg')
        if args.check:
            n_src = os.path.getsize(f)
            n_dst = os.path.getsize(dst) if os.path.isfile(dst) else 0
        else:
            n_src, n_dst = slim_audio(f, dst)
        tot_src += n_src
        tot_dst += n_dst
        print('  %-32s %6dKB -> %6dKB' % (os.path.basename(f), n_src // 1024, n_dst // 1024))
    print('  音频合计 %.2f MB -> %.2f MB（省 %.0f%%）'
          % (tot_src / 1048576.0, tot_dst / 1048576.0, (1 - tot_dst / max(tot_src, 1)) * 100))

    print('== 模型 -> 摘掉内嵌死贴图 ==')
    if args.check:
        m_src = os.path.getsize(SRC_MODEL)
        m_dst = os.path.getsize(DST_MODEL) if os.path.isfile(DST_MODEL) else 0
    else:
        got = strip_glb_texture(SRC_MODEL, DST_MODEL)
        if got is None:
            m_src = m_dst = os.path.getsize(DST_MODEL)
        else:
            m_src, m_dst = got
    print('  %-32s %6dKB -> %6dKB' % (os.path.basename(SRC_MODEL), m_src // 1024, m_dst // 1024))

    if not args.check:
        stale = clean_stale(DST_AUDIO, {'.ogg'})
        for s in stale:
            print('  清掉过期产物 assets_web/audio/' + s)

    print('-' * 58)
    print('合计 %.2f MB -> %.2f MB' % ((tot_src + m_src) / 1048576.0, (tot_dst + m_dst) / 1048576.0))
    if not args.check:
        print('记得跑 tools/apply_web_audio_ogg.py（把 src 里的 .wav 引用改成 .ogg）')
    return 0


if __name__ == '__main__':
    sys.exit(main())