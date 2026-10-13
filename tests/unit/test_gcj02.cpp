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
#include "mapwidget/map_interact.h" // B.5 ⑥（M7-R2.2-G2）：交互状态机纯函数（header-only）
#include "mapwidget/map_view.h"     // B.5 ①（M7-R2.1-F2）：投影/基准纯核（pp_core）
#include "mapwidget/tile_cache.h"   // B.5 ②（M7-R2.2-G2）：TileKey/LRU/预算纯核（header-only）

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

static int run_cases() {
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

    // ---- M7-R2.2-G2 / 附录 B.5 ②：TileCache LRU/预算真值表（载荷 = int 哑载荷）----
    {
        using pp::map::TileCache;
        using pp::map::TileKey;
        TileCache<int> c(100);
        const TileKey a{"src", "road", 4, 1, 2};
        const TileKey b{"src", "road", 4, 2, 2};
        const TileKey d{"src", "road", 4, 3, 2};
        check(c.find(a) == nullptr && c.size() == 0, "tile_cache/empty-find", "expected miss");
        c.insert(a, 1, 60);
        check(c.bytes() == 60 && c.size() == 1, "tile_cache/accounting",
              "bytes=" + std::to_string(c.bytes()));
        check(c.find(a) != nullptr && *c.find(a) == 1, "tile_cache/hit", "expected payload 1");
        // LRU：touch a → b 入缓存超预算 → 逐出最久未用 = a
        c.insert(b, 2, 60);
        check(c.size() == 1 && c.find(a) == nullptr && c.find(b) != nullptr && *c.find(b) == 2,
              "tile_cache/lru-evict-oldest", "expected b survived, a evicted");
        // 队序：insert(d) 后 d 队头、b 队尾 → 超预算逐出 b（d 存活）
        c.insert(d, 3, 60);
        check(c.size() == 1 && c.find(d) != nullptr, "tile_cache/touch-order",
              "expected d survived (b was older after touch-free inserts)");
        // 同键替换：footprint 记账更新、不双计
        TileCache<int> r(1000);
        r.insert(a, 7, 40);
        r.insert(a, 8, 25);
        check(r.bytes() == 25 && r.size() == 1 && *r.find(a) == 8, "tile_cache/replace-same-key",
              "bytes=" + std::to_string(r.bytes()));
        // 预算收紧即逐出（set_budget 内含 evict）
        r.set_budget(10);
        check(r.size() == 0 && r.bytes() == 0, "tile_cache/set-budget-evict", "expected emptied");
        // 单件超预算：入后即被逐出（与原 evict_to_budget 循环逐位同义）
        TileCache<int> big(10);
        big.insert(a, 1, 999);
        check(big.size() == 0, "tile_cache/oversized-entry-evicted", "expected immediate evict");
    }

    // ---- M7-R2.2-G2 / 附录 B.5 ⑥：交互状态机真值表 ----
    {
        namespace ix = pp::map::interact;
        using ix::PointerAction;
        using ix::PointerKind;
        using ix::PointerStep;
        ix::DragState s;
        // press：左键进机；非左键不消费不动状态
        PointerStep st = ix::pointer_step(s, PointerKind::Press, QPointF(10, 10), true, false);
        check(st.action == PointerAction::Consume && st.consume && st.state.pressed,
              "interact/press-left", "expected consumed pressed state");
        s = st.state;
        st = ix::pointer_step(s, PointerKind::Press, QPointF(50, 50), false, false);
        check(st.action == PointerAction::Ignore && !st.consume && st.state.pressed == s.pressed,
              "interact/press-other-button", "expected ignore + state unchanged");
        // move：阈值内 = 消费无平移；出阈值 = 首拖整段位移补全
        st = ix::pointer_step(s, PointerKind::Move, QPointF(12, 10), false, true);
        check(st.action == PointerAction::Consume && !st.state.dragging,
              "interact/move-under-threshold", "expected no drag yet");
        st = ix::pointer_step(s, PointerKind::Move, QPointF(30, 10), false, true);
        check(st.action == PointerAction::Pan && st.state.dragging &&
                  st.pan_delta == QPointF(20, 0),
              "interact/drag-begin-full-delta", "expected pan delta 20,0 (press->cursor once)");
        // 后续 move：增量 = 相邻两点差
        s = st.state;
        st = ix::pointer_step(s, PointerKind::Move, QPointF(35, 16), false, true);
        check(st.action == PointerAction::Pan && st.pan_delta == QPointF(5, 6),
              "interact/drag-step-delta", "expected 5,6");
        // move 未按左键 = 不消费
        s = st.state;
        st = ix::pointer_step(s, PointerKind::Move, QPointF(90, 90), false, false);
        check(st.action == PointerAction::Ignore && !st.consume, "interact/move-not-held",
              "expected ignore");
        // release：拖拽后抬起 = Consume 不算点击；小幅抬起 = Click；未按下抬起 = Ignore
        s = st.state;
        st = ix::pointer_step(s, PointerKind::Release, QPointF(40, 20), true, true);
        check(st.action == PointerAction::Consume && !st.state.pressed && !st.state.dragging,
              "interact/release-after-drag", "expected no click");
        st = ix::pointer_step(ix::DragState{}, PointerKind::Press, QPointF(5, 5), true, false);
        s = st.state;
        st = ix::pointer_step(s, PointerKind::Release, QPointF(6, 5), true, false);
        check(st.action == PointerAction::Click, "interact/release-small-move", "expected click");
        st = ix::pointer_step(ix::DragState{}, PointerKind::Release, QPointF(0, 0), true, false);
        check(st.action == PointerAction::Ignore, "interact/release-without-press",
              "expected ignore");
        // 滚轮档位：±1 / clamp / dy=0
        check(ix::wheel_target_zoom(10, 120, 2, 18) == 11, "interact/wheel-up", "expected 11");
        check(ix::wheel_target_zoom(10, -120, 2, 18) == 9, "interact/wheel-down", "expected 9");
        check(ix::wheel_target_zoom(18, 120, 2, 18) == 18, "interact/wheel-clamp-hi",
              "expected 18");
        check(ix::wheel_target_zoom(4, 0, 2, 18) == 4, "interact/wheel-zero", "expected 4");
        // pan：地图内容跟手 = 世界中心位移 == −delta（往返对拍）
        const ix::Viewport v0{31.23, 121.47, 12};
        const ix::Viewport v1 = ix::pan(v0, QPointF(7, -3));
        const QPointF w0 = pp::map::latlon_to_world(v0.center_lat, v0.center_lon, v0.zoom);
        const QPointF w1 = pp::map::latlon_to_world(v1.center_lat, v1.center_lon, v1.zoom);
        check(std::fabs((w0.x() - w1.x()) - 7) < 1e-6 && std::fabs((w0.y() - w1.y()) + 3) < 1e-6,
              "interact/pan-world-delta", "centre must move by -delta in world px");
        // wheel_zoom：光标下地理点不动（锚点保持）
        const QPointF cursor(200, 120);
        const QSize size(800, 600);
        const QPointF cw = pp::map::latlon_to_world(v0.center_lat, v0.center_lon, v0.zoom);
        const QPointF cursor_world = cw + cursor - QPointF(size.width() / 2.0, size.height() / 2.0);
        const std::pair<double, double> anchor = pp::map::world_to_latlon(cursor_world, 12);
        const ix::Viewport vz = ix::wheel_zoom(v0, 120, cursor, size, 2, 18);
        check(vz.zoom == 13, "interact/wheel-zoom-step", "expected zoom 13");
        const QPointF cwz = pp::map::latlon_to_world(vz.center_lat, vz.center_lon, vz.zoom);
        const QPointF cursor_world_z =
            cwz + cursor - QPointF(size.width() / 2.0, size.height() / 2.0);
        const std::pair<double, double> anchor_z = pp::map::world_to_latlon(cursor_world_z, 13);
        check(std::fabs(anchor_z.first - anchor.first) < 1e-9 &&
                  std::fabs(anchor_z.second - anchor.second) < 1e-9,
              "interact/wheel-anchor-pinned", "cursor geographic point must not move");
        // 图钉命中：最近者优先 / 并列先者优先 / 半径外不命中
        const std::vector<QPointF> pins = {QPointF(100, 100), QPointF(110, 100), QPointF(300, 300)};
        check(ix::hit_test_pins(pins, QPointF(101, 100), 13.0) == 0, "interact/pin-hit-nearest",
              "expected pin 0");
        check(ix::hit_test_pins(pins, QPointF(105, 100), 13.0) == 0, "interact/pin-hit-tie-first",
              "expected pin 0 (tie keeps first)");
        check(ix::hit_test_pins(pins, QPointF(400, 400), 13.0) == -1, "interact/pin-miss",
              "expected -1");
    }

    if (g_failed == 0) {
        std::printf("test_gcj02: OK (3 cities offset+roundtrip, %zu out-of-China identities)\n",
                    sizeof(kOutside) / sizeof(kOutside[0]));
        return 0;
    }
    std::printf("test_gcj02: FAILED (%d assertion(s))\n", g_failed);
    return 1;
}

int main() {
    // main 不得让异常外泄（外泄=terminate，bugprone-exception-escape）：run_cases 整体罩住；
    // 异常路径返回 1（非零=失败，ctest 判定不变），断言与成功路径逐字不动。
    try {
        return run_cases();
    } catch (...) {
        std::printf("test_gcj02: FAILED (unexpected exception)\n");
        return 1;
    }
}
