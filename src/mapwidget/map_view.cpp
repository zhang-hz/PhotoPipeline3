// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 地图视图纯函数（B.5 拆分步①：map_view → pp_core）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.5 步①；契约注释见 map_view.h。
// 实现自 mapwidget.cpp 匿名命名空间逐字迁出（M7-R2.1-F2；行为零变化）。
#include "mapwidget/map_view.h"

#include "mapwidget/coord.h" // wgs84_to_gcj02 / gcj02_to_wgs84（基准边界唯一转换源）

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pp::map {

double clamp_lat(double lat) { return std::clamp(lat, -kMaxMercatorLat, kMaxMercatorLat); }

double world_span(int zoom) { return double(kTileSize) * double(std::int64_t(1) << zoom); }

int wrap_tile_x(int x, int tile_count) {
    const int m = x % tile_count;
    return m < 0 ? m + tile_count : m;
}

QPointF latlon_to_world(double lat, double lon, int zoom) {
    const double span = world_span(zoom);
    const double s = std::sin(clamp_lat(lat) / 180.0 * kPi);
    return {(lon + 180.0) / 360.0 * span,
            (0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * kPi)) * span};
}

std::pair<double, double> world_to_latlon(QPointF world, int zoom) {
    const double span = world_span(zoom);
    const double n = kPi * (1.0 - 2.0 * world.y() / span);
    // R-103（M7-WP7）：世界方块外（上下灰区）的 y 会给出 |lat|→90 的越带纬度；Mercator 有效
    // 带 = ±85.0511…（与 latlon_to_world 的 clamp_lat 同界）。取点/拖拽/光标读数都经此处。
    return {clamp_lat(std::atan(std::sinh(n)) * 180.0 / kPi), world.x() / span * 360.0 - 180.0};
}

std::pair<double, double> wgs_to_datum(double lat, double lon, bool gcj02) {
    if (gcj02) {
        return wgs84_to_gcj02(lat, lon);
    }
    return {lat, lon};
}

std::pair<double, double> datum_to_wgs(double lat, double lon, bool gcj02) {
    if (gcj02) {
        return gcj02_to_wgs84(lat, lon);
    }
    return {lat, lon};
}

} // namespace pp::map
