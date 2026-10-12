// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — EXIF/XMP 时间标签集合单源（deep-review-0.3.2.md R-197：4 份真相收敛）
//
// 契约（M7-R2.1-F3）：原 exif_editor.cpp 内 4 份字面量 —— 清除集 kExifTimeKeys[3] +
// kXmpTimeKeys[2] 与命中集 is_exif_time_key(6 键) / is_xmp_time_key(3 键) —— 已静默漂移
// （清除 5 键 vs 命中 9 键，deep-review R-197/E5-07）。单表收口，每键显式角色：
//   * wall_clock = **墙钟时间标签**（与引擎写路径 core/metadata.cpp 的 kExifTimeKeys/
//     kXmpTimeKeys 逐键相同，5 键 —— 两侧同集是契约，test_smoke 钉住；**角色标注**用，
//     自 M7R22G2-0 起不再是清除集）；
//   * touched = **时间规则触达键**（wall_clock + OffsetTime* 3 + Xmp MetadataDate，9 键），
//     〔批〕命中标记用（time_shift 连带改写 OffsetTime*，见 core/metadata.h R-234）。
// M7R22G2-0（主对话行为裁定，2026-10-13）：时间「清除」语义 = **连带 touched 全部 9 键**
// （与 shift 命中面同集，消「清除 3 键 vs 命中 9 键」漂移）——清除谓词 = is_time_clear_key；
// 原「清除仍只动 wall_clock（现状字节级一致）」口径随裁定换锚（断言换锚登记见
// build/m7r22-g2/handoff.md）。
// header-only（R-109 路线，同 format_policy.h / byte_text.h）；tests/unit/test_smoke.cpp 钉真值表。
#pragma once

#include <cstddef>
#include <string>

namespace pp::ui::timetags {

struct TimeTag {
    const char *key;
    bool xmp;        // XMP 侧（false = EXIF 侧）
    bool wall_clock; // 墙钟时间标签（= 引擎写路径同集的角色标注）；false = 时区/元数据日期
};

inline constexpr TimeTag kTimeTags[] = {
    {"Exif.Photo.DateTimeOriginal", false, true},
    {"Exif.Photo.DateTimeDigitized", false, true},
    {"Exif.Image.DateTime", false, true},
    {"Exif.Photo.OffsetTime", false, false},
    {"Exif.Photo.OffsetTimeOriginal", false, false},
    {"Exif.Photo.OffsetTimeDigitized", false, false},
    {"Xmp.xmp.CreateDate", true, true},
    {"Xmp.xmp.ModifyDate", true, true},
    {"Xmp.xmp.MetadataDate", true, false},
};

inline constexpr std::size_t kTimeTagCount = sizeof(kTimeTags) / sizeof(kTimeTags[0]);

// 命中谓词（touched 集）：时间规则会触达的键（EXIF/XMP 各一）。
inline bool is_time_key(const std::string &k, bool xmp) {
    for (std::size_t i = 0; i < kTimeTagCount; ++i) {
        if (kTimeTags[i].xmp == xmp && k == kTimeTags[i].key)
            return true;
    }
    return false;
}

inline bool is_exif_time_key(const std::string &k) { return is_time_key(k, false); }
inline bool is_xmp_time_key(const std::string &k) { return is_time_key(k, true); }

// 墙钟子集谓词（角色标注）：与引擎写路径 kExifTimeKeys/kXmpTimeKeys 同集；自
// M7R22G2-0 起**不是**清除集（清除集 = is_time_clear_key）。
inline bool is_wall_clock_time_key(const std::string &k) {
    for (std::size_t i = 0; i < kTimeTagCount; ++i) {
        if (kTimeTags[i].wall_clock && k == kTimeTags[i].key)
            return true;
    }
    return false;
}

// 清除集谓词（M7R22G2-0 主对话裁定）：时间「清除」连带 touched 全部 9 键 —— 与命中面 /
// shift 触达面同集（OffsetTime* 3、Xmp MetadataDate 一并 remove）。
inline bool is_time_clear_key(const std::string &k, bool xmp) { return is_time_key(k, xmp); }

} // namespace pp::ui::timetags
