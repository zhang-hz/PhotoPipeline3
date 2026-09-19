// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — platform paths (contract: docs/m1-tasks.md §3.13 PP-FROZEN header,
// 落地约束 docs/m1b-tasks.md §2.2).
//
// Resolution (once per process, cached):
//   1. executable_dir() = dirname(/proc/self/exe) on Linux;
//   2. portability probe: create `<exe>/.pp-write-test`, then delete it. Writable ->
//      portable mode, data_dir() == executable_dir();
//   3. otherwise fall back to $XDG_DATA_HOME/PhotoPipeline (default
//      ~/.local/share/PhotoPipeline);
//   4. data_dir(), `<data>/presets`, `<data>/logs` are created on first use; if creation
//      fails in both branches, data_dir() returns an empty path for the caller to handle.
//
// TODO(M2): Windows branch (%APPDATA%/PhotoPipeline, §3.13 header comment) — the M1b
//           target platform is Linux only.

#include "platform/paths.h"

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

namespace pp::platform {
namespace {

constexpr const char* kWriteProbeName = ".pp-write-test";
constexpr const char* kAppDirName = "PhotoPipeline";

struct Cache {
    std::once_flag once;
    std::filesystem::path exe_dir;
    bool exe_writable = false;
    std::filesystem::path data;  // empty = no usable directory; caller must cope
};

Cache g_cache;

// /proc/self/exe is the Linux truth for the running executable (§2.2). The buffer grows
// until readlink stops truncating; a failure yields an empty path (-> XDG fallback).
std::filesystem::path read_exe_path() {
    std::string buf(1024, '\0');
    for (;;) {
        const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
        if (n < 0) return {};
        if (static_cast<std::size_t>(n) < buf.size()) {
            buf.resize(static_cast<std::size_t>(n));
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
}

// 可写探测：在 dir 下创建 .pp-write-test 写入后删除；任何一步失败即不可写。
bool dir_writable(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;

    const std::filesystem::path probe = dir / kWriteProbeName;
    bool ok = false;
    {
        std::ofstream f(probe, std::ios::binary | std::ios::trunc);
        if (f) {
            f << "pp write probe\n";
            f.flush();
            ok = static_cast<bool>(f);
        }
    }
    std::error_code rm_ec;
    std::filesystem::remove(probe, rm_ec);  // 探测文件必须清掉（失败也不留痕于逻辑）
    return ok;
}

std::filesystem::path xdg_data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') return std::filesystem::path(xdg) / kAppDirName;
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "share" / kAppDirName;
    }
    return {};
}

bool ensure_dir(const std::filesystem::path& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;
    return std::filesystem::is_directory(dir, ec);
}

bool ensure_layout(const std::filesystem::path& base) {
    return !base.empty() && ensure_dir(base) && ensure_dir(base / "presets") &&
           ensure_dir(base / "logs");
}

void resolve() {
    g_cache.exe_dir = read_exe_path();
    if (!g_cache.exe_dir.empty()) g_cache.exe_dir = g_cache.exe_dir.parent_path();
    g_cache.exe_writable = dir_writable(g_cache.exe_dir);

    g_cache.data = g_cache.exe_writable ? g_cache.exe_dir : xdg_data_dir();
    if (!ensure_layout(g_cache.data)) {
        // 便携分支的探测通过但目录创建失败（或 XDG 不可写）→ 试另一分支，再失败返回空。
        if (g_cache.exe_writable && g_cache.data != xdg_data_dir()) {
            g_cache.data = xdg_data_dir();
            if (!ensure_layout(g_cache.data)) g_cache.data.clear();
        } else {
            g_cache.data.clear();
        }
    }
}

const std::filesystem::path& data_dir_cached() {
    std::call_once(g_cache.once, resolve);
    return g_cache.data;
}

}  // namespace

std::filesystem::path executable_dir() {
    std::call_once(g_cache.once, resolve);
    return g_cache.exe_dir;
}

std::filesystem::path data_dir() { return data_dir_cached(); }

std::filesystem::path settings_file() { return data_dir_cached() / "settings.ini"; }

std::filesystem::path presets_dir() { return data_dir_cached() / "presets"; }

std::filesystem::path logs_dir() { return data_dir_cached() / "logs"; }

}  // namespace pp::platform
