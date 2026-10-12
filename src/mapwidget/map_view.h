// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 地图视图纯函数（B.5 拆分步①：map_view → pp_core）
//
// Contract: docs/deep-review-0.3.2.md 附录 B.5 步①（map_view：latlon/world/screen 投影 +
// wgs_to_datum/datum_to_wgs 基准边界；纯函数、零 Qt Widgets，随 pp_core 可单测）。
// 迁出自 mapwidget.cpp 匿名命名空间（M7-R2.1-F2；实现逐字，值/行为零变化）：
//   * 投影族 = Web Mercator（clamp ±85.05112877980659；span = 256·2^zoom）；
//   * 基准边界 = **唯一** WGS↔datum 转换面（R-219 统一约定后 geocoder URL 侧同口径；
//     gcj02 标志 = 有效源的 provider->gcj02，调用方传入）。
// 常量随迁（kTileSize/kPi/kMaxMercatorLat）保持逐字不变。
#pragma once

#include <QPointF>
#include <utility>

namespace pp::map {

inline constexpr int kTileSize = 256;
inline constexpr double kMaxMercatorLat = 85.05112877980659;
inline constexpr double kPi = 3.14159265358979323846;

// 纬度夹入 Mercator 有效带
double clamp_lat(double lat);

// 世界方块边长（像素）
double world_span(int zoom);

// 瓦片 x 环绕（东西向循环；南北向不环绕）
int wrap_tile_x(int x, int tile_count);

// WGS-84/datum 经纬度 → 世界像素（Mercator）
QPointF latlon_to_world(double lat, double lon, int zoom);

// 世界像素 → 经纬度（越带 y 回夹到 ±85.0511…；R-103）
std::pair<double, double> world_to_latlon(QPointF world, int zoom);

// 基准边界（frozen: conversion only at the boundary）：gcj02=false = 恒等
std::pair<double, double> wgs_to_datum(double lat, double lon, bool gcj02);
std::pair<double, double> datum_to_wgs(double lat, double lon, bool gcj02);

} // namespace pp::map
