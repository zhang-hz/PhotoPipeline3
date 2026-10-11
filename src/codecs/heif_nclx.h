// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — HEIF/AVIF nclx 色彩描述与嵌入 ICC 的一致性推导（M7-WP2 · UX-14 A1）。
//
// 口径（docs/ux-review-14.md §14 A1）：nclx **必须描述像素实际所在的 RGB 空间** —— 不得
// 在 P3/AdobeRGB 目标下写死 sRGB（旧实现恒写 sRGB，与同组 ICC 互相矛盾 ⇒ 不同解码器显示
// 不一致，ICC 嵌入失败时甚至整图偏色）。管线把色彩目标（或保持原样时的源 profile）的
// ICC 字节放进 `MetadataPayloads::icc_profile`，本头从该 ICC 的色度基 + TRC 反推 CICP 码：
//   * 矩阵系数不受影响 —— YCbCr 变换确实用 BT.601 全范围（enc_heif 像素段），由调用方照写；
//   * 无法识别的色域 → `unspecified`（2）**不冒充 sRGB**（ICC 仍携带真相）；
//   * 无 ICC（空）→ sRGB nclx（0.2/0.3.0 现状口径）。
// 纯函数、可单测（tests/unit/test_enc_oiio 的 nclx 用例直测本函数；编码器侧的文件级断言
// 受 libheif 行为约束：ICC 在场时只写 `colr/prof` 一个 box、nclx 被丢 —— 见测试注释）。
#pragma once
#include <cmath>
#include <lcms2.h>
#include <libheif/heif.h>
#include <string>

namespace pp {

struct NclxDesc {
    heif_color_primaries primaries = heif_color_primaries_ITU_R_BT_709_5;
    heif_transfer_characteristics trc = heif_transfer_characteristic_IEC_61966_2_1;
};

namespace nclx_detail {

// 参考色度基（D50 适配后的 as-stored 值）：ICC 规范要求 rXYZ/gXYZ/bXYZ 存**D50 适配**色度基
// （cmsCreateRGBProfile 亦如此生成），拿标称 xy（D65 域）直接比会永远比不中。故参考值同样
// 经 lcms2 构造后取 as-stored 色度 —— 同源对比，适配口径自动一致。
struct RefPrimaries {
    double rx, ry, gx, gy, bx, by;
};

inline bool icc_chromaticity(cmsHPROFILE p, cmsTagSignature tag, double &x, double &y) {
    const auto *v = static_cast<const cmsCIEXYZ *>(cmsReadTag(p, tag));
    if (v == nullptr)
        return false;
    const double sum = v->X + v->Y + v->Z;
    if (!(sum > 0.0))
        return false;
    x = v->X / sum;
    y = v->Y / sum;
    return true;
}

inline bool read_primaries(cmsHPROFILE p, RefPrimaries &out) {
    return icc_chromaticity(p, cmsSigRedColorantTag, out.rx, out.ry) &&
           icc_chromaticity(p, cmsSigGreenColorantTag, out.gx, out.gy) &&
           icc_chromaticity(p, cmsSigBlueColorantTag, out.bx, out.by);
}

// 构造参考 profile 并取 as-stored 色度基（失败返回 false）。
inline bool ref_primaries_srgb(RefPrimaries &out) {
    cmsHPROFILE p = cmsCreate_sRGBProfile();
    if (!p)
        return false;
    const bool ok = read_primaries(p, out);
    cmsCloseProfile(p);
    return ok;
}

inline bool ref_primaries_matrix(double rx, double ry, double gx, double gy, double bx, double by,
                                 double gamma, RefPrimaries &out) {
    const cmsCIExyY d65{0.3127, 0.3290, 1.0};
    const cmsCIExyYTRIPLE prim{{rx, ry, 1.0}, {gx, gy, 1.0}, {bx, by, 1.0}};
    cmsToneCurve *trc = cmsBuildGamma(nullptr, gamma);
    if (!trc)
        return false;
    cmsToneCurve *trio[3] = {trc, trc, trc};
    cmsHPROFILE p = cmsCreateRGBProfile(&d65, &prim, trio);
    cmsFreeToneCurve(trc);
    if (!p)
        return false;
    const bool ok = read_primaries(p, out);
    cmsCloseProfile(p);
    return ok;
}

inline bool near_xy(double x, double y, double rx, double ry, double tol) {
    return std::fabs(x - rx) <= tol && std::fabs(y - ry) <= tol;
}

inline bool same_primaries(const RefPrimaries &a, const RefPrimaries &b, double tol = 0.005) {
    return near_xy(a.rx, a.ry, b.rx, b.ry, tol) && near_xy(a.gx, a.gy, b.gx, b.gy, tol) &&
           near_xy(a.bx, a.by, b.bx, b.by, tol);
}

} // namespace nclx_detail

inline NclxDesc nclx_for_icc(const std::string &icc) {
    NclxDesc d;
    if (icc.empty())
        return d; // 无 ICC：sRGB nclx（现状口径）
    cmsHPROFILE p = cmsOpenProfileFromMem(icc.data(), static_cast<cmsUInt32Number>(icc.size()));
    if (p == nullptr)
        return d;
    nclx_detail::RefPrimaries got{};
    const bool have = nclx_detail::read_primaries(p, got);
    double gamma = 0.0;
    // TRC 取红/灰通道曲线（ICC 每通道一条；我们生成的目标 profile 三通道同曲线）。
    const cmsToneCurve *trc = static_cast<const cmsToneCurve *>(cmsReadTag(p, cmsSigRedTRCTag));
    if (trc == nullptr)
        trc = static_cast<const cmsToneCurve *>(cmsReadTag(p, cmsSigGrayTRCTag));
    if (trc != nullptr) {
        if (cmsIsToneCurveLinear(trc) != 0)
            gamma = 1.0;
        else
            gamma = cmsEstimateGamma(trc, 0.001);
    }
    cmsCloseProfile(p);
    if (!have)
        return d; // 读不出色度基：保守维持 sRGB

    // TRC 分档：线性 → linear；≈2.2（AdobeRGB 纯 gamma 2.1992）→ H.273 code 4（gamma 2.2）；
    // 其余（sRGB 分段曲线估计值亦在 2.2 附近，须与 primaries 联合判读）按下述 primaries 规则。
    const bool linear = gamma > 0.9 && gamma < 1.1;
    const bool pure_gamma22 = gamma > 2.15 && gamma < 2.25;

    // 参考表（as-stored，同源对比）：sRGB / Display P3 / Adobe RGB / BT.2020。
    // Adobe RGB 与 sRGB 共享 R/B 基、G 基不同 ⇒ 可分。Adobe RGB 无 CICP primaries 码
    // ⇒ 命中它时输出 unspecified（不冒充 sRGB，UX-14 A1 的核心病灶）。
    nclx_detail::RefPrimaries ref_srgb{}, ref_p3{}, ref_argb{}, ref_2020{};
    const bool ok_srgb = nclx_detail::ref_primaries_srgb(ref_srgb);
    const bool ok_p3 =
        nclx_detail::ref_primaries_matrix(0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 2.2, ref_p3);
    const bool ok_argb = nclx_detail::ref_primaries_matrix(0.640, 0.330, 0.210, 0.710, 0.150, 0.060,
                                                           563.0 / 256.0, ref_argb);
    const bool ok_2020 =
        nclx_detail::ref_primaries_matrix(0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 2.2, ref_2020);
    if (ok_srgb && nclx_detail::same_primaries(got, ref_srgb)) {
        d.primaries = heif_color_primaries_ITU_R_BT_709_5;
        d.trc = linear ? heif_transfer_characteristic_linear
                       : heif_transfer_characteristic_IEC_61966_2_1;
    } else if (ok_p3 && nclx_detail::same_primaries(got, ref_p3)) {
        // Display P3 = SMPTE EG 432-1 基 + D65（TRC 定义即 sRGB 曲线）。
        d.primaries = heif_color_primaries_SMPTE_EG_432_1;
        d.trc = linear ? heif_transfer_characteristic_linear
                       : heif_transfer_characteristic_IEC_61966_2_1;
    } else if (ok_argb && nclx_detail::same_primaries(got, ref_argb)) {
        // Adobe RGB (1998)：CICP 无对应 primaries 码 → unspecified（ICC 带真相）。
        d.primaries = heif_color_primaries_unspecified;
        d.trc = linear         ? heif_transfer_characteristic_linear
                : pure_gamma22 ? heif_transfer_characteristic_ITU_R_BT_470_6_System_M
                               : heif_transfer_characteristic_unspecified;
    } else if (ok_2020 && nclx_detail::same_primaries(got, ref_2020)) {
        d.primaries = heif_color_primaries_ITU_R_BT_2020_2_and_2100_0;
        d.trc = linear ? heif_transfer_characteristic_linear
                       : heif_transfer_characteristic_IEC_61966_2_1;
    } else {
        d.primaries = heif_color_primaries_unspecified;
        d.trc =
            linear ? heif_transfer_characteristic_linear : heif_transfer_characteristic_unspecified;
    }
    return d;
}

} // namespace pp
