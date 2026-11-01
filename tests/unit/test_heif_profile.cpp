// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: HEIF/AVIF 编码档位（M8 真实 HEVC 静图信号收口）
//
// 断言口径：
//   * 既有编号 0..6 不重排（预设 JSON 持久化 heif_profile 整数）；
//   * 每个档位 → (profile_idc, chroma, bit_depth) 的真实信号；
//   * 每个 (格式, 档位, 位深, 色度) 合法/非法组合；
//   * 非原生信号只下发必要的内部 x265 参数；
//   * 旧兼容层（test_enc_oiio.cpp 锁定）语义不变。
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

#include "codecs/heif_profile.h"

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool ok, const std::string &case_name, const std::string &detail = std::string()) {
    ++g_checks;
    if (ok)
        return;
    ++g_failed;
    std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    std::fflush(stdout);
}

using pp::HeifProfile;

std::string pc(pp::HeifProfile p) { return pp::heif_profile_label(p); }

void test_enum_numbering() {
    // 0..6 是 0.3.6 已持久化编号，不得重排；新信号只能追加。
    check(static_cast<int>(HeifProfile::Auto) == 0, "enum/auto");
    check(static_cast<int>(HeifProfile::Main) == 1, "enum/main");
    check(static_cast<int>(HeifProfile::Main10) == 2, "enum/main10");
    check(static_cast<int>(HeifProfile::Main12) == 3, "enum/main12");
    check(static_cast<int>(HeifProfile::MainStill) == 4, "enum/mainstill");
    check(static_cast<int>(HeifProfile::High) == 5, "enum/high");
    check(static_cast<int>(HeifProfile::Professional) == 6, "enum/professional");
    check(static_cast<int>(HeifProfile::Rext420_8) == 7, "enum/rext420_8");
    check(static_cast<int>(HeifProfile::Rext444_12) == 15, "enum/rext444_12");
    // W2-T2（C18）：still 变体 16..21 追加在末尾，编号不重排。
    check(static_cast<int>(HeifProfile::Main10Still) == 16, "enum/main10still");
    check(static_cast<int>(HeifProfile::Main422_10Still) == 17, "enum/main422_10still");
    check(static_cast<int>(HeifProfile::Main422_12Still) == 18, "enum/main422_12still");
    check(static_cast<int>(HeifProfile::Main444_Still) == 19, "enum/main444_still");
    check(static_cast<int>(HeifProfile::Main444_10Still) == 20, "enum/main444_10still");
    check(static_cast<int>(HeifProfile::Main444_12Still) == 21, "enum/main444_12still");
    check(pp::kHeifProfileCount == 22, "enum/count");
    for (int64_t i = 0; i < pp::kHeifProfileCount; ++i) {
        HeifProfile p = HeifProfile::Auto;
        check(pp::heif_profile_from_int(i, &p), "from_int/valid/" + std::to_string(i));
        check(static_cast<int>(p) == i, "from_int/roundtrip/" + std::to_string(i));
    }
    HeifProfile tmp = HeifProfile::Auto;
    check(!pp::heif_profile_from_int(-1, &tmp), "from_int/negative");
    check(!pp::heif_profile_from_int(22, &tmp), "from_int/too_large");
    // 旧版本（0.3.6）读新预设里的 16..21 必须显式失败，不静默当 Auto。
    for (int64_t i = 16; i <= 21; ++i) {
        HeifProfile p = HeifProfile::Auto;
        check(pp::heif_profile_from_int(i, &p) && static_cast<int>(p) == i,
              "enum/still_range/" + std::to_string(i));
    }
}

void test_signal_specs() {
    struct Case {
        HeifProfile p;
        bool hevc;
        int idc;
        int chroma;
        int depth;
    };
    const Case cases[] = {
        {HeifProfile::Main, true, 1, 1, 8},
        {HeifProfile::Main10, true, 2, 1, 10},
        {HeifProfile::MainStill, true, 3, 1, 8},
        {HeifProfile::Main12, true, 4, -1, 12},
        {HeifProfile::Rext420_8, true, 4, 1, 8},
        {HeifProfile::Rext420_10, true, 4, 1, 10},
        {HeifProfile::Rext420_12, true, 4, 1, 12},
        {HeifProfile::Rext422_8, true, 4, 2, 8},
        {HeifProfile::Rext422_10, true, 4, 2, 10},
        {HeifProfile::Rext422_12, true, 4, 2, 12},
        {HeifProfile::Rext444_8, true, 4, 3, 8},
        {HeifProfile::Rext444_10, true, 4, 3, 10},
        {HeifProfile::Rext444_12, true, 4, 3, 12},
        {HeifProfile::High, false, 1, 3, 0},
        {HeifProfile::Professional, false, 2, -1, 0},
        // W2-T2（C18）：still 变体 16..21 —— 16 与 Main10 同信号，17..21 与对应 RExt 同信号。
        {HeifProfile::Main10Still, true, 2, 1, 10},
        {HeifProfile::Main422_10Still, true, 4, 2, 10},
        {HeifProfile::Main422_12Still, true, 4, 2, 12},
        {HeifProfile::Main444_Still, true, 4, 3, 8},
        {HeifProfile::Main444_10Still, true, 4, 3, 10},
        {HeifProfile::Main444_12Still, true, 4, 3, 12},
    };
    for (const Case &c : cases) {
        pp::HeifSignalSpec s;
        const bool ok = pp::heif_profile_signal(c.p, &s);
        check(ok, "signal/present/" + pc(c.p));
        if (!ok)
            continue;
        check(s.hevc == c.hevc, "signal/hevc/" + pc(c.p));
        check(s.profile_idc == c.idc, "signal/idc/" + pc(c.p),
              "want " + std::to_string(c.idc) + " got " + std::to_string(s.profile_idc));
        check(s.chroma == c.chroma, "signal/chroma/" + pc(c.p));
        check(s.bit_depth == c.depth, "signal/depth/" + pc(c.p));
    }
    pp::HeifSignalSpec none;
    check(!pp::heif_profile_signal(HeifProfile::Auto, &none), "signal/auto");
}

void test_signal_for_avif() {
    pp::HeifSignalSpec s;
    check(pp::heif_profile_signal_for(true, HeifProfile::Main, &s) && !s.hevc && s.profile_idc == 0,
          "signal_for/avif/main");
    check(pp::heif_profile_signal_for(true, HeifProfile::High, &s) && !s.hevc &&
              s.profile_idc == 1 && s.chroma == 3,
          "signal_for/avif/high");
    check(pp::heif_profile_signal_for(true, HeifProfile::Professional, &s) && !s.hevc &&
              s.profile_idc == 2,
          "signal_for/avif/professional");
    check(!pp::heif_profile_signal_for(true, HeifProfile::MainStill, &s),
          "signal_for/avif/hevc_option");
    check(!pp::heif_profile_signal_for(true, HeifProfile::Rext420_10, &s),
          "signal_for/avif/rext_option");
    check(pp::heif_profile_signal_for(false, HeifProfile::Main, &s) && s.hevc && s.profile_idc == 1,
          "signal_for/heif/main");
    check(!pp::heif_profile_signal_for(false, HeifProfile::High, &s),
          "signal_for/heif/avif_option");
    check(!pp::heif_profile_signal_for(false, HeifProfile::Auto, &s), "signal_for/heif/auto");
}

void test_derive_default() {
    using pp::heif_derive_signal_profile;
    check(heif_derive_signal_profile(false, 8, 1, true) == HeifProfile::MainStill,
          "derive/hevc/8s");
    check(heif_derive_signal_profile(false, 8, 1, false) == HeifProfile::Main, "derive/hevc/8seq");
    check(heif_derive_signal_profile(false, 10, 1, true) == HeifProfile::Rext420_10,
          "derive/hevc/10", "10 位 4:2:0 的原生静图信号是 idc=4");
    check(heif_derive_signal_profile(false, 12, 1, true) == HeifProfile::Rext420_12,
          "derive/hevc/12");
    check(heif_derive_signal_profile(false, 9, 1, true) == HeifProfile::Rext420_10,
          "derive/hevc/9");
    check(heif_derive_signal_profile(false, 13, 1, true) == HeifProfile::Rext420_12,
          "derive/hevc/13");
    check(heif_derive_signal_profile(false, 8, 2, true) == HeifProfile::Rext422_8,
          "derive/hevc/422-8");
    check(heif_derive_signal_profile(false, 10, 3, true) == HeifProfile::Rext444_10,
          "derive/hevc/444-10");
    check(heif_derive_signal_profile(true, 8, 1, true) == HeifProfile::Main, "derive/avif/main");
    check(heif_derive_signal_profile(true, 8, 3, true) == HeifProfile::High, "derive/avif/high");
    check(heif_derive_signal_profile(true, 10, 3, true) == HeifProfile::High, "derive/avif/high10");
    check(heif_derive_signal_profile(true, 8, 2, true) == HeifProfile::Professional,
          "derive/avif/422");
    check(heif_derive_signal_profile(true, 12, 1, true) == HeifProfile::Professional,
          "derive/avif/12");
}

void test_compat_matrix() {
    struct Case {
        bool avif;
        HeifProfile p;
        int depth;
        int chroma;
        bool still;
        bool legal;
    };
    const Case cases[] = {
        // Auto 恒合法。
        {false, HeifProfile::Auto, 8, 1, true, true},
        {false, HeifProfile::Auto, 12, 3, true, true},
        {true, HeifProfile::Auto, 8, 1, true, true},
        {true, HeifProfile::Auto, 12, 2, true, true},
        // HEVC 真实信号：位深/色度逐档精确。
        {false, HeifProfile::Main, 8, 1, true, true},
        {false, HeifProfile::Main, 8, 1, false, true},
        {false, HeifProfile::Main, 10, 1, true, false},
        {false, HeifProfile::Main, 8, 2, true, false},
        {false, HeifProfile::Main, 8, 3, true, false},
        {false, HeifProfile::MainStill, 8, 1, true, true},
        {false, HeifProfile::MainStill, 8, 1, false, false},
        {false, HeifProfile::MainStill, 10, 1, true, false},
        {false, HeifProfile::Main10, 10, 1, true, true},
        {false, HeifProfile::Main10, 8, 1, true, false},
        {false, HeifProfile::Main10, 10, 2, true, false},
        {false, HeifProfile::Main10, 12, 1, true, false},
        {false, HeifProfile::Main12, 12, 1, true, true},
        {false, HeifProfile::Main12, 12, 2, true, true},
        {false, HeifProfile::Main12, 12, 3, true, true},
        {false, HeifProfile::Main12, 8, 1, true, false},
        {false, HeifProfile::Main12, 10, 1, true, false},
        {false, HeifProfile::Rext420_8, 8, 1, true, true},
        {false, HeifProfile::Rext420_8, 8, 1, false, true},
        {false, HeifProfile::Rext420_8, 10, 1, true, false},
        {false, HeifProfile::Rext420_10, 10, 1, true, true},
        {false, HeifProfile::Rext420_10, 8, 1, true, false},
        {false, HeifProfile::Rext420_12, 12, 1, true, true},
        {false, HeifProfile::Rext422_8, 8, 2, true, true},
        {false, HeifProfile::Rext422_8, 8, 1, true, false},
        {false, HeifProfile::Rext422_10, 10, 2, true, true},
        {false, HeifProfile::Rext422_12, 12, 2, true, true},
        {false, HeifProfile::Rext444_8, 8, 3, true, true},
        {false, HeifProfile::Rext444_10, 10, 3, true, true},
        {false, HeifProfile::Rext444_12, 12, 3, true, true},
        {false, HeifProfile::Rext444_12, 12, 2, true, false},
        // AVIF 专用档位不能用于 HEIF；HEVC 信号不能用于 AVIF。
        {false, HeifProfile::High, 8, 3, true, false},
        {false, HeifProfile::Professional, 12, 2, true, false},
        {true, HeifProfile::MainStill, 8, 1, true, false},
        {true, HeifProfile::Main10, 10, 1, true, false},
        {true, HeifProfile::Rext420_10, 10, 1, true, false},
        {true, HeifProfile::Main, 8, 1, true, true},
        {true, HeifProfile::Main, 10, 1, true, true},
        {true, HeifProfile::Main, 10, 0, true, true},
        {true, HeifProfile::Main, 12, 1, true, false},
        {true, HeifProfile::Main, 8, 3, true, false},
        {true, HeifProfile::High, 8, 3, true, true},
        {true, HeifProfile::High, 10, 3, true, true},
        {true, HeifProfile::High, 10, 1, true, false},
        {true, HeifProfile::Professional, 8, 2, true, true},
        {true, HeifProfile::Professional, 10, 2, true, true},
        {true, HeifProfile::Professional, 12, 1, true, true},
        {true, HeifProfile::Professional, 12, 3, true, true},
        {true, HeifProfile::Professional, 8, 1, true, false},
        // —— W2-T2（C18）：still 变体 16..21 ——
        // signal 与对应 Main10/RExt 档同（idc/chroma/bd 由 heif_profile_signal 校验）；
        // still=false（序列帧）→ 一律拒绝（still 变体只用于静止图输出）。
        {false, HeifProfile::Main10Still, 10, 1, true, true},
        {false, HeifProfile::Main10Still, 10, 1, false, false},
        {false, HeifProfile::Main10Still, 8, 1, true, false},
        {false, HeifProfile::Main10Still, 10, 3, true, false},
        {false, HeifProfile::Main422_10Still, 10, 2, true, true},
        {false, HeifProfile::Main422_10Still, 10, 1, true, false},
        {false, HeifProfile::Main422_12Still, 12, 2, true, true},
        {false, HeifProfile::Main422_12Still, 12, 2, false, false},
        {false, HeifProfile::Main444_Still, 8, 3, true, true},
        {false, HeifProfile::Main444_Still, 10, 3, true, false},
        {false, HeifProfile::Main444_10Still, 10, 3, true, true},
        {false, HeifProfile::Main444_10Still, 12, 3, true, false},
        {false, HeifProfile::Main444_12Still, 12, 3, true, true},
        // AVIF 不能选 HEVC 的 still 变体。
        {true, HeifProfile::Main10Still, 10, 1, true, false},
        {true, HeifProfile::Main444_10Still, 10, 3, true, false},
    };
    for (const Case &c : cases) {
        std::string err;
        const bool got =
            pp::heif_profile_compatible_for(c.avif, c.p, c.depth, c.chroma, c.still, &err);
        const std::string tag = std::string(c.avif ? "avif/" : "heif/") + pc(c.p) + "/" +
                                std::to_string(c.depth) + "/" + std::to_string(c.chroma) + "/" +
                                (c.still ? "still" : "seq");
        check(got == c.legal, "compat/" + tag,
              "want " + std::string(c.legal ? "legal" : "illegal") + ", err=" + err);
        if (!c.legal) {
            check(!err.empty(), "compat/error_text/" + tag);
            const bool mentions =
                err.find("档位") != std::string::npos || err.find("位深") != std::string::npos ||
                err.find("色度") != std::string::npos || err.find("HEIF") != std::string::npos ||
                err.find("AVIF") != std::string::npos || err.find("4:4:4") != std::string::npos;
            check(mentions, "compat/error_actionable/" + tag, "err=" + err);
        }
        check(err.empty() == c.legal, "compat/error_only_failure/" + tag, "err=" + err);
    }
}

void test_labels() {
    std::set<std::string> labels;
    for (int i = 0; i < pp::kHeifProfileCount; ++i) {
        HeifProfile p = HeifProfile::Auto;
        check(pp::heif_profile_from_int(i, &p), "label/from_int/" + std::to_string(i));
        const std::string text = pc(p);
        check(!text.empty(), "label/nonempty/" + std::to_string(i));
        check(labels.insert(text).second, "label/unique/" + std::to_string(i), text);
    }
    check(pc(HeifProfile::Auto).find("自动") != std::string::npos, "label/auto");
    check(pc(HeifProfile::MainStill).find("静止图") != std::string::npos, "label/mainstill");
    check(pc(HeifProfile::MainStill).find("idc 3") != std::string::npos, "label/mainstill-idc");
    check(pc(HeifProfile::Main).find("idc 1") != std::string::npos, "label/main-idc");
    check(pc(HeifProfile::Main10).find("idc 2") != std::string::npos, "label/main10-idc");
    check(pc(HeifProfile::Rext420_8).find("idc 4") != std::string::npos, "label/rext-idc");
    check(pc(HeifProfile::Rext422_10).find("4:2:2") != std::string::npos, "label/422");
    check(pc(HeifProfile::Rext444_12).find("4:4:4") != std::string::npos, "label/444");
    check(pc(HeifProfile::High).find("AV1") != std::string::npos, "label/high");
    check(pc(HeifProfile::Professional).find("AV1") != std::string::npos, "label/pro");
    // AVIF 标签不能复用 HEVC 的 general_profile_idc 文案。
    check(pp::heif_profile_label_for(true, HeifProfile::Main).find("AV1") != std::string::npos,
          "label/avif/main_av1");
    check(pp::heif_profile_label_for(true, HeifProfile::Main).find("idc") == std::string::npos,
          "label/avif/main_no_idc");
    check(pp::heif_profile_label_for(false, HeifProfile::Main).find("idc 1") != std::string::npos,
          "label/heif/main_idc");
}

void test_x265_signal_params() {
    {
        const pp::HeifX265SignalParams p = pp::heif_x265_signal_params(HeifProfile::Main);
        check(p.count == 3, "params/main/count");
        check(std::string(p.items[0].key) == "x265:total-frames" &&
                  std::string(p.items[0].value) == "2",
              "params/main/total-frames");
        check(std::string(p.items[1].key) == "x265:keyint" && std::string(p.items[1].value) == "2",
              "params/main/keyint");
        check(std::string(p.items[2].key) == "x265:repeat-headers" &&
                  std::string(p.items[2].value) == "1",
              "params/main/repeat-headers");
    }
    {
        const pp::HeifX265SignalParams p = pp::heif_x265_signal_params(HeifProfile::Main10);
        check(p.count == 3, "params/main10/count");
        check(std::string(p.items[0].key) == "x265:total-frames" &&
                  std::string(p.items[0].value) == "2",
              "params/main10/total-frames");
        check(std::string(p.items[1].key) == "x265:keyint" && std::string(p.items[1].value) == "2",
              "params/main10/keyint");
        check(std::string(p.items[2].key) == "x265:repeat-headers" &&
                  std::string(p.items[2].value) == "1",
              "params/main10/repeat-headers");
    }
    {
        // Rext420_8：libheif 已把 keyframeMax 置 1（mainstillpicture），只需关掉
        // onePictureOnly（total-frames=2）即可让 determineLevel 走 idc=4。
        const pp::HeifX265SignalParams p = pp::heif_x265_signal_params(HeifProfile::Rext420_8);
        check(p.count == 1, "params/rext420_8/count");
        check(std::string(p.items[0].key) == "x265:total-frames" &&
                  std::string(p.items[0].value) == "2",
              "params/rext420_8/total-frames");
    }
    const HeifProfile native[] = {HeifProfile::Auto,
                                  HeifProfile::MainStill,
                                  HeifProfile::Rext420_10,
                                  HeifProfile::Rext420_12,
                                  HeifProfile::Rext422_8,
                                  HeifProfile::Rext444_12,
                                  HeifProfile::Main422_10Still,
                                  HeifProfile::Main422_12Still,
                                  HeifProfile::Main444_Still,
                                  HeifProfile::Main444_10Still,
                                  HeifProfile::Main444_12Still};
    for (const HeifProfile p : native)
        check(pp::heif_x265_signal_params(p).count == 0, "params/native/" + pc(p));
}

void test_still_variants() {
    // —— 分类：16..21 是 still 变体；0..15 不是 ——
    const HeifProfile stills[] = {
        HeifProfile::Main10Still,   HeifProfile::Main422_10Still, HeifProfile::Main422_12Still,
        HeifProfile::Main444_Still, HeifProfile::Main444_10Still, HeifProfile::Main444_12Still,
    };
    for (int i = 0; i < pp::kHeifProfileCount; ++i) {
        HeifProfile p = HeifProfile::Auto;
        check(pp::heif_profile_from_int(i, &p), "still/from_int/" + std::to_string(i));
        bool want = false;
        for (const HeifProfile s : stills)
            want = want || (p == s);
        check(pp::heif_profile_is_still_variant(p) == want, "still/classify/" + std::to_string(i));
    }
    // —— 可用性：Main10Still（16）实测不可用（PTL 无 still 位 + SPS DPB 4/2）——
    {
        std::string reason;
        check(!pp::heif_profile_still_available(HeifProfile::Main10Still, &reason),
              "still/available/16");
        check(reason.find("DPB") != std::string::npos, "still/available/16_dpb", reason);
        check(reason.find("max_dec_pic_buffering_minus1=4") != std::string::npos,
              "still/available/16_dpb_value", reason);
        check(reason.find("num_reorder_pics=2") != std::string::npos,
              "still/available/16_reorder_value", reason);
        check(reason.find("不可用") != std::string::npos, "still/available/16_unavailable", reason);
        // 理由必须点名替代出路（不静默灰）。
        check(reason.find("Main10") != std::string::npos, "still/available/16_alternative", reason);
    }
    for (const HeifProfile p : stills) {
        std::string reason;
        const bool ok = pp::heif_profile_still_available(p, &reason);
        if (p == HeifProfile::Main10Still) {
            check(!ok, "still/available/only_16_unavailable");
            continue;
        }
        check(ok, "still/available/" + pc(p));
        check(reason.empty(), "still/available/no_reason/" + pc(p), reason);
    }
    // 非 still 档位恒可用（本函数只判 still 变体）。
    check(pp::heif_profile_still_available(HeifProfile::Main10, nullptr), "still/available/main10");
    check(pp::heif_profile_still_available(HeifProfile::Rext444_10, nullptr),
          "still/available/rext444_10");

    // —— 标签：still 变体统一带 "Still Picture"；3 个既有误导标签已修正 ——
    for (const HeifProfile p : stills)
        check(pc(p).find("Still Picture") != std::string::npos, "label/still_picture/" + pc(p));
    check(pc(HeifProfile::Main444_10Still).find("idc 4") != std::string::npos, "label/still/idc4");
    check(pc(HeifProfile::Main10Still).find("idc 2") != std::string::npos, "label/still/idc2");
    // Rext420_8：标准没有 "Main 4:2:0 8" profile（旧标签会让人以为有）→ 改叫 Main Intra + 非
    // still。
    check(pc(HeifProfile::Rext420_8).find("Main Intra") != std::string::npos,
          "label/rext420_8/main_intra", pc(HeifProfile::Rext420_8));
    check(pc(HeifProfile::Rext420_8).find("非 still") != std::string::npos,
          "label/rext420_8/not_still", pc(HeifProfile::Rext420_8));
    // Rext422_8：标准无 422-8 profile，8 位是 Main 4:2:2 10 的子集。
    check(pc(HeifProfile::Rext422_8).find("4:2:2 10") != std::string::npos,
          "label/rext422_8/is_422_10", pc(HeifProfile::Rext422_8));
    check(pc(HeifProfile::Rext422_8).find("8 位信号") != std::string::npos,
          "label/rext422_8/8bit_subset", pc(HeifProfile::Rext422_8));
    // Rext444_8：标准名是 Main 4:4:4（无 "8" 后缀）。
    check(pc(HeifProfile::Rext444_8).find("Main 4:4:4（8 位") != std::string::npos,
          "label/rext444_8/no_suffix", pc(HeifProfile::Rext444_8));

    // —— tooltip（C19：短标签 + 完整信号说明；RExt 一律带 Apple 风险）——
    for (int i = 1; i < pp::kHeifProfileCount; ++i) {
        HeifProfile p = HeifProfile::Auto;
        (void)pp::heif_profile_from_int(i, &p);
        if (p == HeifProfile::High || p == HeifProfile::Professional)
            continue; // AVIF 专用：HEIF tooltip 会说明「不能用于 HEIF」
        const std::string tip = pp::heif_profile_tooltip(false, p);
        check(!tip.empty(), "tooltip/nonempty/" + pc(p));
        if (p == HeifProfile::Main || p == HeifProfile::Main10 || p == HeifProfile::MainStill ||
            p == HeifProfile::Main12 || p == HeifProfile::Auto || p == HeifProfile::Main10Still)
            continue; // 非 RExt（16 的不可用理由另测；Apple 风险句只对 RExt 家族要求）
        // 17..21 + 7..15：RExt 家族 → Apple 风险句必须在。
        check(tip.find("iPhone") != std::string::npos, "tooltip/apple_risk/" + pc(p), tip);
    }
    check(pp::heif_profile_tooltip(false, HeifProfile::Main444_10Still).find("Rext444_10") !=
              std::string::npos,
          "tooltip/444_10still/same_signal");
    check(pp::heif_profile_tooltip(false, HeifProfile::Main444_10Still).find("R-2") !=
              std::string::npos,
          "tooltip/444_10still/spec_review");
    check(pp::heif_profile_tooltip(false, HeifProfile::Main10Still).find("不可用") !=
              std::string::npos,
          "tooltip/main10still/unavailable");
    check(pp::heif_profile_tooltip(false, HeifProfile::Main10Still).find("DPB") !=
              std::string::npos,
          "tooltip/main10still/dpb");
    check(pp::heif_profile_tooltip(true, HeifProfile::Main).find("AV1") != std::string::npos,
          "tooltip/avif/main");
    check(pp::heif_profile_tooltip(true, HeifProfile::High).find("4:4:4") != std::string::npos,
          "tooltip/avif/high");

    // —— x265 内部信号参数：16 走 Main10 三项注入；17..21 原生（不注入）——
    {
        const pp::HeifX265SignalParams p = pp::heif_x265_signal_params(HeifProfile::Main10Still);
        check(p.count == 3, "params/main10still/count");
        const pp::HeifX265SignalParams m10 = pp::heif_x265_signal_params(HeifProfile::Main10);
        for (int i = 0; i < 3; ++i)
            check(std::string(p.items[i].key) == std::string(m10.items[i].key) &&
                      std::string(p.items[i].value) == std::string(m10.items[i].value),
                  "params/main10still/same_as_main10/" + std::to_string(i));
    }
    for (const HeifProfile p : stills) {
        if (p == HeifProfile::Main10Still)
            continue;
        check(pp::heif_x265_signal_params(p).count == 0, "params/still_native/" + pc(p));
    }
}

void test_legacy_shims() {
    // test_enc_oiio.cpp 锁定旧口径；本文件确认兼容层未被真实信号改造带跑。
    check(pp::derive_heif_profile(false, 8, 1, false) == HeifProfile::Main, "legacy/derive/8");
    check(pp::derive_heif_profile(false, 8, 1, true) == HeifProfile::MainStill, "legacy/derive/8s");
    check(pp::derive_heif_profile(false, 10, 1, true) == HeifProfile::Main10, "legacy/derive/10");
    check(pp::derive_heif_profile(false, 12, 3, true) == HeifProfile::Main12, "legacy/derive/12");
    check(pp::derive_heif_profile(true, 8, 3, true) == HeifProfile::High, "legacy/derive/high");
    std::string why;
    check(pp::heif_profile_compatible(HeifProfile::Auto, 8, 1, true, &why), "legacy/compat/auto");
    check(pp::heif_profile_compatible(HeifProfile::Main10, 10, 1, true, &why), "legacy/compat/ok");
    check(!pp::heif_profile_compatible(HeifProfile::Main10, 8, 1, true, &why),
          "legacy/compat/mismatch");
    check(why.find("档位") != std::string::npos && why.find("位深") != std::string::npos,
          "legacy/compat/zh", why);
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    test_enum_numbering();
    test_signal_specs();
    test_signal_for_avif();
    test_derive_default();
    test_compat_matrix();
    test_labels();
    test_x265_signal_params();
    test_still_variants();
    test_legacy_shims();
    std::printf("heif_profile: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
