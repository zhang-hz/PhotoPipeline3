// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 地理编码客户端（B.5 拆分步⑤；契约见 geocode_client.h）
//
// 实现 = mapwidget.cpp 的 start_search/start_suggest/start_reverse/abort_*/make_request/
// redact_secrets/request_failure_text/missing_key_text 逐段迁出（M7-R2.2-G2）；
// 三份 finished 处理器的共同前段（R-158）收口 run_request 一条骨架 + 三配置。
// tr 上下文保持 MapWidget（冻结文案查表键不变：MapWidget::tr 原样调用）。
#include "mapwidget/geocode_client.h"

#include "mapwidget/mapwidget.h" // MapWidget::tr（冻结文案查表键不变）

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <chrono>
#include <optional>
#include <utility>

namespace pp::map {

namespace {

constexpr int kRequestTimeoutMs = 8000; // frozen: request timeout 8 s

} // namespace

GeocodeClient::GeocodeClient(QObject *context, QNetworkAccessManager *nam, Callbacks cb)
    : context_(context), nam_(nam), cb_(std::move(cb)) {}

void GeocodeClient::abort_search() {
    if (search_reply_ != nullptr) {
        QNetworkReply *const reply = search_reply_;
        search_reply_ = nullptr;
        reply->abort();
    }
}

void GeocodeClient::abort_suggest() {
    if (suggest_reply_ != nullptr) {
        QNetworkReply *const reply = suggest_reply_;
        suggest_reply_ = nullptr;
        reply->abort();
    }
}

void GeocodeClient::abort_reverse() {
    if (reverse_reply_ != nullptr) {
        QNetworkReply *const reply = reverse_reply_;
        reverse_reply_ = nullptr;
        reply->abort();
    }
}

void GeocodeClient::abort_all() {
    abort_search();
    abort_suggest();
    abort_reverse();
}

void GeocodeClient::teardown() {
    // 销毁期（原 ~MapWidget 口径）：先断开 finished（不再重入处理器）再 abort + deleteLater。
    for (QNetworkReply *reply : {search_reply_, suggest_reply_, reverse_reply_}) {
        if (reply != nullptr) {
            reply->disconnect();
            reply->abort();
            reply->deleteLater();
        }
    }
    search_reply_ = nullptr;
    suggest_reply_ = nullptr;
    reverse_reply_ = nullptr;
}

// 搜索管线请求的统一构造：每 provider 一个 UA（OSM/Nominatim 政策要求非空 UA）+ 8s 超时。
static QNetworkRequest make_request(const TileProvider *provider, const QUrl &url) {
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
QString GeocodeClient::redact_secrets(const QString &detail) {
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
QString GeocodeClient::request_failure_text(const QString &what, const Geocoder *geocoder,
                                            const QString &detail) {
    const QString safe = redact_secrets(detail);
    if (geocoder != nullptr && !geocoder->requires_key) {
        return MapWidget::tr("%1请求失败：%2（该源大陆不可达，请切高德）").arg(what, safe);
    }
    return MapWidget::tr("%1请求失败：%2").arg(what, safe);
}

QString GeocodeClient::missing_key_text() {
    return MapWidget::tr("未配置高德 Key，请到设置›地图填写");
}

bool GeocodeClient::needs_key_missing(const Geocoder *geocoder) const {
    return geocoder != nullptr && geocoder->requires_key && amap_key_.isEmpty();
}

// —— 一条 handler 骨架（R-158 收口）：abort 旧 reply → UA/超时请求 → finished 统一读体/判序 ——
void GeocodeClient::run_request(QNetworkReply **slot, const RequestConfig &cfg) {
    if (*slot != nullptr) { // 每类请求至多一个在途（新请求 abort 旧的）
        QNetworkReply *const old = *slot;
        *slot = nullptr;
        old->abort();
    }
    QNetworkReply *const reply = nam_->get(make_request(provider_, cfg.url));
    *slot = reply;
    QObject::connect(reply, &QNetworkReply::finished, context_, [this, slot, reply, cfg] {
        const bool current = (*slot == reply);
        if (current) {
            *slot = nullptr;
        }
        // abort 过的 reply 设备已关闭：不再 readAll（否则 Qt 打 "device not open" 噪声）。
        const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
        const QNetworkReply::NetworkError error = reply->error();
        const QString error_string = reply->errorString();
        reply->deleteLater();
        // R-102（M7-WP7）：`!current`（已被新请求/离线 abort 取代）**先于** offline 判定
        // —— 否则切离线瞬间的在途请求会吐「离线模式」错误清列表、旧钉却残留。
        if (!current) {
            return; // 已被新请求取代：静默丢弃
        }
        // R-012：世代过期（清空/回车/新一轮联想）同样静默丢弃（generation==0 = 不校验）。
        if (cfg.generation != 0 && cfg.generation != suggest_generation_) {
            return;
        }
        if (offline_) {
            if (cfg.on_error) {
                cfg.on_error(cfg.offline_text); // 冻结文案（docs/m1b-tasks.md §2.5）
            }
            return;
        }
        if (error != QNetworkReply::NoError) {
            if (cfg.on_error) {
                cfg.on_error(request_failure_text(cfg.what, cfg.geocoder, error_string));
            }
            return;
        }
        if (cfg.on_body) {
            cfg.on_body(body);
        }
    });
}

// 搜索请求（含周边偏置开关）：near = 视口中心（WGS-84），由 geocoder 转该源基准
// （amap around 的 location 是 GCJ-02）。with_near=false 即全文搜（0 结果时的回退路径）。
// 每次 run_search 恰好以一对 search_results_ready + search_finished 收尾（含失败）。
void GeocodeClient::start_search(const Geocoder *geocoder, const QString &query, bool with_near,
                                 const std::pair<double, double> &center_wgs) {
    if (geocoder == nullptr) {
        const QString text = MapWidget::tr("未知的地理编码源");
        if (cb_.on_search_error) {
            cb_.on_search_error(text);
        }
        return;
    }
    if (needs_key_missing(geocoder)) { // R-077：空 Key 不发请求（与 run_search 同一预检）
        const QString text = missing_key_text();
        if (cb_.on_search_error) {
            cb_.on_search_error(text);
        }
        return;
    }
    const std::optional<std::pair<double, double>> near =
        with_near ? std::optional<std::pair<double, double>>(center_wgs) : std::nullopt;
    const QString fallback_query = query; // 周边无果时回退全文用（下游按值捕获 = 拷贝）
    RequestConfig cfg;
    cfg.url = geocoder->search_url(query, amap_key_, near);
    cfg.what = MapWidget::tr("搜索");
    cfg.geocoder = geocoder;
    cfg.offline_text = MapWidget::tr("离线模式：搜索不可用");
    cfg.on_error = [this](const QString &text) {
        if (cb_.on_search_error) {
            cb_.on_search_error(text); // 双信号对（ready + finished 同文）
        }
    };
    cfg.on_body = [this, geocoder, with_near, fallback_query](const QByteArray &body) {
        std::vector<GeocodeResult> parsed;
        const QString parse_error = parse_search(body, geocoder->id, parsed);
        if (!parse_error.isEmpty()) {
            if (cb_.on_search_error) {
                cb_.on_search_error(parse_error);
            }
            return;
        }
        if (parsed.empty()) {
            // 周边偏置消费 near 的只有 amap 的 around 端点；nominatim 忽略 near（两次同
            // URL）， 故只在周边真生效时回退一次全文（§4.3「优先周边偏置，回退全文」）。
            if (with_near && geocoder->id == QLatin1String("amap")) {
                start_search(geocoder, fallback_query, false, {});
                return;
            }
            const QString text = MapWidget::tr("无匹配结果"); // 0 结果分类（§4.3）
            if (cb_.on_search_error) {
                cb_.on_search_error(text);
            }
            return;
        }
        // 冻结事实（docs/m1b-tasks.md §2.5）：search_finished 顺序 == 内部 results 顺序，
        // 故 d.results 与图钉都按 parsed 原序生成，search_select(i) 直接索引同一结果。
        QStringList titles;
        titles.reserve(int(parsed.size()));
        for (const GeocodeResult &result : parsed) {
            titles << result.title;
        }
        if (cb_.on_search_ok) {
            cb_.on_search_ok(parsed, titles);
        }
    };
    run_request(&search_reply_, cfg);
}

// 联想请求（amap inputtips；nominatim 无此端点，由调用方先行拦截）。
// R-012（M7-WP7）：**查询世代**守卫 —— run_suggest 每次（含空查询）递增世代并 abort 在途
// 联想；回调世代不符即丢弃（清空/回车后旧建议不再复活可点）。
void GeocodeClient::start_suggest(const Geocoder *geocoder, const QString &query,
                                  quint64 generation) {
    RequestConfig cfg;
    cfg.url = geocoder->suggest_url(query, amap_key_);
    cfg.what = MapWidget::tr("联想");
    cfg.geocoder = geocoder;
    cfg.generation = generation;
    cfg.offline_text = MapWidget::tr("离线模式：联想不可用");
    cfg.on_error = [this](const QString &text) {
        if (cb_.on_suggest) {
            cb_.on_suggest({}, text);
        }
    };
    cfg.on_body = [this, geocoder](const QByteArray &body) {
        std::vector<SuggestItem> items;
        const QString parse_error = parse_suggest(body, geocoder->id, items);
        if (!parse_error.isEmpty()) {
            if (cb_.on_suggest) {
                cb_.on_suggest({}, parse_error);
            }
            return;
        }
        if (items.empty()) {
            if (cb_.on_suggest) {
                cb_.on_suggest({}, MapWidget::tr("无匹配结果")); // 0 结果分类（§4.3）
            }
            return;
        }
        if (cb_.on_suggest) {
            cb_.on_suggest(items, QString());
        }
    };
    run_request(&suggest_reply_, cfg);
}

// 逆地理请求：坐标**一律 WGS-84**（R-219：与 search_url 的 near 同一约定 —— 源基准转换
// 只发生在 geocoder 的 URL 构造边界内，调用方永不预转换）。
void GeocodeClient::start_reverse(const Geocoder *geocoder, double lat, double lon) {
    RequestConfig cfg;
    cfg.url = geocoder->reverse_url(lat, lon, amap_key_);
    cfg.what = MapWidget::tr("逆地理");
    cfg.geocoder = geocoder;
    cfg.offline_text = MapWidget::tr("离线模式：逆地理不可用");
    cfg.on_error = [this](const QString &text) {
        if (cb_.on_reverse) {
            cb_.on_reverse(QString(), text);
        }
    };
    cfg.on_body = [this, geocoder](const QByteArray &body) {
        QString address;
        const QString parse_error = parse_reverse(body, geocoder->id, address);
        if (!parse_error.isEmpty()) {
            if (cb_.on_reverse) {
                cb_.on_reverse(QString(), parse_error);
            }
            return;
        }
        if (address.isEmpty()) {
            // 0 结果分类（§4.3）：坐标没有解析出地址（如海面/无地址区）。
            if (cb_.on_reverse) {
                cb_.on_reverse(QString(), MapWidget::tr("未找到该坐标的地址"));
            }
            return;
        }
        if (cb_.on_reverse) {
            cb_.on_reverse(address, QString());
        }
    };
    run_request(&reverse_reply_, cfg);
}

} // namespace pp::map
