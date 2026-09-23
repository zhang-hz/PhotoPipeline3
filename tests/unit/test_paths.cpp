// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — platform paths unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.13 (PP-FROZEN header) / docs/m1b-tasks.md §2.2 / §4.1:
// executable_dir() exists, settings_file() sits in data_dir(), data_dir() exists and is
// writable, presets/logs live under it, and the writability probe leaves no file behind.
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "platform/paths.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string show(const fs::path &p) { return p.empty() ? std::string("<empty>") : p.string(); }

// True when `child` is directly below `parent` (both absolute, lexically normal).
bool directly_under(const fs::path &child, const fs::path &parent) {
    return !child.empty() && !parent.empty() && child.parent_path() == parent;
}

} // namespace

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
        check(fs::is_directory(data, ec), "data/is-directory", show(data) + " ec=" + ec.message());
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
    check(pp::platform::presets_dir() == presets && pp::platform::logs_dir() == logs, "dirs/cached",
          "second call differs");

    // ---- M2-T14 (#23): 平台路径 = 原生字节串（可能含非 UTF-8 字节）----
    // 组合、创建、枚举都不得经过 QString：Qt6 在 Unix 上把 QString 文件名固定按 UTF-8 编码，
    // 非法字节会变成 U+FFFD（写回时是 EF BF BD），因此字节路径必须走 std::filesystem 原生字节。
#ifndef _WIN32
    {
        const std::string raw_name = "caf\xE9"; // 0xE9 不是合法 UTF-8 序列
        const fs::path raw = data / raw_name;
        std::error_code ec;
        fs::create_directories(raw, ec);
        check(!ec && fs::is_directory(raw, ec), "byte-path/creatable",
              show(raw) + " ec=" + ec.message());
        check((data / "caf\xE9").string() == raw.string(), "byte-path/compose",
              "composition changed bytes: " + raw.string());
        bool found = false;
        for (const fs::directory_entry &e : fs::directory_iterator(data, ec)) {
            if (e.path().filename().string() == raw_name)
                found = true;
        }
        check(found, "byte-path/enumerate", "raw-byte entry not listed under " + show(data));
        fs::remove_all(raw, ec);
        check(logs.string() == (data / "logs").string() &&
                  settings.string() == (data / "settings.ini").string(),
              "byte-path/dir-composition", "logs=" + show(logs) + " settings=" + show(settings));
    }
#else
    // Windows（M3-D2 activeCodePage=UTF-8）：窄串即 UTF-8。验证中文+emoji 目录名
    // 的 UTF-8 往返（对应 Linux 侧的原生字节断言，语义等价：路径字节不经 QString）。
    {
        const std::string raw_name = "路径📸测试";
        const fs::path raw = data / raw_name;
        std::error_code ec;
        fs::create_directories(raw, ec);
        check(!ec && fs::is_directory(raw, ec), "byte-path/creatable",
              show(raw) + " ec=" + ec.message());
        check((data / raw_name).string() == raw.string(), "byte-path/compose",
              "composition changed bytes: " + raw.string());
        bool found = false;
        for (const fs::directory_entry &e : fs::directory_iterator(data, ec)) {
            if (e.path().filename().string() == raw_name)
                found = true;
        }
        check(found, "byte-path/enumerate", "raw-byte entry not listed under " + show(data));
        fs::remove_all(raw, ec);
        check(logs.string() == (data / "logs").string() &&
                  settings.string() == (data / "settings.ini").string(),
              "byte-path/dir-composition", "logs=" + show(logs) + " settings=" + show(settings));
    }
#endif

    // ---- data_dir() 的字节来源：可写 exe 目录，或平台回退目录的逐字节拼接 ----
    {
#ifdef _WIN32
        const fs::path fallback = [] {
            const char *appdata = std::getenv("APPDATA");
            if (appdata != nullptr && *appdata != '\0')
                return fs::path(appdata) / "PhotoPipeline";
            return fs::path{};
        }();
#else
        const fs::path fallback = [] {
            const char *xdg = std::getenv("XDG_DATA_HOME");
            if (xdg != nullptr && *xdg != '\0')
                return fs::path(xdg) / "PhotoPipeline";
            const char *home = std::getenv("HOME");
            if (home != nullptr && *home != '\0')
                return fs::path(home) / ".local" / "share" / "PhotoPipeline";
            return fs::path{};
        }();
#endif
        if (data != exe_dir && !fallback.empty())
            check(data.string() == fallback.string(), "data/byte-source",
                  show(data) + " != " + show(fallback));
    }

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
