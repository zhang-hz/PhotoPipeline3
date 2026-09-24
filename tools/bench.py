#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — --dev bench 记录脚本（M4-W5-T19；dev-only）。

用法:
  python tools/bench.py [BUILD_DIR] [--cases a,b,c] [--scale N] [--out DIR]

作用：经 tools/env.py 构造工具链环境后运行 `photopipeline --dev bench`（capture_output，
故 dev harness 的捕获型句柄判据不接管 stdout），把逐场景行 + 判定行 + stderr 原样打印，
并按 <OUT>/bench-report.json 复述机读结果。退出码透传（非 0 = 判定未达成）。

BUILD_DIR 默认 <repo>/build/release-dev；OUT 默认 <repo>/.cache/bench。
本脚本只做"调用 + 原样转述"，不解析、不改写任何数字（禁圆场：数字只在 bench 里产生）。
"""

import json
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USAGE = ("用法: python tools/bench.py [BUILD_DIR] [--cases a,b,c] [--scale N] [--out DIR]\n"
         "      （等价环境变量 PP_BENCH_CASES / PP_BENCH_SCALE / PP_BENCH_OUT）\n")


def main(argv):
    build = os.path.join(REPO, 'build', 'release-dev')
    cases = os.environ.get('PP_BENCH_CASES', '')
    scale = os.environ.get('PP_BENCH_SCALE', '')
    out = os.environ.get('PP_BENCH_OUT', os.path.join(REPO, '.cache', 'bench'))
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '-h' or a == '--help':
            sys.stdout.write(USAGE)
            return 0
        if a == '--cases':
            cases = argv[i + 1]
            i += 2
            continue
        if a == '--scale':
            scale = argv[i + 1]
            i += 2
            continue
        if a == '--out':
            out = argv[i + 1]
            i += 2
            continue
        build = a
        i += 1

    binary = os.path.join(build, 'photopipeline.exe' if os.name == 'nt' else 'photopipeline')
    if not os.path.isfile(binary):
        binary = os.path.join(build, 'photopipeline')
    if not os.path.isfile(binary):
        print('bench: no dev harness binary under {} (configure with -DPP_BUILD_DEV=ON)'.format(build),
              file=sys.stderr)
        return 2

    inner = [binary, '--dev', 'bench', '--out', out]
    if cases:
        inner += ['--bench-cases', cases]
    if scale:
        inner += ['--bench-scale', str(scale)]
    cmd = [sys.executable, os.path.join(REPO, 'tools', 'env.py'), 'run', '--'] + inner
    # stdout 落到**文件**再回读：GUI 子系统产物的 AttachConsole 判据只认"捕获型"句柄
    # （管道/磁盘文件），ConPTY 下的伪控制台会让直连 stdout 的字节落到控制台而丢失
    # （tools/env.py 头注已记录同一现象）。写文件 → GetFileType = FILE_TYPE_DISK → 原样保留。
    os.makedirs(out, exist_ok=True)
    log_path = os.path.join(out, 'bench.log')
    with open(log_path, 'w', encoding='utf-8', errors='replace') as log:
        proc = subprocess.run(cmd, cwd=REPO, stdout=log, stderr=subprocess.STDOUT)
    with open(log_path, encoding='utf-8', errors='replace') as log:
        sys.stdout.write(log.read())
    report = os.path.join(out, 'bench-report.json')
    if os.path.isfile(report):
        print('bench: report 原样转述 ({})'.format(report))
        with open(report, encoding='utf-8') as f:
            print(json.dumps(json.load(f), ensure_ascii=False, indent=1))
    return proc.returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
