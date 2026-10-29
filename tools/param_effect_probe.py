#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""M8 / W3 —— 高级参数「是否真的连线生效」机械化探针（需求 4a）。

背景
----
M8 把 116 行高级参数（103 可设置 + 13 只读）写进参数目录
（``tools/param_catalog_specs/*.json``）。本探针用**A/B 两次真实编码**逐行取证：

  * A、B 两个变体各带一个**合法且彼此不同**的取值（值来自 spec 的 enum/域/默认），
    条件参数同时带上它依赖的前置键（如 ``x265:psy-rdoq`` 前置 ``x265:rdoq-level=2``）；
  * 对 ``--dev --format heif|avif|jxl --param K=V`` 的产物**文件字节**做 SHA256：
      产物 SHA 不同  ⇒ EFFECT    （参数到达编码器并改变了码流/容器）
      产物 SHA 相同  ⇒ NO_EFFECT （逐条归因：合法 no-op / 上游限制 / 真断线）
  * 再对 NO_EFFECT 行做归因辅助：A/B 均成功 = 键名与取值已被后端解析
    （x265 的 ``x265_param_parse`` 对未知选项名返回 -1、aom 的
    ``aom_codec_set_option`` 对未知名返回错误、libjxl 的
    ``JxlEncoderFrameSettingsSetOption`` 对越界返回错误；三处在上游实现里都会把
    「参数没送达」变成**硬编码失败**——所以「A/B 都成功」本身即证明键送达；
    剩下的零字节差异只能来自语义不改变本配置的码流，而不是被丢弃）。
  * 只读行（13）不编码，登记为 READONLY（上游固定/推导值）。

独立证据面（用户点名怀疑 HEIF 444 与 420）
------------------------------------------
``--chroma-evidence`` 段会对 HEIF/AVIF 的 chroma=420/422/444 分别编码，再用同一
``photopipeline.exe --dev`` 把产物解码回 PNG，比较：
  * 产物字节 SHA / 解码 PNG SHA；
  * 解码像素相对源图的 RGB MAE（444 应显著小于 420 —— 编码侧 chroma 抽样真实发生）；
  * 420 vs 444 解码像素的差异样本占比。
PNG 解码/比较由本脚本内置的纯 Python（zlib 标准库）实现，不新增依赖。
评分侧（SSIMULACRA2 数值）由 metric-verifier 负责，本探针不做评分结论。

用法（所有命令经 tools/env.py 包装；默认 exe = 本任务构建树 build/w3-param）
--------------------------------------------------------------------------
    python tools/env.py run -- python tools/param_effect_probe.py
    python tools/env.py run -- python tools/param_effect_probe.py --chroma-evidence
    python tools/env.py run -- python tools/param_effect_probe.py --only x265:crf,aom:enable-qm
    python tools/env.py run -- python tools/param_effect_probe.py --formats avif --backend libaom

产物（gitignored，报告引用路径）
------------------------------
    build/evidence-w3/results.json     机器可读逐行结果
    build/evidence-w3/results.csv      同上（表格）
    build/evidence-w3/run.log          本次运行的完整 stdout（用 Tee 亦可）
    build/evidence-w3/runs/<行>/<A|B>/ 每次 A/B 的产物 + 编码器日志
    build/evidence-w3/chroma/          色度独立证据（产物 + 解码 PNG）

判据/口径
---------
EFFECT           两个变体产物字节 SHA 不同（或一个成功一个被后端拒绝 = 值被消费）。
NO_EFFECT        A/B 都成功且产物逐字节相同；归因见报告（合法 no-op / 上游限制）。
EFFECT_REJECTED  B（或 A）被后端拒绝、另一变体成功：拒绝文案证明键已到达库。
ERROR            两次都失败或产物缺失：需人工归因（可能是本探针取值不当）。
READONLY         只读行，按目录声明登记，不编码。
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zlib
from datetime import datetime

# ---------------------------------------------------------------------------
# 路径/常量
# ---------------------------------------------------------------------------

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC_DIR = os.path.join(ROOT, "tools", "param_catalog_specs")
EVIDENCE_DIR = os.path.join(ROOT, "build", "evidence-w3")
DEFAULT_EXE = os.path.join(ROOT, "build", "w3-param", "photopipeline.exe")
DEFAULT_FIXTURE = os.path.join(ROOT, "build", "tmp-m8fix", "color.png")
FALLBACK_FIXTURES = [
    os.path.join(ROOT, "tests", "golden", "base", "rgb8.png"),
    os.path.join(ROOT, "tests", "golden", "base", "rgb16.png"),
]

# spec 文件 → 运行面。bitdepth 显式给定（HEIF/AVIF=8；JXL 用 dev 默认 16）。
SPEC_META = {
    "x265.json": {
        "format": "heif",
        "backend": "",
        "ext": ".heic",
        "bitdepth": 8,
        "engine_prefix": "x265:",
        "backend_label": "x265",
    },
    "aom.json": {
        "format": "avif",
        "backend": "libaom",
        "ext": ".avif",
        "bitdepth": 8,
        "engine_prefix": "aom:",
        "backend_label": "libaom",
    },
    "svt.json": {
        "format": "avif",
        "backend": "svt-av1",
        "ext": ".avif",
        "bitdepth": 8,
        "engine_prefix": "__svt:",
        "backend_label": "svt-av1",
    },
    "jxl.json": {
        "format": "jxl",
        "backend": "",
        "ext": ".jxl",
        "bitdepth": 16,
        "engine_prefix": "",
        "backend_label": "libjxl",
    },
}

# 每行 A/B 取值与前置键。未列出的行按 spec 类型走默认规则（见 choose_ab）。
#   a/b         两个变体的本行取值（None = 该变体不设本键 = "未设 vs 设"）
#   prereq      两个变体都带的前置键 [(key, value), ...]
#   a_params/b_params  仅 A / 仅 B 额外带的键（用于 expert_options 这类"门"参数）
#   meta        两个变体都带的 --meta（JXL box_compress 需要元数据 box 才可测）
#   note        归因提示（写进结果 JSON/报告）
OVERRIDES = {
    # ---------------- HEIF / x265（36 可设置）----------------
    "x265:crf": {"a": "10", "b": "45", "note": "RC 路径：crf 覆盖 quality 派生的 rfConstant"},
    "x265:qp": {"a": "10", "b": "45", "note": "RC 路径：qp 会切到 CQP（x265 param.cpp OPT(\"qp\")）"},
    "x265:qpmin": {"a": "0", "b": "40"},
    "x265:qpmax": {"a": "69", "b": "20"},
    "x265:aq-mode": {"a": "0", "b": "2"},
    "x265:aq-strength": {"a": "0.5", "b": "3.0", "prereq": [("x265:aq-mode", "2")]},
    "x265:qcomp": {"a": "0.5", "b": "1.0"},
    "x265:psy-rd": {"a": "0.5", "b": "4.0"},
    "x265:psy-rdoq": {"a": "0", "b": "20", "prereq": [("x265:rdoq-level", "2")]},
    "x265:rdoq-level": {"a": "0", "b": "2"},
    "x265:rd": {"a": "2", "b": "6"},
    "x265:rd-refine": {
        "a": "0",
        "b": "1",
        "prereq": [("x265:rd", "6"), ("x265:aq-mode", "2")],
    },
    "x265:cu-lossless": {"a": "0", "b": "1", "prereq": [("x265:rd", "4")]},
    "x265:ssim-rd": {"a": "0", "b": "1"},
    "x265:qg-size": {"a": "8", "b": "64"},
    "x265:cbqpoffs": {"a": "-12", "b": "12"},
    "x265:crqpoffs": {"a": "-12", "b": "12"},
    "x265:min-cu-size": {"a": "8", "b": "32"},
    "x265:max-tu-size": {"a": "4", "b": "32"},
    "x265:limit-tu": {"a": "0", "b": "4"},
    "x265:fast-intra": {"a": "0", "b": "1"},
    "x265:splitrd-skip": {"a": "0", "b": "1"},
    "x265:signhide": {"a": "0", "b": "1"},
    "x265:strong-intra-smoothing": {"a": "0", "b": "1"},
    "x265:tskip": {"a": "0", "b": "1", "prereq": [("x265:rd", "4")]},
    "x265:tskip-fast": {
        "a": "0",
        "b": "1",
        "prereq": [("x265:rd", "4"), ("x265:tskip", "1")],
    },
    "x265:lowpass-dct": {"a": "0", "b": "1"},
    "x265:sao": {"a": "0", "b": "1"},
    "x265:sao-non-deblock": {"a": "0", "b": "1", "prereq": [("x265:sao", "1")]},
    "x265:limit-sao": {"a": "0", "b": "1", "prereq": [("x265:sao", "1")]},
    "x265:selective-sao": {"a": "0", "b": "3", "prereq": [("x265:sao", "1")]},
    "x265:no-deblock": {"a": "0", "b": "1"},
    "x265:level-idc": {"a": "0", "b": "51"},
    "x265:high-tier": {"a": "0", "b": "1", "prereq": [("x265:level-idc", "51")]},
    # allow-non-conformance 是**许可开关**（只在 profile 会推导成 NONE 时阻塞/放行），
    # 常规合规编码下不改变码流 → 验"连线"用合规配置 A/B 同码流 + 源码证据归因。
    "x265:allow-non-conformance": {
        "a": "0",
        "b": "1",
        "note": "许可开关：仅 api.cpp:189 在 profile==NONE 时读取；合规配置下预期零码流差异",
    },
    "x265:wpp": {"a": "0", "b": "1"},
    # ---------------- AVIF / libaom（51 可设置）----------------
    "aom:cq-level": {"a": "10", "b": "55"},
    "aom:aq-mode": {"a": "0", "b": "3"},
    "aom:deltaq-mode": {"a": "0", "b": "4"},
    "aom:deltaq-strength": {
        "a": "0",
        "b": "1000",
        "prereq": [("aom:deltaq-mode", "4")],
    },
    "aom:enable-chroma-deltaq": {"a": "0", "b": "1"},
    "aom:enable-qm": {"a": "0", "b": "1"},
    "aom:qm-min": {"a": "0", "b": "15", "prereq": [("aom:enable-qm", "1")]},
    "aom:qm-max": {"a": "0", "b": "15", "prereq": [("aom:enable-qm", "1")]},
    "aom:sharpness": {"a": "0", "b": "7"},
    "aom:enable-adaptive-sharpness": {"a": "0", "b": "1"},
    "aom:dist-metric": {"a": "psnr", "b": "qm-psnr"},
    "aom:disable-trellis-quant": {"a": "0", "b": "3"},
    "aom:quant-b-adapt": {"a": "0", "b": "1"},
    "aom:min-cr": {"a": "0", "b": "100"},
    "aom:sb-qp-sweep": {"a": "0", "b": "1"},
    "aom:auto-intra-tools-off": {"a": "0", "b": "1"},
    "aom:enable-cdef": {"a": "0", "b": "2"},
    "aom:enable-restoration": {"a": "0", "b": "1"},
    "aom:loopfilter-control": {"a": "0", "b": "2"},
    "aom:enable-dual-filter": {"a": "0", "b": "1"},
    "aom:enable-rect-partitions": {"a": "0", "b": "1"},
    "aom:enable-ab-partitions": {"a": "0", "b": "1"},
    "aom:enable-1to4-partitions": {"a": "0", "b": "1"},
    "aom:min-partition-size": {"a": "4", "b": "64"},
    "aom:max-partition-size": {"a": "64", "b": "128"},
    "aom:sb-size": {"a": "64", "b": "128"},
    "aom:enable-intra-edge-filter": {"a": "0", "b": "1"},
    "aom:enable-tx64": {"a": "0", "b": "1"},
    "aom:enable-flip-idtx": {"a": "0", "b": "1"},
    "aom:enable-rect-tx": {"a": "0", "b": "1"},
    "aom:reduced-tx-type-set": {"a": "0", "b": "1"},
    "aom:use-intra-dct-only": {"a": "0", "b": "1"},
    "aom:use-intra-default-tx-only": {"a": "0", "b": "1"},
    "aom:enable-palette": {"a": "0", "b": "1"},
    "aom:enable-cfl-intra": {"a": "0", "b": "1"},
    "aom:enable-angle-delta": {"a": "0", "b": "1"},
    "aom:enable-filter-intra": {"a": "0", "b": "1"},
    "aom:enable-smooth-intra": {"a": "0", "b": "1"},
    "aom:enable-paeth-intra": {"a": "0", "b": "1"},
    "aom:enable-directional-intra": {"a": "0", "b": "1"},
    "aom:enable-diagonal-intra": {"a": "0", "b": "1"},
    "aom:tile-columns": {"a": "0", "b": "1"},
    "aom:tile-rows": {"a": "0", "b": "1"},
    "aom:row-mt": {
        "a": "0",
        "b": "1",
        "note": "纯线程并行开关；单 worker + 编码器内部线程关闭时预期不改变码流",
    },
    "aom:target-seq-level-idx": {"a": "0", "b": "13"},
    "aom:strict-level-conformance": {
        "a": "0",
        "b": "1",
        "prereq": [("aom:target-seq-level-idx", "13")],
    },
    "aom:set-tier-mask": {
        "a": "0",
        "b": "1",
        "prereq": [("aom:target-seq-level-idx", "13")],
    },
    "aom:force-video-mode": {"a": "0", "b": "1"},
    "aom:timing-info": {"a": "unspecified", "b": "constant"},
    "aom:cdf-update-mode": {"a": "0", "b": "2"},
    "aom:tune-content": {"a": "default", "b": "screen"},
    # ---------------- JPEG XL（16 可设置）----------------
    "noise": {
        "a": "0",
        "b": "1",
        "prereq": [("distance", "4")],
        "note": "旧式自适应噪声开关；dist=1 时库的 GetNoiseParameter 可能不建立噪声模型（合法 no-op），"
                "本探针前置 distance=4 以进入噪声建模路径（J16）",
    },
    "group_order_center_x": {
        "a": "0",
        "b": "100",
        "prereq": [("group_order", "1")],
        "note": "仅 group_order=center(1) 时有语义",
    },
    "group_order_center_y": {
        "a": "0",
        "b": "100",
        "prereq": [("group_order", "1")],
        "note": "仅 group_order=center(1) 时有语义",
    },
    "upsampling_mode": {
        "a": "0",
        "b": "1",
        "prereq": [("resampling", "2")],
        "note": "J3：仅 resampling=2/4/8 时有效",
    },
    "uses_original_profile": {"a": "0", "b": "1"},
    "box_compress": {
        "a": "0",
        "b": "1",
        "meta": ["Exif.Image.ImageDescription=probe box payload"],
        "note": "压缩元数据 box；需要有元数据 payload（--meta 注入）才可观察",
    },
    "expert_options": {
        "a": "0",
        "b": "1",
        "a_params": [("effort", "10")],
        "b_params": [("effort", "11")],
        "note": "唯一语义 = 放开 effort=11（J6）；A=effort10+B 关，B=effort11+B 开",
    },
    "buffering": {"a": "0", "b": "1", "note": "输入缓冲策略；小图一次性输入可能零码流差异"},
    "output_mode": {
        "a": "0",
        "b": "2",
        "note": "0=内部缓冲顺序写，2=乱序 jxlp（需 ftyp v1）；0 vs 1 在单帧小图下同码流，"
                "取 0 vs 2 观察容器模式差异",
    },
    "disable_perceptual_heuristics": {
        "a": "0",
        "b": "1",
        "prereq": [("uses_original_profile", "1")],
    },
    "intensity_target": {"a": "0", "b": "1000"},
    "min_nits": {"a": "0", "b": "10"},
    "relative_to_max_display": {"a": "0", "b": "1"},
    "linear_below": {"a": "0", "b": "100"},
    "intrinsic_size": {"a": None, "b": "320x240"},
    "frame_name": {"a": None, "b": "probe-frame"},
}

# 既有键（非 116 目录行）重点抽查：用户点名怀疑的 chroma/preset/tune 走这里，
# 单独统计、不混进 116 行口径。
EXTRA_CHECKS = [
    # (backend_key, format, backend, key, a, b, prereq, note)
    ("x265", "heif", "", "chroma", "420", "444", [], "既有键：HEIF 色度抽样"),
    ("x265", "heif", "", "chroma", "422", "444", [], "既有键：HEIF 色度抽样"),
    ("x265", "heif", "", "preset", "ultrafast", "veryslow", [], "既有键：x265 preset"),
    ("x265", "heif", "", "tune", "psnr", "grain", [], "既有键：x265 tune"),
    ("libaom", "avif", "libaom", "chroma", "420", "444", [], "既有键：AVIF(libaom) 色度抽样"),
    ("libaom", "avif", "libaom", "quality", "20", "90", [], "既有键：libaom 质量"),
    ("x265", "heif", "", "x265:crf", "10", "45", [], "透传代表：crf"),
    ("libaom", "avif", "libaom", "aom:cq-level", "10", "55", [], "透传代表：cq-level"),
    # JXL expert_options 的「门」语义证据：effort=11 只有允许专家选项后才被库接受 →
    # A 变体（expert=0）应被 libjxl 拒绝、B 变体（expert=1）成功。
    ("libjxl", "jxl", "", "expert_options", "0", "1", [("effort", "11")],
     "effort=11 门控：expert_options=0 时 libjxl 拒绝 effort=11，=1 时接受"),
]


# ---------------------------------------------------------------------------
# 基础工具
# ---------------------------------------------------------------------------

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def fmt_value_json(v):
    """spec 里的取值 → --param 文本（bool→1/0，数值直接，字符串原样）。"""
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, float):
        return ("%.10g" % v)
    return str(v)


def choose_ab(row):
    """默认 A/B：bool 0/1；数值 lo/hi；enum 首/末候选。text 必须由 OVERRIDES 给出。"""
    t = row.get("type")
    if t == "bool":
        return "0", "1"
    if t in ("int", "float"):
        lo, hi = row.get("lo"), row.get("hi")
        if lo is not None and hi is not None and float(hi) > float(lo):
            return fmt_value_json(lo), fmt_value_json(hi)
        return None, None
    if t == "enum":
        vals = [c[1] for c in row.get("choices", [])]
        if len(vals) >= 2:
            return fmt_value_json(vals[0]), fmt_value_json(vals[-1])
        return None, None
    return None, None


def load_specs(spec_dir):
    specs = []
    for name in ("x265.json", "aom.json", "svt.json", "jxl.json"):
        path = os.path.join(spec_dir, name)
        if not os.path.isfile(path):
            raise SystemExit("probe: 缺少 spec 文件 " + path)
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
        meta = SPEC_META[name]
        specs.append({"file": name, "doc": doc, "meta": meta})
    return specs


def read_text_best_effort(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


# ---------------------------------------------------------------------------
# PNG 解码（纯 Python / zlib）—— 仅 8bit 非隔行，供解码像素比较
# ---------------------------------------------------------------------------

def png_decode_rgb(path):
    """返回 (w, h, rgb_bytes)。支持 8bit、非隔行、color type 0/2/4/6。"""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: " + path)
    pos = 8
    w = h = bitdepth = colortype = interlace = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        ctype = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            w = int.from_bytes(payload[0:4], "big")
            h = int.from_bytes(payload[4:8], "big")
            bitdepth = payload[8]
            colortype = payload[9]
            interlace = payload[12]
        elif ctype == b"IDAT":
            idat += payload
        elif ctype == b"IEND":
            break
    if bitdepth != 8 or interlace != 0:
        raise ValueError("unsupported PNG (bitdepth=%r interlace=%r): %s"
                         % (bitdepth, interlace, path))
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colortype)
    if channels is None:
        raise ValueError("unsupported PNG colortype %r: %s" % (colortype, path))
    raw = zlib.decompress(bytes(idat))
    bpp = channels
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    rp = 0
    for y in range(h):
        ftype = raw[rp]
        rp += 1
        cur = bytearray(raw[rp:rp + stride])
        rp += stride
        if ftype == 1:  # Sub
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 0xFF
        elif ftype == 2:  # Up
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                cur[i] = (cur[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = cur
        prev = cur
    # → RGB
    rgb = bytearray(w * h * 3)
    if channels == 3:
        rgb[:] = out
    elif channels == 1:
        for i in range(w * h):
            g = out[i]
            rgb[3 * i:3 * i + 3] = bytes((g, g, g))
    elif channels == 2:
        for i in range(w * h):
            g = out[2 * i]
            rgb[3 * i:3 * i + 3] = bytes((g, g, g))
    else:  # 4
        for i in range(w * h):
            rgb[3 * i:3 * i + 3] = out[4 * i:4 * i + 3]
    return w, h, bytes(rgb)


def rgb_mae(a, b):
    """两幅同尺寸 RGB8 的逐样本平均绝对误差（0..255）。"""
    n = min(len(a), len(b))
    if n == 0:
        return None
    s = 0
    for i in range(n):
        d = a[i] - b[i]
        s += d if d >= 0 else -d
    return s / float(n)


def rgb_diff_percent(a, b):
    """逐样本差异占比（任一通道差 ≥1 即算差异样本）。"""
    n = min(len(a), len(b)) // 3
    if n == 0:
        return None
    diff = 0
    for i in range(n):
        j = 3 * i
        if a[j] != b[j] or a[j + 1] != b[j + 1] or a[j + 2] != b[j + 2]:
            diff += 1
    return 100.0 * diff / n


# ---------------------------------------------------------------------------
# 编码器调用
# ---------------------------------------------------------------------------

def encode(exe, fixture, out_dir, fmt, backend, bitdepth, params, metas, extra,
           log_dir, tag, timeout):
    """跑一次 --dev 编码；返回 dict（rc/hash/bytes/log）。"""
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(out_dir, exist_ok=True)
    cmd = [exe, "--dev", fixture, "--out", out_dir, "--format", fmt, "--workers", "1",
           "--log-level", "info"]
    if backend:
        cmd += ["--backend", backend]
    if bitdepth is not None:
        cmd += ["--bitdepth", str(bitdepth)]
    for k, v in params:
        cmd += ["--param", "%s=%s" % (k, v)]
    for m in metas:
        cmd += ["--meta", m]
    cmd += extra
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=timeout)
        rc = r.returncode
        stdout, stderr = r.stdout or "", r.stderr or ""
    except subprocess.TimeoutExpired as exc:
        rc = -999
        stdout = (exc.stdout or b"") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = (exc.stderr or b"") if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", "replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", "replace")
        stdout += "\n[probe] TIMEOUT after %.1fs\n" % timeout
    except OSError as exc:
        rc = -998
        stdout, stderr = "", "[probe] OSError: %s\n" % exc
    dt = time.time() - t0
    artifact = None
    if os.path.isdir(out_dir):
        for name in sorted(os.listdir(out_dir)):
            p = os.path.join(out_dir, name)
            if os.path.isfile(p) and not name.endswith(".pp.json") and \
                    name != "progress-trace.jsonl":
                artifact = p
                break
    sha = sha256_file(artifact) if artifact else None
    size = os.path.getsize(artifact) if artifact else None
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
        with open(os.path.join(log_dir, tag + ".stdout.log"), "w", encoding="utf-8") as f:
            f.write(stdout)
        with open(os.path.join(log_dir, tag + ".stderr.log"), "w", encoding="utf-8") as f:
            f.write(stderr)
    return {
        "rc": rc,
        "dt": round(dt, 2),
        "sha": sha,
        "bytes": size,
        "artifact": artifact,
        "stdout": stdout,
        "stderr": stderr,
        "cmd": cmd,
    }


def interesting_log_lines(res, key):
    """抓与参数/失败相关的日志行（用于归因证据）。"""
    text = (res.get("stdout") or "") + "\n" + (res.get("stderr") or "")
    needles = [key.lower(), (key.split(":", 1)[-1]).lower()]
    out = []
    for line in text.splitlines():
        low = line.lower()
        if any(n and n in low for n in needles):
            out.append(line.strip())
        elif any(w in low for w in ("rejected", "unsupported", "invalid", "ignored",
                                    "out of range", "not in the m8", "error")):
            out.append(line.strip())
    # 去掉重复、限制条数
    seen, uniq = set(), []
    for line in out:
        if line and line not in seen:
            seen.add(line)
            uniq.append(line)
    return uniq[:12]


def run_variant(exe, fixture, run_root, spec_meta, row_key, variant, params, metas,
                extra, timeout):
    fmt = spec_meta["format"]
    out_dir = os.path.join(run_root, sanitize(row_key), variant)
    log_dir = os.path.join(run_root, sanitize(row_key))
    return encode(exe, fixture, out_dir, fmt, spec_meta["backend"],
                  spec_meta["bitdepth"], params, metas, extra, log_dir, variant, timeout)


def sanitize(name):
    return re.sub(r"[^A-Za-z0-9._-]+", "_", name)


# ---------------------------------------------------------------------------
# 逐行判定
# ---------------------------------------------------------------------------

def evaluate_row(exe, fixture, run_root, spec, row, timeout):
    key = row["key"]
    meta = spec["meta"]
    result = {
        "spec": spec["file"],
        "format": meta["format"],
        "backend": meta["backend_label"],
        "key": key,
        "label": row.get("label", ""),
        "type": row.get("type"),
        "form": row.get("form"),
        "scope": row.get("scope"),
        "engine": row.get("engine"),
        "rules": row.get("rules", ""),
        "group": row.get("group", ""),
        "a": None,
        "b": None,
        "prereq": [],
        "status": None,
        "evidence": "",
        "a_sha": None,
        "b_sha": None,
        "a_bytes": None,
        "b_bytes": None,
        "a_rc": None,
        "b_rc": None,
        "a_dt": None,
        "b_dt": None,
        "log_lines": [],
        "note": "",
    }
    ov = OVERRIDES.get(key, {})
    a, b = ov.get("a"), ov.get("b")
    if "a" not in ov and "b" not in ov:
        a, b = choose_ab(row)
    prereq = [tuple(x) for x in ov.get("prereq", [])]
    metas = list(ov.get("meta", []))
    extra = list(ov.get("extra", []))
    result["a"], result["b"] = a, b
    result["prereq"] = ["%s=%s" % (k, v) for k, v in prereq]
    result["note"] = ov.get("note", "")
    if a is None and b is None:
        result["status"] = "ERROR"
        result["evidence"] = "无法为该类型行生成合法 A/B 取值（需在 OVERRIDES 补充）"
        return result

    a_params = list(prereq) + list(ov.get("a_params", []))
    b_params = list(prereq) + list(ov.get("b_params", []))
    if a is not None:
        a_params.append((key, a))
    if b is not None:
        b_params.append((key, b))

    ra = run_variant(exe, fixture, run_root, meta, key + "_A", "A", a_params, metas,
                     extra, timeout)
    rb = run_variant(exe, fixture, run_root, meta, key + "_B", "B", b_params, metas,
                     extra, timeout)
    result.update({
        "a_sha": ra["sha"], "b_sha": rb["sha"],
        "a_bytes": ra["bytes"], "b_bytes": rb["bytes"],
        "a_rc": ra["rc"], "b_rc": rb["rc"],
        "a_dt": ra["dt"], "b_dt": rb["dt"],
        "log_lines": (interesting_log_lines(ra, key) +
                      [l for l in interesting_log_lines(rb, key)
                       if l not in interesting_log_lines(ra, key)])[:16],
    })
    a_ok = ra["rc"] == 0 and ra["sha"] is not None
    b_ok = rb["rc"] == 0 and rb["sha"] is not None
    if a_ok and b_ok:
        if ra["sha"] != rb["sha"]:
            result["status"] = "EFFECT"
            result["evidence"] = (
                "A/B 产物字节 SHA256 不同（A=%s.. %sB, B=%s.. %sB）⇒ 参数改变码流/容器"
                % (ra["sha"][:16], ra["bytes"], rb["sha"][:16], rb["bytes"]))
        else:
            result["status"] = "NO_EFFECT"
            result["evidence"] = (
                "A/B 产物逐字节相同（SHA=%s.. %sB）⇒ 本配置下码流无变化；"
                "需按 note/源码归因（合法 no-op / 上游限制 / 真断线）"
                % (ra["sha"][:16], ra["bytes"]))
    elif a_ok and not b_ok:
        result["status"] = "EFFECT_REJECTED"
        result["evidence"] = (
            "B 变体被后端拒绝（rc=%s），A 成功 ⇒ 键/值确实被后端消费（拒绝即证据）；"
            "B 错误行：%s" % (rb["rc"], " | ".join(interesting_log_lines(rb, key)[:3])))
    elif b_ok and not a_ok:
        result["status"] = "EFFECT_REJECTED"
        result["evidence"] = (
            "A 变体被后端拒绝（rc=%s），B 成功 ⇒ 键/值确实被后端消费；A 错误行：%s"
            % (ra["rc"], " | ".join(interesting_log_lines(ra, key)[:3])))
    else:
        result["status"] = "ERROR"
        result["evidence"] = (
            "A/B 均失败或产物缺失（rcA=%s rcB=%s）；A 日志：%s；B 日志：%s"
            % (ra["rc"], rb["rc"],
               " | ".join(interesting_log_lines(ra, key)[:3]),
               " | ".join(interesting_log_lines(rb, key)[:3])))
    return result


def evaluate_extra(exe, fixture, run_root, item, timeout):
    backend_key, fmt, backend, key, a, b, prereq, note = item
    spec_meta = {"format": fmt, "backend": backend, "ext": None, "bitdepth": 8}
    res = {
        "spec": "(extra)",
        "format": fmt,
        "backend": backend_key,
        "key": key,
        "label": "既有键/代表抽查",
        "type": "extra",
        "form": "",
        "scope": "existing",
        "engine": key.split(":", 1)[-1] if ":" in key else key,
        "rules": "",
        "group": "",
        "a": a, "b": b,
        "prereq": ["%s=%s" % (k, v) for k, v in prereq],
        "status": None, "evidence": "", "note": note,
        "a_sha": None, "b_sha": None, "a_bytes": None, "b_bytes": None,
        "a_rc": None, "b_rc": None, "a_dt": None, "b_dt": None, "log_lines": [],
    }
    params_a = list(prereq) + [(key, a)]
    params_b = list(prereq) + [(key, b)]
    ra = run_variant(exe, fixture, run_root, spec_meta, "extra_" + key + "_A", "A",
                     params_a, [], [], timeout)
    rb = run_variant(exe, fixture, run_root, spec_meta, "extra_" + key + "_B", "B",
                     params_b, [], [], timeout)
    res.update({"a_sha": ra["sha"], "b_sha": rb["sha"], "a_bytes": ra["bytes"],
                "b_bytes": rb["bytes"], "a_rc": ra["rc"], "b_rc": rb["rc"],
                "a_dt": ra["dt"], "b_dt": rb["dt"]})
    if ra["rc"] == 0 and rb["rc"] == 0 and ra["sha"] != rb["sha"]:
        res["status"] = "EFFECT"
        res["evidence"] = ("A/B 产物 SHA 不同（%s.. vs %s..）⇒ 既有键真实生效"
                           % ((ra["sha"] or "")[:16], (rb["sha"] or "")[:16]))
    elif ra["rc"] == 0 and rb["rc"] == 0:
        res["status"] = "NO_EFFECT"
        res["evidence"] = "A/B 产物逐字节相同"
    elif ra["rc"] == 0 or rb["rc"] == 0:
        res["status"] = "EFFECT_REJECTED"
        res["evidence"] = "一变体被拒绝（rcA=%s rcB=%s）" % (ra["rc"], rb["rc"])
    else:
        res["status"] = "ERROR"
        res["evidence"] = "两变体均失败（rcA=%s rcB=%s）" % (ra["rc"], rb["rc"])
    return res


# ---------------------------------------------------------------------------
# 色度独立证据（用户点名怀疑面：HEIF 444 与 420）
# ---------------------------------------------------------------------------

def run_chroma_evidence(exe, fixture, evdir, timeout):
    """HEIF/AVIF chroma 420/422/444：产物字节 + 解码 PNG 像素 vs 源图。"""
    os.makedirs(evdir, exist_ok=True)
    src_w, src_h, src_rgb = png_decode_rgb(fixture)
    out = {
        "fixture": fixture,
        "fixture_size": [src_w, src_h],
        "source_sha": sha256_file(fixture),
        "backends": {},
    }
    for backend_label, fmt, backend in (("x265", "heif", ""),
                                        ("libaom", "avif", "libaom")):
        cases = {}
        decodes = {}
        for chroma in ("420", "422", "444"):
            tag = "%s_chroma%s" % (backend_label, chroma)
            enc_dir = os.path.join(evdir, tag + "_enc")
            log_dir = os.path.join(evdir, "logs")
            r = encode(exe, fixture, enc_dir, fmt, backend, 8, [("chroma", chroma)], [],
                       [], log_dir, tag + "_enc", timeout)
            if r["rc"] != 0 or not r["sha"]:
                cases[chroma] = {"error": "encode failed rc=%s" % r["rc"],
                                 "log": interesting_log_lines(r, "chroma")}
                continue
            dec_dir = os.path.join(evdir, tag + "_dec")
            d = encode(exe, r["artifact"], dec_dir, "png", "", 8, [], [], [],
                       log_dir, tag + "_dec", timeout)
            decoded_path = d["artifact"] if d["rc"] == 0 else None
            entry = {
                "product_bytes": r["bytes"],
                "product_sha": r["sha"],
                "encode_ms": r["dt"] * 1000.0,
            }
            if decoded_path:
                dw, dh, rgb = png_decode_rgb(decoded_path)
                entry["decoded_png"] = os.path.basename(decoded_path)
                entry["decoded_png_bytes"] = os.path.getsize(decoded_path)
                entry["decoded_png_sha"] = sha256_file(decoded_path)
                entry["decoded_size"] = [dw, dh]
                if (dw, dh) == (src_w, src_h):
                    entry["mae_vs_source"] = round(rgb_mae(rgb, src_rgb), 3)
                    decodes[chroma] = rgb
                else:
                    entry["mae_vs_source"] = None
                    entry["note"] = "解码尺寸与源不同，跳过像素比较"
            cases[chroma] = entry
        # 420 vs 444 解码像素差异
        if "420" in decodes and "444" in decodes:
            out.setdefault("pairwise", {})[backend_label] = {
                "diff_percent_420_vs_444": round(rgb_diff_percent(decodes["420"],
                                                                  decodes["444"]), 3),
                "mae_420_vs_444": round(rgb_mae(decodes["420"], decodes["444"]), 3),
            }
        out["backends"][backend_label] = cases
    return out


# ---------------------------------------------------------------------------
# 源锚点交叉核对（pinned 源码，只读）——引擎名是否在上游名字表里
# ---------------------------------------------------------------------------

def _brace_body(text, start_idx):
    """从 start_idx 起找第一个 '{' 并返回配平后的函数体（不含花括号）。"""
    i = text.find("{", start_idx)
    if i < 0:
        return ""
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
    return text[i + 1:]


def _find_pinned_file(bt_root, subdir, name, need_suffix=""):
    root = os.path.join(bt_root, subdir, "src")
    if not os.path.isdir(root):
        return None
    for base, dirs, files in os.walk(root):
        if name in files and base.endswith(need_suffix):
            return os.path.join(base, name)
    return None


def source_anchor_check(evdir=None):
    """从 pinned vcpkg 源码抽取上游真名表，与目录 engine 名做机械交叉核对。

    * x265：`x265_param_parse` 函数体内的 ``OPT("name")`` / ``OPT2("a","b")``
      —— 名不在表里 ⇒ param_parse 返回 -1 ⇒ libheif 插件把它变成硬错误。
    * libaom：``encoder_set_option``（av1_cx_iface.c）**实际消费**的
      ``g_av1_codec_arg_defs.<field>`` 集合，再按 arg_defs.c 的
      ``.field = ARG_DEF...(...)`` 取该选项的真名 —— 只有这些名字能被
      ``aom_codec_set_option`` 接受；不在集合里 ⇒ set_option 返回错误。
    """
    result = {"x265": {"available": False}, "libaom": {"available": False}}
    bt = os.path.join(ROOT, "vcpkg", "buildtrees")
    if not os.path.isdir(bt):
        return result

    x265_src = _find_pinned_file(bt, "x265", "param.cpp", os.path.join("common"))
    if x265_src:
        text = read_text_best_effort(x265_src)
        idx = text.find("int x265_param_parse")
        body = _brace_body(text, idx) if idx >= 0 else ""
        names = set(re.findall(r'\bOPT2?\(\s*"([^"]+)"', body))
        for a, b in re.findall(r'\bOPT2\(\s*"([^"]+)"\s*,\s*"([^"]+)"', body):
            names.add(a)
            names.add(b)
        result["x265"] = {
            "available": True,
            "source": rel(x265_src),
            "function": "x265_param_parse",
            "names": sorted(names),
        }

    aom_iface = _find_pinned_file(bt, "aom", "av1_cx_iface.c")
    aom_argdefs = _find_pinned_file(bt, "aom", "arg_defs.c")
    if aom_iface and aom_argdefs:
        iface_text = read_text_best_effort(aom_iface)
        idx = iface_text.find("static aom_codec_err_t encoder_set_option")
        body = _brace_body(iface_text, idx) if idx >= 0 else ""
        fields = set(re.findall(r"g_av1_codec_arg_defs\.(\w+)", body))
        ad = read_text_best_effort(aom_argdefs)
        consumed = set()
        for field in fields:
            m = re.search(r"\." + re.escape(field) + r"\s*=\s*ARG_DEF[_A-Z]*\(", ad)
            if not m:
                continue
            i = ad.find("(", m.start())
            depth = 0
            end = None
            for j in range(i, len(ad)):
                if ad[j] == "(":
                    depth += 1
                elif ad[j] == ")":
                    depth -= 1
                    if depth == 0:
                        end = j
                        break
            seg = ad[i:end + 1] if end is not None else ""
            consumed |= set(s for s in re.findall(r'"([a-z][a-z0-9-]*)"', seg))
        result["libaom"] = {
            "available": True,
            "source": rel(aom_iface) + " + " + rel(aom_argdefs),
            "function": "encoder_set_option → aom_codec_set_option",
            "names": sorted(consumed),
        }
    return result


def jxl_dispatch_surface():
    """解析 enc_jxl.cpp：目录键在编码器侧的两条下发面（frame settings / 特殊 API）。"""
    src_path = os.path.join(ROOT, "src", "codecs", "enc_jxl.cpp")
    src = read_text_best_effort(src_path)
    frame = set(re.findall(r'\{"([a-z0-9_]+)",\s*JXL_ENC_FRAME_SETTING_', src))
    m = re.search(r"kSpecialKeys\[\]\s*=\s*\{(.*?)\};", src, re.S)
    special = set(re.findall(r'"([a-z0-9_]+)"', m.group(1))) if m else set()
    return {
        "source": rel(src_path),
        "frame_settings": sorted(frame),
        "special_api": sorted(special),
        "known": sorted(frame | special),
    }


def static_audit(specs, anchors):
    """不跑编码的静态审计：目录行 ↔ 上游名字表 ↔ 编码器下发点 三层对账。"""
    audit = {"rows": {}, "jxl_dispatch": jxl_dispatch_surface(),
             "heif_passthrough": {
                 "source": "src/codecs/enc_heif.cpp",
                 "allowed_prefixes": {
                     "heif": "x265:",
                     "avif(libaom)": "aom:",
                     "avif(svt-av1)": "无透传通道",
                 },
             }}
    by_backend = {}
    for spec in specs:
        label = spec["meta"]["backend_label"]
        for row in spec["doc"]["rows"]:
            if row.get("scope") != "settable":
                continue
            engine = (row.get("engine") or "").strip()
            if not engine:
                continue
            a = by_backend.setdefault(label, {"checked": 0, "missing": []})
            a["checked"] += 1
            src = anchors.get(label)
            if not src or not src.get("available"):
                continue
            if engine not in set(src.get("names", [])):
                # x265_param_parse 对 "no-" 前缀有**通用取反处理**（pinned param.cpp：
                # `if (!strncmp(name, "no-", 3)) { name += 3; value = !value ...}`），
                # 因此 `no-deblock` 这类目录 engine 名合法 —— 判定为真名表子集。
                if engine.startswith("no-") and engine[3:] in set(src.get("names", [])):
                    continue
                a["missing"].append({"key": row["key"], "engine": engine,
                                     "reason": "engine 名不在 pinned 上游真名表里"})
    audit["rows"] = by_backend
    # JXL 目录键覆盖率
    jkeys = [r["key"] for s in specs if s["meta"]["backend_label"] == "libjxl"
             for r in s["doc"]["rows"] if r.get("scope") == "settable"]
    known = set(audit["jxl_dispatch"]["known"])
    audit["jxl_dispatch"]["catalog_keys"] = jkeys
    audit["jxl_dispatch"]["missing_from_dispatch"] = [k for k in jkeys if k not in known]
    return audit



def rel(path):
    try:
        return os.path.relpath(path, ROOT).replace("\\", "/")
    except ValueError:
        return path


def spec_engine_rows(specs):
    """[(backend_label, engine_name, key)]，只取可设置行。"""
    out = []
    for spec in specs:
        label = spec["meta"]["backend_label"]
        for row in spec["doc"]["rows"]:
            if row.get("scope") != "settable":
                continue
            engine = (row.get("engine") or "").strip()
            if engine:
                out.append((label, engine, row["key"]))
    return out


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def parse_args(argv):
    p = argparse.ArgumentParser(
        description="M8/W3 高级参数真实连线机械化探针（A/B 编码 → 产物 SHA256）")
    p.add_argument("--exe", default=DEFAULT_EXE, help="photopipeline.exe（默认 build/w3-param）")
    p.add_argument("--fixture", default=None,
                   help="输入 fixture（默认 build/tmp-m8fix/color.png，缺省回退 tests/golden/base）")
    p.add_argument("--out", default=EVIDENCE_DIR, help="证据输出目录")
    p.add_argument("--spec-dir", default=SPEC_DIR, help="参数目录 spec 目录")
    p.add_argument("--formats", default="",
                   help="只跑这些 format（逗号分隔：heif,avif,jxl）")
    p.add_argument("--backend", default="",
                   help="只跑这些 backend（逗号分隔：x265,libaom,svt-av1,libjxl）")
    p.add_argument("--only", default="",
                   help="只跑这些 key（逗号分隔；支持前缀匹配如 x265:）")
    p.add_argument("--timeout", type=float, default=180.0, help="单次编码超时秒（默认 180）")
    p.add_argument("--chroma-evidence", action="store_true",
                   help="只跑/追加色度独立证据（HEIF/AVIF 420/422/444 解码像素比较）")
    p.add_argument("--with-chroma-evidence", action="store_true",
                   help="跑行判定后再跑色度独立证据")
    p.add_argument("--skip-extra", action="store_true", help="跳过既有键代表性抽查")
    p.add_argument("--static-audit", action="store_true",
                   help="只跑静态审计（目录行 ↔ 上游名字表 ↔ 编码器下发点），不编码、不需 exe")
    p.add_argument("--keep-runs", action="store_true",
                   help="保留每次 A/B 的产物目录（默认保留；--no-keep-runs 才清理）")
    p.add_argument("--no-keep-runs", dest="keep_runs", action="store_false",
                   default=True, help="删除 runs 目录只留结果（不建议：报告要引用证据）")
    return p.parse_args(argv)


def pick_fixture(arg):
    if arg:
        if not os.path.isfile(arg):
            raise SystemExit("probe: fixture 不存在: " + arg)
        return os.path.abspath(arg)
    if os.path.isfile(DEFAULT_FIXTURE):
        return DEFAULT_FIXTURE
    for cand in FALLBACK_FIXTURES:
        if os.path.isfile(cand):
            return cand
    raise SystemExit("probe: 找不到 fixture；用 --fixture 指定")


def filter_match(filters, value):
    if not filters:
        return True
    return value in filters


def main(argv):
    args = parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except AttributeError:
        pass
    evdir = os.path.abspath(args.out)
    os.makedirs(evdir, exist_ok=True)
    specs = load_specs(args.spec_dir)
    anchors = source_anchor_check(evdir)

    # ---- 静态审计（不编码）：目录行 ↔ 上游名字表 ↔ 编码器下发点 ----
    audit = static_audit(specs, anchors)
    audit_path = os.path.join(evdir, "static_audit.json")
    with open(audit_path, "w", encoding="utf-8") as f:
        json.dump(audit, f, ensure_ascii=False, indent=2)
    if args.static_audit:
        print("=" * 78)
        print("M8/W3 高级参数静态审计（不编码）")
        for label, a in audit["rows"].items():
            print("  %-9s engine 交叉核对 checked=%d missing=%d %s"
                  % (label, a["checked"], len(a["missing"]),
                     json.dumps(a["missing"], ensure_ascii=False) if a["missing"] else ""))
        jd = audit["jxl_dispatch"]
        print("  libjxl   目录键 %d 个，下发面缺失 %s"
              % (len(jd["catalog_keys"]), jd["missing_from_dispatch"] or "无"))
        print("  产物：%s" % rel(audit_path))
        print("=" * 78)
        return 0

    exe = os.path.abspath(args.exe)
    if not os.path.isfile(exe):
        sys.stderr.write("probe: 找不到 photopipeline.exe: %s\n" % exe)
        sys.stderr.write("       先构建本任务树：python tools/env.py run -- cmake --build build/w3-param\n")
        return 2
    fixture = pick_fixture(args.fixture)
    runs_dir = os.path.join(evdir, "runs")
    os.makedirs(runs_dir, exist_ok=True)
    t_start = time.time()

    fmt_filter = [s.strip() for s in args.formats.split(",") if s.strip()]
    backend_filter = [s.strip() for s in args.backend.split(",") if s.strip()]
    key_filters = [s.strip() for s in args.only.split(",") if s.strip()]

    print("=" * 78)
    print("M8/W3 高级参数真实连线机械化探针")
    print("  exe     :", rel(exe))
    print("  exe mtime:", datetime.fromtimestamp(os.path.getmtime(exe)).isoformat(timespec="seconds"))
    print("  fixture :", rel(fixture), "size=%s" % (os.path.getsize(fixture),),
          "sha256=%s" % sha256_file(fixture)[:16])
    print("  out     :", rel(evdir))
    print("=" * 78)

    # ---- 0) 确定性对照：每个 (format,backend) 跑两次未设参数，必须同 SHA ----
    det = {}
    ctrl_combos = [("heif", "", "x265"), ("avif", "libaom", "libaom"), ("jxl", "", "libjxl")]
    for fmt, backend, label in ctrl_combos:
        if fmt_filter and fmt not in fmt_filter:
            continue
        if backend_filter and label not in backend_filter:
            continue
        r1 = encode(exe, fixture, os.path.join(runs_dir, "_ctrl_%s_A" % label), fmt,
                    backend, SPEC_META["jxl.json"]["bitdepth"] if fmt == "jxl" else 8,
                    [], [], [], os.path.join(runs_dir, "_ctrl_%s" % label), "A",
                    args.timeout)
        r2 = encode(exe, fixture, os.path.join(runs_dir, "_ctrl_%s_B" % label), fmt,
                    backend, SPEC_META["jxl.json"]["bitdepth"] if fmt == "jxl" else 8,
                    [], [], [], os.path.join(runs_dir, "_ctrl_%s" % label), "B",
                    args.timeout)
        ok = (r1["rc"] == 0 and r2["rc"] == 0 and r1["sha"] == r2["sha"])
        det[label] = {"ok": ok, "rc": [r1["rc"], r2["rc"]],
                      "sha": [r1["sha"], r2["sha"]], "dt": [r1["dt"], r2["dt"]]}
        print("[control] %-7s deterministic=%s  sha=%s  (%ss/%ss)"
              % (label, "YES" if ok else "NO", (r1["sha"] or "")[:16],
                 r1["dt"], r2["dt"]))
        if not ok:
            sys.stderr.write("probe: 警告 —— %s 未设参数两次编码结果不一致，"
                             "A/B 判定将不可靠（先排查环境污染）\n" % label)

    # ---- 1) 逐行判定 ----
    results = []
    readonly_rows = []
    spec_index = {}
    for spec in specs:
        fmt = spec["meta"]["format"]
        label = spec["meta"]["backend_label"]
        if fmt_filter and fmt not in fmt_filter:
            continue
        if backend_filter and label not in backend_filter:
            continue
        for row in spec["doc"]["rows"]:
            key = row["key"]
            if key_filters and not any(key == f or key.startswith(f) for f in key_filters):
                continue
            if row.get("scope") != "settable":
                readonly_rows.append({
                    "spec": spec["file"], "format": fmt, "backend": label,
                    "key": key, "label": row.get("label", ""),
                    "type": row.get("type"), "form": row.get("form"),
                    "scope": row.get("scope"), "engine": row.get("engine"),
                    "group": row.get("group", ""),
                    "status": "READONLY",
                    "evidence": "目录声明只读（上游固定/推导），不编码；不参与可设置行统计",
                })
                continue
            idx = len(results) + 1
            res = evaluate_row(exe, fixture, runs_dir, spec, row, args.timeout)
            results.append(res)
            spec_index[key] = res
            icon = {"EFFECT": "OK  ", "NO_EFFECT": "NO  ", "EFFECT_REJECTED": "REJ ",
                    "ERROR": "ERR "}.get(res["status"], "?   ")
            extra_note = ""
            if res["status"] in ("NO_EFFECT", "ERROR"):
                extra_note = "  <- " + res["evidence"][:80]
            print("[%3d] %-4s %-28s A=%-10s B=%-10s %s%s"
                  % (idx, icon, res["key"], str(res["a"]), str(res["b"]),
                     res["status"], extra_note))

    # ---- 2) 既有键代表性抽查 ----
    extras = []
    if not args.skip_extra and not key_filters:
        for item in EXTRA_CHECKS:
            backend_key, fmt, backend = item[0], item[1], item[2]
            label = backend_key
            if fmt_filter and fmt not in fmt_filter:
                continue
            if backend_filter and label not in backend_filter:
                continue
            r = evaluate_extra(exe, fixture, runs_dir, item, args.timeout)
            extras.append(r)
            print("[extra] %-4s %-24s %s" % (
                "OK  " if r["status"] == "EFFECT" else ("NO  " if r["status"] == "NO_EFFECT"
                                                        else r["status"]),
                r["key"], r["status"]))

    # ---- 3) 色度独立证据 ----
    chroma = None
    if args.chroma_evidence or args.with_chroma_evidence:
        print("[chroma] HEIF/AVIF 420/422/444 产物 + 解码像素比较 …")
        chroma = run_chroma_evidence(exe, fixture, os.path.join(evdir, "chroma"),
                                     args.timeout)
        for backend_label, cases in chroma.get("backends", {}).items():
            for c, entry in cases.items():
                print("  %-6s chroma=%-3s bytes=%-8s mae_vs_source=%s"
                      % (backend_label, c, entry.get("product_bytes"),
                         entry.get("mae_vs_source")))
            pw = chroma.get("pairwise", {}).get(backend_label, {})
            if pw:
                print("  %-6s 420 vs 444 解码像素：差异样本 %.2f%%，MAE %.3f"
                      % (backend_label, pw.get("diff_percent_420_vs_444", -1),
                         pw.get("mae_420_vs_444", -1)))

    # ---- 4) 汇总 + 落盘 ----
    by_status = {}
    by_backend = {}
    for r in results:
        by_status[r["status"]] = by_status.get(r["status"], 0) + 1
        b = r["backend"]
        by_backend.setdefault(b, {}).setdefault(r["status"], 0)
        by_backend[b][r["status"]] += 1
    for r in readonly_rows:
        by_backend.setdefault(r["backend"], {}).setdefault("READONLY", 0)
        by_backend[r["backend"]]["READONLY"] += 1

    summary = {
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "elapsed_s": round(time.time() - t_start, 1),
        "exe": rel(exe),
        "exe_sha256": sha256_file(exe),
        "fixture": rel(fixture),
        "fixture_sha256": sha256_file(fixture),
        "spec_dir": rel(os.path.abspath(args.spec_dir)),
        "rows_total": len(results) + len(readonly_rows),
        "settable_tested": len(results),
        "readonly": len(readonly_rows),
        "by_status": by_status,
        "by_backend": by_backend,
        "determinism": det,
        "anchors": {k: {kk: vv for kk, vv in v.items() if kk != "names"}
                    for k, v in anchors.items()},
    }

    # 引擎名交叉核对（复用静态审计结果；audit 已在上方落盘 static_audit.json）
    summary["anchor_cross_check"] = audit["rows"]
    summary["static_audit"] = rel(audit_path)

    doc = {
        "summary": summary,
        "rows": results,
        "readonly_rows": readonly_rows,
        "extra_checks": extras,
        "chroma_evidence": chroma,
        "static_audit": audit,
        "overrides": {k: v for k, v in OVERRIDES.items()},
    }
    json_path = os.path.join(evdir, "results.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, indent=2)
    csv_path = os.path.join(evdir, "results.csv")
    with open(csv_path, "w", encoding="utf-8", newline="") as f:
        f.write("spec,format,backend,key,scope,type,form,a,b,prereq,status,"
                "a_sha,b_sha,a_bytes,b_bytes,a_rc,b_rc,evidence\n")
        for r in results:
            f.write(",".join([
                r["spec"], r["format"], r["backend"], r["key"], r["scope"],
                r["type"] or "", r["form"] or "", csv_escape(r["a"]),
                csv_escape(r["b"]), csv_escape(";".join(r["prereq"])),
                r["status"], r["a_sha"] or "", r["b_sha"] or "",
                str(r["a_bytes"] or ""), str(r["b_bytes"] or ""),
                str(r["a_rc"]), str(r["b_rc"]),
                csv_escape(r["evidence"]),
            ]) + "\n")
        for r in readonly_rows:
            f.write(",".join([r["spec"], r["format"], r["backend"], r["key"],
                              r["scope"], r["type"] or "", r["form"] or "",
                              "", "", "", "READONLY", "", "", "", "", "", "",
                              csv_escape(r["evidence"])]) + "\n")
        for r in extras:
            f.write(",".join(["(extra)", r["format"], r["backend"], r["key"],
                              "existing", "extra", "", csv_escape(r["a"]),
                              csv_escape(r["b"]), "", r["status"],
                              r["a_sha"] or "", r["b_sha"] or "",
                              str(r["a_bytes"] or ""), str(r["b_bytes"] or ""),
                              str(r["a_rc"]), str(r["b_rc"]),
                              csv_escape(r["evidence"])]) + "\n")

    print("=" * 78)
    print("统计：可设置行 %d（EFFECT %d / NO_EFFECT %d / EFFECT_REJECTED %d / ERROR %d）"
          "  只读 %d" % (
              len(results), by_status.get("EFFECT", 0), by_status.get("NO_EFFECT", 0),
              by_status.get("EFFECT_REJECTED", 0), by_status.get("ERROR", 0),
              len(readonly_rows)))
    for b in sorted(by_backend):
        print("  %-9s %s" % (b, json.dumps(by_backend[b], ensure_ascii=False,
                                           sort_keys=True)))
    if extras:
        ex_status = {}
        for r in extras:
            ex_status[r["status"]] = ex_status.get(r["status"], 0) + 1
        print("  既有键抽查：%s" % json.dumps(ex_status, ensure_ascii=False,
                                             sort_keys=True))
    if audit["rows"]:
        print("  静态审计（engine 名/下发点）：%s"
              % json.dumps({k: {"checked": v["checked"], "missing": v["missing"]}
                            for k, v in audit["rows"].items()},
                           ensure_ascii=False, sort_keys=True))
        print("        %s" % rel(audit_path))
    print("  产物：%s" % rel(json_path))
    print("        %s" % rel(csv_path))
    print("  用时：%.1fs" % (time.time() - t_start))
    print("=" * 78)

    no_effect = [r for r in results if r["status"] in ("NO_EFFECT", "ERROR")]
    if no_effect:
        print("需归因的行（报告 docs/m8-param-wiring-audit.md 逐条说明）：")
        for r in no_effect:
            print("  - %-28s %-12s %s" % (r["key"], r["status"], r["evidence"][:110]))
    return 0


def csv_escape(v):
    if v is None:
        return ""
    s = str(v)
    if any(c in s for c in (",", "\"", "\n")):
        return "\"" + s.replace("\"", "\"\"") + "\""
    return s


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
