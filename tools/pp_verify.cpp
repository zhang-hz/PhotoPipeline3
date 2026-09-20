// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T8: pp_verify golden assertion tool (docs/m1-tasks.md §3.16 +
// tests/golden/SCHEMA.md).
//
//   pp_verify <expected.json> <actual_output> [--selftest]
//     pixel.mode      "exact" (bit-exact after decoding both sides) | "psnr" (+ threshold_db)
//     metadata[]      {key, op: eq|exists|absent, value} — values are rendered by the frozen
//                     normalisation of M2-T8 (see normalize_value_text below / SCHEMA.md)
//     warnings_contain[]  WarningKind names, read from the <actual_output>.pp.json sidecar
//                         written by `photopipeline --dev` (a decoded image cannot carry them)
//   output:  `VERIFY <case> OK|FAIL <detail>`; exit code = number of FAILs
//   --selftest: synthesises its own samples (PNG/JPEG/metadata/sidecar) in .cache/tmp and
//               checks that each assertion mode both passes and fails as expected.
//
// WebP outputs are decoded with libwebp, never with OIIO: OIIO's WebP reader returns
// premultiplied RGB for files with alpha (T6 landing ruling ⑤), which would break the
// bit-exact assertions of SCHEMA.md.

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <exiv2/exiv2.hpp>
#include <webp/decode.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* kUsage =
    "usage: pp_verify <expected.json> <actual_output> [--selftest]\n"
    "  asserts per tests/golden/SCHEMA.md: pixel (exact | psnr+threshold_db),\n"
    "  metadata [{key, op: eq|exists|absent, value}], warnings_contain [WarningKind]\n"
    "  --selftest   run the built-in three-state self test (no corpus needed)\n"
    "output: VERIFY <case> OK|FAIL <detail>; exit code = number of FAILs\n";

std::string lower_ext(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// ---------------------------------------------------------------------------
// image loading (normalised float samples)
// ---------------------------------------------------------------------------
struct Sample {
    int width = 0, height = 0, channels = 0, bitdepth = 8;
    std::vector<float> px;  // interleaved, 0..1
    std::string error;
};

Sample load_oiio(const fs::path& p) {
    Sample s;
    auto in = OIIO::ImageInput::open(p.string());
    if (!in) {
        s.error = "cannot open image: " + p.string() + " (" + OIIO::geterror() + ")";
        return s;
    }
    const OIIO::ImageSpec spec = in->spec();
    s.width = spec.width;
    s.height = spec.height;
    s.channels = spec.nchannels;
    s.bitdepth = static_cast<int>(spec.format.basesize()) * 8;
    if (s.width <= 0 || s.height <= 0 || s.channels <= 0) {
        s.error = "empty image: " + p.string();
        in->close();
        return s;
    }
    s.px.assign(static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.height) *
                    static_cast<std::size_t>(s.channels),
                0.0f);
    // chend must be > chbegin: the six-argument ImageBuf::read with chend == 0 segfaults on
    // OIIO 3.1.14 (T3 landing revision ①); ImageInput::read_image has the same constraint.
    const bool ok = in->read_image(0, 0, 0, s.channels, OIIO::TypeDesc::FLOAT, s.px.data());
    if (!ok) s.error = "cannot read pixels: " + p.string() + " (" + in->geterror() + ")";
    in->close();
    return s;
}

Sample load_webp(const fs::path& p) {
    Sample s;
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        s.error = "cannot open webp: " + p.string();
        return s;
    }
    const std::vector<char> raw((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    int w = 0, h = 0;
    if (!WebPGetInfo(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), &w, &h) || w <= 0 ||
        h <= 0) {
        s.error = "invalid webp: " + p.string();
        return s;
    }
    uint8_t* rgba = WebPDecodeRGBA(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), &w, &h);
    if (rgba == nullptr) {
        s.error = "WebPDecodeRGBA failed: " + p.string();
        return s;
    }
    s.width = w;
    s.height = h;
    s.channels = 4;
    s.bitdepth = 8;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    s.px.resize(n * 4);
    for (std::size_t i = 0; i < n * 4; ++i) {
        s.px[i] = static_cast<float>(rgba[i]) / 255.0f;
    }
    WebPFree(rgba);
    return s;
}

Sample load_sample(const fs::path& p) {
    if (lower_ext(p) == ".webp") return load_webp(p);
    return load_oiio(p);
}

bool same_geometry(const Sample& a, const Sample& b, std::string& detail) {
    if (a.width != b.width || a.height != b.height) {
        detail = "geometry mismatch: input " + std::to_string(a.width) + "x" +
                 std::to_string(a.height) + " vs output " + std::to_string(b.width) + "x" +
                 std::to_string(b.height);
        return false;
    }
    if (a.channels != b.channels) {
        detail = "channel mismatch: input " + std::to_string(a.channels) + " vs output " +
                 std::to_string(b.channels);
        return false;
    }
    return true;
}

// exact: both sides are quantised at 16-bit and compared sample by sample. Comparing at the
// widest supported depth keeps 8-bit → 16-bit containers (e.g. jxl/png at --bitdepth 16) exact,
// because the upscaled value (v * 257) is an exact rational multiple of the 8-bit code.
std::string compare_exact(const Sample& in, const Sample& out) {
    std::string detail;
    if (!same_geometry(in, out, detail)) return detail;
    const std::size_t n = in.px.size();
    std::size_t mismatches = 0;
    std::size_t first = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const long a = std::lround(std::clamp(in.px[i], 0.0f, 1.0f) * 65535.0f);
        const long b = std::lround(std::clamp(out.px[i], 0.0f, 1.0f) * 65535.0f);
        if (a != b) {
            if (mismatches == 0) first = i;
            ++mismatches;
        }
    }
    if (mismatches == 0) return {};
    const std::size_t ch = static_cast<std::size_t>(in.channels);
    const std::size_t pixel = first / ch;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "exact mismatch: %zu/%zu samples differ; first at pixel (%d,%d) channel %zu: "
                  "input %.6f vs output %.6f",
                  mismatches, n, static_cast<int>(pixel % in.width),
                  static_cast<int>(pixel / in.width), first % ch, in.px[first], out.px[first]);
    return buf;
}

std::string compare_psnr(const Sample& in, const Sample& out, double threshold_db) {
    std::string detail;
    if (!same_geometry(in, out, detail)) return detail;
    const std::size_t n = in.px.size();
    if (n == 0) return "empty sample set";
    double mse = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(in.px[i]) - static_cast<double>(out.px[i]);
        mse += d * d;
    }
    mse /= static_cast<double>(n);
    const double psnr = mse <= 0 ? std::numeric_limits<double>::infinity()
                                 : 10.0 * std::log10(1.0 / mse);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "psnr %.2f dB < threshold %.2f dB", psnr, threshold_db);
    if (psnr + 1e-9 < threshold_db) return buf;
    return {};
}

// ---------------------------------------------------------------------------
// metadata (Exiv2)
// ---------------------------------------------------------------------------
std::string xmp_key_normalized(const std::string& key) {
    std::string k = key;
    std::replace(k.begin(), k.end(), ':', '.');  // Xmp.xmp:CreateDate → Xmp.xmp.CreateDate
    return k;
}

// --- M2-T8 (#26 landed): stable text rendering of metadata values ------------
// expected.json compares *text*, so the rendering must be identical across runs, hosts and
// Exiv2 builds. Frozen rules (docs/m2-tasks.md §4 T8 ①):
//   * rationals            → lowest terms "num/den"; den == 1 → integer form
//   * ASCII                → trailing NULs and leading/trailing whitespace stripped
//   * arrays (> 1 element) → elements joined with ", "
//   * undefined (binary)   → "0x" + lowercase hex, truncated past 64 hex digits + "..."
//   * every other type     → Exiv2's own text (Exifdatum::print)
// The rules are deliberately applied per *element type*: a rational array is reduced element
// wise, an undefined value is hex-dumped, an ASCII value is trimmed, and any other multi-element
// value is joined. Scalar values of the remaining types keep Exiv2's rendering, which is where
// the human-readable forms ("2.3.0.0", "F2.8", "YYYY:MM:DD HH:MM:SS") come from.
bool ascii_blank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string trim_ascii_text(const std::string& in) {
    std::size_t end = in.size();
    while (end > 0 && in[end - 1] == '\0') --end;
    std::size_t begin = 0;
    while (begin < end && ascii_blank(in[begin])) ++begin;
    while (end > begin && ascii_blank(in[end - 1])) --end;
    return in.substr(begin, end - begin);
}

std::string reduce_rational(int64_t num, int64_t den) {
    // Degenerate guard: Exif allows 0/0 (e.g. unknown GPS values). No corpus instance (T8
    // report), rules do not define it — keep the raw pair instead of dividing by zero.
    if (den == 0) return std::to_string(num) + "/0";
    uint64_t a = num < 0 ? 0u - static_cast<uint64_t>(num) : static_cast<uint64_t>(num);
    uint64_t b = den < 0 ? 0u - static_cast<uint64_t>(den) : static_cast<uint64_t>(den);
    while (b != 0) {
        const uint64_t t = a % b;
        a = b;
        b = t;
    }
    const int64_t g = static_cast<int64_t>(a == 0 ? 1 : a);
    int64_t n = num / g;
    int64_t d = den / g;
    if (d < 0) {
        n = -n;
        d = -d;
    }
    if (d == 1) return std::to_string(n);
    return std::to_string(n) + "/" + std::to_string(d);
}

std::string binary_hex(const Exiv2::Value& v) {
    std::vector<Exiv2::byte> raw(v.size());
    if (!raw.empty()) v.copy(raw.data(), Exiv2::invalidByteOrder);
    static const char* kDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(raw.size() * 2);
    for (const Exiv2::byte b : raw) {
        hex += kDigits[b >> 4];
        hex += kDigits[b & 0x0f];
    }
    if (hex.size() > 64) hex = hex.substr(0, 64) + "...";
    return "0x" + hex;
}

// Value::count() means "number of elements" only for the numeric types and the XMP collection
// types; for the string-like types (asciiString/string/comment/xmpText/date/time) it is the
// *byte length*, so a 16-character XMP text must not be mistaken for a 16-element array (found
// while probing Xmp.xmp.CreatorTool; locked by the normalize-xmp-* selftest probes).
bool has_element_count(Exiv2::TypeId t) {
    switch (t) {
        case Exiv2::unsignedByte:
        case Exiv2::unsignedShort:
        case Exiv2::unsignedLong:
        case Exiv2::unsignedRational:
        case Exiv2::signedByte:
        case Exiv2::signedShort:
        case Exiv2::signedLong:
        case Exiv2::signedRational:
        case Exiv2::tiffFloat:
        case Exiv2::tiffDouble:
        case Exiv2::tiffIfd:
        case Exiv2::unsignedLongLong:
        case Exiv2::signedLongLong:
        case Exiv2::tiffIfd8:
        case Exiv2::xmpAlt:
        case Exiv2::xmpBag:
        case Exiv2::xmpSeq:
        case Exiv2::langAlt:
            return true;
        default:
            return false;
    }
}

std::string scalar_text(const Exiv2::Metadatum& d, Exiv2::ExifData* exif) {
    std::string printed = d.print(exif);
    if (printed.empty()) printed = d.toString();
    return printed;
}

std::string normalize_value_text(const Exiv2::Metadatum& d, Exiv2::ExifData* exif) {
    const Exiv2::Value& v = d.value();
    switch (d.typeId()) {
        case Exiv2::unsignedRational:
        case Exiv2::signedRational: {
            std::string out;
            for (std::size_t i = 0; i < v.count(); ++i) {
                const Exiv2::Rational r = v.toRational(i);
                if (!out.empty()) out += ", ";
                out += reduce_rational(r.first, r.second);
            }
            return out;
        }
        case Exiv2::asciiString:
            return trim_ascii_text(d.toString());
        case Exiv2::undefined:
            return binary_hex(v);
        default:
            break;
    }
    if (has_element_count(d.typeId()) && v.count() > 1) {  // arrays: elements joined with ", "
        std::string out;
        try {
            for (std::size_t i = 0; i < v.count(); ++i) {
                if (!out.empty()) out += ", ";
                out += v.toString(i);
            }
            return out;
        } catch (const std::exception&) {
            return scalar_text(d, exif);  // a rendering quirk must not become a hard error
        }
    }
    return scalar_text(d, exif);
}

bool metadata_lookup(Exiv2::ExifData& exif, Exiv2::XmpData& xmp, const std::string& key,
                     bool& present, std::string& value, std::string& err) {
    present = false;
    value.clear();
    try {
        if (key.rfind("Xmp.", 0) == 0) {
            const Exiv2::XmpKey xk(xmp_key_normalized(key));
            const auto it = xmp.findKey(xk);
            if (it == xmp.end()) return true;
            present = true;
            value = normalize_value_text(*it, nullptr);
            return true;
        }
        const Exiv2::ExifKey ek(key);
        const auto it = exif.findKey(ek);
        if (it == exif.end()) return true;
        present = true;
        value = normalize_value_text(*it, &exif);
        return true;
    } catch (const std::exception& e) {
        err = std::string("metadata key '") + key + "': " + e.what();
        return false;
    }
}

std::string check_metadata(const fs::path& actual, const QJsonArray& asserts) {
    if (asserts.isEmpty()) return {};
    Exiv2::Image::UniquePtr img;
    try {
        img = Exiv2::ImageFactory::open(actual.string());
        if (!img) return "cannot open output for metadata: " + actual.string();
        img->readMetadata();
    } catch (const std::exception& e) {
        return std::string("metadata read failed: ") + e.what();
    }
    Exiv2::ExifData exif = img->exifData();
    Exiv2::XmpData xmp = img->xmpData();

    std::string failures;
    for (const QJsonValue& v : asserts) {
        const QJsonObject a = v.toObject();
        const std::string key = a.value("key").toString().toStdString();
        const std::string op = a.value("op").toString("eq").toStdString();
        const std::string want = a.value("value").toString().toStdString();
        bool present = false;
        std::string got, err;
        if (!metadata_lookup(exif, xmp, key, present, got, err)) return err;
        auto add_failure = [&failures](const std::string& text) {
            if (!failures.empty()) failures += "; ";
            failures += text;
        };
        if (op == "eq") {
            if (!present) {
                add_failure("metadata '" + key + "' missing (expected '" + want + "')");
            } else if (got != want) {
                add_failure("metadata '" + key + "' = '" + got + "' != '" + want + "'");
            }
        } else if (op == "exists") {
            if (!present) add_failure("metadata '" + key + "' missing (expected to exist)");
        } else if (op == "absent") {
            if (present) add_failure("metadata '" + key + "' present ('" + got + "')");
        } else {
            add_failure("unknown metadata op '" + op + "'");
        }
    }
    return failures;
}

// ---------------------------------------------------------------------------
// warnings (sidecar written by `photopipeline --dev`)
// ---------------------------------------------------------------------------
std::string check_warnings(const fs::path& actual, const QJsonArray& asserts) {
    if (asserts.isEmpty()) return {};
    const fs::path sidecar = actual.string() + ".pp.json";
    std::ifstream f(sidecar, std::ios::binary);
    if (!f) return "warnings_contain requires the sidecar " + sidecar.string() + " (missing)";
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const QJsonObject obj = QJsonDocument::fromJson(QByteArray::fromStdString(text)).object();
    if (obj.isEmpty()) return "sidecar " + sidecar.string() + " is not valid JSON";
    std::vector<std::string> kinds;
    for (const QJsonValue& w : obj.value("warnings").toArray()) {
        if (w.isString()) {
            kinds.push_back(w.toString().toStdString());
        } else if (w.isObject()) {
            kinds.push_back(w.toObject().value("kind").toString().toStdString());
        }
    }
    std::string failures;
    for (const QJsonValue& v : asserts) {
        const std::string want = v.toString().toStdString();
        const bool found = std::find(kinds.begin(), kinds.end(), want) != kinds.end();
        if (!found) {
            std::string have;
            for (const std::string& k : kinds) {
                if (!have.empty()) have += ",";
                have += k;
            }
            if (!failures.empty()) failures += "; ";
            failures += "warning '" + want + "' not reported (have: [" + have + "])";
        }
    }
    return failures;
}

// ---------------------------------------------------------------------------
// one case
// ---------------------------------------------------------------------------
fs::path golden_root_for(const fs::path& expected_json) {
    // expected.json lives in <golden>/smoke/, so <golden> = parent(parent)
    std::error_code ec;
    const fs::path dir = fs::absolute(expected_json, ec).parent_path();
    if (!dir.empty()) {
        const fs::path candidate = dir.parent_path();
        if (fs::is_directory(candidate, ec)) return candidate;
    }
    return {};
}

bool check_case(const QJsonObject& exp, const fs::path& actual, const fs::path& golden_root,
                std::string& detail) {
    std::vector<std::string> failures;

    const QJsonObject asserts = exp.value("assert").toObject();
    const QJsonObject pixel = asserts.value("pixel").toObject();
    if (!pixel.isEmpty()) {
        const std::string mode = pixel.value("mode").toString().toStdString();
        std::string input_rel = exp.value("input").toString().toStdString();
        fs::path input_path(input_rel);
        if (input_path.is_relative() && !golden_root.empty()) input_path = golden_root / input_path;
        const Sample in = load_sample(input_path);
        if (!in.error.empty()) {
            failures.push_back("input: " + in.error);
        } else {
            const Sample out = load_sample(actual);
            if (!out.error.empty()) {
                failures.push_back("output: " + out.error);
            } else if (mode == "exact") {
                const std::string e = compare_exact(in, out);
                if (!e.empty()) failures.push_back(e);
            } else if (mode == "psnr") {
                const double threshold = pixel.value("threshold_db").toDouble(40.0);
                const std::string e = compare_psnr(in, out, threshold);
                if (!e.empty()) failures.push_back(e);
            } else {
                failures.push_back("unknown pixel.mode '" + mode + "'");
            }
        }
    }

    const std::string meta_err = check_metadata(actual, asserts.value("metadata").toArray());
    if (!meta_err.empty()) failures.push_back(meta_err);

    const std::string warn_err = check_warnings(actual, asserts.value("warnings_contain").toArray());
    if (!warn_err.empty()) failures.push_back(warn_err);

    if (failures.empty()) {
        detail.clear();
        return true;
    }
    detail.clear();
    for (const std::string& f : failures) {
        if (!detail.empty()) detail += "; ";
        detail += f;
    }
    return false;
}

// ---------------------------------------------------------------------------
// --selftest
// ---------------------------------------------------------------------------
bool write_image(const fs::path& p, int w, int h,
                 const std::vector<unsigned char>& rgb, std::string& err,
                 bool jpeg = false) {
    auto out = OIIO::ImageOutput::create(p.string());
    if (!out) {
        err = "no OIIO output plugin for " + p.string();
        return false;
    }
    OIIO::ImageSpec spec(w, h, 3, OIIO::TypeDesc::UINT8);
    if (jpeg) spec.attribute("quality", 95);
    if (!out->open(p.string(), spec)) {
        err = "open failed: " + out->geterror();
        return false;
    }
    if (!out->write_image(OIIO::TypeDesc::UINT8, rgb.data())) {
        err = "write failed: " + out->geterror();
        out->close();
        return false;
    }
    out->close();
    return true;
}

// Smooth bilinear gradient: keeps the selftest PSNR probe meaningful for a lossy codec
// (a hard-edged checker at 16x16 is dominated by JPEG ringing).
std::vector<unsigned char> pattern(int w, int h) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
            px[i] = static_cast<unsigned char>((x * 235) / (w - 1));
            px[i + 1] = static_cast<unsigned char>((y * 235) / (h - 1));
            px[i + 2] = static_cast<unsigned char>(((x + y) * 235) / (w + h - 2));
        }
    }
    return px;
}

int selftest() {
    std::error_code ec;
    const fs::path dir = fs::current_path(ec) / ".cache" / "tmp" / "pp_verify_selftest";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (ec) {
        std::printf("VERIFY selftest FAIL cannot create %s: %s\n", dir.string().c_str(),
                    ec.message().c_str());
        return 1;
    }
    const int w = 16, h = 16;
    const std::vector<unsigned char> base = pattern(w, h);
    const fs::path a_png = dir / "a.png";
    const fs::path b_png = dir / "b.png";
    const fs::path c_png = dir / "c.png";
    const fs::path inv_png = dir / "inverted.png";
    const fs::path a_jpg = dir / "a.jpg";
    const fs::path m_png = dir / "meta.png";
    std::string err;
    std::vector<unsigned char> perturbed = base;
    perturbed[(5 * w + 5) * 3] = static_cast<unsigned char>(perturbed[(5 * w + 5) * 3] ^ 0x40);
    std::vector<unsigned char> inverted = base;
    for (unsigned char& v : inverted) v = static_cast<unsigned char>(255 - v);
    if (!write_image(a_png, w, h, base, err) || !write_image(b_png, w, h, base, err) ||
        !write_image(c_png, w, h, perturbed, err) || !write_image(inv_png, w, h, inverted, err) ||
        !write_image(a_jpg, w, h, base, err, true) || !write_image(m_png, w, h, base, err)) {
        std::printf("VERIFY selftest FAIL writing samples: %s\n", err.c_str());
        return 1;
    }

    // metadata sample: one tag per normalisation class of M2-T8 (#26) so that --selftest locks
    // the frozen rules (docs/m2-tasks.md §4 T8 ①) alongside the assertion modes.
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(m_png.string());
        img->readMetadata();
        Exiv2::ExifData exif = img->exifData();
        exif["Exif.Image.Artist"] = "M1-T8";              // asciiString, scalar
        exif["Exif.Image.ImageDescription"] = "  padded M2-T8  ";  // asciiString, trimmed
        exif["Exif.Photo.FNumber"] = "28/10";             // unsignedRational -> 14/5
        exif["Exif.Photo.BrightnessValue"] = "-6/4";      // signedRational -> -3/2
        exif["Exif.Image.XResolution"] = "300/1";         // unsignedRational, den == 1
        exif["Exif.Image.YCbCrSubSampling"] = "2 1";      // unsignedShort array
        auto undefined_value = [](const std::vector<Exiv2::byte>& bytes) {
            Exiv2::DataValue dv(Exiv2::undefined);
            dv.read(bytes.data(), bytes.size(), Exiv2::invalidByteOrder);
            return dv;
        };
        const Exiv2::DataValue version_bytes = undefined_value({'0', '2', '3', '2'});
        std::vector<Exiv2::byte> long_binary(40);         // undefined, > 64 hex digits
        for (std::size_t i = 0; i < long_binary.size(); ++i) {
            long_binary[i] = static_cast<Exiv2::byte>(i + 1);
        }
        const Exiv2::DataValue maker_bytes = undefined_value(long_binary);
        exif["Exif.Photo.ExifVersion"].setValue(&version_bytes);  // undefined, <= 64 hex digits
        exif["Exif.Photo.MakerNote"].setValue(&maker_bytes);
        img->setExifData(exif);
        Exiv2::XmpData xmp = img->xmpData();
        xmp["Xmp.xmp.CreatorTool"] = "PhotoPipeline-M2-T8";  // xmpText: count() is a byte length
        xmp["Xmp.dc.subject"] = "alpha";                     // xmpBag: ", " joined items
        xmp["Xmp.dc.subject"] = "beta";
        img->setXmpData(xmp);
        img->writeMetadata();
    } catch (const std::exception& e) {
        std::printf("VERIFY selftest FAIL writing metadata sample: %s\n", e.what());
        return 1;
    }

    // sidecar for the warnings assertion
    {
        const std::string json =
            "{\"warnings\":[{\"kind\":\"AlphaFlattened\",\"detail\":\"selftest\"}]}";
        std::ofstream f((b_png.string() + ".pp.json"), std::ios::binary | std::ios::trunc);
        f << json;
    }

    auto make_exp = [](const char* input, const char* mode, double threshold,
                       const QJsonArray& metadata, const QJsonArray& warnings) {
        QJsonObject pixel;
        if (mode != nullptr && *mode != '\0') {  // empty mode = no pixel assertion
            pixel["mode"] = mode;
            if (std::strcmp(mode, "psnr") == 0) pixel["threshold_db"] = threshold;
        }
        QJsonObject a;
        a["pixel"] = pixel;
        a["metadata"] = metadata;
        a["warnings_contain"] = warnings;
        QJsonObject e;
        e["case"] = "selftest";
        e["input"] = input;
        e["assert"] = a;
        return e;
    };
    auto meta_eq = [](const char* key, const char* value) {
        QJsonObject o;
        o["key"] = key;
        o["op"] = "eq";
        o["value"] = value;
        QJsonArray a;
        a.append(o);
        return a;
    };
    auto meta_op = [](const char* key, const char* op) {
        QJsonObject o;
        o["key"] = key;
        o["op"] = op;
        QJsonArray a;
        a.append(o);
        return a;
    };
    auto warns = [](const char* kind) {
        QJsonArray a;
        a.append(kind);
        return a;
    };

    struct Probe {
        const char* label;
        QJsonObject exp;
        fs::path actual;
        bool expect_ok;
    };
    const std::vector<Probe> probes = {
        {"exact-pass", make_exp("a.png", "exact", 0, {}, {}), b_png, true},
        {"exact-fail", make_exp("a.png", "exact", 0, {}, {}), c_png, false},
        {"psnr-pass", make_exp("a.png", "psnr", 30.0, {}, {}), a_jpg, true},
        {"psnr-fail", make_exp("a.png", "psnr", 30.0, {}, {}), inv_png, false},
        {"metadata-eq-pass", make_exp("a.png", "exact", 0, meta_eq("Exif.Image.Artist", "M1-T8"), {}),
         m_png, true},
        {"metadata-eq-fail", make_exp("a.png", "exact", 0, meta_eq("Exif.Image.Artist", "nope"), {}),
         m_png, false},
        {"metadata-exists-pass",
         make_exp("a.png", "exact", 0, meta_op("Exif.Image.Artist", "exists"), {}), m_png, true},
        {"metadata-absent-fail",
         make_exp("a.png", "exact", 0, meta_op("Exif.Image.Artist", "absent"), {}), m_png, false},
        {"metadata-absent-pass",
         make_exp("a.png", "exact", 0, meta_op("Exif.Image.Software", "absent"), {}), m_png, true},
        {"warnings-pass", make_exp("a.png", "exact", 0, {}, warns("AlphaFlattened")), b_png, true},
        {"warnings-fail", make_exp("a.png", "exact", 0, {}, warns("DepthDowngrade")), b_png, false},
        // M2-T8 (#26): one probe per frozen normalisation class.
        {"normalize-ascii-trim",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Image.ImageDescription", "padded M2-T8"), {}),
         m_png, true},
        {"normalize-rational-reduced",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Photo.FNumber", "14/5"), {}), m_png, true},
        {"normalize-signed-rational",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Photo.BrightnessValue", "-3/2"), {}),
         m_png, true},
        {"normalize-rational-den1",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Image.XResolution", "300"), {}), m_png, true},
        {"normalize-array-join",
         make_exp("a.png", nullptr, 0,
                  meta_eq("Exif.Image.YCbCrSubSampling", "2, 1"), {}),
         m_png, true},
        {"normalize-undefined-hex",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Photo.ExifVersion", "0x30323332"), {}),
         m_png, true},
        {"normalize-hex-truncated",
         make_exp("a.png", nullptr, 0,
                  meta_eq("Exif.Photo.MakerNote",
                          "0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20..."),
                  {}),
         m_png, true},
        {"normalize-rational-raw-text-rejected",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Photo.FNumber", "F2.8"), {}), m_png, false},
        // XMP: text values must stay scalar (count() is a byte length), collections join items.
        {"normalize-xmp-text-scalar",
         make_exp("a.png", nullptr, 0,
                  meta_eq("Xmp.xmp.CreatorTool", "PhotoPipeline-M2-T8"), {}),
         m_png, true},
        {"normalize-xmp-bag-join",
         make_exp("a.png", nullptr, 0, meta_eq("Xmp.dc.subject", "alpha, beta"), {}), m_png, true},
    };

    int failures = 0;
    for (const Probe& p : probes) {
        std::string detail;
        const bool ok = check_case(p.exp, p.actual, dir, detail);
        const bool behaves = (ok == p.expect_ok);
        if (!behaves) ++failures;
        std::printf("VERIFY selftest %-22s %s (expected %s, got %s)%s%s\n", p.label,
                    behaves ? "OK" : "FAIL", p.expect_ok ? "OK" : "FAIL", ok ? "OK" : "FAIL",
                    detail.empty() ? "" : " ", detail.c_str());
    }

    // M2-T8 (#26): direct probes of the frozen normalisation rules. The corpus cannot express
    // trailing NULs or den == 0, so the helpers are exercised here too.
    auto text_probe = [&failures](const char* label, const std::string& got, const char* want) {
        const bool ok = got == want;
        if (!ok) ++failures;
        std::printf("VERIFY selftest %-22s %s (got '%s', want '%s')\n", label, ok ? "OK" : "FAIL",
                    got.c_str(), want);
    };
    text_probe("normalize-ascii-nul-trim", trim_ascii_text(std::string("  padded\0\0", 10)),
               "padded");
    text_probe("normalize-rational-reduce", reduce_rational(28, 10), "14/5");
    text_probe("normalize-rational-int", reduce_rational(300, 1), "300");
    text_probe("normalize-rational-negative", reduce_rational(6, -4), "-3/2");
    text_probe("normalize-rational-zero", reduce_rational(0, 5), "0");
    text_probe("normalize-rational-den0-guard", reduce_rational(7, 0), "7/0");

    std::printf("VERIFY selftest %s (%zu probes, %d unexpected)\n", failures == 0 ? "OK" : "FAIL",
                probes.size() + 6, failures);
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0) return selftest();
    }
    if (argc < 3) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    const fs::path expected_json = argv[1];
    const fs::path actual = argv[2];

    std::ifstream f(expected_json, std::ios::binary);
    if (!f) {
        std::printf("VERIFY %s FAIL cannot open expected json %s\n", expected_json.string().c_str(),
                    expected_json.string().c_str());
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        std::printf("VERIFY %s FAIL invalid expected json: %s\n", expected_json.string().c_str(),
                    perr.errorString().toStdString().c_str());
        return 1;
    }
    const QJsonObject exp = doc.object();
    const QString case_qs = exp.value("case").toString(QString::fromStdString(expected_json.stem().string()));
    const std::string case_name = case_qs.toStdString();

    std::error_code ec;
    if (!fs::exists(actual, ec)) {
        std::printf("VERIFY %s FAIL actual output does not exist: %s\n", case_name.c_str(),
                    actual.string().c_str());
        return 1;
    }

    std::string detail;
    const bool ok = check_case(exp, actual, golden_root_for(expected_json), detail);
    if (ok) {
        std::printf("VERIFY %s OK\n", case_name.c_str());
        return 0;
    }
    std::printf("VERIFY %s FAIL %s\n", case_name.c_str(), detail.c_str());
    return 1;
}
