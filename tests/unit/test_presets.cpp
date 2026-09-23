// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: presets + preset JSON I/O (M1-T2)
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
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

std::string diff_preset(const pp::PresetData &a, const pp::PresetData &b) {
    if (a.version != b.version)
        return "version";
    if (a.name != b.name)
        return "name '" + a.name + "' != '" + b.name + "'";
    if (a.format_id != b.format_id)
        return "format '" + a.format_id + "' != '" + b.format_id + "'";
    if (a.backend_id != b.backend_id)
        return "backend '" + a.backend_id + "' != '" + b.backend_id + "'";
    if (a.tech_id != b.tech_id)
        return "tech '" + a.tech_id + "' != '" + b.tech_id + "'";
    if (a.lossless != b.lossless)
        return "lossless";
    if (a.out_bitdepth != b.out_bitdepth)
        return "bitdepth";
    if (a.color_target != b.color_target)
        return "color_target";
    if (a.conflict != b.conflict)
        return "conflict";
    if (!(a.params == b.params)) {
        std::string d = "params differ:";
        for (const auto &[k, v] : a.params)
            if (!b.params.count(k))
                d += " <" + k;
        for (const auto &[k, v] : b.params)
            if (!a.params.count(k))
                d += " >" + k;
        return d;
    }
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

pp::PresetData make_delta_preset() {
    using namespace pp;
    PresetData p;
    p.version = 1;
    p.name = "Web 高质量 📸 测试";
    p.format_id = "jxl";
    p.backend_id = "libjxl";
    p.tech_id = "modular";
    p.lossless = true;
    p.out_bitdepth = 16;
    p.color_target = ColorTarget::DisplayP3;
    p.conflict = ConflictPolicy::Rename;
    p.params["distance"] = 0.0;
    p.params["effort"] = int64_t(9);
    p.params["color_transform"] = std::string("YCoCg");
    p.params["keep_invisible"] = true;
    p.params["resampling"] = int64_t(-1); // Enum（整型 choice）
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

} // namespace

int main() {
    using namespace pp;
    namespace fs = std::filesystem;

    const fs::path root = fs::current_path() / ".cache" / "tmp" /
                          ("m1t2_presets_" + std::to_string(pptest::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // 1) 预设往返：unicode name + rules 各字段（delta）
    case_begin("1) 预设往返：unicode name + rules 各字段（delta）");
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

        // schema 顶层键（§3.12）
        const QJsonObject o = parse_object(file, c);
        for (const char *k : {"version", "name", "format", "backend", "tech", "lossless",
                              "bitdepth", "color_target", "conflict", "params", "rules"})
            check(o.contains(QLatin1String(k)), c, std::string("missing key '") + k + "'");
        check(o.value("name").toString().toStdString() == p.name, c, "name not restored in JSON");
        check(o.value("color_target").toString() == "p3" &&
                  o.value("conflict").toString() == "rename",
              c, "color_target/conflict values");
        check(o.value("bitdepth").toInteger(-1) == 16 && o.value("lossless").toBool(false), c,
              "bitdepth/lossless values");
        const QJsonObject params = o.value("params").toObject();
        check(params.value("distance").toDouble(-1) == 0.0 &&
                  params.value("effort").toInteger(-1) == 9 &&
                  params.value("color_transform").toString() == "YCoCg" &&
                  params.value("keep_invisible").toBool(false) &&
                  params.value("resampling").toInteger(99) == -1,
              c, "params JSON values");
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
    }

    // 2) 预设往返：时区语义 TimeShift + gps 缺省 + gps_clear
    case_begin("2) 预设往返：时区语义 TimeShift + gps 缺省 + gps_clear");
    {
        const std::string c = "roundtrip-timezone";
        PresetData p;
        p.name = "时区语义";
        p.format_id = "jpeg";
        p.backend_id = "jpegli";
        p.tech_id = "dct";
        p.lossless = false;
        p.out_bitdepth = 8;
        p.color_target = ColorTarget::KeepOriginal;
        p.conflict = ConflictPolicy::Skip;
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
    }

    // 3) 保留键不落盘（顶层 lossless 表达）
    case_begin("3) 保留键不落盘（顶层 lossless 表达）");
    {
        const std::string c = "reserved-key";
        PresetData p;
        p.name = "R";
        p.format_id = "png";
        p.backend_id = "oiio";
        p.tech_id = "deflate";
        p.params["__lossless"] = true;
        p.params["compressionLevel"] = int64_t(5);
        const fs::path file = fresh_dir(root, "reserved") / "p.json";
        const std::string err = ui::save_preset(file, p);
        check(err.empty(), c, "save: " + err);
        check(read_file(file).find("__lossless") == std::string::npos, c,
              "reserved key leaked to JSON");
        PresetData q;
        check(ui::load_preset(file, q).empty(), c, "load failed");
        check(!q.params.count("__lossless") && param_int(q.params, "compressionLevel", -1) == 5, c,
              "__lossless must not come back through JSON");
    }

    // 4) list_presets：*.json 扫描 + 按名排序（无效 JSON 用文件名主干）
    case_begin("4) list_presets：*.json 扫描 + 按名排序（无效 JSON 用文件名主干）");
    {
        const std::string c = "list-presets";
        const fs::path dir = fresh_dir(root, "list");
        PresetData p;
        p.format_id = "jxl";
        p.backend_id = "libjxl";
        p.tech_id = "vardct";
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
        write_file(parity_dir / "a.json", R"({"version":1,"name":"A","format":"jxl"})");
        write_file(parity_dir / "upper.JSON", R"({"version":1,"name":"Upper","format":"jxl"})");
        write_file(parity_dir / "b.tar.json", R"({"version":1,"name":"Tar","format":"jxl"})");
        write_file(parity_dir / ".hidden.json", R"({"version":1,"name":"Hidden","format":"jxl"})");
        write_file(parity_dir / "notes.txt", "ignore");
        fs::create_directories(parity_dir / "sub.json", ec);
        // M3（Windows）："不可读文件不列出"的前提在 Windows 不可构造——fs::perms::none
        // 仅映射只读属性（读取不受影响），与 Linux 下 root 情形同类；故该 fixture 仅
        // POSIX 非 root 创建，期望集 "A,Tar,Upper" 在两平台一致成立。
#if !defined(_WIN32)
        if (::getuid() != 0) { // root 无视读权限，两套实现都会列出
            write_file(parity_dir / "noread.json",
                       R"({"version":1,"name":"NoRead","format":"jxl"})");
            fs::permissions(parity_dir / "noread.json", fs::perms::none, ec);
        }
#endif
        std::string got;
        for (const auto &[path, name] : ui::list_presets(parity_dir))
            got += (got.empty() ? "" : ",") + name;
        check(got == "A,Tar,Upper", c, "listing parity (hidden/case/dir/readable): [" + got + "]");
    }

    // 5) validate_preset：格式/后端/技术/位深/参数/版本
    case_begin("5) validate_preset：格式/后端/技术/位深/参数/版本");
    {
        const std::string c = "validate-preset";
        PresetData p;
        p.name = "ok";
        p.format_id = "jxl";
        p.backend_id = "libjxl";
        p.tech_id = "modular";
        p.lossless = true;
        p.out_bitdepth = 16;
        p.params = default_params(*find_format("jxl"), "libjxl", "modular", true);
        check(validate_preset(p).empty(), c, "clean preset should pass: " + validate_preset(p));

        PresetData bad = p;
        bad.format_id = "nope";
        check(!validate_preset(bad).empty(), c, "unknown format must fail");
        bad = p;
        bad.backend_id = "nope";
        check(validate_preset(bad).find("backend") != std::string::npos, c,
              "unknown backend must fail");
        bad = p;
        bad.tech_id = "nope";
        check(validate_preset(bad).find("tech") != std::string::npos, c, "unknown tech must fail");
        bad = p;
        bad.out_bitdepth = 12;
        check(validate_preset(bad).find("bitdepth") != std::string::npos, c,
              "bad bitdepth must fail");
        bad = p;
        bad.params["effort"] = int64_t(42);
        check(validate_preset(bad).find("effort") != std::string::npos, c,
              "out-of-range param must fail");
        bad = p;
        bad.params["modular_predictor"] = std::string("fast");
        check(validate_preset(bad).find("modular_predictor") != std::string::npos, c,
              "wrong param type must fail");
        bad = p;
        bad.version = 2;
        check(validate_preset(bad).find("version") != std::string::npos, c,
              "bad version must fail");

        // heif/avif：运行时内省参数 → 静态校验跳过技术/参数；位深按静态允许集校验（M1-T2b：
        // heif/avif = {8,10,12}，默认位深由调用方选，不在表内）
        PresetData h;
        h.name = "heif";
        h.format_id = "heif";
        h.backend_id = "x265";
        h.tech_id = "x265-main";
        h.out_bitdepth = 10;
        check(validate_preset(h).empty(), c,
              "runtime-introspected preset should pass: " + validate_preset(h));
        for (const int d : {8, 10, 12}) {
            h.out_bitdepth = d;
            check(validate_preset(h).empty(), c,
                  "heif bitdepth " + std::to_string(d) + " must pass: " + validate_preset(h));
        }
        PresetData a;
        a.name = "avif";
        a.format_id = "avif";
        a.backend_id = "libaom";
        a.out_bitdepth = 12;
        check(validate_preset(a).empty(), c, "avif bitdepth 12 must pass: " + validate_preset(a));
        a.backend_id = "svt-av1";
        check(validate_preset(a).empty(), c,
              "avif/svt-av1 bitdepth 12 is in the static allowed set (runtime probe intersects): " +
                  validate_preset(a));
        h.out_bitdepth = 16;
        check(validate_preset(h).find("bitdepth") != std::string::npos, c,
              "heif bitdepth 16 must fail");
        PresetData j;
        j.name = "jpeg";
        j.format_id = "jpeg";
        j.backend_id = "jpegli";
        j.tech_id = "dct";
        j.out_bitdepth = 12;
        check(validate_preset(j).find("bitdepth") != std::string::npos, c,
              "jpeg bitdepth 12 must fail");
    }

    // 6) normalize_preset：补齐后端/技术/参数 + 应用锁定
    case_begin("6) normalize_preset：补齐后端/技术/参数 + 应用锁定");
    {
        const std::string c = "normalize-preset";
        PresetData p;
        p.format_id = "jxl";
        p.lossless = true;
        p.out_bitdepth = 16;
        p.params["distance"] = 5.0;
        p.params["modular_lossy_palette"] = true;
        const std::vector<std::string> touched = normalize_preset(p);
        check(p.backend_id == "libjxl" && p.tech_id == "modular", c,
              "default backend/tech: " + p.backend_id + "/" + p.tech_id);
        check(contains(touched, "effort") && contains(touched, "color_transform"), c,
              "filled keys [" + join(touched) + "]");
        check(contains(touched, "distance") && contains(touched, "modular_lossy_palette"), c,
              "lock-rewritten keys [" + join(touched) + "]");
        check(param_float(p.params, "distance", -1) == 0.0 &&
                  !param_bool(p.params, "modular_lossy_palette", true),
              c, "locks not applied by normalize_preset");
        check(validate_preset(p).empty(), c,
              "normalized preset must validate: " + validate_preset(p));
        PresetData bad;
        bad.format_id = "nope";
        check(normalize_preset(bad).empty(), c, "unknown format → nothing to do");
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
        write_file(badcolor, R"({"version":1,"name":"x","format":"jxl","color_target":"cmyk"})");
        const std::string err = ui::load_preset(badcolor, out);
        check(err.find("color_target") != std::string::npos, c, "bad color_target: [" + err + "]");
        const fs::path badconf = root / "conflict.json";
        write_file(badconf, R"({"version":1,"name":"x","format":"jxl","conflict":"explode"})");
        check(ui::load_preset(badconf, out).find("conflict") != std::string::npos, c,
              "bad conflict");
        // 缺省字段 → 默认值（向前兼容）
        const fs::path minimal = root / "minimal.json";
        write_file(minimal, R"({"format":"png","name":"m"})");
        PresetData m;
        check(ui::load_preset(minimal, m).empty(), c, "minimal preset must load");
        check(m.version == 1 && m.out_bitdepth == 8 &&
                  m.color_target == ColorTarget::KeepOriginal &&
                  m.conflict == ConflictPolicy::Rename && m.params.empty(),
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
