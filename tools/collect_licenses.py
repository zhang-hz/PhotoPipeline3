#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline 第三方许可文本汇总（M2-T11c；GPL/LGPL 分发合规）

用法:
  python tools/collect_licenses.py <APPDIR>      # AppDir 根（绝对或相对当前目录）
  python tools/collect_licenses.py <APPDIR> [--dest <目录>]   # --dest 缺省 = <APPDIR>/usr/share/licenses
  python tools/collect_licenses.py --check [--min N] [--share <目录>]   # M4-W4-T16 门禁模式（不写盘）
      M4-W4-T16 增补（设计 §12.2「许可/SPDX」判据行: 许可 ≥30 且 SPDX 100%）：
      ① 许可数 = share 下可收集的第三方许可文本数 + 本项目 LICENSE ≥ --min（默认 30）
      ② SPDX 覆盖 = src|tests|tools 源码（.c/.cc/.cpp/.cxx/.h/.hpp/.hh/.py/.sh/.cmake）
         逐文件含 `SPDX-License-Identifier`，必须 100%
      成功末行 `LICENSE-CHECK total=<n> min=<m> spdx=<c>/<t> ok=1`（CI 摘要/断言用）；
      退出码 0 = 全绿，1 = 判据不满足，2 = 入参/输入目录硬错误。
env 覆盖:
  PP_VCPKG_SHARE  vcpkg 已安装 port 的 share 根
                  （默认 <repo>/vcpkg_installed/<triplet>/share；
                   triplet = x64-windows-avx2（Windows）/ x64-linux-avx2（POSIX）；
                   M4-T3 起唯一构建 triplet 即 AVX2（CMakePresets 的 base-windows/base-linux），
                   旧的非 AVX2 目录（x64-windows / x64-linux）已不再由 vcpkg 安装）

收集规则（M2-T11c 冻结，与 tools/make_appimage.sh 头注释一致）:
  1) 遍历 share/<entry>/ 每个条目，取**第一个**存在的许可文本:
       精确名 copyright 优先；否则 LICENSE* / LICENCE* / COPYING* 中
       LC_ALL=C 字典序第一个（大小写不敏感匹配，兼容 port 的两种落盘形态）。
  2) 落盘为 usr/share/licenses/<entry>/copyright：目录名保持 vcpkg port 名
     （便于按 port 溯源/对账），文件内容逐字节不改（仅在源头改名时才统一目标名）。
  3) 本项目自身的 LICENSE（GPL-3.0-or-later）→ usr/share/licenses/PhotoPipeline/LICENSE。
  4) 无许可文本的条目**必须列名**（不静默跳过），并按性质分两类打印:
       * 真实 port（share/<entry>/ 含 vcpkg_abi_info.txt，vcpkg 对每个已装 port 都会写）
         → 「许可缺失（真实 port）」，属真缺口，需在报告里给处置建议；
       * 非 port 条目（CMake 配置/别名 shim：png→libpng、WebP→libwebp、lcms2→lcms、
         jpeg→libjpeg-turbo、gif→giflib、iconv→libiconv、hwy→highway、
         tsl-robin-map→robin-map、unofficial-*→对应 port、SVT-AV1→svt-av1；
         以及 vcpkg 自带的文档目录 doc/ man/）→ 其许可由对应 port 覆盖，仅列名提示。
  5) 幂等: 先 rm -rf <APPDIR>/usr/share/licenses 再收集，重复运行结果一致。
  6) 退出码: 0 = 汇总完成（缺失条目已列名，不视为失败）；1 = 入参/输入目录硬错误。

M3-W3（裁定 D3：Python 单实现，本文件替代 tools/collect_licenses.sh）:
  * PP_VCPKG_SHARE 默认按平台 triplet 分派（x64-windows / x64-linux）
  * 排序用 Python sorted（码点序 ≡ LC_ALL=C 字节序），输出与 locale 无关
  * 全部输出行（中文）与退出码 0/1 逐字不变
"""

import os
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRIPLET = 'x64-windows-avx2' if os.name == 'nt' else 'x64-linux-avx2'


def die(message):
    sys.stderr.write('collect_licenses: {}\n'.format(message))
    raise SystemExit(1)


def pick_license(directory):
    """取一个条目目录下的许可文本路径（无 → 空字符串）。"""
    copyright_path = os.path.join(directory, 'copyright')
    if os.path.isfile(copyright_path):
        return copyright_path
    candidates = []
    try:
        names = os.listdir(directory)
    except OSError:
        names = []
    for name in names:
        full = os.path.join(directory, name)
        if not os.path.isfile(full):
            continue
        lowered = name.lower()
        if (lowered.startswith('license') or lowered.startswith('licence')
                or lowered.startswith('copying')):
            candidates.append(full)
    candidates.sort()
    return candidates[0] if candidates else ''


def sort_list(items):
    """列表按字典序排序后以空格分隔（bash: sort | tr '\\n' ' '，末尾保留一个空格）。"""
    if not items:
        return ''
    return ' '.join(sorted(items)) + ' '


def collect(share, dest, write):
    """遍历 share 收集许可文本 → (collected, renamed, missing_ports, missing_nonports)。

    write=True 时落盘到 dest（collect_licenses 的默认行为，逐字节与 M2-T11c 冻结口径一致）；
    write=False 时只统计（`--check` 门禁用，不落盘、幂等、零副作用）。
    """
    collected = 0
    renamed = []
    missing_ports = []
    missing_nonports = []

    for entry in sorted(os.listdir(share)):
        directory = os.path.join(share, entry)
        if not os.path.isdir(directory):
            continue
        src = pick_license(directory)
        if src:
            if write:
                os.makedirs(os.path.join(dest, entry), exist_ok=True)
                shutil.copyfile(src, os.path.join(dest, entry, 'copyright'))
            collected += 1
            if os.path.basename(src) != 'copyright':
                renamed.append('{} <- {}'.format(entry, os.path.basename(src)))
        elif os.path.isfile(os.path.join(directory, 'vcpkg_abi_info.txt')):
            missing_ports.append(entry)
        else:
            missing_nonports.append(entry)
    return collected, renamed, missing_ports, missing_nonports


# ── --check（M4-W4-T16；设计 §12.2「许可/SPDX」门禁行：许可 ≥30 且 SPDX 100%）────────────
#   判据① 许可数 = share 下可收集到的第三方许可文本数 + 本项目 LICENSE ≥ --min（默认 30）
#   判据② SPDX 覆盖 = src|tests|tools 源码（.c/.cc/.cpp/.cxx/.h/.hpp/.hh/.py/.sh/.cmake）
#          逐文件含 `SPDX-License-Identifier`；必须 100%（缺一即点名并失败）
#   注: 测试数据（tests/golden/**/*.json、SCHEMA.md）不在审计范围 —— 它们不是源码分发单元。
CHECK_EXT = ('.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.hh', '.py', '.sh', '.cmake')
CHECK_TREES = ('src', 'tests', 'tools')
SPDX_TAG = 'SPDX-License-Identifier'


def spdx_audit():
    """返回 (covered, total, missing[])；git 不可用/无跟踪文件 → (None, None, ['<git ...>'])。"""
    try:
        out = subprocess.run(['git', 'ls-files'] + list(CHECK_TREES), capture_output=True,
                             text=True, cwd=ROOT, timeout=120)
    except (OSError, subprocess.SubprocessError) as exc:
        return None, None, ['git ls-files 失败: {}'.format(exc)]
    if out.returncode != 0:
        return None, None, ['git ls-files 退出码 {}: {}'.format(out.returncode, out.stderr.strip())]
    covered = 0
    total = 0
    missing = []
    for rel in out.stdout.splitlines():
        rel = rel.strip()
        if not rel or not rel.lower().endswith(CHECK_EXT):
            continue
        total += 1
        try:
            with open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as fp:
                head = fp.read(1200)
        except OSError as exc:
            missing.append('{}（读取失败: {}）'.format(rel, exc))
            continue
        if SPDX_TAG in head:
            covered += 1
        else:
            missing.append(rel)
    return covered, total, missing


def check_mode(argv):
    """`--check`：许可数 + SPDX 覆盖率硬判据（不写盘）。返回进程退出码。"""
    argv = list(argv)
    minimum = 30
    share_override = ''
    if '--min' in argv:
        k = argv.index('--min')
        if k + 1 >= len(argv):
            die('用法: tools/collect_licenses.py --check [--min N] [--share <目录>]（--min 缺参数）')
        try:
            minimum = int(argv[k + 1])
        except ValueError:
            die('--min 必须是整数: {!r}'.format(argv[k + 1]))
        del argv[k:k + 2]
    if '--share' in argv:
        k = argv.index('--share')
        if k + 1 >= len(argv):
            die('用法: tools/collect_licenses.py --check [--min N] [--share <目录>]（--share 缺参数）')
        share_override = argv[k + 1]
        del argv[k:k + 2]
    stray = [a for a in argv if a != '--check']
    if stray:
        die('--check 不接受位置参数/其它开关: {}'.format(' '.join(stray)))

    share = share_override or os.environ.get('PP_VCPKG_SHARE') or os.path.join(
        ROOT, 'vcpkg_installed', TRIPLET, 'share')
    root_license = os.path.join(ROOT, 'LICENSE')
    if not os.path.isdir(share):
        die('vcpkg share 目录不存在: {}（先 vcpkg install / 设 PP_VCPKG_SHARE / --share）'.format(share))
    if not os.path.isfile(root_license):
        die('项目 LICENSE 缺失: {}'.format(root_license))

    collected, _, missing_ports, _ = collect(share, '', write=False)
    total_lic = collected + 1          # + PhotoPipeline/LICENSE
    covered, spdx_total, missing_spdx = spdx_audit()

    print('collect_licenses --check: share={}'.format(share))
    print('collect_licenses --check: 许可数 = {}（第三方 {} + PhotoPipeline 1）判据 ≥ {}'.format(
        total_lic, collected, minimum))
    if missing_ports:
        print('collect_licenses --check: 警告: 许可缺失（真实 port）: {}'.format(
            sort_list(missing_ports)), file=sys.stderr)
    if spdx_total is None:
        die('SPDX 审计无法进行: {}'.format('; '.join(missing_spdx)))

    ok = True
    if total_lic < minimum:
        print('collect_licenses --check: FAIL 许可数不足: {} < {}'.format(total_lic, minimum),
              file=sys.stderr)
        ok = False
    if missing_spdx:
        print('collect_licenses --check: FAIL SPDX 覆盖不足（{}/{}），缺 {} 个（前 20）:'.format(
            covered, spdx_total, len(missing_spdx)), file=sys.stderr)
        for rel in missing_spdx[:20]:
            print('collect_licenses --check:   ' + rel, file=sys.stderr)
        ok = False
    print('collect_licenses --check: SPDX 覆盖 = {}/{}（判据 100%）'.format(covered, spdx_total))
    print('LICENSE-CHECK total={} min={} spdx={}/{} ok={}'.format(
        total_lic, minimum, covered, spdx_total, 1 if ok else 0))
    return 0 if ok else 1


def main(argv):
    argv = list(argv)
    if '--check' in argv:
        return check_mode(argv)
    dest_override = ''
    if '--dest' in argv:
        k = argv.index('--dest')
        if k + 1 >= len(argv):
            die('用法: tools/collect_licenses.py <APPDIR> [--dest <目录>]（--dest 缺参数）')
        dest_override = argv[k + 1]
        del argv[k:k + 2]
    appdir = argv[0] if argv else ''
    if not appdir:
        die('用法: tools/collect_licenses.py <APPDIR>（AppDir 根）')
    if not os.path.isabs(appdir):
        appdir = os.getcwd() + '/' + appdir
    share = os.environ.get('PP_VCPKG_SHARE') or os.path.join(
        ROOT, 'vcpkg_installed', TRIPLET, 'share')
    root_license = os.path.join(ROOT, 'LICENSE')
    if dest_override and not os.path.isabs(dest_override):
        dest_override = os.getcwd() + '/' + dest_override
    dest = dest_override or (appdir + '/usr/share/licenses')

    if not os.path.isdir(appdir):
        die('AppDir 不存在: {}'.format(appdir))
    if not os.path.isdir(share):
        die('vcpkg share 目录不存在: {}（先 vcpkg install / 设 PP_VCPKG_SHARE）'.format(share))
    if not os.path.isfile(root_license):
        die('项目 LICENSE 缺失: {}'.format(root_license))

    # ---- 幂等：先清空目标目录再收集 ----
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest, exist_ok=True)

    collected, renamed, missing_ports, missing_nonports = collect(share, dest, write=True)

    # ---- 本项目许可（GPL-3.0-or-later） ----
    os.makedirs(dest + '/PhotoPipeline', exist_ok=True)
    shutil.copyfile(root_license, dest + '/PhotoPipeline/LICENSE')

    # 列表排序：输出与 locale 无关，保证重复运行可逐字比对
    print('collect_licenses: 第三方条目 {} 个 + PhotoPipeline/LICENSE -> {}'.format(
        collected, dest))
    if renamed:
        print('collect_licenses: 源文件名非 copyright（目标名统一为 copyright）: {}'.format(
            sort_list(renamed)))
    if missing_ports:
        print('collect_licenses: 警告: 许可缺失（真实 port，share/<port>/ 无 '
              'copyright|LICENSE*|COPYING*|LICENCE*）: {}'.format(sort_list(missing_ports)),
              file=sys.stderr)
    if missing_nonports:
        print('collect_licenses: 提示: 非 port 条目无许可文件（CMake 配置/别名 shim 或 vcpkg '
              '文档目录，许可由对应 port 覆盖）: {}'.format(sort_list(missing_nonports)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
