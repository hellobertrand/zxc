# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Function Multi-Versioning: per-ISA variant objects for the runtime dispatcher.
# Populates ZXC_VARIANT_OBJECTS, consumed by the zxc_lib target.

# Function Multi-Versioning Helper
# Compiles compress/decompress/huffman with specific flags and suffix so the
# runtime dispatcher can route to a BMI2/AVX2/AVX512/NEON-aware Huffman codec
# in addition to the LZ77 stages.
macro(zxc_add_variant suffix flags)
    foreach(_src compress decompress huffman)
        add_library(zxc_${_src}${suffix} OBJECT src/lib/zxc_${_src}.c)
        # Common flags first: the ISA flags below must have the last word, since
        # a later -march= would reset the feature set they select.
        zxc_apply_common_flags(zxc_${_src}${suffix})
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
zxc_add_variant(_default "")

# --- 2. Architecture Specific Variants (skipped in no-intrinsics mode) ---
if(ZXC_DISABLE_SIMD)
    message(STATUS "ZXC_DISABLE_SIMD: Skipping SIMD variants (no explicit AVX/NEON code paths).")
else()
# Driven by the target architectures, not the host: a universal build enables
# several of these branches at once, so they are independent ifs.
if(ZXC_TARGET_X86)
    message(STATUS "Building x86_64 AVX2 and AVX512 variants...")
    # No _sse2 variant: SSE2 is the x86-64 baseline, so _default already
    # compiles the SSE2 code paths (ZXC_USE_SSE2 in zxc_internal.h).
    if(MSVC)
        # AVX2 for MSVC (Enables AVX2/BMI1/BMI2 sets)
        zxc_add_variant(_avx2 "/arch:AVX2;/D__BMI__;/D__BMI2__;/D__LZCNT__")
        # AVX512 for MSVC (VS2019 16.10+ supports /arch:AVX512)
        zxc_add_variant(_avx512 "/arch:AVX512;/D__BMI__;/D__BMI2__;/D__LZCNT__")
    else()
        set(ZXC_AVX2_FLAGS   -mavx2 -mbmi -mbmi2 -mlzcnt)
        set(ZXC_AVX512_FLAGS -mavx512f -mavx512bw -mavx512vbmi -mavx512vbmi2 -mbmi -mbmi2 -mlzcnt)
        if(APPLE AND ZXC_TARGET_AARCH64)
            # Universal build: every variant is compiled for every slice, so the
            # x86 flags must be confined to the x86 one. -Xarch_<arch> covers the
            # single argument that follows it, hence one prefix per flag.
            list(TRANSFORM ZXC_AVX2_FLAGS   PREPEND "-Xarch_x86_64;")
            list(TRANSFORM ZXC_AVX512_FLAGS PREPEND "-Xarch_x86_64;")
        endif()
        zxc_add_variant(_avx2   "${ZXC_AVX2_FLAGS}")
        zxc_add_variant(_avx512 "${ZXC_AVX512_FLAGS}")
    endif()
endif()

if(ZXC_TARGET_AARCH64)
    message(STATUS "AArch64: NEON is baseline; no dedicated SIMD variant needed.")
endif()

if(ZXC_TARGET_ARM32)
    message(STATUS "Building ARMv7 NEON32 variant...")
    zxc_add_variant(_neon32 "-march=armv7-a;-mfpu=neon")
endif()
endif()
