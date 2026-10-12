// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页地图覆盖层（B.1 拆分步 B6：meta_map_overlay）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B6：覆盖层 + open_map_expanded_view +
// MapDialog 生命周期 + 尺寸记忆；状态归 MapOverlayController）。
// 覆盖层 = map 的子布局（卡内行节奏/地图几何零改动）：光标读数 chip / 缩放 ± / 「展开」钮 /
// 瓦片加载 chip / 图层浮层底板；大地图 = **同一 map 控件重挂载**（reparent），关闭挂回复位。
#pragma once

#include <QHBoxLayout>
#include <QSize>

class QLabel;
class QToolButton;
class QWidget;

namespace pp::ui::meta {

struct MetaState; // 完整定义见 meta_state.h

// M6-T12（§5.3/§7-T12 体验件）+ M6-T13（§7-T13 R5）：地图覆盖层与大地图对话框的状态面
// （B6：从 MetaState 平铺成员收归本聚合）。
struct MapOverlayController {
    QToolButton *zoom_out = nullptr;   // 地图覆盖层：缩小（矢量图标钮）
    QToolButton *zoom_in = nullptr;    // 地图覆盖层：放大
    QLabel *map_cursor = nullptr;      // 地图覆盖层左上：光标坐标读数（接 cursor_moved）
    QToolButton *map_expand = nullptr; // 覆盖层右上：打开「大地图」对话框
    QWidget *map_host = nullptr;       // map 的卡片宿主（挂回用；与 map_line 同源）
    QHBoxLayout *map_line = nullptr;   // map 在卡片里所占的行（重挂载的同一槽位）
    QSize map_dialog_size;             // 上次调整的大地图尺寸（跨展开记忆）
    QLabel *map_loading = nullptr;     // R-315：瓦片加载反馈 chip（tiles_pending_changed）
    QWidget *layer_plate = nullptr;    // 图层 Seg 的地图浮层底板（半透明，overlay 令牌）
};

// M6-T13：展开大地图 —— **同一控件重挂载**（对话框每次展开新建；尺寸记忆读写 MapOverlayController）
void open_map_expanded_view(MetaState *st);

} // namespace pp::ui::meta
