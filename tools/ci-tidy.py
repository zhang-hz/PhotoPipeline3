#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — clang-tidy 静态分析棘轮（M4-W4-T16）

依据: docs/v0.3.0-design.md §12.2 质量门禁表「静态分析棘轮」行 ——
  clang-tidy（bugprone/performance/portability）｜告警数 ≤ 基线，新增代码 0 告警（基线文件入库）｜main-full
      + §12.1 main-full 拓扑首步「lint+tidy 棘轮」+ §12.3 job 预算（本脚本被 main-full 的 tidy job 调用）

用法:
  python tools/ci-tidy.py [BUILD_DIR]              # 棘轮门禁（默认 build/release-dev）
  python tools/ci-tidy.py [BUILD_DIR] --update     # 重新播种基线（口径见下，慎用）
  python tools/ci-tidy.py [BUILD_DIR] --list       # 只打印将检查的 TU 清单（不跑 clang-tidy）

选项:
  --checks <CSV>      检查集（默认 -*,bugprone-*,performance-*,portability-*；设计 §12.2 冻结口径）
  --baseline <PATH>   基线文件（默认 tools/baseline/clang-tidy.tsv，入库）
  --candidate <PATH>  未播种 fingerprint 时的候选基线（默认 <baseline>.candidate.tsv）
  --clang-tidy <PATH> clang-tidy 可执行文件（优先级最高）；亦可用 env CLANG_TIDY
  --jobs N            并行 TU 数（默认 = CPU 核数）
  --require-baseline  未播种 fingerprint ⇒ 直接失败（收紧 CI；默认关，见「播种路径」）
  --timeout N         单个 TU 的超时秒数（默认 600；超时 = 环境错误）

环境:
  BUILD_DIR 必须是**已 configure 且导出 compile_commands.json** 的构建树：
      cmake --preset release-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  本脚本只读该 JSON 里的编译参数（不起编译），因此**不需要** build 目标（生成的 version.h 在
  configure 期已落盘；AUTOMOC 的 *_autogen/mocs_compilation.cpp 属生成物，被显式排除）。
  Windows 上编译参数依赖 vcvars 的 INCLUDE/LIB（MSVC 系统头不在命令行里），故本机/CI 一律经
  环境构造器跑：python tools/env.py run -- python tools/ci-tidy.py build/release-dev

exit codes:
  0  棘轮通过；或（未播种 fingerprint 的）播种路径
  1  棘轮违规（总数/文件级超基线，或出现基线外的新增告警）；或 TU 解析失败（计数不完整 ⇒ 不可判绿）
  2  环境/用法错误（缺 compile_commands.json、找不到 clang-tidy、参数错、超时）

-----------------------------------------------------------------------------
棘轮口径（M4-W4-T16 冻结；与设计 §12.2 判据逐条对应）

  fingerprint = <platform>|<tool>|<checkset>
      platform  = windows-msvc / linux-gcc（平台族；编译器版本记进基线但不进 fingerprint）
      tool      = `clang-tidy --version` 的 LLVM 版本（如 clang-tidy 21.1.0）
      checkset  = --checks 的规范化字符串
    ⇒ 工具链/检查集一变，基线**不会**被悄悄套用（走播种路径或显式 --update）。

  统计口径:
    * 只统计**指向本仓库源码**（src/**、tests/**、tools/** 下的 .cpp/.h/.hpp…）的 `warning:` 诊断行；
      vcpkg/Qt/系统头的告警由 .clang-tidy 的 HeaderFilterRegex('src/.*') 拦掉，不进计数。
    * 每条诊断按**它自己指向的文件**归属（头文件告警归头文件，不重复计入每个包含它的 TU）。
    * `per-file` 计数 = 归到该文件的告警条数；`total` = 各文件之和。

  棘轮判据（fingerprint 命中基线记录时）:
    ① total ≤ 基线 total                     —— 「告警数 ≤ 基线」
    ② 每个文件 count ≤ 该文件基线 count       —— 防止把告警从 A 文件搬到 B 文件
    ③ 基线里**没有**的文件必须 count == 0     —— 「新增代码 0 告警」
  任一条不满足 ⇒ exit 1，逐文件点名（前 40 行，house 风格）。

  播种路径（fingerprint 未入库；CI 首跑/工具链升级后）:
    * 打印 `TIDY-SEED` 通告 + 写候选基线文件（内容 = 现有记录 + 本次测量），exit 0（**不**判红）。
    * 由维护者复核候选文件后并入 tools/baseline/clang-tidy.tsv 入库（一次动作即可开启硬棘轮）。
    * CI 若要「未播种即红」用 --require-baseline（main-full 未启用：播种是口径动作，不是代码回归）。

  基线更新口径（唯一合法途径 = --update）:
    * 允许: ① 检查集/工具链/平台口径变更 ② 有意收敛后重新播种（如批量修完告警）③ 首次入库。
    * 禁止: 用它把**新增**告警洗白 —— 棘轮存在的意义就是让新增告警必须被修掉或被显式记录。
    * 更新后基线 diff 必须进同一提交（评审可见「基线变化」这一事实）。
"""

import argparse
import collections
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BUILD = os.path.join(ROOT, 'build', 'release-dev')
DEFAULT_BASELINE = os.path.join(ROOT, 'tools', 'baseline', 'clang-tidy.tsv')
DEFAULT_CHECKS = '-*,bugprone-*,performance-*,portability-*'
SCHEMA = 1

# 参与静态分析的源文件范围（与设计 §12.2「全源」口径一致；生成物/第三方一律在外）。
SCOPE_RE = re.compile(r'^(src|tests|tools)/')
SRC_EXT = ('.cpp', '.cc', '.cxx', '.c', '.h', '.hpp', '.hh')

# 诊断行: `<file>:<line>:<col>: warning: <msg> [<check>]`（clang-tidy 默认输出格式）
DIAG_RE = re.compile(r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<sev>warning|error): (?P<msg>.*)$')
CHECK_RE = re.compile(r'\[(?P<check>[-a-zA-Z0-9_.]+)\]\s*$')


def die(code, message):
    sys.stderr.write('ci-tidy: {}\n'.format(message))
    raise SystemExit(code)


def read_versions(key):
    """读 tools/versions.env 的单源键（钉版 clang-tidy 版本）。"""
    path = os.path.join(ROOT, 'tools', 'versions.env')
    try:
        with open(path, encoding='utf-8') as fp:
            for raw in fp:
                line = raw.strip()
                if not line or line.startswith('#'):
                    continue
                k, sep, v = line.partition('=')
                if sep and k.strip() == key:
                    return v.strip().strip('"').strip("'")
    except OSError:
        pass
    return ''


def find_clang_tidy(explicit):
    """按 ① --clang-tidy ② CLANG_TIDY ③ 仓库钉版 tools/bin ④ PATH 定位可执行文件。"""
    cands = []
    if explicit:
        cands.append(explicit)
    env = os.environ.get('CLANG_TIDY', '')
    if env:
        cands.append(env)
    names = ('clang-tidy.exe', 'clang-tidy') if os.name == 'nt' else ('clang-tidy', 'clang-tidy.exe')
    for n in names:
        cands.append(os.path.join(ROOT, 'tools', 'bin', n))
    for c in cands:
        if c and os.path.isfile(c) and os.access(c, os.X_OK if os.name != 'nt' else os.F_OK):
            return c
    which = shutil.which('clang-tidy')
    if which:
        return which
    return ''


def tool_version(exe):
    try:
        out = subprocess.run([exe, '--version'], capture_output=True, text=True, timeout=120)
    except (OSError, subprocess.SubprocessError) as exc:
        die(2, 'clang-tidy 无法执行: {} ({})'.format(exe, exc))
    text = (out.stdout or '') + (out.stderr or '')
    m = re.search(r'LLVM version ([0-9][0-9.]*)', text)
    if not m:
        die(2, 'clang-tidy --version 输出无法解析 LLVM 版本:\n{}'.format(text.strip()))
    return m.group(1), text.strip().splitlines()[0] if text.strip() else ''


def platform_id():
    return 'windows-msvc' if os.name == 'nt' else 'linux-gcc'


def compiler_version(build_dir, db):
    """编译器版本（只作基线记录/审计，不进 fingerprint）。

    两个来源，够用且与平台无关:
      ① CMakeCache.txt 的 CMAKE_CXX_COMPILER_VERSION（GCC/Clang 一般都会写）
      ② 编译库命令行里的 MSVC 工具集路径（`.../VC/Tools/MSVC/14.51.36231/...`；①缺失时的兜底）
    """
    path = os.path.join(build_dir, 'CMakeCache.txt')
    try:
        with open(path, encoding='utf-8', errors='replace') as fp:
            for line in fp:
                m = re.match(r'CMAKE_CXX_COMPILER_VERSION:[A-Z]+=(.*)$', line.strip())
                if m:
                    return m.group(1)
    except OSError:
        pass
    for entry in db:
        cmd = entry.get('command', '') or ' '.join(entry.get('arguments', []) or [])
        m = re.search(r'MSVC[\\/]([0-9]+\.[0-9]+\.[0-9]+)', cmd)
        if m:
            return 'MSVC ' + m.group(1)
    return 'unknown'


def load_compile_db(build_dir):
    path = os.path.join(build_dir, 'compile_commands.json')
    if not os.path.isfile(path):
        die(2, '缺 compile_commands.json: {}\n'
               '  → 先 configure 导出编译库: cmake --preset release-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'.format(path))
    try:
        with open(path, encoding='utf-8') as fp:
            db = json.load(fp)
    except (OSError, ValueError) as exc:
        die(2, 'compile_commands.json 读取失败: {} ({})'.format(path, exc))
    return db


def select_tus(db):
    """挑出仓库自有 TU（src/**, tests/**, tools/** 的源文件）；生成物与第三方一律排除。"""
    sel = []
    for entry in db:
        f = os.path.normpath(entry.get('file', ''))
        if not f:
            continue
        try:
            rel = os.path.relpath(os.path.abspath(f), ROOT).replace('\\', '/')
        except ValueError:
            continue
        if rel.startswith('..'):
            continue
        if not SCOPE_RE.match(rel):
            continue
        if not rel.lower().endswith(SRC_EXT):
            continue
        if '_autogen/' in rel or rel.startswith('build/'):
            continue
        sel.append(rel)
    return sorted(set(sel))


def run_one(exe, build_dir, checks, rel, timeout, env):
    """跑单个 TU；返回 (rel, counts_by_file, errors, seconds)。"""
    cmd = [exe, '-p', build_dir, '--checks=' + checks, '--quiet', os.path.join(ROOT, rel)]
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env,
                              cwd=ROOT, errors='replace')
    except subprocess.TimeoutExpired:
        return rel, {}, ['超时 {}s（--timeout 可调）'.format(timeout)], time.time() - t0
    text = (proc.stdout or '') + (proc.stderr or '')
    counts = collections.Counter()
    errors = []
    for line in text.splitlines():
        m = DIAG_RE.match(line.strip())
        if not m:
            continue
        sev = m.group('sev')
        raw = m.group('file')
        dfile = os.path.normpath(raw)
        if not os.path.isabs(dfile):
            dfile = os.path.abspath(os.path.join(build_dir, dfile))
        try:
            drel = os.path.relpath(os.path.abspath(dfile), ROOT).replace('\\', '/')
        except ValueError:
            drel = ''
        if sev == 'error':
            # 编译期硬错误（缺头/语法/缺 INCLUDE）= 计数不完整，绝不能当成"0 告警"放绿。
            errors.append('{}: error: {}'.format(raw, m.group('msg')[:200]))
            continue
        if not drel or drel.startswith('..') or not SCOPE_RE.match(drel):
            continue
        counts[drel] += 1
    if proc.returncode != 0 and not errors:
        tail = [ln for ln in text.splitlines() if 'Error while processing' in ln or 'error:' in ln]
        errors.append('clang-tidy 退出码 {}（{}）'.format(proc.returncode, tail[0].strip() if tail else '无明细'))
    return rel, counts, errors, time.time() - t0


def warn_missing_include(errors):
    """Windows 上未构造 vcvars 环境的典型症状（MSVC 系统头找不到）→ 给 env.py 指路。"""
    for e in errors:
        if 'file not found' in e or "'vector'" in e or "'cstddef'" in e or "'string'" in e:
            return True
    return False


# ── 基线文件 I/O ────────────────────────────────────────────────────────────
# 格式（TSV；# 起头为注释）:
#   REC  <fingerprint> <platform> <compiler> <tool> <checks> <total> <files> <generated_at>
#   FILE <fingerprint> <relpath> <count>            （只记 count>0 的行；缺行 = 0）
def read_baseline(path):
    records = {}
    files = collections.defaultdict(dict)
    if not os.path.isfile(path):
        return records, files
    with open(path, encoding='utf-8') as fp:
        for raw in fp:
            line = raw.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            parts = line.split('\t')
            if parts[0] == 'REC' and len(parts) >= 9:
                records[parts[1]] = {
                    'platform': parts[2], 'compiler': parts[3], 'tool': parts[4],
                    'checks': parts[5], 'total': int(parts[6]), 'files': int(parts[7]),
                    'generated_at': parts[8],
                }
            elif parts[0] == 'FILE' and len(parts) == 4:
                files[parts[1]][parts[2]] = int(parts[3])
    return records, files


def write_baseline(path, records, files, header_extra=''):
    lines = [
        '# PhotoPipeline — clang-tidy 棘轮基线（M4-W4-T16；docs/v0.3.0-design.md §12.2）',
        '# 口径: 总数 ≤ 基线 ／ 每文件 ≤ 基线 ／ 基线外文件（新增代码）必须 0 告警',
        '# 统计: bugprone-* / performance-* / portability-*，仅本仓库 src|tests|tools 源码',
        '# 更新: python tools/ci-tidy.py <BUILD_DIR> --update   （只允许在口径/工具链变更或有意收敛后重新播种）',
        '# 生成: python tools/ci-tidy.py <BUILD_DIR> [--update]（同一提交内基线 diff 必须可见）',
        '# schema={}'.format(SCHEMA),
    ]
    if header_extra:
        lines.append('# ' + header_extra)
    lines.append('# REC  <fingerprint>\t<platform>\t<compiler>\t<tool>\t<checks>\t<total>\t<files>\t<generated_at>')
    lines.append('# FILE <fingerprint>\t<relpath>\t<count>            （只记 count>0；缺行 = 0）')
    for fp_key in sorted(records):
        r = records[fp_key]
        lines.append('\t'.join(['REC', fp_key, r['platform'], r['compiler'], r['tool'], r['checks'],
                                str(r['total']), str(r['files']), r['generated_at']]))
        for rel in sorted(files.get(fp_key, {})):
            lines.append('\t'.join(['FILE', fp_key, rel, str(files[fp_key][rel])]))
    with open(path, 'w', encoding='utf-8', newline='\n') as fp:
        fp.write('\n'.join(lines) + '\n')


def main(argv):
    ap = argparse.ArgumentParser(add_help=True, description='clang-tidy 静态分析棘轮（M4-W4-T16）')
    ap.add_argument('build_dir', nargs='?', default=DEFAULT_BUILD)
    ap.add_argument('--checks', default=DEFAULT_CHECKS)
    ap.add_argument('--baseline', default=DEFAULT_BASELINE)
    ap.add_argument('--candidate', default='')
    ap.add_argument('--clang-tidy', default='')
    ap.add_argument('--jobs', type=int, default=0)
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--update', action='store_true')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--require-baseline', action='store_true')
    args = ap.parse_args(argv)

    build_dir = args.build_dir if os.path.isabs(args.build_dir) else os.path.join(ROOT, args.build_dir)
    build_dir = os.path.normpath(build_dir)
    db = load_compile_db(build_dir)
    tus = select_tus(db)
    if not tus:
        die(2, '编译库里没有匹配的 TU（src|tests|tools 下的源码）—— build_dir 是否正确？')

    if args.list:
        for rel in tus:
            print(rel)
        print('ci-tidy: {} 个 TU（只列清单，未运行 clang-tidy）'.format(len(tus)))
        return 0

    exe = find_clang_tidy(args.clang_tidy)
    if not exe:
        die(2, '找不到 clang-tidy。钉版安装: python -m pip install clang-tidy=={}（tools/versions.env 的 '
               'CLANG_TIDY_VERSION）；或用 --clang-tidy <path> / env CLANG_TIDY 指定'
               .format(read_versions('CLANG_TIDY_VERSION') or '<版本>'))
    ver, _ver_line = tool_version(exe)
    pinned = read_versions('CLANG_TIDY_VERSION')
    if pinned and ver != pinned:
        # 版本不符 = 与基线/CI 不可比（同 clang-format 门禁的版本断言口径）⇒ 环境错误。
        die(2, 'clang-tidy 版本不符: 实测 {} ≠ 单源 tools/versions.env 的 CLANG_TIDY_VERSION={}\n'
               '  → 安装钉版: python -m pip install clang-tidy=={}'.format(ver, pinned, pinned))
    platform = platform_id()
    compiler = compiler_version(build_dir, db)
    fp_key = '{}|clang-tidy {}|{}'.format(platform, ver, args.checks)
    jobs = args.jobs or (os.cpu_count() or 4)

    print('ci-tidy: tool=clang-tidy {} @ {}'.format(ver, exe))
    print('ci-tidy: build_dir={}  TU={}  jobs={}  checks={}'.format(build_dir, len(tus), jobs, args.checks))
    print('ci-tidy: platform={} compiler={} fingerprint={}'.format(platform, compiler, fp_key))

    t0 = time.time()
    counts_total = collections.Counter()
    failed = []
    env = dict(os.environ)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(run_one, exe, build_dir, args.checks, rel, args.timeout, env) for rel in tus]
        done = 0
        for fut in concurrent.futures.as_completed(futures):
            rel, counts, errors, secs = fut.result()
            done += 1
            if errors:
                failed.append((rel, errors))
            counts_total.update(counts)
            if done % 10 == 0 or done == len(tus):
                print('ci-tidy:   {}/{} TU 完成（累计告警 {}，{:.0f}s）'.format(
                    done, len(tus), sum(counts_total.values()), time.time() - t0))
    elapsed = time.time() - t0

    if failed:
        print('ci-tidy: {} 个 TU 解析失败（计数不完整 ⇒ 不可判绿）:'.format(len(failed)))
        for rel, errors in failed[:10]:
            print('  {}: {}'.format(rel, errors[0]))
        if any(warn_missing_include(err) for _, err in failed):
            print('  → Windows 上需构造 MSVC 环境（INCLUDE/LIB）: '
                  'python tools/env.py run -- python tools/ci-tidy.py {}'.format(args.build_dir))
        return 1

    total = sum(counts_total.values())
    n_files = len(counts_total)
    nonempty = sorted(counts_total.items(), key=lambda kv: (-kv[1], kv[0]))
    for rel, c in nonempty:
        print('ci-tidy:   {:>3}  {}'.format(c, rel))

    records, files = read_baseline(args.baseline)
    record = records.get(fp_key)
    seed = record is None

    if args.update:
        files[fp_key] = dict(counts_total)
        records[fp_key] = {
            'platform': platform, 'compiler': compiler, 'tool': 'clang-tidy ' + ver,
            'checks': args.checks, 'total': total, 'files': n_files,
            'generated_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        }
        write_baseline(args.baseline, records, files)
        print('TIDY-UPDATE total={} files={} baseline={}（基线已重写；diff 必须随本次提交评审）'
              .format(total, n_files, args.baseline))
        return 0

    if seed:
        candidate = args.candidate or (args.baseline + '.candidate.tsv')
        new_records = dict(records)
        new_files = {k: dict(v) for k, v in files.items()}
        new_files[fp_key] = dict(counts_total)
        new_records[fp_key] = {
            'platform': platform, 'compiler': compiler, 'tool': 'clang-tidy ' + ver,
            'checks': args.checks, 'total': total, 'files': n_files,
            'generated_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        }
        write_baseline(candidate, new_records, new_files)
        print('TIDY-SEED fingerprint 未入库 —— 本轮不判红（播种是口径动作，不是代码回归）。')
        print('TIDY-SEED fingerprint={}'.format(fp_key))
        print('TIDY-SEED 候选基线已写出: {}（复核后并入 {} 即开启硬棘轮）'.format(candidate, args.baseline))
        print('TIDY total={} files={} violations=0 seed=1'.format(total, n_files))
        if args.require_baseline:
            print('ci-tidy: --require-baseline 生效 ⇒ 未播种指纹按失败处理')
            return 1
        return 0

    # ── 硬棘轮 ──────────────────────────────────────────────────────────────
    base_files = files.get(fp_key, {})
    violations = []
    if total > record['total']:
        violations.append('总数 {} > 基线总数 {}（+{}）'.format(total, record['total'], total - record['total']))
    for rel, c in nonempty:
        if rel in base_files:
            if c > base_files[rel]:
                violations.append('{}: {} > 基线 {}（+{}）'.format(rel, c, base_files[rel], c - base_files[rel]))
        else:
            violations.append('{}: 基线外文件（新增代码）{} 条告警（须 0）'.format(rel, c))

    print('ci-tidy: 基线记录 platform={} compiler={} tool={} total={} files={} generated_at={}'
          .format(record['platform'], record['compiler'], record['tool'], record['total'],
                  record['files'], record['generated_at']))
    if violations:
        print('ci-tidy: 棘轮违规 {} 条（前 40 条）:'.format(len(violations)))
        for v in violations[:40]:
            print('ci-tidy:   ' + v)
        print('ci-tidy: 处置: 修掉新增告警；确属口径变更/有意收敛 ⇒ python tools/ci-tidy.py {} --update '
              '（基线 diff 进同一提交）'.format(args.build_dir))
        print('TIDY total={} baseline={} files={} violations={} seed=0'.format(
            total, record['total'], n_files, len(violations)))
        return 1

    print('ci-tidy: 棘轮通过（{} 文件，{:.0f}s，基线 {}）'.format(n_files, elapsed, args.baseline))
    print('TIDY total={} baseline={} files={} violations=0 seed=0'.format(total, record['total'], n_files))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)
