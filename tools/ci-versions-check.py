#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — 版本/依赖单源一致性断言（M7-WP3；R-062/R-091/R-122/R-128/R-036）

用途: CI 各道的「依赖单源一致性断言」步骤统一调用本脚本（本地也可跑）。
      把"版本号/哈希散落多处"的漂移变成**当场红**，而不是发行后才发现。

检查项（全部通过才 exit 0；任一失败逐条点名后 exit 1）:
  1. tools/versions.env 可解析且必需键非空（tools/versions_env.py 的严格解析，R-131）。
  2. `--expect KEY=VALUE`（可多次）：工作流 env 常量与 versions.env 同值
     （QT_VERSION / VCPKG_TAG / CLANG_TIDY_VERSION / CLANG_FORMAT_VERSION …）。
  3. CMakeLists.txt `project(PhotoPipeline VERSION x.y.z)` == vcpkg.json `version`
     （R-122：版本 bump 必须同改；WP13 的 0.3.3 bump 会在这里被钉住）。
  4. vcpkg.json `builtin-baseline` == versions.env `VCPKG_SHA`（R-091：键不是死配置 ——
     它是 manifest 的基线；checkout 侧另有 HEAD==VCPKG_SHA 断言，见各 workflow 的 vcpkg 步骤）。
  5. `.github/workflows/*.yml` 不得含 `Qt/<x.y.z>/` 路径字面量（R-063：路径一律
     `${{ env.QT_VERSION }}` 拼接或经 GITHUB_ENV 导出；字面量出现 = 漂移）。
  6. tools/bin/clang-format-* 的 sha512 == versions.env 的期望值（R-036 信任锚的第二个消费点；
     执行前消费点在 tools/ci-format.sh）。

用法:
  python tools/ci-versions-check.py [--expect KEY=VALUE]...
"""

import argparse
import glob
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from versions_env import VersionsError, load as load_versions  # noqa: E402  (tools/ 内部模块)

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REQUIRED_KEYS = (
    'QT_VERSION', 'VCPKG_TAG', 'VCPKG_SHA',
    'CLANG_TIDY_VERSION', 'CLANG_FORMAT_VERSION',
    'CLANG_FORMAT_SHA512_WIN64', 'CLANG_FORMAT_SHA512_LINUX',
    'AQTINSTALL_VERSION', 'CMAKE_VERSION', 'NINJA_VERSION',
)


def read_text(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace') as fp:
        return fp.read()


def main(argv):
    ap = argparse.ArgumentParser(description='版本/依赖单源一致性断言（M7-WP3）')
    ap.add_argument('--expect', action='append', default=[], metavar='KEY=VALUE',
                    help='断言 versions.env 的 KEY 等于 VALUE（工作流 env 常量对账）')
    args = ap.parse_args(argv)

    problems = []
    try:
        versions = load_versions()
    except VersionsError as exc:
        print('ci-versions-check: FAIL {}'.format(exc))
        return 1
    for key in REQUIRED_KEYS:
        if key not in versions:
            problems.append('versions.env 缺必需键 {}'.format(key))
    for item in args.expect:
        key, sep, value = item.partition('=')
        if not sep:
            problems.append('--expect 参数不是 KEY=VALUE: {!r}'.format(item))
            continue
        if key not in versions:
            problems.append('versions.env 缺键 {}（--expect {}）'.format(key, item))
        elif versions[key] != value:
            problems.append('单源不一致 {}: 工作流 env={} vs versions.env={}'.format(
                key, value, versions[key]))

    # 3) CMakeLists project 版本 == vcpkg.json version
    cm = re.search(r'project\s*\(\s*PhotoPipeline\s+VERSION\s+([0-9][0-9.]*)',
                   read_text('CMakeLists.txt'))
    vm = re.search(r'"version"\s*:\s*"([^"]+)"', read_text('vcpkg.json'))
    if not cm:
        problems.append('CMakeLists.txt 解析不出 project(PhotoPipeline VERSION …)')
    if not vm:
        problems.append('vcpkg.json 解析不出 "version"')
    if cm and vm and cm.group(1) != vm.group(1):
        problems.append('版本不一致: CMakeLists project VERSION={} vs vcpkg.json version={} '
                        '（R-122）'.format(cm.group(1), vm.group(1)))

    # 4) builtin-baseline == VCPKG_SHA
    bm = re.search(r'"builtin-baseline"\s*:\s*"([0-9a-f]+)"', read_text('vcpkg.json'))
    if not bm:
        problems.append('vcpkg.json 解析不出 "builtin-baseline"（R-091）')
    elif bm.group(1) != versions.get('VCPKG_SHA', ''):
        problems.append('builtin-baseline {} != versions.env VCPKG_SHA {}（R-091）'.format(
            bm.group(1), versions.get('VCPKG_SHA', '<缺>')))

    # 5) workflow 的 Qt 路径字面量（R-063）
    lit_re = re.compile(r'Qt/[0-9]+\.[0-9]+\.[0-9]+/')
    for path in sorted(glob.glob(os.path.join(ROOT, '.github', 'workflows', '*.yml'))):
        rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        for lineno, line in enumerate(read_text(rel).splitlines(), start=1):
            if lit_re.search(line):
                problems.append('Qt 版本路径字面量（R-063）: {}:{}: {}'.format(
                    rel, lineno, line.strip()[:100]))

    # 6) clang-format 二进制 sha512 == versions.env 信任锚（R-036）
    for name, key in (('clang-format-win64.exe', 'CLANG_FORMAT_SHA512_WIN64'),
                      ('clang-format-linux-x86_64', 'CLANG_FORMAT_SHA512_LINUX')):
        rel = os.path.join('tools', 'bin', name)
        full = os.path.join(ROOT, rel)
        if not os.path.isfile(full):
            problems.append('钉版二进制缺失: {}'.format(rel))
            continue
        got = hashlib.sha512(open(full, 'rb').read()).hexdigest()
        want = versions.get(key, '')
        if got != want:
            problems.append('sha512 不符（R-036 信任锚）: {} 实际 {} != {} {}'.format(
                rel, got[:16], want[:16], key))

    if problems:
        for p in problems:
            print('ci-versions-check: FAIL {}'.format(p))
        print('ci-versions-check: {} 项不一致'.format(len(problems)))
        return 1
    print('ci-versions-check: OK ({} keys, expect={})'.format(
        len(versions), len(args.expect)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
