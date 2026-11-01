// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — HEIF 编码能力纯核实现（Level 表 / grid 规划 / hvcC 回读 / SPS PTL 解析）。
#include "codecs/heif_capability.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

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
// W2-T2：hvcc_payload 非空时把**完整** hvcC payload 拷出（SPS PTL 解析要内嵌 SPS NAL；
// 旧路径只读 23 字节的固定字段）。空指针 = 旧行为（只读 23 字节，不额外分配）。
int scan_boxes(std::ifstream &in, uint64_t start, uint64_t end, int depth, HeifHvccInfo &info,
               std::vector<std::uint8_t> *hvcc_payload) {
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
            // hvcC 只承载参数集（VPS/SPS/PPS/SEI），1 MiB 是防御畸形文件的上限。
            constexpr uint64_t kMaxHvccPayload = 1U << 20;
            const uint64_t want =
                hvcc_payload != nullptr ? std::min<uint64_t>(payload_size, kMaxHvccPayload) : 23;
            std::vector<unsigned char> payload_bytes(static_cast<std::size_t>(want), 0);
            in.seekg(static_cast<std::streamoff>(payload), std::ios::beg);
            in.read(reinterpret_cast<char *>(payload_bytes.data()),
                    static_cast<std::streamsize>(want));
            if (static_cast<uint64_t>(in.gcount()) != want) {
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
            if (hvcc_payload != nullptr)
                hvcc_payload->assign(payload_bytes.begin(), payload_bytes.end());
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
            const int rc =
                scan_boxes(in, child_start, payload + payload_size, depth + 1, info, hvcc_payload);
            if (rc != 0)
                return rc;
        }
        pos += box_size;
    }
    return 0;
}

// 打开文件并按 box 树找 hvcC（heif_parse_hvcc / heif_parse_sps_ptl 共用）。
bool scan_file_for_hvcc(const std::filesystem::path &path, HeifHvccInfo &info,
                        std::vector<std::uint8_t> *hvcc_payload) {
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
    const int rc = scan_boxes(in, 0, file_size, 0, info, hvcc_payload);
    if (rc > 0)
        return true;
    if (rc < 0)
        return false;
    info.error = "未找到 hvcC 配置盒（不是 HEIF/HEVC 文件，或使用 AV1 编码）";
    return false;
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

// NOLINTBEGIN(bugprone-easily-swappable-parameters)：纯规划函数的参数由名字/注释固定
int heif_grid_mode_from_param_value(int value) {
    // heif_grid 用户面编号（UI 顺序）：0=关闭、1=自动、2=总是。
    switch (value) {
    case 0:
        return 0;
    case 1:
        return 2; // 自动
    case 2:
        return 1; // 总是
    default:
        return -1;
    }
}

int heif_grid_mode_from_legacy(int value) {
    // M9 保留键 __heif_grid 的内部编号：0=关闭、1=总是、2=自动。
    switch (value) {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return 2;
    default:
        return -1;
    }
}
// NOLINTEND(bugprone-easily-swappable-parameters)

const char *heif_grid_mode_name(int internal_mode) {
    switch (internal_mode) {
    case 0:
        return "关闭";
    case 1:
        return "总是";
    case 2:
        return "自动";
    default:
        return "未知";
    }
}

bool heif_param_in_core(std::string_view key) {
    // C16：Level 从高级区上移核心区；grid/tile 保持高级（它们只影响分块派生图）。
    return key == "x265:level-idc";
}

namespace {

constexpr int kGridAlign = 8;
constexpr int kBaseTileW = 1024;
constexpr int kBaseTileH = 576;
constexpr int kMinTile = 64; // x265 静止图 CTU/色度 sanity 下界
constexpr int kMaxTile = 8192;
constexpr int kMaxGridSide = 256; // HEIF grid rows/cols 是 u8+1
constexpr int64_t kMaxGridTiles = 65535;

int align_down8(int value) { return value <= 0 ? 0 : (value / kGridAlign) * kGridAlign; }

int64_t grid_dim_cap(int level_idc) {
    const int64_t max_luma = heif_level_max_luma_ps(level_idc);
    if (max_luma <= 0)
        return 0;
    return static_cast<int64_t>(std::sqrt(8.0 * static_cast<double>(max_luma)));
}

struct TileChoice {
    int w = 0;
    int h = 0;
    bool ok = false;
};

// 显式 Level 下“面积最大、等面积取长宽比最接近 1024:576”的可行 tile（M9 A9 规则）。
TileChoice best_tile_for_level(int level_idc, int width, int height) {
    TileChoice out;
    if (level_idc == 0 || width <= 0 || height <= 0)
        return out;
    const int64_t max_luma = heif_level_max_luma_ps(level_idc);
    if (max_luma <= 0)
        return out;
    const int64_t dim_cap = grid_dim_cap(level_idc);
    const int w_max =
        static_cast<int>(std::min<int64_t>(std::min<int64_t>(width, kBaseTileW), dim_cap));
    const int h_max =
        static_cast<int>(std::min<int64_t>(std::min<int64_t>(height, kBaseTileH), dim_cap));
    int best_w = 0;
    int best_h = 0;
    int64_t best_area = -1;
    double best_aspect_diff = 1e9;
    const double target_aspect = static_cast<double>(kBaseTileW) / kBaseTileH;
    for (int w = align_down8(w_max); w >= kMinTile; w -= kGridAlign) {
        int h = static_cast<int>(std::min<int64_t>(h_max, max_luma / w));
        h = align_down8(h);
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
    out.w = best_w;
    out.h = best_h;
    out.ok = best_w >= kMinTile && best_h >= kMinTile;
    return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
bool tile_fits_level(int level_idc, int tile_w, int tile_h) {
    const int64_t max_luma = heif_level_max_luma_ps(level_idc);
    if (max_luma <= 0)
        return false;
    const int64_t dim_cap = grid_dim_cap(level_idc);
    return static_cast<int64_t>(tile_w) * tile_h <= max_luma && tile_w <= dim_cap &&
           tile_h <= dim_cap;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
std::string manual_tile_error_zh(int level_idc, int tile_w, int tile_h, int width, int height) {
    const int64_t max_luma = heif_level_max_luma_ps(level_idc);
    const int64_t dim_cap = grid_dim_cap(level_idc);
    const int64_t area = static_cast<int64_t>(tile_w) * tile_h;
    std::string msg = "手动 tile " + std::to_string(tile_w) + "×" + std::to_string(tile_h);
    if (max_luma >= 0 && area > max_luma) {
        msg += "（" + std::to_string(area) + " 亮度样本）超过 HEVC Level " +
               heif_level_name(level_idc) + " 的 MaxLumaPs 上限 " + std::to_string(max_luma);
    }
    if (tile_w > dim_cap || tile_h > dim_cap) {
        if (max_luma >= 0 && area > max_luma)
            msg += "，且";
        else
            msg += " 的宽/高 ";
        msg += "超过该 Level 的单边上限 " + std::to_string(dim_cap) + " 像素";
    }
    const TileChoice best = best_tile_for_level(level_idc, width, height);
    if (best.ok) {
        msg += "；该 Level 下最大可行 tile 为 " + std::to_string(best.w) + "×" +
               std::to_string(best.h) + "（" +
               std::to_string(static_cast<int64_t>(best.w) * best.h) + " 亮度样本）";
    } else {
        msg += "；该 Level 下连 " + std::to_string(kMinTile) + "×" + std::to_string(kMinTile) +
               " 的最小 tile 都容不下";
    }
    msg += "。三条出路：① 把 heif_tile_w/heif_tile_h 设为 0（自动推导）；② 提高 "
           "x265:level-idc；③ 把 heif_grid 设为关闭（单图输出）。";
    return msg;
}

} // namespace

HeifGridPlan heif_plan_grid(const HeifGridRequest &request) {
    HeifGridPlan plan;
    plan.level_idc = request.level_idc;
    if (request.width <= 0 || request.height <= 0) {
        plan.ok = false;
        plan.error_zh = "HEIF grid 规划失败：图像尺寸无效（宽高必须为正）。";
        return plan;
    }
    if (request.level_idc != 0 && heif_level_max_luma_ps(request.level_idc) < 0) {
        plan.ok = false;
        plan.level_fits_image = false;
        plan.error_zh = heif_level_fit_error_zh(request.level_idc, request.width, request.height);
        return plan;
    }
    plan.level_fits_image = heif_level_fits(request.level_idc, request.width, request.height);
    const bool explicit_level = request.level_idc != 0;
    const bool manual = request.tile_width != 0 || request.tile_height != 0;
    if (manual && request.grid_mode == 0)
        plan.note_zh = "分块模式=关闭，已忽略手动 tile 设置（单图输出）。";
    if (request.grid_mode == 0) {
        if (explicit_level && !plan.level_fits_image) {
            plan.ok = false;
            plan.error_zh =
                heif_level_fit_error_zh(request.level_idc, request.width, request.height) +
                " 当前分块模式=关闭（单图）；如需自动分块请把 heif_grid 改为「自动」或"
                "「总是」。";
        }
        return plan;
    }
    if (request.grid_mode != 1 && request.grid_mode != 2) {
        // 非法内部模式：按关闭处理（调用方的模式映射已负责报错），不算规划错误。
        return plan;
    }

    // 是否需要 grid（C16 自动语义）：
    //   手动 tile > 总是 grid > 自动（显式 Level 装不下整图 / Level=自动且超过 7.2 容量）。
    const bool auto_capacity_fits = heif_level_fits(72, request.width, request.height);
    bool want_grid = false;
    if (manual || request.grid_mode == 1)
        want_grid = true;
    else if (explicit_level)
        want_grid = !plan.level_fits_image;
    else
        want_grid = !auto_capacity_fits;
    if (!want_grid)
        return plan;

    plan.use_grid = true;
    plan.tile_level_idc = explicit_level ? request.level_idc : 0;

    int auto_w = std::max(kGridAlign, align_down8(std::min(request.width, kBaseTileW)));
    int auto_h = std::max(kGridAlign, align_down8(std::min(request.height, kBaseTileH)));
    if (explicit_level && !tile_fits_level(request.level_idc, auto_w, auto_h)) {
        const TileChoice best =
            best_tile_for_level(request.level_idc, request.width, request.height);
        if (!best.ok) {
            plan.ok = false;
            plan.use_grid = false;
            plan.error_zh = "目标 HEVC Level " + std::string(heif_level_name(request.level_idc)) +
                            " 下没有可用的 tile（连 " + std::to_string(kMinTile) + "×" +
                            std::to_string(kMinTile) +
                            " 都容不下）；请提高 x265:level-idc 或关闭 "
                            "grid。";
            return plan;
        }
        auto_w = best.w;
        auto_h = best.h;
    }

    int tile_w = auto_w;
    int tile_h = auto_h;
    if (manual) {
        plan.manual_tile = true;
        if (request.tile_width < 0 || request.tile_width > kMaxTile || request.tile_height < 0 ||
            request.tile_height > kMaxTile) {
            plan.ok = false;
            plan.use_grid = false;
            plan.error_zh = "手动 tile 参数越界：heif_tile_w/heif_tile_h 合法范围 0–" +
                            std::to_string(kMaxTile) + "（0=自动；非 0 时最小 " +
                            std::to_string(kMinTile) + "，按 8 向下对齐）。";
            return plan;
        }
        if (request.tile_width != 0) {
            const int aligned = align_down8(request.tile_width);
            if (aligned < kMinTile) {
                plan.ok = false;
                plan.use_grid = false;
                plan.error_zh = "手动 tile 宽 " + std::to_string(request.tile_width) +
                                " 按 8 向下对齐到 " + std::to_string(aligned) + " 后小于最小值 " +
                                std::to_string(kMinTile) + "；请改为 0（自动）或 ≥" +
                                std::to_string(kMinTile) + "。";
                return plan;
            }
            if (aligned != request.tile_width)
                plan.aligned_width_from = request.tile_width;
            tile_w = aligned;
        }
        if (request.tile_height != 0) {
            const int aligned = align_down8(request.tile_height);
            if (aligned < kMinTile) {
                plan.ok = false;
                plan.use_grid = false;
                plan.error_zh = "手动 tile 高 " + std::to_string(request.tile_height) +
                                " 按 8 向下对齐到 " + std::to_string(aligned) + " 后小于最小值 " +
                                std::to_string(kMinTile) + "；请改为 0（自动）或 ≥" +
                                std::to_string(kMinTile) + "。";
                return plan;
            }
            if (aligned != request.tile_height)
                plan.aligned_height_from = request.tile_height;
            tile_h = aligned;
        }
        // 手动 tile ≥ 图像（rows×cols==1）→ 降级单图（grid 1×1 无收益）。先于 Level 校验：
        // 降级后 tile 不参与编码，不应因“tile 超 Level”报错。
        const int tentative_cols =
            static_cast<int>((static_cast<int64_t>(request.width) + tile_w - 1) / tile_w);
        const int tentative_rows =
            static_cast<int>((static_cast<int64_t>(request.height) + tile_h - 1) / tile_h);
        if (tentative_cols == 1 && tentative_rows == 1) {
            plan.use_grid = false;
            plan.downgraded_single = true;
            plan.tile_width = tile_w;
            plan.tile_height = tile_h;
            plan.cols = 1;
            plan.rows = 1;
            plan.tile_luma_samples = static_cast<int64_t>(tile_w) * tile_h;
            plan.note_zh = "手动 tile " + std::to_string(tile_w) + "×" + std::to_string(tile_h) +
                           " 覆盖整图（grid 1×1 无收益），已降级为单图。";
            return plan;
        }
        if (explicit_level) {
            // 单轴手动：另一轴继续自动求解，并在 Level 内收缩（0=自动的语义）。
            if (request.tile_height == 0) {
                // 宽手动、高自动：按手动宽求最大可行高。
                const int64_t cap = heif_level_max_luma_ps(request.level_idc) / tile_w;
                const int lim = align_down8(static_cast<int>(std::min<int64_t>(
                    std::min<int64_t>(auto_h, grid_dim_cap(request.level_idc)), cap)));
                if (lim < kMinTile) {
                    plan.ok = false;
                    plan.use_grid = false;
                    plan.error_zh = manual_tile_error_zh(request.level_idc, tile_w, auto_h,
                                                         request.width, request.height);
                    return plan;
                }
                tile_h = lim;
            }
            if (request.tile_width == 0) {
                // 高手动、宽自动：按手动高求最大可行宽。
                const int64_t cap = heif_level_max_luma_ps(request.level_idc) / tile_h;
                const int lim = align_down8(static_cast<int>(std::min<int64_t>(
                    std::min<int64_t>(auto_w, grid_dim_cap(request.level_idc)), cap)));
                if (lim < kMinTile) {
                    plan.ok = false;
                    plan.use_grid = false;
                    plan.error_zh = manual_tile_error_zh(request.level_idc, auto_w, tile_h,
                                                         request.width, request.height);
                    return plan;
                }
                tile_w = lim;
            }
            if (!tile_fits_level(request.level_idc, tile_w, tile_h)) {
                plan.ok = false;
                plan.use_grid = false;
                plan.error_zh = manual_tile_error_zh(request.level_idc, tile_w, tile_h,
                                                     request.width, request.height);
                return plan;
            }
        } else {
            plan.manual_with_auto_level = true;
            plan.note_zh = "Level=自动 + 手动 tile：每 tile 的 Level 由 x265 自动选择；该配置与 "
                           "Apple 参考（1024×576）不同，iPhone 接受度未验证。";
        }
    }

    const int cols = static_cast<int>((static_cast<int64_t>(request.width) + tile_w - 1) / tile_w);
    const int rows = static_cast<int>((static_cast<int64_t>(request.height) + tile_h - 1) / tile_h);
    if (cols <= 0 || rows <= 0) {
        plan.ok = false;
        plan.use_grid = false;
        plan.error_zh = "HEIF grid 规划失败：网格行列数无效。";
        return plan;
    }
    if (cols > kMaxGridSide || rows > kMaxGridSide ||
        static_cast<int64_t>(cols) * rows > kMaxGridTiles) {
        plan.ok = false;
        plan.use_grid = false;
        plan.error_zh = "HEIF grid 网格 " + std::to_string(rows) + "×" + std::to_string(cols) +
                        "（" + std::to_string(static_cast<int64_t>(rows) * cols) +
                        " 个 tile）超过上限：单边 ≤" + std::to_string(kMaxGridSide) +
                        " 且 rows×cols ≤" + std::to_string(kMaxGridTiles) +
                        "；请增大 tile 或缩小图像。";
        return plan;
    }
    plan.tile_width = tile_w;
    plan.tile_height = tile_h;
    plan.tile_luma_samples = static_cast<int64_t>(tile_w) * tile_h;
    plan.cols = cols;
    plan.rows = rows;
    if (cols == 1 && rows == 1) {
        plan.use_grid = false;
        plan.downgraded_single = true;
        plan.note_zh = "tile " + std::to_string(tile_w) + "×" + std::to_string(tile_h) +
                       " 覆盖整图（grid 1×1 无收益），已降级为单图。";
        return plan;
    }
    // MIAF 7.3.11.4.2 奇偶：只 warn，不阻断（保持既有 801×601→grid 行为）。
    const bool odd_for_chroma =
        (request.chroma == 1 && (request.width % 2 != 0 || request.height % 2 != 0)) ||
        (request.chroma == 2 && request.width % 2 != 0);
    if (odd_for_chroma) {
        plan.miaf_parity_warn = true;
        plan.warning_zh =
            "图像 " + std::to_string(request.width) + "×" + std::to_string(request.height) +
            " 为奇数尺寸（MIAF 7.3.11.4.2：4:2:0 建议宽高为偶数、4:2:2 建议宽为偶数）；"
            "grid 仍按原尺寸输出（仅提醒，不阻断）。";
    }
    return plan;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：mode/level/宽高/chroma 由规划语义固定
HeifGridPlan heif_plan_grid(int grid_mode, int level_idc, int width, int height, int chroma) {
    HeifGridRequest request;
    request.grid_mode = grid_mode;
    request.level_idc = level_idc;
    request.width = width;
    request.height = height;
    request.chroma = chroma;
    return heif_plan_grid(request);
}

bool heif_parse_hvcc(const std::filesystem::path &path, HeifHvccInfo &info) {
    return scan_file_for_hvcc(path, info, nullptr);
}

// ---------------------------------------------------------------------------
// SPS profile_tier_level / DPB 解析（W2-T2 / C18）
// ---------------------------------------------------------------------------
namespace {

// 位读取器（MSB first）。越界一律返回 false（不静默读 0）。
class BitReader {
public:
    BitReader(const std::vector<std::uint8_t> &data) : data_(data) {}

    bool read_bits(int count, std::uint32_t &out) {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            if (bit_pos_ >= data_.size() * 8U)
                return false;
            const std::uint8_t byte = data_[bit_pos_ / 8U];
            value = (value << 1) | ((byte >> (7 - (bit_pos_ % 8U))) & 1U);
            ++bit_pos_;
        }
        out = value;
        return true;
    }

    // ue(v)：前导 0 的个数 + 余数（H.265 §9.2）。
    bool read_ue(std::uint32_t &out) {
        int zeros = 0;
        std::uint32_t bit = 0;
        while (true) {
            if (!read_bits(1, bit))
                return false;
            if (bit != 0)
                break;
            if (++zeros > 31)
                return false; // 非法指数哥伦布
        }
        std::uint32_t rest = 0;
        if (zeros > 0 && !read_bits(zeros, rest))
            return false;
        out = (1U << zeros) - 1U + rest;
        return true;
    }

private:
    const std::vector<std::uint8_t> &data_;
    std::size_t bit_pos_ = 0;
};

// NAL → RBSP：跳过 2 字节 NAL 头，去掉 00 00 03 里的 03（防竞争字节）。
// 实测教训（W2-T2 / design-recon 第一版解析器）：不去防竞争字节会让位偏移错，constraint 全错。
std::vector<std::uint8_t> nal_to_rbsp(const std::uint8_t *nal, std::size_t size) {
    std::vector<std::uint8_t> out;
    if (size <= 2)
        return out;
    out.reserve(size);
    std::size_t zeros = 0;
    for (std::size_t i = 2; i < size; ++i) {
        const std::uint8_t byte = nal[i];
        if (zeros >= 2 && byte == 0x03) {
            zeros = 0; // emulation_prevention_three_byte：丢弃，不进入 RBSP
            continue;
        }
        out.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    return out;
}

// 48 位 constraint 的位宽切分（x265 4.3 entropy.cpp:738-775）：
// 所有 profile = 4 + 44 保留；RExt（4/5/9/10/11）= 4 + 9 具名 + 35 保留。
bool is_rext_profile_idc(int idc) {
    return idc == 4 || idc == 5 || idc == 9 || idc == 10 || idc == 11;
}

// 解析 profile_tier_level 里的**一个** profile 层（general 或 sub-layer）：88 + 8 位。
// info 为空时只消费位（用于跳过 sub-layer PTL）。
bool parse_ptl_profile(BitReader &br, HeifSpsPtlInfo *info) {
    std::uint32_t v = 0;
    if (!br.read_bits(2, v))
        return false;
    const int profile_space = static_cast<int>(v);
    if (!br.read_bits(1, v))
        return false;
    const int tier_flag = static_cast<int>(v);
    if (!br.read_bits(5, v))
        return false;
    const int profile_idc = static_cast<int>(v);
    if (!br.read_bits(32, v))
        return false;
    const std::uint32_t compat = v;
    int flags[4] = {0, 0, 0, 0};
    for (int &flag : flags) {
        if (!br.read_bits(1, v))
            return false;
        flag = static_cast<int>(v);
    }
    int bits44[44] = {};
    for (int &b : bits44) {
        if (!br.read_bits(1, v))
            return false;
        b = static_cast<int>(v);
    }
    if (!br.read_bits(8, v))
        return false;
    const int level_idc = static_cast<int>(v);
    if (info == nullptr)
        return true;
    info->profile_space = profile_space;
    info->tier_flag = tier_flag;
    info->profile_idc = profile_idc;
    info->profile_compatibility_flags = compat;
    info->progressive_source = flags[0];
    info->interlaced_source = flags[1];
    info->non_packed = flags[2];
    info->frame_only = flags[3];
    info->level_idc = level_idc;
    info->constraint_bits.clear();
    for (const int f : flags)
        info->constraint_bits.push_back(f != 0 ? '1' : '0');
    for (const int b : bits44)
        info->constraint_bits.push_back(b != 0 ? '1' : '0');
    if (is_rext_profile_idc(profile_idc)) {
        // 具名位顺序 = general_max_12bit … general_lower_bit_rate（9 位）。
        info->max_12bit = bits44[0];
        info->max_10bit = bits44[1];
        info->max_8bit = bits44[2];
        info->max_422chroma = bits44[3];
        info->max_420chroma = bits44[4];
        info->max_monochrome = bits44[5];
        info->intra = bits44[6];
        info->one_picture_only = bits44[7];
        info->lower_bit_rate = bits44[8];
    } else {
        // 非 RExt：这 9 个位置是保留位（H.265 要求写 0），**没有 still 语义** →
        // 置 -1 = 不适用，避免把「保留位 0」误报成「intra=0」。
        info->max_12bit = -1;
        info->max_10bit = -1;
        info->max_8bit = -1;
        info->max_422chroma = -1;
        info->max_420chroma = -1;
        info->max_monochrome = -1;
        info->intra = -1;
        info->one_picture_only = -1;
        info->lower_bit_rate = -1;
    }
    return true;
}

// sps_max_sub_layers_minus1 = 0 时无子层字段；>0 时按 §7.3.3 顺序跳过。
bool skip_sub_layer_ptl(BitReader &br, int max_sub_layers_minus1) {
    if (max_sub_layers_minus1 <= 0)
        return true;
    if (max_sub_layers_minus1 > 7)
        return false;
    std::vector<int> profile_present(static_cast<std::size_t>(max_sub_layers_minus1), 0);
    std::vector<int> level_present(static_cast<std::size_t>(max_sub_layers_minus1), 0);
    std::uint32_t v = 0;
    for (int i = 0; i < max_sub_layers_minus1; ++i) {
        if (!br.read_bits(1, v))
            return false;
        profile_present[static_cast<std::size_t>(i)] = static_cast<int>(v);
        if (!br.read_bits(1, v))
            return false;
        level_present[static_cast<std::size_t>(i)] = static_cast<int>(v);
    }
    for (int i = max_sub_layers_minus1; i < 8; ++i) {
        if (!br.read_bits(2, v))
            return false;
    }
    for (int i = 0; i < max_sub_layers_minus1; ++i) {
        if (profile_present[static_cast<std::size_t>(i)] != 0 && !parse_ptl_profile(br, nullptr))
            return false;
        if (level_present[static_cast<std::size_t>(i)] != 0 && !br.read_bits(8, v))
            return false;
    }
    return true;
}

// 从一个 SPS NAL（含 2 字节头）解析 PTL + DPB。失败返回 false 并填 info.error。
bool parse_sps_nal(const std::uint8_t *nal, std::size_t size, HeifSpsPtlInfo &info) {
    const std::vector<std::uint8_t> rbsp = nal_to_rbsp(nal, size);
    if (rbsp.size() < 5) {
        info.error = "SPS NAL 过短（RBSP < 5 字节）";
        return false;
    }
    BitReader br(rbsp);
    std::uint32_t v = 0;
    if (!br.read_bits(4, v)) { // sps_video_parameter_set_id
        info.error = "SPS 头解析失败";
        return false;
    }
    if (!br.read_bits(3, v)) {
        info.error = "SPS 头解析失败（sps_max_sub_layers_minus1）";
        return false;
    }
    const int max_sub_layers_minus1 = static_cast<int>(v);
    if (!br.read_bits(1, v)) { // sps_temporal_id_nesting_flag
        info.error = "SPS 头解析失败（temporal_id_nesting）";
        return false;
    }
    if (!parse_ptl_profile(br, &info)) {
        info.error = "SPS 的 profile_tier_level 解析失败（位流截断）";
        return false;
    }
    if (!skip_sub_layer_ptl(br, max_sub_layers_minus1)) {
        info.error = "SPS 的子层 profile_tier_level 解析失败";
        return false;
    }
    // —— sps_seq_parameter_set_id … sps_max_dec_pic_buffering（DPB）——
    if (!br.read_ue(v)) {
        info.error = "SPS 的 sps_seq_parameter_set_id 解析失败";
        return false;
    }
    std::uint32_t chroma_format_idc = 0;
    if (!br.read_ue(chroma_format_idc)) {
        info.error = "SPS 的 chroma_format_idc 解析失败";
        return false;
    }
    info.chroma_format_idc = static_cast<int>(chroma_format_idc);
    if (chroma_format_idc == 3) {
        if (!br.read_bits(1, v)) { // separate_colour_plane_flag
            info.error = "SPS 的 separate_colour_plane_flag 解析失败";
            return false;
        }
    }
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!br.read_ue(width) || !br.read_ue(height)) {
        info.error = "SPS 的 pic_width/height 解析失败";
        return false;
    }
    if (!br.read_bits(1, v)) { // conformance_window_flag
        info.error = "SPS 的 conformance_window_flag 解析失败";
        return false;
    }
    if (v != 0) {
        for (int i = 0; i < 4; ++i) {
            std::uint32_t win = 0;
            if (!br.read_ue(win)) {
                info.error = "SPS 的 conformance window 解析失败";
                return false;
            }
        }
    }
    std::uint32_t depth_luma = 0;
    std::uint32_t depth_chroma = 0;
    if (!br.read_ue(depth_luma) || !br.read_ue(depth_chroma)) {
        info.error = "SPS 的 bit_depth_luma/chroma_minus8 解析失败";
        return false;
    }
    info.bit_depth_luma = static_cast<int>(depth_luma) + 8;
    info.bit_depth_chroma = static_cast<int>(depth_chroma) + 8;
    std::uint32_t log2_poc = 0;
    if (!br.read_ue(log2_poc)) {
        info.error = "SPS 的 log2_max_pic_order_cnt_lsb_minus4 解析失败";
        return false;
    }
    if (!br.read_bits(1, v)) { // sps_sub_layer_ordering_info_present_flag
        info.error = "SPS 的 sps_sub_layer_ordering_info_present_flag 解析失败";
        return false;
    }
    const int start = v != 0 ? 0 : max_sub_layers_minus1;
    for (int i = start; i <= max_sub_layers_minus1; ++i) {
        std::uint32_t buffering = 0;
        std::uint32_t reorder = 0;
        std::uint32_t latency = 0;
        if (!br.read_ue(buffering) || !br.read_ue(reorder) || !br.read_ue(latency)) {
            info.error = "SPS 的 DPB（max_dec_pic_buffering / num_reorder_pics）解析失败";
            return false;
        }
        // 取最高 temporal sub-layer 的值（still 语义按最外层判）。
        info.sps_max_dec_pic_buffering_minus1 = static_cast<int>(buffering);
        info.sps_max_num_reorder_pics = static_cast<int>(reorder);
        info.sps_max_latency_increase_plus1 = static_cast<int>(latency);
    }
    info.found = true;
    info.error.clear();
    return true;
}

// 48 位 constraint 的 -1/0/1 → 可读文本（错误文案用）。
std::string flag_text(int flag) {
    if (flag < 0)
        return "不适用（该 profile 的保留位）";
    return flag != 0 ? "1" : "0";
}

} // namespace

bool heif_parse_sps_ptl(const std::vector<std::uint8_t> &hvcc_payload, HeifSpsPtlInfo &info) {
    info = HeifSpsPtlInfo{};
    if (hvcc_payload.size() < 23) {
        info.error = "hvcC 载荷过短（< 23 字节）";
        return false;
    }
    if (hvcc_payload[0] != 1) {
        info.error = "hvcC configurationVersion != 1";
        return false;
    }
    const std::size_t num_arrays = hvcc_payload[22];
    std::size_t pos = 23;
    for (std::size_t a = 0; a < num_arrays; ++a) {
        if (pos + 3 > hvcc_payload.size()) {
            info.error = "hvcC 的 NAL 数组被截断（读数组头越界）";
            return false;
        }
        const int nal_type = hvcc_payload[pos] & 0x3f;
        pos += 1;
        const std::size_t num_nalus =
            (static_cast<std::size_t>(hvcc_payload[pos]) << 8) | hvcc_payload[pos + 1];
        pos += 2;
        for (std::size_t n = 0; n < num_nalus; ++n) {
            if (pos + 2 > hvcc_payload.size()) {
                info.error = "hvcC 的 NAL 长度字段被截断";
                return false;
            }
            const std::size_t nal_size =
                (static_cast<std::size_t>(hvcc_payload[pos]) << 8) | hvcc_payload[pos + 1];
            pos += 2;
            if (pos + nal_size > hvcc_payload.size()) {
                info.error = "hvcC 的 NAL 数据被截断";
                return false;
            }
            if (nal_type == 33 && !info.found) { // 首个 SPS
                if (!parse_sps_nal(hvcc_payload.data() + pos, nal_size, info))
                    return false;
            }
            pos += nal_size;
        }
    }
    if (!info.found) {
        info.error = "hvcC 里没有 SPS（nal_unit_type=33）";
        return false;
    }
    return true;
}

bool heif_parse_sps_ptl(const std::filesystem::path &path, HeifSpsPtlInfo &info) {
    info = HeifSpsPtlInfo{};
    HeifHvccInfo hvcc;
    std::vector<std::uint8_t> payload;
    if (!scan_file_for_hvcc(path, hvcc, &payload)) {
        info.error = hvcc.error.empty() ? std::string("无法读取 hvcC") : hvcc.error;
        return false;
    }
    return heif_parse_sps_ptl(payload, info);
}

std::string heif_sps_still_error_zh(HeifProfile profile, const HeifSpsPtlInfo &info) {
    if (!heif_profile_is_still_variant(profile))
        return std::string();
    const std::string label = heif_profile_label(profile);
    HeifSignalSpec spec;
    if (!heif_profile_signal(profile, &spec))
        return "编码档位「" + label + "」不是可校验的 still 变体。";
    if (!info.found)
        return "编码档位「" + label + "」要求 SPS 写出 still 约束，但产物里没有可解析的 SPS（" +
               info.error + "）。";
    if (info.profile_idc != spec.profile_idc)
        return "编码档位「" + label +
               "」要求 SPS general_profile_idc=" + std::to_string(spec.profile_idc) + "，实际为 " +
               std::to_string(info.profile_idc) + "（48 位 constraint=" + info.constraint_bits +
               "）。";
    if (profile == HeifProfile::Main10Still) {
        // Main10（idc 2）的 PTL 没有 still 位（具名位不适用）→ 唯一可用的「单图」证据是 DPB。
        if (info.intra >= 0 || info.one_picture_only >= 0)
            return "编码档位「" + label +
                   "」的内部校验失败：非 RExt 的 PTL 不应有 intra/one_picture 位。";
        if (info.sps_max_dec_pic_buffering_minus1 == 0 && info.sps_max_num_reorder_pics == 0)
            return std::string();
        return "编码档位「" + label +
               "」无法写出 still（单图）语义：idc=2（Main10）的 PTL 没有 intra/one_picture "
               "约束位（非 RExt 的 44 位是保留位），SPS 的 DPB 约束实测 "
               "max_dec_pic_buffering_minus1=" +
               std::to_string(info.sps_max_dec_pic_buffering_minus1) +
               " / num_reorder_pics=" + std::to_string(info.sps_max_num_reorder_pics) +
               "（要求 0 / 0）。该档位不可用（UI 已置灰），请改选 Main10（同 idc 2 的普通单图"
               "信号）或 RExt still 档位（如 Main 4:4:4 10 Still Picture）。";
    }
    if (info.intra == 1 && info.one_picture_only == 1)
        return std::string();
    return "编码档位「" + label +
           "」要求 SPS 的 PTL 写出 intra=1 && one_picture_only=1（RExt still 约束），实际为 "
           "intra=" +
           flag_text(info.intra) + " / one_picture_only=" + flag_text(info.one_picture_only) +
           "（profile_idc=" + std::to_string(info.profile_idc) +
           "，48 位 constraint=" + info.constraint_bits + "）。";
}

} // namespace pp
