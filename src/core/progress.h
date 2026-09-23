// PP-THAWED(0.3.0-M4-D20): 纯规格块（M4-W0-T1 落注）—— 0.3.0 新模块规格，**零声明、零实现、未接入构建**
//   落地任务 = W1-T6（core/progress：权重表/ProgressSynth/ProgressMux；出口 `test_progress` 绿）→ 落地后改标 PP-FROZEN(0.3.0)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — progress spec (0.3.0 / M4-W0-T1：仅规格注释，待 W1-T6 落地为声明 + 实现)
//   依据 docs/v0.3.0-design.md §3.6 表行（`core/progress.h`（新））+ §7.1–§7.4（三级模型/权重表/合成/节流）
//   本文件当前不写任何声明/实现，也不在 CMakeLists.txt 中编译（T1 不接入构建）。
#pragma once

// ============================================================================================
// PP-THAWED(0.3.0-M4-D20) §3.6 · core/progress.h（新）
//   裁定原文（逐字抄录，剥去行首 "// " 前缀即设计原文）：
// clang-format off
// | `core/progress.h`（新） | `struct StageWeights`（§7.2 常量表）；`class ProgressSynth`（合成进度发生器）；`class ProgressMux`（行级回调→事件汇流，节流 60ms） |
// clang-format on
//
// §7.1 三级进度模型（逐字摘录）：
// clang-format off
// 运行级：总进度 = Σ 输出完成度 / (文件数 × 格式数)（复选文件口径）
// 源文件级：聚合行 = mean(各输出行进度)；状态 = 阶段态/终态
// 输出级：阶段权重 + 阶段内 frac（真实或合成）
// clang-format on
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
//
// 建议形态（§3.6/§7 未逐字给出成员名与函数签名 → 由 W1-T6 定稿，非本任务改动）：
// clang-format off
// struct StageWeights {                 // §7.2 常量表（输出级行进度的确定性来源）
//     static constexpr float probe_acquire = 0.03f;   //  3%  即时 1
//     static constexpr float decode        = 0.27f;   // 27%  OIIO 读行回调（无回调格式即时 1 并日志注明）
//     static constexpr float orient_color  = 0.10f;   // 10%  即时 1（大图按行计 50/50）
//     static constexpr float encode        = 0.50f;   // 50%  真实行级 或 合成（§7.3）
//     static constexpr float metawrite     = 0.10f;   // 10%  metawrite + mtime + 落盘；即时 1
// };
//
// class ProgressSynth {                 // 合成进度发生器（§7.3；WebP/HEIF/AVIF 无回调面）
// public:
//     ProgressSynth(std::string format_id, std::uint64_t pixels);   // k[format] 校准表 → t_est
//     float on_tick(std::chrono::steady_clock::time_point now);     // f(x)=1-(1-x)^2 → 0.95 平推（不倒退）
//     float on_done();                                              // 完成事件：跳 1.0
//     bool  synthetic() const { return true; }                      // → UI 斜纹 + tooltip
// };
//
// class ProgressMux {                   // 行级回调 → FileEvent 汇流（§7.4；节流 60ms）
// public:
//     // 编码器行回调（每 N 行或每 20ms）→ FileEvent{state=Progress}；编码内进度为 encoder.h 的
//     // ProgressFn = std::function<void(float)>（§3.1）
//     void  set_event_callback(std::function<void(const FileEvent&)>);
//     void  bind(int output_index, const std::function<void(ProgressFn)>&);   // 输出级 → 三级模型
//     float overall_frac() const;      // 运行级：Σ 输出完成度 / (文件数 × 格式数)
//     float file_frac() const;         // 源文件级：mean(各输出行进度)
//     // 日志快照：progress_max_row / progress_reported（debug 级，逐文件；不逐行落盘）
// };
// clang-format on
//
// 边界与纪律：合成进度只在"编码器无回调"的格式上启用，且必须如实置 `synthetic=true`
//   （UI 斜纹，§7.3）；真实进度不得被合成覆盖（`progress_reported=false` 时才标合成，§3.1）。
// ============================================================================================
