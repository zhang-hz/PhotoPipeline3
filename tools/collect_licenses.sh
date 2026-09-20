#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline 第三方许可文本汇总（M2-T11c；GPL/LGPL 分发合规）
#
# 用法:
#   tools/collect_licenses.sh <APPDIR>      # AppDir 根（绝对或相对当前目录）
# env 覆盖:
#   PP_VCPKG_SHARE  vcpkg 已安装 port 的 share 根
#                   （默认 <repo>/vcpkg_installed/x64-linux/share）
#
# 收集规则（M2-T11c 冻结，与 tools/make_appimage.sh 头注释一致）:
#   1) 遍历 share/<entry>/ 每个条目，取**第一个**存在的许可文本:
#        精确名 copyright 优先；否则 LICENSE* / LICENCE* / COPYING* 中
#        LC_ALL=C 字典序第一个（大小写不敏感匹配，兼容 port 的两种落盘形态）。
#   2) 落盘为 usr/share/licenses/<entry>/copyright：目录名保持 vcpkg port 名
#      （便于按 port 溯源/对账），文件内容逐字节不改（仅在源头改名时才统一目标名）。
#   3) 本项目自身的 LICENSE（GPL-3.0-or-later）→ usr/share/licenses/PhotoPipeline/LICENSE。
#   4) 无许可文本的条目**必须列名**（不静默跳过），并按性质分两类打印:
#        * 真实 port（share/<entry>/ 含 vcpkg_abi_info.txt，vcpkg 对每个已装 port 都会写）
#          → 「许可缺失（真实 port）」，属真缺口，需在报告里给处置建议；
#        * 非 port 条目（CMake 配置/别名 shim：png→libpng、WebP→libwebp、lcms2→lcms、
#          jpeg→libjpeg-turbo、gif→giflib、iconv→libiconv、hwy→highway、
#          tsl-robin-map→robin-map、unofficial-*→对应 port、SVT-AV1→svt-av1；
#          以及 vcpkg 自带的文档目录 doc/ man/）→ 其许可由对应 port 覆盖，仅列名提示。
#   5) 幂等: 先 rm -rf <APPDIR>/usr/share/licenses 再收集，重复运行结果一致。
#   6) 退出码: 0 = 汇总完成（缺失条目已列名，不视为失败）；1 = 入参/输入目录硬错误。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
die() { echo "collect_licenses: $*" >&2; exit 1; }

APPDIR="${1:-}"
[ -n "$APPDIR" ] || die "用法: tools/collect_licenses.sh <APPDIR>（AppDir 根）"
case "$APPDIR" in /*) : ;; *) APPDIR="$PWD/$APPDIR" ;; esac
SHARE="${PP_VCPKG_SHARE:-$ROOT/vcpkg_installed/x64-linux/share}"
ROOT_LICENSE="$ROOT/LICENSE"
DEST="$APPDIR/usr/share/licenses"

[ -d "$APPDIR" ] || die "AppDir 不存在: $APPDIR"
[ -d "$SHARE" ] || die "vcpkg share 目录不存在: $SHARE（先 vcpkg install / 设 PP_VCPKG_SHARE）"
[ -f "$ROOT_LICENSE" ] || die "项目 LICENSE 缺失: $ROOT_LICENSE"

# 取一个条目目录下的许可文本路径（无 → 空输出）
pick_license() {  # usage: pick_license <dir>
    local dir="$1"
    [ -f "$dir/copyright" ] && { printf '%s\n' "$dir/copyright"; return 0; }
    LC_ALL=C find "$dir" -maxdepth 1 -type f \
        \( -iname 'LICENSE*' -o -iname 'LICENCE*' -o -iname 'COPYING*' \) \
        -printf '%p\n' 2>/dev/null | LC_ALL=C sort | sed -n '1p'
    return 0
}

# ---- 幂等：先清空目标目录再收集 ----
rm -rf "$DEST"
mkdir -p "$DEST"

collected=0
renamed=()
missing_ports=()
missing_nonports=()

for d in "$SHARE"/*/; do
    [ -d "$d" ] || continue
    entry="$(basename "$d")"
    src="$(pick_license "$d")"
    if [ -n "$src" ]; then
        mkdir -p "$DEST/$entry"
        cp -f "$src" "$DEST/$entry/copyright"
        collected=$((collected + 1))
        [ "$(basename "$src")" = "copyright" ] || renamed+=("$entry <- $(basename "$src")")
    elif [ -f "$d/vcpkg_abi_info.txt" ]; then
        missing_ports+=("$entry")
    else
        missing_nonports+=("$entry")
    fi
done

# ---- 本项目许可（GPL-3.0-or-later） ----
mkdir -p "$DEST/PhotoPipeline"
cp -f "$ROOT_LICENSE" "$DEST/PhotoPipeline/LICENSE"

# 列表按 LC_ALL=C 排序：输出与 locale 无关，保证重复运行可逐字比对
sort_list() { [ "$#" -eq 0 ] && return 0; printf '%s\n' "$@" | LC_ALL=C sort | tr '\n' ' '; }

echo "collect_licenses: 第三方条目 $collected 个 + PhotoPipeline/LICENSE -> $DEST"
[ "${#renamed[@]}" -eq 0 ] || echo "collect_licenses: 源文件名非 copyright（目标名统一为 copyright）: $(sort_list "${renamed[@]}")"
if [ "${#missing_ports[@]}" -ne 0 ]; then
    echo "collect_licenses: 警告: 许可缺失（真实 port，share/<port>/ 无 copyright|LICENSE*|COPYING*|LICENCE*）: $(sort_list "${missing_ports[@]}")" >&2
fi
if [ "${#missing_nonports[@]}" -ne 0 ]; then
    echo "collect_licenses: 提示: 非 port 条目无许可文件（CMake 配置/别名 shim 或 vcpkg 文档目录，许可由对应 port 覆盖）: $(sort_list "${missing_nonports[@]}")"
fi
