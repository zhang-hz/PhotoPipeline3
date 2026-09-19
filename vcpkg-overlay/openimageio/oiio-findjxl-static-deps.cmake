# SUB-CD overlay append (PhotoPipeline M0), appended verbatim to OIIO's
# src/cmake/modules/FindJXL.cmake by vcpkg-overlay/openimageio/portfile.cmake.
#
# libjxl / libjxl_threads are static archives under the pinned vcpkg triplet
# (x64-linux has VCPKG_LIBRARY_LINKAGE=static), and the upstream module reports only
# those two libraries. The transitive static link closure therefore has to be added
# here, mirroring `pkg-config --static --libs libjxl`:
#   -ljxl -lhwy -lbrotlienc -lbrotlidec -lbrotlicommon -ljxl_cms -llcms2
# Without it every consumer of the JXL plugin fails with undefined references
# (hwy::*, BrotliDecoder*/BrotliEncoder*, cms*).
if(JXL_FOUND)
    find_library(JXL_HWY_LIBRARY NAMES hwy)
    find_library(JXL_BROTLIENC_LIBRARY NAMES brotlienc)
    find_library(JXL_BROTLIDEC_LIBRARY NAMES brotlidec)
    find_library(JXL_BROTLICOMMON_LIBRARY NAMES brotlicommon)
    find_library(JXL_CMS_LIBRARY NAMES jxl_cms)
    find_library(JXL_LCMS_LIBRARY NAMES lcms2)
    foreach(_jxl_dep
            JXL_HWY_LIBRARY
            JXL_BROTLIENC_LIBRARY
            JXL_BROTLIDEC_LIBRARY
            JXL_BROTLICOMMON_LIBRARY
            JXL_CMS_LIBRARY
            JXL_LCMS_LIBRARY)
        if(${_jxl_dep})
            list(APPEND JXL_LIBRARIES "${${_jxl_dep}}")
        endif()
    endforeach()
    unset(_jxl_dep)
endif()

mark_as_advanced(
    JXL_HWY_LIBRARY
    JXL_BROTLIENC_LIBRARY
    JXL_BROTLIDEC_LIBRARY
    JXL_BROTLICOMMON_LIBRARY
    JXL_CMS_LIBRARY
    JXL_LCMS_LIBRARY
)
