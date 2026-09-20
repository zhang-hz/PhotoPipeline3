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
#   * **插件依赖闭包（M2-T18 缺陷修复）**: 旧实现只对主二进制做闭包，插件的依赖从未纳入，
#     导致 GUI 双击无响应。两类后果（诊断工具 tools/appimage-check-closure.sh）:
#       A) not found —— 目标机缺库（libxcb-cursor0 等）。
#       B) **交叉版本 ABI 冲突** —— 插件的 libQt6* 依赖解析到目标机**系统 Qt**
#          （本机系统 Qt 6.10 的 libQt6XcbQpa.so.6 / libQt6Svg.so.6）而随包 Qt 是 6.8.3，
#          运行期报 `libQt6Core.so.6: version 'Qt_6.10' not found` → xcb 插件加载失败
#          → `Could not load the Qt platform plugin "xcb"` → 进程 exit 134（双击无响应、
#          从终端启动才看得到）。**只跑主二进制的旧烟测测不出 B 类**。
#     修复 = ①闭包输入扩为「主二进制 + 全部插件源 ELF」，②解析路径加 Qt 工具链 lib 目录
#     （保证 libQt6* 命中随包 Qt 而非系统 Qt），③把插件的 X11 支持库纳入随包。
#     打包期用 tools/appimage-check-closure.sh 对 AppDir 内**每一个** ELF 做硬门禁
#     （A/B 任一命中即失败），并把「目标机系统要求清单」写入
#     <OUT_DIR>/PhotoPipeline-<version>-deps.txt（供 README 系统要求章节取证）。
#   * 随包 / 不随包策略（M2-T18 定，逐类理由）:
#       BUNDLE_ALWAYS（白名单豁免，必须随包）: libQt6*（必须来自 Qt 工具链，绝不允许
#         系统 Qt 顶替）、libxcb-*.so*（Qt xcb 插件专属扩展库，体积小、ABI 稳定、目标机
#         常缺）、libxkbcommon-x11.so*、libX11-xcb.so*。
#       NEVER_BUNDLE（绝不随包，硬断言）: glibc/loader 家族（libc/libm/libdl/libpthread/
#         libgcc_s/libstdc++ 等，必须与宿主一致）、GL/EGL/GLX 驱动栈（libGL/libEGL/libGLX/
#         libOpenGL/libGLdispatch/libdrm/libgbm，须用厂商驱动）、**单副本不变式**库
#         （libX11.so.6 / libxcb.so.1 / libxkbcommon.so.0 / libXau / libXdmcp / libICE /
#         libSM / libglib-2.0 / libdbus-1）：一旦同时存在系统与随包两份，系统 libX11 与
#         随包 libxcb-* 会各自拉住不同副本 → 同进程两份 X 连接状态，禁止。
#         随之包内的 libxcb-*.so* 链接的是**系统** libxcb.so.1（单一副本），符合预期。
#       GL 栈不随包 → 目标机需 libgl1/libegl1（Debian/Ubuntu）或 mesa-libGL/mesa-libEGL
#         （Fedora）；AppRun 启动前检测并在 stderr/弹窗/日志明确报错（不静默）。
#   * Qt 插件: usr/lib/qt-plugins/{platforms/libqoffscreen.so, platforms/libqxcb.so,
#     imageformats/, iconengines/, styles/, tls/}；工具链缺某个目录/文件 → 跳过并提示（不视为失败）。
#     tls/（M2-T11b 裁定加入）= Qt 6.8 的 libqopensslbackend.so / libqcertonlybackend.so：运行期
#     **dlopen 系统 libssl.so.3 / libcrypto.so.3**（插件本身不链接 OpenSSL），故 OpenSSL 属系统
#     白名单、**不**随包（目标机需 libssl3）；缺它时 QNetworkAccessManager 报
#     `qt.network.ssl: No functional TLS backend was found`，在线地图瓦片/经纬度反查失效。
#   * OIIO 插件: 本仓库 OIIO 为静态构建（插件内建），脚本按候选路径探测，存在则整拷到
#     usr/lib/oiio-plugins；否则建空目录 + 提示（AppRun 始终导出 OIIO_LIBRARY_PATH）。
#   * 第三方许可（M2-T11c，GPL/LGPL 分发合规）: 由 tools/collect_licenses.sh 汇总
#     vcpkg 已装 port 的许可文本 → usr/share/licenses/<port>/copyright（目录名 = port 名，
#     便于溯源），本项目许可 → usr/share/licenses/PhotoPipeline/LICENSE；取文件规则、
#     缺失条目列名规则见该脚本头注释。缺许可的真实 port 只告警不失败（须在报告里列名处置）。
#   * 打包器: tools/bin/appimagetool-x86_64.AppImage（入库 + 旁置 .sha512），
#     运行方式 APPIMAGE_EXTRACT_AND_RUN=1（不依赖 FUSE）。
#   * type2 runtime（M2-T11d）: appimagetool 默认从 GitHub `continuous` 渠道**在线下载**
#     runtime（§1.7 禁网络 + continuous 漂移 → 同日不同产物），故 runtime 也入库:
#     tools/bin/type2-runtime-x86_64（+ 旁置 .sha512），打包固定用 `--runtime-file` 指向它。
#     校验失败/文件缺失 → exit 2 硬失败，**不静默回退到网络下载**。
#   * 幂等: AppDir 与同名产物先删后建，可重复重跑（结构一致）；runtime 固定后产物仅剩
#     squashfs 超级块时间戳/mtime 的非确定性（不承诺字节可复现）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-dist}"
case "$OUT_DIR" in /*) : ;; *) OUT_DIR="$ROOT/$OUT_DIR" ;; esac
BUILD_DIR="${PP_BUILD_DIR:-$ROOT/build/m2-release}"
BIN="$BUILD_DIR/photopipeline"
TOOL="$ROOT/tools/bin/appimagetool-x86_64.AppImage"
TOOL_SHA="$TOOL.sha512"
RUNTIME="$ROOT/tools/bin/type2-runtime-x86_64"
RUNTIME_SHA="$RUNTIME.sha512"
DESKTOP_SRC="$ROOT/share/applications/photopipeline.desktop"
ICON_SRC="$ROOT/share/icons/hicolor/256x256/apps/photopipeline.png"

die() { echo "make_appimage: $*" >&2; exit 1; }
# 入库 runtime 缺失/损坏 = 硬失败（exit 2）：绝不让 appimagetool 回退到在线下载 runtime
die_runtime() {
    echo "make_appimage: $*" >&2
    echo "make_appimage: exit 2 — 拒绝回退到网络下载 runtime（§1.7 打包不得依赖网络）；请恢复入库资产 tools/bin/type2-runtime-x86_64{,.sha512}" >&2
    exit 2
}
note() { echo "make_appimage: $*"; }

[ -x "$BIN" ] || die "release 产物缺失: $BIN（先 cmake --preset release -B ${BUILD_DIR#"$ROOT"/} && cmake --build ${BUILD_DIR#"$ROOT"/} -j）"
[ -f "$TOOL" ] || die "打包器缺失: $TOOL（入库资产，见 §2.9）"
[ -f "$TOOL_SHA" ] || die "打包器 SHA512 旁置文件缺失: $TOOL_SHA"
[ -f "$DESKTOP_SRC" ] || die "desktop 文件缺失: $DESKTOP_SRC（§2.6 冻结文本）"
[ -f "$ICON_SRC" ] || die "图标缺失: $ICON_SRC（先 python3 tools/make_icon.py，§2.5）"

note "校验打包器 SHA512"
( cd "$ROOT" && sha512sum -c "${TOOL_SHA#"$ROOT"/}" >/dev/null ) || die "打包器 SHA512 校验失败"

# ---- type2 runtime 前置校验（M2-T11d；失败即 exit 2，不联网） ----
[ -f "$RUNTIME" ] || die_runtime "type2 runtime 入库文件缺失: $RUNTIME"
[ -f "$RUNTIME_SHA" ] || die_runtime "type2 runtime SHA512 旁置文件缺失: $RUNTIME_SHA"
note "校验 type2 runtime SHA512（--runtime-file，离线打包）"
( cd "$ROOT" && sha512sum -c "${RUNTIME_SHA#"$ROOT"/}" >/dev/null ) || die_runtime "type2 runtime SHA512 校验失败: $RUNTIME"

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

# ---- 非系统 .so 递归闭包（白名单规则 + BUNDLE_ALWAYS/NEVER_BUNDLE 见文件头） ----
shopt -s nullglob
declare -A SEEN=()
QT_LIB_DIR_REAL="$(cd "$QT_LIB_DIR" && pwd -P)"
APPDIR_LIB_REAL="$(cd "$APPDIR/usr/lib" && pwd -P)"
# 解析路径: 随包 usr/lib 优先，其次 Qt 工具链 lib。
# 后者是 M2-T18 的关键: 插件源码树里 $ORIGIN/../../lib 在 AppDir 布局下（qt-plugins/platforms/
# ../../lib = usr/lib/lib，不存在）失效，若不显式给出工具链 lib 目录，ldd 会把 libQt6XcbQpa.so.6
# 解析到**系统 Qt**（B 类缺陷）并被系统前缀白名单跳过。
RESOLVE_PATH="$APPDIR/usr/lib:$QT_LIB_DIR"

# BUNDLE_ALWAYS: 命中即视为「必须随包」，不受系统前缀白名单约束
is_bundle_always() {
    case "$1" in
        libQt6*.so*|libxcb-*.so*|libxkbcommon-x11.so*|libX11-xcb.so*) return 0 ;;
    esac
    return 1
}
# NEVER_BUNDLE: 绝不随包（ABI/驱动/单副本不变式）；命中即硬失败（防回归）
is_never_bundle() {
    case "$1" in
        libc.so*|libm.so*|libmvec.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*|ld-linux*|libgcc_s.so*|libstdc++.so*|\
        libGL.so*|libEGL.so*|libGLX.so*|libOpenGL.so*|libGLdispatch.so*|libdrm.so*|libgbm.so*|libvulkan.so*|\
        libX11.so*|libxcb.so*|libxkbcommon.so*|libXau.so*|libXdmcp.so*|libXext.so*|libICE.so*|libSM.so*|\
        libglib-2.0.so*|libgthread-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*|libdbus-1.so*|libsystemd.so*|libudev.so*)
            return 0 ;;
    esac
    return 1
}
copy_closure() {  # usage: copy_closure <elf>...
    local -a queue=("$@")
    local elf lib name lib_dir
    while [ "${#queue[@]}" -gt 0 ]; do
        elf="${queue[0]}"
        queue=("${queue[@]:1}")
        while IFS= read -r lib; do
            [ -n "$lib" ] && [ -e "$lib" ] || continue
            name="$(basename "$lib")"
            if is_never_bundle "$name"; then
                continue   # 交给目标机（系统要求，写入 -deps.txt）
            fi
            case "$lib" in
                /lib/*|/lib64/*|/usr/lib/*|/usr/lib32/*|/usr/lib64/*)
                    is_bundle_always "$name" || continue ;;  # 系统白名单（BUNDLE_ALWAYS 例外）
            esac
            # libQt6* 只允许来自 Qt 工具链（或已由工具链拷入的随包副本 usr/lib）:
            # 命中目标机系统 Qt = B 类交叉版本冲突，硬失败
            if [[ "$name" == libQt6* ]]; then
                lib_dir="$(cd "$(dirname "$lib")" && pwd -P)"
                [ "$lib_dir" = "$QT_LIB_DIR_REAL" ] || [ "$lib_dir" = "$APPDIR_LIB_REAL" ] \
                    || die "libQt6 依赖未解析到 Qt 工具链: $name -> $lib
  期望目录: $QT_LIB_DIR_REAL（或随包副本 $APPDIR_LIB_REAL）
  这会造成随包 Qt 与系统 Qt 交叉版本冲突（M2-T18 缺陷类），拒绝打包。"
            fi
            [ -n "${SEEN[$name]:-}" ] && continue
            SEEN[$name]=1
            cp -Lf "$lib" "$APPDIR/usr/lib/$name"
            queue+=("$lib")
        done < <(LD_LIBRARY_PATH="$RESOLVE_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
                 ldd "$elf" 2>/dev/null | awk '/=>/ {print $3} /^[[:space:]]*\// {print $1}')
    done
}

copy_closure "$APPDIR/usr/bin/photopipeline"

# ---- Qt 插件（§2.9 冻结清单）；M2-T18: 用**工具链源路径**作闭包输入 ----
QTP="$APPDIR/usr/lib/qt-plugins"
mkdir -p "$QTP/platforms"
PLUGIN_SRCS=()
for p in platforms/libqoffscreen.so platforms/libqxcb.so; do
    if [ -f "$QT_PLUGIN_SRC/$p" ]; then
        cp -Lf "$QT_PLUGIN_SRC/$p" "$QTP/$p"
        PLUGIN_SRCS+=("$QT_PLUGIN_SRC/$p")
    else
        note "提示: Qt 插件缺失（跳过）: $p"
    fi
done
for d in imageformats iconengines styles tls; do
    if [ -d "$QT_PLUGIN_SRC/$d" ]; then
        mkdir -p "$QTP/$d"
        cp -aLf "$QT_PLUGIN_SRC/$d/." "$QTP/$d/"
        for f in "$QT_PLUGIN_SRC/$d"/*.so; do
            [ -f "$f" ] && PLUGIN_SRCS+=("$f")
        done
    else
        note "提示: Qt 插件目录不存在（跳过）: $d/"
    fi
done
# 插件自身依赖（libQt6XcbQpa/libQt6Svg/libxcb-*/libxkbcommon-x11 …）+ 它们带出的 Qt 库
# 一并纳入闭包（libqsvg/libqsvgicon 也会拉进 libQt6Svg，M2-T18 一并修复）
[ "${#PLUGIN_SRCS[@]}" -gt 0 ] && copy_closure "${PLUGIN_SRCS[@]}"
# 再对「拷入 AppDir 后」的插件复跑一遍（幂等；捕获 $ORIGIN 布局差异）
plugin_elfs=("$QTP"/platforms/*.so "$QTP"/imageformats/*.so "$QTP"/iconengines/*.so "$QTP"/styles/*.so)
[ "${#plugin_elfs[@]}" -gt 0 ] && copy_closure "${plugin_elfs[@]}"
[ "${#PLUGIN_SRCS[@]}" -gt 0 ] && note "插件依赖闭包: 输入 ${#PLUGIN_SRCS[@]} 个插件 ELF，usr/lib 现有 $(find "$APPDIR/usr/lib" -maxdepth 1 -name '*.so*' | wc -l) 个 .so"

# ---- 随包 Qt 溯源断言（M2-T18）: usr/lib/libQt6*.so.* 必须与 Qt 工具链逐一字节一致 ----
# 防止「系统 Qt 被误拷进随包」这一类回归（B 类缺陷的根因），比路径判断更硬。
for f in "$APPDIR/usr/lib"/libQt6*.so.*; do
    [ -e "$f" ] || continue
    n="$(basename "$f")"
    [ -f "$QT_LIB_DIR/$n" ] || die "随包 Qt 库在工具链中不存在（来源可疑）: $n"
    cmp -s "$f" "$QT_LIB_DIR/$n" || die "随包 Qt 库与 Qt 工具链不一致（疑似系统 Qt 混入）: $n ← $QT_LIB_DIR/$n"
done

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

# ---- 第三方许可文本汇总（M2-T11c；规则见 tools/collect_licenses.sh 头注释） ----
note "许可汇总 → usr/share/licenses/（规则: tools/collect_licenses.sh）"
bash "$ROOT/tools/collect_licenses.sh" "$APPDIR"
LIC_DIR="$APPDIR/usr/share/licenses"
LIC_COUNT="$(find "$LIC_DIR" -mindepth 2 -maxdepth 2 -type f -name copyright | wc -l)"

# ---- AppRun（§2.9 冻结内容 + M2-T18 增补） ----
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
# AppRun — PhotoPipeline AppImage 入口
#   §2.9 冻结项（不改）: cd "$APPDIR"；导出 LD_LIBRARY_PATH / QT_PLUGIN_PATH /
#   OIIO_LIBRARY_PATH；QT_QPA_PLATFORM_PLATFORM_PATH 不设；终端场景 exec 主程序。
#   M2-T18 增补（双击无响应缺陷）: ①启动前依赖自检 → 缺库时 stderr 明确报错并给出
#   发行版包名；②无终端（文件管理器双击/桌面启动器 stderr 被丢弃）时把 stderr 写入
#   日志，且启动即失败（≤15s 非零退出）时弹窗提示。
#   取舍: 弹窗（zenity→kdialog→xmessage 依次尝试）是双击场景**用户唯一可见**的通道，
#   日志（$HOME/.cache/PhotoPipeline-appimage.log）保证任何情况下都留下可回传的证据；
#   两者互为兜底、代价仅几行 sh，故同时保留；弹窗不可用时不阻塞启动。
cd "$APPDIR" || exit 1
export LD_LIBRARY_PATH="$APPDIR/usr/lib"
export QT_PLUGIN_PATH="$APPDIR/usr/lib/qt-plugins"
export OIIO_LIBRARY_PATH="$APPDIR/usr/lib/oiio-plugins"
# QT_QPA_PLATFORM_PLATFORM_PATH 不设（§2.9）

LOG="${XDG_CACHE_HOME:-$HOME/.cache}/PhotoPipeline-appimage.log"

# 缺库 → 发行版包名提示。随包已含 libQt6*/libxcb-*/libxkbcommon-x11/libX11-xcb，此处只列
# **刻意不随包**的系统项: GL/EGL 驱动栈、glibc 家族、单副本不变式库（libX11/libxcb 核心）、
# 字体栈、dbus/glib（理由见 tools/make_appimage.sh 文件头）。
pkg_hint() {
    case "$1" in
        libGL.so.1|libGLX.so.0|libOpenGL.so.0|libGLdispatch.so.0) echo "Debian/Ubuntu: libgl1 ｜ Fedora: mesa-libGL/glx-utils" ;;
        libEGL.so.1)                    echo "Debian/Ubuntu: libegl1 ｜ Fedora: mesa-libEGL" ;;
        libX11.so.6)                    echo "Debian/Ubuntu: libx11-6 ｜ Fedora: libX11" ;;
        libxcb.so.1)                    echo "Debian/Ubuntu: libxcb1 ｜ Fedora: libxcb" ;;
        libxkbcommon.so.0)              echo "Debian/Ubuntu: libxkbcommon0 ｜ Fedora: libxkbcommon" ;;
        libxcb-cursor.so.0)             echo "Debian/Ubuntu: libxcb-cursor0 ｜ Fedora: xcb-util-cursor" ;;
        libxkbcommon-x11.so.0)          echo "Debian/Ubuntu: libxkbcommon-x11-0 ｜ Fedora: libxkbcommon-x11" ;;
        libX11-xcb.so.1)                echo "Debian/Ubuntu: libx11-xcb1 ｜ Fedora: libX11-xcb" ;;
        libdbus-1.so.3)                 echo "Debian/Ubuntu: libdbus-1-3 ｜ Fedora: dbus-libs" ;;
        libglib-2.0.so.0)               echo "Debian/Ubuntu: libglib2.0-0 ｜ Fedora: glib2" ;;
        libfontconfig.so.1)             echo "Debian/Ubuntu: libfontconfig1 ｜ Fedora: fontconfig" ;;
        libfreetype.so.6)               echo "Debian/Ubuntu: libfreetype6 ｜ Fedora: freetype" ;;
        libstdc++.so.6)                 echo "Debian/Ubuntu: libstdc++6 ｜ Fedora: libstdc++" ;;
        *)                              echo "发行版对应包: 用 ldconfig -p | grep $(printf '%s' "$1" | sed 's/\.so.*//') 定位" ;;
    esac
}

report_failure() {  # $1 = 摘要（可多行）
    printf 'PhotoPipeline: %s\n' "$1" >&2
    printf '\n[%s] %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z' 2>/dev/null)" "$1" >>"$LOG" 2>/dev/null
    [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] || return 0
    msg="$1

日志文件: $LOG"
    command -v zenity  >/dev/null 2>&1 && zenity --error --no-wrap --title="PhotoPipeline 启动失败" --text="$msg" 2>/dev/null && return 0
    command -v kdialog >/dev/null 2>&1 && kdialog --error "$msg" 2>/dev/null && return 0
    command -v xmessage >/dev/null 2>&1 && xmessage -center "PhotoPipeline 启动失败 — $msg" 2>/dev/null && return 0
    return 0
}

# ---- ① 启动前依赖自检（ldd 缺失则跳过，不阻塞启动） ----
if command -v ldd >/dev/null 2>&1; then
    missing="$(for f in usr/bin/photopipeline usr/lib/*.so usr/lib/*.so.* usr/lib/qt-plugins/*/*.so; do
                   [ -e "$f" ] || continue
                   ldd "$f" 2>/dev/null | awk '/=> not found/ {print $1}'
               done | LC_ALL=C sort -u)"
    if [ -n "$missing" ]; then
        detail="$(printf '%s\n' "$missing" | while IFS= read -r lib; do
                      printf '  %s  →  %s\n' "$lib" "$(pkg_hint "$lib")"
                  done)"
        report_failure "缺少运行期系统库，无法启动（AppImage 已自带 Qt 与 libxcb-* 等插件依赖）:
$detail
安装上列包后重试；本机已安装清单可用 ldconfig -p 核对。"
        exit 3
    fi
fi

# ---- ② 启动 ----
if [ -t 0 ] && [ -t 2 ]; then
    exec usr/bin/photopipeline "$@"          # 终端场景: 原样透明（§2.9）
fi
# 无终端（双击/桌面启动器）: stderr → 日志；启动即失败 → 弹窗（不静默）
: >"$LOG" 2>/dev/null || LOG=/dev/null
T0="$(date +%s 2>/dev/null || echo 0)"
usr/bin/photopipeline "$@" 2>>"$LOG" &
CHILD=$!
trap 'kill -TERM "$CHILD" 2>/dev/null' INT TERM HUP
wait "$CHILD"
RC=$?
T1="$(date +%s 2>/dev/null || echo 0)"
# 143/130/129 = 被外部 TERM/INT/HUP 结束（会话注销、用户 kill）→ 不算启动失败，不弹窗；
# 其余非零（含 134=SIGABRT、139=SIGSEGV 等启动即崩）且 ≤15s 退出 → 弹窗（不静默）
case "$RC" in
    0|143|130|129) : ;;
    *)
        if [ "$((T1 - T0))" -le 15 ]; then
            report_failure "启动失败（退出码 $RC，$((T1 - T0)) 秒内退出）:
$(tail -n 12 "$LOG" 2>/dev/null)"
        fi
        ;;
esac
exit "$RC"
APPRUN
chmod 755 "$APPDIR/AppRun"

# ---- 打包前自检（§2.9 烟测 ②③ 的脚本内固化） ----
missing="$(LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$APPDIR/usr/bin/photopipeline" | grep 'not found' || true)"
[ -z "$missing" ] || die "AppDir 二进制存在未解析依赖:
$missing"

# ---- M2-T18 依赖闭包硬门禁：AppDir 内**每一个** ELF（主二进制 + usr/lib + 全部插件） ----
# A 类 not found（目标机缺库）/ B 类 libQt6* 解析到包外（随包 Qt 与系统 Qt 交叉版本冲突）
# 任一命中即失败 —— 这正是「GUI 双击无响应」缺陷的通用检出手段。
DEPS_TXT="$OUT_DIR/PhotoPipeline-${VERSION}-deps.txt"
if ! CLOSURE_OUT="$(bash "$ROOT/tools/appimage-check-closure.sh" "$APPDIR" 2>&1)"; then
    printf '%s\n' "$CLOSURE_OUT" >&2
    die "依赖闭包门禁失败（A=not found 或 B=libQt6* 外泄，见上）: $APPDIR"
fi
printf '%s\n' "$CLOSURE_OUT" >"$DEPS_TXT"
note "闭包门禁 PASS: 全部 ELF 无 not found、libQt6* 全部来自随包 Qt（$QT_LIB_DIR）"
note "  $(printf '%s\n' "$CLOSURE_OUT" | grep '① \[FAIL-A\]')"
note "  $(printf '%s\n' "$CLOSURE_OUT" | grep '② \[FAIL-B\]')"
note "  $(printf '%s\n' "$CLOSURE_OUT" | grep '③ \[SYS\]')"
note "目标机系统要求清单（供 README）→ $DEPS_TXT"
[ -f "$APPDIR/AppRun" ] || die "AppRun 缺失"
grep -qx 'Exec=photopipeline' "$APPDIR/usr/share/applications/photopipeline.desktop" \
    || die "desktop Exec 与冻结文本不一致"
grep -qx 'Icon=photopipeline' "$APPDIR/usr/share/applications/photopipeline.desktop" \
    || die "desktop Icon 与冻结文本不一致"
# 烟测 ④ 前置（产物内验证见打包后）
[ -d "$LIC_DIR" ] || die "许可目录缺失: usr/share/licenses"
[ -f "$LIC_DIR/PhotoPipeline/LICENSE" ] || die "本项目 LICENSE 未随包: usr/share/licenses/PhotoPipeline/LICENSE"
[ "$LIC_COUNT" -ge 30 ] || die "许可文本数不足（烟测 ④）: $LIC_COUNT < 30"
note "烟测 ④ 前置: usr/share/licenses/ 存在，copyright 文件 $LIC_COUNT 个（≥30 ✓）"

# ---- 打包（--runtime-file = 入库 runtime，离线，M2-T11d） ----
note "appimagetool → $APPIMAGE（--runtime-file=$RUNTIME）"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream --runtime-file "$RUNTIME" "$APPDIR" "$APPIMAGE"
[ -x "$APPIMAGE" ] || die "打包失败：$APPIMAGE 不存在"

# ---- 烟测 ④ 产物内验证：从打好的 AppImage 解包确认许可目录确实在产物里 ----
EXTRACT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/pp-appimage-verify.XXXXXX")"
trap 'rm -rf "$EXTRACT_DIR"' EXIT
( cd "$EXTRACT_DIR" && APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-extract 'usr/share/licenses/*' >/dev/null ) \
    || die "从产物解包 usr/share/licenses/ 失败: $APPIMAGE"
PKG_LIC="$EXTRACT_DIR/squashfs-root/usr/share/licenses"
PKG_COUNT="$(find "$PKG_LIC" -mindepth 2 -maxdepth 2 -type f -name copyright 2>/dev/null | wc -l)"
[ "$PKG_COUNT" -ge 30 ] || die "产物内许可文本数不足（烟测 ④）: $PKG_COUNT < 30"
[ -f "$PKG_LIC/PhotoPipeline/LICENSE" ] || die "产物内缺 usr/share/licenses/PhotoPipeline/LICENSE"
note "烟测 ④: 产物内 usr/share/licenses/ = $PKG_COUNT 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓"
note "  产物内示例: $(find "$PKG_LIC" -mindepth 2 -maxdepth 2 -type f -name copyright | LC_ALL=C sort | sed -n '1p' | sed "s#^$EXTRACT_DIR/##")"
note "  产物内示例: squashfs-root/usr/share/licenses/PhotoPipeline/LICENSE"

note "结构清单:"
( cd "$APPDIR" && find . -maxdepth 3 -mindepth 1 \( -type d -o -type f -o -type l \) | sort | sed 's/^/  /' )
note "usr/lib 共 $(find "$APPDIR/usr/lib" -maxdepth 1 -name '*.so*' | wc -l) 个 .so"
note "产物: $APPIMAGE ($(stat -c '%s' "$APPIMAGE") bytes)"
