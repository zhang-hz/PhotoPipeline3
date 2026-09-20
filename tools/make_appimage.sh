#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline AppImage 打包脚本（M2-T11；docs/m2-tasks.md §2.9 规则冻结）
#
# 用法:
#   tools/make_appimage.sh [OUT_DIR]      # 默认 dist/
# env 覆盖:
#   PP_BUILD_DIR   release 预设构建目录（默认 <repo>/build/m2-release）
#   QT_DIR         Qt 工具链根（默认从产物的 ldd 反查 libQt6Core.so.6 所在目录）
#
# 产物: <OUT_DIR>/PhotoPipeline-<version>-x86_64.AppImage
#       <OUT_DIR>/PhotoPipeline.AppDir/（组装树，可重复运行 = 先删后建）
#
# 设计要点（§2.9）:
#   * 输入 = release 预设产物（无 dev harness）；版本号取自产物自身 `--version`
#     （唯一来源仍是 version.h / 顶层 project()，脚本不硬编码版本）。
#   * usr/lib = ldd 递归闭包中的**非系统** .so（Qt6*.so、libjpeg.so.62 …）。
#     白名单规则（§2.9 要求的探测规则，固化在此）:
#       探测命令 = ldd <elf> | awk '/=>/ {print $3} /^[[:space:]]*\// {print $1}'
#       系统前缀（跳过，交给目标机）: /lib/ /lib64/ /usr/lib/ /usr/lib32/ /usr/lib64/
#       其余（Qt 工具链、vcpkg_installed、$HOME 等）→ 拷入 usr/lib，文件名 = 被引用的 soname。
#   * Qt 插件: usr/lib/qt-plugins/{platforms/libqoffscreen.so, platforms/libqxcb.so,
#     imageformats/, iconengines/, styles/}；工具链缺某个目录/文件 → 跳过并提示（不视为失败）。
#   * OIIO 插件: 本仓库 OIIO 为静态构建（插件内建），脚本按候选路径探测，存在则整拷到
#     usr/lib/oiio-plugins；否则建空目录 + 提示（AppRun 始终导出 OIIO_LIBRARY_PATH）。
#   * 打包器: tools/bin/appimagetool-x86_64.AppImage（入库 + 旁置 .sha512），
#     运行方式 APPIMAGE_EXTRACT_AND_RUN=1（不依赖 FUSE）。
#   * 幂等: AppDir 与同名产物先删后建；重复运行字节级可复现（gzip 打包，无时间戳参与命名）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-dist}"
case "$OUT_DIR" in /*) : ;; *) OUT_DIR="$ROOT/$OUT_DIR" ;; esac
BUILD_DIR="${PP_BUILD_DIR:-$ROOT/build/m2-release}"
BIN="$BUILD_DIR/photopipeline"
TOOL="$ROOT/tools/bin/appimagetool-x86_64.AppImage"
TOOL_SHA="$TOOL.sha512"
DESKTOP_SRC="$ROOT/share/applications/photopipeline.desktop"
ICON_SRC="$ROOT/share/icons/hicolor/256x256/apps/photopipeline.png"

die() { echo "make_appimage: $*" >&2; exit 1; }
note() { echo "make_appimage: $*"; }

[ -x "$BIN" ] || die "release 产物缺失: $BIN（先 cmake --preset release -B ${BUILD_DIR#"$ROOT"/} && cmake --build ${BUILD_DIR#"$ROOT"/} -j）"
[ -f "$TOOL" ] || die "打包器缺失: $TOOL（入库资产，见 §2.9）"
[ -f "$TOOL_SHA" ] || die "打包器 SHA512 旁置文件缺失: $TOOL_SHA"
[ -f "$DESKTOP_SRC" ] || die "desktop 文件缺失: $DESKTOP_SRC（§2.6 冻结文本）"
[ -f "$ICON_SRC" ] || die "图标缺失: $ICON_SRC（先 python3 tools/make_icon.py，§2.5）"

note "校验打包器 SHA512"
( cd "$ROOT" && sha512sum -c "${TOOL_SHA#"$ROOT"/}" >/dev/null ) || die "打包器 SHA512 校验失败"

# ---- 版本（单源：产物 --version） ----
VERSION="$("$BIN" --version | awk 'NR==1 && $1=="PhotoPipeline" {print $2}')"
[ -n "$VERSION" ] || die "无法从 '$BIN --version' 读取版本号"
APPIMAGE="$OUT_DIR/PhotoPipeline-${VERSION}-x86_64.AppImage"
APPDIR="$OUT_DIR/PhotoPipeline.AppDir"

# ---- Qt 工具链位置（默认从产物反查，避免硬编码版本目录） ----
QT_LIB_DIR="$(ldd "$BIN" | awk '/libQt6Core\.so/ {print $3; exit}')"
[ -n "$QT_LIB_DIR" ] || die "从 ldd 反查 Qt 失败（libQt6Core.so.6 未解析）"
QT_LIB_DIR="$(dirname "$QT_LIB_DIR")"
QT_PLUGIN_SRC="${QT_DIR:+$QT_DIR/plugins}"
[ -n "$QT_PLUGIN_SRC" ] || QT_PLUGIN_SRC="$QT_LIB_DIR/../plugins"
[ -d "$QT_PLUGIN_SRC" ] || die "Qt 插件目录不存在: $QT_PLUGIN_SRC"

note "版本=$VERSION  qt-plugins=$QT_PLUGIN_SRC"

# ---- 幂等：先删后建 ----
rm -rf "$APPDIR"
rm -f "$APPIMAGE"
mkdir -p "$OUT_DIR" "$APPDIR/usr/bin" "$APPDIR/usr/lib"

install -m 755 "$BIN" "$APPDIR/usr/bin/photopipeline"

# ---- 非系统 .so 递归闭包（白名单规则见文件头） ----
shopt -s nullglob
declare -A SEEN=()
copy_closure() {  # usage: copy_closure <elf>...
    local -a queue=("$@")
    local elf lib name
    while [ "${#queue[@]}" -gt 0 ]; do
        elf="${queue[0]}"
        queue=("${queue[@]:1}")
        while IFS= read -r lib; do
            [ -n "$lib" ] && [ -e "$lib" ] || continue
            case "$lib" in
                /lib/*|/lib64/*|/usr/lib/*|/usr/lib32/*|/usr/lib64/*) continue ;;  # 系统白名单
            esac
            name="$(basename "$lib")"
            [ -n "${SEEN[$name]:-}" ] && continue
            SEEN[$name]=1
            cp -Lf "$lib" "$APPDIR/usr/lib/$name"
            queue+=("$lib")
        done < <(LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
                 ldd "$elf" 2>/dev/null | awk '/=>/ {print $3} /^[[:space:]]*\// {print $1}')
    done
}

copy_closure "$APPDIR/usr/bin/photopipeline"

# ---- Qt 插件（§2.9 冻结清单） ----
QTP="$APPDIR/usr/lib/qt-plugins"
mkdir -p "$QTP/platforms"
for p in platforms/libqoffscreen.so platforms/libqxcb.so; do
    if [ -f "$QT_PLUGIN_SRC/$p" ]; then
        cp -Lf "$QT_PLUGIN_SRC/$p" "$QTP/$p"
    else
        note "提示: Qt 插件缺失（跳过）: $p"
    fi
done
for d in imageformats iconengines styles; do
    if [ -d "$QT_PLUGIN_SRC/$d" ]; then
        mkdir -p "$QTP/$d"
        cp -aLf "$QT_PLUGIN_SRC/$d/." "$QTP/$d/"
    else
        note "提示: Qt 插件目录不存在（跳过）: $d/"
    fi
done
# 插件自身可能引入 qt-plugins 之外的 Qt 库（如 Svg/DBus）→ 再走一遍闭包
plugin_elfs=("$QTP"/platforms/*.so "$QTP"/imageformats/*.so "$QTP"/iconengines/*.so "$QTP"/styles/*.so)
[ "${#plugin_elfs[@]}" -gt 0 ] && copy_closure "${plugin_elfs[@]}"

# ---- OIIO 插件目录（静态 OIIO → 通常不存在；探测候选，存在则整拷） ----
OIIO_PLUGINS="$APPDIR/usr/lib/oiio-plugins"
mkdir -p "$OIIO_PLUGINS"
OIIO_PLUGIN_SRC=""
for cand in \
    "$ROOT/vcpkg_installed/x64-linux/lib/oiio-plugins" \
    "$ROOT/vcpkg_installed/x64-linux/lib/openimageio" \
    "$ROOT/vcpkg_installed/x64-linux/share/openimageio/plugins"; do
    if [ -d "$cand" ] && [ -n "$(ls -A "$cand" 2>/dev/null)" ]; then
        OIIO_PLUGIN_SRC="$cand"
        break
    fi
done
if [ -n "$OIIO_PLUGIN_SRC" ]; then
    cp -aLf "$OIIO_PLUGIN_SRC/." "$OIIO_PLUGINS/"
    note "OIIO 插件: $OIIO_PLUGIN_SRC → usr/lib/oiio-plugins"
else
    note "提示: 未发现 OIIO 插件目录（本构建 OIIO 静态链接、插件内建）；usr/lib/oiio-plugins 留空"
fi

# ---- usr/share 资产（§2.5/§2.6）+ AppDir 根副本（appimagetool 要求） ----
mkdir -p "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp -f "$DESKTOP_SRC" "$APPDIR/usr/share/applications/photopipeline.desktop"
cp -f "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/256x256/apps/photopipeline.png"
cp -f "$DESKTOP_SRC" "$APPDIR/photopipeline.desktop"
cp -f "$ICON_SRC" "$APPDIR/photopipeline.png"
cp -f "$ICON_SRC" "$APPDIR/.DirIcon"

# ---- AppRun（§2.9 冻结内容） ----
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
# AppRun — PhotoPipeline AppImage 入口（M2-T11；docs/m2-tasks.md §2.9 冻结）
cd "$APPDIR" || exit 1
export LD_LIBRARY_PATH="$APPDIR/usr/lib"
export QT_PLUGIN_PATH="$APPDIR/usr/lib/qt-plugins"
export OIIO_LIBRARY_PATH="$APPDIR/usr/lib/oiio-plugins"
# QT_QPA_PLATFORM_PLATFORM_PATH 不设（§2.9）
exec usr/bin/photopipeline "$@"
APPRUN
chmod 755 "$APPDIR/AppRun"

# ---- 打包前自检（§2.9 烟测 ②③ 的脚本内固化） ----
missing="$(LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$APPDIR/usr/bin/photopipeline" | grep 'not found' || true)"
[ -z "$missing" ] || die "AppDir 二进制存在未解析依赖:
$missing"
[ -f "$APPDIR/AppRun" ] || die "AppRun 缺失"
grep -qx 'Exec=photopipeline' "$APPDIR/usr/share/applications/photopipeline.desktop" \
    || die "desktop Exec 与冻结文本不一致"
grep -qx 'Icon=photopipeline' "$APPDIR/usr/share/applications/photopipeline.desktop" \
    || die "desktop Icon 与冻结文本不一致"

# ---- 打包 ----
note "appimagetool → $APPIMAGE"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream "$APPDIR" "$APPIMAGE"
[ -x "$APPIMAGE" ] || die "打包失败：$APPIMAGE 不存在"

note "结构清单:"
( cd "$APPDIR" && find . -maxdepth 3 -mindepth 1 \( -type d -o -type f -o -type l \) | sort | sed 's/^/  /' )
note "usr/lib 共 $(find "$APPDIR/usr/lib" -maxdepth 1 -name '*.so*' | wc -l) 个 .so"
note "产物: $APPIMAGE ($(stat -c '%s' "$APPIMAGE") bytes)"
