// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T7: libheif encoders (HEIF/HEVC + AVIF/AV1), runtime parameter
// introspection (R4) and the 10-bit capability probe (R19).
//
// Design notes (facts measured on libheif 1.23.1, see docs/m1-tasks.md §7):
//   * `heif_get_encoder_descriptors(format, name_filter, out, count)` is a 4-argument free
//     function (no context) in 1.23.1; the name filter is a substring of the encoder's
//     `id_name`.
//   * Parameter names are NEVER hard-coded beyond the semantic ones named by the task book
//     (quality / lossless / chroma / preset): the ParamSet -> encoder mapping iterates
//     `heif_encoder_list_parameters()` and dispatches by the *runtime* parameter type.
//     M4-T7（§3.1 线程映射义务）再点名一个语义键：`threads` —— 且**仍走内省判定**
//     （is_known("threads")），插件不暴露该键时如实记录并保留后端默认池（R4 口径）。
//   * Pixels are handed to libheif as Y/Cb/Cr planes (§3.8 mapping table). The RGB -> YCbCr
//     matrix is full-range BT.601 (matrix_coefficients = 6 / SMPTE 170M), which is what the
//     nclx profile written next to the planes declares.
#include <libheif/heif.h>
#include <libheif/heif_tiling.h> // 大图无损：标准 HEIF grid（heif_context_add_grid_image）

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "codecs/enc_common.h"     // M7-WP8/B.7(R-156/162/165/166): shared helpers
#include "codecs/enc_tune_facts.h" // M10 W2-T3（C20）：tune 静态事实表（原生/仿真/置灰）
#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "codecs/encoders.h"
#include "codecs/heif_capability.h"       // M8：Level 拟合 / grid 规划 / hvcC 回读
#include "codecs/heif_nclx.h"             // M7-WP2 UX-14 A1：nclx ← ICC 推导（纯函数）
#include "codecs/heif_profile.h"          // M7-WP2 I4：编码档位单源（UX-10.1/10.3）
#include "codecs/param_catalog/catalog.h" // M8：目录行合并（x265:/aom: 透传键 + 只读行）
#include "core/fsops.h"                   // R-003：atomic_temp_path / commit / discard
#include "core/logger.h"
#include "core/params.h"
#include "core/simd/simd.h" // M4-W5-T19：交织 + 量化热路径（§11.2）

namespace pp {
namespace {

constexpr std::string_view kStage = "encode";
constexpr std::string_view kFile = "enc_heif.cpp";

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

// M7-WP8/B.7 (R-156): ms_since / param_value_text live in codecs/enc_common (single source).

std::string heif_error_text(const heif_error &e) {
    std::string s = (e.message && *e.message) ? e.message : "unspecified libheif error";
    s += " (heif_error code=" + std::to_string(static_cast<int>(e.code)) +
         " subcode=" + std::to_string(static_cast<int>(e.subcode)) + ")";
    return s;
}

// M8-B1（C7/§4.5）：插件透传值格式化。libheif 的 aom 插件把 `aom:<opt>` 原样交给
// aom_codec_set_option()，后者用 strtoul 解析参数 → bool 只认数字 "0"/"1"（实测
// "true" 报 "Invalid character 't'"）；x265 的 x265_atobool 同时接受 "true"/"false" 与
// "1"/"0" —— 故统一发数字（两端都合法）。整数十进制、浮点 %.10g（x265 atof / aom
// strtoul 之外的 aom 无浮点目录键）、字符串原样。
std::string passthrough_value_text(const ParamValue &v) {
    if (const bool *b = std::get_if<bool>(&v))
        return *b ? "1" : "0";
    if (const int64_t *i = std::get_if<int64_t>(&v))
        return std::to_string(*i);
    if (const double *d = std::get_if<double>(&v)) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.10g", *d);
        return buf;
    }
    if (const std::string *s = std::get_if<std::string>(&v))
        return *s;
    return {};
}

// C3/C7：透传键必须是 `prefix:engine-name` 形态；x265:/aom:/svt: 是仅有的三个前缀名。
bool is_passthrough_key(std::string_view key) {
    return key.rfind("x265:", 0) == 0 || key.rfind("aom:", 0) == 0 || key.rfind("svt:", 0) == 0;
}

constexpr heif_error kHeifOk{heif_error_Ok, heif_suberror_Unspecified, nullptr};

bool libheif_format(std::string_view format_id, heif_compression_format &out) {
    if (format_id == "heif") {
        out = heif_compression_HEVC;
        return true;
    }
    if (format_id == "avif") {
        out = heif_compression_AV1;
        return true;
    }
    return false;
}

// backend_id (frozen format table) -> substring of heif_encoder_descriptor_get_id_name()
std::string_view backend_match(std::string_view backend_id) {
    if (backend_id == "x265")
        return "x265";
    if (backend_id == "svt-av1")
        return "svt";
    if (backend_id == "libaom")
        return "aom";
    return backend_id;
}

// runtime id_name / descriptive name -> frozen backend_id (unknown encoders keep their name)。
// W2-T3（C20）：输入既可能是 id_name（"x265"/"svt"/"aom"），也可能是
// heif_encoder_get_name() 的描述串（"AOMedia Project AV1 Encoder …"/"SVT-AV1 encoder …"，
// 大写前缀）—— 必须大小写不敏感，否则 tune 事实表查不到 avif 两个后端。
std::string canonical_backend_id(std::string_view id_name) {
    std::string lowered(id_name);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lowered.find("x265") != std::string::npos)
        return "x265";
    if (lowered.find("svt") != std::string::npos)
        return "svt-av1";
    if (lowered.find("aom") != std::string::npos)
        return "libaom";
    return std::string(id_name);
}

// Descriptors are enumerated per call (the list is owned by libheif and stable for the
// process lifetime, but the returned pointer array is ours).
// R-305（M7-WP2）：旧实现固定 `raw[32]`，插件数 > 32 时**静默截断**。改为容量自增重试
// （返回值 == 容量即认为可能截断 → 翻倍再来），并给一个显式上限（1024，超出即 log_error
// 并按截断如实返回），不再无声丢插件。
std::vector<const heif_encoder_descriptor *> enumerate_descriptors(heif_compression_format fmt) {
    constexpr int kHardCap = 1024;
    int cap = 32;
    for (;;) {
        std::vector<const heif_encoder_descriptor *> raw(static_cast<size_t>(cap), nullptr);
        const int n = heif_get_encoder_descriptors(fmt, nullptr, raw.data(), cap);
        std::vector<const heif_encoder_descriptor *> out;
        for (int i = 0; i < n && i < cap; i++)
            if (raw[static_cast<size_t>(i)])
                out.push_back(raw[static_cast<size_t>(i)]);
        if (n < cap)
            return out; // 未满 ⇒ 无截断
        if (cap >= kHardCap) {
            log_error(kStage, kFile, "encoder descriptor list truncated at hard cap",
                      {{"cap", std::to_string(kHardCap)}});
            return out;
        }
        cap *= 2;
    }
}

const heif_encoder_descriptor *find_descriptor(heif_compression_format fmt,
                                               std::string_view backend_id) {
    const std::vector<const heif_encoder_descriptor *> descs = enumerate_descriptors(fmt);
    if (descs.empty())
        return nullptr;
    const std::string_view want = backend_match(backend_id);
    if (backend_id.empty() || want.empty())
        return descs.front(); // empty = highest priority
    for (const heif_encoder_descriptor *d : descs) {
        const char *id_name = heif_encoder_descriptor_get_id_name(d);
        if (id_name && std::string_view(id_name).find(want) != std::string_view::npos)
            return d;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// runtime parameter introspection -> ParamDef
// ---------------------------------------------------------------------------

ParamDef param_def_from(heif_encoder *enc, const heif_encoder_parameter *p) {
    ParamDef d;
    const char *name = heif_encoder_parameter_get_name(p);
    d.key = name ? name : "";
    d.label = d.key;
    // W2-T3（C20）：插件身份在 tune 分支之前就要用（事实表按 backend id 查值/标签/原因）。
    const char *plugin_name = heif_encoder_get_name(enc);
    const std::string plugin = plugin_name ? plugin_name : "";
    const std::string backend = canonical_backend_id(plugin);
    // M7-R2.5：内省参数以 libheif/插件键名作标签，中文界面里出现 speed/quality/chroma/
    // tune/preset 等英文行。只翻**常用键**（其余键名保留，tooltip 有来源说明）。
    if (d.key == "quality")
        d.label = "质量";
    else if (d.key == "speed")
        d.label = "速度";
    else if (d.key == "chroma")
        d.label = "色度采样";
    else if (d.key == "tune")
        d.label = "调优";
    else if (d.key == "preset")
        d.label = "预设";
    else if (d.key == "threads")
        d.label = "线程数";
    else if (d.key == "qp")
        d.label = "量化参数";
    else if (d.key == "min-q")
        d.label = "最小量化";
    else if (d.key == "max-q")
        d.label = "最大量化";
    // AVIF-01：has_default=false 的“可选覆盖”参数（SVT qp 等）不得把范围下限当默认值；
    // 由 params/paramform 跳过默认写入，编码器保持插件默认。
    d.has_default = heif_encoder_has_default(enc, d.key.c_str()) != 0;
    // HEIF-05：无损下 x265/SVT 的 quality 被 bLossless 覆盖，UI 不应继续可调。
    if (d.key == "quality")
        d.visible = [](const ParamSet &s) { return !lossless_flag(s); };
    // E3 由调度器强制写入 threads（AVIF）；x265 根本没有该参数。UI 上再给一个可调旋钮
    // 只会“看起来能设、实际被覆盖/不存在” → 隐藏；运行日志仍有 E 分配记录。
    if (d.key == "threads")
        d.visible = [](const ParamSet &) { return false; };
    const heif_encoder_parameter_type t = heif_encoder_parameter_get_type(p);
    std::string type_desc;
    if (t == heif_encoder_parameter_type_integer) {
        int have_min = 0, have_max = 0, mn = 0, mx = 0, nvalid = 0;
        const int *values = nullptr;
        heif_encoder_parameter_get_valid_integer_values(p, &have_min, &have_max, &mn, &mx, &nvalid,
                                                        &values);
        int current = 0;
        const bool have_current =
            heif_encoder_get_parameter_integer(enc, d.key.c_str(), &current).code == heif_error_Ok;
        if (nvalid > 0 && values) {
            // fixed value set -> Enum with integer choices (e.g. svt tile-rows)
            d.type = ParamType::Enum;
            for (int i = 0; i < nvalid; i++)
                d.choices.emplace_back(std::to_string(values[i]), int64_t(values[i]));
            // R-230：默认值口径统一为**插件当前值**（与 Int/Bool/String 分支同源）；
            // 旧口径取 values[0] ⇒ UI 默认值失真（插件默认未必是值集首项）。
            d.def = int64_t(have_current ? current : values[0]);
            type_desc = "integer set";
        } else {
            d.type = ParamType::Int;
            d.lo = have_min ? static_cast<double>(mn) : static_cast<double>(INT32_MIN);
            d.hi = have_max ? static_cast<double>(mx) : static_cast<double>(INT32_MAX);
            d.step = 1;
            d.def = int64_t(have_current ? current : (have_min ? mn : 0));
            type_desc = have_min || have_max
                            ? "integer [" + std::to_string(mn) + "," + std::to_string(mx) + "]"
                            : "integer";
        }
    } else if (t == heif_encoder_parameter_type_boolean) {
        d.type = ParamType::Bool;
        int current = 0;
        d.def =
            heif_encoder_get_parameter_boolean(enc, d.key.c_str(), &current).code == heif_error_Ok
                ? (current != 0)
                : false;
        type_desc = "boolean";
    } else {
        d.type = ParamType::Enum;
        const char *const *strings = nullptr;
        if (heif_encoder_parameter_get_valid_string_values(p, &strings).code == heif_error_Ok &&
            strings) {
            for (int i = 0; strings[i]; i++)
                d.choices.emplace_back(strings[i], std::string(strings[i]));
        }
        char buf[512] = {};
        std::string current;
        if (heif_encoder_get_parameter_string(enc, d.key.c_str(), buf, sizeof(buf)).code ==
            heif_error_Ok)
            current = buf;
        if (current.empty() && !d.choices.empty())
            current = std::get<std::string>(d.choices.front().second);
        d.def = current;
        type_desc = d.choices.empty() ? "string (unconstrained)" : "string set";
    }
    // W2-T3（C20）：tune 值集/标签/原因 = 静态事实表（原生 + 仿真 + 置灰）。运行时内省仍是
    // 原生集合的第一来源；事实表补齐"插件不支持但库支持/可仿真"与"不可用（置灰）"两组，
    // 二者的一致性与漂移由 tests/unit/test_heif_tune.cpp 机械断言。
    const bool is_x265 = backend == "x265";
    if (d.key == "tune") {
        if (const pp::tune::BackendFacts *facts = pp::tune::facts_for(backend)) {
            d.choices.clear();
            for (const pp::tune::ValueFact &v : facts->values)
                d.choices.emplace_back(std::string(v.label), std::string(v.value));
        }
    }
    // W2-T4（C21）：quality 行的等效映射（x265 限定：CRF=(100−q)/2；AVIF 两个后端映射不同，
    // 不改标签以免给出错误公式）。标签轨 80px 会省略，全文见 tooltip。
    if (d.key == "quality" && is_x265)
        d.label = "质量（0–100；CRF=(100−q)/2）";
    // 核心/高级白名单（UX-10.2/10.3，M7-R2A 对表收口）：核心集**按插件真实参数面逐插件**
    // 声明（旧口径是跨插件并集 + 内省过滤：SVT 无 preset 却共享 x265 的 preset 核心位、
    // x265 无 speed 却共享 SVT 的 speed 核心位 —— 两向错位都靠"插件恰好不暴露"掩盖，
    // 不是对表）。现口径：
    //   x265（HEIF）：quality / lossless / chroma / **preset** / tune（画质取向提核心）；
    //   SVT-AV1 / libaom（AVIF）：quality / lossless / chroma / **speed** / tune
    //   （speed 是 SVT 的速度主档；**SVT 无 preset**）；libaom 同集合与其实际键的交集。
    // 其余插件旋钮（tu-intra-depth / complexity / threads / qp / min-q / max-q …）留高级。
    d.advanced =
        !(d.key == "quality" || d.key == "lossless" || d.key == "chroma" || d.key == "tune" ||
          (is_x265 && d.key == "preset") || (!is_x265 && d.key == "speed"));
    // M7-R2.1-F4（R2D2 ③-6 余量收口）：ParamDef.group 显式声明（A19 词表单源）——
    // 核心集 → kGroupCore（不入子分组折叠）；其余插件旋钮 → kGroupEncodeTools。旧口径
    // group 缺省空串、由 paramform 回落「编码工具」（paramform.cpp subgroup_of 的空串分支），
    // 归属判据从消费端回落改为声明面显式，分组结果逐项不变。
    d.group = d.advanced ? pp::kGroupEncodeTools : pp::kGroupCore;
    d.tooltip = "libheif 运行时内省参数（heif_encoder_list_parameters，类型 " + type_desc + "）：" +
                d.key + "。名称/范围/默认值均来自当前编码插件，M1 不硬编码。";
    // W2-T3（C20）：tune 的机制解释（事实表单源）接在基础内省说明之前；必须放在上面
    // 基础 tooltip 赋值**之后**（旧位置被逐字覆盖 → UI 探针实测抓到，勿再前移）。
    if (d.key == "tune") {
        if (const pp::tune::BackendFacts *facts = pp::tune::facts_for(backend))
            d.tooltip = std::string(facts->tooltip) + "\n" + d.tooltip;
    }
    // W2-T4（C21）：quality 映射说明同样追加在基础 tooltip 之后（不能提前）。
    if (d.key == "quality" && is_x265) {
        d.tooltip += "\n质量 0–100 在 x265 后端映射为 CRF=(100−quality)/2"
                     "（quality=50 ⇔ CRF 25，encoder_x265.cc:992-998）；"
                     "CRF/QP 的合法域随位深下移（8 位 0..51、10 位 −12..51、12 位 −24..51）。";
    }
    return d;
}

// Live introspection of one encoder instance: the encoder's own parameter list.
std::vector<ParamDef> introspect_live(heif_encoder *enc) {
    std::vector<ParamDef> out;
    const heif_encoder_parameter *const *ps = heif_encoder_list_parameters(enc);
    for (int i = 0; ps && ps[i]; i++) {
        const char *name = heif_encoder_parameter_get_name(ps[i]);
        // 第二轮裁定：alpha 明确不支持；不暴露 alpha 专用参数（libaom 的 alpha-quality）。
        if (name != nullptr) {
            const std::string_view n{name};
            if (n == "alpha-quality" || n == "alpha_quality")
                continue;
        }
        out.push_back(param_def_from(enc, ps[i]));
    }
    return out;
}

// ---------------------------------------------------------------------------
// YCbCr plane filling (full-range BT.601)
// ---------------------------------------------------------------------------

struct ChromaLayout {
    heif_chroma chroma = heif_chroma_420;
    int cw_div = 2, ch_div = 2; // chroma width/height divisors, ceil() at odd sizes
    int cw_off = 1, ch_off = 1; // rounding offsets (x + off) / div
};

ChromaLayout layout_for(heif_chroma c) {
    ChromaLayout l;
    l.chroma = c;
    if (c == heif_chroma_444) {
        l.cw_div = 1;
        l.ch_div = 1;
        l.cw_off = 0;
        l.ch_off = 0;
    } else if (c == heif_chroma_422) {
        l.cw_div = 2;
        l.ch_div = 1;
        l.cw_off = 1;
        l.ch_off = 0;
    }
    return l;
}

int quantize(float v, int maxv) {
    // R-089（M7-WP2）：量化口径统一为 **simd::quantize16**（float 乘加 + 截断，E6 标准舍入）。
    // 本函数原是 chroma 平面的 `lround(clamp·maxv)`（double 半入），与 Y/alpha 的
    // quantize16 并存 ⇒ 平面间最多差 1 LSB。现仅保留为**逐样本回退**（长度非整行的
    // 边角），算子序列与 quantize16_ref 逐字一致（clamp → ×maxv+0.5 → 截断），
    // 不再引入第二套舍入语义。
    const float x = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    // 算子序列保持 clamp → ×maxv+0.5 → 截断（禁止 lround 替换：与 quantize16_ref 逐字同序）；
    // 拆命名临时量仅为可读性/告警面，浮点运算序零变化。
    const float scaled = x * static_cast<float>(maxv) + 0.5f;
    return static_cast<int>(scaled);
}

void store_sample(uint8_t *base, size_t stride, int bitdepth, int x, int y, int value) {
    if (bitdepth > 8)
        reinterpret_cast<uint16_t *>(base + static_cast<size_t>(y) * stride)[x] =
            static_cast<uint16_t>(value);
    else
        base[static_cast<size_t>(y) * stride + x] = static_cast<uint8_t>(value);
}

// R-314（0.3.6-alpha1）：从**已写出的容器**回读主图位深（libheif 的 handle 在
// heif_context_encode_image 刚返回时调用 get_luma_bits_per_pixel 会 AV —— 见调用点注释；
// 落盘后重新 read_from_file 的 handle 是安全的，且这个口径就是解码器看到的事实）。
int heif_file_luma_bits(const std::filesystem::path &p) {
    heif_context *ctx = heif_context_alloc();
    if (ctx == nullptr)
        return -1;
    int bits = -1;
    heif_image_handle *handle = nullptr;
    const std::string file = normalize_extended_path(p).string();
    if (heif_context_read_from_file(ctx, file.c_str(), nullptr).code == heif_error_Ok &&
        heif_context_get_primary_image_handle(ctx, &handle).code == heif_error_Ok &&
        handle != nullptr) {
        bits = heif_image_handle_get_luma_bits_per_pixel(handle);
    }
    if (handle != nullptr)
        heif_image_handle_release(handle);
    heif_context_free(ctx);
    return bits;
}

// ---------------------------------------------------------------------------
// nclx 色彩描述与嵌入 ICC 的一致性（UX-14 A1，M7-WP2）
// ---------------------------------------------------------------------------
// 推导实现 = pp::nclx_for_icc()（codecs/heif_nclx.h，纯函数、可单测）。口径：nclx 必须
// 描述像素实际所在的 RGB 空间（从 meta.icc_profile 反推 CICP 码；无 ICC → sRGB 现状；
// 无法识别 → unspecified 不冒充 sRGB）。矩阵系数照写 BT.601 全范围（上方像素段的事实）。

} // namespace

// ---------------------------------------------------------------------------
// encoder
// ---------------------------------------------------------------------------

namespace {

class HeifEncoder final : public IEncoder {
public:
    HeifEncoder(std::string format_id, std::string backend_id)
        : format_id_(std::move(format_id)), backend_id_(std::move(backend_id)) {}

    const FormatDef &format() const override {
        const FormatDef *f = find_format(format_id_);
        assert(f != nullptr);
        static const FormatDef kFallback{};
        return f ? *f : kFallback;
    }

    EncodeResult encode(const EncodeRequest &req) override;

private:
    // E1: immutable -> one instance may serve several workers concurrently.
    const std::string format_id_;
    const std::string backend_id_;
};

EncodeResult HeifEncoder::encode(const EncodeRequest &req) {
    EncodeResult res;
    const auto t0 = std::chrono::steady_clock::now();
    auto finish = [&]() {
        res.t.encode_ms = enc::ms_since(t0);
        res.t.total_ms = res.t.encode_ms;
        return res;
    };
    // R-162 unified end state (M7-WP8/B.7): no error-level log here -- pipeline records
    // the single `encode failed` line (old double record removed). Timing via finish().
    auto fail = [&](const std::string &msg) {
        res.bytes = 0;
        res.error = msg;
        return finish();
    };
    // E7 metadata failures are the only encode-time warnings: they change what lands in the
    // output container. E9 (unknown / ignored / forced parameters) is a configuration defect
    // and is reported through the run log only — never through EncodeResult.warnings
    // (main-dialogue ruling, §3.8 T6 落地口径 ⑥).
    auto note_meta = [&](const std::string &msg) {
        log_warn(kStage, kFile, msg, {{"format", format_id_}, {"backend", backend_id_}});
        res.warnings.push_back(Warning{WarningKind::MetadataDropped, msg});
    };
    auto note_param = [&](const std::string &msg, const std::string &key,
                          const std::string &value) {
        log_warn(
            kStage, kFile, msg,
            {{"format", format_id_}, {"backend", backend_id_}, {"param", key}, {"value", value}});
    };

    try {
        if (!req.img.initialized() || req.img.spec().width <= 0 || req.img.spec().height <= 0)
            return fail("empty input image");
        const OIIO::ImageSpec &ispec = req.img.spec();
        const int w = ispec.width, h = ispec.height, nch = ispec.nchannels;
        assert(w > 0 && h > 0);
        assert(nch >= 1 && nch <= 4);
        if (nch < 1 || nch > 4)
            return fail("unsupported channel count " + std::to_string(nch) + " (expected 1..4)");
        if (req.target.out_bitdepth != 8 && req.target.out_bitdepth != 10 &&
            req.target.out_bitdepth != 12)
            return fail("unsupported bitdepth " + std::to_string(req.target.out_bitdepth) +
                        " for " + format_id_ + " (expected 8, 10 or 12)");
        // 无损口径（R-307 的单源读取；在参数循环之前就要用）：
        //   * 无损模式下 chroma **由用户/参数决定**（M15 用户面要求：无损 = RC 第四档，
        //     色度采样仍可配置）；不再强制 4:4:4，也不再因后端不暴露 chroma 而失败；
        //   * 4:2:0/4:2:2 在编码前下采样色度 —— x265 的 bLossless 只保证输入 YCbCr 平面
        //     无损，对 RGB 源不是像素级无损 ⇒ 下方给**如实中文警告**，不静默假装像素无损。
        const bool want_lossless = lossless_flag(req.target.params);

        heif_compression_format fmt = heif_compression_HEVC;
        if (!libheif_format(format_id_, fmt))
            return fail("not a libheif format: " + format_id_);
        const heif_encoder_descriptor *desc = find_descriptor(fmt, backend_id_);
        if (!desc)
            return fail("no libheif encoder available for backend '" + backend_id_ + "' (" +
                        format_id_ + ")");

        heif_context *ctx = heif_context_alloc();
        if (!ctx)
            return fail("heif_context_alloc() failed");
        struct CtxGuard {
            heif_context *c;
            ~CtxGuard() {
                if (c)
                    heif_context_free(c);
            }
        } ctx_guard{ctx};

        heif_encoder *enc = nullptr;
        heif_error e = heif_context_get_encoder(ctx, desc, &enc);
        if (e.code != heif_error_Ok || !enc)
            return fail("heif_context_get_encoder(" + backend_id_ +
                        ") failed: " + heif_error_text(e));
        struct EncGuard {
            heif_encoder *e;
            ~EncGuard() {
                if (e)
                    heif_encoder_release(e);
            }
        } enc_guard{enc};

        // ---- parameters: map the ParamSet through the encoder's own parameter list ----
        const std::vector<ParamDef> live = introspect_live(enc);
        std::vector<std::string> known;
        known.reserve(live.size());
        for (const ParamDef &p : live)
            known.push_back(p.key);
        auto is_known = [&known](const std::string &k) {
            return std::find(known.begin(), known.end(), k) != known.end();
        };
        // W2-T3（C20）：tune 的仿真/置灰处理需要后端身份；backend_id_ 为空（= 首个插件）
        // 时按插件 id_name 归一（与 param_def_from 同一 canonical_backend_id）。
        const char *enc_name = heif_encoder_get_name(enc);
        const std::string tune_backend =
            backend_id_.empty() ? canonical_backend_id(enc_name ? enc_name : "") : backend_id_;
        bool tune_animation_sim = false; // animation：插件 tune 保持 ssim，注入三键
        std::string tune_aom_sim;        // libaom 扩展值：走内部 aom:tune 通道

        for (const ParamDef &p : live) {
            const auto it = req.target.params.find(p.key);
            if (it == req.target.params.end())
                continue;
            heif_error se = kHeifOk;
            std::string rejected;
            switch (p.type) {
            case ParamType::Int: {
                const int64_t *iv = std::get_if<int64_t>(&it->second);
                if (!iv) {
                    rejected = "expected integer";
                    break;
                }
                if (*iv < INT32_MIN || *iv > INT32_MAX) {
                    rejected = "out of integer range";
                    break;
                }
                int iv32 = static_cast<int>(*iv);
                // AVIF-07：libsvt 对 all-intra 把 speed>9 全部映射到 M9；UI 的 10–13 是死档。
                if (format_id_ == "avif" && p.key == "speed" && iv32 > 9) {
                    note_param("svt-av1 all-intra maps speed>9 to 9", p.key,
                               std::to_string(iv32) + " -> 9");
                    iv32 = 9;
                }
                se = heif_encoder_set_parameter_integer(enc, p.key.c_str(), iv32);
                break;
            }
            case ParamType::Enum: {
                // R-306（0.3.6-alpha1）：Enum 有两个来源 —— 整数选择集（int64_t）与字符串
                // 选择集（chroma / preset / tune 等，ParamValue 是 std::string）。旧实现把
                // Enum 一律当整数派发 → 字符串参数全部报 "expected integer" 被丢弃，
                // 于是 HEIF 的 chroma=444/422、preset、tune **从未生效**（libheif 插件按
                // 默认 420/medium 编码，UI 选 4:4:4 实际仍是 4:2:0）。按值类型分派。
                if (const int64_t *iv = std::get_if<int64_t>(&it->second)) {
                    if (*iv < INT32_MIN || *iv > INT32_MAX) {
                        rejected = "out of integer range";
                        break;
                    }
                    se = heif_encoder_set_parameter_integer(enc, p.key.c_str(),
                                                            static_cast<int>(*iv));
                } else if (const std::string *sv = std::get_if<std::string>(&it->second)) {
                    // W2-T3（C20）：tune 的三类值分开处理 —— 原生走插件；仿真值不下发插件
                    // （插件白名单会拒绝），由本文件在用户透传之前注入/走内部 aom:tune；
                    // 置灰值给中文可操作错误（不把 plugin 的 "invalid value" 直接抛给用户）。
                    const pp::tune::ValueFact *tune_fact =
                        p.key == "tune" ? pp::tune::value_fact(tune_backend, *sv) : nullptr;
                    if (tune_fact != nullptr && tune_fact->kind == pp::tune::Kind::Disabled) {
                        return fail(pp::tune::unavailable_message(tune_backend, *tune_fact));
                    }
                    if (tune_fact != nullptr && tune_fact->kind == pp::tune::Kind::Simulated) {
                        if (tune_backend == "x265" && *sv == "animation") {
                            tune_animation_sim = true;
                            break; // 不调 set_parameter_string("tune", "animation")
                        }
                        if (tune_backend == "libaom") {
                            tune_aom_sim = *sv;
                            break; // 不调 set_parameter_string("tune", <v>)
                        }
                        return fail(pp::tune::unavailable_message(tune_backend, *tune_fact));
                    }
                    se = heif_encoder_set_parameter_string(enc, p.key.c_str(), sv->c_str());
                } else {
                    rejected = "expected integer or string";
                }
                break;
            }
            case ParamType::Bool: {
                const bool *bv = std::get_if<bool>(&it->second);
                if (!bv) {
                    rejected = "expected boolean";
                    break;
                }
                se = heif_encoder_set_parameter_boolean(enc, p.key.c_str(), *bv ? 1 : 0);
                break;
            }
            default: {
                // R-298：原 `case ParamType::Float` 是死分支 —— param_def_from 只可能产出
                // Int/Enum/Bool/String-E 四型（libheif 参数面无浮点型），旧代码还拿 integer
                // setter 去承接浮点（语义两不像）。改为显式拒绝 + 记录：将来出现未支持
                // 类型时不再静默丢参（也不再假装"已接线"）。
                rejected = "unsupported parameter type";
                break;
            }
            }
            if (rejected.empty() && se.code != heif_error_Ok)
                rejected = heif_error_text(se);
            if (rejected.empty())
                continue;
            // Specialised setters exist for two semantic parameters; use them as fallback so a
            // plugin that exposes the key but rejects the generic path still honours it.
            bool recovered = false;
            if (p.key == "quality") {
                const int64_t v = param_int(req.target.params, "quality", 90);
                recovered =
                    heif_encoder_set_lossy_quality(enc, static_cast<int>(v)).code == heif_error_Ok;
            } else if (p.key == "lossless") {
                const bool v = param_bool(req.target.params, "lossless", false);
                recovered = heif_encoder_set_lossless(enc, v ? 1 : 0).code == heif_error_Ok;
            }
            if (!recovered)
                // 已知参数被后端拒绝 = 用户显式配置没有生效。旧实现只记 warning 继续跑，
                // 产物与 UI/预设不一致（"选 4:4:4 实际 4:2:0" 一类静默降级）。这里如实失败；
                // 未知键（E9）仍只记日志、不失败（见下方 unrecognised 段）。
                return fail("parameter '" + p.key + "' rejected by backend: " + rejected);
        }

        // W2-T3（C20）tune=animation 近似仿真：插件 tune 保持默认 ssim，在**用户透传之前**
        // 注入 x265 tune=animation 的 still 相关三键（psy-rd/aq-strength/deblock）。用户若在
        // 高级区显式给同键，下面的透传循环后写覆盖（以用户为准）。
        if (tune_animation_sim) {
            struct Injection {
                const char *key;
                const char *value;
            };
            const Injection injections[] = {
                {"x265:psy-rd", "0.4"},
                {"x265:aq-strength", "0.4"},
                {"x265:deblock", "1:1"},
            };
            for (const Injection &injection : injections) {
                const heif_error ie =
                    heif_encoder_set_parameter_string(enc, injection.key, injection.value);
                if (ie.code != heif_error_Ok)
                    return fail(std::string("tune=animation 近似仿真需要内部参数 ") +
                                injection.key + "=" + injection.value + "，插件拒绝：" +
                                heif_error_text(ie));
            }
            note_param("tune=animation is simulated (plugin tune stays ssim; injected "
                       "x265:psy-rd/aq-strength/deblock before user passthrough)",
                       "tune", "animation");
        }
        // W2-T3（C20）libaom 扩展值（ssimulacra2 / vmaf_saliency_map）：插件原生 tune 白名单
        // 不认，改走 aom:tune custom option（libheif aom 插件在全部内置控制之后应用，最后生效）。
        if (!tune_aom_sim.empty()) {
            const heif_error ae =
                heif_encoder_set_parameter_string(enc, "aom:tune", tune_aom_sim.c_str());
            if (ae.code != heif_error_Ok)
                return fail("调优值「" + tune_aom_sim + "」需要内部 aom:tune 通道，插件拒绝：" +
                            heif_error_text(ae));
            note_param("tune value dispatched through internal aom:tune option", "tune",
                       tune_aom_sim);
        }
        // W2-T4（C21）：恒定 QP + 显式 Level = x265 硬拒绝（level.cpp:443-445），旧实现只回
        // 泛化 "subcode 5001"。预检（param_rules X12）已给中文错误；编码器侧仍兜底同一文案，
        // 保证 --dev/矩阵/预设任何入口都不会把这条不可行组合静默带进 x265 open。
        if (format_id_ == "heif" && tune_backend == "x265") {
            const bool has_qp = req.target.params.find("x265:qp") != req.target.params.end();
            if (has_qp && param_int(req.target.params, "x265:level-idc", 0) > 0) {
                return fail("恒定 QP 与显式 Level 不兼容：x265 拒绝 \"Constant QP is "
                            "inconsistent with specifying a decoder level\"。请改用 CRF/质量"
                            "模式，或把 Level（x265:level-idc）设为自动（0）。");
            }
        }

        // R-3xx / M8-B1（docs/m8-encoder-params-and-ssimulacra2-design.md §4.5 C3/C7）：
        // 插件私有点位透传（libheif 的 x265/aom 插件都支持 `plugin:option` 形态的字符串
        // 参数，但**不把它们列进 heif_encoder_list_parameters**）。M8 收口口径：
        //   * **后端作用域**：HEIF/x265 只接受 `x265:`；AVIF/libaom 只接受 `aom:`；
        //     `svt:` 不是合法透传通道（libheif 1.23.5 的 SVT 插件没有该分支，design F6）
        //     ⇒ 其它前缀 = 显式配置错误 → 失败（不再静默丢弃 / 假接线）。
        //   * **目录白名单**：透传键必须在 M8 参数目录里（catalog::has_key）——清单外/禁止
        //     键由预检拦截，编码器侧仍兜底失败，绝不静默。
        //   * **值格式化**：见 passthrough_value_text（bool → "1"/"0"，libaom 只认数字）。
        std::string_view allowed_prefix;
        if (backend_id_ == "x265")
            allowed_prefix = "x265:";
        else if (backend_id_ == "libaom")
            allowed_prefix = "aom:";
        const std::string backend_label =
            backend_id_.empty() ? std::string("(first)") : backend_id_;
        for (const auto &[key, value] : req.target.params) {
            if (key.rfind("__", 0) == 0)
                continue;
            if (is_known(key))
                continue;
            if (!is_passthrough_key(key))
                continue;
            const bool prefix_ok = !allowed_prefix.empty() && key.rfind(allowed_prefix, 0) == 0;
            if (!prefix_ok) {
                std::string msg = "plugin option '";
                msg += key;
                msg += "' is not valid for backend '";
                msg += backend_label;
                msg += "' (";
                msg += format_id_;
                msg += "): HEIF/x265 accepts only 'x265:' options, AVIF/libaom only 'aom:'; "
                       "'svt:' has no passthrough channel";
                return fail(msg);
            }
            if (!catalog::has_key(format_id_, backend_id_, key)) {
                std::string msg = "plugin option '";
                msg += key;
                msg += "' is not in the M8 parameter catalog (未收录参数); see "
                       "docs/m8-param-exposure-review.md";
                return fail(msg);
            }
            const std::string text = passthrough_value_text(value);
            // M15：x265 无损（bLossless ⇒ RC 强制 CQP=4）与显式 decoder level **硬冲突**：
            // enforceLevel 在 determineLevel 之前直接失败（x265 level.cpp:443-445
            // "Constant QP is inconsistent with specifying a decoder level"）。因此无损下
            // level-idc 无法真正下发 —— 跳过下发并**如实告警**（UI/规则层同样给 warn），
            // 不静默假装 Level 生效，也不让"可配置的一项"变成必失败项。
            if (want_lossless && key == "x265:level-idc") {
                note_param("无损模式下 x265 拒绝显式 Level（无损 = 恒定 QP，与 decoder level "
                           "冲突；x265 level.cpp:443-445），该值不下发，码流 Level 固定写 8.5"
                           "（如需指定 Level 请关闭无损）",
                           key, text);
                continue;
            }
            const heif_error pe = heif_encoder_set_parameter_string(enc, key.c_str(), text.c_str());
            if (pe.code != heif_error_Ok) {
                std::string msg = "plugin option '";
                msg += key;
                msg += "' rejected by backend: ";
                msg += heif_error_text(pe);
                return fail(msg);
            }
        }

        // R-307（0.3.6-alpha1）：无损开关**显式消费** `lossless` / `__lossless` 两键。
        // 旧实现只依赖内省参数表里恰好出现 "lossless" 这一条；dev harness 的
        // `--lossless` 只写内部键 `__lossless`（静态格式表里 heif 的 techs 为空，
        // apply_locks 无法补出显式键）→ `--lossless` 对 HEIF **完全无效**（产物与有损
        // 逐字节相同），GUI 走内省表单时才有值。这里统一走 lossless_flag 单源并直接
        // 调 libheif 的无损 setter（插件缺失该能力时报 warning，不静默改语义）。
        if (want_lossless && format_id_ == "avif" && backend_id_ == "svt-av1") {
            return fail("avif: svt-av1 has no lossless path (its plugin exposes no chroma and "
                        "supports 4:2:0 only); choose backend libaom");
        }
        {
            const heif_error le = heif_encoder_set_lossless(enc, want_lossless ? 1 : 0);
            if (le.code != heif_error_Ok && want_lossless)
                return fail("lossless requested but backend rejected it: " + heif_error_text(le));
        }
        // 无损 + 非 4:4:4：如实警告（不静默假装像素无损）。
        //   * 4:2:0/4:2:2 的色度抽样发生在编码之前，x265 的 bLossless 只保证**输入 YCbCr
        //     平面**无损，对 RGB 源仍有色度损失；
        //   * 4:4:4 = 当前产品口径下的像素级无损路径（RGB↔YCbCr 往返仍有舍入，见规则 G3）。
        //   * chroma 值由上面的参数循环/平面布局取用，本段不再改写（M15：用户可配置）。
        {
            const std::string requested_chroma = param_str(req.target.params, "chroma", "420");
            if (want_lossless && requested_chroma != "444")
                note_param("无损模式 + 色度 " + requested_chroma +
                               "：色度在编码前被下采样，YCbCr 码流无损但不等于像素级无损"
                               "（需要像素级无损请把色度采样设为 4:4:4）",
                           "chroma", requested_chroma);
        }

        // —— E3 线程映射（§3.1 正文，W1-T7 落地）——
        //   §3.1：heif/avif = 插件 `threads` 参数（内省名核对，缺则记 R4 口径）。
        //   内省核对结果（本机实测，tools/pp_linkprobe.cpp 的内省面 `check_heif_params`，
        //   命令与逐字输出见 T7 自检/m4-report）：
        //     * HEVC（x265 4.3-vcpkg）参数表 = quality|lossless|preset|tune|tu-intra-depth|
        //       complexity|chroma —— **无 `threads`** → E 不可映射，后端保留其默认线程池
        //       （如实记录，R4 口径；NOT-perf 注：无更好选项）。
        //     * AV1（SVT-AV1 4.2.0）参数表 = speed|threads|tile-rows|tile-cols|quality|lossless|
        //       qp|min-q|max-q|tune —— **有 `threads`** → 映射 E（E==1 → 1，与 0.2 逐字一致）。
        //   E3 契约：x265 后端（HEVC）无法受 `encode_threads` 约束 → 该后端的实际并发不入
        //   §8.2 的 Σ 计数（缺口已如实记录；libaom/SVT/JXL 三个生效面覆盖其余格式）。
        // W1-T6（§7.3 口径）：heif/avif = **合成进度面** —— 本编码器不调用 `req.progress`，
        // `EncodeResult::progress_reported` 保持 false；pipeline 的 ProgressMux 按 k[heif]/k[avif]
        // 时长估算出 `synthetic=true` 的进度（§7.3 表：WebP/HEIF/AVIF 无编码回调）。
        // 如实记录（libheif 1.23.5 头文件核对）：编码面（libheif/heif.h）无任何进度回调；
        // 仅 libheif/heif_decoding.h 的**解码插件**接口有 progress 钩子（不适用于编码）。
        (void)req.progress;
        if (is_known("threads")) {
            const int want_threads = req.encode_threads >= 1 ? req.encode_threads : 1;
            const heif_error te = heif_encoder_set_parameter_integer(enc, "threads", want_threads);
            if (te.code != heif_error_Ok)
                note_param("could not set threads: " + heif_error_text(te), "threads",
                           std::to_string(want_threads));
        } else {
            log_info(kStage, kFile, "backend exposes no 'threads' parameter; default pool kept",
                     {{"format", format_id_},
                      {"backend", backend_id_},
                      {"encode_threads", std::to_string(req.encode_threads)}});
        }

        // E9: unrecognised keys are ignored (never an error) and logged, not warned.
        // M8-B1：插件透传前缀键已在上面按作用域/目录校验并下发（合法则已生效、非法已失败），
        // 不再落 "unrecognised" 日志（旧实现会同时记一条误导性的忽略日志）。
        for (const auto &[key, value] : req.target.params) {
            if (key.rfind("__", 0) == 0)
                continue; // reserved keys (§3.4)
            // W2-T1（C16）：HEIF grid/tile 是目录普通键，但不是插件透传参数；由上方
            // grid 规划段消费，不能落 "unrecognised" 日志（否则每次编码都误报）。
            if (key == "heif_grid" || key == "heif_tile_w" || key == "heif_tile_h")
                continue;
            if (is_passthrough_key(key))
                continue; // handled above (scope + catalog enforced)
            if (!is_known(key))
                note_param("unrecognised parameter ignored", key, enc::param_value_text(value));
        }

        // ---- chroma layout ----
        // M15：无损不再特判 4:4:4 —— 平面布局与编码器参数都取用户值（与有损同一路径）。
        // 缺省 420 是 HEVC 的主流兼容面，显式选 444/422 时按真实平面布局编码；无损 + 非 444
        // 的"色度抽样有损"已在上方如实警告。
        ChromaLayout layout = layout_for(heif_chroma_420);
        if (is_known("chroma")) {
            const std::string c = param_str(req.target.params, "chroma", "420");
            if (c == "444")
                layout = layout_for(heif_chroma_444);
            else if (c == "422")
                layout = layout_for(heif_chroma_422);
            else if (c == "420")
                layout = layout_for(heif_chroma_420);
            else
                note_param("unknown chroma value, using 420", "chroma", c);
        } else {
            const std::string c = param_str(req.target.params, "chroma", "");
            if (!c.empty() && c != "420")
                note_param("backend exposes no chroma parameter; forced 420", "chroma", c);
        }

        const int bitdepth = req.target.out_bitdepth;
        const int maxv = (1 << bitdepth) - 1;
        const int cw = (w + layout.cw_off) / layout.cw_div;
        const int ch = (h + layout.ch_off) / layout.ch_div;
        const bool has_alpha = (nch == 2 || nch == 4);

        // ---- I4 / M8 收口：编码档位 = 真实 HEVC 静图信号 ----
        // 旧实现用 derive_heif_profile 只按位深推导：10 位 4:2:0 被标成 Main10，但 x265
        // 静止图实际写 general_profile_idc=4（Main 10 Intra）—— 档位名与产物不符，用户也无法
        // 选择真正的 Main10（idc=2）。现口径（codecs/heif_profile.h 单源）：
        //   * 档位 → 期望 (profile_idc, chroma, bit_depth)（heif_profile_signal）；
        //   * 非原生信号（Main idc=1 / Main10 idc=2 / Rext420_8 idc=4）由
        //     heif_x265_signal_params 在**用户透传循环之后**下发内部 x265 参数（后写覆盖先写）；
        //   * Auto = 插件原生行为，不注入任何参数；
        //   * 编码后用 hvcC 回读校验 idc/chroma/bitdepth/level，名不副实即失败。
        // 本输出恒为静止图（still = true）。
        const int chroma_idx = (layout.chroma == heif_chroma_444)   ? 3
                               : (layout.chroma == heif_chroma_422) ? 2
                               : (layout.chroma == heif_chroma_420) ? 1
                                                                    : 0;
        const bool avif = (format_id_ == "avif");
        HeifSignalSpec expected_signal{};
        bool have_expected_signal = false;
        HeifProfile effective_profile = HeifProfile::Auto;
        int requested_level_idc = 0;
        {
            const auto profile_it = req.target.params.find(std::string(kHeifProfileParamKey));
            if (profile_it != req.target.params.end() &&
                std::get_if<int64_t>(&profile_it->second) == nullptr)
                return fail("编码档位参数 \"__heif_profile\" 类型错误：应为整数枚举值。");
            HeifProfile want_v = HeifProfile::Auto;
            if (!heif_profile_from_int(param_int(req.target.params, kHeifProfileParamKey, 0),
                                       &want_v))
                return fail("编码档位参数 \"__heif_profile\" 越界：合法值 0..15。");
            std::string why;
            if (!heif_profile_compatible_for(avif, want_v, bitdepth, chroma_idx, /*still=*/true,
                                             &why))
                return fail(why);
            effective_profile = want_v == HeifProfile::Auto
                                    ? heif_derive_signal_profile(avif, bitdepth, chroma_idx, true)
                                    : want_v;
            if (!heif_profile_signal_for(avif, effective_profile, &expected_signal))
                return fail("无法解析编码档位「" + heif_profile_label_for(avif, effective_profile) +
                            "」");
            if (expected_signal.chroma < 0)
                expected_signal.chroma = chroma_idx; // Main12 兼容别名：色度随输入
            have_expected_signal = true;
            if (!avif) {
                const HeifX265SignalParams extra = heif_x265_signal_params(want_v);
                for (int i = 0; i < extra.count; ++i) {
                    const heif_error se = heif_encoder_set_parameter_string(enc, extra.items[i].key,
                                                                            extra.items[i].value);
                    if (se.code != heif_error_Ok)
                        return fail("编码档位「" + heif_profile_label(want_v) +
                                    "」需要内部 x265 参数 " + extra.items[i].key + "=" +
                                    extra.items[i].value + "，插件拒绝：" + heif_error_text(se));
                    const auto user_it = req.target.params.find(std::string(extra.items[i].key));
                    if (user_it != req.target.params.end() &&
                        passthrough_value_text(user_it->second) != extra.items[i].value)
                        note_param("internal signal parameter wins over user value",
                                   extra.items[i].key,
                                   passthrough_value_text(user_it->second) + " -> " +
                                       extra.items[i].value);
                }
            }
            log_info(kStage, kFile, "heif profile signal",
                     {{"format", format_id_},
                      {"backend", backend_id_},
                      {"profile", heif_profile_label_for(avif, effective_profile)},
                      {"profile_idc", std::to_string(expected_signal.profile_idc)},
                      {"bitdepth", std::to_string(bitdepth)},
                      {"chroma", heif_chroma_text(chroma_idx)}});
        }

        // ---- x265 Level 预校验 + 有损 HEIF grid 规划（M9 grid-heif / W2-T1 用户面）----
        // 用户面普通键（C16）：
        //   heif_grid    0 = 关闭；1 = 自动（缺省）；2 = 总是 grid
        //   heif_tile_w / heif_tile_h  0 = 自动；手动值 8 向下对齐（64–8192）
        // 兼容读别名 __heif_grid（M9 保留键）保持内部编号 0=关闭 / 1=总是 / 2=自动；
        // 新键存在时优先于旧键（旧预设/--dev 仍生效，两个都给时记一条日志）。
        // x265 的 enforceLevel 会拒绝 luma 样本数超过 Level MaxLumaPs 的图，但旧实现只在
        // “heif_context_encode_image failed” 里带出 x265 日志。这里先用 heif_plan_grid(...) 做
        // Level 拟合：grid 时每 tile 单独满足 Level（tile 尺寸由 plan 给出），只有最终仍是
        // 单图时才返回中文可操作错误（点名样本数、Level 上限与最小可行 Level）。
        // 注意：无损大图有自己的 2048-tile grid 路径（x265 强制 Level 8.5，语义不同），
        // 有损 grid 规划不接管它（want_lossless 时保持原单图/无损 grid 行为）。
        HeifGridPlan grid_plan{};
        bool lossy_grid = false;
        if (!avif) {
            requested_level_idc =
                static_cast<int>(param_int(req.target.params, "x265:level-idc", 0));
            // 未知 Level（x265 只会在 encoder_open 里报 “specified level N does not exist”）：
            // grid 也救不了，保留准确的中文错误（与旧行为一致）。
            // M15：无损下 level-idc 不下发（见上方透传段的硬冲突说明）⇒ 无效值也不阻断。
            if (!want_lossless && requested_level_idc != 0 &&
                heif_level_max_luma_ps(requested_level_idc) < 0)
                return fail(heif_level_fit_error_zh(requested_level_idc, w, h));
            // M15：无损下 x265 的 determineLevel() 在 bLossless 分支**固定**写 Level 8.5
            // （source/encoder/level.cpp:160-166）；显式 level-idc 因 CQP/level 硬冲突不下发
            // （透传段已 warn）—— 这里再补一条产物口径说明（PTL 恒 8.5）。
            if (want_lossless && requested_level_idc != 0)
                note_param("无损模式：码流 PTL Level 固定写 8.5（x265 determineLevel 的 bLossless "
                           "分支），用户 level-idc 不参与产物 Level",
                           "x265:level-idc", std::to_string(requested_level_idc));

            auto read_int_key = [&](const char *key, int64_t &out) -> std::string {
                const auto it = req.target.params.find(key);
                if (it == req.target.params.end())
                    return {};
                const int64_t *iv = std::get_if<int64_t>(&it->second);
                if (iv == nullptr)
                    return "参数 " + std::string(key) + " 类型错误：应为整数。";
                out = *iv;
                return {};
            };

            const bool has_new_grid =
                req.target.params.find("heif_grid") != req.target.params.end();
            const bool has_legacy_grid =
                req.target.params.find("__heif_grid") != req.target.params.end();
            int internal_grid_mode = 2; // 缺省 = 自动
            int64_t grid_value = 2;
            if (has_new_grid) {
                if (const std::string err = read_int_key("heif_grid", grid_value); !err.empty())
                    return fail(err);
                internal_grid_mode = heif_grid_mode_from_param_value(static_cast<int>(grid_value));
                if (internal_grid_mode < 0)
                    return fail("参数 heif_grid 取值非法：合法值 0=关闭 / 1=自动 / 2=总是。");
                if (has_legacy_grid)
                    note_param("heif_grid wins over legacy __heif_grid", "heif_grid",
                               std::to_string(grid_value));
            } else if (has_legacy_grid) {
                if (const std::string err = read_int_key("__heif_grid", grid_value); !err.empty())
                    return fail(err);
                internal_grid_mode = heif_grid_mode_from_legacy(static_cast<int>(grid_value));
                if (internal_grid_mode < 0)
                    return fail("保留键 __heif_grid 取值非法：合法值 0=关闭 / 1=总是 / 2=自动。");
                log_info(kStage, kFile, "legacy __heif_grid used; prefer heif_grid",
                         {{"value", std::to_string(grid_value)},
                          {"mode", heif_grid_mode_name(internal_grid_mode)}});
            }

            int64_t tile_w_raw = 0;
            int64_t tile_h_raw = 0;
            if (const std::string err = read_int_key("heif_tile_w", tile_w_raw); !err.empty())
                return fail(err);
            if (const std::string err = read_int_key("heif_tile_h", tile_h_raw); !err.empty())
                return fail(err);
            if (tile_w_raw < 0 || tile_w_raw > 8192 || tile_h_raw < 0 || tile_h_raw > 8192)
                return fail("手动 tile 参数越界：heif_tile_w/heif_tile_h 合法范围 0–8192"
                            "（0=自动；非 0 时最小 64，按 8 向下对齐）。");

            HeifGridRequest grid_request;
            grid_request.grid_mode = internal_grid_mode;
            grid_request.level_idc = requested_level_idc;
            grid_request.width = w;
            grid_request.height = h;
            grid_request.chroma = chroma_idx;
            grid_request.tile_width = static_cast<int>(tile_w_raw);
            grid_request.tile_height = static_cast<int>(tile_h_raw);
            // C17'：still 单图档位（MainStill / 16..21）+ 显式 Level>5.1 时规划压到
            // still 互操作上界 5.1（编码 Level 仍是用户值）；profile 用**真实信号**推导后的
            // effective_profile（Auto 8 位 4:2:0 会推导成 Main Still Picture）。
            grid_request.profile = effective_profile;
            grid_plan = heif_plan_grid(grid_request);
            if (!grid_plan.ok)
                return fail(grid_plan.error_zh);
            lossy_grid = grid_plan.use_grid && !want_lossless;
            // M15：无损下 x265 无视 level-idc（PTL 固定 8.5），Level 拟合失败不阻断单图编码
            // —— 只在有损路径上保留"Level 装不下 → 中文错误"的硬校验。
            if (!grid_plan.level_fits_image && !lossy_grid && !want_lossless)
                return fail(heif_level_fit_error_zh(requested_level_idc, w, h));
            if (!grid_plan.level_fits_image && want_lossless)
                log_info(kStage, kFile, "lossless ignores level fit check (x265 writes Level 8.5)",
                         {{"level", heif_level_name(requested_level_idc)},
                          {"image", std::to_string(w) + "x" + std::to_string(h)}});
            if (lossy_grid && has_alpha)
                return fail("HEIF grid 不承载 alpha 通道：请拍平 alpha 或把 heif_grid 设为关闭"
                            "（单图输出）。");
            if (lossy_grid && (grid_plan.tile_width <= 0 || grid_plan.tile_height <= 0 ||
                               grid_plan.cols <= 0 || grid_plan.rows <= 0))
                return fail("HEIF grid 规划失败：目标 Level 下没有可用的 tile 尺寸"
                            "（请提高 x265:level-idc 或关闭 grid）。");
            if (grid_plan.downgraded_single) {
                log_info(kStage, kFile, "heif grid downgraded to single image",
                         {{"reason", grid_plan.note_zh},
                          {"tile", std::to_string(grid_plan.tile_width) + "x" +
                                       std::to_string(grid_plan.tile_height)}});
            }
            if (grid_plan.aligned_width_from > 0 || grid_plan.aligned_height_from > 0) {
                log_info(kStage, kFile, "heif manual tile aligned down to 8",
                         {{"tile_w_in", std::to_string(grid_plan.aligned_width_from)},
                          {"tile_w", std::to_string(grid_plan.tile_width)},
                          {"tile_h_in", std::to_string(grid_plan.aligned_height_from)},
                          {"tile_h", std::to_string(grid_plan.tile_height)}});
            }
            if (lossy_grid && grid_plan.manual_with_auto_level)
                log_info(kStage, kFile, "heif manual tile with automatic level",
                         {{"note", grid_plan.note_zh}});
            // C17'：still 单图档位 + Level>5.1 → 规划被压到 still 互操作上界 5.1（warn 级，
            // 不阻断、不改用户 Level）。单图路径（未分块）也会走到这里，保证警告可见。
            // M15：无损下 x265 的 PTL Level 固定 8.5、规划 Level 不生效 → 不报这条误导性告警。
            if (!want_lossless && !grid_plan.warning_zh.empty() && !grid_plan.miaf_parity_warn) {
                log_warn(kStage, kFile, "heif grid warning",
                         {{"level", heif_level_name(requested_level_idc)},
                          {"planning_level", heif_level_name(grid_plan.planning_level_idc)},
                          {"tile", std::to_string(grid_plan.tile_width) + "x" +
                                       std::to_string(grid_plan.tile_height)},
                          {"planned_grid",
                           std::to_string(grid_plan.cols) + "x" + std::to_string(grid_plan.rows)},
                          {"warning", grid_plan.warning_zh}});
            }
            if (lossy_grid && grid_plan.miaf_parity_warn)
                log_warn(kStage, kFile, "heif grid MIAF parity warning",
                         {{"image", std::to_string(w) + "x" + std::to_string(h)},
                          {"chroma", heif_chroma_text(chroma_idx)},
                          {"note", grid_plan.warning_zh}});
            if (lossy_grid) {
                // 规划摘要（需求：每 tile 尺寸/网格/每 tile Level/总 tile 数如实输出）。
                const std::string tile_level =
                    grid_plan.tile_level_idc == 0
                        ? std::string("自动（x265 逐 tile 选择）")
                        : std::string(heif_level_name(grid_plan.tile_level_idc));
                const std::string mode_name = heif_grid_mode_name(internal_grid_mode);
                const std::string tile_text = std::to_string(grid_plan.tile_width) + "x" +
                                              std::to_string(grid_plan.tile_height);
                const std::string grid_text =
                    std::to_string(grid_plan.cols) + "x" + std::to_string(grid_plan.rows);
                const std::string tiles_text = std::to_string(grid_plan.total_tiles());
                if (internal_grid_mode == 2 && grid_plan.level_idc != 0 &&
                    !grid_plan.level_fits_image && !grid_plan.manual_tile) {
                    std::string warn_msg =
                        "Level " + std::string(heif_level_name(requested_level_idc)) +
                        " 无法容纳 " + std::to_string(w) + "x" + std::to_string(h) + "（" +
                        std::to_string(static_cast<int64_t>(w) * static_cast<int64_t>(h)) +
                        " 亮度样本），已自动分块为 " + grid_text + " 个 " + tile_text +
                        " tile（每 tile Level " + tile_level + "，共 " + tiles_text + " 次编码）";
                    log_warn(kStage, kFile, warn_msg,
                             {{"level", heif_level_name(requested_level_idc)},
                              {"image", std::to_string(w) + "x" + std::to_string(h)},
                              {"luma_samples",
                               std::to_string(static_cast<int64_t>(w) * static_cast<int64_t>(h))},
                              {"max_luma_ps",
                               std::to_string(heif_level_max_luma_ps(requested_level_idc))},
                              {"tile", tile_text},
                              {"tile_level", tile_level},
                              {"grid", grid_text},
                              {"tiles", tiles_text}});
                } else {
                    log_info(kStage, kFile, "heif grid plan",
                             {{"mode", mode_name},
                              {"image", std::to_string(w) + "x" + std::to_string(h)},
                              {"tile", tile_text},
                              {"tile_level", tile_level},
                              {"grid", grid_text},
                              {"tiles", tiles_text},
                              {"level", heif_level_name(requested_level_idc)}});
                }
            } else if (!want_lossless && !grid_plan.note_zh.empty()) {
                log_info(kStage, kFile, "heif grid note", {{"note", grid_plan.note_zh}});
            }
        }

        // Known upstream limitation (measured with SVT-AV1 4.1.0 through libheif 1.23.1):
        // a >8-bit encode that carries an alpha plane fails inside the SVT plugin AND leaves
        // the heap corrupted, so the very next allocation/free aborts the process (observed:
        // "corrupted size vs. prev_size" in x265/libheif teardown). Refuse the combination up
        // front so callers get a clean EncodeResult error instead of a crash.
        // Isolation probe: .cache/tmp/m1-t7/scratch.cpp ("svtalpha" aborts, "svt", "svt12",
        // "x265alpha" are clean).
        // NOTE(upstream): re-probing and dropping the guard depends on an upstream fix.
        if (format_id_ == "avif" && backend_id_ == "svt-av1" && bitdepth > 8 && has_alpha)
            return fail("svt-av1 does not support 10-bit with alpha; use backend libaom");

        heif_image *img = nullptr;
        e = heif_image_create(w, h, heif_colorspace_YCbCr, layout.chroma, &img);
        if (e.code != heif_error_Ok || !img)
            return fail("heif_image_create failed: " + heif_error_text(e));
        struct ImgGuard {
            heif_image *i;
            ~ImgGuard() {
                if (i)
                    heif_image_release(i);
            }
        } img_guard{img};

        struct PlaneRef {
            heif_channel ch;
            int w, h;
            uint8_t *base = nullptr;
            size_t stride = 0;
        };
        PlaneRef planes[4];
        int nplanes = 0;
        planes[nplanes++] = {heif_channel_Y, w, h, nullptr, 0};
        planes[nplanes++] = {heif_channel_Cb, cw, ch, nullptr, 0};
        planes[nplanes++] = {heif_channel_Cr, cw, ch, nullptr, 0};
        if (has_alpha)
            planes[nplanes++] = {heif_channel_Alpha, w, h, nullptr, 0};
        for (int i = 0; i < nplanes; i++) {
            e = heif_image_add_plane(img, planes[i].ch, planes[i].w, planes[i].h, bitdepth);
            if (e.code != heif_error_Ok) {
                std::string msg = std::string("heif_image_add_plane(") +
                                  std::to_string(static_cast<int>(planes[i].ch)) + ", " +
                                  std::to_string(planes[i].w) + "x" + std::to_string(planes[i].h) +
                                  ", " + std::to_string(bitdepth) +
                                  " bit) failed: " + heif_error_text(e);
                if (bitdepth > 8)
                    msg += "; encoder does not support " + std::to_string(bitdepth) + "-bit input";
                return fail(msg);
            }
            planes[i].base = heif_image_get_plane2(img, planes[i].ch, &planes[i].stride);
            if (!planes[i].base)
                return fail("heif_image_get_plane2 returned no buffer for plane " +
                            std::to_string(static_cast<int>(planes[i].ch)));
        }

        // ---- pixels: float32 RGB(A)/gray(A) -> full-range BT.601 YCbCr ----
        // M4-W5-T19 接线（design §11.2 热路径表第 2/5 行）：像素面从"逐像素 ConstIterator"
        // 改为**逐行 get_pixels**（连续 float 行）+ **pp::simd::interleave**（通道→RGB 展开）+
        // **pp::simd::quantize16**（Y / alpha 平面的量化，唯一运行期分派层）。YCbCr 表达式的
        // 算子顺序**逐字保留**（0.299r + 0.587g + 0.114b 等），故像素语义等价。量化口径从
        // `lround(clamp(x,0,1)·maxv)`（double）换为 simd 的 float 乘加截断 —— 两者仅在乘积落在
        // k+0.5 的 ±1e-5 带内时可能差 1 LSB。
        //
        // M4-W5-T20 修复（T19 回归坐标面）：展开条件原为 `nch < 3`，于是 **nch == 4 的入参**
        // （RGBA 源，或 pipeline 灰度升维后的 (g,g,g,a)）在 stride-4 的 rowf 上仍按 `x * 3`
        // 取 r/g/b → 逐样本错位（实测：Y 53~70% 样本差 ≤ 全量程，Cb/Cr 6~39%），heif/avif
        // 产物像素全错（48MP 级不可接受；实测 PSNR 3.1 dB，修复后与源逐位一致）。
        // 修复口径 = 任何非 3 通道入参都先经 interleave 展开成 3 通道 RGB 面（其契约：nch < 3
        // 灰度复制；nch == 4 取前 3 通道 = 旧口径的 it[0..2]），alpha 仍按原 stride 取。
        // 目击者 = 全语料回归基线（tools/baseline/golden.windows.log 的 heif/avif 字节面：
        // 修复后逐字节复现 T19 之前的基线），金样 20 对不含 4 通道→heif/avif 用例故未覆盖。
        // R-166（M7-WP8/B.7）：chroma 不再整帧驻留（48MP 旧口径 cb_full+cr_full ≈ +384MB）——
        // 按 2×2（420）/ 2×1（422）/ 1×1（444）行组滚动：本 chroma 行所需的 ≤ ch_div 行源行
        // 即时转换进滚动缓冲（w × ch_div × 2 面），降采样累加序（dy→dx）与量化口径逐字不变
        // ⇒ 产物字节逐位相同。行取样走 enc::fetch_row（R-165 单口径 = img.roi() 行窗）。
        std::vector<float> cb_roll(static_cast<size_t>(w) * static_cast<size_t>(layout.ch_div));
        std::vector<float> cr_roll(static_cast<size_t>(w) * static_cast<size_t>(layout.ch_div));
        std::vector<float> rowf(static_cast<size_t>(w) * static_cast<size_t>(nch));
        std::vector<float> rgbf;
        if (nch != 3)
            rgbf.resize(static_cast<size_t>(w) * 3);
        std::vector<float> yrow(w);
        std::vector<float> arow(w);
        std::vector<uint16_t> yq(w);
        std::vector<uint16_t> aq(w);
        // box-downsample the chroma planes (2x2 for 420, 2x1 for 422, none for 444)
        // R-089（M7-WP2）：chroma 平面量化统一走 **pp::simd::quantize16**（与 Y/alpha 同一
        // 舍入口径），逐行量化替代逐样本 `quantize(lround)` —— 平面间不再有 1 LSB 口径差。
        std::vector<float> cbrow(static_cast<size_t>(cw));
        std::vector<float> crrow(static_cast<size_t>(cw));
        std::vector<uint16_t> cbq(static_cast<size_t>(cw));
        std::vector<uint16_t> crq(static_cast<size_t>(cw));
        for (int cy = 0; cy < ch; cy++) {
            // 本 chroma 行组的源行（y 递增 = 与旧逐行序一致）：Y/alpha 平面即写，chroma 值
            // 进滚动缓冲。浮点式子与旧逐字相同。
            int rows_used = 0;
            for (int dy = 0; dy < layout.ch_div; dy++) {
                const int y = cy * layout.ch_div + dy;
                if (y >= h)
                    break;
                // R-163：`EncodeRequest::cancelled` 真实消费 —— 行级提前退出（写盘前无半成品）。
                if (req.cancelled && req.cancelled())
                    return fail("encode cancelled");
                std::string row_err;
                if (!enc::fetch_row(req.img, y, rowf.data(), row_err))
                    return fail("cannot read pixels (row " + std::to_string(y) + "): " + row_err);
                const float *src = rowf.data();
                if (nch != 3) { // 1/2/4 通道 → 3 通道（灰度复制 / 取 R,G,B；T20 修复 nch==4 错位）
                    pp::simd::interleave(rowf.data(), nch, rgbf.data(), 3, static_cast<size_t>(w));
                    src = rgbf.data();
                }
                for (int x = 0; x < w; ++x) {
                    const float r = src[static_cast<size_t>(x) * 3 + 0];
                    const float g = src[static_cast<size_t>(x) * 3 + 1];
                    const float b = src[static_cast<size_t>(x) * 3 + 2];
                    const size_t idx = static_cast<size_t>(dy) * w + x;
                    yrow[static_cast<size_t>(x)] = 0.299f * r + 0.587f * g + 0.114f * b;
                    cb_roll[idx] = -0.168736f * r - 0.331264f * g + 0.5f * b + 0.5f;
                    cr_roll[idx] = 0.5f * r - 0.418688f * g - 0.081312f * b + 0.5f;
                    if (has_alpha)
                        arow[static_cast<size_t>(x)] =
                            rowf[static_cast<size_t>(x) * static_cast<size_t>(nch) +
                                 static_cast<size_t>(nch - 1)];
                }
                pp::simd::quantize16(yrow.data(), yq.data(), static_cast<size_t>(w), maxv);
                for (int x = 0; x < w; ++x)
                    store_sample(planes[0].base, planes[0].stride, bitdepth, x, y, yq[x]);
                if (has_alpha) {
                    pp::simd::quantize16(arow.data(), aq.data(), static_cast<size_t>(w), maxv);
                    for (int x = 0; x < w; ++x)
                        store_sample(planes[3].base, planes[3].stride, bitdepth, x, y, aq[x]);
                }
                rows_used = dy + 1;
            }
            for (int cx = 0; cx < cw; cx++) {
                float scb = 0.0f, scr = 0.0f;
                int n = 0;
                for (int dy = 0; dy < rows_used; dy++) {
                    for (int dx = 0; dx < layout.cw_div; dx++) {
                        const int sx = cx * layout.cw_div + dx;
                        if (sx >= w)
                            break;
                        const size_t idx = static_cast<size_t>(dy) * w + sx;
                        scb += cb_roll[idx];
                        scr += cr_roll[idx];
                        n++;
                    }
                }
                const float inv = n > 0 ? 1.0f / static_cast<float>(n) : 0.0f;
                cbrow[static_cast<size_t>(cx)] = scb * inv;
                crrow[static_cast<size_t>(cx)] = scr * inv;
            }
            pp::simd::quantize16(cbrow.data(), cbq.data(), static_cast<size_t>(cw), maxv);
            pp::simd::quantize16(crrow.data(), crq.data(), static_cast<size_t>(cw), maxv);
            for (int cx = 0; cx < cw; cx++) {
                store_sample(planes[1].base, planes[1].stride, bitdepth, cx, cy,
                             cbq[static_cast<size_t>(cx)]);
                store_sample(planes[2].base, planes[2].stride, bitdepth, cx, cy,
                             crq[static_cast<size_t>(cx)]);
            }
        }

        // ---- colour description: nclx + optional ICC (E5) ----
        // UX-14 A1（M7-WP2）：nclx 的 primaries/TRC 从**实际嵌入的 ICC** 反推（见
        // nclx_for_icc）—— 与 colr 组里的 ICC 同源同义，不再恒写 sRGB；
        // 矩阵系数照旧 BT.601 全范围（上方 YCbCr 变换确实用它）。
        heif_color_profile_nclx nclx{};
        nclx.version = 1;
        const NclxDesc nclx_desc = nclx_for_icc(req.meta.icc_profile);
        nclx.color_primaries = nclx_desc.primaries;
        nclx.transfer_characteristics = nclx_desc.trc;
        nclx.matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_601_6; // matrix used above
        nclx.full_range_flag = 1;
        const heif_error ne = heif_image_set_nclx_color_profile(img, &nclx);
        if (ne.code != heif_error_Ok)
            log_warn(
                kStage, kFile, "heif_image_set_nclx_color_profile failed",
                {{"format", format_id_}, {"backend", backend_id_}, {"error", heif_error_text(ne)}});
        if (!req.meta.icc_profile.empty()) {
            const heif_error ie = heif_image_set_raw_color_profile(
                img, "prof", req.meta.icc_profile.data(), req.meta.icc_profile.size());
            if (ie.code != heif_error_Ok)
                note_meta("ICC profile not embedded: " + heif_error_text(ie));
        }

        // ---- encode ----
        // UX-14 A1 精确码（E1 残余收口，M7-R2A）：显式给 heif_context_encode_image 传
        // heif_encoding_options.output_nclx_profile —— 旧口径只 set image 的 nclx，libheif 的
        // 颜色转换管线在编码时丢它（x265 VUI 链路实测 P3 落 unspecified(2)）；输出 nclx 显式
        // 传入后码流 VUI CICP **精确**跟随 nclx_for_icc 推导值（sRGB→1、P3→12；AdobeRGB 按
        // CICP 表无对应码 → unspecified，不冒充）。所有权（libheif 1.23.5 heif_encoding.cc）：
        // heif_encoding_options_free 只 delete 结构体、不解引用 output_nclx_profile ⇒ 栈上
        // nclx 活过本次 encode 即安全。
        heif_image_handle *handle = nullptr;

        // ---- 大图无损：HEIF 网格（grid）分块编码 ----
        // libde265 的 4:4:4 / 高位深无损路径在 **单图 > 16 Mi 像素（2^24 luma samples）** 时
        // 解码失败（实测：3902x4200=16.39MP 可解，3902x4500=17.56MP 报
        // "Decoding the input data did not give a decompressed image"；10 位下阈值按同一
        // luma 样本数收敛）。这不是 OIIO 的私有行为，而是 libheif→libde265 的解码上限。
        // 标准 HEIF 的 grid 派生图把大图拆成多个独立 HEVC item（每块 <= 2048x2048），
        // 解码器逐块解、libheif 再拼回整图 —— 既保住无损（色度按用户设置，M15 起不再
        // 强制 4:4:4），也保住可打开性。
        // 约束：grid 的每个 tile 必须同尺寸；边缘块用边缘像素填充，grid 的输出尺寸仍写
        // 原图 w×h（libheif 解码时裁掉填充）。alpha 的 grid 承载未在本版验证 → 明确拒绝，
        // 不静默产出打不开的文件。
        const int64_t luma_samples = static_cast<int64_t>(w) * static_cast<int64_t>(h);
        constexpr int64_t kSingleImageLosslessMaxSamples = 16LL * 1024 * 1024;
        constexpr int kGridTileSize = 2048;
        const bool lossless_grid = want_lossless && luma_samples > kSingleImageLosslessMaxSamples;
        if (lossless_grid && has_alpha)
            return fail("HEIF lossless with alpha above 16 MP is not decodable by libde265/OIIO "
                        "(use AVIF/JXL/PNG lossless, or flatten alpha)");
        heif_encoding_options *enc_opts = heif_encoding_options_alloc();
        if (enc_opts != nullptr)
            enc_opts->output_nclx_profile = &nclx;
        if (lossless_grid) {
            const uint32_t cols = static_cast<uint32_t>((w + kGridTileSize - 1) / kGridTileSize);
            const uint32_t rows = static_cast<uint32_t>((h + kGridTileSize - 1) / kGridTileSize);
            heif_image_handle *grid = nullptr;
            e = heif_context_add_grid_image(ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                            cols, rows, enc_opts, &grid);
            if (enc_opts != nullptr)
                heif_encoding_options_free(enc_opts);
            enc_opts = nullptr;
            if (e.code != heif_error_Ok || grid == nullptr)
                return fail("heif_context_add_grid_image failed: " + heif_error_text(e));
            struct GridGuard {
                heif_image_handle *h;
                ~GridGuard() {
                    if (h)
                        heif_image_handle_release(h);
                }
            } grid_guard{grid};
            // M15：tile 的色度面尺寸按 layout（420/422/444）推导，平面布局与无损/有损无关；
            // 每通道从已生成的整图平面按块拷入（边缘 clamp 复制）。
            const int tile_cw = (kGridTileSize + layout.cw_off) / layout.cw_div;
            const int tile_ch = (kGridTileSize + layout.ch_off) / layout.ch_div;
            // 参数顺序与有损 grid 同构（dst_w/dst_h/src_x0/src_y0），交换在编译期无法发现、
            // 运行时会立刻产生接缝，故保留直白签名并附调用点注释（同有损段的 NOLINT 口径）。
            // NOLINTBEGIN(bugprone-easily-swappable-parameters)
            const auto copy_tile_plane = [&](heif_image *tile, heif_channel ch, const PlaneRef &src,
                                             int dst_w, int dst_h, int src_x0, int src_y0) {
                size_t dstride = 0;
                uint8_t *dst = heif_image_get_plane2(tile, ch, &dstride);
                if (dst == nullptr)
                    return false;
                for (int y = 0; y < dst_h; ++y) {
                    const int sy = std::min(src_y0 + y, src.h - 1);
                    for (int x = 0; x < dst_w; ++x) {
                        const int sx = std::min(src_x0 + x, src.w - 1);
                        if (bitdepth > 8) {
                            reinterpret_cast<uint16_t *>(dst +
                                                         static_cast<size_t>(y) * dstride)[x] =
                                reinterpret_cast<const uint16_t *>(
                                    src.base + static_cast<size_t>(sy) * src.stride)[sx];
                        } else {
                            dst[static_cast<size_t>(y) * dstride + x] =
                                src.base[static_cast<size_t>(sy) * src.stride + sx];
                        }
                    }
                }
                return true;
            };
            // NOLINTEND(bugprone-easily-swappable-parameters)
            const heif_channel chans[3] = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
            const int plane_w[3] = {kGridTileSize, tile_cw, tile_cw};
            const int plane_h[3] = {kGridTileSize, tile_ch, tile_ch};
            for (uint32_t ty = 0; ty < rows; ++ty) {
                for (uint32_t tx = 0; tx < cols; ++tx) {
                    if (req.cancelled && req.cancelled()) {
                        if (grid_guard.h != nullptr) {
                            heif_image_handle_release(grid_guard.h);
                            grid_guard.h = nullptr;
                        }
                        return fail("encode cancelled");
                    }
                    heif_image *tile = nullptr;
                    e = heif_image_create(kGridTileSize, kGridTileSize, heif_colorspace_YCbCr,
                                          layout.chroma, &tile);
                    if (e.code != heif_error_Ok || tile == nullptr)
                        return fail("heif_image_create(grid tile) failed: " + heif_error_text(e));
                    struct TileGuard {
                        heif_image *i;
                        ~TileGuard() {
                            if (i)
                                heif_image_release(i);
                        }
                    } tile_guard{tile};
                    bool tile_ok = true;
                    for (int ci = 0; ci < 3 && tile_ok; ++ci) {
                        e = heif_image_add_plane(tile, chans[ci], plane_w[ci], plane_h[ci],
                                                 bitdepth);
                        if (e.code != heif_error_Ok) {
                            tile_ok = false;
                            break;
                        }
                        // 平面坐标（Y 与色度面同尺/半尺由 layout 决定）→ 每 tile 起点按该
                        // 平面自身 tile 尺寸平移。
                        tile_ok = copy_tile_plane(tile, chans[ci], planes[ci], plane_w[ci],
                                                  plane_h[ci], static_cast<int>(tx) * plane_w[ci],
                                                  static_cast<int>(ty) * plane_h[ci]);
                    }
                    if (!tile_ok)
                        return fail("cannot build HEIF lossless grid tile at (" +
                                    std::to_string(tx) + "," + std::to_string(ty) + ")");
                    (void)heif_image_set_nclx_color_profile(tile, &nclx);
                    if (!req.meta.icc_profile.empty())
                        (void)heif_image_set_raw_color_profile(
                            tile, "prof", req.meta.icc_profile.data(), req.meta.icc_profile.size());
                    e = heif_context_add_image_tile(ctx, grid, tx, ty, tile, enc);
                    if (e.code != heif_error_Ok)
                        return fail("heif_context_add_image_tile(" + std::to_string(tx) + "," +
                                    std::to_string(ty) + ") failed: " + heif_error_text(e));
                }
            }
            handle = grid;
            grid_guard.h = nullptr; // 所有权转交 handle_guard（见下）
        } else if (lossy_grid) {
            // ---- M9 grid-heif：有损 HEIF grid（Apple 1024×576 / 自动适配 Level）----
            // 每个 tile 是独立 HEVC 图（HEVC Level 按 tile 计算）；从**已生成的整图 Y/Cb/Cr
            // 平面**按通道切片（不重跑 RGB→YCbCr），边缘 clamp 复制（与 Apple T7/现有无损
            // grid 同口径）。通道切片规则已验证与逐 tile 转换逐字节等价（Phase 1，2026-10-31）：
            //   Y:     起点 (tx*tile_w, ty*tile_h)；
            //   Cb/Cr: 起点 (tx*tile_w/cw_div, ty*tile_h/ch_div)，尺寸为 tile 的色度面尺寸。
            const int tile_w = grid_plan.tile_width;
            const int tile_h = grid_plan.tile_height;
            const uint32_t cols = static_cast<uint32_t>(grid_plan.cols);
            const uint32_t rows = static_cast<uint32_t>(grid_plan.rows);
            const int tile_cw = (tile_w + layout.cw_off) / layout.cw_div;
            const int tile_ch = (tile_h + layout.ch_off) / layout.ch_div;
            log_info(kStage, kFile, "heif lossy grid",
                     {{"image", std::to_string(w) + "x" + std::to_string(h)},
                      {"tile", std::to_string(tile_w) + "x" + std::to_string(tile_h)},
                      {"grid", std::to_string(cols) + "x" + std::to_string(rows)},
                      {"level", heif_level_name(requested_level_idc)}});
            heif_image_handle *grid = nullptr;
            e = heif_context_add_grid_image(ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                            cols, rows, enc_opts, &grid);
            if (enc_opts != nullptr)
                heif_encoding_options_free(enc_opts);
            enc_opts = nullptr;
            if (e.code != heif_error_Ok || grid == nullptr)
                return fail("heif_context_add_grid_image failed: " + heif_error_text(e));
            struct GridGuard {
                heif_image_handle *h;
                ~GridGuard() {
                    if (h)
                        heif_image_handle_release(h);
                }
            } grid_guard{grid};
            // 通道感知 clamp 拷贝：从 planes[] 的对应整图平面取源，越界复制最后一行/列。
            // dst_w/dst_h/src_x0/src_y0 的语义由唯一调用点固定（tile 目标尺寸与平面起点），
            // 相邻 int 交换编译期无法发现但运行时会立即产生接缝，因此保留直白签名并在调用点
            // 附注释。该告警指向 lambda 的参数行（而非声明行），故用成对的 NOLINT 块注释包裹；
            // 首版 NOLINTNEXTLINE 实测仍 +1（clang-tidy 21 报告位置在参数行）。
            // NOLINTBEGIN(bugprone-easily-swappable-parameters)
            const auto copy_tile_plane = [&](heif_image *tile, heif_channel ch, const PlaneRef &src,
                                             int dst_w, int dst_h, int src_x0, int src_y0) {
                size_t dstride = 0;
                uint8_t *dst = heif_image_get_plane2(tile, ch, &dstride);
                if (dst == nullptr)
                    return false;
                for (int y = 0; y < dst_h; ++y) {
                    const int sy = std::min(src_y0 + y, src.h - 1);
                    for (int x = 0; x < dst_w; ++x) {
                        const int sx = std::min(src_x0 + x, src.w - 1);
                        if (bitdepth > 8) {
                            reinterpret_cast<uint16_t *>(dst +
                                                         static_cast<size_t>(y) * dstride)[x] =
                                reinterpret_cast<const uint16_t *>(
                                    src.base + static_cast<size_t>(sy) * src.stride)[sx];
                        } else {
                            dst[static_cast<size_t>(y) * dstride + x] =
                                src.base[static_cast<size_t>(sy) * src.stride + sx];
                        }
                    }
                }
                return true;
            };
            // NOLINTEND(bugprone-easily-swappable-parameters)
            const heif_channel chans[3] = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
            const int plane_w[3] = {tile_w, tile_cw, tile_cw};
            const int plane_h[3] = {tile_h, tile_ch, tile_ch};
            for (uint32_t ty = 0; ty < rows; ++ty) {
                for (uint32_t tx = 0; tx < cols; ++tx) {
                    if (req.cancelled && req.cancelled()) {
                        if (grid_guard.h != nullptr) {
                            heif_image_handle_release(grid_guard.h);
                            grid_guard.h = nullptr;
                        }
                        return fail("encode cancelled");
                    }
                    heif_image *tile = nullptr;
                    e = heif_image_create(tile_w, tile_h, heif_colorspace_YCbCr, layout.chroma,
                                          &tile);
                    if (e.code != heif_error_Ok || tile == nullptr)
                        return fail("heif_image_create(grid tile) failed: " + heif_error_text(e));
                    struct TileGuard {
                        heif_image *i;
                        ~TileGuard() {
                            if (i)
                                heif_image_release(i);
                        }
                    } tile_guard{tile};
                    bool tile_ok = true;
                    for (int ci = 0; ci < 3 && tile_ok; ++ci) {
                        e = heif_image_add_plane(tile, chans[ci], plane_w[ci], plane_h[ci],
                                                 bitdepth);
                        if (e.code != heif_error_Ok) {
                            tile_ok = false;
                            break;
                        }
                        const int div_x = (ci == 0) ? 1 : layout.cw_div;
                        const int div_y = (ci == 0) ? 1 : layout.ch_div;
                        const int src_x0 = static_cast<int>(tx) * tile_w / div_x;
                        const int src_y0 = static_cast<int>(ty) * tile_h / div_y;
                        tile_ok = copy_tile_plane(tile, chans[ci], planes[ci], plane_w[ci],
                                                  plane_h[ci], src_x0, src_y0);
                    }
                    if (!tile_ok)
                        return fail("cannot build HEIF grid tile at (" + std::to_string(tx) + "," +
                                    std::to_string(ty) + ")");
                    (void)heif_image_set_nclx_color_profile(tile, &nclx);
                    if (!req.meta.icc_profile.empty())
                        (void)heif_image_set_raw_color_profile(
                            tile, "prof", req.meta.icc_profile.data(), req.meta.icc_profile.size());
                    e = heif_context_add_image_tile(ctx, grid, tx, ty, tile, enc);
                    if (e.code != heif_error_Ok)
                        return fail("heif_context_add_image_tile(" + std::to_string(tx) + "," +
                                    std::to_string(ty) + ") failed: " + heif_error_text(e));
                }
            }
            handle = grid;
            grid_guard.h = nullptr; // 所有权转交 handle_guard（见下）
        } else {
            e = heif_context_encode_image(ctx, img, enc, enc_opts, &handle);
            if (enc_opts != nullptr)
                heif_encoding_options_free(enc_opts);
            enc_opts = nullptr;
            if (e.code != heif_error_Ok) {
                std::string msg = "heif_context_encode_image failed: " + heif_error_text(e);
                if (format_id_ == "avif" && backend_id_ == "svt-av1" && bitdepth == 10 && has_alpha)
                    msg += " [svt-av1 does not support 10-bit with alpha; use backend libaom]";
                else if (format_id_ == "heif" && bitdepth > 8) {
                    // 旧文案「encoder does not support 10-bit input」是错误归因：本机构建的
                    // x265 4.3 含 8/10/12-bit 多库，10 位失败的真实原因在 libheif/x265 的
                    // subcode（例如 level-idc 与图像尺寸不匹配时是 Encoder_initialization）。
                    if (e.subcode == heif_suberror_Encoder_initialization)
                        msg += " [HEIF 10 位编码失败：x265 拒绝打开编码器（多为 level-idc 与"
                               "图像尺寸不匹配，或内部信号参数被拒）；详见上方 libheif 错误]";
                    else if (e.subcode == heif_suberror_Unsupported_parameter)
                        msg += " [HEIF 10 位编码失败：x265 插件拒绝了某个参数（x265: 前缀）；"
                               "详见上方 libheif 错误]";
                    else
                        msg += " [HEIF 10 位编码失败：详见上方 libheif 错误（不是"
                               "“不支持 10 位输入”）]";
                }
                return fail(msg);
            }
        }
        if (handle == nullptr)
            return fail("encoder produced no image handle");
        struct HandleGuard {
            heif_image_handle *h;
            ~HandleGuard() {
                if (h)
                    heif_image_handle_release(h);
            }
        } handle_guard{handle};

        // R-314（0.3.6-alpha1）：位深**回读校验**放在容器落盘之后（见下方 write 段）——
        // 用户报告"选 10 位、产物仍是 Main(8 位)"；插件在少数组合下会静默降位。写完后
        // 用 libheif 重新打开 tmp 文件读主图位深，与请求不一致即失败，不让"UI 写 10 位、
        // 文件是 8 位"静默通过。（注：对 heif_context_encode_image 刚返回的 handle 调用
        // heif_image_handle_get_luma_bits_per_pixel 在本机 libheif 1.23.5 上会 AV，
        // 所以必须走"落盘后重新 read_from_file"这条路。）

        // ---- metadata (E7: strictly before heif_context_write) ----
        if (!req.meta.exif_blob.empty()) {
            const heif_error me =
                heif_context_add_exif_metadata(ctx, handle, req.meta.exif_blob.data(),
                                               static_cast<int>(req.meta.exif_blob.size()));
            if (me.code != heif_error_Ok)
                note_meta("EXIF metadata dropped: " + heif_error_text(me));
        }
        if (!req.meta.xmp_rdf.empty()) {
            const heif_error me = heif_context_add_XMP_metadata(
                ctx, handle, req.meta.xmp_rdf.data(), static_cast<int>(req.meta.xmp_rdf.size()));
            if (me.code != heif_error_Ok)
                note_meta("XMP metadata dropped: " + heif_error_text(me));
        }

        // ---- write ----
        // R-163：写盘前最后一次取消检查（行级检查见像素段；此处覆盖长 encode 段之后）。
        if (req.cancelled && req.cancelled())
            return fail("encode cancelled");
        // R-003（M7-WP2 接线）：流式产物先写**同目录临时文件**（fsops::atomic_temp_path），
        // 成功后 commit_temp_file 原子替换最终路径；任一步失败 discard_temp_file 清场 ——
        // 不再在最终路径上留半成品、Overwrite 也不再先毁旧好文件。
        struct Sink {
            FILE *f = nullptr;
            uint64_t bytes = 0;
            std::string error;
        } sink;
        const std::filesystem::path tmp_path = atomic_temp_path(req.target.out_path);
        if (tmp_path.empty())
            return fail("cannot derive temp path for: " + req.target.out_path.string());
        sink.f = std::fopen(normalize_extended_path(tmp_path).string().c_str(), "wb");
        if (!sink.f) {
            discard_temp_file(tmp_path);
            return fail("cannot open output file: " + req.target.out_path.string());
        }
        heif_writer writer{};
        writer.writer_api_version = 1;
        writer.write = [](heif_context *, const void *data, size_t size,
                          void *userdata) -> heif_error {
            Sink *s = static_cast<Sink *>(userdata);
            if (std::fwrite(data, 1, size, s->f) != size) {
                s->error = "short write";
                return heif_error{heif_error_Encoding_error, heif_suberror_Unspecified,
                                  "short write"};
            }
            s->bytes += size;
            return kHeifOk;
        };
        e = heif_context_write(ctx, &writer, &sink);
        const int close_rc = std::fclose(sink.f);
        sink.f = nullptr;
        auto fail_tmp = [&](const std::string &msg) {
            discard_temp_file(tmp_path);
            return fail(msg);
        };
        if (e.code != heif_error_Ok)
            return fail_tmp("heif_context_write failed: " + heif_error_text(e));
        if (!sink.error.empty())
            return fail_tmp("write error: " + sink.error);
        if (close_rc != 0)
            return fail_tmp("fclose failed for " + req.target.out_path.string());
        if (sink.bytes == 0)
            return fail_tmp("heif_context_write produced 0 bytes");
        {
            std::error_code ec;
            const uint64_t disk_bytes =
                static_cast<uint64_t>(std::filesystem::file_size(tmp_path, ec));
            if (ec || disk_bytes != sink.bytes)
                return fail_tmp("temp output size mismatch: wrote " + std::to_string(sink.bytes) +
                                ", on disk " + std::to_string(disk_bytes));
            // R-314：容器落盘后回读真实位深（与请求不一致 = 静默降位，必须失败）。
            const int got_bits = heif_file_luma_bits(tmp_path);
            if (got_bits > 0 && got_bits != bitdepth)
                return fail_tmp("encoder produced " + std::to_string(got_bits) + "-bit " +
                                format_id_ + " but " + std::to_string(bitdepth) +
                                "-bit was requested");
            // M8-HEIF：编码档位真实性校验 —— 以产物 hvcC 为准（解码器实际看到的信号），
            // 名不副实（例如选了 Main10 却写 idc=4）必须失败，不能只信日志。
            if (format_id_ == "heif" && have_expected_signal) {
                HeifHvccInfo hvcc;
                if (!heif_parse_hvcc(tmp_path, hvcc))
                    return fail_tmp("编码档位校验失败：产物 hvcC 无法解析（" + hvcc.error + "）");
                if (hvcc.profile_idc != expected_signal.profile_idc)
                    return fail_tmp(
                        "编码档位校验失败：请求「" + heif_profile_label(effective_profile) +
                        "」（profile_idc " + std::to_string(expected_signal.profile_idc) +
                        "），产物 hvcC profile_idc=" + std::to_string(hvcc.profile_idc));
                if (hvcc.chroma_format_idc != chroma_idx)
                    return fail_tmp(
                        "编码档位校验失败：请求色度 " + heif_chroma_text(chroma_idx) +
                        "，产物 hvcC chroma_format_idc=" + std::to_string(hvcc.chroma_format_idc));
                if (hvcc.bit_depth_luma != bitdepth)
                    return fail_tmp(
                        "编码档位校验失败：请求 " + std::to_string(bitdepth) +
                        " 位，产物 hvcC bitDepthLuma=" + std::to_string(hvcc.bit_depth_luma));
                // 无损路径 x265 在 determineLevel 里强制 Level 8.5（general_level_idc=255），
                // 且显式 level-idc 在无损下不下发（CQP/level 硬冲突，见透传段 warn）——与用户
                // level-idc 无关，故跳过 level 比较；profile/chroma/bitdepth 仍校验。
                if (requested_level_idc > 0 && !lossless_grid && !want_lossless &&
                    hvcc.level_idc != heif_level_general_idc(requested_level_idc))
                    return fail_tmp("Level 校验失败：请求 x265:level-idc=" +
                                    std::to_string(requested_level_idc) + "（Level " +
                                    heif_level_name(requested_level_idc) +
                                    "，码流 general_level_idc 应为 " +
                                    std::to_string(heif_level_general_idc(requested_level_idc)) +
                                    "），产物 hvcC level_idc=" + std::to_string(hvcc.level_idc));
                // Level 校验口径：
                //   * 单图 / 有损 grid：产物 hvcC 的 level_idc 必须等于请求 level-idc 映射的
                //     general_level_idc。有损 grid 的每个 tile 都按同一 x265:level-idc 编码，
                //     heif_plan_grid 保证 tile 合法，故共享 hvcC 的 level 即请求值 —— 统一在
                //     上面校验（!lossless_grid && !want_lossless 覆盖单图与有损 grid）。
                //   * 无损 grid / 无损单图：x265 在 determineLevel 里强制 Level 8.5
                //     （general_level_idc=255），与用户 level-idc 无关，故跳过 level 比较；
                //     profile/chroma/bitdepth 仍逐项校验（tile 共享同一 hvcC）。
                //
                // W2-T2（C18）：hvcC **丢 48 位 constraint**（libheif hevc_boxes.cc:628-630
                // 只写 profile_space/tier/idc/compat/level，constraint 字节实测恒 0），所以
                // still 语义只能从 hvcC 内嵌的 SPS NAL 解析（去 00 00 03 防竞争字节）。这里
                // 解析 SPS 的 PTL/DPB 并落日志；对 still 变体档位（16..21）断言码流真的写出了
                // intra=1 && one_picture_only=1，16 号（Main10Still）额外断言 DPB 约束 —— 名不
                // 副实名即失败（不伪造、不静默）。
                HeifSpsPtlInfo sps;
                const bool sps_ok = heif_parse_sps_ptl(tmp_path, sps);
                if (sps_ok) {
                    const auto flag_text = [](int flag) {
                        if (flag < 0)
                            return std::string("n/a");
                        return std::string(flag != 0 ? "1" : "0");
                    };
                    log_info(kStage, kFile, "heif sps ptl",
                             {{"profile", heif_profile_label(effective_profile)},
                              {"idc", std::to_string(sps.profile_idc)},
                              {"level_idc", std::to_string(sps.level_idc)},
                              {"chroma", std::to_string(sps.chroma_format_idc)},
                              {"bitdepth", std::to_string(sps.bit_depth_luma)},
                              {"constraint48", sps.constraint_bits},
                              {"intra", flag_text(sps.intra)},
                              {"one_picture_only", flag_text(sps.one_picture_only)},
                              {"dpb_minus1", std::to_string(sps.sps_max_dec_pic_buffering_minus1)},
                              {"num_reorder_pics", std::to_string(sps.sps_max_num_reorder_pics)}});
                }
                if (const std::string still_err = heif_sps_still_error_zh(effective_profile, sps);
                    !still_err.empty())
                    return fail_tmp(still_err);
                if (!sps_ok)
                    log_warn(kStage, kFile, "heif sps ptl unparsable", {{"error", sps.error}});
            }
            std::string cerr;
            if (!commit_temp_file(tmp_path, req.target.out_path, &cerr))
                return fail_tmp("commit failed: " + cerr);
        }

        res.bytes = sink.bytes;
        log_debug(kStage, kFile, "encoded",
                  {{"format", format_id_},
                   {"backend", backend_id_},
                   {"size", std::to_string(w) + "x" + std::to_string(h)},
                   {"channels", std::to_string(nch)},
                   {"bitdepth", std::to_string(bitdepth)},
                   {"bytes", std::to_string(res.bytes)}});
        return finish();
    } catch (const std::exception &ex) {
        return fail(std::string("exception: ") + ex.what());
    } catch (...) {
        return fail("unknown exception");
    }
}

// ---------------------------------------------------------------------------
// factories + static registration (§3.17)
// ---------------------------------------------------------------------------

std::unique_ptr<IEncoder> make_heif_x265() { return std::make_unique<HeifEncoder>("heif", "x265"); }
std::unique_ptr<IEncoder> make_avif_svt() {
    return std::make_unique<HeifEncoder>("avif", "svt-av1");
}
std::unique_ptr<IEncoder> make_avif_aom() {
    return std::make_unique<HeifEncoder>("avif", "libaom");
}

} // namespace

// Static self-registration (§3.17). The macro pastes the factory name, so it needs
// unqualified access to the anonymous-namespace factories above -> expand inside namespace pp.
PP_REGISTER_ENCODER("heif", "x265", make_heif_x265);
PP_REGISTER_ENCODER("avif", "svt-av1", make_avif_svt);
PP_REGISTER_ENCODER("avif", "libaom", make_avif_aom);

// No link anchors here (M2-T13 起即如此；R-051/M7-WP2 起其余编码器的锚点轨已整体删除):
// self-registration is guaranteed by the link form, not by referenced symbols — every
// consumer links pp_core whole-archive via the pp_core_registered interface target
// (CMakeLists §2b), so both T7 TUs register unconditionally. 注册缺失的诊断由
// encoders.cpp 的期望键断言承担（R-051）。

namespace {

// ---------------------------------------------------------------------------
// 10-bit capability probe (R19) — real encode + decode round trip on a 64x64 image
// ---------------------------------------------------------------------------

heif_error probe_sink_write(heif_context *, const void *data, size_t size, void *userdata) {
    auto *blob = static_cast<std::vector<uint8_t> *>(userdata);
    blob->insert(blob->end(), static_cast<const uint8_t *>(data),
                 static_cast<const uint8_t *>(data) + size);
    return kHeifOk;
}

// Returns true when a 64x64 image with `bitdepth`-bit planes encodes AND decodes back with
// the same per-channel bit depth.
bool bitdepth_roundtrip_ok(heif_compression_format fmt, std::string_view backend_id, int bitdepth,
                           std::string &why) {
    const heif_encoder_descriptor *desc = find_descriptor(fmt, backend_id);
    if (!desc) {
        why = "no encoder descriptor";
        return false;
    }
    heif_context *ctx = heif_context_alloc();
    if (!ctx) {
        why = "heif_context_alloc failed";
        return false;
    }
    heif_encoder *enc = nullptr;
    heif_error e = heif_context_get_encoder(ctx, desc, &enc);
    if (e.code != heif_error_Ok || !enc) {
        why = "get_encoder: " + heif_error_text(e);
        heif_context_free(ctx);
        return false;
    }
    const int W = 64, H = 64, cw = 32, ch = 32, maxv = (1 << bitdepth) - 1;
    heif_image *img = nullptr;
    bool ok = false;
    std::vector<uint8_t> blob;
    heif_image_handle *handle = nullptr;
    heif_context *rctx = nullptr;
    heif_image_handle *rhandle = nullptr;
    heif_image *decoded = nullptr;
    do {
        e = heif_image_create(W, H, heif_colorspace_YCbCr, heif_chroma_420, &img);
        if (e.code != heif_error_Ok) {
            why = "image_create: " + heif_error_text(e);
            break;
        }
        const heif_channel chans[3] = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
        const int ws[3] = {W, cw, cw}, hs[3] = {H, ch, ch};
        bool planes_ok = true;
        for (int i = 0; i < 3; i++) {
            e = heif_image_add_plane(img, chans[i], ws[i], hs[i], bitdepth);
            if (e.code != heif_error_Ok) {
                why = "add_plane " + std::to_string(bitdepth) + "-bit: " + heif_error_text(e);
                planes_ok = false;
                break;
            }
            size_t stride = 0;
            uint8_t *base = heif_image_get_plane2(img, chans[i], &stride);
            if (!base) {
                why = "no plane buffer";
                planes_ok = false;
                break;
            }
            for (int y = 0; y < hs[i]; y++)
                for (int x = 0; x < ws[i]; x++)
                    store_sample(base, stride, bitdepth, x, y, (x * maxv) / (ws[i] - 1));
        }
        if (!planes_ok)
            break;
        heif_color_profile_nclx nclx{};
        nclx.version = 1;
        nclx.color_primaries = heif_color_primaries_ITU_R_BT_709_5;
        nclx.transfer_characteristics = heif_transfer_characteristic_IEC_61966_2_1;
        nclx.matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_601_6;
        nclx.full_range_flag = 1;
        heif_image_set_nclx_color_profile(img, &nclx);
        (void)heif_encoder_set_lossy_quality(enc, 90); // best effort quality baseline
        e = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
        if (e.code != heif_error_Ok) {
            why = "encode: " + heif_error_text(e);
            break;
        }
        heif_writer writer{};
        writer.writer_api_version = 1;
        writer.write = probe_sink_write;
        e = heif_context_write(ctx, &writer, &blob);
        if (e.code != heif_error_Ok || blob.empty()) {
            why = "write: " + heif_error_text(e);
            break;
        }
        // read back
        rctx = heif_context_alloc();
        if (!rctx) {
            why = "readback context alloc failed";
            break;
        }
        e = heif_context_read_from_memory_without_copy(rctx, blob.data(), blob.size(), nullptr);
        if (e.code != heif_error_Ok) {
            why = "readback: " + heif_error_text(e);
            break;
        }
        e = heif_context_get_primary_image_handle(rctx, &rhandle);
        if (e.code != heif_error_Ok) {
            why = "readback handle: " + heif_error_text(e);
            break;
        }
        e = heif_decode_image(rhandle, &decoded, heif_colorspace_undefined, heif_chroma_undefined,
                              nullptr);
        if (e.code != heif_error_Ok) {
            why = "decode: " + heif_error_text(e);
            break;
        }
        const int got = heif_image_get_bits_per_pixel_range(decoded, heif_channel_Y);
        if (got != bitdepth) {
            why = "decoded bit depth " + std::to_string(got) + " != " + std::to_string(bitdepth);
            break;
        }
        ok = true;
    } while (false);

    if (decoded)
        heif_image_release(decoded);
    if (rhandle)
        heif_image_handle_release(rhandle);
    if (rctx)
        heif_context_free(rctx);
    if (handle)
        heif_image_handle_release(handle);
    if (img)
        heif_image_release(img);
    heif_encoder_release(enc);
    heif_context_free(ctx);
    return ok;
}

std::mutex &probe_mutex() {
    static std::mutex m;
    return m;
}
// R-305（M7-WP2）：probe 缓存**失效策略显式化** ——
//   * 有效范围 = 进程生命周期：编码插件集在 libheif 加载后不再变化（插件静态链接 /
//     插件目录在启动时扫描一次），同一 (format, backend) 的位深能力是确定值；
//   * 键 = "format/backend"，值 = 探测结论（"8"|"8,10"|"8,10,12"）；同进程内并发只读 +
//     首次双检写入（probe_mutex）；
//   * 如需在插件热替换后重探（当前无此产品能力），调用方应走新进程 —— 不提供隐式 TTL。
std::map<std::string, std::string> &probe_cache() {
    static std::map<std::string, std::string> c;
    return c;
}

} // namespace

std::vector<BackendDef> introspect_backends(std::string_view format_id) {
    std::vector<BackendDef> out;
    heif_compression_format fmt = heif_compression_HEVC;
    if (!libheif_format(format_id, fmt))
        return out; // non-libheif -> empty

    for (const heif_encoder_descriptor *desc : enumerate_descriptors(fmt)) {
        BackendDef b;
        const char *id_name = heif_encoder_descriptor_get_id_name(desc);
        const char *long_name = heif_encoder_descriptor_get_name(desc);
        b.id = canonical_backend_id(id_name ? id_name : "");
        b.label = (long_name && *long_name) ? long_name : b.id;
        b.runtime_introspected = true;

        heif_context *ctx = heif_context_alloc();
        heif_encoder *enc = nullptr;
        if (ctx && heif_context_get_encoder(ctx, desc, &enc).code == heif_error_Ok && enc) {
            TechDef t;
            t.id = "runtime"; // single synthetic tech: params come from the codec plugin
            t.label = b.label;
            t.lossless_capable = heif_encoder_descriptor_supports_lossless_compression(desc) != 0;
            t.params = introspect_live(enc);
            // M8-B0：目录行并入运行时参数面（UI/预设/--param/校验共用同一份表）。
            std::vector<ParamDef> extra = catalog::param_defs(format_id, b.id);
            // W2-T1（C16）：Level 行上移核心区（目录生成物统一 advanced=true；核心定位在
            // 运行时合并点按 heif_param_in_core 单源改写，catalog.h 的加性字段由 W2-T6 收口）。
            for (ParamDef &p : extra) {
                if (heif_param_in_core(p.key))
                    p.advanced = false;
            }
            t.params.insert(t.params.end(), extra.begin(), extra.end());
            b.techs.push_back(std::move(t));
            heif_encoder_release(enc);
        } else {
            log_warn(kStage, kFile, "could not instantiate encoder for introspection",
                     {{"format", format_id}, {"backend", b.id}});
        }
        if (ctx)
            heif_context_free(ctx);
        out.push_back(std::move(b));
    }
    return out;
}

std::string probe_bitdepth_support(std::string_view format_id, std::string_view backend_id) {
    heif_compression_format fmt = heif_compression_HEVC;
    if (!libheif_format(format_id, fmt))
        return {};
    const std::string key = std::string(format_id) + "/" + std::string(backend_id);
    {
        std::lock_guard<std::mutex> lock(probe_mutex());
        const auto it = probe_cache().find(key);
        if (it != probe_cache().end())
            return it->second;
    }
    std::string result;
    std::string detail;
    for (const int depth : {8, 10, 12}) {
        std::string why;
        const bool ok = bitdepth_roundtrip_ok(fmt, backend_id, depth, why);
        if (ok) {
            if (!result.empty())
                result += ",";
            result += std::to_string(depth);
        } else {
            detail += (detail.empty() ? "" : "; ") + std::to_string(depth) + "bit: " + why;
        }
    }
    if (result.empty()) {
        // The contract only allows "8" / "8,10" / "8,10,12": a backend that cannot even do
        // 8-bit is a broken environment -> report the minimum and shout in the log.
        result = "8";
        log_error(kStage, kFile, "8-bit probe failed; reporting minimum",
                  {{"format", format_id}, {"backend", backend_id}, {"detail", detail}});
    } else {
        log_info(kStage, kFile, "bit depth probe",
                 {{"format", format_id},
                  {"backend", backend_id},
                  {"supported", result},
                  {"unsupported", detail}});
    }
    {
        std::lock_guard<std::mutex> lock(probe_mutex());
        probe_cache()[key] = result;
    }
    return result;
}

} // namespace pp
