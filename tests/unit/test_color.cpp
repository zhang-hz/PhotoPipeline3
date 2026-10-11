// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T4 — ColorManager unit tests (hand-written assertions, no GTest).
//
// Contract: docs/m1-tasks.md §3.6 (PP-FROZEN header) / §4.4 (unit test list) / §7 facts.
// Golden values: M0 spike e, Lab D50 = (100, 7.57e-06, -7.63e-06).
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <lcms2.h>
#include <lcms2_plugin.h> // _cmsMAT3inverse/_cmsMAT3eval: white-point round trip (M2-T6)

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/span.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/colormanager.h"

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
    const struct {
        int p, t;
        const char *log;
    } misses[] = {
        {9, 16, "CICP transfer 16 未支持，保留合成 ICC 处理（不按 sRGB 重解释）"},
        {9, 18, "CICP transfer 18 未支持，保留合成 ICC 处理（不按 sRGB 重解释）"},
        {12, 16, "CICP transfer 16 未支持，保留合成 ICC 处理（不按 sRGB 重解释）"},
        {12, 18, "CICP transfer 18 未支持，保留合成 ICC 处理（不按 sRGB 重解释）"},
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
    std::string err;
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

    std::printf("test_color: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
