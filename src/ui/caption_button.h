// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — caption 三钮（壳层自绘件；R-279 双实现收编的唯一实现）
//
// 背景（deep-review-0.3.2.md R-279/E6-18）：caption 钮曾有两份手拷 —— mainwindow_impl.h 的
// CaptionButton（图标名/双色/命中带通用）与 settings_dialog.cpp 的 CaptionCloseButton
// （固定关图标、无命中带、图标 12px 漂移）。M7-R2.1-F4 收编为本头唯一实现：settings_dialog
// 直用 CaptionButton（icons::Name::CaptionClose），两实现合并后图标边长统一 kCaptionIconPx=10
// （§3.3 caption 字形盒 10×10；原 settings 侧 12 为漂移，合并顺带归位）。
// 契约（同 mainwindow_impl.h 原注释逐字）：
//   * **单轨（M5-T4，裁定 A1）**：可见面与命中区同为 46×32（theme::Metrics::caption_btn_h ==
//     caption_btn_hit_h == platform::kCaptionHitHeightPx）。命中带 = 按钮矩形（居中的 32px 带
//     在顶栏 44px 内），无边框窗口的 WM_NCHITTEST 用同一条带（platform/frameless.cpp 的
//     rect_of(caption_button) 在"控件高 > 命中带高"时才收敛，单轨下两者恒等）。
//   * 悬停换色由本控件显式落地（enter/leaveEvent）：QIcon 的取色不受 QSS 的 :hover color 影响。
//   * 非冻结面（壳层自绘件；mainwindow_impl.h / settings_dialog.cpp 共用）。
#pragma once

#include "ui/icons.h"
#include "ui/theme.h"

#include <QColor>
#include <QPushButton>
#include <QRect>

namespace pp::ui {

// caption 图标边长（px）：mockup 的 .cap 内联 svg 是 10×10（WinUI3 语言的 caption 字形盒）
inline constexpr int kCaptionIconPx = 10;

class CaptionButton : public QPushButton {
public:
    // normal/hover = 两态令牌色（tokens.css .cap{color:var(--txt)} /
    // .cap.close:hover{color:#fff}）。
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

} // namespace pp::ui
