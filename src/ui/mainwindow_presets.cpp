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
#include <QInputDialog> // M10-W3-T2：格式预设「另存…」命名
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
#include <mutex> // M10-W3-T5：内置预设 seed 的进程内一次性执行（std::once_flag）
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "codecs/heif_profile.h" // M10-W3-T2：保留键 __heif_profile（格式预设 reserved 字段单源）
#include "core/fsops.h"          // R-006/R-073：补齐预检的路径渲染 + 输出根试建/试写探针
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

// 基类型显式 uint8_t（三值枚举；performance-enum-size，值域/语义零变化）。
enum class PresetOp : std::uint8_t { Load, Save, Delete };
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
namespace {

// M12（需求 1/2）：工厂（出厂）格式预设的幂等 seed —— 进程内只跑一次（构造期 refresh_preset_card
// 首调即落地）。M12 起出厂预设不再是 C++ 硬编码库/只读模板，单一真源 = 随发行包分发的仓库 JSON
// 数据文件（`<exe 目录>/presets/formats/**`）：seed 把它们按**原字节**复制到
// `<data_dir>/presets/formats/`，并迁移删除 0.4.0.alpha1 的旧版只读内置模板（`_builtin-*.json`）。
// 失败只记日志（启动期不弹对话框、不阻塞 UI，也绝不覆盖用户改过的文件）。
// 自由函数（不改 mainwindow_impl.h 的声明面 = 本任务写作用域外）。
void seed_factory_format_presets_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        const pp::ui::FormatPresetSeedReport report = pp::ui::seed_factory_format_presets();
        for (const std::string &err : report.errors)
            pp::log_warn("preset", "mainwindow_presets.cpp", "factory format preset seed failed",
                         {{"error", err}});
        pp::log_info("preset", "mainwindow_presets.cpp", "factory format presets seed",
                     {{"written", std::to_string(report.written)},
                      {"skipped", std::to_string(report.skipped)},
                      {"removed_legacy", std::to_string(report.removed_legacy)},
                      {"failed", std::to_string(report.failed)}});
    });
}

} // namespace

void MainWindow::Impl::refresh_preset_card() {
    if (page_output == nullptr)
        return;
    // M12（需求 1/2）：工厂（出厂）格式预设 seed（幂等；本进程首次调用时执行，见上）
    seed_factory_format_presets_once();
    // M10-W3-T2：格式预设行的接线（一次性；构造期 refresh_preset_card 首调即接上 —— 本任务
    // 的写作用域不含 mainwindow.cpp 的 wire 段，接线点因此落在本 TU 的构造期入口上）。
    wire_format_preset_card();
    std::vector<std::pair<QString, QString>> items;
    for (const auto &[path, name] : pp::ui::list_presets(pp::platform::presets_dir()))
        items.emplace_back(QString::fromStdString(path.string()), QString::fromStdString(name));
    page_output->set_preset_list(items);
    page_output->set_current_preset(last_preset_path);
    // M10-W3-T5：seed 之后立即刷新现役格式的格式预设行 —— 首启时该行的列表请求可能发生在
    // seed/接线之前（页签构造期），不刷新会"内置预设文件已在盘上、下拉里却看不到"。
    refresh_format_preset_row(page_output->current_format(), page_output);
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
    // WP5 跨界 2（M7-R2D1）：`presets_dir() / name` 在空 data_dir 时退化成 **CWD 相对名**
    // （operator/ 对空路径只留尾段）→ 预设会被写到进程工作目录、后续删除按相对名误删 CWD 文件。
    // paths 层已把空目录收成空路径（path_under），这里补**调用方**守卫：目录不可用 = 显式报错。
    const std::filesystem::path dir = pp::platform::presets_dir();
    if (dir.empty()) {
        warn_preset_op(w, PresetOp::Save, T("预设目录不可用（数据目录为空）"));
        return;
    }
    const std::filesystem::path path = dir / (name.toStdString() + ".json");
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
    // WP5 跨界 2（M7-R2D1）：删除按**绝对**路径执行 —— 空 presets_dir 场景下拼出的 CWD 相对名
    // 不得成为删除目标（QFile::remove("x.json") 会删掉工作目录里的同名文件）。
    const QFileInfo info(path);
    if (info.isRelative()) {
        warn_preset_op(w, PresetOp::Delete, T("拒绝删除相对路径预设（%1）").arg(path));
        return;
    }
    const QString name = info.completeBaseName();
    const QMessageBox::StandardButton answer =
        QMessageBox::question(w, T("删除预设"), T("确定删除预设“%1”？").arg(name),
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

// ===========================================================================
// M10-W3-T2（C23 / 设计 §8.5）：格式预设（每格式页签顶部的「格式预设」行）
//   分工：页面（ui/page_output.*）只做「列表喂入 / 载入落卡 / 只读自适应面 / 注释位」并发表
//   意图信号；**文件 I/O、校验链（§8.4）、归一、二次确认、绝对路径守卫全在本 TU** —— 与全局
//   预设卡（refresh_preset_card/load_preset_file/…）同一「单一持有者」口径。
//   与全局预设**无隐式叠加**：格式预设只写当前格式卡；全局预设载入覆盖全卡（会话态保留）。
// ===========================================================================
namespace {

// 格式预设操作类型 → 中文对话框标题（与 PresetOp 同款；错误文案一字不改地透出 I/O 层）。
enum class FormatPresetOp : std::uint8_t { Load, Save, Delete, Import, Export };
const char *format_preset_op_title(FormatPresetOp op) {
    switch (op) {
    case FormatPresetOp::Load:
        return "载入格式预设失败";
    case FormatPresetOp::Save:
        return "保存格式预设失败";
    case FormatPresetOp::Delete:
        return "删除格式预设失败";
    case FormatPresetOp::Import:
        return "导入格式预设失败";
    case FormatPresetOp::Export:
        return "导出格式预设失败";
    }
    return "格式预设操作失败";
}

void warn_format_preset(QWidget *parent, FormatPresetOp op, const QString &detail) {
    QMessageBox::warning(parent, T(format_preset_op_title(op)), detail);
}

// 目标文件已存在 → 二次确认（同名二次确认：另存/导入/导出三条"写新名字"的路径都必须过）。
bool confirm_overwrite(QWidget *parent, const QString &name) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parent, T("覆盖格式预设"), T("格式预设“%1”已存在，覆盖它？").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

// 删除二次确认（与全局预设卡同一先例；删除是破坏性动作 → 默认按钮 = No）。
bool confirm_delete(QWidget *parent, const QString &name) {
    const QMessageBox::StandardButton answer =
        QMessageBox::question(parent, T("删除格式预设"), T("确定删除格式预设“%1”？").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

// M12（需求 4）：QString 路径 → 出厂默认预设判定（单源 = core/format_preset.h 的
// is_default_format_preset_name；用**文件名 stem** 判定 —— 与 UI 高亮/删除保护同源）。
// 独立小函数而非在信号 lambda 里直接构造临时 filesystem::path：后者在本 TU（MSVC 19.51 /GL）
// 触发过 C1001 内部编译器错误（实测，2026-11-01；改写形态后编译通过，语义不变）。
QString path_stem(const QString &path) {
    return path.isEmpty() ? QString() : QFileInfo(path).completeBaseName();
}

bool is_default_preset_qpath(const QString &path) {
    return !path.isEmpty() && pp::is_default_format_preset_name(path_stem(path).toStdString());
}

// M12（需求 4）：出厂默认预设的不可删除说明（UI 守卫与「删除」处理器共用同一句中文）。
QString default_preset_protected_text() {
    return T("出厂默认预设不可删除：可直接「保存」修改它，或用「另存」复制一份");
}

} // namespace

// 一次性接线（构造期 refresh_preset_card 首调；幂等 —— 重复调用 no-op）。
void MainWindow::Impl::wire_format_preset_card() {
    if (format_preset_wired || page_output == nullptr)
        return;
    format_preset_wired = true;
    QObject::connect(
        page_output, &PageOutput::format_preset_list_requested, w,
        [this](const QString &format_id) { refresh_format_preset_row(format_id, page_output); });
    QObject::connect(
        page_output, &PageOutput::format_preset_load_requested, w, [this](const QString &path) {
            const std::string err = format_preset_load_into(path, page_output);
            if (err.empty()) {
                refresh_format_preset_row(page_output->current_format(), page_output);
                return;
            }
            // 损坏/版本/kind 不符/跨格式 → **显式中文错误**（不静默套默认），
            // 卡值与列表高亮都保持原状（设计 §8.3「选中后显式报错并保留当前卡值」）。
            warn_format_preset(w, FormatPresetOp::Load, QString::fromStdString(err));
            const QString fmt = page_output->current_format();
            page_output->set_current_format_preset(fmt, page_output->current_format_preset(fmt));
        });
    QObject::connect(
        page_output, &PageOutput::format_preset_save_requested, w, [this](const QString &path) {
            if (page_output == nullptr)
                return;
            const QString fmt = page_output->current_format();
            // M12（需求 4）：「保存」恒可用 —— 选中 `default` 时 **直接覆盖修改该格式的
            // default.json**（用户口径：出厂预设可保存修改；不可删除才由 UI/ I/O 守卫）。
            const QString name = QFileInfo(path).completeBaseName();
            std::filesystem::path saved;
            const std::string err =
                format_preset_save_card(page_output, name, pp::ui::format_presets_root(), &saved);
            if (!err.empty()) {
                warn_format_preset(w, FormatPresetOp::Save, QString::fromStdString(err));
                return;
            }
            refresh_format_preset_row(fmt, page_output);
        });
    QObject::connect(page_output, &PageOutput::format_preset_save_as_requested, w,
                     [this] { format_preset_save_as(page_output); });
    QObject::connect(page_output, &PageOutput::format_preset_delete_requested, w,
                     [this](const QString &path) { format_preset_delete_file(path); });
    QObject::connect(page_output, &PageOutput::format_preset_import_requested, w,
                     [this] { format_preset_import_dialog(page_output); });
    QObject::connect(
        page_output, &PageOutput::format_preset_export_requested, w,
        [this](const QString &path) { format_preset_export_dialog(page_output, path); });
}

// 列表喂入：当前格式目录下的 *.json（坏文件仍列出，显示名后缀「（损坏）」——设计 §8.3；
// 选中后由 format_preset_load_into 给出完整中文原因，绝不静默套默认值）。
// M12（需求 4 / 简报 §4.4-3，硬要求）：喂列表后，若该格式**还没有已载入的来源**（首启 / 新建
// 格式卡 / 重新勾选 / 来源文件被删），静默载入该格式的 `default` 预设（无 default 则首项）——
// 让「无未选择态」在 UI 上成立且自适应摘要始终按 default 策略显示。
//   * 静默：失败只 log_warn + 注释位中文错误，**不弹 modal**（启动期/自动路径不得阻塞用户）；
//   * 不递归：format_preset_load_into 不回调 refresh（只有用户下拉/保存等显式路径才刷新）；
//   * 全局预设接管态（detached）⇒ 抑制（否则会把全局预设值冲掉，M12 Lead 裁定）。
void MainWindow::Impl::refresh_format_preset_row(const QString &format_id, PageOutput *target,
                                                 const std::filesystem::path &presets_root) {
    if (target == nullptr)
        return;
    const QString fmt = format_id.isEmpty() ? target->current_format() : format_id;
    const std::filesystem::path base =
        presets_root.empty() ? pp::ui::format_presets_root() : presets_root;
    std::vector<std::pair<QString, QString>> items;
    if (!fmt.isEmpty() && !base.empty()) {
        for (const pp::ui::FormatPresetEntry &entry :
             pp::ui::list_format_presets_in(base / fmt.toStdString())) {
            QString display = QString::fromStdString(entry.name);
            if (entry.broken)
                display += T("（损坏）");
            items.emplace_back(QString::fromStdString(entry.path.string()), display);
        }
    }
    target->set_format_preset_list(fmt, items);
    // ---- M12：自动选中并载入该格式的 default（无 default 的异常数据目录取首项）----
    if (fmt.isEmpty() || items.empty() || !target->current_format_preset(fmt).isEmpty() ||
        target->format_preset_detached(fmt))
        return;
    QString auto_path;
    for (const auto &[path, name] : items) {
        if (is_default_preset_qpath(path)) {
            auto_path = path;
            break;
        }
    }
    if (auto_path.isEmpty())
        auto_path = items.front().first; // 异常数据目录（无 default）：载入首项
    const std::string err = format_preset_load_into(auto_path, target);
    if (err.empty())
        return;
    // 失败：静默（不弹框）+ 注释位中文错误 + 记日志；会话态不指向坏文件（下拉由列表规则高亮
    // default，按钮按既有禁用语义），用户仍可手动选其它预设或修复文件。
    pp::log_warn(
        "preset", "mainwindow_presets.cpp", "auto load default format preset failed",
        {{"format", fmt.toStdString()}, {"path", auto_path.toStdString()}, {"error", err}});
    target->set_format_preset_note(fmt, T("默认预设载入失败：%1").arg(QString::fromStdString(err)));
    target->set_current_format_preset(fmt, QString());
}

// 采集：当前格式卡快照（后端/技术/位深/核心+高级参数/保留键 __heif_profile）+ 自适应面。
// 卡值真源 = PageOutput::config_base()（与 collect_preset / 运行配置同一条 spec_of 链）。
pp::FormatPresetData MainWindow::Impl::format_preset_collect(PageOutput *target,
                                                             const QString &name) const {
    pp::FormatPresetData preset;
    preset.name = name.toStdString();
    if (target == nullptr)
        return preset;
    const QString fmt = target->current_format();
    preset.format_id = fmt.toStdString();
    if (fmt.isEmpty())
        return preset;
    const pp::RunConfig cfg = target->config_base();
    for (const pp::OutputFormatSpec &spec : cfg.outputs) {
        if (spec.format_id != preset.format_id)
            continue;
        preset.backend_id = spec.backend_id;
        preset.tech_id = spec.tech_id;
        preset.out_bitdepth = spec.out_bitdepth;
        preset.params = spec.params; // `__` 保留键由 IO 层忽略（heif_profile 走 reserved）
        const auto it = spec.params.find(std::string(pp::kHeifProfileParamKey));
        if (it != spec.params.end()) {
            if (const int64_t *value = std::get_if<int64_t>(&it->second))
                preset.heif_profile = *value;
        }
        break;
    }
    preset.adaptive_policy = target->format_preset_adaptive_policy(fmt).toStdString();
    preset.adaptive_enabled = target->format_preset_adaptive_enabled(fmt);
    return preset;
}

// 保存：先归一（补 backend/tech/位深/默认键）再落盘；I/O 层同一套校验，**不过不写盘**。
std::string MainWindow::Impl::format_preset_save_card(PageOutput *target, const QString &name,
                                                      const std::filesystem::path &presets_root,
                                                      std::filesystem::path *saved) {
    if (target == nullptr)
        return "输出页不可用：无法采集格式卡快照";
    const QString fmt = target->current_format();
    if (fmt.isEmpty())
        return "未选择输出格式：无可保存的格式预设";
    pp::FormatPresetData preset = format_preset_collect(target, name);
    // §8.4 第 3 步：normalize（backend/tech 空 → 首选；位深 0 → 默认；缺失键补默认 + 锁）
    std::string nerr;
    (void)pp::normalize_format_preset(preset, &nerr);
    if (!nerr.empty())
        return nerr;
    // §8.4 第 4/5 步：validate（逐键/未知键/跨技术残留键/heif_profile/自适应策略）
    std::string verr = pp::validate_format_preset(preset);
    if (!verr.empty())
        return verr;
    const std::filesystem::path root_dir =
        presets_root.empty() ? pp::ui::format_presets_root() : presets_root;
    if (root_dir.empty())
        return "格式预设目录不可用（数据目录为空）";
    std::filesystem::path saved_path;
    std::string err = pp::ui::save_format_preset_to(root_dir, preset, &saved_path);
    if (!err.empty())
        return err;
    if (saved != nullptr)
        *saved = saved_path;
    target->set_current_format_preset(fmt, QString::fromStdString(saved_path.string()));
    target->set_format_preset_note(fmt, T("已保存格式预设：%1").arg(name));
    return {};
}

// 载入：读文件（含 kind/version/字段类型/完整校验）→ 格式匹配 → normalize → 落卡 + 只读
// 自适应面 + 注释位。**任一步失败即返回中文错误且卡值零改动**（不静默套默认）。
std::string MainWindow::Impl::format_preset_load_into(const QString &path, PageOutput *target) {
    if (target == nullptr)
        return "输出页不可用：无法载入格式预设";
    if (path.isEmpty())
        return "格式预设路径为空";
    const QString fmt = target->current_format();
    if (fmt.isEmpty())
        return "未选择输出格式：无法载入格式预设";
    pp::FormatPresetData preset;
    std::string lerr =
        pp::ui::load_format_preset(std::filesystem::path(path.toStdString()), preset);
    if (!lerr.empty())
        return lerr; // 损坏 JSON / kind 不符 / 版本过新 / 逐键校验失败 —— 显式中文错误
    std::string err = pp::check_format_preset_applies_to(preset, fmt.toStdString());
    if (!err.empty())
        return err; // 跨格式套用 / 后端·技术·位深不可用
    std::string nerr;
    const std::vector<std::string> touched = pp::normalize_format_preset(preset, &nerr);
    if (!nerr.empty())
        return nerr;
    // 归一后按**最终值**再走一遍校验（设计 §8.4：保存/载入/开始运行同一套链）
    std::string verr = pp::validate_format_preset(preset);
    if (!verr.empty())
        return verr;
    // 「已补齐 N 个默认项」只数**用户可见**的补齐项：`__` 前缀是内部管道键（如 __lossless，
    // 每次归一都会被补上且不落盘），把它计进 N 会让每份预设都恒报「已补齐 1 项」= 假读数。
    int filled_defaults = 0;
    for (const std::string &key : touched) {
        if (key.rfind("__", 0) != 0)
            ++filled_defaults;
    }
    pp::OutputFormatSpec spec;
    spec.format_id = preset.format_id;
    spec.backend_id = preset.backend_id;
    spec.tech_id = preset.tech_id;
    spec.out_bitdepth = preset.out_bitdepth;
    spec.params = preset.params;
    // 内部管道键：预设未显式带 lossless 参数时，用同一判据还原无损语义（params.h 单源）
    spec.params[std::string(pp::kLosslessKey)] = pp::lossless_flag(preset.params);
    if (preset.heif_profile.has_value())
        spec.params[std::string(pp::kHeifProfileParamKey)] = *preset.heif_profile;
    // M10 / W3-T5（A20）：自适应开关/策略随落卡下发（`apply_state_to_form` 恢复会话态；
    // 下面 set_format_preset_adaptive 再显式同步一次只读面/开关，两条路径同一结果）。
    spec.adaptive_enabled = preset.adaptive_enabled;
    spec.adaptive_policy = preset.adaptive_policy;
    // M12（需求 4）：成功载入 = 卡值重新有了文件来源 ⇒ 解除「全局预设接管」态（save 恢复可用）。
    // 必须在 set_current_format_preset 之前清（按钮态刷新读的是新标志）。
    target->set_format_preset_detached(fmt, false);
    target->apply_format_preset(spec);
    target->set_current_format_preset(fmt, path);
    target->set_format_preset_adaptive(fmt, QString::fromStdString(preset.adaptive_policy),
                                       preset.adaptive_enabled);
    // 注释位：normalize 补了默认项时提示「已载入（已补齐 N 个默认项）」（设计 §8.5），
    // 否则标出来源（「来自格式预设：X」）。
    const QString name = QFileInfo(path).completeBaseName();
    target->set_format_preset_note(
        fmt, filled_defaults == 0
                 ? T("来自格式预设：%1").arg(name)
                 : T("已载入（已补齐 %1 个默认项）：%2").arg(filled_defaults).arg(name));
    return {};
}

// 另存…：命名（默认 = 当前预设名/格式名）→ 同名二次确认 → 写入预设目录。
void MainWindow::Impl::format_preset_save_as(PageOutput *target) {
    if (target == nullptr)
        return;
    const QString fmt = target->current_format();
    if (fmt.isEmpty()) {
        warn_format_preset(w, FormatPresetOp::Save, T("未选择输出格式：无可保存的格式预设"));
        return;
    }
    const std::filesystem::path presets_dir_root = pp::ui::format_presets_root();
    if (presets_dir_root.empty()) {
        warn_format_preset(w, FormatPresetOp::Save, T("格式预设目录不可用（数据目录为空）"));
        return;
    }
    const QString current = target->current_format_preset(fmt);
    QString suggestion =
        current.isEmpty() ? T("%1 预设").arg(fmt) : QFileInfo(current).completeBaseName();
    // M12（需求 4 / Lead 裁定）：「另存」= 复制。当前预设是 `default` 时建议名用
    // 「default 副本」—— 不沿用 default 本身（否则命名对话框里一次回车 = 覆盖出厂默认文件）。
    if (is_default_preset_qpath(current))
        suggestion += T(" 副本");
    bool ok = false;
    const QString name = QInputDialog::getText(w, T("另存格式预设"), T("格式预设名称（= 文件名）"),
                                               QLineEdit::Normal, suggestion, &ok);
    if (!ok)
        return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        warn_format_preset(w, FormatPresetOp::Save, T("格式预设名不能为空"));
        return;
    }
    const std::filesystem::path existing =
        presets_dir_root / fmt.toStdString() / (trimmed.toStdString() + ".json");
    std::error_code ec;
    if (std::filesystem::exists(existing, ec) && !confirm_overwrite(w, trimmed))
        return;
    std::filesystem::path saved;
    const std::string err = format_preset_save_card(target, trimmed, presets_dir_root, &saved);
    if (!err.empty()) {
        warn_format_preset(w, FormatPresetOp::Save, QString::fromStdString(err));
        return;
    }
    refresh_format_preset_row(fmt, target);
}

// 删除：绝对路径守卫 + 二次确认（与全局预设 delete_preset_file 同一先例）。
// M12（需求 4）：出厂默认预设 `default` **不可删除**（按钮已置灰；本守卫是防御面，避免任何
// 调用方绕过 UI 走到模态对话框）—— 但可「保存」直接修改它、可「另存」复制一份。
void MainWindow::Impl::format_preset_delete_file(const QString &path) {
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    if (is_default_preset_qpath(path)) {
        warn_format_preset(w, FormatPresetOp::Delete, default_preset_protected_text());
        return;
    }
    if (info.isRelative()) {
        warn_format_preset(w, FormatPresetOp::Delete,
                           T("拒绝删除相对路径的格式预设（%1）").arg(path));
        return;
    }
    if (!info.exists()) {
        warn_format_preset(w, FormatPresetOp::Delete, T("格式预设文件不存在（%1）").arg(path));
        return;
    }
    if (!confirm_delete(w, info.completeBaseName()))
        return;
    const std::string err = pp::ui::delete_format_preset(std::filesystem::path(path.toStdString()));
    if (!err.empty()) {
        warn_format_preset(w, FormatPresetOp::Delete, QString::fromStdString(err));
        return;
    }
    if (page_output != nullptr) {
        const QString fmt = page_output->current_format();
        if (page_output->current_format_preset(fmt) == path)
            page_output->set_current_format_preset(fmt, QString());
        // M12（需求 4 / 简报 §4.4-6）：删除当前预设后**自愈回 default 并载入**，不留空态 ——
        // refresh_format_preset_row 在会话来源为空时自动选中并静默载入 default（其自适应策略
        // 随载入刷新，不再残留被删来源的旧策略会话态 = 旧 S-4/S-5 分支的职责）。
        refresh_format_preset_row(fmt, page_output);
    }
}

// 导入…：文件对话框 → 校验 → 复制进 `<data_dir>/presets/formats/<format>/`（同名二次确认）。
void MainWindow::Impl::format_preset_import_dialog(PageOutput *target) {
    Q_UNUSED(target);
    const QString file = QFileDialog::getOpenFileName(w, T("导入格式预设"), QString(),
                                                      T("格式预设 (*.json);;所有文件 (*)"));
    if (file.isEmpty())
        return;
    const QString name = QFileInfo(file).completeBaseName();
    // M12（需求 4）：`default` 是出厂默认预设名 —— 导入不得顶替它（否则等于绕过「保存」语义
    // 覆盖出厂默认文件；要改 default 请选中它后用「保存」）。显式中文拒绝，不静默改名。
    if (is_default_preset_qpath(file)) {
        warn_format_preset(
            w, FormatPresetOp::Import,
            T("「default」是出厂默认预设名：请把文件改名后再导入（要修改出厂默认预设，"
              "请先选中它再用「保存」）"));
        return;
    }
    std::filesystem::path saved;
    // 先只读取校验一次（拿到 format_id 才能判断同名目标），失败即报错、不写盘
    pp::FormatPresetData preview;
    const std::string lerr =
        pp::ui::load_format_preset(std::filesystem::path(file.toStdString()), preview);
    if (!lerr.empty()) {
        warn_format_preset(w, FormatPresetOp::Import, QString::fromStdString(lerr));
        return;
    }
    const std::filesystem::path presets_dir_root = pp::ui::format_presets_root();
    if (presets_dir_root.empty()) {
        warn_format_preset(w, FormatPresetOp::Import, T("格式预设目录不可用（数据目录为空）"));
        return;
    }
    std::error_code ec;
    const std::filesystem::path dest =
        presets_dir_root / preview.format_id / (name.toStdString() + ".json");
    if (std::filesystem::exists(dest, ec) && !confirm_overwrite(w, name))
        return;
    const std::string err = pp::ui::import_format_preset(std::filesystem::path(file.toStdString()),
                                                         name.toStdString(), &saved);
    if (!err.empty()) {
        warn_format_preset(w, FormatPresetOp::Import, QString::fromStdString(err));
        return;
    }
    refresh_format_preset_row(QString::fromStdString(saved.parent_path().filename().string()),
                              page_output);
}

// 导出…：当前格式卡快照 → 用户在文件对话框里选的路径（校验 + 同名二次确认）。
void MainWindow::Impl::format_preset_export_dialog(PageOutput *target, const QString &preset_path) {
    if (target == nullptr)
        return;
    const QString fmt = target->current_format();
    if (fmt.isEmpty()) {
        warn_format_preset(w, FormatPresetOp::Export, T("未选择输出格式：无可导出的格式预设"));
        return;
    }
    QString suggestion =
        preset_path.isEmpty() ? T("%1 预设").arg(fmt) : QFileInfo(preset_path).completeBaseName();
    suggestion = QDir::home().filePath(suggestion + QStringLiteral(".json"));
    const QString dest =
        QFileDialog::getSaveFileName(w, T("导出格式预设"), suggestion, T("格式预设 (*.json)"));
    if (dest.isEmpty())
        return;
    const QFileInfo dest_info(dest);
    if (dest_info.exists() && !confirm_overwrite(w, dest_info.completeBaseName()))
        return;
    pp::FormatPresetData preset = format_preset_collect(target, dest_info.completeBaseName());
    std::string nerr;
    (void)pp::normalize_format_preset(preset, &nerr);
    if (!nerr.empty()) {
        warn_format_preset(w, FormatPresetOp::Export, QString::fromStdString(nerr));
        return;
    }
    const std::string err =
        pp::ui::export_format_preset(preset, std::filesystem::path(dest.toStdString()));
    if (!err.empty()) {
        warn_format_preset(w, FormatPresetOp::Export, QString::fromStdString(err));
        return;
    }
    target->set_format_preset_note(fmt, T("已导出格式预设：%1").arg(dest_info.completeBaseName()));
}

} // namespace pp::ui
