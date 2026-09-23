// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M4-W1-T8 — 生效值预览（preview_effective）单元测试（手写断言）。
//
// 契约：docs/v0.3.0-design.md §3.5（冻结形态）+ §5.1（语义：`preview_effective()` 与
//   `build_plan()` **共用同一套合成逻辑** —— 抽纯函数，杜绝显示/写入两张皮）；
//   PP-FROZEN(0.3.0) 头见 core/metadata.h。
// 覆盖：三组时间（原值 / Δ / 时区语义）/ GPS（原值、规则覆盖、清除、隐私剥除）/ 例外逐项覆盖 /
//       ignore_batch / 不可解析值 / 无元数据源 / changed 口径 / 零副作用；
//       **同源对拍**：同输入下 preview_effective 的生效值与 build_plan 的产物逐字段一致
//       （本文件的核心断言 = "两张皮"检测）。
// 语料：tests/golden/meta/exif_full.jpg（真实容器：三个 EXIF 时间标签 + GPS）+
//       内存合成 SourceMeta（全字段/坏值/空值三态，零文件 IO）。
// 每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数。

#include <exiv2/exiv2.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/metadata.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &c, const std::string &d) {
    if (!ok) {
        std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
        ++g_failed;
    }
}

std::string show(const std::string &s) { return "'" + s + "'"; }
std::string show(const std::optional<pp::DateTimeVal> &v) {
    return v.has_value() ? show(v->value) : std::string("<none>");
}
std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return std::string(buf);
}

fs::path repo_root() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec))
            return p;
        if (!p.has_parent_path() || p.parent_path() == p)
            break;
        p = p.parent_path();
    }
    return {};
}

fs::path corpus() { return repo_root() / "tests" / "golden"; }

// ------------------------------------------------------------------ exiv2 helpers

std::string exif_str(const Exiv2::ExifData &d, const char *key) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end())
        return {};
    std::string v = it->toString();
    while (!v.empty() && v.back() == '\0')
        v.pop_back();
    return v;
}

bool has_exif(const Exiv2::ExifData &d, const char *key) {
    return d.findKey(Exiv2::ExifKey(key)) != d.end();
}

double rational_at(const Exiv2::ExifData &d, const char *key, std::size_t n) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end())
        return std::nan("");
    const Exiv2::Rational r = it->value().toRational(n);
    if (r.second == 0)
        return std::nan("");
    return static_cast<double>(r.first) / static_cast<double>(r.second);
}

// 测试侧**独立**的写路径读回（不复用生产的 gps_from_exif，避免"同源"退化为"同代码"）
std::optional<pp::GpsData> plan_gps(const pp::MetadataPlan &plan) {
    double d = 0, m = 0, s = 0;
    if (!has_exif(plan.exif, "Exif.GPSInfo.GPSLatitude") ||
        !has_exif(plan.exif, "Exif.GPSInfo.GPSLongitude"))
        return std::nullopt;
    pp::GpsData g;
    d = rational_at(plan.exif, "Exif.GPSInfo.GPSLatitude", 0);
    m = rational_at(plan.exif, "Exif.GPSInfo.GPSLatitude", 1);
    s = rational_at(plan.exif, "Exif.GPSInfo.GPSLatitude", 2);
    g.lat = d + m / 60.0 + s / 3600.0;
    d = rational_at(plan.exif, "Exif.GPSInfo.GPSLongitude", 0);
    m = rational_at(plan.exif, "Exif.GPSInfo.GPSLongitude", 1);
    s = rational_at(plan.exif, "Exif.GPSInfo.GPSLongitude", 2);
    g.lon = d + m / 60.0 + s / 3600.0;
    if (exif_str(plan.exif, "Exif.GPSInfo.GPSLatitudeRef") == "S")
        g.lat = -g.lat;
    if (exif_str(plan.exif, "Exif.GPSInfo.GPSLongitudeRef") == "W")
        g.lon = -g.lon;
    if (has_exif(plan.exif, "Exif.GPSInfo.GPSAltitude")) {
        const double alt = rational_at(plan.exif, "Exif.GPSInfo.GPSAltitude", 0);
        g.altitude = (exif_str(plan.exif, "Exif.GPSInfo.GPSAltitudeRef") == "1") ? -alt : alt;
    }
    if (has_exif(plan.exif, "Exif.GPSInfo.GPSImgDirection"))
        g.direction = rational_at(plan.exif, "Exif.GPSInfo.GPSImgDirection", 0);
    const std::string date = exif_str(plan.exif, "Exif.GPSInfo.GPSDateStamp");
    if (!date.empty() && has_exif(plan.exif, "Exif.GPSInfo.GPSTimeStamp")) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                      static_cast<int>(rational_at(plan.exif, "Exif.GPSInfo.GPSTimeStamp", 0)),
                      static_cast<int>(rational_at(plan.exif, "Exif.GPSInfo.GPSTimeStamp", 1)),
                      static_cast<int>(rational_at(plan.exif, "Exif.GPSInfo.GPSTimeStamp", 2)));
        g.timestamp = date + " " + buf;
    }
    return g;
}

// ------------------------------------------------------------------ builders

pp::TimeShift delta(int years = 0, int months = 0, int days = 0, int hours = 0, int minutes = 0,
                    int seconds = 0) {
    pp::TimeShift s;
    s.mode = pp::TimeShift::Mode::Delta;
    s.years = years;
    s.months = months;
    s.days = days;
    s.hours = hours;
    s.minutes = minutes;
    s.seconds = seconds;
    return s;
}

pp::TimeShift tz(int from_min, int to_min) {
    pp::TimeShift s;
    s.mode = pp::TimeShift::Mode::TimezoneSemantic;
    s.from_offset_min = from_min;
    s.to_offset_min = to_min;
    return s;
}

// 内存合成源：三个 EXIF 时间标签 + 完整 GPS（含高度/方向/时间戳）——零文件 IO
pp::SourceMeta synthetic_source() {
    pp::SourceMeta m;
    m.exif["Exif.Photo.DateTimeOriginal"] = std::string("2024:03:01 10:00:00");
    m.exif["Exif.Photo.DateTimeDigitized"] = std::string("2024:03:01 10:00:05");
    m.exif["Exif.Image.DateTime"] = std::string("2024:03:02 09:00:00");
    pp::GpsData g;
    g.lat = 31.2304;
    g.lon = 121.4737;
    g.altitude = 4.5;
    g.direction = 90.0;
    g.timestamp = std::string("2024:03:01 02:30:00");
    pp::write_gps(m.exif, g);
    m.has_time = true;
    m.has_gps = true;
    return m;
}

// ------------------------------------------------------------------ 同源对拍（两张皮检测）

using TimeField = pp::EffectiveField<std::optional<pp::DateTimeVal>>;

const char *const kTimeKeys[3] = {"Exif.Photo.DateTimeOriginal", "Exif.Photo.DateTimeDigitized",
                                  "Exif.Image.DateTime"};

// 同输入下：preview_effective 的**生效值** vs build_plan 的**产物**（时间三字段 + GPS +
// 隐私剥除）必须逐字段一致；任一处不一致 = 显示/写入两张皮（本任务的出口断言）。
void check_same_source(const pp::SourceMeta &src, const pp::BatchRules &rules,
                       const std::optional<pp::MetadataOverride> &ex, const std::string &cs) {
    const pp::EffectivePreview prev = pp::preview_effective(src, rules, ex);
    const pp::MetadataPlan plan = pp::build_plan(src, rules, ex);

    const TimeField *const fields[3] = {&prev.datetime_original, &prev.datetime_digitized,
                                        &prev.datetime_modify};
    for (int i = 0; i < 3; ++i) {
        const std::string got = exif_str(plan.exif, kTimeKeys[i]);
        const std::string want =
            fields[i]->effective.has_value() ? fields[i]->effective->value : std::string();
        check(got == want, cs + "/same-source-time/" + kTimeKeys[i],
              "preview=" + show(want) + " plan=" + show(got));
    }

    const std::optional<pp::GpsData> got = plan_gps(plan);
    check(got.has_value() == prev.gps.effective.has_value(), cs + "/same-source-gps-presence",
          std::string("preview=") + (prev.gps.effective.has_value() ? "yes" : "no") +
              " plan=" + (got.has_value() ? "yes" : "no"));
    if (got.has_value() && prev.gps.effective.has_value()) {
        check(std::fabs(got->lat - prev.gps.effective->lat) < 1e-6, cs + "/same-source-gps-lat",
              num(got->lat) + " vs " + num(prev.gps.effective->lat));
        check(std::fabs(got->lon - prev.gps.effective->lon) < 1e-6, cs + "/same-source-gps-lon",
              num(got->lon) + " vs " + num(prev.gps.effective->lon));
        check(got->altitude.has_value() == prev.gps.effective->altitude.has_value(),
              cs + "/same-source-gps-alt-presence", "altitude presence differs");
        if (got->altitude.has_value() && prev.gps.effective->altitude.has_value()) {
            check(std::fabs(*got->altitude - *prev.gps.effective->altitude) < 1e-3,
                  cs + "/same-source-gps-alt",
                  num(*got->altitude) + " vs " + num(*prev.gps.effective->altitude));
        }
        check(got->direction.has_value() == prev.gps.effective->direction.has_value(),
              cs + "/same-source-gps-dir-presence", "direction presence differs");
        check(got->timestamp == prev.gps.effective->timestamp, cs + "/same-source-gps-time",
              got->timestamp.value_or("<none>") + " vs " +
                  prev.gps.effective->timestamp.value_or("<none>"));
    }

    // 隐私剥除：预览说"将被移除"⇒ 写路径不得留下任何 EXIF/XMP
    if (prev.strip_privacy) {
        check(plan.exif.empty() && plan.xmp.empty(), cs + "/same-source-privacy-strip",
              "plan still carries metadata");
    }
    check(prev.sync_mtime == rules.sync_mtime, cs + "/sync-mtime-echo", "echo mismatch");
}

// ------------------------------------------------------------------ preview 比较（零副作用）

bool same_time_field(const TimeField &a, const TimeField &b) {
    return a.changed == b.changed && a.original == b.original && a.effective == b.effective;
}

bool same_gps_field(const pp::EffectiveField<std::optional<pp::GpsData>> &a,
                    const pp::EffectiveField<std::optional<pp::GpsData>> &b) {
    if (a.changed != b.changed || a.original.has_value() != b.original.has_value() ||
        a.effective.has_value() != b.effective.has_value()) {
        return false;
    }
    const auto same = [](const std::optional<pp::GpsData> &x, const std::optional<pp::GpsData> &y) {
        if (x.has_value() != y.has_value())
            return false;
        if (!x.has_value())
            return true;
        return x->lat == y->lat && x->lon == y->lon && x->altitude == y->altitude &&
               x->direction == y->direction && x->timestamp == y->timestamp;
    };
    return same(a.original, b.original) && same(a.effective, b.effective);
}

bool same_preview(const pp::EffectivePreview &a, const pp::EffectivePreview &b) {
    return same_time_field(a.datetime_original, b.datetime_original) &&
           same_time_field(a.datetime_digitized, b.datetime_digitized) &&
           same_time_field(a.datetime_modify, b.datetime_modify) && same_gps_field(a.gps, b.gps) &&
           a.strip_privacy == b.strip_privacy && a.sync_mtime == b.sync_mtime;
}

std::string dump(const pp::EffectivePreview &p) {
    return "t0=" + show(p.datetime_original.original) + "→" + show(p.datetime_original.effective) +
           " digitized=" + show(p.datetime_digitized.original) + "→" +
           show(p.datetime_digitized.effective) + " modify=" + show(p.datetime_modify.original) +
           "→" + show(p.datetime_modify.effective) +
           " gps=" + (p.gps.original.has_value() ? num(p.gps.original->lat) : "<none>") + "→" +
           (p.gps.effective.has_value() ? num(p.gps.effective->lat) : "<none>");
}

// ===========================================================================
// cases
// ===========================================================================

void test_fixture_original() {
    const pp::SourceMeta src = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");
    check(src.error.empty(), "fixture/read-ok", src.error);
    std::printf("info fixture times: dto=%s digitized=%s modify=%s gps=%s\n",
                show(exif_str(src.exif, "Exif.Photo.DateTimeOriginal")).c_str(),
                show(exif_str(src.exif, "Exif.Photo.DateTimeDigitized")).c_str(),
                show(exif_str(src.exif, "Exif.Image.DateTime")).c_str(),
                src.has_gps ? "yes" : "no");

    const pp::BatchRules rules; // 空规则
    const pp::EffectivePreview p = pp::preview_effective(src, rules, std::nullopt);
    // 金值：exif_full.jpg 的 DateTimeOriginal（tests/golden/smoke/exif-roundtrip.json 同源）
    check(p.datetime_original.original.has_value() &&
              p.datetime_original.original->value == "2024:03:01 10:00:00",
          "fixture/original-dto", show(p.datetime_original.original));
    check(p.datetime_original.effective == p.datetime_original.original, "fixture/no-rules-keep",
          dump(p));
    check(!p.datetime_original.changed, "fixture/no-rules-changed-false", dump(p));
    check(!p.strip_privacy && !p.sync_mtime, "fixture/flags-default", dump(p));

    // GPS 原值（金值：31°13'49.44" → 31.2304；121°28'25.32" → 121.4737）
    check(p.gps.original.has_value(), "fixture/gps-present", "expected GPS");
    if (p.gps.original.has_value()) {
        check(std::fabs(p.gps.original->lat - 31.2304) < 1e-4, "fixture/gps-lat",
              num(p.gps.original->lat));
        check(std::fabs(p.gps.original->lon - 121.4737) < 1e-4, "fixture/gps-lon",
              num(p.gps.original->lon));
    }
    check(p.gps.effective.has_value() && p.gps.original.has_value() &&
              p.gps.effective->lat == p.gps.original->lat && !p.gps.changed,
          "fixture/gps-no-rules-keep", dump(p));

    // 规则未配置 = 三字段全"未改变"
    check(!p.datetime_digitized.changed && !p.datetime_modify.changed && !p.gps.changed,
          "fixture/no-rules-all-unchanged", dump(p));
    check_same_source(src, rules, std::nullopt, "fixture/no-rules");

    // 零副作用 + 确定性：两次调用结果逐字段相同，源容器与源值不动
    const std::size_t n_exif = src.exif.count();
    const std::size_t n_xmp = src.xmp.count();
    const pp::EffectivePreview p2 = pp::preview_effective(src, rules, std::nullopt);
    const pp::EffectivePreview p3 = pp::preview_effective(src, rules, std::nullopt);
    check(src.exif.count() == n_exif && src.xmp.count() == n_xmp, "purity/containers-untouched",
          std::to_string(src.exif.count()) + " vs " + std::to_string(n_exif));
    check(exif_str(src.exif, "Exif.Photo.DateTimeOriginal") == "2024:03:01 10:00:00",
          "purity/source-value-untouched", exif_str(src.exif, "Exif.Photo.DateTimeOriginal"));
    check(same_preview(p2, p3), "purity/deterministic", dump(p2) + " vs " + dump(p3));
    check(same_preview(p, p2), "purity/repeatable", dump(p) + " vs " + dump(p2));
}

void test_fixture_delta() {
    const pp::SourceMeta src = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");

    pp::BatchRules rules;
    rules.time_shift = delta(/*years=*/0, 0, 0, /*hours=*/2, 0, 0);
    const pp::EffectivePreview p = pp::preview_effective(src, rules, std::nullopt);
    // 金值：10:00:00 + 2h = 12:00:00（Δ 语义走 shift_exif_datetime）
    check(p.datetime_original.effective.has_value() &&
              p.datetime_original.effective->value == "2024:03:01 12:00:00",
          "delta/dto-golden", show(p.datetime_original.effective));
    check(p.datetime_original.changed, "delta/changed-true", dump(p));
    check(p.datetime_original.original.has_value() &&
              p.datetime_original.original->value == "2024:03:01 10:00:00",
          "delta/original-preserved", show(p.datetime_original.original));
    check_same_source(src, rules, std::nullopt, "delta/2h");

    // 日历步进（月）+ 精确时长（天）混合：3 月 1 日 +1 月 = 4 月 1 日，再 -1 天 = 3 月 31 日
    pp::BatchRules cal;
    cal.time_shift = delta(0, /*months=*/1, /*days=*/-1, 0, 0, 0);
    const pp::EffectivePreview q = pp::preview_effective(src, cal, std::nullopt);
    check(q.datetime_original.effective.has_value() &&
              q.datetime_original.effective->value == "2024:03:31 10:00:00",
          "delta/month-day-golden", show(q.datetime_original.effective));
    check_same_source(src, cal, std::nullopt, "delta/month-day");

    // noop（全零）→ 无变化（不做无谓写入）
    pp::BatchRules noop;
    noop.time_shift = delta();
    const pp::EffectivePreview n = pp::preview_effective(src, noop, std::nullopt);
    check(n.datetime_original.effective == n.datetime_original.original &&
              !n.datetime_original.changed,
          "delta/noop-unchanged", dump(n));
    check_same_source(src, noop, std::nullopt, "delta/noop");
}

void test_fixture_timezone() {
    const pp::SourceMeta src = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");

    pp::BatchRules rules;
    rules.time_shift = tz(480, 540); // +08:00 → +09:00
    const pp::EffectivePreview p = pp::preview_effective(src, rules, std::nullopt);
    // 金值：墙钟 +1h（10:00:00 → 11:00:00；reinterpret_timezone 语义）
    check(p.datetime_original.effective.has_value() &&
              p.datetime_original.effective->value == "2024:03:01 11:00:00",
          "tz/dto-golden", show(p.datetime_original.effective));
    check(p.datetime_original.changed, "tz/changed-true", dump(p));
    check_same_source(src, rules, std::nullopt, "tz/+08-to+09");

    // 写路径的 OffsetTime* 与预览一致地反映目标时区（同一 TimeShift）
    const pp::MetadataPlan plan = pp::build_plan(src, rules, std::nullopt);
    check(exif_str(plan.exif, "Exif.Photo.OffsetTimeOriginal") == "+09:00",
          "tz/offset-time-written", exif_str(plan.exif, "Exif.Photo.OffsetTimeOriginal"));

    // from == to → noop（不写 OffsetTime*，预览无变化）
    pp::BatchRules same;
    same.time_shift = tz(480, 480);
    const pp::EffectivePreview s = pp::preview_effective(src, same, std::nullopt);
    check(!s.datetime_original.changed &&
              s.datetime_original.effective == s.datetime_original.original,
          "tz/same-offset-noop", dump(s));
    check_same_source(src, same, std::nullopt, "tz/same-offset");
}

void test_fixture_gps() {
    const pp::SourceMeta src = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");

    // ---- 规则覆盖（规则未配的字段 = 清空，写路径 write_gps 同口径）----
    pp::BatchRules rules;
    pp::GpsData g;
    g.lat = 22.5431;
    g.lon = 114.0579;
    rules.gps = g;
    const pp::EffectivePreview p = pp::preview_effective(src, rules, std::nullopt);
    check(p.gps.effective.has_value() && p.gps.effective->lat == 22.5431 &&
              p.gps.effective->lon == 114.0579,
          "gps/rule-effective", dump(p));
    check(!p.gps.effective->altitude.has_value() && !p.gps.effective->timestamp.has_value(),
          "gps/rule-optionals-empty", "规则未配的可选项必须为空");
    check(p.gps.changed, "gps/rule-changed", dump(p));
    check(p.gps.original.has_value() && std::fabs(p.gps.original->lat - 31.2304) < 1e-4,
          "gps/rule-original-preserved", dump(p));
    check_same_source(src, rules, std::nullopt, "gps/rule-set");

    // ---- 规则值与原值完全相同 → changed = false（§5.1：changed = (effective ≠ original)）----
    pp::BatchRules same;
    same.gps = p.gps.original;
    const pp::EffectivePreview sp = pp::preview_effective(src, same, std::nullopt);
    check(!sp.gps.changed && sp.gps.effective.has_value() && sp.gps.original.has_value() &&
              sp.gps.effective->lat == sp.gps.original->lat,
          "gps/same-value-unchanged", dump(sp));
    check_same_source(src, same, std::nullopt, "gps/same-value");

    // ---- gps_clear ----
    pp::BatchRules clear;
    clear.gps_clear = true;
    const pp::EffectivePreview c = pp::preview_effective(src, clear, std::nullopt);
    check(!c.gps.effective.has_value(), "gps/clear-effective-none", dump(c));
    check(c.gps.changed, "gps/clear-changed", dump(c));
    check_same_source(src, clear, std::nullopt, "gps/clear");

    // ---- gps_clear 与 gps 同时配置：清除优先（与 build_plan 的优先级逐字一致）----
    pp::BatchRules both;
    both.gps = g;
    both.gps_clear = true;
    const pp::EffectivePreview b = pp::preview_effective(src, both, std::nullopt);
    check(!b.gps.effective.has_value(), "gps/clear-wins", dump(b));
    check_same_source(src, both, std::nullopt, "gps/clear-wins");

    // ---- 无 GPS 源：effective 空（UI"无 GPS，规则将写入"）----
    pp::SourceMeta no_gps;
    no_gps.exif["Exif.Photo.DateTimeOriginal"] = std::string("2024:03:01 10:00:00");
    const pp::EffectivePreview ng = pp::preview_effective(no_gps, rules, std::nullopt);
    check(!ng.gps.original.has_value() && ng.gps.effective.has_value() &&
              ng.gps.effective->lat == 22.5431,
          "gps/no-source-rule-writes", dump(ng));
    check(ng.gps.changed, "gps/no-source-changed", dump(ng));
    check_same_source(no_gps, rules, std::nullopt, "gps/no-source-rule");
}

void test_fixture_privacy() {
    const pp::SourceMeta src = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");

    pp::BatchRules rules;
    rules.time_shift = delta(0, 0, 0, 2, 0, 0); // 与剥除同时：剥除优先（写入路径同）
    rules.gps = pp::GpsData{22.5, 114.0, std::nullopt, std::nullopt, std::nullopt};
    rules.strip_privacy = true;
    const pp::EffectivePreview p = pp::preview_effective(src, rules, std::nullopt);
    check(p.strip_privacy, "privacy/flag-echo", dump(p));
    check(!p.datetime_original.effective.has_value() &&
              !p.datetime_digitized.effective.has_value() &&
              !p.datetime_modify.effective.has_value(),
          "privacy/time-effective-none", dump(p));
    check(!p.gps.effective.has_value(), "privacy/gps-effective-none", dump(p));
    check(p.datetime_original.changed && p.gps.changed, "privacy/changed-when-original-exists",
          dump(p));
    // 原值仍如实带出（UI 显示"原 → 将被移除"）
    check(p.datetime_original.original.has_value(), "privacy/original-kept",
          show(p.datetime_original.original));
    check_same_source(src, rules, std::nullopt, "privacy/strip");

    // 例外把批量剥除关掉 → 时间/GPS 按批量规则生效（三态覆盖）
    pp::MetadataOverride ex;
    ex.strip_privacy = false;
    const pp::EffectivePreview q = pp::preview_effective(src, rules, ex);
    check(!q.strip_privacy, "privacy/exception-off", dump(q));
    check(q.datetime_original.effective.has_value() &&
              q.datetime_original.effective->value == "2024:03:01 12:00:00",
          "privacy/exception-off-time", show(q.datetime_original.effective));
    check(q.gps.effective.has_value() && q.gps.effective->lat == 22.5, "privacy/exception-off-gps",
          dump(q));
    check_same_source(src, rules, ex, "privacy/exception-off");

    // 例外把批量剥除打开 → 全空
    pp::BatchRules plain;
    plain.time_shift = delta(0, 0, 0, 1, 0, 0);
    pp::MetadataOverride on;
    on.strip_privacy = true;
    const pp::EffectivePreview r = pp::preview_effective(src, plain, on);
    check(r.strip_privacy && !r.datetime_original.effective.has_value() &&
              !r.gps.effective.has_value(),
          "privacy/exception-on", dump(r));
    check_same_source(src, plain, on, "privacy/exception-on");

    // sync_mtime 回显（MetadataOverride 无该三态 → 恒 = rules）
    pp::BatchRules mtime;
    mtime.sync_mtime = true;
    check(pp::preview_effective(src, mtime, std::nullopt).sync_mtime, "privacy/sync-mtime-echo",
          "expected true");
    check_same_source(src, mtime, std::nullopt, "privacy/sync-mtime");
}

void test_exception_overrides() {
    const pp::SourceMeta src = synthetic_source(); // 三字段齐备 + 完整 GPS

    pp::BatchRules rules;
    rules.time_shift = delta(0, 0, 0, 2, 0, 0);
    rules.gps = pp::GpsData{22.5, 114.0, std::nullopt, std::nullopt, std::nullopt};

    // 逐项覆盖：例外的时间/GPS 覆盖批量
    pp::MetadataOverride ex;
    ex.time_shift = delta(0, 0, 0, -1, 0, 0);
    pp::GpsData g2;
    g2.lat = 39.9042;
    g2.lon = 116.4074;
    ex.gps = g2;
    const pp::EffectivePreview p = pp::preview_effective(src, rules, ex);
    check(p.datetime_original.effective.has_value() &&
              p.datetime_original.effective->value == "2024:03:01 09:00:00",
          "exception/time-override", show(p.datetime_original.effective));
    check(p.datetime_digitized.effective.has_value() &&
              p.datetime_digitized.effective->value == "2024:03:01 09:00:05",
          "exception/digitized-override", show(p.datetime_digitized.effective));
    check(p.gps.effective.has_value() && p.gps.effective->lat == 39.9042, "exception/gps-override",
          dump(p));
    check_same_source(src, rules, ex, "exception/override");

    // ignore_batch：整单忽略批量（例外自身的时间照常生效；批量 GPS 不落）
    pp::MetadataOverride ig;
    ig.ignore_batch = true;
    ig.time_shift = delta(0, 0, 0, 1, 0, 0);
    const pp::EffectivePreview q = pp::preview_effective(src, rules, ig);
    check(q.datetime_original.effective.has_value() &&
              q.datetime_original.effective->value == "2024:03:01 11:00:00",
          "exception/ignore-batch-shift", show(q.datetime_original.effective));
    check(q.gps.effective.has_value() && q.gps.original.has_value() &&
              q.gps.effective->lat == q.gps.original->lat,
          "exception/ignore-batch-gps-keeps-original", dump(q));
    check(!q.gps.changed, "exception/ignore-batch-gps-unchanged", dump(q));
    check_same_source(src, rules, ig, "exception/ignore-batch");

    // 例外 gps_clear（批量 gps 覆盖不了它：清除优先，与写路径同）
    pp::MetadataOverride clr;
    clr.gps_clear = true;
    const pp::EffectivePreview c = pp::preview_effective(src, rules, clr);
    check(!c.gps.effective.has_value(), "exception/gps-clear-wins", dump(c));
    check_same_source(src, rules, clr, "exception/gps-clear");

    // 批量 gps_clear + 例外 gps：build_plan 的 gps_clear 仍然优先（预览必须同结论）
    pp::BatchRules cleared;
    cleared.gps_clear = true;
    pp::MetadataOverride ex_gps;
    ex_gps.gps = g2;
    const pp::EffectivePreview cg = pp::preview_effective(src, cleared, ex_gps);
    check(!cg.gps.effective.has_value(), "exception/clear-beats-exception-gps", dump(cg));
    check_same_source(src, cleared, ex_gps, "exception/clear-beats-exception-gps");
}

void test_synthetic_matrix() {
    const pp::SourceMeta src = synthetic_source();

    // 三字段齐备 + Δ → 三个字段各自生效（对拍计划产物）
    pp::BatchRules rules;
    rules.time_shift = delta(0, 0, 0, 1, 30, 0);
    const pp::EffectivePreview p = pp::preview_effective(src, rules, std::nullopt);
    check(p.datetime_original.effective.has_value() &&
              p.datetime_original.effective->value == "2024:03:01 11:30:00",
          "synthetic/dto", show(p.datetime_original.effective));
    check(p.datetime_digitized.effective.has_value() &&
              p.datetime_digitized.effective->value == "2024:03:01 11:30:05",
          "synthetic/digitized", show(p.datetime_digitized.effective));
    check(p.datetime_modify.effective.has_value() &&
              p.datetime_modify.effective->value == "2024:03:02 10:30:00",
          "synthetic/modify", show(p.datetime_modify.effective));
    check(p.datetime_original.changed && p.datetime_digitized.changed && p.datetime_modify.changed,
          "synthetic/all-changed", dump(p));
    check_same_source(src, rules, std::nullopt, "synthetic/delta");

    // 时区语义（+08:00 → -05:00 = -13h）：跨日回退 2024-02-29（闰年）
    pp::BatchRules tzs;
    tzs.time_shift = tz(480, -300);
    const pp::EffectivePreview t = pp::preview_effective(src, tzs, std::nullopt);
    check(t.datetime_original.effective.has_value() &&
              t.datetime_original.effective->value == "2024:02:29 21:00:00",
          "synthetic/tz-cross-day", show(t.datetime_original.effective));
    check_same_source(src, tzs, std::nullopt, "synthetic/tz-cross-day");

    // GPS 规则含可选项（高度/方向/时间戳）→ 写路径逐个落盘、预览同值
    pp::BatchRules gps_rules;
    pp::GpsData g;
    g.lat = -33.8688;
    g.lon = 151.2093;
    g.altitude = 12.5;
    g.direction = 270.25;
    g.timestamp = std::string("2024:03:01 02:30:00");
    gps_rules.gps = g;
    const pp::EffectivePreview pg = pp::preview_effective(src, gps_rules, std::nullopt);
    check(pg.gps.effective.has_value() && pg.gps.effective->lat == -33.8688 &&
              pg.gps.effective->lon == 151.2093 && pg.gps.effective->altitude.has_value() &&
              *pg.gps.effective->altitude == 12.5 && pg.gps.effective->direction.has_value() &&
              *pg.gps.effective->direction == 270.25 && pg.gps.effective->timestamp.has_value(),
          "synthetic/gps-optionals", dump(pg));
    check_same_source(src, gps_rules, std::nullopt, "synthetic/gps-optionals");

    // 南纬/西经符号（*Ref 定号）在两条路径上一致
    check(pg.gps.original.has_value() && pg.gps.original->lat > 0 && pg.gps.original->lon > 0,
          "synthetic/source-north-east", dump(pg));

    // 不可解析的时间值：不猜值 —— 预览"无变化"，写路径同样跳过
    pp::SourceMeta broken;
    broken.exif["Exif.Photo.DateTimeOriginal"] = std::string("not a date");
    broken.exif["Exif.Photo.DateTimeDigitized"] = std::string("2024:13:45 99:99:99");
    pp::BatchRules shift;
    shift.time_shift = delta(0, 0, 0, 1, 0, 0);
    const pp::EffectivePreview b = pp::preview_effective(broken, shift, std::nullopt);
    check(b.datetime_original.original.has_value() &&
              b.datetime_original.original->value == "not a date",
          "synthetic/unparsable-original-kept", show(b.datetime_original.original));
    check(b.datetime_original.effective == b.datetime_original.original &&
              !b.datetime_original.changed,
          "synthetic/unparsable-unchanged", dump(b));
    check(b.datetime_digitized.effective == b.datetime_digitized.original,
          "synthetic/invalid-date-unchanged", dump(b));
    check_same_source(broken, shift, std::nullopt, "synthetic/unparsable");

    // 无元数据源（probe 失败/空容器）：三字段全空 + 无 GPS，changed 全 false
    pp::SourceMeta empty;
    const pp::EffectivePreview e = pp::preview_effective(empty, shift, std::nullopt);
    check(!e.datetime_original.original.has_value() && !e.datetime_original.effective.has_value() &&
              !e.datetime_digitized.changed && !e.datetime_modify.changed && !e.gps.changed &&
              !e.gps.original.has_value() && !e.gps.effective.has_value(),
          "synthetic/empty-source", dump(e));
    check_same_source(empty, shift, std::nullopt, "synthetic/empty-source");

    // 编辑列表（非时间/GPS 规则）不影响时间/GPS 生效值，但写路径照常落编辑
    pp::BatchRules edits;
    edits.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("M4-T8"), false});
    edits.xmp_edits.push_back(pp::TagEdit{"Xmp.dc.title", std::string("标题"), false});
    const pp::EffectivePreview ed = pp::preview_effective(src, edits, std::nullopt);
    check(!ed.datetime_original.changed && !ed.gps.changed, "synthetic/edits-keep-time-gps",
          dump(ed));
    check_same_source(src, edits, std::nullopt, "synthetic/edits-only");
}

} // namespace

int main() {
    test_fixture_original();
    test_fixture_delta();
    test_fixture_timezone();
    test_fixture_gps();
    test_fixture_privacy();
    test_exception_overrides();
    test_synthetic_matrix();

    if (g_failed == 0) {
        std::printf("test_effective: OK\n");
        return 0;
    }
    std::printf("test_effective: FAILED (%d)\n", g_failed);
    return g_failed;
}
