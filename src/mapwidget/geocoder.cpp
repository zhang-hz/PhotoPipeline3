// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6 — geocoding providers (M6-T2).
//
// Contract: docs/m6-map-upgrade.md §3.C1/C3 + §4.2 endpoint fact table (frozen with this file).
// Pure URL/JSON model, zero Qt Widgets — compiled into pp_core so unit tests can link it
// (CMake glob pre-wired by the architect). Public coordinates are WGS-84 (EXIF-compliant):
// amap responses are GCJ-02 and get converted at this parse boundary (the same rule the
// providers.cpp adapter delegates to — `parse_provider_search` maps GeocodeResult back to the
// frozen SearchResult without touching the values); nominatim is WGS-84 end to end.
//
// M7-WP7 / R-219：URL 构造的坐标入参**统一 WGS-84、边界内转换** —— 原约定自相矛盾
// （search_url 的 near 内转 GCJ-02，reverse_url 却要求调用方预转）。现 search_url/reverse_url
// 同一约定：入参 WGS-84，amap 线上格式（GCJ-02）在本文件的 URL 边界内转换。
//
// Frozen endpoint facts (must not drift):
//   amap search   https://restapi.amap.com/v3/place/text?key=&keywords=&city=&offset=20&page=1
//                 &extensions=all
//   amap around   https://restapi.amap.com/v3/place/around?key=&location={lon},{lat}&radius=1000
//                 &keywords=&offset=20&extensions=all   (location is GCJ-02: the WGS-84 `near`
//                 input is converted via wgs84_to_gcj02 before it is written)
//   amap suggest  https://restapi.amap.com/v3/assistant/inputtips?key=&keywords=&city=&datatype=poi
//   amap reverse https://restapi.amap.com/v3/geocode/regeo?key=&location={lon},{lat}&extensions=all
//                 (location is GCJ-02: the WGS-84 input is converted via wgs84_to_gcj02)
//   nominatim     https://nominatim.openstreetmap.org/search?format=json&limit=20
//                 &accept-language=zh&q=
//                 https://nominatim.openstreetmap.org/reverse?format=json&lat=&lon=
//                 &accept-language=zh     (suggest unsupported -> empty QUrl)
//
// Query parameters are percent-encoded by hand (QUrl::toPercentEncoding), same convention as
// providers.cpp; the `{lon},{lat}` pair keeps its raw comma (a legal sub-delim), matching the
// fact table above.

#include "mapwidget/geocoder.h"

#include "mapwidget/coord.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <utility>

namespace pp::map {
namespace {

QString msg(const char *text) { return QCoreApplication::translate("pp::map", text); }

QString encode(const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }

// 6 decimals ≈ 0.1 m — matches the decimal-degree examples on the amap wire.
QString coord_number(double v) { return QString::number(v, 'f', 6); }

const Geocoder &amap_geocoder() {
    static const Geocoder g{QStringLiteral("amap"), QStringLiteral("高德地图"), true, true};
    return g;
}

const Geocoder &nominatim_geocoder() {
    static const Geocoder g{QStringLiteral("nominatim"), QStringLiteral("Nominatim (OSM)"), false,
                            false};
    return g;
}

// Nominatim reports lat/lon as JSON strings; amap uses strings for `status`/`distance` too.
// Accept both string and number.
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
    return {}; // 缺失/数组字段：amap 空字段常回 []
}

// amap 结构化地址：pname + cityname + adname + address 拼接，空字段跳过。
QString amap_address(const QJsonObject &o) {
    QString address;
    for (const char *key : {"pname", "cityname", "adname", "address"}) {
        address += json_text(o.value(QLatin1String(key)));
    }
    return address;
}

// amap 的 "lng,lat" 字符串（GCJ-02）→ WGS-84；解析失败返回 false。
bool amap_location_wgs(const QJsonValue &v, double *lat, double *lon) {
    const QStringList parts = v.toString().split(QLatin1Char(','));
    if (parts.size() != 2) {
        return false;
    }
    bool lon_ok = false;
    bool lat_ok = false;
    const double lon_gcj = parts.at(0).trimmed().toDouble(&lon_ok);
    const double lat_gcj = parts.at(1).trimmed().toDouble(&lat_ok);
    if (!lon_ok || !lat_ok) {
        return false;
    }
    const std::pair<double, double> wgs = gcj02_to_wgs84(lat_gcj, lon_gcj);
    *lat = wgs.first;
    *lon = wgs.second;
    return true;
}

} // namespace

QUrl Geocoder::search_url(const QString &q, const QString &key,
                          std::optional<std::pair<double, double>> near) const {
    if (id == QLatin1String("amap")) {
        if (near.has_value()) {
            // near 为 WGS-84；amap around 的 location 必须是 GCJ-02。
            const std::pair<double, double> gcj = wgs84_to_gcj02(near->first, near->second);
            return QUrl(QStringLiteral("https://restapi.amap.com/v3/place/around?key=") +
                        encode(key) + QStringLiteral("&location=") + coord_number(gcj.second) +
                        QLatin1Char(',') + coord_number(gcj.first) +
                        QStringLiteral("&radius=1000&keywords=") + encode(q) +
                        QStringLiteral("&offset=20&extensions=all"));
        }
        return QUrl(QStringLiteral("https://restapi.amap.com/v3/place/text?key=") + encode(key) +
                    QStringLiteral("&keywords=") + encode(q) +
                    QStringLiteral("&city=&offset=20&page=1&extensions=all"));
    }
    return QUrl(QStringLiteral("https://nominatim.openstreetmap.org/search?format=json&limit=20"
                               "&accept-language=zh&q=") +
                encode(q));
}

QUrl Geocoder::suggest_url(const QString &q, const QString &key) const {
    if (id == QLatin1String("amap")) {
        return QUrl(QStringLiteral("https://restapi.amap.com/v3/assistant/inputtips?key=") +
                    encode(key) + QStringLiteral("&keywords=") + encode(q) +
                    QStringLiteral("&city=&datatype=poi"));
    }
    return {}; // nominatim 无联想端点
}

QUrl Geocoder::reverse_url(double lat, double lon, const QString &key) const {
    if (id == QLatin1String("amap")) {
        // R-219：入参 WGS-84（与 search_url 的 near 同一约定），amap 线上 location = GCJ-02
        // —— 转换只发生在本 URL 边界内，调用方永不预转换。
        const std::pair<double, double> gcj = wgs84_to_gcj02(lat, lon);
        return QUrl(QStringLiteral("https://restapi.amap.com/v3/geocode/regeo?key=") + encode(key) +
                    QStringLiteral("&location=") + coord_number(gcj.second) + QLatin1Char(',') +
                    coord_number(gcj.first) + QStringLiteral("&extensions=all"));
    }
    return QUrl(QStringLiteral("https://nominatim.openstreetmap.org/reverse?format=json&lat=") +
                coord_number(lat) + QStringLiteral("&lon=") + coord_number(lon) +
                QStringLiteral("&accept-language=zh"));
}

const Geocoder *geocoder_by_id(const QString &id) {
    if (id == QLatin1String("amap")) {
        return &amap_geocoder();
    }
    if (id == QLatin1String("nominatim")) {
        return &nominatim_geocoder();
    }
    return nullptr;
}

// R-158（M7-WP7）：C1 适配映射单源（地图源 id → 地理编码源）。"amap" → amap；其余（osm 及
// 未知 id）沿用旧兜底语义 → nominatim。
const Geocoder *geocoder_for_provider(const QString &provider_id) {
    return geocoder_by_id(provider_id == QLatin1String("amap") ? QStringLiteral("amap")
                                                               : QStringLiteral("nominatim"));
}

// R-223（M7-WP7）：结果稳定键（跨层对齐唯一身份）：title + 规范化坐标（1e-6° 网格）。
QString result_key(const QString &title, double lat, double lon) {
    return title + QLatin1Char('\x1f') + QString::number(lat, 'f', 6) + QLatin1Char(',') +
           QString::number(lon, 'f', 6);
}

QStringList geocoder_ids() { return {QStringLiteral("amap"), QStringLiteral("nominatim")}; }

QString parse_search(const QByteArray &body, const QString &geocoder_id,
                     std::vector<GeocodeResult> &out) {
    out.clear();
    const Geocoder *geocoder = geocoder_by_id(geocoder_id);
    if (geocoder == nullptr) {
        return msg("未知的地理编码源：%1").arg(geocoder_id);
    }

    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return QCoreApplication::translate("pp::map", "搜索响应解析失败：%1")
            .arg(parse_error.errorString());
    }

    if (geocoder->id == QLatin1String("amap")) {
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
        for (const QJsonValue v : pois) { // 按值=显式拷贝（迭代器引用类型是代理，不绑 const&）
            const QJsonObject poi = v.toObject();
            double lat = 0;
            double lon = 0;
            if (!amap_location_wgs(poi.value(QStringLiteral("location")), &lat, &lon)) {
                continue;
            }
            GeocodeResult result;
            result.title = poi.value(QStringLiteral("name")).toString();
            result.address = amap_address(poi);
            result.lat = lat;
            result.lon = lon;
            result.type = poi.value(QStringLiteral("type")).toString();
            bool distance_ok = false;
            const double distance =
                json_number(poi.value(QStringLiteral("distance")), &distance_ok);
            result.distance_m = distance_ok ? distance : -1; // 仅 around 响应带 distance
            out.push_back(std::move(result));
        }
        return {};
    }

    if (!doc.isArray()) {
        return msg("搜索响应解析失败：顶层不是 JSON 数组");
    }
    for (const QJsonValue v : doc.array()) { // 按值=显式拷贝（同上）
        const QJsonObject item = v.toObject();
        bool lat_ok = false;
        bool lon_ok = false;
        const double lat = json_number(item.value(QStringLiteral("lat")), &lat_ok);
        const double lon = json_number(item.value(QStringLiteral("lon")), &lon_ok);
        if (!lat_ok || !lon_ok || std::fabs(lat) > 90.0 || std::fabs(lon) > 180.0) {
            continue;
        }
        GeocodeResult result; // nominatim 本就 WGS-84，无转换；无结构化地址字段。
        result.title = item.value(QStringLiteral("display_name")).toString();
        result.lat = lat;
        result.lon = lon;
        result.type = item.value(QStringLiteral("type")).toString();
        out.push_back(std::move(result));
    }
    return {};
}

QString parse_suggest(const QByteArray &body, const QString &geocoder_id,
                      std::vector<SuggestItem> &out) {
    out.clear();
    const Geocoder *geocoder = geocoder_by_id(geocoder_id);
    if (geocoder == nullptr) {
        return msg("未知的地理编码源：%1").arg(geocoder_id);
    }
    if (geocoder->id != QLatin1String("amap")) {
        return msg("该源不支持联想搜索：%1").arg(geocoder_id);
    }

    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return QCoreApplication::translate("pp::map", "联想响应解析失败：%1")
            .arg(parse_error.errorString());
    }
    if (!doc.isObject()) {
        return msg("联想响应解析失败：顶层不是 JSON 对象");
    }
    const QJsonObject root = doc.object();
    if (json_text(root.value(QStringLiteral("status"))) != QLatin1String("1")) {
        QString info = root.value(QStringLiteral("info")).toString();
        if (info.isEmpty()) {
            info = msg("未知错误");
        }
        return QCoreApplication::translate("pp::map", "高德联想失败：%1").arg(info);
    }
    const QJsonArray tips = root.value(QStringLiteral("tips")).toArray();
    for (const QJsonValue v : tips) { // 按值=显式拷贝（同上）
        const QJsonObject tip = v.toObject();
        SuggestItem item;
        item.title = tip.value(QStringLiteral("name")).toString();
        item.address = json_text(tip.value(QStringLiteral("district"))) +
                       json_text(tip.value(QStringLiteral("address")));
        double lat = 0;
        double lon = 0;
        if (amap_location_wgs(tip.value(QStringLiteral("location")), &lat, &lon)) {
            item.lat = lat;
            item.lon = lon;
            item.has_point = true;
        }
        out.push_back(std::move(item));
    }
    return {};
}

QString parse_reverse(const QByteArray &body, const QString &geocoder_id, QString &address_out) {
    address_out.clear();
    const Geocoder *geocoder = geocoder_by_id(geocoder_id);
    if (geocoder == nullptr) {
        return msg("未知的地理编码源：%1").arg(geocoder_id);
    }

    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        return QCoreApplication::translate("pp::map", "逆地理响应解析失败：%1")
            .arg(parse_error.errorString());
    }
    if (!doc.isObject()) {
        return msg("逆地理响应解析失败：顶层不是 JSON 对象");
    }
    const QJsonObject root = doc.object();

    if (geocoder->id == QLatin1String("amap")) {
        if (json_text(root.value(QStringLiteral("status"))) != QLatin1String("1")) {
            QString info = root.value(QStringLiteral("info")).toString();
            if (info.isEmpty()) {
                info = msg("未知错误");
            }
            return QCoreApplication::translate("pp::map", "高德逆地理失败：%1").arg(info);
        }
        address_out = root.value(QStringLiteral("regeocode"))
                          .toObject()
                          .value(QStringLiteral("formatted_address"))
                          .toString();
        if (address_out == QLatin1String("[]")) {
            address_out.clear(); // amap 空地址可能回字符串 "[]"
        }
        return {};
    }

    if (root.contains(QStringLiteral("error"))) {
        return QCoreApplication::translate("pp::map", "Nominatim 逆地理失败：%1")
            .arg(json_text(root.value(QStringLiteral("error"))));
    }
    address_out = root.value(QStringLiteral("display_name")).toString();
    return {};
}

} // namespace pp::map
