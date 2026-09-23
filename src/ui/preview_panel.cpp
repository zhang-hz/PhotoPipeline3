// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-W2-T10）— 常驻输入预览面板实现（落地口径见 ui/preview_panel.h）
//
// 三段结构（自上而下）：
//   ① 舞台 PreviewView（= mockup .pv-stage；#pp-preview-stage 的底/边框/圆角由 theme QSS 提供）：
//      画当前 QImage（适应 = 等比铺满舞台 / 1:1 = 原始像素 + 拖拽平移）+ 恒显徽标 + 缩放 chip
//      两枚；
//   ② 底条（= mockup .pv-bar）：◀ ▶ 翻图 ·「n / N」· 文件名 · 右侧热键提示表（随注册表动态生成）；
//   ③ 异步池（= §6.1「独立低并发（≤2）后台线程池」）：≤2 jthread；队列只保留最新请求，过期请求
//      **不解码**、过期结果**不投递**（快速翻图不堆积）；LRU 命中在池线程内瞬时返回。
//
// 全部字面量（尺寸/文案/色值）来源：docs/mockups/meta-dark.html 的 .pv-* 规则（§9.4：HTML =
// 精确规格） 与 ui/theme.h 的
// tokens；舞台内元素（深色舞台上的徽标/chip）是主题无关固定值，单列常量并注明出处。

#include "ui/preview_panel.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QString>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
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
#include <thread>
#include <utility>
#include <vector>

#include "ui/theme.h" // 只在实现里引（头文件前置声明 theme::Tokens 的原因见 preview_panel.h）

namespace pp::ui {
namespace {

// ---- mockup meta-dark.html .pv-* 的尺寸字面量（HTML = 精确规格）----
constexpr int kStageMarginX = 10; // .pv-stage{margin:0 10px}
constexpr int kBarPadTop = 8;     // .pv-bar{padding:8px 12px 10px}
constexpr int kBarPadX = 12;
constexpr int kBarPadBottom = 10;
constexpr int kBarGap = 10;      // .pv-bar{gap:10px}
constexpr int kNavButtonPx = 26; // .nav-arrow{width:26px;height:26px;border-radius:5px}
constexpr int kNavButtonRadius = 5;
constexpr int kChipPadX = 8; // .pv-zoom .z{padding:3px 8px;border-radius:4px}
constexpr int kChipPadY = 3;
constexpr int kChipRadius = 4;
constexpr int kChipGap = 4;       // .pv-zoom{gap:4px}
constexpr int kOverlayMargin = 8; // .pv-badge{top:8px;left:8px} / .pv-zoom{top:8px;right:8px}
constexpr int kOverlayPadX = 8;   // .pv-badge{padding:3px 8px}
constexpr int kOverlayPadY = 3;
constexpr int kOverlayRadius = 4;
constexpr int kKeyChipPadX = 6; // .key{padding:1px 6px;border-radius:4px}
constexpr int kKeyChipPadY = 1;
constexpr int kKeyChipGap = 6; // .keyhint{gap:6px}
// 舞台内元素是**主题无关**的深色舞台配色（mockup 固定值；舞台底 #0d0f12 = tokens.stage_bg）
const char *const kOverlayFg = "#dfe6ec";            // .pv-badge / .z 字色
const char *const kOverlayBg = "rgba(10,12,16,62%)"; // rgba(10,12,16,.62)
const char *const kOverlayBd = "rgba(255,255,255,12%)";
const char *const kOverlayActiveBg = "rgba(255,255,255,10%)"; // .seg div.on 的选中底
const char *const kOverlayActiveBd = "rgba(255,255,255,28%)";

QString T(const char *s) { return QCoreApplication::translate("PreviewPanel", s); }

// 单行省略标签（§9.3「全界面禁止文案折行（超长省略号）」）。
// minimumSizeHint 宽压到 0：超长文件名/提示**省略**而不是把中栏撑宽（三栏 1440 校准尺寸
// 258/537/601 不得被中栏内容的最小宽顶开）。
// NOTE：mainwindow.cpp 的同名助手在匿名 namespace 内、跨 TU 不可见；抽出公共头会动清单外文件面，
//       故本面板自带一份等价的小实现。
class ElideLabel : public QLabel {
public:
    explicit ElideLabel(QWidget *parent = nullptr) : QLabel(parent) {}
    void set_full_text(const QString &text) {
        full_ = text;
        updateGeometry();
        apply_elide();
    }
    QString full_text() const { return full_; }
    QSize sizeHint() const override {
        const QSize base = QLabel::sizeHint();
        if (full_.isEmpty())
            return base;
        return QSize(fontMetrics().horizontalAdvance(full_) + 2, base.height());
    }
    QSize minimumSizeHint() const override {
        return QSize(0, QLabel::minimumSizeHint().height()); // 可压到 0（靠 set_full_text 的省略）
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        apply_elide();
    }

private:
    void apply_elide() {
        if (full_.isEmpty()) {
            QLabel::clear();
            return;
        }
        if (width() <= 0) {
            QLabel::setText(full_);
            return;
        }
        QLabel::setText(fontMetrics().elidedText(full_, Qt::ElideRight, std::max(0, width())));
    }
    QString full_;
};

// 热键提示里的 chip / 名称（§9.3 禁折行）：自然宽度用 sizeHint，但允许被压缩到 0
// （窄栏时按 mockup 的 flex-shrink 语义裁剪，不换行、也不把中栏最小宽顶上去）。
class ClipLabel : public QLabel {
public:
    explicit ClipLabel(const QString &text, QWidget *parent) : QLabel(text, parent) {}
    QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }
};

// 预览舞台（mockup .pv-stage）。画图只做**显示所必需**的缩放/平移（§6.1「不做色彩管理之外的
// 任何处理」→ 像素不动，只在绘制阶段缩放）：
//   * 适应：scale = min(view_w/img_w, view_h/img_h) —— **跟随舞台缩放（可放大，与 mockup 的
//     .pv-stage 满幅呈现一致；"适应"与"1:1"对小图才有可辨识差异）**；注意这是**绘制阶段**的
//     缩放，与解码口径无关：core 的 decode_preview 一律不放大（≤10 张 2048px 的像素就是源像素）。
//   * 1:1 ：原始像素居中；图大于舞台时可左键拖拽平移（夹取到不露舞台外的边界）
// 圆角裁切（mockup .pv-stage{overflow:hidden}）由 paintEvent 的 clipPath 落地。
class PreviewView : public QFrame {
public:
    explicit PreviewView(QWidget *parent = nullptr);

    void set_image(const QImage &image);
    void set_placeholder(const QString &text);
    void set_fit(bool fit);
    bool fit() const { return fit_; }
    void set_tokens(const theme::Tokens &tokens);
    void set_zoom_handler(std::function<void(bool)> handler) { zoom_handler_ = std::move(handler); }
    QSize displayed_size() const { return image_.isNull() ? QSize() : target_size(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QSize target_size() const;
    QRect target_rect() const;
    QPoint clamp_pan(const QPoint &pan) const;
    void layout_overlays();

    QImage image_;
    QString placeholder_;
    bool fit_ = true;
    QPoint pan_;
    bool dragging_ = false;
    QPoint drag_origin_;
    QPoint drag_pan0_;
    theme::Tokens tokens_;
    QLabel *badge_ = nullptr;
    QWidget *zoom_row_ = nullptr;
    QToolButton *zoom_fit_ = nullptr;
    QToolButton *zoom_1to1_ = nullptr;
    std::function<void(bool)> zoom_handler_;
};

PreviewView::PreviewView(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("pp-preview-stage")); // theme QSS: QFrame#pp-preview-stage
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(120);

    // 徽标「输入 · 未修改像素」（§6.1：恒显；mockup .pv-badge）
    badge_ = new QLabel(T("输入 · 未修改像素"), this);
    badge_->setObjectName(QStringLiteral("pp-preview-badge"));
    badge_->setFont(theme::font(theme::Typography::hint_px, 500));

    // 缩放「适应 | 1:1」（mockup .pv-zoom 的两枚 chip；文案逐字含「适应 ▾」的插入符）
    zoom_row_ = new QWidget(this);
    auto *zoom_layout = new QHBoxLayout(zoom_row_);
    zoom_layout->setContentsMargins(0, 0, 0, 0);
    zoom_layout->setSpacing(kChipGap);
    zoom_fit_ = new QToolButton(zoom_row_);
    zoom_fit_->setObjectName(QStringLiteral("pp-preview-zoom-fit"));
    zoom_fit_->setText(T("适应 ▾"));
    zoom_fit_->setToolTip(T("适应舞台（等比铺满）"));
    zoom_1to1_ = new QToolButton(zoom_row_);
    zoom_1to1_->setObjectName(QStringLiteral("pp-preview-zoom-1to1"));
    zoom_1to1_->setText(QStringLiteral("1:1"));
    zoom_1to1_->setToolTip(T("原始像素（1:1，可拖拽平移）"));
    for (QToolButton *button : {zoom_fit_, zoom_1to1_}) {
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFont(theme::font(theme::Typography::hint_px, 500));
        button->setCursor(Qt::PointingHandCursor);
        zoom_layout->addWidget(button);
    }
    zoom_fit_->setChecked(true);
    QObject::connect(zoom_fit_, &QToolButton::clicked, this, [this] {
        if (zoom_handler_)
            zoom_handler_(true);
    });
    QObject::connect(zoom_1to1_, &QToolButton::clicked, this, [this] {
        if (zoom_handler_)
            zoom_handler_(false);
    });

    set_tokens(theme::tokens(theme::system_theme_mode()));
}

void PreviewView::set_tokens(const theme::Tokens &tokens) {
    tokens_ = tokens;
    update(); // 舞台内的深色徽标/chip 配色与主题无关（固定深色舞台），故只重绘
}

void PreviewView::set_image(const QImage &image) {
    image_ = image;
    placeholder_.clear();
    pan_ = QPoint(0, 0);
    update();
}

void PreviewView::set_placeholder(const QString &text) {
    image_ = QImage();
    placeholder_ = text;
    pan_ = QPoint(0, 0);
    update();
}

void PreviewView::set_fit(bool fit) {
    if (fit_ == fit)
        return;
    fit_ = fit;
    pan_ = QPoint(0, 0); // 切换缩放 → 复位平移
    zoom_fit_->setChecked(fit);
    zoom_1to1_->setChecked(!fit);
    update();
}

QSize PreviewView::target_size() const {
    if (image_.isNull())
        return {};
    if (!fit_)
        return image_.size();
    // 适应 = 等比铺满舞台（放大/缩小都允许；"适应"与"1:1"的差异对小图同样可辨识）
    const double scale =
        std::min(double(width()) / image_.width(), double(height()) / image_.height());
    return {std::max(1, int(std::lround(image_.width() * scale))),
            std::max(1, int(std::lround(image_.height() * scale)))};
}

QPoint PreviewView::clamp_pan(const QPoint &pan) const {
    const QSize target = target_size();
    const int max_x = std::max(0, (target.width() - width()) / 2);
    const int max_y = std::max(0, (target.height() - height()) / 2);
    return {std::clamp(pan.x(), -max_x, max_x), std::clamp(pan.y(), -max_y, max_y)};
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
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (image_.isNull()) {
        painter.setPen(tokens_.text3);
        painter.setFont(theme::font(theme::Typography::body_px));
        painter.drawText(rect(), Qt::AlignCenter,
                         placeholder_.isEmpty() ? T("未选中文件") : placeholder_);
        return;
    }
    // .pv-stage{overflow:hidden}：图像裁在圆角内
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), tokens_.radius_stage,
                        tokens_.radius_stage);
    painter.setClipPath(clip);
    painter.drawImage(target_rect(), image_);
}

void PreviewView::resizeEvent(QResizeEvent *event) {
    QFrame::resizeEvent(event);
    pan_ = clamp_pan(pan_);
    layout_overlays();
}

void PreviewView::layout_overlays() {
    if (badge_ != nullptr) {
        badge_->adjustSize(); // QSS padding 已计入 sizeHint
        badge_->move(kOverlayMargin, kOverlayMargin);
        badge_->raise();
    }
    if (zoom_row_ != nullptr) {
        zoom_row_->adjustSize();
        const QSize hint = zoom_row_->size();
        zoom_row_->move(std::max(kOverlayMargin, width() - kOverlayMargin - hint.width()),
                        kOverlayMargin);
        zoom_row_->raise();
    }
}

void PreviewView::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || fit_ || image_.isNull()) {
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
    setCursor(Qt::ArrowCursor);
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
    bool zoom_fit = true;
    bool locked = false; // G5 运行期只读（锁打标热键）
    bool hotkeys_enabled = true;
    const pp::ClassRegistry *classes = nullptr; // 不持有（T11 的注册表实例）

    // 加载状态机：attempted_path = 最近一次已发起的请求路径（幂等去重 + 防失败重试风暴；
    // 需要强制重取时走公开的 reload()）；loaded_path = 最近一次**成功出图**的路径
    // （信息行与「已有图」的判据）。
    QString attempted_path;
    QString loaded_path;
    QString info_text;

    // 控件
    PreviewView *view = nullptr;
    QWidget *bar = nullptr;
    QToolButton *prev = nullptr;
    QToolButton *next = nullptr;
    QLabel *pos = nullptr;
    ElideLabel *name = nullptr;
    QWidget *keyhint = nullptr;
    QHBoxLayout *keyhint_layout = nullptr;

    std::unique_ptr<pp::PreviewCache> cache;
    Pool *pool = nullptr;
    Router *router = nullptr;

    explicit Impl(PreviewPanel *owner);
    ~Impl();

    void build_ui();
    void apply_style();
    void rebuild_key_hints();
    void select(int index, bool force);
    void start_load(const QString &path);
    void update_bar();
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
    pool = new Pool();
    pool->impl = this;
    pool->start();
    router = new Router(this);
    if (QApplication::instance() != nullptr)
        QApplication::instance()->installEventFilter(router);
}

PreviewPanel::Impl::~Impl() {
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
    view->set_zoom_handler([this](bool fit) {
        if (zoom_fit == fit)
            return;
        q->set_zoom_fit(fit);
    });
    auto *stage_margin = new QHBoxLayout();
    stage_margin->setContentsMargins(kStageMarginX, 0, kStageMarginX, 0);
    stage_margin->addWidget(view);
    outer->addLayout(stage_margin, 1);

    bar = new QWidget(q);
    bar->setObjectName(QStringLiteral("pp-preview-bar"));
    auto *bar_layout = new QHBoxLayout(bar);
    bar_layout->setContentsMargins(kBarPadX, kBarPadTop, kBarPadX, kBarPadBottom);
    bar_layout->setSpacing(kBarGap);

    const auto make_nav = [this, bar_layout](const char *object_name, const char *glyph,
                                             const char *tip) {
        auto *button = new QToolButton(bar);
        button->setObjectName(QString::fromLatin1(object_name));
        button->setText(QString::fromUtf8(glyph));
        button->setToolTip(T(tip));
        button->setFixedSize(kNavButtonPx, kNavButtonPx);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        bar_layout->addWidget(button);
        return button;
    };
    prev = make_nav("pp-preview-prev", "◀", "上一张 (←)");
    next = make_nav("pp-preview-next", "▶", "下一张 (→)");
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
    // 只给本面板自有的 pp-* 钩子写样式（舞台框 #pp-preview-stage 归 ui/theme.h 的骨架 QSS）。
    QString qss;
    qss += QStringLiteral("QLabel#pp-preview-badge { color: %1; background: %2; border: 1px solid "
                          "%3; border-radius: %4px; padding: %5px %6px; }\n")
               .arg(QString::fromLatin1(kOverlayFg), QString::fromLatin1(kOverlayBg),
                    QString::fromLatin1(kOverlayBd))
               .arg(kOverlayRadius)
               .arg(kOverlayPadY)
               .arg(kOverlayPadX);
    qss += QStringLiteral("QToolButton#pp-preview-zoom-fit, QToolButton#pp-preview-zoom-1to1 { "
                          "color: %1; background: %2; border: 1px solid %3; border-radius: %4px; "
                          "padding: %5px %6px; }\n")
               .arg(QString::fromLatin1(kOverlayFg), QString::fromLatin1(kOverlayBg),
                    QString::fromLatin1(kOverlayBd))
               .arg(kChipRadius)
               .arg(kChipPadY)
               .arg(kChipPadX);
    qss += QStringLiteral("QToolButton#pp-preview-zoom-fit:checked, "
                          "QToolButton#pp-preview-zoom-1to1:checked { background: %1; "
                          "border-color: %2; color: #ffffff; }\n")
               .arg(QString::fromLatin1(kOverlayActiveBg), QString::fromLatin1(kOverlayActiveBd));
    qss += QStringLiteral("QToolButton#pp-preview-prev, QToolButton#pp-preview-next { color: %1; "
                          "background: %2; border: 1px solid %3; border-radius: %4px; }\n")
               .arg(theme::css_color(tokens.text2), theme::css_color(tokens.control),
                    theme::css_color(tokens.control_bd))
               .arg(kNavButtonRadius);
    qss += QStringLiteral("QToolButton#pp-preview-prev:disabled, "
                          "QToolButton#pp-preview-next:disabled { color: %1; }\n")
               .arg(theme::css_color(tokens.text3));
    qss += QStringLiteral("QLabel#pp-preview-pos, QLabel#pp-preview-name, "
                          "QLabel#pp-preview-keyhint-label, QLabel#pp-preview-keyname { color: %1; "
                          "}\n")
               .arg(theme::css_color(tokens.text3));
    qss +=
        QStringLiteral("QLabel#pp-preview-key { color: %1; background: %2; border: 1px solid %3; "
                       "border-radius: %4px; padding: %5px %6px; }\n")
            .arg(theme::css_color(tokens.text2), theme::css_color(tokens.control),
                 theme::css_color(tokens.control_bd))
            .arg(kChipRadius)
            .arg(kKeyChipPadY)
            .arg(kKeyChipPadX);
    q->setStyleSheet(qss);
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
    const auto add_chip = [this](const QString &key, const QString &label) {
        auto *chip = new ClipLabel(key, keyhint);
        chip->setObjectName(QStringLiteral("pp-preview-key"));
        chip->setFont(theme::font(theme::Typography::badge_px, theme::Typography::badge_weight));
        chip->setAlignment(Qt::AlignCenter);
        keyhint_layout->addWidget(chip);
        auto *name_label = new ClipLabel(label, keyhint);
        name_label->setObjectName(QStringLiteral("pp-preview-keyname"));
        name_label->setFont(theme::font(theme::Typography::hint_px));
        keyhint_layout->addWidget(name_label);
    };
    auto *title = new ClipLabel(T("快速打标"), keyhint);
    title->setObjectName(QStringLiteral("pp-preview-keyhint-label"));
    title->setFont(theme::font(theme::Typography::hint_px));
    keyhint_layout->addWidget(title);
    // 热键表 = 注册表的 classes（hotkey ≠ 0，§6.2）+ 保留的「0 清除」
    if (classes != nullptr) {
        for (const pp::ClassDef &def : classes->classes) {
            if (def.hotkey == 0)
                continue;
            add_chip(QString(QChar(def.hotkey)), QString::fromStdString(def.name));
        }
    }
    add_chip(QStringLiteral("0"), T("清除"));
    keyhint->setEnabled(!locked);
}

void PreviewPanel::Impl::update_bar() {
    const bool has_files = current >= 0 && current < files.size();
    pos->setText(has_files ? QStringLiteral("%1 / %2").arg(current + 1).arg(files.size())
                           : QString());
    name->set_full_text(has_files ? QFileInfo(files.at(current)).fileName() : QString());
    prev->setEnabled(has_files && current > 0);
    next->setEnabled(has_files && current + 1 < files.size());
}

void PreviewPanel::Impl::select(int index, bool force) {
    if (index < 0 || index >= files.size())
        index = files.isEmpty() ? -1 : 0; // 无选中 → 首个文件（§5.2 同一条跟随口径）
    current = index;
    update_bar();
    if (index < 0) {
        view->set_placeholder(T("（无文件可预览）"));
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
        std::printf("preview: index=%d load_ms=%lld failed err=\"%s\"\n", index, ms,
                    result.error.c_str());
        std::fflush(stdout);
        emit q->display_changed();
        emit q->preview_loaded(index, ms, false);
        return;
    }
    view->set_image(result.image);
    loaded_path = index >= 0 && index < files.size() ? files.at(index) : QString();
    // 信息行（§6.1「文件名/尺寸/位深/色彩空间随图显示」；尺寸/位深 = 源文件 probe 摘要）
    info_text = QStringLiteral("%1×%2 · %3 bit · %4")
                    .arg(result.info.width)
                    .arg(result.info.height)
                    .arg(result.info.src_bitdepth)
                    .arg(QString::fromStdString(result.color_space));
    std::printf("preview: index=%d px=%dx%d from_embedded=%d load_ms=%lld info=\"%s\"\n", index,
                result.image.width(), result.image.height(), result.from_embedded ? 1 : 0, ms,
                qUtf8Printable(info_text));
    std::fflush(stdout);
    emit q->display_changed();
    emit q->preview_loaded(index, ms, true);
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
        q->previous();
        return true;
    case Qt::Key_Right:
        q->next();
        return true;
    default:
        break;
    }
    const QString text = key_event->text();
    if (hotkeys_enabled && !locked && text.size() == 1 && hotkey_taken(text.at(0))) {
        emit_hotkey(text.at(0).toLatin1()); // 打标接线位（T11 接通）
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
    if (d.current + 1 < d.files.size())
        d.select(d.current + 1, /*force=*/false);
}

void PreviewPanel::previous() {
    Impl &d = *impl_;
    if (d.current > 0)
        d.select(d.current - 1, /*force=*/false);
}

void PreviewPanel::set_zoom_fit(bool fit) {
    Impl &d = *impl_;
    if (d.zoom_fit == fit)
        return;
    d.zoom_fit = fit;
    d.view->set_fit(fit);
    emit zoom_changed(fit);
}

bool PreviewPanel::zoom_fit() const { return impl_->zoom_fit; }

void PreviewPanel::set_class_registry(const pp::ClassRegistry *registry) {
    Impl &d = *impl_;
    d.classes = registry;
    d.rebuild_key_hints();
}

void PreviewPanel::set_locked(bool locked) {
    Impl &d = *impl_;
    d.locked = locked;
    d.keyhint->setEnabled(!locked); // 运行期只读：打标热键置灰
}

void PreviewPanel::set_hotkeys_enabled(bool on) { impl_->hotkeys_enabled = on; }

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
    if (d.loaded_path == d.files.at(d.current))
        return d.info_text;
    return tr("载入中…");
}

QString PreviewPanel::current_position_text() const {
    const Impl &d = *impl_;
    if (d.current < 0 || d.current >= d.files.size())
        return {};
    return QStringLiteral("%1 / %2").arg(d.current + 1).arg(d.files.size());
}

QString PreviewPanel::current_path() const {
    const Impl &d = *impl_;
    if (d.current < 0 || d.current >= d.files.size())
        return {};
    return d.files.at(d.current);
}

bool PreviewPanel::image_visible() const {
    const Impl &d = *impl_;
    return d.current >= 0 && d.current < d.files.size() && d.loaded_path == d.files.at(d.current);
}

QSize PreviewPanel::displayed_size() const { return impl_->view->displayed_size(); }

int PreviewPanel::pending_requests() const {
    const std::lock_guard<std::mutex> lock(impl_->pool->mu);
    return static_cast<int>(impl_->pool->queue.size());
}

} // namespace pp::ui
