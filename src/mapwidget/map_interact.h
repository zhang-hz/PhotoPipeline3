// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 地图交互状态机纯函数（B.5 拆分步⑥：交互状态机）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.5 步⑥（输入事件 → Action 纯函数化）；
// 迁出自 mapwidget.cpp 的 on_mouse_*/on_wheel（M7-R2.2-G2，语义逐位一致）：
//   * pointer_step = 按键三态机（press/move/release）：4px 点击阈值、按住左键才算拖拽、
//     首拖以「按下点 → 当前点」整段位移补全（不吞 4px 阈值内的位移）、抬起位移 < 阈值 = 点击；
//   * pan / wheel_zoom = 视口纯几何（世界像素换算走 map_view 单源；滚轮 = 光标下地理点不动）；
//   * hit_test_pins = 结果钉屏距命中（≤ hit_radius 的最近者；并列先者优先）。
// 纯函数、零 Qt Widgets（QPointF/QSize = QtCore/Gui）；tests/unit/test_gcj02.cpp 钉真值表。
#pragma once

#include "mapwidget/map_view.h"

#include <QLineF>
#include <QPointF>
#include <QSize>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <vector>

namespace pp::map::interact {

// 视口（**内部基准面**坐标；公共坐标仍 WGS-84 —— datum 换算只在 MapWidget 边界做）
struct Viewport {
    double center_lat = 0;
    double center_lon = 0;
    int zoom = 0;
};

// 按键状态（= 原 Impl 的 pressed/dragging/press_pos/last_drag_pos 四件）
struct DragState {
    bool pressed = false;
    bool dragging = false;
    QPointF press_pos;
    QPointF last_drag_pos;
};

enum class PointerKind { Press, Move, Release };

enum class PointerAction {
    Ignore,  // 事件未消费（原 handler return false）
    Consume, // 已消费、无几何副作用
    Pan,     // 拖拽平移（pan_delta = 光标位移；地图内容跟手 = 中心走 −delta）
    Click,   // 抬起且位移 < 阈值（pin 命中优先于取点，由调用方判）
};

struct PointerStep {
    DragState state;
    PointerAction action = PointerAction::Ignore;
    QPointF pan_delta;    // Pan 时有效
    bool consume = false; // = 原 handler 的返回值（事件过滤器消费语义）
};

// 点击阈值（frozen: press/release 位移 < 4 px = click）
inline constexpr double kClickThresholdPx = 4.0;

// 输入事件 → (新状态, Action)。left_button = 本次事件 button==Left（press/release）；
// left_held = move 时左键保持（buttons & Left）。
inline PointerStep pointer_step(const DragState &s, PointerKind kind, QPointF pos, bool left_button,
                                bool left_held) {
    PointerStep out;
    out.state = s;
    switch (kind) {
    case PointerKind::Press:
        if (!left_button) {
            return out; // Ignore
        }
        out.state.pressed = true;
        out.state.dragging = false;
        out.state.press_pos = pos;
        out.state.last_drag_pos = pos;
        out.action = PointerAction::Consume;
        out.consume = true;
        return out;
    case PointerKind::Move: {
        if (!s.pressed || !left_held) {
            return out; // Ignore（悬停移动不消费）
        }
        if (!out.state.dragging && QLineF(out.state.press_pos, pos).length() >= kClickThresholdPx) {
            out.state.dragging = true;
            // 首拖：整段按下→当前位移一次给足（不吞阈值内已走的像素）
            out.state.last_drag_pos = out.state.press_pos;
        }
        if (out.state.dragging) {
            out.pan_delta = pos - out.state.last_drag_pos;
            out.state.last_drag_pos = pos;
            out.action = out.pan_delta.isNull() ? PointerAction::Consume : PointerAction::Pan;
        } else {
            out.action = PointerAction::Consume;
        }
        out.consume = true;
        return out;
    }
    case PointerKind::Release: {
        if (!left_button || !s.pressed) {
            return out; // Ignore
        }
        const double moved = QLineF(out.state.press_pos, pos).length();
        const bool was_drag = out.state.dragging;
        out.state.pressed = false;
        out.state.dragging = false;
        out.action = (!was_drag && moved < kClickThresholdPx) ? PointerAction::Click
                                                              : PointerAction::Consume;
        out.consume = true;
        return out;
    }
    }
    return out;
}

// 滚轮 → 目标档（dy 同号取 ±1；clamp 后同档 = 无变化）
inline int wheel_target_zoom(int zoom, int dy, int min_z, int max_z) {
    if (dy == 0) {
        return zoom;
    }
    return std::clamp(zoom + (dy > 0 ? 1 : -1), min_z, max_z);
}

// 拖拽平移（纯几何）：地图内容跟手 = 中心走 −delta（纬度夹 Mercator 带，R-103）
inline Viewport pan(const Viewport &v, QPointF delta) {
    const QPointF center_world = latlon_to_world(v.center_lat, v.center_lon, v.zoom);
    const std::pair<double, double> next =
        world_to_latlon(center_world - QPointF(delta.x(), delta.y()), v.zoom);
    return Viewport{clamp_lat(next.first), next.second, v.zoom};
}

// 光标锚点缩放（纯几何）：光标下地理点不动（frozen 滚轮语义）；同档 = 原视口返回
inline Viewport wheel_zoom(const Viewport &v, int dy, QPointF cursor, QSize widget_size, int min_z,
                           int max_z) {
    const int target = wheel_target_zoom(v.zoom, dy, min_z, max_z);
    if (target == v.zoom) {
        return v;
    }
    const QPointF center_world = latlon_to_world(v.center_lat, v.center_lon, v.zoom);
    const QPointF half(widget_size.width() / 2.0, widget_size.height() / 2.0);
    const std::pair<double, double> anchor =
        world_to_latlon(center_world + cursor - half, v.zoom); // screen_to_world
    const QPointF anchor_world = latlon_to_world(anchor.first, anchor.second, target);
    const std::pair<double, double> next = world_to_latlon(anchor_world - (cursor - half), target);
    return Viewport{clamp_lat(next.first), next.second, target};
}

// 结果钉命中：屏距 ≤ hit_radius 的最近者（并列先者优先）；未命中 = -1
inline int hit_test_pins(const std::vector<QPointF> &pin_screen, QPointF cursor,
                         double hit_radius) {
    int hit = -1;
    double best = 0.0;
    for (std::size_t i = 0; i < pin_screen.size(); ++i) {
        const double dist = QLineF(pin_screen[i], cursor).length();
        if (dist > hit_radius) {
            continue;
        }
        if (hit < 0 || dist < best) {
            hit = int(i);
            best = dist;
        }
    }
    return hit;
}

} // namespace pp::map::interact
