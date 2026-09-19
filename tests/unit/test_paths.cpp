// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — platform paths unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.13 (PP-FROZEN header) / docs/m1b-tasks.md §2.2 / §4.1:
// executable_dir() exists, settings_file() sits in data_dir(), data_dir() exists and is
// writable, presets/logs live under it, and the writability probe leaves no file behind.
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "platform/paths.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string& case_name, const std::string& detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string show(const fs::path& p) { return p.empty() ? std::string("<empty>") : p.string(); }

// True when `child` is directly below `parent` (both absolute, lexically normal).
bool directly_under(const fs::path& child, const fs::path& parent) {
    return !child.empty() && !parent.empty() && child.parent_path() == parent;
}

}  // namespace

int main() {
    // ---- executable_dir(): from /proc/self/exe, exists, cached ----
    const fs::path exe_dir = pp::platform::executable_dir();
    check(!exe_dir.empty(), "exe/non-empty", "executable_dir() returned an empty path");
    check(exe_dir.is_absolute(), "exe/absolute", "got " + show(exe_dir));
    {
        std::error_code ec;
        check(fs::is_directory(exe_dir, ec), "exe/is-directory",
              show(exe_dir) + " ec=" + ec.message());
    }
    check(pp::platform::executable_dir() == exe_dir, "exe/cached",
          "second call differs: " + show(pp::platform::executable_dir()));
    check(!fs::exists(exe_dir / ".pp-write-test"), "exe/probe-file-cleaned",
          "writability probe file was left in " + show(exe_dir));

    // ---- data_dir(): non-empty, exists, writable, cached ----
    const fs::path data = pp::platform::data_dir();
    check(!data.empty(), "data/non-empty", "data_dir() returned an empty path");
    {
        std::error_code ec;
        check(fs::is_directory(data, ec), "data/is-directory",
              show(data) + " ec=" + ec.message());
    }
    check(pp::platform::data_dir() == data, "data/cached",
          "second call differs: " + show(pp::platform::data_dir()));
    {
        std::error_code ec;
        const fs::path probe = data / ".pp_test_paths_write_probe";
        bool wrote = false;
        {
            std::ofstream f(probe, std::ios::binary | std::ios::trunc);
            if (f) {
                f << "probe\n";
                f.flush();
                wrote = static_cast<bool>(f);
            }
        }
        check(wrote, "data/writable", "cannot create " + show(probe));
        fs::remove(probe, ec);
    }

    // ---- settings_file() = data_dir()/settings.ini ----
    const fs::path settings = pp::platform::settings_file();
    check(directly_under(settings, data), "settings/in-data-dir",
          show(settings) + " not directly under " + show(data));
    check(settings.filename() == "settings.ini", "settings/file-name", show(settings));

    // ---- presets_dir()/logs_dir(): under data_dir(), created ----
    const fs::path presets = pp::platform::presets_dir();
    const fs::path logs = pp::platform::logs_dir();
    check(directly_under(presets, data), "presets/in-data-dir",
          show(presets) + " not directly under " + show(data));
    check(directly_under(logs, data), "logs/in-data-dir",
          show(logs) + " not directly under " + show(data));
    {
        std::error_code ec;
        check(fs::is_directory(presets, ec), "presets/exists",
              show(presets) + " ec=" + ec.message());
        check(fs::is_directory(logs, ec), "logs/exists", show(logs) + " ec=" + ec.message());
    }
    check(pp::platform::presets_dir() == presets && pp::platform::logs_dir() == logs,
          "dirs/cached", "second call differs");

    // Portable mode: when the executable directory is writable, data_dir() is the
    // executable directory (a read-only install location legitimately falls back to XDG).
    if (data == exe_dir) {
        std::printf("test_paths: portable mode (data_dir == executable_dir = %s)\n",
                    show(data).c_str());
    } else {
        std::printf("test_paths: fallback mode (executable_dir = %s, data_dir = %s)\n",
                    show(exe_dir).c_str(), show(data).c_str());
    }

    if (g_failed == 0) {
        std::printf("test_paths: OK\n");
        return 0;
    }
    std::printf("test_paths: FAILED (%d)\n", g_failed);
    return g_failed;
}
