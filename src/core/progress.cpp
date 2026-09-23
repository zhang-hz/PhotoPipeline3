// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/progress（0.3.0 / M4-W1-T6）：阶段权重表 + 合成进度 + 行级回调汇流
//
// 依据 docs/v0.3.0-design.md §7.1–§7.4（三级模型 / 权重表 / 合成 / 节流）与 §3.3/§3.6 行。
// 本文件只做**算术 + 汇流**：不做任何图像处理、不碰文件、不持有跨文件状态（逐文件一个 mux）。
//
// 目录：
//   1. StageWeights::stage_weight / stage_name
//   2. ProgressSynth（§7.3 合成进度发生器 + k 校准表）
//   3. ProgressMux（§7.4 汇流节流 + §7.2 权重算术 + §7.1 三级读数）
//
// 单调性契约（§7.3 + 金样 progress-trace）：任一输出的 `stage_frac`、文件级
// `overall_frac` 在时间轴上**只增不减**；mux 内部以 `*_high` 水位兜底（越界即 warn 日志），
// 消费端（GUI/侧车）无需再做单调化。

#include "core/progress.h"

#include "codecs/encoder.h" // ProgressFn（§3.1）
#include "core/logger.h"
#include "core/pipeline.h" // FileEvent / FileState（§3.3 的汇流目标；.cpp 层包含，无头文件环）

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace pp {

namespace {

constexpr std::string_view kStage = "progress";
constexpr std::string_view kFile = "progress.cpp";

using Clock = std::chrono::steady_clock;

// §7.4 节流：每 20ms 或每 0.5% 进度取先到者（"每 N 行或每 20ms"的落地口径；
// GUI 的 60ms 刷新节流由消费端负责，不在本模块）。
constexpr double kEmitMinIntervalMs = 20.0;
constexpr float kEmitStep = 0.005f;

double ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// §7.3 k 校准表（**可校准**）：k = 毫秒 / 百万像素，t_est = k × pixels / 1e6。
// 口径：列内格式 = "编码器无回调"面（§7.3 表：webp/heif/avif）。
// 初值 = W1-T6 本机实测回归（Windows 11 25H2 / i7-14700K / 24.0 MP 合成语料 / 默认参数 /
// `--workers 1`，取 `[pipeline] output done` 的 encode_ms 除以 MP；两种内容各测一遍，取几何均值
// 作为"典型内容"的折中）：
//   webp：高熵 11.51 s（480 ms/MP）· 平滑 5.01 s（208 ms/MP）→ 几何均值 316 → 取 315
//   heif：高熵  7.18 s（299 ms/MP）· 平滑 3.05 s（127 ms/MP）→ 几何均值 195
//   avif：高熵  1.55 s（ 64 ms/MP）· 平滑 0.54 s（ 22 ms/MP）→ 几何均值  38
// 回归方法（重跑即完成校准，无需改代码结构）：`photopipeline --dev <大图> --out … --format <fmt>
// --log-level debug` → 取 `encode_ms` 与像素数（MP）→ k = encode_ms/MP；样本多了就按内容类型取
// 几何均值覆盖上表。注意：k 偏大 = 估算偏慢（进度条偏保守，完成时跳 1.0）；k 偏小 = 提前顶到
// 0.95 后平推 —— 两个方向都不会在完成前虚报 100%（§7.3 的 0.95 封顶）。
struct KRow {
    std::string_view format;
    double k_ms_per_mp;
};
constexpr KRow kKTable[] = {
    {"webp", 315.0}, // 实测（见上）：480 / 208 ms/MP → 315
    {"heif", 195.0}, // 实测（见上）：299 / 127 ms/MP → 195
    {"avif", 38.0},  // 实测（见上）： 64 /  22 ms/MP →  38
};

std::string lower_ascii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

} // namespace

// ---------------------------------------------------------------------------
// 1. StageWeights / stage_name
// ---------------------------------------------------------------------------
float StageWeights::stage_weight(Stage s) {
    switch (s) {
    case Stage::Probe:
        return probe_acquire;
    case Stage::Decode:
        return decode;
    case Stage::Orient:
        return orient;
    case Stage::Color:
        return color;
    case Stage::Flatten:
        return 0.f; // §7.2 未列 Flatten → 无权重（逐输出 encode 段之前的编排步）
    case Stage::Encode:
        return encode;
    case Stage::MetaWrite:
        return metawrite;
    case Stage::Done:
        return 1.f; // 收尾样本（mux.finish() 的 stage 文本用）
    }
    return 0.f;
}

const char *stage_name(Stage s) {
    switch (s) {
    case Stage::Probe:
        return "Probe";
    case Stage::Decode:
        return "Decode";
    case Stage::Orient:
        return "Orient";
    case Stage::Color:
        return "Color";
    case Stage::Flatten:
        return "Flatten";
    case Stage::Encode:
        return "Encode";
    case Stage::MetaWrite:
        return "MetaWrite";
    case Stage::Done:
        return "Done";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// 2. ProgressSynth（§7.3）
// ---------------------------------------------------------------------------
double ProgressSynth::k_factor(std::string_view format_id) {
    const std::string key = lower_ascii(format_id);
    for (const KRow &row : kKTable) {
        if (row.format == key)
            return row.k_ms_per_mp;
    }
    return k_default;
}

bool ProgressSynth::has_k(std::string_view format_id) {
    const std::string key = lower_ascii(format_id);
    for (const KRow &row : kKTable) {
        if (row.format == key)
            return true;
    }
    return false;
}

ProgressSynth::ProgressSynth(std::string format_id, std::uint64_t pixels)
    : format_id_(std::move(format_id)), pixels_(pixels), t0_(Clock::now()) {
    const double mp = static_cast<double>(pixels_) / 1.0e6;
    t_est_ms_ = k_factor(format_id_) * mp;
    if (t_est_ms_ <= 0.0)
        value_ = 0.95f; // 退化（0 像素/0 k）：立即进入封顶平推
}

float ProgressSynth::on_tick(Clock::time_point now) {
    if (done_)
        return value_;
    const double window_ms = 0.95 * t_est_ms_; // [t0, t0+0.95·t_est] 上的缓出窗口
    float target = 0.95f;
    if (window_ms > 0.0) {
        const double elapsed = ms_between(t0_, now);
        const double x = clamp01(static_cast<float>(std::min(1.0, elapsed / window_ms)));
        target = static_cast<float>(0.95 * (1.0 - (1.0 - x) * (1.0 - x))); // f(x)=1-(1-x)^2 缓出
    }
    // 超窗/时钟回拨：停在 0.95 平推（不倒退）
    value_ = std::max(value_, std::min(target, 0.95f));
    return value_;
}

float ProgressSynth::on_done() {
    done_ = true;
    value_ = 1.0f; // 完成事件跳 1.0
    return value_;
}

// ---------------------------------------------------------------------------
// 3. ProgressMux（§7.1/§7.2/§7.4）
// ---------------------------------------------------------------------------
struct ProgressMux::Impl {
    struct Out {
        float enc_frac = 0.f;    // 编码段（§7.2 encode 50%）
        float meta_frac = 0.f;   // 落盘段（§7.2 metawrite 10%）
        int max_row = 0;         // 行级回调的最大折算行号（无回调 → 0）
        int samples = 0;         // 编码器回调次数
        bool started = false;    // bind_output 已调用
        bool reported = false;   // 编码器报过真实进度（§3.1 progress_reported）
        bool synth_flag = false; // 当前是否标合成（§7.3；= !reported 且属合成面）
        bool synth_expected = false;
        bool skipped = false, failed = false, done = false;
        std::string format_id;
        std::unique_ptr<ProgressSynth> synth;
    };
    struct EmitMark {
        double t_ms = -1.0e12;
        float frac = 0.f;
        bool valid = false;
    };

    int w = 0, h = 0;
    std::vector<Out> outs;
    float shared_probe = 0.f, shared_decode = 0.f, shared_orient = 0.f, shared_color = 0.f;
    // 单调水位（只增；防任何上游回退穿到消费端）。mutable：info_locked() 为 const 读快照，
    // 但同一水位必须对 const 读也生效（否则 const 快照可能返回低于已发事件的值）。
    mutable float overall_high = 0.f;
    std::map<int, EmitMark> marks;
    std::function<void(const FileEvent &)> cb;
    Clock::time_point t_start{Clock::now()};
    mutable std::mutex mu;

    explicit Impl(int width, int height, std::size_t outputs)
        : w(width), h(height), outs(outputs == 0 ? 1 : outputs) {}

    // ---- 加权算术（§7.2）----
    float file_frac_locked() const {
        const float shared = shared_probe + shared_decode + shared_orient + shared_color;
        double sum = 0.0;
        for (const Out &o : outs)
            sum += static_cast<double>(StageWeights::encode) * o.enc_frac +
                   static_cast<double>(StageWeights::metawrite) * o.meta_frac;
        const double n = static_cast<double>(outs.size());
        return clamp01(static_cast<float>(shared + sum / n));
    }
    float outputs_done_locked() const {
        double sum = 0.0;
        for (const Out &o : outs)
            sum += (static_cast<double>(StageWeights::encode) * o.enc_frac +
                    static_cast<double>(StageWeights::metawrite) * o.meta_frac) /
                   static_cast<double>(StageWeights::encode + StageWeights::metawrite);
        return clamp01(static_cast<float>(sum / static_cast<double>(outs.size())));
    }
    bool synthetic_locked(const Out &o) const { return !o.reported && o.synth_flag; }

    ProgressInfo info_locked(Stage s, int output_index, float stage_frac) const {
        ProgressInfo pi;
        pi.output_index = output_index;
        pi.stage = s;
        pi.stage_frac = clamp01(stage_frac);
        float overall = file_frac_locked();
        if (overall < overall_high) {
            // 契约兜底：正常路径不该发生（各分量单调）；发生即如实记一条 warn 并沿用高水位。
            log_warn(
                kStage, kFile, "progress overall regressed; holding high-water mark",
                {{"computed", std::to_string(overall)}, {"high", std::to_string(overall_high)}});
            overall = overall_high;
        }
        overall_high = overall;
        pi.overall_frac = overall;
        pi.synthetic = (output_index >= 0 && output_index < static_cast<int>(outs.size()))
                           ? synthetic_locked(outs[static_cast<std::size_t>(output_index)])
                           : false;
        return pi;
    }
    // 节流判决（§7.4）：首样本 / 到达 1.0 / 时间窗 / 进度步进，取先到者；force = 阶段切换等强制发点
    bool should_emit_locked(int key, float frac, float overall, bool force) {
        EmitMark &m = marks[key];
        if (force)
            return true;
        if (!m.valid)
            return true;
        if (frac >= 1.0f && m.frac < 1.0f)
            return true; // 段内完成必须递达（§7.3 "完成事件跳 1.0"）
        if (overall >= 1.0f && m.frac < 1.0f)
            return true;
        if (ms_between(t_start, Clock::now()) - m.t_ms >= kEmitMinIntervalMs)
            return true;
        if (frac - m.frac >= kEmitStep)
            return true;
        return false;
    }
    void mark_locked(int key, float frac) {
        EmitMark &m = marks[key];
        m.t_ms = ms_between(t_start, Clock::now());
        m.frac = frac;
        m.valid = true;
    }
    // 事件回调**持锁**调用：进度事件必须按序递达（两个采样源 = 编码器线程 / 合成泵线程）
    void emit_locked(int output_index, Stage s, float stage_frac, bool force) {
        const ProgressInfo pi = info_locked(s, output_index, stage_frac);
        if (!should_emit_locked(output_index, pi.stage_frac, pi.overall_frac, force))
            return;
        mark_locked(output_index, pi.stage_frac);
        if (cb)
            cb(FileEvent{0, FileState::Progress, nullptr, pi});
    }
};

ProgressMux::ProgressMux(int width, int height, std::size_t outputs)
    : impl_(std::make_unique<Impl>(width, height, outputs)) {}

ProgressMux::~ProgressMux() = default;

void ProgressMux::set_event_callback(std::function<void(const FileEvent &)> cb) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->cb = std::move(cb);
}

void ProgressMux::shared_stage(Stage s, float frac) {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    const float f = clamp01(frac);
    switch (s) {
    case Stage::Probe:
        d.shared_probe = std::max(d.shared_probe, StageWeights::probe_acquire * f);
        break;
    case Stage::Decode:
        d.shared_decode = std::max(d.shared_decode, StageWeights::decode * f);
        break;
    case Stage::Orient:
        d.shared_orient = std::max(d.shared_orient, StageWeights::orient * f);
        break;
    case Stage::Color:
        d.shared_color = std::max(d.shared_color, StageWeights::color * f);
        break;
    default:
        return; // 源文件级共享段只有 probe/decode/orient/color 四段（§7.2）
    }
    d.emit_locked(-1, s, f, /*force=*/true);
}

ProgressFn ProgressMux::bind_output(std::size_t index, std::string_view format_id) {
    Impl &d = *impl_;
    {
        std::lock_guard<std::mutex> lk(d.mu);
        if (index >= d.outs.size())
            return ProgressFn{};
        Impl::Out &o = d.outs[index];
        o.started = true;
        o.format_id = std::string(format_id);
        o.synth_expected = ProgressSynth::has_k(format_id);
        o.synth_flag =
            o.synth_expected; // 无回调面先按合成标识；编码返回后按 progress_reported 校正
        if (!o.synth) {
            o.synth = std::make_unique<ProgressSynth>(
                o.format_id, static_cast<std::uint64_t>(d.w) * static_cast<std::uint64_t>(d.h));
        }
        if (!o.synth_expected) {
            log_debug(kStage, kFile, "format not in the k table; synthetic estimate uses k_default",
                      {{"format", o.format_id},
                       {"k_default_ms_per_mp", std::to_string(ProgressSynth::k_default)}});
        }
        d.emit_locked(static_cast<int>(index), Stage::Encode, 0.f, /*force=*/true);
    }
    return ProgressFn{[this, index](float v) {
        // 进度回调不得参与编码成败（§3.1 "尽力而为"）：任何异常在此吞掉并记一条 warn，
        // 保证 E8（异常不穿越 IEncoder 边界）与编码器内的 C 资源清理路径不被破坏。
        try {
            Impl &dd = *impl_;
            std::lock_guard<std::mutex> lk(dd.mu);
            if (index >= dd.outs.size())
                return;
            Impl::Out &o = dd.outs[index];
            const float f = clamp01(v);
            o.reported = true; // 真实进度不得被合成覆盖（§3.1/§7.3）
            o.synth_flag = false;
            ++o.samples;
            o.enc_frac = std::max(o.enc_frac, f);
            if (dd.h > 0)
                o.max_row = std::max(o.max_row, static_cast<int>(std::lround(f * dd.h)));
            dd.emit_locked(static_cast<int>(index), Stage::Encode, o.enc_frac, /*force=*/false);
        } catch (...) {
            static std::once_flag warned;
            std::call_once(warned, [] {
                log_warn(kStage, kFile, "progress callback threw; ignored",
                         {{"source", "encoder ProgressFn"}});
            });
        }
    }};
}

bool ProgressMux::synth_active(std::size_t index) const {
    const Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    if (index >= d.outs.size())
        return false;
    const Impl::Out &o = d.outs[index];
    return o.synth_expected && !o.reported && !o.done && !o.skipped && !o.failed;
}

void ProgressMux::output_stage(std::size_t index, Stage s, float frac) {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    if (index >= d.outs.size())
        return;
    d.outs[index].started = true;
    d.emit_locked(static_cast<int>(index), s, frac, /*force=*/true);
}

void ProgressMux::on_output_encode_done(std::size_t index, bool progress_reported) {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    if (index >= d.outs.size())
        return;
    Impl::Out &o = d.outs[index];
    o.reported = progress_reported;
    o.synth_flag = !progress_reported; // false → 该输出标合成（§3.1）
    if (o.synth) {
        // 编码结束：合成估算收敛到 1.0（完成事件跳 1.0，§7.3）
        o.synth->on_done();
        o.enc_frac = std::max(o.enc_frac, o.synth->value());
    }
    o.enc_frac = 1.f; // §7.2：encode 段完成
    d.emit_locked(static_cast<int>(index), Stage::Encode, 1.f, /*force=*/true);
}

void ProgressMux::on_output_metawrite_done(std::size_t index) {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    if (index >= d.outs.size())
        return;
    Impl::Out &o = d.outs[index];
    o.meta_frac = 1.f; // §7.2：metawrite + mtime + 落盘
    o.done = true;
    d.emit_locked(static_cast<int>(index), Stage::MetaWrite, 1.f, /*force=*/true);
}

void ProgressMux::on_output_failed(std::size_t index) {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    if (index >= d.outs.size())
        return;
    Impl::Out &o = d.outs[index];
    o.failed = true; // 不虚增：失败输出停在已完成的进度上
    d.emit_locked(static_cast<int>(index), Stage::Encode, o.enc_frac, /*force=*/true);
}

void ProgressMux::on_output_skipped(std::size_t index) {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    if (index >= d.outs.size())
        return;
    Impl::Out &o = d.outs[index];
    o.started = true;
    o.skipped = true;
    o.done = true;
    o.enc_frac = 1.f; // 该输出无工作可做 → 全段记满（文件整体仍可达 1.0）
    o.meta_frac = 1.f;
    d.emit_locked(static_cast<int>(index), Stage::MetaWrite, 1.f, /*force=*/true);
}

void ProgressMux::on_output_no_encode(std::size_t index) {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    if (index >= d.outs.size())
        return;
    Impl::Out &o = d.outs[index];
    o.started = true;
    o.enc_frac = 1.f; // 仅元数据模式：无编码段（§3.2 的另一个入口）
    d.emit_locked(static_cast<int>(index), Stage::MetaWrite, 0.f, /*force=*/true);
}

int ProgressMux::tick(std::chrono::steady_clock::time_point now) {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    int advanced = 0;
    for (std::size_t i = 0; i < d.outs.size(); ++i) {
        Impl::Out &o = d.outs[i];
        if (!o.synth || o.reported || o.done || o.skipped || o.failed)
            continue;
        // 未绑定的输出不产生合成样本（没有编码在跑）
        if (!o.started || !o.synth_expected)
            continue;
        const float est = o.synth->on_tick(now);
        if (est <= o.enc_frac)
            continue;
        o.enc_frac = est;
        ++advanced;
        d.emit_locked(static_cast<int>(i), Stage::Encode, o.enc_frac, /*force=*/false);
    }
    return advanced;
}

int ProgressMux::tick() { return tick(Clock::now()); }

void ProgressMux::finish() {
    Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    // 收尾样本：成功路径此处 file_frac 已按 §7.2 达 1.0（§7.1 的逐输出段已全部记满）
    d.emit_locked(-1, Stage::Done, d.file_frac_locked(), /*force=*/true);
}

float ProgressMux::file_frac() const {
    const Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    return d.file_frac_locked();
}

float ProgressMux::outputs_done_frac() const {
    const Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    return d.outputs_done_locked();
}

float ProgressMux::output_frac(std::size_t index) const {
    const Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    if (index >= d.outs.size())
        return 0.f;
    const Impl::Out &o = d.outs[index];
    const double chunk = StageWeights::encode + StageWeights::metawrite;
    return clamp01(static_cast<float>(
        (StageWeights::encode * o.enc_frac + StageWeights::metawrite * o.meta_frac) / chunk));
}

std::size_t ProgressMux::outputs() const { return impl_->outs.size(); }

ProgressInfo ProgressMux::snapshot(Stage s, int output_index) const {
    const Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    ProgressInfo pi;
    pi.output_index = output_index;
    pi.stage = s;
    pi.stage_frac = 0.f;
    pi.overall_frac = d.file_frac_locked();
    pi.synthetic = (output_index >= 0 && output_index < static_cast<int>(d.outs.size()))
                       ? d.synthetic_locked(d.outs[static_cast<std::size_t>(output_index)])
                       : false;
    return pi;
}

ProgressMux::Stats ProgressMux::stats(std::size_t index) const {
    const Impl &d = *impl_;
    std::lock_guard<std::mutex> lk(d.mu);
    Stats st;
    if (index >= d.outs.size())
        return st;
    const Impl::Out &o = d.outs[index];
    st.enc_frac = o.enc_frac;
    st.meta_frac = o.meta_frac;
    st.max_row = o.max_row;
    st.samples = o.samples;
    st.reported = o.reported;
    st.synthetic = d.synthetic_locked(o);
    st.skipped = o.skipped;
    st.started = o.started;
    st.est_ms = o.synth ? o.synth->estimate_ms() : 0.0;
    return st;
}

} // namespace pp
