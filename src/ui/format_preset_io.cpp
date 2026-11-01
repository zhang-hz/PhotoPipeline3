// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 格式级参数预设的 JSON/文件 I/O（M10 / W3-T1；契约 docs/v0.4.0-design.md §8 C23）
//
// 路径层按**原生字节串**处理（复用 ui/preset_io.cpp 的 M2-T14 口径）：文件读写走
// std::filesystem + FILE*（Windows 宽字符打开），QString 只用于 JSON 文本。这样含非 UTF-8
// 字节的目录/文件名不会被 Qt 的 UTF-8 假设改写（与全局预设 I/O 同一条纪律）。
#include "ui/format_preset_io.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QString>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/fsops.h" // write_atomic_file（R-194/R-290 原子写单实现）
#include "core/logger.h"
#include "core/params.h"
#include "platform/paths.h"

namespace pp::ui {
namespace {

constexpr std::string_view kStage = "preset";

QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
std::string utf8(const QString &s) { return s.toUtf8().toStdString(); }

bool is_valid_utf8(const std::string &s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        unsigned int cp = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + extra >= s.size())
            return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += extra + 1;
    }
    return true;
}

// 错误信息里的路径：合法 UTF-8 原样；否则非 ASCII 字节转义成 \xNN（避免 UI 丢字节）。
std::string display_path(const std::filesystem::path &p) {
    std::string s = p.string();
    if (is_valid_utf8(s))
        return s;
    std::string out;
    for (const unsigned char c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string errno_text() {
    if (errno == 0)
        return "I/O error";
    return std::error_code(errno, std::generic_category()).message();
}

// 读整个文件（FILE* 而非 ifstream：真实读错误按返回值报错、不抛异常；与 preset_io 同口径）。
std::string read_file_bytes(const std::filesystem::path &file, std::string &out) {
#if defined(_WIN32)
    std::FILE *f = ::_wfopen(file.c_str(), L"rb");
#else
    std::FILE *f = std::fopen(file.c_str(), "rb");
#endif
    if (f == nullptr)
        return "无法打开 '" + display_path(file) + "' 读取";
    std::string bytes;
    char buf[64 * 1024];
    std::string err;
    for (;;) {
        errno = 0;
        const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n > 0)
            bytes.append(buf, n);
        if (n == sizeof(buf))
            continue;
        if (std::ferror(f) != 0)
            err = "读取 '" + display_path(file) + "' 失败：" + errno_text();
        break;
    }
    std::fclose(f);
    if (!err.empty())
        return err;
    out = std::move(bytes);
    return {};
}

std::string directory_target_error(const std::filesystem::path &file, const char *action) {
    std::error_code dir_ec;
    if (!std::filesystem::is_directory(file, dir_ec))
        return {};
    return std::string("无法") + action + " '" + display_path(file) + "'：目标是目录";
}

bool has_json_suffix(const std::string &name) {
    if (name.size() < 5)
        return false;
    std::string ext = name.substr(name.size() - 5);
    for (char &c : ext)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return ext == ".json";
}

// 路径段安全（格式 id / 预设名）：拒绝空/./../分隔符/控制字符（防目录逃逸）。
bool safe_component(std::string_view s) {
    if (s.empty() || s == "." || s == "..")
        return false;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '/' || c == '\\')
            return false;
    }
    return true;
}

// 内置（出厂）预设的文件名标记（单源 = core/format_preset.h 的 kFormatPresetBuiltinPrefix）。
bool has_builtin_prefix(std::string_view name) {
    const std::string_view prefix = kFormatPresetBuiltinPrefix;
    return name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0;
}

// —— ParamValue ⇄ JSON ——
QJsonValue to_json(const ParamValue &v) {
    if (const bool *b = std::get_if<bool>(&v))
        return QJsonValue(*b);
    if (const int64_t *i = std::get_if<int64_t>(&v))
        return QJsonValue(static_cast<qint64>(*i));
    if (const double *d = std::get_if<double>(&v))
        return QJsonValue(*d);
    if (const std::string *s = std::get_if<std::string>(&v))
        return QJsonValue(qs(*s));
    return QJsonValue(QJsonValue::Null);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
const ParamDef *lookup_param_def(const std::string &format_id, const std::string &backend_id,
                                 const std::string &tech_id, const std::string &key) {
    const FormatDef *f = find_format(format_id);
    return f != nullptr ? pp::lookup_param_def(*f, backend_id, tech_id, key) : nullptr;
}

// 按表内类型还原 variant（保证预设往返类型精确）；无 schema 信息时按 JSON 类型推断。
ParamValue param_from_json(const QJsonValue &v, const ParamDef *def) {
    if (def != nullptr) {
        switch (def->type) {
        case ParamType::Int:
            return ParamValue{static_cast<int64_t>(v.toInteger(0))};
        case ParamType::Float:
            return ParamValue{v.toDouble(0.0)};
        case ParamType::Bool:
            return ParamValue{v.toBool(false)};
        case ParamType::Enum:
            if (!def->choices.empty()) {
                const ParamValue &proto = def->choices.front().second;
                if (std::holds_alternative<int64_t>(proto))
                    return ParamValue{static_cast<int64_t>(v.toInteger(0))};
                if (std::holds_alternative<std::string>(proto))
                    return ParamValue{utf8(v.toString())};
                if (std::holds_alternative<bool>(proto))
                    return ParamValue{v.toBool(false)};
            }
            break;
        }
    }
    switch (v.type()) {
    case QJsonValue::Bool:
        return ParamValue{v.toBool()};
    case QJsonValue::String:
        return ParamValue{utf8(v.toString())};
    case QJsonValue::Double: {
        constexpr double kMaxExactInt = 9007199254740992.0; // 2^53（JSON 安全整数上限）
        const double d = v.toDouble();
        if (d == std::floor(d) && std::abs(d) <= kMaxExactInt)
            return ParamValue{static_cast<int64_t>(d)};
        return ParamValue{d};
    }
    default:
        return ParamValue{};
    }
}

QJsonObject params_to_json(const ParamSet &params) {
    QJsonObject out;
    for (const auto &[key, value] : params) {
        if (key.rfind("__", 0) == 0)
            continue; // 保留键不落盘（heif_profile 走 reserved；__lossless 由显式 lossless
                      // 参数承载）
        out.insert(qs(key), to_json(value));
    }
    return out;
}

bool params_from_json(const QJsonValue &v, const std::string &format_id,
                      const std::string &backend_id, const std::string &tech_id, ParamSet &out,
                      std::string &err) {
    if (v.isUndefined() || v.isNull())
        return true;
    if (!v.isObject()) {
        err = "params 必须是 JSON 对象";
        return false;
    }
    const QJsonObject params = v.toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const std::string key = utf8(it.key());
        if (key.rfind("__", 0) == 0)
            continue; // 保留键由 reserved/显式参数重建（与 preset_io.cpp 同口径）
        if (it.value().isObject() || it.value().isArray() || it.value().isNull()) {
            err = "参数「" + key + "」的值必须是标量（数字/布尔/字符串）";
            return false;
        }
        out[key] =
            param_from_json(it.value(), lookup_param_def(format_id, backend_id, tech_id, key));
    }
    return true;
}

// —— JSON → FormatPresetData（只解析结构；语义校验在 load_format_preset 末尾统一做）——
std::string parse_preset_object(const QJsonObject &root, FormatPresetData &out) {
    // kind 必须存在且为 "format-preset"：防止把全局预设（outputs[] 形态）选进格式预设下拉。
    const QJsonValue kind_v = root.value(QStringLiteral("kind"));
    if (kind_v.isUndefined()) {
        if (root.contains(QStringLiteral("outputs")))
            return "这不是格式预设文件（缺少 kind 字段，且含全局预设的 outputs 数组）；"
                   "请在全局预设区载入该文件";
        return "这不是格式预设文件：缺少 kind 字段（kind 必须为 \"format-preset\"）";
    }
    if (!kind_v.isString() || utf8(kind_v.toString()) != kFormatPresetKind) {
        return "不是格式预设文件：kind = \"" + utf8(kind_v.toString()) +
               "\"（应为 \"format-preset\"）；若要载入全局预设，请用全局预设下拉";
    }
    const QJsonValue version_v = root.value(QStringLiteral("version"));
    int version = 1;
    if (!version_v.isUndefined()) {
        if (!version_v.isDouble() || version_v.toDouble() != std::floor(version_v.toDouble()))
            return "格式预设字段 version 必须是整数";
        version = static_cast<int>(version_v.toInteger(1));
        if (version > kFormatPresetVersion) {
            return "不支持的格式预设版本 " + std::to_string(version) +
                   "（当前支持 1；请升级 PhotoPipeline，或把文件降级为 v1）";
        }
        if (version < 1)
            return "格式预设字段 version 非法（" + std::to_string(version) + "）";
    }
    FormatPresetData p;
    p.version = version;
    p.name = utf8(root.value(QStringLiteral("name")).toString());
    p.format_id = utf8(root.value(QStringLiteral("format")).toString());
    p.backend_id = utf8(root.value(QStringLiteral("backend")).toString());
    p.tech_id = utf8(root.value(QStringLiteral("tech")).toString());
    const QJsonValue bitdepth_v = root.value(QStringLiteral("bitdepth"));
    if (!bitdepth_v.isUndefined()) {
        if (!bitdepth_v.isDouble() || bitdepth_v.toDouble() != std::floor(bitdepth_v.toDouble()))
            return "格式预设字段 bitdepth 必须是整数";
        p.out_bitdepth = static_cast<int>(bitdepth_v.toInteger(0));
    }
    std::string perr;
    if (!params_from_json(root.value(QStringLiteral("params")), p.format_id, p.backend_id,
                          p.tech_id, p.params, perr))
        return perr;
    const QJsonValue reserved_v = root.value(QStringLiteral("reserved"));
    if (!reserved_v.isUndefined() && !reserved_v.isNull()) {
        if (!reserved_v.isObject())
            return "格式预设字段 reserved 必须是 JSON 对象";
        const QJsonObject reserved = reserved_v.toObject();
        for (auto it = reserved.constBegin(); it != reserved.constEnd(); ++it) {
            const std::string key = utf8(it.key());
            if (key == kFormatPresetHeifProfileReserved) {
                if (!it.value().isDouble())
                    return "reserved.heif_profile 必须是整数";
                const double d = it.value().toDouble();
                if (d != std::floor(d))
                    return "reserved.heif_profile 必须是整数";
                p.heif_profile = static_cast<int64_t>(d);
            } else {
                log_warn(kStage, "format_preset_io.cpp", "unknown reserved field ignored",
                         {{"field", key}});
            }
        }
    }
    const QJsonValue adaptive_v = root.value(QStringLiteral("adaptive"));
    if (!adaptive_v.isUndefined() && !adaptive_v.isNull()) {
        if (!adaptive_v.isObject())
            return "格式预设字段 adaptive 必须是 JSON 对象";
        const QJsonObject a = adaptive_v.toObject();
        const QJsonValue enabled_v = a.value(QStringLiteral("enabled"));
        if (!enabled_v.isUndefined() && !enabled_v.isBool())
            return "adaptive.enabled 必须是布尔值";
        p.adaptive_enabled = enabled_v.toBool(false);
        const QJsonValue policy_v = a.value(QStringLiteral("policy"));
        if (!policy_v.isUndefined() && !policy_v.isNull()) {
            if (!policy_v.isString())
                return "adaptive.policy 必须是字符串（行式 DSL 文本）";
            p.adaptive_policy = utf8(policy_v.toString());
        }
        for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
            const std::string key = utf8(it.key());
            if (key != "enabled" && key != "policy")
                log_warn(kStage, "format_preset_io.cpp", "unknown adaptive field ignored",
                         {{"field", key}});
        }
    }
    // 未知根字段忽略但记日志（前向兼容；设计 §8.2）。
    static const char *const kKnown[] = {"kind", "version",  "name",   "format",   "backend",
                                         "tech", "bitdepth", "params", "reserved", "adaptive"};
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const std::string key = utf8(it.key());
        const bool known = std::any_of(std::begin(kKnown), std::end(kKnown),
                                       [&key](const char *k) { return key == k; });
        if (!known)
            log_warn(kStage, "format_preset_io.cpp", "unknown format preset field ignored",
                     {{"field", key}});
    }
    out = std::move(p);
    return {};
}

QJsonObject preset_to_json(const FormatPresetData &p) {
    QJsonObject root;
    root.insert(QStringLiteral("kind"), qs(std::string(kFormatPresetKind)));
    root.insert(QStringLiteral("version"), p.version);
    root.insert(QStringLiteral("name"), qs(p.name));
    root.insert(QStringLiteral("format"), qs(p.format_id));
    root.insert(QStringLiteral("backend"), qs(p.backend_id));
    root.insert(QStringLiteral("tech"), qs(p.tech_id));
    root.insert(QStringLiteral("bitdepth"), p.out_bitdepth);
    root.insert(QStringLiteral("params"), params_to_json(p.params));
    if (p.heif_profile.has_value()) {
        QJsonObject reserved;
        reserved.insert(qs(std::string(kFormatPresetHeifProfileReserved)),
                        static_cast<qint64>(*p.heif_profile));
        root.insert(QStringLiteral("reserved"), reserved);
    }
    if (p.adaptive_enabled || !p.adaptive_policy.empty()) {
        QJsonObject adaptive;
        adaptive.insert(QStringLiteral("enabled"), p.adaptive_enabled);
        if (!p.adaptive_policy.empty())
            adaptive.insert(QStringLiteral("policy"), qs(p.adaptive_policy));
        root.insert(QStringLiteral("adaptive"), adaptive);
    }
    return root;
}

std::string write_preset_json(const std::filesystem::path &file, const FormatPresetData &p) {
    if (file.empty())
        return "预设路径为空";
    if (const std::string derr = directory_target_error(file, "写入"); !derr.empty())
        return derr;
    if (!file.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        if (ec)
            return "无法创建目录 '" + display_path(file.parent_path()) + "'：" + ec.message();
    }
    const QByteArray bytes = QJsonDocument(preset_to_json(p)).toJson(QJsonDocument::Indented);
    std::string io_err;
    if (!write_atomic_file(
            file, std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
            &io_err))
        return "无法保存 '" + display_path(file) + "'：" + io_err;
    return {};
}

std::string save_validated(const std::filesystem::path &presets_root, const FormatPresetData &p,
                           std::filesystem::path *saved_path) {
    // M10-W3-T5：`_builtin-` 前缀 = 只读模板（内置/出厂预设）→ 拒绝覆盖（另存…可换新名）。
    if (is_builtin_format_preset_name(p.name))
        return "拒绝覆盖内置预设（" + p.name +
               "）：`_builtin-` 前缀是只读模板；请用「另存…」保存为新的预设";
    if (const std::string verr = validate_format_preset(p); !verr.empty())
        return verr;
    if (presets_root.empty())
        return "格式预设目录不可用（数据目录为空）；请检查程序是否可写或 %APPDATA%";
    std::error_code ec;
    std::filesystem::create_directories(presets_root, ec);
    if (ec)
        return "无法创建格式预设目录 '" + display_path(presets_root) + "'：" + ec.message();
    const std::filesystem::path dir = presets_root / p.format_id;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return "无法创建格式预设目录 '" + display_path(dir) + "'：" + ec.message();
    const std::filesystem::path file = dir / (p.name + ".json");
    if (const std::string werr = write_preset_json(file, p); !werr.empty())
        return werr;
    if (saved_path != nullptr)
        *saved_path = file;
    return {};
}

// —— 自适应策略 JSON AST 导入（§9.3；核心解析在 core/adaptive，这里只做 Qt JSON 映射）——
bool json_fail(AdaptiveError *err, std::string code, std::string message, std::string hint) {
    if (err != nullptr) {
        err->line = 0;
        err->col = 0;
        err->code = std::move(code);
        err->message = std::move(message);
        err->hint = std::move(hint);
    }
    return false;
}

bool json_int(const QJsonObject &o, const char *field, int64_t fallback, int64_t &out,
              AdaptiveError *err) {
    const QJsonValue v = o.value(QLatin1String(field));
    if (v.isUndefined()) {
        out = fallback;
        return true;
    }
    if (!v.isDouble() || v.toDouble() != std::floor(v.toDouble()))
        return json_fail(err, "json_field",
                         std::string("自适应策略 AST 字段 ") + field + " 必须是整数",
                         "见 docs/schemas/adaptive-policy.v1.schema.json。");
    out = static_cast<int64_t>(v.toDouble());
    return true;
}

bool json_double(const QJsonObject &o, const char *field, double fallback, double &out,
                 AdaptiveError *err) {
    const QJsonValue v = o.value(QLatin1String(field));
    if (v.isUndefined()) {
        out = fallback;
        return true;
    }
    if (!v.isDouble())
        return json_fail(err, "json_field",
                         std::string("自适应策略 AST 字段 ") + field + " 必须是数值",
                         "见 docs/schemas/adaptive-policy.v1.schema.json。");
    out = v.toDouble();
    return true;
}

bool parse_method_json(const QJsonValue &v, AdaptiveMethod &out, AdaptiveError *err) {
    if (v.isUndefined()) {
        out = AdaptiveMethod::Linear;
        return true;
    }
    if (!v.isString())
        return json_fail(err, "json_field", "action.method 必须是字符串",
                         "可取 linear / bisect / exp / greedy。");
    const std::string s = utf8(v.toString());
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
    return json_fail(err, "json_field", "action.method 非法：" + s,
                     "可取 linear / bisect / exp / greedy。");
}

bool parse_stages_json(const QJsonValue &v, std::vector<AdaptiveStage> &out, AdaptiveError *err) {
    if (!v.isArray())
        return json_fail(err, "json_field", "自适应策略 AST 缺少 stages 数组",
                         "stages 是必填数组（至少 1 个 stage）。");
    const QJsonArray stages = v.toArray();
    for (int i = 0; i < stages.size(); ++i) {
        const std::string at = "stages[" + std::to_string(i) + "]";
        if (!stages.at(i).isObject())
            return json_fail(err, "json_field", at + " 不是对象", "每个 stage 是 JSON 对象。");
        const QJsonObject so = stages.at(i).toObject();
        AdaptiveStage st;
        const QJsonValue id = so.value(QStringLiteral("id"));
        if (!id.isString() || utf8(id.toString()).empty())
            return json_fail(err, "json_field", at + ".id 必须是非空字符串",
                             "stage id 形如 quality-up（[a-z][a-z0-9_-]{0,31}）。");
        st.id = utf8(id.toString());
        const QJsonValue actions_v = so.value(QStringLiteral("actions"));
        if (!actions_v.isArray())
            return json_fail(err, "json_field", at + ".actions 必须是数组",
                             "每个 stage 至少 1 个 search/set 动作。");
        const QJsonArray actions = actions_v.toArray();
        for (int j = 0; j < actions.size(); ++j) {
            const std::string aat = at + ".actions[" + std::to_string(j) + "]";
            if (!actions.at(j).isObject())
                return json_fail(err, "json_field", aat + " 不是对象", "动作是 JSON 对象。");
            const QJsonObject ao = actions.at(j).toObject();
            AdaptiveAction a;
            a.line = 0;
            a.col = 0;
            const QJsonValue kind = ao.value(QStringLiteral("kind"));
            if (!kind.isString())
                return json_fail(err, "json_field", aat + ".kind 必须是字符串",
                                 "kind 取 search 或 set。");
            const std::string kind_s = utf8(kind.toString());
            const QJsonValue key = ao.value(QStringLiteral("key"));
            if (!key.isString() || utf8(key.toString()).empty())
                return json_fail(err, "json_field", aat + ".key 必须是非空字符串",
                                 "key = 参数键 / profile / chroma。");
            a.key = utf8(key.toString());
            if (kind_s == "search") {
                a.kind = AdaptiveActionKind::Search;
                const QJsonValue from = ao.value(QStringLiteral("from"));
                if (from.isString()) {
                    if (utf8(from.toString()) != "current")
                        return json_fail(err, "json_field", aat + ".from 只能是 \"current\" 或数值",
                                         "from 表示搜索起点；数值或 current。");
                    a.from.current = true;
                } else if (from.isDouble()) {
                    a.from.number = from.toDouble();
                } else {
                    return json_fail(err, "json_field", aat + ".from 缺少或类型不合法",
                                     "from 表示搜索起点；数值或 current。");
                }
                if (!json_double(ao, "to", 0.0, a.to, err))
                    return false;
                if (!ao.contains(QStringLiteral("to")))
                    return json_fail(err, "json_field", aat + ".to 缺失", "search 必须有 to。");
                if (!json_double(ao, "step", 0.0, a.step, err))
                    return false;
                if (!ao.contains(QStringLiteral("step")))
                    return json_fail(err, "json_field", aat + ".step 缺失", "search 必须有 step。");
                if (!parse_method_json(ao.value(QStringLiteral("method")), a.method, err))
                    return false;
                int64_t action_rounds = 0;
                if (!json_int(ao, "max_rounds", 0, action_rounds, err))
                    return false;
                a.max_rounds = static_cast<int>(action_rounds);
            } else if (kind_s == "set") {
                a.kind = AdaptiveActionKind::Set;
                const QJsonValue value = ao.value(QStringLiteral("value"));
                if (value.isBool()) {
                    a.value = ParamValue{value.toBool()};
                } else if (value.isDouble()) {
                    constexpr double kMaxExactInt = 9007199254740992.0;
                    const double d = value.toDouble();
                    a.value = (d == std::floor(d) && std::abs(d) <= kMaxExactInt)
                                  ? ParamValue{static_cast<int64_t>(d)}
                                  : ParamValue{d};
                } else if (value.isString()) {
                    a.value = ParamValue{utf8(value.toString())};
                } else {
                    return json_fail(err, "json_field", aat + ".value 缺失或不是标量",
                                     "value 是数字/布尔/字符串。");
                }
            } else {
                std::string msg = aat + ".kind 非法：";
                msg += kind_s;
                return json_fail(err, "json_field", msg, "kind 取 search 或 set。");
            }
            st.actions.push_back(std::move(a));
        }
        out.push_back(std::move(st));
    }
    return true;
}

} // namespace

// ===========================================================================
// 路径
// ===========================================================================
std::filesystem::path format_presets_root() {
    return platform::path_under(platform::presets_dir(), "formats");
}

std::filesystem::path format_preset_dir(std::string_view format_id) {
    if (!safe_component(format_id))
        return {};
    const std::filesystem::path root = format_presets_root();
    if (root.empty())
        return {};
    return root / std::string(format_id);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：format_id/name 由路径构造语义固定
std::filesystem::path format_preset_path(std::string_view format_id, std::string_view name) {
    if (!safe_component(name))
        return {};
    const std::filesystem::path dir = format_preset_dir(format_id);
    if (dir.empty())
        return {};
    return dir / (std::string(name) + ".json");
}

// ===========================================================================
// 列举
// ===========================================================================
std::vector<FormatPresetEntry> list_format_presets_in(const std::filesystem::path &dir) {
    std::vector<FormatPresetEntry> out;
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec))
        return out;
    std::filesystem::directory_iterator it(dir, ec);
    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec)
            break;
        const std::filesystem::path path = it->path();
        const std::string fname = path.filename().string();
        if (fname.empty() || fname[0] == '.')
            continue; // 不列隐藏文件
        if (!has_json_suffix(fname))
            continue;
        std::error_code fec;
        if (!it->is_regular_file(fec))
            continue;
        FormatPresetEntry entry;
        entry.path = path;
        FormatPresetData p;
        const std::string err = load_format_preset(path, p);
        if (err.empty()) {
            entry.name = p.name;
        } else {
            entry.name = path.stem().string();
            entry.broken = true;
            entry.error = err;
        }
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(), [](const FormatPresetEntry &a, const FormatPresetEntry &b) {
        const int c = QString::compare(qs(a.name), qs(b.name), Qt::CaseSensitive);
        if (c != 0)
            return c < 0;
        return a.path.string() < b.path.string();
    });
    return out;
}

std::vector<FormatPresetEntry> list_format_presets(std::string_view format_id) {
    return list_format_presets_in(format_preset_dir(format_id));
}

// ===========================================================================
// 保存 / 读取 / 删除
// ===========================================================================
std::string save_format_preset_to(const std::filesystem::path &presets_root,
                                  const FormatPresetData &p, std::filesystem::path *saved_path) {
    return save_validated(presets_root, p, saved_path);
}

std::string save_format_preset(const FormatPresetData &p, std::filesystem::path *saved_path) {
    return save_validated(format_presets_root(), p, saved_path);
}

std::string load_format_preset(const std::filesystem::path &file, FormatPresetData &out) {
    if (file.empty())
        return "格式预设路径为空";
    if (const std::string derr = directory_target_error(file, "读取"); !derr.empty())
        return derr;
    std::string bytes;
    if (const std::string rerr = read_file_bytes(file, bytes); !rerr.empty())
        return rerr;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())), &perr);
    if (perr.error != QJsonParseError::NoError)
        return "JSON 解析失败 '" + display_path(file) + "'：" + utf8(perr.errorString());
    if (!doc.isObject())
        return "无效的格式预设 '" + display_path(file) + "'：根不是 JSON 对象";
    FormatPresetData p;
    const std::string perr2 = parse_preset_object(doc.object(), p);
    if (!perr2.empty())
        return "无效的格式预设 '" + display_path(file) + "'：" + perr2;
    if (const std::string verr = validate_format_preset(p); !verr.empty())
        return "无效的格式预设 '" + display_path(file) + "'：" + verr;
    out = std::move(p);
    return {};
}

std::string delete_format_preset(const std::filesystem::path &file) {
    if (file.empty())
        return "拒绝删除：删除路径为空";
    // M10-W3-T5：内置（出厂）预设只读 —— 拒绝删除（另存…/导出可保存副本）。
    if (is_builtin_format_preset_path(file))
        return "拒绝删除内置预设（" + display_path(file) +
               "）：`_builtin-` 前缀是只读模板；请用「另存…」保存副本后再改";
    if (!file.is_absolute())
        return "拒绝删除相对路径的格式预设（" + display_path(file) + "）";
    std::error_code ec;
    if (std::filesystem::is_directory(file, ec))
        return "拒绝删除：目标是目录（" + display_path(file) + "）";
    if (!std::filesystem::exists(file, ec))
        return "格式预设文件不存在（" + display_path(file) + "）";
    std::filesystem::remove(file, ec);
    if (ec)
        return "删除格式预设失败（" + display_path(file) + "）：" + ec.message();
    return {};
}

// ===========================================================================
// 导入 / 导出
// ===========================================================================
std::string import_format_preset(const std::filesystem::path &source, std::string_view name,
                                 std::filesystem::path *saved_path) {
    return import_format_preset_to(format_presets_root(), source, name, saved_path);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：presets_root/source 由导入语义固定
std::string import_format_preset_to(const std::filesystem::path &presets_root,
                                    const std::filesystem::path &source, std::string_view name,
                                    std::filesystem::path *saved_path) {
    FormatPresetData p;
    if (const std::string lerr = load_format_preset(source, p); !lerr.empty())
        return "导入失败：" + lerr;
    if (!name.empty())
        p.name = std::string(name);
    return save_validated(presets_root, p, saved_path);
}

std::string export_format_preset(const FormatPresetData &p, const std::filesystem::path &dest) {
    if (const std::string verr = validate_format_preset(p); !verr.empty())
        return verr;
    if (dest.empty())
        return "导出路径为空";
    std::error_code ec;
    if (std::filesystem::is_directory(dest, ec))
        return "导出路径是目录（" + display_path(dest) + "）";
    return write_preset_json(dest, p);
}

// ===========================================================================
// 自适应策略 JSON AST 导入（§9.3）
// ===========================================================================
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：json 文本/format_id 由解析语义固定
bool parse_adaptive_policy_json(std::string_view json, std::string_view format_id,
                                AdaptivePolicy &out, AdaptiveError *err) {
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray(json.data(), static_cast<qsizetype>(json.size())), &perr);
    if (perr.error != QJsonParseError::NoError)
        return json_fail(err, "json_syntax",
                         "自适应策略 JSON 解析失败：" + utf8(perr.errorString()),
                         "见 docs/schemas/adaptive-policy.v1.schema.json 的 AST 结构。");
    if (!doc.isObject())
        return json_fail(err, "json_field", "自适应策略 JSON 根必须是对象",
                         "根对象 = AdaptivePolicy{version,target,max_rounds,output,...,stages}。");
    const QJsonObject root = doc.object();
    AdaptivePolicy p;
    int64_t ivalue = 0;
    if (!json_int(root, "version", kAdaptivePolicyVersion, ivalue, err))
        return false;
    p.version = static_cast<int>(ivalue);
    if (!json_double(root, "target", p.target, p.target, err))
        return false;
    if (!json_int(root, "max_rounds", p.max_rounds, ivalue, err))
        return false;
    p.max_rounds = static_cast<int>(ivalue);
    if (!json_double(root, "plateau", p.plateau, p.plateau, err))
        return false;
    const QJsonValue output = root.value(QStringLiteral("output"));
    if (!output.isUndefined()) {
        if (!output.isString())
            return json_fail(err, "json_field", "output 必须是字符串", "取 last 或 best。");
        const std::string s = utf8(output.toString());
        if (s == "last") {
            p.output = AdaptiveOutput::Last;
        } else if (s == "best") {
            p.output = AdaptiveOutput::Best;
        } else {
            return json_fail(err, "json_field", "output 非法：" + s, "取 last 或 best。");
        }
    }
    const QJsonValue omu = root.value(QStringLiteral("on_metric_unavailable"));
    if (!omu.isUndefined()) {
        if (!omu.isString())
            return json_fail(err, "json_field", "on_metric_unavailable 必须是字符串",
                             "取 stop 或 fail。");
        const std::string s = utf8(omu.toString());
        if (s == "stop") {
            p.on_metric_unavailable = AdaptiveMetricUnavailable::Stop;
        } else if (s == "fail") {
            p.on_metric_unavailable = AdaptiveMetricUnavailable::Fail;
        } else {
            return json_fail(err, "json_field", "on_metric_unavailable 非法：" + s,
                             "取 stop 或 fail。");
        }
    }
    if (!parse_stages_json(root.value(QStringLiteral("stages")), p.stages, err))
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

bool load_adaptive_policy_json(const std::filesystem::path &file, std::string_view format_id,
                               AdaptivePolicy &out, AdaptiveError *err) {
    if (const std::string derr = directory_target_error(file, "读取"); !derr.empty()) {
        json_fail(err, "json_field", derr, "选择一个 JSON 文件。");
        return false;
    }
    std::string bytes;
    if (const std::string rerr = read_file_bytes(file, bytes); !rerr.empty()) {
        json_fail(err, "json_field", rerr, "检查文件是否存在/可读。");
        return false;
    }
    return parse_adaptive_policy_json(bytes, format_id, out, err);
}

// ===========================================================================
// 内置（出厂）预设：只读判定 + 幂等 seed（M10 / W3-T5；设计 §8.3 / §9.7）
// ===========================================================================
bool is_builtin_format_preset_path(const std::filesystem::path &file) {
    return has_builtin_prefix(file.filename().string());
}

bool is_builtin_format_preset_name(std::string_view name) { return has_builtin_prefix(name); }

FormatPresetSeedReport seed_builtin_format_presets_to(const std::filesystem::path &presets_root) {
    FormatPresetSeedReport report;
    const std::string prefix(kFormatPresetBuiltinPrefix);
    for (const FormatPresetData &p : builtin_format_presets()) {
        if (presets_root.empty()) {
            ++report.failed;
            report.errors.push_back("内置预设「" + p.name +
                                    "」未 seed：格式预设目录不可用（数据目录为空）");
            continue;
        }
        if (!safe_component(p.format_id)) {
            ++report.failed;
            report.errors.push_back("内置预设「" + p.name + "」未 seed：格式 id 非法（" +
                                    p.format_id + "）");
            continue;
        }
        const std::filesystem::path dir = presets_root / p.format_id;
        const std::filesystem::path file = dir / (prefix + p.format_id + ".json");
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
            ++report.skipped; // 幂等：已存在（上次已 seed / 用户改过）→ 一个字节都不动
            continue;
        }
        // seed 前走与保存同一条校验链：不合法就不写（不制造坏文件、不静默）。
        const std::string verr = validate_format_preset(p);
        if (!verr.empty()) {
            ++report.failed;
            report.errors.push_back("内置预设「" + p.name + "」校验失败：" + verr);
            continue;
        }
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            ++report.failed;
            report.errors.push_back("内置预设「" + p.name + "」未 seed：无法创建目录 '" +
                                    display_path(dir) + "'：" + ec.message());
            continue;
        }
        const std::string werr = write_preset_json(file, p);
        if (!werr.empty()) {
            ++report.failed;
            report.errors.push_back(werr);
            continue;
        }
        ++report.written;
    }
    return report;
}

FormatPresetSeedReport seed_builtin_format_presets() {
    return seed_builtin_format_presets_to(format_presets_root());
}

} // namespace pp::ui
