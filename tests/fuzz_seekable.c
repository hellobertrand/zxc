/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fuzz_seekable.c
 * @brief Fuzzer for the seekable random-access decompression API.
 *
 * Strategy: compress fuzzed input with seekable=1, then exercise the full
 * seekable read path (open, metadata getters, single-threaded decompress,
 * multi-threaded decompress) and verify data integrity.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_seekable.h"

#define FUZZ_SEEKABLE_MAX_INPUT (4 << 20) /* 4 MiB */

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return 0;

    /* Use first byte as level, second as flags */
    const int level = (data[0] % 5) + 1;
    const int use_checksum = data[1] & 1;
    const int use_mt = data[1] & 2;
    data += 2;
    size -= 2;

    if (size == 0 || size > FUZZ_SEEKABLE_MAX_INPUT) return 0;

    /* Persistent buffers - reused across iterations to reduce allocator pressure */
    static uint8_t* comp_buf = NULL;
    static size_t comp_cap = 0;
    static uint8_t* decomp_buf = NULL;
    static size_t decomp_cap = 0;

    /* ------------------------------------------------------------------ */
    /* Phase 1: Compress with seekable=1                                   */
    /* ------------------------------------------------------------------ */
    const uint64_t bound64 = zxc_compress_bound(size);
    if (bound64 == 0 || bound64 > SIZE_MAX) return 0;
    const size_t bound = (size_t)bound64;
    if (bound > comp_cap) {
        void* new_buf = realloc(comp_buf, bound);
        if (!new_buf) return 0;
        comp_buf = (uint8_t*)new_buf;
        comp_cap = bound;
    }

    zxc_compress_opts_t copts = {
        .level = level,
        .checksum_enabled = use_checksum,
        .seekable = 1,
    };
    const int64_t csize = zxc_compress(data, size, comp_buf, bound, &copts);
    if (csize < 0) {
        free(comp_buf);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    /* Phase 2: Open seekable handle                                       */
    /* ------------------------------------------------------------------ */
    zxc_seekable* s = zxc_seekable_open(comp_buf, (size_t)csize);
    if (!s) {
        free(comp_buf);
        return 0;
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3: Exercise metadata getters                                   */
    /* ------------------------------------------------------------------ */
    const uint32_t num_blocks = zxc_seekable_get_num_blocks(s);
    const uint64_t total_decomp = zxc_seekable_get_decompressed_size(s);
    assert(total_decomp == size);

    for (uint32_t i = 0; i < num_blocks; i++) {
        const uint32_t csz = zxc_seekable_get_block_comp_size(s, i);
        const uint32_t dsz = zxc_seekable_get_block_decomp_size(s, i);
        assert(csz > 0);
        assert(dsz > 0);
        (void)csz;
        (void)dsz;
    }
    /* Out-of-range access should return 0 */
    assert(zxc_seekable_get_block_comp_size(s, num_blocks) == 0);
    assert(zxc_seekable_get_block_decomp_size(s, num_blocks) == 0);

    /* ------------------------------------------------------------------ */
    /* Phase 4: Full decompression via seekable range                       */
    /* ------------------------------------------------------------------ */
    if (size > decomp_cap) {
        void* new_buf = realloc(decomp_buf, size);
        if (!new_buf) {
            zxc_seekable_free(s);
            return 0;
        }
        decomp_buf = (uint8_t*)new_buf;
        decomp_cap = size;
    }

    int64_t dec_result;
    if (use_mt && size > 4096) {
        dec_result = zxc_seekable_decompress_range_mt(s, decomp_buf, size, 0, size, 2);
    } else {
        dec_result = zxc_seekable_decompress_range(s, decomp_buf, size, 0, size);
    }
    assert(dec_result == (int64_t)size);
    assert(memcmp(data, decomp_buf, size) == 0);

    /* ------------------------------------------------------------------ */
    /* Phase 5: Partial range decompression (sub-block extraction)          */
    /* ------------------------------------------------------------------ */
    if (size >= 4) {
        /* Extract a range from the middle */
        const size_t off = size / 4;
        const size_t len = size / 2;
        dec_result = zxc_seekable_decompress_range(s, decomp_buf, len, off, len);
        assert(dec_result == (int64_t)len);
        assert(memcmp(data + off, decomp_buf, len) == 0);
    }

    /* ------------------------------------------------------------------ */
    /* Phase 6: Edge cases                                                  */
    /* ------------------------------------------------------------------ */
    /* Zero-length read */
    dec_result = zxc_seekable_decompress_range(s, decomp_buf, size, 0, 0);
    assert(dec_result == 0);

    /* Out-of-bounds read */
    dec_result = zxc_seekable_decompress_range(s, decomp_buf, size, total_decomp, 1);
    assert(dec_result < 0);

    /* NULL handle */
    assert(zxc_seekable_get_num_blocks(NULL) == 0);
    assert(zxc_seekable_get_decompressed_size(NULL) == 0);

    /* ------------------------------------------------------------------ */
    /* Cleanup (comp_buf and decomp_buf are static, not freed)              */
    /* ------------------------------------------------------------------ */
    zxc_seekable_free(s);

    /* ------------------------------------------------------------------ */
    /* Phase 7: Fuzz the parser with raw data (malformed seekable archives) */
    /* ------------------------------------------------------------------ */
    zxc_seekable* s2 = zxc_seekable_open(data, size);
    if (s2) {
        /* If it parsed, try a read - should not crash */
        const uint64_t td = zxc_seekable_get_decompressed_size(s2);
        if (td > 0 && td <= 1048576) {
            uint8_t* tmp = (uint8_t*)malloc((size_t)td);
            if (tmp) {
                zxc_seekable_decompress_range(s2, tmp, (size_t)td, 0, (size_t)td);
                free(tmp);
            }
        }
        zxc_seekable_free(s2);
    }


    return 0;
}
