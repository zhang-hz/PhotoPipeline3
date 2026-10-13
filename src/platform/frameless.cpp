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

#include "platform/mica.h" // apply_window_corner（UX-13 圆角与 DWM 属性同文件收口）

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <bit>
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
namespace {
// Win32 平台互操作的显式安全转换封装（M7R21F4-0，tidy performance-no-int-to-ptr 收口）：
// WId/LPARAM 为整数句柄载体、HWND/NCCALCSIZE_PARAMS* 为指针 —— int↔ptr 惯用式此前散写
// reinterpret_cast 五处。std::bit_cast 位拷贝与 reinterpret_cast 逐位等价（同宽整数↔
// 指针的 trivially-copyable 位复制），行为零变化；调用点收口为命名函数，语义可检索。
inline HWND hwnd_of(WId id) { return std::bit_cast<HWND>(id); }
inline NCCALCSIZE_PARAMS *nccalc_params_of(LPARAM lparam) {
    return std::bit_cast<NCCALCSIZE_PARAMS *>(lparam);
}
} // namespace

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
    const HWND handle = hwnd_of(window->winId());
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
    // M7-WP5（R-312 + UX-7②）：**三钮命中优先于边缘缩放带**。旧序（边/角先判）把三钮的
    // 顶部 6px 与右缘 6px 划给缩放 → 点关闭钮最上 6px 触发缩放、点了"没反应"。新序下
    // 顶栏 6px 缩放带只在三钮之外生效（UX-7 逐字要求），三钮整面可点。
    // 三钮先于拖拽：三钮同时也是 drag_excludes 成员，顺序不可颠倒。
    if (!box.close_button.isNull() && box.close_button.contains(p))
        return HitZone::CloseButton;
    if (!box.max_button.isNull() && box.max_button.contains(p))
        return HitZone::MaxButton;
    if (!box.min_button.isNull() && box.min_button.contains(p))
        return HitZone::MinButton;
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
    // M7-WP5（R-310）：缓存的 QWidget* 全部改 QPointer —— 事件过滤器在控件销毁期仍可能
    // 收到消息（销毁顺序：子控件先于父窗），裸指针判定 hit 是低概率 UB。QPointer 悬空即空，
    // rect_of/build_box/trigger 全部把空当"该件不存在"，与"未接线"同语义。
    QPointer<QWidget> window;
    QPointer<QWidget> caption;
    std::vector<QPointer<QWidget>> explicit_excludes;
    std::vector<QPointer<QWidget>> drag_excludes; // 三钮 + 显式清单（去重后）
    QPointer<QWidget> min_button;
    QPointer<QWidget> max_button;
    QPointer<QWidget> close_button;
    bool intercept = false;
    CaptionAction last_action = CaptionAction::Minimize;
    int action_count = 0;

    void refresh_excludes() {
        drag_excludes = explicit_excludes;
        for (const QPointer<QWidget> &button : {min_button, max_button, close_button}) {
            if (button != nullptr && std::find(drag_excludes.begin(), drag_excludes.end(),
                                               button) == drag_excludes.end())
                drag_excludes.push_back(button);
        }
    }

    // 子控件矩形：相对窗口原点；physical=true → 乘 devicePixelRatio（DPI 口径，硬约束 3）。
    // caption_button=true → 纵向收敛到 §9.1/§3.3 的 46×32 命中带（居中的 kCaptionHitHeightPx 高）。
    // M5-T4 单轨（裁定 A1）：按钮可见面本身就是 46×32 → logical.height() == 32，收敛条件不成立，
    // 返回值就是按钮矩形本身（本函数**逻辑零改动**，只是"双轨收敛"随几何换锚不再触发）。
    // M7-WP5（R-308）：physical 分支**复用 to_physical** —— 旧实现对 topLeft 用 lround、
    // 对 width/height 单独 lround（双轨取整），dpr=1.25 等分数比下与 to_physical 的
    // "边取整后作差"能差 1px（命中盒与窗口盒不同源）。统一后全仓只有 to_physical 一处取整。
    QRect rect_of(const QPointer<QWidget> &child, bool physical,
                  bool caption_button = false) const {
        if (child.isNull() || window.isNull())
            return QRect();
        const QPoint rel = child->mapToGlobal(QPoint(0, 0)) - window->mapToGlobal(QPoint(0, 0));
        QRect logical(rel, child->size());
        if (caption_button && logical.height() > kCaptionHitHeightPx) {
            const int y = logical.top() + (logical.height() - kCaptionHitHeightPx) / 2;
            logical = QRect(logical.left(), y, logical.width(), kCaptionHitHeightPx);
        }
        if (!physical)
            return logical;
        return to_physical(logical, window->devicePixelRatioF());
    }

    bool is_caption_button(const QPointer<QWidget> &child) const {
        return !child.isNull() &&
               (child == min_button || child == max_button || child == close_button);
    }

    HitBox build_box(bool physical) const {
        HitBox box;
        if (window.isNull())
            return box;
        const QPoint origin = window->mapToGlobal(QPoint(0, 0));
        const QRect logical_window(origin, window->size());
        box.window =
            physical ? to_physical(logical_window, window->devicePixelRatioF()) : logical_window;
        const auto shift = [&box](const QRect &r) {
            return r.isNull() ? r : r.translated(box.window.topLeft());
        };
        box.caption = shift(rect_of(caption, physical));
        box.min_button = shift(rect_of(min_button, physical, /*caption_button=*/true));
        box.max_button = shift(rect_of(max_button, physical, /*caption_button=*/true));
        box.close_button = shift(rect_of(close_button, physical, /*caption_button=*/true));
        for (const QPointer<QWidget> &child : drag_excludes) {
            // 三钮的排除区 = **命中带**（M5-T4 单轨下 = 按钮矩形 46×32）：带外回落到 Caption
            // （拖拽），若整块排除会变成"点上去既不开窗也不拖窗"的死区。
            const QRect r = shift(rect_of(child, physical, is_caption_button(child)));
            if (!r.isNull())
                box.drag_excludes.push_back(r);
        }
        box.border = kResizeBorderPx;
        box.maximized = window->isMaximized();
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
                             const std::vector<QWidget *> &drag_excludes, QObject *parent) {
    if (window == nullptr)
        return nullptr;
    // 设计 §9.1：最终 GUI 不带系统边框/系统标题栏
    window->setWindowFlag(Qt::FramelessWindowHint, true);
    auto *frameless = new Frameless(window, parent != nullptr ? parent : window);
    frameless->d_->caption = caption;
    frameless->d_->explicit_excludes.reserve(drag_excludes.size());
    for (QWidget *excluded : drag_excludes)
        frameless->d_->explicit_excludes.push_back(excluded);
    frameless->d_->refresh_excludes();
    if (QCoreApplication::instance() != nullptr) {
        if (native_hit_test_supported()) {
            QCoreApplication::instance()->installNativeEventFilter(frameless);
        } else {
            // 全局事件过滤：顶栏空白按下 → startSystemMove；边缘按下 → startSystemResize
            QCoreApplication::instance()->installEventFilter(frameless);
        }
    }
#ifdef _WIN32
    // M7-WP5（UX-7① + UX-13）：真无边框壳层收口。主窗 + 四弹窗（settings/exif/presets/map
    // 同走 Frameless::attach）**同一函数同一口径**：
    //   1) WS_THICKFRAME 显式保留（Win11 圆角与 DWM 阴影的来源；缺了就补，QPA 可能未置）；
    //   2) SWP_FRAMECHANGED 重算一次非客户区 → WM_NCCALCSIZE 走下方过滤器的"整窗客户区"策略
    //      （原生标题栏与复古三钮随之消失，见 nativeEventFilter）；
    //   3) DWMWA_WINDOW_CORNER_PREFERENCE = ROUND（Win10 不支持 → 静默忽略）。
    // offscreen 平台的 winId() 不是真窗口（IsWindow 为假）→ 整段跳过，--ui-smoke 行为不变。
    {
        // M7-R2.4：此处 winId() 是**唯一**有意建窗点（先把原生窗建起来才能改样式）；
        // 递归风险已根治 —— 创建期投递的消息进过滤器只读 internalWinId()=0 直接放行。
        const HWND handle = hwnd_of(window->winId());
        if (handle != nullptr && ::IsWindow(handle)) {
            const LONG_PTR style = ::GetWindowLongPtrW(handle, GWL_STYLE);
            if ((style & WS_THICKFRAME) == 0)
                ::SetWindowLongPtrW(handle, GWL_STYLE, style | WS_THICKFRAME);
            ::SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                               SWP_FRAMECHANGED);
            apply_window_corner(handle);
        }
    }
#endif
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
    if (d_->intercept || d_->window.isNull())
        return;
    QWidget *window = d_->window.data();
#ifdef _WIN32
    // 能力表：Windows 三钮 = WM_SYSCOMMAND(SC_MINIMIZE/SC_MAXIMIZE/SC_CLOSE)。
    // M7-R2.4：只读句柄（不建窗），理由同 nativeEventFilter 处注释。
    const HWND handle = hwnd_of(window->internalWinId());
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
    if (message == nullptr || result == nullptr || d_->window.isNull())
        return false;
    auto *msg = static_cast<MSG *>(message);
    // M7-R2.4（0.3.3 打不开 P0 根治）：**过滤器内禁用 winId()**。winId() 在窗口创建期
    // （attach 的 winId() → CreateWindowEx 同步投递 WM_NCCALCSIZE → 本过滤器）因 data.winid
    // 尚未赋值而重入 createWinId() → 递归建窗直至栈崩（0.3.3 Windows 启动即挂的根因）。
    // internalWinId() 只读当前句柄、永不建窗（qwidget.h:221 取值语义）；未建窗 = 0 →
    // 消息放行 Qt，语义 = "该窗口尚无原生句柄可拦截"。同因禁用 trigger() 的 winId()。
    const HWND handle = hwnd_of(d_->window->internalWinId());
    if (handle == nullptr || msg->hwnd != handle)
        return false;
    // M7-WP5（UX-7①）：真无边框 = **非客户区收口**。只置 Qt::FramelessWindowHint 不处理本消息时，
    // Windows 仍按 WS_CAPTION 族样式保留原生标题栏与复古三钮（temp_bug.png 上排）。此处把
    // 客户区扩到整窗（rgrc[0] = 窗口矩形，返回 0）→ 原生 chrome 整体不存在；
    // WS_THICKFRAME 保留（attach 已确保）→ DWM 阴影/圆角/八向缩放/Snap 全不受影响。
    if (msg->message == WM_NCCALCSIZE) {
        if (msg->wParam != 0) {
            // wParam=TRUE：lParam = NCCALCSIZE_PARAMS*；rgrc[0] 入参 = 拟议的新窗口矩形，
            // 出参 = 新客户区矩形。默认（非最大化）客户区 = 整窗。
            auto *params = nccalc_params_of(msg->lParam);
            if (params != nullptr && ::IsZoomed(msg->hwnd)) {
                // 最大化时窗口矩形会超出显示器一圈（粗框藏在屏幕外）→ 整窗=客户区会让内容
                // 出屏并盖住任务栏。钳到显示器工作区 = 原生最大化窗口的同款语义。
                const HMONITOR monitor = ::MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                if (monitor != nullptr && ::GetMonitorInfoW(monitor, &info))
                    params->rgrc[0] = info.rcWork;
            }
        }
        // wParam=FALSE：lParam = RECT*（单矩形）→ 同口径返回整窗（rgrc 保持原值即整窗）。
        *result = 0;
        return true;
    }
    if (msg->message != WM_NCHITTEST)
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
