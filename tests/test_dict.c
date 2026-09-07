/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "../include/zxc_dict.h"
#include "test_common.h"

/* Build a valid 128-byte packed lengths table from arbitrary content bytes
 * (the .zxd format requires one). */
static void build_test_huf_lengths(const void* data, size_t n, uint8_t out[ZXC_HUF_TABLE_SIZE]) {
    uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) freq[p[i]]++;
    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    zxc_huf_build_code_lengths(freq, code_len, NULL, ZXC_HUF_MAX_CODE_LEN_DENSITY);
    zxc_huf_pack_lengths(code_len, out);
}

/* Cache-line aligned workspace for the static contexts. */
#if defined(_WIN32)
#include <malloc.h>
static void* static_ws_alloc(size_t size) { return size ? _aligned_malloc(size, 64) : NULL; }
static void static_ws_free(void* p) { _aligned_free(p); }
#else
static void* static_ws_alloc(size_t size) {
    void* p = NULL;
    if (size == 0 || posix_memalign(&p, 64, (size + 63) & ~(size_t)63) != 0) return NULL;
    return p;
}
static void static_ws_free(void* p) { free(p); }
#endif

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
    int rc =
        zxc_dict_load(zxd, (size_t)written, &loaded_content, &loaded_size, &loaded_huf, &loaded_id);
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
        loaded_id == zxc_dict_id(content, content_size, NULL)) {
        printf("  [FAIL] dict_id binding incorrect: got %u\n", loaded_id);
        free(zxd);
        return 0;
    }

    /* The folded load and the standalone accessor must both return the exact
     * table bytes we stored, and agree with each other. */
    const void* huf_acc = zxc_dict_huf(zxd, (size_t)written);
    if (!loaded_huf || loaded_huf != huf_acc || memcmp(loaded_huf, huf, ZXC_HUF_TABLE_SIZE) != 0) {
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

    uint32_t id1 = zxc_dict_id(data, size, NULL);
    uint32_t id2 = zxc_dict_id(data, size, NULL);

    if (id1 != id2 || id1 == 0) {
        printf("  [FAIL] dict_id not deterministic or zero: %u vs %u\n", id1, id2);
        return 0;
    }

    uint32_t id_null = zxc_dict_id(NULL, 0, NULL);
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
    const uint32_t expected_id = zxc_dict_id(dict, dict_size, NULL);

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
        int64_t dec_size =
            zxc_decompress(compressed, (size_t)comp_size, decompressed, src_size, &dopts);
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
        int64_t comp_size = zxc_compress_block(cctx, src, src_size, compressed, comp_bound, &copts);
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
        uint32_t x = (uint32_t)i * 2654435761U;
        for (int j = 0; j < PLEN; j++) {
            x = x * 1103515245U + 12345U;
            dict[(size_t)i * PLEN + j] = (uint8_t)(x >> 16);
        }
    }
    int order[NPAT];
    for (int i = 0; i < NPAT; i++) order[i] = i;
    uint32_t s = 777U;
    for (int i = NPAT - 1; i > 0; i--) {
        s = s * 1103515245U + 12345U;
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
    int64_t rc =
        zxc_decompress(compressed, (size_t)comp_size, decompressed, sizeof(decompressed), &dopts);
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
    int64_t rc =
        zxc_decompress(compressed, (size_t)comp_size, decompressed, sizeof(decompressed), &dopts);
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
    int64_t dec_size =
        zxc_decompress(compressed, (size_t)comp_size, decompressed, sizeof(decompressed), &dopts);
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
            .level = level,
            .checksum_enabled = 1,
            .dict = dict,
            .dict_size = dict_size,
        };
        int64_t comp_size = zxc_compress(src, src_size, compressed, comp_bound, &copts);
        if (comp_size <= 0) {
            printf("  [FAIL] level %d: compress returned %lld (%s)\n", level, (long long)comp_size,
                   zxc_error_name((int)comp_size));
            free(src);
            free(compressed);
            free(decompressed);
            free(dict);
            return 0;
        }
        zxc_decompress_opts_t dopts = {.checksum_enabled = 1, .dict = dict, .dict_size = dict_size};
        int64_t dec_size =
            zxc_decompress(compressed, (size_t)comp_size, decompressed, src_size, &dopts);
        if (dec_size != (int64_t)src_size || memcmp(src, decompressed, src_size) != 0) {
            printf("  [FAIL] level %d: dec_size=%lld err=%s\n", level, (long long)dec_size,
                   dec_size < 0 ? zxc_error_name((int)dec_size) : "content mismatch");
            free(src);
            free(compressed);
            free(decompressed);
            free(dict);
            return 0;
        }
        printf("  [PASS] level %d\n", level);
    }

    free(src);
    free(compressed);
    free(decompressed);
    free(dict);
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
 * sequence is validated by the SAFE 4x loop -- which accepts these back-refs only
 * because the floor is `dst - dict_size`, not `dst`. If the dictionary term were
 * dropped from d_floor, they would be wrongly rejected (BAD_OFFSET).
 * See project_dict_written_floor.
 */
int test_dict_safe_loop_backref(void) {
    printf("=== TEST: Dict - many small back-refs in the SAFE 4x loop ===\n");

    enum { NPAT = 256, PLEN = 40 };
    const size_t dict_size = (size_t)NPAT * PLEN;
    uint8_t* dict = (uint8_t*)malloc(dict_size);
    for (int i = 0; i < NPAT; i++) {
        uint32_t x = (uint32_t)i * 2654435761U;
        for (int j = 0; j < PLEN; j++) {
            x = x * 1103515245U + 12345U;
            dict[(size_t)i * PLEN + j] = (uint8_t)(x >> 16);
        }
    }

    int order[NPAT];
    for (int i = 0; i < NPAT; i++) order[i] = i;
    uint32_t s = 12345U;
    for (int i = NPAT - 1; i > 0; i--) {
        s = s * 1103515245U + 12345U;
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
    int64_t dec_size =
        zxc_decompress(compressed, (size_t)comp_size, decompressed, sizeof(decompressed), &dopts);
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
            printf("  [FAIL] %s: expected %s, got %lld (%s)\n", label,
                   zxc_error_name((int)want_err), (long long)rc, zxc_error_name((int)rc));
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
    *s = *s * 1664525U + 1013904223U;
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
                              (huf_lcg(&s) % 5) ? 200U : 500U);
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
        sizes[i] = gen_structured_sample(bufs[i], SCAP, 0x1000U + (uint32_t)i);
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
        if (id == zxc_dict_id(dict_buf, (size_t)dsz, NULL)) {
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
        sizes[i] = gen_structured_sample(bufs[i], SCAP, 0x2000U + (uint32_t)i);
        samples[i] = bufs[i];
    }
    uint8_t* heldout = (uint8_t*)malloc(HCAP);
    const size_t hsz = gen_structured_sample(heldout, HCAP, 0xBEEFU);

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

        zxc_decompress_opts_t d2 = {.dict = dict_buf, .dict_size = (size_t)dsz, .dict_huf = huf};
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

/* A degenerate training corpus (a single repeated byte, a zero-filled region)
 * is matched in full against the trained dict, so there are no post-LZ literals
 * to histogram. Training must emit a legitimately empty (all-zero) shared table
 * rather than failing, and that .zxd must remain usable for a compress/
 * decompress roundtrip (the empty table is treated as "no shared table"). */
int test_dict_huf_degenerate_corpus(void) {
    printf("=== TEST: Dict - degenerate corpus -> empty shared table ===\n");

    enum { SAMPLE_SIZE = 4096 };
    const uint8_t fills[2] = {(uint8_t)'A', 0x00};

    uint8_t* sample = (uint8_t*)malloc(SAMPLE_SIZE);
    uint8_t* comp = NULL;
    uint8_t* out = NULL;
    int ok = 1;

    for (size_t f = 0; f < sizeof(fills) && ok; f++) {
        memset(sample, fills[f], SAMPLE_SIZE);
        const void* samples[1] = {sample};
        const size_t sizes[1] = {SAMPLE_SIZE};

        uint8_t dict_buf[8192];
        uint8_t huf[ZXC_HUF_TABLE_SIZE];

        ok = 0;
        do {
            const int64_t dsz = zxc_train_dict(samples, sizes, 1, dict_buf, sizeof(dict_buf));
            if (dsz <= 0) {
                printf("  [FAIL] fill 0x%02X: train_dict: %lld\n", fills[f], (long long)dsz);
                break;
            }
            /* Previously returned ZXC_ERROR_CORRUPT_DATA on an empty histogram. */
            const int hrc = zxc_train_dict_huf(samples, sizes, 1, dict_buf, (size_t)dsz, huf);
            if (hrc != ZXC_OK) {
                printf("  [FAIL] fill 0x%02X: train_dict_huf on empty histogram: %s\n", fills[f],
                       zxc_error_name(hrc));
                break;
            }
            /* It must be the empty (all-zero) table, not a built code. */
            int empty = 1;
            for (int i = 0; i < ZXC_HUF_TABLE_SIZE; i++) {
                if (huf[i]) {
                    empty = 0;
                    break;
                }
            }
            if (!empty) {
                printf("  [FAIL] fill 0x%02X: expected an all-zero shared table\n", fills[f]);
                break;
            }

            /* The empty-table .zxd must serialize and load cleanly. */
            const size_t bound = zxc_dict_save_bound((size_t)dsz);
            uint8_t* zxd = (uint8_t*)malloc(bound);
            const int64_t zsz = zxc_dict_save(dict_buf, (size_t)dsz, huf, zxd, bound);
            const void* content = NULL;
            size_t csz = 0;
            const void* table = NULL;
            uint32_t id = 0;
            const int lrc =
                (zsz > 0) ? zxc_dict_load(zxd, (size_t)zsz, &content, &csz, &table, &id) : (int)zsz;
            free(zxd);
            if (zsz <= 0 || lrc != ZXC_OK) {
                printf("  [FAIL] fill 0x%02X: empty-table .zxd save=%lld load=%d\n", fills[f],
                       (long long)zsz, lrc);
                break;
            }

            /* Volet 2: the empty table must be usable end-to-end. */
            const size_t cap = (size_t)zxc_compress_bound(SAMPLE_SIZE);
            comp = (uint8_t*)malloc(cap);
            out = (uint8_t*)malloc(SAMPLE_SIZE + 64);
            zxc_compress_opts_t co = {.level = 6,
                                      .block_size = 4096,
                                      .dict = dict_buf,
                                      .dict_size = (size_t)dsz,
                                      .dict_huf = huf};
            const int64_t csize = zxc_compress(sample, SAMPLE_SIZE, comp, cap, &co);
            if (csize <= 0) {
                printf("  [FAIL] fill 0x%02X: compress with empty-table dict: %lld\n", fills[f],
                       (long long)csize);
                break;
            }
            zxc_decompress_opts_t deo = {
                .dict = dict_buf, .dict_size = (size_t)dsz, .dict_huf = huf};
            const int64_t dsize = zxc_decompress(comp, (size_t)csize, out, SAMPLE_SIZE + 64, &deo);
            if (dsize != (int64_t)SAMPLE_SIZE || memcmp(out, sample, SAMPLE_SIZE) != 0) {
                printf("  [FAIL] fill 0x%02X: roundtrip with empty-table dict: %lld\n", fills[f],
                       (long long)dsize);
                break;
            }
            ok = 1;
        } while (0);

        free(comp);
        comp = NULL;
        free(out);
        out = NULL;
    }

    free(sample);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Reusable / static decompression contexts with a dictionary                 */
/* ------------------------------------------------------------------------- */

static const uint8_t k_dctx_dict[] =
    "The quick brown fox jumps over the lazy dog. "
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
    "Pack my box with five dozen liquor jugs. "
    "How vexingly quick daft zebras jump!";

/* Compresses @p src at @p level with an optional (dict, table); <= 0 on failure. */
static int64_t dctx_make_archive(int level, size_t block_size, const uint8_t* src, size_t n,
                                 const uint8_t* dict, size_t dict_size, const uint8_t* huf,
                                 uint8_t* out, size_t cap) {
    zxc_compress_opts_t co = {
        .level = level,
        .block_size = block_size,
        .checksum_enabled = 1,
        .dict = dict,
        .dict_size = dict_size,
        .dict_huf = huf,
    };
    return zxc_compress(src, n, out, cap, &co);
}

typedef struct {
    const char* label;
    const uint8_t* arc;
    int64_t n;
    const zxc_decompress_opts_t* opts;
    int64_t want; /* decompressed size, or the expected error */
} dctx_step_t;

static int dctx_run_steps(zxc_dctx* dctx, const dctx_step_t* steps, size_t n_steps,
                          const uint8_t* src, size_t src_size, uint8_t* dec) {
    for (size_t i = 0; i < n_steps; i++) {
        memset(dec, 0, src_size);
        const int64_t got = zxc_decompress_dctx(dctx, steps[i].arc, (size_t)steps[i].n, dec,
                                                src_size, steps[i].opts);
        if (got != steps[i].want || (got > 0 && memcmp(dec, src, src_size) != 0)) {
            printf("  [FAIL] %s: got %lld, expected %lld\n", steps[i].label, (long long)got,
                   (long long)steps[i].want);
            return 0;
        }
        printf("  [PASS] %s\n", steps[i].label);
    }
    return 1;
}

int test_dict_dctx_roundtrip(void) {
    printf("=== TEST: Dict - reusable dctx honours the dictionary ===\n");
    const size_t dict_size = sizeof(k_dctx_dict) - 1;
    const size_t src_size = 4096;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, k_dctx_dict, dict_size);
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    build_test_huf_lengths(k_dctx_dict, dict_size, huf);

    const size_t cap = (size_t)zxc_compress_bound(src_size);
    uint8_t* a3 = (uint8_t*)malloc(cap);
    uint8_t* a7 = (uint8_t*)malloc(cap);
    uint8_t* a0 = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(src_size);
    const int64_t n3 =
        dctx_make_archive(3, 0, src, src_size, k_dctx_dict, dict_size, NULL, a3, cap);
    const int64_t n7 = dctx_make_archive(7, 0, src, src_size, k_dctx_dict, dict_size, huf, a7, cap);
    const int64_t n0 = dctx_make_archive(3, 0, src, src_size, NULL, 0, NULL, a0, cap);
    zxc_dctx* dctx = zxc_create_dctx();
    int ok = n3 > 0 && n7 > 0 && n0 > 0 && dctx != NULL;
    if (!ok)
        printf("  [FAIL] setup: %lld %lld %lld\n", (long long)n3, (long long)n7, (long long)n0);

    uint8_t wrong[sizeof(k_dctx_dict)];
    memcpy(wrong, k_dctx_dict, sizeof(wrong));
    wrong[10] ^= 0x55;

    const zxc_decompress_opts_t none = {.checksum_enabled = 1};
    const zxc_decompress_opts_t right = {
        .checksum_enabled = 1, .dict = k_dctx_dict, .dict_size = dict_size};
    const zxc_decompress_opts_t right_huf = {
        .checksum_enabled = 1, .dict = k_dctx_dict, .dict_size = dict_size, .dict_huf = huf};
    const zxc_decompress_opts_t bad = {
        .checksum_enabled = 1, .dict = wrong, .dict_size = dict_size};
    const int64_t full = (int64_t)src_size;

    const dctx_step_t steps[] = {
        {"L3, no dict -> DICT_REQUIRED", a3, n3, &none, ZXC_ERROR_DICT_REQUIRED},
        {"L3, wrong dict -> DICT_MISMATCH", a3, n3, &bad, ZXC_ERROR_DICT_MISMATCH},
        {"L3, right dict", a3, n3, &right, full},
        {"L7 + table, right dict (tree attach)", a7, n7, &right_huf, full},
        {"L7 + table again (cached tree)", a7, n7, &right_huf, full},
        {"L7 + table, table omitted -> DICT_MISMATCH", a7, n7, &right, ZXC_ERROR_DICT_MISMATCH},
        {"plain archive, NULL opts (dict -> none re-init)", a0, n0, NULL, full},
        {"L3, right dict again (none -> dict re-init)", a3, n3, &right, full},
    };
    if (ok) ok = dctx_run_steps(dctx, steps, sizeof(steps) / sizeof(steps[0]), src, src_size, dec);

    zxc_free_dctx(dctx);
    free(src);
    free(a3);
    free(a7);
    free(a0);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_dict_static_dctx_rejected(void) {
    printf("=== TEST: Dict - static dctx rejects dictionaries explicitly ===\n");
    const size_t dict_size = sizeof(k_dctx_dict) - 1;
    const size_t block_size = ZXC_BLOCK_SIZE_MIN;
    const size_t src_size = block_size;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, k_dctx_dict, dict_size);

    const size_t cap = (size_t)zxc_compress_bound(src_size);
    uint8_t* a3 = (uint8_t*)malloc(cap);
    uint8_t* a0 = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(src_size);
    const int64_t n3 =
        dctx_make_archive(3, block_size, src, src_size, k_dctx_dict, dict_size, NULL, a3, cap);
    const int64_t n0 = dctx_make_archive(3, block_size, src, src_size, NULL, 0, NULL, a0, cap);

    const size_t ws_sz = zxc_static_dctx_workspace_size(block_size, 0);
    void* ws = static_ws_alloc(ws_sz);
    zxc_dctx* dctx = ws ? zxc_init_static_dctx(ws, ws_sz, block_size, 0) : NULL;
    int ok = n3 > 0 && n0 > 0 && dctx != NULL;
    if (!ok) printf("  [FAIL] setup: %lld %lld ws=%zu\n", (long long)n3, (long long)n0, ws_sz);

    const zxc_decompress_opts_t right = {
        .checksum_enabled = 1, .dict = k_dctx_dict, .dict_size = dict_size};
    const dctx_step_t steps[] = {
        {"dict archive, right dict -> DICT_UNSUPPORTED", a3, n3, &right,
         ZXC_ERROR_DICT_UNSUPPORTED},
        {"dict archive, no dict -> DICT_UNSUPPORTED", a3, n3, NULL, ZXC_ERROR_DICT_UNSUPPORTED},
        {"plain archive, dict supplied -> DICT_UNSUPPORTED", a0, n0, &right,
         ZXC_ERROR_DICT_UNSUPPORTED},
        {"plain archive, no dict -> OK", a0, n0, NULL, (int64_t)src_size},
    };
    if (ok) ok = dctx_run_steps(dctx, steps, sizeof(steps) / sizeof(steps[0]), src, src_size, dec);

    zxc_free_dctx(dctx); /* no-op for a static context */
    static_ws_free(ws);
    free(src);
    free(a3);
    free(a0);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_dict_cctx_roundtrip(void) {
    printf("=== TEST: Dict - reusable cctx honours the dictionary ===\n");
    const size_t dict_size = sizeof(k_dctx_dict) - 1;
    const size_t src_size = 4096;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, k_dctx_dict, dict_size);
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    build_test_huf_lengths(k_dctx_dict, dict_size, huf);
    const size_t cap = (size_t)zxc_compress_bound(src_size);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(src_size);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    int ok = 0;

    const zxc_compress_opts_t c3 = {.level = 3, .dict = k_dctx_dict, .dict_size = dict_size};
    const zxc_compress_opts_t c7 = {
        .level = 7, .dict = k_dctx_dict, .dict_size = dict_size, .dict_huf = huf};
    const zxc_decompress_opts_t d_plain = {.checksum_enabled = 0};
    const zxc_decompress_opts_t d3 = {.dict = k_dctx_dict, .dict_size = dict_size};
    const zxc_decompress_opts_t d7 = {.dict = k_dctx_dict, .dict_size = dict_size, .dict_huf = huf};
    /* Each step: compress through the cctx, check the header id, then decode
     * one-shot with the matching options and expect the source back. */
    const struct {
        const char* label;
        const zxc_compress_opts_t* co;
        uint32_t want_id;
        const zxc_decompress_opts_t* dop;
    } steps[] = {
        {"L3 + dict", &c3, zxc_dict_id(k_dctx_dict, dict_size, NULL), &d3},
        {"L7 + dict + table (tree attach)", &c7, zxc_dict_id(k_dctx_dict, dict_size, huf), &d7},
        {"L7 + dict + table again (cached tree)", &c7, zxc_dict_id(k_dctx_dict, dict_size, huf),
         &d7},
        {"NULL opts: dictionary is not sticky", NULL, 0, &d_plain},
        {"L3 + dict again (re-carve)", &c3, zxc_dict_id(k_dctx_dict, dict_size, NULL), &d3},
    };
    do {
        if (!cctx) {
            printf("  [FAIL] zxc_create_cctx\n");
            break;
        }
        int bad = 0;
        for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]) && !bad; i++) {
            const int64_t cs = zxc_compress_cctx(cctx, src, src_size, comp, cap, steps[i].co);
            const uint32_t id = cs > 0 ? zxc_get_dict_id(comp, (size_t)cs) : 0;
            memset(dec, 0, src_size);
            const int64_t r =
                cs > 0 ? zxc_decompress(comp, (size_t)cs, dec, src_size, steps[i].dop) : -1;
            if (cs <= 0 || id != steps[i].want_id || r != (int64_t)src_size ||
                memcmp(dec, src, src_size) != 0) {
                printf("  [FAIL] %s: cs=%lld id=%08X want=%08X r=%lld\n", steps[i].label,
                       (long long)cs, id, steps[i].want_id, (long long)r);
                bad = 1;
                break;
            }
            printf("  [PASS] %s\n", steps[i].label);
        }
        if (bad) break;
        /* Dictionary archives refuse a dictionary-less decode: the id is real. */
        const int64_t cs = zxc_compress_cctx(cctx, src, src_size, comp, cap, &c3);
        if (cs <= 0 ||
            zxc_decompress(comp, (size_t)cs, dec, src_size, &d_plain) != ZXC_ERROR_DICT_REQUIRED) {
            printf("  [FAIL] cctx dict archive decoded without a dictionary\n");
            break;
        }
        printf("  [PASS] cctx dict archive -> DICT_REQUIRED without the dictionary\n");

        /* Static cctx rejects dictionaries explicitly. */
        const size_t ws_sz = zxc_static_cctx_workspace_size(ZXC_BLOCK_SIZE_MIN, 3, 0);
        void* ws = static_ws_alloc(ws_sz);
        const zxc_compress_opts_t so = {.level = 3, .block_size = ZXC_BLOCK_SIZE_MIN};
        zxc_cctx* sc = ws ? zxc_init_static_cctx(ws, ws_sz, &so) : NULL;
        const zxc_compress_opts_t sdict = {.level = 3,
                                           .block_size = ZXC_BLOCK_SIZE_MIN,
                                           .dict = k_dctx_dict,
                                           .dict_size = dict_size};
        const int64_t r1 = sc ? zxc_compress_cctx(sc, src, src_size, comp, cap, &sdict) : -1;
        const int64_t r2 = sc ? zxc_compress_cctx(sc, src, src_size, comp, cap, NULL) : -1;
        static_ws_free(ws);
        if (r1 != ZXC_ERROR_DICT_UNSUPPORTED || r2 <= 0) {
            printf("  [FAIL] static cctx: dict -> %lld, plain -> %lld\n", (long long)r1,
                   (long long)r2);
            break;
        }
        printf("  [PASS] static cctx: dict -> DICT_UNSUPPORTED, plain -> OK\n");
        ok = 1;
    } while (0);

    zxc_free_cctx(cctx);
    free(src);
    free(comp);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* A block_size switch can round [dict | block] back to the size a context was
 * carved for without a prefix: the block API must re-carve, not copy into NULL. */
int test_dict_block_cctx_dict_reinit(void) {
    printf("=== TEST: Dict - block cctx re-carves when a dictionary arrives ===\n");
    const size_t dict_size = sizeof(k_dctx_dict) - 1;
    const size_t n = 3000;
    uint8_t* src = (uint8_t*)malloc(n);
    gen_dict_friendly_data(src, n, k_dctx_dict, dict_size);
    const size_t cap = (size_t)zxc_compress_block_bound(n);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* out = (uint8_t*)malloc(n + ZXC_DECOMPRESS_TAIL_PAD);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_dctx* dctx = zxc_create_dctx();
    /* 8192 without a dictionary, then ceil(dict + 4096) == 8192 with one. */
    const zxc_compress_opts_t plain = {.level = 3, .block_size = 2 * ZXC_BLOCK_SIZE_MIN};
    const zxc_compress_opts_t with = {
        .level = 3, .block_size = ZXC_BLOCK_SIZE_MIN, .dict = k_dctx_dict, .dict_size = dict_size};
    const zxc_decompress_opts_t dd = {.dict = k_dctx_dict, .dict_size = dict_size};
    int ok = 0;
    do {
        if (!cctx || !dctx) {
            printf("  [FAIL] context allocation\n");
            break;
        }
        const int64_t c1 = zxc_compress_block(cctx, src, n, comp, cap, &plain);
        const int64_t c2 = zxc_compress_block(cctx, src, n, comp, cap, &with);
        const int64_t r = c2 > 0 ? zxc_decompress_block(dctx, comp, (size_t)c2, out,
                                                        n + ZXC_DECOMPRESS_TAIL_PAD, &dd)
                                 : -1;
        if (c1 <= 0 || c2 <= 0 || r != (int64_t)n || memcmp(out, src, n) != 0) {
            printf("  [FAIL] %lld, %lld, roundtrip %lld\n", (long long)c1, (long long)c2,
                   (long long)r);
            break;
        }
        printf("  [PASS] no-dict at 8K then dict at 4K: %lld -> %lld bytes\n", (long long)c1,
               (long long)c2);
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(src);
    free(comp);
    free(out);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* A failed table attach must not leave the context believing it still holds
 * the previous table: the next call with that table has to rebuild its tree. */
int test_dict_ctx_table_cache_recovers(void) {
    printf("=== TEST: Dict - context table cache recovers after a bad table ===\n");
    enum { NS = 6, SCAP = 16384, HCAP = 16384 };
    uint8_t* bufs[NS];
    const void* samples[NS];
    size_t sizes[NS];
    for (int i = 0; i < NS; i++) {
        bufs[i] = (uint8_t*)malloc(SCAP);
        sizes[i] = gen_structured_sample(bufs[i], SCAP, 0x4000U + (uint32_t)i);
        samples[i] = bufs[i];
    }
    uint8_t* heldout = (uint8_t*)malloc(HCAP);
    const size_t hsz = gen_structured_sample(heldout, HCAP, 0xBADCU);
    uint8_t dict_buf[8192];
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    uint8_t bad[ZXC_HUF_TABLE_SIZE];
    memset(bad, 0x11, sizeof(bad)); /* every code one bit long: over-subscribed */
    const size_t cap = (size_t)zxc_compress_bound(hsz);
    uint8_t* c1 = (uint8_t*)malloc(cap);
    uint8_t* c3 = (uint8_t*)malloc(cap);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    int ok = 0;
    do {
        const int64_t dsz = zxc_train_dict(samples, sizes, NS, dict_buf, sizeof(dict_buf));
        if (dsz <= 0 || !cctx ||
            zxc_train_dict_huf(samples, sizes, NS, dict_buf, (size_t)dsz, huf) != ZXC_OK) {
            printf("  [FAIL] setup\n");
            break;
        }
        const zxc_compress_opts_t good = {.level = 6,
                                          .block_size = 4096,
                                          .dict = dict_buf,
                                          .dict_size = (size_t)dsz,
                                          .dict_huf = huf};
        const zxc_compress_opts_t broken = {.level = 6,
                                            .block_size = 4096,
                                            .dict = dict_buf,
                                            .dict_size = (size_t)dsz,
                                            .dict_huf = bad};
        const int64_t n1 = zxc_compress_cctx(cctx, heldout, hsz, c1, cap, &good);
        const int64_t n2 = zxc_compress_cctx(cctx, heldout, hsz, c3, cap, &broken);
        const int64_t n3 = zxc_compress_cctx(cctx, heldout, hsz, c3, cap, &good);
        if (n1 <= 0 || n2 != ZXC_ERROR_CORRUPT_DATA || n3 != n1 ||
            memcmp(c1, c3, (size_t)n1) != 0) {
            printf("  [FAIL] %lld, %lld, %lld (archives %s)\n", (long long)n1, (long long)n2,
                   (long long)n3, n3 == n1 && memcmp(c1, c3, (size_t)n1) == 0 ? "equal" : "differ");
            break;
        }
        printf("  [PASS] bad table -> CORRUPT_DATA, then the good table yields the same archive\n");
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    free(c1);
    free(c3);
    free(heldout);
    for (int i = 0; i < NS; i++) free(bufs[i]);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* A table only accompanies a dictionary: with dict_size == 0 a reused context
 * must drop a previously attached table instead of emitting enc_lit=3 into a
 * dictionary-less archive. A table trained against a tiny dictionary codes raw
 * literals well, so the shared table wins on a short block either way. */
int test_dict_ctx_table_without_dict(void) {
    printf("=== TEST: Dict - context drops the table when the dictionary is absent ===\n");
    enum { NS = 6, SCAP = 16384, PSZ = 3000 };
    uint8_t* bufs[NS];
    const void* samples[NS];
    size_t sizes[NS];
    for (int i = 0; i < NS; i++) {
        bufs[i] = (uint8_t*)malloc(SCAP);
        sizes[i] = gen_structured_sample(bufs[i], SCAP, 0x5000U + (uint32_t)i);
        samples[i] = bufs[i];
    }
    uint8_t dict_buf[64];
    memset(dict_buf, 'x', sizeof(dict_buf));
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    const size_t cap = (size_t)zxc_compress_bound(PSZ);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* ref = (uint8_t*)malloc(cap);
    uint8_t* out = (uint8_t*)malloc(PSZ + 64);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    int ok = 0;
    do {
        if (!cctx || sizes[0] < PSZ ||
            zxc_train_dict_huf(samples, sizes, NS, dict_buf, sizeof(dict_buf), huf) != ZXC_OK) {
            printf("  [FAIL] setup\n");
            break;
        }
        /* Same carved chunk ([dict | 4096] rounded up) so the context, table
         * state included, is reused without a dictionary. */
        const size_t carved = zxc_block_size_ceil(sizeof(dict_buf) + 4096);
        const zxc_compress_opts_t with = {.level = 6,
                                          .block_size = 4096,
                                          .dict = dict_buf,
                                          .dict_size = sizeof(dict_buf),
                                          .dict_huf = huf};
        const zxc_compress_opts_t none = {
            .level = 6, .block_size = carved, .dict = dict_buf, .dict_size = 0, .dict_huf = huf};
        const int64_t n1 = zxc_compress_cctx(cctx, bufs[0], PSZ, comp, cap, &with);
        if (n1 <= 0 || comp[ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + 8] != 3) {
            printf("  [FAIL] the shared table was not selected with the dictionary (%lld)\n",
                   (long long)n1);
            break;
        }
        const int64_t n = zxc_compress_cctx(cctx, bufs[0], PSZ, comp, cap, &none);
        const int64_t r = zxc_compress(bufs[0], PSZ, ref, cap, &none);
        if (n <= 0 || r != n || memcmp(comp, ref, (size_t)n) != 0 ||
            zxc_get_dict_id(comp, (size_t)n) != 0 ||
            zxc_decompress(comp, (size_t)n, out, PSZ + 64, NULL) != (int64_t)PSZ ||
            memcmp(out, bufs[0], PSZ) != 0) {
            printf("  [FAIL] cctx %lld vs one-shot %lld, enc_lit=%u, dict_id %08X\n", (long long)n,
                   (long long)r,
                   n > 0 ? (unsigned)comp[ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + 8] : 0U,
                   n > 0 ? zxc_get_dict_id(comp, (size_t)n) : 0U);
            break;
        }
        printf("  [PASS] dict_size 0 after a dict call: plain archive, identical to one-shot\n");
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    free(comp);
    free(ref);
    free(out);
    for (int i = 0; i < NS; i++) free(bufs[i]);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* Every dictionary entry point rejects an oversized dictionary before touching it. */
int test_dict_oversized_rejected_everywhere(void) {
    printf("=== TEST: Dict - oversized dictionary rejected on every entry point ===\n");
    uint8_t src[256], comp[1024];
    for (size_t i = 0; i < sizeof(src); i++) src[i] = (uint8_t)i;
    const zxc_compress_opts_t plain = {.level = 3};
    const int64_t cs = zxc_compress(src, sizeof(src), comp, sizeof(comp), &plain);
    const size_t too_big = (size_t)ZXC_DICT_SIZE_MAX + 1;
    const zxc_compress_opts_t co = {.level = 3, .dict = src, .dict_size = too_big};
    const zxc_decompress_opts_t dop = {.dict = src, .dict_size = too_big};
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_dctx* dctx = zxc_create_dctx();
    int ok = 0;
    if (cs > 0 && cctx && dctx) {
        uint8_t out[512];
        const int64_t e[] = {
            zxc_compress(src, sizeof(src), comp, sizeof(comp), &co),
            zxc_compress_cctx(cctx, src, sizeof(src), comp, sizeof(comp), &co),
            zxc_compress_block(cctx, src, sizeof(src), comp, sizeof(comp), &co),
            zxc_decompress(comp, (size_t)cs, out, sizeof(out), &dop),
            zxc_decompress_dctx(dctx, comp, (size_t)cs, out, sizeof(out), &dop),
            zxc_decompress_block(dctx, comp + ZXC_FILE_HEADER_SIZE,
                                 (size_t)cs - ZXC_FILE_HEADER_SIZE, out, sizeof(out), &dop),
        };
        ok = 1;
        for (size_t i = 0; i < sizeof(e) / sizeof(e[0]); i++) {
            if (e[i] != ZXC_ERROR_DICT_TOO_LARGE) {
                printf("  [FAIL] entry %zu: %lld\n", i, (long long)e[i]);
                ok = 0;
            }
        }
    } else {
        printf("  [FAIL] setup\n");
    }
    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    if (ok) printf("  [PASS] six entry points -> DICT_TOO_LARGE\nPASS\n\n");
    return ok;
}

int test_dict_block_huf_roundtrip(void) {
    printf("=== TEST: Dict - block API honours the shared literal table ===\n");
    enum { NS = 6, SCAP = 16384, HCAP = 32768, BLK = 4096 };
    uint8_t* bufs[NS];
    const void* samples[NS];
    size_t sizes[NS];
    for (int i = 0; i < NS; i++) {
        bufs[i] = (uint8_t*)malloc(SCAP);
        sizes[i] = gen_structured_sample(bufs[i], SCAP, 0x3000U + (uint32_t)i);
        samples[i] = bufs[i];
    }
    uint8_t* heldout = (uint8_t*)malloc(HCAP);
    const size_t hsz = gen_structured_sample(heldout, HCAP, 0xC0DEU);
    uint8_t dict_buf[8192];
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    const size_t cap = (size_t)zxc_compress_block_bound(BLK);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* out = (uint8_t*)malloc(BLK + ZXC_DECOMPRESS_TAIL_PAD);
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    zxc_dctx* dctx = zxc_create_dctx();
    int ok = 0;
    do {
        const int64_t dsz = zxc_train_dict(samples, sizes, NS, dict_buf, sizeof(dict_buf));
        if (dsz <= 0 || !cctx || !dctx ||
            zxc_train_dict_huf(samples, sizes, NS, dict_buf, (size_t)dsz, huf) != ZXC_OK) {
            printf("  [FAIL] setup\n");
            break;
        }
        const zxc_compress_opts_t co = {
            .level = 6, .dict = dict_buf, .dict_size = (size_t)dsz, .dict_huf = huf};
        const zxc_decompress_opts_t d_tab = {
            .dict = dict_buf, .dict_size = (size_t)dsz, .dict_huf = huf};
        const zxc_decompress_opts_t d_no = {.dict = dict_buf, .dict_size = (size_t)dsz};

        int n_table = 0, n_blocks = 0, bad = 0;
        for (size_t off = 0; off + BLK <= hsz && !bad; off += BLK, n_blocks++) {
            const int64_t cs = zxc_compress_block(cctx, heldout + off, BLK, comp, cap, &co);
            if (cs <= 0) {
                printf("  [FAIL] compress_block @%zu: %lld\n", off, (long long)cs);
                bad = 1;
                break;
            }
            /* enc_lit sits at sub-header offset 8, right after the block header. */
            const int table_used = comp[0] == ZXC_BLOCK_GLO && comp[ZXC_BLOCK_HEADER_SIZE + 8] == 3;
            n_table += table_used;
            int64_t r = zxc_decompress_block(dctx, comp, (size_t)cs, out,
                                             BLK + ZXC_DECOMPRESS_TAIL_PAD, &d_tab);
            if (r != BLK || memcmp(out, heldout + off, BLK) != 0) {
                printf("  [FAIL] decompress_block @%zu: %lld\n", off, (long long)r);
                bad = 1;
                break;
            }
            r = zxc_decompress_block_safe(dctx, comp, (size_t)cs, out, BLK, &d_tab);
            if (r != BLK || memcmp(out, heldout + off, BLK) != 0) {
                printf("  [FAIL] decompress_block_safe @%zu: %lld\n", off, (long long)r);
                bad = 1;
                break;
            }
            /* enc_lit=3 cannot decode without the table. */
            r = zxc_decompress_block(dctx, comp, (size_t)cs, out, BLK + ZXC_DECOMPRESS_TAIL_PAD,
                                     &d_no);
            if (table_used ? (r >= 0) : (r != BLK)) {
                printf("  [FAIL] table-less decode @%zu: %lld (table_used=%d)\n", off, (long long)r,
                       table_used);
                bad = 1;
                break;
            }
        }
        if (bad) break;
        if (n_table == 0) {
            printf(
                "  [FAIL] no block selected enc_lit=3: the table was never "
                "exercised\n");
            break;
        }
        printf("  [PASS] %d/%d blocks coded with the shared table, all roundtrip\n", n_table,
               n_blocks);

        /* Static cctx: no dictionary prefix in the workspace, explicit error. */
        {
            const size_t ws_sz = zxc_static_cctx_workspace_size(BLK, 6, 0);
            void* ws = static_ws_alloc(ws_sz);
            const zxc_compress_opts_t so = {.level = 6, .block_size = BLK};
            zxc_cctx* sc = ws ? zxc_init_static_cctx(ws, ws_sz, &so) : NULL;
            const int64_t r = sc ? zxc_compress_block(sc, heldout, BLK, comp, cap, &co) : -1;
            static_ws_free(ws);
            if (r != ZXC_ERROR_DICT_UNSUPPORTED) {
                printf("  [FAIL] static cctx + dict: %lld\n", (long long)r);
                break;
            }
            printf("  [PASS] static cctx + dict -> DICT_UNSUPPORTED\n");
        }

        /* Static dctx: same contract on both block decoders, the strict one
         * routing dictionary calls through the fast one. */
        {
            const int64_t n = zxc_compress_block(cctx, heldout, BLK, comp, cap, &co);
            const size_t ws_sz = zxc_static_dctx_workspace_size(BLK, 0);
            void* ws = malloc(ws_sz);
            zxc_dctx* sd = ws ? zxc_init_static_dctx(ws, ws_sz, BLK, 0) : NULL;
            const int64_t r1 = sd && n > 0
                                   ? zxc_decompress_block(sd, comp, (size_t)n, out,
                                                          BLK + ZXC_DECOMPRESS_TAIL_PAD, &d_tab)
                                   : -1;
            const int64_t r2 =
                sd && n > 0 ? zxc_decompress_block_safe(sd, comp, (size_t)n, out, BLK, &d_tab) : -1;
            free(ws);
            if (r1 != ZXC_ERROR_DICT_UNSUPPORTED || r2 != ZXC_ERROR_DICT_UNSUPPORTED) {
                printf("  [FAIL] static dctx + dict: %lld / %lld\n", (long long)r1, (long long)r2);
                break;
            }
            printf("  [PASS] static dctx + dict -> DICT_UNSUPPORTED on both decoders\n");
        }

        ok = 1;
    } while (0);

    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(comp);
    free(out);
    free(heldout);
    for (int i = 0; i < NS; i++) free(bufs[i]);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* The block API must remember the caller's block size, not the [dict | block]
 * chunk it carves: fed back as the next base, the chunk doubled on every call
 * until the context could not be allocated. The stored size is observable in
 * the Chunk Size Code that a later frame compression writes with NULL opts. */
int test_dict_block_stored_block_size(void) {
    printf("=== TEST: Dict - block API keeps the caller's block size ===\n");
    enum { BLK = 4096, CALLS = 4 };
    uint8_t src[BLK], dict_buf[1024], huf[ZXC_HUF_TABLE_SIZE], comp[BLK + 512];
    for (size_t i = 0; i < sizeof(src); i++) src[i] = (uint8_t)('a' + (i * 7) % 26);
    for (size_t i = 0; i < sizeof(dict_buf); i++) dict_buf[i] = (uint8_t)('a' + i % 26);
    const void* samples[1] = {src};
    const size_t sizes[1] = {sizeof(src)};
    uint8_t expected_code = 0;
    for (size_t bs = ZXC_BLOCK_SIZE_DEFAULT; bs > 1; bs >>= 1) expected_code++;
    zxc_cctx* cctx = zxc_create_cctx(NULL); /* default block size */
    int ok = 0;
    do {
        if (!cctx ||
            zxc_train_dict_huf(samples, sizes, 1, dict_buf, sizeof(dict_buf), huf) != ZXC_OK) {
            printf("  [FAIL] setup\n");
            break;
        }
        const zxc_compress_opts_t co = {
            .level = 6, .dict = dict_buf, .dict_size = sizeof(dict_buf), .dict_huf = huf};
        int64_t r = 0;
        for (int i = 0; i < CALLS && r >= 0; i++)
            r = zxc_compress_block(cctx, src, sizeof(src), comp, sizeof(comp), &co);
        if (r <= 0) {
            printf("  [FAIL] compress_block: %lld\n", (long long)r);
            break;
        }
        /* NULL opts: the frame path uses the stored block size for its header. */
        const int64_t f = zxc_compress_cctx(cctx, src, sizeof(src), comp, sizeof(comp), NULL);
        if (f <= 0 || comp[5] != expected_code) {
            printf("  [FAIL] frame after %d block calls: %lld, chunk code %u (want %u)\n", CALLS,
                   (long long)f, f > 0 ? comp[5] : 0U, expected_code);
            break;
        }
        printf("  [PASS] %d dict block calls, then a frame still carries chunk code %u\n", CALLS,
               expected_code);
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_dict_static_ctx_roundtrip(void) {
    printf("=== TEST: Dict - static contexts carved with a dictionary capacity ===\n");
    const size_t dict_size = sizeof(k_dctx_dict) - 1;
    const size_t block = ZXC_BLOCK_SIZE_MIN;
    const size_t src_size = block;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, k_dctx_dict, dict_size);
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    build_test_huf_lengths(k_dctx_dict, dict_size, huf);
    const size_t cap = (size_t)zxc_compress_bound(src_size);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(src_size + ZXC_DECOMPRESS_TAIL_PAD);

    const zxc_compress_opts_t init = {.level = 7, .block_size = block, .dict_size = dict_size};
    const size_t cws = zxc_static_cctx_workspace_size(block, 7, dict_size);
    const size_t dws = zxc_static_dctx_workspace_size(block, dict_size);
    void* cw = static_ws_alloc(cws);
    void* dw = static_ws_alloc(dws);
    zxc_cctx* sc = cw ? zxc_init_static_cctx(cw, cws, &init) : NULL;
    zxc_dctx* sd = dw ? zxc_init_static_dctx(dw, dws, block, dict_size) : NULL;

    const zxc_compress_opts_t c7 = {.level = 7,
                                    .block_size = block,
                                    .dict = k_dctx_dict,
                                    .dict_size = dict_size,
                                    .dict_huf = huf};
    const zxc_compress_opts_t c_plain = {.level = 7, .block_size = block};
    const zxc_compress_opts_t c_half = {
        .level = 3, .block_size = block, .dict = k_dctx_dict, .dict_size = dict_size / 2};
    const zxc_decompress_opts_t d7 = {.dict = k_dctx_dict, .dict_size = dict_size, .dict_huf = huf};
    const zxc_decompress_opts_t d3 = {.dict = k_dctx_dict, .dict_size = dict_size};
    const zxc_decompress_opts_t d_half = {.dict = k_dctx_dict, .dict_size = dict_size / 2};
    int ok = 0;
    do {
        if (!sc || !sd) {
            printf("  [FAIL] static init: cctx %p (ws %zu) dctx %p (ws %zu)\n", (void*)sc, cws,
                   (void*)sd, dws);
            break;
        }
        /* Frame API with dictionary + table, cross-checked against the one-shot decoder. */
        int64_t cs = zxc_compress_cctx(sc, src, src_size, comp, cap, &c7);
        int64_t r = cs > 0 ? zxc_decompress(comp, (size_t)cs, dec, src_size, &d7) : -1;
        if (cs <= 0 ||
            zxc_get_dict_id(comp, (size_t)cs) != zxc_dict_id(k_dctx_dict, dict_size, huf) ||
            r != (int64_t)src_size || memcmp(dec, src, src_size) != 0) {
            printf("  [FAIL] static cctx L7 + table: cs=%lld r=%lld\n", (long long)cs,
                   (long long)r);
            break;
        }
        memset(dec, 0, src_size);
        r = zxc_decompress_dctx(sd, comp, (size_t)cs, dec, src_size, &d7);
        if (r != (int64_t)src_size || memcmp(dec, src, src_size) != 0) {
            printf("  [FAIL] static dctx L7 + table: %lld\n", (long long)r);
            break;
        }
        printf("  [PASS] frame API, dictionary + table, both static contexts\n");

        /* A dictionary-less archive still goes through the same contexts. */
        cs = zxc_compress_cctx(sc, src, src_size, comp, cap, &c_plain);
        memset(dec, 0, src_size);
        r = cs > 0 ? zxc_decompress_dctx(sd, comp, (size_t)cs, dec, src_size, NULL) : -1;
        if (cs <= 0 || zxc_get_dict_id(comp, (size_t)cs) != 0 || r != (int64_t)src_size ||
            memcmp(dec, src, src_size) != 0) {
            printf("  [FAIL] plain archive through dict-capable contexts: %lld %lld\n",
                   (long long)cs, (long long)r);
            break;
        }
        printf("  [PASS] plain archive through the same contexts\n");

        /* The capacity is an upper bound: a smaller dictionary fits. */
        cs = zxc_compress_cctx(sc, src, src_size, comp, cap, &c_half);
        memset(dec, 0, src_size);
        r = cs > 0 ? zxc_decompress_dctx(sd, comp, (size_t)cs, dec, src_size, &d_half) : -1;
        if (cs <= 0 || r != (int64_t)src_size || memcmp(dec, src, src_size) != 0) {
            printf("  [FAIL] half-size dictionary: %lld %lld\n", (long long)cs, (long long)r);
            break;
        }
        printf("  [PASS] smaller dictionary than the capacity\n");

        /* Block API through the same static contexts. */
        const size_t bcap = (size_t)zxc_compress_block_bound(src_size);
        const zxc_compress_opts_t cb = {
            .level = 3, .block_size = block, .dict = k_dctx_dict, .dict_size = dict_size};
        cs = zxc_compress_block(sc, src, src_size, comp, bcap, &cb);
        memset(dec, 0, src_size);
        r = cs > 0 ? zxc_decompress_block(sd, comp, (size_t)cs, dec,
                                          src_size + ZXC_DECOMPRESS_TAIL_PAD, &d3)
                   : -1;
        const int64_t rs =
            cs > 0 ? zxc_decompress_block_safe(sd, comp, (size_t)cs, dec, src_size, &d3) : -1;
        if (cs <= 0 || r != (int64_t)src_size || rs != (int64_t)src_size ||
            memcmp(dec, src, src_size) != 0) {
            printf("  [FAIL] block API: cs=%lld r=%lld rs=%lld\n", (long long)cs, (long long)r,
                   (long long)rs);
            break;
        }
        printf("  [PASS] block API, dictionary, both static contexts\n");
        ok = 1;
    } while (0);
    static_ws_free(cw);
    static_ws_free(dw);
    free(src);
    free(comp);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_dict_static_ctx_capacity(void) {
    printf("=== TEST: Dict - static contexts enforce the dictionary capacity ===\n");
    const size_t dict_size = sizeof(k_dctx_dict) - 1;
    const size_t small = dict_size / 2;
    const size_t block = ZXC_BLOCK_SIZE_MIN;
    const size_t src_size = block;
    uint8_t* src = (uint8_t*)malloc(src_size);
    gen_dict_friendly_data(src, src_size, k_dctx_dict, dict_size);
    const size_t cap = (size_t)zxc_compress_bound(src_size);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(src_size + ZXC_DECOMPRESS_TAIL_PAD);
    void* cw = NULL;
    void* dw = NULL;
    int ok = 0;
    do {
        /* Sizing: a capacity costs room, none costs nothing, too much is refused. */
        const size_t c0 = zxc_static_cctx_workspace_size(block, 3, 0);
        const size_t cd = zxc_static_cctx_workspace_size(block, 3, dict_size);
        const size_t d0 = zxc_static_dctx_workspace_size(block, 0);
        const size_t dd = zxc_static_dctx_workspace_size(block, dict_size);
        if (c0 == 0 || cd <= c0 || d0 == 0 || dd <= d0 ||
            zxc_static_cctx_workspace_size(block, 3, ZXC_DICT_SIZE_MAX + 1) != 0 ||
            zxc_static_dctx_workspace_size(block, ZXC_DICT_SIZE_MAX + 1) != 0) {
            printf("  [FAIL] sizing: %zu/%zu %zu/%zu\n", c0, cd, d0, dd);
            break;
        }
        printf("  [PASS] workspace sizing with and without a capacity\n");

        /* A workspace sized without the capacity cannot carve it. */
        cw = static_ws_alloc(cd);
        dw = static_ws_alloc(dd);
        const zxc_compress_opts_t init_full = {
            .level = 3, .block_size = block, .dict_size = dict_size};
        if (!cw || !dw || zxc_init_static_cctx(cw, c0, &init_full) != NULL ||
            zxc_init_static_dctx(dw, d0, block, dict_size) != NULL) {
            printf("  [FAIL] undersized workspace accepted\n");
            break;
        }
        printf("  [PASS] undersized workspace refused\n");

        /* Contexts carved for half the dictionary reject the full one everywhere. */
        const zxc_compress_opts_t init_small = {
            .level = 3, .block_size = block, .dict_size = small};
        zxc_cctx* sc = zxc_init_static_cctx(cw, cd, &init_small);
        zxc_dctx* sd = zxc_init_static_dctx(dw, dd, block, small);
        const zxc_compress_opts_t c_full = {
            .level = 3, .block_size = block, .dict = k_dctx_dict, .dict_size = dict_size};
        const zxc_decompress_opts_t d_full = {.dict = k_dctx_dict, .dict_size = dict_size};
        const int64_t arch = zxc_compress(src, src_size, comp, cap, &c_full);
        if (!sc || !sd || arch <= 0) {
            printf("  [FAIL] setup for capacity checks\n");
            break;
        }
        const int64_t e1 = zxc_compress_cctx(sc, src, src_size, comp + 0, cap, &c_full);
        const int64_t e2 = zxc_compress_block(sc, src, src_size, dec, src_size + 64, &c_full);
        const int64_t e3 = zxc_decompress_dctx(sd, comp, (size_t)arch, dec, src_size, &d_full);
        const int64_t e4 = zxc_decompress_dctx(sd, comp, (size_t)arch, dec, src_size, NULL);
        const int64_t e5 = zxc_decompress_block(sd, comp + ZXC_FILE_HEADER_SIZE,
                                                (size_t)arch - ZXC_FILE_HEADER_SIZE, dec,
                                                src_size + ZXC_DECOMPRESS_TAIL_PAD, &d_full);
        if (e1 != ZXC_ERROR_DICT_UNSUPPORTED || e2 != ZXC_ERROR_DICT_UNSUPPORTED ||
            e3 != ZXC_ERROR_DICT_UNSUPPORTED || e4 != ZXC_ERROR_DICT_REQUIRED ||
            e5 != ZXC_ERROR_DICT_UNSUPPORTED) {
            printf("  [FAIL] capacity: %lld %lld %lld %lld %lld\n", (long long)e1, (long long)e2,
                   (long long)e3, (long long)e4, (long long)e5);
            break;
        }
        printf(
            "  [PASS] dictionary beyond the capacity -> DICT_UNSUPPORTED, none -> DICT_REQUIRED\n");

        /* The strict decoder is bounded by the carved block before any routing. */
        const int64_t b2 = zxc_decompress_block_safe(sd, comp, (size_t)arch, dec, block + 1, NULL);
        if (b2 != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] strict decoder bound on static dctx: %lld\n", (long long)b2);
            break;
        }
        printf("  [PASS] strict decoder bounded by the carved block\n");
        ok = 1;
    } while (0);
    static_ws_free(cw);
    static_ws_free(dw);
    free(src);
    free(comp);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* A static dctx never decodes past its carved block, whatever the caller's
 * buffer, and the strict decoder never keeps a table from an earlier call. */
int test_dict_static_ctx_block_guards(void) {
    printf("=== TEST: Dict - static dctx block guards ===\n");
    enum { NS = 6, SCAP = 16384, PIN = 4096, BIG = 6000 };
    uint8_t* bufs[NS];
    const void* samples[NS];
    size_t sizes[NS];
    for (int i = 0; i < NS; i++) {
        bufs[i] = (uint8_t*)malloc(SCAP);
        sizes[i] = gen_structured_sample(bufs[i], SCAP, 0x6000U + (uint32_t)i);
        samples[i] = bufs[i];
    }
    uint8_t dict_buf[8192];
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    const size_t cap = (size_t)zxc_compress_block_bound(SCAP);
    uint8_t* comp = (uint8_t*)malloc(cap);
    uint8_t* out = (uint8_t*)malloc(SCAP + ZXC_DECOMPRESS_TAIL_PAD);
    uint8_t* noise = (uint8_t*)malloc(BIG);
    uint64_t x = 0x9E3779B97F4A7C15ull; /* xorshift: incompressible, stored RAW */
    for (size_t i = 0; i < BIG; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        noise[i] = (uint8_t)(x >> 24);
    }
    zxc_cctx* cctx = zxc_create_cctx(NULL);
    const size_t dws = zxc_static_dctx_workspace_size(PIN, sizeof(dict_buf));
    void* dw = static_ws_alloc(dws);
    zxc_dctx* sd = dw ? zxc_init_static_dctx(dw, dws, PIN, sizeof(dict_buf)) : NULL;
    int ok = 0;
    do {
        const int64_t dsz = zxc_train_dict(samples, sizes, NS, dict_buf, sizeof(dict_buf));
        if (dsz <= 0 || !cctx || !sd ||
            zxc_train_dict_huf(samples, sizes, NS, dict_buf, (size_t)dsz, huf) != ZXC_OK) {
            printf("  [FAIL] setup\n");
            break;
        }
        /* Blocks larger than the carved block: GLO (compressible) and RAW. */
        const zxc_compress_opts_t plain = {.level = 3};
        const int64_t g = zxc_compress_block(cctx, bufs[0], BIG, comp, cap, &plain);
        const int64_t e1 =
            g > 0 ? zxc_decompress_block(sd, comp, (size_t)g, out, BIG + 200, NULL) : 0;
        const int64_t e2 =
            g > 0 ? zxc_decompress_block_safe(sd, comp, (size_t)g, out, BIG, NULL) : 0;
        const int64_t r = zxc_compress_block(cctx, noise, BIG, comp, cap, &plain);
        const int64_t e3 =
            r > 0 ? zxc_decompress_block(sd, comp, (size_t)r, out, BIG + 200, NULL) : 0;
        const int64_t e4 =
            r > 0 ? zxc_decompress_block_safe(sd, comp, (size_t)r, out, BIG, NULL) : 0;
        if (g <= 0 || r <= 0 || comp[0] != ZXC_BLOCK_RAW || e1 != ZXC_ERROR_BAD_BLOCK_SIZE ||
            e2 != ZXC_ERROR_BAD_BLOCK_SIZE || e3 != ZXC_ERROR_BAD_BLOCK_SIZE ||
            e4 != ZXC_ERROR_BAD_BLOCK_SIZE) {
            printf("  [FAIL] oversized blocks: GLO %lld/%lld, RAW(type %u) %lld/%lld\n",
                   (long long)e1, (long long)e2, r > 0 ? comp[0] : 0, (long long)e3, (long long)e4);
            break;
        }
        printf("  [PASS] blocks larger than the carved block -> BAD_BLOCK_SIZE on both decoders\n");

        /* A table attached by the fast decoder must not serve the strict one.
         * The block must use the table without referencing the dictionary
         * content, so only the table can stop the strict decoder: compress a
         * corpus window against a zero dictionary (no match lands in it) with
         * the trained table attached. Decoding it with the real dictionary of
         * the same size, content identical, proves there is no back-reference. */
        uint8_t* zeros = (uint8_t*)calloc((size_t)dsz, 1);
        uint8_t huf_z[ZXC_HUF_TABLE_SIZE]; /* trained against the zero dictionary */
        const zxc_compress_opts_t co_z = {
            .level = 6, .dict = zeros, .dict_size = (size_t)dsz, .dict_huf = huf_z};
        const zxc_decompress_opts_t d_z = {
            .dict = zeros, .dict_size = (size_t)dsz, .dict_huf = huf_z};
        const zxc_decompress_opts_t d_real = {
            .dict = dict_buf, .dict_size = (size_t)dsz, .dict_huf = huf_z};
        zxc_dctx* heap = zxc_create_dctx();
        int found = 0,
            bad = !zeros || !heap ||
                  zxc_train_dict_huf(samples, sizes, NS, zeros, (size_t)dsz, huf_z) != ZXC_OK;
        for (size_t off = 0; off + 2048 <= sizes[2] && !found && !bad; off += 2048) {
            const uint8_t* blk = bufs[2] + off;
            const int64_t n = zxc_compress_block(cctx, blk, 2048, comp, cap, &co_z);
            if (n <= 0) {
                bad = 1;
                break;
            }
            if (!(comp[0] == ZXC_BLOCK_GLO && comp[ZXC_BLOCK_HEADER_SIZE + 8] == 3)) continue;
            if (zxc_decompress_block(heap, comp, (size_t)n, out, 2048 + ZXC_DECOMPRESS_TAIL_PAD,
                                     &d_real) != 2048 ||
                memcmp(out, blk, 2048) != 0)
                continue; /* references the dictionary content: not this one */
            found = 1;
            const int64_t f = zxc_decompress_block(sd, comp, (size_t)n, out,
                                                   2048 + ZXC_DECOMPRESS_TAIL_PAD, &d_z);
            const int64_t s2 = zxc_decompress_block_safe(sd, comp, (size_t)n, out, 2048, NULL);
            if (f != 2048 || s2 >= 0) {
                printf("  [FAIL] enc_lit=3 block: fast %lld, strict without dict %lld\n",
                       (long long)f, (long long)s2);
                bad = 1;
            }
        }
        zxc_free_dctx(heap);
        free(zeros);
        if (bad) break;
        if (!found) {
            printf("  [FAIL] no table-only enc_lit=3 block found to exercise the strict decoder\n");
            break;
        }
        printf("  [PASS] strict decoder refuses an enc_lit=3 block without the dictionary\n");
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    static_ws_free(dw);
    free(comp);
    free(out);
    free(noise);
    for (int i = 0; i < NS; i++) free(bufs[i]);
    if (ok) printf("PASS\n\n");
    return ok;
}
