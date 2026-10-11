// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/simd 探测层实现（M4-T3；CPUID，无任何第三方依赖）
//
// 契约：docs/v0.3.0-design.md §11.2「运行期 cpu_has_avx2()（CPUID）选择实现
//       （仅此一层运行时分派——基线本就 AVX2，ref 路径服务测试与调试）」。

// —— 编译期闸门（§11.1）——
// 本 TU 必须带 AVX2 基线编译：MSVC 的 /arch:AVX2 与 GCC/Clang 的 -march=x86-64-v3
// 都定义 __AVX2__。缺了它说明 flags 注入链断了（cmake/avx2.cmake 未生效），
// 此时宁可构建失败，也不要产出一个"看起来成功、实则退回 SSE2"的产物。
#if !defined(__AVX2__)
#error                                                                                             \
    "PP 0.3.0 基线要求 AVX2（§11.1）：本 TU 未取得 __AVX2__ —— 查 cmake/avx2.cmake 是否挂在 pp_core 上。"
#endif

#include "core/simd/simd.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

// 平台分派（仅"调用哪个 CPUID 入口"这一层；ISA 分派纪律见头文件注记）。
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace pp::simd {
namespace {

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#define PP_SIMD_HOST_X86 1
#endif

#ifdef PP_SIMD_HOST_X86

// CPUID（leaf/subleaf）→ 4 个 32 位寄存器，顺序 eax/ebx/ecx/edx。
void cpuid(int leaf, int subleaf, int out[4]) {
#if defined(_MSC_VER)
    ::__cpuidex(out, leaf, subleaf);
#else
    unsigned int a = 0, b = 0, c = 0, d = 0;
    ::__get_cpuid_count(static_cast<unsigned int>(leaf), static_cast<unsigned int>(subleaf), &a, &b,
                        &c, &d);
    out[0] = static_cast<int>(a);
    out[1] = static_cast<int>(b);
    out[2] = static_cast<int>(c);
    out[3] = static_cast<int>(d);
#endif
}

// XCR0：低 32 位在 eax、高 32 位在 edx（XGETBV 用 ecx 选寄存器号）。
// GCC/Clang 的 <immintrin.h> 只在 -mxsave 下提供 _xgetbv()，故此处用内联汇编
// （编译期闸门已保证本 TU 带 AVX2 基线，xgetbv 指令在 x86-64 上恒可用）。
unsigned long long read_xcr0() {
#if defined(_MSC_VER)
    return ::_xgetbv(0);
#else
    unsigned int eax = 0, edx = 0;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<unsigned long long>(edx) << 32) | static_cast<unsigned long long>(eax);
#endif
}

#endif // PP_SIMD_HOST_X86

bool detect_avx2() {
#ifdef PP_SIMD_HOST_X86
    int r[4] = {0, 0, 0, 0};

    // leaf 0：确认存在 leaf 7（structured extended features）——没有它就没有 AVX2 位可查。
    cpuid(0, 0, r);
    if (r[0] < 7) {
        return false;
    }

    // leaf 1：OSXSAVE(ECX[27]) + AVX(ECX[28]) —— CPU 有 AVX 且 OS 开了 XSAVE。
    cpuid(1, 0, r);
    const bool osxsave = (r[2] & (1 << 27)) != 0;
    const bool avx = (r[2] & (1 << 28)) != 0;
    if (!osxsave || !avx) {
        return false;
    }

    // XCR0[2:1] 必须同时置位（XMM + YMM 状态由 OS 保存/恢复），否则 AVX2 指令
    // 会把上半个 YMM 寄存器写丢 —— 这是"CPU 支持但不可用"的经典情形。
    const unsigned long long xcr0 = read_xcr0();
    if ((xcr0 & 0x6ULL) != 0x6ULL) {
        return false;
    }

    // leaf 7 subleaf 0：EBX[5] = AVX2。
    cpuid(7, 0, r);
    return (r[1] & (1 << 5)) != 0;
#else
    // 非 x86：本仓库无此形态；返回 false 走显式失败点（见头文件注记 ②）。
    return false;
#endif
}

} // namespace

bool cpu_has_avx2() {
    // 线程安全的一次性初始化（C++11 起保证）；缓存避免热路径重复 CPUID。
    static const bool cached = detect_avx2();
    return cached;
}

// —— R-303：标量回退测试开关（契约见 simd.h 的脚手架块）——
namespace {
std::atomic<int> &force_scalar_state() {
    // 三态：0 = 未初始化（首查时采样 PP_SIMD_FORCE_SCALAR 环境变量）、1 = 关、2 = 开。
    static std::atomic<int> state{0};
    return state;
}

bool env_force_scalar() {
#if defined(_MSC_VER)
    // MSVC 的 getenv 带 C4996 弃用告警（/W4 面），走 _dupenv_s 保持新代码 0 告警。
    char *buf = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&buf, &len, "PP_SIMD_FORCE_SCALAR") != 0 || buf == nullptr)
        return false;
    const bool on = !(std::strcmp(buf, "0") == 0 || std::strcmp(buf, "") == 0 ||
                      std::strcmp(buf, "false") == 0);
    std::free(buf);
    return on;
#else
    const char *v = std::getenv("PP_SIMD_FORCE_SCALAR");
    if (v == nullptr)
        return false;
    return !(std::strcmp(v, "0") == 0 || std::strcmp(v, "") == 0 || std::strcmp(v, "false") == 0);
#endif
}
} // namespace

void set_force_scalar(bool on) {
    force_scalar_state().store(on ? 2 : 1, std::memory_order_relaxed);
}

bool force_scalar_enabled() {
    std::atomic<int> &st = force_scalar_state();
    int cur = st.load(std::memory_order_relaxed);
    if (cur == 0) {
        // 首次查询：环境变量采样一次；并发首查时允许两个线程都算一遍（同值，CAS 收口）。
        const int want = env_force_scalar() ? 2 : 1;
        int expected = 0;
        if (!st.compare_exchange_strong(expected, want, std::memory_order_relaxed))
            cur = expected;
        else
            cur = want;
    }
    return cur == 2;
}

bool use_avx2() { return cpu_has_avx2() && !force_scalar_enabled(); }

} // namespace pp::simd
