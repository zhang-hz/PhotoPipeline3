#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PP-FROZEN(structure): fresh-machine bootstrap (everything repo-local)

M3-W3（裁定 D3：共享脚本 Python 单实现）下的平台增量，均已在本文件内实现:
  * aqt arch 分派: Linux → <venv>/bin/aqt install-qt linux desktop $QT linux_gcc_64 -O .toolchain/Qt
                   Windows → ... install-qt windows desktop $QT win64_msvc2022_64 -O .toolchain/Qt
    （QT_VERSION 来自 tools/versions.env 解析）
  * venv 解释器/脚本目录: Windows = Scripts/{python.exe,pip.exe,aqt.exe}（venv 由运行本脚本的
    解释器创建；Windows 的 `python3` 常是 Microsoft Store 占位别名，不可用），POSIX = bin/{python,pip,aqt}
  * 仅 Windows: 继续向同一 venv `pip install cmake ninja`（VS 的 cmake/ninja 不在 PATH，
    仓库内工具链自足）
  * vcpkg bootstrap: POSIX 用 bootstrap-vcpkg.sh -disableMetrics，Windows 用 bootstrap-vcpkg.bat
    （clone/fetch/checkout VCPKG_TAG 逻辑与 bash 版同源）
  * 删除原 `cp -n tools/env.sh.example tools/env.sh` 步骤：环境故事由 tools/env.py 接管
  * 最终 OK 行改为 `python tools/env.py run -- ...` 形态

原 bash 版（tools/bootstrap.sh）语义不变，除上述规格给定的 delta 外逐条对齐:
  set -euo pipefail（失败即以该命令退出码退出）、
  venv 创建失败静默回退 `pip3 install --target .toolchain/pylibs aqtinstall`、
  aqt 用 HOME=<repo>/.cache/aqt-home 隔离缓存。

用法: python tools/bootstrap.py
"""

import os
import shutil
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WINDOWS = os.name == 'nt'
# bash 版写死 `python3`。Windows 上 `python3` 往往是 Microsoft Store 的占位别名
# （WindowsApps\python3.exe，实测不可用：exit 1 且无输出），故 Windows 直接用运行本
# 脚本的解释器；POSIX 保持 `python3`（找不到再回退到同一解释器）。
if WINDOWS:
    PYTHON = sys.executable
else:
    PYTHON = shutil.which('python3') or sys.executable


def read_versions():
    """解析 tools/versions.env（KEY=VALUE，'#' 起头为注释）→ dict。"""
    values = {}
    with open(os.path.join(ROOT, 'tools', 'versions.env'), encoding='utf-8') as fp:
        for raw in fp:
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            key, sep, value = line.partition('=')
            if sep:
                values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def sh(cmd, env=None, quiet_stderr=False, check=True):
    """执行命令；check=True 时等价 bash `set -e`（非零 → 以该退出码退出）。"""
    kwargs = {'env': env}
    if quiet_stderr:
        kwargs['stderr'] = subprocess.DEVNULL
    rc = subprocess.run(cmd, **kwargs).returncode
    if check and rc != 0:
        sys.exit(rc)
    return rc


def main():
    versions = read_versions()
    qt_version = versions.get('QT_VERSION', '')
    vcpkg_tag = versions.get('VCPKG_TAG', '')

    toolchain = os.path.join(ROOT, '.toolchain')
    binary_cache = os.path.join(ROOT, '.cache', 'vcpkg-binary')
    os.makedirs(toolchain, exist_ok=True)
    os.makedirs(binary_cache, exist_ok=True)

    venv = os.path.join(toolchain, 'venv')
    if WINDOWS:
        venv_bin, venv_exe = os.path.join(venv, 'Scripts'), '.exe'
    else:
        venv_bin, venv_exe = os.path.join(venv, 'bin'), ''
    pip = os.path.join(venv_bin, 'pip' + venv_exe)
    aqt = os.path.join(venv_bin, 'aqt' + venv_exe)

    if sh([PYTHON, '-m', 'venv', venv], quiet_stderr=True, check=False) == 0:
        sh([pip, '--no-cache-dir', 'install', 'aqtinstall'])
        if WINDOWS:
            # M3-W3 设计增量: VS 的 cmake/ninja 不在 PATH，装进同一 venv 让仓库内工具链自足。
            sh([pip, '--no-cache-dir', 'install', 'cmake', 'ninja'])
        aqt_cmd = [aqt]
        aqt_env = None
    else:
        # bash 版写死 `pip3`；Windows 上 `pip3` 也可能是 Store 别名占位，故 Windows
        # 一律走 `<解释器> -m pip`，POSIX 优先 `pip3`（找不到再回退）。
        pip3 = None if WINDOWS else shutil.which('pip3')
        fallback = [pip3] if pip3 else [PYTHON, '-m', 'pip']
        sh(fallback + ['--no-cache-dir', 'install',
                       '--target', os.path.join(toolchain, 'pylibs'), 'aqtinstall'])
        aqt_cmd = [PYTHON, '-m', 'aqt']
        aqt_env = dict(os.environ, PYTHONPATH=os.path.join(toolchain, 'pylibs'))

    aqt_home = os.path.join(ROOT, '.cache', 'aqt-home')
    os.makedirs(aqt_home, exist_ok=True)
    install_env = dict(os.environ if aqt_env is None else aqt_env, HOME=aqt_home)
    qt_out = os.path.join(toolchain, 'Qt')
    if WINDOWS:
        sh(aqt_cmd + ['install-qt', 'windows', 'desktop', qt_version,
                      'win64_msvc2022_64', '-O', qt_out], env=install_env)
    else:
        sh(aqt_cmd + ['install-qt', 'linux', 'desktop', qt_version,
                      'linux_gcc_64', '-O', qt_out], env=install_env)

    vcpkg = os.path.join(ROOT, 'vcpkg')
    if not os.path.isdir(vcpkg):
        sh(['git', 'clone', 'https://github.com/microsoft/vcpkg', vcpkg])
    sh(['git', '-C', vcpkg, 'fetch', '--tags'])
    sh(['git', '-C', vcpkg, 'checkout', vcpkg_tag])
    bootstrap = os.path.join(vcpkg, 'bootstrap-vcpkg.bat' if WINDOWS else 'bootstrap-vcpkg.sh')
    sh([bootstrap, '-disableMetrics'])

    print('OK — python tools/env.py run -- cmake --preset release '
          '&& python tools/env.py run -- cmake --build --preset release')
    return 0


if __name__ == '__main__':
    sys.exit(main())
