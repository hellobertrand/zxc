/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../include/zxc_stream.h"
#include "test_common.h"

int test_seekable_table_sizes() {
    printf("=== TEST: Seekable - Table Sizes ===\n");

    /* block_header(8) + 10*8 = 88 */
    if (zxc_seek_table_size(10) != 88) {
        printf("Failed: size for 10 blocks\n");
        return 0;
    }
    /* block_header(8) + 0 = 8 */
    if (zxc_seek_table_size(0) != 8) {
        printf("Failed: zero blocks size\n");
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

int test_seekable_table_write() {
    printf("=== TEST: Seekable - Table Write/Validate ===\n");

    const uint32_t comp[] = {100, 200, 150};
    const size_t sz = zxc_seek_table_size(3);
    uint8_t* buf = malloc(sz);
    if (!buf) return 0;

    const int64_t written = zxc_write_seek_table(buf, sz, comp, 3);
    if (written != (int64_t)sz) {
        printf("Failed: write size mismatch\n");
        free(buf);
        return 0;
    }

    /* Validate block_type == SEK in the block header */
    if (buf[0] != ZXC_BLOCK_SEK) {
        printf("Failed: bad block_type (%u)\n", buf[0]);
        free(buf);
        return 0;
    }

    /* Size field: 3 entries x 8, low 32 bits. Entries: where each block starts. */
    if (zxc_le32(buf + 3) != 24 || zxc_le64(buf + 8) != 16 || zxc_le64(buf + 16) != 116 ||
        zxc_le64(buf + 24) != 316) {
        printf("Failed: bad size field or entries\n");
        free(buf);
        return 0;
    }

    free(buf);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_roundtrip() {
    printf("=== TEST: Seekable - Compress/Decompress Roundtrip ===\n");

    const size_t SRC_SIZE = 256 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 42);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) {
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_compress_opts_t opts = {
        .level = ZXC_LEVEL_DEFAULT, .block_size = 64 * 1024, .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }
    /* Sub-test A: single block (data < block_size) roundtrip */
    {
        const size_t SMALL = 60 * 1024; /* fits in one 64KB block */
        memset(dec, 0, SMALL);
        zxc_compress_opts_t opts_a = {.level = ZXC_LEVEL_DEFAULT,
                                      .block_size = 64 * 1024,
                                      .checksum_enabled = 1,
                                      .seekable = 0};
        const int64_t a_csize = zxc_compress(src, SMALL, dst, dst_cap, &opts_a);
        if (a_csize <= 0) {
            printf("Failed: single-block 64KB compress (%lld)\n", (long long)a_csize);
            free(src);
            free(dst);
            free(dec);
            return 0;
        }
        zxc_decompress_opts_t ad = {.checksum_enabled = 1};
        const int64_t a_dsize = zxc_decompress(dst, (size_t)a_csize, dec, SMALL, &ad);
        if (a_dsize != (int64_t)SMALL || memcmp(src, dec, SMALL) != 0) {
            printf("Failed: single-block 64KB roundtrip (dsize=%lld)\n", (long long)a_dsize);
            if (a_dsize == (int64_t)SMALL) {
                for (size_t i = 0; i < SMALL; i++) {
                    if (src[i] != dec[i]) {
                        printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n", i, src[i],
                               dec[i]);
                        break;
                    }
                }
            }
            free(src);
            free(dst);
            free(dec);
            return 0;
        }
        printf("  sub-test A (single block, 60KB): OK\n");
    }
    /* Sub-test B: exactly 2 blocks (128KB with 64KB block_size) */
    {
        const size_t TWO = 128 * 1024;
        memset(dec, 0, TWO);
        zxc_compress_opts_t opts_b = {.level = ZXC_LEVEL_DEFAULT,
                                      .block_size = 64 * 1024,
                                      .checksum_enabled = 1,
                                      .seekable = 0};
        const int64_t b_csize = zxc_compress(src, TWO, dst, dst_cap, &opts_b);
        if (b_csize <= 0) {
            printf("Failed: 2-block 64KB compress (%lld)\n", (long long)b_csize);
            free(src);
            free(dst);
            free(dec);
            return 0;
        }
        zxc_decompress_opts_t bd = {.checksum_enabled = 1};
        const int64_t b_dsize = zxc_decompress(dst, (size_t)b_csize, dec, TWO, &bd);
        if (b_dsize != (int64_t)TWO || memcmp(src, dec, TWO) != 0) {
            printf("Failed: 2-block 64KB roundtrip (dsize=%lld)\n", (long long)b_dsize);
            if (b_dsize == (int64_t)TWO) {
                for (size_t i = 0; i < TWO; i++) {
                    if (src[i] != dec[i]) {
                        printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n", i, src[i],
                               dec[i]);
                        break;
                    }
                }
            }
            free(src);
            free(dst);
            free(dec);
            return 0;
        }
        printf("  sub-test B (2 blocks, 128KB): OK\n");
    }
    /* Sub-test C: full 256KB (4 blocks x 64KB) */
    {
        memset(dec, 0, SRC_SIZE);
        zxc_compress_opts_t opts_ns = {.level = ZXC_LEVEL_DEFAULT,
                                       .block_size = 64 * 1024,
                                       .checksum_enabled = 1,
                                       .seekable = 0};
        const int64_t ns_csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts_ns);
        if (ns_csize <= 0) {
            printf("Failed: non-seekable compress (%lld)\n", (long long)ns_csize);
            free(src);
            free(dst);
            free(dec);
            return 0;
        }
        zxc_decompress_opts_t nd = {.checksum_enabled = 1};
        const int64_t ns_dsize = zxc_decompress(dst, (size_t)ns_csize, dec, SRC_SIZE, &nd);
        if (ns_dsize != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
            printf("Failed: non-seekable 64KB block_size roundtrip (dsize=%lld)\n",
                   (long long)ns_dsize);
            if (ns_dsize == (int64_t)SRC_SIZE) {
                for (size_t i = 0; i < SRC_SIZE; i++) {
                    if (src[i] != dec[i]) {
                        printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n", i, src[i],
                               dec[i]);
                        break;
                    }
                }
            }
            free(src);
            free(dst);
            free(dec);
            return 0;
        }
        printf("  sub-test C (4 blocks, 256KB): OK\n");
    }

    /* Re-compress with seekable=1 for the actual test */
    memset(dec, 0, SRC_SIZE);
    const int64_t csize2 = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize2 <= 0) {
        printf("Failed: seekable re-compress (%lld)\n", (long long)csize2);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Full decompression with standard API (backward compatibility) */
    zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
    const int64_t dsize = zxc_decompress(dst, (size_t)csize2, dec, SRC_SIZE, &dopts);
    if (dsize != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: decompress mismatch (csize=%lld dsize=%lld expected=%zu)\n",
               (long long)csize2, (long long)dsize, SRC_SIZE);
        if (dsize == (int64_t)SRC_SIZE) {
            for (size_t i = 0; i < SRC_SIZE; i++) {
                if (src[i] != dec[i]) {
                    printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n", i, src[i], dec[i]);
                    break;
                }
            }
        }
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    free(src);
    free(dst);
    free(dec);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_open_query() {
    printf("=== TEST: Seekable - Open and Query ===\n");

    const size_t SRC_SIZE = 200 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 99);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 2, .block_size = 64 * 1024, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        return 0;
    }

    const uint32_t nb = zxc_seekable_get_num_blocks(s);
    if (nb < 3) {
        printf("Failed: expected >= 3 blocks, got %u\n", nb);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    const uint64_t total = zxc_seekable_get_decompressed_size(s);
    if (total != SRC_SIZE) {
        printf("Failed: decomp size\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    uint64_t sum = 0;
    for (uint32_t i = 0; i < nb; i++) {
        sum += zxc_seekable_get_block_decomp_size(s, i);
        if (zxc_seekable_get_block_comp_size(s, i) == 0) {
            printf("Failed: zero comp size\n");
            zxc_seekable_free(s);
            free(src);
            free(dst);
            return 0;
        }
    }
    if (sum != SRC_SIZE) {
        printf("Failed: block sizes sum\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable_free(s);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_random_access() {
    printf("=== TEST: Seekable - Random Access ===\n");

    const size_t SRC_SIZE = 300 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 77);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 3, .block_size = 64 * 1024, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        return 0;
    }

    /* First 1000 bytes */
    uint8_t out1[1000];
    int64_t r = zxc_seekable_decompress_range(s, out1, 1000, 0, 1000);
    if (r != 1000 || memcmp(src, out1, 1000) != 0) {
        printf("Failed: first 1000 bytes (r=%lld)\n", (long long)r);
        if (r == 1000) {
            for (int i = 0; i < 1000; i++) {
                if (src[i] != out1[i]) {
                    printf("  first diff at byte %d: src=0x%02x out=0x%02x\n", i, src[i], out1[i]);
                    break;
                }
            }
        }
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* Middle range spanning multiple blocks */
    const uint64_t off = 100 * 1024;
    const size_t len = 80 * 1024;
    uint8_t* out2 = malloc(len);
    if (!out2) {
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }
    r = zxc_seekable_decompress_range(s, out2, len, off, len);
    if (r != (int64_t)len || memcmp(src + off, out2, len) != 0) {
        printf("Failed: mid-range\n");
        free(out2);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }
    free(out2);

    /* Last bytes */
    uint8_t out3[512];
    r = zxc_seekable_decompress_range(s, out3, 512, SRC_SIZE - 512, 512);
    if (r != 512 || memcmp(src + SRC_SIZE - 512, out3, 512) != 0) {
        printf("Failed: tail\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* Entire file */
    uint8_t* out4 = malloc(SRC_SIZE);
    if (!out4) {
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }
    r = zxc_seekable_decompress_range(s, out4, SRC_SIZE, 0, SRC_SIZE);
    if (r != (int64_t)SRC_SIZE || memcmp(src, out4, SRC_SIZE) != 0) {
        printf("Failed: full range\n");
        free(out4);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }
    free(out4);

    /* Zero length */
    uint8_t dummy;
    r = zxc_seekable_decompress_range(s, &dummy, 1, 0, 0);
    if (r != 0) {
        printf("Failed: zero-length\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable_free(s);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_non_seekable_reject() {
    printf("=== TEST: Seekable - Non-Seekable Archive Rejected ===\n");

    const size_t SRC_SIZE = 10000;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 11);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE);
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 0};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (s) {
        printf("Failed: expected NULL for non-seekable\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_single_block() {
    printf("=== TEST: Seekable - Single Block ===\n");

    const size_t SRC_SIZE = 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 55);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 256;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        return 0;
    }
    if (zxc_seekable_get_num_blocks(s) != 1) {
        printf("Failed: expected 1 block\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    uint8_t out[100];
    int64_t r = zxc_seekable_decompress_range(s, out, 100, 500, 100);
    if (r != 100 || memcmp(src + 500, out, 100) != 0) {
        printf("Failed: range data\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable_free(s);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_all_levels() {
    printf("=== TEST: Seekable - All Compression Levels ===\n");

    const size_t SRC_SIZE = 128 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 33);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) {
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    for (int lvl = 1; lvl <= 5; lvl++) {
        zxc_compress_opts_t opts = {.level = lvl, .block_size = 32 * 1024, .seekable = 1};
        const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
        if (csize <= 0) {
            printf("Failed: compress level %d\n", lvl);
            free(src);
            free(dst);
            free(dec);
            return 0;
        }

        zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
        if (!s) {
            printf("Failed: open level %d\n", lvl);
            free(src);
            free(dst);
            free(dec);
            return 0;
        }

        int64_t r = zxc_seekable_decompress_range(s, dec, SRC_SIZE, 0, SRC_SIZE);
        if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
            printf("Failed: level %d data mismatch (r=%lld expected=%zu)\n", lvl, (long long)r,
                   SRC_SIZE);
            if (r == (int64_t)SRC_SIZE) {
                for (size_t i = 0; i < SRC_SIZE; i++) {
                    if (src[i] != dec[i]) {
                        printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n", i, src[i],
                               dec[i]);
                        break;
                    }
                }
            }
            zxc_seekable_free(s);
            free(src);
            free(dst);
            free(dec);
            return 0;
        }
        zxc_seekable_free(s);
    }

    free(src);
    free(dst);
    free(dec);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_many_blocks() {
    printf("=== TEST: Seekable - Many Small Blocks ===\n");

    /* Use minimum block size (4KB) with 256KB data => 64 blocks.
     * This stresses the seekable block tracking array (dispatch lines 410-424)
     * and ensures the seek table handles high block counts correctly. */
    const size_t SRC_SIZE = 256 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 77);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 4096;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) {
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Compress with minimum block_size = 4096 */
    zxc_compress_opts_t opts = {
        .level = ZXC_LEVEL_DEFAULT, .block_size = 4096, .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress (%lld)\n", (long long)csize);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Open and verify block count = 64 */
    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    const uint32_t n_blocks = zxc_seekable_get_num_blocks(s);
    if (n_blocks != 64) {
        printf("Failed: expected 64 blocks, got %u\n", n_blocks);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Full decompress via seekable API */
    int64_t r = zxc_seekable_decompress_range(s, dec, SRC_SIZE, 0, SRC_SIZE);
    if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: full decompress mismatch (r=%lld)\n", (long long)r);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Random access: read 100 bytes from the middle of block 32 */
    const uint64_t mid_off = 32 * 4096 + 2000;
    uint8_t spot[100];
    r = zxc_seekable_decompress_range(s, spot, 100, mid_off, 100);
    if (r != 100 || memcmp(src + mid_off, spot, 100) != 0) {
        printf("Failed: random access at offset %llu\n", (unsigned long long)mid_off);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Cross-block read: span block boundary (last 50B of block 15 + first 50B of block 16) */
    const uint64_t cross_off = 16 * 4096 - 50;
    uint8_t cross[100];
    r = zxc_seekable_decompress_range(s, cross, 100, cross_off, 100);
    if (r != 100 || memcmp(src + cross_off, cross, 100) != 0) {
        printf("Failed: cross-block read at offset %llu\n", (unsigned long long)cross_off);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_seekable_free(s);
    free(src);
    free(dst);
    free(dec);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_open_file() {
    printf("=== TEST: Seekable - Open File ===\n");

    /* Compress seekable data into a buffer, write to tmpfile, then open via file API */
    const size_t SRC_SIZE = 128 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 99);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) {
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_compress_opts_t opts = {
        .level = ZXC_LEVEL_DEFAULT, .block_size = 32 * 1024, .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress (%lld)\n", (long long)csize);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Write compressed data to a temp file */
    FILE* tf = tmpfile();
    if (!tf) {
        printf("Failed: tmpfile\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }
    if (fwrite(dst, 1, (size_t)csize, tf) != (size_t)csize) {
        printf("Failed: fwrite\n");
        fclose(tf);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }
    fflush(tf);

    /* Open via file API */
    zxc_seekable* s = zxc_seekable_open_file(tf);
    if (!s) {
        printf("Failed: zxc_seekable_open_file returned NULL\n");
        fclose(tf);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Verify block count: 128KB / 32KB = 4 blocks */
    const uint32_t n_blocks = zxc_seekable_get_num_blocks(s);
    if (n_blocks != 4) {
        printf("Failed: expected 4 blocks, got %u\n", n_blocks);
        zxc_seekable_free(s);
        fclose(tf);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Full decompress from file */
    int64_t r = zxc_seekable_decompress_range(s, dec, SRC_SIZE, 0, SRC_SIZE);
    if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: full decompress from file (r=%lld)\n", (long long)r);
        zxc_seekable_free(s);
        fclose(tf);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Random access from file: read 200 bytes spanning block boundary */
    const uint64_t cross_off = 32 * 1024 - 100; /* last 100B of block 0 + first 100B of block 1 */
    uint8_t cross[200];
    r = zxc_seekable_decompress_range(s, cross, 200, cross_off, 200);
    if (r != 200 || memcmp(src + cross_off, cross, 200) != 0) {
        printf("Failed: cross-block read from file\n");
        zxc_seekable_free(s);
        fclose(tf);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_seekable_free(s);
    fclose(tf);

    /* NULL input rejection */
    if (zxc_seekable_open_file(NULL)) {
        printf("Failed: NULL not rejected\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    free(src);
    free(dst);
    free(dec);
    printf("PASS\n\n");
    return 1;
}

/* Cross-boundary range: decompresses bytes that span exactly two blocks */
int test_seekable_cross_boundary() {
    printf("=== TEST: Seekable - Cross-Boundary Range ===\n");

    const size_t BLK = 64 * 1024;
    const size_t SRC_SIZE = BLK * 4; /* 4 blocks */
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 123);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 3, .block_size = BLK, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        return 0;
    }

    /* Read 200 bytes starting 100 bytes before the block 0/1 boundary */
    const uint64_t boundary = BLK;
    const uint64_t off = boundary - 100;
    const size_t len = 200;
    uint8_t out[200];

    int64_t r = zxc_seekable_decompress_range(s, out, sizeof(out), off, len);
    if (r != (int64_t)len) {
        printf("Failed: cross-boundary range returned %lld\n", (long long)r);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }
    if (memcmp(out, src + off, len) != 0) {
        printf("Failed: cross-boundary data mismatch\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* Also test a range spanning 3 blocks */
    const uint64_t off3 = BLK * 2 - 100;
    const size_t len3 = BLK + 200;
    uint8_t* out3 = malloc(len3);
    if (!out3) {
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    r = zxc_seekable_decompress_range(s, out3, len3, off3, len3);
    if (r != (int64_t)len3 || memcmp(out3, src + off3, len3) != 0) {
        printf("Failed: 3-block span mismatch\n");
        free(out3);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    free(out3);
    zxc_seekable_free(s);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

/* Open with truncated data should return NULL */
int test_seekable_truncated_input() {
    printf("=== TEST: Seekable - Truncated Input Rejected ===\n");

    const size_t SRC_SIZE = 64 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 44);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 256;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    /* Truncate to half */
    zxc_seekable* s = zxc_seekable_open(dst, (size_t)(csize / 2));
    if (s) {
        printf("Failed: should reject truncated data\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* Truncate to just header */
    s = zxc_seekable_open(dst, 16);
    if (s) {
        printf("Failed: should reject header-only data\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* Zero bytes */
    s = zxc_seekable_open(dst, 0);
    if (s) {
        printf("Failed: should reject zero-length data\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

/* Corrupted SEK block: ensure no crash (no UB) */
int test_seekable_corrupted_sek() {
    printf("=== TEST: Seekable - Corrupted SEK Block ===\n");

    const size_t SRC_SIZE = 64 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 66);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 256;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    /* Corrupt a byte in the SEK payload area (before footer) */
    uint8_t* corrupt = malloc((size_t)csize);
    if (!corrupt) {
        free(src);
        free(dst);
        return 0;
    }
    memcpy(corrupt, dst, (size_t)csize);
    corrupt[csize - 14] ^= 0xFF;

    zxc_seekable* s = zxc_seekable_open(corrupt, (size_t)csize);
    /* May succeed or fail - just ensure no crash */
    if (s) {
        uint8_t out[100];
        (void)zxc_seekable_decompress_range(s, out, sizeof(out), 0, 100);
        zxc_seekable_free(s);
    }

    free(corrupt);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

/* Range beyond file end should return error */
int test_seekable_range_out_of_bounds() {
    printf("=== TEST: Seekable - Out-of-Bounds Range ===\n");

    const size_t SRC_SIZE = 32 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 22);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 256;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        return 0;
    }

    uint8_t out[256];
    /* offset past EOF */
    int64_t r = zxc_seekable_decompress_range(s, out, sizeof(out), SRC_SIZE + 100, 100);
    if (r > 0) {
        printf("Failed: should reject offset past EOF (got %lld)\n", (long long)r);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* offset valid but length extends past EOF */
    r = zxc_seekable_decompress_range(s, out, sizeof(out), SRC_SIZE - 50, 200);
    if (r > 0) {
        printf("Failed: should reject range extending past EOF (got %lld)\n", (long long)r);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable_free(s);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

/* dst_capacity too small for requested range */
int test_seekable_dst_too_small() {
    printf("=== TEST: Seekable - Dst Too Small ===\n");

    const size_t SRC_SIZE = 32 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 91);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 256;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        return 0;
    }

    uint8_t out[10];
    int64_t r = zxc_seekable_decompress_range(s, out, 10, 0, 1000);
    if (r > 0) {
        printf("Failed: should reject insufficient dst capacity (got %lld)\n", (long long)r);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable_free(s);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

/* Empty file with seekable=1 */
/* Empty file with seekable=1: buffer API rejects NULL src, verify graceful rejection.
 * Also verify via streaming API (which supports empty files). */
int test_seekable_empty_file() {
    printf("=== TEST: Seekable - Empty File ===\n");

    const size_t dst_cap = (size_t)zxc_compress_bound(0) + 256;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) return 0;

    /* Buffer API: NULL src with size 0 produces a valid empty frame */
    zxc_compress_opts_t opts = {.level = 3, .seekable = 1};
    const int64_t csize = zxc_compress(NULL, 0, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: expected valid empty frame (got %lld)\n", (long long)csize);
        free(dst);
        return 0;
    }
    const uint64_t orig = zxc_get_decompressed_size(dst, (size_t)csize);
    if (orig != 0) {
        printf("Failed: empty frame size should be 0 (got %llu)\n", (unsigned long long)orig);
        free(dst);
        return 0;
    }

    /* Streaming API: empty file via tmpfile() should work */
    FILE* fin = tmpfile();
    FILE* fout = tmpfile();
    if (fin && fout) {
        int64_t stream_sz = zxc_stream_compress(fin, fout, &opts);
        if (stream_sz < 0) {
            printf("Failed: stream compress empty (got %lld)\n", (long long)stream_sz);
            fclose(fin);
            fclose(fout);
            free(dst);
            return 0;
        }
        /* Decompress the stream output */
        rewind(fout);
        FILE* fdec = tmpfile();
        if (fdec) {
            zxc_decompress_opts_t dopts = {.checksum_enabled = 0};
            int64_t dsz = zxc_stream_decompress(fout, fdec, &dopts);
            if (dsz != 0) {
                printf("Failed: stream decompress empty should return 0 (got %lld)\n",
                       (long long)dsz);
                fclose(fin);
                fclose(fout);
                fclose(fdec);
                free(dst);
                return 0;
            }
            fclose(fdec);
        }
        fclose(fin);
        fclose(fout);
    }

    free(dst);
    printf("PASS\n\n");
    return 1;
}

/* Seekable without checksum (seekable=1, checksum_enabled=0) */
int test_seekable_no_checksum() {
    printf("=== TEST: Seekable - No Checksum ===\n");

    const size_t SRC_SIZE = 256 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 31);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {
        .level = 3, .block_size = 64 * 1024, .seekable = 1, .checksum_enabled = 0};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        return 0;
    }

    uint8_t out[512];
    const uint64_t off = 64 * 1024 + 100;
    int64_t r = zxc_seekable_decompress_range(s, out, sizeof(out), off, 512);
    if (r != 512 || memcmp(out, src + off, 512) != 0) {
        printf("Failed: no-checksum range mismatch\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* Asking to verify an archive that carries no checksums must stay a no-op.
     * Without the file_has_checksums half of the guard the decoder would read
     * the next block's header as a stored checksum and reject every block. */
    zxc_seekable_set_checksum(s, 1);
    r = zxc_seekable_decompress_range(s, out, sizeof(out), off, 512);
    if (r != 512 || memcmp(out, src + off, 512) != 0) {
        printf("Failed: set_checksum(1) on a checksum-less archive -> %lld\n", (long long)r);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }
    zxc_seekable_set_checksum(s, 0);

    /* Full decompress also works */
    uint8_t* full = malloc(SRC_SIZE);
    if (!full) {
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }
    zxc_decompress_opts_t dopts = {.checksum_enabled = 0};
    int64_t dsize = zxc_decompress(dst, (size_t)csize, full, SRC_SIZE, &dopts);
    if (dsize != (int64_t)SRC_SIZE || memcmp(src, full, SRC_SIZE) != 0) {
        printf("Failed: full decompress mismatch\n");
        free(full);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    free(full);
    zxc_seekable_free(s);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

/* ========================================================================= */
/*  Reader-callback API (zxc_seekable_open_reader)                           */
/* ========================================================================= */

/* In-memory reader: counts invocations so we can assert lazy I/O. */
typedef struct {
    const uint8_t* data;
    uint64_t size;
    int call_count;
    uint64_t bytes_read;
} reader_test_ctx_t;

static int64_t reader_test_read_at(void* ctx, void* dst, size_t len, uint64_t offset) {
    reader_test_ctx_t* m = (reader_test_ctx_t*)ctx;
    m->call_count++;
    if (offset > m->size || len > m->size - offset) return -1;
    memcpy(dst, m->data + offset, len);
    m->bytes_read += (uint64_t)len;
    return (int64_t)len;
}

/* Thread-safe in-memory reader for the MT test. Holds no mutable state, so
 * concurrent invocations from worker threads are race-free by construction. */
typedef struct {
    const uint8_t* data;
    uint64_t size;
} reader_test_ctx_mt_t;

// cppcheck-suppress constParameterCallback
static int64_t reader_test_read_at_mt(void* ctx, void* dst, size_t len, uint64_t offset) {
    const reader_test_ctx_mt_t* m = (const reader_test_ctx_mt_t*)ctx;
    if (offset > m->size || len > m->size - offset) return -1;
    memcpy(dst, m->data + offset, len);
    return (int64_t)len;
}

/* Reader that always reports a short read - must be rejected at open(). */
static int64_t reader_short_read_at(void* ctx, void* dst, size_t len, uint64_t offset) {
    (void)ctx;
    (void)dst;
    (void)offset;
    return (int64_t)(len > 0 ? len - 1 : 0);
}

/* Reader that returns a negative error code - must propagate. */
static int64_t reader_error_read_at(void* ctx, void* dst, size_t len, uint64_t offset) {
    (void)ctx;
    (void)dst;
    (void)len;
    (void)offset;
    return ZXC_ERROR_IO;
}

/* Open and roundtrip via a user-supplied callback reader (single-threaded). */
int test_seekable_open_reader() {
    printf("=== TEST: Seekable - Open Reader Callback ===\n");

    const size_t SRC_SIZE = 256 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 211);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) {
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_compress_opts_t opts = {
        .level = 3, .block_size = 64 * 1024, .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress (%lld)\n", (long long)csize);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Open through the reader callback */
    reader_test_ctx_t mctx = {
        .data = dst, .size = (uint64_t)csize, .call_count = 0, .bytes_read = 0};
    zxc_reader_t r = {.read_at = reader_test_read_at, .ctx = &mctx, .size = (uint64_t)csize};

    zxc_seekable* s = zxc_seekable_open_reader(&r);
    if (!s) {
        printf("Failed: zxc_seekable_open_reader returned NULL\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* open() should perform exactly 3 reads: header, footer, EOF + SEK headers. */
    if (mctx.call_count != 3) {
        printf("Failed: expected 3 reads at open, got %d\n", mctx.call_count);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* 256KB / 64KB = 4 blocks */
    if (zxc_seekable_get_num_blocks(s) != 4) {
        printf("Failed: expected 4 blocks, got %u\n", zxc_seekable_get_num_blocks(s));
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }
    if (zxc_seekable_get_decompressed_size(s) != SRC_SIZE) {
        printf("Failed: bad total decompressed size\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Full-range decompress */
    int64_t n = zxc_seekable_decompress_range(s, dec, SRC_SIZE, 0, SRC_SIZE);
    if (n != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: full range mismatch (n=%lld)\n", (long long)n);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Cross-block sub-range */
    const uint64_t off = 64 * 1024 - 200;
    const size_t len = 400;
    uint8_t cross[400];
    n = zxc_seekable_decompress_range(s, cross, len, off, len);
    if (n != (int64_t)len || memcmp(cross, src + off, len) != 0) {
        printf("Failed: cross-block sub-range mismatch\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    // Lazy I/O: a sub-range inside block 2 costs exactly 2 reads, its entries
    // then the block. read_at is reached through a function pointer cppcheck
    // cannot follow, so it folds the comparison to a constant; suppress the two
    // false positives.
    const int before = mctx.call_count;
    n = zxc_seekable_decompress_range(s, cross, len, 128 * 1024 + 10, len);
    // cppcheck-suppress duplicateExpression
    const int delta = mctx.call_count - before;
    // cppcheck-suppress knownConditionTrueFalse
    if (n != (int64_t)len || delta != 2) {
        printf("Failed: single-block read should trigger 2 read_at (got %d)\n", delta);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_seekable_free(s);

    /* NULL reader rejection */
    if (zxc_seekable_open_reader(NULL)) {
        printf("Failed: NULL reader not rejected\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Missing read_at rejection */
    zxc_reader_t bad = {.read_at = NULL, .ctx = NULL, .size = 100};
    if (zxc_seekable_open_reader(&bad)) {
        printf("Failed: NULL read_at not rejected\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Zero size rejection */
    zxc_reader_t empty = {.read_at = reader_test_read_at, .ctx = &mctx, .size = 0};
    if (zxc_seekable_open_reader(&empty)) {
        printf("Failed: zero size not rejected\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Short read at open: must reject */
    zxc_reader_t short_r = {.read_at = reader_short_read_at, .ctx = NULL, .size = (uint64_t)csize};
    if (zxc_seekable_open_reader(&short_r)) {
        printf("Failed: short-read reader not rejected\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Negative return at open: must reject */
    zxc_reader_t err_r = {.read_at = reader_error_read_at, .ctx = NULL, .size = (uint64_t)csize};
    if (zxc_seekable_open_reader(&err_r)) {
        printf("Failed: error-returning reader not rejected\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    free(src);
    free(dst);
    free(dec);
    printf("PASS\n\n");
    return 1;
}

/* Multi-threaded decompression through a reader callback.
 * The callback only does memcpy on const data, so it is naturally thread-safe. */
int test_seekable_open_reader_mt() {
    printf("=== TEST: Seekable - Open Reader Callback (MT) ===\n");

    const size_t SRC_SIZE = 1 * 1024 * 1024; /* 1 MB => 16 x 64KB blocks */
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 137);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t* dec = malloc(SRC_SIZE);
    if (!dst || !dec) {
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_compress_opts_t opts = {
        .level = 3, .block_size = 64 * 1024, .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress (%lld)\n", (long long)csize);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    /* Use the stateless reader: worker threads call it concurrently. */
    reader_test_ctx_mt_t mctx = {.data = dst, .size = (uint64_t)csize};
    zxc_reader_t r = {.read_at = reader_test_read_at_mt, .ctx = &mctx, .size = (uint64_t)csize};

    zxc_seekable* s = zxc_seekable_open_reader(&r);
    if (!s) {
        printf("Failed: open_reader\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    int64_t n = zxc_seekable_decompress_range_mt(s, dec, SRC_SIZE, 0, SRC_SIZE, 4);
    if (n != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: MT full range mismatch (n=%lld)\n", (long long)n);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_seekable_free(s);
    free(src);
    free(dst);
    free(dec);
    printf("PASS\n\n");
    return 1;
}

/* Two ways zxc_seekable_decompress_range used to report success without filling
 * dst, handing the caller whatever its buffer already held. */
int test_seekable_range_reports_short_reads(void) {
    printf("=== TEST: Seekable - a range that cannot be filled is refused ===\n");

    const size_t SRC_SIZE = 256 * 1024;
    const size_t BLK = 64 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    uint32_t rng = 0x2E5B9A17u;
    for (size_t i = 0; i < SRC_SIZE; i++) {
        rng = rng * 1103515245u + 12345u;
        src[i] = (uint8_t)(rng >> 16);
    }
    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    uint8_t out[2048];
    int ok = 0;
    do {
        if (!dst) break;
        zxc_compress_opts_t opts = {.level = 3, .block_size = BLK, .seekable = 1};
        const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
        if (csize <= 0) {
            printf("  [FAIL] compress -> %lld\n", (long long)csize);
            break;
        }

        /* An offset so large that offset + len wraps: the range guard used to
         * pass and the call returned len with dst untouched. */
        zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
        if (!s) {
            printf("  [FAIL] open\n");
            break;
        }
        memset(out, 0xAB, sizeof(out));
        const int64_t wrapped =
            zxc_seekable_decompress_range(s, out, 512, 0xFFFFFFFFFFFFFF00ULL, 512);
        const int64_t wrapped_mt =
            zxc_seekable_decompress_range_mt(s, out, 512, 0xFFFFFFFFFFFFFF00ULL, 512, 4);
        zxc_seekable_free(s);
        if (wrapped != ZXC_ERROR_SRC_TOO_SMALL || wrapped_mt != ZXC_ERROR_SRC_TOO_SMALL) {
            printf("  [FAIL] wrapping offset: st %lld, mt %lld, want SRC_TOO_SMALL\n",
                   (long long)wrapped, (long long)wrapped_mt);
            break;
        }

        /* A block whose header claims fewer bytes than the seek table budgets:
         * the loop copies short and the call used to return len anyway. */
        zxc_block_header_t bh;
        if (zxc_read_block_header(dst + ZXC_FILE_HEADER_SIZE, (size_t)csize - ZXC_FILE_HEADER_SIZE,
                                  &bh) != ZXC_OK) {
            printf("  [FAIL] could not read block 0\n");
            break;
        }
        bh.comp_size = 1000;
        zxc_write_block_header(dst + ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &bh);

        zxc_seekable* sh = zxc_seekable_open(dst, (size_t)csize);
        if (!sh) {
            printf("  [FAIL] open (short)\n");
            break;
        }
        memset(out, 0xAB, sizeof(out));
        const int64_t shortr = zxc_seekable_decompress_range(sh, out, 2000, 0, 2000);
        uint8_t* wide = malloc(BLK + 2000);
        const int64_t shortr_mt =
            wide ? zxc_seekable_decompress_range_mt(sh, wide, BLK + 2000, 0, BLK + 2000, 4) : 0;
        free(wide);
        zxc_seekable_free(sh);
        if (!wide) break;
        if (shortr != ZXC_ERROR_CORRUPT_DATA || shortr_mt != ZXC_ERROR_CORRUPT_DATA) {
            printf("  [FAIL] short block: st %lld, mt %lld, want CORRUPT_DATA\n", (long long)shortr,
                   (long long)shortr_mt);
            break;
        }
        ok = 1;
    } while (0);

    free(src);
    free(dst);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* Random access never verified per-block checksums: both paths carved their
 * context with checksum_enabled = 0 and file_has_checksums was never read, so
 * zxc_seekable_set_checksum had nothing to switch. Data is incompressible on
 * purpose: a RAW block memcpys a flipped byte straight through, so only the
 * checksum catches it. */
int test_seekable_corrupted_block_checksum(void) {
    printf("=== TEST: Seekable - opting into checksums catches a corrupted block ===\n");

    const size_t SRC_SIZE = 256 * 1024;
    const size_t BLK = 64 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    uint32_t rng = 0x2E5B9A17u;
    for (size_t i = 0; i < SRC_SIZE; i++) {
        rng = rng * 1103515245u + 12345u;
        src[i] = (uint8_t)(rng >> 16);
    }

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }
    zxc_compress_opts_t opts = {
        .level = 3, .block_size = BLK, .seekable = 1, .checksum_enabled = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    int ok = 0;
    uint8_t out[512], out_bad[512];
    do {
        if (csize <= 0) {
            printf("Failed: compress -> %lld\n", (long long)csize);
            break;
        }
        /* Block 1 via the seek table; flip a byte past its 8-byte header. */
        zxc_seekable* probe = zxc_seekable_open(dst, (size_t)csize);
        if (!probe || zxc_seekable_get_num_blocks(probe) < 2) {
            printf("Failed: need at least 2 blocks\n");
            zxc_seekable_free(probe);
            break;
        }
        const size_t off1 = ZXC_FILE_HEADER_SIZE + zxc_seekable_get_block_comp_size(probe, 0);
        zxc_seekable_free(probe);
        /* A fixture failure, not a checksum failure: the payload is an LCG, so
         * the encoder should always store it RAW. Skipping here would remove
         * the only coverage of silent corruption, so fail loudly instead. */
        if (dst[off1] != ZXC_BLOCK_RAW) {
            printf(
                "  [FAIL] fixture: block 1 is type %u, expected RAW; the LCG payload is no "
                "longer incompressible, pick another\n",
                dst[off1]);
            break;
        }
        dst[off1 + ZXC_BLOCK_HEADER_SIZE + 4] ^= 0xFF;

        zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
        if (!s) {
            printf("Failed: open\n");
            break;
        }
        /* Default is off: the corrupted RAW block is handed back as-is. */
        const int64_t unchecked =
            zxc_seekable_decompress_range(s, out_bad, sizeof(out_bad), BLK, sizeof(out_bad));
        /* Opting in refuses it, on a context already carved. */
        const int64_t enabled_rc = zxc_seekable_set_checksum(s, 1);
        const int64_t checked =
            zxc_seekable_decompress_range(s, out_bad, sizeof(out_bad), BLK, sizeof(out_bad));
        /* Intact block, still verifying, must decode byte-exact. */
        const int64_t good = zxc_seekable_decompress_range(s, out, sizeof(out), 0, sizeof(out));
        /* And opting back out accepts it again. */
        const int64_t off_again = zxc_seekable_set_checksum(s, 0);
        const int64_t unchecked2 =
            zxc_seekable_decompress_range(s, out_bad, sizeof(out_bad), BLK, sizeof(out_bad));
        zxc_seekable_free(s);

        if (unchecked != (int64_t)sizeof(out_bad) || unchecked2 != (int64_t)sizeof(out_bad)) {
            printf("Failed: verification off should accept the block, got %lld then %lld\n",
                   (long long)unchecked, (long long)unchecked2);
            break;
        }
        if (enabled_rc != ZXC_OK || off_again != ZXC_OK) {
            printf("Failed: set_checksum -> %lld, %lld\n", (long long)enabled_rc,
                   (long long)off_again);
            break;
        }
        if (checked != ZXC_ERROR_BAD_CHECKSUM) {
            printf("Failed: verifying -> %lld, want ZXC_ERROR_BAD_CHECKSUM\n", (long long)checked);
            break;
        }
        if (good != (int64_t)sizeof(out) || memcmp(out, src, sizeof(out)) != 0) {
            printf("Failed: intact block 0 -> %lld\n", (long long)good);
            break;
        }
        if (zxc_seekable_set_checksum(NULL, 0) != ZXC_ERROR_NULL_INPUT) {
            printf("Failed: set_checksum(NULL) should be NULL_INPUT\n");
            break;
        }

        /* Opting in before the first call goes through the carve instead of the
         * live update, so it needs its own handle to be covered. */
        zxc_seekable* early = zxc_seekable_open(dst, (size_t)csize);
        if (!early) {
            printf("Failed: open (early)\n");
            break;
        }
        zxc_seekable_set_checksum(early, 1);
        const int64_t at_carve =
            zxc_seekable_decompress_range(early, out_bad, sizeof(out_bad), BLK, sizeof(out_bad));
        zxc_seekable_free(early);
        if (at_carve != ZXC_ERROR_BAD_CHECKSUM) {
            printf("Failed: opted in before first use -> %lld, want BAD_CHECKSUM\n",
                   (long long)at_carve);
            break;
        }

        /* The MT path carves its own context, and falls back to ST inside one
         * block: hence a range over blocks 0 to 2. */
        zxc_seekable* smt = zxc_seekable_open(dst, (size_t)csize);
        if (!smt) {
            printf("Failed: open (mt)\n");
            break;
        }
        const size_t span = 3 * BLK;
        uint8_t* wide = malloc(span);
        if (!wide) {
            zxc_seekable_free(smt);
            break;
        }
        zxc_seekable_set_checksum(smt, 1);
        const int64_t bad_mt = zxc_seekable_decompress_range_mt(smt, wide, span, 0, span, 4);
        free(wide);
        zxc_seekable_free(smt);
        if (bad_mt != ZXC_ERROR_BAD_CHECKSUM) {
            printf("Failed: mt range over the corrupted block -> %lld, want BAD_CHECKSUM\n",
                   (long long)bad_mt);
            break;
        }
        ok = 1;
    } while (0);

    free(src);
    free(dst);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* Seekable with checksum (seekable=1, checksum_enabled=1) */
int test_seekable_with_checksum() {
    printf("=== TEST: Seekable - With Checksum ===\n");

    const size_t SRC_SIZE = 256 * 1024;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    fill_seek_data(src, SRC_SIZE, 47);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 1024;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }

    zxc_compress_opts_t opts = {
        .level = 3, .block_size = 64 * 1024, .seekable = 1, .checksum_enabled = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(src);
        free(dst);
        return 0;
    }

    /* Seekable random access */
    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: open\n");
        free(src);
        free(dst);
        return 0;
    }

    /* Range from block 0 */
    uint8_t out[512];
    int64_t r = zxc_seekable_decompress_range(s, out, sizeof(out), 0, 512);
    if (r != 512 || memcmp(out, src, 512) != 0) {
        printf("Failed: checksum range head mismatch\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* Range spanning blocks 1-2 */
    const uint64_t off = 64 * 1024 + 100;
    r = zxc_seekable_decompress_range(s, out, sizeof(out), off, 512);
    if (r != 512 || memcmp(out, src + off, 512) != 0) {
        printf("Failed: checksum range mid mismatch\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    /* Full decompress with checksum verification */
    uint8_t* full = malloc(SRC_SIZE);
    if (!full) {
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }
    zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
    int64_t dsize = zxc_decompress(dst, (size_t)csize, full, SRC_SIZE, &dopts);
    if (dsize != (int64_t)SRC_SIZE || memcmp(src, full, SRC_SIZE) != 0) {
        printf("Failed: full decompress with checksum mismatch\n");
        free(full);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        return 0;
    }

    free(full);
    zxc_seekable_free(s);
    free(src);
    free(dst);
    printf("PASS\n\n");
    return 1;
}

int test_seekable_work_buf_tail_pad(void) {
    printf("=== TEST: Seekable - full-size blocks at default block_size ===\n");

    const size_t SRC_SIZE = 1536 * 1024;
    const size_t BLK = 512 * 1024;

    uint8_t* const src = (uint8_t*)malloc(SRC_SIZE);
    if (!src) {
        printf("Failed: malloc src\n");
        return 0;
    }
    gen_lz_data(src, SRC_SIZE);

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 4096;
    uint8_t* const dst = (uint8_t*)malloc(dst_cap);
    uint8_t* const dec = (uint8_t*)malloc(SRC_SIZE);
    if (!dst || !dec) {
        printf("Failed: malloc dst/dec\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_compress_opts_t opts = {
        .level = ZXC_LEVEL_DEFAULT, .block_size = BLK, .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress returned %lld\n", (long long)csize);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    zxc_seekable* const s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) {
        printf("Failed: zxc_seekable_open returned NULL\n");
        free(src);
        free(dst);
        free(dec);
        return 0;
    }
    if (zxc_seekable_get_num_blocks(s) < 2) {
        printf("Failed: need >= 2 full blocks, got %u\n", zxc_seekable_get_num_blocks(s));
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    const int64_t r = zxc_seekable_decompress_range(s, dec, SRC_SIZE, 0, SRC_SIZE);
    if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: full-range decompress returned %lld (expected %zu)\n", (long long)r,
               SRC_SIZE);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    const uint64_t mid_off = 200 * 1024;
    const size_t mid_len = 100 * 1024;
    uint8_t* const mid = (uint8_t*)malloc(mid_len);
    if (!mid) {
        printf("Failed: malloc mid\n");
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }
    const int64_t r2 = zxc_seekable_decompress_range(s, mid, mid_len, mid_off, mid_len);
    if (r2 != (int64_t)mid_len || memcmp(src + mid_off, mid, mid_len) != 0) {
        printf("Failed: mid-block decompress returned %lld (expected %zu)\n", (long long)r2,
               mid_len);
        free(mid);
        zxc_seekable_free(s);
        free(src);
        free(dst);
        free(dec);
        return 0;
    }

    free(mid);
    zxc_seekable_free(s);
    free(src);
    free(dst);
    free(dec);
    printf("PASS\n\n");
    return 1;
}

/*
 * A seek-table entry spans exactly one block, so it cannot exceed a block
 * header plus a full block plus its checksum. Inflating one entry and
 * deflating the next by the same amount keeps the prefix sum landing on the
 * EOF block, so only the per-entry bound can reject the table.
 */
/* 2^30 + 5 RAW blocks of 4 KiB served by a callback, four terabytes that never
 * exist: the old 2^30 cap is gone, open costs three reads, nothing is loaded. */
#define SYNTH_BS 4096u
#define SYNTH_BLK (ZXC_BLOCK_HEADER_SIZE + SYNTH_BS)

typedef struct {
    uint64_t n;       /* block count */
    uint64_t eof_off; /* EOF block header: 16 + n * SYNTH_BLK */
    uint64_t size;    /* whole archive */
    uint8_t file_hdr[ZXC_FILE_HEADER_SIZE];
    uint8_t blk_hdr[ZXC_BLOCK_HEADER_SIZE]; /* every block is RAW, full, same header */
    uint8_t eof_hdr[ZXC_BLOCK_HEADER_SIZE];
    uint8_t sek_hdr[ZXC_BLOCK_HEADER_SIZE];
    uint8_t footer[ZXC_FILE_FOOTER_SIZE];
    int counting; /* cleared before the multi-threaded read: workers only read it */
    int calls;
} synth_ctx_t;

static uint8_t synth_payload(const uint64_t blk, const uint32_t pos) {
    return (uint8_t)(blk * 7u + (blk >> 11) + pos * 13u);
}

static uint8_t synth_byte(const synth_ctx_t* c, uint64_t off) {
    if (off < ZXC_FILE_HEADER_SIZE) return c->file_hdr[off];
    if (off < c->eof_off) {
        const uint64_t rel = off - ZXC_FILE_HEADER_SIZE;
        const uint32_t in = (uint32_t)(rel % SYNTH_BLK);
        return in < ZXC_BLOCK_HEADER_SIZE
                   ? c->blk_hdr[in]
                   : synth_payload(rel / SYNTH_BLK, in - ZXC_BLOCK_HEADER_SIZE);
    }
    off -= c->eof_off;
    if (off < ZXC_BLOCK_HEADER_SIZE) return c->eof_hdr[off];
    off -= ZXC_BLOCK_HEADER_SIZE;
    if (off < ZXC_BLOCK_HEADER_SIZE) return c->sek_hdr[off];
    off -= ZXC_BLOCK_HEADER_SIZE;
    if (off < c->n * ZXC_SEEK_ENTRY_SIZE) { /* entry i = 16 + i * SYNTH_BLK, LE */
        const uint64_t v = ZXC_FILE_HEADER_SIZE + (off / ZXC_SEEK_ENTRY_SIZE) * SYNTH_BLK;
        return (uint8_t)(v >> (8 * (off % ZXC_SEEK_ENTRY_SIZE)));
    }
    off -= c->n * ZXC_SEEK_ENTRY_SIZE;
    return c->footer[off];
}

static int64_t synth_read_at(void* ctx, void* dst, size_t len, uint64_t offset) {
    synth_ctx_t* c = (synth_ctx_t*)ctx;
    if (c->counting) c->calls++;
    if (offset > c->size || len > c->size - offset) return -1;
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < len; i++) d[i] = synth_byte(c, offset + i);
    return (int64_t)len;
}

int test_seekable_beyond_old_cap(void) {
    printf("=== TEST: Seekable - 2^30 + 5 blocks through a callback, nothing materialised ===\n");
    if (sizeof(size_t) < 8) {
        printf("  [SKIP] 64-bit hosts only\n\n");
        return 1;
    }
    synth_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.n = (1ULL << 30) + 5;
    c.counting = 1;
    c.eof_off = ZXC_FILE_HEADER_SIZE + c.n * SYNTH_BLK;
    c.size =
        c.eof_off + 2 * ZXC_BLOCK_HEADER_SIZE + c.n * ZXC_SEEK_ENTRY_SIZE + ZXC_FILE_FOOTER_SIZE;
    const uint64_t total = c.n * SYNTH_BS;
    const zxc_block_header_t raw = {
        .block_type = ZXC_BLOCK_RAW, .block_flags = 0, .reserved = 0, .comp_size = SYNTH_BS};
    const zxc_block_header_t eof = {
        .block_type = ZXC_BLOCK_EOF, .block_flags = 0, .reserved = 0, .comp_size = 0};
    if (zxc_write_file_header(c.file_hdr, sizeof(c.file_hdr), SYNTH_BS, 0, 0) < 0 ||
        zxc_write_block_header(c.blk_hdr, sizeof(c.blk_hdr), &raw) < 0 ||
        zxc_write_block_header(c.eof_hdr, sizeof(c.eof_hdr), &eof) < 0 ||
        zxc_seek_table_header(c.sek_hdr, sizeof(c.sek_hdr), (uint32_t)c.n) < 0 ||
        zxc_write_file_footer(c.footer, sizeof(c.footer), total, 0, 0) < 0) {
        printf("  [FAIL] fixture headers\n");
        return 0;
    }

    const zxc_reader_t r = {.read_at = synth_read_at, .ctx = &c, .size = c.size};
    zxc_seekable* s = zxc_seekable_open_reader(&r);
    uint8_t got[32];
    uint8_t want[32];
    int ok = 0;
    do {
        if (!s || c.calls != 3) {
            printf("  [FAIL] open: %s, %d reads\n", s ? "ok" : "NULL", c.calls);
            break;
        }
        if (zxc_seekable_get_num_blocks(s) != (uint32_t)c.n ||
            zxc_seekable_get_decompressed_size(s) != total) {
            printf("  [FAIL] geometry\n");
            break;
        }
        /* The archive's last 32 bytes: block 2^30 + 4, above the old cap. */
        for (int i = 0; i < 32; i++) want[i] = synth_payload(c.n - 1, SYNTH_BS - 32 + (uint32_t)i);
        if (zxc_seekable_decompress_range(s, got, sizeof(got), total - 32, 32) != 32 ||
            memcmp(got, want, 32) != 0) {
            printf("  [FAIL] tail read\n");
            break;
        }
        /* Across blocks 2^30 and 2^30 + 1, multi-threaded. */
        c.counting = 0;
        const uint64_t hi = 1ULL << 30;
        for (int i = 0; i < 16; i++) want[i] = synth_payload(hi, SYNTH_BS - 16 + (uint32_t)i);
        for (int i = 0; i < 16; i++) want[16 + i] = synth_payload(hi + 1, (uint32_t)i);
        if (zxc_seekable_decompress_range_mt(s, got, sizeof(got), (hi + 1) * SYNTH_BS - 16, 32,
                                             2) != 32 ||
            memcmp(got, want, 32) != 0) {
            printf("  [FAIL] boundary read\n");
            break;
        }
        if (zxc_seekable_get_block_comp_size(s, 0) != SYNTH_BLK ||
            zxc_seekable_get_block_comp_size(s, (uint32_t)c.n - 1) != SYNTH_BLK) {
            printf("  [FAIL] block sizes\n");
            break;
        }
        ok = 1;
    } while (0);
    zxc_seekable_free(s);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_seekable_forged_table_entry() {
    printf("=== TEST: Seekable - Forged Table Entry ===\n");

    const size_t SRC_SIZE = 64 * 1024;
    const size_t BLOCK_SIZE = 4096;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    /* Incompressible: every block falls back to RAW and sits on the bound. */
    unsigned rng = 4242u;
    for (size_t i = 0; i < SRC_SIZE; i++) {
        rng = rng * 1103515245u + 12345u;
        src[i] = (uint8_t)(rng >> 24);
    }

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 256;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }
    zxc_compress_opts_t opts = {.level = 1, .seekable = 1, .block_size = BLOCK_SIZE};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    free(src);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(dst);
        return 0;
    }

    const uint32_t num_blocks = (uint32_t)(SRC_SIZE / BLOCK_SIZE);
    /* [file header][blocks][EOF][SEK header + N*8][footer] */
    uint8_t* const entries =
        dst + csize - ZXC_FILE_FOOTER_SIZE - (size_t)num_blocks * ZXC_SEEK_ENTRY_SIZE;
    uint8_t* const entry1 = entries + ZXC_SEEK_ENTRY_SIZE;
    uint8_t* const last = entries + (size_t)(num_blocks - 1) * ZXC_SEEK_ENTRY_SIZE;
    const uint64_t entry_max = ZXC_BLOCK_HEADER_SIZE + BLOCK_SIZE;
    const uint64_t e1 = zxc_le64(entry1);
    const uint64_t el = zxc_le64(last);
    const size_t cap = 3 * BLOCK_SIZE;
    uint8_t* const out = malloc(cap);

    int ok = out != NULL;
    if (ok &&
        (zxc_le64(entries) != ZXC_FILE_HEADER_SIZE || e1 != ZXC_FILE_HEADER_SIZE + entry_max ||
         zxc_le64(entries + 2 * ZXC_SEEK_ENTRY_SIZE) != e1 + entry_max)) {
        /* On the bound, so a one-byte push below crosses it. */
        printf("Failed: expected RAW blocks on the bound (%llu)\n", (unsigned long long)entry_max);
        ok = 0;
    }
    if (ok) {
        zxc_seekable* intact = zxc_seekable_open(dst, (size_t)csize);
        if (!intact || zxc_seekable_decompress_range(intact, out, cap, BLOCK_SIZE, 16) != 16) {
            printf("Failed: intact archive rejected\n");
            ok = 0;
        }
        zxc_seekable_free(intact);
    }

    /* Entries are checked on access, not at open: open succeeds, the bad block
     * is refused, the others decode. */
    struct {
        const char* what;
        uint8_t* at;
        uint64_t value;
        uint64_t bad_off; /* decompressed offset inside a refused block */
    } cases[] = {
        {"entry 1 pushed up: block 0 spans more than one block", entry1, e1 + 1, 0},
        {"entry 1 pulled back: offsets must increase", entry1, ZXC_FILE_HEADER_SIZE, 0},
        {"entry 0 off the file header", entries, ZXC_FILE_HEADER_SIZE + 1, 0},
        {"last entry on the EOF block: empty last block", last, el + entry_max,
         (uint64_t)(num_blocks - 1) * BLOCK_SIZE},
    };
    for (size_t k = 0; ok && k < sizeof(cases) / sizeof(cases[0]); k++) {
        const uint64_t keep = zxc_le64(cases[k].at);
        zxc_store_le64(cases[k].at, cases[k].value);
        zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
        if (!s) {
            printf("Failed: %s: open must not scan the table\n", cases[k].what);
            ok = 0;
        } else {
            const uint32_t bad_blk = (uint32_t)(cases[k].bad_off / BLOCK_SIZE);
            const int64_t st = zxc_seekable_decompress_range(s, out, cap, cases[k].bad_off, 16);
            /* Two blocks, so the multi-threaded planner loads the span. */
            const uint64_t mt_off =
                bad_blk + 1 < num_blocks ? cases[k].bad_off : cases[k].bad_off - BLOCK_SIZE;
            const int64_t mt =
                zxc_seekable_decompress_range_mt(s, out, cap, mt_off, BLOCK_SIZE + 16, 2);
            /* Block 2 is intact in every case. */
            const int64_t good = zxc_seekable_decompress_range(s, out, cap, 2 * BLOCK_SIZE, 16);
            if (st != ZXC_ERROR_CORRUPT_DATA || mt != ZXC_ERROR_CORRUPT_DATA || good != 16 ||
                zxc_seekable_get_block_comp_size(s, bad_blk) != 0 ||
                zxc_seekable_get_block_comp_size(s, 2) != entry_max) {
                printf("Failed: %s: st %lld, mt %lld, block 2 %lld, sizes %u/%u\n", cases[k].what,
                       (long long)st, (long long)mt, (long long)good,
                       zxc_seekable_get_block_comp_size(s, bad_blk),
                       zxc_seekable_get_block_comp_size(s, 2));
                ok = 0;
            }
            zxc_seekable_free(s);
        }
        zxc_store_le64(cases[k].at, keep);
    }

    free(out);
    free(dst);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* A forged entry must neither redirect a block to another's bytes nor make the
 * reader touch memory outside the archive. Both slipped through a span check
 * done with wrapping arithmetic: an entry near 2^64 wrapped `a + header`, and
 * one aliasing a neighbour passed while the doubled span fitted one block -
 * which compressible blocks guarantee. A block with genuine neighbours still decodes. */
int test_seekable_forged_entry_aliasing(void) {
    printf("=== TEST: Seekable - forged entries cannot alias or escape the archive ===\n");

    enum { BS = 4096, NB = 4 };
    uint8_t* src = malloc((size_t)BS * NB);
    if (!src) return 0;
    /* Four distinct, highly compressible blocks. */
    for (size_t b = 0; b < NB; b++)
        for (size_t i = 0; i < BS; i++) src[b * BS + i] = (uint8_t)('A' + b + (i % 7 == 0));

    const size_t cap = (size_t)zxc_compress_bound((size_t)BS * NB) + 256;
    uint8_t* arc = malloc(cap);
    uint8_t* out = malloc(2 * BS);
    int ok = 0;
    if (!arc || !out) goto done;

    zxc_compress_opts_t opts = {.level = 3, .seekable = 1, .block_size = BS, .checksum_enabled = 1};
    const int64_t csz = zxc_compress(src, (size_t)BS * NB, arc, cap, &opts);
    if (csz <= 0) {
        printf("  [FAIL] compress: %lld\n", (long long)csz);
        goto done;
    }
    uint8_t* const entries = arc + csz - ZXC_FILE_FOOTER_SIZE - (size_t)NB * ZXC_SEEK_ENTRY_SIZE;
    uint8_t* const entry1 = entries + ZXC_SEEK_ENTRY_SIZE;
    const uint64_t e1 = zxc_le64(entry1);
    const uint64_t e2 = zxc_le64(entries + 2 * ZXC_SEEK_ENTRY_SIZE);
    const uint64_t e3 = zxc_le64(entries + 3 * ZXC_SEEK_ENTRY_SIZE);
    const uint64_t eof_off =
        e3 + ZXC_BLOCK_HEADER_SIZE + zxc_le32(arc + e3 + 3) + ZXC_BLOCK_CHECKSUM_SIZE;
    const uint64_t entry_max = ZXC_BLOCK_HEADER_SIZE + BS + ZXC_BLOCK_CHECKSUM_SIZE;
    /* Both forgeries below relied on a wrapped or doubled span still fitting. */
    if (eof_off + 4 > entry_max || e2 - ZXC_FILE_HEADER_SIZE > entry_max) {
        printf("  [FAIL] fixture: blocks are not small enough to test aliasing\n");
        goto done;
    }

    /* Case 1: entry 1 near 2^64. Block 1 starts there, block 0 ends there and
     * block 2 follows it: all three are refused. Block 3 must still decode. */
    zxc_store_le64(entry1, UINT64_MAX - 3);
    zxc_seekable* s = zxc_seekable_open(arc, (size_t)csz);
    if (!s) {
        printf("  [FAIL] wrap: open refused (entries are checked on access)\n");
        goto done;
    }
    int64_t r = zxc_seekable_decompress_range(s, out, 2 * BS, BS, 16);
    int64_t rmt = zxc_seekable_decompress_range_mt(s, out, 2 * BS, BS, BS + 16, 2);
    uint32_t sz = zxc_seekable_get_block_comp_size(s, 1);
    int64_t good = zxc_seekable_decompress_range(s, out, 2 * BS, 3 * BS, 16);
    zxc_seekable_free(s);
    if (r != ZXC_ERROR_CORRUPT_DATA || rmt != ZXC_ERROR_CORRUPT_DATA || sz != 0 || good != 16 ||
        memcmp(out, src + 3 * BS, 16) != 0) {
        printf("  [FAIL] wrap: st %lld, mt %lld, size %u, block 3 %lld\n", (long long)r,
               (long long)rmt, sz, (long long)good);
        goto done;
    }
    printf("  [PASS] entry near 2^64 is refused, block 3 still decodes\n");
    zxc_store_le64(entry1, e1);

    /* Case 2: entry 1 aliases entry 0. Block 1 must not come back as block 0;
     * blocks 2 and 3, genuine on both sides, must decode. */
    zxc_store_le64(entry1, ZXC_FILE_HEADER_SIZE);
    s = zxc_seekable_open(arc, (size_t)csz);
    if (!s) {
        printf("  [FAIL] alias: open refused\n");
        goto done;
    }
    r = zxc_seekable_decompress_range(s, out, 2 * BS, BS, 16);
    rmt = zxc_seekable_decompress_range_mt(s, out, 2 * BS, BS, BS + 16, 2);
    sz = zxc_seekable_get_block_comp_size(s, 1);
    good = zxc_seekable_decompress_range(s, out, 2 * BS, 2 * BS, 16);
    const int good2 = good == 16 && memcmp(out, src + 2 * BS, 16) == 0;
    good = zxc_seekable_decompress_range(s, out, 2 * BS, 3 * BS, 16);
    const int good3 = good == 16 && memcmp(out, src + 3 * BS, 16) == 0;
    zxc_seekable_free(s);
    if (r != ZXC_ERROR_CORRUPT_DATA || rmt != ZXC_ERROR_CORRUPT_DATA || sz != 0 || !good2 ||
        !good3) {
        printf("  [FAIL] alias: st %lld, mt %lld, size %u, blocks 2/3 %d/%d\n", (long long)r,
               (long long)rmt, sz, good2, good3);
        goto done;
    }
    printf("  [PASS] entry aliasing a neighbour is refused, blocks 2 and 3 still decode\n");

    ok = 1;
    printf("PASS\n\n");
done:
    free(src);
    free(arc);
    free(out);
    return ok;
}
