// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — HEIF/AVIF 编码档位（profile）单源（M7-WP2 · I4 接口规格，主对话定稿）。
//
// 口径（docs/ux-review-14.md §10「口径澄清」）：这里的 profile = **编码档位**（HEVC 的
// Main / Main10 / Main12 / Main Still Picture，AV1 的 Main / High / Professional），
// 决定**解码兼容性**；**不是**色彩 ICC profile。libheif 的两个编码插件都不暴露 `profile`
// 参数 —— 档位由插件按位深/色度/静止图自动推导，用户不可见。本头文件把该推导规则
// 复刻成**纯函数**，供 UI 档位行（WP6）与编码前反查（enc_heif）共用同一份真相。
//
// 规则复刻（逐条对齐 libheif 1.23.5 插件源；见 docs/ux-review-14.md §10 定位表）：
//   * HEVC（plugins/encoder_x265.cc:793-802）：bit_depth==8 → "main"（静止图 →
//     "mainstillpicture"）；10 → "main10-intra"；12 → "main12-intra"。
//   * AV1（plugins/encoder_svt.cc:946-951）：YUV422 或 bitdepth_y > 10 →
//     PROFESSIONAL_PROFILE；YUV444 → HIGH_PROFILE；其余 → MAIN_PROFILE
//     （静止图模式 svt_config.avif=true 不改档位规则）。
//
// 实际控制面仍是 **位深 + 色度 + 静止图模式**（插件无 profile 参数），故"用户选定档位"
// 的落地语义 = 编码前用 heif_profile_compatible 反查：当前设置产出的档位与选定不一致
// ⇒ **拒绝并报中文错误**（不静默出错档位），见 enc_heif.cpp 的接线。
#pragma once
#include <string>
#include <string_view>

namespace pp {

// 调用面（本波新增声明，追加在文件末尾之外的独立新头 —— 加性扩展零冲突）：
//   HeifProfile derive_heif_profile(bool avif, int bit_depth, int chroma, bool still);
//   std::string heif_profile_label(HeifProfile);
//   bool heif_profile_compatible(HeifProfile want, int bit_depth, int chroma, bool still,
//                                std::string *err_zh);
// chroma 编码：0 = 4:0:0、1 = 4:2:0、2 = 4:2:2、3 = 4:4:4（与 UI 档位行同序）。

// 编码档位枚举（顺序即 UI 下拉顺序：自动 → HEVC 档 → AV1 档）。
enum class HeifProfile {
    Auto = 0,
    Main = 1,
    Main10 = 2,
    Main12 = 3,
    MainStill = 4,
    High = 5,
    Professional = 6,
};

// ParamSet 承载键（**保留键**："__" 前缀 ⇒ 不参与未知参数校验、不落预设、不进日志快照，
// 见 params.h §3.4 约定）。值 = HeifProfile 的 int64_t 序号；缺省 = Auto。
// WP6 的档位行写它，enc_heif 编码前读它做反查。
inline constexpr std::string_view kHeifProfileParamKey = "__heif_profile";

// 插件推导规则的纯函数复刻：给定（是否 AV1 / 位深 / 色度 / 是否静止图）→ 实际产出档位。
// bit_depth ∈ {8,10,12}（越界值按插件行为归入最接近的规则分支并如实推导）。
inline HeifProfile derive_heif_profile(bool avif, int bit_depth, int chroma, bool still) {
    if (!avif) {
        // HEVC：只看位深；8 位静止图走 Main Still Picture（x265 mainstillpicture）。
        if (bit_depth <= 8)
            return still ? HeifProfile::MainStill : HeifProfile::Main;
        if (bit_depth <= 10)
            return HeifProfile::Main10;
        return HeifProfile::Main12;
    }
    // AV1：422 或 >10bit → Professional；444 → High；其余（420/400 的 8/10bit）→ Main。
    if (chroma == 2 || bit_depth > 10)
        return HeifProfile::Professional;
    if (chroma == 3)
        return HeifProfile::High;
    return HeifProfile::Main;
}

// 中文标签（UI 档位行展示；与 docs/ux-review-14.md §10.1 的选项文案同口径）。
inline std::string heif_profile_label(HeifProfile p) {
    switch (p) {
    case HeifProfile::Auto:
        return "自动";
    case HeifProfile::Main:
        return "Main（兼容性最好）";
    case HeifProfile::Main10:
        // x265 对静止图固定写 "main10-intra"（HEVC general_profile_idc=4 / Main10 Intra，
        // 与用户参考 T2.heic 相同）；位深确实是 10。标签注明 Intra，避免工具显示 "Main"
        // 时被误认为选错了位深（R-315）。
        return "Main10（10 位 · Intra）";
    case HeifProfile::Main12:
        return "Main12（12 位）";
    case HeifProfile::MainStill:
        return "Main Still Picture（8 位 · 静止图）";
    case HeifProfile::High:
        return "High（4:4:4）";
    case HeifProfile::Professional:
        return "Professional（12 位 / 4:2:2）";
    }
    return "自动";
}

// 反查：want 是否能由（bit_depth, chroma, still）产出。Auto 恒 true。
// 由于本函数签名不含 avif（I4 定稿），判定口径 = **存在某个编码格式**（HEIF 或 AVIF）
// 使 derive == want —— 档位行的预校验用；编码期的严格反查（固定格式）在 enc_heif 内
// 用 derive_heif_profile 直接比对（同一规则源，无第二套真相）。
// 不一致 → false 且（err_zh 非空时）写中文错误，说明当前设置在两种格式下各自产出什么。
inline bool heif_profile_compatible(HeifProfile want, int bit_depth, int chroma, bool still,
                                    std::string *err_zh) {
    if (want == HeifProfile::Auto)
        return true;
    const HeifProfile heif_p = derive_heif_profile(false, bit_depth, chroma, still);
    const HeifProfile avif_p = derive_heif_profile(true, bit_depth, chroma, still);
    if (heif_p == want || avif_p == want)
        return true;
    if (err_zh != nullptr) {
        *err_zh = "所选编码档位「" + heif_profile_label(want) + "」与当前设置不一致：位深 " +
                  std::to_string(bit_depth) + "、色度 " +
                  (chroma == 0   ? std::string("4:0:0")
                   : chroma == 1 ? std::string("4:2:0")
                   : chroma == 2 ? std::string("4:2:2")
                   : chroma == 3 ? std::string("4:4:4")
                                 : std::string("未知")) +
                  (still ? "、静止图" : "") + " 产出 HEIF「" + heif_profile_label(heif_p) +
                  "」/ AVIF「" + heif_profile_label(avif_p) + "」。请改选「自动」或调整位深/色度。";
    }
    return false;
}

} // namespace pp
