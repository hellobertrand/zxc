/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

int test_opaque_context_api() {
    printf("=== TEST: Opaque Context API (zxc_create_cctx / zxc_create_dctx) ===\n");

    /* 1. NULL context -> ZXC_ERROR_NULL_INPUT */
    {
        uint8_t d[64];
        zxc_compress_opts_t co = {.level = 3, .checksum_enabled = 0};
        if (zxc_compress_cctx(NULL, d, sizeof(d), d, sizeof(d), &co) != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] compress_cctx NULL ctx\n");
            return 0;
        }
        zxc_decompress_opts_t do_ = {.checksum_enabled = 0};
        if (zxc_decompress_dctx(NULL, d, sizeof(d), d, sizeof(d), &do_) != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] decompress_dctx NULL ctx\n");
            return 0;
        }
        printf("  [PASS] NULL context -> ZXC_ERROR_NULL_INPUT\n");
    }

    /* 2. Create with eager init, multi-call reuse, free */
    zxc_compress_opts_t create_opts = {.level = 3, .checksum_enabled = 0};
    zxc_cctx* cctx = zxc_create_cctx(&create_opts);
    zxc_dctx* dctx = zxc_create_dctx();
    if (!cctx || !dctx) {
        printf("  [FAIL] create returned NULL\n");
        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
        return 0;
    }

    const size_t src_sz = 8192;
    uint8_t* src = malloc(src_sz);
    const size_t comp_cap = (size_t)zxc_compress_bound(src_sz);
    uint8_t* comp = malloc(comp_cap);
    uint8_t* dec = malloc(src_sz);

    /* 3. Three calls with the SAME cctx: level 1, 3, 5 */
    for (int lvl = 1; lvl <= 5; lvl += 2) {
        gen_lz_data(src, src_sz);
        zxc_compress_opts_t co = {.level = lvl, .checksum_enabled = (lvl == 3)};
        const int64_t csz = zxc_compress_cctx(cctx, src, src_sz, comp, comp_cap, &co);
        if (csz <= 0) {
            printf("  [FAIL] compress_cctx level %d returned %lld\n", lvl, (long long)csz);
            goto fail;
        }

        zxc_decompress_opts_t do_ = {.checksum_enabled = (lvl == 3)};
        const int64_t dsz = zxc_decompress_dctx(dctx, comp, (size_t)csz, dec, src_sz, &do_);
        if (dsz != (int64_t)src_sz || memcmp(src, dec, src_sz) != 0) {
            printf("  [FAIL] roundtrip level %d (dsz=%lld)\n", lvl, (long long)dsz);
            goto fail;
        }
    }
    printf("  [PASS] Multi-call reuse (level 1, 3, 5)\n");

    /* 4. Free is safe to call multiple times / on NULL */
    zxc_free_cctx(cctx);
    cctx = NULL;
    zxc_free_cctx(NULL); /* no-op */
    zxc_free_dctx(dctx);
    dctx = NULL;
    zxc_free_dctx(NULL);
    printf("  [PASS] Free + double-free + NULL safety\n");

    free(src);
    free(comp);
    free(dec);
    printf("PASS\n\n");
    return 1;

fail:
    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(src);
    free(comp);
    free(dec);
    return 0;
}

int test_estimate_cctx_size() {
    printf("=== TEST: Unit - zxc_estimate_cctx_size ===\n");

    /* 1. Zero input returns zero. */
    if (zxc_estimate_cctx_size(0) != 0) {
        printf("  [FAIL] estimate(0) must return 0\n");
        return 0;
    }
    printf("  [PASS] estimate(0) == 0\n");

    /* 2. Non-zero input returns non-zero estimate. */
    const uint64_t e1k = zxc_estimate_cctx_size(1024);
    if (e1k == 0) {
        printf("  [FAIL] estimate(1 KiB) must be > 0\n");
        return 0;
    }
    printf("  [PASS] estimate(1 KiB) = %llu bytes\n", (unsigned long long)e1k);

    /* 3. Sizes below ZXC_BLOCK_SIZE_MIN collapse to the same estimate. */
    if (zxc_estimate_cctx_size(512) != e1k ||
        zxc_estimate_cctx_size(4096) != e1k) {
        printf("  [FAIL] estimates below MIN must round to ZXC_BLOCK_SIZE_MIN\n");
        return 0;
    }
    printf("  [PASS] estimate rounds sub-MIN inputs to the same value\n");

    /* 4. Monotonic: estimate grows with src_size across block_size tiers. */
    const uint64_t e64k = zxc_estimate_cctx_size(64 * 1024);
    const uint64_t e1m = zxc_estimate_cctx_size(1024 * 1024);
    const uint64_t e8m = zxc_estimate_cctx_size(8 * 1024 * 1024);
    if (!(e1k <= e64k && e64k <= e1m && e1m <= e8m)) {
        printf("  [FAIL] estimates must be monotonic: %llu, %llu, %llu, %llu\n",
               (unsigned long long)e1k, (unsigned long long)e64k,
               (unsigned long long)e1m, (unsigned long long)e8m);
        return 0;
    }
    printf("  [PASS] monotonic: 1K=%llu, 64K=%llu, 1M=%llu, 8M=%llu\n",
           (unsigned long long)e1k, (unsigned long long)e64k,
           (unsigned long long)e1m, (unsigned long long)e8m);

    /* 5. Sanity: estimate for a large block must exceed the block itself. */
    if (e1m < 1024 * 1024) {
        printf("  [FAIL] estimate(1 MiB)=%llu should exceed 1 MiB\n",
               (unsigned long long)e1m);
        return 0;
    }
    printf("  [PASS] estimate exceeds raw block size (context overhead present)\n");

    /* 6. Sub-linear scaling: doubling src_size must roughly double the estimate,
     *    bounded above by 10x factor (chain 2x, everything else 1x). */
    if (e8m < 4 * e1m || e8m > 12 * e1m) {
        printf("  [FAIL] 8x src_size yields %.2fx memory (expected ~8x)\n",
               (double)e8m / (double)e1m);
        return 0;
    }
    printf("  [PASS] scaling: 8x src_size -> %.2fx memory\n",
           (double)e8m / (double)e1m);

    printf("PASS\n\n");
    return 1;
}
