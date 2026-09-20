/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_deps.h
 * @brief Single point of override for the C-library dependencies of libzxc.
 *
 * The core compression/decompression code only ever reaches the standard
 * library through the macros and headers declared here.
 * Freestanding consumers vendor the source tree and replace this file with
 * an environment-specific version that maps the same macros onto their own
 * allocator, sort, and basic header set.
 *
 * @par Stock (hosted) build
 * Pulls in @c <limits.h>, @c <stdint.h>, @c <stdlib.h>, @c <string.h> and
 * expands the macros to their libc equivalents.
 *
 * Per-symbol @c -D overrides are also accepted (each macro is guarded by
 * an @c ifndef), so vendoring is optional for ad-hoc consumers.
 */

#ifndef ZXC_DEPS_H
#define ZXC_DEPS_H

/**
 * @addtogroup internal
 * @{
 */

/**
 * @name Standard Headers
 * @brief Pulled in by the stock libzxc build to provide @c size_t,
 * @c uintN_t / @c intN_t, @c CHAR_BIT, @c malloc / @c calloc /
 * @c realloc / @c free, and @c memcpy / @c memset / @c memmove /
 * @c memcmp.
 *
 * Vendored overrides of this file typically substitute @c <linux/limits.h>,
 * @c <linux/types.h>, @c <linux/string.h>, @c <linux/slab.h>,
 * @c <linux/sort.h>.
 * @{
 */
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/** @} */ /* end of Standard Headers */

/**
 * @name Heap Allocator Abstraction
 * @brief Macros around the libc allocators so non-libc targets (Linux kernel,
 * embedded freestanding builds, custom arenas) can override them via @c -D
 * flags **before** including any zxc header, or by vendoring this file.
 *
 * @note Aligned allocations go through @ref ZXC_ALIGNED_MALLOC /
 * @ref ZXC_ALIGNED_FREE (see below), not these.
 * @{
 */

/** @def ZXC_MALLOC
 *  @brief Heap allocator. Default: libc @c malloc. */
#ifndef ZXC_MALLOC
#define ZXC_MALLOC(size) malloc(size)
#endif

/** @def ZXC_CALLOC
 *  @brief Zero-initialised heap allocator. Default: libc @c calloc. */
#ifndef ZXC_CALLOC
#define ZXC_CALLOC(nmemb, size) calloc(nmemb, size)
#endif

/** @def ZXC_REALLOC
 *  @brief In-place / move heap reallocator. Default: libc @c realloc. */
#ifndef ZXC_REALLOC
#define ZXC_REALLOC(ptr, size) realloc(ptr, size)
#endif

/** @def ZXC_FREE
 *  @brief Heap deallocator. Default: libc @c free. */
#ifndef ZXC_FREE
#define ZXC_FREE(ptr) free(ptr)
#endif

/** @} */ /* end of Heap Allocator Abstraction */

/**
 * @name Aligned Allocator Abstraction
 * @brief Macros around the cache-line-aligned allocator used for compression
 * workspace and per-context scratch buffers.
 *
 * The default is the pair defined below, wrapping @c _aligned_malloc on Windows
 * and @c posix_memalign elsewhere - the core's only use of either.
 *
 * Kernel builds typically map this to the slab allocator: @c kmalloc already
 * returns @c ARCH_KMALLOC_MINALIGN-aligned memory, which is greater than or
 * equal to the cache line size on every supported architecture.
 * @{
 */

// Defining ZXC_ALIGNED_MALLOC takes the whole block: the two helpers below are
// then not compiled at all, and ZXC_ALIGNED_FREE has to come from the host too.
#ifndef ZXC_ALIGNED_MALLOC

/** @brief Default cache-line-aligned allocator; release with @ref zxc_aligned_free. */
static inline void* zxc_aligned_malloc(const size_t size, const size_t alignment) {
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) return NULL;
    return ptr;
#endif
}

/** @brief Counterpart of @ref zxc_aligned_malloc; NULL is a no-op. */
static inline void zxc_aligned_free(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/** @def ZXC_ALIGNED_MALLOC
 *  @brief Cache-line-aligned allocator. */
#define ZXC_ALIGNED_MALLOC(size, alignment) zxc_aligned_malloc(size, alignment)
/** @def ZXC_ALIGNED_FREE
 *  @brief Counterpart deallocator for @ref ZXC_ALIGNED_MALLOC. */
#define ZXC_ALIGNED_FREE(ptr) zxc_aligned_free(ptr)

#endif  // ZXC_ALIGNED_MALLOC

/** @} */ /* end of Aligned Allocator Abstraction */

/** @} */ /* end of addtogroup internal */

#endif /* ZXC_DEPS_H */
