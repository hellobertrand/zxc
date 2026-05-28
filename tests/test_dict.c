/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

#include "../include/zxc_dict.h"

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

    size_t bound = zxc_dict_save_bound(content_size);
    uint8_t* zxd = (uint8_t*)malloc(bound);
    int64_t written = zxc_dict_save(content, content_size, zxd, bound);
    if (written < 0) {
        printf("  [FAIL] zxc_dict_save returned %lld\n", (long long)written);
        free(zxd);
        return 0;
    }

    const void* loaded_content = NULL;
    size_t loaded_size = 0;
    uint32_t loaded_id = 0;
    int rc = zxc_dict_load(zxd, (size_t)written, &loaded_content, &loaded_size, &loaded_id);
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

    uint32_t expected_id = zxc_dict_id(content, content_size);
    if (loaded_id != expected_id) {
        printf("  [FAIL] dict_id mismatch: got %u, expected %u\n", loaded_id, expected_id);
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

    /* Save to .zxd and verify zxc_dict_get_id */
    size_t zxd_bound = zxc_dict_save_bound(dict_size);
    uint8_t* zxd = (uint8_t*)malloc(zxd_bound);
    int64_t zxd_size = zxc_dict_save(dict, dict_size, zxd, zxd_bound);
    if (zxd_size <= 0) {
        printf("  [FAIL] zxc_dict_save returned %lld\n", (long long)zxd_size);
        free(zxd);
        return 0;
    }

    uint32_t zxd_id = zxc_dict_get_id(zxd, (size_t)zxd_size);
    if (zxd_id != expected_id) {
        printf("  [FAIL] zxc_dict_get_id: got 0x%08X, expected 0x%08X\n", zxd_id, expected_id);
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
    fread(result, 1, src_size, f_dec);
    int ok = (memcmp(src, result, src_size) == 0);

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

    int rc = zxc_seekable_set_dict(s, dict_content, dict_size);
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
    zxc_seekable_set_dict(s, dict_content, dict_size);

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
