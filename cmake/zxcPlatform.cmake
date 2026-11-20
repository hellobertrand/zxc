# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.
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

# =============================================================================
# Native tuning
# =============================================================================
# Probed: GCC rejects -march=native on RISC-V and POWER.
set(ZXC_NATIVE_FLAGS "")
if(ZXC_NATIVE_ARCH AND NOT MSVC)
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-march=native" ZXC_HAS_MARCH_NATIVE)
    if(ZXC_HAS_MARCH_NATIVE)
        set(ZXC_NATIVE_FLAGS -march=native)
    else()
        message(STATUS "zxc: compiler rejects -march=native - ignoring ZXC_NATIVE_ARCH.")
        set(ZXC_NATIVE_ARCH OFF)
    endif()
endif()

# =============================================================================
# Per-role SIMD variants and optional features
# =============================================================================
if(NOT ZXC_ENCODER MATCHES "^(runtime|default)$")
    message(FATAL_ERROR "ZXC_ENCODER must be runtime or default, not '${ZXC_ENCODER}'.")
endif()
if(NOT ZXC_DECODER MATCHES "^(runtime|entropy|default)$")
    message(FATAL_ERROR
        "ZXC_DECODER must be runtime, entropy or default, not '${ZXC_DECODER}'.")
endif()

# ZXC_DISABLE_SIMD leaves nothing to select.
set(ZXC_ENCODER_EFFECTIVE "${ZXC_ENCODER}")
set(ZXC_DECODER_EFFECTIVE "${ZXC_DECODER}")
if(ZXC_DISABLE_SIMD)
    set(ZXC_ENCODER_EFFECTIVE "default")
    set(ZXC_DECODER_EFFECTIVE "default")
endif()

if(ZXC_ENCODER_EFFECTIVE STREQUAL "default")
    add_compile_definitions(ZXC_ENCODER_ONLY_DEFAULT)
endif()
if(ZXC_DECODER_EFFECTIVE STREQUAL "default")
    add_compile_definitions(ZXC_DECODER_ONLY_DEFAULT)
elseif(ZXC_DECODER_EFFECTIVE STREQUAL "entropy")
    add_compile_definitions(ZXC_DECODER_ENTROPY_DISPATCH)
endif()

if(NOT ZXC_DICTIONARY)
    add_compile_definitions(ZXC_NO_DICT)
endif()
