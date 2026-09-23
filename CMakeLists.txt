cmake_minimum_required(VERSION 3.28)
project(PhotoPipeline VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PP_BUILD_DEV "Build dev-only tools" OFF)
set(CMAKE_AUTOMOC ON)  # Q_OBJECT (mainwindow) — wave3 template fix

# 版本单源（M2-T11 §2.2）：版本号唯一定义点 = 上面的 project(PhotoPipeline VERSION …)。
# cmake/version.h.in 用 @…@ 占位（@ONLY → 不碰任何 ${} 字面量）生成到构建目录，
# 源码树零污染；生成头经 pp_core 的 PUBLIC include 目录传播给全部消费者，
# 代码统一 `#include "core/version.h"`。
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/gen/core")
configure_file("${CMAKE_SOURCE_DIR}/cmake/version.h.in"
               "${CMAKE_BINARY_DIR}/gen/core/version.h" @ONLY)

# M3: Qt 前缀自包含（presets 不再依赖 $env{QT_DIR}）。env QT_DIR 优先；否则用仓库内
# aqt 布局（QT_VERSION 单源 = tools/versions.env；平台分派 arch 目录）。
file(READ "${CMAKE_SOURCE_DIR}/tools/versions.env" _pp_versions_env)
string(REGEX MATCH "QT_VERSION=([0-9][0-9.]*)" _pp_qt_version_match "${_pp_versions_env}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "QT_VERSION not found in tools/versions.env")
endif()
set(_pp_qt_version "${CMAKE_MATCH_1}")
if(DEFINED ENV{QT_DIR} AND NOT "$ENV{QT_DIR}" STREQUAL "")
    set(QT_DIR "$ENV{QT_DIR}" CACHE PATH "Qt installation prefix")
else()
    if(WIN32)
        set(_pp_qt_arch "msvc2022_64")
    else()
        set(_pp_qt_arch "gcc_64")
    endif()
    set(QT_DIR "${CMAKE_SOURCE_DIR}/.toolchain/Qt/${_pp_qt_version}/${_pp_qt_arch}"
        CACHE PATH "Qt installation prefix (repo-local aqt layout)")
    unset(_pp_qt_arch)
endif()
list(PREPEND CMAKE_PREFIX_PATH "${QT_DIR}")
unset(_pp_versions_env)
unset(_pp_qt_version_match)
unset(_pp_qt_version)

# M3-D2: Windows 应用清单（activeCodePage=UTF-8）随版本号生成后嵌入可执行文件。
configure_file("${CMAKE_SOURCE_DIR}/cmake/photopipeline.manifest.in"
               "${CMAKE_BINARY_DIR}/gen/photopipeline.manifest" @ONLY)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    # M3-D2: /utf-8 = 源码与执行字符集均为 UTF-8（与清单 activeCodePage 配对）。
    # /external:anglebrackets + /external:W0 = 尖括号包含的系统头（Qt/OIIO/STL）
    # 不参与 /W4 告警；/permissive- = 标准符合模式；NOMINMAX = 禁用 windows.h 的
    # min/max 宏（与 std::min/max 共存）。
    add_compile_options(/utf-8 /W4 /permissive- /external:anglebrackets /external:W0 /DNOMINMAX)
endif()

# ccache 可用性回退（M1-T15；R18/D7）：只有 ccache 真正可用时才设 launcher，否则整条构建会红。
# 背景（R18 实测）：未注入工具链环境（原 `source tools/env.sh`，M3-W3 起为
# `python tools/env.py run -- …`）时 CCACHE_DIR 未设、HOME 也不可用 → ccache 把缓存目录
# 解析到一个只读位置，第一次真正编译就 `ccache: error: Read-only file system` 退出 1，ninja 于是对
# 每个编译单元报错。回退策略 = 不带 launcher 继续构建（退化为无缓存），而不是让构建失败。
#
# 探测分两级（两级都过才设 launcher）：
#   ① `ccache --version`（成本 ~5ms）：抓"二进制在 PATH 里但根本跑不起来"（缺动态库/架构不符/包装脚本报错）。
#   ② 让 ccache 真的编译一个空 TU（成本 ~50ms）：**这一级才是 R18 的检测点**。实测 `ccache --version`
#      在 R18 环境下仍返回 0（`--version` 不碰缓存目录），只有真实编译才会暴露只读缓存目录。
find_program(PP_CCACHE ccache)
if(PP_CCACHE)
    execute_process(COMMAND ${PP_CCACHE} --version
                    RESULT_VARIABLE PP_CCACHE_RESULT
                    OUTPUT_QUIET ERROR_QUIET)
    if(NOT PP_CCACHE_RESULT EQUAL 0)
        message(STATUS "ccache found at ${PP_CCACHE} but not runnable (exit ${PP_CCACHE_RESULT}); "
                       "building without compiler launcher")
        set(PP_CCACHE "")
    endif()
endif()
if(PP_CCACHE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(PP_CCACHE_PROBE_SRC "${CMAKE_BINARY_DIR}/pp_ccache_probe.cpp")
    set(PP_CCACHE_PROBE_OBJ "${CMAKE_BINARY_DIR}/pp_ccache_probe.o")
    file(WRITE "${PP_CCACHE_PROBE_SRC}" "int pp_ccache_probe() { return 0; }\n")
    execute_process(COMMAND ${PP_CCACHE} ${CMAKE_CXX_COMPILER} -c ${PP_CCACHE_PROBE_SRC}
                            -o ${PP_CCACHE_PROBE_OBJ}
                    RESULT_VARIABLE PP_CCACHE_PROBE_RESULT
                    OUTPUT_QUIET ERROR_QUIET)
    file(REMOVE "${PP_CCACHE_PROBE_SRC}" "${PP_CCACHE_PROBE_OBJ}")
    if(NOT PP_CCACHE_PROBE_RESULT EQUAL 0)
        message(STATUS "ccache found at ${PP_CCACHE} but its cache directory is unusable "
                       "(probe compile exit ${PP_CCACHE_PROBE_RESULT}); building without compiler "
                       "launcher (R18: run `python tools/env.py run -- ...` to set CCACHE_DIR)")
        set(PP_CCACHE "")
    endif()
endif()
if(PP_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${PP_CCACHE})
endif()

enable_testing()

# 1) 组件与静态 jpegli (M1 §2.3)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)   # Core/Gui 随 Widgets
find_package(OpenImageIO REQUIRED CONFIG)
find_package(JPEG REQUIRED)
find_package(exiv2 REQUIRED CONFIG)
find_package(libheif REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
pkg_check_modules(LIBJXL REQUIRED IMPORTED_TARGET libjxl)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
pkg_check_modules(WEBPMUX REQUIRED IMPORTED_TARGET libwebpmux)  # M3-W4a v1.8: enc_webp 的 ICCP mux API（WebPMux*）
pkg_check_modules(TIFF4 REQUIRED IMPORTED_TARGET libtiff-4)
# M3-W3: 共享脚本 Python 单实现后，ui_smoke 的 ctest 接线改走解释器。
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# jpegli public C API (jpegli_*): upstream builds it into the EXCLUDE_FROM_ALL
# jpegli-static target, and the compat libjpeg.so hides those symbols via
# -Wl,--exclude-libs=ALL, so the overlay port installs the archive as libjpegli.a
# and exports it from its CONFIG package (with the required libhwy link interface).
# wave3 ruling / M1 §2.3 item 1: consumers link libjpeg-turbo::jpegli-static + JPEG::JPEG.
find_package(libjpeg-turbo CONFIG REQUIRED)
# PP-NOTE: find_package/pkg-config 目标名允许按 vcpkg_installed 实际情况机械修正，逐条记录

# 2) pp_core：glob 收编 core/decode/codecs（CONFIGURE_DEPENDS → 新增文件自动生效，M1 §2.3）
#    M1b：paths.cpp（U1，Qt-free 平台路径）与 coord.cpp（U2，纯数学 GCJ-02）编入 pp_core
#    供单测链接；两者同时从下方 UI glob 剔除。文件未存在时 GLOB 空匹配、REMOVE_ITEM no-op。
file(GLOB_RECURSE PP_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/core/*.cpp
     ${CMAKE_SOURCE_DIR}/src/decode/*.cpp
     ${CMAKE_SOURCE_DIR}/src/codecs/*.cpp
     ${CMAKE_SOURCE_DIR}/src/platform/paths.cpp
     ${CMAKE_SOURCE_DIR}/src/mapwidget/coord.cpp
     ${CMAKE_SOURCE_DIR}/src/ui/preset_io.cpp)   # 唯一 Qt 适配层（JSON），UI/harness/测试三处复用
add_library(pp_core STATIC ${PP_CORE_SOURCES})
# M2-T11 §2.2：生成的 core/version.h（构建目录）随 src 一并 PUBLIC 传播
target_include_directories(pp_core PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/gen)
target_link_libraries(pp_core PUBLIC OpenImageIO::OpenImageIO JPEG::JPEG
    libjpeg-turbo::jpegli-static Exiv2::exiv2lib heif PkgConfig::LIBJXL
    PkgConfig::WEBP PkgConfig::WEBPMUX PkgConfig::LCMS2 PkgConfig::TIFF4 spdlog::spdlog Qt6::Core)

# 2b) 静态库自注册的唯一链接形态（M2-T13）。pp_core 是 STATIC 库，编码器 TU 里的
#     PP_REGISTER_ENCODER 静态初始化器只有在归档成员被拉入时才会运行；因此每个
#     pp_core 消费者一律通过本 INTERFACE 目标链接 —— whole-archive 只在此处定义
#     一次（T7c 裁定的固化），新 target 无从遗漏，也不再需要任何空函数"链接锚点"
#     （T13 已删除 enc_oiio.cpp / enc_heif.cpp 的 anchor）。
add_library(pp_core_registered INTERFACE)
target_link_libraries(pp_core_registered INTERFACE $<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>)

# 3) 单测：glob tests/unit/test_*.cpp，每个文件一个 target + ctest 条目（M1 §2.3）
#    取代 M0 的 add_executable(pp_unit …) 与 add_test(NAME unit …)（test_smoke.cpp 自动收编）
file(GLOB PP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/unit/test_*.cpp)
foreach(src ${PP_TEST_SOURCES})
  get_filename_component(tname ${src} NAME_WE)          # test_fsops → ctest 名 test_fsops
  add_executable(pp_${tname} ${src})
  if(WIN32)
    # M3 v1.9（D2 单一 UTF-8 世界）：测试二进制同样嵌入应用清单。否则进程 ACP 跟随系统
    # 区域（本机 GBK），tests/golden 的 📸 等字符经宽→窄转换失败 → 未捕获异常 → abort
    # （实测 0xc0000409 / FAST_FAIL_FATAL_APP_EXIT）。清单使进程 ACP=UTF-8，与 Linux 的
    # UTF-8 字节语义对齐。
    target_sources(pp_${tname} PRIVATE ${CMAKE_BINARY_DIR}/gen/photopipeline.manifest)
  endif()
  # 静态库自注册：唯一链接形态（M2-T13）= pp_core_registered
  target_link_libraries(pp_${tname} PRIVATE pp_core_registered)
  add_test(NAME ${tname} COMMAND pp_${tname})
  # M3-T11b：测试超时兜底（与 CMakePresets 的 execution.timeout=120 双保险）。
  # 单个挂起用例由"吃掉整个 CI job"变为"2 分钟内失败并点名"（run #21 的 windows job
  # 曾因 test_presets 挂起 66 min 而触顶取消）。
  set_tests_properties(${tname} PROPERTIES TIMEOUT 120)
endforeach()

# 4) UI 可执行：glob ui/mapwidget/platform（M1 §2.3）
file(GLOB_RECURSE PP_UI_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/ui/*.cpp
     ${CMAKE_SOURCE_DIR}/src/mapwidget/*.cpp
     ${CMAKE_SOURCE_DIR}/src/platform/*.cpp)
list(REMOVE_ITEM PP_UI_SOURCES ${CMAKE_SOURCE_DIR}/src/ui/preset_io.cpp  # 已在 pp_core，避免重复符号
     ${CMAKE_SOURCE_DIR}/src/platform/paths.cpp ${CMAKE_SOURCE_DIR}/src/mapwidget/coord.cpp)  # M1b：同上
add_executable(photopipeline src/main.cpp ${PP_UI_SOURCES})
if(WIN32)
    # M3-D2/D5/D6: 嵌入应用清单（UTF-8 代码页）；GUI 子系统（双击无控制台，
    # --version/--dev/--ui-smoke 由 main.cpp 的 AttachConsole 挂接父控制台）；
    # mica.cpp 需要 dwmapi（DwmSetWindowAttribute）。
    target_sources(photopipeline PRIVATE ${CMAKE_BINARY_DIR}/gen/photopipeline.manifest)
    set_target_properties(photopipeline PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(photopipeline PRIVATE dwmapi)
endif()
# 静态库自注册：唯一链接形态（M2-T13）= pp_core_registered
target_link_libraries(photopipeline PRIVATE pp_core_registered
    Qt6::Widgets Qt6::Network)
# dev harness 接线（M1-T8，§3.15 裁定）：option 已存在但此前无 target 消费 →
# 发布构建（不带 -DPP_BUILD_DEV=ON）不含 `--dev` 代码路径；鼓点/矩阵/规模用 -DPP_BUILD_DEV=ON。
if(PP_BUILD_DEV)
  target_compile_definitions(photopipeline PRIVATE PP_BUILD_DEV)
  # M1b-U10：UI 冒烟取代 M0 的 PP_M0_SMOKE 定时退出桩。走脚本以便同时校验冻结成功末行
  # `UI-SMOKE OK shots=7 pages=3`；offscreen 平台由测试环境注入。
  add_test(NAME ui_smoke COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ui_smoke.py ${CMAKE_BINARY_DIR})
  set_tests_properties(ui_smoke PROPERTIES TIMEOUT 240)
endif()

# 5) 既有 M0 target（pp_linkprobe / pp_mkfixtures / pp_spikes / 其 ctest 条目）保持不变
#    新增工具按需显式 add_executable（T8 加 pp_verify；其它工具不得改动此块）
add_executable(pp_linkprobe tools/pp_linkprobe.cpp)
target_link_libraries(pp_linkprobe PRIVATE
    OpenImageIO::OpenImageIO JPEG::JPEG PkgConfig::LCMS2 Exiv2::exiv2lib
    heif PkgConfig::LIBJXL PkgConfig::WEBP libjpeg-turbo::jpegli-static)

add_executable(pp_mkfixtures tools/pp_mkfixtures.cpp)
target_link_libraries(pp_mkfixtures PRIVATE
    OpenImageIO::OpenImageIO JPEG::JPEG Exiv2::exiv2lib
    heif PkgConfig::LIBJXL PkgConfig::WEBP PkgConfig::TIFF4 libjpeg-turbo::jpegli-static)

add_executable(pp_spikes tools/pp_spikes.cpp)
target_link_libraries(pp_spikes PRIVATE
    OpenImageIO::OpenImageIO PkgConfig::LCMS2 Exiv2::exiv2lib)
# 静态库自注册：唯一链接形态（M2-T13）= pp_core_registered —— M0 tool target 亦为 pp_core 消费者
target_link_libraries(pp_linkprobe PRIVATE pp_core_registered)
target_link_libraries(pp_mkfixtures PRIVATE pp_core_registered)
target_link_libraries(pp_spikes PRIVATE pp_core_registered)

add_test(NAME linkprobe  COMMAND pp_linkprobe)
add_test(NAME fixtures   COMMAND pp_mkfixtures --verify ${CMAKE_SOURCE_DIR}/tests/golden/meta)
set_tests_properties(fixtures PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME spike_e    COMMAND pp_spikes e)
add_test(NAME spike_f    COMMAND pp_spikes f --golden-root ${CMAKE_SOURCE_DIR}/tests/golden)
set_tests_properties(spike_f PROPERTIES SKIP_RETURN_CODE 77)
# M3-T11b：M0 工具条目同样加超时兜底（SKIP_RETURN_CODE 已在上面单独设置，互不覆盖）
set_tests_properties(linkprobe fixtures spike_e spike_f PROPERTIES TIMEOUT 120)

# 8) pp_verify（M1-T8；§2.3 所有者序列 T8 的唯一 target 增量）
#    断言 tests/golden/SCHEMA.md 的 pixel/metadata/warnings 三态；--selftest 不需语料。
add_executable(pp_verify tools/pp_verify.cpp)
# 静态库自注册：唯一链接形态（M2-T13）= pp_core_registered
target_link_libraries(pp_verify PRIVATE pp_core_registered)
add_test(NAME verify_selftest COMMAND pp_verify --selftest)
# M3-T11b：与其余条目统一（23 个 ctest 条目全部带 TIMEOUT 120 属性）
set_tests_properties(verify_selftest PROPERTIES TIMEOUT 120)

# M3 v1.9（D2）：工具二进制同样嵌入 UTF-8 清单（同上；pp_mkfixtures/pp_verify/spike_f
# 均遍历含非 ASCII 文件名的 tests/golden）。
if(WIN32)
  foreach(pp_tool pp_linkprobe pp_mkfixtures pp_spikes pp_verify)
    target_sources(${pp_tool} PRIVATE ${CMAKE_BINARY_DIR}/gen/photopipeline.manifest)
  endforeach()
endif()
