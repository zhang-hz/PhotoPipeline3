// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — minimal INI settings store.
//
// Contract: docs/m1-tasks.md §3.14 (PP-FROZEN header) + docs/m1b-tasks.md §2.2:
//   * single level `key=value`, `#` starts a whole-line comment;
//   * unknown keys (and comments, and malformed lines) are kept verbatim on save;
//   * missing file -> all defaults, no error;
//   * saving is atomic: write `<file>.tmp`, then rename over `<file>`.
//
// Key names are the struct field names (snake_case); booleans are written `true`/`false`;
// doubles use the shortest `%.10g` form. Values that do not parse are ignored on load
// (the default is kept) — load_settings has no error channel by contract.
//
// M5-T11（A28，按契约 C6 的加性追加面）：新增 `ui_theme_mode`（system|light|dark，默认
// system）—— settings_pairs 末尾追加一行 + apply_pair 加一个词表分支；既有键的读写语义与
// 文件字节序（未知键/注释原样保留）零改动（test_settings 的全字段往返随码扩展）。
//
// M6-T11（契约 C6「设置加键规则」，docs/m6-map-upgrade.md §3）：新增 `map_style`
// （road|satellite|hybrid，默认 road）+ `map_sat_labels`（bool，默认 true）—— 同口径的加性
// 追加（settings_pairs 末尾两行 + apply_pair 两个分支）；`map_style` 词表外/空值保留默认，
// `amap_key` 脱敏规则与既有键语义一律不变。
//
// M7-WP5（R-094，docs/deep-review-0.3.2.md「key 明文存 settings」）：`amap_key` 落盘经
// platform/secret 封装 —— 写侧 seal（Windows DPAPI / 其它平台编码档）→ **明文不再落盘**；
// 读侧 unseal（无前缀的老明文原样兼容，首次保存即升级为封存形态）；保存侧对等价封存行逐字
// 保留（DPAPI 封存非确定性 → 不换密文）、对解不开的封存行不冲空（换机/换账号不丢密文）；
// 保存成功后 harden_file 收紧权限（非 Windows 0600）。其余键的读写语义与"未知键/注释原样
// 保留"的字节口径零改动（test_settings 全字段往返随码换锚 amap_key 断言）。

#include "core/settings.h"

#include "core/fsops.h"      // validate_output_template()（§4.2 模板校验，T15 加载期口径）
#include "platform/secret.h" // M7-WP5 / R-094：amap_key 落盘封存（DPAPI/编码档，明文不落盘）

#include <cctype> // std::tolower（A28 的 ui_theme_mode 词表解析）
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace pp {
namespace {

bool is_space(unsigned char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trim(std::string s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_space(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && is_space(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool parse_int(const std::string &v, int &out) {
    if (v.empty())
        return false;
    errno = 0;
    char *end = nullptr;
    const long val = std::strtol(v.c_str(), &end, 10);
    if (errno != 0 || end == v.c_str() || *end != '\0')
        return false;
    if (val < INT_MIN || val > INT_MAX)
        return false;
    out = static_cast<int>(val);
    return true;
}

bool parse_double(const std::string &v, double &out) {
    if (v.empty())
        return false;
    errno = 0;
    char *end = nullptr;
    const double val = std::strtod(v.c_str(), &end);
    if (errno != 0 || end == v.c_str() || *end != '\0')
        return false;
    out = val;
    return true;
}

bool parse_bool(const std::string &v, bool &out) {
    std::string low;
    low.reserve(v.size());
    for (char c : v)
        low.push_back(ascii_lower(c));
    if (low == "true" || low == "yes" || low == "on" || low == "1") {
        out = true;
        return true;
    }
    if (low == "false" || low == "no" || low == "off" || low == "0") {
        out = false;
        return true;
    }
    return false;
}

std::string format_double(double d) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", d);
    return buf;
}

// Serialized field list, in a fixed order: used both for saving (values) and for the
// log snapshot. `key` is exactly the AppSettings field name.
// `redact_secrets` is used by settings_to_string() only: the log snapshot must never carry
// the amap web-service key in clear text (main-dialogue ruling on the M1b-U1 report), while
// save_settings() obviously writes the real value.
std::vector<std::pair<std::string, std::string>> settings_pairs(const AppSettings &s,
                                                                bool redact_secrets = false) {
    std::vector<std::pair<std::string, std::string>> kv;
    kv.emplace_back("workers", std::to_string(s.workers));
    kv.emplace_back("budget_gb", std::to_string(s.budget_gb));
    kv.emplace_back("flatten_gray", format_double(s.flatten_gray));
    kv.emplace_back("log_level", s.log_level);
    kv.emplace_back("map_provider", s.map_provider);
    // —— R-094（M7-WP5）：日志快照照旧脱敏；**落盘值经 secret::seal**（明文不再落盘）——
    kv.emplace_back("amap_key", (redact_secrets && !s.amap_key.empty())
                                    ? std::string("<set>")
                                    : pp::platform::secret::seal(s.amap_key));
    kv.emplace_back("tile_cache_mb", std::to_string(s.tile_cache_mb));
    kv.emplace_back("rotate_orientation", s.rotate_orientation ? "true" : "false");
    kv.emplace_back("last_format", s.last_format);
    kv.emplace_back("last_preset", s.last_preset);
    kv.emplace_back("last_out_root", s.last_out_root);
    // —— 0.3.0 / W1-T7 追加（§3.6 行前两项；顺序 = 结构体末尾顺序）——
    kv.emplace_back("stagger_ms", std::to_string(s.stagger_ms));
    kv.emplace_back("thread_budget", std::to_string(s.thread_budget));
    // —— 0.3.0 / W1-T8 追加（§3.6 行最后一项；顺序 = 结构体末尾顺序）——
    kv.emplace_back("class_file", s.class_file);
    // —— 0.3.0 / W3-T15 追加（§3.6 行中间两项；追加顺序 = 结构体末尾顺序，见 settings.h 头注）——
    kv.emplace_back("output_template", s.output_template);
    kv.emplace_back("split_by_format", s.split_by_format ? "true" : "false");
    // —— 0.3.0 / M5-T11 追加（A28；追加顺序 = 结构体末尾顺序）——
    kv.emplace_back("ui_theme_mode", s.ui_theme_mode);
    // —— M6-T11 追加（契约 C6；追加顺序 = 结构体末尾顺序）——
    kv.emplace_back("map_style", s.map_style);
    kv.emplace_back("map_sat_labels", s.map_sat_labels ? "true" : "false");
    return kv;
}

void apply_pair(AppSettings &s, const std::string &key, const std::string &value) {
    if (key == "workers") {
        int v = 0;
        if (parse_int(value, v))
            s.workers = v;
    } else if (key == "budget_gb") {
        int v = 0;
        if (parse_int(value, v))
            s.budget_gb = v;
    } else if (key == "flatten_gray") {
        double v = 0;
        if (parse_double(value, v))
            s.flatten_gray = v;
    } else if (key == "log_level") {
        if (!value.empty())
            s.log_level = value;
    } else if (key == "map_provider") {
        // R-104（M7-R2B）：持久层词表回落（对照 map_style / ui_theme_mode 的"值不合法 →
        // 保留默认"口径）—— 词表 = osm|amap（单源 = mapwidget/providers.h `provider_ids()`，
        // core 不上望 mapwidget，此处镜像同集并注释锚定；UI 侧回落已由 M7-WP7 落）。
        // 词表外/空值 → 保留默认（osm）：手改 INI 写 foo 时下拉不再落空留脏值。
        // 大小写不敏感（手改配置文件容错）。
        std::string v = value;
        for (char &c : v)
            c = ascii_lower(c);
        if (v == "osm" || v == "amap")
            s.map_provider = v;
    } else if (key == "amap_key") {
        // R-094（M7-WP5）：读侧解封（空值 = 未配置，恒为空）。三种存量形态：
        //   * 封存串（ppsec1:/ppsec2:）→ 解封得明文；解不开（换机/换账号/损坏）→ 空；
        //   * 0.3.2 及更早的明文 → 原样读入（老格式兼容；下次保存即升级为封存形态）。
        s.amap_key = pp::platform::secret::unseal(value);
    } else if (key == "tile_cache_mb") {
        int v = 0;
        if (parse_int(value, v))
            s.tile_cache_mb = v;
    } else if (key == "rotate_orientation") {
        bool v = false;
        if (parse_bool(value, v))
            s.rotate_orientation = v;
    } else if (key == "last_format") {
        s.last_format = value;
    } else if (key == "last_preset") {
        s.last_preset = value;
    } else if (key == "last_out_root") {
        s.last_out_root = value;
    } else if (key == "stagger_ms") {
        // §8.1/§9.3：设置域 0–2000 ms（0 = 关闭）。本存储层保持"哑存储"口径（不解析即忽略、
        // 不裁剪合法整数 —— 域裁剪属设置对话框/UI 的输入约束，见 §9.3）；
        // 仅把**负数**归零（引擎口径：<=0 即关闭，见 StaggerLimiter），避免负值进入日志/引擎。
        int v = 0;
        if (parse_int(value, v))
            s.stagger_ms = v > 0 ? v : 0;
    } else if (key == "thread_budget") {
        // §8.2：0 = 逻辑核；负值无意义 → 归零（同"自动"）。
        int v = 0;
        if (parse_int(value, v))
            s.thread_budget = v > 0 ? v : 0;
    } else if (key == "class_file") {
        // §3.6/§6.2：分类注册表路径。空值 = 未配置 → 保留默认（惰性空串，消费点经
        // default_class_file() 解析 —— R-050），因为"空路径"不是合法注册表位置。
        if (!value.empty())
            s.class_file = value;
    } else if (key == "output_template") {
        // §4.2/§9.3：路径模板默认值。与 store 的既有口径一致（"值不合法 → 保留默认"）：
        //   * 空值 = 未配置 → 保留默认（同 class_file）；
        //   * `validate_output_template()` 判负（未知 $ 符号 / `..` / 绝对路径 / 反斜杠 /
        //     盘符）→ 保留默认 —— 非法模板不进内存，输出页的 ready_to_start 也就不会因
        //     手改配置文件而莫名变红（校验仍是同一函数，见 core/fsops.h）。
        if (!value.empty() && validate_output_template(value, nullptr))
            s.output_template = value;
    } else if (key == "split_by_format") {
        // §4.2 联动：本开关 = "模板含 $format 段"的存储镜像。存储层只做布尔解析（哑存储），
        // 与模板的配平由设置对话框/输出页维护（§9.3 设置项「分文件夹默认结构」）。
        bool v = false;
        if (parse_bool(value, v))
            s.split_by_format = v;
    } else if (key == "ui_theme_mode") {
        // A28/§9：手动主题档。词表 = system|light|dark；**词表外/空值 → 保留默认（system）**
        // （同 class_file/output_template 的"值不合法 → 保留默认"口径）—— 不把未知档带进
        // 内存，UI 侧的三档回填也就不会出现"第四档"。大小写不敏感（手改配置文件容错）。
        std::string v = value;
        for (char &c : v)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (v == "system" || v == "light" || v == "dark")
            s.ui_theme_mode = v;
    } else if (key == "map_style") {
        // C6/§5.2：GPS 卡图层档。词表 = road|satellite|hybrid（与 pp::map::TileStyle 同序）；
        // **词表外/空值 → 保留默认（road）**（同 ui_theme_mode 的"值不合法 → 保留默认"口径）
        // —— 不把未知档带进内存，GPS 卡的三态 Seg 也就不会出现"第四档"。大小写不敏感
        // （手改配置文件容错）。
        std::string v = value;
        for (char &c : v)
            c = ascii_lower(c);
        // R-137（M7-R2.1-F4）：词表判定走 pp::kMapStyleIds 单源（is_map_style_id）。
        if (is_map_style_id(v))
            s.map_style = v;
    } else if (key == "map_sat_labels") {
        // C6：混合图层的「叠标注」开关（哑存储：只做既有布尔词表解析；消费归 UI/W-M5）。
        bool v = false;
        if (parse_bool(value, v))
            s.map_sat_labels = v;
    }
}

// Split one raw line into (key, value); returns false for comments/blank/malformed lines.
bool split_line(const std::string &line, std::string &key, std::string &value) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#')
        return false;
    const std::size_t eq = t.find('=');
    if (eq == std::string::npos)
        return false;
    key = trim(t.substr(0, eq));
    value = trim(t.substr(eq + 1));
    return !key.empty();
}

} // namespace

AppSettings load_settings(const std::filesystem::path &file) {
    AppSettings s; // defaults (docs/m1-tasks.md §3.14)
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return s; // missing/unreadable file -> all defaults, no error

    std::string line;
    while (std::getline(in, line)) {
        std::string key;
        std::string value;
        if (!split_line(line, key, value))
            continue;
        apply_pair(s, key, value);
    }
    return s;
}

std::string save_settings(const std::filesystem::path &file, const AppSettings &s) {
    const std::vector<std::pair<std::string, std::string>> pairs = settings_pairs(s);

    // 1) Read the current file so comments/unknown keys survive byte-for-byte.
    std::vector<std::string> lines;
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return "settings: cannot read " + file.string();
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
        if (in.bad())
            return "settings: read error on " + file.string();
    }

    // 2) Rewrite known keys in place; unknown/comment/malformed lines are left untouched.
    std::vector<bool> seen(pairs.size(), false);
    for (std::string &line : lines) {
        std::string key;
        std::string value;
        if (!split_line(line, key, value))
            continue;
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            if (pairs[i].first != key)
                continue;
            // —— R-094 密钥行特例（M7-WP5）：等价即原行逐字保留 ——
            //   1) 老行是封存串且解封 == 新明文 → 保留原行（DPAPI 封存非确定性：否则每次
            //      保存都换密文 → 文件字节无谓漂移，"保存两次不漂移"的口径失守）；
            //   2) 老行是解不开的封存串（换机/换账号）且新值为空 → 保留原行，不把不可恢复的
            //      密文冲成空值（用户回到原账号仍可解）；
            //   3) 其余（老明文/值变化/新值非空）→ 写封存串 —— 老明文行在此**首次保存即升级**。
            if (key == "amap_key") {
                pp::platform::secret::StoredKind kind = pp::platform::secret::StoredKind::Empty;
                const std::string old_plain = pp::platform::secret::unseal(value, &kind);
                const bool equivalent =
                    kind == pp::platform::secret::StoredKind::Sealed && old_plain == s.amap_key;
                const bool preserve_corrupt =
                    kind == pp::platform::secret::StoredKind::Corrupt && s.amap_key.empty();
                if ((equivalent || preserve_corrupt) && !value.empty()) {
                    seen[i] = true;
                    break;
                }
            }
            line = key + "=" + pairs[i].second;
            seen[i] = true;
            break;
        }
    }

    // 3) Append keys the file did not have yet (fresh file: all of them).
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (!seen[i])
            lines.push_back(pairs[i].first + "=" + pairs[i].second);
    }

    // 4) Atomic write（R-194/R-290，M7-WP1 收敛）：实现单源 = core/fsops 的 write_atomic_file
    //    （I1：同目录 PID/随机后缀 tmp → 字节校验 → rename 覆盖；失败清场）。
    //    A missing parent directory fails here (creating it is platform::data_dir()'s job,
    //    not the settings store's).
    std::string text;
    for (const std::string &line : lines) {
        text += line;
        text += '\n';
    }
    std::string io_err;
    if (!write_atomic_file(file, text, &io_err))
        return "settings: " + io_err;
    // R-094（M7-WP5）：承载密钥的文件收紧权限（非 Windows 0600；Windows no-op —— DPAPI 密文
    // 自保护）。失败不阻断保存（权限是加固不是正确性前提），口径见 platform/secret.h。
    (void)pp::platform::secret::harden_file(file);
    return {};
}

std::string settings_to_string(const AppSettings &s) {
    std::string out;
    for (const auto &[key, value] : settings_pairs(s, /*redact_secrets=*/true)) {
        if (!out.empty())
            out += ' ';
        out += key;
        out += '=';
        out += value;
    }
    return out;
}

} // namespace pp
