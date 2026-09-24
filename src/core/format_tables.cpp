// PP-FROZEN(structure): 只允许替换 /* PP-PLACEHOLDER */ 注释处的内容
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/params.h"

namespace pp {

namespace {

// —— 谓词辅助（M1-T2；规则见 docs/m1-tasks.md §4.2）——
// PP-FROZEN(0.3.0)：保留键 "__lossless" = 无损的**内部管道键**（T13 定稿保留，见 params.h）；
// 无损开关由参数引擎写入 ParamSet（见 src/core/params.cpp）。
// 0.3.0（M4-T13）：无损另有**显式 schema 参数**（key = "lossless"，Bool，见 params.h 的
// kLosslessParamKey 与 docs/param-catalog.md）。两键由引擎同步（apply_locks 是唯一写入点）；
// 谓词仍读内部管道键 —— 单实现、零行为漂移（0.2 的谓词语义逐字不变）。
bool is_lossless(const ParamSet &s) { return param_bool(s, "__lossless", false); }
bool not_lossless(const ParamSet &s) { return !is_lossless(s); }

bool quality_mode_is_distance(const ParamSet &s) {
    return param_str(s, "quality_mode", "distance") == "distance";
}
bool quality_mode_is_quality(const ParamSet &s) {
    return param_str(s, "quality_mode", "distance") == "quality";
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
std::optional<ParamValue> lock_false_when_lossless(const ParamSet &s) {
    if (is_lossless(s))
        return ParamValue{false};
    return std::nullopt;
}

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
    p.tooltip = std::string("无损输出（0.3.0 显式 schema 参数；由格式参数卡顶部的「无损」复选框"
                            "驱动，本行不单独渲染）。编码器侧：") +
                encoder_note +
                "。内部管道键 __lossless 与本参数由参数引擎同步（src/core/params.cpp）。";
    return p;
}

} // namespace

const std::vector<FormatDef> &static_formats() {
    static const std::vector<FormatDef> fmts = {
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
                                            "（jpegli_set_quality，内部换算为距离）；二选一联动下方控件。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "distance", .label = "视觉距离",
                                 .type = ParamType::Float, .def = 1.0, .lo = 0.3, .hi = 3.0,
                                 .step = 0.1, .choices = {}, .advanced = false,
                                 .tooltip = "Butteraugli 目标距离（jpegli_set_distance，jpegli/encode.h:119）；"
                                            "1.0=视觉透明；jpegli 参考 CLI 推荐 0.5–3.0、允许 0.0–25.0"
                                            "（tools/cjpegli.cc:53）。quality_mode=distance 时可见。",
                                 .visible = quality_mode_is_distance, .locked = {}},
                        ParamDef{.key = "quality", .label = "质量",
                                 .type = ParamType::Int, .def = int64_t(90), .lo = 1, .hi = 100,
                                 .step = 1, .choices = {}, .advanced = false,
                                 .tooltip = "libjpeg 兼容质量分（jpegli_set_quality，jpegli/encode.h:51）；"
                                            "jpegli 参考 CLI 默认 90、允许 1–100（tools/cjpegli.cc:118,133）。"
                                            "quality_mode=quality 时可见。",
                                 .visible = quality_mode_is_quality, .locked = {}},
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
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "progressive", .label = "渐进式",
                                 .type = ParamType::Bool, .def = true, .choices = {}, .advanced = false,
                                 .tooltip = "渐进式扫描（progressive_mode，jpeglib.h:413），对应 "
                                            "jpegli_set_progressive_level 1–2（jpegli/encode.h:150）；"
                                            "参考实现默认级别 2（lib/extras/enc/jpegli.h:32）。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "optimize_coding", .label = "哈夫曼表优化",
                                 .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                 .tooltip = "Huffman 表优化（optimize_coding，jpeglib.h:369）；参考实现默认"
                                            "开启（lib/extras/enc/jpegli.h:33），jpegli_set_defaults 置 FALSE"
                                            "（lib/jpegli/encode.cc:65）。关闭后必须同时关闭渐进式"
                                            "（tools/cjpegli.cc:146）。",
                                 .visible = {}, .locked = lock_true_when_progressive},
                        ParamDef{.key = "arith_code", .label = "算术编码",
                                 .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                 .tooltip = "算术编码 arithmetic coding（arith_code，jpeglib.h:368）。本构建 "
                                            "jpegli 未实现：置 true 直接报错 \"Arithmetic coding is not "
                                            "implemented.\"（lib/jpegli/encode.cc:263-265），且 jconfig.h:13 "
                                            "未定义 C_ARITH_CODING_SUPPORTED；兼容性风险，保持关闭。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "restart_in_rows", .label = "重启间隔（行）",
                                 .type = ParamType::Int, .def = int64_t(0), .lo = 0, .hi = 64,
                                 .step = 1, .choices = {}, .advanced = true,
                                 .tooltip = "Restart marker 间隔，单位 MCU 行（restart_in_rows，"
                                            "jpeglib.h:383）；0=不插入。jpegli 换算为 restart_interval 并"
                                            "限制 ≤65535（lib/jpegli/encode.cc:376-380）。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "dct_method", .label = "DCT 方法",
                                 .type = ParamType::Enum, .def = int64_t(0),
                                 .choices = {{"ISLOW", int64_t(0)},
                                             {"IFAST", int64_t(1)},
                                             {"FLOAT", int64_t(2)}},
                                 .advanced = true,
                                 .tooltip = "DCT/IDCT 算法选择（J_DCT_METHOD，jpeglib.h:247-250：ISLOW=0、"
                                            "IFAST=1、FLOAT=2，后两者标注 legacy feature）。jpegli 内部固定"
                                            "浮点 DCT（jpegli_set_defaults 置 JDCT_FLOAT，"
                                            "lib/jpegli/encode.cc:68），该字段仅为 libjpeg 兼容层保留。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "smoothing_factor", .label = "平滑",
                                 .type = ParamType::Int, .def = int64_t(0), .lo = 0, .hi = 100,
                                 .step = 1, .choices = {}, .advanced = true,
                                 .tooltip = "输入平滑强度（smoothing_factor，jpeglib.h:374）：0=关闭、"
                                            "1–100=强度；jpegli 校验 0–100（lib/jpegli/encode.cc:272-273）。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "xyb_mode", .label = "XYB 色彩量化",
                                 .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                 .tooltip = "XYB 色彩空间编码（jpegli_set_xyb_mode，jpegli/encode.h:132），"
                                            "实验性；会改变默认量化表与色度下采样并需嵌入 XYB ICC。参考实现"
                                            "默认关闭（lib/extras/enc/jpegli.h:26）。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "adaptive_quantization", .label = "自适应量化",
                                 .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                 .tooltip = "自适应量化（jpegli_enable_adaptive_quantization，"
                                            "jpegli/encode.h:143-146）：按局部图像特性制造更多零系数；"
                                            "库与参考实现默认启用（lib/extras/enc/jpegli.h:30）。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "std_quant_tables", .label = "标准量化表",
                                 .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                 .tooltip = "改用 JPEG 标准 Annex K 量化表"
                                            "（jpegli_use_standard_quant_tables，jpegli/encode.h:152-157，"
                                            "无参开关函数）；false=jpegli 自有量化表（参考实现默认，"
                                            "lib/extras/enc/jpegli.h:31）。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "psnr_target", .label = "PSNR 目标",
                                 .type = ParamType::Float, .def = 0.0, .lo = 0.0, .hi = 100.0,
                                 .step = 0.5, .choices = {}, .advanced = true,
                                 .tooltip = "PSNR 目标（jpegli_set_psnr，jpegli/encode.h:125-127）：0=关闭"
                                            "（默认），>0 启用距离搜索逼近该 PSNR（dB）；tolerance/min/max "
                                            "取参考默认 0.01/0.1/25.0（lib/jpegli/encode.cc:84-87），"
                                            "头文件未规定上限（100 为 UI 上限）。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "cicp_transfer_function", .label = "CICP 传递函数",
                                 .type = ParamType::Int, .def = int64_t(2), .lo = 0, .hi = 18,
                                 .step = 1, .choices = {}, .advanced = true,
                                 .tooltip = "输入传递函数 CICP/H.273 码（jpegli_set_cicp_transfer_function，"
                                            "jpegli/encode.h:134-138，须在 jpegli_set_defaults 之前调用）："
                                            "默认 2=unknown；jpegli 仅对 16=PQ、18=HLG 改变默认量化表"
                                            "（lib/jpegli/quant.cc:525-526,577-579），其余码值被接受但无"
                                            "效果（无校验）。",
                                 .visible = {}, .locked = {}},
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
                                 ParamDef{.key = "effort", .label = "编码努力",
                                          .type = ParamType::Int, .def = int64_t(7), .lo = 1,
                                          .hi = 10, .step = 1, .choices = {}, .advanced = false,
                                          .tooltip = "编码努力 effort（JXL_ENC_FRAME_SETTING_EFFORT，"
                                                     "jxl/encode.h:126-132）：1 lightning … 7 squirrel"
                                                     "（库默认）… 10 glacier，不影响解码速度。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "decoding_speed", .label = "解码速度预算",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 4, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "解码速度预算（JXL_ENC_FRAME_SETTING_DECODING_SPEED，"
                                                     "jxl/encode.h:134-138）：0=最慢解码但密度最高（库默认）… "
                                                     "4=最快解码。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "codestream_level", .label = "码流级别",
                                          .type = ParamType::Enum, .def = int64_t(10),
                                          .choices = {{"auto", int64_t(-1)},
                                                      {"5", int64_t(5)},
                                                      {"10", int64_t(10)}},
                                          .advanced = true,
                                          .tooltip = "JPEG XL 码流级别（JxlEncoderSetCodestreamLevel，"
                                                     "jxl/encode.h:1348-1383）：-1=自动（库默认，按 basic "
                                                     "info 选择）、5=兼容性最广、10=解除限制（CMYK/32bit）；"
                                                     "catalog 默认 10。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "distance", .label = "视觉距离",
                                          .type = ParamType::Float, .def = 1.0, .lo = 0.0,
                                          .hi = 25.0, .step = 0.1, .choices = {}, .advanced = false,
                                          .tooltip = "目标 Butteraugli 距离（JxlEncoderSetFrameDistance，"
                                                     "jxl/encode.h:1428-1442）：范围 0–25、库默认 1.0、"
                                                     "1.0=视觉无损、推荐 0.5–3.0；0.0 需配合 "
                                                     "JxlEncoderSetFrameLossless 才是真无损。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "photon_noise", .label = "感光噪声模拟 (ISO)",
                                          .type = ParamType::Float, .def = 0.0, .lo = 0.0,
                                          .hi = 6400.0, .step = 100.0, .choices = {}, .advanced = true,
                                          .tooltip = "Photon noise 胶片颗粒模拟"
                                                     "（JXL_ENC_FRAME_SETTING_PHOTON_NOISE，"
                                                     "jxl/encode.h:166-171）：100=轻微、3200=明显，库默认 "
                                                     "0；头文件未规定上限，6400 为 UI 上限。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "epf", .label = "边缘保持滤波",
                                          .type = ParamType::Int, .def = int64_t(2), .lo = -1,
                                          .hi = 3, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "边缘保持滤波（JXL_ENC_FRAME_SETTING_EPF，"
                                                     "jxl/encode.h:189-192）：-1=编码器自选（库默认）、"
                                                     "0–3=强度；catalog 默认 2。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "keep_invisible", .label = "保留不可见像素",
                                          .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                          .tooltip = "保留不可见像素 RGB（JXL_ENC_FRAME_SETTING_KEEP_INVISIBLE，"
                                                     "jxl/encode.h:205-208）：库以 -1 表示默认（无损=1、"
                                                     "有损=0）；Bool 无法表达 -1，本表取 true（更保真）。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "dots", .label = "点阵生成",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "点阵生成（JXL_ENC_FRAME_SETTING_DOTS，"
                                                     "jxl/encode.h:179-182）：库以 -1 表示默认（编码器自选）；"
                                                     "Bool 无法表达 -1，本表取 true（不主动关闭编码工具）。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "patches", .label = "图块复用",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "图块/贴片复用（JXL_ENC_FRAME_SETTING_PATCHES，"
                                                     "jxl/encode.h:184-187）：库以 -1 表示默认（编码器自选）；"
                                                     "Bool 无法表达 -1，本表取 true。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "gaborish", .label = "Gaborish 滤波",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "Gaborish 滤波器（JXL_ENC_FRAME_SETTING_GABORISH，"
                                                     "jxl/encode.h:194-197）：库以 -1 表示默认（编码器自选，"
                                                     "通常启用）；Bool 无法表达 -1，本表取 true。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "progressive_ac", .label = "AC 渐进（谱进）",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "VarDCT AC 系数谱渐进"
                                                     "（JXL_ENC_FRAME_SETTING_PROGRESSIVE_AC，jxl/encode.h:232-236）："
                                                     "-1=编码器自选（库默认）、0=关闭、1=开启；只影响解码渐进性，"
                                                     "不改变最终画质。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "qprogressive_ac", .label = "AC 渐进（量化位）",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "VarDCT AC 最低有效位量化渐进"
                                                     "（JXL_ENC_FRAME_SETTING_QPROGRESSIVE_AC，"
                                                     "jxl/encode.h:238-242）：-1=编码器自选（库默认）、0=关闭、"
                                                     "1=开启。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "progressive_dc", .label = "DC 渐进级数",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 2, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "VarDCT DC 低分辨率渐进层"
                                                     "（JXL_ENC_FRAME_SETTING_PROGRESSIVE_DC，"
                                                     "jxl/encode.h:244-248）：-1=编码器自选（库默认）、0=关闭、"
                                                     "1=额外 64×64 层、2=512×512 与 64×64 层。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "resampling", .label = "编码前降采样",
                                          .type = ParamType::Enum, .def = int64_t(-1),
                                          .choices = {{"auto", int64_t(-1)},
                                                      {"1", int64_t(1)},
                                                      {"2", int64_t(2)},
                                                      {"4", int64_t(4)},
                                                      {"8", int64_t(8)}},
                                          .advanced = true,
                                          .tooltip = "编码前降采样（JXL_ENC_FRAME_SETTING_RESAMPLING，"
                                                     "jxl/encode.h:140-146）：-1=编码器自选（库默认，低质量时才"
                                                     "降采样）、1=不降采样、2/4/8=按倍数降采样并在解码端升采样；"
                                                     "会降低有效分辨率。VarDCT/Modular 通用。",
                                          .visible = not_lossless, .locked = {}},
                                 ParamDef{.key = "group_order", .label = "256×256 组顺序",
                                          .type = ParamType::Enum, .def = int64_t(-1),
                                          .choices = {{"auto", int64_t(-1)},
                                                      {"scanline", int64_t(0)},
                                                      {"center", int64_t(1)}},
                                          .advanced = true,
                                          .tooltip = "256×256 区域在码流中的存放顺序"
                                                     "（JXL_ENC_FRAME_SETTING_GROUP_ORDER，jxl/encode.h:210-214）："
                                                     "-1=编码器默认、0=扫描线顺序、1=中心优先（影响渐进渲染），"
                                                     "不改变画质。VarDCT/Modular 通用。",
                                          .visible = not_lossless, .locked = {}},
                                 lossless_param("JxlEncoderSetFrameLossless(true)（jxl/encode.h:1411-"
                                                "1424，库会强制走 Modular 路径）"),
                              } },
                    TechDef{ .id = "modular", .label = "Modular", .lossless_capable = true,
                             .params = {
                                 ParamDef{.key = "effort", .label = "编码努力",
                                          .type = ParamType::Int, .def = int64_t(7), .lo = 1,
                                          .hi = 10, .step = 1, .choices = {}, .advanced = false,
                                          .tooltip = "编码努力 effort（JXL_ENC_FRAME_SETTING_EFFORT，"
                                                     "jxl/encode.h:126-132）：1 lightning … 7 squirrel"
                                                     "（库默认）… 10 glacier，不影响解码速度。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "decoding_speed", .label = "解码速度预算",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 4, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "解码速度预算（JXL_ENC_FRAME_SETTING_DECODING_SPEED，"
                                                     "jxl/encode.h:134-138）：0=最慢解码但密度最高（库默认）… "
                                                     "4=最快解码。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "codestream_level", .label = "码流级别",
                                          .type = ParamType::Enum, .def = int64_t(10),
                                          .choices = {{"auto", int64_t(-1)},
                                                      {"5", int64_t(5)},
                                                      {"10", int64_t(10)}},
                                          .advanced = true,
                                          .tooltip = "JPEG XL 码流级别（JxlEncoderSetCodestreamLevel，"
                                                     "jxl/encode.h:1348-1383）：-1=自动（库默认，按 basic "
                                                     "info 选择）、5=兼容性最广、10=解除限制（CMYK/32bit）；"
                                                     "catalog 默认 10。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "distance", .label = "视觉距离",
                                          .type = ParamType::Float, .def = 0.0, .lo = 0.0,
                                          .hi = 15.0, .step = 0.1, .choices = {}, .advanced = false,
                                          .tooltip = "Modular 视觉距离（同一 JxlEncoderSetFrameDistance，"
                                                     "jxl/encode.h:1428-1442）：0=完全无损（需 "
                                                     "JxlEncoderSetFrameLossless）、>0=有损 Modular；"
                                                     "catalog 上限 15，库允许 0–25。",
                                          .visible = {}, .locked = lock_zero_when_lossless},
                                 ParamDef{.key = "color_transform", .label = "色彩变换",
                                          .type = ParamType::Enum, .def = std::string("YCoCg"),
                                          .choices = {{"None", std::string("None")},
                                                      {"YCoCg", std::string("YCoCg")},
                                                      {"XYB", std::string("XYB")},
                                                      {"YCbCr", std::string("YCbCr")}},
                                          .advanced = true,
                                          .tooltip = "色彩变换：头文件分属两个选项——"
                                                     "JXL_ENC_FRAME_SETTING_COLOR_TRANSFORM（-1 默认、"
                                                     "0=XYB、1=none、2=YCbCr，jxl/encode.h:272-277）与 "
                                                     "JXL_ENC_FRAME_SETTING_MODULAR_COLOR_SPACE（-1 默认、"
                                                     "0–41=RCT 索引、6=YCoCg，jxl/encode.h:279-286）；"
                                                     "XYB/None/YCbCr 走前者，YCoCg 走后者；无损需可逆变换"
                                                     "（None 或 YCoCg），YCbCr 不做事后变换但声明数据为 YCbCr。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "modular_group_size", .label = "组尺寸",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 3, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Modular 组尺寸"
                                                     "（JXL_ENC_FRAME_SETTING_MODULAR_GROUP_SIZE，"
                                                     "jxl/encode.h:288-290）：-1=默认、0=128、1=256、"
                                                     "2=512、3=1024。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "modular_predictor", .label = "预测器",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 15, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Modular 预测器"
                                                     "（JXL_ENC_FRAME_SETTING_MODULAR_PREDICTOR，"
                                                     "jxl/encode.h:292-297）：-1=默认；0 zero、1 left、"
                                                     "5 gradient … 15 mix everything。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "modular_palette_colors", .label = "调色板颜色数上限",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 4096, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "调色板（JXL_ENC_FRAME_SETTING_PALETTE_COLORS，"
                                                     "jxl/encode.h:262-265）：-1=编码器默认、0=关闭、"
                                                     ">0=调色板颜色数上限（头文件未规定上限，4096 为 UI 上限）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "modular_lossy_palette", .label = "有损调色板",
                                          .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                          .tooltip = "Delta palette 有损调色板"
                                                     "（JXL_ENC_FRAME_SETTING_LOSSY_PALETTE，"
                                                     "jxl/encode.h:267-270）：-1=默认、0=关闭、1=开启；"
                                                     "有损，无损档保持关闭。",
                                          .visible = {}, .locked = lock_false_when_lossless},
                                 ParamDef{.key = "brotli_effort", .label = "Brotli 努力",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 11, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Brotli 努力（JXL_ENC_FRAME_SETTING_BROTLI_EFFORT，"
                                                     "jxl/encode.h:329-334）：-1=默认（brob box 取 4）、"
                                                     "0 最快 … 11 最慢；头文件注明其服务 JPEG 重压缩与"
                                                     "元数据 box，与 modular 编码无关。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "responsive", .label = "响应式渐进",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "Modular 模式的响应式/渐进编码"
                                                     "（JXL_ENC_FRAME_SETTING_RESPONSIVE，jxl/encode.h:227-230，"
                                                     "头文件语义为 modular）；库默认 -1（编码器自选），"
                                                     "Bool 无法表达 -1。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "channel_colors_global_percent", .label = "全局调色板阈值 %",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "全图通道调色板阈值（JXL_ENC_FRAME_SETTING_"
                                                     "CHANNEL_COLORS_GLOBAL_PERCENT，jxl/encode.h:250-254）："
                                                     "-1=编码器默认、0–100=颜色数低于该百分比时启用全局调色板。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "channel_colors_group_percent", .label = "局部调色板阈值 %",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "每组通道调色板阈值（JXL_ENC_FRAME_SETTING_"
                                                     "CHANNEL_COLORS_GROUP_PERCENT，jxl/encode.h:256-260）："
                                                     "-1=编码器默认、0–100=颜色数低于该百分比时启用局部调色板。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "modular_ma_tree_learning_percent", .label = "MA 树学习比例 %",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 200, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "MA 树学习的像素比例（JXL_ENC_FRAME_SETTING_"
                                                     "MODULAR_MA_TREE_LEARNING_PERCENT，jxl/encode.h:299-303）："
                                                     "-1=默认（50）、0=不做 MA 且解码更快、100=全部、>100 亦允许"
                                                     "（更耗内存）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "modular_nb_prev_channels", .label = "前序通道属性数",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 11, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "参与 MA 树的前序通道数（JXL_ENC_FRAME_SETTING_"
                                                     "MODULAR_NB_PREV_CHANNELS，jxl/encode.h:305-311）："
                                                     "-1=默认、0–11 合法；推荐 0–3。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "resampling", .label = "编码前降采样",
                                          .type = ParamType::Enum, .def = int64_t(-1),
                                          .choices = {{"auto", int64_t(-1)},
                                                      {"1", int64_t(1)},
                                                      {"2", int64_t(2)},
                                                      {"4", int64_t(4)},
                                                      {"8", int64_t(8)}},
                                          .advanced = true,
                                          .tooltip = "编码前降采样（JXL_ENC_FRAME_SETTING_RESAMPLING，"
                                                     "jxl/encode.h:140-146）：-1=编码器自选（库默认，低质量时才"
                                                     "降采样）、1=不降采样、2/4/8=按倍数降采样并在解码端升采样；"
                                                     "会降低有效分辨率。VarDCT/Modular 通用。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "group_order", .label = "256×256 组顺序",
                                          .type = ParamType::Enum, .def = int64_t(-1),
                                          .choices = {{"auto", int64_t(-1)},
                                                      {"scanline", int64_t(0)},
                                                      {"center", int64_t(1)}},
                                          .advanced = true,
                                          .tooltip = "256×256 区域在码流中的存放顺序"
                                                     "（JXL_ENC_FRAME_SETTING_GROUP_ORDER，jxl/encode.h:210-214）："
                                                     "-1=编码器默认、0=扫描线顺序、1=中心优先（影响渐进渲染），"
                                                     "不改变画质。VarDCT/Modular 通用。",
                                          .visible = {}, .locked = {}},
                                 lossless_param("JxlEncoderSetFrameLossless(true)（jxl/encode.h:1411-"
                                                "1424；Modular+lossless 即真无损）"),
                              } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
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
                                          .visible = {}, .locked = {}},
                              } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "tiff", .label = "TIFF", .ext = "tif",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "codec", .label = "Compression", .lossless_capable = true,
                             .params = {
                                 ParamDef{.key = "compression", .label = "压缩方案（技术选择器）",
                                          .type = ParamType::Enum, .def = std::string("lzw"),
                                          .choices = {{"none", std::string("none")},
                                                      {"lzw", std::string("lzw")},
                                                      {"zip", std::string("zip")},
                                                      {"ccittrle", std::string("ccittrle")},
                                                      {"packbits", std::string("packbits")}},
                                          .advanced = false,
                                          .tooltip = "压缩方案（OIIO 属性 compression，"
                                                     "src/tiff.imageio/tiffoutput.cpp:660-673）。OIIO 3.1.14.0 "
                                                     "输出名表：none/lzw/zip/ccittrle/packbits"
                                                     "（tiffoutput.cpp:301-338）；deflate 是 zip 的别名（OIIO 只"
                                                     "认 zip）；未知名静默回退 deflate（tiffoutput.cpp:339-344）。"
                                                     "库默认 zip（tiffoutput.cpp:662），本表默认 lzw。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "deflate_level", .label = "Deflate 级别",
                                          .type = ParamType::Int, .def = int64_t(6), .lo = 1,
                                          .hi = 9, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Deflate 级别（OIIO 属性 tiff:zipquality，"
                                                     "tiffoutput.cpp:694-702，钳制 1–9）；也可用 "
                                                     "compression=\"zip:9\" 形式（ImageSpec::"
                                                     "decode_compression_metadata，OpenImageIO/imageio.h:756-760）。"
                                                     "compression=zip 时可见。",
                                          .visible = compression_is_zip, .locked = {}},
                                 ParamDef{.key = "predictor", .label = "预测器",
                                          .type = ParamType::Enum, .def = int64_t(2),
                                          .choices = {{"none", int64_t(1)},
                                                      {"horizontal", int64_t(2)},
                                                      {"float", int64_t(3)}},
                                          .advanced = true,
                                          .tooltip = "Predictor（OIIO 属性 tiff:predictor，取整数，"
                                                     "tiffoutput.cpp:1018-1020；libtiff PREDICTOR_NONE/"
                                                     "HORIZONTAL/FLOATINGPOINT=1/2/3，tiff.h:304-306）。OIIO 在 "
                                                     "lzw/deflate 且 8/16bit 时自动使用 horizontal"
                                                     "（tiffoutput.cpp:677-693）。",
                                          .visible = compression_is_lzw_or_zip, .locked = {}},
                                 ParamDef{.key = "tiff_tile_width", .label = "Tile 宽度",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 4096, .step = 16, .choices = {}, .advanced = true,
                                          .tooltip = "TIFF tile 宽度（OIIO 经 ImageSpec::tile_width 设置，"
                                                     "writer 无 tiff:tilewidth 属性；tiffoutput.cpp:480-487,"
                                                     "575-577）：0=条带扫描 strip（默认）、>0 时须为 16 的倍数"
                                                     "且 tile 高同为 16 的倍数，否则 writer 直接报错"
                                                     "（tiffoutput.cpp:481-485）；4096 为 UI 上限。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "tiff_tile_height", .label = "Tile 高度",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 4096, .step = 16, .choices = {}, .advanced = true,
                                          .tooltip = "TIFF tile 高度（OIIO 经 ImageSpec::tile_height 设置，"
                                                     "writer 无 tiff:tileheight 属性；tiffoutput.cpp:480-487,"
                                                     "575-577）：0=条带扫描 strip（默认）、>0 时须与 tile 宽"
                                                     "同为 16 的倍数（tiffoutput.cpp:481-485）；4096 为 UI 上限。",
                                          .visible = {}, .locked = {}},
                              } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "webp", .label = "WebP", .ext = "webp",
            .backends = { BackendDef{
                .id = "libwebp", .label = "libwebp", .runtime_introspected = false,
                .techs = {
                    TechDef{ .id = "lossy", .label = "Lossy", .lossless_capable = false,
                             .params = {
                                 ParamDef{.key = "quality", .label = "质量",
                                          .type = ParamType::Int, .def = int64_t(90), .lo = 1,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = false,
                                          .tooltip = "有损质量（WebPConfig::quality，webp/encode.h:99-103；"
                                                     "WebPValidateConfig 校验 0–100）：0=最小体积、100=最大。"
                                                     "库默认 75（webp/encode.h:175-179），本表取 90"
                                                     "（视觉透明档）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "sharp_yuv", .label = "锐利色度上采样",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = false,
                                          .tooltip = "锐利色度上采样（WebPConfig::use_sharp_yuv，"
                                                     "webp/encode.h:151）：更锐利但更慢的 RGB→YUV 转换；"
                                                     "WebPConfigInit 默认 0，本表取 true（视觉透明档）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "method", .label = "压缩方法",
                                          .type = ParamType::Int, .def = int64_t(4), .lo = 0,
                                          .hi = 6, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "质量/速度权衡（WebPConfig::method，webp/encode.h:104；"
                                                     "校验 0–6）：0 最快 … 6 最慢最好，库默认 4。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "preset", .label = "内容预设",
                                          .type = ParamType::Enum, .def = int64_t(2),
                                          .choices = {{"default", int64_t(0)},
                                                      {"picture", int64_t(1)},
                                                      {"photo", int64_t(2)},
                                                      {"drawing", int64_t(3)},
                                                      {"icon", int64_t(4)},
                                                      {"text", int64_t(5)}},
                                          .advanced = true,
                                          .tooltip = "内容预设（WebPPreset，webp/encode.h:157-166），经 "
                                                     "WebPConfigPreset 应用：库默认 DEFAULT(0)，本表取 "
                                                     "photo(2)。预设只调整有损参数（photo：sns 80、filter 30、"
                                                     "sharpness 3，src/enc/config_enc.c）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "sns_strength", .label = "空间噪声整形",
                                          .type = ParamType::Int, .def = int64_t(50), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "空间噪声整形（WebPConfig::sns_strength，"
                                                     "webp/encode.h:113）：0=关闭、100=最强；库默认 50、"
                                                     "photo 预设 80（src/enc/config_enc.c）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "filter_strength", .label = "去噪滤波",
                                          .type = ParamType::Int, .def = int64_t(30), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "去块滤波强度（WebPConfig::filter_strength，"
                                                     "webp/encode.h:114）：0=关闭、100=最强；库默认 60、"
                                                     "photo 预设 30（src/enc/config_enc.c）；本表默认 30"
                                                     "（preset=photo 的生效值），0 时 filter_type 无影响。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "autofilter", .label = "自动滤波",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "自动调整滤波强度（WebPConfig::autofilter，"
                                                     "webp/encode.h:118，取值 0/1）；库默认 0。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "pass", .label = "通道数",
                                          .type = ParamType::Int, .def = int64_t(1), .lo = 1,
                                          .hi = 10, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "熵分析通道数（WebPConfig::pass，webp/encode.h:125，"
                                                     "范围 1–10）；库默认 1，增加可略提升压缩率但更慢。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "filter_sharpness", .label = "滤波锐度",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 7, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "滤波锐度（WebPConfig::filter_sharpness，webp/encode.h:115，"
                                                     "0=最锐 … 7=最钝）；库默认 0、photo 预设 3"
                                                     "（src/enc/config_enc.c）；仅 filter_strength>0 或 "
                                                     "autofilter 时生效。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "filter_type", .label = "滤波类型（强/简）",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "滤波类型（WebPConfig::filter_type，webp/encode.h:116-117）："
                                                     "false=simple(0)、true=strong(1，U/V 也滤波)；库默认 1；"
                                                     "仅 filter_strength>0 或 autofilter 时生效。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "segments", .label = "段数",
                                          .type = ParamType::Int, .def = int64_t(4), .lo = 1,
                                          .hi = 4, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "最大分段数（WebPConfig::segments，webp/encode.h:112，"
                                                     "[1..4]）；库默认 4，photo 预设 4、text 预设 2"
                                                     "（src/enc/config_enc.c）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "alpha_compression", .label = "Alpha 压缩",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "Alpha 平面压缩（WebPConfig::alpha_compression，"
                                                     "webp/encode.h:119-120）：true=WebP 无损压缩(1)、false=不压缩(0)；"
                                                     "库默认 1；作用于有损路径的 Alpha 编码（src/enc/alpha_enc.c:385）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "alpha_filtering", .label = "Alpha 预测滤波",
                                          .type = ParamType::Int, .def = int64_t(1), .lo = 0,
                                          .hi = 2, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Alpha 预测滤波（WebPConfig::alpha_filtering，"
                                                     "webp/encode.h:121-122）：0=none、1=fast（库默认）、2=best；"
                                                     "头文件文档值为 0–2（WebPValidateConfig 仅校验 ≥0）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "alpha_quality", .label = "Alpha 质量",
                                          .type = ParamType::Int, .def = int64_t(100), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Alpha 平面质量（WebPConfig::alpha_quality，"
                                                     "webp/encode.h:123-124）：0=最小体积 … 100=无损（库默认 100）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "partitions", .label = "Token 分区数 log2",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 3, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Token 分区数（WebPConfig::partitions，webp/encode.h:131-132，"
                                                     "log2 值 [0..3]）；库默认 0（更易渐进解码）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "partition_limit", .label = "分区退化上限",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "为满足 512k 预测模式编码上限允许的质量退化"
                                                     "（WebPConfig::partition_limit，webp/encode.h:133-135）："
                                                     "0=不退化（库默认）… 100=最大退化。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "preprocessing", .label = "预处理滤波",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 7, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "预处理滤波（WebPConfig::preprocessing，webp/encode.h:129-130）："
                                                     "0=none（库默认）、1=segment-smooth、2=伪随机抖动，按位组合至 7"
                                                     "（WebPValidateConfig 允许 0–7）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "qmin", .label = "最小量化因子",
                                          .type = ParamType::Int, .def = int64_t(0), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "量化因子下限（WebPConfig::qmin，webp/encode.h:153）；"
                                                     "库默认 0，须满足 qmin≤qmax（WebPValidateConfig）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "qmax", .label = "最大量化因子",
                                          .type = ParamType::Int, .def = int64_t(100), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "量化因子上限（WebPConfig::qmax，webp/encode.h:154）；"
                                                     "库默认 100，须满足 qmin≤qmax（WebPValidateConfig）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "emulate_jpeg_size", .label = "模拟 JPEG 体积",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "重映射压缩参数以贴近同质量 JPEG 体积"
                                                     "（WebPConfig::emulate_jpeg_size，webp/encode.h:136-139）；"
                                                     "库默认 false；有损路径。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "low_memory", .label = "低内存模式",
                                          .type = ParamType::Bool, .def = false, .choices = {},
                                          .advanced = true,
                                          .tooltip = "降低内存占用（WebPConfig::low_memory，webp/encode.h:141，"
                                                     "代价是更多 CPU）；库默认 false；作用于有损 VP8 路径"
                                                     "（src/enc/webp_enc.c:119）。",
                                          .visible = {}, .locked = {}},
                                 lossless_param("WebPConfig::lossless = 1（webp/encode.h:97-98；"
                                                "有损技术下该参数为 false）"),
                              } },
                    TechDef{ .id = "lossless", .label = "Lossless", .lossless_capable = true,
                             .params = {
                                 ParamDef{.key = "quality", .label = "努力（借 quality 参数）",
                                          .type = ParamType::Int, .def = int64_t(80), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = false,
                                          .tooltip = "无损模式下 quality 表示压缩努力"
                                                     "（WebPConfig::quality，webp/encode.h:99-103）：0 最快、"
                                                     "100 最小体积。WebPConfigLosslessPreset(level 6) 给出 "
                                                     "method 4 + quality 75（src/enc/config_enc.c）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "exact", .label = "保留透明区 RGB 原值",
                                          .type = ParamType::Bool, .def = true, .choices = {},
                                          .advanced = true,
                                          .tooltip = "保留透明区 RGB 原值（WebPConfig::exact，"
                                                     "webp/encode.h:145-148）：0=丢弃以获得更好压缩（库默认）；"
                                                     "本表取 true（视觉透明档）。",
                                          .visible = is_lossless, .locked = {}},
                                 ParamDef{.key = "method", .label = "压缩方法",
                                          .type = ParamType::Int, .def = int64_t(4), .lo = 0,
                                          .hi = 6, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "质量/速度权衡（WebPConfig::method，webp/encode.h:104）："
                                                     "0 最快 … 6 最好；WebPConfigLosslessPreset(6) 亦取 4。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "near_lossless", .label = "近无损强度",
                                          .type = ParamType::Int, .def = int64_t(100), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "近无损（WebPConfig::near_lossless，webp/encode.h:143-144）："
                                                     "0=最大颜色改动 … 100=关闭（库默认 100）；<100 时启用 VP8L "
                                                     "近无损预处理、输出非严格无损（src/enc/vp8l_enc.c:"
                                                     "1105-1117,1572-1577）。",
                                          .visible = {}, .locked = {}},
                                 lossless_param("WebPConfig::lossless = 1（webp/encode.h:97-98）"
                                                "或 tech_id=\"lossless\"；两者一致时无损生效"),
                              } } } } },
            .bitdepths = {8},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "bmp", .label = "BMP", .ext = "bmp",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "raw", .label = "Raw", .lossless_capable = true,
                             .params = {} } } } },
            .bitdepths = {24},
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "none" },
        FormatDef{
            .id = "heif", .label = "HEIF", .ext = "heic",
            .backends = { BackendDef{
                .id = "x265", .label = "x265", .runtime_introspected = true,
                .techs = {} } },
            .bitdepths = {8, 10, 12},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "libheif" },
        FormatDef{
            .id = "avif", .label = "AVIF", .ext = "avif",
            .backends = {
                BackendDef{ .id = "svt-av1", .label = "SVT-AV1", .runtime_introspected = true,
                            .techs = {} },
                BackendDef{ .id = "libaom", .label = "libaom", .runtime_introspected = true,
                            .techs = {} } },
            .bitdepths = {8, 10, 12},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "libheif" },
    };
    return fmts;
}

} // namespace pp
