# vcpkg overlay fix (PhotoPipeline M0), appended to share/libheif/libheif-config.cmake:
# with the svt-av1 feature the exported static "heif" target links libSvtAv1Enc.a,
# which in turn needs the unvendored fastfeat library (the vcpkg svt-av1 port applies
# unvendor-fastfeat.diff and only expresses that dependency in SvtAv1Enc.pc
# "Libs: ... -lfastfeat"). CMake consumers link the archive directly, so the
# dependency has to be part of the exported link interface:
#   undefined reference to `fast9_detect_nonmax' (OpenImageIO 3.1.14.0 link step).
get_filename_component(_heif_fix_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_target_property(_heif_fix_type heif TYPE)
if(_heif_fix_type STREQUAL "STATIC_LIBRARY")
    set_property(TARGET heif APPEND PROPERTY INTERFACE_LINK_LIBRARIES
        "$<$<NOT:$<CONFIG:DEBUG>>:${_heif_fix_prefix}/lib/libfastfeat.a>"
        "$<$<CONFIG:DEBUG>:${_heif_fix_prefix}/debug/lib/libfastfeat.a>")
endif()
unset(_heif_fix_type)
unset(_heif_fix_prefix)
