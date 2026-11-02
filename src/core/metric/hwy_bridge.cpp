// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/metric/hwy_bridge（0.4.2 / M14-C6-①）：Highway 私有化副本的 bridge shim
//
// 背景（nightly asan/tsan 真实债，docs/m12-adjudications.md A13 / docs/m14-tasks.md §8-①）：
//   vendored ssimulacra2（third_party/ssimulacra2，含 gauss_blur）经 compat 层 include 了
//   vcpkg Highway 1.4.0 头。vcpkg 预编译的 jpegli/libjxl 每个 TU 都带
//   `-DHWY_DISABLED_TARGETS=(HWY_AVX3|HWY_AVX3_SPR|HWY_AVX3_ZEN4|…|HWY_SSSE3|…)`
//   （见 vcpkg/buildtrees/libjpeg-turbo/.../build.ninja 的 simd.cc 行；libjxl 同），
//   所以它们的 dispatch 表里 AVX3 槽位 = nullptr；而我方 TU（-march=x86-64-v3 / /arch:AVX2，
//   未定义 HWY_DISABLED_TARGETS）编译出的 `hwy::ChosenTarget::GetIndex()` 内联体窗口包含
//   AVX3。该 inline 是 COMDAT，全链接只保留一份定义，我方对象先出现 ⇒ **我方 GetIndex 版本
//   被选中用于 jpegli 的调用点** ⇒ AVX-512 runner 上算出 AVX3 索引 ⇒ 命中 jpegli 表的
//   nullptr ⇒ `jpegli::VectorSize()` pc=0 SEGV（跨轮非确定 = runner CPU 池混合 + COMDAT 选择）。
//
// 修法（CMakeLists.txt 的 M8-C0 ODR 段）：把这批 vendored TU 的 `hwy` 命名空间改名私有化
// （`hwy=pp_ssim2_hwy`，与 M12-D25 的 `jxl=pp_ssim2_jxl` 同机制、vendored 源码零字节改动）
// ⇒ 我方 TU 的 hwy 符号与 jpegli/libjxl 零重叠。改名后这批 TU 仍引用两个 hwy 的**非内联**
// 符号（动态派发机制的中枢），本文件负责把它们桥回真实 libhwy / hwy.dll：
//   * `pp_ssim2_hwy::GetChosenTarget()`  → 真实 `hwy::GetChosenTarget()` 单例（同一对象、
//     同一份「已选目标」状态；两个类由同一份 1.4.0 头生成、布局一致，故按引用转发）；
//   * `pp_ssim2_hwy::SupportedTargets()` → 真实 `hwy::SupportedTargets()`（同一 CPU 探测）。
// 于是私有化的 gauss_blur 仍走**与修复前完全相同的运行时派发目标**（AVX-512 CPU → AVX3；
// AVX2 CPU → AVX2），SSIMULACRA2 数值逐位不变（M12-D26 的 0.05–0.27 漂移来自
// `HWY_COMPILE_ONLY_STATIC` 把路径钉到 AVX2，本修法不使用该宏）。同时 release 二进制里
// 消失的同名 COMDAT 合并风险一并消除。
//
// 细节与纪律：
//   * 本 TU **故意不 include 任何 hwy 头**：它同时被两套命名空间引用（真实 `hwy` 与改名后的
//     `pp_ssim2_hwy`），只做最小前向声明即可；不引入宏状态，避免二次 ODR 面。
//   * 本 TU 不在 `PP_SSIM2_PRIVATE_TUS` 里（不带 `hwy=pp_ssim2_hwy` 宏）——它就是桥本身。
//   * Windows：vendored TU 的 HWY_TARGETS 为单目标（MSVC 把 AVX3 标 BROKEN）⇒ 走 hwy 的
//     simplified dispatch 分支，本文件的符号不被引用（链接器不拉取该归档成员），零成本。
//   * Linux：动态派发路径引用上述两个符号 ⇒ 由本文件提供定义，真实符号由 libhwy.a 提供
//     （libjpeg-turbo 的 CONFIG 包导出 hwy link interface，既有链接面不变）。
//   * 未使用 `HWY_COMPILE_ONLY_STATIC`（红线：会把 gauss_blur 从动态派发钉到 baseline）。

#include <cstdint>

// ---------------------------------------------------------------------------------------------
// 真实 Highway（libhwy.a / hwy.dll）的最小声明面：只声明本 bridge 需要转发的两个符号。
// 与 hwy/targets.h（1.4.0）的签名一致：`ChosenTarget& GetChosenTarget()` /
// `int64_t SupportedTargets()`。此处刻意不写 HWY_DLLEXPORT（静态库/导入库两种链接形态下
// 直接按名字解析即可；Windows 侧由 hwy.lib 导入库提供）。
// ⚠ 必须用 `struct` 而不是 `class` 做前向声明：hwy 源码里是 `struct ChosenTarget`，MSVC 的
//   修饰名区分 class(V) / struct(U)，写错会得到 `?GetChosenTarget@hwy@@YAAEAV...` 与 hwy.dll
//   实际导出的 `...YAAEAU...` 不匹配 ⇒ LNK2019（Windows 本机构建实证）。
// ---------------------------------------------------------------------------------------------
namespace hwy {
struct ChosenTarget;
ChosenTarget &GetChosenTarget();
std::int64_t SupportedTargets();
} // namespace hwy

// ---------------------------------------------------------------------------------------------
// 私有化命名空间（`hwy=pp_ssim2_hwy` 宏改名的结果）：提供 vendored TU 引用的两个非内联符号。
// `pp_ssim2_hwy::ChosenTarget` 在此处只需前向声明——函数只透传引用，不触碰成员布局。
// 布局前提 = 同一份 vcpkg Highway 1.4.0 头文件（仓库没有 vendored hwy 副本；本仓 include
// 的 <hwy/*> 全部来自 vcpkg_installed），故 `reinterpret_cast` 两侧是同一类定义的
// ABI 兼容引用（全仓不变量；若将来 vendored hwy 副本出现，必须改回桥接函数而不能直接转型）。
// ---------------------------------------------------------------------------------------------
namespace pp_ssim2_hwy {

struct ChosenTarget;

// 转发到真实单例：保证「已选目标」状态与 jpegli/libjxl 看到的是同一份（与修复前逐位一致）。
ChosenTarget &GetChosenTarget() {
    return *reinterpret_cast<ChosenTarget *>(&hwy::GetChosenTarget());
}

// 转发到真实 CPU 探测：SupportedTargets 的返回值/语义与修复前完全相同。
std::int64_t SupportedTargets() { return hwy::SupportedTargets(); }

} // namespace pp_ssim2_hwy
