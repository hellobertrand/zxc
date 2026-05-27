/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Fuzz target: dictionary roundtrip.
 *
 * The fuzzer input is split into a dictionary prefix and block data.
 * The first 2 bytes encode the dict size (u16 LE, capped at 32 KB).
 * The remainder is the block data to compress with that dictionary.
 * The roundtrip (compress -> decompress) must produce identical output.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"

#define FUZZ_DICT_MAX_INPUT (256 << 10) /* 256 KiB */
#define FUZZ_DICT_MAX_DICT (32 << 10)   /* 32 KiB */

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static void* comp_buf = NULL;
    static size_t comp_cap = 0;
    static void* decomp_buf = NULL;
    static size_t decomp_cap = 0;

    if (size < 4) return 0;
    if (size > FUZZ_DICT_MAX_INPUT) return 0;

    /* First 2 bytes: dict_size (u16 LE, capped). Byte 2: level. */
    size_t dict_size = (size_t)(data[0] | (data[1] << 8));
    if (dict_size > FUZZ_DICT_MAX_DICT) dict_size = FUZZ_DICT_MAX_DICT;
    const int level = (data[2] % 6) + 1;
    data += 3;
    size -= 3;

    if (dict_size >= size) dict_size = size / 2;
    const uint8_t* dict = data;
    const uint8_t* src = data + dict_size;
    const size_t src_size = size - dict_size;

    if (src_size == 0) return 0;

    const uint64_t bound64 = zxc_compress_bound(src_size);
    if (bound64 == 0 || bound64 > SIZE_MAX) return 0;
    const size_t bound = (size_t)bound64;
    if (bound > comp_cap) {
        void* nb = realloc(comp_buf, bound);
        if (!nb) return 0;
        comp_buf = nb;
        comp_cap = bound;
    }

    zxc_compress_opts_t copts = {
        .level = level,
        .checksum_enabled = 1,
        .dict = dict_size > 0 ? dict : NULL,
        .dict_size = dict_size,
    };
    const int64_t csize = zxc_compress(src, src_size, comp_buf, bound, &copts);
    if (csize < 0) return 0;

    if (src_size > decomp_cap) {
        void* nb = realloc(decomp_buf, src_size);
        if (!nb) return 0;
        decomp_buf = nb;
        decomp_cap = src_size;
    }

    zxc_decompress_opts_t dopts = {
        .checksum_enabled = 1,
        .dict = dict_size > 0 ? dict : NULL,
        .dict_size = dict_size,
    };
    const int64_t dsize = zxc_decompress(comp_buf, (size_t)csize, decomp_buf, src_size, &dopts);

    if (dsize >= 0) {
        assert((size_t)dsize == src_size);
        assert(memcmp(src, decomp_buf, src_size) == 0);
    }

    return 0;
}
