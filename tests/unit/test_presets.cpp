// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: presets + preset JSON I/O (M1-T2；M4-T13 schema v2 + v1 迁移)
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
//
// M4-T13（§3.6 [重排]）：PresetData → schema v2（outputs[] + output_template/split_by_format）；
// 0.2 单格式 JSON 由 load_preset() 走 migrate_preset_v1() 零丢失迁移。本文件的既有用例
// （往返/清理/保留键/扫描/校验/归一/IO 错误/字节路径）逐条保留语义并改到 v2 字段面，
// 新增 v1↔v2 迁移往返的**逐字段零丢失**断言（R31）。
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "core/params.h"
#include "core/presets.h"
#include "core/settings.h" // M2-T14: settings INI 路径的字节往返
#include "env_compat.h"    // M3 v1.5: getpid 薄垫层
#include "ui/preset_io.h"

namespace {

int g_fail = 0;

void fail(const std::string &c, const std::string &d) {
    std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
    ++g_fail;
}

void check(bool ok, const std::string &c, const std::string &d) {
    if (!ok)
        fail(c, d);
}

// M3-T11b 诊断：在每个用例块开头把用例名写到 stderr（无缓冲，挂起时最后一行即挂起处）。
// 与判定输出（stdout 的 FAIL/OK 行）分离：不改任何断言、输出行与退出码。
void case_begin(const char *name) {
    std::fprintf(stderr, "CASE %s\n", name);
    std::fflush(stderr);
}

bool contains(const std::vector<std::string> &v, const std::string &s) {
    for (const std::string &x : v)
        if (x == s)
            return true;
    return false;
}

std::string join(const std::vector<std::string> &v) {
    std::string out;
    for (const std::string &s : v)
        out += (out.empty() ? "" : ",") + s;
    return out;
}

// —— 字段级比较（PresetData 无 operator==）——
bool same_timeshift(const pp::TimeShift &a, const pp::TimeShift &b) {
    return a.mode == b.mode && a.years == b.years && a.months == b.months && a.days == b.days &&
           a.hours == b.hours && a.minutes == b.minutes && a.seconds == b.seconds &&
           a.from_offset_min == b.from_offset_min && a.to_offset_min == b.to_offset_min;
}

bool same_gps(const pp::GpsData &a, const pp::GpsData &b) {
    return a.lat == b.lat && a.lon == b.lon && a.altitude == b.altitude &&
           a.direction == b.direction && a.timestamp == b.timestamp;
}

bool same_edit(const pp::TagEdit &a, const pp::TagEdit &b) {
    return a.key == b.key && a.value == b.value && a.remove == b.remove;
}

std::string diff_edits(const char *what, const std::vector<pp::TagEdit> &a,
                       const std::vector<pp::TagEdit> &b) {
    if (a.size() != b.size())
        return std::string(what) + " size " + std::to_string(a.size()) +
               " != " + std::to_string(b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!same_edit(a[i], b[i]))
            return std::string(what) + "[" + std::to_string(i) + "] differs (key " + a[i].key +
                   " / " + b[i].key + ")";
    return {};
}

// 逐输出字段（v2 的 outputs[] 元素）
std::string diff_spec(std::size_t i, const pp::OutputFormatSpec &a, const pp::OutputFormatSpec &b) {
    const std::string tag = "outputs[" + std::to_string(i) + "].";
    if (a.format_id != b.format_id)
        return tag + "format '" + a.format_id + "' != '" + b.format_id + "'";
    if (a.backend_id != b.backend_id)
        return tag + "backend '" + a.backend_id + "' != '" + b.backend_id + "'";
    if (a.tech_id != b.tech_id)
        return tag + "tech '" + a.tech_id + "' != '" + b.tech_id + "'";
    if (a.out_bitdepth != b.out_bitdepth)
        return tag + "bitdepth " + std::to_string(a.out_bitdepth) +
               " != " + std::to_string(b.out_bitdepth);
    if (pp::lossless_flag(a.params) != pp::lossless_flag(b.params))
        return tag + "lossless " + (pp::lossless_flag(a.params) ? "true" : "false") +
               " != " + (pp::lossless_flag(b.params) ? "true" : "false");
    // 保留键（"__" 前缀）不落盘（M1-T2 §3.12 冻结口径：load 不还原、save 不写出）→
    // 持久化面比较时跳过；无损标志由上面的 lossless_flag 断言单独覆盖（显式 schema 参数）。
    const auto persisted_keys = [](const pp::ParamSet &s) {
        std::vector<std::string> keys;
        for (const auto &[k, v] : s)
            if (k.rfind("__", 0) != 0)
                keys.push_back(k);
        return keys;
    };
    const std::vector<std::string> ak = persisted_keys(a.params);
    const std::vector<std::string> bk = persisted_keys(b.params);
    if (ak != bk) {
        std::string d = tag + "params differ:";
        for (const std::string &k : ak)
            if (!contains(bk, k))
                d += " <" + k;
        for (const std::string &k : bk)
            if (!contains(ak, k))
                d += " >" + k;
        return d;
    }
    for (const std::string &k : ak)
        if (!(a.params.at(k) == b.params.at(k)))
            return tag + "params['" + k + "'] differs";
    return {};
}

std::string diff_preset(const pp::PresetData &a, const pp::PresetData &b) {
    if (a.version != b.version)
        return "version " + std::to_string(a.version) + " != " + std::to_string(b.version);
    if (a.name != b.name)
        return "name '" + a.name + "' != '" + b.name + "'";
    if (a.outputs.size() != b.outputs.size())
        return "outputs size " + std::to_string(a.outputs.size()) +
               " != " + std::to_string(b.outputs.size());
    for (std::size_t i = 0; i < a.outputs.size(); ++i) {
        const std::string d = diff_spec(i, a.outputs[i], b.outputs[i]);
        if (!d.empty())
            return d;
    }
    if (a.output_template != b.output_template)
        return "output_template '" + a.output_template + "' != '" + b.output_template + "'";
    if (a.split_by_format != b.split_by_format)
        return "split_by_format";
    if (a.color_target != b.color_target)
        return "color_target";
    if (a.conflict != b.conflict)
        return "conflict";
    if (a.rules.time_shift.has_value() != b.rules.time_shift.has_value())
        return "rules.time_shift presence";
    if (a.rules.time_shift && !same_timeshift(*a.rules.time_shift, *b.rules.time_shift))
        return "rules.time_shift fields";
    if (a.rules.gps.has_value() != b.rules.gps.has_value())
        return "rules.gps presence";
    if (a.rules.gps && !same_gps(*a.rules.gps, *b.rules.gps))
        return "rules.gps fields";
    if (a.rules.gps_clear != b.rules.gps_clear)
        return "rules.gps_clear";
    if (a.rules.strip_privacy != b.rules.strip_privacy)
        return "rules.strip_privacy";
    if (a.rules.sync_mtime != b.rules.sync_mtime)
        return "rules.sync_mtime";
    if (const std::string d =
            diff_edits("rules.exif_edits", a.rules.exif_edits, b.rules.exif_edits);
        !d.empty())
        return d;
    return diff_edits("rules.xmp_edits", a.rules.xmp_edits, b.rules.xmp_edits);
}

std::filesystem::path fresh_dir(const std::filesystem::path &root, const char *name) {
    const std::filesystem::path d = root / name;
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d;
}

std::string read_file(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const std::filesystem::path &p, const std::string &s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

// M2-T14：路径按字节读取（不经 QString）—— 否则含非 UTF-8 字节的路径在测试里也会失真。
QJsonObject parse_object(const std::filesystem::path &p, const std::string &c) {
    const std::string s = read_file(p);
    QJsonParseError e{};
    const QJsonDocument d =
        QJsonDocument::fromJson(QByteArray(s.data(), static_cast<qsizetype>(s.size())), &e);
    if (e.error != QJsonParseError::NoError || !d.isObject()) {
        fail(c, "not a JSON object: " + p.string());
        return {};
    }
    return d.object();
}

// v2 单输出构造助手（jxl/modular + 无损 + 16 位）
pp::OutputFormatSpec make_jxl_spec() {
    pp::OutputFormatSpec spec;
    spec.format_id = "jxl";
    spec.backend_id = "libjxl";
    spec.tech_id = "modular";
    spec.out_bitdepth = 16;
    spec.params[std::string(pp::kLosslessParamKey)] = true;
    spec.params["distance"] = 0.0;
    spec.params["effort"] = int64_t(9);
    spec.params["color_transform"] = std::string("YCoCg");
    spec.params["keep_invisible"] = true;
    spec.params["resampling"] = int64_t(-1); // Enum（整型 choice）
    return spec;
}

pp::PresetData make_delta_preset() {
    using namespace pp;
    PresetData p;
    p.version = 2;
    p.name = "Web 高质量 📸 测试";
    p.outputs.push_back(make_jxl_spec());
    p.output_template = "$format/$dir/$file";
    p.split_by_format = true;
    p.color_target = ColorTarget::DisplayP3;
    p.conflict = ConflictPolicy::Rename;
    TimeShift ts;
    ts.mode = TimeShift::Mode::Delta;
    ts.years = 1;
    ts.months = 2;
    ts.days = 3;
    ts.hours = 4;
    ts.minutes = 5;
    ts.seconds = 6;
    p.rules.time_shift = ts;
    GpsData g;
    g.lat = 31.2304;
    g.lon = 121.4737;
    g.altitude = 12.5;
    g.direction = 359.99;
    g.timestamp = std::string("2024:05:06 07:08:09");
    p.rules.gps = g;
    p.rules.gps_clear = false;
    TagEdit set;
    set.key = "Exif.Image.Artist";
    set.value = std::string("Zhang");
    p.rules.exif_edits.push_back(set);
    TagEdit del;
    del.key = "Exif.Photo.UserComment";
    del.remove = true; // value = nullopt
    p.rules.exif_edits.push_back(del);
    TagEdit xmp;
    xmp.key = "Xmp.dc.title";
    xmp.value = std::string("demo");
    p.rules.xmp_edits.push_back(xmp);
    p.rules.strip_privacy = true;
    p.rules.sync_mtime = true;
    return p;
}

// 0.2 单格式形态（schema v1）的 JSON 文本（迁移用例的输入；逐字段可断言）
std::string v1_json() {
    return R"({
  "version": 1,
  "name": "0.2 单格式",
  "format": "jxl",
  "backend": "libjxl",
  "tech": "modular",
  "lossless": true,
  "bitdepth": 16,
  "color_target": "p3",
  "conflict": "skip",
  "params": { "effort": 9, "distance": 0.0, "color_transform": "YCoCg" },
  "rules": { "sync_mtime": true, "strip_privacy": true }
})";
}

// 同上，但取**未声明显式 lossless 参数**的格式（T13 复核项 1 的回归 fixture）：
// png/oiio/deflate 的 lossless_capable=true（无损复选框可见可勾），而 format_tables 不声明
// 显式参数 `lossless`（写进 params 会被 cross_validate 判"未知参数"）→ 0.2 顶层 lossless
// 只能走输出级 `outputs[i].lossless` 落盘点，否则 load→save→load 会静默回退 false。
std::string v1_json_png() {
    return R"({
  "version": 1,
  "name": "0.2 PNG 无损",
  "format": "png",
  "backend": "oiio",
  "tech": "deflate",
  "lossless": true,
  "bitdepth": 16,
  "color_target": "keep",
  "conflict": "rename",
  "params": { "compressionLevel": 9 },
  "rules": {}
})";
}

} // namespace

int main() {
    using namespace pp;
    namespace fs = std::filesystem;

    const fs::path root = fs::current_path() / ".cache" / "tmp" /
                          ("m1t2_presets_" + std::to_string(pptest::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // 1) 预设往返：unicode name + rules 各字段（delta）+ v2 schema 顶层键/逐输出键
    case_begin("1) 预设往返：unicode name + rules 各字段（delta）+ v2 schema");
    {
        const std::string c = "roundtrip-delta";
        const PresetData p = make_delta_preset();
        const fs::path file = fresh_dir(root, "delta") / "preset.json";
        const std::string err = ui::save_preset(file, p);
        check(err.empty(), c, "save: " + err);
        check(fs::exists(file), c, "file not written");
        PresetData q;
        const std::string lerr = ui::load_preset(file, q);
        check(lerr.empty(), c, "load: " + lerr);
        const std::string d = diff_preset(p, q);
        check(d.empty(), c, "round-trip diff: " + d);

        // schema 顶层键（v2）
        const QJsonObject o = parse_object(file, c);
        for (const char *k : {"version", "name", "outputs", "output_template", "split_by_format",
                              "color_target", "conflict", "rules"})
            check(o.contains(QLatin1String(k)), c, std::string("missing key '") + k + "'");
        check(o.value("version").toInteger(-1) == 2 && o.value("split_by_format").toBool(false), c,
              "version/split_by_format values");
        check(o.value("output_template").toString() == "$format/$dir/$file", c, "output_template");
        check(o.value("name").toString().toStdString() == p.name, c, "name not restored in JSON");
        check(o.value("color_target").toString() == "p3" &&
                  o.value("conflict").toString() == "rename",
              c, "color_target/conflict values");
        // outputs[0]：format/backend/tech/bitdepth/params（含显式无损参数 lossless）
        const QJsonArray outputs = o.value("outputs").toArray();
        check(outputs.size() == 1, c, "outputs size " + std::to_string(outputs.size()));
        const QJsonObject spec = outputs.isEmpty() ? QJsonObject{} : outputs.at(0).toObject();
        check(spec.value("format").toString() == "jxl" &&
                  spec.value("backend").toString() == "libjxl" &&
                  spec.value("tech").toString() == "modular",
              c, "outputs[0] format/backend/tech");
        check(spec.value("bitdepth").toInteger(-1) == 16, c, "outputs[0] bitdepth");
        const QJsonObject params = spec.value("params").toObject();
        check(params.value("distance").toDouble(-1) == 0.0 &&
                  params.value("effort").toInteger(-1) == 9 &&
                  params.value("color_transform").toString() == "YCoCg" &&
                  params.value("keep_invisible").toBool(false) &&
                  params.value("resampling").toInteger(99) == -1,
              c, "params JSON values");
        check(params.value("lossless").toBool(false), c,
              "explicit lossless schema parameter must be serialized (params keys: " +
                  join([&params] {
                      std::vector<std::string> ks;
                      for (auto it = params.constBegin(); it != params.constEnd(); ++it)
                          ks.push_back(it.key().toStdString());
                      return ks;
                  }()) +
                  ")");
        const QJsonObject rules = o.value("rules").toObject();
        for (const char *k : {"time_shift", "gps", "gps_clear", "exif_edits", "xmp_edits",
                              "strip_privacy", "sync_mtime"})
            check(rules.contains(QLatin1String(k)), c,
                  std::string("missing rules key '") + k + "'");
        const QJsonObject ts = rules.value("time_shift").toObject();
        check(ts.value("mode").toString() == "delta" && ts.value("years").toInteger(-1) == 1 &&
                  ts.value("seconds").toInteger(-1) == 6 &&
                  ts.value("from_offset_min").toInteger(-1) == 0 &&
                  ts.value("to_offset_min").toInteger(-1) == 0,
              c, "rules.time_shift fields");
        const QJsonObject gps = rules.value("gps").toObject();
        check(gps.value("lat").toDouble() == 31.2304 && gps.value("lon").toDouble() == 121.4737 &&
                  gps.value("altitude").toDouble(-1) == 12.5 &&
                  gps.value("direction").toDouble(-1) == 359.99 &&
                  gps.value("timestamp").toString().toStdString() == "2024:05:06 07:08:09",
              c, "rules.gps fields");
        const QJsonArray exif = rules.value("exif_edits").toArray();
        check(exif.size() == 2, c, "exif_edits size");
        if (exif.size() == 2) {
            check(exif.at(0).toObject().value("key").toString() == "Exif.Image.Artist" &&
                      exif.at(0).toObject().value("value").toString() == "Zhang" &&
                      !exif.at(0).toObject().contains("remove"),
                  c, "exif_edits[0] set form");
            check(exif.at(1).toObject().value("value").isNull() &&
                      exif.at(1).toObject().value("remove").toBool(false),
                  c, "exif_edits[1] remove form");
        }
        check(rules.value("strip_privacy").toBool(false) && rules.value("sync_mtime").toBool(false),
              c, "privacy/mtime flags");
        check(read_file(file).find("📸") != std::string::npos, c,
              "unicode name should be stored as raw UTF-8");
        check(read_file(file).find("__lossless") == std::string::npos, c,
              "reserved key must never be persisted");
    }

    // 1b) v1 → v2 迁移：**逐字段零丢失**（R31）
    case_begin("1b) v1 → v2 迁移：逐字段零丢失（R31）");
    {
        const std::string c = "migrate-v1-zeroloss";
        const fs::path file = fresh_dir(root, "migrate") / "v1.json";
        write_file(file, v1_json());
        PresetData p;
        const std::string err = ui::load_preset(file, p);
        check(err.empty(), c, "v1 load: " + err);
        check(p.version == 2, c,
              "migrated version must be 2 (got " + std::to_string(p.version) + ")");
        check(p.name == "0.2 单格式", c, "name '" + p.name + "'");
        check(p.outputs.size() == 1, c,
              "v1 → 单元素 outputs（got " + std::to_string(p.outputs.size()) + ")");
        if (!p.outputs.empty()) {
            const OutputFormatSpec &s = p.outputs[0];
            check(s.format_id == "jxl" && s.backend_id == "libjxl" && s.tech_id == "modular", c,
                  "outputs[0] format/backend/tech: " + s.format_id + "/" + s.backend_id + "/" +
                      s.tech_id);
            check(s.out_bitdepth == 16, c, "outputs[0] bitdepth " + std::to_string(s.out_bitdepth));
            check(lossless_flag(s.params), c, "lossless flag lost in migration");
            check(param_int(s.params, "effort", -1) == 9 &&
                      param_float(s.params, "distance", -1) == 0.0 &&
                      param_str(s.params, "color_transform", "") == "YCoCg",
                  c, "v1 params lost in migration");
        }
        check(p.color_target == ColorTarget::DisplayP3, c, "color_target lost");
        check(p.conflict == ConflictPolicy::Skip, c, "conflict lost");
        check(p.rules.sync_mtime && p.rules.strip_privacy, c, "rules lost");
        // 0.2 = 单输出"现状直出" → 模板取单输出兼容形态、分文件夹关
        check(p.output_template == "$dir/$file", c,
              "migrated template '" + p.output_template + "'");
        check(!p.split_by_format, c, "migrated split_by_format must be false");
        // 迁移结果必须自洽（可校验、可归一）
        check(validate_preset(p).empty(), c,
              "migrated preset must validate: " + validate_preset(p));
    }

    // 1c) v1 → v2 → 保存（v2）→ 加载：迁移往返逐字段零丢失
    case_begin("1c) v1 → v2 → 保存 → 加载：迁移往返零丢失");
    {
        const std::string c = "migrate-roundtrip";
        const fs::path dir = fresh_dir(root, "migrate-rt");
        write_file(dir / "v1.json", v1_json());
        PresetData migrated;
        check(ui::load_preset(dir / "v1.json", migrated).empty(), c, "v1 load failed");
        check(ui::save_preset(dir / "v2.json", migrated).empty(), c, "v2 save failed");
        PresetData again;
        check(ui::load_preset(dir / "v2.json", again).empty(), c, "v2 load failed");
        const std::string d = diff_preset(migrated, again);
        check(d.empty(), c, "migration round-trip diff: " + d);
        check(validate_preset(again).empty(), c, "loaded v2 preset must validate");
    }

    // 1c2) v1（**未声明显式 lossless 参数**的格式：png/oiio/deflate）迁移往返零丢失
    //      —— T13 复核项 1 的回归断言：0.2 顶层 lossless=true 必须经 load→save→load 存活。
    //      该格式在 params 里**不能**带显式 `lossless`（会被 cross_validate 判"未知参数"），
    //      故落盘点 = 输出级 `outputs[0].lossless`；保存文本里不得出现保留键 __lossless。
    case_begin("1c2) v1 未声明分支（png）迁移往返：lossless 不静默回退");
    {
        const std::string c = "migrate-undeclared-lossless";
        const fs::path dir = fresh_dir(root, "migrate-png");
        write_file(dir / "v1.json", v1_json_png());

        PresetData p;
        check(ui::load_preset(dir / "v1.json", p).empty(), c, "v1 load failed");
        check(p.outputs.size() == 1, c, "expected single output");
        if (!p.outputs.empty()) {
            const OutputFormatSpec &s = p.outputs[0];
            check(s.format_id == "png" && s.backend_id == "oiio" && s.tech_id == "deflate", c,
                  "outputs[0] 三元组: " + s.format_id + "/" + s.backend_id + "/" + s.tech_id);
            check(lossless_flag(s.params), c, "迁移后 lossless 标志丢失");
            check(s.params.count(std::string(kLosslessParamKey)) == 0, c,
                  "png 不应带显式 lossless 参数（该格式未声明，会被判未知参数）");
            check(param_int(s.params, "compressionLevel", -1) == 9, c, "params 迁移丢失");
        }
        // 迁移结果必须自洽（png 上带显式键会被 cross_validate 判未知参数）
        check(validate_preset(p).empty(), c,
              "migrated preset must validate: " + validate_preset(p));
        check(cross_validate(p.outputs[0].params, "png", "deflate").empty(), c,
              "png 参数集不得被判未知参数：[" +
                  join(cross_validate(p.outputs[0].params, "png", "deflate")) + "]");

        // 保存：lossless 落进输出级字段；保留键仍绝不落盘
        const fs::path file = dir / "v2.json";
        check(ui::save_preset(file, p).empty(), c, "v2 save failed");
        const QJsonObject spec =
            parse_object(file, c).value(QStringLiteral("outputs")).toArray().at(0).toObject();
        check(spec.value(QStringLiteral("lossless")).toBool(false), c,
              "输出级 lossless 字段未落盘（复核项 1 回归）");
        check(!spec.value(QStringLiteral("params")).toObject().contains(QStringLiteral("lossless")),
              c, "png 的 params 不应含显式 lossless");
        check(read_file(file).find("__lossless") == std::string::npos, c,
              "reserved key must never be persisted");

        // 往返：再次加载后标志仍在（这正是复核项 1 报的静默回退）
        PresetData again;
        check(ui::load_preset(file, again).empty(), c, "v2 reload failed");
        check(!again.outputs.empty() && lossless_flag(again.outputs[0].params), c,
              "load→save→load 后 lossless 静默回退 false（复核项 1）");
        const std::string d = diff_preset(p, again);
        check(d.empty(), c, "undeclared-format round-trip diff: " + d);
    }

    // 1d) v2 多输出往返（2 输出 + 模板 + 分文件夹 + 逐输出参数/位深）
    case_begin("1d) v2 多输出往返（2 输出 + 模板 + 分文件夹）");
    {
        const std::string c = "roundtrip-multi";
        PresetData p = make_delta_preset();
        OutputFormatSpec second;
        second.format_id = "webp";
        second.backend_id = "libwebp";
        second.tech_id = "lossless";
        second.out_bitdepth = 8;
        second.params[std::string(kLosslessParamKey)] = true;
        second.params["quality"] = int64_t(80);
        second.params["exact"] = true;
        p.outputs.push_back(second);
        const fs::path file = fresh_dir(root, "multi") / "p.json";
        check(ui::save_preset(file, p).empty(), c, "save multi failed");
        PresetData q;
        check(ui::load_preset(file, q).empty(), c, "load multi failed");
        check(diff_preset(p, q).empty(), c, "multi round-trip diff: " + diff_preset(p, q));
        const QJsonObject o = parse_object(file, c);
        check(o.value("outputs").toArray().size() == 2, c, "two outputs expected in JSON");
        check(o.value("outputs")
                  .toArray()
                  .at(1)
                  .toObject()
                  .value("params")
                  .toObject()
                  .value("lossless")
                  .toBool(false),
              c, "outputs[1].params.lossless missing");
        check(validate_preset(q).empty(), c, "multi preset must validate: " + validate_preset(q));
    }

    // 1e) v2 加载错误路径：版本/结构显式报错（不静默）
    case_begin("1e) v2 加载错误路径：版本/结构显式报错");
    {
        const std::string c = "v2-load-errors";
        PresetData out;
        out.name = "untouched";
        const fs::path dir = fresh_dir(root, "v2-errors");
        write_file(dir / "future.json", R"({"version":3,"name":"x"})");
        const std::string e1 = ui::load_preset(dir / "future.json", out);
        check(e1.find("version") != std::string::npos && out.name == "untouched", c,
              "future version must fail loudly: [" + e1 + "]");
        write_file(dir / "nooutputs.json", R"({"version":2,"name":"x"})");
        check(ui::load_preset(dir / "nooutputs.json", out).find("outputs") != std::string::npos, c,
              "missing outputs must fail");
        write_file(dir / "empty.json", R"({"version":2,"name":"x","outputs":[]})");
        check(ui::load_preset(dir / "empty.json", out).find("outputs") != std::string::npos, c,
              "empty outputs must fail");
        write_file(dir / "baditem.json", R"({"version":2,"name":"x","outputs":[17]})");
        check(ui::load_preset(dir / "baditem.json", out).find("outputs[0]") != std::string::npos, c,
              "non-object output must fail");
        write_file(dir / "badparams.json",
                   R"({"version":2,"name":"x","outputs":[{"format":"jxl","params":[]}]})");
        check(ui::load_preset(dir / "badparams.json", out).find("params") != std::string::npos, c,
              "non-object params must fail");
        check(out.name == "untouched", c, "failed loads must not modify out");
    }

    // 2) 预设往返：时区语义 TimeShift + gps 缺省 + gps_clear
    case_begin("2) 预设往返：时区语义 TimeShift + gps 缺省 + gps_clear");
    {
        const std::string c = "roundtrip-timezone";
        PresetData p;
        p.name = "时区语义";
        OutputFormatSpec spec;
        spec.format_id = "jpeg";
        spec.backend_id = "jpegli";
        spec.tech_id = "dct";
        spec.out_bitdepth = 8;
        p.outputs.push_back(spec);
        p.color_target = ColorTarget::KeepOriginal;
        p.conflict = ConflictPolicy::Skip;
        p.output_template = "$dir/$file";
        TimeShift ts;
        ts.mode = TimeShift::Mode::TimezoneSemantic;
        ts.from_offset_min = 480;
        ts.to_offset_min = 540;
        p.rules.time_shift = ts;
        p.rules.gps_clear = true;
        p.rules.sync_mtime = true;
        const fs::path file = fresh_dir(root, "tz") / "p.json";
        std::string err = ui::save_preset(file, p);
        check(err.empty(), c, "save: " + err);
        PresetData q;
        err = ui::load_preset(file, q);
        check(err.empty(), c, "load: " + err);
        const std::string d = diff_preset(p, q);
        check(d.empty(), c, "round-trip diff: " + d);
        const QJsonObject o = parse_object(file, c);
        const QJsonObject rules = o.value("rules").toObject();
        const QJsonObject tsj = rules.value("time_shift").toObject();
        check(tsj.value("mode").toString() == "timezone" &&
                  tsj.value("from_offset_min").toInteger(-1) == 480 &&
                  tsj.value("to_offset_min").toInteger(-1) == 540,
              c, "timezone fields");
        check(rules.value("gps").isUndefined(), c, "absent gps should stay absent");
        check(o.value("conflict").toString() == "skip", c, "conflict value");
        // jpeg 不声明显式无损参数 → params 里不应凭空出现 lossless 键（未知参数会被拒）
        check(!q.outputs.at(0).params.count(std::string(kLosslessParamKey)), c,
              "jpeg must not carry the explicit lossless parameter");
    }

    // 3) 保留键不落盘（无损由"可落盘的面"表达；T13 复核项 1 后该面有两级，见断言）
    case_begin("3) 保留键不落盘（显式 lossless 参数表达）");
    {
        const std::string c = "reserved-key";
        PresetData p;
        p.name = "R";
        OutputFormatSpec spec;
        spec.format_id = "png";
        spec.backend_id = "oiio";
        spec.tech_id = "deflate";
        spec.params[std::string(kLosslessKey)] = true;
        spec.params["compressionLevel"] = int64_t(5);
        p.outputs.push_back(spec);
        const fs::path file = fresh_dir(root, "reserved") / "p.json";
        const std::string err = ui::save_preset(file, p);
        check(err.empty(), c, "save: " + err);
        // 不变式一（M1 冻结口径，逐字保留）：保留键字面量绝不落盘
        check(read_file(file).find("__lossless") == std::string::npos, c,
              "reserved key leaked to JSON");
        PresetData q;
        check(ui::load_preset(file, q).empty(), c, "load failed");
        check(param_int(q.outputs.at(0).params, "compressionLevel", -1) == 5, c,
              "params survived JSON round-trip");
        // 不变式二（T13 复核项 1 新增）：无损标志必须以**可落盘的面**存活 —— png/oiio/deflate
        // 未声明显式参数，故走输出级 `outputs[i].lossless`，加载时还原为内部管道键
        // （谓词/编码器的输入面；键名本身仍不落盘，见上面的 JSON 文本断言）。
        // 旧断言是"标志不得回传"—— 那正是复核项 1 报的静默丢失（load→save→load 由 true 变 false）。
        check(lossless_flag(q.outputs.at(0).params), c,
              "无损标志经 save→load 静默回退 false（复核项 1）");
        check(lossless_flag(q.outputs.at(0).params) == lossless_flag(spec.params), c,
              "无损标志往返前后不一致");
    }

    // 4) list_presets：*.json 扫描 + 按名排序（无效 JSON 用文件名主干）
    case_begin("4) list_presets：*.json 扫描 + 按名排序（无效 JSON 用文件名主干）");
    {
        const std::string c = "list-presets";
        const fs::path dir = fresh_dir(root, "list");
        PresetData p;
        OutputFormatSpec spec;
        spec.format_id = "jxl";
        spec.backend_id = "libjxl";
        spec.tech_id = "vardct";
        p.outputs.push_back(spec);
        for (const auto &[name, file] : std::vector<std::pair<std::string, std::string>>{
                 {"Zeta", "a.json"}, {"Alpha", "b.json"}, {"Mike", "c.json"}}) {
            p.name = name;
            const std::string err = ui::save_preset(dir / file, p);
            check(err.empty(), c, "save " + file + ": " + err);
        }
        write_file(dir / "bad.json", "{ this is not json");
        write_file(dir / "notes.txt", "ignore me");
        const auto list = ui::list_presets(dir);
        check(list.size() == 4, c, "expected 4 entries, got " + std::to_string(list.size()));
        std::string names;
        for (const auto &[path, name] : list)
            names += (names.empty() ? "" : ",") + name;
        check(names == "Alpha,Mike,Zeta,bad", c, "order [" + names + "]");
        check(ui::list_presets(root / "does-not-exist").empty(), c, "missing dir → empty list");

        // M2-T14 换掉 QDir 实现后钉住旧语义：隐藏文件不列、*.json 大小写不敏感、目录不列、
        // 不可读文件不列（实测 QDir::entryInfoList("*.json", Files|Readable) 的既有行为）
        const fs::path parity_dir = fresh_dir(root, "list-parity");
        write_file(parity_dir / "a.json",
                   R"({"version":2,"name":"A","outputs":[{"format":"jxl"}]})");
        write_file(parity_dir / "upper.JSON",
                   R"({"version":2,"name":"Upper","outputs":[{"format":"jxl"}]})");
        write_file(parity_dir / "b.tar.json",
                   R"({"version":2,"name":"Tar","outputs":[{"format":"jxl"}]})");
        write_file(parity_dir / ".hidden.json",
                   R"({"version":2,"name":"Hidden","outputs":[{"format":"jxl"}]})");
        write_file(parity_dir / "notes.txt", "ignore");
        fs::create_directories(parity_dir / "sub.json", ec);
        // M3（Windows）："不可读文件不列出"的前提在 Windows 不可构造——fs::perms::none
        // 仅映射只读属性（读取不受影响），与 Linux 下 root 情形同类；故该 fixture 仅
        // POSIX 非 root 创建，期望集 "A,Tar,Upper" 在两平台一致成立。
#if !defined(_WIN32)
        if (::getuid() != 0) { // root 无视读权限，两套实现都会列出
            write_file(parity_dir / "noread.json",
                       R"({"version":2,"name":"NoRead","outputs":[{"format":"jxl"}]})");
            fs::permissions(parity_dir / "noread.json", fs::perms::none, ec);
        }
#endif
        std::string got;
        for (const auto &[path, name] : ui::list_presets(parity_dir))
            got += (got.empty() ? "" : ",") + name;
        check(got == "A,Tar,Upper", c, "listing parity (hidden/case/dir/readable): [" + got + "]");
    }

    // 5) validate_preset：格式/后端/技术/位深/参数/版本/模板（v2 多输出）
    case_begin("5) validate_preset：格式/后端/技术/位深/参数/版本/模板");
    {
        const std::string c = "validate-preset";
        PresetData p;
        p.name = "ok";
        OutputFormatSpec spec;
        spec.format_id = "jxl";
        spec.backend_id = "libjxl";
        spec.tech_id = "modular";
        spec.out_bitdepth = 16;
        spec.params = default_params(*find_format("jxl"), "libjxl", "modular", true);
        p.outputs.push_back(spec);
        p.output_template = "$format/$dir/$file";
        check(validate_preset(p).empty(), c, "clean preset should pass: " + validate_preset(p));

        PresetData bad = p;
        bad.outputs[0].format_id = "nope";
        check(!validate_preset(bad).empty(), c, "unknown format must fail");
        bad = p;
        bad.outputs[0].backend_id = "nope";
        check(validate_preset(bad).find("backend") != std::string::npos, c,
              "unknown backend must fail");
        bad = p;
        bad.outputs[0].tech_id = "nope";
        check(validate_preset(bad).find("tech") != std::string::npos, c, "unknown tech must fail");
        bad = p;
        bad.outputs[0].out_bitdepth = 12;
        check(validate_preset(bad).find("bitdepth") != std::string::npos, c,
              "bad bitdepth must fail");
        bad = p;
        bad.outputs[0].params["effort"] = int64_t(42);
        check(validate_preset(bad).find("effort") != std::string::npos, c,
              "out-of-range param must fail");
        bad = p;
        bad.outputs[0].params["modular_predictor"] = std::string("fast");
        check(validate_preset(bad).find("modular_predictor") != std::string::npos, c,
              "wrong param type must fail");
        bad = p;
        bad.version = 3;
        check(validate_preset(bad).find("version") != std::string::npos, c,
              "bad version must fail");
        bad = p;
        bad.outputs.clear();
        check(validate_preset(bad).find("outputs") != std::string::npos, c,
              "empty outputs must fail");
        // §4.2：非法模板 = 预设无效（未知符号 / .. / 绝对路径）
        for (const char *t : {"$bogus/$file", "../$file", "/abs/$file", "$dir//../x"}) {
            bad = p;
            bad.output_template = t;
            check(!validate_preset(bad).empty(), c,
                  std::string("invalid template must fail: ") + t);
        }
        // 多输出：错误消息带 output #N 前缀（单输出不带 —— 0.2 文本逐字沿用）
        bad = p;
        bad.outputs.push_back(spec);
        bad.outputs[1].backend_id = "nope";
        check(validate_preset(bad).find("output #1") != std::string::npos, c,
              "multi-output error must carry the output index: " + validate_preset(bad));

        // heif/avif：运行时内省参数 → 静态校验跳过技术/参数；位深按静态允许集校验（M1-T2b：
        // heif/avif = {8,10,12}，默认位深由调用方选，不在表内）
        PresetData h;
        h.name = "heif";
        OutputFormatSpec hs;
        hs.format_id = "heif";
        hs.backend_id = "x265";
        hs.tech_id = "x265-main";
        hs.out_bitdepth = 10;
        h.outputs.push_back(hs);
        check(validate_preset(h).empty(), c,
              "runtime-introspected preset should pass: " + validate_preset(h));
        for (const int d : {8, 10, 12}) {
            h.outputs[0].out_bitdepth = d;
            check(validate_preset(h).empty(), c,
                  "heif bitdepth " + std::to_string(d) + " must pass: " + validate_preset(h));
        }
        PresetData a;
        a.name = "avif";
        OutputFormatSpec as;
        as.format_id = "avif";
        as.backend_id = "libaom";
        as.out_bitdepth = 12;
        a.outputs.push_back(as);
        check(validate_preset(a).empty(), c, "avif bitdepth 12 must pass: " + validate_preset(a));
        a.outputs[0].backend_id = "svt-av1";
        check(validate_preset(a).empty(), c,
              "avif/svt-av1 bitdepth 12 is in the static allowed set (runtime probe intersects): " +
                  validate_preset(a));
        h.outputs[0].out_bitdepth = 16;
        check(validate_preset(h).find("bitdepth") != std::string::npos, c,
              "heif bitdepth 16 must fail");
        PresetData j;
        j.name = "jpeg";
        OutputFormatSpec js;
        js.format_id = "jpeg";
        js.backend_id = "jpegli";
        js.tech_id = "dct";
        js.out_bitdepth = 12;
        j.outputs.push_back(js);
        check(validate_preset(j).find("bitdepth") != std::string::npos, c,
              "jpeg bitdepth 12 must fail");
    }

    // 6) normalize_preset：补齐后端/技术/参数 + 应用锁定（v2 逐输出）
    case_begin("6) normalize_preset：补齐后端/技术/参数 + 应用锁定（v2）");
    {
        const std::string c = "normalize-preset";
        PresetData p;
        OutputFormatSpec spec;
        spec.format_id = "jxl";
        spec.out_bitdepth = 16;
        spec.params[std::string(kLosslessParamKey)] = true;
        spec.params[std::string(kLosslessKey)] = true;
        spec.params["distance"] = 5.0;
        spec.params["modular_lossy_palette"] = true;
        p.outputs.push_back(spec);
        const std::vector<std::string> touched = normalize_preset(p);
        check(p.outputs[0].backend_id == "libjxl" && p.outputs[0].tech_id == "modular", c,
              "default backend/tech: " + p.outputs[0].backend_id + "/" + p.outputs[0].tech_id);
        check(contains(touched, "effort") && contains(touched, "color_transform"), c,
              "filled keys [" + join(touched) + "]");
        check(contains(touched, "distance") && contains(touched, "modular_lossy_palette"), c,
              "lock-rewritten keys [" + join(touched) + "]");
        check(param_float(p.outputs[0].params, "distance", -1) == 0.0 &&
                  !param_bool(p.outputs[0].params, "modular_lossy_palette", true),
              c, "locks not applied by normalize_preset");
        check(validate_preset(p).empty(), c,
              "normalized preset must validate: " + validate_preset(p));
        PresetData bad;
        OutputFormatSpec bspec;
        bspec.format_id = "nope";
        bad.outputs.push_back(bspec);
        check(normalize_preset(bad).empty(), c, "unknown format → nothing to do");
        // 空模板 → 用 §3.2 默认补齐
        PresetData t;
        t.outputs.push_back(spec);
        t.output_template.clear();
        normalize_preset(t);
        check(t.output_template == "$format/$dir/$file", c,
              "empty template must be normalized to the §3.2 default");
    }

    // 7) I/O 错误路径
    case_begin("7) I/O 错误路径");
    {
        const std::string c = "io-errors";
        const PresetData p = make_delta_preset();
        const fs::path nested = root / "new" / "deep" / "p.json";
        check(ui::save_preset(nested, p).empty() && fs::exists(nested), c,
              "save must create parent dirs");
        check(!ui::save_preset(fs::path{}, p).empty(), c, "empty path must fail");
        PresetData out;
        out.name = "untouched";
        check(!ui::load_preset(root / "missing.json", out).empty(), c, "missing file must fail");
        check(out.name == "untouched", c, "failed load must not modify out");
        const fs::path badj = root / "bad.json";
        write_file(badj, "{ oops");
        check(!ui::load_preset(badj, out).empty(), c, "invalid JSON must fail");
        const fs::path nonobj = root / "array.json";
        write_file(nonobj, "[1,2,3]");
        check(!ui::load_preset(nonobj, out).empty(), c, "non-object root must fail");
        const fs::path badcolor = root / "color.json";
        write_file(
            badcolor,
            R"({"version":2,"name":"x","outputs":[{"format":"jxl"}],"color_target":"cmyk"})");
        const std::string err = ui::load_preset(badcolor, out);
        check(err.find("color_target") != std::string::npos, c, "bad color_target: [" + err + "]");
        const fs::path badconf = root / "conflict.json";
        write_file(badconf,
                   R"({"version":2,"name":"x","outputs":[{"format":"jxl"}],"conflict":"explode"})");
        check(ui::load_preset(badconf, out).find("conflict") != std::string::npos, c,
              "bad conflict");
        // 缺省字段 → 默认值（向前兼容）
        const fs::path minimal = root / "minimal.json";
        write_file(minimal, R"({"format":"png","name":"m"})");
        PresetData m;
        check(ui::load_preset(minimal, m).empty(), c, "minimal v1 preset must load");
        // 最小 v1 预设：缺省字段取默认；迁移只补内部管道键（__ 前缀，不落盘）
        int persisted = 0;
        for (const auto &[k, v] : m.outputs.at(0).params)
            if (k.rfind("__", 0) != 0)
                ++persisted;
        check(m.version == 2 && m.outputs.size() == 1 && m.outputs[0].format_id == "png" &&
                  m.outputs[0].out_bitdepth == 8 && m.color_target == ColorTarget::KeepOriginal &&
                  m.conflict == ConflictPolicy::Rename && persisted == 0,
              c, "minimal preset defaults");
        // M2-T14：目录不是合法预设文件 —— 必须返回错误而不得抛异常（libstdc++ 的 ifstream
        // 读目录会抛 std::ios_base::failure），也不得留下 <dir>.tmp 兄弟文件
        const fs::path as_dir = fresh_dir(root, "io-dir");
        PresetData dout;
        dout.name = "untouched";
        check(!ui::load_preset(as_dir, dout).empty(), c, "directory must not load as a preset");
        check(dout.name == "untouched", c, "failed load must not modify out (dir)");
        check(!ui::save_preset(as_dir, p).empty(), c, "directory must not be a preset target");
        check(!fs::exists(fs::path(as_dir.string() + ".tmp")), c, "stray <dir>.tmp created");
    }

    // 8) M2-T14（#23）：非 UTF-8 字节路径 / UTF-8 非 ASCII 路径往返
    case_begin("8) M2-T14（#23）：非 UTF-8 字节路径 / UTF-8 非 ASCII 路径往返");
    //    Qt6/Linux 的 QString 文件名一律按 UTF-8 编码、解码严格（非法字节 → U+FFFD），
    //    所以下面的原始字节路径是"经 QString 的旧实现必然失败、字节层实现必须往返"的判据。
    {
        const std::string c = "locale-safe-path";
        const PresetData p = make_delta_preset();
#if !defined(_WIN32)
        // M3 v1.9：原始非 UTF-8 字节路径（孤立 0xE9）在 Windows 上不可表示——路径为 UTF-16，
        // 非法 UTF-8 序列无法构造/往返；该判据仅在字节路径语义的 POSIX 上成立（同 noread
        // fixture 先例）。下方"合法 UTF-8 非 ASCII 路径"回归段两平台均保留并生效。
        // 0xE9 单独出现不是合法 UTF-8 序列 → 目录名 "caf<E9>"、文件名 "p<E9>.json"
        const fs::path raw_dir = fresh_dir(root, "caf\xE9");
        const fs::path raw_file = raw_dir / "p\xE9.json";
        const std::string raw_bytes = raw_file.string();
        {
            const QString via_utf8 =
                QString::fromUtf8(raw_bytes.data(), static_cast<qsizetype>(raw_bytes.size()));
            check(via_utf8.contains(QChar::ReplacementCharacter), c,
                  "precondition: path must contain non-UTF-8 bytes");
        }

        const std::string serr = ui::save_preset(raw_file, p);
        check(serr.empty(), c, "save into non-UTF-8 byte path: " + serr);
        check(fs::exists(raw_file), c, "no file at byte path " + raw_bytes);
        PresetData q;
        const std::string lerr = ui::load_preset(raw_file, q);
        check(lerr.empty(), c, "load from non-UTF-8 byte path: " + lerr);
        check(diff_preset(p, q).empty(), c, "byte-path round-trip diff: " + diff_preset(p, q));
        check(parse_object(raw_file, c).value("name").toString().toStdString() == p.name, c,
              "byte-path JSON content");

        // list_presets：返回的路径必须是原字节串（不得经 QString 再编码）
        const auto list = ui::list_presets(raw_dir);
        check(list.size() == 1, c, "list size " + std::to_string(list.size()));
        if (list.size() == 1) {
            check(list[0].first.string() == raw_bytes, c,
                  "listed path bytes changed: " + list[0].first.string());
            PresetData r;
            check(ui::load_preset(list[0].first, r).empty(), c, "load via listed path");
            check(diff_preset(p, r).empty(), c, "listed-path round-trip diff");
        }

        // settings INI：同一条路径边界的字节往返（M2-T14 覆盖项）
        const fs::path ini = raw_dir / "settings.ini";
        AppSettings s;
        s.workers = 7;
        s.log_level = "debug";
        s.last_preset = raw_bytes;
        const std::string inierr = save_settings(ini, s);
        check(inierr.empty(), c, "settings save: " + inierr);
        const AppSettings s2 = load_settings(ini);
        check(s2.workers == 7 && s2.log_level == "debug" && s2.last_preset == raw_bytes, c,
              "settings byte-path round trip");
#endif // !defined(_WIN32)

        // 回归：合法 UTF-8 的非 ASCII 路径行为不变
        const fs::path utf8_dir = fresh_dir(root, "预设📸");
        const fs::path utf8_file = utf8_dir / "预设.json";
        check(ui::save_preset(utf8_file, p).empty(), c, "save into UTF-8 non-ASCII path");
        PresetData u;
        check(ui::load_preset(utf8_file, u).empty(), c, "load from UTF-8 non-ASCII path");
        check(diff_preset(p, u).empty(), c, "UTF-8 path round-trip diff");
        const auto ulist = ui::list_presets(utf8_dir);
        check(ulist.size() == 1 && ulist[0].first.string() == utf8_file.string(), c,
              "UTF-8 path listing bytes");
    }

    case_begin("9) 收尾（清理 + 汇总）");
    fs::remove_all(root, ec);
    if (g_fail == 0)
        std::printf("test_presets: OK\n");
    else
        std::printf("test_presets: %d failure(s)\n", g_fail);
    return g_fail;
}
