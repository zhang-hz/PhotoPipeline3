cmake_minimum_required(VERSION 3.28)
project(PhotoPipeline VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PP_BUILD_DEV "Build dev-only tools" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_program(PP_CCACHE ccache)
if(PP_CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${PP_CCACHE})
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenImageIO REQUIRED CONFIG)
find_package(JPEG REQUIRED)
find_package(exiv2 REQUIRED CONFIG)
find_package(libheif REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
pkg_check_modules(LIBJXL REQUIRED IMPORTED_TARGET libjxl)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp libwebpencode)
pkg_check_modules(TIFF4 REQUIRED IMPORTED_TARGET libtiff-4)
# PP-NOTE: find_package/pkg-config 目标名允许按 vcpkg_installed 实际情况机械修正，逐条记录

add_library(pp_core STATIC src/core/format_tables.cpp)
target_include_directories(pp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(photopipeline src/main.cpp src/ui/mainwindow.cpp)
target_link_libraries(photopipeline PRIVATE pp_core Qt6::Widgets)
target_compile_definitions(photopipeline PRIVATE PP_M0_SMOKE)  # M1 removes

add_executable(pp_linkprobe tools/pp_linkprobe.cpp)
target_link_libraries(pp_linkprobe PRIVATE
    OpenImageIO::OpenImageIO JPEG::JPEG PkgConfig::LCMS2 exiv2::exiv2
    libheif::libheif PkgConfig::LIBJXL PkgConfig::WEBP)

add_executable(pp_mkfixtures tools/pp_mkfixtures.cpp)
target_link_libraries(pp_mkfixtures PRIVATE
    OpenImageIO::OpenImageIO JPEG::JPEG exiv2::exiv2
    libheif::libheif PkgConfig::LIBJXL PkgConfig::WEBP PkgConfig::TIFF4)

add_executable(pp_spikes tools/pp_spikes.cpp)
target_link_libraries(pp_spikes PRIVATE
    OpenImageIO::OpenImageIO PkgConfig::LCMS2 exiv2::exiv2)

add_executable(pp_unit tests/unit/test_smoke.cpp)
target_link_libraries(pp_unit PRIVATE pp_core)

enable_testing()
add_test(NAME linkprobe  COMMAND pp_linkprobe)
add_test(NAME unit       COMMAND pp_unit)
add_test(NAME fixtures   COMMAND pp_mkfixtures --verify ${CMAKE_SOURCE_DIR}/tests/golden/meta)
set_tests_properties(fixtures PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME spike_e    COMMAND pp_spikes e)
add_test(NAME spike_f    COMMAND pp_spikes f --golden-root ${CMAKE_SOURCE_DIR}/tests/golden)
set_tests_properties(spike_f PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME offscreen  COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen $<TARGET_FILE:photopipeline>)
set_tests_properties(offscreen PROPERTIES TIMEOUT 30)
