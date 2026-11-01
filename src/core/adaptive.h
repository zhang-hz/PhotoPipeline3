// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 自适应参数调整核心（M10 / W3-T3；契约 docs/v0.4.0-design.md §9「C24」）
//
// 职责（core 纯函数，**零 Qt**；执行器在 W3-T4 的 `core/adaptive_exec.*`）：
//   1. 行式 DSL v1.1 的解析（parse）与规范化渲染（render）—— 设计 §9.2 的 EBNF；
//      缩进无语义、空行/注释自由、错误带**行号+列号**；`parse ∘ render` 幂等。
//      v1.1（0.4.0 迭代）加性扩展：search 支持显式方向 `down`/`up` 与步长断点
//      `breakpoint <值> step <步长>`（语义见 docs/adaptive-policy.md §6.1；断点 = 开区间上界
//      分段：v > 断点才用该段步长；跨断点不得跳过断点值）。旧语法（无方向/无断点）逐字兼容。
//   2. AST（`AdaptivePolicy`/`AdaptiveStage`/`AdaptiveAction`）与默认值（target 90 /
//      max_rounds 8 / output last / on_metric_unavailable stop / plateau 0）。
//   3. `validate_adaptive_policy`：允许/禁止目标键注册表（§9.4）、target(0,100]/
//      max_rounds(1..32)/step/方向、set 值类型与枚举值、`profile` 伪参数、`tech` 默认禁止、
//      `output best`（P1 未交付）显式拒绝；错误 = 稳定英文 code + 中文 message + 中文 hint。
//   4. 出厂默认策略库（§9.7；HEIF/AVIF/JXL/WebP/JPEG 各 1 条，编译内置）；
//      PNG/TIFF/BMP 不提供（无损，评分≈100，策略无意义）。
//
// 分层口径：
//   * DSL 文本是**唯一真源**（人类可读写、随格式预设持久化）；JSON AST 只是**导入**面
//     （脚本生成用），其 Qt JSON 解析在 `src/ui/format_preset_io.*`（core 不引 Qt），
//     导出仍为 DSL 文本（避免双真源）。
//   * 校验的格式上下文只有 `format_id`（预设只按格式分库）；backend/tech 相关的参数
//     存在性按"该格式全部后端×技术并集"判定（与 `core/params.cpp` 的未知参数口径一致）。
//
// 正交性（§9.4/§9.8）：迭代期间只有策略声明的参数可变；评分参考（源图/色彩/位深/分辨率）
// 恒定的保证 = 禁止表硬拒绝（lossless/resampling/HDR/保留键/位深/全局项/`tech`）+ 每轮
// `validate_round`（W3-T4）。允许面是**数据驱动**的：该格式参数目录中可设的键（减去禁止表）
// + search 白名单（6 个数值质量键）+ `profile` 伪参数 —— 设计 §9.4 表是分类示例，禁止表才是
// 硬边界（例 3 的 `set x265:psy-rd` / `set x265:aq-mode` 因此合法）。
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/params.h"
#include "core/types.h"

namespace pp {

// DSL/AST 版本（`version 1`）：>1 显式报错；策略文本上限 16 KiB（设计 §8.2/§9.2）。
inline constexpr int kAdaptivePolicyVersion = 1;
inline constexpr std::size_t kAdaptivePolicyMaxBytes = 16 * 1024;

enum class AdaptiveOutput : unsigned char { Last, Best };
enum class AdaptiveMetricUnavailable : unsigned char { Stop, Fail };
enum class AdaptiveMethod : unsigned char { Linear, Bisect, Exp, Greedy };
enum class AdaptiveActionKind : unsigned char { Search, Set };
// 搜索方向（DSL v1.1 加性）：Auto = 旧语法（方向由 from→to 的相对大小在运行期推断）；
// Down/Up = 显式方向（`from current down to 0.1` / `... up to 90`）：只降/只升，
// 起点已在端点错侧 ⇒ 该动作 0 轮（不是错误，见 docs/adaptive-policy.md §6.1）。
enum class AdaptiveDirection : unsigned char { Auto, Down, Up };

// 步长断点（DSL v1.1）：按**数值升序**排列的分段上界 + 该段步长。语义（开区间上界）：
//   * step(v) = 基础 step 若 v <= 第一个断点；否则 = 「v 严格大于的最大断点」的 step；
//   * 降序跨断点不得跳过断点值：若下一步会越过尚未尝试的断点 b（next < b < v），先钳到 b；
//     升序对称；端点 to 必被尝试（钳位）。
// 仅 method linear 支持（其余 method 无「步长排程」语义，validate 显式拒绝）。
struct AdaptiveBreakpoint {
    double value = 0.0;
    double step = 0.0;
};

// search 的起点：`current`（运行配置当前值）或显式数值。
struct AdaptiveValueRef {
    bool current = false;
    double number = 0.0;
};

struct AdaptiveAction {
    AdaptiveActionKind kind = AdaptiveActionKind::Set;
    std::string key; // 目录参数键 / "profile" / "chroma" / "tech"（伪参数）
    // —— Search（kind == Search）——
    AdaptiveValueRef from; // `from current` 或 `from <number>`
    double to = 0.0;
    double step = 0.0;
    AdaptiveMethod method = AdaptiveMethod::Linear;
    int max_rounds = 0; // 0 = 继承全局（阶段级上界由执行器按 §9.5 处理）
    // —— Set（kind == Set）——
    ParamValue value;      // 解析出的字面量（bool/int64/double/string）；执行前按目录类型归一
    int line = 0, col = 0; // 1-based 源码定位；JSON AST 导入为 0
    // —— DSL v1.1 加性（search）——
    AdaptiveDirection direction = AdaptiveDirection::Auto; // Auto = 旧语法（运行期推断）
    std::vector<AdaptiveBreakpoint> breakpoints;           // 升序；空 = 无断点（旧行为）
};

struct AdaptiveStage {
    std::string id;
    std::vector<AdaptiveAction> actions;
    int line = 0, col = 0;
};

struct AdaptivePolicy {
    int version = 1;
    double target = 90.0; // (0,100]
    int max_rounds = 8;   // 1..32
    AdaptiveOutput output = AdaptiveOutput::Last;
    AdaptiveMetricUnavailable on_metric_unavailable = AdaptiveMetricUnavailable::Stop;
    double plateau = 0.0; // 0=关；>0=连续 2 轮改善 < 阈值即止
    std::vector<AdaptiveStage> stages;
};

// 统一错误面：稳定 code（ASCII，供测试/日志）+ 中文 message/hint（供 UI）。
struct AdaptiveError {
    int line = 0; // 1-based；0 = 与具体行无关（全局/AST 级）
    int col = 0;  // 1-based；0 = 无列信息
    std::string code;
    std::string message;
    std::string hint;
};

// —— 解析 / 渲染 ——
// 语法错误与**选项字面量范围**在 parse 阶段报（带 line/col）；目标键注册表等需要格式
// 上下文的检查在 validate 阶段报。失败返回 false 且 *err 非空（不修改 out 的既有内容）。
bool parse_adaptive_policy(std::string_view text, AdaptivePolicy &out, AdaptiveError *err);
// 规范化渲染（丢弃注释/空行；固定选项顺序 + 两空格缩进）。`parse(render(p))` 语义等价 p。
std::string render_adaptive_policy(const AdaptivePolicy &p);

// —— 校验（AST + 目标键注册表）——
// `format_id` 空 = 只做与格式无关的检查（禁止表、全局选项、set 值结构）；
// 非空 = 追加格式作用域检查（允许面/枚举值/profile 格式）。失败时 *err 非空。
bool validate_adaptive_policy(const AdaptivePolicy &p, std::string_view format_id,
                              AdaptiveError *err);
// 一步式（格式预设保存/载入用）：parse → validate。
bool parse_and_validate_adaptive_policy(std::string_view text, std::string_view format_id,
                                        AdaptivePolicy &out, AdaptiveError *err);

// —— 目标键注册表（§9.4）——
// search 白名单（数值质量参数；formats 为空 = 全格式）：
struct AdaptiveKeyRule {
    std::string_view key;
    bool searchable = false;
    bool settable = false;
    std::vector<std::string_view> formats; // 空 = 全格式
};
const std::vector<AdaptiveKeyRule> &adaptive_key_rules();
// 禁止表（硬拒绝；与格式无关）：lossless/resampling/upsampling_mode/intensity_target/
// min_nits/relative_to_max_display/linear_below/keep_invisible/noise/photon_noise/
// disable_perceptual_heuristics/expert_options/codestream_level/buffering/output_mode/
// frame_name/intrinsic_size + 位深/全局项/`__` 保留键（`profile` 是唯一例外，用伪键表达）。
const std::vector<std::string_view> &adaptive_forbidden_keys();

// —— 值归一（校验与执行器共用单源）——
// 按目录参数类型把 DSL 字面量归一为 ParamSet 可下发值：
//   * Int：整型/整值 double/数字字符串；Float：数值/数字字符串；Bool：bool/"true"/"false"；
//   * Enum：按 choices 的引擎值或中文显示名匹配（int 型枚举接受数字/`关闭（0）`形式）；
//   * String：仅字符串。
// 失败返回 false（*err_zh 可空）；成功时 out 的类型 = 该目录参数期望类型。
bool adaptive_coerce_set_value(const ParamValue &in, const ParamDef &def, ParamValue &out,
                               std::string *err_zh);

// `profile` 伪参数：值（数字序号或档位名，如 `Main444_10Still`）→ HeifProfile 序号
// （0..21）。未知/越界返回 false。
bool adaptive_profile_from_value(const ParamValue &in, int64_t &out_profile);
// 序号 → 规范档位名（与 codecs/heif_profile.h 的枚举名一致；越界 = 空串）。
std::string adaptive_profile_name(int64_t profile);

// —— 出厂默认策略库（§9.7；编译内置，随预设复制到数据目录由 W3-T5 落地）——
struct AdaptiveDefaultPolicy {
    std::string format_id;
    std::string text; // 行式 DSL（含注释，可直接作为出厂文件内容）
};
const std::vector<AdaptiveDefaultPolicy> &default_adaptive_policies();
// 该格式的默认策略；PNG/TIFF/BMP（或未知格式）返回 nullptr。
const AdaptiveDefaultPolicy *default_adaptive_policy_for(std::string_view format_id);

// —— 文本词表（日志/UI 摘要/测试断言共用）——
std::string_view adaptive_method_name(AdaptiveMethod m);
std::string_view adaptive_output_name(AdaptiveOutput o);
std::string_view adaptive_metric_unavailable_name(AdaptiveMetricUnavailable m);
// 方向词表：Auto 渲染为 ""（旧语法不写方向词）；Down = "down"；Up = "up"。
std::string_view adaptive_direction_name(AdaptiveDirection d);
// 错误码全表（稳定顺序；测试对表断言，UI 可做错误分类）。当前 45 条（42 + 断点 3 条）。
const std::vector<std::string_view> &adaptive_error_codes();

// —— 持久化辅助：adaptive.policy 的「JSON 字符串数组，一行一条」（需求 3/6）——
// 内部 AST/预设数据仍用**单个 DSL 文本**（std::string，\n 分隔）承载；只有 JSON 边界做数组
// ⇄ 文本转换（零 \n 转义的落盘形态，人类可读、可 git diff）：
//   * `adaptive_policy_to_lines`：文本 → 行数组。按 '\n' 拆行、剥行尾 '\r'、**丢弃末尾空行**
//     （策略末尾空行无语义）；中间空行/注释行原样保留。
//   * `adaptive_policy_from_lines`：行数组 → 文本。'\n' 连接并补末尾换行（规范文本形态，
//     `to_lines(from_lines(L)) == L`）。任一行内嵌 '\n' / '\r' ⇒ 失败 + 中文原因
//     （保住「数组下标 + 1 = DSL 行号」的错误定位口径）。
std::vector<std::string> adaptive_policy_to_lines(std::string_view text);
bool adaptive_policy_from_lines(const std::vector<std::string> &lines, std::string &out,
                                std::string *err_zh);
// 旧字符串形态 → 规范文本：按 '\n' / '\r\n' 拆行后走 `adaptive_policy_from_lines`。
std::string adaptive_policy_from_legacy_string(std::string_view text);

} // namespace pp
