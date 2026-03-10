/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_buffer.h
 * @brief Buffer-based (single-shot) compression and decompression API.
 *
 * This header exposes the simplest way to use ZXC: pass an entire input buffer
 * and receive the result in a single output buffer.  All functions in this
 * header are single-threaded and blocking.
 *
 * @par Typical usage
 * @code
 * // Compress
 * size_t bound = zxc_compress_bound(src_size);
 * void *dst    = malloc(bound);
 * zxc_compress_opts_t opts = { .level = ZXC_LEVEL_DEFAULT, .checksum = 1 };
 * int64_t csize = zxc_compress(src, src_size, dst, bound, &opts);
 *
 * // Decompress
 * uint64_t orig = zxc_get_decompressed_size(dst, csize);
 * void *out     = malloc(orig);
 * zxc_decompress_opts_t dopts = { .checksum = 1 };
 * int64_t dsize = zxc_decompress(dst, csize, out, orig, &dopts);
 * @endcode
 *
 * @see zxc_stream.h  for the streaming (multi-threaded) API.
 * @see zxc_sans_io.h for the low-level sans-I/O building blocks.
 */

#ifndef ZXC_BUFFER_H
#define ZXC_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "zxc_export.h"
#include "zxc_stream.h" /* zxc_compress_opts_t, zxc_decompress_opts_t */

/**
 * @defgroup buffer_api Buffer API
 * @brief Single-shot, buffer-based compression and decompression.
 * @{
 */

/**
 * @brief Calculates the maximum theoretical compressed size for a given input.
 *
 * Useful for allocating output buffers before compression.
 * Accounts for file headers, block headers, and potential expansion
 * of incompressible data.
 *
 * @param[in] input_size Size of the input data in bytes.
 *
 * @return           Maximum required buffer size in bytes.
 */
ZXC_EXPORT uint64_t zxc_compress_bound(const size_t input_size);

/**
 * @brief Compresses a data buffer using the ZXC algorithm.
 *
 * This version uses standard size_t types and void pointers.
 * It executes in a single thread (blocking operation).
 * It writes the ZXC file header followed by compressed blocks.
 *
 * @param[in] src          Pointer to the source buffer.
 * @param[in] src_size     Size of the source data in bytes.
 * @param[out] dst          Pointer to the destination buffer.
 * @param[in] dst_capacity Maximum capacity of the destination buffer.
 * @param[in] opts         Compression options (NULL uses all defaults).
 *                         Only @c level, @c block_size, and @c checksum are used.
 *
 * @return The number of bytes written to dst (>0 on success),
 *         or a negative zxc_error_t code (e.g., ZXC_ERROR_DST_TOO_SMALL) on failure.
 */
ZXC_EXPORT int64_t zxc_compress(const void* src, const size_t src_size, void* dst,
                                const size_t dst_capacity, const zxc_compress_opts_t* opts);

/**
 * @brief Decompresses a ZXC compressed buffer.
 *
 * This version uses standard size_t types and void pointers.
 * It executes in a single thread (blocking operation).
 * It expects a valid ZXC file header followed by compressed blocks.
 *
 * @param[in] src          Pointer to the source buffer containing compressed data.
 * @param[in] src_size      Size of the compressed data in bytes.
 * @param[out] dst          Pointer to the destination buffer.
 * @param[in] dst_capacity  Capacity of the destination buffer.
 * @param[in] opts          Decompression options (NULL uses all defaults).
 *                          Only @c checksum is used.
 *
 * @return The number of bytes written to dst (>0 on success),
 *         or a negative zxc_error_t code (e.g., ZXC_ERROR_CORRUPT_DATA) on failure.
 */
ZXC_EXPORT int64_t zxc_decompress(const void* src, const size_t src_size, void* dst,
                                  const size_t dst_capacity, const zxc_decompress_opts_t* opts);

/**
 * @brief Returns the decompressed size stored in a ZXC compressed buffer.
 *
 * This function reads the file footer to extract the original uncompressed size
 * without performing any decompression. Useful for allocating output buffers.
 *
 * @param[in] src       Pointer to the compressed data buffer.
 * @param[in] src_size  Size of the compressed data in bytes.
 *
 * @return The original uncompressed size in bytes, or 0 if the buffer is invalid
 *         or too small to contain a valid ZXC archive.
 */
ZXC_EXPORT uint64_t zxc_get_decompressed_size(const void* src, const size_t src_size);

/** @} */ /* end of buffer_api */

#endif  // ZXC_BUFFER_H