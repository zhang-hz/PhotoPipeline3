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
// Events: the PP-FROZEN header declares no paint/mouse/wheel overrides (and it may not be
// edited), so an event filter owned by the pimpl receives them for the widget — the only
// conforming way to hook QWidget events without touching the frozen class declaration.

#include "mapwidget/mapwidget.h"

#include "mapwidget/layers.h"
#include "mapwidget/providers.h"

#include <QByteArray>
#include <QColor>
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
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <list>
#include <memory>
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
constexpr int kCopyrightPointSize = 6;  // frozen: 6 pt credit label
constexpr int kSearchZoomFloor = 12;    // ruling 2026-09-19: a search hit zooms to >= 12
constexpr int kMarkerRadius = 7;
constexpr int kMarkerArm = 5;

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
    return {std::atan(std::sinh(n)) * 180.0 / kPi, world.x() / span * 360.0 - 180.0};
}

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
    QLabel *banner = nullptr;
    EventFilter *filter = nullptr;

    QPoint press_pos;
    QPoint last_drag_pos;
    bool pressed = false;
    bool dragging = false;
    int consecutive_failures = 0;

    std::vector<PendingTile> pending;
    std::list<TileKey> lru; // front = most recently used
    QHash<TileKey, CacheEntry> cache;
    qint64 cache_bytes = 0;
    qint64 cache_budget = qint64(kDefaultCacheMb) * kBytesPerMb;

    std::vector<SearchResult> results;

    // M6-T4 加性状态：图层 + 结果图钉（图层绘制见 M6-T5 select_source/paint_layers；命中在 T6）。
    TileStyle tile_style_ = TileStyle::Road;
    std::vector<Pin> result_pins;

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
        if (tile_style_ == TileStyle::Hybrid) {
            stack.push_back(PaintLayer{source, QString::fromLatin1(kLayerLabel)});
        }
        return stack;
    }

    // 版权行文案随**有效源**（M6-T5）：卫星/混合回退到 esri 影像时也报对应源版权；
    // 未知源沿用旧行为（OpenStreetMap 版权）。
    QString credit_text(const TileSource *source) const {
        const QString id =
            source != nullptr ? source->id : (provider != nullptr ? provider->id : QString());
        if (id == QLatin1String("amap")) {
            return MapWidget::tr("© 高德地图");
        }
        if (id == QLatin1String("esri")) {
            return MapWidget::tr("© Esri, Maxar, Earthstar Geographics");
        }
        return MapWidget::tr("© OpenStreetMap contributors");
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

    void insert_tile(const TileKey &key, const QByteArray &data) {
        QPixmap pixmap;
        if (!pixmap.loadFromData(data)) {
            note_failure(); // undecodable payload = failed tile, stays grey
            return;
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
            return;
        }
        insert_tile(entry.key, body);
        consecutive_failures = 0;
        set_banner_visible(false);
        q->update();
    }

    void notify_pending() { emit q->tiles_pending_changed(int(pending.size())); }

    void note_failure() {
        ++consecutive_failures;
        if (consecutive_failures >= kFailureBannerThreshold) {
            set_banner_visible(true);
        }
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

    // ---- search -----------------------------------------------------------------
    void abort_search() {
        if (search_reply != nullptr) {
            QNetworkReply *const reply = search_reply;
            search_reply = nullptr;
            reply->abort();
        }
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
                        request_tile(key); // no-op when offline / already pending / 4 in flight
                    }
                }
            }
        }
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

    // Bottom-right credit label —— 随**有效源**：卫星/混合回退到 esri 影像时报 esri 版权（§4.1：
    // 卫星底图偏暗，白底 α190 保留可读性）。
    const QString credit = credit_text(select_source(tile_style_));
    QFont font = painter.font();
    font.setPointSize(kCopyrightPointSize);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const QRect box(w - metrics.horizontalAdvance(credit) - 8, h - metrics.height() - 6,
                    metrics.horizontalAdvance(credit) + 6, metrics.height() + 2);
    painter.fillRect(box, QColor(255, 255, 255, 190));
    painter.setPen(QColor(0x40, 0x40, 0x40));
    painter.drawText(box, Qt::AlignCenter, credit);
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
    if (d.search_reply != nullptr) {
        d.search_reply->disconnect();
        d.search_reply->abort();
        d.search_reply->deleteLater();
        d.search_reply = nullptr;
    }
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
        d.abort_search();
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
    d.results.clear();
    if (d.offline) {
        emit search_finished({}, tr("离线模式：搜索不可用"));
        return;
    }
    if (d.provider != nullptr && d.provider->id == QLatin1String("amap") && d.amap_key.isEmpty()) {
        emit search_finished({}, tr("使用高德搜索需要先在设置中填写 Key"));
        return;
    }
    const QString provider_id = d.provider->id;
    const QString user_agent = d.provider->user_agent();
    QNetworkRequest request(d.provider->search_url(query, d.amap_key));
    request.setRawHeader("User-Agent", user_agent.toUtf8());
    request.setTransferTimeout(std::chrono::milliseconds(kRequestTimeoutMs));

    d.abort_search();
    QNetworkReply *const reply = d.nam->get(request);
    d.search_reply = reply;
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, provider_id] {
        Impl &dd = *impl_;
        const QByteArray body = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const QString error_string = reply->errorString();
        reply->deleteLater();
        if (dd.search_reply == reply) {
            dd.search_reply = nullptr;
        }
        if (dd.offline) {
            emit search_finished({}, tr("离线模式：搜索不可用"));
            return;
        }
        if (error != QNetworkReply::NoError) {
            emit search_finished({}, tr("搜索请求失败：%1").arg(error_string));
            return;
        }
        std::vector<SearchResult> parsed;
        const QString parse_error = parse_search(provider_id, body, parsed);
        if (!parse_error.isEmpty()) {
            emit search_finished({}, parse_error);
            return;
        }
        dd.results = parsed;
        QStringList titles;
        titles.reserve(int(parsed.size()));
        for (const SearchResult &result : parsed) {
            titles << result.title;
        }
        emit search_finished(titles, QString());
    });
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

// ---- M6-T4 加性状态存取：图层切换 + 结果图钉（图层绘制见 M6-T5；命中在 T6） ----------

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

} // namespace pp::map
