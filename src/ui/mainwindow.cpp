// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — main window (M1b-U10 integration; M4-T9 骨架重构).
//
// 冻结头（§2.14）只给出公开 API + 私有 build_ui/wire/lock_for_run/refresh_status/save_session
// + `struct Impl`，因此全部控件与状态承载在本 TU 的 MainWindow::Impl 内。两个无法用覆写实现的
// 行为（§2.14 要求"窗口整体 acceptDrops，dropAccept → add_paths" 与 closeEvent 保存会话）
// 通过装在窗口上的事件过滤器完成——头文件逐字节冻结，不添加虚函数覆写。
//
// 接线口径全部来自 W-B 落地口径（§2.8/§2.9/§2.10/§2.11/§2.12/§2.13）与 §4.3 接线备忘：
//   * FileListModel::set_thumbnailer 内部已 connect(ready→apply_thumb) 并自动 enqueue →
//   不重复连接；
//   * PageOutput 的格式/参数/位深/冲突状态只经冻结 API（config_base/collect_preset/apply_preset）；
//   * PageMeta 的 objectName 契约（gps_map 等）用于 set_offline_maps 与冒烟断言；
//   * ExifEditor::result() 遮蔽 QDialog::result() → 判 accept 必须写 dlg.QDialog::result()。
//
// M4-T9（0.3.0）骨架重构（依据 docs/v0.3.0-design.md §9.1/§9.2 + docs/mockups/）：
//   * 无边框窗口（Qt::FramelessWindowHint）+ 顶栏自绘 caption 三钮：接线在 platform/frameless
//     （Windows = WM_NCHITTEST/HTCAPTION/HTMAXBUTTON/六向缩放；其它平台 =
//     startSystemMove/Resize）；
//   * 三栏骨架：QSplitter(左 330 固定轴 | 中输入预览 | 右 587 固定轴 + QStacked 三页)，
//     尺寸持久化走 QSettings（M5-T4 换锚到规范 §3.2 的 330/587 基线与 min-width:0 约束）；
//   * 顶栏/底栏 + 主题 tokens 单源（ui/theme.h）：明暗跟随系统（QStyleHints::colorScheme）。
//     本任务只给面板**占位与布局接线**：中栏内容归 T10、左栏内容归 T11、右栏三页内容归 W3。
// M5-T4（W-G2 壳层，依据 docs/qt-gui-design-spec.md v6 §3/§6.2/§8 + m5-adjudications
// A1/A4/A5/A7）：
//   * 顶栏 44px 三段式（品牌区 20/13-700/9.5 ｜ 一体式分段导航 92×30 居中 ｜ ☆/⚙ 28×28 + caption
//     46×32 单轨）；状态栏 30px（状态四档同色 + 自然语摘要 + 固定轴 330/587 + CTA 22 居右）。

#include "ui/mainwindow.h"
#include "ui/formkit.h" // M5-T7：meta-geom 探针读回卡头胶囊（formkit::PillBadge）与 R1/R2 几何
#include "ui/icons.h"

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
#include "ui/leading_indent.h"  // R-218（M7-R2E2）：逻辑边距（标题栏）单源
#include "ui/mainwindow_impl.h" // M7-WP9：Impl/壳层件共享层（探针 TU 同视图）
#include "ui/page_meta.h"
#include "ui/page_output.h"
#include "ui/page_run.h"
#include "ui/paramform.h"
#include "ui/preset_io.h"
#include "ui/presets_dialog.h"
#include "ui/preview_panel.h"
#include "ui/settings_dialog.h"
#include "ui/theme.h"
#include "ui/thumbnails.h"

namespace pp::ui {
namespace {

// ---------------------------------------------------------------------------
// M4-T9 骨架部件（设计 §9.1/§9.2；尺寸与色值一律取自 ui/theme.h 的 tokens，不写死字面量）
// ---------------------------------------------------------------------------
namespace theme = pp::ui::theme;
namespace icons = pp::ui::icons;
namespace plat = pp::platform;

bool skeleton_state_disabled() {
    // --ui-smoke 置位（main.cpp）：不读写面板持久化 → 冻结截图与机器无关
    return qEnvironmentVariableIsSet("PP_UI_NO_STATE");
}

// 启动/走查主题：PP_UI_THEME=dark|light 强制；未设置 → 持久化档（A28）→ 跟随系统（§9.2）。
// M4-W2-fix：解析搬到 theme::preferred_theme_mode()（单源 = ui/theme.h）——main.cpp 的启动期
// DWM 深色标题栏（apply_window_backdrop）必须与 GUI 落地的是**同一个**模式，否则窗口边框会
// 按系统色滞后（第 11 条）。
using theme::preferred_theme_mode;

// —— M6-T11（§5.2 R5 / 契约 C6 的消费面）：图层档的跨进程记忆 ——
// 档词表 = AppSettings.map_style（road|satellite|hybrid），与 GPS 卡图层 Seg 的三态段序
// （矢量|卫星|混合；objectName pp-meta-gps-layer，C7 冻结面）**同序同义**，也与
// mapwidget/layers.h 的 TileStyle 枚举同序。本表 = 该序在外壳侧的唯一登记处（core 侧词表在
// settings.cpp 的 apply_pair；页面侧段序在 page_meta.cpp 的三段构造）。
struct MapStyleRow {
    const char *id;
    pp::map::TileStyle style;
};
constexpr MapStyleRow kMapStyles[] = {
    {"road", pp::map::TileStyle::Road},           // 0 矢量
    {"satellite", pp::map::TileStyle::Satellite}, // 1 卫星
    {"hybrid", pp::map::TileStyle::Hybrid},       // 2 混合
};

int map_style_index(const std::string &id) {
    for (int i = 0; i < 3; ++i) {
        if (id == kMapStyles[i].id)
            return i;
    }
    return 0; // 词表外/空 → 矢量（与 core「值不合法 → 保留默认」同口径）
}

// 会话态文件（QSettings INI）：与 settings.ini 同目录（data_dir() 保证已存在）
QString ui_state_path() {
    const std::filesystem::path path = pp::platform::data_dir() / "ui-state.ini";
    const std::string bytes = path.string();
    return QFile::decodeName(QByteArray(bytes.data(), static_cast<int>(bytes.size())));
}

// 顶栏应用图标（mockup .app-title 的 24×24 svg：相机外框 + 镜头圆 + 右上小点）：
// M5-T3 起并入图标库（单实现，M3 铁律六）—— 形 = icons::Name::Image（与 mockup 逐点同源）。
QPixmap app_icon_pixmap(const QColor &accent, int size, qreal dpr) {
    return pp::ui::icons::pixmap(pp::ui::icons::Name::Image, size, accent, dpr);
}

// 卡头（mockup .card-h）：标题（12.5px/700）+ 可选徽标位（.pill，无内容则隐藏）+
// 右侧 hint 位（调用方拿 row->layout() 继续追加，右对齐）
QWidget *make_card_header(QWidget *parent, const QString &title, QLabel **title_out,
                          ElidedLabel **pill_out = nullptr) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 9, 12, 7);
    layout->setSpacing(8);
    auto *label = new QLabel(title, row);
    label->setObjectName(QStringLiteral("pp-card-title"));
    label->setFont(
        theme::font(theme::Typography::card_title_px, theme::Typography::card_title_weight));
    layout->addWidget(label);
    if (title_out != nullptr)
        *title_out = label;
    if (pill_out != nullptr) {
        auto *pill = new ElidedLabel(row); // 长文件名/计数都按 §9.3 省略号
        // M5-T5（§2.7「徽章只表状态不表计数」+ 1-11 修：蓝色实心误导可点击）：卡头徽标 =
        // **中性**胶囊（mockup .pill n）。色调仍由 theme.h 的 pill QSS 单源给出。
        pill->setProperty(theme::kPillProperty,
                          QString::fromLatin1(theme::kPillNeutral)); // QSS：QLabel[ppPill="n"]
        pill->setFont(theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
        pill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // 不随行高拉伸
        pill->setVisible(false);                                         // 原型里有内容才画药丸
        layout->addWidget(pill);
        *pill_out = pill;
    }
    layout->addStretch(1);
    return row;
}

// 空态投放区（修 1-06；§8「文件列表=投放区+显式"添加文件…"钮」）：
//   居中矢量图标 + 引导语「拖放照片到此处」+ 显式「添加文件…」钮（A16：§8 的逐字引导语
//   「将照片拖到此处，或点击 ＋ 添加」归属**预览画布**，文件列表不得同屏重复同一句）；
//   框/底为 accent 档（mockup fix-5 `.list1{background:var(--acc-dim);border:1px solid
//   var(--acc-bd)}`）。
QWidget *build_empty_state(QWidget *parent, QFrame **drop_zone_out, QLabel **icon_out,
                           ElidedLabel **text_out, QPushButton **add_out) {
    auto *page = new QWidget(parent);
    page->setObjectName(QStringLiteral("pp-empty-state"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 10, 10);
    auto *zone = new QFrame(page);
    zone->setObjectName(QStringLiteral("pp-drop-zone"));
    zone->setAttribute(Qt::WA_StyledBackground, true);
    auto *zone_layout = new QVBoxLayout(zone);
    zone_layout->setContentsMargins(14, 14, 14, 14);
    zone_layout->setSpacing(10); // 组内距 8–12（§4.3）
    zone_layout->addStretch(1);
    auto *icon = new QLabel(zone);
    icon->setObjectName(QStringLiteral("pp-drop-icon"));
    icon->setFixedSize(26, 26);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *text = new ElidedLabel(zone);
    text->setObjectName(QStringLiteral("pp-drop-text"));
    text->setFont(theme::font(theme::Typography::body_px));
    text->setAlignment(Qt::AlignCenter);
    // A16：文件列表空态 = 「拖放照片到此处」（§8 的逐字引导语只归预览画布；同屏两条同文
    // 冗余 → 列表侧改短句 + 显式「添加文件…」钮，见下方 add）
    text->set_full_text(T("拖放照片到此处"));
    auto *add = new QPushButton(T("添加文件…"), zone);
    add->setObjectName(QStringLiteral("pp-empty-add"));
    add->setFont(theme::font(theme::Typography::body_px, 600));
    add->setFixedHeight(theme::Metrics::btn_h); // 裁定 A7（R4 按钮高 26）
    add->setCursor(Qt::PointingHandCursor);
    add->setAccessibleName(T("添加文件…"));
    icons::apply(add, icons::Name::Add, theme::tokens(theme::preferred_theme_mode()).accent,
                 QColor(), 12);
    auto *btn_row = new QHBoxLayout();
    btn_row->setContentsMargins(0, 0, 0, 0);
    btn_row->addStretch(1);
    btn_row->addWidget(add);
    btn_row->addStretch(1);
    zone_layout->addWidget(icon, 0, Qt::AlignHCenter);
    zone_layout->addWidget(text, 0);
    zone_layout->addLayout(btn_row);
    zone_layout->addStretch(1);
    layout->addWidget(zone);
    page->setVisible(false); // 有文件时不占位（apply_empty_state 是唯一开关）
    if (drop_zone_out != nullptr)
        *drop_zone_out = zone;
    if (icon_out != nullptr)
        *icon_out = icon;
    if (text_out != nullptr)
        *text_out = text;
    if (add_out != nullptr)
        *add_out = add;
    return page;
}

// 图标钮（mockup .icon-btn：R4 图标钮 28×28 r4，悬停显底）。
// M5-T3（契约 C3）：图标 = **矢量绘制**（ui/icons.h），不再是 Segoe UI Symbol 字形 ——
// mockup/run.html 的 ☆/⚙ 内联 SVG 已转录进图标库（star/gear 逐点同源），故 M4-W2-fix 的
// "彩色 emoji 字形自带颜色、QSS 失效"整条历史问题随之消失（QIcon 按模式取色的矢量引擎）。
// 取色：Normal = --txt2、Active/选中 = --txt、Disabled = 预合成的 .dim 等效色（见 refresh_theme）。
QToolButton *make_icon_button(QWidget *parent, QAction *action, const char *object_name,
                              icons::Name icon_name) {
    auto *button = new QToolButton(parent);
    button->setObjectName(QString::fromLatin1(object_name));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(theme::Metrics::icon_btn, theme::Metrics::icon_btn);
    icons::apply(button, icon_name, theme::tokens(theme::preferred_theme_mode()).text2, QColor(),
                 15); // mockup/run.html .icon-btn 的 15×15 svg
    button->setFocusPolicy(Qt::NoFocus);
    if (action != nullptr) {
        button->setDefaultAction(action); // 提示/enabled 跟 QAction（lock_for_run 靠它置灰）
        // 纯图标钮不出文本（可见面 = 图标）：QAction 的 text 只为无障碍/语义而设，不落到控件文案
        button->setText(QString());
        // §11：图标钮必有 accessibleName + tooltip（同源 = QAction 的文案）
        button->setAccessibleName(action->toolTip().isEmpty() ? action->text() : action->toolTip());
    }
    return button;
}

// caption 三钮：图标 = icons 库（mockup 的 .cap 内联 svg 逐点同源：─ / □ / ✕），尺寸 =
// §3.3 逐字「46×32」（M5-T4 单轨：可见面 = 命中区 = 46×32，见 theme::Metrics::caption_btn_h）。
QPushButton *make_caption_button(QWidget *parent, icons::Name icon_name, const QString &tip,
                                 const char *object_name, const QColor &normal,
                                 const QColor &hover) {
    auto *button = new CaptionButton(icon_name, normal, hover, parent);
    button->setObjectName(QString::fromLatin1(object_name));
    button->setToolTip(tip);
    button->setAccessibleName(tip); // §11：图标钮必有 accessibleName + tooltip
    button->setFixedSize(theme::Metrics::caption_btn_w, theme::Metrics::caption_btn_h);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

// R-157（M7-WP9）：日志级别文本解析**单源** = pp::log_level_from_text（core/logger.h，
// WP8 状态映射包落地）。本函数保留 UI 侧入口名（调用点零改动），5 档分支拷贝删除；
// 判据（大小写敏感小写 / "critical" 判负 / 非法回退）与启动侧 main.cpp 同一条实现。
bool parse_log_level_text(const std::string &s, pp::LogLevel &out) {
    return pp::log_level_from_text(s, out);
}

} // namespace

MainWindow::Impl::Impl(MainWindow *owner, const pp::AppSettings &s)
    : w(owner), settings(s), theme_mode(apply_startup_theme(s.ui_theme_mode)) {
    // M5-T14（§12-12/§11）：文件列表系统焦点框抑制 = **应用样式层收口**（见
    // FileViewFocusFrameStyle 的成因说明）。安装点 = 本窗口任何子部件之前：其后创建的部件在
    // 铺样式/被 QSS 包装时都以本代理为底座（refresh_theme 的骨架 QSS 亦在之后落地）。
    // 安全前提（Qt 6.11 QApplication::setStyle 行为）：旧样式只在「父对象 = qApp」时被删除 →
    // 先给旧样式**脱父**再换，旧实例继续存活（仍被先前建立的 QSS 包装引用 → 不会留下悬空
    // 底座；进程级单例，随进程退出回收）。目标部件按 objectName 识别，不登记任何指针。
    {
        QStyle *current = QApplication::style();
        if (current != nullptr && dynamic_cast<FileViewFocusFrameStyle *>(current) == nullptr) {
            if (current->parent() == qApp)
                current->setParent(nullptr);
            auto *proxy = new FileViewFocusFrameStyle(current); // 代理接管底座所有权
            proxy->setObjectName(current->objectName());        // 样式名口径不变（诊断/日志用）
            QApplication::setStyle(proxy);
        }
    }
    // M4-T11：分类注册表路径（§3.6/§6.2 单源 = settings.class_file；空 = 默认
    // data_dir()/classes.json）
    classes_file =
        QString::fromStdString(s.class_file.empty() ? pp::default_class_file() : s.class_file);
    model = new FileListModel(w);
    model->set_class_registry(&classes); // 分类单源：模型只读/直写注册表，不复制状态
    thumbs = new Thumbnailer(96, w);
    model->set_thumbnailer(thumbs); // U4 口径：内部已 connect(ready→apply_thumb) + 自动 enqueue

    // M5-T5：搜索（NameRole，大小写不敏感）+ 筛选（仅勾选/仅有例外）——同一代理，两者正交
    proxy = new LeftPanelFilterProxy(w);
    proxy->setSourceModel(model);
    proxy->setFilterRole(FileListModel::NameRole);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterFixedString(QString());

    groups = new FileGroupProxyModel(w);
    groups->setSourceModel(proxy); // 分组节（顶层） + 文件行（叶子）；过滤只是可见性
    groups->set_group_mode(model->group_mode());

    poll = new QTimer(w);
    poll->setInterval(kPollIntervalMs);
    log_tail = new QTimer(w);
    log_tail->setInterval(500); // 日志尾刷新（人眼可读的节奏；不参与进度节流）
    // R-024：状态栏合并刷新（100–200ms 档取 150ms）—— 运行期逐文件事件洪峰不再每条都跑
    // 一次全量 refresh_status（含两个 O(n) 计数 + 磁盘查询）。单次触发（singleShot 语义）：
    // 只要还有事件进来就一直推后，静默 150ms 后落地一次。
    status_flush = new QTimer(w);
    status_flush->setSingleShot(true);
    status_flush->setInterval(kStatusFlushMs);
    // UX-2：状态栏一次性过滤态提示（kStatusNoteMs 后回落正常状态行）
    status_note_timer = new QTimer(w);
    status_note_timer->setSingleShot(true);
    status_note_timer->setInterval(kStatusNoteMs);
    connect(status_note_timer, &QTimer::timeout, w, [this] {
        status_note.clear();
        w->refresh_status();
    });

    filter = new Filter(this);
    w->installEventFilter(filter);

    load_classes(); // §6.2：classes.json 往返（文件不存在 = 首次运行，保持默认模板）
}

MainWindow::Impl::~Impl() {
    if (filter != nullptr) {
        w->removeEventFilter(filter);
        delete filter;
        filter = nullptr;
    }
}

bool MainWindow::Impl::handle_event(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *e = static_cast<QDragEnterEvent *>(event);
        // R-071/R-154：运行中拒收拖放 —— 原实现放行 → 目录拖入触发同步递归扫盘冻结 GUI
        // （poll/取消停摆），新行还默认勾选显示"排队"却不属本批（run_rows 是启动快照）。
        if (running) {
            e->ignore();
            return true;
        }
        if (e->mimeData() != nullptr && e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
            return true;
        }
        return false;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        if (running) { // R-071/R-154：同上（拖放已被 DragEnter 拒收，这里是第二道）
            e->ignore();
            return true;
        }
        if (e->mimeData() == nullptr)
            return false;
        QStringList paths;
        for (const QUrl &url : e->mimeData()->urls()) {
            if (url.isLocalFile())
                paths << url.toLocalFile();
        }
        if (!paths.isEmpty()) {
            w->add_paths(paths);
            e->acceptProposedAction();
            return true;
        }
        return false;
    }
    case QEvent::Close: {
        if (running) {
            const QMessageBox::StandardButton answer =
                QMessageBox::question(w, T("退出"), T("批处理正在运行，确定退出吗？"),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                event->ignore();
                return true;
            }
            if (sched)
                sched->cancel();
        }
        w->save_session();
        return false; // 放行默认关闭路径
    }
    case QEvent::WindowStateChange:
        // 最大化/还原 → caption 最大化钮字形与提示跟随（双击标题栏最大化也走这里）
        update_caption_buttons();
        return false;
    case QEvent::Show:
        // M4-W2-fix 第 11 条：DWM 沉浸式深色标题栏/Mica 必须在**真窗口出现后**重放一次
        // （构造期 winId 尚未成型；启动期 main.cpp 已按同一主题模式调过一次，这里是运行期
        // 主题切换/显示时序的兜底，保证窗口边框不再按启动时的系统色滞后）。
        // 同时重放一次 tokens：投影效果器的 blur/offset 是**设备像素**（见 refresh_theme），
        // 构造期 devicePixelRatioF() 还是 1，必须等真窗口出现后按实际 dpr 重算。
        refresh_theme();
        return false;
    case QEvent::MouseButtonPress:
        // R-144（M7-R2D1）：状态栏「N 项失败」下钻 —— 点击跳转运行页（失败原因所在页）；
        // 失败清单落盘路径在该段 tooltip（write_failure_list）。
        if (watched == status && run_failed > 0 && !running) {
            w->set_current_page(3);
            return true;
        }
        return false;
    default:
        break;
    }
    return false; // 未处理的事件照常投递（QObject::eventFilter 的默认语义）
}

// ---------------------------------------------------------------------------
// MainWindow：构造 / 布局 / 接线
// ---------------------------------------------------------------------------

MainWindow::MainWindow(const pp::AppSettings &settings, QWidget *parent)
    : QMainWindow(parent), impl_(std::make_unique<Impl>(this, settings)) {
    build_ui();
    wire();
    impl_->restore_session();
    impl_->restore_splitter_state();
    impl_->refresh_preset_card(); // M4-T13：预设卡列表（会话上次预设高亮）
    impl_->sync_exceptions();
    impl_->on_content_changed();
    set_current_page(1);
}

// R-107：显式收尾序（原 `= default` 靠"析构期无事件循环"的巧合保证正确性）。
// ① 取消在途批次（幂等）→ ② 阻塞等 worker 退出 → ③ 置空事件回调（此后不再有回调落回
// 已在析构的 this）→ ④ 释放调度器。closeEvent 只做异步 cancel，析构路径必须自己收口。
MainWindow::~MainWindow() {
    if (impl_ && impl_->sched) {
        impl_->sched->cancel();
        impl_->sched->wait();
        impl_->sched->set_event_callback({});
        impl_->sched.reset();
    }
}

void MainWindow::build_ui() {
    // R-252 时序锚点①「style 先装」：FileViewFocusFrameStyle 须先于任何子部件安装（Impl 构造
    // 已装；迟装 = 先建部件绕过焦点框抑制代理，M5-T14 的成因说明）。此处断言前置条件成立。
    Q_ASSERT(dynamic_cast<FileViewFocusFrameStyle *>(QApplication::style()) != nullptr);
    Impl &d = *impl_;
    // R-251：tokens 先填后用 —— 原状 d.tokens 在 refresh_theme()（build_ui 末尾）才落地，早期
    // 控件（＋/−/筛选图标、caption 三钮取色）读到的是**未填充**默认令牌，靠末尾重放掩盖；同一
    // 函数里两种写法（d.tokens vs theme::tokens(preferred_theme_mode())）就此收敛为一条取值链。
    d.tokens = theme::tokens(d.theme_mode);
    setWindowTitle(QStringLiteral("PhotoPipeline"));
    setAcceptDrops(true);
    // §9.1 尺寸基准：最小窗口 1180×720 / 1440×900 校准（屏幕不足时收敛到可用区）
    setMinimumSize(theme::Metrics::min_window_w, theme::Metrics::min_window_h);
    {
        QRect available(0, 0, theme::Metrics::calibrated_w, theme::Metrics::calibrated_h);
        if (const QScreen *screen = QGuiApplication::primaryScreen())
            available = screen->availableGeometry();
        const int max_w = std::max(theme::Metrics::min_window_w, available.width() - 40);
        const int max_h = std::max(theme::Metrics::min_window_h, available.height() - 40);
        resize(std::clamp(theme::Metrics::calibrated_w, theme::Metrics::min_window_w, max_w),
               std::clamp(theme::Metrics::calibrated_h, theme::Metrics::min_window_h, max_h));
    }

    // ---- 根容器（窗口底：mockup body 渐变；深色 = Mica 兜底色）----
    auto *central = new QWidget(this);
    d.root = central;
    central->setObjectName(QStringLiteral("pp-root"));
    central->setAttribute(Qt::WA_StyledBackground, true);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- 顶栏（兼标题栏，§3.1/§3.3）：44px 三段式 ----
    //   品牌区（图标 20 + 标题 13/700 + 版本 pill 9.5）｜ 导航区（一体式分段导航，§6.2）｜
    //   工具与窗口控制区（左推：☆ 收藏、⚙ 设置 28×28；caption 三连 46×32）。
    //   三段分布均衡（修 5-01「中段不得大片空置」）：导航区在**两侧弹性之间居中**，品牌贴左、
    //   工具与窗口控制贴右（fix-1-toolbar 裁定「品牌/步骤/工具三段分布，步骤条居中」）。
    d.titlebar = new QWidget(central);
    d.titlebar->setObjectName(QStringLiteral("pp-titlebar"));
    d.titlebar->setFixedHeight(theme::Metrics::toolbar_height);
    auto *top = new QHBoxLayout(d.titlebar);
    // R-218（M7-R2E2）：标题栏内距走逻辑边距（LTR 左 pad_left / RTL 镜像）；LTR 值逐字节不变。
    set_logical_margins(d.titlebar, theme::Metrics::titlebar_pad_left,
                        theme::Metrics::titlebar_pad_right);
    top->setSpacing(theme::Metrics::toolbar_gap); // .toolbar{gap:12px}

    // 品牌区（§3.3）：图标 20（矢量，accent）+ 标题 13/700 + 版本 pill 9.5（§4.2 徽标档）
    d.app_icon = new QLabel(d.titlebar);
    d.app_icon->setObjectName(QStringLiteral("pp-app-icon"));
    d.app_icon->setFixedSize(theme::Metrics::brand_icon, theme::Metrics::brand_icon);
    auto *title = new QLabel(QStringLiteral("PhotoPipeline"), d.titlebar);
    title->setObjectName(QStringLiteral("pp-app-title"));
    title->setFont(theme::font(theme::Typography::caption_px, theme::Typography::caption_weight));
    // R-150：版本徽标 = PP_VERSION_STRING（core/version.h 单源；原硬编码 "0.3.0" 在 0.3.2 发行后
    // 仍显示旧版本）。与 settings_dialog 的「关于」同一条取值口径。
    auto *version = new QLabel(QLatin1String(PP_VERSION_STRING), d.titlebar);
    version->setObjectName(QStringLiteral("pp-app-ver"));
    version->setFont(theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
    // mockup .toolbar{align-items:center}：徽标按自身高度居中，不被行高拉伸
    version->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *title_row = new QHBoxLayout();
    title_row->setContentsMargins(0, 0, 0, 0);
    title_row->setSpacing(theme::Metrics::brand_gap); // .brand{gap:8px}
    title_row->addWidget(d.app_icon);
    title_row->addWidget(title);
    title_row->addWidget(version);
    top->addLayout(title_row);

    d.nav_group = new QActionGroup(this);
    d.nav_group->setExclusive(true);
    d.nav_meta = new QAction(tr("元数据"), this);
    d.nav_output = new QAction(tr("输出"), this);
    d.nav_run = new QAction(tr("运行"), this);
    for (QAction *action : {d.nav_meta, d.nav_output, d.nav_run}) {
        action->setCheckable(true);
        d.nav_group->addAction(action);
    }
    d.nav_meta->setChecked(true);
    // 导航区（§6.2）：一体式分段导航 = **单控件**（ShellNav），段序 = 运行流程顺序
    // ① 元数据 → ② 输出 → ③ 运行（数字圆内嵌；K1：不放 ✓ 等完成态装饰）。
    top->addStretch(1);
    d.nav = new ShellNav(d.titlebar);
    d.nav->setObjectName(QStringLiteral("pp-nav-strip"));
    d.nav->add_segment(d.nav_meta, 1);
    d.nav->add_segment(d.nav_output, 2);
    d.nav->add_segment(d.nav_run, 3);
    top->addWidget(d.nav);
    top->addStretch(1);
    // M5-T3：QAction 只承载提示/使能（图标 = 矢量引擎，见 make_icon_button）；setText 保留
    // 「收藏当前设置 / 设置」供 QAction 语义与探针读回（按钮本体 ToolButtonIconOnly，不出文本）。
    d.presets_action = new QAction(tr("收藏当前设置"), this);
    d.presets_action->setToolTip(tr("收藏当前设置"));
    d.settings_action = new QAction(tr("设置"), this);
    d.settings_action->setToolTip(tr("设置"));
    d.presets_btn = make_icon_button(d.titlebar, d.presets_action, "pp-presets", icons::Name::Star);
    d.settings_btn =
        make_icon_button(d.titlebar, d.settings_action, "pp-settings", icons::Name::Gear);
    // 工具与窗口控制区：v6 .tb-tools 无 gap（☆/⚙ 28×28 相邻、三连 46×32 紧贴 ☰ 侧），
    // 与原型实测一致（图标中心距 28 / caption 中心距 46；右侧留 .toolbar 的 6px 内边距）。
    auto *caps = new QHBoxLayout();
    caps->setContentsMargins(0, 0, 0, 0);
    caps->setSpacing(0);
    // 取色（§3.1/WinUI3 语言）：字形 = --txt；关闭悬停 = #c42b1c 底 +
    // 白字（tokens.close_hover_fg）。 初值用 preferred_theme_mode() 的令牌，refresh_theme()
    // 末尾按当前主题重放（明暗切换即换色）。
    const theme::Tokens boot = theme::tokens(preferred_theme_mode());
    d.cap_min = make_caption_button(d.titlebar, icons::Name::CaptionMin, tr("最小化"), "pp-cap-min",
                                    boot.text, boot.text);
    d.cap_max = make_caption_button(d.titlebar, icons::Name::CaptionMax, tr("最大化"), "pp-cap-max",
                                    boot.text, boot.text);
    d.cap_close = make_caption_button(d.titlebar, icons::Name::CaptionClose, tr("关闭"),
                                      "pp-cap-close", boot.text, boot.close_hover_fg);
    caps->addWidget(d.cap_min);
    caps->addWidget(d.cap_max);
    caps->addWidget(d.cap_close);
    auto *right_row = new QHBoxLayout();
    right_row->setContentsMargins(0, 0, 0, 0);
    right_row->setSpacing(0); // 见上：v6 工具区无 gap（右端整体贴 .toolbar 的 6px 内边距）
    right_row->addWidget(d.presets_btn);
    right_row->addWidget(d.settings_btn);
    right_row->addLayout(caps);
    top->addLayout(right_row);
    outer->addWidget(d.titlebar);

    // ---- 三栏骨架（§3.2）：左 330（280–400 可调）| 中弹性 | 右 587（520–680 可调）----
    auto *main_area = new QWidget(central);
    auto *main_layout = new QHBoxLayout(main_area);
    main_layout->setContentsMargins(theme::Metrics::main_pad_x, theme::Metrics::main_pad_top,
                                    theme::Metrics::main_pad_x, theme::Metrics::main_pad_bottom);
    main_layout->setSpacing(0);
    d.splitter = new QSplitter(Qt::Horizontal, main_area);
    d.splitter->setObjectName(QStringLiteral("pp-splitter"));
    d.splitter->setHandleWidth(theme::Metrics::gap); // 列间距 = mockup .main gap:10px
    d.splitter->setChildrenCollapsible(false);
    main_layout->addWidget(d.splitter);
    outer->addWidget(main_area, 1);

    // 左栏（T11 落地；§3.2 左栏基线 330 / 280–400 可调）：文件卡（勾选+分组）+ 分类卡（§6.2）
    d.left_col = new QWidget(d.splitter);
    d.left_col->setObjectName(QStringLiteral("pp-left-col"));
    // §3.2「左 330px（280–400 可调）」：基线 = 初始尺寸（setSizes 落 330）、可调区间 =
    // [min, max]；窗口缩放不动它（stretch 0 + min-width:0，见下）。
    // 「任何页内容不得反向撑大栏宽（min-width:0）」：横向 QSizePolicy::Ignored = Qt 侧等效 ——
    // 子内容的 sizeHint/minimumSizeHint 一律不参与列宽决定（列宽只由固定轴/弹性分配给出）。
    d.left_col->setMinimumWidth(theme::Metrics::left_width_min);
    d.left_col->setMaximumWidth(theme::Metrics::left_width_max);
    d.left_col->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *left_col_layout = new QVBoxLayout(d.left_col);
    left_col_layout->setContentsMargins(0, 0, 0, 0);
    left_col_layout->setSpacing(theme::Metrics::gap); // mockup .col-left{gap:10px}

    d.left_card = new QFrame(d.left_col);
    d.left_card->setObjectName(QStringLiteral("pp-card"));
    d.left_card->setAttribute(Qt::WA_StyledBackground, true);
    auto *left_layout = new QVBoxLayout(d.left_card);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(0);

    d.panel = new QWidget(d.left_card);
    d.panel->setObjectName(QStringLiteral("pp-file-panel"));
    auto *pv = new QVBoxLayout(d.panel);
    pv->setContentsMargins(0, 0, 0, 6);
    pv->setSpacing(4);

    // 卡头：标题 + 计数徽标（**中性** pill，§2.7/1-11）+ 右端 hint（不支持的格式数 / 运行期锁定）
    QLabel *panel_title = nullptr;
    auto *card_head = make_card_header(d.panel, tr("文件"), &panel_title, &d.file_count);
    d.file_count->setObjectName(QStringLiteral("pp-file-count"));
    d.unsupported = new ElidedLabel(card_head);
    d.unsupported->setObjectName(QStringLiteral("pp-card-hint"));
    // R-059（M7-WP4）：写死色 #dd8800 收口 warn 令牌（A6 令牌收口裁定；formkit.h「不写色值
    // 字面量」纪律）—— 与状态栏警醒档同源取色。
    d.unsupported->setStyleSheet(
        QStringLiteral("color: ") +
        theme::css_color(theme::tokens(theme::preferred_theme_mode()).warn));
    d.unsupported->setVisible(false);
    d.file_hint = new ElidedLabel(card_head);
    d.file_hint->setObjectName(QStringLiteral("pp-card-hint"));
    d.file_hint->setFont(theme::font(theme::Typography::hint_px));
    // M5-T5（1-12）：「拖放添加」教学文案常驻标题旁的问题 → 教学文案移入空态投放区；
    // 卡头 hint 只保留运行期锁定反馈（非运行态为空）
    d.file_hint->set_full_text(QString());
    if (auto *head_layout = qobject_cast<QHBoxLayout *>(card_head->layout())) {
        head_layout->addWidget(d.unsupported); // addStretch 之后 = 右对齐
        head_layout->addWidget(d.file_hint);
    }
    pv->addWidget(card_head);

    // fp-tools（v6 mockup `.ftools`：搜索 + ＋ + − + 筛选 + 分组下拉，**单行常驻**，修
    // 1-07/1-09）： 全部矢量图标钮（R4 图标钮宽 28 + 裁定 A7 按钮高 26）都带 tooltip +
    // accessibleName（§11）；
    // 无选择时**隐藏**移除钮（1-08：不再常驻禁用）；「清空列表」仍在列表右键/＋
    // 菜单里（功能零丢失）。
    d.tools_row = new QWidget(d.panel);
    d.tools_row->setObjectName(QStringLiteral("pp-tools"));
    auto *tools = new QHBoxLayout(d.tools_row);
    tools->setContentsMargins(theme::Metrics::card_pad_x, 0, theme::Metrics::card_pad_x, 8);
    tools->setSpacing(6); // .ftools{gap:6px}
    d.search = new QLineEdit(d.tools_row);
    d.search->setObjectName(QStringLiteral("pp-search"));
    d.search->setPlaceholderText(tr("搜索文件名…")); // 1-10：省略号 = …（非三句点）
    d.search->setClearButtonEnabled(true);
    d.search->setFixedHeight(theme::Metrics::input_h);
    auto *add_btn = new QToolButton(d.tools_row);
    add_btn->setObjectName(QStringLiteral("pp-add"));
    add_btn->setToolTip(tr("添加文件 / 文件夹"));
    add_btn->setAccessibleName(tr("添加文件 / 文件夹")); // §11：图标钮必有 accessibleName
    icons::apply(add_btn, icons::Name::Add, d.tokens.text2, QColor(), 12);
    add_btn->setPopupMode(QToolButton::InstantPopup);
    // 裁定 A7（R4 表按钮高 26）：＋/− 是**按钮**，此前挂 input_h(24) 是 M4 遗留 → 换锚 btn_h。
    add_btn->setFixedHeight(theme::Metrics::btn_h);
    add_btn->setFixedWidth(theme::Metrics::icon_btn); // R4 图标钮宽 28
    auto *add_menu = new QMenu(add_btn);
    QAction *act_add_files = add_menu->addAction(tr("添加文件…"));
    act_add_files->setObjectName(QStringLiteral("pp-add-files"));
    QAction *act_add_dir = add_menu->addAction(tr("添加文件夹…"));
    act_add_dir->setObjectName(QStringLiteral("pp-add-dir"));
    add_menu->addSeparator();
    QAction *act_clear = add_menu->addAction(tr("清空列表"));
    act_clear->setObjectName(QStringLiteral("pp-clear-all"));
    add_btn->setMenu(add_menu);
    d.add_files_action = act_add_files;
    d.add_dir_action = act_add_dir;
    d.clear_action = act_clear;
    auto *remove_btn = new QToolButton(d.tools_row);
    remove_btn->setObjectName(QStringLiteral("pp-remove-sel"));
    remove_btn->setToolTip(tr("移除所选"));
    remove_btn->setAccessibleName(tr("移除所选")); // §11
    icons::apply(remove_btn, icons::Name::Remove, d.tokens.text2, QColor(), 12);
    remove_btn->setFixedHeight(theme::Metrics::btn_h); // 裁定 A7（同 ＋）
    remove_btn->setFixedWidth(theme::Metrics::icon_btn);
    remove_btn->setEnabled(false);
    remove_btn->setVisible(false); // 1-08：无选择时**隐藏**（refresh_status/sync_selection 驱动）
    // 筛选…（修 1-07 的语义可读性：矢量漏斗 + tooltip + accessibleName）：菜单三项 = 两个
    // **可见性**开关（仅显示已勾选 / 仅显示有例外的文件）+ 清除筛选 —— 全部只改可见性，
    // 与搜索/勾选/分组正交（§6.2 的正交口径，语义零新增：代理只读既有 roles）。
    auto *filter_btn = new QToolButton(d.tools_row);
    filter_btn->setObjectName(QStringLiteral("pp-filter"));
    filter_btn->setToolTip(tr("筛选…"));
    filter_btn->setAccessibleName(tr("筛选…")); // §11
    icons::apply(filter_btn, icons::Name::Filter, d.tokens.text2, QColor(), 12);
    filter_btn->setPopupMode(QToolButton::InstantPopup);
    filter_btn->setFixedHeight(theme::Metrics::btn_h); // 裁定 A7
    filter_btn->setFixedWidth(theme::Metrics::icon_btn);
    auto *filter_menu = new QMenu(filter_btn);
    QAction *act_filter_checked = filter_menu->addAction(tr("仅显示已勾选"));
    act_filter_checked->setCheckable(true);
    QAction *act_filter_exceptions = filter_menu->addAction(tr("仅显示有例外的文件"));
    act_filter_exceptions->setCheckable(true);
    filter_menu->addSeparator();
    QAction *act_filter_clear = filter_menu->addAction(tr("清除筛选"));
    filter_btn->setMenu(filter_menu);
    d.filter_btn = filter_btn;
    d.filter_checked = act_filter_checked;
    d.filter_exceptions = act_filter_exceptions;
    d.filter_clear = act_filter_clear;
    // UX-2（M7-R2D1）：勾选操作钮（pp-check-menu）—— 高频批量勾选不必依赖右键。菜单三项 =
    // 勾选全部 / 取消全部勾选 / 反选勾选（与文件列表右键同一组意图，作用于**可见行**）；
    // 快捷键进 tooltip（Ctrl+A 全选行 / Ctrl+Shift+A 勾选全部 / Ctrl+D 取消全部勾选）。
    auto *check_btn = new QToolButton(d.tools_row);
    check_btn->setObjectName(QStringLiteral("pp-check-menu"));
    check_btn->setToolTip(
        tr("勾选操作（Ctrl+A 全选行 · Ctrl+Shift+A 勾选全部 · Ctrl+D 取消全部勾选）"));
    check_btn->setAccessibleName(tr("勾选操作"));
    icons::apply(check_btn, icons::Name::CheckAll, d.tokens.text2, QColor(), 12);
    check_btn->setPopupMode(QToolButton::InstantPopup);
    check_btn->setFixedHeight(theme::Metrics::btn_h);
    check_btn->setFixedWidth(theme::Metrics::icon_btn);
    auto *check_menu = new QMenu(check_btn);
    QAction *act_check_all = check_menu->addAction(tr("勾选全部"));
    QAction *act_uncheck_all = check_menu->addAction(tr("取消全部勾选"));
    QAction *act_invert_checks = check_menu->addAction(tr("反选勾选"));
    act_check_all->setToolTip(tr("勾选全部（Ctrl+Shift+A）"));
    act_uncheck_all->setToolTip(tr("取消全部勾选（Ctrl+D）"));
    act_invert_checks->setToolTip(tr("反选勾选"));
    check_btn->setMenu(check_menu);
    d.check_menu_btn = check_btn;
    // GUI-R2：左栏下拉同样走 formkit 唯一构造点（原先逐控件 setStyleSheet 是第六条样式路径）
    {
        formkit::ComboSpec group_spec;
        group_spec.object_name = QStringLiteral("pp-group-mode");
        group_spec.accessible = tr("分组方式");
        group_spec.tooltip = tr("分组方式"); // 1-09：工具行并入后，语义提示进 tooltip
        d.group_mode = formkit::combo_box(d.tools_row, group_spec);
    }
    d.group_mode->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);
    d.group_mode->addItem(tr("按月份"), int(pp::GroupMode::Month));
    d.group_mode->addItem(tr("按相机型号"), int(pp::GroupMode::Camera));
    d.group_mode->addItem(tr("按源格式"), int(pp::GroupMode::SourceFormat));
    d.group_mode->addItem(tr("无分组"), int(pp::GroupMode::None));
    tools->addWidget(d.search, 1);
    tools->addWidget(add_btn);
    tools->addWidget(remove_btn);
    tools->addWidget(filter_btn);
    tools->addWidget(check_btn);
    tools->addWidget(d.group_mode);
    pv->addWidget(d.tools_row);
    d.add_btn = add_btn;
    d.remove_sel = remove_btn;

    // 文件列表（M4-T11：QListView → QTreeView；两列 = 勾选列 + 行内容列，分组节跨两列）；
    // M5-T5：列表与空态投放区同槽切页（修 1-06 —— 空态居中引导 + 显式"添加文件…"钮，
    // 且空态不渲染滚动条）。
    d.file_stack = new QStackedWidget(d.panel);
    d.file_stack->setObjectName(QStringLiteral("pp-file-stack"));
    d.view = new FileTreeView(d.file_stack);
    // M5-T14（§12-12/§11）：objectName 是双职 —— 骨架 QSS 钩子 + FileViewFocusFrameStyle 的
    // 系统焦点框抑制判定键（该代理样式在 Impl 构造期最早点已安装，按此名识别目标，不持指针）。
    d.view->setObjectName(QStringLiteral("pp-file-view"));
    d.file_delegate = new FileGroupDelegate(d.view); // M5-T5：持有以重放 tokens（A6）
    d.view->setModel(d.groups);
    d.view->setItemDelegate(d.file_delegate);
    d.view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d.view->setSelectionBehavior(QAbstractItemView::SelectRows);
    d.view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d.view->setRootIsDecorated(false);   // mockup：节头无展开箭头（.ghead 平铺）
    d.view->setIndentation(0);           // 节内行与节头同左沿（mockup）
    d.view->setHeaderHidden(true);       // mockup：无表头
    d.view->setUniformRowHeights(false); // 节头 26px / 文件行 58px（§7.5）
    d.view->setExpandsOnDoubleClick(true);
    d.view->setAllColumnsShowFocus(true);
    d.view->header()->setSectionResizeMode(FileGroupProxyModel::kCheckColumn, QHeaderView::Fixed);
    // 勾选列宽 = 单源函数（内距 6 + theme::Metrics::checkbox_px 13 + 间隙 8；A6 废除手写 6+15+8）
    d.view->header()->resizeSection(FileGroupProxyModel::kCheckColumn,
                                    FileGroupDelegate::check_column_width());
    d.view->header()->setSectionResizeMode(FileGroupProxyModel::kRowColumn, QHeaderView::Stretch);
    d.file_stack->addWidget(d.view);
    d.empty_state =
        build_empty_state(d.panel, &d.drop_zone, &d.drop_icon, &d.drop_text, &d.empty_add);
    d.file_stack->addWidget(d.empty_state);
    pv->addWidget(d.file_stack, 1);
    left_layout->addWidget(d.panel, 1); // 文件面板 = 卡的整个内容区（尺寸基准见 §9.1）
    left_col_layout->addWidget(d.left_card, 1);

    // 分类卡（mockup .cls-panel）：卡头（标题 + hint）+ 分类面板（T11）
    d.classify_card = new QFrame(d.left_col);
    d.classify_card->setObjectName(QStringLiteral("pp-card"));
    d.classify_card->setAttribute(Qt::WA_StyledBackground, true);
    auto *classify_layout = new QVBoxLayout(d.classify_card);
    classify_layout->setContentsMargins(0, 0, 0, 6);
    classify_layout->setSpacing(0);
    QLabel *classify_title = nullptr;
    QWidget *classify_head =
        make_card_header(d.classify_card, tr("分类"), &classify_title, nullptr);
    // M5-T5（1-14）：「1–9 键打标」不再常驻标题行 → 移入 tooltip（卡片标题 + 分类面板两处可及）
    classify_title->setToolTip(tr("1–9 键打标"));
    d.classify_hint = new ElidedLabel(classify_head);
    d.classify_hint->setObjectName(QStringLiteral("pp-card-hint"));
    d.classify_hint->setFont(theme::font(theme::Typography::hint_px));
    d.classify_hint->set_full_text(QString()); // 非运行态为空；运行中 → 「运行中只读」（G5）
    if (auto *head_layout = qobject_cast<QHBoxLayout *>(classify_head->layout()))
        head_layout->addWidget(d.classify_hint);
    classify_layout->addWidget(classify_head);
    d.classify_panel = new ClassifyPanel(d.classify_card);
    d.classify_panel->setToolTip(tr("1–9 键打标")); // 面板内 hover 也可及（1-14 的引导层）
    classify_layout->addWidget(d.classify_panel);
    left_col_layout->addWidget(d.classify_card, 0);

    // 中栏卡：输入预览（T10：卡头 = 文件名徽标 + 尺寸/位深/色彩空间 hint；卡体 = PreviewPanel）
    d.preview_card = new QFrame(d.splitter);
    d.preview_card->setObjectName(QStringLiteral("pp-card"));
    d.preview_card->setAttribute(Qt::WA_StyledBackground, true);
    // §3.2 min-width:0：中栏是**弹性列**（唯一吃窗口余量者），缩到最小时不得被预览内容顶住
    d.preview_card->setMinimumWidth(0);
    d.preview_card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *preview_layout = new QVBoxLayout(d.preview_card);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    preview_layout->setSpacing(0);
    QLabel *preview_title = nullptr;
    QWidget *preview_head =
        make_card_header(d.preview_card, tr("输入预览"), &preview_title, &d.preview_pill);
    d.preview_pill->setObjectName(QStringLiteral("pp-preview-pill")); // §9.3 pp-* 测试钩子
    d.preview_hint = new ElidedLabel(preview_head);
    d.preview_hint->setObjectName(QStringLiteral("pp-card-hint"));
    d.preview_hint->setFont(theme::font(theme::Typography::hint_px));
    if (auto *head_layout = qobject_cast<QHBoxLayout *>(preview_head->layout()))
        head_layout->addWidget(d.preview_hint); // addStretch(1) 之后 = 右对齐（§9.3 超长省略号）
    preview_layout->addWidget(preview_head);
    d.preview_panel = new PreviewPanel(d.preview_card); // 舞台/翻图/缩放/徽标/热键提示
    preview_layout->addWidget(d.preview_panel, 1);

    // 右栏：步骤内容区（QStacked 三页，W3 填内容）
    d.stack = new QStackedWidget(d.splitter);
    d.stack->setObjectName(QStringLiteral("pp-stack"));
    d.page_meta = new PageMeta(d.stack);
    d.page_output = new PageOutput(d.stack);
    d.page_run = new PageRun(d.stack);
    d.stack->addWidget(d.page_meta);   // 页 1 元数据
    d.stack->addWidget(d.page_output); // 页 2 输出
    d.stack->addWidget(d.page_run);    // 页 3 运行

    d.splitter->addWidget(d.left_col);
    d.splitter->addWidget(d.preview_card);
    d.splitter->addWidget(d.stack);
    d.stack->setMinimumWidth(theme::Metrics::right_width_min); // §3.2 右栏 520–680 可调
    d.stack->setMaximumWidth(theme::Metrics::right_width_max);
    d.stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // min-width:0（同上）
    // 栏宽固定轴（§3.2/§12-16）：左/右 = 固定轴（stretch 0 → 窗口缩放不动），中栏 = 弹性列
    // （stretch 1 → 吃全部余量）；1440 校准落点 = 330 / 487 / 587。
    d.splitter->setStretchFactor(0, 0);
    d.splitter->setStretchFactor(1, theme::Metrics::mid_stretch);
    d.splitter->setStretchFactor(2, theme::Metrics::right_stretch);
    d.splitter->setSizes(
        {theme::Metrics::left_width, theme::Metrics::mid_width_1440, theme::Metrics::right_width});

    // ---- 底栏（§3.1 状态栏 30px）：状态词 · 输出摘要 · [▶ 开始运行] ----
    // 与内容区以 hairline 分隔（修 5-13/§5-13，QSS 的 border-top）；栏内三种重量元素**分区**：
    // 状态点 + 状态词（语义色 + 600 字重）｜ 分隔细线 ｜ 输出摘要（--txt3 次级）｜ 实心主 CTA
    // 居右。
    d.bottombar = new QWidget(central);
    d.bottombar->setObjectName(QStringLiteral("pp-bottombar"));
    d.bottombar->setFixedHeight(theme::Metrics::bottom_height);
    auto *bottom = new QHBoxLayout(d.bottombar);
    bottom->setContentsMargins(theme::Metrics::statusbar_pad_x, 0, theme::Metrics::statusbar_pad_x,
                               0);
    bottom->setSpacing(theme::Metrics::statusbar_gap); // .statusbar{gap:8px}
    // M5-T3：状态点 = 形状（QSS background + border-radius 的 8px 圆，mockup .dot），不再是 ●
    // 字形。M5-T4：色调 = 动态属性 ppStatusTone 四档（§8：点与词同色；空/就绪绝不用红，
    // 色值仍单源在 theme.h 的 QSS 里，C1）。
    d.status_dot = new QLabel(d.bottombar);
    d.status_dot->setObjectName(QStringLiteral("pp-status-dot"));
    d.status_dot->setFixedSize(theme::Metrics::dot_px, theme::Metrics::dot_px);
    d.status = new ElidedLabel(d.bottombar); // §9.3：超长文案省略号，不折行
    d.status->setObjectName(QStringLiteral("pp-status"));
    d.status->setFont(theme::font(theme::Typography::small_px)); // 状态栏 11px
    d.status->set_full_text(tr("未添加文件"));
    // R-144：状态栏失败段可下钻（点击 → 运行页）—— 走窗口同一只事件过滤器（handle_event）
    d.status->installEventFilter(d.filter);
    d.status_sep = new QFrame(d.bottombar); // 段间分隔（修 5-13：栏内分区）
    d.status_sep->setObjectName(QStringLiteral("pp-status-sep"));
    d.status_sep->setAttribute(Qt::WA_StyledBackground, true);
    d.status_sep->setFixedSize(1, theme::Metrics::statusbar_sep_h);
    d.output_status = new ElidedLabel(d.bottombar);
    d.output_status->setObjectName(QStringLiteral("pp-output-status"));
    d.output_status->setFont(theme::font(theme::Typography::small_px)); // 状态栏 11px
    // A19（§3.2 骨架行）：第三段「剩余磁盘 1.2 TB」—— 次级读数（--txt3）+ 段前 hairline 分区
    d.disk_sep = new QFrame(d.bottombar);
    d.disk_sep->setObjectName(QStringLiteral("pp-disk-sep"));
    d.disk_sep->setAttribute(Qt::WA_StyledBackground, true);
    d.disk_sep->setFixedSize(1, theme::Metrics::statusbar_sep_h);
    d.disk_status = new ElidedLabel(d.bottombar);
    d.disk_status->setObjectName(QStringLiteral("pp-disk-status"));
    d.disk_status->setFont(theme::font(theme::Typography::small_px)); // 状态栏 11px
    bottom->addWidget(d.status_dot);
    bottom->addWidget(d.status);
    bottom->addWidget(d.status_sep);
    bottom->addWidget(d.output_status);
    bottom->addWidget(d.disk_sep);
    bottom->addWidget(d.disk_status);
    bottom->addStretch(1);
    d.start = new QPushButton(tr("开始运行"), d.bottombar);
    d.start->setObjectName(QStringLiteral("pp-start"));
    // 图标 = icons::play（矢量 ▶，mockup statusbar .btn-go 的 ▶）；色 = acc-on（实心 CTA 上的字色）
    icons::apply(d.start, icons::Name::Play, d.tokens.on_accent, d.tokens.text3, 12);
    d.start->setFixedHeight(theme::Metrics::go_height); // R4 主 CTA 22px（禁用 = 灰底灰字）
    d.start->setFont(theme::font(theme::Typography::caption_px, 700));
    d.start->setDefault(true);
    bottom->addWidget(d.start);
    outer->addWidget(d.bottombar);

    setCentralWidget(central);

    // ---- 无边框接线（§9.1 能力表）：顶栏 = 拖拽/HTCAPTION 区；交互控件全部排除 ----
    // 导航区 = 一整件（ShellNav 单控件）→ 整块排除（含段间 hairline 位，§9.1 硬约束 1）。
    d.frameless = plat::Frameless::attach(this, d.titlebar, {d.nav, d.presets_btn, d.settings_btn});
    if (d.frameless != nullptr)
        d.frameless->set_caption_buttons(d.cap_min, d.cap_max, d.cap_close);

    // 分类注册表 → 预览面板的热键提示表（§6.1「热键表随分类注册表动态生成」，消费 W1-T8 的
    // classify 接口）。T10 只**只读消费**：注册表实例由本窗口持有，T11 接管 CRUD/持久化/打标
    // 与勾选语义时共用同一实例（面板自动跟随，无需改面板）。
    d.preview_panel->set_class_registry(&d.classes);
    // M4-T11：左栏分类面板（§6.2）——同一注册表与同一文件模型（单源；分类只做组织 + 圈选）
    d.classify_panel->set_registry(&d.classes);
    d.classify_panel->set_model(d.model);

    d.refresh_theme(); // tokens 落地（明暗跟随系统）
}

void MainWindow::wire() {
    Impl &d = *impl_;

    connect(d.nav_meta, &QAction::triggered, this, [this] { set_current_page(1); });
    connect(d.nav_output, &QAction::triggered, this, [this] { set_current_page(2); });
    connect(d.nav_run, &QAction::triggered, this, [this] { set_current_page(3); });
    connect(d.settings_action, &QAction::triggered, this, &MainWindow::open_settings);
    connect(d.presets_action, &QAction::triggered, this, &MainWindow::manage_presets);

    // caption 三钮 → platform/frameless（§9.1 能力表：Windows = WM_SYSCOMMAND(SC_*)；
    // 其它平台 = showMinimized/showMaximized/close）
    if (d.frameless != nullptr) {
        connect(d.cap_min, &QPushButton::clicked, this,
                [this] { impl_->frameless->trigger(plat::CaptionAction::Minimize); });
        connect(d.cap_max, &QPushButton::clicked, this, [this] {
            impl_->frameless->trigger(plat::CaptionAction::Maximize);
            impl_->update_caption_buttons();
        });
        connect(d.cap_close, &QPushButton::clicked, this,
                [this] { impl_->frameless->trigger(plat::CaptionAction::Close); });
    }
    // 明暗跟随系统（§9.2 末行）：系统色板变化 → 重新落地 tokens
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { impl_->set_theme_mode(preferred_theme_mode()); });

    connect(d.add_files_action, &QAction::triggered, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("添加文件"), QString(),
            tr("图片 (*.jpg *.jpeg *.png *.tif *.tiff *.webp *.jxl *.heic *.heif *.avif *.bmp "
               "*.gif *.tga);;所有文件 (*)"));
        if (!files.isEmpty())
            add_paths(files);
    });
    connect(d.add_dir_action, &QAction::triggered, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("添加文件夹"));
        if (!dir.isEmpty())
            add_paths(QStringList{dir});
    });
    connect(d.clear_action, &QAction::triggered, this, [this] { impl_->model->clear(); });
    // M5-T5：空态投放区的显式「添加文件…」钮复用同一入口（§8「显式按钮」；功能零分叉）
    connect(d.empty_add, &QPushButton::clicked, this,
            [this] { impl_->add_files_action->trigger(); });
    connect(d.remove_sel, &QToolButton::clicked, this, [this] { impl_->remove_selected(); });
    // M5-T5 筛选（修 1-07 的语义可读性）：三项 = 两个可见性开关 + 清除。
    // 过滤只改可见性（§6.2 正交口径）：勾选集合/计数/分组键都不变；分组视图随可见性重建。
    const auto apply_filter = [this] {
        Impl &impl_ref = *impl_;
        impl_ref.groups->rebuild();
        impl_ref.rebuild_group_view();
        impl_ref.apply_empty_state();
        refresh_status();
    };
    connect(d.filter_checked, &QAction::toggled, this, [this, apply_filter](bool on) {
        impl_->proxy->set_checked_only(on);
        apply_filter();
    });
    connect(d.filter_exceptions, &QAction::toggled, this, [this, apply_filter](bool on) {
        impl_->proxy->set_exceptions_only(on);
        apply_filter();
    });
    connect(d.filter_clear, &QAction::triggered, this, [this, apply_filter] {
        // R-153：原实现 clear_filters + 两次 setChecked 各自触发 apply_filter（最多 3 连刷）。
        // 这里屏蔽两枚开关的 toggled，只走一次 apply_filter。
        Impl &impl_ref = *impl_;
        impl_ref.proxy->clear_filters(); // R-143：含分类过滤档（chip `.on` 随之归「全部」）
        {
            const QSignalBlocker b1(impl_ref.filter_checked);
            const QSignalBlocker b2(impl_ref.filter_exceptions);
            impl_ref.filter_checked->setChecked(false);
            impl_ref.filter_exceptions->setChecked(false);
        }
        if (impl_ref.classify_panel != nullptr)
            impl_ref.classify_panel->set_filter_class_id(QString());
        apply_filter();
    });
    connect(d.search, &QLineEdit::textChanged, this, [this](const QString &text) {
        Impl &impl_ref = *impl_;
        impl_ref.proxy->setFilterFixedString(text);
        impl_ref.groups
            ->rebuild(); // 过滤 = 可见性变化 → 分组节重建（勾选状态住在源模型，不受影响）
        impl_ref.rebuild_group_view();
    });
    connect(d.view, &QTreeView::doubleClicked, this, [this](const QModelIndex &idx) {
        Impl &impl_ref = *impl_;
        if (!idx.isValid() || impl_ref.groups->is_group(idx))
            return;
        // R-152（数据完整性）：过滤态下 idx 是**分组代理**行号，必须走完整的
        // proxy->mapToSource(groups->mapToSource(idx)) 链才能拿到根 FileListModel 行号
        // （与 :2701/:3377/:3444 三处同口径）。原先漏 proxy 一层 → 搜索/仅勾选过滤后双击
        // 打开的 EXIF 编辑器把例外写到**别的文件**上。
        open_exif_editor_row(impl_ref.proxy->mapToSource(impl_ref.groups->mapToSource(idx)).row());
    });
    connect(d.view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { impl_->sync_selection(); });
    // 折叠记忆（§6.2）：过滤/摘要重建后不丢用户的展开态。程序化落地（apply_group_view_state）
    // 期间的 collapsed/expanded 不记为用户操作（否则"重建 → 程序折叠 → 记忆被自己清掉"）。
    connect(d.view, &QTreeView::collapsed, this, [this](const QModelIndex &idx) {
        Impl &impl_ref = *impl_;
        if (impl_ref.applying_group_state)
            return;
        if (idx.isValid() && impl_ref.groups->is_group(idx))
            impl_ref.groups->set_collapsed(impl_ref.groups->group_key_at(idx.row()), true);
    });
    connect(d.view, &QTreeView::expanded, this, [this](const QModelIndex &idx) {
        Impl &impl_ref = *impl_;
        if (impl_ref.applying_group_state)
            return;
        if (idx.isValid() && impl_ref.groups->is_group(idx))
            impl_ref.groups->set_collapsed(impl_ref.groups->group_key_at(idx.row()), false);
    });
    // 分组模式（§6.2：月份 | 相机型号 | 源格式 + 无分组）——键由模型出、节名由代理出
    connect(d.group_mode, &QComboBox::currentIndexChanged, this, [this](int) {
        Impl &impl_ref = *impl_;
        impl_ref.set_group_mode(pp::GroupMode(impl_ref.group_mode->currentData().toInt()));
    });
    // G5（2026-09-20 R1 修订）：底栏开始按钮运行中保持"开始"且禁用 → 取消唯一入口 = 运行页
    connect(d.start, &QPushButton::clicked, this, &MainWindow::on_start);

    connect(d.model, &FileListModel::content_changed, this,
            [this] { impl_->on_content_changed(); });
    connect(d.model, &FileListModel::exception_changed, this, [this](std::size_t) {
        impl_->sync_exceptions();
        // M4-T12：例外是 preview_effective 的第三个入参（§5.1 源 ⊕ 批量 ⊕ 例外）→ 例外一变，
        // 生效值显示必须随之刷新（跟随项快照里带的就是这一份例外）
        impl_->sync_selection();
    });
    // M4-T11：勾选集合变化 → 底栏"已选 N / M"由 content_changed → refresh_status 承担；分类面板
    // 的高亮只跟随**选中项**（sync_selection 里刷新），与勾选集合无关（勾选不改变归属）。
    // 分组摘要批次就绪 → 分组视图重建（节头/节数变化；重建后恢复展开态与选中）
    connect(d.model, &FileListModel::group_source_changed, this,
            [this] { impl_->rebuild_group_view(); });
    // 分组结构变化（重建/reset）→ 跨列节头 + 展开态 + 选中恢复
    connect(d.groups, &QAbstractItemModel::modelReset, this,
            [this] { impl_->rebuild_group_view(); });

    // ---- M4-T11：分类面板接线（§6.2）----
    // CRUD/颜色/热键改动 → 立即落盘 classes.json（§6.2「打标即写 registry」的持久化面）
    connect(d.classify_panel, &ClassifyPanel::registry_changed, this,
            [this] { impl_->save_classes(); });
    // 被拒操作的非模态反馈（不弹对话框：面板的对话框只在用户主动打开的菜单项里）
    connect(d.classify_panel, &ClassifyPanel::error_occurred, this, [](const QString &message) {
        // R-278：诊断输出收 logger（原 std::fprintf 绕过日志面）
        pp::log_warn("ui", "classify", message.toStdString());
    });
    // M7-R2D1 / R-143：chip 过滤档 → 左栏过滤代理（列表按类过滤；「全部」/再点 = 清除）
    connect(d.classify_panel, &ClassifyPanel::class_filter_requested, this,
            [this](const QString &class_id) {
                Impl &impl_ref = *impl_;
                impl_ref.proxy->set_class_filter(class_id);
                impl_ref.groups->rebuild();
                impl_ref.rebuild_group_view();
                impl_ref.apply_empty_state();
                refresh_status();
            });

    // ---- UX-2（M7-R2D1）：批量勾选入口 —— 工具钮菜单 + 快捷键（同一组意图，作用于可见行）----
    if (d.check_menu_btn != nullptr && d.check_menu_btn->menu() != nullptr) {
        const QList<QAction *> check_actions = d.check_menu_btn->menu()->actions();
        if (check_actions.size() == 3) {
            connect(check_actions[0], &QAction::triggered, this,
                    [this] { impl_->apply_visible_checks(Impl::CheckOp::All); });
            connect(check_actions[1], &QAction::triggered, this,
                    [this] { impl_->apply_visible_checks(Impl::CheckOp::None); });
            connect(check_actions[2], &QAction::triggered, this,
                    [this] { impl_->apply_visible_checks(Impl::CheckOp::Invert); });
        }
    }
    // 快捷键挂在**文件列表**焦点域（WidgetWithChildrenShortcut）：文本输入控件里 Ctrl+A 等
    // 仍归控件自己（搜索框选中文字），不被应用级快捷键抢走。
    {
        QAction *select_all = new QAction(tr("全选行"), this);
        select_all->setShortcut(QKeySequence::SelectAll); // Ctrl+A（Qt 标准）
        select_all->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        select_all->setToolTip(tr("全选行（Ctrl+A）"));
        d.view->addAction(select_all);
        connect(select_all, &QAction::triggered, this, [this] {
            if (impl_->view != nullptr)
                impl_->view->selectAll();
        });
        QAction *quick_check = new QAction(tr("勾选全部"), this);
        quick_check->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
        quick_check->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        quick_check->setToolTip(tr("勾选全部（Ctrl+Shift+A）"));
        d.view->addAction(quick_check);
        connect(quick_check, &QAction::triggered, this,
                [this] { impl_->apply_visible_checks(Impl::CheckOp::All); });
        QAction *quick_uncheck = new QAction(tr("取消全部勾选"), this);
        quick_uncheck->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        quick_uncheck->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        quick_uncheck->setToolTip(tr("取消全部勾选（Ctrl+D）"));
        d.view->addAction(quick_uncheck);
        connect(quick_uncheck, &QAction::triggered, this,
                [this] { impl_->apply_visible_checks(Impl::CheckOp::None); });
    }

    // ---- M4-T10：中栏预览面板接线（§6.1）----
    // 卡头（pill = 文件名，hint = 尺寸·位深·色彩空间）由面板的 display_changed 驱动；
    // 热键提示表在 build_ui 里已 set_class_registry(&classes)（动态生成）。
    connect(d.preview_panel, &PreviewPanel::display_changed, this, [this] {
        Impl &impl_ref = *impl_;
        const QString file_name = impl_ref.preview_panel->current_file_name();
        impl_ref.preview_pill->set_full_text(file_name);
        impl_ref.preview_pill->setVisible(!file_name.isEmpty()); // 原型：有内容才画药丸
        impl_ref.preview_hint->set_full_text(impl_ref.preview_panel->current_info_text());
    });
    // R-151（翻图/打标对象错位）：←/→ 翻图同步左栏选中 —— 热键打标作用于"所见"的那一张。
    // （仅翻图动作发 navigated；选中驱动的 set_current 不发，故不会吞多选。）
    connect(d.preview_panel, &PreviewPanel::navigated, this,
            [this](int index) { impl_->select_row(index); });
    // 打标接线（W2-T11 接通）：预览面板的 `class_hotkey`（'1'..'9' 打标 / '0' 清除）→ 注册表。
    // 归属对象 = 当前选中集（多选 = 批量打标，与勾选/批量语义一致）；无选中 → 预览当前项。
    connect(d.preview_panel, &PreviewPanel::class_hotkey, this, [this](QChar key) {
        Impl &impl_ref = *impl_;
        const char k = key.toLatin1();
        if (k == '0') {
            impl_ref.apply_class_to_selection(QString());
            return;
        }
        for (const pp::ClassDef &c : impl_ref.classes.classes) {
            if (c.hotkey == k) {
                impl_ref.apply_class_to_selection(QString::fromStdString(c.id));
                return;
            }
        }
    });

    // 探测完成 → alpha 预检。注意：这不是 U4 的 ready→apply_thumb 连接（那个由模型内部自理），
    // 只是消费方对同一信号只读旁路，不与模型竞争写入。
    connect(d.thumbs, &Thumbnailer::ready, this,
            [this](int, const QString &, const pp::ImageInfo &info, bool ok, const QString &,
                   const QImage &) {
                if (!ok || !info.has_alpha || impl_->running || impl_->cached_alpha)
                    return;
                impl_->cached_alpha = true;
                impl_->page_output->set_batch_has_alpha(true);
            });

    connect(d.poll, &QTimer::timeout, this, [this] {
        if (impl_->sched && impl_->run_total > 0 && impl_->run_terminal >= impl_->run_total) {
            on_scheduler_done();
        }
    });
    // M4-T14：日志尾（运行期每 500ms；结束态由 on_scheduler_done 再读一次兜底）
    connect(d.log_tail, &QTimer::timeout, this, [this] { impl_->push_log_tail(); });
    // R-024：状态栏合并刷新（运行期逐文件事件洪峰 → 150ms 一次全量刷新）
    connect(d.status_flush, &QTimer::timeout, this, [this] { refresh_status(); });

    connect(d.page_meta, &PageMeta::rules_changed, this, [this] { refresh_status(); });
    connect(d.page_meta, &PageMeta::open_editor_requested, this,
            &MainWindow::open_exif_editor_path);
    connect(d.page_meta, &PageMeta::clear_exception_requested, this,
            [this](const QString &path) { impl_->clear_exception(path); });

    connect(d.page_output, &PageOutput::config_changed, this, [this] { refresh_status(); });
    connect(d.page_output, &PageOutput::manage_presets_requested, this,
            &MainWindow::manage_presets);
    connect(d.page_output, &PageOutput::open_settings_requested, this, &MainWindow::open_settings);
    // M4-T13：预设卡（mockup output-dark 的「预设」卡）——文件 I/O 与 rules 合并的单一持有者仍是
    // MainWindow（与 manage_presets 同一批私有实现），页面只发意图信号。
    connect(d.page_output, &PageOutput::preset_load_requested, this,
            [this](const QString &path) { impl_->load_preset_file(path); });
    connect(d.page_output, &PageOutput::preset_save_requested, this,
            [this](const QString &name) { impl_->save_preset_named(name); });
    connect(d.page_output, &PageOutput::preset_saveas_requested, this,
            [this] { manage_presets(); });
    connect(d.page_output, &PageOutput::preset_delete_requested, this,
            [this](const QString &path) { impl_->delete_preset_file(path); });
    connect(d.page_output, &PageOutput::output_template_changed, this,
            [this](const QString &) { refresh_status(); });

    connect(d.page_run, &PageRun::cancel_requested, this, &MainWindow::on_cancel);
    connect(d.page_run, &PageRun::open_output_requested, this, [](const QString &dir) {
        if (!dir.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(d.page_run, &PageRun::open_logs_requested, this, [this] { impl_->open_logs(); });
    // M5-T9（§7.3 操作行「再次运行」）：页只发意图 —— 回收 = 与底栏「开始运行」同一条路径
    // （预检/锁定/调度/状态都在外壳，on_start 里已有全部守卫）。
    // R-006/R-146：改经 on_run_again（补齐语义 + 含失败先确认），不再直接 on_start。
    connect(d.page_run, &PageRun::run_again_requested, this, &MainWindow::on_run_again);

    // ---- UX-1（M7-WP6/M7-R2D1）：文件列表右键菜单（公共件 = ui/contextmenu::attach_card_menu）
    // ---- + UX-2 勾选三项。菜单内容见 docs/ux-review-14.md §1 表第 ① 行；运行中按 G5 只读置灰。
    contextmenu::attach_card_menu(d.view, [this] {
        Impl &d2 = *impl_;
        // UX-2：勾选三项作用于**当前可见（过滤后）**的行（§2 改进方案 1）。运行集是全局的，
        // 故作用域写进菜单文案（有筛选时显示"（可见 N 行）"）—— 不静默扩大作用面。
        const auto visible_rows = [&d2] {
            std::vector<std::size_t> rows;
            for (int r = 0; r < d2.proxy->rowCount(); ++r) {
                const QModelIndex src = d2.proxy->mapToSource(d2.proxy->index(r, 0));
                if (src.isValid())
                    rows.push_back(std::size_t(src.row()));
            }
            return rows;
        };
        const bool filtered = d2.proxy->filtering() || !d2.search->text().isEmpty();
        contextmenu::FileListIntents in;
        in.running = d2.running;
        in.check_all = [&d2] { d2.apply_visible_checks(Impl::CheckOp::All); };
        in.uncheck_all = [&d2] { d2.apply_visible_checks(Impl::CheckOp::None); };
        in.invert_checks = [&d2] { d2.apply_visible_checks(Impl::CheckOp::Invert); };
        in.edit_exif = [this] {
            if (impl_->view != nullptr && impl_->view->currentIndex().isValid())
                open_exif_editor_row(
                    impl_->proxy
                        ->mapToSource(impl_->groups->mapToSource(impl_->view->currentIndex()))
                        .row());
        };
        in.remove_selected = [this] { impl_->remove_selected(); };
        in.clear_list = [this] { impl_->model->clear(); };
        // 「按分类打标 →」子菜单：分类注册表动态生成（含"清除归属"）
        for (const pp::ClassDef &c : d2.classes.classes) {
            in.tag_targets.emplace_back(QString::fromStdString(c.name), [&d2, id = c.id] {
                d2.apply_class_to_selection(QString::fromStdString(id));
            });
        }
        in.tag_targets.emplace_back(tr("清除归属"),
                                    [&d2] { d2.apply_class_to_selection(QString()); });
        in.copy_path = [&d2] {
            const QModelIndex idx = d2.view != nullptr ? d2.view->currentIndex() : QModelIndex();
            if (!idx.isValid())
                return;
            const int row = d2.proxy->mapToSource(d2.groups->mapToSource(idx)).row();
            if (row < 0 || std::size_t(row) >= d2.model->size())
                return;
            if (QClipboard *clip = QGuiApplication::clipboard())
                clip->setText(
                    QString::fromStdString(d2.model->row(std::size_t(row)).entry.src.string()));
        };
        std::vector<contextmenu::Item> items = contextmenu::file_list_actions(in);
        if (filtered) {
            // 作用域提示（UX-2：过滤出 5 行点"勾选全部"是勾 5 行还是 24 行 —— 文案说清）
            const QString scope = tr("（可见 %1 行）").arg(visible_rows().size());
            for (contextmenu::Item &item : items) {
                if (!item.separator && !item.text.isEmpty())
                    item.text += scope;
            }
        }
        return items;
    });
}

// ---------------------------------------------------------------------------
// 公开 API
// ---------------------------------------------------------------------------

void MainWindow::add_paths(const QStringList &paths) {
    if (paths.isEmpty())
        return;
    impl_->model->add_paths(paths);
}

void MainWindow::set_current_page(int one_based) {
    Impl &d = *impl_;
    const int index = std::clamp(one_based, 1, 3) - 1;
    d.stack->setCurrentIndex(index);
    QAction *action = index == 0 ? d.nav_meta : (index == 1 ? d.nav_output : d.nav_run);
    if (action != nullptr && !action->isChecked())
        action->setChecked(true);
}

void MainWindow::set_offline_maps(bool off) {
    // M5-T12/A30：banner 本体（文案/配色）已在 mapwidget.cpp 的 banner 区中性化 —— 本外壳只切
    // 离线态；**令牌化的中性底/字由元数据页（PageMeta::set_tokens）铺**（令牌单源在宿主侧，
    // mapwidget 不持令牌、也不再自持任何硬编码语义色）。
    const QList<pp::map::MapWidget *> maps = findChildren<pp::map::MapWidget *>();
    for (pp::map::MapWidget *map : maps)
        map->set_offline(off);
}

void MainWindow::save_session() {
    Impl &d = *impl_;
    d.settings.last_format = d.page_output->current_format().toStdString();
    d.settings.last_out_root = d.page_output->out_root().toStdString();
    d.settings.last_preset = d.last_preset_path.toStdString();
    pp::save_settings(pp::platform::settings_file(), d.settings);
    d.save_classes();        // §6.2：分类归属随会话落盘（打标期已即时写；此处兜底）
    d.save_splitter_state(); // §9.1：三栏尺寸持久化（QSettings）
}

// ---------------------------------------------------------------------------
// M4-T11：左栏（勾选 / 自动分组 / 分类面板）
// ---------------------------------------------------------------------------

void MainWindow::Impl::rebuild_group_view() {
    if (view == nullptr || groups == nullptr)
        return;
    // 视图侧状态在模型 reset 之后是**延迟**重建的（QTreeView 内部 delayed layout）：此刻调
    // expandAll()/collapse() 会与随后到来的布局互相覆盖。故统一推迟到下一轮事件循环落地。
    QTimer::singleShot(0, w, [this] { apply_group_view_state(); });
}

void MainWindow::Impl::apply_group_view_state() {
    if (view == nullptr || groups == nullptr)
        return;
    // R-252 时序锚点③「重入成对」：applying_group_state 置位/复位必须成对；重入 = 程序化
    // 展开/折叠被误记为用户操作（污染折叠记忆）。
    Q_ASSERT(!applying_group_state);
    applying_group_state = true; // 程序化展开/折叠不写入折叠记忆（见 collapsed/expanded 接线）
    // 节头跨两列（mockup .ghead 通栏；节头本身不参与勾选/选择）
    for (int g = 0; g < groups->group_count(); ++g) {
        const QModelIndex idx = groups->group_index(g);
        if (idx.isValid())
            view->setFirstColumnSpanned(g, QModelIndex(), true);
    }
    // 重建（reset）会丢选中 → 按路径**整组**恢复（R-153：多选 5 行后敲过滤字符不再塌缩成 1 行；
    // 被搜索过滤隐藏时不越权恢复）。
    // 注意顺序：QTreeView::scrollTo（setCurrentIndex 内部）会**展开**当前项的祖先，故选中恢复
    // 必须排在展开态落地**之前**，否则用户的折叠会被"露出当前项"覆盖。
    // R-252 时序锚点②「选中恢复先于 expandAll」——顺序即契约，勿调换下一行与 expandAll。
    restore_selection_by_paths(last_selected_paths);
    // 展开态：默认全展开（mockup：节头 + 行平铺）；用户折叠过的节按代理记的键保持折叠（最终态）
    view->expandAll();
    for (int g = 0; g < groups->group_count(); ++g) {
        if (!groups->is_collapsed(groups->group_key_at(g)))
            continue;
        const QModelIndex idx = groups->group_index(g);
        if (idx.isValid())
            view->collapse(idx);
    }
    applying_group_state = false;
}

void MainWindow::Impl::restore_selection_by_path(const QString &path) {
    restore_selection_by_paths(path.isEmpty() ? QStringList{} : QStringList{path});
}

// R-153：分组重建后**整组**恢复选中（不止首个）。被过滤隐藏的行不越权恢复（与旧口径一致）。
void MainWindow::Impl::restore_selection_by_paths(const QStringList &paths) {
    if (view->selectionModel() == nullptr || paths.isEmpty())
        return;
    QItemSelection selection;
    QModelIndex first;
    for (const QString &path : paths) {
        for (std::size_t i = 0; i < model->size(); ++i) {
            if (QString::fromStdString(model->row(i).entry.src.string()) != path)
                continue;
            const QModelIndex src = model->index(int(i), 0);
            const QModelIndex filtered = proxy->mapFromSource(src);
            if (!filtered.isValid())
                break; // 被搜索过滤隐藏
            const QModelIndex group_idx = groups->mapFromSource(filtered);
            if (!group_idx.isValid())
                break;
            if (!first.isValid())
                first = group_idx;
            selection.select(group_idx, group_idx);
            break;
        }
    }
    if (selection.isEmpty())
        return;
    view->selectionModel()->select(selection,
                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (first.isValid())
        view->setCurrentIndex(first);
}

// R-151：翻图（PreviewPanel::navigated）同步左栏选中 —— "所见即所标"。
void MainWindow::Impl::select_row(int row) {
    if (view == nullptr || view->selectionModel() == nullptr || row < 0 ||
        std::size_t(row) >= model->size())
        return;
    const QModelIndex filtered = proxy->mapFromSource(model->index(row, 0));
    if (!filtered.isValid())
        return; // 被过滤隐藏：不动选中（保持旧口径的"不越权"）
    const QModelIndex group_idx = groups->mapFromSource(filtered);
    if (!group_idx.isValid())
        return;
    view->setCurrentIndex(group_idx);
    view->selectionModel()->select(group_idx,
                                   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void MainWindow::Impl::set_group_mode(pp::GroupMode mode) {
    model->set_group_mode(mode);  // 键（§6.2 三态 + 无分组）
    groups->set_group_mode(mode); // 节名文案
    groups->rebuild();
    rebuild_group_view();
}

// ---------------------------------------------------------------------------
// M4-T9：主题 tokens 落地 / 三栏持久化 / caption 字形
// ---------------------------------------------------------------------------

// M5-T3：骨架图标的两态取色（Normal = --txt2 / --txt；Disabled = .dim 预合成色 —— 与 QSS 同源）。
// 图标名同时挂到 ppIcon 属性（icons::apply 内），供 --ui-smoke 的 icons 探针逐钮断言。
void MainWindow::Impl::apply_chrome_icons() {
    // 纯图标钮的**可见面只有图标**（§3.3/§11）：QToolButton 在 QAction 使能态变化时会把 action 的
    // 文案同步回按钮（实测：置灰瞬间文案重现），必须在同一次重放里清掉；否则一旦图标再被清空，
    // QCommonStyle 会因"图标空 + 文本非空"回落成文本渲染（§3.3 禁字形充当图标）。
    if (presets_btn != nullptr) {
        icons::apply(presets_btn, icons::Name::Star, tokens.text2, tokens.icon_disabled_fg, 15);
        presets_btn->setText(QString());
    }
    if (settings_btn != nullptr) {
        icons::apply(settings_btn, icons::Name::Gear, tokens.text2, tokens.icon_disabled_fg, 15);
        settings_btn->setText(QString());
    }
    if (add_btn != nullptr)
        icons::apply(add_btn, icons::Name::Add, tokens.text2, tokens.text3, 12);
    if (remove_sel != nullptr)
        icons::apply(remove_sel, icons::Name::Remove, tokens.text2, tokens.text3, 12);
    if (filter_btn != nullptr)
        icons::apply(filter_btn, icons::Name::Filter, tokens.text2, tokens.text3, 12);
    if (empty_add != nullptr) // 空态显式钮的 ＋（矢量；色 = accent —— 投放引导的邀请语义）
        icons::apply(empty_add, icons::Name::Add, tokens.accent, tokens.text3, 12);
    if (start != nullptr)
        icons::apply(start, icons::Name::Play, tokens.on_accent, tokens.text3, 12);
    if (cap_min != nullptr)
        static_cast<CaptionButton *>(cap_min)->set_colors(tokens.text, tokens.text);
    if (cap_max != nullptr)
        static_cast<CaptionButton *>(cap_max)->set_colors(tokens.text, tokens.text);
    if (cap_close != nullptr)
        static_cast<CaptionButton *>(cap_close)->set_colors(tokens.text, tokens.close_hover_fg);
}

void MainWindow::Impl::set_theme_mode(theme::ThemeMode mode) {
    theme_mode = mode;
    refresh_theme();
}

void MainWindow::Impl::refresh_theme() {
    // R-253（M7-WP9）分段：原 142 行单函数 → 四段（基座 QSS / 面板重放 / chrome 图标 /
    // 效果器+DWM），调用顺序与原行序逐字一致，行为零变化。
    theme_apply_base();
    theme_replay_panels();
    theme_replay_chrome();
    theme_apply_effects();
}

// 段1（R-253）：令牌取值 + 调色板/字族 + 骨架 QSS。
void MainWindow::Impl::theme_apply_base() {

    tokens = theme::tokens(theme_mode);
    // 调色板 + 基准字族（§9.2 排印）：全局生效，既有页面/对话框随之明暗
    QApplication::setPalette(theme::palette(tokens));
    QApplication::setFont(theme::font(theme::Typography::base_px));
    // 骨架 QSS（只作用于 pp-* 钩子）；顶栏图标/分段导航随主题改色。
    // §11/A18：分段导航**禁用系统虚线框**（`outline: none`）—— 焦点指示只有自绘的 accent 1px 环
    // （ShellNav::paintEvent），与设置弹窗的"可见焦点指示单源"同口径。
    // M5-T14（§12-12/§11）：文件列表视图的系统焦点框（Windows11/Fluent 样式 = 当前行全部列上
    // 的「1px 白 + 1px 黑」指示框）会盖掉左缘 2px accent 选中条（真实平台 T13 取证：
    // filelist-selprobe after=[#ffffff,#ffffff,#000000,#000000,…]）。此处 `outline: none` 是
    // **口径声明**（`filelist-geom` 的 `focus-frame-off` 断言即钉在本串上），但该 QSS 路径对
    // QTreeView 的焦点框**不生效** —— 绘制调用 `style()->drawPrimitive(PE_FrameFocusRect, …)`
    // 不带 widget（QTBUG-4281），规则查不到 → 实际收口在 FileViewFocusFrameStyle（见其说明）。
    // 选中面本体由委托自绘（accent-dim 底 + 左缘 2px accent 条，修 3-17）。
    w->setStyleSheet(theme::style_sheet(tokens) +
                     QStringLiteral("QWidget#pp-nav-strip { outline: none; }\n"
                                    "QTreeView#pp-file-view { outline: none; }\n"));
}

// 段2（R-253）：各面板/页面 tokens 重放 + 左栏微件 QSS + 空态投放区。
void MainWindow::Impl::theme_replay_panels() {
    // M4-T10：预览面板的本地 QSS（徽标/chip/底条）随 tokens 重放（舞台框仍走上面的骨架 QSS）
    if (preview_panel != nullptr)
        preview_panel->set_tokens(tokens);
    // M4-T11：分类面板（行字色/计数色/新建钮）随 tokens 重放
    if (classify_panel != nullptr)
        classify_panel->set_tokens(tokens);
    // M4-T12：元数据页（关键信息卡/生效对照块/生效坐标框/折叠入口/浅色卡阴影）随 tokens 重放
    if (page_meta != nullptr)
        page_meta->set_tokens(tokens);
    // M4-T13：输出页（磁贴/药丸开关/卡片 QSS/页签/浅色卡阴影）随 tokens 重放
    if (page_output != nullptr)
        page_output->set_tokens(tokens);
    // R-009 收尾（M7-R2D1）：运行页令牌随主题切换重放 —— 原 theme_replay_panels 只重放
    // preview/classify/page_meta/page_output
    // 四件，主题切换后运行页仍持构造期令牌（浅色下整页深色）。
    if (page_run != nullptr)
        page_run->set_tokens(tokens);
    // M4-T11：左栏控件样式（mockup .mini-btn / .search / .combo；tokens 单源）
    {
        const QString mini =
            QStringLiteral("QToolButton#pp-add, QToolButton#pp-remove-sel, QToolButton#pp-filter "
                           "{ background:%1; "
                           "border:1px solid %2; border-radius:%3px; color:%4; "
                           "font-weight:600; padding:0 %5px; }"
                           "QToolButton#pp-add:disabled, QToolButton#pp-remove-sel:disabled, "
                           "QToolButton#pp-filter:disabled { color:%6; }")
                .arg(theme::css_color(tokens.control), theme::css_color(tokens.control_bd))
                .arg(theme::Metrics::control_radius)
                .arg(theme::css_color(tokens.text2), QString::number(theme::Metrics::pill_pad_x),
                     theme::css_color(tokens.text3));
        const QString input =
            QStringLiteral("background:%1; border:1px solid %2; "
                           "border-radius:%3px; color:%4; padding:0 8px;")
                .arg(theme::css_color(tokens.control), theme::css_color(tokens.control_bd))
                .arg(theme::Metrics::control_radius)
                .arg(theme::css_color(tokens.text));
        if (add_btn != nullptr)
            add_btn->setStyleSheet(mini);
        if (remove_sel != nullptr)
            remove_sel->setStyleSheet(mini);
        if (filter_btn != nullptr)
            filter_btn->setStyleSheet(mini);
        if (search != nullptr)
            search->setStyleSheet(input);
        // group_mode 不再逐控件 setStyleSheet —— GUI-R2：下拉样式单源 = theme::formkit_qss
    }
    // M5-T5：文件行委托的 tokens 重放（A6 —— 状态胶囊/注释/例外标记/勾选框的取色单源）
    if (file_delegate != nullptr)
        file_delegate->set_tokens(tokens);
    // M5-T5：空态投放区（修 1-06）—— accent 档框 + 居中引导（§8）；图标按 dpr 光栅化
    if (drop_zone != nullptr) {
        drop_zone->setStyleSheet(
            QStringLiteral("QFrame#pp-drop-zone { background:%1; "
                           "border:1px solid %2; border-radius:%3px; }")
                .arg(theme::css_color(tokens.accent_dim), theme::css_color(tokens.accent_bd))
                .arg(theme::Metrics::card_radius));
    }
    if (drop_text != nullptr)
        drop_text->setStyleSheet(QStringLiteral("color:%1").arg(theme::css_color(tokens.text2)));
    if (empty_add != nullptr) {
        empty_add->setStyleSheet(
            QStringLiteral("QPushButton#pp-empty-add { background:%1; border:1px solid %2; "
                           "border-radius:%3px; color:%4; padding:0 %5px; }"
                           "QPushButton#pp-empty-add:disabled { color:%6; }")
                .arg(theme::css_color(tokens.card), theme::css_color(tokens.accent_bd))
                .arg(theme::Metrics::btn_radius)
                .arg(theme::css_color(tokens.accent), QString::number(theme::Metrics::pill_pad_x),
                     theme::css_color(tokens.text3)));
    }
}

// 段3（R-253）：chrome 图标（QIcon 取色不吃 QSS → 主题切换显式换色）+ 分段导航令牌。
void MainWindow::Impl::theme_replay_chrome() {
    // 顶栏/底栏/文件工具行的图标随主题重放（QIcon 取色不吃 QSS → 主题切换必须显式换色）
    const qreal chrome_dpr = w != nullptr ? w->devicePixelRatioF() : 1.0;
    if (app_icon != nullptr)
        app_icon->setPixmap(app_icon_pixmap(tokens.accent, theme::Metrics::brand_icon, chrome_dpr));
    // M5-T5：空态投放区的引导图标（矢量 → pixmap；色彩单源 tokens）
    if (drop_icon != nullptr)
        drop_icon->setPixmap(
            icons::pixmap(icons::Name::Image, drop_icon->width(), tokens.accent, chrome_dpr));
    apply_chrome_icons();
    if (nav != nullptr)
        nav->set_tokens(tokens); // 一体式分段导航（自绘件持有令牌副本）
}

// 段4（R-253）：投影效果器（设备像素换算）+ 卡片阴影 + DWM 标题栏重放 + 整窗 update。
void MainWindow::Impl::theme_apply_effects() {
    // 开始运行钮投影（.go box-shadow 0 2px 10px；QSS 无 box-shadow → 效果器落地）。
    // 注意 Qt 的 QGraphicsEffect**参数是设备像素**（CSS 的 blur/offset 是逻辑像素）→ 按
    // devicePixelRatio 换算，投影的铺开范围才与原型一致（M4-W2-fix：实测未换算时 dpr=2 下
    // 环带 Δblue 仅 +5，原型 +17；换算后见 selfChecks）。
    const qreal dpr = w != nullptr ? w->devicePixelRatioF() : 1.0;
    const auto to_device = [dpr](int logical) {
        return static_cast<qreal>(logical) * (dpr > 0.01 ? dpr : 1.0);
    };
    if (start != nullptr) {
        if (tokens.go_shadow_blur > 0) {
            auto *go_shadow = qobject_cast<QGraphicsDropShadowEffect *>(start->graphicsEffect());
            if (go_shadow == nullptr) {
                go_shadow = new QGraphicsDropShadowEffect(start);
                start->setGraphicsEffect(go_shadow);
            }
            go_shadow->setColor(tokens.go_shadow_color);
            go_shadow->setBlurRadius(to_device(tokens.go_shadow_blur));
            go_shadow->setXOffset(0);
            go_shadow->setYOffset(to_device(tokens.go_shadow_dy));
        } else {
            start->setGraphicsEffect(nullptr);
        }
    }
    // 卡片阴影（§9.2 浅色卡 0 1px 4px rgba(16,24,40,.06)；深色无阴影）
    for (QFrame *card : {left_card, classify_card, preview_card}) {
        if (card == nullptr)
            continue;
        if (tokens.shadow_blur > 0) {
            auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(card->graphicsEffect());
            if (shadow == nullptr) {
                shadow = new QGraphicsDropShadowEffect(card);
                card->setGraphicsEffect(shadow);
            }
            shadow->setColor(tokens.shadow_color);
            shadow->setBlurRadius(to_device(tokens.shadow_blur));
            shadow->setXOffset(0);
            shadow->setYOffset(to_device(tokens.shadow_dy));
        } else {
            card->setGraphicsEffect(nullptr); // 删旧效果（setGraphicsEffect(nullptr) 会析构它）
        }
    }
    // DWM 深色标题栏/Mica 随主题重放（§9.1 能力表最后一行：DWMWA_USE_IMMERSIVE_DARK_MODE
    // 必须与主题一致；启动期由 main.cpp 调一次，运行期主题变化在此重放。非 Windows 平台
    // 该调用在 mica.cpp 内为空操作。首次（构造期，尚未显示）跳过：那时没有真窗口句柄）
    if (w != nullptr && w->isVisible())
        plat::apply_window_backdrop(reinterpret_cast<void *>(w->winId()), tokens.dark());
    if (w != nullptr)
        w->update();
}

void MainWindow::Impl::update_caption_buttons() {
    if (cap_max == nullptr || w == nullptr)
        return;
    const bool maximized = w->isMaximized();
    // M5-T3：最大化/还原 = 换**矢量图标**（caption-max ↔ caption-restore），不再是 ❐/□ 字形
    auto *button = static_cast<CaptionButton *>(cap_max);
    button->set_icon_name(maximized ? icons::Name::CaptionRestore : icons::Name::CaptionMax);
    const QString tip = maximized ? MainWindow::tr("向下还原") : MainWindow::tr("最大化");
    cap_max->setToolTip(tip);
    cap_max->setAccessibleName(tip); // §11：文案随状态变化，两者同步
}

// A13（T11）：分栏几何键 bump 至 v6 键名 —— 旧键名（`ui/splitter`，M4 的 258/601 口径）下的
// 老会话几何**一次性丢弃**，新基线（§3.2 左 330 / 右 587）在下一轮启动即生效；不写迁移代码
// （旧值 = 恰好要作废的那一份）。键名里的 v6 = 规范版本，改基线口径时随之 bump。
QString splitter_state_key() { return QStringLiteral("ui/splitter-v6"); }

void MainWindow::Impl::restore_splitter_state() {
    if (splitter == nullptr || skeleton_state_disabled())
        return;
    QSettings state(ui_state_path(), QSettings::IniFormat);
    const QByteArray saved = state.value(splitter_state_key()).toByteArray();
    if (!saved.isEmpty())
        splitter->restoreState(saved);
}

void MainWindow::Impl::save_splitter_state() {
    if (splitter == nullptr || skeleton_state_disabled())
        return;
    QSettings state(ui_state_path(), QSettings::IniFormat);
    state.setValue(splitter_state_key(), splitter->saveState());
}

// ---------------------------------------------------------------------------
// 设置 / 预设 / EXIF 编辑器
// ---------------------------------------------------------------------------

void MainWindow::open_settings() {
    Impl &d = *impl_;
    if (d.running)
        return;
    SettingsDialog dlg(d.settings, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    d.settings = dlg.settings();
    // A28（T11）：主题档的即时落地（§9「手动切换（设置·通用）」）—— 对话框把三档选择写进
    // settings.ui_theme_mode（下面随即落盘），这里按单源顺序重解析本窗主题（PP_UI_THEME 强制档 >
    // 持久化档 > 跟随系统）并重建 QSS/自绘件；档未变 → set_theme_mode 仍是幂等的整窗重放。
    d.set_theme_mode(theme::preferred_theme_mode());
    pp::LogLevel level = pp::LogLevel::Info;
    if (!parse_log_level_text(d.settings.log_level, level))
        level = pp::LogLevel::Info;
    pp::log_set_level(level);
    d.page_meta->set_map_provider(QString::fromStdString(d.settings.map_provider),
                                  QString::fromStdString(d.settings.amap_key),
                                  d.settings.tile_cache_mb);
    // M6-T11（§5.2 R5）：设置页也能改图层档（「地图」页签）→ accept 后与启动期同一条回填
    d.apply_map_style();
    pp::save_settings(pp::platform::settings_file(), d.settings); // 设置持久化：立即落盘
    refresh_status();
}

void MainWindow::open_exif_editor_row(int row) {
    Impl &d = *impl_;
    if (d.running)
        return;
    if (row < 0 || static_cast<std::size_t>(row) >= d.model->size())
        return;
    const FileRow &file_row = d.model->row(static_cast<std::size_t>(row));
    ExifEditor dlg(QString::fromStdString(file_row.entry.src.string()), d.page_meta->rules(),
                   file_row.entry.exception, this);
    dlg.exec();
    // ExifEditor::result() 遮蔽 QDialog::result()（U8 口径）→ 必须显式限定基类
    if (dlg.QDialog::result() == QDialog::Accepted) {
        d.model->set_exception(static_cast<std::size_t>(row), dlg.result());
        refresh_status();
    }
}

void MainWindow::open_exif_editor_path(const QString &path) {
    Impl &d = *impl_;
    for (std::size_t i = 0; i < d.model->size(); ++i) {
        if (QString::fromStdString(d.model->row(i).entry.src.string()) == path) {
            open_exif_editor_row(static_cast<int>(i));
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Impl：联动 / 工具
// ---------------------------------------------------------------------------

void MainWindow::Impl::restore_session() {
    // M4-T15（§9.3 设置项「分文件夹默认结构」= AppSettings.output_template + split_by_format
    // 一对，§3.6）：启动期把设置里的**默认结构**喂给输出页（【机械性】接线 —— 承接
    // core/settings.h 头注"T15 接线"的义务；本函数即 GUI 侧唯一会话恢复点）。
    // 只落模板，不落 split_by_format —— 后者是 §4.2 定义的**派生态**（= 模板含 $format 段；
    // output_page 的 set_output_template/split_by_format()/config_base() 三处同判据）。
    // 【为什么不能两个都落】：§3.6/§3.2 的默认对本身不同调（模板 `$format/$dir/$file` +
    // 开关 false），若在此把开关一并落值，`set_split_by_format(false)` 会按 §4.2 联动删掉
    // `$format` 段 → **首启默认结构被静默改成平铺**，与 §3.2「多选输出时 UI 默认 true」和
    // output-dark 原型（开关默认开）冲突。故此处以模板为真值；设置对话框写出的开关恒与
    // 模板配平，二者不矛盾。
    // 随后 last_preset 的 apply_preset 会覆盖它（§2.14 预设优先）。
    page_output->set_output_template(QString::fromStdString(settings.output_template));
    page_output->restore_last(QString::fromStdString(settings.last_format),
                              QString::fromStdString(settings.last_out_root));
    page_meta->set_map_provider(QString::fromStdString(settings.map_provider),
                                QString::fromStdString(settings.amap_key), settings.tile_cache_mb);
    // M6-T11（§5.2 R5）：图层档的启动回填（在 set_map_provider 之后 —— 图层支持性提示按最终
    // 源判定；`map_sat_labels` 保持持久化面，消费归 W-M5）
    apply_map_style();
    // 预设优先于 last_format（§2.14）：仅当文件仍存在
    if (settings.last_preset.empty())
        return;
    std::error_code ec;
    if (!std::filesystem::exists(settings.last_preset, ec) || ec)
        return;
    pp::PresetData preset;
    if (!pp::ui::load_preset(settings.last_preset, preset).empty())
        return;
    pp::normalize_preset(preset);
    page_output->apply_preset(preset);
    page_meta->apply_rules(preset.rules);
    last_preset_path = QString::fromStdString(settings.last_preset);
}

// M6-T11（§5.2 R5 / C6）：图层档（矢量/卫星/混合）的跨进程记忆。
//   * 回填（本函数）：settings.map_style → GPS 卡图层 Seg + map->set_tile_style；调用点 =
//     启动期 restore_session（在 set_map_provider 之后 —— 支持性提示按最终源判定）与设置
//     对话框 accept 之后（档可在设置页被改）。
//   * 写回：Seg 换层（用户点段）→ settings.map_style + 立即落盘；首次调用接线，此后常驻。
//   * M6-T12 加性：同一次调用下发 `map_sat_labels`（混合「叠标注」开关）——
//     settings.map_sat_labels → map->set_sat_labels（启动与设置变更即这两个调用点）。
// 单源：段序 → 图层的映射仍是 page_meta 的 layer_style_of（本处不重复枚举枚举值），故回填
// 先「点段」= 与用户同一条链路（set_tile_style + 图层支持性提示 + 自验钩子一并落地），随后
// 再按 kMapStyles 直达一次 set_tile_style（同值幂等，是"档 = 图"的兜底直落）。两行的组合
// 语义 =「记忆的图层必须真的显示出来」：段在目标档而图未跟（或反之）都会让用户看到与档
// 不符的图层。
void MainWindow::Impl::apply_map_style() {
    auto *seg =
        page_meta->findChild<formkit::SegmentedControl *>(QStringLiteral("pp-meta-gps-layer"));
    if (seg == nullptr) {
        return; // 无图层 Seg（防御性；GPS 卡为常驻卡）
    }
    if (!map_layer_bound) {
        map_layer_bound = true;
        QObject::connect(seg, &formkit::SegmentedControl::current_changed, w, [this](int index) {
            const std::string id = kMapStyles[std::clamp(index, 0, 2)].id;
            if (settings.map_style == id) {
                return; // 程序化回填（set_current_index）不发射信号；同值也不落盘
            }
            settings.map_style = id;
            // 立即落盘：跨进程记忆不依赖正常退出（与 open_settings 的"立即落盘"同口径；
            // 写回的是内存档当时的全量值，与 closeEvent 的 save_session 同源）
            pp::save_settings(pp::platform::settings_file(), settings);
        });
    }
    // R-252 时序锚点④「map 首调接线」：换层写回 settings 的接线只在首次调用建立一次
    // （map_layer_bound 幂等门槛）；重复 connect = 同一次换层写多次盘。
    Q_ASSERT(map_layer_bound);
    const int index = map_style_index(settings.map_style);
    if (seg->current_index() != index) {
        if (auto *segment = qobject_cast<QAbstractButton *>(seg->segment(index))) {
            segment->click(); // 同值短路由 Seg 的 activate 保证（本分支本就只走"不同值"）
        }
    }
    if (auto *map = page_meta->findChild<pp::map::MapWidget *>(QStringLiteral("pp-meta-map"))) {
        map->set_tile_style(kMapStyles[index].style); // 档 → 图直达（§5.2；幂等）
        // M6-T12（§5.2/§7-T12 R5）：`map_sat_labels`（混合「叠标注」开关，T11 落地存储）的
        // 消费面 —— 启动与「设置·地图」变更后各下发一次（本函数两个调用点即这两处）；
        // mapwidget 侧只影响 Hybrid 的 label 层（false = 只画影像层），其余绘制零改动。
        map->set_sat_labels(settings.map_sat_labels);
    }
}

void MainWindow::Impl::on_content_changed() {
    if (!running) {
        QStringList paths;
        paths.reserve(static_cast<int>(model->size()));
        for (std::size_t i = 0; i < model->size(); ++i) {
            paths << QString::fromStdString(model->row(i).entry.src.string());
        }
        if (paths != cached_paths) {
            cached_paths = paths;
            page_meta->set_batch_files(
                paths); // 首文件时间预览（PageMeta 跟随项限扫 ≤8 =
                        // kMultiRowCap；R-262：原注释「限扫 200」与实现相反）
            // M4-T10：中栏预览面板的文件集合（列表顺序 = 翻图顺序；集合未变则不打扰）
            if (preview_panel != nullptr)
                preview_panel->set_files(paths);
            // M4-T11（§6.2）：分类归属的惰性清理（只清理"文件已不存在"的项；列表变化时才做，
            // 避免运行期逐事件 IO）
            classes.prune_missing();
            bool any_alpha = false;
            for (std::size_t i = 0; i < model->size(); ++i) {
                const FileRow &row = model->row(i);
                if (row.probe_ok && row.info.has_alpha)
                    any_alpha = true;
            }
            if (any_alpha != cached_alpha) {
                cached_alpha = any_alpha;
                page_output->set_batch_has_alpha(any_alpha); // 无探测信息时保守 false
            }
            // M4-T12（§5.2）：批内文件集合变化且**无选中** → 跟随项回落到新的首个文件
            // （有选中时不打扰用户的选中面；跟随项一律经 sync_selection 同一条口径喂入）
            const bool no_selection = view->selectionModel() == nullptr ||
                                      view->selectionModel()->selectedIndexes().isEmpty();
            if (no_selection && !running)
                sync_selection();
        }
        // 例外编辑只在非运行期发生（编辑器/清除入口在运行中关闭）→ 运行中跳过 O(n) 重扫
        sync_exceptions();
    }
    apply_empty_state(); // M5-T5（修 1-06）：列表空 ⟺ 空态投放区（唯一开关）
    w->refresh_status();
}

// M5-T5（修 1-06）：空态判定 = 列表为空且未在运行（运行期不可能为空 —— 开始运行需非空列表；
// 该纯函数同时是 --ui-smoke 的真值表断言对象，与运行期落地同一实现）。
bool MainWindow::Impl::empty_state_visible(bool model_empty, bool running) {
    return model_empty && !running;
}

void MainWindow::Impl::apply_empty_state() {
    if (file_stack == nullptr)
        return;
    const bool empty = empty_state_visible(model != nullptr && model->empty(), running);
    const int page = empty ? 1 : 0; // 页 0 = 列表，页 1 = 空态投放区
    if (file_stack->currentIndex() != page)
        file_stack->setCurrentIndex(page);
}

void MainWindow::Impl::sync_selection() {
    QStringList paths;
    QList<MetaSelectionItem> items;
    int first_row = -1; // M4-T10：中栏预览跟随选中项（多选取首个）
    bool has = false;
    if (view->selectionModel() != nullptr) {
        // M4-T11 的两列树（勾选列 + 行内容列）使 selectedIndexes() 对同一行返回多枚索引 →
        // 先按源行号去重，再建跟随项（否则多选小表/预览会出现重复行；M4-T12 自验抓到）
        std::vector<int> rows;
        for (const QModelIndex &idx : view->selectionModel()->selectedIndexes()) {
            const int row = proxy->mapToSource(groups->mapToSource(idx)).row();
            if (row < 0 || static_cast<std::size_t>(row) >= model->size())
                continue;
            if (std::find(rows.begin(), rows.end(), row) == rows.end())
                rows.push_back(row);
        }
        for (const int row : rows) {
            if (first_row < 0)
                first_row = row;
            const FileRow &file_row = model->row(static_cast<std::size_t>(row));
            paths << QString::fromStdString(file_row.entry.src.string());
            items.append(meta_selection_item(file_row));
            has = true;
        }
    }
    // M4-T10：中栏预览（列表行号口径；无选中 → 面板回落到首个文件，与 §5.2 同一条跟随口径）
    if (preview_panel != nullptr)
        preview_panel->set_current(first_row);
    // M4-T12（§5.2）：元数据页一切显示跟随选中项；**无选中 → 首个文件 + 「(1/N)」提示**
    if (!has && !model->empty())
        items.append(meta_selection_item(model->row(0)));
    page_meta->set_selection(items, has);
    remove_sel->setVisible(has); // 1-08：无选择即隐藏（与 refresh_status 同一口径）
    remove_sel->setEnabled(!running && has);
    // M4-T11：分类面板高亮跟随选中（§5.2 同一条跟随口径）；分组重建后据此恢复选中
    if (has && !paths.isEmpty()) {
        last_selected_path = paths.first();
        last_selected_paths = paths; // R-153：整组快照（多选重建后整组恢复）
    }
    // M7-R2D1 / R-143：sync_class_views 已随 chip `.on` 拆态（= 过滤档）移除
}

// M4-T12：跟随项快照（§5.2「probe 摘要已在文件添加时收集」——本函数只做搬运，零 IO/零探测）
MetaSelectionItem MainWindow::Impl::meta_selection_item(const FileRow &row) {
    MetaSelectionItem item;
    item.path = QString::fromStdString(row.entry.src.string());
    item.exception = row.entry.exception;
    item.info = row.info;
    item.probe_ok = row.probe_ok;
    std::error_code ec;
    const auto size = std::filesystem::file_size(row.entry.src, ec);
    item.file_size = ec ? 0 : static_cast<qint64>(size);
    return item;
}

void MainWindow::Impl::sync_exceptions() {
    QStringList paths;
    for (std::size_t i = 0; i < model->size(); ++i) {
        if (model->row(i).has_exception()) {
            paths << QString::fromStdString(model->row(i).entry.src.string());
        }
    }
    page_meta->set_exception_summary(paths);
}

void MainWindow::Impl::clear_exception(const QString &path) {
    for (std::size_t i = 0; i < model->size(); ++i) {
        if (QString::fromStdString(model->row(i).entry.src.string()) == path) {
            model->set_exception(i, std::nullopt);
            return;
        }
    }
}

void MainWindow::Impl::remove_selected() {
    if (view->selectionModel() == nullptr)
        return;
    QList<int> rows;
    for (const QModelIndex &idx : view->selectionModel()->selectedIndexes()) {
        const int row = proxy->mapToSource(groups->mapToSource(idx)).row();
        if (row >= 0 && !rows.contains(row))
            rows << row;
    }
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end());
    model->remove_rows(rows);
}

// ---------------------------------------------------------------------------
// M7-R2D1 / UX-2：批量勾选（工具钮菜单 + 快捷键 + 右键三项共用的唯一落地）
//   * 作用面 = **当前可见（过滤后）**的行（docs/ux-review-14.md §2 改进方案 1/4）；
//   * 状态复用 FileListModel 既有勾选面（set_all_checked / set_checked / is_checked），
//     不加第二套勾选状态；
//   * 过滤生效时给一次非阻塞作用域提示（R-067：勾 5 行还是 24 行 —— 文案说清）。
// ---------------------------------------------------------------------------
void MainWindow::Impl::apply_visible_checks(CheckOp op) {
    if (model == nullptr || proxy == nullptr || running)
        return; // G5：运行中面板锁定（菜单/快捷键双兜底）
    std::vector<std::size_t> visible;
    visible.reserve(std::size_t(std::max(0, proxy->rowCount())));
    for (int r = 0; r < proxy->rowCount(); ++r) {
        const QModelIndex src = proxy->mapToSource(proxy->index(r, 0));
        if (src.isValid())
            visible.push_back(std::size_t(src.row()));
    }
    const bool filtered = proxy->filtering() || (search != nullptr && !search->text().isEmpty());
    if (!filtered && op != CheckOp::Invert) {
        model->set_all_checked(op == CheckOp::All); // 可见 = 全部 → 单次信号（O(1) 计数同步）
    } else {
        for (const std::size_t row : visible) {
            const bool on = op == CheckOp::All    ? true
                            : op == CheckOp::None ? false
                                                  : !model->is_checked(row);
            model->set_checked(row, on);
        }
    }
    if (filtered) {
        set_status_note(MainWindow::tr("勾选操作作用于可见 %1 行（筛选生效中；参与运行集仍是全局）")
                            .arg(visible.size()));
    }
}

// UX-2：状态栏一次性非阻塞提示（kStatusNoteMs 后由定时器回落正常状态行）
void MainWindow::Impl::set_status_note(const QString &note) {
    status_note = note;
    if (status_note_timer != nullptr)
        status_note_timer->start();
    w->refresh_status();
}

} // namespace pp::ui
