// PP-FROZEN(file)
#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "codecs/encoder.h"
#include "core/colormanager.h"
#include "core/fsops.h"
#include "core/metadata.h"
#include "core/params.h"
#include "core/pixelbudget.h"
#include "core/types.h"

namespace pp {

struct FileEntry {
    std::filesystem::path src;
    std::filesystem::path base_dir;         // 镜像路径基准
    std::optional<MetadataOverride> exception;
    ImageInfo info;                          // probe 后回填
    bool probe_done = false;
};

struct RunConfig {
    // 输出
    std::filesystem::path out_root;
    std::string format_id, backend_id, tech_id;
    bool lossless = false;
    ParamSet params;
    int out_bitdepth = 8;
    ColorTarget color_target = ColorTarget::KeepOriginal;
    ConflictPolicy conflict = ConflictPolicy::Rename;
    bool rotate_orientation = true;
    double flatten_gray = 1.0;               // alpha 合成底色（0=黑..1=白）
    // 元数据
    BatchRules rules;
    bool metadata_only = false;
    // 运行
    int workers = 0;                         // 0=物理核数
    uint64_t budget_bytes = 0;               // 0=default_capacity_bytes()
};

struct FileResult {
    std::filesystem::path src, out;
    ImageInfo info;
    Timing t;
    uint64_t out_bytes = 0;
    std::vector<Warning> warnings;
    std::string error;
    std::string color_src, color_dst;
    bool ok = false, skipped = false, cancelled = false;
};

enum class FileState { Queued, Probing, Decoding, Orienting, Coloring, Flattening,
                       Encoding, Writing, Done, Skipped, Failed, Cancelled };

struct FileEvent {
    std::size_t index = 0;
    FileState state = FileState::Queued;
    const FileResult* result = nullptr;      // 仅在终态（Done/Skipped/Failed/Cancelled）非空
};

// 单文件全流程（线程内串行）；budget 可为 nullptr（仅元数据模式不需要）
// reserved 为本批次已分配输出路径（批内冲突）；返回值带终态
FileResult run_one_file(FileEntry& fe, const RunConfig& cfg, IEncoder* enc,
                        PixelBudget* budget, const std::vector<std::filesystem::path>& reserved,
                        const std::function<bool()>& cancelled,
                        const std::function<void(FileState)>& on_stage);

// 仅元数据模式（零重编码）：仅 JPEG/PNG/TIFF/WebP；HEIF/AVIF/JXL 由上层拒绝
FileResult run_metadata_only(FileEntry& fe, const RunConfig& cfg,
                             const std::vector<std::filesystem::path>& reserved,
                             const std::function<bool()>& cancelled,
                             const std::function<void(FileState)>& on_stage);

// 输入白名单校验（唯一入口，UI 与 harness 共用）
bool format_supports_metadata_only(std::string_view format_id);

}  // namespace pp
