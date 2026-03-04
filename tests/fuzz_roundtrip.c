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

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const size_t bound = zxc_compress_bound(size);
    void* comp_buf = malloc(bound);
    if (!comp_buf) return 0;

    const int level = size > 0 ? (data[0] % 5) + 1 : 1;
    const int64_t csize = zxc_compress(data, size, comp_buf, bound, level, 0);
    if (csize < 0) {
        free(comp_buf);
        return 0;
    }

    if (size == 0) {
        free(comp_buf);
        return 0;
    }
    void* decomp_buf = malloc(size);
    if (!decomp_buf) {
        free(comp_buf);
        return 0;
    }

    const int64_t dsize = zxc_decompress(comp_buf, (size_t)csize, decomp_buf, size, 0);

    if (dsize >= 0) {
        assert((size_t)dsize == size);
        assert(memcmp(data, decomp_buf, size) == 0);
    }

    free(decomp_buf);
    free(comp_buf);

    return 0;
}