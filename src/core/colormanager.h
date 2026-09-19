// PP-FROZEN(file)
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

}  // namespace pp
