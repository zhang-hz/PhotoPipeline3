// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — global pixel budget (token pool) implementation.
//
// Contract: docs/m1-tasks.md §3.3 (PP-FROZEN) / §4.1, discipline §1.14
// (acquire checks the cancel flag so a cancelled run can never deadlock).

#include "core/pixelbudget.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace pp {
namespace {

// Physical memory currently available for new work. Returns 0 when unknown.
uint64_t available_memory_bytes() {
    // M3（裁定 #15 销账）：Windows = GlobalMemoryStatusEx.ullAvailPhys；
    // macOS 探针仍留空（回退 8 GB 上限），Linux 路径不变。
#ifdef _WIN32
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (::GlobalMemoryStatusEx(&st)) {
        return st.ullAvailPhys;
    }
    return 0;
#elif defined(__linux__)
    // MemAvailable is the kernel's own estimate (free + reclaimable page cache).
    std::ifstream f("/proc/meminfo");
    std::string key;
    uint64_t value = 0;
    std::string unit;
    while (f >> key >> value) {
        if (key == "MemAvailable:") {
            return value * 1024ull;
        }
        std::getline(f, unit);
    }
#endif
#if defined(__unix__) || defined(__APPLE__)
    const long pages = ::sysconf(_SC_AVPHYS_PAGES);
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        return static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
    }
#endif
    return 0;
}

} // namespace

PixelBudget::PixelBudget(uint64_t capacity_bytes) : capacity_(capacity_bytes) {}

bool PixelBudget::acquire(uint64_t bytes, const std::function<bool()> &cancelled) {
    // Larger than the whole pool: fail at once, the caller reports the error (never wait).
    if (bytes > capacity_) {
        return false;
    }
    std::unique_lock<std::mutex> lk(mu_);
    for (;;) {
        if (cancelled && cancelled()) {
            return false; // no quota taken
        }
        if (bytes <= capacity_ - used_) {
            used_ += bytes;
            peak_ = std::max(peak_, used_);
            return true;
        }
        // Wake at least once per second to re-check the cancel flag.
        cv_.wait_for(lk, std::chrono::seconds(1));
    }
}

void PixelBudget::release(uint64_t bytes) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        assert(bytes <= used_ && "PixelBudget::release() underflow");
        used_ = bytes > used_ ? 0 : used_ - bytes;
    }
    cv_.notify_all();
}

uint64_t PixelBudget::capacity() const {
    std::lock_guard<std::mutex> lk(mu_);
    return capacity_;
}

uint64_t PixelBudget::used() const {
    std::lock_guard<std::mutex> lk(mu_);
    return used_;
}

uint64_t PixelBudget::peak() const {
    std::lock_guard<std::mutex> lk(mu_);
    return peak_;
}

uint64_t PixelBudget::frame_bytes(int w, int h, int channels) {
    assert(w >= 0 && h >= 0 && "frame_bytes(): negative dimension");
    assert(channels >= 1 && channels <= 4 && "frame_bytes(): channels must be 1..4");
    if (w <= 0 || h <= 0) {
        return 0;
    }
    const uint64_t ch = channels <= 0 ? 1u : static_cast<uint64_t>(channels);
    return static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * ch * 4ull;
}

uint64_t PixelBudget::default_capacity_bytes() {
    constexpr uint64_t kCeiling = 8ull * 1024 * 1024 * 1024; // 8 GB
    const uint64_t available = available_memory_bytes();
    if (available == 0) {
        return kCeiling; // unknown amount of RAM -> use the ceiling
    }
    const uint64_t half = available / 2;
    return half == 0 ? kCeiling : std::min(half, kCeiling);
}

// ---------------------------------------------------------------------------
// M10 / W1-T1：评分独立内存配额（非阻塞；声明与口径见 pixelbudget.h 末尾块）
// ---------------------------------------------------------------------------
ScoreQuota::ScoreQuota(uint64_t capacity_bytes) : capacity_(capacity_bytes) {}

bool ScoreQuota::try_acquire(uint64_t bytes) {
    // 非阻塞：容量不足（含单任务 > 整池）→ 立即 false，不等待、不扣额。
    std::lock_guard<std::mutex> lk(mu_);
    if (bytes > capacity_ - used_) {
        return false;
    }
    used_ += bytes;
    peak_ = std::max(peak_, used_);
    return true;
}

bool ScoreQuota::acquire(uint64_t bytes, std::chrono::milliseconds timeout) {
    // M10 / W1-B1：评分池 worker 的阻塞记账。单任务需求超过整池容量 → 等也等不到（设计
    // §4.2.2(2)：该输出评分 Unavailable、文件仍成功），立即 false 让调用方快速降级。
    if (bytes > capacity_) {
        return false;
    }
    std::unique_lock<std::mutex> lk(mu_);
    const bool ok = cv_.wait_for(lk, timeout, [this, bytes] { return bytes <= capacity_ - used_; });
    if (!ok) {
        return false; // 超时：不扣额
    }
    used_ += bytes;
    peak_ = std::max(peak_, used_);
    return true;
}

void ScoreQuota::release(uint64_t bytes) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        used_ = bytes > used_ ? 0 : used_ - bytes;
    }
    cv_.notify_all(); // M10 / W1-B1：唤醒阻塞 acquire 的评分 worker
}

uint64_t ScoreQuota::capacity() const {
    std::lock_guard<std::mutex> lk(mu_);
    return capacity_;
}

uint64_t ScoreQuota::used() const {
    std::lock_guard<std::mutex> lk(mu_);
    return used_;
}

uint64_t ScoreQuota::peak() const {
    std::lock_guard<std::mutex> lk(mu_);
    return peak_;
}

uint64_t ScoreQuota::default_capacity_bytes(uint64_t file_capacity_bytes) {
    constexpr uint64_t kCeiling = 4ull * 1024 * 1024 * 1024; // 评分峰值上限 4 GiB
    if (file_capacity_bytes == 0) {
        return kCeiling; // 未知文件容量 → 按评分 ceiling
    }
    const uint64_t half = file_capacity_bytes / 2;
    return half == 0 ? kCeiling : std::min(half, kCeiling);
}

ScoreQuota &default_score_quota() {
    // 进程级单例：容量由 PixelBudget 的默认容量推导（可用 RAM 的 1/4，上限 4 GiB）。
    static ScoreQuota quota(
        ScoreQuota::default_capacity_bytes(PixelBudget::default_capacity_bytes()));
    return quota;
}

} // namespace pp
