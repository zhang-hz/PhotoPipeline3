#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline 性能/输出链路机械探针（M10 / W1-T11，dev-only，不进 ctest）。

设计真源：docs/v0.4.0-design.md
  * §0.3 目标 SLO / §12.1 P4-1..P4-6、P1-1、P1-2、P9-1、多格式并行行
  * §4.6 逐输出事件（OutputDone）/ §5.3 状态机、§4.4 同文件多格式并行（B1）
证据面（只读解析，不伪造）：
  * `--dev` 运行日志（`<out>/logs/run-*.log`）：file done / output done / quality metric /
    pixel budget acquired / e3 report / run summary 行
  * sidecar `<产物>.pp.json`：`timing.*_ms` + `metric.ms` + `out_bytes`（SCHEMA.md，只读）
  * `progress-trace.jsonl`：逐事件 `t_ms`（HEIC 首次 Probing→Decoding、wall 兜底）
  * 外部 runner 的 `*.summary.txt`（wall/cpu/exit_line，recon 与 CI runner 同格式）
  三者任一缺失 → 对应探针 BLOCKED（列出缺字段与 W1 依赖），**绝不假绿**。

探针与阈值（逐条对齐设计 §12.1，报告里有对照表）：
  P4-1  单文件 22.8MP → JXL `--score`：metric_ms ≤ 0.6×encode_ms 且 wall ≤ 1.5×encode_ms+2000
  P4-3  8×22.8MP 评分批：Σmetric_ms / wall_ms ≥ 4
  P4-4  48MP PNG + `--score`：exit=0 且产物存在（评分可 unavailable，文件必须成功）
  P4-5  ≥150MB TIFF 输出（162MB 口径）：metawrite_ms ≤ 0.5×encode_ms 或 ≤800ms
  P4-6  22.8MP HEIC→JPEG：首次 Probing→Decoding ≤1000ms
  P1-1  同 P4-3 批：由 `pixel budget acquired` 时间戳算实际并发 ≥4
  P1-2  8×4MP→JPEG：peak_active_files ≥4 且 wall ≤600ms
  P9-1  同 P4-3 批：cpu_ms / wall_ms ≥ 6 核（CPU 需 runner 采样或本工具 --exec）
  MULTI4-WALL  1 文件 4 格式无评分：wall ≤15s（§12.1「多格式并行」）
  MULTI2-WALL  1 文件 2 格式 + 评分：wall ≤20s
  MULTI-SHA    并行产物 SHA 与串行路径逐字节相同（需 manifest 提供 serial_dir；否则 BLOCKED）
  P4-2（进度静默）不在本工具：见 tools/pp_progress_audit.py（设计 §12.1 P4-2）。

用法:
  python tools/perf_smoke.py --recon-corpus build/design-recon/pipeline      # 既有 34 轮证据
  python tools/perf_smoke.py --manifest build/m10/w1-tools/perf/runs.json    # CI 汇总断言
  python tools/perf_smoke.py --selftest                                      # 合成夹具正反例
  python tools/perf_smoke.py --gen-fixtures                                  # 只生成夹具
  python tools/perf_smoke.py --exec build/m10/w1-tools/runs/x --cwd . -- \
      build/release-dev/photopipeline.exe --dev <inputs> --out build/m10/w1-tools/runs/x \
      --format jxl --score --log-level info
  说明：--exec 只做「运行 + 测量 + 写 perf-summary.txt」；断言由随后的 --manifest/--recon-corpus
  解析完成（CI 里两段式，避免把测量失败误报成 SLO 失败）。

退出码: 0 = 全部探针 PASS；1 = 任一 FAIL；2 = 无 FAIL 但有 BLOCKED（证据不足，不得当绿）；
        3 = 用法/IO 错误；--selftest 时 0 = 全部用例符合预期。
机器可读行: PERF-RUN / PERF-THRESHOLD / PERF-PROBE / PERF-CHECK / PERF-INFO / PERF-SUMMARY /
        PERF-SELFTEST / PERF-EXEC（key=value，逐行可 grep）。
"""

import argparse
import copy
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES_DEFAULT = os.path.join(ROOT, "build", "m10", "w1-tools", "fixtures", "perf")

# ---------------------------------------------------------------------------
# 阈值单源（设计 docs/v0.4.0-design.md §12.1；改动必须同步设计与报告对照表）
# ---------------------------------------------------------------------------
P4_1_METRIC_RATIO = 0.6
P4_1_WALL_RATIO = 1.5
P4_1_WALL_SLOP_MS = 2000.0
P4_3_METRIC_PER_WALL = 4.0
P4_4_EXIT_CODE = 0
P4_5_META_RATIO = 0.5
P4_5_META_ABS_MS = 800.0
P4_5_MIN_OUT_BYTES = 150_000_000  # 162MB TIFF 样本口径（设计 §12.1 P4-5）
P4_6_HEIC_FIRST_DECODE_MS = 1000.0
P1_1_MIN_CONCURRENCY = 4
P1_2_MIN_PEAK_ACTIVE = 4
P1_2_MAX_WALL_MS = 600.0
P9_1_MIN_CORES = 6.0
MULTI4_MAX_WALL_MS = 15000.0
MULTI2_MAX_WALL_MS = 20000.0

PROBE_ORDER = [
    "P4-1",
    "P4-3",
    "P4-4",
    "P4-5",
    "P4-6",
    "P1-1",
    "P1-2",
    "P9-1",
    "MULTI4-WALL",
    "MULTI2-WALL",
    "MULTI-SHA",
]
PROBE_REF = {
    "P4-1": "§12.1/P4-1",
    "P4-3": "§12.1/P4-3",
    "P4-4": "§12.1/P4-4",
    "P4-5": "§12.1/P4-5",
    "P4-6": "§12.1/P4-6",
    "P1-1": "§12.1/P1-1",
    "P1-2": "§12.1/P1-2",
    "P9-1": "§12.1/P9-1",
    "MULTI4-WALL": "§12.1/多格式并行",
    "MULTI2-WALL": "§12.1/多格式并行",
    "MULTI-SHA": "§12.1/多格式并行",
}
PROBE_TITLE = {
    "P4-1": "单文件阶段预算 metric≤0.6×encode 且 wall≤1.5×encode+2000",
    "P4-3": "8×22.8MP 评分批 Σmetric/wall≥4",
    "P4-4": "48MP PNG+评分 exit=0 且产物存在",
    "P4-5": "162MB TIFF metawrite≤0.5×encode 或≤800ms",
    "P4-6": "HEIC 首次 Probing→Decoding≤1000ms",
    "P1-1": "评分批实际并发≥4（pixel budget acquired 时间戳）",
    "P1-2": "8×4MP 小图批 peak_active_files≥4 且 wall≤600ms",
    "P9-1": "评分批 cpu_ms/wall_ms≥6 核",
    "MULTI4-WALL": "1 文件 4 格式无评分 wall≤15s",
    "MULTI2-WALL": "1 文件 2 格式+评分 wall≤20s",
    "MULTI-SHA": "并行产物 SHA 与串行路径逐字节相同",
}

# 设计 §12.1 中不属于本工具的行（如实登记，避免误读为覆盖）
INFO_ROWS = [
    ("P4-2", "tools/pp_progress_audit.py", "进度静默 ≤500ms（本工具只解析运行日志，不判事件间隔）"),
    ("P9-2", "tests/unit/test_scheduler_contract", "alloc_threads_effective 穷举单测"),
    ("回归", "tools/regression.py", "workers=1 无评分/无自适应 zero diff"),
]

# 角色 → 既有 34 轮证据中的规范运行名（build/design-recon/pipeline/report.md §1.4 命名）
RECON_ROLE_CANDIDATES = {
    "single_score_jxl": ["info-single-jxl-score", "single-jxl-w0-score"],
    "score_batch": ["info-batch8-jpeg-w8-score", "batch8-jpeg-w8-score-gui-default"],
    "small_batch": ["mp4-batch8-w8-noscore"],
    "big48_score": ["big48-jpeg-score"],
    "tiff_meta": ["single-tiff16-noscore"],
    "heic_input": ["heic5-src-single-jpeg-noscore"],
    "multi4": ["single-multi4-noscore"],
    "multi2_score": ["single-multi2-score", "info-single-multi2-score"],
}


# ---------------------------------------------------------------------------
# 通用小工具
# ---------------------------------------------------------------------------


def _kv(value):
    """机器可读 value 的紧凑转义（空白/引号/等号包成单段；不引入 JSON 依赖，便于 grep）。"""
    text = str(value if value is not None else "")
    text = text.replace("\r", " ").replace("\n", " ").replace('"', "'")
    if text == "":
        return '""'
    if any(c.isspace() for c in text) or "=" in text:
        return '"%s"' % text
    return text


def _num(value):
    if isinstance(value, bool) or value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    try:
        return float(str(value).strip())
    except (TypeError, ValueError):
        return None


def _unescape_log_value(text):
    """日志引号值里的 `\\\\` → `\\`、`\\"` → `"`（logger 转义还原成真实路径）。"""
    return str(text).replace('\\\\', '\\').replace('\\"', '"')


RE_KV = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)=(?:"((?:[^"\\]|\\.)*)"|([^\s}]+))')


def parse_kv(text):
    """把 `k=v k2="v 2"` 解析成 dict；quoted 值保留 logger 内层转义（需要时再 unescape）。"""
    out = {}
    for match in RE_KV.finditer(text):
        out[match.group(1)] = match.group(2) if match.group(2) is not None else match.group(3)
    return out


RE_LOG_LINE = re.compile(
    r"^(\d{2}):(\d{2}):(\d{2})\.(\d{3}) \[[^\]]+\] \[\d+\] \[([^\]]+)\] \[([^\]]+)\] (.*)$"
)


def resolve_path(raw, base_dir, repo_root=ROOT):
    """把日志/sidecar 里的路径字段解析为可用路径；不存在时返回 best-guess（供存在性断言）。"""
    if not raw:
        return None
    text = _unescape_log_value(str(raw)).strip().replace('"', "")
    # sidecar/log 里的相对路径多相对仓库根；fixture 里多相对 run dir。
    candidates = []
    if os.path.isabs(text):
        candidates.append(text)
    else:
        candidates.extend(
            [os.path.join(repo_root, text), os.path.join(os.getcwd(), text), os.path.join(base_dir, text)]
        )
    for cand in candidates:
        cand = os.path.normpath(cand)
        if os.path.isfile(cand):
            return cand
    return os.path.normpath(candidates[0]) if candidates else None


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def rel_of(path, base_dir):
    try:
        rel = os.path.relpath(path, base_dir)
    except ValueError:
        return os.path.basename(path)
    return rel.replace("\\", "/")


# ---------------------------------------------------------------------------
# 探针结果模型
# ---------------------------------------------------------------------------


class Probe:
    def __init__(self, probe_id):
        self.probe_id = probe_id
        self.checks = []

    def add(self, name, value, threshold, status, detail="", source="", run=""):
        self.checks.append(
            {
                "name": name,
                "value": value,
                "threshold": threshold,
                "status": status,
                "detail": detail,
                "source": source,
                "run": run,
            }
        )
        return self

    def info(self, name, value, threshold="", detail="", source="", run=""):
        return self.add(name, value, threshold, "INFO", detail, source, run)

    def blocked(self, detail, run="", source="evidence"):
        return self.add("evidence", "missing", "required", "BLOCKED", detail, source, run)

    def status(self):
        statuses = [c["status"] for c in self.checks if c["status"] != "INFO"]
        if not statuses:
            return "BLOCKED"
        if "FAIL" in statuses:
            return "FAIL"
        if "BLOCKED" in statuses:
            return "BLOCKED"
        return "PASS"

    def counts(self):
        statuses = [c["status"] for c in self.checks if c["status"] != "INFO"]
        return (
            len(statuses),
            statuses.count("PASS"),
            statuses.count("FAIL"),
            statuses.count("BLOCKED"),
        )

    def lines(self, verbose_ok=False):
        total, passed, failed, blocked = self.counts()
        yield "PERF-PROBE probe=%s ref=%s status=%s checks=%d pass=%d fail=%d blocked=%d title=%s" % (
            self.probe_id,
            PROBE_REF.get(self.probe_id, ""),
            self.status(),
            total,
            passed,
            failed,
            blocked,
            _kv(PROBE_TITLE.get(self.probe_id, "")),
        )
        for check in self.checks:
            if check["status"] == "PASS" and not verbose_ok:
                continue
            yield (
                "PERF-CHECK probe=%s run=%s id=%s value=%s threshold=%s status=%s source=%s detail=%s"
                % (
                    self.probe_id,
                    _kv(check["run"] or "-"),
                    check["name"],
                    _kv(check["value"]),
                    _kv(check["threshold"]),
                    check["status"],
                    _kv(check["source"] or "-"),
                    _kv(check["detail"]),
                )
            )


# ---------------------------------------------------------------------------
# 日志 / sidecar / trace / summary 解析
# ---------------------------------------------------------------------------


def parse_log_text(text):
    """解析 --dev 运行日志 → 事实字典（只取探针需要行，未知行忽略）。"""
    facts = {
        "app_version": None,
        "run_start": {},
        "scheduler_start": {},
        "run_summary": {},
        "file_done": [],
        "output_done": [],
        "quality_metric": [],
        "budget_acquired": [],
        "e3": [],
        "log_lines": 0,
    }
    last_ms = None
    day_offset = 0
    for line in text.splitlines():
        match = RE_LOG_LINE.match(line)
        if not match:
            continue
        tod_ms = (
            (int(match.group(1)) * 3600 + int(match.group(2)) * 60 + int(match.group(3))) * 1000
            + int(match.group(4))
        )
        if last_ms is not None and tod_ms < last_ms - 12 * 3600 * 1000:
            day_offset += 86400000  # 跨午夜（长跑）
        last_ms = tod_ms
        ts = day_offset + tod_ms
        facts["log_lines"] += 1
        tag = match.group(5)
        msg = match.group(7).strip()
        if msg.startswith("version "):
            kv = parse_kv(msg[len("version "):])
            if kv.get("lib") == "app":
                facts["app_version"] = kv.get("version")
        elif msg.startswith("run start "):
            kv = parse_kv(msg[len("run start "):])
            kv["_ts"] = ts
            facts["run_start"] = kv
        elif msg.startswith("start ") and tag == "scheduler":
            kv = parse_kv(msg[len("start "):])
            kv["_ts"] = ts
            facts["scheduler_start"] = kv
        elif msg.startswith("file done "):
            kv = parse_kv(msg[len("file done "):])
            kv["_ts"] = ts
            facts["file_done"].append(kv)
        elif msg.startswith("output done "):
            kv = parse_kv(msg[len("output done "):])
            kv["_ts"] = ts
            facts["output_done"].append(kv)
        elif msg.startswith("quality metric "):
            kv = parse_kv(msg[len("quality metric "):])
            kv["_ts"] = ts
            facts["quality_metric"].append(kv)
        elif msg.startswith("pixel budget acquired "):
            kv = parse_kv(msg[len("pixel budget acquired "):])
            kv["_ts"] = ts
            facts["budget_acquired"].append(kv)
        elif msg.startswith("e3 report "):
            kv = parse_kv(msg[len("e3 report "):])
            kv["_ts"] = ts
            facts["e3"].append(kv)
        elif msg.startswith("run summary "):
            kv = parse_kv(msg[len("run summary "):])
            kv["_ts"] = ts
            facts["run_summary"] = kv
    return facts


def load_trace_events(path):
    """progress-trace.jsonl → (events, error)；只读取不修改。"""
    events = []
    if not path or not os.path.isfile(path):
        return events, "trace 不存在"
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for lineno, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError as exc:
                    return events, "第 %d 行 JSON 失败: %s" % (lineno, exc)
                if isinstance(obj, dict):
                    events.append(obj)
    except OSError as exc:
        return events, "读取失败: %s" % exc
    return events, ""


def parse_summary_file(path):
    """解析 runner summary（recon 的 results/<run>.summary.txt 与 perf_smoke --exec 同格式）。"""
    data = {}
    if not path or not os.path.isfile(path):
        return data
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if "=" not in line:
                continue
            key, _, value = line.partition("=")
            data[key.strip()] = value.strip()
    return data


class RunData:
    """一次 `--dev` 运行的证据容器（日志 + sidecar + trace + 可选 runner summary）。"""

    def __init__(
        self,
        name,
        run_dir,
        repo_root=ROOT,
        summary_path=None,
        overrides=None,
        roles=None,
        serial_dir=None,
        expected_outputs=None,
        notes="",
    ):
        self.name = name
        self.run_dir = os.path.abspath(run_dir)
        self.repo_root = repo_root
        self.summary_path = os.path.abspath(summary_path) if summary_path else None
        self.overrides = dict(overrides or {})
        self.roles = list(roles or [])
        self.serial_dir = os.path.abspath(serial_dir) if serial_dir else None
        self.expected_outputs = list(expected_outputs or [])
        self.notes = notes
        self.log_path = None
        self.log_facts = {}
        self.summary = {}
        self.sidecars = []
        self.sidecar_errors = []
        self.trace = []
        self.trace_error = ""
        self._loaded = False

    # -- 加载 --------------------------------------------------------------
    def load(self):
        if self._loaded:
            return self
        self.summary = parse_summary_file(self.summary_path)
        self._load_log()
        self._load_sidecars()
        self._load_trace()
        self._loaded = True
        return self

    def _load_log(self):
        override = self.overrides.get("log")
        if override:
            self.log_path = resolve_path(override, self.run_dir, self.repo_root)
        else:
            logs = glob.glob(os.path.join(self.run_dir, "logs", "*.log"))
            self.log_path = max(logs, key=os.path.basename) if logs else None
        text = ""
        if self.log_path and os.path.isfile(self.log_path):
            try:
                with open(self.log_path, "r", encoding="utf-8", errors="replace") as f:
                    text = f.read()
            except OSError:
                text = ""
        self.log_facts = parse_log_text(text)

    def _load_sidecars(self):
        for path in sorted(glob.glob(os.path.join(self.run_dir, "**", "*.pp.json"), recursive=True)):
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as f:
                    data = json.load(f)
            except (OSError, json.JSONDecodeError) as exc:
                self.sidecar_errors.append("%s: %s" % (path, exc))
                continue
            if isinstance(data, dict):
                self.sidecars.append({"path": path, "data": data})

    def _load_trace(self):
        trace_path = self.overrides.get("trace") or os.path.join(self.run_dir, "progress-trace.jsonl")
        if self.overrides.get("trace"):
            trace_path = resolve_path(trace_path, self.run_dir, self.repo_root)
        self.trace, self.trace_error = load_trace_events(trace_path)

    # -- 标量证据 ----------------------------------------------------------
    def wall(self):
        override = _num(self.overrides.get("wall_ms"))
        if override is not None:
            return override, "manifest.wall_ms"
        value = _num(self.summary.get("wall_ms"))
        if value is not None:
            return value, "summary.wall_ms"
        value = _num(self.log_facts.get("run_summary", {}).get("total_ms"))
        if value is not None:
            return value, "log.run_summary.total_ms"
        times = sorted(_num(ev.get("t_ms")) for ev in self.trace if _num(ev.get("t_ms")) is not None)
        if len(times) >= 2:
            return times[-1] - times[0], "trace.span"
        return None, ""

    def cpu(self):
        override = _num(self.overrides.get("cpu_ms"))
        if override is not None:
            return override, "manifest.cpu_ms"
        value = _num(self.summary.get("cpu_ms_sum_children"))
        if value is not None:
            return value, "summary.cpu_ms_sum_children"
        return None, ""

    def cpu_wall(self):
        """CPU 比值分母：--exec 记录了进程外壁钟时用 exec_wall_ms（与实测 CPU 同域，防包装器 CPU 造成假绿）。"""
        value = _num(self.summary.get("exec_wall_ms"))
        if value is not None:
            return value, "summary.exec_wall_ms"
        return self.wall()

    def cores(self):
        override = _num(self.overrides.get("cores"))
        if override is not None:
            return override, "manifest.cores"
        value = _num(self.summary.get("cores"))
        if value is not None:
            return value, "summary.cores"
        return float(os.cpu_count() or 0), "os.cpu_count"

    def exit_code(self):
        override = self.overrides.get("exit_code")
        if override is not None:
            return int(override), "manifest.exit_code"
        match = re.search(r"EXIT\s+(-?\d+)", str(self.summary.get("exit_line", "")))
        if match:
            return int(match.group(1)), "summary.exit_line"
        failed = _num(self.log_facts.get("run_summary", {}).get("failed"))
        if failed is not None:
            return int(failed), "log.run_summary.failed(派生)"
        return None, ""

    def n_files(self):
        for key in ("run_start", "run_summary", "scheduler_start"):
            value = _num(self.log_facts.get(key, {}).get("files"))
            if value is not None:
                return int(value)
        files = [ev.get("file") for ev in self.trace if isinstance(ev.get("file"), int)]
        if files:
            return max(files) + 1
        return None

    def log_version(self):
        return self.log_facts.get("app_version")

    def runner_args(self):
        return self.summary.get("args", "")

    # -- 逐输出证据 --------------------------------------------------------
    def sidecar_outputs(self):
        rows = []
        for item in self.sidecars:
            data = item["data"]
            timing = data.get("timing") or {}
            metric = data.get("metric") or {}
            rows.append(
                {
                    "sidecar": item["path"],
                    "rel": os.path.basename(item["path"])[: -len(".pp.json")],
                    "format": data.get("format", ""),
                    "out": data.get("out", ""),
                    "out_bytes": _num(data.get("out_bytes")),
                    "ok": bool(data.get("ok", data.get("file_ok", False))),
                    "encode_ms": _num(timing.get("encode_ms")),
                    "metawrite_ms": _num(timing.get("metawrite_ms")),
                    "decode_ms": _num(timing.get("decode_ms")),
                    "total_ms": _num(timing.get("total_ms")),
                    "metric_ms": _num(metric.get("ms")) if isinstance(metric, dict) else None,
                    "metric_state": metric.get("state") if isinstance(metric, dict) else None,
                    "width": _num((data.get("info") or {}).get("width")),
                    "height": _num((data.get("info") or {}).get("height")),
                }
            )
        return rows

    def scored_outputs(self):
        rows = [row for row in self.sidecar_outputs() if row["metric_state"] == "ok" and row["encode_ms"]]
        if rows:
            return rows, "sidecar"
        # 兜底：日志 quality metric + output done 配对（sidecar 缺失时）
        by_out = {}
        for fact in self.log_facts.get("output_done", []):
            raw = _unescape_log_value(fact.get("out", ""))
            by_out[raw] = _num(fact.get("encode_ms"))
        rows = []
        for fact in self.log_facts.get("quality_metric", []):
            if fact.get("state") != "ok":
                continue
            raw = _unescape_log_value(fact.get("out", ""))
            encode = by_out.get(raw)
            if encode is None:
                continue
            rows.append(
                {
                    "sidecar": raw,
                    "rel": os.path.basename(raw),
                    "format": fact.get("format", ""),
                    "out": raw,
                    "encode_ms": encode,
                    "metric_ms": _num(fact.get("ms")),
                    "metric_state": "ok",
                    "total_ms": None,
                }
            )
        return rows, "log.quality_metric"

    def metrics_sum(self):
        rows, source = self.scored_outputs()
        values = [row["metric_ms"] for row in rows if row["metric_ms"] is not None]
        if values:
            return sum(values), len(values), source
        value = 0.0
        count = 0
        for fact in self.log_facts.get("quality_metric", []):
            ms = _num(fact.get("ms"))
            if fact.get("state") == "ok" and ms is not None:
                value += ms
                count += 1
        return value, count, "log.quality_metric"

    def encode_sum(self):
        total = 0.0
        count = 0
        for fact in self.log_facts.get("output_done", []):
            value = _num(fact.get("encode_ms"))
            if value is not None:
                total += value
                count += 1
        if count:
            return total, count, "log.output_done"
        rows = self.sidecar_outputs()
        values = [row["encode_ms"] for row in rows if row["encode_ms"] is not None]
        return (sum(values), len(values), "sidecar.timing") if values else (0.0, 0, "")

    def metawrite_sum(self):
        total = 0.0
        count = 0
        for fact in self.log_facts.get("output_done", []):
            value = _num(fact.get("metawrite_ms"))
            if value is not None:
                total += value
                count += 1
        if count:
            return total, count, "log.output_done"
        rows = self.sidecar_outputs()
        values = [row["metawrite_ms"] for row in rows if row["metawrite_ms"] is not None]
        return (sum(values), len(values), "sidecar.timing") if values else (0.0, 0, "")

    def peak_active(self):
        facts = self.log_facts.get("e3", [])
        if facts:
            return _num(facts[-1].get("peak_active_files"))
        return None

    def peak_concurrency(self):
        """`pixel budget acquired`→`file done` 区间并集扫描 → 峰值在飞文件数（P1-1）。"""
        acquires = self.log_facts.get("budget_acquired", [])
        if not acquires:
            return None, "无 pixel budget acquired 行"
        done_ts = {}
        for fact in self.log_facts.get("file_done", []):
            raw = fact.get("src", "")
            if raw not in done_ts:
                done_ts[raw] = _num(fact.get("_ts"))
        times = [value for value in done_ts.values() if value is not None]
        summary_ts = _num(self.log_facts.get("run_summary", {}).get("_ts"))
        run_end = max(times) if times else None
        if summary_ts is not None:
            run_end = max(run_end or 0.0, summary_ts)
        points = []
        for fact in acquires:
            start = _num(fact.get("_ts"))
            if start is None:
                continue
            end = done_ts.get(fact.get("src", ""))
            if end is None or end < start:
                end = run_end if run_end is not None else start
            if end < start:
                end = start
            points.append((start, 1))
            points.append((end, -1))
        if not points:
            return None, "acquire 无时间戳"
        points.sort(key=lambda item: (item[0], item[1]))  # 同一时刻先释放再获取
        active = 0
        peak = 0
        for _, delta in points:
            active += delta
            peak = max(peak, active)
        return peak, "budget_acquired→file_done 区间"

    def artifacts(self):
        rows = []
        if self.expected_outputs:
            for rel in self.expected_outputs:
                path = resolve_path(rel, self.run_dir, self.repo_root)
                rows.append({"raw": rel, "path": path, "rel": rel.replace("\\", "/"), "exists": bool(path and os.path.isfile(path))})
            return rows
        raws = []
        for row in self.sidecar_outputs():
            if row["out"]:
                raws.append(row["out"])
        for fact in self.log_facts.get("output_done", []):
            raw = fact.get("out", "")
            if raw:
                raws.append(raw)
        seen = set()
        for raw in raws:
            path = resolve_path(raw, self.run_dir, self.repo_root)
            if not path or path in seen:
                continue
            seen.add(path)
            text = _unescape_log_value(raw)
            explicit_rel = any(row["out"] == raw for row in self.sidecar_outputs())
            rel = rel_of(path, self.run_dir) if not explicit_rel else os.path.normpath(text).replace("\\", "/")
            rows.append({"raw": raw, "path": path, "rel": rel, "exists": os.path.isfile(path)})
        return rows

    def trace_first_state(self, state):
        best = None
        for ev in self.trace:
            if str(ev.get("state", "")).strip().lower() != state:
                continue
            t = _num(ev.get("t_ms"))
            if t is None:
                continue
            if best is None or t < best[1]:
                best = (ev.get("file"), t)
        return best

    def trace_state_count(self, state, file_index=None):
        count = 0
        for ev in self.trace:
            if str(ev.get("state", "")).strip().lower() != state:
                continue
            if file_index is not None and ev.get("file") != file_index:
                continue
            count += 1
        return count


# ---------------------------------------------------------------------------
# 运行集合（recon 语料 / manifest / 夹具）
# ---------------------------------------------------------------------------


class RunCollection:
    def __init__(self, label, runs, roles=None):
        self.label = label
        self.runs = list(runs)
        self.roles = dict(roles or {})

    def load_all(self):
        for run in self.runs:
            run.load()
        return self

    def role(self, name):
        return self.roles.get(name)

    def run_names(self):
        return [run.name for run in self.runs]


def _apply_role_overrides(roles, overrides, runs_by_name):
    for item in overrides or []:
        if "=" not in item:
            raise SystemExit("--role 需要 ROLE=RUN 形式: %s" % item)
        role, _, run_name = item.partition("=")
        run = runs_by_name.get(run_name)
        if run is None:
            raise SystemExit("--role 指定的运行不存在: %s（可用: %s）" % (run_name, ", ".join(sorted(runs_by_name))))
        roles[role] = run
    return roles


def build_from_manifest(path, role_overrides=None):
    path = os.path.abspath(path)
    if not os.path.isfile(path):
        raise SystemExit("manifest 不存在: %s" % path)
    base = os.path.dirname(path)
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        manifest = json.load(f)
    runs = []
    for item in manifest.get("runs", []):
        run_dir = os.path.join(base, item["dir"]) if not os.path.isabs(item["dir"]) else item["dir"]
        summary = item.get("summary")
        summary_path = os.path.join(base, summary) if summary and not os.path.isabs(summary) else summary
        serial = item.get("serial_dir")
        serial_dir = os.path.join(base, serial) if serial and not os.path.isabs(serial) else serial
        overrides = {key: item[key] for key in ("wall_ms", "cpu_ms", "cores", "exit_code", "log", "trace") if key in item}
        runs.append(
            RunData(
                name=item.get("name") or os.path.basename(run_dir),
                run_dir=run_dir,
                summary_path=summary_path,
                overrides=overrides,
                roles=item.get("roles", []),
                serial_dir=serial_dir,
                expected_outputs=item.get("expected_outputs", []),
                notes=item.get("notes", ""),
            )
        )
    runs_by_name = {run.name: run for run in runs}
    roles = {}
    for run in runs:
        for role in run.roles:
            roles[role] = run
    _apply_role_overrides(roles, role_overrides, runs_by_name)
    return RunCollection(os.path.basename(path), runs, roles)


def build_from_recon(corpus, role_overrides=None):
    corpus = os.path.abspath(corpus)
    results = os.path.join(corpus, "results")
    summaries = sorted(glob.glob(os.path.join(results, "*.summary.txt")))
    runs = []
    for summary_path in summaries:
        data = parse_summary_file(summary_path)
        name = data.get("name") or os.path.basename(summary_path)[: -len(".summary.txt")]
        out_raw = data.get("out", "")
        run_dir = resolve_path(out_raw, corpus) if out_raw else os.path.join(corpus, "out", name)
        runs.append(
            RunData(
                name=name,
                run_dir=run_dir,
                summary_path=summary_path,
                notes="recon 34 轮证据（build/design-recon/pipeline/results）",
            )
        )
    runs_by_name = {run.name: run for run in runs}
    roles = {}
    for role, candidates in RECON_ROLE_CANDIDATES.items():
        for candidate in candidates:
            if candidate in runs_by_name:
                roles[role] = runs_by_name[candidate]
                break
    _apply_role_overrides(roles, role_overrides, runs_by_name)
    return RunCollection(os.path.basename(corpus), runs, roles)


# ---------------------------------------------------------------------------
# 探针实现（每个 P* 一条；缺证据 → BLOCKED，从不默认 PASS）
# ---------------------------------------------------------------------------


def probe_p4_1(rc):
    probe = Probe("P4-1")
    run = rc.role("single_score_jxl")
    if run is None:
        return probe.blocked("缺 role single_score_jxl：需要 22.8MP TIFF→JXL 的 --dev --score 运行")
    rows, source = run.scored_outputs()
    if not rows:
        return probe.blocked("无 sidecar metric（评分未开启/sidecar 缺失）", run=run.name)
    wall, wall_source = run.wall()
    if wall is None:
        return probe.blocked("无 wall 证据（summary/log total_ms/trace 全缺）", run=run.name)
    for row in rows:
        encode = row["encode_ms"]
        metric = row["metric_ms"]
        if not encode or metric is None:
            probe.blocked("sidecar 缺 timing.encode_ms 或 metric.ms", run=run.name)
            continue
        ratio = metric / encode
        on = "PASS" if ratio <= P4_1_METRIC_RATIO else "FAIL"
        probe.add(
            "metric_ratio",
            "%.3f" % ratio,
            "<=%.3f" % P4_1_METRIC_RATIO,
            on,
            "metric=%.1fms encode=%.1fms" % (metric, encode),
            source + ".metric.ms/.timing.encode_ms",
            run.name,
        )
        limit = P4_1_WALL_RATIO * encode + P4_1_WALL_SLOP_MS
        on = "PASS" if wall <= limit else "FAIL"
        file_wall = row.get("total_ms")
        probe.add(
            "wall",
            "%.1f" % wall,
            "<=%.1f" % limit,
            on,
            "file_total_ms=%s wall_source=%s" % ("%.1f" % file_wall if file_wall else "n/a", wall_source),
            wall_source,
            run.name,
        )
    size = rows[0].get("width"), rows[0].get("height")
    if size[0] and size[1]:
        probe.info("resolution", "%dx%d" % (size[0], size[1]), "≈22.8MP（3902x5854）", "设计 P4-1 素材口径")
    return probe


def probe_p4_3(rc):
    probe = Probe("P4-3")
    run = rc.role("score_batch")
    if run is None:
        return probe.blocked("缺 role score_batch：需要 8×22.8MP TIFF→JPEG 的 --dev --score 批")
    metric_sum, metric_count, metric_source = run.metrics_sum()
    if metric_count == 0:
        return probe.blocked("批运行无评分输出（sidecar metric / log quality metric 全缺）", run=run.name)
    wall, wall_source = run.wall()
    if wall is None:
        return probe.blocked("批运行无 wall 证据", run=run.name)
    n_files = run.n_files()
    if n_files is not None and n_files != 8:
        probe.add("batch_files", str(n_files), "8", "BLOCKED", "非 8 文件批，不能替代 8×22.8MP 口径", "log.run_start.files", run.name)
    ratio = metric_sum / wall
    on = "PASS" if ratio >= P4_3_METRIC_PER_WALL else "FAIL"
    probe.add(
        "metric_per_wall",
        "%.3f" % ratio,
        ">=%.1f" % P4_3_METRIC_PER_WALL,
        on,
        "Σmetric=%.1fms(%d) wall=%.1fms metric_source=%s wall_source=%s"
        % (metric_sum, metric_count, wall, metric_source, wall_source),
        "Σmetric/wall",
        run.name,
    )
    return probe


def probe_p4_4(rc):
    probe = Probe("P4-4")
    run = rc.role("big48_score")
    if run is None:
        return probe.blocked("缺 role big48_score：需要 48MP PNG + --score 的运行")
    exit_code, exit_source = run.exit_code()
    if exit_code is None:
        return probe.blocked("无 exit code 证据（需 runner 捕获 EXIT 行或 perf_smoke --exec）", run=run.name)
    on = "PASS" if exit_code == P4_4_EXIT_CODE else "FAIL"
    probe.add("exit_code", str(exit_code), "==%d" % P4_4_EXIT_CODE, on, "source=%s" % exit_source, exit_source, run.name)
    artifacts = run.artifacts()
    if not artifacts:
        return probe.blocked("无期望产物路径证据（sidecar.out / log output done / manifest 全缺）", run=run.name)
    missing = [row for row in artifacts if not row["exists"]]
    on = "PASS" if not missing else "FAIL"
    probe.add(
        "artifacts",
        "%d/%d present" % (len(artifacts) - len(missing), len(artifacts)),
        "all present",
        on,
        "missing=%s" % (", ".join(row["rel"] for row in missing[:4]) or "-"),
        "file existence",
        run.name,
    )
    return probe


def probe_p4_5(rc):
    probe = Probe("P4-5")
    run = rc.role("tiff_meta")
    if run is None:
        return probe.blocked("缺 role tiff_meta：需要 162MB TIFF 输出的运行")
    rows = [
        row
        for row in run.sidecar_outputs()
        if row["encode_ms"] and row["metawrite_ms"] is not None and (row["out_bytes"] or 0) >= P4_5_MIN_OUT_BYTES
    ]
    if not rows:
        return probe.blocked(
            "缺 ≥%dMB 输出样本（sidecar.out_bytes 不足或 sidecar 缺失）" % (P4_5_MIN_OUT_BYTES // 1_000_000),
            run=run.name,
        )
    for row in rows:
        encode = row["encode_ms"]
        metawrite = row["metawrite_ms"]
        ratio = metawrite / encode
        ok = (ratio <= P4_5_META_RATIO) or (metawrite <= P4_5_META_ABS_MS)
        probe.info("out_bytes", "%d" % row["out_bytes"], ">=%d" % P4_5_MIN_OUT_BYTES, "162MB TIFF 样本口径", "sidecar.out_bytes", run.name)
        probe.add(
            "metawrite_bound",
            "meta=%.1fms ratio=%.3f" % (metawrite, ratio),
            "ratio<=%.1f OR meta<=%.0fms" % (P4_5_META_RATIO, P4_5_META_ABS_MS),
            "PASS" if ok else "FAIL",
            "encode=%.1fms" % encode,
            "sidecar.timing.metawrite_ms/encode_ms",
            run.name,
        )
    return probe


def probe_p4_6(rc):
    probe = Probe("P4-6")
    run = rc.role("heic_input")
    if run is None:
        return probe.blocked("缺 role heic_input：需要 22.8MP HEIC→JPEG 的运行")
    if not run.trace:
        return probe.blocked("无 progress-trace 事件（%s）" % run.trace_error, run=run.name)
    first_probing = run.trace_first_state("probing")
    if first_probing is None:
        return probe.blocked("trace 无 probing 事件", run=run.name)
    file_index, probe_t = first_probing
    first_decoding = None
    for ev in run.trace:
        if str(ev.get("state", "")).strip().lower() != "decoding":
            continue
        if ev.get("file") != file_index:
            continue
        t = _num(ev.get("t_ms"))
        if t is None:
            continue
        if first_decoding is None or t < first_decoding:
            first_decoding = t
    if first_decoding is None:
        return probe.blocked("trace 无 decoding 事件（file=%s）" % file_index, run=run.name)
    delta = first_decoding - probe_t
    on = "PASS" if delta <= P4_6_HEIC_FIRST_DECODE_MS else "FAIL"
    probe.add(
        "probe_to_decode",
        "%.1f" % delta,
        "<=%.0f" % P4_6_HEIC_FIRST_DECODE_MS,
        on,
        "probing@%.1f decoding@%.1fms file=%s" % (probe_t, first_decoding, file_index),
        "trace.first(probing→decoding)",
        run.name,
    )
    probe.info(
        "probe_events",
        str(run.trace_state_count("probing", file_index)),
        "1（无双重 probe）",
        "HEAD 双重 probe 证据；消除依赖 W1-T3",
        "trace.state=probing 计数",
        run.name,
    )
    return probe


def probe_p1_1(rc):
    probe = Probe("P1-1")
    run = rc.role("score_batch")
    if run is None:
        return probe.blocked("缺 role score_batch：需要 8×22.8MP 评分批")
    peak, source = run.peak_concurrency()
    if peak is None:
        return probe.blocked("无 pixel budget acquired 时间戳证据（%s）" % source, run=run.name)
    on = "PASS" if peak >= P1_1_MIN_CONCURRENCY else "FAIL"
    probe.add("peak_concurrency", str(peak), ">=%d" % P1_1_MIN_CONCURRENCY, on, source, "log.budget_acquired/file_done", run.name)
    return probe


def probe_p1_2(rc):
    probe = Probe("P1-2")
    run = rc.role("small_batch")
    if run is None:
        return probe.blocked("缺 role small_batch：需要 8×4MP→JPEG（无评分）运行")
    peak = run.peak_active()
    if peak is None:
        probe.blocked("无 e3 report peak_active_files（日志缺失）", run=run.name)
    else:
        on = "PASS" if peak >= P1_2_MIN_PEAK_ACTIVE else "FAIL"
        probe.add(
            "peak_active_files",
            "%d" % peak,
            ">=%d" % P1_2_MIN_PEAK_ACTIVE,
            on,
            source="log.e3_report",
            run=run.name,
        )
    wall, wall_source = run.wall()
    if wall is None:
        probe.blocked("无 wall 证据", run=run.name)
    else:
        on = "PASS" if wall <= P1_2_MAX_WALL_MS else "FAIL"
        probe.add("wall", "%.1f" % wall, "<=%.0f" % P1_2_MAX_WALL_MS, on, "source=%s" % wall_source, wall_source, run.name)
    return probe


def probe_p9_1(rc):
    probe = Probe("P9-1")
    run = rc.role("score_batch")
    if run is None:
        return probe.blocked("缺 role score_batch：需要 8×22.8MP 评分批")
    cpu, cpu_source = run.cpu()
    wall, wall_source = run.cpu_wall()
    if cpu is None:
        return probe.blocked(
            "无 CPU 时间证据：--dev 日志不含 CPU；需 runner summary（cpu_ms_sum_children）或 perf_smoke --exec",
            run=run.name,
        )
    if wall is None:
        return probe.blocked("无 wall 证据", run=run.name)
    cores = cpu / wall
    on = "PASS" if cores >= P9_1_MIN_CORES else "FAIL"
    probe.add(
        "cpu_cores",
        "%.3f" % cores,
        ">=%.1f" % P9_1_MIN_CORES,
        on,
        "cpu=%.1fms cpu_wall=%.1fms cpu_source=%s wall_source=%s"
        % (cpu, wall, cpu_source, wall_source),
        "cpu_ms/cpu_wall_ms",
        run.name,
    )
    return probe


def _multi_wall_probe(rc, probe_id, role, limit_ms):
    probe = Probe(probe_id)
    run = rc.role(role)
    if run is None:
        return probe.blocked("缺 role %s：多格式并行运行" % role)
    wall, wall_source = run.wall()
    if wall is None:
        return probe.blocked("无 wall 证据", run=run.name)
    on = "PASS" if wall <= limit_ms else "FAIL"
    probe.add("wall", "%.1f" % wall, "<=%.0f" % limit_ms, on, "source=%s" % wall_source, wall_source, run.name)
    return probe


def probe_multi_sha(rc):
    probe = Probe("MULTI-SHA")
    runs = [rc.role("multi4"), rc.role("multi2_score")]
    runs = [run for run in runs if run is not None]
    if not runs:
        return probe.blocked("缺 role multi4/multi2_score：多格式并行运行")
    for run in runs:
        if not run.serial_dir:
            probe.blocked(
                "%s 缺串行基线（manifest.serial_dir 未提供）：需 W1-T6 双跑并落 SHA 基线" % run.name,
                run=run.name,
            )
            continue
        artifacts = run.artifacts()
        if not artifacts:
            probe.blocked("无产物路径证据", run=run.name)
            continue
        for row in artifacts:
            rel = row["rel"]
            serial_path = os.path.join(run.serial_dir, rel.replace("/", os.sep))
            if not row["exists"]:
                probe.add("sha_artifact", "missing", "exists", "FAIL", "并行产物缺失 rel=%s" % rel, "file existence", run.name)
                continue
            if not os.path.isfile(serial_path):
                probe.add(
                    "sha_baseline",
                    "missing",
                    "exists",
                    "BLOCKED",
                    "串行基线缺失 rel=%s（需 W1-T6 双跑）" % rel,
                    "manifest.serial_dir",
                    run.name,
                )
                continue
            sha_par = sha256_file(row["path"])
            sha_ser = sha256_file(serial_path)
            on = "PASS" if sha_par == sha_ser else "FAIL"
            probe.add(
                "sha_equal",
                sha_par[:12],
                "==%s" % sha_ser[:12],
                on,
                "rel=%s" % rel,
                "sha256",
                run.name,
            )
    return probe


def run_probes(rc):
    rc.load_all()
    return [
        probe_p4_1(rc),
        probe_p4_3(rc),
        probe_p4_4(rc),
        probe_p4_5(rc),
        probe_p4_6(rc),
        probe_p1_1(rc),
        probe_p1_2(rc),
        probe_p9_1(rc),
        _multi_wall_probe(rc, "MULTI4-WALL", "multi4", MULTI4_MAX_WALL_MS),
        _multi_wall_probe(rc, "MULTI2-WALL", "multi2_score", MULTI2_MAX_WALL_MS),
        probe_multi_sha(rc),
    ]


# ---------------------------------------------------------------------------
# 输出（机器可读行）
# ---------------------------------------------------------------------------


def print_thresholds():
    print("PERF-THRESHOLD probe=P4-1 id=metric_ratio threshold=%s ref=%s" % (P4_1_METRIC_RATIO, PROBE_REF["P4-1"]))
    print(
        "PERF-THRESHOLD probe=P4-1 id=wall threshold=%sx_encode+%dms ref=%s"
        % (P4_1_WALL_RATIO, P4_1_WALL_SLOP_MS, PROBE_REF["P4-1"])
    )
    print("PERF-THRESHOLD probe=P4-3 id=metric_per_wall threshold=>=%s ref=%s" % (P4_3_METRIC_PER_WALL, PROBE_REF["P4-3"]))
    print("PERF-THRESHOLD probe=P4-4 id=exit_code threshold===%d ref=%s" % (P4_4_EXIT_CODE, PROBE_REF["P4-4"]))
    print(
        "PERF-THRESHOLD probe=P4-5 id=metawrite threshold=%sx_encode_OR_%dms ref=%s"
        % (P4_5_META_RATIO, P4_5_META_ABS_MS, PROBE_REF["P4-5"])
    )
    print("PERF-THRESHOLD probe=P4-6 id=probe_to_decode threshold=%dms ref=%s" % (P4_6_HEIC_FIRST_DECODE_MS, PROBE_REF["P4-6"]))
    print("PERF-THRESHOLD probe=P1-1 id=peak_concurrency threshold=>=%d ref=%s" % (P1_1_MIN_CONCURRENCY, PROBE_REF["P1-1"]))
    print(
        "PERF-THRESHOLD probe=P1-2 id=peak_active_files threshold=>=%d id=wall threshold=%dms ref=%s"
        % (P1_2_MIN_PEAK_ACTIVE, P1_2_MAX_WALL_MS, PROBE_REF["P1-2"])
    )
    print("PERF-THRESHOLD probe=P9-1 id=cpu_cores threshold=>=%s ref=%s" % (P9_1_MIN_CORES, PROBE_REF["P9-1"]))
    print("PERF-THRESHOLD probe=MULTI4-WALL id=wall threshold=%dms ref=%s" % (MULTI4_MAX_WALL_MS, PROBE_REF["MULTI4-WALL"]))
    print("PERF-THRESHOLD probe=MULTI2-WALL id=wall threshold=%dms ref=%s" % (MULTI2_MAX_WALL_MS, PROBE_REF["MULTI2-WALL"]))
    print("PERF-THRESHOLD probe=MULTI-SHA id=sha256 threshold=equal ref=%s" % PROBE_REF["MULTI-SHA"])


def print_runs(rc):
    for run in rc.runs:
        wall, wall_source = run.wall()
        cpu, cpu_source = run.cpu()
        exit_code, exit_source = run.exit_code()
        metric_sum, metric_count, _ = run.metrics_sum()
        encode_sum, encode_count, _ = run.encode_sum()
        meta_sum, meta_count, _ = run.metawrite_sum()
        peak_concurrency, _ = run.peak_concurrency()
        print(
            "PERF-RUN name=%s files=%s wall_ms=%s wall_src=%s cpu_ms=%s cpu_src=%s cores=%s exit=%s exit_src=%s "
            "sidecars=%d metric_sum_ms=%.1f metric_n=%d encode_sum_ms=%.1f encode_n=%d meta_sum_ms=%.1f meta_n=%d "
            "peak_active=%s peak_concurrency=%s trace_events=%d log_lines=%d version=%s"
            % (
                run.name,
                run.n_files() if run.n_files() is not None else "-",
                "%.1f" % wall if wall is not None else "-",
                wall_source or "-",
                "%.0f" % cpu if cpu is not None else "-",
                cpu_source or "-",
                "%.0f" % run.cores()[0] if run.cores()[0] else "-",
                exit_code if exit_code is not None else "-",
                exit_source or "-",
                len(run.sidecars),
                metric_sum,
                metric_count,
                encode_sum,
                encode_count,
                meta_sum,
                meta_count,
                "%d" % run.peak_active() if run.peak_active() is not None else "-",
                peak_concurrency if peak_concurrency is not None else "-",
                len(run.trace),
                run.log_facts.get("log_lines", 0),
                run.log_version() or "-",
            )
        )


def overall_exit(probes):
    statuses = [probe.status() for probe in probes]
    if any(status == "FAIL" for status in statuses):
        return 1
    if any(status == "BLOCKED" for status in statuses):
        return 2
    return 0


def print_probes(probes, verbose=False):
    for probe in probes:
        for line in probe.lines(verbose_ok=verbose):
            print(line)
    for probe_id, owner, note in INFO_ROWS:
        print("PERF-INFO probe=%s owner=%s status=NOT-EVALUATED note=%s" % (probe_id, owner, _kv(note)))


def print_summary(probes, rc):
    statuses = [probe.status() for probe in probes]
    print(
        "PERF-SUMMARY corpus=%s runs=%d probes=%d pass=%d fail=%d blocked=%d exit=%d"
        % (
            _kv(rc.label),
            len(rc.runs),
            len(probes),
            statuses.count("PASS"),
            statuses.count("FAIL"),
            statuses.count("BLOCKED"),
            overall_exit(probes),
        )
    )


def write_json(path, probes, rc):
    payload = {
        "schema": "pp-perf-smoke-v1",
        "corpus": rc.label,
        "runs": [
            {
                "name": run.name,
                "run_dir": run.run_dir,
                "wall_ms": run.wall()[0],
                "cpu_ms": run.cpu()[0],
                "exit_code": run.exit_code()[0],
                "sidecars": len(run.sidecars),
            }
            for run in rc.runs
        ],
        "probes": [
            {
                "probe": probe.probe_id,
                "status": probe.status(),
                "checks": probe.checks,
            }
            for probe in probes
        ],
        "exit": overall_exit(probes),
    }
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
    print("PERF-JSON path=%s" % os.path.abspath(path))


# ---------------------------------------------------------------------------
# 合成夹具（--selftest / --gen-fixtures）：
#   真实文件 + 与 --dev 日志/sidecar/trace/summary 同格式的合成证据，逐条正反例。
# ---------------------------------------------------------------------------


AREA = (3902, 5854)  # 22,842,308 px = 22.8MP（设计素材口径）


def _out(rel, fmt, out_bytes=1024, **extra):
    row = {
        "rel": rel,
        "format": fmt,
        "out_bytes": out_bytes,
        "width": AREA[0],
        "height": AREA[1],
        "ok": True,
        "write_artifact": True,
    }
    row.update(extra)
    return row


def _default_acquires_dones(n_files, wall_ms):
    acquires = [float(i * 10) for i in range(n_files)]
    dones = [float(wall_ms - 200) for _ in range(n_files)]
    return acquires, dones


def _base_specs():
    specs = {}
    acquires, dones = _default_acquires_dones(1, 3200.0)
    specs["single-jxl-score"] = {
        "n_files": 1,
        "out_specs": [_out("T1_0.jxl", "jxl", 4009535)],
        "score": True,
        "encode_ms": 1000.0,
        "metric_ms": 500.0,
        "metawrite_ms": 0.5,
        "wall_ms": 3200.0,
        "cpu_ms": None,
        "cores": 16,
        "exit_code": 0,
        "e3_peak_active": 1,
        "acquires": acquires,
        "dones": dones,
        "trace_span_ms": 3200.0,
        "write_summary": False,
    }
    specs["batch8-score"] = {
        "n_files": 8,
        "out_specs": [_out("T1_%d.jpg" % i, "jpeg", 4583044) for i in range(8)],
        "score": True,
        "encode_ms": 500.0,
        "metric_ms": 1000.0,
        "metawrite_ms": 20.0,
        "wall_ms": 1900.0,
        "cpu_ms": 12000.0,
        "cores": 16,
        "exit_code": 0,
        "e3_peak_active": 4,
        "acquires": [0.0, 10.0, 20.0, 30.0, 900.0, 910.0, 920.0, 930.0],
        "dones": [899.0, 899.0, 899.0, 899.0, 1800.0, 1800.0, 1800.0, 1800.0],
        "trace_span_ms": 1900.0,
        "write_summary": True,
    }
    specs["batch8-small-noscore"] = {
        "n_files": 8,
        "out_specs": [_out("M4_%d.jpg" % i, "jpeg", 23884, write_artifact=True) for i in range(8)],
        "score": False,
        "encode_ms": 60.0,
        "metric_ms": None,
        "metawrite_ms": 0.01,
        "wall_ms": 550.0,
        "cpu_ms": 1094.0,
        "cores": 16,
        "exit_code": 0,
        "e3_peak_active": 4,
        "trace_span_ms": 550.0,
        "write_summary": False,
    }
    specs["big48-score"] = {
        "n_files": 1,
        "out_specs": [_out("big48mp.jpg", "jpeg", 123456, width=8000, height=6000)],
        "score": True,
        "encode_ms": 900.0,
        "metric_ms": 300.0,
        "metawrite_ms": 0.01,
        "wall_ms": 1200.0,
        "cpu_ms": 3000.0,
        "cores": 16,
        "exit_code": 0,
        "e3_peak_active": 1,
        "trace_span_ms": 1200.0,
        "write_summary": False,
        "expected_outputs": ["big48mp.jpg"],
    }
    specs["tiff-meta"] = {
        "n_files": 1,
        "out_specs": [_out("T1_0.tif", "tiff", 162328000)],
        "score": False,
        "encode_ms": 1000.0,
        "metric_ms": None,
        "metawrite_ms": 780.0,  # ratio 0.78 不达标但绝对 ≤800ms 达标（验证 OR 两支）
        "wall_ms": 2000.0,
        "cpu_ms": 2000.0,
        "cores": 16,
        "exit_code": 0,
        "e3_peak_active": 1,
        "trace_span_ms": 2000.0,
        "write_summary": False,
    }
    specs["heic-input"] = {
        "n_files": 1,
        "out_specs": [_out("T5.jpg", "jpeg", 1024)],
        "score": False,
        "encode_ms": 500.0,
        "metric_ms": None,
        "metawrite_ms": 0.01,
        "wall_ms": 1500.0,
        "cpu_ms": 1500.0,
        "cores": 16,
        "exit_code": 0,
        "e3_peak_active": 1,
        "heic_probe_to_decode_ms": 400.0,
        "trace_span_ms": 1500.0,
        "write_summary": False,
    }
    specs["multi4-noscore"] = {
        "n_files": 1,
        "out_specs": [
            _out("jpeg/T1_0.jpg", "jpeg", 4583044),
            _out("webp/T1_0.webp", "webp", 4868734),
            _out("jxl/T1_0.jxl", "jxl", 4009535),
            _out("heif/T1_0.heic", "heif", 1941390),
        ],
        "score": False,
        "encode_ms": 2000.0,
        "metric_ms": None,
        "metawrite_ms": 20.0,
        "wall_ms": 14000.0,
        "cpu_ms": 40000.0,
        "cores": 16,
        "exit_code": 0,
        "e3_peak_active": 1,
        "trace_span_ms": 14000.0,
        "write_summary": False,
        "serial": True,
        "serial_identical": True,
    }
    specs["multi2-score"] = {
        "n_files": 1,
        "out_specs": [_out("jpeg/T1_0.jpg", "jpeg", 4583044), _out("webp/T1_0.webp", "webp", 4868734)],
        "score": True,
        "encode_ms": 2000.0,
        "metric_ms": 500.0,
        "metawrite_ms": 20.0,
        "wall_ms": 19000.0,
        "cpu_ms": 35000.0,
        "cores": 16,
        "exit_code": 0,
        "e3_peak_active": 1,
        "trace_span_ms": 19000.0,
        "write_summary": False,
        "serial": True,
        "serial_identical": True,
    }
    return specs


ROLE_TO_RUN = {
    "single_score_jxl": "single-jxl-score",
    "score_batch": "batch8-score",
    "small_batch": "batch8-small-noscore",
    "big48_score": "big48-score",
    "tiff_meta": "tiff-meta",
    "heic_input": "heic-input",
    "multi4": "multi4-noscore",
    "multi2_score": "multi2-score",
}
RUN_TO_ROLES = {}
for _role, _run in ROLE_TO_RUN.items():
    RUN_TO_ROLES.setdefault(_run, []).append(_role)


def _outputs_for_file(spec, file_index):
    if spec["n_files"] == 1:
        return list(range(len(spec["out_specs"])))
    return [file_index]


def _trace_events(spec):
    events = []
    seq = 0

    def emit(file_index, t_ms, state, stage, overall, output_index=-1):
        nonlocal seq
        events.append(
            {
                "seq": seq,
                "file": file_index,
                "state": state,
                "stage": stage,
                "output_index": output_index,
                "stage_frac": 1.0 if state == "done" else 0.5,
                "overall_frac": overall,
                "synthetic": False,
                "t_ms": round(t_ms, 1),
            }
        )
        seq += 1

    span = float(spec.get("trace_span_ms") or spec["wall_ms"])
    n_files = spec["n_files"]
    step = span / (n_files + 1)
    for file_index in range(n_files):
        base = file_index * step
        emit(file_index, 0.5 + base, "probing", "Probe", 0.0)
        decode_delta = spec.get("heic_probe_to_decode_ms")
        decode_t = (0.5 + decode_delta) if decode_delta is not None else 20.0
        emit(file_index, decode_t, "decoding", "Decode", 0.05)
        emit(file_index, decode_t + 10.0, "progress", "Encode", 0.4, output_index=0)
        for out_index in _outputs_for_file(spec, file_index):
            out_t = decode_t + 20.0 + out_index * 5.0
            emit(file_index, min(out_t, span - 20.0), "output_done", "Done", 0.6 + 0.1 * out_index, output_index=out_index)
        emit(file_index, span - 10.0, "progress", "Done", 1.0)
        emit(file_index, span, "done", "Done", 1.0)
    return events


def _log_timestamp(offset_ms):
    total = int(round(offset_ms))
    hours = (10 + total // 3600000) % 24
    minutes = (total // 60000) % 60
    seconds = (total // 1000) % 60
    millis = total % 1000
    return "%02d:%02d:%02d.%03d" % (hours, minutes, seconds, millis)


def _write_run_files(case_root, run_name, spec, serial_identical=True):
    run_dir = os.path.join(case_root, "runs", run_name)
    os.makedirs(os.path.join(run_dir, "logs"), exist_ok=True)
    n_files = spec["n_files"]
    wall = float(spec["wall_ms"])
    acquires = spec.get("acquires") or [float(i * 10) for i in range(n_files)]
    dones = spec.get("dones") or [wall - 100.0 for _ in range(n_files)]
    lines = []

    def log_line(offset_ms, tag, file_tag, msg):
        lines.append((float(offset_ms), _log_timestamp(offset_ms), tag, file_tag, msg))

    log_line(0.0, "run", "dev_cli.cpp", "version {lib=app version=0.4.0.selftest}")
    log_line(
        1.0,
        "run",
        "dev_cli.cpp",
        'run start {outputs=%s output_count=%d files=%d workers=%d out="%s" params="synthetic"}'
        % ("+".join(row["format"] for row in spec["out_specs"]), len(spec["out_specs"]), n_files, n_files, run_name),
    )
    log_line(
        2.0,
        "scheduler",
        "scheduler.cpp",
        "start {files=%d workers=%d workers_resolved=%d stagger_ms=0 thread_budget=%d outputs=%d}"
        % (n_files, n_files, n_files, spec["cores"], len(spec["out_specs"])),
    )
    for file_index in range(n_files):
        out_indices = _outputs_for_file(spec, file_index)
        done_at = float(dones[file_index])
        log_line(float(acquires[file_index]), "pipeline", "pipeline.cpp",
                 'pixel budget acquired {src="corpus/T%d.tif" need_bytes=4202984672 capacity_bytes=8589934592 used_bytes=0}'
                 % file_index)
        encode_sum = 0.0
        for out_index in out_indices:
            row = spec["out_specs"][out_index]
            encode_ms = float(row.get("encode_ms", spec["encode_ms"]))
            metawrite_ms = float(row.get("metawrite_ms", spec["metawrite_ms"]))
            encode_sum += encode_ms
            if spec["score"] and spec.get("metric_ms") is not None:
                log_line(
                    done_at - 40.0,
                    "pipeline",
                    "pipeline.cpp",
                    'quality metric {src="corpus/T%d.tif" out="%s" format=%s state=ok score=92.5 ms=%.1f note=""}'
                    % (file_index, row["rel"], row["format"], spec["metric_ms"]),
                )
            log_line(
                done_at - 20.0,
                "pipeline",
                "pipeline.cpp",
                'output done {src="corpus/T%d.tif" out="%s" format=%s ok=true bytes=%d encode_ms=%.1f metawrite_ms=%.1f warnings=0}'
                % (file_index, row["rel"], row["format"], int(row["out_bytes"]), encode_ms, metawrite_ms),
            )
        log_line(
            done_at,
            "pipeline",
            "pipeline.cpp",
            "file done {src=\"corpus/T%d.tif\" outputs=%d ok_outputs=%d bytes=0 decode_ms=10 orient_ms=0 color_ms=0 "
            "flatten_ms=0 encode_ms=%.1f metawrite_ms=%.1f verdict=Done warnings=0}"
            % (file_index, len(out_indices), len(out_indices), encode_sum, spec["metawrite_ms"]),
        )
    log_line(wall - 5.0, "scheduler", "scheduler.cpp",
             "budget report {capacity_bytes=8589934592 peak_bytes=0 used_bytes=0}")
    log_line(wall - 3.0, "scheduler", "scheduler.cpp",
             "e3 report {thread_budget=%d stagger_ms=0 peak_active_files=%d peak_sum_encode_threads=%d active_files_end=0 files_done=%d}"
             % (spec["cores"], spec["e3_peak_active"], spec["e3_peak_active"], n_files))
    log_line(wall - 2.0, "run", "dev_cli.cpp",
             "run summary {files=%d ok=%d failed=0 skipped=0 cancelled=0 bytes=0 total_ms=%.1f throughput_mb_s=0 avg_file_ms=%.1f}"
             % (n_files, n_files, wall, wall / n_files))
    lines.sort(key=lambda item: (item[0], item[1]))
    with open(os.path.join(run_dir, "logs", "run-0001.log"), "w", encoding="utf-8", newline="\n") as f:
        for _, ts, tag, file_tag, msg in lines:
            f.write("%s [info] [1234] [%s] [%s] %s\n" % (ts, tag, file_tag, msg))

    # sidecar + 产物 + 串行基线
    serial_root = os.path.join(case_root, "serial", run_name)
    for out_index, row in enumerate(spec["out_specs"]):
        out_path = os.path.join(run_dir, row["rel"].replace("/", os.sep))
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        if row.get("write_artifact", True):
            payload = ("%s|%s|%s" % (case_root, run_name, row["rel"])).encode("utf-8")
            with open(out_path, "wb") as f:
                f.write(payload)
        encode_ms = float(row.get("encode_ms", spec["encode_ms"]))
        metawrite_ms = float(row.get("metawrite_ms", spec["metawrite_ms"]))
        sidecar = {
            "format": row["format"],
            "ok": bool(row.get("ok", True)),
            "file_ok": bool(row.get("ok", True)),
            "file_state": "ok" if row.get("ok", True) else "failed",
            "out": row["rel"],
            "out_bytes": int(row["out_bytes"]),
            "out_bytes_total": int(row["out_bytes"]),
            "outputs_total": len(spec["out_specs"]),
            "info": {
                "channels": 3,
                "format": "tiff",
                "has_alpha": False,
                "height": row["height"],
                "is_multipage": False,
                "src_bitdepth": 16,
                "width": row["width"],
            },
            "timing": {
                "color_ms": 0,
                "decode_ms": 10.0,
                "encode_ms": encode_ms,
                "flatten_ms": 0,
                "metawrite_ms": metawrite_ms,
                "orient_ms": 0,
                "total_ms": wall,
            },
            "params": "synthetic-selftest",
            "progress_max_row": 0,
            "progress_reported": False,
            "skipped": False,
            "src": "corpus/T0.tif",
            "warnings": [],
        }
        if spec["score"] and spec.get("metric_ms") is not None and row.get("ok", True):
            sidecar["metric"] = {
                "colorspace": "sRGB",
                "ms": float(spec["metric_ms"]),
                "name": "ssimulacra2",
                "note": "",
                "reference": "encoder-input",
                "score": 92.5,
                "state": "ok",
                "version": 1,
            }
        with open(out_path + ".pp.json", "w", encoding="utf-8", newline="\n") as f:
            json.dump(sidecar, f, ensure_ascii=False, indent=2)
        if spec.get("serial"):
            serial_path = os.path.join(serial_root, row["rel"].replace("/", os.sep))
            os.makedirs(os.path.dirname(serial_path), exist_ok=True)
            payload = ("%s|%s|%s" % (case_root, run_name, row["rel"])).encode("utf-8")
            if not serial_identical:
                payload = payload + b"-serial-diff"
            with open(serial_path, "wb") as f:
                f.write(payload)

    # progress-trace.jsonl
    with open(os.path.join(run_dir, "progress-trace.jsonl"), "w", encoding="utf-8", newline="\n") as f:
        for ev in _trace_events(spec):
            f.write(json.dumps(ev, ensure_ascii=False, sort_keys=True) + "\n")

    # runner summary（同 recon results/<run>.summary.txt 格式）
    summary_rel = None
    if spec.get("write_summary"):
        summary_rel = "summaries/%s.summary.txt" % run_name
        summary_path = os.path.join(case_root, summary_rel)
        os.makedirs(os.path.dirname(summary_path), exist_ok=True)
        exit_line = "EXIT %d (failed files)" % int(spec["exit_code"])
        cpu_ms = float(spec["cpu_ms"]) if spec.get("cpu_ms") is not None else 0.0
        with open(summary_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("name=%s\n" % run_name)
            f.write("out=%s\n" % run_dir)
            f.write("wall_ms=%.1f\n" % wall)
            f.write("cpu_ms_sum_children=%.0f\n" % cpu_ms)
            f.write("cpu_pct_of_all_cores=%.1f\n" % (100.0 * cpu_ms / wall / spec["cores"] if wall else 0.0))
            f.write("cores=%d\n" % spec["cores"])
            f.write("sys_load_pct_before=0.0\n")
            f.write("sys_load_pct_after=0.0\n")
            f.write("exit_line=%s\n" % exit_line)
            f.write("log=%s\n" % os.path.join(run_dir, "logs", "run-0001.log"))
            f.write("args=--dev synthetic-fixture\n")
    return summary_rel


def build_case_fixtures(case_root, specs, include_runs, serial_identical=True):
    case_root = os.path.abspath(case_root)
    guard = os.path.join("build", "m10", "w1-tools", "fixtures")
    if guard.replace("\\", "/") not in case_root.replace("\\", "/"):
        raise SystemExit("拒绝写入非 build/m10/w1-tools/fixtures 下的夹具: %s" % case_root)
    if os.path.isdir(case_root):
        shutil.rmtree(case_root)
    os.makedirs(case_root, exist_ok=True)
    runs_meta = []
    for run_name in include_runs:
        spec = specs[run_name]
        if run_name in ("multi4-noscore", "multi2-score"):
            serial_identical_local = spec.get("serial_identical", True)
        else:
            serial_identical_local = serial_identical
        summary_rel = _write_run_files(case_root, run_name, spec, serial_identical=serial_identical_local)
        entry = {"name": run_name, "dir": "runs/" + run_name, "roles": RUN_TO_ROLES.get(run_name, [])}
        if summary_rel:
            entry["summary"] = summary_rel
        if spec.get("serial"):
            entry["serial_dir"] = "serial/" + run_name
        if run_name == "big48-score":
            entry["expected_outputs"] = spec.get("expected_outputs", [])
        if not spec.get("write_summary"):
            entry["wall_ms"] = float(spec["wall_ms"])
        if spec.get("cpu_ms") is not None and not spec.get("write_summary"):
            entry["cpu_ms"] = float(spec["cpu_ms"])
        entry["cores"] = spec["cores"]
        entry["exit_code"] = int(spec["exit_code"])
        runs_meta.append(entry)
    manifest = {
        "schema": "pp-perf-runs-v1",
        "label": "selftest:" + os.path.basename(case_root),
        "note": "合成夹具（tools/perf_smoke.py --selftest 生成；尺寸/字节数值为解析用合成值）",
        "runs": runs_meta,
    }
    with open(os.path.join(case_root, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    return case_root


SELFTEST_CASES = [
    # (case 名, 覆盖 spec, 只含 runs（None=全量）, 串行基线一致?, 期望 exit, 期望探针状态)
    ("pass_all", {}, None, True, 0, {}),
    ("fail_p4_1_metric", {"single-jxl-score": {"metric_ms": 700.0}}, None, True, 1, {"P4-1": "FAIL"}),
    ("fail_p4_1_wall", {"single-jxl-score": {"wall_ms": 3600.0}}, None, True, 1, {"P4-1": "FAIL"}),
    ("fail_p4_3_ratio", {"batch8-score": {"metric_ms": 800.0}}, None, True, 1, {"P4-3": "FAIL"}),
    ("fail_p4_4_exit", {"big48-score": {"exit_code": 1}}, None, True, 1, {"P4-4": "FAIL"}),
    ("fail_p4_4_missing", {"big48-score": {"out_specs": [_out("big48mp.jpg", "jpeg", 0, width=8000, height=6000, write_artifact=False)]}}, None, True, 1, {"P4-4": "FAIL"}),
    ("fail_p4_5_metawrite", {"tiff-meta": {"metawrite_ms": 900.0}}, None, True, 1, {"P4-5": "FAIL"}),
    ("fail_p4_6_probe_decode", {"heic-input": {"heic_probe_to_decode_ms": 3300.0, "trace_span_ms": 4200.0}}, None, True, 1, {"P4-6": "FAIL"}),
    (
        "fail_p1_1_concurrency",
        {
            "batch8-score": {
                "acquires": [0.0, 0.0, 900.0, 900.0, 1800.0, 1800.0, 2700.0, 2700.0],
                "dones": [899.0, 899.0, 1799.0, 1799.0, 2699.0, 2699.0, 3599.0, 3599.0],
                "wall_ms": 3800.0,
                "cpu_ms": 24000.0,
                "metric_ms": 2000.0,
                "trace_span_ms": 3800.0,
            }
        },
        None,
        True,
        1,
        {"P1-1": "FAIL"},
    ),
    ("fail_p1_2_peak", {"batch8-small-noscore": {"e3_peak_active": 2}}, None, True, 1, {"P1-2": "FAIL"}),
    ("fail_p1_2_wall", {"batch8-small-noscore": {"wall_ms": 1200.0}}, None, True, 1, {"P1-2": "FAIL"}),
    ("fail_p9_1_cpu", {"batch8-score": {"cpu_ms": 10000.0}}, None, True, 1, {"P9-1": "FAIL"}),
    ("fail_multi4_wall", {"multi4-noscore": {"wall_ms": 16000.0, "trace_span_ms": 16000.0}}, None, True, 1, {"MULTI4-WALL": "FAIL"}),
    ("fail_multi2_wall", {"multi2-score": {"wall_ms": 21000.0, "trace_span_ms": 21000.0}}, None, True, 1, {"MULTI2-WALL": "FAIL"}),
    ("fail_multi_sha", {"multi4-noscore": {"serial_identical": False}}, None, True, 1, {"MULTI-SHA": "FAIL"}),
    (
        "blocked_missing_roles",
        {},
        ["single-jxl-score"],
        True,
        2,
        {
            "P4-1": "PASS",
            "P4-3": "BLOCKED",
            "P4-4": "BLOCKED",
            "P4-5": "BLOCKED",
            "P4-6": "BLOCKED",
            "P1-1": "BLOCKED",
            "P1-2": "BLOCKED",
            "P9-1": "BLOCKED",
            "MULTI4-WALL": "BLOCKED",
            "MULTI2-WALL": "BLOCKED",
            "MULTI-SHA": "BLOCKED",
        },
    ),
]
SELFTEST_ALL_RUNS = list(ROLE_TO_RUN.values())


def _merge_specs(overrides):
    specs = copy.deepcopy(_base_specs())
    for run_name, patch in (overrides or {}).items():
        if run_name not in specs:
            raise SystemExit("selftest 覆盖了未知运行: %s" % run_name)
        specs[run_name].update(patch)
    return specs


def write_fixtures(fixtures_root):
    fixtures_root = os.path.abspath(fixtures_root)
    guard = os.path.join("build", "m10", "w1-tools", "fixtures")
    if guard.replace("\\", "/") not in fixtures_root.replace("\\", "/"):
        raise SystemExit("拒绝写入非 build/m10/w1-tools/fixtures 下的夹具: %s" % fixtures_root)
    if os.path.isdir(fixtures_root):
        shutil.rmtree(fixtures_root)
    os.makedirs(fixtures_root, exist_ok=True)
    roots = {}
    for case_name, overrides, include_runs, serial_ok, _, _ in SELFTEST_CASES:
        case_root = os.path.join(fixtures_root, case_name)
        specs = _merge_specs(overrides)
        include = include_runs if include_runs is not None else SELFTEST_ALL_RUNS
        roots[case_name] = build_case_fixtures(case_root, specs, include, serial_identical=serial_ok)
    return roots


def run_selftest(fixtures_root, verbose=False):
    roots = write_fixtures(fixtures_root)
    print("PERF-SELFTEST fixtures=%s cases=%d" % (os.path.abspath(fixtures_root), len(SELFTEST_CASES)))
    failures = 0
    observed = {probe_id: set() for probe_id in PROBE_ORDER}
    for case_name, _, _, _, expect_exit, expect_status in SELFTEST_CASES:
        rc = build_from_manifest(os.path.join(roots[case_name], "manifest.json"))
        probes = run_probes(rc)
        statuses = {probe.probe_id: probe.status() for probe in probes}
        exit_code = overall_exit(probes)
        mismatch = []
        for probe_id in PROBE_ORDER:
            want = expect_status.get(probe_id, "PASS")
            got = statuses.get(probe_id)
            observed[probe_id].add(got)
            if got != want:
                mismatch.append("%s: want=%s got=%s" % (probe_id, want, got))
        if exit_code != expect_exit:
            mismatch.append("exit: want=%d got=%d" % (expect_exit, exit_code))
        if mismatch:
            failures += 1
            print(
                "PERF-SELFTEST case=%s exit=%d statuses=%s result=FAIL mismatch=%s"
                % (case_name, exit_code, ",".join("%s=%s" % (pid, statuses[pid]) for pid in PROBE_ORDER), "; ".join(mismatch))
            )
        else:
            print(
                "PERF-SELFTEST case=%s exit=%d statuses=%s result=PASS"
                % (case_name, exit_code, ",".join("%s=%s" % (pid, statuses[pid]) for pid in PROBE_ORDER))
            )
        if verbose:
            for probe in probes:
                for line in probe.lines(verbose_ok=True):
                    print(line)
    # 反假绿：每个探针至少被反例命中一次 FAIL，且正例全 PASS（否则 selftest 本身不可信）
    missing_fail = [pid for pid in PROBE_ORDER if "FAIL" not in observed[pid]]
    if missing_fail:
        failures += 1
        print("PERF-SELFTEST anti-fake-green=FAIL probes_without_fail=%s" % ",".join(missing_fail))
    else:
        print("PERF-SELFTEST anti-fake-green=PASS probes_with_fail=%d/%d" % (len(PROBE_ORDER), len(PROBE_ORDER)))
    ok = failures == 0
    print("PERF-SELFTEST RESULT: %s cases=%d failures=%d" % ("OK" if ok else "FAIL", len(SELFTEST_CASES), failures))
    return 0 if ok else 1


# ---------------------------------------------------------------------------
# --exec：运行 + 测量（wall/cpu/exit），写 perf-summary.txt；断言留给解析模式
# ---------------------------------------------------------------------------

try:  # psutil 由仓库 venv 提供（tools/env.py run 下可用）；缺失时 cpu 记为 None
    import psutil  # noqa: F401
except ImportError:  # pragma: no cover - 环境依赖
    psutil = None


def _cpu_ms_of(proc_handle):
    if proc_handle is None:
        return 0.0
    try:
        total = proc_handle.cpu_times()
        cpu = total.user + total.system
        for child in proc_handle.children(recursive=True):
            try:
                ctimes = child.cpu_times()
                cpu += ctimes.user + ctimes.system
            except Exception:  # noqa: BLE001 - 子进程可能瞬间退出
                continue
        return cpu * 1000.0
    except Exception:  # noqa: BLE001 - NoSuchProcess 等
        return 0.0


def cmd_exec(args):
    run_dir = os.path.abspath(args.exec_run_dir)
    os.makedirs(run_dir, exist_ok=True)
    command = list(args.cmd)
    while command and command[0] == "--":  # argparse REMAINDER 会把分隔符 `--` 一并收进来
        command.pop(0)
    if not command:
        print("错误: --exec 需要 `--` 之后的被测命令", file=sys.stderr)
        return 3
    cwd = os.path.abspath(args.cwd) if args.cwd else ROOT
    stdout_path = os.path.join(run_dir, "perf-exec.stdout.txt")
    proc_handle = None
    cpu_ms = 0.0
    start = time.perf_counter()
    with open(stdout_path, "wb") as fout:
        try:
            proc = subprocess.Popen(
                command,
                cwd=cwd,
                stdin=subprocess.DEVNULL,
                stdout=fout,
                stderr=subprocess.STDOUT,
            )
        except OSError as exc:
            print(
                "PERF-EXEC run=%s status=ERROR reason=%s executable=%s cwd=%s command=%s"
                % (os.path.basename(run_dir), _kv(exc), _kv(command[0]), _kv(cwd), _kv(" ".join(command)))
            )
            return 3
        if psutil is not None:
            try:
                proc_handle = psutil.Process(proc.pid)
            except Exception:  # noqa: BLE001
                proc_handle = None
        while proc.poll() is None:
            time.sleep(0.04)
            cpu_ms = max(cpu_ms, _cpu_ms_of(proc_handle))
        cpu_ms = max(cpu_ms, _cpu_ms_of(proc_handle))
        returncode = proc.returncode
    wall_ms = (time.perf_counter() - start) * 1000.0
    cores = os.cpu_count() or 0
    exit_line = ""
    try:
        with open(stdout_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("EXIT "):
                    exit_line = line.strip()
    except OSError:
        pass
    if not exit_line:
        exit_line = "EXIT %d (process)" % returncode
    # wall 口径：优先 --dev 日志的 run summary total_ms（进程内墙钟，SLO 可比）；
    # 进程外壁钟（含 env.py 包装开销）另记 exec_wall_ms，供归因。
    logs = glob.glob(os.path.join(run_dir, "logs", "*.log"))
    log_path = max(logs, key=os.path.basename) if logs else ""
    dev_total_ms = None
    if log_path:
        try:
            with open(log_path, "r", encoding="utf-8", errors="replace") as f:
                facts = parse_log_text(f.read())
            dev_total_ms = _num(facts.get("run_summary", {}).get("total_ms"))
        except OSError:
            dev_total_ms = None
    wall_for_slo = dev_total_ms if dev_total_ms is not None else wall_ms
    summary_path = os.path.join(run_dir, "perf-summary.txt")
    cpu_value = "%.0f" % cpu_ms if psutil is not None else ""
    with open(summary_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("name=%s\n" % os.path.basename(run_dir))
        f.write("out=%s\n" % run_dir)
        f.write("wall_ms=%.1f\n" % wall_for_slo)
        f.write("exec_wall_ms=%.1f\n" % wall_ms)
        if cpu_value:
            f.write("cpu_ms_sum_children=%s\n" % cpu_value)
        f.write("cpu_pct_of_all_cores=%.1f\n" % (100.0 * cpu_ms / wall_ms / cores if wall_ms and cpu_value else 0.0))
        f.write("cores=%d\n" % cores)
        f.write("exit_line=%s\n" % exit_line)
        f.write("log=%s\n" % log_path)
        f.write("args=%s\n" % " ".join(command))
    print(
        "PERF-EXEC run=%s command=%s wall_ms=%.1f exec_wall_ms=%.1f cpu_ms=%s cores=%d exit_line=%s summary=%s"
        % (
            _kv(os.path.basename(run_dir)),
            _kv(" ".join(command)),
            wall_for_slo,
            wall_ms,
            cpu_value or "unavailable",
            cores,
            _kv(exit_line),
            summary_path,
        )
    )
    if psutil is None:
        print("PERF-EXEC warning=cpu_ms_unavailable reason=psutil-missing（P9-1 将 BLOCKED，不假绿）")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="PhotoPipeline 性能/输出链路机械探针（M10 W1-T11；设计 v0.4.0 §12.1，dev-only）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--recon-corpus", help="既有侦察语料根（含 results/*.summary.txt 与 out/<run>/）")
    source.add_argument("--manifest", help="运行清单 JSON（schema=pp-perf-runs-v1）")
    source.add_argument("--selftest", action="store_true", help="跑合成夹具正反例")
    source.add_argument("--gen-fixtures", action="store_true", help="只生成合成夹具")
    source.add_argument("--exec", dest="exec_run_dir", metavar="RUN_DIR", help="运行 `--` 之后的命令并测量/写 summary")
    parser.add_argument("--cwd", help="--exec 的工作目录（默认仓库根）")
    parser.add_argument("--role", action="append", default=[], metavar="ROLE=RUN", help="角色绑定覆盖（可多次）")
    parser.add_argument("--fixtures", default=FIXTURES_DEFAULT, help="夹具根目录（默认 build/m10/w1-tools/fixtures/perf）")
    parser.add_argument("--verbose", action="store_true", help="打印 PASS 检查行")
    parser.add_argument("--json-out", help="把探针结果写成 JSON 文件（CI artifact）")
    parser.add_argument("cmd", nargs=argparse.REMAINDER, help="--exec 的被测命令（放在 `--` 之后）")
    args = parser.parse_args(argv)

    if args.selftest:
        return run_selftest(args.fixtures, args.verbose)
    if args.gen_fixtures:
        roots = write_fixtures(args.fixtures)
        print("PERF-FIXTURES wrote=%d root=%s" % (len(roots), os.path.abspath(args.fixtures)))
        return 0
    if args.exec_run_dir:
        return cmd_exec(args)
    if not args.recon_corpus and not args.manifest:
        parser.print_help()
        return 3

    if args.recon_corpus:
        corpus = os.path.abspath(args.recon_corpus)
        if not os.path.isdir(corpus):
            print("错误: --recon-corpus 目录不存在: %s" % corpus, file=sys.stderr)
            return 3
        rc = build_from_recon(corpus, args.role)
    else:
        rc = build_from_manifest(args.manifest, args.role)

    print_thresholds()
    rc.load_all()
    print_runs(rc)
    probes = run_probes(rc)
    print_probes(probes, verbose=args.verbose)
    print_summary(probes, rc)
    if args.json_out:
        write_json(args.json_out, probes, rc)
    return overall_exit(probes)


if __name__ == "__main__":
    sys.exit(main())
