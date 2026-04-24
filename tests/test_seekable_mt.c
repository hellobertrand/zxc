/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

int test_seekable_mt_roundtrip() {
    printf("=== TEST: Seekable MT - Roundtrip (4 threads) ===\n");

    const size_t SRC_SIZE = 512 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 55);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) { free(src); free(dst); free(dec); return 0; }

    /* Compress with small blocks to create many blocks */
    zxc_compress_opts_t opts = {.level = 3, .block_size = 64 * 1024, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); free(dec); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); free(dec); return 0; }

    /* Full decompression with 4 threads */
    int64_t r = zxc_seekable_decompress_range_mt(s, dec, SRC_SIZE, 0, SRC_SIZE, 4);
    if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: MT decompress mismatch\n");
        zxc_seekable_free(s); free(src); free(dst); free(dec);
        return 0;
    }

    zxc_seekable_free(s);
    free(src); free(dst); free(dec);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_mt_single_block() {
    printf("=== TEST: Seekable MT - Single Block Fallback ===\n");

    const size_t SRC_SIZE = 32 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 88);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) { free(src); free(dst); free(dec); return 0; }

    /* Compress with block_size >= SRC_SIZE => only 1 block */
    zxc_compress_opts_t opts = {.level = 3, .block_size = 64 * 1024, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); free(dec); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); free(dec); return 0; }

    /* Should fallback to ST since only 1 block */
    int64_t r = zxc_seekable_decompress_range_mt(s, dec, SRC_SIZE, 0, SRC_SIZE, 4);
    if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: single-block MT mismatch\n");
        zxc_seekable_free(s); free(src); free(dst); free(dec);
        return 0;
    }

    zxc_seekable_free(s);
    free(src); free(dst); free(dec);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_mt_random_access() {
    printf("=== TEST: Seekable MT - Random Access ===\n");

    const size_t SRC_SIZE = 512 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 11);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 3, .block_size = 64 * 1024, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }

    /* Mid-range spanning 3+ blocks with MT */
    const uint64_t off = 100 * 1024;
    const size_t len = 200 * 1024;
    uint8_t* out = malloc(len);
    if (!out) { zxc_seekable_free(s); free(src); free(dst); return 0; }

    int64_t r = zxc_seekable_decompress_range_mt(s, out, len, off, len, 4);
    if (r != (int64_t)len || memcmp(src + off, out, len) != 0) {
        printf("Failed: MT random access mismatch\n");
        free(out); zxc_seekable_free(s); free(src); free(dst);
        return 0;
    }

    free(out);
    zxc_seekable_free(s);
    free(src); free(dst);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_mt_full_file() {
    printf("=== TEST: Seekable MT - Full File (auto threads) ===\n");

    const size_t SRC_SIZE = 1024 * 1024; /* 1 MB */
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 7);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) { free(src); free(dst); free(dec); return 0; }

    /* 16 blocks of 64K */
    zxc_compress_opts_t opts = {.level = 3, .block_size = 64 * 1024, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); free(dec); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); free(dec); return 0; }

    /* Decompress with auto-detect threads (n_threads=0) */
    int64_t r = zxc_seekable_decompress_range_mt(s, dec, SRC_SIZE, 0, SRC_SIZE, 0);
    if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: MT full file mismatch\n");
        zxc_seekable_free(s); free(src); free(dst); free(dec);
        return 0;
    }

    zxc_seekable_free(s);
    free(src); free(dst); free(dec);
    printf("PASS\n\n");
    return 1;
}
