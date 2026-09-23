// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — encoder interface (M0 frozen)
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.1（依据 docs/v0.3.0-design.md §3.1）
//   本文件内 OutputTarget / ProgressFn / EncodeRequest / EncodeResult / IEncoder 为 0.3.0
//   一次性解冻（D20）授权变更面；落地任务 = W1-T5（多格式管线重排）。
//   对应任务落地后：把本文件内的 PP-THAWED 标记改标为 PP-FROZEN(0.3.0)（冻结头 SPDX 延续）。
//   本文件其余既有声明（MetadataPayloads）不在裁定表内 → 维持 PP-FROZEN(M0) 只读。
//   §3.1 整节标注 [重排]：结构重排，不再保证聚合初始化兼容（随本次解冻一次性接受）。
//
//   §3.1 正文义务（逐字抄录，下同：行首 "// " 与折行均为注释包装，正文为单段）：
// clang-format off
// 线程映射义务（E3）：`encode_threads` 必须映射到库 API——jxl=`JxlThreadParallelRunner(E)`（E=1 时 nullptr）；heif/avif=插件 `threads` 参数（内省名核对，缺则记 R4 口径）；webp=`thread_level = E>1`；jpegli/oiio=无内部线程（如实 `progress_reported` 与此无关，但日志注明 E 不生效）。
// clang-format on
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

// PP-THAWED(0.3.0-M4-D20) §3.1 · OutputTarget + ProgressFn
//   0.3.0 新增（OutputTarget 每输出格式一份，替代 0.2 的单 format/bitdepth；本文件当前无此声明，
//   T5 落地时按下方形态新增）。落地任务 W1-T5 → 落地后改标 PP-FROZEN(0.3.0)。
//   [重排] 本行以下 0.3.0 冻结形态（设计 §3.1 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// // —— 0.3.0 冻结形态 ——
// struct OutputTarget {                          // 每输出格式一份（替代单 format/bitdepth）
//     std::string        format_id;              // "jpeg" / "jxl" / ...
//     std::string        backend_id;             // "svt-av1" / "libaom" / ...
//     std::string        tech_id;                // "vardct" / "modular" / ...
//     ParamSet           params;                 // 已按 schema 校验+锁定
//     int                out_bitdepth = 8;
//     std::filesystem::path out_path;            // fsops::output_path() 预计算结果
//     bool               supports_alpha;         // 影响 flatten 编排（§4.3）
// };
//
// using ProgressFn = std::function<void(float)>; // 编码内进度 0..1；可空
// clang-format on
// PP-THAWED(0.3.0-M4-D20) §3.1 · EncodeRequest
//   [重排] params/out_bitdepth/out_path 被 target 替代，并追加 progress/encode_threads
//   → 位置不兼容，聚合初始化不再兼容（0.3.0 一次性接受）。
//   落地任务 W1-T5 → 落地后改标 PP-FROZEN(0.3.0)。
//   0.3.0 冻结形态（设计 §3.1 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// struct EncodeRequest {
//     OIIO::ImageBuf&            img;            // float32，通道 1/2/3/4（orient+color 后）
//     const OutputTarget&        target;         // [重排] 替代 params/outBitDepth/outPath
//     const MetadataPayloads&    meta;
//     std::function<bool()>      cancelled;
//     ProgressFn                 progress;       // 追加：真实行级进度（尽力而为）
//     int                        encode_threads; // 追加：E3 分配的内部线程数（≥1）
// };
// clang-format on
struct EncodeRequest {
    OIIO::ImageBuf &img; // float32, channels {1,2,3,4}
    const ParamSet &params;
    int out_bitdepth = 8;
    const MetadataPayloads &meta;
    std::filesystem::path out_path;
    std::function<bool()> cancelled;
    // M1 (T6b, main-dialogue ruling): explicit tech selection. Additive field
    // appended at the end so existing positional aggregate initialisation of
    // the preceding members stays valid.
    std::string tech_id; // M1: "vardct"|"modular"|"lossy"|"lossless"|"runtime"|""（空 = 按 lossless
                         // 回退推断）
};

// PP-THAWED(0.3.0-M4-D20) §3.1 · EncodeResult
//   追加字段 progress_reported 落于结构体末尾（既有成员与语义不变）。
//   落地任务 W1-T5 → 落地后改标 PP-FROZEN(0.3.0)。
//   0.3.0 冻结形态（设计 §3.1 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// struct EncodeResult {
//     size_t                  bytes;
//     std::vector<Warning>    warnings;
//     Timing                  t;
//     std::string             error;             // 0.2 起已有，保留
//     bool        progress_reported = false;    // 追加：false → UI 标合成进度
// };
// clang-format on
//   注（抄录差异，非本表新增语义）：0.2 现形为 `uint64_t bytes = 0;`；§3.1 逐字形态为
//   `size_t bytes;`（无默认值）——T5 落地时以 §3.1 为准。
struct EncodeResult {
    uint64_t bytes = 0;
    std::vector<Warning> warnings;
    Timing t;
    // M1 (T7, main-dialogue ruling): non-empty = encode failed (bytes is then 0).
    // Additive field only; all preceding members and their semantics are unchanged.
    std::string error;
};

// PP-THAWED(0.3.0-M4-D20) §3.1 · IEncoder
//   0.3.0 形态：encode() 单输出一次调用（一个 EncodeRequest.target = 一次调用）。
//   落地任务 W1-T5 → 落地后改标 PP-FROZEN(0.3.0)。
//   0.3.0 冻结形态（设计 §3.1 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// class IEncoder {
//     virtual const FormatDef& format() const = 0;
//     virtual EncodeResult encode(const EncodeRequest&) = 0;   // 单输出一次调用
//     virtual ~IEncoder() = default;
// };
// clang-format on
//   线程映射义务（E3）见本文件头部 §3.1 正文抄录：encode_threads 必须映射到各后端库 API。
class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual const FormatDef &format() const = 0;
    virtual EncodeResult encode(const EncodeRequest &) = 0;
};

} // namespace pp
