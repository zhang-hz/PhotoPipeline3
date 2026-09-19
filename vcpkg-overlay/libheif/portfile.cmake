vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO  strukturag/libheif
    REF "v${VERSION}"
    SHA512 ebb0bf74d6d1ae2d39a7cefcec0f4a04006fee330e147c49c863f6d4febb45e82ccebf36cc4e30812bd3591918ee8964fbde9a3a4793c5f60eaf30a0e011add0
    HEAD_REF master
    PATCHES
        cxx-linkage-pkgconfig.diff
        find-modules.diff
        gdk-pixbuf.patch
        symbol-exports.diff
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        aom         WITH_AOM_DECODER
        aom         WITH_AOM_ENCODER
        aom         VCPKG_LOCK_FIND_PACKAGE_AOM
        gdk-pixbuf  WITH_GDK_PIXBUF
        hevc        WITH_X265
        hevc        VCPKG_LOCK_FIND_PACKAGE_X265
        iso23001-17 WITH_UNCOMPRESSED_CODEC
        iso23001-17 VCPKG_LOCK_FIND_PACKAGE_ZLIB
        jpeg        WITH_JPEG_DECODER
        jpeg        WITH_JPEG_ENCODER
        jpeg        VCPKG_LOCK_FIND_PACKAGE_JPEG
        openjpeg    WITH_OpenJPEG_DECODER
        openjpeg    WITH_OpenJPEG_ENCODER
        openjpeg    VCPKG_LOCK_FIND_PACKAGE_OpenJPEG
        svt-av1     WITH_SvtEnc
        svt-av1     VCPKG_LOCK_FIND_PACKAGE_SvtEnc
        h264        WITH_X264
        openh264    WITH_OpenH264_DECODER
)

vcpkg_find_acquire_program(PKGCONFIG)
set(ENV{PKG_CONFIG} "${PKGCONFIG}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTING=OFF
        -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
        "-DCMAKE_PROJECT_INCLUDE=${CURRENT_PORT_DIR}/cmake-project-include.cmake"
        -DPLUGIN_DIRECTORY=  # empty
        -DWITH_DAV1D=OFF
        -DWITH_EXAMPLES=OFF
        -DWITH_LIBSHARPYUV=OFF
        -DWITH_OpenH264_DECODER=OFF
        # SUB-CD overlay fix: SVT-AV1 defaults to plugin mode
        # (plugin_option(SvtEnc ... OFF ON)), aom/x265/libde265 default to
        # built-in. With vcpkg 2026.07.29 the x64-linux triplet is static, so a
        # plugin would only be loadable via LIBHEIF_PLUGIN_PATH at runtime;
        # force built-in so heif_get_encoder_descriptors() lists SVT-AV1.
        -DWITH_SvtEnc_PLUGIN=OFF
        -DVCPKG_LOCK_FIND_PACKAGE_Brotli=OFF
        -DVCPKG_LOCK_FIND_PACKAGE_Doxygen=OFF
        -DVCPKG_LOCK_FIND_PACKAGE_LIBDE265=ON   # feature candidate
        -DVCPKG_LOCK_FIND_PACKAGE_PNG=OFF
        -DVCPKG_LOCK_FIND_PACKAGE_TIFF=OFF
        ${FEATURE_OPTIONS}
    OPTIONS_RELEASE
        "-DPLUGIN_INSTALL_DIRECTORY=${CURRENT_PACKAGES_DIR}/plugins/libheif"
    OPTIONS_DEBUG
        "-DPLUGIN_INSTALL_DIRECTORY=${CURRENT_PACKAGES_DIR}/debug/plugins/libheif"
    MAYBE_UNUSED_VARIABLES
        VCPKG_LOCK_FIND_PACKAGE_AOM
        VCPKG_LOCK_FIND_PACKAGE_Brotli
        VCPKG_LOCK_FIND_PACKAGE_OpenJPEG
        VCPKG_LOCK_FIND_PACKAGE_SvtEnc
        VCPKG_LOCK_FIND_PACKAGE_X265
        VCPKG_LOCK_FIND_PACKAGE_ZLIB
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/libheif")
vcpkg_fixup_pkgconfig()

# SUB-CD overlay fixes: the upstream-generated libheif-config.cmake has two gaps
# for static consumers (both reproduced with OpenImageIO 3.1.14.0):
#   * it references AOM::aom without find_dependency(AOM)  -> configure/generate error
#   * it references libSvtAv1Enc.a without the unvendored fastfeat library -> link error
# The fix texts live in separate files so they are appended verbatim.
if("aom" IN_LIST FEATURES)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/libheif-config-append-aom.cmake" _heif_cfg_fix)
    file(APPEND "${CURRENT_PACKAGES_DIR}/share/libheif/libheif-config.cmake" "${_heif_cfg_fix}")
    unset(_heif_cfg_fix)
endif()
if("svt-av1" IN_LIST FEATURES)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/libheif-config-append-svtenc.cmake" _heif_cfg_fix)
    file(APPEND "${CURRENT_PACKAGES_DIR}/share/libheif/libheif-config.cmake" "${_heif_cfg_fix}")
    unset(_heif_cfg_fix)
endif()

if (VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/libheif/heif_export.h" "!defined(LIBHEIF_STATIC_BUILD)" "1")
else()
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/libheif/heif_export.h" "!defined(LIBHEIF_STATIC_BUILD)" "0")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib/libheif" "${CURRENT_PACKAGES_DIR}/debug/lib/libheif")

file(GLOB maybe_plugins "${CURRENT_PACKAGES_DIR}/plugins/libheif/*")
if(maybe_plugins STREQUAL "")
    file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/plugins" "${CURRENT_PACKAGES_DIR}/debug/plugins")
endif()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
