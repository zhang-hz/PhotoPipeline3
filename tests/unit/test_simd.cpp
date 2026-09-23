// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M4-T3 — core/simd 探测层单测（骨架）。
//
// Contract: docs/v0.3.0-design.md §11.2（运行期 cpu_has_avx2() 分派）/
//           docs/m4-tasks.md §3 W0-T3（本任务出口：simd 骨架 + ctest 绿）。
// 框架：延续仓库既有口径 —— 手写断言，无第三方测试框架；失败逐条打印，
//       main() 返回失败条数（0 = 全绿）。
//
// 本任务（W0-T3）断言范围 = **只有探测层**：
//   ① 编译期基线确认（本 TU 与 pp_core 同源，__AVX2__ 必须成立）；
//   ② cpu_has_avx2() 在本机（i7-14700K / x86-64-v3 基线机）必须为 true —— 基线下
//      返回 false 意味着产物与编译口径矛盾，属真实故障，不放宽；
//   ③ 缓存语义：连续调用结果恒定（幂等）。
//
// 未落地（W5-T19，占位说明，本任务**不**提前冻结签名）：
//   5 对 avx2/ref 一致性单测 —— flatten / quantize / transpose / downscale / interleave，
//   判据（§11.2）：flatten 与 interleave 逐位相等、quantize 舍入一致、transpose 逐位、
//   downscale 容差 ≤1e-5。届时本文件扩展为 test_simd 的对照组入口。

#include <cstdio>
#include <string>

#include "core/simd/simd.h"

#if !defined(__AVX2__)
#error "test_simd 必须带 AVX2 基线编译（§11.1）：__AVX2__ 未定义说明 flags 链断了。"
#endif

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

} // namespace

int main() {
    // ① 编译期基线：能编译到这里就说明 __AVX2__ 成立（上面的 #error 是闸门）。
    //    这里再做一次运行期可读的显式记录，便于 CI 日志取证。
    std::printf("test_simd: 编译期基线 __AVX2__ = 已定义（x86-64-v3 / /arch:AVX2）\n");

    // ② 本机支持：基线机器（i7-14700K，Raptor Lake）必须报告 AVX2。
    const bool has_avx2 = pp::simd::cpu_has_avx2();
    check(has_avx2, "cpu_has_avx2/this-machine",
          "本机报告无 AVX2 —— 基线产物无法安全运行（CPUID leaf7 EBX[5] / XCR0 检查）");
    std::printf("test_simd: cpu_has_avx2() = %s\n", has_avx2 ? "true" : "false");

    // ③ 幂等：缓存路径与首次探测必须给同一答案（排除静态量初始化竞态/脏读）。
    for (int i = 0; i < 3; ++i) {
        check(pp::simd::cpu_has_avx2() == has_avx2, "cpu_has_avx2/idempotent",
              "第 " + std::to_string(i + 1) + " 次调用与首次不一致");
    }

    if (g_failed == 0) {
        std::printf("test_simd: OK (探测层；5 对 avx2/ref 一致性单测在 W5-T19 落地)\n");
        return 0;
    }
    std::printf("test_simd: FAILED (%d)\n", g_failed);
    return g_failed;
}
