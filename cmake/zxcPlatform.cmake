# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Target architecture detection and Emscripten/WebAssembly overrides.

# =============================================================================
# Target architecture
# =============================================================================
set(ZXC_TARGET_ARCHS "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(ZXC_TARGET_ARCHS "${CMAKE_OSX_ARCHITECTURES}")
endif()

set(ZXC_TARGET_X86 OFF)
set(ZXC_TARGET_AARCH64 OFF)
set(ZXC_TARGET_ARM32 OFF)
foreach(_arch IN LISTS ZXC_TARGET_ARCHS)
    if(_arch MATCHES "amd64|x86_64|AMD64")
        set(ZXC_TARGET_X86 ON)
    elseif(_arch MATCHES "aarch64|arm64|ARM64")
        # Must be tested before "^arm", which "arm64" matches too.
        set(ZXC_TARGET_AARCH64 ON)
    elseif(_arch MATCHES "^arm")
        set(ZXC_TARGET_ARM32 ON)
    endif()
endforeach()

if(ZXC_NATIVE_ARCH AND (CMAKE_CROSSCOMPILING OR
                        NOT "${ZXC_TARGET_ARCHS}" STREQUAL "${CMAKE_SYSTEM_PROCESSOR}"))
    message(STATUS "zxc: target (${ZXC_TARGET_ARCHS}) is not the build host "
                   "(${CMAKE_SYSTEM_PROCESSOR}) - ignoring ZXC_NATIVE_ARCH.")
    set(ZXC_NATIVE_ARCH OFF)
endif()

# =============================================================================
# Emscripten / WebAssembly overrides
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(STATUS "Emscripten detected - configuring for WebAssembly.")
    set(ZXC_DISABLE_SIMD ON  CACHE BOOL "" FORCE)
    set(ZXC_BUILD_CLI    OFF CACHE BOOL "" FORCE)
    set(ZXC_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(ZXC_NATIVE_ARCH  OFF CACHE BOOL "" FORCE)
    set(ZXC_ENABLE_LTO   OFF CACHE BOOL "" FORCE)
    set(ZXC_PGO_MODE     "OFF" CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

if(ZXC_DISABLE_SIMD)
    add_compile_definitions(ZXC_DISABLE_SIMD)
endif()
