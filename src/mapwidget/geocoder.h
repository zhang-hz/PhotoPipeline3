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
    // 坐标入参**一律 WGS-84**（R-219：search_url 的 near 与 reverse_url 同一约定 —— 需要
    // 源基准（amap=GCJ-02）时在 URL 构造边界内转换，调用方永不预转换）；解析出参亦为 WGS-84。
    QUrl search_url(const QString &q, const QString &key,
                    std::optional<std::pair<double, double>> near) const;
    QUrl suggest_url(const QString &q, const QString &key) const;
    QUrl reverse_url(double lat, double lon, const QString &key) const;
};

const Geocoder *geocoder_by_id(const QString &id);
// R-158（M7-WP7）：C1 适配映射单源 —— 地图源 id → 地理编码源（"amap"→amap；其余含未知→
// nominatim）。providers.cpp / mapwidget.cpp 共同消费，不再各自持拷贝。
const Geocoder *geocoder_for_provider(const QString &provider_id);
QStringList geocoder_ids();

// R-223（M7-WP7）：搜索结果/结果图钉/列表行跨层对齐的**稳定键**（title+lat+lon 规范化串）。
// 三方（map 内部 results、map result_pins、页面结果列表）各自算同一键，命中/点行按键对齐
// —— 不再依赖"同一信号同序"的脆弱序号契约（点 A 钉弹 B 行的风险面）。
QString result_key(const QString &title, double lat, double lon);

// 解析到 WGS-84 出参;amap 分支内部 gcj02_to_wgs84 转换。成功返回 "",否则返回中文错误串。
QString parse_search(const QByteArray &body, const QString &geocoder_id,
                     std::vector<GeocodeResult> &out);
QString parse_suggest(const QByteArray &body, const QString &geocoder_id,
                      std::vector<SuggestItem> &out);
QString parse_reverse(const QByteArray &body, const QString &geocoder_id, QString &address_out);

} // namespace pp::map
