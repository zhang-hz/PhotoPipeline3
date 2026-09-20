// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M2-T11b 证据程序：用 **AppImage 内的 Qt 库与 tls/ 插件**发一次真实 HTTPS GET，
// 证明打包后的 TLS 后端可用（在线地图瓦片/经纬度反查依赖它）。
//
// 用法（见 T11 报告）:
//   g++ -std=c++17 -fPIC tools/tls_probe.cpp -o /tmp/tls_probe \
//       -I"$QT_DIR/include" -I"$QT_DIR/include/QtCore" -I"$QT_DIR/include/QtNetwork" \
//       -L"$QT_DIR/lib" -lQt6Core -lQt6Network
//   APP=<repo>/dist/PhotoPipeline.AppDir
//   LD_LIBRARY_PATH="$APP/usr/lib" QT_PLUGIN_PATH="$APP/usr/lib/qt-plugins" /tmp/tls_probe [URL]
// 退出码: 0 = HTTP 200 + 非空 PNG；1 = 请求/内容断言失败；2 = 超时。
// 注意：可执行文件**不带 rpath**，因此运行期 Qt 库只能来自 LD_LIBRARY_PATH（= AppDir 副本）。

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>

#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QUrl url(argc > 1 ? QString::fromLocal8Bit(argv[1])
                            : QStringLiteral("https://tile.openstreetmap.org/0/0/0.png"));

    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("PhotoPipeline/0.1.0 TLS probe (M2-T11b)"));
    QNetworkReply* reply = nam.get(request);

    // 用户可读的 TTY/stdout 进度：插件加载与握手失败会在这里显形
    QObject::connect(reply, &QNetworkReply::sslErrors, [](const QList<QSslError>& errors) {
        for (const QSslError& e : errors) {
            std::printf("ssl_error: %s\n", e.errorString().toUtf8().constData());
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const bool png_magic = body.startsWith(QByteArray::fromHex("89504e470d0a1a0a"));
        std::printf("probe url=%s\n", url.toString().toUtf8().constData());
        std::printf("probe tls=%s\n", QSslSocket::supportsSsl() ? "supported" : "MISSING");
        std::printf("probe http_status=%d bytes=%lld png_magic=%s\n", status,
                    static_cast<long long>(body.size()), png_magic ? "yes" : "no");
        std::printf("probe reply_error=%d (%s)\n", static_cast<int>(reply->error()),
                    reply->errorString().toUtf8().constData());
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200 &&
                        png_magic && !body.isEmpty();
        std::printf("TLS-PROBE %s\n", ok ? "OK" : "FAIL");
        app.exit(ok ? 0 : 1);
    });
    QTimer::singleShot(30000, &app, [&]() {
        std::printf("TLS-PROBE TIMEOUT\n");
        app.exit(2);
    });
    return app.exec();
}
