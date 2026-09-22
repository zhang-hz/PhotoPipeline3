// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T5 — metadata layer unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.7 (PP-FROZEN) / §4.5 and design §5.2–§5.6.
// Covers: read/payloads/plan synthesis matrix/GPS/privacy/edits/effective datetime/
//         metadata-only byte fidelity (Spike F method)/PNG eXIf (R1)/mtime.
// Scratch files live under <repo>/.cache/tmp/m1-t5/ (never outside the repository).
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <exiv2/exiv2.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/logger.h"
#include "core/metadata.h"
#include "core/pipeline.h"  // frozen format_supports_metadata_only() (weak fallback in metadata.cpp)
#include "env_compat.h"  // M3 v1.5: POSIX env API 薄垫层

namespace fs = std::filesystem;

namespace pp {
// Internal helpers of src/core/metadata.cpp. Deliberately outside the frozen header: the mirror
// fallback keeps R1 coverage in a build where Exiv2 cannot open PNG at all, and the capability
// probe is what T8's format_supports_metadata_only() should delegate to.
std::size_t detail_mirror_key_exif_to_xmp(const Exiv2::ExifData& exif, Exiv2::XmpData& xmp);
bool detail_metadata_only_supported(std::string_view format_id);
}  // namespace pp

namespace {

int g_failed = 0;

void fail(const std::string& c, const std::string& d) {
    std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
    ++g_failed;
}

void check(bool ok, const std::string& c, const std::string& d) {
    if (!ok) fail(c, d);
}

std::string show(const std::string& s) { return "'" + s + "'"; }
std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return std::string(buf);
}

// ------------------------------------------------------------------ paths / fixtures

fs::path repo_root() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec)) return p;
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

fs::path corpus() { return repo_root() / "tests" / "golden"; }

fs::path tmp_dir(const std::string& name) {
    std::error_code ec;
    const fs::path d = repo_root() / ".cache" / "tmp" / "m1-t5" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

bool copy_fixture(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

// ------------------------------------------------------------------ exiv2 helpers

std::string exif_str(const Exiv2::ExifData& d, const char* key) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end()) return {};
    std::string v = it->toString();
    while (!v.empty() && v.back() == '\0') v.pop_back();
    return v;
}

std::string xmp_str(const Exiv2::XmpData& d, const char* key) {
    auto it = d.findKey(Exiv2::XmpKey(key));
    if (it == d.end()) return {};
    std::string v = it->toString();
    while (!v.empty() && v.back() == '\0') v.pop_back();
    return v;
}

bool has_exif(const Exiv2::ExifData& d, const char* key) {
    return d.findKey(Exiv2::ExifKey(key)) != d.end();
}
bool has_xmp(const Exiv2::XmpData& d, const char* key) {
    return d.findKey(Exiv2::XmpKey(key)) != d.end();
}

double rational_at(const Exiv2::ExifData& d, const char* key, std::size_t n) {
    auto it = d.findKey(Exiv2::ExifKey(key));
    if (it == d.end()) return std::nan("");
    const Exiv2::Rational r = it->value().toRational(n);
    if (r.second == 0) return std::nan("");
    return static_cast<double>(r.first) / static_cast<double>(r.second);
}

// deg + min/60 + sec/3600 from the three EXIF rationals.
double dms_to_degrees(const Exiv2::ExifData& d, const char* key) {
    return rational_at(d, key, 0) + rational_at(d, key, 1) / 60.0 + rational_at(d, key, 2) / 3600.0;
}

bool exiv2_supports(const fs::path& p) {
    try {
        return Exiv2::ImageFactory::getType(p.string()) != Exiv2::ImageType::none;
    } catch (...) {
        return false;
    }
}

// ------------------------------------------------------------------ byte / pixel helpers

std::vector<uint8_t> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// First SOS marker (0xFF 0xDA) — JPEG entropy data is byte-stuffed, so the first hit is the
// scan header. Same method as M0 Spike F (docs/m0-tasks.md §12.3).
std::size_t find_sos(const std::vector<uint8_t>& data) {
    for (std::size_t i = 0; i + 1 < data.size(); ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0xDA) return i;
    }
    return std::string::npos;
}

std::string pixel_hash(const fs::path& p) {
    try {
        return OIIO::ImageBufAlgo::computePixelHashSHA1(OIIO::ImageBuf(p.string()));
    } catch (...) {
        return {};
    }
}

// ------------------------------------------------------------------ plan helpers

pp::TimeShift delta(int years = 0, int months = 0, int days = 0, int hours = 0, int minutes = 0,
                    int seconds = 0) {
    pp::TimeShift s;
    s.mode = pp::TimeShift::Mode::Delta;
    s.years = years;
    s.months = months;
    s.days = days;
    s.hours = hours;
    s.minutes = minutes;
    s.seconds = seconds;
    return s;
}

bool has_warning(const pp::MetadataPlan& p, pp::WarningKind k) {
    for (const pp::Warning& w : p.warnings) {
        if (w.kind == k) return true;
    }
    return false;
}

std::string warning_detail(const pp::MetadataPlan& p, pp::WarningKind k) {
    for (const pp::Warning& w : p.warnings) {
        if (w.kind == k) return w.detail;
    }
    return {};
}

// ===========================================================================
// cases
// ===========================================================================

void test_read_metadata(const fs::path& tmp) {
    const fs::path jpg = corpus() / "meta" / "exif_full.jpg";
    const pp::SourceMeta src = pp::read_metadata(jpg);
    check(src.error.empty(), "read/jpg/no-error", src.error);
    check(src.has_time, "read/jpg/has-time", "expected true");
    check(src.has_gps, "read/jpg/has-gps", "expected true");
    check(exif_str(src.exif, "Exif.Image.Artist") == "M0", "read/jpg/artist",
          show(exif_str(src.exif, "Exif.Image.Artist")));
    check(src.orientation >= 1 && src.orientation <= 8, "read/jpg/orientation",
          std::to_string(src.orientation));
    check(src.icc.empty(), "read/jpg/no-icc", "fixture has no ICC");

    const pp::SourceMeta missing = pp::read_metadata(tmp / "does-not-exist.jpg");
    check(!missing.error.empty(), "read/missing/error", "expected a non-empty error");
    check(missing.exif.empty() && missing.xmp.empty(), "read/missing/empty-containers", "");
    check(!missing.has_time && !missing.has_gps && missing.orientation == 1,
          "read/missing/defaults", "expected defaults");

    const fs::path tif = corpus() / "base" / "rgb8.tif";
    const pp::SourceMeta t = pp::read_metadata(tif);
    check(t.error.empty(), "read/tif/no-error", t.error);
    check(!t.has_gps, "read/tif/no-gps", "expected false");

    // R1-adjacent environment fact: PNG readability depends on the exiv2 build (needs zlib).
    const fs::path png = corpus() / "base" / "rgb8.png";
    const bool png_ok = exiv2_supports(png);
    const pp::SourceMeta p = pp::read_metadata(png);
    if (png_ok) {
        check(p.error.empty(), "read/png/no-error", p.error);
    } else {
        check(!p.error.empty(), "read/png/unsupported-error", "expected an error in this build");
    }
    std::printf("info exiv2 can open png: %s\n", png_ok ? "yes" : "no");
}

void test_gps() {
    // ---- decimal -> DMS -> decimal (error < 1e-6 deg) ----
    Exiv2::ExifData exif;
    pp::GpsData g;
    g.lat = 31.2304;
    g.lon = 121.4737;
    g.altitude = 4.5;
    g.direction = 90.0;
    g.timestamp = std::string("2024:03:01 02:30:00");
    pp::write_gps(exif, g);

    check(exif_str(exif, "Exif.GPSInfo.GPSVersionID") == "2 2 0 0", "gps/version-id",
          show(exif_str(exif, "Exif.GPSInfo.GPSVersionID")));
    check(exif_str(exif, "Exif.GPSInfo.GPSLatitudeRef") == "N", "gps/lat-ref",
          show(exif_str(exif, "Exif.GPSInfo.GPSLatitudeRef")));
    check(exif_str(exif, "Exif.GPSInfo.GPSLongitudeRef") == "E", "gps/lon-ref",
          show(exif_str(exif, "Exif.GPSInfo.GPSLongitudeRef")));
    const std::string lat_s = exif_str(exif, "Exif.GPSInfo.GPSLatitude");
    check(lat_s.find('-') == std::string::npos, "gps/lat-magnitude-only", show(lat_s));
    check(std::fabs(dms_to_degrees(exif, "Exif.GPSInfo.GPSLatitude") - 31.2304) < 1e-6,
          "gps/lat-roundtrip", num(dms_to_degrees(exif, "Exif.GPSInfo.GPSLatitude")));
    check(std::fabs(dms_to_degrees(exif, "Exif.GPSInfo.GPSLongitude") - 121.4737) < 1e-6,
          "gps/lon-roundtrip", num(dms_to_degrees(exif, "Exif.GPSInfo.GPSLongitude")));
    check(exif_str(exif, "Exif.GPSInfo.GPSAltitudeRef") == "0", "gps/altitude-ref",
          show(exif_str(exif, "Exif.GPSInfo.GPSAltitudeRef")));
    check(std::fabs(rational_at(exif, "Exif.GPSInfo.GPSAltitude", 0) - 4.5) < 1e-9,
          "gps/altitude-value", num(rational_at(exif, "Exif.GPSInfo.GPSAltitude", 0)));
    check(exif_str(exif, "Exif.GPSInfo.GPSImgDirectionRef") == "T", "gps/direction-ref",
          show(exif_str(exif, "Exif.GPSInfo.GPSImgDirectionRef")));
    check(std::fabs(rational_at(exif, "Exif.GPSInfo.GPSImgDirection", 0) - 90.0) < 1e-6,
          "gps/direction-value", num(rational_at(exif, "Exif.GPSInfo.GPSImgDirection", 0)));
    check(exif_str(exif, "Exif.GPSInfo.GPSDateStamp") == "2024:03:01", "gps/date-stamp",
          show(exif_str(exif, "Exif.GPSInfo.GPSDateStamp")));
    check(exif_str(exif, "Exif.GPSInfo.GPSTimeStamp") == "2/1 30/1 0/1", "gps/time-stamp",
          show(exif_str(exif, "Exif.GPSInfo.GPSTimeStamp")));

    // ---- southern / western hemisphere + optional fields absent ----
    Exiv2::ExifData south;
    pp::GpsData g2;
    g2.lat = -33.8688;
    g2.lon = -70.6693;
    pp::write_gps(south, g2);
    check(exif_str(south, "Exif.GPSInfo.GPSLatitudeRef") == "S", "gps/south-ref",
          show(exif_str(south, "Exif.GPSInfo.GPSLatitudeRef")));
    check(exif_str(south, "Exif.GPSInfo.GPSLongitudeRef") == "W", "gps/west-ref",
          show(exif_str(south, "Exif.GPSInfo.GPSLongitudeRef")));
    // G7: the rationals carry the magnitude only — the hemisphere lives in *Ref
    check(std::fabs(dms_to_degrees(south, "Exif.GPSInfo.GPSLatitude") - 33.8688) < 1e-6,
          "gps/south-roundtrip", num(dms_to_degrees(south, "Exif.GPSInfo.GPSLatitude")));
    check(std::fabs(dms_to_degrees(south, "Exif.GPSInfo.GPSLongitude") - 70.6693) < 1e-6,
          "gps/west-roundtrip", num(dms_to_degrees(south, "Exif.GPSInfo.GPSLongitude")));
    check(!has_exif(south, "Exif.GPSInfo.GPSAltitude") && !has_exif(south, "Exif.GPSInfo.GPSImgDirection") &&
              !has_exif(south, "Exif.GPSInfo.GPSDateStamp"),
          "gps/optionals-absent", "no optional tags may be written");

    // ---- a second write_gps() replaces the whole block (no stale optionals) ----
    pp::write_gps(exif, g2);
    check(!has_exif(exif, "Exif.GPSInfo.GPSAltitude") &&
              !has_exif(exif, "Exif.GPSInfo.GPSAltitudeRef") &&
              !has_exif(exif, "Exif.GPSInfo.GPSTimeStamp"),
          "gps/rewrite-clears-stale", "stale optional GPS tags survived a rewrite");

    // ---- clear_gps() removes the whole block ----
    pp::clear_gps(exif);
    long gps_left = 0;
    for (auto it = exif.begin(); it != exif.end(); ++it) {
        if (it->key().rfind("Exif.GPSInfo.", 0) == 0) ++gps_left;
    }
    check(gps_left == 0, "gps/clear", std::to_string(gps_left) + " GPS tag(s) left");
}

void test_privacy(const fs::path& tmp) {
    const fs::path jpg = corpus() / "meta" / "exif_full.jpg";
    const pp::SourceMeta src = pp::read_metadata(jpg);

    pp::BatchRules rules;
    rules.strip_privacy = true;
    rules.time_shift = delta(0, 0, 0, 1);  // must be ignored: strip wins
    pp::GpsData g;
    g.lat = 1.0;
    g.lon = 2.0;
    rules.gps = g;
    rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("X"), false});
    rules.xmp_edits.push_back(pp::TagEdit{"Xmp.dc.title", std::string("X"), false});

    const pp::MetadataPlan plan = pp::build_plan(src, rules, std::nullopt);
    check(plan.exif.empty(), "privacy/plan-exif-empty", std::to_string(plan.exif.count()) + " tag(s)");
    check(plan.xmp.empty(), "privacy/plan-xmp-empty", std::to_string(plan.xmp.count()) + " prop(s)");
    check(!plan.has_time && plan.datetime_original.empty(), "privacy/no-time",
          show(plan.datetime_original));

    // byte-level: JPEG exif/xmp sections must be gone after the rewrite, pixels untouched
    const fs::path out = tmp / "stripped.jpg";
    check(copy_fixture(jpg, out), "privacy/copy", out.string());
    const std::string h0 = pixel_hash(jpg);
    const std::string err = pp::rewrite_metadata_only(jpg, out, plan, pp::make_payloads(plan));
    check(err.empty(), "privacy/rewrite", err);
    const pp::SourceMeta back = pp::read_metadata(out);
    check(back.error.empty() && back.exif.empty() && back.xmp.empty(), "privacy/readback-empty",
          "exif=" + std::to_string(back.exif.count()) + " xmp=" + std::to_string(back.xmp.count()));
    check(!back.has_time && !back.has_gps, "privacy/readback-flags", "time/gps must be gone");
    check(pixel_hash(out) == h0, "privacy/pixels-unchanged", "pixel hash changed");

    // three-state override: exception false beats a batch true
    pp::MetadataOverride keep;
    keep.strip_privacy = false;
    const pp::MetadataPlan kept = pp::build_plan(src, rules, keep);
    check(!kept.exif.empty(), "privacy/override-false", "strip must be disabled by the exception");
    // and the exception can switch it on when the batch rule is off
    pp::MetadataOverride strip;
    strip.strip_privacy = true;
    const pp::MetadataPlan stripped = pp::build_plan(src, pp::BatchRules{}, strip);
    check(stripped.exif.empty() && stripped.xmp.empty(), "privacy/override-true", "strip not applied");
}

void test_edits() {
    Exiv2::ExifData exif;
    Exiv2::XmpData xmp;
    std::vector<std::string> errors;

    std::vector<pp::TagEdit> ee;
    ee.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("Zhang"), false});
    ee.push_back(pp::TagEdit{"Exif.Photo.DateTimeOriginal", std::string("2024:03:01 10:00:00"), false});
    std::vector<pp::TagEdit> xe;
    xe.push_back(pp::TagEdit{"Xmp.dc.title", std::string("demo"), false});
    xe.push_back(pp::TagEdit{"Xmp.xmp:CreateDate", std::string("2024-03-01T10:00:00"), false});
    pp::apply_edits(exif, xmp, ee, xe, errors);
    check(errors.empty(), "edit/set-no-errors", errors.empty() ? "" : errors[0]);
    check(exif_str(exif, "Exif.Image.Artist") == "Zhang", "edit/set-exif",
          show(exif_str(exif, "Exif.Image.Artist")));
    check(exif_str(exif, "Exif.Photo.DateTimeOriginal") == "2024:03:01 10:00:00", "edit/set-exif-time", "");
    check(xmp_str(xmp, "Xmp.dc.title").find("demo") != std::string::npos, "edit/set-xmp-langalt",
          show(xmp_str(xmp, "Xmp.dc.title")));
    check(has_xmp(xmp, "Xmp.xmp.CreateDate"), "edit/xmp-colon-key-normalized",
          "the 'Xmp.xmp:CreateDate' notation must map to the Exiv2 dotted key");

    // remove: remove flag and nullopt value are equivalent
    std::vector<pp::TagEdit> del{pp::TagEdit{"Exif.Image.Artist", std::nullopt, true}};
    errors.clear();
    pp::apply_edits(exif, xmp, del, {}, errors);
    check(errors.empty() && !has_exif(exif, "Exif.Image.Artist"), "edit/remove-flag",
          errors.empty() ? "tag still present" : errors[0]);
    std::vector<pp::TagEdit> del2{pp::TagEdit{"Xmp.dc.title", std::nullopt, false}};
    errors.clear();
    pp::apply_edits(exif, xmp, {}, del2, errors);
    check(errors.empty() && !has_xmp(xmp, "Xmp.dc.title"), "edit/remove-nullopt", "");
    // removing a missing tag is a no-op, not an error
    errors.clear();
    pp::apply_edits(exif, xmp, del, {}, errors);
    check(errors.empty(), "edit/remove-missing-is-noop", errors.empty() ? "" : errors[0]);

    // invalid keys / wrong container / empty key / type mismatch → errors, never throw
    errors.clear();
    std::vector<pp::TagEdit> bad{pp::TagEdit{"Exif.Image.NoSuchTag", std::string("x"), false}};
    pp::apply_edits(exif, xmp, bad, {}, errors);
    check(errors.size() == 1, "edit/invalid-key",
          "expected 1 error, got " + std::to_string(errors.size()));
    errors.clear();
    std::vector<pp::TagEdit> badns{pp::TagEdit{"Xmp.nosuchns.foo", std::string("x"), false}};
    pp::apply_edits(exif, xmp, {}, badns, errors);
    check(errors.size() == 1, "edit/invalid-xmp-namespace",
          "expected 1 error, got " + std::to_string(errors.size()));
    errors.clear();
    std::vector<pp::TagEdit> wrong{pp::TagEdit{"Xmp.dc.title", std::string("x"), false}};
    pp::apply_edits(exif, xmp, wrong, {}, errors);
    check(errors.size() == 1, "edit/wrong-container",
          "an Xmp key in exif_edits must be rejected");
    errors.clear();
    std::vector<pp::TagEdit> empty_key{pp::TagEdit{"", std::string("x"), false}};
    pp::apply_edits(exif, xmp, empty_key, {}, errors);
    check(errors.size() == 1, "edit/empty-key", "expected 1 error");

    errors.clear();
    std::vector<pp::TagEdit> type_set{pp::TagEdit{"Exif.Image.Orientation", std::string("6"), false}};
    pp::apply_edits(exif, xmp, type_set, {}, errors);
    check(errors.empty() && exif_str(exif, "Exif.Image.Orientation") == "6", "edit/set-numeric", "");
    errors.clear();
    std::vector<pp::TagEdit> type_bad{pp::TagEdit{"Exif.Image.Orientation", std::string("abc"), false}};
    pp::apply_edits(exif, xmp, type_bad, {}, errors);
    check(errors.size() == 1, "edit/type-mismatch",
          "expected 1 error, got " + std::to_string(errors.size()));
    check(exif_str(exif, "Exif.Image.Orientation") == "6", "edit/type-mismatch-keeps-old-value",
          show(exif_str(exif, "Exif.Image.Orientation")));

    errors.clear();
    std::vector<pp::TagEdit> type_new{pp::TagEdit{"Exif.Image.YCbCrPositioning", std::string("zz"), false}};
    pp::apply_edits(exif, xmp, type_new, {}, errors);
    check(errors.size() == 1 && !has_exif(exif, "Exif.Image.YCbCrPositioning"),
          "edit/type-mismatch-new-tag-dropped",
          errors.empty() ? "no error reported" : "empty tag left behind");
}

void test_build_plan() {
    const pp::SourceMeta src = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");
    const char* kDto = "Exif.Photo.DateTimeOriginal";

    pp::BatchRules rules;
    rules.time_shift = delta(0, 0, 0, 1);  // +1h
    pp::GpsData g;
    g.lat = 1.5;
    g.lon = 2.5;
    rules.gps = g;
    rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("BATCH"), false});
    rules.xmp_edits.push_back(pp::TagEdit{"Xmp.dc.title", std::string("BATCH"), false});

    // ---- batch only ----
    const pp::MetadataPlan p1 = pp::build_plan(src, rules, std::nullopt);
    check(exif_str(p1.exif, kDto) == "2024:03:01 11:00:00", "plan/batch/time",
          show(exif_str(p1.exif, kDto)));
    check(exif_str(p1.exif, "Exif.Image.Artist") == "BATCH", "plan/batch/artist",
          show(exif_str(p1.exif, "Exif.Image.Artist")));
    check(xmp_str(p1.xmp, "Xmp.dc.title").find("BATCH") != std::string::npos, "plan/batch/xmp", "");
    check(std::fabs(dms_to_degrees(p1.exif, "Exif.GPSInfo.GPSLatitude") - 1.5) < 1e-6,
          "plan/batch/gps", num(dms_to_degrees(p1.exif, "Exif.GPSInfo.GPSLatitude")));
    check(p1.has_time && p1.datetime_original == "2024:03:01 11:00:00", "plan/batch/effective",
          show(p1.datetime_original));
    check(p1.warnings.empty(), "plan/batch/no-warnings", "unexpected warning");

    // ---- inherit: an empty exception changes nothing ----
    const pp::MetadataPlan p2 = pp::build_plan(src, rules, pp::MetadataOverride{});
    check(exif_str(p2.exif, kDto) == exif_str(p1.exif, kDto) &&
              exif_str(p2.exif, "Exif.Image.Artist") == "BATCH" &&
              std::fabs(dms_to_degrees(p2.exif, "Exif.GPSInfo.GPSLatitude") - 1.5) < 1e-6,
          "plan/inherit", "an empty exception must reproduce the batch result");

    // ---- override: per-item replacement, not accumulation ----
    pp::MetadataOverride ex;
    ex.time_shift = delta(0, 0, 0, 2);  // replaces +1h (10:00 -> 12:00)
    ex.gps_clear = true;                // clears the batch GPS and the source GPS
    ex.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("EX"), false});
    const pp::MetadataPlan p3 = pp::build_plan(src, rules, ex);
    check(exif_str(p3.exif, kDto) == "2024:03:01 12:00:00", "plan/override/time",
          show(exif_str(p3.exif, kDto)));
    check(exif_str(p3.exif, "Exif.Image.Artist") == "EX", "plan/override/edit-wins",
          show(exif_str(p3.exif, "Exif.Image.Artist")));
    check(!has_exif(p3.exif, "Exif.GPSInfo.GPSLatitude") && !has_exif(p3.exif, "Exif.GPSInfo.GPSLatitudeRef"),
          "plan/override/gps-clear", "GPS must be gone");
    check(xmp_str(p3.xmp, "Xmp.dc.title").find("BATCH") != std::string::npos,
          "plan/override/untouched-xmp", "xmp edits were not overridden");
    check(p3.has_time && p3.datetime_original == "2024:03:01 12:00:00", "plan/override/effective", "");

    // ---- ignore_batch: the batch rules disappear, the source survives ----
    pp::MetadataOverride ign;
    ign.ignore_batch = true;
    const pp::MetadataPlan p4 = pp::build_plan(src, rules, ign);
    check(exif_str(p4.exif, kDto) == "2024:03:01 10:00:00", "plan/ignore-batch/time",
          show(exif_str(p4.exif, kDto)));
    check(exif_str(p4.exif, "Exif.Image.Artist") == "M0", "plan/ignore-batch/no-batch-edit",
          show(exif_str(p4.exif, "Exif.Image.Artist")));
    check(xmp_str(p4.xmp, "Xmp.dc.title").empty(), "plan/ignore-batch/no-batch-xmp", "");
    check(std::fabs(dms_to_degrees(p4.exif, "Exif.GPSInfo.GPSLatitude") - 31.2304) < 1e-6,
          "plan/ignore-batch/source-gps-kept",
          num(dms_to_degrees(p4.exif, "Exif.GPSInfo.GPSLatitude")));

    // ---- ignore_batch + own exception fields ----
    pp::MetadataOverride ign2;
    ign2.ignore_batch = true;
    ign2.time_shift = delta(0, 0, 0, 3);
    ign2.gps_clear = true;
    ign2.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("EX2"), false});
    const pp::MetadataPlan p5 = pp::build_plan(src, rules, ign2);
    check(exif_str(p5.exif, kDto) == "2024:03:01 13:00:00" &&
              exif_str(p5.exif, "Exif.Image.Artist") == "EX2" &&
              !has_exif(p5.exif, "Exif.GPSInfo.GPSLatitude"),
          "plan/ignore-batch/own-fields",
          show(exif_str(p5.exif, kDto)) + " " + show(exif_str(p5.exif, "Exif.Image.Artist")));

    // ---- strip_privacy three-state ----
    pp::BatchRules rs = rules;
    rs.strip_privacy = true;
    check(pp::build_plan(src, rs, std::nullopt).exif.empty(), "plan/strip/batch", "");
    pp::MetadataOverride off;
    off.strip_privacy = false;
    const pp::MetadataPlan p7 = pp::build_plan(src, rs, off);
    check(!p7.exif.empty() && exif_str(p7.exif, "Exif.Image.Artist") == "BATCH",
          "plan/strip/exception-off", "the exception must switch the batch strip off");
    pp::MetadataOverride on;
    on.strip_privacy = true;
    check(pp::build_plan(src, rules, on).exif.empty(), "plan/strip/exception-on", "");

    // ---- no time field anywhere → TimeFieldMissing, shift skipped ----
    const pp::SourceMeta blank;
    pp::BatchRules tshift;
    tshift.time_shift = delta(0, 0, 0, 1);
    const pp::MetadataPlan p9 = pp::build_plan(blank, tshift, std::nullopt);
    check(p9.warnings.size() == 1 && has_warning(p9, pp::WarningKind::TimeFieldMissing),
          "plan/time-field-missing", "expected exactly one TimeFieldMissing warning");
    check(!p9.has_time && p9.datetime_original.empty(), "plan/time-field-missing/flags", "");

    // ---- timezone semantic: wall clock + OffsetTime* ----
    pp::BatchRules tz;
    pp::TimeShift tzs;
    tzs.mode = pp::TimeShift::Mode::TimezoneSemantic;
    tzs.from_offset_min = 480;
    tzs.to_offset_min = 540;
    tz.time_shift = tzs;
    const pp::MetadataPlan p10 = pp::build_plan(src, tz, std::nullopt);
    check(exif_str(p10.exif, kDto) == "2024:03:01 11:00:00", "plan/tz/wall-clock",
          show(exif_str(p10.exif, kDto)));
    check(exif_str(p10.exif, "Exif.Photo.OffsetTime") == "+09:00" &&
              exif_str(p10.exif, "Exif.Photo.OffsetTimeOriginal") == "+09:00" &&
              exif_str(p10.exif, "Exif.Photo.OffsetTimeDigitized") == "+09:00",
          "plan/tz/offset-time",
          show(exif_str(p10.exif, "Exif.Photo.OffsetTime")) + " " +
              show(exif_str(p10.exif, "Exif.Photo.OffsetTimeOriginal")) + " " +
              show(exif_str(p10.exif, "Exif.Photo.OffsetTimeDigitized")));
    check(p10.datetime_original == "2024:03:01 11:00:00", "plan/tz/effective",
          show(p10.datetime_original));

    // ---- a no-op shift leaves everything alone (and does not warn) ----
    pp::BatchRules noop;
    pp::TimeShift same;
    same.mode = pp::TimeShift::Mode::TimezoneSemantic;
    same.from_offset_min = 480;
    same.to_offset_min = 480;
    noop.time_shift = same;
    const pp::MetadataPlan p11 = pp::build_plan(src, noop, std::nullopt);
    check(exif_str(p11.exif, kDto) == "2024:03:01 10:00:00" && p11.warnings.empty() &&
              !has_exif(p11.exif, "Exif.Photo.OffsetTime"),
          "plan/noop-shift", "a no-op shift must not touch anything");

    // ---- XMP dates are shifted too (only when present) ----
    pp::SourceMeta xs;  // EXIF-empty source: the XMP dates are the only time fields
    xs.xmp["Xmp.xmp.CreateDate"] = "2024-03-01T10:00:00.250+08:00";
    xs.xmp["Xmp.xmp.ModifyDate"] = "2024-03-02T00:30:00Z";

    const pp::MetadataPlan q1 = pp::build_plan(xs, rules, std::nullopt);  // delta +1h
    check(xmp_str(q1.xmp, "Xmp.xmp.CreateDate") == "2024-03-01T11:00:00.250+08:00",
          "plan/xmp-delta/create", show(xmp_str(q1.xmp, "Xmp.xmp.CreateDate")));
    check(xmp_str(q1.xmp, "Xmp.xmp.ModifyDate") == "2024-03-02T01:30:00Z", "plan/xmp-delta/modify",
          show(xmp_str(q1.xmp, "Xmp.xmp.ModifyDate")));
    check(q1.warnings.empty(), "plan/xmp-delta/no-time-missing",
          "an XMP-only time field must count as present");
    check(q1.has_time && q1.datetime_original == "2024:03:01 11:00:00", "plan/xmp-delta/effective",
          show(q1.datetime_original));

    const pp::MetadataPlan q2 = pp::build_plan(xs, tz, std::nullopt);  // +08 -> +09
    check(xmp_str(q2.xmp, "Xmp.xmp.CreateDate") == "2024-03-01T11:00:00.250+09:00",
          "plan/xmp-tz/create", show(xmp_str(q2.xmp, "Xmp.xmp.CreateDate")));
    check(xmp_str(q2.xmp, "Xmp.xmp.ModifyDate") == "2024-03-02T09:30:00+09:00", "plan/xmp-tz/modify",
          show(xmp_str(q2.xmp, "Xmp.xmp.ModifyDate")));
}

void test_payloads() {
    const pp::SourceMeta src = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");
    pp::BatchRules rules;
    rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("BATCH"), false});
    rules.xmp_edits.push_back(pp::TagEdit{"Xmp.dc.title", std::string("demo"), false});
    const pp::MetadataPlan plan = pp::build_plan(src, rules, std::nullopt);
    const pp::Payloads pl = pp::make_payloads(plan);

    const std::string ii("II*\0", 4);
    const std::string mm("MM\0*", 4);
    check(!pl.exif_blob.empty(), "payload/exif-non-empty", "expected a TIFF blob");
    check(pl.exif_blob.compare(0, 4, ii) == 0 || pl.exif_blob.compare(0, 4, mm) == 0,
          "payload/exif-tiff-header",
          "prefix bytes differ from II*\\0 / MM\\0*");
    Exiv2::ExifData back;
    Exiv2::ExifParser::decode(back, reinterpret_cast<const Exiv2::byte*>(pl.exif_blob.data()),
                              pl.exif_blob.size());
    check(exif_str(back, "Exif.Image.Artist") == "BATCH", "payload/exif-decode-roundtrip",
          show(exif_str(back, "Exif.Image.Artist")));
    check(exif_str(back, "Exif.Photo.DateTimeOriginal") == "2024:03:01 10:00:00",
          "payload/exif-decode-time", show(exif_str(back, "Exif.Photo.DateTimeOriginal")));

    check(!pl.xmp_rdf.empty() && pl.xmp_rdf.find("x:xmpmeta") != std::string::npos,
          "payload/xmp-non-empty", "expected an RDF packet");
    Exiv2::XmpData xback;
    check(Exiv2::XmpParser::decode(xback, pl.xmp_rdf) == 0 &&
              xmp_str(xback, "Xmp.dc.title").find("demo") != std::string::npos,
          "payload/xmp-decode-roundtrip", show(xmp_str(xback, "Xmp.dc.title")));

    // privacy strip → empty payloads (nothing to inject)
    pp::BatchRules strip;
    strip.strip_privacy = true;
    const pp::Payloads empty = pp::make_payloads(pp::build_plan(src, strip, std::nullopt));
    check(empty.exif_blob.empty() && empty.xmp_rdf.empty(), "payload/stripped-empty", "");
}

void test_effective_datetime() {
    {
        Exiv2::ExifData e;
        Exiv2::XmpData x;
        e["Exif.Image.DateTime"] = "2023:01:01 00:00:00";
        check(pp::effective_datetime(e, x) == "2023:01:01 00:00:00", "effective/image-datetime",
              show(pp::effective_datetime(e, x)));
    }
    {
        Exiv2::ExifData e;
        Exiv2::XmpData x;
        e["Exif.Image.DateTime"] = "2023:01:01 00:00:00";
        e["Exif.Photo.DateTimeOriginal"] = "2024:03:01 10:00:00";
        check(pp::effective_datetime(e, x) == "2024:03:01 10:00:00", "effective/dto-wins",
              show(pp::effective_datetime(e, x)));
    }
    {
        Exiv2::ExifData e;
        Exiv2::XmpData x;
        x["Xmp.xmp.CreateDate"] = "2024-03-01T10:00:00+08:00";
        check(pp::effective_datetime(e, x) == "2024:03:01 10:00:00", "effective/xmp-iso",
              show(pp::effective_datetime(e, x)));
    }
    {
        Exiv2::ExifData e;
        Exiv2::XmpData x;
        e["Exif.Photo.DateTimeOriginal"] = "2024:03:01 10:00:00";
        x["Xmp.xmp.CreateDate"] = "2020-01-01T00:00:00";
        check(pp::effective_datetime(e, x) == "2024:03:01 10:00:00", "effective/exif-over-xmp",
              show(pp::effective_datetime(e, x)));
    }
    {
        Exiv2::ExifData e;
        Exiv2::XmpData x;
        x["Xmp.xmp.CreateDate"] = "2024-03-01T10:00:00.500Z";
        check(pp::effective_datetime(e, x) == "2024:03:01 10:00:00", "effective/fraction-and-zulu",
              show(pp::effective_datetime(e, x)));
    }
    {
        // The task book writes the property notation "Xmp.xmp:CreateDate"; apply_edits() must
        // normalize it to the Exiv2 dotted key, and effective_datetime() must read it back.
        Exiv2::ExifData e;
        Exiv2::XmpData x;
        std::vector<std::string> errors;
        const std::vector<pp::TagEdit> edits{
            pp::TagEdit{"Xmp.xmp:CreateDate", std::string("2024-03-01T10:00:00"), false}};
        pp::apply_edits(e, x, {}, edits, errors);
        check(errors.empty() && has_xmp(x, "Xmp.xmp.CreateDate"), "effective/colon-key-normalized",
              errors.empty() ? "key missing" : errors[0]);
        check(pp::effective_datetime(e, x) == "2024:03:01 10:00:00", "effective/colon-key-read",
              show(pp::effective_datetime(e, x)));
    }
    {
        Exiv2::ExifData e;
        Exiv2::XmpData x;
        check(pp::effective_datetime(e, x).empty(), "effective/none", "expected an empty string");
    }
}

// WebP/RIFF: concatenation of the image payload chunks. Exiv2 legitimately adds a VP8X
// extended-format header once metadata is present, so container/metadata chunks (VP8X, EXIF,
// XMP , ICCP) are excluded and only the coded image (and animation) chunks are compared.
std::string riff_payload(const fs::path& p) {
    const std::vector<uint8_t> b = read_bytes(p);
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0) return {};
    const auto u32 = [&b](std::size_t o) {
        return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
               (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
    };
    std::string out;
    std::size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const std::string id(reinterpret_cast<const char*>(b.data() + pos), 4);
        const uint32_t sz = u32(pos + 4);
        const std::size_t end = std::min(b.size(), pos + 8 + sz);
        if (id == "VP8 " || id == "VP8L" || id == "ALPH" || id == "ANIM" || id == "ANMF") {
            out.append(reinterpret_cast<const char*>(b.data() + pos), end - pos);
        }
        pos = end + (sz & 1u);  // RIFF chunks are word aligned
    }
    return out;
}

// PNG: concatenated IDAT payload bytes — the PNG equivalent of the JPEG "SOS tail" criterion.
std::string png_idat(const fs::path& p) {
    const std::vector<uint8_t> b = read_bytes(p);
    static const unsigned char kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() < 8 || std::memcmp(b.data(), kSig, 8) != 0) return {};
    const auto be32 = [&b](std::size_t o) {
        return (static_cast<uint32_t>(b[o]) << 24) | (static_cast<uint32_t>(b[o + 1]) << 16) |
               (static_cast<uint32_t>(b[o + 2]) << 8) | static_cast<uint32_t>(b[o + 3]);
    };
    std::string out;
    std::size_t pos = 8;
    while (pos + 12 <= b.size()) {
        const uint32_t len = be32(pos);
        const std::string type(reinterpret_cast<const char*>(b.data() + pos + 4), 4);
        const std::size_t end = std::min(b.size(), pos + 12 + static_cast<std::size_t>(len));
        if (type == "IDAT") out.append(reinterpret_cast<const char*>(b.data() + pos + 8), end - (pos + 8));
        pos = end;
    }
    return out;
}

void test_metadata_only(const fs::path& tmp) {
    const fs::path src = corpus() / "meta" / "exif_full.jpg";
    const fs::path out = tmp / "metadata_only.jpg";
    const pp::SourceMeta meta = pp::read_metadata(src);
    pp::BatchRules rules;
    rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("M1-T5"), false});
    const pp::MetadataPlan plan = pp::build_plan(meta, rules, std::nullopt);
    const std::string err = pp::rewrite_metadata_only(src, out, plan, pp::make_payloads(plan));
    check(err.empty(), "meta-only/rewrite", err);

    // (1) every byte from the SOS marker onwards must be identical (M0 Spike F method)
    const std::vector<uint8_t> before = read_bytes(src);
    const std::vector<uint8_t> after = read_bytes(out);
    const std::size_t s1 = find_sos(before);
    const std::size_t s2 = find_sos(after);
    bool tail_ok = s1 != std::string::npos && s2 != std::string::npos &&
                   before.size() - s1 == after.size() - s2;
    if (tail_ok) {
        tail_ok = std::memcmp(before.data() + s1, after.data() + s2, before.size() - s1) == 0;
    }
    check(tail_ok, "meta-only/sos-tail-identical",
          "sos=" + std::to_string(s1) + "/" + std::to_string(s2) +
              " sizes=" + std::to_string(before.size()) + "/" + std::to_string(after.size()));
    // (2) decoded pixels must hash equally
    const std::string h0 = pixel_hash(src);
    check(!h0.empty() && h0 == pixel_hash(out), "meta-only/pixel-hash-equal", "pixel hash changed");
    // (3) the edited tag landed, the rest survived
    const pp::SourceMeta back = pp::read_metadata(out);
    check(exif_str(back.exif, "Exif.Image.Artist") == "M1-T5", "meta-only/artist-written",
          show(exif_str(back.exif, "Exif.Image.Artist")));
    check(exif_str(back.exif, "Exif.Photo.DateTimeOriginal") == "2024:03:01 10:00:00",
          "meta-only/time-preserved", show(exif_str(back.exif, "Exif.Photo.DateTimeOriginal")));
    check(back.has_gps, "meta-only/gps-preserved", "GPS was lost");
}

// Same rewrite path for the other containers Exiv2 can write now (PNG/WebP/TIFF): the coded
// payload must survive byte for byte (PNG IDAT / WebP VP8L chunk level; TIFF is checked by pixel
// hash because Exiv2 relocates the strips when the IFD grows).
void test_metadata_only_other_containers(const fs::path& tmp) {
    {
        const fs::path src = corpus() / "base" / "rgb8.png";
        const fs::path out = tmp / "metadata_only.png";
        const pp::SourceMeta meta = pp::read_metadata(src);
        if (!meta.error.empty()) {
            fail("meta-only/png/read", meta.error);
        } else {
            pp::BatchRules rules;
            rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("M1-T5"), false});
            rules.xmp_edits.push_back(pp::TagEdit{"Xmp.dc.title", std::string("png meta-only"), false});
            const pp::MetadataPlan plan = pp::build_plan(meta, rules, std::nullopt);
            const std::string err = pp::rewrite_metadata_only(src, out, plan, pp::make_payloads(plan));
            check(err.empty(), "meta-only/png/rewrite", err);
            const std::string idat_src = png_idat(src);
            const std::string idat_out = png_idat(out);
            check(!idat_src.empty() && idat_src == idat_out, "meta-only/png/idat-identical",
                  "IDAT payload differs (src=" + std::to_string(idat_src.size()) +
                      " out=" + std::to_string(idat_out.size()) + " bytes)");
            check(pixel_hash(src) == pixel_hash(out), "meta-only/png/pixel-hash", "pixels changed");
            const pp::SourceMeta back = pp::read_metadata(out);
            check(exif_str(back.exif, "Exif.Image.Artist") == "M1-T5", "meta-only/png/artist-written",
                  show(exif_str(back.exif, "Exif.Image.Artist")));
            check(xmp_str(back.xmp, "Xmp.dc.title").find("png meta-only") != std::string::npos,
                  "meta-only/png/xmp-written", show(xmp_str(back.xmp, "Xmp.dc.title")));
            std::printf("info meta-only png: idat_src=%zu idat_out=%zu idat_equal=%d pixel_hash_equal=%d\n",
                        idat_src.size(), idat_out.size(), (int)(idat_src == idat_out),
                        (int)(pixel_hash(src) == pixel_hash(out)));
        }
    }
    {
        const fs::path src = corpus() / "meta" / "webp_lossless.webp";
        const fs::path out = tmp / "metadata_only.webp";
        const pp::SourceMeta meta = pp::read_metadata(src);
        if (!meta.error.empty()) {
            fail("meta-only/webp/read", meta.error);
        } else {
            pp::BatchRules rules;
            rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("M1-T5"), false});
            const pp::MetadataPlan plan = pp::build_plan(meta, rules, std::nullopt);
            const std::string err = pp::rewrite_metadata_only(src, out, plan, pp::make_payloads(plan));
            check(err.empty(), "meta-only/webp/rewrite", err);
            check(pixel_hash(src) == pixel_hash(out), "meta-only/webp/pixel-hash", "pixels changed");
            check(!riff_payload(src).empty() && riff_payload(src) == riff_payload(out),
                  "meta-only/webp/payload-identical", "RIFF payload chunks changed");
            check(exif_str(pp::read_metadata(out).exif, "Exif.Image.Artist") == "M1-T5",
                  "meta-only/webp/artist-written", "");
        }
    }
    {
        const fs::path src = corpus() / "base" / "rgb16.tif";
        const fs::path out = tmp / "metadata_only.tif";
        const pp::SourceMeta meta = pp::read_metadata(src);
        if (!meta.error.empty()) {
            fail("meta-only/tif/read", meta.error);
        } else {
            pp::BatchRules rules;
            rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("M1-T5"), false});
            const pp::MetadataPlan plan = pp::build_plan(meta, rules, std::nullopt);
            const std::string err = pp::rewrite_metadata_only(src, out, plan, pp::make_payloads(plan));
            check(err.empty(), "meta-only/tif/rewrite", err);
            check(pixel_hash(src) == pixel_hash(out), "meta-only/tif/pixel-hash", "pixels changed");
            check(exif_str(pp::read_metadata(out).exif, "Exif.Image.Artist") == "M1-T5",
                  "meta-only/tif/artist-written", "");
        }
    }
}

void test_png_r1(const fs::path& tmp) {
    // R1 (closed by M1-T5b: the exiv2 port now builds with its "png" feature → zlib).
    // PNG uses the same post-encode Exiv2 path as JPEG/TIFF/WebP; the mirror/drop fallback in
    // write_metadata_exiv2() stays as a safety net for a build without PNG support.
    const fs::path src = corpus() / "base" / "rgb8.png";
    const fs::path out = tmp / "r1.png";
    check(copy_fixture(src, out), "r1/png/copy", out.string());
    const std::string h0 = pixel_hash(out);

    const pp::SourceMeta meta = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");
    pp::BatchRules rules;
    rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("M1-T5"), false});
    rules.xmp_edits.push_back(pp::TagEdit{"Xmp.dc.title", std::string("png r1"), false});
    pp::MetadataPlan plan = pp::build_plan(meta, rules, std::nullopt);
    const std::string err = pp::write_metadata_exiv2(out, plan, pp::make_payloads(plan));

    check(err.empty(), "r1/png/non-fatal", "metadata loss must not fail the file: " + err);
    check(pixel_hash(out) == h0, "r1/png/pixels-intact", "pixel hash changed");

    const bool writable = exiv2_supports(out);
    if (writable) {
        // success path: EXIF *and* XMP land, and no downgrade warning may be produced
        const pp::SourceMeta back = pp::read_metadata(out);
        check(back.error.empty(), "r1/png/readback", back.error);
        check(exif_str(back.exif, "Exif.Image.Artist") == "M1-T5", "r1/png/exif-artist",
              show(exif_str(back.exif, "Exif.Image.Artist")));
        check(exif_str(back.exif, "Exif.Photo.DateTimeOriginal") == "2024:03:01 10:00:00",
              "r1/png/exif-time", show(exif_str(back.exif, "Exif.Photo.DateTimeOriginal")));
        check(back.has_gps &&
                  std::fabs(dms_to_degrees(back.exif, "Exif.GPSInfo.GPSLatitude") - 31.2304) < 1e-6,
              "r1/png/exif-gps", num(dms_to_degrees(back.exif, "Exif.GPSInfo.GPSLatitude")));
        check(xmp_str(back.xmp, "Xmp.dc.title").find("png r1") != std::string::npos,
              "r1/png/xmp-title", show(xmp_str(back.xmp, "Xmp.dc.title")));
        check(!has_warning(plan, pp::WarningKind::MetadataDropped), "r1/png/no-drop-warning",
              show(warning_detail(plan, pp::WarningKind::MetadataDropped)));
        std::printf("info R1 png exif: writable=1 artist=%s time=%s gps=%s xmp_title=%s warnings=%zu\n",
                    show(exif_str(back.exif, "Exif.Image.Artist")).c_str(),
                    show(exif_str(back.exif, "Exif.Photo.DateTimeOriginal")).c_str(),
                    num(dms_to_degrees(back.exif, "Exif.GPSInfo.GPSLatitude")).c_str(),
                    show(xmp_str(back.xmp, "Xmp.dc.title")).c_str(), plan.warnings.size());
    } else {
        // fallback path — only reachable when exiv2 has no png support (EXV_HAVE_LIBZ undefined)
        check(has_warning(plan, pp::WarningKind::MetadataDropped), "r1/png/dropped-warning",
              "expected Warning{MetadataDropped} for a PNG output");
        check(warning_detail(plan, pp::WarningKind::MetadataDropped).find("png") != std::string::npos,
              "r1/png/warning-detail",
              show(warning_detail(plan, pp::WarningKind::MetadataDropped)));
        std::printf("info R1 png exif: writable=0 dropped_detail=%s\n",
                    show(warning_detail(plan, pp::WarningKind::MetadataDropped)).c_str());
    }
}

// The metadata-only capability must now include PNG; the probe is a runtime check so a future
// exiv2 build without a format handler degrades automatically (T8 delegates to it).
void test_metadata_only_capability() {
    check(pp::detail_metadata_only_supported("jpeg"), "capability/jpeg", "expected true");
    check(pp::detail_metadata_only_supported("png"), "capability/png", "expected true (R1 closed)");
    check(pp::detail_metadata_only_supported("tiff"), "capability/tiff", "expected true");
    check(pp::detail_metadata_only_supported("webp"), "capability/webp", "expected true");
    check(!pp::detail_metadata_only_supported("heif"), "capability/heif", "expected false");
    check(!pp::detail_metadata_only_supported("avif"), "capability/avif", "expected false");
    check(!pp::detail_metadata_only_supported("jxl"), "capability/jxl", "expected false");
    check(!pp::detail_metadata_only_supported("bmp"), "capability/bmp", "expected false");
    check(!pp::detail_metadata_only_supported("nonsense"), "capability/unknown", "expected false");
    std::printf("info metadata-only capability: jpeg=%d png=%d tiff=%d webp=%d heif=%d avif=%d jxl=%d "
                "bmp=%d\n",
                (int)pp::detail_metadata_only_supported("jpeg"),
                (int)pp::detail_metadata_only_supported("png"),
                (int)pp::detail_metadata_only_supported("tiff"),
                (int)pp::detail_metadata_only_supported("webp"),
                (int)pp::detail_metadata_only_supported("heif"),
                (int)pp::detail_metadata_only_supported("avif"),
                (int)pp::detail_metadata_only_supported("jxl"),
                (int)pp::detail_metadata_only_supported("bmp"));

    // frozen predicate (§3.9): must agree for the metadata-only formats
    check(pp::format_supports_metadata_only("png"), "capability/frozen-api/png", "expected true");
    check(pp::format_supports_metadata_only("jpeg") && pp::format_supports_metadata_only("tiff") &&
              pp::format_supports_metadata_only("webp"),
          "capability/frozen-api/others", "expected true for jpeg/tiff/webp");
    check(!pp::format_supports_metadata_only("heif") && !pp::format_supports_metadata_only("avif") &&
              !pp::format_supports_metadata_only("jxl") && !pp::format_supports_metadata_only("bmp"),
          "capability/frozen-api/unsupported", "expected false for heif/avif/jxl/bmp");
}

void test_mirror_helper() {
    Exiv2::ExifData exif;
    exif["Exif.Photo.DateTimeOriginal"] = "2024:03:01 10:00:00";
    exif["Exif.Image.Artist"] = "Zhang";
    exif["Exif.GPSInfo.GPSLatitudeRef"] = "N";
    exif["Exif.GPSInfo.GPSLatitude"] = "31/1 13/1 4944/100";
    Exiv2::XmpData xmp;
    const std::size_t n = pp::detail_mirror_key_exif_to_xmp(exif, xmp);
    check(n == 3, "mirror/count", std::to_string(n));
    check(xmp_str(xmp, "Xmp.exif.DateTimeOriginal") == "2024-03-01T10:00:00", "mirror/date-iso",
          show(xmp_str(xmp, "Xmp.exif.DateTimeOriginal")));
    check(xmp_str(xmp, "Xmp.tiff.Artist") == "Zhang", "mirror/artist",
          show(xmp_str(xmp, "Xmp.tiff.Artist")));
    check(xmp_str(xmp, "Xmp.exif.GPSLatitude") == "31,13.824N", "mirror/gps-decimal-minutes",
          show(xmp_str(xmp, "Xmp.exif.GPSLatitude")));

    std::string packet;
    check(Exiv2::XmpParser::encode(packet, xmp) == 0 && !packet.empty(), "mirror/encode", "");
    Exiv2::XmpData back;
    check(Exiv2::XmpParser::decode(back, packet) == 0 &&
              xmp_str(back, "Xmp.exif.GPSLatitude") == "31,13.824N",
          "mirror/encode-decode", show(xmp_str(back, "Xmp.exif.GPSLatitude")));
}

void test_post_write_formats(const fs::path& tmp) {
    const pp::SourceMeta meta = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");
    pp::MetadataPlan plan = pp::build_plan(meta, pp::BatchRules{}, std::nullopt);  // mutable: see push_plan_warning()
    const pp::Payloads pl = pp::make_payloads(plan);

    struct Case {
        const char* fixture;
        const char* name;
    };
    const Case cases[] = {{"base/rgb16.tif", "out.tif"}, {"meta/webp_lossless.webp", "out.webp"}};
    for (const Case& c : cases) {
        const fs::path in = corpus() / c.fixture;
        const fs::path out = tmp / c.name;
        if (!copy_fixture(in, out)) {
            fail(std::string("postwrite/") + c.name + "/copy", in.string());
            continue;
        }
        const std::string h0 = pixel_hash(out);
        const std::string err = pp::write_metadata_exiv2(out, plan, pl);
        check(err.empty(), std::string("postwrite/") + c.name + "/no-error", err);
        check(pixel_hash(out) == h0 && !h0.empty(),
              std::string("postwrite/") + c.name + "/pixels-intact", "pixel hash changed");
        const pp::SourceMeta back = pp::read_metadata(out);
        check(exif_str(back.exif, "Exif.Image.Artist") == "M0" &&
                  exif_str(back.exif, "Exif.Photo.DateTimeOriginal") == "2024:03:01 10:00:00" &&
                  back.has_gps,
              std::string("postwrite/") + c.name + "/metadata-landed",
              show(exif_str(back.exif, "Exif.Image.Artist")));
    }

    // BMP: no metadata container — must be a silent success (§5.2/§5.8)
    const fs::path bmp_in = corpus() / "base" / "bmp24.bmp";
    const fs::path bmp_out = tmp / "out.bmp";
    if (copy_fixture(bmp_in, bmp_out)) {
        const std::string h0 = pixel_hash(bmp_out);
        const std::string err = pp::write_metadata_exiv2(bmp_out, plan, pl);
        check(err.empty(), "postwrite/bmp/no-error", err);
        check(pixel_hash(bmp_out) == h0, "postwrite/bmp/pixels-intact", "pixel hash changed");
    } else {
        fail("postwrite/bmp/copy", bmp_in.string());
    }
}

// M2-T4 (#6): the additive warnings out-parameter of write_metadata_exiv2().
// Contract: the explicit channel carries byte-identical detail strings to the legacy
// plan.warnings push (so the pipeline's merge-dedup collapses the pair), it stays silent on the
// success path, and the 3-argument M1 form still compiles/behaves unchanged (used by
// test_post_write_formats/test_png_r1 above).
void test_write_warnings_out_param(const fs::path& tmp) {
    const pp::SourceMeta meta = pp::read_metadata(corpus() / "meta" / "exif_full.jpg");
    pp::BatchRules rules;
    rules.exif_edits.push_back(pp::TagEdit{"Exif.Image.Artist", std::string("M2-T4"), false});
    pp::MetadataPlan plan = pp::build_plan(meta, rules, std::nullopt);  // mutable: plan.warnings channel
    const pp::Payloads pl = pp::make_payloads(plan);

    // Failure case: the post-encode Exiv2 write cannot open its container → metadata loss stays
    // non-fatal, but the message must arrive through the explicit out-parameter *and* the legacy
    // plan.warnings channel.
    const fs::path missing = tmp / "no-such-dir" / "out.jpg";
    std::vector<std::string> got;
    const std::string err = pp::write_metadata_exiv2(missing, plan, pl, &got);
    check(err.empty(), "write-warnings/non-fatal", "metadata loss must stay non-fatal: " + err);
    check(got.size() == 1, "write-warnings/out-param-count",
          "expected exactly one writer message, got " + std::to_string(got.size()));
    check(plan.warnings.size() == got.size(), "write-warnings/frozen-plan-channel",
          "plan.warnings production changed: size " + std::to_string(plan.warnings.size()) +
              " vs out-param " + std::to_string(got.size()));
    if (got.empty() || plan.warnings.empty()) {
        fail("write-warnings/detail", "no writer message produced");
    } else {
        check(got.front() == plan.warnings.front().detail, "write-warnings/identical-detail",
              show(got.front()) + " vs " + show(plan.warnings.front().detail));
        check(plan.warnings.front().kind == pp::WarningKind::MetadataDropped,
              "write-warnings/kind", "expected MetadataDropped");
        check(got.front().find("metadata dropped") != std::string::npos, "write-warnings/detail",
              show(got.front()));
    }

    // Success path: the out-parameter stays empty (no invented warnings).
    const fs::path ok_out = tmp / "write-warnings-ok.jpg";
    if (!copy_fixture(corpus() / "base" / "photo.jpg", ok_out)) {
        fail("write-warnings/copy", ok_out.string());
        return;
    }
    pp::MetadataPlan plan2 = pp::build_plan(pp::read_metadata(ok_out), rules, std::nullopt);
    std::vector<std::string> got2;
    const std::string err2 =
        pp::write_metadata_exiv2(ok_out, plan2, pp::make_payloads(plan2), &got2);
    check(err2.empty(), "write-warnings/success-no-error", err2);
    check(got2.empty(), "write-warnings/success-silent",
          "unexpected writer message: " + (got2.empty() ? std::string() : show(got2.front())));

    std::printf("info write-warnings: failure_detail=%s warnings=%zu\n",
                (got.empty() ? std::string("<none>") : show(got.front())).c_str(), plan.warnings.size());
}

// §4.5 / consensus §3.6: MakerNote bytes are carried across containers verbatim, never parsed or
// edited, and the layer only reports "Makernote present, N bytes" in the log.
void test_makernote(const fs::path& tmp) {
    const fs::path src = corpus() / "meta" / "exif_full.jpg";
    const fs::path out = tmp / "makernote.jpg";
    pp::SourceMeta meta = pp::read_metadata(src);
    const std::string bytes("\x4d\x4d\x00\x2a\x00\x07\xff", 7);
    Exiv2::Value::UniquePtr v = Exiv2::Value::create(Exiv2::undefined);
    v->read(reinterpret_cast<const Exiv2::byte*>(bytes.data()), bytes.size(), Exiv2::invalidByteOrder);
    meta.exif.add(Exiv2::ExifKey("Exif.Photo.MakerNote"), v.get());

    const fs::path logs = tmp / "logs";
    pptest::unsetenv("PP_LOG_LEVEL");  // the test asserts an info line
    pp::log_init(logs, pp::LogLevel::Info);
    const pp::MetadataPlan plan = pp::build_plan(meta, pp::BatchRules{}, std::nullopt);
    const std::string err = pp::rewrite_metadata_only(src, out, plan, pp::make_payloads(plan));
    pp::log_shutdown();
    check(err.empty(), "makernote/rewrite", err);

    // bytes survived the container rewrite untouched
    const pp::SourceMeta back = pp::read_metadata(out);
    auto it = back.exif.findKey(Exiv2::ExifKey("Exif.Photo.MakerNote"));
    bool same = false;
    if (it != back.exif.end() && it->size() == bytes.size()) {
        same = true;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (it->value().toInt64(i) != static_cast<unsigned char>(bytes[i])) same = false;
        }
    }
    check(same, "makernote/bytes-preserved",
          it == back.exif.end() ? "tag missing" : "size=" + std::to_string(it->size()));

    // the info line was logged
    bool logged = false;
    std::error_code ec;
    for (const fs::directory_iterator::value_type& e : fs::directory_iterator(logs, ec)) {
        if (!e.is_regular_file()) continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (text.find("Makernote present, 7 bytes") != std::string::npos) logged = true;
    }
    check(logged, "makernote/logged", "no 'Makernote present, 7 bytes' line in " + logs.string());
}

void test_mtime(const fs::path& tmp) {
    const fs::path f = tmp / "mtime.bin";
    {
        std::ofstream out(f, std::ios::binary);
        out << "mtime test\n";
    }
    const auto t0 = fs::last_write_time(f);
    check(pp::sync_file_mtime(f, "").empty(), "mtime/empty-is-noop-error", "");
    check(fs::last_write_time(f) == t0, "mtime/empty-is-noop", "mtime changed for an empty value");

    const std::string err = pp::sync_file_mtime(f, "2024:03:01 10:00:00");
    check(err.empty(), "mtime/set", err);
    std::tm tm{};
    tm.tm_year = 2024 - 1900;
    tm.tm_mon = 3 - 1;
    tm.tm_mday = 1;
    tm.tm_hour = 10;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const std::time_t want = std::mktime(&tm);
#if defined(_WIN32)
    // M3：MSVC file_clock 仅提供 utc 对（LWG 3694）；经 utc 中转，与 POSIX 分支同瞬点。
    const auto want_ft = std::chrono::file_clock::from_utc(
        std::chrono::utc_clock::from_sys(std::chrono::system_clock::from_time_t(want)));
#else
    const auto want_ft =
        fs::file_time_type::clock::from_sys(std::chrono::system_clock::from_time_t(want));
#endif
    check(fs::last_write_time(f) == want_ft, "mtime/value", "last_write_time differs from EXIF time");

    check(!pp::sync_file_mtime(f, "2024-03-01 10:00:00").empty(), "mtime/invalid-format", "");
    check(!pp::sync_file_mtime(f, "2023:02:29 10:00:00").empty(), "mtime/invalid-date", "");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // keep partial output if a library aborts
    const fs::path root = repo_root();
    if (root.empty()) {
        std::printf("FAIL setup: repository root (tests/golden) not found from %s\n",
                    fs::current_path().string().c_str());
        return 1;
    }
    const fs::path tmp = tmp_dir("scratch");
    std::printf("info scratch dir: %s\n", tmp.string().c_str());

    test_read_metadata(tmp);
    test_gps();
    test_privacy(tmp);
    test_edits();
    test_build_plan();
    test_payloads();
    test_effective_datetime();
    test_metadata_only(tmp);
    test_metadata_only_other_containers(tmp);
    test_png_r1(tmp);
    test_metadata_only_capability();
    test_mirror_helper();
    test_post_write_formats(tmp);
    test_write_warnings_out_param(tmp);
    test_makernote(tmp);
    test_mtime(tmp);

    if (g_failed == 0) {
        std::printf("OK test_metadata: all cases passed\n");
    } else {
        std::printf("FAILED test_metadata: %d case(s)\n", g_failed);
    }
    return g_failed;
}
