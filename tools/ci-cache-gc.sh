#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline — GitHub Actions 缓存清理（M2-T19 ④）
#
# 为什么需要: vcpkg/ccache 用**滚动 key**（末段含 run_id，见 build-test.yml 注释：固定 key 会
# "首次保存即冻结半成品、之后永远命中不再更新"），代价是每轮都新增条目（3 job × ~425MiB vcpkg
# + 1 × ccache）。不清理会在若干轮后顶到仓库缓存上限，触发平台 LRU 驱逐 —— 那时被淘汰的可能
# 正是最贵的 Qt 缓存。
#
# 规则（幂等、无副作用、只碰自己的前缀）:
#   * 只处理 key 以 `vcpkg-` / `ccache-` 开头的条目；`qt-*` 与其它前缀一律不碰。
#   * 按 createdAt 倒序，保留最新 PP_GC_KEEP 个（默认 8）。
#   * 另删 createdAt 早于 PP_GC_MAX_AGE_DAYS 天（默认 7）的条目。
#   * 任何失败（gh 缺失/无权限/网络）都 exit 0 —— 清理**不得**把 job 弄红。
#
# 用法:
#   tools/ci-cache-gc.sh                    # 真删并打印清单与释放空间
#   PP_GC_DRY_RUN=1 tools/ci-cache-gc.sh    # 只打印将删清单（本地核对 / 幂等验证）
# 环境:
#   GH_TOKEN              CI 里传 secrets.GITHUB_TOKEN（需 actions: write 权限）
#   PP_GC_KEEP            保留条数（默认 8）
#   PP_GC_MAX_AGE_DAYS    最大保留天数（默认 7）
#   PP_GC_PREFIXES        受管前缀（默认 "vcpkg- ccache-"）
set -uo pipefail

KEEP="${PP_GC_KEEP:-8}"
MAX_AGE_DAYS="${PP_GC_MAX_AGE_DAYS:-7}"
PREFIXES="${PP_GC_PREFIXES:-vcpkg- ccache-}"
DRY="${PP_GC_DRY_RUN:-0}"

if ! command -v gh >/dev/null 2>&1; then
    echo "ci-cache-gc: gh 不可用，跳过（不影响 job）"
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ci-cache-gc: python3 不可用，跳过（不影响 job）"
    exit 0
fi

echo "== GitHub Actions 缓存清理: 保留最新 ${KEEP} 个 / ${MAX_AGE_DAYS} 天，前缀 [${PREFIXES}]，DRY_RUN=${DRY}"

JSON="$(gh cache list --limit 100 --json id,key,sizeInBytes,createdAt 2>/dev/null)" || {
    echo "ci-cache-gc: gh cache list 失败（无权限/网络？），跳过（不影响 job）"
    exit 0
}
[ -n "$JSON" ] || { echo "ci-cache-gc: 缓存列表为空，无事可做"; exit 0; }

PLAN="$(printf '%s' "$JSON" | PP_GC_KEEP="$KEEP" PP_GC_MAX_AGE_DAYS="$MAX_AGE_DAYS" PP_GC_PREFIXES="$PREFIXES" python3 -c '
import json, os, sys, datetime
data = json.load(sys.stdin)
keep = int(os.environ["PP_GC_KEEP"])
days = float(os.environ["PP_GC_MAX_AGE_DAYS"])
prefixes = os.environ["PP_GC_PREFIXES"].split()
now = datetime.datetime.now(datetime.timezone.utc)

def parse(ts):
    return datetime.datetime.fromisoformat(ts.replace("Z", "+00:00"))

managed = [c for c in data if any(str(c.get("key", "")).startswith(p) for p in prefixes)]
managed.sort(key=lambda c: parse(c["createdAt"]), reverse=True)
total = sum(int(c["sizeInBytes"]) for c in managed)
print("INFO\t%d\t%d\t%d" % (len(managed), total, len(data)))
for i, c in enumerate(managed):
    age_days = (now - parse(c["createdAt"])).total_seconds() / 86400.0
    why = ""
    if i >= keep:
        why = "第 %d 新 > 保留 %d" % (i + 1, keep)
    elif age_days > days:
        why = "已存 %.1f 天 > %d 天" % (age_days, days)
    if why:
        print("DEL\t%s\t%s\t%s\t%s" % (c["id"], c["sizeInBytes"], c["key"], why))
')" || { echo "ci-cache-gc: 计划生成失败，跳过（不影响 job）"; exit 0; }

INFO="$(printf '%s\n' "$PLAN" | awk -F'\t' '$1=="INFO"{print $2" "$3" "$4}')"
read -r N_MANAGED TOTAL_BYTES N_ALL <<<"$INFO"
echo "   受管条目 ${N_MANAGED} 个 / 共 $(awk -v b="${TOTAL_BYTES:-0}" 'BEGIN{printf "%.1f MiB", b/1048576}')（仓库缓存条目总数 ${N_ALL}）"

DELS="$(printf '%s\n' "$PLAN" | awk -F'\t' '$1=="DEL"')"
if [ -z "$DELS" ]; then
    echo "   无条目需要删除（幂等：保持最新 ${KEEP} 个且都在 ${MAX_AGE_DAYS} 天内）"
    echo "== 完成：释放 0.0 MiB"
    exit 0
fi

FREED=0; N_DEL=0; N_FAIL=0
while IFS=$'\t' read -r _ id size key why; do
    [ -n "${id:-}" ] || continue
    printf '   删除 %-10s %8.1f MiB  %s  （%s）\n' "$id" "$(awk -v b="$size" 'BEGIN{print b/1048576}')" "$key" "$why"
    if [ "$DRY" = "1" ]; then
        FREED=$((FREED + size)); N_DEL=$((N_DEL + 1)); continue
    fi
    if gh cache delete "$id" >/dev/null 2>&1; then
        FREED=$((FREED + size)); N_DEL=$((N_DEL + 1))
    else
        # 已被并发清理 / 条目消失都不算失败（幂等语义）
        N_FAIL=$((N_FAIL + 1))
    fi
done <<<"$DELS"

if [ "$DRY" = "1" ]; then
    echo "== DRY-RUN：将删除 ${N_DEL} 个条目，可释放 $(awk -v b="$FREED" 'BEGIN{printf "%.1f MiB", b/1048576}')"
else
    echo "== 完成：已删除 ${N_DEL} 个条目，释放 $(awk -v b="$FREED" 'BEGIN{printf "%.1f MiB", b/1048576}')（${N_FAIL} 个已被并发删除/不存在，忽略）"
fi
exit 0
