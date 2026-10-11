#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline — D15 格式门禁（M4-T4；依据 docs/v0.3.0-design.md §12.2 质量门禁表 + §12.3 job 预算）
#
# 判据：`clang-format --dry-run -Werror` 全源 0 diff。
#   全源 = git 跟踪的 src/** tests/** tools/** 下 C/C++ 源（根 .clang-format 为唯一风格源）。
#   本地与 CI 共用本脚本（pr-fast 的 fmt job 与开发机逐字同命令），故"本地绿 → CI 绿"是同一条判据。
#
# 用法:
#   bash tools/ci-format.sh            # 门禁：0 diff 才退 0（有 diff → 退 1 并逐文件点名）
#   bash tools/ci-format.sh --list     # 只打印将检查的文件清单
#   bash tools/ci-format.sh --write    # 就地格式化（= clang-format -i；开发机修格式用）
#
# 环境:
#   CLANG_FORMAT=<path>  覆盖可执行文件。默认按平台取仓库钉版：
#                        Windows（MINGW/MSYS/CYGWIN）→ tools/bin/clang-format-win64.exe
#                        其它（Linux CI / 容器）    → tools/bin/clang-format-linux-x86_64
#                        两者都在仓库内（+ .sha512），落在 PATH 上的 clang-format 仅作兜底。
#                        ⇒ 本地与 CI 用**同一份钉版二进制**，0 diff 才是同一判据。
#                        来源: PyPI clang-format 23.1.1 wheel（win_amd64 / manylinux_2_27_x86_64），
#                        wheel sha256 = eaf3075d…86d5 / 64900462…2f56（打包的即 LLVM 官方发行二进制，
#                        见 wheel METADATA Project-URL Download: github.com/llvm/llvm-project/releases）。
#   PP_FMT_VERSION=<x.y.z>  期望版本**校验值**（R-128：单源 = tools/versions.env 的
#                        CLANG_FORMAT_VERSION；本变量若设置且与单源不符 ⇒ 直接失败）。
#                        版本不符 = 判据不可比 ⇒ 直接失败；确要放行用 PP_FMT_ALLOW_ANY_VERSION=1。
#   完整性（R-036，deep-review-0.3.2）: 仓库钉版二进制在**执行前**做 sha512 校验 ——
#                        期望哈希钉在 tools/versions.env（CLANG_FORMAT_SHA512_WIN64 / _LINUX，
#                        外部信任锚；同目录的 *.sha512 侧车与二进制可被一并篡改，不作判据）。
#                        不符即失败。CLANG_FORMAT 显式指向外部文件时跳过哈希（用户显式指定）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STYLE_FILE="$ROOT/.clang-format"

# ── 单源解析 tools/versions.env（R-128/R-036/R-131：缺键/空值即失败，报错点落在真因） ──
versions_get() {  # $1 = key
    local v
    v="$(sed -n "s/^$1=//p" "$ROOT/tools/versions.env" | tail -1 | tr -d '\r')"
    if [ -z "$v" ]; then
        echo "ci-format: tools/versions.env 缺键 $1（或值为空）" >&2
        exit 2
    fi
    printf '%s' "$v"
}
EXPECTED_VERSION="$(versions_get CLANG_FORMAT_VERSION)"
if [ -n "${PP_FMT_VERSION:-}" ] && [ "$PP_FMT_VERSION" != "$EXPECTED_VERSION" ]; then
    echo "ci-format: PP_FMT_VERSION=$PP_FMT_VERSION 与单源 tools/versions.env 的" >&2
    echo "ci-format: CLANG_FORMAT_VERSION=$EXPECTED_VERSION 不一致（两处口径必须同源）" >&2
    exit 2
fi

mode=check
case "${1:-}" in
    --list) mode=list ;;
    --write) mode=write ;;
    '') ;;
    *) echo "ci-format: 未知参数 ${1}（可用: --list / --write）" >&2; exit 2 ;;
esac

[ -f "$STYLE_FILE" ] || {
    echo "ci-format: 根 .clang-format 缺失（$STYLE_FILE）—— 风格源不存在，判据无意义" >&2
    exit 2
}

# 解析 clang-format 本体（仓库钉版优先，保证与 CI 同版本/同二进制）。
case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*|Windows*) pinned=("$ROOT/tools/bin/clang-format-win64.exe" "$ROOT/tools/bin/clang-format-linux-x86_64") ;;
    *) pinned=("$ROOT/tools/bin/clang-format-linux-x86_64" "$ROOT/tools/bin/clang-format-win64.exe") ;;
esac
if [ -n "${CLANG_FORMAT:-}" ]; then
    CF="$CLANG_FORMAT"
elif [ -f "${pinned[0]}" ]; then
    CF="${pinned[0]}"
elif [ -f "${pinned[1]}" ]; then
    CF="${pinned[1]}"
elif command -v clang-format >/dev/null 2>&1; then
    CF="$(command -v clang-format)"
else
    echo "ci-format: 找不到 clang-format（CLANG_FORMAT=<path>，或补 tools/bin/clang-format-{win64.exe,linux-x86_64}）" >&2
    exit 2
fi

# ── R-036：执行前 sha512 完整性校验（期望哈希 = versions.env 外部信任锚） ──
case "$(basename "$CF")" in
    clang-format-win64.exe)      want="$(versions_get CLANG_FORMAT_SHA512_WIN64)"; pinned_use=1 ;;
    clang-format-linux-x86_64)   want="$(versions_get CLANG_FORMAT_SHA512_LINUX)"; pinned_use=1 ;;
    *)                           want=""; pinned_use=0 ;;
esac
if [ "$pinned_use" = 1 ]; then
    if command -v sha512sum >/dev/null 2>&1; then
        got="$(sha512sum "$CF" | awk '{print $1}')"
    else
        got="$(python3 -c "import hashlib,sys;print(hashlib.sha512(open(sys.argv[1],'rb').read()).hexdigest())" "$CF")"
    fi
    if [ "$got" != "$want" ]; then
        echo "ci-format: 钉版二进制 sha512 不符（R-036 信任锚）: $CF" >&2
        echo "ci-format:   期望 $want" >&2
        echo "ci-format:   实际 $got" >&2
        echo "ci-format: 二进制被替换/损坏 ⇒ 判据不可信，拒绝执行。" >&2
        exit 2
    fi
else
    echo "ci-format: 注意: CLANG_FORMAT 指向仓库外二进制，sha512 校验跳过（$CF）" >&2
fi

version="$("$CF" --version | head -1)"
case "$version" in
    *"$EXPECTED_VERSION"*) ;;
    *)
        echo "ci-format: clang-format 版本不符：期望 $EXPECTED_VERSION，实际 '$version'（$CF）" >&2
        echo "ci-format: 版本不同 ⇒ 判据不可比。用仓库钉版（tools/bin/clang-format-*，$EXPECTED_VERSION）" >&2
        echo "ci-format: 或显式放行 PP_FMT_ALLOW_ANY_VERSION=1（结果自负）。" >&2
        if [ "${PP_FMT_ALLOW_ANY_VERSION:-0}" != "1" ]; then
            exit 2
        fi
        ;;
esac

# 全源清单：git 跟踪 + 固定后缀（分平台/分目录零特例，新文件自动纳入）。
mapfile -t files < <(git -C "$ROOT" ls-files -- 'src/**' 'tests/**' 'tools/**' |
    grep -E '\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl|ipp)$' | sort)
if [ "${#files[@]}" -eq 0 ]; then
    echo "ci-format: 源清单为空（在仓库根外运行？$ROOT）" >&2
    exit 2
fi

if [ "$mode" = list ]; then
    printf '%s\n' "${files[@]}"
    echo "ci-format: 共 ${#files[@]} 个文件（$version）"
    exit 0
fi

cd "$ROOT"
if [ "$mode" = write ]; then
    "$CF" -i --style=file "${files[@]}"
    echo "ci-format: 已就地格式化 ${#files[@]} 个文件（$version）"
    exit 0
fi

# 门禁：--dry-run -Werror，输出里每个 "error: code should be clang-formatted" 点名一个待改文件。
log="$(mktemp)"
trap 'rm -f "$log"' EXIT
rc=0
"$CF" --dry-run -Werror --style=file "${files[@]}" >"$log" 2>&1 || rc=$?

bad=$(grep -c 'code should be clang-formatted' "$log" || true)
echo "ci-format: check ${#files[@]} files with $version (tool: $CF) → violations=$bad"
if [ "$rc" -ne 0 ]; then
    # 只回显前 40 行（CI 日志友好）；完整清单在本地直接跑同一命令即可复现。
    head -40 "$log"
    echo "ci-format: FAIL —— 以上文件需格式化（本地修: bash tools/ci-format.sh --write）"
    exit 1
fi
echo "ci-format: OK —— 全源 0 diff（$version，style=file:$STYLE_FILE）"
