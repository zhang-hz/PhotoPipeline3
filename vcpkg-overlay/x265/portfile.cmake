vcpkg_from_bitbucket(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO multicoreware/x265_git
    REF "${VERSION}"
    SHA512 53fe7b3a1c9f10bfad33f3d794b9ace87973993a5ebfabb2021b2679b1c9c44a0da9d33e9b75347571a14e6bb8224ed55d619d93c9d106d1637dcd28f99a3895
    HEAD_REF master
    PATCHES
        disable-install-pdb.patch
        version.patch
        linkage.diff
        pkgconfig.diff
        pthread.diff
        compiler-target.diff
        neon.diff
        advapi32.patch # Required since v4.2 as it is now using RegOpenKeyExA, RegQueryValueExA & RegCloseKey
)

vcpkg_check_features(OUT_FEATURE_OPTIONS OPTIONS
    FEATURES
        tool   ENABLE_CLI
)

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86" OR VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    vcpkg_find_acquire_program(NASM)
    list(APPEND OPTIONS "-DNASM_EXECUTABLE=${NASM}")
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "static" AND NOT VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_OSX)
        # x265 doesn't create sufficient PIC for asm, breaking usage
        # in shared libs, e.g. the libheif gdk pixbuf plugin.
        # Users can override this in custom triplets.
        list(APPEND OPTIONS "-DENABLE_ASSEMBLY=OFF")
    endif()
elseif(VCPKG_TARGET_IS_WINDOWS)
    list(APPEND OPTIONS "-DENABLE_ASSEMBLY=OFF")
endif()

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" ENABLE_SHARED)

# ===========================================================================
# SUB-CD overlay change (M1-T7b, R19): multilib build (8 + 10 + 12 bit)
#
# Upstream ships multi-bit-depth only as build/linux/multilib.sh; the pinned
# vcpkg port builds the default 8-bit-only library, so libheif's x265 plugin —
# which picks the codec API per bit depth with x265_api_get(bitDepth)
# (libheif/plugins/encoder_x265.cc:773; dispatch table in
# source/encoder/api.cpp:1129) — rejects 10-bit HEIF input with
# "Bit depth not supported by x265". Design §3.4 wants 10-bit HEIF by default.
#
# Recipe followed verbatim (upstream build/linux/multilib.sh):
#   12bit lib: -DHIGH_BIT_DEPTH=ON -DMAIN12=ON -DEXPORT_C_API=OFF
#              -DENABLE_SHARED=OFF -DENABLE_CLI=OFF
#   10bit lib: -DHIGH_BIT_DEPTH=ON              -DEXPORT_C_API=OFF
#              -DENABLE_SHARED=OFF -DENABLE_CLI=OFF
#    8bit lib: normal build + -DEXTRA_LIB="<10bit.a>;<12bit.a>"
#              -DLINKED_10BIT=ON -DLINKED_12BIT=ON
#              (EXTRA_LIB is what arms the glue in
#               source/encoder/CMakeLists.txt:14-23)
#   combine : ar -M  CREATE libx265.a  ADDLIB 8bit  ADDLIB 10bit  ADDLIB 12bit
# ===========================================================================
set(X265_MULTILIB ON)
if(NOT VCPKG_LIBRARY_LINKAGE STREQUAL "static" AND NOT VCPKG_TARGET_IS_WINDOWS)
    # Non-Windows shared builds: upstream multilib only covers static; the pinned
    # x64-linux triplet is static anyway (Linux behaviour unchanged).
    # Windows shared builds DO run multilib: upstream x265-shared links the depth
    # archives directly via EXTRA_LIB (source/CMakeLists.txt, x265-shared target),
    # producing a single libx265.dll with all depths.
    set(X265_MULTILIB OFF)
endif()
# Depth-archive output name per toolchain: MSVC keeps the target name
# (x265-static.lib, no `lib` prefix); GCC produces libx265.a (OUTPUT_NAME x265).
if(VCPKG_TARGET_IS_WINDOWS)
    set(X265_DEPTH_LIB "x265-static.lib")
else()
    set(X265_DEPTH_LIB "libx265.a")
endif()

set(X265_LINKED_OPTIONS_RELEASE "")
set(X265_LINKED_OPTIONS_DEBUG "")
if(X265_MULTILIB)
    if(NOT DEFINED Z_VCPKG_CMAKE_GENERATOR)
        set(Z_VCPKG_CMAKE_GENERATOR "Ninja")
    endif()
    foreach(_x265_cfg IN ITEMS release debug)
        if(DEFINED VCPKG_BUILD_TYPE AND NOT "${VCPKG_BUILD_TYPE}" STREQUAL "${_x265_cfg}")
            continue()
        endif()
        if(_x265_cfg STREQUAL "debug")
            set(_x265_short "dbg")
            set(_x265_build_type "Debug")
        else()
            set(_x265_short "rel")
            set(_x265_build_type "Release")
        endif()
        foreach(_x265_depth IN ITEMS 10 12)
            set(_x265_main12 OFF)
            if(_x265_depth EQUAL 12)
                set(_x265_main12 ON)
            endif()
            set(_x265_extra_dir
                "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${_x265_short}-${_x265_depth}bit")
            vcpkg_execute_required_process(
                COMMAND "${CMAKE_COMMAND}"
                    -S "${SOURCE_PATH}/source"
                    -B "${_x265_extra_dir}"
                    -G "${Z_VCPKG_CMAKE_GENERATOR}"
                    "-DCMAKE_BUILD_TYPE=${_x265_build_type}"
                    -DHIGH_BIT_DEPTH=ON
                    "-DMAIN12=${_x265_main12}"
                    -DEXPORT_C_API=OFF
                    -DENABLE_SHARED=OFF
                    -DENABLE_CLI=OFF
                    -DENABLE_ASSEMBLY=OFF
                    -DENABLE_PIC=ON
                    -DENABLE_LIBNUMA=OFF
                    "-DVERSION=${VERSION}"
                WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}"
                LOGNAME "configure-${TARGET_TRIPLET}-${_x265_short}-${_x265_depth}bit"
                TIMEOUT 1800
            )
            vcpkg_execute_required_process(
                COMMAND "${CMAKE_COMMAND}" --build "${_x265_extra_dir}" --target x265-static
                WORKING_DIRECTORY "${CURRENT_BUILDTREES_DIR}"
                LOGNAME "build-${TARGET_TRIPLET}-${_x265_short}-${_x265_depth}bit"
                TIMEOUT 1800
            )
        endforeach()
    endforeach()
    # The 8-bit build needs both extra archives on -DEXTRA_LIB so that
    # source/encoder/CMakeLists.txt:14-23 compiles the multi-depth dispatch glue.
    # M3 v1.2: EXTRA_LIB 的 `;` 必须写成 `\;` —— OPTIONS 经 execute_process 展开时，
    # 裸 `;` 会把值拆成两个 argv（12bit 档丢失 → LNK2019，x64-windows 实证）；
    # `\;` 使整串保持单 argv，cmake -D 收到含 `;` 的单值后存为两条目列表
    # （与上游 build/linux/multilib.sh 的 shell 引号语义一致）。
    if(NOT DEFINED VCPKG_BUILD_TYPE OR "${VCPKG_BUILD_TYPE}" STREQUAL "release")
        list(APPEND X265_LINKED_OPTIONS_RELEASE
            "-DEXTRA_LIB=${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel-10bit/${X265_DEPTH_LIB}\\;${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel-12bit/${X265_DEPTH_LIB}"
            -DLINKED_10BIT=ON
            -DLINKED_12BIT=ON
        )
    endif()
    if(NOT DEFINED VCPKG_BUILD_TYPE OR "${VCPKG_BUILD_TYPE}" STREQUAL "debug")
        list(APPEND X265_LINKED_OPTIONS_DEBUG
            "-DEXTRA_LIB=${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg-10bit/${X265_DEPTH_LIB}\\;${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg-12bit/${X265_DEPTH_LIB}"
            -DLINKED_10BIT=ON
            -DLINKED_12BIT=ON
        )
    endif()
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/source"
    OPTIONS
        ${OPTIONS}
        -DENABLE_SHARED=${ENABLE_SHARED}
        -DENABLE_PIC=ON
        -DENABLE_LIBNUMA=OFF
        "-DVERSION=${VERSION}"
    OPTIONS_RELEASE
        ${X265_LINKED_OPTIONS_RELEASE}
    OPTIONS_DEBUG
        -DENABLE_CLI=OFF
        ${X265_LINKED_OPTIONS_DEBUG}
    MAYBE_UNUSED_VARIABLES
        ENABLE_LIBNUMA
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

# Combine the per-depth archives into the single installed libx265.a
# (last step of upstream build/linux/multilib.sh). The 8-bit archive carries the
# dispatch glue, the 10/12-bit archives carry the namespaced codec APIs
# (x265_10bit::x265_api_get / x265_12bit::x265_api_get).
# M3 (Windows dynamic): the shared 8-bit build links the depth archives directly
# via EXTRA_LIB at DLL link time, so the GNU-ar MRI merge below only applies to
# static installs (Linux); Windows dynamic needs no archive merge.
if(X265_MULTILIB AND VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    find_program(X265_AR NAMES ar REQUIRED)
    foreach(_x265_cfg IN ITEMS release debug)
        if(DEFINED VCPKG_BUILD_TYPE AND NOT "${VCPKG_BUILD_TYPE}" STREQUAL "${_x265_cfg}")
            continue()
        endif()
        if(_x265_cfg STREQUAL "debug")
            set(_x265_short "dbg")
            set(_x265_installed "${CURRENT_PACKAGES_DIR}/debug/lib/libx265.a")
        else()
            set(_x265_short "rel")
            set(_x265_installed "${CURRENT_PACKAGES_DIR}/lib/libx265.a")
        endif()
        if(NOT EXISTS "${_x265_installed}")
            continue()
        endif()
        set(_x265_8bit "${CURRENT_BUILDTREES_DIR}/x265-8bit-${_x265_short}.a")
        file(RENAME "${_x265_installed}" "${_x265_8bit}")
        set(_x265_mri "${CURRENT_BUILDTREES_DIR}/x265-multilib-${_x265_short}.mri")
        file(WRITE "${_x265_mri}"
            "CREATE ${_x265_installed}\n"
            "ADDLIB ${_x265_8bit}\n"
            "ADDLIB ${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${_x265_short}-10bit/libx265.a\n"
            "ADDLIB ${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-${_x265_short}-12bit/libx265.a\n"
            "SAVE\n"
            "END\n"
        )
        execute_process(
            COMMAND "${X265_AR}" -M INPUT_FILE "${_x265_mri}"
            RESULT_VARIABLE _x265_ar_result
            OUTPUT_VARIABLE _x265_ar_stdout
            ERROR_VARIABLE _x265_ar_stderr
        )
        if(NOT _x265_ar_result EQUAL 0)
            message(FATAL_ERROR
                "x265 multilib archive merge failed (${_x265_ar_result}): "
                "${_x265_ar_stdout} ${_x265_ar_stderr}")
        endif()
        execute_process(
            COMMAND "${X265_AR}" s "${_x265_installed}"
            RESULT_VARIABLE _x265_ranlib_result
            ERROR_VARIABLE _x265_ranlib_stderr
        )
        if(NOT _x265_ranlib_result EQUAL 0)
            message(FATAL_ERROR
                "x265 multilib archive index failed (${_x265_ranlib_result}): "
                "${_x265_ranlib_stderr}")
        endif()
    endforeach()
endif()

if("tool" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES x265 AUTO_CLEAN)
endif()

if(VCPKG_TARGET_IS_WINDOWS AND VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/x265.h" "#ifdef X265_API_IMPORTS" "#if 1")
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
