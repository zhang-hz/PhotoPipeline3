// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M6 — coordinate display formatting (new, non-frozen)
#pragma once
#include <QString>
#include <optional>

namespace pp::map {
QString deg_to_dms(double deg, bool is_lat);          // "N 39°54.246'" / "E 116°24.444'"
std::optional<double> dms_to_deg(const QString &s);   // 解析回十进制度;失败返回 nullopt
bool coord_precision_suspect(double lat, double lon); // 粗包围盒边缘/外 → true(国境/港澳台)
} // namespace pp::map
