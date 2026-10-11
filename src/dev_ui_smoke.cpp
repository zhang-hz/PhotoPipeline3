// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M7-WP8 / B.6（R-193/323）：--ui-smoke 走查 harness（自 main.cpp 拆出）。
//
// Contract: docs/v0.3.0-design.md §4.3（--ui-smoke 冻结面：参数在 QApplication 之前解析、
// 冻结截图集与末行）；docs/deep-review-0.3.2.md 附录 B.6（拆四 TU）。行为零变化：
// 冻结行 `UI-SMOKE OK shots=12 pages=3` 与 12 张截图逐字节口径不动。
#include <QApplication>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "core/logger.h"
#include "platform/paths.h"
#include "ui/mainwindow.h"

namespace fs = std::filesystem;

namespace pp::dev {

namespace {

const char *kUiSmokeUsage =
    "usage: photopipeline --ui-smoke [--inputs DIR] [--shots DIR]\n"
    "  --inputs DIR   input directory (default <repo>/tests/golden/base)\n"
    "  --shots DIR    screenshot directory (default .cache/ui-review; empty = do not save)\n"
    "exit code 0 = all frozen assertions passed; 1 = smoke failure; 2 = usage/argument error\n";

// 可执行文件目录：复用 pp::platform::executable_dir()（Linux 读 /proc/self/exe，
// Windows 读 GetModuleFileNameW —— M3 单一实现，不再各自探测）；空结果回退 argv[0]。
fs::path executable_directory(const char *argv0) {
    const fs::path exe_dir = pp::platform::executable_dir();
    if (!exe_dir.empty())
        return exe_dir;
    std::error_code ec;
    const fs::path arg = fs::absolute(fs::path(argv0 != nullptr ? argv0 : ""), ec);
    return arg.parent_path();
}

// 从可执行文件向上找仓库根：含 .git 或 CMakeLists.txt 的最近目录
std::string find_repo_root(const char *argv0) {
    std::error_code ec;
    fs::path dir = executable_directory(argv0);
    for (int i = 0; i < 8 && !dir.empty(); ++i) {
        if (fs::exists(dir / ".git", ec) || fs::exists(dir / "CMakeLists.txt", ec)) {
            return dir.string();
        }
        const fs::path up = dir.parent_path();
        if (up == dir)
            break;
        dir = up;
    }
    return {};
}

} // namespace

int run_ui_smoke(int argc, char **argv) {
    QString inputs;
    QString shots = QStringLiteral(".cache/ui-review");
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--ui-smoke")
            continue;
        if (a == "--inputs" || a == "--shots") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "photopipeline --ui-smoke: %s requires a value\n%s",
                             std::string(a).c_str(), kUiSmokeUsage);
                return 2;
            }
            const QString value = QString::fromLocal8Bit(argv[++i]);
            if (a == "--inputs") {
                inputs = value;
            } else {
                shots = value;
            }
            continue;
        }
        std::fprintf(stderr, "photopipeline --ui-smoke: unknown option '%s'\n%s",
                     std::string(a).c_str(), kUiSmokeUsage);
        return 2;
    }
    if (inputs.isEmpty()) {
        const std::string root = find_repo_root(argv[0]);
        if (root.empty()) {
            std::fprintf(stderr,
                         "photopipeline --ui-smoke: cannot locate the repository root (searched "
                         "upward from the executable directory for .git/CMakeLists.txt); pass "
                         "--inputs DIR\n");
            return 1;
        }
        inputs = QString::fromStdString((fs::path(root) / "tests/golden/base").string());
    }
    if (!QFileInfo::exists(inputs)) {
        std::fprintf(stderr, "photopipeline --ui-smoke: input directory does not exist: %s\n",
                     inputs.toLocal8Bit().constData());
        return 1;
    }

    // 冒烟分支不走磁盘 settings/log：默认 AppSettings；日志自然走 stderr（§4.3）。
    // M2-T3 §2.3：PP_LOG_LEVEL 亦作用于 --ui-smoke——此处只影响 stderr 过滤；未设置时
    // level_from_env_or(Info) 与默认级别相同，输出与现状逐字节一致。
    // M4-T9：PP_UI_NO_STATE = 不读写面板持久化（data_dir()/ui-state.ini）→ 冻结截图与
    // 开发机状态无关（冒烟只跑 offscreen；真实平台渲染走 --shots .cache/ui-review）。
    qputenv("PP_UI_NO_STATE", "1");
    pp::log_set_level(pp::level_from_env_or(pp::LogLevel::Info));

    QApplication app(argc, argv);
    pp::ui::MainWindow w(pp::AppSettings{});
    w.resize(1440, 900);
    w.show();
    w.set_offline_maps(true);
    w.add_paths(QStringList{inputs});
    w.ui_smoke_walk(shots); // walk 内部自跑事件循环并写 pp_ui_smoke_exit

    const QVariant ran = w.property("pp_ui_smoke_ran");
    if (!ran.isValid() || !ran.toBool()) {
        std::fprintf(stderr, "photopipeline --ui-smoke: walk did not run\n");
        return 1;
    }
    return w.property("pp_ui_smoke_exit").toInt();
}

} // namespace pp::dev
