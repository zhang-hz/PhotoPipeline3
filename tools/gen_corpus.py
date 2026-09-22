#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline M0 golden corpus generator (frozen contract: docs/m0-tasks.md §12.4).

usage: python tools/gen_corpus.py [GOLDEN_ROOT=tests/golden]
  OIIOTOOL      default vcpkg_installed/<triplet>/tools/openimageio/oiiotool (env override)
  PP_MKFIXTURES default build/release/pp_mkfixtures (env override)

Idempotent: every output is overwritten on re-run.

M3-W3 平台增量（裁定 D3：共享脚本 Python 单实现，本文件替代 tools/gen_corpus.sh）:
  * 可执行文件探测 name 与 name + '.exe' 双探测（Windows 上产物带 .exe）
  * 默认 vcpkg 已装目录 = vcpkg_installed/x64-windows（Windows）/ x64-linux（POSIX）
  * `head -c 300` → 读前 300 字节；`sha256sum` 输出格式保持 `<hex>  <path>`
    （两空格、正斜杠路径、相对 GOLDEN_ROOT）
  其余（13 个 pattern fixture + multi.tif/anim.gif/jxl8.jxl + meta/ + edge/ + CHECKSUMS、
  全部 `gen_corpus: ...` 输出行与退出码）与 bash 版逐字一致。
"""

import hashlib
import os
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRIPLET = 'x64-windows' if os.name == 'nt' else 'x64-linux'

_arg = sys.argv[1] if len(sys.argv) > 1 else ''
if _arg:
    GOLDEN_ROOT = _arg
elif os.environ.get('GOLDEN_ROOT'):
    GOLDEN_ROOT = os.environ['GOLDEN_ROOT']
else:
    GOLDEN_ROOT = os.path.join(ROOT, 'tests', 'golden')

OIIOTOOL = os.environ.get('OIIOTOOL') or os.path.join(
    ROOT, 'vcpkg_installed', TRIPLET, 'tools', 'openimageio', 'oiiotool')
PP_MKFIXTURES = os.environ.get('PP_MKFIXTURES') or os.path.join(
    ROOT, 'build', 'release', 'pp_mkfixtures')


def resolve_exe(path):
    """name 与 name + '.exe' 双探测（Windows 上实际产物带 .exe）；找不到 → None。"""
    for candidate in (path, path + '.exe'):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def run(cmd, stderr=None):
    return subprocess.run(cmd, stderr=stderr).returncode


def pattern(tool, out, channels, chnames, depth, cell='8'):
    """pattern <out> <channels> <chnames> <depth> [checker cell size]

    TIFF 存 DateTime 字段，oiiotool 否则填墙钟时间（跨次再生成字节不稳定，审计 D2）。
    `--eraseattrib DateTime` 无效；显式 --attrib 有效。
    """
    stamp = []
    if out.endswith('.tif'):
        stamp = ['--attrib', 'DateTime', '2024:01:01 00:00:00']
    return run([tool, '--pattern', 'checker:width={}:height={}'.format(cell, cell),
                '64x64', channels, '--chnames', chnames, '-d', depth] + stamp + ['-o', out])


def main():
    base = os.path.join(GOLDEN_ROOT, 'base')
    edge = os.path.join(GOLDEN_ROOT, 'edge')
    meta = os.path.join(GOLDEN_ROOT, 'meta')
    real = os.path.join(GOLDEN_ROOT, 'real')
    for directory in (base, edge, meta, real):
        os.makedirs(directory, exist_ok=True)

    oiio = resolve_exe(OIIOTOOL)
    if oiio is None:
        print('gen_corpus: oiiotool not executable: {}'.format(OIIOTOOL), file=sys.stderr)
        return 1
    mkfixtures = resolve_exe(PP_MKFIXTURES)
    if mkfixtures is None:
        print('gen_corpus: pp_mkfixtures not executable: {}'.format(PP_MKFIXTURES), file=sys.stderr)
        return 1

    print('gen_corpus: base/ (OIIOTOOL={})'.format(OIIOTOOL))
    for name, channels, chnames, depth in (
            ('rgb8.png', '3', 'R,G,B', 'uint8'),
            ('rgb16.png', '3', 'R,G,B', 'uint16'),
            ('gray8.png', '1', 'Y', 'uint8'),
            ('gray16.png', '1', 'Y', 'uint16'),
            ('graya8.png', '2', 'Y,A', 'uint8'),
            ('rgba8.png', '4', 'R,G,B,A', 'uint8'),
            ('rgba16.png', '4', 'R,G,B,A', 'uint16'),
            ('rgb8.tif', '3', 'R,G,B', 'uint8'),
            ('rgb16.tif', '3', 'R,G,B', 'uint16'),
            ('gray16.tif', '1', 'Y', 'uint16'),
            ('photo.jpg', '3', 'R,G,B', 'uint8'),
            ('targa.tga', '3', 'R,G,B', 'uint8'),
            ('bmp24.bmp', '3', 'R,G,B', 'uint8')):
        rc = pattern(oiio, os.path.join(base, name), channels, chnames, depth)
        if rc != 0:
            return rc

    # multi.tif: 两个子图。DateTime 必须在 --siappend **之前**逐子图 pin（--siappend 之后的
    # attrib 只作用于顶层 spec，子图时间戳会继续漂移 —— 审计 D2）。
    rc = run([oiio, '--pattern', 'checker:width=8:height=8', '64x64', '3', '--chnames', 'R,G,B',
              '-d', 'uint8', '--attrib', 'DateTime', '2024:01:01 00:00:00',
              '--pattern', 'checker:width=4:height=4', '32x32', '3', '--chnames', 'R,G,B',
              '-d', 'uint8', '--attrib', 'DateTime', '2024:01:01 00:00:00',
              '--siappend', '-o', os.path.join(base, 'multi.tif')])
    if rc != 0:
        return rc

    # anim.gif: 三个子图（动画）。
    rc = run([oiio, '--pattern', 'checker:width=8:height=8', '64x64', '3', '--chnames', 'R,G,B',
              '-d', 'uint8',
              '--pattern', 'checker:width=4:height=4', '64x64', '3', '--chnames', 'R,G,B',
              '-d', 'uint8',
              '--pattern', 'checker:width=2:height=2', '64x64', '3', '--chnames', 'R,G,B',
              '-d', 'uint8',
              '--siappend', '--siappend', '-o', os.path.join(base, 'anim.gif')])
    if rc != 0:
        return rc

    # jxl8.jxl: 由 oiiotool 写出；jxl 输出插件不可用时改用 pp_mkfixtures 的 meta/jxl_exif.jxl
    # （走哪条路下面会报告）。
    jxl_via = 'oiiotool'
    if run([oiio, '--pattern', 'checker:width=8:height=8', '64x64', '3', '--chnames', 'R,G,B',
            '-d', 'uint8', '-o', os.path.join(base, 'jxl8.jxl')],
           stderr=subprocess.DEVNULL) != 0:
        jxl_via = 'pp_mkfixtures(meta/jxl_exif.jxl)'

    print('gen_corpus: meta/ (PP_MKFIXTURES={})'.format(PP_MKFIXTURES))
    rc = run([mkfixtures, '--make', meta])
    if rc != 0:
        return rc

    if jxl_via != 'oiiotool':
        shutil.copyfile(os.path.join(meta, 'jxl_exif.jxl'), os.path.join(base, 'jxl8.jxl'))
    print('gen_corpus: base/jxl8.jxl produced via {}'.format(jxl_via))

    print('gen_corpus: edge/')
    # cmyk.tif 由 pp_mkfixtures 产出（libtiff 直写，PHOTOMETRIC_SEPARATED）。
    shutil.copyfile(os.path.join(meta, 'cmyk.tif'), os.path.join(edge, 'cmyk.tif'))
    with open(os.path.join(base, 'photo.jpg'), 'rb') as src:
        head = src.read(300)
    with open(os.path.join(edge, 'corrupt_trunc.jpg'), 'wb') as dst:
        dst.write(head)
    open(os.path.join(edge, 'corrupt_zero.png'), 'wb').close()
    shutil.copyfile(os.path.join(base, 'rgb8.png'), os.path.join(edge, '测试📸unicode.png'))

    print('gen_corpus: CHECKSUMS')
    relatives = []
    for directory in ('base', 'edge', 'meta'):
        for dirpath, _dirnames, filenames in os.walk(os.path.join(GOLDEN_ROOT, directory)):
            for filename in filenames:
                full = os.path.join(dirpath, filename)
                relatives.append(os.path.relpath(full, GOLDEN_ROOT).replace(os.sep, '/'))
    relatives.sort()
    lines = []
    for rel in relatives:
        digest = hashlib.sha256()
        with open(os.path.join(GOLDEN_ROOT, rel), 'rb') as fp:
            for chunk in iter(lambda: fp.read(1 << 20), b''):
                digest.update(chunk)
        lines.append('{}  {}'.format(digest.hexdigest(), rel))
    with open(os.path.join(GOLDEN_ROOT, 'CHECKSUMS'), 'w', encoding='utf-8', newline='\n') as fp:
        fp.write(''.join(line + '\n' for line in lines))

    print('gen_corpus: OK - {} fixtures in {} (CHECKSUMS written)'.format(
        len(relatives), GOLDEN_ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
