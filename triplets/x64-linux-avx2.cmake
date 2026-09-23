# M4-T3 / 2026-09-24 / AVX2 依赖基线 —— Linux / GCC（Clang 同口径）
# 依据：docs/v0.3.0-design.md §11.1（flags 表）＋ docs/m4-tasks.md §3 W0-T3
#        主对话裁定：依赖侧 ISA/LTO flags 全部撤除（与 Windows 侧统一，不搞平台分叉）
#
# —— 本 triplet 的职责（收敛后的最终口径）——
#   **只承载命名身份**：继承 vcpkg 原生 triplet x64-linux（静态库，一字不改：
#   VCPKG_LIBRARY_LINKAGE=static、VCPKG_CMAKE_SYSTEM_NAME=Linux），
#   不注入任何 ISA flags（`-march=x86-64-v3`）、不注入任何 LTO flags（`-flto=auto`）。
#   AVX2 在依赖侧一律走各库自身机制（设计 §11.1 同节口径）：
#     * libwebp：WEBP_ENABLE_SIMD=ON（逐文件 AVX2 内核 + 运行时派发）；
#     * OpenImageIO：overlay 端口显式 `-DUSE_SIMD=avx2`（本任务落地，GCC 侧即 -mavx2）；
#     * x265 / SVT-AV1 / libaom：NASM 汇编 + 运行时派发；
#     * libjxl / jpegli：Highway 运行时派发。
#   自研 targets 的 `-march=x86-64-v3 -mtune=raptorlake|haswell` + LTO
#   （GCC `-flto=auto` / Clang `-flto=thin`+lld）全部保留在 cmake/avx2.cmake。
#
# —— 为什么依赖侧不注入（Windows 侧实测引爆，同机制在 Linux 同样成立）——
#   ① LTO：`/GL` 的 IL 对象与 CMake 的 WINDOWS_EXPORT_ALL_SYMBOLS/__create_def 机制冲突
#      （aom 端口即红）；Linux 侧虽无该 Windows-only 机制，但裁定要求两侧统一口径，
#      不为 LTO 在依赖侧开平台分叉。
#   ② ISA：libwebp 1.6.0 全局 ISA flag 会破坏其**逐文件** AVX2 + 运行时派发模型
#      （Windows 侧实测无损解码崩溃 0xC0000005；上游 commit「cmake: fix per-file
#       assembly flags」明文反对全局 flag）。libwebp 的 AVX2 内核在 Linux 上同样由
#       WEBP_ENABLE_SIMD + 逐文件 flags 提供，撤掉全局 flag 一个不丢。
#   ③ 附注：本侧因此也天然避开了 GCC/Clang 的 `-flto=auto` / `-flto=thin` 语法分叉
#      （triplet 作用域拿不到编译器 ID，无法就地分派）。
#
# 验收口径（修订后）：依赖 AVX2 内核经各自机制可查证；全产物功能零回退（ctest + 金样）。

# 原生 triplet 定位（同 Windows 侧：优先仓库内 vcpkg，退 vcpkg root）。
set(_pp_native_triplet "${CMAKE_CURRENT_LIST_DIR}/../vcpkg/triplets/x64-linux.cmake")
if(NOT EXISTS "${_pp_native_triplet}")
    set(_pp_native_triplet "${VCPKG_ROOT_DIR}/triplets/x64-linux.cmake")
endif()
if(NOT EXISTS "${_pp_native_triplet}")
    message(FATAL_ERROR
        "x64-linux-avx2: 找不到原生 triplet x64-linux.cmake"
        "（试过 ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/triplets/ 与 ${VCPKG_ROOT_DIR}/triplets/）")
endif()
include("${_pp_native_triplet}")
unset(_pp_native_triplet)

# 刻意不设 VCPKG_C_FLAGS / VCPKG_CXX_FLAGS / VCPKG_LINKER_FLAGS：
# 依赖侧 ISA/LTO 一律走各库自身机制（见文件头裁定记录）。
