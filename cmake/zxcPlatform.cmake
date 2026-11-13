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
set(ZXC_TARGET_LOONGARCH64 OFF)
foreach(_arch IN LISTS ZXC_TARGET_ARCHS)
    if(_arch MATCHES "amd64|x86_64|AMD64")
        set(ZXC_TARGET_X86 ON)
    elseif(_arch MATCHES "aarch64|arm64|ARM64")
        # Must be tested before "^arm", which "arm64" matches too.
        set(ZXC_TARGET_AARCH64 ON)
    elseif(_arch MATCHES "^arm")
        set(ZXC_TARGET_ARM32 ON)
    elseif(_arch MATCHES "loongarch64|loong64")
        set(ZXC_TARGET_LOONGARCH64 ON)
    endif()
endforeach()
# -m32, Win32 and MinGW32 still report x86_64/AMD64: probe as the dispatcher does.
if(ZXC_TARGET_X86 AND NOT (APPLE AND CMAKE_OSX_ARCHITECTURES))
    include(CheckCSourceCompiles)
    # Compile only, no link.
    set(_zxc_try_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    unset(ZXC_COMPILER_X86_64 CACHE)  # rerun: target flags may have changed
    check_c_source_compiles("
#if !defined(__x86_64__) && !defined(_M_X64)
#error not x86-64
#endif
int zxc_x86_64_probe;" ZXC_COMPILER_X86_64)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_zxc_try_type}")
    if(NOT ZXC_COMPILER_X86_64)
        set(ZXC_TARGET_X86 OFF)
    endif()
endif()

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
    set(ZXC_TARGET_X86     OFF)
    set(ZXC_TARGET_AARCH64 OFF)
    set(ZXC_TARGET_ARM32   OFF)
    add_compile_definitions(ZXC_DISABLE_SIMD)
    set(ZXC_BUILD_CLI    OFF CACHE BOOL "" FORCE)
    set(ZXC_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(ZXC_NATIVE_ARCH  OFF CACHE BOOL "" FORCE)
    set(ZXC_ENABLE_LTO   OFF CACHE BOOL "" FORCE)
    set(ZXC_PGO_MODE     "OFF" CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

# Removed after 1.0; the macro stays for embedders.
if(ZXC_DISABLE_SIMD)
    message(WARNING "ZXC_DISABLE_SIMD is no longer a build option and is ignored: "
                    "CMake builds no scalar-only library.")
endif()
unset(ZXC_DISABLE_SIMD CACHE)

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
# SIMD variants and optional features
# =============================================================================
if(NOT ZXC_BUILD_SIMD_VARIANTS MATCHES "^(all|decoder|minimal)$")
    message(FATAL_ERROR "ZXC_BUILD_SIMD_VARIANTS must be all, decoder or minimal, not '${ZXC_BUILD_SIMD_VARIANTS}'.")
endif()
# The sources read the choice (src/lib/zxc_build.h); all is the default.
if(ZXC_BUILD_SIMD_VARIANTS STREQUAL "decoder")
    add_compile_definitions(ZXC_BUILD_SIMD_VARIANTS_DECODER)
elseif(ZXC_BUILD_SIMD_VARIANTS STREQUAL "minimal")
    add_compile_definitions(ZXC_BUILD_SIMD_VARIANTS_MINIMAL)
endif()

if(NOT ZXC_ENABLE_DICT)
    add_compile_definitions(ZXC_NO_DICT)
endif()
