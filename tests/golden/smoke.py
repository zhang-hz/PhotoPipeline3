#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — golden smoke driver (M1-T8: docs/m1-tasks.md §3.16 / §4.8;
M2-T8: assertion level, docs/m2-tasks.md §4 T8 ②; M4-T5: 3 多格式对，docs/v0.3.0-design.md §4;
M4-T6: 1 progress-trace 对，docs/v0.3.0-design.md §7).

20 transcode pairs (9 M1 smoke pairs upgraded + 7 M2 assertion-level pairs + 3 M4 multi-format
pairs + 1 M4 progress-trace pair). Every case runs `photopipeline --dev` into
.cache/out-smoke/<case>/ and is then asserted by pp_verify against tests/golden/smoke/<case>.json
(SCHEMA.md): pixels + metadata values (M2-T8 normalisation, #26 landed) + the WarningKind list
from the .pp.json sidecar (+ M4-T6: the progress event stream).

usage: python tests/golden/smoke.py [BUILD_DIR] [--cases a,b,c]
  BUILD_DIR   default <repo>/build/release   (must be configured with -DPP_BUILD_DEV=ON)
  --cases     optional subset of case names (comma-separated); default = all pairs.
              PP_CASES=<a,b,c> in the environment is the equivalent switch (--cases wins).
  PP_BIN      default <BUILD_DIR>/photopipeline
  PP_VERIFY   default <BUILD_DIR>/pp_verify
  OUT_ROOT    default <repo>/.cache/out-smoke
exit code = number of failed cases (0 = all OK); 2 = usage error (unknown case name …)

M4-T4（D14 pr-fast 金样子集，docs/v0.3.0-design.md §12.3/§12.2）:
  * `--cases`/`PP_CASES` 只**筛选**用例，断言/容差/输出目录与全量完全同源（金样只增不减）。
  * 不带开关 = 全量，控制台输出逐字不变（含冻结行）。
  * 子集生效时追加一行 `smoke: 子集 …`，冻结行 `SMOKE total=…` 语义不变（total=选中例数）。

M4-T5（多格式对）:
  * case 行第 2 段支持 ';' 分隔多源；第 3 段支持 ',' 分隔多产物（多产物 → pp_verify 第二参数
    是用例输出根目录，逐产物按 expected.json 的 outputs[].rel 定位与断言）。
  * 单源单产物的 16 对行为逐字不变。

M4-T6（progress-trace 对，docs/v0.3.0-design.md §7）:
  * 新增第 20 对 `progress-trace`：产物断言同多格式对，另由 expected.json 的 `progress_trace`
    段断言 `<用例输出根目录>/progress-trace.jsonl`（--dev 侧车逐事件写出，schema 见 SCHEMA.md）：
    overall_frac 单调不倒退、synthetic 口径（jxl 真实 / webp 合成）、终态 done → 达 1.0。
  * 对其它 19 对的断言与命令**零改动**（进度侧车只是 --dev 的额外产物，不进断言面）。

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

USAGE = ("用法: python tests/golden/smoke.py [BUILD_DIR] [--cases a,b,c]\n"
         "      （子集开关等价环境变量: PP_CASES=a,b,c；不带开关 = 全量）\n")


def parse_argv(argv):
    """[BUILD_DIR] + --cases/--cases=<spec> 解析 → (build_arg, cases_spec)。

    历史命令行（`smoke.py <BUILD_DIR>`）行为逐字不变；未知选项 → usage 错误（exit 2）。
    """
    build = ''
    spec = None
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == '--cases':
            i += 1
            if i >= len(argv):
                print('smoke: --cases 缺少参数', file=sys.stderr)
                raise SystemExit(2)
            spec = argv[i]
        elif arg.startswith('--cases='):
            spec = arg.split('=', 1)[1]
        elif arg in ('-h', '--help'):
            sys.stdout.write(USAGE)
            raise SystemExit(0)
        elif arg.startswith('-'):
            print('smoke: 未知选项 {}'.format(arg), file=sys.stderr)
            sys.stderr.write(USAGE)
            raise SystemExit(2)
        elif build:
            print('smoke: 多余的参数 {}（BUILD_DIR 只接受一个）'.format(arg), file=sys.stderr)
            raise SystemExit(2)
        else:
            build = arg
        i += 1
    return build, spec


def select_cases(spec, cases):
    """按 spec（逗号分隔 case 名）从 cases 里选出子集。

    spec is None（未给开关）→ 原样返回（全量，默认行为零变化）。spec 给空/全是逗号 →
    usage 错误（exit 2），避免"变量为空 → 子集悄悄变空 → 假绿"。未知 case 名同理（exit 2）。
    """
    if spec is None:
        return list(cases)
    known = {}
    for entry in cases:
        known[entry.split('|')[0]] = entry
    wanted = []
    for name in spec.split(','):
        name = name.strip()
        if not name:
            continue
        if name not in known:
            print('smoke: 未知 case 名 {}（可用: {}）'.format(name, ','.join(sorted(known))),
                  file=sys.stderr)
            raise SystemExit(2)
        if name not in wanted:
            wanted.append(name)
    if not wanted:
        print('smoke: 空子集（--cases/PP_CASES 未给出任何 case 名）', file=sys.stderr)
        raise SystemExit(2)
    return [known[name] for name in wanted]


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_arg, _spec = parse_argv(sys.argv[1:])
if _arg:
    BUILD = _arg
elif os.environ.get('BUILD_DIR'):
    BUILD = os.environ['BUILD_DIR']
else:
    BUILD = os.path.join(ROOT, 'build', 'release')
if _spec is None:
    # 环境变量路径：PP_CASES 为空串 = 未设置（惯用"取消"写法）→ 全量。
    _spec = os.environ.get('PP_CASES') or None
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
# M4-T5: 第 2/3 段支持 ';'（多源）与 ','（多产物）—— 详见文件头。
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
    # M4-T5（0.3.0 多格式输出 §4）：1 源 → N 产物的路径/像素/元数据断言。
    #  - split：$format/$dir/$file（分文件夹）；mirror：$dir/$format/$file；conflict：$format/$file
    #    + 两个同主名源（rgb8.png / rgb8.tif）撞同一最终路径 → 逐输出独立 rename。
    #  - 输入字段支持 ';' 分隔多源；产物字段支持 ',' 分隔多产物（多产物时第二参数 = 用例根目录）。
    "multiformat-split|base/rgba8.png|jpeg/base/rgba8.jpg,webp/base/rgba8.webp|--outputs jpeg:jpegli,webp:libwebp --bitdepth 8 --lossless --meta Exif.Image.Artist=M4-T5-split",
    "multiformat-mirror|base/rgb8.png|base/jpeg/rgb8.jpg,base/webp/rgb8.webp|--outputs jpeg:jpegli,webp:libwebp --bitdepth 8 --lossless --meta Exif.Image.Artist=M4-T5-mirror --template $dir/$format/$file",
    "multiformat-conflict|base/rgb8.png;base/rgb8.tif|jpeg/rgb8.jpg,jpeg/rgb8 (1).jpg,webp/rgb8.webp,webp/rgb8 (1).webp|--outputs jpeg:jpegli,webp:libwebp --bitdepth 8 --lossless --template $format/$file --conflict rename",
    # M4-T6（0.3.0 逐文件进度 §7）：产物断言 + 进度事件流断言（<out>/progress-trace.jsonl，
    # 由 --dev 侧车逐事件写出）。jxl = 真实行级（synthetic=false, progress_reported=true）、
    # webp = 合成面（synthetic=true, progress_reported=false，§7.3）；另断言 overall_frac
    # 单调不倒退与"终态 done → 达 1.0"（见 pp_verify 的 progress_trace 段 + SCHEMA.md）。
    "progress-trace|base/rgb8.png|jxl/rgb8.jxl,webp/rgb8.webp|--outputs jxl:libjxl:modular,webp:libwebp:lossless --bitdepth 8 --lossless --meta Exif.Image.Artist=M4-T6-progress --template $format/$file",
]


def main():
    binary = resolve_exe(PP_BIN)
    if binary is None:
        print('smoke: photopipeline not executable: {}'.format(PP_BIN), file=sys.stderr)
        print("smoke: build directory '{}' has no dev harness binary;".format(BUILD), file=sys.stderr)
        print('smoke: configure it with -DPP_BUILD_DEV=ON (a plain release build has no --dev).',
              file=sys.stderr)
        print('用法: python tests/golden/smoke.py [BUILD_DIR]', file=sys.stderr)
        return 2
    verify = resolve_exe(PP_VERIFY)
    if verify is None:
        print('smoke: pp_verify not executable: {}'.format(PP_VERIFY), file=sys.stderr)
        print('用法: python tests/golden/smoke.py [BUILD_DIR]', file=sys.stderr)
        return 2
    if not os.path.isdir(os.path.join(GOLDEN, 'base')):
        print('smoke: golden corpus missing: {}/base (run python tools/gen_corpus.py)'.format(GOLDEN),
              file=sys.stderr)
        return 2

    os.makedirs(OUT_ROOT, exist_ok=True)

    selected = select_cases(_spec, CASES)
    if _spec:
        # 仅子集模式追加此行（默认路径控制台输出逐字不变）。
        print('smoke: 子集 {} → 选中 {}/{} 对'.format(
            ','.join(entry.split('|')[0] for entry in selected), len(selected), len(CASES)))

    passed = 0
    failed = 0
    print('%-16s %-8s %s' % ('case', 'result', 'detail'))
    for entry in selected:
        parts = entry.split('|')
        name = parts[0]
        input_rel = parts[1]
        out_rel = parts[2]
        args = '|'.join(parts[3:])
        outdir = os.path.join(OUT_ROOT, name)
        log = OUT_ROOT + '/' + name + '.dev.log'
        shutil.rmtree(outdir, ignore_errors=True)
        # 0.3.0 多格式：输入可多源（';' 分隔）、产物可多件（',' 分隔）
        input_paths = [GOLDEN + '/' + p.strip() for p in input_rel.split(';') if p.strip()]
        # 等价 bash 的不分词引用展开：`$args` 按空白切分。
        command = ([binary, '--dev'] + input_paths +
                   ['--out', outdir, '--base', GOLDEN, '--conflict', 'overwrite', '--workers', '1']
                   + args.split())
        with open(log, 'wb') as log_fp:
            rc = subprocess.run(command, stdout=log_fp, stderr=subprocess.STDOUT).returncode
        if rc != 0:
            print('%-16s %-8s --dev exit %d (log: %s)' % (name, 'FAIL', rc, log))
            failed += 1
            continue
        # 多产物 → pp_verify 拿用例根目录（逐产物按 outputs[].rel 定位/断言）；
        # 单产物 → 拿产物文件本身（v1 口径，逐字不变）。
        actual = outdir if ',' in out_rel else outdir + '/' + out_rel
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
