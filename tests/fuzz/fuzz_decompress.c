// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file fuzz_decompress.c
 * @brief Fuzzer for the one-shot decompressor on untrusted input.
 *
 * The decoder is the security-critical surface: it parses attacker-controlled
 * frames, so it must never crash, over-read, or over-write regardless of how
 * malformed the input is. This is a one-way target -- the fuzzer bytes are fed
 * straight to zxc_decompress() with no preceding compression step.
 *
 * Strategy: decode into a buffer of exactly the advertised size, so ASan sees
 * any overrun, then once more into a shorter one. Malformed frames must return
 * an error rather than misbehave.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../include/zxc_buffer.h"

#define FUZZ_DECOMPRESS_MAX_OUT (4 << 20) /* 4 MiB */

static void decode_exact(const uint8_t* data, const size_t size, const size_t dst_capacity) {
    uint8_t* const dst = dst_capacity ? (uint8_t*)malloc(dst_capacity) : NULL;
    if (dst_capacity && !dst) return;
    zxc_decompress(data, size, dst, dst_capacity);
    free(dst);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const uint64_t expected_size = zxc_get_decompressed_size(data, size);
    if (expected_size == 0 || expected_size > FUZZ_DECOMPRESS_MAX_OUT) {
        // Whole array as capacity: still exact.
        static uint8_t out_buf[FUZZ_DECOMPRESS_MAX_OUT];
        zxc_decompress(data, size, out_buf, sizeof(out_buf));
        return 0;
    }
    decode_exact(data, size, (size_t)expected_size);
    const size_t short_by = 1 + (size_t)(((expected_size - 1) * data[size / 2]) >> 8);
    decode_exact(data, size, (size_t)expected_size - short_by);
    return 0;
}
