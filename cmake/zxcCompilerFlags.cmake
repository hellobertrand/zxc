# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# C standard, LTO/PGO configuration and per-target flag helpers.

# =============================================================================
# C Standard
# =============================================================================
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable _GNU_SOURCE for ftello/fseeko on Linux
# Enable 64-bit off_t on 32-bit Linux to prevent fseeko/ftello/pread truncation
if(UNIX AND NOT APPLE)
    add_compile_definitions(_GNU_SOURCE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
elseif(APPLE)
    add_compile_definitions(_GNU_SOURCE)
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

# Warnings and PGO, for every zxc target. Defined once because each target used
# to repeat its own list, and the variant objects ended up with no warnings at
# all. Warning level is top-level only: an embedder sets its own.
macro(zxc_apply_common_flags target)
    if(MSVC)
        # /wd4244: block-bounded uint64->size_t narrowing, lossless.
        target_compile_options(${target} PRIVATE /wd4244)
        if(PROJECT_IS_TOP_LEVEL)
            target_compile_options(${target} PRIVATE /W3)
        endif()
    elseif(PROJECT_IS_TOP_LEVEL)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    zxc_apply_pgo(${target})
endmacro()
