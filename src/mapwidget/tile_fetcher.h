// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 瓦片抓取器（B.5 拆分步③：tile_fetcher）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.5 步③（tile_fetcher：pending/并发 4/8s/offline
// 门闸，paint 不再发请求）；迁出自 mapwidget.cpp（M7-R2.2-G2，门闸/失败语义逐位一致）：
//   * 并发 ≤ 4、同键不重发、8s 超时、UA 随 provider 单源、离线零请求（frozen）；
//   * R-221：paint 只登记（queue 集合语义），0ms 一次性定时器在 paint 外 flush；
//   * R-101：失败后 750ms 延迟重抓（不叠加）；R-161：失败 streak 只被**解码成功**的瓦片复位
//     （sink 返回 true；坏 payload 不抵消连续失败计数，banner 阈值 ≥3 可触发）；
//   * 网络全在本件；解码/入缓存经 sink 回调（调用方 = MapWidget::Impl 的 TileCache）。
#pragma once

#include "mapwidget/providers.h"
#include "mapwidget/tile_cache.h"

#include <QByteArray>
#include <QObject>
#include <QUrl>

#include <functional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace pp::map {

class TileFetcher {
public:
    struct Callbacks {
        // 解码 + 入缓存；返回**解码成功**（R-161 streak 判据）
        std::function<bool(const TileKey &, const QByteArray &)> sink;
        std::function<void(int pending_count)> pending_changed;
        std::function<void(bool visible)> banner; // 失败横幅显隐（≥3 连败）
        std::function<void()> repaint;            // 延迟重抓触发的重绘
    };

    TileFetcher(QObject *context, QNetworkAccessManager *nam, Callbacks cb);

    void set_provider(const TileProvider *provider) { provider_ = provider; }
    void set_offline(bool off) { offline_ = off; }
    bool offline() const { return offline_; }

    // R-221：登记缺瓦片（集合语义、可重复）；0ms 一次性定时器统一 flush
    void queue(const TileKey &key);
    // 真正发请求（并发/离线/同键门闸在内）；上层在 resize/重抓等路径显式 flush 时调用
    void flush();

    void abort_all();      // 在途全部 abort（finished 处理器负责清 pending）
    void teardown();       // 销毁路径：disconnect + abort + deleteLater（不重入处理器）
    void reset_failures(); // 失败 streak 清零（离线恢复等路径）
    int pending_count() const;

private:
    struct PendingTile {
        QNetworkReply *reply = nullptr;
        TileKey key; // 完成的瓦片按这个键交 sink（source/layer/zoom/x/y）
    };

    bool is_pending(const TileKey &key) const;
    void request_tile(const TileKey &key);
    void on_tile_finished(QNetworkReply *reply);
    void notify_pending();
    void note_failure();
    void schedule_tile_retry();

    QObject *context_ = nullptr;
    QNetworkAccessManager *nam_ = nullptr;
    Callbacks cb_;
    const TileProvider *provider_ = nullptr;
    bool offline_ = false;

    std::vector<PendingTile> pending_;
    std::vector<TileKey> fetch_wanted_; // R-221：paint 登记的待抓集合
    bool fetch_flush_scheduled_ = false;
    bool retry_scheduled_ = false; // R-101：失败重抓定时器在途（不叠加）
    int consecutive_failures_ = 0;
};

} // namespace pp::map
