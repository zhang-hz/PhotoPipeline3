// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6-T1 — tile source / layer style unit tests (hand-written assertions).
//
// Contract: docs/m6-map-upgrade.md §4.1 URL fact table (frozen with src/mapwidget/layers.*).
// Qt Core only (no QApplication), links pp_core. Every failure prints "FAIL <case>: <detail>";
// main() returns the number of failures.

#include <cstdio>
#include <string>

#include <QString>
#include <QStringList>
#include <QUrl>

#include "mapwidget/layers.h"

namespace {

using pp::map::TileSource;
using pp::map::TileStyle;

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string q(const QString &s) { return s.toStdString(); }

// amap 子域:取 host 形如 webrd01../webst03..(prefix="webrd"/"webst"),返回 1..4;
// 不匹配/越界返回 -1。
int amap_subdomain(const QUrl &url, const QString &host_prefix) {
    const QString host = url.host();
    // prefix 之后必须是零填充两位 "0n"(n=1..4)。
    if (!host.startsWith(host_prefix) || host.size() <= host_prefix.size() + 1 ||
        host.at(host_prefix.size()) != QLatin1Char('0')) {
        return -1;
    }
    bool ok = false;
    const int n = host.mid(host_prefix.size() + 1, 1).toInt(&ok);
    return (ok && n >= 1 && n <= 4) ? n : -1;
}

} // namespace

int main() {
    const TileSource *amap = pp::map::tile_source_by_id(QStringLiteral("amap"));
    const TileSource *osm = pp::map::tile_source_by_id(QStringLiteral("osm"));
    const TileSource *esri = pp::map::tile_source_by_id(QStringLiteral("esri"));
    check(amap != nullptr && osm != nullptr && esri != nullptr, "source/registry",
          "tile_source_by_id 对 amap/osm/esri 必须非空");
    check(pp::map::tile_source_by_id(QStringLiteral("bogus")) == nullptr, "source/unknown",
          "未知 id 应返回 nullptr");
    check(pp::map::tile_source_ids() == QStringList({"amap", "osm", "esri"}), "source/ids",
          "tile_source_ids 顺序/内容漂移: " + q(pp::map::tile_source_ids().join(QLatin1Char(','))));
    if (amap == nullptr || osm == nullptr || esri == nullptr) {
        std::printf("test_layers: FAILED (source registry returned null)\n");
        return g_failed;
    }

    // 字段与栅格基准(gcj02):amap=true;osm/esri=false。
    check(amap->id == QLatin1String("amap") && amap->label == QStringLiteral("高德地图") &&
              amap->gcj02 && !amap->attribution.isEmpty(),
          "amap/fields", "id/label/gcj02/attribution 漂移");
    check(osm->id == QLatin1String("osm") && osm->label == QStringLiteral("OpenStreetMap") &&
              !osm->gcj02 && !osm->attribution.isEmpty(),
          "osm/fields", "id/label/gcj02/attribution 漂移");
    check(esri->id == QLatin1String("esri") && esri->label == QStringLiteral("Esri 影像") &&
              !esri->gcj02 && !esri->attribution.isEmpty(),
          "esri/fields", "id/label/gcj02/attribution 漂移");

    // supports 能力矩阵:amap 三态全支持;osm 仅 Road;esri 仅 Satellite/Hybrid。
    check(amap->supports(TileStyle::Road) && amap->supports(TileStyle::Satellite) &&
              amap->supports(TileStyle::Hybrid),
          "supports/amap", "amap 应支持 Road/Satellite/Hybrid 全部");
    check(osm->supports(TileStyle::Road) && !osm->supports(TileStyle::Satellite) &&
              !osm->supports(TileStyle::Hybrid),
          "supports/osm", "osm 应仅支持 Road");
    check(!esri->supports(TileStyle::Road) && esri->supports(TileStyle::Satellite) &&
              esri->supports(TileStyle::Hybrid),
          "supports/esri", "esri 应支持 Satellite/Hybrid,不支持 Road");

    // styles_for:各源支持的图层名列表(顺序 Road/Satellite/Hybrid)。
    check(pp::map::styles_for(QStringLiteral("amap")) ==
              QStringList({"road", "satellite", "hybrid"}),
          "styles_for/amap", "amap -> road,satellite,hybrid");
    check(pp::map::styles_for(QStringLiteral("osm")) == QStringList({"road"}), "styles_for/osm",
          "osm -> road");
    check(pp::map::styles_for(QStringLiteral("esri")) == QStringList({"satellite", "hybrid"}),
          "styles_for/esri", "esri -> satellite,hybrid");
    check(pp::map::styles_for(QStringLiteral("bogus")).isEmpty(), "styles_for/unknown",
          "未知源应为空列表");

    // ---- URL 事实表(§4.1):逐条核 scheme/host/path/query;样本 z=3,x=5,y=6(x!=y 以便验顺序)----
    const QUrl road = amap->tile_url(3, 5, 6, TileStyle::Road);
    check(road.scheme() == QLatin1String("https") && road.path() == QLatin1String("/appmaptile") &&
              road.host().endsWith(QStringLiteral(".is.autonavi.com")),
          "amap/road/scheme-host-path", "got " + q(road.toString()));
    check(road.query() == QLatin1String("lang=zh_cn&size=1&scale=1&style=8&x=5&y=6&z=3"),
          "amap/road/query", "got " + q(road.query()));
    check(amap_subdomain(road, QStringLiteral("webrd")) >= 1, "amap/road/subdomain",
          "webrd 子域应落在 01..04: " + q(road.host()));

    const QUrl sat = amap->tile_url(3, 5, 6, TileStyle::Satellite);
    check(sat.scheme() == QLatin1String("https") && sat.path() == QLatin1String("/appmaptile") &&
              sat.host().startsWith(QStringLiteral("webst0")) &&
              sat.host().endsWith(QStringLiteral(".is.autonavi.com")),
          "amap/satellite/scheme-host-path", "got " + q(sat.toString()));
    check(sat.query() == QLatin1String("style=6&x=5&y=6&z=3"), "amap/satellite/query",
          "got " + q(sat.query()));
    check(amap_subdomain(sat, QStringLiteral("webst")) >= 1, "amap/satellite/subdomain",
          "webst 子域应落在 01..04: " + q(sat.host()));

    const QUrl hybrid = amap->tile_url(3, 5, 6, TileStyle::Hybrid);
    check(hybrid.host().startsWith(QStringLiteral("webst0")) &&
              hybrid.path() == QLatin1String("/appmaptile") &&
              hybrid.query() == QLatin1String("style=6&x=5&y=6&z=3"),
          "amap/hybrid/image-layer",
          "Hybrid 的 tile_url 应为 style=6 影像层: " + q(hybrid.toString()));

    const QUrl label = amap->label_url(3, 5, 6);
    check(label.scheme() == QLatin1String("https") &&
              label.host().startsWith(QStringLiteral("webst0")) &&
              label.path() == QLatin1String("/appmaptile") &&
              label.query() == QLatin1String("style=7&x=5&y=6&z=3"),
          "amap/hybrid/label-layer", "标注层应为 style=7: " + q(label.toString()));
    check(amap_subdomain(label, QStringLiteral("webst")) >= 1, "amap/hybrid/label-subdomain",
          "webst 子域应落在 01..04: " + q(label.host()));

    const QUrl osm_road = osm->tile_url(3, 5, 6, TileStyle::Road);
    check(osm_road.scheme() == QLatin1String("https") &&
              osm_road.host() == QLatin1String("tile.openstreetmap.org"),
          "osm/road/scheme-host", "got " + q(osm_road.toString()));
    check(osm_road.toString() == QLatin1String("https://tile.openstreetmap.org/3/5/6.png"),
          "osm/road/bytes", "got " + q(osm_road.toString()));

    const QUrl esri_sat = esri->tile_url(3, 5, 6, TileStyle::Satellite);
    check(esri_sat.scheme() == QLatin1String("https") &&
              esri_sat.host() == QLatin1String("server.arcgisonline.com") &&
              esri_sat.path() ==
                  QLatin1String("/ArcGIS/rest/services/World_Imagery/MapServer/tile/3/6/5"),
          "esri/satellite/z-y-x-order",
          "路径必须为 /{z}/{y}/{x}(y 在前): " + q(esri_sat.toString()));
    check(esri->tile_url(10, 300, 200, TileStyle::Satellite).path() ==
              QLatin1String("/ArcGIS/rest/services/World_Imagery/MapServer/tile/10/200/300"),
          "esri/satellite/z-y-x-order/second-sample",
          "路径必须为 /{z}/{y}/{x}: " +
              q(esri->tile_url(10, 300, 200, TileStyle::Satellite).toString()));
    const QUrl esri_hyb = esri->tile_url(3, 5, 6, TileStyle::Hybrid);
    check(esri_hyb == esri_sat, "esri/hybrid/reuses-imagery", "esri Hybrid 应复用同一影像层 URL");

    // 不支持组合:返回空 QUrl(调用方先查 supports();UI 负责回退)。
    check(!osm->tile_url(3, 5, 6, TileStyle::Satellite).isValid() &&
              !osm->tile_url(3, 5, 6, TileStyle::Hybrid).isValid(),
          "osm/unsupported-empty", "osm 无卫星/混合,应返回空 QUrl");
    check(!esri->tile_url(3, 5, 6, TileStyle::Road).isValid(), "esri/unsupported-empty",
          "esri 无矢量路网,应返回空 QUrl");
    check(!osm->label_url(3, 5, 6).isValid() && !esri->label_url(3, 5, 6).isValid(),
          "label/unsupported-empty", "osm/esri 无标注层,应返回空 QUrl");

    // R-220 换锚（M7-WP7，裁定登记 build/m7-wp7/handoff.md）：子域由「进程级原子游标轮换」
    // 改为「瓦片坐标的纯函数」—— 断言同步改为①同瓦片恒同子域（纯/可复现）②不同瓦片覆盖
    // 01..04（分摊面不塌）。旧断言（同瓦片 8 连调覆盖 4 子域）钉的是被有意移除的全局状态。
    {
        const int n0 =
            amap_subdomain(amap->tile_url(3, 5, 6, TileStyle::Road), QStringLiteral("webrd"));
        bool stable = true;
        for (int i = 0; i < 8; ++i) {
            if (amap_subdomain(amap->tile_url(3, 5, 6, TileStyle::Road), QStringLiteral("webrd")) !=
                n0) {
                stable = false;
            }
        }
        check(stable && n0 >= 1 && n0 <= 4, "amap/subdomain/pure",
              "同瓦片必须恒同子域（纯函数）且落在 01..04: " +
                  q(amap->tile_url(3, 5, 6, TileStyle::Road).host()));
        bool seen[4] = {false, false, false, false};
        for (int x = 0; x < 8; ++x) {
            const int n =
                amap_subdomain(amap->tile_url(3, x, 6, TileStyle::Road), QStringLiteral("webrd"));
            if (n >= 1 && n <= 4) {
                seen[n - 1] = true;
            }
        }
        check(seen[0] && seen[1] && seen[2] && seen[3], "amap/subdomain/coverage",
              "不同瓦片未覆盖全部 4 个子域（分摊面塌了）");
    }
    for (int i = 0; i < 4; ++i) {
        check(amap_subdomain(amap->tile_url(3, 5, 6, TileStyle::Satellite),
                             QStringLiteral("webst")) >= 1 &&
                  amap_subdomain(amap->label_url(3, 5, 6), QStringLiteral("webst")) >= 1,
              "amap/webst/subdomain-range", "webst 子域越出 01..04");
    }

    // 图层名往返 + 未知回落 Road。
    check(pp::map::tile_style_name(TileStyle::Road) == QLatin1String("road") &&
              pp::map::tile_style_name(TileStyle::Satellite) == QLatin1String("satellite") &&
              pp::map::tile_style_name(TileStyle::Hybrid) == QLatin1String("hybrid"),
          "style/name", "tile_style_name 漂移");
    for (const TileStyle style : {TileStyle::Road, TileStyle::Satellite, TileStyle::Hybrid}) {
        check(pp::map::tile_style_from_name(pp::map::tile_style_name(style)) == style,
              "style/roundtrip",
              "from_name(name(s)) != s for " + q(pp::map::tile_style_name(style)));
    }
    check(pp::map::tile_style_from_name(QStringLiteral("bogus")) == TileStyle::Road &&
              pp::map::tile_style_from_name(QString()) == TileStyle::Road,
          "style/unknown-default-road", "未知名称应回落 Road");

    if (g_failed == 0) {
        std::printf("test_layers: OK (URL fact table amap/osm/esri, supports matrix, "
                    "style round-trip, subdomain rotation)\n");
        return 0;
    }
    std::printf("test_layers: FAILED (%d assertion(s))\n", g_failed);
    return g_failed;
}
