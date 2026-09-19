# Hand-written CONFIG package for the intercepted (jpegli-based) libjpeg-turbo.
# Mirrors the target name exported by the official libjpeg-turbo port
# (libjpeg-turbo::jpeg) which consumers such as libtiff's cmake/JPEGCodec.cmake
# look up via find_package(libjpeg-turbo CONFIG).
if(TARGET libjpeg-turbo::jpeg)
    set(libjpeg-turbo_FOUND TRUE)
    return()
endif()

get_filename_component(_ljt_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# ---------------------------------------------------------------------------
# Shared library: libjpeg-API/ABI compatible drop-in (libjpeg.so.62).
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Static jpegli C API (lib/libjpegli.a, installed by this overlay port).
# Only this archive exports the jpegli_* symbols: libjpeg.so links jpegli-static
# with -Wl,--exclude-libs=ALL, so its jpegli_* symbols are LOCAL. The static
# library uses highway, whose archive must be on the final link line as well.
# ---------------------------------------------------------------------------
if(NOT TARGET libjpeg-turbo::jpegli-static)
    add_library(libjpeg-turbo::jpegli-static STATIC IMPORTED)
    set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_ljt_prefix}/include"
    )

    set(_ljt_jpegli_release "${_ljt_prefix}/lib/libjpegli.a")
    set(_ljt_jpegli_debug "${_ljt_prefix}/debug/lib/libjpegli.a")
    if(EXISTS "${_ljt_jpegli_release}" AND EXISTS "${_ljt_jpegli_debug}")
        set_property(TARGET libjpeg-turbo::jpegli-static PROPERTY IMPORTED_CONFIGURATIONS "RELEASE;DEBUG")
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            IMPORTED_LOCATION_RELEASE "${_ljt_jpegli_release}"
            IMPORTED_LOCATION_DEBUG "${_ljt_jpegli_debug}"
        )
    elseif(EXISTS "${_ljt_jpegli_release}")
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            IMPORTED_LOCATION "${_ljt_jpegli_release}")
    elseif(EXISTS "${_ljt_jpegli_debug}")
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            IMPORTED_LOCATION "${_ljt_jpegli_debug}")
    endif()

    set(_ljt_hwy_release "${_ljt_prefix}/lib/libhwy.a")
    set(_ljt_hwy_debug "${_ljt_prefix}/debug/lib/libhwy.a")
    if(EXISTS "${_ljt_hwy_release}" AND EXISTS "${_ljt_hwy_debug}")
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            INTERFACE_LINK_LIBRARIES
            "$<$<NOT:$<CONFIG:DEBUG>>:${_ljt_hwy_release}>;$<$<CONFIG:DEBUG>:${_ljt_hwy_debug}>")
    elseif(EXISTS "${_ljt_hwy_release}")
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_ljt_hwy_release}")
    elseif(EXISTS "${_ljt_hwy_debug}")
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_ljt_hwy_debug}")
    endif()

    unset(_ljt_jpegli_release)
    unset(_ljt_jpegli_debug)
    unset(_ljt_hwy_release)
    unset(_ljt_hwy_debug)
endif()

unset(_ljt_release)
unset(_ljt_debug)
unset(_ljt_prefix)

set(libjpeg-turbo_FOUND TRUE)
set(LIBJPEG_TURBO_FOUND TRUE)
