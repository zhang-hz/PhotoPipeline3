// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 「大地图」弹窗实现（M6-T13 / W-M5 R5）
//
// chrome 单源：dialogkit（settings_dialog.{h,cpp} 的弹窗套件）—— 标题栏 40（矢量图标 17 +
//   关闭 caption）/ 底栏 48 / 焦点环 / 弹窗 QSS，与设置/预设/EXIF 编辑器同一套控件语言。
// 地图槽：内容区只放**接管来的控件的槽**（QVBoxLayout），本类不建第二只地图、不做任何
//   状态复制（§7-T13「同控件复用」）。
#include "ui/map_dialog.h"

#include "mapwidget/mapwidget.h"
#include "platform/frameless.h" // 无边框接线（拖拽 / 八向缩放）
#include "ui/icons.h"           // 标题栏矢量图标（C3：禁字形）
#include "ui/settings_dialog.h" // dialogkit
#include "ui/theme.h"           // 令牌（chrome 配色单源）

#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace pp::ui {
namespace {

// 「更大」的地图区下限（§7-T13；对话框下限 = 该值 + chrome 度量），初始尺寸再宽出两档
constexpr int kMapViewW = 640;
constexpr int kMapViewH = 480;
constexpr int kDialogPadX = 60; // 初始宽 = 下限 + 120
constexpr int kDialogPadY = 96; // 初始高 = 下限 + 96
constexpr const char *kTitleText = QT_TR_NOOP("地图");
constexpr const char *kCloseText = QT_TR_NOOP("关闭");

} // namespace

MapDialog::MapDialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("pp-meta-map-dialog"));
    setWindowTitle(tr(kTitleText));
    const theme::Tokens tokens = theme::tokens(theme::preferred_theme_mode());
    setStyleSheet(dialogkit::dialog_qss(tokens));

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("pp-dlg-card"));
    auto *card_line = new QVBoxLayout(card);
    card_line->setContentsMargins(0, 0, 0, 0);
    card_line->setSpacing(0);
    const dialogkit::TitleBar bar =
        dialogkit::make_title_bar(card, icons::Name::MapPin, tr(kTitleText));
    card_line->addWidget(bar.widget);

    // 地图槽：内容区（正文内边距 = dialogkit 的 14/18/10 —— 与三弹窗同轴）
    slot_ = new QWidget(card);
    slot_line_ = new QVBoxLayout(slot_);
    slot_line_->setContentsMargins(dialogkit::kBodyPadX, dialogkit::kBodyPadTop,
                                   dialogkit::kBodyPadX, dialogkit::kBodyPadBottom);
    slot_line_->setSpacing(0);
    card_line->addWidget(slot_, 1);

    // 底栏（§7.4 48px）：单动作「关闭」（无待提交状态 → 不做 OK/取消；Esc/标题栏 ✕ 同效）
    auto *footer = new QFrame(card);
    footer->setObjectName(QLatin1String(dialogkit::kFooterName));
    footer->setFixedHeight(dialogkit::kFooterH);
    auto *footer_line = new QHBoxLayout(footer);
    footer_line->setContentsMargins(dialogkit::kFooterPadX, 0, dialogkit::kFooterPadX, 0);
    footer_line->setSpacing(theme::Metrics::group_gap);
    auto *close_btn = dialogkit::make_button(footer, tr(kCloseText), /*primary=*/false);
    close_btn->setObjectName(QStringLiteral("pp-meta-map-close"));
    close_btn->setToolTip(tr("关闭大地图，回到卡片里的地图"));
    footer_line->addStretch(1);
    footer_line->addWidget(close_btn);
    card_line->addWidget(footer);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(card, 1);

    setMinimumSize(2 * dialogkit::kBodyPadX + kMapViewW,
                   dialogkit::kTitleBarH + dialogkit::kBodyPadTop + dialogkit::kBodyPadBottom +
                       dialogkit::kFooterH + kMapViewH);
    resize(minimumWidth() + kDialogPadX, minimumHeight() + kDialogPadY);

    // 无边框接线（标题栏拖拽 + 八向缩放；与设置/预设/编辑器同一机制）
    pp::platform::Frameless::attach(this, bar.widget, {bar.close});
    dialogkit::bind_close_button(bar, this);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::reject);

    // §11：自绘焦点环（accent 1px；系统虚线框由 dialog_qss 的 outline:none 关闭）
    auto *ring = new dialogkit::FocusRing(this);
    ring->set_tokens(tokens);
}

MapDialog::~MapDialog() {
    release(); // R-261：析构兜底 —— adopt/release 成对，调用方漏调 release 也不丢复原
}

void MapDialog::adopt(pp::map::MapWidget *map) {
    if (map == nullptr) {
        return;
    }
    release(); // 换接管先复原旧契约（幂等）
    map_ = map;
    if (map->parentWidget() != slot_) {
        map->setParent(slot_);
    }
    // 卡片里的固定高只在卡片生效：放开上限让地图随对话框伸缩（R-261：约束记账在本类，
    // release()/析构成对复原 —— 不再把复原义务单边推给调用方）
    adopt_max_height_ = map->maximumHeight();
    map->setMaximumHeight(QWIDGETSIZE_MAX);
    slot_line_->addWidget(map, 1);
    map->show();
}

void MapDialog::release() {
    if (map_ == nullptr) {
        return;
    }
    map_->setMaximumHeight(adopt_max_height_); // 复原 adopt 放开的几何约束
    map_ = nullptr;
}

} // namespace pp::ui
