// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — mainwindow_presets.cpp（M7-WP9 / R-049·R-252-260 按数据所有权拆 TU）。
//
// 域：预设卡 CRUD（载入/另存/删除/刷新 + 预设对话框动作分派）。
// 数据所有权 = last_preset_path 与 presets_dir() 落盘；页面只发信号、本 TU 单一持有者。
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

enum class PresetOp { Load, Save, Delete };
void warn_preset_op(QWidget *parent, PresetOp op, const QString &detail) {
    const char *title = op == PresetOp::Load   ? "载入预设失败"
                        : op == PresetOp::Save ? "保存预设失败"
                                               : "删除预设失败";
    QMessageBox::warning(parent, T(title), detail);
}

} // namespace

// M4-T13：预设卡的三条动作（页面信号 → 此处单一持有者）+ 列表刷新。
// 与 manage_presets() 共用同一批实现：载入（含 v1 迁移与显式报错）/
// 保存（<presets_dir>/<name>.json） / 删除（二次确认）。
void MainWindow::Impl::refresh_preset_card() {
    if (page_output == nullptr)
        return;
    std::vector<std::pair<QString, QString>> items;
    for (const auto &[path, name] : pp::ui::list_presets(pp::platform::presets_dir()))
        items.emplace_back(QString::fromStdString(path.string()), QString::fromStdString(name));
    page_output->set_preset_list(items);
    page_output->set_current_preset(last_preset_path);
}

void MainWindow::Impl::load_preset_file(const QString &path) {
    if (path.isEmpty())
        return;
    pp::PresetData preset;
    const std::string err = pp::ui::load_preset(path.toStdString(), preset);
    if (!err.empty()) {
        // R31：加载失败**显式报错**（不静默回退到默认值）
        warn_preset_op(w, PresetOp::Load, QString::fromStdString(err));
        return;
    }
    pp::normalize_preset(preset);
    // T13 复核项 2：**能载入但校验不过**的预设（非法模板 / 未知格式 / 越界参数 / v1 迁移出的
    // tech·lossless 矛盾）同样必须显式报错 —— 否则 apply_preset 内部静默 return，用户点预设
    // 表现为"无反应"（presets.h「预设里带非法模板 = 预设无效，不得静默通过」）。
    const std::string verr = pp::validate_preset(preset);
    if (!verr.empty()) {
        warn_preset_op(w, PresetOp::Load, QString::fromStdString(verr));
        return;
    }
    page_output->apply_preset(preset);
    page_meta->apply_rules(preset.rules);
    last_preset_path = path;
    refresh_preset_card();
    w->refresh_status();
}

void MainWindow::Impl::save_preset_named(const QString &name) {
    if (name.isEmpty())
        return;
    pp::PresetData preset = page_output->collect_preset(name);
    preset.rules = page_meta->rules(); // rules 归 PageMeta（MainWindow 合并）
    // T13 复核项 2：保存路径同样先校验（模板框允许任意文本，非法模板不得静默落盘 ——
    // 写出的预设下次载入即被拒，属"先污染后报错"）。校验不过 → 提示并**不写文件**。
    const std::string verr = pp::validate_preset(preset);
    if (!verr.empty()) {
        warn_preset_op(w, PresetOp::Save, QString::fromStdString(verr));
        return;
    }
    const std::filesystem::path path = pp::platform::presets_dir() / (name.toStdString() + ".json");
    const std::string err = pp::ui::save_preset(path, preset);
    if (!err.empty()) {
        warn_preset_op(w, PresetOp::Save, QString::fromStdString(err));
        return;
    }
    last_preset_path = QString::fromStdString(path.string());
    refresh_preset_card();
}

void MainWindow::Impl::delete_preset_file(const QString &path) {
    if (path.isEmpty())
        return;
    const QString name = QFileInfo(path).completeBaseName();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        w, MainWindow::tr("删除预设"), MainWindow::tr("确定删除预设“%1”？").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    if (!QFile::remove(path)) {
        warn_preset_op(w, PresetOp::Delete, path);
        return;
    }
    if (last_preset_path == path)
        last_preset_path.clear();
    refresh_preset_card();
}

void MainWindow::manage_presets() {
    Impl &d = *impl_;
    if (d.running)
        return;
    const std::filesystem::path dir = pp::platform::presets_dir();
    std::vector<std::pair<QString, QString>> items;
    for (const auto &[path, name] : pp::ui::list_presets(dir)) {
        items.emplace_back(QString::fromStdString(path.string()), QString::fromStdString(name));
    }
    PresetsDialog dlg(
        items,
        d.last_preset_path.isEmpty() ? QString() : QFileInfo(d.last_preset_path).completeBaseName(),
        this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    switch (dlg.action()) {
    case PresetsDialog::Action::Load:
        d.load_preset_file(dlg.path());
        return;
    case PresetsDialog::Action::SaveAs:
        d.save_preset_named(dlg.name()); // U6 已在对话框内清洗
        return;
    case PresetsDialog::Action::Delete:
        d.delete_preset_file(dlg.path());
        return;
    case PresetsDialog::Action::None:
    default:
        return;
    }
}

} // namespace pp::ui
