/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_stream.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FILE* const f_in = fmemopen((void*)data, size, "rb");
    if (!f_in) return 0;

    char* comp_buf = NULL;
    size_t comp_size = 0;
    FILE* const f_comp = open_memstream(&comp_buf, &comp_size);
    if (!f_comp) {
        fclose(f_in);
        return 0;
    }

    if (zxc_stream_compress(f_in, f_comp, 1, 2, 0) != 0) {
        fclose(f_in);
        fclose(f_comp);
        free(comp_buf);
        return 0;
    }

    fclose(f_comp);
    fclose(f_in);

    FILE* const f_comp_read = fmemopen(comp_buf, comp_size, "rb");

    char* decomp_buf = NULL;
    size_t decomp_size = 0;
    FILE* const f_decomp = open_memstream(&decomp_buf, &decomp_size);

    if (!f_comp_read || !f_decomp) {
        if (f_comp_read) fclose(f_comp_read);
        if (f_decomp) fclose(f_decomp);
        free(comp_buf);
        free(decomp_buf);
        return 0;
    }

    const int64_t res = zxc_stream_decompress(f_comp_read, f_decomp, 1, 0);

    fclose(f_comp_read);
    fclose(f_decomp);

    if (res >= 0) {
        assert(decomp_size == size);
        assert(memcmp(data, decomp_buf, size) == 0);
    }

    free(comp_buf);
    free(decomp_buf);

    return 0;
}