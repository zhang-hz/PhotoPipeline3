// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.2 **已落地**（依据 docs/v0.3.0-design.md §3.2）
//   落地任务 = W1-T5（多格式管线重排）；原标注 PP-THAWED(0.3.0-M4-D20) 随落地再冻结为
//   本标记（冻结头 SPDX 延续）。本节标注 [重排]：结构重排，不再保证聚合初始化兼容。
//   本文件其余既有声明（FileEntry / FileResult 既有成员 / FileState / FileEvent）不在裁定
//   表内 → 维持 PP-FROZEN(M0/M1) 只读；`FileEvent` 的 progress 追加属 §3.3（见下方 §3.3 块，
//   落地任务 = W1-T7）。
//
//   0.2 → 0.3.0 差异清单（逐条，T5 落地）：
//     ① format_id/backend_id/tech_id/params/out_bitdepth 删除 → outputs[] 元素；
//     ② `color_target = ColorTarget::KeepOriginal` → `color = ColorTarget::Keep`：
//        W0 已裁定口径 a —— §3.2 的 `ColorTarget::Keep` **即现形 `ColorTarget::KeepOriginal`**
//        （命名映射；不改 src/core/colormanager.h）【机械性】；
//     ③ `flatten_gray`：§3.2 逐字为 `bool flatten_gray = false`，与同文档 §4.3「底色规则
//        不变」自相矛盾，且 bool 化丢失底色值（撞铁律"全参数暴露"）并改变 alpha 合成像素
//        （撞"禁止降级"/金样不回退）→ 主对话裁定 §3.2 该行为设计勘误，**保持现形
//        `double flatten_gray = 1.0`（v0.2 白底语义）**【语义性】；
//     ④ `lossless` 字段删除（§3.2 冻结形态无此成员）：无损语义随 `OutputFormatSpec.params`
//        承载（由冻结引擎 default_params(fmt,backend,tech,lossless) 产出，引擎自带内部键），
//        pipeline **不再自行注入**该键、只按 spec.params + tech_id 判定【语义性，主对话裁定 B】；
//     ⑤ `workers` 注释口径 0 = 自动（逻辑核）。
//
//   T5 定稿（**授权来源与措辞更正**，复核项 4）：
//     设计 §3.2 的围栏行只给了两条三参签名（design.md:120-121），未定义 `FileOutcome`/`EventFn`、
//     也未规定 enc/budget/reserved/cancelled/on_stage 的承载方式。本文件此前把「由 T5 落地时
//     定稿」写成对设计正文的引文，实为 **M4-T1 头注释里的落地提示**（原 PP-THAWED 块：
//     「注：`FileOutcome` … 与 `EventFn` 在 §3.2 未给定义（scheduler.h 0.2 现形为 `EventCb`）
//     —— 由 T5 落地时定稿」）；本次书面追认来自主对话对 W1-T5 的裁定（第一次升级答复
//     备报 a：「接受。EventFn={on_event, const RunScope*}、RunScope{budget,reserved,cancelled}
//     …… §3.2 授权 T5 定稿范围内，无隐藏态、单一入口的设计正确」；`OutputResult` 由同次裁定
//     第二条「按提议落地」追认）。按 §15.2 记入 m4-report「偏差与例外」。
//     * `EventFn`（第三参）= 事件回调 + 运行期共享态载体（保持 §3.2 的三参形态、无隐藏态、
//       单一入口 —— 不接受"另开一个不收预算的重载"）；
//     * `RunScope` = budget / reserved / cancelled；
//     * `FileOutcome` = §4.1 的源文件级聚合终态（全成功=Done / 任一失败=DoneWithErrors /
//       全失败=Failed / 全跳过=Skipped / 已取消=Cancelled）。
#pragma once
#include "codecs/encoder.h"
#include "core/colormanager.h"
#include "core/fsops.h"
#include "core/metadata.h"
#include "core/params.h"
#include "core/pixelbudget.h"
#include "core/types.h"
#include <cstdint>
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

// —— 0.3.0 冻结形态（设计 §3.2 逐字）——
struct OutputFormatSpec { // 用户配置面（≠ OutputTarget：无 out_path）
    std::string format_id, backend_id, tech_id;
    ParamSet params;
    int out_bitdepth = 8;
};

struct RunConfig {
    // —— 0.2 字段中被替代者删除：format/backend/tech/params/out_bitdepth ——
    std::filesystem::path out_root;
    std::vector<OutputFormatSpec> outputs;         // [重排] 多格式核心；≥1 项；仅元数据模式=1 项
    ColorTarget color = ColorTarget::KeepOriginal; // §3.2 ColorTarget::Keep（W0 命名映射 a）
    ConflictPolicy conflict = ConflictPolicy::Rename;
    bool rotate_orientation = true;
    double flatten_gray = 1.0; // alpha 合成底色（0=黑..1=白）；§3.2 bool 形态已裁定为勘误
    BatchRules rules;
    bool metadata_only = false;
    int workers = 0; // 0 = 自动（逻辑核）
    std::uint64_t budget_bytes = 0;
    // —— 0.3.0 追加 ——
    bool split_by_format = false; // 联动 output_template 含 $format 段；多选输出时 UI 默认 true
    std::string output_template = "$format/$dir/$file"; // 路径模板（§4.2）；替代固定结构二选一
    int stagger_ms = 150;                               // 0=立即启动
    int thread_budget = 0;                              // 0 = 逻辑核数
};

// 0.3.0 追加（主对话裁定：§3 表头通则「加性字段一律追加在结构体末尾」）：
// 逐输出结果行。§4.1「记录 result[target]」+ §4.4「reserved 按每个 out_path 登记」+
// T14 逐输出进度行 + 金样 outputs[] 断言共同要求它；FileResult 的既有成员语义不变，
// 单输出时**逐字等价**于 v0.2：out == outputs[0].out、out_bytes == outputs[0].out_bytes、
// warnings == outputs[0].warnings（串联）、ok/skipped/cancelled/error 取聚合口径。
struct OutputResult {
    std::string format_id, backend_id, tech_id;
    std::filesystem::path out; // 冲突解析后的最终输出路径
    uint64_t out_bytes = 0;
    std::vector<Warning> warnings;
    Timing t;
    std::string error; // 非空 = 该输出失败
    bool ok = false, skipped = false;
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
    std::vector<OutputResult> outputs; // 0.3.0 追加（末尾；逐输出，配置顺序）
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
//   T5 口径：本任务不新增 FileState 值、不追加 FileEvent::progress —— 逐输出进度事件是
//   T6/T7 的落地面（§3.3 + §7.4），pipeline 侧只需按阶段发既有 FileState（§4.1 不改事件形状）。
struct FileEvent {
    std::size_t index = 0;
    FileState state = FileState::Queued;
    const FileResult *result = nullptr; // 仅在终态（Done/Skipped/Failed/Cancelled）非空
};

// —— T5 定稿（§3.2 注）：运行期共享态 + 事件回调载体 + 聚合终态 ——
// RunScope 由调用方（Scheduler / 单测）持有并保证生命周期覆盖整次 run_one_file。
struct RunScope {
    PixelBudget *budget = nullptr;               // 可为 null（仅元数据模式不需要内存背压）
    std::vector<std::filesystem::path> reserved; // 本批次已分配输出路径快照（§4.4 逐 out_path）
    std::function<bool()> cancelled;             // 可空
};

// §3.2 冻结形态的第三参 `EventFn`（T5 定稿为「事件回调 + 运行期共享态」载体）：
//   * on_event：阶段/终态事件；可空。`FileEvent::index` 与 `result` 由调用方（Scheduler）
//     在转发时按自己的批内槽位回填 —— pipeline 不持有批内索引，恒填 0、result 恒为 nullptr，
//     回调实现应忽略这两个字段（src/core/scheduler.cpp 的适配器即如此）。
//   * scope：可空（nullptr = 无预算、无取消、空 reserved）。
struct EventFn {
    std::function<void(const FileEvent &)> on_event;
    const RunScope *scope = nullptr;
};

// §4.1 的源文件级聚合终态。
struct FileOutcome {
    enum class Verdict { Done, DoneWithErrors, Failed, Skipped, Cancelled };
    Verdict verdict = Verdict::Failed;
    FileResult file;
};

// PP-FROZEN(0.3.0) §3.2 · run_one_file + run_metadata_only（0.3.0 冻结形态，设计 §3.2 逐字）：
// clang-format off
// FileOutcome run_one_file(const FileEntry&, const RunConfig&, EventFn);   // 内部多输出循环（§4）
// FileOutcome run_metadata_only(const FileEntry&, const RunConfig&, EventFn); // 语义不变（互斥：outputs.size()==1）
// clang-format on
// 单文件全流程（线程内串行，§4.1）：probe(一次) → budget.acquire(2×frame) → decode(一次,float32)
//   → orient(如启用) → color(全局目标,一次) → 按 targets（§4.3 排序）逐个 flatten/encode/metawrite
//   → mtime → release → 聚合终态。
// 逐 target 串行（同文件内），文件间并行由 scheduler 管；峰值内存维持 2×frame。
// 多输出聚合与硬校验：outputs 为空 → 该文件失败；metadata_only 走 run_metadata_only（该入口
// 要求 outputs.size() == 1），run_one_file 本身拒绝 metadata_only 配置（复核项 11 措辞修正）。
FileOutcome run_one_file(const FileEntry &, const RunConfig &, EventFn);

// 仅元数据模式（零重编码）：仅 JPEG/PNG/TIFF/WebP；HEIF/AVIF/JXL 由上层拒绝
FileOutcome run_metadata_only(const FileEntry &, const RunConfig &, EventFn);

// 输入白名单校验（唯一入口，UI 与 harness 共用）
bool format_supports_metadata_only(std::string_view format_id);

} // namespace pp
