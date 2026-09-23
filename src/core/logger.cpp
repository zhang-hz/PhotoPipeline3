// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — logger implementation.
//
// Contract: docs/m1-tasks.md §3.1 (PP-FROZEN) / §4.1 + docs/m2-tasks.md §2.3/§2.4 (M2-T3).
//   * spdlog basic_file_sink (non-rotating), 20 newest run-*.log files kept by hand
//   * level overridable through PP_LOG_LEVEL (case-insensitive); §2.3 adds the explicit
//     startup entry point level_from_env_or() (invalid value -> one stderr line)
//   * §2.4 single-file size cap: > 16 MiB -> frozen note line + last 8 MiB kept, checked on
//     the write path (accounted bytes, no whole-file read-back); the 20-file rule is untouched
//   * line format: HH:MM:SS.mmm [lvl] [tid] [stage] [file] message {k=v k=v}
//   * warn and above flush immediately
//   * never initialized / sink open failure / any internal error -> stderr, never throws

#include "core/logger.h"
#include "core/version.h"

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/oiioversion.h>
#include <exiv2/exiv2.hpp>
#include <jxl/encode.h>
#include <lcms2.h>
#include <libheif/heif.h>
#include <tiffio.h>
#include <webp/encode.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace pp {
namespace {

// M2-T11 §2.2 版本单源：app 版本取自生成的 core/version.h，唯一来源 = 顶层
// project(PhotoPipeline VERSION …)。T3 的内联字面量随之删除——当时顾虑的
// target_compile_definitions 已由 configure_file 生成头替代（无需额外编译定义）。
constexpr const char *kAppVersion = PP_VERSION_STRING;

// jpegli exposes no version API (only jpeg_* compatibility macros); the overlay pins the
// upstream commit, see vcpkg-overlay/libjpeg-turbo/portfile.cmake REF.
constexpr const char *kJpegliCommit = "031a0077";

constexpr std::size_t kKeepRunFiles = 20;

// M2-T3 §2.4 frozen size cap.
constexpr std::uintmax_t kSizeCapBytes = 16u * 1024u * 1024u; // 16 MiB
constexpr std::uintmax_t kTailBytes = 8u * 1024u * 1024u;     // 8 MiB
constexpr const char *kTruncateNote = "[note] log truncated (size cap 16MiB, tail kept 8MiB)\n";
// Upper bound of the spdlog pattern overhead around the rendered body
// ("HH:MM:SS.mmm [level] [tid] " + '\n'): timestamp 12 + brackets/level 12 + tid <= 20 + 3.
constexpr std::size_t kLineOverhead = 64;

constexpr const char *kLogPattern = "%H:%M:%S.%e [%l] [%t] %v";

std::mutex g_mu;
std::shared_ptr<spdlog::logger> g_logger; // null -> stderr fallback
LogLevel g_level = LogLevel::Info;
std::string g_qt_version;
// §2.4 accounting: active run file and its accounted size (guarded by g_mu).
std::filesystem::path g_log_path; // empty -> no file sink
std::uintmax_t g_log_bytes = 0;

spdlog::level::level_enum to_spdlog(LogLevel lv) noexcept {
    switch (lv) {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warn:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Critical:
        return spdlog::level::critical;
    }
    return spdlog::level::info;
}

const char *level_name(LogLevel lv) noexcept {
    switch (lv) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warn:
        return "warning";
    case LogLevel::Error:
        return "error";
    case LogLevel::Critical:
        return "critical";
    }
    return "info";
}

bool parse_level(std::string_view s, LogLevel &out) noexcept {
    std::string lowered;
    lowered.reserve(s.size());
    for (const char c : s) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lowered == "trace") {
        out = LogLevel::Trace;
        return true;
    }
    if (lowered == "debug") {
        out = LogLevel::Debug;
        return true;
    }
    if (lowered == "info") {
        out = LogLevel::Info;
        return true;
    }
    if (lowered == "warn") {
        out = LogLevel::Warn;
        return true;
    }
    if (lowered == "error") {
        out = LogLevel::Error;
        return true;
    }
    if (lowered == "critical") {
        out = LogLevel::Critical;
        return true;
    }
    return false;
}

LogLevel env_level_or(LogLevel fallback) noexcept {
    const char *env = std::getenv("PP_LOG_LEVEL");
    LogLevel lv = fallback;
    if (env != nullptr && parse_level(env, lv)) {
        return lv;
    }
    return fallback;
}

unsigned long current_tid() noexcept {
#if defined(__linux__)
    return static_cast<unsigned long>(::syscall(SYS_gettid));
#else
    return static_cast<unsigned long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

std::string timestamp_now(bool with_date) {
    const auto now = std::chrono::system_clock::now();
    const auto secs = std::chrono::system_clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char buf[32];
    if (with_date) {
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900,
                      tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                      static_cast<int>(ms));
    }
    return std::string(buf);
}

// k=v rendering: values containing whitespace / quotes / braces are quoted and escaped.
std::string render_value(std::string_view v) {
    bool quote = v.empty();
    for (const char c : v) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\\' || c == '{' ||
            c == '}') {
            quote = true;
            break;
        }
    }
    if (!quote) {
        return std::string(v);
    }
    std::string out;
    out.reserve(v.size() + 2);
    out.push_back('"');
    for (const char c : v) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    out.push_back('"');
    return out;
}

std::string render_body(std::string_view stage, std::string_view file, std::string_view msg,
                        LogFields fields) {
    std::string out;
    out.reserve(stage.size() + file.size() + msg.size() + 24 + fields.size() * 16);
    out.push_back('[');
    out.append(stage);
    out += "] [";
    out.append(file);
    out += "] ";
    out.append(msg);
    if (fields.size() != 0) {
        out += " {";
        bool first = true;
        for (const auto &kv : fields) {
            if (!first) {
                out.push_back(' ');
            }
            first = false;
            out.append(kv.first);
            out.push_back('=');
            out += render_value(kv.second);
        }
        out.push_back('}');
    }
    return out;
}

void write_stderr(LogLevel lv, const std::string &body) noexcept {
    std::fprintf(stderr, "%s [%s] [%lu] %s\n", timestamp_now(false).c_str(), level_name(lv),
                 current_tid(), body.c_str());
    std::fflush(stderr);
}

// Delete the oldest run-*.log files so that at most kKeepRunFiles remain (errors ignored).
void prune_old_runs(const std::filesystem::path &dir) noexcept {
    // NOTE(cap): the size rule of M2 §2.4 is enforced on the write path
    // (enforce_size_cap_locked), not here: the 20-file rule is unchanged and independent.
    try {
        using Entry = std::pair<std::filesystem::file_time_type, std::filesystem::path>;
        std::vector<Entry> files;
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec);
        const std::filesystem::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code sub;
            if (!it->is_regular_file(sub)) {
                continue;
            }
            const std::filesystem::path p = it->path();
            const std::string name = p.filename().string();
            if (name.rfind("run-", 0) != 0 || p.extension() != ".log") {
                continue;
            }
            std::error_code tec;
            std::filesystem::file_time_type t = std::filesystem::last_write_time(p, tec);
            if (tec) {
                t = std::filesystem::file_time_type{};
            }
            files.emplace_back(t, p);
        }
        if (files.size() <= kKeepRunFiles) {
            return;
        }
        std::sort(files.begin(), files.end(), [](const Entry &a, const Entry &b) {
            if (a.first != b.first) {
                return a.first < b.first;
            }
            return a.second.string() < b.second.string();
        });
        const std::size_t drop = files.size() - kKeepRunFiles;
        for (std::size_t i = 0; i < drop; ++i) {
            std::error_code rec;
            std::filesystem::remove(files[i].second, rec);
        }
    } catch (...) {
        // retention must never break logging
    }
}

// File sink construction shared by log_init and the §2.4 rewrite path (append mode, as M1a).
std::shared_ptr<spdlog::logger> make_file_logger(const std::filesystem::path &file,
                                                 spdlog::level::level_enum lv) {
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file.string(), false);
    auto lg = std::make_shared<spdlog::logger>("pp", std::move(sink));
    // M3 v1.9：显式给定 eol="\n"。spdlog 默认 eol 在 Windows 为 "\r\n"（os::default_eol），
    // 使日志行尾与 Linux 不一致（冻结行格式按 LF 锚定；getline 保留 \r 会让 $ 失配）。
    // 日志文件跨平台逐字节同格式；控制台/stderr 仍由 CRT 文本模式按平台惯例处理。
    lg->set_formatter(std::make_unique<spdlog::pattern_formatter>(
        kLogPattern, spdlog::pattern_time_type::local, "\n"));
    lg->set_level(lv);
    return lg;
}

// §2.4: called with g_mu held, immediately before writing `incoming` bytes.
// Accounting is done from the bytes we hand to the sink (plus a fixed upper bound of the
// pattern prefix), so the decision never reads the file back. When the cap is crossed the file
// is rewritten as the frozen note line plus the last kTailBytes (only that window is read), and
// a fresh sink is opened on the same path. Best effort: any failure keeps the old logger.
std::shared_ptr<spdlog::logger> enforce_size_cap_locked(std::shared_ptr<spdlog::logger> lg,
                                                        std::size_t incoming) noexcept {
    if (g_log_path.empty() || g_log_bytes + incoming <= kSizeCapBytes) {
        return lg;
    }
    try {
        lg->flush();
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(g_log_path, ec);
        if (ec || size <= kTailBytes) {
            return lg;
        }
        const std::uintmax_t from = size - kTailBytes;
        std::string tail;
        {
            std::ifstream in(g_log_path, std::ios::binary);
            if (!in) {
                return lg;
            }
            in.seekg(static_cast<std::streamoff>(from), std::ios::beg);
            if (!in) {
                return lg;
            }
            tail.resize(static_cast<std::size_t>(size - from));
            in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
            tail.resize(static_cast<std::size_t>(in.gcount()));
        }
        if (from > 0) {
            // Start at a line boundary inside the kept window (the note line is the only
            // synthetic line; everything after it stays well-formed).
            const std::size_t nl = tail.find('\n');
            if (nl != std::string::npos) {
                tail.erase(0, nl + 1);
            }
        }
        std::string rewritten = kTruncateNote;
        rewritten += tail;
        {
            std::ofstream out(g_log_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                return lg;
            }
            out.write(rewritten.data(), static_cast<std::streamsize>(rewritten.size()));
            out.flush();
            if (!out) {
                return lg;
            }
        }
        auto fresh = make_file_logger(g_log_path, lg->level());
        g_logger = fresh;
        g_log_bytes = rewritten.size();
        return fresh;
    } catch (...) {
        return lg;
    }
}

std::string hex8(uint32_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08x", v);
    return std::string(buf);
}

std::string first_line(const char *s) {
    if (s == nullptr) {
        return std::string();
    }
    std::string out(s);
    const std::size_t nl = out.find('\n');
    if (nl != std::string::npos) {
        out.erase(nl);
    }
    while (!out.empty() && (out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::string oiio_version() {
    std::string v(OIIO::get_string_attribute("version"));
    if (v.empty()) {
        v = OIIO_VERSION_STRING;
    }
    return v;
}

std::string webp_version() {
    const int packed = WebPGetEncoderVersion();
    if (packed <= 0) {
        return std::string();
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d.%d.%d", (packed >> 16) & 0xff, (packed >> 8) & 0xff,
                  packed & 0xff);
    return std::string(buf);
}

// x265 / svt-av1 / aom have no direct version entry point here; libheif reports them in the
// long encoder descriptor name ("x265 HEVC encoder (4.2)", ...). §3.1 behavior contract.
void append_heif_encoder_versions(std::vector<std::pair<std::string, std::string>> &out) {
    struct Wanted {
        heif_compression_format fmt;
        const char *key;
        const char *needle;
    };
    const Wanted wanted[] = {
        {heif_compression_HEVC, "x265", "x265"},
        {heif_compression_AV1, "svt-av1", "svt"},
        {heif_compression_AV1, "aom", "aom"},
    };
    for (const Wanted &w : wanted) {
        int count = heif_get_encoder_descriptors(w.fmt, nullptr, nullptr, 0);
        if (count <= 0) {
            continue;
        }
        std::vector<const heif_encoder_descriptor *> descs(static_cast<std::size_t>(count),
                                                           nullptr);
        count = heif_get_encoder_descriptors(w.fmt, nullptr, descs.data(), count);
        for (int i = 0; i < count; ++i) {
            const heif_encoder_descriptor *d = descs[static_cast<std::size_t>(i)];
            const char *long_name = heif_encoder_descriptor_get_name(d);
            const char *id_name = heif_encoder_descriptor_get_id_name(d);
            std::string hay = long_name != nullptr ? long_name : "";
            if (id_name != nullptr) {
                hay += ' ';
                hay += id_name;
            }
            std::string lowered;
            lowered.reserve(hay.size());
            for (const char c : hay) {
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (lowered.find(w.needle) != std::string::npos) {
                out.emplace_back(w.key, long_name != nullptr ? std::string(long_name) : hay);
                break;
            }
        }
    }
}

} // namespace

void log_init(const std::filesystem::path &log_dir, LogLevel min_level) {
    try {
        const LogLevel lv = env_level_or(min_level);

        std::shared_ptr<spdlog::logger> lg;
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        const bool dir_ok = std::filesystem::is_directory(log_dir, ec);
        std::filesystem::path file;
        if (dir_ok) {
            file = log_dir / ("run-" + timestamp_now(true) + ".log");
            try {
                lg = make_file_logger(file, to_spdlog(lv));
            } catch (const std::exception &e) {
                lg.reset();
                std::fprintf(stderr, "log_init: cannot open '%s' (%s); logging to stderr\n",
                             file.string().c_str(), e.what());
            } catch (...) {
                lg.reset();
                std::fprintf(stderr, "log_init: cannot open '%s'; logging to stderr\n",
                             file.string().c_str());
            }
        } else {
            std::fprintf(stderr, "log_init: cannot create log dir '%s'; logging to stderr\n",
                         log_dir.string().c_str());
        }

        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_logger = lg;
            g_level = lv;
            g_log_path = lg ? file : std::filesystem::path();
            g_log_bytes = 0; // §2.4 accounting starts from the on-disk size (append mode)
            if (lg) {
                std::error_code sec;
                const std::uintmax_t existing = std::filesystem::file_size(file, sec);
                if (!sec) {
                    g_log_bytes = existing;
                }
            }
        }

        if (lg) {
            prune_old_runs(log_dir);
        }
    } catch (...) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_logger.reset();
        g_log_path.clear();
        g_log_bytes = 0;
    }
}

void log_shutdown() {
    std::shared_ptr<spdlog::logger> lg;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        lg = std::move(g_logger);
        g_logger.reset();
        g_log_path.clear();
        g_log_bytes = 0;
    }
    if (lg) {
        try {
            lg->flush();
        } catch (...) {
            // shutdown must never throw (idempotent, safe to call repeatedly)
        }
    }
}

void log_set_level(LogLevel lv) {
    std::shared_ptr<spdlog::logger> lg;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_level = lv;
        lg = g_logger;
    }
    if (lg) {
        try {
            lg->set_level(to_spdlog(lv));
        } catch (...) {
        }
    }
}

LogLevel log_level() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_level;
}

// M2-T3 §2.3: the frozen startup entry point. Read once per call; main.cpp calls it exactly once
// at start-up, so a later change of the variable does not follow the running process.
LogLevel level_from_env_or(LogLevel fallback) {
    const char *env = std::getenv("PP_LOG_LEVEL");
    if (env == nullptr) {
        return fallback;
    }
    LogLevel lv = fallback;
    if (parse_level(env, lv)) {
        return lv;
    }
    std::fprintf(stderr, "PP_LOG_LEVEL 无效：\"%s\"，已忽略\n", env); // §2.3 frozen text
    std::fflush(stderr);
    return fallback;
}

void log_write(LogLevel lv, std::string_view stage, std::string_view file, std::string_view msg,
               LogFields fields) noexcept {
    try {
        std::shared_ptr<spdlog::logger> lg;
        LogLevel current = LogLevel::Info;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            lg = g_logger;
            current = g_level;
        }
        if (static_cast<int>(lv) < static_cast<int>(current)) {
            return;
        }
        const std::string body = render_body(stage, file, msg, fields);
        if (!lg) {
            write_stderr(lv, body);
            return;
        }
        // §2.4: the cap check and the write share the lock. Truncation rewrites the file and
        // swaps in a fresh sink, which must not interleave with another thread's write; the
        // sink itself serialises writes anyway, so this adds no new contention.
        std::lock_guard<std::mutex> lk(g_mu);
        const std::size_t incoming = body.size() + kLineOverhead;
        lg = enforce_size_cap_locked(std::move(lg), incoming);
        lg->log(to_spdlog(lv), "{}", body);
        if (static_cast<int>(lv) >= static_cast<int>(LogLevel::Warn)) {
            lg->flush();
        }
        g_log_bytes += incoming;
    } catch (...) {
        // logging never propagates exceptions
    }
}

void set_qt_version_string(std::string version) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_qt_version = std::move(version);
}

std::vector<std::pair<std::string, std::string>> library_versions() {
    std::vector<std::pair<std::string, std::string>> out;
    auto add = [&out](std::string key, std::string value) {
        out.emplace_back(std::move(key), std::move(value));
    };
    try {
        add("app", kAppVersion);

        std::string qt;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            qt = g_qt_version;
        }
        if (!qt.empty()) {
            add("qt", qt);
        }

        add("oiio", oiio_version());
        add("jpegli", kJpegliCommit);
        add("libjxl", hex8(JxlEncoderVersion()));
        {
            const char *heif = heif_get_version();
            add("libheif", heif != nullptr ? std::string(heif) : std::string());
        }
        add("exiv2", Exiv2::versionString());
        add("lcms2", std::to_string(cmsGetEncodedCMMversion()));
        add("webp", webp_version());
        add("tiff", first_line(TIFFGetVersion()));

        append_heif_encoder_versions(out);
    } catch (...) {
        // report the entries gathered so far
    }
    return out;
}

} // namespace pp
