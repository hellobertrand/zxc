/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
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
 * Strategy: query zxc_get_decompressed_size() to size the output buffer when
 * the header advertises a plausible length (clamped to the static cap), then
 * call zxc_decompress() on the raw bytes. Correctness is enforced by the
 * sanitizers (ASan/UBSan) catching any out-of-bounds access; truncated,
 * corrupt, and adversarial frames are all expected to return an error rather
 * than misbehave.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../include/zxc_buffer.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static uint8_t out_buf[4 << 20]; /* 4 MiB */
    size_t out_capacity = sizeof(out_buf);

    uint64_t expected_size = zxc_get_decompressed_size(data, size);

    if (expected_size > 0 && expected_size <= out_capacity) out_capacity = (size_t)expected_size;

    zxc_decompress(data, size, out_buf, out_capacity, 0);

    return 0;
}