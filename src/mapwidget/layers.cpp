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
//
// M7-WP7（R-158/R-220/R-224，追认见 build/m7-wp7/handoff.md）：
//   * R-220 纯函数化：子域 n 由瓦片坐标**确定性派生**（(x+y+z) mod 4 + 1）—— 同瓦片恒同
//     子域（可缓存/可复现），相邻瓦片自然分摊到 4 个子域；不再掺全局 atomic 游标
//     （原文档承诺"纯函数 URL 事实表"，游标使 URL 依赖调用序）。
//   * R-224 表驱动：源事实（id/label/基准/版权/支持集/URL 构造器）收敛进 kSources 单表；
//     supports/tile_url/label_url/tile_source_by_id 的 4 处字符串 if 链全部改查表，加源 =
//     加一行。URL 事实（host/path/query 模板）逐字不动。
//   * R-158 单源：`tile_source_for_provider`（地图源 id → 瓦片源的 C1 适配映射）在此**唯一**
//     定义，providers.cpp / mapwidget.cpp 共同消费（此前两 TU 各拷一份）。
//
// Attribution（§9-X5 合规）：地图右下版权行由 mapwidget 读取 TileSource::attribution 绘制，
// 本文件的构造是**唯一定义处**。amap=「© 高德地图」、osm=「© OpenStreetMap contributors」、
// esri=「© Esri, Maxar, Earthstar Geographics」。

#include "mapwidget/layers.h"

#include <cstdint>

namespace pp::map {
namespace {

// webrd01..04 / webst01..04 的 4 子域（URL 事实表 n 的取值域）。
constexpr int kAmapSubdomains = 4;

// R-220：子域 = 瓦片坐标的纯函数（同瓦片恒同子域）。ASCII 事实不变（0{n}，n=1..4）。
int amap_subdomain(int z, int x, int y) {
    const auto mix = std::int64_t(x) + std::int64_t(y) + std::int64_t(z);
    return 1 + int((mix < 0 ? -mix : mix) % kAmapSubdomains);
}

QUrl amap_road_url(int z, int x, int y) {
    return QUrl(QStringLiteral("https://webrd0%1.is.autonavi.com/appmaptile"
                               "?lang=zh_cn&size=1&scale=1&style=8&x=%2&y=%3&z=%4")
                    .arg(amap_subdomain(z, x, y))
                    .arg(x)
                    .arg(y)
                    .arg(z));
}

QUrl amap_satellite_url(int z, int x, int y) {
    return QUrl(QStringLiteral("https://webst0%1.is.autonavi.com/appmaptile?style=6"
                               "&x=%2&y=%3&z=%4")
                    .arg(amap_subdomain(z, x, y))
                    .arg(x)
                    .arg(y)
                    .arg(z));
}

QUrl amap_label_url(int z, int x, int y) {
    return QUrl(QStringLiteral("https://webst0%1.is.autonavi.com/appmaptile?style=7"
                               "&x=%2&y=%3&z=%4")
                    .arg(amap_subdomain(z, x, y))
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

using UrlBuilder = QUrl (*)(int, int, int);

// R-224 源事实表（**单表**）：加源 = 加一行。styles 位图 = Road(1)/Satellite(2)/Hybrid(4)；
// 三个 URL 构造器槽按层语义取用（road / satellite+hybrid 影像 / hybrid 标注；nullptr = 无）。
struct SourceSpec {
    TileSource source;
    int styles = 0;
    UrlBuilder road = nullptr;
    UrlBuilder imagery = nullptr;
    UrlBuilder labels = nullptr;
};

int style_bit(TileStyle style) {
    switch (style) {
    case TileStyle::Satellite:
        return 2;
    case TileStyle::Hybrid:
        return 4;
    case TileStyle::Road:
        break;
    }
    return 1;
}

const SourceSpec *k_sources() {
    static const SourceSpec table[] = {
        {TileSource{QStringLiteral("amap"), QStringLiteral("高德地图"), true,
                    QStringLiteral("© 高德地图")},
         1 | 2 | 4, &amap_road_url, &amap_satellite_url, &amap_label_url},
        {TileSource{QStringLiteral("osm"), QStringLiteral("OpenStreetMap"), false,
                    QStringLiteral("© OpenStreetMap contributors")},
         1, &osm_road_url, nullptr, nullptr},
        {TileSource{QStringLiteral("esri"), QStringLiteral("Esri 影像"), false,
                    QStringLiteral("© Esri, Maxar, Earthstar Geographics")},
         2 | 4, nullptr, &esri_imagery_url, nullptr},
    };
    return table;
}

constexpr std::size_t kSourceCount = 3;

const SourceSpec *spec_of(const QString &id) {
    const SourceSpec *const table = k_sources();
    for (std::size_t i = 0; i < kSourceCount; ++i) {
        if (table[i].source.id == id) {
            return &table[i];
        }
    }
    return nullptr;
}

} // namespace

bool TileSource::supports(TileStyle style) const {
    const SourceSpec *const spec = spec_of(id);
    return spec != nullptr && (spec->styles & style_bit(style)) != 0;
}

QUrl TileSource::tile_url(int z, int x, int y, TileStyle style) const {
    if (!supports(style)) {
        return {}; // 调用方先查 supports()(UI 会回退),不支持组合不给 URL
    }
    const SourceSpec *const spec = spec_of(id);
    if (spec == nullptr) {
        return {};
    }
    if (style == TileStyle::Road) {
        return spec->road != nullptr ? spec->road(z, x, y) : QUrl();
    }
    // Satellite 与 Hybrid 的影像层同一条（Hybrid 的 tile_url = 影像层；标注走 label_url）
    return spec->imagery != nullptr ? spec->imagery(z, x, y) : QUrl();
}

QUrl TileSource::label_url(int z, int x, int y) const {
    const SourceSpec *const spec = spec_of(id);
    return spec != nullptr && spec->labels != nullptr ? spec->labels(z, x, y) : QUrl();
}

const TileSource *tile_source_by_id(const QString &id) {
    const SourceSpec *const spec = spec_of(id);
    return spec != nullptr ? &spec->source : nullptr;
}

// R-158 单源：C1 适配映射（地图源 id → 瓦片源）。非 amap（含未知 id）沿用旧兜底语义（osm）。
const TileSource *tile_source_for_provider(const QString &provider_id) {
    return tile_source_by_id(provider_id == QLatin1String("amap") ? QStringLiteral("amap")
                                                                  : QStringLiteral("osm"));
}

QStringList tile_source_ids() {
    QStringList out;
    const SourceSpec *const table = k_sources();
    for (std::size_t i = 0; i < kSourceCount; ++i) {
        out << table[i].source.id;
    }
    return out;
}

QStringList styles_for(const QString &source_id) {
    const TileSource *const source = tile_source_by_id(source_id);
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
