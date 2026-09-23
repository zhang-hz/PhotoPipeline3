// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/progress（0.3.0 / M4-W1-T6：逐文件进度系统）
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.6 行 `core/progress.h`（新）**已落地**
//   落地任务 = W1-T6（StageWeights / ProgressSynth / ProgressMux）；原标注
//   PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记（冻结头 SPDX 延续）。
//   裁定原文（逐字抄录，剥去行首 "// " 前缀即设计原文）：
// clang-format off
// | `core/progress.h`（新） | `struct StageWeights`（§7.2 常量表）；`class ProgressSynth`（合成进度发生器）；`class ProgressMux`（行级回调→事件汇流，节流 60ms） |
// clang-format on
//   另：本文件同时承载 **§3.3 的 `ProgressInfo`**（设计 §3.3 的表头文件是 `core/scheduler.h`；
//   物理声明落在本文件，与 §3.3 的 `FileEvent` 落在 `core/pipeline.h` 同一处置 —— 两个块互相
//   依赖（`FileEvent::progress` 需要 `ProgressInfo` 完整类型），而 `scheduler.h` 必须包含
//   `pipeline.h`，把 `ProgressInfo` 留在 `scheduler.h` 会形成包含环。故 T6 将其声明落在进度模块
//   头文件，`scheduler.h` 的 §3.3 块留交叉引用与逐字抄录）。`ProgressInfo` 的**字段名/顺序/
//   语义/默认值**与设计 §3.3 逐字一致（唯一例外见下方 §3.3 块末注：`Stage::Queued`）。
//
// §7.1 三级进度模型（逐字摘录）：
// clang-format off
// 运行级：总进度 = Σ 输出完成度 / (文件数 × 格式数)（复选文件口径）
// 源文件级：聚合行 = mean(各输出行进度)；状态 = 阶段态/终态
// 输出级：阶段权重 + 阶段内 frac（真实或合成）
// clang-format on
//   落地面：源文件级 = `ProgressMux::file_frac()`（§7.2 权重表加权，含共享段一次）；
//   运行级 = 各文件 `ProgressMux::outputs_done_frac()` 的均值（= §7.1 的 Σ/(文件数×格式数)，
//   由消费端/Sidecar 聚合；mux 是**逐文件**对象）。
//
// §7.2 阶段权重表（输出级行进度的确定性来源；逐字摘录）：
// clang-format off
// | 阶段 | 权重 | frac 来源 |
// | probe + acquire | 3% | 即时 1 |
// | decode | 27% | OIIO 读行回调（无回调格式即时 1 并日志注明） |
// | orient + color | 10% | 即时 1（大图按行计 50/50） |
// | encode | 50% | 真实行级 或 合成（§7.3） |
// | metawrite + mtime + 落盘 | 10% | 即时 1 |
//
// 多输出时源文件级 = 3%+27%+10%（共享段一次）+ mean(各输出 50%+10%)。
// clang-format on
//
// §7.3 合成进度（ProgressSynth；逐字摘录）：
// clang-format off
// WebP/HEIF/AVIF 无编码回调 → 估算时长 `t_est = k[format] × pixels / 1e6`（k 校准表随日志实测回归校正），在 `[t0, t0+0.95·t_est]` 上按 `f(x)=1-(1-x)^2` 缓出到 0.95，完成事件跳 1.0；`synthetic=true` → UI 斜纹条纹 + tooltip「合成进度（编码器无回调）」。中途超时过 `t_est` 则停在 0.95 平推（不倒退）。
// clang-format on
//
// §7.4 进度节流与日志（逐字摘录）：
// clang-format off
// `ProgressMux` 将编码器行回调（每 N 行或每 20ms）汇流为 `FileEvent{state=Progress}`，GUI 节流 60ms 刷新；运行日志每文件落 `progress_max_row/progress_reported` 快照（debug 级）。**进度事件不得淹没日志**（不逐行落盘）。
// clang-format on
//   T6 落地口径：本 mux 自身节流 = **每 20ms 或每 0.5% 进度（≈每 N 行）取先到者**（另有
//   首样本 / 1.0 / 阶段切换 / synthetic 翻转四处强制发点）；GUI 的 60ms 刷新节流由消费端负责
//   （不在本模块）。日志快照 = 每输出一条 debug 行（`progress_max_row` / `progress_reported`
//   / `progress_samples` / `progress_synthetic`，pipeline.cpp 落），**不逐行落盘**。
//
// 边界与纪律：合成进度只在"编码器无回调"的格式上启用，且必须如实置 `synthetic=true`
//   （UI 斜纹，§7.3）；真实进度不得被合成覆盖（`progress_reported=false` 时才标合成，§3.1）。
#pragma once
#include "codecs/encoder.h" // ProgressFn（§3.1：编码内进度 0..1）
#include "core/types.h"     // Stage（PP-FROZEN(M0)；本模块只读）

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace pp {

// §7.4 的汇流目标（物理声明在 core/pipeline.h；此处仅作 std::function 形参 → 前向声明足够）
struct FileEvent;

// ============================================================================================
// §3.3 · ProgressInfo（逐字成员形态）
//   设计 §3.3 逐字（行首 "// " 为注释包装）：
// clang-format off
// struct ProgressInfo {                          // 追加
//     int   output_index = -1;                   // -1 = 源文件级阶段（probe/decode/color）
//     Stage stage = Stage::Queued;
//     float stage_frac  = 0.f;                   // 阶段内 0..1
//     float overall_frac = 0.f;                  // 文件整体 0..1（权重表 §7.2）
//     bool  synthetic   = false;                 // true = 合成进度（UI 斜纹）
// };
// clang-format on
//   注（表缺口，【语义性】）：逐字为 `Stage stage = Stage::Queued;`，但现形 `Stage`
//   （core/types.h，PP-FROZEN(M0)，§3.6 未授权改动）为 {Probe, Decode, Orient, Color, Flatten,
//   Encode, MetaWrite, Done}，**无 `Queued` 值** → 落地取 `Stage::Probe`（枚举首值 = 管线第一阶段；
//   "尚未进入任何阶段"的语义由同批解冻的 `FileEvent::state == FileState::Queued` 承载）。
//   M4-T1 已上报、主对话未裁定；T6 按最小偏离落地并记 m4-report「偏差与例外」。
// ============================================================================================
struct ProgressInfo {
    int output_index = -1;      // -1 = 源文件级阶段（probe/decode/color）
    Stage stage = Stage::Probe; // 注：设计逐字 Stage::Queued → 见上方表缺口说明
    float stage_frac = 0.f;     // 阶段内 0..1
    float overall_frac = 0.f;   // 文件整体 0..1（权重表 §7.2）
    bool synthetic = false;     // true = 合成进度（UI 斜纹）
};

// ============================================================================================
// §7.2 · StageWeights（阶段权重表常量；输出级行进度的确定性来源）
//   合计恒 = 1（单测 test_progress 断言）：
//     probe_acquire(3%) + decode(27%) + orient_color(10%) + encode(50%) + metawrite(10%)
//   `orient`/`color` 是 §7.2「大图按行计 50/50」的拆分（各 5%）；`Flatten` 不在权重表内
//   （§7.2 未列）→ 权重 0：它属于"逐输出 encode 段之前"的编排步，只更新阶段文本、不推进
//   整体数（见 stage_weight() 与 ProgressMux::output_stage）。
// ============================================================================================
struct StageWeights {
    static constexpr float probe_acquire = 0.03f; //  3% 即时 1（probe + budget acquire）
    static constexpr float decode = 0.27f;        // 27% 读行回调；无回调格式即时 1 并日志注明
    static constexpr float orient_color = 0.10f;  // 10% 即时 1（大图按行计 50/50）
    static constexpr float encode = 0.50f;        // 50% 真实行级 或 合成（§7.3）
    static constexpr float metawrite = 0.10f;     // 10% metawrite + mtime + 落盘；即时 1

    // 派生量（只读；合计恒 1）
    static constexpr float orient = orient_color * 0.5f;                   // 5%（orient 单阶段）
    static constexpr float color = orient_color * 0.5f;                    // 5%（color 单阶段）
    static constexpr float shared = probe_acquire + decode + orient_color; // 0.40（共享段）
    static constexpr float total = shared + encode + metawrite;            // 1.00

    // 单阶段权重（Flatten → 0：§7.2 未列；Done → 1：收尾样本用）
    static float stage_weight(Stage s);
};

// 阶段名（日志/快照/侧车用；与 core/types.h 的 Stage 一一对应）
const char *stage_name(Stage s);

// ============================================================================================
// §7.3 · ProgressSynth（合成进度发生器：编码器无回调格式的时长估算）
//   * `t_est = k[format] × pixels / 1e6`（毫秒；k 校准表见 progress.cpp，**可校准**）
//   * 在 `[t0, t0 + 0.95·t_est]` 上按 `f(x) = 1-(1-x)^2` 缓出到 0.95
//   * 完成事件 `on_done()` 跳 1.0；`on_tick()` 单调不倒退，超窗后停在 0.95 平推
//   * `synthetic()` 恒 true（UI 斜纹 + tooltip「合成进度（编码器无回调）」）
//   线程纪律：本类不是线程安全的；`ProgressMux` 持锁调用（mux 自身可被泵线程并发 tick）。
// ============================================================================================
class ProgressSynth {
public:
    ProgressSynth(std::string format_id, std::uint64_t pixels);

    // 时间推进采样；返回值 ∈ [0, 0.95]（完成前）。同一/更早时刻重复调用不倒退。
    float on_tick(std::chrono::steady_clock::time_point now);
    // 完成事件：跳到 1.0（此后 on_tick 亦保持 1.0）
    float on_done();
    float value() const { return value_; }
    bool done() const { return done_; }
    bool synthetic() const { return true; }
    const std::string &format_id() const { return format_id_; }
    std::uint64_t pixels() const { return pixels_; }
    double estimate_ms() const { return t_est_ms_; }

    // k 校准表（ms / 百万像素）：表内格式返回实测 k；未列格式 → k_default。
    static double k_factor(std::string_view format_id);
    static bool has_k(std::string_view format_id);
    // 未列格式的兜底 k（同时是 k 表的量纲说明：k = 毫秒每百万像素）
    static constexpr double k_default = 60.0;

private:
    std::string format_id_;
    std::uint64_t pixels_ = 0;
    double t_est_ms_ = 0.0;
    std::chrono::steady_clock::time_point t0_{};
    float value_ = 0.f;
    bool done_ = false;
};

// ============================================================================================
// §7.4 · ProgressMux（编码器行回调 → FileEvent{state=Progress} 汇流 + 三级模型算术）
//   * 逐文件一个实例（`run_one_file` / `run_metadata_only` 内）；线程安全：内部互斥 +
//     事件回调**持锁**调用（保序：进度事件必须单调递达消费端）。
//   * 源文件级共享段（§7.2 前三行，一次）：shared_stage(Stage, frac) 推进；不执行的阶段用
//     shared_skip() 即时记满（权重不丢失，整体恒可达 1.0）。
//   * 输出级（§7.2 后两行，逐输出）：bind_output() 取编码器 ProgressFn；on_output_* 收尾。
//   * 合成输出：bind_output 时按 k 表建立 ProgressSynth；编码期间由**泵**（pipeline 侧
//     20ms 采样线程，编码器无回调 → 只能时间驱动）调用 tick() 推进。
//   * 节流：每 20ms 或每 0.5% 进度发点（另有首样本/1.0/阶段切换/synthetic 翻转强制发点）。
// ============================================================================================
class ProgressMux {
public:
    // width/height：progress_max_row 的折算基数（行级回调 frac → round(frac×height)）；
    // outputs：= RunConfig::outputs.size()（≥1；0 视作 1）
    ProgressMux(int width, int height, std::size_t outputs);
    ~ProgressMux();
    ProgressMux(const ProgressMux &) = delete;
    ProgressMux &operator=(const ProgressMux &) = delete;

    void set_event_callback(std::function<void(const FileEvent &)> cb);

    // ---- 源文件级共享段（§7.2 前的三项，一次）----
    void shared_stage(Stage s, float frac); // Probe/Decode/Orient/Color；frac 取历史最大（单调）
    void shared_skip(Stage s) { shared_stage(s, 1.f); } // 该阶段不执行（权重不丢失）

    // ---- 输出级（§7.2 后两项，逐输出）----
    // 交给编码器的 ProgressFn（编码内 0..1）；同一输出重复绑定 → 返回同一闭包语义
    ProgressFn bind_output(std::size_t index, std::string_view format_id);
    // 该输出是否走合成估算（k 表内 → 编码期间需外部 tick 泵；编码器自带回调的格式 → false）
    bool synth_active(std::size_t index) const;
    // 阶段切换（Flatten/Encode/MetaWrite…）：只更新阶段文本与 stage_frac，不改加权数
    void output_stage(std::size_t index, Stage s, float frac = 0.f);
    // 编码返回后：如实记录 progress_reported（false → 该输出标合成，§3.1）
    void on_output_encode_done(std::size_t index, bool progress_reported);
    void on_output_metawrite_done(std::size_t index);
    void on_output_failed(std::size_t index);
    void on_output_skipped(std::size_t index);   // 冲突策略 Skip 命中
    void on_output_no_encode(std::size_t index); // 仅元数据模式（无编码段，其余同完成）

    // 合成估算推进（泵线程 / 消费端节流刷新调用）；返回被推进的输出数
    int tick(std::chrono::steady_clock::time_point now);
    int tick();
    // 收尾：未决输出不虚增；仅补发一条当前快照（成功路径此前已按 §7.2 达 1.0）
    void finish();

    // ---- 三级模型的读数 ----
    float file_frac() const;         // 源文件级（§7.2：共享段 + mean(各输出 50%+10%)）∈[0,1]
    float outputs_done_frac() const; // §7.1 分子（每文件）：Σ 输出完成度 / 格式数 ∈[0,1]
    float output_frac(std::size_t index) const; // 单输出完成度（0.5e+0.1m 归一）
    std::size_t outputs() const;
    // 当前快照（阶段事件携带；output_index = -1 表示源文件级）
    ProgressInfo snapshot(Stage s, int output_index = -1) const;
    // 逐输出快照（日志 progress_max_row/progress_reported 用）
    struct Stats {
        float enc_frac = 0.f, meta_frac = 0.f;
        int max_row = 0, samples = 0;
        bool reported = false, synthetic = false, skipped = false, started = false;
        double est_ms = 0.0;
    };
    Stats stats(std::size_t index) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp
