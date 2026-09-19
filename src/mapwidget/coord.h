// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — WGS-84 <-> GCJ-02 conversion (M1b frozen)
#pragma once
#include <utility>

namespace pp::map {

// Public iterative algorithm. GCJ->WGS is an iterative approximation (~1m).
// Out of China (rough bbox test) -> identity (returns input unchanged).
std::pair<double, double> wgs84_to_gcj02(double lat, double lon);
std::pair<double, double> gcj02_to_wgs84(double lat, double lon);

}  // namespace pp::map
