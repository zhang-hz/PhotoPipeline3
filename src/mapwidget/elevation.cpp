// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M13 — 选点海拔（需求 4；契约见 elevation.h）
//
// 纯函数面 = provider 事实表 + URL 构造 + 响应解析（单测逐字钉 URL/解析，永不发网络）；
// ElevationClient = 异步单在途客户端（QNetworkAccessManager 归本模块持有）。
// 错误文案口径（C5「失败静默降级、不阻塞」）：网络/HTTP/解析失败只经回调返回短中文原因，
// 由 GPS 卡写 alt tooltip —— 不弹窗、不写 gps_status、不产生任何阻塞等待。
#include "mapwidget/elevation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariant>

#include <cmath>
#include <cstddef>
#include <utility>

namespace pp::map {

namespace {

// 超时口径（docs/m13-tasks.md §7 C5：5s）。自管定时器而非 QNetworkRequest::setTransferTimeout：
// 后者在 Qt 6 的超时错误码与用户主动 abort 不可区分（都可能是 OperationCanceledError），
// 「超时」文案就落不了地；这里超时 = 显式中止 + 专属文案。
constexpr int kElevationTimeoutMs = 5000;
// UA = 产品名（C5 逐字；无 key/无鉴权）。
constexpr const char *kUserAgent = "PhotoPipeline";
// 坐标 URL 定点小数位（C5 的 <lat6>/<lon6>）。
constexpr int kCoordDecimals = 6;

// 缺省 open-meteo；opentopodata 为备选源（失败不自动重试 —— 契约口径）。加源 = 加一行。
const ElevationProvider kProviders[] = {
    {QStringLiteral("open-meteo"), QStringLiteral("Open-Meteo"),
     QStringLiteral("https://api.open-meteo.com/v1/elevation"), true},
    {QStringLiteral("opentopodata"), QStringLiteral("OpenTopoData"),
     QStringLiteral("https://api.opentopodata.org/v1/srtm30m"), false},
};
constexpr int kProviderCount = int(std::size(kProviders));

// 6 位定点（负零归零：经度 0 不出现 "-0.000000"）。
QString coord6(double value) {
    const double normalized = value == 0.0 ? 0.0 : value;
    return QString::number(normalized, 'f', kCoordDecimals);
}

bool valid_coord(double lat, double lon) {
    return std::isfinite(lat) && std::isfinite(lon) && lat >= -90.0 && lat <= 90.0 &&
           lon >= -180.0 && lon <= 180.0;
}

} // namespace

QUrl ElevationProvider::url(double lat, double lon) const {
    if (!valid_coord(lat, lon)) {
        return QUrl();
    }
    const QString query =
        open_meteo ? QStringLiteral("latitude=%1&longitude=%2").arg(coord6(lat), coord6(lon))
                   : QStringLiteral("locations=%1,%2").arg(coord6(lat), coord6(lon));
    return QUrl(endpoint + QLatin1Char('?') + query);
}

const ElevationProvider *elevation_provider_by_id(const QString &id) {
    for (const ElevationProvider &provider : kProviders) {
        if (provider.id == id) {
            return &provider;
        }
    }
    return nullptr;
}

const ElevationProvider *elevation_default_provider() { return &kProviders[0]; }

QStringList elevation_provider_ids() {
    QStringList ids;
    ids.reserve(kProviderCount);
    for (const ElevationProvider &provider : kProviders) {
        ids << provider.id;
    }
    return ids;
}

QUrl elevation_url(const QString &provider_id, double lat, double lon) {
    const ElevationProvider *provider = elevation_provider_by_id(provider_id);
    if (provider == nullptr) {
        return QUrl();
    }
    return provider->url(lat, lon);
}

bool parse_elevation(const QString &provider_id, const QByteArray &body, double *out,
                     QString *err) {
    const auto fail = [err](const QString &text) {
        if (err != nullptr) {
            *err = text;
        }
        return false;
    };
    const ElevationProvider *provider = elevation_provider_by_id(provider_id);
    if (provider == nullptr) {
        return fail(QStringLiteral("未知的海拔源"));
    }
    QJsonParseError parse_error{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(QStringLiteral("响应不是合法 JSON 对象"));
    }
    const QJsonObject root = doc.object();
    QJsonValue elevation;
    if (provider->open_meteo) {
        // open-meteo: {"elevation":[38.0]}（单点请求 = 单元素数组）
        const QJsonValue array_value = root.value(QStringLiteral("elevation"));
        if (!array_value.isArray()) {
            return fail(QStringLiteral("响应缺少 elevation 数组"));
        }
        const QJsonArray values = array_value.toArray();
        if (values.isEmpty()) {
            return fail(QStringLiteral("elevation 数组为空"));
        }
        elevation = values.at(0);
    } else {
        // opentopodata: {"results":[{"elevation":38.0,...}]}
        const QJsonValue array_value = root.value(QStringLiteral("results"));
        if (!array_value.isArray()) {
            return fail(QStringLiteral("响应缺少 results 数组"));
        }
        const QJsonArray rows = array_value.toArray();
        if (rows.isEmpty()) {
            return fail(QStringLiteral("results 数组为空"));
        }
        if (!rows.at(0).isObject()) {
            return fail(QStringLiteral("results[0] 不是对象"));
        }
        elevation = rows.at(0).toObject().value(QStringLiteral("elevation"));
    }
    if (elevation.isNull() || !elevation.isDouble()) {
        return fail(QStringLiteral("响应缺少数值型 elevation 字段"));
    }
    const double value = elevation.toDouble();
    if (!std::isfinite(value)) {
        return fail(QStringLiteral("海拔不是有限数值"));
    }
    if (out != nullptr) {
        *out = value;
    }
    if (err != nullptr) {
        err->clear();
    }
    return true;
}

// ---------------------------------------------------------------------------
// ElevationClient：异步、单在途、序号防陈旧
// ---------------------------------------------------------------------------

ElevationClient::ElevationClient(QObject *parent) : QObject(parent) {
    nam_ = new QNetworkAccessManager(this);
    timeout_timer_ = new QTimer(this);
    timeout_timer_->setSingleShot(true);
    timeout_timer_->setInterval(kElevationTimeoutMs);
    QObject::connect(timeout_timer_, &QTimer::timeout, this, [this] {
        if (reply_ == nullptr) {
            return;
        }
        timed_out_ = true; // finished 处理器据此给「请求超时」（与主动取消区分）
        reply_->abort();
    });
    provider_id_ = elevation_default_provider()->id;
}

ElevationClient::~ElevationClient() { abort_in_flight(); }

void ElevationClient::set_provider(const QString &provider_id) {
    provider_id_ = elevation_provider_by_id(provider_id) != nullptr
                       ? provider_id
                       : elevation_default_provider()->id;
}

void ElevationClient::abort_in_flight() {
    timeout_timer_->stop();
    timed_out_ = false;
    if (reply_ == nullptr) {
        return;
    }
    QNetworkReply *const old = reply_;
    reply_ = nullptr;  // 先置空：finished 若同步重入，按陈旧丢弃
    old->disconnect(); // 断开本客户端处理器（取消 = 静默，不回调）
    old->abort();
    old->deleteLater();
}

void ElevationClient::cancel() {
    abort_in_flight();
    ++seq_; // 已排队/在途响应一律作废
}

void ElevationClient::finish(quint64 seq, double lat, double lon, double elevation_m,
                             const QString &error) {
    if (cb_.on_done) {
        cb_.on_done(seq, lat, lon, elevation_m, error);
    }
}

quint64 ElevationClient::request(double lat, double lon) {
    abort_in_flight();
    const quint64 seq = ++seq_;
    const ElevationProvider *provider = elevation_provider_by_id(provider_id_);
    if (provider == nullptr) {
        provider = elevation_default_provider();
    }
    const QUrl url = provider->url(lat, lon);
    if (!url.isValid()) {
        finish(seq, lat, lon, 0.0, QStringLiteral("坐标无效"));
        return seq;
    }
    timed_out_ = false;
    QNetworkRequest net_request(url);
    net_request.setRawHeader(QByteArrayLiteral("User-Agent"),
                             QByteArray(kUserAgent)); // UA = 产品名（C5）
    net_request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    QNetworkReply *const reply = nam_->get(net_request);
    reply_ = reply;
    timeout_timer_->start();
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, seq, lat, lon, provider] {
        if (reply_ != reply) { // 兜底：已被新请求取代/取消（abort 路径已 disconnect）
            reply->deleteLater();
            return;
        }
        reply_ = nullptr;
        timeout_timer_->stop();
        const bool timed_out = timed_out_;
        timed_out_ = false;
        const QNetworkReply::NetworkError net_error = reply->error();
        const QString net_text = reply->errorString();
        const QVariant status_value = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int http_status = status_value.isValid() ? status_value.toInt() : 0;
        // abort 过的 reply 设备已关闭：不再 readAll（否则 Qt 打 "device not open" 噪声）。
        const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
        reply->deleteLater();
        if (seq != seq_) { // 陈旧响应（cancel 已推进序号）：静默丢弃
            return;
        }
        const auto fail = [this, seq, lat, lon](const QString &error) {
            finish(seq, lat, lon, 0.0, error);
        };
        if (timed_out) {
            fail(QStringLiteral("请求超时"));
            return;
        }
        if (net_error == QNetworkReply::OperationCanceledError) {
            return; // 用户/程序主动取消（非超时）：静默
        }
        if (http_status >= 400) {
            fail(QStringLiteral("HTTP %1").arg(http_status));
            return;
        }
        if (net_error != QNetworkReply::NoError) {
            switch (net_error) {
            case QNetworkReply::TimeoutError:
                fail(QStringLiteral("请求超时"));
                return;
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::ProxyConnectionRefusedError:
            case QNetworkReply::ProxyNotFoundError:
            case QNetworkReply::ProxyTimeoutError:
                fail(QStringLiteral("网络不可用"));
                return;
            default:
                fail(QStringLiteral("请求失败：%1").arg(net_text)); // 失败分类原文（无 key 可泄）
                return;
            }
        }
        double elevation_m = 0.0;
        QString parse_error;
        if (!parse_elevation(provider->id, body, &elevation_m, &parse_error)) {
            fail(parse_error);
            return;
        }
        finish(seq, lat, lon, elevation_m, QString());
    });
    return seq;
}

} // namespace pp::map
