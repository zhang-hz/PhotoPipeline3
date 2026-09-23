// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M4-W1-T6 — core/progress unit tests (hand-written assertions).
//
// 契约：docs/v0.3.0-design.md §7.1–§7.4（三级模型 / 阶段权重表 / 合成进度 / 汇流节流）
//   + §3.3（ProgressInfo / FileEvent.progress）+ §3.6（core/progress.h 行）。
// 覆盖（任务书步骤 7 逐条）：
//   1. 权重表合计 = 1（含 §7.2 表行拆分与 Flatten 无权重口径）；
//   2. ProgressSynth：单调不倒退 + 0.95 封顶 + 完成跳 1.0 + 超时（过 t_est）平推；
//   3. ProgressMux：§7.2 共享段/输出段加权 + §7.1 运行级分子 + 节流行为 +
//      synthetic 口径（真实进度不得被合成覆盖）+ 事件单调递达 + 快照字段。
// 每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数（0 = 全绿）。

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/pipeline.h" // FileEvent / FileState（§3.3）
#include "core/progress.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

std::string f2s(float v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(v));
    return buf;
}

using Clock = std::chrono::steady_clock;

// 采集事件用的最小 sink（ProgressMux 的事件回调面）
struct Collector {
    std::vector<pp::FileEvent> events;
    void operator()(const pp::FileEvent &e) { events.push_back(e); }
    std::size_t count_state(pp::FileState s) const {
        std::size_t n = 0;
        for (const pp::FileEvent &e : events)
            if (e.state == s)
                ++n;
        return n;
    }
    // 进度事件里的 overall_frac 序列（消费端看到的单调契约）
    bool monotonic(std::string *why) const {
        float last = -1.f;
        for (const pp::FileEvent &e : events) {
            const float v = e.progress.overall_frac;
            if (v < last - 1e-6f) {
                if (why != nullptr)
                    *why = "overall_frac regressed: " + f2s(last) + " -> " + f2s(v);
                return false;
            }
            last = v;
        }
        return true;
    }
};

} // namespace

int main() {
    // =====================================================================
    // 1. 阶段权重表（§7.2）
    // =====================================================================
    {
        const float sum = pp::StageWeights::probe_acquire + pp::StageWeights::decode +
                          pp::StageWeights::orient_color + pp::StageWeights::encode +
                          pp::StageWeights::metawrite;
        check(near(sum, 1.0f), "weights/sum-one", "sum=" + f2s(sum));
        check(near(pp::StageWeights::total, 1.0f), "weights/total-one",
              "total=" + f2s(pp::StageWeights::total));
        check(near(pp::StageWeights::shared, 0.40f), "weights/shared-forty",
              "shared=" + f2s(pp::StageWeights::shared));
        check(near(pp::StageWeights::orient, pp::StageWeights::color), "weights/orient-color-5050",
              f2s(pp::StageWeights::orient) + " vs " + f2s(pp::StageWeights::color));
        check(near(pp::StageWeights::orient + pp::StageWeights::color,
                   pp::StageWeights::orient_color),
              "weights/orient-color-split", "split must equal the §7.2 row weight");
        // 单阶段权重（Flatten 不在 §7.2 表内 → 0）
        check(near(pp::StageWeights::stage_weight(pp::Stage::Probe), 0.03f), "weights/row-probe",
              f2s(pp::StageWeights::stage_weight(pp::Stage::Probe)));
        check(near(pp::StageWeights::stage_weight(pp::Stage::Decode), 0.27f), "weights/row-decode",
              f2s(pp::StageWeights::stage_weight(pp::Stage::Decode)));
        check(near(pp::StageWeights::stage_weight(pp::Stage::Encode), 0.50f), "weights/row-encode",
              f2s(pp::StageWeights::stage_weight(pp::Stage::Encode)));
        check(near(pp::StageWeights::stage_weight(pp::Stage::MetaWrite), 0.10f),
              "weights/row-metawrite", f2s(pp::StageWeights::stage_weight(pp::Stage::MetaWrite)));
        check(near(pp::StageWeights::stage_weight(pp::Stage::Flatten), 0.0f),
              "weights/flatten-zero", "Flatten must carry no weight (not listed in §7.2)");
    }

    // =====================================================================
    // 2. ProgressSynth（§7.3）
    // =====================================================================
    {
        // k 表：webp/heif/avif 在表内（合成面），jpeg/jxl 不在（真实回调面）
        check(pp::ProgressSynth::has_k("webp"), "synth/k-webp", "webp must be in the k table");
        check(pp::ProgressSynth::has_k("heif"), "synth/k-heif", "heif must be in the k table");
        check(pp::ProgressSynth::has_k("avif"), "synth/k-avif", "avif must be in the k table");
        check(!pp::ProgressSynth::has_k("jpeg"), "synth/k-jpeg-absent",
              "jpeg has a real row callback -> must not be a synthetic format");
        check(!pp::ProgressSynth::has_k("jxl"), "synth/k-jxl-absent",
              "jxl reports real progress -> must not be a synthetic format");
        check(pp::ProgressSynth::k_factor("WEBP") == pp::ProgressSynth::k_factor("webp"),
              "synth/k-case-insensitive", "format ids are case-insensitive");
        check(pp::ProgressSynth::k_factor("nope") == pp::ProgressSynth::k_default,
              "synth/k-default", "unlisted format must fall back to k_default");
    }
    {
        using Synth = pp::ProgressSynth;
        Synth s("webp", 24 * 1000 * 1000); // 24 MP
        check(s.synthetic(), "synth/flag-true", "ProgressSynth is always synthetic (§7.3)");
        check(s.estimate_ms() > 0.0, "synth/estimate-positive",
              "t_est must be positive: " + f2s(static_cast<float>(s.estimate_ms())));
        const auto t0 = Clock::now();
        const double win = 0.95 * s.estimate_ms();

        // 单调不递减：密集采样 [t0, t0+2·t_est]
        float last = -1.f;
        bool monotone = true;
        bool capped = true;
        for (int i = 0; i <= 200; ++i) {
            const double ms = win * 2.0 * static_cast<double>(i) / 200.0;
            const float v = s.on_tick(t0 + std::chrono::duration_cast<Clock::duration>(
                                               std::chrono::duration<double, std::milli>(ms)));
            if (v < last - 1e-6f)
                monotone = false;
            if (v > 0.95f + 1e-6f)
                capped = false;
            last = v;
        }
        check(monotone, "synth/monotonic", "on_tick must never regress");
        check(capped, "synth/cap-095",
              "value must stay <= 0.95 before completion, got " + f2s(last));
        check(near(last, 0.95f), "synth/flat-at-095",
              "past t_est the value must hold at 0.95 (平推), got " + f2s(last));

        // 缓出形状 f(x)=1-(1-x)^2：窗口半程应约 0.95·0.75
        Synth half("webp", 24 * 1000 * 1000);
        const double hw = 0.95 * half.estimate_ms();
        const float hv =
            half.on_tick(t0 + std::chrono::duration_cast<Clock::duration>(
                                  std::chrono::duration<double, std::milli>(hw * 0.5)));
        check(near(hv, 0.95f * 0.75f, 1e-3f), "synth/ease-out-shape",
              "f(0.5) must be 1-(1-0.5)^2 = 0.75 of the cap, got " + f2s(hv));

        // 完成事件跳 1.0（此后 tick 亦保持）
        check(near(s.on_done(), 1.0f), "synth/done-jumps-one", f2s(s.on_done()));
        check(s.done(), "synth/done-flag", "done() after on_done()");
        check(near(s.on_tick(t0), 1.0f), "synth/done-sticky",
              "after completion on_tick must stay at 1.0");
    }
    {
        // 退化输入（0 像素）：立即可用，不 NaN、不倒退
        pp::ProgressSynth s("heif", 0);
        check(s.on_tick(Clock::now()) >= 0.f, "synth/zero-pixels-safe", "must not be negative");
        check(s.on_tick(Clock::now()) <= 0.95f, "synth/zero-pixels-capped", "must respect the cap");
    }

    // =====================================================================
    // 3. ProgressMux（§7.1/§7.2/§7.4）
    // =====================================================================
    {
        // 单输出：共享段 + 输出段按 §7.2 加权
        Collector col;
        pp::ProgressMux mux(100, 100, 1);
        mux.set_event_callback(std::ref(col));
        check(near(mux.file_frac(), 0.f), "mux/initial-zero", f2s(mux.file_frac()));

        mux.shared_stage(pp::Stage::Probe, 1.f);
        check(near(mux.file_frac(), 0.03f), "mux/shared-probe",
              "probe+acquire = 3%, got " + f2s(mux.file_frac()));
        mux.shared_stage(pp::Stage::Decode, 1.f);
        check(near(mux.file_frac(), 0.30f), "mux/shared-decode",
              "decode = 27%, got " + f2s(mux.file_frac()));
        mux.shared_skip(pp::Stage::Orient);
        mux.shared_skip(pp::Stage::Color);
        check(near(mux.file_frac(), 0.40f), "mux/shared-skipped-credit",
              "skipped stages must still credit their weight → 40%, got " + f2s(mux.file_frac()));

        const pp::ProgressFn fn = mux.bind_output(0, "jpeg");
        check(static_cast<bool>(fn), "mux/bind-returns-fn", "bind_output must return a ProgressFn");
        check(!mux.synth_active(0), "mux/jpeg-not-synthetic",
              "jpeg reports real progress → no synth pump");
        fn(0.5f);
        check(near(mux.file_frac(), 0.40f + 0.25f), "mux/encode-half",
              "encode 50% × 0.5 → +25%, got " + f2s(mux.file_frac()));
        check(near(mux.output_frac(0), (0.5f * 0.5f) / 0.6f), "mux/output-frac-normalised",
              "single-output completion must be (0.5e+0.1m)/0.6 with e=0.5, got " +
                  f2s(mux.output_frac(0)));
        mux.on_output_encode_done(0, /*progress_reported=*/true);
        check(near(mux.file_frac(), 0.90f), "mux/encode-done",
              "encode segment complete → 40%+50%, got " + f2s(mux.file_frac()));
        mux.on_output_metawrite_done(0);
        check(near(mux.file_frac(), 1.0f), "mux/file-frac-one",
              "all segments complete → 1.0, got " + f2s(mux.file_frac()));
        check(near(mux.outputs_done_frac(), 1.0f), "mux/run-level-one",
              "§7.1 numerator per file, got " + f2s(mux.outputs_done_frac()));
        check(col.count_state(pp::FileState::Progress) > 0, "mux/emits-progress-state",
              "ProgressMux must emit FileEvent{state=Progress} (§7.4)");
        std::string why;
        check(col.monotonic(&why), "mux/events-monotonic", why);
        for (const pp::FileEvent &e : col.events) {
            if (e.state == pp::FileState::Progress && e.progress.synthetic) {
                check(false, "mux/real-not-synthetic",
                      "a real-progress output must never be marked synthetic (§3.1)");
                break;
            }
        }
        const pp::ProgressMux::Stats st = mux.stats(0);
        check(st.reported && !st.synthetic && st.samples == 1 && st.max_row == 50,
              "mux/stats-snapshot",
              "reported/samples/max_row must follow the row callback (height 100 → row 50)");
    }
    {
        // 多输出：源文件级 = 共享段一次 + mean(各输出 50%+10%)（§7.2）；运行级 = Σ/(文件数×格式数)
        Collector col;
        pp::ProgressMux mux(64, 64, 2);
        mux.set_event_callback(std::ref(col));
        mux.shared_stage(pp::Stage::Probe, 1.f);
        mux.shared_stage(pp::Stage::Decode, 1.f);
        mux.shared_skip(pp::Stage::Orient);
        mux.shared_skip(pp::Stage::Color);
        // 输出 0：真实行级（jpeg）
        const pp::ProgressFn f0 = mux.bind_output(0, "jpeg");
        f0(1.0f);
        mux.on_output_encode_done(0, true);
        mux.on_output_metawrite_done(0);
        check(near(mux.file_frac(), 0.70f), "mux/multi-after-first",
              "shared 0.40 + mean(0.6, 0)/2 = 0.70, got " + f2s(mux.file_frac()));
        // 输出 1：合成面（webp）
        const pp::ProgressFn f1 = mux.bind_output(1, "webp");
        check(mux.synth_active(1), "mux/webp-synthetic-active",
              "webp must be a synthetic-estimate output (§7.3), checked after bind");
        const pp::ProgressInfo snap = mux.snapshot(pp::Stage::Encode, 1);
        check(snap.synthetic, "mux/webp-snapshot-synthetic",
              "snapshot of a synthetic output must carry synthetic=true (§7.3 UI 斜纹)");
        check(snap.output_index == 1, "mux/snapshot-output-index", "output_index must round-trip");
        // 合成估算推进（tick，等价泵线程 20ms 采样）
        const auto t0 = Clock::now();
        mux.tick(t0 + std::chrono::milliseconds(5000));
        const float synth_mid = mux.stats(1).enc_frac;
        check(synth_mid > 0.f && synth_mid <= 0.95f, "mux/synth-tick-advances",
              "estimated frac must advance but stay capped, got " + f2s(synth_mid));
        mux.tick(t0 + std::chrono::milliseconds(1000)); // 时钟回拨
        check(mux.stats(1).enc_frac >= synth_mid, "mux/synth-no-regress",
              "a backwards clock must not regress the estimate");
        // 合成输出拿到真实回调 → 立即翻 real（真实进度不得被合成覆盖）
        f1(0.25f);
        check(!mux.stats(1).synthetic && mux.stats(1).reported, "mux/real-overrides-synth",
              "a real callback must clear the synthetic flag (§3.1)");
        mux.on_output_encode_done(1, /*progress_reported=*/false);
        check(mux.stats(1).synthetic, "mux/reported-false-marks-synthetic",
              "progress_reported=false → the output is marked synthetic (§3.1)");
        mux.on_output_metawrite_done(1);
        mux.finish();
        check(near(mux.file_frac(), 1.0f), "mux/multi-final-one", f2s(mux.file_frac()));
        check(near(mux.outputs_done_frac(), 1.0f), "mux/multi-run-level-one",
              f2s(mux.outputs_done_frac()));
        std::string why;
        check(col.monotonic(&why), "mux/multi-monotonic", why);
        // 合成输出的进度事件必须是 synthetic=true（含 bind 之后到 finish 之前的全部）
        std::size_t synth_events = 0, real_events = 0;
        for (const pp::FileEvent &e : col.events) {
            if (e.state != pp::FileState::Progress)
                continue;
            if (e.progress.output_index == 1)
                e.progress.synthetic ? ++synth_events : ++real_events;
        }
        check(synth_events > 0, "mux/synth-events-present", "synthetic output must emit events");
        check(real_events == 0, "mux/synth-events-consistent",
              "every event of a synthetic output must carry synthetic=true (" +
                  std::to_string(real_events) + " violated)");
    }
    {
        // §7.2：Skip 命中的输出全段记满（文件整体仍可达 1.0）
        Collector col;
        pp::ProgressMux mux(32, 32, 2);
        mux.set_event_callback(std::ref(col));
        mux.shared_stage(pp::Stage::Probe, 1.f);
        mux.shared_stage(pp::Stage::Decode, 1.f);
        mux.shared_skip(pp::Stage::Orient);
        mux.shared_skip(pp::Stage::Color);
        mux.on_output_skipped(1);
        const pp::ProgressFn f0 = mux.bind_output(0, "jpeg");
        f0(1.0f);
        mux.on_output_encode_done(0, true);
        mux.on_output_metawrite_done(0);
        check(near(mux.file_frac(), 1.0f), "mux/skip-credits-fully",
              "a skipped output must be credited (file reaches 1.0), got " + f2s(mux.file_frac()));
        std::string why;
        check(col.monotonic(&why), "mux/skip-monotonic", why);
    }
    {
        // §7.4 节流：密集回调（同一时刻推进 0.05% 步进）必须被折叠；末值 1.0 必须递达
        Collector col;
        pp::ProgressMux mux(1000, 1000, 1);
        mux.set_event_callback(std::ref(col));
        mux.shared_stage(pp::Stage::Probe, 1.f);
        mux.shared_stage(pp::Stage::Decode, 1.f);
        mux.shared_skip(pp::Stage::Orient);
        mux.shared_skip(pp::Stage::Color);
        const pp::ProgressFn fn = mux.bind_output(0, "jpeg");
        const int kSteps = 2000; // 0.05% 步进 → 1000 个 0.5% 步格
        for (int i = 1; i <= kSteps; ++i)
            fn(static_cast<float>(i) / static_cast<float>(kSteps));
        const std::size_t progress_events = col.count_state(pp::FileState::Progress);
        check(progress_events < static_cast<std::size_t>(kSteps) / 2, "mux/throttle-collapses",
              std::to_string(progress_events) + " events for " + std::to_string(kSteps) +
                  " callbacks (must collapse)");
        check(progress_events > 0, "mux/throttle-emits-something", "0 events is a false pass");
        check(col.events.back().progress.stage_frac >= 1.0f - 1e-6f, "mux/throttle-last-sample",
              "the final sample must reach 1.0, got " + f2s(col.events.back().progress.stage_frac));
        std::string why;
        check(col.monotonic(&why), "mux/throttle-monotonic", why);
        // 重复同值回调不再产生事件（去重）
        const std::size_t before = col.events.size();
        for (int i = 0; i < 100; ++i)
            fn(1.0f);
        check(col.events.size() == before, "mux/throttle-dedup",
              "identical samples must not re-emit (" + std::to_string(col.events.size() - before) +
                  " extra)");
    }

    if (g_failed == 0)
        std::printf("test_progress: all checks passed\n");
    return g_failed;
}
