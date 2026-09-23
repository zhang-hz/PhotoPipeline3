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

const char *kUsage =
    "usage: pp_verify <expected.json> <actual_output|case_dir> [--selftest]\n"
    "  asserts per tests/golden/SCHEMA.md: pixel (exact | psnr+threshold_db),\n"
    "  metadata [{key, op: eq|exists|absent, value}], warnings_contain [WarningKind]\n"
    "  v2 (0.3.0): expected.json 携带 outputs[] 时，第二参数是**用例输出根目录**，\n"
    "  逐输出按 outputs[i].rel 定位文件并各自断言（路径断言 = 文件必须存在）\n"
    "  M4-T6: 携带 progress_trace 段时同样要求第二参数是用例输出根目录，\n"
    "  断言 <root>/progress-trace.jsonl 的 overall_frac 单调性 + synthetic 口径\n"
    "  --selftest   run the built-in three-state self test (no corpus needed)\n"
    "output: VERIFY <case> OK|FAIL <detail>; exit code = number of FAILs\n";

std::string lower_ext(const fs::path &p) {
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
    std::vector<float> px; // interleaved, 0..1
    std::string error;
};

Sample load_oiio(const fs::path &p) {
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
    if (!ok)
        s.error = "cannot read pixels: " + p.string() + " (" + in->geterror() + ")";
    in->close();
    return s;
}

Sample load_webp(const fs::path &p, bool rgb_only = false) {
    Sample s;
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        s.error = "cannot open webp: " + p.string();
        return s;
    }
    const std::vector<char> raw((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    int w = 0, h = 0;
    if (!WebPGetInfo(reinterpret_cast<const uint8_t *>(raw.data()), raw.size(), &w, &h) || w <= 0 ||
        h <= 0) {
        s.error = "invalid webp: " + p.string();
        return s;
    }
    // 通道对齐（M4-T5）：参照是 3 通道（无 alpha 源）时按 RGB 解码，否则按 RGBA。
    // 目的仅是消除 libwebp RGBA 扩张造成的**伪**通道不匹配；alpha 产物仍走 RGBA（M1 口径：
    // 绝不使用 OIIO 的 WebP reader，它对带 alpha 的文件返回预乘 RGB）。
    uint8_t *rgba =
        rgb_only
            ? WebPDecodeRGB(reinterpret_cast<const uint8_t *>(raw.data()), raw.size(), &w, &h)
            : WebPDecodeRGBA(reinterpret_cast<const uint8_t *>(raw.data()), raw.size(), &w, &h);
    if (rgba == nullptr) {
        s.error =
            std::string("WebPDecode") + (rgb_only ? "RGB" : "RGBA") + " failed: " + p.string();
        return s;
    }
    s.width = w;
    s.height = h;
    s.channels = rgb_only ? 3 : 4;
    s.bitdepth = 8;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    s.px.resize(n * static_cast<std::size_t>(s.channels));
    for (std::size_t i = 0; i < s.px.size(); ++i) {
        s.px[i] = static_cast<float>(rgba[i]) / 255.0f;
    }
    WebPFree(rgba);
    return s;
}

Sample load_sample(const fs::path &p, bool webp_rgb_only = false) {
    if (lower_ext(p) == ".webp")
        return load_webp(p, webp_rgb_only);
    return load_oiio(p);
}

bool same_geometry(const Sample &a, const Sample &b, std::string &detail) {
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
std::string compare_exact(const Sample &in, const Sample &out) {
    std::string detail;
    if (!same_geometry(in, out, detail))
        return detail;
    const std::size_t n = in.px.size();
    std::size_t mismatches = 0;
    std::size_t first = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const long a = std::lround(std::clamp(in.px[i], 0.0f, 1.0f) * 65535.0f);
        const long b = std::lround(std::clamp(out.px[i], 0.0f, 1.0f) * 65535.0f);
        if (a != b) {
            if (mismatches == 0)
                first = i;
            ++mismatches;
        }
    }
    if (mismatches == 0)
        return {};
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

std::string compare_psnr(const Sample &in, const Sample &out, double threshold_db) {
    std::string detail;
    if (!same_geometry(in, out, detail))
        return detail;
    const std::size_t n = in.px.size();
    if (n == 0)
        return "empty sample set";
    double mse = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(in.px[i]) - static_cast<double>(out.px[i]);
        mse += d * d;
    }
    mse /= static_cast<double>(n);
    const double psnr =
        mse <= 0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(1.0 / mse);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "psnr %.2f dB < threshold %.2f dB", psnr, threshold_db);
    if (psnr + 1e-9 < threshold_db)
        return buf;
    return {};
}

// ---------------------------------------------------------------------------
// metadata (Exiv2)
// ---------------------------------------------------------------------------
std::string xmp_key_normalized(const std::string &key) {
    std::string k = key;
    std::replace(k.begin(), k.end(), ':', '.'); // Xmp.xmp:CreateDate → Xmp.xmp.CreateDate
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

std::string trim_ascii_text(const std::string &in) {
    std::size_t end = in.size();
    while (end > 0 && in[end - 1] == '\0')
        --end;
    std::size_t begin = 0;
    while (begin < end && ascii_blank(in[begin]))
        ++begin;
    while (end > begin && ascii_blank(in[end - 1]))
        --end;
    return in.substr(begin, end - begin);
}

std::string reduce_rational(int64_t num, int64_t den) {
    // Degenerate guard: Exif allows 0/0 (e.g. unknown GPS values). No corpus instance (T8
    // report), rules do not define it — keep the raw pair instead of dividing by zero.
    if (den == 0)
        return std::to_string(num) + "/0";
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
    if (d == 1)
        return std::to_string(n);
    return std::to_string(n) + "/" + std::to_string(d);
}

std::string binary_hex(const Exiv2::Value &v) {
    std::vector<Exiv2::byte> raw(v.size());
    if (!raw.empty())
        v.copy(raw.data(), Exiv2::invalidByteOrder);
    static const char *kDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(raw.size() * 2);
    for (const Exiv2::byte b : raw) {
        hex += kDigits[b >> 4];
        hex += kDigits[b & 0x0f];
    }
    if (hex.size() > 64)
        hex = hex.substr(0, 64) + "...";
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

std::string scalar_text(const Exiv2::Metadatum &d, Exiv2::ExifData *exif) {
    std::string printed = d.print(exif);
    if (printed.empty())
        printed = d.toString();
    return printed;
}

std::string normalize_value_text(const Exiv2::Metadatum &d, Exiv2::ExifData *exif) {
    const Exiv2::Value &v = d.value();
    switch (d.typeId()) {
    case Exiv2::unsignedRational:
    case Exiv2::signedRational: {
        std::string out;
        for (std::size_t i = 0; i < v.count(); ++i) {
            const Exiv2::Rational r = v.toRational(i);
            if (!out.empty())
                out += ", ";
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
    if (has_element_count(d.typeId()) && v.count() > 1) { // arrays: elements joined with ", "
        std::string out;
        try {
            for (std::size_t i = 0; i < v.count(); ++i) {
                if (!out.empty())
                    out += ", ";
                out += v.toString(i);
            }
            return out;
        } catch (const std::exception &) {
            return scalar_text(d, exif); // a rendering quirk must not become a hard error
        }
    }
    return scalar_text(d, exif);
}

bool metadata_lookup(Exiv2::ExifData &exif, Exiv2::XmpData &xmp, const std::string &key,
                     bool &present, std::string &value, std::string &err) {
    present = false;
    value.clear();
    try {
        if (key.rfind("Xmp.", 0) == 0) {
            const Exiv2::XmpKey xk(xmp_key_normalized(key));
            const auto it = xmp.findKey(xk);
            if (it == xmp.end())
                return true;
            present = true;
            value = normalize_value_text(*it, nullptr);
            return true;
        }
        const Exiv2::ExifKey ek(key);
        const auto it = exif.findKey(ek);
        if (it == exif.end())
            return true;
        present = true;
        value = normalize_value_text(*it, &exif);
        return true;
    } catch (const std::exception &e) {
        err = std::string("metadata key '") + key + "': " + e.what();
        return false;
    }
}

std::string check_metadata(const fs::path &actual, const QJsonArray &asserts) {
    if (asserts.isEmpty())
        return {};
    Exiv2::Image::UniquePtr img;
    try {
        img = Exiv2::ImageFactory::open(actual.string());
        if (!img)
            return "cannot open output for metadata: " + actual.string();
        img->readMetadata();
    } catch (const std::exception &e) {
        return std::string("metadata read failed: ") + e.what();
    }
    Exiv2::ExifData exif = img->exifData();
    Exiv2::XmpData xmp = img->xmpData();

    std::string failures;
    for (const QJsonValue &v : asserts) {
        const QJsonObject a = v.toObject();
        const std::string key = a.value("key").toString().toStdString();
        const std::string op = a.value("op").toString("eq").toStdString();
        const std::string want = a.value("value").toString().toStdString();
        bool present = false;
        std::string got, err;
        if (!metadata_lookup(exif, xmp, key, present, got, err))
            return err;
        auto add_failure = [&failures](const std::string &text) {
            if (!failures.empty())
                failures += "; ";
            failures += text;
        };
        if (op == "eq") {
            if (!present) {
                add_failure("metadata '" + key + "' missing (expected '" + want + "')");
            } else if (got != want) {
                add_failure("metadata '" + key + "' = '" + got + "' != '" + want + "'");
            }
        } else if (op == "exists") {
            if (!present)
                add_failure("metadata '" + key + "' missing (expected to exist)");
        } else if (op == "absent") {
            if (present)
                add_failure("metadata '" + key + "' present ('" + got + "')");
        } else {
            add_failure("unknown metadata op '" + op + "'");
        }
    }
    return failures;
}

// ---------------------------------------------------------------------------
// warnings (sidecar written by `photopipeline --dev`)
// ---------------------------------------------------------------------------
std::string check_warnings(const fs::path &actual, const QJsonArray &asserts) {
    if (asserts.isEmpty())
        return {};
    const fs::path sidecar = actual.string() + ".pp.json";
    std::ifstream f(sidecar, std::ios::binary);
    if (!f)
        return "warnings_contain requires the sidecar " + sidecar.string() + " (missing)";
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const QJsonObject obj = QJsonDocument::fromJson(QByteArray::fromStdString(text)).object();
    if (obj.isEmpty())
        return "sidecar " + sidecar.string() + " is not valid JSON";
    std::vector<std::string> kinds;
    for (const QJsonValue &w : obj.value("warnings").toArray()) {
        if (w.isString()) {
            kinds.push_back(w.toString().toStdString());
        } else if (w.isObject()) {
            kinds.push_back(w.toObject().value("kind").toString().toStdString());
        }
    }
    std::string failures;
    for (const QJsonValue &v : asserts) {
        const std::string want = v.toString().toStdString();
        const bool found = std::find(kinds.begin(), kinds.end(), want) != kinds.end();
        if (!found) {
            std::string have;
            for (const std::string &k : kinds) {
                if (!have.empty())
                    have += ",";
                have += k;
            }
            if (!failures.empty())
                failures += "; ";
            failures += "warning '" + want + "' not reported (have: [" + have + "])";
        }
    }
    return failures;
}

// ---------------------------------------------------------------------------
// one case
// ---------------------------------------------------------------------------
fs::path golden_root_for(const fs::path &expected_json) {
    // expected.json lives in <golden>/smoke/, so <golden> = parent(parent)
    std::error_code ec;
    const fs::path dir = fs::absolute(expected_json, ec).parent_path();
    if (!dir.empty()) {
        const fs::path candidate = dir.parent_path();
        if (fs::is_directory(candidate, ec))
            return candidate;
    }
    return {};
}

bool check_case(const QJsonObject &exp, const fs::path &actual, const fs::path &golden_root,
                std::string &detail) {
    std::vector<std::string> failures;

    const QJsonObject asserts = exp.value("assert").toObject();
    const QJsonObject pixel = asserts.value("pixel").toObject();
    if (!pixel.isEmpty()) {
        const std::string mode = pixel.value("mode").toString().toStdString();
        std::string input_rel = exp.value("input").toString().toStdString();
        fs::path input_path(input_rel);
        if (input_path.is_relative() && !golden_root.empty())
            input_path = golden_root / input_path;
        const Sample in = load_sample(input_path);
        if (!in.error.empty()) {
            failures.push_back("input: " + in.error);
        } else {
            // 参照 = 3 通道（无 alpha 源）且产物是 webp → 产物按 RGB 解码（通道对齐）
            const Sample out = load_sample(actual, in.channels == 3);
            if (!out.error.empty()) {
                failures.push_back("output: " + out.error);
            } else if (mode == "exact") {
                const std::string e = compare_exact(in, out);
                if (!e.empty())
                    failures.push_back(e);
            } else if (mode == "psnr") {
                const double threshold = pixel.value("threshold_db").toDouble(40.0);
                const std::string e = compare_psnr(in, out, threshold);
                if (!e.empty())
                    failures.push_back(e);
            } else {
                failures.push_back("unknown pixel.mode '" + mode + "'");
            }
        }
    }

    const std::string meta_err = check_metadata(actual, asserts.value("metadata").toArray());
    if (!meta_err.empty())
        failures.push_back(meta_err);

    const std::string warn_err =
        check_warnings(actual, asserts.value("warnings_contain").toArray());
    if (!warn_err.empty())
        failures.push_back(warn_err);

    if (failures.empty()) {
        detail.clear();
        return true;
    }
    detail.clear();
    for (const std::string &f : failures) {
        if (!detail.empty())
            detail += "; ";
        detail += f;
    }
    return false;
}

// ---------------------------------------------------------------------------
// M4-T5: expected.json v2 —— outputs[] 多产物断言（路径断言 + 逐输出像素/元数据/告警）
// ---------------------------------------------------------------------------
// expected.json 携带 outputs[] 时，第二参数是**用例输出根目录**：每个条目
//   { "rel": "<相对用例根目录的产物路径>", "format": "<格式 id，记录用>", "assert": {…} }
// 各自定位文件并断言 —— 文件不存在即 FAIL（= 路径断言）。条目缺 assert → 回落顶层 assert。
bool check_case_outputs(const QJsonObject &exp, const fs::path &case_root,
                        const fs::path &golden_root, std::string &detail) {
    std::vector<std::string> failures;
    const QJsonArray outputs = exp.value("outputs").toArray();
    const QJsonObject top_asserts = exp.value("assert").toObject();
    for (const QJsonValue &v : outputs) {
        const QJsonObject o = v.toObject();
        const std::string rel = o.value("rel").toString().toStdString();
        if (rel.empty()) {
            failures.push_back("outputs[] entry without 'rel'");
            continue;
        }
        const fs::path file = case_root / fs::path(rel);
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) {
            failures.push_back("output '" + rel + "' missing (expected at " + file.string() + ")");
            continue;
        }
        QJsonObject per = o.value("assert").toObject();
        if (per.isEmpty()) {
            per = top_asserts;
        }
        QJsonObject one;
        one["case"] = exp.value("case");
        // 逐输出可用自己的 input 覆盖顶层 input（一源多产物时产物对应不同源，如 conflict 对）
        const QString own_input = o.value("input").toString();
        one["input"] = own_input.isEmpty() ? exp.value("input") : QJsonValue(own_input);
        one["assert"] = per;
        std::string one_detail;
        if (!check_case(one, file, golden_root, one_detail)) {
            failures.push_back(rel + ": " + one_detail);
        }
    }
    if (failures.empty()) {
        detail.clear();
        return true;
    }
    detail.clear();
    for (const std::string &f : failures) {
        if (!detail.empty())
            detail += "; ";
        detail += f;
    }
    return false;
}

// ---------------------------------------------------------------------------
// M4-T6: expected.json 的 progress_trace 段 —— --dev 进度事件流断言
//   （docs/v0.3.0-design.md §7.1–§7.4；文件 = <用例输出根目录>/progress-trace.jsonl，
//    由 photopipeline --dev 逐事件写出，schema 见 tests/golden/SCHEMA.md）
//   {
//     "file": "progress-trace.jsonl",            // 相对用例输出根目录（缺省同值）
//     "expect_outputs": [
//       { "index": 0, "synthetic": false, "reported": true },   // jxl：真实行级
//       { "index": 1, "synthetic": true,  "reported": false }   // webp：合成（§7.3）
//     ]
//   }
//   断言（逐条 → 失败即拼进 detail）：
//     ① 文件存在、逐行可解析（JSON Lines）、非空；
//     ② 每文件（file 列）的 overall_frac 单调不倒退且 ∈ [0,1]（§7.3/§7.4 消费端契约）；
//     ③ expect_outputs 的每个下标：存在事件，且该下标的**所有**事件 synthetic 与期望一致
//        （§3.1：真实进度不得被合成覆盖；§7.3：合成面必须如实标 synthetic）；
//     ④ reported 非空时：该产物 sidecar（expect_outputs[].rel 或 outputs[index].rel 的
//        `<rel>.pp.json`）的 progress_reported 必须与期望一致 —— "编码器是否真报了行级进度"
//        的硬证据（§7.4 快照键）；progress_max_row 与真实/合成口径同向（真实 > 0、合成 = 0）；
//     ⑤ 终态为 done 的文件必须存在 overall_frac == 1.0 的进度事件（§7.2 权重合计=1 的可观测面）。
// ---------------------------------------------------------------------------
struct TraceRow {
    int file = -1;
    std::string state;
    int output_index = -1;
    float overall_frac = -1.f;
    bool synthetic = false;
};

bool read_progress_trace(const fs::path &p, std::vector<TraceRow> &rows, std::string &err) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        err = "progress trace missing: " + p.string();
        return false;
    }
    std::string line;
    std::size_t bad = 0;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;
        const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        if (o.isEmpty()) {
            ++bad;
            continue;
        }
        TraceRow r;
        r.file = o.value("file").toInt(-1);
        r.state = o.value("state").toString().toStdString();
        r.output_index = o.value("output_index").toInt(-1);
        r.overall_frac = static_cast<float>(o.value("overall_frac").toDouble(-1.0));
        r.synthetic = o.value("synthetic").toBool(false);
        rows.push_back(r);
    }
    if (bad != 0) {
        err = std::to_string(bad) + " unparsable line(s) in " + p.string();
        return false;
    }
    if (rows.empty()) {
        err = "empty progress trace: " + p.string();
        return false;
    }
    return true;
}

std::string check_progress_trace(const QJsonObject &exp, const fs::path &case_root) {
    std::vector<std::string> failures;
    const QJsonObject pt = exp.value("progress_trace").toObject();
    std::string rel = pt.value("file").toString("progress-trace.jsonl").toStdString();
    if (rel.empty())
        rel = "progress-trace.jsonl";
    std::vector<TraceRow> rows;
    std::string err;
    if (!read_progress_trace(case_root / rel, rows, err))
        return err;

    // ② 单调不倒退 + 值域（逐文件；每文件只报首条违例）
    std::vector<int> files_seen;
    std::vector<float> last_by_file, max_by_file;
    std::vector<std::string> terminal_by_file;
    for (const TraceRow &r : rows) {
        if (r.overall_frac < 0.f) {
            failures.push_back("row without a usable overall_frac (file " + std::to_string(r.file) +
                               ")");
            break;
        }
        if (r.overall_frac > 1.0f + 1e-6f) {
            failures.push_back("file " + std::to_string(r.file) + ": overall_frac " +
                               std::to_string(r.overall_frac) + " > 1");
            break;
        }
        std::size_t slot = 0;
        for (;; ++slot) {
            if (slot == files_seen.size()) {
                files_seen.push_back(r.file);
                last_by_file.push_back(-1.f);
                max_by_file.push_back(0.f);
                terminal_by_file.push_back(std::string());
                break;
            }
            if (files_seen[slot] == r.file)
                break;
        }
        if (last_by_file[slot] >= 0.f && r.overall_frac < last_by_file[slot] - 1e-6f) {
            failures.push_back("file " + std::to_string(r.file) + ": overall_frac regressed (" +
                               std::to_string(last_by_file[slot]) + " -> " +
                               std::to_string(r.overall_frac) + ")");
            break;
        }
        last_by_file[slot] = std::max(last_by_file[slot], r.overall_frac);
        max_by_file[slot] = std::max(max_by_file[slot], r.overall_frac);
        if (r.state == "done" || r.state == "skipped" || r.state == "failed" ||
            r.state == "cancelled")
            terminal_by_file[slot] = r.state;
    }

    // ③ synthetic 口径（逐输出下标）+ ④ sidecar 的 progress_reported/max_row
    const QJsonArray expect = pt.value("expect_outputs").toArray();
    for (const QJsonValue &v : expect) {
        const QJsonObject eo = v.toObject();
        const int index = eo.value("index").toInt(-1);
        if (index < 0) {
            failures.push_back("expect_outputs entry without a valid 'index'");
            continue;
        }
        const bool want_synth = eo.value("synthetic").toBool(false);
        std::size_t seen = 0, mismatch = 0;
        for (const TraceRow &r : rows) {
            if (r.output_index != index)
                continue;
            ++seen;
            if (r.synthetic != want_synth)
                ++mismatch;
        }
        if (seen == 0) {
            failures.push_back("no progress event for output_index " + std::to_string(index));
        } else if (mismatch != 0) {
            failures.push_back("output_index " + std::to_string(index) + ": " +
                               std::to_string(mismatch) + "/" + std::to_string(seen) +
                               " events do not match synthetic=" + (want_synth ? "true" : "false"));
        }
        if (!eo.contains("reported"))
            continue;
        const bool want_reported = eo.value("reported").toBool(false);
        std::string out_rel = eo.value("rel").toString().toStdString();
        if (out_rel.empty()) {
            const QJsonArray outs = exp.value("outputs").toArray();
            if (index < outs.size())
                out_rel = outs.at(index).toObject().value("rel").toString().toStdString();
        }
        if (out_rel.empty()) {
            failures.push_back(
                "output_index " + std::to_string(index) +
                ": no sidecar path (set expect_outputs[].rel or outputs[index].rel)");
            continue;
        }
        const fs::path sidecar = case_root / (out_rel + ".pp.json");
        std::ifstream in(sidecar, std::ios::binary);
        if (!in) {
            failures.push_back("sidecar missing for progress_reported assertion: " +
                               sidecar.string());
            continue;
        }
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        const QJsonObject so = QJsonDocument::fromJson(QByteArray::fromStdString(text)).object();
        const bool got_reported = so.value("progress_reported").toBool(!want_reported);
        const int got_row = so.value("progress_max_row").toInt(-1);
        if (got_reported != want_reported) {
            failures.push_back(out_rel +
                               ": sidecar progress_reported=" + (got_reported ? "true" : "false") +
                               ", expected " + (want_reported ? "true" : "false"));
        }
        if (want_reported && got_row <= 0)
            failures.push_back(out_rel +
                               ": real progress but progress_max_row=" + std::to_string(got_row));
        if (!want_reported && got_row != 0)
            failures.push_back(
                out_rel + ": synthetic output but progress_max_row=" + std::to_string(got_row));
    }

    // ⑤ 成功文件必须留下 1.0 的进度事件
    for (std::size_t i = 0; i < files_seen.size(); ++i) {
        if (terminal_by_file[i] == "done" && max_by_file[i] < 1.0f - 1e-6f)
            failures.push_back(
                "file " + std::to_string(files_seen[i]) +
                ": terminal state done but max overall_frac = " + std::to_string(max_by_file[i]));
    }

    if (failures.empty())
        return {};
    std::string detail;
    for (const std::string &f : failures) {
        if (!detail.empty())
            detail += "; ";
        detail += f;
    }
    return detail;
}

// v1（无 outputs[]）= 单产物断言（<actual> 即产物文件）；v2 = outputs[] 逐产物断言。
// M4-T6：两者都可叠加 progress_trace 段（此时第二参数必须是**用例输出根目录**）。
bool check_expected(const QJsonObject &exp, const fs::path &actual, const fs::path &golden_root,
                    std::string &detail) {
    const QJsonArray outputs = exp.value("outputs").toArray();
    if (!outputs.isEmpty()) {
        if (!check_case_outputs(exp, actual, golden_root, detail))
            return false;
    } else if (!check_case(exp, actual, golden_root, detail)) {
        return false;
    }
    if (exp.contains("progress_trace")) {
        std::error_code ec;
        if (!fs::is_directory(actual, ec)) {
            detail = "progress_trace requires the case output root as the second argument (" +
                     actual.string() + ")";
            return false;
        }
        const std::string pt_err = check_progress_trace(exp, actual);
        if (!pt_err.empty()) {
            detail = pt_err;
            return false;
        }
    }
    detail.clear();
    return true;
}

// ---------------------------------------------------------------------------
// --selftest
// ---------------------------------------------------------------------------
bool write_image(const fs::path &p, int w, int h, const std::vector<unsigned char> &rgb,
                 std::string &err, bool jpeg = false) {
    auto out = OIIO::ImageOutput::create(p.string());
    if (!out) {
        err = "no OIIO output plugin for " + p.string();
        return false;
    }
    OIIO::ImageSpec spec(w, h, 3, OIIO::TypeDesc::UINT8);
    if (jpeg)
        spec.attribute("quality", 95);
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
    for (unsigned char &v : inverted)
        v = static_cast<unsigned char>(255 - v);
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
        exif["Exif.Image.Artist"] = "M1-T8";                      // asciiString, scalar
        exif["Exif.Image.ImageDescription"] = "  padded M2-T8  "; // asciiString, trimmed
        exif["Exif.Photo.FNumber"] = "28/10";                     // unsignedRational -> 14/5
        exif["Exif.Photo.BrightnessValue"] = "-6/4";              // signedRational -> -3/2
        exif["Exif.Image.XResolution"] = "300/1";                 // unsignedRational, den == 1
        exif["Exif.Image.YCbCrSubSampling"] = "2 1";              // unsignedShort array
        auto undefined_value = [](const std::vector<Exiv2::byte> &bytes) {
            Exiv2::DataValue dv(Exiv2::undefined);
            dv.read(bytes.data(), bytes.size(), Exiv2::invalidByteOrder);
            return dv;
        };
        const Exiv2::DataValue version_bytes = undefined_value({'0', '2', '3', '2'});
        std::vector<Exiv2::byte> long_binary(40); // undefined, > 64 hex digits
        for (std::size_t i = 0; i < long_binary.size(); ++i) {
            long_binary[i] = static_cast<Exiv2::byte>(i + 1);
        }
        const Exiv2::DataValue maker_bytes = undefined_value(long_binary);
        exif["Exif.Photo.ExifVersion"].setValue(&version_bytes); // undefined, <= 64 hex digits
        exif["Exif.Photo.MakerNote"].setValue(&maker_bytes);
        img->setExifData(exif);
        Exiv2::XmpData xmp = img->xmpData();
        xmp["Xmp.xmp.CreatorTool"] = "PhotoPipeline-M2-T8"; // xmpText: count() is a byte length
        xmp["Xmp.dc.subject"] = "alpha";                    // xmpBag: ", " joined items
        xmp["Xmp.dc.subject"] = "beta";
        img->setXmpData(xmp);
        img->writeMetadata();
    } catch (const std::exception &e) {
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

    auto make_exp = [](const char *input, const char *mode, double threshold,
                       const QJsonArray &metadata, const QJsonArray &warnings) {
        QJsonObject pixel;
        if (mode != nullptr && *mode != '\0') { // empty mode = no pixel assertion
            pixel["mode"] = mode;
            if (std::strcmp(mode, "psnr") == 0)
                pixel["threshold_db"] = threshold;
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
    auto meta_eq = [](const char *key, const char *value) {
        QJsonObject o;
        o["key"] = key;
        o["op"] = "eq";
        o["value"] = value;
        QJsonArray a;
        a.append(o);
        return a;
    };
    auto meta_op = [](const char *key, const char *op) {
        QJsonObject o;
        o["key"] = key;
        o["op"] = op;
        QJsonArray a;
        a.append(o);
        return a;
    };
    auto warns = [](const char *kind) {
        QJsonArray a;
        a.append(kind);
        return a;
    };

    struct Probe {
        const char *label;
        QJsonObject exp;
        fs::path actual;
        bool expect_ok;
    };
    const std::vector<Probe> probes = {
        {"exact-pass", make_exp("a.png", "exact", 0, {}, {}), b_png, true},
        {"exact-fail", make_exp("a.png", "exact", 0, {}, {}), c_png, false},
        {"psnr-pass", make_exp("a.png", "psnr", 30.0, {}, {}), a_jpg, true},
        {"psnr-fail", make_exp("a.png", "psnr", 30.0, {}, {}), inv_png, false},
        {"metadata-eq-pass",
         make_exp("a.png", "exact", 0, meta_eq("Exif.Image.Artist", "M1-T8"), {}), m_png, true},
        {"metadata-eq-fail",
         make_exp("a.png", "exact", 0, meta_eq("Exif.Image.Artist", "nope"), {}), m_png, false},
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
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Photo.BrightnessValue", "-3/2"), {}), m_png,
         true},
        {"normalize-rational-den1",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Image.XResolution", "300"), {}), m_png, true},
        {"normalize-array-join",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Image.YCbCrSubSampling", "2, 1"), {}), m_png,
         true},
        {"normalize-undefined-hex",
         make_exp("a.png", nullptr, 0, meta_eq("Exif.Photo.ExifVersion", "0x30323332"), {}), m_png,
         true},
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
         make_exp("a.png", nullptr, 0, meta_eq("Xmp.xmp.CreatorTool", "PhotoPipeline-M2-T8"), {}),
         m_png, true},
        {"normalize-xmp-bag-join",
         make_exp("a.png", nullptr, 0, meta_eq("Xmp.dc.subject", "alpha, beta"), {}), m_png, true},
    };

    int failures = 0;
    for (const Probe &p : probes) {
        std::string detail;
        const bool ok = check_expected(p.exp, p.actual, dir, detail);
        const bool behaves = (ok == p.expect_ok);
        if (!behaves)
            ++failures;
        std::printf("VERIFY selftest %-22s %s (expected %s, got %s)%s%s\n", p.label,
                    behaves ? "OK" : "FAIL", p.expect_ok ? "OK" : "FAIL", ok ? "OK" : "FAIL",
                    detail.empty() ? "" : " ", detail.c_str());
    }

    // M4-T5: expected.json v2（outputs[]）探针：逐产物定位/路径断言/断言按产物绑定
    {
        // 两个产物都命中 → OK（第二参 = 用例输出根目录）
        QJsonObject e = make_exp("a.png", nullptr, 0, {}, {});
        QJsonArray outs;
        {
            QJsonObject o0;
            o0["rel"] = "b.png";
            o0["format"] = "png";
            QJsonObject a0;
            a0["pixel"] = QJsonObject{{"mode", "exact"}};
            a0["metadata"] = QJsonArray{};
            a0["warnings_contain"] = QJsonArray{};
            o0["assert"] = a0;
            outs.append(o0);
            QJsonObject o1;
            o1["rel"] = "a.png";
            QJsonObject a1;
            a1["pixel"] = QJsonObject{{"mode", "exact"}};
            a1["metadata"] = QJsonArray{};
            a1["warnings_contain"] = QJsonArray{};
            o1["assert"] = a1;
            outs.append(o1);
        }
        e["outputs"] = outs;
        {
            std::string detail;
            const bool ok = check_expected(e, dir, dir, detail);
            const bool behaves = ok;
            if (!behaves)
                ++failures;
            std::printf("VERIFY selftest %-22s %s%s%s\n", "outputs-pass", behaves ? "OK" : "FAIL",
                        detail.empty() ? "" : " ", detail.c_str());
        }
        // 产物缺失 → FAIL（= 路径断言）
        {
            QJsonArray missing;
            QJsonObject o;
            o["rel"] = "no-such-output.png";
            missing.append(o);
            QJsonObject e2 = e;
            e2["outputs"] = missing;
            std::string detail;
            const bool ok = check_expected(e2, dir, dir, detail);
            const bool behaves = !ok;
            if (!behaves)
                ++failures;
            std::printf("VERIFY selftest %-22s %s%s%s\n", "outputs-missing-fail",
                        behaves ? "OK" : "FAIL", detail.empty() ? "" : " ", detail.c_str());
        }
        // 断言按产物绑定：第二个产物（c.png 已被扰动）的 exact 断言必须失败
        {
            QJsonArray bound;
            QJsonObject o0;
            o0["rel"] = "b.png";
            QJsonObject a0;
            a0["pixel"] = QJsonObject{{"mode", "exact"}};
            o0["assert"] = a0;
            bound.append(o0);
            QJsonObject o1;
            o1["rel"] = "c.png";
            QJsonObject a1;
            a1["pixel"] = QJsonObject{{"mode", "exact"}};
            o1["assert"] = a1;
            bound.append(o1);
            QJsonObject e3 = e;
            e3["outputs"] = bound;
            std::string detail;
            const bool ok = check_expected(e3, dir, dir, detail);
            const bool behaves = !ok && detail.find("c.png") != std::string::npos;
            if (!behaves)
                ++failures;
            std::printf("VERIFY selftest %-22s %s%s%s\n", "outputs-binding-fail",
                        behaves ? "OK" : "FAIL", detail.empty() ? "" : " ", detail.c_str());
        }
    }

    // M4-T6: progress_trace 段探针（单调性 / synthetic 口径 / sidecar progress_reported）
    {
        const fs::path pdir = dir / "progress";
        std::error_code ec;
        fs::create_directories(pdir, ec);
        auto write_text = [](const fs::path &p, const std::string &text) {
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            f << text;
        };
        write_text(pdir / "o0.png", "not-an-image"); // 仅作"产物存在"的路径断言
        write_text(pdir / "o1.png", "not-an-image");
        write_text(pdir / "o0.png.pp.json", "{\"progress_reported\":true,\"progress_max_row\":64}");
        write_text(pdir / "o1.png.pp.json", "{\"progress_reported\":false,\"progress_max_row\":0}");
        // 合法事件流：共享段 0.03 → 0.40；输出 0（真实）→ 0.65/0.70；输出 1（合成）→ 1.0
        const std::string ok_trace =
            "{\"file\":0,\"state\":\"probing\",\"output_index\":-1,\"overall_frac\":0.0,"
            "\"synthetic\":false}\n"
            "{\"file\":0,\"state\":\"progress\",\"output_index\":0,\"overall_frac\":0.40,"
            "\"synthetic\":false}\n"
            "{\"file\":0,\"state\":\"progress\",\"output_index\":0,\"overall_frac\":0.65,"
            "\"synthetic\":false}\n"
            "{\"file\":0,\"state\":\"progress\",\"output_index\":1,\"overall_frac\":0.70,"
            "\"synthetic\":true}\n"
            "{\"file\":0,\"state\":\"progress\",\"output_index\":-1,\"overall_frac\":1.0,"
            "\"synthetic\":false}\n"
            "{\"file\":0,\"state\":\"done\",\"output_index\":1,\"overall_frac\":1.0,"
            "\"synthetic\":true}\n";
        write_text(pdir / "progress-trace.jsonl", ok_trace);

        auto make_pt_exp = [&](const std::string &trace_text) {
            QJsonObject e;
            e["case"] = "selftest-progress";
            QJsonArray outs;
            for (const char *rel : {"o0.png", "o1.png"}) {
                QJsonObject o;
                o["rel"] = rel;
                QJsonObject a;
                a["pixel"] = QJsonObject{};
                a["metadata"] = QJsonArray{};
                a["warnings_contain"] = QJsonArray{};
                o["assert"] = a;
                outs.append(o);
            }
            e["outputs"] = outs;
            QJsonObject pt;
            pt["file"] = "progress-trace.jsonl";
            QJsonArray eo;
            {
                QJsonObject o0;
                o0["index"] = 0;
                o0["synthetic"] = false;
                o0["reported"] = true;
                eo.append(o0);
                QJsonObject o1;
                o1["index"] = 1;
                o1["synthetic"] = true;
                o1["reported"] = false;
                eo.append(o1);
            }
            pt["expect_outputs"] = eo;
            e["progress_trace"] = pt;
            if (!trace_text.empty())
                write_text(pdir / "progress-trace.jsonl", trace_text);
            else
                write_text(pdir / "progress-trace.jsonl", ok_trace);
            return e;
        };

        {
            const QJsonObject e = make_pt_exp("");
            std::string detail;
            const bool ok = check_expected(e, pdir, dir, detail);
            if (!ok)
                ++failures;
            std::printf("VERIFY selftest %-22s %s%s%s\n", "progress-trace-pass", ok ? "OK" : "FAIL",
                        detail.empty() ? "" : " ", detail.c_str());
        }
        {
            // overall_frac 倒退（0.70 → 0.40）→ 必须 FAIL 且点名单调性
            std::string bad = ok_trace;
            const std::string needle = "\"output_index\":1,\"overall_frac\":0.70";
            const std::size_t at = bad.find(needle);
            if (at != std::string::npos)
                bad.replace(at, needle.size(), "\"output_index\":1,\"overall_frac\":0.40");
            const QJsonObject e = make_pt_exp(bad);
            std::string detail;
            const bool ok = check_expected(e, pdir, dir, detail);
            const bool behaves = !ok && detail.find("regressed") != std::string::npos;
            if (!behaves)
                ++failures;
            std::printf("VERIFY selftest %-22s %s%s%s\n", "progress-trace-regress-fail",
                        behaves ? "OK" : "FAIL", detail.empty() ? "" : " ", detail.c_str());
        }
        {
            // synthetic 口径不符（合成面被标 false）→ 必须 FAIL
            std::string bad = ok_trace;
            const std::string needle =
                "\"output_index\":1,\"overall_frac\":0.70,\"synthetic\":true";
            const std::size_t at = bad.find(needle);
            if (at != std::string::npos)
                bad.replace(at, needle.size(),
                            "\"output_index\":1,\"overall_frac\":0.70,\"synthetic\":false");
            const QJsonObject e = make_pt_exp(bad);
            std::string detail;
            const bool ok = check_expected(e, pdir, dir, detail);
            const bool behaves = !ok && detail.find("synthetic") != std::string::npos;
            if (!behaves)
                ++failures;
            std::printf("VERIFY selftest %-22s %s%s%s\n", "progress-trace-synthetic-fail",
                        behaves ? "OK" : "FAIL", detail.empty() ? "" : " ", detail.c_str());
        }
        write_text(pdir / "progress-trace.jsonl", ok_trace); // 复位（不影响其它探针）
    }

    // M2-T8 (#26): direct probes of the frozen normalisation rules. The corpus cannot express
    // trailing NULs or den == 0, so the helpers are exercised here too.
    auto text_probe = [&failures](const char *label, const std::string &got, const char *want) {
        const bool ok = got == want;
        if (!ok)
            ++failures;
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
                probes.size() + 6 + 3 + 3, failures);
    return failures;
}

} // namespace

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--selftest") == 0)
            return selftest();
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
    const QString case_qs =
        exp.value("case").toString(QString::fromStdString(expected_json.stem().string()));
    const std::string case_name = case_qs.toStdString();

    std::error_code ec;
    if (!fs::exists(actual, ec)) {
        std::printf("VERIFY %s FAIL actual output does not exist: %s\n", case_name.c_str(),
                    actual.string().c_str());
        return 1;
    }

    std::string detail;
    const bool ok = check_expected(exp, actual, golden_root_for(expected_json), detail);
    if (ok) {
        std::printf("VERIFY %s OK\n", case_name.c_str());
        return 0;
    }
    std::printf("VERIFY %s FAIL %s\n", case_name.c_str(), detail.c_str());
    return 1;
}
