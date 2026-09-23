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
//   * 三栏骨架：QSplitter(左 258 固定 | 中输入预览 | 右 QStacked 三页)，尺寸持久化走 QSettings；
//   * 顶栏/底栏 + 主题 tokens 单源（ui/theme.h）：明暗跟随系统（QStyleHints::colorScheme）。
//     本任务只给面板**占位与布局接线**：中栏内容归 T10、左栏内容归 T11、右栏三页内容归 W3。

#include "ui/mainwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDesktopServices>
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
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/logger.h"
#include "mapwidget/mapwidget.h"
#include "platform/frameless.h"
#include "platform/mica.h"
#include "platform/paths.h"
#include "ui/exif_editor.h"
#include "ui/filelistmodel.h"
#include "ui/page_meta.h"
#include "ui/page_output.h"
#include "ui/page_run.h"
#include "ui/paramform.h"
#include "ui/preset_io.h"
#include "ui/presets_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/theme.h"
#include "ui/thumbnails.h"

namespace pp::ui {
namespace {

constexpr int kPollIntervalMs = 150; // §2.14：运行轮询间隔
constexpr int kThumbWaitMs = 10000;  // §4.3：缩略图队列空上限
constexpr int kRunWaitMs = 120000;   // U10 冻结：冒烟实跑上限
constexpr int kSmokeShotCount = 8;
constexpr qint64 kMinShotBytes = 10 * 1024;

// 冒烟截图清单（顺序与名称逐字节固定 = §4.3 冻结 8 张；2026-09-20 勘误以任务书为准）
const char *const kSmokeShots[kSmokeShotCount] = {
    "01-meta.png",      "02-output.png",   "02b-output-avif.png", "03-run.png",
    "03b-run-done.png", "04-settings.png", "05-exif-editor.png",  "06-presets.png",
};
constexpr int kShotMeta = 0;
constexpr int kShotOutput = 1;
constexpr int kShotAvif = 2;
constexpr int kShotRun = 3;
constexpr int kShotRunDone = 4;
constexpr int kShotSettings = 5;
constexpr int kShotExif = 6;
constexpr int kShotPresets = 7;

// ---------------------------------------------------------------------------
// §M2-T16：走查 EXIF 编辑器 fixture 的确定性选取（消除对目录枚举顺序的依赖）
// ---------------------------------------------------------------------------
// 走查原先用 model->row(0)（“首个输入文件”）构造 EXIF 编辑器，而输入列表来自
// FileListModel::add_paths → std::filesystem::recursive_directory_iterator（readdir 顺序
// 未定义）：仓库工作树首个是 gray16.png（含 IFD0）→ 通过；`git archive` 导出的干净树
// 首个是 targa.tga（无 EXIF）→ “IFD0 下找不到叶子标签”必红（CI 每次全新 checkout）。
// 现口径：entries() 的路径**先按字典序排序**（排序结果与 readdir 顺序无关；同名候选也因此
// 定序），再按下面的**固定候选优先级**取第一个存在者；候选全不存在 → UI-SMOKE FAIL，不静默跳过。
// 候选 = 实测含 IFD0 叶子（Exif.Image.*）的 tests/golden/base 语料（exiv2 探针逐文件实测，
// 见 M2-T16 报告；每个候选取首个 IFD0 叶子的值均非空）：
//   photo.jpg  Exif.Image.Software（1 个 IFD0 叶子，规范 JPEG EXIF 载体）
//   gray16.tif / multi.tif / rgb8.tif / rgb16.tif  Exif.Image.ImageWidth=64（各 15 个 IFD0 叶子）
//   rgb8.png / gray16.png / gray8.png / graya8.png / rgba8.png / rgb16.png / rgba16.png
//              Exif.Image.Software（各 1 个 IFD0 叶子）
//   jxl8.jxl   Exif.Image.Software（1 个 IFD0 叶子）
// 实测**无 EXIF**（0 个 Exif.Image.*）因而不作候选：anim.gif / bmp24.bmp / targa.tga。
const char *const kSmokeExifFixtures[] = {
    "photo.jpg", "gray16.tif", "multi.tif", "rgb8.tif",  "rgb16.tif",  "rgb8.png", "gray16.png",
    "gray8.png", "graya8.png", "rgba8.png", "rgb16.png", "rgba16.png", "jxl8.jxl",
};

// 候选清单文本（仅用于失败信息，便于定位语料不合规）
QString smoke_exif_fixture_names() {
    QStringList names;
    for (const char *f : kSmokeExifFixtures)
        names << QString::fromLatin1(f);
    return names.join(QLatin1String(" / "));
}

// 选取 EXIF fixture：entries 路径字典序排序 → 按 kSmokeExifFixtures 固定优先级取第一个存在者。
// 返回 nullopt = 一个候选都没有（调用方必须 UI-SMOKE FAIL）。
std::optional<pp::FileEntry> pick_smoke_exif_fixture(const std::vector<pp::FileEntry> &entries) {
    std::vector<pp::FileEntry> sorted = entries; // 值语义拷贝（src/base_dir/exception）
    std::sort(sorted.begin(), sorted.end(), [](const pp::FileEntry &a, const pp::FileEntry &b) {
        return a.src.string() < b.src.string(); // 字典序：与 readdir 顺序无关
    });
    for (const char *candidate : kSmokeExifFixtures) {
        const std::filesystem::path name(candidate);
        for (const pp::FileEntry &e : sorted) {
            if (e.src.filename() != name)
                continue;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(e.src, ec) || ec)
                continue; // 枚举后被移走
            return e;
        }
    }
    return std::nullopt;
}

// 诊断用路径标签：优先仓库根相对路径（CI 日志可核对），树外语料（如 /tmp 隔离树）退回文件名。
QString smoke_fixture_label(const std::filesystem::path &p, const QString &repo_root_path) {
    const QString abs = QDir::cleanPath(QString::fromStdString(p.string()));
    if (!repo_root_path.isEmpty()) {
        const QString rel = QDir(repo_root_path).relativeFilePath(abs);
        if (!rel.startsWith(QLatin1String("..")) && !QDir::isAbsolutePath(rel))
            return rel;
    }
    return QString::fromStdString(p.filename().string());
}

// 断言 e 的最小字节数（语义 = "offscreen 下非空渲染"）：1440×900 整窗截图 >10KB；
// 对话框截图 >2KB——空列表的预设对话框大面积空白，PNG 压缩后仅 ~7.6KB（实测），
// 仍是有内容的真实渲染，故按控件面积分档（数值随每次冒烟 stdout 打印备查）。
qint64 min_shot_bytes(int index) { return index >= kShotSettings ? 2 * 1024 : kMinShotBytes; }

QString T(const char *s) { return QCoreApplication::translate("MainWindow", s); }

// ---------------------------------------------------------------------------
// M4-T9 骨架部件（设计 §9.1/§9.2；尺寸与色值一律取自 ui/theme.h 的 tokens，不写死字面量）
// ---------------------------------------------------------------------------
namespace theme = pp::ui::theme;
namespace plat = pp::platform;

bool skeleton_state_disabled() {
    // --ui-smoke 置位（main.cpp）：不读写面板持久化 → 冻结截图与机器无关
    return qEnvironmentVariableIsSet("PP_UI_NO_STATE");
}

// 启动/走查主题：PP_UI_THEME=dark|light 强制；未设置 → 跟随系统（§9.2 明暗跟随系统）。
// 强制档供 W5 双主题截图走查用（系统同一时刻只能是一种模式）。
theme::ThemeMode preferred_theme_mode() {
    const QByteArray forced = qgetenv("PP_UI_THEME").trimmed().toLower();
    if (forced == QByteArrayLiteral("dark"))
        return theme::ThemeMode::Dark;
    if (forced == QByteArrayLiteral("light"))
        return theme::ThemeMode::Light;
    return theme::system_theme_mode();
}

// 会话态文件（QSettings INI）：与 settings.ini 同目录（data_dir() 保证已存在）
QString ui_state_path() {
    const std::filesystem::path path = pp::platform::data_dir() / "ui-state.ini";
    const std::string bytes = path.string();
    return QFile::decodeName(QByteArray(bytes.data(), static_cast<int>(bytes.size())));
}

// 顶栏应用图标（mockup .app-title 的 24×24 svg 等价绘制：相机外框 + 镜头圆 + 右上小点）
QPixmap app_icon_pixmap(const QColor &accent, int size = 18) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal scale = size / 24.0;
    painter.setPen(QPen(accent, 1.8 * scale));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(2 * scale, 4 * scale, 20 * scale, 16 * scale), 3 * scale,
                            3 * scale);
    painter.drawEllipse(QPointF(12 * scale, 12 * scale), 3.6 * scale, 3.6 * scale);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawEllipse(QPointF(18.2 * scale, 7.6 * scale), 1.2 * scale, 1.2 * scale);
    return pixmap;
}

// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」）：保存全文，按当前宽度省略
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {}
    void set_full_text(const QString &text) {
        full_text_ = text;
        updateGeometry(); // sizeHint 随全文变化（否则布局按"已省略文本"定宽 → 永远省略）
        apply_elide();
    }
    QString full_text() const { return full_text_; }
    // 布局按**全文**申请宽度（有空间就出全文），只在挤不下时省略；
    // minimumSizeHint 保持 QLabel 默认（可被压缩 → resizeEvent 触发省略）
    QSize sizeHint() const override {
        QSize base = QLabel::sizeHint();
        if (full_text_.isEmpty())
            return base;
        return QSize(fontMetrics().horizontalAdvance(full_text_) + 2, base.height());
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        apply_elide();
    }

private:
    void apply_elide() {
        if (full_text_.isEmpty()) {
            QLabel::clear();
            return;
        }
        if (width() <= 0) { // 布局尚未定宽：先出全文，resizeEvent 时再省略
            QLabel::setText(full_text_);
            return;
        }
        QLabel::setText(fontMetrics().elidedText(full_text_, Qt::ElideRight, std::max(0, width())));
    }
    QString full_text_;
};

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
        auto *pill = new ElidedLabel(row);             // 长文件名/计数都按 §9.3 省略号
        pill->setProperty(theme::kPillProperty, true); // QSS：QLabel[ppPill="true"]（.pill）
        pill->setFont(theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
        pill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // 不随行高拉伸
        pill->setVisible(false);                                         // 原型里有内容才画药丸
        layout->addWidget(pill);
        *pill_out = pill;
    }
    layout->addStretch(1);
    return row;
}

// 步骤切换钮（mockup .step / .step .n）：几何与配色逐条照 CSS —— 圆角 6、padding 6×16、
// 编号徽标 18×18 r=99、徽标↔文案间距 8、文案 13px/600（.step{font-size:13px}）。
// 选中态 = accent-dim 底 + accent-bd 边 + txt 字 + accent 实心徽标（编号字色 on_accent）。
// 自绘而不是 QToolButton 的 icon+text：icon 与 text 的间距由样式决定、无法对齐 .step 的
// gap:8px；自绘后 badge/gap/padding 才能与原型逐像素一致（自检里按墨迹簇量过）。
class StepButton : public QAbstractButton {
public:
    explicit StepButton(QWidget *parent = nullptr) : QAbstractButton(parent) {
        setCheckable(true);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFont(theme::font(theme::Typography::caption_px, 600));
    }
    void set_number(int number) {
        number_ = number;
        updateGeometry();
        update();
    }
    void set_tokens(const theme::Tokens &tokens) {
        tokens_ = tokens;
        update();
    }
    QSize sizeHint() const override {
        const QFontMetrics metrics(font());
        return QSize(theme::Metrics::step_pad_x * 2 + theme::Metrics::step_badge +
                         theme::Metrics::step_badge_gap + metrics.horizontalAdvance(text()),
                     theme::Metrics::step_pad_y * 2 + theme::Metrics::step_badge);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool active = isChecked() && isEnabled();
        // 底/边：.step{background:transparent;border:1px solid transparent}
        //         .step.active{background:--acc-dim;border-color:--acc-bd}
        if (active) {
            painter.setPen(QPen(tokens_.accent_bd, 1.0));
            painter.setBrush(tokens_.accent_dim);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::NoBrush);
        }
        const qreal radius = theme::Metrics::step_radius;
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        // 编号徽标：.step .n{18×18;r=99;background:--ctl;border:1px --ctl-bd;color:--txt2}
        //            .step.active .n{background:--acc;border-color:--acc;color:on_accent}
        const qreal badge = theme::Metrics::step_badge;
        const QRectF badge_rect(theme::Metrics::step_pad_x, (height() - badge) / 2.0, badge, badge);
        painter.setPen(QPen(active ? tokens_.accent : tokens_.control_bd, 1.0));
        painter.setBrush(active ? tokens_.accent : tokens_.control);
        painter.drawEllipse(badge_rect.adjusted(0.5, 0.5, -0.5, -0.5));
        painter.setFont(
            theme::font(theme::Typography::step_badge_px, theme::Typography::step_badge_weight));
        painter.setPen(active ? tokens_.on_accent : tokens_.text2);
        painter.drawText(badge_rect, Qt::AlignCenter, QString::number(number_));
        // 文案（§9.3 禁止折行 → 超长省略号）
        painter.setFont(font());
        painter.setPen(!isEnabled() ? tokens_.text3 : (active ? tokens_.text : tokens_.text2));
        const int text_x = static_cast<int>(badge_rect.right()) + theme::Metrics::step_badge_gap;
        const int text_w = std::max(0, width() - text_x - theme::Metrics::step_pad_x);
        const QFontMetrics metrics(font());
        painter.drawText(QRect(text_x, 0, text_w, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         metrics.elidedText(text(), Qt::ElideRight, text_w));
    }

private:
    theme::Tokens tokens_;
    int number_ = 1;
};

// 图标钮（mockup .icon-btn 32×32 r=6）
QToolButton *make_icon_button(QWidget *parent, QAction *action, const char *object_name) {
    auto *button = new QToolButton(parent);
    button->setObjectName(QString::fromLatin1(object_name));
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFixedSize(theme::Metrics::icon_btn, theme::Metrics::icon_btn);
    button->setFont(theme::font(14.0));
    button->setFocusPolicy(Qt::NoFocus);
    if (action != nullptr)
        button->setDefaultAction(action); // 文本/提示/enabled 全跟 QAction（lock_for_run 靠它置灰）
    return button;
}

// caption 三钮（§9.1 能力表：46 宽、贴满顶栏高（mockup .capbtns align-self:stretch）；
// 关闭悬停 #c42b1c 在 tokens/QSS 里）
QPushButton *make_caption_button(QWidget *parent, const QString &glyph, const QString &tip,
                                 const char *object_name) {
    auto *button = new QPushButton(glyph, parent);
    button->setObjectName(QString::fromLatin1(object_name));
    button->setToolTip(tip);
    button->setFixedSize(theme::Metrics::caption_btn_w, theme::Metrics::caption_btn_h);
    button->setFont(theme::font(theme::Typography::small_px));
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

const char *hit_zone_name(plat::HitZone zone) {
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

const char *caption_action_name(plat::CaptionAction action) {
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

bool parse_log_level_text(const std::string &s, pp::LogLevel &out) {
    if (s == "trace") {
        out = pp::LogLevel::Trace;
        return true;
    }
    if (s == "debug") {
        out = pp::LogLevel::Debug;
        return true;
    }
    if (s == "info") {
        out = pp::LogLevel::Info;
        return true;
    }
    if (s == "warn") {
        out = pp::LogLevel::Warn;
        return true;
    }
    if (s == "error") {
        out = pp::LogLevel::Error;
        return true;
    }
    return false;
}

QString format_label(const QString &id) {
    const pp::FormatDef *f = pp::find_format(id.toStdString());
    return f != nullptr ? QString::fromStdString(f->label) : id;
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

QString path_text(const std::filesystem::path &p) {
    return QFileInfo(QString::fromStdString(p.string())).fileName();
}

bool is_metadata_only_format(const QString &suffix) {
    static const QStringList ok{QStringLiteral("jpg"),  QStringLiteral("jpeg"),
                                QStringLiteral("png"),  QStringLiteral("tif"),
                                QStringLiteral("tiff"), QStringLiteral("webp")};
    return ok.contains(suffix);
}

} // namespace

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
    QWidget *bottombar = nullptr;                         // 底栏
    QSplitter *splitter = nullptr;                        // 三栏
    QLabel *app_icon = nullptr;
    StepButton *step_meta = nullptr;
    StepButton *step_output = nullptr;
    StepButton *step_run = nullptr;
    QAction *presets_action = nullptr;
    QToolButton *presets_btn = nullptr;
    QToolButton *settings_btn = nullptr;
    QPushButton *cap_min = nullptr;
    QPushButton *cap_max = nullptr;
    QPushButton *cap_close = nullptr;
    ElidedLabel *output_status = nullptr;
    QLabel *status_dot = nullptr;
    ElidedLabel *preview_pill = nullptr; // 中栏卡头徽标（T10 填文件名）
    QFrame *left_card = nullptr;         // 左栏卡框（T11 填内容）
    QFrame *preview_card = nullptr;      // 中栏卡框（T10 填内容）

    // ---- 左：文件面板 ----
    QWidget *panel = nullptr;
    FileListModel *model = nullptr;
    Thumbnailer *thumbs = nullptr;
    QSortFilterProxyModel *proxy = nullptr;
    QListView *view = nullptr;
    ElidedLabel *file_count = nullptr; // 卡头计数徽标（.pill）
    ElidedLabel *unsupported = nullptr;
    QLineEdit *search = nullptr;
    QPushButton *add_files = nullptr;
    QPushButton *add_dir = nullptr;
    QPushButton *remove_sel = nullptr;
    QPushButton *clear_all = nullptr;

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

    // ---- 运行状态 ----
    std::unique_ptr<pp::Scheduler> sched;
    QTimer *poll = nullptr;
    bool running = false;
    // 完成检测（§2.14 的"轮询 !running()"机制不可用：pp::Scheduler::running() 只在 wait() 内清零，
    // 见报告 api-deltas）→ 轮询改为"终态事件计数 == 本批文件数"，语义等价且不阻塞 GUI。
    std::size_t run_total = 0;
    std::size_t run_terminal = 0;
    std::size_t run_done = 0;
    std::size_t run_failed = 0;
    std::size_t run_skipped = 0;
    QString last_preset_path;
    QStringList cached_paths; // 行集合签名（避免每次状态变化都重扫时间预览）
    bool cached_alpha = false;

    // ---- 拖放 / closeEvent 事件过滤器（冻结头不允许覆写虚函数）----
    struct Filter : QObject {
        Impl *impl = nullptr;
        explicit Filter(Impl *i) : QObject(i->w), impl(i) {}
        bool eventFilter(QObject *watched, QEvent *event) override {
            return impl->handle_event(watched, event);
        }
    };
    Filter *filter = nullptr;

    // ---- 冒烟 ----
    QStringList smoke_failures;
    int smoke_shots = 0;

    Impl(MainWindow *owner, const pp::AppSettings &s);
    ~Impl();

    bool handle_event(QObject *watched, QEvent *event);

    // 联动 / 工具
    void on_content_changed();
    void sync_selection();
    void sync_exceptions();
    void clear_exception(const QString &path);
    void remove_selected();
    void restore_session();
    void open_logs();
    void handle_file_event(const pp::FileEvent &ev);

    // M4-T9 骨架：主题 / 三栏持久化 / caption 行为
    void refresh_theme();
    void set_theme_mode(theme::ThemeMode mode);
    void restore_splitter_state();
    void save_splitter_state();
    void update_caption_buttons();

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
    static QString repo_root();
};

MainWindow::Impl::Impl(MainWindow *owner, const pp::AppSettings &s)
    : w(owner), settings(s), theme_mode(preferred_theme_mode()) {
    model = new FileListModel(w);
    thumbs = new Thumbnailer(96, w);
    model->set_thumbnailer(thumbs); // U4 口径：内部已 connect(ready→apply_thumb) + 自动 enqueue

    proxy = new QSortFilterProxyModel(w);
    proxy->setSourceModel(model);
    proxy->setFilterRole(FileListModel::NameRole);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterFixedString(QString());

    poll = new QTimer(w);
    poll->setInterval(kPollIntervalMs);

    filter = new Filter(this);
    w->installEventFilter(filter);
}

MainWindow::Impl::~Impl() {
    if (filter != nullptr) {
        w->removeEventFilter(filter);
        delete filter;
        filter = nullptr;
    }
}

bool MainWindow::Impl::handle_event(QObject * /*watched*/, QEvent *event) {
    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *e = static_cast<QDragEnterEvent *>(event);
        if (e->mimeData() != nullptr && e->mimeData()->hasUrls()) {
            e->acceptProposedAction();
            return true;
        }
        return false;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
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
    impl_->sync_exceptions();
    impl_->on_content_changed();
    set_current_page(1);
}

MainWindow::~MainWindow() = default;

void MainWindow::build_ui() {
    Impl &d = *impl_;
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

    // ---- 顶栏（兼标题栏，§9.1）：图标+标题 | ①元数据 ②输出 ③运行 | ☆ ⚙ | ─ □ ✕ ----
    d.titlebar = new QWidget(central);
    d.titlebar->setObjectName(QStringLiteral("pp-titlebar"));
    d.titlebar->setFixedHeight(theme::Metrics::toolbar_height);
    auto *top = new QHBoxLayout(d.titlebar);
    top->setContentsMargins(theme::Metrics::titlebar_pad_left, 0, 0, 0);
    top->setSpacing(14);

    d.app_icon = new QLabel(d.titlebar);
    d.app_icon->setObjectName(QStringLiteral("pp-app-icon"));
    d.app_icon->setFixedSize(18, 18);
    auto *title = new QLabel(QStringLiteral("PhotoPipeline"), d.titlebar);
    title->setObjectName(QStringLiteral("pp-app-title"));
    title->setFont(theme::font(theme::Typography::caption_px, 600));
    auto *version = new QLabel(QStringLiteral("0.3.0"), d.titlebar);
    version->setObjectName(QStringLiteral("pp-app-ver"));
    version->setFont(theme::font(theme::Typography::hint_px, 500));
    // mockup .toolbar{align-items:center}：徽标按自身高度居中，不被行高拉伸
    version->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *title_row = new QHBoxLayout();
    title_row->setContentsMargins(0, 0, 0, 0);
    title_row->setSpacing(8);
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
    // mockup .toolbar{gap:14px} + .steps{margin-left:22px} = 36px（外加上方 14 的间距）
    top->addSpacing(theme::Metrics::step_margin_left);
    auto *steps = new QHBoxLayout();
    steps->setContentsMargins(0, 0, 0, 0);
    steps->setSpacing(theme::Metrics::step_gap); // .steps{gap:6px}
    d.step_meta = new StepButton(d.titlebar);
    d.step_output = new StepButton(d.titlebar);
    d.step_run = new StepButton(d.titlebar);
    const struct {
        StepButton *button;
        QAction *action;
        int number;
        const char *object_name;
    } step_bindings[] = {
        {d.step_meta, d.nav_meta, 1, "pp-step-meta"},
        {d.step_output, d.nav_output, 2, "pp-step-output"},
        {d.step_run, d.nav_run, 3, "pp-step-run"},
    };
    for (const auto &binding : step_bindings) {
        binding.button->setObjectName(QString::fromLatin1(binding.object_name));
        binding.button->set_number(binding.number);
        binding.button->setText(binding.action->text());
        binding.button->setChecked(binding.action->isChecked());
        // 自绘步钮 ↔ QAction 同步：点击 → action->trigger()（= 既有 set_current_page 路径）；
        // action 的 checked/enabled 变化 → 步钮（lock_for_run/set_current_page 依赖 action）
        connect(binding.button, &QAbstractButton::clicked, binding.action, &QAction::trigger);
        connect(binding.action, &QAction::toggled, binding.button, &QAbstractButton::setChecked);
        connect(binding.action, &QAction::changed, binding.button,
                [action = binding.action, button = binding.button] {
                    button->setEnabled(action->isEnabled());
                });
        steps->addWidget(binding.button);
    }
    top->addLayout(steps);

    top->addStretch(1);
    d.presets_action = new QAction(QStringLiteral("☆"), this);
    d.presets_action->setToolTip(tr("预设"));
    d.settings_action = new QAction(QStringLiteral("⚙"), this);
    d.settings_action->setToolTip(tr("设置"));
    d.presets_btn = make_icon_button(d.titlebar, d.presets_action, "pp-presets");
    d.settings_btn = make_icon_button(d.titlebar, d.settings_action, "pp-settings");
    auto *caps = new QHBoxLayout();
    caps->setContentsMargins(2, 0, 0, 0); // .capbtns{margin-left:2px}
    caps->setSpacing(0);
    d.cap_min = make_caption_button(d.titlebar, QStringLiteral("–"), tr("最小化"), "pp-cap-min");
    d.cap_max = make_caption_button(d.titlebar, QStringLiteral("□"), tr("最大化"), "pp-cap-max");
    d.cap_close = make_caption_button(d.titlebar, QStringLiteral("✕"), tr("关闭"), "pp-cap-close");
    caps->addWidget(d.cap_min);
    caps->addWidget(d.cap_max);
    caps->addWidget(d.cap_close);
    // mockup .tb-right{gap:10px}：☆/⚙ 之间与 ⚙↔三钮之间都是 10（后者再叠 .capbtns 的 2px）
    auto *right_row = new QHBoxLayout();
    right_row->setContentsMargins(0, 0, 0, 0);
    right_row->setSpacing(10);
    right_row->addWidget(d.presets_btn);
    right_row->addWidget(d.settings_btn);
    right_row->addSpacing(2);
    right_row->addLayout(caps);
    top->addLayout(right_row);
    outer->addWidget(d.titlebar);

    // ---- 三栏骨架（§9.1）：左 258 固定 | 中 1.0 弹性 | 右 1.15 弹性 ----
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

    // 左栏卡（T11 填内容；本任务只给卡框与既有控件接线）
    d.left_card = new QFrame(d.splitter);
    d.left_card->setObjectName(QStringLiteral("pp-card"));
    d.left_card->setAttribute(Qt::WA_StyledBackground, true);
    // §9.1「左栏 258px 固定（280 可调）」：下界 = 固定值（缩放时不被压缩，尺寸可复现），
    // 上界 = 可调上限（用户拖拽分栏手柄最多拉到 280）
    d.left_card->setMinimumWidth(theme::Metrics::left_width);
    d.left_card->setMaximumWidth(theme::Metrics::left_width_max);
    auto *left_layout = new QVBoxLayout(d.left_card);
    left_layout->setContentsMargins(8, 6, 8, 8);
    left_layout->setSpacing(6);

    d.panel = new QWidget(d.left_card);
    d.panel->setObjectName(QStringLiteral("pp-file-panel"));
    auto *pv = new QVBoxLayout(d.panel);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(4);

    // 卡头：标题 + 计数徽标（.pill）+ 右端 hint（不支持的格式数 / 拖放提示）
    QLabel *panel_title = nullptr;
    auto *card_head = make_card_header(d.left_card, tr("文件"), &panel_title, &d.file_count);
    d.file_count->setObjectName(QStringLiteral("pp-file-count"));
    d.unsupported = new ElidedLabel(card_head);
    d.unsupported->setObjectName(QStringLiteral("pp-card-hint"));
    d.unsupported->setStyleSheet(QStringLiteral("color:#dd8800"));
    d.unsupported->setVisible(false);
    auto *drop_hint = new ElidedLabel(card_head);
    drop_hint->setObjectName(QStringLiteral("pp-card-hint"));
    drop_hint->setFont(theme::font(theme::Typography::hint_px));
    drop_hint->set_full_text(tr("拖放添加"));
    if (auto *head_layout = qobject_cast<QHBoxLayout *>(card_head->layout())) {
        head_layout->addWidget(d.unsupported); // addStretch 之后 = 右对齐
        head_layout->addWidget(drop_hint);
    }
    pv->addWidget(card_head);

    auto *buttons = new QGridLayout();
    buttons->setSpacing(4);
    d.add_files = new QPushButton(tr("添加文件…"), d.panel);
    d.add_files->setObjectName(QStringLiteral("pp-add-files"));
    d.add_dir = new QPushButton(tr("添加文件夹…"), d.panel);
    d.add_dir->setObjectName(QStringLiteral("pp-add-dir"));
    d.remove_sel = new QPushButton(tr("移除所选"), d.panel);
    d.remove_sel->setObjectName(QStringLiteral("pp-remove-sel"));
    d.clear_all = new QPushButton(tr("清空"), d.panel);
    d.clear_all->setObjectName(QStringLiteral("pp-clear-all"));
    d.remove_sel->setEnabled(false);
    buttons->addWidget(d.add_files, 0, 0);
    buttons->addWidget(d.add_dir, 0, 1);
    buttons->addWidget(d.remove_sel, 1, 0);
    buttons->addWidget(d.clear_all, 1, 1);
    pv->addLayout(buttons);

    d.search = new QLineEdit(d.panel);
    d.search->setObjectName(QStringLiteral("pp-search"));
    d.search->setPlaceholderText(tr("搜索文件名…"));
    d.search->setClearButtonEnabled(true);
    pv->addWidget(d.search);

    d.view = new QListView(d.panel);
    d.view->setObjectName(QStringLiteral("pp-file-view"));
    d.view->setModel(d.proxy);
    d.view->setItemDelegate(new FileDelegate(d.view));
    d.view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d.view->setSelectionBehavior(QAbstractItemView::SelectRows);
    d.view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d.view->setUniformItemSizes(true);
    pv->addWidget(d.view, 1);
    left_layout->addWidget(d.panel, 1);

    // 中栏卡：输入预览（T10 接 decode_preview + 缩放/翻图/徽标；本任务只给卡框与舞台）
    d.preview_card = new QFrame(d.splitter);
    d.preview_card->setObjectName(QStringLiteral("pp-card"));
    d.preview_card->setAttribute(Qt::WA_StyledBackground, true);
    auto *preview_layout = new QVBoxLayout(d.preview_card);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    preview_layout->setSpacing(0);
    QLabel *preview_title = nullptr;
    preview_layout->addWidget(
        make_card_header(d.preview_card, tr("输入预览"), &preview_title, &d.preview_pill));
    auto *stage = new QFrame(d.preview_card);
    stage->setObjectName(QStringLiteral("pp-preview-stage"));
    stage->setAttribute(Qt::WA_StyledBackground, true);
    auto *stage_layout = new QVBoxLayout(stage);
    stage_layout->setContentsMargins(10, 10, 10, 10);
    auto *stage_hint = new ElidedLabel(stage);
    stage_hint->setObjectName(QStringLiteral("pp-card-hint"));
    stage_hint->setAlignment(Qt::AlignCenter);
    stage_hint->set_full_text(tr("（预览面板占位：T10 接入 decode_preview）")); // §9.3 省略号
    stage_layout->addWidget(stage_hint);
    auto *stage_margin = new QHBoxLayout();
    stage_margin->setContentsMargins(10, 10, 10, 10);
    stage_margin->addWidget(stage);
    preview_layout->addLayout(stage_margin, 1);

    // 右栏：步骤内容区（QStacked 三页，W3 填内容）
    d.stack = new QStackedWidget(d.splitter);
    d.stack->setObjectName(QStringLiteral("pp-stack"));
    d.page_meta = new PageMeta(d.stack);
    d.page_output = new PageOutput(d.stack);
    d.page_run = new PageRun(d.stack);
    d.stack->addWidget(d.page_meta);   // 页 1 元数据
    d.stack->addWidget(d.page_output); // 页 2 输出
    d.stack->addWidget(d.page_run);    // 页 3 运行

    d.splitter->addWidget(d.left_card);
    d.splitter->addWidget(d.preview_card);
    d.splitter->addWidget(d.stack);
    // 左栏固定（stretch 0）；中/右弹性：1440 校准落点 = mockup 实测 537/601（§9.4 以 mockup 为准，
    // §9.1 文本「1.15」与原型实测 1.111/1.119 冲突 → 见 T9 偏差），缩放时 Qt 按当前尺寸等比分配。
    d.splitter->setStretchFactor(0, 0);
    d.splitter->setStretchFactor(1, theme::Metrics::mid_stretch);
    d.splitter->setStretchFactor(2, theme::Metrics::right_stretch);
    d.splitter->setSizes({theme::Metrics::left_width, theme::Metrics::mid_width_1440,
                          theme::Metrics::right_width_1440});

    // ---- 底栏（§9.1）：选中摘要 · 输出摘要 · [开始运行]（运行中变状态，W3-T14 接运行态）----
    d.bottombar = new QWidget(central);
    d.bottombar->setObjectName(QStringLiteral("pp-bottombar"));
    d.bottombar->setFixedHeight(theme::Metrics::bottom_height);
    auto *bottom = new QHBoxLayout(d.bottombar);
    bottom->setContentsMargins(theme::Metrics::titlebar_pad_left, 0,
                               theme::Metrics::titlebar_pad_left, 0);
    bottom->setSpacing(14);
    d.status_dot = new QLabel(QStringLiteral("●"), d.bottombar);
    d.status_dot->setObjectName(QStringLiteral("pp-status-dot"));
    d.status_dot->setFont(theme::font(8.0));
    d.status = new ElidedLabel(d.bottombar); // §9.3：超长文案省略号，不折行
    d.status->setObjectName(QStringLiteral("pp-status"));
    d.status->setFont(theme::font(theme::Typography::body_px));
    d.status->set_full_text(tr("没有文件"));
    d.output_status = new ElidedLabel(d.bottombar);
    d.output_status->setObjectName(QStringLiteral("pp-output-status"));
    d.output_status->setFont(theme::font(theme::Typography::body_px));
    bottom->addWidget(d.status_dot);
    bottom->addWidget(d.status);
    bottom->addWidget(d.output_status);
    bottom->addStretch(1);
    d.start = new QPushButton(tr("▶  开始运行"), d.bottombar);
    d.start->setObjectName(QStringLiteral("pp-start"));
    d.start->setFixedHeight(theme::Metrics::go_height); // .go{height:34px}
    d.start->setFont(theme::font(theme::Typography::caption_px, 700));
    d.start->setDefault(true);
    bottom->addWidget(d.start);
    outer->addWidget(d.bottombar);

    setCentralWidget(central);

    // ---- 无边框接线（§9.1 能力表）：顶栏 = 拖拽/HTCAPTION 区；交互控件全部排除 ----
    d.frameless = plat::Frameless::attach(
        this, d.titlebar, {d.step_meta, d.step_output, d.step_run, d.presets_btn, d.settings_btn});
    if (d.frameless != nullptr)
        d.frameless->set_caption_buttons(d.cap_min, d.cap_max, d.cap_close);

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

    connect(d.add_files, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("添加文件"), QString(),
            tr("图片 (*.jpg *.jpeg *.png *.tif *.tiff *.webp *.jxl *.heic *.heif *.avif *.bmp "
               "*.gif *.tga);;所有文件 (*)"));
        if (!files.isEmpty())
            add_paths(files);
    });
    connect(d.add_dir, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("添加文件夹"));
        if (!dir.isEmpty())
            add_paths(QStringList{dir});
    });
    connect(d.remove_sel, &QPushButton::clicked, this, [this] { impl_->remove_selected(); });
    connect(d.clear_all, &QPushButton::clicked, this, [this] { impl_->model->clear(); });
    connect(d.search, &QLineEdit::textChanged, this,
            [this](const QString &text) { impl_->proxy->setFilterFixedString(text); });
    connect(d.view, &QListView::doubleClicked, this, [this](const QModelIndex &idx) {
        if (idx.isValid())
            open_exif_editor_row(impl_->proxy->mapToSource(idx).row());
    });
    connect(d.view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { impl_->sync_selection(); });
    // G5（2026-09-20 R1 修订）：底栏开始按钮运行中保持"开始"且禁用 → 取消唯一入口 = 运行页
    connect(d.start, &QPushButton::clicked, this, &MainWindow::on_start);

    connect(d.model, &FileListModel::content_changed, this,
            [this] { impl_->on_content_changed(); });
    connect(d.model, &FileListModel::exception_changed, this,
            [this](std::size_t) { impl_->sync_exceptions(); });
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

    connect(d.page_meta, &PageMeta::rules_changed, this, [this] { refresh_status(); });
    connect(d.page_meta, &PageMeta::open_editor_requested, this,
            &MainWindow::open_exif_editor_path);
    connect(d.page_meta, &PageMeta::clear_exception_requested, this,
            [this](const QString &path) { impl_->clear_exception(path); });

    connect(d.page_output, &PageOutput::config_changed, this, [this] { refresh_status(); });
    connect(d.page_output, &PageOutput::manage_presets_requested, this,
            &MainWindow::manage_presets);
    connect(d.page_output, &PageOutput::open_settings_requested, this, &MainWindow::open_settings);

    connect(d.page_run, &PageRun::cancel_requested, this, &MainWindow::on_cancel);
    connect(d.page_run, &PageRun::open_output_requested, this, [](const QString &dir) {
        if (!dir.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(d.page_run, &PageRun::open_logs_requested, this, [this] { impl_->open_logs(); });
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
    const QList<pp::map::MapWidget *> maps = findChildren<pp::map::MapWidget *>();
    for (pp::map::MapWidget *map : maps)
        map->set_offline(off);
}

// ---------------------------------------------------------------------------
// 运行：开始 / 取消 / 结束 / G5 锁定
// ---------------------------------------------------------------------------

void MainWindow::on_start() {
    Impl &d = *impl_;
    if (d.running) {
        QMessageBox::warning(this, tr("无法开始"), tr("正在运行"));
        return;
    }
    if (d.model->empty()) {
        QMessageBox::warning(this, tr("无法开始"), tr("没有文件"));
        return;
    }
    const QString reason = d.page_output->ready_to_start();
    if (!reason.isEmpty()) {
        QMessageBox::warning(this, tr("无法开始"), reason);
        return;
    }

    pp::RunConfig cfg = d.page_output->config_base();
    cfg.rules = d.page_meta->rules();
    cfg.workers = d.settings.workers;
    cfg.budget_bytes = static_cast<uint64_t>(d.settings.budget_gb) << 30; // 0 = 自动
    cfg.flatten_gray = d.settings.flatten_gray;
    cfg.rotate_orientation = d.settings.rotate_orientation;

    // M2-T5 §2.7：交叉参数约束的最后闸门。ParamForm 已实时红字提示，这里对真正要下发的
    // 配置再校验一次；非空 → 列出全部消息并阻止开始（不进入 lock_for_run/Scheduler）。
    // 0.3.0 多输出：逐输出求值（T13 起 outputs 可能 >1，UI 现在恒 1 项）。
    {
        std::vector<std::string> cross;
        for (const pp::OutputFormatSpec &spec : cfg.outputs) {
            const std::vector<std::string> msgs =
                pp::cross_validate(spec.params, spec.format_id, spec.tech_id);
            cross.insert(cross.end(), msgs.begin(), msgs.end());
        }
        if (!cross.empty()) {
            QStringList lines;
            lines.reserve(static_cast<int>(cross.size()));
            for (const std::string &msg : cross)
                lines << QString::fromStdString(msg);
            QMessageBox::warning(this, tr("无法开始"), lines.join(QLatin1Char('\n')));
            return;
        }
    }

    if (cfg.metadata_only) {
        QStringList bad;
        for (std::size_t i = 0; i < d.model->size(); ++i) {
            const QString name = path_text(d.model->row(i).entry.src);
            const QString suffix = QFileInfo(name).suffix().toLower();
            if (!is_metadata_only_format(suffix))
                bad << name;
        }
        if (!bad.isEmpty()) {
            const QString head = bad.mid(0, 20).join(QLatin1Char('\n'));
            const QString more =
                bad.size() > 20 ? tr("\n…（其余 %1 个省略）").arg(bad.size() - 20) : QString();
            const QMessageBox::StandardButton answer = QMessageBox::warning(
                this, tr("仅元数据预检"),
                tr("以下 %1 个文件的格式不支持仅元数据（输出将保持源格式：JPEG / PNG / TIFF / "
                   "WebP）：\n%2%3\n\n仍要继续吗？")
                    .arg(bad.size())
                    .arg(head, more),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
    }

    QStringList names;
    names.reserve(static_cast<int>(d.model->size()));
    for (std::size_t i = 0; i < d.model->size(); ++i) {
        names << path_text(d.model->row(i).entry.src);
    }
    std::vector<pp::FileEntry> entries = d.model->entries(); // 含单文件例外（引擎侧生效）
    const std::size_t total = entries.size();
    for (std::size_t i = 0; i < total; ++i)
        d.model->set_state(i, pp::FileState::Queued);
    d.run_total = total;
    d.run_terminal = 0;
    d.run_done = 0;
    d.run_failed = 0;
    d.run_skipped = 0;

    d.sched = std::make_unique<pp::Scheduler>(cfg, std::move(entries));
    d.sched->set_event_callback([this](const pp::FileEvent &ev) {
        // 回调可能来自任意 worker 线程：立即按值拷贝 FileResult（指针仅发射瞬间有效）
        const bool has_result = ev.result != nullptr;
        const pp::FileResult copy = has_result ? *ev.result : pp::FileResult{};
        QMetaObject::invokeMethod(
            this,
            [this, ev, has_result, copy]() {
                pp::FileEvent forwarded = ev;
                forwarded.result = has_result ? &copy : nullptr;
                impl_->handle_file_event(forwarded);
            },
            Qt::QueuedConnection);
    });
    d.sched->start();
    lock_for_run(true); // G5
    d.page_run->begin_run(total, names);
    d.poll->start();
    refresh_status();
}

void MainWindow::on_cancel() {
    if (impl_->sched)
        impl_->sched->cancel(); // 幂等；进行中的文件跑完
}

void MainWindow::on_scheduler_done() {
    Impl &d = *impl_;
    if (!d.sched)
        return;
    d.poll->stop();
    d.sched->wait();
    // 排空在途 queued 事件后再定稿，避免 end_run 之后又被迟到事件改写计数
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    const pp::RunSummary sum = d.sched->summary();
    d.page_run->end_run(sum, d.page_output->out_root());
    d.sched.reset();
    lock_for_run(false);
    impl_->on_content_changed();
    refresh_status();
}

void MainWindow::lock_for_run(bool lock) {
    Impl &d = *impl_;
    d.running = lock;
    d.nav_meta->setEnabled(!lock);
    d.nav_output->setEnabled(!lock);
    d.settings_action->setEnabled(!lock);
    d.presets_action->setEnabled(!lock);
    d.panel->setEnabled(!lock);
    d.page_meta->setEnabled(!lock);
    d.page_output->setEnabled(!lock);
    if (lock)
        set_current_page(3); // stack 锁到运行页
    refresh_status();
}

void MainWindow::refresh_status() {
    Impl &d = *impl_;
    const std::size_t count = d.model->size();
    const std::size_t unsupported = d.model->unsupported_count();
    const std::size_t exceptions = d.model->exception_count();
    const QString reason = d.page_output->ready_to_start();

    QString text;
    bool invalid = false;
    if (d.running) {
        // 运行中：跳过/失败计数同步状态栏（增量计数，避免每个事件 O(n) 重扫）
        text = tr("运行中 · 完成 %1 · 失败 %2 · 跳过 %3")
                   .arg(d.run_done)
                   .arg(d.run_failed)
                   .arg(d.run_skipped);
    } else if (count == 0) {
        text = tr("没有文件");
        invalid = true;
    } else if (!reason.isEmpty()) {
        text = reason;
        invalid = true;
    } else {
        // §9.1 底栏 = 选中摘要 · 输出摘要 · [开始运行]。
        // 勾选集合（参与运行的子集）由 T11 落地；当前参与集 = 全部文件（语义不变）。
        text = tr("已选 %1 / %2 个文件 · 就绪").arg(count).arg(count);
    }
    if (exceptions > 0)
        text += tr(" · %1 个例外").arg(exceptions);
    d.status->set_full_text(text); // §9.3：超长省略号（不折行）
    const QString alert =
        invalid ? QStringLiteral("color:") + theme::css_color(d.tokens.err) : QString();
    d.status->setStyleSheet(alert);
    if (d.status_dot != nullptr)
        d.status_dot->setStyleSheet(alert);

    // 输出摘要（底栏第二段；运行中保持上一次的值，避免逐事件重建 RunConfig）
    if (d.output_status != nullptr && !d.running) {
        QStringList labels;
        for (const pp::OutputFormatSpec &spec : d.page_output->config_base().outputs)
            labels << format_label(QString::fromStdString(spec.format_id));
        if (labels.isEmpty())
            labels << format_label(d.page_output->current_format());
        d.output_status->set_full_text(
            tr("输出：%1 · %2 个格式").arg(labels.join(QStringLiteral(" + "))).arg(labels.size()));
        // 旧状态行里的"输出根目录 / 冲突策略"不丢：收进输出摘要的悬浮提示（§9.3 不折行）
        d.output_status->setToolTip(
            tr("输出根目录：%1\n同名冲突：%2")
                .arg(d.page_output->out_root(),
                     conflict_label(d.page_output->config_base().conflict)));
    }

    // 卡头计数徽标（mockup .pill 内是纯数字）
    d.file_count->set_full_text(QString::number(count));
    d.file_count->setVisible(true);
    d.unsupported->set_full_text(unsupported > 0 ? tr("（%1 个不支持）").arg(unsupported)
                                                 : QString());
    d.unsupported->setVisible(unsupported > 0);

    // G5（2026-09-20 R1 修订）：底栏按钮恒为"开始"；运行中禁用（取消只在运行页）。
    // M4-T9：文案按 mockup .go 改「▶  开始运行」；"运行中变状态"归 W3-T14。
    const bool can_start = !d.running && count > 0 && reason.isEmpty();
    d.start->setText(tr("▶  开始运行"));
    d.start->setEnabled(can_start);
    const bool has_selection = d.view->selectionModel() != nullptr &&
                               !d.view->selectionModel()->selectedIndexes().isEmpty();
    d.remove_sel->setEnabled(!d.running && has_selection);
}

void MainWindow::save_session() {
    Impl &d = *impl_;
    d.settings.last_format = d.page_output->current_format().toStdString();
    d.settings.last_out_root = d.page_output->out_root().toStdString();
    d.settings.last_preset = d.last_preset_path.toStdString();
    pp::save_settings(pp::platform::settings_file(), d.settings);
    d.save_splitter_state(); // §9.1：三栏尺寸持久化（QSettings）
}

// ---------------------------------------------------------------------------
// M4-T9：主题 tokens 落地 / 三栏持久化 / caption 字形
// ---------------------------------------------------------------------------

void MainWindow::Impl::set_theme_mode(theme::ThemeMode mode) {
    theme_mode = mode;
    refresh_theme();
}

void MainWindow::Impl::refresh_theme() {
    tokens = theme::tokens(theme_mode);
    // 调色板 + 基准字族（§9.2 排印）：全局生效，既有页面/对话框随之明暗
    QApplication::setPalette(theme::palette(tokens));
    QApplication::setFont(theme::font(theme::Typography::base_px));
    // 骨架 QSS（只作用于 pp-* 钩子）；标题栏图标/步钮随主题改色
    w->setStyleSheet(theme::style_sheet(tokens));
    if (app_icon != nullptr)
        app_icon->setPixmap(app_icon_pixmap(tokens.accent, 18));
    for (StepButton *step : {step_meta, step_output, step_run}) {
        if (step != nullptr)
            step->set_tokens(tokens);
    }
    // 开始运行钮投影（.go box-shadow 0 2px 10px；QSS 无 box-shadow → 效果器落地）
    if (start != nullptr) {
        if (tokens.go_shadow_blur > 0) {
            auto *go_shadow = qobject_cast<QGraphicsDropShadowEffect *>(start->graphicsEffect());
            if (go_shadow == nullptr) {
                go_shadow = new QGraphicsDropShadowEffect(start);
                start->setGraphicsEffect(go_shadow);
            }
            go_shadow->setColor(tokens.go_shadow_color);
            go_shadow->setBlurRadius(tokens.go_shadow_blur);
            go_shadow->setXOffset(0);
            go_shadow->setYOffset(tokens.go_shadow_dy);
        } else {
            start->setGraphicsEffect(nullptr);
        }
    }
    // 卡片阴影（§9.2 浅色卡 0 1px 4px rgba(16,24,40,.06)；深色无阴影）
    for (QFrame *card : {left_card, preview_card}) {
        if (card == nullptr)
            continue;
        if (tokens.shadow_blur > 0) {
            auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(card->graphicsEffect());
            if (shadow == nullptr) {
                shadow = new QGraphicsDropShadowEffect(card);
                card->setGraphicsEffect(shadow);
            }
            shadow->setColor(tokens.shadow_color);
            shadow->setBlurRadius(tokens.shadow_blur);
            shadow->setXOffset(0);
            shadow->setYOffset(tokens.shadow_dy);
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
    cap_max->setText(maximized ? QStringLiteral("❐") : QStringLiteral("□"));
    cap_max->setToolTip(maximized ? MainWindow::tr("向下还原") : MainWindow::tr("最大化"));
}

void MainWindow::Impl::restore_splitter_state() {
    if (splitter == nullptr || skeleton_state_disabled())
        return;
    QSettings state(ui_state_path(), QSettings::IniFormat);
    const QByteArray saved = state.value(QStringLiteral("ui/splitter")).toByteArray();
    if (!saved.isEmpty())
        splitter->restoreState(saved);
}

void MainWindow::Impl::save_splitter_state() {
    if (splitter == nullptr || skeleton_state_disabled())
        return;
    QSettings state(ui_state_path(), QSettings::IniFormat);
    state.setValue(QStringLiteral("ui/splitter"), splitter->saveState());
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
    pp::LogLevel level = pp::LogLevel::Info;
    if (!parse_log_level_text(d.settings.log_level, level))
        level = pp::LogLevel::Info;
    pp::log_set_level(level);
    d.page_meta->set_map_provider(QString::fromStdString(d.settings.map_provider),
                                  QString::fromStdString(d.settings.amap_key),
                                  d.settings.tile_cache_mb);
    pp::save_settings(pp::platform::settings_file(), d.settings); // 设置持久化：立即落盘
    refresh_status();
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
    case PresetsDialog::Action::Load: {
        pp::PresetData preset;
        const std::string err = pp::ui::load_preset(dlg.path().toStdString(), preset);
        if (!err.empty()) {
            QMessageBox::warning(this, tr("载入预设失败"), QString::fromStdString(err));
            return;
        }
        pp::normalize_preset(preset);
        d.page_output->apply_preset(preset);
        d.page_meta->apply_rules(preset.rules);
        d.last_preset_path = dlg.path();
        refresh_status();
        return;
    }
    case PresetsDialog::Action::SaveAs: {
        const QString name = dlg.name(); // U6 已在对话框内清洗
        if (name.isEmpty())
            return;
        pp::PresetData preset = d.page_output->collect_preset(name);
        preset.rules = d.page_meta->rules(); // rules 归 PageMeta（MainWindow 合并）
        const std::filesystem::path path = dir / (name.toStdString() + ".json");
        const std::string err = pp::ui::save_preset(path, preset);
        if (!err.empty()) {
            QMessageBox::warning(this, tr("保存预设失败"), QString::fromStdString(err));
            return;
        }
        d.last_preset_path = QString::fromStdString(path.string());
        return;
    }
    case PresetsDialog::Action::Delete: {
        const QString path = dlg.path();
        if (path.isEmpty())
            return;
        if (!QFile::remove(path)) {
            QMessageBox::warning(this, tr("删除预设失败"), path);
            return;
        }
        if (d.last_preset_path == path)
            d.last_preset_path.clear();
        return;
    }
    case PresetsDialog::Action::None:
    default:
        return;
    }
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
    page_output->restore_last(QString::fromStdString(settings.last_format),
                              QString::fromStdString(settings.last_out_root));
    page_meta->set_map_provider(QString::fromStdString(settings.map_provider),
                                QString::fromStdString(settings.amap_key), settings.tile_cache_mb);
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

void MainWindow::Impl::on_content_changed() {
    if (!running) {
        QStringList paths;
        paths.reserve(static_cast<int>(model->size()));
        for (std::size_t i = 0; i < model->size(); ++i) {
            paths << QString::fromStdString(model->row(i).entry.src.string());
        }
        if (paths != cached_paths) {
            cached_paths = paths;
            page_meta->set_batch_files(paths); // 首文件时间预览（PageMeta 内部限扫 200）
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
        }
        // 例外编辑只在非运行期发生（编辑器/清除入口在运行中关闭）→ 运行中跳过 O(n) 重扫
        sync_exceptions();
    }
    w->refresh_status();
}

void MainWindow::Impl::sync_selection() {
    QStringList paths;
    bool has = false;
    if (view->selectionModel() != nullptr) {
        for (const QModelIndex &idx : view->selectionModel()->selectedIndexes()) {
            const int row = proxy->mapToSource(idx).row();
            if (row < 0 || static_cast<std::size_t>(row) >= model->size())
                continue;
            paths << QString::fromStdString(
                model->row(static_cast<std::size_t>(row)).entry.src.string());
            has = true;
        }
    }
    page_meta->set_selected_files(paths);
    remove_sel->setEnabled(!running && has);
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
        const int row = proxy->mapToSource(idx).row();
        if (row >= 0 && !rows.contains(row))
            rows << row;
    }
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end());
    model->remove_rows(rows);
}

namespace {

// §M2-T16b(c')：logs_dir() 来自平台字节层，可能含**非 UTF-8 字节**；Qt6 的 QString 恒为
// UTF-8 语义，QString::fromStdString(dir.string()) 会把非法序列替换成 U+FFFD →
// QDir::entryInfoList 直接丢掉全部条目，QUrl 也会指向被改写后的假路径（T14 报告实测）。故：
//   * 枚举一律走 std::filesystem::directory_iterator（目录项名保留原始字节）；
//   * 只有交给 QDesktopServices 时才需要 QString：先做 UTF-8 往返校验，合法 → fromLocalFile；
//     非法 → stderr 明确提示（不静默失败）。
bool bytes_are_valid_utf8(const std::string &bytes) {
    const QByteArray raw(bytes.data(), static_cast<int>(bytes.size()));
    return QString::fromUtf8(raw).toUtf8() == raw;
}

void open_local_path_bytes(const std::filesystem::path &p, const char *what) {
    const std::string bytes = p.string();
    if (!bytes_are_valid_utf8(bytes)) {
        std::fprintf(stderr,
                     "ui: cannot open %s: path contains non-UTF-8 bytes; open it from a shell "
                     "instead (raw: %s)\n",
                     what, bytes.c_str());
        std::fflush(stderr);
        return;
    }
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QString::fromUtf8(bytes.data(), static_cast<int>(bytes.size()))));
}

// 最新 run-*.log：mtime 新者优先；同 mtime 取名字字典序小者（与 QDir::Time 同口径）。
// 无匹配 → 空 path（调用方退回目录本身）。字节级匹配：非 UTF-8 文件名同样命中。
std::filesystem::path newest_run_log(const std::filesystem::path &dir) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newest_time{};
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec) || fec)
            continue;
        const std::string name = it->path().filename().string();
        if (name.size() < 8 || name.compare(0, 4, "run-") != 0 ||
            name.compare(name.size() - 4, 4, ".log") != 0) {
            continue;
        }
        const std::filesystem::file_time_type t = it->last_write_time(fec);
        if (fec)
            continue;
        if (newest.empty() || t > newest_time ||
            (t == newest_time && name < newest.filename().string())) {
            newest = it->path();
            newest_time = t;
        }
    }
    return newest;
}

} // namespace

void MainWindow::Impl::open_logs() {
    const std::filesystem::path dir = pp::platform::logs_dir();
    if (dir.empty()) {
        std::fprintf(stderr, "ui: cannot open logs: data_dir()/logs is unavailable (empty path)\n");
        std::fflush(stderr);
        return;
    }
    const std::filesystem::path newest = newest_run_log(dir);
    open_local_path_bytes(newest.empty() ? dir : newest, "logs");
}

void MainWindow::Impl::handle_file_event(const pp::FileEvent &ev) {
    if (ev.index >= model->size())
        return;
    switch (ev.state) { // 计数先于 set_state：set_state 同步触发 content_changed → refresh_status
    case pp::FileState::Done:
        ++run_terminal;
        ++run_done;
        break;
    case pp::FileState::Skipped:
        ++run_terminal;
        ++run_skipped;
        break;
    case pp::FileState::Failed:
        ++run_terminal;
        ++run_failed;
        break;
    case pp::FileState::Cancelled:
        ++run_terminal; // Scheduler::finish() 对每个文件恰好发一次终态事件
        break;
    default:
        break;
    }
    model->set_state(ev.index, ev.state);
    page_run->on_event(ev);
    w->refresh_status();
}

// ---------------------------------------------------------------------------
// --ui-smoke 脚本化走查（§4.3 冻结断言）
// ---------------------------------------------------------------------------

void MainWindow::ui_smoke_walk(const QString &shots_dir) {
    Impl &d = *impl_;
    setProperty("pp_ui_smoke_ran", true);
    setProperty("pp_ui_smoke_exit", 1);
    d.smoke_failures.clear();
    d.smoke_shots = 0;
    try {
        d.smoke_run(shots_dir);
    } catch (const std::exception &e) {
        d.smoke_fail(tr("未捕获异常：%1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        d.smoke_fail(tr("未捕获异常"));
    }
    if (d.smoke_failures.isEmpty()) {
        std::printf("UI-SMOKE OK shots=%d pages=3\n", d.smoke_shots);
        std::fflush(stdout);
        setProperty("pp_ui_smoke_exit", 0);
    } else {
        for (const QString &failure : d.smoke_failures) {
            std::fprintf(stderr, "UI-SMOKE FAIL %s\n", qUtf8Printable(failure));
        }
        std::fprintf(stderr, "UI-SMOKE FAILED %d\n", static_cast<int>(d.smoke_failures.size()));
        std::fflush(stderr);
    }
}

void MainWindow::Impl::pump(int ms) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    } while (timer.elapsed() < ms);
}

bool MainWindow::Impl::wait_for(const std::function<bool()> &pred, int timeout_ms) {
    QElapsedTimer timer;
    timer.start();
    while (!pred()) {
        if (timer.elapsed() >= timeout_ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return true;
}

void MainWindow::Impl::wait_thumbs(int timeout_ms) {
    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(thumbs, &Thumbnailer::queue_empty, &loop, &QEventLoop::quit);
    guard.start(timeout_ms);
    if (thumbs->pending() > 0)
        loop.exec();
    pump(250); // 让已排队的 ready 结果落到模型
}

void MainWindow::Impl::smoke_fail(const QString &reason) {
    smoke_failures << reason;
    std::fprintf(stderr, "ui-smoke: %s\n", qUtf8Printable(reason));
    std::fflush(stderr);
}

QString MainWindow::Impl::repo_root() {
    static const QString cached = [] {
        QDir dir(QCoreApplication::applicationDirPath());
        for (int i = 0; i < 8; ++i) {
            if (dir.exists(QStringLiteral("CMakeLists.txt")) ||
                dir.exists(QStringLiteral(".git"))) {
                return dir.absolutePath();
            }
            if (!dir.cdUp())
                break;
        }
        return QString();
    }();
    return cached;
}

void MainWindow::Impl::smoke_grab(const QString &dir, const char *name, QWidget *target) {
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QString::fromLatin1(name));
    QWidget *source = target != nullptr ? target : static_cast<QWidget *>(w);
    const QPixmap shot = source->grab();
    if (shot.isNull() || !shot.save(path, "PNG")) {
        smoke_fail(MainWindow::tr("截图保存失败：%1").arg(path));
        return;
    }
    ++smoke_shots;
}

void MainWindow::Impl::smoke_amap_boundary(pp::map::MapWidget *map) {
    const QString prev_provider = QString::fromStdString(settings.map_provider);
    const QString prev_key = QString::fromStdString(settings.amap_key);

    map->set_amap_key(QStringLiteral("ui-smoke-key")); // key 任填；offline 下不发请求
    map->set_provider(QStringLiteral("amap"));
    map->center_on(39.9042, 116.4074, 10);
    pump(150);

    double lat = 0.0;
    double lon = 0.0;
    int hits = 0;
    const QMetaObject::Connection conn = QObject::connect(
        map, &pp::map::MapWidget::point_selected, w, [&lat, &lon, &hits](double la, double lo) {
            lat = la;
            lon = lo;
            ++hits;
        });

    const QPointF local(map->width() / 2.0, map->height() / 2.0);
    const QPointF global(map->mapToGlobal(local.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(map, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(map, &release);
    QObject::disconnect(conn);

    if (hits != 1) {
        smoke_fail(
            MainWindow::tr("amap 边界：视口中心点击未产生 point_selected（hits=%1）").arg(hits));
    } else {
        const double dlat = lat - 39.9042;
        const double dlon = lon - 116.4074;
        if (std::hypot(dlat, dlon) >= 0.001) {
            smoke_fail(MainWindow::tr("amap 边界：选中点偏差 %1°（%2, %3）")
                           .arg(std::hypot(dlat, dlon), 0, 'f', 6)
                           .arg(lat, 0, 'f', 6)
                           .arg(lon, 0, 'f', 6));
        }
    }

    page_meta->set_map_provider(prev_provider, prev_key, settings.tile_cache_mb);
    map->clear_marker();
    pump(80);
}

// ---------------------------------------------------------------------------
// M4-T9 骨架自检：窗口行为矩阵的可自动化部分（手测项见任务书 W5 走查清单：
// Snap 布局悬停 / 双击最大化 / Aero 摸边 / 多显示器 DPI）
// ---------------------------------------------------------------------------

void MainWindow::Impl::smoke_probe_skeleton() {
    // ---- 无边框窗口标志 + 顶栏/底栏高度 + 最小窗口（§9.1 尺寸基准）----
    const bool frameless_flag = (w->windowFlags() & Qt::FramelessWindowHint) != 0;
    std::printf("UI-SMOKE skeleton: frameless=%d titlebar=%d bottom=%d min=%dx%d size=%dx%d\n",
                frameless_flag ? 1 : 0, titlebar != nullptr ? titlebar->height() : -1,
                bottombar != nullptr ? bottombar->height() : -1, w->minimumWidth(),
                w->minimumHeight(), w->width(), w->height());
    std::fflush(stdout);
    if (!frameless_flag)
        smoke_fail(MainWindow::tr("无边框窗口：未置 Qt::FramelessWindowHint"));
    if (titlebar == nullptr || titlebar->height() != theme::Metrics::toolbar_height)
        smoke_fail(MainWindow::tr("顶栏高度不是 %1px").arg(theme::Metrics::toolbar_height));
    if (bottombar == nullptr || bottombar->height() != theme::Metrics::bottom_height)
        smoke_fail(MainWindow::tr("底栏高度不是 %1px").arg(theme::Metrics::bottom_height));
    if (w->minimumWidth() != theme::Metrics::min_window_w ||
        w->minimumHeight() != theme::Metrics::min_window_h) {
        smoke_fail(MainWindow::tr("最小窗口不是 %1×%2")
                       .arg(theme::Metrics::min_window_w)
                       .arg(theme::Metrics::min_window_h));
    }
    if (splitter == nullptr) {
        smoke_fail(MainWindow::tr("三栏 splitter 缺失"));
        return;
    }

    // ---- 三栏尺寸：1440×900 校准（§9.1）----
    const auto check_columns = [this](const char *tag, int left, int mid, int right, int tol) {
        std::printf("UI-SMOKE columns-%s: %d/%d/%d (expect %d/%d/%d ±%d)\n", tag, left, mid, right,
                    theme::Metrics::left_width, theme::Metrics::mid_width_1440,
                    theme::Metrics::right_width_1440, tol);
        std::fflush(stdout);
        const bool ok = std::abs(left - theme::Metrics::left_width) <= tol &&
                        std::abs(mid - theme::Metrics::mid_width_1440) <= tol &&
                        std::abs(right - theme::Metrics::right_width_1440) <= tol;
        if (!ok)
            smoke_fail(MainWindow::tr("三栏尺寸 %1 不符：%2/%3/%4")
                           .arg(QString::fromLatin1(tag))
                           .arg(left)
                           .arg(mid)
                           .arg(right));
    };
    {
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() != 3) {
            smoke_fail(MainWindow::tr("splitter 不是三栏（size=%1）").arg(sizes.size()));
        } else {
            check_columns("1440", sizes.at(0), sizes.at(1), sizes.at(2), 4);
        }
    }

    // ---- 放大：左栏固定 258；中/右弹性（1440 校准比 537:601，§9.4 以 mockup 为准）----
    w->resize(1700, 1000);
    pump(150);
    {
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() == 3) {
            const double ratio = sizes.at(1) > 0 ? double(sizes.at(2)) / double(sizes.at(1)) : 0.0;
            const double expect =
                double(theme::Metrics::right_width_1440) / double(theme::Metrics::mid_width_1440);
            const double tol = theme::Metrics::ratio_tolerance_permille / 1000.0;
            std::printf("UI-SMOKE columns-grown: %d/%d/%d mid:right=%.3f (expect %.3f±%.3f, "
                        "left 固定 %d)\n",
                        sizes.at(0), sizes.at(1), sizes.at(2), ratio, expect, tol,
                        theme::Metrics::left_width);
            std::fflush(stdout);
            if (std::abs(sizes.at(0) - theme::Metrics::left_width) > 2)
                smoke_fail(MainWindow::tr("放大后左栏不再是固定 %1px（实为 %2）")
                               .arg(theme::Metrics::left_width)
                               .arg(sizes.at(0)));
            if (std::abs(ratio - expect) > tol)
                smoke_fail(MainWindow::tr("中:右弹性比 %.3f 偏离校准比 %.3f（±%.3f）")
                               .arg(ratio, 0, 'f', 3)
                               .arg(expect, 0, 'f', 3)
                               .arg(tol, 0, 'f', 3));
        }
    }

    // ---- 最小窗口：1180×720（§9.1）----
    w->resize(theme::Metrics::min_window_w, theme::Metrics::min_window_h);
    pump(150);
    {
        const QList<int> sizes = splitter->sizes();
        std::printf("UI-SMOKE columns-min: %d/%d/%d\n", sizes.value(0), sizes.value(1),
                    sizes.value(2));
        std::fflush(stdout);
        if (sizes.size() == 3 && std::abs(sizes.at(0) - theme::Metrics::left_width) > 2)
            smoke_fail(MainWindow::tr("最小窗口下左栏不是 %1px（实为 %2）")
                           .arg(theme::Metrics::left_width)
                           .arg(sizes.at(0)));
    }

    // ---- 回到校准尺寸（后续 8 张冻结截图的基准）----
    w->resize(theme::Metrics::calibrated_w, theme::Metrics::calibrated_h);
    pump(200);
    {
        const QList<int> sizes = splitter->sizes();
        if (sizes.size() == 3)
            check_columns("recalibrated", sizes.at(0), sizes.at(1), sizes.at(2), 4);
    }

    // ---- 顶栏部件几何（mockup .steps/.step/.capbtns 逐条）----
    {
        const int cap_h = cap_close != nullptr ? cap_close->height() : -1;
        const int cap_w = cap_close != nullptr ? cap_close->width() : -1;
        std::printf("UI-SMOKE caption-geometry: w=%d h=%d (mockup 46×52=满高; §9.1 对照 46×32)\n",
                    cap_w, cap_h);
        std::fflush(stdout);
        if (cap_w != theme::Metrics::caption_btn_w || cap_h != theme::Metrics::toolbar_height) {
            smoke_fail(MainWindow::tr("caption 三钮几何 %1×%2（期望 %3×%4）")
                           .arg(cap_w)
                           .arg(cap_h)
                           .arg(theme::Metrics::caption_btn_w)
                           .arg(theme::Metrics::toolbar_height));
        }
        if (step_meta == nullptr || step_output == nullptr || step_run == nullptr) {
            smoke_fail(MainWindow::tr("步骤切换钮缺失"));
        } else {
            const int h = step_meta->height();
            std::printf("UI-SMOKE steps: meta=%d+%d output=%d+%d run=%d+%d h=%d badge=%d gap=%d "
                        "pad=%d/%d\n",
                        step_meta->x(), step_meta->width(), step_output->x(), step_output->width(),
                        step_run->x(), step_run->width(), h, theme::Metrics::step_badge,
                        theme::Metrics::step_gap, theme::Metrics::step_pad_x,
                        theme::Metrics::step_pad_y);
            std::fflush(stdout);
            const int expect_h = theme::Metrics::step_pad_y * 2 + theme::Metrics::step_badge;
            if (h != expect_h)
                smoke_fail(
                    MainWindow::tr("步钮高度 %1（期望 .step = 6×2+18 = %2）").arg(h).arg(expect_h));
            if (step_output->x() - (step_meta->x() + step_meta->width()) !=
                theme::Metrics::step_gap)
                smoke_fail(MainWindow::tr("步钮间距不是 %1px").arg(theme::Metrics::step_gap));
        }
        if (file_count == nullptr || !file_count->isVisible() || file_count->text().isEmpty()) {
            smoke_fail(MainWindow::tr("卡头计数徽标（.pill）异常：null=%1 visible=%2 text=\"%3\"")
                           .arg(file_count == nullptr ? 1 : 0)
                           .arg(file_count != nullptr && file_count->isVisible() ? 1 : 0)
                           .arg(file_count != nullptr ? file_count->text() : QString()));
        } else {
            std::printf("UI-SMOKE pill: count=%s objectName=%s\n",
                        qUtf8Printable(file_count->text()),
                        qUtf8Printable(file_count->objectName()));
            std::fflush(stdout);
        }
    }
}

void MainWindow::Impl::smoke_probe_frameless() {
    // ---- (a) 纯函数命中判定（合成盒：1440×900 窗口 / 52px 顶栏 / 三钮贴右 46×32）----
    plat::HitBox box;
    box.window = QRect(0, 0, 1440, 900);
    box.caption = QRect(0, 0, 1440, 52);
    box.min_button = QRect(1440 - 3 * 46, 10, 46, 32);
    box.max_button = QRect(1440 - 2 * 46, 10, 46, 32);
    box.close_button = QRect(1440 - 46, 10, 46, 32);
    box.drag_excludes = {QRect(200, 10, 300, 32)}; // 步骤切换钮位（硬约束 1 的被排除区）
    box.border = plat::kResizeBorderPx;
    struct HitCase {
        const char *name;
        QPoint point;
        plat::HitZone expect;
    };
    const HitCase cases[] = {
        {"caption", {700, 26}, plat::HitZone::Caption},
        {"excluded", {250, 26}, plat::HitZone::Client},
        {"min-btn", {1440 - 3 * 46 + 10, 26}, plat::HitZone::MinButton},
        {"max-btn", {1440 - 2 * 46 + 10, 26}, plat::HitZone::MaxButton},
        {"close-btn", {1440 - 46 + 10, 26}, plat::HitZone::CloseButton},
        {"client", {700, 450}, plat::HitZone::Client},
        {"outside", {1500, 450}, plat::HitZone::None},
        {"left", {2, 450}, plat::HitZone::Left},
        {"right", {1438, 450}, plat::HitZone::Right},
        {"top", {700, 2}, plat::HitZone::Top},
        {"bottom", {700, 898}, plat::HitZone::Bottom},
        {"top-left", {2, 2}, plat::HitZone::TopLeft},
        {"top-right", {1438, 2}, plat::HitZone::TopRight},
        {"bottom-left", {2, 898}, plat::HitZone::BottomLeft},
        {"bottom-right", {1438, 898}, plat::HitZone::BottomRight},
    };
    QStringList report;
    int bad = 0;
    for (const HitCase &item : cases) {
        const plat::HitZone got = plat::hit_test(box, item.point);
        report << QStringLiteral("%1=%2").arg(QString::fromLatin1(item.name),
                                              QString::fromLatin1(hit_zone_name(got)));
        if (got != item.expect) {
            ++bad;
            report.last() +=
                QStringLiteral("(≠%1)").arg(QString::fromLatin1(hit_zone_name(item.expect)));
        }
    }
    std::printf("UI-SMOKE hit: %s\n", qUtf8Printable(report.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (bad > 0)
        smoke_fail(MainWindow::tr("命中区判定不符 %1 项：%2").arg(bad).arg(report.join(' ')));

    // ---- (b) 最大化：仍保留 1px 缩放边命中，内侧立即回 Client（硬约束 2）----
    plat::HitBox max_box = box;
    max_box.maximized = true;
    const plat::HitZone edge = plat::hit_test(max_box, {0, 450});
    const plat::HitZone inner = plat::hit_test(max_box, {1, 450});
    const plat::HitZone corner = plat::hit_test(max_box, {1439, 899});
    std::printf("UI-SMOKE hit-maximized: edge=%s inner=%s corner=%s\n", hit_zone_name(edge),
                hit_zone_name(inner), hit_zone_name(corner));
    std::fflush(stdout);
    if (edge != plat::HitZone::Left || inner != plat::HitZone::Client ||
        corner != plat::HitZone::BottomRight)
        smoke_fail(MainWindow::tr("最大化 1px 缩放边命中不符（edge=%1 inner=%2 corner=%3）")
                       .arg(QString::fromLatin1(hit_zone_name(edge)),
                            QString::fromLatin1(hit_zone_name(inner)),
                            QString::fromLatin1(hit_zone_name(corner))));

    // ---- (c) Win32 命令常量（§9.1「三钮行为 SC_*」；数值由 frameless.cpp 的 static_assert 与 SDK
    // 核对）----
    std::printf("UI-SMOKE caption-sc: min=0x%lx max=0x%lx close=0x%lx\n",
                plat::syscommand_for(plat::CaptionAction::Minimize),
                plat::syscommand_for(plat::CaptionAction::Maximize),
                plat::syscommand_for(plat::CaptionAction::Close));
    std::fflush(stdout);
    if (plat::syscommand_for(plat::CaptionAction::Minimize) != 0xF020L ||
        plat::syscommand_for(plat::CaptionAction::Maximize) != 0xF030L ||
        plat::syscommand_for(plat::CaptionAction::Close) != 0xF060L)
        smoke_fail(MainWindow::tr("三钮命令常量不是 SC_MINIMIZE/SC_MAXIMIZE/SC_CLOSE"));

    if (frameless == nullptr) {
        smoke_fail(MainWindow::tr("无边框接线缺失（Frameless::attach 返回空）"));
        return;
    }

    // ---- (d) 真窗口命中：顶栏空白 = 拖拽；三钮 = 各自命中区；控件/列表 = 不拖拽（硬约束 1）----
    const QPoint caption_point =
        titlebar->mapToGlobal(QPoint(titlebar->width() / 2, titlebar->height() / 2));
    // 注意：mapToGlobal 吃**控件自身坐标**，故中心点取 rect().center()（geometry() 是父坐标系）
    const QPoint close_point = cap_close->mapToGlobal(cap_close->rect().center());
    const QPoint step_point = step_output->mapToGlobal(step_output->rect().center());
    const QPoint panel_point = view->mapToGlobal(QPoint(8, 8));
    const plat::HitZone live_caption = frameless->classify_logical(caption_point);
    const plat::HitZone live_close = frameless->classify_logical(close_point);
    const plat::HitZone live_step = frameless->classify_logical(step_point);
    const plat::HitZone live_panel = frameless->classify_logical(panel_point);
    std::printf("UI-SMOKE hit-live: caption=%s close=%s step=%s panel=%s\n",
                hit_zone_name(live_caption), hit_zone_name(live_close), hit_zone_name(live_step),
                hit_zone_name(live_panel));
    std::fflush(stdout);
    if (live_caption != plat::HitZone::Caption || live_close != plat::HitZone::CloseButton ||
        live_step != plat::HitZone::Client || live_panel != plat::HitZone::Client) {
        smoke_fail(MainWindow::tr("真窗口命中不符：顶栏=%1 关闭钮=%2 步骤钮=%3 文件列表=%4")
                       .arg(QString::fromLatin1(hit_zone_name(live_caption)),
                            QString::fromLatin1(hit_zone_name(live_close)),
                            QString::fromLatin1(hit_zone_name(live_step)),
                            QString::fromLatin1(hit_zone_name(live_panel))));
    }

    // ---- (e) 原生命中测试往返（Windows：SendMessage(WM_NCHITTEST) → HT* 实测）----
    // 这是能力表"拖拽移动/八向缩放/最大化钮 Snap/三钮"在**系统眼里**的端到端取证
    // （Snap 悬停与双击最大化是 OS 侧行为，落在 W5 手测清单）。
    bool native_supported = false;
    {
        const qreal dpr = w->devicePixelRatioF();
        const auto physical = [&](const QPoint &logical_global) {
            return plat::to_physical(logical_global, dpr);
        };
        struct NativeCase {
            const char *name;
            QPoint logical_global;
            plat::HitZone expect;
        };
        const QPoint right_edge(titlebar->width() - 1, titlebar->height() / 2);
        const QPoint bottom_edge(titlebar->width() / 2, w->height() - 1);
        const NativeCase native_cases[] = {
            {"caption", titlebar->mapToGlobal(QPoint(titlebar->width() / 2, 26)),
             plat::HitZone::Caption},
            {"close", cap_close->mapToGlobal(cap_close->rect().center()),
             plat::HitZone::CloseButton},
            {"max", cap_max->mapToGlobal(cap_max->rect().center()), plat::HitZone::MaxButton},
            {"min", cap_min->mapToGlobal(cap_min->rect().center()), plat::HitZone::MinButton},
            {"left", w->mapToGlobal(QPoint(0, w->height() / 2)), plat::HitZone::Left},
            {"right", w->mapToGlobal(right_edge), plat::HitZone::Right},
            {"bottom", w->mapToGlobal(bottom_edge), plat::HitZone::Bottom},
            {"client", view->mapToGlobal(QPoint(8, 8)), plat::HitZone::Client},
        };
        QStringList native_report;
        int native_bad = 0;
        for (const NativeCase &item : native_cases) {
            const plat::HitZone got =
                plat::probe_native_hit_test(w, physical(item.logical_global), &native_supported);
            native_report << QStringLiteral("%1=%2").arg(QString::fromLatin1(item.name),
                                                         QString::fromLatin1(hit_zone_name(got)));
            if (native_supported && got != item.expect) {
                ++native_bad;
                native_report.last() +=
                    QStringLiteral("(≠%1)").arg(QString::fromLatin1(hit_zone_name(item.expect)));
            }
        }
        std::printf("UI-SMOKE native-hit: supported=%d %s\n", native_supported ? 1 : 0,
                    qUtf8Printable(native_report.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (native_supported && native_bad > 0)
            smoke_fail(MainWindow::tr("原生命中测试不符 %1 项：%2")
                           .arg(native_bad)
                           .arg(native_report.join(' ')));
    }

    // ---- (f) 三钮命令路由（拦截模式：只取证不真的最小化/关闭）----
    frameless->set_intercept_actions(true);
    QStringList routed;
    const QMetaObject::Connection conn = QObject::connect(
        frameless, &plat::Frameless::caption_action, w, [&routed](plat::CaptionAction action) {
            routed << QString::fromLatin1(caption_action_name(action));
        });
    cap_min->click();
    cap_max->click();
    cap_close->click();
    QObject::disconnect(conn);
    frameless->set_intercept_actions(false);
    std::printf("UI-SMOKE caption-route: %s\n", qUtf8Printable(routed.join(QLatin1Char(','))));
    std::fflush(stdout);
    const QStringList expect{QStringLiteral("minimize"), QStringLiteral("maximize"),
                             QStringLiteral("close")};
    if (routed != expect)
        smoke_fail(MainWindow::tr("三钮命令路由不符：%1").arg(routed.join(',')));
    std::printf("UI-SMOKE frameless-platform: native-hit-test=%d\n",
                plat::Frameless::native_hit_test_supported() ? 1 : 0);
    std::fflush(stdout);
}

void MainWindow::Impl::smoke_probe_theme() {
    const auto dump = [](const char *label, const theme::Tokens &t) {
        std::printf(
            "UI-SMOKE theme-%s: window=%s card=%s card-bd=%s text=%s/%s/%s accent=%s "
            "accent-dim=%s on-accent=%s go-text=%s mod=%s/%s control=%s progress=%s/%s "
            "shadow=(dy=%d blur=%d) go-shadow=(dy=%d blur=%d)\n",
            label, qUtf8Printable(theme::css_color(t.window)),
            qUtf8Printable(theme::css_color(t.card)), qUtf8Printable(theme::css_color(t.card_bd)),
            qUtf8Printable(theme::css_color(t.text)), qUtf8Printable(theme::css_color(t.text2)),
            qUtf8Printable(theme::css_color(t.text3)), qUtf8Printable(theme::css_color(t.accent)),
            qUtf8Printable(theme::css_color(t.accent_dim)),
            qUtf8Printable(theme::css_color(t.on_accent)),
            qUtf8Printable(theme::css_color(t.go_text)), qUtf8Printable(theme::css_color(t.mod_bd)),
            qUtf8Printable(theme::css_color(t.mod_text)),
            qUtf8Printable(theme::css_color(t.control)),
            qUtf8Printable(theme::css_color(t.progress_fill_from)),
            qUtf8Printable(theme::css_color(t.progress_fill_to)), t.shadow_dy, t.shadow_blur,
            t.go_shadow_dy, t.go_shadow_blur);
        std::fflush(stdout);
    };
    set_theme_mode(theme::ThemeMode::Dark);
    pump(80);
    dump("dark", tokens);
    const theme::Tokens dark = tokens;
    if (dark.window != QColor(0x22, 0x25, 0x2b) || dark.text != QColor(0xec, 0xec, 0xf0) ||
        dark.text2 != QColor(0xa9, 0xaa, 0xb4) || dark.text3 != QColor(0x7a, 0x7b, 0x86) ||
        dark.card.alpha() != 13 || dark.go_text != QColor(0x0c, 0x1b, 0x24) ||
        dark.shadow_blur != 0 || dark.go_shadow_blur != 10 || dark.go_shadow_dy != 2 ||
        dark.radius_card != 8 || dark.radius_control != 4 || dark.mod_bd != dark.accent_bd ||
        dark.mod_text != dark.accent || dark.progress_fill_from != QColor(0x3a, 0xa3, 0xdc) ||
        dark.progress_fill_to != QColor(0x4c, 0xc2, 0xff))
        smoke_fail(MainWindow::tr("深色 tokens 与 mockup/:root 不符"));

    set_theme_mode(theme::ThemeMode::Light);
    pump(80);
    dump("light", tokens);
    const theme::Tokens light = tokens;
    if (light.window != QColor(0xe3, 0xe5, 0xea) || light.card != QColor(0xff, 0xff, 0xff) ||
        light.card_bd != QColor(0, 0, 0, 20) || light.text != QColor(0x1b, 0x1c, 0x20) ||
        light.text2 != QColor(0x5d, 0x5f, 0x68) || light.text3 != QColor(0x8b, 0x8d, 0x96) ||
        light.on_accent != QColor(0xff, 0xff, 0xff) || light.go_text != QColor(0x0c, 0x1b, 0x24) ||
        light.shadow_blur != 4 || light.shadow_dy != 1 || light.go_shadow_blur != 10 ||
        light.go_shadow_dy != 2 || light.mod_bd != light.accent_bd ||
        light.mod_text != light.accent)
        smoke_fail(MainWindow::tr("浅色 tokens 与 mockup/:root 不符"));

    // accent（§9.2：系统强调色，mockup 值兜底）：判据 = 调色板样式是否镜像系统（见 theme.h）。
    // 有系统强调色 → 明暗两侧同值；否则逐侧比对 mockup 兜底值
    const bool sys_accent = theme::system_accent_available();
    const QString style_name =
        QApplication::style() != nullptr ? QApplication::style()->objectName() : QString();
    std::printf("UI-SMOKE accent-source: %s (style=%s mirrors-system=%d)\n",
                sys_accent ? "system-accent" : "mockup-fallback", qUtf8Printable(style_name),
                theme::style_mirrors_system_accent() ? 1 : 0);
    std::fflush(stdout);
    if (sys_accent) {
        if (dark.accent != light.accent)
            smoke_fail(MainWindow::tr("系统强调色下明暗 accent 不一致（%1/%2）")
                           .arg(theme::css_color(dark.accent), theme::css_color(light.accent)));
    } else if (dark.accent != QColor(0x4c, 0xc2, 0xff) ||
               light.accent != QColor(0x00, 0x67, 0xc0)) {
        smoke_fail(MainWindow::tr("无系统强调色时 accent 未退回 mockup 值（%1/%2）")
                       .arg(theme::css_color(dark.accent), theme::css_color(light.accent)));
    }

    // ---- 明暗跟随系统（§9.2 末行）----
    // 处理路径 = system_theme_mode()（读 QStyleHints::colorScheme）→ set_theme_mode()，
    // 由 wire() 的 colorSchemeChanged 槽驱动。offscreen 平台不派发该信号（下面的
    // signal-fired 只打印不判死），故这里对**解析 + 落地**两段分别取证：
    set_theme_mode(theme::ThemeMode::Dark);
    pump(40);
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    pump(80);
    const Qt::ColorScheme hint = QGuiApplication::styleHints()->colorScheme();
    const theme::ThemeMode resolved = theme::system_theme_mode();
    const bool signal_fired = !tokens.dark(); // 槽被派发 → tokens 已翻到浅色
    std::printf("UI-SMOKE theme-follow: hint=%s resolved=%s signal-fired=%d applied=%s\n",
                hint == Qt::ColorScheme::Light
                    ? "light"
                    : (hint == Qt::ColorScheme::Dark ? "dark" : "unknown"),
                resolved == theme::ThemeMode::Light ? "light" : "dark", signal_fired ? 1 : 0,
                tokens.dark() ? "dark" : "light");
    std::fflush(stdout);
    if (hint == Qt::ColorScheme::Light && resolved != theme::ThemeMode::Light)
        smoke_fail(MainWindow::tr("system_theme_mode() 未跟随 QStyleHints::colorScheme"));
    set_theme_mode(resolved); // 与信号槽同一条处理路径
    pump(40);
    if (resolved == theme::ThemeMode::Light && tokens.dark())
        smoke_fail(MainWindow::tr("set_theme_mode(light) 未落地浅色 tokens"));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    pump(80);
    set_theme_mode(theme::system_theme_mode());
    pump(40);
    if (tokens.dark() != true)
        smoke_fail(MainWindow::tr("色板回到深色后 tokens 未回深色"));

    // 截图基准 = 启动口径（PP_UI_THEME 强制档 / 跟随系统；原型基准是 meta-dark）
    set_theme_mode(preferred_theme_mode());
    pump(80);
    std::printf("UI-SMOKE theme-baseline: %s\n", tokens.dark() ? "dark" : "light");
    std::fflush(stdout);
}

void MainWindow::Impl::smoke_probe_lifecycle() {
    // 窗口构造/销毁（第二实例）+ 最小尺寸约束（§9.1 1180×720）
    auto *second = new MainWindow(pp::AppSettings{}, nullptr);
    second->resize(600, 400); // 低于最小窗口 → 应被钳到 1180×720
    second->show();
    pump(150);
    const QSize size = second->size();
    const bool frameless_flag = (second->windowFlags() & Qt::FramelessWindowHint) != 0;
    std::printf("UI-SMOKE lifecycle: second-window=%dx%d frameless=%d\n", size.width(),
                size.height(), frameless_flag ? 1 : 0);
    std::fflush(stdout);
    if (size.width() != theme::Metrics::min_window_w ||
        size.height() != theme::Metrics::min_window_h)
        smoke_fail(MainWindow::tr("最小尺寸未生效：%1×%2").arg(size.width()).arg(size.height()));
    if (!frameless_flag)
        smoke_fail(MainWindow::tr("第二窗口未置 Qt::FramelessWindowHint"));
    delete second; // 不走 close()：避免 Close 过滤器写会话
    pump(80);
    std::printf("UI-SMOKE lifecycle: destroyed ok\n");
    std::fflush(stdout);
}

void MainWindow::Impl::smoke_run(const QString &shots_dir) {
    // ---- M4-T9 骨架自检（窗口行为矩阵可自动化部分；先跑，之后截图归位 1440×900）----
    smoke_probe_lifecycle();
    smoke_probe_skeleton();
    smoke_probe_frameless();
    smoke_probe_theme();

    // ---- 页 1（元数据）：等缩略图队列空 → 01-meta.png ----
    w->set_current_page(1);
    pump(200);
    wait_thumbs(kThumbWaitMs);
    smoke_grab(shots_dir, kSmokeShots[kShotMeta]);

    // ---- 断言 a：MapWidget 存在且 grab 非空 ----
    pp::map::MapWidget *map = w->findChild<pp::map::MapWidget *>();
    if (map == nullptr) {
        smoke_fail(MainWindow::tr("地图控件缺失（findChild<MapWidget*> 为空）"));
    } else {
        const QImage image = map->grab().toImage();
        if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
            smoke_fail(MainWindow::tr("地图 grab 返回空图"));
        }
        // ---- 断言 b：amap 基准面边界（GCJ-02 ↔ WGS-84）----
        smoke_amap_boundary(map);
    }

    // ---- 页 2（输出）：参数谓词 → 02-output.png ----
    w->set_current_page(2);
    pump(200);
    page_output->select_format(QStringLiteral("jxl"));
    pump(300);

    ParamForm *form = w->findChild<ParamForm *>(QStringLiteral("pp-param-form"));
    if (form == nullptr) {
        smoke_fail(MainWindow::tr("输出页 ParamForm 缺失（pp-param-form）"));
    } else {
        if (!form->is_param_visible("distance")) {
            smoke_fail(MainWindow::tr("jxl：distance 参数应可见"));
        }
        // jxl + 无损 → 自动 modular 且 distance 锁定 0.0（§4.3 谓词断言）
        form->set_selection(FormSelection{std::string(), std::string(), true});
        const FormSelection lossless_sel = form->selection();
        if (lossless_sel.tech != "modular") {
            smoke_fail(MainWindow::tr("jxl 无损：技术未切到 modular（当前 %1）")
                           .arg(QString::fromStdString(lossless_sel.tech)));
        }
        const pp::ParamSet values = form->values();
        const auto it = values.find("distance");
        const double *distance = it == values.end() ? nullptr : std::get_if<double>(&it->second);
        if (distance == nullptr || *distance != 0.0) {
            smoke_fail(MainWindow::tr("jxl 无损：distance 未锁定为 0.0"));
        }
        form->set_selection(FormSelection{}); // 还原首个后端/技术
        pump(120);
    }
    // 复位 jxl 默认参数（上面的无损断言把 distance 锁到 0.0；保留了截图就不是默认态，
    // 实跑前置也要求 "jxl 默认参"）。select_format 无条件重建 ParamForm → 回到表内默认值。
    page_output->select_format(QStringLiteral("jxl"));
    pump(200);
    smoke_grab(shots_dir, kSmokeShots[kShotOutput]);

    // ---- 02b-output-avif.png：avif 运行时内省 + 位深探测落定（§4.3）→ 还原 jxl ----
    page_output->select_format(QStringLiteral("avif"));
    pump(300);
    smoke_grab(shots_dir, kSmokeShots[kShotAvif]);
    page_output->select_format(QStringLiteral("jxl"));
    pump(200);

    // jpeg：quality_mode=quality → quality 可见且 distance 隐藏（§4.3）
    page_output->select_format(QStringLiteral("jpeg"));
    pump(300);
    form = w->findChild<ParamForm *>(QStringLiteral("pp-param-form"));
    if (form == nullptr) {
        smoke_fail(MainWindow::tr("jpeg：ParamForm 缺失"));
    } else {
        form->set_values(pp::ParamSet{{"quality_mode", std::string("quality")}});
        pump(80);
        if (!form->is_param_visible("quality")) {
            smoke_fail(MainWindow::tr("jpeg（quality 模式）：quality 应可见"));
        }
        if (form->is_param_visible("distance")) {
            smoke_fail(MainWindow::tr("jpeg（quality 模式）：distance 应隐藏"));
        }
    }

    // tiff：compression 恒可见；compression=none → deflate_level 隐藏（§4.3）
    page_output->select_format(QStringLiteral("tiff"));
    pump(300);
    form = w->findChild<ParamForm *>(QStringLiteral("pp-param-form"));
    if (form == nullptr) {
        smoke_fail(MainWindow::tr("tiff：ParamForm 缺失"));
    } else {
        if (!form->is_param_visible("compression")) {
            smoke_fail(MainWindow::tr("tiff：compression 应可见"));
        }
        form->set_values(pp::ParamSet{{"compression", std::string("none")}});
        pump(80);
        if (form->is_param_visible("deflate_level")) {
            smoke_fail(MainWindow::tr("tiff（none）：deflate_level 应隐藏"));
        }
    }

    // ---- 实跑前置：jxl + out_root=<仓库>/.cache/tmp/ui-smoke-out + conflict=overwrite ----
    page_output->select_format(QStringLiteral("jxl"));
    pump(250);
    QString root_dir = repo_root();
    if (root_dir.isEmpty()) {
        root_dir = QDir::currentPath();
        std::fprintf(stderr, "ui-smoke: repo root not found; out_root falls back to %s\n",
                     qUtf8Printable(root_dir));
    }
    const QString out_root = root_dir + QStringLiteral("/.cache/tmp/ui-smoke-out");
    QDir(out_root).removeRecursively();
    if (!QDir().mkpath(out_root)) {
        smoke_fail(MainWindow::tr("无法创建输出目录：%1").arg(out_root));
    }
    page_output->set_out_root(out_root);
    // conflict 无独立 setter → 用冻结 API 走一次 collect/apply 往返（其余字段保持现状）
    pp::PresetData preset = page_output->collect_preset(QStringLiteral("ui-smoke"));
    preset.conflict = pp::ConflictPolicy::Overwrite;
    page_output->apply_preset(preset);
    pump(200);
    if (page_output->current_format() != QLatin1String("jxl")) {
        smoke_fail(
            MainWindow::tr("冒烟前置：格式不是 jxl（%1）").arg(page_output->current_format()));
    }
    if (page_output->out_root() != out_root) {
        smoke_fail(MainWindow::tr("冒烟前置：输出根目录未生效（%1）").arg(page_output->out_root()));
    }
    if (page_output->config_base().conflict != pp::ConflictPolicy::Overwrite) {
        smoke_fail(MainWindow::tr("冒烟前置：冲突策略不是覆盖"));
    }

    // ---- 页 3（运行）：点开始 → 03-run.png ----
    w->set_current_page(3);
    pump(200);
    start->click();
    // 尽量抓在"运行中"：本批 16 个小文件 ~150ms 就跑完，故只推进到出现部分进度即抓，
    // 否则退化为结束态（与 03b 同图，仅记入 stdout 供审查判断）。
    QProgressBar *run_progress = w->findChild<QProgressBar *>(QStringLiteral("runProgress"));
    {
        QElapsedTimer spin;
        spin.start();
        while (spin.elapsed() < 60) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            const int value = run_progress != nullptr ? run_progress->value() : 0;
            const int maximum = run_progress != nullptr ? run_progress->maximum() : 0;
            if (page_run->is_running() && maximum > 0 && value > 0 && value < maximum)
                break;
            QThread::msleep(1);
        }
    }
    std::printf("UI-SMOKE 03-run: running=%d progress=%d/%d\n", page_run->is_running() ? 1 : 0,
                run_progress != nullptr ? run_progress->value() : -1,
                run_progress != nullptr ? run_progress->maximum() : -1);
    std::fflush(stdout);
    smoke_grab(shots_dir, kSmokeShots[kShotRun]);

    if (!wait_for([this] { return !page_run->is_running(); }, kRunWaitMs)) {
        smoke_fail(MainWindow::tr("运行未在 %1 ms 内结束").arg(kRunWaitMs));
        if (sched)
            sched->cancel();
        wait_for([this] { return !page_run->is_running(); }, 30000);
    }
    pump(300);
    smoke_grab(shots_dir, kSmokeShots[kShotRunDone]);

    // ---- 断言 d：结束态（未运行 / 进度满 / 摘要可见）----
    if (page_run->is_running()) {
        smoke_fail(MainWindow::tr("运行结束后 is_running() 仍为真"));
    }
    QProgressBar *progress = w->findChild<QProgressBar *>(QStringLiteral("runProgress"));
    if (progress == nullptr) {
        smoke_fail(MainWindow::tr("运行页缺少 runProgress"));
    } else if (progress->maximum() <= 0 || progress->value() != progress->maximum()) {
        smoke_fail(
            MainWindow::tr("进度条未满：%1 / %2").arg(progress->value()).arg(progress->maximum()));
    }
    QGroupBox *summary = w->findChild<QGroupBox *>(QStringLiteral("runSummary"));
    if (summary == nullptr || !summary->isVisible()) {
        smoke_fail(MainWindow::tr("运行摘要不可见"));
    }
    if (QLabel *run_status = w->findChild<QLabel *>(QStringLiteral("runStatus"))) {
        std::printf("UI-SMOKE run status: %s\n", qUtf8Printable(run_status->text()));
    }
    if (QLabel *summary_text = w->findChild<QLabel *>(QStringLiteral("runSummaryText"))) {
        const QString text =
            QString(summary_text->text()).replace(QLatin1Char('\n'), QLatin1String(" / "));
        std::printf("UI-SMOKE summary: %s\n", qUtf8Printable(text));
    }
    std::fflush(stdout);

    // ---- 对话框三连（构造 → show → processEvents → grab → close，绝不 exec）----
    { // 04-settings.png：设置对话框
        SettingsDialog dlg(settings, w);
        dlg.show();
        pump(250);
        smoke_grab(shots_dir, kSmokeShots[kShotSettings], &dlg);
        dlg.close();
        pump(80);
    }
    { // 05-exif-editor.png：确定性 fixture 的 EXIF 编辑器（reject：不产生编辑）
        // §M2-T16：不再取 model->row(0)——那依赖 readdir 顺序（CI 干净 checkout 必红）。
        const std::optional<pp::FileEntry> fixture = pick_smoke_exif_fixture(model->entries());
        if (model->empty()) {
            smoke_fail(MainWindow::tr("文件列表为空，无法打开 EXIF 编辑器"));
        } else if (!fixture.has_value()) {
            smoke_fail(MainWindow::tr("EXIF 编辑器：语料中没有含 IFD0 叶子的候选 fixture"
                                      "（已枚举 %1 个文件；候选：%2）")
                           .arg(static_cast<int>(model->size()))
                           .arg(smoke_exif_fixture_names()));
        } else {
            const pp::FileEntry &entry = *fixture;
            const bool fixture_had_exception = entry.exception.has_value();
            // 诊断行（新增，不改冻结末行）：CI 日志据此核对本次实际使用的 fixture
            std::printf("UI-SMOKE exif-fixture=%s\n",
                        qUtf8Printable(smoke_fixture_label(entry.src, repo_root())));
            std::fflush(stdout);
            ExifEditor dlg(QString::fromStdString(entry.src.string()), page_meta->rules(),
                           entry.exception, w);
            dlg.setProperty("pp_exif_editor_suppress_modal", true); // 模态短路（U8 口径）
            dlg.show();
            pump(400);
            // §9.1 U10-FIX：先程序化选中一个叶子标签（IFD0 下首个），使右值区有键名+值再 grab
            // （U8 挂在 selectionModel 的 currentChanged 上，setCurrentItem 同样触发）
            if (QTreeWidget *tree = dlg.findChild<QTreeWidget *>(QStringLiteral("exif_tree"))) {
                QTreeWidgetItem *group = nullptr;
                for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                    QTreeWidgetItem *node = tree->topLevelItem(i);
                    if (node != nullptr && node->text(0) == QStringLiteral("IFD0")) {
                        group = node;
                        break;
                    }
                }
                if (group == nullptr && tree->topLevelItemCount() > 0) {
                    group = tree->topLevelItem(0);
                }
                QTreeWidgetItem *leaf = nullptr;
                if (group != nullptr) {
                    tree->expandItem(group);
                    for (int i = 0; i < group->childCount(); ++i) {
                        QTreeWidgetItem *child = group->child(i);
                        if (child != nullptr && child->childCount() == 0) {
                            leaf = child;
                            break;
                        }
                    }
                }
                if (leaf == nullptr) {
                    smoke_fail(MainWindow::tr("EXIF 编辑器：IFD0 下找不到叶子标签，右值区将为空"));
                } else {
                    tree->setCurrentItem(leaf);
                    pump(200);
                    std::printf("UI-SMOKE 05-exif-editor selection: %s\n",
                                qUtf8Printable(leaf->text(0)));
                    std::fflush(stdout);
                }
            } else {
                smoke_fail(MainWindow::tr("EXIF 编辑器缺少 exif_tree 控件"));
            }
            pump(150);
            smoke_grab(shots_dir, kSmokeShots[kShotExif], &dlg);
            dlg.reject();
            pump(120);
            // §M2-T16b：断言绑定到本 fixture 行（不再引用 row(0)，与输入顺序彻底解耦）：
            // reject 只能不改变该行的例外状态。
            bool fixture_found = false;
            bool fixture_exception_now = false;
            for (std::size_t i = 0; i < model->size(); ++i) {
                if (model->row(i).entry.src == entry.src) {
                    fixture_exception_now = model->row(i).has_exception();
                    fixture_found = true;
                    break;
                }
            }
            if (!fixture_found) {
                smoke_fail(MainWindow::tr("EXIF 编辑器：reject 后 fixture 行从列表消失"));
            } else if (fixture_exception_now != fixture_had_exception) {
                smoke_fail(MainWindow::tr("EXIF 编辑器 reject 后产生了例外"));
            }
        }
    }
    { // 06-presets.png：预设对话框（§4.3 冻结：空列表 + suggested_name "sample"）
        // M2-T21c：示例名必须是能通过 sanitize_name 的 ASCII（U6 冻结口径 `[A-Za-z0-9_\- ]`）。
        // 旧值 "示例预设" 被清洗成空串 → has_name=false →"另存为"灰化，截图里"框内有名字却
        // 按不动"，与空态提示"输入名称后点\"另存为\"创建"自相矛盾。
        const std::vector<std::pair<QString, QString>> empty_list;
        PresetsDialog dlg(empty_list, QStringLiteral("sample"), w);
        dlg.show();
        pump(250);
        smoke_grab(shots_dir, kSmokeShots[kShotPresets], &dlg);
        dlg.close();
        pump(80);
    }

    // ---- 断言 e：8 张 PNG 全部存在且 >10KB ----
    if (shots_dir.isEmpty()) {
        std::fprintf(stderr, "ui-smoke: --shots 未给：跳过截图与体积断言（CI 模式）\n");
        smoke_shots = 0;
    } else {
        for (int i = 0; i < kSmokeShotCount; ++i) {
            const char *name = kSmokeShots[i];
            const QFileInfo info(QDir(shots_dir).filePath(QString::fromLatin1(name)));
            std::printf("UI-SMOKE shot %s %lld bytes\n", name, static_cast<long long>(info.size()));
            if (!info.exists() || !info.isFile()) {
                smoke_fail(MainWindow::tr("截图缺失：%1").arg(name));
            } else if (info.size() <= min_shot_bytes(i)) {
                smoke_fail(MainWindow::tr("截图过小（%1 字节，阈值 %2）：%3")
                               .arg(info.size())
                               .arg(min_shot_bytes(i))
                               .arg(name));
            }
        }
        std::fflush(stdout);
        if (smoke_shots != kSmokeShotCount) {
            smoke_fail(MainWindow::tr("截图计数 %1 ≠ %2").arg(smoke_shots).arg(kSmokeShotCount));
        }
    }
}

} // namespace pp::ui
