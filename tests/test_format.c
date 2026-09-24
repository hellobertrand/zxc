/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

/* Round-trip the Huffman codec over a few representative literal distributions. */
static int huf_roundtrip_case(const char* label, const uint8_t* literals, size_t n) {
    uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
    for (size_t i = 0; i < n; i++) freq[literals[i]]++;

    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    if (zxc_huf_build_code_lengths(freq, code_len, NULL, ZXC_HUF_MAX_CODE_LEN_DENSITY) != ZXC_OK) {
        printf("Failed [%s]: build_code_lengths\n", label);
        return 0;
    }
    /* Validate the lengths-limit invariant. */
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (code_len[i] > ZXC_HUF_MAX_CODE_LEN_ULTRA) {
            printf("Failed [%s]: code_len[%d] = %d > %d\n", label, i, code_len[i],
                   ZXC_HUF_MAX_CODE_LEN_ULTRA);
            return 0;
        }
    }

    /* Worst-case payload size: 128-byte header + packed codes + per-node pad. */
    const size_t cap = ZXC_HUF_TABLE_SIZE + 2 * n + 4096;
    uint8_t* enc = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(n + ZXC_PAD_SIZE);
    uint8_t* scr = (uint8_t*)malloc(n + ZXC_PIVCO_SCRATCH_PAD);
    int ok = 0;
    if (!enc || !dec || !scr) {
        printf("Failed [%s]: alloc\n", label);
        goto done;
    }

    const int written = zxc_huf_encode_section(literals, n, freq, code_len, enc, cap);
    if (written < 0) {
        printf("Failed [%s]: encode_section -> %d\n", label, written);
        goto done;
    }

    /* The Lagrangian selector prices candidates with zxc_huf_calc_size; it must
     * predict the EXACT encoded size (with the 128-byte lengths header). */
    const size_t est = zxc_huf_calc_size(freq, code_len, 1);
    if (est != (size_t)written) {
        printf("Failed [%s]: calc_size %zu != encoded %d\n", label, est, written);
        goto done;
    }

    const int rc = zxc_huf_decode_section(enc, (size_t)written, dec, n, scr);
    if (rc != ZXC_OK) {
        printf("Failed [%s]: decode_section -> %d\n", label, rc);
        goto done;
    }

    if (memcmp(literals, dec, n) != 0) {
        printf("Failed [%s]: roundtrip mismatch\n", label);
        goto done;
    }

    printf("  [PASS] %s (n=%zu, encoded=%d B, ratio=%.1f%%)\n", label, n, written,
           100.0 * (double)written / (double)n);
    ok = 1;
done:
    free(enc);
    free(dec);
    free(scr);
    return ok;
}

int test_huffman_codec() {
    printf("=== TEST: Unit - Huffman Codec (build/encode/decode roundtrip) ===\n");

    const size_t N = 8192;
    uint8_t* buf = (uint8_t*)malloc(N);
    if (!buf) return 0;

    /* Case 1: heavily skewed (90% one byte, 10% noise). */
    for (size_t i = 0; i < N; i++)
        buf[i] = (zxc_test_rand() % 10 == 0) ? (uint8_t)(zxc_test_rand() & 0xFF) : 'A';
    if (!huf_roundtrip_case("Skewed (90% 'A')", buf, N)) {
        free(buf);
        return 0;
    }

    /* Case 2: uniform random - Huffman should be near no-op (~1 byte/sym). */
    for (size_t i = 0; i < N; i++) buf[i] = (uint8_t)(zxc_test_rand() & 0xFF);
    if (!huf_roundtrip_case("Uniform random", buf, N)) {
        free(buf);
        return 0;
    }

    /* Case 3: two-symbol alphabet - best case, ~1 bit/symbol. */
    for (size_t i = 0; i < N; i++) buf[i] = (zxc_test_rand() & 1) ? 'X' : 'Y';
    if (!huf_roundtrip_case("Two-symbol alphabet", buf, N)) {
        free(buf);
        return 0;
    }

    /* Case 4: single-symbol - degenerate but must still roundtrip. */
    for (size_t i = 0; i < N; i++) buf[i] = 'Z';
    if (!huf_roundtrip_case("Single-symbol", buf, N)) {
        free(buf);
        return 0;
    }

    /* Case 5: small block (just above the min-literals threshold). */
    for (size_t i = 0; i < ZXC_HUF_MIN_LITERALS; i++)
        buf[i] = (zxc_test_rand() % 4 == 0) ? (uint8_t)(zxc_test_rand() & 0xFF) : 'k';
    if (!huf_roundtrip_case("Small block at threshold", buf, ZXC_HUF_MIN_LITERALS)) {
        free(buf);
        return 0;
    }

    free(buf);
    printf("PASS\n\n");
    return 1;
}

/* --------------------------------------------------------------------------
 * Encoder-side flat/length nudge (zxc_huf_nudge_code_lengths)
 * -------------------------------------------------------------------------- */

/* Cross-check the nudge's buddy-decomposition cost model against the REAL
 * tree the decoder builds: bits and modeled level-touches must match exactly,
 * with the same flat/leaf-pair/deep-flat weighting as zxc_pivco_decode_core.
 * This pins the "flat coverage from bl_count[] alone" math to the shipping
 * flat detector (zxc_pivco_tree_build) for any valid length vector. */
static int nudge_cost_matches_tree(const char* label, const uint8_t* code_len,
                                   const uint32_t* freq) {
    uint64_t bits = 0;
    uint64_t touches = 0;
    zxc_huf_nudge_cost(code_len, freq, &bits, &touches);

    uint8_t packed[ZXC_HUF_TABLE_SIZE];
    zxc_huf_pack_lengths(code_len, packed);
    static zxc_pivco_tree_t tree;
    static uint32_t codes[ZXC_HUF_NUM_SYMBOLS];
    uint8_t len2[ZXC_HUF_NUM_SYMBOLS];
    if (zxc_huf_dict_tree_build(packed, &tree, codes, len2) != ZXC_OK) {
        printf("Failed [%s]: reference tree build rejected the lengths\n", label);
        return 0;
    }

    /* Node counts, bottom-up; the lone root's missing child is the empty slot. */
    uint32_t count[ZXC_PIVCO_NODE_SLOTS] = {0};
    for (int d = tree.max_depth; d >= 0; d--) {
        const int L = tree.leaf_base[d + 1] - tree.leaf_base[d];
        const int N = tree.node_base[d + 1] - tree.node_base[d];
        for (int i = 0; i < L; i++)
            count[tree.node_base[d] + i] = freq[tree.syms[tree.leaf_base[d] + i]];
        for (int i = L; i < N; i++) {
            const int k2 = tree.node_base[d + 1] + 2 * (i - L);
            count[tree.node_base[d] + i] = count[k2] + count[k2 + 1];
        }
    }

    uint64_t rbits = 0;
    for (int s = 0; s < ZXC_HUF_NUM_SYMBOLS; s++) rbits += (uint64_t)code_len[s] * freq[s];
    uint64_t rtouches = 0;
    for (int d = 0; d <= tree.max_depth; d++) {
        const int L = tree.leaf_base[d + 1] - tree.leaf_base[d];
        const int N = tree.node_base[d + 1] - tree.node_base[d];
        /* A flat or covered parent hides its children. */
        const uint8_t* parent =
            d ? tree.flat_d + tree.node_base[d - 1] + tree.leaf_base[d] - tree.leaf_base[d - 1]
              : NULL;
        for (int i = 0; i < N; i++) {
            if (parent && parent[i >> 1]) continue;
            const uint32_t c = count[tree.node_base[d] + i];
            if (i < L) {
                /* Lone leaf memset; leaf-pair children are emitted by the parent. */
                if ((i ^ 1) >= L) rtouches += c;
            } else if (tree.flat_d[tree.node_base[d] + i]) {
                uint64_t t = 1;
                if (tree.flat_d[tree.node_base[d] + i] > ZXC_PIVCO_UNPACK_FLAT_SIMD_MAX)
                    t += ZXC_HUF_NUDGE_DEEP_FLAT_PENALTY;
                rtouches += (uint64_t)c * t;
            } else {
                rtouches += c; /* merge node (leaf-pair parents included) */
            }
        }
    }
    rtouches += (uint64_t)ZXC_HUF_NUDGE_LEVEL_COST * (uint64_t)(tree.max_depth + 1);

    if (bits != rbits || touches != rtouches) {
        printf("Failed [%s]: model (bits=%llu touches=%llu) != tree (bits=%llu touches=%llu)\n",
               label, (unsigned long long)bits, (unsigned long long)touches,
               (unsigned long long)rbits, (unsigned long long)rtouches);
        return 0;
    }
    return 1;
}

/* Full nudge pipeline on one distribution and cap: build -> nudge -> validate
 * structure, the calc_size == encode invariant, the decode roundtrip, the
 * adoption guard arithmetic, determinism, and the cost model on both the
 * baseline and the (possibly) adopted vector. */
static int huf_nudge_case(const char* label, const uint8_t* literals, size_t n_lit,
                          const int max_code_len) {
    uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
    for (size_t i = 0; i < n_lit; i++) freq[literals[i]]++;

    uint8_t base_len[ZXC_HUF_NUM_SYMBOLS];
    if (zxc_huf_build_code_lengths(freq, base_len, NULL, max_code_len) != ZXC_OK) {
        printf("Failed [%s]: build_code_lengths\n", label);
        return 0;
    }
    if (!nudge_cost_matches_tree(label, base_len, freq)) return 0;

    uint8_t nudged[ZXC_HUF_NUM_SYMBOLS];
    memcpy(nudged, base_len, sizeof(nudged));
    const int adopted = zxc_huf_nudge_code_lengths(freq, nudged, NULL, 0, max_code_len);

    /* Determinism: a second independent run must reproduce the result. */
    uint8_t nudged2[ZXC_HUF_NUM_SYMBOLS];
    memcpy(nudged2, base_len, sizeof(nudged2));
    const int adopted2 = zxc_huf_nudge_code_lengths(freq, nudged2, NULL, 0, max_code_len);
    if (adopted != adopted2 || memcmp(nudged, nudged2, sizeof(nudged)) != 0) {
        printf("Failed [%s]: nudge is not deterministic\n", label);
        return 0;
    }

    /* A builder-sized scratch (workspace allocated) and a full one: same result. */
    {
        const size_t caps[2] = {ZXC_HUF_BUILD_SCRATCH_SIZE, ZXC_HUF_NUDGE_SCRATCH_SIZE};
        for (int i = 0; i < 2; i++) {
            uint8_t* scratch = (uint8_t*)malloc(caps[i]);
            if (!scratch) return 0;
            memcpy(nudged2, base_len, sizeof(nudged2));
            const int adopted3 =
                zxc_huf_nudge_code_lengths(freq, nudged2, scratch, caps[i], max_code_len);
            free(scratch);
            if (adopted3 != adopted || memcmp(nudged, nudged2, sizeof(nudged)) != 0) {
                printf("Failed [%s]: nudge differs with a %zu-byte scratch\n", label, caps[i]);
                return 0;
            }
        }
    }

    if (!adopted) {
        if (memcmp(nudged, base_len, sizeof(nudged)) != 0) {
            printf("Failed [%s]: rejected nudge modified the lengths\n", label);
            return 0;
        }
    } else {
        /* Structural rails: cap respected, every live symbol keeps a code.
         * (Coarse-DP candidates may add zero-freq ghost leaves, so a length
         * on a freq == 0 symbol is legal; the reverse is not.) */
        for (int s = 0; s < ZXC_HUF_NUM_SYMBOLS; s++) {
            if (nudged[s] > max_code_len || (freq[s] != 0 && nudged[s] == 0)) {
                printf("Failed [%s]: adopted lengths invalid at sym %d (len=%d)\n", label, s,
                       nudged[s]);
                return 0;
            }
        }
        /* The adoption guard must hold on the exact model costs. */
        uint64_t b0, t0, b1, t1;
        zxc_huf_nudge_cost(base_len, freq, &b0, &t0);
        zxc_huf_nudge_cost(nudged, freq, &b1, &t1);
        if (b1 * 1000 > b0 * ZXC_HUF_NUDGE_BITS_PERMIL || t1 * 256 > t0 * ZXC_HUF_NUDGE_MERGE_Q8) {
            printf(
                "Failed [%s]: adopted candidate violates the guard "
                "(bits %llu->%llu, touches %llu->%llu)\n",
                label, (unsigned long long)b0, (unsigned long long)b1, (unsigned long long)t0,
                (unsigned long long)t1);
            return 0;
        }
        if (!nudge_cost_matches_tree(label, nudged, freq)) return 0;
    }

    /* Selector/encoder invariant + decode roundtrip on the final vector. */
    const size_t cap = ZXC_HUF_TABLE_SIZE + 2 * n_lit + 4096;
    uint8_t* enc = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(n_lit + ZXC_PAD_SIZE);
    uint8_t* scr = (uint8_t*)malloc(n_lit + ZXC_PIVCO_SCRATCH_PAD);
    int ok = 0;
    if (!enc || !dec || !scr) {
        printf("Failed [%s]: alloc\n", label);
        goto done;
    }
    {
        const int written = zxc_huf_encode_section(literals, n_lit, freq, nudged, enc, cap);
        if (written < 0) {
            printf("Failed [%s]: encode_section -> %d\n", label, written);
            goto done;
        }
        const size_t est = zxc_huf_calc_size(freq, nudged, 1);
        if (est != (size_t)written) {
            printf("Failed [%s]: calc_size %zu != encoded %d\n", label, est, written);
            goto done;
        }
        if (zxc_huf_decode_section(enc, (size_t)written, dec, n_lit, scr) != ZXC_OK ||
            memcmp(literals, dec, n_lit) != 0) {
            printf("Failed [%s]: nudged roundtrip mismatch\n", label);
            goto done;
        }
    }
    printf("  [PASS] %s (cap=%d, %s)\n", label, max_code_len, adopted ? "adopted" : "kept");
    ok = 1;
done:
    free(enc);
    free(dec);
    free(scr);
    return ok;
}

int test_huffman_nudge() {
    printf("=== TEST: Unit - Huffman flat/length nudge (model + guard + roundtrip) ===\n");

    const size_t N = 16384;
    uint8_t* buf = (uint8_t*)malloc(N);
    if (!buf) return 0;
    int ok = 1;

    /* Geometric: deep 11-bit trees at ULTRA cap, the nudge's main target. */
    for (size_t i = 0; i < N; i++) {
        uint32_t r = zxc_test_rand();
        int b = 0;
        while (b < 17 && (r & 1)) {
            b++;
            r >>= 1;
        }
        buf[i] = (uint8_t)b;
    }
    ok &= huf_nudge_case("Geometric", buf, N, ZXC_HUF_MAX_CODE_LEN_ULTRA);
    ok &= huf_nudge_case("Geometric cap8", buf, N, ZXC_HUF_MAX_CODE_LEN_DENSITY);

    /* Zipf over the full alphabet: ragged class counts, boundary rounding. */
    for (size_t i = 0; i < N; i++) {
        const uint32_t r = zxc_test_rand() % 6000;
        uint32_t s = 0;
        uint32_t acc = 0;
        while (s < 255) {
            acc += 1000 / (s + 1);
            if (r < acc) break;
            s++;
        }
        buf[i] = (uint8_t)s;
    }
    ok &= huf_nudge_case("Zipf", buf, N, ZXC_HUF_MAX_CODE_LEN_ULTRA);
    ok &= huf_nudge_case("Zipf cap8", buf, N, ZXC_HUF_MAX_CODE_LEN_DENSITY);

    /* Text-like: few dozen symbols, mild skew (the common literal section). */
    for (size_t i = 0; i < N; i++) {
        const uint32_t r = zxc_test_rand();
        buf[i] = (uint8_t)('a' + (((r & 0xFF) * ((r >> 8) & 0xFF)) >> 11));
    }
    ok &= huf_nudge_case("Text-like", buf, N, ZXC_HUF_MAX_CODE_LEN_ULTRA);

    /* Uniform 256: already a single flat root; the nudge must keep baseline. */
    for (size_t i = 0; i < N; i++) buf[i] = (uint8_t)(i & 0xFF);
    {
        uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
        for (size_t i = 0; i < N; i++) freq[buf[i]]++;
        uint8_t len[ZXC_HUF_NUM_SYMBOLS];
        if (zxc_huf_build_code_lengths(freq, len, NULL, ZXC_HUF_MAX_CODE_LEN_DENSITY) != ZXC_OK) {
            ok = 0;
        } else {
            uint8_t kept[ZXC_HUF_NUM_SYMBOLS];
            memcpy(kept, len, sizeof(kept));
            if (zxc_huf_nudge_code_lengths(freq, len, NULL, 0, ZXC_HUF_MAX_CODE_LEN_DENSITY) != 0 ||
                memcmp(kept, len, sizeof(kept)) != 0) {
                printf("Failed [Uniform-256]: expected a no-op nudge\n");
                ok = 0;
            } else {
                printf("  [PASS] Uniform-256 no-op\n");
            }
        }
    }

    /* Degenerate alphabets (n = 1, 2, 3): must never be touched. */
    for (int nsym = 1; nsym <= 3; nsym++) {
        uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
        for (int s = 0; s < nsym; s++) freq[(uint8_t)('A' + s)] = (uint32_t)(100 * (s + 1));
        uint8_t len[ZXC_HUF_NUM_SYMBOLS];
        uint8_t kept[ZXC_HUF_NUM_SYMBOLS];
        if (zxc_huf_build_code_lengths(freq, len, NULL, ZXC_HUF_MAX_CODE_LEN_DENSITY) != ZXC_OK) {
            ok = 0;
            continue;
        }
        memcpy(kept, len, sizeof(kept));
        if (zxc_huf_nudge_code_lengths(freq, len, NULL, 0, ZXC_HUF_MAX_CODE_LEN_DENSITY) != 0 ||
            memcmp(kept, len, sizeof(kept)) != 0) {
            printf("Failed [Degenerate n=%d]: expected untouched lengths\n", nsym);
            ok = 0;
        }
    }
    if (ok) printf("  [PASS] Degenerate n=1..3 untouched\n");

    /* Fuzz: random alphabets/frequencies; the cost model must match the real
     * tree for every baseline AND every adopted vector, at both caps (large
     * alphabets exercise the coarse-DP tiers and their ghost padding). */
    {
        int checked = 0;
        int adopted_cnt = 0;
        for (int it = 0; it < 300 && ok; it++) {
            uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
            const int nsym = 4 + (int)(zxc_test_rand() % 253);
            for (int s = 0; s < nsym; s++)
                freq[(uint8_t)(zxc_test_rand() & 0xFF)] = 1 + (zxc_test_rand() & 0xFFFFF);
            const int cap = (it & 1) ? ZXC_HUF_MAX_CODE_LEN_ULTRA : ZXC_HUF_MAX_CODE_LEN_DENSITY;
            uint8_t len[ZXC_HUF_NUM_SYMBOLS];
            if (zxc_huf_build_code_lengths(freq, len, NULL, cap) != ZXC_OK) {
                ok = 0;
                break;
            }
            if (!nudge_cost_matches_tree("Fuzz baseline", len, freq)) {
                ok = 0;
                break;
            }
            checked++;
            if (zxc_huf_nudge_code_lengths(freq, len, NULL, 0, cap)) {
                adopted_cnt++;
                if (!nudge_cost_matches_tree("Fuzz nudged", len, freq)) {
                    ok = 0;
                    break;
                }
            }
        }
        if (ok)
            printf("  [PASS] Fuzz: %d histograms cross-checked, %d nudges adopted\n", checked,
                   adopted_cnt);
    }

    free(buf);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Round-trip the shared-table (dictionary) Huffman section codec: encode with
 * external code lengths and NO 128-byte header, decode through a prebuilt
 * table -- the enc_lit == 3 wire path (FORMAT.md Sec 5.2.2). */
static int huf_dict_roundtrip_case(const char* label, const uint8_t* literals, size_t n) {
    uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
    for (size_t i = 0; i < n; i++) freq[literals[i]]++;

    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    if (zxc_huf_build_code_lengths(freq, code_len, NULL, ZXC_HUF_MAX_CODE_LEN_DENSITY) != ZXC_OK) {
        printf("Failed [%s]: build_code_lengths\n", label);
        return 0;
    }

    /* Round the lengths through the 128-byte packed form, as a .zxd does. */
    uint8_t packed[ZXC_HUF_TABLE_SIZE];
    uint8_t unpacked[ZXC_HUF_NUM_SYMBOLS];
    zxc_huf_pack_lengths(code_len, packed);
    if (zxc_huf_unpack_lengths(packed, unpacked) != ZXC_OK ||
        memcmp(code_len, unpacked, sizeof(code_len)) != 0) {
        printf("Failed [%s]: pack/unpack lengths roundtrip\n", label);
        return 0;
    }

    /* Tree-at-attach, as zxc_cctx_attach_dict_huf does. */
    zxc_pivco_tree_t tree;
    uint32_t codes[ZXC_HUF_NUM_SYMBOLS];
    uint8_t tree_len[ZXC_HUF_NUM_SYMBOLS];
    if (zxc_huf_dict_tree_build(packed, &tree, codes, tree_len) != ZXC_OK ||
        memcmp(tree_len, code_len, sizeof(tree_len)) != 0) {
        printf("Failed [%s]: dict_tree_build\n", label);
        return 0;
    }

    const size_t cap = ZXC_HUF_TABLE_SIZE + 2 * n + 4096;
    uint8_t* enc = (uint8_t*)malloc(cap);
    uint8_t* enc_blk = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(n + ZXC_PAD_SIZE);
    uint8_t* scr = (uint8_t*)malloc(n + ZXC_PIVCO_SCRATCH_PAD);
    if (!enc || !enc_blk || !dec || !scr) {
        printf("Failed [%s]: alloc\n", label);
        goto fail;
    }

    const int written =
        zxc_huf_encode_section_dict(literals, n, freq, code_len, &tree, codes, enc, cap);
    if (written < 0) {
        printf("Failed [%s]: encode_section_dict -> %d\n", label, written);
        goto fail;
    }

    /* Header-less variant of the size estimator (with_header = 0) must match. */
    if (zxc_huf_calc_size(freq, code_len, 0) != (size_t)written) {
        printf("Failed [%s]: dict calc_size != encoded\n", label);
        goto fail;
    }

    /* Same lengths, same bitstreams: the dict section must be exactly the
     * per-block section minus its 128-byte lengths header. */
    const int written_blk = zxc_huf_encode_section(literals, n, freq, code_len, enc_blk, cap);
    if (written_blk != written + (int)ZXC_HUF_TABLE_SIZE ||
        memcmp(enc, enc_blk + ZXC_HUF_TABLE_SIZE, (size_t)written) != 0) {
        printf("Failed [%s]: dict section != per-block section minus header\n", label);
        goto fail;
    }

    if (zxc_huf_decode_section_dict(enc, (size_t)written, dec, n, &tree, scr) != ZXC_OK ||
        memcmp(literals, dec, n) != 0) {
        printf("Failed [%s]: decode_section_dict roundtrip mismatch\n", label);
        goto fail;
    }

    /* Error paths: truncated payload, undersized dst_cap. */
    if (zxc_huf_decode_section_dict(enc, 0, dec, n, &tree, scr) == ZXC_OK) {
        printf("Failed [%s]: truncated payload accepted\n", label);
        goto fail;
    }
    if (zxc_huf_encode_section_dict(literals, n, freq, code_len, &tree, codes, enc, 4) !=
        ZXC_ERROR_DST_TOO_SMALL) {
        printf("Failed [%s]: undersized dst_cap not rejected\n", label);
        goto fail;
    }

    free(enc);
    free(enc_blk);
    free(dec);
    free(scr);
    printf("  [PASS] %s (n=%zu, encoded=%d B, header saved=%d B)\n", label, n, written,
           (int)ZXC_HUF_TABLE_SIZE);
    return 1;

fail:
    free(enc);
    free(enc_blk);
    free(dec);
    free(scr);
    return 0;
}

int test_huffman_codec_dict() {
    printf("=== TEST: Unit - Huffman Codec, shared dictionary table (enc_lit == 3) ===\n");

    const size_t N = 8192;
    uint8_t* buf = (uint8_t*)malloc(N);
    if (!buf) return 0;

    /* Skewed text-like distribution: the shared-table sweet spot. */
    for (size_t i = 0; i < N; i++)
        buf[i] = (zxc_test_rand() % 10 == 0) ? (uint8_t)(zxc_test_rand() & 0x7F) : 'A';
    if (!huf_dict_roundtrip_case("Skewed (90% 'A')", buf, N)) {
        free(buf);
        return 0;
    }

    /* Two-symbol alphabet: ~1 bit/symbol, headerless gain is maximal. */
    for (size_t i = 0; i < N; i++) buf[i] = (zxc_test_rand() & 1) ? 'X' : 'Y';
    if (!huf_dict_roundtrip_case("Two-symbol alphabet", buf, N)) {
        free(buf);
        return 0;
    }

    /* Small block: where the 128-byte header would dominate per-block cost. */
    for (size_t i = 0; i < ZXC_HUF_MIN_LITERALS; i++)
        buf[i] = (zxc_test_rand() % 4 == 0) ? (uint8_t)('a' + (zxc_test_rand() % 26)) : 'k';
    if (!huf_dict_roundtrip_case("Small block at threshold", buf, ZXC_HUF_MIN_LITERALS)) {
        free(buf);
        return 0;
    }

    /* A literal with NO code in the shared table must be refused by the
     * encoder (the validity check that triggers the per-block fallback). */
    {
        uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
        for (size_t i = 0; i < 256; i++) buf[i] = (zxc_test_rand() & 1) ? 'X' : 'Y';
        for (size_t i = 0; i < 256; i++) freq[buf[i]]++;
        uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
        if (zxc_huf_build_code_lengths(freq, code_len, NULL, ZXC_HUF_MAX_CODE_LEN_DENSITY) !=
            ZXC_OK) {
            free(buf);
            return 0;
        }
        buf[100] = '!'; /* unseen in training: no code assigned */
        freq['!']++;    /* keep the histogram in sync with the mutated buffer */
        uint8_t enc[1024];
        zxc_pivco_tree_t tree;
        uint32_t codes[ZXC_HUF_NUM_SYMBOLS];
        uint8_t packed[ZXC_HUF_TABLE_SIZE];
        uint8_t tree_len[ZXC_HUF_NUM_SYMBOLS];
        zxc_huf_pack_lengths(code_len, packed);
        if (zxc_huf_dict_tree_build(packed, &tree, codes, tree_len) != ZXC_OK) {
            printf("Failed: dict_tree_build (code-less literal case)\n");
            free(buf);
            return 0;
        }
        if (zxc_huf_encode_section_dict(buf, 256, freq, code_len, &tree, codes, enc, sizeof(enc)) !=
            ZXC_ERROR_CORRUPT_DATA) {
            printf("Failed: code-less literal not rejected by encode_section_dict\n");
            free(buf);
            return 0;
        }
        printf("  [PASS] code-less literal rejected (per-block fallback trigger)\n");
    }

    free(buf);
    printf("PASS\n\n");
    return 1;
}

/* Regression: a degenerate single-symbol table must carry code_len == 1
 * (FORMAT.md, decoder validation requirements). The v6 decoder rejected a
 * lone symbol with a longer length; the v7 rewrite briefly accepted it. */
int test_huffman_single_symbol_validation() {
    printf("=== TEST: Unit - Huffman single-symbol table validation ===\n");

    zxc_pivco_tree_t tree;
    uint32_t codes[ZXC_HUF_NUM_SYMBOLS];
    uint8_t tree_len[ZXC_HUF_NUM_SYMBOLS];
    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    uint8_t packed[ZXC_HUF_TABLE_SIZE];

    /* Lone symbol with code length 1: the only legal degenerate form. */
    memset(code_len, 0, sizeof(code_len));
    code_len['A'] = 1;
    zxc_huf_pack_lengths(code_len, packed);
    if (zxc_huf_dict_tree_build(packed, &tree, codes, tree_len) != ZXC_OK) {
        printf("Failed: single symbol with code_len=1 must be accepted\n");
        return 0;
    }
    printf("  [PASS] single symbol, code_len=1 accepted\n");

    /* Lone symbol with any longer length is declared corrupt by the format. */
    for (int len = 2; len <= ZXC_HUF_MAX_CODE_LEN_ULTRA; len++) {
        memset(code_len, 0, sizeof(code_len));
        code_len['A'] = (uint8_t)len;
        zxc_huf_pack_lengths(code_len, packed);
        if (zxc_huf_dict_tree_build(packed, &tree, codes, tree_len) != ZXC_ERROR_CORRUPT_DATA) {
            printf("Failed: single symbol with code_len=%d must be rejected\n", len);
            return 0;
        }
    }
    printf("  [PASS] single symbol, code_len 2..%d rejected\n", ZXC_HUF_MAX_CODE_LEN_ULTRA);

    printf("PASS\n\n");
    return 1;
}

// Checks that the EOF block is correctly appended
int test_eof_block_structure() {
    printf("=== TEST: Unit - EOF Block Structure ===\n");

    const char* input = "test";
    size_t src_size = 4;
    size_t max_dst_size = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = malloc(max_dst_size);
    if (!compressed) return 0;

    zxc_compress_opts_t _co26 = {.level = 1, .checksum_enabled = 0};
    int64_t comp_size = zxc_compress(input, src_size, compressed, max_dst_size, &_co26);
    if (comp_size <= 0) {
        printf("Failed: Compression returned 0\n");
        free(compressed);
        return 0;
    }

    // Validating Footer and EOF Block
    // Total Overhead: 8 bytes (Footer) + 8 bytes (EOF Header) = 16 bytes
    if (comp_size < 16) {
        printf("Failed: Compressed size too small for Footer + EOF (%lld)\n", (long long)comp_size);
        free(compressed);
        return 0;
    }

    // 1. Verify 8-byte Footer: [SrcSize (8)]
    const uint8_t* footer_ptr = compressed + comp_size - ZXC_FILE_FOOTER_SIZE;
    if (zxc_le64(footer_ptr) != 4) {
        printf("Failed: Footer mismatch. Src: %llu\n", (unsigned long long)zxc_le64(footer_ptr));
        free(compressed);
        return 0;
    }

    // 2. Verify EOF Block Header (8 bytes)
    // Should be immediately before the footer
    const uint8_t* eof_ptr = compressed + comp_size - ZXC_FILE_FOOTER_SIZE - ZXC_BLOCK_HEADER_SIZE;
    uint8_t expected[8] = {0xFF, 0, 0, 0, 0, 0, 0, 0};
    expected[7] = zxc_hash8(expected);

    if (memcmp(eof_ptr, expected, 8) != 0) {
        printf(
            "Failed: EOF block mismatch.\nExpected: %02X %02X %02X ... %02X\nGot:      %02X %02X "
            "%02X ... %02X\n",
            expected[0], expected[1], expected[2], expected[7], eof_ptr[0], eof_ptr[1], eof_ptr[2],
            eof_ptr[7]);
        free(compressed);
        return 0;
    }

    printf("PASS\n\n");
    free(compressed);
    return 1;
}

int test_header_checksum() {
    printf("Running test_header_checksum...\n");

    uint8_t header_buf[ZXC_BLOCK_HEADER_SIZE];
    zxc_block_header_t bh_in = {.block_type = ZXC_BLOCK_GLO,
                                .block_flags = 0,
                                .reserved = 0,
                                .header_checksum = 0,
                                .comp_size = 1024};

    // 1. Write Header
    if (zxc_write_block_header(header_buf, ZXC_BLOCK_HEADER_SIZE, &bh_in) !=
        ZXC_BLOCK_HEADER_SIZE) {
        printf("  [FAIL] zxc_write_block_header failed\n");
        return 0;
    }

    // Verify manually that checksum byte is non-zero (highly likely)
    if (header_buf[7] == 0) {
        // It's technically possible but very unlikely with a good hash
        printf("  [WARN] Checksum is 0 (unlikely but possible)\n");
    }

    // 2. Read Header (Valid)
    zxc_block_header_t bh_out;
    if (zxc_read_block_header(header_buf, ZXC_BLOCK_HEADER_SIZE, &bh_out) != 0) {
        printf("  [FAIL] zxc_read_block_header failed on valid input\n");
        return 0;
    }

    if (bh_out.block_type != bh_in.block_type || bh_out.comp_size != bh_in.comp_size ||
        bh_out.header_checksum != header_buf[7]) {
        printf("  [FAIL] Read data mismatch\n");
        return 0;
    }

    // 3. Corrupt Header Checksum
    uint8_t original_checksum = header_buf[7];
    header_buf[7] = ~original_checksum;  // Flip bits
    if (zxc_read_block_header(header_buf, ZXC_BLOCK_HEADER_SIZE, &bh_out) == 0) {
        printf("  [FAIL] zxc_read_block_header should have failed on corrupted checksum\n");
        return 0;
    }
    header_buf[7] = original_checksum;  // Restore

    // 4. Corrupt Header Content
    header_buf[0] = ZXC_BLOCK_RAW;  // Change type
    if (zxc_read_block_header(header_buf, ZXC_BLOCK_HEADER_SIZE, &bh_out) == 0) {
        printf("  [FAIL] zxc_read_block_header should have failed on corrupted content\n");
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

// 5. Two blocks swapped in a stream-written archive: position-seeded checksums refuse it.
int test_swapped_blocks_stream() {
    printf("TEST: Swapped blocks, stream reader... ");

    // 1. Create input data withDISTINCT patterns for 2 blocks (so blocks are different)
    // ZXC_BLOCK_SIZE_DEFAULT is 256KB. We need > 256KB. Let's use 600KB.
    size_t input_sz = 600 * 1024;
    uint8_t* val_buf = malloc(input_sz);
    if (!val_buf) return 0;

    // Fill Block 1 with 0xAA, Block 2 with 0xBB, Block 3 with 0xCC...
    memset(val_buf, 0xAA, 256 * 1024);
    memset(val_buf + 256 * 1024, 0xBB, 256 * 1024);
    memset(val_buf + 512 * 1024, 0xCC, input_sz - 512 * 1024);

    FILE* f_in = tmpfile();
    FILE* f_comp = tmpfile();
    fwrite(val_buf, 1, input_sz, f_in);
    rewind(f_in);

    // 2. Compress with Checksum Enabled
    zxc_compress_opts_t _sco27 = {.n_threads = 1, .level = 1, .checksum_enabled = 1};
    zxc_stream_compress(f_in, f_comp, &_sco27);

    // 3. Read compressed data to memory
    long comp_sz = ftell(f_comp);
    rewind(f_comp);
    uint8_t* comp_buf = malloc((size_t)comp_sz);
    if (fread(comp_buf, 1, comp_sz, f_comp) != (size_t)comp_sz) {
        printf("[FAIL] Failed to read compressed data\n");
        free(val_buf);
        free(comp_buf);
        fclose(f_in);
        fclose(f_comp);
        return 0;
    }

    // 4. Parse Blocks to identify Block 1 and Block 2
    // File Header: ZXC_FILE_HEADER_SIZE bytes
    size_t off1 = ZXC_FILE_HEADER_SIZE;
    // Parse Block 1 Header
    zxc_block_header_t bh1;
    zxc_read_block_header(comp_buf + off1, ZXC_BLOCK_HEADER_SIZE, &bh1);
    size_t len1 = ZXC_BLOCK_HEADER_SIZE + bh1.comp_size + ZXC_BLOCK_CHECKSUM_SIZE;

    size_t off2 = off1 + len1;
    // Parse Block 2 Header
    zxc_block_header_t bh2;
    zxc_read_block_header(comp_buf + off2, ZXC_BLOCK_HEADER_SIZE, &bh2);
    size_t len2 = ZXC_BLOCK_HEADER_SIZE + bh2.comp_size + ZXC_BLOCK_CHECKSUM_SIZE;

    // Ensure we have at least 2 full blocks + EOF + footer
    if (off2 + len2 > (size_t)comp_sz) {
        printf("[FAIL] Compressed size too small for test\n");
        free(val_buf);
        free(comp_buf);
        fclose(f_in);
        fclose(f_comp);
        return 0;
    }

    // 5. Swap Block 1 and Block 2
    // To safely swap, we need a new buffer
    uint8_t* swapped_buf = malloc((size_t)comp_sz);

    // Copy File Header
    // Copy File Header
    memcpy(swapped_buf, comp_buf, ZXC_FILE_HEADER_SIZE);
    size_t w_off = ZXC_FILE_HEADER_SIZE;

    // Write Block 2 first
    memcpy(swapped_buf + w_off, comp_buf + off2, len2);
    w_off += len2;

    // Write Block 1 second
    memcpy(swapped_buf + w_off, comp_buf + off1, len1);
    w_off += len1;

    // Write remaining data (EOF block + footer)
    size_t remaining_off = off2 + len2;
    size_t remaining_len = comp_sz - remaining_off;
    memcpy(swapped_buf + w_off, comp_buf + remaining_off, remaining_len);

    // 6. Write to File for Decompression
    FILE* f_bad = tmpfile();
    fwrite(swapped_buf, 1, (size_t)comp_sz, f_bad);
    rewind(f_bad);

    // 7. Attempt Decompression
    FILE* f_out = tmpfile();
    zxc_decompress_opts_t _sdo28 = {.n_threads = 1, .checksum_enabled = 1};
    int64_t res = zxc_stream_decompress(f_bad, f_out, &_sdo28);

    fclose(f_in);
    fclose(f_comp);
    fclose(f_bad);
    fclose(f_out);
    free(val_buf);
    free(comp_buf);
    free(swapped_buf);

    if (res != ZXC_ERROR_BAD_CHECKSUM) {
        printf("  [FAIL] zxc_stream_decompress on swapped blocks -> %lld, want BAD_CHECKSUM\n",
               (long long)res);
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

/* Two blocks 32 apart swapped, through the one-shot reader: position-seeded checksums. */
int test_swapped_blocks_oneshot(void) {
    printf("TEST: Swapped blocks, one-shot reader... ");

    const size_t BLK = 4 * 1024;
    const size_t NBLK = 40; /* > 33 so blocks 1 and 33 both exist */
    const size_t in_sz = BLK * NBLK;
    uint8_t* src = malloc(in_sz);
    if (!src) return 0;
    /* Each block distinct, and incompressible so every block is RAW and its
     * physical size is predictable. */
    uint32_t rng = 0x51ED270Bu;
    for (size_t i = 0; i < in_sz; i++) {
        rng = rng * 1103515245u + 12345u;
        src[i] = (uint8_t)(rng >> 16);
    }

    const size_t cap = (size_t)zxc_compress_bound(in_sz) + 4096;
    uint8_t* comp = malloc(cap);
    uint8_t* swapped = malloc(cap);
    uint8_t* out = malloc(in_sz);
    int ok = 0;
    do {
        if (!comp || !swapped || !out) break;
        zxc_compress_opts_t co = {.level = 3, .block_size = BLK, .checksum_enabled = 1};
        const int64_t n = zxc_compress(src, in_sz, comp, cap, &co);
        if (n <= 0) {
            printf("[FAIL] compress -> %lld\n", (long long)n);
            break;
        }
        /* Offsets of every data block, walking the frame once. */
        size_t off[64], len[64], nb = 0;
        size_t p = ZXC_FILE_HEADER_SIZE;
        while (p + ZXC_BLOCK_HEADER_SIZE <= (size_t)n && nb < 64) {
            zxc_block_header_t bh;
            if (zxc_read_block_header(comp + p, (size_t)n - p, &bh) != ZXC_OK) break;
            if (bh.block_type == ZXC_BLOCK_EOF) break;
            off[nb] = p;
            len[nb] = ZXC_BLOCK_HEADER_SIZE + bh.comp_size + ZXC_BLOCK_CHECKSUM_SIZE;
            p += len[nb];
            nb++;
        }
        if (nb < 34) {
            printf("[FAIL] got %zu blocks, need >= 34\n", nb);
            break;
        }
        /* Swap blocks 1 and 33: same length (both full), so the frame layout
         * is untouched and only the order changes. */
        if (len[1] != len[33]) {
            printf("[FAIL] blocks 1 and 33 differ in size (%zu vs %zu)\n", len[1], len[33]);
            break;
        }
        memcpy(swapped, comp, (size_t)n);
        memcpy(swapped + off[1], comp + off[33], len[33]);
        memcpy(swapped + off[33], comp + off[1], len[1]);

        zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
        const int64_t intact = zxc_decompress(comp, (size_t)n, out, in_sz, &dopts);
        if (intact != (int64_t)in_sz || memcmp(out, src, in_sz) != 0) {
            printf("[FAIL] the intact archive -> %lld\n", (long long)intact);
            break;
        }
        const int64_t r = zxc_decompress(swapped, (size_t)n, out, in_sz, &dopts);
        if (r != ZXC_ERROR_BAD_CHECKSUM) {
            printf("[FAIL] a 32-apart swap -> %lld, want BAD_CHECKSUM\n", (long long)r);
            break;
        }
        ok = 1;
    } while (0);

    free(src);
    free(comp);
    free(swapped);
    free(out);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* When the header announces a seek table, the 8 bytes after the EOF block must be
 * its SEK header. The check the sequential readers share must return the table's
 * full 64-bit length, not the header field: past a 4 GiB table the field folds the
 * high half in. */
int test_seek_tail_rule(void) {
    printf("=== TEST: Format - SEK header check after EOF ===\n");
    enum { BS = 4096 };
    const uint64_t nblocks = (1ULL << 30) + 1; /* table past 4 GiB: its high half is 1 */
    const uint64_t table = zxc_seek_table_bytes(nblocks);
    const uint64_t total_out = nblocks * BS;
    uint8_t peek[ZXC_BLOCK_HEADER_SIZE];
    /* Built by the writer, so the rule is checked against what is emitted. */
    if (zxc_seek_table_header(peek, sizeof(peek), (uint32_t)nblocks) < 0) return 0;
    const uint32_t field = zxc_le32(peek + 3);
    const uint32_t fold = (uint32_t)(table ^ (table >> 32));
    if (field != fold) {
        printf("Failed: size field %u, want the fold %u, not the low half %u\n", field, fold,
               (uint32_t)table);
        return 0;
    }

    uint64_t got = 0;
    if (!zxc_seek_header_ok(peek, total_out, BS, &got) || got != table) {
        printf("Failed: SEK of %llu blocks: matched %d, length %llu, want %llu\n",
               (unsigned long long)nblocks, got != 0, (unsigned long long)got,
               (unsigned long long)table);
        return 0;
    }
    /* Off by one block: the header field no longer agrees. */
    if (zxc_seek_header_ok(peek, total_out - BS, BS, &got)) {
        printf("Failed: matched a SEK header for the wrong block count\n");
        return 0;
    }
    /* A lying flag: the footer's head where the SEK header should be. */
    uint8_t footer[ZXC_BLOCK_HEADER_SIZE];
    zxc_store_le64(footer, 10);
    if (zxc_seek_header_ok(footer, 10, BS, &got)) {
        printf("Failed: a source size accepted as a SEK header\n");
        return 0;
    }
    printf("PASS\n\n");
    return 1;
}

/* Only the SEK block belongs between the EOF block and the footer (Sec 5.5).
 * Both frame decoders read the footer from the end, so inserted bytes used to
 * ride along and still report success: size and digest cover the decoded bytes,
 * not the gap. */
int test_tail_between_eof_and_footer(void) {
    printf("=== TEST: Format - only a SEK block may sit before the footer ===\n");
    const size_t n = 4096;
    uint8_t* const src = malloc(n);
    const size_t cap = (size_t)zxc_compress_bound(n) + 128;
    uint8_t* const arc = malloc(cap);
    uint8_t* const mod = malloc(cap + 128);
    uint8_t* const out = malloc(n);
    if (!src || !arc || !mod || !out) {
        free(src);
        free(arc);
        free(mod);
        free(out);
        return 0;
    }
    for (size_t i = 0; i < n; i++) src[i] = (uint8_t)(i * 7u);

    int ok = 1;
    const zxc_decompress_opts_t verify = {.checksum_enabled = 1};
    /* Checksummed (16-byte footer) and plain (8-byte) both. */
    for (int cs = 0; cs <= 1 && ok; cs++) {
        const zxc_compress_opts_t co = {.level = 3, .checksum_enabled = cs};
        const int64_t alen = zxc_compress(src, n, arc, cap, &co);
        const size_t footer_len =
            (size_t)ZXC_FILE_FOOTER_SIZE + (cs ? (size_t)ZXC_FILE_DIGEST_SIZE : 0);
        if (alen <= (int64_t)footer_len) {
            printf("  [FAIL] cs=%d: compress returned %lld\n", cs, (long long)alen);
            ok = 0;
            break;
        }
        if (zxc_decompress(arc, (size_t)alen, out, n, &verify) != (int64_t)n) {
            printf("  [FAIL] cs=%d: the intact archive must decode\n", cs);
            ok = 0;
            break;
        }
        const size_t head = (size_t)alen - footer_len;
        const size_t gaps[] = {1, 8, 64};
        for (size_t g = 0; g < sizeof(gaps) / sizeof(gaps[0]) && ok; g++) {
            memcpy(mod, arc, head);
            memset(mod + head, 0xAB, gaps[g]);
            memcpy(mod + head + gaps[g], arc + head, footer_len);
            const int64_t r = zxc_decompress(mod, head + gaps[g] + footer_len, out, n, &verify);
            if (r != ZXC_ERROR_CORRUPT_DATA) {
                printf("  [FAIL] cs=%d: %zu inserted bytes gave %lld, want %d\n", cs, gaps[g],
                       (long long)r, ZXC_ERROR_CORRUPT_DATA);
                ok = 0;
            }
        }
    }

    /* The legitimate gap: a seekable archive carries its SEK block there. */
    if (ok) {
        const zxc_compress_opts_t so = {.level = 3, .checksum_enabled = 1, .seekable = 1};
        const int64_t slen = zxc_compress(src, n, arc, cap, &so);
        if (slen <= 0 || zxc_decompress(arc, (size_t)slen, out, n, &verify) != (int64_t)n) {
            printf("  [FAIL] a seekable archive must still decode (len %lld)\n", (long long)slen);
            ok = 0;
        }
    }

    free(src);
    free(arc);
    free(mod);
    free(out);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* The four sequential readers on one archive: 1 if all decode @p n bytes, 0 if
 * all refuse, -1 (printed) if they disagree. */
static int seek_flag_verdict(const uint8_t* arc, const size_t len, const size_t n, uint8_t* out,
                             const char* what) {
    const zxc_decompress_opts_t o = {.checksum_enabled = 1};
    int acc[4] = {0, 0, 0, 0};

    acc[0] = zxc_decompress(arc, len, out, n + 64, &o) == (int64_t)n;

    zxc_dctx* const d = zxc_create_dctx();
    acc[1] = d && zxc_decompress_dctx(d, arc, len, out, n + 64, &o) == (int64_t)n;
    zxc_free_dctx(d);

    FILE* const f = tmpfile();
    if (f && fwrite(arc, 1, len, f) == len && fseek(f, 0, SEEK_SET) == 0)
        acc[2] = zxc_stream_decompress(f, NULL, &o) == (int64_t)n;
    if (f) fclose(f);

    zxc_dstream* const ds = zxc_dstream_create(&o);
    if (ds) {
        zxc_inbuf_t ib = {arc, len, 0};
        zxc_outbuf_t ob = {out, n + 64, 0};
        acc[3] =
            zxc_dstream_decompress(ds, &ob, &ib) >= 0 && zxc_dstream_finished(ds) && ob.pos == n;
    }
    zxc_dstream_free(ds);

    if (acc[0] == acc[1] && acc[1] == acc[2] && acc[2] == acc[3]) return acc[0];
    printf("  [FAIL] %s: readers disagree (buffer %d, dctx %d, FILE* %d, push %d)\n", what, acc[0],
           acc[1], acc[2], acc[3]);
    return -1;
}

/* HAS_SEEK_TABLE announces the SEK block (Sec 3.1, 5.5), and every reader holds
 * the tail to it: a flag set over no table, or clear over one, is refused by the
 * four sequential readers and zxc_seekable_open alike. Writers set it for what
 * they write, not for what was asked: the context API ignores seekable. An empty
 * seekable archive carries an empty table and opens with 0 blocks. */
int test_seek_flag_contract(void) {
    printf("=== TEST: Format - HAS_SEEK_TABLE matches the tail on every reader ===\n");
    const size_t n = 3 * 4096 + 7;
    const size_t cap = (size_t)zxc_compress_bound(n);
    uint8_t* const src = malloc(n);
    uint8_t* const plain = malloc(cap);
    uint8_t* const seek = malloc(cap);
    uint8_t* const lie = malloc(cap);
    uint8_t* const out = malloc(n + 64);
    int ok = src && plain && seek && lie && out;
    if (ok) gen_lz_data(src, n);

    for (int cs = 0; cs <= 1 && ok; cs++) {
        const zxc_compress_opts_t po = {.level = 3, .block_size = 4096, .checksum_enabled = cs};
        zxc_compress_opts_t so = po;
        so.seekable = 1;
        const int64_t pl = zxc_compress(src, n, plain, cap, &po);
        const int64_t sl = zxc_compress(src, n, seek, cap, &so);
        if (pl <= 0 || sl <= 0 || (plain[6] & ZXC_FILE_FLAG_HAS_SEEK_TABLE) ||
            !(seek[6] & ZXC_FILE_FLAG_HAS_SEEK_TABLE)) {
            printf("  [FAIL] cs=%d: flag does not follow the seekable option\n", cs);
            ok = 0;
            break;
        }
        zxc_seekable* s = zxc_seekable_open(seek, (size_t)sl);
        ok = seek_flag_verdict(plain, (size_t)pl, n, out, "plain") == 1 &&
             seek_flag_verdict(seek, (size_t)sl, n, out, "seekable") == 1 && s &&
             !zxc_seekable_open(plain, (size_t)pl);
        zxc_seekable_free(s);
        if (!ok) {
            printf("  [FAIL] cs=%d: an honest archive was refused\n", cs);
            break;
        }

        // Flag set over no table.
        memcpy(lie, plain, (size_t)pl);
        lie[6] |= ZXC_FILE_FLAG_HAS_SEEK_TABLE;
        zxc_file_header_sign(lie);
        s = zxc_seekable_open(lie, (size_t)pl);
        ok = seek_flag_verdict(lie, (size_t)pl, n, out, "flag without table") == 0 && !s;
        zxc_seekable_free(s);

        // Flag clear over a table.
        memcpy(lie, seek, (size_t)sl);
        lie[6] &= (uint8_t)~ZXC_FILE_FLAG_HAS_SEEK_TABLE;
        zxc_file_header_sign(lie);
        s = zxc_seekable_open(lie, (size_t)sl);
        ok = ok && seek_flag_verdict(lie, (size_t)sl, n, out, "table without flag") == 0 && !s;
        zxc_seekable_free(s);
        if (!ok) printf("  [FAIL] cs=%d: a lying flag was accepted somewhere\n", cs);
    }

    // Empty source: the flag still promises a table, an empty one. The FILE*
    // writer cannot know the input is empty when it writes the header, so both
    // writers must agree on these bytes. It opens with 0 blocks, from memory and
    // from a file alike, and only the empty range lies inside it.
    if (ok) {
        const zxc_compress_opts_t so = {.level = 3, .block_size = 4096, .seekable = 1};
        const int64_t el = zxc_compress(NULL, 0, seek, cap, &so);
        const size_t want = ZXC_FILE_HEADER_SIZE + 2 * ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE;
        FILE* const fi = tmpfile();
        FILE* const fo = tmpfile();
        int64_t fl = -1;
        if (fi && fo) fl = zxc_stream_compress(fi, fo, &so);
        const int same = fl == el && fo && fseek(fo, 0, SEEK_SET) == 0 &&
                         fread(lie, 1, (size_t)(fl > 0 ? fl : 0), fo) == (size_t)fl &&
                         memcmp(lie, seek, (size_t)el) == 0;
        zxc_seekable* const sm = zxc_seekable_open(seek, (size_t)el);
        zxc_seekable* const sf = fo ? zxc_seekable_open_file(fo) : NULL;
        int opens = sm && sf;
        for (int k = 0; k < 2 && opens; k++) {
            zxc_seekable* const h = k ? sf : sm;
            opens = zxc_seekable_get_num_blocks(h) == 0 &&
                    zxc_seekable_get_decompressed_size(h) == 0 &&
                    zxc_seekable_decompress_range(h, out, n, 0, 0) == 0 &&
                    zxc_seekable_decompress_range(h, out, n, 0, 1) == ZXC_ERROR_SRC_TOO_SMALL &&
                    zxc_seekable_decompress_range_mt(h, out, n, 0, 1, 2) == ZXC_ERROR_SRC_TOO_SMALL;
        }
        zxc_seekable_free(sm);
        zxc_seekable_free(sf);
        if (fi) fclose(fi);
        if (fo) fclose(fo);
        ok = el == (int64_t)want && (seek[6] & ZXC_FILE_FLAG_HAS_SEEK_TABLE) && same &&
             seek_flag_verdict(seek, (size_t)el, 0, out, "empty seekable") == 1 && opens;
        if (!ok)
            printf(
                "  [FAIL] empty seekable: %lld bytes (want %zu), FILE* writer %lld, same %d, "
                "opens with 0 blocks %d\n",
                (long long)el, want, (long long)fl, same, opens);
    }

    // The context API ignores seekable, so its header must not promise a table.
    if (ok) {
        const zxc_compress_opts_t so = {.level = 3, .block_size = 4096, .seekable = 1};
        zxc_cctx* const c = zxc_create_cctx(&so);
        const int64_t cl = c ? zxc_compress_cctx(c, src, n, lie, cap, &so) : -1;
        zxc_free_cctx(c);
        ok = cl > 0 && !(lie[6] & ZXC_FILE_FLAG_HAS_SEEK_TABLE) &&
             seek_flag_verdict(lie, (size_t)cl, n, out, "cctx") == 1;
        if (!ok) printf("  [FAIL] cctx: header promises a table it does not write\n");
    }

    free(src);
    free(plain);
    free(seek);
    free(lie);
    free(out);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_footer_digest(void) {
    printf("TEST: Footer digest... ");
    enum { N = 40 * 1024 };
    uint8_t* src = malloc(N);
    if (!src) return 0;
    uint32_t rng = 0x2545F491u;
    for (size_t i = 0; i < N; i++) {
        rng = rng * 1103515245u + 12345u;
        src[i] = (uint8_t)(rng >> 16);
    }
    const size_t cap = (size_t)zxc_compress_bound(N);
    uint8_t* a = malloc(cap);
    uint8_t* b = malloc(cap);
    uint8_t* out = malloc(N);
    int ok = 0;
    do {
        if (!a || !b || !out) break;
        const zxc_compress_opts_t co = {.level = 3, .block_size = 4096, .checksum_enabled = 1};
        const int64_t ca = zxc_compress(src, N, a, cap, &co);
        const int64_t cb = zxc_compress(src, N, b, cap, &co);
        if (ca <= 0 || cb <= 0) {
            printf("[FAIL] compress\n");
            break;
        }
        /* Deterministic: two -C compressions of the same bytes are identical. */
        if (ca != cb || memcmp(a, b, (size_t)ca) != 0) {
            printf("[FAIL] non-deterministic -C output\n");
            break;
        }
        const uint64_t digest = zxc_le64(a + ca - ZXC_FILE_DIGEST_SIZE);
        if (digest == 0) {
            printf("[FAIL] digest is zero on a non-empty archive\n");
            break;
        }
        /* A verified decode accepts it; flipping a digest byte fails BAD_CHECKSUM;
         * an unverified decode ignores it. */
        const zxc_decompress_opts_t verify = {.checksum_enabled = 1};
        const zxc_decompress_opts_t quiet = {.checksum_enabled = 0};
        if (zxc_decompress(a, (size_t)ca, out, N, &verify) != N) {
            printf("[FAIL] verified decode of intact archive\n");
            break;
        }
        a[ca - ZXC_FILE_DIGEST_SIZE] ^= 0xFF;
        if (zxc_decompress(a, (size_t)ca, out, N, &verify) != ZXC_ERROR_BAD_CHECKSUM) {
            printf("[FAIL] flipped digest not caught\n");
            break;
        }
        if (zxc_decompress(a, (size_t)ca, out, N, &quiet) != N) {
            printf("[FAIL] unverified decode should ignore the digest\n");
            break;
        }
        /* Empty -C archive: digest of zero blocks is 0, footer is 16 bytes. */
        uint8_t e[64];
        const int64_t ce = zxc_compress(NULL, 0, e, sizeof(e), &co);
        if (ce <=
                (int64_t)(ZXC_FILE_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE + ZXC_FILE_DIGEST_SIZE) - 1 ||
            zxc_le64(e + ce - ZXC_FILE_DIGEST_SIZE) != 0) {
            printf("[FAIL] empty -C archive digest\n");
            break;
        }
        ok = 1;
    } while (0);
    free(src);
    free(a);
    free(b);
    free(out);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* Builds a header with the given chunk-size code, fixes the header checksum, and returns
 * zxc_read_file_header's verdict (block_size out via *bs). */
static int chunk_code_verdict(uint8_t code, size_t* bs) {
    uint8_t hdr[ZXC_FILE_HEADER_SIZE];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 0xF5;
    hdr[1] = 0x2E;
    hdr[2] = 0xB0;
    hdr[3] = 0x9C;                     // magic (LE)
    hdr[4] = ZXC_FILE_FORMAT_VERSION;  // version
    hdr[5] = code;                     // chunk-size code
    hdr[6] = 0;                        // flags: no checksum
    uint16_t sum = zxc_hash16(hdr);    // bytes 14-15 already 0
    hdr[14] = (uint8_t)(sum & 0xFF);
    hdr[15] = (uint8_t)(sum >> 8);
    int has_checksum = -1;
    *bs = 0;
    return zxc_read_file_header(hdr, sizeof(hdr), bs, &has_checksum, NULL, NULL);
}

int test_chunk_size_code() {
    printf("=== TEST: Chunk-size code validation ===\n");

    size_t bs = 0;

    // Valid exponent code 19 -> 512 KB.
    int rc = chunk_code_verdict(19, &bs);
    if (rc != ZXC_OK || bs != 512 * 1024) {
        printf("  [FAIL] code 19: rc=%d (%s), block_size=%zu\n", rc, zxc_error_name(rc), bs);
        return 0;
    }
    printf("  [PASS] Code 19 -> 512 KB\n");

    // Legacy code 64 was accepted in v5 (mapped to 256 KB) but is removed in v6.
    rc = chunk_code_verdict(64, &bs);
    if (rc != ZXC_ERROR_BAD_BLOCK_SIZE) {
        printf("  [FAIL] legacy code 64: expected %d, got %d\n", ZXC_ERROR_BAD_BLOCK_SIZE, rc);
        return 0;
    }
    printf("  [PASS] Legacy code 64 rejected (ZXC_ERROR_BAD_BLOCK_SIZE)\n");

    // Out-of-range code 99 -> rejected.
    rc = chunk_code_verdict(99, &bs);
    if (rc != ZXC_ERROR_BAD_BLOCK_SIZE) {
        printf("  [FAIL] code 99: expected %d, got %d\n", ZXC_ERROR_BAD_BLOCK_SIZE, rc);
        return 0;
    }
    printf("  [PASS] Code 99 rejected\n");

    printf("PASS\n\n");
    return 1;
}

/* Compresses a deterministic payload into a fresh buffer; caller frees both. */
static uint8_t* forge_build(size_t plain_sz, int compressible, size_t block_sz, int checksum,
                            uint8_t** out_plain, size_t* out_sz) {
    uint8_t* plain = (uint8_t*)malloc(plain_sz);
    if (!plain) return NULL;
    unsigned s = 99u;
    for (size_t i = 0; i < plain_sz; i++) {
        s = s * 1103515245u + 12345u;
        plain[i] = compressible ? (uint8_t)('a' + (i % 26)) : (uint8_t)(s >> 24);
    }
    const uint64_t bound = zxc_compress_bound(plain_sz);
    uint8_t* arc = (uint8_t*)malloc((size_t)bound);
    if (!arc) {
        free(plain);
        return NULL;
    }
    zxc_compress_opts_t o = {0};
    o.level = 3;
    o.block_size = block_sz;
    o.checksum_enabled = checksum;
    const int64_t n = zxc_compress(plain, plain_sz, arc, (size_t)bound, &o);
    if (n < 0) {
        free(plain);
        free(arc);
        return NULL;
    }
    *out_plain = plain;
    *out_sz = (size_t)n;
    return arc;
}

/* Rewrites block 0's comp_size, keeping the 8-bit header checksum valid. */
static int forge_comp_size(uint8_t* arc, size_t arc_sz, uint32_t comp_size) {
    const size_t rem = arc_sz - ZXC_FILE_HEADER_SIZE;
    zxc_block_header_t bh;
    if (zxc_read_block_header(arc + ZXC_FILE_HEADER_SIZE, rem, &bh) != ZXC_OK) {
        printf("  [FAIL] block 0 header unreadable\n");
        return 0;
    }
    bh.comp_size = comp_size;
    if (zxc_write_block_header(arc + ZXC_FILE_HEADER_SIZE, rem, &bh) != ZXC_BLOCK_HEADER_SIZE) {
        printf("  [FAIL] block 0 header not rewritten\n");
        return 0;
    }
    return 1;
}

/* Decodes through zxc_decompress (use_dctx == 0) or a reusable zxc_dctx: the
   two frame walks are separate code and must reject the same forgeries. */
static int64_t forge_decode_via(const uint8_t* arc, size_t arc_sz, size_t plain_sz, int checksum,
                                int use_dctx) {
    uint8_t* out = (uint8_t*)malloc(plain_sz);
    if (!out) return ZXC_ERROR_MEMORY;
    zxc_decompress_opts_t o = {0};
    o.checksum_enabled = checksum;
    int64_t r;
    if (use_dctx) {
        zxc_dctx* d = zxc_create_dctx();
        r = d ? zxc_decompress_dctx(d, arc, arc_sz, out, plain_sz, &o) : ZXC_ERROR_MEMORY;
        zxc_free_dctx(d);
    } else {
        r = zxc_decompress(arc, arc_sz, out, plain_sz, &o);
    }
    free(out);
    return r;
}

/*
 * A block header carries comp_size as a plain u32, and the header checksum
 * covers it, so a forged size is structurally valid. Two things must stop it:
 * comp_size may not exceed the file's block size, and the walk may not report
 * success without having reached the EOF block - a forged size can span it.
 */
int test_forged_block_comp_size() {
    printf("TEST: Forged block comp_size... ");
    const size_t block_sz = 4096;
    int ok = 1;

    for (int checksum = 0; checksum <= 1 && ok; checksum++) {
        const size_t trailer = checksum ? ZXC_BLOCK_CHECKSUM_SIZE : 0;
        uint8_t* plain = NULL;
        size_t arc_sz = 0;
        /* Incompressible: block 0 falls back to RAW with comp_size == block_sz,
           so the legal case sits exactly on the bound. */
        uint8_t* arc = forge_build(64 * 1024, 0, block_sz, checksum, &plain, &arc_sz);
        if (!arc) {
            printf("  [FAIL] setup\n");
            return 0;
        }
        const size_t plain_sz = 64 * 1024;
        uint8_t* forged = (uint8_t*)malloc(arc_sz);
        if (!forged) {
            free(arc);
            free(plain);
            printf("  [FAIL] setup\n");
            return 0;
        }

        zxc_block_header_t b0;
        if (zxc_read_block_header(arc + ZXC_FILE_HEADER_SIZE, arc_sz - ZXC_FILE_HEADER_SIZE, &b0) !=
            ZXC_OK) {
            printf("  [FAIL] block 0 header unreadable\n");
            ok = 0;
        }
        if (ok && (b0.block_type != ZXC_BLOCK_RAW || b0.comp_size != block_sz)) {
            printf("  [FAIL] expected a RAW block at the bound, got type=%u comp_size=%u\n",
                   b0.block_type, b0.comp_size);
            ok = 0;
        }
        for (int via = 0; ok && via <= 1; via++) {
            if (forge_decode_via(arc, arc_sz, plain_sz, checksum, via) != (int64_t)plain_sz) {
                printf("  [FAIL] intact archive rejected via %s\n",
                       via ? "dctx" : "zxc_decompress");
                ok = 0;
            }
        }

        if (ok) { /* One byte over the bound. */
            memcpy(forged, arc, arc_sz);
            ok = forge_comp_size(forged, arc_sz, (uint32_t)block_sz + 1u);
            for (int via = 0; ok && via <= 1; via++) {
                const int64_t r = forge_decode_via(forged, arc_sz, plain_sz, checksum, via);
                if (r != ZXC_ERROR_BAD_BLOCK_SIZE) {
                    printf("  [FAIL] comp_size = block_size + 1 via %s gave %lld\n",
                           via ? "dctx" : "zxc_decompress", (long long)r);
                    ok = 0;
                }
            }
        }

        if (ok) { /* Spans every remaining byte, EOF and footer included. */
            memcpy(forged, arc, arc_sz);
            ok = forge_comp_size(
                forged, arc_sz,
                (uint32_t)(arc_sz - ZXC_FILE_HEADER_SIZE - ZXC_BLOCK_HEADER_SIZE - trailer));
            for (int via = 0; ok && via <= 1; via++) {
                const int64_t r = forge_decode_via(forged, arc_sz, plain_sz, checksum, via);
                if (r != ZXC_ERROR_BAD_BLOCK_SIZE) {
                    printf("  [FAIL] swallowing comp_size via %s gave %lld\n",
                           via ? "dctx" : "zxc_decompress", (long long)r);
                    ok = 0;
                }
            }
        }
        free(forged);
        free(arc);
        free(plain);

        if (!ok) break;

        /* Compressible: the swallowing size stays under block_size, so only the
           EOF requirement can catch it. */
        const size_t small_sz = 40 * 1024;
        arc = forge_build(small_sz, 1, block_sz, checksum, &plain, &arc_sz);
        if (!arc) {
            printf("  [FAIL] setup\n");
            return 0;
        }
        const uint32_t swallow =
            (uint32_t)(arc_sz - ZXC_FILE_HEADER_SIZE - ZXC_BLOCK_HEADER_SIZE - trailer);
        if (swallow > block_sz) {
            printf("  [FAIL] archive too large to test the EOF path (%u)\n", swallow);
            ok = 0;
        }
        if (ok) {
            ok = forge_comp_size(arc, arc_sz, swallow);
            const int64_t want = checksum ? ZXC_ERROR_BAD_CHECKSUM : ZXC_ERROR_CORRUPT_DATA;
            for (int via = 0; ok && via <= 1; via++) {
                const int64_t r = forge_decode_via(arc, arc_sz, small_sz, checksum, via);
                if (r != want) {
                    printf("  [FAIL] skipped EOF via %s gave %lld, expected %lld\n",
                           via ? "dctx" : "zxc_decompress", (long long)r, (long long)want);
                    ok = 0;
                }
            }
        }
        free(arc);
        free(plain);
    }

    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Every protected bit must move its checksum. Both hashes are multiplicative:
 * a flipped bit shifts the result by an amount fixed by the constants and the
 * flip's sign, so the guarantee is re-derived here from the constants and a
 * constant that opens a hole fails the suite. Block header: every bit. File
 * header: every bit and every pair. */
int test_header_checksum_single_bit() {
    printf("=== TEST: every header bit moves its checksum ===\n");

    const int rounds = 4000;
    uint32_t seed = 0x5EED1234u;

    // Block header: bytes 0-6 are covered, byte 7 holds the checksum.
    for (int r = 0; r < rounds; r++) {
        uint8_t h[ZXC_BLOCK_HEADER_SIZE];
        for (size_t i = 0; i < sizeof(h); i++) {
            seed = seed * 1664525u + 1013904223u;
            h[i] = (uint8_t)(seed >> 24);
        }
        h[7] = 0;
        const uint8_t ref = zxc_hash8(h);
        for (int bit = 0; bit < 56; bit++) {
            h[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
            const uint8_t got = zxc_hash8(h);
            h[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
            if (got == ref) {
                printf("  [FAIL] hash8: flipping byte %d bit %d leaves 0x%02X\n", bit >> 3, bit & 7,
                       ref);
                return 0;
            }
        }
    }
    printf("  [PASS] hash8: all 56 bits, %d headers\n", rounds);

    // File header: bytes 0-13 are covered, bytes 14-15 hold the checksum.
    for (int r = 0; r < rounds; r++) {
        uint8_t h[ZXC_FILE_HEADER_SIZE];
        for (size_t i = 0; i < sizeof(h); i++) {
            seed = seed * 1664525u + 1013904223u;
            h[i] = (uint8_t)(seed >> 24);
        }
        h[14] = 0;
        h[15] = 0;
        const uint16_t ref = zxc_hash16(h);
        for (int bit = 0; bit < 112; bit++) {
            h[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
            const uint16_t got = zxc_hash16(h);
            h[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
            if (got == ref) {
                printf("  [FAIL] hash16: flipping byte %d bit %d leaves 0x%04X\n", bit >> 3,
                       bit & 7, ref);
                return 0;
            }
        }
    }
    printf("  [PASS] hash16: all 112 bits, %d headers\n", rounds);

    // Proof from the constants: bit k of the first half shifts the result by
    // +/- 2^k * P2 * P1, of the second half by +/- 2^k * P1. A flip can pass
    // only if the net shift's top halfword is 0x0000 or 0xFFFF.
    {
        uint64_t c[112];
        for (int k = 0; k < 64; k++) c[k] = ((1ULL << k) * ZXC_HASH_MULT64) * ZXC_HASH_GOLDEN64;
        for (int k = 0; k < 48; k++) c[64 + k] = (1ULL << k) * ZXC_HASH_GOLDEN64;
        for (int a = 0; a < 112; a++) {
            for (int b = a; b < 112; b++) { /* b == a: weight 1 */
                for (unsigned sg = 0; sg < 4u; sg++) {
                    if (b == a && sg > 1) break;
                    uint64_t d = (sg & 1u) ? 0 - c[a] : c[a];
                    if (b != a) d += (sg & 2u) ? 0 - c[b] : c[b];
                    const unsigned top = (unsigned)(d >> 48);
                    if (top == 0 || top == 0xFFFFu) {
                        printf("  [FAIL] hash16: bits %d,%d can cancel in the top halfword\n", a,
                               b);
                        return 0;
                    }
                }
            }
        }
    }
    printf("  [PASS] hash16: no 1- or 2-bit error can pass, proven from the constants\n");

    // The real function, every bit pair, on 8 random headers.
    for (int r = 0; r < 8; r++) {
        uint8_t h[ZXC_FILE_HEADER_SIZE];
        for (size_t i = 0; i < sizeof(h); i++) {
            seed = seed * 1664525u + 1013904223u;
            h[i] = (uint8_t)(seed >> 24);
        }
        h[14] = 0;
        h[15] = 0;
        const uint16_t ref = zxc_hash16(h);
        for (int a = 0; a < 112; a++) {
            h[a >> 3] ^= (uint8_t)(1u << (a & 7));
            for (int b = a + 1; b < 112; b++) {
                h[b >> 3] ^= (uint8_t)(1u << (b & 7));
                const int same = zxc_hash16(h) == ref;
                h[b >> 3] ^= (uint8_t)(1u << (b & 7));
                if (same) {
                    printf("  [FAIL] hash16: bits %d,%d flipped, checksum unchanged\n", a, b);
                    return 0;
                }
            }
            h[a >> 3] ^= (uint8_t)(1u << (a & 7));
        }
    }
    printf("  [PASS] hash16: weight 2 exhaustive on 8 headers\n");

    printf("PASS\n\n");
    return 1;
}
