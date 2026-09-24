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
#include <QComboBox>
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
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleHints>
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

#include "core/logger.h"
#include "core/settings.h"
#include "decode/oiio_reader.h" // M4-T12：探针的 probe 摘要（与列表模型同一条 oiio probe 通道）
#include "mapwidget/mapwidget.h"
#include "platform/frameless.h"
#include "platform/mica.h"
#include "platform/paths.h"
#include "ui/classify_panel.h"
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
#include "ui/thumbnails.h"

namespace pp::ui {
namespace {

constexpr int kPollIntervalMs = 150; // §2.14：运行轮询间隔
constexpr int kThumbWaitMs = 10000;  // §4.3：缩略图队列空上限
constexpr int kRunWaitMs = 120000;   // U10 冻结：冒烟实跑上限
constexpr int kSmokeShotCount = 12;
constexpr qint64 kMinShotBytes = 10 * 1024;

// 冒烟截图清单（顺序与名称逐字节固定；M4-T14 起 = 12 张：§4.3 冻结 8 张 + 运行页 4 张）
//   * 03-run / 03b-run-done：**真实运行**下的运行页（运行中锁定态 / 结束态）；
//   * 03c-run-idle：运行页空闲态（三卡空态 + 引导）；
//   * 03d-run-rows：运行页逐输出行探针态（真实/斜纹/完成产物/失败原因 四类同行）；
//   * 03e-run-cancel：运行页取消态（取消唯一入口 → 取消后定稿）；
//   * 07-run-light：浅色主题运行页（明暗双主题覆盖）。
const char *const kSmokeShots[kSmokeShotCount] = {
    "01-meta.png",      "02-output.png",      "02b-output-avif.png", "03-run.png",
    "03b-run-done.png", "03c-run-idle.png",   "03d-run-rows.png",    "03e-run-cancel.png",
    "04-settings.png",  "05-exif-editor.png", "06-presets.png",      "07-run-light.png",
};
constexpr int kShotMeta = 0;
constexpr int kShotOutput = 1;
constexpr int kShotAvif = 2;
constexpr int kShotRun = 3;
constexpr int kShotRunDone = 4;
constexpr int kShotRunIdle = 5;
constexpr int kShotRunRows = 6;
constexpr int kShotRunCancel = 7;
constexpr int kShotSettings = 8;
constexpr int kShotExif = 9;
constexpr int kShotPresets = 10;
constexpr int kShotRunLight = 11;

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
// M4-W2-fix：解析搬到 theme::preferred_theme_mode()（单源 = ui/theme.h）——main.cpp 的启动期
// DWM 深色标题栏（apply_window_backdrop）必须与 GUI 落地的是**同一个**模式，否则窗口边框会
// 按系统色滞后（第 11 条）。
using theme::preferred_theme_mode;

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
        const bool enabled = isEnabled();
        // 运行期锁定（G5）：mockup .step.dim{opacity:.45} 是**整钮**（含 18×18 编号徽标）变暗
        // —— 自绘路径用 painter 不透明度落地（QSS 无控件级 opacity）。
        painter.setOpacity(enabled ? 1.0 : theme::Metrics::step_dim_opacity);
        const bool active = isChecked() && enabled;
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
        // 文案（§9.3 禁止折行 → 超长省略号）。禁用态的字色仍是 .step 的 --txt2：变暗由上面的
        // 整钮不透明度（.step.dim opacity .45）承担，不再叠加第二层 text3（否则比原型暗一档）。
        painter.setFont(font());
        painter.setPen(active ? tokens_.text : tokens_.text2);
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

// 图标钮（mockup .icon-btn 32×32 r=6）。
// 字形字体（M4-W2-fix）：☆/⚙ 在 Windows 上会命中 **彩色 emoji** 字体（Segoe UI Emoji）→ 字形
// 自带颜色，QSS 的 color/disabled 全部失效（原型 meta-dark.png 里两枚字形都是 --txt2 的单色文本）。
// 故显式把"符号文本字体"排在字族栈最前（Segoe UI Symbol / Noto Sans Symbols 2 均有 U+2606/U+2699
// 的单色字形）；找不到该字体的平台自然落回原字族栈，行为不变。
QToolButton *make_icon_button(QWidget *parent, QAction *action, const char *object_name) {
    auto *button = new QToolButton(parent);
    button->setObjectName(QString::fromLatin1(object_name));
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFixedSize(theme::Metrics::icon_btn, theme::Metrics::icon_btn);
    QFont glyph_font = theme::font(14.0);
    glyph_font.setFamilies({QStringLiteral("Segoe UI Symbol"), QStringLiteral("Segoe UI"),
                            QStringLiteral("Noto Sans Symbols 2"),
                            QString::fromLatin1(theme::Typography::family_ui)});
    button->setFont(glyph_font);
    button->setFocusPolicy(Qt::NoFocus);
    if (action != nullptr)
        button->setDefaultAction(action); // 文本/提示/enabled 全跟 QAction（lock_for_run 靠它置灰）
    return button;
}

// caption 三钮（§9.1 能力表：46 宽；命中区 46×32、hover 面贴满顶栏 52px —— 双轨口径见
// theme::Metrics::caption_btn_hit_h / caption_btn_h）。关闭悬停 #c42b1c 在 tokens/QSS 里。
//
// 双轨落地：
//   * 可见面 = 控件自身 46×满高 → QSS :hover 背景铺满 52px（原型 .capbtns{align-self:stretch}）；
//   * 命中 = 居中的 46×32 带（§9.1 明文）→ hitButton 只认带内（Ctrl/程序化 click() 不受影响），
//     无边框窗口的 WM_NCHITTEST 用同一条带（platform/frameless.cpp 的 rect_of(caption_button)）。
class CaptionButton : public QPushButton {
public:
    explicit CaptionButton(const QString &glyph, QWidget *parent) : QPushButton(glyph, parent) {}

    // 命中带（居中）：与 platform/frameless.cpp 的 rect_of(..., caption_button=true) 同一条带
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
};

QPushButton *make_caption_button(QWidget *parent, const QString &glyph, const QString &tip,
                                 const char *object_name) {
    auto *button = new CaptionButton(glyph, parent);
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
    ElidedLabel *preview_pill = nullptr; // 中栏卡头徽标（文件名，T10 填）
    ElidedLabel *preview_hint = nullptr; // 中栏卡头右端 hint（尺寸·位深·色彩空间，T10 填）
    QWidget *left_col = nullptr;         // 左栏容器（文件卡 + 分类卡，§9.1 左栏 258 固定）
    QFrame *left_card = nullptr;         // 左栏文件卡框（T11 填内容）
    QFrame *classify_card = nullptr;     // 左栏分类卡框（T11）
    QFrame *preview_card = nullptr;      // 中栏卡框（内嵌 T10 的 PreviewPanel）
    // ---- M4-T10：常驻输入预览面板（§6.1）+ 分类注册表（热键表数据源）----
    PreviewPanel *preview_panel = nullptr;
    // 分类注册表的**当前实例**（T10 只读消费：热键提示表；T11 接管 CRUD/持久化/打标与勾选语义）
    pp::ClassRegistry classes;

    // ---- 左：文件面板（M1b 内容 + M4-T11 勾选/分组）----
    QWidget *panel = nullptr;
    FileListModel *model = nullptr;
    Thumbnailer *thumbs = nullptr;
    QSortFilterProxyModel *proxy = nullptr; // 搜索过滤（§6.2：与勾选正交）
    FileGroupProxyModel *groups = nullptr;  // M4-T11：分组节层次层（QTreeView 的数据源）
    FileTreeView *view = nullptr;           // M4-T11：QListView → QTreeView（§3.6 裁定行）
    QComboBox *group_mode = nullptr;        // 分组：按月份 | 按相机型号 | 按源格式 | 无分组
    QLabel *groupby_label = nullptr;        // 「分组：」（.groupby 文案，--txt3）
    ElidedLabel *file_count = nullptr;      // 卡头计数徽标（.pill）
    ElidedLabel *unsupported = nullptr;
    ElidedLabel *file_hint = nullptr; // 卡头 hint（"拖放添加" / "运行中锁定"）
    QLineEdit *search = nullptr;
    QToolButton *add_btn = nullptr;      // ＋（添加文件…/添加文件夹… 菜单）
    QToolButton *remove_sel = nullptr;   // −（移除所选）
    QAction *add_files_action = nullptr; // ＋ 菜单项：添加文件…
    QAction *add_dir_action = nullptr;   // ＋ 菜单项：添加文件夹…
    QAction *clear_action = nullptr;     // ＋ 菜单项：清空列表
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

    // ---- 运行状态 ----
    std::unique_ptr<pp::Scheduler> sched;
    QTimer *poll = nullptr;
    // M4-T14：日志尾卡喂入（§9.3「日志尾卡（5 行滚动 + 跳转）」）——单一持有者 = MainWindow：
    // 运行期每 500ms 读最新 run-*.log 的末尾若干行推给运行页（页只渲染，不做文件 IO）
    QTimer *log_tail = nullptr;
    bool running = false;
    // 完成检测（§2.14 的"轮询 !running()"机制不可用：pp::Scheduler::running() 只在 wait() 内清零，
    // 见报告 api-deltas）→ 轮询改为"终态事件计数 == 本批文件数"，语义等价且不阻塞 GUI。
    std::size_t run_total = 0;
    std::size_t run_terminal = 0;
    std::size_t run_done = 0;
    std::size_t run_failed = 0;
    std::size_t run_skipped = 0;
    // M4-T11：本批提交的**模型行号**（ev.index 是提交序号 → 需映射回列表行更新状态）
    std::vector<std::size_t> run_rows;
    QString last_selected_path; // 分组重建后恢复选中（重建 = 模型 reset，选中会丢）
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
    MetaSelectionItem meta_selection_item(const FileRow &row); // M4-T12：跟随项快照（零 IO 探测）
    void sync_exceptions();
    void clear_exception(const QString &path);
    void remove_selected();
    void restore_session();
    void open_logs();
    void push_log_tail(); // M4-T14：run-*.log 末尾 5 行 → 运行页日志尾卡
    void handle_file_event(const pp::FileEvent &ev);
    void restore_selection_by_path(const QString &path);

    // M4-T9 骨架：主题 / 三栏持久化 / caption 行为
    void refresh_theme();
    void set_theme_mode(theme::ThemeMode mode);
    void restore_splitter_state();
    void save_splitter_state();
    void update_caption_buttons();

    // M4-T11：左栏（勾选/分组/分类）
    void load_classes(); // classes.json → 注册表（失败仅告警，保持默认）
    void save_classes(); // 注册表 → classes.json（打标/CRUD 即写）
    void apply_class_to_selection(const QString &class_id); // 热键/菜单打标（选中集或当前项）
    void sync_class_views(std::size_t row);                 // 面板高亮 + 预览热键表刷新
    void rebuild_group_view();     // 分组结构变化后：跨列节头 + 展开态 + 选中恢复
    void apply_group_view_state(); // 上述状态的落地（推迟到下一轮事件循环）
    void set_group_mode(pp::GroupMode mode);

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
    // M4-T11 文件列表/分类自检（§3.6 roles + §6.2 勾选/正交搜索/分组节/分类面板）
    void smoke_probe_filelist();
    void smoke_probe_classify();
    // M4-T12 元数据页自检（§5.2：生效值显示与写入一致性 + .mod + 多选小表 + 无选中提示 + 地图跟随）
    void smoke_probe_meta(const QString &shots_dir);
    MetaSelectionItem meta_probe_item(const QString &path); // 探针的跟随项快照（oiio probe 补摘要）
    // M4-T13 预设卡（页面信号 → 单一持有者：I/O + rules 合并 + 列表刷新）
    void refresh_preset_card();
    void load_preset_file(const QString &path);
    void save_preset_named(const QString &name);
    void delete_preset_file(const QString &path);
    // M4-T14 运行页自检（§3.4/§7：逐输出行 · 真实/斜纹像素取证 · 失败原因 · 取消态 · 浅色）
    void smoke_probe_run(const QString &shots_dir);
    static QString repo_root();
};

MainWindow::Impl::Impl(MainWindow *owner, const pp::AppSettings &s)
    : w(owner), settings(s), theme_mode(preferred_theme_mode()) {
    // M4-T11：分类注册表路径（§3.6/§6.2 单源 = settings.class_file；空 = 默认
    // data_dir()/classes.json）
    classes_file =
        QString::fromStdString(s.class_file.empty() ? pp::default_class_file() : s.class_file);
    model = new FileListModel(w);
    model->set_class_registry(&classes); // 分类单源：模型只读/直写注册表，不复制状态
    thumbs = new Thumbnailer(96, w);
    model->set_thumbnailer(thumbs); // U4 口径：内部已 connect(ready→apply_thumb) + 自动 enqueue

    proxy = new QSortFilterProxyModel(w);
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
    case QEvent::Show:
        // M4-W2-fix 第 11 条：DWM 沉浸式深色标题栏/Mica 必须在**真窗口出现后**重放一次
        // （构造期 winId 尚未成型；启动期 main.cpp 已按同一主题模式调过一次，这里是运行期
        // 主题切换/显示时序的兜底，保证窗口边框不再按启动时的系统色滞后）。
        // 同时重放一次 tokens：投影效果器的 blur/offset 是**设备像素**（见 refresh_theme），
        // 构造期 devicePixelRatioF() 还是 1，必须等真窗口出现后按实际 dpr 重算。
        refresh_theme();
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

    // 左栏（T11 落地；§9.1 左栏 258px 固定 / 280 可调）：文件卡（勾选+分组）+ 分类卡（§6.2）
    d.left_col = new QWidget(d.splitter);
    d.left_col->setObjectName(QStringLiteral("pp-left-col"));
    // §9.1「左栏 258px 固定（280 可调）」：下界 = 固定值（缩放时不被压缩，尺寸可复现），
    // 上界 = 可调上限（用户拖拽分栏手柄最多拉到 280）
    d.left_col->setMinimumWidth(theme::Metrics::left_width);
    d.left_col->setMaximumWidth(theme::Metrics::left_width_max);
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

    // 卡头：标题 + 计数徽标（.pill）+ 右端 hint（不支持的格式数 / 拖放提示）
    QLabel *panel_title = nullptr;
    auto *card_head = make_card_header(d.panel, tr("文件"), &panel_title, &d.file_count);
    d.file_count->setObjectName(QStringLiteral("pp-file-count"));
    d.unsupported = new ElidedLabel(card_head);
    d.unsupported->setObjectName(QStringLiteral("pp-card-hint"));
    d.unsupported->setStyleSheet(QStringLiteral("color:#dd8800"));
    d.unsupported->setVisible(false);
    d.file_hint = new ElidedLabel(card_head);
    d.file_hint->setObjectName(QStringLiteral("pp-card-hint"));
    d.file_hint->setFont(theme::font(theme::Typography::hint_px));
    d.file_hint->set_full_text(tr("拖放添加")); // mockup .hint；运行中 → 「运行中锁定」
    if (auto *head_layout = qobject_cast<QHBoxLayout *>(card_head->layout())) {
        head_layout->addWidget(d.unsupported); // addStretch 之后 = 右对齐
        head_layout->addWidget(d.file_hint);
    }
    pv->addWidget(card_head);

    // fp-tools（mockup：搜索 + ＋ + −）：添加/移除入口收进两枚 mini-btn；「清空列表」在列表
    // 右键菜单里（M1b 功能零丢失，控件集合与 mockup 一致）
    auto *tools = new QHBoxLayout();
    tools->setContentsMargins(10, 0, 10, 8); // .fp-tools{padding:0 10px 8px}
    tools->setSpacing(6);                    // .fp-tools{gap:6px}
    d.search = new QLineEdit(d.panel);
    d.search->setObjectName(QStringLiteral("pp-search"));
    d.search->setPlaceholderText(tr("搜索文件名…"));
    d.search->setClearButtonEnabled(true);
    d.search->setFixedHeight(theme::Metrics::input_h);
    auto *add_btn = new QToolButton(d.panel);
    add_btn->setObjectName(QStringLiteral("pp-add"));
    add_btn->setText(QStringLiteral("＋"));
    add_btn->setToolTip(tr("添加文件 / 文件夹"));
    add_btn->setPopupMode(QToolButton::InstantPopup);
    add_btn->setFixedHeight(theme::Metrics::input_h);
    add_btn->setFixedWidth(28);
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
    auto *remove_btn = new QToolButton(d.panel);
    remove_btn->setObjectName(QStringLiteral("pp-remove-sel"));
    remove_btn->setText(QStringLiteral("−"));
    remove_btn->setToolTip(tr("移除所选"));
    remove_btn->setFixedHeight(theme::Metrics::input_h);
    remove_btn->setFixedWidth(28);
    remove_btn->setEnabled(false);
    tools->addWidget(d.search, 1);
    tools->addWidget(add_btn);
    tools->addWidget(remove_btn);
    pv->addLayout(tools);
    d.add_btn = add_btn;
    d.remove_sel = remove_btn;

    // groupby（mockup：`分组：<combo>`；§6.2 三态 + 「无分组」）
    auto *groupby = new QHBoxLayout();
    groupby->setContentsMargins(10, 0, 10, 8); // .groupby{padding:0 10px 8px}
    groupby->setSpacing(6);
    auto *group_label = new QLabel(tr("分组："), d.panel);
    group_label->setObjectName(QStringLiteral("pp-groupby-label"));
    group_label->setFont(theme::font(theme::Typography::small_px));
    d.groupby_label = group_label;
    d.group_mode = new QComboBox(d.panel);
    d.group_mode->setObjectName(QStringLiteral("pp-group-mode"));
    d.group_mode->setFixedHeight(26); // .combo{height:26px}
    d.group_mode->addItem(tr("按月份"), int(pp::GroupMode::Month));
    d.group_mode->addItem(tr("按相机型号"), int(pp::GroupMode::Camera));
    d.group_mode->addItem(tr("按源格式"), int(pp::GroupMode::SourceFormat));
    d.group_mode->addItem(tr("无分组"), int(pp::GroupMode::None));
    groupby->addWidget(group_label);
    groupby->addWidget(d.group_mode, 1);
    pv->addLayout(groupby);

    // 文件列表（M4-T11：QListView → QTreeView；两列 = 勾选列 29px + 行内容列，分组节跨两列）
    d.view = new FileTreeView(d.panel);
    d.view->setObjectName(QStringLiteral("pp-file-view"));
    d.view->setModel(d.groups);
    d.view->setItemDelegate(new FileGroupDelegate(d.view));
    d.view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d.view->setSelectionBehavior(QAbstractItemView::SelectRows);
    d.view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d.view->setRootIsDecorated(false);   // mockup：节头无展开箭头（.ghead 平铺）
    d.view->setIndentation(0);           // 节内行与节头同左沿（mockup）
    d.view->setHeaderHidden(true);       // mockup：无表头
    d.view->setUniformRowHeights(false); // 节头 26px / 文件行 56px
    d.view->setExpandsOnDoubleClick(true);
    d.view->setAllColumnsShowFocus(true);
    d.view->header()->setSectionResizeMode(FileGroupProxyModel::kCheckColumn, QHeaderView::Fixed);
    d.view->header()->resizeSection(FileGroupProxyModel::kCheckColumn, 29); // 6+15+8
    d.view->header()->setSectionResizeMode(FileGroupProxyModel::kRowColumn, QHeaderView::Stretch);
    pv->addWidget(d.view, 1);
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
    d.classify_hint = new ElidedLabel(classify_head);
    d.classify_hint->setObjectName(QStringLiteral("pp-card-hint"));
    d.classify_hint->setFont(theme::font(theme::Typography::hint_px));
    d.classify_hint->set_full_text(tr("1–9 键打标")); // mockup .hint；运行中 → 「运行中只读」
    if (auto *head_layout = qobject_cast<QHBoxLayout *>(classify_head->layout()))
        head_layout->addWidget(d.classify_hint);
    classify_layout->addWidget(classify_head);
    d.classify_panel = new ClassifyPanel(d.classify_card);
    classify_layout->addWidget(d.classify_panel);
    left_col_layout->addWidget(d.classify_card, 0);

    // 中栏卡：输入预览（T10：卡头 = 文件名徽标 + 尺寸/位深/色彩空间 hint；卡体 = PreviewPanel）
    d.preview_card = new QFrame(d.splitter);
    d.preview_card->setObjectName(QStringLiteral("pp-card"));
    d.preview_card->setAttribute(Qt::WA_StyledBackground, true);
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
    connect(d.remove_sel, &QToolButton::clicked, this, [this] { impl_->remove_selected(); });
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
        open_exif_editor_row(impl_ref.groups->mapToSource(idx).row());
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
        std::fprintf(stderr, "ui: classify: %s\n", message.toUtf8().constData());
        std::fflush(stderr);
    });

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
    // M4-T11（§6.2/D2）：勾选 = 参与运行集合 —— 未勾选任何文件 = 无参与文件
    if (d.model->checked_count() == 0) {
        QMessageBox::warning(this, tr("无法开始"), tr("未勾选任何文件"));
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
    // M4-T14（机械性接线）：§8.1 交错步距 / §8.2 线程预算 T 的实际生效值来自设置（W1-T7 已持久化；
    // 设置页控件归 T15）。运行总览卡的「交错 / 线程预算 / 当前分配」按这里下发的同一份值显示 ——
    // 读数与引擎同源，不是 UI 自算的另一套数。
    cfg.stagger_ms = d.settings.stagger_ms;
    cfg.thread_budget = d.settings.thread_budget;

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

    // 参与运行集合（M4-T11：勾选口径；顺序 = 列表行序）
    const std::vector<std::size_t> run_rows = d.model->checked_rows();
    std::vector<pp::FileEntry> entries = d.model->checked_entries(); // 含单文件例外（引擎侧生效）
    const std::size_t total = entries.size();

    if (cfg.metadata_only) {
        QStringList bad;
        for (std::size_t k = 0; k < total; ++k) {
            const QString name = path_text(entries[k].src);
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
    names.reserve(static_cast<int>(total));
    for (const pp::FileEntry &entry : entries)
        names << path_text(entry.src);

    // M4-T14：运行页运行计划（§3.4/§7.2/§8.1/§8.2）——逐源文件的尺寸/字节（行数文案与压缩比的
    // 输入；取自列表已 probe 的摘要，零额外 IO，未 probe 则 0 = 未知，页面自动退化文案）+
    // 输出格式清单（= 配置顺序，逐输出子行的格式 chip 与行数）+ 调度读数。
    {
        PageRun::RunPlan plan;
        for (const pp::OutputFormatSpec &spec : cfg.outputs)
            plan.format_ids << QString::fromStdString(spec.format_id);
        plan.widths.reserve(static_cast<int>(total));
        plan.heights.reserve(static_cast<int>(total));
        plan.bytes.reserve(static_cast<int>(total));
        for (std::size_t k = 0; k < total; ++k) {
            const std::size_t row = k < run_rows.size() ? run_rows[k] : k;
            if (row < d.model->size()) {
                const FileRow &fr = d.model->row(row);
                plan.widths << (fr.probe_ok ? fr.info.width : 0);
                plan.heights << (fr.probe_ok ? fr.info.height : 0);
            } else {
                plan.widths << 0;
                plan.heights << 0;
            }
            // 源文件字节：字节层取（std::filesystem），避免非 UTF-8 路径经 QString 往返失真
            std::error_code ec;
            const std::uintmax_t size = std::filesystem::file_size(entries[k].src, ec);
            plan.bytes << (ec ? 0 : static_cast<qint64>(size));
        }
        plan.stagger_ms = cfg.stagger_ms;
        plan.workers = cfg.workers;
        plan.thread_budget = cfg.thread_budget;
        plan.metadata_only = cfg.metadata_only;
        d.page_run->set_plan(plan);
    }

    for (const std::size_t row : run_rows)
        d.model->set_state(row, pp::FileState::Queued); // 只复位本批（参与运行）行的状态
    d.run_rows = run_rows;                              // ev.index（提交序号）→ 列表行号的映射
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
    d.log_tail->start();
    impl_->push_log_tail(); // 立刻给一次（本次 run 日志刚开写）
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
    d.log_tail->stop();
    d.sched->wait();
    // 排空在途 queued 事件后再定稿，避免 end_run 之后又被迟到事件改写计数
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    const pp::RunSummary sum = d.sched->summary();
    d.page_run->end_run(sum, d.page_output->out_root());
    d.sched.reset();
    lock_for_run(false);
    impl_->on_content_changed();
    impl_->push_log_tail(); // 结束态再读一次（末几行含 summary/收尾日志）
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
    // G5「预览只读、分类只读」：预览面板运行时锁**打标热键**（分类只读）；翻图/缩放仍可用
    // （预览本身无修改语义，读图不受运行影响）。
    if (d.preview_panel != nullptr)
        d.preview_panel->set_locked(lock);
    // M4-T11：左栏分类面板同样运行期只读；卡头 hint 按 mockup 切换（"1–9 键打标"/"运行中只读"、
    // "拖放添加"/"运行中锁定"）
    if (d.classify_panel != nullptr)
        d.classify_panel->set_locked(lock);
    if (d.classify_hint != nullptr)
        d.classify_hint->set_full_text(lock ? tr("运行中只读") : tr("1–9 键打标"));
    if (d.file_hint != nullptr)
        d.file_hint->set_full_text(lock ? tr("运行中锁定") : tr("拖放添加"));
    if (d.group_mode != nullptr)
        d.group_mode->setEnabled(!lock); // mockup run-dark：分组下拉置灰
    if (lock)
        set_current_page(3); // stack 锁到运行页
    refresh_status();
}

void MainWindow::refresh_status() {
    Impl &d = *impl_;
    const std::size_t count = d.model->size();
    const std::size_t checked = d.model->checked_count();
    const std::size_t unsupported = d.model->unsupported_count();
    const std::size_t exceptions = d.model->exception_count();
    const QString reason = d.page_output->ready_to_start();

    QString text;
    bool invalid = false;
    if (d.running) {
        // M4-T14（mockup run-dark 底栏）：运行中 = 「成功 N · 失败 N · 进行中 N · 排队 N」。
        // 并行读数与运行页同源（page_run 的同一份事件流折算：已取件 − 已结算 = 进行中）。
        const int started = d.page_run->files_started();
        const int terminal_files = d.page_run->files_terminal();
        const int inflight = std::max(0, started - terminal_files);
        const int queued = std::max(0, static_cast<int>(d.run_total) - started);
        text = tr("成功 %1 · 失败 %2 · 进行中 %3 · 排队 %4")
                   .arg(d.run_done)
                   .arg(d.run_failed)
                   .arg(inflight)
                   .arg(queued);
    } else if (count == 0) {
        text = tr("没有文件");
        invalid = true;
    } else if (!reason.isEmpty()) {
        text = reason;
        invalid = true;
    } else if (checked == 0) {
        // M4-T11（§6.2/D2）：勾选 = 参与运行集合；未勾选 = 无参与文件
        text = tr("未勾选任何文件");
        invalid = true;
    } else {
        // §9.1 底栏 = 选中摘要 · 输出摘要 · [开始运行]（mockup："已选 12 / 24 个文件 · 就绪"）
        text = tr("已选 %1 / %2 个文件 · 就绪").arg(checked).arg(count);
    }
    if (exceptions > 0)
        text += tr(" · %1 个例外").arg(exceptions);
    d.status->set_full_text(text); // §9.3：超长省略号（不折行）
    const QString alert =
        invalid ? QStringLiteral("color:") + theme::css_color(d.tokens.err) : QString();
    d.status->setStyleSheet(alert);
    if (d.status_dot != nullptr)
        d.status_dot->setStyleSheet(alert);

    // 输出摘要（底栏第二段）
    // M4-T13：按 mockup output-dark 底栏的产出量预告 —— 「N 个文件 × M 个格式 = N×M 个输出」；
    // 格式清单 / 模板 / 输出根目录 / 冲突策略收进悬浮提示（§9.3 不折行）。
    // M4-T14（mockup run-dark 底栏第二段）：**运行中该段显示 ETA**（「ETA 约 00:23」，--txt3），
    // 结束/空闲恢复产出量预告 —— 与原型同一条"次要状态行"的两种内容。
    if (d.output_status != nullptr) {
        if (d.running) {
            const int eta = d.page_run->eta_seconds();
            d.output_status->set_full_text(eta >= 0 ? tr("ETA 约 %1").arg(eta_text(eta))
                                                    : tr("ETA 约 —"));
            d.output_status->setToolTip(QString());
        } else {
            QStringList labels;
            for (const pp::OutputFormatSpec &spec : d.page_output->config_base().outputs)
                labels << format_label(QString::fromStdString(spec.format_id));
            if (labels.isEmpty())
                labels << format_label(d.page_output->current_format());
            const int formats = std::max(1, static_cast<int>(labels.size()));
            d.output_status->set_full_text(tr("%1 个文件 × %2 个格式 = %3 个输出")
                                               .arg(checked)
                                               .arg(formats)
                                               .arg(checked * formats));
            d.output_status->setToolTip(
                tr("格式：%1\n模板：%2\n输出根目录：%3\n同名冲突：%4")
                    .arg(labels.join(QStringLiteral(" + ")), d.page_output->output_template(),
                         d.page_output->out_root(),
                         conflict_label(d.page_output->config_base().conflict)));
        }
    }

    // 卡头计数徽标（mockup .pill 内是纯数字）
    d.file_count->set_full_text(QString::number(count));
    d.file_count->setVisible(true);
    d.unsupported->set_full_text(unsupported > 0 ? tr("（%1 个不支持）").arg(unsupported)
                                                 : QString());
    d.unsupported->setVisible(unsupported > 0);

    // G5（2026-09-20 R1 修订）：运行中按钮禁用（**取消唯一入口 = 运行页**，该裁定不变）；
    // M4-T14（mockup run-dark .go）：运行中按钮文案变**状态读数**「运行中… 68%」，
    // 空闲/结束恢复「▶  开始运行」。
    // M4-T11：可开始还需勾选集非空（勾选 = 参与运行集合）
    const bool can_start = !d.running && count > 0 && checked > 0 && reason.isEmpty();
    if (d.running) {
        const int pct = d.page_run->progress_percent();
        d.start->setText(pct >= 0 ? tr("运行中… %1%").arg(pct) : tr("运行中…"));
    } else {
        d.start->setText(tr("▶  开始运行"));
    }
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
    d.save_classes();        // §6.2：分类归属随会话落盘（打标期已即时写；此处兜底）
    d.save_splitter_state(); // §9.1：三栏尺寸持久化（QSettings）
}

// ---------------------------------------------------------------------------
// M4-T11：左栏（勾选 / 自动分组 / 分类面板）
// ---------------------------------------------------------------------------

void MainWindow::Impl::load_classes() {
    if (classes_file.isEmpty())
        return;
    std::string err;
    if (!classes.load(std::filesystem::path(classes_file.toStdString()), &err)) {
        // 损坏的注册表：**不改动用户文件**、保持默认模板并在 stderr 明说（不静默）
        std::fprintf(stderr, "ui: classes.json load failed (%s): %s\n",
                     classes_file.toUtf8().constData(), err.c_str());
        std::fflush(stderr);
    }
}

void MainWindow::Impl::save_classes() {
    if (classes_file.isEmpty())
        return;
    std::string err;
    if (!classes.save(std::filesystem::path(classes_file.toStdString()), &err)) {
        std::fprintf(stderr, "ui: classes.json save failed (%s): %s\n",
                     classes_file.toUtf8().constData(), err.c_str());
        std::fflush(stderr);
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
    sync_class_views(targets.front());
    w->refresh_status();
}

void MainWindow::Impl::sync_class_views(std::size_t row) {
    if (classify_panel == nullptr)
        return;
    const QString class_id =
        row < model->size() ? QString::fromStdString(model->class_id_of(row)) : QString();
    classify_panel->set_current_class_id(class_id); // .cls-row.on 跟随选中（§5.2 同一条跟随口径）
}

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
    applying_group_state = true; // 程序化展开/折叠不写入折叠记忆（见 collapsed/expanded 接线）
    // 节头跨两列（mockup .ghead 通栏；节头本身不参与勾选/选择）
    for (int g = 0; g < groups->group_count(); ++g) {
        const QModelIndex idx = groups->group_index(g);
        if (idx.isValid())
            view->setFirstColumnSpanned(g, QModelIndex(), true);
    }
    // 重建（reset）会丢选中 → 按路径恢复（被搜索过滤隐藏时不越权恢复）。
    // 注意顺序：QTreeView::scrollTo（setCurrentIndex 内部）会**展开**当前项的祖先，故选中恢复
    // 必须排在展开态落地**之前**，否则用户的折叠会被"露出当前项"覆盖。
    restore_selection_by_path(last_selected_path);
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
    if (view->selectionModel() == nullptr || path.isEmpty())
        return;
    for (std::size_t i = 0; i < model->size(); ++i) {
        if (QString::fromStdString(model->row(i).entry.src.string()) != path)
            continue;
        const QModelIndex src = model->index(int(i), 0);
        const QModelIndex filtered = proxy->mapFromSource(src);
        if (!filtered.isValid())
            return; // 被搜索过滤隐藏
        const QModelIndex group_idx = groups->mapFromSource(filtered);
        if (!group_idx.isValid())
            return;
        view->setCurrentIndex(group_idx);
        view->selectionModel()->select(group_idx, QItemSelectionModel::ClearAndSelect |
                                                      QItemSelectionModel::Rows);
        return;
    }
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
    // M4-T11：左栏控件样式（mockup .mini-btn / .search / .combo / .groupby；tokens 单源）
    {
        const QString mini =
            QStringLiteral("QToolButton#pp-add, QToolButton#pp-remove-sel { background:%1; "
                           "border:1px solid %2; border-radius:%3px; color:%4; "
                           "font-weight:600; padding:0 %5px; }"
                           "QToolButton#pp-add:disabled, QToolButton#pp-remove-sel:disabled "
                           "{ color:%6; }")
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
        if (search != nullptr)
            search->setStyleSheet(input);
        if (group_mode != nullptr)
            group_mode->setStyleSheet(input);
        if (groupby_label != nullptr)
            groupby_label->setStyleSheet(
                QStringLiteral("color:%1").arg(theme::css_color(tokens.text3)));
    }
    if (app_icon != nullptr)
        app_icon->setPixmap(app_icon_pixmap(tokens.accent, 18));
    for (StepButton *step : {step_meta, step_output, step_run}) {
        if (step != nullptr)
            step->set_tokens(tokens);
    }
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
        QMessageBox::warning(w, MainWindow::tr("载入预设失败"), QString::fromStdString(err));
        return;
    }
    pp::normalize_preset(preset);
    // T13 复核项 2：**能载入但校验不过**的预设（非法模板 / 未知格式 / 越界参数 / v1 迁移出的
    // tech·lossless 矛盾）同样必须显式报错 —— 否则 apply_preset 内部静默 return，用户点预设
    // 表现为"无反应"（presets.h「预设里带非法模板 = 预设无效，不得静默通过」）。
    const std::string verr = pp::validate_preset(preset);
    if (!verr.empty()) {
        QMessageBox::warning(w, MainWindow::tr("载入预设失败"), QString::fromStdString(verr));
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
        QMessageBox::warning(w, MainWindow::tr("保存预设失败"), QString::fromStdString(verr));
        return;
    }
    const std::filesystem::path path = pp::platform::presets_dir() / (name.toStdString() + ".json");
    const std::string err = pp::ui::save_preset(path, preset);
    if (!err.empty()) {
        QMessageBox::warning(w, MainWindow::tr("保存预设失败"), QString::fromStdString(err));
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
        QMessageBox::warning(w, MainWindow::tr("删除预设失败"), path);
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
    w->refresh_status();
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
    remove_sel->setEnabled(!running && has);
    // M4-T11：分类面板高亮跟随选中（§5.2 同一条跟随口径）；分组重建后据此恢复选中
    if (has && !paths.isEmpty())
        last_selected_path = paths.first();
    sync_class_views(first_row >= 0 ? std::size_t(first_row) : std::size_t(-1));
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

void MainWindow::Impl::push_log_tail() {
    // §9.3「日志尾卡（5 行滚动 + 跳转）」：单一持有者 = MainWindow（页只渲染）。
    // 读法 = 尾部 8KB 窗口（避免整读大日志）→ 按行切 → **丢掉窗口首行的半截行** → 取末 5 行。
    // 日志格式 = core/logger.cpp 的 `%H:%M:%S.%e [%l] [%t] %v`（与 mockup .loglines 同形）。
    const std::filesystem::path dir = pp::platform::logs_dir();
    if (dir.empty()) {
        page_run->set_log_tail(QStringList(), QString());
        return;
    }
    const std::filesystem::path file = newest_run_log(dir);
    if (file.empty()) {
        page_run->set_log_tail(QStringList(), QString());
        return;
    }
    QStringList lines;
    std::ifstream in(file, std::ios::binary);
    if (in) {
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size > 0) {
            constexpr std::streamoff kWindow = 8192;
            const std::streamoff start = size > kWindow ? size - kWindow : 0;
            in.seekg(start, std::ios::beg);
            std::string buf(static_cast<std::size_t>(size - start), '\0');
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            buf.resize(static_cast<std::size_t>(in.gcount()));
            QString text = QString::fromUtf8(buf.data(), static_cast<int>(buf.size()));
            if (start > 0) {
                const int first_break = text.indexOf(QLatin1Char('\n'));
                text = first_break >= 0 ? text.mid(first_break + 1) : QString();
            }
            const QStringList all = text.split(QLatin1Char('\n'));
            for (const QString &raw : all) {
                const QString line = raw.trimmed();
                if (!line.isEmpty())
                    lines << line;
            }
            while (lines.size() > 5)
                lines.removeFirst();
        }
    }
    page_run->set_log_tail(lines, QString::fromStdString(file.filename().string()));
}

void MainWindow::Impl::handle_file_event(const pp::FileEvent &ev) {
    // M4-T11：ev.index = 提交序号（仅勾选文件）→ 映射回列表行号再落状态
    if (ev.index < 0 || std::size_t(ev.index) >= run_rows.size())
        return;
    const std::size_t row = run_rows[std::size_t(ev.index)];
    if (row >= model->size())
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
    model->set_state(row, ev.state);
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
    // ---- 左栏四个动作钮的 pp-* 测试钩子（M4-W2-fix 第 3 条：§9.3「objectName 沿 pp-*
    // 命名法」）---- 「＋ 添加文件…/添加文件夹…/清空列表」收在 ＋ 的弹出菜单里（T11 的 .fp-tools
    // 形态）， 故钩子挂在 QAction 上；「− 移除所选」是按钮。
    {
        const QAction *hooks[] = {add_files_action, add_dir_action, clear_action};
        const char *names[] = {"pp-add-files", "pp-add-dir", "pp-clear-all"};
        QStringList found;
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            const bool hit =
                hooks[i] != nullptr && hooks[i]->objectName() == QString::fromLatin1(names[i]);
            ok = ok && hit;
            found << QStringLiteral("%1=%2").arg(QString::fromLatin1(names[i]),
                                                 hooks[i] != nullptr ? hooks[i]->objectName()
                                                                     : QStringLiteral("(null)"));
        }
        const bool remove_ok =
            remove_sel != nullptr && remove_sel->objectName() == QStringLiteral("pp-remove-sel");
        ok = ok && remove_ok;
        found << QStringLiteral("pp-remove-sel=%1")
                     .arg(remove_sel != nullptr ? remove_sel->objectName()
                                                : QStringLiteral("(null)"));
        found << QStringLiteral("file-count=%1")
                     .arg(file_count != nullptr ? file_count->objectName()
                                                : QStringLiteral("(null)"));
        std::printf("UI-SMOKE hooks: %s\n", qUtf8Printable(found.join(QLatin1Char(' '))));
        std::fflush(stdout);
        if (!ok || file_count == nullptr ||
            file_count->objectName() != QStringLiteral("pp-file-count"))
            smoke_fail(MainWindow::tr("左栏 pp-* 测试钩子缺失或命名不符：%1")
                           .arg(found.join(QLatin1Char(' '))));
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

    // ---- (g) caption 双轨（M4-W2-fix 第 5 条，主对话裁定）----
    //   * 可见/hover 面 = 控件 46×满高（52px）→ 关闭钮 hover 的 #c42b1c 铺满顶栏（原型整高）；
    //   * 命中面 = 居中的 46×32 带（§9.1 明文）→ 带外按下不触发按钮，且窗口命中判定回落到
    //     Caption（拖拽），不是"点了没反应"的死区。
    {
        const QRect widget = cap_close->rect();
        const QRect band = CaptionButton::hit_band(widget);
        // 带外取样点：命中带之上 2px（仍避开顶边 6px 缩放带 —— 那条带按硬约束优先返回缩放）
        const int outside_y = std::max(plat::kResizeBorderPx + 1, band.top() - 2);
        std::printf("UI-SMOKE caption-band: widget=%dx%d hit=%dx%d band-y=%d..%d "
                    "out-of-band-zone=%s\n",
                    widget.width(), widget.height(), band.width(), band.height(), band.top(),
                    band.bottom(),
                    hit_zone_name(frameless->classify_logical(
                        cap_close->mapToGlobal(QPoint(cap_close->width() / 2, outside_y)))));
        std::fflush(stdout);
        if (widget.height() != theme::Metrics::caption_btn_h ||
            band.height() != theme::Metrics::caption_btn_hit_h ||
            (widget.height() - band.height()) / 2 != band.top())
            smoke_fail(MainWindow::tr("caption 双轨几何不符：控件=%1 命中带=%2 (期望 %3×%4)")
                           .arg(widget.height())
                           .arg(band.height())
                           .arg(theme::Metrics::caption_btn_w)
                           .arg(theme::Metrics::caption_btn_hit_h));
        // 带外（贴满顶栏的 hover 面里、32px 带之上的 2px 处）真发一次按下+释放：不得触发按钮
        frameless->set_intercept_actions(true);
        QStringList band_routed;
        const QMetaObject::Connection band_conn =
            QObject::connect(frameless, &plat::Frameless::caption_action, w,
                             [&band_routed](plat::CaptionAction action) {
                                 band_routed << QString::fromLatin1(caption_action_name(action));
                             });
        const auto send_click = [](QWidget *target, const QPoint &pos) {
            const QPointF global(target->mapToGlobal(pos));
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), global, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), global, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(target, &release);
        };
        send_click(cap_close, QPoint(cap_close->width() / 2, outside_y)); // 带外
        const int routed_after_outside = band_routed.size();
        send_click(cap_close, QPoint(cap_close->width() / 2, widget.height() / 2)); // 带内
        QObject::disconnect(band_conn);
        frameless->set_intercept_actions(false);
        std::printf("UI-SMOKE caption-dual-track: out-of-band=%d in-band=%s\n",
                    routed_after_outside,
                    band_routed.size() > 0 ? qUtf8Printable(band_routed.last()) : "(none)");
        std::fflush(stdout);
        if (routed_after_outside != 0 || band_routed.size() != 1 ||
            band_routed.last() != QStringLiteral("close"))
            smoke_fail(MainWindow::tr("caption 命中带失效：带外=%1 带内=%2")
                           .arg(routed_after_outside)
                           .arg(band_routed.join(',')));
        if (frameless->classify_logical(cap_close->mapToGlobal(
                QPoint(cap_close->width() / 2, outside_y))) != plat::HitZone::Caption)
            smoke_fail(MainWindow::tr("caption 命中带之外不是拖拽区（应回落 HTCAPTION/Caption）"));
    }

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

    // ---- theme::set_mod 便捷函数（M4-W2-fix 第 9 条；T12 的 .mod 消费入口）----
    // 承诺的语义 = setProperty(kModProperty) + unpolish/polish 触发 QSS 重算；QSS 侧规则
    // （*[ppMod="true"] 的强调边框/字 + 700 字重）由 style_sheet 落地，这里两者都取证。
    {
        QLabel probe;
        theme::set_mod(&probe, true);
        const bool on = probe.property(theme::kModProperty).toBool();
        theme::set_mod(&probe, false);
        const bool off = !probe.property(theme::kModProperty).toBool();
        const bool rule = w->styleSheet().contains(QStringLiteral("ppMod"));
        std::printf("UI-SMOKE set-mod: on=%d off=%d qss-rule=%d\n", on ? 1 : 0, off ? 1 : 0,
                    rule ? 1 : 0);
        std::fflush(stdout);
        if (!on || !off || !rule)
            smoke_fail(MainWindow::tr("theme::set_mod 未落地（on=%1 off=%2 qss-rule=%3）")
                           .arg(on ? 1 : 0)
                           .arg(off ? 1 : 0)
                           .arg(rule ? 1 : 0));
    }
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

// ---------------------------------------------------------------------------
// M4-T10 预览面板自检（§6.1 控件面 + 热键接线位）
//   自动化部分：徽标文案恒显、缩放 chip 两枚、热键提示表（随注册表生成）、位置读数、
//   翻图（next/previous → 索引/读数/出图）、缩放（适应/1:1 的绘制尺寸差异）、
//   热键路由（未锁定发出 class_hotkey、锁定后不发出）。
//   手测部分（W5 走查清单）：真实 48MP 点选的 250ms 主观手感、1:1 拖拽平移、快速翻图不堆积。
// ---------------------------------------------------------------------------

void MainWindow::Impl::smoke_probe_preview() {
    if (preview_panel == nullptr) {
        smoke_fail(MainWindow::tr("预览面板缺失（PreviewPanel 未建）"));
        return;
    }
    if (model->empty()) {
        smoke_fail(MainWindow::tr("预览自检：文件列表为空"));
        return;
    }
    // 1:1 断言要读 model 的 probe 摘要（源尺寸）→ 先等缩略图通道把 probe 结果落进模型
    wait_thumbs(kThumbWaitMs);

    // ---- (a) 控件面：徽标恒显 / 缩放两枚 chip / 热键提示表（随 ClassRegistry 生成）----
    QLabel *badge = preview_panel->findChild<QLabel *>(QStringLiteral("pp-preview-badge"));
    QToolButton *zoom_fit =
        preview_panel->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-fit"));
    QToolButton *zoom_11 =
        preview_panel->findChild<QToolButton *>(QStringLiteral("pp-preview-zoom-1to1"));
    QStringList key_chips;
    for (QLabel *chip : preview_panel->findChildren<QLabel *>(QStringLiteral("pp-preview-key")))
        key_chips << chip->text();
    QStringList key_names;
    for (QLabel *label :
         preview_panel->findChildren<QLabel *>(QStringLiteral("pp-preview-keyname")))
        key_names << label->text();
    std::printf("UI-SMOKE preview-controls: badge=\"%s\" zoom-fit=\"%s\" zoom-1:1=\"%s\" "
                "keys=[%s] names=[%s] pos=\"%s\"\n",
                badge != nullptr ? qUtf8Printable(badge->text()) : "<missing>",
                zoom_fit != nullptr ? qUtf8Printable(zoom_fit->text()) : "<missing>",
                zoom_11 != nullptr ? qUtf8Printable(zoom_11->text()) : "<missing>",
                qUtf8Printable(key_chips.join(QLatin1Char(' '))),
                qUtf8Printable(key_names.join(QLatin1Char(' '))),
                qUtf8Printable(preview_panel->current_position_text()));
    std::fflush(stdout);
    if (badge == nullptr || badge->text() != QStringLiteral("输入 · 未修改像素")) {
        smoke_fail(MainWindow::tr("预览徽标不是「输入 · 未修改像素」（%1）")
                       .arg(badge != nullptr ? badge->text() : QStringLiteral("<missing>")));
    }
    if (zoom_fit == nullptr || zoom_11 == nullptr || zoom_11->text() != QStringLiteral("1:1")) {
        smoke_fail(MainWindow::tr("缩放「适应 | 1:1」chip 缺失或不符"));
    }
    // 默认模板（§6.2）：精选/待定/废片（热键 1/2/3）+ 保留的「0 清除」
    const QStringList expect_chips{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                                   QStringLiteral("0")};
    if (key_chips != expect_chips)
        smoke_fail(MainWindow::tr("热键提示 chip 表不符：%1").arg(key_chips.join(',')));
    if (key_names.size() != 4 || key_names.at(0) != QStringLiteral("精选") ||
        key_names.at(1) != QStringLiteral("待定") || key_names.at(2) != QStringLiteral("废片") ||
        key_names.at(3) != QStringLiteral("清除")) {
        smoke_fail(MainWindow::tr("热键提示名称表不符：%1").arg(key_names.join(',')));
    }
    if (preview_panel->current_position_text() != QStringLiteral("1 / %1").arg(model->size()))
        smoke_fail(
            MainWindow::tr("预览位置读数不符：%1").arg(preview_panel->current_position_text()));

    const int original_index = preview_panel->current_index();

    // ---- (b) 翻图：next/previous → 索引与读数跟随；出图在 5s 内落地 ----
    preview_panel->next();
    const int after_next = preview_panel->current_index();
    preview_panel->previous();
    const int after_prev = preview_panel->current_index();
    if (after_next != original_index + 1 || after_prev != original_index)
        smoke_fail(MainWindow::tr("翻图索引不符：%1→%2→%3")
                       .arg(original_index)
                       .arg(after_next)
                       .arg(after_prev));
    // 定位到第二张等出图（同时验证池的异步投递链路）
    preview_panel->next();
    const bool loaded = wait_for([this] { return preview_panel->image_visible(); }, 5000);
    const QSize shown = preview_panel->displayed_size();
    std::printf("UI-SMOKE preview-flip: index=%d pos=\"%s\" name=\"%s\" visible=%d "
                "displayed=%dx%d\n",
                preview_panel->current_index(),
                qUtf8Printable(preview_panel->current_position_text()),
                qUtf8Printable(preview_panel->current_file_name()), loaded ? 1 : 0, shown.width(),
                shown.height());
    std::fflush(stdout);
    if (!loaded)
        smoke_fail(
            MainWindow::tr("翻图后 5s 内未出图（index=%1）").arg(preview_panel->current_index()));
    if (preview_panel->current_info_text().isEmpty())
        smoke_fail(MainWindow::tr("预览信息行为空（尺寸/位深/色彩空间未落地）"));

    // ---- (c) 缩放：1:1 的绘制尺寸 = 源像素尺寸；适应 = 舞台内等比（两者对 64px 小图必须不同）----
    const pp::ImageInfo current_info =
        model->row(static_cast<std::size_t>(preview_panel->current_index())).info;
    preview_panel->set_zoom_fit(false);
    pump(60);
    const QSize at_1to1 = preview_panel->displayed_size();
    preview_panel->set_zoom_fit(true);
    pump(60);
    const QSize at_fit = preview_panel->displayed_size();
    std::printf("UI-SMOKE preview-zoom: 1:1=%dx%d fit=%dx%d (source %dx%d)\n", at_1to1.width(),
                at_1to1.height(), at_fit.width(), at_fit.height(), current_info.width,
                current_info.height);
    std::fflush(stdout);
    if (at_1to1.width() != current_info.width || at_1to1.height() != current_info.height)
        smoke_fail(MainWindow::tr("1:1 不是源像素尺寸：%1x%2 ≠ %3x%4")
                       .arg(at_1to1.width())
                       .arg(at_1to1.height())
                       .arg(current_info.width)
                       .arg(current_info.height));
    if (at_fit == at_1to1)
        smoke_fail(MainWindow::tr("适应与 1:1 绘制尺寸相同（%1x%2）——缩放未生效")
                       .arg(at_fit.width())
                       .arg(at_fit.height()));

    // ---- (d) 热键路由 + 打标接线位：未锁定发出、锁定（G5 分类只读）不发出 ----
    int hits = 0;
    char last_key = 0;
    const QMetaObject::Connection conn =
        connect(preview_panel, &PreviewPanel::class_hotkey, w, [&hits, &last_key](QChar key) {
            ++hits;
            last_key = key.toLatin1();
        });
    const auto send_key = [this](int key, const char *text) {
        QWidget *target =
            view != nullptr ? static_cast<QWidget *>(view) : static_cast<QWidget *>(w);
        target->setFocus();
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString::fromLatin1(text));
        QApplication::sendEvent(target, &press);
    };
    send_key(Qt::Key_1, "1");
    pump(40);
    const int unlocked_hits = hits;
    preview_panel->set_locked(true);
    send_key(Qt::Key_1, "1");
    pump(40);
    const int locked_hits = hits;
    preview_panel->set_locked(false);
    disconnect(conn);
    std::printf("UI-SMOKE preview-hotkey: unlocked=%d locked=%d last=\"%c\"\n", unlocked_hits,
                locked_hits, last_key != 0 ? last_key : '?');
    std::fflush(stdout);
    if (unlocked_hits != 1 || locked_hits != 1 || last_key != '1')
        smoke_fail(MainWindow::tr("热键路由不符：未锁定=%1 锁定=%2 末键=%3（期望 1/1/'1'）")
                       .arg(unlocked_hits)
                       .arg(locked_hits)
                       .arg(QChar(last_key != 0 ? last_key : '?')));

    // ---- (e) 快速翻图不堆积（§6.1）：连翻 8 张 → 池内队列深度恒 ≤1，且最终收敛到末张 ----
    int max_pending = 0;
    for (int i = 0; i < 8 && preview_panel->current_index() + 1 < static_cast<int>(model->size());
         ++i) {
        preview_panel->next();
        pump(2); // 只推进 2ms（远短于一次解码）
        max_pending = std::max(max_pending, preview_panel->pending_requests());
    }
    const int burst_index = preview_panel->current_index();
    const bool burst_loaded = wait_for([this] { return preview_panel->image_visible(); }, 5000);
    std::printf("UI-SMOKE preview-burst: index=%d queued_max=%d pending_now=%d visible=%d\n",
                burst_index, max_pending, preview_panel->pending_requests(), burst_loaded ? 1 : 0);
    std::fflush(stdout);
    if (max_pending > 1)
        smoke_fail(MainWindow::tr("快速翻图堆积：池队列深度 %1 > 1").arg(max_pending));
    if (!burst_loaded || preview_panel->pending_requests() != 0)
        smoke_fail(MainWindow::tr("快速翻图后未收敛：index=%1 pending=%2")
                       .arg(burst_index)
                       .arg(preview_panel->pending_requests()));

    // ---- 归位：截图基准 = 面板显示首个文件（§5.2 跟随口径），且恢复未锁定态 ----
    preview_panel->set_current(original_index);
    pump(120);
}

// ---------------------------------------------------------------------------
// M4-T11 文件列表自检（§3.6 roles + §6.2 勾选/正交搜索/自动分组节头）
//   自动化部分：roles 可读性与可勾选标志、真实鼠标点击勾选（自绘命中框）、内容列不误触勾选、
//   搜索过滤与勾选正交（过滤不丢勾选/不丢计数）、分组节 = 组名+计数（三态 + 无分组，节内计数
//   合计 = 可见行数，节内行序稳定）、节头跨列与展开态。
//   手测部分（W5 走查清单）：拖拽分栏后左栏下限、节头折叠手感、万级文件的滚动流畅度。
// ---------------------------------------------------------------------------

void MainWindow::Impl::smoke_probe_filelist() {
    if (model->empty()) {
        smoke_fail(MainWindow::tr("文件列表自检：列表为空"));
        return;
    }
    wait_thumbs(kThumbWaitMs);
    if (!wait_for([this] { return model->group_scan_idle(); }, 15000))
        smoke_fail(MainWindow::tr("分组摘要扫描未在 15s 内收敛"));

    // ---- (a) §3.6 追加 roles：CheckState（Qt 标准）/ ClassId / GroupKey ----
    const QModelIndex src0 = model->index(0, 0);
    const bool checkable = (model->flags(src0) & Qt::ItemIsUserCheckable) != 0;
    const QVariant check_state = src0.data(Qt::CheckStateRole);
    const QVariant class_id = src0.data(FileListModel::ClassIdRole);
    const QVariant group_key = src0.data(FileListModel::GroupKeyRole);
    std::printf("UI-SMOKE filelist-roles: checkable=%d check_state=%d class_id=\"%s\" "
                "group_key=\"%s\" scanned=%d\n",
                checkable ? 1 : 0, check_state.toInt(), qUtf8Printable(class_id.toString()),
                qUtf8Printable(group_key.toString()), model->group_source_scanned(0) ? 1 : 0);
    std::fflush(stdout);
    if (!checkable)
        smoke_fail(MainWindow::tr("文件行未置 Qt::ItemIsUserCheckable（勾选无入口）"));
    if (!check_state.isValid() || check_state.toInt() != int(Qt::Checked))
        smoke_fail(MainWindow::tr("默认勾选不是 Qt::Checked（实为 %1）").arg(check_state.toInt()));
    if (!class_id.isValid() || !group_key.isValid())
        smoke_fail(MainWindow::tr("ClassIdRole / GroupKeyRole 不可读"));
    for (std::size_t i = 0; i < model->size(); ++i) {
        if (!model->group_source_scanned(i)) {
            smoke_fail(MainWindow::tr("第 %1 行的分组摘要未回填").arg(static_cast<int>(i)));
            break;
        }
    }

    // ---- (b) 勾选：真实鼠标点击（列 0 自绘命中框）→ 取消/恢复；内容列不误触 ----
    const int checked_all = int(model->checked_count());
    if (checked_all != int(model->size()))
        smoke_fail(MainWindow::tr("默认并非全勾：%1 / %2").arg(checked_all).arg(model->size()));
    const auto click_at = [this](const QPoint &pos) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos),
                          view->viewport()->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos),
                            view->viewport()->mapToGlobal(pos), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &release);
        pump(30);
    };
    const QModelIndex group0 = groups->group_index(0);
    if (!group0.isValid()) {
        smoke_fail(MainWindow::tr("分组节缺失（groups->group_index(0) 无效）"));
        return;
    }
    const QModelIndex first_file = groups->index(0, FileGroupProxyModel::kCheckColumn, group0);
    if (!first_file.isValid()) {
        smoke_fail(MainWindow::tr("首文件行缺失"));
        return;
    }
    view->scrollTo(first_file);
    pump(40);
    const QRect check_hit = FileGroupDelegate::check_rect(view->visualRect(first_file));
    const bool hit_ok = view->indexAt(check_hit.center()) == first_file;
    click_at(check_hit.center());
    const bool after_click = model->is_checked(0);
    const int checked_after_click = int(model->checked_count());
    click_at(check_hit.center()); // 恢复
    // 内容列（文件名/缩略图区域）点击不得改勾选：命中框 = 列 1 左沿 + 40px
    const QModelIndex first_content = groups->index(0, FileGroupProxyModel::kRowColumn, group0);
    const QRect content_rect = view->visualRect(first_content);
    const QPoint content_pos(content_rect.left() + 40, content_rect.center().y());
    const bool content_hits = view->indexAt(content_pos) == first_content;
    const bool checked_before_content = model->is_checked(0);
    click_at(content_pos);
    const bool checked_after_content = model->is_checked(0);
    std::printf("UI-SMOKE filelist-check: hit=%d after_click=%d checked=%d/%d restore=%d "
                "content_hit=%d content_toggle=%d\n",
                hit_ok ? 1 : 0, after_click ? 1 : 0, checked_after_click, int(model->size()),
                model->is_checked(0) ? 1 : 0, content_hits ? 1 : 0,
                checked_before_content != checked_after_content ? 1 : 0);
    std::fflush(stdout);
    if (!hit_ok)
        smoke_fail(MainWindow::tr("勾选框命中失败（visualRect 中心不是该行）"));
    if (after_click)
        smoke_fail(MainWindow::tr("点击勾选框未取消勾选"));
    if (checked_after_click != checked_all - 1)
        smoke_fail(MainWindow::tr("勾选计数不符：%1（期望 %2）")
                       .arg(checked_after_click)
                       .arg(checked_all - 1));
    if (!model->is_checked(0))
        smoke_fail(MainWindow::tr("二次点击未恢复勾选"));
    if (content_hits && checked_before_content != checked_after_content)
        smoke_fail(MainWindow::tr("内容列点击误触了勾选"));

    // ---- (c) 搜索正交（§6.2/D2）：过滤只改可见性，勾选/计数/键都不丢 ----
    const std::size_t probe_row = model->size() > 1 ? model->size() / 2 : 0;
    const QString probe_name =
        QString::fromStdString(model->row(probe_row).entry.src.filename().string());
    const QString filter_text =
        probe_name.left(std::max(1, int(probe_name.size()) - 4)); // 名称前缀（至少 1 字符）
    const int checked_before_filter = int(model->checked_count());
    model->set_checked(probe_row, false); // 该行取消勾选
    const int checked_with_unchecked = int(model->checked_count());
    search->setText(filter_text);
    pump(60);
    const int filtered_rows = proxy->rowCount();
    const int checked_while_filtered = int(model->checked_count());
    const bool key_preserved = !model->is_checked(probe_row) &&
                               model->group_key_of(probe_row) ==
                                   model->group_key_of(probe_row); // 键仍可读（不随过滤变化）
    search->clear();
    pump(60);
    const int rows_restored = proxy->rowCount();
    const bool check_survived = !model->is_checked(probe_row);
    model->set_checked(probe_row, true);
    std::printf("UI-SMOKE filelist-search: filter=\"%s\" visible=%d/%d checked %d→%d→%d "
                "restored_rows=%d key_kept=%d check_survived=%d\n",
                qUtf8Printable(filter_text), filtered_rows, int(model->size()),
                checked_before_filter, checked_with_unchecked, checked_while_filtered,
                rows_restored, key_preserved ? 1 : 0, check_survived ? 1 : 0);
    std::fflush(stdout);
    if (filtered_rows >= int(model->size()))
        smoke_fail(MainWindow::tr("搜索未过滤任何行（filter=\"%1\" → %2 / %3）")
                       .arg(filter_text)
                       .arg(filtered_rows)
                       .arg(model->size()));
    if (checked_while_filtered != checked_with_unchecked)
        smoke_fail(MainWindow::tr("过滤改变了勾选计数：%1 ≠ %2")
                       .arg(checked_while_filtered)
                       .arg(checked_with_unchecked));
    if (!check_survived)
        smoke_fail(MainWindow::tr("过滤丢失了勾选状态（§6.2 正交性）"));
    if (rows_restored != int(model->size()))
        smoke_fail(
            MainWindow::tr("清空搜索后行数未复原：%1 ≠ %2").arg(rows_restored).arg(model->size()));

    // ---- (d) 分组节 = 组名 + 计数（§6.2）；三态 + 「无分组」----
    const auto dump_headers = [this](const char *mode_name) {
        QStringList headers;
        for (int g = 0; g < groups->group_count(); ++g)
            headers << groups->header_text_at(g);
        std::printf("UI-SMOKE filelist-group-%s: groups=%d [%s]\n", mode_name,
                    groups->group_count(), qUtf8Printable(headers.join(QStringLiteral(" | "))));
        std::fflush(stdout);
    };
    const auto check_structure = [this](const char *mode_name) -> bool {
        // 节数/节内行数 = 由模型的 GroupKeyRole 直接推导（代理分组 == 模型键）
        const int visible = proxy->rowCount();
        int sum = 0;
        for (int g = 0; g < groups->group_count(); ++g) {
            const int size = groups->group_size_at(g);
            sum += size;
            const QString label = groups->group_label_at(g);
            if (label.isEmpty() ||
                groups->header_text_at(g) != FileGroupProxyModel::header_text(label, size)) {
                smoke_fail(MainWindow::tr("节头文案不符（%1 的第 %2 节）")
                               .arg(QString::fromLatin1(mode_name))
                               .arg(g));
                return false;
            }
            if (size <= 0) {
                smoke_fail(MainWindow::tr("空节未被折叠（%1 第 %2 节）")
                               .arg(QString::fromLatin1(mode_name))
                               .arg(g));
                return false;
            }
        }
        if (sum != visible) {
            smoke_fail(MainWindow::tr("节内计数合计 %1 ≠ 可见行数 %2（%3）")
                           .arg(sum)
                           .arg(visible)
                           .arg(QString::fromLatin1(mode_name)));
            return false;
        }
        return true;
    };
    // 视图侧状态（跨列节头/展开态）在模型 reset 后**延迟一轮**落地（见 apply_group_view_state）
    // → 每次切模式后 pump 一次再断言
    set_group_mode(pp::GroupMode::SourceFormat);
    pump(40);
    dump_headers("format");
    check_structure("format");
    set_group_mode(pp::GroupMode::Camera);
    pump(40);
    dump_headers("camera");
    check_structure("camera");
    set_group_mode(pp::GroupMode::None);
    pump(40);
    dump_headers("none");
    check_structure("none");
    if (groups->group_count() != 1 ||
        groups->group_label_at(0) !=
            QString::fromUtf8(pp::kNoGroupLabel.data(), int(pp::kNoGroupLabel.size()))) {
        smoke_fail(
            MainWindow::tr("「无分组」模式不是单节「%1」")
                .arg(QString::fromUtf8(pp::kNoGroupLabel.data(), int(pp::kNoGroupLabel.size()))));
    }
    // 月份模式：标签形态 "YYYY 年 M 月"（mockup .ghead）；键形态由 core/classify 保证
    set_group_mode(pp::GroupMode::Month);
    pump(60);
    dump_headers("month");
    check_structure("month");
    for (int g = 0; g < groups->group_count(); ++g) {
        const QString key = groups->group_key_at(g);
        const QString label = groups->group_label_at(g);
        if (key.isEmpty())
            continue; // 「无分组」
        if (!(key.size() == 7 && key.at(4) == QLatin1Char('-')) ||
            !label.endsWith(QStringLiteral("月"))) {
            smoke_fail(
                MainWindow::tr("月份节键/节名形态不符：key=\"%1\" label=\"%2\"").arg(key, label));
            break;
        }
    }
    // 节头跨列（mockup .ghead 通栏）+ 展开态
    const QModelIndex g0 = groups->group_index(0);
    const QRect header_rect = view->visualRect(g0);
    std::printf("UI-SMOKE filelist-span: header=%dx%d viewport=%dx%d expanded=%d\n",
                header_rect.width(), header_rect.height(), view->viewport()->width(),
                view->viewport()->height(), view->isExpanded(g0) ? 1 : 0);
    {
        const QWidget *panel_w = view->parentWidget() != nullptr ? view->parentWidget() : view;
        const QWidget *card_w =
            panel_w->parentWidget() != nullptr ? panel_w->parentWidget() : panel_w;
        std::printf("UI-SMOKE filelist-geometry: view=%dx%d panel=%dx%d card=%dx%d col=%dx%d "
                    "colmin=%dx%d tree_min=%dx%d\n",
                    view->width(), view->height(), panel_w->width(), panel_w->height(),
                    card_w->width(), card_w->height(), left_col->width(), left_col->height(),
                    left_col->minimumWidth(), left_col->minimumHeight(), view->minimumWidth(),
                    view->minimumHeight());
    }
    std::fflush(stdout);
    if (!header_rect.isValid() || header_rect.width() < view->viewport()->width() * 9 / 10)
        smoke_fail(MainWindow::tr("节头未跨两列：%1 < 视口 %2")
                       .arg(header_rect.width())
                       .arg(view->viewport()->width()));
    if (!view->isExpanded(g0))
        smoke_fail(MainWindow::tr("分组节默认未展开"));
    if (view->isExpanded(g0)) { // 折叠记忆：用户折叠 → 重建 → 仍折叠（走真实信号路径）
        view->collapse(g0);
        pump(40);
        const bool collapsed_recorded = groups->is_collapsed(groups->group_key_at(0));
        groups->rebuild();
        pump(60);
        const bool still_collapsed = !view->isExpanded(groups->group_index(0)) &&
                                     groups->is_collapsed(groups->group_key_at(0));
        std::printf("UI-SMOKE filelist-collapse: key=\"%s\" recorded=%d expanded_after=%d "
                    "collapsed_flag=%d rebuilds=%d\n",
                    qUtf8Printable(groups->group_key_at(0)), collapsed_recorded ? 1 : 0,
                    view->isExpanded(groups->group_index(0)) ? 1 : 0,
                    groups->is_collapsed(groups->group_key_at(0)) ? 1 : 0, groups->rebuilds());
        std::fflush(stdout);
        if (!collapsed_recorded)
            smoke_fail(MainWindow::tr("用户折叠未被记为折叠态（§6.2 折叠记忆）"));
        if (!still_collapsed)
            smoke_fail(MainWindow::tr("折叠态未跨重建保持（§6.2 过滤/摘要变化不丢展开态）"));
        groups->set_collapsed(groups->group_key_at(0), false);
        groups->rebuild();
        pump(40);
    }

    // ---- 归位：截图基准 = 全部勾选 + 按月份分组 + 首个文件 + 列表回顶（mockup 左栏口径）----
    model->set_all_checked(true);
    set_group_mode(pp::GroupMode::Month);
    if (search->text() != QString())
        search->clear();
    if (view->selectionModel() != nullptr)
        view->selectionModel()->clearSelection();
    view->setCurrentIndex(QModelIndex()); // 当前项也会触发 scrollTo（会把节头滚出视口）→ 一并清掉
    view->scrollToTop();
    preview_panel->set_current(0);
    pump(150);
}

// ---------------------------------------------------------------------------
// M4-T11 分类面板自检（§6.2 CRUD / 热键打标 / 圈选 / classes.json 往返）
//   自动化部分：面板行结构与「全部」虚拟分类、CRUD（增/改/色/热键唯一/删）、
//   热键打标（预览接线位 → registry）、右键菜单项集合与圈选三项语义、
//   classes.json 保存-重载往返、运行期只读。
//   手测部分（W5 走查清单）：颜色对话框取色、重命名输入体验、原生右键菜单外观。
// ---------------------------------------------------------------------------

void MainWindow::Impl::smoke_probe_classify() {
    if (classify_panel == nullptr || model->empty()) {
        smoke_fail(MainWindow::tr("分类自检：面板或列表缺失"));
        return;
    }
    // 冒烟不碰用户真实注册表：把 classes.json 指到仓库临时目录（结束前还原）
    const QString saved_file = classes_file;
    const pp::ClassRegistry backup = classes; // 快照（结束时还原）
    const QString tmp_dir = repo_root() + QStringLiteral("/.cache/tmp/ui-smoke-classes");
    QDir(tmp_dir).removeRecursively();
    if (!QDir().mkpath(tmp_dir))
        smoke_fail(MainWindow::tr("无法创建分类冒烟目录：%1").arg(tmp_dir));
    classes_file = tmp_dir + QStringLiteral("/classes.json");

    // ---- (a) 面板结构：1 + classes.size() 行；首行 = 「全部」（计数 = 文件总数）----
    const QModelIndex g0 = groups->group_index(0);
    const QModelIndex first_file = groups->index(0, FileGroupProxyModel::kRowColumn, g0);
    if (first_file.isValid() && view->selectionModel() != nullptr) {
        view->setCurrentIndex(first_file); // 选中首文件 → 面板 .on 跟随
        view->selectionModel()->select(first_file, QItemSelectionModel::ClearAndSelect |
                                                       QItemSelectionModel::Rows);
    }
    pump(60);
    const int expect_rows = 1 + int(classes.classes.size());
    std::printf("UI-SMOKE classify-panel: rows=%d expect=%d all_label=\"%s\" all_count=%d "
                "total=%d active=%d hotkeys=[%s]\n",
                classify_panel->row_count(), expect_rows,
                qUtf8Printable(classify_panel->row_label(0)), classify_panel->row_count_value(0),
                int(model->size()), classify_panel->row_active(0) ? 1 : 0,
                qUtf8Printable(classify_panel->hotkey_labels().join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (classify_panel->row_count() != expect_rows)
        smoke_fail(MainWindow::tr("面板行数 %1 ≠ 1 + 类数 %2")
                       .arg(classify_panel->row_count())
                       .arg(expect_rows));
    if (classify_panel->row_label(0) != tr("全部") ||
        classify_panel->row_count_value(0) != int(model->size())) {
        smoke_fail(MainWindow::tr("「全部」行不符：label=\"%1\" count=%2（期望 %3）")
                       .arg(classify_panel->row_label(0))
                       .arg(classify_panel->row_count_value(0))
                       .arg(model->size()));
    }
    // 默认模板（§6.2）：精选/待定/废片 + 热键 1/2/3
    const QStringList hotkeys = classify_panel->hotkey_labels();
    if (hotkeys !=
        QStringList{QStringLiteral("1=精选"), QStringLiteral("2=待定"), QStringLiteral("3=废片")}) {
        smoke_fail(MainWindow::tr("默认类表/热键不符：[%1]").arg(hotkeys.join(QLatin1Char(' '))));
    }

    // ---- (b) 打标：预览热键接线位（class_hotkey → 注册表）----
    const int target_row = preview_panel->current_index() >= 0 ? preview_panel->current_index() : 0;
    const QString target_path =
        QString::fromStdString(model->row(std::size_t(target_row)).entry.src.string());
    const auto send_hotkey = [this](int key, const char *text) {
        QWidget *target =
            view != nullptr ? static_cast<QWidget *>(view) : static_cast<QWidget *>(w);
        target->setFocus();
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString::fromLatin1(text));
        QApplication::sendEvent(target, &press);
        pump(40);
    };
    send_hotkey(Qt::Key_2, "2"); // → 待定（maybe）
    const std::optional<std::string> tagged =
        classes.class_of(std::filesystem::path(target_path.toStdString()));
    pump(60);
    const QString model_class = QString::fromStdString(model->class_id_of(std::size_t(target_row)));
    const QVariant dot = model->index(target_row, 0).data(Qt::DecorationRole);
    const int maybe_count = int(model->class_file_count("maybe"));
    std::printf("UI-SMOKE classify-tag: row=%d file=\"%s\" registry=%s model_class=\"%s\" "
                "dot_valid=%d maybe_count=%d panel_count=%d\n",
                target_row, qUtf8Printable(QFileInfo(target_path).fileName()),
                tagged.has_value() ? tagged->c_str() : "<none>", qUtf8Printable(model_class),
                dot.isValid() ? 1 : 0, maybe_count,
                classify_panel->row_count_value(2)); // 行 2 = 待定（默认模板顺序）
    std::fflush(stdout);
    if (tagged.value_or("") != "maybe")
        smoke_fail(MainWindow::tr("热键 '2' 未把文件标为「待定」（实为 %1）")
                       .arg(QString::fromStdString(tagged.value_or("<none>"))));
    if (model_class != QLatin1String("maybe"))
        smoke_fail(MainWindow::tr("ClassIdRole 未跟随打标：\"%1\"").arg(model_class));
    if (!dot.isValid())
        smoke_fail(MainWindow::tr("装饰角色（分类色点）未给出颜色"));
    if (classify_panel->row_count_value(2) != maybe_count)
        smoke_fail(MainWindow::tr("面板「待定」计数 %1 ≠ 模型计数 %2")
                       .arg(classify_panel->row_count_value(2))
                       .arg(maybe_count));

    // ---- (c) 右键菜单项集合 + 圈选三项语义 ----
    QMenu *class_menu = classify_panel->build_row_menu(1); // 行 1 = 精选
    QStringList class_actions;
    for (QAction *action : class_menu->actions()) {
        if (!action->isSeparator())
            class_actions << action->text();
    }
    delete class_menu;
    QMenu *all_menu = classify_panel->build_row_menu(0); // 行 0 = 「全部」
    QStringList all_actions;
    for (QAction *action : all_menu->actions()) {
        if (!action->isSeparator())
            all_actions << action->text();
    }
    delete all_menu;
    const QStringList expect_class{QStringLiteral("全选该类"), QStringLiteral("反选该类"),
                                   QStringLiteral("清空勾选"), QStringLiteral("重命名…"),
                                   QStringLiteral("颜色…"),    QStringLiteral("热键…"),
                                   QStringLiteral("删除")};
    const QStringList expect_all{QStringLiteral("全选该类"), QStringLiteral("反选该类"),
                                 QStringLiteral("清空勾选")};
    std::printf("UI-SMOKE classify-menu: class=[%s] all=[%s]\n",
                qUtf8Printable(class_actions.join(QLatin1Char(' '))),
                qUtf8Printable(all_actions.join(QLatin1Char(' '))));
    std::fflush(stdout);
    if (class_actions != expect_class)
        smoke_fail(MainWindow::tr("类行菜单不符：[%1]").arg(class_actions.join(QLatin1Char(' '))));
    if (all_actions != expect_all)
        smoke_fail(
            MainWindow::tr("「全部」行菜单不符：[%1]").arg(all_actions.join(QLatin1Char(' '))));
    // 圈选：该类全部取消 → 全选该类复原；反选该类；清空勾选
    classify_panel->invert_class(QStringLiteral("maybe")); // 只有 1 个待定文件 → 取消
    const int after_invert = int(model->checked_count());
    classify_panel->check_class(QStringLiteral("maybe")); // 全选该类 → +1（其余不动）
    const int after_check_class = int(model->checked_count());
    classify_panel->clear_checks();
    const int after_clear = int(model->checked_count());
    classify_panel->check_class(QString()); // 「全部」= 全部文件
    const int after_all = int(model->checked_count());
    std::printf("UI-SMOKE classify-bulk: invert=%d check_class=%d clear=%d all=%d total=%d\n",
                after_invert, after_check_class, after_clear, after_all, int(model->size()));
    std::fflush(stdout);
    if (after_invert != int(model->size()) - 1)
        smoke_fail(MainWindow::tr("反选该类计数不符：%1（期望 %2）")
                       .arg(after_invert)
                       .arg(int(model->size()) - 1));
    if (after_check_class != int(model->size()))
        smoke_fail(MainWindow::tr("全选该类计数不符：%1").arg(after_check_class));
    if (after_clear != 0)
        smoke_fail(MainWindow::tr("清空勾选后仍有 %1 个勾选").arg(after_clear));
    if (after_all != int(model->size()))
        smoke_fail(MainWindow::tr("「全部」全选计数不符：%1").arg(after_all));

    // ---- (d) CRUD（§6.2）：增 / 改名 / 颜色 / 热键唯一 / 删；「全部」不可改 ----
    // 中文名 → ASCII slug 不可用 → id 走 "class" 兜底（id 是持久化键，显示名可随时改）
    const bool created = classify_panel->new_class(QStringLiteral("旅行"), 0x1565C0u, '4');
    const QString new_id =
        classes.classes.empty() ? QString() : QString::fromStdString(classes.classes.back().id);
    const bool slug_fallback = new_id == QLatin1String("class");
    const bool dup_hotkey_rejected = !classify_panel->set_class_hotkey(new_id, '1');
    const bool renamed = classify_panel->rename_class(new_id, QStringLiteral("远行"));
    const bool recolored = classify_panel->set_class_color(new_id, 0xFF00FFu);
    const bool all_immutable =
        !classify_panel->rename_class(QString::fromUtf8(pp::ClassRegistry::kAllId.data(),
                                                        int(pp::ClassRegistry::kAllId.size())),
                                      QStringLiteral("x")) &&
        !classify_panel->delete_class(QString::fromUtf8(pp::ClassRegistry::kAllId.data(),
                                                        int(pp::ClassRegistry::kAllId.size())));
    const std::size_t rows_after_create = classes.classes.size();
    const pp::ClassDef *travel = classes.find(new_id.toStdString());
    std::printf(
        "UI-SMOKE classify-crud: created=%d id=\"%s\" slug_fallback=%d "
        "dup_hotkey_rejected=%d renamed=%d recolored=%d classes=%d name=\"%s\" rgb=%06X "
        "hotkey=%c all_immutable=%d\n",
        created ? 1 : 0, qUtf8Printable(new_id), slug_fallback ? 1 : 0, dup_hotkey_rejected ? 1 : 0,
        renamed ? 1 : 0, recolored ? 1 : 0, int(rows_after_create),
        travel != nullptr ? travel->name.c_str() : "<none>", travel != nullptr ? travel->rgb : 0u,
        travel != nullptr && travel->hotkey != 0 ? travel->hotkey : '-', all_immutable ? 1 : 0);
    std::fflush(stdout);
    if (!created || !dup_hotkey_rejected || !renamed || !recolored)
        smoke_fail(MainWindow::tr("CRUD 失败：created=%1 dup_hotkey=%2 rename=%3 color=%4")
                       .arg(created ? 1 : 0)
                       .arg(dup_hotkey_rejected ? 1 : 0)
                       .arg(renamed ? 1 : 0)
                       .arg(recolored ? 1 : 0));
    if (travel == nullptr || travel->name != "远行" || (travel->rgb & 0xFFFFFFu) != 0xFF00FFu)
        smoke_fail(MainWindow::tr("CRUD 结果未落注册表"));
    if (!all_immutable)
        smoke_fail(MainWindow::tr("保留虚拟分类「全部」被改名/删除成功（应被拒）"));
    if (classify_panel->row_count() != 1 + int(classes.classes.size()))
        smoke_fail(MainWindow::tr("CRUD 后面板行数未跟随"));

    // ---- (e) classes.json 往返（§6.2 持久化）----
    save_classes(); // 打标 + CRUD 均已触发；这里显式落盘取文件
    const std::filesystem::path file(classes_file.toStdString());
    pp::ClassRegistry reloaded;
    std::string load_err;
    const bool loaded = reloaded.load(file, &load_err);
    bool same = loaded && reloaded.classes.size() == classes.classes.size() &&
                reloaded.assignment.size() == classes.assignment.size();
    for (std::size_t i = 0; same && i < classes.classes.size(); ++i) {
        const pp::ClassDef &a = classes.classes[i];
        const pp::ClassDef &b = reloaded.classes[i];
        same = a.id == b.id && a.name == b.name && a.rgb == b.rgb && a.hotkey == b.hotkey;
    }
    const std::optional<std::string> reloaded_class =
        reloaded.class_of(std::filesystem::path(target_path.toStdString()));
    std::printf("UI-SMOKE classify-roundtrip: file=\"%s\" loaded=%d classes=%d/%d "
                "assignment=%d/%d same=%d tagged=%s err=\"%s\"\n",
                qUtf8Printable(QFileInfo(classes_file).fileName()), loaded ? 1 : 0,
                int(reloaded.classes.size()), int(classes.classes.size()),
                int(reloaded.assignment.size()), int(classes.assignment.size()), same ? 1 : 0,
                reloaded_class.has_value() ? reloaded_class->c_str() : "<none>", load_err.c_str());
    std::fflush(stdout);
    if (!loaded || !same)
        smoke_fail(MainWindow::tr("classes.json 往返不符（loaded=%1 same=%2 err=%3）")
                       .arg(loaded ? 1 : 0)
                       .arg(same ? 1 : 0)
                       .arg(QString::fromStdString(load_err)));
    if (reloaded_class.value_or("") != "maybe")
        smoke_fail(MainWindow::tr("往返后归属丢失：%1")
                       .arg(QString::fromStdString(reloaded_class.value_or("<none>"))));

    // ---- (f) 运行期只读（G5）：锁 → 行/新建钮置灰、菜单不弹 ----
    classify_panel->set_locked(true);
    const bool locked_rows_disabled =
        classify_panel->row_widget(0) != nullptr && !classify_panel->row_widget(0)->isEnabled();
    const int locked_checked = int(model->checked_count());
    classify_panel->invert_class(QStringLiteral("maybe")); // 锁定时为 no-op
    const bool lock_respected = int(model->checked_count()) == locked_checked;
    classify_panel->set_locked(false);
    std::printf("UI-SMOKE classify-lock: rows_disabled=%d bulk_noop=%d\n",
                locked_rows_disabled ? 1 : 0, lock_respected ? 1 : 0);
    std::fflush(stdout);
    if (!locked_rows_disabled || !lock_respected)
        smoke_fail(MainWindow::tr("运行期只读未生效（rows=%1 noop=%2）")
                       .arg(locked_rows_disabled ? 1 : 0)
                       .arg(lock_respected ? 1 : 0));

    // ---- 归位：还原注册表快照与 classes.json 路径（截图基准不受冒烟污染）----
    classes = backup;
    classes_file = saved_file;
    model->notify_class_changed();
    classify_panel->set_registry(&classes);
    preview_panel->set_class_registry(&classes);
    model->set_all_checked(true);
    if (view->selectionModel() != nullptr)
        view->selectionModel()->clearSelection();
    view->setCurrentIndex(QModelIndex());
    pump(120);
}

// ---------------------------------------------------------------------------
// M4-W3-T14 运行页自检（§3.4 + §7.1-7.4 + §9.3；mockup run-dark = 精确规格）
//   * 页实例独立（不动主窗口的运行页）：外壳容器挂主题骨架 QSS（窗口底渐变 + QFrame#pp-card
//     等只作用于 pp-* 钩子的规则），页面只吃 tokens —— 与 smoke_probe_meta 的独立页同口径；
//   * 探针覆盖：逐输出子行五态（真实进行中 / 斜纹合成 / 完成产物事实 / 失败原因 / 交错排队）·
//     **真实 vs 斜纹的像素级取证**（斜纹 = 双色带 #3aa3dc/#4cc2ff 同时出现；实心 = 只有 accent，
//     无 #3aa3dc）· 总览只读栅格（交错/线程预算/并行/当前分配）· 取消态 · 浅色主题。
// ---------------------------------------------------------------------------
namespace {

// 迷你进度条取样：统计左/右三色 token 的命中数（取样带 = 条内中部，避开 1px 边框与圆角）
struct BarPixels {
    int samples = 0, accent = 0, from = 0, to = 0;
    double accent_ratio() const { return samples > 0 ? double(accent) / samples : 0.0; }
    double from_ratio() const { return samples > 0 ? double(from) / samples : 0.0; }
    double to_ratio() const { return samples > 0 ? double(to) / samples : 0.0; }
};

BarPixels probe_bar_pixels(const QImage &img, const theme::Tokens &t, double frac) {
    BarPixels p;
    if (img.isNull() || img.width() < 24 || img.height() < 4)
        return p;
    const auto near = [](QRgb c, const QColor &ref) {
        return std::abs(qRed(c) - ref.red()) <= 16 && std::abs(qGreen(c) - ref.green()) <= 16 &&
               std::abs(qBlue(c) - ref.blue()) <= 16;
    };
    // 取样带 = **期望填充区的内部**：grab() 会用调色板窗底色预填 pixmap（实测轨道像素不透明），
    // 故不能靠 alpha 区分轨道/填充；改为按语义读数 frac（ppBarFrac）算右沿，再两侧各内缩 4px
    // 避开 1px 边框与圆角弧。
    const int inner_left = 2;
    const int inner_right = img.width() - 2;
    const int fill_right = inner_left + int(double(inner_right - inner_left) * frac);
    const int left = inner_left + 4;
    const int right = std::min(inner_right - 4, fill_right - 4);
    if (right <= left)
        return p;
    const int y = img.height() / 2;
    for (int x = left; x <= right; ++x) {
        const QRgb c = img.pixel(x, y);
        ++p.samples;
        if (near(c, t.accent))
            ++p.accent;
        if (near(c, t.progress_fill_from))
            ++p.from;
        if (near(c, t.progress_fill_to))
            ++p.to;
    }
    return p;
}

// 取样带色彩直方图（失败时的取证：把实际画出来的颜色列出来，不靠猜）
QString probe_bar_histogram(const QImage &img) {
    if (img.isNull())
        return QStringLiteral("(null)");
    QHash<QRgb, int> hist;
    const int y = img.height() / 2;
    for (int x = 0; x < img.width(); ++x) {
        const QRgb c = img.pixel(x, y);
        ++hist[static_cast<QRgb>(c | 0xff000000u)]; // 忽略 alpha 维度用于分组
    }
    QList<QPair<int, QRgb>> sorted;
    sorted.reserve(hist.size());
    for (auto it = hist.constBegin(); it != hist.constEnd(); ++it)
        sorted.append({it.value(), it.key()});
    std::sort(
        sorted.begin(), sorted.end(),
        [](const QPair<int, QRgb> &a, const QPair<int, QRgb> &b) { return a.first > b.first; });
    QStringList parts;
    for (int i = 0; i < sorted.size() && i < 4; ++i) {
        parts << QStringLiteral("%1x%2")
                     .arg(QColor(QRgb(sorted[i].second)).name(QColor::HexRgb))
                     .arg(sorted[i].first);
    }
    return QStringLiteral("%1x%2 [%3]")
        .arg(img.width())
        .arg(img.height())
        .arg(parts.join(QLatin1Char(' ')));
}

// 探针用的事件构造
pp::FileEvent probe_progress(int index, int out_index, pp::Stage stage, float frac, bool synth) {
    pp::FileEvent ev{};
    ev.index = static_cast<std::size_t>(index);
    ev.state = pp::FileState::Progress;
    ev.progress.output_index = out_index;
    ev.progress.stage = stage;
    ev.progress.stage_frac = frac;
    ev.progress.overall_frac = frac;
    ev.progress.synthetic = synth;
    return ev;
}

pp::FileEvent probe_terminal(int index, pp::FileState state, const pp::FileResult &result) {
    pp::FileEvent ev{};
    ev.index = static_cast<std::size_t>(index);
    ev.state = state;
    ev.result = &result;
    return ev;
}

QString probe_full_text(const QWidget *page, const QString &name) {
    const auto *label = page->findChild<QLabel *>(name);
    if (label == nullptr)
        return QString();
    const QVariant full = label->property("ppFullText");
    return full.isValid() ? full.toString() : label->text();
}

} // namespace

void MainWindow::Impl::smoke_probe_run(const QString &shots_dir) {
    theme::Tokens dark = tokens;
    if (dark.mode != theme::ThemeMode::Dark)
        dark = theme::tokens(theme::ThemeMode::Dark); // 探针基准恒深色（原型基准）

    QWidget shell;
    shell.setObjectName(QStringLiteral("pp-root")); // theme 骨架 QSS 的窗口底（渐变）
    shell.setStyleSheet(theme::style_sheet(dark));
    shell.resize(theme::Metrics::right_width_1440, theme::Metrics::calibrated_h - 118);
    auto *shell_layout = new QVBoxLayout(&shell);
    shell_layout->setContentsMargins(0, 0, 0, 0);
    auto *page = new PageRun(&shell);
    shell_layout->addWidget(page);
    page->set_tokens(dark);
    shell.show();
    pump(200);

    // ---- 运行计划：4 个源文件 × 2 格式（mockup run-dark 的任务行组合）----
    PageRun::RunPlan plan;
    plan.format_ids = QStringList{QStringLiteral("jpeg"), QStringLiteral("webp")};
    plan.widths = QVector<int>{4032, 2480, 6000, 4032};
    plan.heights = QVector<int>{3024, 3508, 4000, 3024};
    plan.bytes = QVector<qint64>{5'000'000, 6'200'000, 7'400'000, 1'310'720};
    plan.stagger_ms = 150; // §8.1 默认（mockup「交错 150 ms」）
    plan.workers = 0;      // 0 = 逻辑核
    plan.thread_budget = 0;
    page->set_plan(plan);
    page->set_log_tail(
        QStringList{
            QStringLiteral("18:23:43.412 [info] [encode] IMG_2732.jpg → webp 228 KB ratio=5.6 "
                           "t=0.9s"),
            QStringLiteral("18:23:44.087 [warn] [depth] scan_007.png 16bit→8bit 降档被禁止，跳过"),
            QStringLiteral("18:23:44.560 [info] [sched] DSC_0412.ARW.tif 启动（交错偏移 150ms）"),
            QStringLiteral("18:23:45.004 [info] [encode] P1010888.RW2.jpg → jpeg 640 KB ratio=4.1 "
                           "t=1.2s"),
            QStringLiteral("18:23:45.510 [info] [run] 24 个输出已结算 · 成功 22 · 失败 1 · 跳过 1"),
        },
        QStringLiteral("run-20260923-182341.log"));

    // ---- 03c-run-idle.png：空闲态（就绪 + 空任务表 + 引导 + 日志尾）----
    page->reset();
    pump(120);
    {
        const QString pill = probe_full_text(page, QStringLiteral("pp-run-state-pill"));
        const QString summary = probe_full_text(page, QStringLiteral("pp-run-summary"));
        const QString empty = probe_full_text(page, QStringLiteral("pp-run-task-empty"));
        std::printf("UI-SMOKE run-idle: pill=\"%s\" summary=\"%s\" empty=\"%s\" bars=%d\n",
                    qUtf8Printable(pill), qUtf8Printable(summary), qUtf8Printable(empty),
                    int(page->findChildren<QWidget *>(
                                QRegularExpression(QStringLiteral("^pp-run-bar-\\d+-\\d+$")))
                            .size()));
        std::fflush(stdout);
        if (pill != QStringLiteral("就绪") || summary.isEmpty() || empty.isEmpty())
            smoke_fail(MainWindow::tr("运行页空闲态不符（pill=%1 summary=%2 empty=%3）")
                           .arg(pill, summary, empty));
    }
    smoke_grab(shots_dir, kSmokeShots[kShotRunIdle], &shell);

    // ---- 03d-run-rows.png：逐输出行探针态（真实 / 斜纹 / 完成 / 失败 / 交错排队）----
    page->begin_run(4, QStringList{QStringLiteral("IMG_2731.jpg"), QStringLiteral("scan_007.png"),
                                   QStringLiteral("P1010888.RW2.jpg"),
                                   QStringLiteral("IMG_2732.jpg")});
    pump(120);
    // 文件 0：共享段（probe/decode/color）→ JPEG 真实行级 78% + WebP 合成 52%
    page->on_event(probe_progress(0, -1, pp::Stage::Probe, 1.0f, false));
    page->on_event(probe_progress(0, -1, pp::Stage::Decode, 1.0f, false));
    page->on_event(probe_progress(0, -1, pp::Stage::Color, 1.0f, false));
    page->on_event(probe_progress(0, 0, pp::Stage::Encode, 0.78f, false));
    page->on_event(probe_progress(0, 1, pp::Stage::Encode, 0.52f, true));
    // 文件 1：共享段 → JPEG 进行中 30% → 终态失败（逐输出原因 + 文件级聚合原因同文案）
    {
        pp::FileResult failed;
        failed.info.width = 2480;
        failed.info.height = 3508;
        failed.error = QStringLiteral("16→8 位降档被禁止").toStdString();
        failed.outputs.resize(2);
        failed.outputs[0].format_id = "jpeg";
        failed.outputs[0].error = QStringLiteral("16→8 位降档被禁止").toStdString();
        failed.outputs[0].t.encode_ms = 640;
        failed.outputs[1].format_id = "webp";
        page->on_event(probe_progress(1, -1, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_progress(1, 0, pp::Stage::Encode, 0.30f, false));
        page->on_event(probe_terminal(1, pp::FileState::Failed, failed));
    }
    // 文件 2：无事件 = 交错排队（stagger_ms=150）→「排队中（交错启动）」
    // 文件 3：终态完成（产物事实：字节 · 压缩比 · 耗时）
    {
        pp::FileResult done;
        done.info.width = 4032;
        done.info.height = 3024;
        done.out_bytes = 421888 + 233472;
        done.outputs.resize(2);
        done.outputs[0].format_id = "jpeg";
        done.outputs[0].ok = true;
        done.outputs[0].out_bytes = 421888; // 412 KB
        done.outputs[0].t.encode_ms = 2300;
        done.outputs[0].t.metawrite_ms = 100; // 2.4s；ratio = 1310720/421888 = 3.1×
        done.outputs[1].format_id = "webp";
        done.outputs[1].ok = true;
        done.outputs[1].out_bytes = 233472; // 228 KB
        done.outputs[1].t.encode_ms = 850;
        done.outputs[1].t.metawrite_ms = 50; // 0.9s；ratio = 5.6×
        page->on_event(probe_progress(3, -1, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_terminal(3, pp::FileState::Done, done));
    }
    pump(250); // 60ms flush + 布局

    // ---- (a) 行数与行文案 ----
    {
        const int bars = int(page->findChildren<QWidget *>(
                                     QRegularExpression(QStringLiteral("^pp-run-bar-\\d+-\\d+$")))
                                 .size());
        const QString f0_real = probe_full_text(page, QStringLiteral("pp-run-stage-0-0"));
        const QString f0_synth = probe_full_text(page, QStringLiteral("pp-run-stage-0-1"));
        const QString f1_stage = probe_full_text(page, QStringLiteral("pp-run-stage-1-0"));
        const QString f1_pct = probe_full_text(page, QStringLiteral("pp-run-pct-1-0"));
        const QString f2_row0 = probe_full_text(page, QStringLiteral("pp-run-stage-2-0"));
        const QString f2_row1 = probe_full_text(page, QStringLiteral("pp-run-stage-2-1"));
        const QString f3_pct = probe_full_text(page, QStringLiteral("pp-run-pct-3-0"));
        const QString f3_stage = probe_full_text(page, QStringLiteral("pp-run-stage-3-0"));
        std::printf("UI-SMOKE run-probe-rows: bars=%d f0=[%s|%s] f1=[%s|%s] f2=[%s|%s] "
                    "f3=[%s|%s]\n",
                    bars, qUtf8Printable(f0_real), qUtf8Printable(f0_synth),
                    qUtf8Printable(f1_stage), qUtf8Printable(f1_pct), qUtf8Printable(f2_row0),
                    qUtf8Printable(f2_row1), qUtf8Printable(f3_pct), qUtf8Printable(f3_stage));
        std::fflush(stdout);
        if (bars != 8)
            smoke_fail(MainWindow::tr("逐输出行数不符：bar=%1（期望 8）").arg(bars));
        if (!f0_real.contains(QStringLiteral("行")) || !f0_real.contains(QStringLiteral("/3024")))
            smoke_fail(MainWindow::tr("真实行级进度未按「编码 N/M 行」显示：%1").arg(f0_real));
        if (f0_synth != QStringLiteral("编码（合成进度）"))
            smoke_fail(MainWindow::tr("合成进度行文案不符：%1").arg(f0_synth));
        if (f1_stage != QStringLiteral("16→8 位降档被禁止") || f1_pct != QStringLiteral("失败"))
            smoke_fail(MainWindow::tr("失败行未直显原因：pct=%1 stage=%2").arg(f1_pct, f1_stage));
        if (f2_row0 != QStringLiteral("排队中（交错启动）") || f2_row1 != QStringLiteral("排队中"))
            smoke_fail(MainWindow::tr("排队行文案不符：%1 / %2").arg(f2_row0, f2_row1));
        if (f3_pct != QStringLiteral("412 KB") || !f3_stage.contains(QStringLiteral("3.1×")) ||
            !f3_stage.contains(QStringLiteral("2.4s")))
            smoke_fail(MainWindow::tr("完成行未显示产物事实（字节·压缩比·耗时）：%1 / %2")
                           .arg(f3_pct, f3_stage));
    }

    // ---- (b) 真实 vs 斜纹：像素级取证（斜纹 = 双色带同时出现；实心 = 无 #3aa3dc）----
    {
        auto *real_bar = page->findChild<QWidget *>(QStringLiteral("pp-run-bar-0-0"));
        auto *synth_bar = page->findChild<QWidget *>(QStringLiteral("pp-run-bar-0-1"));
        if (real_bar == nullptr || synth_bar == nullptr) {
            smoke_fail(MainWindow::tr("运行页缺少逐输出迷你条钩子（pp-run-bar-0-*）"));
        } else {
            const BarPixels real_px = probe_bar_pixels(real_bar->grab().toImage(), dark,
                                                       real_bar->property("ppBarFrac").toDouble());
            const BarPixels synth_px = probe_bar_pixels(
                synth_bar->grab().toImage(), dark, synth_bar->property("ppBarFrac").toDouble());
            std::printf("UI-SMOKE run-bars: real{samples=%d accent=%.2f from=%.2f to=%.2f} "
                        "synth{samples=%d accent=%.2f from=%.2f to=%.2f} synth-flag=%d/%d\n",
                        real_px.samples, real_px.accent_ratio(), real_px.from_ratio(),
                        real_px.to_ratio(), synth_px.samples, synth_px.accent_ratio(),
                        synth_px.from_ratio(), synth_px.to_ratio(),
                        real_bar->property("ppBarSynthetic").toBool() ? 1 : 0,
                        synth_bar->property("ppBarSynthetic").toBool() ? 1 : 0);
            std::printf("UI-SMOKE run-bars-hist: real=%s synth=%s accent=%s from=%s to=%s\n",
                        qUtf8Printable(probe_bar_histogram(real_bar->grab().toImage())),
                        qUtf8Printable(probe_bar_histogram(synth_bar->grab().toImage())),
                        qUtf8Printable(theme::css_color(dark.accent)),
                        qUtf8Printable(theme::css_color(dark.progress_fill_from)),
                        qUtf8Printable(theme::css_color(dark.progress_fill_to)));
            std::fflush(stdout);
            const bool real_solid = real_px.samples > 40 && real_px.accent_ratio() >= 0.85 &&
                                    real_px.from_ratio() <= 0.05;
            const bool synth_striped =
                synth_px.samples > 40 && synth_px.from_ratio() >= 0.2 && synth_px.to_ratio() >= 0.2;
            const bool flags = !real_bar->property("ppBarSynthetic").toBool() &&
                               synth_bar->property("ppBarSynthetic").toBool();
            if (!real_solid || !synth_striped || !flags)
                smoke_fail(
                    MainWindow::tr("真实/斜纹渲染不符（real-solid=%1 synth-striped=%2 flags=%3）")
                        .arg(real_solid ? 1 : 0)
                        .arg(synth_striped ? 1 : 0)
                        .arg(flags ? 1 : 0));
        }
    }

    // ---- (c) 总览卡只读栅格（§3.4/§8.1/§8.2）----
    {
        const int cores = [] {
            const unsigned hw = std::thread::hardware_concurrency();
            return hw > 0 ? static_cast<int>(hw) : 1;
        }();
        const QString stagger = probe_full_text(page, QStringLiteral("pp-run-stagger"));
        const QString budget = probe_full_text(page, QStringLiteral("pp-run-budget"));
        const QString workers = probe_full_text(page, QStringLiteral("pp-run-workers"));
        const QString alloc = probe_full_text(page, QStringLiteral("pp-run-alloc"));
        const QString hint = probe_full_text(page, QStringLiteral("pp-run-progress-hint"));
        const QString percent = probe_full_text(page, QStringLiteral("pp-run-percent"));
        std::printf("UI-SMOKE run-overview: stagger=\"%s\" budget=\"%s\" workers=\"%s\" "
                    "alloc=\"%s\" hint=\"%s\" percent=\"%s\"\n",
                    qUtf8Printable(stagger), qUtf8Printable(budget), qUtf8Printable(workers),
                    qUtf8Printable(alloc), qUtf8Printable(hint), qUtf8Printable(percent));
        std::fflush(stdout);
        if (stagger != QStringLiteral("150 ms"))
            smoke_fail(MainWindow::tr("交错读数不符：%1").arg(stagger));
        if (budget != MainWindow::tr("自适应（%1 核）").arg(cores))
            smoke_fail(MainWindow::tr("线程预算读数不符：%1（期望 自适应（%2 核））")
                           .arg(budget)
                           .arg(cores));
        if (!workers.endsWith(QStringLiteral("workers")) ||
            !alloc.contains(QStringLiteral("文件并行")))
            smoke_fail(MainWindow::tr("并行/当前分配读数不符：%1 / %2").arg(workers, alloc));
        if (hint != QStringLiteral("4 / 8 个输出"))
            smoke_fail(MainWindow::tr("总览输出计数不符：%1（期望 4 / 8 个输出）").arg(hint));
        if (percent.isEmpty() || percent == QStringLiteral("0%"))
            smoke_fail(MainWindow::tr("总览百分比未推进：%1").arg(percent));
    }
    smoke_grab(shots_dir, kSmokeShots[kShotRunRows], &shell);

    // ---- (d) 取消态（唯一取消入口 = 运行页；取消后定稿为「已取消」）----
    {
        int cancel_hits = 0;
        const QMetaObject::Connection conn = QObject::connect(
            page, &PageRun::cancel_requested, page, [&cancel_hits]() { ++cancel_hits; });
        page->begin_run(
            2, QStringList{QStringLiteral("IMG_2731.jpg"), QStringLiteral("IMG_2732.jpg")});
        page->on_event(probe_progress(0, -1, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_progress(0, 0, pp::Stage::Encode, 0.42f, false));
        pump(120);
        auto *cancel = page->findChild<QPushButton *>(QStringLiteral("pp-run-cancel"));
        if (cancel == nullptr || !cancel->isEnabled()) {
            smoke_fail(MainWindow::tr("运行页取消钮不可用（pp-run-cancel）"));
        } else {
            cancel->click();
            pump(120);
            pp::RunSummary sum;
            sum.total = 2;
            sum.cancelled = 2;
            sum.total_ms = 900.0;
            page->on_event(probe_terminal(0, pp::FileState::Cancelled, pp::FileResult{}));
            page->on_event(probe_terminal(1, pp::FileState::Cancelled, pp::FileResult{}));
            page->end_run(sum, QString());
            pump(150);
            const QString pill = probe_full_text(page, QStringLiteral("pp-run-state-pill"));
            const QString row0 = probe_full_text(page, QStringLiteral("pp-run-stage-0-0"));
            std::printf("UI-SMOKE run-cancel: hits=%d pill=\"%s\" row0=\"%s\" cancel-visible=%d\n",
                        cancel_hits, qUtf8Printable(pill), qUtf8Printable(row0),
                        cancel->isVisible() ? 1 : 0);
            std::fflush(stdout);
            if (cancel_hits != 1 || pill != QStringLiteral("已取消") || cancel->isVisible() ||
                row0 != QStringLiteral("取消"))
                smoke_fail(MainWindow::tr("取消态不符（hits=%1 pill=%2 row0=%3 cancel-visible=%4）")
                               .arg(cancel_hits)
                               .arg(pill, row0)
                               .arg(cancel->isVisible() ? 1 : 0));
            smoke_grab(shots_dir, kSmokeShots[kShotRunCancel], &shell);
        }
        QObject::disconnect(conn);
    }

    // ---- (e) 浅色主题（07-run-light.png）：同一页 tokens 重放 ----
    {
        const theme::Tokens light = theme::tokens(theme::ThemeMode::Light);
        shell.setStyleSheet(theme::style_sheet(light));
        page->set_tokens(light);
        page->begin_run(
            4, QStringList{QStringLiteral("IMG_2731.jpg"), QStringLiteral("scan_007.png"),
                           QStringLiteral("P1010888.RW2.jpg"), QStringLiteral("IMG_2732.jpg")});
        page->on_event(probe_progress(0, -1, pp::Stage::Decode, 1.0f, false));
        page->on_event(probe_progress(0, 0, pp::Stage::Encode, 0.78f, false));
        page->on_event(probe_progress(0, 1, pp::Stage::Encode, 0.52f, true));
        pump(250);
        QWidget *light_bar = page->findChild<QWidget *>(QStringLiteral("pp-run-bar-0-1"));
        const BarPixels light_synth =
            light_bar != nullptr ? probe_bar_pixels(light_bar->grab().toImage(), light,
                                                    light_bar->property("ppBarFrac").toDouble())
                                 : BarPixels{};
        std::printf("UI-SMOKE run-light: synth{samples=%d from=%.2f to=%.2f} window=%dx%d\n",
                    light_synth.samples, light_synth.from_ratio(), light_synth.to_ratio(),
                    shell.width(), shell.height());
        std::fflush(stdout);
        if (light_synth.samples == 0 || light_synth.from_ratio() < 0.2 ||
            light_synth.to_ratio() < 0.2)
            smoke_fail(MainWindow::tr("浅色主题下斜纹渲染不符（samples=%1 from=%2 to=%3）")
                           .arg(light_synth.samples)
                           .arg(light_synth.from_ratio(), 0, 'f', 2)
                           .arg(light_synth.to_ratio(), 0, 'f', 2));
        smoke_grab(shots_dir, kSmokeShots[kShotRunLight], &shell);
    }
    page->reset();
    pump(80);
}

void MainWindow::Impl::smoke_run(const QString &shots_dir) {
    // ---- M4-T9 骨架自检（窗口行为矩阵可自动化部分；先跑，之后截图归位 1440×900）----
    smoke_probe_lifecycle();
    smoke_probe_skeleton();
    smoke_probe_frameless();
    smoke_probe_theme();
    smoke_probe_preview();       // M4-T10：预览面板（翻图/缩放/徽标/热键接线位）
    smoke_probe_filelist();      // M4-T11：勾选/正交搜索/分组节（§3.6 roles + §6.2）
    smoke_probe_meta(shots_dir); // M4-T12：生效值显示/写入一致性 + 多选小表 + 地图跟随（§5.2）

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

    // ---- 实跑前置：out_root=<仓库>/.cache/tmp/ui-smoke-out + conflict=overwrite ----
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
    if (page_output->out_root() != out_root) {
        smoke_fail(MainWindow::tr("冒烟前置：输出根目录未生效（%1）").arg(page_output->out_root()));
    }
    // M4-T14 实跑前置：输出 = **JPEG + WebP**（mockup run-dark 任务行的两格式组合）——让
    // 03-run.png 的「逐输出子行」在真实运行下呈现 N=2 行（§3.4「多格式时一个源文件 N 行输出」），
    // 并顺带覆盖多输出管线的 UI 事件流与"真实/合成"两类进度。走冻结 API 的 collect/apply 往返
    // （select_format 是单选钩子，构造多选集合只能经预设面），同时把冲突策略置为覆盖。
    {
        page_output->select_format(QStringLiteral("jpeg"));
        pump(250);
        pp::PresetData preset = page_output->collect_preset(QStringLiteral("ui-smoke"));
        if (preset.outputs.size() != 1) {
            smoke_fail(MainWindow::tr("冒烟前置：jpeg 单选应收集到 1 个输出（实为 %1）")
                           .arg(preset.outputs.size()));
        }
        const pp::OutputFormatSpec jpeg_spec = preset.outputs.front();
        page_output->select_format(QStringLiteral("webp"));
        pump(250);
        preset = page_output->collect_preset(QStringLiteral("ui-smoke"));
        const pp::OutputFormatSpec webp_spec = preset.outputs.front();
        preset.outputs = {jpeg_spec, webp_spec};
        preset.conflict = pp::ConflictPolicy::Overwrite;
        preset.split_by_format = true; // 分格式子目录（$format/$dir/$file）
        preset.output_template = "$format/$dir/$file";
        page_output->apply_preset(preset);
        pump(250);
    }
    const std::vector<pp::OutputFormatSpec> run_outputs = page_output->config_base().outputs;
    if (run_outputs.size() != 2) {
        smoke_fail(
            MainWindow::tr("冒烟前置：输出格式不是 2 项（实为 %1）").arg(run_outputs.size()));
    }
    if (page_output->config_base().conflict != pp::ConflictPolicy::Overwrite) {
        smoke_fail(MainWindow::tr("冒烟前置：冲突策略不是覆盖"));
    }

    // ---- 页 3（运行）：点开始 → 03-run.png ----
    w->set_current_page(3);
    pump(200);
    start->click();
    // 尽量抓在"运行中"：本批 16 个小文件很短，故只推进到出现部分进度即抓，
    // 否则退化为结束态（与 03b 同图，仅记入 stdout 供审查判断）。
    QProgressBar *run_progress = w->findChild<QProgressBar *>(QStringLiteral("pp-run-progress"));
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
    std::printf("UI-SMOKE 03-run: running=%d progress=%d/%d outputs=%d/%d bottom=%s\n",
                page_run->is_running() ? 1 : 0,
                run_progress != nullptr ? run_progress->value() : -1,
                run_progress != nullptr ? run_progress->maximum() : -1, page_run->outputs_done(),
                page_run->outputs_total(), qUtf8Printable(start->text()));
    std::fflush(stdout);
    // G5 锁定视觉（M4-W2-fix 第 7 条 + T14 锁定接线）：运行中「设置/预设」置灰（QSS :disabled =
    // .icon-btn.dim opacity .4 的等效预合成色）、步骤 1/2 置灰（.step.dim opacity .45，含编号
    // 徽标）、左栏文件面板/分组下拉置灰、预览与分类只读、运行页取消可点（唯一取消入口）。
    {
        const bool presets_off = presets_btn != nullptr && !presets_btn->isEnabled();
        const bool settings_off = settings_btn != nullptr && !settings_btn->isEnabled();
        const bool steps_off = step_meta != nullptr && !step_meta->isEnabled() &&
                               step_output != nullptr && !step_output->isEnabled();
        const bool panel_off = panel != nullptr && !panel->isEnabled();
        const bool group_off = group_mode != nullptr && !group_mode->isEnabled();
        const bool preview_locked =
            [&] { // 预览只读的**可观测面** = 打标热键表置灰（PreviewPanel::set_locked 的唯一落地）
                QLabel *keyhint =
                    w->findChild<QLabel *>(QStringLiteral("pp-preview-keyhint-label"));
                return keyhint != nullptr && !keyhint->isEnabled();
            }();
        QPushButton *run_cancel = w->findChild<QPushButton *>(QStringLiteral("pp-run-cancel"));
        const bool cancel_ready =
            run_cancel != nullptr && run_cancel->isVisible() && run_cancel->isEnabled();
        const bool new_page_locked = page_meta != nullptr && !page_meta->isEnabled() &&
                                     page_output != nullptr && !page_output->isEnabled();
        std::printf("UI-SMOKE lock-visual: presets=%d settings=%d steps1-2=%d panel=%d group=%d "
                    "preview-locked=%d page1-2=%d run-cancel=%d\n",
                    presets_off ? 1 : 0, settings_off ? 1 : 0, steps_off ? 1 : 0, panel_off ? 1 : 0,
                    group_off ? 1 : 0, preview_locked ? 1 : 0, new_page_locked ? 1 : 0,
                    cancel_ready ? 1 : 0);
        std::fflush(stdout);
        if (!presets_off || !settings_off || !steps_off || !panel_off || !group_off ||
            !preview_locked || !new_page_locked || !cancel_ready) {
            smoke_fail(MainWindow::tr("运行期锁定未生效（预设=%1 设置=%2 步骤=%3 文件面板=%4 "
                                      "分组=%5 预览=%6 页1-2=%7 取消钮=%8）")
                           .arg(presets_off ? 1 : 0)
                           .arg(settings_off ? 1 : 0)
                           .arg(steps_off ? 1 : 0)
                           .arg(panel_off ? 1 : 0)
                           .arg(group_off ? 1 : 0)
                           .arg(preview_locked ? 1 : 0)
                           .arg(new_page_locked ? 1 : 0)
                           .arg(cancel_ready ? 1 : 0));
        }
    }
    // ---- M4-T14 断言：真实运行下的**逐输出行**（§3.4：行数 = 文件数 × 格式数；运行中的行有
    // 阶段文字/进度读数；锁定态的行不因运行而消失）----
    {
        const QList<QWidget *> bars = w->findChildren<QWidget *>(
            QRegularExpression(QStringLiteral("^pp-run-bar-\\d+-\\d+$")));
        const QList<QWidget *> groups =
            w->findChildren<QWidget *>(QRegularExpression(QStringLiteral("^pp-run-group-\\d+$")));
        const int expect_rows = int(model->checked_count()) * int(run_outputs.size());
        std::printf("UI-SMOKE run-rows: bars=%d expect=%d groups=%d\n", bars.size(), expect_rows,
                    groups.size());
        std::fflush(stdout);
        if (bars.size() != expect_rows || groups.size() != int(model->checked_count())) {
            smoke_fail(MainWindow::tr("逐输出行数不符：bar=%1 group=%2（期望 %3/%4）")
                           .arg(bars.size())
                           .arg(groups.size())
                           .arg(expect_rows)
                           .arg(model->checked_count()));
        }
    }
    // ---- M4-T14 断言：底栏运行态（mockup run-dark .bottom：状态计数 + ETA + .go 变状态读数）----
    {
        const auto full_text = [](const ElidedLabel *label) {
            if (label == nullptr)
                return QString();
            const QVariant full = label->property("ppFullText");
            return full.isValid() ? full.toString() : label->text();
        };
        const QString bottom = full_text(status);
        const QString eta = full_text(output_status);
        const QString go = start != nullptr ? start->text() : QString();
        std::printf("UI-SMOKE run-bottom: status=\"%s\" eta=\"%s\" go=\"%s\"\n",
                    qUtf8Printable(bottom), qUtf8Printable(eta), qUtf8Printable(go));
        std::fflush(stdout);
        if (!bottom.contains(QStringLiteral("进行中")) ||
            !bottom.contains(QStringLiteral("排队")) || !eta.startsWith(QStringLiteral("ETA")) ||
            !go.startsWith(QStringLiteral("运行中"))) {
            smoke_fail(
                MainWindow::tr("底栏运行态不符（status=%1 eta=%2 go=%3）").arg(bottom, eta, go));
        }
    }
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
    QProgressBar *progress = w->findChild<QProgressBar *>(QStringLiteral("pp-run-progress"));
    if (progress == nullptr) {
        smoke_fail(MainWindow::tr("运行页缺少 pp-run-progress"));
    } else if (progress->maximum() <= 0 || progress->value() != progress->maximum()) {
        smoke_fail(
            MainWindow::tr("进度条未满：%1 / %2").arg(progress->value()).arg(progress->maximum()));
    }
    // M4-T14：摘要行（pp-run-summary）+ 结束态逐输出行定稿（产物事实/失败原因）
    if (QLabel *summary_line = w->findChild<QLabel *>(QStringLiteral("pp-run-summary"))) {
        const QString text = summary_line->property("ppFullText").toString();
        std::printf("UI-SMOKE summary: %s\n", qUtf8Printable(text));
        if (text.isEmpty())
            smoke_fail(MainWindow::tr("运行摘要为空（pp-run-summary）"));
    } else {
        smoke_fail(MainWindow::tr("运行页缺少摘要行（pp-run-summary）"));
    }
    if (QLabel *run_pill = w->findChild<QLabel *>(QStringLiteral("pp-run-state-pill"))) {
        std::printf("UI-SMOKE run state: %s\n",
                    qUtf8Printable(run_pill->property("ppFullText").toString()));
    }
    {
        // 终态逐输出行：每行 bar 已结算（done/failed/skipped）且满格；完成行给出产物事实
        // （字节数 + 压缩比 ×），失败行给出原因 —— 数据源 = 引擎的 OutputResult（§3.4）。
        const QList<QWidget *> bars = w->findChildren<QWidget *>(
            QRegularExpression(QStringLiteral("^pp-run-bar-\\d+-\\d+$")));
        int settled = 0, done_rows = 0, failed_rows = 0, with_bytes = 0, with_ratio = 0;
        QString first_failure;
        for (QWidget *bar : bars) {
            const QString kind = bar->property("ppBarKind").toString();
            if (kind != QLatin1String("pending") && kind != QLatin1String("active"))
                ++settled;
            if (kind == QLatin1String("done"))
                ++done_rows;
            if (kind == QLatin1String("failed"))
                ++failed_rows;
            const QString name = bar->objectName();
            const QString suffix = name.mid(int(QStringLiteral("pp-run-bar-").size()));
            if (QLabel *pct = w->findChild<QLabel *>(QStringLiteral("pp-run-pct-") + suffix)) {
                const QString text = pct->property("ppFullText").toString();
                if (text.endsWith(QLatin1String("KB")) || text.endsWith(QLatin1String("MB")))
                    ++with_bytes;
            }
            if (QLabel *stage = w->findChild<QLabel *>(QStringLiteral("pp-run-stage-") + suffix)) {
                const QString text = stage->property("ppFullText").toString();
                if (text.contains(QStringLiteral("×")))
                    ++with_ratio;
                if (kind == QLatin1String("failed") && first_failure.isEmpty())
                    first_failure = text;
            }
        }
        std::printf("UI-SMOKE run-rows-final: total=%d settled=%d done=%d failed=%d bytes=%d "
                    "ratio=%d first-failure=\"%s\"\n",
                    bars.size(), settled, done_rows, failed_rows, with_bytes, with_ratio,
                    qUtf8Printable(first_failure));
        std::fflush(stdout);
        if (settled != bars.size() || bars.isEmpty())
            smoke_fail(
                MainWindow::tr("结束态仍有未结算输出行（%1/%2）").arg(settled).arg(bars.size()));
        if (done_rows > 0 && (with_bytes == 0 || with_ratio == 0))
            smoke_fail(MainWindow::tr("完成行未显示产物事实（字节=%1 压缩比=%2）")
                           .arg(with_bytes)
                           .arg(with_ratio));
        if (failed_rows > 0 && first_failure.isEmpty())
            smoke_fail(MainWindow::tr("失败行未直显原因"));
    }
    // 日志尾卡（§9.3）：单一持有者 = MainWindow 读 run-*.log 末尾、页只渲染。**--ui-smoke 分支
    // 不写磁盘日志**（main.cpp 冒烟分支只走 stderr，§4.3）→ 这里如实打印实时读到的内容（空 =
    // 页面占位文案「（尚无运行日志）」）；**填充态**由 smoke_probe_run 的独立页探针取证（03d）。
    if (QLabel *log_tail = w->findChild<QLabel *>(QStringLiteral("pp-run-log-lines"))) {
        const QString shown = log_tail->text();
        QString flat = shown.left(200);
        flat.replace(QLatin1Char('\n'), QLatin1String(" | "));
        std::printf("UI-SMOKE run-log-tail: %s\n",
                    flat.isEmpty() ? "(empty)" : qUtf8Printable(flat));
        std::fflush(stdout);
    } else {
        smoke_fail(MainWindow::tr("运行页缺少日志尾控件（pp-run-log-lines）"));
    }

    // ---- M4-T14：运行页自检（独立页实例：逐输出行五态 / 真实 vs 斜纹像素取证 / 取消态 /
    // 浅色）----
    smoke_probe_run(shots_dir);

    // ---- 对话框三连（构造 → show → processEvents → grab → close，绝不 exec）----
    { // 04-settings.png：设置对话框
        SettingsDialog dlg(settings, w);
        dlg.show();
        pump(250);
        smoke_grab(shots_dir, kSmokeShots[kShotSettings], &dlg);
        // ---- M4-T15（§9.3 设置追加项）：控件 → AppSettings 读回自检 ----
        // 不新增截图、不改冻结末行。两侧都验：未 accept（非确定路径）= 构造快照原样返回
        // （§2.10）；accept 后 = 控件值，且"分文件夹默认结构"写回 output_template +
        // split_by_format 一对（§3.6 两字段）。
        {
            const pp::AppSettings snapshot = dlg.settings();
            auto *stagger = dlg.findChild<QSpinBox *>(QStringLiteral("pp-stagger-ms"));
            auto *threads = dlg.findChild<QSpinBox *>(QStringLiteral("pp-thread-budget"));
            auto *structure = dlg.findChild<QComboBox *>(QStringLiteral("pp-folder-structure"));
            if (stagger == nullptr || threads == nullptr || structure == nullptr) {
                smoke_fail(MainWindow::tr("设置对话框缺少 T15 追加项控件（pp-stagger-ms / "
                                          "pp-thread-budget / pp-folder-structure）"));
            } else {
                stagger->setValue(250);
                threads->setValue(5);
                const int idx = structure->findData(QStringLiteral("$dir/$format/$file"));
                if (idx >= 0) {
                    structure->setCurrentIndex(idx);
                }
                dlg.accept();
                const pp::AppSettings edited = dlg.settings();
                const bool ok = snapshot.stagger_ms == settings.stagger_ms &&
                                snapshot.thread_budget == settings.thread_budget &&
                                snapshot.output_template == settings.output_template &&
                                snapshot.split_by_format == settings.split_by_format &&
                                edited.stagger_ms == 250 && edited.thread_budget == 5 &&
                                edited.output_template == "$dir/$format/$file" &&
                                edited.split_by_format;
                std::printf("UI-SMOKE settings-dialog: stagger=%d threads=%d tmpl=%s split=%d "
                            "(snapshot stagger=%d threads=%d tmpl=%s)\n",
                            edited.stagger_ms, edited.thread_budget, edited.output_template.c_str(),
                            edited.split_by_format ? 1 : 0, snapshot.stagger_ms,
                            snapshot.thread_budget, snapshot.output_template.c_str());
                std::fflush(stdout);
                if (!ok) {
                    smoke_fail(MainWindow::tr("设置追加项读回不符（stagger=%1 threads=%2 tmpl=%3 "
                                              "split=%4）")
                                   .arg(edited.stagger_ms)
                                   .arg(edited.thread_budget)
                                   .arg(QString::fromStdString(edited.output_template))
                                   .arg(edited.split_by_format ? 1 : 0));
                }
            }
        }
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

    // ---- M4-T11：分类面板自检（放在截图之后：自检会临时改写注册表并在结束时还原）----
    smoke_probe_classify();

    // ---- M4-T12：主窗口接线面的活体断言 —— 无选中 → 首个文件 + 「(1/N)」（§5.2）----
    //   （分类自检收尾已清空选中；此处只读页面钩子，不改列表/选中）
    if (!model->empty()) {
        sync_selection();
        pump(150);
        const int follow_count = page_meta->property("pp_meta_follow_count").toInt();
        const QString follow_name = page_meta->property("pp_meta_follow_name").toString();
        const auto *hint = page_meta->findChild<QLabel *>(QStringLiteral("pp-meta-info-hint"));
        const QString hint_text = hint != nullptr && hint->property("ppFullText").isValid()
                                      ? hint->property("ppFullText").toString()
                                      : (hint != nullptr ? hint->text() : QString());
        const QString expected =
            QStringLiteral("%1 (1/%2)")
                .arg(QFileInfo(QString::fromStdString(model->row(0).entry.src.string())).fileName())
                .arg(model->size());
        std::printf("UI-SMOKE meta-follow-live: count=%d name=%s hint=%s\n", follow_count,
                    qUtf8Printable(QFileInfo(follow_name).fileName()), qUtf8Printable(hint_text));
        std::fflush(stdout);
        if (follow_count != 1 || hint_text != expected) {
            smoke_fail(MainWindow::tr("无选中跟随未回落到首个文件（count=%1 hint=%2 want=%3）")
                           .arg(follow_count)
                           .arg(hint_text, expected));
        }
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

// ---------------------------------------------------------------------------
// M4-T12 元数据页自检（§5.2 生效值显示 / D4 地图单钉 / §9.2 .mod / 多选小表 / 无选中提示）
//   确定性输入 = 仓库金样 `tests/golden/meta/exif_full.jpg`（与 tests/unit/test_effective.cpp
//   同一 fixture：DateTimeOriginal 2024:03:01 10:00:00、GPS 31.2304/121.4737；金值口径见
//   test_effective 的 delta/2h 与 tz/+08-to+09 用例）。
//   核心断言 = **显示值与写入值同源**（"两张皮"检测）：页面显示的生效时间/坐标必须等于
//   `pp::build_plan()`（写路径）在同一份规则下的落盘值。
//   页实例独立（不动主窗口的页面/列表/选中）；地图与本窗口同一条离线口径（探针不发网络请求）。
// ---------------------------------------------------------------------------
MetaSelectionItem MainWindow::Impl::meta_probe_item(const QString &path) {
    MetaSelectionItem item;
    item.path = path;
    // probe 摘要 = 与列表模型同一条 oiio probe 通道（本探针只补"已收集"的那份快照）
    const pp::ProbeOutcome probe = pp::probe_file(std::filesystem::path(path.toStdString()));
    item.info = probe.info;
    item.probe_ok = probe.error.empty();
    item.file_size = QFileInfo(path).size();
    return item;
}

void MainWindow::Impl::smoke_probe_meta(const QString &shots_dir) {
    const QString repo = repo_root();
    const QString fixture =
        repo.isEmpty() ? QString() : repo + QStringLiteral("/tests/golden/meta/exif_full.jpg");
    if (fixture.isEmpty() || !QFileInfo::exists(fixture)) {
        smoke_fail(MainWindow::tr("元数据页自检：金样 fixture 缺失（%1）").arg(fixture));
        return;
    }
    PageMeta page;
    page.set_tokens(tokens);
    page.resize(theme::Metrics::right_width_1440, theme::Metrics::calibrated_h);
    page.show();
    pump(150);
    if (auto *map = page.findChild<pp::map::MapWidget *>()) {
        map->set_offline(true); // 与本窗口 set_offline_maps(true) 同口径（探针不发请求）
    }
    const auto label = [&page](const QString &name) {
        const auto *widget = page.findChild<QLabel *>(name);
        return widget != nullptr ? widget->text() : QString();
    };
    // 语义值：ElidedLabel 把全文挂在 ppFullText 上（显示文本按宽度省略，见 page_meta.cpp）
    const auto full = [&page](const QString &name) {
        const auto *widget = page.findChild<QLabel *>(name);
        if (widget == nullptr) {
            return QString();
        }
        const QVariant value = widget->property("ppFullText");
        return value.isValid() ? value.toString() : widget->text();
    };
    const auto edit_text = [&page](const QString &name) {
        const auto *widget = page.findChild<QLineEdit *>(name);
        return widget != nullptr ? widget->text() : QString();
    };
    const auto placeholder = [&page](const QString &name) {
        const auto *widget = page.findChild<QLineEdit *>(name);
        return widget != nullptr ? widget->placeholderText() : QString();
    };
    const auto wait_probe = [this, &page] {
        return wait_for([&page] { return !page.property("pp_meta_follow_pending").toBool(); },
                        10000);
    };
    // "YYYY:MM:DD hh:mm:ss" → "YYYY-MM-DD hh:mm:ss"（页面的显示口径；与 page_meta.cpp 同式）
    const auto shown_form = [](QString text) {
        if (text.size() >= 10) {
            QString date = text.left(10);
            date.replace(QLatin1Char(':'), QLatin1Char('-'));
            text = date + text.mid(10);
        }
        return text;
    };
    const auto write_side_time = [&fixture](const pp::BatchRules &rules) {
        const pp::SourceMeta src = pp::read_metadata(std::filesystem::path(fixture.toStdString()));
        const pp::MetadataPlan plan = pp::build_plan(src, rules, std::nullopt);
        return QString::fromStdString(pp::effective_datetime(plan.exif, plan.xmp));
    };

    page.set_batch_files(QStringList{fixture});
    page.set_selection(QList<MetaSelectionItem>{meta_probe_item(fixture)}, /*from_selection=*/true);
    if (!wait_probe()) {
        smoke_fail(MainWindow::tr("元数据页自检：跟随项 probe 未在 10s 内回填"));
        return;
    }
    pump(80);

    // ---- (a) 无规则：原值 = 生效值（未修改时两值相同也显示）+ 来源字段标注 ----
    {
        const QString old_shown = full(QStringLiteral("pp-meta-time-old-0"));
        const QString new_shown = full(QStringLiteral("pp-meta-time-new-0"));
        const QString src_shown = full(QStringLiteral("pp-meta-time-src-0"));
        const bool first_field_unmodified =
            !page.findChild<QLabel *>(QStringLiteral("pp-meta-time-new-0"))
                 ->property(theme::kModProperty)
                 .toBool();
        std::printf("UI-SMOKE meta-geometry: page=%dx%d old-w=%d new-w=%d src-w=%d\n", page.width(),
                    page.height(),
                    page.findChild<QLabel *>(QStringLiteral("pp-meta-time-old-0"))->width(),
                    page.findChild<QLabel *>(QStringLiteral("pp-meta-time-new-0"))->width(),
                    page.findChild<QLabel *>(QStringLiteral("pp-meta-time-src-0"))->width());
        std::fflush(stdout);
        std::printf("UI-SMOKE meta-baseline: old=%s new=%s src=%s unmodified=%d\n",
                    qUtf8Printable(old_shown), qUtf8Printable(new_shown), qUtf8Printable(src_shown),
                    first_field_unmodified ? 1 : 0);
        std::fflush(stdout);
        if (old_shown != QStringLiteral("2024-03-01 10:00:00") || new_shown != old_shown ||
            src_shown != QStringLiteral("DateTimeOriginal") || !first_field_unmodified) {
            smoke_fail(MainWindow::tr("生效值基线不符（old=%1 new=%2 src=%3）")
                           .arg(old_shown, new_shown, src_shown));
        }
        // 关键信息卡（只读）：8 个字段**逐格有显示**（空 = 缺陷；"—" = 源里确无该字段）。
        // fixture exif_full.jpg 只带 Artist/DateTimeOriginal/GPS（本文件头注释已列）→
        // 相机/镜头/曝光 三格**合法**为 "—"，其余五格必须是真值。
        const QStringList keys = {
            QStringLiteral("pp-meta-info-time"), QStringLiteral("pp-meta-info-camera"),
            QStringLiteral("pp-meta-info-lens"), QStringLiteral("pp-meta-info-exposure"),
            QStringLiteral("pp-meta-info-size"), QStringLiteral("pp-meta-info-color"),
            QStringLiteral("pp-meta-info-file"), QStringLiteral("pp-meta-info-gps")};
        QStringList values;
        for (const QString &key : keys) {
            values << full(key);
        }
        std::printf("UI-SMOKE meta-info: %s\n", qUtf8Printable(values.join(QStringLiteral(" | "))));
        std::fflush(stdout);
        const std::array<int, 5> must_have = {0, 4, 5, 6, 7}; // 时间/尺寸/色彩/文件/GPS
        for (const int index : must_have) {
            const QString &value = values.at(index);
            if (value.isEmpty() || value == QStringLiteral("—")) {
                smoke_fail(MainWindow::tr("关键信息卡字段缺失（%1）：%2")
                               .arg(keys.at(index), values.join(QStringLiteral(" | "))));
            }
        }
        for (const QString &value : values) {
            if (value.isEmpty()) {
                smoke_fail(MainWindow::tr("关键信息卡有空字段：%1")
                               .arg(values.join(QStringLiteral(" | "))));
                break;
            }
        }
    }

    // ---- (b) Δ+2h：显示生效值 == build_plan 写入值 == test_effective 金值 ----
    {
        pp::BatchRules rules;
        pp::TimeShift shift;
        shift.mode = pp::TimeShift::Mode::Delta;
        shift.hours = 2;
        rules.time_shift = shift;
        page.apply_rules(rules);
        pump(80);
        const QString shown = full(QStringLiteral("pp-meta-time-new-0"));
        const QString written = shown_form(write_side_time(rules));
        const bool spin_mod = page.findChild<QSpinBox *>(QStringLiteral("pp-meta-delta-hours"))
                                  ->property(theme::kModProperty)
                                  .toBool();
        const bool idle_spin_mod = page.findChild<QSpinBox *>(QStringLiteral("pp-meta-delta-years"))
                                       ->property(theme::kModProperty)
                                       .toBool();
        const bool new_mod = page.findChild<QLabel *>(QStringLiteral("pp-meta-time-new-0"))
                                 ->property(theme::kModProperty)
                                 .toBool();
        std::printf("UI-SMOKE meta-effective: shown=%s write=%s golden=2024-03-01 12:00:00 "
                    "mod-hours=%d mod-years=%d mod-new=%d\n",
                    qUtf8Printable(shown), qUtf8Printable(written), spin_mod ? 1 : 0,
                    idle_spin_mod ? 1 : 0, new_mod ? 1 : 0);
        std::fflush(stdout);
        if (shown != QStringLiteral("2024-03-01 12:00:00") || shown != written || !spin_mod ||
            idle_spin_mod || !new_mod) {
            smoke_fail(MainWindow::tr("Δ+2h 生效值不符（shown=%1 write=%2 mod=%3/%4/%5）")
                           .arg(shown, written)
                           .arg(spin_mod ? 1 : 0)
                           .arg(idle_spin_mod ? 1 : 0)
                           .arg(new_mod ? 1 : 0));
        }
        if (!shots_dir.isEmpty()) { // 走查图：.mod（改动值 = 强调边框+强调色字+700）
            QDir().mkpath(shots_dir);
            const QString path = QDir(shots_dir).filePath(QStringLiteral("01c-meta-mod.png"));
            const bool saved = page.grab().save(path, "PNG");
            std::printf("UI-SMOKE meta-mod-shot: %s saved=%d\n", qUtf8Printable(path),
                        saved ? 1 : 0);
            std::fflush(stdout);
        }
    }

    // ---- (c) 时区语义（+08:00 → +09:00）：墙钟 +1h，同样与写入值对拍 ----
    {
        pp::BatchRules rules;
        pp::TimeShift shift;
        shift.mode = pp::TimeShift::Mode::TimezoneSemantic;
        shift.from_offset_min = 480;
        shift.to_offset_min = 540;
        rules.time_shift = shift;
        page.apply_rules(rules);
        pump(80);
        const QString shown = full(QStringLiteral("pp-meta-time-new-0"));
        const QString written = shown_form(write_side_time(rules));
        std::printf("UI-SMOKE meta-timezone: shown=%s write=%s golden=2024-03-01 11:00:00\n",
                    qUtf8Printable(shown), qUtf8Printable(written));
        std::fflush(stdout);
        if (shown != QStringLiteral("2024-03-01 11:00:00") || shown != written) {
            smoke_fail(
                MainWindow::tr("时区语义生效值不符（shown=%1 write=%2）").arg(shown, written));
        }
    }

    // ---- (d) GPS 规则：生效坐标显示 + 地图 center_on 生效坐标 + 单钉（D4）----
    const double pick_lat = 22.5431;
    const double pick_lon = 114.0579;
    {
        pp::BatchRules rules;
        pp::GpsData gps;
        gps.lat = pick_lat;
        gps.lon = pick_lon;
        rules.gps = gps;
        page.apply_rules(rules);
        pump(120);
        auto *map = page.findChild<pp::map::MapWidget *>();
        const QString lat_text = edit_text(QStringLiteral("pp-meta-gps-lat"));
        const QString lon_text = edit_text(QStringLiteral("pp-meta-gps-lon"));
        const bool marker = map != nullptr && map->has_marker();
        const double center_lat = map != nullptr ? map->center_lat() : 0.0;
        const double center_lon = map != nullptr ? map->center_lon() : 0.0;
        const bool centered =
            std::fabs(center_lat - pick_lat) < 1e-3 && std::fabs(center_lon - pick_lon) < 1e-3;
        const bool mod = page.findChild<QLineEdit *>(QStringLiteral("pp-meta-gps-lat"))
                             ->property(theme::kModProperty)
                             .toBool();
        std::printf("UI-SMOKE meta-gps: lat=%s lon=%s marker=%d center=%.6f,%.6f centered=%d "
                    "mod=%d\n",
                    qUtf8Printable(lat_text), qUtf8Printable(lon_text), marker ? 1 : 0, center_lat,
                    center_lon, centered ? 1 : 0, mod ? 1 : 0);
        std::fflush(stdout);
        if (lat_text != QStringLiteral("22.543100°") || lon_text != QStringLiteral("114.057900°") ||
            !marker || !centered || !mod) {
            smoke_fail(MainWindow::tr("GPS 生效值/地图跟随不符（lat=%1 lon=%2 marker=%3 "
                                      "centered=%4）")
                           .arg(lat_text, lon_text)
                           .arg(marker ? 1 : 0)
                           .arg(centered ? 1 : 0));
        }
    }

    // ---- (e) 隐私剥除：时间/GPS 生效值 → 「将被移除」（§5.1）----
    {
        pp::BatchRules rules;
        pp::TimeShift shift;
        shift.mode = pp::TimeShift::Mode::Delta;
        shift.hours = 2;
        rules.time_shift = shift;
        rules.strip_privacy = true;
        page.apply_rules(rules);
        pump(80);
        const QString time_text = full(QStringLiteral("pp-meta-time-new-0"));
        const QString gps_lat = edit_text(QStringLiteral("pp-meta-gps-lat"));
        const QString gps_hint = placeholder(QStringLiteral("pp-meta-gps-lat"));
        std::printf("UI-SMOKE meta-strip: time=%s gps-value=%s gps-placeholder=%s\n",
                    qUtf8Printable(time_text), qUtf8Printable(gps_lat), qUtf8Printable(gps_hint));
        std::fflush(stdout);
        if (time_text != QStringLiteral("将被移除") || !gps_lat.isEmpty() ||
            gps_hint != QStringLiteral("将被移除")) {
            smoke_fail(MainWindow::tr("隐私剥除的生效值显示不符（time=%1 gps=%2/%3）")
                           .arg(time_text, gps_lat, gps_hint));
        }
    }

    // ---- (f) 清除 GPS：生效坐标空 + 「将清除 GPS」+ 钉清除，且**视口保持不动**（D4/§5.2）----
    {
        pp::BatchRules rules;
        rules.gps_clear = true;
        auto *map = page.findChild<pp::map::MapWidget *>();
        const double center_before_lat = map != nullptr ? map->center_lat() : 0.0;
        const double center_before_lon = map != nullptr ? map->center_lon() : 0.0;
        page.apply_rules(rules);
        pump(120);
        const QString gps_lat = edit_text(QStringLiteral("pp-meta-gps-lat"));
        const QString gps_hint = placeholder(QStringLiteral("pp-meta-gps-lat"));
        const bool marker = map != nullptr && map->has_marker();
        const bool kept = map != nullptr &&
                          std::fabs(map->center_lat() - center_before_lat) < 1e-9 &&
                          std::fabs(map->center_lon() - center_before_lon) < 1e-9;
        std::printf("UI-SMOKE meta-gps-clear: value=%s placeholder=%s marker=%d center-kept=%d\n",
                    qUtf8Printable(gps_lat), qUtf8Printable(gps_hint), marker ? 1 : 0,
                    kept ? 1 : 0);
        std::fflush(stdout);
        if (!gps_lat.isEmpty() || gps_hint != QStringLiteral("将清除 GPS") || marker || !kept) {
            smoke_fail(MainWindow::tr("清除 GPS 的生效值显示/地图语义不符（value=%1 ph=%2 "
                                      "marker=%3 kept=%4）")
                           .arg(gps_lat, gps_hint)
                           .arg(marker ? 1 : 0)
                           .arg(kept ? 1 : 0));
        }
    }

    // ---- (g) 多选（12 选）→ 逐行小表：上限 8 行 + 「…还有 4 个」（§5.2）----
    {
        QStringList paths;
        const QString meta_dir = repo + QStringLiteral("/tests/golden/meta");
        const QString base_dir = repo + QStringLiteral("/tests/golden/base");
        for (const QString &dir : {meta_dir, base_dir}) {
            const QDir d(dir);
            for (const QString &name : d.entryList(QDir::Files, QDir::Name)) {
                if (name.endsWith(QStringLiteral(".png")) ||
                    name.endsWith(QStringLiteral(".jpg")) ||
                    name.endsWith(QStringLiteral(".tif")) ||
                    name.endsWith(QStringLiteral(".webp")) ||
                    name.endsWith(QStringLiteral(".jxl")) ||
                    name.endsWith(QStringLiteral(".avif")) ||
                    name.endsWith(QStringLiteral(".heic"))) {
                    paths << d.filePath(name);
                }
            }
        }
        if (paths.size() < 12) {
            smoke_fail(MainWindow::tr("多选自检语料不足（%1 < 12）").arg(paths.size()));
        } else {
            const QStringList batch = paths.mid(0, 12);
            QList<MetaSelectionItem> items;
            for (const QString &path : batch) {
                items.append(meta_probe_item(path));
            }
            page.set_batch_files(batch);
            page.set_selection(items, /*from_selection=*/true);
            if (!wait_probe()) {
                smoke_fail(MainWindow::tr("元数据页自检：多选 probe 未在 10s 内回填"));
            }
            pump(120);
            const int rows = page.property("pp_meta_multi_rows").toInt();
            const QString more = full(QStringLiteral("pp-meta-multi-more"));
            const QString row0_name = full(QStringLiteral("pp-meta-multi-name-0"));
            const QString row0_old = full(QStringLiteral("pp-meta-multi-old-0"));
            const QString row0_new = full(QStringLiteral("pp-meta-multi-new-0"));
            const bool single_rows_hidden = full(QStringLiteral("pp-meta-time-old-0")).isEmpty();
            std::printf("UI-SMOKE meta-multi: rows=%d more=%s row0=%s | %s -> %s "
                        "single-hidden=%d\n",
                        rows, qUtf8Printable(more), qUtf8Printable(row0_name),
                        qUtf8Printable(row0_old), qUtf8Printable(row0_new),
                        single_rows_hidden ? 1 : 0);
            std::fflush(stdout);
            if (rows != 8 || more != QStringLiteral("…还有 4 个") ||
                row0_name != QFileInfo(batch.first()).fileName()) {
                smoke_fail(MainWindow::tr("多选小表不符（rows=%1 more=%2 row0=%3）")
                               .arg(rows)
                               .arg(more, row0_name));
            }
            if (!shots_dir.isEmpty()) { // 走查图：多选小表（12 选 → 8 行 + 「…还有 4 个」）
                QDir().mkpath(shots_dir);
                const QString path = QDir(shots_dir).filePath(QStringLiteral("01b-meta-multi.png"));
                const bool saved = page.grab().save(path, "PNG");
                std::printf("UI-SMOKE meta-multi-shot: %s saved=%d\n", qUtf8Printable(path),
                            saved ? 1 : 0);
                std::fflush(stdout);
                if (!saved) {
                    smoke_fail(MainWindow::tr("多选小表走查图保存失败：%1").arg(path));
                }
            }
        }
    }

    // ---- (g2) EXIF 编辑器：例外生效值回显 + .mod（§9.2；T12 step 8）----
    {
        pp::BatchRules rules;
        pp::TimeShift shift;
        shift.mode = pp::TimeShift::Mode::Delta;
        shift.hours = 2;
        rules.time_shift = shift;
        pp::GpsData gps;
        gps.lat = pick_lat;
        gps.lon = pick_lon;
        rules.gps = gps;
        ExifEditor editor(fixture, rules, std::nullopt, w);
        editor.setProperty("pp_exif_editor_suppress_modal", true);
        editor.show();
        pump(300);
        int tab_index = -1;
        if (auto *tabs = editor.findChild<QTabWidget *>(QStringLiteral("tabs"))) {
            tab_index = tabs->count() - 1; // 末页 = 时间 / GPS（与 make_time_gps_tab 同序）
            tabs->setCurrentIndex(tab_index);
        }
        pump(200);
        const auto *time_label = editor.findChild<QLabel *>(QStringLiteral("time_effective"));
        const auto *gps_label = editor.findChild<QLabel *>(QStringLiteral("gps_effective"));
        const QString time_text = time_label != nullptr ? time_label->text() : QString();
        const QString gps_text = gps_label != nullptr ? gps_label->text() : QString();
        const bool time_mod =
            time_label != nullptr && time_label->property(theme::kModProperty).toBool();
        const bool gps_mod =
            gps_label != nullptr && gps_label->property(theme::kModProperty).toBool();
        std::printf("UI-SMOKE meta-editor: tab=%d time=%s mod=%d gps=%s mod=%d\n", tab_index,
                    qUtf8Printable(time_text), time_mod ? 1 : 0, qUtf8Printable(gps_text),
                    gps_mod ? 1 : 0);
        std::fflush(stdout);
        if (time_text != QStringLiteral("生效：2024-03-01 12:00:00") ||
            gps_text != QStringLiteral("生效：22.543100, 114.057900") || !time_mod || !gps_mod) {
            smoke_fail(MainWindow::tr("EXIF 编辑器生效值回显不符（time=%1 gps=%2 mod=%3/%4）")
                           .arg(time_text, gps_text)
                           .arg(time_mod ? 1 : 0)
                           .arg(gps_mod ? 1 : 0));
        }
        if (!shots_dir.isEmpty()) { // 走查图（不占 8 张冻结截图的计数）
            QDir().mkpath(shots_dir);
            const QString path =
                QDir(shots_dir).filePath(QStringLiteral("05b-exif-editor-effective.png"));
            const bool saved = editor.grab().save(path, "PNG");
            std::printf("UI-SMOKE meta-editor-shot: %s saved=%d\n", qUtf8Printable(path),
                        saved ? 1 : 0);
            std::fflush(stdout);
            if (!saved) {
                smoke_fail(MainWindow::tr("EXIF 编辑器走查图保存失败：%1").arg(path));
            }
        }
        editor.reject();
        pump(120);
    }

    // ---- (h) 无选中 → 首个文件 + 「(1/N)」提示（§5.2）----
    {
        const QStringList batch{fixture, repo + QStringLiteral("/tests/golden/base/rgb8.png"),
                                repo + QStringLiteral("/tests/golden/base/photo.jpg")};
        page.set_batch_files(batch);
        page.set_selection(QList<MetaSelectionItem>{meta_probe_item(batch.first())},
                           /*from_selection=*/false);
        if (!wait_probe()) {
            smoke_fail(MainWindow::tr("元数据页自检：无选中回退的 probe 未回填"));
        }
        pump(80);
        const QString hint = full(QStringLiteral("pp-meta-info-hint"));
        std::printf("UI-SMOKE meta-follow-fallback: hint=%s\n", qUtf8Printable(hint));
        std::fflush(stdout);
        const QString expected =
            QStringLiteral("%1 (1/%2)").arg(QFileInfo(fixture).fileName()).arg(batch.size());
        if (hint != expected) {
            smoke_fail(MainWindow::tr("无选中跟随提示不符（got=%1 want=%2）").arg(hint, expected));
        }
    }

    page.hide();
    pump(80);
}

} // namespace pp::ui
