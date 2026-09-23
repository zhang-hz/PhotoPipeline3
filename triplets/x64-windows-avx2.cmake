# M4-T3 / 2026-09-24 / AVX2 依赖基线 —— Windows / MSVC
# 依据：docs/v0.3.0-design.md §11.1（flags 表）＋ docs/m4-tasks.md §3 W0-T3
#        主对话裁定 dwfq-26e56ab0-3（LTO 收敛）与后续裁定（ISA flags 收敛）
#
# —— 本 triplet 的职责（收敛后的最终口径）——
#   **只承载命名身份**：继承 vcpkg 原生 triplet x64-windows（动态 CRT + DLL 链接形态
#   一字不改），不注入任何 ISA flags、不注入任何 LTO flags。
#   AVX2 在依赖侧的落地**一律走各库自己的机制**（设计 §11.1 同节口径）：
#     * libwebp：WEBP_ENABLE_SIMD=ON，其 AVX2 内核由 libwebp **逐文件** flags 构建
#       （WEBP_HAVE_FLAG_AVX2 能力探测 + 运行时 CPU 派发）；
#     * OpenImageIO：overlay 端口显式 `-DUSE_SIMD=avx2`（本任务落地）；
#     * x265 / SVT-AV1 / libaom：NASM 汇编 + 运行时派发（编译 flags 不影响其汇编内核）；
#     * libjxl / jpegli：Highway 运行时派发。
#   自研 targets（pp_core / photopipeline / 单测 / 工具）的 `/arch:AVX2` + `/GL` + `/LTCG`
#   全部保留在 cmake/avx2.cmake —— 那才是本任务真正的 ISA 基线注入点。
#
# —— 为什么依赖侧不注入 flags（两次实测引爆，均由主对话裁定）——
#   ① LTO（`/GL` + `/LTCG`）：aom 端口直接构建失败（debug，44 s）：
#        FAILED: aom_av1_rc.dll aom_av1_rc.lib
#        unrecognized file format in 'CMakeFiles\aom_av1_rc.dir\av1\ratectrl_rtc.cc.obj, 0'
#      根因：`/GL` 产出 IL 目标文件（dumpbin /headers → "File Type: ANONYMOUS OBJECT"，
#      无 COFF 符号表），而 aom 的 aom_av1_rc 目标带 WINDOWS_EXPORT_ALL_SYMBOLS ON
#      （aom CMakeLists.txt:403），CMake 生成 .def 时走 `cmake -E __create_def
#      --nm=CMAKE_NM-NOTFOUND`，其内置 COFF 读取器读不了 IL 对象。机制性不可绕。
#      → 裁定：LTO 只作用于自研 targets（与 §11.1 散文「LTO 只作用于自研 targets」一致）。
#   ② ISA（`/arch:AVX2`）：libwebp 1.6.0 **无损解码**崩溃（0xC0000005），有最小复现：
#        探针（仅链 libwebp）WebPGetInfo=1 64x64 通过 → WebPDecodeRGBA 段错误；
#        同探针 + 无 /arch:AVX2 的 libwebp → checksum=2192 OK；lossy 文件两者都 OK。
#      40 个依赖 DLL 逐个回填扫描：唯一触发崩溃的就是 libwebp.dll。
#      根因：libwebp 的 AVX2 源文件（1.6.0 新增 src/dsp/lossless_avx2.c 等）必须**逐文件**
#      加 /arch:AVX2；全局 flag 让 `WEBP_MSC_AVX2` → `WEBP_USE_AVX2` 在更多 TU 打开，
#      破坏其运行时派发模型。上游 commit「cmake: fix per-file assembly flags」原文：
#      "the highest level assembly flag would be applied to all assembly files ... would
#       defeat the runtime cpu detection check and could result in a crash"。
#      → 裁定：依赖侧不注入 ISA flags（铁律「禁止降级」优先；功能零回退 > flag 铺满）。
#   注：per-port overlay triplet 不受支持（实测：--overlay-triplets 下
#   `<dir>/libwebp/<triplet>.cmake` 不被加载），故无法在 triplet 层做单端口豁免。
#
# 验收口径（修订后，见 M4-T3 返回结果 deviations 第 2 条）：
#   (i) 自研 targets 构建日志 AVX2 flags 可查证；(ii) 依赖 AVX2 内核经各自机制可查证；
#   (iii) 全产物功能零回退（ctest + 金样，webp-lossless 为哨兵）。

# 原生 triplet 定位：优先仓库内 vcpkg（CI 与本机都是 <repo>/vcpkg），
# 找不到再退回 vcpkg 自己的 root（triplet 作用域里由 vcpkg-tool 注入）。
set(_pp_native_triplet "${CMAKE_CURRENT_LIST_DIR}/../vcpkg/triplets/x64-windows.cmake")
if(NOT EXISTS "${_pp_native_triplet}")
    set(_pp_native_triplet "${VCPKG_ROOT_DIR}/triplets/x64-windows.cmake")
endif()
if(NOT EXISTS "${_pp_native_triplet}")
    message(FATAL_ERROR
        "x64-windows-avx2: 找不到原生 triplet x64-windows.cmake"
        "（试过 ${CMAKE_CURRENT_LIST_DIR}/../vcpkg/triplets/ 与 ${VCPKG_ROOT_DIR}/triplets/）")
endif()
include("${_pp_native_triplet}")
unset(_pp_native_triplet)

# 刻意不设 VCPKG_C_FLAGS / VCPKG_CXX_FLAGS / VCPKG_LINKER_FLAGS：
# 依赖侧 ISA/LTO 一律走各库自身机制（见文件头裁定记录）。
