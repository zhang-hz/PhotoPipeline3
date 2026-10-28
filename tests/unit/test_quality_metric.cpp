// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: 质量评分指标 (M8-C1, SSIMULACRA 2)。
//
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
// 覆盖（C1 任务卡）：
//   * 恒等：16×16 / 128×128 / 129×127 自比 == 100.0（逐样本相同）；
//   * 单调性：±1 量化 < 100 且高于强模糊；
//   * 8-bit / 16-bit / 灰度输入；负分合法（反转图 < 100，不 clamp）；
//   * 边界：7×7 → Unavailable、尺寸不等 → Unavailable、8×8 可评；
//   * 确定性：同输入两次分数逐位相同；
//   * 回调异常 / 空回调 / 非有限像素 → Unavailable 且不崩溃。
//
// 两个 env 门控的“证据模式”（不参与 ctest 断言，供 build/m8/c1/ 取证）：
//   PP_METRIC_PERF=1            → 1MP/4MP/12MP 的 ms/MP 与进程峰值工作集；
//   PP_METRIC_ORACLE_REF/DIST   → 读取两份 float32 线性 RGB 行优先裸文件，
//   PP_METRIC_ORACLE_W/H         打印 ORACLE_SCORE <score>（与官方 CLI 对照用）。
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
// clang-format off
// 顺序敏感：psapi.h 依赖先包含的 windows.h（clang-format 会按字典序打乱）。
#include <windows.h>
#include <psapi.h>
// clang-format on
#pragma comment(lib, "psapi.lib")
#else
#include <fstream>
#endif

#include "core/metric/metric.h"

namespace {

int g_fail = 0;

void fail(const std::string &c, const std::string &d) {
    std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
    ++g_fail;
}

void check(bool ok, const std::string &c, const std::string &d) {
    if (!ok)
        fail(c, d);
}

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.12g", v);
    return buf;
}

std::string num_i(long long v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld", v);
    return buf;
}

// ---------------------------------------------------------------- 像素工具
//
// 所有输入都是 sRGB 线性 float RGB（与 C2 将喂给 compute() 的口径一致）。
struct Image {
    int w = 0;
    int h = 0;
    std::vector<float> px; // interleaved RGB
};

uint32_t NextRand(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

float RandUnit(uint32_t *state) {
    // [0, 1) 的 24-bit 分度值，避免 1.0 边界。
    return static_cast<float>(NextRand(state) >> 8) / 16777216.0f;
}

struct ImageSize {
    int w = 0;
    int h = 0;
};

// 生成参数（struct 化同时满足 clang-tidy bugprone-easily-swappable-parameters：
// 相邻参数不再同型/可互转）。
struct ImageSpec {
    uint32_t seed = 0;
    int levels = 0; // >1 时量化到 levels 级整数网格
    bool gray = false;
};

// 生成确定性伪随机图；gray=true 时三通道相同；levels>1 时量化到 levels 级整数网格。
Image MakeImage(ImageSize size, const ImageSpec &spec) {
    Image img;
    img.w = size.w;
    img.h = size.h;
    img.px.resize(static_cast<size_t>(size.w) * static_cast<size_t>(size.h) * 3);
    uint32_t state = spec.seed;
    for (size_t i = 0; i < img.px.size(); i += 3) {
        float v = RandUnit(&state);
        if (spec.levels > 1) {
            const float q = static_cast<float>(spec.levels - 1);
            v = std::round(v * q) / q;
        }
        img.px[i + 0] = v;
        img.px[i + 1] = spec.gray ? v : RandUnit(&state);
        img.px[i + 2] = spec.gray ? v : RandUnit(&state);
    }
    return img;
}

struct QuantizeSpec {
    int levels = 256;
    uint32_t seed = 0;
};

// 逐像素量化误差：每个样本按 1/levels 的整数网格偏移 ±1 级（截断到 [0,1]）。
Image QuantizePlusMinusOne(const Image &src, const QuantizeSpec &spec) {
    Image out = src;
    uint32_t state = spec.seed;
    const float step = 1.0f / static_cast<float>(spec.levels - 1);
    for (size_t i = 0; i < out.px.size(); ++i) {
        const int sign = (NextRand(&state) & 1u) ? 1 : -1;
        float v = out.px[i] + static_cast<float>(sign) * step;
        if (v < 0.0f)
            v = 0.0f;
        if (v > 1.0f)
            v = 1.0f;
        out.px[i] = v;
    }
    return out;
}

// 强模糊（可直接调用 4 次 3×3 盒式模糊；替代“强模糊”对照，不依赖外部库）。
Image StrongBlur(const Image &src, int radius) {
    Image out = src;
    for (int pass = 0; pass < 2; ++pass) {
        Image tmp = out;
        for (int y = 0; y < out.h; ++y) {
            for (int x = 0; x < out.w; ++x) {
                float sum[3] = {0.0f, 0.0f, 0.0f};
                int count = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    const int yy = y + dy;
                    if (yy < 0 || yy >= out.h)
                        continue;
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int xx = x + dx;
                        if (xx < 0 || xx >= out.w)
                            continue;
                        const size_t i = (static_cast<size_t>(yy) * static_cast<size_t>(out.w) +
                                          static_cast<size_t>(xx)) *
                                         3;
                        for (int c = 0; c < 3; ++c)
                            sum[c] += out.px[i + c];
                        ++count;
                    }
                }
                const size_t i =
                    (static_cast<size_t>(y) * static_cast<size_t>(out.w) + static_cast<size_t>(x)) *
                    3;
                for (int c = 0; c < 3; ++c) {
                    tmp.px[i + c] = sum[c] / static_cast<float>(count);
                }
            }
        }
        out = tmp;
    }
    return out;
}

pp::metric::FrameSource SourceOf(const Image &img) {
    pp::metric::FrameSource src;
    src.width = img.w;
    src.height = img.h;
    src.row = [&img](int y, float *rgb) {
        const size_t offset = static_cast<size_t>(y) * static_cast<size_t>(img.w) * 3;
        std::memcpy(rgb, img.px.data() + offset, static_cast<size_t>(img.w) * 3 * sizeof(float));
    };
    return src;
}

bool Score(const Image &ref, const Image &dist, pp::metric::Result *out) {
    *out = pp::metric::compute(SourceOf(ref), SourceOf(dist));
    return out->state == pp::metric::State::Ok && std::isfinite(out->score);
}

// ---------------------------------------------------------------- 进程峰值内存
#if defined(_WIN32)
double PeakWorkingSetMB() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters))) {
        return -1.0;
    }
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
}
#else
double PeakWorkingSetMB() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            long long kb = 0;
            std::sscanf(line.c_str() + 6, "%lld", &kb);
            return static_cast<double>(kb) / 1024.0;
        }
    }
    return -1.0;
}
#endif

// ---------------------------------------------------------------- 证据模式
//
// 流式行源（perf 用）：不物化整幅输入，使峰值 RSS 归因于 compute() 自身。
// 参考/失真使用同一 seed 与同一基值生成，仅失真侧每样本 ±1/255（8-bit LSB）。
struct SyntheticSpec {
    uint32_t seed = 0;
    bool jitter = false;
};

pp::metric::FrameSource SyntheticSource(ImageSize size, const SyntheticSpec &spec) {
    pp::metric::FrameSource src;
    src.width = size.w;
    src.height = size.h;
    src.row = [size, spec](int y, float *rgb) {
        uint32_t state = spec.seed ^ (static_cast<uint32_t>(y) * 2654435761u);
        for (int x = 0; x < size.w; ++x) {
            const float spatial = static_cast<float>(x) / static_cast<float>(size.w);
            for (int c = 0; c < 3; ++c) {
                const uint32_t rnd = NextRand(&state);
                float v = 0.5f * static_cast<float>(rnd >> 8) / 16777216.0f + 0.5f * spatial;
                v = std::round(v * 255.0f) / 255.0f;
                if (spec.jitter) {
                    v += ((rnd & 1u) ? 1.0f : -1.0f) / 255.0f;
                }
                if (v < 0.0f)
                    v = 0.0f;
                if (v > 1.0f)
                    v = 1.0f;
                rgb[3 * x + c] = v;
            }
        }
    };
    return src;
}

// perf：1MP / 4MP / 12MP 的 ms/MP 与进程峰值工作集（行式输出便于写 perf.log）。
int RunPerfMode() {
    struct Case {
        int w;
        int h;
        const char *name;
    };
    const Case cases[] = {{1024, 1024, "1MP"}, {2048, 2048, "4MP"}, {4096, 3072, "12MP"}};
    const double base_peak = PeakWorkingSetMB();
    std::printf("PERF_BASE peak_rss_mb=%.1f\n", base_peak);
    for (const Case &c : cases) {
        const uint32_t seed = 0xC1C1u + static_cast<uint32_t>(c.w);
        const pp::metric::FrameSource ref = SyntheticSource({c.w, c.h}, {seed, false});
        const pp::metric::FrameSource dist = SyntheticSource({c.w, c.h}, {seed, true});
        const auto t0 = std::chrono::steady_clock::now();
        pp::metric::Result r = pp::metric::compute(ref, dist);
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double mp = static_cast<double>(c.w) * c.h / 1.0e6;
        std::printf("PERF %s %dx%d px=%lld ms=%.1f ms_per_mp=%.1f ms_internal=%.1f "
                    "score=%.6f state=%s peak_rss_mb=%.1f\n",
                    c.name, c.w, c.h, static_cast<long long>(c.w) * static_cast<long long>(c.h), ms,
                    ms / mp, r.ms, r.score, pp::metric::state_name(r.state), PeakWorkingSetMB());
        if (r.state != pp::metric::State::Ok) {
            fail("perf", std::string("case ") + c.name +
                             " state=" + pp::metric::state_name(r.state) + " note=" + r.note);
        }
    }
    return g_fail;
}

// oracle：读取两份 `float32 线性 RGB、行优先、交织` 裸文件并打印分数。
int RunOracleMode(const char *ref_path, const char *dist_path, int w, int h) {
    auto read_raw = [&](const char *path, std::vector<float> *out) -> bool {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        std::FILE *f = std::fopen(path, "rb");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (f == nullptr)
            return false;
        out->resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
        const size_t got = std::fread(out->data(), sizeof(float), out->size(), f);
        std::fclose(f);
        return got == out->size();
    };
    Image ref;
    ref.w = w;
    ref.h = h;
    Image dist;
    dist.w = w;
    dist.h = h;
    if (!read_raw(ref_path, &ref.px) || !read_raw(dist_path, &dist.px)) {
        std::printf("ORACLE_ERROR 无法读取裸文件\n");
        return 1;
    }
    pp::metric::Result r = pp::metric::compute(SourceOf(ref), SourceOf(dist));
    if (r.state != pp::metric::State::Ok) {
        std::printf("ORACLE_ERROR state=%s note=%s\n", pp::metric::state_name(r.state),
                    r.note.c_str());
        return 1;
    }
    std::printf("ORACLE_SCORE %.8f ms=%.1f\n", r.score, r.ms);
    return 0;
}

// ---------------------------------------------------------------- 功能断言
void TestIdentity() {
    const int dims[][2] = {{16, 16}, {128, 128}, {129, 127}, {8, 8}};
    for (const auto &d : dims) {
        const std::string c = "identity/" + num_i(d[0]) + "x" + num_i(d[1]);
        const Image img = MakeImage({d[0], d[1]}, {0x1234u + static_cast<uint32_t>(d[0])});
        pp::metric::Result r;
        check(Score(img, img, &r), c, "应返回 Ok");
        check(r.score == 100.0, c, "自比应恰为 100.0，得到 " + num(r.score));
        check(r.ms >= 0.0, c, "ms 应 >= 0");
    }
}

void TestMonotonicity() {
    const int w = 96;
    const int h = 80;
    const Image base = MakeImage({w, h}, {0x5EEDu, 256, false});
    const Image quant = QuantizePlusMinusOne(base, {256, 0xBEEFu});
    const Image blur = StrongBlur(base, 4);

    pp::metric::Result quant_result;
    pp::metric::Result blur_result;
    check(Score(base, quant, &quant_result), "monotonic/quant", "应返回 Ok");
    check(Score(base, blur, &blur_result), "monotonic/blur", "应返回 Ok");

    check(quant_result.score < 100.0, "monotonic/quant",
          "±1 量化应 < 100，得到 " + num(quant_result.score));
    check(blur_result.score < 100.0, "monotonic/blur",
          "强模糊应 < 100，得到 " + num(blur_result.score));
    check(quant_result.score > blur_result.score, "monotonic/order",
          "轻微量化分数应高于强模糊：quant=" + num(quant_result.score) +
              " blur=" + num(blur_result.score));
}

void TestGrayAnd16Bit() {
    // 灰度输入（三通道复制）= 契约中“灰度复制为 RGB”的口径。
    const int w = 64;
    const int h = 48;
    const Image gray = MakeImage({w, h}, {0x6A1u, 256, true});
    const Image gray_bad = QuantizePlusMinusOne(gray, {256, 0x91u});
    pp::metric::Result r;
    check(Score(gray, gray, &r), "gray/identity", "应返回 Ok");
    check(r.score == 100.0, "gray/identity", "灰度自比应 100.0，得到 " + num(r.score));
    check(Score(gray, gray_bad, &r), "gray/quant", "应返回 Ok");
    check(r.score < 100.0, "gray/quant", "灰度 ±1 应 < 100，得到 " + num(r.score));

    // 16-bit 网格：以 65535 级量化后 ±1（更大网格 ⇒ 更接近无损 ⇒ 分数更高）。
    const Image deep = MakeImage({w, h}, {0x16B17u, 65536, false});
    const Image deep_bad = QuantizePlusMinusOne(deep, {65536, 0xC0DEu});
    const Image base8 = MakeImage({w, h}, {0x16B17u, 256, false});
    const Image base8_bad = QuantizePlusMinusOne(base8, {256, 7u});
    pp::metric::Result deep_result;
    pp::metric::Result quant8_result;
    check(Score(deep, deep, &deep_result), "16bit/identity", "应返回 Ok");
    check(deep_result.score == 100.0, "16bit/identity",
          "16-bit 自比应 100.0，得到 " + num(deep_result.score));
    check(Score(deep, deep_bad, &deep_result), "16bit/quant", "应返回 Ok");
    check(Score(base8, base8_bad, &quant8_result), "16bit/compare8", "应返回 Ok");
    check(deep_result.score > quant8_result.score, "16bit/order",
          "16-bit ±1 应高于 8-bit ±1：16bit=" + num(deep_result.score) +
              " 8bit=" + num(quant8_result.score));
}

void TestNegativeAllowed() {
    // 反转图（1 - v）是强失真，分数应显著 < 100（可以为负；这里只断言 < 50）。
    const Image base = MakeImage({64, 64}, {0xFFFFu, 256, false});
    Image inverted = base;
    for (float &v : inverted.px)
        v = 1.0f - v;
    pp::metric::Result r;
    check(Score(base, inverted, &r), "negative/state", "应返回 Ok");
    check(r.score < 50.0, "negative/score", "反转图应显著低分（<50），得到 " + num(r.score));
    check(r.score <= 100.0, "negative/upper", "分数上界 100，得到 " + num(r.score));
}

void TestBounds() {
    const Image small = MakeImage({7, 7}, {1u});
    pp::metric::Result r = pp::metric::compute(SourceOf(small), SourceOf(small));
    check(r.state == pp::metric::State::Unavailable, "bounds/7x7",
          "7×7 应为 Unavailable，得到 " + std::string(pp::metric::state_name(r.state)));
    check(r.score == -1.0, "bounds/7x7", "Unavailable 时 score 应为占位 -1.0");
    check(!r.note.empty(), "bounds/7x7", "Unavailable 应给中文原因");

    const Image a = MakeImage({16, 16}, {2u});
    const Image b = MakeImage({16, 15}, {3u});
    r = pp::metric::compute(SourceOf(a), SourceOf(b));
    check(r.state == pp::metric::State::Unavailable, "bounds/mismatch",
          "尺寸不等应为 Unavailable，得到 " + std::string(pp::metric::state_name(r.state)));

    const Image zero = MakeImage({0, 16}, {4u});
    r = pp::metric::compute(SourceOf(zero), SourceOf(zero));
    check(r.state == pp::metric::State::Unavailable, "bounds/zero", "0 尺寸应为 Unavailable");
}

void TestExceptions() {
    const Image img = MakeImage({32, 32}, {9u});
    pp::metric::FrameSource ref = SourceOf(img);
    pp::metric::FrameSource dist = SourceOf(img);

    // 回调抛异常 → Unavailable，不崩溃。
    pp::metric::RowFn throw_row = [](int y, float *) {
        if (y == 2)
            throw std::runtime_error("boom");
    };
    dist.row = throw_row;
    pp::metric::Result r = pp::metric::compute(ref, dist);
    check(r.state == pp::metric::State::Unavailable, "exceptions/throw",
          "行回调抛异常应为 Unavailable，得到 " + std::string(pp::metric::state_name(r.state)));

    // 空回调 → Unavailable。
    pp::metric::FrameSource empty;
    empty.width = 32;
    empty.height = 32;
    r = pp::metric::compute(empty, ref);
    check(r.state == pp::metric::State::Unavailable, "exceptions/empty", "空回调应为 Unavailable");

    // 非有限像素 → Unavailable（不能把 NaN 传进评分库）。
    dist = SourceOf(img);
    dist.row = [&img](int y, float *rgb) {
        SourceOf(img).row(y, rgb);
        if (y == 5)
            rgb[0] = std::nanf("");
    };
    r = pp::metric::compute(ref, dist);
    check(r.state == pp::metric::State::Unavailable, "exceptions/nan",
          "NaN 像素应为 Unavailable，得到 " + std::string(pp::metric::state_name(r.state)));

    // 回调被调用次数 == height。
    int calls = 0;
    dist = SourceOf(img);
    dist.row = [&img, &calls](int y, float *rgb) {
        ++calls;
        SourceOf(img).row(y, rgb);
    };
    r = pp::metric::compute(ref, dist);
    check(r.state == pp::metric::State::Ok, "exceptions/count", "应返回 Ok");
    check(calls == img.h, "exceptions/count", "行回调应被调用 height 次，实际 " + num_i(calls));
}

void TestDeterminism() {
    const Image ref = MakeImage({73, 61}, {0xD00Du});
    const Image dist = QuantizePlusMinusOne(ref, {256, 0xD00Eu});
    pp::metric::Result first;
    pp::metric::Result second;
    check(Score(ref, dist, &first), "determinism/a", "应返回 Ok");
    check(Score(ref, dist, &second), "determinism/b", "应返回 Ok");
    check(first.score == second.score, "determinism/score",
          "同输入两次分数应逐位相同：" + num(first.score) + " vs " + num(second.score));
}

void TestNames() {
    check(std::strcmp(pp::metric::metric_name(), "ssimulacra2") == 0, "names/metric_name",
          std::string("metric_name()=") + pp::metric::metric_name());
    check(pp::metric::metric_version() == 1, "names/metric_version", "metric_version() 应为 1");
    check(std::strcmp(pp::metric::state_name(pp::metric::State::Off), "off") == 0, "names/off",
          "Off → off");
    check(std::strcmp(pp::metric::state_name(pp::metric::State::Ok), "ok") == 0, "names/ok",
          "Ok → ok");
    check(std::strcmp(pp::metric::state_name(pp::metric::State::Unavailable), "unavailable") == 0,
          "names/unavailable", "Unavailable → unavailable");
    check(std::strcmp(pp::metric::state_name(pp::metric::State::Failed), "failed") == 0,
          "names/failed", "Failed → failed");
}

// C2 在 worker 线程内调用 compute：并发调用必须各自 Ok、互不污染、分数一致。
void TestConcurrency() {
    const Image ref = MakeImage({61, 47}, {0x0C0C0Cu});
    const Image dist = QuantizePlusMinusOne(ref, {256, 0x0C0D0Du});
    constexpr int kThreads = 4;
    std::vector<pp::metric::Result> results(kThreads);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&ref, &dist, &results, t]() {
            results[static_cast<size_t>(t)] = pp::metric::compute(SourceOf(ref), SourceOf(dist));
        });
    }
    for (std::thread &thread : threads)
        thread.join();
    for (int t = 0; t < kThreads; ++t) {
        const pp::metric::Result &r = results[static_cast<size_t>(t)];
        check(r.state == pp::metric::State::Ok, "concurrency/state",
              "线程 " + num_i(t) + " state=" + pp::metric::state_name(r.state));
        check(r.score == results[0].score, "concurrency/score",
              "线程 " + num_i(t) + " 分数应与 0 号线程一致");
    }
}

// 证据模式读环境变量（MSVC 的 getenv 弃用告警在此局部隔离）。
const char *GetEnv(const char *name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

} // namespace

// 测试入口允许异常终止（非产品路径；先例 test_param_catalog.cpp）。
int main() { // NOLINT(bugprone-exception-escape)
    if (const char *perf = GetEnv("PP_METRIC_PERF"); perf != nullptr && perf[0] != '\0') {
        return RunPerfMode();
    }
    const char *oracle_ref = GetEnv("PP_METRIC_ORACLE_REF");
    const char *oracle_dist = GetEnv("PP_METRIC_ORACLE_DIST");
    const char *oracle_w = GetEnv("PP_METRIC_ORACLE_W");
    const char *oracle_h = GetEnv("PP_METRIC_ORACLE_H");
    if (oracle_ref != nullptr && oracle_dist != nullptr && oracle_w != nullptr &&
        oracle_h != nullptr) {
        return RunOracleMode(oracle_ref, oracle_dist, std::atoi(oracle_w), std::atoi(oracle_h));
    }

    TestIdentity();
    TestMonotonicity();
    TestGrayAnd16Bit();
    TestNegativeAllowed();
    TestBounds();
    TestExceptions();
    TestDeterminism();
    TestConcurrency();
    TestNames();

    if (g_fail == 0) {
        std::printf("test_quality_metric: OK\n");
    } else {
        std::printf("test_quality_metric: %d failure(s)\n", g_fail);
    }
    return g_fail;
}
