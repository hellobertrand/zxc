/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_dict.h
 * @brief Pre-trained dictionary API for ZXC compression.
 *
 * Provides functions to train, save, load, and identify dictionaries that
 * improve compression ratio on small, similar payloads. Dictionaries are
 * stored as external `.zxd` files and referenced by a 32-bit ID in the
 * ZXC file header.
 *
 * A dictionary contains raw byte content that prefills the LZ77 sliding
 * window at the start of each block, giving the compressor immediate
 * access to representative patterns without waiting for them to appear
 * in the input stream.
 *
 * @code
 * // Train a dictionary from a corpus of JSON samples
 * void* dict_buf = malloc(32768);
 * int64_t dict_sz = zxc_train_dict(samples, sizes, n, dict_buf, 32768);
 *
 * // Save to .zxd file
 * void* zxd = malloc(zxc_dict_save_bound(dict_sz));
 * int64_t zxd_sz = zxc_dict_save(dict_buf, dict_sz, zxd, ...);
 *
 * // Use for compression
 * zxc_compress_opts_t opts = { .level = 3, .dict = dict_buf, .dict_size = dict_sz };
 * zxc_compress(src, src_size, dst, dst_capacity, &opts);
 * @endcode
 */

#ifndef ZXC_DICT_H
#define ZXC_DICT_H

#include <stddef.h>
#include <stdint.h>

#include "zxc_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup dict Dictionary
 * @brief Pre-trained dictionary training, serialization, and identification.
 * @{
 */

/**
 * @brief Compute the dictionary ID for the given content.
 *
 * The ID is a deterministic 32-bit hash of the raw dictionary content.
 * It is stored in the ZXC file header so the decoder can verify that
 * the correct dictionary is provided at decompression time.
 *
 * @param[in] dict      Pointer to dictionary content.
 * @param[in] dict_size Size in bytes.
 * @return 32-bit dictionary ID. Returns 0 if @p dict is NULL or @p dict_size is 0.
 */
ZXC_EXPORT uint32_t zxc_dict_id(const void* dict, size_t dict_size);

/**
 * @brief Load and validate a `.zxd` dictionary file from a memory buffer.
 *
 * On success, @p content_out points into the input buffer (zero-copy).
 * The caller must keep @p buf alive while the content pointer is in use.
 *
 * @param[in]  buf              Buffer containing the .zxd file.
 * @param[in]  buf_size         Size of @p buf in bytes.
 * @param[out] content_out      Receives a pointer to the dictionary content.
 * @param[out] content_size_out Receives the content size in bytes.
 * @param[out] dict_id_out      Receives the dictionary ID (may be NULL).
 * @return @ref ZXC_OK on success, or a negative @ref zxc_error_t code.
 */
ZXC_EXPORT int zxc_dict_load(const void* buf, size_t buf_size, const void** content_out,
                             size_t* content_size_out, uint32_t* dict_id_out);

/**
 * @brief Serialize dictionary content to the `.zxd` file format.
 *
 * @param[in]  content       Raw dictionary content.
 * @param[in]  content_size  Size of @p content in bytes (max ZXC_DICT_SIZE_MAX).
 * @param[out] buf           Output buffer for the .zxd file.
 * @param[in]  buf_capacity  Capacity of @p buf.
 * @return Number of bytes written on success, or a negative @ref zxc_error_t code.
 */
ZXC_EXPORT int64_t zxc_dict_save(const void* content, size_t content_size, void* buf,
                                 size_t buf_capacity);

/**
 * @brief Returns the maximum .zxd file size for a given content size.
 *
 * @param[in] content_size Size of the dictionary content.
 * @return Total .zxd file size (header + content).
 */
ZXC_EXPORT size_t zxc_dict_save_bound(size_t content_size);

/**
 * @brief Returns the dictionary ID stored in a `.zxd` file buffer.
 *
 * Reads the dict_id field from the .zxd header without validating the full
 * file. Returns 0 if the buffer is too small or the magic word doesn't match.
 *
 * @param[in] buf       Buffer containing the .zxd file.
 * @param[in] buf_size  Size of @p buf in bytes.
 * @return Dictionary ID, or 0 if the buffer is not a valid .zxd file.
 */
ZXC_EXPORT uint32_t zxc_dict_get_id(const void* buf, size_t buf_size);

/**
 * @brief Train a dictionary from a corpus of samples.
 *
 * Analyzes the samples to select byte sequences that maximize LZ77 match
 * coverage. The resulting dictionary content can be passed directly to
 * zxc_compress_opts_t::dict or serialized with zxc_dict_save().
 *
 * @param[in]  samples        Array of pointers to sample buffers.
 * @param[in]  sample_sizes   Array of sample sizes in bytes.
 * @param[in]  n_samples      Number of samples.
 * @param[out] dict_buf       Output buffer for trained dictionary content.
 * @param[in]  dict_capacity  Capacity of @p dict_buf (max ZXC_DICT_SIZE_MAX).
 * @return Size of the trained dictionary on success, or a negative
 *         @ref zxc_error_t code.
 */
ZXC_EXPORT int64_t zxc_train_dict(const void* const* samples, const size_t* sample_sizes,
                                  size_t n_samples, void* dict_buf, size_t dict_capacity);

/** @} */ /* end of dict */

#ifdef __cplusplus
}
#endif

#endif /* ZXC_DICT_H */
