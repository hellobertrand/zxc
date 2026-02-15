/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_error.h"
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
    // Create short repeating patterns with very short distances.
    // Uses a 5-byte period (not aligned to uint32_t) to avoid being
    // classified as NUM data by zxc_probe_is_numeric().
    // LZ will match at offset=5 (< 255), exercising 8-bit offset encoding.
    const uint8_t pattern[] = "ABCDE";
    for (size_t i = 0; i < size; i++) {
        buf[i] = pattern[i % 5];
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
    if (zxc_stream_compress(f_in, f_comp, 1, 3, 1) < 0) {
        printf("Compression Failed!\n");
        fclose(f_in);
        fclose(f_comp);
        free(input);
        return 0;
    }
    fseek(f_comp, 0, SEEK_SET);

    // Decompress with NULL output
    // This should return the decompressed size but write nothing
    int64_t d_sz = zxc_stream_decompress(f_comp, NULL, 1, 1);

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
    if (zxc_stream_compress(NULL, f, 1, 5, 0) >= 0) {
        printf("Failed: Should return < 0 when Input is NULL\n");
        fclose(f);
        return 0;
    }

    // 2. Output NULL -> Must SUCCEED (Benchmark / Dry-Run Mode)
    if (zxc_stream_compress(f, NULL, 1, 5, 0) < 0) {
        printf("Failed: Should allow NULL Output (Benchmark mode support)\n");
        fclose(f);
        return 0;
    }

    // 3. Decompression Input NULL -> Must fail
    if (zxc_stream_decompress(NULL, f, 1, 0) >= 0) {
        printf("Failed: Decompress should return < 0 when Input is NULL\n");
        fclose(f);
        return 0;
    }

    // 3b. Decompression Output NULL -> Must SUCCEED (Benchmark mode)
    if (zxc_stream_decompress(f_valid, NULL, 1, 0) < 0) {
        printf("Failed: Decompress should allow NULL Output (Benchmark mode support)\n");
        fclose(f_valid);
        return 0;
    }

    // 4. zxc_compress NULL checks
    if (zxc_compress(NULL, 100, (void*)1, 100, 3, 0) >= 0) {
        printf("Failed: zxc_compress should return < 0 when src is NULL\n");
        fclose(f);
        return 0;
    }
    if (zxc_compress((void*)1, 100, NULL, 100, 3, 0) >= 0) {
        printf("Failed: zxc_compress should return < 0 when dst is NULL\n");
        fclose(f);
        return 0;
    }

    // 5. zxc_decompress NULL checks
    if (zxc_decompress(NULL, 100, (void*)1, 100, 0) >= 0) {
        printf("Failed: zxc_decompress should return < 0 when src is NULL\n");
        fclose(f);
        return 0;
    }
    if (zxc_decompress((void*)1, 100, NULL, 100, 0) >= 0) {
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

    size_t cap = zxc_compress_bound(SRC_SIZE);
    uint8_t* compressed = malloc(cap);
    uint8_t* decomp_buf = malloc(SRC_SIZE);

    if (!compressed || !decomp_buf) {
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    int64_t comp_sz = zxc_compress(src, SRC_SIZE, compressed, cap, 3, 1);
    if (comp_sz <= 0) {
        printf("Prepare failed\n");
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    // Try decompressing with progressively cropped size
    // 1. Cut off the Footer (last ZXC_FILE_FOOTER_SIZE bytes)
    if (comp_sz > ZXC_FILE_FOOTER_SIZE) {
        if (zxc_decompress(compressed, comp_sz - ZXC_FILE_FOOTER_SIZE, decomp_buf, SRC_SIZE, 1) >=
            0) {
            printf("Failed: Should fail when footer is missing\n");
            free(compressed);
            free(decomp_buf);
            return 0;
        }
    }

    // 2. Cut off half the file
    if (zxc_decompress(compressed, comp_sz / 2, decomp_buf, SRC_SIZE, 1) >= 0) {
        printf("Failed: Should fail when stream is truncated by half\n");
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    // 3. Cut off just 1 byte
    if (zxc_decompress(compressed, comp_sz - 1, decomp_buf, SRC_SIZE, 1) >= 0) {
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

    // This should fail cleanly (return < 0) because writing to f_out is impossible
    if (zxc_stream_compress(f_in, f_out, 1, 5, 0) >= 0) {
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
    int64_t compressed_size =
        zxc_compress(src, src_size, compressed, max_dst_size, 3, checksum_enabled);
    if (compressed_size <= 0) {
        printf("Failed: zxc_compress returned %lld\n", (long long)compressed_size);
        free(src);
        free(compressed);
        return 0;
    }
    printf("Compressed %zu bytes to %lld bytes\n", src_size, (long long)compressed_size);

    // 3. Decompress
    uint8_t* decompressed = malloc(src_size);
    int64_t decompressed_size =
        zxc_decompress(compressed, compressed_size, decompressed, src_size, checksum_enabled);

    if (decompressed_size != (int64_t)src_size) {
        printf("Failed: zxc_decompress returned %lld, expected %zu\n", (long long)decompressed_size,
               src_size);
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
    int64_t small_res =
        zxc_compress(src, src_size, compressed, small_capacity, 3, checksum_enabled);
    if (small_res >= 0) {
        printf("Failed: zxc_compress should fail with small buffer (returned %lld)\n",
               (long long)small_res);
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
    // Should have read 4 bytes safely (in LE order, matching zxc_le_partial)
    uint64_t expected_accum = (uint64_t)small_buffer[0] | ((uint64_t)small_buffer[1] << 8) |
                              ((uint64_t)small_buffer[2] << 16) | ((uint64_t)small_buffer[3] << 24);
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

// Checks that the EOF block is correctly appended
int test_eof_block_structure() {
    printf("=== TEST: Unit - EOF Block Structure ===\n");

    const char* input = "test";
    size_t src_size = 4;
    size_t max_dst_size = zxc_compress_bound(src_size);
    uint8_t* compressed = malloc(max_dst_size);
    if (!compressed) return 0;

    int64_t comp_size = zxc_compress(input, src_size, compressed, max_dst_size, 1, 0);
    if (comp_size <= 0) {
        printf("Failed: Compression returned 0\n");
        free(compressed);
        return 0;
    }

    // Validating Footer and EOF Block
    // Total Overhead: 12 bytes (Footer) + 8 bytes (EOF Header) = 20 bytes
    if (comp_size < 20) {
        printf("Failed: Compressed size too small for Footer + EOF (%lld)\n", (long long)comp_size);
        free(compressed);
        return 0;
    }

    // 1. Verify 12-byte Footer
    // Structure: [SrcSize (8)] + [Hash (4)]
    const uint8_t* footer_ptr = compressed + comp_size - 12;
    uint32_t f_src_low = zxc_le32(footer_ptr);       // Should be 4
    uint32_t f_src_high = zxc_le32(footer_ptr + 4);  // Should be 0
    uint32_t f_hash = zxc_le32(footer_ptr + 8);      // Should be 0 (checksum disabled)

    if (f_src_low != 4 || f_src_high != 0 || f_hash != 0) {
        printf("Failed: Footer mismatch. Src: %u, Hash: %u\n", f_src_low, f_hash);
        free(compressed);
        return 0;
    }

    // 2. Verify EOF Block Header (8 bytes)
    // Should be immediately before the footer
    const uint8_t* eof_ptr = compressed + comp_size - 20;
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
                                .header_crc = 0,
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
        bh_out.header_crc != header_buf[7]) {
        printf("  [FAIL] Read data mismatch\n");
        return 0;
    }

    // 3. Corrupt Header Checksum
    uint8_t original_crc = header_buf[7];
    header_buf[7] = ~original_crc;  // Flip bits
    if (zxc_read_block_header(header_buf, ZXC_BLOCK_HEADER_SIZE, &bh_out) == 0) {
        printf("  [FAIL] zxc_read_block_header should have failed on corrupted CRC\n");
        return 0;
    }
    header_buf[7] = original_crc;  // Restore

    // 4. Corrupt Header Content
    header_buf[0] = ZXC_BLOCK_RAW;  // Change type
    if (zxc_read_block_header(header_buf, ZXC_BLOCK_HEADER_SIZE, &bh_out) == 0) {
        printf("  [FAIL] zxc_read_block_header should have failed on corrupted content\n");
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

// 5. Test Global Checksum Order Sensitivity
// Ensures that swapping two blocks (even if valid individually) triggers a global checksum failure.
int test_global_checksum_order() {
    printf("TEST: Global Checksum Order Sensitivity... ");

    // 1. Create input data withDISTINCT patterns for 2 blocks (so blocks are different)
    // ZXC_BLOCK_SIZE is 256KB. We need > 256KB. Let's use 600KB.
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
    zxc_stream_compress(f_in, f_comp, 1, 1, 1);

    // 3. Read compressed data to memory
    long comp_sz = ftell(f_comp);
    rewind(f_comp);
    uint8_t* comp_buf = malloc(comp_sz);
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

    // Ensure we have at least 2 full blocks + EOF + Global Checksum
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
    uint8_t* swapped_buf = malloc(comp_sz);

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

    // Write remaining data (EOF block + Global Checksum)
    size_t remaining_off = off2 + len2;
    size_t remaining_len = comp_sz - remaining_off;
    memcpy(swapped_buf + w_off, comp_buf + remaining_off, remaining_len);

    // 6. Write to File for Decompression
    FILE* f_bad = tmpfile();
    fwrite(swapped_buf, 1, comp_sz, f_bad);
    rewind(f_bad);

    // 7. Attempt Decompression
    FILE* f_out = tmpfile();
    int64_t res = zxc_stream_decompress(f_bad, f_out, 1, 1);

    fclose(f_in);
    fclose(f_comp);
    fclose(f_bad);
    fclose(f_out);
    free(val_buf);
    free(comp_buf);
    free(swapped_buf);

    if (res >= 0) {
        printf("  [FAIL] zxc_stream_decompress unexpectedly succeeded on swapped blocks\n");
        return 0;
    }

    printf("PASS\n\n");
    return 1;
}

// Test zxc_get_decompressed_size
int test_get_decompressed_size() {
    printf("=== TEST: Unit - zxc_get_decompressed_size ===\n");

    // 1. Compress some data, then check decompressed size
    size_t src_size = 64 * 1024;
    uint8_t* src = malloc(src_size);
    gen_lz_data(src, src_size);

    size_t max_dst = zxc_compress_bound(src_size);
    uint8_t* compressed = malloc(max_dst);

    int64_t comp_size = zxc_compress(src, src_size, compressed, max_dst, 3, 0);
    if (comp_size <= 0) {
        printf("Failed: Compression returned 0\n");
        free(src);
        free(compressed);
        return 0;
    }

    size_t reported = zxc_get_decompressed_size(compressed, comp_size);
    if (reported != src_size) {
        printf("Failed: Expected %zu, got %zu\n", src_size, reported);
        free(src);
        free(compressed);
        return 0;
    }
    printf("  [PASS] Valid compressed data\n");

    // 2. Too-small buffer
    if (zxc_get_decompressed_size(compressed, 4) != 0) {
        printf("Failed: Should return 0 for too-small buffer\n");
        free(src);
        free(compressed);
        return 0;
    }
    printf("  [PASS] Too-small buffer\n");

    // 3. Invalid magic word
    uint8_t bad_buf[64] = {0};
    if (zxc_get_decompressed_size(bad_buf, sizeof(bad_buf)) != 0) {
        printf("Failed: Should return 0 for invalid magic\n");
        free(src);
        free(compressed);
        return 0;
    }
    printf("  [PASS] Invalid magic word\n");

    printf("PASS\n\n");
    free(src);
    free(compressed);
    return 1;
}

int test_error_name() {
    printf("--- Test: zxc_error_name ---\n");

    struct {
        int code;
        const char* expected;
    } cases[] = {
        {ZXC_OK, "ZXC_OK"},
        {ZXC_ERROR_MEMORY, "ZXC_ERROR_MEMORY"},
        {ZXC_ERROR_DST_TOO_SMALL, "ZXC_ERROR_DST_TOO_SMALL"},
        {ZXC_ERROR_SRC_TOO_SMALL, "ZXC_ERROR_SRC_TOO_SMALL"},
        {ZXC_ERROR_BAD_MAGIC, "ZXC_ERROR_BAD_MAGIC"},
        {ZXC_ERROR_BAD_VERSION, "ZXC_ERROR_BAD_VERSION"},
        {ZXC_ERROR_BAD_HEADER, "ZXC_ERROR_BAD_HEADER"},
        {ZXC_ERROR_BAD_CHECKSUM, "ZXC_ERROR_BAD_CHECKSUM"},
        {ZXC_ERROR_CORRUPT_DATA, "ZXC_ERROR_CORRUPT_DATA"},
        {ZXC_ERROR_BAD_OFFSET, "ZXC_ERROR_BAD_OFFSET"},
        {ZXC_ERROR_OVERFLOW, "ZXC_ERROR_OVERFLOW"},
        {ZXC_ERROR_IO, "ZXC_ERROR_IO"},
        {ZXC_ERROR_NULL_INPUT, "ZXC_ERROR_NULL_INPUT"},
        {ZXC_ERROR_BAD_BLOCK_TYPE, "ZXC_ERROR_BAD_BLOCK_TYPE"},
    };
    const int n = sizeof(cases) / sizeof(cases[0]);

    for (int i = 0; i < n; i++) {
        const char* name = zxc_error_name(cases[i].code);
        if (strcmp(name, cases[i].expected) != 0) {
            printf("  [FAIL] zxc_error_name(%d) = \"%s\", expected \"%s\"\n", cases[i].code, name,
                   cases[i].expected);
            return 0;
        }
    }
    printf("  [PASS] All %d known error codes\n", n);

    // Unknown codes should return "ZXC_UNKNOWN_ERROR"
    const char* unk = zxc_error_name(-999);
    if (strcmp(unk, "ZXC_UNKNOWN_ERROR") != 0) {
        printf("  [FAIL] zxc_error_name(-999) = \"%s\", expected \"ZXC_UNKNOWN_ERROR\"\n", unk);
        return 0;
    }
    unk = zxc_error_name(42);
    if (strcmp(unk, "ZXC_UNKNOWN_ERROR") != 0) {
        printf("  [FAIL] zxc_error_name(42) = \"%s\", expected \"ZXC_UNKNOWN_ERROR\"\n", unk);
        return 0;
    }
    printf("  [PASS] Unknown error codes\n");

    printf("PASS\n\n");
    return 1;
}

int test_buffer_error_codes() {
    printf("=== TEST: Unit - Buffer API Error Codes ===\n");

    /* ------------------------------------------------------------------ */
    /* zxc_compress error paths                                           */
    /* ------------------------------------------------------------------ */

    // 1. NULL src
    int64_t r = zxc_compress(NULL, 100, (void*)1, 100, 3, 0);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] NULL src: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT, (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_compress NULL src -> ZXC_ERROR_NULL_INPUT\n");

    // 2. NULL dst
    r = zxc_compress((void*)1, 100, NULL, 100, 3, 0);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] NULL dst: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT, (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_compress NULL dst -> ZXC_ERROR_NULL_INPUT\n");

    // 3. src_size == 0
    uint8_t dummy[16];
    r = zxc_compress(dummy, 0, dummy, sizeof(dummy), 3, 0);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] src_size==0: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT, (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_compress src_size==0 -> ZXC_ERROR_NULL_INPUT\n");

    // 4. dst_capacity == 0
    r = zxc_compress(dummy, sizeof(dummy), dummy, 0, 3, 0);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] dst_cap==0: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT, (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_compress dst_capacity==0 -> ZXC_ERROR_NULL_INPUT\n");

    // 5. dst too small for file header (< 16 bytes)
    {
        uint8_t src[64];
        uint8_t dst[8];  // Too small for file header (16 bytes)
        gen_lz_data(src, sizeof(src));
        r = zxc_compress(src, sizeof(src), dst, sizeof(dst), 3, 0);
        if (r >= 0) {
            printf("  [FAIL] dst too small for header: expected < 0, got %lld\n", (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_compress dst too small for header -> negative\n");

    // 6. dst too small for data (fits header but not chunk)
    {
        const size_t src_sz = 4096;
        uint8_t* src = malloc(src_sz);
        const size_t small_dst = 128;
        uint8_t* dst = malloc(small_dst);
        gen_lz_data(src, src_sz);
        r = zxc_compress(src, src_sz, dst, small_dst, 3, 0);
        if (r >= 0) {
            printf("  [FAIL] dst too small for chunk: expected < 0, got %lld\n", (long long)r);
            free(src);
            free(dst);
            return 0;
        }
        free(src);
        free(dst);
    }
    printf("  [PASS] zxc_compress dst too small for chunk -> negative\n");

    // 7. dst too small for EOF + footer
    {
        // Compress first to find the exact compressed size, then retry with
        // just enough for the data blocks but not for the EOF + footer.
        const size_t src_sz = 256;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);
        const size_t full_cap = zxc_compress_bound(src_sz);
        uint8_t* full_dst = malloc(full_cap);
        const int64_t full_sz = zxc_compress(src, src_sz, full_dst, full_cap, 3, 0);
        if (full_sz <= 0) {
            printf("  [SKIP] Cannot prepare for EOF test\n");
            free(src);
            free(full_dst);
        } else {
            // EOF header(8) + footer(12) = 20 bytes at the end.
            // Try with a buffer that's just a few bytes too small.
            const size_t tight = (size_t)full_sz - 5;
            uint8_t* tight_dst = malloc(tight);
            r = zxc_compress(src, src_sz, tight_dst, tight, 3, 0);
            if (r >= 0) {
                printf("  [FAIL] dst too small for EOF+footer: expected < 0, got %lld\n",
                       (long long)r);
                free(src);
                free(full_dst);
                free(tight_dst);
                return 0;
            }
            free(src);
            free(full_dst);
            free(tight_dst);
        }
    }
    printf("  [PASS] zxc_compress dst too small for EOF+footer -> negative\n");

    /* ------------------------------------------------------------------ */
    /* zxc_decompress error paths                                         */
    /* ------------------------------------------------------------------ */

    // 8. NULL src
    r = zxc_decompress(NULL, 100, (void*)1, 100, 0);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] decompress NULL src: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT,
               (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_decompress NULL src -> ZXC_ERROR_NULL_INPUT\n");

    // 9. NULL dst
    r = zxc_decompress((void*)1, 100, NULL, 100, 0);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] decompress NULL dst: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT,
               (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_decompress NULL dst -> ZXC_ERROR_NULL_INPUT\n");

    // 10. src too small for file header
    {
        uint8_t tiny[4] = {0};
        uint8_t out[64];
        r = zxc_decompress(tiny, sizeof(tiny), out, sizeof(out), 0);
        if (r != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] src too small: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT,
                   (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_decompress src too small -> ZXC_ERROR_NULL_INPUT\n");

    // 11. Bad file header (invalid magic)
    {
        uint8_t bad_src[64];
        memset(bad_src, 0, sizeof(bad_src));
        uint8_t out[64];
        r = zxc_decompress(bad_src, sizeof(bad_src), out, sizeof(out), 0);
        if (r != ZXC_ERROR_BAD_HEADER) {
            printf("  [FAIL] bad magic: expected %d, got %lld\n", ZXC_ERROR_BAD_HEADER,
                   (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_decompress bad magic -> ZXC_ERROR_BAD_HEADER\n");

    // Prepare a valid compressed buffer for subsequent decompress error tests
    const size_t test_src_sz = 1024;
    uint8_t* test_src = malloc(test_src_sz);
    gen_lz_data(test_src, test_src_sz);
    const size_t comp_cap = zxc_compress_bound(test_src_sz);
    uint8_t* comp_buf = malloc(comp_cap);
    const int64_t comp_sz = zxc_compress(test_src, test_src_sz, comp_buf, comp_cap, 3, 1);
    if (comp_sz <= 0) {
        printf("  [FAIL] Could not prepare compressed data\n");
        free(test_src);
        free(comp_buf);
        return 0;
    }

    // 12. Corrupt block header (damage the first block header byte after file header)
    {
        uint8_t* corrupt = malloc(comp_sz);
        memcpy(corrupt, comp_buf, comp_sz);
        // Corrupt the block type byte at offset ZXC_FILE_HEADER_SIZE
        corrupt[ZXC_FILE_HEADER_SIZE] = 0xFF;  // Invalid block type
        uint8_t* out = malloc(test_src_sz);
        r = zxc_decompress(corrupt, comp_sz, out, test_src_sz, 1);
        if (r >= 0) {
            printf("  [FAIL] corrupt block header: expected < 0, got %lld\n", (long long)r);
            free(corrupt);
            free(out);
            free(test_src);
            free(comp_buf);
            return 0;
        }
        free(corrupt);
        free(out);
    }
    printf("  [PASS] zxc_decompress corrupt block header -> negative\n");

    // 13. Truncated at EOF (missing footer)
    {
        // Find the EOF block: it ends with the footer(12 bytes)
        // Truncate so the footer is missing
        const size_t trunc_sz = (size_t)comp_sz - ZXC_FILE_FOOTER_SIZE + 2;  // Cut most of footer
        uint8_t* out = malloc(test_src_sz);
        r = zxc_decompress(comp_buf, trunc_sz, out, test_src_sz, 1);
        if (r >= 0) {
            printf("  [FAIL] truncated footer: expected < 0, got %lld\n", (long long)r);
            free(out);
            free(test_src);
            free(comp_buf);
            return 0;
        }
        free(out);
    }
    printf("  [PASS] zxc_decompress truncated footer -> negative\n");

    // 14. Stored size mismatch (corrupt the source size in footer)
    {
        uint8_t* corrupt = malloc(comp_sz);
        memcpy(corrupt, comp_buf, comp_sz);
        // Footer is at end: last 12 bytes = [src_size(8)] + [global_hash(4)]
        // Corrupt the source size field (add 1 to the first byte)
        const size_t footer_offset = (size_t)comp_sz - ZXC_FILE_FOOTER_SIZE;
        corrupt[footer_offset] ^= 0x01;  // Flip a bit in the stored source size
        uint8_t* out = malloc(test_src_sz);
        r = zxc_decompress(corrupt, comp_sz, out, test_src_sz, 1);
        if (r >= 0) {
            printf("  [FAIL] size mismatch: expected < 0, got %lld\n", (long long)r);
            free(corrupt);
            free(out);
            free(test_src);
            free(comp_buf);
            return 0;
        }
        free(corrupt);
        free(out);
    }
    printf("  [PASS] zxc_decompress stored size mismatch -> negative\n");

    // 15. Global checksum failure (corrupt the global hash in footer)
    {
        uint8_t* corrupt = malloc(comp_sz);
        memcpy(corrupt, comp_buf, comp_sz);
        // Global hash is the last 4 bytes of the file
        corrupt[comp_sz - 1] ^= 0xFF;
        uint8_t* out = malloc(test_src_sz);
        r = zxc_decompress(corrupt, comp_sz, out, test_src_sz, 1);
        if (r != ZXC_ERROR_BAD_CHECKSUM) {
            printf("  [FAIL] bad global checksum: expected %d, got %lld\n", ZXC_ERROR_BAD_CHECKSUM,
                   (long long)r);
            free(corrupt);
            free(out);
            free(test_src);
            free(comp_buf);
            return 0;
        }
        free(corrupt);
        free(out);
    }
    printf("  [PASS] zxc_decompress global checksum -> ZXC_ERROR_BAD_CHECKSUM\n");

    // 16. dst too small for decompression
    {
        uint8_t* out = malloc(test_src_sz / 4);  // Way too small
        r = zxc_decompress(comp_buf, comp_sz, out, test_src_sz / 4, 0);
        if (r >= 0) {
            printf("  [FAIL] dst too small for decompress: expected < 0, got %lld\n", (long long)r);
            free(out);
            free(test_src);
            free(comp_buf);
            return 0;
        }
        free(out);
    }
    printf("  [PASS] zxc_decompress dst too small -> negative\n");

    free(test_src);
    free(comp_buf);
    printf("PASS\n\n");
    return 1;
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
        const size_t cap = zxc_compress_bound(src_sz);
        uint8_t* comp = malloc(cap);
        int64_t comp_sz = zxc_compress(src, src_sz, comp, cap, 3, 0);
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
        int64_t r = zxc_stream_compress(NULL, f_out, 1, 3, 0);
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
        int64_t r = zxc_stream_decompress(NULL, f_out, 1, 0);
        if (r != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] decompress NULL f_in: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT,
                   (long long)r);
            if (f_out) fclose(f_out);
            return 0;
        }
        if (f_out) fclose(f_out);
    }
    printf("  [PASS] zxc_stream_decompress NULL f_in -> ZXC_ERROR_NULL_INPUT\n");

    // 3. zxc_stream_decompress with bad header (invalid file)
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

        int64_t r = zxc_stream_decompress(f_in, f_out, 1, 0);
        if (r != ZXC_ERROR_BAD_HEADER) {
            printf("  [FAIL] bad header: expected %d, got %lld\n", ZXC_ERROR_BAD_HEADER,
                   (long long)r);
            fclose(f_in);
            fclose(f_out);
            return 0;
        }
        fclose(f_in);
        fclose(f_out);
    }
    printf("  [PASS] zxc_stream_decompress bad header -> ZXC_ERROR_BAD_HEADER\n");

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

        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, 1, 3, 1);
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
        fread(comp_data, 1, comp_file_sz, f_comp_out);
        fclose(f_comp_out);

        // Corrupt the stored source size in footer (last 12 bytes: [src_size(8)] + [hash(4)])
        const size_t footer_off = comp_file_sz - ZXC_FILE_FOOTER_SIZE;
        comp_data[footer_off] ^= 0x01;  // Flip a bit in stored source size

        FILE* f_corrupt = tmpfile();
        FILE* f_dec_out = tmpfile();
        fwrite(comp_data, 1, comp_file_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, 1, 1);
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

        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, 1, 3, 1);
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
        fread(comp_data, 1, comp_file_sz, f_comp_out);
        fclose(f_comp_out);

        // Corrupt the global checksum (last 4 bytes)
        comp_data[comp_file_sz - 1] ^= 0xFF;

        FILE* f_corrupt = tmpfile();
        FILE* f_dec_out = tmpfile();
        fwrite(comp_data, 1, comp_file_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, 1, 1);
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

        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, 1, 3, 0);
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
        fread(comp_data, 1, trunc_sz, f_comp_out);
        fclose(f_comp_out);

        FILE* f_corrupt = tmpfile();
        FILE* f_dec_out = tmpfile();
        fwrite(comp_data, 1, trunc_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, 1, 0);
        fclose(f_corrupt);
        fclose(f_dec_out);
        // Should fail: missing EOF/footer means io_error or bad read
        if (r >= 0) {
            printf("  [FAIL] truncated stream: expected < 0, got %lld\n", (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_stream_decompress truncated -> negative\n");

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

    // Edge Cases: 1-byte file
    gen_random_data(buffer, 1);
    if (!test_round_trip("1-byte Input", buffer, 1, 3, 0)) total_failures++;
    if (!test_round_trip("1-byte Input (with checksum)", buffer, 1, 3, 1)) total_failures++;

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

    printf("\n--- Test Coverage: Repetitive Pattern Encoding ---\n");

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

    if (!test_null_output_decompression()) total_failures++;

    if (!test_max_compressed_size_logic()) total_failures++;
    if (!test_invalid_arguments()) total_failures++;
    if (!test_truncated_input()) total_failures++;
    if (!test_io_failures()) total_failures++;
    if (!test_thread_params()) total_failures++;
    if (!test_bit_reader()) total_failures++;
    if (!test_bitpack()) total_failures++;
    if (!test_eof_block_structure()) total_failures++;
    if (!test_header_checksum()) total_failures++;
    if (!test_global_checksum_order()) total_failures++;
    if (!test_get_decompressed_size()) total_failures++;
    if (!test_error_name()) total_failures++;
    if (!test_buffer_error_codes()) total_failures++;
    if (!test_stream_get_decompressed_size_errors()) total_failures++;
    if (!test_stream_engine_errors()) total_failures++;

    if (total_failures > 0) {
        printf("FAILED: %d tests failed.\n", total_failures);
        return 1;
    }

    printf("ALL TESTS PASSED SUCCESSFULLY.\n");
    return 0;
}