/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"

#define FUZZ_ROUNDTRIP_MAX_INPUT (4 << 20) /* 4 MiB */

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static void* comp_buf = NULL;
    static size_t comp_cap = 0;
    static void* decomp_buf = NULL;
    static size_t decomp_cap = 0;

    if (size > FUZZ_ROUNDTRIP_MAX_INPUT) return 0;

    const uint64_t bound64 = zxc_compress_bound(size);
    if (bound64 == 0 || bound64 > SIZE_MAX) return 0;
    const size_t bound = (size_t)bound64;
    if (bound > comp_cap) {
        void* new_buf = realloc(comp_buf, bound);
        if (!new_buf) return 0;
        comp_buf = new_buf;
        comp_cap = bound;
    }

    const int level = size > 0 ? (data[0] % 5) + 1 : 1;
    zxc_compress_opts_t copts = {.level = level};
    const int64_t csize = zxc_compress(data, size, comp_buf, bound, &copts);
    if (csize < 0) return 0;

    if (size == 0) return 0;

    if (size > decomp_cap) {
        void* new_buf = realloc(decomp_buf, size);
        if (!new_buf) return 0;
        decomp_buf = new_buf;
        decomp_cap = size;
    }

    const int64_t dsize = zxc_decompress(comp_buf, (size_t)csize, decomp_buf, size, NULL);

    if (dsize >= 0) {
        assert((size_t)dsize == size);
        assert(memcmp(data, decomp_buf, size) == 0);
    }

    return 0;
}