#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""HEIF 配置矩阵验证器（M9 · docs/m9-adjudications.md A8）。

目的：把「iPhone 可能硬解的 HEVC/HEIF 配置组合」穷举成可复核矩阵，逐例编码 +
结构回读 + 解码检查，输出 JSONL 证据与 Markdown 汇总。

真源（本工具不复制规则，只复刻事实表）：
  * Level / MaxLumaPs：src/codecs/heif_capability.cpp（x265 4.3 level.cpp 第一列）。
    **6.2 的 4,278,190,080 是 maxLumaSamplesPerSecond，不是 MaxLumaPs**；62 → 35,651,584。
  * 档位编号：src/codecs/heif_profile.h（0..6 既有，7..15 为 RExt 追加；Main12=3 是
    12 位兼容别名，色度随输入）。
  * 信号期望 idc：heif_profile_signal()（1 Main / 2 Main10 / 3 MSP / 4 RExt）。
  * grid 规划：heif_plan_grid()（C17' = tile 由规划 Level 的 MaxLumaPs(M) + 单边上限
    D=floor(sqrt(8M)) 与图像宽高推导，面积优先、无固定基准；Level=自动时规划上界 = still
    互操作档 Level 5.1 = 3GPP TS 26.140 §4.5 / TS 26.141 §4.2 的 hvc1.1.2.L153；模式
    1=总是 grid，2=仅整图装不下时 grid）。

用法（一切命令经 tools/env.py run --）：
  python tools/env.py run -- python tools/heif_matrix_check.py --phase baseline
  python tools/env.py run -- python tools/heif_matrix_check.py --phase final
  ... --set baseline-small --max-cases 5            # 冒烟
  ... --set selftest                               # 只验解析器（不编码）
  ... --resume --force                             # 默认 resume；--force 重跑已有 case

退出码：0 = 没有 unexpected / legal 失败；1 = 有；2 = 参数/环境错误。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import traceback
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ENV_PY = REPO / "tools" / "env.py"
HVCC_PY = REPO / "build" / "analysis" / "hvcc.py"
IINFO = REPO / "vcpkg_installed" / "x64-windows-avx2" / "tools" / "openimageio" / "iinfo.exe"
DEFAULT_EXE = REPO / "build" / "release-dev" / "photopipeline.exe"
MATRIX_DIR = REPO / "build" / "matrix"
# W2-T1：并行任务隔离用（--tmp-dir 覆盖；默认沿用 M9 的 build/matrix/tmp）。
TMP_DIR = MATRIX_DIR / "tmp"
SMALL_TIF = REPO / "build" / "tmp-heif" / "small.tif"
T1_TIF = REPO / "super" / "T1.tif"

WORKERS = 1  # 硬规则：每例一个 worker，避免编码器内部线程/资源竞争影响 verdict。

# ---------------------------------------------------------------------------
# 事实表（逐字对齐 src/codecs/heif_capability.cpp kLevels；idc = 3 * level）
# ---------------------------------------------------------------------------
LEVEL_PS = {
    10: 36864,
    20: 122880,
    21: 245760,
    30: 552960,
    31: 983040,
    40: 2228224,
    41: 2228224,
    50: 8912896,
    51: 8912896,
    52: 8912896,
    60: 35651584,
    61: 35651584,
    62: 35651584,
    63: 80216064,
    70: 142606336,
    71: 142606336,
    72: 142606336,
}
LEVELS = [0] + sorted(LEVEL_PS)
# C17'：still 单图互操作档 Level 上界 = 51 (=5.1)；证据 = 3GPP TS 26.140 §4.5 /
# TS 26.141 §4.2（HEIF 'heic'+'MiHB' 互操作档：HEVC Main/Main Tier/Level 5.1，
# itemTypes hvc1.1.2.L153）。H.265 本身不强制此上限（见 docs/heif-tile-sizing.md）。
STILL_INTEROP_MAX_LEVEL = 51
LEVEL_SR = {  # maxLumaSamplesPerSecond（仅用于文档/自动 level 说明；静止图 fps≈1/25 不构成约束）
    10: 552960,
    20: 3686400,
    21: 7372800,
    30: 16588800,
    31: 33177600,
    40: 66846720,
    41: 133693440,
    50: 267386880,
    51: 534773760,
    52: 1069547520,
    60: 1069547520,
    61: 2139095040,
    62: 4278190080,
    63: 4812963840,
    70: 4812963840,
    71: 8556380160,
    72: 17112760320,
}
CHROMAS = ["420", "422", "444"]
CHROMA_IDX = {"420": 1, "422": 2, "444": 3}
BITDEPTHS = [8, 10, 12]
# W2-T3（C20）：HEIF/x265 的 tune 事实表（与 src/codecs/enc_tune_facts.cpp 逐值对齐）：
#   * 合法 = 4 原生 + animation（引擎注入近似仿真）；
#   * 置灰 = zerolatency（单帧无效果）/ vmaf（x265 空实现）→ 必须干净失败且点名「不可用」。
HEIF_TUNE_LEGAL = ["psnr", "ssim", "grain", "fastdecode", "animation"]
HEIF_TUNE_DISABLED = ["zerolatency", "vmaf"]

# 档位编号单源 = src/codecs/heif_profile.h（0..6 既有编号，7..15 RExt 追加，
# 16..21 = W2-T2 的 still 变体）。
# key: int 编号；value: (显示名, 期望 signal dict) ；signal 为 None = Auto（不校验信号）。
# still=True → 编码后除 hvcC 外还断言 SPS PTL 的 intra/one_picture（C18）；
# unavailable=True → 该档位实测不可用（编码必须**干净失败**且错误点名 still/DPB）。
PROFILES = {
    0: ("Auto", None),
    1: ("Main", {"idc": 1, "chroma": "420", "bd": 8}),
    2: ("Main10", {"idc": 2, "chroma": "420", "bd": 10}),
    3: ("Main12", {"idc": 4, "chroma": None, "bd": 12}),  # 兼容别名：12 位、色度随输入
    4: ("MainStill", {"idc": 3, "chroma": "420", "bd": 8}),
    5: ("High", {"avif_only": True}),
    6: ("Professional", {"avif_only": True}),
    7: ("Rext420_8", {"idc": 4, "chroma": "420", "bd": 8}),
    8: ("Rext420_10", {"idc": 4, "chroma": "420", "bd": 10}),
    9: ("Rext420_12", {"idc": 4, "chroma": "420", "bd": 12}),
    10: ("Rext422_8", {"idc": 4, "chroma": "422", "bd": 8}),
    11: ("Rext422_10", {"idc": 4, "chroma": "422", "bd": 10}),
    12: ("Rext422_12", {"idc": 4, "chroma": "422", "bd": 12}),
    13: ("Rext444_8", {"idc": 4, "chroma": "444", "bd": 8}),
    14: ("Rext444_10", {"idc": 4, "chroma": "444", "bd": 10}),
    15: ("Rext444_12", {"idc": 4, "chroma": "444", "bd": 12}),
    # —— W2-T2（C18）：still 变体 16..21 ——
    # 16 = Main 10 Still Picture：走 Main10 注入路径，PTL 无 still 位；SPS DPB 实测 4/2
    #（要求 0/0）→ 实测不可用（UI 置灰），编码必须干净失败（错误点名 DPB/still）。
    16: ("Main10Still", {"idc": 2, "chroma": "420", "bd": 10, "still": True,
                         "unavailable": True}),
    17: ("Main422_10Still", {"idc": 4, "chroma": "422", "bd": 10, "still": True}),
    18: ("Main422_12Still", {"idc": 4, "chroma": "422", "bd": 12, "still": True}),
    19: ("Main444_Still", {"idc": 4, "chroma": "444", "bd": 8, "still": True}),
    20: ("Main444_10Still", {"idc": 4, "chroma": "444", "bd": 10, "still": True}),
    21: ("Main444_12Still", {"idc": 4, "chroma": "444", "bd": 12, "still": True}),
}
# HEVC 合法档位编号（用于 final 的 signal×level 深矩阵；still 变体单列 final-still 集）
HEVC_SIGNAL_PROFILES = [1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15]
# W2-T2 still 变体（17..21 可编码；16 预期干净失败）
STILL_PROFILES = [16, 17, 18, 19, 20, 21]


def level_idc(level: int) -> int:
    """x265 level 枚举 → general_level_idc。0=自动（用 auto_level 推导）。"""
    return level * 3


def auto_level(luma_samples: int) -> int:
    """x265 determineLevel 的最小可行 Level（静止图 fps≈1/25，sample-rate 不绑定）。

    与 src/codecs/heif_capability.cpp heif_level_min_idc 同口径：升序第一个
    max_luma_ps >= luma_samples 的 Level。
    """
    for lv in sorted(LEVEL_PS):
        if LEVEL_PS[lv] >= luma_samples:
            return lv
    return 85


def level_fits(level: int, width: int, height: int) -> bool:
    if level == 0:
        return True
    if level not in LEVEL_PS:
        return False
    # 与 src/codecs/heif_capability.cpp heif_level_fits 同口径：x265 在 enforceLevel 前把
    # source 宽高补齐到 minCUSize(=8) 的倍数，Level 判定用补齐后的面积；另有单边
    # ≤ sqrt(8*MaxLumaPs) 的约束。
    pw = (width + 7) // 8 * 8
    ph = (height + 7) // 8 * 8
    if pw * ph > LEVEL_PS[level]:
        return False
    limit = int((8.0 * LEVEL_PS[level]) ** 0.5)
    return width <= limit and height <= limit


def align_down(value: int, a: int) -> int:
    if a <= 1:
        return value
    return (value // a) * a


def plan_grid(grid_mode: int, level: int, width: int, height: int, chroma: str,
              tile_w: int = 0, tile_h: int = 0, profile: int | None = None) -> dict:
    """复刻 C17'（0.4.0.alpha2）的 heif_plan_grid（src/codecs/heif_capability.cpp）。

    语义：
      * grid_mode 内部编号 0=关闭 / 1=总是 / 2=自动；手动 tile > 0 时强制分块；
      * Level=自动 时只有整图超出最高 Level 7.2 容量才分块，否则单图（默认零变化）；
      * 自动 tile 由「规划 Level 的 MaxLumaPs(M) + 单边上限 D=floor(sqrt(8M))」与图像
        宽高推导（**无固定基准**；面积优先、同面积取 |tw/th − 图宽高比| 最小；8 对齐、
        ≥64；tile 宽高 clamp 到图像宽高与 8192）；
      * 规划 Level：显式 Level 直接用；Level=自动 → still 互操作上界 5.1（3GPP
        TS 26.140/26.141，hvc1.1.2.L153）；still 单图档位（MainStill/16..21）+
        显式 Level>5.1 → 压到 5.1 并给 warning（编码 Level 仍保留用户值）；
      * 手动 tile 8 向下对齐；<64 / >8192 / 显式 Level 装不下 → ok=False + 中文错误；
      * rows×cols==1 → 降级单图；单边 >256 或 rows×cols>65535 → 错误。
    """
    plan = {"ok": True, "error": "", "use_grid": False, "level_idc": level,
            "tile_width": 0, "tile_height": 0, "cols": 0, "rows": 0,
            "tile_luma_samples": 0, "level_fits_image": level_fits(level, width, height),
            "downgraded": False, "manual": bool(tile_w or tile_h),
            "tile_level_idc": 0, "note": "", "planning_level_idc": 0,
            "still_capped": False, "warning": ""}
    if width <= 0 or height <= 0:
        plan.update(ok=False, error="图像尺寸无效")
        return plan
    if level != 0 and level not in LEVEL_PS:
        plan.update(ok=False, level_fits_image=False, error="Level 不存在")
        return plan
    if grid_mode not in (0, 1, 2):
        return plan
    explicit = level != 0
    manual = bool(tile_w or tile_h)
    # C17'：still 单图档位集合 = MainStill(4) + W2-T2 still 变体(16..21)。显式这些档位 +
    # 显式 Level>5.1 时规划压到 5.1（编码 Level 仍保留用户值）；Auto/None 视为非 still
    # （enc_heif 的 effective_profile 推导对 Auto 8 位 4:2:0 会得到 Main Still Picture，
    # 但矩阵的 final-grid 用例没有「Auto + 显式 Level>5.1」组合，故不影响期望）。
    still_profile = profile in (4, 16, 17, 18, 19, 20, 21)
    planning_level = level
    still_capped = False
    if level == 0:
        planning_level = STILL_INTEROP_MAX_LEVEL  # 51 = Level 5.1
    elif still_profile and level > STILL_INTEROP_MAX_LEVEL:
        planning_level = STILL_INTEROP_MAX_LEVEL
        still_capped = True
    plan["planning_level_idc"] = planning_level
    plan["still_capped"] = still_capped
    if still_capped:
        plan["warning"] = (f"x265:level-idc={level} 超过 still 单图档位互操作上界 Level 5.1"
                           f"（3GPP TS 26.140 §4.5 / TS 26.141 §4.2，itemTypes "
                           f"hvc1.1.2.L153）；tile 规划按 5.1，编码 Level 保留用户值")
    if grid_mode == 0:
        if explicit and not plan["level_fits_image"]:
            plan.update(ok=False, error=f"图像超过 Level {level} 上限且分块模式=关闭")
        return plan

    auto_capacity_fits = level_fits(72, width, height)
    if manual:
        want_grid = True
    elif grid_mode == 1:
        want_grid = True
    elif explicit:
        want_grid = not plan["level_fits_image"]
    else:
        want_grid = not auto_capacity_fits
    if not want_grid:
        return plan
    plan["use_grid"] = True
    plan["tile_level_idc"] = level if explicit else 0

    k_min_tile, k_align, k_max_tile = 64, 8, 8192

    def align8(v: int) -> int:
        return 0 if v <= 0 else (v // k_align) * k_align

    def best_tile_for(planning: int) -> tuple[int, int, bool]:
        """C17'：面积优先枚举（同面积取最贴近图像宽高比；8 对齐、≥64、clamp 图像/8192）。"""
        max_luma = LEVEL_PS[planning]
        dim_cap = int((8.0 * max_luma) ** 0.5)
        w_max = min(width, dim_cap)
        h_max = min(height, dim_cap)
        best_w = best_h = 0
        best_area = -1
        best_diff = 1e9
        target = width / height
        w = align8(w_max)
        while w >= k_min_tile:
            h = align8(min(h_max, max_luma // w))
            if h >= k_min_tile:
                area = w * h
                if area <= max_luma:
                    diff = abs(w / h - target)
                    if area > best_area or (area == best_area and diff < best_diff):
                        best_area, best_diff, best_w, best_h = area, diff, w, h
            w -= k_align
        best_w, best_h = min(best_w, k_max_tile), min(best_h, k_max_tile)
        return best_w, best_h, best_w >= k_min_tile and best_h >= k_min_tile

    auto_w, auto_h, best_ok = best_tile_for(planning_level)
    if not best_ok:
        if not manual and explicit and not plan["level_fits_image"]:
            plan.update(ok=False, use_grid=False,
                        error=f"Level {level} 下没有可用 tile（图像宽/高不足 64 像素）")
            return plan
        if not manual:
            plan.update(use_grid=False, downgraded=True,
                        note="图像尺寸不足以规划 ≥64 像素的 tile，已按单图输出")
            return plan
        auto_w, auto_h = align8(width), align8(height)

    tw, th = auto_w, auto_h
    if manual:
        if tile_w < 0 or tile_w > 8192 or tile_h < 0 or tile_h > 8192:
            plan.update(ok=False, use_grid=False, error="手动 tile 参数越界（0–8192）")
            return plan
        if tile_w:
            tw = align8(tile_w)
            if tw < k_min_tile:
                plan.update(ok=False, use_grid=False,
                            error=f"手动 tile 宽 {tile_w} 对齐后小于 64")
                return plan
        if tile_h:
            th = align8(tile_h)
            if th < k_min_tile:
                plan.update(ok=False, use_grid=False,
                            error=f"手动 tile 高 {tile_h} 对齐后小于 64")
                return plan
        tentative_cols = (width + tw - 1) // tw
        tentative_rows = (height + th - 1) // th
        if tentative_cols == 1 and tentative_rows == 1:
            plan.update(use_grid=False, downgraded=True, tile_width=tw, tile_height=th,
                        cols=1, rows=1, tile_luma_samples=tw * th,
                        note="手动 tile 覆盖整图，降级单图")
            return plan
        if explicit:
            # 自动轴按**规划 Level** 收缩（still 档位压到 5.1 时自动轴也遵守该上界）；
            # 最终 tile 校验仍按用户 Level（编码用它）。
            plan_max_luma = LEVEL_PS[planning_level]
            plan_dim_cap = int((8.0 * plan_max_luma) ** 0.5)
            user_max_luma = LEVEL_PS[level]
            user_dim_cap = int((8.0 * user_max_luma) ** 0.5)
            if tile_h == 0:
                cap = plan_max_luma // tw
                lim = align8(min(auto_h, plan_dim_cap, cap))
                if lim < k_min_tile:
                    plan.update(ok=False, use_grid=False,
                                error=f"手动 tile {tw}×{auto_h} 超过 Level {level} 上限")
                    return plan
                th = lim
            if tile_w == 0:
                cap = plan_max_luma // th
                lim = align8(min(auto_w, plan_dim_cap, cap))
                if lim < k_min_tile:
                    plan.update(ok=False, use_grid=False,
                                error=f"手动 tile {auto_w}×{th} 超过 Level {level} 上限")
                    return plan
                tw = lim
            if not (tw * th <= user_max_luma and tw <= user_dim_cap and th <= user_dim_cap):
                best_w, best_h, best_ok = best_tile_for(level)
                detail = (f"最大可行 tile 为 {best_w}×{best_h}" if best_ok
                          else "连 64×64 都容不下")
                plan.update(ok=False, use_grid=False,
                            error=(f"手动 tile {tw}×{th} 超过 Level {level} 的 MaxLumaPs "
                                   f"{user_max_luma}/单边 {user_dim_cap}；{detail}；三条出路："
                                   f"自动/提高 Level/关闭 grid"))
                return plan
        else:
            plan["note"] = "Level=自动 + 手动 tile（iPhone 接受度未验证）"

    cols = (width + tw - 1) // tw
    rows_ = (height + th - 1) // th
    if cols > 256 or rows_ > 256 or cols * rows_ > 65535:
        plan.update(ok=False, use_grid=False,
                    error=f"网格 {rows_}×{cols} 超过 grid 上限 65535/单边 256")
        return plan
    plan.update(tile_width=tw, tile_height=th, tile_luma_samples=tw * th,
                cols=cols, rows=rows_)
    if cols == 1 and rows_ == 1:
        plan.update(use_grid=False, downgraded=True, note="tile 覆盖整图，降级单图")
    return plan


# ---------------------------------------------------------------------------
# BMFF 解析（grid / 多 item / hvcC / ispe / ipma / iloc / iinf）
# ---------------------------------------------------------------------------
def iter_boxes(buf: bytes, start: int = 0, end: int | None = None, depth: int = 0):
    """产出 (type, payload_start, payload_end, box_start)。只递归容器。"""
    if end is None:
        end = len(buf)
    p = start
    while p + 8 <= end:
        size = struct.unpack(">I", buf[p:p + 4])[0]
        typ = buf[p + 4:p + 8]
        hdr = 8
        if size == 1:
            if p + 16 > end:
                return
            size = struct.unpack(">Q", buf[p + 8:p + 16])[0]
            hdr = 16
        elif size == 0:
            size = end - p
        if size < hdr or p + size > end:
            return
        yield typ, p + hdr, p + size, p
        if typ in (b"meta", b"iprp", b"ipco", b"iinf", b"iref", b"dinf"):
            child = p + hdr
            if typ == b"meta":
                child += 4  # FullBox
            yield from iter_boxes(buf, child, p + size, depth + 1)
        p += size


def parse_hvcc_payload(b: bytes) -> dict:
    return {
        "configVersion": b[0],
        "profile_space": (b[1] >> 6) & 3,
        "tier": (b[1] >> 5) & 1,
        "profile_idc": b[1] & 0x1F,
        "compat": struct.unpack(">I", b[2:6])[0],
        "constraint": b[6:12].hex(),
        "level_idc": b[12],
        "chroma": b[16] & 3,
        "bd_luma": (b[17] & 7) + 8,
        "bd_chroma": (b[18] & 7) + 8,
    }


# ---------------------------------------------------------------------------
# W2-T2（C18）：hvcC 内嵌 SPS 的 PTL / DPB 解析（复刻 src/codecs/heif_capability.cpp）
#   * libheif 生成 hvcC 时会丢 48 位 constraint（实测 constraint 字节恒 0）→ still 语义
#     （intra/one_picture_only）只能从 SPS 读；
#   * NAL 先跳 2 字节头，再**去 00 00 03 防竞争字节**（不做会让位偏移错）。
# ---------------------------------------------------------------------------
class _Bits:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def u(self, n: int) -> int:
        v = 0
        for _ in range(n):
            if self.pos >= len(self.data) * 8:
                raise ValueError("bitstream exhausted")
            byte = self.data[self.pos >> 3]
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def ue(self) -> int:
        zeros = 0
        while self.u(1) == 0:
            zeros += 1
            if zeros > 31:
                raise ValueError("bad ue")
        return (1 << zeros) - 1 + (self.u(zeros) if zeros else 0)


def _rbsp_from_nal(nal: bytes) -> bytes:
    out = bytearray()
    zeros = 0
    for i in range(2, len(nal)):
        b = nal[i]
        if zeros >= 2 and b == 3:
            zeros = 0
            continue
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def _parse_sps_nal(nal: bytes) -> dict | None:
    try:
        b = _Bits(_rbsp_from_nal(nal))
        b.u(4)  # sps_video_parameter_set_id
        max_sub = b.u(3)
        b.u(1)  # sps_temporal_id_nesting_flag
        out = {}
        b.u(2)  # profile_space
        out["tier"] = b.u(1)
        out["idc"] = b.u(5)
        out["compat"] = b.u(32)
        flags = [b.u(1) for _ in range(4)]
        c44 = [b.u(1) for _ in range(44)]
        out["constraint48"] = "".join(map(str, flags + c44))
        out["progressive"] = flags[0]
        out["interlaced"] = flags[1]
        out["non_packed"] = flags[2]
        out["frame_only"] = flags[3]
        if out["idc"] in (4, 5, 9, 10, 11):
            names = ["max_12bit", "max_10bit", "max_8bit", "max_422chroma", "max_420chroma",
                     "max_monochrome", "intra", "one_picture_only", "lower_bit_rate"]
            for i, nm in enumerate(names):
                out[nm] = c44[i]
        else:
            for nm in ("intra", "one_picture_only"):
                out[nm] = -1  # 非 RExt：保留位，不适用（不是 0）
        out["level_idc"] = b.u(8)
        # —— SPS 体：到 DPB 为止 ——
        b.ue()  # sps_seq_parameter_set_id
        out["chroma"] = b.ue()
        if out["chroma"] == 3:
            b.u(1)  # separate_colour_plane_flag
        b.ue()  # width
        b.ue()  # height
        if b.u(1):  # conformance_window_flag
            for _ in range(4):
                b.ue()
        out["bd_luma"] = b.ue() + 8
        out["bd_chroma"] = b.ue() + 8
        b.ue()  # log2_max_pic_order_cnt_lsb_minus4
        present = b.u(1)
        start = 0 if present else max_sub
        for _ in range(start, max_sub + 1):
            out["dpb_minus1"] = b.ue()
            out["num_reorder_pics"] = b.ue()
            out["latency_plus1"] = b.ue()
        return out
    except (ValueError, IndexError):
        return None


def parse_sps_ptl(hvcc: bytes) -> dict | None:
    """从 hvcC payload 取首个 SPS（nal_unit_type=33）的 PTL/DPB；失败返回 None。"""
    if len(hvcc) < 23 or hvcc[0] != 1:
        return None
    pos = 23
    for _ in range(hvcc[22]):
        if pos + 3 > len(hvcc):
            return None
        nal_type = hvcc[pos] & 0x3F
        pos += 1
        count = struct.unpack(">H", hvcc[pos:pos + 2])[0]
        pos += 2
        for _ in range(count):
            if pos + 2 > len(hvcc):
                return None
            length = struct.unpack(">H", hvcc[pos:pos + 2])[0]
            pos += 2
            nal = hvcc[pos:pos + length]
            pos += length
            if nal_type == 33:
                return _parse_sps_nal(nal)
    return None


def parse_infe(payload: bytes) -> dict | None:
    if len(payload) < 8:
        return None
    ver = payload[0]
    q = 4
    item_id = struct.unpack(">H", payload[q:q + 2])[0]
    q += 2
    q += 2  # protection_index
    itype = payload[q:q + 4].decode("latin-1")
    return {"id": item_id, "type": itype, "version": ver}


def parse_iinf(payload: bytes) -> list[dict]:
    ver = payload[0]
    pos = 4
    if ver == 0:
        count = struct.unpack(">H", payload[pos:pos + 2])[0]
        pos += 2
    else:
        count = struct.unpack(">I", payload[pos:pos + 4])[0]
        pos += 4
    items = []
    while pos + 8 <= len(payload):
        size = struct.unpack(">I", payload[pos:pos + 4])[0]
        typ = payload[pos + 4:pos + 8]
        if size < 8 or pos + size > len(payload):
            break
        if typ == b"infe":
            it = parse_infe(payload[pos + 8:pos + size])
            if it:
                items.append(it)
        pos += size
    return items


def parse_iloc(payload: bytes) -> dict:
    ver = payload[0]
    pos = 4
    off_sz = payload[pos] >> 4
    len_sz = payload[pos] & 0xF
    base_sz = payload[pos + 1] >> 4
    idx_sz = payload[pos + 1] & 0xF if ver in (1, 2) else 0
    pos += 2
    if ver < 2:
        count = struct.unpack(">H", payload[pos:pos + 2])[0]
        pos += 2
    else:
        count = struct.unpack(">I", payload[pos:pos + 4])[0]
        pos += 4

    def read_n(n):
        nonlocal pos
        v = 0
        for _ in range(n):
            v = (v << 8) | payload[pos]
            pos += 1
        return v

    out = {}
    for _ in range(count):
        item_id = read_n(2) if ver < 2 else read_n(4)
        construction = 0
        if ver in (1, 2):
            cm = struct.unpack(">H", payload[pos:pos + 2])[0]
            construction = cm & 0xF
            pos += 2
        pos += 2  # data_reference_index
        base = read_n(base_sz)
        n_ext = struct.unpack(">H", payload[pos:pos + 2])[0]
        pos += 2
        exts = []
        for _ in range(n_ext):
            if ver in (1, 2) and idx_sz:
                read_n(idx_sz)
            off = read_n(off_sz)
            ln = read_n(len_sz)
            exts.append((off, ln))
        out[item_id] = {"construction": construction, "base": base, "extents": exts}
    return out


def parse_ipma(payload: bytes) -> dict:
    # ISO/IEC 14496-12 ItemPropertyAssociationBox：entry_count 恒为 32 位；
    # version 0 的 item_ID 是 16 位，version >= 1 是 32 位；flags&1 → 15 位 property_index。
    ver = payload[0]
    flags = int.from_bytes(payload[1:4], "big")
    pos = 4
    count = struct.unpack(">I", payload[pos:pos + 4])[0]
    pos += 4
    out = {}
    for _ in range(count):
        if ver >= 1:
            item_id = struct.unpack(">I", payload[pos:pos + 4])[0]
            pos += 4
        else:
            item_id = struct.unpack(">H", payload[pos:pos + 2])[0]
            pos += 2
        n = payload[pos]
        pos += 1
        idxs = []
        for _ in range(n):
            if flags & 1:
                idxs.append(struct.unpack(">H", payload[pos:pos + 2])[0] & 0x7FFF)
                pos += 2
            else:
                idxs.append(payload[pos] & 0x7F)
                pos += 1
        out[item_id] = idxs
    return out


def parse_iref(payload: bytes) -> list[dict]:
    pos = 4
    refs = []
    while pos + 8 <= len(payload):
        size = struct.unpack(">I", payload[pos:pos + 4])[0]
        typ = payload[pos + 4:pos + 8].decode("latin-1")
        if size < 8 or pos + size > len(payload):
            break
        body = payload[pos + 8:pos + size]
        from_id = struct.unpack(">H", body[0:2])[0]
        n = struct.unpack(">H", body[2:4])[0]
        to = [struct.unpack(">H", body[4 + 2 * i:6 + 2 * i])[0] for i in range(n)]
        refs.append({"type": typ, "from": from_id, "to": to})
        pos += size
    return refs


@dataclass
class HeifStructure:
    ok: bool = False
    error: str = ""
    items: list = field(default_factory=list)
    grids: list = field(default_factory=list)  # [{id, rows, cols, w, h}]
    tiles: list = field(default_factory=list)  # [{id, ispe, hvcc}]
    hvcc_all: list = field(default_factory=list)
    sps_all: list = field(default_factory=list)  # W2-T2：每 hvcC 的 SPS PTL/DPB
    ispe_all: list = field(default_factory=list)
    iref: list = field(default_factory=list)
    ftyp: dict = field(default_factory=dict)

    def to_json(self) -> dict:
        return {
            "item_count": len(self.items),
            "grids": self.grids,
            "tile_count": len(self.tiles),
            "tile_ispe": sorted({tuple(t["ispe"]) for t in self.tiles if t.get("ispe")}),
            # W2-T1：每 tile hvcC 的 level/profile（结构断言的落盘证据）。
            "tile_levels": sorted({(t.get("hvcc") or {}).get("level_idc")
                                   for t in self.tiles if t.get("hvcc")}),
            "tile_profiles": sorted({(t.get("hvcc") or {}).get("profile_idc")
                                     for t in self.tiles if t.get("hvcc")}),
            "hvcc": self.hvcc_all,
            "sps": self.sps_all,
            "ispe_all": self.ispe_all,
            "iref_count": len(self.iref),
            "ftyp": self.ftyp,
            "error": self.error,
        }


def parse_heif(path: Path) -> HeifStructure:
    st = HeifStructure()
    try:
        buf = path.read_bytes()
    except Exception as e:  # pragma: no cover - 取证路径
        st.error = f"read failed: {e}"
        return st

    items, iloc, ipma, ipco_children, idat = [], {}, {}, [], None
    iref = []
    for typ, s, e, _bs in iter_boxes(buf):
        if typ == b"ftyp":
            if e - s >= 8:
                major = buf[s:s + 4].decode("latin-1")
                brands = [buf[i:i + 4].decode("latin-1") for i in range(s + 8, e - 3, 4)]
                st.ftyp = {"major": major, "compatible": brands}
        elif typ == b"iinf":
            items = parse_iinf(buf[s:e])
        elif typ == b"iloc":
            iloc = parse_iloc(buf[s:e])
        elif typ == b"ipma":
            ipma = parse_ipma(buf[s:e])
        elif typ == b"idat":
            idat = s
        elif typ == b"iref":
            iref = parse_iref(buf[s:e])
        elif typ == b"ipco":
            # 直接子 box = 属性
            for ptyp, ps, pe, _ in iter_boxes(buf, s, e):
                prop = {"type": ptyp.decode("latin-1"), "start": ps, "end": pe}
                if ptyp == b"ispe" and pe - ps >= 12:
                    prop["w"], prop["h"] = struct.unpack(">II", buf[ps + 4:ps + 12])
                elif ptyp == b"hvcC":
                    prop["hvcc"] = parse_hvcc_payload(buf[ps:pe])
                    prop["sps"] = parse_sps_ptl(buf[ps:pe])  # W2-T2：SPS PTL/DPB
                ipco_children.append(prop)

    st.items = items
    st.iref = iref
    st.ispe_all = [[p["w"], p["h"]] for p in ipco_children if p["type"] == "ispe"]

    def item_payload(item_id: int) -> bytes | None:
        loc = iloc.get(item_id)
        if not loc or not loc["extents"]:
            return None
        off, ln = loc["extents"][0]
        base = loc["base"]
        if loc["construction"] == 1 and idat is not None:
            return buf[idat + base + off:idat + base + off + ln]
        return buf[base + off:base + off + ln]

    hvcc_index_to_idx = {}
    for i, p in enumerate(ipco_children):
        if p["type"] == "hvcC":
            hvcc_index_to_idx[i] = len(st.hvcc_all)
            st.hvcc_all.append(p["hvcc"])
            st.sps_all.append(p.get("sps"))  # W2-T2：同一 hvcC 的 SPS PTL/DPB

    for it in items:
        assoc = ipma.get(it["id"], [])
        # ipma 的 property_index 是 **1 基**（ISO/IEC 14496-12）；取首个 ispe 与首个 hvcC。
        ispe = None
        hvcc = None
        for a in assoc:
            if a < 1 or a > len(ipco_children):
                continue
            p = ipco_children[a - 1]
            if p["type"] == "ispe" and ispe is None:
                ispe = [p["w"], p["h"]]
            elif p["type"] == "hvcC" and hvcc is None:
                hvcc = p["hvcc"]
        if it["type"] == "grid":
            data = item_payload(it["id"])
            if data and len(data) >= 8:
                # ISO/IEC 23008-12 ImageGrid：version(1) flags(1) rows-1(1) cols-1(1)，
                # flags&1 → 32 位 output_width/height，否则 16 位（Apple T7 实测 flags=0）。
                flags = data[1]
                if flags & 1 and len(data) >= 12:
                    w, h = struct.unpack(">II", data[4:12])
                else:
                    w, h = struct.unpack(">HH", data[4:8])
                st.grids.append({
                    "id": it["id"], "version": data[0], "flags": flags,
                    "rows": data[2] + 1, "cols": data[3] + 1,
                    "w": w, "h": h,
                })
            else:
                st.grids.append({"id": it["id"], "error": "grid payload missing/short"})
        elif it["type"] == "hvc1":
            st.tiles.append({"id": it["id"], "ispe": ispe, "hvcc": hvcc})
    st.ok = True
    return st


# ---------------------------------------------------------------------------
# Case 模型与枚举
# ---------------------------------------------------------------------------
@dataclass
class Case:
    case_id: str
    set_name: str
    src_kind: str  # small | t1
    src: Path
    width: int
    height: int
    bitdepth: int
    chroma: str
    level: int
    tier: int
    profile: int | None  # None = 不注入 __heif_profile（baseline）
    grid: int | None     # None = 不注入 grid 键（baseline）
    tile_w: int | None = None   # None = 不注入 heif_tile_w
    tile_h: int | None = None
    grid_key: str = "__heif_grid"  # W2-T1 新键 heif_grid；旧 M9 用例保留别名兼容验证
    tune: str | None = None      # W2-T3（C20）：tune 值（None = 不注入，走插件默认）
    expected_legal: bool = True
    expected_reason: str = ""
    expected_profile_idc: int | None = None
    expected_level_idc: int | None = None
    expected_tier: int = 0
    expected_grid: dict | None = None
    expected_error_kind: str = ""  # level | profile | param | tile | grid | still | any

    @property
    def luma(self) -> int:
        return self.width * self.height

    def repro(self, exe: Path) -> str:
        parts = [
            "python", "tools/env.py", "run", "--", str(exe),
            "--dev", str(self.src), "--out", f"build/matrix/tmp/{self.case_id}",
            "--format", "heif", "--bitdepth", str(self.bitdepth), "--color", "keep",
            "--conflict", "overwrite",
            "--param", f"chroma={self.chroma}", "--param", "quality=80",
            "--param", f"x265:level-idc={self.level}",
            "--param", f"x265:high-tier={self.tier}",
        ]
        if self.profile is not None:
            parts += ["--param", f"__heif_profile={self.profile}"]
        if self.grid is not None:
            parts += ["--param", f"{self.grid_key}={self.grid}"]
        if self.tile_w is not None:
            parts += ["--param", f"heif_tile_w={self.tile_w}"]
        if self.tile_h is not None:
            parts += ["--param", f"heif_tile_h={self.tile_h}"]
        if self.tune is not None:
            parts += ["--param", f"tune={self.tune}"]
        parts += ["--workers", str(WORKERS)]
        # Windows PowerShell 友好：路径含空格时加引号由调用者负责，这里保留原样。
        return " ".join(parts)


def profile_signal_legal(profile: int, bitdepth: int, chroma: str) -> tuple[bool, str]:
    """HEIF（非 AVIF）下档位是否与 (位深,色度) 兼容。逐字对齐 heif_profile_compatible_for。"""
    if profile == 0:
        return True, "auto"
    name, sig = PROFILES[profile]
    if sig is None:
        return True, "auto"
    if sig.get("avif_only"):
        return False, f"{name} 是 AVIF 专用档位，不能用于 HEIF"
    if profile == 3:  # Main12 兼容别名：12 位、色度随输入
        if bitdepth != 12:
            return False, "Main12 要求 12 位"
        return True, "main12-alias"
    if sig["bd"] != bitdepth:
        return False, f"{name} 要求 {sig['bd']} 位（当前 {bitdepth}）"
    if sig["chroma"] is not None and CHROMA_IDX[sig["chroma"]] != CHROMA_IDX[chroma]:
        return False, f"{name} 要求 4:{sig['chroma'][1:]}（当前 {chroma}）"
    return True, name


def expected_tier_flag(level: int, tier: int, luma: int) -> int:
    # x265：High tier 只在 Level ≥ 4.0 且码率/CPB 落在 High 档时置位；实测 level<40 → 0。
    eff = level if level else auto_level(luma)
    return 1 if (tier == 1 and eff >= 40) else 0


def make_case(set_name, src_kind, src, width, height, bd, chroma, level, tier,
              profile=None, grid=None, tile_w=None, tile_h=None,
              grid_key="__heif_grid", tune=None) -> Case:
    cid_bits = [src_kind, f"b{bd}", chroma, f"l{level}", f"t{tier}"]
    if profile is not None:
        cid_bits.append(f"p{profile}")
    if grid is not None:
        cid_bits.append(f"g{grid}" + ("n" if grid_key == "heif_grid" else ""))
    if tile_w is not None or tile_h is not None:
        cid_bits.append(f"m{tile_w or 0}x{tile_h or 0}")
    if tune is not None:
        cid_bits.append(f"tn{tune}")
    cid = "-".join(cid_bits)

    legal = True
    reasons = []
    profile_bad = False
    level_bad = False
    tile_bad = False
    grid_bad = False
    still_bad = False
    tune_bad = False
    # W2-T3（C20）：置灰 tune 值在编码器参数循环里**先于**档位/Level 失败（同一事实表文案）。
    if tune is not None and tune in HEIF_TUNE_DISABLED:
        legal = False
        tune_bad = True
        reasons.append(f"tune={tune} 为置灰值（不可用）：编码必须干净失败并点名原因")
    # profile 兼容
    if profile is not None:
        pok, preason = profile_signal_legal(profile, bd, chroma)
        if not pok:
            legal = False
            profile_bad = True
            reasons.append(preason)
        # W2-T2（C18）：16 = Main10Still 实测不可用（PTL 无 still 位 + SPS DPB 4/2）→
        # 兼容门控放行（信号对得上），但编码必须在**编码后 SPS 断言**里干净失败。
        if PROFILES[profile][1] and PROFILES[profile][1].get("unavailable"):
            legal = False
            still_bad = True
            reasons.append("Main10Still 实测不可用：非 RExt 的 PTL 无 still 位 + SPS DPB 4/2"
                           "（要求 0/0）→ 编码后断言必须失败")
    # grid / tile / level（W2-T1：手动 tile + 新键 heif_grid）
    grid_plan = None
    if grid is not None or tile_w is not None or tile_h is not None:
        grid_mode = 2 if grid is None else grid
        if grid_key == "heif_grid":
            # 新键用户面编号 0=关闭 / 1=自动 / 2=总是 → 内部模式 0/2/1。
            grid_mode = {0: 0, 1: 2, 2: 1}.get(grid_mode, grid_mode)
        grid_plan = plan_grid(grid_mode, level, width, height, chroma,
                              tile_w or 0, tile_h or 0, profile)
        if not grid_plan["ok"]:
            legal = False
            err = grid_plan["error"]
            if "手动 tile" in err or "tile" in err.lower() or "MaxLumaPs" in err \
                    or "65535" in err or "网格" in err:
                tile_bad = True
            elif "关闭" in err:
                grid_bad = True
            else:
                level_bad = True
            reasons.append(err)
        elif grid_plan["use_grid"] and level != 0:
            if grid_plan["tile_luma_samples"] > LEVEL_PS.get(level, 0):
                legal = False
                level_bad = True
                reasons.append("grid tile 仍超过 Level 上限")
    elif level == 0:
        pass
    elif not level_fits(level, width, height):
        legal = False
        level_bad = True
        reasons.append(f"整图 {width}x{height}={width * height} 超过 Level {level} 上限 {LEVEL_PS.get(level)}")

    exp_profile_idc = None
    if profile is not None:
        _, sig = PROFILES[profile]
        exp_profile_idc = sig["idc"] if sig and "idc" in sig else None
    if profile in (None, 0):
        # Auto = 插件原生信号（heif_derive_signal_profile）：8 位 4:2:0 静图 → idc3；
        # 10/12 位 4:2:0 与 4:2:2/4:4:4 → idc4（RExt）。
        exp_profile_idc = 3 if (bd == 8 and chroma == "420") else 4

    use_grid = bool(grid_plan and grid_plan["use_grid"])
    effective_luma = grid_plan["tile_luma_samples"] if use_grid else width * height
    exp_level_idc = level_idc(level) if level else level_idc(auto_level(effective_luma))

    expected_grid = None
    if use_grid:
        tile_level = level_idc(level) if level else level_idc(auto_level(effective_luma))
        expected_grid = {
            "cols": grid_plan["cols"],
            "rows": grid_plan["rows"],
            "tile_width": grid_plan["tile_width"],
            "tile_height": grid_plan["tile_height"],
            "tile_level_idc": tile_level,
        }

    if not legal:
        # enc_heif 先做档位反查、再做 Level/grid 预检；多者非法时错误文案按此顺序。
        # W2-T2：16 号（Main10Still）是「档位信号兼容但实测不可用」→ 失败点在编码后 SPS 断言。
        # W2-T3：置灰 tune 值在参数循环最先失败（tune 文案优先）。
        err_kind = ("tune" if tune_bad else "profile" if profile_bad else "still" if still_bad
                    else "grid" if grid_bad else "tile" if tile_bad else "level" if level_bad
                    else "")
    else:
        err_kind = ""
    if profile is None and grid is None and tile_w is None and tile_h is None and tune is None:
        # baseline 旧二进制没有中文预校验（只有 libheif/x265 泛化错误），不强制点名原因。
        err_kind = ""

    return Case(
        case_id=cid, set_name=set_name, src_kind=src_kind, src=src, width=width, height=height,
        bitdepth=bd, chroma=chroma, level=level, tier=tier, profile=profile, grid=grid,
        tile_w=tile_w, tile_h=tile_h, grid_key=grid_key, tune=tune,
        expected_legal=legal, expected_reason="; ".join(reasons) if reasons else "legal",
        expected_profile_idc=exp_profile_idc, expected_level_idc=exp_level_idc,
        expected_tier=expected_tier_flag(level, tier, effective_luma),
        expected_grid=expected_grid,
        expected_error_kind=err_kind,
    )


def small_cases(set_name: str, profiles: list[int | None], grids: list[int | None],
                levels: list[int] = LEVELS, tiers=(0, 1)) -> list[Case]:
    out = []
    for bd in BITDEPTHS:
        for ch in CHROMAS:
            for lv in levels:
                for tier in tiers:
                    for prof in profiles:
                        for g in grids:
                            out.append(make_case(set_name, "small", SMALL_TIF, 1024, 576,
                                                 bd, ch, lv, tier, prof, g))
    return out


def t1_cases(set_name: str, profiles: list[int | None], grids: list[int | None],
             levels: list[int], chromas=("420", "444"), tiers=(0,)) -> list[Case]:
    out = []
    for bd in BITDEPTHS:
        for ch in chromas:
            for lv in levels:
                for tier in tiers:
                    for prof in profiles:
                        for g in grids:
                            out.append(make_case(set_name, "t1", T1_TIF, 3902, 5854,
                                                 bd, ch, lv, tier, prof, g))
    return out


def build_case_sets(phase: str, want_shrink: bool, t1_extra: bool) -> dict[str, list[Case]]:
    sets: dict[str, list[Case]] = {}
    if phase == "baseline":
        # 基线：dev harness 尚不接受 "__" 保留键 -> 不注入 profile/grid。
        sets["baseline-small"] = small_cases("baseline-small", [None], [None])
        t1_levels = [0, 31, 52, 60, 62, 63]
        if t1_extra:
            t1_levels += [70, 71, 72]
        sets["baseline-t1"] = t1_cases("baseline-t1", [None], [None], t1_levels, tiers=(0,))
    else:
        # final：完整矩阵。
        # 1) Auto 全量（9 × 18 × 2 = 324），并显式注入 __heif_profile=0 / __heif_grid=0
        sets["final-auto"] = small_cases("final-auto", [0], [0])
        # 2) 每个真实信号 × 全部 level × 2 tier（Auto 不重复；Main12 覆盖 3 种色度）
        sig = []
        for prof in HEVC_SIGNAL_PROFILES:
            _, s = PROFILES[prof]
            bds = [s["bd"]]
            chs = [s["chroma"]] if s["chroma"] else list(CHROMAS)
            for bd in bds:
                for ch in chs:
                    for lv in LEVELS:
                        for tier in (0, 1):
                            sig.append(make_case("final-signals", "small", SMALL_TIF,
                                                 1024, 576, bd, ch, lv, tier, prof, 0))
        sets["final-signals"] = sig
        # 3) 档位兼容性矩阵：16 档 × 9 (bd,chroma) × level 0 × tier 0（含应当拒绝的组合）
        compat = []
        for prof in PROFILES:
            for bd in BITDEPTHS:
                for ch in CHROMAS:
                    compat.append(make_case("final-profile-compat", "small", SMALL_TIF,
                                            1024, 576, bd, ch, 0, 0, prof, 0))
        sets["final-profile-compat"] = compat
        # 3b) W2-T2（C18）：still 变体 16..21 —— 档位×色度/位深 + SPS still 断言。
        #     * 16（Main10Still）实测不可用（PTL 无 still 位 + SPS DPB 4/2）→ 期望干净失败；
        #     * 17..21 原生 RExt still → 必须成功且 SPS 写出 intra=1/one_picture_only=1；
        #     * 14（Rext444_10）与 20（Main444_10Still）是**同一真实信号** → 对照用例。
        still_cases = [
            (16, 10, "420", 0),
            (17, 10, "422", 0),
            (18, 12, "422", 0),
            (19, 8, "444", 0),
            (20, 10, "444", 0),
            (21, 12, "444", 0),
            (19, 8, "444", 31),   # 显式 Level 3.1
            (20, 10, "444", 40),  # 显式 Level 4.0
            (14, 10, "444", 0),   # 对照：既有 Rext444_10（同一信号）
        ]
        sets["final-still"] = [
            make_case("final-still", "small", SMALL_TIF, 1024, 576, bd, ch, lv, 0, prof, 0)
            for prof, bd, ch, lv in still_cases
        ]
        # 4) grid / 规划上界（still 互操作 L5.1）/ T1 真实大图
        grid_cases = [
            # 任务要求的主证据：T1 + L3.1 + grid=2 → 6×5 tile (768×1280 = M31)，每 tile L3.1
            ("t1", 10, "420", 31, 0, 2, None),   # Auto（idc4）
            ("t1", 10, "420", 31, 0, 2, 2),      # Main10（idc2，Apple T7 同配置）
            ("t1", 10, "420", 31, 0, 2, 8),      # Rext420_10（idc4 对照）
            ("t1", 8, "420", 31, 0, 2, None),    # Auto（idc3）
            ("t1", 10, "420", 31, 0, 1, 2),      # 总是 grid（显式 L3.1 → 768×1280 6×5）
            ("t1", 10, "420", 0, 0, 2, None),    # 自动 Level + auto grid → 不分块
            ("t1", 10, "420", 0, 0, 1, None),    # 自动 Level + 总是 grid（规划上界 L5.1 → 2176×4096 2×2）
            ("t1", 10, "420", 31, 0, 0, None),   # 关 grid + 非法 Level → 必须干净失败
            ("t1", 10, "420", 52, 0, 0, None),   # 旧误导后缀复现点（新码应为中文 Level 错误）
            ("t1", 8, "420", 52, 0, 0, None),    # 8 位对照（旧码无后缀）
            ("t1", 10, "420", 31, 0, 2, 1),      # Profile 不兼容（Main 要求 8 位）→ 干净失败
            ("t1", 12, "444", 31, 0, 2, 15),     # Rext444_12 12 位大图 grid
            ("small", 8, "420", 30, 0, 2, None), # 小图 tile 收缩（L3.0 → 960×576 2×1）
            ("small", 10, "420", 30, 0, 2, 2),   # 小图 Main10 + 收缩 tile
            ("small", 8, "420", 0, 0, 1, None),  # 小图总是 grid（1×1 降级单图）
            ("small", 8, "420", 10, 0, 2, None), # 小图强收缩（L1.0 → 256×144 4×4，16 块）
            ("t1", 10, "444", 31, 0, 2, None),   # 444 grid
            ("t1", 8, "422", 31, 0, 2, None),    # 422 grid
            ("t1", 8, "420", 31, 0, 2, 4),       # MainStill grid（idc3）
            ("t1", 12, "420", 31, 0, 2, 9),      # Rext420_12 grid（idc4）
        ]
        gc = []
        for kind, bd, ch, lv, tier, gm, prof in grid_cases:
            if kind == "t1":
                gc.append(make_case("final-grid", "t1", T1_TIF, 3902, 5854, bd, ch, lv, tier, prof, gm))
            else:
                gc.append(make_case("final-grid", "small", SMALL_TIF, 1024, 576, bd, ch, lv, tier, prof, gm))
        if want_shrink:
            gc.append(make_case("final-grid", "t1", T1_TIF, 3902, 5854, 10, "420", 20, 0, None, 2))
        sets["final-grid"] = gc
        # 4b) W2-T1 用户面新键 heif_grid / heif_tile_w / heif_tile_h（C16/C17）：
        #     自动语义 / 总是 Apple / 手动合法/非法 / grid 关闭 / Level 收缩 / 8 对齐。
        gt_cases = [
            ("small", 8, "420", 0, 1, None, None, None, "new-auto + level auto → 单图"),
            ("t1", 10, "420", 31, 1, None, None, 2, "new-auto + L3.1 → 6×5（768×1280=M31）/ 每 tile L3.1"),
            ("t1", 10, "420", 0, 2, None, None, None, "new-always + level auto → 规划上界 L5.1（2×2）"),
            ("small", 8, "420", 0, 2, None, None, None, "new-always 1×1 → 降级单图"),
            ("small", 8, "420", 31, 2, 512, 288, None, "manual 512×288 合法 → 2×2"),
            ("small", 8, "420", 0, 1, 512, 288, None, "manual + level auto → note"),
            ("t1", 10, "420", 31, 2, 1024, 1024, None, "manual 1024×1024 超 MaxLumaPs → 中文预检失败"),
            ("small", 8, "420", 31, 2, 32, 256, None, "manual <64 → 中文预检失败"),
            ("small", 8, "420", 30, 0, None, None, None, "grid 关闭 + 超 Level → 中文预检失败"),
            ("small", 8, "420", 30, 1, None, None, None, "new-auto + L3.0 → 960×576 2×1"),
            ("small", 8, "420", 31, 2, 1004, 300, None, "manual 1004→1000 / 300→296 → 2×2"),
        ]
        gt = []
        for kind, bd, ch, lv, gm, tw, th, prof, _label in gt_cases:
            src = T1_TIF if kind == "t1" else SMALL_TIF
            sw, sh = (3902, 5854) if kind == "t1" else (1024, 576)
            gt.append(make_case("final-gridtile", kind, src, sw, sh, bd, ch, lv, 0, prof, gm,
                                tw, th, grid_key="heif_grid"))
        sets["final-gridtile"] = gt
        # 4c) W2-T3（C20）：tune 全量 —— 4 原生 + animation（引擎注入近似仿真）+ 2 置灰。
        #     合法值：编码成功且产物结构可解（hvcC/SPS 断言沿用通用判据）；
        #     置灰值：必须干净失败且错误点名「不可用」（同一事实表文案）。
        tune_cases: list[Case] = []
        for bd, ch, prof in ((8, "420", 0), (10, "420", 2), (10, "444", 20), (12, "422", 18)):
            for tune in HEIF_TUNE_LEGAL:
                tune_cases.append(make_case("final-tune", "small", SMALL_TIF, 1024, 576,
                                            bd, ch, 0, 0, prof, 0, tune=tune))
        for tune in HEIF_TUNE_DISABLED:
            tune_cases.append(make_case("final-tune", "small", SMALL_TIF, 1024, 576,
                                        8, "420", 0, 0, 0, 0, tune=tune))
        sets["final-tune"] = tune_cases
        # 5) T1 单图（无 grid）大图基线：level 0 auto（6.0）与高 Level；含 Main10 单图
        t1s = []
        for bd in BITDEPTHS:
            for ch in ("420", "444"):
                t1s.append(make_case("final-t1-single", "t1", T1_TIF, 3902, 5854, bd, ch, 0, 0, 0, 0))
        t1s += [
            make_case("final-t1-single", "t1", T1_TIF, 3902, 5854, 10, "420", 0, 0, 2, 0),
            make_case("final-t1-single", "t1", T1_TIF, 3902, 5854, 8, "420", 60, 0, 0, 0),
            make_case("final-t1-single", "t1", T1_TIF, 3902, 5854, 8, "420", 63, 0, 0, 0),
        ]
        sets["final-t1-single"] = t1s
    return sets


def selftest_cases() -> list[Case]:
    """只验证解析器：不编码，直接解析已有产物。"""
    return [
        make_case("selftest", "small", SMALL_TIF, 1024, 576, 8, "420", 0, 0, None, None),
        make_case("selftest", "small", SMALL_TIF, 1024, 576, 10, "444", 0, 0, None, None),
        make_case("selftest", "t1", T1_TIF, 3902, 5854, 10, "420", 31, 0, 2, 2),
    ]


# ---------------------------------------------------------------------------
# 编码执行 + 证据 record
# ---------------------------------------------------------------------------
def sha256_prefix(path: Path, n: int = 16) -> str:
    try:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()[:n]
    except Exception:
        return "?"


def extract_error(stdout: str, stderr: str) -> str:
    lines = []
    for ln in (stdout + "\n" + stderr).splitlines():
        s = ln.strip()
        if not s:
            continue
        if s.startswith("error:") or " error: " in s or "错误" in s or "未收录参数" in s \
                or "失败" in s or "不支持" in s or "exceeds" in s or "could not be opened" in s \
                or "Invalid image size" in s or "编码档位" in s or "超过 HEVC Level" in s:
            lines.append(s)
    if not lines:
        # 退回：stderr 首个非空行
        for ln in stderr.splitlines():
            if ln.strip():
                lines.append(ln.strip())
                break
    return " | ".join(lines[:3])[:1200]


def tail(text: str, n: int = 14, max_chars: int = 2400) -> str:
    lines = [ln for ln in text.splitlines() if ln.strip()]
    return "\n".join(lines[-n:])[-max_chars:]


def run_encode(case: Case, exe: Path, timeout: int, outdir: Path) -> dict:
    cmd = [
        sys.executable, str(ENV_PY), "run", "--", str(exe),
        "--dev", str(case.src), "--out", str(outdir),
        "--format", "heif", "--bitdepth", str(case.bitdepth), "--color", "keep",
        "--conflict", "overwrite",
        "--param", f"chroma={case.chroma}", "--param", "quality=80",
        "--param", f"x265:level-idc={case.level}",
        "--param", f"x265:high-tier={case.tier}",
    ]
    if case.profile is not None:
        cmd += ["--param", f"__heif_profile={case.profile}"]
    if case.grid is not None:
        cmd += ["--param", f"{case.grid_key}={case.grid}"]
    if case.tile_w is not None:
        cmd += ["--param", f"heif_tile_w={case.tile_w}"]
    if case.tile_h is not None:
        cmd += ["--param", f"heif_tile_h={case.tile_h}"]
    if case.tune is not None:
        cmd += ["--param", f"tune={case.tune}"]
    cmd += ["--workers", str(WORKERS)]

    t0 = time.time()
    timed_out = False
    try:
        p = subprocess.run(cmd, cwd=str(REPO), capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=timeout)
        rc, out, err = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        timed_out = True
        rc = -999
        out = (e.stdout or b"").decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        err = (e.stderr or b"").decode("utf-8", "replace") if isinstance(e.stderr, bytes) else (e.stderr or "")
        err += f"\n[matrix] timeout after {timeout}s"
    elapsed = time.time() - t0
    return {"cmd": cmd, "exit_code": rc, "timed_out": timed_out, "stdout": out,
            "stderr": err, "elapsed_s": round(elapsed, 3)}


def find_output(outdir: Path) -> Path | None:
    if not outdir.exists():
        return None
    hits = sorted(outdir.rglob("*.heic"))
    return hits[0] if hits else None


def decode_check(path: Path) -> dict:
    if not IINFO.exists():
        return {"status": "skip", "text": "iinfo not found"}
    try:
        p = subprocess.run([str(IINFO), "-v", str(path)], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=180)
    except subprocess.TimeoutExpired:
        return {"status": "fail", "text": "iinfo timeout"}
    txt = (p.stdout or "") + (p.stderr or "")
    m = re.search(r":\s*(\d+)\s*x\s*(\d+)\s*,", txt)
    return {"status": "ok" if p.returncode == 0 and m else "fail",
            "width": int(m.group(1)) if m else None,
            "height": int(m.group(2)) if m else None,
            "exit_code": p.returncode, "text": tail(txt, 6, 1200)}


def hvcc_probe(path: Path) -> dict:
    """复用 build/analysis/hvcc.py（任务要求）。返回解析后的首个 hvcC + 全部。

    0.4.2 / C7（构建零临时依赖）：hvcc.py 位于 build/ 证据目录，可能已按用户要求清空。
    缺失 -> **显式跳过**（missing=True，由 evaluate 记为 skip 并打印原因），不静默失败、也不
    把「工具缺失」伪装成「码流 hvcC 违规」。
    """
    if not HVCC_PY.exists():
        return {"ok": False, "missing": True, "hvcc": [], "ispe": [],
                "raw_error": f"hvcc.py missing: {HVCC_PY}（build/ 已按 C7 清空；"
                             "可从历史 M9 证据恢复，或跳过 hvcC 断言重跑）"}
    try:
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"  # hvcc.py 的 print 在 GBK 控制台会 UnicodeEncodeError
        p = subprocess.run([sys.executable, str(HVCC_PY), str(path)], capture_output=True,
                           text=True, encoding="utf-8", errors="replace", timeout=120, env=env)
        data = json.loads(p.stdout.strip().splitlines()[-1])
        return {"ok": True, "hvcc": data.get("hvcC", []), "ispe": data.get("ispe", []),
                "raw_error": ""}
    except Exception as e:
        return {"ok": False, "hvcc": [], "ispe": [], "raw_error": str(e)}


def evaluate(case: Case, run: dict, outdir: Path, out_file: Path | None,
             hv: dict, st: HeifStructure, dec: dict) -> tuple[str, list[str], dict]:
    """返回 (verdict, mismatches, facts)。"""
    mism: list[str] = []
    facts: dict = {"hvcC": hv.get("hvcc", [{}])[0] if hv.get("hvcc") else None,
                   "grid": st.to_json()}
    # 0.4.2 / C7：hvcC 探针依赖 build/analysis/hvcc.py（build/ 已按 C7 清空）。
    # 工具缺失 -> 该例显式 skip 并打印原因（不静默，也不把「工具缺失」计成「码流违规」）。
    if hv.get("missing"):
        facts["skip_reason"] = hv.get("raw_error", "hvcc.py missing")
        return "skip", [str(facts["skip_reason"])], facts
    rc = run["exit_code"]
    out_exists = out_file is not None and out_file.exists() and out_file.stat().st_size > 0
    facts["out_exists"] = out_exists
    facts["out_bytes"] = out_file.stat().st_size if out_exists else 0

    if not case.expected_legal:
        if rc == 0 and out_exists:
            return "unexpected", ["预期非法组合却编码成功"], facts
        if rc == 0 and not out_exists:
            return "unexpected", ["退出码 0 但没有产物"], facts
        if run["timed_out"]:
            return "fail", ["预期失败但超时（未能在时限内给出干净失败）"], facts
        if out_exists:
            return "fail", ["预期失败但留下了产物"], facts
        err = extract_error(run["stdout"], run["stderr"])
        facts["error_text"] = err
        kind = case.expected_error_kind
        if kind == "level" and "Level" not in err and "level" not in err and "亮度样本" not in err:
            # 基线旧二进制只给 libheif 泛化错误；final 应命中中文预校验。
            mism.append(f"失败信息未点名 Level：{err[:300]}")
            return "fail", mism, facts
        if kind == "profile" and "档位" not in err and "profile" not in err.lower():
            mism.append(f"失败信息未点名档位：{err[:300]}")
            return "fail", mism, facts
        if kind == "tile" and not any(k in err for k in ("tile", "Tile", "MaxLumaPs", "最大可行")):
            mism.append(f"失败信息未点名手动 tile：{err[:300]}")
            return "fail", mism, facts
        if kind == "grid" and "关闭" not in err and "grid" not in err.lower():
            mism.append(f"失败信息未点名 grid 关闭：{err[:300]}")
            return "fail", mism, facts
        if kind == "still" and not any(k in err for k in ("DPB", "still", "不可用",
                                                          "max_dec_pic_buffering")):
            mism.append(f"失败信息未点名 still/DPB 约束：{err[:300]}")
            return "fail", mism, facts
        if kind == "tune" and "不可用" not in err and "调优" not in err:
            mism.append(f"失败信息未点明 tune 不可用原因：{err[:300]}")
            return "fail", mism, facts
        return "expected-fail", [], facts

    # 预期合法：必须成功 + 产物结构 + 解码
    if rc != 0:
        err = extract_error(run["stdout"], run["stderr"])
        facts["error_text"] = err
        return "fail", [f"合法组合编码失败 exit={rc}: {err[:400]}"], facts
    if not out_exists:
        return "fail", ["exit=0 但没有 HEIC 产物"], facts

    if hv.get("hvcc"):
        h = hv["hvcc"][0]
        facts["hvcC"] = h
        if case.expected_profile_idc is not None and h["profile_idc"] != case.expected_profile_idc:
            mism.append(f"profile_idc {h['profile_idc']} != {case.expected_profile_idc}")
        exp_chroma = CHROMA_IDX[case.chroma]
        if h["chroma"] != exp_chroma:
            mism.append(f"chroma {h['chroma']} != {exp_chroma}")
        if h["bd_luma"] != case.bitdepth:
            mism.append(f"bd_luma {h['bd_luma']} != {case.bitdepth}")
        if case.expected_level_idc is not None and h["level_idc"] != case.expected_level_idc:
            mism.append(f"level_idc {h['level_idc']} != {case.expected_level_idc}")
        if h["tier"] != case.expected_tier:
            mism.append(f"tier {h['tier']} != {case.expected_tier}")
        if len(hv["hvcc"]) > 1:
            uniq = {(x["profile_idc"], x["level_idc"], x["chroma"], x["bd_luma"]) for x in hv["hvcc"]}
            if len(uniq) > 1:
                mism.append(f"多个 hvcC 不一致: {sorted(uniq)}")
    else:
        mism.append("产物中没有可解析的 hvcC")

    # W2-T2（C18）：SPS PTL/DPB 断言 —— still 语义只在 SPS 里（libheif 的 hvcC constraint
    # 实测恒 0），所以 17..21 必须 intra=1 && one_picture_only=1；16 走失败面（不可用）。
    sps = (st.sps_all or [None])[0] if st.sps_all else None
    facts["sps"] = sps
    if sps is None:
        mism.append("产物中没有可解析的 SPS（无法断言 still 语义）")
    else:
        # hvcC 与 SPS 的 idc/chroma/位深必须一致（同一个码流的两个视图）。
        if facts.get("hvcC"):
            h = facts["hvcC"]
            if sps.get("idc") != h.get("profile_idc"):
                mism.append(f"SPS idc {sps.get('idc')} != hvcC {h.get('profile_idc')}")
            if sps.get("chroma") != h.get("chroma"):
                mism.append(f"SPS chroma {sps.get('chroma')} != hvcC {h.get('chroma')}")
            if sps.get("bd_luma") != h.get("bd_luma"):
                mism.append(f"SPS bd {sps.get('bd_luma')} != hvcC {h.get('bd_luma')}")
            if sps.get("level_idc") != h.get("level_idc"):
                mism.append(f"SPS level {sps.get('level_idc')} != hvcC {h.get('level_idc')}")
            # libheif 不写 48 位 constraint → still 位只能来自 SPS（断言的必要性证据）。
            facts["hvcc_constraint_zero"] = h.get("constraint") == "000000000000"
        sig = PROFILES[case.profile][1] if case.profile is not None else None
        if sig and sig.get("still"):
            if sps.get("intra") != 1 or sps.get("one_picture_only") != 1:
                mism.append(f"still 变体 P{case.profile} 的 SPS 未写出 "
                            f"intra=1/one_picture_only=1：intra={sps.get('intra')} "
                            f"one_picture_only={sps.get('one_picture_only')} "
                            f"constraint48={sps.get('constraint48')}")

    # W2-T1：品牌结构断言（libheif 按 hvcC profile 选 major brand：Main/MainStill → heic；
    # Main10/RExt → heix。两种都带 mif1+miaf；grid 本身不改 brand）。
    ftyp = st.ftyp or {}
    expected_major = "heic" if case.expected_profile_idc in (1, 3) else "heix"
    if ftyp.get("major") != expected_major:
        mism.append(f"brand major={ftyp.get('major')!r} != {expected_major!r}")
    for brand in ("mif1", expected_major, "miaf"):
        if brand not in (ftyp.get("compatible") or []):
            mism.append(f"brand 兼容列表缺少 {brand}: {ftyp.get('compatible')}")

    if case.expected_grid:
        gs = st.grids
        if not gs:
            mism.append("grid 模式但没有 grid item")
        else:
            g = gs[0]
            exp = case.expected_grid
            if "error" in g:
                mism.append(f"grid item 解析失败: {g['error']}")
            else:
                if g["w"] != case.width or g["h"] != case.height:
                    mism.append(f"grid 声明尺寸 {g['w']}x{g['h']} != 源 {case.width}x{case.height}")
                if g["cols"] != exp["cols"] or g["rows"] != exp["rows"]:
                    mism.append(f"grid {g['cols']}x{g['rows']} != 期望 {exp['cols']}x{exp['rows']}")
                if len(st.tiles) != exp["cols"] * exp["rows"]:
                    mism.append(f"tile 数 {len(st.tiles)} != {exp['cols'] * exp['rows']}")
                if st.tiles:
                    sizes = {tuple(t["ispe"]) for t in st.tiles if t.get("ispe")}
                    if sizes != {(exp["tile_width"], exp["tile_height"])}:
                        mism.append(f"tile ispe {sorted(sizes)} != {(exp['tile_width'], exp['tile_height'])}")
                    # W2-T1：每 tile hvcC 的 level_idc 必须等于规划值（显式 Level / 每 tile 自动）。
                    exp_tile_level = exp.get("tile_level_idc")
                    if exp_tile_level:
                        bad_levels = sorted({
                            (t.get("hvcc") or {}).get("level_idc") for t in st.tiles
                            if (t.get("hvcc") or {}).get("level_idc") != exp_tile_level
                        })
                        if bad_levels:
                            mism.append(f"tile hvcC level {bad_levels} != 期望 {exp_tile_level}")
                dimg = [r for r in st.iref if r["type"] == "dimg"]
                if not dimg or len(dimg[0]["to"]) != len(st.tiles):
                    mism.append(f"dimg 引用数 {len(dimg[0]['to']) if dimg else 0} != tile 数 {len(st.tiles)}")
    else:
        if st.grids:
            mism.append("单图模式却出现 grid item")
        if not st.items:
            mism.append("未能解析 iinf（无法确认 hvc1 主 item）")
        elif not any(i["type"] == "hvc1" for i in st.items):
            mism.append("没有 hvc1 主 item")
        exp_ispe = [[case.width, case.height]]
        if hv.get("ispe") and hv["ispe"] != exp_ispe:
            # 单图 ispe 必须与源尺寸一致
            mism.append(f"ispe {hv['ispe']} != 源尺寸 {exp_ispe}")

    if dec["status"] == "fail":
        if case.bitdepth > 8 or case.chroma != "420":
            facts["decode_status"] = "local-decoder-limitation"
            facts["decode_text"] = dec["text"]
        else:
            mism.append(f"解码失败: {dec['text'][:200]}")
            facts["decode_status"] = "fail"
    else:
        facts["decode_status"] = dec["status"]
        facts["decode_text"] = dec["text"]
        if dec.get("width") and (dec["width"], dec["height"]) != (case.width, case.height):
            mism.append(f"解码尺寸 {dec['width']}x{dec['height']} != {case.width}x{case.height}")

    if mism:
        return "fail", mism, facts
    return "pass", [], facts


def cleanup_outdir(outdir: Path, keep: bool) -> str | None:
    """删除大产物（磁盘纪律）。keep=True 时保留并返回路径。"""
    if not outdir.exists():
        return None
    if keep:
        return str(outdir)
    shutil.rmtree(outdir, ignore_errors=True)
    return None


def run_case(case: Case, exe: Path, timeout: int, keep_artifacts: bool) -> dict:
    outdir = TMP_DIR / case.case_id
    if outdir.exists():
        shutil.rmtree(outdir, ignore_errors=True)
    outdir.mkdir(parents=True, exist_ok=True)

    run = run_encode(case, exe, timeout, outdir)
    out_file = find_output(outdir)
    hv = hvcc_probe(out_file) if out_file else {"ok": False, "hvcc": [], "ispe": [], "raw_error": "no output"}
    st = parse_heif(out_file) if out_file else HeifStructure()
    dec = decode_check(out_file) if out_file else {"status": "skip", "text": "no output"}
    verdict, mism, facts = evaluate(case, run, outdir, out_file, hv, st, dec)

    keep = keep_artifacts and case.src_kind == "small" and verdict in ("fail", "unexpected")
    artifact_dir = cleanup_outdir(outdir, keep)

    rec = {
        "case_id": case.case_id,
        "set": case.set_name,
        "phase": "?",
        "src": str(case.src),
        "src_kind": case.src_kind,
        "width": case.width, "height": case.height, "luma": case.luma,
        "bitdepth": case.bitdepth, "chroma": case.chroma, "level": case.level,
        "tier": case.tier,
        "profile": case.profile,
        "profile_name": PROFILES[case.profile][0] if case.profile is not None else "(not injected)",
        "grid": case.grid,
        "grid_key": case.grid_key,
        "tile_w": case.tile_w,
        "tile_h": case.tile_h,
        "expected_legal": case.expected_legal,
        "expected_reason": case.expected_reason,
        "expected_profile_idc": case.expected_profile_idc,
        "expected_level_idc": case.expected_level_idc,
        "expected_tier": case.expected_tier,
        "expected_grid": case.expected_grid,
        "expected_error_kind": case.expected_error_kind,
        "exit_code": run["exit_code"],
        "timed_out": run["timed_out"],
        "elapsed_s": run["elapsed_s"],
        "stdout_tail": tail(run["stdout"]),
        "stderr_tail": tail(run["stderr"]),
        "error_text": facts.get("error_text", ""),
        "out_exists": facts["out_exists"],
        "out_bytes": facts["out_bytes"],
        "hvcC": facts.get("hvcC"),
        "hvcC_all": hv.get("hvcc", []),
        "structure": facts.get("grid"),
        "decode_status": facts.get("decode_status"),
        "decode_text": facts.get("decode_text", ""),
        "verdict": verdict,
        "mismatches": mism,
        "repro": case.repro(exe),
        "artifact_dir": artifact_dir,
        "binary": str(exe),
        "binary_mtime": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(exe.stat().st_mtime)) if exe.exists() else "?",
        "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    return rec


def load_records(jsonl: Path) -> dict[str, dict]:
    out = {}
    if jsonl.exists():
        for ln in jsonl.read_text(encoding="utf-8").splitlines():
            try:
                r = json.loads(ln)
                out[r["case_id"]] = r
            except Exception:
                continue
    return out


# ---------------------------------------------------------------------------
# Markdown 汇总
# ---------------------------------------------------------------------------
def group_key(r: dict) -> str:
    exp = "legal" if r["expected_legal"] else "illegal"
    h = r.get("hvcC") or {}
    fact = f"idc={h.get('profile_idc')}/lvl={h.get('level_idc')}/ch={h.get('chroma')}/bd={h.get('bd_luma')}/tier={h.get('tier')}"
    err = (r.get("error_text") or "").split("|")[0].strip()[:160]
    return f"{r['verdict']}|{exp}|exit={r['exit_code']}|out={r['out_exists']}|{fact}|{err}"


def write_report(jsonl: Path, md: Path, phase: str, exe: Path, elapsed_total: float) -> None:
    recs = list(load_records(jsonl).values())
    if not recs:
        md.write_text(f"# HEIF matrix · {phase}\n\nno records\n", encoding="utf-8")
        return
    from collections import Counter, defaultdict

    verdicts = Counter(r["verdict"] for r in recs)
    exp_counts = Counter(("legal" if r["expected_legal"] else "illegal") for r in recs)
    sets = Counter(r["set"] for r in recs)
    decode = Counter(r.get("decode_status") for r in recs)
    groups = defaultdict(list)
    for r in recs:
        if r["verdict"] in ("fail", "unexpected"):
            groups[group_key(r)].append(r)
    unmatched = sum(len(v) for v in groups.values())

    lines = []
    lines.append(f"# HEIF 配置矩阵 · {phase}")
    lines.append("")
    lines.append(f"- 生成时间：{time.strftime('%Y-%m-%dT%H:%M:%S')}")
    lines.append(f"- 二进制：`{exe}`（mtime {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(exe.stat().st_mtime)) if exe.exists() else '?'}，sha256[:16]={sha256_prefix(exe)}）")
    lines.append(f"- 记录数：{len(recs)}；耗时（本轮新增）：{elapsed_total:.1f}s")
    bins = Counter(r.get("binary_mtime") for r in recs)
    lines.append(f"- 二进制 mtime 分布：{dict(bins)}" +
                 ("  ⚠️ **混用了不同构建的产物，结论需按 mtime 过滤**" if len(bins) > 1 else ""))
    lines.append(f"- 合法/非法（按模型）：{exp_counts.get('legal',0)} / {exp_counts.get('illegal',0)}")
    lines.append(f"- verdict：pass={verdicts.get('pass',0)} fail={verdicts.get('fail',0)} "
                 f"expected-fail={verdicts.get('expected-fail',0)} unexpected={verdicts.get('unexpected',0)}")
    lines.append(f"- 解码：{dict(decode)}")
    lines.append(f"- case 集：{dict(sets)}")
    lines.append(f"- **验收断言**：合法组合 pass = {verdicts.get('pass',0)} / {exp_counts.get('legal',0)}；"
                 f"非法组合 expected-fail = {verdicts.get('expected-fail',0)} / {exp_counts.get('illegal',0)}；"
                 f"unexpected = {verdicts.get('unexpected',0)}。")
    lines.append("- pass 判定 = 退出码 0 + 产物存在 + hvcC `profile_idc`/`level_idc`/`chroma`/`bit_depth_luma`/`tier` "
                 "与请求逐项相等 + ispe（单图）或 grid 结构（grid 声明尺寸、rows×cols、tile 数、tile ispe、dimg 引用）"
                 "+ `iinfo` 解码尺寸；任一不符即 fail。")
    lines.append("- 去重：同一 case_id 在多个集出现时只跑一次（如 final-profile-compat 的 24 个合法组合已由 "
                 "final-auto / final-signals 覆盖，故该集只剩 120 个非法组合）。")
    lines.append("")
    # 按 case 集拆分 verdict
    per_set = defaultdict(lambda: Counter())
    for r in recs:
        per_set[r["set"]][r["verdict"]] += 1
    lines.append("### 按 case 集")
    lines.append("")
    lines.append("| set | total | pass | fail | expected-fail | unexpected |")
    lines.append("|---|---|---|---|---|---|")
    for s, c in sorted(per_set.items()):
        lines.append(f"| {s} | {sum(c.values())} | {c.get('pass',0)} | {c.get('fail',0)} | "
                     f"{c.get('expected-fail',0)} | {c.get('unexpected',0)} |")
    lines.append("")
    lines.append("## 判定口径（复刻实现真源）")
    lines.append("")
    lines.append("- Level MaxLumaPs 来自 `src/codecs/heif_capability.cpp`（x265 4.3 第一列）："
                 "62→35,651,584；63→80,216,064；70/71/72→142,606,336（**不是**任务书里的 62→4,278,190,080，"
                 "那是 maxLumaSamplesPerSecond）。")
    lines.append("- 档位编号来自 `src/codecs/heif_profile.h`：1 Main(idc1)、2 Main10(idc2)、3 Main12(别名,12位任意色度)、"
                 "4 MainStill(idc3)、7..15 Rext(idc4)。5/6 为 AVIF 专用，HEIF 下应拒绝。")
    lines.append("- grid 规划复刻 `heif_plan_grid`（C17'）：tile 由规划 Level 的 MaxLumaPs(M) 与单边上限 "
                 "D=floor(sqrt(8M)) 及图像宽高推导（面积优先、同面积取最贴近图像宽高比，8 像素对齐、"
                 "≥64、无固定基准）；Level=自动 的规划上界 = still 互操作档 Level 5.1（3GPP "
                 "TS 26.140 §4.5 / TS 26.141 §4.2，hvc1.1.2.L153）；mode 1=总是 grid，"
                 "2=仅整图装不下时 grid。")
    lines.append("- 解码检查：`iinfo -v OUT`。局部 libde265/RExt 限制仅在结构检查通过时记为 "
                 "`local-decoder-limitation`（不判 fail），但会单列。")
    lines.append("")

    lines.append("")
    grid_recs = [r for r in recs if (r.get("expected_grid") or r.get("grid") in (1, 2))]
    if grid_recs:
        lines.append("## Grid 结构核验")
        lines.append("")
        lines.append("| case | 图像 | grid(rows×cols) | tile | tiles | 每 tile hvcC lvl | 期望 grid | hvcC idc/lvl/ch/bd | 解码 | verdict |")
        lines.append("|---|---|---|---|---|---|---|---|---|---|")
        for r in sorted(grid_recs, key=lambda x: x["case_id"]):
            st = r.get("structure") or {}
            g = (st.get("grids") or [{}])[0]
            ispe = st.get("tile_ispe") or []
            exp = r.get("expected_grid") or {}
            h = r.get("hvcC") or {}
            exp_s = (f"{exp.get('rows')}×{exp.get('cols')} / {exp.get('tile_width')}×{exp.get('tile_height')}"
                     if exp else "—")
            gs = f"{g.get('rows')}×{g.get('cols')}" if "rows" in g else ("ERR" if g else "—")
            tile = "×".join(map(str, ispe[0])) if len(ispe) == 1 else (str(ispe) if ispe else "—")
            levels = st.get("tile_levels") or []
            lines.append(f"| {r['case_id']} | {r['width']}×{r['height']} | {gs} | {tile} | "
                         f"{st.get('tile_count', 0)} | {levels} | {exp_s} | "
                         f"{h.get('profile_idc')}/{h.get('level_idc')}/{h.get('chroma')}/{h.get('bd_luma')} | "
                         f"{r.get('decode_status')} | {r['verdict']} |")
        lines.append("")

    stills = [r for r in recs if r["set"] == "final-still"]
    if stills:
        lines.append("## W2-T2 still 变体（C18）：SPS PTL/DPB 断言")
        lines.append("")
        lines.append("| case | 档位 | bd/chroma | hvcC idc/lvl | SPS intra | SPS one_picture | DPB(minus1/reorder) | 48 位 constraint | hvcC constraint | verdict |")
        lines.append("|---|---|---|---|---|---|---|---|---|---|")
        for r in sorted(stills, key=lambda x: x["case_id"]):
            h = r.get("hvcC") or {}
            st = r.get("structure") or {}
            sps = (st.get("sps") or [{}])[0] if st.get("sps") else {}
            prof = PROFILES.get(r.get("profile") or -1, ("?", None))[0]
            c48 = sps.get("constraint48") or "—"
            lines.append(
                f"| {r['case_id']} | {prof} | {r['bitdepth']}/{r['chroma']} | "
                f"{h.get('profile_idc')}/{h.get('level_idc')} | {sps.get('intra')} | "
                f"{sps.get('one_picture_only')} | "
                f"{sps.get('dpb_minus1')}/{sps.get('num_reorder_pics')} | `{c48}` | "
                f"`{h.get('constraint', '—')}` | {r['verdict']} |")
        lines.append("")
        lines.append("- 口径：still 语义只在 **SPS**（libheif 生成 hvcC 时丢 48 位 constraint，"
                     "实测 `hvcC.constraint` 恒 0）→ 17..21 断言 `intra=1 && one_picture_only=1`；"
                     "16（Main10Still）的 PTL 无 still 位，只能靠 DPB 0/0，实测 4/2 → 期望干净失败。")
        lines.append("")

    t1s = [r for r in recs if r["set"] == "final-t1-single"]
    if t1s:
        lines.append("## T1 单图（无 grid，22,842,308 luma）")
        lines.append("")
        lines.append("| case | bd/chroma | level | hvcC idc/lvl/ch/bd | 解码 | verdict |")
        lines.append("|---|---|---|---|---|---|")
        for r in sorted(t1s, key=lambda x: x["case_id"]):
            h = r.get("hvcC") or {}
            lines.append(f"| {r['case_id']} | {r['bitdepth']}/{r['chroma']} | {r['level']} | "
                         f"{h.get('profile_idc')}/{h.get('level_idc')}/{h.get('chroma')}/{h.get('bd_luma')} | "
                         f"{r.get('decode_status')} | {r['verdict']} |")
        lines.append("")

    bad = [r for r in recs if r["verdict"] == "expected-fail"]
    if bad:
        kinds = defaultdict(list)
        for r in bad:
            kinds[r.get("expected_error_kind") or "any"].append(r)
        lines.append("## expected-fail 错误文案抽样（证明“干净失败”）")
        lines.append("")
        for kind, rs in sorted(kinds.items()):
            lines.append(f"- `{kind}`：{len(rs)} 例。样例：")
            for r in rs[:2]:
                lines.append(f"  - `{r['case_id']}` → `{(r.get('error_text') or '')[:280]}`")
        lines.append("")

    if groups:
        lines.append(f"## Anomaly groups（{len(groups)} 组 / {unmatched} 例）")
        lines.append("")
        for i, (k, v) in enumerate(sorted(groups.items(), key=lambda kv: -len(kv[1]))[:40]):
            r = v[0]
            lines.append(f"### A{i+1}. {len(v)} 例 · verdict={r['verdict']}")
            lines.append("")
            lines.append(f"- 分组键：`{k}`")
            lines.append(f"- 代表 case：`{r['case_id']}`（src={r['src_kind']} bd={r['bitdepth']} "
                         f"chroma={r['chroma']} level={r['level']} tier={r['tier']} "
                         f"profile={r['profile']} grid={r['grid']}）")
            if r.get("mismatches"):
                lines.append(f"- mismatches：`{'; '.join(r['mismatches'])}`")
            if r.get("error_text"):
                lines.append(f"- error：`{r['error_text'][:500]}`")
            if r.get("hvcC"):
                lines.append(f"- hvcC：`{json.dumps(r['hvcC'], ensure_ascii=False)}`")
            if r.get("expected_grid"):
                lines.append(f"- 期望 grid：`{json.dumps(r['expected_grid'], ensure_ascii=False)}`")
            lines.append(f"- repro：`{r['repro']}`")
            if len(v) > 1:
                ids = ", ".join(x["case_id"] for x in v[:10])
                lines.append(f"- 同组样例：{ids}{' …' if len(v) > 10 else ''}")
            lines.append("")

    lines.append("## 已知异常核对（baseline 任务书点名项）")
    lines.append("")
    by_id = {r["case_id"]: r for r in recs}

    def find(pred):
        return [r for r in recs if pred(r)]

    # (i) T1 + level <= 52 失败 + 10-bit 误导后缀
    t1_low = [r for r in recs if r["src_kind"] == "t1" and r["level"] in (31, 52)
              and not r["expected_legal"] and r["grid"] == 0]
    if t1_low:
        with_suffix = [r for r in t1_low if "does not support" in (r.get("error_text") or "")]
        if phase == "baseline":
            lines.append(f"- (i) T1 + L≤5.2 失败：{len(t1_low)} 例；其中 error 含 "
                         f"\"encoder does not support N-bit input\" 的 {len(with_suffix)} 例"
                         f"（10/12 位应命中该误导后缀，8 位不应）。")
            for r in with_suffix[:3]:
                lines.append(f"  - `{r['case_id']}` → `{r['error_text'][:300]}`")
        else:
            lines.append(f"- (i) T1 + L≤5.2（关 grid）失败：{len(t1_low)} 例；其中 error 含旧误导后缀 "
                         f"\"does not support N-bit input\" 的 **{len(with_suffix)} 例**（目标 0）。"
                         "新码应给中文 Level 预校验：")
            for r in t1_low[:3]:
                lines.append(f"  - `{r['case_id']}`（bd={r['bitdepth']}）→ `{(r.get('error_text') or '')[:300]}`")
    else:
        lines.append("- (i) T1 + L≤5.2：本轮未运行该子集。")

    # (ii) 63/70/71/72
    lv_hi = [r for r in recs if r["level"] in (63, 70, 71, 72)]
    if lv_hi:
        acc = [r for r in lv_hi if r["verdict"] in ("pass", "fail") and r["exit_code"] == 0]
        lines.append(f"- (ii) Level 6.3/7.0/7.1/7.2：{len(lv_hi)} 例，编码成功 {len(acc)} 例"
                     f"（实际 level_idc 示例：{[(r['level'], (r.get('hvcC') or {}).get('level_idc')) for r in acc[:4]]}）。"
                     "**任务书假设 x265 4.3 不支持这四档，实测被推翻**：x265 4.3 level.cpp 有 6.3/7.0/7.1/7.2，"
                     "且新预校验表也收录（63→80,216,064；70/71/72→142,606,336）。")
    else:
        lines.append("- (ii) Level 6.3/7.x：本轮未运行该子集。")

    # (iii) 10-bit 420 实际 idc
    b10_420 = [r for r in recs if r["src_kind"] == "small" and r["bitdepth"] == 10 and r["chroma"] == "420"
               and r["level"] == 0 and r["tier"] == 0 and r["verdict"] == "pass"]
    if b10_420:
        if phase == "final":
            auto_r = next((r for r in b10_420 if r["profile"] == 0), None)
            main10_r = next((r for r in b10_420 if r["profile"] == 2), None)
            lines.append("- (iii) 10-bit 4:2:0 真实信号："
                         f"Auto → idc={(auto_r.get('hvcC') or {}).get('profile_idc') if auto_r else '?'}（RExt），"
                         f"显式 Main10(2) → idc={(main10_r.get('hvcC') or {}).get('profile_idc') if main10_r else '?'}；"
                         "两者均按请求校验通过 —— 旧 UI 把 idc=4 一律标 Main10 的名不副实已消除。")
        else:
            ids = {(r.get("hvcC") or {}).get("profile_idc") for r in b10_420}
            lines.append(f"- (iii) 10-bit 4:2:0 实际 profile_idc={sorted(x for x in ids if x is not None)}"
                         "（旧口径 UI 标 Main10；idc=4=RExt。新信号 Main10 才应写 idc=2，final 复核）。")
    # (iv) 4:4:4 + 显式 Main10/Main12（旧口径拒绝；新口径 Main12 别名应放行、Main10 仍不匹配）
    if phase == "final":
        def _rec(prof, bd, ch):
            for r in recs:
                if r["profile"] == prof and r["bitdepth"] == bd and r["chroma"] == ch \
                        and r["level"] == 0 and r["tier"] == 0 and r["grid"] == 0:
                    return r
            return None
        p3_444 = _rec(3, 12, "444")
        p2_444 = _rec(2, 10, "444")
        p14_444 = _rec(14, 10, "444")
        lines.append("- (iv) 4:4:4 + 显式档位（旧 `heif_profile_compatible` 曾一律拒绝 HEVC 档位 × 4:4:4）：")
        if p3_444:
            lines.append(f"  - Main12(3) @12-bit/4:4:4 → `{p3_444['verdict']}`，"
                         f"hvcC={json.dumps(p3_444.get('hvcC'), ensure_ascii=False)}（兼容别名，色度随输入）")
        if p2_444:
            lines.append(f"  - Main10(2) @10-bit/4:4:4 → `{p2_444['verdict']}`（按设计不匹配：Main10 固定 4:2:0；"
                         f"错误：`{(p2_444.get('error_text') or '')[:200]}`）")
        if p14_444:
            lines.append(f"  - Rext444_10(14) @10-bit/4:4:4 → `{p14_444['verdict']}`，"
                         f"hvcC={json.dumps(p14_444.get('hvcC'), ensure_ascii=False)}")
    else:
        lines.append("- (iv) 4:4:4 + 显式 Main10/Main12：**baseline 无法验证**——dev harness 尚未接受 "
                     "`__heif_profile`（保留键被拒，见本报告 harness 探测）；源码层旧 "
                     "`heif_profile_compatible` 对 4:4:4+HEVC 档位判 false（`src/codecs/heif_profile.h` 旧口径），"
                     "final 用新二进制与新编号验证。")
    # (v) auto level
    small_auto = [r for r in recs if r["src_kind"] == "small" and r["level"] == 0 and r["tier"] == 0
                  and r["bitdepth"] == 8 and r["chroma"] == "420" and r["verdict"] == "pass"]
    if small_auto:
        lv = (small_auto[0].get("hvcC") or {}).get("level_idc")
        lines.append(f"- (v) 小图 auto Level 实际 level_idc={lv}（{'3.1' if lv == 93 else '见记录'}；"
                     "1024×576=589,824 > L3.0 552,960 -> 最小可行 L3.1，与 x265 determineLevel 一致）。")
    lines.append("")

    lines.append("## 覆盖声明（如实）")
    lines.append("")
    lines.append(f"- 本轮运行 case 集：{dict(sets)}；未列入的集未运行。")
    if phase == "baseline":
        lines.append("- baseline 二进制（2026-10-29 构建）**不支持** `__heif_profile` / `__heif_grid` 保留键："
                     "dev harness 报 `未收录参数`；因此 baseline 无 profile/grid 维度，也不含 AVIF。")
        lines.append("- T1 仅跑任务书点名子集（level 0/31/52/60/62/63 × chroma 420/444 × bd 8/10/12，tier=0）；"
                     "level 70/71/72 未在 T1 上跑（默认；`--t1-extra-levels` 可加）。")
        lines.append("- 4:4:4/12-bit 的局部解码限制只影响 `decode_status` 分类，不掩盖 hvcC 结构断言。")
    else:
        lines.append("- final 实际覆盖：Auto 324（9 组 bd/chroma × 18 Level × 2 tier）+ 13 个真实信号 × 全 Level × 2 tier"
                     "（540）+ 档位兼容矩阵（计划 144，24 个合法组合与前述重复去重后 120 个拒绝面）+ "
                     "T1 grid 20（L3.1 6×5 / 总是 grid / Main10 / 422 / 444 / 12-bit / tile 收缩）+ "
                     "**W2-T1 grid×tile×Level 11**（heif_grid 新键自动/总是/关闭、手动 tile 合法/非法、"
                     "Level 收缩、8 向下对齐、1×1 降级单图）+ T1 单图 9"
                     "（auto 6 + Main10 + 显式 L6.0 / L6.3）。")
        lines.append("- 未跑（如实）：AVIF 矩阵；T1 单图全 Level × 全色度；T1 L2.0 grid 收缩（207 tiles，需 `--grid-shrink`）；"
                     "**真机 iPhone 硬解**——本工具只证明码流结构与本地解码，硬解能力仍需真机 VT 强锁探针"
                     "（`super/iphone-test/`）。")
    lines.append("")

    lines.append("## 复现")
    lines.append("")
    lines.append("```")
    lines.append(f"python tools/env.py run -- python tools/heif_matrix_check.py --phase {phase} --resume")
    lines.append("```")
    lines.append("")
    try:
        jsonl_disp = jsonl.resolve().relative_to(REPO)
    except ValueError:
        jsonl_disp = jsonl
    lines.append(f"JSONL：`{jsonl_disp}`（每行一个 case 完整证据）。")
    md.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="HEIF 配置矩阵验证器（M9）")
    ap.add_argument("--phase", choices=["baseline", "final"], default="baseline")
    ap.add_argument("--set", action="append", default=[],
                    help="只跑指定 case 集（可重复）：baseline-small/baseline-t1/final-auto/"
                         "final-signals/final-profile-compat/final-still/final-grid/selftest")
    ap.add_argument("--binary", default=str(DEFAULT_EXE))
    ap.add_argument("--jsonl", default="", help="默认 build/matrix/<phase>.jsonl")
    ap.add_argument("--md", default="", help="默认 build/matrix/<phase>.md")
    ap.add_argument("--tmp-dir", default="",
                    help="产物临时目录（默认 build/matrix/tmp；并行任务用独立目录避免互删）")
    ap.add_argument("--timeout", type=int, default=240, help="单例编码超时秒（默认 240）")
    ap.add_argument("--max-cases", type=int, default=0, help="本轮最多跑 N 例（冒烟）")
    ap.add_argument("--force", action="store_true", help="重跑 JSONL 中已存在的 case")
    ap.add_argument("--no-resume", action="store_true", help="不跳过已有 case（= --force）")
    ap.add_argument("--keep-artifacts", action="store_true", default=True)
    ap.add_argument("--no-keep-artifacts", dest="keep_artifacts", action="store_false")
    ap.add_argument("--t1-extra-levels", action="store_true", help="baseline T1 加 70/71/72")
    ap.add_argument("--grid-shrink", action="store_true", help="final grid 加 T1 L2.0 收缩（207 tiles，慢）")
    ap.add_argument("--list", action="store_true", help="只列 case 数/样例，不执行")
    args = ap.parse_args(argv)

    exe = Path(args.binary)
    if not exe.is_absolute():
        exe = REPO / exe
    if not exe.exists():
        print(f"[matrix] binary not found: {exe}", file=sys.stderr)
        return 2
    MATRIX_DIR.mkdir(parents=True, exist_ok=True)
    global TMP_DIR
    if args.tmp_dir:
        TMP_DIR = Path(args.tmp_dir)
        if not TMP_DIR.is_absolute():
            TMP_DIR = REPO / TMP_DIR
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    # 0.4.2 / C7：evidence 依赖存在性显式报告（缺失 -> 逐例显式 skip；不静默）
    report_evidence_deps()

    if args.set == ["selftest"]:
        return run_selftest(exe)

    all_sets = build_case_sets(args.phase, args.grid_shrink, args.t1_extra_levels)
    chosen = args.set or list(all_sets.keys())
    cases: list[Case] = []
    for s in chosen:
        if s not in all_sets:
            print(f"[matrix] unknown set '{s}'; available: {list(all_sets)}", file=sys.stderr)
            return 2
        cases.extend(all_sets[s])

    if args.list:
        from collections import Counter
        print(f"[matrix] phase={args.phase} cases={len(cases)} sets={dict(Counter(c.set_name for c in cases))}")
        for c in cases[:8]:
            print("  ", c.case_id, "legal=", c.expected_legal, "reason=", c.expected_reason,
                  "grid=", c.expected_grid)
        return 0

    jsonl = Path(args.jsonl) if args.jsonl else MATRIX_DIR / f"{args.phase}.jsonl"
    md = Path(args.md) if args.md else MATRIX_DIR / f"{args.phase}.md"
    if not jsonl.is_absolute():
        jsonl = REPO / jsonl
    if not md.is_absolute():
        md = REPO / md
    existing = {} if (args.force or args.no_resume) else load_records(jsonl)

    print(f"[matrix] phase={args.phase} binary={exe} mtime="
          f"{time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(exe.stat().st_mtime))} "
          f"cases={len(cases)} skip={sum(1 for c in cases if c.case_id in existing)}")
    t0 = time.time()
    run_n = 0
    dep_skips = 0
    seen = set(existing)
    with open(jsonl, "a", encoding="utf-8") as f:
        for case in cases:
            if case.case_id in seen:
                continue
            seen.add(case.case_id)
            if args.max_cases and run_n >= args.max_cases:
                break
            run_n += 1
            if not case.src.exists():
                # 0.4.2 / C7：源夹具缺失 -> 显式 skip（不静默、不伪装成码流 fail）
                reason = (f"源夹具缺失: {case.src}（C7 已清空 build/；重建：用 M9 流程重新生成"
                          "或提供自备夹具到该路径）")
                rec = dependency_skip_record(case, reason)
                dep_skips += 1
                print(f"[{run_n:4d}/{len(cases)}] {case.case_id:<44} skip         "
                      f"（{reason}）", flush=True)
                rec["phase"] = args.phase
                f.write(json.dumps(rec, ensure_ascii=False) + "\n")
                f.flush()
                continue
            try:
                rec = run_case(case, exe, args.timeout, args.keep_artifacts)
            except Exception as e:
                rec = {
                    "case_id": case.case_id, "set": case.set_name, "phase": args.phase,
                    "src": str(case.src), "src_kind": case.src_kind,
                    "width": case.width, "height": case.height, "luma": case.luma,
                    "bitdepth": case.bitdepth, "chroma": case.chroma, "level": case.level,
                    "tier": case.tier, "profile": case.profile, "grid": case.grid,
                    "expected_legal": case.expected_legal, "expected_reason": case.expected_reason,
                    "verdict": "fail", "mismatches": [f"validator exception: {e}"],
                    "error_text": traceback.format_exc()[-800:],
                    "repro": case.repro(exe), "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
                }
            rec["phase"] = args.phase
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
            f.flush()
            h = rec.get("hvcC") or {}
            print(f"[{run_n:4d}/{len(cases)}] {rec['case_id']:<44} {rec['verdict']:<13} "
                  f"exit={rec.get('exit_code')} {rec.get('elapsed_s')}s "
                  f"idc={h.get('profile_idc')} lvl={h.get('level_idc')} "
                  f"grid={len((rec.get('structure') or {}).get('grids', []) or [])} "
                  f"tiles={(rec.get('structure') or {}).get('tile_count')}", flush=True)
    elapsed = time.time() - t0

    # 报告基于 JSONL 全量（含历史 resume 记录）
    write_report(jsonl, md, args.phase, exe, elapsed)
    recs = list(load_records(jsonl).values())
    unexpected = sum(1 for r in recs if r["verdict"] == "unexpected")
    fails = sum(1 for r in recs if r["verdict"] == "fail")
    print(f"[matrix] done run={run_n} in {elapsed:.1f}s; jsonl={jsonl}; report={md}; "
          f"dependency-skip={dep_skips}")
    print(f"[matrix] totals: pass={sum(1 for r in recs if r['verdict']=='pass')} "
          f"fail={fails} expected-fail={sum(1 for r in recs if r['verdict']=='expected-fail')} "
          f"skip={sum(1 for r in recs if r['verdict']=='skip')} "
          f"unexpected={unexpected}")
    if run_n > 0 and dep_skips == run_n:
        # 全部因 evidence 依赖缺失而跳过：显式 SKIP（容忍 C7 清空后不可复跑），不判红。
        print("[matrix] RESULT: SKIP（本轮全部 case 因 evidence 依赖/源夹具缺失而跳过；"
              "重建提示见上方 dep/SKIP 行）")
        return 0
    return 0 if (unexpected == 0 and fails == 0) else 1


def report_evidence_deps() -> dict:
    """0.4.2 / C7（构建零临时依赖）：evidence 依赖存在性**显式报告**（缺失 = 显式 skip，不静默）。

    本工具历史证据输入曾在 build/ 下（analysis/hvcc.py、tmp-heif/small.tif、matrix/probe/…）；
    用户要求清空 build/ 后这些文件不再保证存在。这里统一打印状态与重建提示，并由调用方把
    「缺失」降级为带原因的 skip（不是 fail、更不是静默 pass）。
    """
    deps = [
        ("hvcc.py（hvcC 断言探针；缺失 -> 相关判定显式 skip）", HVCC_PY),
        ("small.tif（small 组 1024x576 源夹具）", SMALL_TIF),
        ("T1.tif（t1 组源夹具，super/ 用户数据）", T1_TIF),
        ("iinfo（解码复核，可选）", IINFO),
    ]
    states = {}
    for label, path in deps:
        present = path.exists()
        states[str(path)] = present
        print(f"[matrix] dep {'OK     ' if present else 'MISSING'} {label} @ {path}")
        if not present:
            print("[matrix]   → 缺失容忍：相关检查将显式 skip 并逐条打印（C7：build/ 已清空；"
                  "重建方式 = 用 M9 流程重新生成/从历史证据恢复，或提供自备夹具到该路径）")
    return states


def dependency_skip_record(case: "Case", reason: str) -> dict:
    """源夹具缺失时的一致性记录（verdict=skip；字段面与 run_case 记录同构，报告可读）。"""
    return {
        "case_id": case.case_id, "set": case.set_name, "phase": "?",
        "src": str(case.src), "src_kind": case.src_kind,
        "width": case.width, "height": case.height, "luma": case.luma,
        "bitdepth": case.bitdepth, "chroma": case.chroma, "level": case.level,
        "tier": case.tier, "profile": case.profile, "grid": case.grid,
        "expected_legal": case.expected_legal, "expected_reason": case.expected_reason,
        "verdict": "skip", "mismatches": [reason],
        "structure": {"tiles": [], "grids": [], "tile_count": 0},
        "hvcC": None, "out_exists": False, "out_bytes": 0,
        "exit_code": None, "elapsed_s": 0.0, "timed_out": False,
        "error_text": "", "ts": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }


def run_selftest(exe: Path) -> int:
    """解析器自检：不编码，直接解析已知产物（原型 grid / 单图）。

    0.4.2 / C7：三条参考产物里两条在 build/（用户清空后可能缺失）-> 逐条显式 SKIP + 原因 +
    重建提示；全部缺失时输出 SKIP（不是伪 OK、也不是 FAIL），部分可测时只对**实际执行**的检查
    下结论，并把 skipped 数量打进结果行。
    """
    checks = [
        (REPO / "build/analysis-heic/grid_proto/exp2_grid_level31/t1_grid_l31_1024x576_main10.heic",
         dict(grid=True, cols=4, rows=11, tiles=44, idc=2, level=93, chroma=1, bd=10),
         "M9 grid 原型产物（build/ 证据目录，C7 后可能缺失；重建 = M9 原型流程）"),
        (REPO / "super/T7.heic", dict(grid=True, cols=6, rows=7, tiles=42, idc=2, level=93,
                                      chroma=1, bd=10),
         "用户数据 super/T7.heic（不在 build/，正常应存在）"),
        (REPO / "build/matrix/probe/b10_c444/small.heic",
         dict(grid=False, cols=None, rows=None, tiles=1, idc=4, level=93, chroma=3, bd=10),
         "M9 probe 产物（build/ 证据目录，C7 后可能缺失；重建 = 先跑 --set baseline-small 生成）"),
    ]
    ok_all = True
    tested = 0
    skipped = 0
    for path, exp, why in checks:
        if not path.exists():
            print(f"SELFTEST SKIP {path} (missing; {why})")
            skipped += 1
            continue
        if not HVCC_PY.exists():
            print(f"SELFTEST SKIP {path} (hvcC 探针 hvcc.py 缺失: {HVCC_PY}；C7 后 build/ 已清空)")
            skipped += 1
            continue
        tested += 1
        st = parse_heif(path)
        hv = hvcc_probe(path)
        h = hv["hvcc"][0] if hv.get("hvcc") else {}
        g = st.grids[0] if st.grids else {}
        got = dict(grid=bool(st.grids), cols=g.get("cols"), rows=g.get("rows"),
                   tiles=len(st.tiles), idc=h.get("profile_idc"), level=h.get("level_idc"),
                   chroma=h.get("chroma"), bd=h.get("bd_luma"))
        ok = (got["grid"] == exp["grid"] and got["tiles"] == exp["tiles"]
              and got["idc"] == exp["idc"] and got["level"] == exp["level"]
              and got["chroma"] == exp["chroma"] and got["bd"] == exp["bd"])
        if exp["grid"]:
            ok = ok and got["cols"] == exp["cols"] and got["rows"] == exp["rows"]
        ok_all = ok_all and ok
        print(f"SELFTEST {'OK  ' if ok else 'FAIL'} {path.name} got={got} exp={exp}")
    if tested == 0:
        print(f"SELFTEST RESULT: SKIP（参考夹具/探针全部缺失：tested=0 skipped={skipped}；"
              "C7 清空 build/ 后属预期，重建提示见上方逐条 SKIP 行）")
        return 0
    print(f"SELFTEST RESULT: {'OK' if ok_all else 'FAIL'}（tested={tested} skipped={skipped}）")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
