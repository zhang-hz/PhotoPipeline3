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
// Events: the PP-FROZEN header declares no paint/mouse/wheel overrides (and it may not be
// edited), so an event filter owned by the pimpl receives them for the widget — the only
// conforming way to hook QWidget events without touching the frozen class declaration.

#include "mapwidget/mapwidget.h"

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
#include <QPalette>
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
struct TileKey {
    QString provider;
    int zoom = 0;
    int x = 0;
    int y = 0;
};

inline bool operator==(const TileKey &a, const TileKey &b) {
    return a.zoom == b.zoom && a.x == b.x && a.y == b.y && a.provider == b.provider;
}

inline size_t qHash(const TileKey &k, size_t seed = 0) {
    size_t h = qHash(k.provider, seed);
    h = h * 1000003u + size_t(k.zoom);
    h = h * 1000003u + size_t(k.x);
    h = h * 1000003u + size_t(k.y);
    return h;
}

struct MapWidget::Impl {
    struct PendingTile {
        QNetworkReply *reply = nullptr;
        QString provider;
        int zoom = 0;
        int x = 0;
        int y = 0;
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
            if (t.zoom == key.zoom && t.x == key.x && t.y == key.y && t.provider == key.provider) {
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
        const TileProvider *const owner = provider_by_id(key.provider);
        if (owner == nullptr) {
            return;
        }
        QNetworkRequest request(owner->tile_url(key.zoom, key.x, key.y));
        request.setRawHeader("User-Agent", owner->user_agent().toUtf8());
        request.setTransferTimeout(std::chrono::milliseconds(kRequestTimeoutMs));
        QNetworkReply *const reply = nam->get(request);
        pending.push_back(PendingTile{reply, key.provider, key.zoom, key.x, key.y});
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
        insert_tile(TileKey{entry.provider, entry.zoom, entry.x, entry.y}, body);
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
        for (int ty = y0; ty <= y1; ++ty) {
            if (ty < 0 || ty >= tile_count) {
                continue; // no vertical wrap on a slippy map
            }
            for (int tx = x0; tx <= x1; ++tx) {
                const TileKey key{provider->id, zoom, wrap_tile_x(tx, tile_count), ty};
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

    // Bottom-right credit label.
    const QString credit = (provider != nullptr && provider->id == QLatin1String("amap"))
                               ? MapWidget::tr("© 高德地图")
                               : MapWidget::tr("© OpenStreetMap contributors");
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

    d.banner = new QLabel(tr("地图离线（可手动输入坐标）"), this);
    d.banner->setAlignment(Qt::AlignCenter);
    d.banner->setFrameShape(QFrame::Box);
    d.banner->setMargin(4);
    QPalette banner_palette = d.banner->palette();
    banner_palette.setColor(QPalette::Window, QColor(0xff, 0xf1, 0xb8));
    banner_palette.setColor(QPalette::WindowText, QColor(0x6b, 0x4a, 0x00));
    d.banner->setPalette(banner_palette);
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

} // namespace pp::map
