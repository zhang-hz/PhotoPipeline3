// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — self-drawn slippy map (M1b-U2).
//
// Contract: docs/m1b-tasks.md §2.5 (PP-FROZEN header + frozen behaviour spec) + §4.1 U2.
//
// Boundary rule (frozen): every public coordinate is WGS-84; the internal tile math runs in the
// provider datum (GCJ-02 for amap) and the conversion happens only here, at the boundary.
//
// Drawing: 256 px tiles placed with the standard slippy formula
//   x_world = (lon + 180) / 360 * 2^z * 256
//   y_world = (0.5 - ln((1 + sin lat) / (1 - sin lat)) / (4 pi)) * 2^z * 256
// Missing tiles stay light grey with a thin tile-grid; fetches are bounded to <= 4 in flight.
//
// M6-T5 layers: tiles are drawn through the W-M1 model (layers.h) — the effective source comes
// from select_source() and the per-frame stack from paint_layers(): Road -> "road", Satellite ->
// "satellite", Hybrid -> "satellite" + "label" (imagery below, labels above; a source without a
// label layer skips it). The cache key is {source, layer, zoom, x, y}; LRU/byte budget unchanged.
//
// M6-T6 pins: result pins (solid accent dot + numbered bubble: the label, or the index when the
// label is empty) are drawn above the tiles and below the main marker, so the main marker is
// always on top. A non-drag click first hit-tests them (nearest pin within kMarkerRadius + 6 px of
// the cursor): on a hit the main marker moves to that pin and pin_clicked(index) is emitted — the
// state-then-signal order of search_select; only without a hit does the click fall back to the
// frozen point-selection behaviour.
//
// M6-T7 search pipeline: run_search goes through the W-M1 Geocoder model (geocoder.h) — the
// provider maps to its geocoder (osm→nominatim, amap→amap) and the query is biased to the viewport
// centre (`near`, WGS-84; geocoder.cpp converts to the source datum for amap's `around`), falling
// back to the full-text endpoint when the biased search finds nothing. Results feed both the frozen
// search_finished (titles in the frozen order, so search_select(i) keeps indexing d.results) and
// the additive search_results_ready, plus one result pin per hit. run_suggest / run_reverse add the
// inputtips and regeo endpoints. Every search-pipeline request keeps exactly one in-flight reply
// (a new request aborts the old), transfers with the same 8 s timeout and UA as tiles, sends
// nothing while offline, and reports failures by class (offline / missing key / request failed /
// parse failed / no match). Credit label: on_paint now reads the effective source's attribution
// (layers.cpp is the single source) instead of hardcoding the text here (D1).
//
// M6-T12 体验件（§5.3 / §7-T12，加性登记 A3）：① `set_sat_labels(false)` 让 Hybrid 只画影像层
// （paint_layers 对 label 层的唯一开关）；② `zoom_in/zoom_out` = 视口中心锚点的 ±1 档缩放（与滚轮
// 同一内部字段，clamp 到 [kMinZoom, kMaxZoom]，中心逐位不动）；③ on_paint 左下比例尺（当前
// zoom/纬度下的米每像素 → 1/2/5 × 10^k 档，白底 α190 + 6pt 与版权行同口径）；④ 光标世界坐标
// 经 datum→WGS-84 后由 `cursor_moved` 发出（约 100 ms 节流；setMouseTracking 使悬停也可回显 ——
// 冻结的滚轮/拖拽/点击三件套行为不变）。
//
// Events (R-222 修正失实叙事): the PP-FROZEN header is frozen in the "加性 + 追认" sense —
// appended declarations are allowed (it has accumulated additive members since M1b), but
// adding virtual overrides would change the class's vtable/object contract, which is a thaw,
// not an addition. So an event filter owned by the pimpl receives paint/mouse/wheel for the
// widget; the frozen behaviour spec (§2.5 三件套) stays as declared.
//
// M7-WP7 behaviour fixes (all opt-in-neutral; frozen lines unchanged):
//   R-012 suggest lifecycle is generation-scoped (run_suggest("") aborts too; run_search
//     aborts in-flight suggest; stale callbacks are dropped) —
//   R-013 request_failure_text redacts query secrets (key=…) from error strings;
//   R-077 an empty amap key never leaves the process (guidance text instead of a request);
//   R-101 failed tiles schedule a delayed repaint (=定点重抓), grey tiles do not stick;
//   R-102 the "!current" (superseded) test runs before the offline branch in all handlers;
//   R-103 world_to_latlon clamps to the Mercator band (|lat| <= 85.05112877980659);
//   R-161 insert_tile returns success so the failure streak is only reset by real tiles;
//   R-221 on_paint no longer dispatches network work: missing tiles are queued and fetched
//     after the paint (paint is idempotent);
//   R-223 pin hits report a stable key (pin_selected) alongside the frozen pin_clicked(index)
//     and search_select_key() picks results by key.

#include "mapwidget/mapwidget.h"

#include "mapwidget/geocoder.h"
#include "mapwidget/layers.h"
#include "mapwidget/providers.h"

#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace pp::map {

namespace {

constexpr int kTileSize = 256;
constexpr int kMinZoom = 2;
constexpr int kMaxZoom = 18;
constexpr int kMaxConcurrentFetches = 4;   // frozen: concurrency <= 4
constexpr int kFailureBannerThreshold = 3; // frozen: >= 3 consecutive failures -> banner
constexpr double kClickThresholdPx = 4.0;  // frozen: press/release displacement < 4 px = click
constexpr double kMaxMercatorLat = 85.05112877980659;
constexpr double kPi = 3.14159265358979323846;
constexpr qint64 kBytesPerMb = 1024 * 1024;
constexpr int kDefaultCacheMb = 64;     // frozen default byte budget
constexpr int kRequestTimeoutMs = 8000; // frozen: request timeout 8 s
// R-311（M7-WP7）：HUD 字号**一处命名**。原实现比例尺借用 kCopyrightPointSize（"版权行字号"
// 被两个不相干 HUD 元素共用，命名失实）；现统一为 kHudPointSize —— 地图自绘 HUD（比例尺 +
// 版权行）同字号同白底 α190。HUD 双轨机制在本文件头「Drawing」段落定：mapwidget 自绘 =
// 比例尺/版权行（贴角、随图重绘）；宿主覆盖层（page_meta 的 chip/钮）= 交互控件（reparent
// 随地图走）—— 两轨各管各，色令牌分别取 kHudPointSize 同档与 overlay_tokens。
constexpr int kHudPointSize = 6;     // frozen: 6 pt credit label (and scale bar, R-311 rename)
constexpr int kSearchZoomFloor = 12; // ruling 2026-09-19: a search hit zooms to >= 12
constexpr int kMarkerRadius = 7;
constexpr int kMarkerArm = 5;
// M6-T6 结果图钉（§4.4）：实心圆半径 / 编号气泡字号与文本像素上限 / 命中阈值（光标-钉心屏距）。
constexpr int kPinRadius = 5;
constexpr int kPinBubblePointSize = 7;
constexpr int kPinBubbleTextMax = 64; // 长地名省略，避免气泡盖住大片地图
constexpr double kPinHitRadius = kMarkerRadius + 6;
// M6-T12 体验件（§5.3 / §7-T12）：光标坐标信号的节流窗 / 比例尺（左下）的目标段长与档系。
constexpr int kCursorThrottleMs = 100;
constexpr int kScaleBarTargetPx = 60;           // 取档前的目标段长（实际段长 = 取整档反算）
constexpr double kScaleBarMppZ0 = 156543.03392; // z=0、256 px 瓦片下的赤道米/像素
// R-101（M7-WP7）：瓦片失败后的**延迟重抓**窗（一次性定时器；离线不排）。
constexpr int kTileRetryDelayMs = 750;

// M6-T5 缓存 key 的 layer 维度（§4.1）：路网 / 影像 / Hybrid 标注叠加层。
const char *const kLayerRoad = "road";
const char *const kLayerSatellite = "satellite";
const char *const kLayerLabel = "label";

double clamp_lat(double lat) { return std::clamp(lat, -kMaxMercatorLat, kMaxMercatorLat); }

double world_span(int zoom) { return double(kTileSize) * double(std::int64_t(1) << zoom); }

int wrap_tile_x(int x, int tile_count) {
    const int m = x % tile_count;
    return m < 0 ? m + tile_count : m;
}

QPointF latlon_to_world(double lat, double lon, int zoom) {
    const double span = world_span(zoom);
    const double s = std::sin(clamp_lat(lat) / 180.0 * kPi);
    return {(lon + 180.0) / 360.0 * span,
            (0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * kPi)) * span};
}

std::pair<double, double> world_to_latlon(QPointF world, int zoom) {
    const double span = world_span(zoom);
    const double n = kPi * (1.0 - 2.0 * world.y() / span);
    // R-103（M7-WP7）：世界方块外（上下灰区）的 y 会给出 |lat|→90 的越带纬度；Mercator 有效
    // 带 = ±85.0511…（与 latlon_to_world 的 clamp_lat 同界）。取点/拖拽/光标读数都经此处。
    return {clamp_lat(std::atan(std::sinh(n)) * 180.0 / kPi), world.x() / span * 360.0 - 180.0};
}

// C1 适配映射单源（R-158）：provider id → 地理编码源由 geocoder.cpp 的
// `geocoder_for_provider(QString)` 唯一定义；本 TU 原来的第二份拷贝已删，调用点直接用单源。

} // namespace

// Tile cache key. Declared at namespace scope (not in the anonymous namespace) so that QHash
// finds qHash() by argument-dependent lookup; it stays private to this translation unit.
// M6-T5：维度由 {provider} 扩为 {source, layer}（layer ∈ road/satellite/label），使 Hybrid 的
// 影像层与标注层、以及回退源（osm 的卫星走 esri）各有独立条目；LRU/字节预算逻辑不变。
struct TileKey {
    QString source; // 瓦片源 id："amap" | "osm" | "esri"
    QString layer;  // "road" | "satellite" | "label"
    int zoom = 0;
    int x = 0;
    int y = 0;
};

inline bool operator==(const TileKey &a, const TileKey &b) {
    return a.zoom == b.zoom && a.x == b.x && a.y == b.y && a.source == b.source &&
           a.layer == b.layer;
}

inline size_t qHash(const TileKey &k, size_t seed = 0) {
    size_t h = qHash(k.source, seed);
    h = h * 1000003u + qHash(k.layer);
    h = h * 1000003u + size_t(k.zoom);
    h = h * 1000003u + size_t(k.x);
    h = h * 1000003u + size_t(k.y);
    return h;
}

// 图层键 → URL 的唯一出口（缓存命中判定与抓取共用同一个 key）。「road」走 Road 路网，
// 「satellite」走影像层（Hybrid 的底层同为影像），「label」走 Hybrid 标注叠加层；
// 源不支持该层（osm 无影像层、esri 无标注层）时返回空 URL —— 调用方据此不发请求。
// static：TileKey 需在匿名命名空间外（ADL 找 qHash），本辅助函数随之留在此处但仅本 TU 可见。
static QUrl tile_url_for(const TileKey &key) {
    const TileSource *const source = tile_source_by_id(key.source);
    if (source == nullptr) {
        return {};
    }
    if (key.layer == QLatin1String(kLayerLabel)) {
        return source->label_url(key.zoom, key.x, key.y);
    }
    if (key.layer == QLatin1String(kLayerSatellite)) {
        // 影像层：优先 Satellite；只支持 Hybrid 的源退用其影像层（当前源皆支持前者）。
        return source->tile_url(key.zoom, key.x, key.y,
                                source->supports(TileStyle::Satellite) ? TileStyle::Satellite
                                                                       : TileStyle::Hybrid);
    }
    return source->tile_url(key.zoom, key.x, key.y, TileStyle::Road); // 路网层：只按 Road 取
}

struct MapWidget::Impl {
    struct PendingTile {
        QNetworkReply *reply = nullptr;
        TileKey key; // 完成的瓦片按这个键入缓存（source/layer/zoom/x/y）
    };

    // 本帧要绘制的图层（source + layer 键；Hybrid = 影像 + 标注两条）。
    struct PaintLayer {
        const TileSource *source = nullptr;
        QString layer;
    };

    struct CacheEntry {
        QPixmap pixmap;
        qint64 footprint = 0; // decoded bytes charged against the budget
        std::list<TileKey>::iterator lru_pos;
    };

    // Receives the QWidget events the frozen header cannot declare overrides for.
    struct EventFilter : QObject {
        explicit EventFilter(Impl *owner) : QObject(owner->q), impl(owner) {}
        bool eventFilter(QObject *watched, QEvent *event) override;
        Impl *impl = nullptr;
    };

    MapWidget *q = nullptr;
    const TileProvider *provider = nullptr;
    QString amap_key;
    bool offline = false;

    double center_lat = 35.0; // datum of the active provider
    double center_lon = 105.0;
    int zoom = 4;

    bool marker_set = false;
    double marker_lat = 0.0; // WGS-84
    double marker_lon = 0.0;

    QNetworkAccessManager *nam = nullptr;
    QNetworkReply *search_reply = nullptr;
    QNetworkReply *suggest_reply = nullptr; // M6-T7：每类请求至多一个在途 reply
    quint64 suggest_generation = 0;         // R-012：联想查询世代（空查询/搜索/新查询均推进）
    QNetworkReply *reverse_reply = nullptr;
    QLabel *banner = nullptr;
    EventFilter *filter = nullptr;

    QPoint press_pos;
    QPoint last_drag_pos;
    bool pressed = false;
    bool dragging = false;
    int consecutive_failures = 0;
    bool retry_scheduled = false;       // R-101：失败重抓定时器在途（不叠加）
    bool fetch_flush_scheduled = false; // R-221：paint 只登记缺瓦片，flush 定时器在途
    std::vector<TileKey> fetch_wanted;  // R-221：本次可视区登记的待抓瓦片（paint 外发请求）

    std::vector<PendingTile> pending;
    std::list<TileKey> lru; // front = most recently used
    QHash<TileKey, CacheEntry> cache;
    qint64 cache_bytes = 0;
    qint64 cache_budget = qint64(kDefaultCacheMb) * kBytesPerMb;

    std::vector<SearchResult> results;

    // M6-T4 加性状态：图层 + 结果图钉（图层绘制见 M6-T5 select_source/paint_layers；图钉绘制与
    // 命中见 M6-T6 on_paint/on_mouse_release）。
    TileStyle tile_style_ = TileStyle::Road;
    std::vector<Pin> result_pins;
    // M6-T12 加性状态：Hybrid 的「叠标注」开关（默认 true = 现行为）+ 光标信号节流钟。
    bool sat_labels_ = true;
    QElapsedTimer cursor_clock;

    // ---- M6-T5 图层选择 ----------------------------------------------------------
    // select_source(style)：给定图层 → 有效瓦片源。当前 provider 的源支持该图层则用它；
    // 不支持时（如 osm 无卫星/混合层）回退 esri（esri 支持 Satellite/Hybrid）；Road 永远用当前源。
    //
    // 基准不漂（契约 C3）：回退只发生在 osm → esri 之间，二者栅格皆 WGS-84；amap 三图层同为
    // GCJ-02。内部中心基准面只由 provider->gcj02 决定，与所选源无关 —— 切图层/回退**不做**
    // 任何 datum 转换，center_lat/lon 与 marker 的 WGS 坐标逐位不变。
    const TileSource *select_source(TileStyle style) const {
        const TileSource *const current =
            provider != nullptr ? tile_source_by_id(provider->id) : nullptr;
        if (style == TileStyle::Road) {
            return current; // 路网：永远当前源（不参与回退）
        }
        if (current != nullptr && current->supports(style)) {
            return current;
        }
        return tile_source_by_id(QStringLiteral("esri")); // 卫星/混合回退：esri 二者皆支持
    }

    // 本帧图层栈（自下而上）：Road → road；Satellite → satellite；Hybrid → satellite + label。
    // 无标注层的源（osm/esri）返回空 label_url → 该层无 URL 不请求，即「无标注层则跳过」。
    // M6-T12：Hybrid 的标注层受 sat_labels_（set_sat_labels）控制 —— false = 只画影像层。
    std::vector<PaintLayer> paint_layers() const {
        std::vector<PaintLayer> stack;
        const TileSource *const source = select_source(tile_style_);
        if (source == nullptr) {
            return stack;
        }
        if (tile_style_ == TileStyle::Road) {
            stack.push_back(PaintLayer{source, QString::fromLatin1(kLayerRoad)});
            return stack;
        }
        stack.push_back(PaintLayer{source, QString::fromLatin1(kLayerSatellite)});
        if (tile_style_ == TileStyle::Hybrid && sat_labels_) {
            stack.push_back(PaintLayer{source, QString::fromLatin1(kLayerLabel)});
        }
        return stack;
    }

    // ---- M6-T12 体验件（§5.3 / §7-T12） -----------------------------------------
    // 缩放 ±1：与滚轮缩放**同一内部字段**（zoom），锚点 = 视口中心 → 中心逐位不动（滚轮是
    // 以光标为锚，两者只在锚点上有别）；越界档位 clamp 后无变化即提前返回（不空重绘）。
    void zoom_step(int delta) {
        const int target = std::clamp(zoom + delta, kMinZoom, kMaxZoom);
        if (target == zoom) {
            return;
        }
        zoom = target;
        q->update();
    }

    // 光标坐标回显（约 100 ms 节流）：屏坐标 → 世界 → 内部基准面 → WGS-84（与取点/绘制同一条
    // 转换管线）；仅在移动时发（无定时器、无空闲发射）。首帧钟未启动 → 立即发。
    void emit_cursor_readout(QPointF screen) {
        if (cursor_clock.isValid() && cursor_clock.elapsed() < kCursorThrottleMs) {
            return;
        }
        cursor_clock.restart();
        const std::pair<double, double> datum = world_to_latlon(screen_to_world(screen), zoom);
        const std::pair<double, double> wgs = datum_to_wgs(datum.first, datum.second);
        emit q->cursor_moved(wgs.first, wgs.second);
    }

    // 左下比例尺（§5.3）：当前 zoom/纬度下的「米每像素」把 ~60 px 段长折成 1/2/5 × 10^k 的整
    // 档（100 m / 500 m / 1 km …），再按该档反算实际段长绘制；白底 α190 + 6pt 与右下版权行同
    // 口径（卫星底图上仍可读）。纬度取视口中心（同一比例尺的横轴近菱形，取中心纬度即用户所在档）。
    void draw_scale_bar(QPainter &painter, int h) const {
        const double mpp = kScaleBarMppZ0 * std::cos(clamp_lat(center_lat) / 180.0 * kPi) /
                           double(std::int64_t(1) << zoom);
        if (!(mpp > 0.0) || !std::isfinite(mpp)) {
            return;
        }
        const double raw_m = double(kScaleBarTargetPx) * mpp;
        const double mag = std::pow(10.0, std::floor(std::log10(raw_m)));
        double nice_m = mag;
        for (const double factor : {1.0, 2.0, 5.0}) {
            if (mag * factor <= raw_m) {
                nice_m = mag * factor;
            }
        }
        const QString label = nice_m >= 1000.0
                                  ? QStringLiteral("%1 km").arg(nice_m / 1000.0, 0, 'g', 4)
                                  : QStringLiteral("%1 m").arg(nice_m, 0, 'g', 4);
        const QString zero = QStringLiteral("0");
        const int bar_px = std::max(8, int(std::lround(nice_m / mpp)));

        painter.save();
        QFont font = painter.font();
        font.setPointSize(kHudPointSize);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const int pad = 5;
        const int gap = 4;
        const int box_w = 2 * pad + metrics.horizontalAdvance(zero) + gap + bar_px + gap +
                          metrics.horizontalAdvance(label);
        const int box_h = metrics.height() + 4;
        const QRect box(6, h - 6 - box_h, box_w, box_h);
        painter.fillRect(box, QColor(255, 255, 255, 190));
        painter.setPen(QColor(0x40, 0x40, 0x40));
        const int mid_y = box.y() + box_h / 2;
        int x = box.x() + pad;
        painter.drawText(QRect(x, box.y(), metrics.horizontalAdvance(zero), box_h),
                         Qt::AlignLeft | Qt::AlignVCenter, zero);
        x += metrics.horizontalAdvance(zero) + gap;
        painter.drawLine(x, mid_y, x + bar_px, mid_y); // 段线 + 两端刻度
        painter.drawLine(x, mid_y - 3, x, mid_y + 3);
        painter.drawLine(x + bar_px, mid_y - 3, x + bar_px, mid_y + 3);
        x += bar_px + gap;
        painter.drawText(QRect(x, box.y(), metrics.horizontalAdvance(label), box_h),
                         Qt::AlignLeft | Qt::AlignVCenter, label);
        painter.restore();
    }

    // ---- datum boundary (frozen: conversion only at the boundary) ----------------
    std::pair<double, double> wgs_to_datum(double lat, double lon) const {
        if (provider != nullptr && provider->gcj02) {
            return wgs84_to_gcj02(lat, lon);
        }
        return {lat, lon};
    }

    std::pair<double, double> datum_to_wgs(double lat, double lon) const {
        if (provider != nullptr && provider->gcj02) {
            return gcj02_to_wgs84(lat, lon);
        }
        return {lat, lon};
    }

    // ---- geometry ---------------------------------------------------------------
    QPointF center_world() const { return latlon_to_world(center_lat, center_lon, zoom); }

    QPointF world_to_screen(QPointF world) const {
        return world - center_world() + QPointF(q->width() / 2.0, q->height() / 2.0);
    }

    QPointF screen_to_world(QPointF screen) const {
        return screen + center_world() - QPointF(q->width() / 2.0, q->height() / 2.0);
    }

    // ---- tile cache (LRU, byte budget) ------------------------------------------
    const QPixmap *find_tile(const TileKey &key) {
        const auto it = cache.find(key);
        if (it == cache.end()) {
            return nullptr;
        }
        lru.splice(lru.begin(), lru, it->lru_pos); // touch: move to most-recently-used
        it->lru_pos = lru.begin();
        return &it->pixmap;
    }

    void evict_to_budget() {
        while (cache_bytes > cache_budget && !lru.empty()) {
            const TileKey oldest = lru.back();
            lru.pop_back();
            const auto it = cache.find(oldest);
            if (it != cache.end()) {
                cache_bytes -= it->footprint;
                cache.erase(it);
            }
        }
    }

    // R-161（M7-WP7）：返回**是否真的入缓存** —— 失败 streak 只被成功瓦片复位；坏 payload
    // 不再"当场抵消"连续失败计数（banner 阈值对 200+ 坏 body 终于可触发）。
    bool insert_tile(const TileKey &key, const QByteArray &data) {
        QPixmap pixmap;
        if (!pixmap.loadFromData(data)) {
            note_failure(); // undecodable payload = failed tile, stays grey
            return false;
        }
        // Ruling 2026-09-19: the cache keeps decoded pixmaps only (the compressed bytes are
        // dropped after decoding) and the budget is charged for the decoded footprint, so
        // set_cache_mb() really bounds process memory: 64 MB ~= 256 tiles of 256x256 ARGB32.
        const qint64 footprint = qint64(pixmap.width()) * qint64(pixmap.height()) * 4;
        const auto existing = cache.find(key);
        if (existing != cache.end()) {
            cache_bytes -= existing->footprint;
            lru.erase(existing->lru_pos);
            cache.erase(existing);
        }
        lru.push_front(key);
        CacheEntry entry;
        entry.pixmap = pixmap;
        entry.footprint = footprint;
        entry.lru_pos = lru.begin();
        cache_bytes += footprint;
        cache.insert(key, entry);
        evict_to_budget();
        return true;
    }

    // ---- fetching ---------------------------------------------------------------
    bool is_pending(const TileKey &key) const {
        for (const PendingTile &t : pending) {
            if (t.key == key) {
                return true;
            }
        }
        return false;
    }

    void request_tile(const TileKey &key) {
        if (offline || provider == nullptr || nam == nullptr) {
            return; // frozen: offline sends no request at all
        }
        if (pending.size() >= size_t(kMaxConcurrentFetches) || is_pending(key)) {
            return;
        }
        const QUrl url = tile_url_for(key);
        if (url.isEmpty()) {
            return; // 该源没有这一图层（如 esri 的标注层）：不发空 URL 请求
        }
        // UA 仍由 provider 单源给出（M6-T5 不改每 provider 的 UA 串；回退源的瓦片也带本 provider
        // 的 UA，OSM/Nominatim 使用政策要求的非空 UA 语义不变）。
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", provider->user_agent().toUtf8());
        request.setTransferTimeout(std::chrono::milliseconds(kRequestTimeoutMs));
        QNetworkReply *const reply = nam->get(request);
        pending.push_back(PendingTile{reply, key});
        notify_pending();
        QObject::connect(reply, &QNetworkReply::finished, q,
                         [this, reply] { on_tile_finished(reply); });
    }

    // R-221（M7-WP7）：paint **不再发网络请求**（paint 幂等化）。缺瓦片只登记进 fetch_wanted
    // （集合语义、可重复登记），0ms 一次性定时器在 paint 之外统一 flush（真正的 nam->get）。
    void queue_tile(const TileKey &key) {
        for (const TileKey &known : fetch_wanted) {
            if (known == key) {
                return;
            }
        }
        fetch_wanted.push_back(key);
        if (!fetch_flush_scheduled) {
            fetch_flush_scheduled = true;
            QTimer::singleShot(0, q, [this] { flush_tile_fetches(); });
        }
    }

    void flush_tile_fetches() {
        fetch_flush_scheduled = false;
        std::vector<TileKey> wanted;
        wanted.swap(fetch_wanted); // 先清后抓：抓取期间新登记的走下一轮 flush
        for (const TileKey &key : wanted) {
            request_tile(key);
        }
    }

    void on_tile_finished(QNetworkReply *reply) {
        const auto it = std::find_if(pending.begin(), pending.end(),
                                     [reply](const PendingTile &t) { return t.reply == reply; });
        if (it == pending.end()) {
            reply->deleteLater();
            return;
        }
        const PendingTile entry = *it;
        pending.erase(it);
        notify_pending();

        const QByteArray body = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        // Cancelled because the widget went offline: not a connectivity failure.
        if (error == QNetworkReply::OperationCanceledError && offline) {
            return;
        }
        if (error != QNetworkReply::NoError || body.isEmpty()) {
            note_failure();
            schedule_tile_retry(); // R-101：灰块不再"等下次任意重绘才补抓"
            return;
        }
        // R-161：坏 payload（insert_tile 失败）不复位 streak、不撤 banner；成功才复位。
        if (insert_tile(entry.key, body)) {
            consecutive_failures = 0;
            set_banner_visible(false);
        } else {
            schedule_tile_retry();
        }
        q->update();
    }

    void notify_pending() { emit q->tiles_pending_changed(int(pending.size())); }

    void note_failure() {
        ++consecutive_failures;
        if (consecutive_failures >= kFailureBannerThreshold) {
            set_banner_visible(true);
        }
    }

    // R-101（M7-WP7）：失败后**延迟重抓** —— 一次性定时器到点再 update()（paint 只排队、
    // flush 在 paint 外发请求，见 on_paint/flush_tile_fetches）；离线/重复调度不叠加。
    void schedule_tile_retry() {
        if (offline || retry_scheduled) {
            return;
        }
        retry_scheduled = true;
        QTimer::singleShot(kTileRetryDelayMs, q, [this] {
            retry_scheduled = false;
            if (!offline) {
                q->update(); // 重绘 → 缺瓦片重新入抓取队列（定点重抓）
            }
        });
    }

    // ---- offline banner (QLabel overlay, never a dialog) -------------------------
    void layout_banner() {
        if (banner == nullptr || q == nullptr) {
            return;
        }
        banner->adjustSize();
        const QSize hint = banner->sizeHint();
        banner->setGeometry(std::max(0, (q->width() - hint.width()) / 2),
                            std::max(0, q->height() - hint.height() - 6), hint.width(),
                            hint.height());
    }

    void set_banner_visible(bool on) {
        if (banner == nullptr) {
            return;
        }
        if (on) {
            layout_banner();
        }
        banner->setVisible(on);
    }

    // ---- search pipeline (M6-T7) -------------------------------------------------
    void abort_search() {
        if (search_reply != nullptr) {
            QNetworkReply *const reply = search_reply;
            search_reply = nullptr;
            reply->abort();
        }
    }

    void abort_suggest() {
        if (suggest_reply != nullptr) {
            QNetworkReply *const reply = suggest_reply;
            suggest_reply = nullptr;
            reply->abort();
        }
    }

    void abort_reverse() {
        if (reverse_reply != nullptr) {
            QNetworkReply *const reply = reverse_reply;
            reverse_reply = nullptr;
            reply->abort();
        }
    }

    void abort_geocoding() {
        abort_search();
        abort_suggest();
        abort_reverse();
    }

    // 搜索管线请求的统一构造：每 provider 一个 UA（OSM/Nominatim 政策要求非空 UA）+ 8s 超时。
    QNetworkRequest make_request(const QUrl &url) const {
        QNetworkRequest request(url);
        if (provider != nullptr) {
            request.setRawHeader("User-Agent", provider->user_agent().toUtf8());
        }
        request.setTransferTimeout(std::chrono::milliseconds(kRequestTimeoutMs));
        return request;
    }

    // R-013（M7-WP7）：错误串**单点脱敏** —— Qt 的 "Error transferring %1…" 模板把含 query 的
    // 完整 URL（含 key=）拼进 errorString，进 UI 文案/截图/自验钩子即泄密。这里剥掉 key= 的值
    // （保守兼容其它 query 诊断），入口只有 request_failure_text 一处。
    static QString redact_secrets(const QString &detail) {
        QString out;
        out.reserve(detail.size());
        const QString needle = QStringLiteral("key=");
        int pos = 0;
        while (pos < detail.size()) {
            const int hit = static_cast<int>(detail.indexOf(needle, pos, Qt::CaseInsensitive));
            if (hit < 0) {
                out += detail.mid(pos);
                break;
            }
            out += detail.mid(pos, hit - pos + needle.size()); // 保留 "key=" 本身
            int end = static_cast<int>(hit + needle.size());
            while (end < detail.size()) {
                const QChar c = detail.at(end);
                if (c == QLatin1Char('&') || c.isSpace()) {
                    break;
                }
                ++end;
            }
            out += QStringLiteral("***"); // 值一律不留
            pos = end;
        }
        return out;
    }

    // 「请求失败」分类文案：免 Key 源（Nominatim）在大陆不可达是常态，附带切源指引（§4.3/§8 R1）。
    static QString request_failure_text(const QString &what, const Geocoder *geocoder,
                                        const QString &detail) {
        const QString safe = redact_secrets(detail);
        if (geocoder != nullptr && !geocoder->requires_key) {
            return MapWidget::tr("%1请求失败：%2（该源大陆不可达，请切高德）").arg(what, safe);
        }
        return MapWidget::tr("%1请求失败：%2").arg(what, safe);
    }

    // R-077（M7-WP7）：高德源空 Key **不发请求** —— 预检落到请求路径（run_* 与本层 start_*
    // 双保险），文案直接指向设置页（"跳转"由宿主 UI 负责，见任务包 WP6）。
    static QString missing_key_text() { return MapWidget::tr("未配置高德 Key，请到设置›地图填写"); }

    bool needs_key_missing(const Geocoder *geocoder) const {
        return geocoder != nullptr && geocoder->requires_key && amap_key.isEmpty();
    }

    // 搜索请求（含周边偏置开关）：near = 视口中心（WGS-84），由 geocoder 转该源基准
    // （amap around 的 location 是 GCJ-02）。with_near=false 即全文搜（0 结果时的回退路径）。
    // 每次 run_search 恰好以一对 search_results_ready + search_finished 收尾（含失败）。
    void start_search(const QString &query, bool with_near) {
        const Geocoder *const geocoder =
            geocoder_for_provider(provider != nullptr ? provider->id : QString());
        if (geocoder == nullptr) {
            const QString text = MapWidget::tr("未知的地理编码源");
            emit q->search_results_ready({}, text);
            emit q->search_finished({}, text);
            return;
        }
        if (needs_key_missing(geocoder)) { // R-077：空 Key 不发请求（与 run_search 同一预检）
            const QString text = missing_key_text();
            emit q->search_results_ready({}, text);
            emit q->search_finished({}, text);
            return;
        }
        const std::pair<double, double> center_wgs = datum_to_wgs(center_lat, center_lon);
        const std::optional<std::pair<double, double>> near =
            with_near ? std::optional<std::pair<double, double>>(center_wgs) : std::nullopt;
        abort_search();
        QNetworkReply *const reply =
            nam->get(make_request(geocoder->search_url(query, amap_key, near)));
        search_reply = reply;
        const QString &fallback_query = query; // 周边无果时回退全文用（下游 lambda 按值捕获=拷贝）
        QObject::connect(
            reply, &QNetworkReply::finished, q, [this, reply, geocoder, with_near, fallback_query] {
                const bool current = (search_reply == reply);
                if (current) {
                    search_reply = nullptr;
                }
                // abort 过的 reply 设备已关闭：不再 readAll（否则 Qt 打 "device not open" 噪声）。
                const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
                const QNetworkReply::NetworkError error = reply->error();
                const QString error_string = reply->errorString();
                reply->deleteLater();
                // R-102（M7-WP7）：`!current`（已被新请求/离线 abort 取代）**先于** offline 判定
                // —— 否则切离线瞬间的在途请求会吐「离线模式」错误清列表、旧钉却残留。
                // 离线的用户可见文案由 run_search 的同步分支给出（冻结行为不变）。
                if (!current) {
                    return; // 已被新请求取代（abort_search/abort_geocoding）：静默丢弃
                }
                if (offline) {
                    // 冻结文案（docs/m1b-tasks.md §2.5）：离线一律报「离线模式：搜索不可用」。
                    const QString text = MapWidget::tr("离线模式：搜索不可用");
                    emit q->search_results_ready({}, text);
                    emit q->search_finished({}, text);
                    return;
                }
                if (error != QNetworkReply::NoError) {
                    const QString text =
                        request_failure_text(MapWidget::tr("搜索"), geocoder, error_string);
                    emit q->search_results_ready({}, text);
                    emit q->search_finished({}, text);
                    return;
                }
                std::vector<GeocodeResult> parsed;
                const QString parse_error = parse_search(body, geocoder->id, parsed);
                if (!parse_error.isEmpty()) {
                    emit q->search_results_ready({}, parse_error);
                    emit q->search_finished({}, parse_error);
                    return;
                }
                if (parsed.empty()) {
                    // 周边偏置消费 near 的只有 amap 的 around 端点；nominatim 忽略 near（两次同
                    // URL）， 故只在周边真生效时回退一次全文（§4.3「优先周边偏置，回退全文」）。
                    if (with_near && geocoder->id == QLatin1String("amap")) {
                        start_search(fallback_query, false);
                        return;
                    }
                    const QString text = MapWidget::tr("无匹配结果"); // 0 结果分类（§4.3）
                    emit q->search_results_ready({}, text);
                    emit q->search_finished({}, text);
                    return;
                }
                // 冻结事实（docs/m1b-tasks.md §2.5）：search_finished 顺序 == 内部 results 顺序，
                // 故 d.results 与图钉都按 parsed 原序生成，search_select(i) 直接索引同一结果。
                results.clear();
                results.reserve(parsed.size());
                std::vector<Pin> pins;
                pins.reserve(parsed.size());
                QStringList titles;
                titles.reserve(int(parsed.size()));
                for (std::size_t i = 0; i < parsed.size(); ++i) {
                    const GeocodeResult &result = parsed[i];
                    results.push_back(SearchResult{result.title, result.lat, result.lon});
                    pins.push_back(Pin{result.lat, result.lon, result.title, int(i)});
                    titles << result.title;
                }
                q->set_result_pins(pins);
                emit q->search_results_ready(parsed, QString());
                emit q->search_finished(titles, QString());
            });
    }

    // 联想请求（amap inputtips；nominatim 无此端点，由调用方先行拦截）。
    // R-012（M7-WP7）：**查询世代**守卫 —— run_suggest 每次（含空查询）递增世代并 abort 在途
    // 联想；回调世代不符即丢弃（清空/回车后旧建议不再复活可点）。
    void start_suggest(const Geocoder *geocoder, const QString &query, quint64 generation) {
        abort_suggest();
        QNetworkReply *const reply = nam->get(make_request(geocoder->suggest_url(query, amap_key)));
        suggest_reply = reply;
        QObject::connect(reply, &QNetworkReply::finished, q, [this, reply, geocoder, generation] {
            const bool current = (suggest_reply == reply);
            if (current) {
                suggest_reply = nullptr;
            }
            const QByteArray body =
                reply->isOpen() ? reply->readAll() : QByteArray(); // 见 start_search
            const QNetworkReply::NetworkError error = reply->error();
            const QString error_string = reply->errorString();
            reply->deleteLater();
            // R-102：`!current` 先于 offline（切离线瞬间在途请求静默丢弃）；
            // R-012：世代过期（清空/回车/新一轮联想）同样静默丢弃。
            if (!current || generation != suggest_generation) {
                return;
            }
            if (offline) {
                emit q->suggest_finished({}, MapWidget::tr("离线模式：联想不可用"));
                return;
            }
            if (error != QNetworkReply::NoError) {
                emit q->suggest_finished(
                    {}, request_failure_text(MapWidget::tr("联想"), geocoder, error_string));
                return;
            }
            std::vector<SuggestItem> items;
            const QString parse_error = parse_suggest(body, geocoder->id, items);
            if (!parse_error.isEmpty()) {
                emit q->suggest_finished({}, parse_error);
                return;
            }
            if (items.empty()) {
                emit q->suggest_finished({}, MapWidget::tr("无匹配结果")); // 0 结果分类（§4.3）
                return;
            }
            emit q->suggest_finished(items, QString());
        });
    }

    // 逆地理请求：坐标**一律 WGS-84**（R-219：与 search_url 的 near 同一约定 —— 源基准转换
    // 只发生在 geocoder 的 URL 构造边界内，调用方永不预转换）。
    void start_reverse(const Geocoder *geocoder, double lat, double lon) {
        abort_reverse();
        QNetworkReply *const reply =
            nam->get(make_request(geocoder->reverse_url(lat, lon, amap_key)));
        reverse_reply = reply;
        QObject::connect(reply, &QNetworkReply::finished, q, [this, reply, geocoder] {
            const bool current = (reverse_reply == reply);
            if (current) {
                reverse_reply = nullptr;
            }
            const QByteArray body =
                reply->isOpen() ? reply->readAll() : QByteArray(); // 见 start_search
            const QNetworkReply::NetworkError error = reply->error();
            const QString error_string = reply->errorString();
            reply->deleteLater();
            // R-102：`!current`（被新请求/离线 abort 取代）先于 offline 判定。
            if (!current) {
                return; // 已被新请求取代：静默丢弃
            }
            if (offline) {
                emit q->reverse_finished(QString(), MapWidget::tr("离线模式：逆地理不可用"));
                return;
            }
            if (error != QNetworkReply::NoError) {
                emit q->reverse_finished(QString(), request_failure_text(MapWidget::tr("逆地理"),
                                                                         geocoder, error_string));
                return;
            }
            QString address;
            const QString parse_error = parse_reverse(body, geocoder->id, address);
            if (!parse_error.isEmpty()) {
                emit q->reverse_finished(QString(), parse_error);
                return;
            }
            if (address.isEmpty()) {
                // 0 结果分类（§4.3）：坐标没有解析出地址（如海面/无地址区）。
                emit q->reverse_finished(QString(), MapWidget::tr("未找到该坐标的地址"));
                return;
            }
            emit q->reverse_finished(address, QString());
        });
    }

    void abort_fetches() {
        const std::vector<QNetworkReply *> in_flight = [this] {
            std::vector<QNetworkReply *> v;
            v.reserve(pending.size());
            for (const PendingTile &t : pending) {
                v.push_back(t.reply);
            }
            return v;
        }();
        for (QNetworkReply *const reply : in_flight) {
            if (reply != nullptr) {
                reply->abort(); // finished() handler removes the pending entry
            }
        }
    }

    // ---- event handling (invoked by EventFilter) ---------------------------------
    void on_paint();
    void on_resize();
    bool on_mouse_press(QMouseEvent *event);
    bool on_mouse_move(QMouseEvent *event);
    bool on_mouse_release(QMouseEvent *event);
    bool on_wheel(QWheelEvent *event);
};

bool MapWidget::Impl::EventFilter::eventFilter(QObject *watched, QEvent *event) {
    if (impl == nullptr || watched != impl->q || event == nullptr) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::Paint:
        impl->on_paint();
        return false; // drawing is done; the default (empty) QWidget handler may still run
    case QEvent::Resize:
        impl->on_resize();
        return false;
    case QEvent::MouseButtonPress:
        return impl->on_mouse_press(static_cast<QMouseEvent *>(event));
    case QEvent::MouseMove:
        return impl->on_mouse_move(static_cast<QMouseEvent *>(event));
    case QEvent::MouseButtonRelease:
        return impl->on_mouse_release(static_cast<QMouseEvent *>(event));
    case QEvent::Wheel:
        return impl->on_wheel(static_cast<QWheelEvent *>(event));
    default:
        return QObject::eventFilter(watched, event);
    }
}

void MapWidget::Impl::on_resize() { layout_banner(); }

void MapWidget::Impl::on_paint() {
    const int w = q->width();
    const int h = q->height();
    if (w <= 0 || h <= 0) {
        return;
    }
    QPainter painter(q);
    painter.fillRect(q->rect(), QColor(0xe9, 0xe9, 0xe9));

    const QPointF center = center_world();
    const double left = center.x() - w / 2.0;
    const double top = center.y() - h / 2.0;

    // Thin grid on the tile lattice marks the not-yet-loaded area.
    painter.setPen(QPen(QColor(0xd2, 0xd2, 0xd2), 1));
    for (double wx = std::floor(left / kTileSize) * kTileSize; wx - left <= w; wx += kTileSize) {
        const int sx = int(std::lround(wx - left));
        painter.drawLine(sx, 0, sx, h);
    }
    for (double wy = std::floor(top / kTileSize) * kTileSize; wy - top <= h; wy += kTileSize) {
        const int sy = int(std::lround(wy - top));
        painter.drawLine(0, sy, w, sy);
    }

    if (provider != nullptr) {
        const int tile_count = int(std::int64_t(1) << zoom);
        const int x0 = int(std::floor(left / kTileSize));
        const int x1 = int(std::floor((left + w - 1) / kTileSize));
        const int y0 = int(std::floor(top / kTileSize));
        const int y1 = int(std::floor((top + h - 1) / kTileSize));
        // M6-T5：逐层绘制（Hybrid 的影像层在前 = 在下，标注层在后 = 叠在上面）；每层各有自己的
        // 缓存条目（key 含 layer）。缺瓦片仍留灰底网格，抓取仍受 ≤4 并发 / 8s 超时 / 离线不发约束。
        for (const PaintLayer &layer : paint_layers()) {
            for (int ty = y0; ty <= y1; ++ty) {
                if (ty < 0 || ty >= tile_count) {
                    continue; // no vertical wrap on a slippy map
                }
                for (int tx = x0; tx <= x1; ++tx) {
                    const TileKey key{layer.source->id, layer.layer, zoom,
                                      wrap_tile_x(tx, tile_count), ty};
                    const int sx = int(std::lround(tx * double(kTileSize) - left));
                    const int sy = int(std::lround(ty * double(kTileSize) - top));
                    if (const QPixmap *const tile = find_tile(key)) {
                        painter.drawPixmap(sx, sy, kTileSize, kTileSize, *tile);
                    } else {
                        queue_tile(key); // R-221：paint 只登记；flush 在 paint 外发请求
                                         // （no-op when offline / already pending / 4 in flight）
                    }
                }
            }
        }
    }

    // M6-T6 结果图钉（§4.4）：实心圆 + 编号气泡，坐标管线和主钉一致（WGS-84 → datum → world
    // → screen）。画在瓦片之上、主 marker 之下 —— 主钉最后画，恒在钉堆顶层。
    if (!result_pins.empty()) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont pin_font = painter.font();
        pin_font.setPointSize(kPinBubblePointSize);
        painter.setFont(pin_font);
        const QFontMetrics pin_metrics(pin_font);
        const QColor accent(0x2b, 0x6c, 0xb0); // 中性强调色
        for (const Pin &pin : result_pins) {
            const std::pair<double, double> datum = wgs_to_datum(pin.lat, pin.lon);
            const QPointF pos = world_to_screen(latlon_to_world(datum.first, datum.second, zoom));
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawEllipse(pos, double(kPinRadius), double(kPinRadius));
            const QString full = pin.label.isEmpty() ? QString::number(pin.index) : pin.label;
            const QString text = pin_metrics.elidedText(full, Qt::ElideRight, kPinBubbleTextMax);
            const QRectF bubble(pos.x() + kPinRadius + 1,
                                pos.y() - kPinRadius - pin_metrics.height() - 2,
                                pin_metrics.horizontalAdvance(text) + 8, pin_metrics.height() + 2);
            painter.setPen(QPen(accent, 1));
            painter.setBrush(QColor(255, 255, 255, 230)); // 白底：卫星影像上仍可读
            painter.drawRoundedRect(bubble, 3, 3);
            painter.setPen(accent);
            painter.drawText(bubble, Qt::AlignCenter, text);
        }
        painter.restore();
    }

    // Marker last (WGS-84 -> datum -> world -> screen).
    if (marker_set) {
        const std::pair<double, double> datum = wgs_to_datum(marker_lat, marker_lon);
        const QPointF pos = world_to_screen(latlon_to_world(datum.first, datum.second, zoom));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(0xd0, 0x22, 0x22), 2));
        painter.drawLine(QPointF(pos.x() - kMarkerRadius - kMarkerArm, pos.y()),
                         QPointF(pos.x() + kMarkerRadius + kMarkerArm, pos.y()));
        painter.drawLine(QPointF(pos.x(), pos.y() - kMarkerRadius - kMarkerArm),
                         QPointF(pos.x(), pos.y() + kMarkerRadius + kMarkerArm));
        painter.drawEllipse(pos, double(kMarkerRadius), double(kMarkerRadius));
        painter.setRenderHint(QPainter::Antialiasing, false);
    }

    // M6-T12：左下比例尺（§5.3）—— 与右下版权行同在瓦片/图钉/主钉之上，互为左右角标。
    draw_scale_bar(painter, h);

    // Bottom-right credit label —— 文案单源 = 有效源的 attribution（D1：唯一定义处在 layers.cpp；
    // 卫星/混合回退到 esri 影像时报 esri 版权）。§4.1：卫星底图偏暗，白底 α190 保留可读性。
    const TileSource *const credit_source = select_source(tile_style_);
    const QString credit = credit_source != nullptr ? credit_source->attribution : QString();
    if (!credit.isEmpty()) {
        QFont font = painter.font();
        font.setPointSize(kHudPointSize);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const QRect box(w - metrics.horizontalAdvance(credit) - 8, h - metrics.height() - 6,
                        metrics.horizontalAdvance(credit) + 6, metrics.height() + 2);
        painter.fillRect(box, QColor(255, 255, 255, 190));
        painter.setPen(QColor(0x40, 0x40, 0x40));
        painter.drawText(box, Qt::AlignCenter, credit);
    }
}

bool MapWidget::Impl::on_mouse_press(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    pressed = true;
    dragging = false;
    press_pos = event->position().toPoint();
    last_drag_pos = press_pos;
    return true;
}

bool MapWidget::Impl::on_mouse_move(QMouseEvent *event) {
    // M6-T12：光标坐标回显（悬停与拖拽都算"移动"，约 100 ms 节流；不动拖拽/点击语义）。
    emit_cursor_readout(event->position());
    if (!pressed || !(event->buttons() & Qt::LeftButton)) {
        return false;
    }
    const QPoint pos = event->position().toPoint();
    if (!dragging && QLineF(QPointF(press_pos), QPointF(pos)).length() >= kClickThresholdPx) {
        dragging = true;
        // First pan step: apply the whole press->cursor delta so nothing is swallowed by the
        // 4 px click threshold.
        last_drag_pos = press_pos;
    }
    if (dragging) {
        const QPoint delta = pos - last_drag_pos;
        last_drag_pos = pos;
        if (!delta.isNull()) {
            // The map content follows the cursor: the centre moves by -delta in world pixels.
            const std::pair<double, double> next =
                world_to_latlon(center_world() - QPointF(delta.x(), delta.y()), zoom);
            center_lat = clamp_lat(next.first);
            center_lon = next.second;
            q->update();
        }
    }
    return true;
}

bool MapWidget::Impl::on_mouse_release(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !pressed) {
        return false;
    }
    const QPoint pos = event->position().toPoint();
    const double moved = QLineF(QPointF(press_pos), QPointF(pos)).length();
    const bool was_drag = dragging;
    pressed = false;
    dragging = false;
    if (!was_drag && moved < kClickThresholdPx) {
        // M6-T6 命中优先于取点（§4.4）：对最近结果钉做屏距判定 —— 光标与钉心 ≤
        // kMarkerRadius + 6 px 即命中（钉位与绘制同一管线，所见即所点）。并列时先者优先。
        const Pin *hit = nullptr;
        double best_dist = 0.0;
        for (const Pin &pin : result_pins) {
            const std::pair<double, double> datum = wgs_to_datum(pin.lat, pin.lon);
            const QPointF pin_pos =
                world_to_screen(latlon_to_world(datum.first, datum.second, zoom));
            const double dist = QLineF(pin_pos, QPointF(pos)).length();
            if (dist > kPinHitRadius) {
                continue;
            }
            if (hit == nullptr || dist < best_dist) {
                hit = &pin;
                best_dist = dist;
            }
        }
        if (hit != nullptr) {
            const Pin picked = *hit;               // 命中钉副本（set_marker 不改 result_pins）
            q->set_marker(picked.lat, picked.lon); // 主钉落到该结果点（含重绘）
            emit q->pin_clicked(picked.index);     // 回报结果索引；点钉不算「取点」（冻结行为）
            // R-223：另报稳定键（title+坐标）—— 消费方按键对齐列表行，不再依赖序号同代。
            emit q->pin_selected(result_key(picked.label, picked.lat, picked.lon));
            return true;
        }
        const std::pair<double, double> datum =
            world_to_latlon(screen_to_world(QPointF(pos)), zoom);
        const std::pair<double, double> wgs = datum_to_wgs(datum.first, datum.second);
        marker_set = true;
        marker_lat = wgs.first;
        marker_lon = wgs.second;
        q->update();
        emit q->point_selected(wgs.first, wgs.second);
    }
    return true;
}

bool MapWidget::Impl::on_wheel(QWheelEvent *event) {
    const int dy = event->angleDelta().y();
    if (dy == 0) {
        return false;
    }
    const int target = std::clamp(zoom + (dy > 0 ? 1 : -1), kMinZoom, kMaxZoom);
    if (target != zoom) {
        // Keep the geographic point under the cursor pinned while the zoom level changes.
        const QPointF cursor = event->position();
        const std::pair<double, double> anchor = world_to_latlon(screen_to_world(cursor), zoom);
        zoom = target;
        const QPointF anchor_world = latlon_to_world(anchor.first, anchor.second, zoom);
        const QPointF center =
            anchor_world - (cursor - QPointF(q->width() / 2.0, q->height() / 2.0));
        const std::pair<double, double> next = world_to_latlon(center, zoom);
        center_lat = clamp_lat(next.first);
        center_lon = next.second;
        q->update();
    }
    return true;
}

MapWidget::MapWidget(QWidget *parent) : QWidget(parent), impl_(std::make_unique<Impl>()) {
    Impl &d = *impl_;
    d.q = this;
    d.provider = provider_by_id(QStringLiteral("osm"));
    d.nam = new QNetworkAccessManager(this);

    // M5-T12/A30（§8 状态色语义）：离线 banner **中性化** —— 文案去括号（说明进 tooltip，§2.6
    // 「实现细节/补充说明进 tooltip」）、底色/字色去**警示黄**（琥珀语义只留 0 字节那类真警告）。
    // 中性底色/字色 = 系统调色板默认（本件不持令牌，宿主页以自身令牌 QSS 接管同一标签 ——
    // 令牌单源在宿主侧）；此文件不再自持任何硬编码语义色（A6 口径）。
    d.banner = new QLabel(tr("地图离线"), this);
    d.banner->setAlignment(Qt::AlignCenter);
    d.banner->setFrameShape(QFrame::Box);
    d.banner->setMargin(4);
    d.banner->setToolTip(tr("可手动输入坐标"));
    d.banner->setAccessibleName(tr("地图离线")); // §11：状态说明可读（tooltip 同文案的主干）
    d.banner->setAutoFillBackground(true);
    d.banner->setVisible(false);

    d.filter = new Impl::EventFilter(&d);
    installEventFilter(d.filter);
    // M6-T12：悬停即回显光标坐标（cursor_moved）—— 冻结的滚轮/拖拽/点击三件套不受影响。
    setMouseTracking(true);

    setMinimumSize(minimumSizeHint());
}

MapWidget::~MapWidget() {
    Impl &d = *impl_;
    for (const Impl::PendingTile &t : d.pending) {
        if (t.reply != nullptr) {
            t.reply->disconnect(); // never re-enter the handler while tearing down
            t.reply->abort();
            t.reply->deleteLater();
        }
    }
    d.pending.clear();
    // 在途的搜索/联想/逆地理请求：先断开 finished（销毁期不再重入处理器）再 abort。
    for (QNetworkReply *const reply : {d.search_reply, d.suggest_reply, d.reverse_reply}) {
        if (reply != nullptr) {
            reply->disconnect();
            reply->abort();
            reply->deleteLater();
        }
    }
    d.search_reply = nullptr;
    d.suggest_reply = nullptr;
    d.reverse_reply = nullptr;
}

void MapWidget::set_provider(const QString &id) {
    Impl &d = *impl_;
    const TileProvider *const next = provider_by_id(id);
    if (next == nullptr || next == d.provider) {
        return; // unknown id: keep the current provider (no invented fallback)
    }
    // 换 provider 才换基准面（amap GCJ-02 ↔ osm WGS-84），故这里的往返转换是必须的；
    // 图层切换/回退（M6-T5 select_source）不动基准面，见 set_tile_style 的「同基准，无漂移」。
    const std::pair<double, double> wgs = d.datum_to_wgs(d.center_lat, d.center_lon);
    d.provider = next;
    const std::pair<double, double> datum = d.wgs_to_datum(wgs.first, wgs.second);
    d.center_lat = clamp_lat(datum.first);
    d.center_lon = datum.second;
    update();
}

void MapWidget::set_amap_key(const QString &key) { impl_->amap_key = key; }

void MapWidget::set_cache_mb(int mb) {
    Impl &d = *impl_;
    d.cache_budget = qint64(std::max(0, mb)) * kBytesPerMb;
    d.evict_to_budget();
    update();
}

void MapWidget::set_offline(bool off) {
    Impl &d = *impl_;
    if (d.offline == off) {
        return;
    }
    d.offline = off;
    if (off) {
        d.abort_geocoding(); // 搜索/联想/逆地理一并在途取消（离线不发请求）
        d.abort_fetches();
        d.set_banner_visible(true); // offline degradation notice (design §6.5 / walkthrough §8.3)
    } else {
        d.consecutive_failures = 0;
        d.set_banner_visible(false);
    }
    update();
}

void MapWidget::set_marker(double lat, double lon) {
    Impl &d = *impl_;
    d.marker_set = true;
    d.marker_lat = lat;
    d.marker_lon = lon;
    update();
}

void MapWidget::clear_marker() {
    Impl &d = *impl_;
    d.marker_set = false;
    update();
}

bool MapWidget::has_marker() const { return impl_->marker_set; }

double MapWidget::marker_lat() const { return impl_->marker_lat; }

double MapWidget::marker_lon() const { return impl_->marker_lon; }

// M4-T12：视口中心回读（内部基准面 → WGS-84，与 center_on/set_provider 同一转换边界）
double MapWidget::center_lat() const {
    return impl_->datum_to_wgs(impl_->center_lat, impl_->center_lon).first;
}

double MapWidget::center_lon() const {
    return impl_->datum_to_wgs(impl_->center_lat, impl_->center_lon).second;
}

void MapWidget::center_on(double lat, double lon, int zoom) {
    Impl &d = *impl_;
    const std::pair<double, double> datum = d.wgs_to_datum(lat, lon);
    d.center_lat = clamp_lat(datum.first);
    d.center_lon = datum.second;
    if (zoom >= 0) {
        d.zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    }
    update();
}

void MapWidget::run_search(const QString &query) {
    Impl &d = *impl_;
    // R-012：搜索接管输入面 —— 在途联想一并清算（世代推进 → 迟到的联想回调永久丢弃，
    // 陈旧建议不再"复活"可点）。
    ++d.suggest_generation;
    d.abort_suggest();
    d.results.clear();
    clear_result_pins(); // 新搜索从零开始：旧查询的结果钉不残留在新结果上
    if (d.offline) {
        // 冻结行为（docs/m1b-tasks.md §2.5）：离线同步回「离线模式：搜索不可用」，不发请求。
        const QString text = tr("离线模式：搜索不可用");
        emit search_results_ready({}, text);
        emit search_finished({}, text);
        return;
    }
    const Geocoder *const geocoder =
        geocoder_for_provider(d.provider != nullptr ? d.provider->id : QString());
    if (geocoder == nullptr) {
        const QString text = tr("未知的地理编码源");
        emit search_results_ready({}, text);
        emit search_finished({}, text);
        return;
    }
    if (d.needs_key_missing(geocoder)) {
        // R-077：空 Key 不发请求，文案指向设置›地图（跳转由宿主 UI 承担）。
        const QString text = Impl::missing_key_text();
        emit search_results_ready({}, text);
        emit search_finished({}, text);
        return;
    }
    // 行为升级（M6-T7，已追认）：走 Geocoder 的 20 条/zh 事实表 + 视口中心周边偏置。
    d.start_search(query, true);
}

// M6-T7 加性：联想（amap inputtips）。
void MapWidget::run_suggest(const QString &query) {
    Impl &d = *impl_;
    // R-012：**空查询也清算** —— 世代推进 + abort 在途联想（旧行为只清列表不 abort，
    // 迟到响应会把陈旧建议弹回并可点进写坐标闭环）。
    ++d.suggest_generation;
    d.abort_suggest();
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        emit suggest_finished({}, QString()); // 空输入：无请求也无错误（联想是增量面）
        return;
    }
    if (d.offline) {
        emit suggest_finished({}, tr("离线模式：联想不可用"));
        return;
    }
    const Geocoder *const geocoder =
        geocoder_for_provider(d.provider != nullptr ? d.provider->id : QString());
    if (geocoder == nullptr) {
        emit suggest_finished({}, tr("未知的地理编码源"));
        return;
    }
    if (d.needs_key_missing(geocoder)) {
        emit suggest_finished({}, Impl::missing_key_text()); // R-077：空 Key 不发请求
        return;
    }
    if (geocoder->suggest_url(trimmed, d.amap_key).isEmpty()) {
        emit suggest_finished({}, tr("该源不支持联想搜索：%1").arg(geocoder->id));
        return;
    }
    d.start_suggest(geocoder, trimmed, d.suggest_generation);
}

// M6-T7 加性：逆地理（选点后回显地址）。公共坐标一律 WGS-84，请求前转该源基准。
void MapWidget::run_reverse(double lat, double lon) {
    Impl &d = *impl_;
    if (d.offline) {
        emit reverse_finished(QString(), tr("离线模式：逆地理不可用"));
        return;
    }
    const Geocoder *const geocoder =
        geocoder_for_provider(d.provider != nullptr ? d.provider->id : QString());
    if (geocoder == nullptr) {
        emit reverse_finished(QString(), tr("未知的地理编码源"));
        return;
    }
    if (d.needs_key_missing(geocoder)) {
        emit reverse_finished(QString(), Impl::missing_key_text()); // R-077：空 Key 不发请求
        return;
    }
    // R-219：入参 WGS-84 原样交给 geocoder（amap 的 GCJ-02 线上格式在其 URL 边界内转换，
    // 与 search_url 的 near 同一约定；调用方不再预转换）。
    d.start_reverse(geocoder, lat, lon);
}

void MapWidget::search_select(int index) {
    Impl &d = *impl_;
    if (index < 0 || index >= int(d.results.size())) {
        return;
    }
    const SearchResult result = d.results[size_t(index)]; // WGS-84
    set_marker(result.lat, result.lon);
    // Ruling 2026-09-19: locating a search hit at country-level zoom carries no information,
    // so raise the zoom to at least kSearchZoomFloor (never lower an already closer view).
    center_on(result.lat, result.lon, std::max(d.zoom, kSearchZoomFloor));
    emit point_selected(result.lat, result.lon);
}

// R-223（M7-WP7 加性）：按稳定键选结果（与 search_select(index) 同语义）；键未命中 = 不动作。
void MapWidget::search_select_key(const QString &key) {
    Impl &d = *impl_;
    for (const SearchResult &result : d.results) {
        if (result_key(result.title, result.lat, result.lon) == key) {
            set_marker(result.lat, result.lon);
            center_on(result.lat, result.lon, std::max(d.zoom, kSearchZoomFloor));
            emit point_selected(result.lat, result.lon);
            return;
        }
    }
}

// ---- M6-T4 加性状态存取：图层切换 + 结果图钉（图层绘制见 M6-T5，图钉绘制与命中见 M6-T6） ----

void MapWidget::set_tile_style(TileStyle style) {
    // 基准不漂（契约 C3）：只换绘制图层与缓存 key 的 layer 维度——三图层同基准（amap 全 GCJ-02；
    // osm/esri 全 WGS-84），select_source 的回退只在 osm→esri（皆 WGS-84）之间发生，故此处
    // **不做**任何 datum 转换，center_lat/center_lon 逐位不变（只 update() 重绘）。
    impl_->tile_style_ = style;
    update();
}

TileStyle MapWidget::tile_style() const { return impl_->tile_style_; }

void MapWidget::set_result_pins(const std::vector<Pin> &pins) {
    impl_->result_pins = pins;
    update();
}

void MapWidget::clear_result_pins() {
    impl_->result_pins.clear();
    update();
}

// ---- M6-T12 加性（追认见 docs/m6-adjudications.md A3）--------------------------

// Hybrid 的「叠标注」开关（§5.2/§7-T12）：false = 只画影像层（paint_layers 里 label 层的唯一
// 开关；非 Hybrid 图层不受影响）。默认 true = 现行为，故不设此开关时绘制逐字节不变。
void MapWidget::set_sat_labels(bool on) {
    impl_->sat_labels_ = on;
    update();
}

// 缩放 ±1（§5.3）：与滚轮缩放同一内部字段（zoom），锚点 = 视口中心 → 中心不动。
void MapWidget::zoom_in() { impl_->zoom_step(+1); }

void MapWidget::zoom_out() { impl_->zoom_step(-1); }

} // namespace pp::map
