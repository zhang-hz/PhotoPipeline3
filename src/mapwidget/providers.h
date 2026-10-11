// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — tile & geocoding providers (M1b frozen)
#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <vector>

namespace pp::map {

struct SearchResult {
    QString title;
    double lat = 0, lon = 0; // WGS-84
};

class TileProvider {
public:
    QString id;    // "osm" | "amap"
    QString label; // "OpenStreetMap" | "高德地图"
    bool gcj02 = false;

    QUrl tile_url(int z, int x, int y) const; // amap rotates webrd01..04
    // Search URL. OSM: Nominatim (no key). Amap: REST place API; empty key -> returns
    // url with key placeholder empty (caller must check amap_key first).
    QUrl search_url(const QString &query, const QString &amap_key) const;
    QString user_agent() const; // "PhotoPipeline/0.1 (batch transcoder; dev)"
};

// nullptr if unknown id
const TileProvider *provider_by_id(const QString &id);
// {"osm","amap"}
QStringList provider_ids();
// Parse search JSON -> results (WGS-84; amap converts GCJ->WGS internally).
// Returns "" on success, else human-readable error (Chinese).
QString parse_search(const QString &provider_id, const QByteArray &body,
                     std::vector<SearchResult> &out);

// ---- M7-WP7 加性追认（R-158；裁定登记 build/m7-wp7/handoff.md）-----------------
// 解析入口的**非歧义名**：与 geocoder 的 `parse_search(body, geocoder_id, out)` 划清边界
// （两者曾同名且参数序相反 —— 手滑温床）。既有 `parse_search(provider_id, body, out)` 保留
// 并转发到本函数，冻结签名/行为零改动。生产与新测试一律用本名。
QString parse_provider_search(const QString &provider_id, const QByteArray &body,
                              std::vector<SearchResult> &out);

} // namespace pp::map
