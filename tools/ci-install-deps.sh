#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline — CI 系统依赖安装（M2-T19 ①）
#
# 三个 CI job 与播种 workflow 共用本脚本，唯一事实来源 = tools/ci-system-deps.txt。
#
# 用法:
#   tools/ci-install-deps.sh [MANIFEST]      # 默认 <repo>/tools/ci-system-deps.txt
#   PP_APT_DRY_RUN=1 tools/ci-install-deps.sh   # 只打印将要安装的包，不真装（本地核对用）
#
# 行为要点:
#   * 清单格式 = 每行一个包 + 行尾 `#` 注释；先剥注释再做词分割（不能直接 grep -v '^#'）。
#   * **容错但不静默**：本发行版不存在的包名不会让 apt 整体失败（否则发行版改名 = 全 job 红），
#     而是收集成 `::warning::` 醒目打印。真正的安全网是 tools/ci-check-deps.sh 的
#     soname 级硬断言 —— 改名可以容忍，**缺 soname 一定红**。
#   * 已安装的包 apt 是 no-op，因此本脚本可重复运行（幂等）。
set -euo pipefail

ROOT="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/.." && pwd)"
MANIFEST="${1:-$ROOT/tools/ci-system-deps.txt}"
[ -f "$MANIFEST" ] || { echo "ci-install-deps: 清单不存在: $MANIFEST" >&2; exit 2; }

# 剥注释 → 词分割 → 去空 → 去重（保持确定性顺序，便于日志比对）
mapfile -t PKGS < <(sed -e 's/#.*//' "$MANIFEST" | tr -s '[:space:]' '\n' | sed -e '/^$/d' | LC_ALL=C sort -u)
[ "${#PKGS[@]}" -gt 0 ] || { echo "ci-install-deps: 清单为空: $MANIFEST" >&2; exit 2; }

echo "ci-install-deps: 清单 $MANIFEST → ${#PKGS[@]} 个候选包"
if [ "${PP_APT_DRY_RUN:-0}" = "1" ]; then
    printf '  %s\n' "${PKGS[@]}"
    exit 0
fi

sudo apt-get update -qq

KNOWN=(); UNKNOWN=()
for p in "${PKGS[@]}"; do
    if apt-cache show --no-all-versions "$p" >/dev/null 2>&1; then
        KNOWN+=("$p")
    else
        UNKNOWN+=("$p")
    fi
done

if [ "${#UNKNOWN[@]}" -gt 0 ]; then
    # ::warning:: = GitHub Actions 注解；本地跑就是一行普通输出。
    echo "::warning::ci-install-deps: ${#UNKNOWN[@]} 个包在本发行版不存在，已跳过: ${UNKNOWN[*]}"
fi
[ "${#KNOWN[@]}" -gt 0 ] || { echo "ci-install-deps: 清单里没有一个包在本发行版存在" >&2; exit 1; }

echo "ci-install-deps: apt-get install -y ${#KNOWN[@]} 个包"
# -y 之外不加 --no-install-recommends: 保持与历史绿灯配置一致（libgl1 的推荐项含 mesa 驱动）。
sudo apt-get install -y "${KNOWN[@]}"
echo "ci-install-deps: 完成（${#KNOWN[@]}/${#PKGS[@]} 已安装/已存在）"
