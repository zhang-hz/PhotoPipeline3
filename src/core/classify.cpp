// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/classify implementation（0.3.0 / M4-W1-T8：分类注册表 / 持久化 / 分组键）
//
// 契约：docs/v0.3.0-design.md §3.6 行 `core/classify.h`（新）+ §6.2（分类子系统）；冻结头见
//   core/classify.h（PP-FROZEN(0.3.0)）。本文件的实现口径：
//   * 纯内存表 + 纯函数：CRUD/归属/分组键不发任何网络、不碰像素（铁律五：无额外图像处理）。
//   * 持久化 = UTF-8 JSON，schema = 本文件自实现的最小 reader/writer（src/core/ 零 Qt；
//     Qt JSON 适配层仍是 src/ui/preset_io.cpp，见该文件头注）。文件形态：
//       { "version": 1,
//         "classes":    [ { "id": "pick", "name": "精选", "rgb": "#2E7D32", "hotkey": "1" }, … ],
//         "assignment": { "<norm_path>": "<class id>", … } }
//     键序固定 = `classes` 的向量顺序 + `assignment` 的 map 顺序（std::map 有序）→ 同一状态
//     两次 save 逐字节相同（test_classify 的幂等用例）。
//   * 读入按"自愈"口径：陈旧/未知 id、重复 id、重复热键、非法 rgb/hotkey 一律丢弃或归零，
//     不因单个坏条目让整份注册表失效；结构性坏 JSON 才返回 false（此时注册表保持原状）。
//   * `load()` **不**做文件存在性清理（§6.2 的"惰性清理"由 `prune_missing()` 承担：load 只见
//     注册表，不知当前文件列表）。
#include "core/classify.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>

#include "core/logger.h"

namespace pp {
namespace {

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

bool is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string trim_copy(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_ascii_space(s[b]))
        ++b;
    while (e > b && is_ascii_space(s[e - 1]))
        --e;
    return std::string(s.substr(b, e - b));
}

char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// 热键域：'1'..'9'（'0' 保留作"清除"，见 core/classify.h 的 ClassDef 注释）
bool is_valid_hotkey(char k) { return k >= '1' && k <= '9'; }

bool is_reserved_id(std::string_view id) { return id == ClassRegistry::kAllId; }

int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// "#RRGGBB" / "RRGGBB" → 0xRRGGBB；非法/缺失 → 0（自愈口径）
std::uint32_t parse_rgb(std::string_view s) {
    if (!s.empty() && s.front() == '#')
        s.remove_prefix(1);
    if (s.size() != 6)
        return 0;
    std::uint32_t v = 0;
    for (const char c : s) {
        const int d = hex_digit(c);
        if (d < 0)
            return 0;
        v = (v << 4) | static_cast<std::uint32_t>(d);
    }
    return v;
}

// ---------------------------------------------------------------------------
// 最小 JSON reader（本文件专用 schema；UTF-8 文本层，路径按原生字节串搬运）
// ---------------------------------------------------------------------------

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    const JsonValue *find(std::string_view key) const {
        if (type != Type::Object)
            return nullptr;
        for (const std::pair<std::string, JsonValue> &kv : object) {
            if (kv.first == key)
                return &kv.second;
        }
        return nullptr;
    }
};

void append_utf8(std::string &out, unsigned int cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : s_(text) {}

    bool parse(JsonValue &out, std::string &err) {
        skip_ws();
        if (!value(out, 0, err))
            return false;
        skip_ws();
        if (pos_ != s_.size())
            return fail(err, "trailing data");
        return true;
    }

private:
    static constexpr int kMaxDepth = 32;

    bool fail(std::string &err, const char *what) const {
        err = std::string(what) + " at byte " + std::to_string(pos_);
        return false;
    }

    void skip_ws() {
        while (pos_ < s_.size() && is_ascii_space(s_[pos_]))
            ++pos_;
    }

    bool literal(std::string_view word) {
        if (s_.compare(pos_, word.size(), word) != 0)
            return false;
        pos_ += word.size();
        return true;
    }

    bool value(JsonValue &out, int depth, std::string &err) {
        if (depth > kMaxDepth)
            return fail(err, "nesting too deep");
        if (pos_ >= s_.size())
            return fail(err, "unexpected end of input");
        switch (s_[pos_]) {
        case '{':
            return object(out, depth, err);
        case '[':
            return array(out, depth, err);
        case '"':
            out.type = JsonValue::Type::String;
            return string(out.str, err);
        case 't':
            if (!literal("true"))
                return fail(err, "expected 'true'");
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return true;
        case 'f':
            if (!literal("false"))
                return fail(err, "expected 'false'");
            out.type = JsonValue::Type::Bool;
            out.boolean = false;
            return true;
        case 'n':
            if (!literal("null"))
                return fail(err, "expected 'null'");
            out.type = JsonValue::Type::Null;
            return true;
        default:
            return number(out, err);
        }
    }

    bool object(JsonValue &out, int depth, std::string &err) {
        out.type = JsonValue::Type::Object;
        ++pos_; // '{'
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip_ws();
            std::string key;
            if (pos_ >= s_.size() || s_[pos_] != '"')
                return fail(err, "expected object key");
            if (!string(key, err))
                return false;
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':')
                return fail(err, "expected ':'");
            ++pos_;
            skip_ws();
            JsonValue v;
            if (!value(v, depth + 1, err))
                return false;
            out.object.emplace_back(std::move(key), std::move(v));
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail(err, "expected ',' or '}'");
        }
    }

    bool array(JsonValue &out, int depth, std::string &err) {
        out.type = JsonValue::Type::Array;
        ++pos_; // '['
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            skip_ws();
            JsonValue v;
            if (!value(v, depth + 1, err))
                return false;
            out.array.push_back(std::move(v));
            skip_ws();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail(err, "expected ',' or ']'");
        }
    }

    bool string(std::string &out, std::string &err) {
        ++pos_; // '"'
        for (;;) {
            if (pos_ >= s_.size())
                return fail(err, "unterminated string");
            const char c = s_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c != '\\') {
                if (static_cast<unsigned char>(c) < 0x20u)
                    return fail(err, "raw control character in string");
                out.push_back(c);
                ++pos_;
                continue;
            }
            ++pos_; // '\'
            if (pos_ >= s_.size())
                return fail(err, "unterminated escape");
            const char e = s_[pos_++];
            switch (e) {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                unsigned int cp = 0;
                if (!hex4(cp, err))
                    return false;
                if (cp >= 0xD800u && cp <= 0xDBFFu) { // 高位代理 → 必须跟低位代理
                    if (pos_ + 1 >= s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u')
                        return fail(err, "unpaired surrogate");
                    pos_ += 2;
                    unsigned int lo = 0;
                    if (!hex4(lo, err))
                        return false;
                    if (lo < 0xDC00u || lo > 0xDFFFu)
                        return fail(err, "invalid low surrogate");
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                    return fail(err, "unpaired surrogate");
                }
                append_utf8(out, cp);
                break;
            }
            default:
                return fail(err, "invalid escape");
            }
        }
    }

    bool hex4(unsigned int &out, std::string &err) {
        if (pos_ + 4 > s_.size())
            return fail(err, "truncated \\u escape");
        unsigned int v = 0;
        for (int i = 0; i < 4; ++i) {
            const int d = hex_digit(s_[pos_ + static_cast<std::size_t>(i)]);
            if (d < 0)
                return fail(err, "invalid \\u escape");
            v = (v << 4) | static_cast<unsigned int>(d);
        }
        pos_ += 4;
        out = v;
        return true;
    }

    bool number(JsonValue &out, std::string &err) {
        const char *begin = s_.data() + pos_;
        char *end = nullptr;
        const double v = std::strtod(begin, &end);
        if (end == begin)
            return fail(err, "invalid value");
        pos_ += static_cast<std::size_t>(end - begin);
        out.type = JsonValue::Type::Number;
        out.number = v;
        return true;
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

// ---------------------------------------------------------------------------
// JSON 写出
// ---------------------------------------------------------------------------

void json_escape(std::string_view s, std::string &out) {
    out.push_back('"');
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20u) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                out += buf;
            } else {
                out.push_back(static_cast<char>(c)); // UTF-8 字节原样透传
            }
        }
    }
    out.push_back('"');
}

std::string hex_rgb(std::uint32_t rgb) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned int>(rgb & 0xFFFFFFu));
    return std::string(buf);
}

// 原子写：`<file>.tmp` → rename（与 core/settings.cpp 同口径；失败不留 tmp）
bool write_atomic(const std::filesystem::path &file, const std::string &text, std::string *err) {
    const auto set_err = [err](const std::string &msg) {
        if (err != nullptr)
            *err = msg;
        return false;
    };
    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
        if (ec)
            return set_err("classify: cannot create directory " + file.parent_path().string() +
                           ": " + ec.message());
    }
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return set_err("classify: cannot write " + tmp.string());
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp, rm_ec);
            return set_err("classify: write failed on " + tmp.string());
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp, rm_ec);
        return set_err("classify: cannot replace " + file.string() + ": " + ec.message());
    }
    return true;
}

// "YYYY:MM:DD…" / "YYYY-MM-DD…" → "YYYY-MM"（月非法/格式不符 → 空串，不猜值）
std::string month_key(std::string_view dt) {
    if (dt.size() < 7)
        return {};
    const char sep_date = dt[4];
    const char sep_month = dt[7];
    if (sep_date != ':' && sep_date != '-')
        return {};
    if (sep_month != ':' && sep_month != '-')
        return {};
    for (const std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u}) {
        if (dt[i] < '0' || dt[i] > '9')
            return {};
    }
    const int month = (dt[5] - '0') * 10 + (dt[6] - '0');
    if (month < 1 || month > 12)
        return {};
    return std::string(dt.substr(0, 4)) + "-" + std::string(dt.substr(5, 2));
}

// 源格式键：去空白 + 去前导 '.' + 小写（"JPEG"/".CR2" → "jpeg"/"cr2"）
std::string format_key(std::string_view fmt) {
    std::string s = trim_copy(fmt);
    if (!s.empty() && s.front() == '.')
        s.erase(0, 1);
    for (char &c : s)
        c = ascii_lower(c);
    return s;
}

} // namespace

// ===========================================================================
// ClassRegistry
// ===========================================================================

ClassRegistry::ClassRegistry() {
    // §6.2 默认模板：精选 / 待定 / 废片（热键与 §6.1 底部提示 `1 精选 2 待定 3 废片 0 清除` 一致）
    classes.push_back(ClassDef{"pick", "精选", 0x2E7D32u, '1'});
    classes.push_back(ClassDef{"maybe", "待定", 0xF9A825u, '2'});
    classes.push_back(ClassDef{"reject", "废片", 0xC62828u, '3'});
}

const ClassDef *ClassRegistry::find(const std::string &id) const {
    if (id.empty() || is_reserved_id(id))
        return nullptr;
    for (const ClassDef &c : classes) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

bool ClassRegistry::add(const ClassDef &c) {
    if (c.id.empty() || is_reserved_id(c.id) || c.name.empty())
        return false;
    if (find(c.id) != nullptr)
        return false; // id 重复
    if (c.hotkey != 0) {
        if (!is_valid_hotkey(c.hotkey))
            return false; // '0' 保留作"清除"；其余非法字符一律拒绝
        for (const ClassDef &other : classes) {
            if (other.hotkey == c.hotkey)
                return false; // 热键唯一：不抢占、不交换
        }
    }
    classes.push_back(c);
    return true;
}

bool ClassRegistry::rename(const std::string &id, const std::string &name) {
    if (name.empty())
        return false;
    for (ClassDef &c : classes) {
        if (c.id == id) {
            c.name = name;
            return true;
        }
    }
    return false; // 含保留 id "all" 与未知 id
}

bool ClassRegistry::erase(const std::string &id) {
    for (auto it = classes.begin(); it != classes.end(); ++it) {
        if (it->id != id)
            continue;
        classes.erase(it);
        // 归属一并清除：被删类的 assignment 不得残留（陈旧 id 不落归属）
        std::size_t removed = 0;
        for (auto a = assignment.begin(); a != assignment.end();) {
            if (a->second == id) {
                a = assignment.erase(a);
                ++removed;
            } else {
                ++a;
            }
        }
        log_debug("Classify", "classify.cpp", "class erased",
                  {{"id", id}, {"assignments_removed", std::to_string(removed)}});
        return true;
    }
    return false;
}

bool ClassRegistry::set_color(const std::string &id, std::uint32_t rgb) {
    for (ClassDef &c : classes) {
        if (c.id == id) {
            c.rgb = rgb & 0xFFFFFFu;
            return true;
        }
    }
    return false;
}

bool ClassRegistry::set_hotkey(const std::string &id, char key) {
    if (key != 0 && !is_valid_hotkey(key))
        return false; // '0' 保留作"清除"
    for (ClassDef &c : classes) {
        if (c.id != id)
            continue;
        if (key != 0) {
            for (const ClassDef &other : classes) {
                if (&other != &c && other.hotkey == key)
                    return false; // 热键唯一：占用者不变
            }
        }
        c.hotkey = key;
        return true;
    }
    return false;
}

void ClassRegistry::assign(const std::filesystem::path &p, const std::string &id) {
    const std::string key = norm_path(p);
    if (key.empty())
        return;
    if (id.empty() || is_reserved_id(id) || find(id) == nullptr) {
        // "全部"= 虚拟分类、未知/陈旧 id、空 id → 无归属（一文件一分类，可无）
        assignment.erase(key);
        return;
    }
    assignment[key] = id;
}

void ClassRegistry::clear_assignment(const std::filesystem::path &p) {
    const std::string key = norm_path(p);
    if (!key.empty())
        assignment.erase(key);
}

std::optional<std::string> ClassRegistry::class_of(const std::filesystem::path &p) const {
    const std::string key = norm_path(p);
    if (key.empty())
        return std::nullopt;
    const auto it = assignment.find(key);
    if (it == assignment.end())
        return std::nullopt;
    if (find(it->second) == nullptr)
        return std::nullopt; // 陈旧 id（类已删）→ 无分类
    return it->second;
}

void ClassRegistry::prune_missing() {
    std::error_code ec;
    std::size_t removed = 0;
    for (auto it = assignment.begin(); it != assignment.end();) {
        const bool present = std::filesystem::exists(it->first, ec);
        if (present || ec) {
            // 存在 → 保留；探测失败（权限/IO）→ 也保留（不因一次探测失败误删归属）
            ++it;
            continue;
        }
        it = assignment.erase(it);
        ++removed;
    }
    if (removed > 0) {
        log_debug("Classify", "classify.cpp", "pruned missing files",
                  {{"removed", std::to_string(removed)},
                   {"remaining", std::to_string(assignment.size())}});
    }
}

std::string ClassRegistry::norm_path(const std::filesystem::path &p) {
    if (p.empty())
        return {};
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(p, ec);
    if (ec)
        abs = p;
    const std::filesystem::path norm = abs.lexically_normal();
    std::string s = norm.generic_string();
    const std::string root = norm.root_path().generic_string();
    while (s.size() > root.size() + 1 && s.back() == '/')
        s.pop_back(); // "a/b/" 与 "a/b" 必须同键（根路径本身不折）
#if defined(_WIN32)
    // 大小写折叠（仅 Windows，§3.6）：只折 ASCII（非 ASCII 的大小写折叠依平台 API/区域，
    // 不属本层口径）；Linux/macOS 保持大小写（macOS 的 APFS 大小写不敏感是文件系统行为，
    // 与注册表键无关 —— 键由本函数单源决定）。
    for (char &c : s)
        c = ascii_lower(c);
#endif
    return s;
}

bool ClassRegistry::load(const std::filesystem::path &file, std::string *err) {
    const auto set_err = [err](const std::string &msg) {
        if (err != nullptr)
            *err = msg;
        return false;
    };
    if (err != nullptr)
        err->clear();

    std::error_code ec;
    if (!std::filesystem::exists(file, ec) || std::filesystem::is_directory(file, ec))
        return true; // 首次运行：保持现状（默认模板），不是错误

    std::ifstream in(file, std::ios::binary);
    if (!in)
        return set_err("classify: cannot read " + file.string());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
        return set_err("classify: read error on " + file.string());

    JsonValue root;
    std::string perr;
    if (!JsonParser(text).parse(root, perr))
        return set_err("classify: invalid json in " + file.string() + ": " + perr);
    if (!root.is_object())
        return set_err("classify: root is not an object in " + file.string());

    // 先解析到暂存区：任何结构性错误都不改动当前注册表（强异常安全）
    std::vector<ClassDef> new_classes;
    std::map<std::string, std::string> new_assignment;

    if (const JsonValue *arr = root.find("classes")) {
        if (!arr->is_array())
            return set_err("classify: 'classes' is not an array in " + file.string());
        for (const JsonValue &e : arr->array) {
            if (!e.is_object())
                continue; // 自愈：坏条目丢弃
            const JsonValue *id = e.find("id");
            if (id == nullptr || !id->is_string() || id->str.empty() || is_reserved_id(id->str))
                continue; // 空 id / 保留 id "all" 不入库
            if (std::any_of(new_classes.begin(), new_classes.end(),
                            [&id](const ClassDef &c) { return c.id == id->str; }))
                continue; // 重复 id：先到先得
            ClassDef d;
            d.id = id->str;
            const JsonValue *name = e.find("name");
            d.name =
                (name != nullptr && name->is_string() && !name->str.empty()) ? name->str : d.id;
            const JsonValue *rgb = e.find("rgb");
            if (rgb != nullptr && rgb->is_string())
                d.rgb = parse_rgb(rgb->str);
            const JsonValue *hk = e.find("hotkey");
            if (hk != nullptr && hk->is_string() && hk->str.size() == 1 &&
                is_valid_hotkey(hk->str[0]))
                d.hotkey = hk->str[0];
            // 热键唯一（自愈）：先到先得，后到者归零
            if (d.hotkey != 0) {
                for (const ClassDef &prev : new_classes) {
                    if (prev.hotkey == d.hotkey) {
                        d.hotkey = 0;
                        break;
                    }
                }
            }
            new_classes.push_back(std::move(d));
        }
    }

    if (const JsonValue *obj = root.find("assignment")) {
        if (!obj->is_object())
            return set_err("classify: 'assignment' is not an object in " + file.string());
        for (const std::pair<std::string, JsonValue> &kv : obj->object) {
            if (kv.first.empty() || !kv.second.is_string() || kv.second.str.empty())
                continue;
            const std::string &class_id = kv.second.str;
            if (is_reserved_id(class_id))
                continue;
            const bool known =
                std::any_of(new_classes.begin(), new_classes.end(),
                            [&class_id](const ClassDef &c) { return c.id == class_id; });
            if (!known)
                continue; // 陈旧 id（类已删）→ 自愈丢弃
            new_assignment[kv.first] = class_id;
        }
    }

    classes = std::move(new_classes);
    assignment = std::move(new_assignment);
    return true;
}

bool ClassRegistry::save(const std::filesystem::path &file, std::string *err) const {
    if (err != nullptr)
        err->clear();

    std::string out;
    out += "{\n";
    out += "  \"version\": 1,\n";
    if (classes.empty()) {
        out += "  \"classes\": [],\n";
    } else {
        out += "  \"classes\": [\n";
        for (std::size_t i = 0; i < classes.size(); ++i) {
            const ClassDef &c = classes[i];
            out += "    { \"id\": ";
            json_escape(c.id, out);
            out += ", \"name\": ";
            json_escape(c.name, out);
            out += ", \"rgb\": ";
            json_escape(hex_rgb(c.rgb), out);
            out += ", \"hotkey\": ";
            if (is_valid_hotkey(c.hotkey)) {
                out.push_back('"');
                out.push_back(c.hotkey);
                out.push_back('"');
            } else {
                out += "null"; // 0 = 未分配
            }
            out += " }";
            out += (i + 1 == classes.size()) ? "\n" : ",\n";
        }
        out += "  ],\n";
    }
    if (assignment.empty()) {
        out += "  \"assignment\": {}\n";
    } else {
        out += "  \"assignment\": {\n";
        std::size_t i = 0;
        for (const std::pair<const std::string, std::string> &kv : assignment) {
            out += "    ";
            json_escape(kv.first, out);
            out += ": ";
            json_escape(kv.second, out);
            out += (++i == assignment.size()) ? "\n" : ",\n";
        }
        out += "  }\n";
    }
    out += "}\n";

    return write_atomic(file, out, err);
}

// ===========================================================================
// GroupKey 提取（§6.2 自动分组视图；纯函数）
// ===========================================================================

std::string group_key(GroupMode mode, const GroupSource &src) {
    switch (mode) {
    case GroupMode::None:
        return {}; // 「无分组」= 单一节（节头名 = kNoGroupLabel）
    case GroupMode::Month:
        return month_key(src.datetime_original);
    case GroupMode::Camera:
        return trim_copy(src.camera_model);
    case GroupMode::SourceFormat:
        return format_key(src.source_format);
    }
    return {};
}

} // namespace pp
