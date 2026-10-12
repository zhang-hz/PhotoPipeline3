// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 元数据页地图覆盖层（B.1 拆分步 B6：meta_map_overlay）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.1（步序 B6）；契约注释见 meta_map_overlay.h。
// 实现自 page_meta.cpp 逐字迁出（M7-R2.1-F2；行为零变化）。
#include "ui/meta_map_overlay.h"

#include "mapwidget/mapwidget.h" // MapWidget 完整类型（重挂载/setMinimumHeight/show）
#include "ui/map_dialog.h"       // M6-T13：大地图对话框（同一 map 控件的重挂载宿主）
#include "ui/meta_state.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QToolButton>
#include <QWidget>

namespace pp::ui::meta {

// ---------------------------------------------------------------------------

// M6-T13（§7-T13 R5）：展开大地图 —— **同一控件重挂载**：map 暂挂到对话框内容区（图层/图钉/
// 视口/在途瓦片请求随控件走，零同步），exec() 返回后挂回卡片行并复位固定高（卡片几何逐位
// 复原）。对话框每次展开新建（主题档/尺寸记忆按时读/写本状态）。
// 卡片位：地图离场期间放**同高空占位**（模态期卡片不上下抖动；关闭时移除）。
void open_map_expanded_view(MetaState *st) {
    if (st == nullptr || st->map == nullptr || st->overlay.map_host == nullptr ||
        st->overlay.map_line == nullptr) {
        return;
    }
    MapDialog dialog(st->map->window());
    if (st->overlay.map_dialog_size.isValid()) {
        dialog.resize(st->overlay.map_dialog_size); // 上次调整的尺寸（跨展开记忆）
    }
    if (st->overlay.map_expand != nullptr) {
        st->overlay.map_expand->setVisible(false); // 对话框自有「关闭」；展开期不重复该入口
    }
    auto *placeholder = new QWidget(st->overlay.map_host);
    placeholder->setObjectName(QString::fromLatin1(kMapPlaceholderName));
    placeholder->setFixedHeight(kMapHeight);
    st->overlay.map_line->addWidget(placeholder, 1, Qt::AlignVCenter);
    dialog.adopt(st->map);
    dialog.exec();    // 模态：返回即已关闭（Esc / 底栏关闭 / 标题栏 ✕ 同效）
    dialog.release(); // R-261：与 adopt 成对（对话框侧复原几何约束；析构另有兜底）
    // 挂回卡片行：先撤占位（占位不参与任何信号链，同步删）再挂 map；随后几何约束逐位复原
    // （宿主 + 行槽 + objectName 随控件、无需重挂）。UX-4 stretch（M7-R2-B）：复位口径 = 与
    // 初建同源（min 260 + 可拉伸），不再 setFixedHeight —— 探针断言改 `>= 260` + geometry 逐位。
    st->map->setParent(st->overlay.map_host);
    delete placeholder;
    st->overlay.map_line->addWidget(st->map, 1);
    st->map->setMinimumHeight(kMapHeight);
    st->map->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    st->map->show();
    if (st->overlay.map_expand != nullptr) {
        st->overlay.map_expand->setVisible(true);
    }
    st->overlay.map_dialog_size = dialog.size();
}

} // namespace pp::ui::meta
