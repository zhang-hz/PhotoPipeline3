// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — mainwindow_classes.cpp（M7-WP9 / R-049·R-252-260 按数据所有权拆 TU）。
//
// 域：分类注册表 CRUD（classes.json 读写 + 热键打标 + 面板/预览联动）。
// 数据所有权 = Impl::classes 注册表与 classes_file 路径。
// 拆分口径 = 行为零变化：实现自 mainwindow.cpp 逐字搬入；objectName / 信号 / 方法签名
// 逐位不变（金样 20 对 + UI-SMOKE 冻结行背书，见 build/m7-wp9/handoff.md）。
#include "ui/formkit.h" // M5-T7：meta-geom 探针读回卡头胶囊（formkit::PillBadge）与 R1/R2 几何
#include "ui/icons.h"
#include "ui/mainwindow.h"

#include <QAbstractButton> // M6-T11：图层 Seg 的段（QAbstractButton）驱动回填
#include <QAbstractItemView>
#if QT_CONFIG(accessibility)
#include <QAccessible> // §11/A18：ShellNav 的 tab/button group 语义（自建接口 + 工厂）
#include <QAccessibleActionInterface>
#include <QAccessibleWidget>
#endif
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QClipboard> // UX-1：「复制路径」等菜单动作
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog> // M6-T13：meta-map-expand 探针在模态循环内取证大地图对话框
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics> // M5-T14：offline-banner 环带取样的文字盒（QFontMetricsF 同头）
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget> // M6-T9：meta-loop 探针读结果列表（可点行/占位行）
#include <QMargins>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProgressBar>
#include <QProxyStyle> // M5-T14：文件列表系统焦点框抑制（QTreeView::drawRow 的焦点框调用不带
                       // widget → QSS outline:none 无效；见 FileViewFocusFrameStyle 说明）
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker> // R-153：filter_clear 一次 apply_filter（屏蔽两枚开关的 toggled）
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStorageInfo> // A19（T11）：底栏「剩余磁盘」段的卷可用空间读数
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/fsops.h" // R-006/R-073：补齐预检的路径渲染 + 输出根试建/试写探针
#include "core/logger.h"
#include "core/params.h" // R-006：find_format（补齐预检取目标扩展名）
#include "core/settings.h"
#include "core/version.h" // R-150：版本徽标取 PP_VERSION_STRING（单源 = CMakeLists.txt:19 生成链）
#include "decode/oiio_reader.h" // M4-T12：探针的 probe 摘要（与列表模型同一条 oiio probe 通道）
#include "mapwidget/mapwidget.h"
#include "platform/frameless.h"
#include "platform/mica.h"
#include "platform/paths.h"
#include "ui/classify_panel.h"
#include "ui/contextmenu.h" // UX-1：公共右键菜单件（单一构造点 + 12 类卡 actions）
#include "ui/exif_editor.h"
#include "ui/filelistmodel.h"
#include "ui/page_meta.h"
#include "ui/page_output.h"
#include "ui/page_run.h"
#include "ui/paramform.h"
#include "ui/preset_io.h"
#include "ui/presets_dialog.h"
#include "ui/preview_panel.h"
#include "ui/settings_dialog.h"
#include "ui/theme.h"

#include "ui/mainwindow_impl.h" // Impl/壳层件共享层

namespace pp::ui {
namespace {
using theme::preferred_theme_mode;

} // namespace

void MainWindow::Impl::load_classes() {
    if (classes_file.isEmpty())
        return;
    std::string err;
    if (!classes.load(std::filesystem::path(classes_file.toStdString()), &err)) {
        // 损坏的注册表：**不改动用户文件**、保持默认模板并明说（不静默）。
        // R-278：诊断输出收 logger（原 std::fprintf 绕过日志面）
        pp::log_warn("ui", classes_file.toStdString(), "classes.json load failed: " + err);
    }
}

void MainWindow::Impl::save_classes() {
    if (classes_file.isEmpty())
        return;
    std::string err;
    if (!classes.save(std::filesystem::path(classes_file.toStdString()), &err)) {
        pp::log_warn("ui", classes_file.toStdString(), "classes.json save failed: " + err);
        return;
    }
    // §6.1：预览面板的热键提示表随注册表（增删改名/热键）动态重生成
    if (preview_panel != nullptr)
        preview_panel->set_class_registry(&classes);
}

void MainWindow::Impl::apply_class_to_selection(const QString &class_id) {
    if (running) // G5：分类只读（快捷键已在预览面板层锁掉，这里再兜一层）
        return;
    std::vector<std::size_t> targets;
    if (view->selectionModel() != nullptr) {
        for (const QModelIndex &idx : view->selectionModel()->selectedIndexes()) {
            const int r = proxy->mapToSource(groups->mapToSource(idx)).row();
            if (r < 0 || std::size_t(r) >= model->size())
                continue;
            const std::size_t row = std::size_t(r);
            if (std::find(targets.begin(), targets.end(), row) == targets.end())
                targets.push_back(row);
        }
    }
    if (targets.empty()) {
        // 无选中（或选中被过滤隐藏）→ 打给预览当前项（§6.1 热键打标的"0 交互成本"路径）
        const int cur = preview_panel != nullptr ? preview_panel->current_index() : -1;
        if (cur >= 0 && std::size_t(cur) < model->size())
            targets.push_back(std::size_t(cur));
    }
    if (targets.empty())
        return;
    for (const std::size_t row : targets)
        model->set_class(row, class_id.toStdString()); // 一文件一分类（空 id = 清除归属）
    save_classes(); // 打标即写 registry（一次落盘；逐行写会放大 IO）
    w->refresh_status();
}

// M7-R2D1 / R-143：sync_class_views（选中项 → 分类面板高亮）随 `.on` 拆态为过滤档而移除 ——
// 归属可见面 = 行内分类色点（FileListModel::Qt::DecorationRole），不再有第二条高亮通道。

} // namespace pp::ui
