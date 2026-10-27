// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — mainwindow_status.cpp（M7-WP9 / R-049·R-252-260 按数据所有权拆 TU）。
//
// 域：状态栏表现层（状态词+色调 / 输出摘要或 ETA / 剩余磁盘 / 计数徽标 / CTA 钮态）。
// 数据所有权 = 纯表现：全部读数取自模型与运行账本，不产生业务副作用。
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
#include "ui/byte_text.h" // R-254（M7-R2.1-F3）：字节自然语读数单源（原 natural_bytes）
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

// A19（T11，§3.2 骨架行）：底栏「剩余磁盘」段的自然语读数 —— R-254（M7-R2.1-F3）：
// 原本文件 natural_bytes 与 page_output.cpp 的 human_bytes **逐字同款双实现**已收敛为
// ui/byte_text.h 单源（输出逐字节不变；格式仍被 smoke 探针
// `^\d+(\.\d)? (B|KB|MB|GB|TB|PB)$` 钉住）。fmt 族（run_status_text.h）是另一口径，见该头。

// M4-T14：底栏 ETA 文案（mockup run-dark「ETA 约 00:23」）；秒 → mm:ss
QString eta_text(int seconds) {
    const int total = std::max(0, seconds);
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString conflict_label(pp::ConflictPolicy p) {
    switch (p) {
    case pp::ConflictPolicy::Rename:
        return T("自动加序号");
    case pp::ConflictPolicy::Skip:
        return T("跳过");
    case pp::ConflictPolicy::Overwrite:
        return T("覆盖");
    }
    return T("自动加序号");
}

} // namespace

void MainWindow::refresh_status() {
    // R-253（M7-WP9）分段：原 124 行单函数同管 7 事 → 语义段各归一段（调用顺序与原行序逐字
    // 一致，行为零变化）。段实现见下方 refresh_status_* 五段。

    Impl &d = *impl_;
    const std::size_t count = d.model->size();
    const std::size_t checked = d.model->checked_count();
    // M5-T8：输出页「预设与预估」卡的输入面（预计输出 = 勾选数 × 已选格式数）。数据源 = 文件模型
    // 的勾选数（与底栏「已选 N / M」同一处读数）；set_input_count 不发信号 → 无回环。
    if (d.page_output != nullptr)
        d.page_output->set_input_count(static_cast<int>(checked));
    const std::size_t unsupported = d.model->unsupported_count();
    const std::size_t exceptions = d.model->exception_count();
    const QString reason = d.page_output->ready_to_start();

    // 状态词 + **色调**（§8 四档：中性=空/无事、绿=就绪/完成、琥珀=警告、红=失败；点与词同色；
    // 修 K3/5-13：就绪与空态**绝不用红**，红色只给失败）。文案 = 自然语（§12-17：无公式、
    // 无实现细节词；fix-3-status 的裁定文案）。
    d.refresh_status_summary(count, checked, exceptions, reason);
    d.refresh_status_output_segment(checked);
    // A19（§3.2）：底栏第三段「剩余磁盘」读数（输出根目录 → 源目录）
    d.disk_free_segment();
    d.refresh_status_badges(count, unsupported);
    d.refresh_status_controls(count, checked, reason);
}

// 段1（R-253）：状态词 + 色调（§8 四档）——文案与档位判定单段收拢。
void MainWindow::Impl::refresh_status_summary(std::size_t count, std::size_t checked,
                                              std::size_t exceptions, const QString &reason) {
    Impl &d = *this;
    // UX-2（M7-R2D1）：一次性过滤态提示优先显示（非阻塞；kStatusNoteMs 后回落正常状态行）
    if (!d.status_note.isEmpty()) {
        d.status->set_full_text(d.status_note);
        d.apply_status_tone(theme::kToneNeutral);
        return;
    }
    QString text;
    const char *tone = theme::kToneNeutral;
    if (d.running) {
        // M4-T14（mockup run-dark 底栏）：运行中 = 各终态桶 + 进行中 + 排队。
        // R-250（双账本收敛单源）：全部桶取自 Impl 的**文件级**计数（原与 page_run 的
        // started/terminal 混拼同一文案）；inflight/queued 由本账本自算。
        // R-140：补「跳过 / 取消 / 部分」三桶 + 标注计数单位（与运行页汇总的"按输出"区分）。
        // R-139：终态词统一走 term_words（与列表胶囊/运行页胶囊同一张词表）。
        const std::size_t started = d.run_started;
        const std::size_t inflight = started > d.run_terminal ? started - d.run_terminal : 0;
        const std::size_t queued = d.run_total > started ? d.run_total - started : 0;
        const auto bucket = [](const char *word, std::size_t n) {
            return QStringLiteral("%1 %2")
                .arg(QCoreApplication::translate("MainWindow", word))
                .arg(static_cast<qulonglong>(n));
        };
        text = QStringList{bucket(term_words::kDone, d.run_done),
                           bucket(term_words::kPartial, d.run_partial),
                           bucket(term_words::kFailed, d.run_failed),
                           bucket(term_words::kSkipped, d.run_skipped),
                           bucket(term_words::kCancelled, d.run_cancelled),
                           bucket(term_words::kRunning, inflight),
                           bucket(term_words::kQueued, queued)}
                   .join(QStringLiteral(" · ")) +
               T("（按文件）");
        // 运行中：绿 = 进行中；已出现失败/例外 → 琥珀（预警，不打断 —— mockup run-dark 底栏
        // 的「进行中 · 1 异常 · 3 失败」正是琥珀档）
        tone = (d.run_failed > 0 || exceptions > 0) ? theme::kToneWarn : theme::kToneOk;
    } else if (count == 0) {
        text = T("未添加文件"); // 空态 = 中性（§8：空/无事发生；不得用红）
    } else if (!reason.isEmpty()) {
        // W-R-T13（A39 ⑤）：预检未过（输出页的阻塞原因）= 警告档，不是失败 —— 但**选中数不被
        // 警告吞掉**：有勾选时读数照报（「已选 N / M · 未设置输出根目录」），警告接在其后；
        // 没有勾选时无可报的读数 → 只出原因（原行为）。
        text = checked > 0 ? T("已选 %1 / %2 · %3").arg(checked).arg(count).arg(reason) : reason;
        tone = theme::kToneWarn;
    } else if (checked == 0) {
        // M4-T11（§6.2/D2）：勾选 = 参与运行集合；未勾选 = 无参与文件（中性：无事发生）
        text = T("未勾选任何文件");
    } else if (d.run_failed > 0) {
        // 上一轮运行有失败项（§8：红 = 失败，唯一允许红的语义）
        text = T("%1 项失败").arg(d.run_failed);
        tone = theme::kToneErr;
    } else {
        // §3.2 底栏 = 选中摘要 · 输出摘要 · [▶ 开始运行]（mockup："已选 12 / 24 · 就绪"）
        text = T("已选 %1 / %2 · 就绪").arg(checked).arg(count);
        tone = theme::kToneOk;
    }
    if (exceptions > 0) {
        text += T(" · %1 个例外").arg(exceptions);
        if (tone != theme::kToneErr)
            tone = theme::kToneWarn; // 例外 = 警告档（琥珀），仍不升到红
    }
    d.status->set_full_text(text); // §9.3：超长省略号（不折行）
    d.apply_status_tone(tone);
    // R-144（M7-R2D1）：失败段**下钻** —— 点击转运行页（原因行所在）；失败清单落盘出处进
    // tooltip（write_failure_list 在收尾时落 logs/）。非失败态不挂 tooltip/手型。
    const bool drillable = !d.running && d.run_failed > 0;
    d.status->setCursor(drillable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    d.status->setToolTip(
        drillable ? T("点击转到运行页查看失败原因\n失败清单：%1")
                        .arg(d.failure_list_path.isEmpty() ? T("（未落盘）") : d.failure_list_path)
                  : QString());
}

// 段2（R-253）：底栏第二段 = 运行中 ETA / 空闲产出量摘要（含悬浮明细）。
void MainWindow::Impl::refresh_status_output_segment(std::size_t checked) {
    Impl &d = *this;
    // 输出摘要（底栏第二段）：自然语「输出：<格式清单> · <文件数/格式数>」（§12-17：禁公式型
    // 文案 —— 原「N 个文件 × M 个格式 = N×M 个输出」作废）。格式清单/模板/输出根目录/冲突策略
    // 收进悬浮提示（§9.3 不折行）。
    // M4-T14（mockup run-dark 底栏第二段）：**运行中该段显示 ETA**（「ETA 约 00:23」，--txt3），
    // 结束/空闲恢复产出量摘要 —— 与原型同一条"次要状态行"的两种内容。
    if (d.output_status != nullptr) {
        if (d.running) {
            const int eta = d.page_run->eta_seconds();
            d.output_status->set_full_text(eta >= 0 ? T("ETA 约 %1").arg(eta_text(eta))
                                                    : T("ETA 约 —"));
            d.output_status->setToolTip(QString());
        } else if (d.page_output->metadata_only()) {
            // M7-R2.5：仅元数据 = 输出容器跟随每个源文件，格式多选被禁用；底栏不能继续报
            // 现役页签的 JPEG XL（横幅已写"保持原格式"，两处自相矛盾）。
            d.output_status->set_full_text(checked > 0
                                               ? T("输出：保持原格式 · %1 个文件").arg(checked)
                                               : T("输出：保持原格式"));
            d.output_status->setToolTip(
                T("仅元数据模式：输出沿用每个源文件自己的容器格式（零重编码）"));
        } else {
            QStringList labels;
            for (const pp::OutputFormatSpec &spec : d.page_output->config_base().outputs)
                labels << format_label(QString::fromStdString(spec.format_id));
            if (labels.isEmpty()) {
                // M7-R2.5：0 个已选格式时不得沿用"游离槽"现役格式（BMP）—— 旧实现
                // 回退 current_format()，清空选择后底栏仍显示「输出：BMP · 16 个文件」，
                // 与「已选 0 / 预计输出 —」自相矛盾。
                d.output_status->set_full_text(T("输出：未选择格式"));
                d.output_status->setToolTip(T("尚未选择输出格式：请在上方「输出格式」中勾选"));
            } else {
                const int formats = static_cast<int>(labels.size());
                const QString list = labels.join(QStringLiteral(" + "));
                // 有参与文件 → 「· N 个文件」（N = 勾选数）；一个都没勾 → 「· M 个格式」（避免
                // "0 个文件"式的空读数 —— fix-3-status 两行的同一条口径）。
                d.output_status->set_full_text(
                    checked > 0 ? T("输出：%1 · %2 个文件").arg(list).arg(checked)
                                : T("输出：%1 · %2 个格式").arg(list).arg(formats));
                d.output_status->setToolTip(
                    T("格式：%1\n模板：%2\n输出根目录：%3\n同名冲突：%4")
                        .arg(list, d.page_output->output_template(), d.page_output->out_root(),
                             conflict_label(d.page_output->config_base().conflict)));
            }
        }
    }
}

// 段4（R-253）：卡头计数徽标（文件数 / 不支持数 / 未能读取数）。
void MainWindow::Impl::refresh_status_badges(std::size_t count, std::size_t unsupported_files) {
    Impl &d = *this;
    // 卡头计数徽标（mockup .pill 内是纯数字）
    d.file_count->set_full_text(QString::number(count));
    d.file_count->setVisible(true);
    // R-148（M7-R2D1）：目录枚举/读取失败与不支持数同一条 hint 面（任一为 0 时不出该段，
    // 既有「（N 个不支持）」文案逐字不变）
    QStringList parts;
    if (unsupported_files > 0)
        parts << T("%1 个不支持").arg(unsupported_files);
    const std::size_t unreadable = model != nullptr ? model->unreadable_count() : 0;
    if (unreadable > 0)
        parts << T("%1 项未能读取").arg(unreadable);
    d.unsupported->set_full_text(
        parts.isEmpty() ? QString() : T("（%1）").arg(parts.join(QStringLiteral(" · "))));
    d.unsupported->setVisible(!parts.isEmpty());
}

// 段5（R-253）：CTA（开始运行/运行中读数）与工具钮（移除/清除筛选）可用态。
void MainWindow::Impl::refresh_status_controls(std::size_t count, std::size_t checked,
                                               const QString &reason) {
    Impl &d = *this;
    // G5（2026-09-20 R1 修订）：运行中按钮禁用（**取消唯一入口 = 运行页**，该裁定不变）；
    // M4-T14（mockup run-dark .go）：运行中按钮文案变**状态读数**「运行中… 68%」，
    // 空闲/结束恢复「▶  开始运行」。
    // M4-T11：可开始还需勾选集非空（勾选 = 参与运行集合）
    const bool can_start = !d.running && count > 0 && checked > 0 && reason.isEmpty();
    if (d.running) {
        const int pct = d.page_run->progress_percent();
        d.start->setText(pct >= 0 ? T("运行中… %1%").arg(pct) : T("运行中…"));
        d.start->setIcon(QIcon()); // 状态读数态不挂图标（mockup run-dark 的 .go 只出读数）
    } else {
        d.start->setText(T("开始运行"));
        icons::apply(d.start, icons::Name::Play, d.tokens.on_accent, d.tokens.text3, 12);
    }
    d.start->setEnabled(can_start);
    // 修 1-08：无选择时**隐藏**移除钮（非常驻禁用）；运行期由面板级 G5 锁定置灰。
    // R-254（M7-R2.1-F3）：可见/可用两行判据（原 mainwindow.cpp sync_selection 与本处双写）
    // 收敛到 apply_remove_sel_state() 单点；本处仍按「有任意选中索引」的廉价口径喂入
    // （refresh_status 逐文件事件高频触发，不做两级代理映射），与 sync_selection 的
    // 「有映射到有效行的选中」在稳定态逐值相同（差异仅限模型/视图失同步的瞬态）。
    const bool has_selection = d.view->selectionModel() != nullptr &&
                               !d.view->selectionModel()->selectedIndexes().isEmpty();
    d.apply_remove_sel_state(has_selection);
    // 筛选菜单：无生效筛选时「清除筛选」不可点（其余两枚自身即状态）
    if (d.filter_clear != nullptr)
        d.filter_clear->setEnabled(d.proxy->filtering());
}

// M5-T4（§8 状态四档）：把色调键落到**状态点 + 状态词**两件上（同色；色值一律单源在 theme.h 的
// QSS 规则里 —— 调用方只设属性 + repolish）。只在取值变化时 repolish（避免每帧重建样式）。
void MainWindow::Impl::apply_status_tone(const char *tone) {
    const QString value = QString::fromLatin1(tone);
    QWidget *const targets[] = {status_dot, status};
    for (QWidget *widget : targets) {
        if (widget == nullptr)
            continue;
        if (widget->property(theme::kStatusToneProperty).toString() == value)
            continue;
        widget->setProperty(theme::kStatusToneProperty, value);
        theme::repolish(widget);
    }
}

// A19（T11，§3.2 骨架行「…｜输出：…｜剩余磁盘 1.2 TB」）：底栏第三段的读数。
// 数据源 = **输出根目录**（未设 = 源目录 = 首个输入文件的目录；两者都无 = 进程工作目录 ——
// 仍是"这次会话往哪写"的最近似面），显示该卷的可用空间（自然语，不显示裸字节）。
// 读数缓存 1.5s：refresh_status 会被逐文件事件高频触发，卷查询不需要逐帧精度。
QString MainWindow::Impl::disk_free_segment() {
    if (page_output == nullptr || disk_status == nullptr)
        return QString();
    QString root = page_output->out_root().trimmed();
    QString source;
    if (root.isEmpty() && model != nullptr && !model->empty())
        source = QString::fromStdString(model->row(0).entry.base_dir.string());
    const bool from_out_root = !root.isEmpty();
    if (root.isEmpty())
        root = source.isEmpty() ? QDir::currentPath() : source;
    constexpr qint64 kRefreshMs = 1500;
    if (disk_checked_path != root || !disk_clock.isValid() || disk_clock.elapsed() > kRefreshMs) {
        disk_checked_path = root;
        const QStorageInfo info(root);
        disk_free_bytes = (info.isValid() && info.isReady()) ? info.bytesAvailable() : -1;
        disk_clock.restart();
    }
    const QString text = disk_free_bytes >= 0
                             ? T("剩余磁盘 %1").arg(byte_text::natural_bytes(disk_free_bytes))
                             : T("剩余磁盘 —");
    // 读数出处进 tooltip（§2.6：单位/口径不占正文）
    disk_status->setToolTip(
        disk_free_bytes >= 0
            ? T("%1所在卷的可用空间：%2").arg(from_out_root ? T("输出根目录") : T("源目录"), root)
            : T("无法读取卷的可用空间：%1").arg(root));
    disk_status->set_full_text(text);
    return text;
}

} // namespace pp::ui
