#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — Qt 工具链归档播种（M4-W4-T16；warm-cache.yml 的兜底通道）

用途: 当 aqtinstall 无法定位目标 Qt 版本时（实测: Qt 6.11.2 的 Windows 仓库布局从
      `windows_x86/desktop/qt6_6112/qt6_6112/Updates.xml` 变成
      `windows_x86/desktop/qt6_6112/qt6_6112_msvc2022_64/Updates.xml`，而 aqt 3.3.0 仍按前者取件
      ⇒ 直接 ERROR「Failed to locate XML data for Qt version」），改用**自备 Qt 归档**播种：
      dev 机上 .toolchain/Qt/<ver>/<arch> 是可直接复用的完整树，打成 zip/tar.gz 放到任意可下载位置
      （Release 资产 / 内部镜像 / artifact 转存），由本脚本下载 + 解包到 `Qt/<ver>/`。

用法:
  python tools/ci-seed-qt.py --sha256 <HEX64> <URL> <DEST_DIR>   # 下载、校验并解包到 DEST_DIR
  python tools/ci-seed-qt.py --sha256 <HEX64> <FILE> <DEST_DIR>  # 本地归档（离线/自测）

  --sha256（M7-WP3 / R-037，deep-review-0.3.2）**必填**：归档完整性的信任锚（64 位十六进制）。
  下载/解包前校验，不符即退出 1 —— Qt 归档直接进工具链并流入发布产物，禁止零校验消费
  （旧口径只在 winzip 侧自比对）。warm-cache.yml 由 workflow_dispatch 的 qt_tarball_sha256
  传入钉值（与 qt_tarball_url 成对）。

默认解包口径:
  * 归档顶层若只有一个目录（如 `msvc2022_64/`）⇒ 去掉这一层，内容落到 DEST_DIR 下
    （即 DEST_DIR/bin/Qt6Core.dll / DEST_DIR/lib/libQt6Core.so.6）；
  * 顶层若直接是 bin/ lib/ ... ⇒ 原样落到 DEST_DIR；
  * 其它形态（多个顶层目录）⇒ 原样解包并在退出信息里提示，由调用方按需断言。
安全: 拒绝绝对路径 / `..` 穿越 / 盘符 / UNC 成员（M7-WP3 / R-038：Windows 下 `os.path.join`
  遇到绝对 rel 会**丢弃 dest**，`C:x`/`\\\\server\\share` 这类驱动器相对/UNC 同样逃逸 ——
  现以 ntpath.isabs + 盘符/UNC 前缀过滤，并在写出前做 dest 前缀断言双保险）；
  只解包普通文件/目录；不改动归档外任何路径。
退出码: 0 成功；1 归档/网络/完整性错误；2 用法错误。
"""

import hashlib
import ntpath
import os
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

USAGE = ("用法: python tools/ci-seed-qt.py --sha256 <HEX64> <URL|FILE> <DEST_DIR>\n"
         "      （--sha256 必填 = 归档 sha256 完整性信任锚，R-037）\n")

_DRIVE_RE = re.compile(r'^[A-Za-z]:')


def die(code, message):
    sys.stderr.write('ci-seed-qt: {}\n'.format(message))
    raise SystemExit(code)


def safe_members(names):
    """过滤掉绝对路径 / `..` 穿越 / 盘符 / UNC 成员（归档内容不可信，R-038）。"""
    keep = []
    for name in names:
        norm = name.replace('\\', '/')
        bad = (
            norm.startswith('/') or norm.startswith('//') or      # POSIX 绝对 / UNC
            norm.startswith('../') or '/../' in norm or norm in ('..', '.') or
            _DRIVE_RE.match(norm) is not None or                  # C:/… 与 C:foo（驱动器相对）
            ntpath.isabs(name)                                    # Windows 绝对（含 \\server\share）
        )
        if bad:
            print('ci-seed-qt: 跳过不安全成员: {}'.format(name))
            continue
        keep.append(name)
    return keep


def target_inside(dest, target):
    """写出前断言 target 在 dest 之内（normpath+前缀；R-038 第二道防线）。"""
    d = os.path.normcase(os.path.normpath(dest))
    t = os.path.normcase(os.path.normpath(target))
    return t == d or t.startswith(d + os.sep)


def extract(archive_path, dest):
    os.makedirs(dest, exist_ok=True)
    with open(archive_path, 'rb') as fp:
        magic = fp.read(4)
    if magic[:2] == b'PK':
        with zipfile.ZipFile(archive_path) as zf:
            infos = safe_members(zf.namelist())
            tops = {i.split('/')[0] for i in infos if '/' in i}
            strip = infos and len(tops) == 1 and all(
                i == list(tops)[0] or i.startswith(list(tops)[0] + '/') for i in infos)
            for info in zf.infolist():
                if info.filename not in infos:
                    continue
                rel = info.filename
                if strip:
                    rel = rel.split('/', 1)[1] if '/' in rel else ''
                if not rel:
                    continue
                target = os.path.join(dest, rel)
                if not target_inside(dest, target):
                    print('ci-seed-qt: 跳过越界成员: {}'.format(info.filename))
                    continue
                if info.is_dir():
                    os.makedirs(target, exist_ok=True)
                    continue
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with zf.open(info) as src, open(target, 'wb') as out:
                    shutil.copyfileobj(src, out)
        return 'zip'
    if magic[:2] == b'\x1f\x8b' or tarfile.is_tarfile(archive_path):
        with tarfile.open(archive_path) as tf:
            members = safe_members([m.name for m in tf.getmembers() if m.isfile() or m.isdir()])
            tops = {m.split('/')[0] for m in members if '/' in m}
            strip = len(tops) == 1 and all(m == list(tops)[0] or m.startswith(list(tops)[0] + '/')
                                           for m in members)
            for member in tf.getmembers():
                if member.name not in members:
                    continue
                rel = member.name
                if strip:
                    rel = rel.split('/', 1)[1] if '/' in rel else ''
                if not rel:
                    continue
                target = os.path.join(dest, rel)
                if not target_inside(dest, target):
                    print('ci-seed-qt: 跳过越界成员: {}'.format(member.name))
                    continue
                if member.isdir():
                    os.makedirs(target, exist_ok=True)
                    continue
                os.makedirs(os.path.dirname(target), exist_ok=True)
                src = tf.extractfile(member)
                if src is None:
                    continue
                with src, open(target, 'wb') as out:
                    shutil.copyfileobj(src, out)
        return 'tar'
    die(1, '无法识别的归档格式（既不是 zip 也不是 tar.gz）: {}'.format(archive_path))


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fp:
        for chunk in iter(lambda: fp.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main(argv):
    args = list(argv)
    want_sha = ''
    while args and args[0] == '--sha256':
        if len(args) < 2:
            sys.stderr.write(USAGE)
            return 2
        want_sha = args[1].strip().lower()
        args = args[2:]
    if len(args) != 2:
        sys.stderr.write(USAGE)
        return 2
    # R-037：--sha256 必填（禁止零完整性校验消费归档）。
    if not re.fullmatch(r'[0-9a-f]{64}', want_sha):
        sys.stderr.write('ci-seed-qt: --sha256 缺失或不是 64 位十六进制（R-037 必填信任锚）\n')
        sys.stderr.write(USAGE)
        return 2
    src, dest = args
    if os.path.isfile(src):
        archive = src
        print('ci-seed-qt: 本地归档 {}'.format(os.path.abspath(src)))
    else:
        tmpdir = tempfile.mkdtemp(prefix='qt-seed-')
        archive = os.path.join(tmpdir, 'qt-archive')
        print('ci-seed-qt: 下载 {}'.format(src))
        try:
            urllib.request.urlretrieve(src, archive)
        except Exception as exc:                      # noqa: BLE001 - 网络层异常一律转 1
            die(1, '下载失败: {}'.format(exc))
    if not os.path.isfile(archive):
        die(1, '归档不存在: {}'.format(archive))
    size = os.path.getsize(archive)
    got_sha = sha256_of(archive)
    if got_sha != want_sha:
        die(1, '归档 sha256 不符（R-037）: 期望 {} 实际 {} —— 拒绝解包'.format(want_sha, got_sha))
    print('ci-seed-qt: sha256 OK {}'.format(got_sha))
    kind = extract(archive, dest)
    print('ci-seed-qt: {} 归档 {} B -> {}'.format(kind, size, dest))
    for probe in ('bin', 'lib', 'plugins', 'include'):
        path = os.path.join(dest, probe)
        print('ci-seed-qt:   {}{}'.format(probe, '' if os.path.isdir(path) else '（缺）'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
