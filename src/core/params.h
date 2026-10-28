// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — ParamSchema (M0 frozen)
#pragma once
#include "core/types.h"
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

namespace pp {

enum class ParamType { Int, Float, Bool, Enum, String };

struct ParamDef {
    std::string key;
    std::string label; // Chinese label, UTF-8
    ParamType type = ParamType::Int;
    ParamValue def;                                          // default (visual-transparent tier)
    double lo = 0, hi = 0, step = 1;                         // Int / Float
    std::vector<std::pair<std::string, ParamValue>> choices; // Enum {name, value}
    bool advanced = false;
    std::string tooltip; // Chinese explanation + English term
    // Predicates (optional; filled in M1, empty in M0)
    std::function<bool(const ParamSet &)> visible;
    std::function<std::optional<ParamValue>(const ParamSet &)> locked;
    // —— M7-WP6 / UX-8 加性字段（**追加在结构体末尾**；既有聚合初始化逐字兼容，
    //    未写本字段的旧写法 = 空串）。分组单源：高级区子分组不再由 UI 按 key 词根启发式
    //    推断（原 paramform 的 subgroup_of），改由本字段显式驱动。取值域见下方 kGroup*。
    //    契约：docs/ux-review-14.md §8 改进方案 2「分组由 schema 显式字段驱动，废除
    //    key 词根启发式（params.h ParamDef 加 group 字段；format_tables.cpp 82 条补 group）」。
    std::string group;
    // —— 第二轮审计 AVIF-01 / HEIF complexity 加性字段（末尾；旧聚合初始化零影响）——
    // libheif 的“可选覆盖”参数（has_default=false，如 SVT qp）不能把范围下限当默认值下发；
    // false ⇒ default_params/fill_defaults/make_defaults 不写入该键，编码器按“未设置”处理。
    bool has_default = true;
};

// —— M7-WP6 / UX-8：ParamDef::group 的取值域（单源词表）——
// 核心参数 → kGroupCore（不入子分组折叠）；高级参数 → 四个子分组之一（二级折叠的四段，
// 默认只展开第一组，见 docs/ux-review-14.md §8）。空串 = 未分类（应视为缺陷，test_smoke 有断言）。
// 形态 = `const char *`（不是 string_view）：聚合初始化 `.group = kGroupCore` 需要**隐式**转换到
// std::string，而 string_view → string 的构造是 explicit（实测 C2440）。
inline constexpr const char *kGroupCore = "核心";
inline constexpr const char *kGroupProgressive = "渐进";
inline constexpr const char *kGroupNoise = "噪声";
inline constexpr const char *kGroupDecodeBudget = "解码预算";
inline constexpr const char *kGroupEncodeTools = "编码工具";

struct TechDef {
    std::string id, label;
    bool lossless_capable = false;
    std::vector<ParamDef> params;
};

struct BackendDef {
    std::string id, label;             // e.g. "svt-av1" / "libaom"
    bool runtime_introspected = false; // true for libheif-based formats
    std::vector<TechDef> techs;
};

struct FormatDef {
    std::string id, label, ext; // e.g. "jxl"
    std::vector<BackendDef> backends;
    // R-211（M7-WP2 注记，不改类型）：本字段是 **int 双语义** ——
    //   * jpeg/jxl/png/tiff/webp/heif/avif：每**样本**位数（bps，8/16 或 8/10/12）；
    //   * bmp：每**像素**位数（bpp：24 = RGB 24bpp = 每样本 8 位；8 = 灰度 8bpp）。
    // 单位随格式翻转，取值/校验处（pipeline plan_targets、presets validate、UI 位深行、
    // enc_oiio bmp 分支）按格式各自解释。彻底根治需拆字段/强类型（涉及 UI 与 pipeline），
    // 本波按评审口径先显式化（本注释 + format_tables bmp 行 + enc_oiio 错误文案），登记裁定。
    // M7-R2B 补齐：单位判定与 bps↔bpp 换算收敛为单源函数 bitdepth_unit/bitdepth_unit_label/
    // bitdepth_bpp/bitdepth_bps（见本文件尾部加性块，R-211）；取值/校验处不再各自翻转单位。
    std::vector<int> bitdepths; // {8} / {8,16} / bmp: {8,24}（单位见上）
    bool supports_alpha = false;
    bool supports_gray = false;
    std::string meta_path; // "exiv2" | "libheif" | "jxl-box" | "none"
};

// Static format tables. libheif-based formats (heif/avif) have empty techs and
// runtime_introspected=true; their params come from libheif at runtime (M1).
const std::vector<FormatDef> &static_formats();

// 运行时内省后端（heif/avif）：返回带 live tech/params 的 backend 列表。
// 定义在 codecs/enc_heif.cpp；UI/dev CLI 的参数类型解析必须用它，不能只看静态表。
std::vector<BackendDef> introspect_backends(std::string_view format_id);

// PP-FROZEN(block): 追加在 static_formats() 声明之后
// —— 参数引擎 ——
// 求 lock：无 locked 谓词或返回 nullopt → 未锁定；返回有值 → 该值为强制值
std::optional<ParamValue> eval_lock(const ParamDef &p, const ParamSet &s);
bool eval_visible(const ParamDef &p, const ParamSet &s);

// PP-FROZEN(0.3.0) §3.6 · 无损的两个承载面（**T13 定稿：内部键 `__lossless` 保留，不退场**）
//   * `kLosslessParamKey` = "lossless"：**显式 schema 参数**（format_tables.cpp 的 Bool，给
//     lossless-capable 的技术声明：jxl vardct/modular、webp lossy/lossless；heif/avif 由
//     libheif 运行时内省提供）。它是用户可见可设、可序列化（预设 v2 的 params）的面。
//   * `kLosslessKey` = "__lossless"：**内部管道键**（0.3.0 保留，不落盘、不显示、不进 values()）：
//     谓词（eval_visible/eval_lock）与编码器（enc_jxl/enc_webp）的既有输入。
//   两键的一致性口径（T13 复核项 5 措辞修正）：`apply_locks()` 是**引擎侧**的同步点（在选定技术
//   声明了显式键时按 lossless 入参写两键；default_params/normalize_preset 都经它）；表单
//   （paramform 的复选框 → `values[__lossless]`，声明时另写 `values[lossless]`）与输出页
//   （`spec_of()` 写 `__lossless`）在各自集合里写入**同一个** `sel.lossless`，随后同样经
//   apply_locks 收口 → 当前无漂移路径；读取统一走 lossless_flag()，禁止各调用方自行分叉。
//   去留裁定（T13 出口硬项之一，二选一）：**保留** —— 退场需要改 12 处谓词 + 2 个编码器的读取点，
//   且会让 0.2 形态调用方（无显式键）失去无损语义；保留 + 单点同步的代价最小、零行为漂移。
//   非声明格式（jpeg/png/tiff/bmp）的落盘口径见 src/ui/preset_io.cpp 的 `outputs[i].lossless`。
inline constexpr std::string_view kLosslessParamKey = "lossless";
inline constexpr std::string_view kLosslessKey = "__lossless";

// PP-FROZEN(0.3.0) · 无损标志读取（**处置顺序**：显式 schema 参数优先 → 内部管道键 → false）。
// 预设 v1 迁移、输出页 config_base/collect_preset、presets 校验/归一都走这一条，杜绝两张皮。
bool lossless_flag(const ParamSet &s);

// 取默认值全集（含 lossless 技术与技术默认选择）
ParamSet default_params(const FormatDef &f, const std::string &backend_id,
                        const std::string &tech_id, bool lossless);

// 应用锁定：把所有被锁定参数的当前值改写为强制值（返回被改写的 key 列表）
std::vector<std::string> apply_locks(const FormatDef &f, const std::string &backend_id,
                                     const std::string &tech_id, bool lossless, ParamSet &s);

// 类型安全取值（缺失或类型不符 → 返回 fallback）
int64_t param_int(const ParamSet &s, std::string_view key, int64_t fallback);
double param_float(const ParamSet &s, std::string_view key, double fallback);
bool param_bool(const ParamSet &s, std::string_view key, bool fallback);
std::string param_str(const ParamSet &s, std::string_view key, std::string_view fallback);

// 校验：范围/枚举成员/类型；返回空串=通过，否则错误描述（英文）
std::string validate_params(const FormatDef &f, const std::string &backend_id,
                            const std::string &tech_id, bool lossless, const ParamSet &s);

// 查找：找不到返回 nullptr
const FormatDef *find_format(std::string_view id);
const BackendDef *find_backend(const FormatDef &f, std::string_view id);
const TechDef *find_tech(const BackendDef &b, std::string_view id);

// 旧预设迁移：缺失 key 用默认值补齐（返回补的 key 列表）
std::vector<std::string> fill_defaults(const FormatDef &f, const std::string &backend_id,
                                       const std::string &tech_id, bool lossless, ParamSet &s);

// 参数快照（日志用）："key=value key=value"（按 key 字典序，value 统一字符串化）
std::string snapshot_params(const ParamSet &s);

// M2-T5: cross-field constraints the per-key predicates cannot express.
// Empty result = OK. Messages are user-facing (frozen texts).
std::vector<std::string> cross_validate(const ParamSet &values, const std::string &format_id,
                                        const std::string &tech_id);

// —— M8 加性（B0，追认 M8-A2）：参数上下文 + 加性重载 ——
// 旧签名的行为/文案零改动（B1 转发到本重载）；新校验需要 backend/位深/尺寸/无损上下文。
struct ParamContext {
    std::string format_id;
    std::string backend_id;
    std::string tech_id;
    int bitdepth = 0;
    bool lossless = false;
    int width = 0;
    int height = 0;
};
std::vector<std::string> cross_validate(const ParamContext &ctx, const ParamSet &values);

// —— M7-WP2 加性追认（R-208 技术/无损判定收敛、R-207 跨字段规则绑键常量；裁定登记
//    build/m7-wp2/handoff.md）：以下声明**追加在既有声明之后**，既有签名零改动。——
// 技术选择单源（原 params.cpp `select_tech` 与 presets.cpp `pick_tech` 逐字重复 → 合一）：
// 显式 id → find_tech；空 id → 首选（lossless 时优先 lossless_capable 的技术）。
const TechDef *select_tech(const BackendDef &b, std::string_view tech_id, bool lossless);
// 无损"显式 schema 参数"声明判定单源（原 presets.cpp `declares_lossless` 与 params.cpp
// `declares_lossless_param` 语义并存 → 合一）：该 (format, backend, tech) 是否把无损暴露
// 为显式 schema 参数（key = kLosslessParamKey）。libheif 系（runtime_introspected）由
// 运行时内省提供同名字段 ⇒ 视为声明（与 0.3.0 迁移口径一致）。
bool declares_lossless(const FormatDef &f, const std::string &backend_id,
                       const std::string &tech_id);
// 跨字段规则的绑键常量（R-207）：规则不再用裸字符串引用键名（表改 key 时编译期可见）。
inline constexpr std::string_view kKeyQmin = "qmin";
inline constexpr std::string_view kKeyQmax = "qmax";
inline constexpr std::string_view kKeyProgressive = "progressive";
inline constexpr std::string_view kKeyOptimizeCoding = "optimize_coding";
inline constexpr std::string_view kKeyTiffTileWidth = "tiff_tile_width";
inline constexpr std::string_view kKeyTiffTileHeight = "tiff_tile_height";

// —— M7-R2B 加性追认（裁定登记 build/m7r2-b/handoff.md「裁定登记」节）：以下声明**追加在
//    既有声明之后**，既有签名零改动、既有行为零改动。——
//
// R-195：ParamDef 查找决胜规则**单源（first-wins）**。旧形态两套实现且决胜相反——
// paramform.cpp `find_param` / preset_io.cpp `lookup_param_def` 首命中即返回（first-wins），
// dev_cli.cpp `set_param` 循环覆盖 def 指针（last-wins）；同 key 多声明（jxl vardct/modular
// 镜像、webp lossy/lossless 镜像）时两处解析出不同 ParamDef。收敛口径 = **first-wins**：
// 同一 TechDef::params 序内首条命中为准（表序即优先级），另一处转发到本函数（跨界需求登记）。
const ParamDef *find_param(const TechDef &t, std::string_view key);
// 全链查找单源（format → backend → tech → key）：优先**命中技术**内 first-wins；该技术不声明
// 时回退到后端全部技术的并集扫描（同为 first-wins，表序优先）。找不到返回 nullptr。
// （preset_io.cpp 的 lookup_param_def 即本语义的第二份手写实现，待转发。）
const ParamDef *lookup_param_def(const FormatDef &f, const std::string &backend_id,
                                 const std::string &tech_id, std::string_view key);

// R-211：out_bitdepth 双语义（8/16 bps vs bmp 24 bpp）的**单位单源**（加性；不拆字段——
// 拆字段涉 UI+pipeline 冻结面，维持「登记待解冻」，见 handoff 裁定登记）。
// 单位判定：bmp = 每**像素**位数（bpp：24 = RGB 24bpp = 每样本 8 位；8 = 灰度 8bpp）；
// 其余格式 = 每**样本**位数（bps：8/16 或 8/10/12）。
enum class BitdepthUnit : unsigned char { BitsPerSample, BitsPerPixel };
BitdepthUnit bitdepth_unit(std::string_view format_id);
// 显示单位标签数据（UI 位深行/tooltip 消费，R2D2）："bps" | "bpp"。
std::string_view bitdepth_unit_label(std::string_view format_id);
// 换算单源（RGB 三样本打包口径）：
//   * bitdepth_bpp：存储值 → 每像素位数。非 bmp：bps × 3（8→24、16→48）；bmp：存储值已是
//     bpp → 恒等（8→8、24→24）。
//   * bitdepth_bps：存储值 → 每样本位数。非 bmp：恒等；bmp：24 bpp（RGB）→ 8 bps，
//     8 bpp（灰度 8 位/样本）→ 8 bps。
//   灰度口径注记：非 bmp 的 ×3 假定 RGB 打包（灰度 = ×1，由调用方按 supports_gray/实际通道
//   折算）；bmp 的 {8,24} 两值语义由上式精确覆盖（测试钉住）。
int bitdepth_bpp(std::string_view format_id, int bitdepth);
int bitdepth_bps(std::string_view format_id, int bitdepth);

} // namespace pp
