// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — application entry point.
//
// M7-WP8 / B.6（R-193/323，docs/deep-review-0.3.2.md 附录 B.6）：main.cpp 拆四 TU ——
// 本文件只留 GUI 启动 + 分发 + §4.3 冻结启动序（约 150 行）；--dev harness 迁
// src/dev_cli.cpp、--dev bench 迁 src/dev_bench.cpp、--ui-smoke 迁 src/dev_ui_smoke.cpp。
// dev 三 TU 仅在 -DPP_BUILD_DEV=ON 构建（CMakeLists §4b），release 产物零 dev 代码路径。
//
// 历史义务（M1-T8 起，逐条保留）：
//   1. `pp::set_qt_version_string(qVersion())` before anything else, so the run-header log
//      carries the Qt version (§3.1 landing revision; core has no Qt dependency).
//   2. the frozen dev harness `--dev` (§3.15), guarded by PP_BUILD_DEV. The macro is wired in
//      CMakeLists.txt (`if(PP_BUILD_DEV) target_compile_definitions(photopipeline PRIVATE
//      PP_BUILD_DEV) endif()`), so a plain release build contains no dev code path.
//   3. M1b-U10: the `--ui-smoke` scripted walk (§4.3) and the GUI start-up order frozen there
//      (style attempt → data_dir → load_settings → log level → log_init → MainWindow → exec →
//      log_shutdown).
//   4. M2-T3 (§2.3): `pp::level_from_env_or()` is the single PP_LOG_LEVEL entry point, called at
//      start-up by the GUI, `--dev` and `--ui-smoke` paths before the level is used.
//   5. M2-T11 (§2.1/§2.2): `--version` prints the frozen single line `PhotoPipeline <version>`
//      from the generated core/version.h (PP_VERSION_STRING) and exits 0, in *every* build
//      (no PP_BUILD_DEV gate); it is handled before QApplication, but only when no
//      `--dev`/`--ui-smoke` switch is present, so both harnesses keep their exact behaviour
//      (including their unknown-option exit 2) and unknown arguments still start the GUI.

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QSplashScreen>
#include <QString>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif

#include "core/logger.h"
#include "core/settings.h"
#include "core/version.h"
#include "platform/mica.h"
#include "platform/paths.h"
#include "ui/mainwindow.h"
#include "ui/theme.h" // M4-W2-fix：preferred_theme_mode()（PP_UI_THEME 强制档的单源解析）

#ifdef PP_BUILD_DEV
// dev 三 TU 的入口（B.6：跨 TU 以 extern 声明衔接，不新增共享头 —— 裁定见
// build/m7-wp8/handoff.md「裁定登记」）。
namespace pp::dev {
int run_dev(int argc, char **argv);
int run_ui_smoke(int argc, char **argv);
} // namespace pp::dev
#endif

#ifdef _WIN32
namespace {
// M3-D5 (v2.0): 句柄是否为"捕获型"（管道/文件重定向）。这类句柄必须原样保留——
// CONOUT$ 会窃走它们，破坏 ctest / subprocess / shell 重定向的冻结行捕获。
bool std_handle_is_captured(DWORD which) {
    const HANDLE h = ::GetStdHandle(which);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        return false;
    }
    const DWORD type = ::GetFileType(h);
    return type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK;
}
} // namespace
#endif

namespace {
// 首次启动的即时反馈：MainWindow 需要构建三页/主题/缩略图线程，Windows 冷启动 + 杀软扫描
// 时可能 1–3s 无窗口。先显示轻量 splash，主窗 show 后 finish；不改变任何启动顺序。
QPixmap startup_splash_pixmap() {
    QPixmap pm(460, 150);
    pm.fill(QColor(34, 37, 43));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont title(QStringLiteral("Segoe UI Variable Text"));
    title.setPixelSize(22);
    title.setWeight(QFont::DemiBold);
    p.setFont(title);
    p.setPen(QColor(245, 247, 250));
    p.drawText(QRect(26, 28, 408, 32), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("PhotoPipeline"));
    QFont sub(QStringLiteral("Microsoft YaHei UI"));
    sub.setPixelSize(12);
    p.setFont(sub);
    p.setPen(QColor(154, 160, 170));
    p.drawText(QRect(26, 70, 408, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("批量像素级转码 · 元数据手术台"));
    p.setPen(QColor(104, 156, 226));
    p.drawText(QRect(26, 108, 408, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("正在加载 · %1").arg(QStringLiteral(PP_VERSION_STRING)));
    return pm;
}
} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
    // M3-D5 (v2.0，取代 v1.4): 判据由"句柄是否有效"改为"句柄是否捕获型"。
    // 捕获型（管道/文件重定向：ctest、tests/*.py 的 subprocess、shell 重定向）原样保留，
    // CONOUT$ 绝不接管；其余情形（句柄缺失，或为 console 型句柄却未附加控制台——
    // pwsh 直调、tools/env.py run 实测静默 0 字节）挂接父控制台，并只重开未被捕获的流。
    const bool out_captured = std_handle_is_captured(STD_OUTPUT_HANDLE);
    const bool err_captured = std_handle_is_captured(STD_ERROR_HANDLE);
    if (!out_captured || !err_captured) {
        const bool attached_now = ::AttachConsole(ATTACH_PARENT_PROCESS) != 0;
        if (attached_now || ::GetConsoleWindow() != nullptr) {
            if (!out_captured) {
                FILE *out = nullptr;
                freopen_s(&out, "CONOUT$", "w", stdout);
            }
            if (!err_captured) {
                FILE *err = nullptr;
                freopen_s(&err, "CONOUT$", "w", stderr);
            }
            if (attached_now) {
                // 仅在自己挂接时改控制台出口代码页，避免污染父 shell 的共享控制台状态。
                ::SetConsoleOutputCP(CP_UTF8);
            }
        }
    }
#endif
    // M2-T11 §2.1（冻结）：--version 非 dev 门控、所有构建可用；stdout 单行 + 退出码 0。
    // 在任何其它参数处理与 QApplication 构造之前返回。优先级取最保守口径：只要 argv 里出现
    // --dev/--ui-smoke，就走原有分支（未知参数在 dev harness 内仍是 exit 2 的用法错误），
    // 因此 --dev/--ui-smoke 的既有行为**逐字节**不变；纯 `--version` 才短路为版本输出。
    bool has_dev_switch = false;
    bool has_version = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dev") == 0 || std::strcmp(argv[i], "--ui-smoke") == 0 ||
            std::strcmp(argv[i], "--ui-traverse") == 0) {
            has_dev_switch = true;
        } else if (std::strcmp(argv[i], "--version") == 0) {
            has_version = true;
        }
    }
    if (has_version && !has_dev_switch) {
        std::printf("PhotoPipeline %s\n", PP_VERSION_STRING);
        return 0;
    }

    // §3.1: core has no Qt dependency; the version string is injected here (before log_init).
    pp::set_qt_version_string(qVersion());

#ifdef PP_BUILD_DEV
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dev") == 0) {
            return pp::dev::run_dev(argc, argv);
        }
        if (std::strcmp(argv[i], "--ui-smoke") == 0 || std::strcmp(argv[i], "--ui-traverse") == 0) {
            return pp::dev::run_ui_smoke(argc, argv);
        }
    }
#else
    // M1-T15: a release build contains no dev harness. This must happen *before* QApplication
    // is constructed: Qt6 silently ignores unknown long options (T14 measured `--dev` → exit 0
    // with empty stdout/stderr), so without this check a release binary would just start the
    // GUI. Report the missing harness and fail with a non-zero exit code instead.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dev") == 0 || std::strcmp(argv[i], "--ui-smoke") == 0 ||
            std::strcmp(argv[i], "--ui-traverse") == 0) {
            std::fprintf(stderr,
                         "photopipeline: dev harness not built (rebuild with -DPP_BUILD_DEV=ON)\n");
            return 2;
        }
    }
#endif

    // §4.3 冻结启动顺序：style 尝试 → data_dir → load_settings → 日志级别 → log_init →
    //                    MainWindow(settings) → show → exec → log_shutdown
    const auto startup_t0 = std::chrono::steady_clock::now();
    const auto startup_ms = [&startup_t0] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - startup_t0)
            .count();
    };
    QApplication app(argc, argv);
    if (std::getenv("PP_STARTUP_TIMING") != nullptr)
        std::fprintf(stderr, "startup main: app=%lldms\n", static_cast<long long>(startup_ms()));
    if (QApplication::setStyle(QStringLiteral("Fluent")) == nullptr) {
        qInfo("PhotoPipeline: style 'Fluent' unavailable; keeping the default style");
    }
    std::unique_ptr<QSplashScreen> splash;
    // 实测：QSplashScreen 首次 show() 在 Windows 上会同步等待 DWM/首次顶层窗初始化
    // （本机 ~1.0s），反而把主窗从 0.61s 拖到 1.6s；默认关闭，仅诊断时 PP_SHOW_SPLASH=1。
    if (std::getenv("PP_SHOW_SPLASH") != nullptr) {
        if (std::getenv("PP_STARTUP_TIMING") != nullptr)
            std::fprintf(stderr, "startup main: splash_pre=%lldms\n",
                         static_cast<long long>(startup_ms()));
        QPixmap splash_pm = startup_splash_pixmap();
        if (std::getenv("PP_STARTUP_TIMING") != nullptr)
            std::fprintf(stderr, "startup main: splash_pixmap=%lldms\n",
                         static_cast<long long>(startup_ms()));
        splash = std::make_unique<QSplashScreen>(splash_pm);
        splash->setAttribute(Qt::WA_ShowWithoutActivating, true);
        if (std::getenv("PP_STARTUP_TIMING") != nullptr)
            std::fprintf(stderr, "startup main: splash_ctor=%lldms\n",
                         static_cast<long long>(startup_ms()));
        splash->show();
        if (std::getenv("PP_STARTUP_TIMING") != nullptr)
            std::fprintf(stderr, "startup main: splash_show=%lldms\n",
                         static_cast<long long>(startup_ms()));
        if (std::getenv("PP_SPLASH_NO_PUMP") == nullptr)
            QApplication::processEvents(); // 先把 splash 画出来，再继续构建主窗
    }
    if (std::getenv("PP_STARTUP_TIMING") != nullptr)
        std::fprintf(stderr, "startup main: splash=%lldms\n", static_cast<long long>(startup_ms()));
    pp::platform::data_dir(); // 确保便携/回退目录存在（结果缓存）
    pp::AppSettings settings = pp::load_settings(pp::platform::settings_file());
    pp::LogLevel level = pp::LogLevel::Info;
    // R-157（M7-WP8）：文本档解析单源 = pp::log_level_from_text（旧本文件两份逐字重复已删；
    // UI 侧第三份的替换归 WP9，见 handoff「跨界需求」）。
    if (!pp::log_level_from_text(settings.log_level, level))
        level = pp::LogLevel::Info;
    // M2-T3 §2.3：环境变量在启动期一次性覆盖 settings.log_level（非法值 → stderr 一行提示，
    // 沿用 settings 值；未设置 → 与 M1b 行为完全一致）。GUI 与 --dev/--ui-smoke 同一入口。
    level = pp::level_from_env_or(level);
    pp::log_init(pp::platform::logs_dir(), level);
    if (std::getenv("PP_STARTUP_TIMING") != nullptr)
        std::fprintf(stderr, "startup main: settings_log=%lldms\n",
                     static_cast<long long>(startup_ms()));
    pp::ui::MainWindow w(settings);
    if (std::getenv("PP_STARTUP_TIMING") != nullptr)
        std::fprintf(stderr, "startup main: mainwindow=%lldms\n",
                     static_cast<long long>(startup_ms()));
    w.show();
    if (splash)
        splash->finish(&w);
    if (std::getenv("PP_STARTUP_TIMING") != nullptr)
        std::fprintf(stderr, "startup main: shown=%lldms\n", static_cast<long long>(startup_ms()));
    // M3-D6: Mica + 深色标题栏（非 Windows 平台在 mica.cpp 内为空操作）。
    // M4-W2-fix 第 11 条 / M5-T11（A28）：模式口径 = **与 GUI 同源**的
    // theme::preferred_theme_mode()，三档顺序 = **强制档 > 持久化档 > 跟随系统** —— 强制档 =
    // PP_UI_THEME=dark|light 环境变量（双主题截图走查 + 启动期 DWM 对齐用，优先级最高）；
    // 持久化档 = AppSettings.ui_theme_mode（A28，设置·通用三档的跨进程落地，注入点在上一行
    // MainWindow 构造期的 apply_startup_theme()）；两者皆无 → 跟随系统色。此前这里直接读
    // styleHints()->colorScheme()，故 PP_UI_THEME=light 强制档下窗口边框/DWM 属性仍按系统色
    // （启动色滞后）；运行期主题切换由 MainWindow::Impl::refresh_theme()/Show 事件重放。
    pp::platform::apply_window_backdrop(reinterpret_cast<void *>(w.winId()),
                                        pp::ui::theme::preferred_theme_mode() ==
                                            pp::ui::theme::ThemeMode::Dark);
    const int code = app.exec();
    pp::log_shutdown();
    return code;
}
