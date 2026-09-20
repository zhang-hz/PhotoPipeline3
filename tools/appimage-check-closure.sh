#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline AppImage 运行期依赖闭包检查（M2-T18）
#
# 用法:
#   tools/appimage-check-closure.sh <AppDir> [--quiet]
#
# 背景（M2-T18 缺陷）: 旧 make_appimage.sh 只对主二进制做 ldd 递归闭包，Qt 插件
# （libqxcb.so 等）的依赖从未纳入。后果有两类:
#   A) not found —— 目标机缺库（插件直接加载失败）。
#   B) 交叉版本 ABI 冲突 —— 插件的 libQt6* 依赖解析到**系统** Qt（如系统 Qt 6.10 的
#      libQt6XcbQpa.so.6）而主体 Qt 是随包 Qt 6.8.3，运行期报
#      `libQt6Core.so.6: version 'Qt_6.10' not found`。本机装了系统 Qt 时 A 类不出现、
#      只有 B 类，故旧烟测（只跑主二进制）完全测不出来。
#
# 检查口径 = 运行期真实口径: 对 AppDir 内**每一个** ELF 跑
#   LD_LIBRARY_PATH=<AppDir>/usr/lib ldd <elf>
# 输出三段:
#   ① [FAIL-A] not found 明细（按 ELF 分组）
#   ② [FAIL-B] libQt6* 解析到 AppDir 之外（按 ELF 分组）
#   ③ [SYS]    解析到 AppDir 之外的其余库 = **目标机系统要求清单**（按 soname 聚合）
# 退出码: 0 = 无 A/B（③ 非空不算失败，属文档化的系统要求）。1 = 存在 A 或 B。
set -uo pipefail

APPDIR="${1:-dist/PhotoPipeline.AppDir}"
QUIET=0
[ "${2:-}" = "--quiet" ] && QUIET=1
[ -d "$APPDIR" ] || { echo "appimage-check-closure: 目录不存在: $APPDIR" >&2; exit 2; }
APPDIR="$(cd "$APPDIR" && pwd)"
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# AppDir 内全部 ELF（排除指向 AppDir 之外的符号链接目标重复项由 realpath 去重）
mapfile -t ELFS < <(
    find "$APPDIR" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) -print0 2>/dev/null |
        xargs -0 -r file -F$'\t' 2>/dev/null |
        awk -F'\t' '$2 ~ /ELF/ {print $1}' | LC_ALL=C sort -u
)
[ "${#ELFS[@]}" -gt 0 ] || { echo "appimage-check-closure: $APPDIR 内未找到 ELF" >&2; exit 2; }

FAIL_A=0
FAIL_B=0
declare -A SYS_SEEN=()
SYS_LINES=()
declare -A A_BLOCK=() B_BLOCK=()

rel() { printf '%s' "${1#"$APPDIR"/}"; }

for elf in "${ELFS[@]}"; do
    a=""; b=""
    while IFS= read -r line; do
        # 形式1: "<soname> => <path> (0x...)" / 形式2: "<soname> => not found"
        # 形式3: "<elf>: <lib>: version `Qt_6.10' not found (required by <lib>)"（符号版本冲突）
        case "$line" in
            *"=> not found"*)
                soname="$(awk '{print $1}' <<<"$line")"
                a+="    $soname"$'\n'
                ;;
            *"not found (required by"*)
                b+="    ${line#"$APPDIR"/}"$'\n'
                ;;
            *"=>"*)
                soname="$(awk '{print $1}' <<<"$line")"
                path="$(awk '{print $3}' <<<"$line")"
                case "$path" in
                    "$APPDIR"/*) : ;;  # 随包，OK
                    *)
                        case "$soname" in
                            libQt6*)
                                b+="    $soname -> $path"$'\n'
                                ;;
                        esac
                        if [ -z "${SYS_SEEN[$soname]:-}" ]; then
                            SYS_SEEN[$soname]="$(dirname "$path")"
                        fi
                        ;;
                esac
                ;;
        esac
    done < <(ldd "$elf" 2>&1)
    if [ -n "$a" ]; then FAIL_A=1; A_BLOCK["$(rel "$elf")"]="$a"; fi
    if [ -n "$b" ]; then FAIL_B=1; B_BLOCK["$(rel "$elf")"]="$b"; fi
done

echo "== AppImage 依赖闭包检查: $APPDIR"
echo "== 受检 ELF: ${#ELFS[@]} 个（LD_LIBRARY_PATH=$APPDIR/usr/lib）"

echo
echo "-- ① [FAIL-A] not found（目标机缺库，致命）: $([ "$FAIL_A" -eq 0 ] && echo 无 || echo "有（$((${#A_BLOCK[@]})) 个 ELF）")"
for k in $(printf '%s\n' "${!A_BLOCK[@]}" | LC_ALL=C sort); do
    [ -n "$k" ] || continue
    echo "  $k:"
    printf '%s' "${A_BLOCK[$k]}"
done

echo
echo "-- ② [FAIL-B] libQt6* 解析到 AppDir 之外（随包 Qt 与系统 Qt 交叉版本冲突，致命）: $([ "$FAIL_B" -eq 0 ] && echo 无 || echo "有（$((${#B_BLOCK[@]})) 个 ELF）")"
for k in $(printf '%s\n' "${!B_BLOCK[@]}" | LC_ALL=C sort); do
    [ -n "$k" ] || continue
    echo "  $k:"
    printf '%s' "${B_BLOCK[$k]}"
done

if [ "$QUIET" -eq 0 ]; then
    echo
    echo "-- ③ [SYS] 目标机系统要求（解析到包外的库，按 soname 聚合）: ${#SYS_SEEN[@]} 个"
    for k in $(printf '%s\n' "${!SYS_SEEN[@]}" | LC_ALL=C sort); do
        echo "  $k (${SYS_SEEN[$k]})"
    done
fi

echo
if [ "$FAIL_A" -eq 0 ] && [ "$FAIL_B" -eq 0 ]; then
    echo "== 结论: PASS（A/B 均无）"
    exit 0
fi
echo "== 结论: FAIL（A=$FAIL_A B=$FAIL_B）"
exit 1
