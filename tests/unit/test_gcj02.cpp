// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U2 — WGS-84 <-> GCJ-02 conversion unit tests (hand-written assertions).
//
// Contract: docs/m1b-tasks.md §2.3 (PP-FROZEN header) + §4.1 U2.
// Deliberately coord-only: pp_test_* targets link pp_core (Qt6::Core) only, so no QApplication /
// widget header may appear here; the MapWidget runtime assertion (set_offline(true) + resize +
// grab() != null) lives in U10's --ui-smoke instead (task book §4.3, amended by the main
// conversation while this task ran — see the M1b-U2 report).
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <cmath>
#include <cstdio>
#include <string>

#include "mapwidget/coord.h"
#include "mapwidget/map_view.h" // B.5 ①（M7-R2.1-F2）：投影/基准纯核（pp_core）

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

// §4.1 U2 offset band, task book amended 2026-09-19: ground-true distance metric,
// 300–700 m; measured 555.5 / 481.8 / 621.7 m for Beijing / Shanghai / Guangzhou.
// dlat_m = dlat_deg * 111320, dlon_m = dlon_deg * 111320 * cos(original WGS-84 latitude).
// (The book's original "300–600 m, 度距 x 111320" did not survive measurement: a longitude
// degree is not 111320 m away from the equator, and Guangzhou's true offset is 621.7 m.
// Recorded in the M1b-U2 report, api-deltas.)
constexpr double kMetersPerDegree = 111320.0;
constexpr double kMinOffsetM = 300.0;
constexpr double kMaxOffsetM = 700.0;
constexpr double kRoundTripToleranceDeg = 1e-6;
constexpr double kPi = 3.14159265358979323846;

struct City {
    const char *name;
    double lat;
    double lon;
};

const City kCities[] = {
    {"beijing", 39.9042, 116.4074},
    {"shanghai", 31.2304, 121.4737},
    {"guangzhou", 23.1291, 113.2644},
};

// Out-of-China samples: the rough bbox test must make the transform an exact identity.
const City kOutside[] = {
    {"new-york", 40.7128, -74.0060}, {"london", 51.5074, -0.1278}, {"sydney", -33.8688, 151.2093},
    {"tokyo", 35.6762, 139.6503},    {"equator", 0.0, 0.0},
};

} // namespace

int main() {
    // 1. wgs -> gcj: nonzero offset of the right order of magnitude, three frozen cities.
    for (const City &city : kCities) {
        const std::pair<double, double> gcj = pp::map::wgs84_to_gcj02(city.lat, city.lon);
        const double dlat = gcj.first - city.lat;
        const double dlon = gcj.second - city.lon;
        // Ground-true distance: the longitude scale shrinks with cos(latitude).
        const double dlat_m = dlat * kMetersPerDegree;
        const double dlon_m = dlon * kMetersPerDegree * std::cos(city.lat / 180.0 * kPi);
        const double offset_m = std::hypot(dlat_m, dlon_m);

        const std::string name = std::string("offset/") + city.name;
        check(offset_m >= kMinOffsetM && offset_m <= kMaxOffsetM, name,
              "offset " + num(offset_m) + " m outside [" + num(kMinOffsetM) + ", " +
                  num(kMaxOffsetM) + "] (dlat=" + num(dlat) + " dlon=" + num(dlon) + ")");
        check(std::fabs(dlat) > 0.0 && std::fabs(dlon) > 0.0, name + "/nonzero",
              "offset must move both axes (dlat=" + num(dlat) + " dlon=" + num(dlon) + ")");
    }

    // 2. round trip gcj(wgs(p)) -> p must come back within 1e-6 deg (and the other way round).
    for (const City &city : kCities) {
        const std::pair<double, double> gcj = pp::map::wgs84_to_gcj02(city.lat, city.lon);
        const std::pair<double, double> back = pp::map::gcj02_to_wgs84(gcj.first, gcj.second);
        const double err = std::hypot(back.first - city.lat, back.second - city.lon);
        check(err < kRoundTripToleranceDeg, std::string("roundtrip/wgs-gcj-wgs/") + city.name,
              "error " + num(err) + " deg >= " + num(kRoundTripToleranceDeg));

        const std::pair<double, double> wgs = pp::map::gcj02_to_wgs84(city.lat, city.lon);
        const std::pair<double, double> again = pp::map::wgs84_to_gcj02(wgs.first, wgs.second);
        const double err2 = std::hypot(again.first - city.lat, again.second - city.lon);
        check(err2 < kRoundTripToleranceDeg, std::string("roundtrip/gcj-wgs-gcj/") + city.name,
              "error " + num(err2) + " deg >= " + num(kRoundTripToleranceDeg));
    }

    // 3. outside the rough China bbox both directions are the exact identity.
    for (const City &city : kOutside) {
        const std::pair<double, double> fwd = pp::map::wgs84_to_gcj02(city.lat, city.lon);
        check(fwd.first == city.lat && fwd.second == city.lon,
              std::string("identity/wgs-gcj/") + city.name,
              "expected (" + num(city.lat) + ", " + num(city.lon) + "), got (" + num(fwd.first) +
                  ", " + num(fwd.second) + ")");
        const std::pair<double, double> inv = pp::map::gcj02_to_wgs84(city.lat, city.lon);
        check(inv.first == city.lat && inv.second == city.lon,
              std::string("identity/gcj-wgs/") + city.name,
              "expected (" + num(city.lat) + ", " + num(city.lon) + "), got (" + num(inv.first) +
                  ", " + num(inv.second) + ")");
    }

    // ---- M7-R2.1-F2 / 附录 B.5 ①：map_view 投影与基准边界（断言只增不减）----
    // 4. clamp_lat 夹入 Mercator 有效带（越带取 ±85.05112877980659）。
    check(pp::map::clamp_lat(89.0) == pp::map::kMaxMercatorLat, "map_view/clamp-lat-hi",
          "expected kMaxMercatorLat, got " + num(pp::map::clamp_lat(89.0)));
    check(pp::map::clamp_lat(-89.0) == -pp::map::kMaxMercatorLat, "map_view/clamp-lat-lo",
          "expected -kMaxMercatorLat, got " + num(pp::map::clamp_lat(-89.0)));
    check(pp::map::clamp_lat(12.5) == 12.5, "map_view/clamp-lat-id",
          "expected 12.5, got " + num(pp::map::clamp_lat(12.5)));

    // 5. wrap_tile_x 东西环绕（负 x 回卷；南北向不环绕是调用方语义，此处只钉回卷）。
    check(pp::map::wrap_tile_x(5, 4) == 1, "map_view/wrap-x-pos",
          "expected 1, got " + num(pp::map::wrap_tile_x(5, 4)));
    check(pp::map::wrap_tile_x(-1, 4) == 3, "map_view/wrap-x-neg",
          "expected 3, got " + num(pp::map::wrap_tile_x(-1, 4)));

    // 6. 投影往返：latlon→world→latlon 误差 < 1e-9 deg（三城 × 三档 zoom）。
    for (const City &city : kCities) {
        for (const int z : {2, 8, 14}) {
            const QPointF w = pp::map::latlon_to_world(city.lat, city.lon, z);
            const std::pair<double, double> back = pp::map::world_to_latlon(w, z);
            const double err =
                std::max(std::fabs(back.first - city.lat), std::fabs(back.second - city.lon));
            check(err < 1e-9,
                  std::string("map_view/proj-roundtrip/") + city.name + "@z" + std::to_string(z),
                  "error " + num(err) + " deg >= 1e-9");
        }
    }

    // 7. world_span 逐档 = 256·2^z；datum 边界 gcj02=false 恒等、true 与 coord 单源等价。
    check(pp::map::world_span(0) == 256.0 && pp::map::world_span(4) == 4096.0,
          "map_view/world-span", "expected 256/4096");
    {
        const std::pair<double, double> id = pp::map::wgs_to_datum(31.23, 121.47, false);
        check(id.first == 31.23 && id.second == 121.47, "map_view/datum-id",
              "gcj02=false must be identity");
        const std::pair<double, double> conv = pp::map::wgs_to_datum(31.23, 121.47, true);
        const std::pair<double, double> ref = pp::map::wgs84_to_gcj02(31.23, 121.47);
        check(conv.first == ref.first && conv.second == ref.second, "map_view/datum-eq",
              "wgs_to_datum(gcj02=true) must equal wgs84_to_gcj02");
        const std::pair<double, double> back = pp::map::datum_to_wgs(conv.first, conv.second, true);
        check(std::fabs(back.first - 31.23) < 1e-8 && std::fabs(back.second - 121.47) < 1e-8,
              "map_view/datum-roundtrip", "error too large");
    }

    if (g_failed == 0) {
        std::printf("test_gcj02: OK (3 cities offset+roundtrip, %zu out-of-China identities)\n",
                    sizeof(kOutside) / sizeof(kOutside[0]));
        return 0;
    }
    std::printf("test_gcj02: FAILED (%d assertion(s))\n", g_failed);
    return 1;
}
