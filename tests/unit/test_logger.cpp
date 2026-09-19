// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — logger unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.1 (PP-FROZEN) / §4.1.
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/logger.h"

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string& case_name, const std::string& detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

fs::path make_temp_dir(const std::string& name) {
    std::error_code ec;
    const fs::path d = fs::current_path(ec) / ".pp_test_tmp" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

void write_file(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    f << text;
}

std::vector<fs::path> run_files(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        std::error_code sec;
        if (!e.is_regular_file(sec)) {
            continue;
        }
        const std::string name = e.path().filename().string();
        if (name.rfind("run-", 0) == 0 && e.path().extension() == ".log") {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Run one callable with stderr redirected to a file; returns the captured text.
template <typename Fn>
std::string capture_stderr(const fs::path& tmp, const std::string& tag, Fn&& fn) {
#if defined(__unix__) || defined(__APPLE__)
    const fs::path file = tmp / ("stderr-" + tag + ".txt");
    std::fflush(stderr);
    const int saved = ::dup(STDERR_FILENO);
    if (saved < 0) {
        fn();
        return std::string();
    }
    if (std::freopen(file.string().c_str(), "w", stderr) == nullptr) {
        ::close(saved);
        fn();
        return std::string();
    }
    fn();
    std::fflush(stderr);
    ::dup2(saved, STDERR_FILENO);
    ::close(saved);
    return read_file(file);
#else
    (void)tmp;
    (void)tag;
    fn();
    return std::string();
#endif
}

std::string versions_dump(const std::vector<std::pair<std::string, std::string>>& v) {
    std::string out;
    for (const auto& kv : v) {
        out += " " + kv.first + "=" + kv.second;
    }
    return out;
}

}  // namespace

int main() {
    const fs::path tmp = make_temp_dir("test_logger");
    ::unsetenv("PP_LOG_LEVEL");

    // ---- A. uninitialized: log_write goes to stderr and never crashes ----
    {
        check(pp::log_level() == pp::LogLevel::Info, "uninit/default-level",
              "default level must be Info");
        const std::string captured = capture_stderr(tmp, "uninit", [] {
            pp::log_info("boot", "logger.cpp", "uninit-probe-line");
            pp::log_warn("boot", "logger.cpp", "uninit-warn-line");
        });
        check(contains(captured, "uninit-probe-line"), "uninit/stderr-fallback",
              "captured stderr: '" + captured + "'");
        check(contains(captured, "uninit-warn-line"), "uninit/stderr-warn",
              "captured stderr: '" + captured + "'");
    }

    // ---- B. level filtering ----
    {
        const fs::path dir = tmp / "level";
        pp::log_init(dir, pp::LogLevel::Info);
        check(pp::log_level() == pp::LogLevel::Info, "level/reported", "expected Info");
        pp::log_debug("stage", "file.cpp", "filtered-debug-line");
        pp::log_info("stage", "file.cpp", "kept-info-line");
        pp::log_shutdown();
        const std::vector<fs::path> files = run_files(dir);
        check(files.size() == 1, "level/one-run-file", "got " + std::to_string(files.size()));
        const std::string text = files.empty() ? std::string() : read_file(files.front());
        check(contains(text, "kept-info-line"), "level/info-written", "log text: '" + text + "'");
        check(!contains(text, "filtered-debug-line"), "level/debug-filtered",
              "log text: '" + text + "'");
    }

    // ---- C. PP_LOG_LEVEL overrides the programmatic minimum (case-insensitive) ----
    {
        const fs::path dir = tmp / "env-debug";
        ::setenv("PP_LOG_LEVEL", "debug", 1);
        pp::log_init(dir, pp::LogLevel::Info);
        check(pp::log_level() == pp::LogLevel::Debug, "env/debug-override",
              "expected Debug, got " + std::to_string(static_cast<int>(pp::log_level())));
        pp::log_debug("stage", "file.cpp", "env-debug-line");
        pp::log_shutdown();
        const std::vector<fs::path> files = run_files(dir);
        const std::string text = files.empty() ? std::string() : read_file(files.front());
        check(contains(text, "env-debug-line"), "env/debug-written", "log text: '" + text + "'");
    }
    {
        const fs::path dir = tmp / "env-mixed";
        ::setenv("PP_LOG_LEVEL", "WaRn", 1);  // mixed case must be accepted
        pp::log_init(dir, pp::LogLevel::Trace);
        check(pp::log_level() == pp::LogLevel::Warn, "env/case-insensitive",
              "expected Warn, got " + std::to_string(static_cast<int>(pp::log_level())));
        pp::log_info("stage", "file.cpp", "env-below-warn");
        pp::log_warn("stage", "file.cpp", "env-warn-kept");
        pp::log_shutdown();
        const std::vector<fs::path> files = run_files(dir);
        const std::string text = files.empty() ? std::string() : read_file(files.front());
        check(!contains(text, "env-below-warn"), "env/warn-filters-info",
              "log text: '" + text + "'");
        check(contains(text, "env-warn-kept"), "env/warn-written", "log text: '" + text + "'");
    }
    {
        const fs::path dir = tmp / "env-bogus";
        ::setenv("PP_LOG_LEVEL", "not-a-level", 1);
        pp::log_init(dir, pp::LogLevel::Error);
        check(pp::log_level() == pp::LogLevel::Error, "env/unknown-ignored",
              "expected the programmatic Error level to win");
        pp::log_shutdown();
        ::unsetenv("PP_LOG_LEVEL");
    }

    // ---- D. line format: HH:MM:SS.mmm [lvl] [tid] [stage] [file] message {k=v k=v} ----
    {
        const fs::path dir = tmp / "format";
        pp::log_init(dir, pp::LogLevel::Info);
        pp::log_info("render", "pipeline.cpp", "hello world",
                     {{"k", "v"}, {"path", "a b.png"}, {"n", "3"}});
        pp::log_info("render", "pipeline.cpp", "no fields here");
        pp::log_shutdown();
        const std::vector<fs::path> files = run_files(dir);
        const std::string text = files.empty() ? std::string() : read_file(files.front());

        const std::regex with_fields(
            R"(^[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} \[info\] \[[0-9]+\] \[render\] \[pipeline\.cpp\] hello world \{k=v path="a b\.png" n=3\}$)");
        const std::regex no_fields(
            R"(^[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} \[info\] \[[0-9]+\] \[render\] \[pipeline\.cpp\] no fields here$)");
        bool matched_fields = false;
        bool matched_plain = false;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (std::regex_match(line, with_fields)) {
                matched_fields = true;
            }
            if (std::regex_match(line, no_fields)) {
                matched_plain = true;
            }
        }
        check(matched_fields, "format/kv-line", "log text: '" + text + "'");
        check(matched_plain, "format/plain-line", "log text: '" + text + "'");
    }

    // ---- E. warn and above flush immediately (no shutdown needed) ----
    {
        const fs::path dir = tmp / "flush";
        pp::log_init(dir, pp::LogLevel::Info);
        pp::log_warn("stage", "file.cpp", "warn-flush-probe");
        const std::vector<fs::path> files = run_files(dir);
        const std::string text = files.empty() ? std::string() : read_file(files.front());
        check(contains(text, "warn-flush-probe"), "flush/warn-immediate",
              "log text: '" + text + "'");
        pp::log_shutdown();
    }

    // ---- F. retention: at most 20 run-*.log files, oldest removed ----
    {
        const fs::path dir = tmp / "retention";
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::vector<std::string> synthetic;
        const auto base = fs::file_time_type::clock::now() - std::chrono::hours(24 * 30);
        for (int i = 0; i < 25; ++i) {
            char name[64];
            std::snprintf(name, sizeof(name), "run-202001%02d-%06d.log", (i / 10) + 1, i);
            const fs::path p = dir / name;
            write_file(p, "old\n");
            std::error_code tec;
            fs::last_write_time(p, base + std::chrono::minutes(i), tec);
            synthetic.push_back(name);
        }
        pp::log_init(dir, pp::LogLevel::Info);
        pp::log_info("stage", "file.cpp", "retention-current-line");
        pp::log_shutdown();
        const std::vector<fs::path> files = run_files(dir);
        check(files.size() == 20, "retention/keeps-20",
              "expect 20 run files, got " + std::to_string(files.size()));
        int kept_synthetic = 0;
        bool current_present = false;
        for (const fs::path& f : files) {
            const std::string name = f.filename().string();
            if (std::find(synthetic.begin(), synthetic.end(), name) != synthetic.end()) {
                ++kept_synthetic;
            } else {
                current_present = true;
            }
        }
        check(kept_synthetic == 19, "retention/oldest-dropped",
              "expect 19 old files + current, kept_synthetic=" +
                  std::to_string(kept_synthetic));
        check(current_present, "retention/current-kept", "current run file was pruned");
        for (int i = 0; i < 6; ++i) {
            check(!fs::exists(dir / synthetic[static_cast<std::size_t>(i)]),
                  "retention/removed-" + synthetic[static_cast<std::size_t>(i)],
                  "oldest file survived pruning");
        }
    }

    // ---- G. log_shutdown is idempotent; post-shutdown writes go to stderr ----
    {
        const fs::path dir = tmp / "shutdown";
        pp::log_init(dir, pp::LogLevel::Info);
        pp::log_info("stage", "file.cpp", "before-shutdown");
        pp::log_shutdown();
        pp::log_shutdown();  // second call must be a no-op
        pp::log_shutdown();  // and a third
        const std::string captured = capture_stderr(tmp, "after-shutdown", [] {
            pp::log_info("stage", "file.cpp", "after-shutdown-probe");
        });
        check(contains(captured, "after-shutdown-probe"), "shutdown/post-write-stderr",
              "captured stderr: '" + captured + "'");
        const std::vector<fs::path> files = run_files(dir);
        const std::string text = files.empty() ? std::string() : read_file(files.front());
        check(contains(text, "before-shutdown"), "shutdown/flushed", "log text: '" + text + "'");
        check(!contains(text, "after-shutdown-probe"), "shutdown/no-write-after",
              "log text: '" + text + "'");
    }

    // ---- H. library_versions() ----
    {
        const std::vector<std::pair<std::string, std::string>> before = pp::library_versions();
        bool qt_before = false;
        for (const auto& kv : before) {
            qt_before = qt_before || kv.first == "qt";
        }
        check(!qt_before, "versions/qt-absent-before-injection",
              "qt must only appear after set_qt_version_string()");

        pp::set_qt_version_string("6.8.3");
        const std::vector<std::pair<std::string, std::string>> v = pp::library_versions();
        std::printf("library_versions:%s\n", versions_dump(v).c_str());
        const std::vector<std::string> required = {"app",  "qt",    "oiio",  "jpegli", "libjxl",
                                                   "libheif", "exiv2", "lcms2", "webp",  "tiff"};
        for (const std::string& key : required) {
            bool found = false;
            for (const auto& kv : v) {
                if (kv.first == key) {
                    found = !kv.second.empty();
                }
            }
            check(found, "versions/" + key, "missing or empty entry '" + key + "'");
        }
    }

    // ---- I. re-init after shutdown ----
    {
        const fs::path dir = tmp / "reinit";
        pp::log_init(dir, pp::LogLevel::Info);
        pp::log_info("stage", "file.cpp", "reinit-line");
        pp::log_shutdown();
        const std::vector<fs::path> files = run_files(dir);
        const std::string text = files.empty() ? std::string() : read_file(files.front());
        check(files.size() == 1 && contains(text, "reinit-line"), "reinit/after-shutdown",
              "files=" + std::to_string(files.size()) + " text='" + text + "'");
    }

    if (g_failed == 0) {
        std::printf("test_logger: OK\n");
        return 0;
    }
    std::printf("test_logger: FAILED (%d)\n", g_failed);
    return g_failed;
}
