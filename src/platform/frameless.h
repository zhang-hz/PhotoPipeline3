// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.3.0（M4-T9）— 无边框窗口平台接线（设计 §9.1 无边框能力表）
//
// 能力表逐行落地（设计 §9.1）：
//   | 能力            | Windows                                  | Linux
//   | 拖拽移动        | WM_NCHITTEST → HTCAPTION（保留系统拖拽/双击最大化/Aero 摸边）|
//   QWidget::startSystemMove() | 八向边缘缩放    | WM_NCHITTEST → HTLEFT…HTBOTTOMRIGHT（6px
//   边框带）| QWidget::startSystemResize() | 最大化钮 Snap   | 最大化钮 → HTMAXBUTTON（Win11 悬停弹
//   Snap Layouts）| 无对应，普通按钮 | 三钮行为        | WM_SYSCOMMAND
//   SC_MINIMIZE/SC_MAXIMIZE/SC_CLOSE | showMinimized/showMaximized/close | Mica/深色标题栏 | DWM
//   SystemBackdrop + DWMWA_USE_IMMERSIVE_DARK_MODE（沿用 platform/mica）| 主题色随 QPalette
//
// M7-WP5 壳层收口（UX-7 真无边框 + UX-13 圆角，docs/ux-review-14.md §7/§13）：
//   * 真·无边框 = 处理 WM_NCCALCSIZE 把客户区扩到整窗（原生标题栏/复古三钮整体消失 ——
//     temp_bug.png 上排那组就是未收口时 Windows 画的原生 chrome）；WS_THICKFRAME 显式保留
//     （Win11 圆角/阴影/八向缩放/Snap 全靠它）；最大化时客户区钳到显示器工作区（不盖任务栏）。
//   * 三钮命中**优先于**边缘缩放带（一并修 R-312：三钮顶部/右缘 6px 不再被缩放带啃掉）；
//     顶栏 6px 缩放带只在三钮之外生效。
//   * Win11 圆角 = DWMWA_WINDOW_CORNER_PREFERENCE=DWMWCP_ROUND（platform/mica 同文件收口）；
//     Win10 不支持 → 静默忽略。主窗 + settings/exif/presets/map 四弹窗同走 attach 同一口径。
//
// 硬约束（设计 §9.1）：
//   1) 全窗口可拖区 = 顶栏空白处；控件/列表/滑条区域不触发拖拽（drag_excludes + 命中区顺序）。
//   2) 最大化时进入全客户区仍保留 1px 缩放边命中（border = 1）。
//   3) DPI/多显示器下命中测试按物理像素（Windows 路径全程在物理像素域；Qt 事件路径用逻辑像素，
//      两条路径共用同一个 hit_test()；rect_of 的物理换算复用 to_physical —— R-308 单轨取整）。
//
// 本文件是"机制分派"而非"双实现"：命中判定、三钮路由、命令常量只有一份，平台差异只在
// "怎么把消息送进来/怎么把命令发出去"（设计 §9.1 明文能力表，不算平台分叉）。

#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QtGlobal>

#include <memory>
#include <vector>

class QWidget;

namespace pp::platform {

// 边缘缩放带宽度（设计 §9.1「6px 边框带」；最大化时收敛为 1px —— 见 Metrics 同名口径。
// **单源规则（M4-W2-fix 第 8 条）**：这两个常量是唯一真源；ui/theme.h 的
// Metrics::resize_border / resize_border_max 只是转发，不得再写第二份数值。）
inline constexpr int kResizeBorderPx = 6;
inline constexpr int kResizeBorderMaximizedPx = 1;

// caption 三钮命中区高度（设计 §9.1/§3.3 逐字：「Win11 规格 46×32」，M5-T4 复述：46×32、透明底、
// 悬停灰、关闭悬停 #c42b1c）。
// **单轨（M5-T4，裁定 A1）**：按钮可见面与命中区同为 46×32 → 命中几何 = 按钮矩形；本常量仍是
// 命中几何的唯一真源（window 侧 rect_of/build_box 消费，theme::Metrics::caption_btn_hit_h 转发），
// 只是"命中带比可见面矮"的双轨收敛（M4：46×满高控件内的居中 32px 带）随顶栏 44 换锚不再触发。
// 命中**语义**零改动：带外按下不触发三钮、HTMINBUTTON/HTMAXBUTTON/HTCLOSE 与 Snap 全保留。
inline constexpr int kCaptionHitHeightPx = 32;

// 命中区。Windows 侧映射到 HT* 常量（映射表在 frameless.cpp，Windows 段内有 static_assert
// 与 SDK 宏逐值核对）；其它平台只用 Caption（拖拽）与四边/四角（缩放）。
enum class HitZone {
    None,
    Client,
    Caption,
    MinButton,
    MaxButton,
    CloseButton,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

enum class CaptionAction { Minimize, Maximize, Close };

// WM_SYSCOMMAND 的 wParam（Win32 ABI 值；与 SDK 的 SC_* 逐值核对在 .cpp 的 Windows 段）
inline constexpr long kScMinimize = 0xF020;
inline constexpr long kScMaximize = 0xF030;
inline constexpr long kScClose = 0xF060;

inline constexpr long syscommand_for(CaptionAction action) noexcept {
    switch (action) {
    case CaptionAction::Minimize:
        return kScMinimize;
    case CaptionAction::Maximize:
        return kScMaximize;
    case CaptionAction::Close:
        return kScClose;
    }
    return 0;
}

// 命中判定的输入盒（坐标 = 同一坐标系：Windows = 屏幕物理像素；Qt 事件路径 = 屏幕逻辑像素）
struct HitBox {
    QRect window;                               // 窗口矩形
    QRect caption;                              // 顶栏（可拖区）矩形
    std::vector<QRect> drag_excludes;           // 顶栏内不参与拖拽的控件矩形
    QRect min_button, max_button, close_button; // 三钮命中区（空 = 该钮不存在）
    int border = kResizeBorderPx;               // 边缘缩放带宽度
    bool maximized = false;                     // 最大化 → border 收敛为 1px（硬约束 2）
};

// 纯函数：命中判定（无平台依赖；运行时与自检探针共用同一实现）
HitZone hit_test(const HitBox &box, const QPoint &point) noexcept;

// 逻辑 → 物理换算（DPI：Qt 逻辑坐标 × devicePixelRatio；dpr <= 0 → 原样返回）
QPoint to_physical(const QPoint &logical, qreal dpr) noexcept;
QRect to_physical(const QRect &logical, qreal dpr) noexcept;

// 自检探针（R-308 收口的寄生面之一，仅供测试/探针；生产不消费）：向 window 真发一次
// **原生命中测试消息**（Windows = SendMessage(WM_NCHITTEST)），返回该物理点在 Windows 眼里
// 属于哪个命中区 —— 即"能力表逐行"的端到端取证（HTCAPTION / HTLEFT…HTBOTTOMRIGHT /
// HTMAXBUTTON / HTMINBUTTON / HTCLOSE）。其它平台 *supported=false（命中测试走 Qt 事件路径，
// 无对应消息），返回 HitZone::None。
HitZone probe_native_hit_test(QWidget *window, const QPoint &physical_point, bool *supported);

// 无边框接线：attach(顶层窗口, 顶栏, {不拖拽的子控件}) → set_caption_buttons(最小, 最大, 关闭)
class Frameless : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    // 给 window 装接线并置 Qt::FramelessWindowHint（设计 §9.1「无系统边框/系统标题栏」）。
    // 返回对象挂在 parent（缺省 = window）下；window == nullptr → 返回 nullptr。
    static Frameless *attach(QWidget *window, QWidget *caption,
                             std::vector<QWidget *> drag_excludes, QObject *parent = nullptr);
    ~Frameless() override;

    void set_caption_buttons(QWidget *minimize, QWidget *maximize, QWidget *close);

    // 三钮命令路由（能力表"三钮行为"行，生产路径）。
    void trigger(CaptionAction action);

    // ===== 测试/探针寄生面（R-308 收口；M7-WP5）========================================
    // 以下成员不是生产输入面：仅供 --ui-smoke 自检探针（mainwindow 的 smoke_probe_frameless）
    // 与单测取证**复用同一份实现**（不允许再造第二套判定/路由）。生产命中只走
    // nativeEventFilter/eventFilter → hit_test，生产命令只走 trigger()。新增钩子一律进本区。
    // intercept = true 时 trigger() 只发信号不执行动作（取证用；生产恒 false）。
    void set_intercept_actions(bool intercept);
    bool intercept_actions() const;
    CaptionAction last_action() const;
    int action_count() const;

    // 命中判定（探针复用运行时同一 hit_test）
    HitZone classify(const QPoint &physical_point) const;        // Windows 口径：屏幕物理像素
    HitZone classify_logical(const QPoint &logical_point) const; // Qt 事件口径：屏幕逻辑像素
    bool window_is_maximized() const;

    // Windows = true（WM_NCHITTEST 路径）；其余平台 = false（Qt startSystemMove/Resize 路径）
    static bool native_hit_test_supported();
    // ===== 测试/探针寄生面 END ========================================================

    bool nativeEventFilter(const QByteArray &event_type, void *message, qintptr *result) override;

Q_SIGNALS:
    void caption_action(pp::platform::CaptionAction action);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    explicit Frameless(QWidget *window, QObject *parent);
};

} // namespace pp::platform
