// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-T9）— 无边框窗口平台接线实现（设计 §9.1 无边框能力表）
//
// 平台差异只有两处（能力表明文，不算"双实现"）：
//   * 消息入口：Windows = WM_NCHITTEST（QAbstractNativeEventFilter）；其它 = qApp 事件过滤器
//     上的 MouseButtonPress（QWindow::startSystemMove/startSystemResize）。
//   * 命令出口：Windows = SendMessage(WM_SYSCOMMAND, SC_*)；其它 = QWidget::showMinimized /
//     showMaximized|showNormal / close。
// 命中判定（hit_test）、命中区构建（build_box）、常量映射只有一份。

#include "platform/frameless.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM
#endif

namespace pp::platform {

namespace {

qreal sane_dpr(qreal dpr) noexcept { return dpr > 0.01 ? dpr : 1.0; }

} // namespace

#ifdef _WIN32
// 命中区 → Win32 HT* 常量（能力表逐行：HTCAPTION / HTLEFT…HTBOTTOMRIGHT / HTMAXBUTTON）
int native_hit_code(HitZone zone) {
    switch (zone) {
    case HitZone::Caption:
        return HTCAPTION;
    case HitZone::MinButton:
        return HTMINBUTTON;
    case HitZone::MaxButton:
        return HTMAXBUTTON; // Win11 悬停 → Snap Layouts
    case HitZone::CloseButton:
        return HTCLOSE;
    case HitZone::Left:
        return HTLEFT;
    case HitZone::Right:
        return HTRIGHT;
    case HitZone::Top:
        return HTTOP;
    case HitZone::Bottom:
        return HTBOTTOM;
    case HitZone::TopLeft:
        return HTTOPLEFT;
    case HitZone::TopRight:
        return HTTOPRIGHT;
    case HitZone::BottomLeft:
        return HTBOTTOMLEFT;
    case HitZone::BottomRight:
        return HTBOTTOMRIGHT;
    default:
        return HTCLIENT;
    }
}

// HT* → 命中区（探针用；与 native_hit_code 互逆）
HitZone hit_zone_from_native(int code) {
    switch (code) {
    case HTCAPTION:
        return HitZone::Caption;
    case HTMINBUTTON:
        return HitZone::MinButton;
    case HTMAXBUTTON:
        return HitZone::MaxButton;
    case HTCLOSE:
        return HitZone::CloseButton;
    case HTLEFT:
        return HitZone::Left;
    case HTRIGHT:
        return HitZone::Right;
    case HTTOP:
        return HitZone::Top;
    case HTBOTTOM:
        return HitZone::Bottom;
    case HTTOPLEFT:
        return HitZone::TopLeft;
    case HTTOPRIGHT:
        return HitZone::TopRight;
    case HTBOTTOMLEFT:
        return HitZone::BottomLeft;
    case HTBOTTOMRIGHT:
        return HitZone::BottomRight;
    case HTCLIENT:
        return HitZone::Client;
    default:
        return HitZone::None;
    }
}
#endif

QPoint to_physical(const QPoint &logical, qreal dpr) noexcept {
    const qreal f = sane_dpr(dpr);
    return QPoint(static_cast<int>(std::lround(logical.x() * f)),
                  static_cast<int>(std::lround(logical.y() * f)));
}

QRect to_physical(const QRect &logical, qreal dpr) noexcept {
    const qreal f = sane_dpr(dpr);
    const int x1 = static_cast<int>(std::lround(logical.x() * f));
    const int y1 = static_cast<int>(std::lround(logical.y() * f));
    const int x2 = static_cast<int>(std::lround((logical.x() + logical.width()) * f));
    const int y2 = static_cast<int>(std::lround((logical.y() + logical.height()) * f));
    return QRect(x1, y1, x2 - x1, y2 - y1);
}

HitZone probe_native_hit_test(QWidget *window, const QPoint &physical_point, bool *supported) {
    if (supported != nullptr)
        *supported = false;
#ifdef _WIN32
    if (window == nullptr)
        return HitZone::None;
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    // offscreen 平台的 winId() 不是真窗口（IsWindow 为假）→ 无原生命中测试可言，如实返回
    if (handle == nullptr || !::IsWindow(handle))
        return HitZone::None;
    if (supported != nullptr)
        *supported = true;
    // WM_NCHITTEST 的 lParam = 屏幕物理像素坐标（short 打包）
    const LPARAM packed =
        MAKELPARAM(static_cast<short>(physical_point.x()), static_cast<short>(physical_point.y()));
    const LRESULT code = ::SendMessageW(handle, WM_NCHITTEST, 0, packed);
    return hit_zone_from_native(static_cast<int>(code));
#else
    (void)window;
    (void)physical_point;
    return HitZone::None;
#endif
}

HitZone hit_test(const HitBox &box, const QPoint &p) noexcept {
    if (!box.window.contains(p))
        return HitZone::None;
    // 硬约束 2：最大化（全客户区）时缩放带收敛为 1px，仍然可命中
    const int border = box.maximized ? kResizeBorderMaximizedPx : std::max(box.border, 0);
    if (border <= 0)
        return box.caption.contains(p) ? HitZone::Caption : HitZone::Client;
    const bool left = p.x() < box.window.left() + border;
    const bool right = p.x() >= box.window.left() + box.window.width() - border;
    const bool top = p.y() < box.window.top() + border;
    const bool bottom = p.y() >= box.window.top() + box.window.height() - border;
    if (top && left)
        return HitZone::TopLeft;
    if (top && right)
        return HitZone::TopRight;
    if (bottom && left)
        return HitZone::BottomLeft;
    if (bottom && right)
        return HitZone::BottomRight;
    if (left)
        return HitZone::Left;
    if (right)
        return HitZone::Right;
    if (top)
        return HitZone::Top;
    if (bottom)
        return HitZone::Bottom;
    // 三钮先于拖拽：三钮同时也是 drag_excludes 成员，顺序不可颠倒
    if (!box.close_button.isNull() && box.close_button.contains(p))
        return HitZone::CloseButton;
    if (!box.max_button.isNull() && box.max_button.contains(p))
        return HitZone::MaxButton;
    if (!box.min_button.isNull() && box.min_button.contains(p))
        return HitZone::MinButton;
    if (box.caption.contains(p)) {
        // 硬约束 1：控件（步骤切换/图标钮/滑条…）区域不触发拖拽
        for (const QRect &exclude : box.drag_excludes) {
            if (!exclude.isNull() && exclude.contains(p))
                return HitZone::Client;
        }
        return HitZone::Caption;
    }
    return HitZone::Client;
}

struct Frameless::Impl {
    QWidget *window = nullptr;
    QWidget *caption = nullptr;
    std::vector<QWidget *> explicit_excludes;
    std::vector<QWidget *> drag_excludes; // 三钮 + 显式清单（去重后）
    QWidget *min_button = nullptr;
    QWidget *max_button = nullptr;
    QWidget *close_button = nullptr;
    bool intercept = false;
    CaptionAction last_action = CaptionAction::Minimize;
    int action_count = 0;

    void refresh_excludes() {
        drag_excludes = explicit_excludes;
        for (QWidget *button : {min_button, max_button, close_button}) {
            if (button != nullptr && std::find(drag_excludes.begin(), drag_excludes.end(),
                                               button) == drag_excludes.end())
                drag_excludes.push_back(button);
        }
    }

    // 子控件矩形：相对窗口原点；physical=true → 乘 devicePixelRatio（DPI 口径，硬约束 3）
    QRect rect_of(QWidget *child, bool physical) const {
        if (child == nullptr || window == nullptr)
            return QRect();
        const QPoint rel = child->mapToGlobal(QPoint(0, 0)) - window->mapToGlobal(QPoint(0, 0));
        if (!physical)
            return QRect(rel, child->size());
        const qreal dpr = sane_dpr(window->devicePixelRatioF());
        const QSize size(static_cast<int>(std::lround(child->width() * dpr)),
                         static_cast<int>(std::lround(child->height() * dpr)));
        return QRect(to_physical(rel, dpr), size);
    }

    HitBox build_box(bool physical) const {
        HitBox box;
        if (window == nullptr)
            return box;
        const QPoint origin = window->mapToGlobal(QPoint(0, 0));
        const QRect logical_window(origin, window->size());
        box.window =
            physical ? to_physical(logical_window, window->devicePixelRatioF()) : logical_window;
        const auto shift = [&box](const QRect &r) {
            return r.isNull() ? r : r.translated(box.window.topLeft());
        };
        box.caption = shift(rect_of(caption, physical));
        box.min_button = shift(rect_of(min_button, physical));
        box.max_button = shift(rect_of(max_button, physical));
        box.close_button = shift(rect_of(close_button, physical));
        for (QWidget *child : drag_excludes) {
            const QRect r = shift(rect_of(child, physical));
            if (!r.isNull())
                box.drag_excludes.push_back(r);
        }
        box.border = kResizeBorderPx;
        box.maximized = window != nullptr && window->isMaximized();
        return box;
    }
};

namespace {

Qt::Edges edges_for(HitZone zone) {
    switch (zone) {
    case HitZone::Left:
        return Qt::LeftEdge;
    case HitZone::Right:
        return Qt::RightEdge;
    case HitZone::Top:
        return Qt::TopEdge;
    case HitZone::Bottom:
        return Qt::BottomEdge;
    case HitZone::TopLeft:
        return Qt::LeftEdge | Qt::TopEdge;
    case HitZone::TopRight:
        return Qt::RightEdge | Qt::TopEdge;
    case HitZone::BottomLeft:
        return Qt::LeftEdge | Qt::BottomEdge;
    case HitZone::BottomRight:
        return Qt::RightEdge | Qt::BottomEdge;
    default:
        return Qt::Edges();
    }
}

} // namespace

#ifdef _WIN32
// 常量值必须与 SDK 逐字一致（本文件在非 Windows 平台同样编译，故数值写在头文件里）
static_assert(kScMinimize == SC_MINIMIZE, "kScMinimize 必须等于 Win32 SC_MINIMIZE");
static_assert(kScMaximize == SC_MAXIMIZE, "kScMaximize 必须等于 Win32 SC_MAXIMIZE");
static_assert(kScClose == SC_CLOSE, "kScClose 必须等于 Win32 SC_CLOSE");
#endif

Frameless::Frameless(QWidget *window, QObject *parent) : QObject(parent), d_(new Impl) {
    d_->window = window;
}
Frameless::~Frameless() {
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
        QCoreApplication::instance()->removeEventFilter(this);
    }
}

Frameless *Frameless::attach(QWidget *window, QWidget *caption,
                             std::vector<QWidget *> drag_excludes, QObject *parent) {
    if (window == nullptr)
        return nullptr;
    // 设计 §9.1：最终 GUI 不带系统边框/系统标题栏
    window->setWindowFlag(Qt::FramelessWindowHint, true);
    auto *frameless = new Frameless(window, parent != nullptr ? parent : window);
    frameless->d_->caption = caption;
    frameless->d_->explicit_excludes = std::move(drag_excludes);
    frameless->d_->refresh_excludes();
    if (QCoreApplication::instance() != nullptr) {
        if (native_hit_test_supported()) {
            QCoreApplication::instance()->installNativeEventFilter(frameless);
        } else {
            // 全局事件过滤：顶栏空白按下 → startSystemMove；边缘按下 → startSystemResize
            QCoreApplication::instance()->installEventFilter(frameless);
        }
    }
    return frameless;
}

void Frameless::set_caption_buttons(QWidget *minimize, QWidget *maximize, QWidget *close) {
    d_->min_button = minimize;
    d_->max_button = maximize;
    d_->close_button = close;
    d_->refresh_excludes();
}

void Frameless::set_intercept_actions(bool intercept) { d_->intercept = intercept; }
bool Frameless::intercept_actions() const { return d_->intercept; }
CaptionAction Frameless::last_action() const { return d_->last_action; }
int Frameless::action_count() const { return d_->action_count; }

bool Frameless::window_is_maximized() const {
    return d_->window != nullptr && d_->window->isMaximized();
}

bool Frameless::native_hit_test_supported() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

HitZone Frameless::classify(const QPoint &physical_point) const {
    return hit_test(d_->build_box(true), physical_point);
}

HitZone Frameless::classify_logical(const QPoint &logical_point) const {
    return hit_test(d_->build_box(false), logical_point);
}

void Frameless::trigger(CaptionAction action) {
    ++d_->action_count;
    d_->last_action = action;
    emit caption_action(action);
    if (d_->intercept || d_->window == nullptr)
        return;
    QWidget *window = d_->window;
#ifdef _WIN32
    // 能力表：Windows 三钮 = WM_SYSCOMMAND(SC_MINIMIZE/SC_MAXIMIZE/SC_CLOSE)
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    if (handle != nullptr) {
        ::SendMessageW(handle, WM_SYSCOMMAND, static_cast<WPARAM>(syscommand_for(action)), 0);
        return;
    }
#endif
    // 能力表：其它平台 = showMinimized / showMaximized / close（Windows 无 HWND 时同此兜底）
    switch (action) {
    case CaptionAction::Minimize:
        window->showMinimized();
        break;
    case CaptionAction::Maximize:
        if (window->isMaximized())
            window->showNormal();
        else
            window->showMaximized();
        break;
    case CaptionAction::Close:
        window->close();
        break;
    }
}

bool Frameless::eventFilter(QObject *watched, QEvent *event) {
    if (native_hit_test_supported() || d_->window == nullptr)
        return QObject::eventFilter(watched, event); // Windows 走 WM_NCHITTEST，不重复处理
    if (event->type() != QEvent::MouseButtonPress)
        return QObject::eventFilter(watched, event);
    auto *widget = qobject_cast<QWidget *>(watched);
    if (widget == nullptr || widget->window() != d_->window)
        return QObject::eventFilter(watched, event);
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton)
        return QObject::eventFilter(watched, event);
    QWindow *handle = d_->window->windowHandle();
    if (handle == nullptr)
        return QObject::eventFilter(watched, event);
    const HitZone zone = classify_logical(mouse->globalPosition().toPoint());
    if (zone == HitZone::Caption && handle->startSystemMove())
        return true;
    const Qt::Edges edges = edges_for(zone);
    if (edges != Qt::Edges() && handle->startSystemResize(edges))
        return true;
    return QObject::eventFilter(watched, event);
}

bool Frameless::nativeEventFilter(const QByteArray &event_type, void *message, qintptr *result) {
    (void)event_type;
#ifdef _WIN32
    if (message == nullptr || result == nullptr || d_->window == nullptr)
        return false;
    auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_NCHITTEST)
        return false;
    const HWND handle = reinterpret_cast<HWND>(d_->window->winId());
    if (handle == nullptr || msg->hwnd != handle)
        return false;
    // WM_NCHITTEST 的 lParam = 屏幕**物理**像素（硬约束 3）
    const QPoint point(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
    const HitZone zone = classify(point);
    if (zone == HitZone::None || zone == HitZone::Client)
        return false; // Client 交回 Qt/DefWindowProc，鼠标事件照常派发
    *result = native_hit_code(zone);
    return true;
#else
    (void)message;
    (void)result;
    return false;
#endif
}

} // namespace pp::platform
