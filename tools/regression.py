#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""# PhotoPipeline M2-T9 — full-corpus regression baseline (docs/m2-tasks.md §4 T9,
# exit criterion §8.6; docs/design.md §8.4 "回归基线：全语料 --dev 日志存档，
# debug 期间每修一 bug 全量 diff").
#
# usage:
#   bash tools/regression.sh [BUILD_DIR] [--update]
#
#   BUILD_DIR   default $BUILD_DIR or <repo>/build/release-dev; must contain the
#               dev harness binary `photopipeline` (built with -DPP_BUILD_DEV=ON).
#   --update    regenerate tools/baseline/golden.log from the current build
#               instead of diffing against it.
#
# exit codes:
#   0  zero diff — the normalized run log reproduces tools/baseline/golden.log
#   1  diff found — the first 40 diff lines are printed
#   2  environment missing (bad usage / no binary / no baseline / no corpus)
#
# ---------------------------------------------------------------------------
# WHAT IS RUN (all through the frozen dev harness `photopipeline --dev`, §3.15)
#   * full corpus, 27 inputs (tests/golden/{base:16, edge:4, meta:7}) x 8 formats
#     jpeg jxl png tiff webp bmp heif avif                      -> 8 invocations
#   * metadata-only (--metadata-only) x tests/golden/base:16 x 4 formats
#     jpeg png tiff webp  (the only formats supporting metadata-only)  -> 4
#   = 12 sequential invocations, 280 file slots.
#   tests/golden/real/ is deliberately NOT part of the baseline (user-supplied
#   samples, empty in-tree); tests/golden/smoke/ belongs to smoke.sh, not here.
#
# THE THREE PREMISES the baseline depends on (do not relax any of them):
#   1. workers=1      — single worker keeps the per-file order, the budget
#                       accounting and the encoder call sequence deterministic.
#   2. fixed out dirs — every run writes to .cache/regression/out-<run>; the dir
#                       is wiped first, so the run log path and the mirrored
#                       output paths (out=<...>) are identical on every run.
#   3. idempotent     — the whole script is safe to re-run: fresh out dirs,
#                       --conflict overwrite, no state carried between runs.
#   The script always runs from the repository root and passes *relative* input
#   paths, so no machine-specific absolute path can enter the baseline.
#
# NORMALIZATION RULES (applied to the spdlog run-*.log file and to the stdout
# table of every invocation, in this order):
#   N1  leading spdlog timestamp `HH:MM:SS.mmm ` -> removed
#   N2  thread id `[12345]` -> `[tid]`
#   N3  repository root prefix -> `<repo>` (guard; inputs are already relative)
#   N4  memory addresses (`0x` + >= 9 hex digits) -> `0x<addr>`; the 9-digit
#       floor keeps the libjxl hex version string `0x00002afa` verbatim
#   N5  `/tmp/<random segment>` -> `<tmp>` (guard)
#   N6  every `<field>_ms=<number>` -> `<field>_ms=<ms>` (decode/orient/color/
#       flatten/encode/metawrite/total/avg_file) and `throughput_mb_s=<number>`
#       -> `throughput_mb_s=<val>`
#   N7  `capacity_bytes=`/`budget_bytes=` -> `<bytes>`: the budget capacity is
#       derived from *currently available* RAM (min(available/2, 8 GiB)), so it
#       changes with machine load and must never enter the baseline
#   N8  the elapsed-conditional `budget report {…}` line is dropped entirely —
#       it only exists when a run took >= 1000 ms, which makes its presence
#       load-dependent; per-file `pixel budget acquired` lines keep the
#       unconditional budget accounting
#   N9  stdout table `file state bytes ms warnings` -> the ms column -> `<ms>`
#       (bytes, state and warnings stay verbatim)
#   Kept verbatim: level/stage/source, file names, parameter snapshot, warnings,
#   failure reasons and all summary counts (files/ok/failed/skipped/cancelled/
#   bytes) — i.e. exactly the "what changed" signal the baseline exists for.
#
# regenerate the baseline: bash tools/regression.sh <BUILD_DIR> --update
#
# ---------------------------------------------------------------------------
# M3-W3（裁定 D3：Python 单实现，本文件替代 tools/regression.sh）
#   * 可执行文件探测按 name / name + '.exe' 双探测（Windows 产物带 .exe）
#   * N1–N9 用 re 逐条实现（见 normalize()），顺序与 sed/grep/awk 管线一致
#   * diff 用 difflib.unified_diff（输出行格式 = 原样打印 diff 行；前 40 行语义不变）
#   * 写进 baseline 的头部注释段、全部 `regression: ...` 输出行、退出码 0/1/2
#     与 bash 版逐字一致（含对 tools/regression.sh / tools/gen_corpus.sh 的既有引用，
#     保证重新生成的 .cache/regression/current.log 仍与 tools/baseline/golden.log 可比）
"""

import difflib
import os
import re
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, 'tools', 'baseline', 'golden.log')
OUT_ROOT_REL = '.cache/regression'
OUT_ROOT = ROOT + '/' + OUT_ROOT_REL
RAW = OUT_ROOT + '/raw'
WORK = OUT_ROOT + '/current.log'

FORMATS = ['jpeg', 'jxl', 'png', 'tiff', 'webp', 'bmp', 'heif', 'avif']
META_FORMATS = ['jpeg', 'png', 'tiff', 'webp']

BIN = ''
CORPUS = []
BASE_CORPUS = []


def usage(dest=None):
    """等价 bash `sed -n '7,18p' "$0" | sed 's/^# \\{0,1\\}//'`：打印本文件头部的 usage 块。"""
    out = []
    inside = False
    for line in (__doc__ or '').splitlines():
        if line.strip() == '# usage:':
            inside = True
        if inside:
            out.append(re.sub(r'^# ?', '', line))
            if 'environment missing' in line:
                break
    (dest or sys.stdout).write('\n'.join(out) + '\n')


# N1..N9 — see the header. Applied to the run log and to the stdout table.
_N1 = re.compile(r'^[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} ')
_N2 = re.compile(r'^(\[[a-z]+\] )\[[0-9]+\] ')
_N4 = re.compile(r'0x[0-9a-fA-F]{9,}')
_N5 = re.compile(r'/tmp/[A-Za-z0-9._+-]+')
_N6 = re.compile(r'([a-z_]+_ms)=[0-9]+(\.[0-9]+)?')
_N6B = re.compile(r'throughput_mb_s=[0-9.]+')
_N7 = re.compile(r'(capacity_bytes|budget_bytes)=[0-9]+')


def normalize(text):
    """sed/grep/awk 管线（N1–N9）→ 规范化后的行列表。"""
    normalized = []
    for line in text.splitlines():
        line = _N1.sub('', line)
        line = _N2.sub(r'\1[tid] ', line)
        line = line.replace(ROOT, '<repo>')
        line = _N4.sub('0x<addr>', line)
        line = _N5.sub('<tmp>', line)
        line = _N6.sub(r'\1=<ms>', line)
        line = _N6B.sub('throughput_mb_s=<val>', line)
        line = _N7.sub(r'\1=<bytes>', line)
        if 'budget report {' in line:
            continue
        fields = line.split()
        if len(fields) >= 4 and fields[1] in ('ok', 'failed', 'skipped', 'cancelled'):
            fields[3] = '<ms>'
            line = ' '.join(fields)
        normalized.append(line)
    return normalized


def read_text(path):
    with open(path, encoding='utf-8', errors='replace') as fp:
        return fp.read()


def write_lines(fp, lines):
    fp.write(''.join(line + '\n' for line in lines))


def resolve_exe(path):
    """name 与 name + '.exe' 双探测（Windows 上实际产物带 .exe）；找不到 → None。"""
    for candidate in (path, path + '.exe'):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def find_files(root_rel):
    """LC_ALL=C sort 的确定性语料清单（相对仓库根、正斜杠路径）。"""
    found = []
    for dirpath, _dirnames, filenames in os.walk(os.path.join(ROOT, root_rel)):
        for filename in filenames:
            full = os.path.join(dirpath, filename)
            found.append(os.path.relpath(full, ROOT).replace(os.sep, '/'))
    found.sort()
    return found


HEADER = [
    '# ===========================================================================',
    '# PhotoPipeline M2-T9 regression baseline — normalized full-corpus --dev log',
    '# (docs/design.md §8.4; docs/m2-tasks.md §4 T9, exit criterion §8.6)',
    '#',
    '# matrix: 8 formats x 27 corpus inputs (base 16 + edge 4 + meta 7)',
    '#         + metadata-only (--metadata-only) x 4 formats x base 16',
    '# run shape: photopipeline --dev <corpus...> --out {}/out-<run> \\'.format(OUT_ROOT_REL),
    '#            --base tests/golden --conflict overwrite --workers 1',
    '# prem.: workers=1, fixed+wiped out dirs, idempotent re-run (see tools/regression.sh)',
    '# norm.: timestamps/tid/*_ms/throughput/capacity+budget bytes -> placeholders;',
    '#        <repo> prefix, 0x<addr>, <tmp> segments; elapsed-conditional',
    '#        "budget report" lines dropped; stdout ms column -> <ms>',
    '# regenerate: bash tools/regression.sh <build_dir> --update',
    '# ===========================================================================',
]


def run_one(label, kind, extra):
    out_rel = OUT_ROOT_REL + '/out-' + label
    out = ROOT + '/' + out_rel
    raw_out = RAW + '/stdout-' + label + '.log'
    shutil.rmtree(out, ignore_errors=True)
    common = ['--out', out_rel, '--base', 'tests/golden',
              '--conflict', 'overwrite', '--workers', '1']
    if kind == 'meta':
        command = [BIN, '--dev'] + BASE_CORPUS + common + ['--metadata-only'] + extra
    else:
        command = [BIN, '--dev'] + CORPUS + common + extra
    child_env = dict(os.environ)
    child_env['LC_ALL'] = 'C'
    with open(raw_out, 'wb') as raw_fp:
        rc = subprocess.run(command, stdout=raw_fp, stderr=subprocess.STDOUT,
                            env=child_env, cwd=ROOT).returncode

    logs_dir = out + '/logs'
    candidates = []
    if os.path.isdir(logs_dir):
        candidates = sorted(name for name in os.listdir(logs_dir)
                            if name.startswith('run-') and name.endswith('.log'))
    logf = os.path.join(logs_dir, candidates[0]) if candidates else ''

    with open(WORK, 'a', encoding='utf-8', newline='\n') as work_fp:
        work_fp.write('### run {} rc={} args={}\n'.format(label, rc, ' '.join(extra)))
        if logf:
            write_lines(work_fp, normalize(read_text(logf)))
        else:
            work_fp.write('(no run log written)\n')
        write_lines(work_fp, normalize(read_text(raw_out)))

    summary = ''
    for line in read_text(raw_out).splitlines():
        if line.startswith('SUMMARY '):
            summary = line[len('SUMMARY '):]
            break
    print('regression: %-14s rc=%-3d %s' % (label, rc, summary))


def count_changed(diff_lines):
    return sum(1 for line in diff_lines
               if line[:1] in '+-' and line[1:2] not in ('+', '-'))


def main(argv):
    global BIN, CORPUS, BASE_CORPUS

    update = 0
    build = ''
    caller_pwd = os.getcwd()
    for arg in argv:
        if arg == '--update':
            update = 1
        elif arg in ('-h', '--help'):
            usage()
            return 0
        elif arg.startswith('-'):
            print('regression: unknown option: {}'.format(arg), file=sys.stderr)
            usage(sys.stderr)
            return 2
        else:
            if build:
                print("regression: too many arguments ('{}' and '{}')".format(build, arg),
                      file=sys.stderr)
                usage(sys.stderr)
                return 2
            build = arg
    if not build:
        build = os.environ.get('BUILD_DIR') or os.path.join(ROOT, 'build', 'release-dev')
    if not os.path.isabs(build):
        build = caller_pwd + '/' + build
    BIN = build + '/photopipeline'

    # ---- environment checks (exit 2 = missing environment, not a regression) ----
    binary = resolve_exe(BIN)
    if binary is None:
        print('regression: photopipeline not executable: {}'.format(BIN), file=sys.stderr)
        print('regression: pass a dev build dir or configure with -DPP_BUILD_DEV=ON.',
              file=sys.stderr)
        print('用法: bash tools/regression.sh [BUILD_DIR] [--update]', file=sys.stderr)
        return 2
    if not (os.path.isdir(os.path.join(ROOT, 'tests', 'golden', 'base'))
            and os.path.isdir(os.path.join(ROOT, 'tests', 'golden', 'edge'))
            and os.path.isdir(os.path.join(ROOT, 'tests', 'golden', 'meta'))):
        print('regression: golden corpus missing under {}/tests/golden '
              '(run tools/gen_corpus.sh)'.format(ROOT), file=sys.stderr)
        return 2
    if update == 0 and not os.path.isfile(BASELINE):
        print('regression: baseline missing: {}'.format(BASELINE), file=sys.stderr)
        print('regression: generate it once with: bash tools/regression.sh {} --update'.format(
            build), file=sys.stderr)
        return 2
    BIN = binary

    os.chdir(ROOT)

    # ---- deterministic corpus listing (LC_ALL=C; relative paths) ----
    CORPUS = find_files('tests/golden/base') + find_files('tests/golden/edge') \
        + find_files('tests/golden/meta')
    CORPUS.sort()
    BASE_CORPUS = find_files('tests/golden/base')
    if not CORPUS or not BASE_CORPUS:
        print('regression: corpus listing is empty', file=sys.stderr)
        return 2

    shutil.rmtree(OUT_ROOT, ignore_errors=True)
    os.makedirs(RAW, exist_ok=True)

    with open(WORK, 'w', encoding='utf-8', newline='\n') as work_fp:
        write_lines(work_fp, HEADER)

    print('regression: build  {}'.format(build))
    print('regression: corpus %d files (base %d + edge 4 + meta 7)' % (
        len(CORPUS), len(BASE_CORPUS)))
    print('regression: out    {} (wiped)'.format(OUT_ROOT_REL))

    for fmt in FORMATS:
        run_one('full-' + fmt, 'full', ['--format', fmt])
    for fmt in META_FORMATS:
        run_one('meta-' + fmt, 'meta', ['--format', fmt])

    work_text = read_text(WORK)
    lines_new = work_text.count('\n')

    if update == 1:
        if os.path.isfile(BASELINE):
            base_text = read_text(BASELINE)
            lines_old = base_text.count('\n')
            changed = count_changed(list(difflib.unified_diff(
                base_text.splitlines(), work_text.splitlines(), lineterm='')))
            print('regression: --update: replacing baseline '
                  '(%d lines -> %d lines, %s changed lines)' % (lines_old, lines_new, changed))
        os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
        shutil.copyfile(WORK, BASELINE)
        print('regression: baseline written: tools/baseline/golden.log (%d lines)' % lines_new)
        return 0

    base_text = read_text(BASELINE)
    lines_base = base_text.count('\n')
    print('regression: normalized %d lines vs baseline %d lines' % (lines_new, lines_base))
    diff_lines = list(difflib.unified_diff(base_text.splitlines(), work_text.splitlines(),
                                           fromfile=BASELINE, tofile=WORK, lineterm=''))
    if not diff_lines:
        print('regression: zero diff (baseline reproduced)')
        return 0
    print('regression: DIFF FOUND (%s lines of unified diff, first 40 shown)' % len(diff_lines))
    for line in diff_lines[:40]:
        sys.stdout.write(line + '\n')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
