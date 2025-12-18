/*
 * Copyright (c) 2025, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "../include/zxc.h"
#include "../include/zxc_stream.h"

// --- Helpers ---

// Generates a buffer of random data (To force RAW)
void gen_random_data(uint8_t* buf, size_t size) {
    for (size_t i = 0; i < size; i++) buf[i] = rand() & 0xFF;
}

// Generates repetitive data (To force GNR/LZ)
void gen_lz_data(uint8_t* buf, size_t size) {
    const char* pattern =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
        "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
        "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
        "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
        "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
        "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
        "mollit anim id est laborum.";
    size_t pat_len = strlen(pattern);
    for (size_t i = 0; i < size; i++) buf[i] = pattern[i % pat_len];
}

// Generates a regular numeric sequence (To force NUM)
void gen_num_data(uint8_t* buf, size_t size) {
    // Fill with 32-bit integers
    uint32_t* ptr = (uint32_t*)buf;
    size_t count = size / 4;
    uint32_t val = 0;
    for (size_t i = 0; i < count; i++) {
        // Arithmetic sequence: 0, 100, 200...
        // Deltas are constant (100), perfect for NUM
        ptr[i] = val;
        val += 100;
    }
}

// Generic Round-Trip test function (Compress -> Decompress -> Compare)
int test_round_trip(const char* test_name, const uint8_t* input, size_t size, int level,
                    int checksum) {
    printf("=== TEST: %s (Sz: %zu, Lvl: %d, CRC: %s) ===\n", test_name, size, level,
           checksum ? "Enabled" : "Disabled");

    FILE* f_in = tmpfile();
    FILE* f_comp = tmpfile();
    FILE* f_decomp = tmpfile();

    if (!f_in || !f_comp || !f_decomp) {
        perror("tmpfile");
        if (f_in) fclose(f_in);
        if (f_comp) fclose(f_comp);
        if (f_decomp) fclose(f_decomp);
        return 0;
    }

    fwrite(input, 1, size, f_in);
    fseek(f_in, 0, SEEK_SET);

    if (zxc_stream_compress(f_in, f_comp, 1, level, checksum) < 0) {
        printf("Compression Failed!\n");
        return 0;
    }

    long comp_size = ftell(f_comp);
    printf("Compressed Size: %ld (Ratio: %.2f)\n", comp_size,
           (double)size / (comp_size > 0 ? comp_size : 1));
    fseek(f_comp, 0, SEEK_SET);

    if (zxc_stream_decompress(f_comp, f_decomp, 1, checksum) < 0) {
        printf("Decompression Failed!\n");
        return 0;
    }

    long decomp_size = ftell(f_decomp);
    if (decomp_size != (long)size) {
        printf("Size Mismatch! Expected %zu, got %ld\n", size, decomp_size);
        return 0;
    }

    fseek(f_decomp, 0, SEEK_SET);
    uint8_t* out_buf = malloc(size > 0 ? size : 1);

    if (fread(out_buf, 1, size, f_decomp) != size) {
        printf("Read validation failed (incomplete read)!\n");
        free(out_buf);
        return 0;
    }

    if (size > 0 && memcmp(input, out_buf, size) != 0) {
        printf("Data Mismatch (Content Corruption)!\n");
        free(out_buf);
        return 0;
    }

    printf("PASS\n\n");

    free(out_buf);
    fclose(f_in);
    fclose(f_comp);
    fclose(f_decomp);
    return 1;
}

// Checks that the utility function calculates a sufficient size
int test_max_compressed_size_logic() {
    printf("=== TEST: Unit - zxc_compress_bound ===\n");

    // Case 1: 0 bytes (must at least contain the header)
    size_t sz0 = zxc_compress_bound(0);
    if (sz0 == 0) {
        printf("Failed: Size for 0 bytes should not be 0 (headers required)\n");
        return 0;
    }

    // Case 2: Small input
    size_t input_val = 100;
    size_t sz100 = zxc_compress_bound(input_val);
    if (sz100 < input_val) {
        printf("Failed: Output buffer size (%zu) too small for input (%zu)\n", sz100, input_val);
        return 0;
    }

    // Case 3: Consistency (size should not decrease arbitrarily)
    if (zxc_compress_bound(2000) < zxc_compress_bound(1000)) {
        printf("Failed: Max size function is not monotonic\n");
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

// Checks API robustness against invalid arguments
int test_invalid_arguments() {
    printf("=== TEST: Unit - Invalid Arguments ===\n");

    FILE* f = tmpfile();
    if (!f) return 0;

    // 1. Input NULL -> Must fail
    if (zxc_stream_compress(NULL, f, 1, 5, 0) != -1) {
        printf("Failed: Should return -1 when Input is NULL\n");
        fclose(f);
        return 0;
    }

    // 2. Output NULL -> Must SUCCEED (Benchmark / Dry-Run Mode)
    if (zxc_stream_compress(f, NULL, 1, 5, 0) == -1) {
        printf("Failed: Should allow NULL Output (Benchmark mode support)\n");
        fclose(f);
        return 0;
    }

    // 3. Decompression Input NULL -> Must fail
    if (zxc_stream_decompress(NULL, f, 1, 0) != -1) {
        printf("Failed: Decompress should return -1 when Input is NULL\n");
        fclose(f);
        return 0;
    }

    printf("PASS\n\n");
    fclose(f);
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
    FILE* f_dummy = fopen(bad_filename, "w");
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

    // This should fail cleanly (return -1) because writing to f_out is impossible
    if (zxc_stream_compress(f_in, f_out, 1, 5, 0) != -1) {
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

    if (!f_in || !f_out) return 0;

    // Test with 0 (Auto)
    if (zxc_stream_compress(f_in, f_out, 0, 5, 0) == -1) {
        // Ok, might fail if empty, but must not crash
    }

    // Test with negative value
    fseek(f_in, 0, SEEK_SET);
    fseek(f_out, 0, SEEK_SET);
    zxc_stream_compress(f_in, f_out, -5, 5, 0);

    printf("PASS (No crash observed)\n\n");
    fclose(f_in);
    fclose(f_out);
    return 1;
}

// Checks the buffer-based API (zxc_compress / zxc_decompress)
int test_buffer_api() {
    printf("=== TEST: Unit - Buffer API (zxc_compress/zxc_decompress) ===\n");

    size_t src_size = 128 * 1024;
    uint8_t* src = malloc(src_size);
    gen_lz_data(src, src_size);

    // 1. Calculate max compressed size
    size_t max_dst_size = zxc_compress_bound(src_size);
    uint8_t* compressed = malloc(max_dst_size);
    int checksum_enabled = 1;

    // 2. Compress
    size_t compressed_size =
        zxc_compress(src, src_size, compressed, max_dst_size, 3, checksum_enabled);
    if (compressed_size == 0) {
        printf("Failed: zxc_compress returned 0\n");
        free(src);
        free(compressed);
        return 0;
    }
    printf("Compressed %zu bytes to %zu bytes\n", src_size, compressed_size);

    // 3. Decompress
    uint8_t* decompressed = malloc(src_size);
    size_t decompressed_size =
        zxc_decompress(compressed, compressed_size, decompressed, src_size, checksum_enabled);

    if (decompressed_size != src_size) {
        printf("Failed: zxc_decompress returned %zu, expected %zu\n", decompressed_size, src_size);
        free(src);
        free(compressed);
        free(decompressed);
        return 0;
    }

    // 4. Verify content
    if (memcmp(src, decompressed, src_size) != 0) {
        printf("Failed: Content mismatch after decompression\n");
        free(src);
        free(compressed);
        free(decompressed);
        return 0;
    }

    // 5. Test error case: Destination too small
    size_t small_capacity = compressed_size / 2;
    size_t small_res = zxc_compress(src, src_size, compressed, small_capacity, 3, checksum_enabled);
    if (small_res != 0) {
        printf("Failed: zxc_compress should fail with small buffer (returned %zu)\n", small_res);
        free(src);
        free(compressed);
        free(decompressed);
        return 0;
    }

    printf("PASS\n\n");
    free(src);
    free(compressed);
    free(decompressed);
    return 1;
}

int main() {
    srand(42);  // Fixed seed for reproducibility
    int total_failures = 0;

    // Standard size for blocks
    const size_t BUF_SIZE = 256 * 1024;
    uint8_t* buffer = malloc(BUF_SIZE);
    if (!buffer) {
        printf("Memory allocation failed!\n");
        return 1;
    }

    gen_random_data(buffer, BUF_SIZE);
    if (!test_round_trip("RAW Block (Random Data)", buffer, BUF_SIZE, 3, 0)) total_failures++;

    gen_lz_data(buffer, BUF_SIZE);
    if (!test_round_trip("GNR Block (Text Pattern)", buffer, BUF_SIZE, 3, 0)) total_failures++;

    gen_num_data(buffer, BUF_SIZE);
    if (!test_round_trip("NUM Block (Integer Sequence)", buffer, BUF_SIZE, 3, 0)) total_failures++;

    gen_random_data(buffer, 50);
    if (!test_round_trip("Small Input (50 bytes)", buffer, 50, 3, 0)) total_failures++;
    if (!test_round_trip("Empty Input (0 bytes)", buffer, 0, 3, 0)) total_failures++;

    printf("\n--- Test Coverage: Checksum ---\n");
    gen_lz_data(buffer, BUF_SIZE);

    if (!test_round_trip("Checksum Disabled", buffer, BUF_SIZE, 3, 0)) total_failures++;
    if (!test_round_trip("Checksum Enabled", buffer, BUF_SIZE, 31, 1)) total_failures++;

    printf("\n--- Test Coverage: Compression Levels ---\n");
    gen_lz_data(buffer, BUF_SIZE);

    if (!test_round_trip("Level 2", buffer, BUF_SIZE, 2, 1)) total_failures++;
    if (!test_round_trip("Level 3", buffer, BUF_SIZE, 3, 1)) total_failures++;
    if (!test_round_trip("Level 4", buffer, BUF_SIZE, 4, 1)) total_failures++;
    if (!test_round_trip("Level 5", buffer, BUF_SIZE, 5, 1)) total_failures++;

    free(buffer);

    // --- UNIT TESTS (ROBUSTNESS/API) ---

    if (!test_buffer_api()) total_failures++;

    if (!test_max_compressed_size_logic()) total_failures++;
    if (!test_invalid_arguments()) total_failures++;
    if (!test_io_failures()) total_failures++;
    if (!test_thread_params()) total_failures++;

    if (total_failures > 0) {
        printf("FAILED: %d tests failed.\n", total_failures);
        return 1;
    }

    printf("ALL TESTS PASSED SUCCESSFULLY.\n");
    return 0;
}