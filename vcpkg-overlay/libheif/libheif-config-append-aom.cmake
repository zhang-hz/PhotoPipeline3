# vcpkg overlay fix (PhotoPipeline M0), appended to share/libheif/libheif-config.cmake:
# the exported static "heif" target's link interface references AOM::aom, but the
# upstream-generated config never calls find_dependency(AOM). Consumers doing
# find_package(libheif CONFIG) therefore fail at generate time with
#   The link interface of target "heif" contains: AOM::aom but the target was not found.
include(CMakeFindDependencyMacro)
find_dependency(AOM)
