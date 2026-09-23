// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/simd：AVX2 热路径的公共面（M4-T3 地基；5 对实现见 W5-T19）
//
// 依据：docs/v0.3.0-design.md §11.2（自研热路径 SIMD 化 + 运行期分派纪律）、
//       docs/v0.3.0-design.md §2 变更地图 `core/simd`（新，加性模块）。
//
// —— 编译期基线（§11.1）——
//   整仓基线 ISA = x86-64-v3 / /arch:AVX2，由 cmake/avx2.cmake（自研 targets）与
//   triplets/x64-windows-avx2.cmake、triplets/x64-linux-avx2.cmake（vcpkg 依赖）成对保证。
//   因此本模块**不是**「为兼容老机器而分派」——基线产物本身即 AVX2。
//
// —— 运行期分派注记（§11.2；本模块是整仓唯一一层运行期 ISA 分派）——
//   分派存在只为三件事：
//     ① `*_ref` 标量实现服务单测与调试，作为 `*_avx2` 实现的正确性基准
//        （flatten/interleave 逐位相等、quantize 舍入一致、transpose 逐位、downscale ≤1e-5）；
//     ② 缺失 AVX2 的宿主上给出**明确失败点**，而不是执行非法指令（SIGILL / 0xC000001D）；
//     ③ 后续可选加速（如 AVX-512 专项）的扩展点收在本文件，不外溢到调用方。
//   纪律：调用方（`pipeline` / `codecs` / `thumbs`）**不得**自己写 CPUID 或 ISA 宏分支；
//   需要 AVX2 路径时一律问本模块。
//
// —— 本任务（W0-T3）交付范围 ——
//   只交付探测层：`cpu_has_avx2()`。5 对热路径
//   （flatten / quantize / transpose / downscale / interleave）
//   及其同签名 `*_avx2` / `*_ref` 实现与一致性单测在 **W5-T19** 落地；
//   落地前本头文件**不声明**任何热路径符号（避免先行冻结签名）。
#pragma once

namespace pp::simd {

// 本进程所在 CPU 是否真的支持 AVX2（CPUID 检测，含 OS 的 XSAVE/YMM 状态确认）。
//
// 判据（x86-64）：
//   leaf 1 ECX[27] OSXSAVE && ECX[28] AVX && XCR0[2:1]==11（XMM+YMM 由 OS 保存）
//   leaf 7 subleaf 0 EBX[5] AVX2
// 非 x86 架构恒返回 false（本仓库当前无该情形，留作显式失败点而非静默 UB）。
//
// 结果缓存于函数内静态量（线程安全初始化，实测成本 = 首次一次 CPUID）。
// 调用约定：热路径可在初始化期问一次并缓存布尔，不必逐帧调用。
bool cpu_has_avx2();

} // namespace pp::simd
