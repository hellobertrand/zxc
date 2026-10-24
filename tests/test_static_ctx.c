// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

#include "test_common.h"

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

    const size_t cap = zxc_compress_bound(src_sz);
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

        zxc_cctx* const cctx = zxc_init_static_cctx(cctx_ws, cctx_ws_sz, block_size, lvl);
        if (!cctx) {
            printf("  [FAIL] level %d: zxc_init_static_cctx returned NULL\n", lvl);
            test_aligned_free(cctx_ws);
            goto fail;
        }
        if (zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, 1) != ZXC_OK) {
            printf("  [FAIL] level %d: checksum refused on a static cctx\n", lvl);
            test_aligned_free(cctx_ws);
            goto fail;
        }

        const int64_t csz = zxc_compress_cctx(cctx, src, src_sz, enc, cap);
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

        zxc_dctx_set_param(dctx, ZXC_D_CHECKSUM, 1);
        const int64_t dsz = zxc_decompress_dctx(dctx, enc, (size_t)csz, dec, src_sz);
        const int frame_ok = dsz == (int64_t)src_sz && memcmp(src, dec, src_sz) == 0;
        /* A block decoded right after the frame carries index 0. dec is compared
         * already, so the block may reuse it. */
        int block_ok = 1;
        if (frame_ok && lvl == zxc_min_level()) {
            zxc_cctx* const bc = zxc_create_cctx();
            int64_t bn = bc ? ZXC_OK : ZXC_ERROR_MEMORY;
            if (bn == ZXC_OK) bn = zxc_cctx_set_param(bc, ZXC_C_LEVEL, lvl);
            if (bn == ZXC_OK) bn = zxc_cctx_set_param(bc, ZXC_C_BLOCK_SIZE, (int64_t)block_size);
            if (bn == ZXC_OK) bn = zxc_cctx_set_param(bc, ZXC_C_CHECKSUM, 1);
            if (bn == ZXC_OK) bn = zxc_compress_block(bc, src, block_size, enc, cap);
            zxc_free_cctx(bc);
            const int64_t bsz =
                bn > 0 ? zxc_decompress_block(dctx, enc, (size_t)bn, dec, src_sz) : bn;
            block_ok = bsz == (int64_t)block_size && memcmp(src, dec, block_size) == 0;
        }
        zxc_free_dctx(dctx); /* no-op for static */
        test_aligned_free(dctx_ws);
        if (!frame_ok || !block_ok) {
            printf("  [FAIL] level %d: roundtrip mismatch (dsz=%lld, block after frame %s)\n", lvl,
                   (long long)dsz, block_ok ? "ok" : "wrong");
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

    if (zxc_init_static_cctx(ws, needed - 1, block_size, ZXC_LEVEL_DEFAULT)) {
        printf("  [FAIL] init should reject undersized workspace\n");
        test_aligned_free(ws);
        return 0;
    }
    /* Same size: should succeed. */
    zxc_cctx* const cctx = zxc_init_static_cctx(ws, needed, block_size, ZXC_LEVEL_DEFAULT);
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

/* Verify the block_size lock on a static cctx: setting a different block_size
 * must return ZXC_ERROR_BAD_BLOCK_SIZE, the pinned one must stay in force. */
int test_static_ctx_block_size_locked(void) {
    printf("=== TEST: Static Context API - block_size lock ===\n");

    const size_t pinned_bs = 64 * 1024;
    const size_t ws_sz = zxc_static_cctx_workspace_size(pinned_bs, ZXC_LEVEL_DENSITY);
    void* const ws = test_aligned_alloc(64, ws_sz);
    if (!ws) {
        printf("  [FAIL] aligned_alloc\n");
        return 0;
    }

    zxc_cctx* const cctx = zxc_init_static_cctx(ws, ws_sz, pinned_bs, ZXC_LEVEL_DENSITY);
    if (!cctx) {
        printf("  [FAIL] init_static_cctx\n");
        test_aligned_free(ws);
        return 0;
    }

    /* Another block_size is refused by the setter; the pinned one is kept. */
    uint8_t src[256] = {0};
    uint8_t dst[1024];
    int64_t bs = 0;
    const int rc = zxc_cctx_set_param(cctx, ZXC_C_BLOCK_SIZE, (int64_t)(pinned_bs * 2));
    if (rc != ZXC_ERROR_BAD_BLOCK_SIZE ||
        zxc_cctx_get_param(cctx, ZXC_C_BLOCK_SIZE, &bs) != ZXC_OK || bs != (int64_t)pinned_bs) {
        printf("  [FAIL] expected ZXC_ERROR_BAD_BLOCK_SIZE, got %d (block size now %lld)\n", rc,
               (long long)bs);
        zxc_free_cctx(cctx);
        test_aligned_free(ws);
        return 0;
    }

    /* Same block_size still works, set explicitly or left alone. */
    const int64_t rc2 = zxc_cctx_set_param(cctx, ZXC_C_BLOCK_SIZE, (int64_t)pinned_bs) == ZXC_OK
                            ? zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst))
                            : ZXC_ERROR_BAD_BLOCK_SIZE;
    if (rc2 <= 0) {
        printf("  [FAIL] same-block_size compress returned %lld\n", (long long)rc2);
        zxc_free_cctx(cctx);
        test_aligned_free(ws);
        return 0;
    }

    /* zxc_cctx_reset() restores the pinned values, not the library defaults:
     * level 3 first, so the two differ, then the reset archive must match a
     * heap context at {6, 64 KiB}. */
    uint8_t ref[1024];
    if (zxc_cctx_set_param(cctx, ZXC_C_LEVEL, 3) != ZXC_OK ||
        zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst)) <= 0) {
        printf("  [FAIL] level-3 call on the pinned context\n");
        zxc_free_cctx(cctx);
        test_aligned_free(ws);
        return 0;
    }
    int64_t lvl = 0;
    const int64_t rc3 = zxc_cctx_reset(cctx) == ZXC_OK
                            ? zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst))
                            : ZXC_ERROR_NULL_INPUT;
    const int64_t rc4 =
        test_compress_cfg(src, sizeof(src), ref, sizeof(ref), ZXC_LEVEL_DENSITY, pinned_bs, 0, 0);
    if (rc3 <= 0 || rc3 != rc4 || memcmp(dst, ref, (size_t)rc3) != 0 ||
        zxc_cctx_get_param(cctx, ZXC_C_LEVEL, &lvl) != ZXC_OK || lvl != ZXC_LEVEL_DENSITY ||
        zxc_cctx_get_param(cctx, ZXC_C_BLOCK_SIZE, &bs) != ZXC_OK || bs != (int64_t)pinned_bs) {
        printf("  [FAIL] reset on a pinned context: %lld, heap %lld, level %lld, block %lld\n",
               (long long)rc3, (long long)rc4, (long long)lvl, (long long)bs);
        zxc_free_cctx(cctx);
        test_aligned_free(ws);
        return 0;
    }

    zxc_free_cctx(cctx);
    test_aligned_free(ws);
    printf("  [PASS] mismatched block_size rejected; matching one accepted; reset pins\n");
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
        const int64_t bn = test_compress_cfg(NULL, 0, arc, sizeof(arc), 3, pinned_bs * 2, 0, 0);
        if (bn <= 0) {
            printf("  [FAIL] setup: empty archive at %zu -> %lld\n", pinned_bs * 2, (long long)bn);
            break;
        }
        const int64_t bp = zxc_decompress_dctx(dctx, arc, (size_t)bn, NULL, 0);
        const int64_t bd = zxc_decompress_dctx(dctx, arc, (size_t)bn, out, sizeof(out));
        if (bp != ZXC_ERROR_BAD_BLOCK_SIZE || bd != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] foreign block size: probe %lld, decode %lld\n", (long long)bp,
                   (long long)bd);
            break;
        }

        /* A dictionary-bound archive, which a static dctx has no room for. */
        zxc_cctx* const wc = zxc_create_cctx();
        int64_t dn = wc ? ZXC_OK : ZXC_ERROR_MEMORY;
        if (dn == ZXC_OK) dn = zxc_cctx_set_param(wc, ZXC_C_LEVEL, 3);
        if (dn == ZXC_OK) dn = zxc_cctx_set_param(wc, ZXC_C_BLOCK_SIZE, (int64_t)pinned_bs);
        if (dn == ZXC_OK) dn = zxc_cctx_set_dict(wc, dict, sizeof(dict), NULL);
        if (dn == ZXC_OK) dn = zxc_compress_cctx(wc, NULL, 0, arc, sizeof(arc));
        if (dn <= 0) {
            printf("  [FAIL] setup: dict-bound empty archive -> %lld\n", (long long)dn);
            zxc_free_cctx(wc);
            break;
        }
        const int64_t dp = zxc_decompress_dctx(dctx, arc, (size_t)dn, NULL, 0);
        const int64_t dd = zxc_decompress_dctx(dctx, arc, (size_t)dn, out, sizeof(out));
        if (dp != ZXC_ERROR_DICT_UNSUPPORTED || dd != ZXC_ERROR_DICT_UNSUPPORTED) {
            printf("  [FAIL] dict-bound archive: probe %lld, decode %lld\n", (long long)dp,
                   (long long)dd);
            zxc_free_cctx(wc);
            break;
        }

        /* Same guards on an archive that stores data: the payload rejection
         * must not answer first, or the caller is told "give me a buffer" for
         * an archive this context could never decode. */
        uint8_t body[4096];
        memset(body, 'z', sizeof(body));
        const int64_t pn =
            test_compress_cfg(body, sizeof(body), arc, sizeof(arc), 3, pinned_bs * 2, 0, 0);
        if (pn <= 0) {
            printf("  [FAIL] setup: payload archive at %zu -> %lld\n", pinned_bs * 2,
                   (long long)pn);
            zxc_free_cctx(wc);
            break;
        }
        if (zxc_decompress_dctx(dctx, arc, (size_t)pn, NULL, 0) != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] payload archive, foreign block size: probe %lld\n",
                   (long long)zxc_decompress_dctx(dctx, arc, (size_t)pn, NULL, 0));
            zxc_free_cctx(wc);
            break;
        }
        const int64_t pdn = zxc_compress_cctx(wc, body, sizeof(body), arc, sizeof(arc));
        zxc_free_cctx(wc);
        if (pdn <= 0) {
            printf("  [FAIL] setup: dict payload archive -> %lld\n", (long long)pdn);
            break;
        }
        if (zxc_decompress_dctx(dctx, arc, (size_t)pdn, NULL, 0) != ZXC_ERROR_DICT_UNSUPPORTED) {
            printf("  [FAIL] payload archive, dictionary: probe %lld\n",
                   (long long)zxc_decompress_dctx(dctx, arc, (size_t)pdn, NULL, 0));
            break;
        }

        /* No dictionary can be attached to a static dctx, oversized or not:
         * the setter refuses it, and the context stays as it was. */
        const int hs = zxc_dctx_set_dict(dctx, dict, (size_t)ZXC_DICT_SIZE_MAX + 1, NULL);
        const int ss = zxc_dctx_set_dict(dctx, dict, sizeof(dict), NULL);
        /* arc holds the dictionary archive by now: rebuild the plain one at the
         * foreign block size, whose verdict shows the context unchanged. */
        const int64_t pn2 =
            test_compress_cfg(body, sizeof(body), arc, sizeof(arc), 3, pinned_bs * 2, 0, 0);
        const int64_t hp = pn2 > 0 ? zxc_decompress_dctx(dctx, arc, (size_t)pn2, NULL, 0) : pn2;
        if (hs != ZXC_ERROR_DICT_UNSUPPORTED || ss != ZXC_ERROR_DICT_UNSUPPORTED ||
            hp != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf(
                "  [FAIL] dictionary on a static dctx: oversized %d, small %d, probe after %lld\n",
                hs, ss, (long long)hp);
            break;
        }

        /* An archive the context can decode still probes as empty. */
        const int64_t gn = test_compress_cfg(NULL, 0, arc, sizeof(arc), 3, pinned_bs, 0, 0);
        if (gn <= 0 || zxc_decompress_dctx(dctx, arc, (size_t)gn, NULL, 0) != 0) {
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
 * and its workspace cannot grow. A raise into the optimal-parser tier must be
 * rejected with ZXC_ERROR_BAD_LEVEL, by the setter now - before the fix it
 * silently heap-allocated a replacement workspace (violating the no-allocation
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

    zxc_cctx* const cctx = zxc_init_static_cctx(ws, ws_sz, pinned_bs, ZXC_LEVEL_DEFAULT);
    if (!cctx) {
        printf("  [FAIL] init_static_cctx\n");
        test_aligned_free(ws);
        return 0;
    }

    uint8_t src[256] = {0};
    uint8_t dst[1024];

    /* Raises to 7 and 6 fail cleanly; the pinned level stays in force. */
    int64_t lvl = 0;
    const int rc = zxc_cctx_set_param(cctx, ZXC_C_LEVEL, ZXC_LEVEL_ULTRA);
    const int rc2 = zxc_cctx_set_param(cctx, ZXC_C_LEVEL, ZXC_LEVEL_DENSITY);
    if (rc != ZXC_ERROR_BAD_LEVEL || rc2 != ZXC_ERROR_BAD_LEVEL ||
        zxc_cctx_get_param(cctx, ZXC_C_LEVEL, &lvl) != ZXC_OK || lvl != ZXC_LEVEL_DEFAULT) {
        printf("  [FAIL] raise: expected ZXC_ERROR_BAD_LEVEL twice, got %d %d (level %lld)\n", rc,
               rc2, (long long)lvl);
        goto fail;
    }

    /* The context is still usable at its pinned level afterwards, through the
     * frame API and the block API. */
    const int64_t rc3 = zxc_compress_cctx(cctx, src, sizeof(src), dst, sizeof(dst));
    const int64_t rc4 = zxc_compress_block(cctx, src, sizeof(src), dst, sizeof(dst));
    if (rc3 <= 0 || rc4 <= 0) {
        printf("  [FAIL] pinned-level compress after rejection returned %lld / %lld\n",
               (long long)rc3, (long long)rc4);
        goto fail;
    }

    /* A static workspace carved AT the dense tier accepts its own level, a
     * lower one, and the way back up. */
    {
        const size_t ws7_sz = zxc_static_cctx_workspace_size(pinned_bs, ZXC_LEVEL_ULTRA);
        void* const ws7 = test_aligned_alloc(64, ws7_sz);
        if (!ws7) {
            printf("  [FAIL] aligned_alloc (level 7 ws)\n");
            goto fail;
        }
        zxc_cctx* const cctx7 = zxc_init_static_cctx(ws7, ws7_sz, pinned_bs, ZXC_LEVEL_ULTRA);
        if (!cctx7 || zxc_compress_cctx(cctx7, src, sizeof(src), dst, sizeof(dst)) <= 0 ||
            zxc_cctx_set_param(cctx7, ZXC_C_LEVEL, 3) != ZXC_OK ||
            zxc_compress_cctx(cctx7, src, sizeof(src), dst, sizeof(dst)) <= 0 ||
            zxc_cctx_set_param(cctx7, ZXC_C_LEVEL, ZXC_LEVEL_ULTRA) != ZXC_OK ||
            zxc_compress_cctx(cctx7, src, sizeof(src), dst, sizeof(dst)) <= 0) {
            printf("  [FAIL] level-7 static cctx should compress at levels 7, 3, 7\n");
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

    if (zxc_init_static_cctx(NULL, 65536, 4096, 3)) {
        printf("  [FAIL] init_static_cctx(NULL workspace) should fail\n");
        return 0;
    }
    /* Pinned values are not clamped: out-of-range levels and a block size
     * that is not a power of two are refused. */
    uint8_t ws[16384];
    if (zxc_init_static_cctx(ws, sizeof(ws), 4096, 0) ||
        zxc_init_static_cctx(ws, sizeof(ws), 4096, ZXC_LEVEL_ULTRA + 1) ||
        zxc_init_static_cctx(ws, sizeof(ws), 10000, 3)) {
        printf("  [FAIL] init_static_cctx(bad level or block size) should fail\n");
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
                            size_t cap) {
    if (b->n <= 0) return b->n;
    return strict ? zxc_decompress_block_safe(d, b->comp, (size_t)b->n, out, cap)
                  : zxc_decompress_block(d, b->comp, (size_t)b->n, out, cap);
}

/* Static dctx block API: the carved block is the effective capacity. Fitting
 * blocks decode whatever the buffer. A payload larger than the carved block is
 * BAD_BLOCK_SIZE, checked before decoding; one that fits but decodes past the
 * block keeps the decoder's own codes. Corruption keeps the dynamic codes. */
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
    const size_t cap = zxc_compress_block_bound(SEQ);
    uint8_t* lz = (uint8_t*)malloc(XL);   /* compressible: SPL */
    uint8_t* rnd = (uint8_t*)malloc(XL);  /* incompressible: RAW */
    uint8_t* lit = (uint8_t*)malloc(LIT); /* 16-symbol text, then a repeat: many literals */
    uint8_t* seq = (uint8_t*)malloc(SEQ); /* 23 fixed bytes + 1 random: thousands of matches */
    uint8_t* mut = (uint8_t*)malloc(cap);
    uint8_t* out = (uint8_t*)malloc(SEQ + PAD);
    uint8_t* out2 = (uint8_t*)malloc(SEQ + PAD);
    bound_block_t blocks[NB] = {{"fit", 0, 0, 0, 0}};
    const size_t ws_sz = zxc_static_dctx_workspace_size(PIN);
    void* ws = test_aligned_alloc(64, ws_sz);
    zxc_cctx* cctx = zxc_create_cctx();
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
        zxc_test_srand(0x5747U);
        gen_lz_data(lz, XL);
        gen_random_data(rnd, XL);
        for (size_t i = 0; i < LIT - 1000; i++)
            lit[i] = (uint8_t)("0123456789abcdef"[zxc_test_rand() & 15]);
        memcpy(lit + LIT - 1000, lit, 1000);
        for (size_t i = 0; i < SEQ; i++)
            seq[i] = (i % 24) == 23 ? (uint8_t)zxc_test_rand() : lz[i % 24];

        /* Prepare the blocks once, each in its own buffer; level 0 = forged
         * later, not compressed. */
        const struct {
            const char* name;
            const uint8_t* src;
            size_t size;
            int level;
            int checksum;
        } plan[NB] = {{"fit", lz, PIN, 3, 0},
                      {"fit+checksum", lz, PIN, 3, 1},
                      {"SPL 5000", lz, MID, 3, 0},
                      {"RAW 5000", rnd, MID, 3, 0},
                      {"SPL 8000", lz, XL, 3, 0},
                      {"RAW 8000", rnd, XL, 3, 0},
                      {"literals L6", lit, LIT, 6, 0},
                      {"literals L1", lit, LIT, 1, 0},
                      {"sequences L7", seq, SEQ, 7, 0},
                      {"header literals", 0, 0, 0, 0},
                      {"header literals+checksum", 0, 0, 0, 0}};
        int prepared = 1;
        for (int i = 0; i < NB; i++) {
            blocks[i].name = plan[i].name;
            blocks[i].src = plan[i].src;
            blocks[i].size = plan[i].size;
            if (!plan[i].level) continue;
            blocks[i].n =
                zxc_cctx_set_param(cctx, ZXC_C_LEVEL, plan[i].level) == ZXC_OK &&
                        zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, plan[i].checksum) == ZXC_OK
                    ? zxc_compress_block(cctx, plan[i].src, plan[i].size, blocks[i].comp, cap)
                    : ZXC_ERROR_BAD_PARAM;
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
            blocks[B_RAW_XL].comp[0] != ZXC_BLOCK_RAW || blocks[B_LIT6].comp[0] != ZXC_BLOCK_SPL ||
            blocks[B_LIT1].comp[0] != ZXC_BLOCK_PAK) {
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
            {B_RAW_MID, 1, PIN, ZXC_ERROR_BAD_BLOCK_SIZE},
            /* beyond the margin: BAD_BLOCK_SIZE when the payload is too big,
               the decoder's own codes when only its output is */
            {B_LZ_XL, 0, XL + 200, ZXC_ERROR_OVERFLOW},
            {B_LZ_XL, 0, PIN, ZXC_ERROR_OVERFLOW},
            {B_LZ_XL, 1, XL, ZXC_ERROR_OVERFLOW},
            {B_RAW_XL, 0, XL + 200, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_RAW_XL, 1, XL, ZXC_ERROR_BAD_BLOCK_SIZE},
            /* more literals or sequences than the carved block's scratch */
            {B_LIT6, 0, LIT + PAD, ZXC_ERROR_DST_TOO_SMALL},
            {B_LIT6, 1, LIT, ZXC_ERROR_DST_TOO_SMALL},
            {B_LIT1, 0, LIT + PAD, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_LIT1, 1, LIT, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_SEQ7, 0, SEQ + PAD, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_SEQ7, 1, SEQ, ZXC_ERROR_BAD_BLOCK_SIZE},
            {B_HDR_LIT, 0, LIT + PAD, ZXC_ERROR_DST_TOO_SMALL},
            {B_HDR_LIT, 1, LIT, ZXC_ERROR_DST_TOO_SMALL},
        };
        int failed = 0;
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            const bound_case_t* k = &cases[c];
            const bound_block_t* b = &blocks[k->block];
            const int64_t got = bound_decode(sd, b, k->strict, out, k->cap);
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

        /* A forged count with a stale checksum is refused by all three shapes.
         * The checksum covers the decompressed bytes, so it no longer wins the
         * race: the decoder's own bounds check fires first. */
        if (zxc_dctx_set_param(sd, ZXC_D_CHECKSUM, 1) != ZXC_OK ||
            zxc_dctx_set_param(hd, ZXC_D_CHECKSUM, 1) != ZXC_OK) {
            printf("  [FAIL] checksum verification refused on a dctx\n");
            break;
        }
        const int64_t c1 = bound_decode(sd, &blocks[B_HDR_LIT_CK], 0, out, PIN + PAD);
        const int64_t c2 = bound_decode(sd, &blocks[B_HDR_LIT_CK], 1, out, PIN);
        const int64_t c3 = bound_decode(hd, &blocks[B_HDR_LIT_CK], 0, out, PIN + PAD);
        if (c1 != ZXC_ERROR_CORRUPT_DATA || c2 != ZXC_ERROR_CORRUPT_DATA ||
            c3 != ZXC_ERROR_CORRUPT_DATA) {
            printf("  [FAIL] forged count: static %lld %lld, dynamic %lld\n", (long long)c1,
                   (long long)c2, (long long)c3);
            break;
        }
        printf("  [PASS] forged count refused by the decoder, static as dynamic\n");

        /* An intact block whose checksum is wrong decodes, then fails the check. */
        bound_block_t stale = blocks[B_FIT_CK];
        memcpy(mut, stale.comp, (size_t)stale.n);
        mut[stale.n - 1] ^= 0xFFU;
        stale.comp = mut;
        const int64_t s1 = bound_decode(sd, &stale, 0, out, PIN + PAD);
        const int64_t s2 = bound_decode(sd, &stale, 1, out, PIN);
        const int64_t s3 = bound_decode(hd, &stale, 0, out, PIN + PAD);
        if (s1 != ZXC_ERROR_BAD_CHECKSUM || s2 != ZXC_ERROR_BAD_CHECKSUM ||
            s3 != ZXC_ERROR_BAD_CHECKSUM) {
            printf("  [FAIL] stale checksum: static %lld %lld, dynamic %lld\n", (long long)s1,
                   (long long)s2, (long long)s3);
            break;
        }
        printf("  [PASS] stale checksum -> BAD_CHECKSUM, static as dynamic\n");

        /* Dictionaries: a static dctx refuses them at the setter, oversized or
         * not, and the fitting block still decodes afterwards. The dynamic dctx
         * takes the small one and refuses the oversized one. */
        const int d1 = zxc_dctx_set_dict(sd, lz, 16, NULL);
        const int d2 = zxc_dctx_set_dict(sd, lz, (size_t)ZXC_DICT_SIZE_MAX + 1, NULL);
        const int d3 = zxc_dctx_set_dict(hd, lz, (size_t)ZXC_DICT_SIZE_MAX + 1, NULL);
        const int d4 = zxc_dctx_set_dict(hd, lz, 16, NULL);
        zxc_dctx_set_param(sd, ZXC_D_CHECKSUM, 0); /* B_FIT carries no checksum */
        const int64_t after = bound_decode(sd, &blocks[B_FIT], 1, out, PIN);
        if (d1 != ZXC_ERROR_DICT_UNSUPPORTED || d2 != ZXC_ERROR_DICT_UNSUPPORTED ||
            d3 != ZXC_ERROR_DICT_TOO_LARGE || d4 != ZXC_OK || after != PIN ||
            zxc_dctx_set_dict(hd, NULL, 0, NULL) != ZXC_OK) {
            printf(
                "  [FAIL] dictionary: static %d, oversized %d; dynamic oversized %d, %d;"
                " decode after %lld\n",
                d1, d2, d3, d4, (long long)after);
            break;
        }
        printf("  [PASS] dictionary -> DICT_UNSUPPORTED on the static dctx, at the setter\n");

        /* Corruption keeps its codes: every bit flip of a fitting block,
         * checksum off then on, gets the same verdict on both dctx, bar a block
         * grown past the carved block (static names it, dynamic says too-small
         * destination). */
        int mismatches = 0;
        long flips = 0;
        for (int pass = 0; pass < 2 && !mismatches; pass++) {
            const bound_block_t* b = &blocks[pass ? B_FIT_CK : B_FIT];
            if (zxc_dctx_set_param(sd, ZXC_D_CHECKSUM, pass) != ZXC_OK ||
                zxc_dctx_set_param(hd, ZXC_D_CHECKSUM, pass) != ZXC_OK) {
                mismatches++;
                break;
            }
            for (size_t bit = 0; bit < (size_t)b->n * 8; bit++, flips++) {
                memcpy(mut, b->comp, (size_t)b->n);
                mut[bit >> 3] ^= (uint8_t)(1U << (bit & 7));
                const int64_t rs = zxc_decompress_block(sd, mut, (size_t)b->n, out, PIN);
                const int64_t rd = zxc_decompress_block(hd, mut, (size_t)b->n, out2, PIN);
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

/* Round trip, then the documented refusals. */
static int inplace_dctx_case(zxc_dctx* d, const char* label, const uint8_t* orig, size_t n,
                             int level, int checksum, size_t block_size) {
    const size_t cbound = zxc_compress_bound(n);
    uint8_t* const comp = (uint8_t*)malloc(cbound);
    if (!comp) return 0;
    const int64_t c = test_compress_cfg(orig, n, comp, cbound, level, block_size, checksum, 0);
    const size_t need = c > 0 ? zxc_decompress_inplace_bound(comp, (size_t)c) : 0;
    uint8_t* const buf = need ? (uint8_t*)malloc(need) : NULL;
    int ok = buf != NULL && zxc_dctx_set_param(d, ZXC_D_CHECKSUM, checksum) == ZXC_OK;
    const size_t csz = (size_t)c;

    if (ok) {
        memset(buf, 0xCC, need);
        memcpy(buf + need - csz, comp, csz); /* flush-right */
        const int64_t r = zxc_decompress_inplace_dctx(d, buf, need, csz);
        if (r != (int64_t)n || memcmp(buf, orig, n) != 0) {
            printf("  [FAIL] %s: inplace_dctx ret=%lld want=%zu\n", label, (long long)r, n);
            ok = 0;
        }
    }
    /* One byte short. */
    if (ok && need - 1 >= csz) {
        memcpy(buf + need - 1 - csz, comp, csz);
        const int64_t r = zxc_decompress_inplace_dctx(d, buf, need - 1, csz);
        if (r != ZXC_ERROR_DST_TOO_SMALL) {
            printf("  [FAIL] %s: undersized -> %lld\n", label, (long long)r);
            ok = 0;
        }
    }
    /* Padding before the footer, which a forger re-signs to cover it. */
    if (ok) {
        const size_t pad = 4096;
        const size_t body = csz - test_footer_len(comp, csz);
        uint8_t* const bad =
            (uint8_t*)malloc(body + pad + ZXC_FILE_DIGEST_SIZE + ZXC_FILE_FOOTER_MAX_SIZE);
        size_t bneed = 0;
        size_t blen = 0;
        if (bad) {
            memcpy(bad, comp, body);
            memset(bad + body, 0xA5, pad);
            const uint64_t digest = checksum ? zxc_le64(comp + body) : 0;
            const int fw = zxc_write_file_footer(
                bad + body + pad, ZXC_FILE_DIGEST_SIZE + ZXC_FILE_FOOTER_MAX_SIZE, body + pad,
                zxc_get_decompressed_size(comp, csz), digest, checksum);
            blen = fw > 0 ? body + pad + (size_t)fw : 0;
            bneed = blen ? zxc_decompress_inplace_bound(bad, blen) : 0;
        }
        uint8_t* const b2 = bneed ? (uint8_t*)malloc(bneed) : NULL;
        if (!b2) {
            printf("  [FAIL] %s: padded archive gave no bound\n", label);
            ok = 0;
        }
        if (b2) {
            memcpy(b2 + bneed - blen, bad, blen);
            const int64_t r = zxc_decompress_inplace_dctx(d, b2, bneed, blen);
            if (r != ZXC_ERROR_CORRUPT_DATA) {
                printf("  [FAIL] %s: padded -> %lld\n", label, (long long)r);
                ok = 0;
            }
        }
        free(b2);
        free(bad);
    }
    if (ok) printf("  [PASS] %s (n=%zu, comp=%zu, margin=%zu)\n", label, n, csz, need - n);
    free(buf);
    free(comp);
    return ok;
}

/* Compresses @p n bytes at @p block_size and decodes them in place through @p d. */
static int64_t inplace_refusal(zxc_dctx* d, const uint8_t* src, size_t n, size_t block_size) {
    const size_t cb = zxc_compress_bound(n);
    uint8_t* const comp = (uint8_t*)malloc(cb);
    const int64_t c = comp ? test_compress_cfg(src, n, comp, cb, 3, block_size, 0, 0) : -1;
    const size_t need = c > 0 ? zxc_decompress_inplace_bound(comp, (size_t)c) : 0;
    uint8_t* const buf = need ? (uint8_t*)malloc(need) : NULL;
    int64_t r = INT64_MIN;
    if (buf) {
        memcpy(buf + need - (size_t)c, comp, (size_t)c);
        r = zxc_decompress_inplace_dctx(d, buf, need, (size_t)c);
    }
    free(buf);
    free(comp);
    return r;
}

/* In-place decode through a caller-owned context, static then heap. */
int test_static_dctx_inplace(void) {
    printf("=== TEST: Static Context API - in-place decode ===\n");

    const size_t N = 1U << 20;
    uint8_t* const text = (uint8_t*)malloc(N);
    uint8_t* const noise = (uint8_t*)malloc(N);
    if (!text || !noise) {
        free(text);
        free(noise);
        return 0;
    }
    fill_payload(text, N);
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < N; i++) {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        noise[i] = (uint8_t)(s >> 56);
    }

    int ok = 1;
    const size_t sizes[] = {ZXC_BLOCK_SIZE_MIN, 64 * 1024};
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        const size_t bs = sizes[k];
        const size_t ws_sz = zxc_static_dctx_workspace_size(bs);
        void* const ws = test_aligned_alloc(64, ws_sz);
        zxc_dctx* const d = ws ? zxc_init_static_dctx(ws, ws_sz, bs) : NULL;
        if (!d) {
            printf("  [FAIL] init_static_dctx(%zu)\n", bs);
            test_aligned_free(ws);
            ok = 0;
            continue;
        }
        char label[64];
        /* One context, several archives. */
        snprintf(label, sizeof(label), "static %zuK, pattern L3", bs >> 10);
        ok &= inplace_dctx_case(d, label, text, N, 3, 1, bs);
        snprintf(label, sizeof(label), "static %zuK, noise L1 (overlap)", bs >> 10);
        ok &= inplace_dctx_case(d, label, noise, N, 1, 0, bs);
        snprintf(label, sizeof(label), "static %zuK, pattern L7", bs >> 10);
        ok &= inplace_dctx_case(d, label, text, N, 7, 1, bs);

        /* The static context's own refusals: a foreign block size at decode
         * time, a dictionary at the setter. */
        const int64_t r_bs = inplace_refusal(d, text, 64 * 1024, bs * 2);
        const int r_dict = zxc_dctx_set_dict(d, text, 1024, NULL);
        if (r_bs != ZXC_ERROR_BAD_BLOCK_SIZE || r_dict != ZXC_ERROR_DICT_UNSUPPORTED) {
            printf("  [FAIL] static %zuK: foreign block size -> %lld, dict -> %d\n", bs >> 10,
                   (long long)r_bs, r_dict);
            ok = 0;
        }
        zxc_free_dctx(d); /* no-op */
        test_aligned_free(ws);
    }

    zxc_dctx* const h = zxc_create_dctx();
    ok &= h && inplace_dctx_case(h, "heap dctx, noise L3 (overlap)", noise, N, 3, 1, 0);
    zxc_free_dctx(h);

    /* Caller errors. */
    uint8_t tiny[64] = {0};
    if (zxc_decompress_inplace_dctx(NULL, tiny, sizeof(tiny), 32) != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] NULL dctx not rejected\n");
        ok = 0;
    }

    free(text);
    free(noise);
    if (ok) printf("PASS\n\n");
    return ok;
}
