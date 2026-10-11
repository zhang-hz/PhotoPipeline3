#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — tools/versions.env 的**唯一**解析实现（M7-WP3 / R-131，deep-review-0.3.2）。

依据: 评审 R-131「versions.env 解析三种错误处理（env.py 静默吞 OSError、bootstrap.py 无 try、
      空值不校验）—— fresh-machine 外报错点远离真因」。修复 = 统一解析函数 + 非空校验。

用法（仓库内脚本一律经本模块，不再各自复制解析循环）:
    from versions_env import load, get
    versions = load()               # 解析 tools/versions.env → dict（失败即抛 VersionsError）
    qt = get('QT_VERSION')          # 取单键；缺键/空值即抛 VersionsError

语义:
  * 文件缺失 / 不可读 / 解析不出任何键 ⇒ VersionsError（**不再**静默返回空 dict）。
  * `KEY=VALUE` 行：KEY 两侧空白剔除；VALUE 去引号；**空 VALUE 一律报错**（点名文件与行号）。
  * `#` 起头与空行忽略；不含 `=` 的行报错（疑似损坏，不允许静默跳过）。
  * 重复 KEY ⇒ 报错（单源文件不允许重复定义）。
  * 错误消息带**文件路径 + 行号 + 键名**，让 fresh-machine 的失败点落在真因上。
"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSIONS_PATH = os.path.join(ROOT, 'tools', 'versions.env')


class VersionsError(Exception):
    """versions.env 缺失/损坏/缺键/空值（调用方一律转非零退出，不允许静默回退）。"""


def load(path=None):
    """解析 KEY=VALUE → dict；任何异常形态都抛 VersionsError（消息带文件/行号/键）。"""
    target = path or VERSIONS_PATH
    values = {}
    try:
        with open(target, encoding='utf-8') as fp:
            raw_lines = fp.read().splitlines()
    except OSError as exc:
        raise VersionsError('versions.env 不可读: {} ({})'.format(target, exc))
    for lineno, raw in enumerate(raw_lines, start=1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        key, sep, value = line.partition('=')
        if not sep:
            raise VersionsError('{}:{}: 不是 KEY=VALUE 行: {!r}'.format(target, lineno, raw))
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if not key:
            raise VersionsError('{}:{}: 键为空: {!r}'.format(target, lineno, raw))
        if not value:
            raise VersionsError('{}:{}: 键 {} 的值为空（单源值不允许为空）'.format(
                target, lineno, key))
        if key in values:
            raise VersionsError('{}:{}: 键 {} 重复定义'.format(target, lineno, key))
        values[key] = value
    if not values:
        raise VersionsError('versions.env 未解析出任何键: {}'.format(target))
    return values


def get(key, path=None):
    """取单键；缺键/空值 ⇒ VersionsError。"""
    values = load(path)
    if key not in values:
        raise VersionsError('versions.env 缺键 {}（{}）'.format(key, path or VERSIONS_PATH))
    return values[key]
