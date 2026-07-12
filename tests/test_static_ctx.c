/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

#if defined(_WIN32)
#include <malloc.h>
static void* test_aligned_alloc(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}
static void test_aligned_free(void* p) { _aligned_free(p); }
#else
static void* test_aligned_alloc(size_t alignment, size_t size) {
    void* p = NULL;
    if (posix_memalign(&p, alignment, size) != 0) return NULL;
    return p;
}
static void test_aligned_free(void* p) { free(p); }
#endif

/* Helper: produce a deterministic compressible payload. */
static void fill_payload(uint8_t* dst, size_t n) {
    for (size_t i = 0; i < n; ++i) dst[i] = (uint8_t)((i * 31u) ^ (i >> 8));
}

/* Roundtrip through a caller-allocated cctx + dctx workspace, at every level. */
int test_static_ctx_roundtrip_all_levels(void) {
    printf("=== TEST: Static Context API - roundtrip at every level ===\n");

    const size_t block_size = 64 * 1024;
    const size_t src_sz = 200 * 1024; /* spans multiple blocks */

    uint8_t* const src = (uint8_t*)malloc(src_sz);
    if (!src) {
        printf("  [FAIL] malloc(src)\n");
        return 0;
    }
    fill_payload(src, src_sz);

    const size_t cap = (size_t)zxc_compress_bound(src_sz);
    uint8_t* const enc = (uint8_t*)malloc(cap);
    uint8_t* const dec = (uint8_t*)malloc(src_sz);
    if (!enc || !dec) {
        printf("  [FAIL] malloc(enc/dec)\n");
        free(src);
        free(enc);
        free(dec);
        return 0;
    }

    for (int lvl = zxc_min_level(); lvl <= zxc_max_level(); ++lvl) {
        /* Size the cctx workspace exactly. */
        const size_t cctx_ws_sz = zxc_static_cctx_workspace_size(block_size, lvl);
        if (cctx_ws_sz == 0) {
            printf("  [FAIL] level %d: cctx_ws_sz == 0\n", lvl);
            goto fail;
        }
        void* const cctx_ws = test_aligned_alloc(64, cctx_ws_sz);
        if (!cctx_ws) {
            printf("  [FAIL] level %d: aligned_alloc(cctx_ws)\n", lvl);
            goto fail;
        }

        zxc_compress_opts_t copts = {
            .level = lvl, .block_size = block_size, .checksum_enabled = 1};
        zxc_cctx* const cctx = zxc_init_static_cctx(cctx_ws, cctx_ws_sz, &copts);
        if (!cctx) {
            printf("  [FAIL] level %d: zxc_init_static_cctx returned NULL\n", lvl);
            test_aligned_free(cctx_ws);
            goto fail;
        }

        const int64_t csz = zxc_compress_cctx(cctx, src, src_sz, enc, cap, NULL);
        zxc_free_cctx(cctx); /* no-op for static */
        test_aligned_free(cctx_ws);
        if (csz <= 0) {
            printf("  [FAIL] level %d: compress returned %lld\n", lvl, (long long)csz);
            goto fail;
        }

        /* Size + init the dctx workspace. */
        const size_t dctx_ws_sz = zxc_static_dctx_workspace_size(block_size);
        if (dctx_ws_sz == 0) {
            printf("  [FAIL] level %d: dctx_ws_sz == 0\n", lvl);
            goto fail;
        }
        void* const dctx_ws = test_aligned_alloc(64, dctx_ws_sz);
        if (!dctx_ws) {
            printf("  [FAIL] level %d: aligned_alloc(dctx_ws)\n", lvl);
            goto fail;
        }
        zxc_dctx* const dctx = zxc_init_static_dctx(dctx_ws, dctx_ws_sz, block_size);
        if (!dctx) {
            printf("  [FAIL] level %d: zxc_init_static_dctx returned NULL\n", lvl);
            test_aligned_free(dctx_ws);
            goto fail;
        }

        zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
        const int64_t dsz = zxc_decompress_dctx(dctx, enc, (size_t)csz, dec, src_sz, &dopts);
        zxc_free_dctx(dctx); /* no-op for static */
        test_aligned_free(dctx_ws);
        if (dsz != (int64_t)src_sz || memcmp(src, dec, src_sz) != 0) {
            printf("  [FAIL] level %d: roundtrip mismatch (dsz=%lld)\n", lvl, (long long)dsz);
            goto fail;
        }

        printf("  [PASS] level %d: roundtrip OK (%lld bytes -> %lld bytes)\n", lvl,
               (long long)src_sz, (long long)csz);
    }

    free(src);
    free(enc);
    free(dec);
    return 1;

fail:
    free(src);
    free(enc);
    free(dec);
    return 0;
}

/* Verify the workspace sizers reject invalid inputs and accept the smallest
 * valid configuration. */
int test_static_ctx_size_query(void) {
    printf("=== TEST: Static Context API - workspace_size queries ===\n");

    /* Invalid: zero block_size. */
    if (zxc_static_cctx_workspace_size(0, ZXC_LEVEL_DEFAULT) != 0) {
        printf("  [FAIL] cctx_size(0) should be 0\n");
        return 0;
    }
    if (zxc_static_dctx_workspace_size(0) != 0) {
        printf("  [FAIL] dctx_size(0) should be 0\n");
        return 0;
    }
    /* Invalid: non-power-of-two block_size. */
    if (zxc_static_cctx_workspace_size(63 * 1024, ZXC_LEVEL_DEFAULT) != 0) {
        printf("  [FAIL] cctx_size(non-pow2) should be 0\n");
        return 0;
    }
    /* Invalid: out-of-range level. */
    if (zxc_static_cctx_workspace_size(64 * 1024, 0) != 0) {
        printf("  [FAIL] cctx_size(level=0) should be 0\n");
        return 0;
    }
    if (zxc_static_cctx_workspace_size(64 * 1024, 99) != 0) {
        printf("  [FAIL] cctx_size(level=99) should be 0\n");
        return 0;
    }

    /* Valid sizes are strictly increasing across levels (level 6 adds opt_scratch). */
    const size_t s3 = zxc_static_cctx_workspace_size(64 * 1024, 3);
    const size_t s5 = zxc_static_cctx_workspace_size(64 * 1024, 5);
    const size_t s6 = zxc_static_cctx_workspace_size(64 * 1024, 6);
    if (s3 == 0 || s5 == 0 || s6 == 0) {
        printf("  [FAIL] one of s3/s5/s6 is 0\n");
        return 0;
    }
    if (s3 != s5) {
        printf("  [FAIL] s3 (%zu) should equal s5 (%zu); only level 6 adds opt_scratch\n", s3, s5);
        return 0;
    }
    if (s6 <= s5) {
        printf("  [FAIL] s6 (%zu) should exceed s5 (%zu)\n", s6, s5);
        return 0;
    }
    printf("  [PASS] level-1..5 share workspace size; level 6 adds opt_scratch\n");
    return 1;
}

/* Verify that init returns NULL when the workspace is too small. */
int test_static_ctx_workspace_too_small(void) {
    printf("=== TEST: Static Context API - workspace too small ===\n");

    const size_t block_size = 64 * 1024;
    const size_t needed = zxc_static_cctx_workspace_size(block_size, ZXC_LEVEL_DEFAULT);
    /* Provide exactly one byte less than needed. */
    void* const ws = test_aligned_alloc(64, needed);
    if (!ws) {
        printf("  [FAIL] aligned_alloc\n");
        return 0;
    }

    zxc_compress_opts_t opts = {.level = ZXC_LEVEL_DEFAULT, .block_size = block_size};
    if (zxc_init_static_cctx(ws, needed - 1, &opts) != NULL) {
        printf("  [FAIL] init should reject undersized workspace\n");
        test_aligned_free(ws);
        return 0;
    }
    /* Same size: should succeed. */
    zxc_cctx* const cctx = zxc_init_static_cctx(ws, needed, &opts);
    if (!cctx) {
        printf("  [FAIL] init should accept exact-sized workspace\n");
        test_aligned_free(ws);
        return 0;
    }
    zxc_free_cctx(cctx); /* no-op */
    test_aligned_free(ws);
    printf("  [PASS] init rejects undersized, accepts exact-sized workspace\n");
    return 1;
}

/* Verify the block_size lock on a static cctx: compressing with a different
 * block_size in opts must return ZXC_ERROR_BAD_BLOCK_SIZE without crashing. */
int test_static_ctx_block_size_locked(void) {
    printf("=== TEST: Static Context API - block_size lock ===\n");

    const size_t pinned_bs = 64 * 1024;
    const size_t ws_sz = zxc_static_cctx_workspace_size(pinned_bs, ZXC_LEVEL_DEFAULT);
    void* const ws = test_aligned_alloc(64, ws_sz);
    if (!ws) {
        printf("  [FAIL] aligned_alloc\n");
        return 0;
    }

    zxc_compress_opts_t opts = {.level = ZXC_LEVEL_DEFAULT, .block_size = pinned_bs};
    zxc_cctx* const cctx = zxc_init_static_cctx(ws, ws_sz, &opts);
    if (!cctx) {
        printf("  [FAIL] init_static_cctx\n");
        test_aligned_free(ws);
        return 0;
    }

    /* A subsequent compress with a different block_size must fail. */
    uint8_t src[256] = {0};
    uint8_t dst[1024];
    zxc_compress_opts_t opts2 = {.level = ZXC_LEVEL_DEFAULT, .block_size = pinned_bs * 2};
    const int64_t rc =
        zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst), &opts2);
    if (rc != ZXC_ERROR_BAD_BLOCK_SIZE) {
        printf("  [FAIL] expected ZXC_ERROR_BAD_BLOCK_SIZE, got %lld\n", (long long)rc);
        zxc_free_cctx(cctx);
        test_aligned_free(ws);
        return 0;
    }

    /* Same block_size still works. */
    const int64_t rc2 = zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst), NULL);
    if (rc2 <= 0) {
        printf("  [FAIL] same-block_size compress returned %lld\n", (long long)rc2);
        zxc_free_cctx(cctx);
        test_aligned_free(ws);
        return 0;
    }

    zxc_free_cctx(cctx);
    test_aligned_free(ws);
    printf("  [PASS] mismatched block_size rejected; matching one accepted\n");
    return 1;
}

/* Regression: a static cctx carved below ZXC_LEVEL_DENSITY has no opt_scratch
 * and its workspace cannot grow. A per-call raise into the optimal-parser tier
 * must be rejected with ZXC_ERROR_BAD_LEVEL - before the fix it silently
 * heap-allocated a replacement workspace (violating the no-allocation
 * contract) and leaked it, or crashed on the NULL scratch via the frame API. */
int test_static_ctx_level_raise_rejected(void) {
    printf("=== TEST: Static Context API - level raise rejected ===\n");

    const size_t pinned_bs = 64 * 1024;
    const size_t ws_sz = zxc_static_cctx_workspace_size(pinned_bs, ZXC_LEVEL_DEFAULT);
    void* const ws = test_aligned_alloc(64, ws_sz);
    if (!ws) {
        printf("  [FAIL] aligned_alloc\n");
        return 0;
    }

    zxc_compress_opts_t opts = {.level = ZXC_LEVEL_DEFAULT, .block_size = pinned_bs};
    zxc_cctx* const cctx = zxc_init_static_cctx(ws, ws_sz, &opts);
    if (!cctx) {
        printf("  [FAIL] init_static_cctx\n");
        test_aligned_free(ws);
        return 0;
    }

    uint8_t src[256] = {0};
    uint8_t dst[1024];

    /* Frame API: raise to 7 must fail cleanly. */
    zxc_compress_opts_t raise = {.level = ZXC_LEVEL_ULTRA, .block_size = pinned_bs};
    const int64_t rc = zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst), &raise);
    if (rc != ZXC_ERROR_BAD_LEVEL) {
        printf("  [FAIL] frame raise: expected ZXC_ERROR_BAD_LEVEL, got %lld\n", (long long)rc);
        goto fail;
    }

    /* Block API: raise to 6 must fail cleanly too. */
    zxc_compress_opts_t raise6 = {.level = ZXC_LEVEL_DENSITY, .block_size = pinned_bs};
    const int64_t rc2 = zxc_compress_block(cctx, src, sizeof(src), dst, sizeof(dst), &raise6);
    if (rc2 != ZXC_ERROR_BAD_LEVEL) {
        printf("  [FAIL] block raise: expected ZXC_ERROR_BAD_LEVEL, got %lld\n", (long long)rc2);
        goto fail;
    }

    /* The context is still usable at its pinned level afterwards. */
    const int64_t rc3 = zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst), NULL);
    if (rc3 <= 0) {
        printf("  [FAIL] pinned-level compress after rejection returned %lld\n", (long long)rc3);
        goto fail;
    }

    /* A static workspace carved AT the dense tier accepts its own level. */
    {
        const size_t ws7_sz = zxc_static_cctx_workspace_size(pinned_bs, ZXC_LEVEL_ULTRA);
        void* const ws7 = test_aligned_alloc(64, ws7_sz);
        if (!ws7) {
            printf("  [FAIL] aligned_alloc (level 7 ws)\n");
            goto fail;
        }
        zxc_compress_opts_t opts7 = {.level = ZXC_LEVEL_ULTRA, .block_size = pinned_bs};
        zxc_cctx* const cctx7 = zxc_init_static_cctx(ws7, ws7_sz, &opts7);
        if (!cctx7 ||
            zxc_compress_cctx(cctx7, src, sizeof(src), dst, sizeof(dst), &opts7) <= 0) {
            printf("  [FAIL] level-7 static cctx should compress at level 7\n");
            test_aligned_free(ws7);
            goto fail;
        }
        test_aligned_free(ws7);
    }

    zxc_free_cctx(cctx);
    test_aligned_free(ws);
    printf("  [PASS] dense-tier raise rejected, pinned level still works\n");
    return 1;

fail:
    zxc_free_cctx(cctx);
    test_aligned_free(ws);
    return 0;
}

/* Verify NULL inputs are gracefully rejected. */
int test_static_ctx_null_inputs(void) {
    printf("=== TEST: Static Context API - NULL inputs ===\n");

    zxc_compress_opts_t opts = {.level = 3, .block_size = 4096};
    if (zxc_init_static_cctx(NULL, 65536, &opts) != NULL) {
        printf("  [FAIL] init_static_cctx(NULL workspace) should fail\n");
        return 0;
    }
    uint8_t ws[16384];
    if (zxc_init_static_cctx(ws, sizeof(ws), NULL) != NULL) {
        printf("  [FAIL] init_static_cctx(NULL opts) should fail\n");
        return 0;
    }
    if (zxc_init_static_dctx(NULL, 65536, 4096) != NULL) {
        printf("  [FAIL] init_static_dctx(NULL workspace) should fail\n");
        return 0;
    }
    /* zxc_free_*ctx must accept a NULL pointer (idempotency). */
    zxc_free_cctx(NULL);
    zxc_free_dctx(NULL);
    printf("  [PASS] NULL inputs rejected; NULL free is idempotent\n");
    return 1;
}
