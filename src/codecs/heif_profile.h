// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — HEIF/AVIF 编码档位（profile）单源。
//
// 口径：这里的 profile = **编码信号档位**，决定解码兼容性；**不是**色彩 ICC profile。
//   * HEIF/HEVC：档位 = hvcC/SPS 里真实写出的 `general_profile_idc` + 色度 + 位深，
//     不是插件日志里的“Main/Main10”字样。10 位 4:2:0 静图在 x265 4.3 里有两种合法信号：
//       - idc=4（RExt，“Main 4:2:0 10 Intra”）：libheif 插件按 main10-intra 的**原生默认**；
//       - idc=2（Main10）：必须在下发参数后把 x265 置为非 all-intra（total-frames/keyint
//         见 heif_x265_signal_params），这正是 Apple HEIC 参考 T7.heic 的信号。
//     8 位 4:2:0 同样有三档：idc=3（Main Still Picture，插件默认）、idc=1（Main）、
//     idc=4（Main Intra / RExt）。4:2:2 / 4:4:4 / 12 位一律 idc=4。
//   * AVIF/AV1：Main / High / Professional（由色度 + 位深推导，插件无 profile 参数）。
//
// 枚举序号的兼容性（重要）：kHeifProfileParamKey 虽是 `__` 保留键（不进日志快照/未知参数
// 校验），但 `src/ui/preset_io.cpp` 用输出级 JSON 字段 `heif_profile` **持久化该整数**
// （test_presets.cpp 亦锁定）。因此**既有编号 0..6 不得重排**，新信号只能追加在末尾：
//   Auto=0 / Main=1 / Main10=2 / Main12=3 / MainStill=4 / High=5 / Professional=6，
//   新增 RExt 信号 7..15。Main12 保留为“12 位 RExt（色度随输入）”的兼容别名；UI 不再
//   展示它，新预设请使用 Rext420_12 / Rext422_12 / Rext444_12。
//
// 本头的 API：
//   1. heif_derive_signal_profile / heif_profile_compatible_for：**真实信号**的推导与反查
//      （enc_heif 与 UI 共用；Auto = 插件原生行为）。
//   2. heif_profile_signal / heif_profile_signal_for：档位 → 期望
//      (profile_idc, chroma, bit_depth)，供编码后 hvcC 回读校验（heif_capability.h 的
//      heif_parse_hvcc）；AVIF 侧用 AV1 seq_profile 表达。
//   3. heif_x265_signal_params：非原生 HEVC 静图信号所需的内部 x265 参数。
//   4. derive_heif_profile / heif_profile_compatible：0.3.6 旧口径的**兼容层**（只按位深
//      推导），仅为 tests/unit/test_enc_oiio.cpp 的既有断言保留；新代码不要使用。
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace pp {

// 编码档位枚举（顺序即 UI 下拉顺序；0..6 为 0.3.6 既有编号，见文件头兼容性说明）。
// 显式 uint8_t 基底：值域 0..15（performance-enum-size），且不影响持久化整数含义。
enum class HeifProfile : std::uint8_t {
    Auto = 0,
    Main = 1,
    Main10 = 2,
    Main12 = 3, // 兼容别名：12 位 RExt（idc=4），色度随输入
    MainStill = 4,
    High = 5,
    Professional = 6,
    // —— 追加区（真实 HEVC 静图信号；idc 均为 4，chroma/bitdepth 见名字）——
    Rext420_8 = 7,
    Rext420_10 = 8,
    Rext420_12 = 9,
    Rext422_8 = 10,
    Rext422_10 = 11,
    Rext422_12 = 12,
    Rext444_8 = 13,
    Rext444_10 = 14,
    Rext444_12 = 15,
};

inline constexpr int kHeifProfileCount = 16;

// ParamSet 承载键（**保留键**："__" 前缀 ⇒ 不参与未知参数校验、不落预设 params、不进日志
// 快照；预设的输出级 `heif_profile` 字段另行持久化）。值 = HeifProfile 的 int64_t 序号。
inline constexpr std::string_view kHeifProfileParamKey = "__heif_profile";

// 序号 → 枚举（越界返回 false，不静默当成 Auto）。
inline bool heif_profile_from_int(int64_t value, HeifProfile *out) {
    if (value < 0 || value >= kHeifProfileCount)
        return false;
    if (out != nullptr)
        *out = static_cast<HeifProfile>(value);
    return true;
}

// 档位归类。Main 同时属于两侧（HEVC Main idc=1 / AV1 Main），由调用方按格式解释。
inline bool heif_profile_is_hevc_signal(HeifProfile p) {
    switch (p) {
    case HeifProfile::Main:
    case HeifProfile::Main10:
    case HeifProfile::Main12:
    case HeifProfile::MainStill:
    case HeifProfile::Rext420_8:
    case HeifProfile::Rext420_10:
    case HeifProfile::Rext420_12:
    case HeifProfile::Rext422_8:
    case HeifProfile::Rext422_10:
    case HeifProfile::Rext422_12:
    case HeifProfile::Rext444_8:
    case HeifProfile::Rext444_10:
    case HeifProfile::Rext444_12:
        return true;
    case HeifProfile::Auto:
    case HeifProfile::High:
    case HeifProfile::Professional:
        return false;
    }
    return false;
}

inline bool heif_profile_is_avif_signal(HeifProfile p) {
    return p == HeifProfile::Main || p == HeifProfile::High || p == HeifProfile::Professional;
}

// 档位 → 期望信号。HEVC 时 profile_idc = general_profile_idc（1 Main / 2 Main10 /
// 3 Main Still / 4 RExt），chroma 编码 0=4:0:0、1=4:2:0、2=4:2:2、3=4:4:4；-1 = 随输入。
// 注意 Main 同时是 HEVC/AV1 名称：本函数按 **HEVC Main** 解释；AVIF 请用
// heif_profile_signal_for(true, …)（AV1 seq_profile：0 Main / 1 High / 2 Professional）。
// Auto 返回 false（默认信号由 heif_derive_signal_profile 给出）。
struct HeifSignalSpec {
    bool hevc = true;
    int profile_idc = -1;
    int chroma = -1;
    int bit_depth = 0; // 0 = 不限定（AVIF Main/High 覆盖 8/10 位）
};

inline bool heif_profile_signal(HeifProfile p, HeifSignalSpec *out) {
    HeifSignalSpec s;
    switch (p) {
    case HeifProfile::Auto:
        return false;
    case HeifProfile::Main:
        s = {true, 1, 1, 8};
        break;
    case HeifProfile::Main10:
        s = {true, 2, 1, 10};
        break;
    case HeifProfile::Main12: // 兼容别名：idc=4、12 位、色度随输入
        s = {true, 4, -1, 12};
        break;
    case HeifProfile::MainStill:
        s = {true, 3, 1, 8};
        break;
    case HeifProfile::High:
        s = {false, 1, 3, 0};
        break;
    case HeifProfile::Professional:
        s = {false, 2, -1, 0};
        break;
    case HeifProfile::Rext420_8:
        s = {true, 4, 1, 8};
        break;
    case HeifProfile::Rext420_10:
        s = {true, 4, 1, 10};
        break;
    case HeifProfile::Rext420_12:
        s = {true, 4, 1, 12};
        break;
    case HeifProfile::Rext422_8:
        s = {true, 4, 2, 8};
        break;
    case HeifProfile::Rext422_10:
        s = {true, 4, 2, 10};
        break;
    case HeifProfile::Rext422_12:
        s = {true, 4, 2, 12};
        break;
    case HeifProfile::Rext444_8:
        s = {true, 4, 3, 8};
        break;
    case HeifProfile::Rext444_10:
        s = {true, 4, 3, 10};
        break;
    case HeifProfile::Rext444_12:
        s = {true, 4, 3, 12};
        break;
    }
    if (out != nullptr)
        *out = s;
    return true;
}

// 格式感知的期望信号：AVIF 的 Main/High/Professional 用 AV1 seq_profile 表达，避免把
// “Main” 误当成 HEVC idc=1。其它档位委托 heif_profile_signal（HEVC 真实信号）。
inline bool heif_profile_signal_for(bool avif, HeifProfile p, HeifSignalSpec *out) {
    if (!avif) {
        if (!heif_profile_is_hevc_signal(p))
            return false;
        return heif_profile_signal(p, out);
    }
    HeifSignalSpec s{false, -1, -1, 0};
    switch (p) {
    case HeifProfile::Auto:
        return false;
    case HeifProfile::Main:
        s = {false, 0, -1, 0}; // AV1 Main：8/10 位、4:2:0/4:0:0（约束由兼容函数判定）
        break;
    case HeifProfile::High:
        s = {false, 1, 3, 0}; // AV1 High：8/10 位、4:4:4
        break;
    case HeifProfile::Professional:
        s = {false, 2, -1, 0}; // AV1 Professional：12 位或 4:2:2
        break;
    default:
        return false; // HEVC 信号档位不是合法 AVIF 档位
    }
    if (out != nullptr)
        *out = s;
    return true;
}

// 位深 → 档位族（8 / 10 / 12；越界值就近归档，与 x265/x265 插件分支一致）。
inline int heif_bit_depth_bucket(int bit_depth) {
    if (bit_depth <= 8)
        return 8;
    if (bit_depth <= 10)
        return 10;
    return 12;
}

// 真实默认信号推导（Auto 的语义 = 插件当前原生行为，不注入内部 x265 信号参数）：
//   * HEVC 4:2:0：8 位静止图 → Main Still（idc=3）；10 位 → RExt（idc=4）；
//     12 位 → RExt（idc=4）；4:2:2 / 4:4:4 一律 RExt（idc=4，位深分档）。
//     4:0:0 在本管线不编码（enc_heif 的布局只有 420/422/444），防御性地按 4:2:0 族归档。
//   * AV1：422 或 >10 位 → Professional；444 → High；其余 → Main。
inline HeifProfile heif_derive_signal_profile(bool avif, int bit_depth, int chroma, bool still) {
    const int depth = heif_bit_depth_bucket(bit_depth);
    if (avif) {
        if (chroma == 2 || bit_depth > 10)
            return HeifProfile::Professional;
        if (chroma == 3)
            return HeifProfile::High;
        return HeifProfile::Main;
    }
    if (chroma == 3) {
        return depth == 8    ? HeifProfile::Rext444_8
               : depth == 10 ? HeifProfile::Rext444_10
                             : HeifProfile::Rext444_12;
    }
    if (chroma == 2) {
        return depth == 8    ? HeifProfile::Rext422_8
               : depth == 10 ? HeifProfile::Rext422_10
                             : HeifProfile::Rext422_12;
    }
    if (depth == 8)
        return still ? HeifProfile::MainStill : HeifProfile::Main;
    return depth == 10 ? HeifProfile::Rext420_10 : HeifProfile::Rext420_12;
}

inline std::string heif_chroma_text(int chroma) {
    switch (chroma) {
    case 0:
        return "4:0:0";
    case 1:
        return "4:2:0";
    case 2:
        return "4:2:2";
    case 3:
        return "4:4:4";
    default:
        return "未知";
    }
}

// 中文标签（UI 档位行 / 日志 / 错误文案同源）。标签必须能区分 idc，避免旧版把
// idc=4 的 10 位静图显示成 “Main10” 而让用户误以为产物是 Main10（R-315 的根因）。
inline std::string heif_profile_label(HeifProfile p) {
    switch (p) {
    case HeifProfile::Auto:
        return "自动";
    case HeifProfile::Main:
        return "Main（8 位 · 4:2:0 · idc 1）";
    case HeifProfile::Main10:
        return "Main10（10 位 · 4:2:0 · idc 2）";
    case HeifProfile::Main12:
        return "Main12（12 位 · 兼容别名 · idc 4）";
    case HeifProfile::MainStill:
        return "Main Still Picture（8 位 · 静止图 · idc 3）";
    case HeifProfile::High:
        return "High（AV1 · 4:4:4）";
    case HeifProfile::Professional:
        return "Professional（AV1 · 12 位 / 4:2:2）";
    case HeifProfile::Rext420_8:
        return "Main 4:2:0 8（idc 4）";
    case HeifProfile::Rext420_10:
        return "Main 4:2:0 10 Intra（idc 4）";
    case HeifProfile::Rext420_12:
        return "Main 4:2:0 12 Intra（idc 4）";
    case HeifProfile::Rext422_8:
        return "Main 4:2:2 8（idc 4）";
    case HeifProfile::Rext422_10:
        return "Main 4:2:2 10（idc 4）";
    case HeifProfile::Rext422_12:
        return "Main 4:2:2 12（idc 4）";
    case HeifProfile::Rext444_8:
        return "Main 4:4:4 8（idc 4）";
    case HeifProfile::Rext444_10:
        return "Main 4:4:4 10（idc 4）";
    case HeifProfile::Rext444_12:
        return "Main 4:4:4 12（idc 4）";
    }
    return "自动";
}

// AVIF 专用标签：AV1 没有 general_profile_idc，不能复用 HEVC 的 idc 文案。
inline std::string heif_profile_label_avif(HeifProfile p) {
    switch (p) {
    case HeifProfile::Auto:
        return "自动";
    case HeifProfile::Main:
        return "Main（AV1 · 8/10 位 · 4:2:0/4:0:0）";
    case HeifProfile::High:
        return "High（AV1 · 8/10 位 · 4:4:4）";
    case HeifProfile::Professional:
        return "Professional（AV1 · 12 位 / 4:2:2）";
    default:
        return heif_profile_label(p); // HEVC 信号档位：保留真实 idc 文案
    }
}

inline std::string heif_profile_label_for(bool avif, HeifProfile p) {
    return avif ? heif_profile_label_avif(p) : heif_profile_label(p);
}

// 严格（格式感知）反查：want 能否由该格式的（位深, 色度, 静止图）真实产出。
// - Auto 恒 true；AVIF 只接受 Main/High/Professional；HEIF 只接受 HEVC 信号档位。
// - 不一致 → false，err_zh 给出可操作的中文原因（档位/位深/色度三项逐条点名）。
inline bool heif_profile_compatible_for(bool avif, HeifProfile want, int bit_depth, int chroma,
                                        bool still, std::string *err_zh) {
    if (want == HeifProfile::Auto)
        return true;
    HeifProfile checked = HeifProfile::Auto;
    if (!heif_profile_from_int(static_cast<int64_t>(want), &checked)) {
        if (err_zh != nullptr)
            *err_zh = "编码档位枚举值越界（合法 0..15）。";
        return false;
    }
    if (avif) {
        if (!heif_profile_is_avif_signal(want)) {
            if (err_zh != nullptr)
                *err_zh = "所选编码档位「" + heif_profile_label(want) +
                          "」是 HEIF/HEVC 的静图信号（general_profile_idc），不能用于 AVIF。"
                          "请改选「自动」/ Main / High / Professional。";
            return false;
        }
        const HeifProfile derived = heif_derive_signal_profile(true, bit_depth, chroma, still);
        if (derived == want)
            return true;
        if (err_zh != nullptr)
            *err_zh = "所选 AVIF 档位「" + heif_profile_label_avif(want) +
                      "」与当前设置不一致：位深 " + std::to_string(bit_depth) + "、色度 " +
                      heif_chroma_text(chroma) + " 产出 AVIF「" + heif_profile_label_avif(derived) +
                      "」。请改选「自动」或调整位深/色度。";
        return false;
    }
    if (!heif_profile_is_hevc_signal(want)) {
        if (err_zh != nullptr)
            *err_zh = "所选编码档位「" + heif_profile_label(want) +
                      "」是 AVIF 专用档位，不能用于 HEIF。请改选 HEIF 的真实信号档位或「自动」。";
        return false;
    }
    if (want == HeifProfile::MainStill && !still) {
        if (err_zh != nullptr)
            *err_zh = "编码档位「" + heif_profile_label(want) +
                      "」只适用于静止图输出，当前编码不是静止图。";
        return false;
    }
    HeifSignalSpec spec;
    if (!heif_profile_signal(want, &spec))
        return false;
    // Main12 是兼容别名：12 位、色度随输入（idc=4）。
    if (want != HeifProfile::Main12) {
        if (spec.bit_depth != heif_bit_depth_bucket(bit_depth)) {
            if (err_zh != nullptr)
                *err_zh = "所选编码档位「" + heif_profile_label(want) + "」要求 " +
                          std::to_string(spec.bit_depth) + " 位，当前输出位深是 " +
                          std::to_string(bit_depth) + " 位。请改选匹配的档位或调整位深。";
            return false;
        }
        if (spec.chroma >= 0 && spec.chroma != chroma) {
            if (err_zh != nullptr)
                *err_zh = "所选编码档位「" + heif_profile_label(want) + "」要求 " +
                          heif_chroma_text(spec.chroma) + "，当前色度是 " +
                          heif_chroma_text(chroma) + "。请改选匹配的档位或调整色度。";
            return false;
        }
    } else if (heif_bit_depth_bucket(bit_depth) != 12) {
        if (err_zh != nullptr)
            *err_zh = "兼容档位「Main12」要求 12 位，当前输出位深是 " + std::to_string(bit_depth) +
                      " 位。请改选匹配的档位或调整位深。";
        return false;
    }
    if (chroma < 0 || chroma > 3) {
        if (err_zh != nullptr)
            *err_zh = "当前色度值无效，无法匹配任何 HEVC 档位。";
        return false;
    }
    return true;
}

// 内部 x265 信号参数（编码前由 enc_heif 在**用户透传循环之后**下发，保证覆盖插件默认）。
// 依据 x265 4.3 `encoder/level.cpp:68-113` 的 determineLevel：
//   * 8 位 4:2:0：totalFrames==1 → idc3；否则 keyframeMax<=1 → idc4，否则 idc1；
//   * 10 位 4:2:0：keyframeMax<=1（或 totalFrames==1）→ idc4，否则 idc2。
// libheif 插件在开编码器时固定 totalFrames=1，并对 8 位静图 apply mainstillpicture
// （keyframeMax=1）；因此：
//   * Main（idc1）  = total-frames=2 + keyint=2 + repeat-headers=1；
//   * Main10（idc2）= 同上；
//   * Rext420_8（idc4）= 只需 total-frames=2（keyframeMax 仍为 1，x265 自动 repeat-headers）。
// 其余档位 = 插件原生默认，不注入任何参数。
struct HeifX265SignalParam {
    const char *key = nullptr;   // 带 "x265:" 前缀（heif_encoder_set_parameter_string 的键）
    const char *value = nullptr; // 十进制/布尔字符串
};
struct HeifX265SignalParams {
    HeifX265SignalParam items[3]{};
    int count = 0;
};

inline HeifX265SignalParams heif_x265_signal_params(HeifProfile p) {
    HeifX265SignalParams out;
    switch (p) {
    case HeifProfile::Main:
    case HeifProfile::Main10:
        out.items[0] = {"x265:total-frames", "2"};
        out.items[1] = {"x265:keyint", "2"};
        out.items[2] = {"x265:repeat-headers", "1"};
        out.count = 3;
        break;
    case HeifProfile::Rext420_8:
        out.items[0] = {"x265:total-frames", "2"};
        out.count = 1;
        break;
    default:
        break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// 兼容层（0.3.6 旧口径）：仅按位深推导，忽略真实色度信号。
// tests/unit/test_enc_oiio.cpp 锁定以下语义，且该文件不在本轮写范围；新代码一律改用
// heif_derive_signal_profile / heif_profile_compatible_for。
// ---------------------------------------------------------------------------
inline HeifProfile derive_heif_profile(bool avif, int bit_depth, int chroma, bool still) {
    if (!avif) {
        if (bit_depth <= 8)
            return still ? HeifProfile::MainStill : HeifProfile::Main;
        if (bit_depth <= 10)
            return HeifProfile::Main10;
        return HeifProfile::Main12;
    }
    if (chroma == 2 || bit_depth > 10)
        return HeifProfile::Professional;
    if (chroma == 3)
        return HeifProfile::High;
    return HeifProfile::Main;
}

// 兼容反查：任一格式（HEIF 或 AVIF）能产出即算兼容（旧行为；不做格式区分）。
// 新代码请用 heif_profile_compatible_for（严格格式面）。
inline bool heif_profile_compatible(HeifProfile want, int bit_depth, int chroma, bool still,
                                    std::string *err_zh) {
    if (want == HeifProfile::Auto)
        return true;
    const bool hevc_profiles = want == HeifProfile::Main || want == HeifProfile::Main10 ||
                               want == HeifProfile::Main12 || want == HeifProfile::MainStill;
    if (hevc_profiles && chroma == 3) {
        if (err_zh != nullptr) {
            *err_zh = "所选编码档位「" + heif_profile_label(want) +
                      "」不覆盖 4:4:4：HEVC 的 4:4:4 需要 Main 4:4:4（idc 4）档位。请改选"
                      "「自动」或把色度改为 4:2:0/4:2:2。";
        }
        return false;
    }
    const HeifProfile heif_p = derive_heif_profile(false, bit_depth, chroma, still);
    const HeifProfile avif_p = derive_heif_profile(true, bit_depth, chroma, still);
    if (heif_p == want || avif_p == want ||
        (want == HeifProfile::Main && heif_p == HeifProfile::MainStill))
        return true;
    if (err_zh != nullptr) {
        *err_zh = "所选编码档位「" + heif_profile_label(want) + "」与当前设置不一致：位深 " +
                  std::to_string(bit_depth) + "、色度 " + heif_chroma_text(chroma) +
                  (still ? "、静止图" : "") + " 产出 HEIF「" + heif_profile_label(heif_p) +
                  "」/ AVIF「" + heif_profile_label(avif_p) + "」。请改选「自动」或调整位深/色度。";
    }
    return false;
}

} // namespace pp
