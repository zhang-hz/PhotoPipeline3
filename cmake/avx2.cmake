# M4-T3 / 2026-09-24 / AVX2 编译地基（docs/v0.3.0-design.md §11.1；docs/m4-tasks.md §3 W0-T3）
#
# 目标机器 = i7-14700K（Raptor Lake），全产物 ISA 基线 = x86-64-v3
# （AVX2 + FMA + BMI1/BMI2 + F16C + LZCNT + MOVBE）。
#
# 本文件只负责**自研 targets**（pp_core / photopipeline / 单测 / M0 工具）的 flags。
# **vcpkg 依赖侧不注入 ISA/LTO flags**（M4-T3 两次实测引爆后的裁定，见
# docs/v0.3.0-design.md §11.1 勘误 f）：libwebp 的全局 `/arch:AVX2` 会破坏其逐文件 AVX2 +
# 运行时派发模型（无损解码 0xC0000005），`/GL` 的 IL 对象又与 aom 端口的
# WINDOWS_EXPORT_ALL_SYMBOLS/`__create_def` 机制冲突 ⇒ 依赖 AVX2 一律走各库自身机制
# （OIIO `-DUSE_SIMD=avx2`、libwebp WEBP_ENABLE_SIMD、x265/SVT/aom NASM 运行时派发、
# jxl/jpegli Highway）。故 triplets/x64-*-avx2.cmake 只承载命名身份，不设 VCPKG_*_FLAGS。
# M4-T21 修文（与 triplet 首注、设计 §11.1 改文同口径；此前本行仍写"由 overlay triplet 注入"）。
#
# 纪律（§11.2）：基线即 AVX2，整仓只保留**一层**运行期分派
# （src/core/simd 的 pp::simd::cpu_has_avx2()）；ref 实现服务测试与调试，
# 不作为第二套 ISA 产物存在。本文件不引入任何第二条 ISA 分支。
#
# 用法（CMakeLists.txt 在定义 target 之前 include 本文件）：
#     include("${CMAKE_SOURCE_DIR}/cmake/avx2.cmake")
#     target_link_libraries(pp_core PUBLIC pp_avx2_baseline ...)
# INTERFACE 目标 PUBLIC 挂在 pp_core 上 → 全部自研消费者（单测 / 工具 / GUI）
# 自动继承同一组编译与链接 flags。

if(TARGET pp_avx2_baseline)
    return()
endif()

add_library(pp_avx2_baseline INTERFACE)

if(MSVC)
    # —— Windows / MSVC（cl.exe；clang-cl 走同一分支：MSVC 变量在 clang-cl 下亦为真）——
    #   /arch:AVX2 = 允许 AVX2 代码生成（并定义 __AVX2__）
    #   /GL        = 全程序优化（编译期产出 IL）
    #   /LTCG      = 链接期消费 IL（与 /GL 成对，缺一则退化为普通链接）
    target_compile_options(pp_avx2_baseline INTERFACE /arch:AVX2 /GL)
    target_link_options(pp_avx2_baseline INTERFACE /LTCG)
    set(_pp_avx2_summary "MSVC /arch:AVX2 /GL + /LTCG")

elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    include(CheckCXXCompilerFlag)

    # §11.1：ISA 基线固定 x86-64-v3。编译器不认这个 arch 名 = 工具链太旧，
    # 直接硬失败（放行会静默降级到 sse2 默认值 —— 正是本任务要堵的口径漂移）。
    check_cxx_compiler_flag("-march=x86-64-v3" PP_HAVE_MARCH_X86_64_V3)
    if(NOT PP_HAVE_MARCH_X86_64_V3)
        message(FATAL_ERROR
            "avx2: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} 不支持 "
            "-march=x86-64-v3（需 GCC>=11 / Clang>=12）——AVX2 基线无法建立，拒绝降级构建。")
    endif()

    # §11.1：探测式 -mtune —— raptorlake 需 GCC>=13 / Clang>=17；不识别则回退 haswell
    # （回退只影响调度/指令选择取向，ISA 基线仍是 x86-64-v3，功能与正确性不变）。
    check_cxx_compiler_flag("-mtune=raptorlake" PP_HAVE_MTUNE_RAPTORLAKE)
    if(PP_HAVE_MTUNE_RAPTORLAKE)
        set(_pp_mtune "-mtune=raptorlake")
    else()
        set(_pp_mtune "-mtune=haswell")
        message(STATUS "avx2: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} 不识别 "
                       "-mtune=raptorlake → 回退 -mtune=haswell（ISA 基线仍为 x86-64-v3）")
    endif()

    # §11.1：LTO 只作用于自研 targets（本 target 的消费者即是自研 targets 全集）。
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_pp_lto_compile "-flto=auto")
        set(_pp_lto_link    "-flto=auto")
    else()
        # Clang：thin LTO；thin 需要 LLVM linker 才能生效，显式指定 lld。
        set(_pp_lto_compile "-flto=thin")
        set(_pp_lto_link    "-flto=thin" "-fuse-ld=lld")
    endif()

    target_compile_options(pp_avx2_baseline INTERFACE
        "-march=x86-64-v3" ${_pp_mtune} ${_pp_lto_compile})
    target_link_options(pp_avx2_baseline INTERFACE ${_pp_lto_link})
    set(_pp_avx2_summary
        "${CMAKE_CXX_COMPILER_ID} -march=x86-64-v3 ${_pp_mtune} "
        "${_pp_lto_compile}（链接 ${_pp_lto_link}）")

else()
    # 未覆盖的编译器（如 AppleClang 之外的工具链）：不静默放行，也不擅自造第二套 ISA 口径。
    message(FATAL_ERROR
        "avx2: 未覆盖的编译器 ID '${CMAKE_CXX_COMPILER_ID}' —— AVX2 基线（§11.1）只定义 "
        "MSVC 与 GCC/Clang 两条口径。新增工具链须先回报主对话裁定 flags 口径。")
endif()

# 构建日志可查证（§1 总出口 5“全产物 -march=x86-64-v3//arch:AVX2，构建日志可查证”）。
message(STATUS "PP-AVX2: 自研 targets 基线 flags = ${_pp_avx2_summary}")
unset(_pp_avx2_summary)
unset(_pp_mtune)
unset(_pp_lto_compile)
unset(_pp_lto_link)
