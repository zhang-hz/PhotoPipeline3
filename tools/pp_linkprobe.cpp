// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M0 link probe: verifies every linked dependency at runtime.
//
// Frozen contract: docs/m0-tasks.md §12.1. One line per check
//   PROBE <name> OK|FAIL <detail>
// followed by the two tail lines
//   PLUGINS: <comma list>
//   HEIF_ENCODERS: <comma list>
// Exit code = number of FAILed checks.

#include <OpenImageIO/imageio.h>

#include <exiv2/exiv2.hpp>
#include <jxl/encode.h>
#include <lcms2.h>
#include <libheif/heif.h>
#include <webp/encode.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// jpegli: the task book assumes <jpegli.h>; google/jpegli installs its public encoder
// API as jpegli/encode.h, built on libjpeg's jpeg_compress_struct.
#if __has_include(<jpegli/encode.h>)
#include <jpegli/encode.h>
#define PP_JPEGLI_HEADER "jpegli/encode.h"
#elif __has_include(<jpegli.h>)
#include <jpegli.h>
#define PP_JPEGLI_HEADER "jpegli.h"
#else
#include <jpeglib.h>
#define PP_JPEGLI_HEADER "(jpeglib.h + extern \"C\" jpegli_* declarations)"
extern "C" {
void jpegli_CreateCompress(j_compress_ptr cinfo, int version, size_t structsize);
void jpegli_set_defaults(j_compress_ptr cinfo);
void jpegli_set_distance(j_compress_ptr cinfo, float distance, boolean force_baseline);
void jpegli_destroy_compress(j_compress_ptr cinfo);
}
#define jpegli_create_compress(cinfo)                                                       \
    jpegli_CreateCompress((cinfo), JPEG_LIB_VERSION,                                        \
                          (size_t)sizeof(struct jpeg_compress_struct))
#endif

namespace {

int g_fails = 0;
std::string g_format_list;
std::vector<std::string> g_heif_encoders;

void report(const std::string& name, bool ok, const std::string& detail) {
    if (!ok) {
        ++g_fails;
    }
    std::printf("PROBE %s %s %s\n", name.c_str(), ok ? "OK" : "FAIL", detail.c_str());
    // Line-by-line visibility: stdout is fully buffered when ctest/pytest capture it,
    // and a later crash would swallow every earlier PROBE line.
    std::fflush(stdout);
}

std::string fmt_double(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return std::string(buf);
}

std::string fmt_hex(uint32_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08x", v);
    return std::string(buf);
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& s : items) {
        if (!out.empty()) {
            out += ",";
        }
        out += s;
    }
    return out.empty() ? std::string("(none)") : out;
}

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    for (std::string& t : out) {
        const size_t b = t.find_first_not_of(" \t");
        const size_t e = t.find_last_not_of(" \t");
        t = (b == std::string::npos) ? std::string() : t.substr(b, e - b + 1);
    }
    return out;
}

// --- checks ---------------------------------------------------------------------------

void check_lcms2() {
    cmsHPROFILE srgb = cmsCreate_sRGBProfile();
    if (srgb == nullptr) {
        report("lcms2", false, "cmsCreate_sRGBProfile() returned null");
        return;
    }
    cmsHTRANSFORM xf = cmsCreateTransform(srgb, TYPE_RGB_FLT, srgb, TYPE_RGB_FLT,
                                          INTENT_RELATIVE_COLORIMETRIC,
                                          cmsFLAGS_BLACKPOINTCOMPENSATION);
    if (xf == nullptr) {
        cmsCloseProfile(srgb);
        report("lcms2", false, "cmsCreateTransform() returned null");
        return;
    }
    const float in[3] = {0.25f, 0.5f, 0.75f};
    float out[3] = {0.0f, 0.0f, 0.0f};
    cmsDoTransform(xf, in, out, 1);
    double err = 0.0;
    for (int i = 0; i < 3; ++i) {
        err = std::max(err, std::fabs(static_cast<double>(out[i]) - static_cast<double>(in[i])));
    }
    cmsDeleteTransform(xf);
    const bool closed = cmsCloseProfile(srgb) != 0;
    report("lcms2", closed && err <= 1e-5,
           "identity_err=" + fmt_double(err) + " closed=" + (closed ? "1" : "0"));
}

void check_exiv2() {
    const std::string v = Exiv2::versionString();
    report("exiv2", !v.empty(), "version=" + (v.empty() ? std::string("(empty)") : v));
}

// M4-T2 (design §10, exiv2 0.28.9): the deprecated Exiv2 runtime BMFF toggle (removed from
// our code in this task) has no replacement API — BMFF support is a pure build-time feature
// (vcpkg feature "bmff"), so calling it was both useless and a deprecation warning. The
// check now asserts the same contract at the observable boundary instead: a minimal
// ISO-BMFF header (ftyp box) must be classified as ImageType::bmff by the image factory,
// which is only possible when exiv2 was built with BMFF support (verified against the
// installed 0.28.9 header: ImageFactory::getType(const byte*, size_t) exists and
// ImageType::bmff is exposed).
void check_exiv2_bmff() {
#if defined(EXIV2_TEST_VERSION) && EXIV2_TEST_VERSION(0, 28, 4)
    // box size(4, BE) + "ftyp" + major_brand "heic" + minor_version(4) + compatible "mif1".
    // The buffer is padded (the type probe needs a readable image header, not just the ftyp
    // box: a 20-byte input makes exiv2 throw "Failed to read input data" — measured with a
    // stand-alone probe against the installed 0.28.9).
    static const unsigned char kFtypHeic[1024] = {
        0x00, 0x00, 0x00, 0x14, 'f',  't',  'y',  'p',
        'h',  'e',  'i',  'c',  0x00, 0x00, 0x00, 0x00,
        'm',  'i',  'f',  '1',
    };
    try {
        const Exiv2::ImageType type = Exiv2::ImageFactory::getType(kFtypHeic, sizeof(kFtypHeic));
        const bool ok = type == Exiv2::ImageType::bmff;
        report("exiv2-bmff", ok,
               "ftyp(heic) -> ImageType=" + std::to_string(static_cast<int>(type)) +
                   " (bmff=" + std::to_string(static_cast<int>(Exiv2::ImageType::bmff)) + ")");
    } catch (const std::exception& e) {
        report("exiv2-bmff", false, std::string("getType threw: ") + e.what());
    }
#else
    report("exiv2-bmff", false, "ImageFactory::getType(const byte*, size_t) missing");
#endif
}

void check_oiio_plugins() {
    g_format_list = std::string(OIIO::get_string_attribute("format_list"));
    const std::vector<std::string> have = split_commas(g_format_list);
    std::vector<std::string> have_lc;
    have_lc.reserve(have.size());
    for (const std::string& h : have) {
        have_lc.push_back(lower_copy(h));
    }
    // §12.1 r3 (SUB-E field fact): OIIO has no separate avif plugin — its heif plugin owns
    // the avif/heic/heif extensions, so "avif" is not an OIIO format name and is not required.
    const std::vector<std::string> want = {"jpeg", "png", "tiff", "jxl", "heif",
                                           "webp", "gif", "targa", "bmp"};
    // Mechanical adaptation (§12.1 last line): OIIO >= 3.x registers the JPEG XL plugin
    // under the format NAME "jpegxl" (its file extension is "jxl"); older OIIO used "jxl".
    // The judging criterion is unchanged: JPEG XL support must be present.
    const auto present = [&have_lc](const std::string& w) {
        if (std::find(have_lc.begin(), have_lc.end(), w) != have_lc.end()) {
            return true;
        }
        return w == "jxl" && std::find(have_lc.begin(), have_lc.end(), "jpegxl") != have_lc.end();
    };
    std::string missing;
    for (const std::string& w : want) {
        if (!present(w)) {
            if (!missing.empty()) {
                missing += ",";
            }
            missing += w;
        }
    }
    const std::string detail =
        missing.empty()
            ? ("required=" + std::to_string(want.size()) +
               " all present (jxl=plugin \"jpegxl\"; heif covers avif)")
            : ("missing=" + missing);
    report("oiio-plugins", missing.empty(), detail);
}

void check_jpegli() {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    std::memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&jerr);
    jpegli_create_compress(&cinfo);
    jpegli_set_defaults(&cinfo);
    // Installed jpegli API: jpegli_set_distance(cinfo, float, force_baseline) — the 3-argument
    // form, not the 1-argument form assumed by the task book.
    jpegli_set_distance(&cinfo, 1.0f, FALSE);
    jpegli_destroy_compress(&cinfo);
    report("jpegli", true, std::string("create/set_distance/destroy ok via ") + PP_JPEGLI_HEADER);
}

void check_libjxl() {
    const uint32_t v = JxlEncoderVersion();
    report("libjxl", v > 0, "JxlEncoderVersion=" + fmt_hex(v));
}

std::vector<const heif_encoder_descriptor*> heif_encoder_descs(heif_compression_format fmt) {
    std::vector<const heif_encoder_descriptor*> descs;
    // libheif >= 1.4 exposes the 4-argument free function
    // heif_get_encoder_descriptors(format, name, out, count), not the 6-argument context form
    // assumed by the task book (verified against the installed headers).
    int count = heif_get_encoder_descriptors(fmt, nullptr, nullptr, 0);
    if (count <= 0) {
        return descs;
    }
    descs.resize(static_cast<size_t>(count), nullptr);
    count = heif_get_encoder_descriptors(fmt, nullptr, descs.data(), count);
    descs.resize(count > 0 ? static_cast<size_t>(count) : 0u);
    return descs;
}

std::vector<std::string> heif_encoder_names(heif_compression_format fmt) {
    std::vector<std::string> names;
    for (const heif_encoder_descriptor* d : heif_encoder_descs(fmt)) {
        const char* n = heif_encoder_descriptor_get_name(d);
        names.push_back(n != nullptr ? std::string(n) : std::string("(unnamed)"));
    }
    return names;
}

void check_libheif(const char* name, heif_compression_format fmt) {
    const std::vector<std::string> names = heif_encoder_names(fmt);
    for (const std::string& n : names) {
        g_heif_encoders.push_back(n);
    }
    report(name, !names.empty(), names.empty() ? "no encoder available" : join(names));
}

// --- encoder parameter introspection (R26/R27) -----------------------------------------
//
// The GUI parameter form is built from heif_encoder_list_parameters() at runtime, so a
// parameter-table drift introduced by a libheif / SVT-AV1 / x265 upgrade would silently
// empty out the UI instead of failing a build. These checks turn that drift into a FAIL:
//   * the introspection list must be non-empty, every entry must carry a non-empty name
//     and a valid heif_encoder_parameter_type (enum drift),
//   * the names the UI relies on must still be present (quality/lossless/preset for x265,
//     threads for SVT-AV1 — SVT-AV1 4.2 parameter surface, R26),
//   * the descriptor name is printed verbatim, so the encoder build actually in use
//     (x265 4.3 / SVT-AV1 4.2) is visible in the log.
struct HeifParamList {
    std::vector<std::string> names;
    bool types_valid = true;
};

bool heif_encoder_params(const heif_encoder_descriptor* desc, HeifParamList& out) {
    heif_context* ctx = heif_context_alloc();
    if (ctx == nullptr) {
        return false;
    }
    heif_encoder* enc = nullptr;
    // libheif >= 1.4 API (verified against the installed 1.23.5 header): the descriptor
    // form is heif_context_get_encoder(), there is no heif_encoder_create().
    const heif_error err = heif_context_get_encoder(ctx, desc, &enc);
    if (err.code != heif_error_Ok || enc == nullptr) {
        heif_context_free(ctx);
        return false;
    }
    for (const heif_encoder_parameter* const* p = heif_encoder_list_parameters(enc);
         p != nullptr && *p != nullptr; ++p) {
        const char* n = heif_encoder_parameter_get_name(*p);
        out.names.push_back(n != nullptr ? std::string(n) : std::string());
        const heif_encoder_parameter_type t = heif_encoder_parameter_get_type(*p);
        if (t != heif_encoder_parameter_type_integer && t != heif_encoder_parameter_type_boolean &&
            t != heif_encoder_parameter_type_string) {
            out.types_valid = false;
        }
    }
    heif_encoder_release(enc);
    heif_context_free(ctx);
    return true;
}

std::string join_names(const std::vector<std::string>& v, size_t limit) {
    std::string out;
    for (size_t i = 0; i < v.size() && i < limit; ++i) {
        if (!out.empty()) {
            out += "|";
        }
        out += v[i];
    }
    if (v.size() > limit) {
        out += "|...";
    }
    return out;
}

void check_heif_params(const char* name, heif_compression_format fmt, const char* want_substr,
                       const std::vector<std::string>& required) {
    const heif_encoder_descriptor* chosen = nullptr;
    std::string chosen_name;
    std::string available;
    for (const heif_encoder_descriptor* d : heif_encoder_descs(fmt)) {
        const char* n = heif_encoder_descriptor_get_name(d);
        const char* id = heif_encoder_descriptor_get_id_name(d);
        const std::string name_s = n != nullptr ? std::string(n) : std::string();
        const std::string id_s = id != nullptr ? std::string(id) : std::string();
        if (!available.empty()) {
            available += ",";
        }
        available += name_s.empty() ? std::string("(unnamed)") : name_s;
        if (chosen == nullptr &&
            (lower_copy(name_s).find(want_substr) != std::string::npos ||
             lower_copy(id_s).find(want_substr) != std::string::npos)) {
            chosen = d;
            chosen_name = name_s;
        }
    }
    if (chosen == nullptr) {
        report(name, false, std::string("no encoder matching \"") + want_substr +
                                "\" (available: " + available + ")");
        return;
    }
    HeifParamList list;
    if (!heif_encoder_params(chosen, list)) {
        report(name, false, "heif_encoder_create failed for " + chosen_name);
        return;
    }
    bool empty_name = false;
    for (const std::string& s : list.names) {
        if (s.empty()) {
            empty_name = true;
        }
    }
    std::string missing;
    for (const std::string& want : required) {
        if (std::find(list.names.begin(), list.names.end(), want) == list.names.end()) {
            if (!missing.empty()) {
                missing += ",";
            }
            missing += want;
        }
    }
    const bool ok = !list.names.empty() && list.types_valid && !empty_name && missing.empty();
    std::string detail = "encoder=" + chosen_name + " params=" + std::to_string(list.names.size()) +
                         " types_valid=" + (list.types_valid ? "1" : "0") +
                         " names_ok=" + (empty_name ? "0" : "1");
    detail += missing.empty() ? (" required=all(" + join_names(required, 8) + ")")
                              : (" missing=" + missing);
    detail += " list=" + join_names(list.names, 12);
    report(name, ok, detail);
}

void check_libwebp() {
    const int v = WebPGetEncoderVersion();
    report("libwebp", v > 0, "WebPGetEncoderVersion=" + std::to_string(v));
}

}  // namespace

int main() {
    check_lcms2();
    check_exiv2();
    check_exiv2_bmff();
    check_oiio_plugins();
    check_jpegli();
    check_libjxl();
    check_libheif("libheif-hevc", heif_compression_HEVC);
    check_libheif("libheif-av1", heif_compression_AV1);
    // R26/R27: encoder parameter introspection (SVT-AV1 "threads" + x265 name surface).
    check_heif_params("libheif-params-hevc", heif_compression_HEVC, "x265",
                      {"quality", "lossless", "preset"});
    check_heif_params("libheif-params-av1", heif_compression_AV1, "svt", {"threads", "quality"});
    check_libwebp();

    std::printf("PLUGINS: %s\n", g_format_list.empty() ? "(none)" : g_format_list.c_str());
    std::printf("HEIF_ENCODERS: %s\n", join(g_heif_encoders).c_str());
    return g_fails;
}
