// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M13 — 选点海拔（需求 4；新模块，非冻结）
//
// Contract: docs/m13-tasks.md §7 C5（GPS 选点自动海拔）+ §4.5（GPS 卡选择项级）。
// 口径（加性新文件；provider 选择暂不暴露 UI，事实表留扩展位）：
//   * provider 事实表两档：open-meteo（默认，GET
//   .../v1/elevation?latitude=<lat6>&longitude=<lon6>，
//     响应 {"elevation":[数值]}）与 opentopodata（备选，GET
//     .../v1/srtm30m?locations=<lat6>,<lon6>， 响应 {"results":[{"elevation":数值}]}）；无
//     key、无鉴权；失败**不自动重试**；
//   * 纯函数 elevation_url(provider_id, lat, lon) 与 parse_elevation(provider_id, body, out, err)
//     —— URL/解析逐字进单测（tests/unit/test_elevation.cpp，不发真实网络）；
//   * ElevationClient：QNetworkAccessManager 异步、单在途（新请求 abort 旧响应 + 单调序号丢弃
//     陈旧回调）、5s 超时、UTF-8 解析；离线/超时/非 200/解析失败 → 明确 error 文案（不弹窗、
//     不阻塞选点/写盘）。QNetworkAccessManager 由本模块持有（不碰冻结面 providers.*）。
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <utility>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace pp::map {

// 海拔源事实（单表两档；加源 = 加一行）。坐标入参一律 WGS-84（EXIF GPS 口径，与选点写回值
// 同基准；本模块不做任何基准转换）。
struct ElevationProvider {
    QString id;             // "open-meteo" | "opentopodata"
    QString label;          // 用户可见名（工具提示「自动获取：N m（Open-Meteo）」用）
    QString endpoint;       // GET 基址（不含 query）
    bool open_meteo = true; // 响应形态分派：true = {"elevation":[…]}，false = {"results":[…]}
    // 坐标按 6 位小数定点格式化（服务端精度上限远高于 1e-6°；避免科学计数法/区域设置差异）。
    QUrl url(double lat, double lon) const;
};

const ElevationProvider *elevation_provider_by_id(const QString &id);
const ElevationProvider *elevation_default_provider(); // open-meteo
QStringList elevation_provider_ids();

// ---- 纯函数面（无网络，单测逐字验证）----
// provider_id 未知 / 坐标非有限或越界（|lat|>90 或 |lon|>180）→ 空 QUrl。
QUrl elevation_url(const QString &provider_id, double lat, double lon);
// 成功返回 true 且 *out = 海拔（米）；失败返回 false，*err（可空）给中文原因。
// out/err 均可空指针；失败时不改写 *out。
bool parse_elevation(const QString &provider_id, const QByteArray &body, double *out,
                     QString *err = nullptr);

// ---- 异步海拔客户端（QObject 子类；std::function 回调、无 Q_OBJECT —— 免 moc）----
class ElevationClient : public QObject {
public:
    struct Callbacks {
        // error 空 = 成功（elevation_m = 米）；失败 elevation_m = 0。
        // seq/lat/lon 回带请求上下文：调用方据此校验「当前坐标仍等于请求坐标」与陈旧响应。
        std::function<void(quint64 seq, double lat, double lon, double elevation_m,
                           const QString &error)>
            on_done;
    };

    explicit ElevationClient(QObject *parent = nullptr);
    ~ElevationClient() override;

    // 缺省 open-meteo；未知 id 回落默认。运行期换源只影响后续请求。
    void set_provider(const QString &provider_id);
    QString provider_id() const { return provider_id_; }
    void set_callbacks(Callbacks cb) { cb_ = std::move(cb); }

    // 发起请求（异步、非阻塞）：自动取消在途；返回本次请求序号（>0）。
    // 陈旧响应（cancel()/新请求已推进序号）永不回调；坐标非法 → 异步语义的 error 回调。
    quint64 request(double lat, double lon);
    void cancel(); // 取消在途（序号递增 → 旧响应静默丢弃；不触发回调）
    bool busy() const { return reply_ != nullptr; }
    quint64 last_seq() const { return seq_; }

private:
    void abort_in_flight();
    void finish(quint64 seq, double lat, double lon, double elevation_m, const QString &error);

    QNetworkAccessManager *nam_ = nullptr;
    QNetworkReply *reply_ = nullptr;
    QTimer *timeout_timer_ = nullptr; // 5s 超时（自管，超时 = 明确文案而非静默取消）
    bool timed_out_ = false;
    quint64 seq_ = 0;
    QString provider_id_;
    Callbacks cb_;
};

} // namespace pp::map
