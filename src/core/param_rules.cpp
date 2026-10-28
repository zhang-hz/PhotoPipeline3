// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M8 联动规则引擎实现（B1；接口冻结见 core/param_rules.h，B0 冻结）。
//
// 单一规则源：docs/m8-param-control-map.md §2–§6 的 G3/G4/X1–X17/A1–A16/S1–S6/J1–J18
// 逐条落成**数据驱动规则表**（Rule{id, format, backend, action, drivers, targets, when,
// lock, message}）。求值分两档：
//   * evaluate()  —— UI 实时态：hidden / disabled / locked / warnings / errors；
//   * preflight() —— 运行前预检：等价于 evaluate().errors（批量只报一次由 UI 层负责）。
//
// 语义约定（与 param_rules.h 头部契约一致）：
//   * 纯函数：不读写全局状态；输入 = ParamContext + 当前 ParamSet；
//   * hidden/disabled 只影响呈现，不改变 ParamSet；locked 是「强制值」，UI 显示为只读；
//   * errors 非空 ⇒ 禁止开始运行；warnings 不阻断；
//   * 旧 cross_validate(values, format, tech) 不经过本引擎（行为/文案零改动）；
//     params.cpp 的加性重载 = 旧通道消息 + 本引擎 preflight。
//
// 三态口径（C2）：目录行 has_default=false，未启用 = 键不在 ParamSet = 规则按「未设置」
// 处理（不误报）。所有 key 一律用目录键名（x265:/aom: 前缀逐字，`__` 只读行不参与）。
#include "core/param_rules.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pp::rules {
namespace {

// ---------------------------------------------------------------------------
// 求值上下文：ParamContext + ParamSet 的只读便捷访问器
// ---------------------------------------------------------------------------
struct EvalCtx {
    std::string format;
    std::string backend;
    std::string tech;
    int bitdepth = 0;
    int width = 0;
    int height = 0;
    bool lossless = false;
    bool modular = false;
    const ParamSet *values = nullptr;

    bool has(std::string_view key) const { return values->find(std::string(key)) != values->end(); }

    // 透传前缀扫描（跨后端键 / svt: 假接线检测；"__" 保留键不参与）
    bool has_prefix(std::string_view prefix) const {
        for (const auto &[key, value] : *values) {
            (void)value;
            if (key.size() >= prefix.size() && key.compare(0, prefix.size(), prefix) == 0)
                return true;
        }
        return false;
    }

    int64_t i(std::string_view key, int64_t fallback) const {
        return param_int(*values, key, fallback);
    }
    double d(std::string_view key, double fallback) const {
        return param_float(*values, key, fallback);
    }
    bool b(std::string_view key, bool fallback) const { return param_bool(*values, key, fallback); }
    std::string s(std::string_view key, std::string_view fallback) const {
        return param_str(*values, key, fallback);
    }
};

enum class Action : unsigned char { Hide, Disable, Lock, Warn, Error };

std::string_view action_name(Action a) {
    switch (a) {
    case Action::Hide:
        return "hide";
    case Action::Disable:
        return "disable";
    case Action::Lock:
        return "lock";
    case Action::Warn:
        return "warn";
    case Action::Error:
        return "error";
    }
    return "?";
}

struct Rule {
    std::string_view id;      // control-map 规则编号（G3/X1/A2/S4/J6…）
    std::string_view format;  // 精确匹配；"" = 任意
    std::string_view backend; // 精确匹配；"" = 任意
    Action action = Action::Warn;
    std::vector<std::string_view> drivers;           // 规则读取的锚点/依赖键（锚点闭合测试用）
    std::vector<std::string_view> targets;           // 被隐藏/禁用/锁定的键
    std::function<bool(const EvalCtx &)> when;       // 空 = 恒成立
    std::function<ParamValue(const EvalCtx &)> lock; // Lock 动作的强制值
    std::string message;                             // 中文：Warn/Error 用户文案；其余 = 原因注记
};

// ---------------------------------------------------------------------------
// 复用条件（数据表里只写 lambda 一次，避免同一判据散落多行文字）
// ---------------------------------------------------------------------------
bool always(const EvalCtx &) { return true; }
bool lossless_on(const EvalCtx &c) { return c.lossless; }

// x265 透传 RC 路径（crf/qp）任一启用 ⇒ 覆盖 quality 默认路径（G4/X1）。
bool x265_rc_override(const EvalCtx &c) { return c.has("x265:crf") || c.has("x265:qp"); }

// JXL「是否 Modular 路径」：lossless 恒走 Modular；否则看技术选择 / Modular-only 键在场。
bool jxl_modular_only_key(std::string_view key) {
    static constexpr std::string_view keys[] = {"modular_group_size",
                                                "modular_predictor",
                                                "modular_palette_colors",
                                                "modular_lossy_palette",
                                                "brotli_effort",
                                                "responsive",
                                                "channel_colors_global_percent",
                                                "channel_colors_group_percent",
                                                "modular_ma_tree_learning_percent",
                                                "modular_nb_prev_channels",
                                                "color_transform"};
    for (const std::string_view candidate : keys)
        if (candidate == key)
            return true;
    return false;
}

bool params_indicate_modular(const ParamSet &values) {
    for (const auto &[key, value] : values) {
        (void)value;
        if (jxl_modular_only_key(key))
            return true;
    }
    return false;
}

bool jxl_lossy_modular(const EvalCtx &c) { return c.modular && !c.lossless; }

// J4 边界（逐文件检查；width/height 未知 = 0 → 跳过，避免误报）。
bool center_x_out_of_bounds(const EvalCtx &c) {
    return c.width > 0 && c.has("group_order_center_x") &&
           c.i("group_order_center_x", 0) >= c.width;
}
bool center_y_out_of_bounds(const EvalCtx &c) {
    return c.height > 0 && c.has("group_order_center_y") &&
           c.i("group_order_center_y", 0) >= c.height;
}

// J14：intrinsic_size 文本 = "宽x高"（两分量同时给出；0/空 = 不写）。
bool parse_intrinsic_size(const std::string &text, int64_t &x, int64_t &y) {
    std::size_t sep = text.find('x');
    if (sep == std::string::npos)
        sep = text.find('X');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= text.size())
        return false;
    const auto parse = [](std::string_view sv, int64_t &out) {
        const char *b = sv.data();
        const char *e = b + sv.size();
        const std::from_chars_result r = std::from_chars(b, e, out);
        return r.ec == std::errc() && r.ptr == e;
    };
    const std::string_view sv(text);
    return parse(sv.substr(0, sep), x) && parse(sv.substr(sep + 1), y);
}

bool intrinsic_size_invalid(const EvalCtx &c) {
    if (!c.has("intrinsic_size"))
        return false;
    int64_t x = 0, y = 0;
    if (!parse_intrinsic_size(c.s("intrinsic_size", ""), x, y))
        return true; // 格式非法
    if (x < 0 || y < 0)
        return true;
    if ((x == 0) != (y == 0))
        return true; // 必须成对：要么都不写（0x0），要么都给
    constexpr int64_t kMax = int64_t{1} << 30;
    return x > kMax || y > kMax;
}

bool frame_name_too_long(const EvalCtx &c) {
    return c.has("frame_name") && c.s("frame_name", "").size() > 1071; // libjxl encode.cc:2759
}

bool frame_name_present(const EvalCtx &c) {
    return c.has("frame_name") && !c.s("frame_name", "").empty();
}

// J17：HDR 色调映射组「一起出现」；只设一部分时提示（全设或全不设不提示）。
int hdr_group_present(const EvalCtx &c) {
    int n = 0;
    for (const char *key :
         {"intensity_target", "min_nits", "relative_to_max_display", "linear_below"})
        if (c.has(key))
            ++n;
    return n;
}

// J18：level 5 容不下 16 位「uses_original_profile」路径（modular_16_bit_buffer_sufficient
// 由 uses_original_profile=true && bits>12 决定需要 level 10；encode.cc:1331-1360/786-800）。
bool jxl_level5_16bit_conflict(const EvalCtx &c) {
    if (c.bitdepth != 16 || !c.has("codestream_level") || c.i("codestream_level", -1) != 5)
        return false;
    return c.lossless || c.modular || c.b("uses_original_profile", false);
}

// 锁值构造器（ParamValue 变体顺序：monostate/bool/int64/double/string）。
ParamValue lock_str(const char *s) { return ParamValue{std::string(s)}; }
ParamValue lock_int(int64_t v) { return ParamValue{v}; }
ParamValue lock_bool(bool v) { return ParamValue{v}; }

// ---------------------------------------------------------------------------
// 规则表（唯一真源；顺序 = 求值顺序，影响 warnings/errors 的稳定顺序）
// ---------------------------------------------------------------------------
std::vector<Rule> build_rules() {
    std::vector<Rule> r;
    auto add = [&r](Rule rule) { r.push_back(std::move(rule)); };

    // ======================= 跨后端 / 透传前缀（C3/S6/G1） =======================
    add({"G1",
         "heif",
         "x265",
         Action::Error,
         {},
         {},
         [](const EvalCtx &c) { return c.has_prefix("aom:"); },
         {},
         "跨后端参数：'aom:' 前缀只对 AVIF/libaom 合法（当前 heif/x265）；"
         "HEIF 的插件透传前缀只有 x265:"});
    add({"S6",
         "heif",
         "x265",
         Action::Error,
         {},
         {},
         [](const EvalCtx &c) { return c.has_prefix("svt:"); },
         {},
         "S6：'svt:' 前缀不是合法透传通道（libheif 1.23.5 的 SVT 插件没有 aom:/svt: 透传，"
         "SVT-AV1 仅 10 个原生键）；请改用原生键或删除该键"});
    add({"G1",
         "avif",
         "libaom",
         Action::Error,
         {},
         {},
         [](const EvalCtx &c) { return c.has_prefix("x265:"); },
         {},
         "跨后端参数：'x265:' 前缀只对 HEIF/x265 合法（当前 avif/libaom）"});
    add({"S6",
         "avif",
         "libaom",
         Action::Error,
         {},
         {},
         [](const EvalCtx &c) { return c.has_prefix("svt:"); },
         {},
         "S6：'svt:' 前缀不是合法透传通道（libheif 1.23.5 无 svt 透传）；SVT-AV1 仅 10 个原生键"});
    add({"S6",
         "avif",
         "svt-av1",
         Action::Error,
         {},
         {},
         [](const EvalCtx &c) {
             return c.has_prefix("svt:") || c.has_prefix("x265:") || c.has_prefix("aom:");
         },
         {},
         "S6：SVT-AV1 既无 'svt:' 透传通道也不接受 x265:/aom: 前缀（仅 10 个原生键）；"
         "请删除这些键"});
    add({"G1",
         "jxl",
         "libjxl",
         Action::Error,
         {},
         {},
         [](const EvalCtx &c) {
             return c.has_prefix("x265:") || c.has_prefix("aom:") || c.has_prefix("svt:");
         },
         {},
         "jxl 不接受插件透传前缀（x265:/aom:/svt: 均非法）；参数必须是目录内键"});

    // ============================== G3：无损（分后端） ==============================
    // —— HEIF/x265：quality/RC 组隐藏、chroma 锁 444 ——
    add({"G3",
         "heif",
         "x265",
         Action::Hide,
         {},
         {"quality"},
         lossless_on,
         {},
         "无损模式：quality 被插件 bLossless 覆盖，隐藏 RC 默认路径行"});
    add({"G3",
         "heif",
         "x265",
         Action::Hide,
         {},
         {"x265:crf", "x265:qp", "x265:qpmin", "x265:qpmax"},
         lossless_on,
         {},
         "无损模式：速率控制组（crf/qp/qpmin/qpmax）整组隐藏（G4 无路径可选）"});
    add({"G3",
         "heif",
         "x265",
         Action::Lock,
         {},
         {"chroma"},
         lossless_on,
         [](const EvalCtx &) { return lock_str("444"); },
         "无损承诺：chroma 锁定 444（4:2:0/4:2:2 的抽样有损，编码器侧同样强制）"});
    add({"G3",
         "heif",
         "x265",
         Action::Warn,
         {},
         {},
         lossless_on,
         {},
         "G3：无损模式已锁定 chroma=444 并隐藏 RC 组；profile/level 由位深与色度推导（只读行），"
         "HEVC 码流无损经 RGB→YCbCr 舍入仍可能有损"});

    // —— AVIF/libaom：quality/cq-level 隐藏、chroma 444 + aq-mode/chroma-deltaq 锁定 ——
    add({"G3",
         "avif",
         "libaom",
         Action::Hide,
         {},
         {"quality"},
         lossless_on,
         {},
         "无损模式：quality 被 bLossless 覆盖，隐藏（速率控制路径见 aom:cq-level）"});
    add({"G3",
         "avif",
         "libaom",
         Action::Hide,
         {},
         {"aom:cq-level"},
         lossless_on,
         {},
         "无损模式：速率控制组整组隐藏（G4 无路径可选）"});
    add({"G3",
         "avif",
         "libaom",
         Action::Lock,
         {},
         {"chroma"},
         lossless_on,
         [](const EvalCtx &) { return lock_str("444"); },
         "无损承诺：chroma 锁定 444（编码器侧同样强制 4:4:4）"});
    add({"A2",
         "avif",
         "libaom",
         Action::Lock,
         {},
         {"aom:aq-mode"},
         lossless_on,
         [](const EvalCtx &) { return lock_int(0); },
         "A2：无损时 aq-mode 锁定 0（自适应量化在无损路径无意义）"});
    add({"A2",
         "avif",
         "libaom",
         Action::Lock,
         {},
         {"aom:enable-chroma-deltaq"},
         lossless_on,
         [](const EvalCtx &) { return lock_bool(false); },
         "A2：无损时 enable-chroma-deltaq 锁定 0（色度 delta-q 会破坏无损）"});
    add({"A12",
         "avif",
         "libaom",
         Action::Disable,
         {},
         {"tune"},
         lossless_on,
         {},
         "A12：无损模式不支持 tune（含 tune=iq）；调优在无损路径无意义"});
    add({"A12",
         "avif",
         "libaom",
         Action::Warn,
         {"tune"},
         {},
         [](const EvalCtx &c) { return c.lossless && c.s("tune", "") == "iq"; },
         {},
         "A12：无损与 tune=iq 互斥（无损下 tune 被禁用；请把 tune 改回 psnr/ssim）"});
    add({"G3",
         "avif",
         "libaom",
         Action::Warn,
         {},
         {},
         lossless_on,
         {},
         "G3：AVIF 无损锁定 chroma=444 / aq-mode=0 / enable-chroma-deltaq=0；"
         "AV1 的 YCbCr 码流无损不是 RGB 逐位无损（评分可能低于 100）"});

    // —— AVIF/SVT-AV1：产品当前无无损路径（S2） ——
    add({"S2",
         "avif",
         "svt-av1",
         Action::Disable,
         {"lossless"},
         {"lossless"},
         always,
         {},
         "S2：SVT-AV1 插件仅 4:2:0 且无 chroma 参数，编码器对无损硬失败；请切换后端为 libaom"});
    add({"S2",
         "avif",
         "svt-av1",
         Action::Error,
         {"lossless"},
         {},
         lossless_on,
         {},
         "S2：SVT-AV1 不支持无损编码（当前产品会硬失败）；请把 AVIF 后端切换为 libaom 或关闭无损"});

    // —— JXL：强制 Modular、distance 隐藏、resampling=1、modular_lossy_palette=false ——
    add({"G3",
         "jxl",
         "libjxl",
         Action::Hide,
         {},
         {"distance"},
         lossless_on,
         {},
         "无损模式：distance 由 JxlEncoderSetFrameLossless 接管，隐藏"});
    add({"G3",
         "jxl",
         "libjxl",
         Action::Lock,
         {},
         {"resampling"},
         lossless_on,
         [](const EvalCtx &) { return lock_int(1); },
         "无损承诺：resampling 锁定 1（JXL-01 假无损防线）"});
    add({"G3",
         "jxl",
         "libjxl",
         Action::Lock,
         {},
         {"modular_lossy_palette"},
         lossless_on,
         [](const EvalCtx &) { return lock_bool(false); },
         "无损承诺：modular_lossy_palette 锁定关闭"});
    add({"J2",
         "jxl",
         "libjxl",
         Action::Error,
         {"uses_original_profile"},
         {},
         [](const EvalCtx &c) {
             return c.lossless && c.has("uses_original_profile") &&
                    !c.b("uses_original_profile", true);
         },
         {},
         "J2：无损编码要求 uses_original_profile=true（libjxl 拒绝 xyb_encoded 下的"
         " JxlEncoderSetFrameLossless）"});
    add({"G3",
         "jxl",
         "libjxl",
         Action::Warn,
         {},
         {},
         lossless_on,
         {},
         "G3：无损强制 Modular 路径（VarDCT-only 行不可见）、resampling=1、"
         "modular_lossy_palette=false；真无损产物评分应为 100"});

    // ============================ G4：速率控制模式互斥 ============================
    add({"X1",
         "heif",
         "x265",
         Action::Error,
         {"x265:crf", "x265:qp"},
         {},
         [](const EvalCtx &c) { return c.has("x265:crf") && c.has("x265:qp"); },
         {},
         "X1（G4）：速率控制路径互斥：x265:crf 与 x265:qp 不能同时启用"
         "（只保留一个；quality 是默认路径）"});
    add({"G4",
         "heif",
         "x265",
         Action::Hide,
         {"x265:crf", "x265:qp"},
         {"quality"},
         x265_rc_override,
         {},
         "G4：已启用透传速率控制（x265:crf/x265:qp），quality 不生效（直通最后应用覆盖）"});
    add({"G4",
         "heif",
         "x265",
         Action::Warn,
         {"x265:crf", "x265:qp"},
         {},
         x265_rc_override,
         {},
         "G4：x265:crf/x265:qp 在全部内置控制之后应用并覆盖 quality；请确认只保留一条路径"});
    add({"A1",
         "avif",
         "libaom",
         Action::Hide,
         {"aom:cq-level"},
         {"quality"},
         [](const EvalCtx &c) { return c.has("aom:cq-level"); },
         {},
         "A1（G4）：已启用 aom:cq-level，quality 不生效（aom: 直通最后应用）"});
    add({"A1",
         "avif",
         "libaom",
         Action::Warn,
         {"aom:cq-level"},
         {},
         [](const EvalCtx &c) { return c.has("aom:cq-level"); },
         {},
         "A1：aom:cq-level 覆盖 quality；两条速率控制路径只能选一条"});
    add({"S1",
         "avif",
         "svt-av1",
         Action::Disable,
         {"qp"},
         {"quality"},
         [](const EvalCtx &c) { return c.has("qp"); },
         {},
         "S1（G4）：qp 已启用，quality 不生效（qp 优先）"});
    add({"S1",
         "avif",
         "svt-av1",
         Action::Warn,
         {"qp"},
         {},
         [](const EvalCtx &c) { return c.has("qp"); },
         {},
         "S1：qp 已启用；quality 将被忽略（qp 优先）"});

    // ============================== X：x265 联动规则 ==============================
    add({"X1B",
         "heif",
         "x265",
         Action::Error,
         {"x265:qpmin", "x265:qpmax"},
         {},
         [](const EvalCtx &c) {
             return c.has("x265:qpmin") && c.has("x265:qpmax") &&
                    c.i("x265:qpmin", 0) > c.i("x265:qpmax", 0);
         },
         {},
         "X1B：x265:qpmin 不能大于 x265:qpmax"});
    add({"X2",
         "heif",
         "x265",
         Action::Disable,
         {"x265:aq-mode"},
         {"x265:aq-strength"},
         [](const EvalCtx &c) { return c.has("x265:aq-mode") && c.i("x265:aq-mode", 1) == 0; },
         {},
         "X2：aq-mode=0 时 aq-strength 不生效"});
    add({"X2",
         "heif",
         "x265",
         Action::Disable,
         {"x265:aq-mode"},
         {"x265:rd-refine"},
         [](const EvalCtx &c) { return c.has("x265:aq-mode") && c.i("x265:aq-mode", 1) == 0; },
         {},
         "X2：rd-refine 需要 aq-mode≠0"});
    add({"X3",
         "heif",
         "x265",
         Action::Disable,
         {"x265:rdoq-level"},
         {"x265:psy-rdoq"},
         [](const EvalCtx &c) {
             return c.has("x265:rdoq-level") && c.i("x265:rdoq-level", 1) == 0;
         },
         {},
         "X3：psy-rdoq 需要 rdoq-level>0"});
    add({"X4",
         "heif",
         "x265",
         Action::Disable,
         {"x265:rd"},
         {"x265:cu-lossless", "x265:tskip", "x265:tskip-fast"},
         [](const EvalCtx &c) { return c.has("x265:rd") && c.i("x265:rd", 3) < 3; },
         {},
         "X4：rd<3 时 cu-lossless / tskip / tskip-fast 不可用"});
    add({"X4",
         "heif",
         "x265",
         Action::Disable,
         {"x265:rd"},
         {"x265:rd-refine"},
         [](const EvalCtx &c) { return c.has("x265:rd") && c.i("x265:rd", 3) < 5; },
         {},
         "X4：rd-refine 需要 rd≥5"});
    add({"X4",
         "heif",
         "x265",
         Action::Disable,
         {"x265:tskip"},
         {"x265:tskip-fast"},
         [](const EvalCtx &c) { return c.has("x265:tskip") && !c.b("x265:tskip", false); },
         {},
         "X4：tskip-fast 依赖 tskip=1"});
    add({"X6",
         "heif",
         "x265",
         Action::Warn,
         {"x265:ssim-rd", "x265:psy-rd"},
         {},
         [](const EvalCtx &c) {
             return c.b("x265:ssim-rd", false) && c.has("x265:psy-rd") &&
                    c.d("x265:psy-rd", 0.0) > 0.0;
         },
         {},
         "X6：ssim-rd=1 时 x265 会清除 psy-rd 设置（二选一）"});
    add({"X5",
         "heif",
         "x265",
         Action::Warn,
         {"chroma", "x265:psy-rd"},
         {},
         [](const EvalCtx &c) {
             return c.s("chroma", "420") == "444" && c.d("x265:psy-rd", 0.0) > 0.0;
         },
         {},
         "X5：chroma=444 且 psy-rd>0 时 x265 会改写色度 QP 偏移（库行为）"});
    add({"X10",
         "heif",
         "x265",
         Action::Disable,
         {"x265:sao"},
         {"x265:limit-sao", "x265:selective-sao"},
         [](const EvalCtx &c) { return c.has("x265:sao") && !c.b("x265:sao", true); },
         {},
         "X10：sao=0 时 limit-sao / selective-sao 不生效"});
    add({"X11",
         "heif",
         "x265",
         Action::Disable,
         {"x265:sao"},
         {"x265:sao-non-deblock"},
         [](const EvalCtx &c) { return c.has("x265:sao") && !c.b("x265:sao", true); },
         {},
         "X11：sao-non-deblock 仅在 sao=1 时有效"});
    add({"X12",
         "heif",
         "x265",
         Action::Disable,
         {"x265:level-idc"},
         {"x265:high-tier"},
         [](const EvalCtx &c) {
             return c.has("x265:high-tier") &&
                    (!c.has("x265:level-idc") || c.i("x265:level-idc", 0) == 0);
         },
         {},
         "X12：high-tier 仅在 level-idc>0 时有效"});
    add({"X12",
         "heif",
         "x265",
         Action::Warn,
         {"x265:level-idc", "x265:crf"},
         {},
         [](const EvalCtx &c) {
             return c.has("x265:level-idc") && c.i("x265:level-idc", 0) > 0 && c.has("x265:crf");
         },
         {},
         "X12：level-idc 与 CRF 同用会强制 VBV（码率/结果非确定）"});
    add({"X13",
         "heif",
         "x265",
         Action::Warn,
         {"chroma", "x265:psy-rd", "x265:cbqpoffs", "x265:crqpoffs"},
         {},
         [](const EvalCtx &c) {
             return c.s("chroma", "420") == "444" && c.d("x265:psy-rd", 0.0) > 0.0 &&
                    (c.has("x265:cbqpoffs") || c.has("x265:crqpoffs"));
         },
         {},
         "X13：4:4:4 + psy-rd>0 时 x265 可能自动改写 cbqpoffs/crqpoffs"});
    add({"X14",
         "heif",
         "x265",
         Action::Warn,
         {"complexity", "x265:wpp"},
         {},
         [](const EvalCtx &c) { return c.has("complexity") && c.i("complexity", 0) >= 90; },
         {},
         "X14：complexity≥90 会关闭 wpp（libheif 固定工具项）；显式 wpp 设置可能被覆盖"});
    // X9（逐文件时机；ctx 带图像尺寸时求值）：min-cu-size>8 且宽高非其倍数 ⇒ x265 自动
    // padding / conformance window（码流尺寸与画面尺寸不同，属警告不阻断）。
    add({"X9",
         "heif",
         "x265",
         Action::Warn,
         {"x265:min-cu-size"},
         {},
         [](const EvalCtx &c) {
             if (!c.has("x265:min-cu-size"))
                 return false;
             const int64_t cu = c.i("x265:min-cu-size", 8);
             if (cu <= 8 || c.width <= 0 || c.height <= 0)
                 return false;
             return c.width % cu != 0 || c.height % cu != 0;
         },
         {},
         "X9：min-cu-size>8 且宽高非其倍数，x265 会启用 padding/conformance window"
         "（码流尺寸与画面尺寸不同）"});
    add({"X15",
         "heif",
         "x265",
         Action::Warn,
         {"x265:psy-rd", "x265:psy-rdoq", "x265:aq-strength"},
         {},
         [](const EvalCtx &c) {
             return c.has("x265:psy-rd") || c.has("x265:psy-rdoq") || c.has("x265:aq-strength");
         },
         {},
         "X15：psy-rd/psy-rdoq/aq-strength 的部分效果会被 tune 与插件固定项"
         "（aq-mode/psy 基线）覆盖"});

    // ============================== A：libaom 联动规则 ==============================
    add({"A3",
         "avif",
         "libaom",
         Action::Warn,
         {"tune"},
         {},
         [](const EvalCtx &c) { return c.s("tune", "") == "iq"; },
         {},
         "A3：tune=iq 时库自动启用 QM/锐化/色度 delta-q（与手工设置可能重叠）"});
    add({"A4",
         "avif",
         "libaom",
         Action::Error,
         {"auto-tiles", "aom:tile-columns", "aom:tile-rows"},
         {},
         [](const EvalCtx &c) {
             return c.b("auto-tiles", false) &&
                    (c.has("aom:tile-columns") || c.has("aom:tile-rows"));
         },
         {},
         "A4：auto-tiles=1 与 aom:tile-columns / aom:tile-rows 互斥（二选一）"});
    add({"A5",
         "avif",
         "libaom",
         Action::Error,
         {"min-q", "max-q"},
         {},
         [](const EvalCtx &c) { return c.i("min-q", 0) > c.i("max-q", 63); },
         {},
         "A5：min-q 不能大于 max-q"});
    add({"A5",
         "avif",
         "libaom",
         Action::Error,
         {"aom:qm-min", "aom:qm-max"},
         {},
         [](const EvalCtx &c) {
             return c.has("aom:qm-min") && c.has("aom:qm-max") &&
                    c.i("aom:qm-min", 0) > c.i("aom:qm-max", 15);
         },
         {},
         "A5：aom:qm-min 不能大于 aom:qm-max"});
    add({"A5",
         "avif",
         "libaom",
         Action::Error,
         {"aom:min-partition-size", "aom:max-partition-size"},
         {},
         [](const EvalCtx &c) {
             return c.has("aom:min-partition-size") && c.has("aom:max-partition-size") &&
                    c.i("aom:min-partition-size", 0) > c.i("aom:max-partition-size", 0);
         },
         {},
         "A5：aom:min-partition-size 不能大于 aom:max-partition-size"});
    add({"A5",
         "avif",
         "libaom",
         Action::Error,
         {"aom:cq-level"},
         {},
         [](const EvalCtx &c) { return c.has("aom:cq-level") && c.i("aom:cq-level", 0) > 63; },
         {},
         "A5：aom:cq-level 必须在 0–63（直通不查范围，必须预检）"});
    add({"A6",
         "avif",
         "libaom",
         Action::Error,
         {"aom:matrix-coefficients", "chroma"},
         {},
         [](const EvalCtx &c) {
             return c.has("aom:matrix-coefficients") &&
                    c.s("aom:matrix-coefficients", "") == "identity" &&
                    c.s("chroma", "420") != "444";
         },
         {},
         "A6：matrix-coefficients=identity 需要 chroma=444"
         "（4:2:0/4:2:2 的抽样会破坏 RGB↔YCbCr 往返）"});
    add({"A7",
         "avif",
         "libaom",
         Action::Warn,
         {"aom:deltaq-mode", "aom:deltaq-strength"},
         {},
         [](const EvalCtx &c) {
             if (!c.has("aom:deltaq-strength") || !c.has("aom:deltaq-mode"))
                 return false;
             const int64_t mode = c.i("aom:deltaq-mode", 1);
             return mode != 4 && mode != 6;
         },
         {},
         "A7：aom:deltaq-strength 仅在 deltaq-mode=4/6 时有意义"});
    add({"A7",
         "avif",
         "libaom",
         Action::Warn,
         {"aom:deltaq-mode"},
         {},
         [](const EvalCtx &c) {
             return c.has("aom:deltaq-mode") && c.i("aom:deltaq-mode", 1) == 1;
         },
         {},
         "A7：deltaq-mode=1 依赖 TPL（libaom 自行开关相关工具，结果可能非预期）"});
    add({"A8",
         "avif",
         "libaom",
         Action::Disable,
         {"aom:enable-qm"},
         {"aom:qm-min", "aom:qm-max"},
         [](const EvalCtx &c) {
             return !c.b("aom:enable-qm", false) && (c.has("aom:qm-min") || c.has("aom:qm-max"));
         },
         {},
         "A8：enable-qm=0 时 qm-min/qm-max 不生效"});
    add({"A9",
         "avif",
         "libaom",
         Action::Warn,
         {"aom:use-intra-dct-only", "aom:use-intra-default-tx-only"},
         {},
         [](const EvalCtx &c) {
             return c.b("aom:use-intra-dct-only", false) &&
                    c.b("aom:use-intra-default-tx-only", false);
         },
         {},
         "A9（源码核实：非互斥，dct-only 优先）：两项同时启用时 "
         "use-intra-default-tx-only 被忽略"});
    add({"A10",
         "avif",
         "libaom",
         Action::Warn,
         {"realtime"},
         {},
         [](const EvalCtx &c) { return c.b("realtime", false); },
         {},
         "A10：realtime=1 改变 cpu-used（speed）域与并行行为（实时编码路径，非静止图常用）"});
    add({"A13",
         "avif",
         "libaom",
         Action::Warn,
         {"aom:row-mt"},
         {},
         [](const EvalCtx &c) { return c.has("aom:row-mt") && !c.b("aom:row-mt", true); },
         {},
         "A13：libheif 在 threads>1 时自动打开 row-mt；显式关闭可能被插件覆盖（建议保持未设置）"});
    add({"A14",
         "avif",
         "libaom",
         Action::Warn,
         {"aom:force-video-mode"},
         {},
         [](const EvalCtx &c) { return c.b("aom:force-video-mode", false); },
         {},
         "A14：force-video-mode=1 放弃 reduced still header（体积/兼容性变差）"});
    add({"A15",
         "avif",
         "libaom",
         Action::Warn,
         {},
         {},
         [](const EvalCtx &c) { return c.has_prefix("aom:"); },
         {},
         "A15：aom: 直通在全部内置控制之后应用，会覆盖同义内置参数"
         "（quality/chroma/tune 等）；请只保留一条控制路径"});
    add({"A16",
         "avif",
         "libaom",
         Action::Warn,
         {"aom:target-seq-level-idx", "aom:set-tier-mask"},
         {},
         [](const EvalCtx &c) {
             return c.has("aom:target-seq-level-idx") || c.has("aom:set-tier-mask");
         },
         {},
         "A16：level/tier 直通必须与插件推导的 level（只读行）一致；不一致会在编码期失败"});

    // ============================== S：SVT-AV1 联动规则 ==============================
    add({"S3",
         "avif",
         "svt-av1",
         Action::Warn,
         {"speed"},
         {},
         [](const EvalCtx &c) { return c.has("speed") && c.i("speed", 12) > 9; },
         {},
         "S3：all-intra 下 speed>9 会被钳到 9（域仍显示 0–13）"});
    add({"S4",
         "avif",
         "svt-av1",
         Action::Error,
         {"tile-cols"},
         {},
         [](const EvalCtx &c) { return c.has("tile-cols") && c.i("tile-cols", 4) > 16; },
         {},
         "S4：tile-cols 不能超过 16（AV1 Annex-A 约束）"});
    add({"S4",
         "avif",
         "svt-av1",
         Action::Error,
         {"tile-rows", "tile-cols"},
         {},
         [](const EvalCtx &c) {
             const int64_t rows = c.i("tile-rows", 4);
             const int64_t cols = c.i("tile-cols", 4);
             return rows > 0 && cols > 0 && rows * cols > 128;
         },
         {},
         "S4：tile-rows×tile-cols 不能超过 128 个 tile（AV1 Annex-A 约束）"});

    // ============================== J：JPEG XL 联动规则 ==============================
    add({"J3",
         "jxl",
         "libjxl",
         Action::Disable,
         {"upsampling_mode", "resampling"},
         {"upsampling_mode"},
         [](const EvalCtx &c) {
             if (!c.has("upsampling_mode"))
                 return false;
             const int64_t factor = c.i("resampling", -1);
             return factor != 2 && factor != 4 && factor != 8;
         },
         {},
         "J3：upsampling_mode 仅在 resampling=2/4/8 时有效（=1 时库无效果）"});
    add({"J4",
         "jxl",
         "libjxl",
         Action::Disable,
         {"group_order", "group_order_center_x", "group_order_center_y"},
         {"group_order_center_x", "group_order_center_y"},
         [](const EvalCtx &c) {
             return (c.has("group_order_center_x") || c.has("group_order_center_y")) &&
                    c.i("group_order", -1) != 1;
         },
         {},
         "J4：group_order_center_x/y 仅在 group_order=center(1) 时使用"});
    add({"J4",
         "jxl",
         "libjxl",
         Action::Error,
         {"group_order_center_x"},
         {},
         center_x_out_of_bounds,
         {},
         "J4：group_order_center_x 必须小于图像宽度（逐文件检查）"});
    add({"J4",
         "jxl",
         "libjxl",
         Action::Error,
         {"group_order_center_y"},
         {},
         center_y_out_of_bounds,
         {},
         "J4：group_order_center_y 必须小于图像高度（逐文件检查）"});
    add({"J5",
         "jxl",
         "libjxl",
         Action::Hide,
         {"box_compress"},
         {"brotli_effort"},
         [](const EvalCtx &c) { return !c.b("box_compress", false); },
         {},
         "J5：box_compress 未开启（box 不压缩）时 brotli_effort 无效果"});
    add({"J6",
         "jxl",
         "libjxl",
         Action::Error,
         {"effort", "expert_options"},
         {},
         [](const EvalCtx &c) {
             return c.has("effort") && c.i("effort", 7) == 11 && !c.b("expert_options", false);
         },
         {},
         "J6：effort=11 需要先启用 expert_options（JxlEncoderAllowExpertOptions，否则库拒绝）"});
    add({"J7",
         "jxl",
         "libjxl",
         Action::Error,
         {"disable_perceptual_heuristics", "uses_original_profile"},
         {},
         [](const EvalCtx &c) {
             return c.b("disable_perceptual_heuristics", false) && c.has("uses_original_profile") &&
                    !c.b("uses_original_profile", true);
         },
         {},
         "J7：disable_perceptual_heuristics=1 要求 uses_original_profile=true"
         "（libjxl 拒绝 non-perceptual XYB）"});
    add({"J7",
         "jxl",
         "libjxl",
         Action::Warn,
         {"disable_perceptual_heuristics", "uses_original_profile"},
         {},
         [](const EvalCtx &c) {
             return c.b("disable_perceptual_heuristics", false) && !c.has("uses_original_profile");
         },
         {},
         "J7：disable_perceptual_heuristics=1 需要 uses_original_profile=true；未显式设置时"
         "按无损/Modular/带 ICC 推导，否则编码期会失败"});
    add({"J8",
         "jxl",
         "libjxl",
         Action::Warn,
         {"uses_original_profile"},
         {},
         [](const EvalCtx &c) {
             return c.has("uses_original_profile") && c.b("uses_original_profile", false) &&
                    !c.lossless;
         },
         {},
         "J8：有损编码下 uses_original_profile=true 可能显著变差（放弃 XYB 感知优化）"});
    add({"J9",
         "jxl",
         "libjxl",
         Action::Hide,
         {},
         {"modular_palette_colors", "modular_lossy_palette", "channel_colors_global_percent",
          "channel_colors_group_percent"},
         jxl_lossy_modular,
         {},
         "J9：有损 Modular 下 palette/channel_colors 组被库静默清零，隐藏"});
    add({"J9",
         "jxl",
         "libjxl",
         Action::Error,
         {"modular_ma_tree_learning_percent"},
         {},
         [](const EvalCtx &c) {
             return jxl_lossy_modular(c) && c.has("modular_ma_tree_learning_percent") &&
                    c.d("modular_ma_tree_learning_percent", -1.0) == 0.0;
         },
         {},
         "J9：有损 Modular 下 modular_ma_tree_learning_percent=0 会导致编码期硬失败（库不拦）"});
    add({"J10",
         "jxl",
         "libjxl",
         Action::Warn,
         {"color_transform"},
         {},
         [](const EvalCtx &c) { return c.s("color_transform", "auto") == "YCoCg"; },
         {},
         "J10：YCoCg 会强制 modular_color_space=6；库要求 modular_color_space>0 时"
         " color_transform≠XYB —— 单一枚举已结构性保证"});
    add({"J11",
         "jxl",
         "libjxl",
         Action::Warn,
         {"modular_predictor"},
         {},
         [](const EvalCtx &c) {
             if (!jxl_lossy_modular(c) || !c.has("modular_predictor"))
                 return false;
             const int64_t v = c.i("modular_predictor", -1);
             return v >= 3 && v <= 6;
         },
         {},
         "J11：有损 Modular 下 modular_predictor=3..6 会被库替换（可设但无效）"});
    add({"J12",
         "jxl",
         "libjxl",
         Action::Warn,
         {"output_mode"},
         {},
         [](const EvalCtx &c) { return c.has("output_mode") && c.i("output_mode", -1) == 2; },
         {},
         "J12：output_mode=2（乱序 jxlp）需要 ftyp v1；老解码器可能不兼容"});
    add({"J13",
         "jxl",
         "libjxl",
         Action::Warn,
         {"frame_name"},
         {},
         frame_name_present,
         {},
         "J13：设置 frame_name 会禁用 fast-lossless 优化（无损可能变慢）"});
    add({"J13",
         "jxl",
         "libjxl",
         Action::Error,
         {"frame_name"},
         {},
         frame_name_too_long,
         {},
         "J13：frame_name 最长 1071 字节（libjxl 限制）"});
    add({"J14",
         "jxl",
         "libjxl",
         Action::Error,
         {"intrinsic_size"},
         {},
         intrinsic_size_invalid,
         {},
         "J14：intrinsic_size 需为「宽x高」文本（两分量同时给出；0/空=不写；单边上限 2^30）"});
    add({"J15",
         "jxl",
         "libjxl",
         Action::Error,
         {"buffering"},
         {},
         [](const EvalCtx &c) { return c.has("buffering") && c.i("buffering", -1) == 3; },
         {},
         "J15：buffering=3 已弃用（0.12 起可选 0/1/2）"});
    add({"J16",
         "jxl",
         "libjxl",
         Action::Warn,
         {"noise"},
         {},
         [](const EvalCtx &c) { return c.b("noise", false); },
         {},
         "J16：noise 是旧式自适应噪声开关，已被 photon_noise 取代；建议优先用 photon_noise"});
    add({"J17",
         "jxl",
         "libjxl",
         Action::Warn,
         {"intensity_target", "min_nits", "relative_to_max_display", "linear_below"},
         {},
         [](const EvalCtx &c) {
             const int n = hdr_group_present(c);
             return n > 0 && n < 4;
         },
         {},
         "J17：HDR 色调映射组（intensity_target/min_nits/relative_to_max_display/"
         "linear_below）建议一起设置；非 HDR 工作流保持未设置/0"});
    add({"J18",
         "jxl",
         "libjxl",
         Action::Error,
         {"codestream_level", "uses_original_profile"},
         {},
         jxl_level5_16bit_conflict,
         {},
         "J18：codestream_level=5 与 16 位 uses_original_profile（无损/Modular）不兼容；"
         "需改为 level 10 或自动（-1）"});
    return r;
}

const std::vector<Rule> &rules() {
    static const std::vector<Rule> table = build_rules();
    return table;
}

bool applies_to(const Rule &rule, const EvalCtx &c) {
    if (!rule.format.empty() && rule.format != c.format)
        return false;
    if (!rule.backend.empty() && rule.backend != c.backend)
        return false;
    return !rule.when || rule.when(c);
}

void apply_rule(const Rule &rule, const EvalCtx &c, RuleState &st) {
    if (!applies_to(rule, c))
        return;
    switch (rule.action) {
    case Action::Hide:
        for (std::string_view key : rule.targets)
            st.hidden.insert(std::string(key));
        break;
    case Action::Disable:
        for (std::string_view key : rule.targets)
            st.disabled.insert(std::string(key));
        break;
    case Action::Lock:
        if (rule.lock) {
            const ParamValue value = rule.lock(c);
            for (std::string_view key : rule.targets)
                st.locked[std::string(key)] = value;
        }
        break;
    case Action::Warn:
        st.warnings.push_back(rule.message);
        break;
    case Action::Error:
        st.errors.push_back(rule.message);
        break;
    }
}

std::string join_keys(const std::vector<std::string_view> &keys) {
    std::string out;
    for (std::string_view k : keys) {
        if (!out.empty())
            out += ",";
        out += k;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// 对外接口（param_rules.h 冻结面）
// ---------------------------------------------------------------------------
RuleState evaluate(const ParamContext &ctx, const ParamSet &values) {
    RuleState st;
    EvalCtx c;
    c.values = &values;
    c.format = ctx.format_id;
    // backend 解析：显式 id → 该 id；空 = 静态首选（与 find_backend/encoders 的 resolve 同口径）。
    if (const FormatDef *f = find_format(ctx.format_id)) {
        const BackendDef *b = find_backend(*f, ctx.backend_id);
        c.backend = b ? b->id : ctx.backend_id;
    } else {
        c.backend = ctx.backend_id;
    }
    // 无损：ParamContext 是权威上下文；ParamSet 里的显式/内部键可补足（UI/预设两条入口）。
    c.lossless = ctx.lossless || lossless_flag(values);
    c.bitdepth = ctx.bitdepth;
    c.width = ctx.width;
    c.height = ctx.height;
    // JXL 技术：显式 tech_id 优先；空 = lossless / Modular-only 键推断（与 enc_jxl 口径一致）。
    c.tech = ctx.tech_id;
    if (c.format == "jxl") {
        c.modular = c.lossless || params_indicate_modular(values);
        if (ctx.tech_id == "modular")
            c.modular = true;
        else if (ctx.tech_id == "vardct" && !c.lossless)
            c.modular = params_indicate_modular(values);
    }
    for (const Rule &rule : rules())
        apply_rule(rule, c, st);
    return st;
}

std::vector<std::string> preflight(const ParamContext &ctx, const ParamSet &values) {
    return evaluate(ctx, values).errors;
}

// ---------------------------------------------------------------------------
// 诊断面（不放进冻结头 core/param_rules.h；tests/unit/test_param_rules.cpp 以相同
// extern 声明消费）。每行 = {id, format, backend, action, drivers, targets, message}：
//   * drivers/targets 为逗号分隔键名（锚点闭合测试逐键核对目录/既有表/引擎锚点）；
//   * 供「规则表每行都有格式/动作/文案」与「目录 rule_ids ↔ 规则表」机械校验。
// ---------------------------------------------------------------------------
std::vector<std::vector<std::string>> rule_table_dump() {
    std::vector<std::vector<std::string>> out;
    out.reserve(rules().size());
    for (const Rule &rule : rules()) {
        out.push_back({std::string(rule.id), std::string(rule.format), std::string(rule.backend),
                       std::string(action_name(rule.action)), join_keys(rule.drivers),
                       join_keys(rule.targets), rule.message});
    }
    return out;
}

} // namespace pp::rules
