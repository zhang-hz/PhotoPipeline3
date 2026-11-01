#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline 进度静默审计（M10 / W1-T11 机械探针，dev-only，不进 ctest）。

设计真源：docs/v0.4.0-design.md
  * §0.3 目标 SLO「运行中事件静默 → ≤500ms」
  * §12.1 P4-2：`progress-trace.jsonl` 相邻事件 ≤500ms（首事件前允许 1s）
  * §4.6 进度模型扩展：OutputDone 逐输出终态事件、心跳 ≤500ms、overall_frac 停在 0.9
  * §5.3 状态机映射：OutputDone（含/不含 metric）是「输出完成」的唯一可识别事实
字段契约：tests/golden/SCHEMA.md「progress_trace」节（只读，本工具不改冻结 schema）。

检查项（逐 trace；任一 FAIL/PENDING = 该 trace 不通过）：
  1. parse        逐行 JSON Lines 可解析、非空、必需字段齐全（seq/file/state/t_ms/
                  output_index/overall_frac）
  2. seq          全局单调序号严格递增（写入顺序 = 递达顺序）
  3. tmono        按 seq 顺序 t_ms 不得大幅倒退（concurrency 下允许 ≤100ms 抖动；
                  大于该值 = 时钟/序列损坏）
  4. first-event  首事件 t_ms ≤1000ms（设计允许启动前 1s）
  5. gap          相邻事件间隔（按 t_ms 排序）≤500ms（P4-2 核心断言）
  6. overall      逐文件 overall_frac ∈ [0,1] 且单调不倒退（容差 1e-6）
  7. terminal-1.0 终态为 done 的文件在终态**之前**存在 overall_frac==1.0 的进度事件
  8. output-done  每个出现过的产物下标（output_index≥0）必须存在可识别的完成事实
                  事件（state=output_done）。**HEAD 尚无该状态** → 报 PENDING/W1-T7，
                  绝不假绿（设计 §4.6 A1/A2，落地任务 W1-T7）。仅在显式
                  `--no-require-output-done` 时降级为 PASS（诊断用，不建议用于门禁）。

用法:
  python tools/pp_progress_audit.py --trace <out>/progress-trace.jsonl [--verbose]
  python tools/pp_progress_audit.py --corpus build/design-recon/pipeline/out [--limit N]
  python tools/pp_progress_audit.py --selftest [--fixtures DIR]     # 正反例（含 600ms gap）
  python tools/pp_progress_audit.py --gen-fixtures [--fixtures DIR] # 只生成合成夹具

退出码: 0 = 全部 PASS；1 = 存在 FAIL/PENDING；2 = 用法错或仅 BLOCKED；3 = 夹具/IO 错误。
机器可读行: AUDIT-TRACE / AUDIT-CHECK / AUDIT-SUMMARY（key=value，逐行可 grep）。
"""

import argparse
import glob
import json
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES_DEFAULT = os.path.join(ROOT, "build", "m10", "w1-tools", "fixtures", "progress")

# 阈值单源（与设计逐条对齐；改动必须同步 docs/v0.4.0-design.md §12.1 P4-2 与报告对照表）
MAX_GAP_MS = 500.0            # §0.3 / §12.1 P4-2：相邻事件 ≤500ms
FIRST_EVENT_BUDGET_MS = 1000.0  # §12.1 P4-2：首事件前允许 1s
OVERALL_TOL = 1e-6            # SCHEMA.md：overall_frac 单调容差
TMONO_TOL_MS = -100.0         # 并发写入下 elapsed 采样允许的倒退上限

TERMINAL_STATES = {"done", "done_with_errors", "failed", "skipped", "cancelled"}
DONE_STATES = {"done"}        # terminal-1.0 只对完全成功的文件做硬断言（SCHEMA.md 口径）
REQUIRED_FIELDS = ("seq", "file", "state", "t_ms", "output_index", "overall_frac")
COMPLETION_STATE_NORM = "outputdone"  # FileState::OutputDone 的小写名（snake_case）归一化后

# W1 依赖：output-done 事实缺口的落地任务（报告「缺字段与依赖」章节同源引用）
OUTPUT_DONE_TASK = "W1-T7（设计 §4.6 A1/A2：FileState::OutputDone + FileEvent::output）"


def norm_state(value):
    """状态名归一化：小写 + 去下划线/连字符（output_done / output-done / outputdone 视为同一事实）。"""
    return str(value or "").strip().lower().replace("_", "").replace("-", "")


class Check:
    __slots__ = ("check_id", "status", "value", "threshold", "detail")

    def __init__(self, check_id, status, value="", threshold="", detail=""):
        self.check_id = check_id
        self.status = status
        self.value = value
        self.threshold = threshold
        self.detail = detail

    def line(self, path, verbose_ok=False):
        if self.status == "PASS" and not verbose_ok:
            return None
        return (
            "AUDIT-CHECK path=%s id=%s status=%s value=%s threshold=%s detail=%s"
            % (path, self.check_id, self.status, _kv(self.value), _kv(self.threshold), _kv(self.detail))
        )


def _kv(value):
    """机器可读 value 的紧凑转义：空白/引号包成单段（不引入 JSON 依赖，便于 grep）。"""
    text = str(value if value is not None else "")
    text = text.replace("\r", " ").replace("\n", " ").replace('"', "'")
    if text == "":
        return '""'
    if any(c.isspace() for c in text) or "=" in text:
        return '"%s"' % text
    return text


class TraceAudit:
    def __init__(self, path):
        self.path = path
        self.events = 0
        self.duration_ms = 0.0
        self.max_gap_ms = 0.0
        self.first_event_ms = 0.0
        self.checks = []
        self.observed_outputs = 0
        self.completed_outputs = 0
        self.error = ""

    def add(self, check):
        self.checks.append(check)
        return check

    def status(self):
        statuses = {c.status for c in self.checks}
        if "FAIL" in statuses:
            return "FAIL"
        if "PENDING" in statuses:
            return "PENDING"
        if "BLOCKED" in statuses:
            return "BLOCKED"
        return "PASS"

    def line(self):
        return (
            "AUDIT-TRACE path=%s events=%d duration_ms=%.1f max_gap_ms=%.1f first_event_ms=%.1f "
            "outputs=%d output_done=%d status=%s"
            % (
                self.path,
                self.events,
                self.duration_ms,
                self.max_gap_ms,
                self.first_event_ms,
                self.observed_outputs,
                self.completed_outputs,
                self.status(),
            )
        )


def load_trace(path):
    """读取 progress-trace.jsonl → (events, error)。空文件/坏行都返回明确 error。"""
    events = []
    if not os.path.isfile(path):
        return events, "trace 文件不存在"
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for lineno, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError as exc:
                    return events, "第 %d 行 JSON 解析失败: %s" % (lineno, exc)
                if not isinstance(obj, dict):
                    return events, "第 %d 行不是 JSON 对象" % lineno
                events.append(obj)
    except OSError as exc:
        return events, "读取失败: %s" % exc
    if not events:
        return events, "trace 为空（0 条事件）"
    return events, ""


def _num(value):
    """尽力转 float；布尔/字符串数值返回 None。"""
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return float(value)
    return None


def audit_trace(path, require_output_done=True):
    """对单个 trace 做全部检查并返回 TraceAudit（不打印）。"""
    audit = TraceAudit(path)
    events, error = load_trace(path)
    audit.events = len(events)
    if error:
        audit.error = error
        audit.add(Check("parse", "FAIL", "0", ">=1", error))
        return audit
    audit.add(Check("parse", "PASS", str(len(events)), ">=1", ""))

    # 1) 必需字段
    missing = []
    for idx, ev in enumerate(events):
        for key in REQUIRED_FIELDS:
            if key not in ev:
                missing.append("line%d:%s" % (idx + 1, key))
    if missing:
        audit.add(Check("fields", "FAIL", "%d missing" % len(missing), "0", ", ".join(missing[:8])))
    else:
        audit.add(Check("fields", "PASS", "0", "0", ""))

    # 2) seq 严格递增（写入顺序 = 递达顺序；R-160 已保证 seq 与写盘同临界区）
    seqs = [ev.get("seq") for ev in events]
    seq_ok = all(_num(s) is not None for s in seqs)
    seq_bads = []
    if seq_ok:
        nums = [float(s) for s in seqs]
        for i in range(len(nums) - 1):
            if nums[i + 1] <= nums[i]:
                seq_bads.append("seq[%d]=%s<=%s" % (i + 1, nums[i + 1], nums[i]))
    else:
        seq_bads.append("非数值 seq")
    audit.add(
        Check(
            "seq",
            "FAIL" if seq_bads else "PASS",
            "%d bad" % len(seq_bads),
            "strictly increasing",
            ", ".join(seq_bads[:5]),
        )
    )

    # 3) t_ms 大倒退（并发下 elapsed 采样可小幅倒退；>100ms = 序列/时钟损坏）
    times = []
    for ev in events:
        t = _num(ev.get("t_ms"))
        if t is not None:
            times.append(t)
    tmono_bads = []
    for i in range(len(times) - 1):
        delta = times[i + 1] - times[i]
        if delta < TMONO_TOL_MS:
            tmono_bads.append("seq%d:%.1f→%.1f(%.1fms)" % (i + 1, times[i], times[i + 1], delta))
    audit.add(
        Check(
            "tmono",
            "FAIL" if tmono_bads else "PASS",
            "%d bad" % len(tmono_bads),
            ">=%.0fms" % TMONO_TOL_MS,
            ", ".join(tmono_bads[:5]),
        )
    )

    # 4/5) 首事件预算 + 相邻事件间隔（按 t_ms 排序，避免并发写入的 seq 交错放大/缩小静默）
    if times:
        sorted_times = sorted(times)
        first = sorted_times[0]
        gaps = [sorted_times[i + 1] - sorted_times[i] for i in range(len(sorted_times) - 1)]
        max_gap = max(gaps) if gaps else 0.0
        audit.first_event_ms = first
        audit.max_gap_ms = max_gap
        audit.duration_ms = sorted_times[-1] - first
        audit.add(
            Check(
                "first-event",
                "PASS" if first <= FIRST_EVENT_BUDGET_MS else "FAIL",
                "%.1f" % first,
                "<=%.0f" % FIRST_EVENT_BUDGET_MS,
                "",
            )
        )
        gap_bads = [g for g in gaps if g > MAX_GAP_MS]
        worst = sorted(gaps, reverse=True)[:3]
        audit.add(
            Check(
                "gap",
                "PASS" if not gap_bads else "FAIL",
                "%.1f" % max_gap,
                "<=%.0f" % MAX_GAP_MS,
                "超阈值 %d 处；top3 gap=%s" % (len(gap_bads), ",".join("%.1f" % g for g in worst)),
            )
        )
    else:
        audit.add(Check("first-event", "FAIL", "n/a", "<=%.0f" % FIRST_EVENT_BUDGET_MS, "无 t_ms"))
        audit.add(Check("gap", "FAIL", "n/a", "<=%.0f" % MAX_GAP_MS, "无 t_ms"))

    # 6) overall_frac 范围 + 逐文件单调（按 seq 顺序即写入顺序）
    overall_bad = []
    per_file = {}
    for ev in events:
        f = ev.get("file")
        value = _num(ev.get("overall_frac"))
        if value is None:
            overall_bad.append("file=%s 非数值 overall_frac" % f)
            continue
        if value < 0.0 or value > 1.0:
            overall_bad.append("file=%s overall=%.6f 越界" % (f, value))
        per_file.setdefault(f, []).append((ev.get("seq"), value))
    mono_bad = []
    for f, rows in per_file.items():
        prev = None
        for seq, value in rows:
            if prev is not None and value < prev - OVERALL_TOL:
                mono_bad.append("file=%s seq=%s %.6f<%.6f" % (f, seq, value, prev))
            prev = value
    if overall_bad:
        audit.add(Check("overall", "FAIL", "%d bad" % len(overall_bad), "[0,1]", ", ".join(overall_bad[:5])))
    elif mono_bad:
        audit.add(Check("overall", "FAIL", "%d backward" % len(mono_bad), "monotonic", ", ".join(mono_bad[:5])))
    else:
        audit.add(Check("overall", "PASS", "%d files" % len(per_file), "monotonic [0,1]", ""))

    # 7) 终态 done 的文件：终态**之前**必须有 overall==1.0 的进度事件（SCHEMA.md 断言 5 口径）
    terminal_missing = []
    checked_done = 0
    for f, rows in per_file.items():
        states = [norm_state(ev.get("state")) for ev in events if ev.get("file") == f]
        if not any(s in DONE_STATES for s in states):
            continue
        checked_done += 1
        non_terminal_one = False
        terminal_one = False
        for ev in events:
            if ev.get("file") != f:
                continue
            value = _num(ev.get("overall_frac"))
            if value is None or abs(value - 1.0) > OVERALL_TOL:
                continue
            if norm_state(ev.get("state")) in TERMINAL_STATES:
                terminal_one = True
            else:
                non_terminal_one = True
        if not non_terminal_one:
            detail = "仅终态事件携带 1.0（终态前无可观测 1.0）" if terminal_one else "无 overall_frac==1.0 事件"
            terminal_missing.append("file=%s %s" % (f, detail))
    audit.add(
        Check(
            "terminal-1.0",
            "FAIL" if terminal_missing else "PASS",
            "%d/%d good" % (checked_done - len(terminal_missing), checked_done),
            "done file has pre-terminal 1.0",
            ", ".join(terminal_missing[:5]),
        )
    )

    # 8) 逐输出完成事实：必须存在 OutputDone 状态事件（HEAD 缺 → PENDING/W1-T7，不假绿）
    outputs = set()
    completed = set()
    for ev in events:
        out_raw = ev.get("output_index")
        out_num = _num(out_raw)
        if out_num is None or out_num != int(out_num) or out_num < 0:
            continue
        out_index = int(out_num)
        key = (ev.get("file"), out_index)
        outputs.add(key)
        if norm_state(ev.get("state")) == COMPLETION_STATE_NORM:
            completed.add(key)
    audit.observed_outputs = len(outputs)
    audit.completed_outputs = len(completed)
    if not outputs:
        audit.add(
            Check("output-done", "BLOCKED", "0 outputs", "all outputs done", "trace 无输出级事件，无法判定")
        )
    elif completed == outputs:
        audit.add(
            Check("output-done", "PASS", "%d/%d" % (len(completed), len(outputs)), "all outputs done", "")
        )
    elif require_output_done:
        missing = sorted(outputs - completed)
        audit.add(
            Check(
                "output-done",
                "PENDING",
                "0/%d" % len(outputs),
                "all outputs done",
                "不满足/待 %s；缺失=%s" % (OUTPUT_DONE_TASK, ", ".join("file=%s,out=%s" % k for k in missing[:5])),
            )
        )
    else:
        audit.add(
            Check(
                "output-done",
                "PASS",
                "%d/%d" % (len(completed), len(outputs)),
                "all outputs done",
                "已按 --no-require-output-done 豁免（诊断用，不得用于门禁）",
            )
        )
    return audit


# ---------------------------------------------------------------------------
# 合成夹具（--selftest 用）：真实文件 + 与 --dev 侧车同 schema 的 JSON Lines。
#   正例必须全 PASS；反例逐条触发目标检查（600ms gap 是设计点名的示例）。
# ---------------------------------------------------------------------------

SELFTEST_CASES = {
    "pass": {
        "expect_checks": {
            "parse": "PASS",
            "fields": "PASS",
            "seq": "PASS",
            "tmono": "PASS",
            "first-event": "PASS",
            "gap": "PASS",
            "overall": "PASS",
            "terminal-1.0": "PASS",
            "output-done": "PASS",
        },
        "expect_status": "PASS",
        "expect_exit": 0,
    },
    "gap_600ms": {
        "expect_checks": {"gap": "FAIL", "output-done": "PASS"},
        "expect_status": "FAIL",
        "expect_exit": 1,
    },
    "first_event_late": {
        "expect_checks": {"first-event": "FAIL"},
        "expect_status": "FAIL",
        "expect_exit": 1,
    },
    "overall_backtrack": {
        "expect_checks": {"overall": "FAIL"},
        "expect_status": "FAIL",
        "expect_exit": 1,
    },
    "overall_out_of_range": {
        "expect_checks": {"overall": "FAIL"},
        "expect_status": "FAIL",
        "expect_exit": 1,
    },
    "no_output_done": {
        "expect_checks": {"output-done": "PENDING", "gap": "PASS", "overall": "PASS"},
        "expect_status": "PENDING",
        "expect_exit": 1,
        "no_require_checks": {"output-done": "PASS"},
        "no_require_status": "PASS",
        "no_require_exit": 0,
    },
    "bad_json": {
        "expect_checks": {"parse": "FAIL"},
        "expect_status": "FAIL",
        "expect_exit": 1,
    },
    "seq_not_increasing": {
        "expect_checks": {"seq": "FAIL"},
        "expect_status": "FAIL",
        "expect_exit": 1,
    },
    "terminal_without_1_0": {
        "expect_checks": {"terminal-1.0": "FAIL"},
        "expect_status": "FAIL",
        "expect_exit": 1,
    },
    "tmono_backwards": {
        "expect_checks": {"tmono": "FAIL", "gap": "PASS"},
        "expect_status": "FAIL",
        "expect_exit": 1,
    },
}

# 正/反例必须同时存在（防「全 PASS 的假绿 selftest」）
SELFTEST_MUST_HAVE = ("PASS", "FAIL", "PENDING")


def _ev(seq, file_idx, t_ms, overall, state="progress", stage="Encode", output_index=-1,
        stage_frac=0.0, synthetic=False):
    return {
        "seq": seq,
        "file": file_idx,
        "state": state,
        "stage": stage,
        "output_index": output_index,
        "stage_frac": stage_frac,
        "overall_frac": overall,
        "synthetic": synthetic,
        "t_ms": t_ms,
    }


def _clean_trace():
    """基础正例：单文件双输出，≤200ms 间隔，两个 OutputDone，终态前 1.0。"""
    evs = [
        _ev(0, 0, 5.0, 0.0, state="probing", stage="Probe"),
        _ev(1, 0, 105.0, 0.1, stage="Decode"),
        _ev(2, 0, 205.0, 0.3, stage="Encode", output_index=0, stage_frac=0.2),
        _ev(3, 0, 305.0, 0.6, stage="Encode", output_index=0, stage_frac=1.0),
        _ev(4, 0, 405.0, 0.7, state="output_done", stage="Done", output_index=0),
        _ev(5, 0, 505.0, 0.8, stage="Encode", output_index=1, stage_frac=0.5),
        _ev(6, 0, 605.0, 0.9, stage="MetaWrite", output_index=1, stage_frac=1.0),
        _ev(7, 0, 705.0, 0.95, state="output_done", stage="Done", output_index=1),
        _ev(8, 0, 805.0, 1.0, stage="Done"),
        _ev(9, 0, 905.0, 1.0, state="done", stage="Done"),
    ]
    return evs


def _write_case(case_dir, events, raw_suffix=None):
    os.makedirs(case_dir, exist_ok=True)
    path = os.path.join(case_dir, "progress-trace.jsonl")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for ev in events:
            f.write(json.dumps(ev, ensure_ascii=False, sort_keys=True) + "\n")
        if raw_suffix:
            f.write(raw_suffix)
    return path


def write_fixtures(fixtures_root):
    """生成全部合成 trace 夹具（幂等：先删本工具的 fixtures 子树再重建）。"""
    fixtures_root = os.path.abspath(fixtures_root)
    guard = os.path.join("build", "m10", "w1-tools", "fixtures")
    if guard.replace("\\", "/") not in fixtures_root.replace("\\", "/"):
        raise SystemExit("拒绝写入非 build/m10/w1-tools/fixtures 下的夹具目录: %s" % fixtures_root)
    if os.path.isdir(fixtures_root):
        shutil.rmtree(fixtures_root)
    os.makedirs(fixtures_root, exist_ok=True)
    paths = {}

    paths["pass"] = _write_case(os.path.join(fixtures_root, "pass"), _clean_trace())

    evs = _clean_trace()
    # 设计点名的反例：一处 600ms 静默（>500ms），其余间隔保持 100ms。
    evs[4]["t_ms"] = evs[3]["t_ms"] + 600.0
    for ev in evs[5:]:
        ev["t_ms"] = evs[4]["t_ms"] + (ev["seq"] - 4) * 100.0
    paths["gap_600ms"] = _write_case(os.path.join(fixtures_root, "gap_600ms"), evs)

    evs = _clean_trace()
    for ev in evs:
        ev["t_ms"] += 1500.0  # 首事件 1505ms > 1000ms 预算
    paths["first_event_late"] = _write_case(os.path.join(fixtures_root, "first_event_late"), evs)

    evs = _clean_trace()
    evs[3]["overall_frac"] = 0.55
    evs[4]["overall_frac"] = 0.5  # 0.55 → 0.50 倒退
    paths["overall_backtrack"] = _write_case(os.path.join(fixtures_root, "overall_backtrack"), evs)

    evs = _clean_trace()
    evs[2]["overall_frac"] = 1.2
    paths["overall_out_of_range"] = _write_case(os.path.join(fixtures_root, "overall_out_of_range"), evs)

    evs = [e for e in _clean_trace() if norm_state(e["state"]) != COMPLETION_STATE_NORM]
    for idx, ev in enumerate(evs):
        ev["seq"] = idx
    paths["no_output_done"] = _write_case(os.path.join(fixtures_root, "no_output_done"), evs)

    evs = _clean_trace()
    paths["bad_json"] = _write_case(
        os.path.join(fixtures_root, "bad_json"), evs, raw_suffix='{"seq": 10, "file": 0, broken\n'
    )

    evs = _clean_trace()
    evs[4]["seq"] = 3  # seq 重复（3,3）
    paths["seq_not_increasing"] = _write_case(os.path.join(fixtures_root, "seq_not_increasing"), evs)

    evs = _clean_trace()
    evs[8]["overall_frac"] = 0.95  # 终态前没有 1.0
    evs[9]["overall_frac"] = 1.0   # 1.0 只在终态事件
    paths["terminal_without_1_0"] = _write_case(os.path.join(fixtures_root, "terminal_without_1_0"), evs)

    evs = _clean_trace()
    evs[5]["t_ms"] = 500.0  # seq 顺序 t_ms 倒退 505→500（-5ms 可容忍）
    evs[6]["t_ms"] = 5.0    # seq6 大幅倒退（-500ms）
    evs[7]["t_ms"] = 605.0
    evs[8]["t_ms"] = 705.0
    evs[9]["t_ms"] = 805.0
    paths["tmono_backwards"] = _write_case(os.path.join(fixtures_root, "tmono_backwards"), evs)
    return paths


def _fmt_checks(audit):
    return ",".join("%s=%s" % (c.check_id, c.status) for c in audit.checks)


def run_selftest(fixtures_root, verbose=False):
    paths = write_fixtures(fixtures_root)
    print("AUDIT-SELFTEST fixtures=%s cases=%d" % (os.path.abspath(fixtures_root), len(SELFTEST_CASES)))
    failures = 0
    statuses = set()
    for name in sorted(SELFTEST_CASES):
        spec = SELFTEST_CASES[name]
        path = paths[name]
        audit = audit_trace(path, require_output_done=True)
        expected = spec["expect_checks"]
        mismatch = []
        got = {c.check_id: c.status for c in audit.checks}
        for check_id, want in expected.items():
            if got.get(check_id) != want:
                mismatch.append("%s: want=%s got=%s" % (check_id, want, got.get(check_id)))
        status = audit.status()
        statuses.add(status)
        if status != spec["expect_status"]:
            mismatch.append("status: want=%s got=%s" % (spec["expect_status"], status))
        exit_code = 0 if status == "PASS" else (2 if status == "BLOCKED" else 1)
        if exit_code != spec["expect_exit"]:
            mismatch.append("exit: want=%d got=%d" % (spec["expect_exit"], exit_code))
        # --no-require-output-done 语义（只在指定 case 上验证，避免假绿）
        if "no_require_checks" in spec:
            relaxed = audit_trace(path, require_output_done=False)
            got_relaxed = {c.check_id: c.status for c in relaxed.checks}
            for check_id, want in spec["no_require_checks"].items():
                if got_relaxed.get(check_id) != want:
                    mismatch.append("relaxed %s: want=%s got=%s" % (check_id, want, got_relaxed.get(check_id)))
            relaxed_status = relaxed.status()
            relaxed_exit = 0 if relaxed_status == "PASS" else (2 if relaxed_status == "BLOCKED" else 1)
            if relaxed_status != spec["no_require_status"]:
                mismatch.append("relaxed status: want=%s got=%s" % (spec["no_require_status"], relaxed_status))
            if relaxed_exit != spec["no_require_exit"]:
                mismatch.append("relaxed exit: want=%d got=%d" % (spec["no_require_exit"], relaxed_exit))
        if mismatch:
            failures += 1
            print("AUDIT-SELFTEST case=%s status=%s result=FAIL mismatch=%s" % (name, status, "; ".join(mismatch)))
        else:
            print("AUDIT-SELFTEST case=%s status=%s checks=%s result=PASS" % (name, status, _fmt_checks(audit)))
        if verbose:
            for check in audit.checks:
                line = check.line(path, verbose_ok=True)
                if line:
                    print(line)
    anti_fake = [s for s in SELFTEST_MUST_HAVE if s not in statuses]
    if anti_fake:
        failures += 1
        print("AUDIT-SELFTEST anti-fake-green=FAIL missing_statuses=%s" % ",".join(anti_fake))
    else:
        print("AUDIT-SELFTEST anti-fake-green=PASS statuses=%s" % ",".join(sorted(statuses)))
    ok = failures == 0
    print("AUDIT-SELFTEST RESULT: %s cases=%d failures=%d" % ("OK" if ok else "FAIL", len(SELFTEST_CASES), failures))
    return 0 if ok else 1


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def cmd_trace(args):
    audit = audit_trace(args.trace, require_output_done=not args.no_require_output_done)
    print(audit.line())
    for check in audit.checks:
        line = check.line(audit.path, verbose_ok=args.verbose)
        if line:
            print(line)
    status = audit.status()
    exit_code = 0 if status == "PASS" else (2 if status == "BLOCKED" else 1)
    print("AUDIT-SUMMARY traces=1 pass=%d fail=%d pending=%d blocked=%d exit=%d"
          % (1 if status == "PASS" else 0, 1 if status == "FAIL" else 0,
             1 if status == "PENDING" else 0, 1 if status == "BLOCKED" else 0, exit_code))
    return exit_code


def cmd_corpus(args):
    root = os.path.abspath(args.corpus)
    if not os.path.isdir(root):
        print("错误: --corpus 目录不存在: %s" % root, file=sys.stderr)
        return 3
    traces = sorted(glob.glob(os.path.join(root, "**", "progress-trace.jsonl"), recursive=True))
    if args.limit:
        traces = traces[: args.limit]
    if not traces:
        print("AUDIT-SUMMARY traces=0 pass=0 fail=0 pending=0 blocked=0 exit=3")
        print("错误: 目录下没有 progress-trace.jsonl: %s" % root, file=sys.stderr)
        return 3
    counts = {"PASS": 0, "FAIL": 0, "PENDING": 0, "BLOCKED": 0}
    worst = []
    silent = 0
    for path in traces:
        audit = audit_trace(path, require_output_done=not args.no_require_output_done)
        status = audit.status()
        counts[status] += 1
        print(audit.line())
        for check in audit.checks:
            line = check.line(audit.path, verbose_ok=args.verbose)
            if line:
                print(line)
        if any(c.check_id == "gap" and c.status == "FAIL" for c in audit.checks):
            silent += 1
        worst.append((audit.max_gap_ms, audit.path))
    worst.sort(reverse=True)
    print("AUDIT-WORST max_gap_ms=%s" % ",".join("%.1f" % w[0] for w in worst[:5]))
    print("AUDIT-SUMMARY traces=%d pass=%d fail=%d pending=%d blocked=%d silent_traces=%d exit=%d"
          % (len(traces), counts["PASS"], counts["FAIL"], counts["PENDING"], counts["BLOCKED"], silent,
             0 if counts["PASS"] == len(traces) else (1 if counts["FAIL"] or counts["PENDING"] else 2)))
    if counts["PASS"] == len(traces):
        return 0
    return 1 if (counts["FAIL"] or counts["PENDING"]) else 2


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="PhotoPipeline 进度静默审计（M10 W1-T11；设计 v0.4.0 §12.1 P4-2，dev-only）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--trace", help="单个 progress-trace.jsonl 路径")
    parser.add_argument("--corpus", help="目录：递归扫描 **/progress-trace.jsonl（如 build/design-recon/pipeline/out）")
    parser.add_argument("--limit", type=int, default=0, help="--corpus 时最多扫描 N 个 trace（0=全部）")
    parser.add_argument("--verbose", action="store_true", help="打印全部检查行（默认只打印非 PASS）")
    parser.add_argument("--no-require-output-done", action="store_true",
                        help="诊断用：豁免 OutputDone 事实检查（不得用于门禁；默认严格）")
    parser.add_argument("--selftest", action="store_true", help="跑合成夹具正反例（不依赖真实运行）")
    parser.add_argument("--gen-fixtures", action="store_true", help="只生成合成夹具")
    parser.add_argument("--fixtures", default=FIXTURES_DEFAULT, help="夹具根目录（默认 build/m10/w1-tools/fixtures/progress）")
    args = parser.parse_args(argv)
    if args.selftest:
        return run_selftest(args.fixtures, args.verbose)
    if args.gen_fixtures:
        paths = write_fixtures(args.fixtures)
        print("AUDIT-FIXTURES wrote=%d root=%s" % (len(paths), os.path.abspath(args.fixtures)))
        return 0
    if args.trace:
        return cmd_trace(args)
    if args.corpus:
        return cmd_corpus(args)
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
