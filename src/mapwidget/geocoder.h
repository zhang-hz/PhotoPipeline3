// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6 — geocoding providers (new, non-frozen)
#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>
#include <utility>
#include <vector>

namespace pp::map {

struct GeocodeResult {
    QString title;
    QString address;
    double lat = 0, lon = 0; // WGS-84
    QString type;
    double distance_m = -1;
};

struct SuggestItem {
    QString title;
    QString address;
    double lat = 0, lon = 0;
    bool has_point = false;
};

struct Geocoder {
    QString id; // "amap" | "nominatim"
    QString label;
    bool requires_key = false;
    bool gcj02 = false; // response datum: amap=true (GCJ-02); nominatim=false (WGS-84)
    QUrl search_url(const QString &q, const QString &key,
                    std::optional<std::pair<double, double>> near) const;
    QUrl suggest_url(const QString &q, const QString &key) const;
    QUrl reverse_url(double lat, double lon, const QString &key) const;
};

const Geocoder *geocoder_by_id(const QString &id);
QStringList geocoder_ids();

// 解析到 WGS-84 出参;amap 分支内部 gcj02_to_wgs84 转换。成功返回 "",否则返回中文错误串。
QString parse_search(const QByteArray &body, const QString &geocoder_id,
                     std::vector<GeocodeResult> &out);
QString parse_suggest(const QByteArray &body, const QString &geocoder_id,
                      std::vector<SuggestItem> &out);
QString parse_reverse(const QByteArray &body, const QString &geocoder_id, QString &address_out);

} // namespace pp::map
