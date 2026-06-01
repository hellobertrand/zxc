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
 * Provides functions to train and identify dictionaries that improve
 * compression ratio on small, similar payloads. A dictionary is raw byte
 * content that prefills the LZ77 sliding window at the start of each block,
 * giving the compressor immediate access to representative patterns.
 *
 * Dictionaries are embedded in the archive (no standalone file format): pass
 * trained content to zxc_compress_opts_t::dict and it is stored in the archive,
 * so decompression needs no external dictionary.
 *
 * @code
 * // Train a dictionary from a corpus of JSON samples and embed it
 * void* dict_buf = malloc(32768);
 * int64_t dict_sz = zxc_train_dict(samples, sizes, n, dict_buf, 32768);
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
 * @brief Pre-trained dictionary training and identification.
 * @{
 */

/**
 * @brief Compute the dictionary ID for the given content.
 *
 * The ID is a deterministic 32-bit hash of the raw dictionary content.
 * It is stored in the ZXC file header so the decoder can verify the embedded
 * dictionary matches.
 *
 * @param[in] dict      Pointer to dictionary content.
 * @param[in] dict_size Size in bytes.
 * @return 32-bit dictionary ID. Returns 0 if @p dict is NULL or @p dict_size is 0.
 */
ZXC_EXPORT uint32_t zxc_dict_id(const void* dict, size_t dict_size);

/**
 * @brief Train a dictionary from a corpus of samples.
 *
 * Analyzes the samples to select byte sequences that maximize LZ77 match
 * coverage. The resulting dictionary content can be passed directly to
 * zxc_compress_opts_t::dict (it is then embedded in the archive).
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
