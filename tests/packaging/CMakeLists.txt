# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Standalone downstream project used by .github/workflows/packaging.yml, never
# built as part of ZXC: it is configured against an install prefix.
#
#   ZXC_CONSUME_MODE=find_package  usage requirements from zxc-targets.cmake
#   ZXC_CONSUME_MODE=direct        include dir + library path, no defines

cmake_minimum_required(VERSION 3.16)
project(zxc_packaging_consumer C)

set(ZXC_CONSUME_MODE "find_package" CACHE STRING "How to consume ZXC (find_package|direct)")

add_executable(consumer consumer.c)

if(ZXC_CONSUME_MODE STREQUAL "find_package")
    find_package(zxc REQUIRED)
    target_link_libraries(consumer PRIVATE zxc::zxc_lib)
elseif(ZXC_CONSUME_MODE STREQUAL "direct")
    if(NOT ZXC_INCLUDE_DIR OR NOT ZXC_LIBRARY)
        message(FATAL_ERROR "direct mode needs -DZXC_INCLUDE_DIR=... -DZXC_LIBRARY=...")
    endif()
    target_include_directories(consumer PRIVATE ${ZXC_INCLUDE_DIR})
    target_link_libraries(consumer PRIVATE ${ZXC_LIBRARY})
    if(NOT WIN32)
        find_package(Threads REQUIRED)
        target_link_libraries(consumer PRIVATE Threads::Threads)
    endif()
else()
    message(FATAL_ERROR "unknown ZXC_CONSUME_MODE '${ZXC_CONSUME_MODE}'")
endif()
