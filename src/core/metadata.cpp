// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T5 — metadata layer implementation.
//
// Contract: docs/m1-tasks.md §3.7 (PP-FROZEN header) / §4.5, semantics §5.2–§5.6, facts §7.
//
// Environment facts verified with a throw-away probe (recorded in the M1-T5 report):
//   * ExifParser::encode(blob, littleEndian, exif) produces a TIFF blob that starts with
//     "II*\0" (49 49 2a 00); ExifParser::encode of an empty ExifData produces size 0.
//   * Exiv2 XMP keys are dotted ("Xmp.xmp.CreateDate"); the property notation used by the
//     task book ("Xmp.xmp:CreateDate") is normalized here, see normalize_xmp_key().
//   * This exiv2 build has no zlib (EXV_HAVE_LIBZ undefined) → PNG is not a registered
//     image type: ImageFactory::getType("*.png") == none and open() throws
//     "unknown image type". write_metadata_exiv2() therefore degrades PNG to a
//     MetadataDropped warning (R1); the mirror-to-XMP fallback is implemented and unit
//     tested through detail_mirror_key_exif_to_xmp().
//   * JPEG/TIFF/WebP support full EXIF+XMP read/write; BMFF (HEIF/AVIF/JXL) is read-only
//     ("Setting Exif metadata in BMFF images is not supported") — hence write path B/C.

#include "core/metadata.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/logger.h"
#include "core/pipeline.h"  // frozen declaration of format_supports_metadata_only()

namespace pp {
namespace {

// ---------------------------------------------------------------------------
// XMP runtime (§4.5: initialize once + terminate in a static destructor)
// ---------------------------------------------------------------------------

struct XmpTeardown {
    ~XmpTeardown() { Exiv2::XmpParser::terminate(); }
};

void ensure_xmp() {
    static std::once_flag once;
    std::call_once(once, [] { Exiv2::XmpParser::initialize(); });
    static const XmpTeardown teardown;  // runs after main(); constructed once
    (void)teardown;
}

// ---------------------------------------------------------------------------
// small string helpers
// ---------------------------------------------------------------------------

std::string lower_copy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// Trailing NULs are part of EXIF ASCII values (count includes the terminator) but never of
// the logical value.
std::string strip_nul(std::string s) {
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// Exiv2 XMP keys are "Xmp.<prefix>.<local>"; the task book also uses the XMP property
// notation "Xmp.<prefix>:<local>" (e.g. "Xmp.xmp:CreateDate"). Accept both.
std::string normalize_xmp_key(std::string_view key) {
    if (key.rfind("Xmp.", 0) != 0) return std::string(key);
    const std::size_t colon = key.find(':', 4);
    if (colon == std::string_view::npos) return std::string(key);
    std::string out(key);
    out[colon] = '.';
    return out;
}

// ---------------------------------------------------------------------------
// civil calendar (proleptic Gregorian, no timezone) — Howard Hinnant's algorithms
// ---------------------------------------------------------------------------

constexpr bool is_leap_year(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int days_in_month(int y, int m) {
    static constexpr int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap_year(y)) return 29;
    return kDays[m - 1];
}

int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

int64_t floor_mod(int64_t a, int64_t b) { return a - floor_div(a, b) * b; }

int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= static_cast<int>(m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civil_from_days(int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int yy = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned dd = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned mm = (mp < 10u) ? mp + 3u : mp - 9u;
    y = yy + static_cast<int>(mm <= 2);
    m = static_cast<int>(mm);
    d = static_cast<int>(dd);
}

struct DateTime {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

bool parse_digits(std::string_view s, std::size_t pos, std::size_t len, int& out) {
    int v = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const char c = s[pos + i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

// Strict EXIF 2.3 form "YYYY:MM:DD HH:MM:SS" (19 chars, no sub-second part).
bool parse_exif_datetime(std::string_view s, DateTime& out) {
    if (s.size() != 19) return false;
    if (s[4] != ':' || s[7] != ':' || s[10] != ' ' || s[13] != ':' || s[16] != ':') return false;
    DateTime t;
    if (!parse_digits(s, 0, 4, t.year) || !parse_digits(s, 5, 2, t.month) || !parse_digits(s, 8, 2, t.day) ||
        !parse_digits(s, 11, 2, t.hour) || !parse_digits(s, 14, 2, t.minute) ||
        !parse_digits(s, 17, 2, t.second)) {
        return false;
    }
    if (t.year < 1 || t.year > 9999) return false;
    if (t.month < 1 || t.month > 12) return false;
    if (t.day < 1 || t.day > days_in_month(t.year, t.month)) return false;
    if (t.hour > 23 || t.minute > 59 || t.second > 60) return false;  // 60 = leap second, normalized below
    out = t;
    return true;
}

std::string format_exif_datetime(const DateTime& t) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d:%02d:%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour,
                  t.minute, t.second);
    return std::string(buf);
}

std::string format_iso_base(const DateTime& t) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour,
                  t.minute, t.second);
    return std::string(buf);
}

// Delta arithmetic: years/months are calendar steps (day-of-month clamps to the target month),
// days/hours/minutes/seconds are exact duration steps applied afterwards (§4.5).
bool apply_delta(const DateTime& in, const TimeShift& s, DateTime& out) {
    int y = in.year + s.years;
    int64_t m = static_cast<int64_t>(in.month) + s.months;
    y += static_cast<int>(floor_div(m - 1, 12));
    m = floor_mod(m - 1, 12) + 1;
    if (y < 1 || y > 9999) return false;
    int day = std::min(in.day, days_in_month(y, static_cast<int>(m)));

    int64_t days = days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(day)) + s.days;
    int64_t secs = static_cast<int64_t>(in.hour) * 3600 + static_cast<int64_t>(in.minute) * 60 + in.second +
                   static_cast<int64_t>(s.hours) * 3600 + static_cast<int64_t>(s.minutes) * 60 + s.seconds;
    const int64_t carry = floor_div(secs, 86400);
    secs -= carry * 86400;
    days += carry;

    int ny = 0, nm = 0, nd = 0;
    civil_from_days(days, ny, nm, nd);
    if (ny < 1 || ny > 9999) return false;
    out = DateTime{ny, nm, nd, static_cast<int>(secs / 3600), static_cast<int>((secs % 3600) / 60),
                   static_cast<int>(secs % 60)};
    return true;
}

// ---------------------------------------------------------------------------
// XMP ISO-8601 date handling ("2024-03-01T10:00:00.123+08:00")
// ---------------------------------------------------------------------------

struct XmpDate {
    DateTime dt;
    std::string fraction;    // ".123" or ""
    bool has_offset = false;
    std::string offset;      // "Z" / "+08:00" (only when has_offset)
};

bool parse_iso_offset(std::string_view tail, bool& has, std::string& text) {
    has = false;
    text.clear();
    if (tail.empty()) return true;
    if (tail.back() == 'Z' || tail.back() == 'z') {
        has = true;
        text = "Z";
        return true;
    }
    if (tail.size() >= 6) {
        const std::size_t p = tail.size() - 6;
        const char sign = tail[p];
        int hh = 0, mm = 0;
        if ((sign == '+' || sign == '-') && tail[p + 3] == ':' && parse_digits(tail, p + 1, 2, hh) &&
            parse_digits(tail, p + 4, 2, mm) && hh <= 23 && mm <= 59) {
            has = true;
            text = std::string(tail.substr(p));
            return true;
        }
    }
    return true;  // no offset recorded (e.g. only fractional seconds)
}

bool parse_xmp_date(std::string_view v, XmpDate& out) {
    if (v.size() < 19) return false;
    std::string head(v.substr(0, 19));
    if (head[4] != '-' || head[7] != '-' || head[10] != 'T' || head[13] != ':' || head[16] != ':') {
        // tolerate the space separator some writers emit
        if (head[10] != ' ') return false;
    }
    for (const std::size_t p : {4u, 7u, 10u, 13u, 16u}) head[p] = (p == 10 ? ' ' : ':');
    DateTime dt;
    if (!parse_exif_datetime(head, dt)) return false;

    std::string_view tail = v.substr(19);
    std::string offset;
    bool has_offset = false;
    if (!parse_iso_offset(tail, has_offset, offset)) return false;
    std::string fraction;
    if (!tail.empty() && tail.front() == '.') {
        const std::size_t end = has_offset ? tail.size() - offset.size() : tail.size();
        fraction = std::string(tail.substr(0, end));
    }
    out.dt = dt;
    out.fraction = fraction;
    out.has_offset = has_offset;
    out.offset = offset;
    return true;
}

int offset_minutes_of(std::string_view offset) {
    if (offset.empty() || offset == "Z" || offset == "z") return 0;
    const char sign = offset.front();
    int hh = 0, mm = 0;
    if (offset.size() < 6 || !parse_digits(offset, 1, 2, hh) || !parse_digits(offset, 4, 2, mm)) return 0;
    const int v = hh * 60 + mm;
    return sign == '-' ? -v : v;
}

// ---------------------------------------------------------------------------
// EXIF key helpers
// ---------------------------------------------------------------------------

const char* const kExifTimeKeys[3] = {"Exif.Photo.DateTimeOriginal", "Exif.Photo.DateTimeDigitized",
                                      "Exif.Image.DateTime"};
const char* const kXmpTimeKeys[2] = {"Xmp.xmp.CreateDate", "Xmp.xmp.ModifyDate"};
// R1: fields mirrored into XMP when a container cannot carry EXIF.
const char* const kMirrorKeys[3] = {"Exif.Photo.DateTimeOriginal", "Exif.Image.Artist",
                                    "Exif.GPSInfo.GPSLatitude"};

void erase_exif_key(Exiv2::ExifData& exif, const char* key) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it != exif.end()) exif.erase(it);
}

std::string urational(int64_t num, int64_t den) {
    if (den <= 0) den = 1;
    if (num < 0) num = 0;  // callers pass magnitudes; hemisphere is carried by *Ref
    return std::to_string(num) + "/" + std::to_string(den);
}

// 3 × unsigned rational (deg / min / sec), magnitude only; hemisphere goes into *Ref (G7).
std::array<std::string, 3> dms_rationals(double degrees) {
    const double mag = std::isfinite(degrees) ? std::fabs(degrees) : 0.0;
    const int64_t micro = std::llround(mag * 3600.0 * 1e6);  // micro arc-seconds
    const int64_t d = micro / 3600000000LL;
    int64_t rem = micro % 3600000000LL;
    const int64_t m = rem / 60000000LL;
    rem %= 60000000LL;
    return {urational(d, 1), urational(m, 1), urational(rem, 1000000)};
}

std::string join_dms(const std::array<std::string, 3>& v) { return v[0] + " " + v[1] + " " + v[2]; }

// MakerNote policy (§4.5 / consensus §3.6): the bytes are carried across containers untouched,
// never parsed and never edited — Exiv2 copies the Exif.Photo.MakerNote value verbatim on
// setExifData()/writeMetadata(); all this layer does is report the size.
void log_makernote(const Exiv2::ExifData& exif, std::string_view stage) {
    auto it = exif.findKey(Exiv2::ExifKey("Exif.Photo.MakerNote"));
    if (it == exif.end()) return;
    const std::string msg = "Makernote present, " + std::to_string(it->value().size()) + " bytes";
    log_info(stage, "metadata.cpp", msg.c_str());
}

// FROZEN-SIGNATURE WORKAROUND: write_metadata_exiv2() receives a `const MetadataPlan&` yet must
// report MetadataDropped/PngExifDropped to its caller (T8 copies plan.warnings into
// FileResult.warnings). Every call site passes a mutable plan object (the build_plan() result
// stored in a local, or a temporary) — never a declared-const object — so writing through the
// cast is well defined there. Documented in the M1-T5 report as `next-needed` for T8.
// M2-T4 RESOLUTION: write_metadata_exiv2() now also takes an explicit `out_warnings`
// out-parameter (see note_write_warning below), which is the supported channel for callers.
// The plan.warnings push is kept verbatim as the backward-compatibility channel — M2-T4 freezes
// its production and content ("new channel adds, never removes"), so both stay in lockstep.
void push_plan_warning(const MetadataPlan& plan, WarningKind kind, std::string detail) {
    auto& mutable_plan = const_cast<MetadataPlan&>(plan);
    mutable_plan.warnings.push_back(Warning{kind, std::move(detail)});
}

// M2-T4 (#6): report a write-path failure/degradation through *both* channels with a
// byte-identical detail string, so the pipeline can merge them and drop the duplicate.
void note_write_warning(const MetadataPlan& plan, std::vector<std::string>* out_warnings,
                        WarningKind kind, const std::string& detail) {
    if (out_warnings != nullptr) out_warnings->push_back(detail);
    push_plan_warning(plan, kind, detail);
}

// Exiv2 read/modify/write in one place; empty containers => remove that metadata section.
std::string exiv2_apply(const std::filesystem::path& file, const Exiv2::ExifData* exif,
                        const Exiv2::XmpData* xmp) {
    if (exif != nullptr) log_makernote(*exif, "MetaWrite");
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(file.string());
        if (!img) return "exiv2: cannot open " + file.string();
        img->readMetadata();
        if (exif == nullptr || exif->empty()) {
            img->clearExifData();
        } else {
            img->setExifData(*exif);
        }
        if (xmp == nullptr || xmp->empty()) {
            img->clearXmpData();
        } else {
            img->setXmpData(*xmp);
        }
        img->writeMetadata();
    } catch (const Exiv2::Error& e) {
        return std::string("exiv2: ") + e.what();
    } catch (const std::exception& e) {
        return std::string("exiv2: ") + e.what();
    } catch (...) {
        return "exiv2: unknown error while writing " + file.string();
    }
    return {};
}

// R1 verification helper: are the mirror-worthy EXIF fields readable back from `file`?
bool key_exif_landed(const std::filesystem::path& file, const Exiv2::ExifData& want) {
    std::vector<std::string> expect;
    for (const char* k : kMirrorKeys) {
        if (want.findKey(Exiv2::ExifKey(k)) != want.end()) expect.emplace_back(k);
    }
    if (expect.empty()) return true;
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(file.string());
        if (!img) return false;
        img->readMetadata();
        const Exiv2::ExifData& got = img->exifData();
        for (const std::string& k : expect) {
            auto it = got.findKey(Exiv2::ExifKey(k));
            if (it == got.end() || strip_nul(it->toString()).empty()) return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool has_any_time_field(const Exiv2::ExifData& exif, const Exiv2::XmpData& xmp) {
    for (const char* k : kExifTimeKeys) {
        if (exif.findKey(Exiv2::ExifKey(k)) != exif.end()) return true;
    }
    for (const char* k : kXmpTimeKeys) {
        if (xmp.findKey(Exiv2::XmpKey(normalize_xmp_key(k))) != xmp.end()) return true;
    }
    return false;
}

// Applies `s` to the EXIF time tags and the XMP dates that are actually present (§4.5).
// Returns the number of fields rewritten.
int apply_time_shift(Exiv2::ExifData& exif, Exiv2::XmpData& xmp, const TimeShift& s) {
    int touched = 0;
    for (const char* k : kExifTimeKeys) {
        auto it = exif.findKey(Exiv2::ExifKey(k));
        if (it == exif.end()) continue;
        const std::string cur = strip_nul(it->toString());
        bool ok = false;
        const std::string next = shift_exif_datetime(cur, s, ok);
        if (!ok) {
            log_warn("MetaWrite", "metadata.cpp", "time shift skipped for unparsable value");
            continue;
        }
        (void)it->setValue(next);
        ++touched;
    }
    for (const char* k : kXmpTimeKeys) {
        const std::string key = normalize_xmp_key(k);
        auto it = xmp.findKey(Exiv2::XmpKey(key));
        if (it == xmp.end()) continue;
        XmpDate d;
        const std::string cur = strip_nul(it->toString());
        if (!parse_xmp_date(cur, d)) {
            log_warn("MetaWrite", "metadata.cpp", "time shift skipped for unparsable xmp date");
            continue;
        }
        DateTime out;
        std::string fraction = d.fraction;
        std::string offset = d.offset;
        if (s.mode == TimeShift::Mode::Delta) {
            if (!apply_delta(d.dt, s, out)) continue;
        } else {
            // The value's own offset (when present) is authoritative; otherwise the caller's
            // `from` zone is the interpretation (§5.3).
            TimeShift delta;
            const int src_off = d.has_offset ? offset_minutes_of(d.offset) : s.from_offset_min;
            delta.minutes = s.to_offset_min - src_off;
            if (!apply_delta(d.dt, delta, out)) continue;
            if (d.has_offset) offset = offset_time_string(s.to_offset_min);
        }
        (void)it->setValue(format_iso_base(out) + fraction + offset);
        ++touched;
    }
    return touched;
}

}  // namespace

// R1 fallback helper (defined at the bottom of this file): mirror the EXIF fields that a
// container cannot carry into XMP. Outside the frozen interface; the unit test declares it too.
std::size_t detail_mirror_key_exif_to_xmp(const Exiv2::ExifData& exif, Exiv2::XmpData& xmp);

// Runtime metadata-only capability probe (defined next to write_metadata_exiv2). Outside the
// frozen interface: T8's format_supports_metadata_only() should delegate to it; the unit test
// declares it too.
bool detail_metadata_only_supported(std::string_view format_id);

// ===========================================================================
// pure helpers (§3.7 "可单测纯函数")
// ===========================================================================

bool TimeShift::is_noop() const {
    if (mode == Mode::TimezoneSemantic) return from_offset_min == to_offset_min;
    return years == 0 && months == 0 && days == 0 && hours == 0 && minutes == 0 && seconds == 0;
}

bool BatchRules::is_noop() const {
    if (time_shift && !time_shift->is_noop()) return false;
    if (gps.has_value() || gps_clear) return false;
    if (!exif_edits.empty() || !xmp_edits.empty()) return false;
    if (strip_privacy || sync_mtime) return false;
    return true;
}

std::string offset_time_string(int offset_min) {
    // EXIF 2.31 OffsetTime is "±HH:MM"; clamp to the representable range.
    constexpr int kMax = 23 * 60 + 59;
    const int v = std::clamp(offset_min, -kMax, kMax);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%c%02d:%02d", v < 0 ? '-' : '+', std::abs(v) / 60, std::abs(v) % 60);
    return std::string(buf);
}

std::string shift_exif_datetime(const std::string& exif_dt, const TimeShift& s, bool& ok) {
    ok = false;
    if (s.mode == TimeShift::Mode::TimezoneSemantic) {
        return reinterpret_timezone(exif_dt, s.from_offset_min, s.to_offset_min, ok);
    }
    DateTime in;
    if (!parse_exif_datetime(exif_dt, in)) return exif_dt;
    DateTime out;
    if (!apply_delta(in, s, out)) return exif_dt;
    ok = true;
    return format_exif_datetime(out);
}

std::string reinterpret_timezone(const std::string& exif_dt, int from_min, int to_min, bool& ok) {
    ok = false;
    DateTime in;
    if (!parse_exif_datetime(exif_dt, in)) return exif_dt;
    TimeShift delta;
    delta.minutes = to_min - from_min;
    DateTime out;
    if (!apply_delta(in, delta, out)) return exif_dt;
    ok = true;
    return format_exif_datetime(out);
}

// ===========================================================================
// read
// ===========================================================================

SourceMeta read_metadata(const std::filesystem::path& p) {
    SourceMeta m;
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(p.string());
        if (!img) {
            m.error = "exiv2: cannot open " + p.string();
            return m;
        }
        img->readMetadata();
        m.exif = img->exifData();
        m.xmp = img->xmpData();
        if (img->iccProfileDefined()) {
            const Exiv2::DataBuf& icc = img->iccProfile();
            if (icc.size() > 0) m.icc.assign(reinterpret_cast<const char*>(icc.c_data()), icc.size());
        }
    } catch (const Exiv2::Error& e) {
        m.error = std::string("exiv2: ") + e.what();
        return m;
    } catch (const std::exception& e) {
        m.error = e.what();
        return m;
    } catch (...) {
        m.error = "exiv2: unknown error while reading " + p.string();
        return m;
    }

    auto oit = m.exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
    if (oit != m.exif.end()) {
        const int64_t v = oit->toInt64();
        m.orientation = (v >= 1 && v <= 8) ? static_cast<int>(v) : 1;
    }
    const bool lat = m.exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude")) != m.exif.end();
    const bool lon = m.exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLongitude")) != m.exif.end();
    m.has_gps = lat && lon;
    m.has_time = !effective_datetime(m.exif, m.xmp).empty();
    log_makernote(m.exif, "MetaRead");
    return m;
}

// ===========================================================================
// plan synthesis
// ===========================================================================

MetadataPlan build_plan(const SourceMeta& src, const BatchRules& rules,
                        const std::optional<MetadataOverride>& ex) {
    MetadataPlan plan;
    plan.exif = src.exif;
    plan.xmp = src.xmp;
    if (!src.error.empty()) {
        log_warn("MetaRead", "metadata.cpp", "source metadata unavailable; continuing with empty metadata");
    }

    // effective = 源 ⊕ BatchRules ⊕ 例外（例外逐项覆盖；ignore_batch 先清空批量规则）
    BatchRules eff;
    const bool ignore_batch = ex.has_value() && ex->ignore_batch;
    if (!ignore_batch) eff = rules;
    if (ex.has_value()) {
        if (ex->time_shift.has_value()) eff.time_shift = ex->time_shift;  // 覆盖（inherit = nullopt）
        if (ex->gps.has_value()) eff.gps = ex->gps;
        if (ex->gps_clear) eff.gps_clear = true;
        // 例外编辑追加在批量编辑之后 → 同 key 的后者覆盖前者（"逐项覆盖"）
        eff.exif_edits.insert(eff.exif_edits.end(), ex->exif_edits.begin(), ex->exif_edits.end());
        eff.xmp_edits.insert(eff.xmp_edits.end(), ex->xmp_edits.begin(), ex->xmp_edits.end());
        if (ex->strip_privacy.has_value()) eff.strip_privacy = *ex->strip_privacy;  // 三态覆盖
    }

    // 隐私剥除优先级最高：剥除后不得再写任何 EXIF/XMP 字段（§4.5）
    if (eff.strip_privacy) {
        strip_privacy(plan.exif, plan.xmp);
        plan.has_time = false;
        plan.datetime_original.clear();
        return plan;
    }

    if (eff.gps_clear) {
        clear_gps(plan.exif);
    } else if (eff.gps.has_value()) {
        write_gps(plan.exif, *eff.gps);
    }

    if (eff.time_shift.has_value() && !eff.time_shift->is_noop()) {
        if (!has_any_time_field(plan.exif, plan.xmp)) {
            plan.warnings.push_back(
                Warning{WarningKind::TimeFieldMissing, "no time field found, time shift skipped"});
        } else {
            apply_time_shift(plan.exif, plan.xmp, *eff.time_shift);
            if (eff.time_shift->mode == TimeShift::Mode::TimezoneSemantic) {
                // EXIF 2.31 OffsetTime* describes the EXIF time tags, so only write them when at
                // least one of those tags is present.
                bool any_exif_time = false;
                for (const char* k : kExifTimeKeys) {
                    if (plan.exif.findKey(Exiv2::ExifKey(k)) != plan.exif.end()) any_exif_time = true;
                }
                if (any_exif_time) {
                    const std::string off = offset_time_string(eff.time_shift->to_offset_min);
                    for (const char* k : {"Exif.Photo.OffsetTime", "Exif.Photo.OffsetTimeOriginal",
                                          "Exif.Photo.OffsetTimeDigitized"}) {
                        plan.exif[k] = off;
                    }
                }
            }
        }
    }

    std::vector<std::string> edit_errors;
    apply_edits(plan.exif, plan.xmp, eff.exif_edits, eff.xmp_edits, edit_errors);
    for (const std::string& e : edit_errors) {
        log_warn("MetaWrite", "metadata.cpp", "tag edit rejected");
        log_debug("MetaWrite", "metadata.cpp", e.c_str());
    }

    plan.datetime_original = effective_datetime(plan.exif, plan.xmp);
    plan.has_time = !plan.datetime_original.empty();
    return plan;
}

// ===========================================================================
// payloads (G1)
// ===========================================================================

Payloads make_payloads(const MetadataPlan& plan) {
    ensure_xmp();
    Payloads out;
    if (!plan.exif.empty()) {
        // ExifParser::encode() takes a mutable ExifData (non-intrusive writing may patch it);
        // the frozen plan is const, so encode from a copy.
        Exiv2::ExifData exif = plan.exif;
        Exiv2::Blob blob;
        Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, exif);
        out.exif_blob.assign(reinterpret_cast<const char*>(blob.data()), blob.size());
    }
    if (!plan.xmp.empty()) {
        std::string packet;
        const int rc = Exiv2::XmpParser::encode(packet, plan.xmp);
        if (rc == 0) {
            out.xmp_rdf = std::move(packet);
        } else {
            log_error("MetaWrite", "metadata.cpp", "XmpParser::encode failed");
        }
    }
    return out;
}

// ===========================================================================
// write path A (JPEG/PNG/TIFF/WebP, post-encode)
// ===========================================================================

std::string write_metadata_exiv2(const std::filesystem::path& out_file, const MetadataPlan& plan,
                                 const Payloads& payloads,
                                 std::vector<std::string>* out_warnings) {
    (void)payloads;  // 后写路径把容器直接交给 Exiv2；payloads 服务于 JXL/HEIF 注入路径（§3.8）
    const std::string ext = lower_copy(out_file.extension().string());
    const bool is_png = (ext == ".png");

    if (ext == ".bmp") {
        // BMP has no metadata container; §5.8 assigns the MetadataDropped warning to the pipeline.
        log_info("MetaWrite", "metadata.cpp", "bmp output carries no metadata container");
        return {};
    }
    if (plan.exif.empty() && plan.xmp.empty()) {
        return {};  // nothing to write (e.g. privacy strip): the encoder output carries none
    }

    std::string err = exiv2_apply(out_file, &plan.exif, &plan.xmp);
    if (err.empty() && (!is_png || key_exif_landed(out_file, plan.exif))) return {};

    if (is_png) {
        // R1 fallback, kept as a safety net: this branch is only reachable when the exiv2 build
        // has no PNG support (EXV_HAVE_LIBZ undefined → PNG is not a registered image type) or
        // when a PNG eXIf chunk write is silently dropped. With the current vcpkg exiv2 port
        // (features bmff,xmp,png → zlib) PNG is writable and the normal path above succeeds —
        // verified by the r1/png/* cases in tests/unit/test_metadata.cpp (R1 CLOSED).
        Exiv2::XmpData mirrored = plan.xmp;
        const std::size_t mirrored_n = detail_mirror_key_exif_to_xmp(plan.exif, mirrored);
        if (mirrored_n > 0) {
            const std::string err2 = exiv2_apply(out_file, nullptr, &mirrored);
            if (err2.empty()) {
                log_warn("MetaWrite", "metadata.cpp", "png exif mirrored to xmp");
                note_write_warning(plan, out_warnings, WarningKind::MetadataDropped,
                                   "png exif mirrored to xmp");
                return {};
            }
            err = err2;
        }
    }

    // Metadata-only loss: keep the pixel output, report it (§5.2 BMP policy; R1 fallback for PNG).
    std::string detail = is_png ? "png metadata dropped: " + err : "metadata dropped: " + err;
    log_error("MetaWrite", "metadata.cpp", detail.c_str());
    note_write_warning(plan, out_warnings, WarningKind::MetadataDropped, detail);
    return {};
}

// ---------------------------------------------------------------------------
// metadata-only capability (runtime probe; not part of the frozen interface)
// ---------------------------------------------------------------------------

bool detail_metadata_only_supported(std::string_view format_id) {
    // v1 policy (§5.5): only containers Exiv2 can rewrite losslessly are metadata-only capable.
    // The container itself is probed at runtime with Exiv2's own type detection, so a future
    // exiv2 build that loses a format handler (e.g. no zlib → no PNG) degrades automatically
    // instead of failing per file. T8's format_supports_metadata_only() should delegate here.
    struct Sample {
        std::string_view id;
        const unsigned char* sig;
        std::size_t size;
    };
    // Structurally complete minimal headers: Exiv2's type detection reads beyond the bare magic
    // (chunk headers / IFD offset), so truncated magics are not enough.
    static const unsigned char kJpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46,
                                          0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01};
    static const unsigned char kPng[] = {  // signature + full IHDR chunk (1x1 RGBA8)
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89};
    static const unsigned char kTiff[] = {  // "II*\0", IFD at 8 with one ImageWidth entry
        0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x03, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const unsigned char kWebp[] = {  // RIFF/WEBP + minimal VP8X chunk
        0x52, 0x49, 0x46, 0x46, 0x16, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50,
        0x38, 0x58, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00};
    static const Sample kSamples[] = {{"jpeg", kJpeg, sizeof(kJpeg)},
                                      {"png", kPng, sizeof(kPng)},
                                      {"tiff", kTiff, sizeof(kTiff)},
                                      {"webp", kWebp, sizeof(kWebp)}};
    for (const Sample& s : kSamples) {
        if (format_id != s.id) continue;
        try {
            return Exiv2::ImageFactory::getType(s.sig, s.size) != Exiv2::ImageType::none;
        } catch (...) {
            return false;
        }
    }
    return false;  // bmp/heif/avif/jxl and anything else: no lossless metadata rewrite in v1
}

// The frozen predicate is declared in core/pipeline.h and owned by T8's pipeline.cpp. Until that
// lands this weak definition keeps callers/tests linkable; a strong definition in pipeline.cpp
// takes precedence (ELF weak-symbol semantics), so T8 stays the owner. Omitted on non-GNU
// toolchains so a missing T8 implementation surfaces as a link error instead of silently working.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
bool format_supports_metadata_only(std::string_view format_id) {
    return detail_metadata_only_supported(format_id);
}
#endif

// ===========================================================================
// metadata-only mode (same container, zero re-encode)
// ===========================================================================

std::string rewrite_metadata_only(const std::filesystem::path& src, const std::filesystem::path& out,
                                  const MetadataPlan& plan, const Payloads& payloads) {
    (void)payloads;  // containers are handed to Exiv2 directly
    std::error_code ec;
    std::filesystem::copy_file(src, out, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return "copy " + src.string() + " -> " + out.string() + " failed: " + ec.message();
    // Exiv2 rewrites the container in place (JPEG/WebP/TIFF keep the compressed payload bytes);
    // the source file is never touched because we work on the copy.
    return exiv2_apply(out, &plan.exif, &plan.xmp);
}

// ===========================================================================
// mtime
// ===========================================================================

std::string sync_file_mtime(const std::filesystem::path& out, const std::string& exif_datetime) {
    if (exif_datetime.empty()) return {};
    DateTime t;
    if (!parse_exif_datetime(exif_datetime, t)) return "invalid exif datetime '" + exif_datetime + "'";
    std::tm tm{};
    tm.tm_year = t.year - 1900;
    tm.tm_mon = t.month - 1;
    tm.tm_mday = t.day;
    tm.tm_hour = t.hour;
    tm.tm_min = t.minute;
    tm.tm_sec = t.second;
    tm.tm_isdst = -1;
    const std::time_t secs = std::mktime(&tm);  // local time, mirrors EXIF semantics
    if (secs == static_cast<std::time_t>(-1)) return "mktime failed for '" + exif_datetime + "'";
    const auto sys = std::chrono::system_clock::from_time_t(secs);
    std::error_code ec;
#if defined(_WIN32)
        // M3：MSVC STL 的 file_clock 只提供 LWG 3694 的 utc 对（无 from_sys；clock_cast
        // 亦不受理 file_clock 目标——本机探针实证），经 utc 中转，与 POSIX 分支同瞬点。
        const auto mtime_ft =
            std::chrono::file_clock::from_utc(std::chrono::utc_clock::from_sys(sys));
#else
        const auto mtime_ft = std::filesystem::file_time_type::clock::from_sys(sys);
#endif
        std::filesystem::last_write_time(out, mtime_ft, ec);
    if (ec) return "set mtime failed: " + ec.message();
    return {};
}

// ===========================================================================
// GPS
// ===========================================================================

void write_gps(Exiv2::ExifData& exif, const GpsData& gps) {
    const double lat = std::clamp(std::isfinite(gps.lat) ? gps.lat : 0.0, -90.0, 90.0);
    const double lon = std::clamp(std::isfinite(gps.lon) ? gps.lon : 0.0, -180.0, 180.0);

    exif["Exif.GPSInfo.GPSVersionID"] = std::string("2 2 0 0");
    exif["Exif.GPSInfo.GPSLatitude"] = join_dms(dms_rationals(lat));
    exif["Exif.GPSInfo.GPSLatitudeRef"] = std::string(lat < 0 ? "S" : "N");
    exif["Exif.GPSInfo.GPSLongitude"] = join_dms(dms_rationals(lon));
    exif["Exif.GPSInfo.GPSLongitudeRef"] = std::string(lon < 0 ? "W" : "E");

    if (gps.altitude.has_value() && std::isfinite(*gps.altitude)) {
        const double alt = *gps.altitude;
        exif["Exif.GPSInfo.GPSAltitudeRef"] = std::string(alt < 0 ? "1" : "0");  // 0 = above sea level
        exif["Exif.GPSInfo.GPSAltitude"] = urational(std::llround(std::fabs(alt) * 1000.0), 1000);
    } else {
        // A GPS override replaces the whole block: stale optionals must not survive.
        erase_exif_key(exif, "Exif.GPSInfo.GPSAltitudeRef");
        erase_exif_key(exif, "Exif.GPSInfo.GPSAltitude");
    }

    if (gps.direction.has_value() && std::isfinite(*gps.direction)) {
        double dir = std::fmod(*gps.direction, 360.0);
        if (dir < 0) dir += 360.0;
        exif["Exif.GPSInfo.GPSImgDirectionRef"] = std::string("T");  // true north
        exif["Exif.GPSInfo.GPSImgDirection"] = urational(std::llround(dir * 100.0), 100);
    } else {
        erase_exif_key(exif, "Exif.GPSInfo.GPSImgDirectionRef");
        erase_exif_key(exif, "Exif.GPSInfo.GPSImgDirection");
    }

    DateTime ts;
    if (gps.timestamp.has_value() && parse_exif_datetime(*gps.timestamp, ts)) {
        char date[16];
        std::snprintf(date, sizeof(date), "%04d:%02d:%02d", ts.year, ts.month, ts.day);
        exif["Exif.GPSInfo.GPSDateStamp"] = std::string(date);
        exif["Exif.GPSInfo.GPSTimeStamp"] =
            urational(ts.hour, 1) + " " + urational(ts.minute, 1) + " " + urational(ts.second, 1);
    } else {
        if (gps.timestamp.has_value()) {
            log_warn("MetaWrite", "metadata.cpp", "gps timestamp is not 'YYYY:MM:DD HH:MM:SS'; skipped");
        }
        erase_exif_key(exif, "Exif.GPSInfo.GPSDateStamp");
        erase_exif_key(exif, "Exif.GPSInfo.GPSTimeStamp");
    }
}

void clear_gps(Exiv2::ExifData& exif) {
    for (auto it = exif.begin(); it != exif.end();) {
        if (it->key().rfind("Exif.GPSInfo.", 0) == 0) {
            it = exif.erase(it);
        } else {
            ++it;
        }
    }
}

// ===========================================================================
// privacy strip
// ===========================================================================

void strip_privacy(Exiv2::ExifData& exif, Exiv2::XmpData& xmp) {
    exif.clear();
    xmp.clear();
    // ICC and pixels are not metadata containers → untouched (they live in the callers'
    // SourceMeta::icc / the encoded image data).
}

// ===========================================================================
// tag edits
// ===========================================================================

void apply_edits(Exiv2::ExifData& exif, Exiv2::XmpData& xmp,
                 const std::vector<TagEdit>& exif_edits, const std::vector<TagEdit>& xmp_edits,
                 std::vector<std::string>& errors) {
    for (const TagEdit& e : exif_edits) {
        const bool remove = e.remove || !e.value.has_value();
        if (e.key.empty()) {
            errors.push_back("exif edit with empty key");
            continue;
        }
        if (e.key.rfind("Exif.", 0) != 0) {
            errors.push_back("exif edit key is not an Exif key: " + e.key);
            continue;
        }
        try {
            Exiv2::ExifKey key(e.key);  // throws for unknown groups/tags
            if (remove) {
                auto it = exif.findKey(key);
                if (it != exif.end()) exif.erase(it);
            } else if (auto it = exif.findKey(key); it != exif.end()) {
                // setValue() returns 0 on success; it clobbers the old value on failure, so keep
                // a clone to restore it (a rejected edit must not damage the source metadata).
                Exiv2::Value::UniquePtr previous = it->value().clone();
                if (it->setValue(*e.value) != 0) {
                    (void)it->setValue(previous.get());
                    errors.push_back("exif set " + e.key + ": value does not match the tag type");
                }
            } else {
                Exiv2::Exifdatum& fresh = exif[e.key];
                if (fresh.setValue(*e.value) != 0) {
                    auto it = exif.findKey(key);
                    if (it != exif.end()) exif.erase(it);
                    errors.push_back("exif set " + e.key + ": value does not match the tag type");
                }
            }
        } catch (const Exiv2::Error& err) {
            errors.push_back("exif " + std::string(remove ? "remove " : "set ") + e.key + ": " + err.what());
        } catch (const std::exception& err) {
            errors.push_back("exif " + std::string(remove ? "remove " : "set ") + e.key + ": " + err.what());
        }
    }

    for (const TagEdit& e : xmp_edits) {
        const bool remove = e.remove || !e.value.has_value();
        if (e.key.empty()) {
            errors.push_back("xmp edit with empty key");
            continue;
        }
        if (e.key.rfind("Xmp.", 0) != 0) {
            errors.push_back("xmp edit key is not an Xmp key: " + e.key);
            continue;
        }
        try {
            const std::string key = normalize_xmp_key(e.key);
            Exiv2::XmpKey xkey(key);  // throws for unknown namespaces
            if (remove) {
                auto it = xmp.findKey(xkey);
                if (it != xmp.end()) xmp.erase(it);
            } else if (auto it = xmp.findKey(xkey); it != xmp.end()) {
                Exiv2::Value::UniquePtr previous = it->value().clone();
                // LangAlt properties (dc:title, dc:description, …) accept a plain string and
                // become the x-default entry (G15).
                if (it->setValue(*e.value) != 0) {
                    (void)it->setValue(previous.get());
                    errors.push_back("xmp set " + e.key + ": value was rejected");
                }
            } else {
                Exiv2::Xmpdatum& fresh = xmp[key];
                if (fresh.setValue(*e.value) != 0) {
                    auto it = xmp.findKey(xkey);
                    if (it != xmp.end()) xmp.erase(it);
                    errors.push_back("xmp set " + e.key + ": value was rejected");
                }
            }
        } catch (const Exiv2::Error& err) {
            errors.push_back("xmp " + std::string(remove ? "remove " : "set ") + e.key + ": " + err.what());
        } catch (const std::exception& err) {
            errors.push_back("xmp " + std::string(remove ? "remove " : "set ") + e.key + ": " + err.what());
        }
    }
}

// ===========================================================================
// effective datetime
// ===========================================================================

std::string effective_datetime(const Exiv2::ExifData& exif, const Exiv2::XmpData& xmp) {
    for (const char* k : {"Exif.Photo.DateTimeOriginal", "Exif.Image.DateTime"}) {
        auto it = exif.findKey(Exiv2::ExifKey(k));
        if (it == exif.end()) continue;
        const std::string v = strip_nul(it->toString());
        if (!v.empty()) return v;
    }
    for (const char* k : {"Xmp.xmp.CreateDate", "Xmp.xmp:CreateDate"}) {
        auto it = xmp.findKey(Exiv2::XmpKey(normalize_xmp_key(k)));
        if (it == xmp.end()) continue;
        const std::string v = strip_nul(it->toString());
        if (v.empty()) continue;
        XmpDate d;
        if (parse_xmp_date(v, d)) return format_exif_datetime(d.dt);  // XMP ISO-8601 → EXIF form
        DateTime t;
        if (parse_exif_datetime(v, t)) return v;
        return v;
    }
    return {};
}

// ===========================================================================
// internal helpers that need external (test) visibility
// ===========================================================================

// XMP writes GPS as "DDD,MM.mmmRef" (decimal minutes); EXIF stores three rationals.
bool exif_dms_to_degrees(const std::string& dms, double& degrees) {
    double parts[3] = {0.0, 0.0, 0.0};
    std::size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        while (pos < dms.size() && dms[pos] == ' ') ++pos;
        const std::size_t slash = dms.find('/', pos);
        if (slash == std::string::npos) return false;
        std::size_t end = dms.find(' ', slash);
        if (end == std::string::npos) end = dms.size();
        const double num = std::strtod(dms.substr(pos, slash - pos).c_str(), nullptr);
        const double den = std::strtod(dms.substr(slash + 1, end - slash - 1).c_str(), nullptr);
        if (den == 0.0) return false;
        parts[i] = num / den;
        pos = end;
    }
    degrees = parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
    return true;
}

std::string xmp_gps_value(const std::string& ref, const std::string& dms) {
    double degrees = 0.0;
    if (!exif_dms_to_degrees(dms, degrees)) return {};
    const double mag = std::fabs(degrees);
    const int d = static_cast<int>(mag);
    const double minutes = (mag - d) * 60.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d,%.3f%s", d, minutes,
                  ref.empty() ? "" : ref.substr(0, 1).c_str());
    return std::string(buf);
}

// R1 fallback: mirror the EXIF fields that a container may not carry into XMP. Not part of the
// frozen interface — declared by the unit test only. Returns the number of properties written.
std::size_t detail_mirror_key_exif_to_xmp(const Exiv2::ExifData& exif, Exiv2::XmpData& xmp) {
    std::size_t n = 0;
    const auto value_of = [&exif](const char* key) -> std::string {
        auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it == exif.end()) return {};
        return strip_nul(it->toString());
    };

    const std::string dto = value_of("Exif.Photo.DateTimeOriginal");
    if (!dto.empty()) {
        DateTime t;
        XmpDate already_iso;
        std::string iso;
        if (parse_exif_datetime(dto, t)) {
            iso = format_iso_base(t);
        } else if (parse_xmp_date(dto, already_iso)) {
            iso = dto;
        }
        if (!iso.empty()) {
            xmp["Xmp.exif.DateTimeOriginal"] = iso;  // XMP dates are ISO-8601
            ++n;
        }
    }
    const std::string artist = value_of("Exif.Image.Artist");
    if (!artist.empty()) {
        xmp["Xmp.tiff.Artist"] = artist;
        ++n;
    }
    const std::string lat =
        xmp_gps_value(value_of("Exif.GPSInfo.GPSLatitudeRef"), value_of("Exif.GPSInfo.GPSLatitude"));
    if (!lat.empty()) {
        xmp["Xmp.exif.GPSLatitude"] = lat;  // "31,13.824N"
        ++n;
    }
    const std::string lon = xmp_gps_value(value_of("Exif.GPSInfo.GPSLongitudeRef"),
                                          value_of("Exif.GPSInfo.GPSLongitude"));
    if (!lon.empty()) {
        xmp["Xmp.exif.GPSLongitude"] = lon;
        ++n;
    }
    return n;
}

}  // namespace pp
