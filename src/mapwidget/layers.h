// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6 — tile sources & layer styles (new, non-frozen)
#pragma once
#include <QString>
#include <QStringList>
#include <QUrl>

namespace pp::map {

enum class TileStyle { Road, Satellite, Hybrid };

struct TileSource {
    QString id; // "amap" | "osm" | "esri"
    QString label;
    bool gcj02 = false; // raster datum: amap=true (GCJ-02); osm/esri=false (WGS-84)
    QString attribution;
    bool supports(TileStyle) const;
    QUrl tile_url(int z, int x, int y, TileStyle) const;
    // Hybrid 标注层(amap style=7)。仅 Hybrid 绘制时调用;无标注层的源(osm/esri)返回空 QUrl。
    QUrl label_url(int z, int x, int y) const;
};

const TileSource *tile_source_by_id(const QString &id);
// R-158（M7-WP7）：C1 适配映射单源 —— 地图源 id → 瓦片源（"amap"→amap；其余含未知→osm）。
// providers.cpp / mapwidget.cpp 共同消费，不再各自持拷贝。
const TileSource *tile_source_for_provider(const QString &provider_id);
QStringList tile_source_ids();
QStringList styles_for(const QString &source_id);
QString tile_style_name(TileStyle);              // "road"|"satellite"|"hybrid"
TileStyle tile_style_from_name(const QString &); // 未知 -> Road

} // namespace pp::map
