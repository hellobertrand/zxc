# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.
#
# Build options.

option(BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)
option(ZXC_NATIVE_ARCH "Enable -march=native for maximum performance" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_ENABLE_LTO "Enable Interprocedural Optimization (LTO)" OFF)
set(ZXC_PGO_MODE "OFF" CACHE STRING "Profile-Guided Optimization mode (OFF/GENERATE/USE)")
set_property(CACHE ZXC_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
option(ZXC_BUILD_CLI "Build the command-line interface" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_INSTALL "Generate install rules (headers, pkg-config, CMake package)"
       ${PROJECT_IS_TOP_LEVEL})
option(ZXC_ENABLE_COVERAGE "Enable code coverage generation" OFF)
option(ZXC_DISABLE_SIMD "Disable explicit SIMD intrinsics (no AVX/NEON code paths)" OFF)
# SIMD variants to build (docs/INSTALL.md, "Trimming the library").
set(ZXC_OPTIMIZE "perf" CACHE STRING "SIMD variants to build: perf, balanced or size")
set_property(CACHE ZXC_OPTIMIZE PROPERTY STRINGS perf balanced size)
option(ZXC_DICTIONARY "Support dictionary frames (compression and decompression)" ON)
option(ZXC_USE_SYSTEM_RAPIDHASH "Use a system-installed rapidhash.h instead of the vendored copy"
       OFF)
