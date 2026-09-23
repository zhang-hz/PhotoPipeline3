// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — tile & geocoding providers (M1b-U2).
//
// Contract: docs/m1b-tasks.md §2.4 (PP-FROZEN header) + the frozen URL fact table + §4.1 U2.
// Frozen URL facts (must not drift):
//   OSM tile     https://tile.openstreetmap.org/{z}/{x}/{y}.png
//                (User-Agent required by the OSM tile usage policy; no subdomain rotation)
//   Amap tile    https://webrd0{n}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8
//                &x={x}&y={y}&z={z}   with n rotating over 1..4; datum is GCJ-02
//   Nominatim    https://nominatim.openstreetmap.org/search?format=json&limit=6&q={query}
//                (User-Agent required); body = [{"display_name","lat","lon"}, ...]
//   Amap search  https://restapi.amap.com/v3/place/text?key={key}&keywords={query}
//                &offset=6&page=1; body = pois[].name + pois[].location ("lng,lat", GCJ-02)
//
// Query parameters are percent-encoded by hand (QUrl::toPercentEncoding) so the emitted URL
// matches the frozen table byte-for-byte, including the empty `key=` placeholder the caller
// gets for amap without a configured key.

#include "mapwidget/providers.h"

#include "mapwidget/coord.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>

#include <atomic>
#include <cmath>

namespace pp::map {
namespace {

constexpr const char *kUserAgent = "PhotoPipeline/0.1 (batch transcoder; dev)";

// webrd01..04 rotation. tile_url() is const (frozen signature), so the cursor is file-static:
// one rotation sequence per process, which is exactly the documented behaviour.
std::atomic<unsigned> g_amap_tile_cursor{0};

QString msg(const char *text) { return QCoreApplication::translate("pp::map", text); }

const TileProvider &osm_provider() {
    static const TileProvider p{QStringLiteral("osm"), QStringLiteral("OpenStreetMap"), false};
    return p;
}

const TileProvider &amap_provider() {
    static const TileProvider p{QStringLiteral("amap"), QStringLiteral("高德地图"), true};
    return p;
}

QString encode(const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }

// Nominatim reports lat/lon as JSON strings; amap uses strings for `status` too. Accept both.
double json_number(const QJsonValue &v, bool *ok) {
    if (v.isString()) {
        bool parsed = false;
        const double d = v.toString().trimmed().toDouble(&parsed);
        *ok = parsed;
        return parsed ? d : 0.0;
    }
    if (v.isDouble()) {
        *ok = true;
        return v.toDouble();
    }
    *ok = false;
    return 0.0;
}

QString json_text(const QJsonValue &v) {
    if (v.isString()) {
        return v.toString();
    }
    if (v.isDouble()) {
        return QString::number(v.toDouble(), 'g', 12);
    }
    return {};
}

} // namespace

QUrl TileProvider::tile_url(int z, int x, int y) const {
    if (id == QLatin1String("amap")) {
        const unsigned n = 1u + (g_amap_tile_cursor.fetch_add(1, std::memory_order_relaxed) % 4u);
        return QUrl(QStringLiteral("https://webrd0%1.is.autonavi.com/appmaptile"
                                   "?lang=zh_cn&size=1&scale=1&style=8&x=%2&y=%3&z=%4")
                        .arg(n)
                        .arg(x)
                        .arg(y)
                        .arg(z));
    }
    return QUrl(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png").arg(z).arg(x).arg(y));
}

QUrl TileProvider::search_url(const QString &query, const QString &amap_key) const {
    if (id == QLatin1String("amap")) {
        return QUrl(QStringLiteral("https://restapi.amap.com/v3/place/text?key=") +
                    encode(amap_key) + QStringLiteral("&keywords=") + encode(query) +
                    QStringLiteral("&offset=6&page=1"));
    }
    return QUrl(
        QStringLiteral("https://nominatim.openstreetmap.org/search?format=json&limit=6&q=") +
        encode(query));
}

QString TileProvider::user_agent() const { return QString::fromLatin1(kUserAgent); }

const TileProvider *provider_by_id(const QString &id) {
    if (id == QLatin1String("osm")) {
        return &osm_provider();
    }
    if (id == QLatin1String("amap")) {
        return &amap_provider();
    }
    return nullptr;
}

QStringList provider_ids() { return {QStringLiteral("osm"), QStringLiteral("amap")}; }

QString parse_search(const QString &provider_id, const QByteArray &body,
                     std::vector<SearchResult> &out) {
    out.clear();
    const TileProvider *provider = provider_by_id(provider_id);
    if (provider == nullptr) {
        return QCoreApplication::translate("pp::map", "未知的地图提供方：%1").arg(provider_id);
    }

    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return QCoreApplication::translate("pp::map", "搜索响应解析失败：%1")
            .arg(parse_error.errorString());
    }

    if (provider->id == QLatin1String("amap")) {
        if (!doc.isObject()) {
            return msg("搜索响应解析失败：顶层不是 JSON 对象");
        }
        const QJsonObject root = doc.object();
        if (json_text(root.value(QStringLiteral("status"))) != QLatin1String("1")) {
            QString info = root.value(QStringLiteral("info")).toString();
            if (info.isEmpty()) {
                info = msg("未知错误");
            }
            return QCoreApplication::translate("pp::map", "高德搜索失败：%1").arg(info);
        }
        const QJsonArray pois = root.value(QStringLiteral("pois")).toArray();
        for (const QJsonValue &v : pois) {
            const QJsonObject poi = v.toObject();
            const QStringList parts =
                poi.value(QStringLiteral("location")).toString().split(QLatin1Char(','));
            if (parts.size() != 2) {
                continue;
            }
            bool lon_ok = false;
            bool lat_ok = false;
            const double lon_gcj = parts.at(0).trimmed().toDouble(&lon_ok);
            const double lat_gcj = parts.at(1).trimmed().toDouble(&lat_ok);
            if (!lon_ok || !lat_ok) {
                continue;
            }
            // location is "lng,lat" in GCJ-02; the public API is WGS-84.
            const std::pair<double, double> wgs = gcj02_to_wgs84(lat_gcj, lon_gcj);
            out.push_back(
                SearchResult{poi.value(QStringLiteral("name")).toString(), wgs.first, wgs.second});
        }
        return {};
    }

    if (!doc.isArray()) {
        return msg("搜索响应解析失败：顶层不是 JSON 数组");
    }
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject item = v.toObject();
        bool lat_ok = false;
        bool lon_ok = false;
        const double lat = json_number(item.value(QStringLiteral("lat")), &lat_ok);
        const double lon = json_number(item.value(QStringLiteral("lon")), &lon_ok);
        if (!lat_ok || !lon_ok || std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) {
            continue;
        }
        out.push_back(
            SearchResult{item.value(QStringLiteral("display_name")).toString(), lat, lon});
    }
    return {};
}

} // namespace pp::map
