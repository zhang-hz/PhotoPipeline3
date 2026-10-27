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
#include "core/filetime.h"

#include <libheif/heif.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/fsops.h" // R-002/R-030：原子写四件套（临时文件 + rename）

#include "core/logger.h"
#include "core/pipeline.h" // frozen declaration of format_supports_metadata_only()

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
    static const XmpTeardown teardown; // runs after main(); constructed once
    (void)teardown;
}

// ---------------------------------------------------------------------------
// small string helpers
// ---------------------------------------------------------------------------

std::string lower_copy(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// Trailing NULs are part of EXIF ASCII values (count includes the terminator) but never of
// the logical value.
std::string strip_nul(std::string s) {
    while (!s.empty() && s.back() == '\0')
        s.pop_back();
    return s;
}

// Exiv2 XMP keys are "Xmp.<prefix>.<local>"; the task book also uses the XMP property
// notation "Xmp.<prefix>:<local>" (e.g. "Xmp.xmp:CreateDate"). Accept both.
std::string normalize_xmp_key(std::string_view key) {
    if (key.rfind("Xmp.", 0) != 0)
        return std::string(key);
    const std::size_t colon = key.find(':', 4);
    if (colon == std::string_view::npos)
        return std::string(key);
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
    if (m == 2 && is_leap_year(y))
        return 29;
    return kDays[m - 1];
}

int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
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

void civil_from_days(int64_t z, int &y, int &m, int &d) {
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

bool parse_digits(std::string_view s, std::size_t pos, std::size_t len, int &out) {
    int v = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const char c = s[pos + i];
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

// Strict EXIF 2.3 form "YYYY:MM:DD HH:MM:SS" (19 chars, no sub-second part).
bool parse_exif_datetime(std::string_view s, DateTime &out) {
    if (s.size() != 19)
        return false;
    if (s[4] != ':' || s[7] != ':' || s[10] != ' ' || s[13] != ':' || s[16] != ':')
        return false;
    DateTime t;
    if (!parse_digits(s, 0, 4, t.year) || !parse_digits(s, 5, 2, t.month) ||
        !parse_digits(s, 8, 2, t.day) || !parse_digits(s, 11, 2, t.hour) ||
        !parse_digits(s, 14, 2, t.minute) || !parse_digits(s, 17, 2, t.second)) {
        return false;
    }
    if (t.year < 1 || t.year > 9999)
        return false;
    if (t.month < 1 || t.month > 12)
        return false;
    if (t.day < 1 || t.day > days_in_month(t.year, t.month))
        return false;
    if (t.hour > 23 || t.minute > 59 || t.second > 60)
        return false; // 60 = leap second, normalized below
    out = t;
    return true;
}

std::string format_exif_datetime(const DateTime &t) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d:%02d:%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour,
                  t.minute, t.second);
    return std::string(buf);
}

std::string format_iso_base(const DateTime &t) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", t.year, t.month, t.day, t.hour,
                  t.minute, t.second);
    return std::string(buf);
}

// Delta arithmetic: years/months are calendar steps (day-of-month clamps to the target month),
// days/hours/minutes/seconds are exact duration steps applied afterwards (§4.5).
bool apply_delta(const DateTime &in, const TimeShift &s, DateTime &out) {
    int y = in.year + s.years;
    int64_t m = static_cast<int64_t>(in.month) + s.months;
    y += static_cast<int>(floor_div(m - 1, 12));
    m = floor_mod(m - 1, 12) + 1;
    if (y < 1 || y > 9999)
        return false;
    int day = std::min(in.day, days_in_month(y, static_cast<int>(m)));

    int64_t days =
        days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(day)) + s.days;
    int64_t secs = static_cast<int64_t>(in.hour) * 3600 + static_cast<int64_t>(in.minute) * 60 +
                   in.second + static_cast<int64_t>(s.hours) * 3600 +
                   static_cast<int64_t>(s.minutes) * 60 + s.seconds;
    const int64_t carry = floor_div(secs, 86400);
    secs -= carry * 86400;
    days += carry;

    int ny = 0, nm = 0, nd = 0;
    civil_from_days(days, ny, nm, nd);
    if (ny < 1 || ny > 9999)
        return false;
    out = DateTime{ny,
                   nm,
                   nd,
                   static_cast<int>(secs / 3600),
                   static_cast<int>((secs % 3600) / 60),
                   static_cast<int>(secs % 60)};
    return true;
}

// ---------------------------------------------------------------------------
// XMP ISO-8601 date handling ("2024-03-01T10:00:00.123+08:00")
// ---------------------------------------------------------------------------

struct XmpDate {
    DateTime dt;
    std::string fraction; // ".123" or ""
    bool has_offset = false;
    std::string offset; // "Z" / "+08:00" (only when has_offset)
};

bool parse_iso_offset(std::string_view tail, bool &has, std::string &text) {
    has = false;
    text.clear();
    if (tail.empty())
        return true;
    if (tail.back() == 'Z' || tail.back() == 'z') {
        has = true;
        text = "Z";
        return true;
    }
    if (tail.size() >= 6) {
        const std::size_t p = tail.size() - 6;
        const char sign = tail[p];
        int hh = 0, mm = 0;
        if ((sign == '+' || sign == '-') && tail[p + 3] == ':' &&
            parse_digits(tail, p + 1, 2, hh) && parse_digits(tail, p + 4, 2, mm) && hh <= 23 &&
            mm <= 59) {
            has = true;
            text = std::string(tail.substr(p));
            return true;
        }
    }
    return true; // no offset recorded (e.g. only fractional seconds)
}

bool parse_xmp_date(std::string_view v, XmpDate &out) {
    if (v.size() < 19)
        return false;
    std::string head(v.substr(0, 19));
    if (head[4] != '-' || head[7] != '-' || head[10] != 'T' || head[13] != ':' || head[16] != ':') {
        // tolerate the space separator some writers emit
        if (head[10] != ' ')
            return false;
    }
    for (const std::size_t p : {4u, 7u, 10u, 13u, 16u})
        head[p] = (p == 10 ? ' ' : ':');
    DateTime dt;
    if (!parse_exif_datetime(head, dt))
        return false;

    std::string_view tail = v.substr(19);
    std::string offset;
    bool has_offset = false;
    if (!parse_iso_offset(tail, has_offset, offset))
        return false;
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
    if (offset.empty() || offset == "Z" || offset == "z")
        return 0;
    const char sign = offset.front();
    int hh = 0, mm = 0;
    if (offset.size() < 6 || !parse_digits(offset, 1, 2, hh) || !parse_digits(offset, 4, 2, mm))
        return 0;
    const int v = hh * 60 + mm;
    return sign == '-' ? -v : v;
}

// ---------------------------------------------------------------------------
// EXIF key helpers
// ---------------------------------------------------------------------------

const char *const kExifTimeKeys[3] = {"Exif.Photo.DateTimeOriginal", "Exif.Photo.DateTimeDigitized",
                                      "Exif.Image.DateTime"};
const char *const kXmpTimeKeys[2] = {"Xmp.xmp.CreateDate", "Xmp.xmp.ModifyDate"};
// R1: fields mirrored into XMP when a container cannot carry EXIF.
const char *const kMirrorKeys[3] = {"Exif.Photo.DateTimeOriginal", "Exif.Image.Artist",
                                    "Exif.GPSInfo.GPSLatitude"};

void erase_exif_key(Exiv2::ExifData &exif, const char *key) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it != exif.end())
        exif.erase(it);
}

std::string urational(int64_t num, int64_t den) {
    if (den <= 0)
        den = 1;
    if (num < 0)
        num = 0; // callers pass magnitudes; hemisphere is carried by *Ref
    return std::to_string(num) + "/" + std::to_string(den);
}

// 3 × unsigned rational (deg / min / sec), magnitude only; hemisphere goes into *Ref (G7).
std::array<std::string, 3> dms_rationals(double degrees) {
    const double mag = std::isfinite(degrees) ? std::fabs(degrees) : 0.0;
    const int64_t micro = std::llround(mag * 3600.0 * 1e6); // micro arc-seconds
    const int64_t d = micro / 3600000000LL;
    int64_t rem = micro % 3600000000LL;
    const int64_t m = rem / 60000000LL;
    rem %= 60000000LL;
    return {urational(d, 1), urational(m, 1), urational(rem, 1000000)};
}

std::string join_dms(const std::array<std::string, 3> &v) { return v[0] + " " + v[1] + " " + v[2]; }

// MakerNote policy (§4.5 / consensus §3.6): the bytes are carried across containers untouched,
// never parsed and never edited — Exiv2 copies the Exif.Photo.MakerNote value verbatim on
// setExifData()/writeMetadata(); all this layer does is report the size.
void log_makernote(const Exiv2::ExifData &exif, std::string_view stage) {
    auto it = exif.findKey(Exiv2::ExifKey("Exif.Photo.MakerNote"));
    if (it == exif.end())
        return;
    const std::string msg = "Makernote present, " + std::to_string(it->value().size()) + " bytes";
    log_info(stage, "metadata.cpp", msg.c_str());
}

// R-171（M7-WP1）：写路径告警**只**走 `out_warnings` 显式出参（M2-T4 的加性通道）——
// 既消灭 const_cast 突破 const 契约的写法，也收掉"plan.warnings 与 out_warnings 双通道
// 纯靠注释 lockstep"的隐患。plan.warnings 自此只承载 build_plan() 的合成期告警。
void note_write_warning(std::vector<std::string> *out_warnings, const std::string &detail) {
    if (out_warnings != nullptr)
        out_warnings->push_back(detail);
}

// ---------------------------------------------------------------------------
// TIFF structure probe（第二轮裁定：多页不支持、缩略图支持）
// ---------------------------------------------------------------------------
// 只解析 IFD0/IFD1（不遍历 Exif/GPS 子目录计数），校验：
//   * IFD 链目录数、IFD1 是否为 reduced-resolution 缩略图；
//   * strip/tile/JPEG 数据偏移是否在文件内；
//   * SubIFDs 数量（转码输出必须为 0）。
// 读整个文件不划算（TIFF 可能数百 MB），这里按需 seek/read。
struct TiffProbe {
    bool ok = false;
    int dirs = 0;
    int sub_ifds = 0;
    bool ifd1_thumbnail = false;
    bool offsets_in_bounds = true;
    std::string error;
};

class TiffReader {
public:
    explicit TiffReader(const std::filesystem::path &p) : f_(p, std::ios::binary) {
        if (!f_)
            return;
        f_.seekg(0, std::ios::end);
        const std::streamoff n = f_.tellg();
        if (n < 0)
            return;
        size_ = static_cast<std::uint64_t>(n);
        open_ = true;
    }
    bool open() const { return open_; }
    std::uint64_t size() const { return size_; }

    bool read_at(std::uint64_t off, void *dst, std::size_t n) {
        if (off > size_ || n > size_ - off)
            return false;
        f_.clear();
        f_.seekg(static_cast<std::streamoff>(off), std::ios::beg);
        f_.read(static_cast<char *>(dst), static_cast<std::streamsize>(n));
        return static_cast<std::size_t>(f_.gcount()) == n;
    }

private:
    std::ifstream f_;
    std::uint64_t size_ = 0;
    bool open_ = false;
};

bool tiff_read_u16(TiffReader &r, std::uint64_t off, bool little, std::uint16_t &v) {
    unsigned char b[2];
    if (!r.read_at(off, b, sizeof(b)))
        return false;
    v = little ? static_cast<std::uint16_t>(b[0] | (b[1] << 8))
               : static_cast<std::uint16_t>((b[0] << 8) | b[1]);
    return true;
}

bool tiff_read_u32(TiffReader &r, std::uint64_t off, bool little, std::uint32_t &v) {
    unsigned char b[4];
    if (!r.read_at(off, b, sizeof(b)))
        return false;
    v = little
            ? (static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
               (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24))
            : ((static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
               (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]));
    return true;
}

bool tiff_read_u64(TiffReader &r, std::uint64_t off, bool little, std::uint64_t &v) {
    unsigned char b[8];
    if (!r.read_at(off, b, sizeof(b)))
        return false;
    v = 0;
    if (little) {
        for (int i = 7; i >= 0; --i)
            v = (v << 8) | b[i];
    } else {
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | b[i];
    }
    return true;
}

// 读取一个 IFD entry 的数值数组；支持 SHORT/LONG/LONG8。返回 false = 越界/类型不支持。
// `ref` 把字节序/类型/count/两个 value 字段打包，避免 6 个整型相邻参数被误换位。
struct TiffValueRef {
    bool little = false;
    bool big = false;
    std::uint16_t type = 0;
    std::uint64_t count = 0;
    std::uint64_t value_or_off = 0;
    std::uint64_t value_field_off = 0;
};

bool tiff_read_values(TiffReader &r, const TiffValueRef &ref, std::vector<std::uint64_t> &out) {
    const std::uint64_t unit = (ref.type == 3)                     ? 2
                               : (ref.type == 4 || ref.type == 13) ? 4
                               : (ref.type == 16)                  ? 8
                                                                   : 0;
    if (unit == 0 || ref.count == 0 || ref.count > 1000000)
        return false;
    const std::uint64_t bytes = unit * ref.count;
    const std::uint64_t inline_max = ref.big ? 8 : 4;
    // count*unit <= inline_max 时数值内联在 entry 的 value 字段里；否则 value 是文件偏移。
    const std::uint64_t off = (bytes <= inline_max) ? ref.value_field_off : ref.value_or_off;
    if (bytes > std::numeric_limits<std::size_t>::max())
        return false;
    std::vector<unsigned char> buf(static_cast<std::size_t>(bytes));
    if (!r.read_at(off, buf.data(), buf.size()))
        return false;
    out.clear();
    out.reserve(static_cast<std::size_t>(ref.count));
    for (std::uint64_t i = 0; i < ref.count; ++i) {
        std::uint64_t v = 0;
        const unsigned char *p = buf.data() + static_cast<std::size_t>(i * unit);
        if (ref.type == 3) {
            v = ref.little ? static_cast<std::uint64_t>(p[0] | (p[1] << 8))
                           : static_cast<std::uint64_t>((p[0] << 8) | p[1]);
        } else if (ref.type == 4 || ref.type == 13) {
            v = ref.little
                    ? (static_cast<std::uint64_t>(p[0]) | (static_cast<std::uint64_t>(p[1]) << 8) |
                       (static_cast<std::uint64_t>(p[2]) << 16) |
                       (static_cast<std::uint64_t>(p[3]) << 24))
                    : ((static_cast<std::uint64_t>(p[0]) << 24) |
                       (static_cast<std::uint64_t>(p[1]) << 16) |
                       (static_cast<std::uint64_t>(p[2]) << 8) | static_cast<std::uint64_t>(p[3]));
        } else {
            for (int k = 7; k >= 0; --k) {
                const int idx = ref.little ? k : 7 - k;
                v = (v << 8) | p[idx];
            }
        }
        out.push_back(v);
    }
    return true;
}

TiffProbe probe_tiff_structure(const std::filesystem::path &p) {
    TiffProbe out;
    TiffReader r(p);
    if (!r.open()) {
        out.error = "cannot open TIFF";
        return out;
    }
    unsigned char hdr[8];
    if (r.size() < 8 || !r.read_at(0, hdr, 8)) {
        out.error = "truncated TIFF header";
        return out;
    }
    bool little = false, big = false;
    if (hdr[0] == 'I' && hdr[1] == 'I')
        little = true;
    else if (hdr[0] == 'M' && hdr[1] == 'M')
        little = false;
    else {
        out.error = "not a TIFF (bad byte order)";
        return out;
    }
    const std::uint16_t magic = little ? static_cast<std::uint16_t>(hdr[2] | (hdr[3] << 8))
                                       : static_cast<std::uint16_t>((hdr[2] << 8) | hdr[3]);
    if (magic == 42)
        big = false;
    else if (magic == 43)
        big = true;
    else {
        out.error = "not a TIFF (bad magic)";
        return out;
    }
    std::uint64_t ifd = 0;
    if (big) {
        if (!tiff_read_u64(r, 8, little, ifd)) {
            out.error = "truncated BigTIFF header";
            return out;
        }
    } else {
        std::uint32_t ifd32 = 0;
        if (!tiff_read_u32(r, 4, little, ifd32)) {
            out.error = "truncated TIFF header";
            return out;
        }
        ifd = ifd32;
    }
    const std::uint16_t entry_size = big ? 20 : 12;
    std::uint64_t main_w = 0, main_h = 0;
    bool seen = false;
    while (ifd != 0 && out.dirs < 16) {
        if (ifd >= r.size()) {
            out.error = "IFD offset out of bounds";
            return out;
        }
        std::uint64_t n = 0;
        if (big) {
            if (!tiff_read_u64(r, ifd, little, n)) {
                out.error = "truncated IFD count";
                return out;
            }
        } else {
            // classic TIFF：IFD entry count 是 2 字节 SHORT；BigTIFF 才是 8 字节。
            std::uint16_t n16 = 0;
            if (!tiff_read_u16(r, ifd, little, n16)) {
                out.error = "truncated IFD count";
                return out;
            }
            n = n16;
        }
        if (n > 4096) {
            out.error = "IFD entry count unreasonable: ifd=" + std::to_string(ifd) +
                        " n=" + std::to_string(n) + " size=" + std::to_string(r.size());
            return out;
        }
        const std::uint64_t entries_off = ifd + (big ? 8 : 2);
        if (entries_off > r.size() || n * entry_size > r.size() - entries_off) {
            out.error = "IFD entries out of bounds";
            return out;
        }
        std::uint64_t next = 0;
        std::uint64_t dir_w = 0, dir_h = 0;
        std::uint64_t newsub = 0;
        bool has_newsub = false;
        std::vector<std::uint64_t> strip_off, strip_len, tile_off, tile_len, jpeg_off, jpeg_len;
        for (std::uint64_t i = 0; i < n; ++i) {
            const std::uint64_t e = entries_off + i * entry_size;
            unsigned char raw[20];
            if (!r.read_at(e, raw, static_cast<std::size_t>(entry_size))) {
                out.error = "truncated IFD entry";
                return out;
            }
            const std::uint16_t tag = static_cast<std::uint16_t>(little ? (raw[0] | (raw[1] << 8))
                                                                        : ((raw[0] << 8) | raw[1]));
            const std::uint16_t type = static_cast<std::uint16_t>(
                little ? (raw[2] | (raw[3] << 8)) : ((raw[2] << 8) | raw[3]));
            std::uint64_t count = 0, value = 0;
            if (big) {
                for (int k = 7; k >= 0; --k)
                    count = (count << 8) | raw[4 + (little ? k : 7 - k)];
                for (int k = 7; k >= 0; --k)
                    value = (value << 8) | raw[12 + (little ? k : 7 - k)];
            } else {
                for (int k = 3; k >= 0; --k)
                    count = (count << 8) | raw[4 + (little ? k : 3 - k)];
                for (int k = 3; k >= 0; --k)
                    value = (value << 8) | raw[8 + (little ? k : 3 - k)];
            }
            const std::uint64_t value_off = e + (big ? 12 : 8);
            if (tag == 0x0100 || tag == 0x0101) {
                std::vector<std::uint64_t> v;
                if (tiff_read_values(r, {little, big, type, count, value, value_off}, v) &&
                    !v.empty()) {
                    (tag == 0x0100 ? dir_w : dir_h) = v[0];
                }
            } else if (tag == 0x00FE) {
                std::vector<std::uint64_t> v;
                if (tiff_read_values(r, {little, big, type, count, value, value_off}, v) &&
                    !v.empty()) {
                    newsub = v[0];
                    has_newsub = true;
                }
            } else if (tag == 0x0111 || tag == 0x0117 || tag == 0x0144 || tag == 0x0145 ||
                       tag == 0x0201 || tag == 0x0202) {
                std::vector<std::uint64_t> v;
                if (!tiff_read_values(r, {little, big, type, count, value, value_off}, v)) {
                    out.offsets_in_bounds = false;
                    out.error = "bad array value in IFD";
                    return out;
                }
                if (tag == 0x0111)
                    strip_off = std::move(v);
                else if (tag == 0x0117)
                    strip_len = std::move(v);
                else if (tag == 0x0144)
                    tile_off = std::move(v);
                else if (tag == 0x0145)
                    tile_len = std::move(v);
                else if (tag == 0x0201)
                    jpeg_off = std::move(v);
                else
                    jpeg_len = std::move(v);
            } else if (tag == 0x014A) {
                std::vector<std::uint64_t> v;
                if (tiff_read_values(r, {little, big, type, count, value, value_off}, v))
                    out.sub_ifds += static_cast<int>(v.size());
            }
            // 通用越界：entry 的 value/count 字段本身已由 entry_size 校验；数据数组在
            // 上面按 tag 读时校验。ExifIFD/GPSIFD 只需在文件内，不在本探针计数。
            if (tag == 0x8769 || tag == 0x8825) {
                if (value >= r.size()) {
                    out.offsets_in_bounds = false;
                    out.error = "Exif/GPS IFD offset out of bounds";
                    return out;
                }
            }
        }
        const auto check_pairs = [&](const std::vector<std::uint64_t> &offs,
                                     const std::vector<std::uint64_t> &lens) {
            for (std::size_t k = 0; k < offs.size(); ++k) {
                const std::uint64_t len = (k < lens.size()) ? lens[k] : 0;
                if (len == 0)
                    continue;
                if (offs[k] == 0 || offs[k] > r.size() || len > r.size() - offs[k]) {
                    out.offsets_in_bounds = false;
                    return false;
                }
            }
            return true;
        };
        if (!check_pairs(strip_off, strip_len) || !check_pairs(tile_off, tile_len) ||
            !check_pairs(jpeg_off, jpeg_len)) {
            out.error = "image data offset out of bounds";
            return out;
        }
        if (out.dirs == 0) {
            main_w = dir_w;
            main_h = dir_h;
        } else if (out.dirs == 1) {
            const bool reduced = (has_newsub && (newsub & 1) != 0) ||
                                 (dir_w > 0 && dir_h > 0 && main_w > 0 && main_h > 0 &&
                                  dir_w < main_w && dir_h < main_h);
            out.ifd1_thumbnail = reduced;
        }
        seen = true;
        ++out.dirs;
        const std::uint64_t next_off = ifd + (big ? 8 : 2) + n * entry_size;
        if (big) {
            if (!tiff_read_u64(r, next_off, little, next)) {
                out.error = "truncated next-IFD pointer";
                return out;
            }
        } else {
            std::uint32_t next32 = 0;
            if (!tiff_read_u32(r, next_off, little, next32)) {
                out.error = "truncated next-IFD pointer";
                return out;
            }
            next = next32;
        }
        if (next != 0 && next <= ifd) {
            out.error = "IFD chain loop/backwards";
            return out;
        }
        ifd = next;
    }
    if (!seen) {
        out.error = "no IFD";
        return out;
    }
    out.ok = true;
    return out;
}

struct TiffExpect {
    int min_dirs = 0;
    int max_dirs = 0;
    bool transcode = false;
};

// 转码输出净化：多页页组 / SubIFD 删除；合法 reduced-resolution 缩略图保留整组。
// 仅对转码路径调用；metadata-only 必须保留原 IFD 链。
void sanitize_transcode_metadata_impl(MetadataPlan &plan) {
    Exiv2::ExifData &exif = plan.exif;
    const auto find_int = [&exif](const char *key) -> std::optional<int64_t> {
        const auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it == exif.end())
            return std::nullopt;
        try {
            return it->toInt64();
        } catch (...) {
            return std::nullopt;
        }
    };
    const std::optional<int64_t> mw = find_int("Exif.Image.ImageWidth");
    const std::optional<int64_t> mh = find_int("Exif.Image.ImageLength");
    const std::optional<int64_t> tw = find_int("Exif.Thumbnail.ImageWidth");
    const std::optional<int64_t> th = find_int("Exif.Thumbnail.ImageLength");
    const std::optional<int64_t> nsf = find_int("Exif.Thumbnail.NewSubfileType");
    const bool has_thumb_data =
        exif.findKey(Exiv2::ExifKey("Exif.Thumbnail.JPEGInterchangeFormat")) != exif.end() ||
        exif.findKey(Exiv2::ExifKey("Exif.Thumbnail.StripOffsets")) != exif.end();
    const bool reduced = (nsf.has_value() && (*nsf & 1) != 0) ||
                         (tw.has_value() && th.has_value() && mw.has_value() && mh.has_value() &&
                          *tw > 0 && *th > 0 && *mw > 0 && *mh > 0 && *tw < *mw && *th < *mh);
    const bool keep_thumbnail = has_thumb_data && reduced;

    const auto erase_prefix = [&exif](std::string_view prefix) {
        for (auto it = exif.begin(); it != exif.end();) {
            if (it->key().rfind(prefix, 0) == 0)
                it = exif.erase(it);
            else
                ++it;
        }
    };
    if (!keep_thumbnail)
        erase_prefix("Exif.Thumbnail.");
    // 多页/子图：Exiv2 的组名是 Image2/Image3（IFD2/IFD3）与 SubImage1..9（SubIFDs）。
    erase_prefix("Exif.Image2.");
    erase_prefix("Exif.Image3.");
    erase_prefix("Exif.SubImage");

    // IFD0 的源结构标签会覆盖新产物；删掉（缩略图的同名标签属于 Exif.Thumbnail，不受影响）。
    static constexpr std::string_view kStructural[] = {
        "Exif.Image.ImageWidth",
        "Exif.Image.ImageLength",
        "Exif.Image.BitsPerSample",
        "Exif.Image.Compression",
        "Exif.Image.PhotometricInterpretation",
        "Exif.Image.StripOffsets",
        "Exif.Image.RowsPerStrip",
        "Exif.Image.StripByteCounts",
        "Exif.Image.PlanarConfiguration",
        "Exif.Image.Predictor",
        "Exif.Image.TileWidth",
        "Exif.Image.TileLength",
        "Exif.Image.TileOffsets",
        "Exif.Image.TileByteCounts",
        "Exif.Image.SamplesPerPixel",
        "Exif.Image.ExtraSamples",
        "Exif.Image.SampleFormat",
        "Exif.Image.NewSubfileType",
        "Exif.Image.SubfileType",
        "Exif.Image.FillOrder",
        "Exif.Image.JPEGInterchangeFormat",
        "Exif.Image.JPEGInterchangeFormatLength",
        "Exif.Image.InterColorProfile",
    };
    for (const std::string_view k : kStructural) {
        const auto it = exif.findKey(Exiv2::ExifKey(std::string(k)));
        if (it != exif.end())
            exif.erase(it);
    }
    // TIFF-07：Exiv2 后写会用 plan.exif 重建 IFD0；若 plan 没有 Exif.Image.DateTime，
    // 编码器写进去的 tag 306 会被重写掉。这里从 DateTimeOriginal 补一份，保证可复现。
    if (exif.findKey(Exiv2::ExifKey("Exif.Image.DateTime")) == exif.end()) {
        const auto src = exif.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
        if (src != exif.end() && !src->toString().empty())
            exif["Exif.Image.DateTime"] = src->toString();
    }
}

// Exiv2 read/modify/write **in place on `file`**；空容器 => remove that metadata section。
// 注意：本函数可能截断重写（Exiv2 内部行为），调用方必须只把它用在**临时副本**上
// （R-002：写回一律 临时文件 + rename，见 exiv2_apply_atomic / rewrite_metadata_only）。
std::string exiv2_apply(const std::filesystem::path &file, const Exiv2::ExifData *exif,
                        const Exiv2::XmpData *xmp, bool clear_extra = false) {
    if (exif != nullptr)
        log_makernote(*exif, "MetaWrite");
    try {
        // R-007（M7-R2B）：Exiv2 open 入口经 normalize_extended_path 归一 `\\?\`（长路径可写；
        // 单源在 core/fsops，codecs 侧 fopen 调用点另行登记）。
        Exiv2::Image::UniquePtr img =
            Exiv2::ImageFactory::open(normalize_extended_path(file).string());
        if (!img)
            return "exiv2: cannot open " + file.string();
        img->readMetadata();
        if (clear_extra) {
            // 隐私剥除：EXIF/XMP 之外，IPTC（APP13）与 COM 也属于可识别内容。
            // 用 set-empty 而非 clear*；容器不支持 IPTC/COM 时 Exiv2 会抛异常，这里逐项吞掉
            // （该容器本来也没有可清的 IPTC/COM，不能因此让隐私剥除整体失败）。
            try {
                img->setIptcData(Exiv2::IptcData{});
            } catch (...) {
                (void)0; // 容器不支持 IPTC：本来无内容可清，隐私剥除继续
            }
            try {
                img->setComment("");
            } catch (...) {
                (void)0; // 容器不支持 COM：同上
            }
        }
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
    } catch (const Exiv2::Error &e) {
        return std::string("exiv2: ") + e.what();
    } catch (const std::exception &e) {
        return std::string("exiv2: ") + e.what();
    } catch (...) {
        return "exiv2: unknown error while writing " + file.string();
    }
    return {};
}

// R-002（P0，数据安全线）：Exiv2 写回 = 临时文件 + rename。Exiv2 的 writeMetadata() 内部
// 是截断重写 —— 直接作用最终文件时，磁盘满/杀进程/文件被占会把已编码好的输出截断成坏文件。
// 这里把改动落在同目录临时副本上，成功才 commit 覆盖；失败清场，目标文件保持原样。
std::string exiv2_apply_atomic(const std::filesystem::path &file, const Exiv2::ExifData *exif,
                               const Exiv2::XmpData *xmp, const TiffExpect *tiff_expect = nullptr,
                               bool clear_extra = false) {
    namespace fs = std::filesystem;
    const fs::path tmp = atomic_temp_path(file);
    if (tmp.empty())
        return "exiv2: cannot build temp name for " + file.string();
    std::error_code cp_ec;
    // R-007：fs 入口归一（超长路径的暂存拷贝同样可落盘）。
    fs::copy_file(normalize_extended_path(file), normalize_extended_path(tmp),
                  fs::copy_options::overwrite_existing, cp_ec);
    if (cp_ec) {
        discard_temp_file(tmp);
        return "exiv2: cannot stage " + file.string() + " -> " + tmp.string() + ": " +
               cp_ec.message();
    }
    const std::string err = exiv2_apply(tmp, exif, xmp, clear_extra);
    if (!err.empty()) {
        discard_temp_file(tmp);
        return err;
    }
    // 第二轮裁定：TIFF 多页/动画不支持、缩略图支持。元数据重写必须在 **commit 前** 验证
    // 产物 IFD 结构；否则坏文件已经覆盖了原本有效的编码产物。
    if (tiff_expect != nullptr) {
        const TiffProbe probe = probe_tiff_structure(tmp);
        bool bad = !probe.ok;
        std::string why = probe.error;
        if (!bad) {
            if (probe.dirs < tiff_expect->min_dirs || probe.dirs > tiff_expect->max_dirs) {
                bad = true;
                why = "TIFF directory count " + std::to_string(probe.dirs) + " outside [" +
                      std::to_string(tiff_expect->min_dirs) + "," +
                      std::to_string(tiff_expect->max_dirs) + "]";
            } else if (tiff_expect->transcode && probe.sub_ifds > 0) {
                bad = true;
                why = "TIFF output carries SubIFDs";
            } else if (tiff_expect->transcode && probe.dirs == 2 && !probe.ifd1_thumbnail) {
                bad = true;
                why = "TIFF IFD1 is not a reduced-resolution thumbnail";
            } else if (!probe.offsets_in_bounds) {
                bad = true;
                why = "TIFF image data offsets out of bounds";
            }
        }
        if (bad) {
            log_warn("MetaWrite", "metadata.cpp", "tiff probe rejected temp",
                     {{"tmp", tmp.string()}, {"why", why}});
            discard_temp_file(tmp);
            return "exiv2: tiff structure check failed: " + why;
        }
    }
    std::string commit_err;
    if (!commit_temp_file(tmp, file, &commit_err)) {
        discard_temp_file(tmp);
        return "exiv2: " + commit_err;
    }
    return {};
}

// R1 verification helper: are the mirror-worthy EXIF fields readable back from `file`?
bool key_exif_landed(const std::filesystem::path &file, const Exiv2::ExifData &want) {
    std::vector<std::string> expect;
    for (const char *k : kMirrorKeys) {
        if (want.findKey(Exiv2::ExifKey(k)) != want.end())
            expect.emplace_back(k);
    }
    if (expect.empty())
        return true;
    try {
        // R-007（M7-R2B）：Exiv2 open 入口经 normalize_extended_path 归一 `\\?\`（长路径可写；
        // 单源在 core/fsops，codecs 侧 fopen 调用点另行登记）。
        Exiv2::Image::UniquePtr img =
            Exiv2::ImageFactory::open(normalize_extended_path(file).string());
        if (!img)
            return false;
        img->readMetadata();
        const Exiv2::ExifData &got = img->exifData();
        for (const std::string &k : expect) {
            auto it = got.findKey(Exiv2::ExifKey(k));
            if (it == got.end() || strip_nul(it->toString()).empty())
                return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool has_any_time_field(const Exiv2::ExifData &exif, const Exiv2::XmpData &xmp) {
    for (const char *k : kExifTimeKeys) {
        if (exif.findKey(Exiv2::ExifKey(k)) != exif.end())
            return true;
    }
    for (const char *k : kXmpTimeKeys) {
        if (xmp.findKey(Exiv2::XmpKey(normalize_xmp_key(k))) != xmp.end())
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// §3.5 / §5.1 生效值预览的共用件（显示与写入**唯一**合成路径）
//   `build_plan()`（写路径）与 `preview_effective()`（显示）都只经这两个纯函数做时间/GPS
//   合成 —— 杜绝"显示/写入两张皮"（test_effective 逐字段对拍）。
// ---------------------------------------------------------------------------

// effective = 源 ⊕ BatchRules ⊕ 例外（例外逐项覆盖；ignore_batch 先清空批量规则）
BatchRules effective_rules(const BatchRules &rules, const std::optional<MetadataOverride> &ex) {
    BatchRules eff;
    const bool ignore_batch = ex.has_value() && ex->ignore_batch;
    if (!ignore_batch)
        eff = rules;
    // M7-WP1 / I5：各卡参与开关（UX-5）。该卡参与规则 = *_enabled && 原判据；缺省 true =
    // 既有行为逐字不变。开关只作用于**批量规则**（例外是单文件显式意志，不受批量卡开关影响）。
    if (!eff.time_enabled)
        eff.time_shift.reset();
    if (!eff.gps_enabled) {
        eff.gps.reset();
        eff.gps_clear = false;
    }
    if (!eff.privacy_enabled) {
        eff.strip_privacy = false;
        eff.modernize_exif = false; // 「补全现代 EXIF」是隐私/元数据卫生卡的可选能力
    }
    if (ex.has_value()) {
        if (ex->time_shift.has_value())
            eff.time_shift = ex->time_shift; // 覆盖（inherit = nullopt）
        if (ex->gps.has_value())
            eff.gps = ex->gps;
        if (ex->gps_clear)
            eff.gps_clear = true;
        // 例外编辑追加在批量编辑之后 → 同 key 的后者覆盖前者（"逐项覆盖"）
        eff.exif_edits.insert(eff.exif_edits.end(), ex->exif_edits.begin(), ex->exif_edits.end());
        eff.xmp_edits.insert(eff.xmp_edits.end(), ex->xmp_edits.begin(), ex->xmp_edits.end());
        if (ex->strip_privacy.has_value())
            eff.strip_privacy = *ex->strip_privacy; // 三态覆盖
    }
    return eff;
}

// 单个 EXIF 时间字段在 TimeShift 下的生效值；nullopt = "无变化"（值不可解析 —— 写入路径
// 在这些情形正是跳过写入，故两条路径给出一致结论）。Δ/时区语义都走既有
// shift_exif_datetime()（它在 TimezoneSemantic 下转发 reinterpret_timezone）。
std::optional<std::string> shifted_exif_time(const std::string &value, const TimeShift &s) {
    bool ok = false;
    const std::string next = shift_exif_datetime(value, s, ok);
    if (!ok)
        return std::nullopt;
    return next;
}

// probe 中某个 EXIF 时间标签的原值（缺失/空 → nullopt）
std::optional<DateTimeVal> exif_time_value(const Exiv2::ExifData &exif, const char *key) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end())
        return std::nullopt;
    const std::string v = strip_nul(it->toString());
    if (v.empty())
        return std::nullopt;
    return DateTimeVal{v};
}

// XMP 日期在 TimeShift 下的生效值（build_plan 的写路径与 preview_effective 的显示路径
// **共用**本纯函数 —— §3.5/§5.1 同源合成，杜绝显示/写入两张皮）。nullopt = 不可解析/越界
// （写入路径在这些情形跳过写入，两条路径同结论）。
std::optional<std::string> shifted_xmp_time(const std::string &value, const TimeShift &s) {
    XmpDate d;
    if (!parse_xmp_date(value, d))
        return std::nullopt;
    DateTime out;
    std::string fraction = d.fraction;
    std::string offset = d.offset;
    if (s.mode == TimeShift::Mode::Delta) {
        if (!apply_delta(d.dt, s, out))
            return std::nullopt;
    } else {
        // The value's own offset (when present) is authoritative; otherwise the caller's
        // `from` zone is the interpretation (§5.3).
        TimeShift delta;
        const int src_off = d.has_offset ? offset_minutes_of(d.offset) : s.from_offset_min;
        delta.minutes = s.to_offset_min - src_off;
        if (!apply_delta(d.dt, delta, out))
            return std::nullopt;
        if (d.has_offset)
            offset = offset_time_string(s.to_offset_min);
    }
    return format_iso_base(out) + fraction + offset;
}

std::string exif_string_value(const Exiv2::ExifData &exif, const char *key) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end())
        return {};
    return strip_nul(it->toString());
}

// XMP 时间标签的原值（缺失/空 → 空串；键形态经 normalize_xmp_key 归一）
std::string xmp_string_value(const Exiv2::XmpData &xmp, const char *key) {
    auto it = xmp.findKey(Exiv2::XmpKey(normalize_xmp_key(key)));
    if (it == xmp.end())
        return {};
    return strip_nul(it->toString());
}

bool rational_value(const Exiv2::ExifData &exif, const char *key, std::size_t n, double &out) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end())
        return false;
    try {
        const Exiv2::Rational r = it->value().toRational(n);
        if (r.second == 0)
            return false;
        out = static_cast<double>(r.first) / static_cast<double>(r.second);
        return true;
    } catch (...) {
        return false;
    }
}

// 3 × Rational → 十进制度（纬度/经度）
bool dms_value(const Exiv2::ExifData &exif, const char *key, double &degrees) {
    double d = 0.0, m = 0.0, s = 0.0;
    if (!rational_value(exif, key, 0, d) || !rational_value(exif, key, 1, m) ||
        !rational_value(exif, key, 2, s)) {
        return false;
    }
    degrees = d + m / 60.0 + s / 3600.0;
    return true;
}

// GPSTimeStamp 的 3 × Rational → "HH:MM:SS"
bool gps_time_of_day(const Exiv2::ExifData &exif, std::string &out) {
    double h = 0.0, m = 0.0, s = 0.0;
    if (!rational_value(exif, "Exif.GPSInfo.GPSTimeStamp", 0, h) ||
        !rational_value(exif, "Exif.GPSInfo.GPSTimeStamp", 1, m) ||
        !rational_value(exif, "Exif.GPSInfo.GPSTimeStamp", 2, s)) {
        return false;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", static_cast<int>(h), static_cast<int>(m),
                  static_cast<int>(s));
    out = buf;
    return true;
}

// probe 的 GPS 原值（Exif 侧读取口径与元数据页表单一致：纬度/经度缺一不可、半球由 *Ref 定号；
// 高度按 GPSAltitudeRef 定号；方向/时间戳按需带出）
std::optional<GpsData> gps_from_exif(const Exiv2::ExifData &exif) {
    double lat = 0.0, lon = 0.0;
    if (!dms_value(exif, "Exif.GPSInfo.GPSLatitude", lat) ||
        !dms_value(exif, "Exif.GPSInfo.GPSLongitude", lon)) {
        return std::nullopt;
    }
    if (exif_string_value(exif, "Exif.GPSInfo.GPSLatitudeRef") == "S")
        lat = -lat;
    if (exif_string_value(exif, "Exif.GPSInfo.GPSLongitudeRef") == "W")
        lon = -lon;

    GpsData g;
    g.lat = lat;
    g.lon = lon;
    double alt = 0.0;
    if (rational_value(exif, "Exif.GPSInfo.GPSAltitude", 0, alt))
        g.altitude = (exif_string_value(exif, "Exif.GPSInfo.GPSAltitudeRef") == "1") ? -alt : alt;
    double dir = 0.0;
    if (rational_value(exif, "Exif.GPSInfo.GPSImgDirection", 0, dir))
        g.direction = dir;
    const std::string date = exif_string_value(exif, "Exif.GPSInfo.GPSDateStamp");
    std::string tod;
    if (!date.empty() && gps_time_of_day(exif, tod))
        g.timestamp = date + " " + tod;
    return g;
}

// changed = (effective ≠ original)，逐字段严格相等：GpsData 无 operator==（PP-FROZEN 面），
// 故比较口径在本单点定义（§5.1 的 "≠" 逐字实现）。
bool gps_same(const std::optional<GpsData> &a, const std::optional<GpsData> &b) {
    if (a.has_value() != b.has_value())
        return false;
    if (!a.has_value())
        return true;
    return a->lat == b->lat && a->lon == b->lon && a->altitude == b->altitude &&
           a->direction == b->direction && a->timestamp == b->timestamp;
}

// Applies `s` to the EXIF time tags and the XMP dates that are actually present (§4.5).
// Returns the number of fields rewritten; `exif_rewritten` (optional) counts the EXIF side only
// — R-234 的 OffsetTime* 写入条件以它为准（"墙钟实际改写成功"）。
int apply_time_shift(Exiv2::ExifData &exif, Exiv2::XmpData &xmp, const TimeShift &s,
                     int *exif_rewritten = nullptr) {
    int touched = 0;
    if (exif_rewritten != nullptr)
        *exif_rewritten = 0;
    for (const char *k : kExifTimeKeys) {
        auto it = exif.findKey(Exiv2::ExifKey(k));
        if (it == exif.end())
            continue;
        const std::string cur = strip_nul(it->toString());
        // EXIF 侧走与 preview_effective() 共用的纯函数（§3.5/§5.1 同源合成逻辑）
        const std::optional<std::string> next = shifted_exif_time(cur, s);
        if (!next.has_value()) {
            log_warn("MetaWrite", "metadata.cpp", "time shift skipped for unparsable value");
            continue;
        }
        (void)it->setValue(*next);
        ++touched;
        if (exif_rewritten != nullptr)
            ++*exif_rewritten;
    }
    for (const char *k : kXmpTimeKeys) {
        const std::string key = normalize_xmp_key(k);
        auto it = xmp.findKey(Exiv2::XmpKey(key));
        if (it == xmp.end())
            continue;
        const std::string cur = strip_nul(it->toString());
        // XMP 侧同走 preview_effective 共用的纯函数（R-085：显示面与写入面同源）
        const std::optional<std::string> next = shifted_xmp_time(cur, s);
        if (!next.has_value()) {
            log_warn("MetaWrite", "metadata.cpp", "time shift skipped for unparsable xmp date");
            continue;
        }
        (void)it->setValue(*next);
        ++touched;
    }
    return touched;
}

} // namespace

// 第二轮裁定：转码元数据净化（public 面，内部实现留在匿名 namespace）。
void sanitize_transcode_metadata(MetadataPlan &plan) { sanitize_transcode_metadata_impl(plan); }

// R1 fallback helper (defined at the bottom of this file): mirror the EXIF fields that a
// container cannot carry into XMP. Outside the frozen interface; the unit test declares it too.
std::size_t detail_mirror_key_exif_to_xmp(const Exiv2::ExifData &exif, Exiv2::XmpData &xmp);

// Runtime metadata-only capability probe (defined next to write_metadata_exiv2). Outside the
// frozen interface: T8's format_supports_metadata_only() should delegate to it; the unit test
// declares it too.
bool detail_metadata_only_supported(std::string_view format_id);

// ===========================================================================
// pure helpers (§3.7 "可单测纯函数")
// ===========================================================================

bool TimeShift::is_noop() const {
    if (mode == Mode::TimezoneSemantic)
        return from_offset_min == to_offset_min;
    return years == 0 && months == 0 && days == 0 && hours == 0 && minutes == 0 && seconds == 0;
}

bool BatchRules::is_noop() const {
    if (time_shift && !time_shift->is_noop())
        return false;
    if (gps.has_value() || gps_clear)
        return false;
    if (!exif_edits.empty() || !xmp_edits.empty())
        return false;
    if (strip_privacy || sync_mtime)
        return false;
    if (modernize_exif)
        return false;
    return true;
}

std::string offset_time_string(int offset_min) {
    // EXIF 2.31 OffsetTime is "±HH:MM"; clamp to the representable range.
    constexpr int kMax = 23 * 60 + 59;
    const int v = std::clamp(offset_min, -kMax, kMax);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%c%02d:%02d", v < 0 ? '-' : '+', std::abs(v) / 60,
                  std::abs(v) % 60);
    return std::string(buf);
}

std::string shift_exif_datetime(const std::string &exif_dt, const TimeShift &s, bool &ok) {
    ok = false;
    if (s.mode == TimeShift::Mode::TimezoneSemantic) {
        return reinterpret_timezone(exif_dt, s.from_offset_min, s.to_offset_min, ok);
    }
    DateTime in;
    if (!parse_exif_datetime(exif_dt, in))
        return exif_dt;
    DateTime out;
    if (!apply_delta(in, s, out))
        return exif_dt;
    ok = true;
    return format_exif_datetime(out);
}

std::string reinterpret_timezone(const std::string &exif_dt, int from_min, int to_min, bool &ok) {
    ok = false;
    DateTime in;
    if (!parse_exif_datetime(exif_dt, in))
        return exif_dt;
    TimeShift delta;
    delta.minutes = to_min - from_min;
    DateTime out;
    if (!apply_delta(in, delta, out))
        return exif_dt;
    ok = true;
    return format_exif_datetime(out);
}

// ===========================================================================
// read
// ===========================================================================

namespace {

// M7-R2.4（HEIF/AVIF 元数据回读兜底）：Exiv2 的 BMFF 读取器读不到 **grid 主图**上的
// Exif/XMP（实测：lossless >16MP 的 HEIF 由 heif_context_add_grid_image 生成，Exif
// item 的 cdsc 指向 grid item；libheif 能读出 52644 字节 EXIF，Exiv2 的 exifData() 为空）。
// 后果不是"写没写"，而是**下一次转码读不回来** → 元数据在二次转码时静默丢失。
// 这里在 Exiv2 结果为空时按容器 API 直读同一份 Exif/XMP（不改写、不复制语义）。
void read_heif_metadata_fallback(SourceMeta &m, const std::string &file) {
    heif_context *ctx = heif_context_alloc();
    if (ctx == nullptr)
        return;
    heif_image_handle *handle = nullptr;
    const heif_error read = heif_context_read_from_file(ctx, file.c_str(), nullptr);
    if (read.code == heif_error_Ok &&
        heif_context_get_primary_image_handle(ctx, &handle).code == heif_error_Ok &&
        handle != nullptr) {
        const int n = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);
        if (n > 0 && n <= 64) {
            std::vector<heif_item_id> ids(static_cast<std::size_t>(n), 0);
            const int got =
                heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), n);
            for (int i = 0; i < got; ++i) {
                const char *type = heif_image_handle_get_metadata_type(handle, ids[i]);
                const std::size_t size = heif_image_handle_get_metadata_size(handle, ids[i]);
                if (type == nullptr || size == 0 || size > (16u << 20))
                    continue;
                std::vector<unsigned char> buf(size, 0);
                if (heif_image_handle_get_metadata(handle, ids[i], buf.data()).code !=
                    heif_error_Ok)
                    continue;
                const std::string type_s(type);
                if (type_s == "Exif" && m.exif.empty()) {
                    // HEIF Exif item = 4 字节大端 TIFF 偏移 + Exif 数据（libheif 头注释同口径）
                    std::size_t skip =
                        size >= 4 ? (std::size_t(buf[0]) << 24 | std::size_t(buf[1]) << 16 |
                                     std::size_t(buf[2]) << 8 | std::size_t(buf[3]))
                                  : 0;
                    if (4 + skip < size)
                        Exiv2::ExifParser::decode(m.exif, buf.data() + 4 + skip, size - 4 - skip);
                } else if (type_s == "mime" && m.xmp.empty()) {
                    const char *ctype = heif_image_handle_get_metadata_content_type(handle, ids[i]);
                    if (ctype == nullptr || std::string_view(ctype) != "application/rdf+xml")
                        continue;
                    const std::string packet(reinterpret_cast<const char *>(buf.data()), size);
                    if (Exiv2::XmpParser::decode(m.xmp, packet) != 0)
                        m.xmp.clear();
                }
            }
        }
    }
    if (handle != nullptr)
        heif_image_handle_release(handle);
    heif_context_free(ctx);
}

bool is_heif_family_ext(const std::string &ext) {
    return ext == ".heic" || ext == ".heif" || ext == ".hif" || ext == ".avif";
}

} // namespace

SourceMeta read_metadata(const std::filesystem::path &p) {
    SourceMeta m;
    try {
        // R-007（M7-R2B）：读入口同口径归一 `\\?\`（长路径源文件可读；与写入口同一单源）。
        Exiv2::Image::UniquePtr img =
            Exiv2::ImageFactory::open(normalize_extended_path(p).string());
        if (!img) {
            m.error = "exiv2: cannot open " + p.string();
            return m;
        }
        img->readMetadata();
        m.exif = img->exifData();
        m.xmp = img->xmpData();
        if (img->iccProfileDefined()) {
            const Exiv2::DataBuf &icc = img->iccProfile();
            if (icc.size() > 0)
                m.icc.assign(reinterpret_cast<const char *>(icc.c_data()), icc.size());
        }
        // M7-R2.4：BMFF（尤其 grid 主图）Exiv2 读不到的 Exif/XMP 由 libheif 容器 API 补齐。
        // 两路独立判空（Exiv2 可能只读到其中一路，例如读得到 XMP 但读不到 Exif）。
        if ((m.exif.empty() || m.xmp.empty()) &&
            is_heif_family_ext(lower_copy(p.extension().string()))) {
            read_heif_metadata_fallback(m, normalize_extended_path(p).string());
        }
    } catch (const Exiv2::Error &e) {
        m.error = std::string("exiv2: ") + e.what();
        return m;
    } catch (const std::exception &e) {
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

// 2026-10 可选功能「补全现代 EXIF」：只补**可由已有事实推导**的字段，绝不编造拍摄信息。
// 现代相机/手机（截至 2026）普遍写 ExifVersion 2.32 / PixelXDimension / GPSVersionID /
// GPSMapDatum / GPSAltitudeRef / GPSImgDirectionRef；老文件缺这些时可由尺寸或坐标基准补齐。
namespace {

void set_missing_text(Exiv2::ExifData &exif, const char *key, std::string_view value) {
    const Exiv2::ExifKey k(key);
    if (exif.findKey(k) == exif.end())
        exif[k.key()] = std::string(value);
}

bool read_positive_uint(const Exiv2::ExifData &exif, const char *key, std::uint32_t &out) {
    const auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end())
        return false;
    try {
        out = it->toUint32();
        return out > 0;
    } catch (const Exiv2::Error &) {
        return false;
    }
}

void complete_modern_exif(Exiv2::ExifData &exif) {
    // ExifVersion 2.32：本程序写的是标准 EXIF 标签集；仅在源文件完全没有该标签时补。
    // 该键类型 = UNDEFINED(4)，必须按 4 字节 undefined 写，不能按 ASCII（Exiv2 会吃掉前导 0）。
    const Exiv2::ExifKey version_key("Exif.Photo.ExifVersion");
    if (exif.findKey(version_key) == exif.end()) {
        Exiv2::Value::UniquePtr value = Exiv2::Value::create(Exiv2::undefined);
        const Exiv2::byte raw[4] = {'0', '2', '3', '2'};
        value->read(raw, 4, Exiv2::bigEndian);
        exif.add(version_key, value.get());
    }

    // 像素尺寸：现代 EXIF 用 PixelXDimension/PixelYDimension；旧文件常只有 ImageWidth/Length。
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    const bool has_w = read_positive_uint(exif, "Exif.Photo.PixelXDimension", w);
    const bool has_h = read_positive_uint(exif, "Exif.Photo.PixelYDimension", h);
    if (!has_w && read_positive_uint(exif, "Exif.Image.ImageWidth", w))
        exif["Exif.Photo.PixelXDimension"] = w;
    if (!has_h && read_positive_uint(exif, "Exif.Image.ImageLength", h))
        exif["Exif.Photo.PixelYDimension"] = h;
    // 反向补齐：老消费端只读 ImageWidth/Length 时也有尺寸（值来自已存在的 Pixel*，不是猜）。
    std::uint32_t pw = 0;
    std::uint32_t ph = 0;
    if (read_positive_uint(exif, "Exif.Photo.PixelXDimension", pw) &&
        !read_positive_uint(exif, "Exif.Image.ImageWidth", w))
        exif["Exif.Image.ImageWidth"] = pw;
    if (read_positive_uint(exif, "Exif.Photo.PixelYDimension", ph) &&
        !read_positive_uint(exif, "Exif.Image.ImageLength", h))
        exif["Exif.Image.ImageLength"] = ph;

    // GPS 元数据：有坐标才补基准；本程序公共坐标恒为 WGS-84。
    const bool has_gps = exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLatitude")) != exif.end() ||
                         exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSLongitude")) != exif.end();
    if (has_gps) {
        set_missing_text(exif, "Exif.GPSInfo.GPSVersionID", "2 2 0 0");
        set_missing_text(exif, "Exif.GPSInfo.GPSMapDatum", "WGS-84");
        const auto alt = exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSAltitude"));
        const auto alt_ref = exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSAltitudeRef"));
        if (alt != exif.end() && alt_ref == exif.end()) {
            try {
                exif["Exif.GPSInfo.GPSAltitudeRef"] = static_cast<std::uint16_t>(
                    alt->toFloat() < 0.0 ? 1 : 0); // 0=海平面以上，1=以下
            } catch (const Exiv2::Error &e) {
                log_debug("MetaWrite", "metadata.cpp",
                          "GPSAltitude unparsable; GPSAltitudeRef not synthesized",
                          {{"error", e.what()}});
            }
        }
        // 本程序/手机写入的 ImgDirection 为真北（T）；仅在方向存在时补基准。
        if (exif.findKey(Exiv2::ExifKey("Exif.GPSInfo.GPSImgDirection")) != exif.end())
            set_missing_text(exif, "Exif.GPSInfo.GPSImgDirectionRef", "T");
    }
}

} // namespace

MetadataPlan build_plan(const SourceMeta &src, const BatchRules &rules,
                        const std::optional<MetadataOverride> &ex) {
    MetadataPlan plan;
    plan.exif = src.exif;
    plan.xmp = src.xmp;
    if (!src.error.empty()) {
        log_warn("MetaRead", "metadata.cpp",
                 "source metadata unavailable; continuing with empty metadata");
    }

    // effective = 源 ⊕ BatchRules ⊕ 例外（例外逐项覆盖；ignore_batch 先清空批量规则）
    // 合成规则单源 = effective_rules()（与 preview_effective() 共用，§3.5/§5.1）
    const BatchRules eff = effective_rules(rules, ex);

    // 隐私剥除优先级最高：剥除后不得再写任何 EXIF/XMP 字段（§4.5）
    if (eff.strip_privacy) {
        strip_privacy(plan.exif, plan.xmp);
        plan.strip_privacy = true;
        plan.has_time = false;
        plan.datetime_original.clear();
        return plan;
    }

    if (eff.gps_clear) {
        clear_gps(plan.exif);
    } else if (eff.gps.has_value()) {
        // R-029：时间戳格式非法 → 保留既有 GPSDateStamp/GPSTimeStamp + Warning 进 plan。
        write_gps(plan.exif, *eff.gps, &plan.warnings);
    }

    if (eff.time_shift.has_value() && !eff.time_shift->is_noop()) {
        if (!has_any_time_field(plan.exif, plan.xmp)) {
            plan.warnings.push_back(
                Warning{WarningKind::TimeFieldMissing, "no time field found, time shift skipped"});
        } else {
            int exif_rewritten = 0;
            apply_time_shift(plan.exif, plan.xmp, *eff.time_shift, &exif_rewritten);
            if (eff.time_shift->mode == TimeShift::Mode::TimezoneSemantic) {
                // R-234：OffsetTime* 描述的是 EXIF 时间标签的时区 —— 只有**墙钟实际改写成功**
                // （至少一个 EXIF 时间标签被写入新值）才写 OffsetTime*；否则"墙钟未变 + 新
                // offset"会把拍摄时刻语义改错。
                if (exif_rewritten > 0) {
                    const std::string off = offset_time_string(eff.time_shift->to_offset_min);
                    for (const char *k : {"Exif.Photo.OffsetTime", "Exif.Photo.OffsetTimeOriginal",
                                          "Exif.Photo.OffsetTimeDigitized"}) {
                        plan.exif[k] = off;
                    }
                }
            }
        }
    }

    std::vector<std::string> edit_errors;
    apply_edits(plan.exif, plan.xmp, eff.exif_edits, eff.xmp_edits, edit_errors);
    for (const std::string &e : edit_errors) {
        // R-084：批量标签编辑被拒（非法 key/类型）升格 Warning —— 不能只进日志，
        // 否则用户以为已写入（plan.warnings → row.warnings 既有聚合链路，UI 可见）。
        log_warn("MetaWrite", "metadata.cpp", "tag edit rejected");
        log_debug("MetaWrite", "metadata.cpp", e.c_str());
        plan.warnings.push_back(Warning{WarningKind::MetadataDropped, "tag edit rejected: " + e});
    }

    plan.datetime_original = effective_datetime(plan.exif, plan.xmp);
    plan.has_time = !plan.datetime_original.empty();
    if (eff.modernize_exif)
        complete_modern_exif(plan.exif);
    return plan;
}

// ===========================================================================
// effective preview（§3.5 / §5.1；纯函数零副作用：与 build_plan 同源、只读 probe）
// ===========================================================================

EffectivePreview preview_effective(const SourceProbe &probe, const BatchRules &rules,
                                   const std::optional<MetadataOverride> &ex) {
    EffectivePreview out{};
    const BatchRules eff = effective_rules(rules, ex); // 与 build_plan 同一套合成逻辑
    out.strip_privacy = eff.strip_privacy;
    out.sync_mtime = eff.sync_mtime; // MetadataOverride 无该三态 → 恒 = rules.sync_mtime

    // —— 三个 EXIF 时间字段（§5.1：DateTimeOriginal / DateTimeDigitized / DateTime(ModifyDate)）——
    const std::optional<TimeShift> shift =
        (eff.time_shift.has_value() && !eff.time_shift->is_noop()) ? eff.time_shift : std::nullopt;
    EffectiveField<std::optional<DateTimeVal>> *const fields[3] = {
        &out.datetime_original, &out.datetime_digitized, &out.datetime_modify};
    int exif_shifted = 0; // R-234/R-085：OffsetTime* 的写入条件（墙钟实际改写成功）与写路径同判据
    for (std::size_t i = 0; i < 3; ++i) {
        EffectiveField<std::optional<DateTimeVal>> &f = *fields[i];
        f.original = exif_time_value(probe.exif, kExifTimeKeys[i]);
        f.effective = f.original;
        if (eff.strip_privacy) {
            // 剥除后不得再写任何 EXIF/XMP 字段（§4.5）→ 无生效值（"将被移除"由 UI 显示）
            f.effective = std::nullopt;
        } else if (shift.has_value() && f.original.has_value()) {
            const std::optional<std::string> next = shifted_exif_time(f.original->value, *shift);
            if (next.has_value()) {
                f.effective = DateTimeVal{*next}; // 不可解析 → 保持原值（与写路径同结论）
                ++exif_shifted;
            }
        }
        f.changed = (f.effective != f.original);
    }

    // —— R-085：XMP 日期（build_plan 一并改写 → 显示面补全，同源 shifted_xmp_time）——
    EffectiveField<std::optional<DateTimeVal>> *const xmp_fields[2] = {&out.xmp_create_date,
                                                                       &out.xmp_modify_date};
    for (std::size_t i = 0; i < 2; ++i) {
        EffectiveField<std::optional<DateTimeVal>> &f = *xmp_fields[i];
        const std::string cur = xmp_string_value(probe.xmp, kXmpTimeKeys[i]);
        f.original = cur.empty() ? std::nullopt : std::optional<DateTimeVal>{DateTimeVal{cur}};
        f.effective = f.original;
        if (eff.strip_privacy) {
            f.effective = std::nullopt;
        } else if (shift.has_value() && f.original.has_value()) {
            const std::optional<std::string> next = shifted_xmp_time(f.original->value, *shift);
            if (next.has_value())
                f.effective = DateTimeVal{*next};
        }
        f.changed = (f.effective != f.original);
    }

    // —— R-085/R-234：OffsetTime*（三键同值写入；条件 = 墙钟实际改写成功）——
    const auto offset_orig = [&probe](const char *key) -> std::optional<std::string> {
        const std::string v = exif_string_value(probe.exif, key);
        return v.empty() ? std::nullopt : std::optional<std::string>{v};
    };
    EffectiveField<std::optional<std::string>> *const off_fields[3] = {
        &out.offset_time, &out.offset_time_original, &out.offset_time_digitized};
    const char *const off_keys[3] = {"Exif.Photo.OffsetTime", "Exif.Photo.OffsetTimeOriginal",
                                     "Exif.Photo.OffsetTimeDigitized"};
    for (std::size_t i = 0; i < 3; ++i) {
        EffectiveField<std::optional<std::string>> &f = *off_fields[i];
        f.original = offset_orig(off_keys[i]);
        f.effective = f.original;
        if (eff.strip_privacy) {
            f.effective = std::nullopt; // 剥除 → 一并消失
        } else if (shift.has_value() && shift->mode == TimeShift::Mode::TimezoneSemantic &&
                   exif_shifted > 0) {
            f.effective = offset_time_string(shift->to_offset_min);
        }
        f.changed = (f.effective != f.original);
    }

    // —— GPS（§5.1）：规则未配置 → effective = original；剥除/gps_clear → nullopt；配置 → 规则值 ——
    out.gps.original = gps_from_exif(probe.exif);
    if (eff.strip_privacy || eff.gps_clear) {
        out.gps.effective = std::nullopt;
    } else if (eff.gps.has_value()) {
        // R-029：回显按写入口径同源 —— 规则时间戳非法时写侧保留源 GPSDateStamp/GPSTimeStamp，
        // 显示面就该显示被保留的源时间戳（而不是那条写不进去的字符串）。
        GpsData g = *eff.gps;
        if (g.timestamp.has_value() && !is_valid_gps_timestamp(*g.timestamp)) {
            g.timestamp = out.gps.original.has_value() ? out.gps.original->timestamp
                                                       : std::optional<std::string>{};
        }
        out.gps.effective = g;
    } else {
        out.gps.effective = out.gps.original;
    }
    out.gps.changed = !gps_same(out.gps.effective, out.gps.original);
    return out;
}

// ===========================================================================
// payloads (G1)
// ===========================================================================

Payloads make_payloads(const MetadataPlan &plan) {
    ensure_xmp();
    Payloads out;
    if (!plan.exif.empty()) {
        // ExifParser::encode() takes a mutable ExifData (non-intrusive writing may patch it);
        // the frozen plan is const, so encode from a copy.
        Exiv2::ExifData exif = plan.exif;
        Exiv2::Blob blob;
        Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, exif);
        out.exif_blob.assign(reinterpret_cast<const char *>(blob.data()), blob.size());
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

std::string write_metadata_exiv2(const std::filesystem::path &out_file, const MetadataPlan &plan,
                                 const Payloads &payloads, std::vector<std::string> *out_warnings) {
    (void)payloads; // 后写路径把容器直接交给 Exiv2；payloads 服务于 JXL/HEIF 注入路径（§3.8）
    const std::string ext = lower_copy(out_file.extension().string());
    const bool is_png = (ext == ".png");

    // R-282（M7-WP1，grep 复核结论）：BMP 分支**保留** —— 评审称其"不可达"只对 pipeline
    // 生产路径成立（那里 BMP 走 meta_path=="none" 的显式告警分支）；本函数是 §5.8 的公开
    // API 面，"BMP 丢元数据 + 不算失败"是其文档化契约且被 test_post_write_formats 钉住。
    // 删除 = 改冻结契约 + 减证据面 → 不删，登记裁定（handoff）。
    if (ext == ".bmp") {
        // BMP has no metadata container; §5.8 assigns the MetadataDropped warning to the pipeline.
        log_info("MetaWrite", "metadata.cpp", "bmp output carries no metadata container");
        return {};
    }
    if (plan.exif.empty() && plan.xmp.empty() && !plan.strip_privacy) {
        return {}; // nothing to write: the encoder output carries none
    }

    // R-002（P0）：写回改临时文件 + rename（exiv2_apply_atomic）。写失败 = 真失败：
    // 返回非空错误、目标文件保持编码产物原样（不再吞成 warning + 成功）。
    // 第二轮裁定：转码 TIFF 只允许 1 个 IFD，或 2 个 IFD 且 IFD1 是合法缩略图；
    // 多页/SubIFD/越界偏移必须在 commit 前拒绝。
    const bool is_tiff = (ext == ".tif" || ext == ".tiff");
    const TiffExpect tiff_expect{1, 2, true};
    std::string err = exiv2_apply_atomic(out_file, &plan.exif, &plan.xmp,
                                         is_tiff ? &tiff_expect : nullptr, plan.strip_privacy);
    if (err.empty() && (!is_png || key_exif_landed(out_file, plan.exif)))
        return {};

    if (is_png) {
        // R1 fallback, kept as a safety net: this branch is only reachable when the exiv2 build
        // has no PNG support (EXV_HAVE_LIBZ undefined → PNG is not a registered image type) or
        // when a PNG eXIf chunk write is silently dropped. With the current vcpkg exiv2 port
        // (features bmff,xmp,png → zlib) PNG is writable and the normal path above succeeds —
        // verified by the r1/png/* cases in tests/unit/test_metadata.cpp (R1 CLOSED).
        Exiv2::XmpData mirrored = plan.xmp;
        const std::size_t mirrored_n = detail_mirror_key_exif_to_xmp(plan.exif, mirrored);
        if (mirrored_n > 0) {
            const std::string err2 = exiv2_apply_atomic(out_file, nullptr, &mirrored);
            if (err2.empty()) {
                log_warn("MetaWrite", "metadata.cpp", "png exif mirrored to xmp");
                note_write_warning(out_warnings, "png exif mirrored to xmp");
                return {};
            }
            err = err2;
        }
    }

    // R-002（P0）：写失败不再降级成"保留像素 + warning + 成功"——坏/缺元数据的产物必须
    // 显式失败（ok=false），交由上层决定保留或重试。文案沿用既有 detail 形态（可检索）；
    // M2-T4 的显式出参通道照旧收到同一 detail（消息必须到达 out_warnings 的契约不变）。
    const std::string detail = is_png ? "png metadata dropped: " + err : "metadata dropped: " + err;
    log_error("MetaWrite", "metadata.cpp", detail.c_str());
    note_write_warning(out_warnings, detail);
    return detail;
}

// ---------------------------------------------------------------------------
// metadata-only capability (runtime probe; not part of the frozen interface)
// ---------------------------------------------------------------------------

bool detail_metadata_only_supported(std::string_view format_id) {
    // v1 policy (§5.5): only containers Exiv2 can rewrite losslessly are metadata-only capable.
    // The container itself is probed at runtime with Exiv2's own type detection, so a future
    // exiv2 build that loses a format handler (e.g. no zlib → no PNG) degrades automatically
    // instead of failing per file. T8's format_supports_metadata_only() should delegate here.
    //
    // R-285：探测结果按进程缓存（结果只取决于 exiv2 构建的类型表，运行期不可变）——
    // 原实现每次调用都重放 ImageFactory::getType（每文件/每输出都会走到），白烧 CPU。
    struct Sample {
        std::string_view id;
        const unsigned char *sig;
        std::size_t size;
    };
    // Structurally complete minimal headers: Exiv2's type detection reads beyond the bare magic
    // (chunk headers / IFD offset), so truncated magics are not enough.
    static const unsigned char kJpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46,
                                          0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01};
    static const unsigned char kPng[] = {// signature + full IHDR chunk (1x1 RGBA8)
                                         0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00,
                                         0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
                                         0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00,
                                         0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89};
    static const unsigned char kTiff[] = {// "II*\0", IFD at 8 with one ImageWidth entry
                                          0x49, 0x49, 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01,
                                          0x00, 0x00, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
                                          0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const unsigned char kWebp[] = {// RIFF/WEBP + minimal VP8X chunk
                                          0x52, 0x49, 0x46, 0x46, 0x16, 0x00, 0x00, 0x00, 0x57,
                                          0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x58, 0x0A, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const Sample kSamples[] = {{"jpeg", kJpeg, sizeof(kJpeg)},
                                      {"png", kPng, sizeof(kPng)},
                                      {"tiff", kTiff, sizeof(kTiff)},
                                      {"webp", kWebp, sizeof(kWebp)}};
    static const std::map<std::string, bool> kCache = [] {
        std::map<std::string, bool> m;
        for (const Sample &s : kSamples) {
            bool ok = false;
            try {
                ok = Exiv2::ImageFactory::getType(s.sig, s.size) != Exiv2::ImageType::none;
            } catch (...) {
                ok = false;
            }
            m.emplace(std::string(s.id), ok);
        }
        return m;
    }();
    const auto it = kCache.find(std::string(format_id));
    if (it == kCache.end())
        return false; // bmp/heif/avif/jxl and anything else: no lossless metadata rewrite in v1
    return it->second;
}

// The frozen predicate is declared in core/pipeline.h and owned by T8's pipeline.cpp. Until that
// lands this weak definition keeps callers/tests linkable; a strong definition in pipeline.cpp
// takes precedence (ELF weak-symbol semantics), so T8 stays the owner. Omitted on non-GNU
// toolchains so a missing T8 implementation surfaces as a link error instead of silently working.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) bool format_supports_metadata_only(std::string_view format_id) {
    return detail_metadata_only_supported(format_id);
}
#endif

// ===========================================================================
// metadata-only mode (same container, zero re-encode)
// ===========================================================================

std::string rewrite_metadata_only(const std::filesystem::path &src,
                                  const std::filesystem::path &out, const MetadataPlan &plan,
                                  const Payloads &payloads) {
    (void)payloads; // containers are handed to Exiv2 directly
    namespace fs = std::filesystem;
    // R-002/R-030：临时文件 + rename。Exiv2 的截断重写只作用在临时副本上；任一步失败
    // 清场并保持 out 原样（不存在就仍不存在 —— 也就没有"占名的坏副本"）。
    // 源文件永远只读（copy_file 只读 src，全程不写 src）。
    const fs::path tmp = atomic_temp_path(out);
    if (tmp.empty())
        return "cannot build temp name for " + out.string();
    std::error_code ec;
    fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        discard_temp_file(tmp);
        return "copy " + src.string() + " -> " + out.string() + " failed: " + ec.message();
    }
    const std::string err = exiv2_apply(tmp, &plan.exif, &plan.xmp, plan.strip_privacy);
    if (!err.empty()) {
        discard_temp_file(tmp);
        return err;
    }
    // 仅元数据：保留原 IFD 链（多页可保留）；断言目录数不变、偏移在界内。
    const std::string ext = lower_copy(out.extension().string());
    if (ext == ".tif" || ext == ".tiff") {
        const TiffProbe before = probe_tiff_structure(src);
        const TiffProbe after = probe_tiff_structure(tmp);
        if (!before.ok || !after.ok || after.dirs != before.dirs ||
            after.sub_ifds != before.sub_ifds || !after.offsets_in_bounds) {
            discard_temp_file(tmp);
            return "exiv2: metadata-only TIFF structure changed (" +
                   std::string(before.error.empty() ? "ok" : before.error) + " -> " +
                   std::string(after.error.empty() ? "ok" : after.error) + ")";
        }
    }
    std::string commit_err;
    if (!commit_temp_file(tmp, out, &commit_err)) {
        discard_temp_file(tmp);
        return commit_err;
    }
    return {};
}

// ===========================================================================
// mtime
// ===========================================================================

std::string sync_file_mtime(const std::filesystem::path &out, const std::string &exif_datetime) {
    if (exif_datetime.empty())
        return {};
    DateTime t;
    if (!parse_exif_datetime(exif_datetime, t))
        return "invalid exif datetime '" + exif_datetime + "'";
    std::tm tm{};
    tm.tm_year = t.year - 1900;
    tm.tm_mon = t.month - 1;
    tm.tm_mday = t.day;
    tm.tm_hour = t.hour;
    tm.tm_min = t.minute;
    tm.tm_sec = t.second;
    tm.tm_isdst = -1;
    const std::time_t secs = std::mktime(&tm); // local time, mirrors EXIF semantics
    if (secs == static_cast<std::time_t>(-1))
        return "mktime failed for '" + exif_datetime + "'";
    const auto sys = std::chrono::system_clock::from_time_t(secs);
    std::error_code ec;
    const auto mtime_ft = pp::file_time_from_sys<std::filesystem::file_time_type::clock>(sys);
    std::filesystem::last_write_time(out, mtime_ft, ec);
    if (ec)
        return "set mtime failed: " + ec.message();
    return {};
}

// ===========================================================================
// GPS
// ===========================================================================

bool is_valid_gps_timestamp(const std::string &s) {
    DateTime ts;
    return parse_exif_datetime(s, ts); // 与写侧同一判据（"YYYY:MM:DD HH:MM:SS"）
}

void write_gps(Exiv2::ExifData &exif, const GpsData &gps, std::vector<Warning> *out_warnings) {
    const double lat = std::clamp(std::isfinite(gps.lat) ? gps.lat : 0.0, -90.0, 90.0);
    const double lon = std::clamp(std::isfinite(gps.lon) ? gps.lon : 0.0, -180.0, 180.0);

    exif["Exif.GPSInfo.GPSVersionID"] = std::string("2 2 0 0");
    exif["Exif.GPSInfo.GPSLatitude"] = join_dms(dms_rationals(lat));
    exif["Exif.GPSInfo.GPSLatitudeRef"] = std::string(lat < 0 ? "S" : "N");
    exif["Exif.GPSInfo.GPSLongitude"] = join_dms(dms_rationals(lon));
    exif["Exif.GPSInfo.GPSLongitudeRef"] = std::string(lon < 0 ? "W" : "E");

    if (gps.altitude.has_value() && std::isfinite(*gps.altitude)) {
        const double alt = *gps.altitude;
        exif["Exif.GPSInfo.GPSAltitudeRef"] =
            std::string(alt < 0 ? "1" : "0"); // 0 = above sea level
        exif["Exif.GPSInfo.GPSAltitude"] = urational(std::llround(std::fabs(alt) * 1000.0), 1000);
    } else {
        // A GPS override replaces the whole block: stale optionals must not survive.
        erase_exif_key(exif, "Exif.GPSInfo.GPSAltitudeRef");
        erase_exif_key(exif, "Exif.GPSInfo.GPSAltitude");
    }

    if (gps.direction.has_value() && std::isfinite(*gps.direction)) {
        double dir = std::fmod(*gps.direction, 360.0);
        if (dir < 0)
            dir += 360.0;
        exif["Exif.GPSInfo.GPSImgDirectionRef"] = std::string("T"); // true north
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
    } else if (gps.timestamp.has_value()) {
        // R-029（数据安全线）：非 "YYYY:MM:DD HH:MM:SS" 的时间戳**不再擦除**既有
        // GPSDateStamp/GPSTimeStamp（原实现跳过写入却顺手清掉源时间戳 = 静默数据损失），
        // 改为保留源值 + 升格 Warning（写路径单通道 out_warnings，R-171）。
        log_warn("MetaWrite", "metadata.cpp",
                 "gps timestamp is not 'YYYY:MM:DD HH:MM:SS'; kept existing GPSDateStamp/"
                 "GPSTimeStamp");
        if (out_warnings != nullptr) {
            out_warnings->push_back(Warning{
                WarningKind::MetadataDropped,
                "gps timestamp is not 'YYYY:MM:DD HH:MM:SS'; existing GPSDateStamp/GPSTimeStamp "
                "kept unchanged"});
        }
    } else {
        // 规则未给时间戳：GPS 块整体替换语义（stale optionals 不得存活）→ 清掉旧时间戳。
        erase_exif_key(exif, "Exif.GPSInfo.GPSDateStamp");
        erase_exif_key(exif, "Exif.GPSInfo.GPSTimeStamp");
    }
}

void clear_gps(Exiv2::ExifData &exif) {
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

void strip_privacy(Exiv2::ExifData &exif, Exiv2::XmpData &xmp) {
    exif.clear();
    xmp.clear();
    // ICC and pixels are not metadata containers → untouched (they live in the callers'
    // SourceMeta::icc / the encoded image data).
}

// ===========================================================================
// tag edits
// ===========================================================================

void apply_edits(Exiv2::ExifData &exif, Exiv2::XmpData &xmp, const std::vector<TagEdit> &exif_edits,
                 const std::vector<TagEdit> &xmp_edits, std::vector<std::string> &errors) {
    for (const TagEdit &e : exif_edits) {
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
            Exiv2::ExifKey key(e.key); // throws for unknown groups/tags
            if (remove) {
                auto it = exif.findKey(key);
                if (it != exif.end())
                    exif.erase(it);
            } else if (auto it = exif.findKey(key); it != exif.end()) {
                // setValue() returns 0 on success; it clobbers the old value on failure, so keep
                // a clone to restore it (a rejected edit must not damage the source metadata).
                Exiv2::Value::UniquePtr previous = it->value().clone();
                if (it->setValue(*e.value) != 0) {
                    (void)it->setValue(previous.get());
                    errors.push_back("exif set " + e.key + ": value does not match the tag type");
                }
            } else {
                Exiv2::Exifdatum &fresh = exif[e.key];
                if (fresh.setValue(*e.value) != 0) {
                    auto it = exif.findKey(key);
                    if (it != exif.end())
                        exif.erase(it);
                    errors.push_back("exif set " + e.key + ": value does not match the tag type");
                }
            }
        } catch (const Exiv2::Error &err) {
            errors.push_back("exif " + std::string(remove ? "remove " : "set ") + e.key + ": " +
                             err.what());
        } catch (const std::exception &err) {
            errors.push_back("exif " + std::string(remove ? "remove " : "set ") + e.key + ": " +
                             err.what());
        }
    }

    for (const TagEdit &e : xmp_edits) {
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
            Exiv2::XmpKey xkey(key); // throws for unknown namespaces
            if (remove) {
                auto it = xmp.findKey(xkey);
                if (it != xmp.end())
                    xmp.erase(it);
            } else if (auto it = xmp.findKey(xkey); it != xmp.end()) {
                Exiv2::Value::UniquePtr previous = it->value().clone();
                // LangAlt properties (dc:title, dc:description, …) accept a plain string and
                // become the x-default entry (G15).
                if (it->setValue(*e.value) != 0) {
                    (void)it->setValue(previous.get());
                    errors.push_back("xmp set " + e.key + ": value was rejected");
                }
            } else {
                Exiv2::Xmpdatum &fresh = xmp[key];
                if (fresh.setValue(*e.value) != 0) {
                    auto it = xmp.findKey(xkey);
                    if (it != xmp.end())
                        xmp.erase(it);
                    errors.push_back("xmp set " + e.key + ": value was rejected");
                }
            }
        } catch (const Exiv2::Error &err) {
            errors.push_back("xmp " + std::string(remove ? "remove " : "set ") + e.key + ": " +
                             err.what());
        } catch (const std::exception &err) {
            errors.push_back("xmp " + std::string(remove ? "remove " : "set ") + e.key + ": " +
                             err.what());
        }
    }
}

// ===========================================================================
// effective datetime
// ===========================================================================

std::string effective_datetime(const Exiv2::ExifData &exif, const Exiv2::XmpData &xmp) {
    for (const char *k : {"Exif.Photo.DateTimeOriginal", "Exif.Image.DateTime"}) {
        auto it = exif.findKey(Exiv2::ExifKey(k));
        if (it == exif.end())
            continue;
        const std::string v = strip_nul(it->toString());
        if (!v.empty())
            return v;
    }
    for (const char *k : {"Xmp.xmp.CreateDate", "Xmp.xmp:CreateDate"}) {
        auto it = xmp.findKey(Exiv2::XmpKey(normalize_xmp_key(k)));
        if (it == xmp.end())
            continue;
        const std::string v = strip_nul(it->toString());
        if (v.empty())
            continue;
        XmpDate d;
        if (parse_xmp_date(v, d))
            return format_exif_datetime(d.dt); // XMP ISO-8601 → EXIF form
        return v; // R-282：原"parse 成功返回 v / 失败也返回 v"两分支同果（死条件），收成一条
    }
    return {};
}

// R-204（M7-R2.1-F4）：机型单源访问器 —— Exif.Image.Model 首尾空白裁剪，缺失 = 空串。
// 裁剪口径与原 group_scan.cpp 手写段逐字符一致（" \t\r\n" 四字符集），行为零变化。
std::string camera_model(const SourceMeta &meta) {
    const auto it = meta.exif.findKey(Exiv2::ExifKey("Exif.Image.Model"));
    if (it == meta.exif.end())
        return {};
    std::string camera = it->toString();
    const std::size_t b = camera.find_first_not_of(" \t\r\n");
    const std::size_t e = camera.find_last_not_of(" \t\r\n");
    return (b == std::string::npos) ? std::string() : camera.substr(b, e - b + 1);
}

// ===========================================================================
// internal helpers that need external (test) visibility
// ===========================================================================

// XMP writes GPS as "DDD,MM.mmmRef" (decimal minutes); EXIF stores three rationals.
bool exif_dms_to_degrees(const std::string &dms, double &degrees) {
    double parts[3] = {0.0, 0.0, 0.0};
    std::size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        while (pos < dms.size() && dms[pos] == ' ')
            ++pos;
        const std::size_t slash = dms.find('/', pos);
        if (slash == std::string::npos)
            return false;
        std::size_t end = dms.find(' ', slash);
        if (end == std::string::npos)
            end = dms.size();
        const double num = std::strtod(dms.substr(pos, slash - pos).c_str(), nullptr);
        const double den = std::strtod(dms.substr(slash + 1, end - slash - 1).c_str(), nullptr);
        if (den == 0.0)
            return false;
        parts[i] = num / den;
        pos = end;
    }
    degrees = parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
    return true;
}

std::string xmp_gps_value(const std::string &ref, const std::string &dms) {
    double degrees = 0.0;
    if (!exif_dms_to_degrees(dms, degrees))
        return {};
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
std::size_t detail_mirror_key_exif_to_xmp(const Exiv2::ExifData &exif, Exiv2::XmpData &xmp) {
    std::size_t n = 0;
    const auto value_of = [&exif](const char *key) -> std::string {
        auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it == exif.end())
            return {};
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
            xmp["Xmp.exif.DateTimeOriginal"] = iso; // XMP dates are ISO-8601
            ++n;
        }
    }
    const std::string artist = value_of("Exif.Image.Artist");
    if (!artist.empty()) {
        xmp["Xmp.tiff.Artist"] = artist;
        ++n;
    }
    const std::string lat = xmp_gps_value(value_of("Exif.GPSInfo.GPSLatitudeRef"),
                                          value_of("Exif.GPSInfo.GPSLatitude"));
    if (!lat.empty()) {
        xmp["Xmp.exif.GPSLatitude"] = lat; // "31,13.824N"
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

} // namespace pp
