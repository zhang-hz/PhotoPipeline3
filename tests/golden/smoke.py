#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — golden smoke driver (M1-T8: docs/m1-tasks.md §3.16 / §4.8;
M2-T8: assertion level, docs/m2-tasks.md §4 T8 ②).

16 transcode pairs (9 M1 smoke pairs upgraded + 7 M2 assertion-level pairs). Every case runs
`photopipeline --dev` into .cache/out-smoke/<case>/ and is then asserted by pp_verify against
tests/golden/smoke/<case>.json (SCHEMA.md): pixels + metadata values (M2-T8 normalisation,
#26 landed) + the WarningKind list from the .pp.json sidecar.

usage: python tests/golden/smoke.py [BUILD_DIR]
  BUILD_DIR   default <repo>/build/release   (must be configured with -DPP_BUILD_DEV=ON)
  PP_BIN      default <BUILD_DIR>/photopipeline
  PP_VERIFY   default <BUILD_DIR>/pp_verify
  OUT_ROOT    default <repo>/.cache/out-smoke
exit code = number of failed cases (0 = all OK)

M3-W3（裁定 D3：Python 单实现，本文件替代 tests/golden/smoke.sh）:
  * PP_BIN/PP_VERIFY 按 name / name + '.exe' 双探测（Windows 产物带 .exe）
  * 冻结行 `SMOKE total=%d pass=%d fail=%d (out: %s)` 与全部消息逐字不变
"""

import os
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_arg = sys.argv[1] if len(sys.argv) > 1 else ''
if _arg:
    BUILD = _arg
elif os.environ.get('BUILD_DIR'):
    BUILD = os.environ['BUILD_DIR']
else:
    BUILD = os.path.join(ROOT, 'build', 'release')
PP_BIN = os.environ.get('PP_BIN') or os.path.join(BUILD, 'photopipeline')
PP_VERIFY = os.environ.get('PP_VERIFY') or os.path.join(BUILD, 'pp_verify')
GOLDEN = os.path.join(ROOT, 'tests', 'golden')
OUT_ROOT = os.environ.get('OUT_ROOT') or os.path.join(ROOT, '.cache', 'out-smoke')


def resolve_exe(path):
    """name 与 name + '.exe' 双探测（Windows 上实际产物带 .exe）；找不到 → None。"""
    for candidate in (path, path + '.exe'):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


# case | input (relative to tests/golden) | expected output (relative to the case dir) | extra CLI args
# tiff16-lzw goes through --preset on purpose: it covers the preset-file branch of §3.15.
# The `--meta Exif.Image.Artist=…` writes are deliberate: they give every pair a metadata *value*
# assertion (M2-T8 ②) without depending on sidecar/toolchain-specific tags (e.g. OIIO's Software).
CASES = [
    "jpeg-lossy|base/photo.jpg|base/photo.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0 --meta Exif.Image.Artist=M2-T8-jpeg",
    "jxl-lossless|base/rgb8.png|base/rgb8.jxl|--format jxl --tech modular --lossless --meta Exif.Image.Artist=M2-T8-jxl",
    "png16-lossless|base/rgb16.png|base/rgb16.png|--format png --bitdepth 16 --meta Exif.Image.Artist=M2-T8-png16",
    "tiff16-lzw|base/rgb16.tif|base/rgb16.tif|--preset {0}/smoke/tiff16-lzw.preset.json --meta Exif.Image.Artist=M2-T8-tiff16".format(GOLDEN),
    "webp-lossless|base/rgba8.png|base/rgba8.webp|--format webp --tech lossless --lossless --meta Exif.Image.Artist=M2-T8-webp",
    "heif-lossy|base/rgb8.png|base/rgb8.heic|--format heif --backend x265 --meta Exif.Image.Artist=M2-T8-heif",
    "avif-lossy|base/rgb8.png|base/rgb8.avif|--format avif --backend svt-av1 --meta Exif.Image.Artist=M2-T8-avif",
    "bmp-exact|base/rgb8.png|base/rgb8.bmp|--format bmp --bitdepth 24 --meta Exif.Image.Artist=M2-T8-bmp",
    "meta-artist|base/photo.jpg|base/photo.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0 --meta Exif.Image.Artist=M1-T8",
    "gray-webp|base/gray8.png|base/gray8.webp|--format webp --tech lossless --lossless --meta Exif.Image.Artist=M2-T8-gray",
    "alpha-jpeg|base/rgba8.png|base/rgba8.jpg|--format jpeg --backend jpegli --bitdepth 8 --meta Exif.Image.Artist=M2-T8-alpha",
    "depth-jpeg|base/rgb16.png|base/rgb16.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0 --meta Exif.Image.Artist=M2-T8-depth",
    "multipage-png|base/multi.tif|base/multi.png|--format png --bitdepth 8",
    "unicode-png|edge/测试📸unicode.png|edge/测试📸unicode.png|--format png --bitdepth 8 --meta Exif.Image.Artist=路径📸测试",
    "exif-roundtrip|meta/exif_full.jpg|meta/exif_full.jpg|--format jpeg --backend jpegli --bitdepth 8 --param distance=1.0",
    "metaonly-jpeg|meta/exif_full.jpg|meta/exif_full.jpg|--metadata-only --format jpeg --meta Exif.Image.Artist=M2-T8-only",
]


def main():
    binary = resolve_exe(PP_BIN)
    if binary is None:
        print('smoke: photopipeline not executable: {}'.format(PP_BIN), file=sys.stderr)
        print("smoke: build directory '{}' has no dev harness binary;".format(BUILD), file=sys.stderr)
        print('smoke: configure it with -DPP_BUILD_DEV=ON (a plain release build has no --dev).',
              file=sys.stderr)
        print('用法: bash tests/golden/smoke.sh [BUILD_DIR]', file=sys.stderr)
        return 2
    verify = resolve_exe(PP_VERIFY)
    if verify is None:
        print('smoke: pp_verify not executable: {}'.format(PP_VERIFY), file=sys.stderr)
        print('用法: bash tests/golden/smoke.sh [BUILD_DIR]', file=sys.stderr)
        return 2
    if not os.path.isdir(os.path.join(GOLDEN, 'base')):
        print('smoke: golden corpus missing: {}/base (run tools/gen_corpus.sh)'.format(GOLDEN),
              file=sys.stderr)
        return 2

    os.makedirs(OUT_ROOT, exist_ok=True)

    passed = 0
    failed = 0
    print('%-16s %-8s %s' % ('case', 'result', 'detail'))
    for entry in CASES:
        parts = entry.split('|')
        name = parts[0]
        input_rel = parts[1]
        out_rel = parts[2]
        args = '|'.join(parts[3:])
        outdir = os.path.join(OUT_ROOT, name)
        log = OUT_ROOT + '/' + name + '.dev.log'
        shutil.rmtree(outdir, ignore_errors=True)
        # 等价 bash 的不分词引用展开：`$args` 按空白切分。
        command = ([binary, '--dev', GOLDEN + '/' + input_rel, '--out', outdir,
                    '--base', GOLDEN, '--conflict', 'overwrite', '--workers', '1']
                   + args.split())
        with open(log, 'wb') as log_fp:
            rc = subprocess.run(command, stdout=log_fp, stderr=subprocess.STDOUT).returncode
        if rc != 0:
            print('%-16s %-8s --dev exit %d (log: %s)' % (name, 'FAIL', rc, log))
            failed += 1
            continue
        actual = outdir + '/' + out_rel
        check = subprocess.run([verify, GOLDEN + '/smoke/' + name + '.json', actual],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding='utf-8', errors='replace')
        result = check.stdout.rstrip('\n')
        if check.returncode == 0:
            print('%-16s %-8s %s' % (name, 'PASS', result))
            passed += 1
        else:
            print('%-16s %-8s %s' % (name, 'FAIL', result))
            failed += 1

    total = passed + failed
    print('SMOKE total=%d pass=%d fail=%d (out: %s)' % (total, passed, failed, OUT_ROOT))
    return failed


if __name__ == '__main__':
    sys.exit(main())
