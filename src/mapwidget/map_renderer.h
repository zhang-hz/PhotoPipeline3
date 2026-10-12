// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 地图整帧绘制（B.5 拆分步④：map_renderer）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.5 步④（map_renderer：纯绘制 + HUD 统一）；
// 迁出自 mapwidget.cpp（M7-R2.2-G2，绘制序与笔笔参数逐位一致）：
//   * paint_map = **纯绘制**：只读 RenderState 快照 + QPainter，零可变状态；缺瓦片只经
//     on_missing 回调登记（R-221：paint 不发网络请求，幂等）；
//   * 绘制序（frozen）：底色 → 瓦片格网 → 图层栈瓦片（Hybrid 影像在下标注在上）→
//     结果钉（实心圆 + 编号气泡）→ 主钉（十字 + 圆，恒在钉堆顶层）→ 左下比例尺 → 右下版权；
//   * R-218：HUD（比例尺/版权/气泡）逻辑坐标 + 单点镜像（rtl 旗标入参；LTR 恒等），
//     地理内容（瓦片/钉位投影坐标）不镜像 —— 投影在调用方完成，本单元不碰 datum。
#pragma once

#include "mapwidget/layers.h"
#include "mapwidget/providers.h" // TileProvider（select_source 入参）
#include "mapwidget/tile_cache.h"

#include <QPainter>
#include <QPointF>
#include <QSize>
#include <QString>

#include <functional>
#include <vector>

namespace pp::map {

// M6-T6 结果图钉 / 主钉几何（§4.4）：绘制与命中共用同源常量（原 mapwidget.cpp 匿名域）。
inline constexpr int kMarkerRadius = 7;
inline constexpr int kMarkerArm = 5;
inline constexpr int kPinRadius = 5;
inline constexpr double kPinHitRadius = kMarkerRadius + 6; // 光标-钉心屏距命中阈值

// 本帧图层（source + layer 键；Hybrid = 影像 + 标注两条，自下而上）
struct PaintLayer {
    const TileSource *source = nullptr;
    QString layer;
};

// 屏幕图钉（几何已在调用方换算；纯绘制不碰 WGS/datum）
struct RenderPin {
    QPointF pos;
    QString label;
    int index = 0;
};

// 帧状态快照（paint_map 只读；查找/登记经回调，零可变状态）
struct RenderState {
    QSize size;
    QPointF center_world;  // 世界像素（map_view 投影）
    double center_lat = 0; // 视口中心纬度（比例尺米/像素用）
    int zoom = 0;
    bool rtl = false; // R-218：HUD 逻辑镜像旗标
    std::vector<PaintLayer> layers;
    std::function<const QPixmap *(const TileKey &)> find_tile;
    std::function<void(const TileKey &)> on_missing; // R-221：paint 只登记、不发请求
    std::vector<RenderPin> pins;
    bool marker_set = false;
    QPointF marker_pos;
    QString credit; // 版权行文案（有效源 attribution，layers.cpp 单源）
};

// M6-T5 图层选择：style → 有效瓦片源（Road = 当前源；影像/混合当前源不支持时回退 esri；
// 基准不漂契约 C3：回退只在 osm→esri（皆 WGS-84）之间，切换不做 datum 转换）。
const TileSource *select_source(const TileProvider *provider, TileStyle style);

// 本帧图层栈（自下而上）：Road → road；Satellite → satellite；Hybrid → satellite + label
// （sat_labels=false 只画影像层；无标注层的源返回空 label_url，调用方不请求）。
std::vector<PaintLayer> paint_layers(const TileProvider *provider, TileStyle style,
                                     bool sat_labels);

// 整帧绘制（绘制序见头注释；语义 = mapwidget.cpp 原 on_paint 逐位）
void paint_map(QPainter &painter, const RenderState &st);

} // namespace pp::map
