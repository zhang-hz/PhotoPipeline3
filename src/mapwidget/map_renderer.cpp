// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 地图整帧绘制（B.5 拆分步④：map_renderer；契约见 map_renderer.h）
//
// 实现 = mapwidget.cpp 的 on_paint / draw_scale_bar / select_source / paint_layers /
// mirror_* 逐段迁出（M7-R2.2-G2；绘制序、笔笔参数、HUD 取值逐位一致）。
#include "mapwidget/map_renderer.h"

#include "mapwidget/map_view.h" // kTileSize / kPi / clamp_lat（投影单源）

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLineF>
#include <QPen>
#include <QPixmap>
#include <QRect>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace pp::map {

namespace {

// M6-T6 结果图钉（§4.4）：实心圆半径 / 编号气泡字号与文本像素上限。
constexpr int kPinBubblePointSize = 7;
constexpr int kPinBubbleTextMax = 64; // 长地名省略，避免气泡盖住大片地图
// R-311（M7-WP7）：HUD 字号**一处命名**（比例尺 + 版权行同字号同白底 α190）。
constexpr int kHudPointSize = 6; // frozen: 6 pt credit label (and scale bar, R-311 rename)
// M6-T12 体验件：比例尺（左下）的目标段长与档系。
constexpr int kScaleBarTargetPx = 60;           // 取档前的目标段长（实际段长 = 取整档反算）
constexpr double kScaleBarMppZ0 = 156543.03392; // z=0、256 px 瓦片下的赤道米/像素

// R-218（M7-R2.1-F2 / B.5 ④）：自绘 HUD 的逻辑坐标**单点镜像** —— RTL 下 leading=右缘，
// LTR 恒等（原样返回）。地理内容（瓦片/图钉/主钉投影坐标）不镜像；镜像只作用于
// HUD 盒/文字/线段这类界面构件。
QRect mirror_x(const QRect &r, int width, bool rtl) {
    if (!rtl) {
        return r;
    }
    return QRect(width - r.x() - r.width(), r.y(), r.width(), r.height());
}

// 点镜像（线段端点按像素取镜像：x' = W - 1 - x；LTR 恒等）
int mirror_px(int x, int width, bool rtl) { return rtl ? width - 1 - x : x; }

} // namespace

const TileSource *select_source(const TileProvider *provider, TileStyle style) {
    // ---- M6-T5 图层选择 ----------------------------------------------------------
    // select_source(style)：给定图层 → 有效瓦片源。当前 provider 的源支持该图层则用它；
    // 不支持时（如 osm 无卫星/混合层）回退 esri（esri 支持 Satellite/Hybrid）；Road 永远用当前源。
    //
    // 基准不漂（契约 C3）：回退只发生在 osm → esri 之间，二者栅格皆 WGS-84；amap 三图层同为
    // GCJ-02。内部中心基准面只由 provider->gcj02 决定，与所选源无关 —— 切图层/回退**不做**
    // 任何 datum 转换，center_lat/lon 与 marker 的 WGS 坐标逐位不变。
    const TileSource *const current =
        provider != nullptr ? tile_source_by_id(provider->id) : nullptr;
    if (style == TileStyle::Road) {
        return current; // 路网：永远当前源（不参与回退）
    }
    if (current != nullptr && current->supports(style)) {
        return current;
    }
    return tile_source_by_id(QStringLiteral("esri")); // 卫星/混合回退：esri 二者皆支持
}

std::vector<PaintLayer> paint_layers(const TileProvider *provider, TileStyle style,
                                     bool sat_labels) {
    // 本帧图层栈（自下而上）：Road → road；Satellite → satellite；Hybrid → satellite + label。
    // 无标注层的源（osm/esri）返回空 label_url → 该层无 URL 不请求，即「无标注层则跳过」。
    // M6-T12：Hybrid 的标注层受 sat_labels（set_sat_labels）控制 —— false = 只画影像层。
    std::vector<PaintLayer> stack;
    const TileSource *const source = select_source(provider, style);
    if (source == nullptr) {
        return stack;
    }
    if (style == TileStyle::Road) {
        stack.push_back(PaintLayer{source, QString::fromLatin1(kLayerRoad)});
        return stack;
    }
    stack.push_back(PaintLayer{source, QString::fromLatin1(kLayerSatellite)});
    if (style == TileStyle::Hybrid && sat_labels) {
        stack.push_back(PaintLayer{source, QString::fromLatin1(kLayerLabel)});
    }
    return stack;
}

namespace {

// 左下比例尺（§5.3）：当前 zoom/纬度下的「米每像素」把 ~60 px 段长折成 1/2/5 × 10^k 的整
// 档（100 m / 500 m / 1 km …），再按该档反算实际段长绘制；白底 α190 + 6pt 与右下版权行同
// 口径（卫星底图上仍可读）。纬度取视口中心（同一比例尺的横轴近菱形，取中心纬度即用户所在档）。
void draw_scale_bar(QPainter &painter, const RenderState &st) {
    const double mpp = kScaleBarMppZ0 * std::cos(clamp_lat(st.center_lat) / 180.0 * kPi) /
                       double(std::int64_t(1) << st.zoom);
    if (!(mpp > 0.0) || !std::isfinite(mpp)) {
        return;
    }
    const double raw_m = double(kScaleBarTargetPx) * mpp;
    const double mag = std::pow(10.0, std::floor(std::log10(raw_m)));
    double nice_m = mag;
    for (const double factor : {1.0, 2.0, 5.0}) {
        if (mag * factor <= raw_m) {
            nice_m = mag * factor;
        }
    }
    const QString label = nice_m >= 1000.0 ? QStringLiteral("%1 km").arg(nice_m / 1000.0, 0, 'g', 4)
                                           : QStringLiteral("%1 m").arg(nice_m, 0, 'g', 4);
    const QString zero = QStringLiteral("0");
    const int bar_px = std::max(8, int(std::lround(nice_m / mpp)));
    const int h = st.size.height();

    painter.save();
    QFont font = painter.font();
    font.setPointSize(kHudPointSize);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int pad = 5;
    const int gap = 4;
    const int box_w = 2 * pad + metrics.horizontalAdvance(zero) + gap + bar_px + gap +
                      metrics.horizontalAdvance(label);
    const int box_h = metrics.height() + 4;
    // R-218：盒与内容一律逻辑坐标 + 单点镜像（LTR 恒等；RTL 下贴右缘、内容右起）
    const QRect box = mirror_x(QRect(6, h - 6 - box_h, box_w, box_h), st.size.width(), st.rtl);
    painter.fillRect(box, QColor(255, 255, 255, 190));
    painter.setPen(QColor(0x40, 0x40, 0x40));
    const int mid_y = box.y() + box_h / 2;
    int x = box.x() + pad;
    painter.drawText(mirror_x(QRect(x, box.y(), metrics.horizontalAdvance(zero), box_h),
                              st.size.width(), st.rtl),
                     Qt::AlignLeft | Qt::AlignVCenter, zero);
    x += metrics.horizontalAdvance(zero) + gap;
    painter.drawLine(mirror_px(x, st.size.width(), st.rtl), mid_y,
                     mirror_px(x + bar_px, st.size.width(), st.rtl), mid_y); // 段线+刻度
    painter.drawLine(mirror_px(x, st.size.width(), st.rtl), mid_y - 3,
                     mirror_px(x, st.size.width(), st.rtl), mid_y + 3);
    painter.drawLine(mirror_px(x + bar_px, st.size.width(), st.rtl), mid_y - 3,
                     mirror_px(x + bar_px, st.size.width(), st.rtl), mid_y + 3);
    x += bar_px + gap;
    painter.drawText(mirror_x(QRect(x, box.y(), metrics.horizontalAdvance(label), box_h),
                              st.size.width(), st.rtl),
                     Qt::AlignLeft | Qt::AlignVCenter, label);
    painter.restore();
}

} // namespace

void paint_map(QPainter &painter, const RenderState &st) {
    const int w = st.size.width();
    const int h = st.size.height();
    if (w <= 0 || h <= 0) {
        return;
    }
    painter.fillRect(QRect(0, 0, w, h), QColor(0xe9, 0xe9, 0xe9));

    const QPointF center = st.center_world;
    const double left = center.x() - w / 2.0;
    const double top = center.y() - h / 2.0;

    // Thin grid on the tile lattice marks the not-yet-loaded area.
    painter.setPen(QPen(QColor(0xd2, 0xd2, 0xd2), 1));
    for (double wx = std::floor(left / kTileSize) * kTileSize; wx - left <= w; wx += kTileSize) {
        const int sx = int(std::lround(wx - left));
        painter.drawLine(sx, 0, sx, h);
    }
    for (double wy = std::floor(top / kTileSize) * kTileSize; wy - top <= h; wy += kTileSize) {
        const int sy = int(std::lround(wy - top));
        painter.drawLine(0, sy, w, sy);
    }

    if (!st.layers.empty() && st.find_tile && st.on_missing) {
        const int tile_count = int(std::int64_t(1) << st.zoom);
        const int x0 = int(std::floor(left / kTileSize));
        const int x1 = int(std::floor((left + w - 1) / kTileSize));
        const int y0 = int(std::floor(top / kTileSize));
        const int y1 = int(std::floor((top + h - 1) / kTileSize));
        // M6-T5：逐层绘制（Hybrid 的影像层在前 = 在下，标注层在后 = 叠在上面）；每层各有自己的
        // 缓存条目（key 含 layer）。缺瓦片仍留灰底网格，抓取仍受 ≤4 并发 / 8s 超时 / 离线不发约束。
        for (const PaintLayer &layer : st.layers) {
            for (int ty = y0; ty <= y1; ++ty) {
                if (ty < 0 || ty >= tile_count) {
                    continue; // no vertical wrap on a slippy map
                }
                for (int tx = x0; tx <= x1; ++tx) {
                    const TileKey key{layer.source->id, layer.layer, st.zoom,
                                      wrap_tile_x(tx, tile_count), ty};
                    const int sx = int(std::lround(tx * double(kTileSize) - left));
                    const int sy = int(std::lround(ty * double(kTileSize) - top));
                    if (const QPixmap *const tile = st.find_tile(key)) {
                        painter.drawPixmap(sx, sy, kTileSize, kTileSize, *tile);
                    } else {
                        st.on_missing(key); // R-221：paint 只登记；flush 在 paint 外发请求
                    }
                }
            }
        }
    }

    // M6-T6 结果图钉（§4.4）：实心圆 + 编号气泡，坐标管线和主钉一致（WGS-84 → datum → world
    // → screen）。画在瓦片之上、主 marker 之下 —— 主钉最后画，恒在钉堆顶层。
    if (!st.pins.empty()) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont pin_font = painter.font();
        pin_font.setPointSize(kPinBubblePointSize);
        painter.setFont(pin_font);
        const QFontMetrics pin_metrics(pin_font);
        const QColor accent(0x2b, 0x6c, 0xb0); // 中性强调色
        for (const RenderPin &pin : st.pins) {
            const QPointF pos = pin.pos;
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent);
            painter.drawEllipse(pos, double(kPinRadius), double(kPinRadius));
            const QString full = pin.label.isEmpty() ? QString::number(pin.index) : pin.label;
            const QString text = pin_metrics.elidedText(full, Qt::ElideRight, kPinBubbleTextMax);
            // R-218：气泡贴钉的**逻辑首侧**（LTR=钉右 / RTL=钉左）；钉心（地理投影）不镜像
            const double bubble_w = pin_metrics.horizontalAdvance(text) + 8;
            const double bubble_x =
                st.rtl ? pos.x() - kPinRadius - 1 - bubble_w : pos.x() + kPinRadius + 1;
            const QRectF bubble(bubble_x, pos.y() - kPinRadius - pin_metrics.height() - 2, bubble_w,
                                pin_metrics.height() + 2);
            painter.setPen(QPen(accent, 1));
            painter.setBrush(QColor(255, 255, 255, 230)); // 白底：卫星影像上仍可读
            painter.drawRoundedRect(bubble, 3, 3);
            painter.setPen(accent);
            painter.drawText(bubble, Qt::AlignCenter, text);
        }
        painter.restore();
    }

    // Marker last (WGS-84 -> datum -> world -> screen).
    if (st.marker_set) {
        const QPointF pos = st.marker_pos;
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(0xd0, 0x22, 0x22), 2));
        painter.drawLine(QPointF(pos.x() - kMarkerRadius - kMarkerArm, pos.y()),
                         QPointF(pos.x() + kMarkerRadius + kMarkerArm, pos.y()));
        painter.drawLine(QPointF(pos.x(), pos.y() - kMarkerRadius - kMarkerArm),
                         QPointF(pos.x(), pos.y() + kMarkerRadius + kMarkerArm));
        painter.drawEllipse(pos, double(kMarkerRadius), double(kMarkerRadius));
        painter.setRenderHint(QPainter::Antialiasing, false);
    }

    // M6-T12：左下比例尺（§5.3）—— 与右下版权行同在瓦片/图钉/主钉之上，互为左右角标。
    draw_scale_bar(painter, st);

    // Bottom-right credit label —— 文案单源 = 有效源的 attribution（D1：唯一定义处在 layers.cpp；
    // 卫星/混合回退到 esri 影像时报 esri 版权）。§4.1：卫星底图偏暗，白底 α190 保留可读性。
    if (!st.credit.isEmpty()) {
        QFont font = painter.font();
        font.setPointSize(kHudPointSize);
        painter.setFont(font);
        const QFontMetrics metrics(font);
        const QRect box =
            mirror_x(QRect(w - metrics.horizontalAdvance(st.credit) - 8, h - metrics.height() - 6,
                           metrics.horizontalAdvance(st.credit) + 6, metrics.height() + 2),
                     w, st.rtl);
        painter.fillRect(box, QColor(255, 255, 255, 190));
        painter.setPen(QColor(0x40, 0x40, 0x40));
        painter.drawText(box, Qt::AlignCenter, st.credit);
    }
}

} // namespace pp::map
