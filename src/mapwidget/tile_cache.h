// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 瓦片缓存纯核（B.5 拆分步②：tile_cache）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.5 步②（tile_cache：TileKey/LRU/预算，零 Qt
// Widgets 可单测）；迁出自 mapwidget.cpp（M7-R2.2-G2，语义逐位一致）。
//   * TileKey = {source, layer, zoom, x, y}（M6-T5 维度；qHash 走 ADL，QHash 可用）；
//   * TileCache<Payload> = LRU + 字节预算：touch = 队头；insert = 替换同键旧项后按预算从
//     队尾逐出（单件超预算会被立即逐出 —— 与原 evict_to_budget 循环逐位同义）；
//   * 预算语义（Ruling 2026-09-19）：只存解码后载荷、按解码足迹计费 —— set_budget 真正
//     约束进程内存（64 MB ≈ 256 张 256×256 ARGB32 瓦片）；
//   * 载荷模板化：产品 = QPixmap，单测 = 哑载荷（tests/unit/test_gcj02.cpp 钉真值表）。
// header-only（R-109 路线，同 format_policy.h / time_tag_keys.h）。
#pragma once

#include <QHash>
#include <QString>
#include <QtGlobal>

#include <cstddef>
#include <list>
#include <utility>

namespace pp::map {

// M6-T5 缓存 key 的 layer 维度词表（§4.1）：路网 / 影像 / Hybrid 标注叠加层。
inline constexpr const char *kLayerRoad = "road";
inline constexpr const char *kLayerSatellite = "satellite";
inline constexpr const char *kLayerLabel = "label";

// 缓存 key。namespace 域（非匿名）：qHash 须经 ADL 被 QHash 找到（原 mapwidget.cpp 同因）。
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

// B.5 ②（M7R22G2）：LRU + 字节预算纯核（零 Qt Widgets；行为 = mapwidget.cpp 原实现逐位）。
template <class Payload> class TileCache {
public:
    struct Entry {
        Payload payload;
        qint64 footprint = 0; // 解码足迹（字节，计入预算）
        typename std::list<TileKey>::iterator lru_pos;
    };

    explicit TileCache(qint64 budget = 0) : budget_(budget) {}

    void set_budget(qint64 bytes) {
        budget_ = bytes;
        evict_to_budget();
    }
    qint64 budget() const { return budget_; }
    qint64 bytes() const { return bytes_; }
    std::size_t size() const { return cache_.size(); }

    // 命中 = touch（提到最近使用）；未命中 = nullptr
    const Payload *find(const TileKey &key) {
        const auto it = cache_.find(key);
        if (it == cache_.end()) {
            return nullptr;
        }
        lru_.splice(lru_.begin(), lru_, it->lru_pos); // touch: move to most-recently-used
        it->lru_pos = lru_.begin();
        return &it->payload;
    }

    // 入缓存（替换同键旧项 → 队头 → 按预算逐出）。**成功语义 = 解码成功**（调用方判据，
    // R-161：失败 streak 只被解码成功的瓦片复位）—— 即使随即被预算逐出，调用方仍按成功论
    // （与原 mapwidget.cpp insert_tile 的返回口径逐位一致）。
    void insert(const TileKey &key, Payload payload, qint64 footprint) {
        const auto existing = cache_.find(key);
        if (existing != cache_.end()) {
            bytes_ -= existing->footprint;
            lru_.erase(existing->lru_pos);
            cache_.erase(existing);
        }
        lru_.push_front(key);
        Entry entry;
        entry.payload = std::move(payload);
        entry.footprint = footprint;
        entry.lru_pos = lru_.begin();
        bytes_ += footprint;
        cache_.insert(key, std::move(entry));
        evict_to_budget();
    }

    // 预算收紧/放宽后的逐出（队尾 = 最久未用）
    void evict_to_budget() {
        while (bytes_ > budget_ && !lru_.empty()) {
            const TileKey oldest = lru_.back();
            lru_.pop_back();
            const auto it = cache_.find(oldest);
            if (it != cache_.end()) {
                bytes_ -= it->footprint;
                cache_.erase(it);
            }
        }
    }

    void clear() {
        cache_.clear();
        lru_.clear();
        bytes_ = 0;
    }

private:
    QHash<TileKey, Entry> cache_;
    std::list<TileKey> lru_; // front = most recently used
    qint64 bytes_ = 0;
    qint64 budget_ = 0;
};

} // namespace pp::map
