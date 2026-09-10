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
    if (zxc_init_static_cctx(ws, needed - 1, &opts)) {
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

/* A size probe on a static dctx must answer under that context's own rules.
 * Short-circuiting the no-destination case ahead of the block-size lock and the
 * no-dictionary rule handed callers a green light for archives the context
 * cannot decode. */
int test_static_dctx_probe_honours_guards(void) {
    printf("=== TEST: Static Context API - size probe honours the dctx guards ===\n");

    const size_t pinned_bs = 64 * 1024;
    const size_t ws_sz = zxc_static_dctx_workspace_size(pinned_bs);
    void* const ws = test_aligned_alloc(64, ws_sz);
    if (!ws) {
        printf("  [FAIL] aligned_alloc\n");
        return 0;
    }
    zxc_dctx* const dctx = zxc_init_static_dctx(ws, ws_sz, pinned_bs);
    if (!dctx) {
        printf("  [FAIL] init_static_dctx\n");
        test_aligned_free(ws);
        return 0;
    }

    int ok = 0;
    uint8_t arc[8192], out[256];
    static uint8_t dict[8192];
    memset(dict, 'x', sizeof(dict));
    do {
        /* An archive whose header declares another block size. */
        zxc_compress_opts_t wrong_bs = {.level = 3, .block_size = pinned_bs * 2};
        const int64_t bn = zxc_compress(NULL, 0, arc, sizeof(arc), &wrong_bs);
        if (bn <= 0) {
            printf("  [FAIL] setup: empty archive at %zu -> %lld\n", pinned_bs * 2, (long long)bn);
            break;
        }
        const int64_t bp = zxc_decompress_dctx(dctx, arc, (size_t)bn, NULL, 0, NULL);
        const int64_t bd = zxc_decompress_dctx(dctx, arc, (size_t)bn, out, sizeof(out), NULL);
        if (bp != ZXC_ERROR_BAD_BLOCK_SIZE || bd != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] foreign block size: probe %lld, decode %lld\n", (long long)bp,
                   (long long)bd);
            break;
        }

        /* A dictionary-bound archive, which a static dctx has no room for. */
        zxc_compress_opts_t with_dict = {.level = 3, .block_size = pinned_bs};
        with_dict.dict = dict;
        with_dict.dict_size = sizeof(dict);
        const int64_t dn = zxc_compress(NULL, 0, arc, sizeof(arc), &with_dict);
        if (dn <= 0) {
            printf("  [FAIL] setup: dict-bound empty archive -> %lld\n", (long long)dn);
            break;
        }
        const int64_t dp = zxc_decompress_dctx(dctx, arc, (size_t)dn, NULL, 0, NULL);
        const int64_t dd = zxc_decompress_dctx(dctx, arc, (size_t)dn, out, sizeof(out), NULL);
        if (dp != ZXC_ERROR_DICT_UNSUPPORTED || dd != ZXC_ERROR_DICT_UNSUPPORTED) {
            printf("  [FAIL] dict-bound archive: probe %lld, decode %lld\n", (long long)dp,
                   (long long)dd);
            break;
        }

        /* Same guards on an archive that stores data: the payload rejection
         * must not answer first, or the caller is told "give me a buffer" for
         * an archive this context could never decode. */
        uint8_t body[4096];
        memset(body, 'z', sizeof(body));
        const int64_t pn = zxc_compress(body, sizeof(body), arc, sizeof(arc), &wrong_bs);
        if (pn <= 0) {
            printf("  [FAIL] setup: payload archive at %zu -> %lld\n", pinned_bs * 2,
                   (long long)pn);
            break;
        }
        if (zxc_decompress_dctx(dctx, arc, (size_t)pn, NULL, 0, NULL) != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] payload archive, foreign block size: probe %lld\n",
                   (long long)zxc_decompress_dctx(dctx, arc, (size_t)pn, NULL, 0, NULL));
            break;
        }
        const int64_t pdn = zxc_compress(body, sizeof(body), arc, sizeof(arc), &with_dict);
        if (pdn <= 0) {
            printf("  [FAIL] setup: dict payload archive -> %lld\n", (long long)pdn);
            break;
        }
        if (zxc_decompress_dctx(dctx, arc, (size_t)pdn, NULL, 0, NULL) !=
            ZXC_ERROR_DICT_UNSUPPORTED) {
            printf("  [FAIL] payload archive, dictionary: probe %lld\n",
                   (long long)zxc_decompress_dctx(dctx, arc, (size_t)pdn, NULL, 0, NULL));
            break;
        }

        /* A dictionary size the library cannot honour is a caller error: it
         * outranks this context's own no-dictionary rule, on both paths. */
        zxc_decompress_opts_t huge = {0};
        huge.dict = dict;
        huge.dict_size = (size_t)ZXC_DICT_SIZE_MAX + 1;
        const int64_t hp = zxc_decompress_dctx(dctx, arc, (size_t)pn, NULL, 0, &huge);
        const int64_t hd = zxc_decompress_dctx(dctx, arc, (size_t)pn, out, sizeof(out), &huge);
        if (hp != ZXC_ERROR_DICT_TOO_LARGE || hd != ZXC_ERROR_DICT_TOO_LARGE) {
            printf("  [FAIL] oversized dictionary: probe %lld, decode %lld\n", (long long)hp,
                   (long long)hd);
            break;
        }

        /* An archive the context can decode still probes as empty. */
        zxc_compress_opts_t good = {.level = 3, .block_size = pinned_bs};
        const int64_t gn = zxc_compress(NULL, 0, arc, sizeof(arc), &good);
        if (gn <= 0 || zxc_decompress_dctx(dctx, arc, (size_t)gn, NULL, 0, NULL) != 0) {
            printf("  [FAIL] matching empty archive did not probe as empty (%lld)\n",
                   (long long)gn);
            break;
        }
        ok = 1;
    } while (0);

    zxc_free_dctx(dctx);
    test_aligned_free(ws);
    if (ok) printf("  [PASS] the probe is refused exactly where the decode is\n");
    return ok;
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
    if (zxc_init_static_cctx(NULL, 65536, &opts)) {
        printf("  [FAIL] init_static_cctx(NULL workspace) should fail\n");
        return 0;
    }
    uint8_t ws[16384];
    if (zxc_init_static_cctx(ws, sizeof(ws), NULL)) {
        printf("  [FAIL] init_static_cctx(NULL opts) should fail\n");
        return 0;
    }
    if (zxc_init_static_dctx(NULL, 65536, 4096)) {
        printf("  [FAIL] init_static_dctx(NULL workspace) should fail\n");
        return 0;
    }
    /* zxc_free_*ctx must accept a NULL pointer (idempotency). */
    zxc_free_cctx(NULL);
    zxc_free_dctx(NULL);
    printf("  [PASS] NULL inputs rejected; NULL free is idempotent\n");
    return 1;
}

/* A prepared block: compressed image and source. */
typedef struct {
    const char* name;
    uint8_t* comp;
    int64_t n;
    const uint8_t* src;
    size_t size;
} bound_block_t;

/* A decode on the static dctx and its verdict (> 0: size, bytes checked;
 * < 0: error code). */
typedef struct {
    int block;
    int strict;
    size_t cap;
    int64_t want;
} bound_case_t;

static int64_t bound_decode(zxc_dctx* d, const bound_block_t* b, int strict, uint8_t* out,
                            size_t cap, const zxc_decompress_opts_t* o) {
    if (b->n <= 0) return b->n;
    return strict ? zxc_decompress_block_safe(d, b->comp, (size_t)b->n, out, cap, o)
                  : zxc_decompress_block(d, b->comp, (size_t)b->n, out, cap, o);
}

/* Static dctx block API: the carved block is the effective capacity. Fitting
 * blocks decode whatever the buffer; larger ones are BAD_BLOCK_SIZE within
 * the margin, the decoder's too-small codes beyond; corruption keeps the
 * dynamic codes. */
int test_static_dctx_block_bounds(void) {
    printf("=== TEST: Static dctx - blocks bounded by the carved block ===\n");
    enum {
        PIN = 4096,
        MID = 5000,
        XL = 8000,
        LIT = 5500,
        SEQ = 65536,
        PAD = ZXC_DECOMPRESS_TAIL_PAD
    };
    enum {
        B_FIT,
        B_FIT_CK,
        B_LZ_MID,
        B_RAW_MID,
        B_LZ_XL,
        B_RAW_XL,
        B_LIT6,
        B_LIT1,
        B_SEQ7,
        B_HDR_LIT,
        B_HDR_LIT_CK,
        NB
    };
    const size_t cap = (size_t)zxc_compress_block_bound(SEQ);
    uint8_t* lz = (uint8_t*)malloc(XL);   /* compressible: GLO */
    uint8_t* rnd = (uint8_t*)malloc(XL);  /* incompressible: RAW */
    uint8_t* lit = (uint8_t*)malloc(LIT); /* 16-symbol text, then a repeat: many literals */
    uint8_t* seq = (uint8_t*)malloc(SEQ); /* 23 fixed bytes + 1 random: thousands of matches */
    uint8_t* mut = (uint8_t*)malloc(cap);
    uint8_t* out = (uint8_t*)malloc(SEQ + PAD);
    uint8_t* out2 = (uint8_t*)malloc(SEQ + PAD);
    bound_block_t blocks[NB] = {{"fit", 0, 0, 0, 0}};
    const size_t ws_sz = zxc_static_dctx_workspace_size(PIN);
    void* ws = test_aligned_alloc(64, ws_sz);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_dctx* hd = zxc_create_dctx();
    zxc_dctx* sd = ws ? zxc_init_static_dctx(ws, ws_sz, PIN) : NULL;
    int ok = 0, blocks_ok = 1;
    for (int i = 0; i < NB; i++) blocks_ok &= (blocks[i].comp = (uint8_t*)malloc(cap)) != NULL;
    do {
        if (!lz || !rnd || !lit || !seq || !mut || !out || !out2 || !cctx || !hd || !sd ||
            !blocks_ok) {
            printf("  [FAIL] setup\n");
            break;
        }
        zxc_test_srand(0x5747u);
        gen_lz_data(lz, XL);
        gen_random_data(rnd, XL);
        for (size_t i = 0; i < LIT - 1000; i++)
            lit[i] = (uint8_t)("0123456789abcdef"[zxc_test_rand() & 15]);
        memcpy(lit + LIT - 1000, lit, 1000);
        for (size_t i = 0; i < SEQ; i++)
            seq[i] = (i % 24) == 23 ? (uint8_t)zxc_test_rand() : lz[i % 24];

        /* Prepare the blocks once, each in its own buffer. */
        const zxc_compress_opts_t l3 = {.level = 3}, l3ck = {.level = 3, .checksum_enabled = 1};
        const zxc_compress_opts_t l1 = {.level = 1}, l6 = {.level = 6}, l7 = {.level = 7};
        const struct {
            const char* name;
            const uint8_t* src;
            size_t size;
            const zxc_compress_opts_t* opts;
        } plan[NB] = {{"fit", lz, PIN, &l3},
                      {"fit+checksum", lz, PIN, &l3ck},
                      {"GLO 5000", lz, MID, &l3},
                      {"RAW 5000", rnd, MID, &l3},
                      {"GLO 8000", lz, XL, &l3},
                      {"RAW 8000", rnd, XL, &l3},
                      {"literals L6", lit, LIT, &l6},
                      {"literals L1", lit, LIT, &l1},
                      {"sequences L7", seq, SEQ, &l7},
                      {"header literals", 0, 0, 0},
                      {"header literals+checksum", 0, 0, 0}};
        int prepared = 1;
        for (int i = 0; i < NB; i++) {
            blocks[i].name = plan[i].name;
            blocks[i].src = plan[i].src;
            blocks[i].size = plan[i].size;
            if (!plan[i].opts) continue;
            blocks[i].n = zxc_compress_block(cctx, plan[i].src, plan[i].size, blocks[i].comp, cap,
                                             plan[i].opts);
            if (blocks[i].n <= 0) {
                printf("  [FAIL] compress %s: %lld\n", plan[i].name, (long long)blocks[i].n);
                prepared = 0;
            }
        }
        if (!prepared) break;
        /* Forged sub-headers: one literal more than the carved block; the
         * second keeps a stale checksum. */
        for (int i = B_HDR_LIT; i <= B_HDR_LIT_CK; i++) {
            const bound_block_t* from = &blocks[i == B_HDR_LIT ? B_LIT6 : B_FIT_CK];
            memcpy(blocks[i].comp, from->comp, (size_t)from->n);
            blocks[i].n = from->n;
            zxc_store_le32(blocks[i].comp + ZXC_BLOCK_HEADER_SIZE + 4, PIN + 1);
        }
        if (blocks[B_RAW_MID].comp[0] != ZXC_BLOCK_RAW ||
            blocks[B_RAW_XL].comp[0] != ZXC_BLOCK_RAW || blocks[B_LIT6].comp[0] != ZXC_BLOCK_GLO ||
            blocks[B_LIT1].comp[0] != ZXC_BLOCK_GHI) {
            printf("  [FAIL] block types: RAW %u %u, literals %u %u\n", blocks[B_RAW_MID].comp[0],
                   blocks[B_RAW_XL].comp[0], blocks[B_LIT6].comp[0], blocks[B_LIT1].comp[0]);
            break;
        }

        /* The table: (block, decoder, buffer) -> verdict. */
        static const bound_case_t cases[] = {
            /* fits: both decoders, exact and larger buffers */
            {B_FIT, 0, PIN + PAD, PIN},
            {B_FIT, 0, 2 * PIN + PAD, PIN},
            {B_FIT, 1, PIN, PIN},
            {B_FIT, 1, 2 * PIN, PIN},
            /* within the margin: named whatever the buffer; strict at the exact
             * carved size: "does not fit", as a dynamic dctx */
            {B_LZ_MID, 0, MID + PAD, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_LZ_MID, 0, PIN, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_LZ_MID, 1, MID, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_LZ_MID, 1, PIN, ZXC_ERROR_OVERFLOW},
            {B_RAW_MID, 0, MID + PAD, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_RAW_MID, 1, MID, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_RAW_MID, 1, PIN, ZXC_ERROR_DST_TOO_SMALL},
            /* beyond the margin: the decoder's own codes, buffer-independent */
            {B_LZ_XL, 0, XL + 200, ZXC_ERROR_OVERFLOW},
            {B_LZ_XL, 0, PIN, ZXC_ERROR_OVERFLOW},
            {B_LZ_XL, 1, XL, ZXC_ERROR_OVERFLOW},
            {B_RAW_XL, 0, XL + 200, ZXC_ERROR_DST_TOO_SMALL},
            {B_RAW_XL, 1, XL, ZXC_ERROR_DST_TOO_SMALL},
            /* more literals or sequences than the carved block's scratch */
            {B_LIT6, 0, LIT + PAD, ZXC_ERROR_DST_TOO_SMALL},
            {B_LIT6, 1, LIT, ZXC_ERROR_DST_TOO_SMALL},
            {B_LIT1, 0, LIT + PAD, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_LIT1, 1, LIT, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_SEQ7, 0, SEQ + PAD, ZXC_ERROR_DST_TOO_SMALL},
            {B_SEQ7, 1, SEQ, ZXC_ERROR_DST_TOO_SMALL},
            {B_HDR_LIT, 0, LIT + PAD, ZXC_ERROR_DST_TOO_SMALL},
            {B_HDR_LIT, 1, LIT, ZXC_ERROR_DST_TOO_SMALL},
        };
        int failed = 0;
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            const bound_case_t* k = &cases[c];
            const bound_block_t* b = &blocks[k->block];
            const int64_t got = bound_decode(sd, b, k->strict, out, k->cap, NULL);
            const int bytes_ok = got <= 0 || memcmp(out, b->src, (size_t)got) == 0;
            if (got != k->want || !bytes_ok) {
                printf("  [FAIL] %s, %s, cap %zu: got %lld, want %lld%s\n", b->name,
                       k->strict ? "strict" : "fast", k->cap, (long long)got, (long long)k->want,
                       bytes_ok ? "" : " (bytes differ)");
                failed++;
            }
        }
        if (failed) break;
        printf("  [PASS] %zu (block, decoder, buffer) verdicts\n",
               sizeof(cases) / sizeof(cases[0]));

        /* The stale checksum wins over the forged count, static as dynamic. */
        const zxc_decompress_opts_t ck = {.checksum_enabled = 1};
        const int64_t c1 = bound_decode(sd, &blocks[B_HDR_LIT_CK], 0, out, PIN + PAD, &ck);
        const int64_t c2 = bound_decode(sd, &blocks[B_HDR_LIT_CK], 1, out, PIN, &ck);
        const int64_t c3 = bound_decode(hd, &blocks[B_HDR_LIT_CK], 0, out, PIN + PAD, &ck);
        if (c1 != ZXC_ERROR_BAD_CHECKSUM || c2 != ZXC_ERROR_BAD_CHECKSUM ||
            c3 != ZXC_ERROR_BAD_CHECKSUM) {
            printf("  [FAIL] stale checksum: static %lld %lld, dynamic %lld\n", (long long)c1,
                   (long long)c2, (long long)c3);
            break;
        }
        printf("  [PASS] stale checksum -> BAD_CHECKSUM before any size verdict\n");

        /* Dictionaries: the same two answers as the dynamic path, in order. */
        const zxc_decompress_opts_t small_dict = {.dict = lz, .dict_size = 16};
        const zxc_decompress_opts_t huge_dict = {.dict = lz, .dict_size = ZXC_DICT_SIZE_MAX + 1};
        const int64_t d1 = bound_decode(sd, &blocks[B_FIT], 0, out, 2 * PIN + PAD, &small_dict);
        const int64_t d2 = bound_decode(sd, &blocks[B_FIT], 1, out, 2 * PIN, &small_dict);
        const int64_t d3 = bound_decode(sd, &blocks[B_FIT], 0, out, PIN + PAD, &huge_dict);
        const int64_t d4 = bound_decode(sd, &blocks[B_FIT], 1, out, PIN, &huge_dict);
        if (d1 != ZXC_ERROR_DICT_UNSUPPORTED || d2 != ZXC_ERROR_DICT_UNSUPPORTED ||
            d3 != ZXC_ERROR_DICT_TOO_LARGE || d4 != ZXC_ERROR_DICT_TOO_LARGE) {
            printf("  [FAIL] dictionary: %lld %lld, oversized %lld %lld\n", (long long)d1,
                   (long long)d2, (long long)d3, (long long)d4);
            break;
        }
        printf(
            "  [PASS] dictionary -> DICT_UNSUPPORTED, oversized -> DICT_TOO_LARGE, both "
            "decoders\n");

        /* Corruption keeps its codes: every bit flip of a fitting block,
         * checksum off then on, gets the same verdict on both dctx, bar a block
         * grown past the carved block (static names it, dynamic says too-small
         * destination). */
        int mismatches = 0;
        long flips = 0;
        for (int pass = 0; pass < 2 && !mismatches; pass++) {
            const bound_block_t* b = &blocks[pass ? B_FIT_CK : B_FIT];
            const zxc_decompress_opts_t* o = pass ? &ck : NULL;
            for (size_t bit = 0; bit < (size_t)b->n * 8; bit++, flips++) {
                memcpy(mut, b->comp, (size_t)b->n);
                mut[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
                const int64_t rs = zxc_decompress_block(sd, mut, (size_t)b->n, out, PIN, o);
                const int64_t rd = zxc_decompress_block(hd, mut, (size_t)b->n, out2, PIN, o);
                const int same = rs == rd && (rs <= 0 || memcmp(out, out2, (size_t)rs) == 0);
                const int grown = rs == ZXC_ERROR_BAD_BLOCK_SIZE && rd == ZXC_ERROR_DST_TOO_SMALL;
                if (!same && !grown && mismatches++ < 3)
                    printf("  [FAIL] %s bit %zu: static %lld, dynamic %lld\n", b->name, bit,
                           (long long)rs, (long long)rd);
            }
        }
        if (mismatches) break;
        printf("  [PASS] %ld bit flips: same verdict as a dynamic dctx\n", flips);
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    zxc_free_dctx(hd);
    zxc_free_dctx(sd); /* no-op for a static context */
    test_aligned_free(ws);
    for (int i = 0; i < NB; i++) free(blocks[i].comp);
    free(lz);
    free(rnd);
    free(lit);
    free(seq);
    free(mut);
    free(out);
    free(out2);
    if (ok) printf("PASS\n\n");
    return ok;
}
