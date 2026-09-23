// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T4 — colour layer: lcms2 float transforms + LRU transform cache.
//
// Contract: docs/m1-tasks.md §3.6 (PP-FROZEN header), §4.4, §5 stage 4.
// Display P3 / Adobe RGB (1998) targets are generated in memory by lcms2
// (main-dialogue ruling in §3.6): D65 white point + standard primaries + TRC,
// so no binary ICC file is redistributed with the repository.
// See assets/icc/README.md for the generation parameters.

#include "core/colormanager.h"

#include <lcms2.h>

#include <OpenImageIO/hash.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/span.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/logger.h"

namespace pp {
namespace {

constexpr const char *kLogFile = "colormanager.cpp";

// Fixed intent/flags (§3.6 numeric contract).
constexpr int kIntent = INTENT_RELATIVE_COLORIMETRIC;
constexpr cmsUInt32Number kTransformFlags = cmsFLAGS_BLACKPOINTCOMPENSATION;

// Every transform runs in float32 (TYPE_GRAY_FLT / TYPE_RGB_FLT); the LRU key
// carries the transform bit depth so a future 8/16-bit path cannot collide.
// NOTE(limit): integer-formatter transforms (TYPE_RGB_8/16) have no caller — the
// pipeline always hands over float storage.
constexpr int kTransformBits = 32;

constexpr std::size_t kCacheCapacity = 16;

// ---------------------------------------------------------------------------
// Target profile generation (lcms2 in-memory, no binary ICC in the repo)
// ---------------------------------------------------------------------------

struct TargetSpec {
    const char *desc;
    double rx, ry, gx, gy, bx, by;
    double gamma; // 0 => IEC 61966-2.1 (sRGB) parametric TRC
};

// Standard primaries (§3.6 ICC ruling). White point is D65 for both, exactly as
// lcms2's own cmsCreate_sRGBProfileTHR uses ({0.3127, 0.3290, 1.0}).
constexpr TargetSpec kP3Spec{"Display P3", 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.0};
constexpr TargetSpec kAdobeSpec{"Adobe RGB (1998)", 0.640, 0.330, 0.210, 0.710, 0.150, 0.060,
                                2.19921875};

// M2-T6 §2.8 — source descriptions of the frozen CICP enumeration. Same D65 white
// point and the same cmsCreateRGBProfile builder as the targets above; these are
// *source* profiles for ICC-less JXL files. (12,13) reuses kP3Spec verbatim.
constexpr TargetSpec kP3Gamma22Spec{"Display P3", 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 2.2};
constexpr TargetSpec kBt2020LinearSpec{
    "BT.2020 linear", 0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 1.0};
constexpr TargetSpec kBt2020SrgbSpec{
    "BT.2020 sRGB-TRC", 0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.0};

const TargetSpec *target_spec(ColorTarget t) {
    switch (t) {
    case ColorTarget::DisplayP3:
        return &kP3Spec;
    case ColorTarget::AdobeRGB:
        return &kAdobeSpec;
    case ColorTarget::SRGB:
    case ColorTarget::KeepOriginal:
        break;
    }
    return nullptr;
}

// M2-T6 §2.8: the source-profile spec behind every recognized CICP pair (nullptr for
// Srgb — lcms2's built-in — and for Unsupported, which never gets a profile).
const TargetSpec *cicp_profile_spec(CicpSource s) {
    switch (s) {
    case CicpSource::DisplayP3:
        return &kP3Spec;
    case CicpSource::DisplayP3Gamma22:
        return &kP3Gamma22Spec;
    case CicpSource::Bt2020Linear:
        return &kBt2020LinearSpec;
    case CicpSource::Bt2020SrgbTrc:
        return &kBt2020SrgbSpec;
    case CicpSource::Srgb:
    case CicpSource::Unsupported:
        break;
    }
    return nullptr;
}

// IEC 61966-2.1 (sRGB) parametric curve, same parameters lcms2 uses internally.
cmsToneCurve *build_srgb_trc() {
    const cmsFloat64Number p[5] = {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045};
    return cmsBuildParametricToneCurve(nullptr, 4, p);
}

cmsHPROFILE make_target_profile(const TargetSpec &s) {
    const cmsCIExyY d65{0.3127, 0.3290, 1.0};
    const cmsCIExyYTRIPLE prim{{s.rx, s.ry, 1.0}, {s.gx, s.gy, 1.0}, {s.bx, s.by, 1.0}};
    cmsToneCurve *trc = (s.gamma > 0.0) ? cmsBuildGamma(nullptr, s.gamma) : build_srgb_trc();
    if (trc == nullptr)
        return nullptr;
    cmsToneCurve *trio[3] = {trc, trc, trc};
    cmsHPROFILE p = cmsCreateRGBProfile(&d65, &prim, trio);
    cmsFreeToneCurve(trc); // profile keeps its own copy of the curve data
    if (p == nullptr)
        return nullptr;
    cmsMLU *mlu = cmsMLUalloc(nullptr, 1);
    if (mlu == nullptr) {
        cmsCloseProfile(p);
        return nullptr;
    }
    const bool ok = cmsMLUsetASCII(mlu, "en", "US", s.desc) != 0 &&
                    cmsWriteTag(p, cmsSigProfileDescriptionTag, mlu) != 0;
    cmsMLUfree(mlu);
    if (!ok) {
        cmsCloseProfile(p);
        return nullptr;
    }
    return p;
}

// Gray profile with D65 white point and the sRGB TRC: the "gray sRGB" assumption
// used when a grayscale source carries no ICC.
cmsHPROFILE make_gray_srgb_profile() {
    const cmsCIExyY d65{0.3127, 0.3290, 1.0};
    cmsToneCurve *trc = build_srgb_trc();
    if (trc == nullptr)
        return nullptr;
    cmsHPROFILE p = cmsCreateGrayProfile(&d65, trc);
    cmsFreeToneCurve(trc);
    return p;
}

std::string serialize_profile(cmsHPROFILE p) {
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

// Generated-profile byte cache (target -> ICC bytes). Shared by transform() and
// load_target_icc(); the singleton caches its transforms, not its bytes.
// M2-T6: the same map also holds the CICP source profiles under keys >= kCicpCacheBase
// so they cannot collide with the ColorTarget enumerators (0..3).
constexpr int kCicpCacheBase = 100;

std::mutex &generated_mutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<int, std::string> &generated_cache() {
    static std::unordered_map<int, std::string> c;
    return c;
}

// ICC bytes of a *colour* target (SRGB = serialized lcms2 built-in). KeepOriginal
// yields an empty string. Empty result + non-empty err means failure.
std::string target_icc_bytes(ColorTarget t, std::string &err) {
    err.clear();
    if (t == ColorTarget::KeepOriginal)
        return {};
    {
        std::lock_guard<std::mutex> lock(generated_mutex());
        auto &cache = generated_cache();
        auto it = cache.find(static_cast<int>(t));
        if (it != cache.end())
            return it->second;
    }

    std::string bytes;
    if (t == ColorTarget::SRGB) {
        cmsHPROFILE p = cmsCreate_sRGBProfile();
        if (p == nullptr) {
            err = "cannot create built-in sRGB profile";
            return {};
        }
        bytes = serialize_profile(p);
        cmsCloseProfile(p);
    } else {
        const TargetSpec *s = target_spec(t);
        if (s == nullptr) {
            err = "unsupported colour target";
            return {};
        }
        cmsHPROFILE p = make_target_profile(*s);
        if (p == nullptr) {
            err = std::string("cannot generate target profile: ") + s->desc;
            return {};
        }
        bytes = serialize_profile(p);
        cmsCloseProfile(p);
    }
    if (bytes.empty()) {
        err = "cannot serialize target ICC profile";
        return {};
    }

    std::lock_guard<std::mutex> lock(generated_mutex());
    generated_cache()[static_cast<int>(t)] = bytes;
    return bytes;
}

// M2-T6 §2.8: serialized source profile for a recognized CICP pair. Cached like the
// target profiles (same mutex/map); Srgb (lcms2 built-in) and Unsupported have none,
// so an empty result is not an error here — it *is* the sRGB-assumption branch.
std::string cicp_source_icc(CicpSource s) {
    if (cicp_profile_spec(s) == nullptr)
        return {};
    const int key = kCicpCacheBase + static_cast<int>(s);
    {
        std::lock_guard<std::mutex> lock(generated_mutex());
        auto &cache = generated_cache();
        auto it = cache.find(key);
        if (it != cache.end())
            return it->second;
    }

    cmsHPROFILE p = make_target_profile(*cicp_profile_spec(s));
    if (p == nullptr)
        return {};
    std::string bytes = serialize_profile(p);
    cmsCloseProfile(p);
    if (bytes.empty())
        return {};

    std::lock_guard<std::mutex> lock(generated_mutex());
    generated_cache()[key] = bytes;
    return bytes;
}

// ---------------------------------------------------------------------------
// Profile description helpers (log/outcome text only)
// ---------------------------------------------------------------------------

std::string fourcc_string(cmsColorSpaceSignature cs) {
    char s[5] = {static_cast<char>((cs >> 24) & 0xff), static_cast<char>((cs >> 16) & 0xff),
                 static_cast<char>((cs >> 8) & 0xff), static_cast<char>(cs & 0xff), '\0'};
    std::string out(s);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out.empty() ? std::string("unknown") : out;
}

std::string icc_label(cmsHPROFILE p) {
    char buf[256] = {};
    const cmsUInt32Number n =
        cmsGetProfileInfoASCII(p, cmsInfoDescription, "en", "US", buf, sizeof(buf) - 1);
    std::string desc = (n > 0) ? std::string(buf) : std::string();
    if (desc.empty())
        desc = fourcc_string(cmsGetColorSpace(p));
    return "ICC(" + desc + ")";
}

std::string icc_bytes_label(const std::string &bytes) {
    cmsHPROFILE p = cmsOpenProfileFromMem(bytes.data(), static_cast<cmsUInt32Number>(bytes.size()));
    if (p == nullptr)
        return "ICC(unreadable)";
    std::string label = icc_label(p);
    cmsCloseProfile(p);
    return label;
}

// ---------------------------------------------------------------------------
// Transform LRU (capacity 16)
// ---------------------------------------------------------------------------

struct XfEntry {
    std::string key;
    cmsHTRANSFORM xf = nullptr;
    std::string src_desc, dst_desc;
    ~XfEntry() {
        if (xf != nullptr)
            cmsDeleteTransform(xf);
    }
};

// Entries are handed out as shared_ptr so that an eviction (or clear_cache())
// can never delete a transform that another worker thread is still using.
// lcms2 float transforms are read-only after creation (cmsxform.c: "Float
// transforms don't use cache", FloatXFORM only reads the pipeline), so sharing
// one handle across threads is safe.
class TransformCache {
public:
    std::shared_ptr<XfEntry> get(const std::string &key) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = index_.find(key);
        if (it == index_.end())
            return nullptr;
        lru_.splice(lru_.begin(), lru_, it->second); // move to front (MRU)
        return *it->second;
    }

    void put(const std::string &key, std::shared_ptr<XfEntry> entry) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = index_.find(key);
        if (it != index_.end()) {
            lru_.erase(it->second);
            index_.erase(it);
        }
        entry->key = key; // eviction needs the key to drop the index entry
        lru_.push_front(std::move(entry));
        index_[key] = lru_.begin();
        while (lru_.size() > kCacheCapacity) {
            auto last = std::prev(lru_.end());
            index_.erase((*last)->key);
            lru_.erase(last);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mu_);
        lru_.clear();
        index_.clear();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mu_);
        return lru_.size();
    }

private:
    mutable std::mutex mu_;
    std::list<std::shared_ptr<XfEntry>> lru_; // front == most recently used
    std::unordered_map<std::string, std::list<std::shared_ptr<XfEntry>>::iterator> index_;
};

std::shared_ptr<XfEntry> build_entry(const std::string &src_icc, bool gray_input,
                                     ColorTarget target, std::string &err) {
    // Destination first: its bytes are also needed for the canonical-source rule below.
    std::string dst_err;
    std::string dst_bytes;
    cmsHPROFILE dst = nullptr;
    std::string dst_desc;
    if (target == ColorTarget::SRGB) {
        dst = cmsCreate_sRGBProfile();
        dst_desc = "sRGB";
        if (dst != nullptr)
            dst_bytes = serialize_profile(dst);
    } else {
        dst_bytes = target_icc_bytes(target, dst_err);
        if (!dst_bytes.empty()) {
            dst = cmsOpenProfileFromMem(dst_bytes.data(),
                                        static_cast<cmsUInt32Number>(dst_bytes.size()));
        }
        if (dst != nullptr)
            dst_desc = icc_label(dst);
    }
    if (dst == nullptr) {
        err = dst_err.empty() ? std::string("cannot create target profile for ") + to_string(target)
                              : dst_err;
        return nullptr;
    }

    // Source profile. lcms2 only collapses a transform into an exact identity when it
    // recognises both sides as the same profile; a profile that made a byte round trip
    // through the file system loses that (measured dark-end drift up to 1.8e-4 for a
    // serialized sRGB). The target ICCs we embed are exactly those serializations, so a
    // source ICC byte-identical to the target ICC reuses the canonical handle — that
    // keeps sRGB->sRGB (and our own re-processed output) an exact identity, which is the
    // §3.6 numeric contract.
    // NOTE(fact): a third-party sRGB ICC is not byte-identical to our target ICC, so
    // sRGB->sRGB drifts by up to ~1.8e-4 in the dark end (lcms2 optimizer precision).
    // Recorded fact within the §3.6 numeric contract; no action item.
    cmsHPROFILE src = nullptr;
    std::string src_desc;
    if (!src_icc.empty()) {
        const bool canonical =
            !dst_bytes.empty() && OIIO::SHA1::digest(src_icc.data(), src_icc.size()) ==
                                      OIIO::SHA1::digest(dst_bytes.data(), dst_bytes.size());
        if (canonical) {
            src = (target == ColorTarget::SRGB)
                      ? cmsCreate_sRGBProfile()
                      : cmsOpenProfileFromMem(dst_bytes.data(),
                                              static_cast<cmsUInt32Number>(dst_bytes.size()));
        } else {
            src =
                cmsOpenProfileFromMem(src_icc.data(), static_cast<cmsUInt32Number>(src_icc.size()));
        }
        if (src == nullptr) {
            cmsCloseProfile(dst);
            err = "cannot parse source ICC profile (" + std::to_string(src_icc.size()) + " bytes)";
            return nullptr;
        }
        src_desc = icc_label(src);
    } else if (gray_input) {
        src = make_gray_srgb_profile();
        if (src == nullptr) {
            cmsCloseProfile(dst);
            err = "cannot create gray sRGB profile";
            return nullptr;
        }
        src_desc = "assumed gray sRGB";
    } else {
        src = cmsCreate_sRGBProfile();
        if (src == nullptr) {
            cmsCloseProfile(dst);
            err = "cannot create built-in sRGB profile";
            return nullptr;
        }
        src_desc = "assumed sRGB";
    }

    const cmsUInt32Number in_type = gray_input ? TYPE_GRAY_FLT : TYPE_RGB_FLT;
    cmsHTRANSFORM xf =
        cmsCreateTransform(src, in_type, dst, TYPE_RGB_FLT, kIntent, kTransformFlags);
    cmsCloseProfile(src);
    cmsCloseProfile(dst);
    if (xf == nullptr) {
        err = "lcms2 rejected the transform (source profile does not match buffer channels)";
        return nullptr;
    }

    auto entry = std::make_shared<XfEntry>();
    entry->xf = xf;
    entry->src_desc = std::move(src_desc);
    entry->dst_desc = std::move(dst_desc);
    return entry;
}

// ---------------------------------------------------------------------------
// ImageBuf channel-plane helpers
// ---------------------------------------------------------------------------

OIIO::ROI plane_roi(const OIIO::ImageSpec &spec, int chbegin, int chend) {
    return OIIO::ROI(spec.x, spec.x + spec.width, spec.y, spec.y + spec.height, 0, 1, chbegin,
                     chend);
}

bool read_planes(const OIIO::ImageBuf &buf, int chbegin, int chend, float *dst) {
    const OIIO::ImageSpec &spec = buf.spec();
    const std::size_t n =
        std::size_t(spec.width) * std::size_t(spec.height) * std::size_t(chend - chbegin);
    return buf.get_pixels(
        plane_roi(spec, chbegin, chend), OIIO::TypeFloat,
        OIIO::span<std::byte>(reinterpret_cast<std::byte *>(dst), n * sizeof(float)));
}

bool write_planes(OIIO::ImageBuf &buf, int chbegin, int chend, const float *src) {
    const OIIO::ImageSpec &spec = buf.spec();
    const std::size_t n =
        std::size_t(spec.width) * std::size_t(spec.height) * std::size_t(chend - chbegin);
    return buf.set_pixels(
        plane_roi(spec, chbegin, chend), OIIO::TypeFloat,
        OIIO::span<const std::byte>(reinterpret_cast<const std::byte *>(src), n * sizeof(float)));
}

// New float buffer with a different channel count, same window/attributes.
OIIO::ImageBuf make_like(const OIIO::ImageBuf &src, int out_ch, const std::vector<float> &data) {
    OIIO::ImageSpec spec = src.spec();
    spec.nchannels = out_ch;
    spec.channelnames = (out_ch == 4) ? std::vector<std::string>{"R", "G", "B", "A"}
                                      : std::vector<std::string>{"R", "G", "B"};
    spec.channelformats.clear();
    spec.format = OIIO::TypeFloat;
    spec.alpha_channel = (out_ch == 4) ? 3 : -1;
    spec.z_channel = -1;
    OIIO::ImageBuf out(spec);
    const std::size_t n = std::size_t(spec.width) * std::size_t(spec.height) * std::size_t(out_ch);
    out.set_pixels(plane_roi(spec, 0, out_ch), OIIO::TypeFloat,
                   OIIO::span<const std::byte>(reinterpret_cast<const std::byte *>(data.data()),
                                               n * sizeof(float)));
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string to_string(ColorTarget t) {
    switch (t) {
    case ColorTarget::KeepOriginal:
        return "keep";
    case ColorTarget::SRGB:
        return "srgb";
    case ColorTarget::DisplayP3:
        return "p3";
    case ColorTarget::AdobeRGB:
        return "adobergb";
    }
    return "keep";
}

bool parse_color_target(std::string_view s, ColorTarget &out) {
    if (s == "keep") {
        out = ColorTarget::KeepOriginal;
        return true;
    }
    if (s == "srgb") {
        out = ColorTarget::SRGB;
        return true;
    }
    if (s == "p3") {
        out = ColorTarget::DisplayP3;
        return true;
    }
    if (s == "adobergb") {
        out = ColorTarget::AdobeRGB;
        return true;
    }
    return false;
}

std::string load_target_icc(ColorTarget t, std::string &err) {
    err.clear();
    // sRGB has an lcms2 built-in and KeepOriginal has no target at all; both are
    // reported as "nothing to load from assets/icc" (§3.6).
    if (t == ColorTarget::SRGB || t == ColorTarget::KeepOriginal)
        return {};
    return target_icc_bytes(t, err);
}

// ---------------------------------------------------------------------------
// M2-T6 §2.8 — CICP (H.273) → source description (frozen enumeration)
// ---------------------------------------------------------------------------

CicpMapping map_cicp_source(const Cicp &cicp) {
    // Hit line is frozen as `CICP (<p>,<t>) → <profile 名>`; the miss text is the
    // §2.8 sentence and reports the transfer value only.
    const auto hit = [&cicp](CicpSource s, const char *name) {
        CicpMapping m;
        m.source = s;
        m.name = name;
        m.src_icc = cicp_source_icc(s);
        m.log_line = "CICP (" + std::to_string(cicp.primaries) + "," +
                     std::to_string(cicp.transfer) + ") → " + name;
        return m;
    };

    // Exactly the four listed pairs; matrix/full_range deliberately do not participate.
    if (cicp.primaries == 1 && cicp.transfer == 13)
        return hit(CicpSource::Srgb, "sRGB");
    if (cicp.primaries == 12 && cicp.transfer == 13)
        return hit(CicpSource::DisplayP3, "Display P3");
    if (cicp.primaries == 12 && cicp.transfer == 1)
        return hit(CicpSource::DisplayP3Gamma22, "Display P3");
    if (cicp.primaries == 9 && cicp.transfer == 8)
        return hit(CicpSource::Bt2020Linear, "BT.2020 linear");
    if (cicp.primaries == 9 && cicp.transfer == 13)
        return hit(CicpSource::Bt2020SrgbTrc, "BT.2020 sRGB-TRC");

    CicpMapping miss; // source stays Unsupported, src_icc stays empty -> assumed sRGB (M1)
    miss.log_line = "CICP transfer " + std::to_string(cicp.transfer) + " 未支持，按 sRGB 处理";
    return miss;
}

struct ColorManager::Impl {
    TransformCache cache;
};

ColorManager::ColorManager() : impl_(new Impl()) {
    // NOTE(design): Impl (and the profile/transform handles it owns) is intentionally
    // neither copied nor freed — ColorManager is a process-lifetime singleton.
}

ColorManager &ColorManager::instance() {
    static ColorManager inst;
    return inst;
}

void ColorManager::clear_cache() { impl_->cache.clear(); }

std::size_t ColorManager::cache_size() const { return impl_->cache.size(); }

ColorOutcome ColorManager::transform(OIIO::ImageBuf &buf, const std::string &src_icc,
                                     bool src_is_gray, ColorTarget target) {
    ColorOutcome oc;
    const OIIO::ImageSpec &spec = buf.spec();
    const int nch = spec.nchannels;
    const int w = spec.width, h = spec.height;

    assert(nch >= 1 && nch <= 4);
    if (nch < 1 || nch > 4) {
        oc.error = "unsupported channel count: " + std::to_string(nch);
        return oc;
    }
    if (w <= 0 || h <= 0) {
        oc.error = "empty image buffer";
        return oc;
    }
    const std::size_t npix = std::size_t(w) * std::size_t(h);
    if (npix > 0xFFFFFFFFull) {
        oc.error = "image too large for an lcms2 transform (pixels > 2^32-1)";
        return oc;
    }

    // Buffer channel convention (§5.2): 1 = gray, 2 = gray+alpha, 3 = RGB,
    // 4 = RGB+alpha. Alpha is never handed to lcms2 and never rewritten.
    const bool gray_input = (nch <= 2);
    const int color_in = gray_input ? 1 : 3;
    const int out_ch = gray_input ? (nch == 1 ? 3 : 4) : nch;
    const std::string assumed_desc = gray_input ? "assumed gray sRGB" : "assumed sRGB";
    if (!src_icc.empty() && src_is_gray && !gray_input) {
        log_debug("color", kLogFile, "src_is_gray hint ignored: buffer has RGB channels",
                  {{"channels", std::to_string(nch)}});
    }

    // ---- KeepOriginal: pixels untouched, embed the source ICC if there is one ----
    if (target == ColorTarget::KeepOriginal) {
        oc.dst_desc = "keep";
        if (src_icc.empty()) {
            oc.src_desc = assumed_desc;
            log_info("color", kLogFile, "keep original pixels, no ICC to embed",
                     {{"channels", std::to_string(nch)}});
        } else {
            oc.src_desc = icc_bytes_label(src_icc);
            oc.icc_to_embed = src_icc;
            log_debug("color", kLogFile, "keep original pixels, source ICC re-embedded",
                      {{"src", oc.src_desc}});
        }
        return oc;
    }

    // ---- transform cache: (source ICC SHA1 | assumed kind, target, in-channels, bitdepth) ----
    const std::string src_key =
        src_icc.empty()
            ? (gray_input ? std::string("assumed:gray-srgb") : std::string("assumed:srgb"))
            : "icc:" + OIIO::SHA1::digest(src_icc.data(), src_icc.size());
    const std::string key = src_key + "|" + std::to_string(static_cast<int>(target)) + "|" +
                            std::to_string(color_in) + "|" + std::to_string(kTransformBits);

    std::shared_ptr<XfEntry> entry = impl_->cache.get(key);
    const bool hit = (entry != nullptr);
    if (!entry) {
        std::string err;
        entry = build_entry(src_icc, gray_input, target, err);
        if (!entry) {
            oc.error = err;
            return oc;
        }
        impl_->cache.put(key, entry);
    }
    oc.src_desc = entry->src_desc;
    oc.dst_desc = entry->dst_desc;

    if (src_icc.empty()) {
        if (gray_input) {
            // Gray sources assume gray sRGB: no NoIccAssumeSrgb warning by ruling.
            log_debug("color", kLogFile, "no ICC profile; assumed gray sRGB",
                      {{"channels", std::to_string(nch)}});
        } else {
            oc.warnings.push_back(
                {WarningKind::NoIccAssumeSrgb, "no ICC profile found; assumed sRGB"});
            log_warn("color", kLogFile, "no ICC profile; assumed sRGB",
                     {{"channels", std::to_string(nch)}});
        }
    }

    // ICC that the encoder/metadata stage should embed for the converted output.
    std::string embed_err;
    oc.icc_to_embed = target_icc_bytes(target, embed_err);
    if (oc.icc_to_embed.empty()) {
        oc.error = embed_err.empty() ? "cannot serialize target ICC profile" : embed_err;
        return oc;
    }

    log_debug("color", kLogFile, "colour transform applied",
              {{"src", oc.src_desc},
               {"dst", oc.dst_desc},
               {"channels", std::to_string(nch)},
               {"out_channels", std::to_string(out_ch)},
               {"cache", hit ? "hit" : "miss"}});

    // ---- pixels: colour planes go through lcms2, alpha is copied verbatim ----
    const cmsUInt32Number count = static_cast<cmsUInt32Number>(npix);
    if (color_in == 1) {
        // Single-channel ROIs read back as contiguous planes; the lcms2 buffers
        // themselves are interleaved (there is no float planar format in lcms2).
        std::vector<float> gray(npix);
        if (!read_planes(buf, 0, 1, gray.data())) {
            oc.error = "cannot read the grayscale plane";
            return oc;
        }
        std::vector<float> alpha;
        if (nch == 2) {
            alpha.resize(npix);
            if (!read_planes(buf, 1, 2, alpha.data())) {
                oc.error = "cannot read the alpha plane";
                return oc;
            }
        }

        std::vector<float> rgb(3 * npix); // single GRAY_FLT -> RGB_FLT call
        cmsDoTransform(entry->xf, gray.data(), rgb.data(), count);

        std::vector<float> out(std::size_t(out_ch) * npix);
        for (std::size_t i = 0; i < npix; ++i) {
            out[std::size_t(out_ch) * i + 0] = rgb[3 * i + 0];
            out[std::size_t(out_ch) * i + 1] = rgb[3 * i + 1];
            out[std::size_t(out_ch) * i + 2] = rgb[3 * i + 2];
            if (out_ch == 4)
                out[std::size_t(out_ch) * i + 3] = alpha[i];
        }
        // NOTE(design): gray and gray+alpha sources are always promoted to RGB(A) for a
        // colour target (single GRAY->RGB call, no separate gray pipeline) — the
        // established design of this pipeline.
        buf = make_like(buf, out_ch, out);
    } else {
        std::vector<float> in(3 * npix);
        std::vector<float> out(3 * npix);
        if (!read_planes(buf, 0, 3, in.data())) {
            oc.error = "cannot read the colour planes";
            return oc;
        }
        cmsDoTransform(entry->xf, in.data(), out.data(), count);
        if (!write_planes(buf, 0, 3, out.data())) {
            oc.error = "cannot write the colour planes";
            return oc;
        }
    }
    return oc;
}

} // namespace pp
