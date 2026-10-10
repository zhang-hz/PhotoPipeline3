// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6-T3 — providers adaption-layer unit tests (hand-written assertions).
//
// Contract: PP-FROZEN src/mapwidget/providers.h + docs/m1b-tasks.md §2.4「URL
// 事实（冻结，不得改动）」 (frozen with src/mapwidget/providers.cpp); the adapter delegates
// tiles/parsing to layers/geocoder (docs/m6-map-upgrade.md §3.C1、§7-T3). Qt Core only (no
// QApplication), links pp_core. Every failure prints "FAIL <case>: <detail>"; main() returns the
// number of failures.

#include <cmath>
#include <cstdio>
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
#include "mapwidget/layers.h"
#include "mapwidget/providers.h"

namespace {

using pp::map::SearchResult;
using pp::map::TileProvider;
using pp::map::TileStyle;

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string q(const QString &s) { return s.toStdString(); }

bool near_eq(double a, double b, double eps = 1e-12) { return std::fabs(a - b) < eps; }

// amap 子域:host 形如 webrd01..webrd04,返回 1..4;不匹配返回 -1。(同 test_layers.cpp)
int amap_subdomain(const QUrl &url) {
    const QString host = url.host();
    if (!host.startsWith(QStringLiteral("webrd")) || host.size() <= 6 ||
        host.at(5) != QLatin1Char('0')) {
        return -1;
    }
    bool ok = false;
    const int n = host.mid(6, 1).toInt(&ok);
    return (ok && n >= 1 && n <= 4) ? n : -1;
}

} // namespace

int main() {
    // ---- 注册表(冻结:{"osm","amap"},顺序不变)----
    const TileProvider *osm = pp::map::provider_by_id(QStringLiteral("osm"));
    const TileProvider *amap = pp::map::provider_by_id(QStringLiteral("amap"));
    check(osm != nullptr && amap != nullptr, "registry/ids", "provider_by_id 对 osm/amap 必须非空");
    check(pp::map::provider_by_id(QStringLiteral("bogus")) == nullptr, "registry/unknown",
          "未知 id 应返回 nullptr");
    check(pp::map::provider_ids() == QStringList({"osm", "amap"}), "registry/order",
          "provider_ids 顺序/内容漂移: " + q(pp::map::provider_ids().join(QLatin1Char(','))));
    if (osm == nullptr || amap == nullptr) {
        std::printf("test_providers: FAILED (provider registry returned null)\n");
        return g_failed;
    }

    // ---- 字段(冻结:id/label/gcj02)----
    check(osm->id == QLatin1String("osm") && osm->label == QStringLiteral("OpenStreetMap") &&
              !osm->gcj02,
          "osm/fields", "id/label/gcj02 漂移");
    check(amap->id == QLatin1String("amap") && amap->label == QStringLiteral("高德地图") &&
              amap->gcj02,
          "amap/fields", "id/label/gcj02 漂移");

    // ---- User-Agent(冻结串)----
    check(osm->user_agent() == QLatin1String("PhotoPipeline/0.1 (batch transcoder; dev)") &&
              amap->user_agent() == QStringLiteral("PhotoPipeline/0.1 (batch transcoder; dev)"),
          "user_agent/bytes", "UA 冻结串漂移: " + q(osm->user_agent()));

    // ---- tile_url:osm 逐字节;amap = webrd0{1..4} style=8 且 x/y/z 正确(改造前事实表)----
    check(osm->tile_url(3, 5, 6).toString() ==
              QLatin1String("https://tile.openstreetmap.org/3/5/6.png"),
          "osm/tile/bytes", "got " + q(osm->tile_url(3, 5, 6).toString()));
    check(osm->tile_url(10, 300, 200).toString() ==
              QLatin1String("https://tile.openstreetmap.org/10/300/200.png"),
          "osm/tile/bytes/second-sample", "got " + q(osm->tile_url(10, 300, 200).toString()));

    const QUrl amap_tile = amap->tile_url(3, 5, 6);
    check(amap_tile.scheme() == QLatin1String("https") &&
              amap_tile.path() == QLatin1String("/appmaptile") &&
              amap_tile.host().endsWith(QStringLiteral(".is.autonavi.com")),
          "amap/tile/scheme-host-path", "got " + q(amap_tile.toString()));
    check(amap_tile.query() == QLatin1String("lang=zh_cn&size=1&scale=1&style=8&x=5&y=6&z=3"),
          "amap/tile/query", "x/y/z 或 style=8 漂移: " + q(amap_tile.query()));
    check(amap_subdomain(amap_tile) >= 1, "amap/tile/subdomain",
          "webrd 子域应落在 01..04: " + q(amap_tile.host()));
    const QUrl amap_tile2 = amap->tile_url(11, 65, 42);
    check(amap_tile2.query() == QLatin1String("lang=zh_cn&size=1&scale=1&style=8&x=65&y=42&z=11"),
          "amap/tile/query/second-sample", "got " + q(amap_tile2.query()));

    // 子域轮换:连续 8 次调用应覆盖 01..04 且全部落在范围内(每进程一条轮换序列)。
    bool seen[4] = {false, false, false, false};
    bool in_range = true;
    for (int i = 0; i < 8; ++i) {
        const int n = amap_subdomain(amap->tile_url(3, 5, 6));
        if (n < 1 || n > 4) {
            in_range = false;
            break;
        }
        seen[n - 1] = true;
    }
    check(in_range, "amap/tile/subdomain-range", "轮换子域越出 01..04");
    check(seen[0] && seen[1] && seen[2] && seen[3], "amap/tile/subdomain-rotation-coverage",
          "8 次调用未覆盖全部 4 个子域");

    // 委托语义(Single source of truth):T3 后 tile_url 即 TileSource::tile_url(..., Road)。
    // amap 子域由共享轮换计数器给出(逐次调用值不同),故按 path/query + 去掉 "webrd0n" 的 host 比对。
    const QUrl modeled_tile =
        pp::map::tile_source_by_id(QStringLiteral("amap"))->tile_url(3, 5, 6, TileStyle::Road);
    const QString amap_tile_tail = amap_tile.host().mid(7); // "webrd0n" 之后 = ".is.autonavi.com"
    check(amap_tile.path() == modeled_tile.path() && amap_tile.query() == modeled_tile.query() &&
              !amap_tile_tail.isEmpty() && amap_tile_tail == modeled_tile.host().mid(7),
          "amap/tile/delegates-to-layers",
          "tile_url 应等于 TileSource::tile_url(..., Road): " + q(modeled_tile.toString()));
    check(osm->tile_url(3, 5, 6) ==
              pp::map::tile_source_by_id(QStringLiteral("osm"))->tile_url(3, 5, 6, TileStyle::Road),
          "osm/tile/delegates-to-layers", "osm tile_url 应与 TileSource 逐字节一致");

    // ---- search_url:M1b 冻结事实逐字节(不得漂移)----
    check(amap->search_url(QStringLiteral("cheese"), QStringLiteral("K")).query() ==
              QLatin1String("key=K&keywords=cheese&offset=6&page=1"),
          "amap/search/bytes",
          "amap 全文搜 query 逐字节漂移: " +
              q(amap->search_url(QStringLiteral("cheese"), QStringLiteral("K")).query()));
    const QUrl amap_search = amap->search_url(QStringLiteral("咖啡"), QStringLiteral("KEY123"));
    check(amap_search.scheme() == QLatin1String("https") &&
              amap_search.host() == QLatin1String("restapi.amap.com") &&
              amap_search.path() == QLatin1String("/v3/place/text"),
          "amap/search/scheme-host-path", "got " + q(amap_search.toString()));
    {
        const QUrlQuery query(amap_search);
        check(query.queryItemValue(QStringLiteral("key")) == QLatin1String("KEY123") &&
                  query.queryItemValue(QStringLiteral("keywords")) == QStringLiteral("咖啡"),
              "amap/search/key-keywords", "key/keywords 应百分号编码后可读回原文");
        check(query.queryItemValue(QStringLiteral("offset")) == QLatin1String("6") &&
                  query.queryItemValue(QStringLiteral("page")) == QLatin1String("1"),
              "amap/search/offset-page", "offset=6&page=1 漂移");
    }
    check(amap->search_url(QStringLiteral("北京"), QString())
              .toString()
              .startsWith(QLatin1String("https://restapi.amap.com/v3/place/text?key=&keywords=")),
          "amap/search/empty-key-placeholder",
          "空 key 应保留 `key=` 空占位(调用方先查 amap_key): " +
              q(amap->search_url(QStringLiteral("北京"), QString()).toString()));

    check(osm->search_url(QStringLiteral("cheese"), QStringLiteral("IGNORED")).query() ==
              QLatin1String("format=json&limit=6&q=cheese"),
          "osm/search/bytes",
          "nominatim query 逐字节漂移: " +
              q(osm->search_url(QStringLiteral("cheese"), QStringLiteral("IGNORED")).query()));
    const QUrl osm_search =
        osm->search_url(QStringLiteral("北京 天安门"), QStringLiteral("IGNORED"));
    check(osm_search.scheme() == QLatin1String("https") &&
              osm_search.host() == QLatin1String("nominatim.openstreetmap.org") &&
              osm_search.path() == QLatin1String("/search"),
          "osm/search/scheme-host-path", "got " + q(osm_search.toString()));
    {
        const QUrlQuery query(osm_search);
        check(query.queryItemValue(QStringLiteral("q")) == QStringLiteral("北京 天安门"),
              "osm/search/q", "q 应百分号编码后可读回原文");
        check(!query.hasQueryItem(QStringLiteral("key")) &&
                  !osm_search.toString().contains(QStringLiteral("IGNORED")),
              "osm/search/no-key", "osm 免 Key,不得携带 key 参数");
    }

    // ---- parse_search:假 amap JSON(pois[].location GCJ-02)→ WGS-84 ----
    std::vector<SearchResult> results;
    const QByteArray amap_body = QByteArray(R"({"status":"1","info":"OK","count":"3","pois":[
      {"name":"测试餐厅","location":"113.947,22.531"},
      {"name":"简单点","location":"116.404,39.915"},
      {"name":"坏点","location":"not-a-coord"}
    ]})");
    QString error = pp::map::parse_search(QStringLiteral("amap"), amap_body, results);
    check(error.isEmpty(), "amap/parse/ok", "成功应返回空串,got: " + q(error));
    check(results.size() == 2, "amap/parse/skip-invalid-location",
          "坏 location 应跳过,got " + std::to_string(results.size()));
    if (results.size() == 2) {
        const std::pair<double, double> wgs = pp::map::gcj02_to_wgs84(22.531, 113.947);
        check(results[0].title == QStringLiteral("测试餐厅"), "amap/parse/title",
              "title 应取 name: " + q(results[0].title));
        check(near_eq(results[0].lat, wgs.first) && near_eq(results[0].lon, wgs.second),
              "amap/parse/wgs84", "lat/lon 应等于 gcj02_to_wgs84(location)");
        check(std::fabs(results[0].lat - 22.531) > 1e-4 &&
                  std::fabs(results[0].lon - 113.947) > 1e-4,
              "amap/parse/converted", "出参不得保留 GCJ-02 原始值");
    }

    // ---- parse_search:假 nominatim JSON(provider "osm" → nominatim 分支;本就 WGS-84)----
    const QByteArray nom_body = QByteArray(R"([
      {"display_name":"天安门, 东城区, 北京市, 中国","lat":"39.9087","lon":"116.3975"},
      {"display_name":"越界点","lat":"100.0","lon":"116.4"}
    ])");
    error = pp::map::parse_search(QStringLiteral("osm"), nom_body, results);
    check(error.isEmpty(), "osm/parse/ok", "成功应返回空串,got: " + q(error));
    check(results.size() == 1, "osm/parse/skip-invalid",
          "越界点应跳过,got " + std::to_string(results.size()));
    if (results.size() == 1) {
        check(results[0].title == QStringLiteral("天安门, 东城区, 北京市, 中国"), "osm/parse/title",
              "title 应取 display_name: " + q(results[0].title));
        check(near_eq(results[0].lat, 39.9087) && near_eq(results[0].lon, 116.3975),
              "osm/parse/wgs84-passthrough", "nominatim 出参不得再转换");
    }

    // 委托语义:T3 后 parse_search 即 geocoder parse_search 的 SearchResult 回映射。
    {
        std::vector<pp::map::GeocodeResult> modeled;
        const QString modeled_error =
            pp::map::parse_search(amap_body, QStringLiteral("amap"), modeled);
        std::vector<SearchResult> via_provider;
        const QString provider_error =
            pp::map::parse_search(QStringLiteral("amap"), amap_body, via_provider);
        bool same = modeled_error == provider_error && modeled.size() == via_provider.size();
        for (std::size_t i = 0; same && i < modeled.size(); ++i) {
            same = modeled[i].title == via_provider[i].title &&
                   modeled[i].lat == via_provider[i].lat && modeled[i].lon == via_provider[i].lon;
        }
        check(same, "parse/delegates-to-geocoder",
              "parse_search 应为 Geocoder parse_search 的 title/lat/lon 回映射");
    }

    // ---- 错误分类(文案与 M1b 同源;out 一律清空)----
    error = pp::map::parse_search(
        QStringLiteral("amap"), QByteArray(R"({"status":"0","info":"INVALID_USER_KEY"})"), results);
    check(!error.isEmpty() && error.contains(QStringLiteral("INVALID_USER_KEY")) && results.empty(),
          "amap/parse/status-error", "status!=1 应返回非空错误串(含 info): " + q(error));
    error = pp::map::parse_search(QStringLiteral("osm"), QByteArray("{oops"), results);
    check(!error.isEmpty() && results.empty(), "parse/bad-json",
          "JSON 解析失败应返回错误串并清空 out");
    error = pp::map::parse_search(QStringLiteral("osm"), QByteArray("{}"), results);
    check(!error.isEmpty(), "osm/parse/not-array", "nominatim 顶层非数组应报错");
    error = pp::map::parse_search(QStringLiteral("amap"), QByteArray("[]"), results);
    check(!error.isEmpty(), "amap/parse/not-object", "amap 顶层非对象应报错");
    error = pp::map::parse_search(QStringLiteral("bogus"), QByteArray("[]"), results);
    check(!error.isEmpty() && error.contains(QStringLiteral("bogus")) && results.empty(),
          "parse/unknown-provider", "未知 provider 应返回非空错误串: " + q(error));
    error = pp::map::parse_search(QStringLiteral("bogus"), QByteArray("{oops"), results);
    check(!error.isEmpty() && error.contains(QStringLiteral("bogus")),
          "parse/unknown-provider-precedence",
          "未知 provider 应先于 JSON 解析报错(旧实现同序): " + q(error));

    // 0 结果:清空 out 并返回空串。
    results.push_back(SearchResult{});
    error = pp::map::parse_search(QStringLiteral("osm"), QByteArray("[]"), results);
    check(error.isEmpty() && results.empty(), "osm/parse/empty-array",
          "0 结果应清空 out 并返回空串");

    if (g_failed == 0) {
        std::printf("test_providers: OK (frozen registry/UA/tile/search bytes, "
                    "parse WGS-84 mapping, error classes)\n");
        return 0;
    }
    std::printf("test_providers: FAILED (%d assertion(s))\n", g_failed);
    return g_failed;
}
