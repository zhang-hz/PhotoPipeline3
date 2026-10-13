// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 地理编码客户端（B.5 拆分步⑤：geocode_client）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.5 步⑤（geocode_client：一条 handler 骨架 +
// 三配置，消 R-158 三份 handler 拷贝与判序 bug）；迁出自 mapwidget.cpp（M7-R2.2-G2，语义逐位）：
//   * **一条骨架** run_reply：abort 旧 reply → UA/8s 超时请求 → finished 读体（abort 过不 readAll）
//     → R-102 `!current` 先于 offline → R-012 世代校验（仅联想）→ 各配置的解析/发射尾巴；
//   * 三配置 = search（周边偏置 + 0 果回退全文 + 双信号对）/ suggest（世代守卫）/ reverse；
//   * R-013 错误串单点脱敏（key= 值不留）；R-077 空 Key 不发请求（文案指向设置页）；
//   * 冻结行为不变：离线文案、失败分类、0 结果分类、发射顺序逐字保留。
#pragma once

#include "mapwidget/geocoder.h"
#include "mapwidget/providers.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <utility>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace pp::map {

class GeocodeClient {
public:
    struct Callbacks {
        // 搜索错误口：调用方发**一对** search_results_ready + search_finished（同文）
        std::function<void(const QString &text)> on_search_error;
        // 搜索成功口：parsed 原序 + titles（调用方建 results/图钉后按 frozen 序发双信号）
        std::function<void(const std::vector<GeocodeResult> &parsed, const QStringList &titles)>
            on_search_ok;
        std::function<void(const std::vector<SuggestItem> &items, const QString &error)> on_suggest;
        std::function<void(const QString &address, const QString &error)> on_reverse;
    };

    GeocodeClient(QObject *context, QNetworkAccessManager *nam, Callbacks cb);

    void set_provider(const TileProvider *provider) { provider_ = provider; }
    void set_offline(bool off) { offline_ = off; }
    bool offline() const { return offline_; }
    void set_amap_key(const QString &key) { amap_key_ = key; }

    // R-012：联想世代（run_suggest 每次含空查询递增；世代不符的回调静默丢弃）
    quint64 bump_suggest_generation() { return ++suggest_generation_; }

    void abort_search();
    void abort_suggest();
    void abort_reverse();
    void abort_all(); // 三类一并在途取消（离线切换路径）
    void teardown();  // 销毁路径：disconnect + abort + deleteLater（不重入处理器）

    // 搜索请求（with_near = 视口中心周边偏置；false = 全文回退路径）。
    // center_wgs = 视口中心（WGS-84；geocoder 转该源基准）。
    void start_search(const Geocoder *geocoder, const QString &query, bool with_near,
                      const std::pair<double, double> &center_wgs);
    void start_suggest(const Geocoder *geocoder, const QString &query, quint64 generation);
    // 逆地理：坐标一律 WGS-84（R-219：源基准转换只在 geocoder URL 边界内）
    void start_reverse(const Geocoder *geocoder, double lat, double lon);

    // R-077：高德源空 Key **不发请求**（run_* 与 start_* 双保险预检共用）
    bool needs_key_missing(const Geocoder *geocoder) const;
    static QString missing_key_text();
    // R-013：错误串单点脱敏（key= 的值一律不留）
    static QString redact_secrets(const QString &detail);
    static QString request_failure_text(const QString &what, const Geocoder *geocoder,
                                        const QString &detail);

private:
    // 一条 handler 骨架的配置（R-158：三份 finished 处理器共同前段收口 run_request）
    struct RequestConfig {
        QUrl url;
        QString what;         // 失败分类文案主语（搜索/联想/逆地理）
        QString offline_text; // 离线冻结文案（docs/m1b-tasks.md §2.5）
        const Geocoder *geocoder = nullptr;
        quint64 generation = 0; // >0 = 联想世代校验（R-012）
        std::function<void(const QString &error)> on_error;
        std::function<void(const QByteArray &body)> on_body;
    };
    void run_request(QNetworkReply **slot, const RequestConfig &cfg);

    QObject *context_ = nullptr;
    QNetworkAccessManager *nam_ = nullptr;
    Callbacks cb_;
    const TileProvider *provider_ = nullptr;
    QString amap_key_;
    bool offline_ = false;

    QNetworkReply *search_reply_ = nullptr;
    QNetworkReply *suggest_reply_ = nullptr;
    QNetworkReply *reverse_reply_ = nullptr;
    quint64 suggest_generation_ = 0;
};

} // namespace pp::map
