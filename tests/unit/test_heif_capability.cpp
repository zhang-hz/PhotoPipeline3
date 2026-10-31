// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: HEIF 编码能力纯核（Level 表 / Level 拟合 / grid 规划 / hvcC）
//
// 边界取自任务实测：
//   * 1024×576 = 589,824 ≤ Level 3.1 (983,040)；
//   * 3902×5854 = 22,842,308：Level 3.1/4.0/4.1/5.0/5.1/5.2 装不下，6.0/6.1/6.2 装得下；
//   * super/T7.heic（Apple 硬件编码）hvcC: idc=2、general_level_idc=93、chroma=1、10bit。
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

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
    check(pp::heif_level_fits(31, 1024, 576), "fit/31/1024x576");
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

    // Apple 参考：1024×576，cols=ceil(3902/1024)=4，rows=ceil(5854/576)=11。
    const pp::HeifGridPlan always = pp::heif_plan_grid(1, 31, w, h, 1);
    check_plan(always, true, 1024, 576, 4, 11, "always/31");
    check(always.tile_luma_samples == 589824, "grid/always/tile_luma_exact");
    check(always.level_fits_image == false, "grid/always/level_fits_image");
    check_plan(pp::heif_plan_grid(2, 31, w, h, 1), true, 1024, 576, 4, 11, "auto/31");
    check_plan(pp::heif_plan_grid(1, 62, w, h, 1), true, 1024, 576, 4, 11, "always/62");

    // 小图 clamp：tile 不得大于图像（8 对齐后 801x601 → 800x576，故 2x2）。
    check_plan(pp::heif_plan_grid(1, 0, 800, 400, 1), true, 800, 400, 1, 1, "small/420");
    check_plan(pp::heif_plan_grid(1, 0, 801, 601, 3), true, 800, 576, 2, 2, "small/444_odd");

    // 小 Level：tile 面积必须 ≤ MaxLumaPs，且色度对齐保持偶数。
    const pp::HeifGridPlan level30 = pp::heif_plan_grid(2, 30, w, h, 1);
    check(level30.use_grid, "grid/level30/use");
    check(level30.tile_luma_samples <= pp::heif_level_max_luma_ps(30), "grid/level30/area",
          std::to_string(level30.tile_luma_samples));
    check(level30.tile_width % 2 == 0 && level30.tile_height % 2 == 0,
          "grid/level30/alignment_420");
    check(level30.cols == (w + level30.tile_width - 1) / level30.tile_width,
          "grid/level30/cols_formula");
    check(level30.rows == (h + level30.tile_height - 1) / level30.tile_height,
          "grid/level30/rows_formula");

    const pp::HeifGridPlan level10 = pp::heif_plan_grid(2, 10, w, h, 1);
    check(level10.use_grid, "grid/level10/use");
    check(level10.tile_luma_samples <= pp::heif_level_max_luma_ps(10), "grid/level10/area",
          std::to_string(level10.tile_luma_samples));

    const pp::HeifGridPlan c422 = pp::heif_plan_grid(2, 31, w, h, 2);
    check(c422.use_grid && c422.tile_width % 2 == 0, "grid/422/width_even");
    const pp::HeifGridPlan c420 = pp::heif_plan_grid(2, 31, w, h, 1);
    check(c420.tile_width % 2 == 0 && c420.tile_height % 2 == 0, "grid/420/both_even");

    const pp::HeifGridPlan invalid = pp::heif_plan_grid(1, 31, 0, 100, 1);
    check(!invalid.use_grid && invalid.tile_width == 0, "grid/invalid_dims");
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
    test_hvcc_parser();
    std::printf("heif_capability: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
