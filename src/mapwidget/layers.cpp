// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6 — tile sources & layer styles (M6-T1).
//
// Contract: docs/m6-map-upgrade.md §3.C1/C2 + §4.1 URL fact table (frozen with this file).
// Pure URL/style model, zero Qt Widgets — compiled into pp_core so unit tests can link it
// (CMake glob pre-wired by the architect). Frozen URL facts (must not drift):
//   amap Road      https://webrd0{n}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1
//                  &style=8&x={x}&y={y}&z={z}   with n rotating over 1..4; datum GCJ-02
//   amap Satellite https://webst0{n}.is.autonavi.com/appmaptile?style=6&x={x}&y={y}&z={z}
//   amap Hybrid    image layer = Satellite (style=6) via tile_url, labels style=7 via label_url
//   osm Road       https://tile.openstreetmap.org/{z}/{x}/{y}.png
//   esri Sat/Hybrid  https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/
//                  MapServer/tile/{z}/{y}/{x}   (y before x; Hybrid reuses the imagery layer)

#include "mapwidget/layers.h"

#include <atomic>

namespace pp::map {
namespace {

// webrd01..04 / webst01..04 rotation. The URL builders are const, so the cursor is
// file-static: one rotation sequence per process — same precedent as providers.cpp.
std::atomic<unsigned> g_amap_subdomain_cursor{0};

unsigned next_amap_subdomain() {
    return 1u + (g_amap_subdomain_cursor.fetch_add(1, std::memory_order_relaxed) % 4u);
}

QUrl amap_road_url(int z, int x, int y) {
    return QUrl(QStringLiteral("https://webrd0%1.is.autonavi.com/appmaptile"
                               "?lang=zh_cn&size=1&scale=1&style=8&x=%2&y=%3&z=%4")
                    .arg(next_amap_subdomain())
                    .arg(x)
                    .arg(y)
                    .arg(z));
}

QUrl amap_satellite_url(int z, int x, int y) {
    return QUrl(QStringLiteral("https://webst0%1.is.autonavi.com/appmaptile?style=6"
                               "&x=%2&y=%3&z=%4")
                    .arg(next_amap_subdomain())
                    .arg(x)
                    .arg(y)
                    .arg(z));
}

QUrl amap_label_url(int z, int x, int y) {
    return QUrl(QStringLiteral("https://webst0%1.is.autonavi.com/appmaptile?style=7"
                               "&x=%2&y=%3&z=%4")
                    .arg(next_amap_subdomain())
                    .arg(x)
                    .arg(y)
                    .arg(z));
}

QUrl osm_road_url(int z, int x, int y) {
    return QUrl(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(z).arg(x).arg(y));
}

QUrl esri_imagery_url(int z, int x, int y) {
    // World_Imagery tile indexing is {z}/{y}/{x} — y comes first, unlike the other sources.
    return QUrl(QStringLiteral("https://server.arcgisonline.com/ArcGIS/rest/services/"
                               "World_Imagery/MapServer/tile/%1/%2/%3")
                    .arg(z)
                    .arg(y)
                    .arg(x));
}

const TileSource &amap_source() {
    static const TileSource s{QStringLiteral("amap"), QStringLiteral("高德地图"), true,
                              QStringLiteral("© 高德地图 AutoNavi")};
    return s;
}

const TileSource &osm_source() {
    static const TileSource s{QStringLiteral("osm"), QStringLiteral("OpenStreetMap"), false,
                              QStringLiteral("© OpenStreetMap contributors")};
    return s;
}

const TileSource &esri_source() {
    static const TileSource s{QStringLiteral("esri"), QStringLiteral("Esri 影像"), false,
                              QStringLiteral("© Esri World Imagery")};
    return s;
}

} // namespace

bool TileSource::supports(TileStyle style) const {
    if (id == QLatin1String("amap")) {
        return true; // Road / Satellite / Hybrid 全支持
    }
    if (id == QLatin1String("osm")) {
        return style == TileStyle::Road;
    }
    if (id == QLatin1String("esri")) {
        return style == TileStyle::Satellite || style == TileStyle::Hybrid; // 纯影像源
    }
    return false;
}

QUrl TileSource::tile_url(int z, int x, int y, TileStyle style) const {
    if (!supports(style)) {
        return {}; // 调用方先查 supports()(UI 会回退),不支持组合不给 URL
    }
    if (id == QLatin1String("amap")) {
        if (style == TileStyle::Road) {
            return amap_road_url(z, x, y);
        }
        return amap_satellite_url(z, x, y); // Satellite;Hybrid 的影像层同为 style=6
    }
    if (id == QLatin1String("osm")) {
        return osm_road_url(z, x, y);
    }
    return esri_imagery_url(z, x, y); // esri:Satellite 与 Hybrid 复用同一影像层
}

QUrl TileSource::label_url(int z, int x, int y) const {
    if (id == QLatin1String("amap")) {
        return amap_label_url(z, x, y);
    }
    return {}; // osm/esri 无独立标注层
}

const TileSource *tile_source_by_id(const QString &id) {
    if (id == QLatin1String("amap")) {
        return &amap_source();
    }
    if (id == QLatin1String("osm")) {
        return &osm_source();
    }
    if (id == QLatin1String("esri")) {
        return &esri_source();
    }
    return nullptr;
}

QStringList tile_source_ids() {
    return {QStringLiteral("amap"), QStringLiteral("osm"), QStringLiteral("esri")};
}

QStringList styles_for(const QString &source_id) {
    const TileSource *source = tile_source_by_id(source_id);
    if (source == nullptr) {
        return {};
    }
    QStringList out;
    for (const TileStyle style : {TileStyle::Road, TileStyle::Satellite, TileStyle::Hybrid}) {
        if (source->supports(style)) {
            out << tile_style_name(style);
        }
    }
    return out;
}

QString tile_style_name(TileStyle style) {
    switch (style) {
    case TileStyle::Road:
        return QStringLiteral("road");
    case TileStyle::Satellite:
        return QStringLiteral("satellite");
    case TileStyle::Hybrid:
        return QStringLiteral("hybrid");
    }
    return QStringLiteral("road");
}

TileStyle tile_style_from_name(const QString &name) {
    if (name == QLatin1String("satellite")) {
        return TileStyle::Satellite;
    }
    if (name == QLatin1String("hybrid")) {
        return TileStyle::Hybrid;
    }
    return TileStyle::Road; // "road" 及一切未知值
}

} // namespace pp::map
