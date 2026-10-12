// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6-T2 — geocoder unit tests (hand-written assertions).
//
// Contract: docs/m6-map-upgrade.md §4.2 endpoint fact table (frozen with
// src/mapwidget/geocoder.*). Qt Core only (no QApplication), links pp_core.
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include "mapwidget/coord.h"
#include "mapwidget/geocoder.h"

namespace {

using pp::map::Geocoder;
using pp::map::GeocodeResult;
using pp::map::SuggestItem;

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string q(const QString &s) { return s.toStdString(); }

bool near_eq(double a, double b, double eps = 1e-12) { return std::fabs(a - b) < eps; }

} // namespace

int run_cases() {
    // ---- 注册表与字段(§4.2:amap 需 Key+GCJ-02;nominatim 免 Key+WGS-84)----
    const Geocoder *amap = pp::map::geocoder_by_id(QStringLiteral("amap"));
    const Geocoder *nominatim = pp::map::geocoder_by_id(QStringLiteral("nominatim"));
    check(amap != nullptr && nominatim != nullptr, "registry/ids",
          "geocoder_by_id 对 amap/nominatim 必须非空");
    check(pp::map::geocoder_by_id(QStringLiteral("bogus")) == nullptr, "registry/unknown",
          "未知 id 应返回 nullptr");
    check(pp::map::geocoder_ids() == QStringList({"amap", "nominatim"}), "registry/order",
          "geocoder_ids 顺序/内容漂移: " + q(pp::map::geocoder_ids().join(QLatin1Char(','))));
    if (amap == nullptr || nominatim == nullptr) {
        std::printf("test_geocoder: FAILED (registry returned null)\n");
        return g_failed;
    }
    check(amap->id == QLatin1String("amap") && !amap->label.isEmpty() && amap->requires_key &&
              amap->gcj02,
          "amap/fields", "id/label/requires_key/gcj02 漂移");
    check(nominatim->id == QLatin1String("nominatim") && !nominatim->label.isEmpty() &&
              !nominatim->requires_key && !nominatim->gcj02,
          "nominatim/fields", "id/label/requires_key(免 Key)/gcj02(WGS-84)漂移");

    // ---- amap 全文搜 URL 事实表(scheme/host/path/参数;ASCII 样本逐字节)----
    check(amap->search_url(QStringLiteral("cheese"), QStringLiteral("K"), std::nullopt).query() ==
              QLatin1String("key=K&keywords=cheese&city=&offset=20&page=1&extensions=all"),
          "amap/search/bytes",
          "全文搜 query 逐字节漂移: " +
              q(amap->search_url(QStringLiteral("cheese"), QStringLiteral("K"), std::nullopt)
                    .query()));
    const QUrl text =
        amap->search_url(QStringLiteral("咖啡"), QStringLiteral("KEY123"), std::nullopt);
    check(text.scheme() == QLatin1String("https") &&
              text.host() == QLatin1String("restapi.amap.com") &&
              text.path() == QLatin1String("/v3/place/text"),
          "amap/search/scheme-host-path", "got " + q(text.toString()));
    {
        const QUrlQuery query(text);
        check(query.queryItemValue(QStringLiteral("key")) == QLatin1String("KEY123"),
              "amap/search/key", "key 参数漂移");
        check(query.queryItemValue(QStringLiteral("keywords")) == QStringLiteral("咖啡"),
              "amap/search/keywords", "keywords 应百分号编码后可读回原文");
        check(query.hasQueryItem(QStringLiteral("city")) &&
                  query.queryItemValue(QStringLiteral("city")).isEmpty(),
              "amap/search/city-empty", "city= 空占位缺失");
        check(query.queryItemValue(QStringLiteral("offset")) == QLatin1String("20"),
              "amap/search/offset", "offset 应为 20");
        check(query.queryItemValue(QStringLiteral("page")) == QLatin1String("1"),
              "amap/search/page", "page 应为 1");
        check(query.queryItemValue(QStringLiteral("extensions")) == QLatin1String("all"),
              "amap/search/extensions", "extensions 应为 all");
        check(!query.hasQueryItem(QStringLiteral("location")), "amap/search/no-location",
              "无 near 时不得出现 around 的 location 参数");
    }

    // ---- amap 周边搜:location 为 GCJ-02(near 传 WGS-84 应被转换)----
    const double near_lat = 39.9087; // 天安门 WGS-84
    const double near_lon = 116.3975;
    const std::pair<double, double> tiananmen{near_lat, near_lon};
    const std::optional<std::pair<double, double>> near = tiananmen;
    const QUrl around = amap->search_url(QStringLiteral("加油站"), QStringLiteral("KEY123"), near);
    check(around.scheme() == QLatin1String("https") &&
              around.host() == QLatin1String("restapi.amap.com") &&
              around.path() == QLatin1String("/v3/place/around"),
          "amap/around/scheme-host-path", "got " + q(around.toString()));
    {
        const QUrlQuery query(around);
        check(query.queryItemValue(QStringLiteral("key")) == QLatin1String("KEY123") &&
                  query.queryItemValue(QStringLiteral("keywords")) == QStringLiteral("加油站"),
              "amap/around/key-keywords", "key/keywords 漂移");
        check(query.queryItemValue(QStringLiteral("radius")) == QLatin1String("1000"),
              "amap/around/radius", "radius 应为 1000");
        check(query.queryItemValue(QStringLiteral("offset")) == QLatin1String("20") &&
                  query.queryItemValue(QStringLiteral("extensions")) == QLatin1String("all"),
              "amap/around/offset-extensions", "offset=20/extensions=all 漂移");
        check(!query.hasQueryItem(QStringLiteral("page")), "amap/around/no-page",
              "around 事实表无 page 参数");
        const QStringList loc =
            query.queryItemValue(QStringLiteral("location")).split(QLatin1Char(','));
        check(loc.size() == 2, "amap/around/location-shape",
              "location 应为 {lon},{lat}: " + q(query.queryItemValue(QStringLiteral("location"))));
        if (loc.size() == 2) {
            const double loc_lon = loc.at(0).toDouble();
            const double loc_lat = loc.at(1).toDouble();
            const std::pair<double, double> gcj = pp::map::wgs84_to_gcj02(near_lat, near_lon);
            // 6 位小数格式化的舍入 ≤5e-7,用 1e-6 容差比对转换后坐标。
            check(near_eq(loc_lat, gcj.first, 1e-6) && near_eq(loc_lon, gcj.second, 1e-6),
                  "amap/around/location-gcj02",
                  "location 应等于 wgs84_to_gcj02(near) 的 {lon},{lat}: " +
                      q(query.queryItemValue(QStringLiteral("location"))));
            check(loc_lon != near_lon || loc_lat != near_lat, "amap/around/location-converted",
                  "location 不得原样保留 WGS-84 输入");
            // 北京一带 GCJ-02 相对 WGS-84 的偏移方向为东北(实测 +0.0014° lat / +0.0062° lon)。
            check(loc_lon > near_lon + 1e-4 && loc_lat > near_lat + 1e-4,
                  "amap/around/location-direction",
                  "火星偏移方向应东北(实测样本): " +
                      q(query.queryItemValue(QStringLiteral("location"))));
        }
    }

    // ---- nominatim 全文搜:免 Key + accept-language=zh ----
    const QUrl nsearch = nominatim->search_url(QStringLiteral("北京 天安门"),
                                               QStringLiteral("SHOULD_NOT_APPEAR"), std::nullopt);
    check(nsearch.scheme() == QLatin1String("https") &&
              nsearch.host() == QLatin1String("nominatim.openstreetmap.org") &&
              nsearch.path() == QLatin1String("/search"),
          "nominatim/search/scheme-host-path", "got " + q(nsearch.toString()));
    {
        const QUrlQuery query(nsearch);
        check(query.queryItemValue(QStringLiteral("q")) == QStringLiteral("北京 天安门"),
              "nominatim/search/q", "q 应百分号编码后可读回原文");
        check(query.queryItemValue(QStringLiteral("format")) == QLatin1String("json") &&
                  query.queryItemValue(QStringLiteral("limit")) == QLatin1String("20"),
              "nominatim/search/format-limit", "format=json/limit=20 漂移");
        check(query.queryItemValue(QStringLiteral("accept-language")) == QLatin1String("zh"),
              "nominatim/search/accept-language", "accept-language 应为 zh");
        check(!query.hasQueryItem(QStringLiteral("key")) &&
                  !nsearch.toString().contains(QStringLiteral("SHOULD_NOT_APPEAR")),
              "nominatim/search/no-key", "nominatim 免 Key,不得携带 key 参数");
    }

    // ---- 联想:amap inputtips;nominatim 无端点(空 QUrl)----
    const QUrl ntip = nominatim->suggest_url(QStringLiteral("x"), QString());
    check(!ntip.isValid(), "nominatim/suggest/unsupported", "nominatim 联想应返回空 QUrl");
    const QUrl tip = amap->suggest_url(QStringLiteral("北京"), QStringLiteral("KEY123"));
    check(tip.scheme() == QLatin1String("https") &&
              tip.host() == QLatin1String("restapi.amap.com") &&
              tip.path() == QLatin1String("/v3/assistant/inputtips"),
          "amap/suggest/scheme-host-path", "got " + q(tip.toString()));
    {
        const QUrlQuery query(tip);
        check(query.queryItemValue(QStringLiteral("key")) == QLatin1String("KEY123") &&
                  query.queryItemValue(QStringLiteral("keywords")) == QStringLiteral("北京") &&
                  query.queryItemValue(QStringLiteral("datatype")) == QLatin1String("poi") &&
                  query.hasQueryItem(QStringLiteral("city")),
              "amap/suggest/params", "key/keywords/city/datatype=poi 漂移");
    }

    // ---- 逆地理 URL:amap location 为 {lon},{lat}(源基准);nominatim lat/lon ----
    // R-219 换锚（M7-WP7，裁定登记 build/m7-wp7/handoff.md）：reverse_url 入参语义由
    // "源基准（调用方预转 GCJ-02）"改为「**WGS-84 入参、URL 边界内转换**」—— 与 search_url
    // 的 near 同一约定。断言改钉「location == wgs84_to_gcj02(入参)」+「不得原样透传 WGS-84」，
    // scheme/host/path/key/extensions 逐字保留（只换锚不降强度）。
    const QUrl regeo = amap->reverse_url(39.990464, 116.481488, QStringLiteral("KEY123"));
    check(regeo.scheme() == QLatin1String("https") &&
              regeo.host() == QLatin1String("restapi.amap.com") &&
              regeo.path() == QLatin1String("/v3/geocode/regeo"),
          "amap/reverse/scheme-host-path", "got " + q(regeo.toString()));
    {
        const QUrlQuery query(regeo);
        const QStringList loc =
            query.queryItemValue(QStringLiteral("location")).split(QLatin1Char(','));
        check(loc.size() == 2, "amap/reverse/location-lon-lat",
              "location 应为 {lon},{lat}: " + q(query.queryItemValue(QStringLiteral("location"))));
        if (loc.size() == 2) {
            const double loc_lon = loc.at(0).toDouble();
            const double loc_lat = loc.at(1).toDouble();
            const std::pair<double, double> gcj = pp::map::wgs84_to_gcj02(39.990464, 116.481488);
            check(near_eq(loc_lat, gcj.first, 1e-6) && near_eq(loc_lon, gcj.second, 1e-6),
                  "amap/reverse/location-converted",
                  "location 应等于 wgs84_to_gcj02(入参 WGS-84) 的 {lon},{lat}: " +
                      q(query.queryItemValue(QStringLiteral("location"))));
            check(std::fabs(loc_lat - 39.990464) > 1e-4 || std::fabs(loc_lon - 116.481488) > 1e-4,
                  "amap/reverse/location-not-passthrough",
                  "入参 WGS-84 不得原样上线上（GCJ-02 偏移须已施加）");
        }
        check(query.queryItemValue(QStringLiteral("key")) == QLatin1String("KEY123") &&
                  query.queryItemValue(QStringLiteral("extensions")) == QLatin1String("all"),
              "amap/reverse/key-extensions", "key/extensions=all 漂移");
    }
    const QUrl nrev = nominatim->reverse_url(39.9087, 116.3975, QString());
    check(nrev.scheme() == QLatin1String("https") &&
              nrev.host() == QLatin1String("nominatim.openstreetmap.org") &&
              nrev.path() == QLatin1String("/reverse"),
          "nominatim/reverse/scheme-host-path", "got " + q(nrev.toString()));
    {
        const QUrlQuery query(nrev);
        check(near_eq(query.queryItemValue(QStringLiteral("lat")).toDouble(), 39.9087, 1e-6) &&
                  near_eq(query.queryItemValue(QStringLiteral("lon")).toDouble(), 116.3975, 1e-6),
              "nominatim/reverse/lat-lon", "lat/lon 漂移");
        check(query.queryItemValue(QStringLiteral("format")) == QLatin1String("json") &&
                  query.queryItemValue(QStringLiteral("accept-language")) == QLatin1String("zh") &&
                  !query.hasQueryItem(QStringLiteral("key")),
              "nominatim/reverse/format-lang-no-key", "format/accept-language/免 Key 漂移");
    }

    // ---- parse_search:假 amap JSON(pois[].name/location GCJ-02)→ WGS-84 ----
    std::vector<GeocodeResult> results;
    const QByteArray amap_body = QByteArray(R"({"status":"1","info":"OK","count":"2","pois":[
      {"name":"测试餐厅","type":"餐饮服务;中餐厅","location":"113.947,22.531",
       "pname":"广东省","cityname":"深圳市","adname":"南山区","address":"科技园路1号","distance":"1234"},
      {"name":"简单点","location":"116.404,39.915"}
    ]})");
    QString error = pp::map::parse_search(amap_body, QStringLiteral("amap"), results);
    check(error.isEmpty(), "amap/parse/ok", "成功应返回空串,got: " + q(error));
    check(results.size() == 2, "amap/parse/size",
          "pois 应为 2 条,got " + std::to_string(results.size()));
    if (results.size() == 2) {
        const std::pair<double, double> wgs = pp::map::gcj02_to_wgs84(22.531, 113.947);
        check(results[0].title == QStringLiteral("测试餐厅"), "amap/parse/title",
              "title 应取 name: " + q(results[0].title));
        check(results[0].address == QStringLiteral("广东省深圳市南山区科技园路1号"),
              "amap/parse/address", "address 应为结构化拼接: " + q(results[0].address));
        check(results[0].type == QStringLiteral("餐饮服务;中餐厅"), "amap/parse/type",
              "type 漂移: " + q(results[0].type));
        check(near_eq(results[0].lat, wgs.first) && near_eq(results[0].lon, wgs.second),
              "amap/parse/wgs84", "lat/lon 应等于 gcj02_to_wgs84(location)");
        check(std::fabs(results[0].lat - 22.531) > 1e-4 &&
                  std::fabs(results[0].lon - 113.947) > 1e-4,
              "amap/parse/converted", "出参不得保留 GCJ-02 原始值");
        check(near_eq(results[0].distance_m, 1234.0, 1e-9), "amap/parse/distance",
              "distance_m 应取 distance");
        check(results[1].address.isEmpty() && results[1].type.isEmpty(),
              "amap/parse/missing-fields", "缺失的结构化字段应为空");
        check(results[1].distance_m < 0, "amap/parse/distance-missing",
              "缺 distance 应为 -1(仅 around 带距离)");
    }

    // ---- parse_search:nominatim 本就 WGS-84,不转换;非法项跳过 ----
    const QByteArray nom_body = QByteArray(R"([
      {"display_name":"天安门, 东城区, 北京市, 中国","lat":"39.9087","lon":"116.3975","type":"attraction"},
      {"display_name":"坏点","lat":"not-a-number","lon":"116.4"},
      {"display_name":"越界点","lat":"100.0","lon":"116.4"}
    ])");
    error = pp::map::parse_search(nom_body, QStringLiteral("nominatim"), results);
    check(error.isEmpty(), "nominatim/parse/ok", "成功应返回空串,got: " + q(error));
    check(results.size() == 1, "nominatim/parse/skip-invalid",
          "坏点/越界点应跳过,got " + std::to_string(results.size()));
    if (results.size() == 1) {
        check(results[0].title == QStringLiteral("天安门, 东城区, 北京市, 中国"),
              "nominatim/parse/title", "title 应取 display_name: " + q(results[0].title));
        check(near_eq(results[0].lat, 39.9087) && near_eq(results[0].lon, 116.3975),
              "nominatim/parse/wgs84-passthrough", "nominatim 出参不得再转换");
    }

    // ---- 0 结果:清空 out 并返回空串 ----
    results.push_back(GeocodeResult{});
    error = pp::map::parse_search(QByteArray("[]"), QStringLiteral("nominatim"), results);
    check(error.isEmpty() && results.empty(), "nominatim/parse/empty-array",
          "0 结果应清空 out 并返回空串");
    error = pp::map::parse_search(QByteArray(R"({"status":"1","info":"OK","count":"0","pois":[]})"),
                                  QStringLiteral("amap"), results);
    check(error.isEmpty() && results.empty(), "amap/parse/empty-pois",
          "0 结果应清空 out 并返回空串");

    // ---- parse_search 错误分类:JSON 坏/结构错/status!=1/未知源 ----
    error = pp::map::parse_search(QByteArray("{oops"), QStringLiteral("amap"), results);
    check(!error.isEmpty() && results.empty(), "parse/search/bad-json",
          "JSON 解析失败应返回错误串并清空 out");
    error = pp::map::parse_search(QByteArray(R"({"status":"0","info":"INVALID_USER_KEY"})"),
                                  QStringLiteral("amap"), results);
    check(error.contains(QStringLiteral("INVALID_USER_KEY")), "amap/parse/status-error",
          "status!=1 应返回其 info: " + q(error));
    error = pp::map::parse_search(QByteArray("[]"), QStringLiteral("amap"), results);
    check(!error.isEmpty(), "amap/parse/not-object", "顶层非对象应报错");
    error = pp::map::parse_search(QByteArray("{}"), QStringLiteral("nominatim"), results);
    check(!error.isEmpty(), "nominatim/parse/not-array", "顶层非数组应报错");
    error = pp::map::parse_search(QByteArray("[]"), QStringLiteral("bogus"), results);
    check(!error.isEmpty(), "parse/search/unknown-geocoder", "未知源应报错");

    // ---- parse_suggest:amap tips[] 坐标转 WGS-84、无 location 的条目标 has_point=false ----
    std::vector<SuggestItem> tips;
    const QByteArray tips_body = QByteArray(R"({"status":"1","info":"OK","count":"2","tips":[
      {"name":"北京大学","district":"北京市海淀区","address":"颐和园路5号","location":"116.310791,39.992974"},
      {"name":"北京市","district":"北京市","address":"","location":""}
    ]})");
    error = pp::map::parse_suggest(tips_body, QStringLiteral("amap"), tips);
    check(error.isEmpty(), "amap/suggest/parse-ok", "成功应返回空串,got: " + q(error));
    check(tips.size() == 2, "amap/suggest/size", "tips 应为 2 条");
    if (tips.size() == 2) {
        const std::pair<double, double> wgs = pp::map::gcj02_to_wgs84(39.992974, 116.310791);
        check(tips[0].title == QStringLiteral("北京大学") &&
                  tips[0].address == QStringLiteral("北京市海淀区颐和园路5号"),
              "amap/suggest/title-address", "title/district+address 漂移");
        check(tips[0].has_point && near_eq(tips[0].lat, wgs.first) &&
                  near_eq(tips[0].lon, wgs.second),
              "amap/suggest/wgs84", "有 location 应转 WGS-84 且 has_point=true");
        check(!tips[1].has_point && tips[1].lat == 0.0 && tips[1].lon == 0.0,
              "amap/suggest/no-point", "空 location 应 has_point=false 且坐标归零");
    }
    tips.push_back(SuggestItem{});
    error = pp::map::parse_suggest(QByteArray(R"({"status":"1","info":"OK","tips":[]})"),
                                   QStringLiteral("amap"), tips);
    check(error.isEmpty() && tips.empty(), "amap/suggest/empty", "0 联想应清空 out 并返回空串");
    error = pp::map::parse_suggest(QByteArray("{oops"), QStringLiteral("amap"), tips);
    check(!error.isEmpty() && tips.empty(), "amap/suggest/bad-json", "JSON 坏应返回错误串");
    error = pp::map::parse_suggest(QByteArray(R"({"status":"0","info":"INVALID_USER_KEY"})"),
                                   QStringLiteral("amap"), tips);
    check(error.contains(QStringLiteral("INVALID_USER_KEY")), "amap/suggest/status-error",
          "status!=1 应返回其 info: " + q(error));
    error = pp::map::parse_suggest(QByteArray("[]"), QStringLiteral("amap"), tips);
    check(!error.isEmpty(), "amap/suggest/not-object", "顶层非对象应报错");
    error = pp::map::parse_suggest(QByteArray("[]"), QStringLiteral("nominatim"), tips);
    check(!error.isEmpty(), "nominatim/suggest/unsupported-parse", "nominatim 联想解析应报不支持");
    error = pp::map::parse_suggest(QByteArray("[]"), QStringLiteral("bogus"), tips);
    check(!error.isEmpty(), "parse/suggest/unknown-geocoder", "未知源应报错");

    // ---- parse_reverse:amap formatted_address / nominatim display_name / 错误分类 ----
    QString address;
    error = pp::map::parse_reverse(
        QByteArray(
            R"({"status":"1","info":"OK","regeocode":{"formatted_address":"北京市朝阳区望京街道望京街9号","addressComponent":{}}})"),
        QStringLiteral("amap"), address);
    check(error.isEmpty() && address == QStringLiteral("北京市朝阳区望京街道望京街9号"),
          "amap/reverse/ok", "address 应取 regeocode.formatted_address: " + q(address));
    address = QStringLiteral("脏数据");
    error = pp::map::parse_reverse(
        QByteArray(R"({"status":"0","info":"USER_KEY_PLATFORM_UNAUTHORIZED"})"),
        QStringLiteral("amap"), address);
    check(error.contains(QStringLiteral("USER_KEY_PLATFORM_UNAUTHORIZED")) && address.isEmpty(),
          "amap/reverse/status-error", "status!=1 应返回其 info 并清空 address: " + q(error));
    error = pp::map::parse_reverse(QByteArray("{oops"), QStringLiteral("amap"), address);
    check(!error.isEmpty() && address.isEmpty(), "reverse/bad-json", "JSON 坏应返回错误串");
    error = pp::map::parse_reverse(QByteArray("[]"), QStringLiteral("amap"), address);
    check(!error.isEmpty(), "amap/reverse/not-object", "顶层非对象应报错");
    error = pp::map::parse_reverse(QByteArray(R"({"display_name":"北京市, 中国"})"),
                                   QStringLiteral("nominatim"), address);
    check(error.isEmpty() && address == QStringLiteral("北京市, 中国"), "nominatim/reverse/ok",
          "address 应取 display_name: " + q(address));
    error = pp::map::parse_reverse(QByteArray(R"({"error":"Unable to geocode"})"),
                                   QStringLiteral("nominatim"), address);
    check(!error.isEmpty() && address.isEmpty(), "nominatim/reverse/error-field",
          "nominatim error 字段应转错误串");
    error = pp::map::parse_reverse(QByteArray("[]"), QStringLiteral("nominatim"), address);
    check(!error.isEmpty(), "nominatim/reverse/not-object", "顶层非对象应报错");
    error = pp::map::parse_reverse(QByteArray("[]"), QStringLiteral("bogus"), address);
    check(!error.isEmpty(), "parse/reverse/unknown-geocoder", "未知源应报错");

    if (g_failed == 0) {
        std::printf("test_geocoder: OK (URL fact table amap/nominatim, GCJ-02 conversion, "
                    "parse error classes)\n");
        return 0;
    }
    std::printf("test_geocoder: FAILED (%d assertion(s))\n", g_failed);
    return g_failed;
}

int main() {
    // main 不得让异常外泄（外泄=terminate，bugprone-exception-escape）：run_cases 整体罩住；
    // 异常路径返回 1（非零=失败，ctest 判定不变），断言与成功路径逐字不动。
    try {
        return run_cases();
    } catch (...) {
        std::printf("test_geocoder: FAILED (unexpected exception)\n");
        return 1;
    }
}
