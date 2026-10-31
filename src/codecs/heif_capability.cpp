// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — HEIF 编码能力纯核实现（Level 表 / grid 规划 / hvcC 回读）。
#include "codecs/heif_capability.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace pp {
namespace {

// x265 4.3 source/encoder/level.cpp:45-65 第一列（MaxLumaPs）逐字复刻。
// 8.5 是 x265 的无损专用“无上限”档（MAX_UINT），这里用 INT64_MAX 表达。
constexpr HeifLevelSpec kLevels[] = {
    {10, "1.0", 36864},     {20, "2.0", 122880},
    {21, "2.1", 245760},    {30, "3.0", 552960},
    {31, "3.1", 983040},    {40, "4.0", 2228224},
    {41, "4.1", 2228224},   {50, "5.0", 8912896},
    {51, "5.1", 8912896},   {52, "5.2", 8912896},
    {60, "6.0", 35651584},  {61, "6.1", 35651584},
    {62, "6.2", 35651584},  {63, "6.3", 80216064},
    {70, "7.0", 142606336}, {71, "7.1", 142606336},
    {72, "7.2", 142606336}, {85, "8.5", std::numeric_limits<int64_t>::max()},
};

bool type_is(const char type[4], const char *want) { return std::memcmp(type, want, 4) == 0; }

uint32_t be32(const unsigned char *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint64_t be64(const unsigned char *p) {
    return (static_cast<uint64_t>(be32(p)) << 32) | static_cast<uint64_t>(be32(p + 4));
}

bool is_container(const char type[4]) {
    return type_is(type, "meta") || type_is(type, "iprp") || type_is(type, "ipco");
}

// 对齐到 a 的整数倍（向下）；a <= 1 时原样返回。
int align_down(int value, int a) {
    if (a <= 1)
        return value;
    return (value / a) * a;
}

// 递归扫描 box 树。返回 1 = 找到 hvcC；-1 = 硬错误；0 = 未找到。
int scan_boxes(std::ifstream &in, uint64_t start, uint64_t end, int depth, HeifHvccInfo &info) {
    if (depth > 8)
        return 0; // 容器层数上限：防御畸形文件
    uint64_t pos = start;
    while (pos + 8 <= end) {
        in.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
        unsigned char header[8] = {};
        in.read(reinterpret_cast<char *>(header), 8);
        if (in.gcount() != 8) {
            info.error = "读取 box 头失败（文件被截断？）";
            return -1;
        }
        uint64_t box_size = be32(header);
        char type[4] = {};
        std::memcpy(type, header + 4, 4);
        uint64_t header_size = 8;
        if (box_size == 1) {
            unsigned char ext[8] = {};
            in.read(reinterpret_cast<char *>(ext), 8);
            if (in.gcount() != 8) {
                info.error = "读取 64 位 box 长度失败（文件被截断？）";
                return -1;
            }
            box_size = be64(ext);
            header_size = 16;
        } else if (box_size == 0) {
            box_size = end - pos; // 该 box 延伸到父容器末尾
        }
        if (box_size < header_size || box_size > end - pos) {
            info.error = "box 尺寸非法（越界或小于头部）";
            return -1;
        }
        const uint64_t payload = pos + header_size;
        const uint64_t payload_size = box_size - header_size;
        if (type_is(type, "hvcC")) {
            // ISO/IEC 14496-15 §8.3.3.1：至少到 numOfArrays（偏移 22）。
            if (payload_size < 23) {
                info.error = "hvcC 载荷过短（< 23 字节）";
                return -1;
            }
            unsigned char payload_bytes[23] = {};
            in.seekg(static_cast<std::streamoff>(payload), std::ios::beg);
            in.read(reinterpret_cast<char *>(payload_bytes), 23);
            if (in.gcount() != 23) {
                info.error = "读取 hvcC 载荷失败（文件被截断？）";
                return -1;
            }
            if (payload_bytes[0] != 1) {
                info.error = "hvcC configurationVersion != 1";
                return -1;
            }
            info.found = true;
            info.profile_idc = payload_bytes[1] & 0x1f;
            info.level_idc = payload_bytes[12];
            info.chroma_format_idc = payload_bytes[16] & 0x03;
            info.bit_depth_luma = static_cast<int>(payload_bytes[17] & 0x07) + 8;
            info.bit_depth_chroma = static_cast<int>(payload_bytes[18] & 0x07) + 8;
            info.num_arrays = payload_bytes[22];
            return 1;
        }
        if (is_container(type)) {
            uint64_t child_start = payload;
            if (type_is(type, "meta")) {
                if (payload_size < 4) {
                    info.error = "meta box 过短（缺 version/flags）";
                    return -1;
                }
                child_start += 4; // meta 是 FullBox
            }
            const int rc = scan_boxes(in, child_start, payload + payload_size, depth + 1, info);
            if (rc != 0)
                return rc;
        }
        pos += box_size;
    }
    return 0;
}

} // namespace

const HeifLevelSpec *heif_level_table(std::size_t *count) {
    if (count != nullptr)
        *count = sizeof(kLevels) / sizeof(kLevels[0]);
    return kLevels;
}

int64_t heif_level_max_luma_ps(int level_idc) {
    if (level_idc == 0)
        return 0; // 自动/不限
    for (const HeifLevelSpec &spec : kLevels) {
        if (spec.level_idc == level_idc)
            return spec.max_luma_ps;
    }
    return -1; // 未知
}

int heif_level_general_idc(int level_idc) {
    if (level_idc <= 0)
        return 0;
    return level_idc * 3;
}

int heif_level_parameter_idc(int general_level_idc) {
    if (general_level_idc <= 0)
        return 0;
    if (general_level_idc % 3 != 0)
        return -1;
    return general_level_idc / 3;
}

const char *heif_level_name(int level_idc) {
    if (level_idc == 0)
        return "自动";
    for (const HeifLevelSpec &spec : kLevels) {
        if (spec.level_idc == level_idc)
            return spec.name;
    }
    return "未知";
}

int heif_level_min_idc(int64_t luma_samples) {
    if (luma_samples <= 0)
        return 10;
    for (const HeifLevelSpec &spec : kLevels) {
        if (spec.max_luma_ps >= luma_samples)
            return spec.level_idc;
    }
    return 85;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：width/height 由调用点语义固定
bool heif_level_fits(int level_idc, int width, int height) {
    if (level_idc == 0)
        return true;
    if (width <= 0 || height <= 0)
        return false;
    const int64_t max_luma = heif_level_max_luma_ps(level_idc);
    if (max_luma < 0)
        return false;
    // x265 在 enforceLevel() 之前把 source 宽高向上补齐到 minCUSize(=8) 的倍数
    // （x265 4.3 source/encoder/encoder.cpp configure(): "set pad size if width is not
    // multiple of the minimum CU size"），因此 Level 判定必须用补齐后的面积。Phase 1 实测：
    // 3902x2284 raw 8,912,168 <= Level 5.2 的 8,912,896，但补齐 3904x2288 = 8,932,352 后
    // x265 报 "picture dimensions are out of range for specified level"。
    constexpr int64_t kX265MinCuAlign = 8;
    const int64_t padded_w =
        (static_cast<int64_t>(width) + kX265MinCuAlign - 1) / kX265MinCuAlign * kX265MinCuAlign;
    const int64_t padded_h =
        (static_cast<int64_t>(height) + kX265MinCuAlign - 1) / kX265MinCuAlign * kX265MinCuAlign;
    if (padded_w * padded_h > max_luma)
        return false;
    // x265 enforceLevel 的另一条约束：宽/高 <= sqrt(8 * MaxLumaPs)
    // （level.cpp: "picture dimensions are out of range for specified level"）。
    // Level 8.5 的 MaxLumaPs 为 INT64_MAX，double 运算足够表达其“无上限”语义。
    const int64_t dim_cap = static_cast<int64_t>(std::sqrt(8.0 * static_cast<double>(max_luma)));
    return static_cast<int64_t>(width) <= dim_cap && static_cast<int64_t>(height) <= dim_cap;
}

std::string heif_level_fit_error_zh(int level_idc, int width, int height) {
    if (heif_level_max_luma_ps(level_idc) < 0) {
        return "x265:level-idc=" + std::to_string(level_idc) +
               " 不是 x265 4.3 支持的 Level（0=自动；合法值 10/20/21/30/31/40/41/50/51/52/"
               "60/61/62/63/70/71/72）。请改正该参数。";
    }
    const int64_t samples = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    // 建议 Level 必须按 x265 补齐后的样本数算（example: 3902x2284 原始面积低于 L5.2 上限，
    // 补齐 3904x2288 后已超，最小可行 Level 是 6.0 而不是 5.2）。
    constexpr int64_t kAlign = 8;
    const int64_t padded_w = (static_cast<int64_t>(width) + kAlign - 1) / kAlign * kAlign;
    const int64_t padded_h = (static_cast<int64_t>(height) + kAlign - 1) / kAlign * kAlign;
    const int min_level = heif_level_min_idc(padded_w * padded_h);
    return "图像 " + std::to_string(width) + "×" + std::to_string(height) + "（原始 " +
           std::to_string(samples) + " 亮度样本；x265 补齐到 8 的倍数后 " +
           std::to_string(padded_w * padded_h) + "）超过 HEVC Level " + heif_level_name(level_idc) +
           " 的上限 " + std::to_string(heif_level_max_luma_ps(level_idc)) +
           " 亮度样本；请把 x265:level-idc 提高到至少 " + heif_level_name(min_level) +
           "（0=自动），或启用 HEIF grid 分块（每 tile 单独满足 Level）。";
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：五参语义由名字/注释固定（纯规划函数）
HeifGridPlan heif_plan_grid(int grid_mode, int level_idc, int width, int height, int chroma) {
    // chroma 不参与 tile 尺寸：统一 8 对齐已覆盖 4:2:0/4:2:2/4:4:4 的色度对齐要求，
    // 且 grid 的每个 tile 都以同一 chroma 布局编码。保留形参以固定调用面语义。
    (void)chroma;
    HeifGridPlan plan;
    plan.level_idc = level_idc;
    if (width <= 0 || height <= 0)
        return plan;
    plan.level_fits_image = heif_level_fits(level_idc, width, height);
    if (level_idc != 0 && heif_level_max_luma_ps(level_idc) < 0) {
        // 未知 Level（x265 4.3 不存在的 level-idc）：不做 grid 规划，交调用方给出
        // “Level 不存在”的中文预校验错误；否则会在编码器里变成泛化的 x265 日志。
        plan.use_grid = false;
        return plan;
    }
    if (grid_mode != 1 && grid_mode != 2)
        return plan; // 0 / 未知 = 关闭
    if (grid_mode == 2 && plan.level_fits_image)
        return plan; // 自动：显式 Level 装得下整图就不分块
    plan.use_grid = true;

    // Apple HEIC 参考 tile 基准（super/T7.heic：1024×576）。tile 宽高统一按 8 像素对齐：
    //   * x265 在 Level 校验前把 source 宽高补齐到 minCUSize(=8) 的倍数（见 heif_level_fits
    //     注释）；grid 的每个 tile 都是独立 HEVC 图，必须按补齐后的面积/宽高满足 Level；
    //   * 8 对齐同时覆盖 4:2:0（宽高）与 4:2:2（宽）的色度对齐要求。
    // 模式 1（总是 grid）：固定 Apple 1024×576（按图像尺寸 clamp、8 对齐）；若显式 Level 容不下
    // 该 tile，则退化为下面的搜索，保证产物合法（合法性优先于“固定尺寸”）。
    // 模式 2（自动）：显式 Level 装不下整图时才走 grid；tile 按 Level 搜索：
    //   面积最大化，等面积取宽高比最接近 Apple 16:9。
    constexpr int kAlign = 8;
    constexpr int kBaseTileW = 1024;
    constexpr int kBaseTileH = 576;
    constexpr int kMinTile = 64; // x265 静止图 CTU/色度 sanity 下界
    const auto align_down8 = [=](int64_t v) -> int64_t {
        return v <= 0 ? int64_t{0} : (v / kAlign) * kAlign;
    };
    auto clamp_tile = [&](int64_t v, int64_t base) -> int {
        const int64_t clamped = std::min<int64_t>(v, base);
        return static_cast<int>(
            std::max<int64_t>(static_cast<int64_t>(kAlign), align_down8(clamped)));
    };
    int tile_w = clamp_tile(width, kBaseTileW);
    int tile_h = clamp_tile(height, kBaseTileH);

    const int64_t max_luma = heif_level_max_luma_ps(level_idc);
    if (max_luma > 0) {
        const int64_t dim_cap =
            static_cast<int64_t>(std::sqrt(8.0 * static_cast<double>(max_luma)));
        const bool base_fits = static_cast<int64_t>(tile_w) * tile_h <= max_luma &&
                               tile_w <= dim_cap && tile_h <= dim_cap;
        if (!base_fits) {
            // 8 对齐 + 面积最大的搜索（w 步长 8，h 取该 w 下能容纳的最大 8 对齐高度）。
            // 宽/高同时受图像尺寸、Apple 基准与 sqrt(8*MaxLumaPs) 限制。
            const int w_max =
                static_cast<int>(std::min<int64_t>(std::min<int64_t>(width, kBaseTileW), dim_cap));
            const int h_max =
                static_cast<int>(std::min<int64_t>(std::min<int64_t>(height, kBaseTileH), dim_cap));
            int best_w = 0;
            int best_h = 0;
            int64_t best_area = -1;
            double best_aspect_diff = 1e9;
            const double target_aspect = static_cast<double>(kBaseTileW) / kBaseTileH;
            for (int w = static_cast<int>(align_down8(static_cast<int64_t>(w_max))); w >= kMinTile;
                 w -= kAlign) {
                int h = static_cast<int>(std::min<int64_t>(h_max, max_luma / w));
                h = static_cast<int>(align_down8(static_cast<int64_t>(h)));
                if (h < kMinTile)
                    continue;
                const int64_t area = static_cast<int64_t>(w) * h;
                if (area > max_luma)
                    continue;
                const double aspect_diff = std::fabs(static_cast<double>(w) / h - target_aspect);
                if (area > best_area || (area == best_area && aspect_diff < best_aspect_diff)) {
                    best_area = area;
                    best_aspect_diff = aspect_diff;
                    best_w = w;
                    best_h = h;
                }
            }
            if (best_w < kMinTile || best_h < kMinTile) {
                // 理论上 Level 表最小值（1.0: 36864）仍能容纳 256x144；这里只做防御：
                // 让调用方回退单图路径并给出 Level 预校验错误，而不是产出非法 grid。
                plan.use_grid = false;
                return plan;
            }
            tile_w = best_w;
            tile_h = best_h;
        }
    }

    plan.tile_width = tile_w;
    plan.tile_height = tile_h;
    plan.tile_luma_samples = static_cast<int64_t>(tile_w) * static_cast<int64_t>(tile_h);
    plan.cols = static_cast<int>((static_cast<int64_t>(width) + tile_w - 1) / tile_w);
    plan.rows = static_cast<int>((static_cast<int64_t>(height) + tile_h - 1) / tile_h);
    return plan;
}

bool heif_parse_hvcc(const std::filesystem::path &path, HeifHvccInfo &info) {
    info = HeifHvccInfo{};
    std::error_code ec;
    const uint64_t file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        info.error = "无法读取文件大小：" + ec.message();
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        info.error = "无法打开文件（路径或权限问题）";
        return false;
    }
    const int rc = scan_boxes(in, 0, file_size, 0, info);
    if (rc > 0)
        return true;
    if (rc < 0)
        return false;
    info.error = "未找到 hvcC 配置盒（不是 HEIF/HEVC 文件，或使用 AV1 编码）";
    return false;
}

} // namespace pp
