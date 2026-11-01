// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: M10 W2-T3（C20）tune 事实表 + 运行时内省一致性
//
// 断言面（docs/v0.4.0-design.md §6.3 / §12.3）：
//   1. 静态事实表自身不变量（值唯一、标签/原因非空、类别集合符合设计）；
//   2. **运行时内省 vs 事实表一致性**：原生事实 ⊆ 插件接受集；插件接受集 ⊆ 事实表；
//      仿真/置灰值不得被插件接受（防依赖升级静默漂移）；
//   3. 置灰值中文说明（可选值列出）与行 tooltip（机制解释）非空且可复核。
//
// 本文件不依赖编码产物（只创建编码器实例做内省）；失败打印 "FAIL <case>: <detail>"，
// main 返回失败数（与既有单测同体例）。
#include <libheif/heif.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "codecs/enc_tune_facts.h"

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

void info(const std::string &text) {
    std::printf("info %s\n", text.c_str());
    std::fflush(stdout);
}

std::string join(const std::vector<std::string> &values) {
    std::string out;
    for (const std::string &v : values) {
        if (!out.empty())
            out += ",";
        out += v;
    }
    return out;
}

std::vector<std::string> values_of_kind(const pp::tune::BackendFacts &facts, pp::tune::Kind kind) {
    std::vector<std::string> out;
    for (const pp::tune::ValueFact &v : facts.values)
        if (v.kind == kind)
            out.push_back(std::string(v.value));
    return out;
}

bool contains(const std::vector<std::string> &haystack, const std::string &needle) {
    for (const std::string &s : haystack)
        if (s == needle)
            return true;
    return false;
}

// 事实表不变量 + 设计集合（值集/类别逐值钉住，依赖升级/误改都红）。
void test_fact_table() {
    const char *const backends[] = {"x265", "svt-av1", "libaom"};
    for (const char *backend : backends) {
        const pp::tune::BackendFacts *facts = pp::tune::facts_for(backend);
        check(facts != nullptr, std::string("facts/backend/") + backend, "事实表缺失");
        if (facts == nullptr)
            continue;
        check(std::string(facts->backend) == backend, std::string("facts/id/") + backend,
              std::string(facts->backend));
        check(!facts->tooltip.empty(), std::string("facts/tooltip/") + backend, "tooltip 为空");
        check(!facts->values.empty(), std::string("facts/values/") + backend, "值集为空");
        std::vector<std::string> seen;
        for (const pp::tune::ValueFact &v : facts->values) {
            const std::string case_name =
                std::string("facts/value/") + backend + "/" + std::string(v.value);
            check(!v.value.empty(), case_name, "value 为空");
            check(!v.label.empty(), case_name, "label 为空");
            check(!v.detail.empty(), case_name, "detail 为空");
            check(!contains(seen, std::string(v.value)), case_name, "重复值");
            seen.push_back(std::string(v.value));
        }
        check(!values_of_kind(*facts, pp::tune::Kind::Native).empty(),
              std::string("facts/native/") + backend, "无原生值");
    }

    const pp::tune::BackendFacts *x265 = pp::tune::facts_for("x265");
    if (x265 != nullptr) {
        check(join(values_of_kind(*x265, pp::tune::Kind::Native)) == "psnr,ssim,grain,fastdecode",
              "facts/x265/native", join(values_of_kind(*x265, pp::tune::Kind::Native)));
        check(join(values_of_kind(*x265, pp::tune::Kind::Simulated)) == "animation",
              "facts/x265/simulated", join(values_of_kind(*x265, pp::tune::Kind::Simulated)));
        check(join(values_of_kind(*x265, pp::tune::Kind::Disabled)) == "zerolatency,vmaf",
              "facts/x265/disabled", join(values_of_kind(*x265, pp::tune::Kind::Disabled)));
        // 别名归一：fast-decode / zero-latency 不单列（插件白名单也不收）。
        check(pp::tune::value_fact("x265", "fast-decode") == nullptr &&
                  pp::tune::value_fact("x265", "zero-latency") == nullptr,
              "facts/x265/aliases", "fast-decode / zero-latency 不应单列");
        check(pp::tune::tune_tooltip("x265").find("animation") != std::string::npos &&
                  pp::tune::tune_tooltip("x265").find("近似仿真") != std::string::npos,
              "facts/x265/tooltip-mechanism", pp::tune::tune_tooltip("x265"));
    }

    const pp::tune::BackendFacts *svt = pp::tune::facts_for("svt-av1");
    if (svt != nullptr) {
        check(join(values_of_kind(*svt, pp::tune::Kind::Native)) == "vq,psnr,ssim,iq,ms-ssim",
              "facts/svt/native", join(values_of_kind(*svt, pp::tune::Kind::Native)));
        check(values_of_kind(*svt, pp::tune::Kind::Simulated).empty() &&
                  values_of_kind(*svt, pp::tune::Kind::Disabled).empty(),
              "facts/svt/extra", "SVT 值集应全为原生（5/5）");
        check(pp::tune::tune_tooltip("svt-av1").find("CRF") != std::string::npos,
              "facts/svt/tooltip-crf", pp::tune::tune_tooltip("svt-av1"));
    }

    const pp::tune::BackendFacts *aom = pp::tune::facts_for("libaom");
    if (aom != nullptr) {
        check(join(values_of_kind(*aom, pp::tune::Kind::Native)) == "auto,psnr,ssim,iq",
              "facts/aom/native", join(values_of_kind(*aom, pp::tune::Kind::Native)));
        check(join(values_of_kind(*aom, pp::tune::Kind::Simulated)) ==
                  "ssimulacra2,vmaf_saliency_map",
              "facts/aom/simulated", join(values_of_kind(*aom, pp::tune::Kind::Simulated)));
        check(join(values_of_kind(*aom, pp::tune::Kind::Disabled)) == "vmaf,butteraugli,cdef-dist",
              "facts/aom/disabled", join(values_of_kind(*aom, pp::tune::Kind::Disabled)));
        check(pp::tune::tune_tooltip("libaom").find("aom:tune") != std::string::npos,
              "facts/aom/tooltip-channel", pp::tune::tune_tooltip("libaom"));
    }

    check(pp::tune::facts_for("nope") == nullptr, "facts/unknown-backend", "未知后端应为空");
    check(pp::tune::tune_tooltip("nope").empty(), "facts/unknown-tooltip",
          "未知后端 tooltip 应为空");
}

// 置灰值中文说明：点名值 + 不可用原因 + 可选值（预检/编码器共用）。
void test_unavailable_messages() {
    const struct {
        const char *backend;
        const char *value;
    } cases[] = {
        {"x265", "zerolatency"},   {"x265", "vmaf"},        {"libaom", "vmaf"},
        {"libaom", "butteraugli"}, {"libaom", "cdef-dist"},
    };
    for (const auto &c : cases) {
        const pp::tune::ValueFact *fact = pp::tune::value_fact(c.backend, c.value);
        check(fact != nullptr && fact->kind == pp::tune::Kind::Disabled,
              std::string("msg/fact/") + c.backend + "/" + c.value, "应为置灰值");
        if (fact == nullptr)
            continue;
        const std::string msg = pp::tune::unavailable_message(c.backend, *fact);
        const std::string case_name = std::string("msg/") + c.backend + "/" + c.value;
        check(msg.find("不可用") != std::string::npos, case_name, msg);
        check(msg.find(c.value) != std::string::npos, case_name, msg);
        check(msg.find("可选值") != std::string::npos, case_name, msg);
    }
}

// 运行时内省：插件自己的 tune 合法值集 + 当前默认值。
struct RuntimeTune {
    bool available = false;
    std::string error;
    std::string plugin;
    std::string default_value;
    std::vector<std::string> values;
};

RuntimeTune introspect(heif_compression_format format, const char *filter) {
    RuntimeTune out;
    heif_context *ctx = heif_context_alloc();
    if (ctx == nullptr) {
        out.error = "heif_context_alloc failed";
        return out;
    }
    const heif_encoder_descriptor *descs[16] = {};
    const int n = heif_get_encoder_descriptors(format, filter, descs, 16);
    if (n <= 0 || descs[0] == nullptr) {
        out.error = std::string("no encoder descriptor for filter '") + filter + "'";
        heif_context_free(ctx);
        return out;
    }
    heif_encoder *enc = nullptr;
    const heif_error e = heif_context_get_encoder(ctx, descs[0], &enc);
    if (e.code != heif_error_Ok || enc == nullptr) {
        out.error = std::string("heif_context_get_encoder failed: ") +
                    (e.message != nullptr ? e.message : "?");
        heif_context_free(ctx);
        return out;
    }
    const char *name = heif_encoder_get_name(enc);
    out.plugin = name != nullptr ? name : "";
    const heif_encoder_parameter *const *params = heif_encoder_list_parameters(enc);
    for (int i = 0; params != nullptr && params[i] != nullptr; ++i) {
        const char *pname = heif_encoder_parameter_get_name(params[i]);
        if (pname == nullptr || std::string(pname) != "tune")
            continue;
        const char *const *strings = nullptr;
        const heif_error ve = heif_encoder_parameter_get_valid_string_values(params[i], &strings);
        if (ve.code != heif_error_Ok || strings == nullptr) {
            out.error = "tune is not a string enum";
            break;
        }
        for (int j = 0; strings[j] != nullptr; ++j)
            out.values.emplace_back(strings[j]);
        char buf[512] = {};
        if (heif_encoder_get_parameter_string(enc, "tune", buf, sizeof(buf)).code == heif_error_Ok)
            out.default_value = buf;
        out.available = true;
        break;
    }
    if (!out.available && out.error.empty())
        out.error = "tune parameter not exposed";
    heif_encoder_release(enc);
    heif_context_free(ctx);
    return out;
}

void test_runtime_consistency() {
    const struct {
        const char *backend;
        const char *filter;
        heif_compression_format format;
    } backends[] = {
        {"x265", "x265", heif_compression_HEVC},
        {"svt-av1", "svt", heif_compression_AV1},
        {"libaom", "aom", heif_compression_AV1},
    };
    for (const auto &b : backends) {
        const RuntimeTune rt = introspect(b.format, b.filter);
        const std::string case_name = std::string("runtime/") + b.backend;
        check(rt.available, case_name, rt.error);
        if (!rt.available)
            continue;
        info(std::string("runtime ") + b.backend + " plugin='" + rt.plugin + "' default='" +
             rt.default_value + "' tune={" + join(rt.values) + "}");
        std::string why;
        const bool consistent = pp::tune::check_runtime_native(b.backend, rt.values, &why);
        check(consistent, case_name, why);
        // 默认值必须是事实表里的**原生**值（否则下拉默认项会指向仿真/置灰值）。
        const pp::tune::ValueFact *def = pp::tune::value_fact(b.backend, rt.default_value);
        check(def != nullptr && def->kind == pp::tune::Kind::Native, case_name + "/default",
              "插件默认 tune='" + rt.default_value + "' 未归类为原生值");
    }
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    test_fact_table();
    test_unavailable_messages();
    test_runtime_consistency();
    std::printf("test_heif_tune: checks=%d failed=%d\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
