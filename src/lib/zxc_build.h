// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file zxc_build.h
 * @brief Build configuration: what the ZXC_OPTIMIZE preset runs, derived once.
 *
 * The build defines ZXC_OPTIMIZE_BALANCED or ZXC_OPTIMIZE_SIZE (perf: neither)
 * and compiles the matching SIMD variants. Free of dependencies, so a variant
 * source can include it ahead of its suffix macros.
 */
#ifndef ZXC_BUILD_H
#define ZXC_BUILD_H

#if defined(ZXC_OPTIMIZE_BALANCED) || defined(ZXC_OPTIMIZE_SIZE)
#define ZXC_OPTIMIZE_TRIMMED
#endif

// Nothing to dispatch: no SIMD, or a trimmed preset on a baseline-only target.
#if !defined(ZXC_ONLY_DEFAULT) &&                                                           \
    (defined(ZXC_DISABLE_SIMD) || (defined(ZXC_OPTIMIZE_TRIMMED) && !defined(__x86_64__) && \
                                   !defined(_M_X64) && !defined(__arm__) && !defined(_M_ARM)))
#define ZXC_ONLY_DEFAULT
#endif

// Roles bound to the _default variant.
#if defined(ZXC_ONLY_DEFAULT) || defined(ZXC_OPTIMIZE_TRIMMED)
#define ZXC_ENCODER_DEFAULT
#endif
#if defined(ZXC_ONLY_DEFAULT) || defined(ZXC_OPTIMIZE_SIZE)
#define ZXC_LZ_DECODER_DEFAULT
#endif
#if defined(ZXC_ONLY_DEFAULT)
#define ZXC_ENTROPY_DECODER_DEFAULT
#endif

// size: the entropy decoder is dispatched on its own, the LZ decoder calling it.
#if defined(ZXC_OPTIMIZE_SIZE) && !defined(ZXC_ONLY_DEFAULT)
#define ZXC_DISPATCH_ENTROPY
#endif

// Trimmed presets build no AVX-512 variant: AVX-512 CPUs run the AVX2 one.
#ifdef ZXC_OPTIMIZE_TRIMMED
#define ZXC_NO_AVX512
#endif

#endif  // ZXC_BUILD_H
