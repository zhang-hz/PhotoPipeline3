#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline 工具链环境（M3-W3：Python 单实现，替代 tools/env.sh / env.sh.example）。

用法:
  python tools/env.py [--strict] run -- <command...>   构造环境后运行命令，透传其退出码
  python tools/env.py [--strict] print                 按 KEY 排序逐行打印 KEY=VALUE（调试/CI 用）

  --strict（M7-WP3 / R-098，deep-review-0.3.2）: Windows 上 vcvars 捕获失败即失败（退出码 2），
  不再只提示一行继续跑。默认（不带开关）行为逐字不变 —— 裸跑报错点远离真因的老问题由
  调用方按需开启；CI/打包路径一律建议带 --strict。

背景（M3 裁定 D3：共享脚本 Python 单实现，删除 bash 版）:
  * tools/env.sh.example 已删除，且 tools/env.sh **不再生成**（bootstrap.py 里原有的
    `cp -n tools/env.sh.example tools/env.sh` 步骤已随 W3 移除）；环境故事全部由本脚本接管。
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
              PATH，仓库内工具链自足）；vswhere 缺失或失败默认只提示一行，不致命
              （`--strict` 时即失败，见用法段 R-098）。

退出码:
  run     子命令退出码（命令不存在 → 127）
  print   0
  用法错   2
  --strict 下 vcvars 捕获失败 / versions.env 损坏   2

M6-T17（REL-0.3.2 门禁取证）: `run` 现在把本进程的 stdin/stdout/stderr **显式**交给子进程
（见 forward_std_streams()）。Windows 的句柄继承对 **GUI 子系统**子进程不生效 ——
photopipeline.exe 是 WIN32_EXECUTABLE（CMakeLists.txt:251），继承式启动时其 GetStdHandle
全为 NULL（同仓探针实测：console 子系统子进程拿到父进程的管道/文件句柄，GUI 子系统子进程
拿到 NULL），于是 `env.py run -- build/release/photopipeline.exe --version` 这类**捕获**
路径拿到 0 字节（应用走 main.cpp 的「挂接父控制台」分支，输出落在控制台，捕获侧看不见）。
显式转发后 Popen 把这些流放进 STARTUPINFO/句柄表，GUI 子进程同样拿到 ⇒ 管道/重定向捕获
恢复；控制台直跑所见不变（句柄指向同一目标）。
"""

import locale
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from versions_env import VersionsError, load as load_versions  # noqa: E402  (tools/ 内部模块)

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

USAGE = ("用法: python tools/env.py [--strict] run -- <command...>\n"
         "      python tools/env.py [--strict] print\n")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_versions():
    """解析 tools/versions.env → dict（R-131：统一走 tools/versions_env.py，失败即抛）。

    旧实现静默吞 OSError 并返回空 dict（评审取证 env.py:69-71）—— fresh-machine 上缺文件时
    QT_VERSION='' 被拼进路径、报错点远离真因。现改为 VersionsError 由 main() 转退出码 2。
    """
    return load_versions()


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


def capture_vcvars(env, strict=False):
    """Windows：vswhere → vcvars64.bat → `set`，把 MSVC 环境覆盖进 env。

    vswhere 不存在/失败、vcvars64.bat 缺失/失败 → 默认 stderr 一行提示后跳过（不致命，
    既有行为逐字不变）；strict=True（R-098 的 --strict）时返回 False，由调用方转退出码 2。
    成功捕获返回 True。
    """
    hint = ("env: 提示: 未捕获 MSVC 工具链环境（vswhere 或 vcvars64.bat 不可用），"
            "cl/link 可能不在 PATH")

    def fail():
        if strict:
            print('env: --strict: 未捕获 MSVC 工具链环境（vswhere/vcvars64.bat 不可用或失败）',
                  file=sys.stderr)
            return False
        print(hint, file=sys.stderr)
        return True

    program_files_x86 = get_key(env, 'ProgramFiles(x86)')
    vswhere = (os.path.join(program_files_x86, 'Microsoft Visual Studio', 'Installer',
                            'vswhere.exe') if program_files_x86 else None)
    if not vswhere or not os.path.isfile(vswhere):
        return fail()
    try:
        probe = subprocess.run(
            [vswhere, '-latest', '-products', '*',
             '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
             '-property', 'installationPath'],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
    except OSError:
        return fail()
    vs_root = decode_console(probe.stdout).strip().splitlines()
    vs_root = vs_root[0].strip() if vs_root else ''
    if probe.returncode != 0 or not vs_root:
        return fail()
    vcvars = os.path.join(vs_root, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
    if not os.path.isfile(vcvars):
        return fail()
    # 规格给定形式 `cmd /s /c ""<vcvars64.bat>" && set"`：列表参数 + 禁 shell=True 时，
    # subprocess 会把内层引号反斜杠转义（cmd 不认 `\"`），实测 rc=1 且 env 为空；
    # 故用等价的列表形式（cmd 收到 `call "<vcvars64.bat>" && set`）。
    captured = subprocess.run(['cmd', '/c', 'call', vcvars, '&&', 'set'],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)
    if captured.returncode != 0:
        return fail()
    for line in decode_console(captured.stdout).splitlines():
        key, sep, value = line.partition('=')
        if not sep or not key or key.startswith('['):
            continue
        set_key(env, key, value)
    return True


def build_env(strict=False):
    """构造完整子进程环境（os.environ 副本 + 工具链增量）→ (env, toolchain_ok)。

    顺序：Windows 先捕获 vcvars，再写仓库值 —— vcvars64.bat 自己会导出
    VCPKG_ROOT=%VCINSTALLDIR%vcpkg（VS 自带 vcpkg），后写者必须是本仓库的值。
    PATH 前置放在最后，保证 <repo>/.toolchain 的工具优先于 VS/系统。
    toolchain_ok = MSVC 环境捕获结果（POSIX 恒 True；R-098 的 --strict 由 main 据此判失败）。
    """
    env = dict(os.environ)
    toolchain_ok = True
    if os.name == 'nt':
        toolchain_ok = capture_vcvars(env, strict=strict)

    version = read_versions()['QT_VERSION']
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
    return env, toolchain_ok


def forward_std_streams():
    """本进程的标准流 → subprocess 关键字参数（可用者才转发）。

    Windows 为什么必需：句柄继承只管「句柄表」，**GUI 子系统**子进程的标准句柄槽位不被
    继承赋值（GetStdHandle 全 NULL）；显式传 stdin/stdout/stderr 才让 Popen 通过
    STARTUPINFO/句柄表把它们交给子进程 —— 否则 photopipeline.exe 的 `--version`（以及任何
    写 stdout 的 GUI 路径）在 `env.py run` 的捕获下静默 0 字节（见文件头 M6-T17 段）。
    POSIX 下与继承等价（同一 fd），故一并转发、只留一条代码路径；无 fileno 的流
    （pythonw / 已被替换成 StringIO 等）跳过，回退为继承。
    """
    kwargs = {}
    for name, stream in (('stdin', sys.stdin), ('stdout', sys.stdout), ('stderr', sys.stderr)):
        try:
            stream.fileno()
        except (AttributeError, ValueError, OSError):  # None / 关闭 / StringIO(UnsupportedOperation)
            continue
        kwargs[name] = stream
    return kwargs


def main(argv):
    strict = False
    while argv and argv[0] == '--strict':
        strict = True
        argv = argv[1:]
    if not argv or argv[0] not in ('run', 'print'):
        sys.stderr.write(USAGE)
        return 2
    mode = argv[0]
    try:
        env, toolchain_ok = build_env(strict=strict)
    except VersionsError as exc:
        sys.stderr.write('env: {}\n'.format(exc))
        return 2
    if not toolchain_ok:
        return 2
    if mode == 'print':
        for key in sorted(env):
            sys.stdout.write('{}={}\n'.format(key, env[key]))
        return 0

    rest = argv[1:]
    if not rest or rest[0] != '--' or len(rest) == 1:
        sys.stderr.write(USAGE)
        return 2
    command = rest[1:]
    if os.name == 'nt':
        # Windows: 给出 env= 时 CreateProcess 仍用**调用者自身**的 PATH 搜索 argv[0]
        # （实测: 只把目录放进子进程环境块 → WinError 2；先写回本进程 PATH 再传同一块 → 找到）。
        # 历史 bash 版的 `source tools/env.sh` 是同一进程改 PATH（该文件现已不再生成）；
        # 此处等价同步，run -- cl / cmake 才可用。
        os.environ['PATH'] = env['PATH']
    try:
        return subprocess.run(command, env=env, **forward_std_streams()).returncode
    except OSError as exc:
        sys.stderr.write('env: {}: {}\n'.format(command[0], exc.strerror or exc))
        return 127


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
