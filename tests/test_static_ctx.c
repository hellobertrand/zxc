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
    for (size_t i = 0; i < n; ++i) dst[i] = (uint8_t)((i * 31U) ^ (i >> 8));
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

        zxc_compress_opts_t copts = {.level = lvl, .block_size = block_size, .checksum_enabled = 1};
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
    const int64_t rc = zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst), &opts2);
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
        if (!cctx7 || zxc_compress_cctx(cctx7, src, sizeof(src), dst, sizeof(dst), &opts7) <= 0) {
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

/* Decodes the n-byte block; a failed compression (n <= 0) propagates. */
static int64_t dec(zxc_dctx* d, const uint8_t* blk, int64_t n, uint8_t* out, size_t cap,
                   int strict) {
    if (n <= 0) return n;
    return strict ? zxc_decompress_block_safe(d, blk, (size_t)n, out, cap, NULL)
                  : zxc_decompress_block(d, blk, (size_t)n, out, cap, NULL);
}

/* Static dctx block API: the carved block is the effective capacity. Fitting
 * blocks decode whatever the buffer; larger ones are BAD_BLOCK_SIZE when the
 * sub-header or decoded size tells, else a too-small-destination code;
 * corruption keeps the dynamic codes. */
int test_static_dctx_block_bounds(void) {
    printf("=== TEST: Static dctx - blocks bounded by the carved block ===\n");
    enum { PIN = 4096, MID = 5000, XL = 8000, LIT = 4500, PAD = ZXC_DECOMPRESS_TAIL_PAD };
    const size_t cap = (size_t)zxc_compress_block_bound(XL);
    uint8_t* lz = (uint8_t*)malloc(XL);          /* compressible: GLO */
    uint8_t* rnd = (uint8_t*)malloc(XL);         /* incompressible: RAW */
    uint8_t* lit = (uint8_t*)malloc(LIT + 1000); /* Huffman literals, then a repeat */
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* mut = (uint8_t*)malloc(cap);
    uint8_t* out = (uint8_t*)malloc(2 * PIN + PAD);
    uint8_t* out2 = (uint8_t*)malloc(2 * PIN + PAD);
    const size_t ws_sz = zxc_static_dctx_workspace_size(PIN);
    void* ws = test_aligned_alloc(64, ws_sz);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_dctx* hd = zxc_create_dctx();
    zxc_dctx* sd = ws ? zxc_init_static_dctx(ws, ws_sz, PIN) : NULL;
    int ok = 0;
    do {
        if (!lz || !rnd || !lit || !comp || !mut || !out || !out2 || !cctx || !hd || !sd) {
            printf("  [FAIL] setup\n");
            break;
        }
        zxc_test_srand(0x5747u);
        gen_lz_data(lz, XL);
        gen_random_data(rnd, XL);
        for (size_t i = 0; i < LIT; i++)
            lit[i] = (uint8_t)("0123456789abcdef"[zxc_test_rand() & 15]);
        memcpy(lit + LIT, lit, 1000);
        const zxc_compress_opts_t l3 = {.level = 3};
        const zxc_compress_opts_t l6 = {.level = 6};

        /* A fitting block: both decoders, exact and larger buffers. */
        const int64_t f = zxc_compress_block(cctx, lz, PIN, comp, cap, &l3);
        const int64_t f1 = dec(sd, comp, f, out, PIN + PAD, 0);
        const int64_t f2 = dec(sd, comp, f, out2, 2 * PIN + PAD, 0);
        const int64_t f3 = dec(sd, comp, f, out, PIN, 1);
        const int64_t f4 = dec(sd, comp, f, out2, 2 * PIN, 1);
        if (f1 != PIN || f2 != PIN || f3 != PIN || f4 != PIN || memcmp(out, lz, PIN) != 0 ||
            memcmp(out2, lz, PIN) != 0) {
            printf("  [FAIL] fitting block: %lld %lld %lld %lld\n", (long long)f1, (long long)f2,
                   (long long)f3, (long long)f4);
            break;
        }
        printf("  [PASS] a fitting block decodes on both decoders, larger buffers included\n");

        /* Within the margin: the decoded size tells, whatever the buffer; the
         * strict decoder at exactly the carved size says "does not fit", as a
         * dynamic dctx would. */
        const int64_t m = zxc_compress_block(cctx, lz, MID, comp, cap, &l3);
        const int64_t m1 = dec(sd, comp, m, out, MID + PAD, 0);
        const int64_t m2 = dec(sd, comp, m, out, PIN, 0);
        const int64_t m3 = dec(sd, comp, m, out, MID, 1);
        const int64_t m4 = dec(sd, comp, m, out, PIN, 1);
        const int64_t mr = zxc_compress_block(cctx, rnd, MID, comp, cap, &l3);
        const uint8_t mr_type = mr > 0 ? comp[0] : 0;
        const int64_t m5 = dec(sd, comp, mr, out, MID + PAD, 0);
        const int64_t m6 = dec(sd, comp, mr, out, MID, 1);
        const int64_t m7 = dec(sd, comp, mr, out, PIN, 1);
        if (m1 != ZXC_ERROR_BAD_BLOCK_SIZE || m2 != ZXC_ERROR_BAD_BLOCK_SIZE ||
            m3 != ZXC_ERROR_BAD_BLOCK_SIZE || m4 != ZXC_ERROR_OVERFLOW ||
            mr_type != ZXC_BLOCK_RAW || m5 != ZXC_ERROR_BAD_BLOCK_SIZE ||
            m6 != ZXC_ERROR_BAD_BLOCK_SIZE || m7 != ZXC_ERROR_DST_TOO_SMALL) {
            printf("  [FAIL] within margin: GLO %lld %lld %lld %lld, RAW(type %u) %lld %lld %lld\n",
                   (long long)m1, (long long)m2, (long long)m3, (long long)m4, mr_type,
                   (long long)m5, (long long)m6, (long long)m7);
            break;
        }
        printf(
            "  [PASS] within the margin -> BAD_BLOCK_SIZE, strict at exact size -> not fitting\n");

        /* Beyond the margin: workspace overflow, the codes a dynamic dctx
         * gives for a too-small destination, buffer-independent. */
        const int64_t x = zxc_compress_block(cctx, lz, XL, comp, cap, &l3);
        const int64_t x1 = dec(sd, comp, x, out, XL + 200, 0);
        const int64_t x2 = dec(sd, comp, x, out, PIN, 0);
        const int64_t x3 = dec(sd, comp, x, out, XL, 1);
        const int64_t xr = zxc_compress_block(cctx, rnd, XL, comp, cap, &l3);
        const int64_t x4 = dec(sd, comp, xr, out, XL + 200, 0);
        const int64_t x5 = dec(sd, comp, xr, out, PIN, 0);
        const int64_t x6 = dec(sd, comp, xr, out, XL, 1);
        if (x1 != ZXC_ERROR_OVERFLOW || x2 != ZXC_ERROR_OVERFLOW ||
            x3 != ZXC_ERROR_BAD_BLOCK_SIZE || x4 != ZXC_ERROR_DST_TOO_SMALL ||
            x5 != ZXC_ERROR_DST_TOO_SMALL || x6 != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] beyond margin: GLO %lld %lld %lld, RAW %lld %lld %lld\n",
                   (long long)x1, (long long)x2, (long long)x3, (long long)x4, (long long)x5,
                   (long long)x6);
            break;
        }
        printf("  [PASS] beyond the margin -> workspace overflow codes, buffer-independent\n");

        /* More Huffman literals than the carved block: told by the sub-header,
         * both decoders. */
        const int64_t h = zxc_compress_block(cctx, lit, LIT + 1000, comp, cap, &l6);
        zxc_gnr_header_t gh = {0};
        uint32_t lc = 0, tc = 0;
        const int hdr =
            h > 0 ? zxc_read_glo_header_and_desc(comp + ZXC_BLOCK_HEADER_SIZE,
                                                 (size_t)h - ZXC_BLOCK_HEADER_SIZE, &gh, &lc, &tc)
                  : -1;
        const int64_t h1 = dec(sd, comp, h, out, LIT + 1000 + PAD, 0);
        const int64_t h2 = dec(sd, comp, h, out, LIT + 1000, 1);
        if (h <= 0 || comp[0] != ZXC_BLOCK_GLO || hdr <= 0 ||
            gh.enc_lit != ZXC_SECTION_ENCODING_HUFFMAN || gh.n_literals <= PIN ||
            h1 != ZXC_ERROR_BAD_BLOCK_SIZE || h2 != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] literal-heavy: type %u enc_lit %u n_literals %u -> %lld %lld\n",
                   h > 0 ? comp[0] : 0U, gh.enc_lit, gh.n_literals, (long long)h1, (long long)h2);
            break;
        }
        const zxc_compress_opts_t l1 = {.level = 1};
        const int64_t k = zxc_compress_block(cctx, lit, LIT + 1000, comp, cap, &l1);
        const int64_t k1 = dec(sd, comp, k, out, LIT + 1000 + PAD, 0);
        const int64_t k2 = dec(sd, comp, k, out, LIT + 1000, 1);
        if (k <= 0 || comp[0] != ZXC_BLOCK_GHI || k1 != ZXC_ERROR_BAD_BLOCK_SIZE ||
            k2 != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] literal-heavy GHI: type %u -> %lld %lld\n", k > 0 ? comp[0] : 0U,
                   (long long)k1, (long long)k2);
            break;
        }
        printf(
            "  [PASS] %u Huffman literals (GLO) and the GHI form -> BAD_BLOCK_SIZE before "
            "decoding\n",
            gh.n_literals);

        /* A dictionary is refused first, on both decoders, whatever the buffer. */
        const zxc_decompress_opts_t with_dict = {.dict = lz, .dict_size = 16};
        const int64_t d1 =
            zxc_decompress_block(sd, comp, (size_t)h, out, 2 * PIN + PAD, &with_dict);
        const int64_t d2 = zxc_decompress_block_safe(sd, comp, (size_t)h, out, 2 * PIN, &with_dict);
        if (d1 != ZXC_ERROR_DICT_UNSUPPORTED || d2 != ZXC_ERROR_DICT_UNSUPPORTED) {
            printf("  [FAIL] dictionary on a static dctx: %lld %lld\n", (long long)d1,
                   (long long)d2);
            break;
        }
        printf("  [PASS] dictionary -> DICT_UNSUPPORTED on both decoders\n");

        /* Corruption keeps its codes: each single-bit flip gives the same
         * verdict on the static and on a dynamic dctx, bar two by design: a
         * block grown past the carved block (static names it, dynamic says
         * too-small destination) and a sub-header announcing more literals
         * than the carved block (a size violation before decoding). */
        const int64_t g = zxc_compress_block(cctx, lz, PIN, comp, cap, &l3);
        int mismatches = 0;
        for (size_t bit = 0; g > 0 && bit < (size_t)g * 8; bit++) {
            memcpy(mut, comp, (size_t)g);
            mut[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
            const int64_t rs = zxc_decompress_block(sd, mut, (size_t)g, out, PIN, NULL);
            const int64_t rd = zxc_decompress_block(hd, mut, (size_t)g, out2, PIN, NULL);
            const int same = rs == rd && (rs <= 0 || memcmp(out, out2, (size_t)rs) == 0);
            const int grown = rs == ZXC_ERROR_BAD_BLOCK_SIZE && rd == ZXC_ERROR_DST_TOO_SMALL;
            zxc_gnr_header_t mh;
            uint32_t mlc, mtc;
            const int announced = rs == ZXC_ERROR_BAD_BLOCK_SIZE &&
                                  zxc_read_glo_header_and_desc(mut + ZXC_BLOCK_HEADER_SIZE,
                                                               (size_t)g - ZXC_BLOCK_HEADER_SIZE,
                                                               &mh, &mlc, &mtc) > 0 &&
                                  mh.n_literals > PIN;
            if (!same && !grown && !announced) {
                if (mismatches++ < 3)
                    printf("  [FAIL] bit %zu: static %lld, dynamic %lld\n", bit, (long long)rs,
                           (long long)rd);
            }
        }
        if (g <= 0 || mismatches) break;
        printf("  [PASS] %lld bit flips: same verdict as a dynamic dctx\n", (long long)g * 8);
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    zxc_free_dctx(hd);
    zxc_free_dctx(sd); /* no-op for a static context */
    test_aligned_free(ws);
    free(lz);
    free(rnd);
    free(lit);
    free(comp);
    free(mut);
    free(out);
    free(out2);
    if (ok) printf("PASS\n\n");
    return ok;
}
