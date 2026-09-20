#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# PhotoPipeline — M1b-U10 offscreen UI smoke（ctest 名 ui_smoke，由 CMake 在 PP_BUILD_DEV 下注册）
#
# 用法: tests/ui_smoke.sh [BUILD_DIR]      # 默认 build/release-dev
# 退出码: 0 = 冻结断言全过（末行 `UI-SMOKE OK shots=8 pages=3`，§4.3 冻结 8 张）；
#         2 = 用法/二进制缺失；其余 = photopipeline --ui-smoke 的退出码透传。
#
# 截图目录（M2-T21c 隔离裁定）：
#   * CI/ctest（本脚本默认）固定写 .cache/ui-smoke-ci —— 本脚本总是以 QT_QPA_PLATFORM=
#     offscreen 运行，产物是 Fusion/offscreen 口径，**不是**真实平台渲染。
#   * 人工审查集固定用 .cache/ui-review（真实平台渲染，不带 QT_QPA_PLATFORM=offscreen，
#     由 `photopipeline --ui-smoke --shots .cache/ui-review` 显式产出）。
#   二者必须分开：否则任何人跑一次 ctest 就会用 offscreen 图覆盖人工审查基线
#   （M2-T21b 复核期间实测发生过：8/8 字节与 sha256 全变、强调色由 Yaru 橙变 Fusion 蓝）。
set -euo pipefail

BUILD_DIR="${1:-build/release-dev}"
case "$BUILD_DIR" in
  /*) : ;;
  *) BUILD_DIR="$PWD/$BUILD_DIR" ;;
esac
BIN="$BUILD_DIR/photopipeline"
if [[ ! -x "$BIN" ]]; then
  echo "ui_smoke: binary not found or not executable: $BIN" >&2
  echo "ui_smoke: build it first, e.g. cmake --preset release-dev -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build --preset release-dev -j" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
SHOTS="$ROOT/.cache/ui-smoke-ci"
mkdir -p "$SHOTS"
cd "$ROOT"

LOG="$(mktemp -t ui-smoke-XXXXXX.log)"
trap 'rm -f "$LOG"' EXIT

set +e
QT_QPA_PLATFORM=offscreen "$BIN" --ui-smoke \
  --inputs "$ROOT/tests/golden/base" \
  --shots "$SHOTS" 2>&1 | tee "$LOG"
STATUS="${PIPESTATUS[0]}"
set -e

if [[ "$STATUS" -ne 0 ]]; then
  echo "ui_smoke: FAIL (photopipeline --ui-smoke exit $STATUS)" >&2
  exit "$STATUS"
fi
if ! grep -q '^UI-SMOKE OK shots=8 pages=3$' "$LOG"; then
  echo "ui_smoke: FAIL (missing frozen success line 'UI-SMOKE OK shots=8 pages=3')" >&2
  exit 1
fi
echo "UI-SMOKE pass"
