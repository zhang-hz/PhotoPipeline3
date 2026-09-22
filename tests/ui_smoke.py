#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — M1b-U10 offscreen UI smoke（ctest 名 ui_smoke，由 CMake 在 PP_BUILD_DEV 下注册）

用法: tests/ui_smoke.py [BUILD_DIR]      # 默认 build/release-dev
退出码: 0 = 冻结断言全过（末行 `UI-SMOKE OK shots=8 pages=3`，§4.3 冻结 8 张）；
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
  * 冻结行为不变：QT_QPA_PLATFORM=offscreen、`^UI-SMOKE OK shots=8 pages=3$` 断言、
    `UI-SMOKE pass` 成功行与两条缺失提示逐字
"""

import os
import subprocess
import sys
import tempfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

FROZEN_LINE = 'UI-SMOKE OK shots=8 pages=3'

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
        with open(log_path, 'w', encoding='utf-8', newline='') as log_fp:
            for line in proc.stdout:
                sys.stdout.write(line)
                sys.stdout.flush()
                log_fp.write(line)
                if line.rstrip('\r\n') == FROZEN_LINE:
                    found = True
        status = proc.wait()

        if status != 0:
            print('ui_smoke: FAIL (photopipeline --ui-smoke exit {})'.format(status),
                  file=sys.stderr)
            return status
        if not found:
            print("ui_smoke: FAIL (missing frozen success line '{}')".format(FROZEN_LINE),
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
