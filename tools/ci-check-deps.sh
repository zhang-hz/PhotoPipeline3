#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline — CI 系统依赖**覆盖自检**（M2-T19 ①，本地与 CI 皆可跑）
#
# 为什么需要它: 历史 CI 红灯的一半是"这一轮又缺一个系统包"（nasm → libgl-dev → libglx-dev →
# libopengl-dev → libegl1），每轮只暴露一个。本脚本把这件事变成**一次可证伪的断言**：
# 从真实构建产物派生全部 DT_NEEDED soname，逐个断言"能被我方清单里的某个包提供"，
# 未覆盖项必须为 0。新出现的 soname 会在这里变成 UNCOVERED 并给出应加入的包名。
#
# 用法:
#   tools/ci-check-deps.sh [BUILD_DIR ...]       # 省略时自动探测构建目录
#   tools/ci-check-deps.sh --sonames FILE        # 直接检查给定的 soname 列表（每行一个，# 注释）
#   tools/ci-check-deps.sh --manifest FILE       # 覆盖清单路径（默认 tools/ci-system-deps.txt）
#   tools/ci-check-deps.sh -v|--verbose          # 逐条打印已覆盖映射
#   tools/ci-check-deps.sh -q|--quiet            # 只打印汇总与未覆盖项
# 退出码: 0 = 未覆盖项 0 个；1 = 有未覆盖项；2 = 用法/环境错误
#
# soname 来源（与 tools/ci-system-deps.txt 的推导口径**一致**，改一处必须改两处）:
#   构建树全部 ELF（可执行 + .so） ∪ vcpkg_installed/**/*.so* ∪
#   Qt 工具链中**实际随 AppImage 分发**的插件（offscreen/xcb 平台插件 + imageformats/
#   iconengines/styles/tls）。刻意不含 Qt 的 wayland/eglfs/qml/sql 插件与库。
#
# 判定规则:
#   * 解析到 /lib,/lib64,/usr/lib,/usr/lib32,/usr/lib64 之外 → BUNDLED（随 Qt/vcpkg/构建树分发）
#   * 解析到系统前缀 → dpkg -S 反查所属包；包 ∈ 清单（或 ∈ BASE_PKGS）→ COVERED，否则 UNCOVERED
#   * ldd 报 `not found` 或 ldconfig -p 查不到 → UNCOVERED（未安装）
#
# 近似手段说明: apt-file 未安装时用 `ldconfig -p` 提供 soname→路径，再 `dpkg -S` 反查包名 ——
#   二者都是本地已有数据库，无需网络。
set -uo pipefail

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
MANIFEST="$ROOT/tools/ci-system-deps.txt"
QUIET=0; VERBOSE=0; SONAME_FILE=""
BUILD_DIRS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --sonames)   SONAME_FILE="${2:-}"; shift 2 ;;
        --manifest)  MANIFEST="${2:-}"; shift 2 ;;
        -v|--verbose) VERBOSE=1; shift ;;
        -q|--quiet)   QUIET=1; shift ;;
        -h|--help)   sed -n '2,26p' "$0"; exit 0 ;;
        -*) echo "ci-check-deps: 未知选项 $1" >&2; exit 2 ;;
        *)  BUILD_DIRS+=("$1"); shift ;;
    esac
done

[ -f "$MANIFEST" ] || { echo "ci-check-deps: 清单不存在: $MANIFEST" >&2; exit 2; }

# 基础包：任何 Debian/Ubuntu 用户态都必然存在（apt/dpkg 自身依赖），刻意不列入安装清单。
BASE_PKGS=" libc6 libstdc++6 libgcc-s1 "
in_base_pkgs() { case "$BASE_PKGS" in *" $1 "*) return 0 ;; *) return 1 ;; esac; }
in_manifest()  { printf '%s\n' "$MANIFEST_PKGS" | grep -qx -- "$1"; }

say() { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }
skip_soname() {  # 非"可安装库"的条目：内核虚拟 DSO、动态加载器（随 libc6 交付）、ldd 打出的路径
    case "$1" in
        linux-vdso.so.*|linux-gate.so.*|ld-linux*.so.*) return 0 ;;
        /*) return 0 ;;
        *) return 1 ;;
    esac
}

# ── 0. 清单解析 ────────────────────────────────────────────────────────────────
MANIFEST_PKGS="$(sed -e 's/#.*//' "$MANIFEST" | tr -s '[:space:]' '\n' | sed -e '/^$/d' | LC_ALL=C sort -u)"
MANIFEST_N="$(printf '%s\n' "$MANIFEST_PKGS" | grep -c .)"

# ── 1. soname 集合 ─────────────────────────────────────────────────────────────
declare -A SONAME_PATH=()      # soname → 解析到的真实路径（"" = not found）

collect_from_ldd() {  # $1 = ELF 路径；把 ldd 输出里的 soname→路径 记入 SONAME_PATH
    local soname path
    while read -r soname path; do
        [ -n "$soname" ] || continue
        [ "$path" = "not" ] && path=""                      # `libfoo.so.1 => not found`
        skip_soname "$soname" && continue
        if [ -n "${SONAME_PATH[$soname]+x}" ] && [ -n "${SONAME_PATH[$soname]}" ]; then
            continue                                        # 已解析到路径，不覆盖
        fi
        SONAME_PATH["$soname"]="$path"
    done < <(ldd "$1" 2>/dev/null | awk '
        /=>/ { n=$1; p=$3; print n, p; next }
        /^[[:space:]]*[^[:space:]]+[[:space:]]+\(0x/ { print $1, "" }')
}

ELF_ROOTS=()
QT_LIB=""; QT_PLUGINS=""; VCPKG_LIB="$ROOT/vcpkg_installed/x64-linux/lib"

if [ -n "$SONAME_FILE" ]; then
    [ -f "$SONAME_FILE" ] || { echo "ci-check-deps: soname 列表不存在: $SONAME_FILE" >&2; exit 2; }
    while IFS= read -r s; do
        s="${s%%#*}"; s="$(printf '%s' "$s" | tr -d '[:space:]')"
        [ -n "$s" ] && SONAME_PATH["$s"]=""             # 路径留空 → 交给 ldconfig -p 解析
    done < "$SONAME_FILE"
    say "== 输入: soname 列表 $SONAME_FILE（${#SONAME_PATH[@]} 条）"
else
    if [ "${#BUILD_DIRS[@]}" -eq 0 ]; then
        for c in build/release-dev build/release build/m2-release build/m2-t19-dev build/m2-t19; do
            [ -d "$ROOT/$c" ] && BUILD_DIRS+=("$ROOT/$c")
        done
    fi
    [ "${#BUILD_DIRS[@]}" -gt 0 ] || { echo "ci-check-deps: 未指定构建目录且自动探测失败（先构建一次）" >&2; exit 2; }
    for d in "${BUILD_DIRS[@]}"; do
        [ -d "$d" ] || { echo "ci-check-deps: 构建目录不存在: $d" >&2; exit 2; }
    done

    for c in "${QT_DIR:-}" "$ROOT/Qt/6.8.3/gcc_64" "$ROOT/.toolchain/Qt/6.8.3/gcc_64"; do
        [ -n "$c" ] && [ -d "$c/lib" ] && { QT_LIB="$c/lib"; QT_PLUGINS="$c/plugins"; break; }
    done

    # ① 随包 Qt 插件（AppImage 分发集）
    for p in platforms/libqoffscreen.so platforms/libqxcb.so; do
        [ -f "$QT_PLUGINS/$p" ] && ELF_ROOTS+=("$QT_PLUGINS/$p")
    done
    for d in imageformats iconengines styles tls; do
        while IFS= read -r f; do ELF_ROOTS+=("$f"); done < <(find "$QT_PLUGINS/$d" -maxdepth 1 -name '*.so' 2>/dev/null)
    done
    # ② 构建树 ELF（可执行 + 共享库）
    for d in "${BUILD_DIRS[@]}"; do
        while IFS= read -r f; do ELF_ROOTS+=("$f"); done < <(
            find "$d" -maxdepth 2 \( -name '*.so' -o -name '*.so.*' \) 2>/dev/null
            find "$d" -maxdepth 1 -type f -executable 2>/dev/null | while IFS= read -r x; do
                readelf -h "$x" >/dev/null 2>&1 && echo "$x"
            done
        )
    done
    # ③ vcpkg 共享库
    while IFS= read -r f; do ELF_ROOTS+=("$f"); done < <(find "$VCPKG_LIB" -maxdepth 1 \( -name '*.so' -o -name '*.so.*' \) 2>/dev/null)

    [ "${#ELF_ROOTS[@]}" -gt 0 ] || { echo "ci-check-deps: 构建树里找不到 ELF（构建目录选错？）" >&2; exit 2; }

    # ldd 用 Qt/vcpkg 目录做搜索路径：解析到两者之一 ⇒ 属"随包"，与 AppImage 判定一致。
    [ -n "$QT_LIB" ] && export LD_LIBRARY_PATH="$QT_LIB:$VCPKG_LIB:${LD_LIBRARY_PATH:-}"
    for elf in "${ELF_ROOTS[@]}"; do collect_from_ldd "$elf"; done

    say "== 输入: 构建目录 ${BUILD_DIRS[*]}"
    say "         Qt lib=${QT_LIB:-<未找到>} plugins=${QT_PLUGINS:-<未找到>}"
    say "         ELF 根 ${#ELF_ROOTS[@]} 个（构建树 + vcpkg + 随包 Qt 插件）"
fi

TOTAL="${#SONAME_PATH[@]}"
[ "$TOTAL" -gt 0 ] || { echo "ci-check-deps: soname 集合为空" >&2; exit 2; }

# ── 2. 分流：随包 vs 系统 ──────────────────────────────────────────────────────
declare -A SYS_SONAMES=()
bundled_n=0
for s in "${!SONAME_PATH[@]}"; do
    p="${SONAME_PATH[$s]}"
    if [ -z "$p" ]; then SYS_SONAMES["$s"]="@MISSING@"; continue; fi   # ldd 报 not found
    case "$p" in
        /lib/*|/lib32/*|/lib64/*|/usr/lib/*|/usr/lib32/*|/usr/lib64/*) SYS_SONAMES["$s"]="$p" ;;
        *) bundled_n=$((bundled_n + 1)) ;;
    esac
done

# ── 3. soname → 提供包（ldconfig -p 给路径，dpkg -S 反查包名） ────────────────────
# canon: Ubuntu 是 merged-usr（/lib -> usr/lib），而 ldconfig -p 在 24.04 上按
# /etc/ld.so.conf.d 里的 **/lib/x86_64-linux-gnu** 原样打印路径，dpkg 数据库里记的却是
# **/usr/lib/x86_64-linux-gnu** ⇒ 不做 realpath 规范化，dpkg -S 会对**每一个** soname 报
# "没有找到相匹配的路径"（run 35520031504 实测：61/61 假阳性）。规范化同时把
# libEGL.so.1 → libEGL.so.1.1.0（dpkg -S 同样接受）。
canon() { realpath -m -- "$1" 2>/dev/null || printf '%s\n' "$1"; }

declare -A LDCONFIG_PATH=()
while read -r soname path; do
    [ -n "$soname" ] || continue
    [ -n "${LDCONFIG_PATH[$soname]+x}" ] && continue
    LDCONFIG_PATH["$soname"]="$path"
done < <(ldconfig -p 2>/dev/null | awk '{ n=$1; p=""; for (i=NF;i>0;i--) if ($i ~ /^\//) { p=$i; break } print n, p }')

declare -A DPKG_OWNER=(); PATHS=()
for s in "${!SYS_SONAMES[@]}"; do
    p="${SYS_SONAMES[$s]}"
    [ "$p" = "@MISSING@" ] && p="${LDCONFIG_PATH[$s]:-}"
    [ -n "$p" ] && PATHS+=("$(canon "$p")")
done
if [ "${#PATHS[@]}" -gt 0 ]; then
    # dpkg -S 接受多路径；未命中的路径写 stderr 且退出码非 0，故忽略退出码。
    while IFS=$'\t' read -r pkg path; do
        pkg="${pkg%%:*}"                       # 去掉 :amd64 架构限定
        DPKG_OWNER["$(canon "$path")"]="$pkg"
    done < <(dpkg -S "${PATHS[@]}" 2>/dev/null | sed -n 's|^\([^:]*\(:[a-zA-Z0-9]*\)\?\): \(/.*\)$|\1\t\3|p')
fi

# ── 4. 判定 ───────────────────────────────────────────────────────────────────
UNCOVERED=(); COVERED_N=0; USED_PKGS=" "
while IFS= read -r s; do
    [ -n "$s" ] || continue
    p="${SYS_SONAMES[$s]}"
    [ "$p" = "@MISSING@" ] && p="${LDCONFIG_PATH[$s]:-}"
    owner=""
    [ -n "$p" ] && owner="${DPKG_OWNER[$(canon "$p")]:-}"
    if [ -n "$owner" ] && { in_manifest "$owner" || in_base_pkgs "$owner"; }; then
        COVERED_N=$((COVERED_N + 1))
        USED_PKGS="$USED_PKGS$owner "
        [ "$VERBOSE" = 1 ] && say "  COVERED   $(printf '%-26s' "$s") ← $owner  ($p)"
        continue
    fi
    if [ -n "$owner" ]; then
        UNCOVERED+=("$s"$'\t'"$p"$'\t'"包 $owner 未列入 $MANIFEST"$'\t'"$owner")
    elif [ -n "$p" ]; then
        UNCOVERED+=("$s"$'\t'"$p"$'\t'"该路径不属于任何 dpkg 包（私有/手工安装；已 realpath 规范化）"$'\t'"")
    else
        UNCOVERED+=("$s"$'\t'"-"$'\t'"本机未安装（ldd 报 not found 且 ldconfig -p 查不到）"$'\t'"")
    fi
done < <(printf '%s\n' "${!SYS_SONAMES[@]}" | LC_ALL=C sort)

# ── 5. 报告 ───────────────────────────────────────────────────────────────────
echo "== CI 系统依赖覆盖自检"
echo "   清单        : $MANIFEST（$MANIFEST_N 个包 + BASE_PKGS:${BASE_PKGS% })"
echo "   soname 总数 : $TOTAL   （随包/加载器 $bundled_n，需系统提供 ${#SYS_SONAMES[@]}）"
echo "   已覆盖      : $COVERED_N / ${#SYS_SONAMES[@]}"
echo "   未覆盖      : ${#UNCOVERED[@]}"

UNUSED=""
for p in $MANIFEST_PKGS; do
    case "$USED_PKGS" in *" $p "*) ;; *) UNUSED="$UNUSED $p" ;; esac
done
[ -n "$UNUSED" ] && say "   （清单中未被 soname 命中的条目，正常：-dev/工具链包）:$UNUSED"

if [ "${#UNCOVERED[@]}" -gt 0 ]; then
    echo ""
    echo "-- [UNCOVERED] 以下 soname 没有任何清单包提供（每一条都会让运行期/链接期失败）:"
    SUGGEST=""
    for u in "${UNCOVERED[@]}"; do
        IFS=$'\t' read -r s p why pkg <<<"$u"
        printf '   %-26s %s\n' "$s" "$why"
        [ "$p" != "-" ] && printf '   %-26s 解析路径: %s\n' "" "$p"
        [ -n "$pkg" ] && SUGGEST="$SUGGEST $pkg"
    done
    echo ""
    echo "   修复: 把上列包名加入 $MANIFEST（行尾写清用途），再重跑本脚本。"
    if [ -n "$SUGGEST" ]; then
        echo "   建议: sudo apt-get install -y $(printf '%s\n' $SUGGEST | LC_ALL=C sort -u | tr '\n' ' ')"
    fi
    exit 1
fi

echo ""
echo "== 结论: PASS —— 未覆盖项 = 0（$COVERED_N 个系统 soname 全部由清单包提供）"
exit 0
