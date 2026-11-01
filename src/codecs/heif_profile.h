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
//   新增 RExt 信号 7..15，W2-T2 的 still 变体 16..21。Main12 保留为“12 位 RExt（色度随
//   输入）”的兼容别名；UI 不再展示它，新预设请使用 Rext420_12 / Rext422_12 / Rext444_12。
//   旧版本读新预设里的 int 16..21 会走 heif_profile_from_int 的越界失败（显式错误，不静默）。
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
    // —— 追加区 2（W2-T2 / C18：still 变体；编号 16–21 追加，不重排）——
    // 语义（docs/v0.4.0-design.md §6.2.1，标准事实如实标注）：
    //   * RExt still 变体 = idc=4 + intra=1 + one_picture_only=1 + 对应 max_12/10/8/422/420
    //     上界位（x265 entropy.cpp:747-755 写出）→ 17..21 全部是**插件原生**（不注入参数）；
    //   * Main10Still（16）在 PTL 里**没有** still 位（非 RExt 的 44 位是保留位，恒 0；
    //     x265 level.cpp:100 注释 "note there is no 10bit still picture profile"）→ 只能靠
    //     SPS 的 DPB 约束证明单图语义，实测不满足（见 heif_profile_still_available）。
    Main10Still = 16,     // idc 2 / 420 / 10；走 Main10 注入路径；实测不可用（UI 置灰）
    Main422_10Still = 17, // idc 4 / 422 / 10
    Main422_12Still = 18, // idc 4 / 422 / 12
    Main444_Still = 19,   // idc 4 / 444 / 8（标准名无 "8" 后缀）
    Main444_10Still = 20, // idc 4 / 444 / 10（= 既有 Rext444_10 的同一真实信号）
    Main444_12Still = 21, // idc 4 / 444 / 12
};

inline constexpr int kHeifProfileCount = 22;

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
    case HeifProfile::Main10Still:
    case HeifProfile::Main422_10Still:
    case HeifProfile::Main422_12Still:
    case HeifProfile::Main444_Still:
    case HeifProfile::Main444_10Still:
    case HeifProfile::Main444_12Still:
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

// W2-T2（C18）：still 变体档位（编号 16..21）。语义 = "单图专用"：
//   * 17..21（RExt）：码流必须有 intra=1 + one_picture_only=1（SPS PTL 的 48 位 constraint）；
//   * 16（Main10Still）：PTL 无 still 位，靠 SPS 的 DPB 约束（见 heif_profile_still_available）。
// 这些档位只在 still=true（本管线恒为静止图）时可用；still=false 的兼容门控直接拒绝。
inline bool heif_profile_is_still_variant(HeifProfile p) {
    switch (p) {
    case HeifProfile::Main10Still:
    case HeifProfile::Main422_10Still:
    case HeifProfile::Main422_12Still:
    case HeifProfile::Main444_Still:
    case HeifProfile::Main444_10Still:
    case HeifProfile::Main444_12Still:
        return true;
    default:
        return false;
    }
}

// still 变体的**实测可用性**（与「信号兼容」分开：兼容 = 位深/色度/idc 能对上，可用性 =
// 本编码后端当前能不能真的写出 still 语义）。纯函数，UI 置灰与报告同源。
//
// Main10Still（16）不可用，理由全部是可复核事实（docs/v0.4.0-design.md §6.2.2 + W2-T2 实测）：
//   * x265 4.3 的 10 位 4:2:0 **没有 still profile**（level.cpp:100 注释原文
//     "note there is no 10bit still picture profile"），idc=2 的 44 位是保留位，不承载
//     intra/one_picture；
//   * 要拿到 idc=2 必须让 x265 走非 intra 分支（注入 total-frames=2/keyint=2），于是 SPS 的
//     DPB 变成 max_dec_pic_buffering_minus1=4 / num_reorder_pics=2（W2-T2 实测，要求 0/0），
//     单图约束无法成立 → 只能置灰，不能伪造。
inline bool heif_profile_still_available(HeifProfile p, std::string *reason_zh) {
    if (p == HeifProfile::Main10Still) {
        if (reason_zh != nullptr)
            *reason_zh = "「Main 10 Still Picture」（idc 2 / 10 位 / 4:2:0）不可用：HEVC 的 "
                         "Main10 PTL 没有 still 约束位（非 RExt 的 44 位是保留位，x265 4.3 "
                         "level.cpp:100 亦注明 10 位没有 still profile），而写出 idc=2 必须走非 "
                         "intra 参数，SPS 实测 DPB 为 max_dec_pic_buffering_minus1=4 / "
                         "num_reorder_pics=2（要求 0/0），单图语义无法成立。请改选 Main10"
                         "（同 idc 2，普通单图信号）或 Main 4:4:4 10 Still Picture 等 RExt "
                         "still 档位。";
        return false;
    }
    return true;
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
    // —— W2-T2 still 变体（C18）：idc/chroma/bitdepth 与对应 RExt 档位同信号；still 语义在
    //    编码后由 SPS PTL/DPB 断言（见 heif_capability.h 的 heif_parse_sps_ptl）。——
    case HeifProfile::Main10Still:
        s = {true, 2, 1, 10};
        break;
    case HeifProfile::Main422_10Still:
        s = {true, 4, 2, 10};
        break;
    case HeifProfile::Main422_12Still:
        s = {true, 4, 2, 12};
        break;
    case HeifProfile::Main444_Still:
        s = {true, 4, 3, 8};
        break;
    case HeifProfile::Main444_10Still:
        s = {true, 4, 3, 10};
        break;
    case HeifProfile::Main444_12Still:
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
        return "Main Intra（4:2:0 8；idc 4；非 still）";
    case HeifProfile::Rext420_10:
        return "Main 4:2:0 10 Intra（idc 4）";
    case HeifProfile::Rext420_12:
        return "Main 4:2:0 12 Intra（idc 4）";
    case HeifProfile::Rext422_8:
        // 标准没有 "4:2:2 8" profile：8 位是 Main 4:2:2 10 的子集（idc 4 + max_8bit=1）。
        return "Main 4:2:2 10（8 位信号；idc 4）";
    case HeifProfile::Rext422_10:
        return "Main 4:2:2 10（idc 4）";
    case HeifProfile::Rext422_12:
        return "Main 4:2:2 12（idc 4）";
    case HeifProfile::Rext444_8:
        // 标准名是 Main 4:4:4（无 "8" 后缀）；8 位是该 profile 的子集。
        return "Main 4:4:4（8 位；idc 4）";
    case HeifProfile::Rext444_10:
        return "Main 4:4:4 10（idc 4）";
    case HeifProfile::Rext444_12:
        return "Main 4:4:4 12（idc 4）";
    // —— W2-T2 still 变体（C18 / §6.2.2）：短标签 + idc；完整信号说明见
    //    heif_profile_tooltip（UI 下拉 item 的 tooltip 全文，C19）——
    case HeifProfile::Main10Still:
        return "Main 10 Still Picture（idc 2 · 10 位 · 4:2:0）";
    case HeifProfile::Main422_10Still:
        return "Main 4:2:2 10 Still Picture（idc 4）";
    case HeifProfile::Main422_12Still:
        return "Main 4:2:2 12 Still Picture（idc 4）";
    case HeifProfile::Main444_Still:
        return "Main 4:4:4 Still Picture（idc 4）";
    case HeifProfile::Main444_10Still:
        return "Main 4:4:4 10 Still Picture（idc 4）";
    case HeifProfile::Main444_12Still:
        return "Main 4:4:4 12 Still Picture（idc 4）";
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

// W2-T2（C18/C19）：档位下拉的**完整信号说明**（UI item tooltip 全文；短标签见
// heif_profile_label_for）。口径 = docs/v0.4.0-design.md §6.2.1 / §6.2.4：
//   * 标准事实（idc / 色度 / 位深 / still 语义 / 编码路径）如实写，不承诺未验证的东西；
//   * RExt 档位一律带 Apple 风险句（RExt 不在 Apple 文档集 → iPhone 接受度未验证）；
//   * 规范 PDF 复核是本环境的开放项 R-2，still 档的官方名用「（待复核）」标注。
inline std::string heif_profile_tooltip(bool avif, HeifProfile p) {
    if (avif) {
        switch (p) {
        case HeifProfile::Main:
            return "AV1 Main：8/10 位 · 4:2:0/4:0:0（AV1 seq_profile 0）；由插件按色度/位深自选。";
        case HeifProfile::High:
            return "AV1 High：8/10 位 · 4:4:4（seq_profile 1）；由插件按色度自选。";
        case HeifProfile::Professional:
            return "AV1 Professional：12 位或 4:2:2（seq_profile 2）；兼容性最差。";
        case HeifProfile::Auto:
            return "自动：插件按位深/色度自选 AV1 档位（4:2:2 或 >10 位→Professional、"
                   "4:4:4→High、其余→Main）。";
        default:
            return "该档位是 HEIF/HEVC 的静图信号，不能用于 AVIF。";
        }
    }
    const std::string apple_rext =
        "Apple：RExt 档不在 Apple 文档集内，iPhone 接受度未验证（不做承诺）。";
    switch (p) {
    case HeifProfile::Auto:
        return "自动：插件按位深/色度自选真实信号（HEVC 8 位 4:2:0 静图→Main Still Picture "
               "idc 3；10/12 位 4:2:0→RExt idc 4；4:2:2/4:4:4→RExt idc 4）；编码后用 hvcC "
               "+ SPS 回读校验。";
    case HeifProfile::Main:
        return "Main：idc 1 · 4:2:0 · 8 位；插件原生会写 Main Still Picture（idc 3），本档由 "
               "enc_heif 注入 x265 total-frames=2 / keyint=2 / repeat-headers=1 得到。"
               "Apple 文档化。";
    case HeifProfile::Main10:
        return "Main10：idc 2 · 4:2:0 · 10 位；由注入 total-frames=2 / keyint=2 / "
               "repeat-headers=1 得到（插件原生 10 位静图是 RExt idc 4）。Apple HEIC 参考信号。";
    case HeifProfile::Main12:
        return "Main12：12 位 RExt（idc 4）的兼容别名，色度随输入；新预设请改用显式 "
               "Rext420_12 / Rext422_12 / Rext444_12。";
    case HeifProfile::MainStill:
        return "Main Still Picture：idc 3 · 4:2:0 · 8 位；插件原生默认（mainstillpicture），"
               "Apple 文档化，兼容性最好。";
    case HeifProfile::Rext420_8:
        return "Main Intra：idc 4 · 4:2:0 · 8 位；注入 total-frames=2 关掉 "
               "one_picture_only_minus1（否则 x265 写 idc 3），因此信号是「Main Intra」，"
               "标准没有 Main 4:2:0 8、也没有 4:2:0 8 的 still profile。" +
               apple_rext;
    case HeifProfile::Rext420_10:
        return "Main 4:2:0 10 Intra：idc 4 · 4:2:0 · 10 位；x265 静图原生默认（10 位 4:2:0 "
               "没有 still profile）。部分工具把它显示成 Main10，但码流 idc 是 4。" +
               apple_rext;
    case HeifProfile::Rext420_12:
        return "Main 4:2:0 12 Intra：idc 4 · 4:2:0 · 12 位；需较新解码器。" + apple_rext;
    case HeifProfile::Rext422_8:
        return "Main 4:2:2 10（8 位信号）：idc 4 · 4:2:2 · 8 位；标准没有 4:2:2 8 档，8 位是 "
               "Main 4:2:2 10 的子集（max_8bit=1）。" +
               apple_rext;
    case HeifProfile::Rext422_10:
        return "Main 4:2:2 10：idc 4 · 4:2:2 · 10 位；插件原生。" + apple_rext;
    case HeifProfile::Rext422_12:
        return "Main 4:2:2 12：idc 4 · 4:2:2 · 12 位；插件原生。" + apple_rext;
    case HeifProfile::Rext444_8:
        return "Main 4:4:4（8 位）：idc 4 · 4:4:4 · 8 位；标准名是 Main 4:4:4（无 “8” 后缀），"
               "8 位是该 profile 的子集。" +
               apple_rext;
    case HeifProfile::Rext444_10:
        return "Main 4:4:4 10：idc 4 · 4:4:4 · 10 位；插件原生。" + apple_rext;
    case HeifProfile::Rext444_12:
        return "Main 4:4:4 12：idc 4 · 4:4:4 · 12 位；插件原生。" + apple_rext;
    // —— W2-T2 still 变体 ——
    case HeifProfile::Main10Still: {
        std::string why;
        (void)heif_profile_still_available(p, &why);
        return "Main 10 Still Picture（官方名待规范复核 R-2）：idc 2 · 4:2:0 · 10 位。"
               "HEVC 的 Main10 PTL 没有 still 约束位（非 RExt 的 44 位是保留位），只能靠 SPS "
               "的 DPB 约束证明单图语义 —— 实测不满足，本档不可用（UI 置灰）。" +
               why;
    }
    case HeifProfile::Main422_10Still:
        return "Main 4:2:2 10 Still Picture（官方名待规范复核 R-2）：idc 4 · 4:2:2 · 10 位；"
               "码流约束 = intra=1 + one_picture_only=1 + max_10bit/max_422chroma 上界位。"
               "插件原生（不注入参数）。" +
               apple_rext;
    case HeifProfile::Main422_12Still:
        return "Main 4:2:2 12 Still Picture（官方名待规范复核 R-2）：idc 4 · 4:2:2 · 12 位；"
               "码流约束 = intra=1 + one_picture_only=1；插件原生。" +
               apple_rext;
    case HeifProfile::Main444_Still:
        return "Main 4:4:4 Still Picture（官方名待规范复核 R-2）：idc 4 · 4:4:4 · 8 位；"
               "码流约束 = intra=1 + one_picture_only=1 + max_8bit 上界位；插件原生。" +
               apple_rext;
    case HeifProfile::Main444_10Still:
        return "Main 4:4:4 10 Still Picture（官方名待规范复核 R-2）：idc 4 · 4:4:4 · 10 位；"
               "码流约束 = intra=1 + one_picture_only=1；插件原生 —— 与既有 Rext444_10 是**同一"
               "真实信号**（x265 日志把 4:4:4 10 位 still 叫 “Main 4:4:4 10 Intra”，是日志命名"
               "遗留）。" +
               apple_rext;
    case HeifProfile::Main444_12Still:
        return "Main 4:4:4 12 Still Picture（官方名待规范复核 R-2）：idc 4 · 4:4:4 · 12 位；"
               "码流约束 = intra=1 + one_picture_only=1；插件原生。" +
               apple_rext;
    case HeifProfile::High:
    case HeifProfile::Professional:
        return "AV1 专用档位，不能用于 HEIF。";
    }
    return std::string();
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
            *err_zh =
                "编码档位枚举值越界（合法 0.." + std::to_string(kHeifProfileCount - 1) + "）。";
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
    // W2-T2（C18）：still 变体（16..21）与 Main Still Picture 一样只适用于静止图输出。
    // 本管线恒为静止图（still=true），这条门控是给 AVIF/序列帧/未来路径的显式拒绝面。
    if (heif_profile_is_still_variant(want) && !still) {
        if (err_zh != nullptr)
            *err_zh = "编码档位「" + heif_profile_label(want) +
                      "」是 still（单图）变体，只适用于静止图输出，当前编码不是静止图。";
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
//   * Main10Still（idc2，W2-T2）= 同 Main10（它只能靠这条路径拿 idc2；still 语义由编码后
//     SPS DPB 断言判定，实测不满足 → 不可用）；
//   * Rext420_8（idc4）= 只需 total-frames=2（keyframeMax 仍为 1，x265 自动 repeat-headers）。
// 其余档位（含 W2-T2 的 17..21 RExt still 变体）= 插件原生默认，不注入任何参数。
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
    // W2-T2（C18/§6.2.2）：16 = Main 10 Still Picture 与 Main10 走**同一条注入路径**
    // （x265 要写 idc=2 就必须走非 intra 分支；这也正是它拿不到 still 语义的原因，见
    // heif_profile_still_available）。17..21 是 RExt 原生 still 信号 → 不注入（default 分支）。
    case HeifProfile::Main:
    case HeifProfile::Main10:
    case HeifProfile::Main10Still:
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
