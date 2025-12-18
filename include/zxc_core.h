/*
 * Copyright (c) 2025, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef ZXC_CORE_H
#define ZXC_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * ZXC Compression Library - Public API
 * ============================================================================
 */

/* ===============================================================
 * ZXC Versioning
 * =============================================================== */

#define ZXC_VERSION_MAJOR 0
#define ZXC_VERSION_MINOR 1
#define ZXC_VERSION_PATCH 1

#define ZXC_STR_HELPER(x) #x
#define ZXC_STR(x) ZXC_STR_HELPER(x)

#define ZXC_LIB_VERSION_STR    \
    ZXC_STR(ZXC_VERSION_MAJOR) \
    "." ZXC_STR(ZXC_VERSION_MINOR) "." ZXC_STR(ZXC_VERSION_PATCH)

/* =============================================================
 * ZXC Compression Levels
 * ============================================================= */

#define ZXC_LEVEL_FAST (2)      // Fastest compression, best for real-time applications
#define ZXC_LEVEL_DEFAULT (3)   // Recommended: ratio > LZ4, decode speed > LZ4
#define ZXC_LEVEL_BALANCED (4)  // Good ratio, good decode speed
#define ZXC_LEVEL_COMPACT (5)   // High density. Best for storage/firmware/assets.

/*
 * UTILITIES
 * ----------------------------------------------------------------------------
 */

/**
 * @brief Calculates the maximum theoretical compressed size for a given input.
 *
 * Useful for allocating output buffers before compression.
 * Accounts for file headers, block headers, and potential expansion
 * of incompressible data.
 *
 * @param input_size Size of the input data in bytes.
 *
 * @return           Maximum required buffer size in bytes.
 */
size_t zxc_compress_bound(size_t input_size);

/**
 * @brief Compresses a data buffer using the ZXC algorithm.
 *
 * This version uses standard size_t types and void pointers.
 * It executes in a single thread (blocking operation).
 * It writes the ZXC file header followed by compressed blocks
 *
 * @param src          Pointer to the source buffer.
 * @param src_size     Size of the source data in bytes.
 * @param dst          Pointer to the destination buffer.
 * @param dst_capacity Maximum capacity of the destination buffer.
 * @param level        Compression level (e.g., ZXC_LEVEL_BALANCED).
 * @param checksum_enabled Flag indicating whether to verify the checksum of the
 * data (1 to enable, 0 to disable).
 *
 * @return The number of bytes written to dst, or 0 if the destination buffer
 * is too small or an error occurred.
 */
size_t zxc_compress(const void* src, size_t src_size, void* dst, size_t dst_capacity, int level,
                    int checksum_enabled);

/**
 * @brief Decompresses a ZXC compressed buffer.
 *
 * This version uses standard size_t types and void pointers.
 * It executes in a single thread (blocking operation).
 * It expects a valid ZXC file header followed by compressed blocks.
 *
 * @param src          Pointer to the source buffer containing compressed data.
 * @param src_size      Size of the compressed data in bytes.
 * @param dst          Pointer to the destination buffer.
 * @param dst_capacity  Capacity of the destination buffer.
 * @param checksum_enabled Flag indicating whether to verify the checksum of the
 * data (1 to enable, 0 to disable).
 *
 * @return The number of bytes written to dst, or 0 if decompression fails
 * (invalid header, corruption, or destination too small).
 */
size_t zxc_decompress(const void* src, size_t src_size, void* dst, size_t dst_capacity,
                      int checksum_enabled);

#ifdef __cplusplus
}
#endif

#endif  // ZXC_CORE_H