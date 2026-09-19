cmake_minimum_required(VERSION 3.28)
project(PhotoPipeline VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PP_BUILD_DEV "Build dev-only tools" OFF)
set(CMAKE_AUTOMOC ON)  # Q_OBJECT (mainwindow) — wave3 template fix

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_program(PP_CCACHE ccache)
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
pkg_check_modules(TIFF4 REQUIRED IMPORTED_TARGET libtiff-4)
# jpegli public C API (jpegli_*): upstream builds it into the EXCLUDE_FROM_ALL
# jpegli-static target, and the compat libjpeg.so hides those symbols via
# -Wl,--exclude-libs=ALL, so the overlay port installs the archive as libjpegli.a
# and exports it from its CONFIG package (with the required libhwy link interface).
# wave3 ruling / M1 §2.3 item 1: consumers link libjpeg-turbo::jpegli-static + JPEG::JPEG.
find_package(libjpeg-turbo CONFIG REQUIRED)
# PP-NOTE: find_package/pkg-config 目标名允许按 vcpkg_installed 实际情况机械修正，逐条记录

# 2) pp_core：glob 收编 core/decode/codecs（CONFIGURE_DEPENDS → 新增文件自动生效，M1 §2.3）
file(GLOB_RECURSE PP_CORE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/core/*.cpp
     ${CMAKE_SOURCE_DIR}/src/decode/*.cpp
     ${CMAKE_SOURCE_DIR}/src/codecs/*.cpp
     ${CMAKE_SOURCE_DIR}/src/ui/preset_io.cpp)   # 唯一 Qt 适配层（JSON），UI/harness/测试三处复用
add_library(pp_core STATIC ${PP_CORE_SOURCES})
target_include_directories(pp_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(pp_core PUBLIC OpenImageIO::OpenImageIO JPEG::JPEG
    libjpeg-turbo::jpegli-static Exiv2::exiv2lib heif PkgConfig::LIBJXL
    PkgConfig::WEBP PkgConfig::LCMS2 PkgConfig::TIFF4 spdlog::spdlog Qt6::Core)

# 3) 单测：glob tests/unit/test_*.cpp，每个文件一个 target + ctest 条目（M1 §2.3）
#    取代 M0 的 add_executable(pp_unit …) 与 add_test(NAME unit …)（test_smoke.cpp 自动收编）
file(GLOB PP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/unit/test_*.cpp)
foreach(src ${PP_TEST_SOURCES})
  get_filename_component(tname ${src} NAME_WE)          # test_fsops → ctest 名 test_fsops
  add_executable(pp_${tname} ${src})
  # 静态库自注册：whole-archive 拉入编码器 TU（T7c）
  target_link_libraries(pp_${tname} PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>)
  add_test(NAME ${tname} COMMAND pp_${tname})
endforeach()

# 4) UI 可执行：glob ui/mapwidget/platform（M1 §2.3）
file(GLOB_RECURSE PP_UI_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/src/ui/*.cpp
     ${CMAKE_SOURCE_DIR}/src/mapwidget/*.cpp
     ${CMAKE_SOURCE_DIR}/src/platform/*.cpp)
list(REMOVE_ITEM PP_UI_SOURCES ${CMAKE_SOURCE_DIR}/src/ui/preset_io.cpp)  # 已在 pp_core，避免重复符号
add_executable(photopipeline src/main.cpp ${PP_UI_SOURCES})
# 静态库自注册：whole-archive 拉入编码器 TU（T7c）
target_link_libraries(photopipeline PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>
    Qt6::Widgets Qt6::Network)
target_compile_definitions(photopipeline PRIVATE PP_M0_SMOKE)  # M1 removes

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
# 静态库自注册：whole-archive 拉入编码器 TU（T7c）——M0 tool target 亦为 pp_core 消费者
target_link_libraries(pp_linkprobe PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>)
target_link_libraries(pp_mkfixtures PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>)
target_link_libraries(pp_spikes PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,pp_core>)

add_test(NAME linkprobe  COMMAND pp_linkprobe)
add_test(NAME fixtures   COMMAND pp_mkfixtures --verify ${CMAKE_SOURCE_DIR}/tests/golden/meta)
set_tests_properties(fixtures PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME spike_e    COMMAND pp_spikes e)
add_test(NAME spike_f    COMMAND pp_spikes f --golden-root ${CMAKE_SOURCE_DIR}/tests/golden)
set_tests_properties(spike_f PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME offscreen  COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen $<TARGET_FILE:photopipeline>)
set_tests_properties(offscreen PROPERTIES TIMEOUT 30)
