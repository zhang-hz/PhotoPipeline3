#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline 工具链环境（M3-W3：Python 单实现，替代 tools/env.sh / env.sh.example）。

用法:
  python tools/env.py run -- <command...>   构造环境后运行命令，透传其退出码
  python tools/env.py print                 按 KEY 排序逐行打印 KEY=VALUE（调试/CI 用）

背景（M3 裁定 D3：共享脚本 Python 单实现，删除 bash 版）:
  * tools/env.sh.example 已删除（env.sh 是本地 `cp -n` 生成物）；环境故事由本脚本接管。
  * CMakePresets 已自包含（CMakeLists.txt 自己读 tools/versions.env 推 QT_DIR），
    故 env.py 只补工具链环境，不做别的事。
  * 构造内容（单实现内平台分派）:
      公共    VCPKG_ROOT=<repo>/vcpkg
              VCPKG_DEFAULT_BINARY_CACHE=<repo>/.cache/vcpkg-binary（缺失则创建）
              QT_DIR=<repo>/.toolchain/Qt/<QT_VERSION>/<arch>
                     （仅当该目录存在时设置；QT_VERSION 来自 tools/versions.env；
                      arch: Windows=msvc2022_64，否则 gcc_64）
      POSIX   CCACHE_DIR=<repo>/.cache/ccache
      Windows PATH 前置 <repo>/.toolchain/venv/Scripts 与
              <repo>/.toolchain/Qt/<QT_VERSION>/msvc2022_64/bin；
              并用 vswhere + vcvars64.bat 捕获 MSVC 工具链环境（VS 的 cl/link 不在
              PATH，仓库内工具链自足）；vswhere 缺失或失败只提示一行，不致命。

退出码:
  run     子命令退出码（命令不存在 → 127）
  print   0
  用法错   2
"""

import locale
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

USAGE = ("用法: python tools/env.py run -- <command...>\n"
         "      python tools/env.py print\n")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_versions():
    """解析 tools/versions.env（KEY=VALUE，'#' 起头为注释）→ dict。"""
    values = {}
    path = os.path.join(ROOT, 'tools', 'versions.env')
    try:
        with open(path, encoding='utf-8') as fp:
            for raw in fp:
                line = raw.strip()
                if not line or line.startswith('#'):
                    continue
                key, sep, value = line.partition('=')
                if not sep:
                    continue
                values[key.strip()] = value.strip().strip('"').strip("'")
    except OSError:
        pass
    return values


def set_key(env, key, value):
    """大小写不敏感地写 env[key]（Windows 上 PATH/Path 混写会产生重复项）。"""
    for existing in list(env):
        if existing.upper() == key.upper():
            env[existing] = value
            return
    env[key] = value


def get_key(env, key):
    for existing in env:
        if existing.upper() == key.upper():
            return env[existing]
    return None


def prepend_path(env, entries):
    """PATH 前置若干目录（去重、保留原有顺序）。"""
    current = get_key(env, 'PATH') or ''
    parts = [e for e in entries if e]
    for old in current.split(os.pathsep):
        if old and old not in parts:
            parts.append(old)
    set_key(env, 'PATH', os.pathsep.join(parts))


def decode_console(data):
    """cmd 管道输出多为控制台 OEM 代码页；逐个候选解码，全部失败则 replace。"""
    candidates = ['utf-8', 'oem', locale.getpreferredencoding(False)]
    for enc in candidates:
        if not enc:
            continue
        try:
            return data.decode(enc)
        except (UnicodeDecodeError, LookupError):
            continue
    return data.decode('utf-8', errors='replace')


def capture_vcvars(env):
    """Windows：vswhere → vcvars64.bat → `set`，把 MSVC 环境覆盖进 env。

    vswhere 不存在/失败、vcvars64.bat 缺失/失败 → stderr 一行提示后跳过（不致命）。
    """
    hint = ("env: 提示: 未捕获 MSVC 工具链环境（vswhere 或 vcvars64.bat 不可用），"
            "cl/link 可能不在 PATH")
    program_files_x86 = get_key(env, 'ProgramFiles(x86)')
    vswhere = (os.path.join(program_files_x86, 'Microsoft Visual Studio', 'Installer',
                            'vswhere.exe') if program_files_x86 else None)
    if not vswhere or not os.path.isfile(vswhere):
        print(hint, file=sys.stderr)
        return
    try:
        probe = subprocess.run(
            [vswhere, '-latest', '-products', '*',
             '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
             '-property', 'installationPath'],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
    except OSError:
        print(hint, file=sys.stderr)
        return
    vs_root = decode_console(probe.stdout).strip().splitlines()
    vs_root = vs_root[0].strip() if vs_root else ''
    if probe.returncode != 0 or not vs_root:
        print(hint, file=sys.stderr)
        return
    vcvars = os.path.join(vs_root, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
    if not os.path.isfile(vcvars):
        print(hint, file=sys.stderr)
        return
    # 规格给定形式 `cmd /s /c ""<vcvars64.bat>" && set"`：列表参数 + 禁 shell=True 时，
    # subprocess 会把内层引号反斜杠转义（cmd 不认 `\"`），实测 rc=1 且 env 为空；
    # 故用等价的列表形式（cmd 收到 `call "<vcvars64.bat>" && set`）。
    captured = subprocess.run(['cmd', '/c', 'call', vcvars, '&&', 'set'],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
    if captured.returncode != 0:
        print(hint, file=sys.stderr)
        return
    for line in decode_console(captured.stdout).splitlines():
        key, sep, value = line.partition('=')
        if not sep or not key or key.startswith('['):
            continue
        set_key(env, key, value)


def build_env():
    """构造完整子进程环境（os.environ 副本 + 工具链增量）。

    顺序：Windows 先捕获 vcvars，再写仓库值 —— vcvars64.bat 自己会导出
    VCPKG_ROOT=%VCINSTALLDIR%vcpkg（VS 自带 vcpkg），后写者必须是本仓库的值。
    PATH 前置放在最后，保证 <repo>/.toolchain 的工具优先于 VS/系统。
    """
    env = dict(os.environ)
    if os.name == 'nt':
        capture_vcvars(env)

    version = read_versions().get('QT_VERSION', '')
    arch = 'msvc2022_64' if os.name == 'nt' else 'gcc_64'
    binary_cache = os.path.join(ROOT, '.cache', 'vcpkg-binary')
    os.makedirs(binary_cache, exist_ok=True)

    set_key(env, 'VCPKG_ROOT', os.path.join(ROOT, 'vcpkg'))
    set_key(env, 'VCPKG_DEFAULT_BINARY_CACHE', binary_cache)
    qt_dir = os.path.join(ROOT, '.toolchain', 'Qt', version, arch)
    if os.path.isdir(qt_dir):
        set_key(env, 'QT_DIR', qt_dir)

    if os.name == 'nt':
        prepend_path(env, [os.path.join(ROOT, '.toolchain', 'venv', 'Scripts'),
                           os.path.join(qt_dir, 'bin')])
    else:
        set_key(env, 'CCACHE_DIR', os.path.join(ROOT, '.cache', 'ccache'))
    return env


def main(argv):
    if not argv or argv[0] not in ('run', 'print'):
        sys.stderr.write(USAGE)
        return 2
    mode = argv[0]
    if mode == 'print':
        env = build_env()
        for key in sorted(env):
            sys.stdout.write('{}={}\n'.format(key, env[key]))
        return 0

    rest = argv[1:]
    if not rest or rest[0] != '--' or len(rest) == 1:
        sys.stderr.write(USAGE)
        return 2
    command = rest[1:]
    env = build_env()
    if os.name == 'nt':
        # Windows: 给出 env= 时 CreateProcess 仍用**调用者自身**的 PATH 搜索 argv[0]
        # （实测: 只把目录放进子进程环境块 → WinError 2；先写回本进程 PATH 再传同一块 → 找到）。
        # bash `source tools/env.sh` 是同一进程改 PATH，此处等价同步，run -- cl / cmake 才可用。
        os.environ['PATH'] = env['PATH']
    try:
        return subprocess.run(command, env=env).returncode
    except OSError as exc:
        sys.stderr.write('env: {}: {}\n'.format(command[0], exc.strerror or exc))
        return 127


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
