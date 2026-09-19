// PP-FROZEN(structure): 只允许替换 /* PP-PLACEHOLDER */ 注释处的内容
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/params.h"

namespace pp {

const std::vector<FormatDef>& static_formats() {
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
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "quality", .label = "质量",
                                 .type = ParamType::Int, .def = int64_t(90), .lo = 1, .hi = 100,
                                 .step = 1, .choices = {}, .advanced = false,
                                 .tooltip = "libjpeg 兼容质量分（jpegli_set_quality，jpegli/encode.h:51）；"
                                            "jpegli 参考 CLI 默认 90、允许 1–100（tools/cjpegli.cc:118,133）。"
                                            "quality_mode=quality 时可见。",
                                 .visible = {}, .locked = {}},
                        ParamDef{.key = "chroma", .label = "色度采样",
                                 .type = ParamType::Enum, .def = std::string("444"),
                                 .choices = {{"444", std::string("444")},
                                             {"422", std::string("422")},
                                             {"420", std::string("420")}},
                                 .advanced = false,
                                 .tooltip = "色度采样（chroma subsampling），按 jpeglib.h:125-126 的 "
                                            "comp_info[].h_samp_factor/v_samp_factor 设置；参考 CLI 还接受 "
                                            "440（tools/cjpegli.cc:66）。444=不降采样（视觉透明档）。",
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
                                 .visible = {}, .locked = {}},
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
                                          .type = ParamType::Float, .def = 1.0, .lo = 0.0,
                                          .hi = 25.0, .step = 0.1, .choices = {}, .advanced = false,
                                          .tooltip = "目标 Butteraugli 距离（JxlEncoderSetFrameDistance，"
                                                     "jxl/encode.h:1428-1442）：范围 0–25、库默认 1.0、"
                                                     "1.0=视觉无损、推荐 0.5–3.0；0.0 需配合 "
                                                     "JxlEncoderSetFrameLossless 才是真无损。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "photon_noise", .label = "感光噪声模拟 (ISO)",
                                          .type = ParamType::Float, .def = 0.0, .lo = 0.0,
                                          .hi = 6400.0, .step = 100.0, .choices = {}, .advanced = true,
                                          .tooltip = "Photon noise 胶片颗粒模拟"
                                                     "（JXL_ENC_FRAME_SETTING_PHOTON_NOISE，"
                                                     "jxl/encode.h:166-171）：100=轻微、3200=明显，库默认 "
                                                     "0；头文件未规定上限，6400 为 UI 上限。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "epf", .label = "边缘保持滤波",
                                          .type = ParamType::Int, .def = int64_t(2), .lo = -1,
                                          .hi = 3, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "边缘保持滤波（JXL_ENC_FRAME_SETTING_EPF，"
                                                     "jxl/encode.h:189-192）：-1=编码器自选（库默认）、"
                                                     "0–3=强度；catalog 默认 2。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "keep_invisible", .label = "保留不可见像素",
                                          .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                          .tooltip = "保留不可见像素 RGB（JXL_ENC_FRAME_SETTING_KEEP_INVISIBLE，"
                                                     "jxl/encode.h:205-208）：库以 -1 表示默认（无损=1、"
                                                     "有损=0）；Bool 无法表达 -1，本表取 true（更保真）。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "responsive", .label = "响应式渐进",
                                          .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                          .tooltip = "响应式/渐进编码（JXL_ENC_FRAME_SETTING_RESPONSIVE，"
                                                     "jxl/encode.h:227-230），头文件注明用于 modular 模式；"
                                                     "库默认 -1（自选）。catalog 将其列于 VarDCT 组。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "use_dct4", .label = "启用 DCT 4×4 块",
                                          .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                          .tooltip = "DCT 4×4 块开关（DCT block size）。libjxl 0.11.2 的 "
                                                     "JxlEncoderFrameSettingId 无对应项"
                                                     "（jxl/encode.h:132-399），M0 保留占位、无后端映射。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "use_dct8", .label = "启用 DCT 8×8 块",
                                          .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                          .tooltip = "DCT 8×8 块开关（DCT block size）。libjxl 0.11.2 的 "
                                                     "JxlEncoderFrameSettingId 无对应项"
                                                     "（jxl/encode.h:132-399），M0 保留占位、无后端映射。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "use_dct16", .label = "启用 DCT 16×16 块",
                                          .type = ParamType::Bool, .def = true, .choices = {}, .advanced = true,
                                          .tooltip = "DCT 16×16 块开关（DCT block size）。libjxl 0.11.2 的 "
                                                     "JxlEncoderFrameSettingId 无对应项"
                                                     "（jxl/encode.h:132-399），M0 保留占位、无后端映射。",
                                          .visible = {}, .locked = {}},
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
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "color_transform", .label = "色彩变换",
                                          .type = ParamType::Enum, .def = std::string("YCoCg"),
                                          .choices = {{"None", std::string("None")},
                                                      {"YCoCg", std::string("YCoCg")},
                                                      {"XYB", std::string("XYB")}},
                                          .advanced = true,
                                          .tooltip = "色彩变换：头文件分属两个选项——"
                                                     "JXL_ENC_FRAME_SETTING_COLOR_TRANSFORM（-1 默认、"
                                                     "0=XYB、1=none、2=YCbCr，jxl/encode.h:272-277）与 "
                                                     "JXL_ENC_FRAME_SETTING_MODULAR_COLOR_SPACE（-1 默认、"
                                                     "0–41=RCT 索引、6=YCoCg，jxl/encode.h:279-286）；"
                                                     "无损需可逆变换（None 或 YCoCg）。",
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
                                 ParamDef{.key = "modular_palette", .label = "调色板",
                                          .type = ParamType::Enum, .def = std::string("auto"),
                                          .choices = {{"auto", std::string("auto")},
                                                      {"off", std::string("off")},
                                                      {"on", std::string("on")}},
                                          .advanced = true,
                                          .tooltip = "调色板（JXL_ENC_FRAME_SETTING_PALETTE_COLORS，"
                                                     "jxl/encode.h:262-265）：头文件为整数——-1=编码器默认、"
                                                     "0=关闭、>0=颜色数上限；auto→-1、off→0，on 的阈值"
                                                     "待裁决。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "modular_lossy_palette", .label = "有损调色板",
                                          .type = ParamType::Bool, .def = false, .choices = {}, .advanced = true,
                                          .tooltip = "Delta palette 有损调色板"
                                                     "（JXL_ENC_FRAME_SETTING_LOSSY_PALETTE，"
                                                     "jxl/encode.h:267-270）：-1=默认、0=关闭、1=开启；"
                                                     "有损，无损档保持关闭。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "brotli_effort", .label = "Brotli 努力",
                                          .type = ParamType::Int, .def = int64_t(-1), .lo = -1,
                                          .hi = 11, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Brotli 努力（JXL_ENC_FRAME_SETTING_BROTLI_EFFORT，"
                                                     "jxl/encode.h:329-334）：-1=默认（brob box 取 4）、"
                                                     "0 最快 … 11 最慢；头文件注明其服务 JPEG 重压缩与"
                                                     "元数据 box，与 modular 编码无关。",
                                          .visible = {}, .locked = {}},
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
                                                      {"deflate", std::string("deflate")},
                                                      {"zstd", std::string("zstd")},
                                                      {"packbits", std::string("packbits")},
                                                      {"jpeg", std::string("jpeg")}},
                                          .advanced = false,
                                          .tooltip = "压缩方案（OIIO 属性 compression，"
                                                     "src/tiff.imageio/tiffoutput.cpp:660-673）。OIIO 3.1.14.0 "
                                                     "输出名表为 none/lzw/zip/ccittrle/packbits"
                                                     "（tiffoutput.cpp:301-338），deflate 需写成 zip；zstd 不在"
                                                     "名表、jpeg 被 ENABLE_JPEG_COMPRESSION=0 编译掉"
                                                     "（tiffoutput.cpp:56,312-314），未知名回退 deflate"
                                                     "（tiffoutput.cpp:339-344）。库默认 zip。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "quality", .label = "质量（仅 jpeg-in-tiff）",
                                          .type = ParamType::Int, .def = int64_t(90), .lo = 1,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "JPEG-in-TIFF 质量（TIFFTAG_JPEGQUALITY，"
                                                     "tiffoutput.cpp:704-709，钳制 1–100）。本 OIIO 构建关闭 "
                                                     "JPEG 压缩（ENABLE_JPEG_COMPRESSION=0，tiffoutput.cpp:56），"
                                                     "compression=jpeg 会被改写为 zip（tiffoutput.cpp:664-672）："
                                                     "M0 无后端，待裁决。compression=jpeg 时可见。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "deflate_level", .label = "Deflate 级别",
                                          .type = ParamType::Int, .def = int64_t(6), .lo = 1,
                                          .hi = 9, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "Deflate 级别（OIIO 属性 tiff:zipquality，"
                                                     "tiffoutput.cpp:694-702，钳制 1–9）；也可用 "
                                                     "compression=\"zip:9\" 形式（ImageSpec::"
                                                     "decode_compression_metadata，OpenImageIO/imageio.h:756-760）。"
                                                     "compression=deflate 时可见。",
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "zstd_level", .label = "ZStd 级别",
                                          .type = ParamType::Int, .def = int64_t(9), .lo = 1,
                                          .hi = 22, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "ZStandard 级别。OIIO 3.1.14.0 的 TIFF 输出既无名表项也"
                                                     "无对应属性（tiffoutput.cpp:301-338 无 zstd；libtiff 侧有 "
                                                     "COMPRESSION_ZSTD=50000，tiff.h:216）：M0 无后端，待裁决。"
                                                     "compression=zstd 时可见。",
                                          .visible = {}, .locked = {}},
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
                                          .type = ParamType::Int, .def = int64_t(20), .lo = 0,
                                          .hi = 100, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "去块滤波强度（WebPConfig::filter_strength，"
                                                     "webp/encode.h:114）：0=关闭、100=最强；库默认 60、"
                                                     "photo 预设 30（src/enc/config_enc.c），catalog 默认 20。",
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
                                          .visible = {}, .locked = {}},
                                 ParamDef{.key = "method", .label = "压缩方法",
                                          .type = ParamType::Int, .def = int64_t(4), .lo = 0,
                                          .hi = 6, .step = 1, .choices = {}, .advanced = true,
                                          .tooltip = "质量/速度权衡（WebPConfig::method，webp/encode.h:104）："
                                                     "0 最快 … 6 最好；WebPConfigLosslessPreset(6) 亦取 4。",
                                          .visible = {}, .locked = {}},
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
            .bitdepths = {8, 10},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "libheif" },
        FormatDef{
            .id = "avif", .label = "AVIF", .ext = "avif",
            .backends = {
                BackendDef{ .id = "svt-av1", .label = "SVT-AV1", .runtime_introspected = true,
                            .techs = {} },
                BackendDef{ .id = "libaom", .label = "libaom", .runtime_introspected = true,
                            .techs = {} } },
            .bitdepths = {8, 10},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "libheif" },
    };
    return fmts;
}

}  // namespace pp
