// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6 — coordinate display formatting (new, non-frozen)
#pragma once
#include <QString>
#include <optional>

namespace pp::map {
QString deg_to_dms(double deg, bool is_lat);        // "N 39°54.246'" / "E 116°24.444'"
std::optional<double> dms_to_deg(const QString &s); // 解析回十进制度;失败返回 nullopt
// 粗包围盒**内**贴近任一边（≤ kEdgeBandDeg）→ true；盒内深处与盒外 → false。
// （M7-WP7 / R-105：原头注"边缘/外→true"与实现相反 —— 实现与 test_dms 均为盒外 false；
//  R-046：提示语义 = "靠近粗包围盒边界"，不是真实国境线判定。）
bool coord_precision_suspect(double lat, double lon);
} // namespace pp::map
