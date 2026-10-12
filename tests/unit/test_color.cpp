// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T4 — ColorManager unit tests (hand-written assertions, no GTest).
//
// Contract: docs/m1-tasks.md §3.6 (PP-FROZEN header) / §4.4 (unit test list) / §7 facts.
// Golden values: M0 spike e, Lab D50 = (100, 7.57e-06, -7.63e-06).
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <lcms2.h>
#include <lcms2_plugin.h> // _cmsMAT3inverse/_cmsMAT3eval: white-point round trip (M2-T6)

#include <OpenImageIO/color.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/span.h>

#include <libheif/heif.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "codecs/encoder.h"
#include "codecs/encoders.h"
#include "core/colormanager.h"
#include "core/pipeline.h"

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
        std::fflush(stdout);
    }
}

// Measured values kept in the test output as evidence for the task report.
void info(const std::string &text) {
    std::printf("info %s\n", text.c_str());
    std::fflush(stdout);
}

std::string fnum(double v) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.9g", v);
    return b;
}

// ---------------------------------------------------------------- fixtures --

OIIO::ImageBuf make_buf(int w, int h, int nch, const std::vector<float> &px) {
    OIIO::ImageSpec spec(w, h, nch, OIIO::TypeFloat);
    OIIO::ImageBuf buf(spec);
    OIIO::ROI roi(0, w, 0, h, 0, 1, 0, nch);
    buf.set_pixels(roi, OIIO::TypeFloat,
                   OIIO::span<const std::byte>(reinterpret_cast<const std::byte *>(px.data()),
                                               px.size() * sizeof(float)));
    return buf;
}

std::vector<float> get_all(const OIIO::ImageBuf &buf) {
    const OIIO::ImageSpec &s = buf.spec();
    std::vector<float> out(std::size_t(s.width) * std::size_t(s.height) * std::size_t(s.nchannels));
    OIIO::ROI roi(0, s.width, 0, s.height, 0, 1, 0, s.nchannels);
    const bool ok = buf.get_pixels(roi, OIIO::TypeFloat,
                                   OIIO::span<std::byte>(reinterpret_cast<std::byte *>(out.data()),
                                                         out.size() * sizeof(float)));
    check(ok, "fixture/get_pixels", "ImageBuf::get_pixels failed");
    return out;
}

// Thread-safe variant of get_all(): no assertions (used inside worker threads).
std::vector<float> get_all_quiet(const OIIO::ImageBuf &buf) {
    const OIIO::ImageSpec &s = buf.spec();
    std::vector<float> out(std::size_t(s.width) * std::size_t(s.height) * std::size_t(s.nchannels));
    OIIO::ROI roi(0, s.width, 0, s.height, 0, 1, 0, s.nchannels);
    buf.get_pixels(roi, OIIO::TypeFloat,
                   OIIO::span<std::byte>(reinterpret_cast<std::byte *>(out.data()),
                                         out.size() * sizeof(float)));
    return out;
}

std::string serialize(cmsHPROFILE p) {
    cmsUInt32Number need = 0;
    if (!cmsSaveProfileToMem(p, nullptr, &need) || need == 0)
        return {};
    std::string out(need, '\0');
    cmsUInt32Number written = need;
    if (!cmsSaveProfileToMem(p, out.data(), &written) || written == 0)
        return {};
    out.resize(written <= need ? written : need);
    return out;
}

std::string srgb_icc_bytes() {
    cmsHPROFILE p = cmsCreate_sRGBProfile();
    check(p != nullptr, "fixture/srgb-profile", "cmsCreate_sRGBProfile returned null");
    std::string b = serialize(p);
    if (p)
        cmsCloseProfile(p);
    check(!b.empty(), "fixture/srgb-serialize", "cmsSaveProfileToMem produced no bytes");
    return b;
}

// Grayscale ICC with D65 white point and a plain gamma TRC (used as a *source*
// profile; gamma differences also give us cheap distinct cache keys).
std::string gray_icc_bytes(double gamma) {
    const cmsCIExyY d65{0.3127, 0.3290, 1.0};
    cmsToneCurve *trc = cmsBuildGamma(nullptr, gamma);
    cmsHPROFILE p = trc ? cmsCreateGrayProfile(&d65, trc) : nullptr;
    if (trc)
        cmsFreeToneCurve(trc);
    check(p != nullptr, "fixture/gray-profile", "cmsCreateGrayProfile returned null");
    std::string b = serialize(p);
    if (p)
        cmsCloseProfile(p);
    return b;
}

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

bool has_warning(const pp::ColorOutcome &oc, pp::WarningKind kind) {
    for (const auto &w : oc.warnings)
        if (w.kind == kind)
            return true;
    return false;
}

double max_abs_diff(const std::vector<float> &a, const std::vector<float> &b) {
    double m = 0.0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        m = std::max(m, std::fabs(double(a[i]) - double(b[i])));
    return m;
}

pp::ColorManager &cm() { return pp::ColorManager::instance(); }

// ------------------------------------------------------------------- cases --

void test_enum_strings() {
    using pp::ColorTarget;
    check(pp::to_string(ColorTarget::KeepOriginal) == "keep", "enum/to_string-keep",
          pp::to_string(ColorTarget::KeepOriginal));
    check(pp::to_string(ColorTarget::SRGB) == "srgb", "enum/to_string-srgb",
          pp::to_string(ColorTarget::SRGB));
    check(pp::to_string(ColorTarget::DisplayP3) == "p3", "enum/to_string-p3",
          pp::to_string(ColorTarget::DisplayP3));
    check(pp::to_string(ColorTarget::AdobeRGB) == "adobergb", "enum/to_string-adobergb",
          pp::to_string(ColorTarget::AdobeRGB));

    ColorTarget t = ColorTarget::SRGB;
    check(pp::parse_color_target("keep", t) && t == ColorTarget::KeepOriginal, "enum/parse-keep",
          "");
    check(pp::parse_color_target("srgb", t) && t == ColorTarget::SRGB, "enum/parse-srgb", "");
    check(pp::parse_color_target("p3", t) && t == ColorTarget::DisplayP3, "enum/parse-p3", "");
    check(pp::parse_color_target("adobergb", t) && t == ColorTarget::AdobeRGB,
          "enum/parse-adobergb", "");
    check(!pp::parse_color_target("displayp3", t), "enum/parse-unknown", "accepted 'displayp3'");
    check(!pp::parse_color_target("", t), "enum/parse-empty", "accepted empty string");
    check(!pp::parse_color_target("keep ", t), "enum/parse-trailing-space", "accepted 'keep '");
}

void test_srgb_to_srgb_identity() {
    const std::string icc = srgb_icc_bytes();
    const std::vector<float> px = {0.0f, 0.0f, 0.0f, 0.25f, 0.5f, 0.75f, 0.5f,   0.5f,   0.5f,
                                   1.0f, 1.0f, 1.0f, 0.9f,  0.1f, 0.33f, 0.017f, 0.404f, 0.777f};
    OIIO::ImageBuf buf = make_buf(6, 1, 3, px);
    cm().clear_cache();
    const pp::ColorOutcome oc = cm().transform(buf, icc, false, pp::ColorTarget::SRGB);
    check(oc.error.empty(), "identity/error", oc.error);
    check(oc.warnings.empty(), "identity/no-warnings",
          "unexpected warning count " + std::to_string(oc.warnings.size()));
    check(buf.nchannels() == 3, "identity/channels", std::to_string(buf.nchannels()));
    const std::vector<float> out = get_all(buf);
    const double d = max_abs_diff(px, out);
    info("identity(sRGB ICC -> sRGB) max_err=" + fnum(d));
    check(d <= 1e-5, "identity/max-err<=1e-5", "max_err=" + fnum(d));
    check(oc.src_desc == "ICC(sRGB built-in)", "identity/src-desc", oc.src_desc);
    check(oc.dst_desc == "sRGB", "identity/dst-desc", oc.dst_desc);
    check(!oc.icc_to_embed.empty(), "identity/icc-to-embed", "empty target ICC bytes");
}

void test_assumed_srgb_identity_and_warning() {
    // No ICC at all on an RGB buffer: assumed sRGB + Warning{NoIccAssumeSrgb};
    // the pixels must still be a <=1e-5 identity.
    const std::vector<float> px = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    OIIO::ImageBuf buf = make_buf(2, 1, 3, px);
    cm().clear_cache();
    const pp::ColorOutcome oc = cm().transform(buf, "", false, pp::ColorTarget::SRGB);
    check(oc.error.empty(), "assumed-srgb/error", oc.error);
    check(has_warning(oc, pp::WarningKind::NoIccAssumeSrgb), "assumed-srgb/warning",
          "NoIccAssumeSrgb missing");
    check(oc.src_desc == "assumed sRGB", "assumed-srgb/src-desc", oc.src_desc);
    const double d = max_abs_diff(px, get_all(buf));
    info("identity(assumed sRGB -> sRGB) max_err=" + fnum(d));
    check(d <= 1e-5, "assumed-srgb/max-err<=1e-5", "max_err=" + fnum(d));
    check(!oc.icc_to_embed.empty(), "assumed-srgb/icc-to-embed", "empty target ICC bytes");
}

void test_white_to_p3_and_adobergb() {
    const std::string icc = srgb_icc_bytes();
    const struct {
        pp::ColorTarget target;
        const char *name;
        const char *dst_desc;
    } cases[] = {{pp::ColorTarget::DisplayP3, "p3", "ICC(Display P3)"},
                 {pp::ColorTarget::AdobeRGB, "adobergb", "ICC(Adobe RGB (1998))"}};

    for (const auto &c : cases) {
        OIIO::ImageBuf buf = make_buf(2, 1, 3, {1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f});
        cm().clear_cache();
        const pp::ColorOutcome oc = cm().transform(buf, icc, false, c.target);
        const std::string tag = std::string("white-to-") + c.name;
        check(oc.error.empty(), tag + "/error", oc.error);
        check(oc.dst_desc == c.dst_desc, tag + "/dst-desc", oc.dst_desc);
        const std::vector<float> out = get_all(buf);
        const double lo = std::min({double(out[0]), double(out[1]), double(out[2])});
        const double hi = std::max({double(out[0]), double(out[1]), double(out[2])});
        info(tag + " white=(" + fnum(out[0]) + "," + fnum(out[1]) + "," + fnum(out[2]) +
             ") midgray=(" + fnum(out[3]) + "," + fnum(out[4]) + "," + fnum(out[5]) + ")");
        check(lo > 0.9, tag + "/white>0.9",
              "rgb=(" + fnum(out[0]) + "," + fnum(out[1]) + "," + fnum(out[2]) + ")");
        check(hi - lo <= 0.01, tag + "/white-neutral", "spread=" + fnum(hi - lo));
        check(std::fabs(lo - 1.0) <= 0.01, tag + "/white-near-1.0",
              "rgb=(" + fnum(out[0]) + "," + fnum(out[1]) + "," + fnum(out[2]) + ")");
        // Mid gray must move off 0.5 (wider gamut / different TRC) but stay sane.
        const double g = out[3];
        check(g > 0.45 && g < 0.56, tag + "/midgray-range",
              "g=(" + fnum(out[3]) + "," + fnum(out[4]) + "," + fnum(out[5]) + ")");
    }
}

// The frozen interface has no Lab target, so the D50 golden value is checked with
// the very same lcms2 intent/flags the module uses (M0 spike e reproduced).
void test_lab_d50_golden() {
    cmsHPROFILE srgb = cmsCreate_sRGBProfile();
    cmsHPROFILE lab = cmsCreateLab4Profile(cmsD50_xyY());
    check(srgb && lab, "lab/profiles", "cmsCreate_sRGBProfile/cmsCreateLab4Profile failed");
    cmsHTRANSFORM xf =
        cmsCreateTransform(srgb, TYPE_RGB_FLT, lab, TYPE_Lab_DBL, INTENT_RELATIVE_COLORIMETRIC,
                           cmsFLAGS_BLACKPOINTCOMPENSATION);
    check(xf != nullptr, "lab/transform", "cmsCreateTransform(sRGB->Lab D50) failed");
    const float white[3] = {1.0f, 1.0f, 1.0f};
    double lab_out[3] = {0.0, 0.0, 0.0};
    if (xf)
        cmsDoTransform(xf, white, lab_out, 1);
    const std::string vals =
        "Lab=(" + fnum(lab_out[0]) + "," + fnum(lab_out[1]) + "," + fnum(lab_out[2]) + ")";
    info("D50 golden " + vals + " (M0 spike e: (100, 7.57e-06, -7.63e-06))");
    check(lab_out[0] >= 99.5 && lab_out[0] <= 100.5, "lab/L-in-[99.5,100.5]", vals);
    check(std::fabs(lab_out[1] - 7.57e-06) <= 1e-4, "lab/a-golden", vals);
    check(std::fabs(lab_out[2] - (-7.63e-06)) <= 1e-4, "lab/b-golden", vals);
    if (xf)
        cmsDeleteTransform(xf);
    if (lab)
        cmsCloseProfile(lab);
    if (srgb)
        cmsCloseProfile(srgb);
}

void test_gray_to_rgb() {
    // (a) gray 0.5, no ICC, gray sRGB assumption -> 3 equal channels, value ~0.5,
    //     and no NoIccAssumeSrgb warning (gray ruling §4.4).
    {
        OIIO::ImageBuf buf = make_buf(2, 1, 1, {0.5f, 0.25f});
        cm().clear_cache();
        const pp::ColorOutcome oc = cm().transform(buf, "", true, pp::ColorTarget::SRGB);
        check(oc.error.empty(), "gray-rgb/error", oc.error);
        check(oc.src_desc == "assumed gray sRGB", "gray-rgb/src-desc", oc.src_desc);
        check(!has_warning(oc, pp::WarningKind::NoIccAssumeSrgb), "gray-rgb/no-warning",
              "gray source must not raise NoIccAssumeSrgb");
        check(buf.nchannels() == 3, "gray-rgb/out-channels", std::to_string(buf.nchannels()));
        const std::vector<float> out = get_all(buf);
        check(out.size() == 6, "gray-rgb/out-size", std::to_string(out.size()));
        for (int i = 0; i < 2; ++i) {
            const double r = out[3 * i + 0], g = out[3 * i + 1], b = out[3 * i + 2];
            check(std::fabs(r - g) <= 1e-6 && std::fabs(g - b) <= 1e-6, "gray-rgb/neutral",
                  "pixel " + std::to_string(i) + " = (" + fnum(r) + "," + fnum(g) + "," + fnum(b) +
                      ")");
        }
        check(std::fabs(double(out[0]) - 0.5) <= 2e-3, "gray-rgb/0.5-value",
              "g=(" + fnum(out[0]) + "," + fnum(out[1]) + "," + fnum(out[2]) + ")");
        check(std::fabs(double(out[3]) - 0.25) <= 2e-3, "gray-rgb/0.25-value",
              "g=(" + fnum(out[3]) + "," + fnum(out[4]) + "," + fnum(out[5]) + ")");
    }
    // (b) same gray plane to Display P3: still neutral, still ~0.5.
    {
        OIIO::ImageBuf buf = make_buf(1, 1, 1, {0.5f});
        cm().clear_cache();
        const pp::ColorOutcome oc = cm().transform(buf, "", true, pp::ColorTarget::DisplayP3);
        check(oc.error.empty(), "gray-p3/error", oc.error);
        check(buf.nchannels() == 3, "gray-p3/out-channels", std::to_string(buf.nchannels()));
        const std::vector<float> out = get_all(buf);
        const double lo = std::min({double(out[0]), double(out[1]), double(out[2])});
        const double hi = std::max({double(out[0]), double(out[1]), double(out[2])});
        info("gray 0.5 -> P3 = (" + fnum(out[0]) + "," + fnum(out[1]) + "," + fnum(out[2]) +
             ") spread=" + fnum(hi - lo));
        // Measured spread is ~8.2e-6 (float rounding through the gray->PCS->P3
        // matrix path); the three channels must stay equal well below 1e-4.
        check(hi - lo <= 1e-4, "gray-p3/neutral", "spread=" + fnum(hi - lo));
        check(lo > 0.48 && hi < 0.52, "gray-p3/value",
              "(" + fnum(out[0]) + "," + fnum(out[1]) + "," + fnum(out[2]) + ")");
    }
    // (c) an explicit gray ICC (gamma 2.2) must actually be used: 0.5 in gamma 2.2
    //     becomes ~0.5038 in sRGB, i.e. noticeably brighter than an identity.
    {
        const std::string gray_icc = gray_icc_bytes(2.2);
        check(!gray_icc.empty(), "gray-icc/serialize", "empty gray ICC bytes");
        OIIO::ImageBuf buf = make_buf(1, 1, 1, {0.5f});
        cm().clear_cache();
        const pp::ColorOutcome oc = cm().transform(buf, gray_icc, true, pp::ColorTarget::SRGB);
        check(oc.error.empty(), "gray-icc/error", oc.error);
        const std::vector<float> out = get_all(buf);
        check(out.size() == 3, "gray-icc/out-size", std::to_string(out.size()));
        check(out[0] > 0.5005 && out[0] < 0.52, "gray-icc/gamma22-to-srgb",
              "v=" + fnum(out[0]) + " (expected ~0.5038)");
    }
}

void test_gray_alpha() {
    // 2 channels (gray+alpha) -> 4 channels (RGBA): gray plane transformed in one
    // GRAY_FLT->RGB_FLT call, alpha copied bit-exactly.
    const std::vector<float> px = {0.5f, 1.0f, 0.25f, 0.5f};
    OIIO::ImageBuf buf = make_buf(2, 1, 2, px);
    cm().clear_cache();
    const pp::ColorOutcome oc = cm().transform(buf, "", true, pp::ColorTarget::SRGB);
    check(oc.error.empty(), "graya/error", oc.error);
    check(!has_warning(oc, pp::WarningKind::NoIccAssumeSrgb), "graya/no-warning",
          "gray+alpha source must not raise NoIccAssumeSrgb");
    check(buf.nchannels() == 4, "graya/out-channels", std::to_string(buf.nchannels()));
    const std::vector<float> out = get_all(buf);
    check(out.size() == 8, "graya/out-size", std::to_string(out.size()));
    for (int i = 0; i < 2; ++i) {
        check(same_bits(out[4 * i + 3], px[2 * i + 1]), "graya/alpha-bit-exact",
              "pixel " + std::to_string(i) + " alpha " + fnum(out[4 * i + 3]) +
                  " != " + fnum(px[2 * i + 1]));
        check(std::fabs(double(out[4 * i]) - double(px[2 * i])) <= 2e-3, "graya/gray-value",
              "pixel " + std::to_string(i) + " gray " + fnum(out[4 * i]));
    }
}

void test_rgba_alpha_bit_exact() {
    const std::string icc = srgb_icc_bytes();
    const std::vector<float> px = {1.0f,  1.0f,   1.0f, 1.0f, 0.25f, 0.5f,
                                   0.75f, 0.125f, 0.0f, 0.0f, 0.0f,  0.0f};
    OIIO::ImageBuf buf = make_buf(3, 1, 4, px);
    cm().clear_cache();
    const pp::ColorOutcome oc = cm().transform(buf, icc, false, pp::ColorTarget::DisplayP3);
    check(oc.error.empty(), "rgba/error", oc.error);
    check(buf.nchannels() == 4, "rgba/out-channels", std::to_string(buf.nchannels()));
    const std::vector<float> out = get_all(buf);
    for (int i = 0; i < 3; ++i) {
        check(same_bits(out[4 * i + 3], px[4 * i + 3]), "rgba/alpha-bit-exact",
              "pixel " + std::to_string(i) + " alpha " + fnum(out[4 * i + 3]) +
                  " != " + fnum(px[4 * i + 3]));
    }
    check(out[0] > 0.99 && out[1] > 0.99 && out[2] > 0.99, "rgba/white-converted",
          "(" + fnum(out[0]) + "," + fnum(out[1]) + "," + fnum(out[2]) + ")");
}

void test_keep_original() {
    const std::string icc = srgb_icc_bytes();
    const std::vector<float> px = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
    // (a) source ICC present: pixels untouched, ICC re-embedded verbatim.
    {
        OIIO::ImageBuf buf = make_buf(2, 1, 3, px);
        cm().clear_cache();
        const pp::ColorOutcome oc = cm().transform(buf, icc, false, pp::ColorTarget::KeepOriginal);
        check(oc.error.empty(), "keep/error", oc.error);
        check(oc.icc_to_embed == icc, "keep/icc-verbatim",
              "embedded bytes differ (" + std::to_string(oc.icc_to_embed.size()) + " vs " +
                  std::to_string(icc.size()) + ")");
        check(oc.dst_desc == "keep", "keep/dst-desc", oc.dst_desc);
        check(cm().cache_size() == 0, "keep/no-cache-entry",
              "cache grew to " + std::to_string(cm().cache_size()));
        const std::vector<float> out = get_all(buf);
        check(out.size() == px.size() && max_abs_diff(px, out) == 0.0, "keep/pixels-untouched",
              "max_err=" + fnum(max_abs_diff(px, out)));
    }
    // (b) no source ICC: nothing to embed, no warning, pixels untouched.
    {
        OIIO::ImageBuf buf = make_buf(2, 1, 3, px);
        const pp::ColorOutcome oc = cm().transform(buf, "", false, pp::ColorTarget::KeepOriginal);
        check(oc.error.empty(), "keep-noicc/error", oc.error);
        check(oc.icc_to_embed.empty(), "keep-noicc/no-embed",
              "got " + std::to_string(oc.icc_to_embed.size()) + " bytes");
        check(oc.warnings.empty(), "keep-noicc/no-warning",
              "unexpected warning count " + std::to_string(oc.warnings.size()));
        check(oc.src_desc == "assumed sRGB", "keep-noicc/src-desc", oc.src_desc);
        const std::vector<float> out = get_all(buf);
        check(out.size() == px.size() && max_abs_diff(px, out) == 0.0,
              "keep-noicc/pixels-untouched", "max_err=" + fnum(max_abs_diff(px, out)));
    }
    // (c) grayscale stays grayscale under KeepOriginal (no up-conversion).
    {
        OIIO::ImageBuf buf = make_buf(2, 1, 1, {0.25f, 0.75f});
        const pp::ColorOutcome oc = cm().transform(buf, "", true, pp::ColorTarget::KeepOriginal);
        check(oc.error.empty(), "keep-gray/error", oc.error);
        check(buf.nchannels() == 1, "keep-gray/channels", std::to_string(buf.nchannels()));
        check(oc.src_desc == "assumed gray sRGB", "keep-gray/src-desc", oc.src_desc);
    }
}

void test_transform_cache() {
    const std::string icc = srgb_icc_bytes();
    cm().clear_cache();
    check(cm().cache_size() == 0, "cache/clear", std::to_string(cm().cache_size()));

    OIIO::ImageBuf a = make_buf(1, 1, 3, {0.2f, 0.4f, 0.6f});
    check(cm().transform(a, icc, false, pp::ColorTarget::SRGB).error.empty(), "cache/call-a", "");
    check(cm().cache_size() == 1, "cache/miss-inserts", std::to_string(cm().cache_size()));

    OIIO::ImageBuf b = make_buf(2, 1, 3, {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f});
    check(cm().transform(b, icc, false, pp::ColorTarget::SRGB).error.empty(), "cache/call-b", "");
    check(cm().cache_size() == 1, "cache/same-key-no-growth", std::to_string(cm().cache_size()));

    // 4-channel input shares the very same RGB transform (alpha never reaches lcms2).
    OIIO::ImageBuf c = make_buf(1, 1, 4, {0.1f, 0.2f, 0.3f, 0.4f});
    check(cm().transform(c, icc, false, pp::ColorTarget::SRGB).error.empty(), "cache/call-c", "");
    check(cm().cache_size() == 1, "cache/rgba-shares-rgb-transform",
          std::to_string(cm().cache_size()));

    // Different target / different input channels -> new entries.
    OIIO::ImageBuf d = make_buf(1, 1, 3, {0.2f, 0.4f, 0.6f});
    check(cm().transform(d, icc, false, pp::ColorTarget::DisplayP3).error.empty(), "cache/call-d",
          "");
    check(cm().cache_size() == 2, "cache/target-in-key", std::to_string(cm().cache_size()));
    OIIO::ImageBuf e = make_buf(1, 1, 1, {0.5f});
    check(cm().transform(e, "", true, pp::ColorTarget::SRGB).error.empty(), "cache/call-e", "");
    check(cm().cache_size() == 3, "cache/channels-and-assumption-in-key",
          std::to_string(cm().cache_size()));

    // Capacity is 16: 17 further distinct keys must not grow past it.
    for (int i = 0; i < 17; ++i) {
        const std::string gi = gray_icc_bytes(1.0 + 0.05 * i);
        OIIO::ImageBuf g = make_buf(1, 1, 1, {0.5f});
        const pp::ColorOutcome oc = cm().transform(g, gi, true, pp::ColorTarget::SRGB);
        check(oc.error.empty(), "cache/fill-" + std::to_string(i), oc.error);
    }
    check(cm().cache_size() == 16, "cache/capacity-16", std::to_string(cm().cache_size()));

    // A repeated key after eviction is a miss that stays at capacity.
    OIIO::ImageBuf h = make_buf(1, 1, 3, {0.2f, 0.4f, 0.6f});
    check(cm().transform(h, icc, false, pp::ColorTarget::SRGB).error.empty(), "cache/call-h", "");
    check(cm().cache_size() == 16, "cache/refill-stays-at-capacity",
          std::to_string(cm().cache_size()));
    cm().clear_cache();
}

void test_target_profile_generation() {
    std::string err;

    // sRGB / KeepOriginal have no loadable file (built-in / nothing to load).
    check(pp::load_target_icc(pp::ColorTarget::SRGB, err).empty() && err.empty(),
          "profile/srgb-empty", err);
    check(pp::load_target_icc(pp::ColorTarget::KeepOriginal, err).empty() && err.empty(),
          "profile/keep-empty", err);

    const struct {
        pp::ColorTarget target;
        const char *desc;
        double trc_at_half; // EOTF(0.5): sRGB ~0.21404, gamma 2.19921875 ~0.21774
    } cases[] = {{pp::ColorTarget::DisplayP3, "Display P3", 0.21404},
                 {pp::ColorTarget::AdobeRGB, "Adobe RGB (1998)", 0.21774}};

    for (const auto &c : cases) {
        const std::string tag = std::string("profile/") + c.desc;
        const std::string bytes = pp::load_target_icc(c.target, err);
        check(err.empty(), tag + "-err", err);
        check(!bytes.empty(), tag + "-bytes", "no ICC bytes generated");

        cmsHPROFILE p = cmsOpenProfileFromMem(bytes.data(), cmsUInt32Number(bytes.size()));
        check(p != nullptr, tag + "-parse", "cmsOpenProfileFromMem failed");
        if (!p)
            continue;
        check(cmsGetColorSpace(p) == cmsSigRgbData, tag + "-colorspace",
              "not RGB (" + std::to_string(int(cmsGetColorSpace(p))) + ")");

        char desc[128] = {};
        cmsGetProfileInfoASCII(p, cmsInfoDescription, "en", "US", desc, sizeof(desc) - 1);
        check(std::string(desc) == c.desc, tag + "-description", std::string(desc));

        const auto *chroma =
            static_cast<const cmsCIExyYTRIPLE *>(cmsReadTag(p, cmsSigChromaticityTag));
        check(chroma != nullptr, tag + "-chromaticity", "tag missing");
        if (chroma) {
            check(std::fabs(chroma->Red.x -
                            (c.target == pp::ColorTarget::DisplayP3 ? 0.680 : 0.640)) < 1e-4,
                  tag + "-red-x", fnum(chroma->Red.x));
            check(std::fabs(chroma->Green.y -
                            (c.target == pp::ColorTarget::DisplayP3 ? 0.690 : 0.710)) < 1e-4,
                  tag + "-green-y", fnum(chroma->Green.y));
            check(std::fabs(chroma->Blue.x - 0.150) < 1e-4 &&
                      std::fabs(chroma->Blue.y - 0.060) < 1e-4,
                  tag + "-blue-xy", fnum(chroma->Blue.x) + "," + fnum(chroma->Blue.y));
        }
        const auto *trc = static_cast<const cmsToneCurve *>(cmsReadTag(p, cmsSigRedTRCTag));
        check(trc != nullptr, tag + "-trc", "red TRC tag missing");
        if (trc) {
            const double v = cmsEvalToneCurveFloat(const_cast<cmsToneCurve *>(trc), 0.5f);
            check(std::fabs(v - c.trc_at_half) < 0.0015, tag + "-trc-at-0.5",
                  "EOTF(0.5)=" + fnum(v) + " expected ~" + fnum(c.trc_at_half));
        }
        // Round trip: serializable again and still an RGB profile.
        const std::string again = serialize(p);
        check(!again.empty(), tag + "-reserialize", "second cmsSaveProfileToMem failed");
        cmsHPROFILE p2 = again.empty()
                             ? nullptr
                             : cmsOpenProfileFromMem(again.data(), cmsUInt32Number(again.size()));
        check(p2 != nullptr && cmsGetColorSpace(p2) == cmsSigRgbData, tag + "-reparse",
              "re-parsed profile is not RGB");
        if (p2)
            cmsCloseProfile(p2);
        cmsCloseProfile(p);

        // load_target_icc is generated once and cached (identical bytes).
        const std::string bytes2 = pp::load_target_icc(c.target, err);
        check(bytes2 == bytes, tag + "-cached", "bytes differ between calls");
    }
}

void test_errors() {
    cm().clear_cache();
    // Unparsable source ICC: error, pixels untouched, no cache entry.
    const std::vector<float> px = {0.1f, 0.2f, 0.3f};
    OIIO::ImageBuf buf = make_buf(1, 1, 3, px);
    const pp::ColorOutcome oc = cm().transform(buf, std::string("\x01\x02\x03\x04not-an-icc"),
                                               false, pp::ColorTarget::SRGB);
    check(!oc.error.empty(), "error/bad-icc", "no error for garbage ICC bytes");
    check(cm().cache_size() == 0, "error/bad-icc-no-cache",
          "cache grew to " + std::to_string(cm().cache_size()));
    const std::vector<float> after = get_all(buf);
    check(max_abs_diff(px, after) == 0.0, "error/bad-icc-pixels-untouched",
          "max_err=" + fnum(max_abs_diff(px, after)));

#ifdef NDEBUG
    // Channel counts outside {1,2,3,4} are an internal invariant (assert in debug
    // builds); release builds must return an error instead of touching memory.
    OIIO::ImageBuf bad = make_buf(1, 1, 5, {0.1f, 0.2f, 0.3f, 0.4f, 0.5f});
    const pp::ColorOutcome oc5 = cm().transform(bad, "", false, pp::ColorTarget::SRGB);
    check(!oc5.error.empty(), "error/5-channels", "no error for a 5-channel buffer");
#endif
}

// The pipeline runs one colour transform per worker thread on a shared cache;
// this is a light stress test of the LRU + shared transform handles. Assertions
// run only on the main thread (check() is not thread safe).
void test_concurrent_transforms() {
    const std::string icc = srgb_icc_bytes();
    constexpr int kThreads = 8;
    constexpr int kIters = 40;
    constexpr int kPixels = 4;
    cm().clear_cache();
    std::atomic<int> errors{0};
    std::atomic<int> wrong{0};
    std::vector<std::thread> pool;
    pool.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        pool.emplace_back([&, t] {
            for (int i = 0; i < kIters; ++i) {
                const float v = 0.125f * float((t + i) % 8);
                std::vector<float> px(std::size_t(kPixels) * 3);
                for (int p = 0; p < kPixels; ++p) {
                    px[3 * p + 0] = v;
                    px[3 * p + 1] = 1.0f - v;
                    px[3 * p + 2] = 0.5f * v;
                }
                OIIO::ImageBuf buf = make_buf(kPixels, 1, 3, px);
                const pp::ColorTarget target =
                    (i % 3 == 0)
                        ? pp::ColorTarget::SRGB
                        : ((i % 3 == 1) ? pp::ColorTarget::DisplayP3 : pp::ColorTarget::AdobeRGB);
                const pp::ColorOutcome oc = cm().transform(buf, icc, false, target);
                if (!oc.error.empty()) {
                    ++errors;
                    continue;
                }
                const std::vector<float> out = get_all_quiet(buf);
                if (out.size() != px.size()) {
                    ++errors;
                    continue;
                }
                if (target == pp::ColorTarget::SRGB) {
                    // canonical sRGB source -> exact identity
                    for (std::size_t k = 0; k < px.size(); ++k)
                        if (std::fabs(double(out[k]) - double(px[k])) > 1e-5)
                            ++wrong;
                } else {
                    for (float o : out)
                        if (!std::isfinite(o) || o < -0.01f || o > 1.01f)
                            ++wrong;
                }
            }
        });
    }
    for (auto &th : pool)
        th.join();
    check(errors.load() == 0, "concurrent/errors", std::to_string(errors.load()));
    check(wrong.load() == 0, "concurrent/pixel-values", std::to_string(wrong.load()));
    check(cm().cache_size() <= 16, "concurrent/cache-capacity", std::to_string(cm().cache_size()));
    info("concurrent: " + std::to_string(kThreads) + " threads x " + std::to_string(kIters) +
         " transforms, cache_size=" + std::to_string(cm().cache_size()));
    cm().clear_cache();
}

// ------------------------------------------------- M2-T6 CICP mapping (§2.8) --

// Round-trip facts of a serialized profile: the white point recovered from the stored
// chromatic-adaptation (chad) matrix — lcms2 adapts colourants and mediaWhitePointTag to
// the D50 PCS by design, so the original white point is exactly chad^-1 * D50 — plus the
// PrimaryChromaticities tag and the red TRC evaluated at 0.5.
struct ProfileFacts {
    bool rgb = false;
    bool chad = false;
    double wx = 0, wy = 0;
    double rx = 0, ry = 0, gx = 0, gy = 0, bx = 0, by = 0;
    double trc_at_half = -1.0;
};

bool profile_facts(const std::string &bytes, ProfileFacts &f) {
    cmsHPROFILE p = cmsOpenProfileFromMem(bytes.data(), cmsUInt32Number(bytes.size()));
    if (p == nullptr)
        return false;
    f.rgb = (cmsGetColorSpace(p) == cmsSigRgbData);
    const auto *chad = static_cast<const cmsMAT3 *>(cmsReadTag(p, cmsSigChromaticAdaptationTag));
    if (chad != nullptr) {
        cmsMAT3 inv;
        if (_cmsMAT3inverse(chad, &inv)) {
            const cmsCIEXYZ *d50 = cmsD50_XYZ();
            cmsVEC3 in, out;
            in.n[0] = d50->X;
            in.n[1] = d50->Y;
            in.n[2] = d50->Z;
            _cmsMAT3eval(&out, &inv, &in);
            const double sum = out.n[0] + out.n[1] + out.n[2];
            if (sum > 0.0) {
                f.chad = true;
                f.wx = out.n[0] / sum;
                f.wy = out.n[1] / sum;
            }
        }
    }
    const auto *ch = static_cast<const cmsCIExyYTRIPLE *>(cmsReadTag(p, cmsSigChromaticityTag));
    if (ch != nullptr) {
        f.rx = ch->Red.x;
        f.ry = ch->Red.y;
        f.gx = ch->Green.x;
        f.gy = ch->Green.y;
        f.bx = ch->Blue.x;
        f.by = ch->Blue.y;
    }
    const auto *trc = static_cast<const cmsToneCurve *>(cmsReadTag(p, cmsSigRedTRCTag));
    if (trc != nullptr) {
        f.trc_at_half = cmsEvalToneCurveFloat(const_cast<cmsToneCurve *>(trc), 0.5f);
    }
    cmsCloseProfile(p);
    return true;
}

std::string cicp_tag(int p, int t) { return "cicp/" + std::to_string(p) + "," + std::to_string(t); }

// §2.8 frozen enumeration: exactly (1,13) (12,13) (12,1) (9,8) (9,13) are mapped. The
// constructed source profiles are serialized and re-read with lcms2 (round trip) and the
// primaries / D65 white point must match the standard values within 2/255; the TRC must
// distinguish sRGB (13) from gamma 2.2 (1) and from linear (8).
void test_cicp_mapping() {
    const double tol = 2.0 / 255.0; // T6 §4: primaries/white point tolerance <= 2/255
    const double d65x = 0.3127, d65y = 0.3290;
    const double srgb_trc_half = 0.21404; // IEC 61966-2.1 EOTF(0.5)
    const double gamma22_half = 0.21764;  // 0.5^2.2
    constexpr double kNoPrimaries = 0.0;  // sRGB: lcms2 built-in, no generated profile

    const struct {
        int primaries, transfer;
        pp::CicpSource source;
        const char *name;
        bool has_icc;
        double rx, ry, gx, gy, bx, by, trc;
    } cases[] = {
        {1, 13, pp::CicpSource::Srgb, "sRGB", false, kNoPrimaries, 0, 0, 0, 0, 0, 0},
        {12, 13, pp::CicpSource::DisplayP3, "Display P3", true, 0.680, 0.320, 0.265, 0.690, 0.150,
         0.060, srgb_trc_half},
        {12, 1, pp::CicpSource::DisplayP3Gamma22, "Display P3", true, 0.680, 0.320, 0.265, 0.690,
         0.150, 0.060, gamma22_half},
        {9, 8, pp::CicpSource::Bt2020Linear, "BT.2020 linear", true, 0.708, 0.292, 0.170, 0.797,
         0.131, 0.046, 0.5},
        {9, 13, pp::CicpSource::Bt2020SrgbTrc, "BT.2020 sRGB-TRC", true, 0.708, 0.292, 0.170, 0.797,
         0.131, 0.046, srgb_trc_half},
    };

    for (const auto &c : cases) {
        pp::Cicp cicp;
        cicp.primaries = c.primaries;
        cicp.transfer = c.transfer;
        const pp::CicpMapping m = pp::map_cicp_source(cicp);
        const std::string tag = cicp_tag(c.primaries, c.transfer);
        const std::string want_log = "CICP (" + std::to_string(c.primaries) + "," +
                                     std::to_string(c.transfer) + ") → " + c.name;
        check(m.recognized(), tag + "-recognized", "pair not recognized");
        check(m.source == c.source, tag + "-source", "wrong CicpSource value");
        check(m.name == std::string(c.name), tag + "-name", m.name);
        check(m.log_line == want_log, tag + "-log-verbatim", m.log_line);
        check(m.src_icc.empty() != c.has_icc, tag + "-icc-presence",
              "src_icc " + std::to_string(m.src_icc.size()) +
                  "B (has_icc=" + (c.has_icc ? "true" : "false") + ")");
        if (!c.has_icc)
            continue; // (1,13): empty src_icc is the M1 assumed-sRGB behaviour
        ProfileFacts f;
        check(profile_facts(m.src_icc, f), tag + "-parse", "cmsOpenProfileFromMem failed");
        check(f.rgb, tag + "-rgb", "generated profile is not RGB");
        check(std::fabs(f.wx - d65x) <= tol && std::fabs(f.wy - d65y) <= tol,
              tag + "-whitepoint-d65", "xy=(" + fnum(f.wx) + "," + fnum(f.wy) + ")");
        check(std::fabs(f.rx - c.rx) <= tol && std::fabs(f.ry - c.ry) <= tol, tag + "-red",
              "xy=(" + fnum(f.rx) + "," + fnum(f.ry) + ") want (" + fnum(c.rx) + "," + fnum(c.ry) +
                  ")");
        check(std::fabs(f.gx - c.gx) <= tol && std::fabs(f.gy - c.gy) <= tol, tag + "-green",
              "xy=(" + fnum(f.gx) + "," + fnum(f.gy) + ") want (" + fnum(c.gx) + "," + fnum(c.gy) +
                  ")");
        check(std::fabs(f.bx - c.bx) <= tol && std::fabs(f.by - c.by) <= tol, tag + "-blue",
              "xy=(" + fnum(f.bx) + "," + fnum(f.by) + ") want (" + fnum(c.bx) + "," + fnum(c.by) +
                  ")");
        check(std::fabs(f.trc_at_half - c.trc) <= tol, tag + "-trc-at-0.5",
              "EOTF(0.5)=" + fnum(f.trc_at_half) + " want ~" + fnum(c.trc));
        info(tag + " -> " + m.name + " white=(" + fnum(f.wx) + "," + fnum(f.wy) + ") prim=(" +
             fnum(f.rx) + "," + fnum(f.ry) + ")/(" + fnum(f.gx) + "," + fnum(f.gy) + ")/(" +
             fnum(f.bx) + "," + fnum(f.by) + ") trc(0.5)=" + fnum(f.trc_at_half) +
             " icc=" + std::to_string(m.src_icc.size()) + "B");
    }

    // The generated profiles must be usable as a *source* in the real transform path:
    // 0.5 neutral is TRC-only (D65 both sides). BT.2020 linear 0.5 -> sRGB ~0.7354.
    {
        pp::Cicp lin;
        lin.primaries = 9;
        lin.transfer = 8;
        OIIO::ImageBuf buf = make_buf(1, 1, 3, {0.5f, 0.5f, 0.5f});
        cm().clear_cache();
        const pp::ColorOutcome oc =
            cm().transform(buf, pp::map_cicp_source(lin).src_icc, false, pp::ColorTarget::SRGB);
        check(oc.error.empty(), "cicp/9,8-transform-error", oc.error);
        const std::vector<float> out = get_all(buf);
        info("cicp/9,8 source profile (BT.2020 linear) 0.5 -> sRGB = " + fnum(out[0]) +
             " (expected ~0.7354)");
        check(std::fabs(double(out[0]) - 0.7354) <= 2e-3, "cicp/9,8-linear-0.5-to-srgb",
              "v=" + fnum(out[0]) + " (expected ~0.7354)");
        check(std::fabs(double(out[0]) - double(out[1])) <= 1e-5 &&
                  std::fabs(double(out[1]) - double(out[2])) <= 1e-5,
              "cicp/9,8-neutral", "spread!=0");
        check(oc.src_desc == "ICC(BT.2020 linear)", "cicp/9,8-src-desc", oc.src_desc);
    }
    {
        pp::Cicp p3;
        p3.primaries = 12;
        p3.transfer = 13;
        OIIO::ImageBuf buf = make_buf(1, 1, 3, {0.5f, 0.5f, 0.5f});
        cm().clear_cache();
        const pp::ColorOutcome oc =
            cm().transform(buf, pp::map_cicp_source(p3).src_icc, false, pp::ColorTarget::SRGB);
        check(oc.error.empty(), "cicp/12,13-transform-error", oc.error);
        const std::vector<float> out = get_all(buf);
        info("cicp/12,13 source profile (Display P3, sRGB TRC) 0.5 -> sRGB = " + fnum(out[0]) +
             " (expected ~0.5)");
        check(std::fabs(double(out[0]) - 0.5) <= 2e-3, "cicp/12,13-srgb-trc-identity",
              "v=" + fnum(out[0]) + " (expected ~0.5)");
        check(oc.src_desc == "ICC(Display P3)", "cicp/12,13-src-desc", oc.src_desc);
        cm().clear_cache();
    }

    // PQ(16) / HLG(18) and every other unlisted pair: not recognized, no profile bytes
    // (-> ColorManager's M1 assumed-sRGB branch) and the §2.8 log text verbatim.
    // UX-14 A5（M7-WP1）：日志口径随行为修正 —— 未支持组合**保留合成 ICC** 继续处理，
    // 原文案"按 sRGB 处理"谎报口径，改为如实描述（断言同严格度换锚，裁定登记见 handoff）。
    // D3（M7-R2A）：HDR 传输特性（PQ=16 / HLG=18，含 BT.2020/BT.2100 基组合）换锚为**明确**
    // HDR 文案（断言同严格度换锚，裁定登记 build/m7r2-a/handoff.md）；其余 miss 文案不动。
    const struct {
        int p, t;
        const char *log;
    } misses[] = {
        {9, 16, "HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释"},
        {9, 18, "HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释"},
        {12, 16, "HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释"},
        {12, 18, "HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释"},
        {10, 16, "HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释"},
        {10, 18, "HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释"},
        {1, 16, "HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释"},
        {1, 8, "CICP transfer 8 未支持，保留合成 ICC 处理（不按 sRGB 重解释）"},
        {9, 1, "CICP transfer 1 未支持，保留合成 ICC 处理（不按 sRGB 重解释）"},
        {11, 13, "CICP transfer 13 未支持，保留合成 ICC 处理（不按 sRGB 重解释）"},
    };
    for (const auto &c : misses) {
        pp::Cicp cicp;
        cicp.primaries = c.p;
        cicp.transfer = c.t;
        const pp::CicpMapping m = pp::map_cicp_source(cicp);
        const std::string tag = cicp_tag(c.p, c.t);
        check(!m.recognized(), tag + "-not-recognized", "unlisted pair was mapped");
        check(m.source == pp::CicpSource::Unsupported, tag + "-unsupported",
              "wrong CicpSource value");
        check(m.name.empty(), tag + "-no-name", m.name);
        check(m.src_icc.empty(), tag + "-srgb-branch",
              "src_icc=" + std::to_string(m.src_icc.size()) + "B (must stay empty)");
        check(m.log_line == std::string(c.log), tag + "-log-verbatim", m.log_line);
    }

    // §2.8: only the first two elements take part — matrix/full_range never change the result.
    const int quad[][2] = {{9, 13}, {12, 1}, {12, 13}, {9, 8}, {9, 16}, {5, 5}};
    for (const auto &pr : quad) {
        pp::Cicp base;
        base.primaries = pr[0];
        base.transfer = pr[1];
        const pp::CicpMapping ref = pp::map_cicp_source(base);
        const std::string tag = cicp_tag(pr[0], pr[1]);
        for (int matrix : {0, 1, 2}) {
            for (int full_range : {0, 1}) {
                pp::Cicp v = base;
                v.matrix = matrix;
                v.full_range = full_range;
                const pp::CicpMapping got = pp::map_cicp_source(v);
                check(got.source == ref.source && got.name == ref.name &&
                          got.src_icc == ref.src_icc && got.log_line == ref.log_line,
                      tag + "-matrix-fullrange-ignored",
                      "matrix=" + std::to_string(matrix) +
                          " full_range=" + std::to_string(full_range) + " changed the mapping");
            }
        }
        // Repeated calls are stable (generated-profile cache).
        check(pp::map_cicp_source(base).src_icc == ref.src_icc, tag + "-stable-bytes",
              "src_icc differs between calls");
    }
}

// ---------------------------------------------------------------------------
// M7-WP1 · UX-14 B1：ΔE2000 数值护栏（sRGB↔P3↔AdobeRGB 往返 ≤ 1.0）
//   改色彩面（A1 nclx / A2 降级）必须有数值标尺；粗断言（白>0.9、spread≤0.01）测不出漂移。
// ΔE2000 = CIEDE2000（Sharma et al. 2005 标准式）；Lab 由 lcms2（同管线同意图）给出，
// 前后两帧走同一 transform ⇒ 系统性差异抵消，剩下的就是往返漂移。
// ---------------------------------------------------------------------------
double delta_e2000(const double lab1[3], const double lab2[3]) {
    const double L1 = lab1[0], a1 = lab1[1], b1 = lab1[2];
    const double L2 = lab2[0], a2 = lab2[1], b2 = lab2[2];
    constexpr double kL = 1.0, kC = 1.0, kH = 1.0;
    const double C1 = std::hypot(a1, b1);
    const double C2 = std::hypot(a2, b2);
    const double Cbar = (C1 + C2) / 2.0;
    const double C7 = std::pow(Cbar, 7);
    const double G = 0.5 * (1.0 - std::sqrt(C7 / (C7 + std::pow(25.0, 7))));
    const double a1p = (1 + G) * a1;
    const double a2p = (1 + G) * a2;
    const double C1p = std::hypot(a1p, b1);
    const double C2p = std::hypot(a2p, b2);
    const auto hpf = [](double ap, double bb) {
        if (ap == 0.0 && bb == 0.0)
            return 0.0;
        const double h = std::atan2(bb, ap) * 180.0 / 3.14159265358979323846;
        return h < 0 ? h + 360.0 : h;
    };
    const double h1p = hpf(a1p, b1);
    const double h2p = hpf(a2p, b2);
    const double dLp = L2 - L1;
    const double dCp = C2p - C1p;
    double dhp = 0.0;
    if (C1p * C2p != 0.0) {
        dhp = h2p - h1p;
        if (dhp > 180.0)
            dhp -= 360.0;
        else if (dhp < -180.0)
            dhp += 360.0;
    }
    const double dHp =
        2.0 * std::sqrt(C1p * C2p) * std::sin(dhp / 2.0 * 3.14159265358979323846 / 180.0);
    const double Lbar = (L1 + L2) / 2.0;
    const double Cbarp = (C1p + C2p) / 2.0;
    double hbarp = h1p + h2p;
    if (C1p * C2p != 0.0) {
        const double diff = std::fabs(h1p - h2p);
        if (diff <= 180.0)
            hbarp = (h1p + h2p) / 2.0;
        else
            hbarp = (h1p + h2p < 360.0 ? (h1p + h2p + 360.0) : (h1p + h2p - 360.0)) / 2.0;
    }
    const double T = 1.0 - 0.17 * std::cos((hbarp - 30.0) * 3.14159265358979323846 / 180.0) +
                     0.24 * std::cos((2.0 * hbarp) * 3.14159265358979323846 / 180.0) +
                     0.32 * std::cos((3.0 * hbarp + 6.0) * 3.14159265358979323846 / 180.0) -
                     0.20 * std::cos((4.0 * hbarp - 63.0) * 3.14159265358979323846 / 180.0);
    const double dtheta = 30.0 * std::exp(-std::pow((hbarp - 275.0) / 25.0, 2));
    const double Rc =
        2.0 * std::sqrt(std::pow(Cbarp, 7) / (std::pow(Cbarp, 7) + std::pow(25.0, 7)));
    const double Sl =
        1.0 + (0.015 * std::pow(Lbar - 50.0, 2)) / std::sqrt(20.0 + std::pow(Lbar - 50.0, 2));
    const double Sc = 1.0 + 0.045 * Cbarp;
    const double Sh = 1.0 + 0.015 * Cbarp * T;
    const double Rt = -std::sin(2.0 * dtheta * 3.14159265358979323846 / 180.0) * Rc;
    return std::sqrt(std::pow(dLp / (kL * Sl), 2) + std::pow(dCp / (kC * Sc), 2) +
                     std::pow(dHp / (kH * Sh), 2) + Rt * (dCp / (kC * Sc)) * (dHp / (kH * Sh)));
}

// RGB（给定 profile）→ Lab D50（lcms2，RC+BPC 与主管线同口径）
bool rgb_to_lab(cmsHPROFILE prof, const double rgb[3], double lab[3]) {
    cmsHPROFILE labp = cmsCreateLab4Profile(nullptr);
    if (!labp)
        return false;
    cmsHTRANSFORM xf =
        cmsCreateTransform(prof, TYPE_RGB_DBL, labp, TYPE_Lab_DBL, INTENT_RELATIVE_COLORIMETRIC,
                           cmsFLAGS_BLACKPOINTCOMPENSATION);
    cmsCloseProfile(labp);
    if (!xf)
        return false;
    cmsDoTransform(xf, rgb, lab, 1);
    cmsDeleteTransform(xf);
    return true;
}

void test_de2000_roundtrip() {
    // 样本取**三空间共域内**颜色（sRGB 子集）—— 色域映射/硬裁不参与，纯测往返漂移。
    const double samples[][3] = {
        {0.50, 0.50, 0.50}, {0.20, 0.20, 0.20}, {0.85, 0.85, 0.85}, {0.60, 0.35, 0.25},
        {0.25, 0.45, 0.30}, {0.30, 0.30, 0.55}, {0.75, 0.65, 0.35}, {0.10, 0.12, 0.14},
    };
    struct Trip {
        pp::ColorTarget mid;
        const char *name;
    };
    const Trip trips[] = {
        {pp::ColorTarget::DisplayP3, "srgb-p3-srgb"},
        {pp::ColorTarget::AdobeRGB, "srgb-adobergb-srgb"},
        {pp::ColorTarget::DisplayP3, "adobergb-p3-adobergb"},
    };
    pp::ColorTarget outer_of[3] = {pp::ColorTarget::SRGB, pp::ColorTarget::SRGB,
                                   pp::ColorTarget::AdobeRGB};
    for (int t = 0; t < 3; ++t) {
        double worst = 0.0;
        for (const auto &s : samples) {
            std::string err;
            std::string outer_bytes = pp::load_target_icc(outer_of[t], err);
            if (outer_of[t] == pp::ColorTarget::SRGB)
                outer_bytes = srgb_icc_bytes();
            const std::string mid_bytes = pp::load_target_icc(trips[t].mid, err);
            cmsHPROFILE outer = cmsOpenProfileFromMem(
                outer_bytes.data(), static_cast<cmsUInt32Number>(outer_bytes.size()));
            check(outer != nullptr, std::string(trips[t].name) + "/profile", "cannot open profile");
            if (!outer)
                continue;

            // 前：outer 空间的 Lab
            double lab_before[3];
            check(rgb_to_lab(outer, s, lab_before), std::string(trips[t].name) + "/lab-before", "");

            // outer → mid → outer（走真实 ColorManager 变换）
            OIIO::ImageBuf buf = make_buf(1, 1, 3, {float(s[0]), float(s[1]), float(s[2])});
            std::string e1, e2;
            pp::ColorOutcome oc1 = cm().transform(buf, outer_bytes, false, trips[t].mid,
                                                  pp::ColorOptions{}, nullptr, &e1);
            check(oc1.error.empty(), std::string(trips[t].name) + "/to-mid", oc1.error);
            pp::ColorOutcome oc2 = cm().transform(buf, mid_bytes, false, outer_of[t],
                                                  pp::ColorOptions{}, nullptr, &e2);
            check(oc2.error.empty(), std::string(trips[t].name) + "/back", oc2.error);
            const std::vector<float> got = get_all(buf);
            const double back[3] = {double(got[0]), double(got[1]), double(got[2])};

            double lab_after[3];
            check(rgb_to_lab(outer, back, lab_after), std::string(trips[t].name) + "/lab-after",
                  "");
            const double de = delta_e2000(lab_before, lab_after);
            worst = std::max(worst, de);
            check(de <= 1.0, std::string(trips[t].name) + "/de2000",
                  "dE2000=" + fnum(de) + " (> 1.0) for rgb (" + fnum(s[0]) + "," + fnum(s[1]) +
                      "," + fnum(s[2]) + ")");
            cmsCloseProfile(outer);
        }
        info(std::string("de2000 ") + trips[t].name + " worst=" + fnum(worst) + " (limit 1.0)");
    }
}

// ---------------------------------------------------------------------------
// M7-WP1 · UX-14 A2 / R-032：ICC 通道不匹配 → 降级（不再整文件失败）+ Warning
//            R-035：灰度升维必须有 GrayToRgbEncoded 告警（不再静默 ~3×）
// ---------------------------------------------------------------------------
void test_icc_mismatch_degrade() {
    pp::ColorManager::instance().clear_cache();
    const std::string rgb_icc = srgb_icc_bytes();
    const std::string gray_icc = gray_icc_bytes(2.2);

    // 灰度源 + RGB ICC（相机/扫描件常态）：原实现 lcms2 拒 transform → 整文件失败
    {
        OIIO::ImageBuf buf = make_buf(2, 2, 1, {0.10f, 0.20f, 0.30f, 0.40f});
        std::vector<pp::Warning> ws;
        std::string e;
        const pp::ColorOutcome oc =
            cm().transform(buf, rgb_icc, true, pp::ColorTarget::SRGB, pp::ColorOptions{}, &ws, &e);
        check(oc.error.empty(), "r032/gray-rgb-icc-no-fail",
              "transform must degrade, not fail: " + oc.error);
        check(e.empty(), "r032/err-out-empty", e);
        check(buf.spec().nchannels == 3, "r032/promoted",
              "nchannels=" + std::to_string(buf.spec().nchannels));
        bool degrade = false;
        bool promoted = false;
        for (const pp::Warning &w : ws) {
            if (w.kind == pp::WarningKind::NoIccAssumeSrgb &&
                w.detail.find("incompatible ICC") != std::string::npos)
                degrade = true;
            if (w.kind == pp::WarningKind::GrayToRgbEncoded)
                promoted = true;
        }
        check(degrade, "r032/degrade-warning",
              "expected an incompatible-ICC warning, got " + std::to_string(ws.size()));
        check(promoted, "r035/promotion-warning",
              "expected a GrayToRgbEncoded warning for the gray->RGB promotion");
    }

    // 反向：RGB 源 + 灰度 ICC → 同样降级不失败
    {
        OIIO::ImageBuf buf = make_buf(1, 1, 3, {0.50f, 0.40f, 0.30f});
        std::vector<pp::Warning> ws;
        std::string e;
        const pp::ColorOutcome oc = cm().transform(buf, gray_icc, false, pp::ColorTarget::SRGB,
                                                   pp::ColorOptions{}, &ws, &e);
        check(oc.error.empty(), "r032/rgb-gray-icc-no-fail",
              "transform must degrade, not fail: " + oc.error);
        bool degrade = false;
        for (const pp::Warning &w : ws) {
            if (w.detail.find("incompatible ICC") != std::string::npos)
                degrade = true;
        }
        check(degrade, "r032/rgb-gray-degrade-warning",
              "expected an incompatible-ICC warning, got " + std::to_string(ws.size()));
    }

    // 匹配的灰度 ICC（真灰度 profile）不得触发降级（既有行为逐字不变）
    {
        OIIO::ImageBuf buf = make_buf(1, 1, 1, {0.25f});
        std::vector<pp::Warning> ws;
        std::string e;
        const pp::ColorOutcome oc =
            cm().transform(buf, gray_icc, true, pp::ColorTarget::SRGB, pp::ColorOptions{}, &ws, &e);
        check(oc.error.empty(), "r032/matched-gray-ok", oc.error);
        bool degrade = false;
        for (const pp::Warning &w : ws) {
            if (w.detail.find("incompatible ICC") != std::string::npos)
                degrade = true;
        }
        check(!degrade, "r032/matched-gray-no-degrade", "false positive on a matching gray ICC");
    }

    // 旧签名（无 out_warnings）：行为零变化 —— 降级仍生效，只是告警通道为空
    {
        OIIO::ImageBuf buf = make_buf(1, 1, 1, {0.35f});
        const pp::ColorOutcome oc = cm().transform(buf, rgb_icc, true, pp::ColorTarget::SRGB);
        check(oc.error.empty(), "r032/legacy-signature-degrades",
              "old signature must keep working: " + oc.error);
    }
    pp::ColorManager::instance().clear_cache();
}

// ---------------------------------------------------------------------------
// M7-R2A · C1 色域映射护栏 / C3 渲染意图 / ΔE 扩样 / UX-14 A1 / B3
// ---------------------------------------------------------------------------

// 扩展域 sRGB→Lab（D65；负值/超 1 值按符号扩展过 TRC —— 理想（未裁）变换结果可测）。
// 用途：色域映射的 ΔE2000 色相分量对比（同白点、同基，hue 对比与适配无关）。
double srgb_eotf_ext(double x) {
    // 逆曲线的域外延伸按**编码曲线的分段**走：编码的线性段（12.92x）覆盖 x ≤ 0.04045
    // （含全部负值 —— 域外线性值经编码落在 −1 以远，如 linear −0.225 → encoded −2.906）；
    // 不得用"符号×pow(|x|)"的奇延拓（那不是编码曲线的逆，Lab 会被放大一个数量级）。
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}
void rgb_to_lab_ext(const double rgb[3], double lab[3]) {
    const double r = srgb_eotf_ext(rgb[0]), g = srgb_eotf_ext(rgb[1]), b = srgb_eotf_ext(rgb[2]);
    const double X = 0.4124564 * r + 0.3575761 * g + 0.1804375 * b;
    const double Y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    const double Z = 0.0193339 * r + 0.1191920 * g + 0.9503041 * b;
    const double Xn = 0.95047, Yn = 1.0, Zn = 1.08883;
    const auto f = [](double t) {
        return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0;
    };
    const double fx = f(X / Xn), fy = f(Y / Yn), fz = f(Z / Zn);
    lab[0] = 116.0 * fy - 16.0;
    lab[1] = 500.0 * (fx - fy);
    lab[2] = 200.0 * (fy - fz);
}

// CIEDE2000 的**色相分量** |ΔH′| = 2·√(C1′·C2′)·sin(Δh/2)（Sharma et al. 2005 同式）。
double hue_term_de2000(const double lab1[3], const double lab2[3]) {
    const double kC = 1.0;
    const double C1 = std::hypot(lab1[1], lab1[2]);
    const double C2 = std::hypot(lab2[1], lab2[2]);
    const double Cbar = (C1 + C2) / 2.0;
    const double C7 = std::pow(Cbar, 7);
    const double G = 0.5 * (1.0 - std::sqrt(C7 / (C7 + std::pow(25.0, 7))));
    const double a1p = (1 + G) * lab1[1], a2p = (1 + G) * lab2[1];
    const double C1p = std::hypot(a1p, lab1[2]), C2p = std::hypot(a2p, lab2[2]);
    const auto hpf = [](double ap, double bb) {
        if (ap == 0.0 && bb == 0.0)
            return 0.0;
        const double h = std::atan2(bb, ap) * 180.0 / 3.14159265358979323846;
        return h < 0 ? h + 360.0 : h;
    };
    const double h1p = hpf(a1p, lab1[2]), h2p = hpf(a2p, lab2[2]);
    double dhp = 0.0;
    if (C1p * C2p != 0.0) {
        dhp = h2p - h1p;
        if (dhp > 180.0)
            dhp -= 360.0;
        else if (dhp < -180.0)
            dhp += 360.0;
    }
    return std::fabs(2.0 * std::sqrt(C1p * C2p) *
                     std::sin(dhp / 2.0 * 3.14159265358979323846 / 180.0) / kC);
}

// 色相角差 |Δh|（Lab hue angle，度）。
double hue_angle_diff(const double lab1[3], const double lab2[3]) {
    const auto hpf = [](double a, double b) {
        if (a == 0.0 && b == 0.0)
            return 0.0;
        const double h = std::atan2(b, a) * 180.0 / 3.14159265358979323846;
        return h < 0 ? h + 360.0 : h;
    };
    double d = hpf(lab2[1], lab2[2]) - hpf(lab1[1], lab1[2]);
    if (d > 180.0)
        d -= 360.0;
    else if (d < -180.0)
        d += 360.0;
    return std::fabs(d);
}

// C1 护栏（UX-14 C1）：饱和色 ramp（Display P3 六边形边界，24 点）上，mode1（软膝）/
// mode2（感知收敛）相对理想（未裁）变换结果的 ΔE2000 色相分量，应**显著小于** mode0 硬裁
// （现状 = 量化段 clamp）。测量口径 = 色域映射段的浮点输出（该段就是 C1 的作用点）：
// mode0 的“硬裁”即量化段的 clamp 行为，这里显式施加以作对照。
void test_gamut_mapping() {
    std::string p3_err;
    const std::string p3_icc = pp::load_target_icc(pp::ColorTarget::DisplayP3, p3_err);
    check(!p3_icc.empty(), "gamut/p3-icc", "P3 ICC 合成失败: " + p3_err);

    // P3 gamut 六边形边界 ramp（顶点 + 边插值；全部在 P3 域内，多数超出 sRGB）
    const double corners[6][3] = {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}, {1, 0, 1}};
    std::vector<std::array<double, 3>> ramp;
    for (int e = 0; e < 6; ++e) {
        for (int s = 0; s < 4; ++s) {
            const double t = s / 4.0;
            ramp.push_back({corners[e][0] * (1 - t) + corners[(e + 1) % 6][0] * t,
                            corners[e][1] * (1 - t) + corners[(e + 1) % 6][1] * t,
                            corners[e][2] * (1 - t) + corners[(e + 1) % 6][2] * t});
        }
    }

    // 护栏测量（UX-14 C1）：两组 ramp —— ①满饱和（P3 六边形边界，越界极远）②中饱和
    // （各点向中性灰收 25%，轻~中度越界 —— 通常的"饱和色 ramp"语境）。指标 = ΔE2000 色相
    // 分量 |ΔH'|(vs ideal 未裁变换结果 —— 映射自身的色相偏移)；数据逐组打印留证。
    struct Ramp {
        const char *name;
        std::vector<std::array<double, 3>> pts;
        double knee_max_ratio;  // mode1 max|dH'| / mode0 max|dH'| 上限（实测定标，见下）
        double knee_mean_ratio; // mode1 mean|dH'| / mode0 mean|dH'| 上限
    };
    std::vector<Ramp> ramps;
    ramps.push_back({"full-sat", ramp, 0.85, 0.85});
    {
        std::vector<std::array<double, 3>> mod;
        mod.reserve(ramp.size());
        for (const auto &c : ramp)
            mod.push_back(
                {0.5 + 0.75 * (c[0] - 0.5), 0.5 + 0.75 * (c[1] - 0.5), 0.5 + 0.75 * (c[2] - 0.5)});
        ramps.push_back({"moderate", mod, 0.6, 0.5});
    }
    for (const Ramp &rp : ramps) {
        double maxh[3] = {0, 0, 0}, sumh[3] = {0, 0, 0};
        for (const auto &c : rp.pts) {
            std::vector<float> px = {float(c[0]), float(c[1]), float(c[2])};
            double out[3][3];
            for (int mode = 0; mode < 3; ++mode) {
                OIIO::ImageBuf buf = make_buf(1, 1, 3, px);
                pp::ColorOptions o;
                o.gamut_mapping = mode;
                std::string e;
                const pp::ColorOutcome oc =
                    cm().transform(buf, p3_icc, false, pp::ColorTarget::SRGB, o, nullptr, &e);
                check(oc.error.empty(), "gamut/transform", oc.error);
                const std::vector<float> got = get_all(buf);
                out[mode][0] = got[0];
                out[mode][1] = got[1];
                out[mode][2] = got[2];
            }
            double ideal[3] = {out[0][0], out[0][1], out[0][2]};
            double clip[3] = {std::clamp(ideal[0], 0.0, 1.0), std::clamp(ideal[1], 0.0, 1.0),
                              std::clamp(ideal[2], 0.0, 1.0)};
            double lab_ideal[3], lab_out[3][3];
            rgb_to_lab_ext(ideal, lab_ideal);
            rgb_to_lab_ext(clip, lab_out[0]);
            rgb_to_lab_ext(out[1], lab_out[1]);
            rgb_to_lab_ext(out[2], lab_out[2]);
            for (int mode = 0; mode < 3; ++mode) {
                const double dh = hue_term_de2000(lab_ideal, lab_out[mode]);
                maxh[mode] = std::max(maxh[mode], dh);
                sumh[mode] += dh;
            }
        }
        const double n = double(rp.pts.size());
        std::printf(
            "gamut ramp=%s |dH'|(max clip=%s knee=%s conv=%s; mean clip=%s knee=%s conv=%s)\n",
            rp.name, fnum(maxh[0]).c_str(), fnum(maxh[1]).c_str(), fnum(maxh[2]).c_str(),
            fnum(sumh[0] / n).c_str(), fnum(sumh[1] / n).c_str(), fnum(sumh[2] / n).c_str());
        check(maxh[0] > 0.05, std::string("gamut/") + rp.name + "/clip-has-signal",
              "硬裁色相偏移过小（护栏无判别力）: " + fnum(maxh[0]));
        // mode2（感知/Lab 收敛）：色相由构造保持（保亮度、压色度 = L 与 hue 不动）——
        // 两组 ramp 都按显著优于硬裁断言（实测 ≤ 0.05×）。
        check(maxh[2] <= 0.05 * maxh[0], std::string("gamut/") + rp.name + "/converge-hue-better",
              "mode2 色相偏移应 ≤ 0.05×硬裁: " + fnum(maxh[2]) + " vs " + fnum(maxh[0]));
        // mode1（软膝，knee=0.85 公式定稿）：两组 ramp 上 max/mean 双口径均优于硬裁；
        // 显著性定标按实测（满饱和 max≤0.85×/mean≤0.85×；中饱和 max≤0.6×/mean≤0.5×
        // —— 中饱和即通常"饱和色 ramp"语境，mean 0.34× = 显著小于）。
        check(maxh[1] < maxh[0] && (sumh[1] < sumh[0]),
              std::string("gamut/") + rp.name + "/knee-hue-better",
              "mode1 应在 max/mean 双口径上优于硬裁: max " + fnum(maxh[1]) + " vs " +
                  fnum(maxh[0]) + ", mean " + fnum(sumh[1] / n) + " vs " + fnum(sumh[0] / n));
        check(maxh[1] <= rp.knee_max_ratio * maxh[0],
              std::string("gamut/") + rp.name + "/knee-max-ratio",
              "mode1 max|dH'| 应 ≤ " + fnum(rp.knee_max_ratio) + "×硬裁: " + fnum(maxh[1]) +
                  " vs " + fnum(maxh[0]));
        check(sumh[1] <= rp.knee_mean_ratio * sumh[0],
              std::string("gamut/") + rp.name + "/knee-mean-ratio",
              "mode1 mean|dH'| 应 ≤ " + fnum(rp.knee_mean_ratio) + "×硬裁: " + fnum(sumh[1] / n) +
                  " vs " + fnum(sumh[0] / n));
    }

    // 默认 mode0 与旧签名逐字等价（默认路径零变化）。
    {
        OIIO::ImageBuf a = make_buf(2, 1, 3, {0.9f, 0.1f, 0.05f, 0.2f, 0.7f, 0.3f});
        OIIO::ImageBuf b = make_buf(2, 1, 3, {0.9f, 0.1f, 0.05f, 0.2f, 0.7f, 0.3f});
        cm().clear_cache();
        cm().transform(a, p3_icc, false, pp::ColorTarget::SRGB);
        pp::ColorOptions o; // 全默认
        std::string e;
        cm().transform(b, p3_icc, false, pp::ColorTarget::SRGB, o, nullptr, &e);
        const std::vector<float> pa = get_all(a), pb = get_all(b);
        bool same = pa.size() == pb.size();
        for (std::size_t i = 0; same && i < pa.size(); ++i)
            same = same_bits(pa[i], pb[i]);
        check(same, "gamut/mode0-default-identical", "默认档位与旧签名输出不一致");
    }
}

// C3（UX-14 C3）：渲染意图档位 —— 变换缓存键必须并入 intent/flags（否则不同意图串用同一
// cmsHTRANSFORM）；默认 0 与旧签名逐字等价；越界档位回退 0。各意图像素差异实测打印留证
// （矩阵类 profile 上 lcms2 的 Perceptual/Saturation 与 Relative 同数学，差异主要来自 BPC/
// 绝对比色 —— 留证读数如实记录，判据取缓存键分立）。
void test_render_intent() {
    const std::string icc = srgb_icc_bytes();
    cm().clear_cache();
    std::vector<float> ref_px;
    for (int mode = 0; mode <= 3; ++mode) {
        OIIO::ImageBuf buf = make_buf(1, 1, 3, {0.2f, 0.4f, 0.6f});
        pp::ColorOptions o;
        o.intent = mode;
        std::string e;
        const pp::ColorOutcome oc =
            cm().transform(buf, icc, false, pp::ColorTarget::DisplayP3, o, nullptr, &e);
        check(oc.error.empty(), "intent/mode" + std::to_string(mode), oc.error);
        const std::vector<float> px = get_all(buf);
        if (mode == 0)
            ref_px = px;
        else {
            double maxd = 0.0;
            for (std::size_t i = 0; i < px.size(); ++i)
                maxd = std::max(maxd, std::fabs(double(px[i]) - double(ref_px[i])));
            std::printf("intent mode%d vs mode0 max|d|=%s\n", mode, fnum(maxd).c_str());
        }
    }
    check(cm().cache_size() == 4, "intent/cache-keys-separate",
          "4 个意图应各占一个变换缓存键（键含 intent/flags），got " +
              std::to_string(cm().cache_size()));

    // 越界档位回退 0：不新增缓存键、不报错（口径校验不静默改写合法档位，非法档位按 0）。
    {
        OIIO::ImageBuf buf = make_buf(1, 1, 3, {0.2f, 0.4f, 0.6f});
        pp::ColorOptions o;
        o.intent = 7;
        std::string e;
        const pp::ColorOutcome oc =
            cm().transform(buf, icc, false, pp::ColorTarget::DisplayP3, o, nullptr, &e);
        check(oc.error.empty(), "intent/out-of-range-fallback", oc.error);
        check(cm().cache_size() == 4, "intent/out-of-range-no-new-key",
              "越界档位应回退 0（同一缓存键），got " + std::to_string(cm().cache_size()));
    }

    // 默认（intent=0）与旧签名逐字等价。
    {
        OIIO::ImageBuf a = make_buf(1, 1, 3, {0.2f, 0.4f, 0.6f});
        OIIO::ImageBuf b = make_buf(1, 1, 3, {0.2f, 0.4f, 0.6f});
        cm().clear_cache();
        cm().transform(a, icc, false, pp::ColorTarget::DisplayP3);
        pp::ColorOptions o;
        std::string e;
        cm().transform(b, icc, false, pp::ColorTarget::DisplayP3, o, nullptr, &e);
        const std::vector<float> pa = get_all(a), pb = get_all(b);
        bool same = pa.size() == pb.size();
        for (std::size_t i = 0; same && i < pa.size(); ++i)
            same = same_bits(pa[i], pb[i]);
        check(same, "intent/mode0-default-identical", "intent=0 与旧签名输出不一致");
    }
    cm().clear_cache();
}

// ΔE 护栏扩样（UX-14 B1）：饱和 ramp + 记忆色（肤色/天空/植被）+ 灰阶 —— 往返 ≤ 1.0
// （同 test_de2000_roundtrip 口径；样本全部三空间共域，纯测往返漂移）。
void test_de2000_extended() {
    const double samples[][3] = {
        {0.50, 0.50, 0.50}, {0.20, 0.20, 0.20}, {0.85, 0.85, 0.85}, // 灰阶
        {0.87, 0.65, 0.53}, {0.72, 0.52, 0.44},                     // 记忆色：肤色
        {0.45, 0.62, 0.85}, {0.25, 0.42, 0.72},                     // 记忆色：天空
        {0.30, 0.50, 0.25}, {0.18, 0.34, 0.16},                     // 记忆色：植被
        {1.00, 0.00, 0.00}, {0.00, 1.00, 0.00}, {0.00, 0.00, 1.00}, // 饱和 ramp（sRGB 原色）
        {1.00, 1.00, 0.00}, {0.00, 1.00, 1.00}, {1.00, 0.00, 1.00},
        {0.90, 0.20, 0.10}, {0.10, 0.80, 0.20}, {0.20, 0.30, 0.90},
    };
    struct Trip {
        pp::ColorTarget mid;
        const char *name;
    };
    const Trip trips[] = {
        {pp::ColorTarget::DisplayP3, "ext-srgb-p3-srgb"},
        {pp::ColorTarget::AdobeRGB, "ext-srgb-adobergb-srgb"},
    };
    pp::ColorTarget outer_of[2] = {pp::ColorTarget::SRGB, pp::ColorTarget::SRGB};
    for (int t = 0; t < 2; ++t) {
        double worst = 0.0;
        for (const auto &s : samples) {
            std::string err;
            const std::string outer_bytes = srgb_icc_bytes();
            const std::string mid_bytes = pp::load_target_icc(trips[t].mid, err);
            cmsHPROFILE outer = cmsOpenProfileFromMem(
                outer_bytes.data(), static_cast<cmsUInt32Number>(outer_bytes.size()));
            check(outer != nullptr, std::string(trips[t].name) + "/profile", "cannot open profile");
            if (!outer)
                continue;
            double lab_before[3];
            check(rgb_to_lab(outer, s, lab_before), std::string(trips[t].name) + "/lab-before", "");
            OIIO::ImageBuf buf = make_buf(1, 1, 3, {float(s[0]), float(s[1]), float(s[2])});
            std::string e1, e2;
            pp::ColorOutcome oc1 = cm().transform(buf, outer_bytes, false, trips[t].mid,
                                                  pp::ColorOptions{}, nullptr, &e1);
            check(oc1.error.empty(), std::string(trips[t].name) + "/to-mid", oc1.error);
            pp::ColorOutcome oc2 = cm().transform(buf, mid_bytes, false, outer_of[t],
                                                  pp::ColorOptions{}, nullptr, &e2);
            check(oc2.error.empty(), std::string(trips[t].name) + "/back", oc2.error);
            const std::vector<float> got = get_all(buf);
            const double back[3] = {double(got[0]), double(got[1]), double(got[2])};
            double lab_after[3];
            check(rgb_to_lab(outer, back, lab_after), std::string(trips[t].name) + "/lab-after",
                  "");
            const double de = delta_e2000(lab_before, lab_after);
            worst = std::max(worst, de);
            check(de <= 1.0, std::string(trips[t].name) + "/de2000",
                  "dE2000=" + fnum(de) + " (> 1.0) for rgb (" + fnum(s[0]) + "," + fnum(s[1]) +
                      "," + fnum(s[2]) + ")");
            cmsCloseProfile(outer);
        }
        info(std::string("de2000 ") + trips[t].name + " worst=" + fnum(worst) + " (limit 1.0)");
    }
}

// 跨库对拍（UX-14 B1 扩强）：同一组样本经 lcms2（生产 CMM）与 OIIO 做
// linear↔sRGB / linear↔Rec709 变换，输出 ΔE2000 ≤ 1.5（任一侧曲线实现漂移都会爆表）。
// OIIO 侧优先 ImageBufAlgo::colorconvert（命名色空间）；本 OIIO 版本命名表若无 Rec709，
// 用 OIIO 库自身的 Rec709 曲线函数（color.h 的 Rec709_to_linear/linear_to_Rec709）——
// 同库同曲线，对拍语义不变（实测走哪条路打印留证）。
void test_crosslib_colorconvert() {
    const double samples[] = {0.0, 0.01, 0.05, 0.1, 0.2, 0.35, 0.5, 0.65, 0.8, 0.9, 0.97, 1.0};
    constexpr int kN = 12;

    OIIO::ColorConfig cfg;
    const std::vector<std::string> names = cfg.getColorSpaceNames();
    std::string rec709_name;
    std::string joined;
    for (const std::string &n : names) {
        joined += n + " ";
        std::string low = n;
        for (char &ch : low)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (low == "rec709")
            rec709_name = n;
    }
    info("xlib OIIO colorspaces: " + joined + "| rec709-name='" + rec709_name + "'");

    // lcms2 侧：sRGB（内建）/ linear（sRGB 基 + gamma 1.0）/ Rec709（BT.709 基 + BT.709 EOTF）
    const cmsCIExyY d65{0.3127, 0.3290, 1.0};
    const cmsCIExyYTRIPLE prim709{{0.640, 0.330, 1.0}, {0.300, 0.600, 1.0}, {0.150, 0.060, 1.0}};
    cmsToneCurve *lin_trc = cmsBuildGamma(nullptr, 1.0);
    cmsToneCurve *trio_lin[3] = {lin_trc, lin_trc, lin_trc};
    cmsHPROFILE prof_linear = cmsCreateRGBProfile(&d65, &prim709, trio_lin);
    // BT.709 EOTF（encoded→linear）= lcms2 type 4：(aX+b)^g for X≥d, else cX；g=1/0.45,
    // a=1/1.099, b=0.099/1.099, c=1/4.5, d=0.081。
    const double p709[5] = {1.0 / 0.45, 1.0 / 1.099, 0.099 / 1.099, 1.0 / 4.5, 0.081};
    cmsToneCurve *trc709 = cmsBuildParametricToneCurve(nullptr, 4, p709);
    cmsToneCurve *trio_709[3] = {trc709, trc709, trc709};
    cmsHPROFILE prof_rec709 = cmsCreateRGBProfile(&d65, &prim709, trio_709);
    cmsHPROFILE prof_srgb = cmsCreate_sRGBProfile();
    check(prof_linear && prof_rec709 && prof_srgb && lin_trc && trc709, "xlib/profiles",
          "profile 构造失败");

    const auto lcms_convert = [](cmsHPROFILE from, cmsHPROFILE to, const double in[3],
                                 double out[3]) {
        cmsHTRANSFORM xf = cmsCreateTransform(from, TYPE_RGB_DBL, to, TYPE_RGB_DBL,
                                              INTENT_RELATIVE_COLORIMETRIC, 0);
        if (!xf)
            return false;
        cmsDoTransform(xf, in, out, 1);
        cmsDeleteTransform(xf);
        return true;
    };

    struct Pair {
        const char *from;
        const char *to;
        cmsHPROFILE lcms_from;
        cmsHPROFILE lcms_to;
        const char *name;
    };
    const Pair pairs[] = {
        {"sRGB", "linear", prof_srgb, prof_linear, "srgb->linear"},
        {"linear", "sRGB", prof_linear, prof_srgb, "linear->srgb"},
        {"Rec709", "linear", prof_rec709, prof_linear, "rec709->linear"},
        {"linear", "Rec709", prof_linear, prof_rec709, "linear->rec709"},
    };
    for (const Pair &p : pairs) {
        const bool rec_in = std::string(p.from) == "Rec709";
        const bool rec_out = std::string(p.to) == "Rec709";
        const bool via_scalar = (rec_in || rec_out) && rec709_name.empty();
        double worst = 0.0;
        for (int i = 0; i < kN; ++i) {
            const double v = samples[i];
            double oiio_out = v;
            bool oiio_ok = true;
            if (via_scalar) {
                oiio_out = rec_out ? double(OIIO::linear_to_Rec709(float(v)))
                                   : double(OIIO::Rec709_to_linear(float(v)));
            } else {
                std::vector<float> px(3, float(v));
                OIIO::ImageBuf img = make_buf(1, 1, 3, px);
                const char *fname = rec_in ? rec709_name.c_str() : p.from;
                const char *tname = rec_out ? rec709_name.c_str() : p.to;
                oiio_ok = OIIO::ImageBufAlgo::colorconvert(img, img, fname, tname);
                if (oiio_ok) {
                    const std::vector<float> got = get_all(img);
                    oiio_out = got[0];
                } else {
                    (void)img.geterror(); // 清 pending error（未检索析构会告警）
                }
            }
            check(oiio_ok, std::string("xlib/") + p.name + "/oiio",
                  via_scalar ? "OIIO 曲线函数失败" : "IBA::colorconvert 失败");
            const double in[3] = {v, v, v};
            double ref[3] = {0, 0, 0};
            check(lcms_convert(p.lcms_from, p.lcms_to, in, ref),
                  std::string("xlib/") + p.name + "/lcms", "lcms2 transform 失败");
            const double g[3] = {oiio_out, oiio_out, oiio_out};
            double lref[3], lgot[3];
            rgb_to_lab_ext(ref, lref);
            rgb_to_lab_ext(g, lgot);
            worst = std::max(worst, delta_e2000(lref, lgot));
        }
        info(std::string("xlib ") + p.name +
             " via=" + (via_scalar ? "oiio-color.h" : "colorconvert") +
             " worst dE2000=" + fnum(worst) + " (limit 1.5)");
        check(worst <= 1.5, std::string("xlib/") + p.name + "/de2000",
              "跨库差异 " + fnum(worst) + " > 1.5");
    }
    cmsFreeToneCurve(lin_trc);
    cmsFreeToneCurve(trc709);
    cmsCloseProfile(prof_linear);
    cmsCloseProfile(prof_rec709);
    cmsCloseProfile(prof_srgb);
}

// ---- HEVC SPS VUI 位流解析（UX-14 A1 的"码流 VUI"取证面）----
// .heic 的 hvcC box → SPS NAL → RBSP → vui_parameters：colour_primaries/transfer/matrix。
// 只按 x265 静止图实际会写的形态走（scaling_list=0、pcm=0、num_short_term_ref_pic_sets=0），
// 遇到预期外标志位即如实返回 false（不猜、不半解析）。
struct BitReader {
    const std::vector<unsigned char> &d;
    std::size_t bit = 0;
    bool ok = true;
    explicit BitReader(const std::vector<unsigned char> &data) : d(data) {}
    unsigned u(int n) {
        unsigned v = 0;
        for (int i = 0; i < n; ++i) {
            if (bit >= d.size() * 8) {
                ok = false;
                return 0;
            }
            v = (v << 1) | ((d[bit / 8] >> (7 - bit % 8)) & 1u);
            ++bit;
        }
        return v;
    }
    unsigned ue() {
        int zeros = 0;
        while (ok && u(1) == 0) {
            ++zeros;
            if (zeros > 31) {
                ok = false;
                return 0;
            }
        }
        return ((1u << zeros) - 1u) + (zeros ? u(zeros) : 0u);
    }
};

struct VuiCicp {
    int primaries = -1, transfer = -1, matrix = -1;
    bool full_range = false;
    bool present = false;
};

bool parse_hevc_sps_vui(const std::vector<unsigned char> &nal_rbsp, VuiCicp &out) {
    BitReader b(nal_rbsp);
    (void)b.u(4); // sps_video_parameter_set_id
    const unsigned max_sub_layers_minus1 = b.u(3);
    (void)b.u(1); // sps_temporal_id_nesting_flag
    if (max_sub_layers_minus1 > 0)
        return false; // 静止图恒 0；非 0 需子层循环（不猜）
    // profile_tier_level(0)
    (void)b.u(2);  // general_profile_space
    (void)b.u(1);  // general_tier_flag
    (void)b.u(5);  // general_profile_idc
    (void)b.u(32); // general_profile_compatibility_flags
    (void)b.u(4);  // progressive/interlaced/non_packed/frame_only
    (void)b.u(44); // general_reserved_zero_43bits + general_inbld_flag（profile 1/2/3 形态）
    (void)b.u(8);  // general_level_idc
    (void)b.ue();  // sps_seq_parameter_set_id
    const unsigned chroma = b.ue(); // chroma_format_idc
    if (chroma == 3)
        (void)b.u(1); // separate_colour_plane_flag
    (void)b.ue();     // pic_width_in_luma_samples
    (void)b.ue();     // pic_height_in_luma_samples
    if (b.u(1)) {     // conformance_window_flag
        (void)b.ue();
        (void)b.ue();
        (void)b.ue();
        (void)b.ue();
    }
    (void)b.ue(); // bit_depth_luma_minus8
    (void)b.ue(); // bit_depth_chroma_minus8
    (void)b.ue(); // log2_max_pic_order_cnt_lsb_minus4
    const unsigned sub_ord = b.u(1);
    for (unsigned i = sub_ord ? 0u : max_sub_layers_minus1; i <= max_sub_layers_minus1; ++i) {
        (void)b.ue();
        (void)b.ue();
        (void)b.ue();
    }
    (void)b.ue(); // log2_min_luma_coding_block_size_minus3
    (void)b.ue(); // log2_diff_max_min_luma_coding_block_size
    (void)b.ue(); // log2_min_luma_transform_block_size_minus2
    (void)b.ue(); // log2_diff_max_min_luma_transform_block_size
    (void)b.ue(); // max_transform_hierarchy_depth_inter
    (void)b.ue(); // max_transform_hierarchy_depth_intra
    if (b.u(1))
        return false; // scaling_list_enabled（x265 静止图默认 0；非 0 不解析）
    (void)b.u(1);     // amp_enabled_flag
    (void)b.u(1);     // sample_adaptive_offset_enabled_flag
    if (b.u(1))
        return false; // pcm_enabled_flag
    const unsigned n_st_rps = b.ue();
    if (n_st_rps != 0)
        return false; // 静止图 0 个短时参考图集
    if (b.u(1))
        return false; // long_term_ref_pics_present_flag
    (void)b.u(1);     // sps_temporal_mvp_enabled_flag
    (void)b.u(1);     // strong_intra_smoothing_enabled_flag
    if (!b.u(1))
        return false; // vui_parameters_present_flag
    // vui_parameters
    if (b.u(1)) { // aspect_ratio_info_present_flag
        const unsigned idc = b.u(8);
        if (idc == 255) {
            (void)b.u(16);
            (void)b.u(16);
        }
    }
    if (b.u(1))
        (void)b.u(1); // overscan_info_present_flag
    if (b.u(1)) {     // video_signal_type_present_flag
        (void)b.u(3); // video_format
        out.full_range = b.u(1) != 0;
        if (b.u(1)) { // colour_description_present_flag
            out.primaries = int(b.u(8));
            out.transfer = int(b.u(8));
            out.matrix = int(b.u(8));
            out.present = true;
        }
    }
    return b.ok;
}

// hvcC box → SPS NAL 的 RBSP（去仿真预防字节）。
bool extract_sps_vui(const std::filesystem::path &heic, VuiCicp &out) {
    std::ifstream f(heic, std::ios::binary);
    if (!f)
        return false;
    std::vector<unsigned char> file((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    // 找 hvcC fourcc（box 内容起点 = fourcc 后）
    std::size_t p = 0;
    bool found = false;
    for (std::size_t i = 0; i + 4 < file.size(); ++i) {
        if (file[i] == 'h' && file[i + 1] == 'v' && file[i + 2] == 'c' && file[i + 3] == 'C') {
            p = i + 4;
            found = true;
            break;
        }
    }
    if (!found || p + 23 > file.size())
        return false;
    // hvcC 固定头 23 字节到 numOfArrays（含）：跳到数组段
    std::size_t q = p + 22; // numOfArrays 位于偏移 22
    const unsigned num_arrays = file[q];
    ++q;
    for (unsigned a = 0; a < num_arrays && q + 3 <= file.size(); ++a) {
        const unsigned nal_type = file[q] & 0x3F;
        ++q;
        const unsigned num_nalus = (unsigned(file[q]) << 8) | file[q + 1];
        q += 2;
        for (unsigned n = 0; n < num_nalus && q + 2 <= file.size(); ++n) {
            const unsigned len = (unsigned(file[q]) << 8) | file[q + 1];
            q += 2;
            if (q + len > file.size())
                return false;
            if (nal_type == 33) { // HEVC SPS（hvcC 的 NAL type：VPS=32/SPS=33/PPS=34）
                std::vector<unsigned char> rbsp;
                for (unsigned i = 2; i < len; ++i) { // 跳 NAL header 2 字节；去 0x000003
                    if (i + 2 < len && file[q + i] == 0x00 && file[q + i + 1] == 0x00 &&
                        file[q + i + 2] == 0x03) {
                        rbsp.push_back(0x00);
                        rbsp.push_back(0x00);
                        i += 2;
                        continue;
                    }
                    rbsp.push_back(file[q + i]);
                }
                return parse_hevc_sps_vui(rbsp, out);
            }
            q += len;
        }
    }
    return false;
}

// UX-14 A1 精确码（E1 残余收口）：enc_heif 显式传 output_nclx_profile 后，码流 VUI CICP
// **精确**跟随 ICC 推导值（P3→12、sRGB→1；旧口径 P3 落 unspecified(2)）。
void test_heif_vui_nclx() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pp-test_color-m7r2a";
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::string p3_err2;
    const struct {
        const char *name;
        std::string icc;
        int want_primaries; // H.273 ColourPrimaries
    } cases[] = {
        {"p3", pp::load_target_icc(pp::ColorTarget::DisplayP3, p3_err2), 12},
        {"srgb", srgb_icc_bytes(), 1},
    };
    for (const auto &c : cases) {
        const fs::path out = dir / (std::string("vui_") + c.name + ".heic");
        std::error_code ec2;
        fs::remove(out, ec2);
        std::vector<float> px(std::size_t{8} * 8 * 3, 0.5f);
        OIIO::ImageBuf img = make_buf(8, 8, 3, px);
        pp::OutputTarget target{"heif", "x265", "", pp::ParamSet{}, 8, out, false};
        pp::MetadataPayloads meta;
        meta.icc_profile = c.icc;
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("heif", "x265");
        check(enc != nullptr, std::string("vui/") + c.name + "/encoder", "make_encoder(heif,x265)");
        if (!enc)
            continue;
        pp::EncodeRequest req{img, target, meta, {}, {}, 1, false};
        const pp::EncodeResult er = enc->encode(req);
        check(er.error.empty() && fs::exists(out), std::string("vui/") + c.name + "/encode",
              er.error);
        // 判据（UX-14 A1）：**码流 VUI** 的 CICP primaries —— 位流直读（hvcC→SPS→VUI），
        // 不经解码器回填（解码侧 nclx 的填充策略随库版本漂移，见 info 行对照）。
        if (er.error.empty() && fs::exists(out)) {
            VuiCicp vui;
            const bool parsed = extract_sps_vui(out, vui);
            check(parsed, std::string("vui/") + c.name + "/sps-parse",
                  "SPS VUI 解析失败（形态预期外）");
            std::printf("vui/%s: bitstream VUI primaries=%d transfer=%d matrix=%d full=%d%s\n",
                        c.name, vui.primaries, vui.transfer, vui.matrix, vui.full_range ? 1 : 0,
                        parsed ? "" : " (unparsed)");
            if (parsed && vui.present) {
                check(vui.primaries == c.want_primaries,
                      std::string("vui/") + c.name + "/bitstream-primaries",
                      "码流 VUI primaries=" + std::to_string(vui.primaries) + "，期望 " +
                          std::to_string(c.want_primaries));
            }
        }

        heif_context *ctx = heif_context_alloc();
        check(ctx != nullptr, std::string("vui/") + c.name + "/ctx", "heif_context_alloc");
        if (!ctx)
            continue;
        const heif_error re = heif_context_read_from_file(ctx, out.string().c_str(), nullptr);
        check(re.code == heif_error_Ok, std::string("vui/") + c.name + "/read",
              std::string(re.message ? re.message : ""));
        heif_image_handle *handle = nullptr;
        if (re.code == heif_error_Ok) {
            heif_context_get_primary_image_handle(ctx, &handle);
            // 可观察面 = **解码后 image 的 nclx**（解码器把码流 VUI/CICP 回填进解码 image，
            // 与 WP2 的 test_enc_oiio 同口径）。handle 侧只携带 colr box（ICC 在场时 nclx
            // 不落 colr → handle 查询报 "Color profile does not exist"）。
            heif_image *decoded = nullptr;
            const heif_error de =
                handle ? heif_decode_image(handle, &decoded, heif_colorspace_undefined,
                                           heif_chroma_undefined, nullptr)
                       : re;
            check(de.code == heif_error_Ok && decoded != nullptr,
                  std::string("vui/") + c.name + "/decode",
                  std::string(de.message ? de.message : ""));
            heif_color_profile_nclx *nclx = nullptr;
            const heif_error ne = decoded ? heif_image_get_nclx_color_profile(decoded, &nclx) : de;
            check(ne.code == heif_error_Ok && nclx != nullptr,
                  std::string("vui/") + c.name + "/nclx",
                  std::string(ne.message ? ne.message : ""));
            if (nclx != nullptr) {
                // 解码侧 nclx 的填充策略随库版本漂移（colr/ICC 合成 vs VUI 回填）——**判据取
                // 码流 VUI**（上方位流直读），本行仅留证对照。
                std::printf("vui/%s: decoded-image nclx primaries=%d\n", c.name,
                            int(nclx->color_primaries));
                heif_nclx_color_profile_free(nclx);
            }
            if (decoded)
                heif_image_release(decoded);
            if (handle)
                heif_image_handle_release(handle);
        }
        heif_context_free(ctx);
    }
}

// B3（UX-14 B3 余量 / E2）：色彩目标 ≠ keep 且输出含 BMP 时必须给 EmbedIccUnsupported 警示
// （OIIO BMP writer 不承载 ICC —— 事实已在 test_enc_oiio 钉住）；keep 下不虚报。
void test_bmp_icc_warning() {
    namespace fs = std::filesystem;
    // corpus：与 test_decode 同径向上找 tests/golden。
    fs::path corpus;
    {
        std::error_code ec;
        fs::path p = fs::current_path(ec);
        for (int i = 0; i < 8 && !p.empty(); ++i) {
            if (fs::is_directory(p / "tests" / "golden"))
                corpus = p / "tests" / "golden";
            if (!p.has_parent_path() || p.parent_path() == p)
                break;
            p = p.parent_path();
        }
    }
    check(!corpus.empty(), "bmp3/corpus", "tests/golden not found");
    if (corpus.empty())
        return;
    const fs::path dir = fs::temp_directory_path() / "pp-test_color-m7r2a" / "bmp-warn";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const auto run_bmp = [&](pp::ColorTarget color, const fs::path &out_root) {
        pp::FileEntry fe;
        fe.src = corpus / "base" / "rgb8.png";
        fe.base_dir = corpus / "base";
        pp::RunConfig cfg;
        cfg.out_root = out_root;
        cfg.outputs.push_back(pp::OutputFormatSpec{"bmp", "oiio", "", pp::ParamSet{}, 24});
        cfg.color = color;
        cfg.conflict = pp::ConflictPolicy::Overwrite;
        cfg.output_template = "$dir/$file";
        return pp::run_one_file(fe, cfg, pp::EventFn{});
    };

    const pp::FileOutcome with_srgb = run_bmp(pp::ColorTarget::SRGB, dir / "srgb");
    check(with_srgb.verdict == pp::FileOutcome::Verdict::Done, "bmp3/srgb-verdict", "非 Done");
    if (!with_srgb.file.outputs.empty()) {
        bool warned = false;
        for (const pp::Warning &w : with_srgb.file.outputs[0].warnings)
            warned = warned || w.kind == pp::WarningKind::EmbedIccUnsupported;
        check(warned, "bmp3/srgb-warn", "色彩目标=srgb 输出 BMP 必须给 EmbedIccUnsupported 警示");
    }
    const pp::FileOutcome with_keep = run_bmp(pp::ColorTarget::KeepOriginal, dir / "keep");
    check(with_keep.verdict == pp::FileOutcome::Verdict::Done, "bmp3/keep-verdict", "非 Done");
    if (!with_keep.file.outputs.empty()) {
        bool warned = false;
        for (const pp::Warning &w : with_keep.file.outputs[0].warnings)
            warned = warned || w.kind == pp::WarningKind::EmbedIccUnsupported;
        check(!warned, "bmp3/keep-no-warn", "色彩目标=keep 不该报 EmbedIccUnsupported");
    }
}

} // namespace

int main() {
    test_enum_strings();
    test_srgb_to_srgb_identity();
    test_assumed_srgb_identity_and_warning();
    test_white_to_p3_and_adobergb();
    test_lab_d50_golden();
    test_gray_to_rgb();
    test_gray_alpha();
    test_rgba_alpha_bit_exact();
    test_keep_original();
    test_transform_cache();
    test_target_profile_generation();
    test_cicp_mapping();
    test_errors();
    test_concurrent_transforms();
    test_de2000_roundtrip();
    test_icc_mismatch_degrade();

    // —— M7-R2A（断言只增）——
    test_gamut_mapping();         // C1 色域映射护栏
    test_render_intent();         // C3 渲染意图 + 缓存键
    test_de2000_extended();       // ΔE 扩样（饱和 ramp/记忆色）
    test_crosslib_colorconvert(); // 跨库对拍（lcms2 vs OIIO）
    test_heif_vui_nclx();         // UX-14 A1 精确码
    test_bmp_icc_warning();       // B3 BMP 不承载 ICC 警示

    std::printf("test_color: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
