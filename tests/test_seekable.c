/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

int test_seekable_table_sizes() {
    printf("=== TEST: Seekable - Table Sizes ===\n");

    /* block_header(8) + 10*4 = 48 */
    if (zxc_seek_table_size(10) != 48) {
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

    /* Validate first comp_size entry (after 8-byte block header) */
    if (zxc_le32(buf + 8) != 100) {
        printf("Failed: bad first comp_size\n");
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
    if (!dst || !dec) { free(src); free(dst); free(dec); return 0; }

    zxc_compress_opts_t opts = {.level = ZXC_LEVEL_DEFAULT, .block_size = 64 * 1024,
                                .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); free(dec); return 0; }
    /* Sub-test A: single block (data < block_size) roundtrip */
    {
        const size_t SMALL = 60 * 1024; /* fits in one 64KB block */
        memset(dec, 0, SMALL);
        zxc_compress_opts_t opts_a = {.level = ZXC_LEVEL_DEFAULT, .block_size = 64 * 1024,
                                      .checksum_enabled = 1, .seekable = 0};
        const int64_t a_csize = zxc_compress(src, SMALL, dst, dst_cap, &opts_a);
        if (a_csize <= 0) {
            printf("Failed: single-block 64KB compress (%lld)\n", (long long)a_csize);
            free(src); free(dst); free(dec); return 0;
        }
        zxc_decompress_opts_t ad = {.checksum_enabled = 1};
        const int64_t a_dsize = zxc_decompress(dst, (size_t)a_csize, dec, SMALL, &ad);
        if (a_dsize != (int64_t)SMALL || memcmp(src, dec, SMALL) != 0) {
            printf("Failed: single-block 64KB roundtrip (dsize=%lld)\n", (long long)a_dsize);
            if (a_dsize == (int64_t)SMALL) {
                for (size_t i = 0; i < SMALL; i++) {
                    if (src[i] != dec[i]) {
                        printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n",
                               i, src[i], dec[i]);
                        break;
                    }
                }
            }
            free(src); free(dst); free(dec); return 0;
        }
        printf("  sub-test A (single block, 60KB): OK\n");
    }
    /* Sub-test B: exactly 2 blocks (128KB with 64KB block_size) */
    {
        const size_t TWO = 128 * 1024;
        memset(dec, 0, TWO);
        zxc_compress_opts_t opts_b = {.level = ZXC_LEVEL_DEFAULT, .block_size = 64 * 1024,
                                      .checksum_enabled = 1, .seekable = 0};
        const int64_t b_csize = zxc_compress(src, TWO, dst, dst_cap, &opts_b);
        if (b_csize <= 0) {
            printf("Failed: 2-block 64KB compress (%lld)\n", (long long)b_csize);
            free(src); free(dst); free(dec); return 0;
        }
        zxc_decompress_opts_t bd = {.checksum_enabled = 1};
        const int64_t b_dsize = zxc_decompress(dst, (size_t)b_csize, dec, TWO, &bd);
        if (b_dsize != (int64_t)TWO || memcmp(src, dec, TWO) != 0) {
            printf("Failed: 2-block 64KB roundtrip (dsize=%lld)\n", (long long)b_dsize);
            if (b_dsize == (int64_t)TWO) {
                for (size_t i = 0; i < TWO; i++) {
                    if (src[i] != dec[i]) {
                        printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n",
                               i, src[i], dec[i]);
                        break;
                    }
                }
            }
            free(src); free(dst); free(dec); return 0;
        }
        printf("  sub-test B (2 blocks, 128KB): OK\n");
    }
    /* Sub-test C: full 256KB (4 blocks x 64KB) */
    {
        memset(dec, 0, SRC_SIZE);
        zxc_compress_opts_t opts_ns = {.level = ZXC_LEVEL_DEFAULT, .block_size = 64 * 1024,
                                       .checksum_enabled = 1, .seekable = 0};
        const int64_t ns_csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts_ns);
        if (ns_csize <= 0) {
            printf("Failed: non-seekable compress (%lld)\n", (long long)ns_csize);
            free(src); free(dst); free(dec);
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
                        printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n",
                               i, src[i], dec[i]);
                        break;
                    }
                }
            }
            free(src); free(dst); free(dec);
            return 0;
        }
        printf("  sub-test C (4 blocks, 256KB): OK\n");
    }

    /* Re-compress with seekable=1 for the actual test */
    memset(dec, 0, SRC_SIZE);
    const int64_t csize2 = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize2 <= 0) {
        printf("Failed: seekable re-compress (%lld)\n", (long long)csize2);
        free(src); free(dst); free(dec);
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
                    printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n",
                           i, src[i], dec[i]);
                    break;
                }
            }
        }
        free(src); free(dst); free(dec);
        return 0;
    }

    free(src); free(dst); free(dec);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 2, .block_size = 64 * 1024, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }

    const uint32_t nb = zxc_seekable_get_num_blocks(s);
    if (nb < 3) { printf("Failed: expected >= 3 blocks, got %u\n", nb); zxc_seekable_free(s); free(src); free(dst); return 0; }

    const uint64_t total = zxc_seekable_get_decompressed_size(s);
    if (total != SRC_SIZE) { printf("Failed: decomp size\n"); zxc_seekable_free(s); free(src); free(dst); return 0; }

    uint64_t sum = 0;
    for (uint32_t i = 0; i < nb; i++) {
        sum += zxc_seekable_get_block_decomp_size(s, i);
        if (zxc_seekable_get_block_comp_size(s, i) == 0) {
            printf("Failed: zero comp size\n"); zxc_seekable_free(s); free(src); free(dst); return 0;
        }
    }
    if (sum != SRC_SIZE) { printf("Failed: block sizes sum\n"); zxc_seekable_free(s); free(src); free(dst); return 0; }

    zxc_seekable_free(s);
    free(src); free(dst);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 3, .block_size = 64 * 1024, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }

    /* First 1000 bytes */
    uint8_t out1[1000];
    int64_t r = zxc_seekable_decompress_range(s, out1, 1000, 0, 1000);
    if (r != 1000 || memcmp(src, out1, 1000) != 0) {
        printf("Failed: first 1000 bytes (r=%lld)\n", (long long)r);
        if (r == 1000) {
            for (int i = 0; i < 1000; i++) {
                if (src[i] != out1[i]) {
                    printf("  first diff at byte %d: src=0x%02x out=0x%02x\n",
                           i, src[i], out1[i]);
                    break;
                }
            }
        }
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* Middle range spanning multiple blocks */
    const uint64_t off = 100 * 1024;
    const size_t len = 80 * 1024;
    uint8_t* out2 = malloc(len);
    if (!out2) { zxc_seekable_free(s); free(src); free(dst); return 0; }
    r = zxc_seekable_decompress_range(s, out2, len, off, len);
    if (r != (int64_t)len || memcmp(src + off, out2, len) != 0) {
        printf("Failed: mid-range\n"); free(out2); zxc_seekable_free(s); free(src); free(dst); return 0;
    }
    free(out2);

    /* Last bytes */
    uint8_t out3[512];
    r = zxc_seekable_decompress_range(s, out3, 512, SRC_SIZE - 512, 512);
    if (r != 512 || memcmp(src + SRC_SIZE - 512, out3, 512) != 0) {
        printf("Failed: tail\n"); zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* Entire file */
    uint8_t* out4 = malloc(SRC_SIZE);
    if (!out4) { zxc_seekable_free(s); free(src); free(dst); return 0; }
    r = zxc_seekable_decompress_range(s, out4, SRC_SIZE, 0, SRC_SIZE);
    if (r != (int64_t)SRC_SIZE || memcmp(src, out4, SRC_SIZE) != 0) {
        printf("Failed: full range\n"); free(out4); zxc_seekable_free(s); free(src); free(dst); return 0;
    }
    free(out4);

    /* Zero length */
    uint8_t dummy;
    r = zxc_seekable_decompress_range(s, &dummy, 1, 0, 0);
    if (r != 0) { printf("Failed: zero-length\n"); zxc_seekable_free(s); free(src); free(dst); return 0; }

    zxc_seekable_free(s);
    free(src); free(dst);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 0};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (s != NULL) {
        printf("Failed: expected NULL for non-seekable\n");
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    free(src); free(dst);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }
    if (zxc_seekable_get_num_blocks(s) != 1) {
        printf("Failed: expected 1 block\n"); zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    uint8_t out[100];
    int64_t r = zxc_seekable_decompress_range(s, out, 100, 500, 100);
    if (r != 100 || memcmp(src + 500, out, 100) != 0) {
        printf("Failed: range data\n"); zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    zxc_seekable_free(s);
    free(src); free(dst);
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
    if (!dst || !dec) { free(src); free(dst); free(dec); return 0; }

    for (int lvl = 1; lvl <= 5; lvl++) {
        zxc_compress_opts_t opts = {.level = lvl, .block_size = 32 * 1024, .seekable = 1};
        const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
        if (csize <= 0) { printf("Failed: compress level %d\n", lvl); free(src); free(dst); free(dec); return 0; }

        zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
        if (!s) { printf("Failed: open level %d\n", lvl); free(src); free(dst); free(dec); return 0; }

        int64_t r = zxc_seekable_decompress_range(s, dec, SRC_SIZE, 0, SRC_SIZE);
        if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
            printf("Failed: level %d data mismatch (r=%lld expected=%zu)\n",
                   lvl, (long long)r, SRC_SIZE);
            if (r == (int64_t)SRC_SIZE) {
                for (size_t i = 0; i < SRC_SIZE; i++) {
                    if (src[i] != dec[i]) {
                        printf("  first diff at byte %zu: src=0x%02x dec=0x%02x\n",
                               i, src[i], dec[i]);
                        break;
                    }
                }
            }
            zxc_seekable_free(s); free(src); free(dst); free(dec); return 0;
        }
        zxc_seekable_free(s);
    }

    free(src); free(dst); free(dec);
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
    if (!dst || !dec) { free(src); free(dst); free(dec); return 0; }

    /* Compress with minimum block_size = 4096 */
    zxc_compress_opts_t opts = {.level = ZXC_LEVEL_DEFAULT, .block_size = 4096,
                                .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress (%lld)\n", (long long)csize);
        free(src); free(dst); free(dec); return 0;
    }

    /* Open and verify block count = 64 */
    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); free(dec); return 0; }

    const uint32_t n_blocks = zxc_seekable_get_num_blocks(s);
    if (n_blocks != 64) {
        printf("Failed: expected 64 blocks, got %u\n", n_blocks);
        zxc_seekable_free(s); free(src); free(dst); free(dec); return 0;
    }

    /* Full decompress via seekable API */
    int64_t r = zxc_seekable_decompress_range(s, dec, SRC_SIZE, 0, SRC_SIZE);
    if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: full decompress mismatch (r=%lld)\n", (long long)r);
        zxc_seekable_free(s); free(src); free(dst); free(dec); return 0;
    }

    /* Random access: read 100 bytes from the middle of block 32 */
    const uint64_t mid_off = 32 * 4096 + 2000;
    uint8_t spot[100];
    r = zxc_seekable_decompress_range(s, spot, 100, mid_off, 100);
    if (r != 100 || memcmp(src + mid_off, spot, 100) != 0) {
        printf("Failed: random access at offset %llu\n", (unsigned long long)mid_off);
        zxc_seekable_free(s); free(src); free(dst); free(dec); return 0;
    }

    /* Cross-block read: span block boundary (last 50B of block 15 + first 50B of block 16) */
    const uint64_t cross_off = 16 * 4096 - 50;
    uint8_t cross[100];
    r = zxc_seekable_decompress_range(s, cross, 100, cross_off, 100);
    if (r != 100 || memcmp(src + cross_off, cross, 100) != 0) {
        printf("Failed: cross-block read at offset %llu\n", (unsigned long long)cross_off);
        zxc_seekable_free(s); free(src); free(dst); free(dec); return 0;
    }

    zxc_seekable_free(s);
    free(src); free(dst); free(dec);
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
    if (!dst || !dec) { free(src); free(dst); free(dec); return 0; }

    zxc_compress_opts_t opts = {.level = ZXC_LEVEL_DEFAULT, .block_size = 32 * 1024,
                                .checksum_enabled = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) {
        printf("Failed: compress (%lld)\n", (long long)csize);
        free(src); free(dst); free(dec); return 0;
    }

    /* Write compressed data to a temp file */
    FILE* tf = tmpfile();
    if (!tf) { printf("Failed: tmpfile\n"); free(src); free(dst); free(dec); return 0; }
    if (fwrite(dst, 1, (size_t)csize, tf) != (size_t)csize) {
        printf("Failed: fwrite\n"); fclose(tf); free(src); free(dst); free(dec); return 0;
    }
    fflush(tf);

    /* Open via file API */
    zxc_seekable* s = zxc_seekable_open_file(tf);
    if (!s) {
        printf("Failed: zxc_seekable_open_file returned NULL\n");
        fclose(tf); free(src); free(dst); free(dec); return 0;
    }

    /* Verify block count: 128KB / 32KB = 4 blocks */
    const uint32_t n_blocks = zxc_seekable_get_num_blocks(s);
    if (n_blocks != 4) {
        printf("Failed: expected 4 blocks, got %u\n", n_blocks);
        zxc_seekable_free(s); fclose(tf); free(src); free(dst); free(dec); return 0;
    }

    /* Full decompress from file */
    int64_t r = zxc_seekable_decompress_range(s, dec, SRC_SIZE, 0, SRC_SIZE);
    if (r != (int64_t)SRC_SIZE || memcmp(src, dec, SRC_SIZE) != 0) {
        printf("Failed: full decompress from file (r=%lld)\n", (long long)r);
        zxc_seekable_free(s); fclose(tf); free(src); free(dst); free(dec); return 0;
    }

    /* Random access from file: read 200 bytes spanning block boundary */
    const uint64_t cross_off = 32 * 1024 - 100;  /* last 100B of block 0 + first 100B of block 1 */
    uint8_t cross[200];
    r = zxc_seekable_decompress_range(s, cross, 200, cross_off, 200);
    if (r != 200 || memcmp(src + cross_off, cross, 200) != 0) {
        printf("Failed: cross-block read from file\n");
        zxc_seekable_free(s); fclose(tf); free(src); free(dst); free(dec); return 0;
    }

    zxc_seekable_free(s);
    fclose(tf);

    /* NULL input rejection */
    if (zxc_seekable_open_file(NULL) != NULL) {
        printf("Failed: NULL not rejected\n");
        free(src); free(dst); free(dec); return 0;
    }

    free(src); free(dst); free(dec);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 3, .block_size = BLK, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }

    /* Read 200 bytes starting 100 bytes before the block 0/1 boundary */
    const uint64_t boundary = BLK;
    const uint64_t off = boundary - 100;
    const size_t len = 200;
    uint8_t out[200];

    int64_t r = zxc_seekable_decompress_range(s, out, sizeof(out), off, len);
    if (r != (int64_t)len) {
        printf("Failed: cross-boundary range returned %lld\n", (long long)r);
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }
    if (memcmp(out, src + off, len) != 0) {
        printf("Failed: cross-boundary data mismatch\n");
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* Also test a range spanning 3 blocks */
    const uint64_t off3 = BLK * 2 - 100;
    const size_t len3 = BLK + 200;
    uint8_t* out3 = malloc(len3);
    if (!out3) { zxc_seekable_free(s); free(src); free(dst); return 0; }

    r = zxc_seekable_decompress_range(s, out3, len3, off3, len3);
    if (r != (int64_t)len3 || memcmp(out3, src + off3, len3) != 0) {
        printf("Failed: 3-block span mismatch\n");
        free(out3); zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    free(out3);
    zxc_seekable_free(s);
    free(src); free(dst);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    /* Truncate to half */
    zxc_seekable* s = zxc_seekable_open(dst, (size_t)(csize / 2));
    if (s != NULL) {
        printf("Failed: should reject truncated data\n");
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* Truncate to just header */
    s = zxc_seekable_open(dst, 16);
    if (s != NULL) {
        printf("Failed: should reject header-only data\n");
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* Zero bytes */
    s = zxc_seekable_open(dst, 0);
    if (s != NULL) {
        printf("Failed: should reject zero-length data\n");
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    free(src); free(dst);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    /* Corrupt a byte in the SEK payload area (before footer) */
    uint8_t* corrupt = malloc((size_t)csize);
    if (!corrupt) { free(src); free(dst); return 0; }
    memcpy(corrupt, dst, (size_t)csize);
    corrupt[csize - 14] ^= 0xFF;

    zxc_seekable* s = zxc_seekable_open(corrupt, (size_t)csize);
    /* May succeed or fail - just ensure no crash */
    if (s) {
        uint8_t out[100];
        (void)zxc_seekable_decompress_range(s, out, sizeof(out), 0, 100);
        zxc_seekable_free(s);
    }

    free(corrupt); free(src); free(dst);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }

    uint8_t out[256];
    /* offset past EOF */
    int64_t r = zxc_seekable_decompress_range(s, out, sizeof(out), SRC_SIZE + 100, 100);
    if (r > 0) {
        printf("Failed: should reject offset past EOF (got %lld)\n", (long long)r);
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* offset valid but length extends past EOF */
    r = zxc_seekable_decompress_range(s, out, sizeof(out), SRC_SIZE - 50, 200);
    if (r > 0) {
        printf("Failed: should reject range extending past EOF (got %lld)\n", (long long)r);
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    zxc_seekable_free(s);
    free(src); free(dst);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {.level = 1, .seekable = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }

    uint8_t out[10];
    int64_t r = zxc_seekable_decompress_range(s, out, 10, 0, 1000);
    if (r > 0) {
        printf("Failed: should reject insufficient dst capacity (got %lld)\n", (long long)r);
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    zxc_seekable_free(s);
    free(src); free(dst);
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

    /* Buffer API: NULL src with size 0 is rejected with ZXC_ERROR_NULL_INPUT */
    zxc_compress_opts_t opts = {.level = 3, .seekable = 1};
    const int64_t csize = zxc_compress(NULL, 0, dst, dst_cap, &opts);
    if (csize >= 0) {
        printf("Failed: expected NULL_INPUT rejection (got %lld)\n", (long long)csize);
        free(dst); return 0;
    }

    /* Streaming API: empty file via tmpfile() should work */
    FILE* fin = tmpfile();
    FILE* fout = tmpfile();
    if (fin && fout) {
        int64_t stream_sz = zxc_stream_compress(fin, fout, &opts);
        if (stream_sz < 0) {
            printf("Failed: stream compress empty (got %lld)\n", (long long)stream_sz);
            fclose(fin); fclose(fout); free(dst); return 0;
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
                fclose(fin); fclose(fout); fclose(fdec); free(dst); return 0;
            }
            fclose(fdec);
        }
        fclose(fin); fclose(fout);
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {
        .level = 3, .block_size = 64 * 1024, .seekable = 1, .checksum_enabled = 0};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }

    uint8_t out[512];
    const uint64_t off = 64 * 1024 + 100;
    int64_t r = zxc_seekable_decompress_range(s, out, sizeof(out), off, 512);
    if (r != 512 || memcmp(out, src + off, 512) != 0) {
        printf("Failed: no-checksum range mismatch\n");
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* Full decompress also works */
    uint8_t* full = malloc(SRC_SIZE);
    if (!full) { zxc_seekable_free(s); free(src); free(dst); return 0; }
    zxc_decompress_opts_t dopts = {.checksum_enabled = 0};
    int64_t dsize = zxc_decompress(dst, (size_t)csize, full, SRC_SIZE, &dopts);
    if (dsize != (int64_t)SRC_SIZE || memcmp(src, full, SRC_SIZE) != 0) {
        printf("Failed: full decompress mismatch\n");
        free(full); zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    free(full);
    zxc_seekable_free(s);
    free(src); free(dst);
    printf("PASS\n\n");
    return 1;
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
    if (!dst) { free(src); return 0; }

    zxc_compress_opts_t opts = {
        .level = 3, .block_size = 64 * 1024, .seekable = 1, .checksum_enabled = 1};
    const int64_t csize = zxc_compress(src, SRC_SIZE, dst, dst_cap, &opts);
    if (csize <= 0) { printf("Failed: compress\n"); free(src); free(dst); return 0; }

    /* Seekable random access */
    zxc_seekable* s = zxc_seekable_open(dst, (size_t)csize);
    if (!s) { printf("Failed: open\n"); free(src); free(dst); return 0; }

    /* Range from block 0 */
    uint8_t out[512];
    int64_t r = zxc_seekable_decompress_range(s, out, sizeof(out), 0, 512);
    if (r != 512 || memcmp(out, src, 512) != 0) {
        printf("Failed: checksum range head mismatch\n");
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* Range spanning blocks 1-2 */
    const uint64_t off = 64 * 1024 + 100;
    r = zxc_seekable_decompress_range(s, out, sizeof(out), off, 512);
    if (r != 512 || memcmp(out, src + off, 512) != 0) {
        printf("Failed: checksum range mid mismatch\n");
        zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    /* Full decompress with checksum verification */
    uint8_t* full = malloc(SRC_SIZE);
    if (!full) { zxc_seekable_free(s); free(src); free(dst); return 0; }
    zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
    int64_t dsize = zxc_decompress(dst, (size_t)csize, full, SRC_SIZE, &dopts);
    if (dsize != (int64_t)SRC_SIZE || memcmp(src, full, SRC_SIZE) != 0) {
        printf("Failed: full decompress with checksum mismatch\n");
        free(full); zxc_seekable_free(s); free(src); free(dst); return 0;
    }

    free(full);
    zxc_seekable_free(s);
    free(src); free(dst);
    printf("PASS\n\n");
    return 1;
}
