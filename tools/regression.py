#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""# PhotoPipeline M2-T9 — full-corpus regression baseline (docs/m2-tasks.md §4 T9,
# exit criterion §8.6; docs/design.md §8.4 "回归基线：全语料 --dev 日志存档，
# debug 期间每修一 bug 全量 diff").
#
# usage:
#   python tools/regression.py [BUILD_DIR] [--update]
#                              [--platform linux|windows] [--baseline FILE]
#                              [--attrib-lines N]
#
#   BUILD_DIR   default $BUILD_DIR or <repo>/build/release-dev; must contain the
#               dev harness binary `photopipeline` (built with -DPP_BUILD_DEV=ON).
#   --update    regenerate the platform baseline (tools/baseline/golden.log on
#               linux, tools/baseline/golden.windows.log on windows) from the
#               current build instead of diffing against it.
#   --platform  force the baseline platform (default: host platform) — the
#               baseline file is picked from it, nothing else changes.
#   --baseline  use FILE as the baseline instead of the platform default
#               (cross-platform review / one-off comparisons).
#   --attrib-lines N  samples printed per attribution bucket (default 3).
#
# exit codes:
#   0  zero diff — the normalized run log reproduces the platform baseline
#   1  diff found — the first 40 diff lines are printed, then the attribution
#      table (per-kind removed/added + transition buckets; detail report path)
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
#   samples, empty in-tree); tests/golden/smoke/ belongs to smoke.py, not here.
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
# regenerate the baseline: python tools/regression.py <BUILD_DIR> --update
#
# ---------------------------------------------------------------------------
# M3-W3（裁定 D3：Python 单实现，本文件替代 tools/regression.sh）
#   * 可执行文件探测按 name / name + '.exe' 双探测（Windows 产物带 .exe）
#   * N1–N9 用 re 逐条实现（见 normalize()），顺序与 sed/grep/awk 管线一致
#   * diff 用 difflib.unified_diff（输出行格式 = 原样打印 diff 行；前 40 行语义不变）
#   * 写进 baseline 的头部注释段、全部 `regression: ...` 输出行、退出码 0/1/2
#     与 bash 版逐字一致（含对 tools/regression.sh / tools/gen_corpus.sh 的既有引用，
#     保证重新生成的 .cache/regression/current.log 仍与 tools/baseline/golden.log 可比）
#     —— M4-T17 例外：只在**追加**平台行与归因行（既有行一字未改），baseline 内容
#     （HEADER + 运行段）与本次改动无关 ⇒ Linux 基线的可比性不受影响。
# ---------------------------------------------------------------------------
# M4-T17（双平台基线口径显式化 + diff 归因辅助）
#   * 基线选择：host platform windows → tools/baseline/golden.windows.log；
#     linux/其他 → tools/baseline/golden.log。可用 --platform 强制平台、
#     或 --baseline FILE 直接指定（跨平台复核）。选择结果打成
#     `regression: platform …` 行；宿主与所选平台不一致时多打一行 warning。
#   * 基线文件自身的 HEADER 段一字未动 —— HEADER 是基线内容的一部分
#     （m3-report §8-9），改它会让 Linux 基线立刻 DIFF FOUND；要改必须在
#     Linux 上 --update 重生成后一起改。
#   * diff 模式在“前 40 行 unified diff”之后追加归因辅助：先按 `### run`
#     段对齐，再按行型（version / run-start / file-done / output-done /
#     budget / row / …）统计 removed/added，并对齐 replace 块给出 transition
#     桶；stdout 打桶表 + 每桶 N 条样例，逐行明细写
#     .cache/regression/attribution.log（可复查）。退出码语义不变。
#   * Windows 上必须经工具链环境启动（Qt DLL 在 PATH 上）：
#     `python tools/env.py run -- python tools/regression.py <BUILD_DIR>`；
#     裸跑会得到 rc=0xC0000135，脚本对此打一行 hint（env.py 才是环境构造的
#     单实现，本脚本不复制它的逻辑）。
# ---------------------------------------------------------------------------
"""

import collections
import difflib
import os
import re
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE_DIR_REL = 'tools/baseline'
HOST_PLATFORM = 'windows' if os.name == 'nt' else (
    'macos' if sys.platform == 'darwin' else 'linux')
BASELINE = os.path.join(ROOT, 'tools', 'baseline', 'golden.log')  # main() 按平台重设
OUT_ROOT_REL = '.cache/regression'
OUT_ROOT = ROOT + '/' + OUT_ROOT_REL
RAW = OUT_ROOT + '/raw'
WORK = OUT_ROOT + '/current.log'
ATTRIB = OUT_ROOT + '/attribution.log'


def platform_baseline_rel(platform):
    """平台 → 基线（仓库相对路径）。windows 专档，其余用 Linux 基线（M4-T17）。"""
    return BASELINE_DIR_REL + ('/golden.windows.log' if platform == 'windows'
                               else '/golden.log')


def platform_of_baseline(path):
    """基线路径 → 平台（归因报告用；golden.windows.log / 文件名含 `.windows.` 记 windows）。"""
    name = os.path.basename(path)
    return 'windows' if name == 'golden.windows.log' or '.windows.' in name else 'linux'

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


# 进程“起不来”的退出码：Windows 0xC0000135 STATUS_DLL_NOT_FOUND / POSIX 127。
# 一次性提示（不是错误：退出码仍按原样进日志，只补一句人话）。
_START_FAIL_RC = (3221225781, 127)
_hint_shown = []


def exe_start_hint(rc):
    if rc in _START_FAIL_RC and not _hint_shown:
        _hint_shown.append(1)
        print('regression: hint: harness rc={} (process could not start — missing '
              'DLL/.so); launch through the toolchain env: '
              'python tools/env.py run -- python tools/regression.py <BUILD_DIR>'.format(rc))


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
    exe_start_hint(rc)

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


# ---------------------------------------------------------------------------
# M4-T17：diff 归因辅助（行型分类）
#   归一化后的行先按行型入桶（顺序敏感：先特殊后一般），再在桶内按短键细分
#   （version 的 lib、row 的 state、run-start 的 schema 等）。归因 = 段对齐 +
#   replace 块成对对齐 → transition 桶；不对齐的尾巴记 removed/added。
# ---------------------------------------------------------------------------
KINDS = (
    ('header', re.compile(r'^# ')),
    ('section', re.compile(r'^### run ')),
    ('version', re.compile(r'^\[info\] \[tid\] \[run\] \[main\.cpp\] version \{')),
    ('run-start', re.compile(r'^\[info\] \[tid\] \[run\] \[main\.cpp\] run start \{')),
    ('run-summary', re.compile(r'^\[info\] \[tid\] \[run\] \[main\.cpp\] run summary \{')),
    ('scheduler', re.compile(r'^\[info\] \[tid\] \[scheduler\] \[scheduler\.cpp\]')),
    ('budget', re.compile(r'\] pixel budget acquired \{')),
    ('file-done', re.compile(r'\] file done \{')),
    ('output-done', re.compile(r'\] output done \{')),
    ('output-warning', re.compile(r'\] output warning \{')),
    ('meta-done', re.compile(r'\] metadata-only done \{')),
    ('encode-error', re.compile(r'\] (encode failed|metadata-only rewrite failed)')),
    ('error', re.compile(r'^\[error\]')),
    ('warning', re.compile(r'^\[warning\]')),
    ('info', re.compile(r'^\[info\]')),
    ('summary', re.compile(r'^SUMMARY ')),
    ('exit', re.compile(r'^EXIT ')),
    ('banner', re.compile(r'^(photopipeline --dev$| {2}\S+=| {4}output\[)')),
    ('table-header', re.compile(r'^file\s+state\s+bytes\s+ms\s+warnings$')),
    ('row', re.compile(r'^\S.*\s(ok|failed|skipped|cancelled) \d+ <ms>(\s.*)?$')),
    ('row-detail', re.compile(r'^ {4,}')),
    ('vendor-stdout', re.compile(r'^(Svt\[|Svt |heif|aom|webp)')),
    ('blank', re.compile(r'^$')),
)


def line_kind(line):
    """归一化行 → 行型（KINDS 里第一条命中；未命中 = 'other'）。"""
    for kind, rx in KINDS:
        if rx.search(line):
            return kind
    return 'other'


def line_key(line):
    """行型内细分键（桶表第二列）；无细分返回 ''。"""
    kind = line_kind(line)
    if kind == 'version':
        m = re.search(r'\{lib=([^ ]+)', line)
        return 'lib=' + (m.group(1) if m else '?')
    if kind == 'row':
        parts = line.split()
        return 'state=' + parts[1] if len(parts) >= 2 else 'state=?'
    if kind == 'row-detail':
        return 'error' if re.match(r'^\s+error:', line) else ''
    if kind == 'run-start':
        if 'outputs=' in line:
            return 'schema=outputs'
        if 'format=' in line:
            return 'schema=format'
        return 'schema=?'
    if kind in ('file-done', 'output-done'):
        m = re.search(r'outputs=(\d+)', line)
        if m:
            return 'outputs=' + m.group(1)
        return 'outputs=1' if 'out="' in line else 'outputs=?'
    if kind == 'run-summary':
        m = re.search(r'files=(\d+)', line)
        return 'files=' + m.group(1) if m else ''
    if kind == 'encode-error':
        return line.split(']')[-1].split(':')[0].strip()[:40]
    return ''


def sectionize(lines):
    """→ (pre, [(label, lines)])；label = `### run ` 之后的整串。"""
    pre = []
    sections = []
    label = None
    cur = []
    for line in lines:
        if line.startswith('### run '):
            if label is None:
                pre = cur
            else:
                sections.append((label, cur))
            label = line[len('### run '):]
            cur = [line]
        else:
            cur.append(line)
    if label is not None:
        sections.append((label, cur))
    return pre, sections


def pair_key(line):
    """行身份键：在 diff 块内按语义配对 old/new（而不是按位置）。

    键取 (角色, src / lib / detail)，让 “0.2 的 file done” 与 “0.3 的 output done”
    落在同一个键上 —— 位置对齐会把它们错配成 file-done→info 之类的伪 transition。
    """
    kind = line_kind(line)
    src = re.search(r'src=(\S+)', line)
    src = src.group(1) if src else ''
    if kind == 'version':
        return ('version', line_key(line))
    if kind in ('file-done', 'output-done', 'meta-done', 'budget'):
        return ('output', src)
    if kind in ('run-start', 'run-summary', 'scheduler'):
        return (kind, src)
    if kind == 'output-warning':
        m = re.search(r'detail="([^"]*)"', line)
        return ('output-warning', src, (m.group(1) if m else '')[:60])
    if kind in ('error', 'warning', 'encode-error'):
        return (kind, src)
    if kind in ('row', 'row-detail'):
        return ('row', src)
    if kind in ('summary', 'exit', 'table-header', 'banner', 'blank'):
        return (kind,)
    return (kind, truncate(line, 60))


def line_bytes(line):
    """行里记录的产物字节数（日志 bytes= 或 stdout 表第 3 列）；没有 → None。"""
    m = re.search(r'\bbytes=(\d+)', line)
    if m:
        return int(m.group(1))
    if line_kind(line) == 'row' or line_kind(line) == 'row-detail':
        parts = line.split()
        if len(parts) >= 3 and parts[2].isdigit():
            return int(parts[2])
    return None


def transition_label(old, new):
    """成对行 → transition 桶标签（归因表的主口径）。"""
    ko, kn = line_kind(old), line_kind(new)
    if ko != kn:
        label = ko + ' -> ' + kn
    elif ko == 'version':
        lo, ln = line_key(old), line_key(new)
        return ('version-bump ' + lo) if lo == ln else (
            'version-lib-set ' + lo + ' -> ' + ln)
    elif ko == 'run-start':
        label = 'run-start ' + line_key(old) + ' -> ' + line_key(new)
    elif ko == 'run-summary':
        label = 'run-summary ' + line_key(old) + ' -> ' + line_key(new)
    elif ko in ('file-done', 'output-done', 'meta-done'):
        label = ko + ' ' + (line_key(old) or '?') + ' -> ' + (line_key(new) or '?')
    elif ko == 'section':
        return 'section ' + old[len('### run '):].split(' ')[0]
    else:
        label = 'in-place ' + ko
    ob, nb = line_bytes(old), line_bytes(new)
    if ob is not None and nb is not None:
        label += ' bytes-same' if ob == nb else ' bytes%+d' % (nb - ob)
    elif (ob is None) != (nb is None):
        label += ' bytes-field'
    return label


class Attribution(object):
    """一次 diff 的归因账：removed/added 桶 + transition 桶 + 逐行明细。"""

    def __init__(self):
        self.removed = collections.Counter()    # (kind, key) -> n
        self.added = collections.Counter()      # (kind, key) -> n
        self.transitions = collections.Counter()  # label -> n
        self.samples = collections.defaultdict(list)  # label -> [(old, new)]
        self.detail = []                        # 逐行明细（写 attribution.log）
        self.sections = []                      # (label, status)

    def _bucket(self, counter, line):
        counter[(line_kind(line), line_key(line))] += 1

    def add_block(self, label, old_lines, new_lines):
        """一段（section 或 pre）内归因：块内按 pair_key 语义配对，剩下记 removed/added。"""
        sm = difflib.SequenceMatcher(None, old_lines, new_lines, autojunk=False)
        self.detail.append('### ' + label)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == 'equal':
                continue
            if tag != 'replace':
                # delete / insert：块内没有对侧行，直接记入 removed/added
                block = old_lines[i1:i2] if tag == 'delete' else new_lines[j1:j2]
                for line in block:
                    self._record(line, None, new_side=(tag != 'delete'))
                continue
            # replace：先按 pair_key 配对（同键按出现顺序），余下的记 removed/added
            old_pool = collections.defaultdict(list)
            for line in old_lines[i1:i2]:
                old_pool[pair_key(line)].append(line)
            for line in new_lines[j1:j2]:
                key = pair_key(line)
                if old_pool.get(key):
                    self._record(old_pool[key].pop(0), line)
                else:
                    self._record(line, None, new_side=True)
            for key in old_pool:
                for line in old_pool[key]:
                    self._record(line, None)

    def _record(self, old, new, new_side=False):
        if new is None and not new_side:
            self._bucket(self.removed, old)
            self.detail.append('-- [%s] %s' % (line_kind(old), old))
            return
        if new is None:
            self._bucket(self.added, old)
            self.detail.append('++ [%s] %s' % (line_kind(old), old))
            return
        t = transition_label(old, new)
        self.transitions[t] += 1
        if len(self.samples[t]) < 24:
            self.samples[t].append((old, new))
        self.detail.append('-  [%s] %s' % (t, old))
        self.detail.append('+  [%s] %s' % (t, new))

    def compare(self, old_lines, new_lines):
        """段对齐（同 label 配同 label）+ 段内归因。"""
        old_pre, old_secs = sectionize(old_lines)
        new_pre, new_secs = sectionize(new_lines)
        if old_pre or new_pre:
            self.add_block('pre', old_pre, new_pre)
        new_map = collections.OrderedDict(new_secs)
        old_map = collections.OrderedDict(old_secs)
        for label in old_map:
            if label not in new_map:
                self.sections.append((label, 'only-in-baseline'))
                self.add_block(label, old_map[label], [])
        for label in new_map:
            if label not in old_map:
                self.sections.append((label, 'only-in-current'))
                self.add_block(label, [], new_map[label])
            else:
                self.sections.append((label, 'paired'))
        for label in old_map:
            if label in new_map:
                self.add_block(label, old_map[label], new_map[label])

    def summary_lines(self, samples_per_bucket):
        out = []
        out.append('sections: old=%d new=%d paired=%d only-baseline=%d only-current=%d' % (
            len([s for s in self.sections if s[1] != 'only-in-current']),
            len([s for s in self.sections if s[1] != 'only-in-baseline']),
            len([s for s in self.sections if s[1] == 'paired']),
            len([s for s in self.sections if s[1] == 'only-in-baseline']),
            len([s for s in self.sections if s[1] == 'only-in-current'])))
        for tag, counter in (('removed', self.removed), ('added', self.added)):
            out.append('%s by kind:' % tag)
            for (kind, key), n in counter.most_common():
                out.append('  %-16s %-28s %5d' % (kind, key, n))
        out.append('transitions:')
        for label, n in self.transitions.most_common():
            out.append('  %-40s %5d' % (label, n))
            for old, new in self.samples[label][:samples_per_bucket]:
                out.append('      - %s' % truncate(old, 150))
                out.append('      + %s' % truncate(new, 150))
        return out


def truncate(text, limit):
    return text if len(text) <= limit else text[:limit - 3] + '...'


def print_attribution(attr, samples_per_bucket):
    for line in attr.summary_lines(samples_per_bucket):
        print('regression: attrib: ' + line)
    try:
        with open(ATTRIB, 'w', encoding='utf-8', newline='\n') as fp:
            fp.write('# regression attribution — platform=%s baseline=%s (%s)\n' % (
                platform_of_baseline(BASELINE), BASELINE, HOST_PLATFORM + ' host'))
            for line in attr.summary_lines(samples_per_bucket):
                fp.write(line + '\n')
            fp.write('\n# per-line detail (kind/transition annotated)\n')
            write_lines(fp, attr.detail)
        print('regression: attrib: detail -> {} ({} lines)'.format(
            ATTRIB[len(ROOT) + 1:].replace(os.sep, '/'), len(attr.detail)))
    except OSError as exc:
        print('regression: attrib: detail report not written: {}'.format(exc),
              file=sys.stderr)


def main(argv):
    global BIN, CORPUS, BASE_CORPUS, BASELINE

    update = 0
    build = ''
    platform = HOST_PLATFORM
    baseline_arg = ''
    attrib_lines = 3
    caller_pwd = os.getcwd()
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == '--update':
            update = 1
        elif arg in ('-h', '--help'):
            usage()
            return 0
        elif arg == '--platform' or arg == '--baseline' or arg == '--attrib-lines':
            if i + 1 >= len(argv):
                print('regression: {} needs a value'.format(arg), file=sys.stderr)
                usage(sys.stderr)
                return 2
            value = argv[i + 1]
            i += 1
            if arg == '--platform':
                if value not in ('linux', 'windows'):
                    print('regression: invalid --platform: {} (linux|windows)'.format(value),
                          file=sys.stderr)
                    return 2
                platform = value
            elif arg == '--baseline':
                baseline_arg = value
            else:
                try:
                    attrib_lines = int(value)
                except ValueError:
                    print('regression: invalid --attrib-lines: {}'.format(value),
                          file=sys.stderr)
                    return 2
                if attrib_lines < 0:
                    print('regression: invalid --attrib-lines: {}'.format(value),
                          file=sys.stderr)
                    return 2
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
        i += 1
    if not build:
        build = os.environ.get('BUILD_DIR') or os.path.join(ROOT, 'build', 'release-dev')
    if not os.path.isabs(build):
        build = caller_pwd + '/' + build
    BIN = build + '/photopipeline'

    # ---- baseline口径（M4-T17）：host platform → 基线；--baseline 直接指定 ----
    if baseline_arg:
        BASELINE = baseline_arg if os.path.isabs(baseline_arg) else caller_pwd + '/' + baseline_arg
    else:
        BASELINE = os.path.join(ROOT, *platform_baseline_rel(platform).split('/'))

    # ---- environment checks (exit 2 = missing environment, not a regression) ----
    binary = resolve_exe(BIN)
    if binary is None:
        print('regression: photopipeline not executable: {}'.format(BIN), file=sys.stderr)
        print('regression: pass a dev build dir or configure with -DPP_BUILD_DEV=ON.',
              file=sys.stderr)
        print('用法: python tools/regression.py [BUILD_DIR] [--update]', file=sys.stderr)
        return 2
    if not (os.path.isdir(os.path.join(ROOT, 'tests', 'golden', 'base'))
            and os.path.isdir(os.path.join(ROOT, 'tests', 'golden', 'edge'))
            and os.path.isdir(os.path.join(ROOT, 'tests', 'golden', 'meta'))):
        print('regression: golden corpus missing under {}/tests/golden '
              '(run python tools/gen_corpus.py)'.format(ROOT), file=sys.stderr)
        return 2
    if update == 0 and not os.path.isfile(BASELINE):
        print('regression: baseline missing: {}'.format(BASELINE), file=sys.stderr)
        print('regression: generate it once with: python tools/regression.py {} --update'.format(
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

    print('regression: platform {} (host {}) baseline={}'.format(
        platform_of_baseline(BASELINE), HOST_PLATFORM,
        os.path.relpath(BASELINE, ROOT).replace(os.sep, '/')))
    if platform_of_baseline(BASELINE) != HOST_PLATFORM:
        print('regression: warning: baseline platform ({}) != host platform ({}) — '
              'the run log stays host-shaped (path separators), expect a large diff'.format(
                  platform_of_baseline(BASELINE), HOST_PLATFORM))
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
        print('regression: baseline written: {} ({} lines)'.format(
            os.path.relpath(BASELINE, ROOT).replace(os.sep, '/'), lines_new))
        return 0

    base_text = read_text(BASELINE)
    lines_base = base_text.count('\n')
    print('regression: normalized %d lines vs baseline %d lines' % (lines_new, lines_base))
    base_lines = base_text.splitlines()
    work_lines = work_text.splitlines()
    diff_lines = list(difflib.unified_diff(base_lines, work_lines,
                                           fromfile=BASELINE, tofile=WORK, lineterm=''))
    if not diff_lines:
        print('regression: zero diff (baseline reproduced)')
        return 0
    print('regression: DIFF FOUND (%s lines of unified diff, first 40 shown)' % len(diff_lines))
    for line in diff_lines[:40]:
        sys.stdout.write(line + '\n')
    # ---- M4-T17：逐行归因辅助（行型分类 + transition 桶）----
    attr = Attribution()
    attr.compare(base_lines, work_lines)
    print_attribution(attr, attrib_lines)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
