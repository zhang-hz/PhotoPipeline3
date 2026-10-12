// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M7-WP8 / B.6（R-193/323）：--dev harness（解析 + run_dev + 侧车），
// 自 main.cpp 拆出；main.cpp 只留 GUI 启动 + 分发 + §4.3 冻结启动序。
//
// Contract: docs/v0.3.0-design.md §3.15（--dev 冻结 CLI 面）+ §7.4（progress-trace 侧车）；
// docs/deep-review-0.3.2.md 附录 B.6。本文件顺带收口：
//   * R-160：ProgressTraceSink 的 seq_++ 移入写锁内（旧在锁外，序号竞争可重复/丢失）；
//   * R-236：--dev 摘要/run-start 日志打印 conflict **生效值**（预设覆盖后）而非 CLI 原文；
//   * R-322：split_by_format 一律回填 pp::template_has_format（派生态单源，消"仅前缀"分叉）；
//   * R-157：日志级别解析走 pp::log_level_from_text（core/logger 单源）；
//   * B.6 ③：参数装配链上提 pp::build_output_specs（与 bench 同源）。
// --dev CLI 面（§3.15）逐字保留：usage 文本仅 --workers 一行按 R-237/283 修正措辞
// （"0 = physical cores" 与实现不符，实为 thread_budget 或逻辑核）。
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "codecs/encoders.h" // probe_bitdepth_support（HEIF/AVIF 运行时位深交集）
#include "core/fsops.h"
#include "core/logger.h"
#include "core/params.h"
#include "core/pipeline.h"
#include "core/presets.h"
#include "core/scheduler.h"
#include "core/types.h"
#include "ui/preset_io.h"

namespace fs = std::filesystem;

namespace pp::dev {

// dev_cli ↔ dev_bench 的分发（两 TU 各自实现/承接；extern 声明 = 不新增共享头，
// 裁定登记 build/m7-wp8/handoff.md）。
int run_bench(int argc, char **argv);

namespace {

// ---------------------------------------------------------------------------
// --dev harness (§3.15)
// ---------------------------------------------------------------------------

const char *kDevUsage =
    "usage: photopipeline --dev <input...> --out <dir> [options]\n"
    "  --out DIR               output root (required)\n"
    "  --format ID             jpeg|jxl|png|tiff|webp|bmp|heif|avif (default jxl)\n"
    "  --backend ID            backend (avif: svt-av1|libaom; default = first)\n"
    "  --tech ID               tech (jxl: vardct|modular; webp: lossy|lossless; default = first)\n"
    "  --outputs LIST          多输出（0.3.0，M4-T5）：逗号分隔的 format[:backend[:tech]]，\n"
    "                          可重复追加；与 --format/--backend/--tech 互斥（二选一）\n"
    "  --template TMPL         输出路径模板（默认按输出数派生的 0.2 兼容/分文件夹形态）：\n"
    "                          $format/$dir/$file（分文件夹）| $dir/$file（0.2 兼容）| "
    "$dir/$format/$file\n"
    "  --lossless              lossless switch\n"
    "  --bitdepth N            output bit depth; default jpeg 8 / jxl 16 / png 16 / tiff 16 /\n"
    "                          webp 8 / bmp 24 / heif 10 / avif 10 (unsupported 10-bit HEIF/AVIF\n"
    "                          default falls back to 8 with a warning)\n"
    "  --color TARGET          keep|srgb|p3|adobergb (default keep)\n"
    "  --conflict POLICY       skip|overwrite|rename (dev default overwrite)\n"
    "  --metadata-only         metadata-only mode (zero re-encode)\n"
    "  --preset FILE           load a preset JSON (command line options win)\n"
    "  --param KEY=VALUE       override one parameter (repeatable; applies to every output)\n"
    "  --meta KEY=VALUE        set/overwrite one metadata tag (repeatable)\n"
    "  --workers N             worker count (0 = thread_budget or logical cores; dev default 1)\n"
    "  --base DIR              mirror-path base directory (repeatable)\n"
    "  --log-level LVL         trace|debug|info|warn|error\n"
    "  -h, --help              this text\n"
    "NOTE (R-145): --dev default values intentionally differ from the GUI ——\n"
    "      --conflict default = overwrite (GUI default = rename; pass --conflict rename for\n"
    "      GUI semantics), and the single-output template derives to $dir/$file (GUI default\n"
    "      = $format/$dir/$file; pass --template '$format/$dir/$file' for GUI semantics).\n"
    "sidecars (--dev only): <out>/<每个产物>.pp.json（逐输出断言面）、\n"
    "                       <out>/progress-trace.jsonl（进度事件流，W1-T6；schema 见 "
    "tests/golden/SCHEMA.md）\n"
    "exit code = number of failed files\n";

// 多输出 spec：format[:backend[:tech]]（--outputs 的单项）
struct OutputSpecText {
    std::string format, backend, tech;
};

struct DevOptions {
    std::vector<fs::path> inputs;
    std::vector<fs::path> bases;
    fs::path out_root;
    bool out_set = false;
    bool help = false;
    std::string format = "jxl", backend, tech, color = "keep", conflict = "overwrite";
    std::string preset, log_level = "info";
    std::string output_template;
    bool has_output_template = false;
    std::vector<OutputSpecText> outputs; // --outputs 追加项
    bool lossless = false, metadata_only = false;
    int bitdepth = -1, workers = 1;
    bool has_format = false, has_backend = false, has_tech = false, has_lossless = false;
    bool has_bitdepth = false, has_color = false, has_conflict = false, has_preset = false;
    bool has_workers = false;
    std::vector<std::pair<std::string, std::string>> params, metas;
};

// "format[:backend[:tech]]" → 三段（后两段可空）
bool parse_output_spec(std::string_view text, OutputSpecText &out) {
    const std::size_t first = text.find(':');
    out.format = std::string(text.substr(0, first));
    if (out.format.empty())
        return false;
    if (first == std::string_view::npos)
        return true;
    const std::size_t second = text.find(':', first + 1);
    out.backend = std::string(text.substr(
        first + 1, second == std::string_view::npos ? std::string_view::npos : second - first - 1));
    if (second == std::string_view::npos)
        return true;
    out.tech = std::string(text.substr(second + 1));
    return true;
}

std::string value_text(const pp::ParamValue &v) {
    if (const bool *b = std::get_if<bool>(&v))
        return *b ? "true" : "false";
    if (const int64_t *i = std::get_if<int64_t>(&v))
        return std::to_string(*i);
    if (const double *d = std::get_if<double>(&v)) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.10g", *d);
        return buf;
    }
    if (const std::string *s = std::get_if<std::string>(&v))
        return *s;
    return {};
}

const char *warning_name(pp::WarningKind k) {
    switch (k) {
    case pp::WarningKind::DepthDowngrade:
        return "DepthDowngrade";
    case pp::WarningKind::LossyFromLossless:
        return "LossyFromLossless";
    case pp::WarningKind::MultipageTruncated:
        return "MultipageTruncated";
    case pp::WarningKind::AlphaFlattened:
        return "AlphaFlattened";
    case pp::WarningKind::NoIccAssumeSrgb:
        return "NoIccAssumeSrgb";
    case pp::WarningKind::MetadataDropped:
        return "MetadataDropped";
    case pp::WarningKind::TimeFieldMissing:
        return "TimeFieldMissing";
    case pp::WarningKind::GrayToRgbEncoded:
        return "GrayToRgbEncoded";
    }
    return "Unknown";
}

bool parse_bool_text(std::string_view s, bool &out) {
    if (s == "true" || s == "1" || s == "yes" || s == "on") {
        out = true;
        return true;
    }
    if (s == "false" || s == "0" || s == "no" || s == "off") {
        out = false;
        return true;
    }
    return false;
}

bool parse_int_text(std::string_view s, int64_t &out) {
    const char *b = s.data();
    const char *e = s.data() + s.size();
    const std::from_chars_result r = std::from_chars(b, e, out);
    return r.ec == std::errc() && r.ptr == e;
}

bool parse_double_text(std::string_view s, double &out) {
    try {
        const std::string tmp(s);
        std::size_t used = 0;
        out = std::stod(tmp, &used);
        return used == tmp.size();
    } catch (...) {
        return false;
    }
}

bool split_kv(std::string_view arg, std::string &key, std::string &value) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string_view::npos || eq == 0)
        return false;
    key.assign(arg.substr(0, eq));
    value.assign(arg.substr(eq + 1));
    return true;
}

bool parse_dev_options(int argc, char **argv, DevOptions &o, std::string &err) {
    auto need = [&](int &i, const char *opt) -> const char * {
        if (i + 1 >= argc) {
            err = std::string(opt) + " requires a value";
            return nullptr;
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--dev")
            continue;
        if (a == "-h" || a == "--help") {
            o.help = true;
            return true;
        }
        if (a == "--out") {
            const char *v = need(i, "--out");
            if (!v)
                return false;
            o.out_root = v;
            o.out_set = true;
        } else if (a == "--format") {
            const char *v = need(i, "--format");
            if (!v)
                return false;
            o.format = v;
            o.has_format = true;
        } else if (a == "--backend") {
            const char *v = need(i, "--backend");
            if (!v)
                return false;
            o.backend = v;
            o.has_backend = true;
        } else if (a == "--tech") {
            const char *v = need(i, "--tech");
            if (!v)
                return false;
            o.tech = v;
            o.has_tech = true;
        } else if (a == "--outputs") {
            const char *v = need(i, "--outputs");
            if (!v)
                return false;
            std::string_view rest(v);
            while (!rest.empty()) {
                const std::size_t comma = rest.find(',');
                const std::string_view item = rest.substr(
                    0, comma == std::string_view::npos ? std::string_view::npos : comma);
                OutputSpecText spec;
                if (!item.empty()) {
                    if (!parse_output_spec(item, spec)) {
                        err = "invalid --outputs item: '" + std::string(item) + "'";
                        return false;
                    }
                    o.outputs.push_back(std::move(spec));
                }
                if (comma == std::string_view::npos)
                    break;
                rest.remove_prefix(comma + 1);
            }
        } else if (a == "--template") {
            const char *v = need(i, "--template");
            if (!v)
                return false;
            o.output_template = v;
            o.has_output_template = true;
        } else if (a == "--lossless") {
            o.lossless = true;
            o.has_lossless = true;
        } else if (a == "--bitdepth") {
            const char *v = need(i, "--bitdepth");
            if (!v)
                return false;
            int64_t n = 0;
            if (!parse_int_text(v, n) || n <= 0) {
                err = "invalid --bitdepth value";
                return false;
            }
            o.bitdepth = static_cast<int>(n);
            o.has_bitdepth = true;
        } else if (a == "--color") {
            const char *v = need(i, "--color");
            if (!v)
                return false;
            o.color = v;
            o.has_color = true;
        } else if (a == "--conflict") {
            const char *v = need(i, "--conflict");
            if (!v)
                return false;
            o.conflict = v;
            o.has_conflict = true;
        } else if (a == "--metadata-only") {
            o.metadata_only = true;
        } else if (a == "--preset") {
            const char *v = need(i, "--preset");
            if (!v)
                return false;
            o.preset = v;
            o.has_preset = true;
        } else if (a == "--param") {
            const char *v = need(i, "--param");
            if (!v)
                return false;
            std::string k, val;
            if (!split_kv(v, k, val)) {
                err = "--param expects KEY=VALUE";
                return false;
            }
            o.params.emplace_back(std::move(k), std::move(val));
        } else if (a == "--meta") {
            const char *v = need(i, "--meta");
            if (!v)
                return false;
            std::string k, val;
            if (!split_kv(v, k, val)) {
                err = "--meta expects KEY=VALUE";
                return false;
            }
            o.metas.emplace_back(std::move(k), std::move(val));
        } else if (a == "--workers") {
            const char *v = need(i, "--workers");
            if (!v)
                return false;
            int64_t n = 0;
            if (!parse_int_text(v, n) || n < 0) {
                err = "invalid --workers value";
                return false;
            }
            o.workers = static_cast<int>(n);
            o.has_workers = true;
        } else if (a == "--base") {
            const char *v = need(i, "--base");
            if (!v)
                return false;
            o.bases.emplace_back(v);
        } else if (a == "--log-level") {
            const char *v = need(i, "--log-level");
            if (!v)
                return false;
            o.log_level = v;
        } else if (!a.empty() && a.front() == '-' && a != "-") {
            err = "unknown option '" + std::string(a) + "'";
            return false;
        } else {
            o.inputs.emplace_back(a);
        }
    }
    if (!o.out_set) {
        err = "--out DIR is required";
        return false;
    }
    if (o.inputs.empty()) {
        err = "at least one input file or directory is required";
        return false;
    }
    return true;
}

// R-239（M7-R2.1-F4）：词表映射上收 core 单源（pp::parse_conflict_policy），本地副本转发
// （词表 skip|overwrite|rename 逐字不变；默认值分歧见 --help 的 R-145 醒目标注）。
bool parse_conflict(std::string_view s, pp::ConflictPolicy &out) {
    return pp::parse_conflict_policy(s, out);
}

// R-236：conflict 的**生效值**文本（= CLI 词表；预设覆盖后与 CLI 原文可能不同）。
// R-239（M7-R2.1-F4）：词表映射上收 core 单源（pp::conflict_policy_name），本地副本转发。
const char *conflict_text(pp::ConflictPolicy p) { return pp::conflict_policy_name(p); }

// --param KEY=VALUE → typed ParamValue using the format's parameter table. Keys that are not in
// the table are kept as strings so the encoder can report them through its E9 warning path.
// R-195 转发（M7-R2.1-F4）：ParamDef 查找走 pp::lookup_param_def 单源（first-wins）；旧
// set_param 双层循环覆盖 def 指针 = last-wins（同 key 多声明时与 GUI 侧解析出不同 def）退役。
// 单源语义：tech 指名 = 命中技术内 first-wins、该技术不声明时回退后端全技术并集（first-wins）。
bool set_param(pp::ParamSet &s, const pp::FormatDef &f, const std::string &backend,
               const std::string &tech, const std::string &key, const std::string &raw,
               std::string &err) {
    const pp::ParamDef *def = pp::lookup_param_def(f, backend, tech, key);
    if (!def) {
        s[key] = raw; // unknown → encoder E9 warning (log_warn, no WarningKind)
        pp::log_warn("harness", "dev_cli.cpp", "unknown parameter key; forwarded to encoder",
                     {{"key", key}, {"value", raw}});
        return true;
    }
    switch (def->type) {
    case pp::ParamType::Int: {
        int64_t v = 0;
        if (!parse_int_text(raw, v)) {
            err = "param '" + key + "': expected int";
            return false;
        }
        s[key] = v;
        return true;
    }
    case pp::ParamType::Float: {
        double v = 0;
        if (!parse_double_text(raw, v)) {
            err = "param '" + key + "': expected float";
            return false;
        }
        s[key] = v;
        return true;
    }
    case pp::ParamType::Bool: {
        bool v = false;
        if (!parse_bool_text(raw, v)) {
            err = "param '" + key + "': expected bool";
            return false;
        }
        s[key] = v;
        return true;
    }
    case pp::ParamType::Enum: {
        for (const auto &choice : def->choices) {
            if (choice.first == raw || value_text(choice.second) == raw) {
                s[key] = choice.second;
                return true;
            }
        }
        err = "param '" + key + "': '" + raw + "' is not a valid choice";
        return false;
    }
    }
    err = "param '" + key + "': unsupported type";
    return false;
}

std::string warning_kinds(const pp::FileResult &r) {
    std::string s;
    for (const pp::Warning &w : r.warnings) {
        if (!s.empty())
            s += ",";
        s += warning_name(w.kind);
    }
    return s;
}

const char *state_name(const pp::FileResult &r) {
    if (r.ok)
        return "ok";
    if (r.skipped)
        return "skipped";
    if (r.cancelled)
        return "cancelled";
    return "failed";
}

std::string tail_truncate(const std::string &s, std::size_t n) {
    if (s.size() <= n)
        return s;
    return "..." + s.substr(s.size() - (n - 3));
}

// <out>.pp.json sidecar（0.3.0：逐输出一份）：携带 pp_verify 需要的 warnings 与调试用的
// timing/info。单输出时字段值与 0.2 逐字等价（row.* 即文件级聚合值）。
QJsonObject sidecar_json(const pp::FileResult &r, const pp::OutputResult &row,
                         const std::string &params_snapshot) {
    QJsonArray warnings;
    for (const pp::Warning &w : row.warnings) {
        QJsonObject o;
        o["kind"] = QString::fromStdString(warning_name(w.kind));
        o["detail"] = QString::fromStdString(w.detail);
        warnings.append(o);
    }
    QJsonObject timing;
    timing["decode_ms"] = r.t.decode_ms;
    timing["orient_ms"] = r.t.orient_ms;
    timing["color_ms"] = r.t.color_ms;
    timing["flatten_ms"] = r.t.flatten_ms;
    timing["encode_ms"] = row.t.encode_ms;
    timing["metawrite_ms"] = row.t.metawrite_ms;
    timing["total_ms"] = r.t.total_ms;

    QJsonObject info;
    info["width"] = r.info.width;
    info["height"] = r.info.height;
    info["channels"] = r.info.channels;
    info["src_bitdepth"] = r.info.src_bitdepth;
    info["has_alpha"] = r.info.has_alpha;
    info["is_multipage"] = r.info.is_multipage;
    info["format"] = QString::fromStdString(r.info.format);

    QJsonObject o;
    o["src"] = QString::fromStdString(r.src.string());
    o["out"] = QString::fromStdString(row.out.string());
    o["format"] = QString::fromStdString(row.format_id);
    o["backend"] = QString::fromStdString(row.backend_id);
    o["tech"] = QString::fromStdString(row.tech_id);
    o["state"] = row.ok ? "ok" : (row.skipped ? "skipped" : "failed");
    o["ok"] = row.ok;
    o["skipped"] = row.skipped;
    o["error"] = QString::fromStdString(row.error);
    o["out_bytes"] = static_cast<double>(row.out_bytes);
    o["warnings"] = warnings;
    o["color_src"] = QString::fromStdString(r.color_src);
    o["color_dst"] = QString::fromStdString(r.color_dst);
    o["timing"] = timing;
    o["info"] = info;
    o["params"] = QString::fromStdString(params_snapshot);
    o["progress_reported"] = row.progress_reported; // W1-T6 §7.4 快照（真实行级 vs 合成）
    o["progress_max_row"] = row.progress_max_row;
    // 源文件级聚合（多输出时逐输出行共用；单输出时与上述逐输出字段相同）
    o["file_state"] = QString::fromStdString(state_name(r));
    o["file_ok"] = r.ok;
    o["file_skipped"] = r.skipped;
    o["file_cancelled"] = r.cancelled;
    o["outputs_total"] = static_cast<int>(r.outputs.size());
    o["out_bytes_total"] = static_cast<double>(r.out_bytes);
    return o;
}

void write_sidecars(const pp::FileResult &r, const std::vector<std::string> &params_snapshots) {
    for (std::size_t i = 0; i < r.outputs.size(); ++i) {
        const pp::OutputResult &row = r.outputs[i];
        if (row.out.empty())
            continue;
        const std::string snap = i < params_snapshots.size() ? params_snapshots[i] : std::string();
        const fs::path path = row.out.string() + ".pp.json";
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
            continue;
        const QJsonDocument doc(sidecar_json(r, row, snap));
        const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
        f.write(bytes.constData(), bytes.size());
    }
}

// ---------------------------------------------------------------------------
// W1-T6：--dev 进度事件流侧车（progress-trace.jsonl）
//   逐行 JSON 对象 = 一个 FileEvent（阶段态/进度态/终态），字段与 SCHEMA.md「progress_trace」
//   一节逐字对应；pp_verify 据此断言 overall_frac 单调不倒退 + synthetic 标志（金样
//   progress-trace 对）。写盘纪律（§7.4「进度事件不得淹没日志」）：本文件里只有**已节流**的
//   事件（ProgressMux 每 20ms 或每 0.5% 发点），不是逐行、也不是逐行回调落盘。
//   * 线程：事件回调来自任意 worker 线程与合成泵线程 → 本写入器自带互斥。
//   * R-160（M7-WP8）：seq 分配与写盘同一临界区（旧 seq_++ 在锁外 —— 并发回调下序号可
//     重复/丢失，金样 progress-trace 按 seq 断言可偶发失败）。
//   * 位置：<--out 根目录>/progress-trace.jsonl（--dev 专属；release 构建无此路径）。
// ---------------------------------------------------------------------------
constexpr std::string_view kProgressTraceFile = "progress-trace.jsonl";

class ProgressTraceSink {
public:
    explicit ProgressTraceSink(const fs::path &out_root) : path_(out_root / kProgressTraceFile) {
        f_.open(path_, std::ios::binary | std::ios::trunc);
    }
    bool ok() const { return static_cast<bool>(f_); }
    const fs::path &path() const { return path_; }

    void on_event(const pp::FileEvent &ev) {
        if (!f_)
            return;
        const pp::ProgressInfo &pi = ev.progress;
        QJsonObject o;
        o["file"] = static_cast<double>(ev.index);
        o["state"] = QString::fromLatin1(state_text(ev.state));
        o["output_index"] = pi.output_index;
        o["stage"] = QString::fromLatin1(pp::stage_name(pi.stage));
        o["stage_frac"] = static_cast<double>(pi.stage_frac);
        o["overall_frac"] = static_cast<double>(pi.overall_frac);
        o["synthetic"] = pi.synthetic;
        o["t_ms"] = elapsed_ms();
        // R-160：seq 分配在锁内（与 write 同一临界区）。
        std::lock_guard<std::mutex> lk(mu_);
        o["seq"] = static_cast<double>(seq_++);
        const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n";
        f_.write(line.constData(), line.size());
        f_.flush(); // --dev 侧车要能被崩溃后的现场取证 / 被测试即时读取
    }

private:
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0_)
            .count();
    }
    static const char *state_text(pp::FileState s) {
        switch (s) {
        case pp::FileState::Queued:
            return "queued";
        case pp::FileState::Probing:
            return "probing";
        case pp::FileState::Decoding:
            return "decoding";
        case pp::FileState::Orienting:
            return "orienting";
        case pp::FileState::Coloring:
            return "coloring";
        case pp::FileState::Flattening:
            return "flattening";
        case pp::FileState::Encoding:
            return "encoding";
        case pp::FileState::Writing:
            return "writing";
        case pp::FileState::Done:
            return "done";
        case pp::FileState::Skipped:
            return "skipped";
        case pp::FileState::Failed:
            return "failed";
        case pp::FileState::Cancelled:
            return "cancelled";
        case pp::FileState::Progress:
            return "progress";
        case pp::FileState::DoneWithErrors:
            // R-076 连带补档（M7-R2.1-F4，R2B ③-2）：引擎侧 DoneWithErrors 有损映射已根治
            // （scheduler 直达本态），旧注释「当前不可达」失 trace 会落 "unknown" 的尾巴。
            // 取名 "done_with_errors" = SCHEMA.md state 词表「FileState 的小写名」口径；
            // SCHEMA 值表未列本值属登记余量（tests/golden 冻结面不动），见 handoff。
            return "done_with_errors";
        }
        return "unknown";
    }

    fs::path path_;
    std::ofstream f_;
    std::mutex mu_;
    double seq_ = 0;
    std::chrono::steady_clock::time_point t0_{std::chrono::steady_clock::now()};
};

fs::path choose_base(const fs::path &file, const std::vector<fs::path> &bases) {
    fs::path best;
    for (const fs::path &b : bases) {
        if (pp::is_inside(file, b) && b.string().size() > best.string().size())
            best = b;
    }
    return best.empty() ? file.parent_path() : best;
}

} // namespace

int run_dev(int argc, char **argv) {
    DevOptions o;
    std::string err;
    // M4-W5-T19（§11.3 基准场景，dev-only）：`--dev bench …` —— 位置参数 "bench" 是子命令标记。
    // 位置参数（第一个非 --dev 参数）不是 "bench" 时**逐字**走既有 --dev 路径，行为零变化。
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dev") == 0)
            continue;
        if (std::strcmp(argv[i], "bench") == 0)
            return run_bench(argc, argv);
        break;
    }
    if (!parse_dev_options(argc, argv, o, err)) {
        std::fprintf(stderr, "photopipeline --dev: %s\n%s", err.c_str(), kDevUsage);
        return 2;
    }
    if (o.help) {
        std::fputs(kDevUsage, stdout);
        return 0;
    }

    pp::LogLevel level = pp::LogLevel::Info;
    // R-157（M7-WP8）：文本档解析单源 = pp::log_level_from_text（旧本文件两份逐字重复已删）。
    if (!pp::log_level_from_text(o.log_level, level)) {
        std::fprintf(stderr, "photopipeline --dev: invalid --log-level '%s'\n",
                     o.log_level.c_str());
        return 2;
    }
    // M2-T3 §2.3：PP_LOG_LEVEL 在启动期一次性覆盖（非法值 → stderr 提示并沿用上面的值）。
    // 与 GUI 路径共用 pp::level_from_env_or()，故 --dev/--ui-smoke 同样生效。
    level = pp::level_from_env_or(level);
    pp::ConflictPolicy conflict = pp::ConflictPolicy::Overwrite;
    if (!parse_conflict(o.conflict, conflict)) {
        std::fprintf(stderr, "photopipeline --dev: invalid --conflict '%s'\n", o.conflict.c_str());
        return 2;
    }

    std::error_code ec;
    fs::create_directories(o.out_root, ec);
    if (ec) {
        std::fprintf(stderr, "photopipeline --dev: cannot create --out '%s': %s\n",
                     o.out_root.string().c_str(), ec.message().c_str());
        return 2;
    }
    pp::log_init(o.out_root / "logs", level);

    // ---- base configuration: preset first, command line wins ----
    pp::PresetData preset;
    const bool have_preset = o.has_preset;
    if (have_preset) {
        const std::string perr = pp::ui::load_preset(o.preset, preset);
        if (!perr.empty()) {
            std::fprintf(stderr, "photopipeline --dev: preset '%s': %s\n", o.preset.c_str(),
                         perr.c_str());
            pp::log_shutdown();
            return 2;
        }
    }
    const bool multi_output = !o.outputs.empty();
    if (multi_output && (o.has_format || o.has_backend || o.has_tech)) {
        std::fprintf(stderr, "photopipeline --dev: --outputs is mutually exclusive with "
                             "--format/--backend/--tech (use format[:backend[:tech]] items)\n");
        pp::log_shutdown();
        return 2;
    }
    // 输出清单：--outputs（0.3.0 多输出）优先；否则 0.2 的单格式形态（预设参与）
    // 预设 v2（M4-T13 §3.6：[重排] 为 outputs[] 数组 + output_template/split_by_format）：
    // --dev 的 0.2 单格式语义 = 读 `outputs[0]`（v1 预设由 load_preset 迁移成单元素数组）。
    const bool preset_has_output = have_preset && !preset.outputs.empty();
    std::vector<OutputSpecText> outputs;
    if (multi_output) {
        outputs = o.outputs;
    } else {
        OutputSpecText s;
        s.format =
            o.has_format ? o.format : (preset_has_output ? preset.outputs[0].format_id : "jxl");
        s.backend =
            o.has_backend ? o.backend : (preset_has_output ? preset.outputs[0].backend_id : "");
        s.tech = o.has_tech ? o.tech : (preset_has_output ? preset.outputs[0].tech_id : "");
        outputs.push_back(s);
    }
    const bool lossless =
        o.has_lossless ? o.lossless
                       : (preset_has_output ? pp::lossless_flag(preset.outputs[0].params) : false);
    pp::ColorTarget color = pp::ColorTarget::KeepOriginal;
    if (o.has_color) {
        if (!pp::parse_color_target(o.color, color)) {
            std::fprintf(stderr, "photopipeline --dev: invalid --color '%s'\n", o.color.c_str());
            pp::log_shutdown();
            return 2;
        }
    } else if (have_preset) {
        color = preset.color_target;
    }
    if (!o.has_conflict && have_preset)
        conflict = preset.conflict;
    pp::BatchRules rules = have_preset ? preset.rules : pp::BatchRules{};
    for (const auto &[k, v] : o.metas) {
        pp::TagEdit e;
        e.key = k;
        e.value = v;
        if (k.rfind("Xmp.", 0) == 0) {
            rules.xmp_edits.push_back(std::move(e));
        } else {
            rules.exif_edits.push_back(std::move(e));
        }
    }

    // ---- 路径模板（§4.2）：--template 优先 → 预设的 output_template → 按输出数派生 ----
    // 单输出派生为 0.2 兼容形态 $dir/$file（§1 需求 3「默认行为兼容 v0.2」+ 金样 16 对不回退）；
    // 多输出派生为分文件夹形态 $format/$dir/$file（§3.2 默认 + §4.2「多选输出时 UI 默认 true」）。
    const std::string output_template =
        o.has_output_template ? o.output_template
        : preset_has_output
            ? preset.output_template
            : (outputs.size() > 1 ? std::string("$format/$dir/$file") : std::string("$dir/$file"));
    {
        std::string tmpl_err;
        if (!pp::validate_output_template(output_template, &tmpl_err)) {
            std::fprintf(stderr, "photopipeline --dev: %s\n", tmpl_err.c_str());
            pp::log_shutdown();
            return 2;
        }
    }

    // ---- 逐输出：参数装配（B.6 ③ pp::build_output_specs 单源）+ 位深探测 ----
    // 参数：引擎默认（含无损技术选择与锁定）→ 预设（仅输出 #0，0.2 单格式语义）→ --param。
    // NOTE(0.3.0/§3.2 + M4-T13)：无损语义随 OutputFormatSpec.params 承载 —— 显式 schema 参数
    // `lossless` + 内部管道键 `__lossless` 由参数引擎保持同步（apply_locks）；pipeline 不自行注入。
    std::vector<pp::OutputSpecRequest> reqs;
    std::vector<bool> bitdepth_explicit;
    for (std::size_t oi = 0; oi < outputs.size(); ++oi) {
        const OutputSpecText &s = outputs[oi];
        const pp::FormatDef *fmt = pp::find_format(s.format);
        if (!fmt) {
            std::fprintf(stderr, "photopipeline --dev: unknown --format '%s'\n", s.format.c_str());
            pp::log_shutdown();
            return 2;
        }
        pp::OutputSpecRequest q;
        q.format_id = s.format;
        q.backend_id = s.backend;
        q.tech_id = s.tech;
        if (preset_has_output && oi == 0) {
            for (const auto &[k, v] : preset.outputs[0].params)
                q.base_params[k] = v;
        }
        for (const auto &[k, v] : o.params) {
            if (!set_param(q.base_params, *fmt, s.backend, s.tech, k, v, err)) {
                std::fprintf(stderr, "photopipeline --dev: %s\n", err.c_str());
                pp::log_shutdown();
                return 2;
            }
        }
        const bool explicit_bd = o.has_bitdepth || (preset_has_output && oi == 0);
        bitdepth_explicit.push_back(explicit_bd);
        q.out_bitdepth =
            o.has_bitdepth ? o.bitdepth
                           : ((preset_has_output && oi == 0) ? preset.outputs[0].out_bitdepth : -1);
        reqs.push_back(std::move(q));
    }
    std::vector<pp::OutputFormatSpec> specs;
    if (const std::string verr = pp::build_output_specs(reqs, lossless, specs); !verr.empty()) {
        std::fprintf(stderr, "photopipeline --dev: %s\n", verr.c_str());
        pp::log_shutdown();
        return 2;
    }
    std::vector<std::string> params_snapshots;
    for (std::size_t oi = 0; oi < specs.size(); ++oi) {
        pp::OutputFormatSpec &spec = specs[oi];
        params_snapshots.push_back(pp::snapshot_params(spec.params));
        // 位深：libheif 系走运行时探测交集（10-bit 后端不支持 → 显式报错 / 默认回退 8 + 警告）
        if (spec.format_id == "heif" || spec.format_id == "avif") {
            const int bitdepth = spec.out_bitdepth;
            const std::string supported =
                pp::probe_bitdepth_support(spec.format_id, spec.backend_id);
            auto is_supported = [&supported](int d) {
                const std::string needle = std::to_string(d);
                std::size_t pos = 0;
                while ((pos = supported.find(needle, pos)) != std::string::npos) {
                    const bool left_ok = pos == 0 || supported[pos - 1] == ',';
                    const bool right_ok = pos + needle.size() == supported.size() ||
                                          supported[pos + needle.size()] == ',';
                    if (left_ok && right_ok)
                        return true;
                    pos += needle.size();
                }
                return false;
            };
            if (!is_supported(bitdepth)) {
                if (bitdepth_explicit[oi]) {
                    std::fprintf(stderr,
                                 "photopipeline --dev: %s/%s does not support %d-bit "
                                 "(supported: %s)\n",
                                 spec.format_id.c_str(), spec.backend_id.c_str(), bitdepth,
                                 supported.c_str());
                    pp::log_shutdown();
                    return 2;
                }
                pp::log_warn("harness", "dev_cli.cpp",
                             "10-bit unsupported by backend; default falls back to 8",
                             {{"format", spec.format_id},
                              {"backend", spec.backend_id},
                              {"supported", supported}});
                std::printf("warning: %s/%s supports [%s]; default bit depth falls back to 8\n",
                            spec.format_id.c_str(), spec.backend_id.c_str(), supported.c_str());
                spec.out_bitdepth = 8;
            }
        }
    }

    if (o.metadata_only) {
        if (specs.size() != 1) {
            std::fprintf(stderr,
                         "photopipeline --dev: --metadata-only requires exactly one "
                         "output (got %zu)\n",
                         specs.size());
            pp::log_shutdown();
            return 2;
        }
        if (!pp::format_supports_metadata_only(specs.front().format_id)) {
            std::fprintf(stderr,
                         "photopipeline --dev: format '%s' does not support --metadata-only "
                         "(jpeg/png/tiff/webp only)\n",
                         specs.front().format_id.c_str());
            pp::log_shutdown();
            return 2;
        }
    }

    // ---- inputs ----
    std::vector<std::string> collect_errors;
    std::vector<fs::path> files;
    for (const fs::path &in : o.inputs) {
        std::error_code iec;
        if (fs::is_directory(in, iec)) {
            std::vector<fs::path> found =
                pp::collect_inputs({in}, pp::input_extensions(), collect_errors);
            files.insert(files.end(), found.begin(), found.end());
        } else {
            files.push_back(in);
        }
    }
    for (const std::string &e : collect_errors) {
        std::fprintf(stderr, "photopipeline --dev: %s\n", e.c_str());
        pp::log_warn("harness", "dev_cli.cpp", "input collection problem", {{"error", e}});
    }
    if (files.empty()) {
        std::fprintf(stderr, "photopipeline --dev: no input files\n");
        pp::log_shutdown();
        return 2;
    }
    std::vector<pp::FileEntry> entries;
    entries.reserve(files.size());
    for (const fs::path &f : files) {
        pp::FileEntry fe;
        fe.src = f;
        fe.base_dir = choose_base(f, o.bases);
        entries.push_back(std::move(fe));
    }

    // ---- run configuration ----
    pp::RunConfig cfg;
    cfg.out_root = o.out_root;
    cfg.outputs = specs;
    cfg.color = color;
    cfg.conflict = conflict;
    cfg.rotate_orientation = true;
    cfg.flatten_gray = 1.0;
    cfg.rules = rules;
    cfg.metadata_only = o.metadata_only;
    cfg.workers = o.workers; // 0 = thread_budget 或逻辑核（R-237/283：旧注释"物理核"失实）
    cfg.budget_bytes = 0;
    // R-322（M7-WP8）：split_by_format 为派生态 —— 一律回填 template_has_format 单源判据
    // （旧 `rfind("$format",0)==0` 仅前缀，与 GUI 的"含段"口径分叉）。
    cfg.split_by_format = pp::template_has_format(output_template);
    cfg.output_template = output_template;
    cfg.stagger_ms = 150;  // §3.2 默认；W1-T7 接设置项
    cfg.thread_budget = 0; // §3.2 默认；W1-T7 接设置项

    // R-236：conflict 打印**生效值**（预设覆盖后），不再透传 CLI 原文。
    const char *conflict_out = conflict_text(conflict);
    std::printf("photopipeline --dev\n");
    if (specs.size() == 1) {
        std::printf("  format=%s backend=%s tech=%s lossless=%s bitdepth=%d color=%s conflict=%s\n",
                    specs[0].format_id.c_str(),
                    specs[0].backend_id.empty() ? "(first)" : specs[0].backend_id.c_str(),
                    specs[0].tech_id.empty() ? "(first)" : specs[0].tech_id.c_str(),
                    lossless ? "true" : "false", specs[0].out_bitdepth,
                    pp::to_string(color).c_str(), conflict_out);
        std::printf("  out=%s workers=%d files=%zu metadata_only=%s params=[%s]\n",
                    o.out_root.string().c_str(), o.workers, entries.size(),
                    o.metadata_only ? "true" : "false", params_snapshots.front().c_str());
    } else {
        std::string list;
        for (const pp::OutputFormatSpec &s : specs) {
            if (!list.empty())
                list += ",";
            list += s.format_id;
            if (!s.backend_id.empty())
                list += ":" + s.backend_id;
            if (!s.tech_id.empty())
                list += ":" + s.tech_id;
            list += "@" + std::to_string(s.out_bitdepth);
        }
        std::printf("  outputs=%zu [%s] template=%s lossless=%s color=%s conflict=%s\n",
                    specs.size(), list.c_str(), output_template.c_str(),
                    lossless ? "true" : "false", pp::to_string(color).c_str(), conflict_out);
        std::printf("  out=%s workers=%d files=%zu metadata_only=%s\n", o.out_root.string().c_str(),
                    o.workers, entries.size(), o.metadata_only ? "true" : "false");
        for (std::size_t oi = 0; oi < specs.size(); ++oi) {
            std::printf("    output[%zu] %s params=[%s]\n", oi, specs[oi].format_id.c_str(),
                        params_snapshots[oi].c_str());
        }
    }

    // ---- run header log: versions + parameter snapshot (§4.8 drumbeat item 4) ----
    std::string output_list;
    for (const pp::OutputFormatSpec &s : specs) {
        if (!output_list.empty())
            output_list += ",";
        output_list += s.format_id + (s.backend_id.empty() ? "" : ":" + s.backend_id);
    }
    pp::log_info("run", "dev_cli.cpp", "run start",
                 {{"outputs", output_list},
                  {"output_count", std::to_string(specs.size())},
                  {"template", output_template},
                  {"lossless", lossless ? "true" : "false"},
                  {"color", pp::to_string(color)},
                  {"conflict", conflict_out}, // R-236：生效值
                  {"workers", std::to_string(o.workers)},
                  {"files", std::to_string(entries.size())},
                  {"metadata_only", o.metadata_only ? "true" : "false"},
                  {"out", o.out_root.string()},
                  {"params", params_snapshots.front()}});
    for (const auto &[key, value] : pp::library_versions()) {
        pp::log_info("run", "dev_cli.cpp", "version", {{"lib", key}, {"version", value}});
    }

    pp::Scheduler sched(cfg, std::move(entries));
    // W1-T6：进度事件流侧车（--dev 专属）：逐事件落 <out>/progress-trace.jsonl，
    // 供金样 progress-trace 对断言（格式见 tests/golden/SCHEMA.md「progress_trace」）。
    ProgressTraceSink trace(o.out_root);
    if (!trace.ok()) {
        pp::log_warn("run", "dev_cli.cpp", "progress trace sidecar could not be opened",
                     {{"path", trace.path().string()}});
    }
    sched.set_event_callback([&trace](const pp::FileEvent &ev) { trace.on_event(ev); });
    sched.start();
    sched.wait();

    const std::vector<pp::FileResult> &results = sched.results();
    std::printf("\n%-46s %-9s %10s %9s  %s\n", "file", "state", "bytes", "ms", "warnings");
    for (const pp::FileResult &r : results) {
        std::printf("%-46s %-9s %10llu %9.1f  %s\n", tail_truncate(r.src.string(), 46).c_str(),
                    state_name(r), static_cast<unsigned long long>(r.out_bytes), r.t.total_ms,
                    warning_kinds(r).c_str());
        if (!r.error.empty())
            std::printf("    error: %s\n", r.error.c_str());
        if (r.outputs.size() > 1) {
            // 0.3.0 多输出：逐输出行（与 run 页逐输出行同口径）
            for (const pp::OutputResult &row : r.outputs) {
                std::printf("    %-8s %-9s %10llu  %s\n", row.format_id.c_str(),
                            row.ok ? "ok" : (row.skipped ? "skipped" : "failed"),
                            static_cast<unsigned long long>(row.out_bytes),
                            tail_truncate(row.out.string(), 60).c_str());
                if (!row.error.empty())
                    std::printf("        error: %s\n", row.error.c_str());
            }
        }
        write_sidecars(r, params_snapshots);
    }

    const pp::RunSummary sum = sched.summary();
    int failed = 0;
    for (const pp::FileResult &r : results) {
        if (!r.ok && !r.skipped && !r.cancelled)
            ++failed;
    }
    std::printf("\nSUMMARY files=%zu ok=%zu failed=%zu skipped=%zu cancelled=%zu bytes=%llu "
                "total_ms=%.1f throughput_mb_s=%.2f avg_file_ms=%.1f\n",
                sum.total, sum.ok, sum.failed, sum.skipped, sum.cancelled,
                static_cast<unsigned long long>(sum.out_bytes), sum.total_ms, sum.throughput_mb_s,
                sum.avg_file_ms);
    std::printf("EXIT %d (failed files)\n", failed);
    pp::log_info("run", "dev_cli.cpp", "run summary",
                 {{"files", std::to_string(sum.total)},
                  {"ok", std::to_string(sum.ok)},
                  {"failed", std::to_string(sum.failed)},
                  {"skipped", std::to_string(sum.skipped)},
                  {"cancelled", std::to_string(sum.cancelled)},
                  {"bytes", std::to_string(sum.out_bytes)},
                  {"total_ms", std::to_string(sum.total_ms)},
                  {"throughput_mb_s", std::to_string(sum.throughput_mb_s)},
                  {"avg_file_ms", std::to_string(sum.avg_file_ms)}});
    pp::log_shutdown();
    return failed;
}

} // namespace pp::dev
