// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 逻辑边距（leading/trailing）辅助（M7-R2E2 / R-218）
//
// 契约（docs/deep-review-0.3.2.md R-218/E4-16）：QSS/内距的非对称左右值不得写死 LTR。
// 本头提供**逻辑边距**单源：`set_logical_margins(host, leading, trailing)` 在 LTR 落
// (leading, trailing)、在 RTL 落 (trailing, leading)，并监听 LayoutDirectionChange 重放
// —— 方向切换后内距随动（ui_smoke 的 rtl-mirror 探针钉死）。`set_leading_indent(host, px)`
// = (px, 0) 的行首缩进形态（page_run 行名称缩进 / page_output 提示行对轨缩进）。
//
// header-only（R-109「非 Widget 数据语义已 header-only 可测」同口径）：探针可直接包含并
// 对临时控件断言；LTR 初值与原 setContentsMargins 调用逐字节一致。
#pragma once

#include <QEvent>
#include <QLayout>
#include <QObject>
#include <QWidget>

namespace pp::ui {

// 逻辑边距事件过滤器（host 的子对象，同生灭；每个 host 一个实例）
class LogicalMarginsFilter : public QObject {
public:
    LogicalMarginsFilter(QWidget *host, int leading, int trailing)
        : QObject(host), host_(host), leading_(leading), trailing_(trailing) {
        host->installEventFilter(this);
        apply();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *ev) override {
        if (watched == host_ && ev->type() == QEvent::LayoutDirectionChange)
            apply();
        return QObject::eventFilter(watched, ev);
    }

private:
    void apply() {
        const bool rtl = host_->layoutDirection() == Qt::RightToLeft;
        const int left = rtl ? trailing_ : leading_;
        const int right = rtl ? leading_ : trailing_;
        // 与原调用点同落点：调用方原来是 QLayout::setContentsMargins 就落布局（布局自带样式
        // 默认边距，落 widget 不等于落布局 —— 实测会叠加出 9px 默认边距），是 widget 就落 widget。
        if (QLayout *lay = host_->layout())
            lay->setContentsMargins(left, 0, right, 0);
        else
            host_->setContentsMargins(left, 0, right, 0);
    }

    QWidget *host_ = nullptr;
    int leading_ = 0;
    int trailing_ = 0;
};

// 逻辑边距（LTR = (leading, trailing) / RTL = (trailing, leading)；纵向恒 0）
inline void set_logical_margins(QWidget *host, int leading, int trailing) {
    if (host != nullptr)
        new LogicalMarginsFilter(host, leading, trailing);
}

// 行首缩进（= set_logical_margins(host, px, 0)）
inline void set_leading_indent(QWidget *host, int px) { set_logical_margins(host, px, 0); }

} // namespace pp::ui
