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
