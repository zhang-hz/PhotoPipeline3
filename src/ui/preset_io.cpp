// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — preset JSON I/O（M1-T2；QtCore-only，schema 见 docs/m1-tasks.md §3.12）
//
// M2-T14：本文件的路径层按**原生字节串**处理（见下方"路径层"注释）；JSON 文本层仍为 UTF-8。
#include "ui/preset_io.h"

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

#include "core/colormanager.h"  // pp::to_string/parse_color_target（color target 唯一映射源）
#include "core/params.h"

namespace pp::ui {
namespace {

// —— 文本层（JSON 内容）：std::string 约定为 UTF-8 ⇄ QString ——
// 只用于预设内容（name/format/params/rules 值），不用于路径。
QString qs(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }

// —— 路径层（M2-T14：#23 销账）——
// 背景（实测，Qt 6.8/Linux）：QFile::encodeName == QString::toLocal8Bit == **UTF-8**，与 locale
// 无关（LC_ALL=C 亦然）；解码则是严格 UTF-8，非法字节 → U+FFFD，QDir 读目录项时甚至直接丢弃
// 非法字节。因此 "filesystem::path → QString → filesystem::path" 对含非 UTF-8 字节的路径必然
// 失真，而原 TODO 建议的 fromLocal8Bit/toLocal8Bit 在 Qt6/Linux 上只是 fromStdString/toStdString
// 的别名，并不改变行为（见 tests/unit/test_presets.cpp 的 locale-safe-path 用例）。
// 落地口径：路径字节**不进 QString** —— 保存/载入/扫描直接用 std::filesystem + std::fstream 的
// 原生字节路径（Linux 上 path::c_str() 即文件名字节串，POSIX API 原样传递）；QString 仅用于
// JSON 文本与 UI 显示。UI 侧需要 QString 路径时，Qt6 的既定编码是 UTF-8（QFile::encodeName），
// 这是 Qt 自身的能力边界，不在本文件内绕过。
bool is_valid_utf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
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
            return false;  // 孤立尾字节 / 5+ 字节序列
        }
        if (i + extra >= s.size()) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        // overlong / 代理区 / 超范围一律判非法
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += extra + 1;
    }
    return true;
}

// 错误信息里的路径：合法 UTF-8 原样返回（与旧输出逐字节一致）；否则把非 ASCII 字节转义成
// \xNN —— 避免 UI 侧 QString::fromStdString 把非法字节静默变成 U+FFFD（丢字节）。
std::string display_path(const std::filesystem::path& p) {
    const std::string s = p.string();
    if (is_valid_utf8(s)) return s;
    std::string out;
    out.reserve(s.size() + 8);
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
    if (errno == 0) return "I/O error";
    return std::error_code(errno, std::generic_category()).message();
}

// 读整个文件。用 FILE* 而非 std::ifstream：libstdc++ 的 basic_filebuf::underflow 在 EISDIR/EIO
// 这类真实读错误上会抛 std::ios_base::failure（复现：ifstream 打开目录 → terminate/SIGABRT），
// 本层按返回值报错、不抛异常。目录由调用方先行拦截（与 QFile 语义一致）。
std::string read_file_bytes(const std::filesystem::path& file, std::string& out) {
#if defined(_WIN32)
    // M3：fs::path::c_str() 在 Windows 为 wchar_t*；原生宽字符打开，正确性不依赖
    // 进程 ANSI 代码页（非 ASCII 预设名在任意宿主/测试二进制下一致）。POSIX 保持字节路径。
    std::FILE* f = ::_wfopen(file.c_str(), L"rb");
#else
    std::FILE* f = std::fopen(file.c_str(), "rb");
#endif
    if (f == nullptr) return "cannot open '" + display_path(file) + "' for reading";
    std::string bytes;
    char buf[64 * 1024];
    std::string err;
    for (;;) {
        errno = 0;
        const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n > 0) bytes.append(buf, n);
        if (n == sizeof(buf)) continue;
        if (std::ferror(f) != 0) err = "cannot read '" + display_path(file) + "': " + errno_text();
        break;
    }
    std::fclose(f);
    if (!err.empty()) return err;
    out = std::move(bytes);
    return {};
}

// 目标为目录 → 与 QFile/QSaveFile 一样立即失败（也避免留下 <dir>.tmp 这种兄弟文件）
std::string directory_target_error(const std::filesystem::path& file, const char* action) {
    std::error_code dir_ec;
    if (!std::filesystem::is_directory(file, dir_ec)) return {};
    return std::string("cannot open '") + display_path(file) + "' for " + action + ": " +
           std::make_error_code(std::errc::is_a_directory).message();
}

// 旧 QDir name filter "*.json" 的等价判定（Qt 的 wildcard 匹配默认大小写不敏感）。
bool has_json_suffix(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || name.size() - dot != 5) return false;
    constexpr const char* kExt = ".json";
    for (std::size_t i = 0; i < 5; ++i) {
        char c = name[dot + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kExt[i]) return false;
    }
    return true;
}

// —— ConflictPolicy 与 JSON 字符串的映射（§3.6 / §3.15）——
// color target 的映射**不再在本文件重复**：统一走 core 的 pp::to_string / pp::parse_color_target
// （src/core/colormanager.h；M2-T7b 销账 #24 的重复字面量）。
const char* conflict_name(ConflictPolicy c) {
    switch (c) {
        case ConflictPolicy::Skip: return "skip";
        case ConflictPolicy::Overwrite: return "overwrite";
        case ConflictPolicy::Rename: return "rename";
    }
    return "rename";
}

bool parse_conflict_name(const std::string& s, ConflictPolicy& out) {
    if (s == "skip") { out = ConflictPolicy::Skip; return true; }
    if (s == "overwrite") { out = ConflictPolicy::Overwrite; return true; }
    if (s == "rename") { out = ConflictPolicy::Rename; return true; }
    return false;
}

// —— ParamValue ⇄ JSON ——
QJsonValue to_json(const ParamValue& v) {
    if (const bool* b = std::get_if<bool>(&v)) return QJsonValue(*b);
    if (const int64_t* i = std::get_if<int64_t>(&v)) return QJsonValue(static_cast<qint64>(*i));
    if (const double* d = std::get_if<double>(&v)) return QJsonValue(*d);
    if (const std::string* s = std::get_if<std::string>(&v)) return QJsonValue(qs(*s));
    return QJsonValue(QJsonValue::Null);
}

// 表内查找参数定义（优先选中技术；tech_id 空=首选技术）；找不到返回 nullptr
const ParamDef* lookup_param_def(const std::string& format_id, const std::string& backend_id,
                                 const std::string& tech_id, const std::string& key) {
    const FormatDef* f = find_format(format_id);
    if (!f) return nullptr;
    const BackendDef* b = find_backend(*f, backend_id);
    if (!b) return nullptr;
    if (const TechDef* t = find_tech(*b, tech_id)) {
        for (const ParamDef& p : t->params)
            if (p.key == key) return &p;
    }
    for (const TechDef& t : b->techs)
        for (const ParamDef& p : t.params)
            if (p.key == key) return &p;
    return nullptr;
}

// 按表内类型还原 variant（保证 preset 往返类型精确）；无 schema 信息时按 JSON 类型推断
ParamValue param_from_json(const QJsonValue& v, const ParamDef* def) {
    if (def) {
        switch (def->type) {
            case ParamType::Int:
                return ParamValue{static_cast<int64_t>(v.toInteger(0))};
            case ParamType::Float:
                return ParamValue{v.toDouble(0.0)};
            case ParamType::Bool:
                return ParamValue{v.toBool(false)};
            case ParamType::Enum:
                if (!def->choices.empty()) {
                    const ParamValue& proto = def->choices.front().second;
                    if (std::holds_alternative<int64_t>(proto))
                        return ParamValue{static_cast<int64_t>(v.toInteger(0))};
                    if (std::holds_alternative<std::string>(proto))
                        return ParamValue{utf8(v.toString())};
                    if (std::holds_alternative<bool>(proto)) return ParamValue{v.toBool(false)};
                }
                break;
        }
    }
    switch (v.type()) {
        case QJsonValue::Bool: return ParamValue{v.toBool()};
        case QJsonValue::String: return ParamValue{utf8(v.toString())};
        case QJsonValue::Double: {
            const double d = v.toDouble();
            if (d == std::floor(d) && std::abs(d) <= 9.007199254740992e15)
                return ParamValue{static_cast<int64_t>(d)};
            return ParamValue{d};
        }
        default: return ParamValue{};
    }
}

// —— TagEdit ——
QJsonObject edit_to_json(const TagEdit& e) {
    QJsonObject o;
    o.insert(QStringLiteral("key"), qs(e.key));
    o.insert(QStringLiteral("value"), e.value ? QJsonValue(qs(*e.value)) : QJsonValue(QJsonValue::Null));
    if (e.remove) o.insert(QStringLiteral("remove"), true);  // 仅在 true 时写出（示例形态）
    return o;
}

std::vector<TagEdit> edits_from_json(const QJsonValue& v) {
    std::vector<TagEdit> out;
    if (!v.isArray()) return out;
    for (const QJsonValue& e : v.toArray()) {
        if (!e.isObject()) continue;
        const QJsonObject o = e.toObject();
        TagEdit te;
        te.key = utf8(o.value(QStringLiteral("key")).toString());
        if (te.key.empty()) continue;
        const QJsonValue value = o.value(QStringLiteral("value"));
        if (value.isString()) te.value = utf8(value.toString());
        te.remove = o.value(QStringLiteral("remove")).toBool(false);
        out.push_back(std::move(te));
    }
    return out;
}

// —— BatchRules（§3.12 rules 对象）——
QJsonObject rules_to_json(const BatchRules& r) {
    QJsonObject o;
    if (r.time_shift) {
        const TimeShift& t = *r.time_shift;
        QJsonObject ts;
        ts.insert(QStringLiteral("mode"),
                  t.mode == TimeShift::Mode::TimezoneSemantic ? QStringLiteral("timezone")
                                                              : QStringLiteral("delta"));
        ts.insert(QStringLiteral("years"), t.years);
        ts.insert(QStringLiteral("months"), t.months);
        ts.insert(QStringLiteral("days"), t.days);
        ts.insert(QStringLiteral("hours"), t.hours);
        ts.insert(QStringLiteral("minutes"), t.minutes);
        ts.insert(QStringLiteral("seconds"), t.seconds);
        // 示例为 delta 实例故未列；时区语义模式往返需要这两项
        ts.insert(QStringLiteral("from_offset_min"), t.from_offset_min);
        ts.insert(QStringLiteral("to_offset_min"), t.to_offset_min);
        o.insert(QStringLiteral("time_shift"), ts);
    }
    if (r.gps) {
        const GpsData& g = *r.gps;
        QJsonObject go;
        go.insert(QStringLiteral("lat"), g.lat);
        go.insert(QStringLiteral("lon"), g.lon);
        go.insert(QStringLiteral("altitude"),
                  g.altitude ? QJsonValue(*g.altitude) : QJsonValue(QJsonValue::Null));
        go.insert(QStringLiteral("direction"),
                  g.direction ? QJsonValue(*g.direction) : QJsonValue(QJsonValue::Null));
        go.insert(QStringLiteral("timestamp"),
                  g.timestamp ? QJsonValue(qs(*g.timestamp)) : QJsonValue(QJsonValue::Null));
        o.insert(QStringLiteral("gps"), go);
    }
    o.insert(QStringLiteral("gps_clear"), r.gps_clear);
    QJsonArray exif;
    for (const TagEdit& e : r.exif_edits) exif.append(edit_to_json(e));
    o.insert(QStringLiteral("exif_edits"), exif);
    QJsonArray xmp;
    for (const TagEdit& e : r.xmp_edits) xmp.append(edit_to_json(e));
    o.insert(QStringLiteral("xmp_edits"), xmp);
    o.insert(QStringLiteral("strip_privacy"), r.strip_privacy);
    o.insert(QStringLiteral("sync_mtime"), r.sync_mtime);
    return o;
}

bool rules_from_json(const QJsonObject& o, BatchRules& r, std::string& err) {
    const QJsonValue ts_v = o.value(QStringLiteral("time_shift"));
    if (!ts_v.isUndefined() && !ts_v.isNull()) {
        if (!ts_v.isObject()) { err = "rules.time_shift is not an object"; return false; }
        const QJsonObject ts = ts_v.toObject();
        TimeShift t;
        const std::string mode = utf8(ts.value(QStringLiteral("mode")).toString(QStringLiteral("delta")));
        if (mode == "timezone" || mode == "timezone_semantic")
            t.mode = TimeShift::Mode::TimezoneSemantic;
        else if (mode == "delta")
            t.mode = TimeShift::Mode::Delta;
        else { err = "rules.time_shift.mode '" + mode + "' is not valid"; return false; }
        t.years = static_cast<int>(ts.value(QStringLiteral("years")).toInteger(0));
        t.months = static_cast<int>(ts.value(QStringLiteral("months")).toInteger(0));
        t.days = static_cast<int>(ts.value(QStringLiteral("days")).toInteger(0));
        t.hours = static_cast<int>(ts.value(QStringLiteral("hours")).toInteger(0));
        t.minutes = static_cast<int>(ts.value(QStringLiteral("minutes")).toInteger(0));
        t.seconds = static_cast<int>(ts.value(QStringLiteral("seconds")).toInteger(0));
        t.from_offset_min = static_cast<int>(ts.value(QStringLiteral("from_offset_min")).toInteger(0));
        t.to_offset_min = static_cast<int>(ts.value(QStringLiteral("to_offset_min")).toInteger(0));
        r.time_shift = t;
    }
    const QJsonValue gps_v = o.value(QStringLiteral("gps"));
    if (!gps_v.isUndefined() && !gps_v.isNull()) {
        if (!gps_v.isObject()) { err = "rules.gps is not an object"; return false; }
        const QJsonObject go = gps_v.toObject();
        GpsData g;
        g.lat = go.value(QStringLiteral("lat")).toDouble(0.0);
        g.lon = go.value(QStringLiteral("lon")).toDouble(0.0);
        const QJsonValue alt = go.value(QStringLiteral("altitude"));
        if (alt.isDouble()) g.altitude = alt.toDouble();
        const QJsonValue dir = go.value(QStringLiteral("direction"));
        if (dir.isDouble()) g.direction = dir.toDouble();
        const QJsonValue ts = go.value(QStringLiteral("timestamp"));
        if (ts.isString()) g.timestamp = utf8(ts.toString());
        r.gps = g;
    }
    r.gps_clear = o.value(QStringLiteral("gps_clear")).toBool(false);
    r.exif_edits = edits_from_json(o.value(QStringLiteral("exif_edits")));
    r.xmp_edits = edits_from_json(o.value(QStringLiteral("xmp_edits")));
    r.strip_privacy = o.value(QStringLiteral("strip_privacy")).toBool(false);
    r.sync_mtime = o.value(QStringLiteral("sync_mtime")).toBool(false);
    return true;
}

}  // namespace

std::string save_preset(const std::filesystem::path& file, const PresetData& p) {
    if (file.empty()) return "preset path is empty";
    if (const std::string derr = directory_target_error(file, "writing"); !derr.empty()) return derr;
    std::error_code ec;
    const std::filesystem::path parent = file.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return "cannot create directory '" + display_path(parent) + "': " + ec.message();
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), p.version);
    root.insert(QStringLiteral("name"), qs(p.name));
    root.insert(QStringLiteral("format"), qs(p.format_id));
    root.insert(QStringLiteral("backend"), qs(p.backend_id));
    root.insert(QStringLiteral("tech"), qs(p.tech_id));
    root.insert(QStringLiteral("lossless"), p.lossless);
    root.insert(QStringLiteral("bitdepth"), p.out_bitdepth);
    root.insert(QStringLiteral("color_target"), qs(pp::to_string(p.color_target)));
    root.insert(QStringLiteral("conflict"), QString::fromLatin1(conflict_name(p.conflict)));

    QJsonObject params;
    for (const auto& [key, value] : p.params) {
        if (key.rfind("__", 0) == 0) continue;  // 保留键（__lossless）不落盘（顶层 lossless 表达）
        params.insert(qs(key), to_json(value));
    }
    root.insert(QStringLiteral("params"), params);
    root.insert(QStringLiteral("rules"), rules_to_json(p.rules));

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // 原子写：先写同目录 <file>.tmp 再 rename 覆盖（与 core/settings.cpp 的 INI 原子写同形）。
    // 不用 QSaveFile/QFile：其文件名为 QString，Qt6/Linux 固定按 UTF-8 编码，无法表达非 UTF-8
    // 字节路径（见文件头"路径层"注释）。
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    std::string write_err;
    {
        errno = 0;
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            write_err = "cannot open '" + display_path(file) + "' for writing: " + errno_text();
        } else {
            out.write(bytes.constData(), static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out) write_err = "short write to '" + display_path(file) + "': " + errno_text();
        }
    }
    if (!write_err.empty()) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp, rm_ec);
        return write_err;
    }
    // 目标已存在 → 沿用其权限位（QSaveFile::commit 的既有语义；不随 umask 漂移）
    {
        std::error_code st_ec;
        const std::filesystem::file_status st = std::filesystem::status(file, st_ec);
        if (!st_ec && std::filesystem::exists(st)) {
            std::error_code pm_ec;
            std::filesystem::permissions(tmp, st.permissions(),
                                         std::filesystem::perm_options::replace, pm_ec);
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp, rm_ec);
        return "cannot commit '" + display_path(file) + "': " + ec.message();
    }
    return {};
}

std::string load_preset(const std::filesystem::path& file, PresetData& out) {
    if (const std::string derr = directory_target_error(file, "reading"); !derr.empty()) return derr;
    std::string bytes;
    if (const std::string rerr = read_file_bytes(file, bytes); !rerr.empty()) return rerr;

    QJsonParseError perr{};
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size())), &perr);
    if (perr.error != QJsonParseError::NoError)
        return "invalid JSON in '" + display_path(file) + "': " + utf8(perr.errorString());
    if (!doc.isObject()) return "invalid preset '" + display_path(file) + "': root is not an object";

    const QJsonObject root = doc.object();
    PresetData p;  // 逐字段解析；失败时不污染 out
    p.version = static_cast<int>(root.value(QStringLiteral("version")).toInteger(1));
    p.name = utf8(root.value(QStringLiteral("name")).toString());
    p.format_id = utf8(root.value(QStringLiteral("format")).toString());
    p.backend_id = utf8(root.value(QStringLiteral("backend")).toString());
    p.tech_id = utf8(root.value(QStringLiteral("tech")).toString());
    p.lossless = root.value(QStringLiteral("lossless")).toBool(false);
    p.out_bitdepth = static_cast<int>(root.value(QStringLiteral("bitdepth")).toInteger(8));

    // 缺省 color_target = keep（取值同样经 core 映射取得，本文件不持有该字面量）
    const std::string color = utf8(root.value(QStringLiteral("color_target"))
                                       .toString(qs(pp::to_string(ColorTarget::KeepOriginal))));
    if (!pp::parse_color_target(color, p.color_target))
        return "invalid color_target '" + color + "' in '" + display_path(file) + "'";
    const std::string conflict = utf8(root.value(QStringLiteral("conflict")).toString(QStringLiteral("rename")));
    if (!parse_conflict_name(conflict, p.conflict))
        return "invalid conflict '" + conflict + "' in '" + display_path(file) + "'";

    const QJsonValue params_v = root.value(QStringLiteral("params"));
    if (!params_v.isUndefined() && !params_v.isNull()) {
        if (!params_v.isObject()) return "invalid preset '" + display_path(file) + "': params is not an object";
        const QJsonObject params = params_v.toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            const std::string key = utf8(it.key());
            if (key.rfind("__", 0) == 0) continue;  // 保留键由顶层字段重建
            p.params[key] = param_from_json(it.value(), lookup_param_def(p.format_id, p.backend_id, p.tech_id, key));
        }
    }

    const QJsonValue rules_v = root.value(QStringLiteral("rules"));
    if (!rules_v.isUndefined() && !rules_v.isNull()) {
        if (!rules_v.isObject()) return "invalid preset '" + display_path(file) + "': rules is not an object";
        std::string rerr;
        if (!rules_from_json(rules_v.toObject(), p.rules, rerr))
            return "invalid preset '" + display_path(file) + "': " + rerr;
    }

    out = std::move(p);
    return {};
}

std::vector<std::pair<std::filesystem::path, std::string>>
list_presets(const std::filesystem::path& dir) {
    std::vector<std::pair<std::filesystem::path, std::string>> out;
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return out;  // 旧 QDir::exists() 语义

    // 旧 QDir::entryInfoList("*.json", Files|Readable, NoSort) 的字节版：目录项名称保持原始字节
    // （QDir 会把非法 UTF-8 字节丢弃/替换，故不能再用 QDir）。
    std::filesystem::directory_iterator it(dir, ec);
    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;  // 不可读 → 返回已读条目
        const std::filesystem::path path = it->path();
        const std::string name = path.filename().string();
        if (name.empty() || name[0] == '.') continue;  // QDir::Hidden 未开 → 不列隐藏文件
        if (!has_json_suffix(name)) continue;          // name filter "*.json"（大小写不敏感）
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;       // QDir::Files
        {
            std::ifstream probe(path, std::ios::binary);  // QDir::Readable
            if (!probe) continue;
        }
        PresetData p;
        const std::string err = load_preset(path, p);
        // 读不出/无效 JSON 的文件仍列出（名取文件名主干），便于 UI 展示与删除
        out.emplace_back(path, err.empty() ? p.name : path.stem().string());
    }
    std::sort(out.begin(), out.end(),
              [](const std::pair<std::filesystem::path, std::string>& a,
                 const std::pair<std::filesystem::path, std::string>& b) {
                  const int c = QString::compare(qs(a.second), qs(b.second), Qt::CaseSensitive);
                  if (c != 0) return c < 0;
                  return a.first.string() < b.first.string();
              });
    return out;
}

}  // namespace pp::ui
