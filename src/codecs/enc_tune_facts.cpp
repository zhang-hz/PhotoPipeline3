// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10 W2-T3（C20）：tune 静态事实表实现。
//
// 事实逐条来源（可复核）：
//   * HEIF/x265 4.3 插件白名单 psnr/ssim/grain/fastdecode（encoder_x265.cc:177-180）；
//     x265 另有 zerolatency（单帧只改线程/GOP 类 → 无效果）与 vmaf（param.cpp:710-713
//     空实现）；animation 的 still 相关效果 = psy-rd 0.4 / aq-strength 0.4 / deblock 1:1，
//     插件不认该值，故由引擎注入（build/design-recon/heif/report.md §5.2/§5.4）。
//   * AVIF/SVT-AV1 4.2 原生 5 值 vq/psnr/ssim/iq/ms-ssim（encoder_svt.cc:122-129）；
//     iq/ms-ssim 会把 BRC 从 CQP 切到 CRF（encoder_svt.cc:873）。
//   * AVIF/libaom 3.15 原生 4 值 auto/psnr/ssim/iq；ssimulacra2 / vmaf_saliency_map 本构建
//     实测可编码（经 aom:tune custom option，encoder_aom.cc:1232-1248 最后应用）；
//     vmaf*/butteraugli/cdef-dist 本构建拒绝（CONFIG_TUNE_* 未启用）。
#include "codecs/enc_tune_facts.h"

#include <algorithm>
#include <cstdio>

namespace pp::tune {
namespace {

constexpr std::string_view kX265Tooltip =
    "调优（tune，x265 4.3）：插件在 preset/tune 之后强制 aq-mode=1、psy-rd=1.0、"
    "psy-rdoq=1.0（encoder_x265.cc:923-933），故 x265 tune 的 psy/AQ 部分被覆盖；"
    "残留差异 = psnr 的 aq-strength=0 / grain 的 sao=0 / fastdecode 的 deblock+sao=0。"
    "animation 为近似仿真：插件 tune 仍用默认 ssim，引擎在用户透传之前注入 "
    "x265:psy-rd=0.4 / x265:aq-strength=0.4 / x265:deblock=1:1；用户显式覆盖这些键时"
    "以用户为准（后写覆盖）。置灰项 = 本后端不可用（原因见各项 tooltip）。";

constexpr std::string_view kSvtTooltip =
    "调优（tune，SVT-AV1 4.2）：五个值均为插件原生（encoder_svt.cc:122-129）。注意 "
    "iq / ms-ssim 会把 BRC 从 CQP 切到 CRF（encoder_svt.cc:873），QP 路径的实际码率不再"
    "由 qp 决定；vq / psnr / ssim 保持 CQP。";

constexpr std::string_view kAomTooltip =
    "调优（tune，libaom 3.15）：auto/psnr/ssim/iq 为插件原生；ssimulacra2 与 "
    "vmaf_saliency_map 由引擎内部走 aom:tune=<值>（libheif 的 aom custom option 在全部"
    "内置控制之后应用，最后生效），不新增 aom:tune 目录行、预设键仍只有 tune。"
    "置灰项 = 本构建 libaom 未启用对应 tune（原因见各项 tooltip）。";

const BackendFacts kX265 = {
    "x265",
    kX265Tooltip,
    {
        {"psnr", "PSNR（客观）", Kind::Native,
         "原生：x265 tune=psnr。aq-strength=0（体积/分数最高；实测 1600×1200 10bit q75 "
         "为 93.64 分 / +16% 体积）。"},
        {"ssim", "SSIM（默认）", Kind::Native,
         "原生：x265 tune=ssim（插件默认值；psy/AQ 部分被插件固定项覆盖）。"},
        {"grain", "胶片颗粒", Kind::Native,
         "原生：x265 tune=grain。sao=0 保留颗粒，体积更小（实测 −14% 体积 / −0.94 分）。"},
        {"fastdecode", "快速解码", Kind::Native,
         "原生：x265 tune=fastdecode。deblock/sao=0，解码更快，质量略变（实测 −0.06 分）。"},
        {"animation", "动画（近似仿真）", Kind::Simulated,
         "近似仿真：插件 tune 仍为默认 ssim；引擎在用户透传前注入 x265:psy-rd=0.4 / "
         "x265:aq-strength=0.4 / x265:deblock=1:1（x265 tune=animation 的静止图相关效果）。"
         "非 x265 原生 tune，故为「近似」。用户显式覆盖这些键时以用户为准。"},
        {"zerolatency", "零延迟（不可用）", Kind::Disabled,
         "单帧无实际效果：x265 zerolatency 只改线程/GOP 类参数，静止图输出不变；"
         "libheif 插件白名单也未收录该值（encoder_x265.cc:177-180）。"},
        {"vmaf", "VMAF（不可用）", Kind::Disabled,
         "x265 4.3 的 tune=vmaf 是空实现（param.cpp:710-713），不产生任何效果；"
         "插件白名单同样未收录。"},
    },
};

const BackendFacts kSvt = {
    "svt-av1",
    kSvtTooltip,
    {
        {"vq", "视觉质量（VQ）", Kind::Native, "原生：SVT-AV1 tune=vq（保持 BRC=CQP）。"},
        {"psnr", "PSNR（默认）", Kind::Native, "原生：SVT-AV1 tune=psnr（插件默认；保持 CQP）。"},
        {"ssim", "SSIM", Kind::Native, "原生：SVT-AV1 tune=ssim（保持 CQP）。"},
        {"iq", "IQ（感知）", Kind::Native,
         "原生：SVT-AV1 tune=iq；会把 BRC 从 CQP 切到 CRF（encoder_svt.cc:873）。"},
        {"ms-ssim", "MS-SSIM", Kind::Native,
         "原生：SVT-AV1 tune=ms-ssim；会把 BRC 从 CQP 切到 CRF（encoder_svt.cc:873）。"},
    },
};

const BackendFacts kAom = {
    "libaom",
    kAomTooltip,
    {
        {"auto", "自动（默认）", Kind::Native, "原生：libaom tune=auto（插件当前默认值）。"},
        {"psnr", "PSNR", Kind::Native, "原生：libaom tune=psnr。"},
        {"ssim", "SSIM", Kind::Native, "原生：libaom tune=ssim。"},
        {"iq", "IQ（感知）", Kind::Native,
         "原生：libaom tune=iq；库自动启用 QM/锐化/色度 delta-q（与手工设置可能重叠）。"},
        {"ssimulacra2", "SSIMULACRA 2（感知）", Kind::Simulated,
         "仿真通道：引擎内部走 aom:tune=ssimulacra2（custom option 最后应用）；本构建实测"
         "可编码。库会启用 QM/锐化自动项，可能与手工 aom:enable-qm/qm-min/qm-max/sharpness "
         "重叠。"},
        {"vmaf_saliency_map", "VMAF 显著性图", Kind::Simulated,
         "仿真通道：引擎内部走 aom:tune=vmaf_saliency_map（custom option 最后应用）；"
         "本构建实测可编码（VMAF 评分本身不在本产品链路）。"},
        {"vmaf", "VMAF（不可用）", Kind::Disabled,
         "本构建 libaom 未启用 VMAF tune（CONFIG_TUNE_VMAF=0）：vmaf / vmaf_with_preprocessing "
         "/ vmaf_without_preprocessing / vmaf_neg 均在编码期被库拒绝。"},
        {"butteraugli", "Butteraugli（不可用）", Kind::Disabled,
         "本构建 libaom 未启用 Butteraugli tune（CONFIG_TUNE_BUTTERAUGLI=0）：编码期被库拒绝。"},
        {"cdef-dist", "CDEF-DIST（不可用）", Kind::Disabled,
         "本构建 libaom 未启用 cdef-dist tune：编码期被库拒绝（Invalid value 'cdef-dist'）。"},
    },
};

std::string join_selectable(const BackendFacts &facts) {
    std::string out;
    for (const ValueFact &v : facts.values) {
        if (v.kind == Kind::Disabled)
            continue;
        if (!out.empty())
            out += " / ";
        out += v.value;
    }
    return out;
}

} // namespace

const BackendFacts *facts_for(std::string_view backend) {
    if (backend == "x265")
        return &kX265;
    if (backend == "svt-av1")
        return &kSvt;
    if (backend == "libaom")
        return &kAom;
    return nullptr;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：backend/value 由事实表查询语义固定
const ValueFact *value_fact(std::string_view backend, std::string_view value) {
    const BackendFacts *facts = facts_for(backend);
    if (facts == nullptr)
        return nullptr;
    for (const ValueFact &v : facts->values)
        if (v.value == value)
            return &v;
    return nullptr;
}

std::string tune_tooltip(std::string_view backend) {
    const BackendFacts *facts = facts_for(backend);
    return facts != nullptr ? std::string(facts->tooltip) : std::string();
}

std::string unavailable_message(std::string_view backend, const ValueFact &fact) {
    const BackendFacts *facts = facts_for(backend);
    std::string msg = "调优值「";
    msg += fact.value;
    msg += "」不可用：";
    msg += fact.detail;
    if (facts != nullptr) {
        const std::string selectable = join_selectable(*facts);
        if (!selectable.empty()) {
            msg += " 可选值：";
            msg += selectable;
            msg += "。";
        }
    }
    return msg;
}

bool check_runtime_native(std::string_view backend, const std::vector<std::string> &runtime,
                          std::string *why) {
    if (why != nullptr)
        why->clear();
    const BackendFacts *facts = facts_for(backend);
    if (facts == nullptr) {
        if (why != nullptr)
            *why = "未知后端：无静态事实表";
        return false;
    }
    bool ok = true;
    const auto append = [why, &ok](const std::string &line) {
        ok = false;
        if (why != nullptr) {
            if (!why->empty())
                *why += "; ";
            *why += line;
        }
    };
    for (const ValueFact &v : facts->values) {
        const bool present =
            std::find(runtime.begin(), runtime.end(), std::string(v.value)) != runtime.end();
        if (v.kind == Kind::Native && !present)
            append("原生值不在运行期接受集：" + std::string(v.value));
        if (v.kind != Kind::Native && present)
            append(std::string(v.kind == Kind::Simulated ? "仿真值" : "置灰值") +
                   "出现在运行期接受集：" + std::string(v.value));
    }
    for (const std::string &value : runtime) {
        const ValueFact *fact = value_fact(backend, value);
        if (fact == nullptr)
            append("运行期值未收录在事实表：" + value);
    }
    return ok;
}

} // namespace pp::tune
