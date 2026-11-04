// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W2-T10 落地 / M5-T6 v6 化）— 常驻输入预览面板实现（落地口径见
// ui/preview_panel.h）
//
// 三段结构（自上而下）：
//   ① 舞台 PreviewView（#pp-preview-stage 的底/边框/圆角由 theme QSS 提供 = §4.1 画布底
//      #16181d / §4.3 画布 r4）：画当前 QImage（适应 = 等比完整显示 / 1:1 = 原始像素 / 阶梯比例档，
//      后两者可拖拽平移）+ 左上徽标（有图才显）+ 右下缩放控件组（− | 100% | ＋ | 适应 | 1:1）+
//      左下操作提示 + 空态（矢量图标 + 引导语，见 §8）；
//   ② 底条（= mockup .pv-bar）：◀ ▶ 翻图 ·「n / N」· 文件名 · 右侧热键提示表（随注册表动态生成，
//      无文件/运行期只读同档禁用）；
//   ③ 异步池（= §6.1「独立低并发（≤2）后台线程池」）：≤2 jthread；队列只保留最新请求，过期请求
//      **不解码**、过期结果**不投递**（快速翻图不堆积）；LRU 命中在池线程内瞬时返回。
//
// 全部字面量（尺寸/文案/色值）来源：docs/mockups-v2/tokens.css 的 .canvas/.zoombar 规则 + v1
// docs/mockups/meta-dark.html 的 .pv-bar 规则（§9.4：HTML = 精确规格）与 ui/theme.h 的 tokens；
// 舞台内元素（深色舞台上的徽标/缩放条/提示/空态）是**主题无关**固定值，单列常量并注明出处。

#include "ui/preview_panel.h"

#include <QAbstractItemView> // R-314：←/→ 在列表焦点下让位（分组节键盘折叠）
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication> // 0.4.2 需求5：显示 profile 获取 + 平台判定（offscreen 回退 sRGB）
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QOperatingSystemVersion> // task-4 §3：启动日志记录 OS build（Qt 缓存值，无新系统调用）
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QResizeEvent>
#include <QScreen> // 屏幕变更诊断日志（主屏名）
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QTextEdit>
#include <QTimer> // task-4：GUI 线程轮询后台显示 profile 获取结果
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "core/display_profile.h" // 0.4.2 需求5：显示面色彩管理（真实色彩，需求5）
#include "core/logger.h"          // R-278：出图诊断走 logger（原 std::printf 绕过日志面）
#include "ui/formkit.h" // A17/T11：打标 chip = formkit::ChipButton（§6.1 批量快捷打标唯一形态）
#include "ui/icons.h"   // M5-T3：翻图钮/缩放 chip 的图标一律矢量绘制（C3）
#include "ui/theme.h"   // 只在实现里引（头文件前置声明 theme::Tokens 的原因见 preview_panel.h）

namespace pp::ui {
namespace {

// ---- mockup .pv-* / .canvas / .zoombar 的尺寸字面量（HTML/CSS = 精确规格）----
constexpr int kStageMarginX =
    10;                       // .pv-stage{margin:0 10px}（v1）/ .card-b{padding:8px}（v2 画布四周）
constexpr int kBarPadTop = 8; // .pv-bar{padding:8px 12px 10px}
constexpr int kBarPadX = 12;
constexpr int kBarPadBottom = 10;
constexpr int kBarGap = 10; // .pv-bar{gap:10px}
// 翻图钮（修 1-21「≥28px」）：R4 图标钮 28×28 r4（v1 .nav-arrow 的 26×26 r5 作废 —— 28 是
// 规范档，取 theme::Metrics::icon_btn / icon_btn_radius，不另立取值）
constexpr int kNavIconPx = 12; // 翻图钮图标边长（v1 .pv-bar 内联 svg 的 12×12 盒）
// 缩放控件组（v2 .zoombar）：容器 r4 + 1px 描边 + 深色半透明底；钮 28×28（§8 K5 的缩放条 +
// R4 图标钮档）、组内间距 1、容器左右内距 4 / 上下 2（26px 容器 - 22px 钮 ⇒ 2 × 2 上下内距）、
// 分隔线 1×13、比例读数内距 5。
constexpr int kZoomBarPadX = 4; // .zoombar{padding:0 4px}
constexpr int kZoomBarPadY = 2;
constexpr int kZoomBarGap = 1;                       // .zoombar{gap:1px}
constexpr int kZoomBarRadius = 4;                    // .zoombar{border-radius:4px}
constexpr int kZoomBtnPx = theme::Metrics::icon_btn; // 缩放条各钮 28×28（R4 图标钮）
// 钮圆角 = R4 图标钮档 r4（theme::Metrics::icon_btn_radius）：v2 .zoombar button 的 3px 作废
// （A11 口径 —— §5 R4 是规则层，优先于 tokens.css 的量化值；悬停面显底时的圆角因此与全局面
// 图标钮同档）
constexpr int kZoomIconPx = 14; // 28px 图标钮内的图标边长（v2 .icon-btn 档；.zoombar 未给图标尺寸）
// A39 ④：「适应 / 1:1」= 文字钮的左右内距（v2 .zoombar 的两颗模式钮 `padding:0 6px`）
constexpr int kZoomTextBtnPadX = 6;
constexpr int kZoomLevelW = 40;   // 比例读数固定宽（tabular；「100%」10px 约 24px + 2×5 内距）
constexpr int kZoomLevelPadX = 5; // .zoombar .z{padding:0 5px}
constexpr int kZoomSepW = 1;      // .zoombar .sep{width:1px;height:13px;margin:0 2px}
constexpr int kZoomSepH = 13;
constexpr int kZoomSepMarginX = 2;
constexpr int kStageOverlayMargin =
    10;                         // .zoombar{right:10px;bottom:10px} / .canvas .hint{left:10px}
constexpr int kOverlayPadX = 8; // .pv-badge{padding:3px 8px}
constexpr int kOverlayPadY = 3;
constexpr int kOverlayRadius = 4;
// A17（T11）：打标 chip 的 pad/radius 已随控件统一进 formkit（ChipButton 的 kChipPadX/…），
// 本文件只剩「chip 之间与文字链的间距」与「清除文字链的内距」两个局部量。
constexpr int kKeyChipPadX = 6; // 「清除」文字链的左右内距（.key{padding:1px 6px} 的口径）
constexpr int kKeyChipGap = 6;  // .keyhint{gap:6px}
// 空态（§8「预览=深灰舞台 #16181d + 图标 + 引导语」；修 1-16）：图标 32（左栏投放区 26 的舞台档）、
// 图标与引导语 10px 间距（§4.3 令牌 8–12 之间取 8）、引导语 11.5px（§4.2 正文·值）
constexpr int kEmptyIconPx = 32;
constexpr int kEmptyGap = 8;
// 照片衬垫（修 3-21：letterbox 与画布底略作区分 + 图像区边界可见）：围绕图像 6px、色取 v1 舞台底
// #0d0f12（比画布底 #16181d 略深一档）、1px 浅描边勾出照片区轮廓、r2。
constexpr int kPhotoMatPad = 6;
constexpr int kPhotoMatRadius = 2;
// 舞台内元素是**主题无关**的深色舞台配色（mockup 固定值；舞台底 #16181d = tokens.stage_bg）。
// UX-6⑦（M7-WP4）：写死色全部收编 theme.h 的 **overlay 令牌组**（覆盖层压在照片上，不随主题
// 变 —— 值逐字不变，取值单源在 theme::overlay_tokens()）；下方别名只作本页短名转发。
const char *const kOverlayFg = theme::overlay_tokens().pv_fg;
const char *const kOverlayBg = theme::overlay_tokens().pv_bg;
const char *const kOverlayBd = theme::overlay_tokens().pv_bd;
const char *const kZoomBarBg = theme::overlay_tokens().zoombar_bg;
const char *const kZoomBarBd = theme::overlay_tokens().zoombar_bd;
const char *const kZoomBarFg = theme::overlay_tokens().zoombar_fg;
const char *const kZoomLevelFg = theme::overlay_tokens().zoom_level_fg;
const char *const kZoomHoverBg = theme::overlay_tokens().zoom_hover_bg;
const char *const kZoomActiveBg = theme::overlay_tokens().zoom_active_bg;
const char *const kZoomSepBg = theme::overlay_tokens().zoom_sep_bg;
const char *const kStageHintFg = theme::overlay_tokens().stage_hint_fg;
const char *const kEmptyIconFg = theme::overlay_tokens().empty_icon_fg;
const char *const kEmptyTextFg = theme::overlay_tokens().empty_text_fg;
const char *const kPhotoMatBg = theme::overlay_tokens().photo_mat_bg;
// 衬垫/空态图标的 QColor 侧（QColor 不解析 CSS 的 rgba() 字面量，故与 QSS 串并列单列）
const QColor &kPhotoMatBdColor = theme::overlay_tokens().photo_mat_bd;
const QColor &kEmptyIconColor = theme::overlay_tokens().empty_icon;
// 缩放模式钮的「当前档」动态属性钩子（ppZoomActive）：**不是 checked**（修 1-19：缩放模式/比例
// 切换由普通按钮承担，无勾选语义）；QSS 只按属性值换底色。
constexpr const char *kZoomActiveProperty = theme::kZoomActiveProperty; // UX-6⑦：单源在 theme.h
// 缩放阶梯（§8 缩放条「− | 100% | ＋」；滚轮/± 钮在同一张梯子上步进 —— 100 档 = 1:1 同值）
constexpr int kZoomLadder[] = {10, 25, 50, 75, 100, 150, 200, 400, 800};
constexpr int kZoomLadderCount = int(sizeof(kZoomLadder) / sizeof(kZoomLadder[0]));

QString T(const char *s) { return QCoreApplication::translate("PreviewPanel", s); }

// —— 0.4.2 启动卡死根治（task-4）：显示 profile 异步获取的 GUI 侧参数 ——
// 轮询间隔：后台完成后 GUI 线程 ≤50ms 内应用（QTimer 驱动，一次轮询只做 mutex + 纯函数判据，
// 微秒级；不忙等）。超时门限 = 5s：到点仍无结果 → 记 warn 并保持 sRGB；之后为「软超时」观察
// 窗（GUI 从不等待；迟到的系统 profile 在 30s 硬上限内仍会异步应用，避免用户永远停在 sRGB）。
constexpr int kDisplayProbePollMs = 50;
constexpr long long kDisplayProbeTimeoutMs = 5000;
constexpr long long kDisplayProbeHardCapMs = 30000;

// 显示 profile 语义比较（与 PreviewCache::set_display_profile 同一判据：ICC 字节 + 来源 +
// 描述名）。相同 = 无需清缓存/重取当前图。
bool same_display_profile(const pp::DisplayProfile &a, const pp::DisplayProfile &b) {
    return a.icc == b.icc && a.from_system == b.from_system && a.name == b.name;
}

// 启动日志口径（task-4 §3）：Windows 版本差异是本次卡死反馈的关键变量，故启动日志如实记录
// OS build / Qt 版本 / 平台。QOperatingSystemVersion 是 Qt 启动时缓存的值（Qt 自己调用过
// RtlGetVersion），这里**不引入新的系统调用**、不阻塞。
std::string os_version_build_text() {
    const QOperatingSystemVersion v = QOperatingSystemVersion::current();
    return std::to_string(v.majorVersion()) + "." + std::to_string(v.minorVersion()) + "." +
           std::to_string(v.microVersion());
}

// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」）。
// minimumSizeHint 宽压到 0：超长文件名/提示**省略**而不是把中栏撑宽（三栏 1440 校准尺寸
// 258/537/601 不得被中栏内容的最小宽顶开）。
// NOTE：mainwindow.cpp 的同名助手在匿名 namespace 内、跨 TU 不可见；抽出公共头会动清单外文件面，
//       故本面板自带一份等价的小实现。
// R-320（M7-WP4）：第 7 份拷贝收敛到 formkit::ElidedLabel —— 本面板特有的
// "minimumSizeHint 宽压到 0"（超长文件名不撑宽中栏）由 squeeze 构造档承载，实现只剩一份。
class ElideLabel : public formkit::ElidedLabel {
public:
    explicit ElideLabel(QWidget *parent = nullptr) : formkit::ElidedLabel(parent, true) {}
};

// 热键提示里的 chip / 名称（§9.3 禁折行）：自然宽度用 sizeHint，但允许被压缩到 0
// （窄栏时按 mockup 的 flex-shrink 语义裁剪，不换行、也不把中栏最小宽顶上去）。
class ClipLabel : public QLabel {
public:
    explicit ClipLabel(const QString &text, QWidget *parent) : QLabel(text, parent) {}
    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }
};

// 预览舞台（§8 画布）。画图只做**显示所必需**的缩放/平移（§6.1「不做色彩管理之外的任何处理」
// → 像素不动，只在绘制阶段缩放）：
//   * 适应（contain）：scale = min(view_w/img_w, view_h/img_h) —— 等比**完整显示**（可放大：
//     解码侧不放大，这里的放大只是绘制阶段的事，像素仍为源像素）；
//   * 1:1（Actual）：原始像素居中；阶梯比例档（Step）：percent/100 等比缩放；
//   * 放大档（目标 ≥ 源）默认**最近邻**（修 3-20：64px 小图的方格边缘不再糊化），缩小档平滑；
//   * 图像下衬「照片衬垫」（修 3-21）：letterbox 区与画布底 §4.1 #16181d 略作区分，深色图像的
//     边界也有可见轮廓。
// 悬浮件（徽标 / 缩放控件组 / 操作提示 / 空态块）都是舞台的子件，按舞台矩形自定位
// （layout_overlays；舞台无布局 → 子件自由摆放，和 mockup 的 absolute 同口径）。
class PreviewView : public QFrame {
public:
    // 缩放模式（修 1-19：模式钮 = 普通按钮，非 checkbox 语义；当前档由 100% 读数 + 钮上的
    // ppZoomActive 高亮表达，不引入选中态切换语义）
    enum class ZoomMode { Fit, Actual, Step };

    explicit PreviewView(QWidget *parent = nullptr);

    void set_image(const QImage &image);
    void set_placeholder(const QString &text); // 有文件但出图失败（警示图标 + 文案）
    void set_empty_state();                    // 无文件（§8 空态：矢量图标 + 引导语）
    void set_zoom_fit(bool fit);               // true = 适应 / false = 1:1（既有语义）
    bool zoom_fit() const { return mode_ == ZoomMode::Fit; }
    void set_zoom_percent(int percent); // 阶梯档（夹取到 kZoomLadder）
    int zoom_percent() const;           // 当前有效比例（适应档 = 按舞台实测）
    void zoom_step(int direction, const QPoint *anchor = nullptr); // ±1 档（滚轮/± 钮）
    bool nearest_neighbor() const;                                 // 修 3-20：当前绘制是否走最近邻
    void set_tokens(const theme::Tokens &tokens);
    void set_zoom_handler(std::function<void()> handler) { zoom_handler_ = std::move(handler); }
    QSize displayed_size() const { return image_.isNull() ? QSize() : target_size(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QSize target_size() const;
    QRect target_rect() const;
    QPoint clamp_pan(const QPoint &pan) const;
    double scale_factor() const;
    bool panning_possible() const;
    void apply_zoom_percent(int percent, const QPoint *anchor);
    void layout_overlays();
    void refresh_overlays(); // 读数/模式高亮/悬浮件可见性（状态变化的唯一收敛点）
    void update_empty_icon();
    void notify_zoom_changed();

    QImage image_;
    QString placeholder_; // 失败态文案（empty_ = false 且 image_ 空）
    bool empty_ = true;   // 无文件（§8 空态）
    ZoomMode mode_ = ZoomMode::Fit;
    int percent_ = 100; // Step 档比例
    QPoint pan_;
    bool dragging_ = false;
    QPoint drag_origin_;
    QPoint drag_pan0_;
    theme::Tokens tokens_;
    QLabel *badge_ = nullptr;
    QWidget *zoom_bar_ = nullptr;
    QToolButton *zoom_out_ = nullptr;
    QToolButton *zoom_in_ = nullptr;
    QToolButton *zoom_fit_btn_ = nullptr;
    QToolButton *zoom_actual_btn_ = nullptr;
    QLabel *zoom_level_ = nullptr;
    QLabel *hint_ = nullptr;
    QWidget *empty_box_ = nullptr;
    QLabel *empty_icon_ = nullptr;
    QLabel *empty_text_ = nullptr;
    std::function<void()> zoom_handler_;
};

PreviewView::PreviewView(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("pp-preview-stage")); // theme QSS: QFrame#pp-preview-stage
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(120);

    // 空态（§8「深灰舞台 + 图标 + 引导语」；修 1-16：禁止纯黑空画布）：矢量图标 + 引导语居中。
    // 图标/文案是**子件**（不是 paintEvent 里的文字）—— 探针据此取证「空态有图标」。
    empty_box_ = new QWidget(this);
    empty_box_->setObjectName(QStringLiteral("pp-preview-empty"));
    auto *empty_layout = new QVBoxLayout(empty_box_);
    empty_layout->setContentsMargins(0, 0, 0, 0);
    empty_layout->setSpacing(kEmptyGap);
    empty_layout->addStretch(1);
    empty_icon_ = new QLabel(empty_box_);
    empty_icon_->setObjectName(QStringLiteral("pp-preview-empty-icon"));
    empty_icon_->setFixedSize(kEmptyIconPx, kEmptyIconPx);
    empty_icon_->setAlignment(Qt::AlignCenter);
    empty_icon_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    empty_text_ = new QLabel(empty_box_);
    empty_text_->setObjectName(QStringLiteral("pp-preview-empty-text"));
    empty_text_->setFont(theme::font(theme::Typography::body_px));
    empty_text_->setAlignment(Qt::AlignCenter);
    empty_text_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    empty_layout->addWidget(empty_icon_, 0, Qt::AlignHCenter);
    empty_layout->addWidget(empty_text_, 0);
    empty_layout->addStretch(1);

    // 徽标「原图 · 未应用修改」（修 1-17/1-18：半透明底 + 描边；空态隐藏 → refresh_overlays）
    badge_ = new QLabel(T("原图 · 未应用修改"), this);
    badge_->setObjectName(QStringLiteral("pp-preview-badge"));
    badge_->setFont(theme::font(theme::Typography::hint_px, 500));
    badge_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // 缩放控件组「− | 100% | ＋ | 适应 | 1:1」（§8 K5/W-R-T13）：悬浮右下**单一控件组**
    // （v2 .zoombar）；−/＋ = 矢量图标钮 28×28（R4），插入符/字形一律不出现（C3）；
    // **「适应 / 1:1」= 文字形态**（A39 ④：§8 组件清单逐字给的是文字，不是图标；v2 .zoombar 的
    // 两颗模式钮同样是 `width:auto;padding:0 6px` 的文字钮）—— tooltip 原样保留。
    zoom_bar_ = new QWidget(this);
    zoom_bar_->setObjectName(QStringLiteral("pp-preview-zoombar"));
    zoom_bar_->setAttribute(Qt::WA_StyledBackground, true); // 容器底/描边由 QSS 落（显式属性保险）
    auto *zoom_layout = new QHBoxLayout(zoom_bar_);
    zoom_layout->setContentsMargins(kZoomBarPadX, kZoomBarPadY, kZoomBarPadX, kZoomBarPadY);
    zoom_layout->setSpacing(kZoomBarGap);
    const auto make_zoom_button = [this, zoom_layout](const char *object_name, icons::Name icon,
                                                      const char *tip) {
        auto *button = new QToolButton(zoom_bar_);
        button->setObjectName(QString::fromLatin1(object_name));
        button->setToolTip(T(tip));
        button->setAccessibleName(T(tip)); // §11：图标钮必有 accessibleName + tooltip
        button->setFixedSize(kZoomBtnPx, kZoomBtnPx);
        button->setIconSize(QSize(kZoomIconPx, kZoomIconPx));
        button->setProperty(icons::kIconNameProperty, QString::fromLatin1(icons::key(icon)));
        button->setProperty(kZoomActiveProperty, false);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        zoom_layout->addWidget(button);
        return button;
    };
    // A39 ④：模式钮 = 文字钮（高与图标钮同档 28 → 同一条带内不撑高；宽随文本 + 2×6 内距）。
    const auto make_zoom_text_button = [this, zoom_layout](const char *object_name,
                                                           const char *text, const char *tip) {
        auto *button = new QToolButton(zoom_bar_);
        button->setObjectName(QString::fromLatin1(object_name));
        button->setText(T(text));
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setToolTip(T(tip));
        button->setAccessibleName(T(tip)); // §11：必有 accessibleName + tooltip
        button->setFont(theme::font(theme::Typography::hint_px, QFont::Normal));
        button->setFixedHeight(kZoomBtnPx);
        button->setFixedWidth(QFontMetrics(button->font()).horizontalAdvance(T(text)) +
                              2 * kZoomTextBtnPadX);
        button->setProperty(kZoomActiveProperty, false);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        zoom_layout->addWidget(button);
        return button;
    };
    zoom_out_ = make_zoom_button("pp-preview-zoom-out", icons::Name::ZoomOut, "缩小");
    zoom_level_ = new QLabel(zoom_bar_);
    zoom_level_->setObjectName(QStringLiteral("pp-preview-zoom-level"));
    // 读数 = 10px tabular（§4.2 辅助档 + §4.2「数值」行）；宽度固定 → 比例变化时钮不漂移
    zoom_level_->setFont(theme::font(theme::Typography::hint_px, QFont::Normal, /*tabular=*/true));
    zoom_level_->setAlignment(Qt::AlignCenter);
    zoom_level_->setFixedWidth(kZoomLevelW);
    zoom_level_->setToolTip(T("当前缩放比例（滚轮缩放 · 双击适应）"));
    zoom_level_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    zoom_layout->addWidget(zoom_level_);
    zoom_in_ = make_zoom_button("pp-preview-zoom-in", icons::Name::ZoomIn, "放大");
    {
        // 分隔线（v2 .zoombar .sep）：固定 1×13，上下居中、左右各 2 的间距
        auto *sep_box = new QWidget(zoom_bar_);
        auto *sep_layout = new QHBoxLayout(sep_box);
        sep_layout->setContentsMargins(kZoomSepMarginX, 0, kZoomSepMarginX, 0);
        sep_layout->setSpacing(0);
        auto *sep = new QFrame(sep_box);
        sep->setObjectName(QStringLiteral("pp-preview-zoom-sep"));
        sep->setAttribute(Qt::WA_StyledBackground, true);
        sep->setFixedSize(kZoomSepW, kZoomSepH);
        sep_layout->addWidget(sep, 0, Qt::AlignVCenter);
        zoom_layout->addWidget(sep_box);
    }
    zoom_fit_btn_ = make_zoom_text_button("pp-preview-zoom-fit", "适应", "适应舞台");
    zoom_actual_btn_ = make_zoom_text_button("pp-preview-zoom-1to1", "1:1", "原始像素 (1:1)");
    QObject::connect(zoom_out_, &QToolButton::clicked, this, [this] { zoom_step(-1); });
    QObject::connect(zoom_in_, &QToolButton::clicked, this, [this] { zoom_step(+1); });
    QObject::connect(zoom_fit_btn_, &QToolButton::clicked, this, [this] { set_zoom_fit(true); });
    QObject::connect(zoom_actual_btn_, &QToolButton::clicked, this,
                     [this] { set_zoom_fit(false); });

    // 左下操作提示（§8 逐字：「拖动平移 · 滚轮缩放 · 双击适应」）
    hint_ = new QLabel(T("拖动平移 · 滚轮缩放 · 双击适应"), this);
    hint_->setObjectName(QStringLiteral("pp-preview-hint"));
    hint_->setFont(theme::font(theme::Typography::hint_px));
    hint_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    set_tokens(theme::tokens(theme::system_theme_mode()));
    refresh_overlays();
    layout_overlays();
}

void PreviewView::set_tokens(const theme::Tokens &tokens) {
    tokens_ = tokens;
    // 缩放钮的取色不吃 QSS（QIcon 是位图/矢量渲染色）→ 显式重放（舞台是固定深色，与主题无关）
    const QColor fg(QString::fromLatin1(kZoomBarFg));
    icons::apply(zoom_out_, icons::Name::ZoomOut, fg, fg, kZoomIconPx);
    icons::apply(zoom_in_, icons::Name::ZoomIn, fg, fg, kZoomIconPx);
    // A39 ④：「适应 / 1:1」= 文字钮（取色走 QSS，见 apply_style 的 pp-preview-zoom-fit/1to1 规则）
    update_empty_icon();
    update(); // 舞台内的深色徽标/缩放条配色与主题无关（固定深色舞台），故只重绘
}

void PreviewView::update_empty_icon() {
    if (empty_icon_ == nullptr)
        return;
    // 空态 = 图片图标（邀请语义）；失败态 = 警示图标（同一条「图标 + 文案」的舞台空态语言）
    const icons::Name name = empty_ ? icons::Name::Image : icons::Name::Warn;
    empty_icon_->setPixmap(icons::pixmap(name, kEmptyIconPx, kEmptyIconColor, devicePixelRatioF()));
}

void PreviewView::set_image(const QImage &image) {
    image_ = image;
    empty_ = false;
    placeholder_.clear();
    pan_ = QPoint(0, 0);
    refresh_overlays();
    update();
}

void PreviewView::set_placeholder(const QString &text) {
    image_ = QImage();
    empty_ = false;
    placeholder_ = text;
    pan_ = QPoint(0, 0);
    refresh_overlays();
    update();
}

void PreviewView::set_empty_state() {
    image_ = QImage();
    empty_ = true;
    placeholder_.clear();
    pan_ = QPoint(0, 0);
    refresh_overlays();
    update();
}

void PreviewView::set_zoom_fit(bool fit) {
    const ZoomMode next = fit ? ZoomMode::Fit : ZoomMode::Actual;
    if (mode_ == next)
        return;
    mode_ = next;
    pan_ = QPoint(0, 0); // 切换缩放 → 复位平移
    refresh_overlays();
    update();
    notify_zoom_changed();
}

double PreviewView::scale_factor() const {
    switch (mode_) {
    case ZoomMode::Actual:
        return 1.0;
    case ZoomMode::Step:
        return std::max(0.01, percent_ / 100.0);
    case ZoomMode::Fit:
        break;
    }
    // 适应 = 等比完整显示（contain；放大/缩小都允许；与 mockup .pv-stage 的满幅呈现一致）
    return std::min(double(std::max(1, width())) / image_.width(),
                    double(std::max(1, height())) / image_.height());
}

QSize PreviewView::target_size() const {
    if (image_.isNull())
        return {};
    const double scale = scale_factor();
    return {std::max(1, int(std::lround(image_.width() * scale))),
            std::max(1, int(std::lround(image_.height() * scale)))};
}

bool PreviewView::nearest_neighbor() const {
    if (image_.isNull())
        return false;
    // 修 3-20：目标 ≥ 源（放大档，小图场景）→ 最近邻；缩小档平滑（下采样抗锯齿）
    const QSize target = target_size();
    return target.width() >= image_.width() && target.height() >= image_.height();
}

int PreviewView::zoom_percent() const {
    switch (mode_) {
    case ZoomMode::Actual:
        return 100;
    case ZoomMode::Step:
        return percent_;
    case ZoomMode::Fit:
        break;
    }
    if (image_.isNull())
        return 100;
    return std::max(1, int(std::lround(scale_factor() * 100.0)));
}

void PreviewView::set_zoom_percent(int percent) { apply_zoom_percent(percent, nullptr); }

void PreviewView::zoom_step(int direction, const QPoint *anchor) {
    const int current = zoom_percent();
    int next = current;
    if (direction > 0) {
        for (int i = 0; i < kZoomLadderCount; ++i) {
            if (kZoomLadder[i] > current) { // 梯子上第一个更大档
                next = kZoomLadder[i];
                break;
            }
        }
    } else {
        for (int i = kZoomLadderCount - 1; i >= 0; --i) {
            if (kZoomLadder[i] < current) { // 梯子上第一个更小档
                next = kZoomLadder[i];
                break;
            }
        }
    }
    apply_zoom_percent(next, anchor);
}

void PreviewView::apply_zoom_percent(int percent, const QPoint *anchor) {
    // 夹取到阶梯档（就近取档；10…800）
    percent = std::clamp(percent, kZoomLadder[0], kZoomLadder[kZoomLadderCount - 1]);
    int snapped = kZoomLadder[0];
    for (int i = 0; i < kZoomLadderCount; ++i) {
        snapped = kZoomLadder[i];
        if (kZoomLadder[i] >= percent)
            break;
    }
    if (mode_ == ZoomMode::Step && percent_ == snapped)
        return;
    // 锚点（滚轮下的光标；无锚 = 舞台中心）在图内的相对位置：缩放后该像素仍落在锚点上
    const QPoint focus = anchor != nullptr ? *anchor : QPoint(width() / 2, height() / 2);
    const QRect before_rect = target_rect();
    double rx = 0.5, ry = 0.5;
    if (before_rect.width() > 0 && before_rect.height() > 0) {
        rx = double(focus.x() - before_rect.left()) / double(before_rect.width());
        ry = double(focus.y() - before_rect.top()) / double(before_rect.height());
    }
    mode_ = ZoomMode::Step;
    percent_ = snapped;
    pan_ = QPoint(0, 0);
    const QSize after = target_size();
    const QRect after_rect = target_rect(); // pan = 0 → 居中位
    pan_ = QPoint(focus.x() - int(std::lround(rx * after.width())) - after_rect.left(),
                  focus.y() - int(std::lround(ry * after.height())) - after_rect.top());
    pan_ = clamp_pan(pan_);
    refresh_overlays();
    update();
    notify_zoom_changed();
}

void PreviewView::notify_zoom_changed() {
    if (zoom_handler_)
        zoom_handler_();
}

QPoint PreviewView::clamp_pan(const QPoint &pan) const {
    const QSize target = target_size();
    const int max_x = std::max(0, (target.width() - width()) / 2);
    const int max_y = std::max(0, (target.height() - height()) / 2);
    return {std::clamp(pan.x(), -max_x, max_x), std::clamp(pan.y(), -max_y, max_y)};
}

bool PreviewView::panning_possible() const {
    const QSize target = target_size();
    return !image_.isNull() && (target.width() > width() || target.height() > height());
}

QRect PreviewView::target_rect() const {
    const QSize target = target_size();
    const QPoint offset = clamp_pan(pan_);
    return QRect(QPoint((width() - target.width()) / 2 + offset.x(),
                        (height() - target.height()) / 2 + offset.y()),
                 target);
}

void PreviewView::paintEvent(QPaintEvent *event) {
    QFrame::paintEvent(event); // 先让 theme QSS 画舞台底/边框（WA_StyledBackground）
    if (image_.isNull())
        return; // 空态/失败态 = 空态块子件（矢量图标 + 文案），舞台底由 QSS 提供
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // .pv-stage{overflow:hidden}：衬垫/图像裁在圆角内
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), tokens_.radius_stage,
                        tokens_.radius_stage);
    painter.setClipPath(clip);
    const QRect target = target_rect();
    // 修 3-21：照片衬垫 = 图像四周 kPhotoMatPad 的略深底 + 1px 浅描边 —— letterbox 带与画布底
    // #16181d 略作区分，深色图像的照片区边界也立得住（衬垫被舞台边界裁切：宽受限的图像只在
    // 上下留出衬垫带 —— 正是 3-21 说的那两条 letterbox 黑带）
    const QRect mat = target.adjusted(-kPhotoMatPad, -kPhotoMatPad, kPhotoMatPad, kPhotoMatPad)
                          .intersected(rect().adjusted(1, 1, -1, -1));
    painter.setPen(QPen(kPhotoMatBdColor, 1));
    painter.setBrush(QColor(QString::fromLatin1(kPhotoMatBg)));
    painter.drawRoundedRect(QRectF(mat).adjusted(0.5, 0.5, -0.5, -0.5), kPhotoMatRadius,
                            kPhotoMatRadius);
    // 修 3-20：小图（放大档）默认最近邻；缩小档平滑
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !nearest_neighbor());
    painter.drawImage(target, image_);
}

void PreviewView::resizeEvent(QResizeEvent *event) {
    QFrame::resizeEvent(event);
    pan_ = clamp_pan(pan_);
    layout_overlays();
    refresh_overlays(); // 适应档的比例读数随舞台尺寸变（读数与几何同步）
}

void PreviewView::refresh_overlays() {
    const bool has_image = !image_.isNull();
    if (badge_ != nullptr)
        badge_->setVisible(has_image); // 修 1-17：空态隐藏角标
    if (zoom_bar_ != nullptr)
        zoom_bar_->setVisible(has_image);
    if (hint_ != nullptr)
        hint_->setVisible(has_image); // 无图可拖/可缩 → 提示不占位
    if (empty_box_ != nullptr)
        empty_box_->setVisible(!has_image);
    if (empty_text_ != nullptr)
        empty_text_->setText(empty_ ? T("将照片拖到此处，或点击 ＋ 添加") : placeholder_);
    update_empty_icon();
    if (zoom_level_ != nullptr)
        zoom_level_->setText(QStringLiteral("%1%").arg(zoom_percent()));
    // 模式读数（修 1-19）：钮的当前档 = ppZoomActive 动态属性（非 checked —— 无勾选语义）；
    // 比例档（Step）两枚模式钮都不高亮，读数即状态。
    const auto set_active = [](QToolButton *button, bool on) {
        if (button == nullptr || button->property(kZoomActiveProperty).toBool() == on)
            return;
        button->setProperty(kZoomActiveProperty, on);
        theme::repolish(button);
    };
    set_active(zoom_fit_btn_, has_image && mode_ == ZoomMode::Fit);
    set_active(zoom_actual_btn_, has_image && mode_ == ZoomMode::Actual);
    if (!dragging_)
        setCursor(panning_possible() ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void PreviewView::layout_overlays() {
    if (empty_box_ != nullptr)
        empty_box_->setGeometry(rect()); // 空态块铺满舞台（内层布局居中）
    if (badge_ != nullptr) {
        badge_->adjustSize(); // QSS padding 已计入 sizeHint
        badge_->move(kStageOverlayMargin, kStageOverlayMargin);
        badge_->raise();
    }
    if (zoom_bar_ != nullptr) {
        zoom_bar_->adjustSize();
        const QSize hint = zoom_bar_->size();
        zoom_bar_->move(
            std::max(kStageOverlayMargin, width() - kStageOverlayMargin - hint.width()),
            std::max(kStageOverlayMargin, height() - kStageOverlayMargin - hint.height()));
        zoom_bar_->raise();
    }
    if (hint_ != nullptr) {
        hint_->adjustSize();
        hint_->move(
            kStageOverlayMargin,
            std::max(kStageOverlayMargin, height() - kStageOverlayMargin - hint_->height()));
        hint_->raise();
    }
}

void PreviewView::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !panning_possible()) {
        QFrame::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    drag_origin_ = event->position().toPoint();
    drag_pan0_ = pan_;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PreviewView::mouseMoveEvent(QMouseEvent *event) {
    if (!dragging_) {
        QFrame::mouseMoveEvent(event);
        return;
    }
    pan_ = clamp_pan(drag_pan0_ + (event->position().toPoint() - drag_origin_));
    update();
    event->accept();
}

void PreviewView::mouseReleaseEvent(QMouseEvent *event) {
    if (!dragging_) {
        QFrame::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    setCursor(panning_possible() ? Qt::OpenHandCursor : Qt::ArrowCursor);
    event->accept();
}

void PreviewView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || image_.isNull()) {
        QFrame::mouseDoubleClickEvent(event);
        return;
    }
    set_zoom_fit(true); // §8 提示「双击适应」
    event->accept();
}

void PreviewView::wheelEvent(QWheelEvent *event) {
    const int delta = event->angleDelta().y();
    if (image_.isNull() || delta == 0) {
        QFrame::wheelEvent(event);
        return;
    }
    const QPoint anchor = event->position().toPoint();
    zoom_step(delta > 0 ? +1 : -1, &anchor); // §8 提示「滚轮缩放」（锚点 = 光标下的像素）
    event->accept();
}

} // namespace

// -------------------------------------------------------------------------------------------
// Impl：状态 + 异步池（≤2 线程）+ 键盘路由 + 底条/热键表
// -------------------------------------------------------------------------------------------

struct PreviewPanel::Impl {
    struct LoadJob {
        std::uint64_t token = 0;
        int index = -1;
        QString path;
        std::chrono::steady_clock::time_point request_time;
    };

    // 键盘路由：应用级事件过滤器（见 ui/preview_panel.h 的「键盘路由」说明）。
    // 无 Q_OBJECT（不需要信号槽，只用虚函数 eventFilter）。
    struct Router : QObject {
        Impl *impl = nullptr;
        explicit Router(Impl *owner) : QObject(nullptr), impl(owner) {}
        bool eventFilter(QObject *watched, QEvent *event) override {
            return impl->route_key(watched, event);
        }
    };

    // 异步池：≤2 jthread；队列**只保留最新一条请求**，在途结果按 token 丢弃。
    struct Pool {
        Impl *impl = nullptr;
        std::mutex mu;
        std::condition_variable_any cv;
        std::deque<LoadJob> queue;
        std::uint64_t next_token = 1;
        std::uint64_t latest_token = 0;
        bool stopping = false;
        std::vector<std::jthread> workers;

        void start();
        void stop();
        void submit(int index, const QString &path);
        void worker_loop(std::stop_token st);
    };

    PreviewPanel *q = nullptr;
    theme::Tokens tokens;
    QStringList files; // 列表顺序 = 翻图顺序
    int current = -1;  // 列表行号；-1 = 空列表
    // 缩放状态（模式/比例/平移）**只在舞台里一份**（PreviewView）—— M5-T6：旧的 bool zoom_fit
    // 副本已收敛（单一数据源：面板读 view->zoom_fit()/zoom_percent()，避免两处状态漂移）
    bool locked = false; // G5 运行期只读（锁打标热键）
    // M7-R2D1 / R-278：hotkeys_enabled（set_hotkeys_enabled 的死开关）已随死 API 清理移除。
    const pp::ClassRegistry *classes = nullptr; // 不持有（T11 的注册表实例）

    // 加载状态机：attempted_path = 最近一次已发起的请求路径（幂等去重 + 防失败重试风暴；
    // 需要强制重取时走公开的 reload()）；loaded_path = 最近一次**成功出图**的路径
    // （信息行与「已有图」的判据）；failed_path/failed_error = 最近一次**失败**的请求
    // （R-277：信息行据此显「无法预览：…」，不再恒显「载入中…」）。
    QString attempted_path;
    QString loaded_path;
    QString info_text;
    QString failed_path;
    QString failed_error;

    // 控件
    PreviewView *view = nullptr;
    QWidget *bar = nullptr;
    QToolButton *prev = nullptr;
    QToolButton *next = nullptr;
    QLabel *pos = nullptr;
    ElideLabel *name = nullptr;
    QWidget *keyhint = nullptr;
    QHBoxLayout *keyhint_layout = nullptr;
    // A17（T11）：打标 chip（formkit::ChipButton；令牌随主题重放要逐件 set_tokens）+
    // 「0 清除」文字链（修 1-20：降级为文字链并分隔）
    QList<formkit::ChipButton *> chips;
    QToolButton *clear_hint = nullptr;

    std::unique_ptr<pp::PreviewCache> cache;
    Pool *pool = nullptr;
    Router *router = nullptr;

    // —— 0.4.2 需求 5（Contract: docs/m14-tasks.md C4）：显示面色彩管理 ——
    // **启动卡死根治（task-4，用户反馈「部分 Windows 版本打开后鼠标转圈、无响应」）**：
    // 显示 profile 的获取（Windows GetDC+GetICMProfileW + 读 ICC 文件 + lcms2 解析）是阻塞
    // 系统调用/文件 I/O，构造期**禁止在 GUI 线程同步执行**。构造期立即注入 sRGB 回退
    // （像素路径 = sRGB，offscreen 冻结截图/末行逐字不变），系统 profile 获取放到后台线程；
    // GUI 线程用 QTimer 轮询 shared state 应用（generation 计数丢弃过期；5s 软超时保持 sRGB
    // + 日志）。后台线程只写 shared state，**绝不触碰 QWidget/PreviewCache**；析构不 join
    // （detach + shared_ptr 状态）—— 对象销毁后线程安全退出，绝不回调已销毁对象。
    // 取证开关 PP_PREVIEW_CM=0（诊断用；正常运行不设置）：跳过注入 = 修复前行为（A/B 截图）。
    struct DisplayProfileProbe {
        std::mutex mu;
        bool done = false;
        std::uint64_t generation = 0; // 发起请求时的面板代次（generation 判据）
        pp::DisplayProfile profile;   // done=true 后有效
        long long elapsed_ms = 0;     // 后台获取耗时（诊断）
    };

    pp::DisplayProfile display_profile_;
    QList<QMetaObject::Connection> screen_connections;
    std::shared_ptr<DisplayProfileProbe> display_probe_; // 在途请求（新请求即替换；旧结果自然丢弃）
    std::uint64_t display_generation_ = 0;               // 每次请求 +1（generation 判据）
    std::chrono::steady_clock::time_point display_probe_start_{};
    bool display_probe_in_flight_ = false;
    bool display_timeout_logged_ = false;
    bool display_profile_initialized_ = false;
    QTimer display_poll_timer_; // GUI 线程轮询（Impl 成员：随 Impl 析构，先于 QWidget 基类销毁）

    explicit Impl(PreviewPanel *owner);
    ~Impl();

    void build_ui();
    void apply_style();
    void rebuild_key_hints();
    void select(int index, bool force);
    void start_load(const QString &path);
    // —— 0.4.2 需求5 + 启动卡死根治：显示 profile 异步获取/应用（全部在 GUI 线程调用）——
    void refresh_display_profile();     // 入口 = 启动 + 屏幕变更（GUI 线程；绝不阻塞）
    void start_display_profile_probe(); // 启动后台获取线程（GUI 线程）
    void poll_display_profile_probe();  // QTimer 回调（GUI 线程）
    void apply_display_profile(const pp::DisplayProfile &profile, const char *source);
    void update_bar();
    void update_keyhint_enabled(); // 修 1-22：chip 与翻图钮同档禁用（空态/只读）
    void on_loaded(int index, std::chrono::steady_clock::time_point request_time,
                   const pp::PreviewImage &result);
    bool route_key(QObject *watched, QEvent *event);
    bool hotkey_taken(QChar ch) const; // 注册表是否占用该热键（'0' = 保留的「清除」）
    void emit_hotkey(char key);
};

void PreviewPanel::Impl::Pool::start() {
    workers.reserve(2);
    for (int i = 0; i < 2; ++i) { // §6.1：独立低并发（≤2）后台线程池
        Pool *const self = this;
        workers.emplace_back([self](std::stop_token st) { self->worker_loop(st); });
    }
}

void PreviewPanel::Impl::Pool::stop() {
    {
        const std::lock_guard<std::mutex> lock(mu);
        stopping = true;
        queue.clear();
        latest_token = 0; // 在途结果一律作废
    }
    for (auto &worker : workers)
        worker.request_stop();
    cv.notify_all();
    workers.clear(); // join：在途解码跑完当前调用后立即退出
}

void PreviewPanel::Impl::Pool::submit(int index, const QString &path) {
    {
        const std::lock_guard<std::mutex> lock(mu);
        if (stopping)
            return;
        latest_token = next_token++; // 旧 token 全部过期
        queue.clear();               // 快速翻图不堆积：队列里只留最新一条
        queue.push_back(LoadJob{latest_token, index, path, std::chrono::steady_clock::now()});
    }
    cv.notify_all();
}

void PreviewPanel::Impl::Pool::worker_loop(std::stop_token st) {
    while (!st.stop_requested()) {
        LoadJob job;
        {
            std::unique_lock<std::mutex> lock(mu);
            cv.wait(lock, st, [this] { return !queue.empty(); });
            if (st.stop_requested())
                return;
            job = queue.front();
            queue.pop_front();
        }
        // 取件后先判过期：过期请求**不做解码**（已翻走的图不该再付全解码的代价）
        {
            const std::lock_guard<std::mutex> lock(mu);
            if (stopping || job.token != latest_token)
                continue;
        }
        const pp::PreviewImage result =
            impl->cache->get(std::filesystem::path(job.path.toStdString()));
        {
            const std::lock_guard<std::mutex> lock(mu);
            if (stopping || job.token != latest_token)
                continue; // 解码期间已翻走 → 结果丢弃（像素仍已进 LRU，回翻即命中）
        }
        Impl *const im = impl;
        const int index = job.index;
        const auto request_time = job.request_time;
        QMetaObject::invokeMethod(
            im->q,
            [im, index, request_time, result] { im->on_loaded(index, request_time, result); },
            Qt::QueuedConnection);
    }
}

PreviewPanel::Impl::Impl(PreviewPanel *owner) : q(owner) {
    tokens = theme::tokens(theme::system_theme_mode());
    cache = std::make_unique<pp::PreviewCache>(); // §6.1：LRU 16 张 / 2048px
    // task-4：后台获取结果由 GUI 线程定时轮询（QTimer 属 GUI 线程；connect 上下文 = q，q 析构
    // 即断开；timer 是 Impl 成员，Impl 析构先于 QWidget 基类，故无悬垂回调）。
    QObject::connect(&display_poll_timer_, &QTimer::timeout, q,
                     [this] { poll_display_profile_probe(); });
    refresh_display_profile(); // 0.4.2 需求5：立即注入 sRGB 回退 + 后台获取系统 profile（不阻塞）
    // 屏幕增删/主屏变更 → 显示 profile 可能换（换显示器/切换主屏）：失效 + 重新**异步**获取
    // （信号本身在 GUI 线程；回调里只失效缓存 + 起后台线程，不等待）。
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        screen_connections.append(QObject::connect(app, &QGuiApplication::primaryScreenChanged, q,
                                                   [this] { refresh_display_profile(); }));
        screen_connections.append(
            QObject::connect(app, &QGuiApplication::screenAdded, q,
                             [this](QScreen *) { refresh_display_profile(); }));
        screen_connections.append(
            QObject::connect(app, &QGuiApplication::screenRemoved, q,
                             [this](QScreen *) { refresh_display_profile(); }));
    }
    pool = new Pool();
    pool->impl = this;
    pool->start();
    router = new Router(this);
    if (QApplication::instance() != nullptr)
        QApplication::instance()->installEventFilter(router);
}

// 0.4.2 需求5 + 启动卡死根治（task-4）：显示 profile 的获取口径（Contract: docs/m14-tasks.md C4）。
// **铁律：本面板任何构造期/信号路径都不允许同步调用 pp::system_display_profile()** ——
// GUI 线程只做「注入当前口径 + 起后台线程 + 轮询」，系统调用/ICC 读取/lcms2 解析全在后台线程。
//   * 离屏平台（--ui-smoke / CI / 截图口径）无物理显示器 → 强制 sRGB 回退：像素路径与既有
//     版本逐字节一致（12 张冻结截图/冻结末行零变化由构造保证，不靠断言宽容）；且**不启动
//     后台线程**（无系统 profile 可取 → --ui-smoke 完全确定，不依赖系统状态/时序）；
//   * 真实平台 → 启动期立即注入 sRGB 回退（make_srgb_display_fallback("startup")），后台线程
//     获取系统 profile（Windows 主显示器 ICC；失败/非 Windows = sRGB 回退并如实记日志），
//     GUI 线程轮询完成后应用；屏幕变更信号走同一异步路径；
//   * PP_PREVIEW_CM=0/off（取证开关，正常运行不设置）→ 不注入 = 修复前行为，供修复前后 A/B。
void PreviewPanel::Impl::refresh_display_profile() {
    // 诊断开关 PP_PREVIEW_CM=0/off：跳过注入（修复前行为），不启动任何后台获取。
    const QByteArray cm_switch = qgetenv("PP_PREVIEW_CM");
    if (cm_switch == "0" || cm_switch.compare("off", Qt::CaseInsensitive) == 0) {
        if (display_probe_in_flight_ || display_probe_ != nullptr) {
            display_probe_in_flight_ = false;
            display_probe_.reset();
        }
        display_poll_timer_.stop();
        pp::log_info(
            "preview", "preview_panel.cpp",
            "display colour management disabled by PP_PREVIEW_CM (diagnostic)",
            {{"offscreen",
              QGuiApplication::platformName() == QStringLiteral("offscreen") ? "1" : "0"}});
        return;
    }
    const bool offscreen = QGuiApplication::platformName().compare(QStringLiteral("offscreen"),
                                                                   Qt::CaseInsensitive) == 0;
    if (offscreen) {
        // 离屏：强制 sRGB 且不启动后台线程（确定性优先；无物理显示器可管理）。
        display_probe_in_flight_ = false;
        display_probe_.reset();
        display_poll_timer_.stop();
        const pp::DisplayProfile fallback = pp::make_srgb_display_fallback(
            "offscreen platform has no physical display (CI/screenshot path)");
        apply_display_profile(fallback, "offscreen");
        pp::log_info("preview", "preview_panel.cpp",
                     "display colour management enabled (offscreen: sRGB forced)",
                     {{"display_profile", display_profile_.name},
                      {"from_system", display_profile_.from_system ? "1" : "0"},
                      {"offscreen", "1"},
                      {"fallback_reason", display_profile_.fallback_reason}});
        return;
    }
    if (!display_profile_initialized_) {
        // 启动期第一步：立即注入 sRGB 回退 —— 像素路径确定、不等系统调用；任何在后台获取
        // 完成前解码的图都按「假定 sRGB → sRGB 显示」恒等路径渲染（= 0.4.1 的显示像素口径）。
        apply_display_profile(
            pp::make_srgb_display_fallback(
                "startup: system display profile acquisition runs in the background"),
            "startup");
        pp::log_info("preview", "preview_panel.cpp",
                     "display colour management enabled (startup sRGB; system profile acquisition "
                     "in background)",
                     {{"display_profile", display_profile_.name},
                      {"from_system", "0"},
                      {"offscreen", "0"},
                      {"startup_srgb", "1"},
                      {"qt", qVersion()},
                      {"platform", QGuiApplication::platformName().toStdString()},
                      {"os_build", os_version_build_text()}});
    } else {
        // 屏幕变更（运行期）：保持当前显示口径直到新结果到达（避免闪回 sRGB），仅记诊断。
        const QScreen *primary = QGuiApplication::primaryScreen();
        pp::log_info("display", "preview_panel.cpp",
                     "display profile refresh requested (screen change; background acquisition)",
                     {{"primary_screen", primary != nullptr ? primary->name().toStdString()
                                                            : std::string("<none>")}});
    }
    start_display_profile_probe();
}

// 启动后台获取线程（GUI 线程调用；线程本身不阻塞 GUI）。shared_ptr<DisplayProfileProbe> 由
// 线程持有：面板析构后线程仍可安全写 shared state（无人读、无回调），完成后自行释放退出。
void PreviewPanel::Impl::start_display_profile_probe() {
    pp::invalidate_display_profile_cache(); // 进程缓存失效（互斥；不阻塞）
    ++display_generation_;
    auto probe = std::make_shared<DisplayProfileProbe>();
    probe->generation = display_generation_;
    display_probe_ = probe;
    display_probe_start_ = std::chrono::steady_clock::now();
    display_probe_in_flight_ = true;
    display_timeout_logged_ = false;
    const auto request_time = display_probe_start_;
    try {
        std::thread([probe, request_time] {
            // 后台线程：只调用 core 的阻塞获取（含 ICC 文件读取上限/诊断延迟），把结果写入
            // shared state。**绝不触碰任何 QWidget / PreviewCache / Impl**。
            pp::DisplayProfile profile = pp::system_display_profile();
            const long long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - request_time)
                                             .count();
            const std::lock_guard<std::mutex> lock(probe->mu);
            probe->profile = std::move(profile);
            probe->elapsed_ms = elapsed_ms;
            probe->done = true;
        }).detach();
    } catch (const std::system_error &error) {
        // 线程创建失败（极端资源耗尽）：保持 sRGB 并如实记日志，绝不回退到 GUI 线程同步获取。
        display_probe_in_flight_ = false;
        display_probe_.reset();
        pp::log_warn("display", "preview_panel.cpp",
                     "cannot spawn the display profile background thread; keeping sRGB",
                     {{"error", error.what()}});
        return;
    }
    display_poll_timer_.start(kDisplayProbePollMs);
}

// GUI 线程轮询（QTimer 回调）：读 shared state → 纯函数判据 → 应用/超时/丢弃。
// 超时是**软超时**：到 5s 记一次 warn 并保持 sRGB（显示面从不等待）；之后在 30s 硬上限内继续
// 观察迟到结果（迟到只意味着晚一点切到正确显示编码，不回退、不二次跳变）。
void PreviewPanel::Impl::poll_display_profile_probe() {
    if (!display_probe_in_flight_ || display_probe_ == nullptr) {
        display_poll_timer_.stop();
        return;
    }
    bool done = false;
    pp::DisplayProfile profile;
    long long elapsed_ms = 0;
    std::uint64_t generation = 0;
    {
        const std::lock_guard<std::mutex> lock(display_probe_->mu);
        done = display_probe_->done;
        generation = display_probe_->generation;
        if (done) {
            profile = display_probe_->profile;
            elapsed_ms = display_probe_->elapsed_ms;
        }
    }
    const long long waited_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - display_probe_start_)
                                    .count();
    switch (pp::classify_display_profile_async(done, generation, display_generation_, waited_ms,
                                               kDisplayProbeTimeoutMs)) {
    case pp::DisplayProfileAsyncState::Pending:
        return; // 继续轮询
    case pp::DisplayProfileAsyncState::Ready: {
        display_probe_in_flight_ = false;
        display_probe_.reset();
        display_poll_timer_.stop();
        const bool changed = !same_display_profile(display_profile_, profile);
        apply_display_profile(profile, "background");
        pp::log_info("display", "preview_panel.cpp",
                     "display profile switched (display colour management enabled)",
                     {{"display_profile", display_profile_.name},
                      {"from_system", display_profile_.from_system ? "1" : "0"},
                      {"elapsed_ms", std::to_string(elapsed_ms)},
                      {"waited_ms", std::to_string(waited_ms)},
                      {"late_after_timeout", display_timeout_logged_ ? "1" : "0"},
                      {"changed", changed ? "1" : "0"},
                      {"fallback_reason", display_profile_.fallback_reason}});
        return;
    }
    case pp::DisplayProfileAsyncState::Stale:
        display_probe_in_flight_ = false;
        display_probe_.reset();
        display_poll_timer_.stop();
        pp::log_debug("display", "preview_panel.cpp",
                      "display profile result discarded (stale generation)",
                      {{"generation", std::to_string(generation)},
                       {"current_generation", std::to_string(display_generation_)}});
        return;
    case pp::DisplayProfileAsyncState::Timeout:
        // 到点仍无结果：如实记一次 warn，保持当前（启动期 = sRGB）显示口径；继续观察迟到结果。
        if (waited_ms >= kDisplayProbeHardCapMs) {
            // 硬上限：放弃观察（后台线程继续跑完自退，结果无人消费）。
            display_probe_in_flight_ = false;
            display_probe_.reset();
            display_poll_timer_.stop();
            pp::log_warn("display", "preview_panel.cpp",
                         "display profile acquisition abandoned after the hard cap; keeping sRGB",
                         {{"hard_cap_ms", std::to_string(kDisplayProbeHardCapMs)},
                          {"waited_ms", std::to_string(waited_ms)}});
            return;
        }
        if (!display_timeout_logged_) {
            display_timeout_logged_ = true;
            pp::log_warn("display", "preview_panel.cpp",
                         "display profile acquisition exceeded 5s; keeping sRGB fallback",
                         {{"timeout_ms", std::to_string(kDisplayProbeTimeoutMs)},
                          {"waited_ms", std::to_string(waited_ms)}});
        }
        return; // 软超时：继续轮询（GUI 从未等待；迟到结果到达后按 Ready 应用）
    }
}

// GUI 线程应用显示 profile：注入 PreviewCache（线程安全；语义变化会清空旧显示编码条目）。
// 若语义真的变化且当前有图：强制重取当前项（旧条目已按旧 profile 编码）→ 显示面立即按新
// profile 重解码。profile 未变（如同一显示器重复信号）→ 清缓存/重取皆 no-op（不抖动）。
void PreviewPanel::Impl::apply_display_profile(const pp::DisplayProfile &profile,
                                               const char *source) {
    const bool changed = !same_display_profile(display_profile_, profile);
    display_profile_ = profile;
    display_profile_initialized_ = true;
    if (cache != nullptr)
        cache->set_display_profile(display_profile_);
    if (!changed)
        return;
    pp::log_debug("display", "preview_panel.cpp", "display profile applied",
                  {{"source", source},
                   {"display_profile", display_profile_.name},
                   {"from_system", display_profile_.from_system ? "1" : "0"},
                   {"fallback_reason", display_profile_.fallback_reason}});
    if (current >= 0 && current < files.size())
        select(current, /*force=*/true); // 重取当前图（缓存已被清空 → 按新 profile 解码）
}

PreviewPanel::Impl::~Impl() {
    // task-4（启动卡死根治）：先停显示 profile 轮询并释放在途 probe —— 析构**不 join** 后台
    // 线程（detach + shared_ptr：线程完成时只写 shared state 后自退，绝不回调已销毁对象，
    // 也不会让退出/析构被慢系统调用拖住）。QTimer 是成员：析构即停，先于 QWidget 基类。
    display_poll_timer_.stop();
    display_probe_in_flight_ = false;
    display_probe_.reset();
    for (const QMetaObject::Connection &connection : screen_connections)
        QObject::disconnect(connection);
    screen_connections.clear();
    pool->stop(); // 先停池（保证没有 worker 再用 cache/impl），再拆其余
    delete pool;
    pool = nullptr;
    if (router != nullptr) {
        if (QApplication::instance() != nullptr)
            QApplication::instance()->removeEventFilter(router);
        delete router;
        router = nullptr;
    }
}

void PreviewPanel::Impl::build_ui() {
    auto *outer = new QVBoxLayout(q);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    view = new PreviewView(q);
    // 缩放状态只在舞台里一份（模式/比例/平移），面板只转发信号（zoom_changed 只发一次）
    view->set_zoom_handler([this] { emit q->zoom_changed(view->zoom_fit()); });
    auto *stage_margin = new QHBoxLayout();
    stage_margin->setContentsMargins(kStageMarginX, 0, kStageMarginX, 0);
    stage_margin->addWidget(view);
    outer->addLayout(stage_margin, 1);

    bar = new QWidget(q);
    bar->setObjectName(QStringLiteral("pp-preview-bar"));
    auto *bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(kBarPadX, kBarPadTop, kBarPadX, kBarPadBottom);
    bar_layout->setSpacing(kBarGap);

    // 翻图钮：图标 = 矢量（icons::prev/next，mockup .pv-bar 的 12×12 内联 svg 逐点同源），
    // 不再是 ◀/▶ 字形（§3.3）；修 1-21：命中区 28×28（R4 图标钮档）。取色在 apply_style()
    // （text2，禁用 text3）。
    const auto make_nav = [this, bar_layout](const char *object_name, icons::Name icon_name,
                                             const char *tip) {
        auto *button = new QToolButton(bar);
        button->setObjectName(QString::fromLatin1(object_name));
        button->setToolTip(T(tip));
        button->setAccessibleName(T(tip)); // §11：图标钮必有 accessibleName + tooltip
        button->setFixedSize(theme::Metrics::icon_btn, theme::Metrics::icon_btn);
        button->setIconSize(QSize(kNavIconPx, kNavIconPx));
        button->setProperty(icons::kIconNameProperty, QString::fromLatin1(icons::key(icon_name)));
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        bar_layout->addWidget(button);
        return button;
    };
    prev = make_nav("pp-preview-prev", icons::Name::Prev, "上一张 (←)");
    next = make_nav("pp-preview-next", icons::Name::Next, "下一张 (→)");
    QObject::connect(prev, &QToolButton::clicked, q, [this] { q->previous(); });
    QObject::connect(next, &QToolButton::clicked, q, [this] { q->next(); });

    pos = new QLabel(bar); // 「n / N」固定短串：用普通 QLabel（保留自身最小宽，不被压掉）
    pos->setObjectName(QStringLiteral("pp-preview-pos"));
    pos->setFont(theme::font(theme::Typography::body_px, QFont::Normal, /*tabular=*/true));
    bar_layout->addWidget(pos);

    name = new ElideLabel(bar);
    name->setObjectName(QStringLiteral("pp-preview-name"));
    name->setFont(theme::font(theme::Typography::body_px));
    bar_layout->addWidget(name, 1);

    keyhint = new QWidget(bar);
    keyhint_layout = new QHBoxLayout(keyhint);
    keyhint_layout->setContentsMargins(0, 0, 0, 0);
    keyhint_layout->setSpacing(kKeyChipGap);
    bar_layout->addWidget(keyhint, 0, Qt::AlignRight);
    outer->addWidget(bar);
}

void PreviewPanel::Impl::apply_style() {
    // UX-6⑥（M7-WP4）：本面板 QSS 全部收编 theme::preview_panel_qss（覆盖层固定色 =
    // theme::overlay_tokens()，不随主题变；选择器只锚本面板 pp-* —— 见 theme.h 的"为何不能公共"）。
    q->setStyleSheet(theme::preview_panel_qss(tokens));

    // A17：chip 是自绘件（不吃 QSS）→ 令牌在这里显式重放（与骨架 QSS 同一次主题切换）
    for (formkit::ChipButton *chip : chips) {
        if (chip != nullptr)
            chip->set_tokens(tokens);
    }
    // 翻图钮矢量图标随令牌取色（text2 / 禁用 text3）—— make_nav 只 setProperty(kIconNameProperty)
    // 探针钩子、**从不 icons::apply** ⇒ 底条两块白方（debug/debug1.png 红框 3；
    // layout-audit ③ ink-btn 钉守）。图标是矢量渲染色，不吃 QSS，必须显式重放。
    if (prev != nullptr)
        icons::apply(prev, icons::Name::Prev, tokens.text2, tokens.text3, kNavIconPx);
    if (next != nullptr)
        icons::apply(next, icons::Name::Next, tokens.text2, tokens.text3, kNavIconPx);
}

void PreviewPanel::Impl::rebuild_key_hints() {
    QLayoutItem *item = nullptr;
    while ((item = keyhint_layout->takeAt(0)) != nullptr) {
        if (QWidget *widget = item->widget()) {
            // 撤下 = 出布局 + 脱离父子关系 + 立即隐藏（残留子件会盖在新表上、也会被 findChildren
            // 命中）；析构仍走 deleteLater（不在事件处理途中直接 delete）。
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    // A17（T11）：chip = formkit::ChipButton（**单实现**；旧实现是「键帽 ClipLabel + 名称
    // ClipLabel」两个件的碎片化拼接，且与 formkit 的同名控件构成双实现）。键帽 = R4 口径的
    // 13×13 r2 小方块（修 1-20「键帽化简」：不再是 padding 1px 6px / r4 的大框），快捷键同时
    // 进 tooltip（悬停可查）。chip 自身不勾选（打标接线位属其后任务）→ 视觉零形变。
    chips.clear();
    const auto add_chip = [this](const QString &key, const QString &label) {
        auto *chip = new formkit::ChipButton(keyhint);
        chip->setObjectName(QStringLiteral("pp-preview-key"));
        chip->set_key_text(key);
        chip->setText(label);
        chip->setCheckable(false); // 仅承载热键提示（勾选语义留给打标接线）
        chip->setFocusPolicy(Qt::NoFocus);
        chip->setToolTip(T("快捷键 ") + key);
        chip->setAccessibleName(label);
        chip->set_tokens(tokens);
        // M7-R2.5：1180 最小窗口下 QHBoxLayout 会把 chip 压成 13×18 空胶囊（文字全没）。
        // 热键 chip 不可压缩，富余宽度由中间文件名的 ElideLabel 承担。
        chip->setMinimumWidth(chip->sizeHint().width());
        keyhint_layout->addWidget(chip);
        chips.append(chip);
    };
    auto *title = new ClipLabel(T("快速打标"), keyhint);
    title->setObjectName(QStringLiteral("pp-preview-keyhint-label"));
    title->setFont(theme::font(theme::Typography::hint_px));
    title->setMinimumWidth(title->sizeHint().width());
    keyhint_layout->addWidget(title);
    // 热键表 = 注册表的 classes（hotkey ≠ 0，§6.2）
    if (classes != nullptr) {
        for (const pp::ClassDef &def : classes->classes) {
            if (def.hotkey == 0)
                continue;
            add_chip(QString(QChar(def.hotkey)), QString::fromStdString(def.name));
        }
    }
    // 修 1-20（A17）：保留的「0 清除」是破坏性动作 → 与打标 chip **分隔线**隔开，并**降级为
    // 文字链**（无键帽、无底框）；热键 '0' 的路由不变（hotkey_taken/emit_hotkey 仍认它）。
    auto *sep = new QFrame(keyhint);
    sep->setObjectName(QStringLiteral("pp-preview-key-sep"));
    sep->setAttribute(Qt::WA_StyledBackground, true);
    sep->setFixedSize(1, 14);
    keyhint_layout->addWidget(sep);
    auto *clear = new QToolButton(keyhint);
    clear->setObjectName(QStringLiteral("pp-preview-clear"));
    clear->setText(T("清除"));
    clear->setAutoRaise(true);
    clear->setCursor(Qt::PointingHandCursor);
    clear->setFocusPolicy(Qt::NoFocus);
    clear->setFixedHeight(formkit::kChipH); // 与 chip 同高（行内基线一致）
    clear->setFont(theme::font(theme::Typography::small_px));
    clear->setToolTip(T("清除打标（快捷键 0）"));
    clear->setAccessibleName(clear->text());
    clear->setMinimumWidth(clear->sizeHint().width());
    clear_hint = clear;
    keyhint_layout->addWidget(clear);
    keyhint->setMinimumWidth(keyhint->sizeHint().width());
    keyhint->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    update_keyhint_enabled();
}

// 修 1-22：打标 chip 的禁用态 = 无文件（空态）或运行期只读（G5）—— 与翻图钮的空态禁用同一条
// 判据（同一函数内给档，面板三处状态变化都走它，不各自复制）
void PreviewPanel::Impl::update_keyhint_enabled() {
    const bool has_files = current >= 0 && current < files.size();
    keyhint->setEnabled(has_files && !locked);
}

void PreviewPanel::Impl::update_bar() {
    const bool has_files = current >= 0 && current < files.size();
    pos->setText(has_files ? QStringLiteral("%1 / %2").arg(current + 1).arg(files.size())
                           : QString());
    name->set_full_text(has_files ? QFileInfo(files.at(current)).fileName() : QString());
    prev->setEnabled(has_files && current > 0);
    next->setEnabled(has_files && current + 1 < files.size());
    update_keyhint_enabled();
}

void PreviewPanel::Impl::select(int index, bool force) {
    if (index < 0 || index >= files.size())
        index = files.isEmpty() ? -1 : 0; // 无选中 → 首个文件（§5.2 同一条跟随口径）
    current = index;
    update_bar();
    if (index < 0) {
        view->set_empty_state(); // §8 空态：矢量图标 + 引导语（修 1-16：不再是纯黑空画布）
        loaded_path.clear();
        attempted_path.clear();
        info_text.clear();
        emit q->display_changed();
        return;
    }
    const QString path = files.at(index);
    // 幂等：同一路径的请求在途 / 已尝试过 → 不重复发起（选择信号可能连续触发）
    if (!force && path == attempted_path) {
        emit q->display_changed();
        return;
    }
    start_load(path);
    emit q->display_changed();
}

void PreviewPanel::Impl::start_load(const QString &path) {
    if (current < 0 || current >= files.size())
        return;
    attempted_path = path;
    pool->submit(current, path);
}

void PreviewPanel::Impl::on_loaded(int index, std::chrono::steady_clock::time_point request_time,
                                   const pp::PreviewImage &result) {
    if (index != current)
        return; // 已翻到别的图：过期结果丢弃
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - request_time)
                             .count();
    if (result.image.isNull()) {
        view->set_placeholder(T("无法预览：") + QString::fromStdString(result.error));
        loaded_path.clear();
        info_text.clear();
        // R-277：记住失败（信息行不再恒显「载入中…」—— 见 current_info_text）
        failed_path = index >= 0 && index < files.size() ? files.at(index) : QString();
        failed_error = QString::fromStdString(result.error);
        // R-278：出图诊断收 logger（原 std::printf 绕过日志面；内容口径不变）
        pp::log_warn("preview", failed_path.toStdString(),
                     "load failed (" + std::to_string(ms) + " ms): " + result.error);
        emit q->display_changed();
        return;
    }
    view->set_image(result.image);
    loaded_path = index >= 0 && index < files.size() ? files.at(index) : QString();
    failed_path.clear(); // R-277：成功出图即清算失败档
    failed_error.clear();
    // 信息行（§6.1「文件名/尺寸/位深/色彩空间随图显示」；尺寸/位深 = 源文件 probe 摘要）
    info_text = QStringLiteral("%1×%2 · %3 bit · %4")
                    .arg(result.info.width)
                    .arg(result.info.height)
                    .arg(result.info.src_bitdepth)
                    .arg(QString::fromStdString(result.color_space));
    // R-278：出图诊断收 logger（250ms 目标取证口径不变，级别 = debug）
    // 0.4.2 需求5：追加 display= 显示编码目标（空 = 未做显示变换），取证显示面色彩管理生效面。
    pp::log_debug("preview", loaded_path.toStdString(),
                  "loaded " + std::to_string(result.image.width()) + "x" +
                      std::to_string(result.image.height()) +
                      (result.from_embedded ? " from_embedded=1" : " from_embedded=0") + " (" +
                      std::to_string(ms) + " ms) info=" + info_text.toStdString() +
                      " display=" + result.display_space);
    emit q->display_changed();
}

bool PreviewPanel::Impl::hotkey_taken(QChar ch) const {
    if (ch == QLatin1Char('0'))
        return true; // '0' 保留作「清除」（§6.2）
    if (classes == nullptr)
        return false;
    for (const pp::ClassDef &def : classes->classes) {
        if (def.hotkey != 0 && QChar(QLatin1Char(def.hotkey)) == ch)
            return true;
    }
    return false;
}

void PreviewPanel::Impl::emit_hotkey(char key) { emit q->class_hotkey(QChar(QLatin1Char(key))); }

bool PreviewPanel::Impl::route_key(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::KeyPress || q->window() == nullptr)
        return false;
    auto *key_event = static_cast<QKeyEvent *>(event);
    if (key_event->modifiers() != Qt::NoModifier)
        return false; // 带修饰键的组合不拦截
    // 仅当事件属于本面板所在窗口（对话框/其它窗口不抢）
    auto *watched_widget = qobject_cast<QWidget *>(watched);
    if (watched_widget == nullptr || watched_widget->window() != q->window())
        return false;
    QWidget *focus = QApplication::focusWidget();
    if (focus != nullptr && focus->window() != q->window())
        return false;
    if (focus != nullptr && (qobject_cast<QLineEdit *>(focus) != nullptr ||
                             qobject_cast<QAbstractSpinBox *>(focus) != nullptr ||
                             qobject_cast<QComboBox *>(focus) != nullptr ||
                             qobject_cast<QTextEdit *>(focus) != nullptr)) {
        return false; // 文本输入控件自己消费（搜索框里数字/方向键照常输入）
    }
    switch (key_event->key()) {
    case Qt::Key_Left: // §6.1 翻图热键
    case Qt::Key_Right: {
        // R-314：焦点在列表视图（QTreeView/QListView…）时把 ←/→ **让给视图** —— 原应用级路由
        // 无条件吞方向键 → 分组节的键盘折叠/展开永远不可达。非列表焦点下翻图语义不变。
        if (focus != nullptr && qobject_cast<QAbstractItemView *>(focus) != nullptr)
            return false;
        if (key_event->key() == Qt::Key_Left)
            q->previous();
        else
            q->next();
        return true;
    }
    default:
        break;
    }
    const QString text = key_event->text();
    if (!locked && text.size() == 1 && hotkey_taken(text.at(0))) {
        emit_hotkey(text.at(0).toLatin1()); // 打标接线位（T11 接通；数字键两路都认）
        return true;
    }
    return false;
}

// -------------------------------------------------------------------------------------------
// PreviewPanel 公开面
// -------------------------------------------------------------------------------------------

PreviewPanel::PreviewPanel(QWidget *parent) : QWidget(parent), impl_(std::make_unique<Impl>(this)) {
    impl_->build_ui();
    impl_->apply_style();
    impl_->rebuild_key_hints();
    impl_->update_bar();
}

PreviewPanel::~PreviewPanel() = default;

void PreviewPanel::set_tokens(const theme::Tokens &tokens) {
    impl_->tokens = tokens;
    impl_->apply_style();
    impl_->view->set_tokens(tokens);
}

void PreviewPanel::set_files(const QStringList &paths) {
    Impl &d = *impl_;
    if (d.files == paths)
        return;
    d.files = paths;
    d.select(d.current, /*force=*/false); // 集合变化：收敛当前项 + 必要时重取
}

void PreviewPanel::set_current(int index) { impl_->select(index, /*force=*/false); }

int PreviewPanel::current_index() const { return impl_->current; }

void PreviewPanel::reload() { impl_->select(impl_->current, /*force=*/true); }

void PreviewPanel::next() {
    Impl &d = *impl_;
    if (d.current + 1 < d.files.size()) {
        d.select(d.current + 1, /*force=*/false);
        emit navigated(d.current); // R-151：翻图同步左栏选中（所见即所标）
    }
}

void PreviewPanel::previous() {
    Impl &d = *impl_;
    if (d.current > 0) {
        d.select(d.current - 1, /*force=*/false);
        emit navigated(d.current); // R-151：同上
    }
}

// 缩放：状态在舞台（view）里一份 —— 本组入口只转发，信号只由舞台的 zoom_handler 发一次
// （程序化设置与按钮/滚轮/双击同一条落地路径）。
void PreviewPanel::set_zoom_fit(bool fit) { impl_->view->set_zoom_fit(fit); }

bool PreviewPanel::zoom_fit() const { return impl_->view->zoom_fit(); }

void PreviewPanel::set_zoom_percent(int percent) { impl_->view->set_zoom_percent(percent); }

int PreviewPanel::zoom_percent() const { return impl_->view->zoom_percent(); }

void PreviewPanel::set_class_registry(const pp::ClassRegistry *registry) {
    Impl &d = *impl_;
    d.classes = registry;
    d.rebuild_key_hints();
}

void PreviewPanel::set_locked(bool locked) {
    Impl &d = *impl_;
    d.locked = locked;
    d.update_keyhint_enabled(); // 运行期只读：打标热键置灰（G5 语义零改动）
}

// M7-R2.1-F4 / C4：线性光重采样透传（切换即清缓存 + 当前图重解；同值 no-op）
void PreviewPanel::set_linear_resample(bool on) {
    Impl &d = *impl_;
    if (d.cache == nullptr || d.cache->linear_resample() == on)
        return;
    d.cache->set_linear_resample(on);
    reload();
}

// M7-R2D1 / R-278：set_hotkeys_enabled / current_path 已随死 API 清理移除（零消费）。

QString PreviewPanel::current_file_name() const {
    const Impl &d = *impl_;
    if (d.current < 0 || d.current >= d.files.size())
        return {};
    return QFileInfo(d.files.at(d.current)).fileName();
}

QString PreviewPanel::current_info_text() const {
    const Impl &d = *impl_;
    if (d.current < 0 || d.current >= d.files.size())
        return {};
    const QString &current_path = d.files.at(d.current);
    if (d.loaded_path == current_path)
        return d.info_text;
    // R-277：加载失败不再恒显「载入中…」—— 与舞台的「无法预览」同判（失败档随成功出图清算）
    if (d.failed_path == current_path)
        return d.failed_error.isEmpty() ? tr("无法预览") : tr("无法预览：%1").arg(d.failed_error);
    return tr("载入中…");
}

QString PreviewPanel::current_position_text() const {
    const Impl &d = *impl_;
    if (d.current < 0 || d.current >= d.files.size())
        return {};
    return QStringLiteral("%1 / %2").arg(d.current + 1).arg(d.files.size());
}

bool PreviewPanel::image_visible() const {
    const Impl &d = *impl_;
    return d.current >= 0 && d.current < d.files.size() && d.loaded_path == d.files.at(d.current);
}

QSize PreviewPanel::displayed_size() const { return impl_->view->displayed_size(); }

bool PreviewPanel::nearest_neighbor() const { return impl_->view->nearest_neighbor(); }

int PreviewPanel::pending_requests() const {
    const std::lock_guard<std::mutex> lock(impl_->pool->mu);
    return static_cast<int>(impl_->pool->queue.size());
}

} // namespace pp::ui
