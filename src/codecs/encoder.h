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
    // R-163（M7-WP2）：本字段**被编码器真实消费** —— 各编码器在编码循环内检查并提前退出
    // （返回 `EncodeResult{bytes=0, error="encode cancelled"}`，写盘前退出 ⇒ 无半成品）。
    // 空 std::function = 永不取消（0.2/0.3.0 现状），语义幂等。
    std::function<bool()> cancelled;
    ProgressFn progress; // 追加：真实行级进度（尽力而为）
    // R-111（M7-WP2）：默认值 = 1 —— 与契约"≥1"及 pipeline.h 的同名默认一致；聚合初始化
    // 省略本字段时得 1（旧行为得 0，与契约矛盾）。
    int encode_threads = 1; // 追加：E3 分配的内部线程数（≥1）
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
//
// —— M7-WP8 / B.7 · R-162 错误收尾口径（契约注释；实现面单源 = codecs/enc_common.h）——
//   * 失败 = `EncodeResult{bytes=0, error≠空, t 已填}`（成败皆填 Timing，统一 finish 口径）；
//     异常不得穿越 IEncoder 边界（E8 原文不变）。
//   * **错误级日志归 pipeline**（pipeline.cpp 的 `encode failed` 行）：编码器内部不再
//     log_error（消 heif/oiio 旧"失败双记"）；E9 参数面的 log_warn（unknown/ignored 参数）
//     保留 —— 那是配置缺陷证据，不是编码失败。
//   * 像素取样单口径（R-165）：整帧走 img.roi()（内存型：jpegli/jxl/webp 的 fetch_raster），
//     逐行走 roi 行窗（流式：heif/oiio 的 fetch_row）；裁剪视图下两者取同一像素面。
//   * 落盘单口径（R-167）：内存型产物一律 enc::write_file（= fsops::write_atomic_file）；
//     库句柄清理用 enc::on_scope_exit。
class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual const FormatDef &format() const = 0;
    virtual EncodeResult encode(const EncodeRequest &) = 0;
};

} // namespace pp
