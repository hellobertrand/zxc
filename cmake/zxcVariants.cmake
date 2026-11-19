# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.
#
# Function Multi-Versioning: per-ISA variant objects for the runtime dispatcher.
# Populates ZXC_VARIANT_OBJECTS, consumed by the zxc_lib target.

# Function Multi-Versioning Helper
# Compiles the listed sources with specific flags and suffix, for the runtime
# dispatcher.
macro(zxc_add_variant suffix flags sources)
    foreach(_src ${sources})
        add_library(zxc_${_src}${suffix} OBJECT src/lib/zxc_${_src}.c)
        # Common flags first: the ISA flags below must have the last word, since
        # a later -march= would reset the feature set they select.
        zxc_apply_lib_flags(zxc_${_src}${suffix})
        target_compile_options(zxc_${_src}${suffix} PRIVATE ${flags})
        target_compile_definitions(zxc_${_src}${suffix} PRIVATE ZXC_FUNCTION_SUFFIX=${suffix})
        # For static builds, define ZXC_STATIC_DEFINE
        if(NOT BUILD_SHARED_LIBS)
            target_compile_definitions(zxc_${_src}${suffix} PRIVATE ZXC_STATIC_DEFINE)
        else()
            # Mark as part of the DLL being built (avoids dllimport on internal symbols)
            target_compile_definitions(zxc_${_src}${suffix} PRIVATE zxc_lib_EXPORTS)
            set_target_properties(zxc_${_src}${suffix} PROPERTIES POSITION_INDEPENDENT_CODE ON)
            # Hide variant symbols from shared library public ABI
            if(NOT MSVC)
                target_compile_options(zxc_${_src}${suffix} PRIVATE -fvisibility=hidden)
            endif()
        endif()
        # Inherit include directories
        target_include_directories(zxc_${_src}${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/lib ${RAPIDHASH_INCLUDE_DIR} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

        list(APPEND ZXC_VARIANT_OBJECTS $<TARGET_OBJECTS:zxc_${_src}${suffix}>)
    endforeach()
endmacro()

set(ZXC_VARIANT_OBJECTS "")

# --- 1. Default Variant (Scalar/Baseline) ---
zxc_add_variant(_default "" "compress;decompress;huffman")

# SIMD variant sources (src/lib/zxc_build.h); AVX-512 with "all" only.
if(ZXC_SIMD_VARIANTS STREQUAL "all")
    set(ZXC_SIMD_SOURCES compress decompress huffman)
elseif(ZXC_SIMD_VARIANTS STREQUAL "decoder")
    set(ZXC_SIMD_SOURCES decompress huffman)
else()
    set(ZXC_SIMD_SOURCES huffman)
endif()

# --- 2. Architecture Specific Variants (skipped in no-intrinsics mode) ---
if(ZXC_DISABLE_SIMD)
    message(STATUS "ZXC_DISABLE_SIMD: Skipping SIMD variants (no explicit AVX/NEON code paths).")
else()
# Driven by the target architectures, not the host: a universal build enables
# several of these branches at once, so they are independent ifs.
if(ZXC_TARGET_X86)
    if(ZXC_SIMD_VARIANTS STREQUAL "all")
        message(STATUS "Building x86_64 AVX2 and AVX512 variants...")
    else()
        message(STATUS "Building x86_64 AVX2 variant...")
    endif()
    # No _sse2 variant: SSE2 is the x86-64 baseline, so _default already
    # compiles the SSE2 code paths (ZXC_USE_SSE2 in zxc_internal.h).
    if(MSVC)
        # AVX2 for MSVC (Enables AVX2/BMI1/BMI2 sets)
        zxc_add_variant(_avx2 "/arch:AVX2;/D__BMI__;/D__BMI2__;/D__LZCNT__" "${ZXC_SIMD_SOURCES}")
        # AVX512 for MSVC (VS2019 16.10+ supports /arch:AVX512)
        # /arch:AVX512 does not cover VBMI/VBMI2.
        set(ZXC_MSVC_AVX512_FLAGS "/arch:AVX512;/D__BMI__;/D__BMI2__;/D__LZCNT__")
        if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
            list(APPEND ZXC_MSVC_AVX512_FLAGS /D__AVX512VBMI__ /D__AVX512VBMI2__)
        else()
            list(APPEND ZXC_MSVC_AVX512_FLAGS -mavx512vbmi -mavx512vbmi2)
        endif()
        if(ZXC_SIMD_VARIANTS STREQUAL "all")
            zxc_add_variant(_avx512 "${ZXC_MSVC_AVX512_FLAGS}" "${ZXC_SIMD_SOURCES}")
        endif()
    else()
        set(ZXC_AVX2_FLAGS   -mavx2 -mbmi -mbmi2 -mlzcnt)
        set(ZXC_AVX512_FLAGS -mavx512f -mavx512bw -mavx512vl -mavx512vbmi -mavx512vbmi2 -mbmi -mbmi2 -mlzcnt)
        if(APPLE AND ZXC_TARGET_AARCH64)
            # Universal build: every variant is compiled for every slice, so the
            # x86 flags must be confined to the x86 one. -Xarch_<arch> covers the
            # single argument that follows it, hence one prefix per flag.
            list(TRANSFORM ZXC_AVX2_FLAGS   PREPEND "-Xarch_x86_64;")
            list(TRANSFORM ZXC_AVX512_FLAGS PREPEND "-Xarch_x86_64;")
        endif()
        zxc_add_variant(_avx2   "${ZXC_AVX2_FLAGS}"   "${ZXC_SIMD_SOURCES}")
        if(ZXC_SIMD_VARIANTS STREQUAL "all")
            zxc_add_variant(_avx512 "${ZXC_AVX512_FLAGS}" "${ZXC_SIMD_SOURCES}")
        endif()
    endif()
endif()

if(ZXC_TARGET_AARCH64)
    message(STATUS "AArch64: NEON is baseline; no dedicated SIMD variant needed.")
endif()

if(ZXC_TARGET_ARM32)
    message(STATUS "Building ARMv7 NEON32 variant...")
    zxc_add_variant(_neon32 "-march=armv7-a;-mfpu=neon" "${ZXC_SIMD_SOURCES}")
endif()
endif()

# --- 3. RISC-V 64 (Linux): fast misaligned scalar access ---
if(ZXC_TARGET_RISCV64 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCCompilerFlag)
    set(_zxc_saved_try_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    set(ZXC_RV64FAST_FLAGS "")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        check_c_compiler_flag("-mtune=generic-ooo" ZXC_HAS_RV_TUNE_GENERIC_OOO)
        if(ZXC_HAS_RV_TUNE_GENERIC_OOO)
            set(ZXC_RV64FAST_FLAGS -mtune=generic-ooo)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        check_c_compiler_flag("-mno-strict-align" ZXC_HAS_RV_NO_STRICT_ALIGN)
        if(ZXC_HAS_RV_NO_STRICT_ALIGN)
            set(ZXC_RV64FAST_FLAGS -mno-strict-align)
        endif()
    endif()
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_zxc_saved_try_type}")
    unset(_zxc_saved_try_type)

    if(ZXC_RV64FAST_FLAGS)
        message(STATUS "Building RISC-V 64 fast-unaligned variant (${ZXC_RV64FAST_FLAGS})...")
        zxc_add_variant(_rv64fast "${ZXC_RV64FAST_FLAGS}" "compress;decompress;huffman")
        add_compile_definitions(ZXC_HAVE_RV64FAST_VARIANT)
    else()
        message(STATUS "RISC-V 64: no fast-unaligned flag; _default only.")
    endif()
endif()
