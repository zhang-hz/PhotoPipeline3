// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 「大地图」弹窗（M6-T13 / W-M5 R5；设计依据 docs/m6-map-upgrade.md §5.3 / §7-T13）
//
// 落点：GPS 卡的小地图（高 136 = mockups-v2 终稿）在密集图钉/卫星图下看不清 → 覆盖层的
//   「展开」钮把**同一个** MapWidget 重挂载（reparent）到大视图对话框：图层/图钉/视口/
//   在途瓦片请求随控件原样带入（零同步、零重建），关闭时由调用方挂回卡片行。
//   故本类只是**纯 UI 容器**（mapwidget.h 零改动；`pp-meta-map` objectName 随控件走、不变）。
// 度量：地图区下限 640×480（「更大」，§7-T13）；对话框下限 = 该值 + dialogkit chrome 度量，
//   可自由缩放（标题栏拖拽 + 八向缩放，与设置/预设/编辑器三弹窗同一套无边框机制）。
// 生命周期：**每次展开新建**（调用方栈上持有，关闭即销毁）—— 主题档在构造期取
//   `theme::preferred_theme_mode()`（与其它弹窗同口径）；尺寸记忆由调用方（page_meta）持有。
#pragma once
#include <QDialog>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

namespace pp::map {
class MapWidget;
}

namespace pp::ui {

class MapDialog : public QDialog {
    Q_OBJECT
public:
    explicit MapDialog(QWidget *parent = nullptr);

    // 接管地图：reparent 到内容区并铺满（放宽卡片里的 136px 固定高，回程由调用方恢复）。
    // 所有权仍属调用方 —— 本类不 delete、不持有第二份状态。
    void adopt(pp::map::MapWidget *map);
    pp::map::MapWidget *map() const { return map_; } // 当前接管的地图（未接管 → nullptr）

private:
    QWidget *slot_ = nullptr;          // 地图宿主（内容区）
    QVBoxLayout *slot_line_ = nullptr; // 内容区布局（接管地图的同一槽）
    pp::map::MapWidget *map_ = nullptr;
};

} // namespace pp::ui
