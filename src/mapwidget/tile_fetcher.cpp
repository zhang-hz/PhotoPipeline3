// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 瓦片抓取器（B.5 拆分步③；契约见 tile_fetcher.h）
//
// 实现 = mapwidget.cpp 的 request_tile/queue_tile/flush_tile_fetches/on_tile_finished/
// note_failure/schedule_tile_retry/abort_fetches/tile_url_for 逐段迁出
// （M7-R2.2-G2；门闸/失败/重抓语义逐位一致）。
#include "mapwidget/tile_fetcher.h"

#include "mapwidget/layers.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

namespace pp::map {

namespace {

constexpr int kMaxConcurrentFetches = 4;   // frozen: concurrency <= 4
constexpr int kFailureBannerThreshold = 3; // frozen: >= 3 consecutive failures -> banner
constexpr int kRequestTimeoutMs = 8000;    // frozen: request timeout 8 s
// R-101（M7-WP7）：瓦片失败后的**延迟重抓**窗（一次性定时器；离线不排）。
constexpr int kTileRetryDelayMs = 750;

// 图层键 → URL 的唯一出口（缓存命中判定与抓取共用同一个 key）。「road」走 Road 路网，
// 「satellite」走影像层（Hybrid 的底层同为影像），「label」走 Hybrid 标注叠加层；
// 源不支持该层（osm 无影像层、esri 无标注层）时返回空 URL —— 调用方据此不发请求。
QUrl tile_url_for(const TileKey &key) {
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

} // namespace

TileFetcher::TileFetcher(QObject *context, QNetworkAccessManager *nam, Callbacks cb)
    : context_(context), nam_(nam), cb_(std::move(cb)) {}

int TileFetcher::pending_count() const { return int(pending_.size()); }

void TileFetcher::reset_failures() { consecutive_failures_ = 0; }

bool TileFetcher::is_pending(const TileKey &key) const {
    for (const PendingTile &t : pending_) {
        if (t.key == key) {
            return true;
        }
    }
    return false;
}

void TileFetcher::request_tile(const TileKey &key) {
    if (offline_ || provider_ == nullptr || nam_ == nullptr) {
        return; // frozen: offline sends no request at all
    }
    if (pending_.size() >= size_t(kMaxConcurrentFetches) || is_pending(key)) {
        return;
    }
    const QUrl url = tile_url_for(key);
    if (url.isEmpty()) {
        return; // 该源没有这一图层（如 esri 的标注层）：不发空 URL 请求
    }
    // UA 仍由 provider 单源给出（M6-T5 不改每 provider 的 UA 串；回退源的瓦片也带本 provider
    // 的 UA，OSM/Nominatim 使用政策要求的非空 UA 语义不变）。
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", provider_->user_agent().toUtf8());
    request.setTransferTimeout(std::chrono::milliseconds(kRequestTimeoutMs));
    QNetworkReply *const reply = nam_->get(request);
    pending_.push_back(PendingTile{reply, key});
    notify_pending();
    QObject::connect(reply, &QNetworkReply::finished, context_,
                     [this, reply] { on_tile_finished(reply); });
}

void TileFetcher::queue(const TileKey &key) {
    for (const TileKey &known : fetch_wanted_) {
        if (known == key) {
            return;
        }
    }
    fetch_wanted_.push_back(key);
    if (!fetch_flush_scheduled_) {
        fetch_flush_scheduled_ = true;
        QTimer::singleShot(0, context_, [this] { flush(); });
    }
}

void TileFetcher::flush() {
    fetch_flush_scheduled_ = false;
    std::vector<TileKey> wanted;
    wanted.swap(fetch_wanted_); // 先清后抓：抓取期间新登记的走下一轮 flush
    for (const TileKey &key : wanted) {
        request_tile(key);
    }
}

void TileFetcher::on_tile_finished(QNetworkReply *reply) {
    const auto it = std::find_if(pending_.begin(), pending_.end(),
                                 [reply](const PendingTile &t) { return t.reply == reply; });
    if (it == pending_.end()) {
        reply->deleteLater();
        return;
    }
    const PendingTile entry = std::move(*it); // 随后 erase 同元素：搬空再析构，与拷贝同可观测
    pending_.erase(it);
    notify_pending();

    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    reply->deleteLater();

    // Cancelled because the widget went offline: not a connectivity failure.
    if (error == QNetworkReply::OperationCanceledError && offline_) {
        return;
    }
    if (error != QNetworkReply::NoError || body.isEmpty()) {
        note_failure();
        schedule_tile_retry(); // R-101：灰块不再"等下次任意重绘才补抓"
        return;
    }
    // R-161：坏 payload（sink 解码失败）不复位 streak、不撤 banner；成功才复位。
    // 解码失败与网络失败同迹（note_failure + 延迟重抓）——与原 insert_tile 内 note_failure
    // + on_tile_finished else 分支的合并语义逐位一致。
    if (cb_.sink && cb_.sink(entry.key, body)) {
        consecutive_failures_ = 0;
        if (cb_.banner) {
            cb_.banner(false);
        }
    } else {
        note_failure();
        schedule_tile_retry();
    }
    if (cb_.repaint) {
        cb_.repaint();
    }
}

void TileFetcher::notify_pending() {
    if (cb_.pending_changed) {
        cb_.pending_changed(int(pending_.size()));
    }
}

void TileFetcher::note_failure() {
    ++consecutive_failures_;
    if (consecutive_failures_ >= kFailureBannerThreshold && cb_.banner) {
        cb_.banner(true);
    }
}

void TileFetcher::schedule_tile_retry() {
    if (offline_ || retry_scheduled_) {
        return;
    }
    retry_scheduled_ = true;
    QTimer::singleShot(kTileRetryDelayMs, context_, [this] {
        retry_scheduled_ = false;
        if (!offline_ && cb_.repaint) {
            cb_.repaint(); // 重绘 → 缺瓦片重新入抓取队列（定点重抓）
        }
    });
}

void TileFetcher::abort_all() {
    const std::vector<QNetworkReply *> in_flight = [this] {
        std::vector<QNetworkReply *> v;
        v.reserve(pending_.size());
        for (const PendingTile &t : pending_) {
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

void TileFetcher::teardown() {
    // 销毁期（原 ~MapWidget 口径）：先断开 finished（不再重入处理器）再 abort + deleteLater。
    for (const PendingTile &t : pending_) {
        if (t.reply != nullptr) {
            t.reply->disconnect();
            t.reply->abort();
            t.reply->deleteLater();
        }
    }
    pending_.clear();
}

} // namespace pp::map
