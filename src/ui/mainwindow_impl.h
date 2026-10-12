// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — mainwindow 私有共享层（M7-WP9 / R-048·R-116·R-258 拆分产物）。
//
// 契约（docs/deep-review-0.3.2.md §5R R-048/R-116/R-258 + 附录 A.1）：
//   * 本头**不是**公开 API（冻结公开面仍是 ui/mainwindow.h，逐字未动）；它只承载
//     MainWindow::Impl 的完整定义与壳层自绘件，供 mainwindow.cpp（产品域）与
//     smoke_probes.cpp（探针域，PP_BUILD_DEV 门控）两个 TU 共享同一 Impl 视图；
//   * 拆分口径 = 行为零变化：类/函数体自 mainwindow.cpp 逐字搬入（仅自由函数补 inline，
//     语义与链接形态不变），objectName / 钩子 / 断言逐位不变；
//   * 探针成员声明以 `#ifdef PP_BUILD_DEV` 门控（发布二进制不含探针面，R-048）。
// 临时私有头：不属于 PP-FROZEN 面；随 mainwindow.cpp 拆分演进。
#pragma once

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

namespace pp::ui {

// 平台壳层别名（与原 mainwindow.cpp 匿名命名空间同义；头内置于 pp::ui 作用域供两 TU 共用）
namespace plat = pp::platform;

// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」）：保存全文，按当前宽度省略
// R-020/R-320（M7-WP4）：ElidedLabel 的 7 份逐字拷贝收敛到 formkit 唯一实现
// （本文件原 30+ 行自拷类删除；行为逐位等价 —— 全文 + ppFullText + tooltip + 省略号）。
using ElidedLabel = formkit::ElidedLabel;

// 运行轮询/状态合并刷新间隔（原 mainwindow.cpp 匿名命名空间；拆 TU 后多域共用，constexpr 单源）
constexpr int kPollIntervalMs = 150; // §2.14：运行轮询间隔
constexpr int kStatusFlushMs = 150;  // R-024：状态栏合并刷新间隔（100–200ms 档）
constexpr int kStatusNoteMs = 4000;  // UX-2：状态栏一次性过滤态提示的驻留（非阻塞）

// 文案翻译单点（历史名 T；context = "MainWindow"，与原实现逐字同源）
inline QString T(const char *s) { return QCoreApplication::translate("MainWindow", s); }

// A28（T11）主窗启动读取点（§9「手动切换（设置·通用）」的跨进程落地）：把 settings.ui_theme_mode
// 注入 theme.h 的进程级持久化档，再按单源顺序解析本窗口的落地模式（PP_UI_THEME 强制档 > 持久化
// 档 > 跟随系统）。注入只发生在这里 —— 之后所有 preferred_theme_mode() 调用点（图标/自绘件初值/
// 弹窗）取到同一档。
// 只吃**入参**（不读盘）：--ui-smoke/走查构造 MainWindow(pp::AppSettings{}) → 恒为默认档
// system → 冻结截图与开发机 settings.ini 状态无关（M4-T9 口径由"调用方给默认设置"承担，
// 故本函数不再叠加 PP_UI_NO_STATE 判断；双主题走查的强制档仍是 PP_UI_THEME）。
inline theme::ThemeMode apply_startup_theme(const std::string &persisted) {
    theme::set_persisted_theme_mode(QByteArray::fromStdString(persisted));
    return theme::preferred_theme_mode();
}

// 顶栏一体式分段导航（§6.2）：**单控件**——共享描边 + 段间 hairline + 选中段填充 acc +
// 数字圆内嵌；段宽 92、高 30（= 段所在行高 = 导航条高 30）。注意与 R4「分段控件 高 24 / 段宽 88」
// 的区别：那是**表单里的互斥模式档**（formkit::SegmentedControl），本件是顶栏导航档，两者同形不同源。
//
// 接线口径（沿 M4）：选中态与使能态的**唯一持有者 = 三个 QAction**（checked → 选中段、
// enabled → 段可用；运行期锁定 G5 与 set_current_page 都走既有 action 路径），本控件只画与收点击
// （点击 → action->trigger()），不持有第二份状态。
// 自绘（而不是三个子钮）的理由：共享描边、段间 hairline、容器圆角裁切（overflow:hidden 等效）
// 与"选中零宽度变化"（段宽恒 92）在一次绘制里落地；段几何 = 容器内的整高切片（92×30）。
//
// §11 无障碍（M5-T12/A18）：
//   * 可访问接口 = **tab/button group 语义**（容器 PageTabList、段 PageTab；名/选中/使能一律取自
//     既有 QAction）——见本文件下方的 ShellNavAccessible/段节点与 installFactory；
//   * 键盘：Tab 进入本控件（**单 Tab 停靠点**），段间用 ←/→（**Ctrl+←/→ 亦可** —— 预览面板的
//     应用级 Router 吃无修饰 ←/→，见下方 keyPressEvent 注释）按**视觉序**（= 段序 = 运行流程序）
//     移动并激活，Home/End（含 Ctrl 变体）到首尾，Enter/Space 激活当前段 —— 与鼠标点击同一条
//     QAction 路径；
//   * 焦点环自绘（accent 描边 1px；本类是自绘件，不出系统虚线框）。
class ShellNav;

// §11/A18：可访问接口工厂（定义在本节之后；objectName 命中，见实现处注释）。
// Qt 未编入 accessibility（QT_CONFIG(accessibility) 关）时为空实现 —— 控件面零变化。
#if QT_CONFIG(accessibility)
void ensure_shell_nav_accessible_factory();
#else
inline void ensure_shell_nav_accessible_factory() {}
#endif

class ShellNav : public QWidget {
public:
    explicit ShellNav(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("pp-nav-strip"));
        // §11/A18：导航 = 交互控件 → 进入 Tab 链（A18 前为 NoFocus，键盘不可达）
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(tr("步骤导航")); // §11：控件可读名（nav-a11y 探针取证非空）
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setMouseTracking(true);
        ensure_shell_nav_accessible_factory(); // §11/A18：tab/button group 语义的可访问接口
    }

    // 段 = [数字圆 15][gap 6][action->text()]；number 内嵌圆内（§6.2）；文案一律来自 QAction
    void add_segment(QAction *action, int number) {
        Segment segment;
        segment.action = action;
        segment.number = number;
        segments_.append(segment);
        if (action != nullptr) {
            // QAction 的 checked/enabled 变化（点击/键盘/set_current_page/lock_for_run）→ 重绘
            connect(action, &QAction::toggled, this, [this] { update(); });
            connect(action, &QAction::changed, this, [this] { update(); });
        }
        updateGeometry();
        update();
    }

    int segment_count() const { return int(segments_.size()); }
    // 段矩形（容器坐标；92×30 —— 含容器描边的整高切片，探针口径见 shell-geom 行）
    QRect segment_rect(int index) const {
        if (index < 0 || index >= segments_.size())
            return QRect();
        return QRect(theme::Metrics::nav_border + index * theme::Metrics::nav_seg_w, 0,
                     theme::Metrics::nav_seg_w, height());
    }
    QAction *segment_action(int index) const {
        return index >= 0 && index < segments_.size() ? segments_.at(index).action : nullptr;
    }
    int segment_number(int index) const {
        return index >= 0 && index < segments_.size() ? segments_.at(index).number : -1;
    }
    // 选中段（checked action）下标；-1 = 无（键盘视觉序的起点，§11/A18）
    int current_segment() const {
        for (int i = 0; i < segments_.size(); ++i) {
            const QAction *action = segments_.at(i).action;
            if (action != nullptr && action->isChecked())
                return i;
        }
        return -1;
    }
    // §11/A18：焦点环是否在画（自绘；探针据此 + 像素取证双重取证）
    bool focus_ring_visible() const { return hasFocus(); }

    void set_tokens(const theme::Tokens &tokens) {
        tokens_ = tokens;
        update();
    }

    QSize sizeHint() const override {
        return QSize(segments_.size() * theme::Metrics::nav_seg_w + 2 * theme::Metrics::nav_border,
                     theme::Metrics::nav_h);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // 容器：共享描边 + 卡片底 + r6（tokens.css `.nav-strip{border:1px solid --ctl-bd;
        // border-radius:6px;background:--card}`）
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(tokens_.control_bd, 1.0));
        painter.setBrush(tokens_.card);
        painter.drawRoundedRect(box, theme::Metrics::nav_radius, theme::Metrics::nav_radius);
        QPainterPath clip; // 容器形状（选中填充/悬停底的圆角裁切 = CSS overflow:hidden 等效）
        clip.addRoundedRect(box, theme::Metrics::nav_radius, theme::Metrics::nav_radius);

        for (int i = 0; i < segments_.size(); ++i) {
            const Segment &segment = segments_.at(i);
            const QAction *action = segment.action;
            const bool active = action != nullptr && action->isChecked();
            const bool enabled = action == nullptr || action->isEnabled();
            const QRect area = segment_rect(i);
            painter.save();
            painter.setClipPath(clip);
            // 运行期锁定（G5）：mockup .step.dim{opacity:.45} 是整段（含数字圆）变暗
            painter.setOpacity(enabled ? 1.0 : theme::Metrics::nav_dim_opacity);
            // 选中段填充 acc（段间 hairline 画在填充之上：非首段的左 1px 线在 CSS 里是
            // `button+button{border-left:1px solid --ctl-bd}`，选中态也保留）
            if (active) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(tokens_.accent);
                painter.drawRect(area);
            } else if (hover_ == i && enabled) {
                // 悬停仅换底、零形变（§12-4）；取与 .cap:hover/图标钮同一"悬停灰"档令牌
                painter.setPen(Qt::NoPen);
                painter.setBrush(tokens_.control);
                painter.drawRect(area);
            }
            if (i > 0) {
                painter.setPen(QPen(tokens_.control_bd, 1.0));
                painter.drawLine(area.left(), theme::Metrics::nav_border, area.left(),
                                 height() - theme::Metrics::nav_border);
            }
            // 内容 = [数字圆 15][gap 6][文案]，整体在段内居中（tokens.css button{gap:6px}）
            const QFont text_font =
                theme::font(theme::Typography::nav_text_px, theme::Typography::nav_text_weight);
            const QFontMetrics text_metrics(text_font);
            const QString text = action != nullptr ? action->text() : QString();
            const int content_w = theme::Metrics::nav_num + theme::Metrics::nav_num_gap +
                                  text_metrics.horizontalAdvance(text);
            const int x0 = area.left() + (area.width() - content_w) / 2;
            const qreal diameter = theme::Metrics::nav_num;
            const QRectF circle(x0, (height() - diameter) / 2.0, diameter, diameter);
            // 数字圆：`.nav-strip .n{background:--ctl;border:1px --ctl-bd;color:--txt2}` /
            // 选中段 `button.on .n{background:rgba(255,255,255,.25);border-color:transparent;
            // color:--acc-on}`（§4.2 徽标档 9.5/700，裁定 A4）
            painter.setPen(active ? Qt::NoPen : QPen(tokens_.control_bd, 1.0));
            painter.setBrush(active ? tokens_.nav_num_sel_bg : tokens_.control);
            painter.drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));
            painter.setFont(
                theme::font(theme::Typography::nav_num_px, theme::Typography::nav_num_weight));
            painter.setPen(active ? tokens_.on_accent : tokens_.text2);
            painter.drawText(circle, Qt::AlignCenter, QString::number(segment.number));
            painter.setFont(text_font);
            painter.setPen(active ? tokens_.on_accent : tokens_.text2);
            painter.drawText(QRect(x0 + theme::Metrics::nav_num + theme::Metrics::nav_num_gap, 0,
                                   text_metrics.horizontalAdvance(text), height()),
                             Qt::AlignVCenter | Qt::AlignLeft, text);
            painter.restore();
        }
        // §11/A18：焦点环自绘 —— accent 描边 1px，环位 = 容器内 1px（与 container 描边并见）、
        // 圆角同容器。本类全自绘 → 系统虚线框（QStyle focus rect）本就不出，无需另行关闭。
        if (hasFocus()) {
            painter.setPen(QPen(tokens_.accent, kFocusRingPx));
            painter.setBrush(Qt::NoBrush);
            const qreal inset = theme::Metrics::nav_border + kFocusRingPx / 2.0;
            painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset),
                                    theme::Metrics::nav_radius, theme::Metrics::nav_radius);
        }
    }

    // §11/A18：键盘 = 「Tab 进入本控件（单停靠点）→ ←/→ 按**视觉序**在段间移动并激活，
    // Home/End 到首尾，Enter/Space 激活当前段」。移动即激活（导航 = 互斥档语义：选中段就是
    // 当前页）；跳过禁用段（运行期锁定 G5 下 1/2 段禁用 → 键盘不会落到不可用段）。
    //
    // 键面与预览面板的翻图热键的关系（**应用里真正可达的一路**）：预览面板装有应用级事件
    // 过滤器（PreviewPanel 的 Router：无修饰的 ←/→ = 翻图，全窗口生效），故在本窗口内
    // **带 Ctrl 的同一组键**才是导航可达路径（Router 只吃无修饰键）；两路都认，语义一致。
    void keyPressEvent(QKeyEvent *event) override {
        const Qt::KeyboardModifiers mods = event->modifiers();
        const bool nav_mods = mods == Qt::NoModifier || mods == Qt::ControlModifier;
        int target = -1;
        if (nav_mods) {
            switch (event->key()) {
            case Qt::Key_Left:
            case Qt::Key_PageUp: // 页签语义的惯用键（QTabWidget 同族）
                target = neighbor_segment(-1);
                break;
            case Qt::Key_Right:
            case Qt::Key_PageDown:
                target = neighbor_segment(1);
                break;
            case Qt::Key_Home:
                target = edge_segment(1);
                break;
            case Qt::Key_End:
                target = edge_segment(-1);
                break;
            default:
                break;
            }
        }
        if (target < 0 && mods == Qt::NoModifier) {
            switch (event->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space:
                target = current_segment();
                break;
            default:
                break;
            }
        }
        if (target >= 0 && activate_segment(target)) {
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override {
        pressed_ = segment_at(event->pos());
        QWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        const int hit = segment_at(event->pos());
        if (hit >= 0 && hit == pressed_) {
            QAction *action = segments_.at(hit).action;
            if (action != nullptr && action->isEnabled())
                action->trigger(); // 唯一路径 = 既有 set_current_page 接线
        }
        pressed_ = -1;
        QWidget::mouseReleaseEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        const int hit = segment_at(event->pos());
        if (hit != hover_) {
            hover_ = hit;
            update();
        }
        QWidget::mouseMoveEvent(event);
    }
    void leaveEvent(QEvent *event) override {
        if (hover_ != -1) {
            hover_ = -1;
            update();
        }
        QWidget::leaveEvent(event);
    }

private:
    struct Segment {
        QAction *action = nullptr;
        int number = 1;
    };
    int segment_at(const QPoint &pos) const {
        for (int i = 0; i < segments_.size(); ++i) {
            if (segment_rect(i).contains(pos))
                return i;
        }
        return -1;
    }
    bool segment_enabled(int index) const {
        const QAction *action = segment_action(index);
        return action != nullptr && action->isEnabled();
    }
    // 视觉序（= 段序）上距当前段最近的使能段（step = ±1，越界回卷）；-1 = 无可去段
    int neighbor_segment(int step) const {
        const int count = int(segments_.size());
        if (count == 0)
            return -1;
        const int from = current_segment() >= 0 ? current_segment() : 0;
        for (int i = 1; i <= count; ++i) {
            const int candidate = ((from + step * i) % count + count) % count;
            if (segment_enabled(candidate))
                return candidate;
        }
        return -1;
    }
    // 视觉序首/尾的使能段（Home = 从最左向右找；End = 反之）
    int edge_segment(int step) const {
        const int count = int(segments_.size());
        if (count == 0)
            return -1;
        const int start = step > 0 ? 0 : count - 1;
        for (int i = 0; i < count; ++i) {
            const int candidate = ((start + step * i) % count + count) % count;
            if (segment_enabled(candidate))
                return candidate;
        }
        return -1;
    }
    bool activate_segment(int index) {
        QAction *action = segment_action(index);
        if (action == nullptr || !action->isEnabled())
            return false;
        action->trigger(); // 唯一路径 = 既有 set_current_page 接线（与鼠标同一条）
        update();
        return true;
    }

    static constexpr int kFocusRingPx = 1; // §11：accent 描边 1px

    QList<Segment> segments_;
    theme::Tokens tokens_;
    int hover_ = -1;
    int pressed_ = -1;
};

#if QT_CONFIG(accessibility)
// ---------------------------------------------------------------------------
// §11 无障碍（M5-T12/A18）：ShellNav 的可访问接口 —— **tab/button group 语义**
//   导航是自绘**单控件**（§6.2：段不是子部件）→ 段在无障碍层是**合成**节点：
//   容器 = `PageTabList`、段 = `PageTab`（名/选中/使能/动作**一律取自既有 QAction** —— 唯一
//   状态源，不新增第二份状态；press = action->trigger()，与鼠标点击同一条路径）。
//   工厂按 **objectName**（"pp-nav-strip"）命中：本类不持 Q_OBJECT（定义在 .cpp 内，类名键
//   只能是基类名），objectName 在本窗口内唯一且已被 shell-geom 探针钉住。
// ---------------------------------------------------------------------------
class ShellNavSegmentAccessible : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    ShellNavSegmentAccessible(ShellNav *nav, int index) : nav_(nav), index_(index) {}
    ~ShellNavSegmentAccessible() override = default;

    int index() const { return index_; }

    // ---- QAccessibleInterface ----
    bool isValid() const override { return nav_ != nullptr && action() != nullptr; }
    QObject *object() const override { return nav_; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }
    QAccessibleInterface *parent() const override;
    QAccessibleInterface *child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QString text(QAccessible::Text t) const override {
        const QAction *a = action();
        switch (t) {
        case QAccessible::Name:
            return a != nullptr ? a->text() : QString();
        case QAccessible::Description: // 语义补充：这是运行流程里的第几步
            return QObject::tr("步骤 %1").arg(index_ + 1);
        default:
            return QString();
        }
    }
    void setText(QAccessible::Text, const QString &) override {}
    QRect rect() const override {
        return nav_ != nullptr ? QRect(nav_->mapToGlobal(nav_->segment_rect(index_).topLeft()),
                                       nav_->segment_rect(index_).size())
                               : QRect();
    }
    QAccessible::Role role() const override { return QAccessible::PageTab; }
    QAccessible::State state() const override {
        const QAction *a = action();
        QAccessible::State state;
        state.disabled = a == nullptr || !a->isEnabled();
        state.selectable = true;
        state.selected = a != nullptr && a->isChecked();
        state.checked = state.selected;
        state.focusable = true; // 段 = 键盘可达（Tab 进容器后 ←/→ 按视觉序移动）
        state.focused = nav_ != nullptr && nav_->hasFocus() && nav_->current_segment() == index_;
        state.active = state.focused;
        return state;
    }
    void *interface_cast(QAccessible::InterfaceType t) override {
        return t == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface *>(this)
                                                 : nullptr;
    }

    // ---- QAccessibleActionInterface（唯一动作 = press；与鼠标同一触发路径）----
    QStringList actionNames() const override { return {QAccessibleActionInterface::pressAction()}; }
    void doAction(const QString &actionName) override {
        if (actionName != QAccessibleActionInterface::pressAction())
            return;
        QAction *a = action();
        if (a != nullptr && a->isEnabled())
            a->trigger();
    }
    // §11/A18：真实键面（Ctrl 变体 = 本应用里可达的一路，见 ShellNav::keyPressEvent 注释）
    QStringList keyBindingsForAction(const QString &) const override {
        return {QStringLiteral("Ctrl+Left"), QStringLiteral("Ctrl+Right"),
                QStringLiteral("Ctrl+Home"), QStringLiteral("Ctrl+End"), QStringLiteral("Space")};
    }

private:
    QAction *action() const { return nav_ != nullptr ? nav_->segment_action(index_) : nullptr; }
    ShellNav *nav_ = nullptr;
    int index_ = -1;
};

class ShellNavAccessible : public QAccessibleWidget {
public:
    explicit ShellNavAccessible(ShellNav *nav)
        : QAccessibleWidget(nav, QAccessible::PageTabList,
                            nav != nullptr ? nav->accessibleName() : QString()) {}
    ~ShellNavAccessible() override {
        qDeleteAll(segments_); // 段节点 = 合成子项（本接口持有；容器接口析构即回收）
        segments_.clear();
    }

    QAccessible::Role role() const override { return QAccessible::PageTabList; }
    QAccessible::State state() const override {
        QAccessible::State state = QAccessibleWidget::state();
        state.focusable = true;
        state.focused = nav() != nullptr && nav()->hasFocus();
        return state;
    }
    int childCount() const override { return nav() != nullptr ? nav()->segment_count() : 0; }
    QAccessibleInterface *child(int index) const override {
        ShellNav *host = nav();
        if (host == nullptr || index < 0 || index >= host->segment_count())
            return nullptr;
        while (segments_.size() <= index) // 惰性建（段数在构造后由 add_segment 递增）
            segments_.append(new ShellNavSegmentAccessible(host, int(segments_.size())));
        return segments_.at(index);
    }
    int indexOfChild(const QAccessibleInterface *child) const override {
        const auto *segment = dynamic_cast<const ShellNavSegmentAccessible *>(child);
        return segment != nullptr ? segment->index() : -1;
    }
    QAccessibleInterface *childAt(int x, int y) const override {
        ShellNav *host = nav();
        if (host == nullptr)
            return nullptr;
        const QPoint local = host->mapFromGlobal(QPoint(x, y)); // childAt 的口径 = 屏幕坐标
        for (int i = 0; i < host->segment_count(); ++i) {
            if (host->segment_rect(i).contains(local))
                return child(i);
        }
        return nullptr;
    }
    QAccessibleInterface *focusChild() const override {
        ShellNav *host = nav();
        if (host == nullptr || !host->hasFocus())
            return nullptr;
        const int current = host->current_segment();
        return current >= 0 ? child(current) : nullptr;
    }
    QString text(QAccessible::Text t) const override {
        ShellNav *host = nav();
        if (t == QAccessible::Name && host != nullptr && !host->accessibleName().isEmpty())
            return host->accessibleName();
        return QAccessibleWidget::text(t);
    }

private:
    ShellNav *nav() const { return static_cast<ShellNav *>(widget()); }
    mutable QList<ShellNavSegmentAccessible *> segments_;
};

inline QAccessibleInterface *ShellNavSegmentAccessible::parent() const {
    return nav_ != nullptr ? QAccessible::queryAccessibleInterface(nav_) : nullptr;
}

namespace {
QAccessibleInterface *shell_nav_accessible_factory(const QString &, QObject *object) {
    if (object == nullptr || object->objectName() != QLatin1String("pp-nav-strip"))
        return nullptr;
    auto *nav = dynamic_cast<ShellNav *>(object);
    return nav != nullptr ? new ShellNavAccessible(nav) : nullptr;
}
} // namespace

inline void ensure_shell_nav_accessible_factory() {
    static const bool installed = [] {
        QAccessible::installFactory(&shell_nav_accessible_factory);
        return true;
    }();
    Q_UNUSED(installed);
}
#endif // QT_CONFIG(accessibility)

// ---------------------------------------------------------------------------
// M5-T14（§12-12/§11）：文件列表视图的**系统焦点框抑制**（样式层收口）
//   事实与依据（Qt 6.11 源码 + T13 真实平台取证）：QTreeView::drawRow 画当前行焦点框的调用是
//   `style()->drawPrimitive(QStyle::PE_FrameFocusRect, &o, painter)` —— **不带 widget 参数**；
//   而 QStyleSheetStyle 的规则查找以该参数为键 → 骨架 QSS 的 `outline: none` 对这条路径天然
//   无效（实测：规则在场时当前行仍被「1px 白 + 1px 黑」焦点框盖住左缘 2px accent 选中条，
//   见 T13 filelist-selprobe after=[#ffffff,#ffffff,#000000,#000000,…]；与 QTBUG-4281
//   「focus rect 无法用样式表去除」同源）。故本件在样式层收口：QProxyStyle 包装当前样式，
//   仅当 PE_FrameFocusRect 的**目标**是文件列表视图时吞掉绘制（判定三条任一命中：widget 参数
//   = 该视图或其 viewport / QStyleOption::styleObject = 该视图 / 无 widget 且画布 = 该视图
//   或其 viewport），其余 primitive/control/几何**全部透传** —— 其他控件的系统焦点提示不受
//   影响。选中面本体由委托自绘（accent-dim 底 + 左缘 2px accent 条，修 3-17）
//   ＝ 可见焦点指示本身（§11「禁用系统虚线框」）。
//   目标识别按 **objectName**（"pp-file-view" 与 Qt 自动命名的 viewport）——不持任何部件
//   指针：多窗口（冒烟走查会临时建/销第二个主窗）与部件析构下零悬空（T14 实测教训：曾以
//   静态指针登记目标视图，第二个主窗析构后指针悬空 → 真实平台焦点框绘制崩于 viewport()）。
//   诊断计数器（只增，不参与断言）：供真实平台失败定位「哪条判定命中/是否漏网」。
class FileViewFocusFrameStyle : public QProxyStyle {
public:
    explicit FileViewFocusFrameStyle(QStyle *base) : QProxyStyle(base) {}
    static void reset_counters() { hits_option() = hits_widget() = hits_device() = misses() = 0; }
    static int &hits_option() {
        static int n = 0;
        return n;
    }
    static int &hits_widget() {
        static int n = 0;
        return n;
    }
    static int &hits_device() {
        static int n = 0;
        return n;
    }
    static int &misses() {
        static int n = 0;
        return n;
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override {
        if (element == QStyle::PE_FrameFocusRect) {
            if (widget != nullptr && is_file_list_target(widget)) {
                ++hits_widget();
                return;
            }
            if (option != nullptr && is_file_list_target(option->styleObject)) {
                ++hits_option();
                return;
            }
            // 画布口径（无 widget 的 QTreeView::drawRow 焦点框调用）：画布是 QWidget 时才可比
            const QObject *canvas =
                painter != nullptr ? dynamic_cast<const QObject *>(painter->device()) : nullptr;
            if (widget == nullptr && is_file_list_target(canvas)) {
                ++hits_device();
                return;
            }
            if (widget == nullptr)
                ++misses();
        }
        // GUI-R2 举一反三：平台指示器矢量化。QSS 只能给 ::branch / ::indicator 塞图片，
        // 无法画矢量形（C3 禁字形）→ 在样式层自绘，形只有一份（formkit + icons）。
        if (element == QStyle::PE_IndicatorBranch && option != nullptr && painter != nullptr) {
            formkit::paint_branch_indicator(
                *painter, option->rect, option->state & QStyle::State_Children,
                option->state & QStyle::State_Open, theme::tokens(theme::preferred_theme_mode()));
            return; // 平台的方块/加号/连接线一并不画（设计稿的树无连接线）
        }
        if (element == QStyle::PE_IndicatorMenuCheckMark && option != nullptr &&
            painter != nullptr) {
            formkit::paint_menu_check(*painter, option->rect,
                                      theme::tokens(theme::preferred_theme_mode()));
            return;
        }
        // 菜单钮的下拉箭头（QToolButton::menu-indicator 走这里）—— 同样矢量化，
        // 不再让平台画那枚小三角（GUI-R2 举一反三第 6 处）
        if (element == QStyle::PE_IndicatorArrowDown && option != nullptr && painter != nullptr) {
            const theme::Tokens t = theme::tokens(theme::preferred_theme_mode());
            const int side = std::min(option->rect.width(), option->rect.height());
            const QRectF box(option->rect.center().x() - side / 2.0,
                             option->rect.center().y() - side / 2.0, side, side);
            icons::paint(*painter, icons::Name::ChevronDown, box,
                         option->state & QStyle::State_Enabled ? t.text2 : t.text3,
                         icons::kStrokePx);
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

private:
    // 目标 = 文件列表视图本体（objectName "pp-file-view"）或其 viewport（Qt 自动命名，
    // 父 = 该视图）。
    static bool is_file_list_target(const QObject *object) {
        const QWidget *widget = qobject_cast<const QWidget *>(object);
        if (widget == nullptr)
            return false;
        if (widget->objectName() == QLatin1String("pp-file-view"))
            return true;
        const QWidget *parent = widget->parentWidget();
        return parent != nullptr && parent->objectName() == QLatin1String("pp-file-view");
    }
};

// caption 三钮（§3.1/§3.3：46×32、透明底、悬停灰、关闭悬停 #c42b1c 白字）。关闭悬停色在
// tokens/QSS 里（theme::Tokens::close_hover / close_hover_fg）。
// R-279（M7-R2D1）：本类是 caption 钮的**唯一实现**（图标名/双色/命中带通用）；
// settings_dialog.cpp 的 CaptionCloseButton 是它的第二份手拷（固定关图标、无命中带）——
// 收编需动非本包文件面，已登记 handoff「跨界需求」（两实现合并的落点建议 = 本类直用）。
//
// **单轨（M5-T4，裁定 A1）**：可见面与命中区同为 46×32（theme::Metrics::caption_btn_h ==
// caption_btn_hit_h == platform::kCaptionHitHeightPx）。命中带 = 按钮矩形（居中的 32px 带在顶栏
// 44px 内），无边框窗口的 WM_NCHITTEST 用同一条带（platform/frameless.cpp 的
// rect_of(caption_button) 在"控件高 > 命中带高"时才收敛，单轨下两者恒等）。
// caption 图标边长（px）：mockup 的 .cap 内联 svg 是 10×10（WinUI3 语言的 caption 字形盒）
constexpr int kCaptionIconPx = 10;

class CaptionButton : public QPushButton {
public:
    // normal/hover = 两态令牌色（tokens.css .cap{color:var(--txt)} /
    // .cap.close:hover{color:#fff}）： QIcon 的取色不受 QSS 的 :hover color
    // 影响，故悬停换色由本控件显式落地（enter/leaveEvent）。
    CaptionButton(icons::Name name, const QColor &normal, const QColor &hover, QWidget *parent)
        : QPushButton(parent), name_(name), normal_(normal), hover_(hover) {
        setText(QString()); // M5-T3：内容 = 矢量图标（icons::caption-*），字形文本清零
        setAttribute(Qt::WA_Hover, true);
        apply_icon(false);
    }

    void set_icon_name(icons::Name name) {
        name_ = name;
        apply_icon(underMouse());
    }
    icons::Name icon_name() const { return name_; }
    void set_colors(const QColor &normal, const QColor &hover) {
        normal_ = normal;
        hover_ = hover;
        apply_icon(underMouse());
    }

    // 命中带：与 platform/frameless.cpp 的 rect_of(..., caption_button=true) 同一条带。单轨下
    // box.height() == band（46×32）→ 原文返回；此处的居中收敛只对"控件比命中带高"的历史形态生效。
    static QRect hit_band(const QRect &box) {
        const int band = theme::Metrics::caption_btn_hit_h;
        if (box.height() <= band)
            return box;
        return QRect(box.left(), box.top() + (box.height() - band) / 2, box.width(), band);
    }

protected:
    bool hitButton(const QPoint &pos) const override {
        return QPushButton::hitButton(pos) && hit_band(rect()).contains(pos);
    }
    void enterEvent(QEnterEvent *event) override {
        QPushButton::enterEvent(event);
        apply_icon(true);
    }
    void leaveEvent(QEvent *event) override {
        QPushButton::leaveEvent(event);
        apply_icon(false);
    }

private:
    void apply_icon(bool hovered) {
        icons::apply(this, name_, hovered ? hover_ : normal_, QColor(), kCaptionIconPx);
    }

    icons::Name name_;
    QColor normal_;
    QColor hover_;
};

inline const char *hit_zone_name(plat::HitZone zone) {
    switch (zone) {
    case plat::HitZone::None:
        return "None";
    case plat::HitZone::Client:
        return "Client";
    case plat::HitZone::Caption:
        return "Caption";
    case plat::HitZone::MinButton:
        return "MinButton";
    case plat::HitZone::MaxButton:
        return "MaxButton";
    case plat::HitZone::CloseButton:
        return "CloseButton";
    case plat::HitZone::Left:
        return "Left";
    case plat::HitZone::Right:
        return "Right";
    case plat::HitZone::Top:
        return "Top";
    case plat::HitZone::Bottom:
        return "Bottom";
    case plat::HitZone::TopLeft:
        return "TopLeft";
    case plat::HitZone::TopRight:
        return "TopRight";
    case plat::HitZone::BottomLeft:
        return "BottomLeft";
    case plat::HitZone::BottomRight:
        return "BottomRight";
    }
    return "?";
}

inline const char *caption_action_name(plat::CaptionAction action) {
    switch (action) {
    case plat::CaptionAction::Minimize:
        return "minimize";
    case plat::CaptionAction::Maximize:
        return "maximize";
    case plat::CaptionAction::Close:
        return "close";
    }
    return "?";
}

inline QString format_label(const QString &id) {
    const pp::FormatDef *f = pp::find_format(id.toStdString());
    return f != nullptr ? QString::fromStdString(f->label) : id;
}

// ---------------------------------------------------------------------------
// M5-T5（§7.5 工具行 + G3）左栏筛选代理：搜索之外的两个**可见性**开关
// （「仅显示已勾选」/「仅显示有例外的文件」）。
//   * 与搜索**正交**（§6.2：过滤只改可见性 —— 勾选集合/计数/分组键/分类归属都不因过滤变化）；
//   * 只读消费源模型已有 roles（Qt::CheckStateRole / ExceptionRole），不新增任何数据语义；
//   * 基类的 filterAcceptsRow 语义（搜索串 × NameRole × 大小写不敏感）原样保留。
// ---------------------------------------------------------------------------
class LeftPanelFilterProxy : public QSortFilterProxyModel {
public:
    explicit LeftPanelFilterProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}

    void set_checked_only(bool on) {
        if (checked_only_ == on)
            return;
        checked_only_ = on;
        invalidateFilter();
    }
    void set_exceptions_only(bool on) {
        if (exceptions_only_ == on)
            return;
        exceptions_only_ = on;
        invalidateFilter();
    }
    // M7-R2D1 / R-143 加性：分类过滤档（空 = 不过滤；chip 点击经 mainwindow 落到这里）
    void set_class_filter(const QString &class_id) {
        if (class_filter_ == class_id)
            return;
        class_filter_ = class_id;
        invalidateFilter();
    }
    QString class_filter() const { return class_filter_; }
    bool checked_only() const { return checked_only_; }
    bool exceptions_only() const { return exceptions_only_; }
    bool filtering() const { return checked_only_ || exceptions_only_ || !class_filter_.isEmpty(); }
    void clear_filters() {
        set_checked_only(false);
        set_exceptions_only(false);
        set_class_filter(QString());
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override {
        if (!QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent))
            return false; // 搜索（文件名，大小写不敏感）
        if (!checked_only_ && !exceptions_only_ && class_filter_.isEmpty())
            return true;
        const QModelIndex idx = sourceModel() != nullptr
                                    ? sourceModel()->index(source_row, 0, source_parent)
                                    : QModelIndex();
        if (!idx.isValid())
            return false;
        if (checked_only_ && idx.data(Qt::CheckStateRole).toInt() != int(Qt::Checked))
            return false;
        if (exceptions_only_ && !idx.data(FileListModel::ExceptionRole).toBool())
            return false;
        if (!class_filter_.isEmpty() &&
            idx.data(FileListModel::ClassIdRole).toString() != class_filter_)
            return false;
        return true;
    }

private:
    bool checked_only_ = false;
    bool exceptions_only_ = false;
    QString class_filter_; // R-143：分类过滤档（空 = 全部）
};

// ---------------------------------------------------------------------------
// Impl：控件树 + 运行状态 + 冒烟断言
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Impl：控件树 + 运行状态 + 冒烟断言
// ---------------------------------------------------------------------------

struct MainWindow::Impl {
    MainWindow *w = nullptr;
    pp::AppSettings settings;

    // ---- M4-T9 骨架：主题 + 无边框 + 三栏 ----
    theme::ThemeMode theme_mode = theme::ThemeMode::Dark; // 明暗跟随系统（§9.2 末行）
    theme::Tokens tokens;                                 // 当前主题 tokens（refresh_theme 落地）
    plat::Frameless *frameless = nullptr;                 // 无边框接线（Windows = WM_NCHITTEST）
    QWidget *root = nullptr;                              // 窗口底（mockup body 渐变）
    QWidget *titlebar = nullptr;                          // 顶栏（兼标题栏；拖拽区）
    QWidget *bottombar = nullptr;                         // 底栏（状态栏 30px）
    QSplitter *splitter = nullptr;                        // 三栏
    QLabel *app_icon = nullptr;
    ShellNav *nav = nullptr; // 顶栏导航区（一体式分段，§6.2；段 = 三个 QAction）
    QAction *presets_action = nullptr;
    QToolButton *presets_btn = nullptr;
    QToolButton *settings_btn = nullptr;
    QPushButton *cap_min = nullptr;
    QPushButton *cap_max = nullptr;
    QPushButton *cap_close = nullptr;
    ElidedLabel *output_status = nullptr;
    // A19（T11，§3.2 骨架行）：第三段「剩余磁盘」+ 段前 hairline；读数缓存（refresh_status 会被
    // 逐文件事件高频触发，卷查询无需逐帧精度）
    ElidedLabel *disk_status = nullptr;
    QFrame *disk_sep = nullptr;
    QString disk_checked_path;
    qint64 disk_free_bytes = -1;
    QElapsedTimer disk_clock;
    QFrame *status_sep = nullptr; // 状态栏段间分隔 hairline（修 5-13）
    QLabel *status_dot = nullptr;
    ElidedLabel *preview_pill = nullptr; // 中栏卡头徽标（文件名，T10 填）
    ElidedLabel *preview_hint = nullptr; // 中栏卡头右端 hint（尺寸·位深·色彩空间，T10 填）
    QWidget *left_col = nullptr;         // 左栏容器（文件卡 + 分类卡，§3.2 左栏 330 固定轴）
    QFrame *left_card = nullptr;         // 左栏文件卡框（T11 填内容）
    QFrame *classify_card = nullptr;     // 左栏分类卡框（T11）
    QFrame *preview_card = nullptr;      // 中栏卡框（内嵌 T10 的 PreviewPanel）
    // ---- M4-T10：常驻输入预览面板（§6.1）+ 分类注册表（热键表数据源）----
    PreviewPanel *preview_panel = nullptr;
    // 分类注册表的**当前实例**（T10 只读消费：热键提示表；T11 接管 CRUD/持久化/打标与勾选语义）
    pp::ClassRegistry classes;

    // ---- 左：文件面板（M1b 内容 + M4-T11 勾选/分组 + M5-T5 工具行/空态）----
    QWidget *panel = nullptr;
    FileListModel *model = nullptr;
    Thumbnailer *thumbs = nullptr;
    LeftPanelFilterProxy *proxy = nullptr;      // 搜索 + 筛选（§6.2：与勾选正交）
    FileGroupProxyModel *groups = nullptr;      // M4-T11：分组节层次层（QTreeView 的数据源）
    FileTreeView *view = nullptr;               // M4-T11：QListView → QTreeView（§3.6 裁定行）
    FileGroupDelegate *file_delegate = nullptr; // M5-T5：行几何/字阶单源 + tokens 重放
    QComboBox *group_mode = nullptr;            // 分组：按月份 | 按相机型号 | 按源格式 | 无分组
    ElidedLabel *file_count = nullptr;          // 卡头计数徽标（中性 pill，§2.7/1-11）
    ElidedLabel *unsupported = nullptr;
    ElidedLabel *file_hint = nullptr; // 卡头 hint（仅运行期「运行中锁定」；教学文案已入空态 1-12）
    QLineEdit *search = nullptr;
    QToolButton *add_btn = nullptr;        // ＋（添加文件…/添加文件夹… 菜单）
    QToolButton *remove_sel = nullptr;     // −（移除所选；无选择时**隐藏**，1-08）
    QToolButton *filter_btn = nullptr;     // 筛选…（M5-T5 工具行第三钮；菜单由其父对象持有）
    QAction *filter_checked = nullptr;     // 仅显示已勾选
    QAction *filter_exceptions = nullptr;  // 仅显示有例外的文件
    QAction *filter_clear = nullptr;       // 清除筛选
    QToolButton *check_menu_btn = nullptr; // UX-2：勾选操作钮（pp-check-menu；三项菜单）
    QWidget *tools_row = nullptr;          // 工具行容器（常驻五件：搜索/添加/移除/筛选/分组）
    QStackedWidget *file_stack = nullptr;  // 文件区两页：0 = 列表，1 = 空态投放区（修 1-06）
    QWidget *empty_state = nullptr;        // 空态页
    QFrame *drop_zone = nullptr;           // 投放区（accent-bd 框 + accent-dim 底）
    QLabel *drop_icon = nullptr;           // 投放引导图标（矢量）
    ElidedLabel *drop_text = nullptr;      // 投放引导文案（§8 逐字）
    QPushButton *empty_add = nullptr;      // 空态的显式「添加文件…」钮（§8）
    QAction *add_files_action = nullptr;   // ＋ 菜单项：添加文件…
    QAction *add_dir_action = nullptr;     // ＋ 菜单项：添加文件夹…
    QAction *clear_action = nullptr;       // ＋ 菜单项：清空列表
    // ---- M4-T11：分类面板（§6.2）----
    ClassifyPanel *classify_panel = nullptr;
    ElidedLabel *classify_hint = nullptr; // "1–9 键打标" / "运行中只读"（G5）
    QString classes_file;                 // classes.json 路径（settings.class_file；§6.2 单源）
    bool applying_group_state =
        false; // 正在程序化落地展开态（期间的 collapsed/expanded 不记为用户操作）

    // ---- 顶栏 / 页面 ----
    QAction *nav_meta = nullptr;
    QAction *nav_output = nullptr;
    QAction *nav_run = nullptr;
    QAction *settings_action = nullptr;
    QActionGroup *nav_group = nullptr;
    QStackedWidget *stack = nullptr;
    PageMeta *page_meta = nullptr;
    PageOutput *page_output = nullptr;
    PageRun *page_run = nullptr;

    // ---- 底栏 ----
    ElidedLabel *status = nullptr;
    QPushButton *start = nullptr;
    // M7-R2D1 / UX-2：状态栏一次性非阻塞提示（过滤态勾选的作用域说明；kStatusNoteMs 后回落）
    QString status_note;
    QTimer *status_note_timer = nullptr;

    // ---- 运行状态 ----
    std::unique_ptr<pp::Scheduler> sched;
    QTimer *poll = nullptr;
    // M4-T14：日志尾卡喂入（§9.3「日志尾卡（5 行滚动 + 跳转）」）——单一持有者 = MainWindow：
    // 运行期每 500ms 读最新 run-*.log 的末尾若干行推给运行页（页只渲染，不做文件 IO）
    QTimer *log_tail = nullptr;
    // R-081（M7-R2D1）：日志尾读取在途门槛 + 补读标记（IO 在 worker，结果经 QFutureWatcher 回 GUI）
    bool log_tail_inflight = false;
    bool log_tail_rerun = false;
    QTimer *status_flush = nullptr; // R-024：运行期状态栏合并刷新（singleShot 150ms）
    bool running = false;
    // 完成检测（§2.14 的"轮询 !running()"机制不可用：pp::Scheduler::running() 只在 wait() 内清零，
    // 见报告 api-deltas）→ 轮询改为"终态事件计数 == 本批文件数"，语义等价且不阻塞 GUI。
    std::size_t run_total = 0;
    std::size_t run_terminal = 0;
    std::size_t run_done = 0;
    std::size_t run_failed = 0;
    std::size_t run_skipped = 0;
    // R-076/R-140：部分成功与取消各自独立计数（部分成功不并入"成功"，取消不隐身）
    std::size_t run_partial = 0;
    std::size_t run_cancelled = 0;
    // R-250：运行读数**单账本** —— started/terminal 全部在 Impl（原与 page_run 的
    // files_started/files_terminal 混拼同一文案 = 双账本）。run_seen = 该文件是否已离开 Queued。
    std::size_t run_started = 0;
    std::vector<bool> run_seen;
    // R-006/R-146：「再次运行」= 补齐语义（只提交缺件 + Skip 冲突策略）；由 on_run_again 置位
    bool run_again_pending = false;
    // ---- M7-R2D1 / R-144 加性：失败清单（运行期收集 → 收尾落盘 logs/，状态栏可下钻）----
    QStringList run_failure_lines; // "路径\t原因"（逐终态失败/部分成功文件）
    QString failure_list_path;     // 最近一次落盘路径（状态栏 tooltip 出处）
    // M4-T11：本批提交的**模型行号**（ev.index 是提交序号 → 需映射回列表行更新状态）
    std::vector<std::size_t> run_rows;
    QString last_selected_path; // 分组重建后恢复选中（重建 = 模型 reset，选中会丢）
    // R-153（分组重建吞多选）：重建前快照**全部**选中路径（不止首个）—— 过滤/摘要/分组模式
    // 变化触发 modelReset 时多选整组恢复，批量打标/移除不再塌缩成 1 行。
    QStringList last_selected_paths;
    QString last_preset_path;
    QStringList cached_paths; // 行集合签名（避免每次状态变化都重扫时间预览）
    bool cached_alpha = false;
    // M6-T11（§5.2 R5）：图层档记忆的写回接线只接一次（apply_map_style 幂等，可反复调用）
    bool map_layer_bound = false;

    // ---- 拖放 / closeEvent 事件过滤器（冻结头不允许覆写虚函数）----
    struct Filter : QObject {
        Impl *impl = nullptr;
        explicit Filter(Impl *i) : QObject(i->w), impl(i) {}
        bool eventFilter(QObject *watched, QEvent *event) override {
            return impl->handle_event(watched, event);
        }
    };
    Filter *filter = nullptr;

#ifdef PP_BUILD_DEV
    // ---- 冒烟 ----
    QStringList smoke_failures;
    int smoke_shots = 0;
#endif // PP_BUILD_DEV

    Impl(MainWindow *owner, const pp::AppSettings &s);
    ~Impl();

    bool handle_event(QObject *watched, QEvent *event);

    // 联动 / 工具
    void on_content_changed();
    void sync_selection();
    MetaSelectionItem meta_selection_item(const FileRow &row); // M4-T12：跟随项快照（零 IO 探测）
    void sync_exceptions();
    void clear_exception(const QString &path);
    void remove_selected();
    void restore_session();
    // M6-T11（§5.2 R5）：settings.map_style → GPS 卡图层 Seg + map->set_tile_style
    // （启动期与设置对话框 accept 后各一次；首次调用同时接线"换层写回 settings"）
    void apply_map_style();
    void open_logs();
    QString disk_free_segment(); // A19（T11）：底栏「剩余磁盘」段（输出根 → 源目录 → 工作目录）
    void push_log_tail();        // M4-T14：run-*.log 末尾 5 行 → 运行页日志尾卡
    void handle_file_event(const pp::FileEvent &ev);
    void schedule_status_refresh(); // R-024：合并刷新（singleShot 起搏）
    // ---- M7-R2D1 加性（UX-2 / R-144 / R-081；既有签名逐字不变）----
    enum class CheckOp { All, None, Invert }; // UX-2：勾选批量操作（作用于当前可见行）
    void apply_visible_checks(CheckOp op);
    void set_status_note(const QString &note); // UX-2：过滤态一次性提示（非阻塞）
    void write_failure_list();                 // R-144：失败清单落盘（logs/；只读源、不覆盖源）
    void restore_selection_by_path(const QString &path);
    void restore_selection_by_paths(const QStringList &paths); // R-153：整组恢复
    void select_row(int row);                                  // R-151：翻图同步选中

    // M4-T9 骨架：主题 / 三栏持久化 / caption 行为
    void refresh_theme();
    // R-253（M7-WP9）：refresh_theme/refresh_status 的**语义分段**（原单函数同管多事 →
    // 每段一个方法；调用顺序与语义逐字保持，行为零变化）。
    void theme_apply_base();
    void theme_replay_panels();
    void theme_replay_chrome();
    void theme_apply_effects();
    void refresh_status_summary(std::size_t count, std::size_t checked, std::size_t exceptions,
                                const QString &reason);
    void refresh_status_output_segment(std::size_t checked);
    void refresh_status_badges(std::size_t count, std::size_t unsupported_files);
    void refresh_status_controls(std::size_t count, std::size_t checked, const QString &reason);
    void apply_chrome_icons(); // M5-T3：骨架图标（☆/⚙/caption/＋/－/▶）随 tokens 重放
    void set_theme_mode(theme::ThemeMode mode);
    void restore_splitter_state();
    void save_splitter_state();
    void update_caption_buttons();
    void apply_status_tone(const char *tone); // M5-T4（§8）：状态点 + 状态词同色四档
#ifdef PP_BUILD_DEV
    void smoke_probe_shell_geometry(); // M5-T4（C7）：壳层几何值表探针
#endif                                 // PP_BUILD_DEV

    // M4-T11：左栏（勾选/分组/分类）
    void load_classes(); // classes.json → 注册表（失败仅告警，保持默认）
    void save_classes(); // 注册表 → classes.json（打标/CRUD 即写）
    void apply_class_to_selection(const QString &class_id); // 热键/菜单打标（选中集或当前项）
    // M7-R2D1 / R-143：sync_class_views（选中项 → 分类面板高亮）随 chip `.on` 拆态（= 过滤档）移除
    void rebuild_group_view();     // 分组结构变化后：跨列节头 + 展开态 + 选中恢复
    void apply_group_view_state(); // 上述状态的落地（推迟到下一轮事件循环）
    void set_group_mode(pp::GroupMode mode);

#ifdef PP_BUILD_DEV
    // 冒烟工具
    void pump(int ms);
    bool wait_for(const std::function<bool()> &pred, int timeout_ms);
    void wait_thumbs(int timeout_ms);
    void smoke_fail(const QString &reason);
    void smoke_grab(const QString &dir, const char *name, QWidget *target = nullptr);
    void smoke_run(const QString &shots_dir);
    void smoke_amap_boundary(pp::map::MapWidget *map);
    // M4-T9 骨架自检（窗口行为矩阵可自动化部分）
    void smoke_probe_skeleton();
    void smoke_probe_frameless();
    void smoke_probe_theme();
    void smoke_probe_lifecycle();
    // M4-T10 预览面板自检（§6.1：翻图/缩放/徽标/热键提示 + 热键接线位）
    void smoke_probe_preview();
    // M5-T6（C7）预览画布几何值表 + 空态/角标/缩放条/衬垫/插值（§8 / 修
    // 1-16/1-17/1-19/1-21/1-22/3-20/3-21）
    void smoke_probe_preview_geometry();
    // M4-T11 文件列表/分类自检（§3.6 roles + §6.2 勾选/正交搜索/分组节/分类面板）
    void smoke_probe_filelist();
    void smoke_probe_classify();
    // M5-T5（C7）文件行/工具行几何值表 + 空态投放区 + 选中面像素取证（§7.5 / 修
    // 3-17/1-06/1-07/1-08）
    void smoke_probe_filelist_geometry();
#endif // PP_BUILD_DEV
    // M5-T5：列表空 ⟺ 空态投放区（修 1-06 的唯一落地；探针与运行期同一实现）
    void apply_empty_state();
    static bool empty_state_visible(bool model_empty, bool running);
#ifdef PP_BUILD_DEV
    // M4-T12 元数据页自检（§5.2：生效值显示与写入一致性 + .mod + 多选小表 + 无选中提示 + 地图跟随）
    void smoke_probe_meta(const QString &shots_dir);
    MetaSelectionItem meta_probe_item(const QString &path); // 探针的跟随项快照（oiio probe 补摘要）
    // M5-T8（C7）输出页几何值表（§7.2 四卡组 + R1–R4 + 修 2-05/2-07/2-10/2-11/2-13/2-14/2-15/
    // 2-16/2-21/2-24/2-25/2-33 + 裁定 A6/A7/A8）
    void smoke_probe_output_geometry();
#endif // PP_BUILD_DEV
    // M4-T13 预设卡（页面信号 → 单一持有者：I/O + rules 合并 + 列表刷新）
    void refresh_preset_card();
    void load_preset_file(const QString &path);
    void save_preset_named(const QString &name);
    void delete_preset_file(const QString &path);
#ifdef PP_BUILD_DEV
    // M4-T14 运行页自检（§3.4/§7：逐输出行 · 真实/斜纹像素取证 · 失败原因 · 取消态 · 浅色）
    void smoke_probe_run(const QString &shots_dir);
    // M5-T9（C7）运行页几何/语义值表（§7.3 三卡组 + R1–R4 + K4：指标单源 / 0 字节琥珀 /
    // 日志尾等宽 4 行 / 完成态唯一形态 + 裁定 A7/A8/A9）
    void smoke_probe_run_geometry();
    // M5-T13（W-G4 验收，C7 探针只增）：§12-13 亮暗布局等价（pp-* 控件几何/可见性逐项一致）
    // + §12-14 卡头 hint 禁折行/单行高/省略号 —— 两条 §12 条目此前无独立探针。
    void smoke_probe_verification();
    // GUI-R2（docs/gui-review-0.3.0-r2.md 回归探针，只增）：下拉一致性 / 格式卡对齐 /
    // 弹窗 caption 关闭钮 / 运行重入守卫 —— 四条自检行各钉一轮排查的一条结论。
    void smoke_probe_gui_r2();
    static QString repo_root();
#endif // PP_BUILD_DEV
};
} // namespace pp::ui
