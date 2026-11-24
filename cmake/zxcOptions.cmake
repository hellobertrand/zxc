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
# SIMD variants to build (docs/INSTALL.md, "Trimming the library").
set(ZXC_BUILD_SIMD_VARIANTS "all" CACHE STRING "SIMD variants to build: all, decoder or minimal")
set_property(CACHE ZXC_BUILD_SIMD_VARIANTS PROPERTY STRINGS all decoder minimal)
option(ZXC_ENABLE_DICT "Support dictionary frames (compression and decompression)" ON)
option(ZXC_USE_SYSTEM_RAPIDHASH "Use a system-installed rapidhash.h instead of the vendored copy"
       OFF)
