# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Rapidhash: system-installed (e.g. vcpkg) or vendored fallback.
# Resolved at configure time so a missing header names the option to set.

if(ZXC_USE_SYSTEM_RAPIDHASH)
    find_path(RAPIDHASH_INCLUDE_DIR rapidhash.h)
    if(NOT RAPIDHASH_INCLUDE_DIR OR NOT EXISTS "${RAPIDHASH_INCLUDE_DIR}/rapidhash.h")
        message(FATAL_ERROR
            "ZXC_USE_SYSTEM_RAPIDHASH is ON but no rapidhash.h was found "
            "(RAPIDHASH_INCLUDE_DIR=${RAPIDHASH_INCLUDE_DIR}). Install rapidhash, "
            "add its prefix to CMAKE_PREFIX_PATH, or point "
            "-DRAPIDHASH_INCLUDE_DIR=<dir> at the directory holding the header.")
    endif()
    message(STATUS "Using system rapidhash from ${RAPIDHASH_INCLUDE_DIR}")
else()
    set(RAPIDHASH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/vendors")
    if(NOT EXISTS "${RAPIDHASH_INCLUDE_DIR}/rapidhash.h")
        message(FATAL_ERROR
            "Vendored rapidhash.h not found in ${RAPIDHASH_INCLUDE_DIR}. "
            "If the vendored copy was removed on purpose (packaging against a "
            "system or vcpkg rapidhash), configure with "
            "-DZXC_USE_SYSTEM_RAPIDHASH=ON.")
    endif()
    message(STATUS "Using vendored rapidhash from ${RAPIDHASH_INCLUDE_DIR}")
endif()
