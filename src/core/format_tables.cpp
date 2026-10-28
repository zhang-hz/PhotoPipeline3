// PP-FROZEN(structure): 结构冻结面 —— **参数事实的骨架逐字不变**：既有 ParamDef 的
//   key/type/def/lo/hi/choices、FormatDef 的 id/label/ext/bitdepths/supports_*/meta_path
//   全表不改（0.3.0/M4-T13 追认新增 `lossless_param()` 显式无损参数行，见下方
//   PP-FROZEN(0.3.0) 块）。可改面（历史口径）= `visible`/`locked` 谓词（M1-T2 填写面）与
//   `tooltip` 文案（事实出处/禁用原因注记）。
//   R-293（M7-WP2 勘误）：旧头部写"只允许替换 /* PP-PLACEHOLDER */ 注释处的内容"，但全文件
//   **0 处 PP-PLACEHOLDER**（锚点失效、形同虚设）。现改为上段的**枚举式口径**（什么可改、
//   什么不可改），并按此执行：M7-WP2 的改动 = tooltip 禁用原因/边界出处 + locked 谓词
//   （R-244/245/246/247/011）+ 工厂函数去重（R-209，行值逐字保留）+ bmp 位深口径（R-090，
//   见该行注释），逐条登记 build/m7-wp2/handoff.md 裁定登记。
//   对照见 docs/m4-report.md 勘误 n 与 docs/m1-tasks.md §3.4。
// SPDX-License-Identifier: GPL-3.0-or-later
#include "codecs/param_catalog/catalog.h" // M8-B0：JXL 目录行并入静态表
#include "core/params.h"

namespace pp {

namespace {

// —— 谓词辅助（M1-T2；规则见 docs/m1-tasks.md §4.2）——
// PP-FROZEN(0.3.0)：保留键 "__lossless" = 无损的**内部管道键**（T13 定稿保留，见 params.h）；
// 无损开关由参数引擎写入 ParamSet（见 src/core/params.cpp）。
// 0.3.0（M4-T13）：无损另有**显式 schema 参数**（key = "lossless"，Bool，见 params.h 的
// kLosslessParamKey 与 docs/param-catalog.md）。两键由引擎同步（apply_locks 是唯一写入点）；
// 谓词仍读内部管道键 —— 单实现、零行为漂移（0.2 的谓词语义逐字不变）。
bool is_lossless(const ParamSet &s) {
    // R-295：键名走 kLosslessKey 单源（原裸字面量 "__lossless" 与 params.h 常量 3:3 分裂）。
    return param_bool(s, kLosslessKey, false);
}
bool not_lossless(const ParamSet &s) { return !is_lossless(s); }

// JPEG-03：psnr_target>0 时 jpegli_set_psnr 覆盖 quality_mode/distance/quality，
// 这三个控件不再生效 → 隐藏，避免"可调但被覆盖"。
bool jpeg_psnr_off(const ParamSet &s) { return param_float(s, "psnr_target", 0.0) <= 0.0; }
bool quality_mode_is_distance(const ParamSet &s) {
    return jpeg_psnr_off(s) && param_str(s, "quality_mode", "distance") == "distance";
}
bool quality_mode_is_quality(const ParamSet &s) {
    return jpeg_psnr_off(s) && param_str(s, "quality_mode", "distance") == "quality";
}

bool compression_is_zip(const ParamSet &s) { return param_str(s, "compression", "lzw") == "zip"; }
bool compression_is_lzw_or_zip(const ParamSet &s) {
    const std::string c = param_str(s, "compression", "lzw");
    return c == "lzw" || c == "zip";
}

std::optional<ParamValue> lock_true_when_progressive(const ParamSet &s) {
    if (param_bool(s, "progressive", true))
        return ParamValue{true};
    return std::nullopt;
}
std::optional<ParamValue> lock_zero_when_lossless(const ParamSet &s) {
    if (is_lossless(s))
        return ParamValue{0.0};
    return std::nullopt;
}
std::optional<ParamValue> lock_one_when_lossless(const ParamSet &s) {
    if (is_lossless(s))
        return ParamValue{int64_t(1)};
    return std::nullopt;
}
bool never_visible(const ParamSet &) { return false; }
std::optional<ParamValue> lock_false_when_lossless(const ParamSet &s) {
    if (is_lossless(s))
        return ParamValue{false};
    return std::nullopt;
}

// —— R-244/245/246（M7-WP2）：JPEG 三个"假控件"的锁定谓词（数据驱动禁用态，不改 UI 代码）——
// 语义：locked 谓词恒返回强制值 ⇒ 参数引擎 apply_locks 钉值、表单呈禁用态；禁用原因写进
// tooltip（"不静默灰"口径）。三者对默认参数集均零改写（强制值 == 表默认值）⇒ 回归零漂移。
//   * arith_code（R-244）：jpegli 硬拒算术编码（置 true 100% 编码失败）→ 恒锁 false；
//   * xyb_mode（R-245）：tooltip 承诺"需嵌 XYB ICC"但全仓无 XYB ICC 生成/嵌入路径 →
//     恒锁 false（不给出未兑现能力）；
//   * dct_method（R-246）：jpegli 固定浮点 DCT、该字段仅 libjpeg 兼容层保留（装饰性）→
//     恒锁 0（表默认值）。锁 0 而非实际生效的 FLOAT(2)：jpegli 不读该字段（实测行为等价），
//     锁 2 会改写既有参数快照（回归/预设字节漂移），锁 0 零漂移（裁定登记见 handoff）。
std::optional<ParamValue> lock_false_always(const ParamSet &) { return ParamValue{false}; }
std::optional<ParamValue> lock_zero_always(const ParamSet &) { return ParamValue{int64_t(0)}; }

// PP-FROZEN(0.3.0) —— 显式无损参数（0.3.0 / M4-T13，key = pp::kLosslessParamKey）
// 出口硬项「lossless 必须显式暴露」：无损从"只存在于内部管道键"升级为**schema 声明的 Bool 参数**，
// 用户可见可设（输出页格式参数卡顶部的「无损」复选框 = 它的控件）、可序列化（预设 v2 的
// `outputs[i].params.lossless`）、可文档化（docs/param-catalog.md §2.2/§2.3/§5.1/§5.2）。
//   * 声明面：jxl（vardct/modular）与 webp（lossy/lossless）；heif/avif 由 libheif 运行时内省
//     提供同名字段（param-catalog §3/§4.2），enc_heif 直接消费它。
//   * 引擎侧：`apply_locks()` 把本参数与 lossless 入参同步（唯一写入点）；谓词/编码器仍读内部
//     管道键 `__lossless`（单实现、零行为漂移 —— 0.2 输出逐字节不变）。
ParamDef lossless_param(const char *encoder_note) {
    ParamDef p;
    p.key = std::string(kLosslessParamKey);
    p.label = "无损";
    p.type = ParamType::Bool;
    p.def = false;
    p.advanced = false;
    p.group = pp::kGroupCore;
    p.tooltip = std::string("无损输出（0.3.0 显式 schema 参数；由格式参数卡顶部的「无损」复选框"
                            "驱动，本行不单独渲染）。编码器侧：") +
                encoder_note +
                "。内部管道键 __lossless 与本参数由参数引擎同步（src/core/params.cpp）。";
    return p;
}

// —— R-209（M7-WP2）：同文件镜像 ParamDef 去重 —— jxl 五参数×2（effort / decoding_speed /
// codestream_level / distance / resampling / group_order）、webp method·quality×2 的
// 行值改由工厂函数单点声明（表里两处调用同一工厂）。**行值逐字保留**（key/type/def/lo/hi/
// choices 不动；tooltip 仅在 webp quality 两行追加"一键两义"注记，其余逐字不动 ——
// 删重只消灭复制粘贴，不改事实）。
// webp `quality` 一键两义（R-209 附带项）：同一 key 在 lossy 技术下是质量分、在 lossless
// 技术下是压缩努力（WebPConfig::quality 的库语义如此）。key 不拆（拆 = 预设 v2 序列化键
// 变更，破坏既有预设），改由 webp_quality_param(lossless_tech) 在标签/tooltip 上把两义
// 说清（见两行 tooltip 的"一键两义"注记）。
ParamDef jxl_effort_param(bool lossy_only) {
    ParamDef p;
    p.key = "effort";
    p.label = "编码努力";
    p.type = ParamType::Int;
    p.def = int64_t(7);
    p.lo = 1;
    // M8-A17（裁定登记 docs/m8-adjudications.md）：上限 10 → 11。libjxl 0.12 的
    // JXL_ENC_FRAME_SETTING_EFFORT 在 JxlEncoderAllowExpertOptions() 后接受 [1..11]
    // （encode.cc:1667-1686；目录行 expert_options 驱动）；11 = 像素无损最省但极慢。
    // 默认值 7 不动 ⇒ 既有产物零变化；effort=11 需要 expert_options=1，由 B1 预检
    // （param_rules J6）拦截、编码器兜底（JxlEncoderFrameSettingsSetOption 报错）。
    p.hi = 11;
    p.step = 1;
    p.advanced = false;
    p.group = pp::kGroupCore;
    p.tooltip = "编码努力 effort（JXL_ENC_FRAME_SETTING_EFFORT，"
                "jxl/encode.h:126-132）：1 lightning … 7 squirrel"
                "（库默认）… 10 glacier；11 需先启用「允许专家选项」"
                "（JxlEncoderAllowExpertOptions，M8-A17），不影响解码速度。";
    p.visible = lossy_only ? not_lossless : std::function<bool(const ParamSet &)>{};
    return p;
}

ParamDef jxl_decoding_speed_param(bool lossy_only) {
    ParamDef p;
    p.key = "decoding_speed";
    p.label = "解码速度预算";
    p.type = ParamType::Int;
    p.def = int64_t(0);
    p.lo = 0;
    p.hi = 4;
    p.step = 1;
    p.advanced = true;
    p.group = pp::kGroupDecodeBudget;
    p.tooltip = "解码速度预算（JXL_ENC_FRAME_SETTING_DECODING_SPEED，"
                "jxl/encode.h:134-138）：0=最慢解码但密度最高（库默认）… "
                "4=最快解码。";
    p.visible = lossy_only ? not_lossless : std::function<bool(const ParamSet &)>{};
    return p;
}

ParamDef jxl_codestream_level_param(bool lossy_only) {
    ParamDef p;
    p.key = "codestream_level";
    p.label = "码流级别";
    p.type = ParamType::Enum;
    // 第二轮审计 JXL-04：库默认 -1(auto)，显式 10 会给每个产物加 jxll box、无谓收窄兼容性。
    p.def = int64_t(-1);
    p.choices = {{"auto", int64_t(-1)}, {"5", int64_t(5)}, {"10", int64_t(10)}};
    p.advanced = true;
    p.group = pp::kGroupEncodeTools;
    p.tooltip = "JPEG XL 码流级别（JxlEncoderSetCodestreamLevel，"
                "jxl/encode.h:1348-1383）：-1=自动（库默认，按 basic "
                "info 选择）、5=兼容性最广、10=解除限制（CMYK/32bit）；"
                "默认 auto。";
    p.visible = lossy_only ? not_lossless : std::function<bool(const ParamSet &)>{};
    return p;
}

ParamDef jxl_distance_param(bool vardct) {
    ParamDef p;
    p.key = "distance";
    p.label = "视觉距离";
    p.type = ParamType::Float;
    p.advanced = false;
    p.group = pp::kGroupCore;
    if (vardct) {
        p.def = 1.0;
        p.lo = 0.0;
        p.hi = 25.0;
        p.step = 0.1;
        p.tooltip = "目标 Butteraugli 距离（JxlEncoderSetFrameDistance，"
                    "jxl/encode.h:1428-1442）：范围 0–25、库默认 1.0、"
                    "1.0=视觉无损、推荐 0.5–3.0；0 在 VarDCT 下走无损路径，"
                    "请用「无损编码」复选框（JXL-05：0 的两种结局已收口）。";
        p.visible = not_lossless;
    } else {
        p.def = 0.0;
        p.lo = 0.0;
        p.hi = 15.0;
        p.step = 0.1;
        p.tooltip = "Modular 视觉距离（同一 JxlEncoderSetFrameDistance，"
                    "jxl/encode.h:1428-1442）：0=完全无损（需 "
                    "JxlEncoderSetFrameLossless）、>0=有损 Modular；"
                    "catalog 上限 15，库允许 0–25。";
        p.locked = lock_zero_when_lossless;
    }
    return p;
}

ParamDef jxl_resampling_param(bool lossy_only) {
    (void)lossy_only;
    ParamDef p;
    p.key = "resampling";
    p.label = "编码前降采样";
    p.type = ParamType::Enum;
    p.def = int64_t(-1);
    p.choices = {{"auto", int64_t(-1)},
                 {"1", int64_t(1)},
                 {"2", int64_t(2)},
                 {"4", int64_t(4)},
                 {"8", int64_t(8)}};
    p.advanced = true;
    p.group = pp::kGroupEncodeTools;
    p.tooltip = "编码前降采样（JXL_ENC_FRAME_SETTING_RESAMPLING，"
                "jxl/encode.h:140-146）：-1=编码器自选（库默认，低质量时才"
                "降采样）、1=不降采样、2/4/8=按倍数降采样并在解码端升采样；"
                "会降低有效分辨率。VarDCT/Modular 通用；无损时强制 1（JXL-01）。";
    p.visible = not_lossless;
    p.locked = lock_one_when_lossless;
    return p;
}

ParamDef jxl_group_order_param(bool lossy_only) {
    ParamDef p;
    p.key = "group_order";
    p.label = "256×256 组顺序";
    p.type = ParamType::Enum;
    p.def = int64_t(-1);
    p.choices = {{"auto", int64_t(-1)}, {"scanline", int64_t(0)}, {"center", int64_t(1)}};
    p.advanced = true;
    p.group = pp::kGroupEncodeTools;
    p.tooltip = "256×256 区域在码流中的存放顺序"
                "（JXL_ENC_FRAME_SETTING_GROUP_ORDER，jxl/encode.h:210-214）："
                "-1=编码器默认、0=扫描线顺序、1=中心优先（影响渐进渲染），"
                "不改变画质。VarDCT/Modular 通用。";
    p.visible = lossy_only ? not_lossless : std::function<bool(const ParamSet &)>{};
    return p;
}

ParamDef webp_quality_param(bool lossless_tech) {
    ParamDef p;
    p.key = "quality"; // 一键两义（见上方 R-209 注记）：lossy=质量分 / lossless=压缩努力
    p.type = ParamType::Int;
    p.step = 1;
    p.advanced = false;
    p.group = pp::kGroupCore;
    if (lossless_tech) {
        p.label = "压缩努力"; // 80px 标签轨放得下；"（借 quality 参数）" 进 tooltip
        p.def = int64_t(80);
        p.lo = 0;
        p.hi = 100;
        p.tooltip = "无损模式下 quality 表示压缩努力"
                    "（WebPConfig::quality，webp/encode.h:99-103）：0 最快、"
                    "100 最小体积。WebPConfigLosslessPreset(level 6) 给出 "
                    "method 4 + quality 75（src/enc/config_enc.c）。"
                    "【一键两义（R-209）：同一 key 在 lossy 技术下是质量分，此面是努力】";
    } else {
        p.label = "质量";
        p.def = int64_t(90);
        p.lo = 1;
        p.hi = 100;
        p.tooltip = "有损质量（WebPConfig::quality，webp/encode.h:99-103；"
                    "WebPValidateConfig 校验 0–100）：0=最小体积、100=最大。"
                    "库默认 75（webp/encode.h:175-179），本表取 90"
                    "（视觉透明档）。【一键两义（R-209）：同一 key 在 lossless 技术下"
                    "是压缩努力，此面是质量】";
    }
    return p;
}

ParamDef webp_method_param(bool lossless_tech) {
    ParamDef p;
    p.key = "method";
    p.label = "压缩方法";
    p.type = ParamType::Int;
    p.def = int64_t(4);
    p.lo = 0;
    p.hi = 6;
    p.step = 1;
    p.advanced = true;
    p.group = pp::kGroupEncodeTools;
    p.tooltip = lossless_tech ? "质量/速度权衡（WebPConfig::method，webp/encode.h:104）："
                                "0 最快 … 6 最好；WebPConfigLosslessPreset(6) 亦取 4。"
                              : "质量/速度权衡（WebPConfig::method，webp/encode.h:104；"
                                "校验 0–6）：0 最快 … 6 最慢最好，库默认 4。";
    return p;
}

} // namespace

const std::vector<FormatDef> &static_formats() {
    static const std::vector<FormatDef> fmts = [] {
        std::vector<FormatDef> v = {
        FormatDef{
            .id = "jpeg", .label = "JPEG", .ext = "jpg",
            .backends = { BackendDef{
                .id = "jpegli", .label = "jpeg-li", .runtime_introspected = false,
                .techs = { TechDef{
                    .id = "dct", .label = "DCT", .lossless_capable = false,
                    .params = {
                        ParamDef{.key = "quality_mode", .label = "质量控制方式",
                                 .type = ParamType::Enum, .def = std::string("distance"),
                                 .choices = {{"distance", std::string("distance")},
                                             {"quality", std::string("quality")}},
                                 .advanced = false,
                                 .tooltip = "质量控制方式：distance=视觉距离（Butteraugli，jpegli 原生 "
                                            "jpegli_set_distance），quality=libjpeg 兼容质量分"
                                            "（jpegli_set_quality，内部换算为距离）；二选一联动下方控件。"
                                            "psnr_target>0 时被 PSNR 搜索覆盖 → 隐藏。",
                                 .visible = jpeg_psnr_off, .locked = {}, .group = pp::kGroupCore},
                        ParamDef{.key = "distance", .label = "视觉距离",
                                 .type = ParamType::Float, .def = 1.0, .lo = 0.3, .hi = 3.0,
                                 .step = 0.1, .choices = {}, .advanced = false,
                                 .tooltip = "Butteraugli 目标距离（jpegli_set_distance，jpegli/encode.h:119）；"
                                            "1.0=视觉透明；jpegli 参考 CLI 推荐 0.5–3.0、允许 0.0–25.0"
                                            "（tools/cjpegli.cc:53）。本表 [0.3, 3.0] 为 **UI 范围**"
                                            "（R-294 边界出处：库允许 0.0–25.0，表收窄到常用带）。"
                                            "quality_mode=distance 时可见。",
                                 .visible = quality_mode_is_distance, .locked = {}, .group = pp::kGroupCore},
                        ParamDef{.key = "quality", .label = "质量",
                                 .type = ParamType::Int, .def = int64_t(90), .lo = 1, .hi = 100,
                                 .step = 1, .choices = {}, .advanced = false,
                                 .tooltip = "libjpeg 兼容质量分（jpegli_set_quality，jpegli/encode.h:51）；"
                                            "jpegli 参考 CLI 默认 90、允许 1–100（tools/cjpegli.cc:118,133）。"
                                            "quality_mode=quality 时可见。",
                                 .visible = quality_mode_is_quality, .locked = {}, .group = pp::kGroupCore},
                        ParamDef{.key = "chroma", .label = "色度采样",
                                 .type = ParamType::Enum, .def = std::string("444"),
                                 .choices = {{"444", std::string("444")},
                                             {"440", std::string("440")},
                                             {"422", std::string("422")},
                                             {"420", std::string("420")}},
                                 .advanced = false,
                                 .tooltip = "色度采样（chroma subsampling），按 jpeglib.h:125-126 的 "
                                            "comp_info[].h_samp_factor/v_samp_factor 设置；取值 "
                                            "444/440/422/420 与参考 CLI 一致（tools/cjpegli.cc:66,138）："
                                            "444=不降采样（视觉透明档）、440=仅垂直方向降采样。",
                                 .visible = {}, .locked = {}, .group = pp::kGroupCore},
                        ParamDef{.key = "progressive", .label = "渐进式",
                                 .type = ParamType::Bool, .def = true, .choices = {}, .advanced = false,
                                 .tooltip = "渐进式扫描（progressive_mode，jpeglib.h:413），对应 "
                                            "jpegli_set_progressive_level 1–2（jpegli/encode.h:150）；"
                                            "参考实现默认级别 2（lib/extras/enc/jpegli.h:32）。",
                                 .visible = {}, .locked = {}, .group = pp::kGroupCore},
                        ParamDef{.key = "optimize_coding", .label = "哈夫曼表优化",
                                 .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                 .tooltip = "Huffman 表优化（optimize_coding，jpeglib.h:369）；参考实现默认"
                                            "开启（lib/extras/enc/jpegli.h:33），jpegli_set_defaults 置 FALSE"
                                            "（lib/jpegli/encode.cc:65）。关闭后必须同时关闭渐进式"
                                            "（tools/cjpegli.cc:146）。",
                                 .visible = {}, .locked = lock_true_when_progressive, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "arith_code", .label = "算术编码",
                                 .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                 .tooltip = "【已禁用：勾选即 100% 编码失败】算术编码 arithmetic coding"
                                            "（arith_code，jpeglib.h:368）。本构建 jpegli 未实现：置 true "
                                            "直接报错 \"Arithmetic coding is not implemented.\""
                                            "（lib/jpegli/encode.cc:263-265），且 jconfig.h:13 未定义 "
                                            "C_ARITH_CODING_SUPPORTED。R-244：锁定为 false（禁用原因见句首）。",
                                 .visible = {}, .locked = lock_false_always, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "restart_in_rows", .label = "重启间隔（行）",
                                 .type = ParamType::Int, .def = int64_t(0), .lo = 0, .hi = 64,
                                 .step = 1, .choices = {}, .advanced = true,
                                 .tooltip = "Restart marker 间隔，单位 MCU 行（restart_in_rows，"
                                            "jpeglib.h:383）；0=不插入。jpegli 换算为 restart_interval 并"
                                            "限制 ≤65535（lib/jpegli/encode.cc:376-380）。本表 [0, 64] "
                                            "为 **UI 范围**（R-294 边界出处：库上限 65535 行）。",
                                 .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "dct_method", .label = "DCT 方法",
                                 .type = ParamType::Enum, .def = int64_t(0),
                                 .choices = {{"ISLOW", int64_t(0)},
                                             {"IFAST", int64_t(1)},
                                             {"FLOAT", int64_t(2)}},
                                 .advanced = true,
                                 .tooltip = "【已禁用：装饰性控件，选择不改变输出】DCT/IDCT 算法选择"
                                            "（J_DCT_METHOD，jpeglib.h:247-250：ISLOW=0、IFAST=1、FLOAT=2，"
                                            "后两者标注 legacy feature）。jpegli 内部固定浮点 DCT"
                                            "（jpegli_set_defaults 置 JDCT_FLOAT，lib/jpegli/encode.cc:68），"
                                            "该字段仅为 libjpeg 兼容层保留。R-246：锁定为 0（不改写既有"
                                            "参数快照；实际恒为浮点 DCT）。",
                                 .visible = {}, .locked = lock_zero_always, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "smoothing_factor", .label = "平滑",
                                 .type = ParamType::Int, .def = int64_t(0), .lo = 0, .hi = 100,
                                 .step = 1, .choices = {}, .advanced = true,
                                 .tooltip = "输入平滑强度（smoothing_factor，jpeglib.h:374）：0=关闭、"
                                            "1–100=强度；jpegli 校验 0–100（lib/jpegli/encode.cc:272-273）。",
                                 .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "xyb_mode", .label = "XYB 色彩量化",
                                 .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                 .tooltip = "【已禁用：需嵌 XYB ICC，本产品无该路径】XYB 色彩空间编码"
                                            "（jpegli_set_xyb_mode，jpegli/encode.h:132），实验性；会改变"
                                            "默认量化表与色度下采样并需嵌入 XYB ICC，而全仓无 XYB ICC "
                                            "生成/嵌入/告警路径（R-245 复核）。参考实现默认关闭"
                                            "（lib/extras/enc/jpegli.h:26）。R-245：锁定为 false。",
                                 .visible = {}, .locked = lock_false_always, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "adaptive_quantization", .label = "自适应量化",
                                 .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                 .tooltip = "自适应量化（jpegli_enable_adaptive_quantization，"
                                            "jpegli/encode.h:143-146）：按局部图像特性制造更多零系数；"
                                            "库与参考实现默认启用（lib/extras/enc/jpegli.h:30）。",
                                 .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "std_quant_tables", .label = "标准量化表",
                                 .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                 .tooltip = "改用 JPEG 标准 Annex K 量化表"
                                            "（jpegli_use_standard_quant_tables，jpegli/encode.h:152-157，"
                                            "无参开关函数）；false=jpegli 自有量化表（参考实现默认，"
                                            "lib/extras/enc/jpegli.h:31）。",
                                 .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "psnr_target", .label = "PSNR 目标",
                                 .type = ParamType::Float, .def = 0.0, .lo = 0.0, .hi = 100.0,
                                 .step = 0.5, .choices = {}, .advanced = true,
                                 .tooltip = "PSNR 目标（jpegli_set_psnr，jpegli/encode.h:125-127）：0=关闭"
                                            "（默认），>0 启用距离搜索逼近该 PSNR（dB）；tolerance/min/max "
                                            "取参考默认 0.01/0.1/25.0（lib/jpegli/encode.cc:84-87），"
                                            "头文件未规定上限（100 为 UI 上限）。【联动提示（R-247）："
                                            "设为 >0 后**静默覆盖**质量控制方式 / 视觉距离 / 质量三项"
                                            "（距离由 PSNR 搜索决定，不再取用上方设定）】。"
                                            "【已知问题：库的系数域 PSNR 估计与 RGB 实测系统性偏低，"
                                            "实测 40 目标约 35 dB；第二轮隐藏该控件，避免误导。】",
                                 .visible = never_visible, .locked = {}, .group = pp::kGroupEncodeTools},
                        ParamDef{.key = "cicp_transfer_function", .label = "传递函数码（仅量化表）",
                                 .type = ParamType::Int, .def = int64_t(2), .lo = 0, .hi = 18,
                                 .step = 1, .choices = {}, .advanced = true,
                                 .tooltip = "**不是色彩标记**：jpegli_set_cicp_transfer_function 只对 16=PQ/"
                                            "18=HLG 改变默认量化表缩放（lib/jpegli/quant.cc:525-526,577-579），"
                                            "不写 CICP/APP 标记；产物色彩仍由恒嵌 ICC 决定。默认 2=unknown。"
                                            "除 16/18 外其余码值被接受但无效果；本表 [0, 18] 为 UI 范围。",
                                 .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                    } } } } },
            .bitdepths = {8},
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "jxl", .label = "JPEG XL", .ext = "jxl",
            .backends = { BackendDef{
                .id = "libjxl", .label = "libjxl", .runtime_introspected = false,
                .techs = {
                    TechDef{ .id = "vardct", .label = "VarDCT", .lossless_capable = false,
                             .params = {
                                 jxl_effort_param(true),        // R-209：工厂单点声明
                                 jxl_decoding_speed_param(true), // R-209
                                 jxl_codestream_level_param(true), // R-209
                                 jxl_distance_param(true),      // R-209
                                 ParamDef{.key = "photon_noise", .label = "感光噪声",
                                          .type = ParamType::Float, .def = 0.0, .lo = 0.0,
                                          .hi = 6400.0, .step = 100.0, .choices = {}, .advanced = true,
                                          .tooltip = "Photon noise 胶片颗粒模拟"
                                                     "（JXL_ENC_FRAME_SETTING_PHOTON_NOISE，"
                                                     "jxl/encode.h:166-171）：100=轻微、3200=明显，库默认 "
                                                     "0；头文件未规定上限，6400 为 UI 上限。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupNoise},
                                 ParamDef{.key = "epf", .label = "边缘保持滤波",
                                          .type = ParamType::Int, .def = int64_t(2), .lo = -1,
                                          .hi = 3, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "边缘保持滤波（JXL_ENC_FRAME_SETTING_EPF，"
                                                     "jxl/encode.h:189-192）：-1=编码器自选（库默认）、"
                                                     "0–3=强度；catalog 默认 2。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "keep_invisible", .label = "保留不可见像素",
                                          .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                          .tooltip = "保留不可见像素 RGB（JXL_ENC_FRAME_SETTING_KEEP_INVISIBLE，"
                                                     "jxl/encode.h:205-208）：库以 -1 表示默认（无损=1、"
                                                     "有损=0）；Bool 无法表达 -1。有损默认 false（避免 3.1× "
                                                     "体积）；无损下该行隐藏。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "dots", .label = "点阵生成",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "点阵生成（JXL_ENC_FRAME_SETTING_DOTS，"
                                                     "jxl/encode.h:179-182）：库以 -1 表示默认（编码器自选）；"
                                                     "Bool 无法表达 -1，本表取 true（不主动关闭编码工具）。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "patches", .label = "图块复用",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "图块/贴片复用（JXL_ENC_FRAME_SETTING_PATCHES，"
                                                     "jxl/encode.h:184-187）：库以 -1 表示默认（编码器自选）；"
                                                     "Bool 无法表达 -1，本表取 true。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "gaborish", .label = "Gaborish 滤波",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "Gaborish 滤波器（JXL_ENC_FRAME_SETTING_GABORISH，"
                                                     "jxl/encode.h:194-197）：库以 -1 表示默认（编码器自选，"
                                                     "通常启用）；Bool 无法表达 -1，本表取 true。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "progressive_ac", .label = "AC 渐进（谱进）",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "VarDCT AC 系数谱渐进"
                                                     "（JXL_ENC_FRAME_SETTING_PROGRESSIVE_AC，jxl/encode.h:232-236）："
                                                     "-1=编码器自选（库默认）、0=关闭、1=开启；只影响解码渐进性，"
                                                     "不改变最终画质。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupProgressive},
                                 ParamDef{.key = "qprogressive_ac", .label = "AC 渐进（量化位）",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "VarDCT AC 最低有效位量化渐进"
                                                     "（JXL_ENC_FRAME_SETTING_QPROGRESSIVE_AC，"
                                                     "jxl/encode.h:238-242）：-1=编码器自选（库默认）、0=关闭、"
                                                     "1=开启。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupProgressive},
                                 ParamDef{.key = "progressive_dc", .label = "DC 渐进级数",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 2, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "VarDCT DC 低分辨率渐进层"
                                                     "（JXL_ENC_FRAME_SETTING_PROGRESSIVE_DC，"
                                                     "jxl/encode.h:244-248）：-1=编码器自选（库默认）、0=关闭、"
                                                     "1=额外 64×64 层、2=512×512 与 64×64 层。",
                                          .visible = not_lossless, .locked = {}, .group = pp::kGroupProgressive},
                                 jxl_resampling_param(true),   // R-209：工厂单点声明
                                 jxl_group_order_param(true),  // R-209
                                 lossless_param("JxlEncoderSetFrameLossless(true)（jxl/encode.h:1411-"
                                                "1424，库会强制走 Modular 路径）"),
                              } },
                    TechDef{ .id = "modular", .label = "Modular", .lossless_capable = true,
                             .params = {
                                 jxl_effort_param(false),         // R-209：工厂单点声明
                                 jxl_decoding_speed_param(false), // R-209
                                 jxl_codestream_level_param(false), // R-209
                                 jxl_distance_param(false),       // R-209
                                 ParamDef{.key = "color_transform", .label = "色彩变换",
                                          .type = ParamType::Enum, .def = std::string("auto"),
                                          .choices = {{"auto", std::string("auto")},
                                                      {"None", std::string("None")},
                                                      {"YCoCg", std::string("YCoCg")},
                                                      {"XYB", std::string("XYB")},
                                                      {"YCbCr", std::string("YCbCr")}},
                                          .advanced = true,
                                          .tooltip = "色彩变换：auto（-1，库默认）不设置，交给 libjxl 按 "
                                                     "uses_original_profile 选择；None/XYB/YCbCr 在当前 C API "
                                                     "下会被库覆写（等于不设置），只有 YCoCg 强制全局 RCT、"
                                                     "通常更大。无损建议 auto。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "modular_group_size", .label = "组尺寸",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 3, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Modular 组尺寸"
                                                     "（JXL_ENC_FRAME_SETTING_MODULAR_GROUP_SIZE，"
                                                     "jxl/encode.h:288-290）：-1=默认、0=128、1=256、"
                                                     "2=512、3=1024。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "modular_predictor", .label = "预测器",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 15, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Modular 预测器"
                                                     "（JXL_ENC_FRAME_SETTING_MODULAR_PREDICTOR，"
                                                     "jxl/encode.h:292-297）：-1=默认；0 zero、1 left、"
                                                     "5 gradient … 15 mix everything。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "modular_palette_colors", .label = "调色板颜色数上限",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 4096, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "调色板（JXL_ENC_FRAME_SETTING_PALETTE_COLORS，"
                                                     "jxl/encode.h:262-265）：-1=编码器默认、0=关闭、"
                                                     ">0=调色板颜色数上限（头文件未规定上限，4096 为 UI 上限）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "modular_lossy_palette", .label = "有损调色板",
                                          .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                          .tooltip = "Delta palette 有损调色板"
                                                     "（JXL_ENC_FRAME_SETTING_LOSSY_PALETTE，"
                                                     "jxl/encode.h:267-270）：-1=默认、0=关闭、1=开启；"
                                                     "有损，无损档保持关闭。",
                                          .visible = {}, .locked = lock_false_when_lossless, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "brotli_effort", .label = "Brotli 努力",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 11, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Brotli 努力（JXL_ENC_FRAME_SETTING_BROTLI_EFFORT，"
                                                     "jxl/encode.h:329-334）：-1=默认（brob box 取 4）、"
                                                     "0 最快 … 11 最慢；本工具不产 brob/JPEG 重压缩 box，"
                                                     "该参数无效果（JXL-09）→ 隐藏。",
                                          .visible = never_visible, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "responsive", .label = "响应式渐进",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "Modular 模式的响应式/渐进编码"
                                                     "（JXL_ENC_FRAME_SETTING_RESPONSIVE，jxl/encode.h:227-230，"
                                                     "头文件语义为 modular）；库默认 -1（编码器自选），"
                                                     "Bool 无法表达 -1。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "channel_colors_global_percent", .label = "全局调色板阈值 %",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "全图通道调色板阈值（JXL_ENC_FRAME_SETTING_"
                                                     "CHANNEL_COLORS_GLOBAL_PERCENT，jxl/encode.h:250-254）："
                                                     "-1=编码器默认、0–100=颜色数低于该百分比时启用全局调色板。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "channel_colors_group_percent", .label = "局部调色板阈值 %",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "每组通道调色板阈值（JXL_ENC_FRAME_SETTING_"
                                                     "CHANNEL_COLORS_GROUP_PERCENT，jxl/encode.h:256-260）："
                                                     "-1=编码器默认、0–100=颜色数低于该百分比时启用局部调色板。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "modular_ma_tree_learning_percent", .label = "MA 树学习比例 %",
                                          .type = ParamType::Float, .def = -1.0, .lo = -1.0,
                                          .hi = 100.0, .step = 1.0, .choices = {}, .advanced = true,
                                          .tooltip = "MA 树学习的像素比例（JXL_ENC_FRAME_SETTING_"
                                                     "MODULAR_MA_TREE_LEARNING_PERCENT，jxl/encode.h:299-303）："
                                                     "-1=默认（50）、0=不做 MA 且解码更快、100=全部、>100 亦允许"
                                                     "（更耗内存）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "modular_nb_prev_channels", .label = "前序通道属性数",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 11, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "参与 MA 树的前序通道数（JXL_ENC_FRAME_SETTING_"
                                                     "MODULAR_NB_PREV_CHANNELS，jxl/encode.h:305-311）："
                                                     "-1=默认、0–11 合法；推荐 0–3。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 jxl_resampling_param(false),  // R-209：工厂单点声明
                                 jxl_group_order_param(false), // R-209
                                 lossless_param("JxlEncoderSetFrameLossless(true)（jxl/encode.h:1411-"
                                                "1424；Modular+lossless 即真无损）"),
                              } } } } },
            .bitdepths = {8, 16},
            // 第二轮裁定：alpha 明确不支持（不输出 alpha；输入在 color 前拍平）。
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "jxl-box" },
        FormatDef{
            .id = "png", .label = "PNG", .ext = "png",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "deflate", .label = "Deflate", .lossless_capable = true,
                             .params = {
                                 ParamDef{.key = "compressionLevel", .label = "zlib 压缩级别",
                                          .type = ParamType::Int, .def = int64_t(6), .lo = 0,
                                          .hi = 9, .step = 1, .choices = {}, .advanced = false,
                                          .tooltip = "zlib 压缩级别（OIIO PNG 输出属性 "
                                                     "png:compressionLevel，src/png.imageio/pngoutput.cpp:164-169）："
                                                     "默认 6，钳制在 [0,9]；无损，仅影响体积与速度。OIIO PNG "
                                                     "writer 无 interlace 属性（png_pvt.h:615-617 固定 "
                                                     "PNG_INTERLACE_NONE）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupCore},
                                 ParamDef{.key = "compression", .label = "zlib 策略",
                                          .type = ParamType::Enum, .def = std::string("default"),
                                          .choices = {{"default", std::string("default")},
                                                      {"filtered", std::string("filtered")},
                                                      {"huffman", std::string("huffman")},
                                                      {"rle", std::string("rle")},
                                                      {"fixed", std::string("fixed")},
                                                      {"pngfast", std::string("pngfast")},
                                                      {"none", std::string("none")}},
                                          .advanced = true,
                                          .tooltip = "zlib 压缩策略（OIIO 属性 compression，"
                                                     "pngoutput.cpp:170-190）；default=Z_DEFAULT_STRATEGY、"
                                                     "filtered=Z_FILTERED、huffman=Z_HUFFMAN_ONLY、"
                                                     "rle=Z_RLE、fixed=Z_FIXED、pngfast=最快、none=不压缩。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "filter", .label = "PNG 行滤波",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 255, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "libpng 行滤波位掩码（OIIO 属性 png:filter，"
                                                     "pngoutput.cpp:199-200）：0=PNG_NO_FILTERS（默认）、"
                                                     "8=PNG_FILTER_NONE、0xF8=全滤波；具体收益依图像内容。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                              } } } } },
            .bitdepths = {8, 16},
            // 第二轮裁定：alpha 明确不支持（不输出 alpha；输入在 color 前拍平）。
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "tiff", .label = "TIFF", .ext = "tif",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "codec", .label = "Compression", .lossless_capable = true,
                             .params = {
                                 ParamDef{.key = "compression", .label = "压缩方案",
                                          .type = ParamType::Enum, .def = std::string("lzw"),
                                          .choices = {{"none", std::string("none")},
                                                      {"lzw", std::string("lzw")},
                                                      {"zip", std::string("zip")},
                                                      {"packbits", std::string("packbits")}},
                                          .advanced = false,
                                          .tooltip = "压缩方案（OIIO 属性 compression，"
                                                     "src/tiff.imageio/tiffoutput.cpp:660-673）。OIIO 3.1.14.0 "
                                                     "输出名表：none/lzw/zip/packbits；deflate 是 zip 的别名"
                                                     "（OIIO 只认 zip）；未知名静默回退 deflate。"
                                                     "ccittrle 只支持 1 位样本、本管线永远产 8/16 位 → 已删除"
                                                     "（TIFF-02）。库默认 zip，本表默认 lzw。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupCore},
                                 ParamDef{.key = "deflate_level", .label = "Deflate 级别",
                                          .type = ParamType::Int, .def = int64_t(6), .lo = 1,
                                          .hi = 9, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Deflate 级别（OIIO 属性 tiff:zipquality，"
                                                     "tiffoutput.cpp:694-702，钳制 1–9）；也可用 "
                                                     "compression=\"zip:9\" 形式（ImageSpec::"
                                                     "decode_compression_metadata，OpenImageIO/imageio.h:756-760）。"
                                                     "compression=zip 时可见。",
                                          .visible = compression_is_zip, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "predictor", .label = "预测器",
                                          .type = ParamType::Enum, .def = int64_t(2),
                                          .choices = {{"none", int64_t(1)},
                                                      {"horizontal", int64_t(2)}},
                                          .advanced = true,
                                 .tooltip = "Predictor（OIIO 属性 tiff:Predictor，取整数；"
                                            "libtiff PREDICTOR_NONE/HORIZONTAL=1/2，tiff.h:304-306）。"
                                            "R-011 复核：控制面真实生效（tiffoutput.cpp:1026-1029 "
                                            "put_parameter 在自动规则之后跑，用户值覆盖自动选择）；"
                                            "none/horizontal 在 lzw/zip 下产出不同字节。float(3) 只支持"
                                            "浮点样本，本编码器输出 8/16 位整型 → 已删除（TIFF-05）。",
                                          .visible = compression_is_lzw_or_zip, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "tiff_tile_width", .label = "Tile 宽度",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 4096, .step = 16, .choices = {}, .advanced = true,
                                          .tooltip = "TIFF tile 宽度（OIIO 经 ImageSpec::tile_width 设置，"
                                                     "writer 无 tiff:tilewidth 属性；tiffoutput.cpp:480-487,"
                                                     "575-577）：0=条带扫描 strip（默认）、>0 时须为 16 的倍数"
                                                     "且 tile 高同为 16 的倍数，否则 writer 直接报错"
                                                     "（tiffoutput.cpp:481-485）；4096 为 UI 上限。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "tiff_tile_height", .label = "Tile 高度",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 4096, .step = 16, .choices = {}, .advanced = true,
                                          .tooltip = "TIFF tile 高度（OIIO 经 ImageSpec::tile_height 设置，"
                                                     "writer 无 tiff:tileheight 属性；tiffoutput.cpp:480-487,"
                                                     "575-577）：0=条带扫描 strip（默认）、>0 时须与 tile 宽"
                                                     "同为 16 的倍数（tiffoutput.cpp:481-485）；4096 为 UI 上限。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                              } } } } },
            .bitdepths = {8, 16},
            // 第二轮裁定：alpha 明确不支持（不输出 alpha；输入在 color 前拍平）。
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "webp", .label = "WebP", .ext = "webp",
            .backends = { BackendDef{
                .id = "libwebp", .label = "libwebp", .runtime_introspected = false,
                .techs = {
                    TechDef{ .id = "lossy", .label = "Lossy", .lossless_capable = false,
                             .params = {
                                 webp_quality_param(false), // R-209：工厂单点声明（一键两义拆清）
                                 ParamDef{.key = "sharp_yuv", .label = "锐利色度上采样",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = false,
                                          .tooltip = "锐利色度上采样（WebPConfig::use_sharp_yuv，"
                                                     "webp/encode.h:151）：更锐利但更慢的 RGB→YUV 转换；"
                                                     "WebPConfigInit 默认 0，本表取 true（视觉透明档）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupCore},
                                 webp_method_param(false), // R-209
                                 // 第二轮审计 WEBP-03：preset 的字段随后被逐项覆盖，六档同字节；
                                 // 删除该死控件（不再暴露 WebPConfigPreset 的内容预设）。
                                 ParamDef{.key = "sns_strength", .label = "空间噪声整形",
                                          .type = ParamType::Int, .def = int64_t(50), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "空间噪声整形（WebPConfig::sns_strength，"
                                                     "webp/encode.h:113）：0=关闭、100=最强；库默认 50、"
                                                     "photo 预设 80（src/enc/config_enc.c）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "filter_strength", .label = "去噪滤波",
                                          .type = ParamType::Int, .def = int64_t(30), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "去块滤波强度（WebPConfig::filter_strength，"
                                                     "webp/encode.h:114）：0=关闭、100=最强；库默认 60、"
                                                     "photo 预设 30（src/enc/config_enc.c）；本表默认 30"
                                                     "（preset=photo 的生效值），0 时 filter_type 无影响。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "autofilter", .label = "自动滤波",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "自动调整滤波强度（WebPConfig::autofilter，"
                                                     "webp/encode.h:118，取值 0/1）；库默认 0。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "pass", .label = "通道数",
                                          .type = ParamType::Int, .def = int64_t(1), .lo = 1,
                                          .hi = 10, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "熵分析通道数（WebPConfig::pass，webp/encode.h:125，"
                                                     "范围 1–10）；库默认 1，增加可略提升压缩率但更慢。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "filter_sharpness", .label = "滤波锐度",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 7, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "滤波锐度（WebPConfig::filter_sharpness，webp/encode.h:115，"
                                                     "0=最锐 … 7=最钝）；库默认 0、photo 预设 3"
                                                     "（src/enc/config_enc.c）；仅 filter_strength>0 或 "
                                                     "autofilter 时生效。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "filter_type", .label = "滤波类型（强/简）",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "滤波类型（WebPConfig::filter_type，webp/encode.h:116-117）："
                                                     "false=simple(0)、true=strong(1，U/V 也滤波)；库默认 1；"
                                                     "仅 filter_strength>0 或 autofilter 时生效。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "segments", .label = "段数",
                                          .type = ParamType::Int, .def = int64_t(4), .lo = 1,
                                          .hi = 4, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "最大分段数（WebPConfig::segments，webp/encode.h:112，"
                                                     "[1..4]）；库默认 4，photo 预设 4、text 预设 2"
                                                     "（src/enc/config_enc.c）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 // 第二轮裁定：alpha 明确不支持 → 删除 alpha 专用参数（alpha_compression/
                                 // alpha_filtering/alpha_quality）；编码器仍保留防御性默认读取，但产品面不再暴露。
                                 ParamDef{.key = "partitions", .label = "Token 分区数 log2",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 3, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Token 分区数（WebPConfig::partitions，webp/encode.h:131-132，"
                                                     "log2 值 [0..3]）；库默认 0（更易渐进解码）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "partition_limit", .label = "分区退化上限",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "为满足 512k 预测模式编码上限允许的质量退化"
                                                     "（WebPConfig::partition_limit，webp/encode.h:133-135）："
                                                     "0=不退化（库默认）… 100=最大退化。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "preprocessing", .label = "预处理滤波",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 7, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "预处理滤波（WebPConfig::preprocessing，webp/encode.h:129-130）："
                                                     "0=none（库默认）、1=segment-smooth、2=伪随机抖动，按位组合至 7"
                                                     "（WebPValidateConfig 允许 0–7）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "qmin", .label = "最小量化因子",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "量化因子下限（WebPConfig::qmin，webp/encode.h:153）；"
                                                     "库默认 0，须满足 qmin≤qmax（WebPValidateConfig）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "qmax", .label = "最大量化因子",
                                          .type = ParamType::Int, .def = int64_t(100), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "量化因子上限（WebPConfig::qmax，webp/encode.h:154）；"
                                                     "库默认 100，须满足 qmin≤qmax（WebPValidateConfig）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "emulate_jpeg_size", .label = "模拟 JPEG 体积",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "重映射压缩参数以贴近同质量 JPEG 体积"
                                                     "（WebPConfig::emulate_jpeg_size，webp/encode.h:136-139）；"
                                                     "库默认 false；有损路径。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 ParamDef{.key = "low_memory", .label = "低内存模式",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "降低内存占用（WebPConfig::low_memory，webp/encode.h:141，"
                                                     "代价是更多 CPU）；库默认 false；作用于有损 VP8 路径"
                                                     "（src/enc/webp_enc.c:119）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 lossless_param("WebPConfig::lossless = 1（webp/encode.h:97-98；"
                                                "有损技术下该参数为 false）"),
                              } },
                    TechDef{ .id = "lossless", .label = "Lossless", .lossless_capable = true,
                             .params = {
                                 webp_quality_param(true), // R-209：工厂单点声明（一键两义拆清）
                                 ParamDef{.key = "exact", .label = "保留透明区 RGB 原值",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "保留透明区 RGB 原值（WebPConfig::exact，"
                                                     "webp/encode.h:145-148）：0=丢弃以获得更好压缩（库默认）；"
                                                     "本表取 true（视觉透明档）。",
                                          .visible = is_lossless, .locked = {}, .group = pp::kGroupEncodeTools},
                                 webp_method_param(true), // R-209
                                 ParamDef{.key = "near_lossless", .label = "近无损强度",
                                          .type = ParamType::Int, .def = int64_t(100), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "近无损（WebPConfig::near_lossless，webp/encode.h:143-144）："
                                                     "0=最大颜色改动 … 100=关闭（库默认 100）；<100 时启用 VP8L "
                                                     "近无损预处理、输出非严格无损（src/enc/vp8l_enc.c:"
                                                     "1105-1117,1572-1577）。",
                                          .visible = {}, .locked = {}, .group = pp::kGroupEncodeTools},
                                 lossless_param("WebPConfig::lossless = 1（webp/encode.h:97-98）"
                                                "或 tech_id=\"lossless\"；两者一致时无损生效"),
                              } } } } },
            .bitdepths = {8},
            // 第二轮裁定：alpha 明确不支持（不输出 alpha；输入在 color 前拍平）。
            .supports_alpha = false, .supports_gray = false,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "bmp", .label = "BMP", .ext = "bmp",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "raw", .label = "Raw", .lossless_capable = true,
                             .params = {} } } } },
            // R-090（M7-WP2）：位深口径改实 —— 编码器（enc_oiio）实际接受 24 与 8，旧表只列
            // {24} ⇒ pipeline/预设校验拒 8、schema 与实现矛盾（灰度 BMP 实产 8bpp 也被说成 24）。
            // R-211 单位口径：**bmp 的 out_bitdepth 是每像素位数（bpp）**，其余格式是每样本
            // 位数（bps）—— 同名 int 双语义，见 params.h 的 FormatDef::bitdepths 注释。
            //   24 = RGB 24bpp（每样本 8 位；default_bitdepth 恒 24，见 main.cpp/page_output）
            //   8  = 8bpp（灰度源产物的真实口径；RGB 源下与 24 等价 —— 两者同为 UINT8 采样）
            .bitdepths = {8, 24},
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "none" },
        FormatDef{
            .id = "heif", .label = "HEIF", .ext = "heic",
            .backends = { BackendDef{
                .id = "x265", .label = "x265", .runtime_introspected = true,
                .techs = {} } },
            .bitdepths = {8, 10, 12},
            // 第二轮裁定：alpha 明确不支持（不输出 alpha；输入在 color 前拍平）。
            .supports_alpha = false, .supports_gray = false,
            .meta_path = "libheif" },
        FormatDef{
            .id = "avif", .label = "AVIF", .ext = "avif",
            .backends = {
                BackendDef{ .id = "svt-av1", .label = "SVT-AV1", .runtime_introspected = true,
                            .techs = {} },
                BackendDef{ .id = "libaom", .label = "libaom", .runtime_introspected = true,
                            .techs = {} } },
            .bitdepths = {8, 10, 12},
            // 第二轮裁定：alpha 明确不支持（不输出 alpha；输入在 color 前拍平）。
            .supports_alpha = false, .supports_gray = false,
            .meta_path = "libheif" },
        };
        // M8-B0：JXL 目录行并入静态表（按 tech 过滤；目录行 has_default=false，既有产物零变化）。
        for (FormatDef &f : v) {
            if (f.id != "jxl")
                continue;
            for (BackendDef &b : f.backends) {
                if (b.runtime_introspected)
                    continue;
                for (TechDef &t : b.techs) {
                    const catalog::TechScope tech = t.id == "modular" ? catalog::TechScope::Modular
                                                                      : catalog::TechScope::Vardct;
                    std::vector<ParamDef> extra = catalog::param_defs(f.id, b.id, tech);
                    t.params.insert(t.params.end(), extra.begin(), extra.end());
                }
            }
        }
        return v;
    }();
    return fmts;
}

} // namespace pp
