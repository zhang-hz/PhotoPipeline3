#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline 应用图标生成器（M2-T11；docs/m2-tasks.md §2.5 参数冻结）。

冻结参数（不得改动的唯一事实来源 = 任务书 §2.5）：
  * 画布 256×256；
  * 背景 #2B2D31 圆角矩形，圆角半径 48px；
  * 内部 3×3 色块阵：每块 56px、间距 12px、起点 (52,52)；
  * 九色按序 #D02222、#22AA22、#0088CC、#DD9A2F、#8A5CF6、#2FBFA8、#C4622D、#5A8DD6、#9AA0A6；
  * 输出 share/icons/hicolor/256x256/apps/photopipeline.png。

幂等性：渲染与编码全程无时间戳/随机源（PNG 由 PIL 以固定 zlib 级别写出，不写任何 tEXt），
同参连跑两次 sha256 必然相等（T11 自验项）。

用法：
    python3 tools/make_icon.py [OUT_PNG]     # 默认写仓库内 §2.5 冻结路径
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

from PIL import Image, ImageDraw

# ---- §2.5 冻结参数 ----
SIZE = 256
BG_COLOR = "#2B2D31"
CORNER_RADIUS = 48
CELL = 56
GAP = 12
ORIGIN = (52, 52)
CELL_COLORS = (
    "#D02222",
    "#22AA22",
    "#0088CC",
    "#DD9A2F",
    "#8A5CF6",
    "#2FBFA8",
    "#C4622D",
    "#5A8DD6",
    "#9AA0A6",
)
GRID = 3  # 3×3

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = REPO_ROOT / "share/icons/hicolor/256x256/apps/photopipeline.png"


def render(out_path: Path) -> None:
    """按冻结参数渲染并写出 PNG（覆盖写，字节确定）。"""
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    # 圆角背景：PIL 的矩形坐标含右下端点，故用 SIZE-1
    draw.rounded_rectangle((0, 0, SIZE - 1, SIZE - 1), radius=CORNER_RADIUS, fill=BG_COLOR)
    for index, color in enumerate(CELL_COLORS):
        row, col = divmod(index, GRID)
        x = ORIGIN[0] + col * (CELL + GAP)
        y = ORIGIN[1] + row * (CELL + GAP)
        draw.rectangle((x, y, x + CELL - 1, y + CELL - 1), fill=color)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    # compress_level 固定 → zlib 输出确定；不传任何 pnginfo/tEXt
    image.save(out_path, format="PNG", compress_level=9)


def main(argv: list[str]) -> int:
    if len(argv) > 2:
        print(f"用法: {Path(argv[0]).name} [OUT_PNG]", file=sys.stderr)
        return 2
    out_path = Path(argv[1]).resolve() if len(argv) == 2 else DEFAULT_OUT
    render(out_path)
    digest = hashlib.sha256(out_path.read_bytes()).hexdigest()
    print(f"icon: {out_path}")
    print(f"icon: sha256 {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
