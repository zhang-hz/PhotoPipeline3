// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M4-T5: multi-output single-file pipeline
// (docs/v0.3.0-design.md §3.2 / §4.1 / §4.2 / §4.3 / §4.4).
//
// M7-WP8 / B.4（docs/deep-review-0.3.2.md 附录 B.4，R-061/324）：run_one_file 拆十阶段 +
// 单一上下文 RunFileContext{fe, cfg, ev, scope, mux, res, t_start}（替代 5 个内联 lambda 的
// 捕获面）。阶段函数 = validate_run_config → probe_source → plan_targets → acquire_budget
// （BudgetGuard 命名 RAII）→ decode_to_work → orient_stage → color_stage → build_meta →
// encode_loop（多输出循环整体下沉）→ finish_file（aggregate 单源聚合）。**行为零变化**：
// 日志行/事件序/计时点/告警序列逐字保留（金样 20 对 + 全语料回归背书）。
//
// 单文件执行序（§4.1 冻结）：
//   probe(一次) → budget.acquire(2×frame) → decode(一次, float32) → orient(如启用)
//   → color(全局目标, 一次) → 按 targets 顺序逐个：
//       [首个破坏 alpha 的 target 之前，在工作缓冲内原地 flatten（§4.3）]
//       encode(target, progress) → metawrite(target 写路径) → 记录 result[target]
//   → mtime(按 effective DateTimeOriginal) → release → 聚合终态(§4.1)
//
// 内存纪律（§4.1 + 本任务提示词）：
//   * 峰值上界 2×frame，且**每一步都在界内**（复核项 1/8 修订）：
//       建立工作缓冲 = decode 缓冲 + 工作缓冲（随即释放 decode 缓冲）；
//       orient        = 工作缓冲 + 一份临时缓冲（EXIF 7 的第二段直接写回工作缓冲，不再分配）；
//       color 升维    = 工作缓冲 + ColorManager 的新缓冲（回拷前先释放旧块，避免扩容瞬时双块）；
//       flatten       = 仅工作缓冲（**原地**合成，零额外帧）。
//   * 工作缓冲的像素存储由 pipeline 自持（std::vector<float>），OIIO::ImageBuf 只做非拥有包装
//     （APPBUFFER）—— 这样 flatten 才能"原地缩通道"。
//   * color 回拷规则（W1-T5 复核项 1 修订，2026-09-24）：ColorManager 的两条落地路径是
//       (i) 灰度源（1/2ch）→ 彩色目标：换**独立新缓冲**（升维）→ 必须回拷；
//       (ii) RGB/RGBA（3/4ch）→ **原地**写回我们包装的同一块存储 → **不得回拷**。
//     判据 = "当前缓冲的本地像素地址是否仍是工作缓冲的存储"；同址回拷会把刚变换好的像素清零
//     后再从同一块内存读回 = 全黑图（静默数据损失，见 color_stage 的逐条注释）。
//   * 逐 target 串行（同文件内），文件间并行由 scheduler 管。
//
// 编码顺序：alpha 不支持（第二轮裁定）→ 所有 target 在 color 前已拍平，无需按
// supports_alpha 排序；FileResult.outputs 仍按**配置顺序**记录。
//
// 其余冻结语义沿用 0.2（§4.8 gray/ICC、第 4 章 metadata、§3.4 reserved）：
//   * gray 源进入不支持灰度的格式 → Warning{GrayToRgbEncoded}；多输出时"任一 target 不支持
//     灰度"即以 sRGB 为有效目标变换一次（color 全局共享，D5）；
//   * alpha → 所有带 alpha 的输入在 color 前拍平，逐输出报 Warning{AlphaFlattened}；
//   * BMP 丢元数据 + warning 不变；meta_plan 只构建一次（全局共享）。
//   * 逐输出独立冲突解析（§4.4）：每 target 各调一次 resolve_conflict，reserved 快照按
//     out_path 粒度由调用方登记。
//
// 工作缓冲里不属于任何 target 的告警（decode/color/…）按"出现时刻"推入每个未跳过 target
// 的行内 —— 单输出时逐字等价于 0.2 的告警序列。
//
// W1-T6 进度接线（design §7.1–§7.4；本文件是进度事件的**唯一**产生点）：
//   * 源文件级共享段（一次）：probe+acquire 3% → decode 27% → orient 5% → color 5%
//     （不执行的阶段用 shared_skip 即时记满 → 共享段恒达 40%，权重不丢失）。
//   * 输出级（逐 target）：flatten（无权重，仅阶段文本）→ encode 50%（真实行级 / 合成）→
//     metawrite+mtime+落盘 10%；Skip 命中的输出全段记满（文件整体仍可达 1.0）。
//   * 事件：阶段事件携带当前进度快照（FileState 为阶段文本）；进度事件由 ProgressMux 发
//     `FileState::Progress`（节流：每 20ms 或每 0.5% 进度，§7.4）。
//   * 合成输出（k 表内的 webp/heif/avif）：编码期间由 SynthPump 按 20ms 采样推进估算（§7.3）；
//     真实行级输出（jpegli/jxl/OIIO）直接经 ProgressFn 汇流，`synthetic=false`。
//   * 每输出的 debug 快照：progress_max_row / progress_reported / samples / synthetic / est_ms
//     （§7.4，**不逐行落盘**；dev 侧车另落 progress-trace 事件流，见 dev_cli.cpp）。

#include "core/pipeline.h"

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "codecs/enc_common.h" // M10 / W1-B1（T6）：编码器飞行缓冲估算（enc::encoder_flight_bytes）
#include "codecs/encoders.h"
#include "codecs/heif_profile.h" // 需求 1 日志：kHeifProfileParamKey（基准参数白名单的 profile 别名）
#include "core/adaptive.h"       // M10 / W3-T4：策略 DSL 解析/校验（自适应输出）
#include "core/adaptive_exec.h" // M10 / W3-T4：执行器（轮循环/候选文件/同步评分）
#include "core/colormanager.h"
#include "core/fastcolor.h" // M10-W1-T2c：评分色彩归一快速内核（3D CLUT）
#include "core/fsops.h"
#include "core/logger.h"
#include "core/metadata.h"
#include "core/metric/metric.h" // M8-C2：SSIMULACRA 2 评分（C0 冻结接口）
#include "core/params.h"
#include "core/pixelbudget.h"
#include "core/progress.h"
#include "core/score_queue.h" // M10 / W1-B1（T5）：进程级评分池（异步评分）
#include "core/simd/simd.h"   // M4-W5-T19：flatten 热路径（§11.2）
#include "decode/oiio_reader.h"

namespace pp {

// Defined in metadata.cpp（第二轮裁定：转码元数据计划净化；不进冻结 metadata.h）。
void sanitize_transcode_metadata(MetadataPlan &plan);

// Defined in metadata.cpp (runtime capability probe; intentionally outside the frozen
// header). The strong definition of the frozen predicate below delegates to it (§4.8).
bool detail_metadata_only_supported(std::string_view format_id);

namespace {

constexpr std::string_view kStage = "pipeline";
constexpr std::string_view kFile = "pipeline.cpp";

// §8.2/W1-T7：编码器内部线程数 E 由调度器经 `RunScope::encode_threads` 传入（E3 分配）。
// 无 scope / 非法值（<1）→ 回落 1 = 0.2 单线程行为（单测直调 run_one_file 的路径逐字不变）。
constexpr int kEncodeThreadsDefault = 1;

inline int encode_threads_of(const RunScope *scope) {
    return (scope != nullptr && scope->encode_threads >= 1) ? scope->encode_threads
                                                            : kEncodeThreadsDefault;
}

// W1-T6（§7.3）：合成进度的采样周期（编码器无回调 → 只能时间驱动）。
constexpr int kSynthPumpMs = 20;

// —— M10 / W1-B1（A5 配套；docs/v0.4.0-design.md §4.6 C15「无长静默」）——
// 心跳判决：静默 ≥300ms 就补一条 heartbeat=true 的 Progress 事件（不推进任何 frac）；
// 采样周期 120ms（≤500ms 判据的一半，留调度抖动余量）。覆盖三处长静默：编码长尾（libjxl
// 收尾回调停更）、probe 长段（HEIC 首次 ~1.4s）、评分期（由评分池看门狗发 scoring=true 心跳）。
constexpr int kHeartbeatTickMs = 120;
constexpr double kHeartbeatSilenceMs = 300.0;

// M8-C2（C10）/ M10-W1-T1（P0 热修）/ M10-W1-B1（T5）：评分内存口径（**评分配额**记账）。
// C1 之后指标实现已流式化（src/core/metric/metric.cpp：逐行推进 + 相邻两尺度线性帧 +
// O(width) scratch；23MP 实测 ~7.6 B/px），故这里按 16 B/px 向上取整记"指标内部缓冲"；
// 参考帧（工作缓冲共享所有权）另按 4 B/px·通道单列。二者相加 = 单个评分任务的 need_bytes。
// **不进文件像素预算**（`need = 2×frame` 保持，见 stage_budget）：配额不足 → 该输出评分
// Unavailable、产物必须成功（设计 §4.2.2(2)/§4.3 的 P0 兜底）。
constexpr std::uint64_t kMetricBytesPerPixel = 16;

// M10-W1-T2c：快速色彩归一的"高敏感区间精确重算"阈值。SSIMULACRA2 ≥ 该值且 < 100 的输出
// （质量极高/近无损，参考与失真只差 1–2 个量化级）对归一残差的放大极强（实测 1 ULP 输入扰动
// 即可让分数变化 0.003–0.08，超过 |Δ|≤0.01 判据）→ 该区间用精确 lcms 重算一次色彩归一，
// 保证与精确路径 |Δ|≤0.01；常规有损输出（<90 分，质量调优常用区间）走快速内核。
// 100.0（真无损/同输入）不触发（确定性变换必然同值）。代价：≥90 分的输出评分近似精确路径成本。
constexpr double kFastColorExactRecheckScore = 90.0;

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// W1-T6（§7.3）：合成进度泵
//   "编码器无回调"的输出（k 表内的 webp/heif/avif，ProgressMux::synth_active 为真）在编码
//   期间无法从编码器拿到采样 → 由本泵线程按 kSynthPumpMs 调 ProgressMux::tick() 推进估算。
//   * RAII：构造即起（enable=false 时不起线程），析构置停止位 + notify + join（≤ 一个采样
//     周期，且用条件变量即时唤醒 → **不给编码调用增加等待延迟**）。
//   * 线程纪律（§8.2 E3）：本线程是**采样/监控线程**，不参与编码、不占 `encode_threads`
//     预算（属 §8.2 的 ε 类，与 GUI/IO 线程同口径）；每个 worker 至多一条（同文件内
//     逐 target 串行编码），随编码返回确定性退出。
//   * 抛出的异常（含消费端回调抛出）一律吞掉：采样不得影响编码成败（§3.1 尽力而为）。
class SynthPump {
public:
    SynthPump(ProgressMux *mux, bool enable) : mux_(enable ? mux : nullptr) {
        if (mux_ == nullptr)
            return;
        th_ = std::thread([this] {
            for (;;) {
                {
                    std::unique_lock<std::mutex> lk(mu_);
                    if (cv_.wait_for(lk, std::chrono::milliseconds(kSynthPumpMs),
                                     [this] { return stop_; })) {
                        return; // stop_ == true
                    }
                }
                try {
                    mux_->tick();
                } catch (...) {
                    // 采样失败不影响编码（下一次采样继续）
                }
            }
        });
    }
    ~SynthPump() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (th_.joinable())
            th_.join();
    }
    SynthPump(const SynthPump &) = delete;
    SynthPump &operator=(const SynthPump &) = delete;

private:
    ProgressMux *mux_ = nullptr;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread th_;
};

// ============================================================================================
// M10 / W1-B1（A5 配套；docs/v0.4.0-design.md §4.6 C15）：无长静默心跳定时器
//   每 kHeartbeatTickMs 采样一次；回调里自行判决"是否真的静默"（mux->ms_since_last_emit）。
//   * RAII：构造即起线程，析构置停止位 + notify + join；
//   * 线程纪律（§8.2 E3）：监控线程，不参与编码、不占 encode_threads（ε 类）；
//   * 回调抛出的异常一律吞掉：心跳不得影响管线成败（与 SynthPump 同口径）。
// ============================================================================================
class HeartbeatTicker {
public:
    explicit HeartbeatTicker(std::function<void()> tick) : tick_(std::move(tick)), stop_(false) {
        th_ = std::thread([this] {
            for (;;) {
                {
                    std::unique_lock<std::mutex> lk(mu_);
                    if (cv_.wait_for(lk, std::chrono::milliseconds(kHeartbeatTickMs),
                                     [this] { return stop_; })) {
                        return; // stop_ == true
                    }
                }
                try {
                    if (tick_)
                        tick_();
                } catch (...) {
                    // 心跳失败不影响管线（下一次采样继续）
                    (void)0; // 显式空操作：注释不计入语句
                }
            }
        });
    }
    ~HeartbeatTicker() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (th_.joinable())
            th_.join();
    }
    // C15：显式提前停止（收尾段；析构幂等）。
    void stop() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        if (th_.joinable())
            th_.join();
    }
    HeartbeatTicker(const HeartbeatTicker &) = delete;
    HeartbeatTicker &operator=(const HeartbeatTicker &) = delete;

private:
    std::function<void()> tick_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread th_;
};

// ============================================================================================
// 0.4.2 / C6-②（M14；TSan 9 条 data race 根治）：EventFn 跨线程投递序列化（单线程 drain 队列）
//   事实（nightly tsan 栈）：run_one_file 内的事件源有 4 类线程 —— ① 文件线程（ctx.stage /
//   emit_output_done / 评分提交）；② SynthPump 泵线程（20ms → mux.tick → mux 事件回调）；
//   ③ HeartbeatTicker 心跳线程（mux.heartbeat / probe 段直发）；④ 评分池线程（异步评分完成
//   事件的 emit 是 scheduler 转发器的值拷贝）。旧 EventFn 契约未声明线程安全面 ⇒ 消费端
//   （GUI 回调 / 单测 sink）被并发回调 ⇒ `vector<FileEvent>::push_back` 与
//   `shared_ptr<OutputResult>` 拷贝数据竞争（tsan：progress.cpp emit_seq ↔ 测试回调）。
//   修法（行为零变化）：run 入口把 on_event 包成本类实例 —— 任意线程 emit 一律先入队，
//   **同一时刻只有 drainer 一个线程调用回调**（drainer 令牌 + 循环取队首）。调用回调时不持
//   锁 ⇒ 不会与 ProgressMux 的 emit_mu 形成锁序反转；回调内再次 emit（可重入）走队列尾随，
//   当前事件序列不受影响。事件不丢、内容不改、同一 run 内严格串行（FIFO）。
//   跨 run 并发不变（每个 run_one_file 各自一个实例）；跨文件事件仍由 Scheduler EvSink 的
//   mu 保护（既有设计，不改）。空回调保持为空 = 旧零成本路径逐字不变。
// ============================================================================================
class SerialEventSink {
public:
    explicit SerialEventSink(std::function<void(const FileEvent &)> fn) : fn_(std::move(fn)) {}
    SerialEventSink(const SerialEventSink &) = delete;
    SerialEventSink &operator=(const SerialEventSink &) = delete;

    void emit(const FileEvent &e) {
        std::unique_lock<std::mutex> lk(mu_);
        pending_.push_back(e);
        if (draining_)
            return; // 已有 drainer 在跑：本事件排在队尾，由它（重新持锁后）消费
        draining_ = true;
        for (;;) {
            if (pending_.empty()) {
                draining_ = false;
                return;
            }
            FileEvent next = std::move(pending_.front());
            pending_.pop_front();
            lk.unlock(); // 调用回调**不持锁**（不与 mux emit_mu 形成锁序反转）
            fn_(next);
            lk.lock();
        }
    }

private:
    std::function<void(const FileEvent &)> fn_; // 构造后只读（多线程读安全）
    std::mutex mu_;
    std::deque<FileEvent> pending_; // guard = mu_；FIFO 保序
    bool draining_ = false;         // guard = mu_：同一时刻至多一个 drainer
};

// run 入口包装：空回调 → 原样返回（零成本路径逐字不变）；非空 → 全部事件经同一个串行 sink。
std::function<void(const FileEvent &)>
serialized_event_fn(std::function<void(const FileEvent &)> fn) {
    if (!fn)
        return fn;
    auto sink = std::make_shared<SerialEventSink>(std::move(fn));
    return [sink](const FileEvent &e) { sink->emit(e); };
}

double ms_since(const Clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string fmt_double(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return buf;
}

bool is_cancelled(const std::function<bool()> &fn) { return fn && fn(); }

void merge_warnings(std::vector<Warning> &dst, const std::vector<Warning> &src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

// M2-T4 (#8): the metadata writer reports its failure/degradation messages through an explicit
// out-parameter as well. Merge them into the file warnings, dropping the messages the legacy
// plan.warnings channel already contributed (identical kind + detail) so the dual channel is
// never visible twice. New-only: nothing already in `dst` is removed or reordered.
void merge_writer_warnings(std::vector<Warning> &dst, const std::vector<std::string> &details) {
    for (const std::string &detail : details) {
        const bool duplicate = std::any_of(dst.begin(), dst.end(), [&detail](const Warning &w) {
            return w.kind == WarningKind::MetadataDropped && w.detail == detail;
        });
        if (!duplicate) {
            dst.push_back(Warning{WarningKind::MetadataDropped, detail});
        }
    }
}

// ---------------------------------------------------------------------------
// 工作缓冲（§4.1 内存纪律的载体）
// ---------------------------------------------------------------------------
// 像素存储归 pipeline 所有（vector），OIIO::ImageBuf 只做非拥有包装 —— 编码器看到的
// spec/像素与 0.2 的 decode 缓冲等价；flatten 缩通道后重新包装即可，无需第三帧。
struct WorkImage {
    OIIO::ImageSpec spec;  // 当前：float32、通道 1..4、原点 (0,0)、行列连续
    std::vector<float> px; // interleaved
    OIIO::ImageBuf buf;    // 非拥有包装（APPBUFFER）

    void rewrap() {
        buf = OIIO::ImageBuf(spec, OIIO::image_span<float>(px.data(),
                                                           static_cast<uint32_t>(spec.nchannels),
                                                           static_cast<uint32_t>(spec.width),
                                                           static_cast<uint32_t>(spec.height)));
    }
};

// src 的像素 → 工作缓冲（这是 0.2 的"decode 缓冲 + 工作缓冲"两帧中的第二次分配；
// 之后调用方可释放 decode 缓冲）。通道数变化（color 灰度升维）时工作缓冲会重新分配。
// W1-T5 复核项 1（2026-09-24）警示：本函数**先清零/覆写目的缓冲再读源**，故 `src` 绝不能与
// `img.px` 同址（同址 = 清零后读回全零）。调用方在 color 段必须先判定 ColorManager 是否替换了
// 缓冲（3/4 通道原地写回时不调用本函数 —— 见 color_stage 的颜色段注释）。
bool work_from_buf(WorkImage &img, const OIIO::ImageBuf &src, std::string &err) {
    if (!src.initialized()) {
        err = "empty image buffer";
        return false;
    }
    const OIIO::ImageSpec &s = src.spec();
    if (s.width <= 0 || s.height <= 0 || s.nchannels <= 0) {
        err = "empty image buffer";
        return false;
    }
    const std::size_t n = static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.height) *
                          static_cast<std::size_t>(s.nchannels);
    // §4.1 内存纪律（复核项 8）：扩容前先**保证**释放旧块 —— 否则 vector 扩容的瞬时双块会与
    // 调用方仍在持有的源缓冲（color 升维时是 ColorManager 新建的缓冲）叠加成 3×frame。
    if (n > img.px.size()) {
        std::vector<float>().swap(img.px);
    }
    img.px.assign(n, 0.0f);
    if (!src.get_pixels(src.roi(), OIIO::TypeDesc::FLOAT, img.px.data())) {
        err = "cannot read pixels: " + src.geterror();
        return false;
    }
    img.spec = s;
    img.spec.format = OIIO::TypeDesc::FLOAT;
    img.spec.x = 0;
    img.spec.y = 0;
    img.spec.z = 0;
    img.spec.depth = 1;
    img.spec.full_width = s.width; // 工作缓冲恒为"满幅、原点 0、行列连续"
    img.spec.full_height = s.height;
    img.spec.full_depth = 1;
    img.rewrap();
    return true;
}

// EXIF orientation 1–8 → the transform that brings the stored pixels upright.
// Mapping (EXIF 2.32；OIIO 原语按 imagebufalgo.h:499-548 核对：rotate90 = 顺时针 90°，
// transpose = 主对角线镜像 AB/CD → AC/BD，rotate180∘transpose = 副对角线镜像):
//   2 flop · 3 rotate180 · 4 flip · 5 transpose · 6 rotate90 · 7 rotate180∘transpose · 8 rotate270
// 内存纪律（§4.1；复核项 1 修订）：任何方向的瞬时峰值都是**工作缓冲 + 一份临时缓冲 = 2×frame**
//   * 单段路径（2/3/4/5/6/8）：结果落在 tmp，再拷回工作缓冲（tmp 与工作缓冲各一帧）；
//   * 两段路径（7 = transpose 后 rotate180）：第二段直接以工作缓冲为 dst 写回（src=tmp 与 dst
//     互不相同 → 无别名风险），全程仍只有一帧临时缓冲。
//   0.2 的 5/7 曾同时持有 pre + tmp（瞬时 3×frame），已按复核意见消除。
bool orient_work(WorkImage &img, int orientation, std::string &err) {
    if (orientation <= 1 || orientation > 8)
        return true;

    const int w = img.spec.width;
    const int h = img.spec.height;
    const bool swap_dims = orientation >= 5; // 5..8 的尺寸对调
    const int out_w = swap_dims ? h : w;
    const int out_h = swap_dims ? w : h;

    const auto set_dims = [&img, &out_w, &out_h](int nw, int nh) {
        img.spec.width = nw;
        img.spec.height = nh;
        img.spec.full_width = nw;
        img.spec.full_height = nh;
        img.rewrap();
    };

    OIIO::ImageBuf tmp; // 唯一临时缓冲（工作缓冲之外至多一帧）
    bool wrote_into_work = false;
    bool ok = false;
    switch (orientation) {
    case 2:
        ok = OIIO::ImageBufAlgo::flop(tmp, img.buf);
        break;
    case 3:
        ok = OIIO::ImageBufAlgo::rotate180(tmp, img.buf);
        break;
    case 4:
        ok = OIIO::ImageBufAlgo::flip(tmp, img.buf);
        break;
    case 5:
        ok = OIIO::ImageBufAlgo::transpose(tmp, img.buf); // EXIF 5 = 主对角线镜像
        break;
    case 6:
        ok = OIIO::ImageBufAlgo::rotate90(tmp, img.buf);
        break;
    case 7:
        // EXIF 7 = 副对角线镜像 = rotate180 ∘ transpose；第二段写回工作缓冲（dst=工作缓冲）
        if (OIIO::ImageBufAlgo::transpose(tmp, img.buf)) {
            set_dims(out_w, out_h);
            ok = OIIO::ImageBufAlgo::rotate180(img.buf, tmp);
            wrote_into_work = ok;
        }
        break;
    case 8:
        ok = OIIO::ImageBufAlgo::rotate270(tmp, img.buf);
        break;
    default:
        return true;
    }
    if (!ok) {
        err = "ImageBufAlgo orientation transform failed: " + img.buf.geterror();
        return false;
    }
    if (wrote_into_work)
        return true; // 像素已直接落在工作缓冲内（无拷回）

    const OIIO::ImageSpec &tspec = tmp.spec();
    const std::size_t n = static_cast<std::size_t>(tspec.width) *
                          static_cast<std::size_t>(tspec.height) *
                          static_cast<std::size_t>(tspec.nchannels);
    if (n != img.px.size()) {
        err = "orientation transform changed the pixel count";
        return false;
    }
    if (!tmp.get_pixels(tmp.roi(), OIIO::TypeDesc::FLOAT, img.px.data())) {
        err = "cannot read oriented pixels: " + tmp.geterror();
        return false;
    }
    set_dims(tspec.width, tspec.height);
    return true;
}

// Composite alpha onto a constant background (§5.5) — **原地**（§4.3 内存纪律：零额外帧）。
// 2 通道 → 1（灰），4 → 3（RGB）。通道数只减不增，dst 索引恒 ≤ src 索引，故前向写入永不
// 覆盖尚未读取的源像素（alpha 分量先读后写，同像素内亦安全）。
//
// 第二轮裁定（alpha 不支持）：OIIO 解码默认返回 associated（预乘）alpha，因此合成式是
// **预乘空间**的 `dst = src + bg*(1-a)`；不要再用直通式 `src*a + bg*(1-a)`（会对已预乘的
// RGB 二次乘 alpha）。JXL 直通源由 decode 层先补乘 alpha，保证进入本函数的缓冲是预乘的。
// 具体向量形态见 core/simd/kernels.cpp；`src == dst` 的原地缩通道由 simd 层支持。
bool flatten_alpha_in_place(WorkImage &img, float background, std::string &err) {
    const int ch = img.spec.nchannels;
    if (ch != 2 && ch != 4) {
        err = "flatten called without an alpha channel (channels=" + std::to_string(ch) + ")";
        return false;
    }
    const int out_ch = (ch == 2) ? 1 : 3;
    const std::size_t npix =
        static_cast<std::size_t>(img.spec.width) * static_cast<std::size_t>(img.spec.height);
    std::vector<float> &px = img.px;
    if (px.size() != npix * static_cast<std::size_t>(ch)) {
        err = "working buffer size mismatch before flatten";
        return false;
    }
    pp::simd::flatten(px.data(), px.data(), npix, ch, background);
    px.resize(npix * static_cast<std::size_t>(out_ch)); // 只缩容（不重分配）
    img.spec.nchannels = out_ch;
    img.spec.channelnames =
        (out_ch == 1) ? std::vector<std::string>{"Y"} : std::vector<std::string>{"R", "G", "B"};
    img.spec.alpha_channel = -1;
    img.rewrap();
    return true;
}

// R-001（P0，数据安全线）：输出路径命中源文件 → 该文件整体失败（源文件永远只读）。
// 判据：两者都在磁盘上 → fs::equivalent（符号链接/大小写/8.3 短名一并识破）；
// 任一不存在（输出尚未落盘是常态）→ 退化为词法绝对路径比较。
bool same_output_as_source(const std::filesystem::path &out, const std::filesystem::path &src) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const bool out_exists = fs::exists(out, ec);
    const bool src_exists = fs::exists(src, ec);
    if (out_exists && src_exists) {
        std::error_code eq_ec;
        if (fs::equivalent(out, src, eq_ec) && !eq_ec) {
            return true;
        }
    }
    const auto lex_abs = [](const fs::path &p) {
        std::error_code aec;
        fs::path a = fs::absolute(p, aec);
        return (aec ? p : a).lexically_normal();
    };
    return lex_abs(out) == lex_abs(src);
}

// ---------------------------------------------------------------------------
// 逐输出计划（§4.2 路径模板 + §4.4 逐输出独立冲突）
// ---------------------------------------------------------------------------
struct PlannedTarget {
    OutputTarget target; // 含冲突解析后的最终 out_path
    const FormatDef *fmt = nullptr;
    std::size_t cfg_index = 0; // 配置顺序（= FileResult.outputs 下标）
    bool skip = false;         // Skip 策略命中
    std::unique_ptr<IEncoder> enc;
    // —— M10 / W3-T4：自适应策略原文（OutputFormatSpec.adaptive_policy 的只读投影；空 = 关闭）——
    // 逐输出独立（每 (文件×输出) 一套轮循环）；不改全局项，也不参与 OutputTarget 的参数快照。
    std::string adaptive_policy;
};

// 目标构建：格式/位深校验、编码器构造、路径模板渲染、逐输出冲突解析。
// 失败 → false 且 err = 该文件的首条错误消息（与 0.2 的消息文本一致）。
// 冲突解析（§4.4，复核项 2 修订）：本文件内**逐个 target 顺序登记**已解析出的 out_path——
// 一源多输出撞同一 desired（例如同格式重复选择、或经 $format/$file 之类的模板把不同源压到
// 同一路径）时，第 2..N 个 target 走 Rename 得到独立名字，而不是沿用同一最终路径互相覆盖。
// 文件结束后的跨文件登记由 scheduler 负责（reserved 按每个 out_path 粒度）。
// R-175（M7-WP8/B.8）：PathCtx 组装收口 path_ctx_for（与 scheduler 的 desired_keys 同源）。
bool plan_targets(const FileEntry &fe, const RunConfig &cfg,
                  const std::vector<std::filesystem::path> &reserved,
                  std::vector<PlannedTarget> &out, std::vector<OutputResult> &rows,
                  std::string &err) {
    namespace fs = std::filesystem;
    out.clear();
    out.reserve(cfg.outputs.size());
    std::vector<fs::path> taken; // 本文件内逐输出累积的硬保留（尚未落盘，R-005）
    for (std::size_t i = 0; i < cfg.outputs.size(); ++i) {
        const OutputFormatSpec &spec = cfg.outputs[i];
        OutputResult &row = rows[i];
        const FormatDef *fmt = find_format(spec.format_id);
        if (!fmt) {
            err = "unknown output format '" + spec.format_id + "'";
            return false;
        }
        if (std::find(fmt->bitdepths.begin(), fmt->bitdepths.end(), spec.out_bitdepth) ==
            fmt->bitdepths.end()) {
            // §3.8 T7 ruling ①: unsupported bit depths are an explicit error, never a silent
            // downgrade (runtime probe intersection happens in the harness/UI).
            err = "output bit depth " + std::to_string(spec.out_bitdepth) +
                  " is not supported by format '" + fmt->id + "'";
            return false;
        }
        std::unique_ptr<IEncoder> enc = make_encoder(spec.format_id, spec.backend_id);
        if (!enc) {
            err = "no encoder registered for format '" + spec.format_id + "' (backend '" +
                  spec.backend_id + "')";
            return false;
        }

        const PathCtx ctx = path_ctx_for(fe, cfg, fmt);
        const fs::path desired = render_output_path(cfg.output_template, ctx, cfg.out_root);
        if (desired.empty()) {
            err = "output path: template '" + cfg.output_template + "' produced no file name";
            return false;
        }
        // R-001（P0）：渲染出的输出路径**永不**写回源文件（防"输出根=输入目录 + $dir/$file
        // 模板 + 覆盖策略"把原图替换成重编码结果）。但"命中源文件"不再整文件失败 —— 源文件
        // 占着这个名字只是一次普通的名字冲突：Rename 产出 "name (1).ext"，Skip 跳过该输出；
        // Overwrite 同样降级为 Rename（只读源文件这条数据安全线优先于冲突策略）。
        // 依据（0.3.6-alpha1 实测）：同目录把 T3.heic 转 DisplayP3，旧行为整文件报错
        // "output path collides with the source file"，同目录输出完全不可用。
        const bool source_collision = same_output_as_source(desired, fe.src);
        const ConflictPolicy effective_conflict =
            (source_collision && cfg.conflict != ConflictPolicy::Skip) ? ConflictPolicy::Rename
                                                                       : cfg.conflict;
        if (source_collision) {
            log_warn(kStage, kFile,
                     "output path collides with the source file; source is never written",
                     {{"src", fe.src.string()},
                      {"out", desired.string()},
                      {"policy", std::string(conflict_policy_name(effective_conflict))}});
        }
        // R-007（P1）：渲染后统一做合法名/长度预检（保留名/非法字符/尾随点空格/长度），
        // 不再让非法名延后到写盘时逐文件失败到底。
        std::string name_err;
        if (!validate_output_filename(desired, &name_err)) {
            err = "output path: " + name_err;
            return false;
        }
        std::string conflict_err;
        // R-005：硬保留 = 本文件更早 target 的未落盘路径；批内 reserved 为软保留（幽灵
        // 跳过防线：Skip 命中软保留而磁盘缺失时回退正常写入）。
        const OutputPlan plan =
            resolve_conflict(desired, effective_conflict, taken, reserved, conflict_err);
        if (!conflict_err.empty()) {
            err = "output path: " + conflict_err;
            return false;
        }
        if (!plan.out_path.empty()) {
            taken.push_back(plan.out_path); // 本文件后续 target 立即看到该路径已被占用
        }

        PlannedTarget pt;
        pt.fmt = fmt;
        pt.cfg_index = i;
        pt.skip = plan.skip;
        pt.enc = std::move(enc);
        pt.adaptive_policy = spec.adaptive_policy; // M10 / W3-T4（A3）：策略随计划传递
        pt.target.format_id = spec.format_id;
        pt.target.backend_id = spec.backend_id;
        pt.target.tech_id = spec.tech_id;
        pt.target.params = spec.params;
        // WEBP-02：tech=lossless 是"无损"的单一真源；即便复选框未勾，记录面也必须写
        // lossless=true（否则侧车/预设与产物相反）。反向 tech=lossy+flag 由编码器显式拒绝。
        if (fmt->id == "webp" && spec.tech_id == "lossless") {
            pt.target.params[std::string(kLosslessParamKey)] = true;
            pt.target.params[std::string(kLosslessKey)] = true;
        }
        pt.target.out_bitdepth = spec.out_bitdepth;
        pt.target.out_path = plan.out_path;
        pt.target.supports_alpha = fmt->supports_alpha;
        out.push_back(std::move(pt));
        row.out = plan.out_path;
        // JPEG-02：JPEG 没有无损模式；CLI/预设里的 lossless=true 不能静默变成"有损但记无损"。
        if (fmt->id == "jpeg" && lossless_flag(spec.params)) {
            row.warnings.push_back(Warning{
                WarningKind::LossyFromLossless,
                "jpeg has no lossless mode; the lossless flag is ignored (output is lossy)"});
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// RunFileContext + 十阶段（B.4，R-061/324）
// ---------------------------------------------------------------------------
// 单文件执行的全部可变状态的唯一载体（替代 5 个内联 lambda 的捕获面）。
// 生命周期 = run_one_file 一次调用；阶段函数按 ①..⑩ 顺序读写本结构。

// R-168：双入口共享的失败收尾（旧行为逐字：行级 error 回填 + log_error + finalize(Failed)）。
void fail_fill(FileOutcome &outcome, Clock::time_point t_start, std::string msg);

// B.4 ④：像素预算的命名 RAII（旧 BudgetGuard 匿名 struct 升格；退出路径一律自动释放）。
struct BudgetGuard {
    PixelBudget *b = nullptr;
    uint64_t bytes = 0;
    bool held = false;
    ~BudgetGuard() {
        if (held && b)
            b->release(bytes);
    }
    BudgetGuard() = default;
    BudgetGuard(const BudgetGuard &) = delete;
    BudgetGuard &operator=(const BudgetGuard &) = delete;
};

struct RunFileContext {
    RunFileContext(const FileEntry &fe_, const RunConfig &cfg_, EventFn &ev_)
        : fe(fe_), cfg(cfg_), ev(ev_) {
        res.src = fe.src;
        cancelled = (ev.scope != nullptr) ? ev.scope->cancelled : std::function<bool()>();
        reserved =
            (ev.scope != nullptr) ? ev.scope->reserved : std::vector<std::filesystem::path>{};
    }

    const FileEntry &fe;
    const RunConfig &cfg;
    EventFn &ev;
    std::function<bool()> cancelled;
    std::vector<std::filesystem::path> reserved;

    FileOutcome outcome;
    FileResult &res = outcome.file;
    Clock::time_point t_start = Clock::now();

    // W1-T6（§3.3/§7.1–§7.4）：进度枢纽。probe 之后建立（合成估算需要像素尺寸）；
    // 在此之前的事件（Queued/Probing 的首次）progress 取默认值（overall=0）。
    std::shared_ptr<ProgressMux> mux;
    // —— M10 / W1-B1（A5 配套）：无长静默心跳（C15）——
    // 声明在 mux **之后** → 析构先于 mux（线程 join 后才释放 mux；回调指向的 ctx 亦有效）。
    std::unique_ptr<HeartbeatTicker> ticker;
    // —— M10 / W3-T4（§9.6「评分期心跳」）：同步评分在飞时的心跳口径 ——
    // 自适应轮次的评分是**同步**的（本线程被 run_metric_job 占用 → 编码器/进度回调都停更），
    // 由 ctx 的心跳 ticker 补发 heartbeat=true 且 **scoring=true** 的 Progress；
    // -1 = 无评分在飞（心跳仍按源文件级发，scoring=false）。只在自适应分支置位。
    std::atomic<int> scoring_output{-1};

    // ---- 阶段间传递的事实 ----
    ImageInfo info;
    int orientation = 1;
    std::string src_icc;
    SourceMeta srcmeta;
    std::vector<PlannedTarget> targets;
    std::vector<Warning> decode_warnings;
    bool rotated = false;
    std::string icc_to_embed;
    MetadataPlan meta_plan;
    Payloads payloads;
    MetadataPayloads meta; // EncodeRequest 的载荷面（build_meta 从 payloads + icc 组装）
    // —— M10 / W1-B1（T5 异步评分）：工作缓冲改由 shared_ptr 持有 ——
    // 评分任务在**文件线程结束之后**仍需要参考帧（编码器输入 = 本缓冲）；共享所有权让
    // 评分任务把帧的生命周期延长到评分完成（内存独立记账，见 score_queue 的 need_bytes）。
    // 帧内容是只读的（评分侧只读），pipeline 侧的写回/包装在编码前已完成。
    std::shared_ptr<WorkImage> work;
    BudgetGuard budget; // ④ 的命名 RAII（acquire_budget 置位，ctx 析构释放）

    // ---- M8-C2：评分的色彩归一事实（stage_color 落位，单源）----
    //   metric_srgb=true → 工作缓冲像素 = sRGB 编码值（评分只需精确 sRGB EOTF）；
    //   false → 像素在 metric_icc 描述的空间（参考/失真走同一条 ColorManager 变换）。
    bool metric_srgb = true;
    std::string metric_icc;

    // ---- 收尾（旧 finalize/fail 两个内联 lambda 的单源化，R-168）----
    FileOutcome finalize(FileOutcome::Verdict v) {
        finalize_aggregate(outcome, t_start, v);
        return outcome;
    }
    FileOutcome fail(std::string msg) {
        fail_fill(outcome, t_start, std::move(msg));
        return outcome;
    }
    // M10 / W1-B1（T5）：异步评分任务可能在 run_one_file 返回后仍持有 mux（snapshot 只读）。
    // 析构先停心跳线程（回调捕获本 ctx），并清空 mux 的事件回调（此后 snapshot 仍安全，
    // 但绝不会有事件经已销毁的 ctx 发出去）。
    ~RunFileContext() {
        if (ticker) {
            ticker->stop();
            ticker.reset();
        }
        if (mux)
            mux->set_event_callback({});
    }

    // ---- 事件面（旧 stage/make_mux 两个内联 lambda 的单源化）----
    // 阶段事件携带当前进度快照（§3.3：progress 为追加字段，Progress 状态为其主用途；
    // 阶段事件带上快照可让消费端一次拿到"阶段文本 + 整体位置"，消费端按 state 分派）。
    void stage(FileState s, Stage st, int output_index) {
        if (!ev.on_event)
            return;
        FileEvent e{};
        e.index = 0; // 批内下标由调用方（Scheduler）回填
        e.state = s;
        e.result = nullptr;
        if (mux)
            e.progress = mux->snapshot(st, output_index);
        ev.on_event(e);
    }
    void make_mux(const ImageInfo &inf, std::size_t outputs) {
        mux = std::make_shared<ProgressMux>(inf.width, inf.height, outputs);
        mux->set_event_callback([this](const FileEvent &e) {
            if (ev.on_event)
                ev.on_event(e);
        });
        // C15：mux 就位后开心跳（静默 ≥300ms 补一条 heartbeat=true 的 Progress 事件；
        // 不推进任何 frac）。覆盖 decode/orient/color/encode 长尾/评分前的所有静默段。
        // M10 / W3-T4：自适应轮次的同步评分期（scoring_output ≥ 0）心跳带 scoring=true。
        ticker = std::make_unique<HeartbeatTicker>([this] {
            if (!mux || mux->ms_since_last_emit() < kHeartbeatSilenceMs)
                return;
            const int idx = scoring_output.load(std::memory_order_relaxed);
            mux->heartbeat(idx, /*scoring=*/idx >= 0);
        });
    }

    // —— M10 / W1-B1（A1/A2；C13）：逐输出终态事件 ——
    // 每个输出在"编码+metawrite 完成/跳过/失败/取消"时立即发一次（含该行快照，metric 未算）；
    // 评分池完成后再补发一次（含 metric，见 score_queue 的提交点）。文件终态事件仍携带完整
    // FileResult（幂等校准口径），消费端"后到覆盖先到、同值早退"。
    void emit_output_done(const OutputResult &row, int cfg_index) {
        if (!ev.on_event)
            return;
        FileEvent e{};
        e.state = FileState::OutputDone;
        if (mux)
            e.progress = mux->snapshot(Stage::Done, cfg_index);
        e.output = std::make_shared<const OutputResult>(row);
        ev.on_event(e);
    }

    // 工作缓冲里不属于单一 target 的告警（decode/color）按出现时刻推入每个未跳过行：
    // 单输出时与 0.2 的 res.warnings 序列逐字一致。
    void push_global(const std::vector<Warning> &ws) {
        for (const Warning &w : ws) {
            for (const PlannedTarget &t : targets) {
                if (!t.skip)
                    res.outputs[t.cfg_index].warnings.push_back(w);
            }
        }
    }
};

// ---- ① validate_run_config：配置硬校验（§3.2 + R-173 校验器）----
bool stage_validate(RunFileContext &ctx) {
    // 逐输出的行记录（配置顺序）；失败路径也保留行（UI/sidecar 需要可见的逐输出状态）。
    // 顺序与旧 run_one_file 逐字一致：行先建、其后才做配置硬校验。
    for (const OutputFormatSpec &spec : ctx.cfg.outputs) {
        OutputResult row;
        row.format_id = spec.format_id;
        row.backend_id = spec.backend_id;
        row.tech_id = spec.tech_id;
        ctx.res.outputs.push_back(std::move(row));
    }
    if (ctx.cfg.outputs.empty()) {
        ctx.fail("no output configured (RunConfig::outputs is empty)");
        return false;
    }
    if (ctx.cfg.metadata_only) {
        ctx.fail("run_one_file must not be called in metadata-only mode "
                 "(use run_metadata_only; RunConfig::outputs.size() must be 1 there)");
        return false;
    }
    // R-173：字段约束（模板 / 未知格式 / 位深归属 / 跨字段参数 / split_by_format 联动）
    // 收口 validate_run_config 单源（消息文本与旧逐点检查逐字一致）。
    if (const std::string cfg_err = validate_run_config(ctx.cfg); !cfg_err.empty()) {
        ctx.fail(cfg_err);
        return false;
    }
    return true;
}

// ---- ② probe_source（CICP 段整体下沉）----
bool stage_probe(RunFileContext &ctx) {
    ctx.stage(FileState::Probing, Stage::Probe, -1);
    // ---- M10 / W1-T3（A4）：复用调度器的 probe 结果，消除双重 probe ----
    // 调度器（scheduler.cpp 的 worker）已为每个文件做过一次 spec-only probe 并回填
    // `FileEntry::probe`（A4 加性字段）；同一次 run 内文件不被外部改写属既定前提（用户竞态
    // 如实记录、不处理）。只有直接调用 run_one_file 的路径（单测/bench）保留自 probe。
    // 失效/降级条件：`probe_done` 为假、或缓存带错误 → 走原路径重新 probe（行为逐字不变）。
    ProbeOutcome own_probe;
    const bool cache_hit = ctx.fe.probe_done && ctx.fe.probe.error.empty();
    const ProbeOutcome &po = cache_hit ? ctx.fe.probe : own_probe;
    if (!cache_hit) {
        // —— M10 / W1-B1（C15）：probe 长段心跳 ——
        // HEIC 首次 probe 实测 ~1.45s（OIIO/libheif 固有），而 mux 尚未建立（需要 probe 的
        // 尺寸）→ 此处直接用 ev.on_event 发心跳（progress 取默认：output_index=-1/Probe/
        // overall=0，不推进任何 frac；文件级单调性不受影响）。
        HeartbeatTicker beat([&ctx] {
            if (!ctx.ev.on_event)
                return;
            FileEvent e{};
            e.state = FileState::Progress;
            e.progress.stage = Stage::Probe;
            e.progress.heartbeat = true;
            ctx.ev.on_event(e);
        });
        own_probe = probe_file(ctx.fe.src);
    } else {
        log_debug(kStage, kFile, "probe cache hit (scheduler probe reused)",
                  {{"src", ctx.fe.src.string()},
                   {"size", std::to_string(po.info.width) + "x" + std::to_string(po.info.height)},
                   {"format", po.info.format}});
    }
    if (!po.error.empty())
        return ctx.fail("probe failed: " + po.error), false;
    ctx.info = po.info;
    ctx.res.info = ctx.info;
    // W1-T6：进度枢纽就位（§7.2 共享段从 probe 起算；§7.3 合成估算需要 pixels）
    ctx.make_mux(ctx.info, ctx.cfg.outputs.size());
    ctx.mux->shared_stage(Stage::Probe, 0.f);
    const OIIO::ImageSpec *spec = po.first_spec ? &*po.first_spec : nullptr;
    ctx.orientation = spec ? orientation_from_spec(*spec) : 1;
    ctx.src_icc = spec ? icc_from_spec(*spec) : std::string();

    // EXIF summary (time / GPS / ICC / Orientation). A read failure is NOT fatal (§4.8).
    ctx.srcmeta = read_metadata(ctx.fe.src);
    if (!ctx.srcmeta.error.empty())
        log_warn(kStage, kFile, "source metadata unavailable; continuing with empty metadata",
                 {{"src", ctx.fe.src.string()}, {"error", ctx.srcmeta.error}});
    if (ctx.src_icc.empty())
        ctx.src_icc = ctx.srcmeta.icc; // R13 chain: embedded ICC first
    // ---- D2（M7-R2A）：Exif.Photo.ColorSpace(0xA001) → 无 ICC 时的源 profile 合成 ----
    // 无嵌入 ICC 且 EXIF 显式声明 AdobeRGB（0xA001 == 2）→ 走 colormanager 的 AdobeRGB
    // profile 合成入口（load_target_icc）取 ICC 字节；=1/缺失 → 现状（sRGB 假定路径不动）。
    // 依据 CIPA DC-008：Exif.Photo.ColorSpace 声明的是像素实际所在空间 —— "无 ICC" 不再
    // 一律等于 "sRGB"。
    if (ctx.src_icc.empty()) {
        // R-313（0.3.6-alpha1）：OIIO 只在少数容器把 Exif:ColorSpace 抬进 ImageSpec；
        // JPEG/TIFF 常见"无 ICC、仅 EXIF ColorSpace=2(AdobeRGB)"的文件会因此被误判为
        // sRGB（输出色偏 + ICC 写成 sRGB）。spec 读不到时回落到 Exiv2 已读出的
        // Exif.Photo.ColorSpace（同一份源事实，不新增读取路径）。
        int exif_cs = spec != nullptr ? exif_colorspace_from_spec(*spec) : 0;
        if (exif_cs == 0) {
            const auto it = ctx.srcmeta.exif.findKey(Exiv2::ExifKey("Exif.Photo.ColorSpace"));
            if (it != ctx.srcmeta.exif.end() && it->count() > 0 && it->toInt64() == 2)
                exif_cs = 2;
        }
        if (exif_cs == 2) {
            std::string adobe_err;
            const std::string adobe = load_target_icc(ColorTarget::AdobeRGB, adobe_err);
            if (!adobe.empty()) {
                ctx.src_icc = adobe;
                log_info(kStage, kFile, "Exif ColorSpace=AdobeRGB -> synthesized AdobeRGB profile",
                         {{"src", ctx.fe.src.string()}});
            } else {
                log_warn(kStage, kFile,
                         "Exif ColorSpace=AdobeRGB but profile synthesis failed; assumed sRGB",
                         {{"src", ctx.fe.src.string()}, {"error", adobe_err}});
            }
        }
    }
    // ---- R13 middle step (issue #7, M2-T6 §2.8): CICP → source profile ----
    // See the M2-T6 landing note: OIIO always publishes a JXL ICCProfile (synthesized from
    // CICP), so CICP presence — not an empty src_icc — marks a source without its own ICC.
    if (spec != nullptr && ctx.info.channels >= 3 &&
        (ctx.info.format == "jpegxl" || ctx.info.format == "jxl")) {
        const OIIO::ParamValue *pv = spec->find_attribute("CICP");
        const bool cicp_ok = (pv != nullptr) && (pv->type().basetype == OIIO::TypeDesc::INT) &&
                             (pv->type().basevalues() * std::size_t(pv->nvalues()) >= 4);
        if (cicp_ok) {
            Cicp cicp;
            cicp.primaries = pv->get<int>(0);
            cicp.transfer = pv->get<int>(1);
            cicp.matrix = pv->get<int>(2);     // not part of the mapping (§2.8)
            cicp.full_range = pv->get<int>(3); // not part of the mapping (§2.8)
            const CicpMapping mapped = map_cicp_source(cicp);
            if (mapped.recognized()) {
                log_info(kStage, kFile, mapped.log_line, {{"src", ctx.fe.src.string()}});
            } else {
                log_warn(kStage, kFile, mapped.log_line, {{"src", ctx.fe.src.string()}});
            }
            if (!mapped.src_icc.empty())
                ctx.src_icc = mapped.src_icc;
        }
    }
    log_debug(kStage, kFile, "probe done",
              {{"src", ctx.fe.src.string()},
               {"size", std::to_string(ctx.info.width) + "x" + std::to_string(ctx.info.height)},
               {"channels", std::to_string(ctx.info.channels)},
               {"bitdepth", std::to_string(ctx.info.src_bitdepth)},
               {"orientation", std::to_string(ctx.orientation)},
               {"icc", ctx.src_icc.empty() ? "none" : std::to_string(ctx.src_icc.size()) + "B"},
               {"multipage", ctx.info.is_multipage ? "true" : "false"},
               {"outputs", std::to_string(ctx.cfg.outputs.size())}});
    return true;
}

// ---- ③ plan_targets + 全跳过早退 + 输出目录 ----
bool stage_plan(RunFileContext &ctx) {
    std::string plan_err;
    if (!plan_targets(ctx.fe, ctx.cfg, ctx.reserved, ctx.targets, ctx.res.outputs, plan_err)) {
        ctx.fail(plan_err);
        return false;
    }

    // 全跳过（Skip 策略命中每一个输出）→ 不解码、不取预算（0.2 行为）
    const bool all_skipped = std::all_of(ctx.targets.begin(), ctx.targets.end(),
                                         [](const PlannedTarget &t) { return t.skip; });
    if (all_skipped) {
        for (std::size_t i = 0; i < ctx.res.outputs.size(); ++i) {
            set_output_state(ctx.res.outputs[i], OutputState::Skipped); // R-172 单点迁移
            ctx.mux->on_output_skipped(i); // R-235：全跳过早退同样发输出级进度事件（§7.2 全段记满）
            ctx.emit_output_done(ctx.res.outputs[i], static_cast<int>(i)); // A1/A2
        }
        ctx.mux->finish();
        log_info(kStage, kFile, "skipped: output exists",
                 {{"src", ctx.fe.src.string()}, {"out", ctx.res.outputs.front().out.string()}});
        ctx.finalize(FileOutcome::Verdict::Skipped);
        return false;
    }

    // 输出目录（逐 target；0.2 在解码前创建）
    for (const PlannedTarget &pt : ctx.targets) {
        if (pt.skip)
            continue;
        std::error_code ec;
        const std::filesystem::path parent = pt.target.out_path.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);
        if (ec) {
            ctx.fail("cannot create output directory '" + parent.string() + "': " + ec.message());
            return false;
        }
    }
    return true;
}

// ---- ④ acquire_budget（2× float32 frame；BudgetGuard 命名 RAII）----
bool stage_budget(RunFileContext &ctx) {
    // 工作缓冲与 decode 缓冲同尺寸（通道数取自 probe），故 2×frame 是**文件级**峰值上界。
    // M10-W1-T1（P0 热修，docs/v0.4.0-design.md §4.3 预算表行）：评分模块的 ~160B/px 内存
    // 不再追加到文件预算 —— HEAD 的 `need = 2×frame + 160B/px×score` 让 24MP 评分批并发掉到 1
    // 且 >45MP 评分直接预算失败（P0）。评分改由进程级 ScoreQuota 独立记账（见
    // score_output_row / kMetricBytesPerPixel），配额不足只把评分降级 Unavailable，产物必须成功。
    uint64_t need_bytes =
        2 * PixelBudget::frame_bytes(ctx.info.width, ctx.info.height, ctx.info.channels);
    ctx.budget.bytes = need_bytes;
    ctx.budget.b = ctx.ev.scope != nullptr ? ctx.ev.scope->budget : nullptr;
    if (ctx.ev.scope != nullptr && ctx.ev.scope->budget != nullptr) {
        if (!ctx.ev.scope->budget->acquire(need_bytes, ctx.cancelled)) {
            if (is_cancelled(ctx.cancelled)) {
                ctx.finalize(FileOutcome::Verdict::Cancelled);
                return false;
            }
            ctx.fail("pixel budget exceeded: need " + std::to_string(need_bytes) + " B, capacity " +
                     std::to_string(ctx.ev.scope->budget->capacity()) + " B");
            return false;
        }
        ctx.budget.held = true;
        // Drumbeat evidence: the 2× frame peak reservation actually made it through the pool.
        log_info(kStage, kFile, "pixel budget acquired",
                 {{"src", ctx.fe.src.string()},
                  {"need_bytes", std::to_string(need_bytes)},
                  {"capacity_bytes", std::to_string(ctx.ev.scope->budget->capacity())},
                  {"used_bytes", std::to_string(ctx.ev.scope->budget->used())},
                  {"peak_bytes", std::to_string(ctx.ev.scope->budget->peak())}});
    }
    if (is_cancelled(ctx.cancelled)) {
        ctx.finalize(FileOutcome::Verdict::Cancelled);
        return false;
    }
    // W1-T6（§7.2 首行）：probe + acquire 完成 → 3%
    ctx.mux->shared_stage(Stage::Probe, 1.f);
    return true;
}

// ---- ⑤ decode_to_work（内存纪律点随之下沉）----
bool stage_decode(RunFileContext &ctx) {
    ctx.stage(FileState::Decoding, Stage::Decode, -1);
    ctx.mux->shared_stage(Stage::Decode, 0.f);
    Clock::time_point t0 = Clock::now();
    DecodeOutcome dec = decode_float(ctx.fe.src, ctx.info);
    ctx.res.t.decode_ms = ms_since(t0);
    if (!dec.error.empty()) {
        ctx.fail("decode failed: " + dec.error);
        return false;
    }
    std::string werr;
    // M10 / W1-B1（T5）：工作缓冲由 shared_ptr 持有（评分任务在文件线程结束后仍要读参考帧）。
    ctx.work = std::make_shared<WorkImage>();
    if (!work_from_buf(*ctx.work, dec.buf, werr)) {
        ctx.fail("decode failed: " + werr);
        return false;
    }
    // W1-T6（§7.2 decode 行）：本仓库的解码入口（src/decode/oiio_reader.h 的 decode_float）
    // **不暴露读行回调**（该文件不在 T6 文件面内 → 不改签名）→ 按 §7.2 的"无回调格式即时 1
    // 并日志注明"落到即时 1，并在此如实记一条 debug（fmt/尺寸可见）。
    ctx.mux->shared_stage(Stage::Decode, 1.f);
    log_debug(kStage, kFile, "decode progress: immediate 1 (no read-row callback exposed)",
              {{"src", ctx.fe.src.string()},
               {"format", ctx.info.format},
               {"decode_ms", fmt_double(ctx.res.t.decode_ms)}});
    // decode 告警（MultipageTruncated 等）随解码缓冲一起转移，再释出缓冲
    ctx.decode_warnings = std::move(dec.warnings);
    dec = DecodeOutcome{}; // 释放 decode 缓冲（工作缓冲已就位；峰值回到 1×frame）
    ctx.push_global(ctx.decode_warnings);
    return true;
}

// ---- ⑥ orient_stage ----
bool stage_orient(RunFileContext &ctx) {
    if (ctx.cfg.rotate_orientation && ctx.orientation != 1) {
        ctx.stage(FileState::Orienting, Stage::Orient, -1);
        ctx.mux->shared_stage(Stage::Orient, 0.f);
        const Clock::time_point t0 = Clock::now();
        std::string oerr;
        if (!orient_work(*ctx.work, ctx.orientation, oerr)) {
            ctx.fail("orient failed: " + oerr);
            return false;
        }
        ctx.res.t.orient_ms = ms_since(t0);
        ctx.rotated = true;
        ctx.mux->shared_stage(Stage::Orient, 1.f);
    } else {
        ctx.mux->shared_skip(Stage::Orient); // 不执行 → 权重不丢失（§7.2 共享段恒达 40%）
    }
    if (is_cancelled(ctx.cancelled)) {
        ctx.finalize(FileOutcome::Verdict::Cancelled);
        return false;
    }
    return true;
}

// ---- ⑦ color_stage（alpha 拍平 + 灰度/回拷判据收单点）----
bool stage_color(RunFileContext &ctx) {
    // 第二轮裁定：alpha 明确不支持。所有带 alpha 的输入在**色彩变换之前**拍平到背景，
    // 产物永远不含 alpha；这样 lcms2 只处理不透明 RGB，避免对预乘值做非线性 TRC 变换。
    // OIIO 解码默认返回 associated（预乘）alpha；JXL 直通源由 decode 层补乘（见 oiio_reader）。
    const int in_ch = ctx.work->spec.nchannels;
    if (in_ch == 2 || in_ch == 4) {
        ctx.stage(FileState::Flattening, Stage::Flatten, -1);
        ctx.mux->shared_stage(Stage::Flatten, 0.f);
        const Clock::time_point ft0 = Clock::now();
        std::string ferr;
        if (!flatten_alpha_in_place(*ctx.work, static_cast<float>(ctx.cfg.flatten_gray), ferr)) {
            ctx.fail("flatten failed: " + ferr);
            return false;
        }
        ctx.res.t.flatten_ms = ms_since(ft0);
        const std::string detail = "alpha composited onto background " +
                                   fmt_double(ctx.cfg.flatten_gray) +
                                   " (alpha is not supported; output is opaque)";
        for (const PlannedTarget &pt : ctx.targets) {
            if (!pt.skip)
                ctx.res.outputs[pt.cfg_index].warnings.push_back(
                    Warning{WarningKind::AlphaFlattened, detail});
        }
        ctx.mux->shared_stage(Stage::Flatten, 1.f);
        log_debug(kStage, kFile, "alpha flattened before color",
                  {{"src", ctx.fe.src.string()},
                   {"channels",
                    std::to_string(in_ch) + " -> " + std::to_string(ctx.work->spec.nchannels)},
                   {"background", fmt_double(ctx.cfg.flatten_gray)}});
    } else {
        ctx.mux->shared_skip(Stage::Flatten);
    }
    const bool src_is_gray = (ctx.info.channels == 1 || ctx.info.channels == 2);
    // 灰度源：任一 target 不支持灰度 → 报 GrayToRgbEncoded，并把有效目标推成 sRGB
    // （多输出下"一次变换、全体共享"，行为对单输出与 0.2 逐字一致）。
    bool any_target_needs_rgb = false;
    for (const PlannedTarget &pt : ctx.targets) {
        if (pt.skip || !src_is_gray || pt.fmt->supports_gray)
            continue;
        any_target_needs_rgb = true;
        ctx.res.outputs[pt.cfg_index].warnings.push_back(
            Warning{WarningKind::GrayToRgbEncoded,
                    "grayscale source encoded as RGB for format '" + pt.fmt->id + "'"});
    }
    ColorTarget eff_target = ctx.cfg.color;
    if (src_is_gray && any_target_needs_rgb && eff_target == ColorTarget::KeepOriginal) {
        // Grayscale pixels must not reach webp/heif/avif: use sRGB as the effective target
        // even though the user kept the original (§4.8).
        eff_target = ColorTarget::SRGB;
    }
    // 复核项 9：混合灰度支持的多输出里，color 是全局一次（D5）⇒ 支持灰度的 target 也被动
    // 升到 RGB。它自己的格式并非"不支持灰度"，故与上面的告警措辞区分开、如实补一条
    // （单输出 / 全支持灰度的组合不触发 → 0.2 语义逐字不变）。
    if (src_is_gray && any_target_needs_rgb && eff_target != ColorTarget::KeepOriginal) {
        for (const PlannedTarget &pt : ctx.targets) {
            if (pt.skip || !pt.fmt->supports_gray)
                continue;
            ctx.res.outputs[pt.cfg_index].warnings.push_back(
                Warning{WarningKind::GrayToRgbEncoded,
                        "grayscale source encoded as RGB for format '" + pt.fmt->id +
                            "' (shared color transform: another output cannot carry grayscale)"});
        }
    }
    ctx.icc_to_embed = ctx.src_icc;
    if (eff_target != ColorTarget::KeepOriginal) {
        ctx.stage(FileState::Coloring, Stage::Color, -1);
        ctx.mux->shared_stage(Stage::Color, 0.f);
        const Clock::time_point t0 = Clock::now();
        // ColorManager 的落地形态见下（可能替换 ImageBuf：灰度升维）—— 只有**被替换**时
        // 才把像素同步回工作缓冲。
        // 回拷规则（W1-T5 复核项 1 修订，2026-09-24）：只有**缓冲被替换**时才回拷。
        //   * `cbuf = work.buf` 对 APPBUFFER 是**共享存储**而非深拷（OIIO 的 ImageBuf 拷贝
        //     构造函数对 APPBUFFER 直接复用同一 bufspan），故 cbuf 的像素就是 work.px；
        //   * ColorManager 的 3/4 通道分支用 write_planes **原地**写进这块存储
        //     （src/core/colormanager.cpp:721-733），此时像素已就位 —— 若照旧回拷，
        //     work_from_buf 的第一步（assign/覆写 n 个样本）与"源"同址 → 把刚变换好的像素
        //     清零后再从同一块内存读回 → **全黑图**且 ok=true、无告警（静默数据损失）。
        //   * 灰度升维分支 `buf = make_like(...)` 换成独立新缓冲 → 地址不同 → 必须回拷。
        // 判据用"当前缓冲的本地像素地址是否仍是工作缓冲的存储"（两条路径都是本地缓冲：
        // APPBUFFER 包装 / make_like 的 LOCALBUFFER，故 localpixels() 恒非空）。
        OIIO::ImageBuf cbuf = ctx.work->buf;
        const void *const work_px = ctx.work->px.data();
        // I3 加性重载：R-032（ICC 通道不匹配降级）/R-035（灰度升维）的告警经 out_warnings
        // 输出并进逐输出行。
        // M7-WP8 跨包接口（WP1 数据面 / WP10 行为实现）：RunConfig 的 color_intent /
        // color_dither8 / gamut_mapping / linear_resample 四字段喂给 ColorOptions；默认值
        // （0/false/0/false）与旧 `ColorOptions{}` 逐字等价 → 默认路径零行为变化。
        ColorOptions copts;
        copts.intent = ctx.cfg.color_intent;
        copts.dither8 = ctx.cfg.color_dither8;
        copts.gamut_mapping = ctx.cfg.gamut_mapping;
        copts.linear_resample = ctx.cfg.linear_resample;
        std::vector<Warning> color_warnings;
        std::string color_err;
        ColorOutcome co = ColorManager::instance().transform(
            cbuf, ctx.src_icc, src_is_gray, eff_target, copts, &color_warnings, &color_err);
        ctx.res.t.color_ms = ms_since(t0);
        if (!co.error.empty()) {
            ctx.fail("color transform failed: " + co.error);
            return false;
        }
        ctx.push_global(co.warnings);
        // R-032/R-035 的降级/升维告警并入逐输出行；同 kind 已有条目时不再重复 ——
        // 灰度→不支持灰度格式时管线已按 §4.8 报过 GrayToRgbEncoded（逐格式条目），
        // 色彩段的升维告警是同义补充（golden 的 warnings_exact 按 kind 序列断言）。
        for (const Warning &cw : color_warnings) {
            for (const PlannedTarget &t : ctx.targets) {
                if (t.skip)
                    continue;
                std::vector<Warning> &roww = ctx.res.outputs[t.cfg_index].warnings;
                const bool dup = std::any_of(roww.begin(), roww.end(),
                                             [&cw](const Warning &e) { return e.kind == cw.kind; });
                if (!dup)
                    roww.push_back(cw);
            }
        }
        ctx.icc_to_embed = co.icc_to_embed;
        ctx.res.color_src = co.src_desc;
        ctx.res.color_dst = co.dst_desc;
        if (cbuf.localpixels() != work_px) {
            std::string cerr;
            if (!work_from_buf(*ctx.work, cbuf, cerr)) {
                ctx.fail("color transform failed: " + cerr);
                return false;
            }
            cbuf.clear();
        }
        ctx.mux->shared_stage(Stage::Color, 1.f);
    } else {
        // Pixels untouched: keep the source profile as-is (T4 ruling ③).
        ctx.res.color_src = ctx.src_icc.empty()
                                ? (src_is_gray ? "assumed gray sRGB" : "assumed sRGB")
                                : "ICC(source)";
        ctx.res.color_dst = "keep";
        ctx.mux->shared_skip(Stage::Color); // 不执行 → 权重不丢失（共享段收口 = 40%）
    }
    log_debug(
        kStage, kFile, "color decision",
        {{"src", ctx.res.color_src},
         {"dst", ctx.res.color_dst},
         {"embed_icc",
          ctx.icc_to_embed.empty() ? "none" : std::to_string(ctx.icc_to_embed.size()) + "B"}});
    // M8-C2（C9）：评分色彩归一的单源事实 —— 与上面像素的真实所在空间严格同源：
    //   * keep：像素未动 → sRGB 编码值仅当“无 ICC 的假定 sRGB”（含 assumed gray sRGB）；
    //     有源 ICC 时按该 ICC 描述（参考与失真用同一份）；
    //   * 非 keep：变换后像素在 eff_target 空间 → sRGB 目标只需精确 EOTF；P3/AdobeRGB 用
    //     实际嵌入的 ICC（icc_to_embed）。
    if (eff_target == ColorTarget::KeepOriginal) {
        ctx.metric_srgb = ctx.src_icc.empty();
        ctx.metric_icc = ctx.src_icc;
    } else {
        ctx.metric_srgb = (eff_target == ColorTarget::SRGB);
        ctx.metric_icc = ctx.metric_srgb ? std::string() : ctx.icc_to_embed;
    }
    return true;
}

// ---- ⑧ build_meta（全局一次，逐输出共享）----
bool stage_meta(RunFileContext &ctx) {
    ctx.meta_plan = build_plan(ctx.srcmeta, ctx.cfg.rules, ctx.fe.exception);
    if (ctx.rotated && !ctx.meta_plan.exif.empty())
        ctx.meta_plan.exif["Exif.Image.Orientation"] = static_cast<uint16_t>(1); // §5.3
    // 第二轮裁定：转码只保留 IFD0 标签 + 合法 IFD1 缩略图；删除多页页组（Image2/Image3/
    // SubImage）与 IFD0 源结构标签。metadata-only 路径**不**调用（保留多页原链）。
    sanitize_transcode_metadata(ctx.meta_plan);
    ctx.payloads = make_payloads(ctx.meta_plan);
    ctx.meta.exif_blob = ctx.payloads.exif_blob; // consumed by JXL/HEIF/AVIF only (E7)
    ctx.meta.xmp_rdf = ctx.payloads.xmp_rdf;
    ctx.meta.icc_profile = ctx.icc_to_embed;
    return true;
}

// ---------------------------------------------------------------------------
// ⑧a M8-C2：SSIMULACRA 2 评分（docs/m8-encoder-params-and-ssimulacra2-design.md C9–C13）
// ---------------------------------------------------------------------------
// 口径（C9）：
//   * 参考 = **编码器实际收到的量化后样本**：工作缓冲（目标色彩空间的编码值）逐行按目标
//     位深走与编码器相同的量化核（8bit = simd::quantize8，可选 quantize8_dithered；>8bit =
//     simd::quantize16(maxv=(1<<bps)-1)），再反归一化回 [0,1] float（scale 常量与 OIIO 解码
//     回读逐字一致：8bit × 1.0f/255、16bit × 1.0f/65535；10/12bit 按 OIIO heif 读取器的
//     bit_range_convert<bd,16> 扩张口径 —— 参考与回读在同一条归一化上）。
//   * 失真 = probe_file(row.out) + decode_float()（OIIO）回读的产物像素。
//   * 两侧经同一条色彩归一（sRGB 目标 = 精确 sRGB EOTF，无 lcms 开销；P3/AdobeRGB/keep+源
//     ICC = 同一份 ICC + ColorManager::transform(target=SRGB)）→ 送 pp::metric::compute()
//     （输入 = sRGB 线性 RGB）。逐样本相同时分数恒为 100.0。
//   * M10-W1-T2c（D-1 修正）：非 sRGB 目标的归一走 **3D CLUT 快速内核**
//     （core/fastcolor.h：lcms 预计算 33³ CLUT + 三线性插值，含 EOTF）；构建失败/非法 ICC/
//     PP_FASTCOLOR=0 → 回退下方精确 lcms 路径（分数口径不变，|Δscore|≤0.01 由单测与端到端
//     对拍钉住）。参考/失真定义、量化口径、metric 数值语义均不变。
//   * 回读失败/尺寸不符/<8×8/取消 ⇒ Unavailable（**不**失败产物、**不**产生 Warning）。
// 位置/线程（C10）：worker 线程内、单输出 encode 成功后、metawrite 前；参考逐行即时生成
// （不物化整帧），失真回读 1 帧落在既有 2×frame 预留内；取消检查在前后各一次。
// ---------------------------------------------------------------------------

// sRGB EOTF：**单源 = core/fastcolor.h 的 pp::srgb_eotf**（M10-W1-T2c）。精确路径与快速
// CLUT 路径、以及下方 srgb 快路径共用同一公式；此处不再定义本地副本（删本地定义是
// W1-T2c 的一部分，公式逐字不变）。

// 精确归一（回退路径）：一整带（rows 行）交错 RGB（目标色彩空间的**编码值**）→ sRGB 线性。
// M10 / W1-T2（§4.2.2(3)）：由"逐行调 lcms"改为"按 64 行带一次变换"（消 ImageBuf/SHA1/
// 入口等固定开销）。**像素工作量不变**：cmsDoTransform 仍是 O(像素数)，按带化不减少整帧
// 归一成本（实测 22.8MP 单次整帧 AdobeRGB→sRGB lcms ≈3.3–4.0s，与调用粒度无关；见
// build/m10/w1-engine-a/report.md §3.3）。
// M10-W1-T2c：本函数降为**回退路径**（快速 CLUT 不可用时）；快速路径见 core/fastcolor.h。
// 数值语义：lcms2 float transform 无跨像素状态，逐带一次 cmsDoTransform 与逐行调用逐样本
// 一致（单测 test_color 的 band-vs-row 对照钉住 max_err=0）。
bool rgb_rows_to_srgb_linear(float *rgb, int width, int rows, bool srgb, const std::string &icc,
                             std::string &err) {
    const std::size_t npix = static_cast<std::size_t>(width) * static_cast<std::size_t>(rows);
    if (!srgb) {
        // 与 color stage 同一条 ColorManager 变换（同一份 ICC → sRGB）；整带一次调用。
        OIIO::ImageSpec spec(width, rows, 3, OIIO::TypeDesc::FLOAT);
        OIIO::ImageBuf buf(spec, OIIO::image_span<float>(rgb, 3, static_cast<uint32_t>(width),
                                                         static_cast<uint32_t>(rows)));
        std::vector<Warning> ignored; // 评分不得把色彩降级告警塞进产物行
        std::string xerr;
        const ColorOutcome co = ColorManager::instance().transform(
            buf, icc, /*src_is_gray=*/false, ColorTarget::SRGB, ColorOptions{}, &ignored, &xerr);
        if (!co.error.empty()) {
            err = "colour normalization failed: " + co.error;
            return false;
        }
        if (buf.localpixels() != rgb && !buf.get_pixels(buf.roi(), OIIO::TypeDesc::FLOAT, rgb)) {
            err = "colour normalization readback failed";
            return false;
        }
    }
    const std::size_t n = npix * 3;
    for (std::size_t i = 0; i < n; ++i)
        rgb[i] = srgb_eotf(rgb[i]);
    return true;
}

// float 编码值 → 目标位深整数网格 → 反归一化（in-place；与编码器/解码器同一口径）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由调用点注释/命名固定
void quantize_dequantize(float *rgb, int n, int bps, int maxv, bool dither8, std::uint32_t y,
                         std::vector<std::uint8_t> &u8, std::vector<std::uint16_t> &u16) {
    const std::size_t count = static_cast<std::size_t>(n);
    if (bps == 8) {
        u8.resize(count);
        if (dither8) {
            // 编码器口径：交织行 (x0=0, y, nch=3)；TPDF 哈希只依赖 (x,y,c) ⇒ 确定性。
            pp::simd::quantize8_dithered(rgb, u8.data(), count, 0, y, 3);
        } else {
            pp::simd::quantize8(rgb, u8.data(), count);
        }
        constexpr float kScale8 = 1.0f / 255.0f;
        for (std::size_t i = 0; i < count; ++i)
            rgb[i] = static_cast<float>(u8[i]) * kScale8;
        return;
    }
    u16.resize(count);
    pp::simd::quantize16(rgb, u16.data(), count, maxv);
    if (bps == 10 || bps == 12) {
        // OIIO heif 读取器把 10/12bit 样本 bit_range_convert<bd,16> 扩张到 u16 再 /65535
        // （heifinput.cpp read_native_scanline）—— 参考侧用同一扩张口径，避免系统性偏移。
        const int shift = 16 - bps;
        const int back = 2 * bps - 16;
        constexpr float kScale16 = 1.0f / 65535.0f;
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t v = u16[i];
            rgb[i] = static_cast<float>((v << shift) | (v >> back)) * kScale16;
        }
        return;
    }
    constexpr float kScale16 = 1.0f / 65535.0f;
    for (std::size_t i = 0; i < count; ++i)
        rgb[i] = static_cast<float>(u16[i]) * kScale16;
}

// 参考行源：工作缓冲逐行 → 3 通道（灰度复制）→ 量化/反归一化（目标空间编码值）。
struct MetricReferenceRows {
    const WorkImage *work = nullptr;
    int width = 0, bps = 8, maxv = 255;
    bool dither8 = false;
    std::vector<std::uint8_t> u8;
    std::vector<std::uint16_t> u16;

    void fill_encoded(int y, float *rgb) {
        const int nch = work->spec.nchannels;
        const float *src = work->px.data() + static_cast<std::size_t>(y) *
                                                 static_cast<std::size_t>(width) *
                                                 static_cast<std::size_t>(nch);
        pp::simd::interleave(src, nch, rgb, 3, static_cast<std::size_t>(width));
        quantize_dequantize(rgb, width * 3, bps, maxv, dither8, static_cast<std::uint32_t>(y), u8,
                            u16);
    }
};

// 失真行源：回读帧逐行 → 3 通道（灰度复制；OIIO 已按 8/16bit 反归一化为 float）。
struct MetricDistortedRows {
    const OIIO::ImageBuf *buf = nullptr;
    int width = 0, nch = 1;
    std::string error;
    std::vector<float> scratch;

    bool fill_encoded(int y, float *rgb) {
        scratch.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(nch));
        const OIIO::ROI roi(0, width, y, y + 1, 0, 1, 0, nch);
        if (!buf->get_pixels(roi, OIIO::TypeDesc::FLOAT, scratch.data())) {
            error = "product pixel readback failed";
            return false;
        }
        pp::simd::interleave(scratch.data(), nch, rgb, 3, static_cast<std::size_t>(width));
        return true;
    }
};

// 行源 → sRGB 线性行（metric::compute 的 RowFn 面）。sRGB 路径逐行即时 EOTF；
// 非 sRGB 路径按 64 行带缓存 + 整带一次归一：
//   * M10-W1-T2c：`fast` 非空 → 3D CLUT 三线性插值（core/fastcolor.h，快路径，输出已含 EOTF）；
//   * 否则 → 精确 lcms 回退（rgb_rows_to_srgb_linear，M10/W1-T2 的按带化，数值语义逐样本
//     等同 HEAD 逐行路径）。
// color_ms 累计"色彩归一"净耗时（fast/exact 都计），供 score_output_row 打点分解与验收。
struct MetricLinearRows {
    static constexpr int kBandRows = 64;
    int width = 0, height = 0;
    bool srgb = true;
    std::string icc;
    std::function<bool(int y, float *rgb)> encoded;
    std::vector<float> band;
    int band_y0 = -1, band_h = 0;
    std::string error;
    // —— M10-W1-T2c：快速归一内核（由 score_output_row 注入；两侧共享同一实例）——
    std::shared_ptr<const FastSrgbLinear> fast;
    double color_ms = 0.0; // 色彩归一净耗时（证据/日志用；不影响评分语义）
    int color_bands = 0;   // 已归一化带数
    bool used_fast = false;

    void row(int y, float *out) {
        const std::size_t n = static_cast<std::size_t>(width) * 3;
        if (!error.empty() || y < 0 || y >= height) { // 防御：契约要求 y ∈ [0,height)
            std::fill_n(out, n, 0.0f);
            return;
        }
        if (srgb) {
            if (!encoded(y, out)) {
                error = "row source failed";
                std::fill_n(out, n, 0.0f);
                return;
            }
            for (std::size_t i = 0; i < n; ++i)
                out[i] = srgb_eotf(out[i]);
            return;
        }
        if (band_y0 < 0 || y < band_y0 || y >= band_y0 + band_h) {
            band_y0 = (y / kBandRows) * kBandRows;
            band_h = std::min(kBandRows, height - band_y0);
            band.resize(n * static_cast<std::size_t>(band_h));
            for (int r = 0; r < band_h; ++r) {
                float *br = band.data() + n * static_cast<std::size_t>(r);
                if (!encoded(band_y0 + r, br)) {
                    error = "row source failed";
                    std::fill_n(out, n, 0.0f);
                    return;
                }
            }
            const Clock::time_point c0 = Clock::now();
            if (fast && fast->ready()) {
                // M10-W1-T2c 快路径：CLUT + 三线性插值（一次调用处理整带；含 EOTF）。
                fast->transform_rows(band.data(), width, band_h);
                used_fast = true;
            } else if (!rgb_rows_to_srgb_linear(band.data(), width, band_h, /*srgb=*/false, icc,
                                                error)) {
                if (error.empty())
                    error = "row source failed";
                std::fill_n(out, n, 0.0f);
                return;
            }
            color_ms += ms_since(c0);
            ++color_bands;
        }
        std::copy_n(band.data() + n * static_cast<std::size_t>(y - band_y0), n, out);
    }
};

// ============================================================================================
// M10 / W1-B1（T5；docs/v0.4.0-design.md §4.2.2(2) / C3 / C14）：评分任务面
//   把"单输出评分"从文件线程里剥离成**值语义任务**：参考帧用 shared_ptr 共享（文件线程结束
//   后仍有效；评分侧只读），产物路径/位深/归一事实全部拷贝进来 —— 任务不再触碰 RunFileContext。
// ============================================================================================
struct MetricJob {
    std::filesystem::path src; // 参考源（日志/证据）
    std::filesystem::path out; // 产物路径（失真侧回读）
    std::string format_id;
    std::shared_ptr<const WorkImage> work; // 参考帧（float32 工作缓冲 = 编码器输入）
    int width = 0, height = 0;
    int bps = 8;          // 输出位深（bitdepth_bps 单源的结果）
    bool dither8 = false; // 参考侧 8bit 抖动（与编码器同口径）
    bool metric_srgb = true;
    std::string metric_icc;
    std::function<bool()> cancelled;
};

// 单输出评分体（T5 重构：从 RunFileContext 脱离，可在评分池线程执行）：
// 失败只落 MetricState（不失败产物、不产生 Warning）。前提 = 该行已编码+metawrite 成功。
void run_metric_job(const MetricJob &job, OutputResult &row) {
    const Clock::time_point t0 = Clock::now();
    const auto unavailable = [&row, t0](std::string note) {
        row.metric_state = MetricState::Unavailable;
        row.metric_ssimulacra2 = -1.0;
        row.metric_note = std::move(note);
        row.metric_ms = ms_since(t0);
    };
    const auto failed = [&row, t0](std::string note) {
        row.metric_state = MetricState::Failed;
        row.metric_ssimulacra2 = -1.0;
        row.metric_note = std::move(note);
        row.metric_ms = ms_since(t0);
    };
    const auto finish = [&]() {
        const char *state = row.metric_state == MetricState::Ok            ? "ok"
                            : row.metric_state == MetricState::Unavailable ? "unavailable"
                            : row.metric_state == MetricState::Failed      ? "failed"
                                                                           : "off";
        const std::string score = row.metric_state == MetricState::Ok
                                      ? fmt_double(row.metric_ssimulacra2)
                                      : std::string();
        log_info(kStage, kFile, "quality metric",
                 {{"src", job.src.string()},
                  {"out", row.out.string()},
                  {"format", row.format_id},
                  {"state", state},
                  {"score", score},
                  {"ms", fmt_double(row.metric_ms)},
                  {"note", row.metric_note}});
    };

    const int width = job.width;
    const int height = job.height;
    if (is_cancelled(job.cancelled)) {
        unavailable("运行取消，评分未完成");
        finish();
        return;
    }
    if (width < 8 || height < 8) {
        unavailable("图像尺寸小于 8×8");
        finish();
        return;
    }
    // ---- 失真回读（C10：probe_file + decode_float；失败/尺寸不符 → Unavailable）----
    const Clock::time_point t_prod0 = Clock::now();
    const ProbeOutcome po = probe_file(row.out);
    if (!po.error.empty() || !po.first_spec) {
        unavailable("产物回读失败");
        finish();
        return;
    }
    if (po.info.width != width || po.info.height != height || po.info.channels < 1 ||
        po.info.channels > 4) {
        unavailable("产物尺寸或通道不符");
        finish();
        return;
    }
    DecodeOutcome dec = decode_float(row.out, po.info);
    if (!dec.error.empty()) {
        unavailable("产物解码失败");
        finish();
        return;
    }
    const OIIO::ImageSpec &dspec = dec.buf.spec();
    if (dspec.width != width || dspec.height != height || dspec.nchannels < 1 ||
        dspec.nchannels > 4) {
        unavailable("产物尺寸或通道不符");
        finish();
        return;
    }
    // M10-W1-T2c：评分耗时的**分解打点**（产物回读 / 指标核心 / 色彩归一；验收判据用）。
    const double product_ms = ms_since(t_prod0);
    // ---- 参考量化口径（C9；bmp 的 bpp 口径由 bitdepth_bps 单源换算）----
    const int bps = job.bps; // 由提交点单源算好（bitdepth_bps × target）
    if (bps < 8 || bps > 16) {
        unavailable("输出位深不支持评分");
        finish();
        return;
    }
    const int maxv = (bps >= 16) ? 65535 : ((1 << bps) - 1);
    const bool dither8 = job.dither8; // 提交点单源判定（含 heif/avif 不抖动口径）
    // ---- M10-W1-T2c（D-1 修正）：快速色彩归一刀 ----
    // 非 sRGB 目标（keep+源 ICC / 宽色域目标）才需要颜色空间归一；sRGB 目标走精确 EOTF。
    // 每输出取一次进程级缓存（同批同 ICC 命中复用）；构建失败/非法 ICC/PP_FASTCOLOR=0 →
    // nullptr → MetricLinearRows 回退精确 lcms（评分参考/失真定义与分数语义不变）。
    std::shared_ptr<const FastSrgbLinear> fast_lut;
    if (!job.metric_srgb)
        fast_lut = FastSrgbLinear::get_shared(job.metric_icc);
    MetricReferenceRows ref_rows;
    ref_rows.work = job.work.get();
    ref_rows.width = width;
    ref_rows.bps = bps;
    ref_rows.maxv = maxv;
    ref_rows.dither8 = dither8;
    MetricDistortedRows dist_rows;
    dist_rows.buf = &dec.buf;
    dist_rows.width = width;
    dist_rows.nch = dspec.nchannels;
    MetricLinearRows ref_lin;
    ref_lin.width = width;
    ref_lin.height = height;
    ref_lin.srgb = job.metric_srgb;
    ref_lin.icc = job.metric_icc;
    ref_lin.fast = fast_lut; // M10-W1-T2c：快路径（nullptr = 精确 lcms 回退）
    ref_lin.encoded = [&ref_rows](int y, float *rgb) {
        ref_rows.fill_encoded(y, rgb);
        return true;
    };
    MetricLinearRows dist_lin;
    dist_lin.width = width;
    dist_lin.height = height;
    dist_lin.srgb = job.metric_srgb;
    dist_lin.icc = job.metric_icc;
    dist_lin.fast = fast_lut; // 与参考侧共享同一不可变 CLUT（构建一次/两侧复用）
    dist_lin.encoded = [&dist_rows](int y, float *rgb) { return dist_rows.fill_encoded(y, rgb); };
    if (is_cancelled(job.cancelled)) {
        unavailable("运行取消，评分未完成");
        finish();
        return;
    }
    const pp::metric::FrameSource ref_src{width, height,
                                          [&ref_lin](int y, float *rgb) { ref_lin.row(y, rgb); }};
    const pp::metric::FrameSource dist_src{
        width, height, [&dist_lin](int y, float *rgb) { dist_lin.row(y, rgb); }};
    pp::metric::Result mr;
    const Clock::time_point t_comp0 = Clock::now();
    try {
        mr = pp::metric::compute(ref_src, dist_src);
    } catch (const std::exception &e) {
        failed(std::string("指标内部错误: ") + e.what());
        finish();
        return;
    } catch (...) {
        failed("指标内部错误");
        finish();
        return;
    }
    const double compute_ms = ms_since(t_comp0);
    // ---- M10-W1-T2c：高敏感区间精确重算兜底 ----
    // 快速内核的归一残差 ≈2 ULP；SSIMULACRA2 对 1 ULP 输入扰动的响应可达 0.003–0.08（随内容
    // 敏感度；实测噪声地板），故"高分输出（≥kFastColorExactRecheckScore）"用精确 lcms 重算
    // 一次色彩归一以保证 |Δ|≤0.01（成本只出现在高分输出；真无损 = 100.0 不触发 —— 同输入经
    // 同一确定性变换必然同值）。
    bool fast_used = ref_lin.used_fast || dist_lin.used_fast;
    bool exact_recheck = false;
    double recheck_ms = 0.0;
    double total_color_ms = ref_lin.color_ms + dist_lin.color_ms;
    if (fast_used && mr.state == pp::metric::State::Ok && mr.score >= kFastColorExactRecheckScore &&
        mr.score < 100.0) {
        MetricLinearRows ref_exact;
        ref_exact.width = width;
        ref_exact.height = height;
        ref_exact.srgb = job.metric_srgb;
        ref_exact.icc = job.metric_icc;
        ref_exact.fast = nullptr; // 精确 lcms 回退路径
        ref_exact.encoded = ref_lin.encoded;
        MetricLinearRows dist_exact;
        dist_exact.width = width;
        dist_exact.height = height;
        dist_exact.srgb = job.metric_srgb;
        dist_exact.icc = job.metric_icc;
        dist_exact.fast = nullptr;
        dist_exact.encoded = dist_lin.encoded;
        const Clock::time_point t_rc0 = Clock::now();
        try {
            const pp::metric::FrameSource ref_src2{
                width, height, [&ref_exact](int y, float *rgb) { ref_exact.row(y, rgb); }};
            const pp::metric::FrameSource dist_src2{
                width, height, [&dist_exact](int y, float *rgb) { dist_exact.row(y, rgb); }};
            const pp::metric::Result mr_exact = pp::metric::compute(ref_src2, dist_src2);
            if (mr_exact.state == pp::metric::State::Ok) {
                mr = mr_exact;
                exact_recheck = true;
                total_color_ms += ref_exact.color_ms + dist_exact.color_ms;
            }
        } catch (...) {
            // 重算失败不覆盖已得到的快速结果（评分尽力而为；不失败产物）。
            (void)0; // 显式空操作：注释不计入语句
        }
        recheck_ms = ms_since(t_rc0);
    }
    row.metric_ms = ms_since(t0); // 端到端评分耗时（回读 + 归一 + 指标核心）
    // M10-W1-T2c 验收打点：评分耗时分解 + 路径证据（仅评分运行出现，不进产物/侧车）。
    // metric_ms = product_ms（产物 probe+decode）+ compute_ms（快速指标计算，含 color_ms）
    //             + recheck_ms（高分输出的精确重算；无则 0）+ 少量杂项。
    const double color_fast_ms = ref_lin.color_ms + dist_lin.color_ms;
    log_debug(kStage, kFile, "quality metric colour normalization",
              {{"src", job.src.string()},
               {"out", row.out.string()},
               {"mode", !fast_used ? (job.metric_srgb ? "srgb-eotf" : "exact-lcms")
                                   : (exact_recheck ? "fast-clut+exact-recheck" : "fast-clut")},
               {"color_ms", fmt_double(total_color_ms)},
               {"product_ms", fmt_double(product_ms)},
               {"compute_ms", fmt_double(compute_ms)},
               {"core_ms", fmt_double(compute_ms - color_fast_ms)},
               {"recheck_ms", fmt_double(recheck_ms)},
               {"metric_ms", fmt_double(row.metric_ms)},
               {"bands", std::to_string(ref_lin.color_bands + dist_lin.color_bands)},
               {"recheck", exact_recheck ? "true" : "false"},
               {"engine", fast_lut ? std::string(fast_lut->engine()) : std::string("exact")}});
    if (!ref_lin.error.empty() || !dist_lin.error.empty()) {
        unavailable("色彩归一失败");
        finish();
        return;
    }
    if (is_cancelled(job.cancelled)) {
        unavailable("运行取消，评分未完成");
        finish();
        return;
    }
    switch (mr.state) {
    case pp::metric::State::Ok:
        if (!std::isfinite(mr.score)) {
            failed("指标返回非法分数");
            break;
        }
        row.metric_state = MetricState::Ok;
        row.metric_ssimulacra2 = mr.score;
        row.metric_note.clear();
        break;
    case pp::metric::State::Unavailable:
        row.metric_state = MetricState::Unavailable;
        row.metric_note = mr.note.empty() ? std::string("产物回读不可用") : mr.note;
        break;
    case pp::metric::State::Failed:
        row.metric_state = MetricState::Failed;
        row.metric_note = mr.note.empty() ? std::string("指标内部错误") : mr.note;
        break;
    case pp::metric::State::Off:
        row.metric_state = MetricState::Off;
        break;
    }
    finish();
}

// ---- T5：评分任务提交（异步；不阻塞文件线程）----
// 顺序契约（C3）：encode → metawrite（落盘）→ OutputDone（不含 metric）→ **提交评分任务**；
// 评分完成的第二次 OutputDone 由池内线程发出（含 metric，scoring=false）。
// 内存：need_bytes = 指标内部缓冲（kMetricBytesPerPixel）+ 参考帧（4 B/px·通道，共享所有权）。
// 配额：池容量装不下 → submit_ex 返回 QuotaExceeded → 该输出 Unavailable（文件仍成功）。
void submit_score_task(RunFileContext &ctx, const PlannedTarget &pt, OutputResult &row) {
    if (!ctx.cfg.quality_score || pt.skip || !ctx.work)
        return; // MetricState::Off = 默认（--dev 不带 --score；仅元数据模式无本入口）
    MetricJob job;
    job.src = ctx.fe.src;
    job.out = row.out;
    job.format_id = row.format_id;
    job.work = ctx.work; // shared_ptr<WorkImage> → shared_ptr<const WorkImage>（只读共享）
    job.width = ctx.work->spec.width;
    job.height = ctx.work->spec.height;
    // 位深/抖动口径单源：bps 由 bitdepth_bps(fmt, target 位深) 换算（bmp 的 bpp 口径同源）；
    // dither8 只在 8bit 量化段生效，heif/avif 的 8bit 走 quantize16 不抖动（既有口径）。
    job.bps = bitdepth_bps(row.format_id, pt.target.out_bitdepth);
    job.dither8 =
        ctx.cfg.color_dither8 && job.bps == 8 && row.format_id != "heif" && row.format_id != "avif";
    job.metric_srgb = ctx.metric_srgb;
    job.metric_icc = ctx.metric_icc;
    // M10-SCOREFIX（所有权）：取消谓词只绑定**本次运行的评分代次令牌**（shared_ptr，原子
    // 只读），不再拷贝 ctx.cancelled —— 后者是 Scheduler 的 `[Impl*]{...}` 值拷贝，评分任务
    // 跨文件线程存活时会随调度器析构而悬垂。用户取消与运行结束都由 pool 翻转该令牌。
    ScorePool &pool = ScorePool::instance();
    const ScoreRunPtr run_state = pool.current_run();
    job.cancelled = [run_state] { return run_state->cancelled(); };

    const std::uint64_t px =
        static_cast<std::uint64_t>(job.width) * static_cast<std::uint64_t>(job.height);
    const std::uint64_t frame_bytes =
        px * static_cast<std::uint64_t>(ctx.work->spec.nchannels) * 4ull;
    const std::uint64_t need_bytes = kMetricBytesPerPixel * px + frame_bytes;

    // 事件面（池内线程用）：emit 为 scheduler 的转发器**值拷贝**（不持有 RunScope）；
    // mux 共享所有权保活（snapshot 只读；ctx 析构时已清空其回调）。
    const std::shared_ptr<ProgressMux> mux = ctx.mux;
    const auto emit = ctx.ev.on_event;
    const int cfg_index = static_cast<int>(pt.cfg_index);
    auto row_ptr = std::make_shared<OutputResult>(row);

    ScoreTask task;
    task.out = row.out.string();
    task.need_bytes = need_bytes;
    task.result = row_ptr;
    task.run_state = run_state; // M10-SCOREFIX：任务绑定本次运行的代次（取消/丢弃按代次判定）
    task.run = [job, row_ptr, mux, emit, cfg_index]() {
        try {
            run_metric_job(job, *row_ptr);
        } catch (const std::exception &e) {
            row_ptr->metric_state = MetricState::Failed;
            row_ptr->metric_ssimulacra2 = -1.0;
            row_ptr->metric_note = std::string("指标内部错误: ") + e.what();
        } catch (...) {
            row_ptr->metric_state = MetricState::Failed;
            row_ptr->metric_ssimulacra2 = -1.0;
            row_ptr->metric_note = "指标内部错误";
        }
        if (!emit)
            return;
        FileEvent e{};
        e.state = FileState::OutputDone;
        if (mux)
            e.progress = mux->snapshot(Stage::Done, cfg_index);
        e.progress.scoring = false;
        e.output = std::make_shared<const OutputResult>(*row_ptr);
        emit(e);
    };
    task.heartbeat = [mux, emit, cfg_index]() {
        if (!emit)
            return;
        FileEvent e{};
        e.state = FileState::Progress;
        if (mux)
            e.progress = mux->snapshot(Stage::MetaWrite, cfg_index);
        e.progress.heartbeat = true;
        e.progress.scoring = true;
        emit(e);
    };

    // M10-SCOREFIX：提交结果三分类（配额不足 / 运行取消 / 池关闭）→ 各自准确的中文 note。
    // 旧实现只有 bool，任何拒绝都写"内存不足"（用户取消后误导）。
    const ScorePool::SubmitStatus status = pool.submit_ex(std::move(task));
    if (status != ScorePool::SubmitStatus::Accepted) {
        // P0 兜底（设计 §4.2.2(2)）：评分不可用 → 该输出 Unavailable，**文件仍成功**。
        // 行留在文件结果里（终态事件可见该 Unavailable 口径）。
        row.metric_state = MetricState::Unavailable;
        row.metric_ssimulacra2 = -1.0;
        row.metric_ms = 0.0;
        switch (status) {
        case ScorePool::SubmitStatus::QuotaExceeded:
            // 只有"配额不足"这一类才写内存不足（P0 兜底的真实原因）。
            row.metric_note = "内存不足，已跳过评分（需要 " + std::to_string(need_bytes) +
                              " B，评分配额 " + std::to_string(pool.capacity()) + " B）";
            break;
        case ScorePool::SubmitStatus::RunCancelled:
            // 运行取消：绝不再写"内存不足"（取消发生在配额充足时同样成立）。
            row.metric_note = "运行取消，评分未完成";
            break;
        case ScorePool::SubmitStatus::PoolStopping:
            row.metric_note = "评分池已关闭，评分未完成";
            break;
        case ScorePool::SubmitStatus::InvalidTask:
        case ScorePool::SubmitStatus::Accepted:
            row.metric_note = "评分任务无效，评分未完成";
            break;
        }
        log_info(kStage, kFile, "quality metric",
                 {{"src", ctx.fe.src.string()},
                  {"out", row.out.string()},
                  {"format", row.format_id},
                  {"state", "unavailable"},
                  {"score", ""},
                  {"ms", "0"},
                  {"note", row.metric_note},
                  {"submit_status", ScorePool::submit_status_name(status)}});
        return;
    }
    log_debug(kStage, kFile, "score task submitted",
              {{"src", ctx.fe.src.string()},
               {"out", row.out.string()},
               {"need_bytes", std::to_string(need_bytes)},
               {"quota_bytes", std::to_string(pool.capacity())},
               {"workers", std::to_string(pool.workers())},
               {"generation", std::to_string(run_state->generation())}});
}

// ============================================================================================
// M10 / W1-B1（T6；docs/v0.4.0-design.md §4.4 / C5）：同文件多格式并行编码
//   模型：共享段（probe/decode/orient/flatten/color/meta）一次 → 逐 target 的
//   encode+metawrite 可在**独立线程**执行（每 task 独立 encoder 实例 + 独立 EncodeRequest，
//   共享只读工作缓冲）→ 完成后提交异步评分。
//   * 并行度 = RunConfig::max_parallel_targets（0 = 自动 → min(2, outputs.size())；A3）；
//   * 飞行缓冲：每并行任务在**文件预算之外**再占一份按格式/位深估算的 token（§4.4.3）；
//     预算装不下 → 整个文件退回串行（不失败，只慢）；
//   * 失败隔离/聚合终态沿用 verdict_of_rows；输出行序恒为配置顺序（res.outputs 按 cfg_index
//     写入，与执行顺序无关）；reserved 仍在文件终态后登记（scheduler 侧，不变）；
//   * 取消：任一 task 见取消 → 置 cancel_hit，后续 task 不再启动（不再处理剩余 target）。
// ============================================================================================
enum class TargetOutcome : std::uint8_t { Ok, Failed, Cancelled, Skipped };

// ============================================================================================
// M10 / W3-T4（docs/v0.4.0-design.md §9.6）：自适应输出（策略非空）的轮循环
//   与 §9.4/§9.5/§9.8 逐条对应；执行器本体的算法/停止条件/候选三件套在 core/adaptive_exec，
//   本段只负责**接线**：真编码器、同步评分（复用 run_metric_job 单源）、进度/日志/侧车与
//   既有 metawrite/mtime 段。
//   非自适应路径（policy 空）不进入本段 → 逐字节零变化（回归 zero diff 的结构性保证）。
// ============================================================================================

// MetricState → 词表（日志/进度用；与 dev_cli 的 sidecar 映射同源 = pp::metric::state_name）。
const char *metric_state_text(MetricState s) {
    switch (s) {
    case MetricState::Off:
        return pp::metric::state_name(pp::metric::State::Off);
    case MetricState::Ok:
        return pp::metric::state_name(pp::metric::State::Ok);
    case MetricState::Unavailable:
        return pp::metric::state_name(pp::metric::State::Unavailable);
    case MetricState::Failed:
        return pp::metric::state_name(pp::metric::State::Failed);
    }
    return "off";
}

// 策略校验错误 → 用户可见文本（带行号/列号/错误码/建议；§9.2 的定位口径）。
std::string adaptive_error_text(const AdaptiveError &e) {
    std::string out;
    if (e.line > 0) {
        out = "第 " + std::to_string(e.line) + " 行第 " + std::to_string(e.col) + " 列：";
    }
    out += e.message;
    if (!e.code.empty())
        out += "（" + e.code + "）";
    if (!e.hint.empty())
        out += "；建议：" + e.hint;
    return out;
}

// ============================================================================================
// 需求 1（自适应迭代优化过程写入日志）：pipeline 侧日志**渲染**辅助（数据面在执行器）
//   * adaptive_params_brief：ParamSet → "k=v,k=v"（白名单键，**只输出实际存在的键**；基准配置
//     与逐轮有效参数共用同一渲染口径，日志可逐字比较）；丢失基线的空白不会伪造默认值；
//   * adaptive_decision_reason：执行器的稳定英文 decision 词表 → 中文一句话（decision_reason）；
//   * join_text：stage 列表/分数序列之类的小拼接（空容器 → 空串）。
// ============================================================================================
std::string adaptive_param_value_text(const ParamValue &value) {
    if (const auto *i = std::get_if<int64_t>(&value))
        return std::to_string(*i);
    if (const auto *d = std::get_if<double>(&value))
        return fmt_double(*d);
    if (const auto *b = std::get_if<bool>(&value))
        return *b ? "true" : "false";
    if (const auto *s = std::get_if<std::string>(&value))
        return *s;
    return {};
}

std::string adaptive_params_brief(const ParamSet &params) {
    // 白名单 = 质量/搜索目标键 + 结构性键 + 编码档位保留键（实际存在的才出）。
    // 顺序固定（日志可逐行 diff）；`__heif_profile` 渲染成 `profile`（人类可读别名）。
    static const char *const kKeys[] = {"quality_mode", "quality", "x265:crf", "x265:qp",
                                        "aom:cq-level", "qp",      "distance", "effort",
                                        "chroma",       "preset",  "tune",     "lossless"};
    std::string out;
    const auto append = [&out](std::string_view key, const ParamValue &value) {
        if (!out.empty())
            out += ",";
        out += key;
        out += "=";
        out += adaptive_param_value_text(value);
    };
    for (const char *key : kKeys) {
        const auto it = params.find(std::string(key));
        if (it != params.end())
            append(key, it->second);
    }
    // HEIF/AVIF 编码档位（保留键；日志用短名 profile）
    const auto profile_it = params.find(std::string(kHeifProfileParamKey));
    if (profile_it != params.end())
        append("profile", profile_it->second);
    return out;
}

std::string join_text(const std::vector<std::string> &items, char sep) {
    std::string out;
    for (const std::string &item : items) {
        if (!out.empty())
            out += sep;
        out += item;
    }
    return out;
}

// 逐轮决策原因（中文；执行器 decision 词表见 core/adaptive_exec.h）。
// 词表之外的取值原样透出（不编造新语义）；评分不可用/编码失败带执行器给的中文 note。
std::string adaptive_decision_reason(const AdaptiveRoundRecord &rec, double target) {
    const bool scored = rec.metric == MetricState::Ok;
    const std::string score = scored ? fmt_double(rec.score) : std::string();
    if (rec.decision == "met") {
        return scored ? ("达标：" + score + " ≥ 目标 " + fmt_double(target)) : "达标";
    }
    if (rec.decision == "improve") {
        return scored ? ("改善：" + score) : "改善（评分不可用）";
    }
    if (rec.decision == "no-improve") {
        return scored ? ("未改善：" + score + " 低于目标 " + fmt_double(target))
                      : "未改善（评分不可用）";
    }
    if (rec.decision == "unscored") {
        return rec.note.empty() ? std::string("评分不可用") : ("评分不可用：" + rec.note);
    }
    if (rec.decision == "encode-failed") {
        return rec.note.empty() ? std::string("编码失败") : ("编码失败：" + rec.note);
    }
    return rec.decision;
}

// ---- 逐输出 metawrite 段（encode 成功后；单次编码与自适应轮循环共用同一条写路径）----
// 语义逐字搬移自 W1 的 run_target（顺序/日志/告警/终态单点不变）。返回 false = 该输出失败
// （行已置 Failed、OutputDone 已发）。
bool run_target_metawrite(RunFileContext &ctx, const PlannedTarget &pt, OutputResult &row) {
    // ---- metadata write (§5.8, §4.8) ----
    ctx.stage(FileState::Writing, Stage::MetaWrite, static_cast<int>(pt.cfg_index));
    ctx.mux->output_stage(pt.cfg_index, Stage::MetaWrite);
    Clock::time_point t0 = Clock::now();
    std::string meta_err;
    // M2-T4 (#6/#8): the writer's explicit failure/degradation channel. R-171 起写路径
    // 告警**只**走本通道（plan.warnings 归 build_plan，写路径不再回写 const plan）。
    std::vector<std::string> writer_warnings;
    if (pt.fmt->meta_path == "exiv2") {
        meta_err = write_metadata_exiv2(row.out, ctx.meta_plan, ctx.payloads, &writer_warnings);
    }
    merge_warnings(row.warnings, ctx.meta_plan.warnings); // build_plan 通道（frozen content）
    merge_writer_warnings(row.warnings, writer_warnings);
    row.t.metawrite_ms = ms_since(t0);
    if (!meta_err.empty()) {
        // R-002（P0）：写回失败 = 该输出真失败（不再吞成 warning + ok=true）。
        // 写回已是临时文件 + rename（metadata.cpp），失败时磁盘上是编码产物原样
        // （无截断半成品；编码产物不删除 —— 删除用户数据比保留像素更危险，裁定见
        // handoff），此处如实记失败行让上层可见。
        row.error = "metadata write failed: " + meta_err;
        row.error_kind = ErrorKind::MetaWrite;
        set_output_state(row, OutputState::Failed);
        log_error(kStage, kFile, row.error,
                  {{"src", ctx.fe.src.string()}, {"out", row.out.string()}});
        ctx.mux->on_output_failed(pt.cfg_index);
        ctx.emit_output_done(row, static_cast<int>(pt.cfg_index)); // A1/A2（失败行终态）
        return false;
    }
    if (pt.fmt->meta_path == "none") {
        // BMP has no metadata container; T5 skips silently, the pipeline reports it (§4.8).
        row.warnings.push_back(
            Warning{WarningKind::MetadataDropped, "bmp output carries no metadata container"});
        // B3（UX-14 B3 余量 / E2，M7-R2A）：OIIO BMP writer 不落 ICC（V5 头 profile 槽
        // 未写，测试钉住事实见 test_enc_oiio 的 ICC 回读）—— 只要本次有 ICC 要嵌
        // （keep 的源 ICC 或非 keep 的目标 ICC），BMP 都无法承载，必须显式警示。
        if (!ctx.icc_to_embed.empty()) {
            row.warnings.push_back(
                Warning{WarningKind::EmbedIccUnsupported,
                        "bmp output cannot embed the ICC profile (color target " +
                            to_string(ctx.cfg.color) + ")"});
        }
    }
    set_output_state(row, OutputState::Ok);          // R-172 单点迁移
    ctx.mux->on_output_metawrite_done(pt.cfg_index); // §7.2 末行：metawrite + mtime + 落盘 = 10%
    return true;
}

// 自适应输出（§9.6）：逐轮编码到候选临时文件 → 同步评分 → 选定者 commit → metawrite。
TargetOutcome run_target_adaptive(RunFileContext &ctx, const PlannedTarget &pt) {
    OutputResult &row = ctx.res.outputs[pt.cfg_index];
    const int cfg_index = static_cast<int>(pt.cfg_index);
    const std::string src = ctx.fe.src.string();

    // 输出级进度绑定（合成标识与单次编码路径同口径；每轮复用同一个 ProgressFn 语义）。
    const ProgressFn out_progress = ctx.mux->bind_output(pt.cfg_index, pt.target.format_id);
    const bool synth_pump = ctx.mux->synth_active(pt.cfg_index);
    const int enc_threads = encode_threads_of(ctx.ev.scope);

    // ---- 策略解析 + 格式作用域校验（失败 = 该输出失败；不静默当"关闭"）----
    AdaptivePolicy policy;
    AdaptiveError perr;
    if (!parse_and_validate_adaptive_policy(pt.adaptive_policy, pt.target.format_id, policy,
                                            &perr)) {
        row.error = "adaptive: " + adaptive_error_text(perr);
        row.error_kind = ErrorKind::Config;
        set_output_state(row, OutputState::Failed);
        log_error(kStage, kFile, "adaptive policy invalid",
                  {{"src", src},
                   {"out", row.out.string()},
                   {"format", pt.target.format_id},
                   {"error", row.error},
                   {"code", perr.code},
                   {"line", std::to_string(perr.line)},
                   {"col", std::to_string(perr.col)}});
        ctx.emit_output_done(row, cfg_index);
        return TargetOutcome::Failed;
    }

    // ---- bit depth note (§5.7，与单次编码路径同口径) ----
    const int out_bps = bitdepth_bps(pt.fmt->id, pt.target.out_bitdepth);
    if (ctx.info.src_bitdepth > out_bps) {
        row.warnings.push_back(Warning{WarningKind::DepthDowngrade,
                                       "source bit depth " + std::to_string(ctx.info.src_bitdepth) +
                                           " -> output bit depth " + std::to_string(out_bps) +
                                           " bps"});
    }

    ctx.stage(FileState::Encoding, Stage::Encode, cfg_index);

    // 每轮编码的行级进度 → (r-1+f)/max_rounds 的单调刻度（overall 不虚增；mux 自身单调高水位）。
    const float rmax = static_cast<float>(std::max(1, policy.max_rounds));
    const auto make_round_progress = [out_progress, rmax](int round) -> ProgressFn {
        return [out_progress, rmax, round](float f) {
            const float g =
                (static_cast<float>(std::max(0, round - 1)) + std::clamp(f, 0.0f, 1.0f)) / rmax;
            out_progress(std::clamp(g, 0.0f, 1.0f));
        };
    };

    // ---- 编码回调：本轮参数 → 候选临时文件（**不写元数据**；评分只回读像素）----
    // 逐轮告警/进度口径按轮记录，最终只采用**选定轮**的（与单次编码路径的告警序列同口径）。
    std::map<int, std::vector<Warning>> round_warnings;
    std::map<int, bool> round_reported;
    AdaptiveEncodeFn encode_round = [&](const AdaptiveEncodeInput &in) -> AdaptiveEncodeOutput {
        OutputTarget t = pt.target;
        t.out_path = in.out;
        t.params = in.params;
        const ProgressFn prog = make_round_progress(in.round);
        EncodeRequest req{ctx.work->buf,        t, ctx.meta, ctx.cancelled, prog, enc_threads,
                          ctx.cfg.color_dither8};
        const Clock::time_point t0 = Clock::now();
        EncodeResult er;
        {
            SynthPump pump(ctx.mux.get(), synth_pump);
            er = pt.enc->encode(req);
        }
        AdaptiveEncodeOutput o;
        o.encode_ms = ms_since(t0);
        o.bytes = er.bytes;
        o.ok = er.error.empty() && er.bytes != 0;
        o.error = er.error;
        o.progress_reported = er.progress_reported;
        round_warnings[in.round] = er.warnings;
        round_reported[in.round] = er.progress_reported;
        return o;
    };

    // ---- 评分回调：同步评分（复用 run_metric_job 单源；产物 = 候选文件）----
    // 自适应必须评分（§9.1-8：即使设置里 quality_score=false，启用自适应的输出仍算分）；
    // 评分期心跳（C15）：scoring_now 置位 → ctx 的心跳 ticker 补发的 heartbeat 带 scoring=true，
    // 并在进入评分前显式发一条 scoring=true 的 Progress。
    AdaptiveScoreFn score_round = [&](const std::filesystem::path &cand) -> AdaptiveScoreOutput {
        AdaptiveScoreOutput so;
        if (is_cancelled(ctx.cancelled)) {
            so.state = MetricState::Unavailable;
            so.note = "运行取消，评分未完成";
            return so;
        }
        MetricJob job;
        job.src = ctx.fe.src;
        job.out = cand;
        job.format_id = row.format_id;
        job.work = ctx.work; // shared_ptr<const WorkImage>（只读共享）
        job.width = ctx.work->spec.width;
        job.height = ctx.work->spec.height;
        job.bps = bitdepth_bps(row.format_id, pt.target.out_bitdepth);
        job.dither8 = ctx.cfg.color_dither8 && job.bps == 8 && row.format_id != "heif" &&
                      row.format_id != "avif";
        job.metric_srgb = ctx.metric_srgb;
        job.metric_icc = ctx.metric_icc;
        // 同步路径在本线程执行（ctx 生命周期覆盖）→ 直接用运行取消谓词，不需要评分池代次令牌。
        job.cancelled = ctx.cancelled;
        OutputResult scratch; // 评分落在临时行：选定后才并回 row（不污染未选定的轮）
        scratch.out = cand;
        scratch.format_id = row.format_id;
        ctx.scoring_output.store(cfg_index);
        if (ctx.ev.on_event) {
            FileEvent e{};
            e.state = FileState::Progress;
            e.progress = ctx.mux->snapshot(Stage::Encode, cfg_index);
            e.progress.heartbeat = true;
            e.progress.scoring = true;
            e.progress.adaptive_max = policy.max_rounds;
            ctx.ev.on_event(e);
        }
        run_metric_job(job, scratch);
        ctx.scoring_output.store(-1);
        so.state = scratch.metric_state;
        so.score = scratch.metric_ssimulacra2;
        so.metric_ms = scratch.metric_ms;
        so.note = scratch.metric_note;
        return so;
    };

    // ---- hooks：逐轮 info 日志 + ProgressInfo（adaptive_round/adaptive_max 加性字段）----
    // 需求 1（迭代优化过程写入日志）：执行器是**数据面**（计划/轮次/决策），本段只做日志与进度
    // 事件的**渲染**。四类 info 行（仅启用自适应时新增 → 默认路径逐字节零影响）：
    //   adaptive plan    —— 开跑前一条：target/max_rounds/stage 列表/policy_sha/基准关键参数
    //   adaptive stage   —— 每阶段开始一条：stage、round_before、当前已选轮
    //   adaptive round   —— 每轮一条：既有字段 + candidate/params/target/decision_reason（中文）
    //   adaptive summary —— 结束一条：selected 参数/reason_detail/各轮分数序列
    // 「已选轮」在 pipeline 侧按当前最高分跟踪（执行器按 output last 语义选定，日志与之同源）。
    int best_round = 0;
    double best_score = -1.0;
    AdaptiveRunHooks hooks;
    hooks.on_plan = [&](const AdaptivePlanInfo &info) {
        log_info(kStage, kFile, "adaptive plan",
                 {{"src", src},
                  {"out", row.out.string()},
                  {"output_index", std::to_string(cfg_index)},
                  {"format", pt.target.format_id},
                  {"backend", pt.target.backend_id},
                  {"tech", pt.target.tech_id},
                  {"bitdepth", std::to_string(pt.target.out_bitdepth)},
                  {"pixels", std::to_string(ctx.work->spec.width) + "x" +
                                 std::to_string(ctx.work->spec.height)},
                  {"policy_version", std::to_string(info.policy_version)},
                  {"target", fmt_double(info.target)},
                  {"max_rounds", std::to_string(info.max_rounds)},
                  {"output", info.output},
                  {"stages", join_text(info.stage_ids, ',')},
                  {"policy_sha", info.policy_sha.substr(0, kAdaptivePolicyShaLogChars)},
                  {"base_params", adaptive_params_brief(pt.target.params)}});
    };
    hooks.on_stage_start = [&](const std::string &stage_id, int round_before) {
        log_info(kStage, kFile, "adaptive stage",
                 {{"src", src},
                  {"out", row.out.string()},
                  {"output_index", std::to_string(cfg_index)},
                  {"stage", stage_id},
                  {"round_before", std::to_string(round_before)},
                  {"selected",
                   best_round > 0 ? "round" + std::to_string(best_round) : std::string("none")},
                  {"best_score", best_score >= 0.0 ? fmt_double(best_score) : std::string()}});
    };
    hooks.on_round = [&](const AdaptiveRoundRecord &rec, int max_rounds) {
        if (rec.metric == MetricState::Ok && rec.score > best_score) {
            best_score = rec.score;
            best_round = rec.round;
        }
        const bool ok_metric = rec.metric == MetricState::Ok;
        log_info(kStage, kFile, "adaptive round",
                 {{"src", src},
                  {"out", row.out.string()},
                  {"output_index", std::to_string(cfg_index)},
                  {"round", std::to_string(rec.round)},
                  {"stage", rec.stage},
                  {"key", rec.key},
                  {"value", rec.value},
                  {"params", rec.effective_params},
                  {"candidate", rec.candidate_path},
                  {"target", fmt_double(policy.target)},
                  {"score", ok_metric ? fmt_double(rec.score) : std::string()},
                  {"state", metric_state_text(rec.metric)},
                  {"bytes", std::to_string(rec.bytes)},
                  {"encode_ms", fmt_double(rec.encode_ms)},
                  {"metric_ms", fmt_double(rec.metric_ms)},
                  {"decision", rec.decision},
                  // 中文决策原因（需求 1 的「decision_reason」）：达标/改善/未改善/评分不可用/
                  // 编码失败；词表之外的取值原样透出（执行器新增词表时不编造翻译）。
                  {"decision_reason", adaptive_decision_reason(rec, policy.target)},
                  {"note", rec.note}});
        if (!ctx.ev.on_event)
            return;
        FileEvent e{};
        e.state = FileState::Progress;
        e.progress = ctx.mux->snapshot(Stage::Encode, cfg_index);
        e.progress.adaptive_round = rec.round;
        e.progress.adaptive_max = max_rounds;
        ctx.ev.on_event(e);
    };
    hooks.on_scoring = [&](int round, int max_rounds) {
        if (!ctx.ev.on_event)
            return;
        FileEvent e{};
        e.state = FileState::Progress;
        e.progress = ctx.mux->snapshot(Stage::Encode, cfg_index);
        e.progress.scoring = true;
        e.progress.adaptive_round = round;
        e.progress.adaptive_max = max_rounds;
        ctx.ev.on_event(e);
    };

    AdaptiveExecTarget atarget;
    atarget.format_id = pt.target.format_id;
    atarget.backend_id = pt.target.backend_id;
    atarget.tech_id = pt.target.tech_id;
    atarget.out_bitdepth = pt.target.out_bitdepth;
    atarget.lossless = lossless_flag(pt.target.params);
    atarget.width = ctx.work->spec.width;
    atarget.height = ctx.work->spec.height;
    atarget.final_path = row.out;
    const AdaptiveRunResult arun = run_policy(policy, pt.target.params, atarget, encode_round,
                                              score_round, ctx.cancelled, hooks);

    // 编码段总成本 = 各轮 encode_ms 之和（逐轮明细在 records/日志里；如实、不折算单轮）。
    row.t.encode_ms = 0.0;
    for (const AdaptiveRoundRecord &rec : arun.records)
        row.t.encode_ms += rec.encode_ms;

    // ---- 汇总日志（info；仅启用自适应时新增）----
    // 需求 1：在既有字段上补 selected 参数文本 / reason_detail（中文原因）/ 各轮分数序列
    // （scores=88.1,89.4,90.2；评分不可用的轮记 "-"，与 round 行的 state 字段互相印证）。
    std::vector<std::string> score_seq;
    score_seq.reserve(arun.records.size());
    for (const AdaptiveRoundRecord &rec : arun.records) {
        score_seq.push_back(rec.metric == MetricState::Ok ? fmt_double(rec.score)
                                                          : std::string("-"));
    }
    log_info(kStage, kFile, "adaptive summary",
             {{"src", src},
              {"out", row.out.string()},
              {"output_index", std::to_string(cfg_index)},
              {"rounds", std::to_string(arun.rounds)},
              {"target", fmt_double(arun.target)},
              {"selected", arun.selected_round > 0 ? "round" + std::to_string(arun.selected_round)
                                                   : std::string("none")},
              {"selected_params", arun.selected_params_text},
              {"policy_sha", arun.policy_sha.substr(0, kAdaptivePolicyShaLogChars)},
              {"final_score",
               arun.final_metric == MetricState::Ok ? fmt_double(arun.final_score) : std::string()},
              {"final_metric", metric_state_text(arun.final_metric)},
              {"scores", join_text(score_seq, ',')},
              {"overhead_ms", fmt_double(arun.overhead_ms)},
              {"reason", arun.reason},
              {"reason_detail", arun.reason_detail},
              {"state",
               arun.ok ? "ok" : (arun.reason == kAdaptiveReasonCancelled ? "cancelled" : "failed")},
              {"error", arun.error}});
    // dev-only 证据通道（侧车 adaptive 对象；不参与引擎语义）。
    publish_adaptive_summary(row.out.string(), std::make_shared<const AdaptiveRunResult>(arun));

    if (!arun.ok) {
        if (arun.reason == kAdaptiveReasonCancelled) {
            // §9.1-6：取消 = 丢弃全部候选、不落盘。
            row.error = arun.error.empty() ? std::string("adaptive cancelled") : arun.error;
            row.error_kind = ErrorKind::Cancelled;
            set_output_state(row, OutputState::Cancelled);
            ctx.mux->on_output_failed(pt.cfg_index); // 进度停在已完成，不虚增
            log_info(kStage, kFile, "encode cancelled",
                     {{"src", src}, {"out", row.out.string()}, {"detail", row.error}});
            ctx.emit_output_done(row, cfg_index);
            return TargetOutcome::Cancelled;
        }
        row.error = !arun.error.empty()
                        ? arun.error
                        : ("adaptive failed: " + (arun.reason.empty() ? "unknown" : arun.reason));
        if (arun.reason == kAdaptiveReasonEncodeFailed)
            row.error_kind = ErrorKind::Encode;
        else if (arun.reason == kAdaptiveReasonCommitFailed)
            row.error_kind = ErrorKind::Io;
        else
            row.error_kind = ErrorKind::Config;
        set_output_state(row, OutputState::Failed);
        log_error(kStage, kFile, row.error,
                  {{"src", src},
                   {"out", row.out.string()},
                   {"reason", arun.reason},
                   {"rounds", std::to_string(arun.rounds)}});
        ctx.emit_output_done(row, cfg_index);
        return TargetOutcome::Failed;
    }

    // ---- 成功：选定候选已 commit 到最终路径 → 该输出的 metric/告警/进度口径落地 ----
    row.out_bytes = arun.final_bytes;
    row.metric_state = arun.final_metric;
    row.metric_ssimulacra2 = arun.final_metric == MetricState::Ok ? arun.final_score : -1.0;
    row.metric_ms = arun.metric_ms;
    row.metric_note = arun.final_metric == MetricState::Ok ? std::string() : arun.reason_note;
    merge_warnings(row.warnings, arun.selected_round > 0 ? round_warnings[arun.selected_round]
                                                         : std::vector<Warning>{});
    ctx.mux->on_output_encode_done(
        pt.cfg_index, arun.selected_round > 0 ? round_reported[arun.selected_round] : false);
    const ProgressMux::Stats pstats = ctx.mux->stats(pt.cfg_index);
    log_debug(kStage, kFile, "progress snapshot",
              {{"src", src},
               {"output_index", std::to_string(cfg_index)},
               {"format", pt.target.format_id},
               {"progress_reported", pstats.reported ? "true" : "false"},
               {"progress_max_row", std::to_string(pstats.max_row)},
               {"progress_samples", std::to_string(pstats.samples)},
               {"progress_synthetic", pstats.synthetic ? "true" : "false"},
               {"encode_est_ms", fmt_double(pstats.est_ms)},
               {"adaptive_rounds", std::to_string(arun.rounds)}});
    row.progress_reported = pstats.reported;
    row.progress_max_row = pstats.max_row;

    if (!run_target_metawrite(ctx, pt, row))
        return TargetOutcome::Failed;
    // 事件：选定轮已评分（同步）→ 直接发含 metric 的输出完成事件（不再提交异步评分任务，
    // 避免同一产物重复计分；分数与产物像素逐字节对应 —— 选定候选就是提交的那份）。
    ctx.emit_output_done(row, cfg_index);
    return TargetOutcome::Ok;
}

// 单 target 的编码 + metawrite 段（线程内串行；可在并行线程执行，见上方线程纪律）。
// 约定：不得调用 ctx.finalize()（聚合终态只由 coordinator 线程做一次）；返回 Cancelled 时
// coordinator 负责发 Cancelled 终态。
TargetOutcome run_target(RunFileContext &ctx, const PlannedTarget &pt) {
    // —— M10 / W3-T4（docs/v0.4.0-design.md §9.6）：启用自适应（策略非空）的输出走轮循环 ——
    // 空策略（默认）= 下方既有单次编码路径，**逐字不变**（回归 zero diff 的结构性保证）。
    if (!pt.adaptive_policy.empty())
        return run_target_adaptive(ctx, pt);
    OutputResult &row = ctx.res.outputs[pt.cfg_index];
    // W1-T6：该输出进入编码段（§7.2 的 50%+10% 块）。绑定必须早于本输出的任何阶段
    // 事件 —— 合成标识（§7.3）以"格式是否在 k 表内"预置，编码返回后再按
    // `progress_reported` 校正；先绑定可保证该输出的**所有**事件 synthetic 口径一致。
    const ProgressFn out_progress = ctx.mux->bind_output(pt.cfg_index, pt.target.format_id);
    const bool synth_pump = ctx.mux->synth_active(pt.cfg_index);

    if (is_cancelled(ctx.cancelled)) {
        // C13：已绑定（该输出的 Encode 0 事件已发）→ 必须给该输出一个 OutputDone 终态，
        // 否则消费端/审计（每个出现过的 output_index 都要有完成事实）缺行。
        set_output_state(row, OutputState::Cancelled);
        ctx.emit_output_done(row, static_cast<int>(pt.cfg_index));
        return TargetOutcome::Cancelled;
    }

    // ---- bit depth note (§5.7) ----
    // BMP-02：BMP 的 out_bitdepth 是 bpp（24），其余格式是 bps；必须换算到同单位再比较，
    // 否则 16-bit 源 → BMP 24bpp（实际 8bps）不会告警。
    const int out_bps = bitdepth_bps(pt.fmt->id, pt.target.out_bitdepth);
    if (ctx.info.src_bitdepth > out_bps) {
        row.warnings.push_back(Warning{WarningKind::DepthDowngrade,
                                       "source bit depth " + std::to_string(ctx.info.src_bitdepth) +
                                           " -> output bit depth " + std::to_string(out_bps) +
                                           " bps"});
    }

    // ---- encode (§5.7)：单输出一次调用（§3.1）----
    ctx.stage(FileState::Encoding, Stage::Encode, static_cast<int>(pt.cfg_index));
    Clock::time_point t0 = Clock::now();
    // EncodeRequest 字段序（encoder.h §3.1）：img, target, meta, cancelled, progress,
    // encode_threads, dither8。W1-T6：progress = ProgressMux 的输出级回调（真实行级 →
    // 直接汇流；无回调格式 → 走合成估算，编码期间由 SynthPump 按 20ms 采样，§7.3）；
    // encode_threads = W1-T7 起由调度器 E3 分配（`RunScope::encode_threads`，§8.2），
    // 无 scope（单测直调）恒 1 = 0.2 行为。dither8 = UX-14 C2（M7-R2A）：RunConfig 的
    // color_dither8 进编码器 8bit 量化段（默认 false = 逐字不变）。
    // T6：并行时每 target 各持一个 EncodeRequest（无共享可变状态）；工作缓冲只读。
    const int enc_threads = encode_threads_of(ctx.ev.scope);
    EncodeRequest req{ctx.work->buf,        pt.target,    ctx.meta,
                      ctx.cancelled,        out_progress, enc_threads,
                      ctx.cfg.color_dither8};
    EncodeResult er;
    {
        SynthPump pump(ctx.mux.get(), synth_pump);
        er = pt.enc->encode(req);
    }
    row.t.encode_ms = ms_since(t0);
    merge_warnings(row.warnings, er.warnings);
    const bool enc_ok = er.error.empty() && er.bytes != 0;
    // 先按 §3.1 口径校正 mux 的合成标识（progress_reported=false → 该输出标合成），
    // 再取快照 —— 快照里的 synthetic/reported 即该输出的**最终**口径（不写两次）。
    if (enc_ok)
        ctx.mux->on_output_encode_done(pt.cfg_index, er.progress_reported);
    else
        ctx.mux->on_output_failed(pt.cfg_index); // 失败：停在已完成进度，不虚增
    // §7.4 进度快照（debug 级，每输出一条；**不逐行落盘**）：progress_max_row /
    // progress_reported 是全仓进度口径的两个事实键，另附 samples/synthetic/est_ms 便于
    // 校准 k 表（§7.3 "k 校准表随日志实测回归校正"）。
    const ProgressMux::Stats pstats = ctx.mux->stats(pt.cfg_index);
    log_debug(kStage, kFile, "progress snapshot",
              {{"src", ctx.fe.src.string()},
               {"output_index", std::to_string(pt.cfg_index)},
               {"format", pt.target.format_id},
               {"progress_reported", pstats.reported ? "true" : "false"},
               {"progress_max_row", std::to_string(pstats.max_row)},
               {"progress_samples", std::to_string(pstats.samples)},
               {"progress_synthetic", pstats.synthetic ? "true" : "false"},
               {"encode_est_ms", fmt_double(pstats.est_ms)}});
    row.progress_reported = pstats.reported;
    row.progress_max_row = pstats.max_row;
    if (!enc_ok) {
        // R-163 残余（E3，M7-R2A）：取消 vs 失败的 verdict 归类**单点**收口 —— 编码器因
        // cancel 提前退出（cancelled() 为真，或错误串是取消语义）时走与各阶段 is_cancelled
        // 检查点一致的 Cancelled 终态（set_output_state 唯一迁移 + error_kind_of 唯一判据
        // 双轨对齐），不再误归 Failed、不再虚报 encode 错误（旧口径：错误串被 "encode
        // failed: " 前缀重写 ⇒ error_kind_of 判 Encode、verdict 判 Failed/ DoneWithErrors）。
        const bool cancelled_row =
            is_cancelled(ctx.cancelled) || er.error.find("encode cancelled") != std::string::npos;
        if (cancelled_row) {
            row.error = "encode cancelled"; // 规范化：error_kind_of 的既有判据即命中 Cancelled
            row.error_kind = error_kind_of(row.error);
            set_output_state(row, OutputState::Cancelled);
            ctx.mux->on_output_failed(pt.cfg_index); // 进度停在已完成，不虚增
            log_info(
                kStage, kFile, "encode cancelled",
                {{"src", ctx.fe.src.string()}, {"out", row.out.string()}, {"detail", er.error}});
            ctx.emit_output_done(row, static_cast<int>(pt.cfg_index)); // A1/A2
            return TargetOutcome::Cancelled; // 取消：不再处理后续 target（同各阶段检查点语义）
        }
        row.error = "encode failed: " +
                    (er.error.empty() ? std::string("encoder produced no data") : er.error);
        row.error_kind = ErrorKind::Encode;
        set_output_state(row, OutputState::Failed);
        log_error(kStage, kFile, row.error,
                  {{"src", ctx.fe.src.string()}, {"out", row.out.string()}});
        ctx.emit_output_done(row, static_cast<int>(pt.cfg_index)); // A1/A2（失败行也要有终态）
        return TargetOutcome::Failed; // 该输出失败：其余输出继续（§4.1 任一失败=DoneWithErrors）
    }
    row.out_bytes = er.bytes;

    // metadata write (§5.8, §4.8)：W3-T4 起与自适应路径共用同一条写路径（语义逐字搬移）。
    if (!run_target_metawrite(ctx, pt, row))
        return TargetOutcome::Failed;
    // —— M10 / W1-B1（A1/A2；C13）：输出完成事件 ——
    // 时点 = "编码 + metawrite 完成"之后（metric 尚未计算；异步评分完成后由评分池补发
    // 含 metric 的第二次 OutputDone）。文件终态事件在此之后才发出（幂等校准）。
    ctx.emit_output_done(row, static_cast<int>(pt.cfg_index));
    // —— M10 / W1-B1（T5；C3/C14）：提交异步评分任务 ——
    // 非自适应默认路径**不再同步等评分**：该行已完成（OutputDone 已发），评分在评分池的
    // 独立线程执行，完成后再发一次含 metric 的 OutputDone。评分失败/不可用不影响产物。
    submit_score_task(ctx, pt, row);
    return TargetOutcome::Ok;
}

// ---- ⑨ encode_loop（T6：跳过行先结算 → 其余 target 按并行度扇出 → 聚合收尾）----
bool stage_encode_loop(RunFileContext &ctx) {
    // 跳过行（冲突 Skip 命中）先按配置顺序结算：不占并行度、不发编码事件（0.2 语义不变）。
    std::vector<std::size_t> order;
    order.reserve(ctx.targets.size());
    for (std::size_t i = 0; i < ctx.targets.size(); ++i) {
        const PlannedTarget &pt = ctx.targets[i];
        if (pt.skip) {
            OutputResult &row = ctx.res.outputs[pt.cfg_index];
            set_output_state(row, OutputState::Skipped); // R-172 单点迁移
            ctx.mux->on_output_skipped(pt.cfg_index);    // §7.2：该输出全段记满（文件整体可达 1.0）
            ctx.emit_output_done(row, static_cast<int>(pt.cfg_index)); // A1/A2：该行终态已定
            continue;
        }
        order.push_back(i);
    }
    if (order.empty()) {
        ctx.res.t.encode_ms = 0.0;
        ctx.res.t.metawrite_ms = 0.0;
        return true; // 全部跳过：无编码任务（与旧循环逐字等价）
    }

    const int total = static_cast<int>(order.size());
    int parallel =
        ctx.cfg.max_parallel_targets > 0 ? ctx.cfg.max_parallel_targets : std::min(2, total);
    // —— 0.4.2 需求6（M14；Lead 已批准 RunScope 末尾加性追加）：调度器自适应线程规划 P ——
    // 规划器给出本文件"同时编码的 target 数"（核心数/编码器能力/CPU 已入规划）；
    // scope->parallel_outputs == 0 = 旧行为逐字不变（无 scope 的单测路径同样走旧口径）。
    // 规划值仍受 RunConfig::max_parallel_targets（用户显式上限）与 target 总数夹取。
    if (ctx.ev.scope != nullptr && ctx.ev.scope->parallel_outputs > 0) {
        parallel = ctx.ev.scope->parallel_outputs;
        if (ctx.cfg.max_parallel_targets > 0)
            parallel = std::min(parallel, ctx.cfg.max_parallel_targets);
    }
    parallel = std::clamp(parallel, 1, total);

    // —— 飞行缓冲（§4.4.3）：并行任务在文件预算之外各占一份 token ——
    // 估算 = 每任务的编码器内部缓冲上界（enc_common 的表，按格式/位深）；整个文件的并行
    // 需求装不下 → parallel 退回 1（串行仍可完成，只慢；**不失败**）。
    struct FlightTokens {
        PixelBudget *budget = nullptr;
        std::vector<std::uint64_t> held;
        ~FlightTokens() {
            for (std::uint64_t b : held) {
                if (budget != nullptr && b != 0)
                    budget->release(b);
            }
        }
    } flight;
    if (parallel > 1 && ctx.ev.scope != nullptr && ctx.ev.scope->budget != nullptr) {
        const std::uint64_t px = static_cast<std::uint64_t>(ctx.work->spec.width) *
                                 static_cast<std::uint64_t>(ctx.work->spec.height);
        std::uint64_t per_task = 0;
        for (const std::size_t idx : order) {
            const PlannedTarget &pt = ctx.targets[idx];
            per_task = std::max(
                per_task,
                enc::encoder_flight_bytes(pt.target.format_id,
                                          bitdepth_bps(pt.fmt->id, pt.target.out_bitdepth), px));
        }
        PixelBudget *budget = ctx.ev.scope->budget;
        const std::uint64_t want = per_task * static_cast<std::uint64_t>(parallel);
        bool ok = per_task > 0 && budget->used() + want <= budget->capacity();
        if (ok) {
            flight.budget = budget;
            for (int i = 0; i < parallel; ++i) {
                if (!budget->acquire(per_task, ctx.cancelled)) {
                    ok = false;
                    break;
                }
                flight.held.push_back(per_task);
            }
        }
        if (!ok) {
            flight.held.clear(); // 全部归还（FlightTokens 析构时 budget 仍有效）
            parallel = 1;
        }
        log_info(kStage, kFile, "encoder flight budget",
                 {{"src", ctx.fe.src.string()},
                  {"parallel", std::to_string(parallel)},
                  {"per_task_bytes", std::to_string(per_task)},
                  {"held_bytes", std::to_string(want)},
                  {"budget_used", std::to_string(budget->used())},
                  {"budget_capacity", std::to_string(budget->capacity())}});
    }

    // —— 扇出（parallel==1 时零线程：主线程直接跑 = 0.3.6 行为）——
    std::atomic<std::size_t> next{0};
    std::atomic<bool> cancel_hit{false};
    std::vector<TargetOutcome> outcome(order.size(), TargetOutcome::Ok);
    const auto worker = [&]() {
        for (;;) {
            const std::size_t slot = next.fetch_add(1, std::memory_order_relaxed);
            if (slot >= order.size())
                return;
            if (cancel_hit.load(std::memory_order_relaxed))
                return;
            const std::size_t idx = order[slot];
            const TargetOutcome o = run_target(ctx, ctx.targets[idx]);
            outcome[slot] = o;
            if (o == TargetOutcome::Cancelled) {
                cancel_hit.store(true, std::memory_order_relaxed);
                return;
            }
        }
    };
    const int spawn = parallel - 1;
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(spawn > 0 ? spawn : 0));
    for (int i = 0; i < spawn; ++i)
        threads.emplace_back(worker);
    worker();
    for (std::thread &t : threads) {
        if (t.joinable())
            t.join();
    }

    // 计时聚合在 join 之后做（并发下不得在任务里 += 共享计数）。
    ctx.res.t.encode_ms = 0.0;
    ctx.res.t.metawrite_ms = 0.0;
    for (const OutputResult &row : ctx.res.outputs) {
        ctx.res.t.encode_ms += row.t.encode_ms;
        ctx.res.t.metawrite_ms += row.t.metawrite_ms;
    }

    const bool any_cancelled = std::any_of(outcome.begin(), outcome.end(), [](TargetOutcome o) {
        return o == TargetOutcome::Cancelled;
    });
    if (any_cancelled) {
        ctx.finalize(FileOutcome::Verdict::Cancelled);
        return false;
    }
    return true;
}

// ---- ⑩ finish_file（mtime + 收尾样本 + 逐输出/整文件日志 + aggregate 单源聚合）----
void stage_finish(RunFileContext &ctx) {
    // C15：收尾前先停心跳线程（此后不再有长静默段；避免收尾事件与心跳交错）。
    if (ctx.ticker) {
        ctx.ticker->stop();
        ctx.ticker.reset();
    }
    // mtime sync (§5.9)：生效 DateTimeOriginal 只算一次，逐输出落盘一次
    // M13-C3：开关取生效 plan（= effective_rules()，per-file 例外可覆盖）；
    // 无例外时与 cfg.rules.sync_mtime 同值 → 行为零变化。
    if (ctx.meta_plan.sync_mtime && !ctx.meta_plan.datetime_original.empty()) {
        for (const OutputResult &row : ctx.res.outputs) {
            if (!row.ok)
                continue;
            const std::string merr = sync_file_mtime(row.out, ctx.meta_plan.datetime_original);
            if (!merr.empty()) {
                log_warn(kStage, kFile, "mtime sync failed",
                         {{"out", row.out.string()}, {"error", merr}});
            }
        }
    }
    // W1-T6：收尾样本（成功路径此处 file_frac 已按 §7.2 达 1.0；失败路径不虚增）
    ctx.mux->finish();

    for (const OutputResult &row : ctx.res.outputs) {
        log_info(kStage, kFile, "output done",
                 {{"src", ctx.fe.src.string()},
                  {"out", row.out.string()},
                  {"format", row.format_id},
                  {"ok", row.ok ? "true" : (row.skipped ? "skipped" : "false")},
                  {"bytes", std::to_string(row.out_bytes)},
                  {"encode_ms", fmt_double(row.t.encode_ms)},
                  {"metawrite_ms", fmt_double(row.t.metawrite_ms)},
                  {"warnings", std::to_string(row.warnings.size())}});
        for (const Warning &w : row.warnings) {
            log_warn(
                kStage, kFile, "output warning",
                {{"src", ctx.fe.src.string()}, {"out", row.out.string()}, {"detail", w.detail}});
        }
    }
    const FileOutcome::Verdict v = verdict_of_rows(ctx.res);
    log_info(
        kStage, kFile, "file done",
        {{"src", ctx.fe.src.string()},
         {"outputs", std::to_string(ctx.res.outputs.size())},
         {"ok_outputs", std::to_string(std::count_if(ctx.res.outputs.begin(), ctx.res.outputs.end(),
                                                     [](const OutputResult &o) { return o.ok; }))},
         {"bytes", std::to_string(ctx.res.out_bytes)},
         {"decode_ms", fmt_double(ctx.res.t.decode_ms)},
         {"orient_ms", fmt_double(ctx.res.t.orient_ms)},
         {"color_ms", fmt_double(ctx.res.t.color_ms)},
         {"flatten_ms", fmt_double(ctx.res.t.flatten_ms)},
         {"encode_ms", fmt_double(ctx.res.t.encode_ms)},
         {"metawrite_ms", fmt_double(ctx.res.t.metawrite_ms)},
         {"verdict", v == FileOutcome::Verdict::Done      ? "Done"
                     : v == FileOutcome::Verdict::Skipped ? "Skipped"
                     : v == FileOutcome::Verdict::Failed  ? "Failed"
                                                          : "DoneWithErrors"},
         {"warnings", std::to_string(ctx.res.warnings.size())}});
    ctx.finalize(v);
}

// R-168（M7-WP8/B.8）：双入口共享的失败收尾（旧行为逐字：行级 error 回填 + log_error +
// finalize(Failed)）。
void fail_fill(FileOutcome &outcome, Clock::time_point t_start, std::string msg) {
    FileResult &res = outcome.file;
    res.error = msg;
    res.error_kind = error_kind_of(msg);
    for (OutputResult &o : res.outputs) {
        if (!o.ok && !o.skipped && o.error.empty()) {
            o.error = msg;
            o.error_kind = res.error_kind;
        }
    }
    log_error(kStage, kFile, msg, {{"src", res.src.string()}});
    finalize_aggregate(outcome, t_start, FileOutcome::Verdict::Failed);
}

} // namespace

// ---------------------------------------------------------------------------
// §3.9 frozen predicate (strong definition overriding the weak fallback in metadata.cpp)
// ---------------------------------------------------------------------------
bool format_supports_metadata_only(std::string_view format_id) {
    return detail_metadata_only_supported(format_id);
}

// ---------------------------------------------------------------------------
// run_one_file（多输出循环，§4）—— B.4 拆解后本体 = 十阶段编排
// ---------------------------------------------------------------------------
FileOutcome run_one_file(const FileEntry &fe, const RunConfig &cfg, EventFn ev) {
    // 0.4.2 / C6-②（M14）：事件投递单线程 drain（跨线程共享可变状态的唯一入口收口；
    // 空回调路径零成本不变 —— 见上方 SerialEventSink 段注释）。
    ev.on_event = serialized_event_fn(std::move(ev.on_event));
    RunFileContext ctx(fe, cfg, ev);
    try {
        if (!stage_validate(ctx))
            return std::move(ctx.outcome);
        if (!stage_probe(ctx))
            return std::move(ctx.outcome);
        if (!stage_plan(ctx))
            return std::move(ctx.outcome);
        if (!stage_budget(ctx))
            return std::move(ctx.outcome);
        if (!stage_decode(ctx))
            return std::move(ctx.outcome);
        if (!stage_orient(ctx))
            return std::move(ctx.outcome);
        if (!stage_color(ctx))
            return std::move(ctx.outcome);
        if (!stage_meta(ctx))
            return std::move(ctx.outcome);
        if (!stage_encode_loop(ctx))
            return std::move(ctx.outcome);
        stage_finish(ctx);
        return std::move(ctx.outcome);
    } catch (const std::exception &e) {
        return ctx.fail(std::string("unexpected exception: ") + e.what());
    } catch (...) {
        return ctx.fail("unexpected non-standard exception");
    }
}

// ---------------------------------------------------------------------------
// run_metadata_only（§3.2：语义不变，互斥 outputs.size()==1；零重编码）
// M7-WP8/R-168：finalize/fail 与 run_one_file 共用单源（旧行为逐字一致）。
// ---------------------------------------------------------------------------
FileOutcome run_metadata_only(const FileEntry &fe, const RunConfig &cfg, EventFn ev) {
    namespace fs = std::filesystem;

    // 0.4.2 / C6-②（M14）：与 run_one_file 同口径的事件串行化（此路径同样有多线程事件源：
    // mux 事件回调 + 心跳线程 + 消费端直发）。
    ev.on_event = serialized_event_fn(std::move(ev.on_event));

    FileOutcome outcome;
    FileResult &res = outcome.file;
    res.src = fe.src;
    const Clock::time_point t_start = Clock::now();

    const std::function<bool()> cancelled =
        (ev.scope != nullptr) ? ev.scope->cancelled : std::function<bool()>();
    const std::vector<fs::path> reserved =
        (ev.scope != nullptr) ? ev.scope->reserved : std::vector<fs::path>{};
    // W1-T6：进度枢纽（仅元数据模式同样走 §7.2 权重表：无 encode/decode 段 → 对应阶段记满）。
    // 本路径单文件单输出，阶段事件按**源文件级**（output_index=-1）发；输出级进度由
    // ProgressMux 的逐输出记账驱动（synthetic 恒 false：无编码器参与）。
    std::shared_ptr<ProgressMux> mux;
    const auto stage = [&ev, &mux](FileState s, Stage st) {
        if (!ev.on_event)
            return;
        FileEvent e{};
        e.index = 0;
        e.state = s;
        e.result = nullptr;
        if (mux)
            e.progress = mux->snapshot(st, -1);
        ev.on_event(e);
    };

    const auto finalize = [&t_start, &outcome](FileOutcome::Verdict v) -> FileOutcome {
        finalize_aggregate(outcome, t_start, v);
        return outcome;
    };
    // M10 / W1-B1（A1/A2；C13）：仅元数据模式的逐输出终态事件（单输出，index 0）。
    // 该路径同样会发输出级进度事件（mux 的 index 0）→ 审计要求每个出现过的 output_index
    // 都有可识别的完成事实；此处按"该行终态已定"时点发（成功/跳过/失败/取消各一次）。
    const auto emit_output_done = [&ev, &mux, &res]() {
        if (!ev.on_event || res.outputs.empty())
            return;
        FileEvent e{};
        e.state = FileState::OutputDone;
        if (mux)
            e.progress = mux->snapshot(Stage::Done, 0);
        e.output = std::make_shared<const OutputResult>(res.outputs.front());
        ev.on_event(e);
    };
    const auto fail = [&t_start, &outcome, &emit_output_done](std::string msg) -> FileOutcome {
        fail_fill(outcome, t_start, std::move(msg)); // R-168：与 run_one_file 同源收尾
        emit_output_done();
        return outcome;
    };

    try {
        // §3.2 硬校验：仅元数据模式与多输出互斥（outputs[0] 仍作配置容器，但其 format_id
        // 不决定产物容器 —— 见下）。
        if (cfg.outputs.size() != 1)
            return fail("metadata-only mode requires exactly one output "
                        "(RunConfig::outputs.size() == 1)");
        // 仅元数据 = 零重编码：产物格式**必须跟随输入**。旧实现拿 cfg.outputs[0].format_id
        // （UI 默认 jxl / 预设里的目标格式）做能力校验与命名，导致"选 jxl 跑仅元数据"要么
        // 直接报 jxl 不支持、要么 $format 目录写成 jxl 而文件仍是 jpg。现改为源扩展名单源。
        const FormatDef *fmt = format_for_source_path(fe.src);
        if (fmt == nullptr)
            return fail("cannot determine source format for metadata-only output: " +
                        fe.src.extension().string());
        {
            OutputResult row;
            row.format_id = fmt->id; // 真实产物格式（源格式），不是 UI 选中的目标格式
            row.backend_id.clear();  // 仅元数据不解码/不编码 → 无后端/技术
            row.tech_id.clear();
            res.outputs.push_back(std::move(row));
        }
        OutputResult &row = res.outputs.front();

        std::string tmpl_err;
        if (!validate_output_template(cfg.output_template, &tmpl_err))
            return fail(tmpl_err);

        stage(FileState::Probing, Stage::Probe);
        // X-METADATA-ONLY-07：仅元数据不解码像素，OIIO probe 的拒绝（CMYK/截断等）不应
        // 阻断纯元数据重写；probe 失败只降级为“无尺寸信息”，真正的门槛是 read_metadata。
        ProbeOutcome po = probe_file(fe.src);
        if (!po.error.empty()) {
            log_warn(kStage, kFile, "probe failed; continuing metadata-only",
                     {{"src", fe.src.string()}, {"error", po.error}});
            po.info = ImageInfo{};
            po.info.format = fmt->id;
        }
        const ImageInfo info = po.info; // §3.2：FileEntry 只读，不回填
        res.info = info;
        // W1-T6：进度枢纽 + §7.2 共享段（本路径不解码/不编码 → decode/orient/color/encode 记满）
        mux = std::make_unique<ProgressMux>(std::max(1, info.width), std::max(1, info.height), 1);
        mux->set_event_callback([&ev](const FileEvent &e) {
            if (ev.on_event)
                ev.on_event(e);
        });
        mux->shared_stage(Stage::Probe, 1.f); // probe + acquire（本路径无内存背压）3%
        mux->shared_skip(Stage::Decode);      // 不解码（零重编码）
        mux->shared_skip(Stage::Orient);
        mux->shared_skip(Stage::Color);
        mux->on_output_no_encode(0); // 无编码段 → 50% 记满（其余走 metawrite 10%）

        if (!format_supports_metadata_only(fmt->id)) {
            return fail("format '" + fmt->id +
                        "' does not support metadata-only rewrite (zero re-encode)");
        }
        // 路径模板（同容器 → 保持源扩展名）+ 冲突解析（逐输出独立）
        // R-175（M7-WP8/B.8）：PathCtx 组装收口 path_ctx_for（metadata_only → 源扩展名）。
        const PathCtx ctx_path = path_ctx_for(fe, cfg, fmt);
        const fs::path desired = render_output_path(cfg.output_template, ctx_path, cfg.out_root);
        if (desired.empty())
            return fail("output path: template '" + cfg.output_template +
                        "' produced no file name");
        // R-001（P0）：仅元数据模式同样**不得写回源文件**；命中源文件按普通名字冲突处理
        // （与 run_one_file 同口径：源文件占着这个名字 → Rename 产出 "name (1).ext"，
        // Skip 跳过该输出，Overwrite 降级为 Rename —— 数据安全线优先于冲突策略）。
        const bool source_collision = same_output_as_source(desired, fe.src);
        const ConflictPolicy effective_conflict =
            (source_collision && cfg.conflict != ConflictPolicy::Skip) ? ConflictPolicy::Rename
                                                                       : cfg.conflict;
        if (source_collision) {
            log_warn(kStage, kFile,
                     "output path collides with the source file; source is never written",
                     {{"src", fe.src.string()},
                      {"out", desired.string()},
                      {"policy", std::string(conflict_policy_name(effective_conflict))}});
        }
        // R-007：渲染后统一做合法名/长度预检（与 run_one_file 同口径）。
        std::string name_err;
        if (!validate_output_filename(desired, &name_err))
            return fail("output path: " + name_err);
        std::string conflict_err;
        // R-005：批内 reserved 为软保留（Skip 命中软保留而磁盘缺失 → 回退正常写入）。
        static const std::vector<fs::path> kNoHardReserved;
        const OutputPlan out_plan =
            resolve_conflict(desired, effective_conflict, kNoHardReserved, reserved, conflict_err);
        if (!conflict_err.empty())
            return fail("output path: " + conflict_err);
        row.out = out_plan.out_path;
        if (out_plan.skip) {
            set_output_state(row, OutputState::Skipped); // R-172 单点迁移
            mux->on_output_skipped(0);                   // §7.2：无工作可做 → 全段记满
            log_info(kStage, kFile, "skipped: output exists",
                     {{"src", fe.src.string()}, {"out", row.out.string()}});
            emit_output_done(); // A1/A2：该行终态（skipped）
            return finalize(FileOutcome::Verdict::Skipped);
        }
        {
            std::error_code ec;
            const fs::path parent = row.out.parent_path();
            if (!parent.empty())
                fs::create_directories(parent, ec);
            if (ec)
                return fail("cannot create output directory: " + ec.message());
        }
        if (is_cancelled(cancelled)) {
            set_output_state(row, OutputState::Cancelled);
            emit_output_done(); // A1/A2：取消行的终态事实
            return finalize(FileOutcome::Verdict::Cancelled);
        }

        SourceMeta srcmeta = read_metadata(fe.src);
        if (!srcmeta.error.empty()) {
            // R-004（P1）：仅元数据模式下"读不到源元数据"不得按空元数据继续 —— 那会把
            // 副本的 EXIF/XMP 清空并报成功。读失败 = 该文件失败（重编码路径不受此限，
            // §4.8 的"读失败非致命"语义保持不变）。
            return fail("source metadata unavailable: " + srcmeta.error);
        }
        MetadataPlan meta_plan = build_plan(srcmeta, cfg.rules, fe.exception);
        // No orientation clearing here: the pixels are not rotated in metadata-only mode, so
        // dropping the tag would change how the image displays.

        const Payloads payloads = make_payloads(meta_plan);
        stage(FileState::Writing, Stage::MetaWrite);
        mux->output_stage(0, Stage::MetaWrite);
        const Clock::time_point t0 = Clock::now();
        // R-030：失败后不留"占名的坏副本"——本次创建的输出（此前不存在）在失败时删除；
        // 覆盖已存在的目标时不删除（那是上一轮的好文件，删除 = 破坏用户数据）。
        std::error_code exist_ec;
        const bool out_existed = fs::exists(row.out, exist_ec);
        const std::string err = rewrite_metadata_only(fe.src, row.out, meta_plan, payloads);
        row.t.metawrite_ms = ms_since(t0);
        res.t.metawrite_ms = row.t.metawrite_ms;
        merge_warnings(row.warnings, meta_plan.warnings);
        if (!err.empty()) {
            if (!out_existed) {
                std::error_code rm_ec;
                fs::remove(row.out, rm_ec);
            }
            return fail("metadata-only rewrite failed: " + err);
        }
        mux->on_output_metawrite_done(0); // §7.2 末行：metawrite + mtime + 落盘 = 10%

        // M13-C3：mtime 同步开关取生效 plan（仅元数据模式同样覆盖；无例外时与旧口径同值）。
        if (meta_plan.sync_mtime && !meta_plan.datetime_original.empty()) {
            const std::string merr = sync_file_mtime(row.out, meta_plan.datetime_original);
            if (!merr.empty()) {
                log_warn(kStage, kFile, "mtime sync failed",
                         {{"out", row.out.string()}, {"error", merr}});
            }
        }

        std::error_code ec;
        row.out_bytes = fs::file_size(row.out, ec);
        if (ec)
            row.out_bytes = 0;
        set_output_state(row, OutputState::Ok); // R-172 单点迁移
        emit_output_done();                     // A1/A2：输出完成事实（早于文件终态）
        mux->finish();                          // W1-T6：收尾样本（此路径 §7.2 全段记满 → 1.0）
        log_info(kStage, kFile, "metadata-only done",
                 {{"src", fe.src.string()},
                  {"out", row.out.string()},
                  {"bytes", std::to_string(row.out_bytes)},
                  {"metawrite_ms", fmt_double(row.t.metawrite_ms)},
                  {"warnings", std::to_string(row.warnings.size())}});
        for (const Warning &w : row.warnings) {
            log_warn(kStage, kFile, "output warning",
                     {{"src", fe.src.string()}, {"out", row.out.string()}, {"detail", w.detail}});
        }
        return finalize(FileOutcome::Verdict::Done);
    } catch (const std::exception &e) {
        return fail(std::string("unexpected exception: ") + e.what());
    } catch (...) {
        return fail("unexpected non-standard exception");
    }
}

// ============================================================================================
// M7-WP8 / B.8 契约收口实现（声明见 pipeline.h 尾部加性块）
// ============================================================================================

const char *output_state_name(OutputState s) {
    switch (s) {
    case OutputState::Pending:
        return "pending";
    case OutputState::Ok:
        return "ok";
    case OutputState::Skipped:
        return "skipped";
    case OutputState::Failed:
        return "failed";
    case OutputState::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

// R-172：终态双轨（state 枚举 ↔ ok/skipped 布尔）的**唯一**对齐点。
void set_output_state(OutputResult &row, OutputState s) {
    row.state = s;
    row.ok = (s == OutputState::Ok);
    row.skipped = (s == OutputState::Skipped);
}

const char *error_kind_name(ErrorKind k) {
    switch (k) {
    case ErrorKind::None:
        return "none";
    case ErrorKind::Config:
        return "config";
    case ErrorKind::Probe:
        return "probe";
    case ErrorKind::Decode:
        return "decode";
    case ErrorKind::Orient:
        return "orient";
    case ErrorKind::Color:
        return "color";
    case ErrorKind::Flatten:
        return "flatten";
    case ErrorKind::Encode:
        return "encode";
    case ErrorKind::MetaWrite:
        return "metawrite";
    case ErrorKind::Io:
        return "io";
    case ErrorKind::Cancelled:
        return "cancelled";
    case ErrorKind::Internal:
        return "internal";
    case ErrorKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

// R-176：消息前缀约定的**唯一**判据（此前散在 pipeline.cpp:630 等处的字符串约定收口于此；
// UI 中文映射表按 ErrorKind 对表，见 handoff「跨界需求」）。
ErrorKind error_kind_of(std::string_view error) {
    if (error.empty())
        return ErrorKind::None;
    const auto starts = [&error](std::string_view p) { return error.rfind(p, 0) == 0; };
    if (starts("probe failed:"))
        return ErrorKind::Probe;
    if (starts("decode failed:"))
        return ErrorKind::Decode;
    if (starts("orient failed:"))
        return ErrorKind::Orient;
    if (starts("color transform failed:"))
        return ErrorKind::Color;
    if (starts("flatten failed:"))
        return ErrorKind::Flatten;
    if (starts("encode failed:"))
        return ErrorKind::Encode;
    if (starts("metadata write failed:") || starts("metadata-only rewrite failed:"))
        return ErrorKind::MetaWrite;
    if (starts("source metadata unavailable"))
        return ErrorKind::MetaWrite;
    if (starts("output path") || starts("cannot create output directory") ||
        starts("pixel budget exceeded"))
        return ErrorKind::Io;
    if (starts("encode cancelled"))
        return ErrorKind::Cancelled;
    if (starts("unexpected exception") || starts("unexpected non-standard exception"))
        return ErrorKind::Internal;
    if (starts("no output configured") || starts("run_one_file must not be called") ||
        starts("metadata-only mode requires") || starts("unknown output format") ||
        starts("no encoder registered") || starts("output bit depth") || starts("format '"))
        return ErrorKind::Config;
    return ErrorKind::Unknown;
}

// R-168：终态聚合单源（run_one_file / run_metadata_only 共用；旧两份 finalize 合一）。
FileOutcome::Verdict verdict_of_rows(const FileResult &res) {
    std::size_t n_ok = 0, n_fail = 0;
    for (const OutputResult &o : res.outputs) {
        if (o.ok) {
            ++n_ok;
        } else if (!o.skipped) {
            ++n_fail;
        }
    }
    if (n_fail == 0 && n_ok > 0)
        return FileOutcome::Verdict::Done;
    if (n_fail == 0)
        return FileOutcome::Verdict::Skipped;
    if (n_ok == 0 && n_fail == res.outputs.size())
        return FileOutcome::Verdict::Failed;
    return FileOutcome::Verdict::DoneWithErrors;
}

void finalize_aggregate(FileOutcome &outcome, std::chrono::steady_clock::time_point t_start,
                        FileOutcome::Verdict v) {
    FileResult &res = outcome.file;
    res.t.total_ms = ms_since(t_start);
    std::size_t n_ok = 0, n_skip = 0, n_fail = 0;
    const OutputResult *first_fail = nullptr;
    for (const OutputResult &o : res.outputs) {
        if (o.ok) {
            ++n_ok;
        } else if (o.skipped) {
            ++n_skip;
        } else {
            ++n_fail;
            if (first_fail == nullptr)
                first_fail = &o;
        }
    }
    res.out_bytes = 0;
    for (const OutputResult &o : res.outputs)
        res.out_bytes += o.out_bytes;
    // 告警聚合：逐输出串联（全局告警在每行各有一份 → 按 kind+detail 去重）
    res.warnings.clear();
    for (const OutputResult &o : res.outputs) {
        for (const Warning &w : o.warnings) {
            const bool dup =
                std::any_of(res.warnings.begin(), res.warnings.end(), [&w](const Warning &e) {
                    return e.kind == w.kind && e.detail == w.detail;
                });
            if (!dup)
                res.warnings.push_back(w);
        }
    }
    res.out = res.outputs.empty() ? std::filesystem::path{} : res.outputs.front().out;
    res.ok = (n_fail == 0 && n_ok > 0);
    res.skipped = (n_fail == 0 && n_ok == 0 && n_skip > 0);
    res.cancelled = (v == FileOutcome::Verdict::Cancelled);
    if (res.ok || res.skipped) {
        res.error.clear();
    } else if (res.error.empty() && first_fail != nullptr) {
        res.error = first_fail->error;
        res.error_kind = first_fail->error_kind;
    }
    if (v == FileOutcome::Verdict::Cancelled) {
        // R-172：取消终态下未决行如实标 Cancelled（旧口径行停在 Pending 面）。
        for (OutputResult &o : res.outputs) {
            if (o.state == OutputState::Pending)
                o.state = OutputState::Cancelled;
        }
    }
    outcome.verdict = v;
}

// R-322 / R-136：split_by_format 的派生判据单源（模板**含** $format 段即 true；与
// page_output 的"含段"口径一致，消 CLI 的"仅前缀"分叉）。
bool template_has_format(std::string_view output_template) {
    return output_template.find("$format") != std::string_view::npos;
}

// R-173：RunConfig 字段约束校验器（空串 = OK）。消息文本与旧逐点检查逐字一致。
std::string validate_run_config(const RunConfig &cfg) {
    if (cfg.outputs.empty())
        return "no output configured (RunConfig::outputs is empty)";
    if (cfg.metadata_only && cfg.outputs.size() != 1)
        return "metadata-only mode requires exactly one output (RunConfig::outputs.size() == 1)";
    std::string tmpl_err;
    if (!validate_output_template(cfg.output_template, &tmpl_err))
        return tmpl_err;
    // split_by_format ↔ $format 联动（R-173/R-322）：true 要求模板含 $format 段；
    // false 恒过（字段是派生态，UI/CLI 落盘时以 template_has_format 回填）。
    if (cfg.split_by_format && !template_has_format(cfg.output_template))
        return "split_by_format=true requires a $format segment in output_template '" +
               cfg.output_template + "'";
    for (const OutputFormatSpec &spec : cfg.outputs) {
        const FormatDef *fmt = find_format(spec.format_id);
        if (!fmt)
            return "unknown output format '" + spec.format_id + "'";
        if (std::find(fmt->bitdepths.begin(), fmt->bitdepths.end(), spec.out_bitdepth) ==
            fmt->bitdepths.end())
            return "output bit depth " + std::to_string(spec.out_bitdepth) +
                   " is not supported by format '" + fmt->id + "'";
    }
    // M2-T5 §2.7 跨字段参数约束（逐输出求值，配置顺序的首条消息即失败原因）
    for (const OutputFormatSpec &spec : cfg.outputs) {
        const std::vector<std::string> msgs =
            cross_validate(spec.params, spec.format_id, spec.tech_id);
        if (!msgs.empty())
            return msgs.front();
    }
    return {};
}

// R-175：PathCtx 组装单源（scheduler desired_keys / pipeline plan_targets / run_metadata_only）。
// R-3xx（仅元数据格式跟随源）：metadata_only 下格式由**源扩展名**解析（不再沿用 UI/预设里
// 选中的目标格式）——输出容器、$format 段、扩展名三者同源，避免"选 jxl 跑仅元数据"时
// 引擎按 jxl 校验/命名而文件仍是 jpg 的两张皮。
const FormatDef *format_for_source_path(const std::filesystem::path &src) {
    std::string ext = src.extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "jpg" || ext == "jpeg")
        return find_format("jpeg");
    if (ext == "tif" || ext == "tiff")
        return find_format("tiff");
    if (ext == "heic" || ext == "heif")
        return find_format("heif");
    return find_format(ext); // png/webp/jxl/avif/bmp（gif/tga 无输出格式 → nullptr）
}

PathCtx path_ctx_for(const FileEntry &fe, const RunConfig &cfg, const FormatDef *fmt) {
    PathCtx ctx;
    if (cfg.metadata_only) {
        // 同容器零重编码：格式/扩展名都以源文件为准（fmt 入参可能是 UI 选中的目标格式）。
        const FormatDef *src_fmt = format_for_source_path(fe.src);
        ctx.format_dir = src_fmt ? src_fmt->id : std::string();
        ctx.ext = fe.src.extension().string();
    } else {
        ctx.format_dir = fmt ? fmt->id : std::string();
        ctx.ext = fmt ? fmt->ext : std::string();
    }
    ctx.rel_dir = relative_dir(fe.src, fe.base_dir);
    ctx.stem = fe.src.stem().string();
    return ctx;
}

int default_bitdepth_for(std::string_view format_id) {
    if (format_id == "jpeg")
        return 8;
    if (format_id == "jxl")
        return 16;
    if (format_id == "png")
        return 16;
    if (format_id == "tiff")
        return 16;
    if (format_id == "webp")
        return 8;
    if (format_id == "bmp")
        return 24;
    // HEIF-03：默认 10 位会让默认产物落到 RExt（profile_idc=4）；8 位才是 Main Still Picture。
    // AVIF 的 10 位仍是 AV1 Main，保持 10。
    if (format_id == "heif")
        return 8;
    if (format_id == "avif")
        return 10;
    return 8;
}

// B.6 ③：参数装配链单源（run_dev / bench / UI config_base 共用）。
// 逐输出：default_params(lossless) → base_params 逐键覆盖 → fill_defaults → apply_locks →
// validate_params → 位深 + fmt->bitdepths 归属校验。空串 = OK。
std::string build_output_specs(const std::vector<OutputSpecRequest> &reqs, bool lossless,
                               std::vector<OutputFormatSpec> &out) {
    out.clear();
    out.reserve(reqs.size());
    for (const OutputSpecRequest &r : reqs) {
        const FormatDef *fmt = find_format(r.format_id);
        if (!fmt)
            return "unknown format '" + r.format_id + "'";
        ParamSet params = default_params(*fmt, r.backend_id, r.tech_id, lossless);
        for (const auto &[k, v] : r.base_params)
            params[k] = v;
        fill_defaults(*fmt, r.backend_id, r.tech_id, lossless, params);
        // NOTE(0.3.0/§3.2 + M4-T13)：无损语义随 OutputFormatSpec.params 承载 —— 显式 schema
        // 参数 `lossless` + 内部管道键 `__lossless` 由参数引擎保持同步（apply_locks）。
        apply_locks(*fmt, r.backend_id, r.tech_id, lossless, params);
        const std::string verr = validate_params(*fmt, r.backend_id, r.tech_id, lossless, params);
        if (!verr.empty())
            return r.format_id + ": invalid parameters: " + verr;

        const int bitdepth =
            r.out_bitdepth > 0 ? r.out_bitdepth : default_bitdepth_for(r.format_id);
        if (std::find(fmt->bitdepths.begin(), fmt->bitdepths.end(), bitdepth) ==
            fmt->bitdepths.end())
            return "bit depth " + std::to_string(bitdepth) + " is not supported by format '" +
                   r.format_id + "'";
        out.push_back(OutputFormatSpec{r.format_id, r.backend_id, r.tech_id, params, bitdepth});
    }
    return {};
}

} // namespace pp
