// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M13 — elevation unit tests (hand-written assertions).
//
// Contract: docs/m13-tasks.md §7 C5 provider/URL/parse fact table (frozen with
// src/mapwidget/elevation.*). **No real network** — pure functions only.
// Qt Core only (no QApplication). Every failure prints "FAIL <case>: <detail>";
// main() returns the number of failures.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "mapwidget/elevation.h"

namespace {

using pp::map::ElevationProvider;

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string q(const QString &s) { return s.toStdString(); }

bool near_eq(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

} // namespace

int run_cases() {
    // ---- provider 事实表（两档；默认 open-meteo）----
    const ElevationProvider *meteo =
        pp::map::elevation_provider_by_id(QStringLiteral("open-meteo"));
    const ElevationProvider *topo =
        pp::map::elevation_provider_by_id(QStringLiteral("opentopodata"));
    check(meteo != nullptr && topo != nullptr, "registry/ids",
          "elevation_provider_by_id 对 open-meteo/opentopodata 必须非空");
    check(pp::map::elevation_provider_by_id(QStringLiteral("bogus")) == nullptr, "registry/unknown",
          "未知 id 应返回 nullptr");
    check(pp::map::elevation_provider_ids() == QStringList({"open-meteo", "opentopodata"}),
          "registry/order",
          "elevation_provider_ids 顺序/内容漂移: " +
              q(pp::map::elevation_provider_ids().join(QLatin1Char(','))));
    const ElevationProvider *default_provider = pp::map::elevation_default_provider();
    check(default_provider != nullptr && default_provider->id == QLatin1String("open-meteo"),
          "registry/default", "默认海拔源必须是 open-meteo");
    if (meteo == nullptr || topo == nullptr || default_provider == nullptr) {
        std::printf("test_elevation: FAILED (provider registry returned null)\n");
        return g_failed;
    }
    check(meteo->label == QStringLiteral("Open-Meteo") &&
              meteo->endpoint == QStringLiteral("https://api.open-meteo.com/v1/elevation") &&
              meteo->open_meteo,
          "open-meteo/fields", "id/label/endpoint/响应形态漂移");
    check(topo->label == QStringLiteral("OpenTopoData") &&
              topo->endpoint == QStringLiteral("https://api.opentopodata.org/v1/srtm30m") &&
              !topo->open_meteo,
          "opentopodata/fields", "id/label/endpoint/响应形态漂移");

    // ---- URL 事实表（逐字节；坐标 6 位定点、纬度在前）----
    {
        const QUrl url = pp::map::elevation_url(QStringLiteral("open-meteo"), 31.2304, 121.4737);
        check(url.isValid() && url.scheme() == QLatin1String("https") &&
                  url.host() == QLatin1String("api.open-meteo.com") &&
                  url.path() == QLatin1String("/v1/elevation"),
              "open-meteo/url/scheme-host-path", "got " + q(url.toString()));
        check(url.query() == QLatin1String("latitude=31.230400&longitude=121.473700"),
              "open-meteo/url/query", "query 逐字漂移: " + q(url.query()));
        check(url.toString() ==
                  QLatin1String(
                      "https://api.open-meteo.com/v1/elevation?latitude=31.230400&longitude="
                      "121.473700"),
              "open-meteo/url/bytes", "URL 逐字漂移: " + q(url.toString()));
    }
    {
        // 负坐标 / 零坐标：定点格式化（负零不出现 "-0.000000"），纬度恒在经度前。
        const QUrl south = pp::map::elevation_url(QStringLiteral("open-meteo"), -33.8688, 151.2093);
        check(south.query() == QLatin1String("latitude=-33.868800&longitude=151.209300"),
              "open-meteo/url/negative", "query 漂移: " + q(south.query()));
        const QUrl zero = pp::map::elevation_url(QStringLiteral("open-meteo"), 0.0, 0.0);
        check(
            zero.toString() ==
                QLatin1String("https://api.open-meteo.com/v1/elevation?latitude=0.000000&longitude="
                              "0.000000"),
            "open-meteo/url/zero", "零坐标 URL 漂移: " + q(zero.toString()));
        // 第 7 位小数四舍五入进 6 位定点（1.23456789 → 1.234568）。
        const QUrl rounded = pp::map::elevation_url(QStringLiteral("open-meteo"), 1.23456789, -0.0);
        check(rounded.query() == QLatin1String("latitude=1.234568&longitude=0.000000"),
              "open-meteo/url/round", "6 位定点四舍五入漂移: " + q(rounded.query()));
    }
    {
        const QUrl url = pp::map::elevation_url(QStringLiteral("opentopodata"), 31.2304, 121.4737);
        check(url.isValid() && url.scheme() == QLatin1String("https") &&
                  url.host() == QLatin1String("api.opentopodata.org") &&
                  url.path() == QLatin1String("/v1/srtm30m"),
              "opentopodata/url/scheme-host-path", "got " + q(url.toString()));
        check(url.query() == QLatin1String("locations=31.230400,121.473700"),
              "opentopodata/url/query", "query 逐字漂移: " + q(url.query()));
        check(url.toString() == QLatin1String("https://api.opentopodata.org/v1/srtm30m?locations="
                                              "31.230400,121.473700"),
              "opentopodata/url/bytes", "URL 逐字漂移: " + q(url.toString()));
    }
    // 未知源 / 非法坐标 → 空 QUrl（调用方据此不发请求）。
    check(!pp::map::elevation_url(QStringLiteral("bogus"), 31.0, 121.0).isValid(),
          "url/unknown-provider", "未知 provider_id 应返回空 QUrl");
    for (const auto &[lat, lon] : {std::pair<double, double>{90.000001, 0.0},
                                   {-90.000001, 0.0},
                                   std::pair<double, double>{0.0, 180.000001},
                                   {0.0, -180.000001}}) {
        const QUrl url = pp::map::elevation_url(QStringLiteral("open-meteo"), lat, lon);
        check(!url.isValid(), "url/out-of-range",
              "越界坐标应返回空 QUrl: " + std::to_string(lat) + "," + std::to_string(lon));
    }
    for (const double nan : {std::nan(""), std::numeric_limits<double>::infinity()}) {
        check(!pp::map::elevation_url(QStringLiteral("open-meteo"), nan, 0.0).isValid() &&
                  !pp::map::elevation_url(QStringLiteral("open-meteo"), 0.0, nan).isValid(),
              "url/non-finite", "NaN/Inf 坐标应返回空 QUrl");
    }
    // 边界值（±90/±180）合法。
    check(pp::map::elevation_url(QStringLiteral("open-meteo"), 90.0, -180.0).isValid(),
          "url/boundary-valid", "边界坐标 ±90/±180 应合法");

    // ---- open-meteo 解析：{"elevation":[数值]} ----
    {
        double value = 0.0;
        QString err;
        check(pp::map::parse_elevation(QStringLiteral("open-meteo"),
                                       QByteArrayLiteral(R"({"elevation":[38.5]})"), &value,
                                       &err) &&
                  near_eq(value, 38.5) && err.isEmpty(),
              "open-meteo/parse/ok", "38.5 解析失败: " + q(err));
    }
    {
        // 整数 JSON 数值（isDouble 覆盖整型）+ 额外字段忽略 + 多元素取首点。
        double value = 0.0;
        QString err;
        const QByteArray body =
            QByteArrayLiteral(R"({"elevation":[12,99],"generationtime_ms":0.123})");
        check(pp::map::parse_elevation(QStringLiteral("open-meteo"), body, &value, &err) &&
                  near_eq(value, 12.0),
              "open-meteo/parse/int-and-extra", "整数/额外字段解析失败: " + q(err));
    }
    {
        double value = 0.0;
        QString err;
        const QByteArray body = QByteArrayLiteral(R"({"elevation":[-430.2]})");
        check(pp::map::parse_elevation(QStringLiteral("open-meteo"), body, &value, &err) &&
                  near_eq(value, -430.2),
              "open-meteo/parse/negative", "负海拔解析失败: " + q(err));
    }

    // ---- open-meteo 解析失败矩阵（false + 明确中文原因 + 不改写 *out）----
    {
        struct Case {
            const char *case_name;
            const char *body;
            const char *expect_error;
        };
        const Case cases[] = {
            {"open-meteo/err/empty", "", "响应不是合法 JSON 对象"},
            {"open-meteo/err/not-json", "not json", "响应不是合法 JSON 对象"},
            {"open-meteo/err/broken-json", R"({"elevation":[})", "响应不是合法 JSON 对象"},
            {"open-meteo/err/array-root", "[]", "响应不是合法 JSON 对象"},
            {"open-meteo/err/no-field", "{}", "响应缺少 elevation 数组"},
            {"open-meteo/err/scalar-field", R"({"elevation":38})", "响应缺少 elevation 数组"},
            {"open-meteo/err/empty-array", R"({"elevation":[]})", "elevation 数组为空"},
            {"open-meteo/err/null-value", R"({"elevation":[null]})",
             "响应缺少数值型 elevation 字段"},
            {"open-meteo/err/string-value", R"({"elevation":["38"]})",
             "响应缺少数值型 elevation 字段"},
            {"open-meteo/err/error-body", R"({"error":true,"reason":"invalid"})",
             "响应缺少 elevation 数组"},
        };
        for (const Case &test_case : cases) {
            double value = 123.0;
            QString err;
            const bool ok = pp::map::parse_elevation(QStringLiteral("open-meteo"),
                                                     QByteArray(test_case.body), &value, &err);
            check(!ok, test_case.case_name, "非法响应不应解析成功");
            check(err == QString::fromUtf8(test_case.expect_error), test_case.case_name,
                  "错误文案漂移: " + q(err));
            check(near_eq(value, 123.0), test_case.case_name, "失败时不得改写 *out");
        }
    }

    // ---- opentopodata 解析：{"results":[{"elevation":数值,...}]} ----
    {
        double value = 0.0;
        QString err;
        const QByteArray body = QByteArrayLiteral(
            R"({"results":[{"elevation":38.0,"location":{"lat":31.2304,"lng":121.4737},"dataset":"srtm30m"}]})");
        check(pp::map::parse_elevation(QStringLiteral("opentopodata"), body, &value, &err) &&
                  near_eq(value, 38.0) && err.isEmpty(),
              "opentopodata/parse/ok", "38.0 解析失败: " + q(err));
    }
    {
        const QByteArray body = QByteArrayLiteral(
            R"({"results":[{"elevation":1234.5678,"dataset":"srtm30m"},{"elevation":0}]})");
        double value = 0.0;
        QString err;
        check(pp::map::parse_elevation(QStringLiteral("opentopodata"), body, &value, &err) &&
                  near_eq(value, 1234.5678),
              "opentopodata/parse/first", "首点取值失败: " + q(err));
    }
    {
        struct Case {
            const char *case_name;
            const char *body;
            const char *expect_error;
        };
        const Case cases[] = {
            {"opentopodata/err/empty", "", "响应不是合法 JSON 对象"},
            {"opentopodata/err/no-field", "{}", "响应缺少 results 数组"},
            {"opentopodata/err/not-array", R"({"results":{}})", "响应缺少 results 数组"},
            {"opentopodata/err/empty-array", R"({"results":[]})", "results 数组为空"},
            {"opentopodata/err/not-object", R"({"results":[38]})", "results[0] 不是对象"},
            {"opentopodata/err/no-elevation", R"({"results":[{"dataset":"srtm30m"}]})",
             "响应缺少数值型 elevation 字段"},
            {"opentopodata/err/null-value", R"({"results":[{"elevation":null}]})",
             "响应缺少数值型 elevation 字段"},
            {"opentopodata/err/error-body", R"({"error":"Invalid request: latitude out of range"})",
             "响应缺少 results 数组"},
        };
        for (const Case &test_case : cases) {
            double value = 456.0;
            QString err;
            const bool ok = pp::map::parse_elevation(QStringLiteral("opentopodata"),
                                                     QByteArray(test_case.body), &value, &err);
            check(!ok, test_case.case_name, "非法响应不应解析成功");
            check(err == QString::fromUtf8(test_case.expect_error), test_case.case_name,
                  "错误文案漂移: " + q(err));
            check(near_eq(value, 456.0), test_case.case_name, "失败时不得改写 *out");
        }
    }

    // ---- 未知源 / 空指针安全 ----
    {
        double value = 0.0;
        QString err;
        check(!pp::map::parse_elevation(QStringLiteral("bogus"), QByteArrayLiteral("{}"), &value,
                                        &err) &&
                  err == QStringLiteral("未知的海拔源"),
              "parse/unknown-provider", "未知 provider 的解析失败文案漂移: " + q(err));
        const QByteArray ok_body = QByteArrayLiteral(R"({"elevation":[7.5]})");
        check(pp::map::parse_elevation(QStringLiteral("open-meteo"), ok_body, nullptr, nullptr),
              "parse/null-out-ok", "out/err 空指针时成功路径应可用");
        check(!pp::map::parse_elevation(QStringLiteral("open-meteo"), QByteArrayLiteral("{}"),
                                        nullptr, nullptr),
              "parse/null-out-fail", "out/err 空指针时失败路径应可用");
        double value2 = 0.0;
        check(pp::map::parse_elevation(QStringLiteral("open-meteo"), ok_body, &value2, nullptr) &&
                  near_eq(value2, 7.5),
              "parse/null-err-ok", "err 空指针时成功路径应可用");
    }

    return g_failed;
}

int main() {
    const int failed = run_cases();
    if (failed == 0) {
        std::printf("test_elevation: OK (provider fact table, URL bytes, parse matrix, "
                    "empty/unknown handling)\n");
        return 0;
    }
    std::printf("test_elevation: FAILED (%d assertion(s))\n", failed);
    return failed;
}
