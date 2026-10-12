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
#include <cmath>
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

// Fixed intent/flags (§3.6 numeric contract) —— C3 档位 0 的映射（现状逐字，含金样 20 对）。
constexpr int kIntent = INTENT_RELATIVE_COLORIMETRIC;
constexpr cmsUInt32Number kTransformFlags = cmsFLAGS_BLACKPOINTCOMPENSATION;

// —— C3（UX-14 C3，M7-R2A）：渲染意图档位 → lcms2 (intent, flags) ——
// 设计口径（主对话定稿，禁止改算法口径）：0 → RC+BPC（现状）/ 1 → Perceptual /
// 2 → Absolute Colorimetric（**不叠 BPC**）/ 3 → Saturation。1/3 按字面不叠 BPC
// （仅 0 携带 BPC；2 明示不叠 —— 绝对比色自带黑点语义）。越界档位按 0 处理（合法档位
// 不静默改写；档位校验归 validate_run_config 面）。
struct IntentFlags {
    int intent;
    cmsUInt32Number flags;
};
IntentFlags intent_flags_of(int mode) {
    switch (mode) {
    case 1:
        return {INTENT_PERCEPTUAL, 0};
    case 2:
        return {INTENT_ABSOLUTE_COLORIMETRIC, 0};
    case 3:
        return {INTENT_SATURATION, 0};
    default:
        return {kIntent, kTransformFlags};
    }
}

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

// —— R-296（M7-R2A）：CICP 映射**表驱动**单源 ——
// §2.8 冻结枚举的每对 (primaries, transfer) 恰好一行；map_cicp_source（命中判定 + 日志）与
// cicp_profile_spec（源 profile 规格）都从本表取值 ⇒ 加一对映射 = 加一行（+ 枚举末尾追加
// CicpSource 值；需新规格时再加一个 TargetSpec 常量），联动点 ≤ 3 处（<4）。
// 既有五对的行为（含 name 文案与 src_icc 生成）逐字不变。
struct CicpRow {
    int primaries, transfer;
    CicpSource source;
    const char *name;
    const TargetSpec *spec; // nullptr = 不生成 profile（sRGB 走 lcms2 内建）
};

constexpr CicpRow kCicpTable[] = {
    {1, 13, CicpSource::Srgb, "sRGB", nullptr},
    {12, 13, CicpSource::DisplayP3, "Display P3", &kP3Spec},
    {12, 1, CicpSource::DisplayP3Gamma22, "Display P3", &kP3Gamma22Spec},
    {9, 8, CicpSource::Bt2020Linear, "BT.2020 linear", &kBt2020LinearSpec},
    {9, 13, CicpSource::Bt2020SrgbTrc, "BT.2020 sRGB-TRC", &kBt2020SrgbSpec},
};

// M2-T6 §2.8: the source-profile spec behind every recognized CICP pair (nullptr for
// Srgb — lcms2's built-in — and for Unsupported, which never gets a profile).
const TargetSpec *cicp_profile_spec(CicpSource s) {
    for (const CicpRow &row : kCicpTable) {
        if (row.source == s)
            return row.spec;
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
    std::string degrade_note; // R-032：ICC 通道不匹配的降级说明（缓存命中也要逐次告警）
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
                                     ColorTarget target, int intent_mode, std::string &err,
                                     std::string *degrade_note = nullptr) {
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
        // R-032（UX-14 A2，数据正确性）：通道不匹配的 ICC（灰度源带 RGB ICC —— 相机/扫描件
        // 常态；或 RGB 源带灰度 ICC）不再让 lcms2 拒 transform → 整文件失败。降级口径 =
        // **忽略该 ICC、按假定处理**（灰度 → assumed gray sRGB；RGB → assumed sRGB），
        // 并经 out_warnings 报 Warning（原实现直接 `lcms2 rejected the transform` 整文件废）。
        const cmsUInt32Number icc_channels = cmsChannelsOf(cmsGetColorSpace(src));
        const bool icc_is_gray = (icc_channels == 1);
        if (icc_is_gray != gray_input) {
            const std::string note = std::string("incompatible ICC profile ignored (") +
                                     (gray_input ? "RGB profile on a grayscale source"
                                                 : "grayscale profile on an RGB source") +
                                     "); assumed " + (gray_input ? "gray sRGB" : "sRGB");
            if (degrade_note != nullptr)
                *degrade_note = note;
            log_warn("color", kLogFile, note.c_str(),
                     {{"icc_channels", std::to_string(icc_channels)},
                      {"buffer_channels", std::string(gray_input ? "gray" : "rgb")}});
            cmsCloseProfile(src);
            src = gray_input ? make_gray_srgb_profile() : cmsCreate_sRGBProfile();
            if (src == nullptr) {
                cmsCloseProfile(dst);
                err = "cannot create fallback profile after ICC mismatch";
                return nullptr;
            }
            src_desc = gray_input ? "assumed gray sRGB" : "assumed sRGB";
        }
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
    // C3：意图/flags 按档位映射（0 = §3.6 现状逐字）。
    const IntentFlags ifx = intent_flags_of(intent_mode);
    cmsHTRANSFORM xf = cmsCreateTransform(src, in_type, dst, TYPE_RGB_FLT, ifx.intent, ifx.flags);
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
    if (degrade_note != nullptr)
        entry->degrade_note = *degrade_note; // 缓存命中时也要能逐次告警（transform 读它）
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

// ---------------------------------------------------------------------------
// C1（UX-14 C1，M7-R2A）：色域映射（ColorOptions::gamut_mapping）
// ---------------------------------------------------------------------------
// 设计口径（主对话定稿，禁止改算法口径）：
//   mode0 = 现状硬裁（变换输出原样，量化段 clamp —— 默认档位逐字节不变）；
//   mode1 = 软裁：cmsDoTransform 后对越界分量软膝压缩（knee=0.85：
//           x>0.85 → y=0.85+0.15·tanh((x-0.85)/0.15)；负侧对称）；
//   mode2 = 感知：越界像素向明度轴收敛（保明度、压色度）。
// 只作用于变换后的 RGB 三分量；alpha 不经手（§5.2）。
float soft_knee(float x) {
    constexpr float kKnee = 0.85f;
    constexpr float kRange = 0.15f;
    if (x > kKnee)
        return kKnee + kRange * std::tanh((x - kKnee) / kRange);
    if (x < -kKnee)
        return -kKnee - kRange * std::tanh((-x - kKnee) / kRange);
    return x;
}

// ---- mode2：向明度轴收敛（保亮度、压色度）----
// "明度轴" = Lab 的 L 轴（a=b=0）。口径 = L 不变、(a,b) 向 0 等比收敛（色度压缩），直到
// 回投的目标 RGB 落回 [0,1]³；色相与明度由构造保持（与理想（未裁）结果的 ΔE2000 色相
// 分量 ≈ 0 —— 护栏的构造性保证）。Lab 空间按**目标色域**的基 + TRC 构造（D65；sRGB/P3 =
// sRGB TRC，AdobeRGB = 纯 gamma 2.2）。只处理越界像素（域内像素不动）。
struct LabSpace {
    double m[9];    // linear RGB → XYZ（列主基）
    double minv[9]; // XYZ → linear RGB
    bool gamma22 = false;
};

LabSpace lab_space_for(ColorTarget t) {
    // 目标基（xy，D65）：sRGB / AdobeRGB 用标准基；P3 用 kP3Spec 的基。
    double rx = 0.640, ry = 0.330, gx = 0.300, gy = 0.600, bx = 0.150, by = 0.060;
    bool g22 = false;
    if (t == ColorTarget::DisplayP3) {
        rx = kP3Spec.rx;
        ry = kP3Spec.ry;
        gx = kP3Spec.gx;
        gy = kP3Spec.gy;
        bx = kP3Spec.bx;
        by = kP3Spec.by;
    } else if (t == ColorTarget::AdobeRGB) {
        rx = kAdobeSpec.rx;
        ry = kAdobeSpec.ry;
        gx = kAdobeSpec.gx;
        gy = kAdobeSpec.gy;
        bx = kAdobeSpec.bx;
        by = kAdobeSpec.by;
        g22 = true;
    }
    const double col[3][3] = {{rx / ry, gx / gy, bx / by},
                              {1, 1, 1},
                              {(1 - rx - ry) / ry, (1 - gx - gy) / gy, (1 - bx - by) / by}};
    const double w[3] = {0.3127 / 0.3290, 1.0, (1 - 0.3127 - 0.3290) / 0.3290};
    // s = C⁻¹·w（3×3 手工求逆：伴随矩阵/行列式）
    const double *c = &col[0][0];
    const double det = c[0] * (c[4] * c[8] - c[5] * c[7]) - c[1] * (c[3] * c[8] - c[5] * c[6]) +
                       c[2] * (c[3] * c[7] - c[4] * c[6]);
    LabSpace sp;
    sp.gamma22 = g22;
    if (det == 0.0)
        return sp;
    const double inv[9] = {(c[4] * c[8] - c[5] * c[7]) / det, (c[2] * c[7] - c[1] * c[8]) / det,
                           (c[1] * c[5] - c[2] * c[4]) / det, (c[5] * c[6] - c[3] * c[8]) / det,
                           (c[0] * c[8] - c[2] * c[6]) / det, (c[2] * c[3] - c[0] * c[5]) / det,
                           (c[3] * c[7] - c[4] * c[6]) / det, (c[1] * c[6] - c[0] * c[7]) / det,
                           (c[0] * c[4] - c[1] * c[3]) / det};
    double s[3];
    for (int i = 0; i < 3; ++i)
        s[i] = inv[3 * i + 0] * w[0] + inv[3 * i + 1] * w[1] + inv[3 * i + 2] * w[2];
    // 行主序 m[3i+j] = A[i][j]（与下方求逆公式的约定一致）
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            sp.m[3 * i + j] = col[i][j] * s[j];
    // minv = (C·diag(s))⁻¹
    const double *m = sp.m;
    const double mdet = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[3] * (m[1] * m[8] - m[2] * m[7]) +
                        m[6] * (m[1] * m[5] - m[2] * m[4]);
    if (mdet == 0.0)
        return sp;
    sp.minv[0] = (m[4] * m[8] - m[5] * m[7]) / mdet;
    sp.minv[1] = (m[2] * m[7] - m[1] * m[8]) / mdet;
    sp.minv[2] = (m[1] * m[5] - m[2] * m[4]) / mdet;
    sp.minv[3] = (m[5] * m[6] - m[3] * m[8]) / mdet;
    sp.minv[4] = (m[0] * m[8] - m[2] * m[6]) / mdet;
    sp.minv[5] = (m[2] * m[3] - m[0] * m[5]) / mdet;
    sp.minv[6] = (m[3] * m[7] - m[4] * m[6]) / mdet;
    sp.minv[7] = (m[1] * m[6] - m[0] * m[7]) / mdet;
    sp.minv[8] = (m[0] * m[4] - m[1] * m[3]) / mdet;
    return sp;
}

double decode_trc(double x, bool gamma22) {
    if (gamma22) {
        const double s = x < 0.0 ? -1.0 : 1.0;
        return s * std::pow(std::fabs(x), 2.2);
    }
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}
double encode_trc(double y, bool gamma22) {
    if (gamma22) {
        const double s = y < 0.0 ? -1.0 : 1.0;
        return s * std::pow(std::fabs(y), 1.0 / 2.2);
    }
    return y <= 0.0031308 ? y * 12.92 : 1.055 * std::pow(y, 1.0 / 2.4) - 0.055;
}

void rgb_to_lab(const LabSpace &sp, const double rgb[3], double lab[3]) {
    const double lin[3] = {decode_trc(rgb[0], sp.gamma22), decode_trc(rgb[1], sp.gamma22),
                           decode_trc(rgb[2], sp.gamma22)};
    const double X = sp.m[0] * lin[0] + sp.m[1] * lin[1] + sp.m[2] * lin[2];
    const double Y = sp.m[3] * lin[0] + sp.m[4] * lin[1] + sp.m[5] * lin[2];
    const double Z = sp.m[6] * lin[0] + sp.m[7] * lin[1] + sp.m[8] * lin[2];
    const auto f = [](double t) {
        return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0;
    };
    const double fx = f(X / 0.95047), fy = f(Y / 1.0), fz = f(Z / 1.08883);
    lab[0] = 116.0 * fy - 16.0;
    lab[1] = 500.0 * (fx - fy);
    lab[2] = 200.0 * (fy - fz);
}

void lab_to_rgb(const LabSpace &sp, const double lab[3], double rgb[3]) {
    const double fy = (lab[0] + 16.0) / 116.0;
    const double fx = fy + lab[1] / 500.0;
    const double fz = fy - lab[2] / 200.0;
    const auto finv = [](double t) {
        const double t3 = t * t * t;
        return t3 > 216.0 / 24389.0 ? t3 : (116.0 * t - 16.0) * (27.0 / 24389.0);
    };
    const double X = 0.95047 * finv(fx), Y = 1.0 * finv(fy), Z = 1.08883 * finv(fz);
    const double lin[3] = {sp.minv[0] * X + sp.minv[1] * Y + sp.minv[2] * Z,
                           sp.minv[3] * X + sp.minv[4] * Y + sp.minv[5] * Z,
                           sp.minv[6] * X + sp.minv[7] * Y + sp.minv[8] * Z};
    for (int i = 0; i < 3; ++i)
        rgb[i] = encode_trc(lin[i], sp.gamma22);
}

void converge_to_lightness(const LabSpace &sp, float &r, float &g, float &b) {
    if (r >= 0.0f && r <= 1.0f && g >= 0.0f && g <= 1.0f && b >= 0.0f && b <= 1.0f)
        return; // 域内像素不动
    const double in[3] = {double(r), double(g), double(b)};
    double lab[3];
    rgb_to_lab(sp, in, lab);
    lab[0] = std::clamp(lab[0], 0.0, 100.0); // 不可表示明度夹到最近可表示值
    // s ∈ [0,1]：Lab(L, s·a, s·b) 回投目标 RGB 落回 [0,1]³ 的最大系数（二分；色相与 L 保持）
    double lo = 0.0, hi = 1.0;
    for (int it = 0; it < 16; ++it) {
        const double mid = 0.5 * (lo + hi);
        const double l2[3] = {lab[0], mid * lab[1], mid * lab[2]};
        double out[3];
        lab_to_rgb(sp, l2, out);
        const bool ok = out[0] >= -1e-9 && out[0] <= 1.0 + 1e-9 && out[1] >= -1e-9 &&
                        out[1] <= 1.0 + 1e-9 && out[2] >= -1e-9 && out[2] <= 1.0 + 1e-9;
        if (ok)
            lo = mid;
        else
            hi = mid;
    }
    const double l3[3] = {lab[0], lo * lab[1], lo * lab[2]};
    double out[3];
    lab_to_rgb(sp, l3, out);
    r = static_cast<float>(std::clamp(out[0], 0.0, 1.0));
    g = static_cast<float>(std::clamp(out[1], 0.0, 1.0));
    b = static_cast<float>(std::clamp(out[2], 0.0, 1.0));
}

void apply_gamut_mapping(float *rgb, std::size_t npix, int mode, ColorTarget target) {
    if (mode == 1) {
        for (std::size_t i = 0; i < 3 * npix; ++i)
            rgb[i] = soft_knee(rgb[i]);
    } else if (mode == 2) {
        const LabSpace sp = lab_space_for(target);
        for (std::size_t i = 0; i < npix; ++i)
            converge_to_lightness(sp, rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]);
    }
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
    // R-296（M7-R2A）：命中判定走 kCicpTable 单源（加一对 = 加一行）。
    // Hit line is frozen as `CICP (<p>,<t>) → <profile 名>`; matrix/full_range
    // deliberately do not participate (§2.8).
    for (const CicpRow &row : kCicpTable) {
        if (row.primaries == cicp.primaries && row.transfer == cicp.transfer) {
            CicpMapping m;
            m.source = row.source;
            m.name = row.name;
            m.src_icc = cicp_source_icc(row.source);
            m.log_line = "CICP (" + std::to_string(cicp.primaries) + "," +
                         std::to_string(cicp.transfer) + ") → " + row.name;
            return m;
        }
    }

    CicpMapping miss; // source stays Unsupported, src_icc stays empty -> assumed sRGB (M1)
    // UX-14 A5（M7-WP1）：日志与行为一致 —— 未支持的 CICP 组合**不构造** sRGB 假定，
    // pipeline 保留 OIIO 合成的 ICC 继续处理（见 pipeline.cpp 的 CICP 中间步：miss 时
    // src_icc 不被覆盖）。原文案"按 sRGB 处理"与行为不符（谎报处理口径），已改。
    // D3（M7-R2A）：HDR 传输特性（PQ=16 / HLG=18，含 BT.2020/BT.2100 基的组合）给**明确**
    // 文案 —— 负面清单不建 HDR 管线、也不拒收：保留合成 ICC 处理、不做 sRGB 重解释。
    if (cicp.transfer == 16 || cicp.transfer == 18) {
        miss.log_line = "HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释";
    } else {
        miss.log_line = "CICP transfer " + std::to_string(cicp.transfer) +
                        " 未支持，保留合成 ICC 处理（不按 sRGB 重解释）";
    }
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
    // I3：旧签名保留并转发（默认 opts + 空 warnings 通道）——既有调用点零改动、零行为变化。
    return transform(buf, src_icc, src_is_gray, target, ColorOptions{}, nullptr, nullptr);
}

ColorOutcome ColorManager::transform(OIIO::ImageBuf &buf, const std::string &src_icc,
                                     bool src_is_gray, ColorTarget target, const ColorOptions &opts,
                                     std::vector<Warning> *out_warnings, std::string *err) {
    // I3 能力档位的消费分工（M7-R2A 起）：intent（C3，本函数 = 变换缓存键 + lcms2 意图）、
    // gamut_mapping（C1，变换后 RGB 后处理）在本层消费；dither8（C2）在编码器 to_u8 量化段
    // 消费（simd::quantize8_dithered）；linear_resample（C4）在显示面（thumbs）消费。
    // 默认值（0/false/0/false）与旧 `ColorOptions{}` 逐字等价 → 默认路径零行为变化。
    int intent_mode = opts.intent;
    if (intent_mode < 0 || intent_mode > 3) {
        log_warn("color", kLogFile, "unknown render intent mode; falling back to 0",
                 {{"intent", std::to_string(intent_mode)}});
        intent_mode = 0;
    }
    if (err != nullptr)
        err->clear();
    const auto note = [out_warnings](WarningKind kind, const std::string &detail) {
        if (out_warnings != nullptr)
            out_warnings->push_back(Warning{kind, detail});
    };
    ColorOutcome oc;
    struct ErrSync { // 出口统一把 oc.error 镜像到 *err（多返回点不漏）
        ColorOutcome *o;
        std::string *e;
        ~ErrSync() {
            if (e != nullptr)
                *e = o->error;
        }
    } err_sync{&oc, err};
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
    // C3：键必须并入 intent/flags —— 否则不同渲染意图串用同一 cmsHTRANSFORM（flags 由
    // intent_mode 唯一派生，两者都入键 = 键与变换语义一一对应）。
    const IntentFlags ifx = intent_flags_of(intent_mode);
    const std::string src_key =
        src_icc.empty()
            ? (gray_input ? std::string("assumed:gray-srgb") : std::string("assumed:srgb"))
            : "icc:" + OIIO::SHA1::digest(src_icc.data(), src_icc.size());
    const std::string key = src_key + "|" + std::to_string(static_cast<int>(target)) + "|" +
                            std::to_string(color_in) + "|" + std::to_string(kTransformBits) + "|" +
                            std::to_string(ifx.intent) + "|" + std::to_string(ifx.flags);

    std::shared_ptr<XfEntry> entry = impl_->cache.get(key);
    const bool hit = (entry != nullptr);
    if (!entry) {
        std::string build_err;
        std::string degrade_note;
        entry = build_entry(src_icc, gray_input, target, intent_mode, build_err, &degrade_note);
        if (!entry) {
            oc.error = build_err;
            return oc;
        }
        impl_->cache.put(key, entry);
    }
    oc.src_desc = entry->src_desc;
    oc.dst_desc = entry->dst_desc;
    // R-032（UX-14 A2）：降级告警逐次经 out_warnings 输出（含缓存命中 —— 每个文件都要看到）
    if (!entry->degrade_note.empty())
        note(WarningKind::NoIccAssumeSrgb, entry->degrade_note);

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

    // R-035：灰度源被升维成 RGB(A)（彩色目标的既定设计：单次 GRAY_FLT→RGB_FLT，无独立灰度
    // 管线）——体积 ~3×，与 supports_gray 的"保持灰度"承诺不同，升维必须有告警（原实现静默）。
    if (gray_input) {
        note(WarningKind::GrayToRgbEncoded,
             std::string("grayscale source promoted to ") + (out_ch == 4 ? "RGBA" : "RGB") +
                 " by the color transform (target " + to_string(target) + ")");
    }

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
        // C1：色域映射（mode0 = 硬裁现状 = 无操作；灰度升维后为中性轴，模式 1/2 亦无损中性）
        apply_gamut_mapping(rgb.data(), npix, opts.gamut_mapping, target);

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
        // C1：色域映射（mode0 = 硬裁现状 = 无操作，逐字节不变）
        apply_gamut_mapping(out.data(), npix, opts.gamut_mapping, target);
        if (!write_planes(buf, 0, 3, out.data())) {
            oc.error = "cannot write the colour planes";
            return oc;
        }
    }
    return oc;
}

} // namespace pp
