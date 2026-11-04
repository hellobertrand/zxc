# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.

cmake_minimum_required(VERSION 3.20)
project(zxc_python C)

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)

# ---- Core ----
# An sdist (PKG-INFO) uses the copy it carries (scripts/vendor_core.py), a checkout its own.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/PKG-INFO)
    set(ZXC_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zxc-core)
else()
    set(ZXC_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
endif()
if(NOT EXISTS ${ZXC_CORE_DIR}/src/lib/zxc_common.c)
    message(FATAL_ERROR "ZXC core not found: sdist built without scripts/vendor_core.py?")
endif()
add_subdirectory(${ZXC_CORE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/zxc_core_build)

# ---- Python extension ----
Python_add_library(_zxc MODULE WITH_SOABI src/zxc/_zxc.c)

# Without "lib" prefix
set_target_properties(_zxc PROPERTIES PREFIX "")

target_include_directories(_zxc PRIVATE ${ZXC_CORE_DIR}/include)

target_link_libraries(_zxc PRIVATE zxc_lib)

install(TARGETS _zxc DESTINATION zxc)