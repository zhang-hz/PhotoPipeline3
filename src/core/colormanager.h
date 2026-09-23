// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <OpenImageIO/imagebuf.h>
#include <cstddef>
#include <string>
#include <vector>
#include "core/types.h"

namespace pp {

enum class ColorTarget { KeepOriginal, SRGB, DisplayP3, AdobeRGB };
std::string to_string(ColorTarget t);          // "keep" | "srgb" | "p3" | "adobergb"
bool parse_color_target(std::string_view s, ColorTarget& out);

struct ColorOutcome {
    std::vector<Warning> warnings;
    std::string error;
    std::string src_desc, dst_desc;   // 日志用："sRGB" / "ICC(Display P3)" / "assumed sRGB"
    std::string icc_to_embed;         // 应在输出中嵌入的 ICC 字节（空=不嵌）
};

class ColorManager {
public:
    static ColorManager& instance();

    // 变换 buf 的颜色通道为 target；alpha 通道原样保留（不使用 lcms2 的 RGBA 类型）
    // src_icc 为空时：src_is_gray → 灰度 sRGB 假定；否则 sRGB 假定 + Warning{NoIccAssumeSrgb}
    // target==KeepOriginal 时不改像素：src_icc 非空 → icc_to_embed=src_icc，否则空
    // 通道 1 → 彩色目标：lcms2 单调用升维（GRAY_FLT→RGB_FLT），无独立灰度管线（复审 S1）
    // 通道 2（灰+α）：变换灰度平面，α 复制
    // 输出目标 ICC：sRGB 用 lcms2 内建；P3/AdobeRGB 从 assets/icc/ 加载（缺失 → error）
    ColorOutcome transform(OIIO::ImageBuf& buf, const std::string& src_icc, bool src_is_gray,
                           ColorTarget target);

    void clear_cache();
    std::size_t cache_size() const;   // 测试用：应随 (src,target,channels,bitdepth) 复用

private:
    ColorManager();
    struct Impl;
    Impl* impl_;
};

// 目标 ICC 加载（assets/icc/ 下文件名固定）："DisplayP3.icc" / "AdobeRGB1998.icc"
std::string load_target_icc(ColorTarget t, std::string& err);  // sRGB → 空串（内建）

// —— M2-T6 §2.8：CICP（H.273）→ 源色彩描述最小映射（冻结枚举） ——
// 无嵌入 ICC 的 JXL 源：OIIO 以 `CICP int[4]` 暴露格式原生描述
// (primaries, transfer, matrix, full_range)；按 §2.8 **仅前两元参与映射**。
struct Cicp {
    int primaries  = 2;   // H.273 ColourPrimaries（2 = unspecified）
    int transfer   = 2;   // H.273 TransferCharacteristics（2 = unspecified）
    int matrix     = 2;   // 不参与映射（冻结口径）
    int full_range = 1;   // 不参与映射（冻结口径）
};

// §2.8 冻结枚举：恰好这四个已列组合可识别，其余（含 PQ 16 / HLG 18）一律未支持。
enum class CicpSource {
    Srgb,               // (1,13)  sRGB：显式，行为同 M1（src_icc 留空 → 既有假定路径）
    DisplayP3,          // (12,13) Display P3（D65，sRGB TRC）
    DisplayP3Gamma22,   // (12,1)  Display P3（D65，gamma 2.2）
    Bt2020Linear,       // (9,8)   BT.2020 linear
    Bt2020SrgbTrc,      // (9,13)  BT.2020 sRGB-TRC
    Unsupported,        // 未列组合：维持 sRGB 假定（不构造 profile）
};

struct CicpMapping {
    CicpSource source = CicpSource::Unsupported;
    std::string name;      // 命中时的 profile 名（"sRGB"/"Display P3"/"BT.2020 linear"/…）
    std::string src_icc;   // 源 ICC 字节；sRGB（lcms2 内建）与未支持为空 → ColorManager 假定 sRGB
    std::string log_line;  // 冻结日志文本：命中 `CICP (<p>,<t>) → <profile 名>`；未支持见 §2.8
    bool recognized() const { return source != CicpSource::Unsupported; }
};

// 纯函数（可单测）：§2.8 冻结枚举逐对判定，无其它组合；matrix/full_range 不参与。
CicpMapping map_cicp_source(const Cicp& cicp);

}  // namespace pp
