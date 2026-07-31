/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Downstream consumer smoke test for an *installed* ZXC, built by
 * .github/workflows/packaging.yml via find_package, pkg-config and raw -I/-l.
 * Touches enough of the API to make the link step meaningful: a mismatched
 * zxc_export.h shows up here as unresolved __imp_zxc_* on Windows.
 */

#include <stdio.h>
#include <string.h>
#include <zxc.h>

int main(void) {
    static char input[64 * 1024];
    for (size_t i = 0; i < sizeof(input); i++) {
        input[i] = (char)('a' + (i % 23));
    }

    const uint64_t bound = zxc_compress_bound(sizeof(input));
    static char compressed[128 * 1024];
    if (bound > sizeof(compressed)) {
        fprintf(stderr, "consumer: bound %llu exceeds the test buffer\n",
                (unsigned long long)bound);
        return 1;
    }

    const int64_t csize = zxc_compress(input, sizeof(input), compressed, sizeof(compressed), NULL);
    if (csize <= 0) {
        fprintf(stderr, "consumer: zxc_compress failed: %s\n", zxc_error_name((int)csize));
        return 1;
    }

    if (zxc_get_decompressed_size(compressed, (size_t)csize) != sizeof(input)) {
        fprintf(stderr, "consumer: zxc_get_decompressed_size disagrees with the input\n");
        return 1;
    }

    static char output[sizeof(input)];
    const int64_t dsize = zxc_decompress(compressed, (size_t)csize, output, sizeof(output), NULL);
    if (dsize != (int64_t)sizeof(input)) {
        fprintf(stderr, "consumer: zxc_decompress failed: %s\n", zxc_error_name((int)dsize));
        return 1;
    }

    if (memcmp(input, output, sizeof(input)) != 0) {
        fprintf(stderr, "consumer: roundtrip mismatch\n");
        return 1;
    }

    printf("consumer: zxc %s roundtrip OK (%zu -> %lld bytes)\n", zxc_version_string(),
           sizeof(input), (long long)csize);
    return 0;
}
