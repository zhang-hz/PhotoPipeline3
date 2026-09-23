// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — encoder interface (M0 frozen)
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.1 **已落地**（依据 docs/v0.3.0-design.md §3.1）
//   落地任务 = W1-T5（多格式管线重排）；原标注 PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记
//   （冻结头 SPDX 延续）。本文件其余既有声明（MetadataPayloads）不在裁定表内 → 维持 PP-FROZEN(M0)。
//   §3.1 整节标注 [重排]：结构重排（OutputTarget 每输出一份 + EncodeRequest 以 target 取代
//   params/out_bitdepth/out_path），聚合初始化不再兼容（随本次解冻一次性接受）。
//
//   §3.1 正文义务（逐字抄录，下同：行首 "// " 与折行均为注释包装，正文为单段）：
// clang-format off
// 线程映射义务（E3）：`encode_threads` 必须映射到库 API——jxl=`JxlThreadParallelRunner(E)`（E=1 时 nullptr）；heif/avif=插件 `threads` 参数（内省名核对，缺则记 R4 口径）；webp=`thread_level = E>1`；jpegli/oiio=无内部线程（如实 `progress_reported` 与此无关，但日志注明 E 不生效）。
// clang-format on
//   W1-T5 接线状态（列在 T7 兑现）：pipeline 本任务恒传 encode_threads=1、progress=空回调，
//   故各编码器当前的线程行为与 0.2 逐字一致（jxl nullptr runner / webp thread_level=0 /
//   heif threads=1 / jpegli·oiio 无内部线程）；映射点已逐处标注 "T7(E3)" 注释。
#pragma once
#include "core/params.h"
#include "core/types.h"
#include <OpenImageIO/imagebuf.h>
#include <filesystem>
#include <functional>

namespace pp {

struct MetadataPayloads {
    std::string exif_blob;   // TIFF blob (ExifData::copy, littleEndian)
    std::string xmp_rdf;     // XMP RDF xml
    std::string icc_profile; // target or as-is profile bytes
};

// —— 0.3.0 冻结形态（设计 §3.1 逐字）——
struct OutputTarget {       // 每输出格式一份（替代单 format/bitdepth）
    std::string format_id;  // "jpeg" / "jxl" / ...
    std::string backend_id; // "svt-av1" / "libaom" / ...
    std::string tech_id;    // "vardct" / "modular" / ...
    ParamSet params;        // 已按 schema 校验+锁定
    int out_bitdepth = 8;
    std::filesystem::path out_path; // fsops::output_path() 预计算结果
    bool supports_alpha;            // 影响 flatten 编排（§4.3）
};

using ProgressFn = std::function<void(float)>; // 编码内进度 0..1；可空

struct EncodeRequest {
    OIIO::ImageBuf &img;        // float32，通道 1/2/3/4（orient+color 后）
    const OutputTarget &target; // [重排] 替代 params/outBitDepth/outPath
    const MetadataPayloads &meta;
    std::function<bool()> cancelled;
    ProgressFn progress; // 追加：真实行级进度（尽力而为）
    int encode_threads;  // 追加：E3 分配的内部线程数（≥1）
};

struct EncodeResult {
    size_t bytes;
    std::vector<Warning> warnings;
    Timing t;
    std::string error;              // 0.2 起已有，保留
    bool progress_reported = false; // 追加：false → UI 标合成进度
};

// 0.3.0 形态：encode() 单输出一次调用（一个 EncodeRequest.target = 一次调用）。
// 线程映射义务（E3）见本文件头部 §3.1 正文抄录。
class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual const FormatDef &format() const = 0;
    virtual EncodeResult encode(const EncodeRequest &) = 0;
};

} // namespace pp
