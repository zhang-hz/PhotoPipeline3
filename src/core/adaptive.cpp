// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 自适应参数调整核心（M10 / W3-T3；契约 docs/v0.4.0-design.md §9「C24」）
//
// 本文件 = 行式 DSL v1 的解析/渲染 + AST + 目标键注册表/禁止表 + 出厂默认策略库。
// 纯 C++（零 Qt）、纯函数；执行器（候选文件/轮循环/同步评分）在 W3-T4 的 adaptive_exec.*。
//
// 设计逐条对应：
//   §9.2 EBNF      —— parse_adaptive_policy（# 注释、缩进无语义、行号+列号定位）
//   §9.2 幂等      —— render_adaptive_policy（固定选项顺序、两空格缩进、注释丢弃）
//   §9.3 AST       —— AdaptivePolicy/AdaptiveStage/AdaptiveAction（JSON AST 的 Qt 解析在
//                     src/ui/format_preset_io.*，导出仍为 DSL 文本）
//   §9.4 注册表    —— search 白名单（6 个数值质量键）+ 禁止表硬拒绝 + 数据驱动允许面
//   §9.7 默认库    —— default_adaptive_policies()（HEIF/AVIF/JXL/WebP/JPEG 各 1 条）
//   §9.8 正交性    —— 禁止表 + set 值按目录类型归一（执行器每轮会再走 validate_params/preflight）
//
// 错误面：稳定英文 code（测试/日志）+ 中文 message/hint（UI 直接展示）。code 全表见
// adaptive_error_codes()；每个 code 在测试里至少被一条非法样例钉住。
#include "core/adaptive.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codecs/heif_profile.h" // profile 伪参数：档位名↔序号 / AVIF-HEIF 信号门控

namespace pp {
namespace {

// ---------------------------------------------------------------------------
// 错误构造
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：line/col 由错误定位语义固定
bool fail(AdaptiveError *err, int line, int col, std::string_view code, std::string message,
          std::string hint = {}) {
    if (err != nullptr) {
        err->line = line;
        err->col = col;
        err->code.assign(code);
        err->message = std::move(message);
        err->hint = std::move(hint);
    }
    return false;
}

bool ok(AdaptiveError *err) {
    if (err != nullptr) {
        err->line = 0;
        err->col = 0;
        err->code.clear();
        err->message.clear();
        err->hint.clear();
    }
    return true;
}

// ---------------------------------------------------------------------------
// 词法：去注释 + 分词（行列定位）
// ---------------------------------------------------------------------------
struct Token {
    bool quoted = false; // "字符串" 字面量
    std::string text;    // 未转义内容
    int col = 1;         // 1-based 列号
};

// 引号感知地截断行尾注释（`#` 到行尾；字符串内的 # 保留）。
void strip_comment(std::string &body) {
    bool in_string = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (in_string) {
            if (c == '\\') {
                ++i; // 跳过被转义的字符（含 \" 与 \\）
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '#') {
            body.resize(i);
            return;
        }
    }
}

bool tokenize(std::string_view raw, int line, std::vector<Token> &out, AdaptiveError *err) {
    std::string body(raw);
    if (!body.empty() && body.back() == '\r')
        body.pop_back();
    strip_comment(body);
    std::size_t i = 0;
    while (i < body.size()) {
        const auto c = static_cast<unsigned char>(body[i]);
        if (c == ' ' || c == '\t') {
            ++i;
            continue;
        }
        Token tok;
        tok.col = static_cast<int>(i) + 1;
        if (body[i] == '"') {
            tok.quoted = true;
            const int start_col = tok.col;
            ++i;
            bool closed = false;
            while (i < body.size()) {
                const char ch = body[i];
                if (ch == '"') {
                    ++i;
                    closed = true;
                    break;
                }
                if (ch == '\\') {
                    if (i + 1 >= body.size())
                        return fail(err, line, static_cast<int>(i) + 1, "bad_escape",
                                    "字符串以未完成的转义序列结尾",
                                    "可用转义：\\\\ \\\" \\n \\t \\r；不支持其它转义。");
                    const char e = body[i + 1];
                    switch (e) {
                    case '\\':
                        tok.text.push_back('\\');
                        break;
                    case '"':
                        tok.text.push_back('"');
                        break;
                    case 'n':
                        tok.text.push_back('\n');
                        break;
                    case 't':
                        tok.text.push_back('\t');
                        break;
                    case 'r':
                        tok.text.push_back('\r');
                        break;
                    default:
                        return fail(err, line, static_cast<int>(i) + 1, "bad_escape",
                                    std::string("字符串里的转义序列「\\") + e + "」不受支持",
                                    "可用转义：\\\\ \\\" \\n \\t \\r。");
                    }
                    i += 2;
                    continue;
                }
                tok.text.push_back(ch);
                ++i;
            }
            if (!closed)
                return fail(err, line, start_col, "unterminated_string", "字符串缺少右引号",
                            "字符串必须写在同一行并以 \" 收尾（跨行字符串不支持）。");
        } else {
            const std::size_t start = i;
            while (i < body.size() && body[i] != ' ' && body[i] != '\t')
                ++i;
            tok.text = body.substr(start, i - start);
        }
        out.push_back(std::move(tok));
    }
    return true;
}

// ---------------------------------------------------------------------------
// 字面量解析
// ---------------------------------------------------------------------------
bool parse_int64(std::string_view s, int64_t &out) {
    if (s.empty())
        return false;
    int64_t v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size())
        return false;
    out = v;
    return true;
}

bool parse_double(std::string_view s, double &out) {
    if (s.empty())
        return false;
    double v = 0.0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v, std::chars_format::general);
    if (r.ec != std::errc() || r.ptr != s.data() + s.size() || !std::isfinite(v))
        return false;
    out = v;
    return true;
}

// 数字渲染（最短往返；整数不带 ".0"，与 DSL 示例一致）。
std::string render_number(double v) {
    if (!std::isfinite(v))
        return "0";
    if (v == std::floor(v) && std::abs(v) < 9007199254740992.0) {
        const auto i = static_cast<int64_t>(v);
        // -0 → 0
        return std::to_string(i == 0 ? 0 : i);
    }
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general);
    if (r.ec != std::errc())
        return "0";
    return std::string(buf, r.ptr);
}

// 裸标识符安全集（渲染用）：首字符字母/下划线，其余 [A-Za-z0-9_.:-]；且不能与关键字冲突。
bool safe_bare_word(std::string_view s) {
    if (s.empty())
        return false;
    const auto first = static_cast<unsigned char>(s[0]);
    if (!(std::isalpha(first) != 0 || s[0] == '_'))
        return false;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) != 0 || c == '_' || c == '.' || c == ':' || c == '-'))
            return false;
    }
    return s != "true" && s != "false" && s != "current";
}

std::string quote_string(std::string_view s) {
    std::string out;
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string render_value(const ParamValue &v) {
    if (const bool *b = std::get_if<bool>(&v))
        return *b ? "true" : "false";
    if (const int64_t *i = std::get_if<int64_t>(&v))
        return std::to_string(*i);
    if (const double *d = std::get_if<double>(&v))
        return render_number(*d);
    if (const std::string *s = std::get_if<std::string>(&v))
        return safe_bare_word(*s) ? *s : quote_string(*s);
    return "\"\""; // monostate（非法 AST 的防御性渲染）
}

// 布尔/数值/裸词 → ParamValue（set 的右值）。
ParamValue literal_value(const Token &tok) {
    if (tok.quoted)
        return ParamValue{tok.text};
    if (tok.text == "true")
        return ParamValue{true};
    if (tok.text == "false")
        return ParamValue{false};
    int64_t iv = 0;
    if (parse_int64(tok.text, iv))
        return ParamValue{iv};
    double dv = 0.0;
    if (parse_double(tok.text, dv))
        return ParamValue{dv};
    return ParamValue{tok.text};
}

// ---------------------------------------------------------------------------
// stage IDENT：[a-z][a-z0-9_-]{0,31}
// ---------------------------------------------------------------------------
bool valid_stage_id(std::string_view s) {
    if (s.empty() || s.size() > 32)
        return false;
    if (!(s[0] >= 'a' && s[0] <= 'z'))
        return false;
    for (const char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 关键字表
// ---------------------------------------------------------------------------
bool is_option_keyword(std::string_view s) {
    return s == "version" || s == "target" || s == "max_rounds" || s == "output" ||
           s == "on_metric_unavailable" || s == "plateau";
}

bool method_from_name(std::string_view s, AdaptiveMethod &out) {
    if (s == "linear") {
        out = AdaptiveMethod::Linear;
        return true;
    }
    if (s == "bisect") {
        out = AdaptiveMethod::Bisect;
        return true;
    }
    if (s == "exp") {
        out = AdaptiveMethod::Exp;
        return true;
    }
    if (s == "greedy") {
        out = AdaptiveMethod::Greedy;
        return true;
    }
    return false;
}

constexpr std::string_view kSearchUsage =
    "用法：search <键> from current|<数值> [down|up] to <数值> step <数值> "
    "[breakpoint <数值> step <数值>]... [method linear|bisect|exp|greedy] [max_rounds N]";

// ---------------------------------------------------------------------------
// §9.4 目标键注册表
// ---------------------------------------------------------------------------
// search 白名单 = 设计 §9.4「数值质量参数」六个键（只允许这些键参与搜索：
// 搜索语义要求候选是单调数值；枚举/布尔/文本只能 set）。
const std::vector<AdaptiveKeyRule> &key_rules_impl() {
    static const std::vector<AdaptiveKeyRule> rules = {
        {"quality", true, true, {"heif", "avif", "webp", "jpeg"}},
        {"distance", true, true, {"jxl", "jpeg"}},
        {"x265:crf", true, true, {"heif"}},
        {"x265:qp", true, true, {"heif"}},
        {"aom:cq-level", true, true, {"avif"}},
        {"qp", true, true, {"avif"}},
        // 枚举/布尔/文本（§9.4 第二类）：只能 set，不能 search。
        {"chroma", false, true, {"heif", "avif", "jpeg"}},
        {"preset", false, true, {"heif", "avif"}},
        {"tune", false, true, {"heif", "avif"}},
        {"epf", false, true, {"jxl"}},
        {"gaborish", false, true, {"jxl"}},
        {"dots", false, true, {"jxl"}},
        {"patches", false, true, {"jxl"}},
        {"modular_predictor", false, true, {"jxl"}},
        {"modular_group_size", false, true, {"jxl"}},
        {"color_transform", false, true, {"jxl"}},
        // 伪参数：profile（HEIF/AVIF 档位名或编号）；tech 默认禁止（§9.4 第三类），
        // 不在本表内 —— 校验时给专门的 tech_not_supported 错误。
        {"profile", false, true, {"heif", "avif"}},
    };
    return rules;
}

// §9.4 禁止表（硬拒绝，与格式无关）：任何一项出现在 search/set 目标键上即 fail。
const std::vector<std::string_view> &forbidden_keys_impl() {
    static const std::vector<std::string_view> keys = {
        // 无损 / 重采样 / HDR / 评分口径类 footgun（改评分参考维度 ⇒ 破坏正交性）
        "lossless",
        "resampling",
        "upsampling_mode",
        "intensity_target",
        "min_nits",
        "relative_to_max_display",
        "linear_below",
        "keep_invisible",
        "noise",
        "photon_noise",
        "disable_perceptual_heuristics",
        "expert_options",
        "codestream_level",
        "buffering",
        "output_mode",
        "frame_name",
        "intrinsic_size",
        // 位深（格式级结构键；由格式预设的 bitdepth 字段承载）
        "bitdepth",
        "out_bitdepth",
        "bit_depth",
        // 全局项（属全局预设职责；不得由自适应改写）
        "color_target",
        "color_intent",
        "color_dither8",
        "gamut_mapping",
        "linear_resample",
        "metadata_only",
        "conflict",
        "rules",
        "output_template",
        "split_by_format",
        "outputs",
        // 文件/预设结构字段
        "kind",
        "version",
        "name",
        "format",
        "backend",
        "reserved",
        "adaptive",
        "policy",
        "params",
    };
    return keys;
}

bool is_forbidden_key(std::string_view key) {
    if (key.rfind("__", 0) == 0)
        return true; // `__` 保留键一律硬拒绝（profile 用伪键 "profile" 表达）
    const auto &keys = forbidden_keys_impl();
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool format_matches(const AdaptiveKeyRule &r, std::string_view format_id) {
    if (r.formats.empty())
        return true;
    return std::find(r.formats.begin(), r.formats.end(), format_id) != r.formats.end();
}

const AdaptiveKeyRule *find_key_rule(std::string_view key) {
    for (const AdaptiveKeyRule &r : key_rules_impl())
        if (r.key == key)
            return &r;
    return nullptr;
}

bool is_heif_like(std::string_view format_id) { return format_id == "heif" || format_id == "avif"; }

// libheif 插件透传键（`x265:` / `aom:` 前缀；与 core/params.cpp 的未知参数口径一致）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：format_id/key 由参数键语义固定
bool is_plugin_passthrough(std::string_view format_id, std::string_view key) {
    if (!is_heif_like(format_id))
        return false;
    return key.rfind("x265:", 0) == 0 || key.rfind("aom:", 0) == 0;
}

// ---------------------------------------------------------------------------
// 格式参数键索引（该格式全部后端×技术的并集；libheif 系追加运行时内省）
// ---------------------------------------------------------------------------
// 说明：MSVC STL 的 std::map 移动构造被标注 noexcept，其内部 _Throw_bad_array_new_length
// （std::bad_alloc 的派生类）未被 clang-tidy 的 ignoreBadAlloc 覆盖 ⇒ 对隐式 move ctor 的误报。
// NOLINTNEXTLINE(bugprone-exception-escape)：MSVC STL map 移动构造 bad_array_new_length 误报
struct FormatKeyIndex {
    bool determinable = false; // false = 该格式参数面不可判定（内省不可用）→ 放行未知键
    std::map<std::string, ParamDef> defs;
};

FormatKeyIndex build_format_key_index(std::string_view format_id) {
    FormatKeyIndex idx;
    const FormatDef *f = find_format(format_id);
    if (f == nullptr)
        return idx;
    bool needs_introspection = false;
    for (const BackendDef &b : f->backends) {
        if (b.runtime_introspected && b.techs.empty())
            needs_introspection = true;
        for (const TechDef &t : b.techs)
            for (const ParamDef &p : t.params)
                idx.defs.emplace(p.key, p); // first-wins（与 lookup_param_def 口径一致）
    }
    if (needs_introspection) {
        // 运行时内省第一来源；不可用（无插件）时保持"不可判定"，不误报未知键。
        for (const BackendDef &lb : introspect_backends(format_id))
            for (const TechDef &t : lb.techs)
                for (const ParamDef &p : t.params)
                    idx.defs.emplace(p.key, p);
    }
    idx.determinable = !idx.defs.empty();
    return idx;
}

// 进程内缓存：内省会实例化 libheif 编码器，代价远高于一次 map 查找；结果对同一进程稳定。
const FormatKeyIndex &format_key_index(std::string_view format_id) {
    static std::mutex m;
    static std::map<std::string, FormatKeyIndex> cache;
    const std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(std::string(format_id));
    if (it == cache.end())
        it = cache.emplace(std::string(format_id), build_format_key_index(format_id)).first;
    return it->second; // std::map 节点地址稳定；插入后不再改写 → 并发只读安全
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：format_id/key 由参数键语义固定
const ParamDef *find_format_param(std::string_view format_id, std::string_view key) {
    if (format_id.empty())
        return nullptr;
    const FormatKeyIndex &idx = format_key_index(format_id);
    const auto it = idx.defs.find(std::string(key));
    return it == idx.defs.end() ? nullptr : &it->second;
}

// 数值搜索的兜底范围（目录参数不可得时；设计 §9.4 的六个键）。
struct NumericRange {
    double lo = 0.0;
    double hi = 100.0;
};
NumericRange search_fallback_range(std::string_view key) {
    if (key == "quality")
        return {0.0, 100.0};
    if (key == "distance")
        return {0.0, 25.0};
    if (key == "x265:crf" || key == "x265:qp")
        return {0.0, 51.0};
    if (key == "aom:cq-level")
        return {0.0, 63.0};
    if (key == "qp")
        return {0.0, 63.0};
    return {0.0, 100.0};
}

// ---------------------------------------------------------------------------
// profile 伪参数：档位名表（与 codecs/heif_profile.h 的枚举名逐字一致）
// ---------------------------------------------------------------------------
struct ProfileNameEntry {
    std::string_view name;
    int64_t value;
};
constexpr ProfileNameEntry kProfileNames[] = {
    {"Auto", 0},
    {"Main", 1},
    {"Main10", 2},
    {"Main12", 3},
    {"MainStill", 4},
    {"High", 5},
    {"Professional", 6},
    {"Rext420_8", 7},
    {"Rext420_10", 8},
    {"Rext420_12", 9},
    {"Rext422_8", 10},
    {"Rext422_10", 11},
    {"Rext422_12", 12},
    {"Rext444_8", 13},
    {"Rext444_10", 14},
    {"Rext444_12", 15},
    {"Main10Still", 16},
    {"Main422_10Still", 17},
    {"Main422_12Still", 18},
    {"Main444_Still", 19},
    {"Main444_10Still", 20},
    {"Main444_12Still", 21},
};

char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equals_ignore_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(a[i]) != ascii_lower(b[i]))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// 枚举值匹配（中文显示名 / 引擎值均可；int 型枚举接受数字与「…（N）」标签）
// ---------------------------------------------------------------------------
bool choice_matches(std::string_view text, const std::pair<std::string, ParamValue> &choice) {
    if (text == choice.first)
        return true;
    if (const std::string *s = std::get_if<std::string>(&choice.second))
        return text == *s;
    if (const int64_t *i = std::get_if<int64_t>(&choice.second)) {
        int64_t parsed = 0;
        if (parse_int64(text, parsed))
            return parsed == *i;
        // 标签形式「关闭（0）」/「自动方差（2）」：取尾部的十进制数字再比较。
        const std::size_t open = text.find_last_of("（(");
        if (open != std::string_view::npos) {
            std::string_view tail = text.substr(open + 1);
            // 右括号可能是全角 '）'（UTF-8 三字节 EF BC 89）或 ASCII ')'：两者都从尾部剥掉。
            // 不能用窄字符字面量 '）'（3 字节 > char，clang 报 "character too large for enclosing
            // character literal type"）；此处按 UTF-8 字节序列匹配。
            constexpr std::string_view kFullWidthRightParen = "\xEF\xBC\x89"; // '）'
            while (!tail.empty()) {
                if (tail.back() == ')') {
                    tail.remove_suffix(1);
                } else if (tail.ends_with(kFullWidthRightParen)) {
                    tail.remove_suffix(kFullWidthRightParen.size());
                } else {
                    break;
                }
            }
            if (parse_int64(tail, parsed))
                return parsed == *i;
        }
        return false;
    }
    if (const bool *b = std::get_if<bool>(&choice.second)) {
        if (text == "true")
            return *b;
        if (text == "false")
            return !*b;
        return false;
    }
    return false;
}

std::string join_choices(const ParamDef &def, std::size_t max_items = 12) {
    std::string out;
    for (std::size_t i = 0; i < def.choices.size() && i < max_items; ++i) {
        if (!out.empty())
            out += " / ";
        out += def.choices[i].first;
    }
    if (def.choices.size() > max_items)
        out += " / …";
    return out;
}

std::string value_type_name(const ParamValue &v) {
    switch (v.index()) {
    case 1:
        return "布尔值";
    case 2:
        return "整数";
    case 3:
        return "数值";
    case 4:
        return "字符串";
    default:
        return "空值";
    }
}

// ---------------------------------------------------------------------------
// 校验辅助：单键
// ---------------------------------------------------------------------------
bool validate_forbidden(std::string_view key, int line, int col, AdaptiveError *err) {
    if (key == "tech")
        return fail(err, line, col, "tech_not_supported", "自适应策略不得修改「tech」（编码技术）",
                    "切换技术会改变评分参考的语义；tech 属 P2 能力，本版默认禁止。请改用 profile/"
                    "chroma 等参数。");
    if (is_forbidden_key(key)) {
        std::string why;
        if (key.rfind("__", 0) == 0)
            why = "`__` 前缀是保留键（内部管道键），不得由策略改写（profile 用伪键 \"profile\"）。";
        else if (key == "lossless" || key == "resampling" || key == "upsampling_mode")
            why = "该键会改变评分参考分辨率/无损口径，破坏自适应优化的正交性。";
        else if (key == "intensity_target" || key == "min_nits" ||
                 key == "relative_to_max_display" || key == "linear_below" ||
                 key == "keep_invisible" || key == "noise" || key == "photon_noise" ||
                 key == "disable_perceptual_heuristics")
            why = "该键属 HDR/感知启发式 footgun，会改变评分参考或产物语义。";
        else if (key == "bitdepth" || key == "out_bitdepth" || key == "bit_depth")
            why = "位深是格式级结构键（随格式预设的 bitdepth 字段），不得在迭代中修改。";
        else if (key == "expert_options" || key == "codestream_level" || key == "buffering" ||
                 key == "output_mode" || key == "frame_name" || key == "intrinsic_size")
            why = "该键属容器/码流语义（会改变产物结构或评分参考），禁止作为自适应目标。";
        else
            why = "该键属全局项（color/conflict/template/rules/metadata_only 等），"
                  "不属于格式级参数快照。";
        return fail(err, line, col, "forbidden_key", "禁止作为自适应目标键：" + std::string(key),
                    why);
    }
    return true;
}

bool validate_set_value(const ParamValue &value, const ParamDef &def, int line, int col,
                        AdaptiveError *err) {
    ParamValue coerced;
    std::string why;
    if (!adaptive_coerce_set_value(value, def, coerced, &why)) {
        if (def.type == ParamType::Enum) {
            return fail(err, line, col, "enum_value", "参数「" + def.key + "」的取值不合法：" + why,
                        "可选值：" + join_choices(def) + "（也可写引擎值/数字编号）。");
        }
        return fail(err, line, col, "set_value_type", "参数「" + def.key + "」的值类型不符：" + why,
                    "按该参数的目录类型填写（整数/浮点/布尔/字符串）。");
    }
    if (def.type == ParamType::Int || def.type == ParamType::Float) {
        double x = 0.0;
        if (const int64_t *i = std::get_if<int64_t>(&coerced))
            x = static_cast<double>(*i);
        else if (const double *d = std::get_if<double>(&coerced))
            x = *d;
        else
            return true;
        if (x < def.lo || x > def.hi) {
            return fail(err, line, col, "set_value_range",
                        "参数「" + def.key + "」的取值 " + render_number(x) + " 超出允许范围 [" +
                            render_number(def.lo) + ", " + render_number(def.hi) + "]",
                        "请改到范围内；动态域（如 10/12 位 CRF 负值）由每轮 preflight 复核。");
        }
    }
    return true;
}

bool validate_search_action(const AdaptiveAction &a, std::string_view format_id, int line, int col,
                            AdaptiveError *err) {
    const AdaptiveKeyRule *rule = find_key_rule(a.key);
    if (rule == nullptr) {
        const ParamDef *def = find_format_param(format_id, a.key);
        if (def != nullptr)
            return fail(err, line, col, "not_searchable",
                        "参数「" + a.key + "」不可搜索（只有数值质量参数支持 search）",
                        "可搜索的键：quality / distance / x265:crf / x265:qp / aom:cq-level / qp；"
                        "枚举/布尔/文本请改用 set。");
        return fail(err, line, col, "unknown_key", "未知的自适应目标键：" + a.key,
                    "search 只接受数值质量参数（quality/distance/x265:crf/x265:qp/"
                    "aom:cq-level/qp）。");
    }
    if (!format_id.empty() && !format_matches(*rule, format_id)) {
        std::string fmts;
        for (const std::string_view f : rule->formats)
            fmts += (fmts.empty() ? "" : "/") + std::string(f);
        return fail(err, line, col, "key_not_in_format",
                    "参数「" + a.key + "」不适用于格式 " + std::string(format_id),
                    "该键只适用于：" + fmts + "。");
    }
    if (!rule->searchable) {
        return fail(err, line, col, "not_searchable",
                    "参数「" + a.key + "」是枚举/布尔/文本键，不能搜索（不可搜索）",
                    "请改用 set；search 只接受数值质量参数。");
    }
    if (a.step <= 0.0 || !std::isfinite(a.step))
        return fail(err, line, col, "step_range", "search 的 step 必须大于 0",
                    "step 是候选步长（正数）；方向由 from→to 的端点序决定。");
    double lo = 0.0, hi = 0.0;
    if (const ParamDef *def = find_format_param(format_id, a.key)) {
        if (def->type == ParamType::Int || def->type == ParamType::Float) {
            lo = def->lo;
            hi = def->hi;
        } else {
            const NumericRange r = search_fallback_range(a.key);
            lo = r.lo;
            hi = r.hi;
        }
    } else {
        const NumericRange r = search_fallback_range(a.key);
        lo = r.lo;
        hi = r.hi;
    }
    auto in_range = [lo, hi](double x) { return x >= lo && x <= hi; };
    if (!a.from.current && !in_range(a.from.number)) {
        return fail(err, line, col, "search_range",
                    "search 的起点 " + render_number(a.from.number) + " 超出参数「" + a.key +
                        "」范围 [" + render_number(lo) + ", " + render_number(hi) + "]",
                    "把 from 改到范围内，或写 `from current` 让策略从运行配置出发。");
    }
    if (!in_range(a.to)) {
        return fail(err, line, col, "search_range",
                    "search 的终点 " + render_number(a.to) + " 超出参数「" + a.key + "」范围 [" +
                        render_number(lo) + ", " + render_number(hi) + "]",
                    "把 to 改到范围内（终点含端点）。");
    }
    if (!a.from.current && a.from.number == a.to && a.direction == AdaptiveDirection::Auto) {
        return fail(err, line, col, "search_noop", "search 的 from 与 to 相同（区间为空）",
                    "把 from/to 写成不同值；若想从运行配置出发请写 `from current`。"
                    "（显式 down/up 时端点相等 = 该动作 0 轮，不是错误。）");
    }
    if (a.max_rounds != 0 && (a.max_rounds < 1 || a.max_rounds > 32)) {
        return fail(err, line, col, "action_max_rounds",
                    "search 的 max_rounds 必须在 1..32（实际 " + std::to_string(a.max_rounds) +
                        "）",
                    "省略该子句表示继承全局 max_rounds。");
    }
    // —— DSL v1.1：步长断点（需求 7）——
    if (!a.breakpoints.empty()) {
        if (a.direction == AdaptiveDirection::Auto)
            return fail(err, line, col, "breakpoint_syntax",
                        "breakpoint 必须与显式方向一起使用（`from … down|up to …`）",
                        "断点的分段步长依赖确定方向；请把 search 写成 "
                        "`search <键> from current down to <终点> step <步长> "
                        "breakpoint <值> step <步长>`（或 up）。");
        if (a.method != AdaptiveMethod::Linear)
            return fail(err, line, col, "breakpoint_syntax",
                        "breakpoint 只支持 method linear（当前 method = " +
                            std::string(adaptive_method_name(a.method)) + "）",
                        "bisect/exp/greedy 没有「按段步长排程」语义；请删除 breakpoint 或改用 "
                        "method linear。");
        // 1) 严格升序（渲染/执行都依赖升序分段）。
        for (std::size_t k = 1; k < a.breakpoints.size(); ++k) {
            if (!(a.breakpoints[k].value > a.breakpoints[k - 1].value))
                return fail(err, line, col, "breakpoint_order",
                            "breakpoint 必须按数值严格升序（第 " + std::to_string(k + 1) +
                                " 个断点 " + render_number(a.breakpoints[k].value) +
                                " 不大于前一个 " + render_number(a.breakpoints[k - 1].value) + "）",
                            "断点按「开区间上界」分段：越靠后的断点对应越高的值区间；"
                            "请重排/去重。");
        }
        const bool down = a.direction == AdaptiveDirection::Down;
        for (std::size_t k = 0; k < a.breakpoints.size(); ++k) {
            const AdaptiveBreakpoint &bp = a.breakpoints[k];
            const std::string at = "第 " + std::to_string(k + 1) + " 个断点";
            if (!(bp.step > 0.0) || !std::isfinite(bp.step))
                return fail(err, line, col, "breakpoint_value",
                            at + "的 step 必须大于 0（实际 " + render_number(bp.step) + "）",
                            "断点 step 是该段的候选步长；正数，方向由 down/up 决定。");
            if (!std::isfinite(bp.value) || !in_range(bp.value))
                return fail(err, line, col, "breakpoint_value",
                            at + "（值 " + render_number(bp.value) + "）超出参数「" + a.key +
                                "」范围 [" + render_number(lo) + ", " + render_number(hi) + "]",
                            "断点是参数值域内的分段上界；请改到范围内。");
            // 2) 与 to 同侧：down 必须 > to（否则永远到不了，属配置错误）；up 必须 < to。
            if (down ? !(bp.value > a.to) : !(bp.value < a.to))
                return fail(
                    err, line, col, "breakpoint_value",
                    at + "（值 " + render_number(bp.value) + "）不在搜索区间内：" +
                        (down ? "down 方向要求断点 > 终点 to=" : "up 方向要求断点 < 终点 to=") +
                        render_number(a.to),
                    "断点应落在 (to, 起点] 区间内（down）/ [起点, to) 区间内（up）。");
            // 3) 显式起点时断点还必须落在起点内侧（否则该断点永不被跨过）。
            if (!a.from.current) {
                const bool inside = down ? bp.value < a.from.number : bp.value > a.from.number;
                if (!inside)
                    return fail(err, line, col, "breakpoint_value",
                                at + "（值 " + render_number(bp.value) + "）不在搜索区间内：" +
                                    (down ? "down 方向要求断点 < 起点 from="
                                          : "up 方向要求断点 > 起点 from=") +
                                    render_number(a.from.number),
                                "断点应严格位于起点与终点之间（起点=current 时不校验这一条）。");
            }
        }
    }
    return true;
}

bool validate_set_action(const AdaptiveAction &a, std::string_view format_id, int line, int col,
                         AdaptiveError *err) {
    if (a.key == "profile") {
        if (format_id.empty())
            return true; // 与格式无关的校验（JSON AST 结构检查）不判 profile 作用域
        if (!is_heif_like(format_id)) {
            return fail(err, line, col, "profile_key_format",
                        "profile 只适用于 HEIF/AVIF 输出（当前格式：" + std::string(format_id) +
                            "）",
                        "非 HEIF/AVIF 格式请删除该 set 动作。");
        }
        int64_t prof = 0;
        if (!adaptive_profile_from_value(a.value, prof)) {
            return fail(err, line, col, "profile_value",
                        "无法识别的编码档位值（" + value_type_name(a.value) + "）",
                        "可写档位名（如 Main444_10Still / Main / Rext444_10）或 0..21 的编号。");
        }
        HeifProfile hp = HeifProfile::Auto;
        if (!heif_profile_from_int(prof, &hp))
            return fail(err, line, col, "profile_value",
                        "编码档位编号 " + std::to_string(prof) + " 越界（合法 0..21）",
                        "见 params 目录的档位行或 docs/param-catalog.md。");
        if (format_id == "avif") {
            if (hp != HeifProfile::Auto && !heif_profile_is_avif_signal(hp)) {
                return fail(err, line, col, "profile_value",
                            "档位「" + heif_profile_label(hp) +
                                "」是 HEIF/HEVC 的静图信号，"
                                "不能用于 AVIF",
                            "AVIF 可写：Auto / Main / High / Professional（编号 0/1/5/6）。");
            }
        } else {
            if (hp != HeifProfile::Auto && !heif_profile_is_hevc_signal(hp)) {
                return fail(err, line, col, "profile_value",
                            "档位「" + heif_profile_label(hp) + "」是 AVIF 专用档位，不能用于 HEIF",
                            "HEIF 请写 HEVC 真实信号档位（Main/Main10/Rext*/*Still）或 Auto。");
            }
            std::string why;
            if (!heif_profile_still_available(hp, &why))
                return fail(err, line, col, "profile_unavailable",
                            "档位「" + heif_profile_label(hp) + "」在本编码后端不可用", why);
        }
        return true;
    }

    if (!validate_forbidden(a.key, line, col, err))
        return false;

    const ParamDef *def = find_format_param(format_id, a.key);
    const bool passthrough = is_plugin_passthrough(format_id, a.key);
    const AdaptiveKeyRule *rule = find_key_rule(a.key);
    bool allowed = passthrough || def != nullptr;
    if (!allowed && rule != nullptr && rule->settable) {
        if (format_id.empty() || format_matches(*rule, format_id))
            allowed = true;
        else
            return fail(
                err, line, col, "key_not_in_format",
                "参数「" + a.key + "」不适用于格式 " + std::string(format_id),
                "该键只适用于：" +
                    [&rule] {
                        std::string fmts;
                        for (const std::string_view f : rule->formats)
                            fmts += (fmts.empty() ? "" : "/") + std::string(f);
                        return fmts;
                    }() +
                    "。");
    }
    if (!allowed) {
        if (!format_id.empty() && format_key_index(format_id).determinable) {
            return fail(err, line, col, "unknown_key",
                        "未知的自适应目标键：" + a.key + "（格式 " + std::string(format_id) +
                            " 的参数表中没有该键）",
                        "只能 set 该格式参数目录中可设的键（用 profile/chroma 等伪参数时按格式"
                        "目录写）；枚举/布尔/文本与数值质量键均可 set。");
        }
        // 参数面不可判定（如无 libheif 插件的构建）：只做结构校验，不误报未知键。
        if (std::holds_alternative<std::monostate>(a.value))
            return fail(err, line, col, "set_value_type", "set 缺少取值",
                        "用法：set <键> <值>（数值/true/false/枚举名/\"字符串\"）。");
        return true;
    }
    if (def != nullptr)
        return validate_set_value(a.value, *def, line, col, err);
    // 透传键/显式白名单键但没有目录定义（插件内省不可用）：只要求值是标量。
    if (std::holds_alternative<std::monostate>(a.value))
        return fail(err, line, col, "set_value_type", "set 缺少取值",
                    "用法：set <键> <值>（数值/true/false/枚举名/\"字符串\"）。");
    return true;
}

} // namespace

// ===========================================================================
// §9.2 解析
// ===========================================================================
bool parse_adaptive_policy(std::string_view text, AdaptivePolicy &out, AdaptiveError *err) {
    if (text.size() > kAdaptivePolicyMaxBytes)
        return fail(err, 1, 1, "policy_too_large",
                    "策略文本超过 16 KiB 上限（实际 " + std::to_string(text.size()) + " 字节）",
                    "策略随预设文件持久化，过长的策略不可维护；请拆分/精简注释与阶段。");

    AdaptivePolicy p;
    bool any_content = false;
    bool seen_stage = false;
    std::set<std::string> seen_options;
    std::set<std::string> seen_stages;

    std::size_t pos = 0;
    int line_no = 0;
    while (pos <= text.size()) {
        ++line_no;
        const std::size_t nl = text.find('\n', pos);
        const std::string_view raw =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        std::vector<Token> toks;
        if (!tokenize(raw, line_no, toks, err))
            return false;
        if (!toks.empty()) {
            any_content = true;
            const Token &first = toks[0];
            if (first.quoted)
                return fail(err, line_no, first.col, "unknown_statement", "语句不能以字符串开头",
                            "可用的语句：version/target/max_rounds/output/on_metric_unavailable/"
                            "plateau/stage/search/set。");
            const std::string &kw = first.text;
            if (is_option_keyword(kw)) {
                if (seen_stage)
                    return fail(err, line_no, first.col, "option_after_stage",
                                "选项「" + kw + "」必须写在所有 stage 之前",
                                "策略结构 = { 选项 } { stage }：把该行移到第一个 stage 之前。");
                if (!seen_options.insert(kw).second)
                    return fail(err, line_no, first.col, "duplicate_option",
                                "选项「" + kw + "」重复出现",
                                "每个全局选项只写一次（省略即用默认值）。");
                if (toks.size() < 2)
                    return fail(err, line_no, first.col, "missing_value",
                                "选项「" + kw + "」缺少取值",
                                "写法：version 1 / target 90 / max_rounds 8 / output last / "
                                "on_metric_unavailable stop / plateau 0.05。");
                const Token &val = toks[1];
                if (toks.size() > 2)
                    return fail(err, line_no, toks[2].col, "stray_token",
                                "选项「" + kw + "」后有多余标记「" + toks[2].text + "」",
                                "每个选项只接受一个取值。");
                if (kw == "version") {
                    int64_t v = 0;
                    if (val.quoted || !parse_int64(val.text, v))
                        return fail(err, line_no, val.col, "invalid_option_value",
                                    "version 必须是整数", "本版只支持 version 1。");
                    if (v != kAdaptivePolicyVersion)
                        return fail(
                            err, line_no, val.col,
                            v > kAdaptivePolicyVersion ? "unsupported_version"
                                                       : "invalid_option_value",
                            "不支持的自适应策略版本 " + std::to_string(v) + "（本版只支持 1）",
                            v > kAdaptivePolicyVersion
                                ? "策略版本向后不兼容；请升级 PhotoPipeline 或按 v1 语法手写。"
                                : "version 必须是 1。");
                    p.version = static_cast<int>(v);
                } else if (kw == "target") {
                    double v = 0.0;
                    if (val.quoted || !parse_double(val.text, v))
                        return fail(err, line_no, val.col, "invalid_option_value",
                                    "target 必须是数值", "target 取值范围 (0, 100]，默认 90。");
                    if (!(v > 0.0 && v <= 100.0))
                        return fail(err, line_no, val.col, "target_range",
                                    "target 必须在 (0, 100]（实际 " + render_number(v) + "）",
                                    "推荐 90 = SSIMULACRA 2 的视觉透明档。");
                    p.target = v;
                } else if (kw == "max_rounds") {
                    int64_t v = 0;
                    if (val.quoted || !parse_int64(val.text, v))
                        return fail(err, line_no, val.col, "invalid_option_value",
                                    "max_rounds 必须是整数", "max_rounds 取值 1..32，默认 8。");
                    if (v < 1 || v > 32)
                        return fail(err, line_no, val.col, "max_rounds_range",
                                    "max_rounds 必须在 1..32（实际 " + std::to_string(v) + "）",
                                    "默认 8；三层轮数上界保证终止（§9.5）。");
                    p.max_rounds = static_cast<int>(v);
                } else if (kw == "output") {
                    if (val.quoted || (val.text != "last" && val.text != "best"))
                        return fail(err, line_no, val.col, "invalid_option_value",
                                    "output 只能是 last 或 best",
                                    "本版默认 output last（未达标输出最后一轮）。");
                    p.output = val.text == "best" ? AdaptiveOutput::Best : AdaptiveOutput::Last;
                } else if (kw == "on_metric_unavailable") {
                    if (val.quoted || (val.text != "stop" && val.text != "fail"))
                        return fail(err, line_no, val.col, "invalid_option_value",
                                    "on_metric_unavailable 只能是 stop 或 fail",
                                    "默认 stop（停止迭代并输出最后成功轮）；fail = 该输出失败。");
                    p.on_metric_unavailable = val.text == "fail" ? AdaptiveMetricUnavailable::Fail
                                                                 : AdaptiveMetricUnavailable::Stop;
                } else { // plateau
                    double v = 0.0;
                    if (val.quoted || !parse_double(val.text, v))
                        return fail(err, line_no, val.col, "invalid_option_value",
                                    "plateau 必须是数值",
                                    "plateau 0 = 关闭；>0 = 连续 2 轮改善"
                                    " < 阈值即止。");
                    if (v < 0.0 || !std::isfinite(v))
                        return fail(err, line_no, val.col, "plateau_range",
                                    "plateau 必须 ≥ 0（实际 " + render_number(v) + "）",
                                    "0 = 关闭 plateau 提前终止。");
                    p.plateau = v;
                }
            } else if (kw == "stage") {
                if (toks.size() < 2)
                    return fail(err, line_no, first.col, "missing_value", "stage 缺少名称",
                                "写法：stage <名称>；名称 = [a-z][a-z0-9_-]{0,31}。");
                if (toks.size() > 2)
                    return fail(err, line_no, toks[2].col, "stray_token",
                                "stage 行有多余标记「" + toks[2].text + "」",
                                "stage 行只写一个名称。");
                const Token &id = toks[1];
                if (id.quoted || !valid_stage_id(id.text))
                    return fail(err, line_no, id.col, "bad_stage_id",
                                "stage 名称不合法：「" + id.text + "」",
                                "名称必须匹配 [a-z][a-z0-9_-]{0,31}（小写字母开头，最长 32）。");
                if (!seen_stages.insert(id.text).second)
                    return fail(err, line_no, id.col, "duplicate_stage",
                                "stage 名称重复：「" + id.text + "」",
                                "阶段名用于日志/侧车的逐轮归因，必须唯一。");
                if (!p.stages.empty() && p.stages.back().actions.empty())
                    return fail(err, p.stages.back().line, p.stages.back().col, "empty_stage",
                                "stage「" + p.stages.back().id + "」没有任何动作",
                                "每个 stage 至少要有 1 个 search 或 set；否则请删除该 stage。");
                AdaptiveStage st;
                st.id = id.text;
                st.line = line_no;
                st.col = first.col;
                p.stages.push_back(std::move(st));
                seen_stage = true;
            } else if (kw == "search" || kw == "set") {
                if (p.stages.empty())
                    return fail(err, line_no, first.col, "action_outside_stage",
                                "动作「" + kw + "」必须写在某个 stage 内",
                                "先写 `stage <名称>`，再在其后写 search/set 行。");
                AdaptiveAction a;
                a.kind = kw == "search" ? AdaptiveActionKind::Search : AdaptiveActionKind::Set;
                a.line = line_no;
                a.col = first.col;
                if (a.kind == AdaptiveActionKind::Search) {
                    std::size_t i = 1;
                    if (i >= toks.size())
                        return fail(err, line_no, first.col, "missing_value", "search 缺少参数键",
                                    std::string(kSearchUsage));
                    if (toks[i].quoted)
                        return fail(err, line_no, toks[i].col, "search_syntax",
                                    "search 的目标键不能加引号", std::string(kSearchUsage));
                    a.key = toks[i].text;
                    ++i;
                    if (i >= toks.size() || toks[i].text != "from")
                        return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                    "search_syntax", "search 缺少 from 子句",
                                    std::string(kSearchUsage));
                    ++i;
                    if (i >= toks.size())
                        return fail(err, line_no, first.col, "missing_value",
                                    "search 缺少 from 的取值", std::string(kSearchUsage));
                    if (toks[i].quoted)
                        return fail(err, line_no, toks[i].col, "bad_number",
                                    "search 的 from 必须是 current 或数值",
                                    std::string(kSearchUsage));
                    if (toks[i].text == "current") {
                        a.from.current = true;
                    } else if (!parse_double(toks[i].text, a.from.number)) {
                        return fail(err, line_no, toks[i].col, "bad_number",
                                    "search 的 from 不是合法数值：「" + toks[i].text + "」",
                                    std::string(kSearchUsage));
                    }
                    ++i;
                    // DSL v1.1：可选显式方向 `down` / `up`（位于 from 取值之后、to 之前）。
                    if (i < toks.size() && !toks[i].quoted &&
                        (toks[i].text == "down" || toks[i].text == "up")) {
                        a.direction = toks[i].text == "down" ? AdaptiveDirection::Down
                                                             : AdaptiveDirection::Up;
                        ++i;
                    }
                    if (i >= toks.size() || toks[i].text != "to")
                        return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                    "search_syntax", "search 缺少 to 子句",
                                    std::string(kSearchUsage));
                    ++i;
                    if (i >= toks.size() || toks[i].quoted || !parse_double(toks[i].text, a.to))
                        return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                    "bad_number", "search 的 to 不是合法数值",
                                    std::string(kSearchUsage));
                    ++i;
                    if (i >= toks.size() || toks[i].text != "step")
                        return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                    "search_syntax", "search 缺少 step 子句",
                                    std::string(kSearchUsage));
                    ++i;
                    if (i >= toks.size() || toks[i].quoted || !parse_double(toks[i].text, a.step))
                        return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                    "bad_number", "search 的 step 不是合法数值",
                                    std::string(kSearchUsage));
                    ++i;
                    // DSL v1.1：步长断点，0..N 组 `breakpoint <数值> step <数值>`（位于基础
                    // step 之后、method 之前）。数值升序/范围等语义检查在 validate 阶段。
                    while (i < toks.size() && !toks[i].quoted && toks[i].text == "breakpoint") {
                        ++i;
                        AdaptiveBreakpoint bp;
                        if (i >= toks.size() || toks[i].quoted ||
                            !parse_double(toks[i].text, bp.value))
                            return fail(
                                err, line_no, i < toks.size() ? toks[i].col : first.col,
                                "breakpoint_syntax",
                                "breakpoint 缺少合法数值（应为 `breakpoint <数值> step <数值>`）",
                                std::string(kSearchUsage));
                        ++i;
                        if (i >= toks.size() || toks[i].text != "step")
                            return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                        "breakpoint_syntax", "breakpoint 后必须跟 `step <数值>`",
                                        std::string(kSearchUsage));
                        ++i;
                        if (i >= toks.size() || toks[i].quoted ||
                            !parse_double(toks[i].text, bp.step))
                            return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                        "breakpoint_syntax",
                                        "breakpoint 的 step 缺少合法数值（应为 `breakpoint <数值> "
                                        "step <数值>`）",
                                        std::string(kSearchUsage));
                        ++i;
                        a.breakpoints.push_back(bp);
                    }
                    if (i < toks.size() && toks[i].text == "method") {
                        ++i;
                        if (i >= toks.size() || toks[i].quoted ||
                            !method_from_name(toks[i].text, a.method))
                            return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                        "bad_method",
                                        "method 只能是 linear / bisect / exp / greedy",
                                        std::string(kSearchUsage));
                        ++i;
                    }
                    if (i < toks.size() && toks[i].text == "max_rounds") {
                        ++i;
                        int64_t v = 0;
                        if (i >= toks.size() || toks[i].quoted || !parse_int64(toks[i].text, v))
                            return fail(err, line_no, i < toks.size() ? toks[i].col : first.col,
                                        "bad_integer", "search 的 max_rounds 必须是整数",
                                        std::string(kSearchUsage));
                        if (v < 1 || v > 32)
                            return fail(err, line_no, toks[i].col, "action_max_rounds",
                                        "search 的 max_rounds 必须在 1..32（实际 " +
                                            std::to_string(v) + "）",
                                        "省略该子句表示继承全局 max_rounds。");
                        a.max_rounds = static_cast<int>(v);
                        ++i;
                    }
                    if (i < toks.size())
                        return fail(err, line_no, toks[i].col, "stray_token",
                                    "search 行有多余标记「" + toks[i].text + "」",
                                    std::string(kSearchUsage));
                } else { // set
                    if (toks.size() < 3)
                        return fail(err, line_no, first.col, "missing_value", "set 缺少键或值",
                                    "用法：set <键> <值>（数值/true/false/枚举名/\"字符串\"）。");
                    if (toks[1].quoted)
                        return fail(err, line_no, toks[1].col, "set_syntax",
                                    "set 的目标键不能加引号", "用法：set <键> <值>。");
                    a.key = toks[1].text;
                    a.value = literal_value(toks[2]);
                    if (toks.size() > 3)
                        return fail(err, line_no, toks[3].col, "stray_token",
                                    "set 行有多余标记「" + toks[3].text + "」",
                                    "set 只接受一个键和一个值。");
                }
                p.stages.back().actions.push_back(std::move(a));
            } else {
                return fail(err, line_no, first.col, "unknown_statement",
                            "无法识别的语句「" + kw + "」",
                            "可用语句：version/target/max_rounds/output/on_metric_unavailable/"
                            "plateau/stage/search/set（关键字小写）。");
            }
        }
        if (nl == std::string_view::npos)
            break;
        pos = nl + 1;
    }

    if (!any_content)
        return fail(err, 1, 1, "empty_policy", "自适应策略为空",
                    "格式预设里 adaptive.enabled=true 时必须提供非空策略文本。");
    if (p.stages.empty())
        return fail(err, 0, 0, "no_stage", "策略没有任何 stage",
                    "至少写一个 `stage <名称>` + 1 个 search/set 动作。");
    if (p.stages.back().actions.empty())
        return fail(err, p.stages.back().line, p.stages.back().col, "empty_stage",
                    "stage「" + p.stages.back().id + "」没有任何动作",
                    "每个 stage 至少要有 1 个 search 或 set；否则请删除该 stage。");
    out = std::move(p);
    return ok(err);
}

// ===========================================================================
// §9.2 规范化渲染（幂等）
// ===========================================================================
std::string render_adaptive_policy(const AdaptivePolicy &p) {
    std::string out;
    out += "version " + std::to_string(p.version) + "\n";
    out += "target " + render_number(p.target) + "\n";
    out += "max_rounds " + std::to_string(p.max_rounds) + "\n";
    out += "output " + std::string(adaptive_output_name(p.output)) + "\n";
    out += "on_metric_unavailable " +
           std::string(adaptive_metric_unavailable_name(p.on_metric_unavailable)) + "\n";
    out += "plateau " + render_number(p.plateau) + "\n";
    for (const AdaptiveStage &st : p.stages) {
        out += "\nstage " + st.id + "\n";
        for (const AdaptiveAction &a : st.actions) {
            if (a.kind == AdaptiveActionKind::Search) {
                out += "  search " + a.key + " from ";
                out += a.from.current ? "current" : render_number(a.from.number);
                // DSL v1.1：显式方向按语义写方向词；旧语法（Auto）不写（from current 时无法
                // 静态推断方向，保持旧文本形态）。
                if (a.direction != AdaptiveDirection::Auto)
                    out += " " + std::string(adaptive_direction_name(a.direction));
                out += " to " + render_number(a.to) + " step " + render_number(a.step);
                for (const AdaptiveBreakpoint &bp : a.breakpoints)
                    out += " breakpoint " + render_number(bp.value) + " step " +
                           render_number(bp.step);
                if (a.method != AdaptiveMethod::Linear)
                    out += " method " + std::string(adaptive_method_name(a.method));
                if (a.max_rounds != 0)
                    out += " max_rounds " + std::to_string(a.max_rounds);
                out += "\n";
            } else {
                out += "  set " + a.key + " " + render_value(a.value) + "\n";
            }
        }
    }
    return out;
}

// ===========================================================================
// §9.4 / §9.8 校验
// ===========================================================================
bool validate_adaptive_policy(const AdaptivePolicy &p, std::string_view format_id,
                              AdaptiveError *err) {
    if (p.version != kAdaptivePolicyVersion)
        return fail(err, 0, 0,
                    p.version > kAdaptivePolicyVersion ? "unsupported_version"
                                                       : "invalid_option_value",
                    "不支持的自适应策略版本 " + std::to_string(p.version) + "（本版只支持 1）",
                    "版本不匹配时不得静默按旧语义执行。");
    if (!(p.target > 0.0 && p.target <= 100.0) || !std::isfinite(p.target))
        return fail(err, 0, 0, "target_range",
                    "target 必须在 (0, 100]（实际 " + render_number(p.target) + "）",
                    "推荐 90 = SSIMULACRA 2 的视觉透明档。");
    if (p.max_rounds < 1 || p.max_rounds > 32)
        return fail(err, 0, 0, "max_rounds_range",
                    "max_rounds 必须在 1..32（实际 " + std::to_string(p.max_rounds) + "）",
                    "默认 8；三层轮数上界保证终止。");
    if (p.plateau < 0.0 || !std::isfinite(p.plateau))
        return fail(err, 0, 0, "plateau_range", "plateau 必须 ≥ 0", "0 = 关闭 plateau 提前终止。");
    if (p.output == AdaptiveOutput::Best)
        return fail(err, 0, 0, "output_best_unsupported",
                    "本版只支持 output last（best 属 P1，未交付）",
                    "未达标时输出最后一轮成功编码；请删除 `output best` 或改写为 `output last`。");
    if (p.stages.empty())
        return fail(err, 0, 0, "no_stage", "策略没有任何 stage",
                    "至少写一个 `stage <名称>` + 1 个 search/set 动作。");

    std::set<std::string> stage_ids;
    for (const AdaptiveStage &st : p.stages) {
        if (!valid_stage_id(st.id))
            return fail(err, st.line, st.col, "bad_stage_id", "stage 名称不合法：「" + st.id + "」",
                        "名称必须匹配 [a-z][a-z0-9_-]{0,31}（小写字母开头，最长 32）。");
        if (!stage_ids.insert(st.id).second)
            return fail(err, st.line, st.col, "duplicate_stage",
                        "stage 名称重复：「" + st.id + "」",
                        "阶段名用于日志/侧车的逐轮归因，必须唯一。");
        if (st.actions.empty())
            return fail(err, st.line, st.col, "empty_stage", "stage「" + st.id + "」没有任何动作",
                        "每个 stage 至少要有 1 个 search 或 set；否则请删除该 stage。");
        for (const AdaptiveAction &a : st.actions) {
            if (a.key.empty())
                return fail(err, a.line, a.col, "unknown_key", "动作缺少目标键",
                            "search/set 都必须写目标键。");
            if (a.kind == AdaptiveActionKind::Search) {
                if (!validate_forbidden(a.key, a.line, a.col, err))
                    return false;
                if (!validate_search_action(a, format_id, a.line, a.col, err))
                    return false;
            } else {
                if (!validate_set_action(a, format_id, a.line, a.col, err))
                    return false;
            }
        }
    }
    return ok(err);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：text/format_id 由策略文本/格式语义固定
bool parse_and_validate_adaptive_policy(std::string_view text, std::string_view format_id,
                                        AdaptivePolicy &out, AdaptiveError *err) {
    AdaptivePolicy p;
    if (!parse_adaptive_policy(text, p, err))
        return false;
    AdaptiveError verr;
    if (!validate_adaptive_policy(p, format_id, &verr)) {
        if (err != nullptr)
            *err = std::move(verr);
        return false;
    }
    out = std::move(p);
    return true;
}

// ===========================================================================
// 注册表访问
// ===========================================================================
const std::vector<AdaptiveKeyRule> &adaptive_key_rules() { return key_rules_impl(); }

const std::vector<std::string_view> &adaptive_forbidden_keys() { return forbidden_keys_impl(); }

// ===========================================================================
// 值归一（校验与执行器共用）
// ===========================================================================
bool adaptive_coerce_set_value(const ParamValue &in, const ParamDef &def, ParamValue &out,
                               std::string *err_zh) {
    auto fail_zh = [err_zh](std::string why) {
        if (err_zh != nullptr)
            *err_zh = std::move(why);
        return false;
    };
    auto numeric = [](const ParamValue &v) -> const double * { return std::get_if<double>(&v); };
    switch (def.type) {
    case ParamType::Int: {
        if (const int64_t *i = std::get_if<int64_t>(&in)) {
            out = ParamValue{*i};
            return true;
        }
        if (const double *d = numeric(in)) {
            if (*d == std::floor(*d) && std::abs(*d) < 9007199254740992.0) {
                out = ParamValue{static_cast<int64_t>(*d)};
                return true;
            }
            return fail_zh("需要整数，实际是小数 " + render_number(*d));
        }
        if (const std::string *s = std::get_if<std::string>(&in)) {
            int64_t v = 0;
            if (parse_int64(*s, v)) {
                out = ParamValue{v};
                return true;
            }
            return fail_zh("需要整数，实际是字符串 \"" + *s + "\"");
        }
        return fail_zh("需要整数，实际是" + value_type_name(in));
    }
    case ParamType::Float: {
        if (const double *d = numeric(in)) {
            out = ParamValue{*d};
            return true;
        }
        if (const int64_t *i = std::get_if<int64_t>(&in)) {
            out = ParamValue{static_cast<double>(*i)};
            return true;
        }
        if (const std::string *s = std::get_if<std::string>(&in)) {
            double v = 0.0;
            if (parse_double(*s, v)) {
                out = ParamValue{v};
                return true;
            }
            return fail_zh("需要数值，实际是字符串 \"" + *s + "\"");
        }
        return fail_zh("需要数值，实际是" + value_type_name(in));
    }
    case ParamType::Bool: {
        if (const bool *b = std::get_if<bool>(&in)) {
            out = ParamValue{*b};
            return true;
        }
        if (const std::string *s = std::get_if<std::string>(&in)) {
            if (*s == "true" || *s == "false") {
                out = ParamValue{*s == "true"};
                return true;
            }
            return fail_zh("需要 true/false，实际是字符串 \"" + *s + "\"");
        }
        return fail_zh("需要 true/false，实际是" + value_type_name(in));
    }
    case ParamType::Enum: {
        if (def.choices.empty()) {
            if (std::holds_alternative<std::monostate>(in))
                return fail_zh("缺少取值");
            out = in;
            return true;
        }
        // 枚举：引擎值 / 中文显示名匹配；int 型枚举接受数字与「…（N）」标签。
        if (const std::string *s = std::get_if<std::string>(&in)) {
            for (const auto &c : def.choices) {
                if (choice_matches(*s, c)) {
                    out = c.second;
                    return true;
                }
            }
            return fail_zh("取值 \"" + *s + "\" 不在可选值内");
        }
        if (const int64_t *i = std::get_if<int64_t>(&in)) {
            for (const auto &c : def.choices) {
                if (const int64_t *ci = std::get_if<int64_t>(&c.second)) {
                    if (*ci == *i) {
                        out = c.second;
                        return true;
                    }
                }
                if (const std::string *cs = std::get_if<std::string>(&c.second)) {
                    if (*cs == std::to_string(*i)) {
                        out = c.second;
                        return true;
                    }
                }
            }
            return fail_zh("数字 " + std::to_string(*i) + " 不在可选值内");
        }
        if (const double *d = numeric(in)) {
            if (*d == std::floor(*d) && std::abs(*d) < 9007199254740992.0)
                return adaptive_coerce_set_value(ParamValue{static_cast<int64_t>(*d)}, def, out,
                                                 err_zh);
            return fail_zh("小数 " + render_number(*d) + " 不是合法枚举值");
        }
        if (const bool *b = std::get_if<bool>(&in))
            return adaptive_coerce_set_value(ParamValue{*b}, def, out, err_zh);
        return fail_zh("需要枚举值，实际是" + value_type_name(in));
    }
    case ParamType::String: {
        if (const std::string *s = std::get_if<std::string>(&in)) {
            out = ParamValue{*s};
            return true;
        }
        return fail_zh("需要字符串，实际是" + value_type_name(in));
    }
    }
    return fail_zh("未知的参数类型");
}

// ===========================================================================
// profile 伪参数
// ===========================================================================
bool adaptive_profile_from_value(const ParamValue &in, int64_t &out_profile) {
    if (const int64_t *i = std::get_if<int64_t>(&in)) {
        if (*i < 0 || *i >= kHeifProfileCount)
            return false;
        out_profile = *i;
        return true;
    }
    if (const double *d = std::get_if<double>(&in)) {
        if (*d != std::floor(*d) || std::abs(*d) >= static_cast<double>(kHeifProfileCount) ||
            *d < 0.0)
            return false;
        out_profile = static_cast<int64_t>(*d);
        return true;
    }
    if (const std::string *s = std::get_if<std::string>(&in)) {
        for (const ProfileNameEntry &e : kProfileNames) {
            if (e.name == *s) {
                out_profile = e.value;
                return true;
            }
        }
        for (const ProfileNameEntry &e : kProfileNames) {
            if (equals_ignore_case(e.name, *s)) {
                out_profile = e.value;
                return true;
            }
        }
        int64_t v = 0;
        if (parse_int64(*s, v) && v >= 0 && v < kHeifProfileCount) {
            out_profile = v;
            return true;
        }
        return false;
    }
    return false;
}

std::string adaptive_profile_name(int64_t profile) {
    for (const ProfileNameEntry &e : kProfileNames)
        if (e.value == profile)
            return std::string(e.name);
    return {};
}

// ===========================================================================
// 出厂默认策略库（§9.7）
// ===========================================================================
const std::vector<AdaptiveDefaultPolicy> &default_adaptive_policies() {
    // 设计 §9.7 的出厂默认：HEIF/AVIF/JXL/WebP/JPEG 各 1 条；PNG/TIFF/BMP 不提供
    // （无损、评分≈100，策略无意义）。文本含注释 = 出厂文件可直接给人读/改。
    static const std::vector<AdaptiveDefaultPolicy> policies = {
        {"heif",
         "# HEIF 出厂策略（设计 §9.7 / §9.2 示例 1）：先把质量拉到 98，再切 4:4:4 / still 档位。\n"
         "# 目标 90 = SSIMULACRA 2 视觉透明档；未达标输出最后一轮。\n"
         "version 1\n"
         "target 90\n"
         "max_rounds 8\n"
         "output last\n"
         "on_metric_unavailable stop\n"
         "\n"
         "stage quality-up\n"
         "  search quality from current to 98 step 2\n"
         "\n"
         "stage chroma-444\n"
         "  set chroma 444\n"
         "  set profile Main444_10Still\n"},
        {"avif", "# AVIF 出厂策略（设计 §9.7）：quality 搜索 → 4:4:4 → tune psnr 兜底。\n"
                 "version 1\n"
                 "target 90\n"
                 "max_rounds 8\n"
                 "output last\n"
                 "on_metric_unavailable stop\n"
                 "\n"
                 "stage quality-up\n"
                 "  search quality from current to 95 step 2\n"
                 "\n"
                 "stage chroma-444\n"
                 "  set chroma 444\n"
                 "\n"
                 "stage tune-psnr\n"
                 "  set tune psnr\n"},
        {"jxl", "# JXL 出厂策略（设计 §9.2 示例 2）：distance 线性扫描 → 二分逼近。\n"
                "version 1\n"
                "target 90\n"
                "max_rounds 10\n"
                "output last\n"
                "plateau 0.05\n"
                "\n"
                "stage distance-scan\n"
                "  search distance from current to 0.05 step 0.1\n"
                "\n"
                "stage distance-bisect\n"
                "  search distance from current to 0.05 step 0.02 method bisect\n"},
        {"webp", "# WebP 出厂策略（设计 §9.7）：quality 搜索 → method（压缩努力）提升。\n"
                 "version 1\n"
                 "target 90\n"
                 "max_rounds 8\n"
                 "output last\n"
                 "on_metric_unavailable stop\n"
                 "\n"
                 "stage quality-up\n"
                 "  search quality from current to 95 step 2\n"
                 "\n"
                 "stage method-up\n"
                 "  set method 6\n"},
        {"jpeg", "# JPEG 出厂策略（设计 §9.7）：jpegli quality 搜索。\n"
                 "version 1\n"
                 "target 90\n"
                 "max_rounds 8\n"
                 "output last\n"
                 "on_metric_unavailable stop\n"
                 "\n"
                 "stage quality-up\n"
                 "  search quality from current to 95 step 2\n"},
    };
    return policies;
}

const AdaptiveDefaultPolicy *default_adaptive_policy_for(std::string_view format_id) {
    for (const AdaptiveDefaultPolicy &p : default_adaptive_policies())
        if (p.format_id == format_id)
            return &p;
    return nullptr;
}

// ===========================================================================
// 词表 / 错误码全表
// ===========================================================================
std::string_view adaptive_method_name(AdaptiveMethod m) {
    switch (m) {
    case AdaptiveMethod::Linear:
        return "linear";
    case AdaptiveMethod::Bisect:
        return "bisect";
    case AdaptiveMethod::Exp:
        return "exp";
    case AdaptiveMethod::Greedy:
        return "greedy";
    }
    return "linear";
}

std::string_view adaptive_output_name(AdaptiveOutput o) {
    return o == AdaptiveOutput::Best ? "best" : "last";
}

std::string_view adaptive_metric_unavailable_name(AdaptiveMetricUnavailable m) {
    return m == AdaptiveMetricUnavailable::Fail ? "fail" : "stop";
}

std::string_view adaptive_direction_name(AdaptiveDirection d) {
    switch (d) {
    case AdaptiveDirection::Down:
        return "down";
    case AdaptiveDirection::Up:
        return "up";
    case AdaptiveDirection::Auto:
        break;
    }
    return {}; // Auto = 旧语法：不写方向词
}

// ===========================================================================
// 持久化辅助：JSON 字符串数组 ⇄ 单个 DSL 文本（需求 3/6：可读化，零 \n 转义）
// ===========================================================================
std::vector<std::string> adaptive_policy_to_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string cur;
    for (const char c : text) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r')
                cur.pop_back();
            lines.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty() && cur.back() == '\r')
        cur.pop_back();
    if (!cur.empty())
        lines.push_back(std::move(cur));
    while (!lines.empty() && lines.back().empty())
        lines.pop_back(); // 策略末尾空行无语义（渲染也丢弃）；中间空行/注释行保留
    return lines;
}

bool adaptive_policy_from_lines(const std::vector<std::string> &lines, std::string &out,
                                std::string *err_zh) {
    std::string text;
    text.reserve(lines.size() * 24);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find('\n') != std::string::npos || lines[i].find('\r') != std::string::npos) {
            if (err_zh != nullptr)
                *err_zh = "adaptive.policy 数组的第 " + std::to_string(i + 1) +
                          " 个元素内嵌换行；数组形态下一个元素只能是一行（否则错误行号无法定位）";
            return false;
        }
        text += lines[i];
        text += '\n';
    }
    out = std::move(text);
    return true;
}

std::string adaptive_policy_from_legacy_string(std::string_view text) {
    std::string out;
    std::string ignored;
    (void)adaptive_policy_from_lines(adaptive_policy_to_lines(text), out, &ignored);
    return out;
}

const std::vector<std::string_view> &adaptive_error_codes() {
    static const std::vector<std::string_view> codes = {
        // 语法/词法
        "empty_policy",
        "policy_too_large",
        "unknown_statement",
        "option_after_stage",
        "duplicate_option",
        "missing_value",
        "invalid_option_value",
        "unsupported_version",
        "bad_stage_id",
        "duplicate_stage",
        "action_outside_stage",
        "empty_stage",
        "bad_number",
        "bad_integer",
        "unterminated_string",
        "bad_escape",
        "search_syntax",
        "bad_method",
        "set_syntax",
        "stray_token",
        // DSL v1.1：步长断点（需求 7；顺序/取值在 validate 阶段报）
        "breakpoint_syntax",
        "breakpoint_order",
        "breakpoint_value",
        // 语义/注册表
        "target_range",
        "max_rounds_range",
        "plateau_range",
        "no_stage",
        "unknown_key",
        "forbidden_key",
        "key_not_in_format",
        "not_searchable",
        "search_range",
        "search_noop",
        "step_range",
        "action_max_rounds",
        "set_value_type",
        "set_value_range",
        "enum_value",
        "profile_key_format",
        "profile_value",
        "profile_unavailable",
        "tech_not_supported",
        "output_best_unsupported",
        // JSON AST 导入（ui/format_preset_io）
        "json_syntax",
        "json_field",
    };
    return codes;
}

} // namespace pp
