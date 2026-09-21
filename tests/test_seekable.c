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

    /* block_header(8) + one group: anchor(8) + 10*4 = 56 */
    if (zxc_seek_table_size(10) != 56) {
        printf("Failed: size for 10 blocks\n");
        return 0;
    }
    /* block_header(8) + no group = 8 */
    if (zxc_seek_table_size(0) != 8) {
        printf("Failed: zero blocks size\n");
        return 0;
    }
    /* A count whose table would not fit 64 bits: refused, not wrapped. */
    if (zxc_seek_table_size(1ULL << 62) != 0 || zxc_seek_table_size(UINT64_MAX) != 0) {
        printf("Failed: counts past a describable table must give 0\n");
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

    /* Size field: one group, anchor(8) + 3 sizes x 4 = 20, exact below 4 GiB. The
     * group: where block 0 starts, then each block's on-disk size. */
    if (zxc_le32(buf + 3) != 20 || zxc_le64(buf + 8) != 16 || zxc_le32(buf + 16) != 100 ||
        zxc_le32(buf + 20) != 200 || zxc_le32(buf + 24) != 150) {
        printf("Failed: bad size field or group\n");
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

    const uint64_t nb = zxc_seekable_get_num_blocks(s);
    if (nb < 3) {
        printf("Failed: expected >= 3 blocks, got %llu\n", (unsigned long long)nb);
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

    const uint64_t n_blocks = zxc_seekable_get_num_blocks(s);
    if (n_blocks != 64) {
        printf("Failed: expected 64 blocks, got %llu\n", (unsigned long long)n_blocks);
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
    const uint64_t n_blocks = zxc_seekable_get_num_blocks(s);
    if (n_blocks != 4) {
        printf("Failed: expected 4 blocks, got %llu\n", (unsigned long long)n_blocks);
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

    /* A checksummed archive carries a 16-byte footer, so its floor is 48, not
     * the 40 of a plain one; cut to 47 it is short of its own footer. */
    zxc_compress_opts_t chk = {.level = 1, .seekable = 1, .checksum_enabled = 1};
    const int64_t csize_chk = zxc_compress(src, SRC_SIZE, dst, dst_cap, &chk);
    if (csize_chk <= 0) {
        printf("Failed: compress with checksums\n");
        free(src);
        free(dst);
        return 0;
    }
    s = zxc_seekable_open(dst, ZXC_FILE_HEADER_SIZE + 2 * ZXC_BLOCK_HEADER_SIZE +
                                   ZXC_FILE_FOOTER_SIZE + ZXC_FILE_DIGEST_SIZE - 1);
    if (s) {
        printf("Failed: should reject a checksummed archive short of its footer\n");
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

/* Reader that serves the bytes but returns INT64_MIN, whose low 32 bits are 0. */
static int64_t reader_wide_error_read_at(void* ctx, void* dst, size_t len, uint64_t offset) {
    (void)reader_test_read_at(ctx, dst, len, offset);
    return INT64_MIN;
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
        printf("Failed: expected 4 blocks, got %llu\n",
               (unsigned long long)zxc_seekable_get_num_blocks(s));
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

    /* Negative return outside int: must reject too */
    zxc_reader_t wide_r = {
        .read_at = reader_wide_error_read_at, .ctx = &mctx, .size = (uint64_t)csize};
    zxc_seekable* const wide = zxc_seekable_open_reader(&wide_r);
    if (wide) {
        printf("Failed: INT64_MIN from the reader read as success\n");
        zxc_seekable_free(wide);
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

/* A range read sees only its blocks: the position-seeded checksum must refuse a moved one. */
int test_seekable_swapped_blocks_caught(void) {
    printf("=== TEST: Seekable - a swapped block fails its checksum ===\n");
    enum { BLK = 4096, NBLK = 4, SPAN = ZXC_BLOCK_HEADER_SIZE + BLK + ZXC_BLOCK_CHECKSUM_SIZE };
    static uint8_t src[BLK * NBLK], arc[2 * BLK * NBLK], dec[BLK * NBLK], tmp[SPAN];
    uint32_t rng = 0x6A09E667u;
    for (size_t i = 0; i < sizeof(src); i++) {
        rng = rng * 1103515245u + 12345u;
        src[i] = (uint8_t)(rng >> 16);
    }
    const zxc_compress_opts_t co = {
        .level = 3, .block_size = BLK, .seekable = 1, .checksum_enabled = 1};
    const int64_t n = zxc_compress(src, sizeof(src), arc, sizeof(arc), &co);
    const size_t b0 = ZXC_FILE_HEADER_SIZE, b1 = b0 + SPAN;
    uint8_t out[64];
    int ok = 0;
    do {
        /* Incompressible: blocks 0 and 1 are full RAW blocks, swappable in place. */
        if (n <= 0 || arc[b0] != ZXC_BLOCK_RAW || arc[b1] != ZXC_BLOCK_RAW ||
            zxc_le32(arc + b0 + 3) != BLK || zxc_le32(arc + b1 + 3) != BLK) {
            printf("  [FAIL] fixture: expected two full RAW blocks (n = %lld)\n", (long long)n);
            break;
        }
        memcpy(tmp, arc + b0, SPAN);
        memmove(arc + b0, arc + b1, SPAN);
        memcpy(arc + b1, tmp, SPAN);

        zxc_seekable* const s = zxc_seekable_open(arc, (size_t)n);
        if (!s) {
            printf("  [FAIL] open\n");
            break;
        }
        const int64_t quiet = zxc_seekable_decompress_range(s, out, sizeof(out), 0, sizeof(out));
        const int moved = quiet == (int64_t)sizeof(out) && memcmp(out, src + BLK, sizeof(out)) == 0;
        zxc_seekable_set_checksum(s, 1);
        const int64_t st = zxc_seekable_decompress_range(s, out, sizeof(out), 0, sizeof(out));
        const int64_t mt = zxc_seekable_decompress_range_mt(s, out, sizeof(out), BLK - 32, 64, 2);
        zxc_seekable_free(s);
        const zxc_decompress_opts_t verify = {.checksum_enabled = 1};
        const int64_t frame = zxc_decompress(arc, (size_t)n, dec, sizeof(dec), &verify);

        if (!moved || st != ZXC_ERROR_BAD_CHECKSUM || mt != ZXC_ERROR_BAD_CHECKSUM ||
            frame != ZXC_ERROR_BAD_CHECKSUM) {
            printf("  [FAIL] unverified moved=%d, st %lld, mt %lld, frame %lld\n", moved,
                   (long long)st, (long long)mt, (long long)frame);
            break;
        }
        ok = 1;
    } while (0);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* Incompressible on purpose: a RAW block passes a flipped byte straight through, so
 * only the checksum catches it. */
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
        printf("Failed: need >= 2 full blocks, got %llu\n",
               (unsigned long long)zxc_seekable_get_num_blocks(s));
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

/* A footer declaring ~2^64 bytes is far above 2^32 blocks; the naive ceil wrapped it to 0. */
int test_seekable_forged_total_size(void) {
    printf("=== TEST: Seekable - Forged Total Size Near 2^64 ===\n");
    enum { BS = 4096 };
    const uint64_t totals[] = {UINT64_MAX, UINT64_MAX - (BS - 2)};
    const zxc_block_header_t eof = {
        .block_type = ZXC_BLOCK_EOF, .block_flags = 0, .reserved = 0, .comp_size = 0};
    /* [file header 16][EOF 8][SEK header for 0 blocks 8][footer 8] */
    uint8_t arc[ZXC_FILE_HEADER_SIZE + 2 * ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE];
    for (size_t k = 0; k < sizeof(totals) / sizeof(totals[0]); k++) {
        uint8_t* p = arc;
        if (zxc_write_file_header(p, ZXC_FILE_HEADER_SIZE, BS, 0, 0, 1) < 0 ||
            zxc_write_block_header(p += ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &eof) < 0 ||
            zxc_seek_table_header(p += ZXC_BLOCK_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, 0) < 0 ||
            zxc_write_file_footer(p + ZXC_BLOCK_HEADER_SIZE, ZXC_FILE_FOOTER_SIZE, totals[k], 0,
                                  0) < 0) {
            printf("Failed: fixture headers\n");
            return 0;
        }
        zxc_seekable* const s = zxc_seekable_open(arc, sizeof(arc));
        if (s) {
            printf("Failed: total %llu opened with %llu blocks\n", (unsigned long long)totals[k],
                   (unsigned long long)zxc_seekable_get_num_blocks(s));
            zxc_seekable_free(s);
            return 0;
        }
    }
    printf("PASS\n\n");
    return 1;
}

/* An EOF header announcing a payload is malformed (FORMAT 11.1), even with a valid
 * header checksum: the seekable open must refuse it like the other decoders. */
int test_seekable_eof_with_payload(void) {
    printf("=== TEST: Seekable - EOF Header With Non-Zero comp_size ===\n");
    enum { SRC_SIZE = 64 * 1024 };
    uint8_t* const src = malloc(SRC_SIZE);
    const size_t cap = (size_t)zxc_compress_bound(SRC_SIZE);
    uint8_t* const arc = malloc(cap);
    int ok = 0;
    if (!src || !arc) goto done;
    fill_seek_data(src, SRC_SIZE, 7);
    const zxc_compress_opts_t opts = {.level = 1, .seekable = 1, .block_size = 4096};
    const int64_t csize = zxc_compress(src, SRC_SIZE, arc, cap, &opts);
    zxc_seekable* s = csize > 0 ? zxc_seekable_open(arc, (size_t)csize) : NULL;
    if (!s) {
        printf("Failed: intact archive does not open\n");
        goto done;
    }
    const uint64_t n = zxc_seekable_get_num_blocks(s);
    zxc_seekable_free(s);

    /* [data blocks][EOF 8][SEK 8 + table][footer 8] */
    uint8_t* const eof = arc + csize - ZXC_FILE_FOOTER_SIZE - (size_t)zxc_seek_table_bytes(n) -
                         2 * ZXC_BLOCK_HEADER_SIZE;
    const zxc_block_header_t forged = {
        .block_type = ZXC_BLOCK_EOF, .block_flags = 0, .reserved = 0, .comp_size = 1};
    zxc_block_header_t back;
    if (zxc_write_block_header(eof, ZXC_BLOCK_HEADER_SIZE, &forged) < 0 ||
        zxc_read_block_header(eof, ZXC_BLOCK_HEADER_SIZE, &back) != ZXC_OK ||
        back.block_type != ZXC_BLOCK_EOF || back.comp_size != 1) {
        printf("Failed: forged EOF header does not read back\n");
        goto done;
    }
    s = zxc_seekable_open(arc, (size_t)csize);
    if (s) {
        printf("Failed: EOF with comp_size 1 opened\n");
        zxc_seekable_free(s);
        goto done;
    }
    ok = 1;
    printf("PASS\n\n");
done:
    free(src);
    free(arc);
    return ok;
}

/* 2^32 + 5 RAW blocks of 4 KiB served by a callback, sixteen terabytes that never
 * exist: no field caps the count, open costs three reads, nothing is loaded. */
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
    const uint64_t table = zxc_seek_table_bytes(c->n);
    if (off < table) {
        /* Group j: anchor 16 + j * GROUP * SYNTH_BLK, then sizes all SYNTH_BLK, LE. */
        const uint64_t j = off / ZXC_SEEK_GROUP_BYTES;
        const uint64_t in = off % ZXC_SEEK_GROUP_BYTES;
        if (in < ZXC_SEEK_ANCHOR_SIZE) {
            const uint64_t v = ZXC_FILE_HEADER_SIZE + j * ZXC_SEEK_GROUP * SYNTH_BLK;
            return (uint8_t)(v >> (8 * in));
        }
        return (uint8_t)((uint32_t)SYNTH_BLK >>
                         (8 * ((in - ZXC_SEEK_ANCHOR_SIZE) % ZXC_SEEK_SIZE_ENTRY)));
    }
    off -= table;
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
    printf("=== TEST: Seekable - 2^32 + 5 blocks through a callback, nothing materialised ===\n");
    if (sizeof(size_t) < 8) {
        printf("  [SKIP] 64-bit hosts only\n\n");
        return 1;
    }
    synth_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.n = (1ULL << 32) + 5;
    c.counting = 1;
    c.eof_off = ZXC_FILE_HEADER_SIZE + c.n * SYNTH_BLK;
    c.size =
        c.eof_off + 2 * ZXC_BLOCK_HEADER_SIZE + zxc_seek_table_bytes(c.n) + ZXC_FILE_FOOTER_SIZE;
    const uint64_t total = c.n * SYNTH_BS;
    const zxc_block_header_t raw = {
        .block_type = ZXC_BLOCK_RAW, .block_flags = 0, .reserved = 0, .comp_size = SYNTH_BS};
    const zxc_block_header_t eof = {
        .block_type = ZXC_BLOCK_EOF, .block_flags = 0, .reserved = 0, .comp_size = 0};
    if (zxc_write_file_header(c.file_hdr, sizeof(c.file_hdr), SYNTH_BS, 0, 0, 1) < 0 ||
        zxc_write_block_header(c.blk_hdr, sizeof(c.blk_hdr), &raw) < 0 ||
        zxc_write_block_header(c.eof_hdr, sizeof(c.eof_hdr), &eof) < 0 ||
        zxc_seek_table_header(c.sek_hdr, sizeof(c.sek_hdr), c.n) < 0 ||
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
        if (zxc_seekable_get_num_blocks(s) != c.n ||
            zxc_seekable_get_decompressed_size(s) != total) {
            printf("  [FAIL] geometry\n");
            break;
        }
        /* The archive's last 32 bytes: block 2^32 + 4, above what a u32 index reaches. */
        for (int i = 0; i < 32; i++) want[i] = synth_payload(c.n - 1, SYNTH_BS - 32 + (uint32_t)i);
        if (zxc_seekable_decompress_range(s, got, sizeof(got), total - 32, 32) != 32 ||
            memcmp(got, want, 32) != 0) {
            printf("  [FAIL] tail read\n");
            break;
        }
        /* Across blocks 2^32 and 2^32 + 1, multi-threaded: a u32 index would wrap
         * onto block 0 and serve its bytes without error. */
        c.counting = 0;
        const uint64_t hi = 1ULL << 32;
        for (int i = 0; i < 16; i++) want[i] = synth_payload(hi, SYNTH_BS - 16 + (uint32_t)i);
        for (int i = 0; i < 16; i++) want[16 + i] = synth_payload(hi + 1, (uint32_t)i);
        if (zxc_seekable_decompress_range_mt(s, got, sizeof(got), (hi + 1) * SYNTH_BS - 16, 32,
                                             2) != 32 ||
            memcmp(got, want, 32) != 0) {
            printf("  [FAIL] boundary read\n");
            break;
        }
        if (zxc_seekable_get_block_comp_size(s, 0) != SYNTH_BLK ||
            zxc_seekable_get_block_comp_size(s, c.n - 1) != SYNTH_BLK) {
            printf("  [FAIL] block sizes\n");
            break;
        }
        ok = 1;
    } while (0);
    zxc_seekable_free(s);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* A multi-threaded read loads every group it spans at once, and must size the last
 * block before a boundary from its own group, not from the next anchor. Group 1's
 * anchor is moved onto block 0: equal-sized RAW blocks hide it, so block 64 reads
 * block 0. Block 63 must still decode, and MT must match ST. */
int test_seekable_mt_group_boundary(void) {
    printf("=== TEST: Seekable - MT read across a group with a moved anchor ===\n");
    /* Three groups: the last one must end on the EOF block, so move a middle one. */
    enum { BS = 4096, NB = 3 * ZXC_SEEK_GROUP };
    const size_t SRC_SIZE = (size_t)BS * NB;
    uint8_t* const src = malloc(SRC_SIZE);
    const size_t cap = (size_t)zxc_compress_bound(SRC_SIZE) + 256;
    uint8_t* const arc = malloc(cap);
    uint8_t* const st_out = malloc(2 * BS);
    uint8_t* const mt_out = malloc(2 * BS);
    int ok = 0;
    zxc_seekable* s = NULL;
    do {
        if (!src || !arc || !st_out || !mt_out) break;
        gen_random_data(src, SRC_SIZE);
        const zxc_compress_opts_t co = {.level = 1, .block_size = BS, .seekable = 1};
        const int64_t csize = zxc_compress(src, SRC_SIZE, arc, cap, &co);
        if (csize <= 0) {
            printf("  [FAIL] compress -> %lld\n", (long long)csize);
            break;
        }
        /* [header][blocks][EOF][SEK header][group 0][group 1][group 2][footer 8] */
        uint8_t* const g0 = arc + csize - ZXC_FILE_FOOTER_SIZE - (size_t)zxc_seek_table_bytes(NB);
        uint8_t* const g1 = g0 + ZXC_SEEK_GROUP_BYTES;
        const uint32_t sz0 = zxc_le32(g0 + ZXC_SEEK_ANCHOR_SIZE);
        const uint32_t sz63 =
            zxc_le32(g0 + ZXC_SEEK_ANCHOR_SIZE + (ZXC_SEEK_GROUP - 1) * ZXC_SEEK_SIZE_ENTRY);
        const uint32_t sz64 = zxc_le32(g1 + ZXC_SEEK_ANCHOR_SIZE);
        if (zxc_le64(g0) != ZXC_FILE_HEADER_SIZE || sz0 != sz63 || sz0 != sz64) {
            printf("  [FAIL] expected equal RAW blocks (%u, %u, %u)\n", sz0, sz63, sz64);
            break;
        }
        zxc_store_le64(g1, ZXC_FILE_HEADER_SIZE);

        s = zxc_seekable_open(arc, (size_t)csize);
        if (!s) {
            printf("  [FAIL] open must not scan the table\n");
            break;
        }
        const uint64_t off = (uint64_t)(ZXC_SEEK_GROUP - 1) * BS;
        const int64_t st = zxc_seekable_decompress_range(s, st_out, 2 * BS, off, 2 * BS);
        const int64_t mt = zxc_seekable_decompress_range_mt(s, mt_out, 2 * BS, off, 2 * BS, 2);
        if (st != 2 * BS || mt != st || memcmp(st_out, mt_out, 2 * BS) != 0) {
            printf("  [FAIL] st %lld, mt %lld: the two paths must agree\n", (long long)st,
                   (long long)mt);
            break;
        }
        if (memcmp(mt_out, src + off, BS) != 0) {
            printf("  [FAIL] block 63 did not decode to its own bytes\n");
            break;
        }
        if (memcmp(mt_out + BS, src, BS) != 0) {
            printf("  [FAIL] block 64 was expected to read block 0 through the moved anchor\n");
            break;
        }
        ok = 1;
    } while (0);
    zxc_seekable_free(s);
    free(src);
    free(arc);
    free(st_out);
    free(mt_out);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_seekable_forged_table_entry() {
    printf("=== TEST: Seekable - Forged Table Entry ===\n");

    /* Three full groups of compressible blocks: a size can grow without crossing the bound. */
    enum { BS = 4096, NB = 3 * ZXC_SEEK_GROUP };
    const size_t SRC_SIZE = (size_t)BS * NB;
    uint8_t* src = malloc(SRC_SIZE);
    if (!src) return 0;
    for (size_t b = 0; b < NB; b++)
        for (size_t i = 0; i < BS; i++) src[b * BS + i] = (uint8_t)('A' + b % 23 + (i % 7 == 0));

    const size_t dst_cap = (size_t)zxc_compress_bound(SRC_SIZE) + 256;
    uint8_t* dst = malloc(dst_cap);
    if (!dst) {
        free(src);
        return 0;
    }
    zxc_compress_opts_t opts = {.level = 1, .seekable = 1, .block_size = BS};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    free(src);
    if (csize <= 0) {
        printf("Failed: compress\n");
        free(dst);
        return 0;
    }

    const uint64_t entry_max = ZXC_BLOCK_HEADER_SIZE + BS;
    /* [file header][blocks][EOF][SEK header][group 0][group 1][group 2][footer] */
    uint8_t* const g0 = dst + csize - ZXC_FILE_FOOTER_SIZE - (size_t)zxc_seek_table_bytes(NB);
    uint8_t* const g1 = g0 + ZXC_SEEK_GROUP_BYTES;
#define SIZE_AT(i)                                                               \
    (g0 + ((i) / ZXC_SEEK_GROUP) * ZXC_SEEK_GROUP_BYTES + ZXC_SEEK_ANCHOR_SIZE + \
     ((i) % ZXC_SEEK_GROUP) * ZXC_SEEK_SIZE_ENTRY)
    uint8_t* const size0 = SIZE_AT(0);
    uint8_t* const size1 = SIZE_AT(1);
    uint8_t* const size_last = SIZE_AT(NB - 1);
    const uint32_t sz0 = zxc_le32(size0);
    const uint32_t sz1 = zxc_le32(size1);
    const uint32_t szl = zxc_le32(size_last);
    const size_t cap = 3 * BS;
    uint8_t* const out = malloc(cap);

    int ok = out != NULL;
    /* The blocks end at the EOF header, before the SEK header and its table. */
    const uint64_t eof_off = (uint64_t)csize - ZXC_FILE_FOOTER_SIZE - zxc_seek_table_bytes(NB) -
                             2 * ZXC_BLOCK_HEADER_SIZE;
    if (ok && (zxc_le64(g0) != ZXC_FILE_HEADER_SIZE || sz0 + 8 > entry_max || sz1 < 16 ||
               sz1 + 1 > entry_max || szl + 1 > entry_max || szl < 16 ||
               /* group 1 with two sizes maxed runs past the EOF block */
               zxc_le64(g1 + ZXC_SEEK_GROUP_BYTES) + 2 * entry_max -
                       zxc_le32(SIZE_AT(ZXC_SEEK_GROUP)) - zxc_le32(SIZE_AT(ZXC_SEEK_GROUP + 1)) <=
                   eof_off)) {
        printf("Failed: expected compressible blocks with slack (%u, %u, %u)\n", sz0, sz1, szl);
        ok = 0;
    }
    if (ok) {
        zxc_seekable* intact = zxc_seekable_open(dst, (size_t)csize);
        if (!intact || zxc_seekable_decompress_range(intact, out, cap, BS, 16) != 16) {
            printf("Failed: intact archive rejected\n");
            ok = 0;
        }
        zxc_seekable_free(intact);
    }

    /* Each group is checked alone on access, so a forged anchor costs only its own
     * group. A size one byte off is caught by its block's header: blocks before
     * it still decode, those after it are shifted and refused. The getter
     * reads no block: 0 when the bounds refuse, else what the table says. The
     * refusal says where the entry landed: on a real header its size fails
     * (CORRUPT_DATA), inside a block the header checksum does (BAD_HEADER). */
    const uint32_t NONE = UINT32_MAX;
    struct {
        const char* what;
        uint8_t* at;
        int width; /* 8 = anchor, 4 = size */
        uint64_t value;
        uint8_t* at2; /* optional second size patch */
        uint32_t value2;
        uint32_t bad_blk;    /* its read is refused, single- and multi-threaded */
        uint32_t bad_getter; /* what the getter reports for it */
        uint32_t near_blk;   /* a neighbour the forgery must NOT cost, or NONE */
        uint32_t after_blk;  /* behind a forged size in its group: refused too, or NONE */
        uint32_t good_blk;   /* in an untouched group */
        int bad_err;         /* its refusal code */
        int after_err;       /* its refusal code */
    } cases[] = {
        {"size 0 pushed up: more than one block", size0, 4, entry_max + 1, NULL, 0, 0, 0, NONE,
         NONE, ZXC_SEEK_GROUP, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA},
        {"size 1 pulled below a header", size1, 4, ZXC_BLOCK_HEADER_SIZE - 1, NULL, 0, 1, 0, NONE,
         NONE, ZXC_SEEK_GROUP, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA},
        {"size 1 one byte long: its block header disagrees", size1, 4, sz1 + 1, NULL, 0, 1, sz1 + 1,
         0, 2, ZXC_SEEK_GROUP, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_BAD_HEADER},
        {"anchor 0 off the file header", g0, 8, ZXC_FILE_HEADER_SIZE + 1, NULL, 0, 0, 0, NONE, NONE,
         ZXC_SEEK_GROUP, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA},
        {"anchor 1 one byte late: group 1 shifted, group 0 untouched", g1, 8, zxc_le64(g1) + 1,
         NULL, 0, ZXC_SEEK_GROUP, zxc_le32(SIZE_AT(ZXC_SEEK_GROUP)), ZXC_SEEK_GROUP - 1, NONE,
         2 * ZXC_SEEK_GROUP, ZXC_ERROR_BAD_HEADER, ZXC_ERROR_CORRUPT_DATA},
        {"anchor 1 near 2^64: group 1 out of bounds, group 0 untouched", g1, 8, UINT64_MAX - 3,
         NULL, 0, ZXC_SEEK_GROUP, 0, ZXC_SEEK_GROUP - 1, NONE, 2 * ZXC_SEEK_GROUP,
         ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA},
        {"last size one byte long: the last group overshoots the EOF block", size_last, 4, szl + 1,
         NULL, 0, NB - 1, 0, NONE, NONE, 0, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA},
        {"last size one byte short: the last group misses the EOF block", size_last, 4, szl - 1,
         NULL, 0, NB - 1, 0, NONE, NONE, 0, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA},
        {"two sizes of group 1 maxed: a middle group overshoots the EOF block",
         SIZE_AT(ZXC_SEEK_GROUP), 4, entry_max, SIZE_AT(ZXC_SEEK_GROUP + 1), (uint32_t)entry_max,
         ZXC_SEEK_GROUP, 0, ZXC_SEEK_GROUP - 1, NONE, 2 * ZXC_SEEK_GROUP, ZXC_ERROR_CORRUPT_DATA,
         ZXC_ERROR_CORRUPT_DATA},
        {"sizes 0 and 1 traded: sum intact, headers disagree", size0, 4, sz0 + 8, size1, sz1 - 8, 0,
         sz0 + 8, NONE, NONE, 2, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA},
    };
    for (size_t k = 0; ok && k < sizeof(cases) / sizeof(cases[0]); k++) {
        const uint64_t keep = cases[k].width == 8 ? zxc_le64(cases[k].at) : zxc_le32(cases[k].at);
        const uint32_t keep2 = cases[k].at2 ? zxc_le32(cases[k].at2) : 0;
        const uint32_t near = cases[k].near_blk;
        const uint32_t after = cases[k].after_blk;
        const uint32_t good_size = zxc_le32(SIZE_AT(cases[k].good_blk));
        const uint32_t near_size = near == NONE ? 0 : zxc_le32(SIZE_AT(near));
        if (cases[k].width == 8)
            zxc_store_le64(cases[k].at, cases[k].value);
        else
            zxc_store_le32(cases[k].at, (uint32_t)cases[k].value);
        if (cases[k].at2) zxc_store_le32(cases[k].at2, cases[k].value2);

        zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
        if (!s) {
            printf("Failed: %s: open must not scan the table\n", cases[k].what);
            ok = 0;
        } else {
            const uint32_t bad = cases[k].bad_blk;
            const uint64_t bad_off = (uint64_t)bad * BS;
            const int64_t st = zxc_seekable_decompress_range(s, out, cap, bad_off, 16);
            /* Two blocks of the same group, so the multi-threaded planner loads it. */
            const uint64_t mt_off =
                (bad % ZXC_SEEK_GROUP == ZXC_SEEK_GROUP - 1) ? bad_off - BS : bad_off;
            const int64_t mt = zxc_seekable_decompress_range_mt(s, out, cap, mt_off, BS + 16, 2);
            const int64_t good =
                zxc_seekable_decompress_range(s, out, cap, (uint64_t)cases[k].good_blk * BS, 16);
            const int64_t near_st =
                near == NONE ? 16
                             : zxc_seekable_decompress_range(s, out, cap, (uint64_t)near * BS, 16);
            const uint32_t near_got = near == NONE ? 0 : zxc_seekable_get_block_comp_size(s, near);
            const int64_t after_st = after == NONE ? cases[k].after_err
                                                   : zxc_seekable_decompress_range(
                                                         s, out, cap, (uint64_t)after * BS, 16);
            const uint32_t bad_got = zxc_seekable_get_block_comp_size(s, bad);
            const uint32_t good_got = zxc_seekable_get_block_comp_size(s, cases[k].good_blk);
            if (st != cases[k].bad_err || mt != cases[k].bad_err || good != 16 || near_st != 16 ||
                near_got != near_size || after_st != cases[k].after_err ||
                bad_got != cases[k].bad_getter || good_got != good_size) {
                printf(
                    "Failed: %s: st %lld, mt %lld, good %lld, near %lld (%u/%u), after %lld, "
                    "sizes %u/%u, %u/%u\n",
                    cases[k].what, (long long)st, (long long)mt, (long long)good,
                    (long long)near_st, near_got, near_size, (long long)after_st, bad_got,
                    cases[k].bad_getter, good_got, good_size);
                ok = 0;
            }
            zxc_seekable_free(s);
        }
        if (cases[k].width == 8)
            zxc_store_le64(cases[k].at, keep);
        else
            zxc_store_le32(cases[k].at, (uint32_t)keep);
        if (cases[k].at2) zxc_store_le32(cases[k].at2, keep2);
    }

#undef SIZE_AT
    free(out);
    free(dst);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}
