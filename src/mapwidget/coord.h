// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — WGS-84 <-> GCJ-02 conversion (M1b frozen)
#pragma once
#include <utility>

namespace pp::map {

// Public algorithm. GCJ->WGS has no closed form: it is solved by fixed-point iteration to a
// 1e-13 deg tolerance (< 0.01 mm) — the "~1m" one-shot-inverse folklore does not apply here
// (R-306: this header's old "~1m" claim contradicted coord.cpp's convergence bound; wording
// unified to the measured implementation).
// Out of China (rough bbox test) -> identity (returns input unchanged).
std::pair<double, double> wgs84_to_gcj02(double lat, double lon);
std::pair<double, double> gcj02_to_wgs84(double lat, double lon);

// ---- M7-WP7 加性追认（R-307；裁定登记 build/m7-wp7/handoff.md）-----------------
// 中国粗包围盒（mainland + Hainan）**常量单源**：coord.cpp 的 out_of_china 与 dms.cpp 的
// 边界带判定（coord_precision_suspect）必须同盒 —— 此前两处各持一份字面量、注释自认
// "改一处须同时改另一处"（R-307）。追加在文件末尾（冻结面加性），既有声明零改动。
constexpr double kChinaBBoxLonMin = 72.004;
constexpr double kChinaBBoxLonMax = 137.8347;
constexpr double kChinaBBoxLatMin = 0.8293;
constexpr double kChinaBBoxLatMax = 55.8271;

} // namespace pp::map
