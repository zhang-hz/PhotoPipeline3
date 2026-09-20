// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — application entry point.
//
// M1-T8 adds two things here:
//   1. `pp::set_qt_version_string(qVersion())` before anything else, so the run-header log
//      carries the Qt version (§3.1 landing revision; core has no Qt dependency).
//   2. the frozen dev harness `--dev` (§3.15), guarded by PP_BUILD_DEV. The macro is wired in
//      CMakeLists.txt (`if(PP_BUILD_DEV) target_compile_definitions(photopipeline PRIVATE
//      PP_BUILD_DEV) endif()`), so a plain release build contains no dev code path.
//   3. M1b-U10: the `--ui-smoke` scripted walk (§4.3) and the GUI start-up order frozen there
//      (style attempt → data_dir → load_settings → log level → log_init → MainWindow → exec →
//      log_shutdown). The M0 `PP_M0_SMOKE` timed exit is gone; `tests/ui_smoke.sh` replaces it.
//   4. M2-T3 (§2.3): `pp::level_from_env_or()` is the single PP_LOG_LEVEL entry point, called at
//      start-up by the GUI, `--dev` and `--ui-smoke` paths before the level is used.

#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "codecs/encoders.h"
#include "core/fsops.h"
#include "core/logger.h"
#include "core/params.h"
#include "core/pipeline.h"
#include "core/presets.h"
#include "core/scheduler.h"
#include "core/settings.h"
#include "core/types.h"
#include "platform/paths.h"
#include "ui/mainwindow.h"
#include "ui/preset_io.h"

namespace fs = std::filesystem;

namespace {

// 设置文件里的日志级别（5 档）；非法 → 调用方回退 info（§4.3 冻结顺序）
bool log_level_from_text(std::string_view s, pp::LogLevel& out) {
    if (s == "trace") { out = pp::LogLevel::Trace; return true; }
    if (s == "debug") { out = pp::LogLevel::Debug; return true; }
    if (s == "info") { out = pp::LogLevel::Info; return true; }
    if (s == "warn") { out = pp::LogLevel::Warn; return true; }
    if (s == "error") { out = pp::LogLevel::Error; return true; }
    return false;
}

}  // namespace

#ifdef PP_BUILD_DEV
namespace {

// ---------------------------------------------------------------------------
// --dev harness (§3.15)
// ---------------------------------------------------------------------------

const char* kDevUsage =
    "usage: photopipeline --dev <input...> --out <dir> [options]\n"
    "  --out DIR               output root (required)\n"
    "  --format ID             jpeg|jxl|png|tiff|webp|bmp|heif|avif (default jxl)\n"
    "  --backend ID            backend (avif: svt-av1|libaom; default = first)\n"
    "  --tech ID               tech (jxl: vardct|modular; webp: lossy|lossless; default = first)\n"
    "  --lossless              lossless switch\n"
    "  --bitdepth N            output bit depth; default jpeg 8 / jxl 16 / png 16 / tiff 16 /\n"
    "                          webp 8 / bmp 24 / heif 10 / avif 10 (unsupported 10-bit HEIF/AVIF\n"
    "                          default falls back to 8 with a warning)\n"
    "  --color TARGET          keep|srgb|p3|adobergb (default keep)\n"
    "  --conflict POLICY       skip|overwrite|rename (dev default overwrite)\n"
    "  --metadata-only         metadata-only mode (zero re-encode)\n"
    "  --preset FILE           load a preset JSON (command line options win)\n"
    "  --param KEY=VALUE       override one parameter (repeatable)\n"
    "  --meta KEY=VALUE        set/overwrite one metadata tag (repeatable)\n"
    "  --workers N             worker count (0 = physical cores; dev default 1)\n"
    "  --base DIR              mirror-path base directory (repeatable)\n"
    "  --log-level LVL         trace|debug|info|warn|error\n"
    "  -h, --help              this text\n"
    "exit code = number of failed files\n";

struct DevOptions {
    std::vector<fs::path> inputs;
    std::vector<fs::path> bases;
    fs::path out_root;
    bool out_set = false;
    bool help = false;
    std::string format = "jxl", backend, tech, color = "keep", conflict = "overwrite";
    std::string preset, log_level = "info";
    bool lossless = false, metadata_only = false;
    int bitdepth = -1, workers = 1;
    bool has_format = false, has_backend = false, has_tech = false, has_lossless = false;
    bool has_bitdepth = false, has_color = false, has_conflict = false, has_preset = false;
    bool has_workers = false;
    std::vector<std::pair<std::string, std::string>> params, metas;
};

std::string value_text(const pp::ParamValue& v) {
    if (const bool* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    if (const int64_t* i = std::get_if<int64_t>(&v)) return std::to_string(*i);
    if (const double* d = std::get_if<double>(&v)) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.10g", *d);
        return buf;
    }
    if (const std::string* s = std::get_if<std::string>(&v)) return *s;
    return {};
}

const char* warning_name(pp::WarningKind k) {
    switch (k) {
        case pp::WarningKind::DepthDowngrade: return "DepthDowngrade";
        case pp::WarningKind::LossyFromLossless: return "LossyFromLossless";
        case pp::WarningKind::MultipageTruncated: return "MultipageTruncated";
        case pp::WarningKind::AlphaFlattened: return "AlphaFlattened";
        case pp::WarningKind::NoIccAssumeSrgb: return "NoIccAssumeSrgb";
        case pp::WarningKind::MetadataDropped: return "MetadataDropped";
        case pp::WarningKind::TimeFieldMissing: return "TimeFieldMissing";
        case pp::WarningKind::GrayToRgbEncoded: return "GrayToRgbEncoded";
    }
    return "Unknown";
}

int default_bitdepth(std::string_view format) {
    if (format == "jpeg") return 8;
    if (format == "jxl") return 16;
    if (format == "png") return 16;
    if (format == "tiff") return 16;
    if (format == "webp") return 8;
    if (format == "bmp") return 24;
    if (format == "heif" || format == "avif") return 10;
    return 8;
}

bool parse_bool_text(std::string_view s, bool& out) {
    if (s == "true" || s == "1" || s == "yes" || s == "on") { out = true; return true; }
    if (s == "false" || s == "0" || s == "no" || s == "off") { out = false; return true; }
    return false;
}

bool parse_int_text(std::string_view s, int64_t& out) {
    const char* b = s.data();
    const char* e = s.data() + s.size();
    const std::from_chars_result r = std::from_chars(b, e, out);
    return r.ec == std::errc() && r.ptr == e;
}

bool parse_double_text(std::string_view s, double& out) {
    try {
        const std::string tmp(s);
        std::size_t used = 0;
        out = std::stod(tmp, &used);
        return used == tmp.size();
    } catch (...) {
        return false;
    }
}

bool split_kv(std::string_view arg, std::string& key, std::string& value) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string_view::npos || eq == 0) return false;
    key.assign(arg.substr(0, eq));
    value.assign(arg.substr(eq + 1));
    return true;
}

bool parse_dev_options(int argc, char** argv, DevOptions& o, std::string& err) {
    auto need = [&](int& i, const char* opt) -> const char* {
        if (i + 1 >= argc) {
            err = std::string(opt) + " requires a value";
            return nullptr;
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--dev") continue;
        if (a == "-h" || a == "--help") { o.help = true; return true; }
        if (a == "--out") {
            const char* v = need(i, "--out"); if (!v) return false;
            o.out_root = v; o.out_set = true;
        } else if (a == "--format") {
            const char* v = need(i, "--format"); if (!v) return false;
            o.format = v; o.has_format = true;
        } else if (a == "--backend") {
            const char* v = need(i, "--backend"); if (!v) return false;
            o.backend = v; o.has_backend = true;
        } else if (a == "--tech") {
            const char* v = need(i, "--tech"); if (!v) return false;
            o.tech = v; o.has_tech = true;
        } else if (a == "--lossless") {
            o.lossless = true; o.has_lossless = true;
        } else if (a == "--bitdepth") {
            const char* v = need(i, "--bitdepth"); if (!v) return false;
            int64_t n = 0;
            if (!parse_int_text(v, n) || n <= 0) { err = "invalid --bitdepth value"; return false; }
            o.bitdepth = static_cast<int>(n); o.has_bitdepth = true;
        } else if (a == "--color") {
            const char* v = need(i, "--color"); if (!v) return false;
            o.color = v; o.has_color = true;
        } else if (a == "--conflict") {
            const char* v = need(i, "--conflict"); if (!v) return false;
            o.conflict = v; o.has_conflict = true;
        } else if (a == "--metadata-only") {
            o.metadata_only = true;
        } else if (a == "--preset") {
            const char* v = need(i, "--preset"); if (!v) return false;
            o.preset = v; o.has_preset = true;
        } else if (a == "--param") {
            const char* v = need(i, "--param"); if (!v) return false;
            std::string k, val;
            if (!split_kv(v, k, val)) { err = "--param expects KEY=VALUE"; return false; }
            o.params.emplace_back(std::move(k), std::move(val));
        } else if (a == "--meta") {
            const char* v = need(i, "--meta"); if (!v) return false;
            std::string k, val;
            if (!split_kv(v, k, val)) { err = "--meta expects KEY=VALUE"; return false; }
            o.metas.emplace_back(std::move(k), std::move(val));
        } else if (a == "--workers") {
            const char* v = need(i, "--workers"); if (!v) return false;
            int64_t n = 0;
            if (!parse_int_text(v, n) || n < 0) { err = "invalid --workers value"; return false; }
            o.workers = static_cast<int>(n); o.has_workers = true;
        } else if (a == "--base") {
            const char* v = need(i, "--base"); if (!v) return false;
            o.bases.emplace_back(v);
        } else if (a == "--log-level") {
            const char* v = need(i, "--log-level"); if (!v) return false;
            o.log_level = v;
        } else if (!a.empty() && a.front() == '-' && a != "-") {
            err = "unknown option '" + std::string(a) + "'";
            return false;
        } else {
            o.inputs.emplace_back(a);
        }
    }
    if (!o.out_set) { err = "--out DIR is required"; return false; }
    if (o.inputs.empty()) { err = "at least one input file or directory is required"; return false; }
    return true;
}

bool parse_log_level(std::string_view s, pp::LogLevel& out) {
    if (s == "trace") { out = pp::LogLevel::Trace; return true; }
    if (s == "debug") { out = pp::LogLevel::Debug; return true; }
    if (s == "info") { out = pp::LogLevel::Info; return true; }
    if (s == "warn") { out = pp::LogLevel::Warn; return true; }
    if (s == "error") { out = pp::LogLevel::Error; return true; }
    return false;
}

bool parse_conflict(std::string_view s, pp::ConflictPolicy& out) {
    if (s == "skip") { out = pp::ConflictPolicy::Skip; return true; }
    if (s == "overwrite") { out = pp::ConflictPolicy::Overwrite; return true; }
    if (s == "rename") { out = pp::ConflictPolicy::Rename; return true; }
    return false;
}

// --param KEY=VALUE → typed ParamValue using the format's parameter table. Keys that are not in
// the table are kept as strings so the encoder can report them through its E9 warning path.
bool set_param(pp::ParamSet& s, const pp::FormatDef& f, const std::string& backend,
               const std::string& tech, const std::string& key, const std::string& raw,
               std::string& err) {
    const pp::ParamDef* def = nullptr;
    const pp::BackendDef* b = pp::find_backend(f, backend);
    if (b) {
        for (const pp::TechDef& t : b->techs) {
            if (!tech.empty() && t.id != tech) continue;
            for (const pp::ParamDef& p : t.params) {
                if (p.key == key) def = &p;
            }
        }
    }
    if (!def) {
        s[key] = raw;  // unknown → encoder E9 warning (log_warn, no WarningKind)
        pp::log_warn("harness", "main.cpp", "unknown parameter key; forwarded to encoder",
                     {{"key", key}, {"value", raw}});
        return true;
    }
    switch (def->type) {
        case pp::ParamType::Int: {
            int64_t v = 0;
            if (!parse_int_text(raw, v)) { err = "param '" + key + "': expected int"; return false; }
            s[key] = v;
            return true;
        }
        case pp::ParamType::Float: {
            double v = 0;
            if (!parse_double_text(raw, v)) { err = "param '" + key + "': expected float"; return false; }
            s[key] = v;
            return true;
        }
        case pp::ParamType::Bool: {
            bool v = false;
            if (!parse_bool_text(raw, v)) { err = "param '" + key + "': expected bool"; return false; }
            s[key] = v;
            return true;
        }
        case pp::ParamType::Enum: {
            for (const auto& choice : def->choices) {
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

std::string warning_kinds(const pp::FileResult& r) {
    std::string s;
    for (const pp::Warning& w : r.warnings) {
        if (!s.empty()) s += ",";
        s += warning_name(w.kind);
    }
    return s;
}

const char* state_name(const pp::FileResult& r) {
    if (r.ok) return "ok";
    if (r.skipped) return "skipped";
    if (r.cancelled) return "cancelled";
    return "failed";
}

std::string tail_truncate(const std::string& s, std::size_t n) {
    if (s.size() <= n) return s;
    return "..." + s.substr(s.size() - (n - 3));
}

// <out>.pp.json sidecar: carries the pipeline warnings/timings that pp_verify needs for
// `warnings_contain` and for debugging (the output file itself cannot express them).
QJsonObject sidecar_json(const pp::FileResult& r, const std::string& params_snapshot) {
    QJsonArray warnings;
    for (const pp::Warning& w : r.warnings) {
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
    timing["encode_ms"] = r.t.encode_ms;
    timing["metawrite_ms"] = r.t.metawrite_ms;
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
    o["out"] = QString::fromStdString(r.out.string());
    o["state"] = state_name(r);
    o["ok"] = r.ok;
    o["skipped"] = r.skipped;
    o["cancelled"] = r.cancelled;
    o["error"] = QString::fromStdString(r.error);
    o["out_bytes"] = static_cast<double>(r.out_bytes);
    o["warnings"] = warnings;
    o["color_src"] = QString::fromStdString(r.color_src);
    o["color_dst"] = QString::fromStdString(r.color_dst);
    o["timing"] = timing;
    o["info"] = info;
    o["params"] = QString::fromStdString(params_snapshot);
    return o;
}

void write_sidecar(const pp::FileResult& r, const std::string& params_snapshot) {
    if (r.out.empty()) return;
    const fs::path path = r.out.string() + ".pp.json";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    const QJsonDocument doc(sidecar_json(r, params_snapshot));
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    f.write(bytes.constData(), bytes.size());
}

fs::path choose_base(const fs::path& file, const std::vector<fs::path>& bases) {
    fs::path best;
    for (const fs::path& b : bases) {
        if (pp::is_inside(file, b) && b.string().size() > best.string().size()) best = b;
    }
    return best.empty() ? file.parent_path() : best;
}

int run_dev(int argc, char** argv) {
    DevOptions o;
    std::string err;
    if (!parse_dev_options(argc, argv, o, err)) {
        std::fprintf(stderr, "photopipeline --dev: %s\n%s", err.c_str(), kDevUsage);
        return 2;
    }
    if (o.help) {
        std::fputs(kDevUsage, stdout);
        return 0;
    }

    pp::LogLevel level = pp::LogLevel::Info;
    if (!parse_log_level(o.log_level, level)) {
        std::fprintf(stderr, "photopipeline --dev: invalid --log-level '%s'\n", o.log_level.c_str());
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
    const std::string format = o.has_format ? o.format : (have_preset ? preset.format_id : "jxl");
    const std::string backend = o.has_backend ? o.backend : (have_preset ? preset.backend_id : "");
    const std::string tech = o.has_tech ? o.tech : (have_preset ? preset.tech_id : "");
    const bool lossless = o.has_lossless ? o.lossless : (have_preset ? preset.lossless : false);
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
    if (!o.has_conflict && have_preset) conflict = preset.conflict;
    pp::BatchRules rules = have_preset ? preset.rules : pp::BatchRules{};

    const pp::FormatDef* fmt = pp::find_format(format);
    if (!fmt) {
        std::fprintf(stderr, "photopipeline --dev: unknown --format '%s'\n", format.c_str());
        pp::log_shutdown();
        return 2;
    }
    if (o.metadata_only && !pp::format_supports_metadata_only(format)) {
        std::fprintf(stderr,
                     "photopipeline --dev: format '%s' does not support --metadata-only "
                     "(jpeg/png/tiff/webp only)\n",
                     format.c_str());
        pp::log_shutdown();
        return 2;
    }

    // ---- parameters: defaults → preset → --param → locks ----
    pp::ParamSet params = pp::default_params(*fmt, backend, tech, lossless);
    if (have_preset) {
        for (const auto& [k, v] : preset.params) params[k] = v;
    }
    for (const auto& [k, v] : o.params) {
        if (!set_param(params, *fmt, backend, tech, k, v, err)) {
            std::fprintf(stderr, "photopipeline --dev: %s\n", err.c_str());
            pp::log_shutdown();
            return 2;
        }
    }
    for (const auto& [k, v] : o.metas) {
        pp::TagEdit e;
        e.key = k;
        e.value = v;
        if (k.rfind("Xmp.", 0) == 0) {
            rules.xmp_edits.push_back(std::move(e));
        } else {
            rules.exif_edits.push_back(std::move(e));
        }
    }
    pp::fill_defaults(*fmt, backend, tech, lossless, params);
    params["__lossless"] = lossless;
    pp::apply_locks(*fmt, backend, tech, lossless, params);
    const std::string verr = pp::validate_params(*fmt, backend, tech, lossless, params);
    if (!verr.empty()) {
        std::fprintf(stderr, "photopipeline --dev: invalid parameters: %s\n", verr.c_str());
        pp::log_shutdown();
        return 2;
    }

    // ---- bit depth: per-format default, runtime probe intersection for libheif formats ----
    const bool bitdepth_explicit = o.has_bitdepth || have_preset;
    int bitdepth = o.has_bitdepth ? o.bitdepth
                                  : (have_preset ? preset.out_bitdepth : default_bitdepth(format));
    if (std::find(fmt->bitdepths.begin(), fmt->bitdepths.end(), bitdepth) ==
        fmt->bitdepths.end()) {
        std::fprintf(stderr, "photopipeline --dev: bit depth %d is not supported by format '%s'\n",
                     bitdepth, format.c_str());
        pp::log_shutdown();
        return 2;
    }
    if (format == "heif" || format == "avif") {
        const std::string supported = pp::probe_bitdepth_support(format, backend);
        auto is_supported = [&supported](int d) {
            const std::string needle = std::to_string(d);
            std::size_t pos = 0;
            while ((pos = supported.find(needle, pos)) != std::string::npos) {
                const bool left_ok = pos == 0 || supported[pos - 1] == ',';
                const bool right_ok = pos + needle.size() == supported.size() ||
                                      supported[pos + needle.size()] == ',';
                if (left_ok && right_ok) return true;
                pos += needle.size();
            }
            return false;
        };
        if (!is_supported(bitdepth)) {
            if (bitdepth_explicit) {
                std::fprintf(stderr,
                             "photopipeline --dev: %s/%s does not support %d-bit "
                             "(supported: %s)\n",
                             format.c_str(), backend.c_str(), bitdepth, supported.c_str());
                pp::log_shutdown();
                return 2;
            }
            pp::log_warn("harness", "main.cpp",
                         "10-bit unsupported by backend; default falls back to 8",
                         {{"format", format}, {"backend", backend}, {"supported", supported}});
            std::printf("warning: %s/%s supports [%s]; default bit depth falls back to 8\n",
                        format.c_str(), backend.c_str(), supported.c_str());
            bitdepth = 8;
        }
    }

    // ---- inputs ----
    std::vector<std::string> collect_errors;
    std::vector<fs::path> files;
    for (const fs::path& in : o.inputs) {
        std::error_code iec;
        if (fs::is_directory(in, iec)) {
            std::vector<fs::path> found =
                pp::collect_inputs({in}, pp::input_extensions(), collect_errors);
            files.insert(files.end(), found.begin(), found.end());
        } else {
            files.push_back(in);
        }
    }
    for (const std::string& e : collect_errors) {
        std::fprintf(stderr, "photopipeline --dev: %s\n", e.c_str());
        pp::log_warn("harness", "main.cpp", "input collection problem", {{"error", e}});
    }
    if (files.empty()) {
        std::fprintf(stderr, "photopipeline --dev: no input files\n");
        pp::log_shutdown();
        return 2;
    }
    std::vector<pp::FileEntry> entries;
    entries.reserve(files.size());
    for (const fs::path& f : files) {
        pp::FileEntry fe;
        fe.src = f;
        fe.base_dir = choose_base(f, o.bases);
        entries.push_back(std::move(fe));
    }

    // ---- run configuration ----
    pp::RunConfig cfg;
    cfg.out_root = o.out_root;
    cfg.format_id = format;
    cfg.backend_id = backend;
    cfg.tech_id = tech;
    cfg.lossless = lossless;
    cfg.params = params;
    cfg.out_bitdepth = bitdepth;
    cfg.color_target = color;
    cfg.conflict = conflict;
    cfg.rotate_orientation = true;
    cfg.flatten_gray = 1.0;
    cfg.rules = rules;
    cfg.metadata_only = o.metadata_only;
    cfg.workers = o.workers;  // 0 = physical cores
    cfg.budget_bytes = 0;

    const std::string params_snapshot = pp::snapshot_params(params);
    std::printf("photopipeline --dev\n");
    std::printf("  format=%s backend=%s tech=%s lossless=%s bitdepth=%d color=%s conflict=%s\n",
                format.c_str(), backend.empty() ? "(first)" : backend.c_str(),
                tech.empty() ? "(first)" : tech.c_str(), lossless ? "true" : "false", bitdepth,
                pp::to_string(color).c_str(), o.conflict.c_str());
    std::printf("  out=%s workers=%d files=%zu metadata_only=%s params=[%s]\n",
                o.out_root.string().c_str(), o.workers, entries.size(),
                o.metadata_only ? "true" : "false", params_snapshot.c_str());

    // ---- run header log: versions + parameter snapshot (§4.8 drumbeat item 4) ----
    pp::log_info("run", "main.cpp", "run start",
                 {{"format", format},
                  {"backend", backend},
                  {"tech", tech},
                  {"lossless", lossless ? "true" : "false"},
                  {"bitdepth", std::to_string(bitdepth)},
                  {"color", pp::to_string(color)},
                  {"conflict", o.conflict},
                  {"workers", std::to_string(o.workers)},
                  {"files", std::to_string(entries.size())},
                  {"metadata_only", o.metadata_only ? "true" : "false"},
                  {"out", o.out_root.string()},
                  {"params", params_snapshot}});
    for (const auto& [key, value] : pp::library_versions()) {
        pp::log_info("run", "main.cpp", "version", {{"lib", key}, {"version", value}});
    }

    pp::Scheduler sched(cfg, std::move(entries));
    sched.start();
    sched.wait();

    const std::vector<pp::FileResult>& results = sched.results();
    std::printf("\n%-46s %-9s %10s %9s  %s\n", "file", "state", "bytes", "ms", "warnings");
    for (const pp::FileResult& r : results) {
        std::printf("%-46s %-9s %10llu %9.1f  %s\n",
                    tail_truncate(r.src.string(), 46).c_str(), state_name(r),
                    static_cast<unsigned long long>(r.out_bytes), r.t.total_ms,
                    warning_kinds(r).c_str());
        if (!r.error.empty()) std::printf("    error: %s\n", r.error.c_str());
        write_sidecar(r, params_snapshot);
    }

    const pp::RunSummary sum = sched.summary();
    int failed = 0;
    for (const pp::FileResult& r : results) {
        if (!r.ok && !r.skipped && !r.cancelled) ++failed;
    }
    std::printf(
        "\nSUMMARY files=%zu ok=%zu failed=%zu skipped=%zu cancelled=%zu bytes=%llu "
        "total_ms=%.1f throughput_mb_s=%.2f avg_file_ms=%.1f\n",
        sum.total, sum.ok, sum.failed, sum.skipped, sum.cancelled,
        static_cast<unsigned long long>(sum.out_bytes), sum.total_ms, sum.throughput_mb_s,
        sum.avg_file_ms);
    std::printf("EXIT %d (failed files)\n", failed);
    pp::log_info("run", "main.cpp", "run summary",
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

}  // namespace

// ---------------------------------------------------------------------------
// --ui-smoke（§4.3 冻结）：参数在 QApplication 之前解析，同 --dev 惯例
// ---------------------------------------------------------------------------

namespace {

const char* kUiSmokeUsage =
    "usage: photopipeline --ui-smoke [--inputs DIR] [--shots DIR]\n"
    "  --inputs DIR   input directory (default <repo>/tests/golden/base)\n"
    "  --shots DIR    screenshot directory (default .cache/ui-review; empty = do not save)\n"
    "exit code 0 = all frozen assertions passed; 1 = smoke failure; 2 = usage/argument error\n";

// 可执行文件目录（Linux：/proc/self/exe；失败回退 argv[0]）
fs::path executable_directory(const char* argv0) {
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && !exe.empty()) return exe.parent_path();
    const fs::path arg = fs::absolute(fs::path(argv0 != nullptr ? argv0 : ""), ec);
    return arg.parent_path();
}

// 从可执行文件向上找仓库根：含 .git 或 CMakeLists.txt 的最近目录
std::string find_repo_root(const char* argv0) {
    std::error_code ec;
    fs::path dir = executable_directory(argv0);
    for (int i = 0; i < 8 && !dir.empty(); ++i) {
        if (fs::exists(dir / ".git", ec) || fs::exists(dir / "CMakeLists.txt", ec)) {
            return dir.string();
        }
        const fs::path up = dir.parent_path();
        if (up == dir) break;
        dir = up;
    }
    return {};
}

int run_ui_smoke(int argc, char** argv) {
    QString inputs;
    QString shots = QStringLiteral(".cache/ui-review");
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--ui-smoke") continue;
        if (a == "--inputs" || a == "--shots") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "photopipeline --ui-smoke: %s requires a value\n%s",
                             std::string(a).c_str(), kUiSmokeUsage);
                return 2;
            }
            const QString value = QString::fromLocal8Bit(argv[++i]);
            if (a == "--inputs") {
                inputs = value;
            } else {
                shots = value;
            }
            continue;
        }
        std::fprintf(stderr, "photopipeline --ui-smoke: unknown option '%s'\n%s",
                     std::string(a).c_str(), kUiSmokeUsage);
        return 2;
    }
    if (inputs.isEmpty()) {
        const std::string root = find_repo_root(argv[0]);
        if (root.empty()) {
            std::fprintf(stderr,
                         "photopipeline --ui-smoke: cannot locate the repository root (searched "
                         "upward from the executable directory for .git/CMakeLists.txt); pass "
                         "--inputs DIR\n");
            return 1;
        }
        inputs = QString::fromStdString((fs::path(root) / "tests/golden/base").string());
    }
    if (!QFileInfo::exists(inputs)) {
        std::fprintf(stderr, "photopipeline --ui-smoke: input directory does not exist: %s\n",
                     inputs.toLocal8Bit().constData());
        return 1;
    }

    // 冒烟分支不走磁盘 settings/log：默认 AppSettings；日志自然走 stderr（§4.3）。
    // M2-T3 §2.3：PP_LOG_LEVEL 亦作用于 --ui-smoke——此处只影响 stderr 过滤；未设置时
    // level_from_env_or(Info) 与默认级别相同，输出与现状逐字节一致。
    pp::log_set_level(pp::level_from_env_or(pp::LogLevel::Info));
    QApplication app(argc, argv);
    pp::ui::MainWindow w(pp::AppSettings{});
    w.resize(1440, 900);
    w.show();
    w.set_offline_maps(true);
    w.add_paths(QStringList{inputs});
    w.ui_smoke_walk(shots);   // walk 内部自跑事件循环并写 pp_ui_smoke_exit

    const QVariant ran = w.property("pp_ui_smoke_ran");
    if (!ran.isValid() || !ran.toBool()) {
        std::fprintf(stderr, "photopipeline --ui-smoke: walk did not run\n");
        return 1;
    }
    return w.property("pp_ui_smoke_exit").toInt();
}

}  // namespace
#endif  // PP_BUILD_DEV

int main(int argc, char** argv) {
    // §3.1: core has no Qt dependency; the version string is injected here (before log_init).
    pp::set_qt_version_string(qVersion());

#ifdef PP_BUILD_DEV
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dev") == 0) {
            return run_dev(argc, argv);
        }
        if (std::strcmp(argv[i], "--ui-smoke") == 0) {
            return run_ui_smoke(argc, argv);
        }
    }
#else
    // M1-T15: a release build contains no dev harness. This must happen *before* QApplication
    // is constructed: Qt6 silently ignores unknown long options (T14 measured `--dev` → exit 0
    // with empty stdout/stderr), so without this check a release binary would just start the
    // GUI. Report the missing harness and fail with a non-zero exit code instead.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dev") == 0 || std::strcmp(argv[i], "--ui-smoke") == 0) {
            std::fprintf(
                stderr,
                "photopipeline: dev harness not built (rebuild with -DPP_BUILD_DEV=ON)\n");
            return 2;
        }
    }
#endif

    // §4.3 冻结启动顺序：style 尝试 → data_dir → load_settings → 日志级别 → log_init →
    //                    MainWindow(settings) → show → exec → log_shutdown
    QApplication app(argc, argv);
    if (QApplication::setStyle(QStringLiteral("Fluent")) == nullptr) {
        qInfo("PhotoPipeline: style 'Fluent' unavailable; keeping the default style");
    }
    pp::platform::data_dir();   // 确保便携/回退目录存在（结果缓存）
    pp::AppSettings settings = pp::load_settings(pp::platform::settings_file());
    pp::LogLevel level = pp::LogLevel::Info;
    if (!log_level_from_text(settings.log_level, level)) level = pp::LogLevel::Info;
    // M2-T3 §2.3：环境变量在启动期一次性覆盖 settings.log_level（非法值 → stderr 一行提示，
    // 沿用 settings 值；未设置 → 与 M1b 行为完全一致）。GUI 与 --dev/--ui-smoke 同一入口。
    level = pp::level_from_env_or(level);
    pp::log_init(pp::platform::logs_dir(), level);
    pp::ui::MainWindow w(settings);
    w.show();
    const int code = app.exec();
    pp::log_shutdown();
    return code;
}
