# Hand-written CONFIG package for the intercepted (jpegli-based) libjpeg-turbo.
# Mirrors the target name exported by the official libjpeg-turbo port
# (libjpeg-turbo::jpeg) which consumers such as libtiff's cmake/JPEGCodec.cmake
# look up via find_package(libjpeg-turbo CONFIG).
if(TARGET libjpeg-turbo::jpeg)
    set(libjpeg-turbo_FOUND TRUE)
    return()
endif()

get_filename_component(_ljt_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(_ljt_release "${_ljt_prefix}/lib/libjpeg.so")
set(_ljt_debug "${_ljt_prefix}/debug/lib/libjpeg.so")

add_library(libjpeg-turbo::jpeg SHARED IMPORTED)
set_target_properties(libjpeg-turbo::jpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ljt_prefix}/include"
)
if(EXISTS "${_ljt_release}" AND EXISTS "${_ljt_debug}")
    set_property(TARGET libjpeg-turbo::jpeg PROPERTY IMPORTED_CONFIGURATIONS "RELEASE;DEBUG")
    set_target_properties(libjpeg-turbo::jpeg PROPERTIES
        IMPORTED_LOCATION_RELEASE "${_ljt_release}"
        IMPORTED_LOCATION_DEBUG "${_ljt_debug}"
    )
elseif(EXISTS "${_ljt_release}")
    set_target_properties(libjpeg-turbo::jpeg PROPERTIES IMPORTED_LOCATION "${_ljt_release}")
elseif(EXISTS "${_ljt_debug}")
    set_target_properties(libjpeg-turbo::jpeg PROPERTIES IMPORTED_LOCATION "${_ljt_debug}")
endif()

unset(_ljt_release)
unset(_ljt_debug)
unset(_ljt_prefix)

set(libjpeg-turbo_FOUND TRUE)
set(LIBJPEG_TURBO_FOUND TRUE)
