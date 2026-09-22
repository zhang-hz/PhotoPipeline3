#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline 第三方许可文本汇总（M2-T11c；GPL/LGPL 分发合规）

用法:
  python tools/collect_licenses.py <APPDIR>      # AppDir 根（绝对或相对当前目录）
env 覆盖:
  PP_VCPKG_SHARE  vcpkg 已安装 port 的 share 根
                  （默认 <repo>/vcpkg_installed/<triplet>/share；
                   triplet = x64-windows（Windows）/ x64-linux（POSIX））

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
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRIPLET = 'x64-windows' if os.name == 'nt' else 'x64-linux'


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


def main(argv):
    appdir = argv[0] if argv else ''
    if not appdir:
        die('用法: tools/collect_licenses.sh <APPDIR>（AppDir 根）')
    if not os.path.isabs(appdir):
        appdir = os.getcwd() + '/' + appdir
    share = os.environ.get('PP_VCPKG_SHARE') or os.path.join(
        ROOT, 'vcpkg_installed', TRIPLET, 'share')
    root_license = os.path.join(ROOT, 'LICENSE')
    dest = appdir + '/usr/share/licenses'

    if not os.path.isdir(appdir):
        die('AppDir 不存在: {}'.format(appdir))
    if not os.path.isdir(share):
        die('vcpkg share 目录不存在: {}（先 vcpkg install / 设 PP_VCPKG_SHARE）'.format(share))
    if not os.path.isfile(root_license):
        die('项目 LICENSE 缺失: {}'.format(root_license))

    # ---- 幂等：先清空目标目录再收集 ----
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest, exist_ok=True)

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
            os.makedirs(os.path.join(dest, entry), exist_ok=True)
            shutil.copyfile(src, os.path.join(dest, entry, 'copyright'))
            collected += 1
            if os.path.basename(src) != 'copyright':
                renamed.append('{} <- {}'.format(entry, os.path.basename(src)))
        elif os.path.isfile(os.path.join(directory, 'vcpkg_abi_info.txt')):
            missing_ports.append(entry)
        else:
            missing_nonports.append(entry)

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
