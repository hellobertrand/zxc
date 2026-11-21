// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file fuzz_roundtrip.c
 * @brief Fuzzer for the one-shot compress -> decompress roundtrip.
 *
 * Asserts the core invariant of a lossless codec: decompressing what the
 * compressor produced must reproduce the original bytes exactly. The fuzzer
 * data is the payload to compress (never untrusted decoder input -- that is
 * fuzz_decompress.c's job), so this target hunts for encoder/decoder
 * mismatches rather than malformed-frame handling.
 *
 * Strategy: derive the compression level from the first byte so libFuzzer
 * explores every level, compress the fuzzed bytes, then decompress into an
 * exact-size buffer and assert a bit-exact roundtrip.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_error.h"

#define FUZZ_ROUNDTRIP_MAX_INPUT (4 << 20) /* 4 MiB */

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > FUZZ_ROUNDTRIP_MAX_INPUT) return 0;

    const size_t bound = zxc_compress_bound(size);
    if (bound == 0) return 0;
    uint8_t* const comp_buf = (uint8_t*)malloc(bound);
    if (!comp_buf) return 0;

    const int level = size > 0 ? (data[0] % (unsigned)zxc_max_level()) + 1 : 1;
    // Bit 6 of data[0] turns the decode-speed split on, data[1] sizes its budget.
    const int budget =
        size > 1 && ((data[0] >> 6) & 1) ? 1 + data[1] % ZXC_FAVOR_DECODE_SPEED_MAX : 0;
    int64_t csize = ZXC_ERROR_MEMORY;
    if (budget) {
        zxc_cctx* const cctx = zxc_create_cctx();
        if (cctx)
            csize = zxc_cctx_set_param(cctx, ZXC_C_LEVEL, level) == ZXC_OK &&
                            zxc_cctx_set_param(cctx, ZXC_C_FAVOR_DECODE_SPEED, budget) == ZXC_OK
                        ? zxc_compress_cctx(cctx, data, size, comp_buf, bound)
                        : ZXC_ERROR_BAD_PARAM;
        zxc_free_cctx(cctx);
    } else {
        csize = zxc_compress(data, size, comp_buf, bound, level);
    }

    uint8_t* const decomp_buf = csize >= 0 && size > 0 ? (uint8_t*)malloc(size) : NULL;
    if (decomp_buf) {
        const int64_t dsize = zxc_decompress(comp_buf, (size_t)csize, decomp_buf, size);
        if (dsize >= 0) {
            assert((size_t)dsize == size);
            assert(memcmp(data, decomp_buf, size) == 0);
        }
    }

    free(decomp_buf);
    free(comp_buf);
    return 0;
}
