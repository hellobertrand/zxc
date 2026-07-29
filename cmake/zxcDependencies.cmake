# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Rapidhash: system-installed (e.g. vcpkg) or vendored fallback.

if(ZXC_USE_SYSTEM_RAPIDHASH)
    find_path(RAPIDHASH_INCLUDE_DIR rapidhash.h)
    if(NOT RAPIDHASH_INCLUDE_DIR)
        message(FATAL_ERROR "ZXC_USE_SYSTEM_RAPIDHASH is ON but rapidhash.h was not found.")
    endif()
    message(STATUS "Using system rapidhash from ${RAPIDHASH_INCLUDE_DIR}")
else()
    set(RAPIDHASH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/vendors")
    message(STATUS "Using vendored rapidhash from ${RAPIDHASH_INCLUDE_DIR}")
endif()
