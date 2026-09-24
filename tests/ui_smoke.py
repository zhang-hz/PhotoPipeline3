#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — M1b-U10 offscreen UI smoke（ctest 名 ui_smoke，由 CMake 在 PP_BUILD_DEV 下注册）

用法: tests/ui_smoke.py [BUILD_DIR]      # 默认 build/release-dev
退出码: 0 = 冻结断言全过（末行 `UI-SMOKE OK shots=12 pages=3`，M4-W3-T14 起冻结 12 张）；
        2 = 用法/二进制缺失；其余 = photopipeline --ui-smoke 的退出码透传。

截图目录（M2-T21c 隔离裁定）：
  * CI/ctest（本脚本默认）固定写 .cache/ui-smoke-ci —— 本脚本总是以 QT_QPA_PLATFORM=
    offscreen 运行，产物是 Fusion/offscreen 口径，**不是**真实平台渲染。
  * 人工审查集固定用 .cache/ui-review（真实平台渲染，不带 QT_QPA_PLATFORM=offscreen，
    由 `photopipeline --ui-smoke --shots .cache/ui-review` 显式产出）。
  二者必须分开：否则任何人跑一次 ctest 就会用 offscreen 图覆盖人工审查基线
  （M2-T21b 复核期间实测发生过：8/8 字节与 sha256 全变、强调色由 Yaru 橙变 Fusion 蓝）。

M3-W3（裁定 D3：Python 单实现，本文件替代 tests/ui_smoke.sh）:
  * BIN 按 name / name + '.exe' 双探测（Windows 产物带 .exe）
  * `mktemp -t` → tempfile；tee 用 Python 边读边写（终端透传 + 临时日志）
  * 冻结行为不变：QT_QPA_PLATFORM=offscreen、`^UI-SMOKE OK shots=12 pages=3$` 断言、
    `UI-SMOKE pass` 成功行与两条缺失提示逐字

M4-W2-T11 扩展段（§6.2 + §3.6 filelistmodel.h 行）:
  * 追加"T11 自检行齐备"断言：勾选/搜索正交/分组节头/分类打标/classes.json 往返/圈选/只读
    这些自动化部分由 `photopipeline --ui-smoke` 在进程内断言（smoke_fail → 退出码 1）并逐行
    以 `UI-SMOKE <tag>:` 打印；本脚本额外要求这些**行存在**（防止断言被静默跳过/裁剪）。
    清单 = 前缀匹配（行内容由 C++ 侧给出，脚本只验 tag 存在）。

M4-W3-T14（运行页）:
  * 冻结末行**升级** `shots=8` → **`shots=12`**（逐字；新增 4 张运行页图：03c-run-idle /
    03d-run-rows / 03e-run-cancel / 07-run-light，见 mainwindow.cpp 的 kSmokeShots）。
  * 追加运行页自检行齐备断言（逐输出行数/结束态定稿/真实vs斜纹像素取证/锁定态/总览读数/
    取消态/空闲态/浅色主题/日志尾/底栏运行读数）。

M4-W3-T15（设置对话框追加项）:
  * 追加 `UI-SMOKE settings-dialog:` 行齐备断言（§9.3 三项：交错启动 ms / 线程预算 /
    分文件夹默认结构 → AppSettings 读回；截图仍为 12 张、冻结末行不变）。
"""

import os
import subprocess
import sys
import tempfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

FROZEN_LINE = 'UI-SMOKE OK shots=12 pages=3'

# M4-W3-T14 运行页自检行（前缀匹配；缺任一 → FAIL）
T14_LINE_PREFIXES = (
    'UI-SMOKE 03-run:',        # 真实运行读数：progress / outputs / 底栏「运行中… N%」
    'UI-SMOKE run-rows:',      # 真实运行：逐输出行数 = 文件数 × 格式数
    'UI-SMOKE run-bottom:',    # 底栏运行态：状态计数 + ETA 约 mm:ss + .go「运行中… N%」
    'UI-SMOKE run-rows-final:',  # 结束态：全行已结算 + 产物事实（字节/压缩比）+ 失败原因
    'UI-SMOKE lock-visual:',   # 运行期锁定（G5）：步骤1-2/设置/预设/文件面板/预览/页1-2/取消钮
    'UI-SMOKE run-log-tail:',  # 日志尾卡（单一持有者 = MainWindow 读 run-*.log）
    'UI-SMOKE run-idle:',      # 运行页空闲态（就绪 + 引导 + 空任务表）
    'UI-SMOKE run-probe-rows:',  # 探针页逐输出行：真实行级/合成/失败原因/排队/产物事实
    'UI-SMOKE run-bars:',      # 真实 vs 斜纹像素取证（accent 实心 vs 双色带）
    'UI-SMOKE run-overview:',  # 总览只读栅格（交错/线程预算/并行/当前分配/输出计数）
    'UI-SMOKE run-cancel:',    # 取消态（唯一取消入口 → 定稿「已取消」）
    'UI-SMOKE run-light:',     # 浅色主题下的斜纹渲染
)

# M4-W3-T15 自检行（前缀匹配；缺任一 → FAIL）：设置对话框三项追加控件 → AppSettings 读回
T15_LINE_PREFIXES = (
    'UI-SMOKE settings-dialog:',  # 交错启动 / 线程预算 / 分文件夹默认结构（§9.3）
)

# M4-W2-T11 自检行（前缀匹配；缺任一 → FAIL）
T11_LINE_PREFIXES = (
    'UI-SMOKE filelist-roles:',
    'UI-SMOKE filelist-check:',
    'UI-SMOKE filelist-search:',
    'UI-SMOKE filelist-group-format:',
    'UI-SMOKE filelist-group-camera:',
    'UI-SMOKE filelist-group-none:',
    'UI-SMOKE filelist-group-month:',
    'UI-SMOKE filelist-span:',
    'UI-SMOKE filelist-collapse:',
    'UI-SMOKE classify-panel:',
    'UI-SMOKE classify-tag:',
    'UI-SMOKE classify-menu:',
    'UI-SMOKE classify-bulk:',
    'UI-SMOKE classify-crud:',
    'UI-SMOKE classify-roundtrip:',
    'UI-SMOKE classify-lock:',
)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

build_dir = sys.argv[1] if len(sys.argv) > 1 else 'build/release-dev'
if not os.path.isabs(build_dir):
    build_dir = os.getcwd() + '/' + build_dir
bin_path = build_dir + '/photopipeline'


def resolve_exe(path):
    """name 与 name + '.exe' 双探测（Windows 上实际产物带 .exe）；找不到 → None。"""
    for candidate in (path, path + '.exe'):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def main():
    binary = resolve_exe(bin_path)
    if binary is None:
        print('ui_smoke: binary not found or not executable: {}'.format(bin_path),
              file=sys.stderr)
        print('ui_smoke: build it first, e.g. cmake --preset release-dev '
              '-DVCPKG_MANIFEST_INSTALL=OFF && cmake --build --preset release-dev -j',
              file=sys.stderr)
        return 2

    shots = os.path.join(ROOT, '.cache', 'ui-smoke-ci')
    os.makedirs(shots, exist_ok=True)

    handle, log_path = tempfile.mkstemp(prefix='ui-smoke-', suffix='.log')
    os.close(handle)
    try:
        child_env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
        proc = subprocess.Popen(
            [binary, '--ui-smoke', '--inputs', ROOT + '/tests/golden/base', '--shots', shots],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=ROOT, env=child_env,
            text=True, encoding='utf-8', errors='replace')
        found = False
        t11_seen = set()
        t14_seen = set()
        t15_seen = set()
        with open(log_path, 'w', encoding='utf-8', newline='') as log_fp:
            for line in proc.stdout:
                sys.stdout.write(line)
                sys.stdout.flush()
                log_fp.write(line)
                if line.rstrip('\r\n') == FROZEN_LINE:
                    found = True
                for prefix in T11_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t11_seen.add(prefix)
                for prefix in T14_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t14_seen.add(prefix)
                for prefix in T15_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t15_seen.add(prefix)
        status = proc.wait()

        if status != 0:
            print('ui_smoke: FAIL (photopipeline --ui-smoke exit {})'.format(status),
                  file=sys.stderr)
            return status
        if not found:
            print("ui_smoke: FAIL (missing frozen success line '{}')".format(FROZEN_LINE),
                  file=sys.stderr)
            return 1
        missing = [p for p in T11_LINE_PREFIXES if p not in t11_seen]
        if missing:
            print('ui_smoke: FAIL (missing T11 self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in T14_LINE_PREFIXES if p not in t14_seen]
        if missing:
            print('ui_smoke: FAIL (missing T14 self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in T15_LINE_PREFIXES if p not in t15_seen]
        if missing:
            print('ui_smoke: FAIL (missing T15 self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        print('UI-SMOKE pass')
        return 0
    finally:
        try:
            os.unlink(log_path)
        except OSError:
            pass


if __name__ == '__main__':
    sys.exit(main())
