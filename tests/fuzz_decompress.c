/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
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