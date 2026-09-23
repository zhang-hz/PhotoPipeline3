#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline Windows 打包后端（M3-T10；与 tools/make_appimage.sh 四项烟测同口径）

用法:
  python tools/make_winzip.py [OUT_DIR]        # 默认 dist（相对路径拼仓库根）
env 覆盖:
  PP_BUILD_DIR    release 预设构建目录（默认 <repo>/build/release）
选项:
  --smoke-exe <path>    可选：dev 构建的 photopipeline.exe（含 --ui-smoke），
                        用于烟测 E-2（offscreen 脚本化走查）
  --windeployqt <path>  可选：否则 shutil.which('windeployqt') → $QT_DIR/bin/windeployqt(.exe)

产物: <OUT_DIR>/PhotoPipeline-<version>-win64.zip（顶层单一目录 PhotoPipeline/，解压即用）

四项烟测（对齐 AppImage ①闭包 ②来源/结构 ③许可 ④启动，Windows 机制化）:
  * 门禁 A（闭包）: 解析 staging 内**每一个** PE（exe + 全部 dll + Qt 插件 dll）的导入表，
    逐名按**顺序**判据（M3-T11 v2.2 / R2；不再维护硬编码系统白名单——永远列不全）:
      a) staging 内有同名文件 → OK[staging]
      b) MUST_BUNDLE 前缀（msvcp140/vcruntime140/concrt140/vccorlib140）→ FAIL[must-bundle]
         **必须在系统判据之前**：否则本机装过 VC redist 会掩盖 app-local 缺失
      c) api-ms-win-*/ext-ms-win-*（API set 契约）→ OK[apiset]
      d) %SystemRoot%\\System32\\<name> 存在 → OK[system]（d3d9/d3d12/uiautomationcore 由此放行）
      e) 否则 → FAIL[missing]
    失败逐名列出并带标记 + 三类计数（对应 AppImage 的 FAIL-A "not found"）。
  * 门禁 B（来源一致性）: staging 内每个 Qt6*.dll 的 sha256 必须等于 Qt 工具链 bin 目录内
    同名文件（对应 AppImage 的 FAIL-B "libQt6* 外泄/系统 Qt 混入"）。
  * 断言 C（结构）: photopipeline.exe、platforms/qwindows.dll、platforms/qoffscreen.dll、
    imageformats/（≥1 dll）、tls/（≥1 dll）齐备。
  * 断言 D（许可）: licenses/*/copyright 计数 ≥30 且 licenses/PhotoPipeline/LICENSE 存在；
    **写盘前 + zip 内各校验一次**（阈值 30 与 AppImage 同口径）。
  * 烟测 E（启动）: staging 内 exe 跑 --version → 冻结行 `PhotoPipeline <ver>` + rc 0；
    给出 --smoke-exe 时追加 E-2（offscreen --ui-smoke，断言 `UI-SMOKE OK shots=8 pages=3`）。
  * 指纹 F: 打印产物字节数 + sha256 + zip 条目数。

退出码: 0 成功；1 任一硬门禁失败；2 输入资产缺失（构建产物 / windeployqt）。

设计要点（对齐 tools/make_appimage.sh 的 §2.9 口径）:
  * 版本号唯一来源 = 产物自身 `--version`；M3-T11b 起**对 staging 内的 exe** 探测（Qt/vcpkg/CRT
    已就位，零调用者-PATH 依赖），且本脚本派生的每个子进程都显式注入 `qt_bin` 到 PATH 最前。
  * zip 条目按路径排序写入（确定性顺序）；只写 OUT_DIR 与临时目录。
  * windeployqt 参数: --release --no-translations --no-system-d3d-compiler --no-opengl-sw
    （本应用为 QWidget/QPainter，不需要 OpenGL 软件回退）。
    **不用** --compiler-runtime: Qt 6.8.3 该选项只投放安装器 vc_redist.x64.exe，与"解压即用"
    不符；改为显式 app-local 部署 Microsoft.VC*.CRT 整目录（R1），并剔除 vc_redist.x64.exe
    与 dxcompiler.dll/dxil.dll（D3D12 RHI 着色器编译器，本应用不走该路径）。
  * 显式补拷 platforms/qoffscreen.dll（对齐 AppImage 同时随包 qoffscreen + qxcb；
    offscreen 供本脚本烟测 E-2 使用）；工具链缺该文件只提示、不失败。
  * 不联网；不触碰 tools/make_appimage.sh 与 CI 工作流。
"""

import glob
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FROZEN_UI_SMOKE = 'UI-SMOKE OK shots=8 pages=3'
VERSION_RE = re.compile(r'^PhotoPipeline \d+\.\d+\.\d+$')

# 门禁 A 判据（M3-T11 v2.2 / R2）：不再维护硬编码系统 DLL 白名单（永远列不全），
# 改为逐名按**顺序**三判据 + MUST_BUNDLE 前缀（见 classify_import）。
# MUST_BUNDLE 必须排在"系统存在"判据**之前** —— 否则本机装过 VC redist 时 msvcp140.dll
# 会被判成系统 DLL，从而掩盖 app-local 缺失（"干净机器跑不起来"的经典坑）。
MUST_BUNDLE_PREFIXES = ('msvcp140', 'vcruntime140', 'concrt140', 'vccorlib140')
SYSTEM_PREFIXES = ('api-ms-win-', 'ext-ms-win-')
SYSTEM32 = os.path.join(
    os.environ.get('SystemRoot') or os.environ.get('WINDIR') or r'C:\Windows', 'System32')


def note(message):
    print('make_winzip: {}'.format(message))


def die(message, code=1):
    sys.stderr.write('make_winzip: {}\n'.format(message))
    raise SystemExit(code)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def classify_import(name, staged_names):
    """门禁 A 的逐名判据（R2，顺序敏感）。

    a) staging 内有同名文件                        → 'staging'
    b) MUST_BUNDLE 前缀（且 a 未命中）              → 'must_bundle'   ← 必须在系统判据之前
    c) api-ms-win-* / ext-ms-win-*（API set 契约）  → 'apiset'
    d) %SystemRoot%\\System32\\<name> 存在          → 'system'
    e) 否则                                        → 'missing'
    """
    lowered = name.lower()
    if lowered in staged_names:
        return 'staging'
    if lowered.startswith(MUST_BUNDLE_PREFIXES):
        return 'must_bundle'
    if lowered.startswith(SYSTEM_PREFIXES):
        return 'apiset'
    if os.path.isfile(os.path.join(SYSTEM32, name)):
        return 'system'
    return 'missing'


def pe_imports(path):
    """返回该 PE 导入表里的 DLL 名列表；非 PE/截断 → ValueError（调用方报文件名）。"""
    with open(path, 'rb') as handle:
        data = handle.read()
    if len(data) < 0x40 or data[0:2] != b'MZ':
        raise ValueError('不是 PE（缺 MZ 头）')
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    if e_lfanew + 24 > len(data) or data[e_lfanew:e_lfanew + 4] != b'PE\0\0':
        raise ValueError('不是 PE（缺 PE 签名）')
    coff = e_lfanew + 4
    num_sections = struct.unpack_from('<H', data, coff + 2)[0]
    size_optional = struct.unpack_from('<H', data, coff + 16)[0]
    optional = coff + 20
    if optional + 2 > len(data):
        raise ValueError('可选头截断')
    magic = struct.unpack_from('<H', data, optional)[0]
    if magic == 0x10B:
        dd_offset = optional + 96
    elif magic == 0x20B:
        dd_offset = optional + 112
    else:
        raise ValueError('未知可选头 magic 0x{:x}'.format(magic))
    import_rva, _import_size = struct.unpack_from('<II', data, dd_offset + 8)

    sections = []
    section_table = optional + size_optional
    for index in range(num_sections):
        base = section_table + index * 40
        if base + 40 > len(data):
            raise ValueError('节表截断')
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
            '<IIII', data, base + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer))

    def rva_to_offset(rva):
        for virtual_address, span, raw_pointer in sections:
            if virtual_address <= rva < virtual_address + span:
                return raw_pointer + (rva - virtual_address)
        return None

    names = []
    if import_rva == 0:
        return names
    offset = rva_to_offset(import_rva)
    if offset is None:
        raise ValueError('导入表 RVA 未映射到任何节')
    while True:
        if offset + 20 > len(data):
            raise ValueError('导入描述符截断')
        original_thunk, timestamp, forwarder, name_rva, first_thunk = struct.unpack_from(
            '<IIIII', data, offset)
        if not (original_thunk or timestamp or forwarder or name_rva or first_thunk):
            break
        name_offset = rva_to_offset(name_rva)
        if name_offset is None:
            raise ValueError('导入名 RVA 未映射')
        end = data.find(b'\0', name_offset)
        if end < 0:
            raise ValueError('导入名未终止')
        names.append(data[name_offset:end].decode('ascii', 'replace'))
        offset += 20
    return names


def collect_tree(staging):
    """staging 内全部文件 → [(相对路径(posix), 绝对路径)]，按路径排序。"""
    items = []
    for root, _dirs, files in os.walk(staging):
        for name in files:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, staging).replace(os.sep, '/')
            items.append((rel, full))
    items.sort()
    return items


def structure_lines(staging, maxdepth=3):
    lines = []

    def recurse(current, depth):
        try:
            names = sorted(os.listdir(current))
        except OSError:
            return
        for name in names:
            full = os.path.join(current, name)
            rel = os.path.relpath(full, staging).replace(os.sep, '/')
            if depth + 1 <= maxdepth:
                lines.append('./' + rel + ('/' if os.path.isdir(full) else ''))
            if os.path.isdir(full):
                recurse(full, depth + 1)

    recurse(staging, 0)
    return sorted(lines)


def child_env(qt_bin):
    """M3-T11b：本脚本派生的**每个**子进程都显式获得 Qt bin 在 PATH 最前。

    这样"启动 photopipeline.exe 需要 Qt6*.dll"就不再依赖调用者（CI 无 env.py 注入时
    曾导致版本探测读到空输出 → run #21 的 winzip job 失败），依赖变成显式且自包含。
    """
    env = os.environ.copy()
    existing = env.get('PATH', '')
    env['PATH'] = qt_bin + os.pathsep + existing if existing else qt_bin
    return env


def resolve_crt_dir():
    """定位微软文档化的 app-local 部署单元 Microsoft.VC*.CRT（R1）。

    优先级: PP_VCREDIST_DIR 显式覆盖 → $VCToolsRedistDir/x64 → $VCINSTALLDIR/Redist/MSVC/<最大版本>/x64；
    在 base 下取 Microsoft.VC*.CRT 中 VC 号最大者（本机 VC145、runner 可能是 VC143，不得硬编码）。
    """
    tried = []
    explicit = os.environ.get('PP_VCREDIST_DIR', '')
    bases = []
    if explicit:
        bases.append(os.path.join(explicit, 'x64'))
        bases.append(explicit)
    else:
        redist = os.environ.get('VCToolsRedistDir', '')
        if redist:
            bases.append(os.path.join(redist, 'x64'))
        vcinstall = os.environ.get('VCINSTALLDIR', '')
        if vcinstall:
            msvc_root = os.path.join(vcinstall, 'Redist', 'MSVC')
            if os.path.isdir(msvc_root):
                versions = sorted(os.listdir(msvc_root))
                if versions:
                    bases.append(os.path.join(msvc_root, versions[-1], 'x64'))
    for base in bases:
        tried.append(base)
        if not os.path.isdir(base):
            continue
        candidates = sorted(glob.glob(os.path.join(base, 'Microsoft.VC*.CRT')))
        if candidates:
            return candidates[-1]
    die('CRT 部署失败: 未找到 Microsoft.VC*.CRT（已尝试: {}）；'
        '可用 PP_VCREDIST_DIR 显式指定 VC Redist 根（如 …\\VC\\Redist\\MSVC\\14.51.36231）'.format(
            ', '.join(tried) if tried else '<无候选：PP_VCREDIST_DIR/VCToolsRedistDir/VCINSTALLDIR 均未设置>'),
        2)


def check_licenses(staging, where):
    licenses = os.path.join(staging, 'licenses')
    count = 0
    if os.path.isdir(licenses):
        for entry in sorted(os.listdir(licenses)):
            if os.path.isfile(os.path.join(licenses, entry, 'copyright')):
                count += 1
    project_license = os.path.join(licenses, 'PhotoPipeline', 'LICENSE')
    if count < 30:
        die('许可断言失败（{}）: licenses/*/copyright = {} < 30'.format(where, count))
    if not os.path.isfile(project_license):
        die('许可断言失败（{}）: 缺 licenses/PhotoPipeline/LICENSE'.format(where))
    return count


def parse_args(argv):
    out_dir = 'dist'
    smoke_exe = ''
    windeployqt = ''
    rest = list(argv)
    while rest:
        item = rest.pop(0)
        if item == '--smoke-exe':
            if not rest:
                die('用法: tools/make_winzip.py [OUT_DIR] [--smoke-exe <path>] [--windeployqt <path>]'
                    '（--smoke-exe 缺参数）')
            smoke_exe = rest.pop(0)
        elif item == '--windeployqt':
            if not rest:
                die('用法: tools/make_winzip.py [OUT_DIR] [--smoke-exe <path>] [--windeployqt <path>]'
                    '（--windeployqt 缺参数）')
            windeployqt = rest.pop(0)
        elif item.startswith('--'):
            die('未知选项: {}（用法: tools/make_winzip.py [OUT_DIR] [--smoke-exe <path>] '
                '[--windeployqt <path>]）'.format(item))
        else:
            out_dir = item
    return out_dir, smoke_exe, windeployqt


def resolve_exe(build_dir):
    for name in ('photopipeline.exe', 'photopipeline'):
        candidate = os.path.join(build_dir, name)
        if os.path.isfile(candidate):
            return candidate
    return ''


def find_windeployqt(explicit):
    if explicit:
        if not os.path.isfile(explicit):
            die('windeployqt 不存在: {}（--windeployqt 指向的文件缺失）'.format(explicit), 2)
        return explicit
    found = shutil.which('windeployqt')
    if found:
        return found
    qt_dir = os.environ.get('QT_DIR', '')
    if qt_dir:
        for name in ('windeployqt.exe', 'windeployqt'):
            candidate = os.path.join(qt_dir, 'bin', name)
            if os.path.isfile(candidate):
                return candidate
    return ''


def main(argv):
    out_dir, smoke_exe, windeployqt_arg = parse_args(argv)
    if not os.path.isabs(out_dir):
        out_dir = os.path.join(ROOT, out_dir)
    build_dir = os.environ.get('PP_BUILD_DIR') or os.path.join(ROOT, 'build', 'release')

    # ---- 1) 输入资产 ----
    binary = resolve_exe(build_dir)
    if not binary:
        die('release 产物缺失: {}/photopipeline.exe（先 cmake --preset release && '
            'cmake --build --preset release）'.format(build_dir), 2)
    windeployqt = find_windeployqt(windeployqt_arg)
    if not windeployqt:
        die('windeployqt 未找到（--windeployqt 未给、PATH 无、$QT_DIR/bin 无）；'
            '请用 python tools/env.py run -- python tools/make_winzip.py 以获得 Qt bin 在 PATH', 2)
    # M3-T11b：Qt bin 由本脚本**显式**注入每个子进程的 PATH（不再依赖调用者的 PATH）。
    qt_bin = os.path.dirname(os.path.abspath(windeployqt))
    note('build={} windeployqt={}'.format(build_dir, windeployqt))

    # ---- 2) 幂等：先删后建 staging ----
    staging = os.path.join(out_dir, 'PhotoPipeline')
    shutil.rmtree(staging, ignore_errors=True)
    os.makedirs(staging, exist_ok=True)

    # ---- 4) 拷贝 exe + 全部 vcpkg DLL（不拷任何 pp_*.exe） ----
    shutil.copy2(binary, os.path.join(staging, 'photopipeline.exe'))
    dll_count = 0
    for name in sorted(os.listdir(build_dir)):
        if name.lower().endswith('.dll'):
            shutil.copy2(os.path.join(build_dir, name), os.path.join(staging, name))
            dll_count += 1
    note('staging: photopipeline.exe + {} 个 DLL（applocal 部署产物）'.format(dll_count))

    # ---- 5) windeployqt（Qt 插件树 + app-local VC 运行时） ----
    command = [windeployqt, '--release', '--no-translations', '--no-system-d3d-compiler',
               '--no-opengl-sw', '--dir', staging,
               os.path.join(staging, 'photopipeline.exe')]
    deploy = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env=child_env(qt_bin))
    deploy_out = deploy.stdout.decode('utf-8', 'replace')
    if deploy.returncode != 0:
        sys.stdout.write(deploy_out)
        die('windeployqt 失败（exit {}）: {}'.format(deploy.returncode, ' '.join(command)))
    note('windeployqt: exit 0（{} 行输出已折叠）'.format(len(deploy_out.splitlines())))

    # ---- 5b) CRT app-local 部署（R1）: Microsoft.VC*.CRT 是微软文档化的部署单元 ----
    # 整目录拷贝是刻意的：逐个挑选会漏掉任一 vcpkg DLL 的 CRT 依赖。
    crt_dir = resolve_crt_dir()
    crt_files = sorted(glob.glob(os.path.join(crt_dir, '*.dll')))
    if not crt_files:
        die('CRT 部署失败: {} 下没有 *.dll（可用 PP_VCREDIST_DIR 显式指定）'.format(crt_dir), 2)
    for path in crt_files:
        shutil.copy2(path, os.path.join(staging, os.path.basename(path)))
    note('CRT app-local 部署: {} 个 ← {}'.format(len(crt_files), crt_dir))

    # ---- 5c) 剔除 windeployqt 的冗余产物（R1.3；存在才删，各一行 note 含字节数） ----
    #  * vc_redist.x64.exe: CRT 已 app-local，安装器冗余（≈18.8 MB）
    #  * dxcompiler.dll / dxil.dll: D3D12 RHI 的着色器编译器，本应用 QWidget/QPainter 不使用
    #    （≈15.8 MB）；--no-system-d3d-compiler 不覆盖这两个 Qt 自带文件，故显式剔除。
    for name in ('vc_redist.x64.exe', 'dxcompiler.dll', 'dxil.dll'):
        redundant = os.path.join(staging, name)
        if os.path.isfile(redundant):
            size = os.path.getsize(redundant)
            os.remove(redundant)
            note('剔除冗余产物: {}（{} bytes）'.format(name, size))

    # ---- 6b) 版本（单源：**staging 内**产物 --version；M3-T11b 零 PATH 依赖） ----
    # 探测对象是 staging 里的 exe：Qt / vcpkg / CRT 全部已在同目录就位，故不依赖调用者 PATH
    # （旧实现探测构建树 exe，那里没有 Qt6*.dll → CI 下输出为空而失败，run #21 真因）。
    staging_exe = os.path.join(staging, 'photopipeline.exe')
    probe = subprocess.run([staging_exe, '--version'], cwd=staging, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace',
                           env=child_env(qt_bin))
    first_line = probe.stdout.splitlines()[0] if probe.stdout else ''
    fields = first_line.split()
    version = fields[1] if len(fields) >= 2 and fields[0] == 'PhotoPipeline' else ''
    if not version:
        die("无法从 '{} --version' 读取版本号（rc={}，stdout={!r}，stderr={!r}）".format(
            staging_exe, probe.returncode, probe.stdout, probe.stderr))
    note('版本={}'.format(version))

    # ---- 6) 显式补拷 platforms/qoffscreen.dll（烟测 E-2 用；缺失只提示） ----
    qt_root = os.path.dirname(qt_bin)
    qoffscreen_src = os.path.join(qt_root, 'plugins', 'platforms', 'qoffscreen.dll')
    qoffscreen_dst = os.path.join(staging, 'platforms', 'qoffscreen.dll')
    if os.path.isfile(qoffscreen_dst):
        note('platforms/qoffscreen.dll 已由 windeployqt 部署')
    elif os.path.isfile(qoffscreen_src):
        os.makedirs(os.path.dirname(qoffscreen_dst), exist_ok=True)
        shutil.copy2(qoffscreen_src, qoffscreen_dst)
        note('补拷 platforms/qoffscreen.dll ← {}'.format(qoffscreen_src))
    else:
        note('提示: Qt 工具链缺 platforms/qoffscreen.dll（跳过）: {}'.format(qoffscreen_src))

    # ---- 7) 门禁 A：PE 导入表闭包 ----
    tree = collect_tree(staging)
    staged_names = {rel.split('/')[-1].lower() for rel, _full in tree}
    pe_files = [(rel, full) for rel, full in tree if rel.lower().endswith(('.exe', '.dll'))]
    imported = {}
    for rel, full in pe_files:
        try:
            for name in pe_imports(full):
                imported.setdefault(name.lower(), set()).add(rel)
        except ValueError as exc:
            die('闭包门禁 A: PE 解析失败 {}: {}'.format(rel, exc))
    unresolved = {}
    counts = {'staging': 0, 'apiset': 0, 'system': 0}
    for name, users in imported.items():
        kind = classify_import(name, staged_names)
        if kind in counts:
            counts[kind] += 1
        else:
            unresolved[name] = (kind, users)
    if unresolved:
        lines = ['闭包门禁 A 失败: {} 个被导入的 DLL 未通过（staging {} · apiset {} · system {}）:'
                 .format(len(unresolved), counts['staging'], counts['apiset'], counts['system'])]
        for name in sorted(unresolved):
            kind, users = unresolved[name]
            label = 'must-bundle' if kind == 'must_bundle' else 'missing'
            lines.append('  {} [{}] ← {}'.format(name, label, ', '.join(sorted(users))))
        die('\n'.join(lines))
    note('闭包门禁 A: {} 个 PE / {} 个导入名 → staging {} · apiset {} · system {}'.format(
        len(pe_files), len(imported), counts['staging'], counts['apiset'], counts['system']))
    bundled_crt = sum(1 for rel, _full in tree
                      if os.path.basename(rel).lower().startswith(MUST_BUNDLE_PREFIXES))
    note('VC 运行时就位（app-local）: msvcp140/vcruntime140 系列 {} 个'.format(bundled_crt))

    # ---- 8) 门禁 B：staging 内 Qt6*.dll 必须与 Qt 工具链逐一 sha256 一致 ----
    qt_dlls = [(rel, full) for rel, full in tree
               if rel.lower().endswith('.dll') and os.path.basename(rel).lower().startswith('qt6')]
    if not qt_dlls:
        die('来源门禁 B 失败: staging 内没有任何 Qt6*.dll（windeployqt 未生效？）')
    mismatched = []
    for rel, full in qt_dlls:
        name = os.path.basename(rel)
        toolchain = os.path.join(qt_bin, name)
        if not os.path.isfile(toolchain):
            mismatched.append('{}（Qt 工具链缺同名文件: {}）'.format(rel, toolchain))
        elif sha256_file(full) != sha256_file(toolchain):
            mismatched.append('{}（与 {} 内容不一致）'.format(rel, toolchain))
    if mismatched:
        die('来源门禁 B 失败: {} 个 Qt6*.dll 来源可疑（疑系统 Qt 混入）:\n  {}'.format(
            len(mismatched), '\n  '.join(mismatched)))
    note('来源门禁 B: {} 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致（{}）'.format(
        len(qt_dlls), qt_bin))

    # ---- 9) 断言 C：结构 ----
    required = [
        'photopipeline.exe',
        'platforms/qwindows.dll',
        'platforms/qoffscreen.dll',
    ]
    missing = [rel for rel in required if not os.path.isfile(os.path.join(staging, rel))]
    imageformats = [rel for rel, _full in tree
                    if rel.startswith('imageformats/') and rel.lower().endswith('.dll')]
    tls = [rel for rel, _full in tree if rel.startswith('tls/') and rel.lower().endswith('.dll')]
    if not imageformats:
        missing.append('imageformats/*.dll (≥1)')
    if not tls:
        missing.append('tls/*.dll (≥1)')
    if missing:
        die('结构断言 C 失败，缺: {}'.format(', '.join(missing)))
    note('结构断言 C: photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / '
         'imageformats/={} dll / tls/={} dll 齐备'.format(len(imageformats), len(tls)))

    # ---- 10) 断言 D（写盘前）：许可汇总 ----
    licenses_dir = os.path.join(staging, 'licenses')
    license_rc = subprocess.call([sys.executable, os.path.join(ROOT, 'tools', 'collect_licenses.py'),
                                  staging, '--dest', licenses_dir], env=child_env(qt_bin))
    if license_rc != 0:
        die('许可汇总失败（collect_licenses.py exit {}）'.format(license_rc))
    license_count = check_licenses(staging, '写盘前')
    note('许可断言 D（写盘前）: licenses/ = {} 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓'
         .format(license_count))

    # ---- 11) 烟测 E-1：staging 内 exe --version ----
    smoke = subprocess.run([os.path.join(staging, 'photopipeline.exe'), '--version'], cwd=staging,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           env=child_env(qt_bin))
    smoke_out = smoke.stdout.decode('utf-8', 'replace')
    smoke_line = smoke_out.splitlines()[0] if smoke_out.splitlines() else ''
    if smoke.returncode != 0:
        die('烟测 E-1 失败: exit {}（输出: {!r}）'.format(smoke.returncode, smoke_out))
    if not VERSION_RE.match(smoke_line):
        die('烟测 E-1 失败: 冻结行不匹配 ^PhotoPipeline \\d+\\.\\d+\\.\\d+$（实际: {!r}）'.format(
            smoke_line))
    note('烟测 E-1: {} (exit 0)'.format(smoke_line))

    # ---- 12) 烟测 E-2（仅 --smoke-exe）：offscreen --ui-smoke ----
    if smoke_exe:
        if not os.path.isfile(smoke_exe):
            die('--smoke-exe 不存在: {}'.format(smoke_exe), 2)
        dev_smoke = os.path.join(staging, 'photopipeline-devsmoke.exe')
        shots_dir = tempfile.mkdtemp(prefix='pp-winzip-shots-')
        try:
            shutil.copy2(smoke_exe, dev_smoke)
            env = child_env(qt_bin)
            env['QT_QPA_PLATFORM'] = 'offscreen'
            walk = subprocess.run(
                [dev_smoke, '--ui-smoke', '--inputs', os.path.join(ROOT, 'tests', 'golden', 'base'),
                 '--shots', shots_dir],
                cwd=staging, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            walk_out = walk.stdout.decode('utf-8', 'replace')
            if walk.returncode != 0:
                sys.stdout.write(walk_out)
                die('烟测 E-2 失败: --ui-smoke exit {}'.format(walk.returncode))
            if FROZEN_UI_SMOKE not in walk_out:
                sys.stdout.write(walk_out)
                die('烟测 E-2 失败: 输出缺冻结行 {!r}'.format(FROZEN_UI_SMOKE))
            note('烟测 E-2: {} (exit 0, QT_QPA_PLATFORM=offscreen)'.format(FROZEN_UI_SMOKE))
        finally:
            if os.path.exists(dev_smoke):
                os.remove(dev_smoke)
            shutil.rmtree(shots_dir, ignore_errors=True)
        leftover = [rel for rel, _full in collect_tree(staging) if 'devsmoke' in rel.lower()]
        if leftover:
            die('打包前断言失败: staging 内残留 {}'.format(leftover))

    # ---- 13) 打包（确定性顺序，arcname 一律 PhotoPipeline/ 前缀） ----
    zip_path = os.path.join(out_dir, 'PhotoPipeline-{}-win64.zip'.format(version))
    os.makedirs(out_dir, exist_ok=True)
    if os.path.exists(zip_path):
        os.remove(zip_path)
    entries = collect_tree(staging)
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for rel, full in entries:
            archive.write(full, 'PhotoPipeline/' + rel)

    # ---- 14) 产物内复核（对齐 AppImage S19） ----
    with zipfile.ZipFile(zip_path, 'r') as archive:
        names = archive.namelist()
    if 'PhotoPipeline/photopipeline.exe' not in names:
        die('产物内复核失败: 缺 PhotoPipeline/photopipeline.exe')
    if 'PhotoPipeline/licenses/PhotoPipeline/LICENSE' not in names:
        die('产物内复核失败: 缺 PhotoPipeline/licenses/PhotoPipeline/LICENSE')
    zip_licenses = [name for name in names
                    if name.startswith('PhotoPipeline/licenses/')
                    and name.endswith('/copyright') and name.count('/') == 3]
    if len(zip_licenses) < 30:
        die('产物内复核失败（许可断言 D）: zip 内 licenses/*/copyright = {} < 30'.format(
            len(zip_licenses)))
    extract_dir = tempfile.mkdtemp(prefix='pp-winzip-verify-')
    try:
        with zipfile.ZipFile(zip_path, 'r') as archive:
            archive.extractall(extract_dir)
        packaged_exe = os.path.join(extract_dir, 'PhotoPipeline', 'photopipeline.exe')
        packaged = subprocess.run([packaged_exe, '--version'], cwd=os.path.dirname(packaged_exe),
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  env=child_env(qt_bin))
        packaged_line = packaged.stdout.decode('utf-8', 'replace').splitlines()
        packaged_line = packaged_line[0] if packaged_line else ''
        if packaged.returncode != 0 or not VERSION_RE.match(packaged_line):
            die('产物内复核失败（烟测 E）: 解压后 --version exit {} 行 {!r}'.format(
                packaged.returncode, packaged_line))
        note('产物内复核: 解压即用 --version = {} (exit 0)；licenses/*/copyright = {}（≥30 ✓）'
             .format(packaged_line, len(zip_licenses)))
    finally:
        shutil.rmtree(extract_dir, ignore_errors=True)

    # ---- 15) 指纹 F + 结构清单 ----
    note('产物: {} ({} bytes, sha256={}, 条目 {})'.format(
        zip_path, os.path.getsize(zip_path), sha256_file(zip_path), len(names)))
    note('结构清单:')
    for line in structure_lines(staging):
        print('  {}'.format(line))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
