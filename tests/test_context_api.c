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
        uint8_t d[64] = {0};
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

/* Regression: a cctx created below ZXC_LEVEL_DENSITY has no opt_scratch; a
 * per-call level raise into the optimal-parser tier must re-init the inner
 * buffers instead of dereferencing the NULL scratch (crash before the fix).
 * Also covers the new out-of-range level validation (ZXC_ERROR_BAD_LEVEL). */
int test_cctx_level_raise_reinit() {
    printf("=== TEST: Opaque Context API - level raise re-init + level validation ===\n");

    const size_t src_sz = 8192;
    uint8_t* src = malloc(src_sz);
    const size_t comp_cap = (size_t)zxc_compress_bound(src_sz);
    uint8_t* comp = malloc(comp_cap);
    uint8_t* dec = malloc(src_sz);
    zxc_cctx* cctx = NULL;
    zxc_dctx* dctx = NULL;
    if (!src || !comp || !dec) goto fail;
    gen_lz_data(src, src_sz);

    /* 1. Eager init at level 3 (no opt_scratch), then raise to 7 in place. */
    zxc_compress_opts_t create_opts = {.level = 3, .checksum_enabled = 0};
    cctx = zxc_create_cctx(&create_opts);
    dctx = zxc_create_dctx();
    if (!cctx || !dctx) {
        printf("  [FAIL] create returned NULL\n");
        goto fail;
    }
    zxc_compress_opts_t raise = {.level = ZXC_LEVEL_ULTRA, .checksum_enabled = 0};
    const int64_t csz = zxc_compress_cctx(cctx, src, src_sz, comp, comp_cap, &raise);
    if (csz <= 0) {
        printf("  [FAIL] level-7 raise on level-3 cctx returned %lld\n", (long long)csz);
        goto fail;
    }
    const int64_t dsz = zxc_decompress_dctx(dctx, comp, (size_t)csz, dec, src_sz, NULL);
    if (dsz != (int64_t)src_sz || memcmp(src, dec, src_sz) != 0) {
        printf("  [FAIL] roundtrip after level raise (dsz=%lld)\n", (long long)dsz);
        goto fail;
    }
    printf("  [PASS] level 3 -> 7 raise re-inits and roundtrips\n");

    /* 2. Same raise through the block API on a fresh low-level cctx. */
    zxc_free_cctx(cctx);
    cctx = zxc_create_cctx(&create_opts);
    if (!cctx) {
        printf("  [FAIL] re-create returned NULL\n");
        goto fail;
    }
    zxc_compress_opts_t raise6 = {.level = ZXC_LEVEL_DENSITY, .checksum_enabled = 0};
    const int64_t bsz = zxc_compress_block(cctx, src, src_sz, comp, comp_cap, &raise6);
    if (bsz <= 0) {
        printf("  [FAIL] level-6 block raise returned %lld\n", (long long)bsz);
        goto fail;
    }
    printf("  [PASS] level 3 -> 6 raise through the block API\n");

    /* 3. Out-of-range levels are silently clamped to ULTRA: level 99 must
     * produce the exact archive level 7 produces, on every entry point. */
    zxc_compress_opts_t lvl7 = {.level = ZXC_LEVEL_ULTRA, .checksum_enabled = 0};
    zxc_compress_opts_t lvl99 = {.level = 99, .checksum_enabled = 0};
    uint8_t* comp7 = malloc(comp_cap);
    if (!comp7) goto fail;
    const int64_t c7 = zxc_compress(src, src_sz, comp7, comp_cap, &lvl7);
    const int64_t c99 = zxc_compress(src, src_sz, comp, comp_cap, &lvl99);
    if (c7 <= 0 || c99 != c7 || memcmp(comp, comp7, (size_t)c7) != 0) {
        printf("  [FAIL] zxc_compress(level=99) must clamp to ULTRA (c7=%lld c99=%lld)\n",
               (long long)c7, (long long)c99);
        free(comp7);
        goto fail;
    }
    free(comp7);
    if (zxc_compress_cctx(cctx, src, src_sz, comp, comp_cap, &lvl99) != c7) {
        printf("  [FAIL] zxc_compress_cctx(level=99) must clamp to ULTRA\n");
        goto fail;
    }
    if (zxc_compress_block(cctx, src, src_sz, comp, comp_cap, &lvl99) <= 0) {
        printf("  [FAIL] zxc_compress_block(level=99) must clamp to ULTRA\n");
        goto fail;
    }
    zxc_cctx* cctx99 = zxc_create_cctx(&lvl99);
    if (!cctx99) {
        printf("  [FAIL] zxc_create_cctx(level=99) must clamp, not fail\n");
        goto fail;
    }
    zxc_free_cctx(cctx99);
    printf("  [PASS] level 99 silently clamped to ULTRA across entry points\n");

    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
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

    const int LVL = 3;

    /* 1. Zero input returns zero. */
    if (zxc_estimate_cctx_size(0, LVL) != 0) {
        printf("  [FAIL] estimate(0) must return 0\n");
        return 0;
    }
    printf("  [PASS] estimate(0) == 0\n");

    /* 2. Non-zero input returns non-zero estimate. */
    const uint64_t e1k = zxc_estimate_cctx_size(1024, LVL);
    if (e1k == 0) {
        printf("  [FAIL] estimate(1 KiB) must be > 0\n");
        return 0;
    }
    printf("  [PASS] estimate(1 KiB) = %llu bytes\n", (unsigned long long)e1k);

    /* 3. Sizes below ZXC_BLOCK_SIZE_MIN collapse to the same estimate. */
    if (zxc_estimate_cctx_size(512, LVL) != e1k || zxc_estimate_cctx_size(4096, LVL) != e1k) {
        printf("  [FAIL] estimates below MIN must round to ZXC_BLOCK_SIZE_MIN\n");
        return 0;
    }
    printf("  [PASS] estimate rounds sub-MIN inputs to the same value\n");

    /* 4. Monotonic: estimate grows with src_size across block_size tiers. */
    const uint64_t e64k = zxc_estimate_cctx_size(64 * 1024, LVL);
    const uint64_t e1m = zxc_estimate_cctx_size(1024 * 1024, LVL);
    const uint64_t e8m = zxc_estimate_cctx_size(8 * 1024 * 1024, LVL);
    if (!(e1k <= e64k && e64k <= e1m && e1m <= e8m)) {
        printf("  [FAIL] estimates must be monotonic: %llu, %llu, %llu, %llu\n",
               (unsigned long long)e1k, (unsigned long long)e64k, (unsigned long long)e1m,
               (unsigned long long)e8m);
        return 0;
    }
    printf("  [PASS] monotonic: 1K=%llu, 64K=%llu, 1M=%llu, 8M=%llu\n", (unsigned long long)e1k,
           (unsigned long long)e64k, (unsigned long long)e1m, (unsigned long long)e8m);

    /* 5. Sanity: estimate for a large block must exceed the block itself. */
    if (e1m < 1024 * 1024) {
        printf("  [FAIL] estimate(1 MiB)=%llu should exceed 1 MiB\n", (unsigned long long)e1m);
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
    printf("  [PASS] scaling: 8x src_size -> %.2fx memory\n", (double)e8m / (double)e1m);

    /* 7. Level 6 includes the optimal-parser scratch peak (~18 x chunk_size)
     *    on top of the persistent cctx, so it must exceed the level-3 figure
     *    by at least one chunk_size worth of bytes. */
    const uint64_t e1m_l3 = zxc_estimate_cctx_size(1024 * 1024, 3);
    const uint64_t e1m_l6 = zxc_estimate_cctx_size(1024 * 1024, 6);
    if (e1m_l6 <= e1m_l3 + (1024 * 1024)) {
        printf("  [FAIL] level 6 must add optimal-parser scratch: l3=%llu l6=%llu\n",
               (unsigned long long)e1m_l3, (unsigned long long)e1m_l6);
        return 0;
    }
    printf("  [PASS] level 6 vs level 3 at 1 MiB: l3=%llu l6=%llu (delta=%llu)\n",
           (unsigned long long)e1m_l3, (unsigned long long)e1m_l6,
           (unsigned long long)(e1m_l6 - e1m_l3));

    printf("PASS\n\n");
    return 1;
}

/* A seek table sits between the EOF block and the footer. The one-shot reads
 * the footer from the end of the archive; the context path used to read it
 * straight after the EOF block and parse the seek table as the footer, so it
 * refused archives zxc_decompress() accepts. */
int test_context_api_seekable_frame(void) {
    printf("=== TEST: Context API - seekable archives decode through a context ===\n");
    static uint8_t body[16384], arc[24576], out[16384];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)('a' + (i % 23));

    zxc_compress_opts_t co = {.level = 3, .block_size = 4096, .seekable = 1};
    const int64_t n = zxc_compress(body, sizeof(body), arc, sizeof(arc), &co);
    if (n <= 0) {
        printf("  [FAIL] seekable compress returned %lld\n", (long long)n);
        return 0;
    }
    zxc_dctx* dctx = zxc_create_dctx();
    if (!dctx) {
        printf("  [FAIL] zxc_create_dctx\n");
        return 0;
    }
    const int64_t d1 = zxc_decompress(arc, (size_t)n, out, sizeof(out), NULL);
    memset(out, 0, sizeof(out));
    const int64_t d2 = zxc_decompress_dctx(dctx, arc, (size_t)n, out, sizeof(out), NULL);
    const int same = (d2 == (int64_t)sizeof(body)) && memcmp(out, body, sizeof(body)) == 0;
    const int64_t p1 = zxc_decompress(arc, (size_t)n, NULL, 0, NULL);
    const int64_t p2 = zxc_decompress_dctx(dctx, arc, (size_t)n, NULL, 0, NULL);
    zxc_free_dctx(dctx);

    if (d1 != (int64_t)sizeof(body) || !same || p1 != ZXC_ERROR_DST_TOO_SMALL || p1 != p2) {
        printf("  [FAIL] one-shot %lld, context %lld (bytes %s), probes %lld %lld\n", (long long)d1,
               (long long)d2, same ? "ok" : "differ", (long long)p1, (long long)p2);
        return 0;
    }
    printf("  [PASS] seek table between EOF and footer: both entry points agree\n");
    printf("PASS\n\n");
    return 1;
}

/* Pins both halves of the no-destination contract, through both entry points:
 * what the probe answers, and what a real decode of the same bytes answers.
 * They agree on every archive that reports an empty payload. On one that stores
 * data the probe decodes nothing and can only say "give me a buffer", so the
 * two verdicts are pinned separately rather than assumed equal. */
static int probe_and_decode(const char* what, const uint8_t* arc, const size_t n,
                            const zxc_decompress_opts_t* opts, const int64_t want_probe,
                            const int64_t want_decode) {
    static uint8_t out[8192];
    zxc_dctx* dctx = zxc_create_dctx();
    if (!dctx) {
        printf("  [FAIL] %s: zxc_create_dctx\n", what);
        return 0;
    }
    const int64_t p1 = zxc_decompress(arc, n, NULL, 0, opts);
    const int64_t p2 = zxc_decompress_dctx(dctx, arc, n, NULL, 0, opts);
    const int64_t d1 = zxc_decompress(arc, n, out, sizeof(out), opts);
    const int64_t d2 = zxc_decompress_dctx(dctx, arc, n, out, sizeof(out), opts);
    zxc_free_dctx(dctx);
    if (p1 == want_probe && p2 == want_probe && d1 == want_decode && d2 == want_decode) return 1;
    printf("  [FAIL] %s: probe %lld %lld (want %lld), decode %lld %lld (want %lld)\n", what,
           (long long)p1, (long long)p2, (long long)want_probe, (long long)d1, (long long)d2,
           (long long)want_decode);
    return 0;
}

/* An empty source is a valid frame: the reusable context must write the same
 * archive as the one-shot entry point, and stay usable afterwards. */
int test_context_api_empty_input(void) {
    printf("=== TEST: Context API - empty input matches the one-shot ===\n");
    uint8_t one_shot[64], from_ctx[64];
    const zxc_compress_opts_t co = {.level = 3};
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    int ok = 0;
    do {
        if (!cctx) {
            printf("  [FAIL] zxc_create_cctx\n");
            break;
        }
        const int64_t expected =
            ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE;
        const int64_t n1 = zxc_compress(NULL, 0, one_shot, sizeof(one_shot), &co);
        const int64_t n2 = zxc_compress_cctx(cctx, NULL, 0, from_ctx, sizeof(from_ctx), &co);
        if (n1 != expected || n2 != n1 || memcmp(one_shot, from_ctx, (size_t)n1) != 0) {
            printf("  [FAIL] one-shot %lld, context %lld (expected %lld)\n", (long long)n1,
                   (long long)n2, (long long)expected);
            break;
        }
        /* The other accepted shape: a real pointer with a zero size. */
        uint8_t probe = 0;
        const int64_t n2b = zxc_compress_cctx(cctx, &probe, 0, from_ctx, sizeof(from_ctx), &co);
        if (n2b != n1 || memcmp(one_shot, from_ctx, (size_t)n1) != 0) {
            printf("  [FAIL] non-NULL source with a zero size: %lld\n", (long long)n2b);
            break;
        }
        /* Every shape below is checked both ways. Verdicts are tallied rather
         * than tested one by one, so a regression shows every shape it broke. */
        const uint8_t junk[36] = {0};
        int disagreed = 0;
        disagreed += !probe_and_decode("empty archive", from_ctx, (size_t)n2, NULL, 0, 0);
        disagreed += !probe_and_decode("zeroed junk", junk, sizeof(junk), NULL, ZXC_ERROR_BAD_MAGIC,
                                       ZXC_ERROR_BAD_MAGIC);
        disagreed += !probe_and_decode("header without footer", from_ctx, ZXC_FILE_HEADER_SIZE,
                                       NULL, ZXC_ERROR_SRC_TOO_SMALL, ZXC_ERROR_SRC_TOO_SMALL);

        /* Crafted block headers, each rewritten through zxc_write_block_header
         * so its hash8 stays valid: a broken hash masks the shape check behind
         * it. */
        uint8_t bad[256];
        zxc_block_header_t bh;
        memcpy(bad, from_ctx, (size_t)n2);
        if (zxc_read_block_header(bad + ZXC_FILE_HEADER_SIZE, (size_t)n2 - ZXC_FILE_HEADER_SIZE,
                                  &bh) != ZXC_OK) {
            printf("  [FAIL] could not read the empty archive's EOF block\n");
            break;
        }
        const zxc_block_header_t eof = bh;

        bh.comp_size = 7; /* EOF carries no payload */
        zxc_write_block_header(bad + ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &bh);
        disagreed += !probe_and_decode("EOF with a payload size", bad, (size_t)n2, NULL,
                                       ZXC_ERROR_BAD_HEADER, ZXC_ERROR_BAD_HEADER);

        bh = eof;
        bh.block_type = ZXC_BLOCK_RAW; /* an empty archive stores no data block */
        zxc_write_block_header(bad + ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &bh);
        disagreed += !probe_and_decode("data block where EOF belongs", bad, (size_t)n2, NULL,
                                       ZXC_ERROR_BAD_HEADER, ZXC_ERROR_BAD_HEADER);

        /* A corrupted global checksum and an unsupplied dictionary are only
         * caught by the frame walk, so the probe has to reach it. */
        const zxc_compress_opts_t cs_co = {.level = 3, .checksum_enabled = 1};
        const zxc_decompress_opts_t cs_do = {.checksum_enabled = 1};
        const int64_t cn = zxc_compress(NULL, 0, bad, sizeof(bad), &cs_co);
        if (cn <= (int64_t)ZXC_FILE_FOOTER_SIZE) {
            printf("  [FAIL] empty+checksum setup: %lld\n", (long long)cn);
            break;
        }
        bad[cn - 1] ^= 0xFF;
        disagreed += !probe_and_decode("corrupted global checksum", bad, (size_t)cn, &cs_do,
                                       ZXC_ERROR_BAD_CHECKSUM, ZXC_ERROR_BAD_CHECKSUM);

        static uint8_t dict[8192];
        memset(dict, 'x', sizeof(dict));
        zxc_compress_opts_t dict_co = {.level = 3};
        dict_co.dict = dict;
        dict_co.dict_size = sizeof(dict);
        zxc_decompress_opts_t dict_do = {0};
        dict_do.dict = dict;
        dict_do.dict_size = sizeof(dict);
        const int64_t dn = zxc_compress(NULL, 0, bad, sizeof(bad), &dict_co);
        if (dn <= (int64_t)ZXC_FILE_FOOTER_SIZE) {
            printf("  [FAIL] empty+dict setup: %lld\n", (long long)dn);
            break;
        }
        disagreed += !probe_and_decode("dictionary not supplied", bad, (size_t)dn, NULL,
                                       ZXC_ERROR_DICT_REQUIRED, ZXC_ERROR_DICT_REQUIRED);

        /* A forged stored size is corrupt data, not "destination too small". */
        static const char payload[] = "a forged footer must read as corrupt data, not as size";
        const int64_t fn =
            zxc_compress_cctx(cctx, payload, sizeof(payload) - 1, bad, sizeof(bad), &co);
        if (fn <= (int64_t)ZXC_FILE_FOOTER_SIZE) {
            printf("  [FAIL] forged footer setup: compress returned %lld\n", (long long)fn);
            break;
        }
        memset(bad + fn - ZXC_FILE_FOOTER_SIZE, 0xFF, 4);
        disagreed += !probe_and_decode("forged stored size", bad, (size_t)fn, NULL,
                                       ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA);

        /* Archives that store data: the probe has no buffer to decode into, so
         * it reports DST_TOO_SMALL where the decode names the real fault. Both
         * sides are pinned: neither half follows from the other. */
        const int64_t pn =
            zxc_compress_cctx(cctx, payload, sizeof(payload) - 1, bad, sizeof(bad), &co);
        if (pn <= (int64_t)ZXC_FILE_FOOTER_SIZE) {
            printf("  [FAIL] payload archive setup: %lld\n", (long long)pn);
            break;
        }
        disagreed += !probe_and_decode("intact payload", bad, (size_t)pn, NULL,
                                       ZXC_ERROR_DST_TOO_SMALL, (int64_t)(sizeof(payload) - 1));
        /* A footer claiming an empty payload the blocks contradict. */
        uint8_t zeroed[256];
        memcpy(zeroed, bad, (size_t)pn);
        memset(zeroed + pn - ZXC_FILE_FOOTER_SIZE, 0, 8);
        disagreed += !probe_and_decode("payload behind a zeroed size", zeroed, (size_t)pn, NULL,
                                       ZXC_ERROR_DST_TOO_SMALL, ZXC_ERROR_CORRUPT_DATA);
        /* Same, with the dictionary the archive needs: the probe still refuses
         * on capacity, through the dictionary bounce path this time. */
        const int64_t pdn =
            zxc_compress_cctx(cctx, payload, sizeof(payload) - 1, bad, sizeof(bad), &dict_co);
        if (pdn <= (int64_t)ZXC_FILE_FOOTER_SIZE) {
            printf("  [FAIL] dict payload archive setup: %lld\n", (long long)pdn);
            break;
        }
        disagreed += !probe_and_decode("payload behind a dictionary", bad, (size_t)pdn, &dict_do,
                                       ZXC_ERROR_DST_TOO_SMALL, (int64_t)(sizeof(payload) - 1));

        /* A dictionary size the library cannot honour is a caller error, not an
         * archive fault, so it outranks both the payload refusal and anything
         * the header could be rejected for. */
        zxc_decompress_opts_t huge_do = {0};
        huge_do.dict = dict;
        huge_do.dict_size = (size_t)ZXC_DICT_SIZE_MAX + 1;
        disagreed +=
            !probe_and_decode("oversized dictionary, empty archive", from_ctx, (size_t)n2, &huge_do,
                              ZXC_ERROR_DICT_TOO_LARGE, ZXC_ERROR_DICT_TOO_LARGE);
        disagreed +=
            !probe_and_decode("oversized dictionary, payload archive", bad, (size_t)pdn, &huge_do,
                              ZXC_ERROR_DICT_TOO_LARGE, ZXC_ERROR_DICT_TOO_LARGE);
        /* Including over a source too short to hold a frame: the caller's fault
         * is settled before anything is read from it. */
        disagreed += !probe_and_decode("oversized dictionary, truncated source", from_ctx,
                                       ZXC_FILE_HEADER_SIZE, &huge_do, ZXC_ERROR_DICT_TOO_LARGE,
                                       ZXC_ERROR_DICT_TOO_LARGE);

        /* A NULL destination with a non-zero capacity is a caller mistake, not
         * a probe, and both entry points have to say so. */
        zxc_dctx* nd = zxc_create_dctx();
        if (!nd) {
            printf("  [FAIL] zxc_create_dctx\n");
            break;
        }
        const int64_t z1 = zxc_decompress(from_ctx, (size_t)n2, NULL, 64, NULL);
        const int64_t z2 = zxc_decompress_dctx(nd, from_ctx, (size_t)n2, NULL, 64, NULL);
        zxc_free_dctx(nd);
        if (z1 != ZXC_ERROR_NULL_INPUT || z2 != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] NULL destination with capacity 64: %lld %lld\n", (long long)z1,
                   (long long)z2);
            break;
        }
        if (disagreed) break;
        printf("  [PASS] %lld-byte empty archive: same bytes and same codes as the one-shot\n",
               (long long)n2);

        /* The context keeps working for a normal payload. */
        static const char text[] = "the quick brown fox jumps over the lazy dog, twice over";
        const size_t len = sizeof(text) - 1;
        uint8_t comp[256], back[256];
        const int64_t n3 = zxc_compress_cctx(cctx, text, len, comp, sizeof(comp), &co);
        const int64_t back_len =
            n3 > 0 ? zxc_decompress(comp, (size_t)n3, back, sizeof(back), NULL) : n3;
        if (back_len != (int64_t)len || memcmp(back, text, len) != 0) {
            printf("  [FAIL] next call: %lld -> %lld\n", (long long)n3, (long long)back_len);
            break;
        }
        printf("  [PASS] the context still compresses after an empty call\n");

        /* An archive with a payload is the one case where the two answers must
         * differ: nothing is wrong with it, there is just nowhere to put it. */
        if (zxc_decompress(comp, (size_t)n3, NULL, 0, NULL) != ZXC_ERROR_DST_TOO_SMALL) {
            printf("  [FAIL] probing a non-empty archive should be DST_TOO_SMALL\n");
            break;
        }

        /* The empty-source fast path skips the option plumbing a normal call
         * runs. Pin what it must still produce: the one-shot's bytes for the
         * same options, and a dictionary left intact for the next call. */
        const zxc_compress_opts_t cs_only = {.level = 3, .checksum_enabled = 1};
        const int64_t e1 = zxc_compress(NULL, 0, one_shot, sizeof(one_shot), &cs_only);
        const int64_t e2 = zxc_compress_cctx(cctx, NULL, 0, bad, sizeof(bad), &cs_only);
        if (e1 <= 0 || e2 != e1 || memcmp(one_shot, bad, (size_t)e1) != 0) {
            printf("  [FAIL] empty + checksum: one-shot %lld, context %lld\n", (long long)e1,
                   (long long)e2);
            break;
        }
        const int64_t e3 = zxc_compress(NULL, 0, one_shot, sizeof(one_shot), &dict_co);
        const int64_t e4 = zxc_compress_cctx(cctx, NULL, 0, bad, sizeof(bad), &dict_co);
        if (e3 <= 0 || e4 != e3 || memcmp(one_shot, bad, (size_t)e3) != 0) {
            printf("  [FAIL] empty + dictionary: one-shot %lld, context %lld\n", (long long)e3,
                   (long long)e4);
            break;
        }
        const int64_t dc = zxc_compress_cctx(cctx, text, len, comp, sizeof(comp), &dict_co);
        const int64_t dr =
            dc > 0 ? zxc_decompress(comp, (size_t)dc, back, sizeof(back), &dict_do) : dc;
        if (dr != (int64_t)len || memcmp(back, text, len) != 0) {
            printf("  [FAIL] dictionary lost across the empty call: %lld -> %lld\n", (long long)dc,
                   (long long)dr);
            break;
        }
        printf("  [PASS] the empty fast path keeps checksum and dictionary options\n");

        /* The block API keeps its documented [1, MAX] contract: a real pointer
         * with a zero size must be refused on the size alone. */
        if (zxc_compress_block(cctx, text, 0, comp, sizeof(comp), &co) != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] the block API should refuse an empty block\n");
            break;
        }
        printf("  [PASS] the block API still refuses an empty block\n");
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    if (ok) printf("PASS\n\n");
    return ok;
}
