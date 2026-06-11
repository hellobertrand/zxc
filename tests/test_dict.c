/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

#include "../include/zxc_dict.h"

/* Build a valid 128-byte packed lengths table from arbitrary content bytes
 * (the .zxd format requires one). */
static void build_test_huf_lengths(const void* data, size_t n,
                                   uint8_t out[ZXC_HUF_TABLE_SIZE]) {
    uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) freq[p[i]]++;
    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    zxc_huf_build_code_lengths(freq, code_len, NULL);
    zxc_huf_pack_lengths(code_len, out);
}

static void gen_dict_friendly_data(uint8_t* buf, size_t size, const uint8_t* dict,
                                   size_t dict_size) {
    for (size_t i = 0; i < size; i++) {
        if (i % 7 < 5 && dict_size > 5) {
            size_t off = (i * 31) % (dict_size - 5);
            buf[i] = dict[off + (i % 5)];
        } else {
            buf[i] = (uint8_t)(i ^ (i >> 8));
        }
    }
}

int test_dict_zxd_roundtrip(void) {
    printf("=== TEST: Dict - .zxd save/load roundtrip ===\n");

    const char* content = "hello dict content for testing zxd format!";
    const size_t content_size = strlen(content);

    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    build_test_huf_lengths(content, content_size, huf);

    size_t bound = zxc_dict_save_bound(content_size);
    uint8_t* zxd = (uint8_t*)malloc(bound);
    int64_t written = zxc_dict_save(content, content_size, huf, zxd, bound);
    if (written < 0) {
        printf("  [FAIL] zxc_dict_save returned %lld\n", (long long)written);
        free(zxd);
        return 0;
    }

    const void* loaded_content = NULL;
    size_t loaded_size = 0;
    const void* loaded_huf = NULL;
    uint32_t loaded_id = 0;
    int rc = zxc_dict_load(zxd, (size_t)written, &loaded_content, &loaded_size, &loaded_huf,
                           &loaded_id);
    if (rc != ZXC_OK) {
        printf("  [FAIL] zxc_dict_load returned %d (%s)\n", rc, zxc_error_name(rc));
        free(zxd);
        return 0;
    }

    if (loaded_size != content_size || memcmp(loaded_content, content, content_size) != 0) {
        printf("  [FAIL] content mismatch after load\n");
        free(zxd);
        return 0;
    }

    /* The id covers (content, table): nonzero, matches the stored header id,
     * and differs from the content-only id. */
    if (loaded_id == 0 || loaded_id != zxc_dict_get_id(zxd, (size_t)written) ||
        loaded_id == zxc_dict_id(content, content_size)) {
        printf("  [FAIL] dict_id binding incorrect: got %u\n", loaded_id);
        free(zxd);
        return 0;
    }

    /* The folded load and the standalone accessor must both return the exact
     * table bytes we stored, and agree with each other. */
    const void* huf_acc = zxc_dict_huf(zxd, (size_t)written);
    if (!loaded_huf || loaded_huf != huf_acc ||
        memcmp(loaded_huf, huf, ZXC_HUF_TABLE_SIZE) != 0) {
        printf("  [FAIL] dict_load table out-param / zxc_dict_huf mismatch\n");
        free(zxd);
        return 0;
    }

    free(zxd);
    printf("PASS\n\n");
    return 1;
}

int test_dict_id_deterministic(void) {
    printf("=== TEST: Dict - dict_id is deterministic ===\n");

    const char* data = "some repeatable dictionary content";
    size_t size = strlen(data);

    uint32_t id1 = zxc_dict_id(data, size);
    uint32_t id2 = zxc_dict_id(data, size);

    if (id1 != id2 || id1 == 0) {
        printf("  [FAIL] dict_id not deterministic or zero: %u vs %u\n", id1, id2);
        return 0;
    }

    uint32_t id_null = zxc_dict_id(NULL, 0);
    if (id_null != 0) {
        printf("  [FAIL] dict_id(NULL, 0) should be 0, got %u\n", id_null);
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

int test_dict_get_id_apis(void) {
    printf("=== TEST: Dict - zxc_get_dict_id / zxc_dict_get_id ===\n");

    const uint8_t dict[] = "dictionary content for get_id test";
    const size_t dict_size = sizeof(dict) - 1;
    const uint32_t expected_id = zxc_dict_id(dict, dict_size);

    /* Compress with dict and verify zxc_get_dict_id reads it back */
    const uint8_t src[] = "some data to compress with dict for id test purposes";
    const size_t src_size = sizeof(src) - 1;
    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);

    zxc_compress_opts_t copts = {.level = 1, .dict = dict, .dict_size = dict_size};
    int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
    if (comp_size <= 0) {
        printf("  [FAIL] compress returned %lld\n", (long long)comp_size);
        free(compressed);
        return 0;
    }

    uint32_t got_id = zxc_get_dict_id(compressed, (size_t)comp_size);
    if (got_id != expected_id) {
        printf("  [FAIL] zxc_get_dict_id: got 0x%08X, expected 0x%08X\n", got_id, expected_id);
        free(compressed);
        return 0;
    }
    printf("  [PASS] zxc_get_dict_id returns 0x%08X\n", got_id);

    /* Compress without dict: should return 0 */
    zxc_compress_opts_t copts2 = {.level = 1};
    int64_t comp2 = zxc_compress(src, src_size, compressed, comp_bound, &copts2);
    if (comp2 > 0 && zxc_get_dict_id(compressed, (size_t)comp2) != 0) {
        printf("  [FAIL] zxc_get_dict_id should return 0 for no-dict file\n");
        free(compressed);
        return 0;
    }
    printf("  [PASS] zxc_get_dict_id returns 0 for no-dict file\n");
    free(compressed);

    /* Save to .zxd and verify zxc_dict_get_id matches what load reports */
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    build_test_huf_lengths(dict, dict_size, huf);
    size_t zxd_bound = zxc_dict_save_bound(dict_size);
    uint8_t* zxd = (uint8_t*)malloc(zxd_bound);
    int64_t zxd_size = zxc_dict_save(dict, dict_size, huf, zxd, zxd_bound);
    if (zxd_size <= 0) {
        printf("  [FAIL] zxc_dict_save returned %lld\n", (long long)zxd_size);
        free(zxd);
        return 0;
    }

    const void* lc = NULL;
    size_t lcs = 0;
    uint32_t loaded_id = 0;
    uint32_t zxd_id = zxc_dict_get_id(zxd, (size_t)zxd_size);
    if (zxd_id == 0 ||
        zxc_dict_load(zxd, (size_t)zxd_size, &lc, &lcs, NULL, &loaded_id) != ZXC_OK ||
        loaded_id != zxd_id) {
        printf("  [FAIL] zxc_dict_get_id: got 0x%08X, load id 0x%08X\n", zxd_id, loaded_id);
        free(zxd);
        return 0;
    }
    printf("  [PASS] zxc_dict_get_id returns 0x%08X\n", zxd_id);

    /* Invalid buffer should return 0 */
    if (zxc_dict_get_id("bad", 3) != 0) {
        printf("  [FAIL] zxc_dict_get_id should return 0 for invalid buffer\n");
        free(zxd);
        return 0;
    }
    printf("  [PASS] zxc_dict_get_id returns 0 for invalid buffer\n");

    free(zxd);
    printf("PASS\n\n");
    return 1;
}

int test_dict_buffer_roundtrip(void) {
    printf("=== TEST: Dict - buffer API roundtrip (all levels) ===\n");

    const uint8_t dict_content[] =
        "The quick brown fox jumps over the lazy dog. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Pack my box with five dozen liquor jugs. "
        "How vexingly quick daft zebras jump!";
    const size_t dict_size = sizeof(dict_content) - 1;

    const size_t src_size = 4096;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, dict_content, dict_size);

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);
    uint8_t* decompressed = (uint8_t*)malloc(src_size);

    for (int level = 1; level <= 6; level++) {
        zxc_compress_opts_t copts = {
            .level = level,
            .checksum_enabled = 1,
            .dict = dict_content,
            .dict_size = dict_size,
        };
        int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
        if (comp_size <= 0) {
            printf("  [FAIL] level %d: compress returned %lld\n", level, (long long)comp_size);
            free(src);
            free(compressed);
            free(decompressed);
            return 0;
        }

        zxc_decompress_opts_t dopts = {
            .checksum_enabled = 1,
            .dict = dict_content,
            .dict_size = dict_size,
        };
        int64_t dec_size = zxc_decompress(compressed, (size_t)comp_size, decompressed, src_size,
                                          &dopts);
        if (dec_size != (int64_t)src_size) {
            printf("  [FAIL] level %d: decompress returned %lld, expected %zu\n", level,
                   (long long)dec_size, src_size);
            free(src);
            free(compressed);
            free(decompressed);
            return 0;
        }

        if (memcmp(src, decompressed, src_size) != 0) {
            printf("  [FAIL] level %d: content mismatch\n", level);
            free(src);
            free(compressed);
            free(decompressed);
            return 0;
        }
        printf("  [PASS] level %d: %zu -> %lld bytes\n", level, src_size, (long long)comp_size);
    }

    free(src);
    free(compressed);
    free(decompressed);
    printf("PASS\n\n");
    return 1;
}

int test_dict_block_roundtrip(void) {
    printf("=== TEST: Dict - block API roundtrip (all levels) ===\n");

    const uint8_t dict_content[] =
        "The quick brown fox jumps over the lazy dog. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Pack my box with five dozen liquor jugs.";
    const size_t dict_size = sizeof(dict_content) - 1;

    const size_t src_size = 4096;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, dict_content, dict_size);

    const size_t comp_bound = (size_t)zxc_compress_block_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);
    uint8_t* decompressed = (uint8_t*)malloc(src_size);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_dctx* dctx = zxc_create_dctx();

    int result = 0;
    if (!src || !compressed || !decompressed || !cctx || !dctx) {
        printf("  [FAIL] allocation failed\n");
        goto cleanup;
    }

    for (int level = 1; level <= 6; level++) {
        zxc_compress_opts_t copts = {
            .level = level,
            .checksum_enabled = 1,
            .dict = dict_content,
            .dict_size = dict_size,
        };
        int64_t comp_size =
            zxc_compress_block(cctx, src, src_size, compressed, comp_bound, &copts);
        if (comp_size <= 0) {
            printf("  [FAIL] level %d: compress_block returned %lld\n", level,
                   (long long)comp_size);
            goto cleanup;
        }

        zxc_decompress_opts_t dopts = {
            .checksum_enabled = 1,
            .dict = dict_content,
            .dict_size = dict_size,
        };
        int64_t dec_size = zxc_decompress_block(dctx, compressed, (size_t)comp_size, decompressed,
                                                src_size, &dopts);
        if (dec_size != (int64_t)src_size || memcmp(src, decompressed, src_size) != 0) {
            printf("  [FAIL] level %d: block roundtrip mismatch (dec_size=%lld)\n", level,
                   (long long)dec_size);
            goto cleanup;
        }
        printf("  [PASS] level %d: %zu -> %lld bytes\n", level, src_size, (long long)comp_size);
    }

    result = 1;

cleanup:
    zxc_free_cctx(cctx); /* safe with NULL */
    zxc_free_dctx(dctx); /* safe with NULL */
    free(src);
    free(compressed);
    free(decompressed);
    if (result) printf("PASS\n\n");
    return result;
}

/* zxc_decompress_block_safe (exact-fit dst) must honor opts->dict; it used to
 * ignore it (BAD_OFFSET on dict back-refs). Real shuffled-pattern dict matches. */
int test_dict_block_safe_roundtrip(void) {
    printf("=== TEST: Dict - block_safe (strict-tail) with real dict back-refs ===\n");

    enum { NPAT = 256, PLEN = 40 };
    const size_t dict_size = (size_t)NPAT * PLEN;
    uint8_t* dict = (uint8_t*)malloc(dict_size);
    for (int i = 0; i < NPAT; i++) {
        uint32_t x = (uint32_t)i * 2654435761u;
        for (int j = 0; j < PLEN; j++) {
            x = x * 1103515245u + 12345u;
            dict[(size_t)i * PLEN + j] = (uint8_t)(x >> 16);
        }
    }
    int order[NPAT];
    for (int i = 0; i < NPAT; i++) order[i] = i;
    uint32_t s = 777u;
    for (int i = NPAT - 1; i > 0; i--) {
        s = s * 1103515245u + 12345u;
        int j = (int)(s % (uint32_t)(i + 1));
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
    const size_t src_size = (size_t)NPAT * (PLEN + 1);
    uint8_t* src = (uint8_t*)malloc(src_size);
    for (int k = 0; k < NPAT; k++) {
        memcpy(src + (size_t)k * (PLEN + 1), dict + (size_t)order[k] * PLEN, PLEN);
        src[(size_t)k * (PLEN + 1) + PLEN] = (uint8_t)k;
    }

    const size_t comp_bound = (size_t)zxc_compress_block_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);
    uint8_t* decompressed = (uint8_t*)malloc(src_size);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_dctx* dctx = zxc_create_dctx();

    int result = 0;
    if (!dict || !src || !compressed || !decompressed || !cctx || !dctx) {
        printf("  [FAIL] allocation failed\n");
        goto cleanup;
    }

    for (int level = 1; level <= 6; level++) {
        zxc_compress_opts_t copts = {.level = level, .dict = dict, .dict_size = dict_size};
        int64_t comp_size = zxc_compress_block(cctx, src, src_size, compressed, comp_bound, &copts);
        if (comp_size <= 0) {
            printf("  [FAIL] level %d: compress_block returned %lld (%s)\n", level,
                   (long long)comp_size, zxc_error_name((int)comp_size));
            goto cleanup;
        }
        /* Strict-tail decode: EXACT dst_capacity == src_size, WITH the dict. */
        zxc_decompress_opts_t dopts = {.dict = dict, .dict_size = dict_size};
        int64_t dec_size = zxc_decompress_block_safe(dctx, compressed, (size_t)comp_size,
                                                     decompressed, src_size, &dopts);
        if (dec_size != (int64_t)src_size || memcmp(src, decompressed, src_size) != 0) {
            printf("  [FAIL] level %d: dec_size=%lld err=%s\n", level, (long long)dec_size,
                   dec_size < 0 ? zxc_error_name((int)dec_size) : "content mismatch");
            goto cleanup;
        }
        printf("  [PASS] level %d (%lld -> %zu)\n", level, (long long)comp_size, src_size);
    }
    result = 1;

cleanup:
    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(dict);
    free(src);
    free(compressed);
    free(decompressed);
    if (result) printf("PASS\n\n");
    return result;
}

int test_dict_mismatch_error(void) {
    printf("=== TEST: Dict - dict_id mismatch error ===\n");

    const uint8_t dict[] = "correct dictionary content";
    const uint8_t wrong_dict[] = "wrong dictionary contentz";
    const size_t dict_size = sizeof(dict) - 1;

    const uint8_t src[] = "some data to compress with dict";
    const size_t src_size = sizeof(src) - 1;

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);

    zxc_compress_opts_t copts = {.level = 3, .dict = dict, .dict_size = dict_size};
    int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
    if (comp_size <= 0) {
        printf("  [FAIL] compress failed: %lld\n", (long long)comp_size);
        free(compressed);
        return 0;
    }

    uint8_t decompressed[256];
    zxc_decompress_opts_t dopts = {.dict = wrong_dict, .dict_size = sizeof(wrong_dict) - 1};
    int64_t rc = zxc_decompress(compressed, (size_t)comp_size, decompressed, sizeof(decompressed),
                                &dopts);
    if (rc != ZXC_ERROR_DICT_MISMATCH) {
        printf("  [FAIL] expected DICT_MISMATCH, got %lld (%s)\n", (long long)rc,
               zxc_error_name((int)rc));
        free(compressed);
        return 0;
    }

    free(compressed);
    printf("PASS\n\n");
    return 1;
}

int test_dict_required_error(void) {
    printf("=== TEST: Dict - dict required error ===\n");

    const uint8_t dict[] = "required dictionary";
    const size_t dict_size = sizeof(dict) - 1;

    const uint8_t src[] = "data needing a dict";
    const size_t src_size = sizeof(src) - 1;

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);

    zxc_compress_opts_t copts = {.level = 3, .dict = dict, .dict_size = dict_size};
    int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
    if (comp_size <= 0) {
        printf("  [FAIL] compress failed: %lld\n", (long long)comp_size);
        free(compressed);
        return 0;
    }

    uint8_t decompressed[256];
    zxc_decompress_opts_t dopts = {0};
    int64_t rc = zxc_decompress(compressed, (size_t)comp_size, decompressed, sizeof(decompressed),
                                &dopts);
    if (rc != ZXC_ERROR_DICT_REQUIRED) {
        printf("  [FAIL] expected DICT_REQUIRED, got %lld (%s)\n", (long long)rc,
               zxc_error_name((int)rc));
        free(compressed);
        return 0;
    }

    free(compressed);
    printf("PASS\n\n");
    return 1;
}

int test_dict_no_dict_compat(void) {
    printf("=== TEST: Dict - no-dict files decompress normally ===\n");

    const uint8_t src[] = "data compressed without any dictionary at all, just normal data";
    const size_t src_size = sizeof(src) - 1;

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);

    zxc_compress_opts_t copts = {.level = 3, .checksum_enabled = 1};
    int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
    if (comp_size <= 0) {
        printf("  [FAIL] compress failed\n");
        free(compressed);
        return 0;
    }

    uint8_t decompressed[256];
    zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
    int64_t dec_size = zxc_decompress(compressed, (size_t)comp_size, decompressed,
                                      sizeof(decompressed), &dopts);
    if (dec_size != (int64_t)src_size || memcmp(src, decompressed, src_size) != 0) {
        printf("  [FAIL] roundtrip without dict failed\n");
        free(compressed);
        return 0;
    }

    free(compressed);
    printf("PASS\n\n");
    return 1;
}

int test_dict_stream_roundtrip(void) {
    printf("=== TEST: Dict - stream API roundtrip ===\n");

    const uint8_t dict_content[] =
        "The quick brown fox jumps over the lazy dog. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
    const size_t dict_size = sizeof(dict_content) - 1;

    const size_t src_size = 8192;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, dict_content, dict_size);

    FILE* f_src = tmpfile();
    FILE* f_comp = tmpfile();
    FILE* f_dec = tmpfile();
    if (!f_src || !f_comp || !f_dec) {
        printf("  [FAIL] tmpfile() failed\n");
        free(src);
        return 0;
    }

    fwrite(src, 1, src_size, f_src);
    rewind(f_src);

    zxc_compress_opts_t copts = {
        .level = ZXC_LEVEL_DEFAULT,
        .checksum_enabled = 1,
        .dict = dict_content,
        .dict_size = dict_size,
    };
    int64_t comp_sz = zxc_stream_compress(f_src, f_comp, &copts);
    if (comp_sz <= 0) {
        printf("  [FAIL] stream_compress returned %lld\n", (long long)comp_sz);
        fclose(f_src);
        fclose(f_comp);
        fclose(f_dec);
        free(src);
        return 0;
    }

    rewind(f_comp);
    zxc_decompress_opts_t dopts = {
        .checksum_enabled = 1,
        .dict = dict_content,
        .dict_size = dict_size,
    };
    int64_t dec_sz = zxc_stream_decompress(f_comp, f_dec, &dopts);
    if (dec_sz != (int64_t)src_size) {
        printf("  [FAIL] stream_decompress returned %lld, expected %zu\n", (long long)dec_sz,
               src_size);
        fclose(f_src);
        fclose(f_comp);
        fclose(f_dec);
        free(src);
        return 0;
    }

    rewind(f_dec);
    uint8_t* result = (uint8_t*)malloc(src_size);
    const size_t rd = fread(result, 1, src_size, f_dec);
    int ok = (rd == src_size && memcmp(src, result, src_size) == 0);

    fclose(f_src);
    fclose(f_comp);
    fclose(f_dec);
    free(result);
    free(src);

    if (!ok) {
        printf("  [FAIL] content mismatch\n");
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

int test_dict_large_dict_roundtrip(void) {
    printf("=== TEST: Dict - large dict (32KB) with small blocks (4KB) ===\n");

    uint8_t* dict = (uint8_t*)malloc(32768);
    for (size_t i = 0; i < 32768; i++) dict[i] = (uint8_t)(i * 7 + 13);
    const size_t dict_size = 32768;

    const size_t src_size = 4096;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, dict, dict_size);

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);
    uint8_t* decompressed = (uint8_t*)malloc(src_size);

    for (int level = 1; level <= 6; level++) {
        zxc_compress_opts_t copts = {
            .level = level, .checksum_enabled = 1, .dict = dict, .dict_size = dict_size,
        };
        int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
        if (comp_size <= 0) {
            printf("  [FAIL] level %d: compress returned %lld (%s)\n", level, (long long)comp_size,
                   zxc_error_name((int)comp_size));
            free(src); free(compressed); free(decompressed); free(dict);
            return 0;
        }
        zxc_decompress_opts_t dopts = {.checksum_enabled = 1, .dict = dict, .dict_size = dict_size};
        int64_t dec_size = zxc_decompress(compressed, (size_t)comp_size, decompressed, src_size,
                                          &dopts);
        if (dec_size != (int64_t)src_size || memcmp(src, decompressed, src_size) != 0) {
            printf("  [FAIL] level %d: dec_size=%lld err=%s\n", level, (long long)dec_size,
                   dec_size < 0 ? zxc_error_name((int)dec_size) : "content mismatch");
            free(src); free(compressed); free(decompressed); free(dict);
            return 0;
        }
        printf("  [PASS] level %d\n", level);
    }

    free(src); free(compressed); free(decompressed); free(dict);
    printf("PASS\n\n");
    return 1;
}

/*
 * Regression test for the GLO SAFE-loop dictionary back-reference path.
 *
 * The other dict roundtrip tests use gen_dict_friendly_data(), which scatters
 * dict-derived BYTES but never forms long matches, so the decoder never emits a
 * dictionary back-reference. This test forges many small *matches* into the dict
 * (distinct 40-byte patterns in shuffled order, each separated by a literal so
 * they don't merge). Dict (10KB) + payload (10KB) stay under 64KB, so every
 * sequence is validated by the SAFE 4x loop's `off > written` check -- which is
 * only correct because `written` is seeded with dict_size. If that seeding were
 * dropped (written = 0), these dict back-refs would be wrongly rejected
 * (BAD_OFFSET). See project_dict_written_floor.
 */
int test_dict_safe_loop_backref(void) {
    printf("=== TEST: Dict - many small back-refs in the SAFE 4x loop ===\n");

    enum { NPAT = 256, PLEN = 40 };
    const size_t dict_size = (size_t)NPAT * PLEN;
    uint8_t* dict = (uint8_t*)malloc(dict_size);
    for (int i = 0; i < NPAT; i++) {
        uint32_t x = (uint32_t)i * 2654435761u;
        for (int j = 0; j < PLEN; j++) {
            x = x * 1103515245u + 12345u;
            dict[(size_t)i * PLEN + j] = (uint8_t)(x >> 16);
        }
    }

    int order[NPAT];
    for (int i = 0; i < NPAT; i++) order[i] = i;
    uint32_t s = 12345u;
    for (int i = NPAT - 1; i > 0; i--) {
        s = s * 1103515245u + 12345u;
        int j = (int)(s % (uint32_t)(i + 1));
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    const size_t src_size = (size_t)NPAT * (PLEN + 1);
    uint8_t* src = (uint8_t*)malloc(src_size);
    for (int k = 0; k < NPAT; k++) {
        memcpy(src + (size_t)k * (PLEN + 1), dict + (size_t)order[k] * PLEN, PLEN);
        src[(size_t)k * (PLEN + 1) + PLEN] = (uint8_t)k;  // literal separator (anti-merge)
    }

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);
    uint8_t* decompressed = (uint8_t*)malloc(src_size);

    int ok = 1;
    for (int level = 1; level <= 6 && ok; level++) {
        zxc_compress_opts_t copts = {.level = level, .dict = dict, .dict_size = dict_size};
        int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
        if (comp_size <= 0) {
            printf("  [FAIL] level %d: compress returned %lld (%s)\n", level, (long long)comp_size,
                   zxc_error_name((int)comp_size));
            ok = 0;
            break;
        }
        zxc_decompress_opts_t dopts = {.dict = dict, .dict_size = dict_size};
        int64_t dec_size =
            zxc_decompress(compressed, (size_t)comp_size, decompressed, src_size, &dopts);
        if (dec_size != (int64_t)src_size || memcmp(src, decompressed, src_size) != 0) {
            printf("  [FAIL] level %d: dec_size=%lld err=%s\n", level, (long long)dec_size,
                   dec_size < 0 ? zxc_error_name((int)dec_size) : "content mismatch");
            ok = 0;
            break;
        }
        printf("  [PASS] level %d (%lld -> %zu)\n", level, (long long)comp_size, src_size);
    }

    free(src);
    free(compressed);
    free(decompressed);
    free(dict);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_dict_train_roundtrip(void) {
    printf("=== TEST: Dict - train then compress/decompress ===\n");

    const char* json_samples[] = {
        "{\"id\":1,\"name\":\"alice\",\"email\":\"alice@example.com\",\"active\":true}",
        "{\"id\":2,\"name\":\"bob\",\"email\":\"bob@example.com\",\"active\":false}",
        "{\"id\":3,\"name\":\"carol\",\"email\":\"carol@example.com\",\"active\":true}",
        "{\"id\":4,\"name\":\"dave\",\"email\":\"dave@example.com\",\"active\":true}",
        "{\"id\":5,\"name\":\"eve\",\"email\":\"eve@example.com\",\"active\":false}",
        "{\"id\":6,\"name\":\"frank\",\"email\":\"frank@example.com\",\"active\":true}",
        "{\"id\":7,\"name\":\"grace\",\"email\":\"grace@example.com\",\"active\":false}",
        "{\"id\":8,\"name\":\"hank\",\"email\":\"hank@example.com\",\"active\":true}",
    };
    const size_t n_samples = sizeof(json_samples) / sizeof(json_samples[0]);
    const void* sample_ptrs[8];
    size_t sample_sizes[8];
    for (size_t i = 0; i < n_samples; i++) {
        sample_ptrs[i] = json_samples[i];
        sample_sizes[i] = strlen(json_samples[i]);
    }

    uint8_t dict_buf[4096];
    int64_t dict_sz =
        zxc_train_dict(sample_ptrs, sample_sizes, n_samples, dict_buf, sizeof(dict_buf));
    if (dict_sz <= 0) {
        printf("  [FAIL] train_dict returned %lld\n", (long long)dict_sz);
        return 0;
    }
    printf("  trained dict: %lld bytes\n", (long long)dict_sz);

    const char* test_input =
        "{\"id\":99,\"name\":\"zara\",\"email\":\"zara@example.com\",\"active\":true}";
    const size_t src_size = strlen(test_input);

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);

    zxc_compress_opts_t copts = {
        .level = ZXC_LEVEL_DEFAULT,
        .checksum_enabled = 1,
        .dict = dict_buf,
        .dict_size = (size_t)dict_sz,
    };
    int64_t comp_size = zxc_compress(test_input, src_size, compressed, comp_bound, &copts);
    if (comp_size <= 0) {
        printf("  [FAIL] compress returned %lld\n", (long long)comp_size);
        free(compressed);
        return 0;
    }

    zxc_compress_opts_t copts_nodict = {.level = ZXC_LEVEL_DEFAULT, .checksum_enabled = 1};
    uint8_t* comp_nodict = (uint8_t*)malloc(comp_bound);
    int64_t comp_nodict_sz =
        zxc_compress(test_input, src_size, comp_nodict, comp_bound, &copts_nodict);
    printf("  with dict: %lld bytes, without: %lld bytes (input: %zu)\n", (long long)comp_size,
           (long long)comp_nodict_sz, src_size);
    free(comp_nodict);

    uint8_t decompressed[256];
    zxc_decompress_opts_t dopts = {
        .checksum_enabled = 1,
        .dict = dict_buf,
        .dict_size = (size_t)dict_sz,
    };
    int64_t dec_size = zxc_decompress(compressed, (size_t)comp_size, decompressed,
                                      sizeof(decompressed), &dopts);
    free(compressed);

    if (dec_size != (int64_t)src_size || memcmp(test_input, decompressed, src_size) != 0) {
        printf("  [FAIL] roundtrip mismatch\n");
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

int test_dict_train_no_frequent_patterns(void) {
    printf("=== TEST: Dict - train fallback when no frequent k-grams ===\n");

    /* A strictly increasing byte sequence has all-distinct 5-grams, so no
     * k-gram repeats and the trainer finds zero scorable segments. This forces
     * the n_segs == 0 fallback: copy the tail of the corpus into the dict. */
    uint8_t corpus[64];
    for (size_t i = 0; i < sizeof(corpus); i++) corpus[i] = (uint8_t)i;

    const void* sample_ptrs[1] = {corpus};
    const size_t sample_sizes[1] = {sizeof(corpus)};

    /* Case 1: capacity >= corpus_size -> copy == corpus_size, dict == whole corpus. */
    uint8_t dict_big[256];
    int64_t sz = zxc_train_dict(sample_ptrs, sample_sizes, 1, dict_big, sizeof(dict_big));
    if (sz != (int64_t)sizeof(corpus)) {
        printf("  [FAIL] expected %zu bytes (full corpus), got %lld\n", sizeof(corpus),
               (long long)sz);
        return 0;
    }
    if (memcmp(dict_big, corpus, sizeof(corpus)) != 0) {
        printf("  [FAIL] dict content does not match corpus tail\n");
        return 0;
    }
    printf("  [PASS] full-corpus fallback (%lld bytes)\n", (long long)sz);

    /* Case 2: capacity < corpus_size -> copy == capacity, dict == last `cap` bytes. */
    const size_t cap = 16;
    uint8_t dict_small[16];
    sz = zxc_train_dict(sample_ptrs, sample_sizes, 1, dict_small, cap);
    if (sz != (int64_t)cap) {
        printf("  [FAIL] expected %zu bytes (capped), got %lld\n", cap, (long long)sz);
        return 0;
    }
    if (memcmp(dict_small, corpus + sizeof(corpus) - cap, cap) != 0) {
        printf("  [FAIL] capped dict does not match corpus tail\n");
        return 0;
    }
    printf("  [PASS] capped tail fallback (%lld bytes)\n", (long long)sz);

    printf("PASS\n\n");
    return 1;
}

int test_dict_seekable_roundtrip(void) {
    printf("=== TEST: Dict - seekable API roundtrip ===\n");

    const uint8_t dict_content[] =
        "The quick brown fox jumps over the lazy dog. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
    const size_t dict_size = sizeof(dict_content) - 1;

    const size_t src_size = 8192;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, dict_content, dict_size);

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);

    zxc_compress_opts_t copts = {
        .level = ZXC_LEVEL_DEFAULT,
        .checksum_enabled = 1,
        .seekable = 1,
        .dict = dict_content,
        .dict_size = dict_size,
    };
    int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
    if (comp_size <= 0) {
        printf("  [FAIL] compress returned %lld\n", (long long)comp_size);
        free(src);
        free(compressed);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(compressed, (size_t)comp_size);
    if (!s) {
        printf("  [FAIL] seekable_open returned NULL\n");
        free(src);
        free(compressed);
        return 0;
    }

    int rc = zxc_seekable_set_dict(s, dict_content, dict_size, NULL);
    if (rc != ZXC_OK) {
        printf("  [FAIL] seekable_set_dict returned %d\n", rc);
        zxc_seekable_free(s);
        free(src);
        free(compressed);
        return 0;
    }

    uint8_t* decompressed = (uint8_t*)malloc(src_size);
    int64_t dec_size = zxc_seekable_decompress_range(s, decompressed, src_size, 0, src_size);
    if (dec_size != (int64_t)src_size) {
        printf("  [FAIL] decompress_range returned %lld, expected %zu\n", (long long)dec_size,
               src_size);
        zxc_seekable_free(s);
        free(src);
        free(compressed);
        free(decompressed);
        return 0;
    }

    int ok = (memcmp(src, decompressed, src_size) == 0);
    zxc_seekable_free(s);
    free(decompressed);
    free(src);
    free(compressed);

    if (!ok) {
        printf("  [FAIL] content mismatch\n");
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

int test_dict_seekable_mt_roundtrip(void) {
    printf("=== TEST: Dict - seekable MT roundtrip ===\n");

    const uint8_t dict_content[] =
        "The quick brown fox jumps over the lazy dog. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
    const size_t dict_size = sizeof(dict_content) - 1;

    /* Use 32KB of data with 4KB blocks = 8 blocks, enough for MT */
    const size_t src_size = 32768;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, dict_content, dict_size);

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);

    zxc_compress_opts_t copts = {
        .level = ZXC_LEVEL_DEFAULT,
        .block_size = 4096,
        .checksum_enabled = 1,
        .seekable = 1,
        .dict = dict_content,
        .dict_size = dict_size,
    };
    int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
    if (comp_size <= 0) {
        printf("  [FAIL] compress returned %lld\n", (long long)comp_size);
        free(src);
        free(compressed);
        return 0;
    }

    zxc_seekable* s = zxc_seekable_open(compressed, (size_t)comp_size);
    if (!s) {
        printf("  [FAIL] seekable_open returned NULL\n");
        free(src);
        free(compressed);
        return 0;
    }
    zxc_seekable_set_dict(s, dict_content, dict_size, NULL);

    /* Full range MT decompress */
    uint8_t* decompressed = (uint8_t*)malloc(src_size);
    int64_t dec_size = zxc_seekable_decompress_range_mt(s, decompressed, src_size, 0, src_size, 4);
    if (dec_size != (int64_t)src_size) {
        printf("  [FAIL] decompress_range_mt returned %lld (%s)\n", (long long)dec_size,
               dec_size < 0 ? zxc_error_name((int)dec_size) : "size mismatch");
        zxc_seekable_free(s);
        free(src);
        free(compressed);
        free(decompressed);
        return 0;
    }

    int ok = (memcmp(src, decompressed, src_size) == 0);
    if (!ok) {
        for (size_t i = 0; i < src_size; i++) {
            if (src[i] != decompressed[i]) {
                printf("  [FAIL] content mismatch at byte %zu\n", i);
                break;
            }
        }
    }

    /* Also test a sub-range across block boundaries */
    if (ok) {
        int64_t sub = zxc_seekable_decompress_range_mt(s, decompressed, 8192, 4000, 8192, 4);
        ok = (sub == 8192 && memcmp(src + 4000, decompressed, 8192) == 0);
        if (!ok) printf("  [FAIL] sub-range MT mismatch\n");
    }

    zxc_seekable_free(s);
    free(decompressed);
    free(src);
    free(compressed);

    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

static const uint8_t k_dict_a[] =
    "The quick brown fox jumps over the lazy dog. "
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
static const uint8_t k_dict_b[] =
    "A completely unrelated dictionary payload hashing to a different dict_id value.";

// Compress `src` with dict A to a tmpfile, then try to stream-decompress it with
// `dec_dict` (NULL = none) and assert the decoder returns `want_err`.
static int stream_dict_error_case(const char* label, const uint8_t* dec_dict, size_t dec_dict_size,
                                  int64_t want_err) {
    const size_t src_size = 8192;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, k_dict_a, sizeof(k_dict_a) - 1);

    FILE* f_src = tmpfile();
    FILE* f_comp = tmpfile();
    FILE* f_dec = tmpfile();
    int ok = 1;
    if (!f_src || !f_comp || !f_dec) {
        printf("  [FAIL] %s: tmpfile() failed\n", label);
        ok = 0;
    }

    if (ok) {
        fwrite(src, 1, src_size, f_src);
        rewind(f_src);
        zxc_compress_opts_t copts = {.level = ZXC_LEVEL_DEFAULT,
                                     .checksum_enabled = 1,
                                     .dict = k_dict_a,
                                     .dict_size = sizeof(k_dict_a) - 1};
        if (zxc_stream_compress(f_src, f_comp, &copts) <= 0) {
            printf("  [FAIL] %s: stream_compress failed\n", label);
            ok = 0;
        }
    }

    if (ok) {
        rewind(f_comp);
        zxc_decompress_opts_t dopts = {
            .checksum_enabled = 1, .dict = dec_dict, .dict_size = dec_dict_size};
        int64_t rc = zxc_stream_decompress(f_comp, f_dec, &dopts);
        if (rc != want_err) {
            printf("  [FAIL] %s: expected %s, got %lld (%s)\n", label, zxc_error_name((int)want_err),
                   (long long)rc, zxc_error_name((int)rc));
            ok = 0;
        }
    }

    if (f_src) fclose(f_src);
    if (f_comp) fclose(f_comp);
    if (f_dec) fclose(f_dec);
    free(src);
    return ok;
}

int test_dict_stream_dict_id_checks(void) {
    printf("=== TEST: Dict - stream decode rejects missing/wrong dict ===\n");
    int ok = stream_dict_error_case("missing dict", NULL, 0, ZXC_ERROR_DICT_REQUIRED);
    ok &= stream_dict_error_case("wrong dict", k_dict_b, sizeof(k_dict_b) - 1,
                                 ZXC_ERROR_DICT_MISMATCH);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

int test_dict_seekable_dict_id_checks(void) {
    printf("=== TEST: Dict - seekable decode rejects missing/wrong dict ===\n");

    const size_t src_size = 8192;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, k_dict_a, sizeof(k_dict_a) - 1);

    size_t comp_bound = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = (uint8_t*)malloc(comp_bound);
    zxc_compress_opts_t copts = {.level = ZXC_LEVEL_DEFAULT,
                                 .checksum_enabled = 1,
                                 .seekable = 1,
                                 .dict = k_dict_a,
                                 .dict_size = sizeof(k_dict_a) - 1};
    int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);

    uint8_t* out = (uint8_t*)malloc(src_size);
    int ok = 1;

    if (comp_size <= 0) {
        printf("  [FAIL] seekable compress failed\n");
        ok = 0;
    }

    // 1. Wrong dict via set_dict must be rejected up front.
    if (ok) {
        zxc_seekable* s = zxc_seekable_open(compressed, (size_t)comp_size);
        if (!s) {
            printf("  [FAIL] seekable_open returned NULL\n");
            ok = 0;
        } else {
            int rc = zxc_seekable_set_dict(s, k_dict_b, sizeof(k_dict_b) - 1, NULL);
            if (rc != ZXC_ERROR_DICT_MISMATCH) {
                printf("  [FAIL] set_dict(wrong): expected DICT_MISMATCH, got %d (%s)\n", rc,
                       zxc_error_name(rc));
                ok = 0;
            }
            zxc_seekable_free(s);
        }
    }

    // 2. Decoding without any dict must be rejected, not silently corrupt
    //    (single-threaded and multi-threaded entry points).
    if (ok) {
        zxc_seekable* s = zxc_seekable_open(compressed, (size_t)comp_size);
        if (!s) {
            printf("  [FAIL] seekable_open returned NULL\n");
            ok = 0;
        } else {
            int64_t st = zxc_seekable_decompress_range(s, out, src_size, 0, src_size);
            int64_t mt = zxc_seekable_decompress_range_mt(s, out, src_size, 0, src_size, 4);
            if (st != ZXC_ERROR_DICT_REQUIRED || mt != ZXC_ERROR_DICT_REQUIRED) {
                printf("  [FAIL] no-dict decode: expected DICT_REQUIRED, got st=%lld mt=%lld\n",
                       (long long)st, (long long)mt);
                ok = 0;
            }
            zxc_seekable_free(s);
        }
    }

    // 3. Correct dict still works (guard against over-rejection).
    if (ok) {
        zxc_seekable* s = zxc_seekable_open(compressed, (size_t)comp_size);
        if (s && zxc_seekable_set_dict(s, k_dict_a, sizeof(k_dict_a) - 1, NULL) == ZXC_OK &&
            zxc_seekable_decompress_range(s, out, src_size, 0, src_size) == (int64_t)src_size &&
            memcmp(src, out, src_size) == 0) {
            // expected
        } else {
            printf("  [FAIL] correct dict roundtrip regressed\n");
            ok = 0;
        }
        zxc_seekable_free(s);
    }

    free(out);
    free(src);
    free(compressed);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* ---------------------------------------------------------------------------
 * Shared literal Huffman table (dict_huf)
 * ------------------------------------------------------------------------- */

/* Deterministic structured-text generator (LCG): dict-trainable patterns with
 * a skewed literal distribution so the shared table has something to win on. */
static uint32_t huf_lcg(uint32_t* s) {
    *s = *s * 1664525u + 1013904223u;
    return *s >> 16;
}

static size_t gen_structured_sample(uint8_t* buf, size_t cap, uint32_t seed) {
    static const char* actions[] = {"login", "logout", "refresh", "checkout"};
    static const char* services[] = {"auth-svc", "billing-svc", "gateway"};
    uint32_t s = seed;
    size_t n = 0;
    while (n + 96 < cap) {
        n += (size_t)snprintf((char*)buf + n, cap - n,
                              "ts=2026-06-10T12:%02u:%02u service=%s action=%s user=%u "
                              "latency_ms=%u status=%u\n",
                              huf_lcg(&s) % 60, huf_lcg(&s) % 60, services[huf_lcg(&s) % 3],
                              actions[huf_lcg(&s) % 4], huf_lcg(&s) % 100000, huf_lcg(&s) % 2000,
                              (huf_lcg(&s) % 5) ? 200u : 500u);
    }
    return n;
}

int test_dict_huf_zxd_roundtrip(void) {
    printf("=== TEST: Dict - .zxd create (one-call == primitives) / load / corruption ===\n");

    enum { NS = 6, SCAP = 16384 };
    uint8_t* bufs[NS];
    const void* samples[NS];
    size_t sizes[NS];
    for (int i = 0; i < NS; i++) {
        bufs[i] = (uint8_t*)malloc(SCAP);
        sizes[i] = gen_structured_sample(bufs[i], SCAP, 0x1000u + (uint32_t)i);
        samples[i] = bufs[i];
    }

    int ok = 0;
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    /* Same capacity as zxc_dict_train uses internally, so the primitive path
     * trains identical content and the byte-identity comparison is exact. */
    uint8_t* dict_buf = (uint8_t*)malloc(ZXC_DICT_SIZE_MAX);
    uint8_t* zxd = NULL;
    uint8_t* zxd_one = NULL;
    do {
        /* Primitive 3-step pipeline. */
        const int64_t dsz = zxc_train_dict(samples, sizes, NS, dict_buf, ZXC_DICT_SIZE_MAX);
        if (dsz <= 0) {
            printf("  [FAIL] train_dict: %lld\n", (long long)dsz);
            break;
        }
        const int hrc = zxc_train_dict_huf(samples, sizes, NS, dict_buf, (size_t)dsz, huf);
        if (hrc != ZXC_OK) {
            printf("  [FAIL] train_dict_huf: %s\n", zxc_error_name(hrc));
            break;
        }
        const size_t bound = zxc_dict_save_bound((size_t)dsz);
        zxd = (uint8_t*)malloc(bound);
        const int64_t zsz = zxc_dict_save(dict_buf, (size_t)dsz, huf, zxd, bound);
        if (zsz <= 0) {
            printf("  [FAIL] dict_save: %lld\n", (long long)zsz);
            break;
        }

        /* One-call creator must produce byte-identical .zxd output (the
         * trainers are deterministic). */
        const size_t one_bound = zxc_dict_save_bound(ZXC_DICT_SIZE_MAX);
        zxd_one = (uint8_t*)malloc(one_bound);
        const int64_t one_sz = zxc_dict_train(samples, sizes, NS, zxd_one, one_bound);
        if (one_sz != zsz || memcmp(zxd_one, zxd, (size_t)zsz) != 0) {
            printf("  [FAIL] zxc_dict_train (%lld B) != 3-step pipeline (%lld B)\n",
                   (long long)one_sz, (long long)zsz);
            break;
        }

        /* Folded load yields content + table + id in one call; the table must
         * match both the trained bytes and the standalone accessor. */
        const void* content = NULL;
        size_t csz = 0;
        const void* table = NULL;
        uint32_t id = 0;
        if (zxc_dict_load(zxd, (size_t)zsz, &content, &csz, &table, &id) != ZXC_OK ||
            csz != (size_t)dsz || memcmp(content, dict_buf, csz) != 0) {
            printf("  [FAIL] load of table-carrying .zxd\n");
            break;
        }
        if (!table || table != zxc_dict_huf(zxd, (size_t)zsz) ||
            memcmp(table, huf, ZXC_HUF_TABLE_SIZE) != 0) {
            printf("  [FAIL] dict_load table out-param / zxc_dict_huf mismatch\n");
            break;
        }
        /* The id must bind the table: different from the content-only id. */
        if (id == zxc_dict_id(dict_buf, (size_t)dsz)) {
            printf("  [FAIL] dict_id does not cover the table\n");
            break;
        }
        /* The format requires the table: NULL lengths must be refused. */
        uint8_t* zxd2 = (uint8_t*)malloc(bound);
        const int64_t z2 = zxc_dict_save(dict_buf, (size_t)dsz, NULL, zxd2, bound);
        free(zxd2);
        if (z2 != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] table-less save accepted: %lld\n", (long long)z2);
            break;
        }

        /* Corrupting the stored table must break the covering id. */
        zxd[zsz - 1] ^= 0xA5;
        if (zxc_dict_load(zxd, (size_t)zsz, &content, &csz, NULL, &id) != ZXC_ERROR_BAD_CHECKSUM) {
            printf("  [FAIL] corrupted table not rejected\n");
            break;
        }
        ok = 1;
    } while (0);

    free(zxd);
    free(zxd_one);
    free(dict_buf);
    for (int i = 0; i < NS; i++) free(bufs[i]);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

int test_dict_huf_table_roundtrip(void) {
    printf("=== TEST: Dict - shared-table compression roundtrip + id binding ===\n");

    enum { NS = 6, SCAP = 16384, HCAP = 32768 };
    uint8_t* bufs[NS];
    const void* samples[NS];
    size_t sizes[NS];
    for (int i = 0; i < NS; i++) {
        bufs[i] = (uint8_t*)malloc(SCAP);
        sizes[i] = gen_structured_sample(bufs[i], SCAP, 0x2000u + (uint32_t)i);
        samples[i] = bufs[i];
    }
    uint8_t* heldout = (uint8_t*)malloc(HCAP);
    const size_t hsz = gen_structured_sample(heldout, HCAP, 0xBEEFu);

    int ok = 0;
    uint8_t dict_buf[8192];
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    uint8_t *c1 = NULL, *c2 = NULL, *out = NULL;
    do {
        const int64_t dsz = zxc_train_dict(samples, sizes, NS, dict_buf, sizeof(dict_buf));
        if (dsz <= 0 ||
            zxc_train_dict_huf(samples, sizes, NS, dict_buf, (size_t)dsz, huf) != ZXC_OK) {
            printf("  [FAIL] training\n");
            break;
        }

        const size_t cap = (size_t)zxc_compress_bound(hsz);
        c1 = (uint8_t*)malloc(cap);
        c2 = (uint8_t*)malloc(cap);
        out = (uint8_t*)malloc(hsz + 64);

        zxc_compress_opts_t o1 = {
            .level = 6, .block_size = 4096, .dict = dict_buf, .dict_size = (size_t)dsz};
        zxc_compress_opts_t o2 = {.level = 6,
                                  .block_size = 4096,
                                  .dict = dict_buf,
                                  .dict_size = (size_t)dsz,
                                  .dict_huf = huf};
        const int64_t s1 = zxc_compress(heldout, hsz, c1, cap, &o1);
        const int64_t s2 = zxc_compress(heldout, hsz, c2, cap, &o2);
        if (s1 <= 0 || s2 <= 0) {
            printf("  [FAIL] compress: %lld / %lld\n", (long long)s1, (long long)s2);
            break;
        }
        /* Exact size accounting makes the shared table a strict improvement
         * on this skewed corpus; never larger by construction. */
        if (s2 > s1) {
            printf("  [FAIL] shared table grew the archive: %lld > %lld\n", (long long)s2,
                   (long long)s1);
            break;
        }

        zxc_decompress_opts_t d2 = {
            .dict = dict_buf, .dict_size = (size_t)dsz, .dict_huf = huf};
        const int64_t r2 = zxc_decompress(c2, (size_t)s2, out, hsz + 64, &d2);
        if (r2 != (int64_t)hsz || memcmp(out, heldout, hsz) != 0) {
            printf("  [FAIL] roundtrip with table: %lld\n", (long long)r2);
            break;
        }

        /* id binding, both directions: table archive without the table, and
         * table-less archive with it, must both be rejected as MISMATCH. */
        zxc_decompress_opts_t d_no = {.dict = dict_buf, .dict_size = (size_t)dsz};
        if (zxc_decompress(c2, (size_t)s2, out, hsz + 64, &d_no) != ZXC_ERROR_DICT_MISMATCH) {
            printf("  [FAIL] table archive accepted without table\n");
            break;
        }
        if (zxc_decompress(c1, (size_t)s1, out, hsz + 64, &d2) != ZXC_ERROR_DICT_MISMATCH) {
            printf("  [FAIL] table-less archive accepted with table\n");
            break;
        }
        ok = 1;
    } while (0);

    free(c1);
    free(c2);
    free(out);
    free(heldout);
    for (int i = 0; i < NS; i++) free(bufs[i]);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}
