/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

int test_block_api() {
    printf("=== TEST: Unit - Block API (zxc_compress_block/zxc_decompress_block) ===\n");

    const size_t src_size = 128 * 1024;  // 128 KB
    int result = 0;

    /* All resources initialized to NULL for centralized cleanup. */
    uint8_t* src = NULL;
    uint8_t* compressed = NULL;
    uint8_t* decompressed = NULL;
    zxc_cctx* cctx = NULL;
    zxc_dctx* dctx = NULL;

    src = malloc(src_size);
    if (!src) goto cleanup;
    gen_lz_data(src, src_size);

    // 1. zxc_compress_block_bound
    const uint64_t block_bound = zxc_compress_block_bound(src_size);
    const uint64_t file_bound = zxc_compress_bound(src_size);
    if (block_bound == 0 || block_bound >= file_bound) {
        printf("Failed: block_bound=%llu should be >0 and < file_bound=%llu\n",
               (unsigned long long)block_bound, (unsigned long long)file_bound);
        goto cleanup;
    }
    printf("  [PASS] block_bound=%llu < file_bound=%llu\n", (unsigned long long)block_bound,
           (unsigned long long)file_bound);

    // 2. Allocate buffers and contexts
    compressed = malloc((size_t)block_bound);
    decompressed = malloc(src_size);
    cctx = zxc_create_cctx(NULL);
    dctx = zxc_create_dctx();
    if (!compressed || !decompressed || !cctx || !dctx) {
        printf("Failed: Allocation failed\n");
        goto cleanup;
    }

    // 3. Compress block (no checksum)
    zxc_compress_opts_t copts = {.level = 3, .checksum_enabled = 0};
    int64_t csize = zxc_compress_block(cctx, src, src_size, compressed, (size_t)block_bound, &copts);
    if (csize <= 0) {
        printf("Failed: zxc_compress_block returned %lld\n", (long long)csize);
        goto cleanup;
    }
    printf("  [PASS] Compress block: %zu -> %lld bytes\n", src_size, (long long)csize);

    // 4. Decompress block (no checksum)
    zxc_decompress_opts_t dopts = {.checksum_enabled = 0};
    int64_t dsize =
        zxc_decompress_block(dctx, compressed, (size_t)csize, decompressed, src_size, &dopts);
    if (dsize != (int64_t)src_size) {
        printf("Failed: zxc_decompress_block returned %lld, expected %zu\n", (long long)dsize,
               src_size);
        goto cleanup;
    }
    if (memcmp(src, decompressed, src_size) != 0) {
        printf("Failed: Content mismatch after block decompression\n");
        goto cleanup;
    }
    printf("  [PASS] Decompress block: roundtrip OK (no checksum)\n");

    // 5. With checksum enabled
    copts.checksum_enabled = 1;
    csize = zxc_compress_block(cctx, src, src_size, compressed, (size_t)block_bound, &copts);
    if (csize <= 0) {
        printf("Failed: zxc_compress_block with checksum returned %lld\n", (long long)csize);
        goto cleanup;
    }
    dopts.checksum_enabled = 1;
    dsize = zxc_decompress_block(dctx, compressed, (size_t)csize, decompressed, src_size, &dopts);
    if (dsize != (int64_t)src_size || memcmp(src, decompressed, src_size) != 0) {
        printf("Failed: Roundtrip with checksum failed\n");
        goto cleanup;
    }
    printf("  [PASS] Decompress block: roundtrip OK (with checksum)\n");

    // 6. Error cases
    if (zxc_compress_block(NULL, src, src_size, compressed, (size_t)block_bound, &copts) >= 0) {
        printf("Failed: Should fail with NULL cctx\n");
        goto cleanup;
    }
    if (zxc_compress_block(cctx, NULL, src_size, compressed, (size_t)block_bound, &copts) >= 0) {
        printf("Failed: Should fail with NULL src\n");
        goto cleanup;
    }
    if (zxc_decompress_block(NULL, compressed, (size_t)csize, decompressed, src_size, &dopts) >=
        0) {
        printf("Failed: Should fail with NULL dctx\n");
        goto cleanup;
    }
    printf("  [PASS] Error cases handled correctly\n");

    // 7. Context reuse across multiple blocks
    for (int i = 0; i < 3; i++) {
        gen_lz_data(src, src_size);
        src[0] = (uint8_t)i;

        copts.checksum_enabled = 0;
        csize =
            zxc_compress_block(cctx, src, src_size, compressed, (size_t)block_bound, &copts);
        if (csize <= 0) {
            printf("Failed: Reuse iteration %d compress failed\n", i);
            goto cleanup;
        }
        dopts.checksum_enabled = 0;
        dsize = zxc_decompress_block(dctx, compressed, (size_t)csize, decompressed, src_size,
                                     &dopts);
        if (dsize != (int64_t)src_size || memcmp(src, decompressed, src_size) != 0) {
            printf("Failed: Reuse iteration %d roundtrip failed\n", i);
            goto cleanup;
        }
    }
    printf("  [PASS] Context reuse: 3 independent blocks OK\n");

    // 8. Auto-resize: src_size > block_size must succeed (ZXC auto-sizes)
    {
        zxc_compress_opts_t guard_opts = {.level = 3, .block_size = 4096, .checksum_enabled = 0};
        int64_t guard_rc = zxc_compress_block(cctx, src, src_size, compressed,
                                              (size_t)block_bound, &guard_opts);
        if (guard_rc <= 0) {
            printf("Failed: src_size > block_size should auto-resize, got %lld\n",
                   (long long)guard_rc);
            goto cleanup;
        }
        printf("  [PASS] Auto-resize: src_size > block_size succeeded\n");
    }

    printf("PASS\n\n");
    result = 1;

cleanup:
    zxc_free_cctx(cctx);  /* safe with NULL */
    zxc_free_dctx(dctx);  /* safe with NULL */
    free(compressed);
    free(decompressed);
    free(src);
    return result;
}

/* Roundtrip helper: compress src into a newly-malloc'd buffer; return compressed size (or <0). */
static int64_t sbs_compress(const uint8_t* src, size_t src_size, int level,
                            int checksum, uint8_t** out_buf, size_t* out_cap) {
    const uint64_t cbound = zxc_compress_block_bound(src_size);
    uint8_t* buf = (uint8_t*)malloc((size_t)cbound);
    if (!buf) return -1;
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_compress_opts_t co = {.level = level, .checksum_enabled = checksum,
                              .block_size = src_size};
    int64_t csz = zxc_compress_block(cctx, src, src_size, buf, (size_t)cbound, &co);
    zxc_free_cctx(cctx);
    if (csz <= 0) { free(buf); return csz; }
    *out_buf = buf;
    *out_cap = (size_t)cbound;
    return csz;
}

int test_decompress_block_safe() {
    printf("=== TEST: Unit - zxc_decompress_block_safe ===\n");

    const size_t sizes[] = {4 * 1024, 64 * 1024, 256 * 1024, 2 * 1024 * 1024};
    const int levels[] = {1, 3, 5, 6};

    /* 1. Roundtrip with dst_capacity == uncompressed_size at multiple sizes & levels. */
    for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        for (size_t li = 0; li < sizeof(levels) / sizeof(levels[0]); li++) {
            for (int checksum = 0; checksum <= 1; checksum++) {
                const size_t n = sizes[si];
                const int lvl = levels[li];
                uint8_t* src = (uint8_t*)malloc(n);
                if (!src) { printf("Failed: malloc src\n"); return 0; }
                gen_lz_data(src, n);

                uint8_t* comp = NULL;
                size_t comp_cap = 0;
                int64_t csz = sbs_compress(src, n, lvl, checksum, &comp, &comp_cap);
                if (csz <= 0) {
                    printf("Failed: compress (n=%zu lvl=%d chk=%d) -> %lld\n",
                           n, lvl, checksum, (long long)csz);
                    free(src); free(comp); return 0;
                }

                uint8_t* dst = (uint8_t*)malloc(n); /* tight: no tail pad */
                if (!dst) { free(src); free(comp); return 0; }

                zxc_dctx* dctx = zxc_create_dctx();
                zxc_decompress_opts_t dopts = {.checksum_enabled = checksum};
                int64_t dsz = zxc_decompress_block_safe(dctx, comp, (size_t)csz,
                                                       dst, n, &dopts);
                int ok = (dsz == (int64_t)n) && memcmp(src, dst, n) == 0;
                zxc_free_dctx(dctx);
                free(dst); free(comp); free(src);
                if (!ok) {
                    printf("Failed: safe roundtrip n=%zu lvl=%d chk=%d -> dsz=%lld\n",
                           n, lvl, checksum, (long long)dsz);
                    return 0;
                }
            }
        }
    }
    printf("  [PASS] safe roundtrip across sizes/levels/checksum\n");

    /* 2. Bit-identical vs zxc_decompress_block on the same compressed payload. */
    {
        const size_t n = 128 * 1024;
        uint8_t* src = (uint8_t*)malloc(n);
        gen_lz_data(src, n);
        uint8_t* comp = NULL;
        size_t comp_cap = 0;
        int64_t csz = sbs_compress(src, n, 3, 0, &comp, &comp_cap);
        const uint64_t dbound = zxc_decompress_block_bound(n);
        uint8_t* dst_fast = (uint8_t*)malloc((size_t)dbound);
        uint8_t* dst_safe = (uint8_t*)malloc(n);

        zxc_dctx* dctx1 = zxc_create_dctx();
        zxc_dctx* dctx2 = zxc_create_dctx();
        int64_t r1 = zxc_decompress_block(dctx1, comp, (size_t)csz, dst_fast,
                                          (size_t)dbound, NULL);
        int64_t r2 = zxc_decompress_block_safe(dctx2, comp, (size_t)csz, dst_safe, n, NULL);
        int ok = (r1 == (int64_t)n) && (r2 == (int64_t)n) &&
                 memcmp(dst_fast, dst_safe, n) == 0;
        zxc_free_dctx(dctx1); zxc_free_dctx(dctx2);
        free(dst_safe); free(dst_fast); free(comp); free(src);
        if (!ok) {
            printf("Failed: safe/fast not bit-identical: r1=%lld r2=%lld\n",
                   (long long)r1, (long long)r2);
            return 0;
        }
        printf("  [PASS] bit-identical output vs fast path\n");
    }

    /* 3. dst_capacity < uncompressed_size -> negative error. */
    {
        const size_t n = 64 * 1024;
        uint8_t* src = (uint8_t*)malloc(n);
        gen_lz_data(src, n);
        uint8_t* comp = NULL;
        size_t comp_cap = 0;
        int64_t csz = sbs_compress(src, n, 3, 0, &comp, &comp_cap);
        uint8_t* dst = (uint8_t*)malloc(n);

        zxc_dctx* dctx = zxc_create_dctx();
        int64_t r = zxc_decompress_block_safe(dctx, comp, (size_t)csz, dst, n - 128, NULL);
        int ok = (r < 0);
        zxc_free_dctx(dctx);
        free(dst); free(comp); free(src);
        if (!ok) {
            printf("Failed: expected negative error for undersized dst, got %lld\n",
                   (long long)r);
            return 0;
        }
        printf("  [PASS] negative error on dst_capacity < uncompressed_size\n");
    }

    /* 4. Literal-heavy input: would trip OVERFLOW in the fast path when
     *    dst_capacity == uncompressed_size; must succeed with the safe API. */
    {
        const size_t n = 32 * 1024;
        uint8_t* src = (uint8_t*)malloc(n);
        gen_random_data(src, n); /* random data -> heavy literal runs, varint-prone */
        uint8_t* comp = NULL;
        size_t comp_cap = 0;
        int64_t csz = sbs_compress(src, n, 3, 0, &comp, &comp_cap);
        if (csz <= 0) { printf("Failed: compress literal-heavy\n"); return 0; }
        uint8_t* dst = (uint8_t*)malloc(n);

        zxc_dctx* dctx = zxc_create_dctx();
        int64_t r = zxc_decompress_block_safe(dctx, comp, (size_t)csz, dst, n, NULL);
        int ok = (r == (int64_t)n) && memcmp(src, dst, n) == 0;
        zxc_free_dctx(dctx);
        free(dst); free(comp); free(src);
        if (!ok) {
            printf("Failed: literal-heavy safe decode: r=%lld\n", (long long)r);
            return 0;
        }
        printf("  [PASS] literal-heavy tail decodes into tight dst\n");
    }

    /* 5. Corrupted stream returns a negative error and does not crash. */
    {
        const size_t n = 16 * 1024;
        uint8_t* src = (uint8_t*)malloc(n);
        gen_lz_data(src, n);
        uint8_t* comp = NULL;
        size_t comp_cap = 0;
        int64_t csz = sbs_compress(src, n, 3, 1, &comp, &comp_cap); /* with checksum */
        /* Flip a byte in the payload to corrupt. */
        comp[ZXC_BLOCK_HEADER_SIZE + (csz - ZXC_BLOCK_HEADER_SIZE) / 2] ^= 0xA5;
        uint8_t* dst = (uint8_t*)malloc(n);

        zxc_dctx* dctx = zxc_create_dctx();
        zxc_decompress_opts_t opts = {.checksum_enabled = 1};
        int64_t r = zxc_decompress_block_safe(dctx, comp, (size_t)csz, dst, n, &opts);
        int ok = (r < 0);
        zxc_free_dctx(dctx);
        free(dst); free(comp); free(src);
        if (!ok) {
            printf("Failed: corrupted stream should fail, got %lld\n", (long long)r);
            return 0;
        }
        printf("  [PASS] corrupted stream -> negative error (no crash)\n");
    }

    printf("PASS\n\n");
    return 1;
}

int test_decompress_block_bound() {
    printf("=== TEST: Unit - zxc_decompress_block_bound ===\n");

    /* 1. Sanity: helper must return more than the input (tail pad > 0). */
    {
        const size_t n = 4096;
        const uint64_t b = zxc_decompress_block_bound(n);
        if (b <= n) {
            printf("Failed: bound(%zu)=%llu must exceed input (tail pad missing)\n", n,
                   (unsigned long long)b);
            return 0;
        }
        /* Pad is a fixed margin, so the delta must be constant. */
        const uint64_t pad = b - n;
        const uint64_t b2 = zxc_decompress_block_bound(n * 4);
        if (b2 - n * 4 != pad) {
            printf("Failed: tail pad must be constant, got %llu vs %llu\n",
                   (unsigned long long)pad, (unsigned long long)(b2 - n * 4));
            return 0;
        }
        printf("  [PASS] bound(n) = n + %llu (constant tail pad)\n", (unsigned long long)pad);
    }

    /* 2. Overflow: huge input must return 0. */
    {
        if (zxc_decompress_block_bound(SIZE_MAX) != 0) {
            printf("Failed: bound(SIZE_MAX) must return 0 on overflow\n");
            return 0;
        }
        printf("  [PASS] bound(SIZE_MAX) -> 0 (overflow guard)\n");
    }

    /* 3. Edge: bound(0) must still return a valid non-zero pad. */
    {
        if (zxc_decompress_block_bound(0) == 0) {
            printf("Failed: bound(0) must be > 0 (tail pad always required)\n");
            return 0;
        }
        printf("  [PASS] bound(0) > 0\n");
    }

    /* 4. Functional: a roundtrip using bound-sized dst must succeed. */
    {
        const size_t src_size = 64 * 1024;
        uint8_t* src = malloc(src_size);
        if (!src) return 0;
        gen_lz_data(src, src_size);

        const uint64_t cbound = zxc_compress_block_bound(src_size);
        uint8_t* compressed = malloc((size_t)cbound);
        const uint64_t dbound = zxc_decompress_block_bound(src_size);
        uint8_t* decompressed = malloc((size_t)dbound);

        zxc_cctx* cctx = zxc_create_cctx(NULL);
        zxc_dctx* dctx = zxc_create_dctx();

        int ok = 0;
        if (compressed && decompressed && cctx && dctx) {
            zxc_compress_opts_t copts = {.level = 3};
            int64_t csize = zxc_compress_block(cctx, src, src_size, compressed,
                                               (size_t)cbound, &copts);
            if (csize > 0) {
                int64_t dsize = zxc_decompress_block(dctx, compressed, (size_t)csize,
                                                     decompressed, (size_t)dbound, NULL);
                ok = (dsize == (int64_t)src_size) &&
                     memcmp(src, decompressed, src_size) == 0;
            }
        }

        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
        free(decompressed);
        free(compressed);
        free(src);

        if (!ok) {
            printf("Failed: roundtrip with bound-sized dst failed\n");
            return 0;
        }
        printf("  [PASS] roundtrip into bound-sized dst OK\n");
    }

    printf("PASS\n\n");
    return 1;
}

/**
 * @brief Stress-test the block API with boundary sizes across all levels.
 *
 * Tests zxc_compress_block / zxc_decompress_block with input sizes carefully
 * chosen to land near internal buffer limits (mflimit, page boundaries).
 *
 * This test covers:
 *   - Sizes near the LZ match-finder safety margin (12-20 bytes)
 *   - Odd sizes that stress alignment assumptions
 *   - Data patterns that trigger each block type encoder (GLO, GHI, NUM, RAW)
 *   - All compression levels
 */
int test_block_api_boundary_sizes() {
    printf("=== TEST: Block API - Boundary Sizes ===\n");

    /* Edge-case sizes: near mflimit (iend-12), near page boundaries, odd,
     * and large block sizes (128KB - 2MB) */
    const size_t sizes[] = {
        13, 14, 15, 16, 17, 19, 20, 23, 24, 25,       /* Near mflimit margin */
        31, 32, 33, 48, 63, 64, 65,                     /* Cache line edges */
        100, 127, 128, 129, 255, 256, 257,               /* Byte boundary edges */
        511, 512, 513, 1023, 1024, 1025,                 /* 1 KB edges */
        4095, 4096, 4097,                                /* Page boundary */
        8191, 8192, 8193,                                /* 2-page boundary */
        16383, 16384, 16385,                             /* 4-page boundary */
        65535, 65536, 65537,                             /* 64 KB boundary */
        128 * 1024, 128 * 1024 + 1,                      /* 128 KB block */
        256 * 1024, 256 * 1024 - 1,                      /* 256 KB block */
        512 * 1024,                                      /* 512 KB block */
        1024 * 1024,                                     /* 1 MB */
        2 * 1024 * 1024,                                 /* 2 MB max block */
    };
    const int num_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));

    /* Data generators: each triggers a different encoder path */
    typedef void (*gen_fn)(uint8_t*, size_t);
    const struct {
        const char* name;
        gen_fn gen;
    } patterns[] = {
        {"LZ (GLO/GHI)", gen_lz_data},
        {"Random (RAW)", gen_random_data},
        {"Numeric (NUM)", gen_num_data},
    };
    const int num_patterns = (int)(sizeof(patterns) / sizeof(patterns[0]));

    const size_t max_size = sizes[num_sizes - 1];
    uint8_t* src = malloc(max_size);
    const uint64_t bound = zxc_compress_block_bound(max_size);
    uint8_t* compressed = malloc((size_t)bound);
    uint8_t* decompressed = malloc(max_size);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_dctx* dctx = zxc_create_dctx();

    if (!src || !compressed || !decompressed || !cctx || !dctx) {
        printf("  [FAIL] allocation failed\n");
        free(src); free(compressed); free(decompressed);
        zxc_free_cctx(cctx); zxc_free_dctx(dctx);
        return 0;
    }

    int failures = 0;

    for (int p = 0; p < num_patterns; p++) {
        /* Generate data once at max size; smaller tests use prefix */
        patterns[p].gen(src, max_size);

        for (int lvl = 1; lvl <= 5; lvl++) {
            for (int s = 0; s < num_sizes; s++) {
                const size_t sz = sizes[s];
                /* NUM encoder needs size >= 16 && multiple of 4 */
                if (p == 2 && (sz < 16 || sz % 4 != 0)) continue;

                zxc_compress_opts_t copts = {.level = lvl, .checksum_enabled = 0};
                const int64_t csize = zxc_compress_block(
                    cctx, src, sz, compressed, (size_t)bound, &copts);

                if (csize <= 0) {
                    /* RAW fallback or incompressible is OK, but actual errors are not */
                    if (csize < 0 && csize != ZXC_ERROR_DST_TOO_SMALL) {
                        printf("  [FAIL] %s lvl=%d sz=%zu: compress error %lld\n",
                               patterns[p].name, lvl, sz, (long long)csize);
                        failures++;
                    }
                    continue;
                }

                zxc_decompress_opts_t dopts = {.checksum_enabled = 0};
                const int64_t dsize = zxc_decompress_block(
                    dctx, compressed, (size_t)csize, decompressed, sz, &dopts);

                if (dsize != (int64_t)sz) {
                    printf("  [FAIL] %s lvl=%d sz=%zu: decompress returned %lld (expected %zu)\n",
                           patterns[p].name, lvl, sz, (long long)dsize, sz);
                    failures++;
                    continue;
                }

                if (memcmp(src, decompressed, sz) != 0) {
                    printf("  [FAIL] %s lvl=%d sz=%zu: content mismatch\n",
                           patterns[p].name, lvl, sz);
                    failures++;
                }
            }
        }
    }

    free(src); free(compressed); free(decompressed);
    zxc_free_cctx(cctx); zxc_free_dctx(dctx);

    if (failures > 0) {
        printf("  [FAIL] %d sub-tests failed\n", failures);
        return 0;
    }

    printf("  [PASS] All boundary sizes passed (%d patterns x 5 levels x %d sizes)\n",
           num_patterns, num_sizes);
    return 1;
}

/*
 * Regression test for blocks > 2 MiB.
 *
 * Before the varint extension, zxc_write_varint silently truncated LL/ML
 * values >= 2^21 (21 bits), corrupting the output for any block that
 * produced a literal run or match length exceeding 2 MiB. The encoder
 * now writes 4- and 5-byte varints (28/32 bits), matching the decoder
 * which already supported them.
 *
 * This test round-trips highly repetitive data in blocks of 3, 4 and 8 MiB.
 * The LZ77 path on such data produces match lengths close to block size,
 * reliably exercising the 4-byte varint branch.
 */
int test_block_api_large_block_varint() {
    printf("=== TEST: Block API - Varint >21 bits (blocks > 2 MiB) ===\n");

    const struct {
        size_t size;
        const char* name;
    } cases[] = {
        {3 * 1024 * 1024, "3 MiB"},
        {4 * 1024 * 1024, "4 MiB"},
        {8 * 1024 * 1024, "8 MiB"},
    };
    const int num_cases = (int)(sizeof(cases) / sizeof(cases[0]));

    int failures = 0;

    for (int i = 0; i < num_cases; i++) {
        const size_t sz = cases[i].size;
        uint8_t* src = malloc(sz);
        const uint64_t bound = zxc_compress_block_bound(sz);
        uint8_t* compressed = bound ? malloc((size_t)bound) : NULL;
        uint8_t* decompressed = malloc(sz);
        zxc_cctx* cctx = zxc_create_cctx(NULL);
        zxc_dctx* dctx = zxc_create_dctx();

        if (!src || !compressed || !decompressed || !cctx || !dctx) {
            printf("  [FAIL] %s: allocation failed\n", cases[i].name);
            failures++;
            goto per_case_cleanup;
        }

        /* Repetitive pattern: after the initial ~400 byte literal run, the
         * rest of the buffer matches, producing a match length close to sz
         * (well above 2^21) triggering the 4-byte varint encoding path. */
        gen_lz_data(src, sz);

        for (int lvl = 1; lvl <= 5; lvl++) {
            zxc_compress_opts_t copts = {.level = lvl, .checksum_enabled = 1};
            const int64_t csize = zxc_compress_block(cctx, src, sz, compressed,
                                                     (size_t)bound, &copts);
            if (csize <= 0) {
                printf("  [FAIL] %s lvl=%d: compress returned %lld\n",
                       cases[i].name, lvl, (long long)csize);
                failures++;
                continue;
            }

            zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
            const int64_t dsize = zxc_decompress_block(dctx, compressed, (size_t)csize,
                                                      decompressed, sz, &dopts);
            if (dsize != (int64_t)sz) {
                printf("  [FAIL] %s lvl=%d: decompress returned %lld (expected %zu)\n",
                       cases[i].name, lvl, (long long)dsize, sz);
                failures++;
                continue;
            }
            if (memcmp(src, decompressed, sz) != 0) {
                printf("  [FAIL] %s lvl=%d: content mismatch after roundtrip\n",
                       cases[i].name, lvl);
                failures++;
                continue;
            }
        }

        if (!failures) {
            printf("  [PASS] %s roundtrip OK across levels 1-5\n", cases[i].name);
        }

    per_case_cleanup:
        free(src);
        free(compressed);
        free(decompressed);
        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
    }

    if (failures > 0) {
        printf("FAILED: %d sub-tests failed\n", failures);
        return 0;
    }
    printf("PASS\n\n");
    return 1;
}
