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
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "codecs/encoders.h"
#include "core/colormanager.h"
#include "core/fsops.h"
#include "core/logger.h"
#include "core/metadata.h"
#include "core/params.h"
#include "core/pixelbudget.h"
#include "core/progress.h"
#include "core/simd/simd.h" // M4-W5-T19：flatten 热路径（§11.2）
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
    std::unique_ptr<ProgressMux> mux;

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
    WorkImage work;
    BudgetGuard budget; // ④ 的命名 RAII（acquire_budget 置位，ctx 析构释放）

    // ---- 收尾（旧 finalize/fail 两个内联 lambda 的单源化，R-168）----
    FileOutcome finalize(FileOutcome::Verdict v) {
        finalize_aggregate(outcome, t_start, v);
        return outcome;
    }
    FileOutcome fail(std::string msg) {
        fail_fill(outcome, t_start, std::move(msg));
        return outcome;
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
        mux = std::make_unique<ProgressMux>(inf.width, inf.height, outputs);
        mux->set_event_callback([this](const FileEvent &e) {
            if (ev.on_event)
                ev.on_event(e);
        });
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
    const ProbeOutcome po = probe_file(ctx.fe.src);
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
    // 工作缓冲与 decode 缓冲同尺寸（通道数取自 probe），故 2×frame 仍是本文件的峰值上界。
    const uint64_t need_bytes =
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
    if (!work_from_buf(ctx.work, dec.buf, werr)) {
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
        if (!orient_work(ctx.work, ctx.orientation, oerr)) {
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
    const int in_ch = ctx.work.spec.nchannels;
    if (in_ch == 2 || in_ch == 4) {
        ctx.stage(FileState::Flattening, Stage::Flatten, -1);
        ctx.mux->shared_stage(Stage::Flatten, 0.f);
        const Clock::time_point ft0 = Clock::now();
        std::string ferr;
        if (!flatten_alpha_in_place(ctx.work, static_cast<float>(ctx.cfg.flatten_gray), ferr)) {
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
        log_debug(
            kStage, kFile, "alpha flattened before color",
            {{"src", ctx.fe.src.string()},
             {"channels", std::to_string(in_ch) + " -> " + std::to_string(ctx.work.spec.nchannels)},
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
        OIIO::ImageBuf cbuf = ctx.work.buf;
        const void *const work_px = ctx.work.px.data();
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
            if (!work_from_buf(ctx.work, cbuf, cerr)) {
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

// ---- ⑨ encode_loop（多输出循环整体下沉：排序/flatten 边界/逐行失败隔离/break 扫尾）----
bool stage_encode_loop(RunFileContext &ctx) {
    std::vector<std::size_t> order(ctx.targets.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;

    for (std::size_t pos = 0; pos < order.size(); ++pos) {
        const std::size_t idx = order[pos];
        const PlannedTarget &pt = ctx.targets[idx];
        OutputResult &row = ctx.res.outputs[pt.cfg_index];
        if (pt.skip) {
            set_output_state(row, OutputState::Skipped); // R-172 单点迁移
            ctx.mux->on_output_skipped(pt.cfg_index);    // §7.2：该输出全段记满（文件整体可达 1.0）
            continue; // 跳过行不参与 encode/告警（0.2 的 Skip 语义）
        }
        // W1-T6：该输出进入编码段（§7.2 的 50%+10% 块）。绑定必须早于本输出的任何阶段
        // 事件 —— 合成标识（§7.3）以"格式是否在 k 表内"预置，编码返回后再按
        // `progress_reported` 校正；先绑定可保证该输出的**所有**事件 synthetic 口径一致。
        const ProgressFn out_progress = ctx.mux->bind_output(pt.cfg_index, pt.target.format_id);
        const bool synth_pump = ctx.mux->synth_active(pt.cfg_index);

        if (is_cancelled(ctx.cancelled)) {
            ctx.finalize(FileOutcome::Verdict::Cancelled);
            return false;
        }

        // ---- bit depth note (§5.7) ----
        // BMP-02：BMP 的 out_bitdepth 是 bpp（24），其余格式是 bps；必须换算到同单位再比较，
        // 否则 16-bit 源 → BMP 24bpp（实际 8bps）不会告警。
        const int out_bps = bitdepth_bps(pt.fmt->id, pt.target.out_bitdepth);
        if (ctx.info.src_bitdepth > out_bps) {
            row.warnings.push_back(
                Warning{WarningKind::DepthDowngrade,
                        "source bit depth " + std::to_string(ctx.info.src_bitdepth) +
                            " -> output bit depth " + std::to_string(out_bps) + " bps"});
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
        const int enc_threads = encode_threads_of(ctx.ev.scope);
        EncodeRequest req{ctx.work.buf,         pt.target,    ctx.meta,
                          ctx.cancelled,        out_progress, enc_threads,
                          ctx.cfg.color_dither8};
        EncodeResult er;
        {
            SynthPump pump(ctx.mux.get(), synth_pump);
            er = pt.enc->encode(req);
        }
        row.t.encode_ms = ms_since(t0);
        ctx.res.t.encode_ms += row.t.encode_ms;
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
            const bool cancelled_row = is_cancelled(ctx.cancelled) ||
                                       er.error.find("encode cancelled") != std::string::npos;
            if (cancelled_row) {
                row.error = "encode cancelled"; // 规范化：error_kind_of 的既有判据即命中 Cancelled
                row.error_kind = error_kind_of(row.error);
                set_output_state(row, OutputState::Cancelled);
                ctx.mux->on_output_failed(pt.cfg_index); // 进度停在已完成，不虚增
                log_info(kStage, kFile, "encode cancelled",
                         {{"src", ctx.fe.src.string()},
                          {"out", row.out.string()},
                          {"detail", er.error}});
                ctx.finalize(FileOutcome::Verdict::Cancelled);
                return false; // 取消：不再处理后续 target（与各阶段检查点同语义）
            }
            row.error = "encode failed: " +
                        (er.error.empty() ? std::string("encoder produced no data") : er.error);
            row.error_kind = ErrorKind::Encode;
            set_output_state(row, OutputState::Failed);
            log_error(kStage, kFile, row.error,
                      {{"src", ctx.fe.src.string()}, {"out", row.out.string()}});
            continue; // 该输出失败：其余输出继续（§4.1 任一失败=DoneWithErrors）
        }
        row.out_bytes = er.bytes;

        // ---- metadata write (§5.8, §4.8) ----
        ctx.stage(FileState::Writing, Stage::MetaWrite, static_cast<int>(pt.cfg_index));
        ctx.mux->output_stage(pt.cfg_index, Stage::MetaWrite);
        t0 = Clock::now();
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
        ctx.res.t.metawrite_ms += row.t.metawrite_ms;
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
            continue;
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
        set_output_state(row, OutputState::Ok); // R-172 单点迁移
        ctx.mux->on_output_metawrite_done(
            pt.cfg_index); // §7.2 末行：metawrite + mtime + 落盘 = 10%
    }
    return true;
}

// ---- ⑩ finish_file（mtime + 收尾样本 + 逐输出/整文件日志 + aggregate 单源聚合）----
void stage_finish(RunFileContext &ctx) {
    // mtime sync (§5.9)：生效 DateTimeOriginal 只算一次，逐输出落盘一次
    if (ctx.cfg.rules.sync_mtime && !ctx.meta_plan.datetime_original.empty()) {
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
    std::unique_ptr<ProgressMux> mux;
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
    const auto fail = [&t_start, &outcome](std::string msg) -> FileOutcome {
        fail_fill(outcome, t_start, std::move(msg)); // R-168：与 run_one_file 同源收尾
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
        if (is_cancelled(cancelled))
            return finalize(FileOutcome::Verdict::Cancelled);

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

        if (cfg.rules.sync_mtime && !meta_plan.datetime_original.empty()) {
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
