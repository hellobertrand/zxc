/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_stream.h"
#include "../src/lib/zxc_internal.h"

// --- Helpers ---

// Generates a buffer of random data (To force RAW)
void gen_random_data(uint8_t* buf, size_t size) {
    for (size_t i = 0; i < size; i++) buf[i] = rand() & 0xFF;
}

// Generates repetitive data (To force GLO/GHI/LZ)
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

void gen_binary_data(uint8_t* buf, size_t size) {
    // Pattern with problematic bytes that could be corrupted in text mode:
    // 0x0A (LF), 0x0D (CR), 0x00 (NULL), 0x1A (EOF/CTRL-Z), 0xFF
    const uint8_t pattern[] = {
        0x5A, 0x58, 0x43, 0x00,  // "ZXC" + NULL
        0x0A, 0x0D, 0x0A, 0x00,  // LF, CR, LF, NULL
        0xFF, 0xFE, 0x0A, 0x0D,  // High bytes + LF/CR
        0x1A, 0x00, 0x0A, 0x0D,  // EOF marker + NULL + LF/CR
        0x00, 0x00, 0x0A, 0x0A,  // Multiple NULLs and LFs
    };
    size_t pat_len = sizeof(pattern);
    for (size_t i = 0; i < size; i++) {
        buf[i] = pattern[i % pat_len];
    }
}

// Generates data with small offsets (<=255 bytes) to force 1-byte offset encoding
// This creates short repeating patterns with matches very close to each other
void gen_small_offset_data(uint8_t* buf, size_t size) {
    // Create short repeating patterns with very short distances
    // Pattern: ABCDABCDABCD... where each match is only 4 bytes away
    const uint8_t pattern[] = "ABCD";
    for (size_t i = 0; i < size; i++) {
        buf[i] = pattern[i % 4];
    }
}

// Generates data with large offsets (>255 bytes) to force 2-byte offset encoding
// This creates patterns where matches are far apart
void gen_large_offset_data(uint8_t* buf, size_t size) {
    // First 300 bytes: unique random data (no matches possible)
    for (size_t i = 0; i < 300 && i < size; i++) {
        buf[i] = (uint8_t)((i * 7 + 13) % 256);
    }
    // Then: repeat patterns from the beginning (offset > 255)
    for (size_t i = 300; i < size; i++) {
        buf[i] = buf[i - 300];  // Offset of 300 bytes (requires 2-byte encoding)
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
        fclose(f_in);
        fclose(f_comp);
        fclose(f_decomp);
        return 0;
    }

    long comp_size = ftell(f_comp);
    printf("Compressed Size: %ld (Ratio: %.2f)\n", comp_size,
           (double)size / (comp_size > 0 ? comp_size : 1));
    fseek(f_comp, 0, SEEK_SET);

    if (zxc_stream_decompress(f_comp, f_decomp, 1, checksum) < 0) {
        printf("Decompression Failed!\n");
        fclose(f_in);
        fclose(f_comp);
        fclose(f_decomp);
        return 0;
    }

    long decomp_size = ftell(f_decomp);
    if (decomp_size != (long)size) {
        printf("Size Mismatch! Expected %zu, got %ld\n", size, decomp_size);
        fclose(f_in);
        fclose(f_comp);
        fclose(f_decomp);
        return 0;
    }

    fseek(f_decomp, 0, SEEK_SET);
    uint8_t* out_buf = malloc(size > 0 ? size : 1);

    if (fread(out_buf, 1, size, f_decomp) != size) {
        printf("Read validation failed (incomplete read)!\n");
        free(out_buf);
        fclose(f_in);
        fclose(f_comp);
        fclose(f_decomp);
        return 0;
    }

    if (size > 0 && memcmp(input, out_buf, size) != 0) {
        printf("Data Mismatch (Content Corruption)!\n");
        free(out_buf);
        fclose(f_in);
        fclose(f_comp);
        fclose(f_decomp);
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

    FILE* f_valid = tmpfile();
    if (!f_valid) {
        fclose(f);
        return 0;
    }
    // Prepare a valid compressed stream for decompression tests
    zxc_stream_compress(f, f_valid, 1, 1, 0);
    rewind(f_valid);

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

    // 3b. Decompression Output NULL -> Must SUCCEED (Benchmark mode)
    if (zxc_stream_decompress(f_valid, NULL, 1, 0) == -1) {
        printf("Failed: Decompress should allow NULL Output (Benchmark mode support)\n");
        fclose(f_valid);
        return 0;
    }

    // 4. zxc_compress NULL checks
    if (zxc_compress(NULL, 100, (void*)1, 100, 3, 0) != 0) {
        printf("Failed: zxc_compress should return 0 when src is NULL\n");
        fclose(f);
        return 0;
    }
    if (zxc_compress((void*)1, 100, NULL, 100, 3, 0) != 0) {
        printf("Failed: zxc_compress should return 0 when dst is NULL\n");
        fclose(f);
        return 0;
    }

    // 5. zxc_decompress NULL checks
    if (zxc_decompress(NULL, 100, (void*)1, 100, 0) != 0) {
        printf("Failed: zxc_decompress should return 0 when src is NULL\n");
        fclose(f);
        return 0;
    }
    if (zxc_decompress((void*)1, 100, NULL, 100, 0) != 0) {
        printf("Failed: zxc_decompress should return 0 when dst is NULL\n");
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
    if (!f_in || !f_out) {
        if (f_in) fclose(f_in);
        if (f_out) fclose(f_out);
        return 0;
    }

    // Test with 0 (Auto) and negative value - must not crash
    zxc_stream_compress(f_in, f_out, 0, 5, 0);
    fseek(f_in, 0, SEEK_SET);
    fseek(f_out, 0, SEEK_SET);
    zxc_stream_compress(f_in, f_out, -5, 5, 0);

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
        if (zxc_stream_compress(f_in, f_comp, num_threads, 3, 1) < 0) {
            printf("Compression failed (threads=%d)!\n", num_threads);
            fclose(f_in);
            fclose(f_comp);
            fclose(f_decomp);
            goto cleanup;
        }

        fseek(f_comp, 0, SEEK_SET);

        if (zxc_stream_decompress(f_comp, f_decomp, num_threads, 1) < 0) {
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

/*
 * Test for zxc_br_init and zxc_br_ensure
 */
int test_bit_reader() {
    printf("=== TEST: Unit - Bit Reader (zxc_br_init / zxc_br_ensure) ===\n");

    // Case 1: Normal initialization
    uint8_t buffer[16];
    for (int i = 0; i < 16; i++) buffer[i] = (uint8_t)i;
    zxc_bit_reader_t br;
    zxc_br_init(&br, buffer, 16);

    if (br.bits != 64) return 0;
    if (br.ptr != buffer + 8) return 0;
    if (br.accum != zxc_le64(buffer)) return 0;
    printf("  [PASS] Normal init\n");

    // Case 2: Small buffer initialization (should not crash)
    uint8_t small_buffer[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    zxc_br_init(&br, small_buffer, 4);
    // Should have read 4 bytes safely
    uint64_t expected_accum = 0;
    memcpy(&expected_accum, small_buffer, 4);
    if (br.accum != expected_accum) return 0;
    if (br.ptr != small_buffer + 4) return 0;
    printf("  [PASS] Small buffer init\n");

    // Case 3: zxc_br_ensure (Normal refill)
    zxc_br_init(&br, buffer, 16);
    br.bits = 10;     // Simulate consumption
    br.accum >>= 54;  // Simulate shift

    zxc_br_ensure(&br, 32);
    // Should have refilled
    if (br.bits < 32) return 0;
    printf("  [PASS] Ensure normal refill\n");

    // Case 4: zxc_br_ensure (End of stream)
    // Init with full buffer but advanced pointer near end
    zxc_br_init(&br, buffer, 16);
    br.ptr = buffer + 16;  // At end
    br.bits = 0;

    // Try to ensure bits, should not read past end
    zxc_br_ensure(&br, 10);
    // The key is it didn't crash.
    printf("  [PASS] Ensure EOF safety\n");

    printf("PASS\n\n");
    return 1;
}

/*
 * Test for zxc_bitpack_stream_32
 */
int test_bitpack() {
    printf("=== TEST: Unit - Bit Packing (zxc_bitpack_stream_32) ===\n");

    const uint32_t src[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    uint8_t dst[16];

    // Pack 4 values with 4 bits each.
    // Input is 0xFFFFFFFF, but should be masked to 0xF (1111).
    // Result should be 2 bytes: 0xFF, 0xFF
    int len = zxc_bitpack_stream_32(src, 4, dst, 16, 4);

    if (len != 2) return 0;
    if (dst[0] != 0xFF || dst[1] != 0xFF) return 0;
    printf("  [PASS] Bitpack overflow masking\n");

    // Edge case: bits = 32
    const uint32_t src32[1] = {0x12345678};
    len = zxc_bitpack_stream_32(src32, 1, dst, 16, 32);
    if (len != 4) return 0;
    if (zxc_le32(dst) != 0x12345678) return 0;
    printf("  [PASS] Bitpack 32 bits\n");

    printf("PASS\n\n");
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
    if (!test_round_trip("GHI Block (Text Pattern)", buffer, BUF_SIZE, 2, 0)) total_failures++;

    gen_lz_data(buffer, BUF_SIZE);
    if (!test_round_trip("GLO Block (Text Pattern)", buffer, BUF_SIZE, 4, 0)) total_failures++;

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

    if (!test_round_trip("Level 1", buffer, BUF_SIZE, 1, 1)) total_failures++;
    if (!test_round_trip("Level 2", buffer, BUF_SIZE, 2, 1)) total_failures++;
    if (!test_round_trip("Level 3", buffer, BUF_SIZE, 3, 1)) total_failures++;
    if (!test_round_trip("Level 4", buffer, BUF_SIZE, 4, 1)) total_failures++;
    if (!test_round_trip("Level 5", buffer, BUF_SIZE, 5, 1)) total_failures++;

    printf("\n--- Test Coverage: Binary Data Preservation ---\n");
    gen_binary_data(buffer, BUF_SIZE);
    if (!test_round_trip("Binary Data (0x00, 0x0A, 0x0D, 0xFF)", buffer, BUF_SIZE, 3, 0))
        total_failures++;
    if (!test_round_trip("Binary Data with Checksum", buffer, BUF_SIZE, 3, 1)) total_failures++;

    // Test with small binary data to ensure even small payloads are preserved
    gen_binary_data(buffer, 128);
    if (!test_round_trip("Small Binary Data (128 bytes)", buffer, 128, 3, 0)) total_failures++;

    printf("\n--- Test Coverage: Variable Offset Encoding (v0.4.0) ---\n");

    // Test 8-bit offset mode (enc_off=1): patterns with all offsets <= 255
    gen_small_offset_data(buffer, BUF_SIZE);
    if (!test_round_trip("8-bit Offsets (Small Pattern)", buffer, BUF_SIZE, 3, 1)) total_failures++;
    if (!test_round_trip("8-bit Offsets (Level 5)", buffer, BUF_SIZE, 5, 1)) total_failures++;

    // Test 16-bit offset mode (enc_off=0): patterns with offsets > 255
    gen_large_offset_data(buffer, BUF_SIZE);
    if (!test_round_trip("16-bit Offsets (Large Distance)", buffer, BUF_SIZE, 3, 1))
        total_failures++;
    if (!test_round_trip("16-bit Offsets (Level 5)", buffer, BUF_SIZE, 5, 1)) total_failures++;

    // Edge case: Mixed buffer that should trigger 16-bit mode
    // (even one large offset forces 16-bit mode)
    gen_small_offset_data(buffer, BUF_SIZE / 2);
    gen_large_offset_data(buffer + BUF_SIZE / 2, BUF_SIZE / 2);
    if (!test_round_trip("Mixed Offsets (Hybrid)", buffer, BUF_SIZE, 3, 1)) total_failures++;

    free(buffer);

    // --- UNIT TESTS (ROBUSTNESS/API) ---

    if (!test_buffer_api()) total_failures++;

    if (!test_multithread_roundtrip()) total_failures++;

    if (!test_max_compressed_size_logic()) total_failures++;
    if (!test_invalid_arguments()) total_failures++;
    if (!test_io_failures()) total_failures++;
    if (!test_thread_params()) total_failures++;
    if (!test_bit_reader()) total_failures++;
    if (!test_bitpack()) total_failures++;

    if (total_failures > 0) {
        printf("FAILED: %d tests failed.\n", total_failures);
        return 1;
    }

    printf("ALL TESTS PASSED SUCCESSFULLY.\n");
    return 0;
}