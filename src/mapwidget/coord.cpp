// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — WGS-84 <-> GCJ-02 conversion (M1b-U2).
//
// Contract: docs/m1b-tasks.md §2.3 (PP-FROZEN header) + §4.1 U2. Pure math, zero Qt —
// compiled into pp_core so unit tests can link it (CMake glob pre-wired by the architect).
//
// The forward transform is the public GCJ-02 ("Mars coordinates") offset algorithm defined on
// the Krasovsky 1940 ellipsoid. The inverse has no closed form: we solve gcj = wgs + offset(wgs)
// by fixed-point iteration, which converges to ~1e-12 deg (< 0.1 mm) in a handful of rounds.

#include "mapwidget/coord.h"

#include <cmath>

namespace pp::map {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Krasovsky 1940 ellipsoid (the datum GCJ-02 is defined on).
constexpr double kAxis = 6378245.0;                       // semi-major axis [m]
constexpr double kEccentricity2 = 0.00669342162296594323; // first eccentricity squared

constexpr int kMaxInverseIterations = 10;
constexpr double kInverseToleranceDeg = 1e-13;

// Rough China bounding box (mainland + Hainan): outside it the offset is defined to be zero.
bool out_of_china(double lat, double lon) {
    return lon < 72.004 || lon > 137.8347 || lat < 0.8293 || lat > 55.8271;
}

double transform_lat(double x, double y) {
    double ret =
        -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kPi) + 20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * kPi) + 40.0 * std::sin(y / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * kPi) + 320.0 * std::sin(y * kPi / 30.0)) * 2.0 / 3.0;
    return ret;
}

double transform_lon(double x, double y) {
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kPi) + 20.0 * std::sin(2.0 * x * kPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * kPi) + 40.0 * std::sin(x / 3.0 * kPi)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * kPi) + 300.0 * std::sin(x / 30.0 * kPi)) * 2.0 / 3.0;
    return ret;
}

} // namespace

std::pair<double, double> wgs84_to_gcj02(double lat, double lon) {
    if (out_of_china(lat, lon)) {
        return {lat, lon};
    }
    const double d_lat = transform_lat(lon - 105.0, lat - 35.0);
    const double d_lon = transform_lon(lon - 105.0, lat - 35.0);

    const double rad_lat = lat / 180.0 * kPi;
    const double sin_lat = std::sin(rad_lat);
    const double magic = 1.0 - kEccentricity2 * sin_lat * sin_lat;
    const double sqrt_magic = std::sqrt(magic);

    // Offset in degrees: transform() yields metres, the divisors are the meridian /
    // prime-vertical radii of curvature at this latitude times pi (metres per 180 deg).
    const double lat_deg =
        (d_lat * 180.0) / ((kAxis * (1.0 - kEccentricity2)) / (magic * sqrt_magic) * kPi);
    const double lon_deg = (d_lon * 180.0) / (kAxis / sqrt_magic * std::cos(rad_lat) * kPi);
    return {lat + lat_deg, lon + lon_deg};
}

std::pair<double, double> gcj02_to_wgs84(double lat, double lon) {
    if (out_of_china(lat, lon)) {
        return {lat, lon};
    }
    // Fixed point: find w with w + offset(w) == (lat, lon).
    double w_lat = lat;
    double w_lon = lon;
    for (int i = 0; i < kMaxInverseIterations; ++i) {
        const std::pair<double, double> fwd = wgs84_to_gcj02(w_lat, w_lon);
        const double next_lat = w_lat + (lat - fwd.first);
        const double next_lon = w_lon + (lon - fwd.second);
        const bool converged = std::fabs(next_lat - w_lat) < kInverseToleranceDeg &&
                               std::fabs(next_lon - w_lon) < kInverseToleranceDeg;
        w_lat = next_lat;
        w_lon = next_lon;
        if (converged) {
            break;
        }
    }
    return {w_lat, w_lon};
}

} // namespace pp::map
