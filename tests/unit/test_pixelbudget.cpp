// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T1 — PixelBudget unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.3 (PP-FROZEN) / §4.1 and discipline §1.14.
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/pixelbudget.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string& case_name, const std::string& detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string num(unsigned long long v) { return std::to_string(v); }

constexpr unsigned long long kGiB = 1024ull * 1024 * 1024;

}  // namespace

int main() {
    // ---- capacity / frame_bytes / default_capacity ----
    {
        pp::PixelBudget b(1000);
        check(b.capacity() == 1000, "capacity/value", "got " + num(b.capacity()));
        check(b.used() == 0, "capacity/initial-used", "got " + num(b.used()));
        check(b.peak() == 0, "capacity/initial-peak", "got " + num(b.peak()));
    }
    {
        check(pp::PixelBudget::frame_bytes(8000, 6000, 3) == 8000ull * 6000ull * 3ull * 4ull,
              "frame-bytes/rgb48mp", "got " + num(pp::PixelBudget::frame_bytes(8000, 6000, 3)));
        check(pp::PixelBudget::frame_bytes(100, 200, 1) == 80000ull, "frame-bytes/gray",
              "got " + num(pp::PixelBudget::frame_bytes(100, 200, 1)));
        check(pp::PixelBudget::frame_bytes(0, 200, 3) == 0, "frame-bytes/zero-width",
              "got " + num(pp::PixelBudget::frame_bytes(0, 200, 3)));
    }
    {
        const unsigned long long cap = pp::PixelBudget::default_capacity_bytes();
        check(cap > 0 && cap <= 8ull * kGiB, "default-capacity/range",
              "expect (0, 8GiB], got " + num(cap));
    }

    // ---- oversized request fails at once (no wait) ----
    {
        pp::PixelBudget b(1000);
        const auto t0 = std::chrono::steady_clock::now();
        const bool got = b.acquire(1001, [] { return false; });
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        check(!got, "over-limit/returns-false", "expected false");
        check(ms < 200, "over-limit/immediate", "took " + std::to_string(ms) + " ms");
        check(b.used() == 0, "over-limit/no-quota-taken", "used=" + num(b.used()));
    }

    // ---- acquire/release accounting + peak ----
    {
        pp::PixelBudget b(1000);
        check(b.acquire(400, nullptr), "acquire/first", "expected true");
        check(b.used() == 400, "acquire/used", "got " + num(b.used()));
        b.release(400);
        check(b.used() == 0, "release/back-to-zero", "got " + num(b.used()));
        check(b.acquire(1000, nullptr), "acquire/full-capacity", "expected true");
        b.release(1000);
        check(b.acquire(250, nullptr), "acquire/after-full-release", "expected true");
        b.release(250);
        check(b.peak() == 1000, "peak/tracks-max", "got " + num(b.peak()));
    }

    // ---- blocked until released ----
    // M2-T2 (#25): deterministic handshake instead of a 300 ms sleep. The cancel predicate is
    // consulted from inside PixelBudget::acquire() with the pool mutex held, and only after the
    // request turned out to be unsatisfiable -> observing it proves the worker is parked in
    // acquire() while the pool is still full (no timing assumption). The bounded wait keeps a
    // regression from hanging the test: on timeout the predicate is flipped so acquire() returns
    // and both the handshake and the wake assertions fail loudly.
    {
        pp::PixelBudget b(100);
        check(b.acquire(100, nullptr), "block/fill", "expected true");

        std::mutex mu;
        std::condition_variable cv;
        bool in_acquire = false;   // guarded by mu
        std::atomic<bool> abort{false};
        std::atomic<bool> acquired{false};

        std::thread t([&b, &mu, &cv, &in_acquire, &abort, &acquired] {
            const bool got = b.acquire(40, [&mu, &cv, &in_acquire, &abort] {
                {
                    std::lock_guard<std::mutex> lk(mu);
                    in_acquire = true;
                }
                cv.notify_one();
                return abort.load();   // always false unless the test timed out
            });
            if (got) {
                acquired.store(true);
                b.release(40);
            }
        });

        bool entered = false;
        {
            std::unique_lock<std::mutex> lk(mu);
            entered = cv.wait_for(lk, std::chrono::seconds(10), [&] { return in_acquire; });
        }
        if (!entered) abort.store(true);   // release the worker so t.join() cannot hang
        check(entered, "block/handshake",
              "acquire(40) never entered the wait path of a full pool");
        check(!acquired.load(), "block/still-waiting",
              "acquire(40) must not succeed while the pool is full");
        b.release(60);
        t.join();
        check(acquired.load(), "block/wakes-on-release", "acquire(40) should have succeeded");
        check(b.used() == 40, "block/remaining-used", "got " + num(b.used()));
        b.release(40);
        check(b.used() == 0, "block/final-used", "got " + num(b.used()));
    }

    // ---- multi-threaded acquire/release never overcommits ----
    {
        constexpr unsigned long long kCap = 4096;
        constexpr unsigned long long kChunk = 512;
        pp::PixelBudget b(kCap);
        std::atomic<bool> over{false};
        std::atomic<int> done{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&b, &over, &done] {
                for (int n = 0; n < 200; ++n) {
                    if (!b.acquire(kChunk, nullptr)) {
                        over.store(true);
                        break;
                    }
                    if (b.used() > kCap) {
                        over.store(true);
                    }
                    std::this_thread::yield();
                    b.release(kChunk);
                }
                done.fetch_add(1);
            });
        }
        for (std::thread& t : threads) {
            t.join();
        }
        check(!over.load(), "concurrent/no-overcommit", "used() exceeded capacity");
        check(done.load() == 4, "concurrent/all-finished", "got " + std::to_string(done.load()));
        check(b.used() == 0, "concurrent/balanced", "used=" + num(b.used()));
        check(b.peak() <= kCap, "concurrent/peak-within-cap", "peak=" + num(b.peak()));
    }

    // ---- cancel while blocked returns false without taking quota ----
    // M2-T2 (#25, same sleep handshake): the predicate being called proves acquire(50) is in its
    // wait path with the pool full, so the cancel flag is set *while blocked* deterministically.
    {
        pp::PixelBudget b(100);
        check(b.acquire(100, nullptr), "cancel/fill", "expected true");
        std::atomic<bool> cancel{false};
        std::atomic<int> result{-1};
        std::mutex mu;
        std::condition_variable cv;
        bool in_acquire = false;   // guarded by mu
        std::thread t([&b, &cancel, &result, &mu, &cv, &in_acquire] {
            const bool got = b.acquire(50, [&cancel, &mu, &cv, &in_acquire] {
                {
                    std::lock_guard<std::mutex> lk(mu);
                    in_acquire = true;
                }
                cv.notify_one();
                return cancel.load();
            });
            result.store(got ? 1 : 0);
        });
        bool entered = false;
        {
            std::unique_lock<std::mutex> lk(mu);
            entered = cv.wait_for(lk, std::chrono::seconds(10), [&] { return in_acquire; });
        }
        check(entered, "cancel/handshake", "acquire(50) never reached the wait path");
        cancel.store(true);
        t.join();
        check(result.load() == 0, "cancel/returns-false",
              "expected false, got result=" + std::to_string(result.load()));
        check(b.used() == 100, "cancel/no-quota-taken", "used=" + num(b.used()));
        b.release(100);
        check(b.used() == 0, "cancel/after-release", "used=" + num(b.used()));
    }

    // ---- cancel already set: even a satisfiable request is refused ----
    {
        pp::PixelBudget b(100);
        const bool got = b.acquire(10, [] { return true; });
        check(!got, "cancel/pre-set", "expected false");
        check(b.used() == 0, "cancel/pre-set-no-quota", "used=" + num(b.used()));
    }

    if (g_failed == 0) {
        std::printf("test_pixelbudget: OK\n");
        return 0;
    }
    std::printf("test_pixelbudget: FAILED (%d)\n", g_failed);
    return g_failed;
}
