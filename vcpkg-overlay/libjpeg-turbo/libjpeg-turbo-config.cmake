# Hand-written CONFIG package for the intercepted (jpegli-based) libjpeg-turbo.
# Mirrors the target name exported by the official libjpeg-turbo port
# (libjpeg-turbo::jpeg) which consumers such as libtiff's cmake/JPEGCodec.cmake
# look up via find_package(libjpeg-turbo CONFIG).
#
# M3: artifact names are platform-conditional — Linux: libjpeg.so / libjpegli.a /
# libhwy.a (static hwy under x64-linux); Windows/MSVC: static ABI wrapper
# jpeg.lib (upstream excludes the shared libjpeg from WIN32; overlay patch
# jpegli-win32-static-compat builds it static) / jpegli-static.lib / hwy.lib
# (import library, dynamic hwy under x64-windows). Discovery via find_library.
if(TARGET libjpeg-turbo::jpeg)
    set(libjpeg-turbo_FOUND TRUE)
    return()
endif()

get_filename_component(_ljt_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# ---------------------------------------------------------------------------
# Static jpegli C API. Only this archive exports the jpegli_* symbols: the
# libjpeg compatibility library hides them, so the static archive is the direct
# call layer. It uses highway, whose library must be on the final link line.
# ---------------------------------------------------------------------------
if(NOT TARGET libjpeg-turbo::jpegli-static)
    add_library(libjpeg-turbo::jpegli-static STATIC IMPORTED)
    set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_ljt_prefix}/include"
    )
    if(WIN32)
        find_library(_ljt_jpegli_release NAMES jpegli-static PATHS "${_ljt_prefix}/lib" NO_DEFAULT_PATH)
        find_library(_ljt_jpegli_debug   NAMES jpegli-static PATHS "${_ljt_prefix}/debug/lib" NO_DEFAULT_PATH)
        find_library(_ljt_hwy_release    NAMES hwy PATHS "${_ljt_prefix}/lib" NO_DEFAULT_PATH)
        find_library(_ljt_hwy_debug      NAMES hwy PATHS "${_ljt_prefix}/debug/lib" NO_DEFAULT_PATH)
    else()
        find_library(_ljt_jpegli_release NAMES jpegli PATHS "${_ljt_prefix}/lib" NO_DEFAULT_PATH)
        find_library(_ljt_jpegli_debug   NAMES jpegli PATHS "${_ljt_prefix}/debug/lib" NO_DEFAULT_PATH)
        find_library(_ljt_hwy_release    NAMES hwy PATHS "${_ljt_prefix}/lib" NO_DEFAULT_PATH)
        find_library(_ljt_hwy_debug      NAMES hwy PATHS "${_ljt_prefix}/debug/lib" NO_DEFAULT_PATH)
    endif()
    if(_ljt_jpegli_release AND _ljt_jpegli_debug)
        set_property(TARGET libjpeg-turbo::jpegli-static PROPERTY IMPORTED_CONFIGURATIONS "RELEASE;DEBUG")
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            IMPORTED_LOCATION_RELEASE "${_ljt_jpegli_release}"
            IMPORTED_LOCATION_DEBUG "${_ljt_jpegli_debug}"
        )
    elseif(_ljt_jpegli_release)
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            IMPORTED_LOCATION "${_ljt_jpegli_release}")
    elseif(_ljt_jpegli_debug)
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            IMPORTED_LOCATION "${_ljt_jpegli_debug}")
    else()
        message(FATAL_ERROR "libjpeg-turbo::jpegli-static: archive not found under ${_ljt_prefix}")
    endif()
    if(_ljt_hwy_release AND _ljt_hwy_debug)
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            INTERFACE_LINK_LIBRARIES
            "$<$<NOT:$<CONFIG:DEBUG>>:${_ljt_hwy_release}>;$<$<CONFIG:DEBUG>:${_ljt_hwy_debug}>")
    elseif(_ljt_hwy_release)
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_ljt_hwy_release}")
    elseif(_ljt_hwy_debug)
        set_target_properties(libjpeg-turbo::jpegli-static PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_ljt_hwy_debug}")
    endif()
    unset(_ljt_jpegli_release)
    unset(_ljt_jpegli_debug)
    unset(_ljt_hwy_release)
    unset(_ljt_hwy_debug)
endif()

# ---------------------------------------------------------------------------
# libjpeg-API/ABI compatible drop-in.
# Linux: shared libjpeg.so. Windows/MSVC (M3 v1.3): STATIC wrapper archive
# jpeg.lib; its INTERFACE pulls jpegli-static (+hwy) onto the consumer link.
# ---------------------------------------------------------------------------
if(WIN32)
    add_library(libjpeg-turbo::jpeg STATIC IMPORTED)
else()
    add_library(libjpeg-turbo::jpeg SHARED IMPORTED)
endif()
set_target_properties(libjpeg-turbo::jpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_ljt_prefix}/include"
)
if(WIN32)
    find_library(_ljt_wrapper_release NAMES jpeg PATHS "${_ljt_prefix}/lib" NO_DEFAULT_PATH)
    find_library(_ljt_wrapper_debug   NAMES jpeg PATHS "${_ljt_prefix}/debug/lib" NO_DEFAULT_PATH)
    if(NOT _ljt_wrapper_release AND NOT _ljt_wrapper_debug)
        message(FATAL_ERROR "libjpeg-turbo::jpeg: static wrapper archive jpeg.lib not found under ${_ljt_prefix} (overlay patch jpegli-win32-static-compat)")
    endif()
    if(_ljt_wrapper_release)
        set_target_properties(libjpeg-turbo::jpeg PROPERTIES IMPORTED_LOCATION "${_ljt_wrapper_release}")
    endif()
    if(_ljt_wrapper_debug)
        set_property(TARGET libjpeg-turbo::jpeg APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
        set_target_properties(libjpeg-turbo::jpeg PROPERTIES IMPORTED_LOCATION_DEBUG "${_ljt_wrapper_debug}")
    endif()
    set_target_properties(libjpeg-turbo::jpeg PROPERTIES
        INTERFACE_LINK_LIBRARIES libjpeg-turbo::jpegli-static)
    unset(_ljt_wrapper_release)
    unset(_ljt_wrapper_debug)
else()
    set(_ljt_release "${_ljt_prefix}/lib/libjpeg.so")
    set(_ljt_debug "${_ljt_prefix}/debug/lib/libjpeg.so")
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
endif()

unset(_ljt_prefix)

set(libjpeg-turbo_FOUND TRUE)
set(LIBJPEG_TURBO_FOUND TRUE)
