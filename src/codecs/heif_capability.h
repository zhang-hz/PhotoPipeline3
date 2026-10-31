// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — HEIF 编码能力纯核：HEVC Level 表 / Level 拟合 / grid 规划 / hvcC 回读。
//
// 为什么需要这个文件（0.3.6-alpha1 HEIF 档位收口）：
//   * x265 的 `level-idc` 是硬约束：图像 luma 样本数超过 Level 的 MaxLumaPs 时编码器
//     直接拒绝（`encoder/level.cpp` 的 enforceLevel），但旧实现的错误只在 x265 日志里，
//     用户看到的是 “heif_context_encode_image failed”。本头把 Level 表与拟合做成纯函数，
//     enc_heif 在编码前给出中文可操作错误。
//   * Apple HEIC 参考（super/T7.heic）用 1024×576 的 HEIF grid 把整图 Level 从 6.0 降
//     到 3.1。heif_plan_grid 是 grid 规划的纯函数单源：enc_heif 的有损 grid 路径用它
//     选 tile 尺寸/行列数，单元测试直接锁边界（T1+Level 3.1 → 4×11×1024×576）。
//   * 编码档位是否名副其实，最终以产物 hvcC 为准：heif_parse_hvcc 用最小 ISO-BMFF
//     box 扫描读取 general_profile_idc / level_idc / chromaFormat / bitDepthLuma。
//
// 事实源（已核验）：
//   * Level 的 MaxLumaPs 取自 x265 4.3 `source/encoder/level.cpp:45-65` 第一列，与
//     ITU-T H.265 Annex A 表 A.8 一致：31→983,040；40/41→2,228,224；50/51/52→8,912,896；
//     60/61/62→35,651,584；63→80,216,064；70/71/72→142,606,336；85=无损专用（无上限）。
//     注意：6.2 的 4,278,190,080 是 **maxLumaSamplesPerSecond**，不是每帧 MaxLumaPs。
//   * hvcC 字段偏移（ISO/IEC 14496-15 §8.3.3.1）：
//     payload[1]&0x1f = general_profile_idc；payload[12] = general_level_idc；
//     payload[16]&3 = chromaFormat；payload[17]&7 = bitDepthLumaMinus8。
//     super/T7.heic 实测：profile_idc=2、level_idc=93、chroma=1、bit_depth=10。
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace pp {

// ---------------------------------------------------------------------------
// HEVC Level
// ---------------------------------------------------------------------------

struct HeifLevelSpec {
    int level_idc = 0;       // x265:level-idc 参数编号（10 = Level 1.0；码流值 = 3×）
    const char *name = "";   // "1.0" … "8.5"
    int64_t max_luma_ps = 0; // MaxLumaPs（0 = 自动/不限；85 = 无损专用无上限）
};

// 完整表（升序，含 x265 4.3 的 6.3/7.0/7.1/7.2/8.5）。
const HeifLevelSpec *heif_level_table(std::size_t *count);

// 0 = 自动（不限）；-1 = 未知 level_idc；85 = 无上限（INT64_MAX）。
// 注意两套编号：x265 的 `x265:level-idc` 参数用 10/20/…/72（十倍 Level），而码流
// hvcC/SPS 的 general_level_idc 用 30/60/…/216（= 3×参数值；8.5 → 255）。本文件的
// level_idc 一律指**参数编号**，编解码边界用下面两个换算函数。
int64_t heif_level_max_luma_ps(int level_idc);

// x265 参数编号 → 码流 general_level_idc（0 → 0；85 → 255）。
int heif_level_general_idc(int level_idc);

// 码流 general_level_idc → x265 参数编号（0 → 0；非 3 的倍数返回 -1）。
int heif_level_parameter_idc(int general_level_idc);

// 0 → "自动"；未知 → "未知"。
const char *heif_level_name(int level_idc);

// 能容纳 luma_samples 的最小 Level（升序第一项；超过 7.2 → 85）。
int heif_level_min_idc(int64_t luma_samples);

// level_idc=0 恒 true；未知 Level 恒 false。判定按 x265 enforceLevel 的真实口径：
// 宽高向上补齐到 minCUSize(=8) 后的面积 <= MaxLumaPs，且宽/高 <= sqrt(8×MaxLumaPs)；
// int64 运算避免溢出。
bool heif_level_fits(int level_idc, int width, int height);

// 中文可操作错误（编码前预校验用）：点名图像尺寸/样本数/Level 上限/最小可行 Level，
// 并给出“提高 level-idc 或启用 grid”两条出路。
std::string heif_level_fit_error_zh(int level_idc, int width, int height);

// ---------------------------------------------------------------------------
// HEIF grid 规划（Apple 风格 1024×576 tile）
// ---------------------------------------------------------------------------

// grid_mode：0 = 关闭；1 = 总是 grid；2 = 仅当显式 Level 装不下整图时 grid。
// 其它值按 0 处理。
// tile 尺寸规则：以 1024×576（Apple T7 参考）为基准，先按图像尺寸 clamp，统一按 8 像素
// 对齐（覆盖 x265 的 minCUSize 补齐与 4:2:0/4:2:2 的色度对齐），再在显式 Level 下同时满足
// tile_luma_samples <= MaxLumaPs、宽高 <= sqrt(8×MaxLumaPs)（x265 enforceLevel 的尺寸约束）；
// 无法满足时退回单图（use_grid=false），由编码前 Level 预校验报中文错误。tile 永不大于图像。
struct HeifGridPlan {
    bool use_grid = false; // 是否应走 grid 派生图
    int level_idc = 0;     // 参与规划的 Level（0 = 自动）
    int tile_width = 0;    // 单 tile 宽（0 = 不适用）
    int tile_height = 0;   // 单 tile 高
    int cols = 0;          // ceil(width / tile_width)
    int rows = 0;          // ceil(height / tile_height)
    int64_t tile_luma_samples = 0;
    bool level_fits_image = true; // 显式 Level 能否容纳整图（0/未知见 heif_level_fits）
};

HeifGridPlan heif_plan_grid(int grid_mode, int level_idc, int width, int height, int chroma);

// ---------------------------------------------------------------------------
// hvcC 回读（编码后真实性校验）
// ---------------------------------------------------------------------------

struct HeifHvccInfo {
    bool found = false;
    int profile_idc = -1;       // general_profile_idc
    int level_idc = -1;         // general_level_idc
    int chroma_format_idc = -1; // 0=4:0:0 1=4:2:0 2=4:2:2 3=4:4:4
    int bit_depth_luma = -1;    // 8/10/12
    int bit_depth_chroma = -1;
    int num_arrays = -1;
    std::string error; // 失败原因（中文）；found=true 时为空
};

// 最小 BMFF 扫描：只递归 meta → iprp → ipco（HEIC 的 hvcC 所在路径），读取共享 hvcC。
// 不读 mdat、不做字节搜索，避免误命中像素数据。返回 info.found。
bool heif_parse_hvcc(const std::filesystem::path &path, HeifHvccInfo &info);

} // namespace pp
