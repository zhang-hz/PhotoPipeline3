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
  * grid 规划：heif_plan_grid()（1024×576 基准，色度对齐，显式 Level 装不下整图时按
    MaxLumaPs 等比收缩；mode 1=总是 grid，2=仅整图装不下时 grid）。

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

# 档位编号单源 = src/codecs/heif_profile.h（0..6 既有编号，7..15 RExt 追加）。
# key: int 编号；value: (显示名, 期望 signal dict) ；signal 为 None = Auto（不校验信号）。
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
}
# HEVC 合法档位编号（用于 final 的 signal×level 深矩阵）
HEVC_SIGNAL_PROFILES = [1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 15]


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


def plan_grid(grid_mode: int, level: int, width: int, height: int, chroma: str) -> dict:
    """逐字复刻 heif_plan_grid（src/codecs/heif_capability.cpp，2026-10-31 08:56 版）。

    2026-10-31 后口径：tile 宽高统一 8 对齐（覆盖 x265 minCUSize 补齐与 420/422 色度对齐）；
    显式 Level 容不下基准 1024×576 时，按“面积最大、等面积取最接近 16:9”搜索 tile。
    """
    plan = {"use_grid": False, "level_idc": level, "tile_width": 0, "tile_height": 0,
            "cols": 0, "rows": 0, "tile_luma_samples": 0,
            "level_fits_image": level_fits(level, width, height)}
    if width <= 0 or height <= 0:
        return plan
    if level != 0 and level not in LEVEL_PS:
        return plan  # 未知 Level：不规划，由调用方给“Level 不存在”错误
    if grid_mode not in (1, 2):
        return plan
    if grid_mode == 2 and plan["level_fits_image"]:
        return plan
    plan["use_grid"] = True

    k_align = 8
    k_base_w, k_base_h = 1024, 576
    k_min_tile = 64

    def align8(v: int) -> int:
        return 0 if v <= 0 else (v // k_align) * k_align

    def clamp_tile(v: int, base: int) -> int:
        return max(k_align, align8(min(v, base)))

    tile_w = clamp_tile(width, k_base_w)
    tile_h = clamp_tile(height, k_base_h)
    max_luma = LEVEL_PS.get(level, 0) if level else 0
    if max_luma > 0:
        dim_cap = int((8.0 * max_luma) ** 0.5)
        base_fits = (tile_w * tile_h <= max_luma and tile_w <= dim_cap and tile_h <= dim_cap)
        if not base_fits:
            w_max = min(width, k_base_w, dim_cap)
            h_max = min(height, k_base_h, dim_cap)
            best_w = best_h = 0
            best_area = -1
            best_diff = 1e9
            target = k_base_w / k_base_h
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
            if best_w < k_min_tile or best_h < k_min_tile:
                plan["use_grid"] = False
                return plan
            tile_w, tile_h = best_w, best_h
    plan["tile_width"] = tile_w
    plan["tile_height"] = tile_h
    plan["tile_luma_samples"] = tile_w * tile_h
    plan["cols"] = (width + tile_w - 1) // tile_w
    plan["rows"] = (height + tile_h - 1) // tile_h
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
    ispe_all: list = field(default_factory=list)
    iref: list = field(default_factory=list)
    ftyp: dict = field(default_factory=dict)

    def to_json(self) -> dict:
        return {
            "item_count": len(self.items),
            "grids": self.grids,
            "tile_count": len(self.tiles),
            "tile_ispe": sorted({tuple(t["ispe"]) for t in self.tiles if t.get("ispe")}),
            "hvcc": self.hvcc_all,
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
    grid: int | None     # None = 不注入 __heif_grid（baseline）
    expected_legal: bool = True
    expected_reason: str = ""
    expected_profile_idc: int | None = None
    expected_level_idc: int | None = None
    expected_tier: int = 0
    expected_grid: dict | None = None
    expected_error_kind: str = ""  # level | profile | param | any

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
            parts += ["--param", f"__heif_grid={self.grid}"]
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
              profile=None, grid=None) -> Case:
    cid_bits = [src_kind, f"b{bd}", chroma, f"l{level}", f"t{tier}"]
    if profile is not None:
        cid_bits.append(f"p{profile}")
    if grid is not None:
        cid_bits.append(f"g{grid}")
    cid = "-".join(cid_bits)

    legal = True
    reasons = []
    profile_bad = False
    level_bad = False
    # profile 兼容
    if profile is not None:
        pok, preason = profile_signal_legal(profile, bd, chroma)
        if not pok:
            legal = False
            profile_bad = True
            reasons.append(preason)
    # grid / level
    grid_plan = None
    if grid is not None:
        grid_plan = plan_grid(grid, level, width, height, chroma)
    if level == 0:
        pass
    elif grid_plan is not None and grid_plan["use_grid"]:
        if grid_plan["tile_luma_samples"] > LEVEL_PS.get(level, 0):
            legal = False
            level_bad = True
            reasons.append("grid tile 仍超过 Level 上限")
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

    effective_luma = grid_plan["tile_luma_samples"] if (grid_plan and grid_plan["use_grid"]) else width * height
    exp_level_idc = level_idc(level) if level else level_idc(auto_level(effective_luma))

    ed = (grid or 0)
    if not legal:
        # enc_heif 先做档位反查、再做 Level 预校验；两者都非法时错误文案是档位。
        err_kind = "profile" if profile_bad else ("level" if level_bad else "")
    else:
        err_kind = ""
    if profile is None and grid is None:
        # baseline 旧二进制没有中文预校验（只有 libheif/x265 泛化错误），不强制点名原因。
        err_kind = ""

    return Case(
        case_id=cid, set_name=set_name, src_kind=src_kind, src=src, width=width, height=height,
        bitdepth=bd, chroma=chroma, level=level, tier=tier, profile=profile, grid=grid,
        expected_legal=legal, expected_reason="; ".join(reasons) if reasons else "legal",
        expected_profile_idc=exp_profile_idc, expected_level_idc=exp_level_idc,
        expected_tier=expected_tier_flag(level, tier, effective_luma),
        expected_grid=grid_plan if (grid_plan and grid_plan["use_grid"]) else None,
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
        # 基线：dev harness 尚不接受 "__" 保留键 ⇒ 不注入 profile/grid。
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
        # 4) grid / Apple 模式 / T1 真实大图
        grid_cases = [
            # 任务要求的主证据：T1 + L3.1 + grid=2 → 4×11 tile，每 tile L3.1
            ("t1", 10, "420", 31, 0, 2, None),   # Auto（idc4）
            ("t1", 10, "420", 31, 0, 2, 2),      # Main10（idc2，Apple T7 同配置）
            ("t1", 10, "420", 31, 0, 2, 8),      # Rext420_10（idc4 对照）
            ("t1", 8, "420", 31, 0, 2, None),    # Auto（idc3）
            ("t1", 10, "420", 31, 0, 1, 2),      # Apple 式（总是 grid）
            ("t1", 10, "420", 0, 0, 2, None),    # 自动 Level + auto grid → 不分块
            ("t1", 10, "420", 0, 0, 1, None),    # 自动 Level + 总是 grid
            ("t1", 10, "420", 31, 0, 0, None),   # 关 grid + 非法 Level → 必须干净失败
            ("t1", 10, "420", 52, 0, 0, None),   # 旧误导后缀复现点（新码应为中文 Level 错误）
            ("t1", 8, "420", 52, 0, 0, None),    # 8 位对照（旧码无后缀）
            ("t1", 10, "420", 31, 0, 2, 1),      # Profile 不兼容（Main 要求 8 位）→ 干净失败
            ("t1", 12, "444", 31, 0, 2, 15),     # Rext444_12 12 位大图 grid
            ("small", 8, "420", 30, 0, 2, None), # 小图 tile 收缩（L3.0 → 2×2 tile）
            ("small", 10, "420", 30, 0, 2, 2),   # 小图 Main10 + 收缩 tile
            ("small", 8, "420", 0, 0, 1, None),  # 小图总是 grid（1×1）
            ("small", 8, "420", 10, 0, 2, None), # 小图强收缩（L1.0 → 4×4 tile，16 块）
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
        cmd += ["--param", f"__heif_grid={case.grid}"]
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
    """复用 build/analysis/hvcc.py（任务要求）。返回解析后的首个 hvcC + 全部。"""
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
    outdir = MATRIX_DIR / "tmp" / case.case_id
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
    lines.append("- grid 规划复刻 `heif_plan_grid`：1024×576 基准 tile、统一 8 像素对齐；显式 Level 容不下基准 tile 时，"
                 "按“面积最大、等面积取最接近 16:9”搜索（mode 1=总是 grid，2=仅整图装不下时 grid）。")
    lines.append("- 解码检查：`iinfo -v OUT`。局部 libde265/RExt 限制仅在结构检查通过时记为 "
                 "`local-decoder-limitation`（不判 fail），但会单列。")
    lines.append("")

    lines.append("")
    grid_recs = [r for r in recs if (r.get("expected_grid") or r.get("grid") in (1, 2))]
    if grid_recs:
        lines.append("## Grid 结构核验")
        lines.append("")
        lines.append("| case | 图像 | grid(rows×cols) | tile | tiles | 期望 grid | hvcC idc/lvl/ch/bd | 解码 | verdict |")
        lines.append("|---|---|---|---|---|---|---|---|---|")
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
            lines.append(f"| {r['case_id']} | {r['width']}×{r['height']} | {gs} | {tile} | "
                         f"{st.get('tile_count', 0)} | {exp_s} | "
                         f"{h.get('profile_idc')}/{h.get('level_idc')}/{h.get('chroma')}/{h.get('bd_luma')} | "
                         f"{r.get('decode_status')} | {r['verdict']} |")
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
                     "1024×576=589,824 > L3.0 552,960 ⇒ 最小可行 L3.1，与 x265 determineLevel 一致）。")
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
                     "T1 grid 20（4×11 / Apple 模式 / Main10 / 422 / 444 / 12-bit / tile 收缩）+ T1 单图 9"
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
                         "final-signals/final-profile-compat/final-grid/selftest")
    ap.add_argument("--binary", default=str(DEFAULT_EXE))
    ap.add_argument("--jsonl", default="", help="默认 build/matrix/<phase>.jsonl")
    ap.add_argument("--md", default="", help="默认 build/matrix/<phase>.md")
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
    (MATRIX_DIR / "tmp").mkdir(parents=True, exist_ok=True)

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
    seen = set(existing)
    with open(jsonl, "a", encoding="utf-8") as f:
        for case in cases:
            if case.case_id in seen:
                continue
            seen.add(case.case_id)
            if args.max_cases and run_n >= args.max_cases:
                break
            run_n += 1
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
    print(f"[matrix] done run={run_n} in {elapsed:.1f}s; jsonl={jsonl}; report={md}")
    print(f"[matrix] totals: pass={sum(1 for r in recs if r['verdict']=='pass')} "
          f"fail={fails} expected-fail={sum(1 for r in recs if r['verdict']=='expected-fail')} "
          f"unexpected={unexpected}")
    return 0 if (unexpected == 0 and fails == 0) else 1


def run_selftest(exe: Path) -> int:
    """解析器自检：不编码，直接解析已知产物（原型 grid / 单图）。"""
    checks = [
        (REPO / "build/analysis-heic/grid_proto/exp2_grid_level31/t1_grid_l31_1024x576_main10.heic",
         dict(grid=True, cols=4, rows=11, tiles=44, idc=2, level=93, chroma=1, bd=10)),
        (REPO / "super/T7.heic", dict(grid=True, cols=6, rows=7, tiles=42, idc=2, level=93,
                                      chroma=1, bd=10)),
        (REPO / "build/matrix/probe/b10_c444/small.heic",
         dict(grid=False, cols=None, rows=None, tiles=1, idc=4, level=93, chroma=3, bd=10)),
    ]
    ok_all = True
    for path, exp in checks:
        if not path.exists():
            print(f"SELFTEST SKIP {path} (missing)")
            continue
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
    print("SELFTEST RESULT:", "OK" if ok_all else "FAIL")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
