/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

// Checks that the stream decompression can accept NULL output (Integrity Check Mode)
int test_null_output_decompression() {
    printf("=== TEST: Unit - NULL Output Decompression (Integrity Check) ===\n");

    size_t size = 64 * 1024;
    uint8_t* input = malloc(size);
    if (!input) return 0;
    gen_lz_data(input, size);

    FILE* f_in = tmpfile();
    FILE* f_comp = tmpfile();

    if (!f_in || !f_comp) {
        if (f_in) fclose(f_in);
        if (f_comp) fclose(f_comp);
        free(input);
        return 0;
    }

    fwrite(input, 1, size, f_in);
    fseek(f_in, 0, SEEK_SET);

    // Compress with checksum
    zxc_compress_opts_t _sco3 = {.n_threads = 1, .level = 3, .checksum_enabled = 1};
    if (zxc_stream_compress(f_in, f_comp, &_sco3) < 0) {
        printf("Compression Failed!\n");
        fclose(f_in);
        fclose(f_comp);
        free(input);
        return 0;
    }
    fseek(f_comp, 0, SEEK_SET);

    // Decompress with NULL output
    // This should return the decompressed size but write nothing
    zxc_decompress_opts_t _sdo4 = {.n_threads = 1, .checksum_enabled = 1};
    int64_t d_sz = zxc_stream_decompress(f_comp, NULL, &_sdo4);

    if (d_sz != (int64_t)size) {
        printf("Failed: Expected size %zu, got %lld\n", size, (long long)d_sz);
        fclose(f_in);
        fclose(f_comp);
        free(input);
        return 0;
    }

    printf("PASS\n\n");
    fclose(f_in);
    fclose(f_comp);
    free(input);
    return 1;
}

// Checks API robustness against invalid arguments
int test_invalid_arguments() {
    printf("=== TEST: Unit - Invalid Arguments ===\n");

    FILE* f = tmpfile();
    if (!f) return 0;

    FILE* f_valid = tmpfile();
    if (!f_valid) {
        fclose(f);
        return 0;
    }
    // Prepare a valid compressed stream for decompression tests
    zxc_compress_opts_t _sco5 = {.n_threads = 1, .level = 1, .checksum_enabled = 0};
    zxc_stream_compress(f, f_valid, &_sco5);
    rewind(f_valid);

    // 1. Input NULL -> Must fail
    zxc_compress_opts_t _sco6 = {.n_threads = 1, .level = 5, .checksum_enabled = 0};
    if (zxc_stream_compress(NULL, f, &_sco6) >= 0) {
        printf("Failed: Should return < 0 when Input is NULL\n");
        fclose(f);
        return 0;
    }

    // 2. Output NULL -> Must SUCCEED (Benchmark / Dry-Run Mode)
    zxc_compress_opts_t _sco7 = {.n_threads = 1, .level = 5, .checksum_enabled = 0};
    if (zxc_stream_compress(f, NULL, &_sco7) < 0) {
        printf("Failed: Should allow NULL Output (Benchmark mode support)\n");
        fclose(f);
        return 0;
    }

    // 3. Decompression Input NULL -> Must fail
    zxc_decompress_opts_t _sdo8 = {.n_threads = 1, .checksum_enabled = 0};
    if (zxc_stream_decompress(NULL, f, &_sdo8) >= 0) {
        printf("Failed: Decompress should return < 0 when Input is NULL\n");
        fclose(f);
        return 0;
    }

    // 3b. Decompression Output NULL -> Must SUCCEED (Benchmark mode)
    zxc_decompress_opts_t _sdo9 = {.n_threads = 1, .checksum_enabled = 0};
    if (zxc_stream_decompress(f_valid, NULL, &_sdo9) < 0) {
        printf("Failed: Decompress should allow NULL Output (Benchmark mode support)\n");
        fclose(f_valid);
        return 0;
    }

    // 4. zxc_compress NULL checks
    zxc_compress_opts_t _co10 = {.level = 3, .checksum_enabled = 0};
    if (zxc_compress(NULL, 100, (void*)1, 100, &_co10) >= 0) {
        printf("Failed: zxc_compress should return < 0 when src is NULL\n");
        fclose(f);
        return 0;
    }
    zxc_compress_opts_t _co11 = {.level = 3, .checksum_enabled = 0};
    if (zxc_compress((void*)1, 100, NULL, 100, &_co11) >= 0) {
        printf("Failed: zxc_compress should return < 0 when dst is NULL\n");
        fclose(f);
        return 0;
    }

    // 5. zxc_decompress NULL checks
    zxc_decompress_opts_t _do12 = {.checksum_enabled = 0};
    if (zxc_decompress(NULL, 100, (void*)1, 100, &_do12) >= 0) {
        printf("Failed: zxc_decompress should return < 0 when src is NULL\n");
        fclose(f);
        return 0;
    }
    zxc_decompress_opts_t _do13 = {.checksum_enabled = 0};
    if (zxc_decompress((void*)1, 100, NULL, 100, &_do13) >= 0) {
        printf("Failed: zxc_decompress should return < 0 when dst is NULL\n");
        fclose(f);
        return 0;
    }

    // 6. zxc_compress_bound overflow check
    if (zxc_compress_bound(SIZE_MAX) != 0) {
        printf("Failed: zxc_compress_bound should return 0 on overflow\n");
        fclose(f);
        return 0;
    }

    printf("PASS\n\n");
    fclose(f);
    return 1;
}

// Checks behavior with truncated compressed input
int test_truncated_input() {
    printf("=== TEST: Unit - Truncated Input (Stream) ===\n");

    const size_t SRC_SIZE = 1024;
    uint8_t src[1024];
    gen_lz_data(src, SRC_SIZE);

    size_t cap = (size_t)zxc_compress_bound(SRC_SIZE);
    uint8_t* compressed = malloc(cap);
    uint8_t* decomp_buf = malloc(SRC_SIZE);

    if (!compressed || !decomp_buf) {
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    zxc_compress_opts_t _co14 = {.level = 3, .checksum_enabled = 1};
    int64_t comp_sz = zxc_compress(src, SRC_SIZE, compressed, cap, &_co14);
    if (comp_sz <= 0) {
        printf("Prepare failed\n");
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    // Try decompressing with progressively cropped size
    // 1. Cut off the Footer (last ZXC_FILE_FOOTER_SIZE bytes)
    if (comp_sz > ZXC_FILE_FOOTER_SIZE) {
        zxc_decompress_opts_t _do15 = {.checksum_enabled = 1};
        if (zxc_decompress(compressed, (size_t)(comp_sz - ZXC_FILE_FOOTER_SIZE), decomp_buf,
                           SRC_SIZE, &_do15) >= 0) {
            printf("Failed: Should fail when footer is missing\n");
            free(compressed);
            free(decomp_buf);
            return 0;
        }
    }

    // 2. Cut off half the file
    zxc_decompress_opts_t _do16 = {.checksum_enabled = 1};
    if (zxc_decompress(compressed, (size_t)(comp_sz / 2), decomp_buf, SRC_SIZE, &_do16) >= 0) {
        printf("Failed: Should fail when stream is truncated by half\n");
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    // 3. Cut off just 1 byte
    zxc_decompress_opts_t _do17 = {.checksum_enabled = 1};
    if (zxc_decompress(compressed, (size_t)(comp_sz - 1), decomp_buf, SRC_SIZE, &_do17) >= 0) {
        printf("Failed: Should fail when stream is truncated by 1 byte\n");
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    printf("PASS\n\n");
    free(compressed);
    free(decomp_buf);
    return 1;
}

// Checks behavior if writing fails
int test_io_failures() {
    printf("=== TEST: Unit - I/O Failures ===\n");

    FILE* f_in = tmpfile();
    if (!f_in) return 0;

    // Create a dummy file to simulate failure
    // Open it in "rb" (read-only) and pass it as "wb" output file.
    // fwrite should return 0 and trigger the error.
    const char* bad_filename = "zxc_test_readonly.tmp";
    FILE* f_dummy = create_restricted_file(bad_filename);
    if (f_dummy) fclose(f_dummy);

    FILE* f_out = fopen(bad_filename, "rb");
    if (!f_out) {
        perror("fopen readonly");
        fclose(f_in);
        return 0;
    }

    // Write some data to input
    fputs("test data to compress", f_in);
    fseek(f_in, 0, SEEK_SET);

    // This should fail cleanly (return < 0) because writing to f_out is impossible
    zxc_compress_opts_t _sco18 = {.n_threads = 1, .level = 5, .checksum_enabled = 0};
    if (zxc_stream_compress(f_in, f_out, &_sco18) >= 0) {
        printf("Failed: Should detect write error on read-only stream\n");
        fclose(f_in);
        fclose(f_out);
        remove(bad_filename);
        return 0;
    }

    printf("PASS\n\n");
    fclose(f_in);
    fclose(f_out);
    remove(bad_filename);
    return 1;
}

// Checks thread selector behavior
int test_thread_params() {
    printf("=== TEST: Unit - Thread Parameters ===\n");

    FILE* f_in = tmpfile();
    FILE* f_out = tmpfile();
    if (!f_in || !f_out) {
        if (f_in) fclose(f_in);
        if (f_out) fclose(f_out);
        return 0;
    }

    // Test with 0 (Auto) and negative value - must not crash
    zxc_compress_opts_t _sco19 = {.n_threads = 0, .level = 5, .checksum_enabled = 0};
    zxc_stream_compress(f_in, f_out, &_sco19);
    fseek(f_in, 0, SEEK_SET);
    fseek(f_out, 0, SEEK_SET);
    zxc_compress_opts_t _sco20 = {.n_threads = -5, .level = 5, .checksum_enabled = 0};
    zxc_stream_compress(f_in, f_out, &_sco20);

    printf("PASS (No crash observed)\n\n");
    fclose(f_in);
    fclose(f_out);
    return 1;
}

// Multi-threaded round-trip test for TSan coverage
int test_multithread_roundtrip() {
    printf("=== TEST: Multi-Thread Round-Trip (TSan Coverage) ===\n");

    const size_t SIZE = 4 * 1024 * 1024;  // 4MB to ensure multiple chunks
    const int ITERATIONS = 3;             // Multiple runs increase race detection
    int result = 0;
    uint8_t* input = malloc(SIZE);
    uint8_t* output = malloc(SIZE);

    if (!input || !output) goto cleanup;
    gen_lz_data(input, SIZE);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        FILE* f_in = tmpfile();
        FILE* f_comp = tmpfile();
        FILE* f_decomp = tmpfile();
        if (!f_in || !f_comp || !f_decomp) {
            if (f_in) fclose(f_in);
            if (f_comp) fclose(f_comp);
            if (f_decomp) fclose(f_decomp);
            goto cleanup;
        }

        fwrite(input, 1, SIZE, f_in);
        fseek(f_in, 0, SEEK_SET);

        // Vary thread count: 2, 4, 8
        int num_threads = 2 << iter;
        zxc_compress_opts_t _sco21 = {.n_threads = num_threads, .level = 3, .checksum_enabled = 1};
        if (zxc_stream_compress(f_in, f_comp, &_sco21) < 0) {
            printf("Compression failed (threads=%d)!\n", num_threads);
            fclose(f_in);
            fclose(f_comp);
            fclose(f_decomp);
            goto cleanup;
        }

        fseek(f_comp, 0, SEEK_SET);

        zxc_decompress_opts_t _sdo22 = {.n_threads = num_threads, .checksum_enabled = 1};
        if (zxc_stream_decompress(f_comp, f_decomp, &_sdo22) < 0) {
            printf("Decompression failed (threads=%d)!\n", num_threads);
            fclose(f_in);
            fclose(f_comp);
            fclose(f_decomp);
            goto cleanup;
        }

        long decomp_size = ftell(f_decomp);
        fseek(f_decomp, 0, SEEK_SET);

        if (decomp_size != (long)SIZE || fread(output, 1, SIZE, f_decomp) != SIZE ||
            memcmp(input, output, SIZE) != 0) {
            printf("Verification failed (threads=%d)!\n", num_threads);
            fclose(f_in);
            fclose(f_comp);
            fclose(f_decomp);
            goto cleanup;
        }

        fclose(f_in);
        fclose(f_comp);
        fclose(f_decomp);
        printf("  Iteration %d: PASS (%d threads)\n", iter + 1, num_threads);
    }

    printf("PASS (3 iterations, 2/4/8 threads)\n\n");
    result = 1;

cleanup:
    free(input);
    free(output);
    return result;
}

int test_stream_get_decompressed_size_errors() {
    printf("=== TEST: Unit - zxc_stream_get_decompressed_size Error Codes ===\n");

    // 1. NULL FILE*
    int64_t r = zxc_stream_get_decompressed_size(NULL);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] NULL FILE*: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT, (long long)r);
        return 0;
    }
    printf("  [PASS] NULL FILE* -> ZXC_ERROR_NULL_INPUT\n");

    // 2. File too small (less than header + footer)
    {
        FILE* f = tmpfile();
        if (!f) {
            printf("  [SKIP] tmpfile failed\n");
            return 0;
        }
        fwrite("tiny", 1, 4, f);
        fseek(f, 0, SEEK_SET);
        r = zxc_stream_get_decompressed_size(f);
        if (r != ZXC_ERROR_SRC_TOO_SMALL) {
            printf("  [FAIL] file too small: expected %d, got %lld\n", ZXC_ERROR_SRC_TOO_SMALL,
                   (long long)r);
            fclose(f);
            return 0;
        }
        fclose(f);
    }
    printf("  [PASS] file too small -> ZXC_ERROR_SRC_TOO_SMALL\n");

    // 3. Bad magic word
    {
        FILE* f = tmpfile();
        if (!f) {
            printf("  [SKIP] tmpfile failed\n");
            return 0;
        }
        // Write enough bytes but with wrong magic
        uint8_t garbage[ZXC_FILE_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE];
        memset(garbage, 0, sizeof(garbage));
        fwrite(garbage, 1, sizeof(garbage), f);
        fseek(f, 0, SEEK_SET);
        r = zxc_stream_get_decompressed_size(f);
        if (r != ZXC_ERROR_BAD_MAGIC) {
            printf("  [FAIL] bad magic: expected %d, got %lld\n", ZXC_ERROR_BAD_MAGIC,
                   (long long)r);
            fclose(f);
            return 0;
        }
        fclose(f);
    }
    printf("  [PASS] bad magic -> ZXC_ERROR_BAD_MAGIC\n");

    // 4. Valid file returns correct size
    {
        // Create a valid compressed file in memory
        const size_t src_sz = 512;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);
        const size_t cap = (size_t)zxc_compress_bound(src_sz);
        uint8_t* comp = malloc(cap);
        zxc_compress_opts_t _co48 = {.level = 3, .checksum_enabled = 0};
        int64_t comp_sz = zxc_compress(src, src_sz, comp, cap, &_co48);
        if (comp_sz <= 0) {
            printf("  [SKIP] compress failed\n");
            free(src);
            free(comp);
            return 0;
        }

        FILE* f = tmpfile();
        fwrite(comp, 1, (size_t)comp_sz, f);
        fseek(f, 0, SEEK_SET);
        r = zxc_stream_get_decompressed_size(f);
        if (r != (int64_t)src_sz) {
            printf("  [FAIL] valid file: expected %zu, got %lld\n", src_sz, (long long)r);
            fclose(f);
            free(src);
            free(comp);
            return 0;
        }
        fclose(f);
        free(src);
        free(comp);
    }
    printf("  [PASS] valid file -> correct size\n");

    printf("PASS\n\n");
    return 1;
}

int test_stream_engine_errors() {
    printf("=== TEST: Unit - Stream Engine Error Codes ===\n");

    // 1. zxc_stream_compress with NULL f_in
    {
        FILE* f_out = tmpfile();
        zxc_compress_opts_t _sco49 = {.n_threads = 1, .level = 3, .checksum_enabled = 0};
        int64_t r = zxc_stream_compress(NULL, f_out, &_sco49);
        if (r != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] compress NULL f_in: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT,
                   (long long)r);
            if (f_out) fclose(f_out);
            return 0;
        }
        if (f_out) fclose(f_out);
    }
    printf("  [PASS] zxc_stream_compress NULL f_in -> ZXC_ERROR_NULL_INPUT\n");

    // 2. zxc_stream_decompress with NULL f_in
    {
        FILE* f_out = tmpfile();
        zxc_decompress_opts_t _sdo50 = {.n_threads = 1, .checksum_enabled = 0};
        int64_t r = zxc_stream_decompress(NULL, f_out, &_sdo50);
        if (r != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] decompress NULL f_in: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT,
                   (long long)r);
            if (f_out) fclose(f_out);
            return 0;
        }
        if (f_out) fclose(f_out);
    }
    printf("  [PASS] zxc_stream_decompress NULL f_in -> ZXC_ERROR_NULL_INPUT\n");

    // 3. zxc_stream_decompress with a bad magic word (invalid file)
    {
        FILE* f_in = tmpfile();
        FILE* f_out = tmpfile();
        if (!f_in || !f_out) {
            if (f_in) fclose(f_in);
            if (f_out) fclose(f_out);
            printf("  [SKIP] tmpfile failed\n");
            return 0;
        }
        // Write garbage data (bad magic)
        uint8_t garbage[64];
        memset(garbage, 0xAA, sizeof(garbage));
        fwrite(garbage, 1, sizeof(garbage), f_in);
        fseek(f_in, 0, SEEK_SET);

        zxc_decompress_opts_t _sdo51 = {.n_threads = 1, .checksum_enabled = 0};
        int64_t r = zxc_stream_decompress(f_in, f_out, &_sdo51);
        if (r != ZXC_ERROR_BAD_MAGIC) {
            printf("  [FAIL] bad magic: expected %d, got %lld\n", ZXC_ERROR_BAD_MAGIC,
                   (long long)r);
            fclose(f_in);
            fclose(f_out);
            return 0;
        }
        fclose(f_in);
        fclose(f_out);
    }
    printf("  [PASS] zxc_stream_decompress bad magic -> ZXC_ERROR_BAD_MAGIC\n");

    // 4. Stream decompress with corrupted footer (stored size mismatch)
    {
        // First, create a valid compressed stream
        const size_t src_sz = 4096;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = tmpfile();
        FILE* f_comp_out = tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);

        zxc_compress_opts_t _sco52 = {.n_threads = 1, .level = 3, .checksum_enabled = 1};
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, &_sco52);
        fclose(f_comp_in);
        if (comp_sz <= 0) {
            printf("  [SKIP] stream compress failed\n");
            fclose(f_comp_out);
            free(src);
            return 0;
        }

        // Read the compressed data, corrupt the footer source size, rewrite
        fseek(f_comp_out, 0, SEEK_END);
        const long comp_file_sz = ftell(f_comp_out);
        uint8_t* comp_data = malloc(comp_file_sz);
        fseek(f_comp_out, 0, SEEK_SET);
        if (fread(comp_data, 1, comp_file_sz, f_comp_out) != (size_t)comp_file_sz) {
            printf("  [FAIL] fread failed\n");
            fclose(f_comp_out);
            free(comp_data);
            free(src);
            return 0;
        }
        fclose(f_comp_out);

        // Corrupt the stored source size in footer (last 12 bytes: [src_size(8)] + [hash(4)])
        const size_t footer_off = comp_file_sz - ZXC_FILE_FOOTER_SIZE;
        comp_data[footer_off] ^= 0x01;  // Flip a bit in stored source size

        FILE* f_corrupt = tmpfile();
        FILE* f_dec_out = tmpfile();
        fwrite(comp_data, 1, comp_file_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        zxc_decompress_opts_t _sdo53 = {.n_threads = 1, .checksum_enabled = 1};
        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, &_sdo53);
        fclose(f_corrupt);
        fclose(f_dec_out);
        free(src);
        if (r >= 0) {
            printf("  [FAIL] corrupt footer size: expected < 0, got %lld\n", (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_stream_decompress corrupt footer -> negative\n");

    // 5. Stream decompress with corrupted global checksum
    {
        const size_t src_sz = 4096;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = tmpfile();
        FILE* f_comp_out = tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);

        zxc_compress_opts_t _sco54 = {.n_threads = 1, .level = 3, .checksum_enabled = 1};
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, &_sco54);
        fclose(f_comp_in);
        if (comp_sz <= 0) {
            printf("  [SKIP] stream compress failed\n");
            fclose(f_comp_out);
            free(src);
            return 0;
        }

        fseek(f_comp_out, 0, SEEK_END);
        const long comp_file_sz = ftell(f_comp_out);
        uint8_t* comp_data = malloc(comp_file_sz);
        fseek(f_comp_out, 0, SEEK_SET);
        if (fread(comp_data, 1, comp_file_sz, f_comp_out) != (size_t)comp_file_sz) {
            printf("  [FAIL] fread failed\n");
            fclose(f_comp_out);
            free(comp_data);
            free(src);
            return 0;
        }
        fclose(f_comp_out);

        // Corrupt the global checksum (last 4 bytes)
        comp_data[comp_file_sz - 1] ^= 0xFF;

        FILE* f_corrupt = tmpfile();
        FILE* f_dec_out = tmpfile();
        fwrite(comp_data, 1, comp_file_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        zxc_decompress_opts_t _sdo55 = {.n_threads = 1, .checksum_enabled = 1};
        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, &_sdo55);
        fclose(f_corrupt);
        fclose(f_dec_out);
        free(src);
        if (r >= 0) {
            printf("  [FAIL] corrupt global checksum: expected < 0, got %lld\n", (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_stream_decompress corrupt checksum -> negative\n");

    // 6. Stream decompress truncated file (missing EOF + footer)
    {
        const size_t src_sz = 4096;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = tmpfile();
        FILE* f_comp_out = tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);

        zxc_compress_opts_t _sco56 = {.n_threads = 1, .level = 3, .checksum_enabled = 0};
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, &_sco56);
        fclose(f_comp_in);
        free(src);
        if (comp_sz <= 0) {
            printf("  [SKIP] stream compress failed\n");
            fclose(f_comp_out);
            return 0;
        }

        fseek(f_comp_out, 0, SEEK_END);
        const long comp_file_sz = ftell(f_comp_out);
        // Truncate: remove the EOF block header + footer
        const long trunc_sz = comp_file_sz - (ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE);
        uint8_t* comp_data = malloc(trunc_sz);
        fseek(f_comp_out, 0, SEEK_SET);
        if (fread(comp_data, 1, trunc_sz, f_comp_out) != (size_t)trunc_sz) {
            printf("  [FAIL] fread failed\n");
            fclose(f_comp_out);
            free(comp_data);
            return 0;
        }
        fclose(f_comp_out);

        FILE* f_corrupt = tmpfile();
        FILE* f_dec_out = tmpfile();
        fwrite(comp_data, 1, trunc_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        zxc_decompress_opts_t _sdo57 = {.n_threads = 1, .checksum_enabled = 0};
        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, &_sdo57);
        fclose(f_corrupt);
        fclose(f_dec_out);
        // Should fail: missing EOF/footer means io_error or bad read
        if (r >= 0) {
            printf("  [FAIL] truncated stream: expected < 0, got %lld\n", (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_stream_decompress truncated -> negative\n");

    // 7. Stream decompress with mid-block body truncation
    {
        const size_t src_sz = 64 * 1024;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = tmpfile();
        FILE* f_comp_out = tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);

        zxc_compress_opts_t sco_mb = {.n_threads = 1, .level = 3, .checksum_enabled = 0};
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, &sco_mb);
        fclose(f_comp_in);
        free(src);
        if (comp_sz <= 0) {
            printf("  [SKIP] stream compress failed\n");
            fclose(f_comp_out);
            return 0;
        }

        fseek(f_comp_out, 0, SEEK_END);
        const long comp_file_sz = ftell(f_comp_out);
        // Truncate mid-block: keep header + first block header + partial body
        const long trunc_sz = ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + 16;
        if (trunc_sz < comp_file_sz) {
            uint8_t* comp_data = malloc(trunc_sz);
            fseek(f_comp_out, 0, SEEK_SET);
            if (fread(comp_data, 1, trunc_sz, f_comp_out) == (size_t)trunc_sz) {
                FILE* f_trunc = tmpfile();
                FILE* f_dec_out = tmpfile();
                fwrite(comp_data, 1, trunc_sz, f_trunc);
                fseek(f_trunc, 0, SEEK_SET);

                zxc_decompress_opts_t sdo_mb = {.n_threads = 1, .checksum_enabled = 0};
                int64_t r = zxc_stream_decompress(f_trunc, f_dec_out, &sdo_mb);
                fclose(f_trunc);
                fclose(f_dec_out);
                if (r >= 0) {
                    printf("  [FAIL] mid-block truncated: expected < 0, got %lld\n", (long long)r);
                    free(comp_data);
                    fclose(f_comp_out);
                    return 0;
                }
            }
            free(comp_data);
        }
        fclose(f_comp_out);
    }
    printf("  [PASS] zxc_stream_decompress mid-block truncated -> negative\n");

    // 8. Streaming fwrite error: compress real data, then decompress to a read-only file
    {
        const size_t src_sz = 64 * 1024;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = tmpfile();
        FILE* f_comp_out = tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);
        free(src);

        zxc_compress_opts_t sco_io = {.n_threads = 1, .level = 1, .checksum_enabled = 0};
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, &sco_io);
        fclose(f_comp_in);
        if (comp_sz <= 0) {
            printf("  [SKIP] compress failed\n");
            fclose(f_comp_out);
            return 0;
        }
        fseek(f_comp_out, 0, SEEK_SET);

        // Open a read-only file as the output: fwrite will fail
        const char* ro_file = "zxc_test_stream_readonly.tmp";
        FILE* f_ro = create_restricted_file(ro_file);
        if (f_ro) fclose(f_ro);
        FILE* f_bad_out = fopen(ro_file, "rb");
        if (f_bad_out) {
            zxc_decompress_opts_t sdo_io = {.n_threads = 1, .checksum_enabled = 0};
            int64_t r = zxc_stream_decompress(f_comp_out, f_bad_out, &sdo_io);
            fclose(f_bad_out);
            if (r >= 0) {
                printf("  [FAIL] fwrite error: expected < 0, got %lld\n", (long long)r);
                fclose(f_comp_out);
                remove(ro_file);
                return 0;
            }
        }
        fclose(f_comp_out);
        remove(ro_file);
    }
    printf("  [PASS] zxc_stream_decompress fwrite error -> negative\n");

    // 9. Multi-threaded streaming I/O failure (writer fwrite error with multiple workers)
    {
        const size_t src_sz = 256 * 1024;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = tmpfile();
        FILE* f_comp_out = tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);
        free(src);

        zxc_compress_opts_t sco_mt = {.n_threads = 4, .level = 1, .checksum_enabled = 0};
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, &sco_mt);
        fclose(f_comp_in);
        if (comp_sz <= 0) {
            printf("  [SKIP] mt compress failed\n");
            fclose(f_comp_out);
            return 0;
        }
        fseek(f_comp_out, 0, SEEK_SET);

        const char* ro_file2 = "zxc_test_stream_mt_readonly.tmp";
        FILE* f_ro2 = create_restricted_file(ro_file2);
        if (f_ro2) fclose(f_ro2);
        FILE* f_bad_out2 = fopen(ro_file2, "rb");
        if (f_bad_out2) {
            zxc_decompress_opts_t sdo_mt = {.n_threads = 4, .checksum_enabled = 0};
            int64_t r = zxc_stream_decompress(f_comp_out, f_bad_out2, &sdo_mt);
            fclose(f_bad_out2);
            if (r >= 0) {
                printf("  [FAIL] mt fwrite error: expected < 0, got %lld\n", (long long)r);
                fclose(f_comp_out);
                remove(ro_file2);
                return 0;
            }
        }
        fclose(f_comp_out);
        remove(ro_file2);
    }
    printf("  [PASS] zxc_stream_decompress mt fwrite error -> negative\n");

    printf("PASS\n\n");
    return 1;
}

/* ======================================================================== */
/*  Streaming round-trip suite                                               */
/*                                                                           */
/*  Historical coverage: patterns x levels x checksum. Each case is its own  */
/*  named entry so CTest can schedule and report them individually.          */
/* ======================================================================== */

/* Thin wrapper around test_round_trip: malloc, generate, run, free. */
#define RT_WRAPPER(fn_name, label, gen, size_expr, level_val, crc_val)               \
    int fn_name(void) {                                                              \
        const size_t _sz = (size_expr);                                              \
        uint8_t* _buf = malloc(_sz > 0 ? _sz : 1);                                   \
        if (!_buf) return 0;                                                         \
        gen(_buf, _sz);                                                              \
        const int _ok = test_round_trip((label), _buf, _sz, (level_val), (crc_val)); \
        free(_buf);                                                                  \
        return _ok;                                                                  \
    }

#define RT_BUF (256 * 1024)
#define RT_LARGE (15 * 1024 * 1024)

/* Encoder path coverage */
RT_WRAPPER(test_roundtrip_raw_random, "RAW Block (Random Data)", gen_random_data, RT_BUF, 3, 0)
RT_WRAPPER(test_roundtrip_ghi_text, "GHI Block (Text Pattern)", gen_lz_data, RT_BUF, 2, 0)
RT_WRAPPER(test_roundtrip_glo_text, "GLO Block (Text Pattern)", gen_lz_data, RT_BUF, 4, 0)
RT_WRAPPER(test_roundtrip_num_seq, "Numeric data (integer sequence)", gen_num_data, RT_BUF, 3, 0)
RT_WRAPPER(test_roundtrip_num_zero, "Numeric data (zero deltas)", gen_num_data_zero, RT_BUF, 3, 0)
RT_WRAPPER(test_roundtrip_num_small, "Numeric data (small deltas)", gen_num_data_small, RT_BUF, 3,
           0)
RT_WRAPPER(test_roundtrip_num_large, "Numeric data (large deltas)", gen_num_data_large, RT_BUF, 3,
           0)

/* Size edge cases */
RT_WRAPPER(test_roundtrip_small_50, "Small Input (50 bytes)", gen_random_data, 50, 3, 0)
RT_WRAPPER(test_roundtrip_empty, "Empty Input (0 bytes)", gen_random_data, 0, 3, 0)
RT_WRAPPER(test_roundtrip_1byte, "1-byte Input", gen_random_data, 1, 3, 0)
RT_WRAPPER(test_roundtrip_1byte_crc, "1-byte Input (with checksum)", gen_random_data, 1, 3, 1)
RT_WRAPPER(test_roundtrip_large_15mb_lz, "Large File (15MB Multi-Block)", gen_lz_data, RT_LARGE, 3,
           1)
RT_WRAPPER(test_roundtrip_large_15mb_num, "Large numeric file (15MB Multi-Block)", gen_num_data,
           RT_LARGE, 3, 1)

/* Checksum coverage */
RT_WRAPPER(test_roundtrip_checksum_off, "Checksum Disabled", gen_lz_data, RT_BUF, 3, 0)
RT_WRAPPER(test_roundtrip_checksum_on, "Checksum Enabled", gen_lz_data, RT_BUF, 31, 1)

/* Per-level coverage */
RT_WRAPPER(test_roundtrip_level1, "Level 1", gen_lz_data, RT_BUF, 1, 1)
RT_WRAPPER(test_roundtrip_level2, "Level 2", gen_lz_data, RT_BUF, 2, 1)
RT_WRAPPER(test_roundtrip_level3, "Level 3", gen_lz_data, RT_BUF, 3, 1)
RT_WRAPPER(test_roundtrip_level4, "Level 4", gen_lz_data, RT_BUF, 4, 1)
RT_WRAPPER(test_roundtrip_level5, "Level 5", gen_lz_data, RT_BUF, 5, 1)
RT_WRAPPER(test_roundtrip_level6, "Level 6 (Huffman literals)", gen_lz_data, RT_BUF, 6, 1)

/* Binary data preservation */
RT_WRAPPER(test_roundtrip_binary, "Binary Data (0x00, 0x0A, 0x0D, 0xFF)", gen_binary_data, RT_BUF,
           3, 0)
RT_WRAPPER(test_roundtrip_binary_crc, "Binary Data with Checksum", gen_binary_data, RT_BUF, 3, 1)
RT_WRAPPER(test_roundtrip_binary_small, "Small Binary Data (128 bytes)", gen_binary_data, 128, 3, 0)

/* Repetitive pattern / offset encoding */
RT_WRAPPER(test_roundtrip_offset8_small, "8-bit Offsets (Small Pattern)", gen_small_offset_data,
           RT_BUF, 3, 1)
RT_WRAPPER(test_roundtrip_offset8_lvl5, "8-bit Offsets (Level 5)", gen_small_offset_data, RT_BUF, 5,
           1)
RT_WRAPPER(test_roundtrip_offset16_large, "16-bit Offsets (Large Distance)", gen_large_offset_data,
           RT_BUF, 3, 1)
RT_WRAPPER(test_roundtrip_offset16_lvl5, "16-bit Offsets (Level 5)", gen_large_offset_data, RT_BUF,
           5, 1)

/* Mixed offsets: two generators populate two halves of the buffer. */
int test_roundtrip_offset_mixed(void) {
    uint8_t* buf = malloc(RT_BUF);
    if (!buf) return 0;
    gen_small_offset_data(buf, RT_BUF / 2);
    gen_large_offset_data(buf + RT_BUF / 2, RT_BUF / 2);
    const int ok = test_round_trip("Mixed Offsets (Hybrid)", buf, RT_BUF, 3, 1);
    free(buf);
    return ok;
}

/**
 * @brief An out-of-range level on the streaming path behaves as the maximum one.
 *
 * Level 99 must produce byte-for-byte what ZXC_LEVEL_ULTRA produces.
 */
int test_stream_level_clamp(void) {
    printf("=== TEST: Stream - Out-of-range level clamps to ULTRA ===\n");

    const size_t size = 96 * 1024;
    uint8_t* input = malloc(size);
    if (!input) return 0;
    gen_lz_data(input, size);

    uint8_t* out[2] = {NULL, NULL};
    long sz[2] = {0, 0};
    const int levels[2] = {ZXC_LEVEL_ULTRA, 99};
    int ok = 1;

    for (int i = 0; i < 2 && ok; i++) {
        FILE* f_in = tmpfile();
        FILE* f_comp = tmpfile();
        if (!f_in || !f_comp) {
            printf("  [SKIP] tmpfile failed\n");
            if (f_in) fclose(f_in);
            if (f_comp) fclose(f_comp);
            free(out[0]);
            free(out[1]);
            free(input);
            return 1;
        }
        fwrite(input, 1, size, f_in);
        fseek(f_in, 0, SEEK_SET);

        zxc_compress_opts_t o = {.n_threads = 1, .level = levels[i]};
        const int64_t c = zxc_stream_compress(f_in, f_comp, &o);
        if (c <= 0) {
            printf("Failed: level %d compress returned %lld\n", levels[i], (long long)c);
            ok = 0;
        } else {
            fseek(f_comp, 0, SEEK_END);
            sz[i] = ftell(f_comp);
            fseek(f_comp, 0, SEEK_SET);
            out[i] = malloc((size_t)sz[i]);
            if (!out[i] || fread(out[i], 1, (size_t)sz[i], f_comp) != (size_t)sz[i]) ok = 0;
        }
        fclose(f_in);
        fclose(f_comp);
    }

    if (ok && (sz[0] != sz[1] || memcmp(out[0], out[1], (size_t)sz[0]) != 0)) {
        printf("Failed: level 99 produced a different stream than level %d (%ld vs %ld bytes)\n",
               ZXC_LEVEL_ULTRA, sz[0], sz[1]);
        ok = 0;
    }

    free(out[0]);
    free(out[1]);
    free(input);
    if (ok) printf("PASS\n\n");
    return ok;
}

/**
 * @brief A block header the parser cannot read must be reported as corruption.
 *
 * Truncating a stream after its first block and appending a footer that agrees
 * with what was kept used to decompress as a clean, successful short read.
 */
int test_stream_corrupt_block_header(void) {
    printf("=== TEST: Stream - Unreadable block header is corruption ===\n");

    const size_t bs = 64 * 1024;
    const size_t size = 3 * bs;
    uint8_t* input = malloc(size);
    if (!input) return 0;
    gen_lz_data(input, size);

    FILE* f_in = tmpfile();
    FILE* f_comp = tmpfile();
    if (!f_in || !f_comp) {
        printf("  [SKIP] tmpfile failed\n");
        if (f_in) fclose(f_in);
        if (f_comp) fclose(f_comp);
        free(input);
        return 1;
    }
    fwrite(input, 1, size, f_in);
    fseek(f_in, 0, SEEK_SET);

    // No block checksum: the forged footer then only has to carry the size.
    zxc_compress_opts_t o = {.n_threads = 1, .level = 3, .block_size = bs};
    const int64_t csz = zxc_stream_compress(f_in, f_comp, &o);
    fclose(f_in);

    if (csz <= 0) {
        printf("Failed: compress returned %lld\n", (long long)csz);
        fclose(f_comp);
        free(input);
        return 0;
    }

    uint8_t* const arc = malloc((size_t)csz);
    if (!arc) {
        fclose(f_comp);
        free(input);
        return 0;
    }
    fseek(f_comp, 0, SEEK_SET);
    const size_t got = fread(arc, 1, (size_t)csz, f_comp);
    fclose(f_comp);
    if (got != (size_t)csz) {
        printf("Failed: read back %zu of %lld bytes\n", got, (long long)csz);
        free(arc);
        free(input);
        return 0;
    }

    int ok = 1;
    zxc_block_header_t bh;
    if (zxc_read_block_header(arc + ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &bh) != ZXC_OK) {
        printf("Failed: unreadable first block header\n");
        ok = 0;
    }

    if (ok) {
        // [file header][block 1][8 bytes no header parser accepts][footer for 1 block]
        const size_t keep = ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + bh.comp_size;
        const size_t flen = keep + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE;
        uint8_t* forged = malloc(flen);
        FILE* f_bad = tmpfile();
        if (!forged || !f_bad) {
            printf("  [SKIP] allocation failed\n");
            free(forged);
            if (f_bad) fclose(f_bad);
            free(arc);
            free(input);
            return 1;
        }
        memcpy(forged, arc, keep);
        memset(forged + keep, 0xFF, ZXC_BLOCK_HEADER_SIZE);
        zxc_write_file_footer(forged + keep + ZXC_BLOCK_HEADER_SIZE, ZXC_FILE_FOOTER_SIZE, bs, 0,
                              0);
        fwrite(forged, 1, flen, f_bad);
        fseek(f_bad, 0, SEEK_SET);

        FILE* f_out = tmpfile();
        const int64_t d = zxc_stream_decompress(f_bad, f_out, NULL);
        if (d != ZXC_ERROR_CORRUPT_DATA) {
            printf("Failed: expected ZXC_ERROR_CORRUPT_DATA, got %lld\n", (long long)d);
            ok = 0;
        }
        if (f_out) fclose(f_out);
        fclose(f_bad);
        free(forged);
    }

    free(arc);
    free(input);
    if (ok) printf("PASS\n\n");
    return ok;
}

/**
 * @brief Compressing with a NULL output reports the size the archive would take.
 *
 * The file header and the seek table used to be written but not counted, so the
 * measuring run under-reported by 16 bytes, 52 with a seek table.
 */
int test_stream_dry_run_size(void) {
    printf("=== TEST: Stream - NULL output measures the exact archive size ===\n");

    const size_t size = 256 * 1024;
    uint8_t* input = malloc(size);
    if (!input) return 0;
    gen_lz_data(input, size);

    const struct {
        const char* name;
        int seekable;
        int checksum;
    } cases[] = {{"plain", 0, 0}, {"seekable", 1, 0}, {"seekable+checksum", 1, 1}};
    int ok = 1;

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]) && ok; i++) {
        zxc_compress_opts_t o = {.n_threads = 1,
                                 .level = 3,
                                 .block_size = 64 * 1024,
                                 .seekable = cases[i].seekable,
                                 .checksum_enabled = cases[i].checksum};
        int64_t written = 0;
        int64_t measured = 0;

        for (int pass = 0; pass < 2 && ok; pass++) {
            FILE* f_in = tmpfile();
            FILE* f_out = pass == 0 ? tmpfile() : NULL;
            if (!f_in || (pass == 0 && !f_out)) {
                printf("  [SKIP] tmpfile failed\n");
                if (f_in) fclose(f_in);
                if (f_out) fclose(f_out);
                free(input);
                return 1;
            }
            fwrite(input, 1, size, f_in);
            fseek(f_in, 0, SEEK_SET);

            const int64_t rc = zxc_stream_compress(f_in, f_out, &o);
            if (rc <= 0) {
                printf("Failed: %s compress returned %lld\n", cases[i].name, (long long)rc);
                ok = 0;
            } else if (pass == 0) {
                fseek(f_out, 0, SEEK_END);
                written = ftell(f_out);
                if (rc != written) {
                    printf("Failed: %s returned %lld for a %lld-byte file\n", cases[i].name,
                           (long long)rc, (long long)written);
                    ok = 0;
                }
            } else {
                measured = rc;
            }
            fclose(f_in);
            if (f_out) fclose(f_out);
        }

        if (ok && measured != written) {
            printf("Failed: %s measured %lld, wrote %lld\n", cases[i].name, (long long)measured,
                   (long long)written);
            ok = 0;
        }
    }

    free(input);
    if (ok) printf("PASS\n\n");
    return ok;
}
