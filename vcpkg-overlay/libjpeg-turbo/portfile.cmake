# PP-FROZEN(structure): 拦截官方 libjpeg-turbo → 构建 google/jpegli
# 选项名与安装目标以 jpegli 实际 CMake 为准核对（修正逐条记录于报告 api-deltas）
#
# REF: google/jpegli 上游没有任何 tag/release
#   (`git ls-remote --tags https://github.com/google/jpegli` 为空；
#    GitHub tags/releases API 均为空数组)，故钉死 main HEAD 的 commit SHA
#   (031a0077f5799a6041004267fc12b956c1f52a20, 2026-06-01) + 下方 SHA512，
#   仍为不可变、可复现的引用。
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO google/jpegli
    REF "031a0077f5799a6041004267fc12b956c1f52a20"
    SHA512 4d57baf7ff88a2a40f46dc0c4fe121df1455064ad36c3a6a479e6536f02c9e436088fb15462336f65bc5c7c6dda54bb740e1c05744399c834aa88a36c6635db9
)

# jpegli 的 CMake 无条件 configure_file() 需要
# third_party/libjpeg-turbo/{jconfig.h.in,jpeglib.h,jmorecfg.h}，而该目录是 git
# submodule（GitHub archive tarball 里是空目录）。不能用 vcpkg 的 libjpeg-turbo
# port 取（本 port 正在遮蔽它），故按 google/jpegli 钉死的 submodule commit
# (gitlink SHA, 见 GitHub contents API) 单独取上游源并拷入。
vcpkg_from_github(
    OUT_SOURCE_PATH JPEGTURBO_SOURCE_PATH
    REPO libjpeg-turbo/libjpeg-turbo
    REF "8ecba3647edb6dd940463fedf38ca33a8e2a73d1"
    SHA512 3b40c10d526350ef61e0ed804b6e33f2ba0393aa31210fd4924f4fd421b84ca842a1e6450f1bcc7a6b01cdb1d3fa11d91fe81552e73913b149a0555bfec19380
)
file(COPY
    "${JPEGTURBO_SOURCE_PATH}/jconfig.h.in"
    "${JPEGTURBO_SOURCE_PATH}/jpeglib.h"
    "${JPEGTURBO_SOURCE_PATH}/jmorecfg.h"
    DESTINATION "${SOURCE_PATH}/third_party/libjpeg-turbo"
)

# jerror.h 是 libjpeg 兼容公开头之一，但 jpegli 上游对它没有任何规则：
# lib/jpegli.cmake 只 configure_file jconfig.h.in / jpeglib.h / jmorecfg.h 到
# build 的 include/jpegli/，再 install(DIRECTORY ... include/jpegli/[去尾斜杠])
# 把这三个头铺到 ${INCLUDEDIR}（无 jerror.h）。而 libtiff 的 tif_jpeg.c 无条件
# #include "jerror.h" ⇒ 干净环境必然 fatal error: jerror.h: No such file or
# directory（开发机曾靠 /usr/include/jerror.h 侥幸编过，属未声明的隐式依赖）。
# 从同一 pinned libjpeg-turbo 源（上方 REF 8ecba364…）取 jerror.h，与 jpeglib.h
# 同 tree、同目录安装，保证 ABI/API 配对；不得改用系统头。
file(INSTALL "${JPEGTURBO_SOURCE_PATH}/jerror.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# 关闭测试/工具/文档/可选后端；只产出 libjpeg 兼容共享库 + 公开头。
# 选项名均取自上游 CMakeLists.txt 的 CACHE 变量定义。
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTING=OFF
        -DJPEGLI_ENABLE_TOOLS=OFF
        -DJPEGLI_ENABLE_DEVTOOLS=OFF
        -DJPEGLI_ENABLE_FUZZERS=OFF
        -DJPEGLI_ENABLE_BENCHMARK=OFF
        -DJPEGLI_ENABLE_DOXYGEN=OFF
        -DJPEGLI_ENABLE_MANPAGES=OFF
        -DJPEGLI_ENABLE_JNI=OFF
        -DJPEGLI_ENABLE_SJPEG=OFF
        -DJPEGLI_ENABLE_OPENEXR=OFF
        -DJPEGLI_ENABLE_TCMALLOC=OFF
        -DJPEGLI_ENABLE_SKCMS=OFF
        -DJPEGLI_BUNDLE_LIBPNG=OFF
        -DJPEGLI_FORCE_SYSTEM_HWY=ON
        -DJPEGLI_FORCE_SYSTEM_LCMS2=ON
        -DJPEGLI_ENABLE_JPEGLI_LIBJPEG=ON
        -DJPEGLI_INSTALL_JPEGLI_LIBJPEG=ON
        -DJPEGLI_LIBJPEG_LIBRARY_VERSION=62.3.0
        -DJPEGLI_LIBJPEG_LIBRARY_SOVERSION=62
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

# jpegli 的公共 C API 静态库：上游没有 install 规则且 jpegli-static 是
# EXCLUDE_FROM_ALL，这里显式构建（rel+dbg）后按 libjpegli.a 安装，供需要
# jpegli_* 符号（而非仅 libjpeg 兼容导出层）的消费方链接使用。
vcpkg_cmake_build(TARGET jpegli-static)
set(_jpegli_static_rel "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/lib/libjpegli-static.a")
set(_jpegli_static_dbg "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/lib/libjpegli-static.a")
if(NOT EXISTS "${_jpegli_static_rel}" OR NOT EXISTS "${_jpegli_static_dbg}")
    message(FATAL_ERROR
        "jpegli-static archive missing (check upstream target/output name): "
        "'${_jpegli_static_rel}' / '${_jpegli_static_dbg}'")
endif()
file(INSTALL "${_jpegli_static_rel}"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib" RENAME libjpegli.a)
file(INSTALL "${_jpegli_static_dbg}"
    DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib" RENAME libjpegli.a)
unset(_jpegli_static_rel)
unset(_jpegli_static_dbg)

# jpegli 扩展头（公开 C API，上游路径 lib/jpegli/{encode,decode,common,types}.h）。
# encode.h/decode.h 内部以 "lib/jpegli/..."、"lib/base/include_jpeglib.h" 引用，
# 故同时安装 include/lib/ 前缀树；再补一份 <jpegli/encode.h> 形式
# （tools/*.cpp 的 __has_include 首选路径）。
file(INSTALL "${SOURCE_PATH}/lib/jpegli"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/lib"
    FILES_MATCHING
        PATTERN "encode.h"
        PATTERN "decode.h"
        PATTERN "common.h"
        PATTERN "types.h"
)
file(INSTALL "${SOURCE_PATH}/lib/base/include_jpeglib.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/lib/base"
)
file(INSTALL "${SOURCE_PATH}/lib/jpegli"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include"
    FILES_MATCHING
        PATTERN "encode.h"
        PATTERN "decode.h"
        PATTERN "common.h"
        PATTERN "types.h"
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
# 官方 port 的 FindJPEG 包装器（用法兼容：find_package(JPEG) / JPEG::JPEG）
file(COPY "${CMAKE_CURRENT_LIST_DIR}/vcpkg-cmake-wrapper.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/jpeg")
# 官方 port 的 CONFIG 包（libjpeg-turbo::jpeg）——jpegli 自身不导出 CMake config，
# 而 tiff 等消费方会 find_package(libjpeg-turbo CONFIG)（见 ports/tiff/jpeccodec.patch）
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/libjpeg-turbo-config.cmake"
             "${CMAKE_CURRENT_LIST_DIR}/libjpeg-turbo-config-version.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/libjpeg-turbo")
file(INSTALL "${SOURCE_PATH}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright)
