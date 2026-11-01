// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: HEIF 编码能力纯核（Level 表 / Level 拟合 / grid 规划 / hvcC）
//
// 边界取自任务实测：
//   * 1024×960 = 983,040 = Level 3.1 的 MaxLumaPs（精确边界）；1024×961 越界；
//   * 3902×5854 = 22,842,308：Level 3.1/4.0/4.1/5.0/5.1/5.2 装不下，6.0/6.1/6.2 装得下；
//   * super/T7.heic（Apple 硬件编码）hvcC: idc=2、general_level_idc=93、chroma=1、10bit。
// C17'（0.4.0.alpha2）：grid tile 不再用固定基准，由规划 Level 的 MaxLumaPs(M) +
// 单边上限 D=floor(sqrt(8M)) 与图像宽高推导（面积优先、同面积取最贴近图像比例）——
// 例如 3902×5854 + L3.1 → 768×1280 = 983,040 = M31 整值，6×5。公式/规范证据：
// docs/heif-tile-sizing.md；std 上界证据：heif_still_picture_interop_max_level_idc()。
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "codecs/heif_capability.h"

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool ok, const std::string &case_name, const std::string &detail = std::string()) {
    ++g_checks;
    if (ok)
        return;
    ++g_failed;
    std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    std::fflush(stdout);
}

void info(const std::string &text) {
    std::printf("info %s\n", text.c_str());
    std::fflush(stdout);
}

namespace fs = std::filesystem;

void test_level_table() {
    std::size_t count = 0;
    const pp::HeifLevelSpec *table = pp::heif_level_table(&count);
    check(table != nullptr && count == 18, "level/table/count", std::to_string(count));
    struct Row {
        int idc;
        const char *name;
        int64_t max_luma;
    };
    const Row rows[] = {
        {10, "1.0", 36864},     {20, "2.0", 122880},    {21, "2.1", 245760},
        {30, "3.0", 552960},    {31, "3.1", 983040},    {40, "4.0", 2228224},
        {41, "4.1", 2228224},   {50, "5.0", 8912896},   {51, "5.1", 8912896},
        {52, "5.2", 8912896},   {60, "6.0", 35651584},  {61, "6.1", 35651584},
        {62, "6.2", 35651584},  {63, "6.3", 80216064},  {70, "7.0", 142606336},
        {71, "7.1", 142606336}, {72, "7.2", 142606336},
    };
    for (const Row &r : rows) {
        check(pp::heif_level_max_luma_ps(r.idc) == r.max_luma, "level/max/" + std::to_string(r.idc),
              std::to_string(pp::heif_level_max_luma_ps(r.idc)));
        check(std::string(pp::heif_level_name(r.idc)) == r.name,
              "level/name/" + std::to_string(r.idc), pp::heif_level_name(r.idc));
    }
    check(pp::heif_level_max_luma_ps(0) == 0, "level/max/auto");
    check(std::string(pp::heif_level_name(0)) == "自动", "level/name/auto");
    check(pp::heif_level_max_luma_ps(55) == -1, "level/max/unknown");
    check(std::string(pp::heif_level_name(55)) == "未知", "level/name/unknown");
    check(pp::heif_level_max_luma_ps(85) > 0, "level/max/85");
}

void test_level_idc_conversion() {
    // x265 参数编号（10/20/…/72）与码流 general_level_idc（30/60/…/216）是两套编号。
    check(pp::heif_level_general_idc(0) == 0, "level/general/auto");
    check(pp::heif_level_general_idc(10) == 30, "level/general/1.0");
    check(pp::heif_level_general_idc(21) == 63, "level/general/2.1");
    check(pp::heif_level_general_idc(31) == 93, "level/general/3.1");
    check(pp::heif_level_general_idc(62) == 186, "level/general/6.2");
    check(pp::heif_level_general_idc(72) == 216, "level/general/7.2");
    check(pp::heif_level_general_idc(85) == 255, "level/general/8.5");
    check(pp::heif_level_parameter_idc(0) == 0, "level/param/auto");
    check(pp::heif_level_parameter_idc(93) == 31, "level/param/3.1");
    check(pp::heif_level_parameter_idc(186) == 62, "level/param/6.2");
    check(pp::heif_level_parameter_idc(255) == 85, "level/param/8.5");
    check(pp::heif_level_parameter_idc(94) == -1, "level/param/not_multiple");
}

void test_level_fit() {
    constexpr int kT1Width = 3902;
    constexpr int kT1Height = 5854;
    check(pp::heif_level_fits(0, kT1Width, kT1Height), "fit/auto/t1");
    check(pp::heif_level_fits(31, 800, 600), "fit/31/800x600");
    check(pp::heif_level_fits(31, 1024, 960), "fit/31/exact_boundary");
    check(!pp::heif_level_fits(31, 1024, 961), "fit/31/over_boundary");
    check(!pp::heif_level_fits(31, kT1Width, kT1Height), "fit/31/t1");
    // 任务实测：T1 在 3.1/4.0/4.1/5.0/5.1/5.2 非法，在 6.0/6.1/6.2 合法。
    const int too_small[] = {30, 31, 40, 41, 50, 51, 52};
    for (const int level : too_small)
        check(!pp::heif_level_fits(level, kT1Width, kT1Height),
              "fit/t1/illegal/" + std::to_string(level));
    const int fits[] = {60, 61, 62, 63, 70, 71, 72};
    for (const int level : fits)
        check(pp::heif_level_fits(level, kT1Width, kT1Height),
              "fit/t1/legal/" + std::to_string(level));
    check(pp::heif_level_fits(85, 20000, 20000), "fit/85/unbounded");
    check(!pp::heif_level_fits(55, 100, 100), "fit/unknown_level");
    check(!pp::heif_level_fits(31, 0, 576), "fit/invalid_dims");
    check(pp::heif_level_fits(0, -1, -1), "fit/auto_invalid_dims");
    // 溢出安全：int64 乘法而不是 int。
    check(!pp::heif_level_fits(31, 65535, 65535), "fit/overflow_safe");
    // x265 先把宽高向上补齐到 minCUSize(8) 的倍数再判 Level：3902x2284 原始
    // 8,912,168 <= Level 5.2 的 8,912,896，但补齐 3904x2288 = 8,932,352 后非法
    // （Phase 1 实测的 "picture dimensions are out of range"）。
    check(!pp::heif_level_fits(52, 3902, 2284), "fit/padding/over");
    check(pp::heif_level_fits(52, 3896, 2280), "fit/padding/under");
    // x265 enforceLevel 的宽高上限 = sqrt(8×MaxLumaPs)（Level 4.0 → ~4222）。
    check(!pp::heif_level_fits(40, 5000, 100), "fit/dim_cap/width_over");
    check(pp::heif_level_fits(40, 4200, 100), "fit/dim_cap/width_under");
}

void test_level_min_and_error() {
    check(pp::heif_level_min_idc(3902LL * 5854) == 60, "min/t1");
    check(pp::heif_level_min_idc(983040) == 31, "min/exact_31");
    check(pp::heif_level_min_idc(983041) == 40, "min/just_over_31");
    check(pp::heif_level_min_idc(36864) == 10, "min/exact_10");
    check(pp::heif_level_min_idc(36865) == 20, "min/just_over_10");
    check(pp::heif_level_min_idc(142606336) == 70, "min/exact_72_max");
    check(pp::heif_level_min_idc(142606337) == 85, "min/over_72");
    const std::string err = pp::heif_level_fit_error_zh(31, 3902, 5854);
    check(err.find("22842308") != std::string::npos, "error/t1/samples", err);
    check(err.find("3.1") != std::string::npos, "error/t1/level", err);
    check(err.find("983040") != std::string::npos, "error/t1/limit", err);
    check(err.find("6.0") != std::string::npos, "error/t1/suggestion", err);
    check(err.find("grid") != std::string::npos, "error/t1/grid", err);
    const std::string unknown = pp::heif_level_fit_error_zh(55, 100, 100);
    check(unknown.find("不是 x265 4.3") != std::string::npos, "error/unknown", unknown);
    // 建议 Level 必须按补齐后的样本数算：3902x2284 原始面积 < L5.2 上限，但补齐后需 6.0。
    const std::string padded = pp::heif_level_fit_error_zh(52, 3902, 2284);
    check(padded.find("6.0") != std::string::npos, "error/padded/suggestion", padded);
    check(padded.find("8932352") != std::string::npos, "error/padded/samples", padded);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：断言辅助函数的四元组按调用点语义排列
void check_plan(const pp::HeifGridPlan &plan, bool use_grid, int tw, int th, int cols, int rows,
                const std::string &tag) {
    check(plan.use_grid == use_grid, "grid/use/" + tag);
    if (!use_grid)
        return;
    check(plan.tile_width == tw, "grid/tw/" + tag,
          "want " + std::to_string(tw) + " got " + std::to_string(plan.tile_width));
    check(plan.tile_height == th, "grid/th/" + tag,
          "want " + std::to_string(th) + " got " + std::to_string(plan.tile_height));
    check(plan.cols == cols, "grid/cols/" + tag, std::to_string(plan.cols));
    check(plan.rows == rows, "grid/rows/" + tag, std::to_string(plan.rows));
    check(plan.tile_luma_samples == static_cast<int64_t>(plan.tile_width) * plan.tile_height,
          "grid/tile_luma/" + tag);
}

void test_grid_plan() {
    constexpr int w = 3902, h = 5854;
    // 0 = 关闭；2 = 自动（Level 3.1 装不下 → grid）；1 = 总是 grid。
    check_plan(pp::heif_plan_grid(0, 31, w, h, 1), false, 0, 0, 0, 0, "off/31");
    check_plan(pp::heif_plan_grid(0, 62, w, h, 1), false, 0, 0, 0, 0, "off/62");
    check_plan(pp::heif_plan_grid(2, 62, w, h, 1), false, 0, 0, 0, 0, "auto/fits");
    check_plan(pp::heif_plan_grid(2, 0, w, h, 1), false, 0, 0, 0, 0, "auto/level0");
    check_plan(pp::heif_plan_grid(9, 31, w, h, 1), false, 0, 0, 0, 0, "invalid_mode");
    // 未知 Level：不能规划 grid（上游 x265 只会报“specified level ... does not exist”）。
    const pp::HeifGridPlan unknown_level = pp::heif_plan_grid(2, 55, w, h, 1);
    check(!unknown_level.use_grid, "grid/unknown_level/no_grid");
    check(!unknown_level.level_fits_image, "grid/unknown_level/not_fit");
    check(!unknown_level.ok, "grid/unknown_level/ok_false");
    check(!unknown_level.error_zh.empty(), "grid/unknown_level/error", unknown_level.error_zh);

    // 关闭 + 显式低 Level：不静默降级，给中文可操作错误（点名分块模式=关闭）。
    const pp::HeifGridPlan closed_over = pp::heif_plan_grid(0, 31, w, h, 1);
    check(!closed_over.ok, "grid/closed_over/ok_false");
    check(closed_over.error_zh.find("关闭") != std::string::npos, "grid/closed_over/closed",
          closed_over.error_zh);
    check(closed_over.error_zh.find("983040") != std::string::npos, "grid/closed_over/limit",
          closed_over.error_zh);

    // C17'：T1 3902×5854 + L3.1 → 面积优先枚举出 768×1280（=983,040=M31 整值）；
    // cols=ceil(3902/768)=6，rows=ceil(5854/1280)=5。
    const pp::HeifGridPlan always = pp::heif_plan_grid(1, 31, w, h, 1);
    check_plan(always, true, 768, 1280, 6, 5, "always/31");
    check(always.tile_luma_samples == 983040, "grid/always/tile_luma_exact",
          std::to_string(always.tile_luma_samples));
    check(always.tile_luma_samples == pp::heif_level_max_luma_ps(31), "grid/always/full_level_m31");
    check(always.planning_level_idc == 31, "grid/always/planning_level",
          std::to_string(always.planning_level_idc));
    check(!always.still_level_capped, "grid/always/not_still_capped");
    check(always.level_fits_image == false, "grid/always/level_fits_image");
    check_plan(pp::heif_plan_grid(2, 31, w, h, 1), true, 768, 1280, 6, 5, "auto/31");
    // 显式 Level 6.0 装得下整图：grid=自动 → 单图（默认行为不变）；grid=总是 → 强制分块，
    // tile clamp 到图像尺寸（3902→3896、5854→5848，8 对齐向下）→ 2×2。
    check_plan(pp::heif_plan_grid(2, 60, w, h, 1), false, 0, 0, 0, 0, "auto/60_fits");
    check_plan(pp::heif_plan_grid(1, 60, w, h, 1), true, 3896, 5848, 2, 2, "always/60_clamp_image");
    check(pp::heif_plan_grid(1, 60, w, h, 1).tile_luma_samples == 22783808,
          "grid/always/60/tile_luma");
    check_plan(pp::heif_plan_grid(1, 62, w, h, 1), true, 3896, 5848, 2, 2, "always/62");

    // 小图 clamp：tile 不得大于图像；grid 1×1 无收益 → 降级单图（W2-T1 语义，既有
    // M9 的 "use_grid=true / 1×1" 行为已按 C16/C17' 修订并登记）。
    const pp::HeifGridPlan small420 = pp::heif_plan_grid(1, 0, 800, 400, 1);
    check_plan(small420, false, 800, 400, 1, 1, "small/420");
    check(small420.downgraded_single, "small/420/downgraded");
    check_plan(pp::heif_plan_grid(1, 0, 801, 601, 3), true, 800, 600, 2, 2, "small/444_odd");
    // 1024×576 恰好一个 tile → 单图降级（grid=总是 也不再产出 1×1 grid）。
    const pp::HeifGridPlan one_tile = pp::heif_plan_grid(1, 0, 1024, 576, 1);
    check(!one_tile.use_grid && one_tile.downgraded_single, "grid/one_tile/downgraded");
    // 图像本身小于 64 像素：grid 无收益 → 降级单图（不报错、不产生 <64 的 tile）。
    const pp::HeifGridPlan tiny = pp::heif_plan_grid(1, 0, 50, 50, 1);
    check(!tiny.use_grid && tiny.downgraded_single && tiny.ok, "grid/tiny/downgraded");
    check(tiny.note_zh.find("单图") != std::string::npos, "grid/tiny/note", tiny.note_zh);
    // 显式低 Level 装不下整图、但图像某一轴 <64 → 无法规划合法 tile → 中文错误。
    const pp::HeifGridPlan tiny_over = pp::heif_plan_grid(2, 10, 50, 2000, 1);
    check(!tiny_over.ok && !tiny_over.use_grid, "grid/tiny_over/ok_false");
    check(!tiny_over.error_zh.empty(), "grid/tiny_over/error", tiny_over.error_zh);

    // 小 Level：tile 面积必须 = MaxLumaPs（面积优先用满 Level 容量），且色度对齐保持偶数。
    const pp::HeifGridPlan level30 = pp::heif_plan_grid(2, 30, w, h, 1);
    check(level30.use_grid, "grid/level30/use");
    check_plan(level30, true, 576, 960, 7, 7, "grid/level30/tile");
    check(level30.tile_luma_samples == pp::heif_level_max_luma_ps(30), "grid/level30/area_exact",
          std::to_string(level30.tile_luma_samples));
    check(level30.tile_width % 2 == 0 && level30.tile_height % 2 == 0,
          "grid/level30/alignment_420");
    check(level30.cols == (w + level30.tile_width - 1) / level30.tile_width,
          "grid/level30/cols_formula");
    check(level30.rows == (h + level30.tile_height - 1) / level30.tile_height,
          "grid/level30/rows_formula");

    const pp::HeifGridPlan level10 = pp::heif_plan_grid(2, 10, w, h, 1);
    check(level10.use_grid, "grid/level10/use");
    check_plan(level10, true, 144, 256, 28, 23, "grid/level10/tile");
    check(level10.tile_luma_samples == pp::heif_level_max_luma_ps(10), "grid/level10/area_exact",
          std::to_string(level10.tile_luma_samples));

    const pp::HeifGridPlan c422 = pp::heif_plan_grid(2, 31, w, h, 2);
    check(c422.use_grid && c422.tile_width % 2 == 0, "grid/422/width_even");
    const pp::HeifGridPlan c420 = pp::heif_plan_grid(2, 31, w, h, 1);
    check(c420.tile_width % 2 == 0 && c420.tile_height % 2 == 0, "grid/420/both_even");

    const pp::HeifGridPlan invalid = pp::heif_plan_grid(1, 31, 0, 100, 1);
    check(!invalid.use_grid && invalid.tile_width == 0, "grid/invalid_dims");
    check(!invalid.ok, "grid/invalid_dims/ok_false");
}

void test_grid_modes_and_manual() {
    // —— 两套编号唯一转换点（heif_grid 用户面 vs __heif_grid 兼容别名）——
    check(pp::heif_grid_mode_from_param_value(0) == 0, "mode/new/off");
    check(pp::heif_grid_mode_from_param_value(1) == 2, "mode/new/auto");
    check(pp::heif_grid_mode_from_param_value(2) == 1, "mode/new/always");
    check(pp::heif_grid_mode_from_param_value(9) == -1, "mode/new/invalid");
    check(pp::heif_grid_mode_from_legacy(0) == 0, "mode/legacy/off");
    check(pp::heif_grid_mode_from_legacy(1) == 1, "mode/legacy/always");
    check(pp::heif_grid_mode_from_legacy(2) == 2, "mode/legacy/auto");
    check(pp::heif_grid_mode_from_legacy(7) == -1, "mode/legacy/invalid");
    check(std::string(pp::heif_grid_mode_name(0)) == "关闭", "mode/name/off");
    check(std::string(pp::heif_grid_mode_name(1)) == "总是", "mode/name/always");
    check(std::string(pp::heif_grid_mode_name(2)) == "自动", "mode/name/auto");
    check(std::string(pp::heif_grid_mode_name(9)) == "未知", "mode/name/unknown");

    // C16：Level 行上移核心区；grid/tile 保持高级。
    check(pp::heif_param_in_core("x265:level-idc"), "core/level-idc");
    check(!pp::heif_param_in_core("heif_grid"), "core/heif_grid");
    check(!pp::heif_param_in_core("heif_tile_w"), "core/heif_tile_w");

    constexpr int w = 3902, h = 5854;
    // 显式低 Level → 自动分块（tile 由规划 Level 的 MaxLumaPs 推导：L3.1 → 768×1280）。
    const pp::HeifGridPlan auto_low = pp::heif_plan_grid(2, 31, w, h, 1);
    check(auto_low.use_grid && auto_low.ok, "auto/low/use", auto_low.error_zh);
    check(auto_low.tile_width == 768 && auto_low.tile_height == 1280, "auto/low/tile");
    check(auto_low.cols == 6 && auto_low.rows == 5, "auto/low/grid");
    check(auto_low.total_tiles() == 30, "auto/low/total");
    check(auto_low.tile_level_idc == 31, "auto/low/tile_level");
    check(!auto_low.manual_tile, "auto/low/not_manual");

    // 总是 grid + Level=自动 → 规划上界 = still 互操作档 Level 5.1（M=8,912,896）；
    // T1 面积优先枚举出 2176×4096（=M51 整值）→ 2×2；每 tile 的 Level 仍由 x265 自动选。
    const pp::HeifGridPlan always_auto_level = pp::heif_plan_grid(1, 0, w, h, 1);
    check(always_auto_level.use_grid && always_auto_level.ok, "always/apple/use",
          always_auto_level.error_zh);
    check(always_auto_level.tile_width == 2176 && always_auto_level.tile_height == 4096,
          "always/apple/tile");
    check(always_auto_level.tile_luma_samples == pp::heif_level_max_luma_ps(51),
          "always/apple/full_level_m51", std::to_string(always_auto_level.tile_luma_samples));
    check(always_auto_level.cols == 2 && always_auto_level.rows == 2, "always/apple/grid");
    check(always_auto_level.planning_level_idc == 51, "always/apple/planning_level");
    check(always_auto_level.tile_level_idc == 0, "always/apple/tile_level_auto");

    // Level=自动 + 常规尺寸 → 单图（默认产物零变化）。
    check(!pp::heif_plan_grid(2, 0, 1024, 576, 1).use_grid, "auto/normal/single");

    // Level=自动 + 超出 x265 最高 Level 7.2 容量 → 自动分块（规划上界 5.1）。
    const pp::HeifGridPlan oversize = pp::heif_plan_grid(2, 0, 20000, 20000, 1);
    check(oversize.use_grid && oversize.ok, "auto/oversize/use", oversize.error_zh);
    check(oversize.tile_width == 2176 && oversize.tile_height == 4096, "auto/oversize/tile");
    check(oversize.cols == 10 && oversize.rows == 5, "auto/oversize/grid");
    check(oversize.tile_level_idc == 0, "auto/oversize/tile_level_auto");

    // —— 手动 tile：合法 ——
    pp::HeifGridRequest manual;
    manual.grid_mode = 2;
    manual.level_idc = 31;
    manual.width = w;
    manual.height = h;
    manual.chroma = 1;
    manual.tile_width = 480;
    manual.tile_height = 256;
    const pp::HeifGridPlan manual_plan = pp::heif_plan_grid(manual);
    check(manual_plan.use_grid && manual_plan.ok, "manual/ok/use", manual_plan.error_zh);
    check(manual_plan.manual_tile, "manual/ok/flag");
    check(manual_plan.tile_width == 480 && manual_plan.tile_height == 256, "manual/ok/tile");
    check(manual_plan.cols == 9 && manual_plan.rows == 23, "manual/ok/grid");
    check(manual_plan.tile_level_idc == 31, "manual/ok/level");
    check(manual_plan.tile_luma_samples <= pp::heif_level_max_luma_ps(31), "manual/ok/area");

    // 单轴手动：宽 512、高自动（Level 3.1 下高取自动推导值 1280，再按 Level 容量收缩）。
    manual.tile_width = 512;
    manual.tile_height = 0;
    const pp::HeifGridPlan manual_w_only = pp::heif_plan_grid(manual);
    check(manual_w_only.use_grid && manual_w_only.ok, "manual/w_only/use", manual_w_only.error_zh);
    check(manual_w_only.tile_width == 512 && manual_w_only.tile_height == 1280,
          "manual/w_only/tile");
    check(manual_w_only.cols == 8 && manual_w_only.rows == 5, "manual/w_only/grid");

    // 8 向下对齐 + 记录原值（1004→1000、300→296）。
    manual.tile_width = 1004;
    manual.tile_height = 300;
    const pp::HeifGridPlan aligned = pp::heif_plan_grid(manual);
    check(aligned.ok, "manual/align/ok", aligned.error_zh);
    check(aligned.tile_width == 1000 && aligned.tile_height == 296, "manual/align/tile");
    check(aligned.aligned_width_from == 1004 && aligned.aligned_height_from == 300,
          "manual/align/from");

    // —— 手动 tile：非法（中文预检错误，点名上限/最大可行 tile/三条出路）——
    manual.tile_width = 1024;
    manual.tile_height = 1024;
    const pp::HeifGridPlan too_big = pp::heif_plan_grid(manual);
    check(!too_big.ok && !too_big.use_grid, "manual/too_big/ok_false");
    check(too_big.error_zh.find("983040") != std::string::npos, "manual/too_big/limit",
          too_big.error_zh);
    check(too_big.error_zh.find("最大可行 tile") != std::string::npos, "manual/too_big/max_tile",
          too_big.error_zh);
    // C17'：最大可行 tile 提示也是推导值（L3.1 → 768×1280 = 983,040 亮度样本）。
    check(too_big.error_zh.find("768×1280") != std::string::npos, "manual/too_big/max_tile_value",
          too_big.error_zh);
    check(too_big.error_zh.find("三条出路") != std::string::npos, "manual/too_big/options",
          too_big.error_zh);
    check(too_big.error_zh.find("提高") != std::string::npos, "manual/too_big/raise",
          too_big.error_zh);

    manual.tile_width = 32;
    manual.tile_height = 128;
    const pp::HeifGridPlan too_small = pp::heif_plan_grid(manual);
    check(!too_small.ok && !too_small.use_grid, "manual/too_small/ok_false");
    check(too_small.error_zh.find("64") != std::string::npos, "manual/too_small/min",
          too_small.error_zh);

    manual.tile_width = 9000;
    manual.tile_height = 256;
    const pp::HeifGridPlan out_of_range = pp::heif_plan_grid(manual);
    check(!out_of_range.ok && out_of_range.error_zh.find("8192") != std::string::npos,
          "manual/range", out_of_range.error_zh);

    // 单边上限：Level 3.1 的 D≈2803，4096 宽即使面积不大也非法。
    manual.tile_width = 4096;
    manual.tile_height = 128;
    const pp::HeifGridPlan wide = pp::heif_plan_grid(manual);
    check(!wide.ok && wide.error_zh.find("2804") != std::string::npos, "manual/dim_cap",
          wide.error_zh);

    // rows×cols 超过 65535（HEIF grid 单边 u8+1，乘积上限 65535）。
    manual.level_idc = 0;
    manual.width = 20000;
    manual.height = 20000;
    manual.tile_width = 64;
    manual.tile_height = 64;
    const pp::HeifGridPlan too_many = pp::heif_plan_grid(manual);
    check(!too_many.ok && too_many.error_zh.find("65535") != std::string::npos,
          "manual/tiles_limit", too_many.error_zh);

    // 手动 tile ≥ 图像（1×1）→ 降级单图（不因 tile 超 Level 报错）。
    manual.level_idc = 31;
    manual.width = 1024;
    manual.height = 576;
    manual.tile_width = 4096;
    manual.tile_height = 4096;
    const pp::HeifGridPlan downgraded = pp::heif_plan_grid(manual);
    check(!downgraded.use_grid && downgraded.downgraded_single && downgraded.ok, "manual/downgrade",
          downgraded.error_zh);
    check(downgraded.note_zh.find("降级") != std::string::npos, "manual/downgrade/note",
          downgraded.note_zh);

    // Level=自动 + 手动 tile → 允许 + note（iPhone 接受度未验证）。
    manual.level_idc = 0;
    manual.width = 1024;
    manual.height = 576;
    manual.tile_width = 512;
    manual.tile_height = 288;
    const pp::HeifGridPlan auto_manual = pp::heif_plan_grid(manual);
    check(auto_manual.use_grid && auto_manual.ok, "manual/auto_level/use", auto_manual.error_zh);
    check(auto_manual.manual_with_auto_level, "manual/auto_level/flag");
    check(auto_manual.tile_level_idc == 0, "manual/auto_level/tile_level_auto");
    check(auto_manual.note_zh.find("iPhone") != std::string::npos, "manual/auto_level/note",
          auto_manual.note_zh);

    // grid=关闭 + 手动 tile：忽略手动值（单图），不误报。
    manual.grid_mode = 0;
    const pp::HeifGridPlan closed_manual = pp::heif_plan_grid(manual);
    check(!closed_manual.use_grid && closed_manual.ok, "manual/closed/not_grid");
    check(closed_manual.note_zh.find("忽略") != std::string::npos, "manual/closed/note",
          closed_manual.note_zh);

    // MIAF 奇偶：只 warn 不阻断（801×601 + 420 → warn；444 / 422 宽偶 → 不 warn）。
    pp::HeifGridRequest odd;
    odd.grid_mode = 1;
    odd.width = 801;
    odd.height = 601;
    odd.chroma = 1;
    const pp::HeifGridPlan odd420 = pp::heif_plan_grid(odd);
    check(odd420.use_grid && odd420.miaf_parity_warn, "miaf/420/warn");
    check(odd420.warning_zh.find("MIAF") != std::string::npos, "miaf/420/text", odd420.warning_zh);
    odd.chroma = 3;
    check(!pp::heif_plan_grid(odd).miaf_parity_warn, "miaf/444/no_warn");
    // 4:2:2 只要求宽为偶数：800（偶）不 warn，801（奇）warn。
    odd.chroma = 2;
    odd.width = 800;
    odd.height = 601;
    check(!pp::heif_plan_grid(odd).miaf_parity_warn, "miaf/422/width_even_no_warn");
    odd.width = 801;
    check(pp::heif_plan_grid(odd).miaf_parity_warn, "miaf/422/width_odd_warn");
}

// C17'：still 单图档位 + 显式 Level > 5.1 → 规划压到 still 互操作上界 5.1（3GPP
// TS 26.140/26.141，itemTypes hvc1.1.2.L153），编码 Level 保留用户值（不静默改参数），
// warning_zh 如实说明；非 still 档位不受此限；Level=自动的规划上界固定 5.1。
void test_still_level_cap() {
    using pp::HeifProfile;
    constexpr int w = 3902, h = 5854;
    check(pp::heif_still_picture_interop_max_level_idc() == 51,
          "still_cap/const_is_5_1"); // 153 = 3 × 51
    pp::HeifGridRequest req;
    req.level_idc = 60;
    req.width = w;
    req.height = h;
    req.chroma = 1;
    req.profile = HeifProfile::Main444_10Still;

    // grid=自动 + 整图在 L6.0 装得下 → 单图；但 still Level 上界偏差仍要 warn。
    req.grid_mode = 2;
    const pp::HeifGridPlan auto_fits = pp::heif_plan_grid(req);
    check(!auto_fits.use_grid, "still_cap/auto_fits/single");
    check(auto_fits.ok, "still_cap/auto_fits/ok", auto_fits.error_zh);
    check(auto_fits.still_level_capped, "still_cap/auto_fits/flag");
    check(auto_fits.planning_level_idc == 51, "still_cap/auto_fits/planning_level",
          std::to_string(auto_fits.planning_level_idc));
    check(auto_fits.warning_zh.find("5.1") != std::string::npos, "still_cap/auto_fits/warn_5_1",
          auto_fits.warning_zh);
    check(auto_fits.warning_zh.find("3GPP") != std::string::npos, "still_cap/auto_fits/warn_3gpp",
          auto_fits.warning_zh);
    check(auto_fits.warning_zh.find("L153") != std::string::npos, "still_cap/auto_fits/warn_l153",
          auto_fits.warning_zh);
    check(auto_fits.warning_zh.find("不静默改") != std::string::npos,
          "still_cap/auto_fits/warn_no_silent_change", auto_fits.warning_zh);

    // grid=总是 → tile 按 5.1 规划（2176×4096 = M51 整值）；编码 Level 仍 = 用户值 60。
    req.grid_mode = 1;
    const pp::HeifGridPlan always = pp::heif_plan_grid(req);
    check_plan(always, true, 2176, 4096, 2, 2, "still_cap/always");
    check(always.tile_level_idc == 60, "still_cap/always/tile_level_user_value",
          std::to_string(always.tile_level_idc));
    check(always.planning_level_idc == 51, "still_cap/always/planning_level");
    check(always.warning_zh.find("5.1") != std::string::npos, "still_cap/always/warn");
    check(always.note_zh.find("5.1") != std::string::npos, "still_cap/always/note", always.note_zh);

    // 非 still 档位（Main10，idc=2）+ L6.0：规划仍按用户 Level → clamp 到图像尺寸 2×2。
    req.profile = HeifProfile::Main10;
    const pp::HeifGridPlan non_still = pp::heif_plan_grid(req);
    check(!non_still.still_level_capped, "still_cap/non_still/not_capped");
    check(non_still.planning_level_idc == 60, "still_cap/non_still/planning_user_level");
    check(non_still.warning_zh.empty(), "still_cap/non_still/no_warn", non_still.warning_zh);
    check_plan(non_still, true, 3896, 5848, 2, 2, "still_cap/non_still/tile");

    // Main Still Picture（idc=3）同样是 still 单图档位 → 同样受 5.1 上界约束。
    req.profile = HeifProfile::MainStill;
    check(pp::heif_plan_grid(req).still_level_capped, "still_cap/main_still/capped");

    // L5.1 本身（=上界）不触发压缩。
    req.profile = HeifProfile::Main444_10Still;
    req.level_idc = 51;
    const pp::HeifGridPlan at_cap = pp::heif_plan_grid(req);
    check(!at_cap.still_level_capped, "still_cap/at_5_1/not_capped");
    check(at_cap.planning_level_idc == 51, "still_cap/at_5_1/planning");

    // Level=自动：规划上界 = 5.1（每 tile Level 仍由 x265 自动选），无 warning。
    req.level_idc = 0;
    req.grid_mode = 1;
    const pp::HeifGridPlan auto_level = pp::heif_plan_grid(req);
    check(auto_level.planning_level_idc == 51, "still_cap/auto_level/planning_bound");
    check(!auto_level.still_level_capped, "still_cap/auto_level/flag");
    check(auto_level.warning_zh.empty(), "still_cap/auto_level/no_warn", auto_level.warning_zh);
    check_plan(auto_level, true, 2176, 4096, 2, 2, "still_cap/auto_level/tile");

    // 旧 M9 兼容入口（无 profile）：显式 Level>5.1 不压缩（profile=Auto 不是 still 档位）。
    const pp::HeifGridPlan legacy = pp::heif_plan_grid(1, 60, w, h, 1);
    check(!legacy.still_level_capped && legacy.planning_level_idc == 60,
          "still_cap/legacy_entry/no_cap");
}

fs::path find_t7() {
    std::error_code ec;
    const fs::path here = fs::absolute(fs::path(__FILE__), ec);
    const fs::path roots[] = {
        here.parent_path().parent_path().parent_path(), // tests/unit/test_*.cpp → repo
        fs::current_path(),
        fs::current_path() / "..",
        fs::current_path() / "../..",
        fs::current_path() / "../../..",
    };
    for (const fs::path &root : roots) {
        const fs::path candidate = root / "super" / "T7.heic";
        if (fs::exists(candidate, ec) && !ec)
            return candidate;
    }
    return {};
}

// 仓库根（相对 __FILE__ 或 cwd 逐级上探；与 find_t7 同口径）。
fs::path find_repo_root() {
    std::error_code ec;
    const fs::path here = fs::absolute(fs::path(__FILE__), ec);
    const fs::path roots[] = {
        here.parent_path().parent_path().parent_path(),
        fs::current_path(),
        fs::current_path() / "..",
        fs::current_path() / "../..",
        fs::current_path() / "../../..",
    };
    for (const fs::path &root : roots) {
        if (fs::exists(root / "tools" / "heif_matrix_check.py", ec) && !ec)
            return root;
    }
    return {};
}

// ---------------------------------------------------------------------------
// W2-T2（C18）：SPS PTL / DPB 解析器的合成码流用例
//   * 合成 SPS 逐位构造（x265 的实写口径：4 通用位 + RExt 9 具名 + 35 保留 / 非 RExt 44 保留）；
//   * hvcC 的 constraint 字节**故意写 0**（libheif 实测行为）→ 证明解析器读的是 SPS 而非 hvcC；
//   * 可选插入 00 00 03 防竞争字节 → 证明去防竞争字节路径正确（不做会位偏移错）。
// ---------------------------------------------------------------------------
class BitWriter {
public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：bits/value 由位写入语义固定
    void u(int bits, std::uint32_t value) {
        for (int i = bits - 1; i >= 0; --i) {
            if (((value >> i) & 1U) != 0)
                bytes_[bit_ / 8] |= static_cast<std::uint8_t>(0x80U >> (bit_ % 8));
            ++bit_;
        }
    }
    void ue(std::uint32_t value) {
        const std::uint32_t v = value + 1;
        int len = 0;
        for (std::uint32_t t = v; t > 1; t >>= 1)
            ++len;
        u(len, 0);
        u(1, 1);
        u(len, v - (1U << len));
    }
    void rbsp_trailing_bits() {
        u(1, 1);
        while (bit_ % 8 != 0)
            u(1, 0);
    }
    std::vector<std::uint8_t> take() const { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_ = std::vector<std::uint8_t>(256, 0);
    std::size_t bit_ = 0;
};

struct SpsSpec {
    int idc = 4;
    int chroma = 3;
    int bit_depth = 10;
    int dpb_minus1 = 2;
    int reorder = 0;
    int intra = 1;
    int one_picture = 1;
};

// 2 字节 NAL 头（type 33）+ RBSP；add_epb = 插入 00 00 03 防竞争字节。
std::vector<std::uint8_t> make_sps_nal(const SpsSpec &s, bool add_epb) {
    BitWriter w;
    w.u(4, 0); // sps_video_parameter_set_id
    w.u(3, 0); // sps_max_sub_layers_minus1（x265 静图恒 0）
    w.u(1, 1); // sps_temporal_id_nesting_flag
    w.u(2, 0); // general_profile_space
    w.u(1, 0); // general_tier_flag
    w.u(5, static_cast<std::uint32_t>(s.idc));
    w.u(32, 1U << s.idc); // general_profile_compatibility_flag[32]
    w.u(1, 1);            // general_progressive_source_flag
    w.u(1, 0);            // general_interlaced_source_flag
    w.u(1, 1);            // general_non_packed_constraint_flag
    w.u(1, 1);            // general_frame_only_constraint_flag
    if (s.idc == 4) {
        w.u(1, 1);                                         // max_12bit
        w.u(1, s.bit_depth <= 10 ? 1U : 0U);               // max_10bit
        w.u(1, s.bit_depth <= 8 ? 1U : 0U);                // max_8bit
        w.u(1, s.chroma == 2 || s.chroma == 1 ? 1U : 0U);  // max_422chroma
        w.u(1, s.chroma == 1 ? 1U : 0U);                   // max_420chroma
        w.u(1, 0);                                         // max_monochrome
        w.u(1, static_cast<std::uint32_t>(s.intra));       // intra
        w.u(1, static_cast<std::uint32_t>(s.one_picture)); // one_picture_only
        w.u(1, 1);                                         // lower_bit_rate
        for (int i = 0; i < 35; ++i)
            w.u(1, 0); // reserved_zero_35bits
    } else {
        for (int i = 0; i < 44; ++i)
            w.u(1, 0); // 非 RExt：44 位保留（still 位**不存在**）
    }
    w.u(8, 93); // general_level_idc（Level 3.1 = 93）
    w.ue(0);    // sps_seq_parameter_set_id
    w.ue(static_cast<std::uint32_t>(s.chroma));
    if (s.chroma == 3)
        w.u(1, 0);                                     // separate_colour_plane_flag
    w.ue(1024);                                        // pic_width_in_luma_samples
    w.ue(576);                                         // pic_height_in_luma_samples
    w.u(1, 0);                                         // conformance_window_flag
    w.ue(static_cast<std::uint32_t>(s.bit_depth - 8)); // bit_depth_luma_minus8
    w.ue(static_cast<std::uint32_t>(s.bit_depth - 8)); // bit_depth_chroma_minus8
    w.ue(4);                                           // log2_max_pic_order_cnt_lsb_minus4
    w.u(1, 1);                                         // sps_sub_layer_ordering_info_present_flag
    w.ue(static_cast<std::uint32_t>(s.dpb_minus1));
    w.ue(static_cast<std::uint32_t>(s.reorder));
    w.ue(1); // sps_max_latency_increase_plus1
    w.rbsp_trailing_bits();
    const std::vector<std::uint8_t> rbsp = w.take();

    std::vector<std::uint8_t> nal = {0x42, 0x01}; // forbidden_zero=0 / type=33 / layer 0 / tid 1
    std::size_t zeros = 0;
    for (const std::uint8_t byte : rbsp) {
        if (add_epb && zeros >= 2 && byte <= 0x03) {
            nal.push_back(0x03); // emulation_prevention_three_byte
            zeros = 0;
        }
        nal.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    return nal;
}

// 23 字节定长头 + 1 个 SPS 数组；constraint 字节故意为 0（libheif 实测）。
std::vector<std::uint8_t> make_hvcc(const std::vector<std::uint8_t> &sps_nal, const SpsSpec &s) {
    std::vector<std::uint8_t> hvcc(23, 0);
    hvcc[0] = 1; // configurationVersion
    hvcc[1] = static_cast<std::uint8_t>(s.idc & 0x1f);
    hvcc[12] = 93; // level_idc（与 SPS 一致）
    hvcc[16] = static_cast<std::uint8_t>(0xfcU | static_cast<std::uint8_t>(s.chroma & 0x03));
    hvcc[17] =
        static_cast<std::uint8_t>(0xf8U | static_cast<std::uint8_t>((s.bit_depth - 8) & 0x07));
    hvcc[18] = hvcc[17];
    hvcc[22] = 1;         // numOfArrays
    hvcc.push_back(0x21); // array_completeness=1 + NAL_unit_type=33（SPS）
    hvcc.push_back(0x00);
    hvcc.push_back(0x01); // numNalus = 1
    const std::size_t len = sps_nal.size();
    hvcc.push_back(static_cast<std::uint8_t>((len >> 8) & 0xff));
    hvcc.push_back(static_cast<std::uint8_t>(len & 0xff));
    hvcc.insert(hvcc.end(), sps_nal.begin(), sps_nal.end());
    return hvcc;
}

void test_sps_ptl_parser() {
    using pp::HeifProfile;
    // ① RExt 4:4:4 10 静止图（插防竞争字节）→ intra/one_picture/上界位全部读对。
    {
        SpsSpec spec;
        spec.idc = 4;
        spec.chroma = 3;
        spec.bit_depth = 10;
        const std::vector<std::uint8_t> hvcc =
            make_hvcc(make_sps_nal(spec, /*add_epb=*/true), spec);
        // hvcC 的 constraint 字节必须仍是 0（libheif 实测）——证明下面的值只能来自 SPS。
        check(hvcc[6] == 0 && hvcc[7] == 0 && hvcc[8] == 0 && hvcc[9] == 0 && hvcc[10] == 0 &&
                  hvcc[11] == 0,
              "sps/synthetic/hvcc_constraint_zero");
        pp::HeifSpsPtlInfo info;
        const bool ok = pp::heif_parse_sps_ptl(hvcc, info);
        check(ok, "sps/synthetic/rext444_10/parse", info.error);
        if (ok) {
            check(info.profile_idc == 4, "sps/synthetic/rext444_10/idc");
            check(info.level_idc == 93, "sps/synthetic/rext444_10/level");
            check(info.chroma_format_idc == 3, "sps/synthetic/rext444_10/chroma");
            check(info.bit_depth_luma == 10, "sps/synthetic/rext444_10/bd");
            check(info.intra == 1, "sps/synthetic/rext444_10/intra");
            check(info.one_picture_only == 1, "sps/synthetic/rext444_10/one_picture");
            check(info.max_12bit == 1, "sps/synthetic/rext444_10/max_12bit");
            check(info.max_10bit == 1, "sps/synthetic/rext444_10/max_10bit");
            check(info.max_8bit == 0, "sps/synthetic/rext444_10/max_8bit");
            check(info.max_422chroma == 0, "sps/synthetic/rext444_10/max_422chroma");
            check(info.max_420chroma == 0, "sps/synthetic/rext444_10/max_420chroma");
            check(info.constraint_bits.size() == 48, "sps/synthetic/rext444_10/constraint48",
                  info.constraint_bits);
            check(info.constraint_bits.compare(0, 4, "1011") == 0,
                  "sps/synthetic/rext444_10/constraint_prefix", info.constraint_bits);
            check(info.sps_max_dec_pic_buffering_minus1 == 2, "sps/synthetic/rext444_10/dpb");
            check(info.sps_max_num_reorder_pics == 0, "sps/synthetic/rext444_10/reorder");
            // still 断言：17..21 的 RExt 信号必须通过。
            check(pp::heif_sps_still_error_zh(HeifProfile::Main444_10Still, info).empty(),
                  "sps/synthetic/rext444_10/still_ok",
                  pp::heif_sps_still_error_zh(HeifProfile::Main444_10Still, info));
            check(pp::heif_sps_still_error_zh(HeifProfile::Main422_12Still, info).empty(),
                  "sps/synthetic/rext444_10/other_rext_still_ok");
        }
    }
    // ② 去掉防竞争字节的开关必须等价（同一 RBSP 插/不插 00 00 03 解析结果一致）。
    {
        SpsSpec spec;
        spec.idc = 4;
        spec.chroma = 2;
        spec.bit_depth = 12;
        const std::vector<std::uint8_t> with_epb =
            make_hvcc(make_sps_nal(spec, /*add_epb=*/true), spec);
        const std::vector<std::uint8_t> plain =
            make_hvcc(make_sps_nal(spec, /*add_epb=*/false), spec);
        pp::HeifSpsPtlInfo a;
        pp::HeifSpsPtlInfo b;
        check(pp::heif_parse_sps_ptl(with_epb, a) && pp::heif_parse_sps_ptl(plain, b),
              "sps/epb/parse_both");
        check(a.constraint_bits == b.constraint_bits && a.intra == b.intra &&
                  a.one_picture_only == b.one_picture_only &&
                  a.sps_max_dec_pic_buffering_minus1 == b.sps_max_dec_pic_buffering_minus1,
              "sps/epb/equivalent", a.constraint_bits + " vs " + b.constraint_bits);
        check(a.intra == 1 && a.one_picture_only == 1, "sps/epb/still_flags");
    }
    // ③ RExt 但 intra/one_picture 缺失 → still 变体断言必须失败并点名具体位。
    {
        SpsSpec spec;
        spec.idc = 4;
        spec.chroma = 3;
        spec.bit_depth = 8;
        spec.intra = 0;
        pp::HeifSpsPtlInfo info;
        check(pp::heif_parse_sps_ptl(make_hvcc(make_sps_nal(spec, true), spec), info),
              "sps/rext_no_intra/parse", info.error);
        const std::string err = pp::heif_sps_still_error_zh(HeifProfile::Main444_Still, info);
        check(!err.empty(), "sps/rext_no_intra/fails");
        check(err.find("intra=0") != std::string::npos, "sps/rext_no_intra/names_intra", err);
        check(err.find("one_picture_only=1") != std::string::npos,
              "sps/rext_no_intra/names_one_picture", err);
        spec.intra = 1;
        spec.one_picture = 0;
        check(pp::heif_parse_sps_ptl(make_hvcc(make_sps_nal(spec, true), spec), info),
              "sps/rext_no_one_picture/parse");
        const std::string err2 = pp::heif_sps_still_error_zh(HeifProfile::Main444_Still, info);
        check(err2.find("one_picture_only=0") != std::string::npos,
              "sps/rext_no_one_picture/names_one_picture", err2);
    }
    // ④ 非 RExt（idc=2 Main10）：still 位**不适用**（-1），DSB 约束是唯一证据。
    {
        SpsSpec spec;
        spec.idc = 2;
        spec.chroma = 1;
        spec.bit_depth = 10;
        spec.dpb_minus1 = 4;
        spec.reorder = 2;
        pp::HeifSpsPtlInfo info;
        check(pp::heif_parse_sps_ptl(make_hvcc(make_sps_nal(spec, true), spec), info),
              "sps/main10/parse", info.error);
        check(info.intra == -1, "sps/main10/intra_not_applicable");
        check(info.one_picture_only == -1, "sps/main10/one_picture_not_applicable");
        check(info.constraint_bits.size() == 48, "sps/main10/constraint48");
        const std::string err = pp::heif_sps_still_error_zh(HeifProfile::Main10Still, info);
        check(!err.empty(), "sps/main10/still_fails_dpb");
        check(err.find("max_dec_pic_buffering_minus1=4") != std::string::npos,
              "sps/main10/names_dpb", err);
        check(err.find("num_reorder_pics=2") != std::string::npos, "sps/main10/names_reorder", err);
        check(err.find("不可用") != std::string::npos, "sps/main10/says_unavailable", err);
        // DPB 0/0 时断言通过 —— 证明这不是「恒失败」的假断言（标准里不存在这样的 x265 配置，
        // 但解析/判定逻辑必须正确）。
        spec.dpb_minus1 = 0;
        spec.reorder = 0;
        check(pp::heif_parse_sps_ptl(make_hvcc(make_sps_nal(spec, true), spec), info),
              "sps/main10_dpb0/parse");
        check(pp::heif_sps_still_error_zh(HeifProfile::Main10Still, info).empty(),
              "sps/main10_dpb0/still_ok",
              pp::heif_sps_still_error_zh(HeifProfile::Main10Still, info));
        // 普通档位（非 still 变体）不参与本断言。
        check(pp::heif_sps_still_error_zh(HeifProfile::Main10, info).empty(),
              "sps/non_still_profile/no_assert");
        check(pp::heif_sps_still_error_zh(HeifProfile::MainStill, info).empty(),
              "sps/mainstill_profile/no_assert");
    }
    // ⑤ idc 与档位不一致 → 显式失败（不把 16/20 混为一谈）。
    {
        SpsSpec spec;
        spec.idc = 3;
        spec.chroma = 1;
        spec.bit_depth = 8;
        pp::HeifSpsPtlInfo info;
        check(pp::heif_parse_sps_ptl(make_hvcc(make_sps_nal(spec, true), spec), info),
              "sps/idc_mismatch/parse");
        const std::string err = pp::heif_sps_still_error_zh(HeifProfile::Main444_10Still, info);
        check(err.find("general_profile_idc") != std::string::npos, "sps/idc_mismatch/named", err);
    }
    // ⑥ 畸形输入：空/截断/无 SPS → false + 中文原因（不崩、不误命中）。
    {
        pp::HeifSpsPtlInfo info;
        check(!pp::heif_parse_sps_ptl(std::vector<std::uint8_t>{}, info), "sps/empty/false");
        check(!info.error.empty(), "sps/empty/error", info.error);
        std::vector<std::uint8_t> truncated(23, 0);
        truncated[0] = 1;
        truncated[22] = 1;
        check(!pp::heif_parse_sps_ptl(truncated, info), "sps/truncated/false");
        check(!info.error.empty(), "sps/truncated/error", info.error);
        // 有 hvcC 但数组里只有 VPS（type 32）→ 必须报「没有 SPS」。
        std::vector<std::uint8_t> no_sps = make_hvcc(make_sps_nal(SpsSpec{}, true), SpsSpec{});
        no_sps[23] = 0x20; // 数组类型改成 32（VPS）
        check(!pp::heif_parse_sps_ptl(no_sps, info), "sps/no_sps/false");
        check(info.error.find("SPS") != std::string::npos, "sps/no_sps/error", info.error);
        check(pp::heif_sps_still_error_zh(HeifProfile::Main444_10Still, info).find("SPS") !=
                  std::string::npos,
              "sps/no_sps/still_error_names_sps");
    }
}

// 真实产物：设计侦察样本（build/design-recon，gitignored）与 Apple T7（super/）。
// 找不到就跳过（CI/干净检出没有这些文件），但**找到时断言逐字锁定实测值**。
void test_sps_ptl_real_files() {
    using pp::HeifProfile;
    const fs::path root = find_repo_root();
    if (!root.empty()) {
        const fs::path rext = root / "build/design-recon/heif/out/p_rext444_10/small320x240.heic";
        const fs::path main10 = root / "build/design-recon/heif/out/p_main10/small320x240.heic";
        std::error_code ec;
        if (fs::exists(rext, ec) && !ec) {
            pp::HeifSpsPtlInfo info;
            const bool ok = pp::heif_parse_sps_ptl(rext, info);
            check(ok, "sps/real/rext444_10/parse", info.error);
            if (ok) {
                check(info.profile_idc == 4, "sps/real/rext444_10/idc");
                check(info.chroma_format_idc == 3, "sps/real/rext444_10/chroma");
                check(info.bit_depth_luma == 10, "sps/real/rext444_10/bd");
                check(info.intra == 1, "sps/real/rext444_10/intra");
                check(info.one_picture_only == 1, "sps/real/rext444_10/one_picture");
                check(info.sps_max_dec_pic_buffering_minus1 == 2, "sps/real/rext444_10/dpb");
                check(pp::heif_sps_still_error_zh(HeifProfile::Main444_10Still, info).empty(),
                      "sps/real/rext444_10/still_ok");
            }
        } else {
            info("skip sps/real/rext444_10: 样本缺失（gitignored / 干净检出）");
        }
        if (fs::exists(main10, ec) && !ec) {
            // 16（Main10Still）的实测证据：idc=2 的注入路径 DPB = 4/2 → still 断言失败。
            pp::HeifSpsPtlInfo info;
            const bool ok = pp::heif_parse_sps_ptl(main10, info);
            check(ok, "sps/real/main10/parse", info.error);
            if (ok) {
                check(info.profile_idc == 2, "sps/real/main10/idc");
                check(info.intra == -1, "sps/real/main10/intra_not_applicable");
                check(info.sps_max_dec_pic_buffering_minus1 == 4, "sps/real/main10/dpb");
                check(info.sps_max_num_reorder_pics == 2, "sps/real/main10/reorder");
                check(!pp::heif_sps_still_error_zh(HeifProfile::Main10Still, info).empty(),
                      "sps/real/main10/still_fails");
            }
        } else {
            info("skip sps/real/main10: 样本缺失（gitignored / 干净检出）");
        }
    }
    const fs::path t7 = find_t7();
    if (t7.empty()) {
        info("skip sps/real/t7: 样本缺失（gitignored / 干净检出）");
        return;
    }
    pp::HeifSpsPtlInfo info;
    const bool ok = pp::heif_parse_sps_ptl(t7, info);
    check(ok, "sps/real/t7/parse", info.error);
    if (!ok)
        return;
    // Apple 硬件编码的 Main10 HEIC：hvcC constraint 有 4 个通用位（0xB0…），
    // 但 48 位里的 RExt 具名位同样不存在 → intra/one_picture 必须是「不适用」。
    check(info.profile_idc == 2, "sps/real/t7/idc");
    check(info.level_idc == 93, "sps/real/t7/level");
    check(info.chroma_format_idc == 1, "sps/real/t7/chroma");
    check(info.bit_depth_luma == 10, "sps/real/t7/bd");
    check(info.intra == -1 && info.one_picture_only == -1, "sps/real/t7/still_bits_absent");
    check(info.sps_max_dec_pic_buffering_minus1 == 4, "sps/real/t7/dpb");
    check(info.sps_max_num_reorder_pics == 0, "sps/real/t7/reorder");
}

void test_hvcc_parser() {
    const fs::path t7 = find_t7();
    if (t7.empty()) {
        info("skip hvcc/T7: super/T7.heic not found from current directory");
    } else {
        pp::HeifHvccInfo hvcc;
        const bool ok = pp::heif_parse_hvcc(t7, hvcc);
        check(ok, "hvcc/t7/parse", hvcc.error);
        if (ok) {
            check(hvcc.found, "hvcc/t7/found");
            check(hvcc.profile_idc == 2, "hvcc/t7/profile_idc", std::to_string(hvcc.profile_idc));
            check(hvcc.level_idc == 93, "hvcc/t7/level_idc", std::to_string(hvcc.level_idc));
            check(hvcc.chroma_format_idc == 1, "hvcc/t7/chroma",
                  std::to_string(hvcc.chroma_format_idc));
            check(hvcc.bit_depth_luma == 10, "hvcc/t7/bit_depth",
                  std::to_string(hvcc.bit_depth_luma));
            check(hvcc.bit_depth_chroma == 10, "hvcc/t7/bit_depth_chroma");
            check(pp::heif_level_parameter_idc(hvcc.level_idc) == 31,
                  "hvcc/t7/level_parameter_idc");
        }
    }

    pp::HeifHvccInfo missing;
    const bool missing_ok = pp::heif_parse_hvcc("build/heif-profile/does-not-exist.heic", missing);
    check(!missing_ok, "hvcc/missing/false");
    check(!missing.error.empty(), "hvcc/missing/error", missing.error);

    // 非 HEIF 文件必须给出明确错误而不是崩溃/误命中；源文件存在时才测（__FILE__ 可能是
    // 相对路径，cwd = 构建目录时未必可解析）。
    std::error_code ec;
    const fs::path self = fs::absolute(fs::path(__FILE__), ec);
    if (fs::exists(self, ec) && !ec) {
        pp::HeifHvccInfo not_heif;
        const bool not_heif_ok = pp::heif_parse_hvcc(self, not_heif);
        check(!not_heif_ok, "hvcc/not_heif/false");
        check(!not_heif.error.empty(), "hvcc/not_heif/error", not_heif.error);
    }
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    test_level_table();
    test_level_idc_conversion();
    test_level_fit();
    test_level_min_and_error();
    test_grid_plan();
    test_grid_modes_and_manual();
    test_still_level_cap();
    test_sps_ptl_parser();
    test_sps_ptl_real_files();
    test_hvcc_parser();
    std::printf("heif_capability: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
