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

void check_exiv2_bmff() {
#if defined(EXIV2_TEST_VERSION) && EXIV2_TEST_VERSION(0, 28, 0)
    // Exiv2 >= 0.28 API (verified against the installed headers).
    const bool ok = Exiv2::enableBMFF(true);
    report("exiv2-bmff", ok, ok ? "enableBMFF(true)=1" : "enableBMFF(true)=0");
#else
    report("exiv2-bmff", false, "API missing");
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

std::vector<std::string> heif_encoder_names(heif_compression_format fmt) {
    std::vector<std::string> names;
    // libheif >= 1.4 exposes the 4-argument free function
    // heif_get_encoder_descriptors(format, name, out, count), not the 6-argument context form
    // assumed by the task book (verified against the installed headers).
    int count = heif_get_encoder_descriptors(fmt, nullptr, nullptr, 0);
    if (count <= 0) {
        return names;
    }
    std::vector<const heif_encoder_descriptor*> descs(static_cast<size_t>(count), nullptr);
    count = heif_get_encoder_descriptors(fmt, nullptr, descs.data(), count);
    for (int i = 0; i < count; ++i) {
        const char* n = heif_encoder_descriptor_get_name(descs[static_cast<size_t>(i)]);
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
    check_libwebp();

    std::printf("PLUGINS: %s\n", g_format_list.empty() ? "(none)" : g_format_list.c_str());
    std::printf("HEIF_ENCODERS: %s\n", join(g_heif_encoders).c_str());
    return g_fails;
}
