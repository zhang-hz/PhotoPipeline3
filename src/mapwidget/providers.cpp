// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — tile & geocoding providers (M1b-U2).
//
// M6-T3：本文件降级为「薄适配层」（docs/m6-map-upgrade.md §3.C1、§7-T3）。
// 冻结的 providers.h 逐字不变（M7-WP7 仅末尾加性追加 parse_provider_search）；瓦片与解析
// 逻辑下沉到两个新模型，本文件只保留 id 映射与结构回映射：
//   - 瓦片：layers.cpp  TileSource::tile_url(z,x,y,TileStyle::Road)
//           （osm 走 tile.openstreetmap.org；amap 走 webrd style=8）
//   - 解析：geocoder.cpp parse_search（amap 分支内部 gcj02_to_wgs84，边界同 M1b）
//
// M7-WP7 / R-158 收敛（追认见 build/m7-wp7/handoff.md）：
//   * 两份 C1 适配映射（本 TU 与 mapwidget.cpp 各拷一份的 geocoder_for_provider /
//     tile_source_for_provider）收敛到 layers.cpp / geocoder.cpp **单源**，本文件只调用；
//   * `parse_search(provider_id, body, out)` 与 geocoder 的 `parse_search(body, id, out)`
//     同名反序 → 新名 `parse_provider_search`（旧名保留转发，冻结签名零改动）；
//   * TileProvider 是 **M1b 冻结遗留壳**（E10-04"僵尸层"）：生产只剩 mapwidget 的
//     provider_by_id/gcj02/user_agent 与本层 UA/label 在用；`search_url`/`tile_url` 是
//     测试可见的冻结字节兼容件（生产搜索走 Geocoder::search_url，生产瓦片走 TileSource）。
//     删除属解冻范畴 → 按「只加性」纪律**保留并标注**，不再扩用。
//
// search_url 未下沉（冻结字节，未委托）：
// docs/m1b-tasks.md §2.4「URL 事实（冻结，不得改动）」把 nominatim limit=6 与
// amap offset=6&page=1 冻结在本层；geocoder 的新事实表则是 limit=20&accept-language=zh
// 与 city=&offset=20&page=1&extensions=all（docs/m6-map-upgrade.md §4.2）。
// 直接委托会破冻结字节，并连带改变搜索行为（6→20）。按 §0 权威链 + §3.C9，该切换属 R1 的
// 能力升级（M6-T7 已由 mapwidget/page_meta 直接消费 Geocoder 完成）；本层 search_url 自此
// **冻结遗留、仅测试可见**（R-060 口径落定），保持逐字节现状。
//
// User-Agent 仍由本层固定给出（OSM/Nominatim 使用政策要求非空 UA，由调用方随请求头发送）。
// 旧实现「非 amap 的 id 一律走 osm/nominatim 分支」的兜底语义保留，见两个 for_provider 映射。

#include "mapwidget/providers.h"

#include "mapwidget/geocoder.h"
#include "mapwidget/layers.h"

#include <QCoreApplication>
#include <QStringList>

namespace pp::map {
namespace {

constexpr const char *kUserAgent = "PhotoPipeline/0.1 (batch transcoder; dev)";

const TileProvider &osm_provider() {
    static const TileProvider p{QStringLiteral("osm"), QStringLiteral("OpenStreetMap"), false};
    return p;
}

const TileProvider &amap_provider() {
    static const TileProvider p{QStringLiteral("amap"), QStringLiteral("高德地图"), true};
    return p;
}

QString encode(const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }

} // namespace

QUrl TileProvider::tile_url(int z, int x, int y) const {
    // 路网层（Road）：amap 走 webrd0{1..4} style=8（子域 = 瓦片坐标的纯函数，R-220），
    // osm 走 tile.openstreetmap.org。映射单源 = layers.cpp 的 tile_source_for_provider。
    const TileSource *source = tile_source_for_provider(id);
    return source != nullptr ? source->tile_url(z, x, y, TileStyle::Road) : QUrl();
}

QUrl TileProvider::search_url(const QString &query, const QString &amap_key) const {
    // M1b 冻结字节逐字节保留（见文件头「search_url 未下沉」；amap 无 key 时留空 `key=` 占位，
    // 由调用方先查 amap_key）。geocoder 侧的全文搜事实表参数不同（offset=20 等），不可直接委托。
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

QString parse_provider_search(const QString &provider_id, const QByteArray &body,
                              std::vector<SearchResult> &out) {
    out.clear();
    if (provider_by_id(provider_id) == nullptr) {
        return QCoreApplication::translate("pp::map", "未知的地图提供方：%1").arg(provider_id);
    }
    // 解析与错误分类（含 amap 的 GCJ-02 → WGS-84）在 geocoder 边界内完成；
    // 此处只把 GeocodeResult 回映射成冻结结构 SearchResult{title,lat,lon}。
    // 映射单源 = geocoder.cpp 的 geocoder_for_provider（R-158）。
    const Geocoder *const geocoder = geocoder_for_provider(provider_id);
    std::vector<GeocodeResult> parsed;
    const QString error = parse_search(body, geocoder->id, parsed);
    if (!error.isEmpty()) {
        return error;
    }
    out.reserve(parsed.size());
    for (const GeocodeResult &result : parsed) {
        out.push_back(SearchResult{result.title, result.lat, result.lon});
    }
    return {};
}

// 冻结签名保留：转发到非歧义名（R-158；加性追认，行为零改动）。
QString parse_search(const QString &provider_id, const QByteArray &body,
                     std::vector<SearchResult> &out) {
    return parse_provider_search(provider_id, body, out);
}

} // namespace pp::map
