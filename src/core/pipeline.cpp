// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M4-T5: multi-output single-file pipeline
// (docs/v0.3.0-design.md §3.2 / §4.1 / §4.2 / §4.3 / §4.4).
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
//     后再从同一块内存读回 = 全黑图（静默数据损失，见下方颜色段的逐条注释）。
//   * 逐 target 串行（同文件内），文件间并行由 scheduler 管。
//
// 编码顺序（§4.3）：supports_alpha 的 target 在前、false 的在后（稳定排序），保证做 alpha 的
// 格式先拿到未拍平的像素；FileResult.outputs 仍按**配置顺序**记录（编码顺序是实现细节）。
//
// 其余冻结语义沿用 0.2（§4.8 gray/ICC、§3.5⑥ alpha、第 4 章 metadata、§3.4 reserved）：
//   * gray 源进入不支持灰度的格式 → Warning{GrayToRgbEncoded}；多输出时"任一 target 不支持
//     灰度"即以 sRGB 为有效目标变换一次（color 全局共享，D5）；
//   * alpha → 不支持 alpha 的 target 报 Warning{AlphaFlattened}；
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
//     （§7.4，**不逐行落盘**；dev 侧车另落 progress-trace 事件流，见 main.cpp）。

#include "core/pipeline.h"

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#include <algorithm>
#include <atomic>
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
// 缓冲（3/4 通道原地写回时不调用本函数 —— 见 run_one_file 的颜色段注释）。
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
// 覆盖尚未读取的源像素（alpha 分量先读后写，同像素内亦安全）。像素值与 0.2 逐位相同。
//
// M4-W5-T19 接线（design §11.2 热路径表首行）：合成循环改由 **pp::simd::flatten**（唯一
// 运行期分派层，cpu_has_avx2() → avx2 / ref）承载 —— AVX2 向量 + FMA（具体迭代形态与
//   三组候选的实测对照见 core/simd/kernels.cpp）。语义逐条不变：
//   同一条式子（a = clamp(alpha,0,1)；c*a + bg*(1-a)）、同一底色 clamp、`src == dst` 的
//   原地缩通道形态在 simd 层显式支持（simd.h 的别名契约）。唯一差异 = 乘加合一（FMA 单次
//   舍入）⇒ 末位可能与 0.2 的 mul+add 不同；金样 20 对（含 alpha-jpeg / multiformat-split
//   的 flatten 产物）为该口径的裁判，实测全绿（T19 selfChecks）。
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

// M2-T5 §2.7 (--dev 校验调用点): cross-field constraints the per-key predicates cannot
// express. 0.3.0 多输出口径：逐输出求值，**配置顺序的首条消息**即该文件的失败原因。
// Empty first message = OK.
std::string first_cross_error(const RunConfig &cfg) {
    for (const OutputFormatSpec &spec : cfg.outputs) {
        const std::vector<std::string> msgs =
            cross_validate(spec.params, spec.format_id, spec.tech_id);
        if (!msgs.empty())
            return msgs.front();
    }
    return {};
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
bool plan_targets(const FileEntry &fe, const RunConfig &cfg,
                  const std::vector<std::filesystem::path> &reserved,
                  std::vector<PlannedTarget> &out, std::vector<OutputResult> &rows,
                  std::string &err) {
    namespace fs = std::filesystem;
    out.clear();
    out.reserve(cfg.outputs.size());
    std::vector<fs::path> taken = reserved; // 本文件内逐输出累积的 reserved 视图
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
        std::unique_ptr<IEncoder> enc;
        if (cfg.metadata_only) {
            if (!format_supports_metadata_only(fmt->id)) {
                err = "format '" + fmt->id +
                      "' does not support metadata-only rewrite (zero re-encode)";
                return false;
            }
        } else {
            enc = make_encoder(spec.format_id, spec.backend_id);
            if (!enc) {
                err = "no encoder registered for format '" + spec.format_id + "' (backend '" +
                      spec.backend_id + "')";
                return false;
            }
        }

        PathCtx ctx;
        ctx.format_dir = fmt->id;
        ctx.rel_dir = relative_dir(fe.src, fe.base_dir);
        ctx.stem = fe.src.stem().string();
        // 仅元数据模式：同一容器 → 保持源扩展名（0.2 的 mirror_path(..., "") 口径）
        ctx.ext = cfg.metadata_only ? fe.src.extension().string() : fmt->ext;
        const fs::path desired = render_output_path(cfg.output_template, ctx, cfg.out_root);
        if (desired.empty()) {
            err = "output path: template '" + cfg.output_template + "' produced no file name";
            return false;
        }
        std::string conflict_err;
        const OutputPlan plan = resolve_conflict(desired, cfg.conflict, taken, conflict_err);
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
        pt.target.out_bitdepth = spec.out_bitdepth;
        pt.target.out_path = plan.out_path;
        pt.target.supports_alpha = fmt->supports_alpha;
        out.push_back(std::move(pt));
        row.out = plan.out_path;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// §3.9 frozen predicate (strong definition overriding the weak fallback in metadata.cpp)
// ---------------------------------------------------------------------------
bool format_supports_metadata_only(std::string_view format_id) {
    return detail_metadata_only_supported(format_id);
}

// ---------------------------------------------------------------------------
// run_one_file（多输出循环，§4）
// ---------------------------------------------------------------------------
FileOutcome run_one_file(const FileEntry &fe, const RunConfig &cfg, EventFn ev) {
    namespace fs = std::filesystem;

    FileOutcome outcome;
    FileResult &res = outcome.file;
    res.src = fe.src;
    const Clock::time_point t_start = Clock::now();

    const std::function<bool()> cancelled =
        (ev.scope != nullptr) ? ev.scope->cancelled : std::function<bool()>();
    const std::vector<fs::path> reserved =
        (ev.scope != nullptr) ? ev.scope->reserved : std::vector<fs::path>{};
    // W1-T6（§3.3/§7.1–§7.4）：进度枢纽。probe 之后建立（合成估算需要像素尺寸）；
    // 在此之前的事件（Queued/Probing 的首次）progress 取默认值（overall=0）。
    std::unique_ptr<ProgressMux> mux;
    // 阶段事件携带当前进度快照（§3.3：progress 为追加字段，Progress 状态为其主用途；
    // 阶段事件带上快照可让消费端一次拿到"阶段文本 + 整体位置"，消费端按 state 分派）。
    const auto stage = [&ev, &mux](FileState s, Stage st, int output_index) {
        if (!ev.on_event)
            return;
        FileEvent e{};
        e.index = 0; // 批内下标由调用方（Scheduler）回填
        e.state = s;
        e.result = nullptr;
        if (mux)
            e.progress = mux->snapshot(st, output_index);
        ev.on_event(e);
    };
    const auto make_mux = [&mux, &ev](const ImageInfo &info, std::size_t outputs) {
        mux = std::make_unique<ProgressMux>(info.width, info.height, outputs);
        mux->set_event_callback([&ev](const FileEvent &e) {
            if (ev.on_event)
                ev.on_event(e);
        });
    };

    // 聚合终态 + FileResult 聚合字段（v0.2 单输出逐字等价：见 pipeline.h 的加法说明）。
    const auto finalize = [&res, &t_start, &outcome](FileOutcome::Verdict v) -> FileOutcome {
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
        res.out = res.outputs.empty() ? fs::path{} : res.outputs.front().out;
        res.ok = (n_fail == 0 && n_ok > 0);
        res.skipped = (n_fail == 0 && n_ok == 0 && n_skip > 0);
        res.cancelled = (v == FileOutcome::Verdict::Cancelled);
        if (res.ok || res.skipped) {
            res.error.clear();
        } else if (res.error.empty() && first_fail != nullptr) {
            res.error = first_fail->error;
        }
        outcome.verdict = v;
        return outcome;
    };

    const auto fail = [&res, &finalize](std::string msg) -> FileOutcome {
        res.error = msg;
        for (OutputResult &o : res.outputs) {
            if (!o.ok && !o.skipped && o.error.empty())
                o.error = msg;
        }
        log_error(kStage, kFile, msg, {{"src", res.src.string()}});
        return finalize(FileOutcome::Verdict::Failed);
    };

    // 逐输出的行记录（配置顺序）；失败路径也保留行（UI/sidecar 需要可见的逐输出状态）
    for (const OutputFormatSpec &spec : cfg.outputs) {
        OutputResult row;
        row.format_id = spec.format_id;
        row.backend_id = spec.backend_id;
        row.tech_id = spec.tech_id;
        res.outputs.push_back(std::move(row));
    }

    const auto verdict_of_rows = [&res]() -> FileOutcome::Verdict {
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
    };

    try {
        // ---- 0. 配置硬校验（§3.2：outputs ≥ 1；仅元数据模式是另一个入口）----
        if (cfg.outputs.empty())
            return fail("no output configured (RunConfig::outputs is empty)");
        if (cfg.metadata_only)
            return fail("run_one_file must not be called in metadata-only mode "
                        "(use run_metadata_only; RunConfig::outputs.size() must be 1 there)");
        std::string tmpl_err;
        if (!validate_output_template(cfg.output_template, &tmpl_err))
            return fail(tmpl_err);

        // ---- 1. cross-field parameter constraints (§2.7, M2-T5): fail with the first message ----
        if (const std::string cross_err = first_cross_error(cfg); !cross_err.empty())
            return fail(cross_err);

        // ---- 2. probe (§5.1): spec only, no pixels ----
        // NOTE(复核项 7)：§4.1 的"probe(一次)"在落地形态下是**每文件两次 spec 级探测** ——
        // Scheduler 在取件时先探一次（早失败 + 预算/串行化决策，并把结果回填自己的 FileEntry），
        // §3.2 的 `const FileEntry&` 使 pipeline 无法复用那份结果，故此处再探一次（只读 spec、
        // 无像素工作，代价为一次头部解析）。0.2 同形（scheduler.cpp:157 + pipeline.cpp:265）。
        stage(FileState::Probing, Stage::Probe, -1);
        const ProbeOutcome po = probe_file(fe.src);
        if (!po.error.empty())
            return fail("probe failed: " + po.error);
        const ImageInfo info = po.info;
        res.info = info;
        // W1-T6：进度枢纽就位（§7.2 共享段从 probe 起算；§7.3 合成估算需要 pixels）
        make_mux(info, cfg.outputs.size());
        mux->shared_stage(Stage::Probe, 0.f);
        const OIIO::ImageSpec *spec = po.first_spec ? &*po.first_spec : nullptr;
        const int orientation = spec ? orientation_from_spec(*spec) : 1;
        std::string src_icc = spec ? icc_from_spec(*spec) : std::string();

        // EXIF summary (time / GPS / ICC / Orientation). A read failure is NOT fatal (§4.8).
        SourceMeta srcmeta = read_metadata(fe.src);
        if (!srcmeta.error.empty())
            log_warn(kStage, kFile, "source metadata unavailable; continuing with empty metadata",
                     {{"src", fe.src.string()}, {"error", srcmeta.error}});
        if (src_icc.empty())
            src_icc = srcmeta.icc; // R13 chain: embedded ICC first
        // ---- R13 middle step (issue #7, M2-T6 §2.8): CICP → source profile ----
        // See the M2-T6 landing note: OIIO always publishes a JXL ICCProfile (synthesized from
        // CICP), so CICP presence — not an empty src_icc — marks a source without its own ICC.
        if (spec != nullptr && info.channels >= 3 &&
            (info.format == "jpegxl" || info.format == "jxl")) {
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
                    log_info(kStage, kFile, mapped.log_line, {{"src", fe.src.string()}});
                } else {
                    log_warn(kStage, kFile, mapped.log_line, {{"src", fe.src.string()}});
                }
                if (!mapped.src_icc.empty())
                    src_icc = mapped.src_icc;
            }
        }
        log_debug(kStage, kFile, "probe done",
                  {{"src", fe.src.string()},
                   {"size", std::to_string(info.width) + "x" + std::to_string(info.height)},
                   {"channels", std::to_string(info.channels)},
                   {"bitdepth", std::to_string(info.src_bitdepth)},
                   {"orientation", std::to_string(orientation)},
                   {"icc", src_icc.empty() ? "none" : std::to_string(src_icc.size()) + "B"},
                   {"multipage", info.is_multipage ? "true" : "false"},
                   {"outputs", std::to_string(cfg.outputs.size())}});

        // ---- 3. 逐输出目标计划：格式/位深/编码器 + 路径模板 + 独立冲突解析 ----
        std::vector<PlannedTarget> targets;
        if (std::string plan_err; !plan_targets(fe, cfg, reserved, targets, res.outputs, plan_err))
            return fail(plan_err);

        // 全跳过（Skip 策略命中每一个输出）→ 不解码、不取预算（0.2 行为）
        const bool all_skipped = std::all_of(targets.begin(), targets.end(),
                                             [](const PlannedTarget &t) { return t.skip; });
        if (all_skipped) {
            for (OutputResult &row : res.outputs)
                row.skipped = true; // 逐输出行标记（聚合判定与 sidecar 都要看它）
            log_info(kStage, kFile, "skipped: output exists",
                     {{"src", fe.src.string()}, {"out", res.outputs.front().out.string()}});
            return finalize(FileOutcome::Verdict::Skipped);
        }

        // 输出目录（逐 target；0.2 在解码前创建）
        for (const PlannedTarget &pt : targets) {
            if (pt.skip)
                continue;
            std::error_code ec;
            const fs::path parent = pt.target.out_path.parent_path();
            if (!parent.empty())
                fs::create_directories(parent, ec);
            if (ec)
                return fail("cannot create output directory '" + parent.string() +
                            "': " + ec.message());
        }

        // ---- 4. pixel budget (§3.3/§3.10): 2× float32 frame (rotate/composite peak, G2) ----
        // 工作缓冲与 decode 缓冲同尺寸（通道数取自 probe），故 2×frame 仍是本文件的峰值上界。
        const uint64_t need_bytes =
            2 * PixelBudget::frame_bytes(info.width, info.height, info.channels);
        bool budget_held = false;
        struct BudgetGuard {
            PixelBudget *b;
            uint64_t bytes;
            bool &held;
            ~BudgetGuard() {
                if (held && b)
                    b->release(bytes);
            }
        } budget_guard{ev.scope != nullptr ? ev.scope->budget : nullptr, need_bytes, budget_held};
        if (ev.scope != nullptr && ev.scope->budget != nullptr) {
            if (!ev.scope->budget->acquire(need_bytes, cancelled)) {
                if (is_cancelled(cancelled)) {
                    return finalize(FileOutcome::Verdict::Cancelled);
                }
                return fail("pixel budget exceeded: need " + std::to_string(need_bytes) +
                            " B, capacity " + std::to_string(ev.scope->budget->capacity()) + " B");
            }
            budget_held = true;
            // Drumbeat evidence: the 2× frame peak reservation actually made it through the pool.
            log_info(kStage, kFile, "pixel budget acquired",
                     {{"src", fe.src.string()},
                      {"need_bytes", std::to_string(need_bytes)},
                      {"capacity_bytes", std::to_string(ev.scope->budget->capacity())},
                      {"used_bytes", std::to_string(ev.scope->budget->used())},
                      {"peak_bytes", std::to_string(ev.scope->budget->peak())}});
        }
        if (is_cancelled(cancelled))
            return finalize(FileOutcome::Verdict::Cancelled);
        // W1-T6（§7.2 首行）：probe + acquire 完成 → 3%
        mux->shared_stage(Stage::Probe, 1.f);

        // ---- 5. decode (§5.2) → 工作缓冲（此后 decode 缓冲立即释出；峰值 = 2×frame）----
        stage(FileState::Decoding, Stage::Decode, -1);
        mux->shared_stage(Stage::Decode, 0.f);
        Clock::time_point t0 = Clock::now();
        DecodeOutcome dec = decode_float(fe.src, info);
        res.t.decode_ms = ms_since(t0);
        if (!dec.error.empty())
            return fail("decode failed: " + dec.error);
        WorkImage work;
        std::string werr;
        if (!work_from_buf(work, dec.buf, werr))
            return fail("decode failed: " + werr);
        // W1-T6（§7.2 decode 行）：本仓库的解码入口（src/decode/oiio_reader.h 的 decode_float）
        // **不暴露读行回调**（该文件不在 T6 文件面内 → 不改签名）→ 按 §7.2 的"无回调格式即时 1
        // 并日志注明"落到即时 1，并在此如实记一条 debug（fmt/尺寸可见）。
        mux->shared_stage(Stage::Decode, 1.f);
        log_debug(kStage, kFile, "decode progress: immediate 1 (no read-row callback exposed)",
                  {{"src", fe.src.string()},
                   {"format", info.format},
                   {"decode_ms", fmt_double(res.t.decode_ms)}});
        // decode 告警（MultipageTruncated 等）随解码缓冲一起转移，再释出缓冲
        const std::vector<Warning> decode_warnings = std::move(dec.warnings);
        dec = DecodeOutcome{}; // 释放 decode 缓冲（工作缓冲已就位；峰值回到 1×frame）

        // 工作缓冲里不属于单一 target 的告警（decode/color）按出现时刻推入每个未跳过行：
        // 单输出时与 0.2 的 res.warnings 序列逐字一致。
        const auto push_global = [&res, &targets](const std::vector<Warning> &ws) {
            for (const Warning &w : ws) {
                for (const PlannedTarget &t : targets) {
                    if (!t.skip)
                        res.outputs[t.cfg_index].warnings.push_back(w);
                }
            }
        };
        push_global(decode_warnings);

        // ---- 6. orient (§5.3) ----
        bool rotated = false;
        if (cfg.rotate_orientation && orientation != 1) {
            stage(FileState::Orienting, Stage::Orient, -1);
            mux->shared_stage(Stage::Orient, 0.f);
            t0 = Clock::now();
            std::string oerr;
            if (!orient_work(work, orientation, oerr))
                return fail("orient failed: " + oerr);
            res.t.orient_ms = ms_since(t0);
            rotated = true;
            mux->shared_stage(Stage::Orient, 1.f);
        } else {
            mux->shared_skip(Stage::Orient); // 不执行 → 权重不丢失（§7.2 共享段恒达 40%）
        }
        if (is_cancelled(cancelled))
            return finalize(FileOutcome::Verdict::Cancelled);

        // ---- 7. color (§5.4 + §4.8 gray/ICC rulings；全局目标，一次，多输出共享 D5) ----
        const bool src_is_gray = (info.channels == 1 || info.channels == 2);
        // 灰度源：任一 target 不支持灰度 → 报 GrayToRgbEncoded，并把有效目标推成 sRGB
        // （多输出下"一次变换、全体共享"，行为对单输出与 0.2 逐字一致）。
        bool any_target_needs_rgb = false;
        for (const PlannedTarget &pt : targets) {
            if (pt.skip || !src_is_gray || pt.fmt->supports_gray)
                continue;
            any_target_needs_rgb = true;
            res.outputs[pt.cfg_index].warnings.push_back(
                Warning{WarningKind::GrayToRgbEncoded,
                        "grayscale source encoded as RGB for format '" + pt.fmt->id + "'"});
        }
        ColorTarget eff_target = cfg.color;
        if (src_is_gray && any_target_needs_rgb && eff_target == ColorTarget::KeepOriginal) {
            // Grayscale pixels must not reach webp/heif/avif: use sRGB as the effective target
            // even though the user kept the original (§4.8).
            eff_target = ColorTarget::SRGB;
        }
        // 复核项 9：混合灰度支持的多输出里，color 是全局一次（D5）⇒ 支持灰度的 target 也被动
        // 升到 RGB。它自己的格式并非"不支持灰度"，故与上面的告警措辞区分开、如实补一条
        // （单输出 / 全支持灰度的组合不触发 → 0.2 语义逐字不变）。
        if (src_is_gray && any_target_needs_rgb && eff_target != ColorTarget::KeepOriginal) {
            for (const PlannedTarget &pt : targets) {
                if (pt.skip || !pt.fmt->supports_gray)
                    continue;
                res.outputs[pt.cfg_index].warnings.push_back(Warning{
                    WarningKind::GrayToRgbEncoded,
                    "grayscale source encoded as RGB for format '" + pt.fmt->id +
                        "' (shared color transform: another output cannot carry grayscale)"});
            }
        }
        std::string icc_to_embed = src_icc;
        if (eff_target != ColorTarget::KeepOriginal) {
            stage(FileState::Coloring, Stage::Color, -1);
            mux->shared_stage(Stage::Color, 0.f);
            t0 = Clock::now();
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
            OIIO::ImageBuf cbuf = work.buf;
            const void *const work_px = work.px.data();
            ColorOutcome co =
                ColorManager::instance().transform(cbuf, src_icc, src_is_gray, eff_target);
            res.t.color_ms = ms_since(t0);
            if (!co.error.empty())
                return fail("color transform failed: " + co.error);
            push_global(co.warnings);
            icc_to_embed = co.icc_to_embed;
            res.color_src = co.src_desc;
            res.color_dst = co.dst_desc;
            if (cbuf.localpixels() != work_px) {
                std::string cerr;
                if (!work_from_buf(work, cbuf, cerr))
                    return fail("color transform failed: " + cerr);
                cbuf.clear();
            }
            mux->shared_stage(Stage::Color, 1.f);
        } else {
            // Pixels untouched: keep the source profile as-is (T4 ruling ③).
            res.color_src = src_icc.empty() ? (src_is_gray ? "assumed gray sRGB" : "assumed sRGB")
                                            : "ICC(source)";
            res.color_dst = "keep";
            mux->shared_skip(Stage::Color); // 不执行 → 权重不丢失（共享段收口 = 40%）
        }
        log_debug(kStage, kFile, "color decision",
                  {{"src", res.color_src},
                   {"dst", res.color_dst},
                   {"embed_icc",
                    icc_to_embed.empty() ? "none" : std::to_string(icc_to_embed.size()) + "B"}});

        // ---- 8. metadata plan / payloads (§3.7, §4.8)：全局一次，逐输出共享 ----
        MetadataPlan meta_plan = build_plan(srcmeta, cfg.rules, fe.exception);
        if (rotated && !meta_plan.exif.empty())
            meta_plan.exif["Exif.Image.Orientation"] = static_cast<uint16_t>(1); // §5.3
        const Payloads payloads = make_payloads(meta_plan);
        MetadataPayloads meta;
        meta.exif_blob = payloads.exif_blob; // consumed by JXL/HEIF/AVIF only (E7)
        meta.xmp_rdf = payloads.xmp_rdf;
        meta.icc_profile = icc_to_embed;

        // ---- 9. 编码循环（§4.3 排序：supports_alpha 的在前，稳定）----
        std::vector<std::size_t> order(targets.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&targets](std::size_t a, std::size_t b) {
            return targets[a].target.supports_alpha && !targets[b].target.supports_alpha;
        });

        const bool image_has_alpha = (work.spec.nchannels == 2 || work.spec.nchannels == 4);
        for (std::size_t pos = 0; pos < order.size(); ++pos) {
            const std::size_t idx = order[pos];
            const PlannedTarget &pt = targets[idx];
            OutputResult &row = res.outputs[pt.cfg_index];
            if (pt.skip) {
                row.skipped = true;
                mux->on_output_skipped(pt.cfg_index); // §7.2：该输出全段记满（文件整体可达 1.0）
                continue; // 跳过行不参与 flatten/告警（0.2 的 Skip 语义）
            }
            // W1-T6：该输出进入编码段（§7.2 的 50%+10% 块）。绑定必须早于本输出的任何阶段
            // 事件 —— 合成标识（§7.3）以"格式是否在 k 表内"预置，编码返回后再按
            // `progress_reported` 校正；先绑定可保证该输出的**所有**事件 synthetic 口径一致。
            const ProgressFn out_progress = mux->bind_output(pt.cfg_index, pt.target.format_id);
            const bool synth_pump = mux->synth_active(pt.cfg_index);

            // §4.3：最后一个 alpha-preserving target 之后、首个 alpha-dropping target 之前原地
            // flatten
            const int cur_ch = work.spec.nchannels;
            const bool buf_has_alpha = (cur_ch == 2 || cur_ch == 4);
            if (image_has_alpha && !pt.target.supports_alpha) {
                if (buf_has_alpha) {
                    stage(FileState::Flattening, Stage::Flatten, static_cast<int>(pt.cfg_index));
                    mux->output_stage(pt.cfg_index, Stage::Flatten);
                    t0 = Clock::now();
                    std::string ferr;
                    if (!flatten_alpha_in_place(work, static_cast<float>(cfg.flatten_gray), ferr)) {
                        // 共享工作缓冲已不可信 → 本 target 与其余未处理 target 全部按输出级失败
                        // 记录，再走聚合（§4.1：此时若已有输出成功 = DoneWithErrors，复核项 5）。
                        const std::string msg = "flatten failed: " + ferr;
                        log_error(kStage, kFile, msg, {{"src", fe.src.string()}});
                        for (std::size_t rest = pos; rest < order.size(); ++rest) {
                            OutputResult &r2 = res.outputs[targets[order[rest]].cfg_index];
                            if (!r2.ok && !r2.skipped && r2.error.empty())
                                r2.error = msg;
                        }
                        break;
                    }
                    res.t.flatten_ms = ms_since(t0);
                }
                row.warnings.push_back(Warning{WarningKind::AlphaFlattened,
                                               "alpha composited onto background " +
                                                   fmt_double(cfg.flatten_gray) + " for format '" +
                                                   pt.target.format_id + "'"});
            }
            if (is_cancelled(cancelled))
                return finalize(FileOutcome::Verdict::Cancelled);

            // ---- bit depth note (§5.7) ----
            if (info.src_bitdepth > pt.target.out_bitdepth) {
                row.warnings.push_back(
                    Warning{WarningKind::DepthDowngrade,
                            "source bit depth " + std::to_string(info.src_bitdepth) +
                                " -> output bit depth " + std::to_string(pt.target.out_bitdepth)});
            }

            // ---- encode (§5.7)：单输出一次调用（§3.1）----
            stage(FileState::Encoding, Stage::Encode, static_cast<int>(pt.cfg_index));
            t0 = Clock::now();
            // EncodeRequest 字段序（encoder.h §3.1）：img, target, meta, cancelled, progress,
            // encode_threads。W1-T6：progress = ProgressMux 的输出级回调（真实行级 → 直接汇流；
            // 无回调格式 → 走合成估算，编码期间由 SynthPump 按 20ms 采样，§7.3）；
            // encode_threads = W1-T7 起由调度器 E3 分配（`RunScope::encode_threads`，§8.2），
            // 无 scope（单测直调）恒 1 = 0.2 行为。
            const int enc_threads = encode_threads_of(ev.scope);
            EncodeRequest req{work.buf, pt.target, meta, cancelled, out_progress, enc_threads};
            EncodeResult er;
            {
                SynthPump pump(mux.get(), synth_pump);
                er = pt.enc->encode(req);
            }
            row.t.encode_ms = ms_since(t0);
            res.t.encode_ms += row.t.encode_ms;
            merge_warnings(row.warnings, er.warnings);
            const bool enc_ok = er.error.empty() && er.bytes != 0;
            // 先按 §3.1 口径校正 mux 的合成标识（progress_reported=false → 该输出标合成），
            // 再取快照 —— 快照里的 synthetic/reported 即该输出的**最终**口径（不写两次）。
            if (enc_ok)
                mux->on_output_encode_done(pt.cfg_index, er.progress_reported);
            else
                mux->on_output_failed(pt.cfg_index); // 失败：停在已完成进度，不虚增
            // §7.4 进度快照（debug 级，每输出一条；**不逐行落盘**）：progress_max_row /
            // progress_reported 是全仓进度口径的两个事实键，另附 samples/synthetic/est_ms 便于
            // 校准 k 表（§7.3 "k 校准表随日志实测回归校正"）。
            const ProgressMux::Stats pstats = mux->stats(pt.cfg_index);
            log_debug(kStage, kFile, "progress snapshot",
                      {{"src", fe.src.string()},
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
                row.error = "encode failed: " +
                            (er.error.empty() ? std::string("encoder produced no data") : er.error);
                row.ok = false;
                log_error(kStage, kFile, row.error,
                          {{"src", fe.src.string()}, {"out", row.out.string()}});
                continue; // 该输出失败：其余输出继续（§4.1 任一失败=DoneWithErrors）
            }
            row.out_bytes = er.bytes;

            // ---- metadata write (§5.8, §4.8) ----
            stage(FileState::Writing, Stage::MetaWrite, static_cast<int>(pt.cfg_index));
            mux->output_stage(pt.cfg_index, Stage::MetaWrite);
            t0 = Clock::now();
            std::string meta_err;
            // M2-T4 (#6/#8): the writer's explicit failure/degradation channel; plan.warnings
            // below stays byte-identical to M1 (semantic freeze) — the two are merged with dedup.
            std::vector<std::string> writer_warnings;
            if (pt.fmt->meta_path == "exiv2") {
                meta_err = write_metadata_exiv2(row.out, meta_plan, payloads, &writer_warnings);
            }
            merge_warnings(row.warnings, meta_plan.warnings); // legacy channel (frozen content)
            merge_writer_warnings(row.warnings, writer_warnings);
            if (!meta_err.empty()) {
                row.warnings.push_back(Warning{WarningKind::MetadataDropped, meta_err});
                log_warn(kStage, kFile, "metadata write failed (non-fatal)",
                         {{"out", row.out.string()}, {"error", meta_err}});
            } else if (pt.fmt->meta_path == "none") {
                // BMP has no metadata container; T5 skips silently, the pipeline reports it (§4.8).
                row.warnings.push_back(Warning{WarningKind::MetadataDropped,
                                               "bmp output carries no metadata container"});
            }
            row.t.metawrite_ms = ms_since(t0);
            res.t.metawrite_ms += row.t.metawrite_ms;
            row.ok = true;
            mux->on_output_metawrite_done(
                pt.cfg_index); // §7.2 末行：metawrite + mtime + 落盘 = 10%
        }

        // ---- 10. mtime sync (§5.9)：生效 DateTimeOriginal 只算一次，逐输出落盘一次 ----
        if (cfg.rules.sync_mtime && !meta_plan.datetime_original.empty()) {
            for (const OutputResult &row : res.outputs) {
                if (!row.ok)
                    continue;
                const std::string merr = sync_file_mtime(row.out, meta_plan.datetime_original);
                if (!merr.empty()) {
                    log_warn(kStage, kFile, "mtime sync failed",
                             {{"out", row.out.string()}, {"error", merr}});
                }
            }
        }
        // W1-T6：收尾样本（成功路径此处 file_frac 已按 §7.2 达 1.0；失败路径不虚增）
        mux->finish();

        for (const OutputResult &row : res.outputs) {
            log_info(kStage, kFile, "output done",
                     {{"src", fe.src.string()},
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
                    {{"src", fe.src.string()}, {"out", row.out.string()}, {"detail", w.detail}});
            }
        }
        const FileOutcome::Verdict v = verdict_of_rows();
        log_info(kStage, kFile, "file done",
                 {{"src", fe.src.string()},
                  {"outputs", std::to_string(res.outputs.size())},
                  {"ok_outputs",
                   std::to_string(std::count_if(res.outputs.begin(), res.outputs.end(),
                                                [](const OutputResult &o) { return o.ok; }))},
                  {"bytes", std::to_string(res.out_bytes)},
                  {"decode_ms", fmt_double(res.t.decode_ms)},
                  {"orient_ms", fmt_double(res.t.orient_ms)},
                  {"color_ms", fmt_double(res.t.color_ms)},
                  {"flatten_ms", fmt_double(res.t.flatten_ms)},
                  {"encode_ms", fmt_double(res.t.encode_ms)},
                  {"metawrite_ms", fmt_double(res.t.metawrite_ms)},
                  {"verdict", v == FileOutcome::Verdict::Done      ? "Done"
                              : v == FileOutcome::Verdict::Skipped ? "Skipped"
                              : v == FileOutcome::Verdict::Failed  ? "Failed"
                                                                   : "DoneWithErrors"},
                  {"warnings", std::to_string(res.warnings.size())}});
        return finalize(v);
    } catch (const std::exception &e) {
        return fail(std::string("unexpected exception: ") + e.what());
    } catch (...) {
        return fail("unexpected non-standard exception");
    }
}

// ---------------------------------------------------------------------------
// run_metadata_only（§3.2：语义不变，互斥 outputs.size()==1；零重编码）
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

    const auto finalize = [&res, &t_start, &outcome](FileOutcome::Verdict v) -> FileOutcome {
        res.t.total_ms = ms_since(t_start);
        res.out_bytes = 0;
        res.warnings.clear();
        for (const OutputResult &o : res.outputs) {
            res.out_bytes += o.out_bytes;
            for (const Warning &w : o.warnings) {
                const bool dup =
                    std::any_of(res.warnings.begin(), res.warnings.end(), [&w](const Warning &e) {
                        return e.kind == w.kind && e.detail == w.detail;
                    });
                if (!dup)
                    res.warnings.push_back(w);
            }
        }
        res.out = res.outputs.empty() ? fs::path{} : res.outputs.front().out;
        res.ok = (v == FileOutcome::Verdict::Done);
        res.skipped = (v == FileOutcome::Verdict::Skipped);
        res.cancelled = (v == FileOutcome::Verdict::Cancelled);
        if ((res.ok || res.skipped) && res.error.empty()) {
            // keep empty
        } else if (res.error.empty() && !res.outputs.empty()) {
            res.error = res.outputs.front().error;
        }
        outcome.verdict = v;
        return outcome;
    };
    const auto fail = [&res, &finalize](std::string msg) -> FileOutcome {
        res.error = msg;
        for (OutputResult &o : res.outputs) {
            if (!o.ok && !o.skipped && o.error.empty())
                o.error = msg;
        }
        log_error(kStage, kFile, msg, {{"src", res.src.string()}});
        return finalize(FileOutcome::Verdict::Failed);
    };

    try {
        // §3.2 硬校验：仅元数据模式与多输出互斥
        if (cfg.outputs.size() != 1)
            return fail("metadata-only mode requires exactly one output "
                        "(RunConfig::outputs.size() == 1)");
        const OutputFormatSpec &spec = cfg.outputs.front();
        {
            OutputResult row;
            row.format_id = spec.format_id;
            row.backend_id = spec.backend_id;
            row.tech_id = spec.tech_id;
            res.outputs.push_back(std::move(row));
        }
        OutputResult &row = res.outputs.front();

        // M2-T5 §2.7：交叉参数约束（仅元数据路径同样是"该文件失败，error=首条消息"）
        if (const std::vector<std::string> msgs =
                cross_validate(spec.params, spec.format_id, spec.tech_id);
            !msgs.empty())
            return fail(msgs.front());

        std::string tmpl_err;
        if (!validate_output_template(cfg.output_template, &tmpl_err))
            return fail(tmpl_err);

        stage(FileState::Probing, Stage::Probe);
        const ProbeOutcome po = probe_file(fe.src);
        if (!po.error.empty())
            return fail("probe failed: " + po.error);
        const ImageInfo info = po.info; // §3.2：FileEntry 只读，不回填
        res.info = info;
        // W1-T6：进度枢纽 + §7.2 共享段（本路径不解码/不编码 → decode/orient/color/encode 记满）
        mux = std::make_unique<ProgressMux>(info.width, info.height, 1);
        mux->set_event_callback([&ev](const FileEvent &e) {
            if (ev.on_event)
                ev.on_event(e);
        });
        mux->shared_stage(Stage::Probe, 1.f); // probe + acquire（本路径无内存背压）3%
        mux->shared_skip(Stage::Decode);      // 不解码（零重编码）
        mux->shared_skip(Stage::Orient);
        mux->shared_skip(Stage::Color);
        mux->on_output_no_encode(0); // 无编码段 → 50% 记满（其余走 metawrite 10%）

        const FormatDef *fmt = find_format(spec.format_id);
        if (!fmt)
            return fail("unknown output format '" + spec.format_id + "'");
        if (!format_supports_metadata_only(fmt->id)) {
            return fail("format '" + fmt->id +
                        "' does not support metadata-only rewrite (zero re-encode)");
        }
        // 路径模板（同容器 → 保持源扩展名）+ 冲突解析（逐输出独立）
        PathCtx ctx;
        ctx.format_dir = fmt->id;
        ctx.rel_dir = relative_dir(fe.src, fe.base_dir);
        ctx.stem = fe.src.stem().string();
        ctx.ext = fe.src.extension().string();
        const fs::path desired = render_output_path(cfg.output_template, ctx, cfg.out_root);
        if (desired.empty())
            return fail("output path: template '" + cfg.output_template +
                        "' produced no file name");
        std::string conflict_err;
        const OutputPlan out_plan = resolve_conflict(desired, cfg.conflict, reserved, conflict_err);
        if (!conflict_err.empty())
            return fail("output path: " + conflict_err);
        row.out = out_plan.out_path;
        if (out_plan.skip) {
            row.skipped = true;
            mux->on_output_skipped(0); // §7.2：无工作可做 → 全段记满
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
            log_warn(kStage, kFile, "source metadata unavailable; continuing with empty metadata",
                     {{"src", fe.src.string()}, {"error", srcmeta.error}});
        }
        MetadataPlan meta_plan = build_plan(srcmeta, cfg.rules, fe.exception);
        // No orientation clearing here: the pixels are not rotated in metadata-only mode, so
        // dropping the tag would change how the image displays.

        const Payloads payloads = make_payloads(meta_plan);
        stage(FileState::Writing, Stage::MetaWrite);
        mux->output_stage(0, Stage::MetaWrite);
        const Clock::time_point t0 = Clock::now();
        const std::string err = rewrite_metadata_only(fe.src, row.out, meta_plan, payloads);
        row.t.metawrite_ms = ms_since(t0);
        res.t.metawrite_ms = row.t.metawrite_ms;
        merge_warnings(row.warnings, meta_plan.warnings);
        if (!err.empty())
            return fail("metadata-only rewrite failed: " + err);
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
        row.ok = true;
        mux->finish(); // W1-T6：收尾样本（此路径 §7.2 全段记满 → 1.0）
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

} // namespace pp
