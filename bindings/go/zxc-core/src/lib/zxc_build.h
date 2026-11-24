// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file zxc_build.h
 * @brief Build configuration: what the ZXC_BUILD_SIMD_VARIANTS choice runs, derived once.
 *
 * The build defines ZXC_BUILD_SIMD_VARIANTS_DECODER or ZXC_BUILD_SIMD_VARIANTS_MINIMAL
 * (all: neither) and compiles the matching SIMD variants. Free of dependencies, so a
 * variant source can include it ahead of its suffix macros.
 */
#ifndef ZXC_BUILD_H
#define ZXC_BUILD_H

#if defined(ZXC_BUILD_SIMD_VARIANTS_DECODER) || defined(ZXC_BUILD_SIMD_VARIANTS_MINIMAL)
#define ZXC_SIMD_TRIMMED
#endif

// Nothing to dispatch: no SIMD, or trimmed variants on a baseline-only target.
#if !defined(ZXC_ONLY_DEFAULT) &&                                                       \
    (defined(ZXC_DISABLE_SIMD) || (defined(ZXC_SIMD_TRIMMED) && !defined(__x86_64__) && \
                                   !defined(_M_X64) && !defined(__arm__) && !defined(_M_ARM)))
#define ZXC_ONLY_DEFAULT
#endif

// Roles bound to the _default variant.
#if defined(ZXC_ONLY_DEFAULT) || defined(ZXC_SIMD_TRIMMED)
#define ZXC_ENCODER_DEFAULT
#endif
#if defined(ZXC_ONLY_DEFAULT) || defined(ZXC_BUILD_SIMD_VARIANTS_MINIMAL)
#define ZXC_LZ_DECODER_DEFAULT
#endif
#if defined(ZXC_ONLY_DEFAULT)
#define ZXC_ENTROPY_DECODER_DEFAULT
#endif

// minimal: the entropy decoder is dispatched on its own, the LZ decoder calling it.
#if defined(ZXC_BUILD_SIMD_VARIANTS_MINIMAL) && !defined(ZXC_ONLY_DEFAULT)
#define ZXC_DISPATCH_ENTROPY
#endif

// Trimmed variants include no AVX-512 one: AVX-512 CPUs run the AVX2 one.
#ifdef ZXC_SIMD_TRIMMED
#define ZXC_NO_AVX512
#endif

#endif  // ZXC_BUILD_H
