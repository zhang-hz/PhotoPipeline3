// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.2（依据 docs/v0.3.0-design.md §3.2）
//   本文件内 OutputFormatSpec / RunConfig / run_one_file / run_metadata_only 为 0.3.0
//   一次性解冻（D20）授权变更面；落地任务 = W1-T5（多格式管线重排）。
//   对应任务落地后：把本文件内的 PP-THAWED 标记改标为 PP-FROZEN(0.3.0)（冻结头 SPDX 延续）。
//   本文件其余既有声明（FileEntry / FileResult / FileState 等）不在裁定表内
//   → 维持 PP-FROZEN(M0/M1) 只读；`FileEvent` 的 progress 追加属 §3.3（见下方 §3.3 块），
//   若其 `FileState` 需新增进度值则属**表外改动**（该缺口已在 §3.3 块内标注、待主对话裁定）。
//   本节标注 [重排]：结构重排，不再保证聚合初始化兼容（随本次解冻一次性接受）。
#pragma once
#include "codecs/encoder.h"
#include "core/colormanager.h"
#include "core/fsops.h"
#include "core/metadata.h"
#include "core/params.h"
#include "core/pixelbudget.h"
#include "core/types.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace pp {

struct FileEntry {
    std::filesystem::path src;
    std::filesystem::path base_dir; // 镜像路径基准
    std::optional<MetadataOverride> exception;
    ImageInfo info; // probe 后回填
    bool probe_done = false;
};

// PP-THAWED(0.3.0-M4-D20) §3.2 · OutputFormatSpec + RunConfig
//   0.3.0 新增 OutputFormatSpec（用户配置面，≠ encoder.h 的 OutputTarget：无 out_path）；
//   RunConfig [重排]：0.2 的 format_id/backend_id/tech_id/params/out_bitdepth 被删除，
//   由 outputs[] 承载，另追加 split_by_format/output_template/stagger_ms/thread_budget。
//   落地任务 W1-T5 → 落地后改标 PP-FROZEN(0.3.0)。
//   0.3.0 冻结形态（设计 §3.2 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// struct OutputFormatSpec {                      // 用户配置面（≠ OutputTarget：无 out_path）
//     std::string format_id, backend_id, tech_id;
//     ParamSet    params;
//     int         out_bitdepth = 8;
// };
// struct RunConfig {
//     // —— 0.2 字段中被替代者删除：format/backend/tech/params/out_bitdepth ——
//     std::filesystem::path     out_root;
//     std::vector<OutputFormatSpec> outputs;     // [重排] 多格式核心；≥1 项；仅元数据模式=1 项
//     ColorTarget               color = ColorTarget::Keep;
//     ConflictPolicy            conflict = ConflictPolicy::Rename;
//     bool                      rotate_orientation = true;
//     bool                      flatten_gray = false;
//     BatchRules                rules;
//     bool                      metadata_only = false;
//     int                       workers = 0;     // 0 = 自动（逻辑核）
//     std::uint64_t             budget_bytes = 0;
//     // —— 0.3.0 追加 ——
//     bool        split_by_format = false;       // 联动 output_template 含 $format 段；多选输出时 UI 默认 true
//     std::string output_template = "$format/$dir/$file";  // 路径模板（§4.2）；替代固定结构二选一
//     int         stagger_ms     = 150;          // 0=立即启动
//     int         thread_budget  = 0;            // 0 = 逻辑核数
// };
// clang-format on
//   注（0.2 现形差异清单，逐条由 T5 按 §3.2 逐字形态处理）：
//     ① format_id/backend_id/tech_id/params/out_bitdepth 删除 → outputs[]
//     元素（聚合初始化不再兼容）； ② `color_target = ColorTarget::KeepOriginal` → `color =
//     ColorTarget::Keep`（字段名与枚举值均变）；
//        ⚠ `ColorTarget::Keep` **在 0.2 现形中不存在**：枚举定义在 src/core/colormanager.h:12
//        `enum class ColorTarget { KeepOriginal, SRGB, DisplayP3, AdobeRGB };`，该文件是
//        `PP-FROZEN(file)` 且设计 §3 全表未覆盖（§2 变更地图亦无 colormanager）——按 §3.2 逐字
//        形态落地需要**表外改动**（重命名/新增枚举值）或显式映射，T5 前需主对话裁定（M4-T1 已上报，
//        W5 收口入 m4-report；本任务不改 colormanager.h）；
//     ③ `flatten_gray`：0.2 现形为 `double flatten_gray = 1.0`（alpha 合成底色 0=黑..1=白），
//        §3.2 逐字为 `bool flatten_gray = false` —— 类型/语义收窄，T5 落地前需主对话裁定（M4-T1
//        已上报，W5 收口入 m4-report）；
//     ④ `workers` 注释口径 0=自动（逻辑核）[0.2 注释为"0=物理核数"]。
struct RunConfig {
    // 输出
    std::filesystem::path out_root;
    std::string format_id, backend_id, tech_id;
    bool lossless = false;
    ParamSet params;
    int out_bitdepth = 8;
    ColorTarget color_target = ColorTarget::KeepOriginal;
    ConflictPolicy conflict = ConflictPolicy::Rename;
    bool rotate_orientation = true;
    double flatten_gray = 1.0; // alpha 合成底色（0=黑..1=白）
    // 元数据
    BatchRules rules;
    bool metadata_only = false;
    // 运行
    int workers = 0;           // 0=物理核数
    uint64_t budget_bytes = 0; // 0=default_capacity_bytes()
};

struct FileResult {
    std::filesystem::path src, out;
    ImageInfo info;
    Timing t;
    uint64_t out_bytes = 0;
    std::vector<Warning> warnings;
    std::string error;
    std::string color_src, color_dst;
    bool ok = false, skipped = false, cancelled = false;
};

enum class FileState {
    Queued,
    Probing,
    Decoding,
    Orienting,
    Coloring,
    Flattening,
    Encoding,
    Writing,
    Done,
    Skipped,
    Failed,
    Cancelled
};

// PP-THAWED(0.3.0-M4-D20) §3.3 · FileEvent
//   落注位置说明（与表头文件不一致，已披露）：设计 §3.3 的表头文件是 `src/core/scheduler.h`
//   （design.md:124），但 `FileEvent` 的**声明物理位于本文件**（本文件下方）——故本块落在真实
//   声明处，scheduler.h 内留交叉引用；裁定行仍属 §3.3。
//   追加字段 progress 落于结构体末尾（既有 index/state/result 语义不变）。
//   落地任务 W1-T7（配合 §3.3 的 ProgressInfo，见 src/core/scheduler.h）→ 落地后改标
//   PP-FROZEN(0.3.0)。 0.3.0 冻结形态（设计 §3.3 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// struct FileEvent {
//     int            index;
//     FileState      state;
//     const FileResult* result = nullptr;        // 终态携带
//     ProgressInfo   progress;                   // 追加（Progress 状态携带）
// };
// clang-format on
//   注（0.2 现形差异，由 T7 按 §3.3 逐字形态处理）：`index` 现形 `std::size_t index = 0;` →
//   §3.3 `int index;`（类型与默认值均变）。
//   注（**表外改动缺口**，须主对话裁定）：§7.4 出现 `FileEvent{state=Progress}`，但本文件
//   `enum class FileState`（本文件上方）现形无 `Progress` 值，且 §3.3 围栏行未给 FileState 的
//   枚举增补、§3.6 亦未授权改 FileState（本文件首部即声明 FileState 不在裁定表内、维持只读）
//   → 「进度态」的承载方式（新增 `FileState::Progress`＝表外改动 / 复用既有态 + 用
//   `ProgressInfo.synthetic` 区分 / 其它）需裁定后才能落地（M4-T1 已上报，W5 收口入 m4-report）。
struct FileEvent {
    std::size_t index = 0;
    FileState state = FileState::Queued;
    const FileResult *result = nullptr; // 仅在终态（Done/Skipped/Failed/Cancelled）非空
};

// PP-THAWED(0.3.0-M4-D20) §3.2 · run_one_file + run_metadata_only
//   [重排] 0.2 现形为
//     `FileResult run_one_file(FileEntry&, const RunConfig&, IEncoder*, PixelBudget*,
//                              const std::vector<std::filesystem::path>& reserved,
//                              const std::function<bool()>& cancelled,
//                              const std::function<void(FileState)>& on_stage)`；
//   0.3.0 收敛为 3 参（enc/budget/reserved/cancelled/on_stage 的承载方式由 T5 按 §4 重排）。
//   run_metadata_only 语义不变（互斥：outputs.size()==1）。
//   落地任务 W1-T5 → 落地后改标 PP-FROZEN(0.3.0)。
//   0.3.0 冻结形态（设计 §3.2 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// FileOutcome run_one_file(const FileEntry&, const RunConfig&, EventFn);   // 内部多输出循环（§4）
// FileOutcome run_metadata_only(const FileEntry&, const RunConfig&, EventFn); // 语义不变（互斥：outputs.size()==1）
// clang-format on
//   注：`FileOutcome`（§4.1 的源文件级聚合终态：全成功=Done / 任一失败=DoneWithErrors /
//   全失败=Failed）与 `EventFn` 在 §3.2 未给定义（scheduler.h 0.2 现形为 `EventCb`）——
//   由 T5 落地时定稿；本文件当前 FileResult/FileState 保持只读。
// 单文件全流程（线程内串行）；budget 可为 nullptr（仅元数据模式不需要）
// reserved 为本批次已分配输出路径（批内冲突）；返回值带终态
FileResult run_one_file(FileEntry &fe, const RunConfig &cfg, IEncoder *enc, PixelBudget *budget,
                        const std::vector<std::filesystem::path> &reserved,
                        const std::function<bool()> &cancelled,
                        const std::function<void(FileState)> &on_stage);

// 仅元数据模式（零重编码）：仅 JPEG/PNG/TIFF/WebP；HEIF/AVIF/JXL 由上层拒绝
FileResult run_metadata_only(FileEntry &fe, const RunConfig &cfg,
                             const std::vector<std::filesystem::path> &reserved,
                             const std::function<bool()> &cancelled,
                             const std::function<void(FileState)> &on_stage);

// 输入白名单校验（唯一入口，UI 与 harness 共用）
bool format_supports_metadata_only(std::string_view format_id);

} // namespace pp
