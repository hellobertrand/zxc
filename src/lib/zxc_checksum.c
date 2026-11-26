// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file zxc_checksum.c
 * @brief Block checksum algorithms, behind zxc_checksum().
 *
 * Compiled once per variant with @c ZXC_FUNCTION_SUFFIX, so an algorithm with
 * SIMD backends gets the one of its callers.
 */

#include "rapidhash.h"
#include "zxc_internal.h"

uint32_t zxc_checksum(const void* RESTRICT input, const size_t len, const uint64_t seed,
                      const uint8_t hash_method) {
    (void)hash_method;
    const uint64_t hash = rapidhash_withSeed(input, len, seed);
    return (uint32_t)(hash ^ (hash >> (sizeof(uint32_t) * CHAR_BIT)));
}
