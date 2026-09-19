// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — preset JSON I/O（M1-T2；QtCore-only，schema 见 docs/m1-tasks.md §3.12）
#include "ui/preset_io.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/params.h"

namespace pp::ui {
namespace {

// —— 路径 / 字符串 ——
// TODO(M2): 非 UTF-8 locale 的路径（Linux 上 std::filesystem::path 为字节串）应改用
// QFile::encodeName/decodeName 往返，当前按 UTF-8 解释（本项目路径约定为 UTF-8）。
QString qs(const std::string& s) { return QString::fromStdString(s); }
QString qs(const std::filesystem::path& p) { return QString::fromStdString(p.string()); }
std::string utf8(const QString& s) { return s.toStdString(); }

// —— ColorTarget / ConflictPolicy 与 JSON 字符串的映射（§3.6 / §3.15）——
// TODO(M2): T4 的 pp::to_string/parse_color_target 落库后改用之（避免两处字面量）
const char* color_target_name(ColorTarget t) {
    switch (t) {
        case ColorTarget::KeepOriginal: return "keep";
        case ColorTarget::SRGB: return "srgb";
        case ColorTarget::DisplayP3: return "p3";
        case ColorTarget::AdobeRGB: return "adobergb";
    }
    return "keep";
}

bool parse_color_target_name(const std::string& s, ColorTarget& out) {
    if (s == "keep") { out = ColorTarget::KeepOriginal; return true; }
    if (s == "srgb") { out = ColorTarget::SRGB; return true; }
    if (s == "p3") { out = ColorTarget::DisplayP3; return true; }
    if (s == "adobergb") { out = ColorTarget::AdobeRGB; return true; }
    return false;
}

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
    std::error_code ec;
    const std::filesystem::path parent = file.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return "cannot create directory '" + parent.string() + "': " + ec.message();
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), p.version);
    root.insert(QStringLiteral("name"), qs(p.name));
    root.insert(QStringLiteral("format"), qs(p.format_id));
    root.insert(QStringLiteral("backend"), qs(p.backend_id));
    root.insert(QStringLiteral("tech"), qs(p.tech_id));
    root.insert(QStringLiteral("lossless"), p.lossless);
    root.insert(QStringLiteral("bitdepth"), p.out_bitdepth);
    root.insert(QStringLiteral("color_target"), QString::fromLatin1(color_target_name(p.color_target)));
    root.insert(QStringLiteral("conflict"), QString::fromLatin1(conflict_name(p.conflict)));

    QJsonObject params;
    for (const auto& [key, value] : p.params) {
        if (key.rfind("__", 0) == 0) continue;  // 保留键（__lossless）不落盘（顶层 lossless 表达）
        params.insert(qs(key), to_json(value));
    }
    root.insert(QStringLiteral("params"), params);
    root.insert(QStringLiteral("rules"), rules_to_json(p.rules));

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile out(qs(file));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return "cannot open '" + file.string() + "' for writing: " + utf8(out.errorString());
    if (out.write(bytes) != bytes.size()) {
        out.cancelWriting();
        return "short write to '" + file.string() + "': " + utf8(out.errorString());
    }
    if (!out.commit()) return "cannot commit '" + file.string() + "': " + utf8(out.errorString());
    return {};
}

std::string load_preset(const std::filesystem::path& file, PresetData& out) {
    QFile in(qs(file));
    if (!in.open(QIODevice::ReadOnly)) return "cannot open '" + file.string() + "' for reading";
    const QByteArray bytes = in.readAll();

    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError)
        return "invalid JSON in '" + file.string() + "': " + utf8(perr.errorString());
    if (!doc.isObject()) return "invalid preset '" + file.string() + "': root is not an object";

    const QJsonObject root = doc.object();
    PresetData p;  // 逐字段解析；失败时不污染 out
    p.version = static_cast<int>(root.value(QStringLiteral("version")).toInteger(1));
    p.name = utf8(root.value(QStringLiteral("name")).toString());
    p.format_id = utf8(root.value(QStringLiteral("format")).toString());
    p.backend_id = utf8(root.value(QStringLiteral("backend")).toString());
    p.tech_id = utf8(root.value(QStringLiteral("tech")).toString());
    p.lossless = root.value(QStringLiteral("lossless")).toBool(false);
    p.out_bitdepth = static_cast<int>(root.value(QStringLiteral("bitdepth")).toInteger(8));

    const std::string color = utf8(root.value(QStringLiteral("color_target")).toString(QStringLiteral("keep")));
    if (!parse_color_target_name(color, p.color_target))
        return "invalid color_target '" + color + "' in '" + file.string() + "'";
    const std::string conflict = utf8(root.value(QStringLiteral("conflict")).toString(QStringLiteral("rename")));
    if (!parse_conflict_name(conflict, p.conflict))
        return "invalid conflict '" + conflict + "' in '" + file.string() + "'";

    const QJsonValue params_v = root.value(QStringLiteral("params"));
    if (!params_v.isUndefined() && !params_v.isNull()) {
        if (!params_v.isObject()) return "invalid preset '" + file.string() + "': params is not an object";
        const QJsonObject params = params_v.toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            const std::string key = utf8(it.key());
            if (key.rfind("__", 0) == 0) continue;  // 保留键由顶层字段重建
            p.params[key] = param_from_json(it.value(), lookup_param_def(p.format_id, p.backend_id, p.tech_id, key));
        }
    }

    const QJsonValue rules_v = root.value(QStringLiteral("rules"));
    if (!rules_v.isUndefined() && !rules_v.isNull()) {
        if (!rules_v.isObject()) return "invalid preset '" + file.string() + "': rules is not an object";
        std::string rerr;
        if (!rules_from_json(rules_v.toObject(), p.rules, rerr))
            return "invalid preset '" + file.string() + "': " + rerr;
    }

    out = std::move(p);
    return {};
}

std::vector<std::pair<std::filesystem::path, std::string>>
list_presets(const std::filesystem::path& dir) {
    std::vector<std::pair<std::filesystem::path, std::string>> out;
    QDir d(qs(dir));
    if (!d.exists()) return out;
    const QFileInfoList files = d.entryInfoList(QStringList{QStringLiteral("*.json")},
                                                QDir::Files | QDir::Readable, QDir::NoSort);
    out.reserve(static_cast<std::size_t>(files.size()));
    for (const QFileInfo& fi : files) {
        const std::filesystem::path path = dir / utf8(fi.fileName());
        PresetData p;
        const std::string err = load_preset(path, p);
        // 读不出/无效 JSON 的文件仍列出（名取文件名主干），便于 UI 展示与删除
        out.emplace_back(path, err.empty() ? p.name : utf8(fi.completeBaseName()));
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
