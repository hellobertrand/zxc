# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.
#
# C standard, LTO/PGO configuration and per-target flag helpers.

# =============================================================================
# C Standard
# =============================================================================
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# The code needs POSIX.1-2008 plus realpath, which musl gates behind
# _XOPEN_SOURCE. Linux stops short of _GNU_SOURCE: it makes glibc >= 2.38
# redirect strtol/strtoll to __isoc23_*, raising the ABI floor from 2.34 to
# 2.38. The BSDs keep it: _POSIX_C_SOURCE would clear __BSD_VISIBLE.
# _FILE_OFFSET_BITS=64 keeps off_t 64-bit on 32-bit Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_POSIX_C_SOURCE=200809L _XOPEN_SOURCE=700
                            _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(_GNU_SOURCE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
elseif(NOT MSVC)
    add_compile_definitions(_GNU_SOURCE)
endif()

# GCC on Win64 may spill AVX vectors with aligned moves onto a 16-byte-aligned
# stack (PR54412): unaligned moves instead (binutils 2.38+).
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND WIN32 AND ZXC_TARGET_X86)
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-Wa,-muse-unaligned-vector-move" ZXC_HAS_UNALIGNED_VECTOR_MOVE)
    if(ZXC_HAS_UNALIGNED_VECTOR_MOVE)
        add_compile_options(-Wa,-muse-unaligned-vector-move)
        add_link_options(-Wa,-muse-unaligned-vector-move)
    endif()
endif()

# Check for LTO support
if(ZXC_ENABLE_LTO AND NOT ZXC_ENABLE_COVERAGE)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result OUTPUT output)
    if(result)
        message(STATUS "LTO/IPO is supported and enabled.")
    else()
        message(WARNING "LTO/IPO is not supported: ${output}")
        set(ZXC_ENABLE_LTO OFF)
    endif()
elseif(ZXC_ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled: Disabling LTO and PGO.")
    set(ZXC_ENABLE_LTO OFF)
    set(ZXC_PGO_MODE "OFF")
endif()

# --- PGO flag selection (Clang vs GCC) ---
set(ZXC_PGO_DIR "${CMAKE_BINARY_DIR}/pgo")
set(ZXC_PGO_GEN_CFLAGS "")
set(ZXC_PGO_GEN_LDFLAGS "")
set(ZXC_PGO_USE_CFLAGS "")
set(ZXC_PGO_USE_LDFLAGS "")

if(NOT MSVC AND NOT ZXC_PGO_MODE STREQUAL "OFF")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Clang: instrumentation-based PGO
        set(ZXC_PGO_PROFDATA "${ZXC_PGO_DIR}/default.profdata")
        set(ZXC_PGO_GEN_CFLAGS  -fprofile-instr-generate=${ZXC_PGO_DIR}/default_%m.profraw)
        set(ZXC_PGO_GEN_LDFLAGS -fprofile-instr-generate)
        set(ZXC_PGO_USE_CFLAGS  -fprofile-instr-use=${ZXC_PGO_PROFDATA})
        set(ZXC_PGO_USE_LDFLAGS -fprofile-instr-use=${ZXC_PGO_PROFDATA})
    else()
        # GCC: directory-based PGO
        set(ZXC_PGO_GEN_CFLAGS  -fprofile-generate=${ZXC_PGO_DIR})
        set(ZXC_PGO_GEN_LDFLAGS -fprofile-generate=${ZXC_PGO_DIR})
        set(ZXC_PGO_USE_CFLAGS  -fprofile-use=${ZXC_PGO_DIR} -fprofile-correction)
        set(ZXC_PGO_USE_LDFLAGS -fprofile-use=${ZXC_PGO_DIR})
    endif()
endif()

# Helper: apply PGO flags to a target
macro(zxc_apply_pgo target)
    if(ZXC_PGO_MODE STREQUAL "GENERATE")
        target_compile_options(${target} PRIVATE ${ZXC_PGO_GEN_CFLAGS})
        target_link_options(${target} PRIVATE ${ZXC_PGO_GEN_LDFLAGS})
    elseif(ZXC_PGO_MODE STREQUAL "USE")
        if(EXISTS "${ZXC_PGO_DIR}")
            target_compile_options(${target} PRIVATE ${ZXC_PGO_USE_CFLAGS})
            target_link_options(${target} PRIVATE ${ZXC_PGO_USE_LDFLAGS})
        endif()
    endif()
endmacro()

# CI turns warnings into errors with CMAKE_COMPILE_WARNING_AS_ERROR, which older
# CMake ignores silently.
if(CMAKE_COMPILE_WARNING_AS_ERROR AND CMAKE_VERSION VERSION_LESS 3.24)
    message(WARNING "CMAKE_COMPILE_WARNING_AS_ERROR needs CMake 3.24+: ignored")
endif()

# Warnings for every zxc target, top-level only: an embedder sets its own.
set(ZXC_WARNING_FLAGS
    -Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wpointer-arith -Wvla -Wcast-qual
    -Wdouble-promotion -Wimplicit-fallthrough -Wmissing-prototypes -Wstrict-prototypes)

macro(zxc_apply_warnings target)
    if(MSVC)
        # /wd4244: block-bounded uint64->size_t narrowing, lossless.
        # /wd4310: constants truncated on purpose by a cast.
        target_compile_options(${target} PRIVATE /wd4244 /wd4310)
        if(PROJECT_IS_TOP_LEVEL)
            target_compile_options(${target} PRIVATE /W4)
        endif()
    elseif(PROJECT_IS_TOP_LEVEL)
        target_compile_options(${target} PRIVATE ${ZXC_WARNING_FLAGS})
    endif()
endmacro()

# Warnings and PGO, defined once so no target misses them.
macro(zxc_apply_common_flags target)
    zxc_apply_warnings(${target})
    zxc_apply_pgo(${target})
endmacro()

# Library sources also check conversions, on 64-bit targets only: on 32-bit ones
# every block-bounded uint64 -> size_t narrowing would warn. Not before GCC 10,
# which flags `u32 += sizeof(x)`.
macro(zxc_apply_lib_flags target)
    zxc_apply_common_flags(${target})
    if(PROJECT_IS_TOP_LEVEL AND NOT MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8
       AND NOT (CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_LESS 10))
        target_compile_options(${target} PRIVATE -Wconversion -Wsign-conversion)
    endif()
endmacro()
