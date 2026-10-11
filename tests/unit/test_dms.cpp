// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6-T10 — 坐标显示格式化单测（手写断言；main() 返回失败数）。
//
// Contract: docs/m6-map-upgrade.md §4.5（坐标基准透明化 R4）+ §6（边界精度）+ 任务书 ④。
// Qt Core only（无 QApplication），链接 pp_core；每失败打印 "FAIL <case>: <detail>"。
//
// 覆盖：
//   1) deg_to_dms / dms_to_deg 往返（3 组中国点 + 2 组境外点；lat/lon 两向）；
//   2) 格式正确性（N/S/E/W 前缀、° 与 ' 符号、分 3 位小数；逐字对齐 dms.h 的示例字符串）；
//   3) 逆解析兼容（三形态 + 纯十进制度 + 非法输入 → nullopt）；
//   4) 粗盒边界带：北京 = false、边界点 (55.8, 137.8) = true、纽约 = false（境外不提示）。

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include <QString>

#include "mapwidget/coord.h" // R-307：粗包围盒常量单源（kChinaBBox*）
#include "mapwidget/dms.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string q(const QString &s) { return s.toStdString(); }

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

struct City {
    const char *name;
    double lat;
    double lon;
};

// 3 组中国点（粗盒内深处）+ 2 组境外点（粗盒外：往返/格式都必须成立，与基准面无关）
const City kCities[] = {
    {"beijing", 39.9042, 116.4074},
    {"shanghai", 31.2304, 121.4737},
    {"shenzhen", 22.5431, 114.0579},
};
const City kOutside[] = {
    {"new-york", 40.7128, -74.0060},
    {"sydney", -33.8688, 151.2093},
};

// 显示最小刻度 = 千分之一分 = 1/60000° ≈ 1.67e-5°（≈1.85 m）→ 往返容差取 1.7e-5（约一个刻度）
constexpr double kRoundTripToleranceDeg = 1.7e-5;

bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

} // namespace

int main() {
    using pp::map::coord_precision_suspect;
    using pp::map::deg_to_dms;
    using pp::map::dms_to_deg;

    // ---- 1. 往返：deg → "N 39°54.252'" → deg 必须回到起点（≤ 一个显示刻度）----
    for (const City &city : kCities) {
        const QString lat_text = deg_to_dms(city.lat, true);
        const QString lon_text = deg_to_dms(city.lon, false);
        const std::optional<double> lat_back = dms_to_deg(lat_text);
        const std::optional<double> lon_back = dms_to_deg(lon_text);
        const std::string name = std::string("roundtrip/") + city.name;
        check(lat_back.has_value() && near(*lat_back, city.lat, kRoundTripToleranceDeg),
              name + "/lat",
              "lat=" + num(city.lat) + " text=\"" + q(lat_text) +
                  "\" back=" + (lat_back.has_value() ? num(*lat_back) : std::string("<nullopt>")));
        check(lon_back.has_value() && near(*lon_back, city.lon, kRoundTripToleranceDeg),
              name + "/lon",
              "lon=" + num(city.lon) + " text=\"" + q(lon_text) +
                  "\" back=" + (lon_back.has_value() ? num(*lon_back) : std::string("<nullopt>")));
    }
    for (const City &city : kOutside) {
        const QString lat_text = deg_to_dms(city.lat, true);
        const QString lon_text = deg_to_dms(city.lon, false);
        const std::optional<double> lat_back = dms_to_deg(lat_text);
        const std::optional<double> lon_back = dms_to_deg(lon_text);
        const std::string name = std::string("roundtrip/") + city.name;
        check(lat_back.has_value() && near(*lat_back, city.lat, kRoundTripToleranceDeg),
              name + "/lat",
              "lat=" + num(city.lat) + " text=\"" + q(lat_text) +
                  "\" back=" + (lat_back.has_value() ? num(*lat_back) : std::string("<nullopt>")));
        check(lon_back.has_value() && near(*lon_back, city.lon, kRoundTripToleranceDeg),
              name + "/lon",
              "lon=" + num(city.lon) + " text=\"" + q(lon_text) +
                  "\" back=" + (lon_back.has_value() ? num(*lon_back) : std::string("<nullopt>")));
    }

    // ---- 2. 格式正确性：逐字对齐 dms.h 示例（度 + 十进制分 3 位 + N/S/E/W 前缀）----
    struct Expected {
        const char *case_name;
        double value;
        bool is_lat;
        const char *text;
    };
    const Expected kExpected[] = {
        {"format/beijing-lat", 39.9042, true, "N 39°54.252'"},
        {"format/beijing-lon", 116.4074, false, "E 116°24.444'"}, // = dms.h 示例逐字
        {"format/sydney-lat", -33.8688, true, "S 33°52.128'"},
        {"format/new-york-lon", -74.0060, false, "W 74°0.360'"},
        {"format/origin", 0.0, true, "N 0°0.000'"},
    };
    for (const Expected &want : kExpected) {
        const QString got = deg_to_dms(want.value, want.is_lat);
        // fromUtf8：字面量是 UTF-8 源（° = U+00B0）；QLatin1String 会把两个 UTF-8 字节
        // 逐字节解释成两个 Latin-1 字符 → 假不等
        check(got == QString::fromUtf8(want.text), want.case_name,
              "got \"" + q(got) + "\" want \"" + std::string(want.text) + "\"");
    }
    // 符号面：前缀字母 + 空格 + 度符号 + 分标记（8 个采样点逐一核对，不依赖逐字表）
    for (const City &city : kCities) {
        for (const bool is_lat : {true, false}) {
            const double value = is_lat ? city.lat : city.lon;
            const QString text = deg_to_dms(value, is_lat);
            const QChar hemi = text.at(0);
            const bool hemi_ok = is_lat ? (hemi == QLatin1Char('N') || hemi == QLatin1Char('S'))
                                        : (hemi == QLatin1Char('E') || hemi == QLatin1Char('W'));
            check(hemi_ok && text.size() > 2 && text.at(1) == QLatin1Char(' ') &&
                      text.contains(QChar(0x00B0)) && text.endsWith(QLatin1Char('\'')),
                  std::string("symbols/") + city.name + (is_lat ? "/lat" : "/lon"),
                  "text \"" + q(text) + "\"");
        }
    }
    // 非有限值：不成坐标 → 空串（不抛、不猜）
    check(deg_to_dms(std::nan(""), true).isEmpty(), "format/nan",
          "NaN 应返回空串，got \"" + q(deg_to_dms(std::nan(""), true)) + "\"");

    // ---- 3. 逆解析：三形态兼容 + 纯十进制度 + 非法输入 → nullopt ----
    struct Parse {
        const char *case_name;
        const char *text;
        double want;
        double eps;
    };
    const Parse kParses[] = {
        {"parse/prefix", "N 39°54.246'", 39.9041, 1e-9},
        {"parse/suffix", "39°54.246'N", 39.9041, 1e-9},
        {"parse/seconds", "N 39°54'14.8\"", 39.9041111111111, 1e-9},
        {"parse/plain", "39.9042", 39.9042, 1e-12},
        {"parse/plain-negative", "-74.006", -74.006, 1e-12},
        {"parse/south-west", "S 33°52.128'", -33.8688, 1e-9},
        {"parse/degrees-only", "0°", 0.0, 1e-12},
        {"parse/roundtrip-text", "E 116°24.444'", 116.4074, 1e-9},
    };
    for (const Parse &want : kParses) {
        const std::optional<double> got = dms_to_deg(QString::fromUtf8(want.text));
        check(got.has_value() && near(*got, want.want, want.eps), want.case_name,
              "\"" + std::string(want.text) + "\" → " +
                  (got.has_value() ? num(*got) : std::string("<nullopt>")) + " want " +
                  num(want.want));
    }
    const char *kInvalid[] = {
        "",
        "   ",
        "abc",
        "N",
        "39°70.000'",
        "39°54.246'x",
        "N 95°0.000'",
        "E 181°0.000'",
        "N -39°0.000'",
        "39.5°30.5",
        "N 39°54'-1.0\"",
    };
    for (const char *bad : kInvalid) {
        const std::optional<double> got = dms_to_deg(QString::fromUtf8(bad));
        check(!got.has_value(), std::string("parse/reject/") + (bad[0] == '\0' ? "<empty>" : bad),
              "应拒绝，got " + (got.has_value() ? num(*got) : std::string("<nullopt>")));
    }

    // ---- 4. 粗盒边界带（coord.cpp out_of_china 同款矩形；盒内近界 = true）----
    struct Sample {
        const char *case_name;
        double lat;
        double lon;
        bool suspect;
    };
    const Sample kSamples[] = {
        {"box/beijing", 39.9042, 116.4074, false},  // 盒内深处 → 正常
        {"box/shenzhen", 22.5431, 114.0579, false}, // 盒内深处 → 正常
        {"box/new-york", 40.7128, -74.0060, false}, // 盒外（境外）→ false
        {"box/equator", 0.0, 0.0, false},           // 盒外 → false
        {"box/just-outside", 55.9, 138.0, false},   // 盒外贴边 → false（境外不提示）
        {"box/west-edge", 30.0, 72.0, false},       // 盒外（lon 72.0 < 72.004）→ false
        {"box/ne-corner-band", 55.8, 137.8, true},  // 盒内边缘一带（lat/lon 双近界）→ true
        {"box/north-band", 55.4, 116.0, true},      // 盒内距北界 0.4271° → true
        {"box/north-deep", 55.2, 116.0, false},     // 盒内距北界 0.6271° → false（带外）
        {"box/band-edge", 55.3271, 116.0, true},    // 距北界 = 0.5°（含界）→ true
        {"box/west-band", 30.0, 72.5, true},        // 盒内距西界 0.496° → true
    };
    for (const Sample &want : kSamples) {
        const bool got = coord_precision_suspect(want.lat, want.lon);
        check(got == want.suspect, want.case_name,
              "got " + std::string(got ? "true" : "false") + " want " +
                  (want.suspect ? "true" : "false") + " at (" + num(want.lat) + ", " +
                  num(want.lon) + ")");
    }
    check(!coord_precision_suspect(std::nan(""), 116.0), "box/nan-lat", "NaN 不应判存疑");
    check(!coord_precision_suspect(39.9, std::nan("")), "box/nan-lon", "NaN 不应判存疑");

    // ---- 5. R-307 常量单源（M7-WP7 只增断言）：coord.h 的 kChinaBBox* = 本文件边界带判定
    //      的同一盒（此前 coord.cpp 与 dms.cpp 双份字面量），值与文档口径逐字一致 ----
    check(pp::map::kChinaBBoxLonMin == 72.004 && pp::map::kChinaBBoxLonMax == 137.8347 &&
              pp::map::kChinaBBoxLatMin == 0.8293 && pp::map::kChinaBBoxLatMax == 55.8271,
          "bbox/single-source",
          "kChinaBBox* 值漂移: " + num(pp::map::kChinaBBoxLonMin) + "/" +
              num(pp::map::kChinaBBoxLonMax) + "/" + num(pp::map::kChinaBBoxLatMin) + "/" +
              num(pp::map::kChinaBBoxLatMax));
    // 由常量推导的边界带样本（与 box/band-edge 同判据、不同来源 —— 钉住两处同一盒）
    check(coord_precision_suspect(pp::map::kChinaBBoxLatMax - 0.5, 116.0), "bbox/derived-band",
          "距北界恰 0.5°（含界）应判存疑");
    check(!coord_precision_suspect(pp::map::kChinaBBoxLatMax - 0.6, 116.0), "bbox/derived-deep",
          "距北界 0.6°（带外）不应判存疑");

    if (g_failed == 0) {
        std::printf(
            "test_dms: OK (%zu cities roundtrip, %zu formats, %zu parses, %zu box samples)\n",
            sizeof(kCities) / sizeof(kCities[0]) + sizeof(kOutside) / sizeof(kOutside[0]),
            sizeof(kExpected) / sizeof(kExpected[0]), sizeof(kParses) / sizeof(kParses[0]),
            sizeof(kSamples) / sizeof(kSamples[0]));
        return 0;
    }
    std::printf("test_dms: FAILED (%d assertion(s))\n", g_failed);
    return g_failed;
}
