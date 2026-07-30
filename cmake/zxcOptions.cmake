# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Build options.

if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()

option(BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)
option(ZXC_NATIVE_ARCH "Enable -march=native for maximum performance" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_ENABLE_LTO "Enable Interprocedural Optimization (LTO)" ${PROJECT_IS_TOP_LEVEL})
set(ZXC_PGO_MODE "OFF" CACHE STRING "Profile-Guided Optimization mode (OFF/GENERATE/USE)")
set_property(CACHE ZXC_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
option(ZXC_BUILD_CLI "Build the command-line interface" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_INSTALL "Generate install rules (headers, pkg-config, CMake package)"
       ${PROJECT_IS_TOP_LEVEL})
option(ZXC_ENABLE_COVERAGE "Enable code coverage generation" OFF)
option(ZXC_DISABLE_SIMD "Disable explicit SIMD intrinsics (no AVX/NEON code paths)" OFF)
option(ZXC_USE_SYSTEM_RAPIDHASH "Use a system-installed rapidhash.h instead of the vendored copy"
       OFF)
