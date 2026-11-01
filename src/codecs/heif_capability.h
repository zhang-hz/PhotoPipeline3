// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — HEIF 编码能力纯核：HEVC Level 表 / Level 拟合 / grid 规划 / hvcC 回读。
//
// 为什么需要这个文件（0.3.6-alpha1 HEIF 档位收口）：
//   * x265 的 `level-idc` 是硬约束：图像 luma 样本数超过 Level 的 MaxLumaPs 时编码器
//     直接拒绝（`encoder/level.cpp` 的 enforceLevel），但旧实现的错误只在 x265 日志里，
//     用户看到的是 “heif_context_encode_image failed”。本头把 Level 表与拟合做成纯函数，
//     enc_heif 在编码前给出中文可操作错误。
//   * tile 尺寸 = 由「规划 Level 的 MaxLumaPs(M) + 单边上限 D=floor(sqrt(8M))」与图像
//     宽高比推导，**没有任何固定基准**（C17'，0.4.0.alpha2；旧 M9/W2-T1 的 Apple 参考
//     1024×576 固定基准已删除）。heif_plan_grid 是 grid 规划的纯函数单源：enc_heif 的
//     有损 grid 路径用它选 tile 尺寸/行列数，单元测试直接锁边界（T1+Level 3.1 →
//     768×1280 = 983,040 亮度样本，6×5）。公式与规范证据：docs/heif-tile-sizing.md。
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
#include <string_view>
#include <vector>

#include "codecs/heif_profile.h" // W2-T2：still 变体断言要档位标签（C18）

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
// HEIF grid 规划（C17'：tile 由 Level 容量 + still 单图规范推导，无固定基准）
// ---------------------------------------------------------------------------

// 用户面配置键（C16）：
//   heif_grid    0=关闭 / 1=自动 / 2=总是（默认自动；ParamSet/预设/--dev 普通键）
//   heif_tile_w  0=自动 / 64–8192（手动值 8 向下对齐）
//   heif_tile_h  同上
// 兼容读别名 __heif_grid（M9 保留键）沿用**内部模式**编号 0=关闭 / 1=总是 / 2=自动。
// 下面两个映射函数是两套编号的唯一转换点（enc_heif / param_rules / 单测共用）。
// 非法值返回 -1（调用方给中文预检错误，不静默猜）。
int heif_grid_mode_from_param_value(int value); // heif_grid: 0→0, 1→2, 2→1
int heif_grid_mode_from_legacy(int value);      // __heif_grid: 0→0, 1→1, 2→2
// 内部模式 → 中文名（"关闭" / "总是" / "自动"；非法 → "未知"）。
const char *heif_grid_mode_name(int internal_mode);

// 该目录键是否上移核心区（C16：只有 x265:level-idc；grid/tile 保持高级）。
bool heif_param_in_core(std::string_view key);

// still 单图互操作档的 Level 上界（= 51，即 Level 5.1；MaxLumaPs = 8,912,896）。
// 证据（一手可复核）：3GPP TS 26.140 §4.5 / TS 26.141 §4.2 规定 HEIF still 图（'heic' +
// 'MiHB' MIAF brand）的互操作档为 HEVC Main / Main Tier / Level 5.1，媒体类型参数
// itemTypes="hvc1.1.2.L153.B0"（L153 = general_level_idc 153 = 3×51）。
// 如实标注：ITU-T H.265 (V9, 2023-09) **没有** still picture profile 的 Level 上限
// （A.3.3/A.3.4/A.3.5 无 level 条款；A.4.2 的 profile-specific level limits 只约束
// "video profiles"，其 NOTE 1 明确排除 still picture profiles），x265 4.3 / libheif 1.23
// 也不拦 → 5.1 是**应用格式（互操作）上界**，不是 HEVC profile 强制。证据等级表见
// docs/heif-tile-sizing.md。
int heif_still_picture_interop_max_level_idc();

// 规划输入（W2-T1 用户面）。grid_mode 用**内部模式**（0/1/2，见上面的映射函数）。
struct HeifGridRequest {
    int grid_mode = 2; // 0 = 关闭；1 = 总是 grid；2 = 自动（按 Level）
    int level_idc = 0; // 0 = 自动
    int width = 0;
    int height = 0;
    int chroma = 1;      // 0=4:0:0 1=4:2:0 2=4:2:2 3=4:4:4（MIAF 奇偶 warn 用）
    int tile_width = 0;  // 0 = 自动；手动值按 8 向下对齐（非法值由 plan.error_zh 报错）
    int tile_height = 0; // 0 = 自动

    // —— C17' 加性字段（追加在末尾）——
    // 编码档位（默认 Auto = 保持既有行为）。still 单图档位（MainStill / 16..21）搭配
    // 显式 Level > 5.1 时：tile 规划按 still 互操作上界 5.1，**编码 Level 不静默改**
    // （仍按用户值）；plan.warning_zh 说明该偏差。非 still 档位不受此限。
    HeifProfile profile = HeifProfile::Auto;
};

// tile 尺寸规则（C17'，0.4.0.alpha2；**删除**旧 Apple 参考 1024×576 固定基准）：
//   * 规划 Level Lp：显式 Level 直接用；Level=自动 → still 互操作上界 5.1；
//     显式 Level + still 单图档位 + Level>5.1 → Lp=5.1 + warning（见上）；
//   * M = MaxLumaPs(Lp)，D = floor(sqrt(8×M))（x265 enforceLevel 的尺寸约束）；在
//     8 像素对齐、[64, min(图宽, D)] 内枚举 tw，th = 8 向下对齐 min(图高, D, floor(M/tw))，
//     取面积最大者（tw×th ≤ M；可恰好等于 M），面积相同取 |tw/th − 图宽高比| 最小者；
//     **没有任何固定基准/魔法常数**（公式与证据：docs/heif-tile-sizing.md）；
//   * 手动 tile：8 向下对齐（对齐发生 → note_zh）；<64 / >8192 / 显式 Level 装不下
//     → error_zh（点名上限、最大可行 tile、三条出路）；单轴 0 表示该轴继续自动求解
//     （另一轴按 Level 容量收缩；仍保留 M9 A9 的 8 对齐口径）；
//   * rows×cols == 1 → downgraded_single（单图 + info 日志，grid 1×1 无收益）；
//   * rows/cols 单边 >256 或 rows×cols>65535 → error_zh（HEIF grid rows/cols 是 u8+1）；
//   * MIAF 7.3.11.4.2 奇偶（420 需宽高偶数、422 需宽偶数）只 warn（warning_zh），
//     不阻断（保持既有 801×601→grid 单测行为）。
// 自动语义（C16，不变）：
//   * Level=自动 + 常规尺寸 → 单图（默认产物零变化）；只有整图超出 x265 最高
//     Level 7.2 容量（MaxLumaPs=142,606,336 且宽高 <= sqrt(8×M)）才自动分块；
//   * 显式 Level 装不下整图 → 自动分块（每 tile 满足该 Level）；
//   * 总是 grid：无显式 Level 时按规划上界 Level 5.1 推导 tile（每 tile x265 自动 Level）。
struct HeifGridPlan {
    bool use_grid = false; // 是否应走 grid 派生图
    int level_idc = 0;     // 参与规划的 Level（0 = 自动）
    int tile_width = 0;    // 单 tile 宽（0 = 不适用）
    int tile_height = 0;   // 单 tile 高
    int cols = 0;          // ceil(width / tile_width)
    int rows = 0;          // ceil(height / tile_height)
    int64_t tile_luma_samples = 0;
    bool level_fits_image = true; // 显式 Level 能否容纳整图（0/未知见 heif_level_fits）

    // —— W2-T1 加性字段（追加在末尾）——
    bool ok = true;                      // false = error_zh 非空（调用方必须失败，不静默降级）
    bool downgraded_single = false;      // rows×cols == 1 → 已降级单图
    bool manual_tile = false;            // 手动 tile 生效（含单轴手动）
    bool manual_with_auto_level = false; // Level=自动 + 手动 tile（允许 + note）
    bool miaf_parity_warn = false;       // 420 奇宽高 / 422 奇宽 → warning_zh
    int tile_level_idc = 0;              // 显式 Level 时 = level_idc；0 = 每 tile x265 自动
    int aligned_width_from = 0;          // 手动宽发生 8 对齐时的原值（0 = 无对齐）
    int aligned_height_from = 0;
    std::string error_zh;   // 中文可操作错误（ok=false）
    std::string note_zh;    // info 级说明（对齐/单图降级/手动+自动 Level）
    std::string warning_zh; // warn 级说明（MIAF 奇偶 / still Level 上界）

    // —— C17' 加性字段（追加在末尾）——
    int planning_level_idc = 0;      // 推导 tile 用的 Level（0 = 不适用；Level=自动 → 51）
    bool still_level_capped = false; // 显式 Level>5.1 的 still 档位 → 规划被压到 5.1

    int64_t total_tiles() const { return static_cast<int64_t>(cols) * rows; }
};

// 完整请求（W2-T1 起为唯一实现；失败原因在 plan.error_zh）。
HeifGridPlan heif_plan_grid(const HeifGridRequest &request);

// M9 兼容入口（无手动 tile）：grid_mode 为内部模式 0/1/2。
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

// ---------------------------------------------------------------------------
// SPS profile_tier_level / DPB 解析（W2-T2 / C18）
//
// 为什么需要它：libheif 生成 hvcC 时**跳过** 48 位 constraint flags
// （hevc_boxes.cc:628-630；实测所有产物 hvcC 的 constraint 字节 = 000000000000），所以
// still 语义（intra / one_picture_only）只能从 hvcC 里内嵌的 SPS NAL 原始字节解析。
//
// 位宽口径（x265 4.3 entropy.cpp:738-775 的实写序列，与 H.265 §7.3.3 一致）：
//   * 所有 profile：4 位（progressive/interlaced/non_packed/frame_only）+ 44 位保留；
//   * RExt（idc 4/5/9/10/11）：4 位 + 9 位具名（max_12/10/8bit、max_422/420chroma、
//     max_monochrome、intra、one_picture_only、lower_bit_rate）+ 35 位保留；
//   * 非 RExt 的 44/35 位是保留位 → 本结构把 9 个具名位置 **-1（不适用）**，不把 0 当成
//     「intra=0」（那会把 Main10 误报成「非 still」而不是「PTL 无此位」）。
// ---------------------------------------------------------------------------
struct HeifSpsPtlInfo {
    bool found = false;
    int profile_space = -1; // general_profile_space（0）
    int tier_flag = -1;     // general_tier_flag
    int profile_idc = -1;   // general_profile_idc（1 Main / 2 Main10 / 3 MainStill / 4 RExt）
    std::uint32_t profile_compatibility_flags = 0; // 32 位 compatibility flags
    // 48 位 constraint 的前 4 位（所有 profile 都有）
    int progressive_source = -1;
    int interlaced_source = -1;
    int non_packed = -1;
    int frame_only = -1;
    // 仅 RExt 有；其它 profile = -1（保留位，不适用）
    int max_12bit = -1;
    int max_10bit = -1;
    int max_8bit = -1;
    int max_422chroma = -1;
    int max_420chroma = -1;
    int max_monochrome = -1;
    int intra = -1;
    int one_picture_only = -1;
    int lower_bit_rate = -1;
    std::string constraint_bits; // 48 位原文（"0"/"1"，取证与错误文案用）
    int level_idc = -1;          // general_level_idc（= 3 × x265 参数编号）
    // —— SPS 级字段（DPB 是 Main 10 Still 唯一可用的「单图」证据）——
    int chroma_format_idc = -1; // 0=4:0:0 1=4:2:0 2=4:2:2 3=4:4:4
    int bit_depth_luma = -1;    // 8/10/12
    int bit_depth_chroma = -1;
    int sps_max_dec_pic_buffering_minus1 = -1; // 最高 temporal sub-layer
    int sps_max_num_reorder_pics = -1;
    int sps_max_latency_increase_plus1 = -1;
    std::string error; // 失败原因（中文）；found=true 时为空
};

// 纯函数：hvcC 配置盒 payload 原始字节 → 首个 SPS（nal_unit_type=33）的 PTL + DPB。
//   * 按 ISO/IEC 14496-15 §8.3.3.1 遍历 numOfArrays（payload[22]）与各 NAL 长度；
//   * NAL 先跳 2 字节头，再**去 00 00 03 防竞争字节**（不做会让位偏移错，实测第一版解析器
//     就错在这里）；只解析首个 SPS，不做字节搜索、不读 mdat。
bool heif_parse_sps_ptl(const std::vector<std::uint8_t> &hvcc_payload, HeifSpsPtlInfo &info);

// 文件便捷入口：与 heif_parse_hvcc 同一套 box 扫描（meta → iprp → ipco → hvcC），取出
// 完整 hvcC payload 后交给上面的纯函数。
bool heif_parse_sps_ptl(const std::filesystem::path &path, HeifSpsPtlInfo &info);

// 编码后「still 变体档位」断言（C18）。返回空串 = 通过；否则中文可操作错误。
//   * 17..21（RExt still）：要求 intra=1 && one_picture_only=1；
//   * 16（Main10Still）：PTL 没有 still 位（具名位为 -1 不适用）+ DPB 要求
//     sps_max_dec_pic_buffering_minus1=0 && sps_max_num_reorder_pics=0（实测 4/2 → 失败，
//     与 heif_profile_still_available 的置灰理由同源）；
//   * 其它档位：返回空串（本函数只判 still 变体，普通档位由 hvcC 回读校验负责）。
std::string heif_sps_still_error_zh(HeifProfile profile, const HeifSpsPtlInfo &info);

} // namespace pp
