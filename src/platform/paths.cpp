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
// M3（裁定 #21 销账）：Windows 分支落地 — exe 探测 GetModuleFileNameW，回退
// %APPDATA%/PhotoPipeline（design §8.5）；其余（写探测/布局/缓存）平台无关。

#include "platform/paths.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

namespace pp::platform {
namespace {

constexpr const char *kWriteProbeName = ".pp-write-test";
constexpr const char *kAppDirName = "PhotoPipeline";

struct Cache {
    std::once_flag once;
    std::filesystem::path exe_dir;
    bool exe_writable = false;
    std::filesystem::path data; // empty = no usable directory; caller must cope
};

Cache g_cache;

// The running executable's own path. Linux: /proc/self/exe (buffer grows until
// readlink stops truncating). Windows: GetModuleFileNameW (buffer grows while
// truncated). A failure yields an empty path (-> fallback data dir).
std::filesystem::path read_exe_path() {
#ifdef _WIN32
    std::wstring buf(1024, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return {};
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
#else
    std::string buf(1024, '\0');
    for (;;) {
        const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
        if (n < 0)
            return {};
        if (static_cast<std::size_t>(n) < buf.size()) {
            buf.resize(static_cast<std::size_t>(n));
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
#endif
}

// 可写探测：在 dir 下创建 .pp-write-test 写入后删除；任何一步失败即不可写。
bool dir_writable(const std::filesystem::path &dir) {
    if (dir.empty())
        return false;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return false;

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
    std::filesystem::remove(probe, rm_ec); // 探测文件必须清掉（失败也不留痕于逻辑）
    return ok;
}

// Fallback data dir when the executable directory is not writable.
// POSIX: $XDG_DATA_HOME (default ~/.local/share). Windows: %APPDATA% (design §8.5;
// narrow env strings are UTF-8 under the M3-D2 activeCodePage=UTF-8 manifest).
std::filesystem::path platform_data_dir() {
#ifdef _WIN32
    const char *appdata = std::getenv("APPDATA");
    if (appdata != nullptr && *appdata != '\0') {
        return std::filesystem::path(appdata) / kAppDirName;
    }
    return {};
#else
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0')
        return std::filesystem::path(xdg) / kAppDirName;
    const char *home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "share" / kAppDirName;
    }
    return {};
#endif
}

bool ensure_dir(const std::filesystem::path &dir) {
    if (dir.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return false;
    return std::filesystem::is_directory(dir, ec);
}

bool ensure_layout(const std::filesystem::path &base) {
    return !base.empty() && ensure_dir(base) && ensure_dir(base / "presets") &&
           ensure_dir(base / "logs");
}

void resolve() {
    g_cache.exe_dir = read_exe_path();
    if (!g_cache.exe_dir.empty())
        g_cache.exe_dir = g_cache.exe_dir.parent_path();
    g_cache.exe_writable = dir_writable(g_cache.exe_dir);

    g_cache.data = g_cache.exe_writable ? g_cache.exe_dir : platform_data_dir();
    if (!ensure_layout(g_cache.data)) {
        // 便携分支的探测通过但目录创建失败（或 XDG 不可写）→ 试另一分支，再失败返回空。
        if (g_cache.exe_writable && g_cache.data != platform_data_dir()) {
            g_cache.data = platform_data_dir();
            if (!ensure_layout(g_cache.data))
                g_cache.data.clear();
        } else {
            g_cache.data.clear();
        }
    }
}

const std::filesystem::path &data_dir_cached() {
    std::call_once(g_cache.once, resolve);
    return g_cache.data;
}

} // namespace

std::filesystem::path executable_dir() {
    std::call_once(g_cache.once, resolve);
    return g_cache.exe_dir;
}

std::filesystem::path data_dir() { return data_dir_cached(); }

std::filesystem::path settings_file() { return data_dir_cached() / "settings.ini"; }

std::filesystem::path presets_dir() { return data_dir_cached() / "presets"; }

std::filesystem::path logs_dir() { return data_dir_cached() / "logs"; }

} // namespace pp::platform
