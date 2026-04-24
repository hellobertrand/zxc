/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

// Checks that the utility function calculates a sufficient size
int test_max_compressed_size_logic() {
    printf("=== TEST: Unit - zxc_compress_bound ===\n");

    // Case 1: 0 bytes (must at least contain the header)
    size_t sz0 = (size_t)zxc_compress_bound(0);
    if (sz0 == 0) {
        printf("Failed: Size for 0 bytes should not be 0 (headers required)\n");
        return 0;
    }

    // Case 2: Small input
    size_t input_val = 100;
    size_t sz100 = (size_t)zxc_compress_bound(input_val);
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

// Checks the buffer-based API (zxc_compress / zxc_decompress)
int test_buffer_api() {
    printf("=== TEST: Unit - Buffer API (zxc_compress/zxc_decompress) ===\n");

    size_t src_size = 128 * 1024;
    uint8_t* src = malloc(src_size);
    gen_lz_data(src, src_size);

    // 1. Calculate max compressed size
    size_t max_dst_size = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = malloc(max_dst_size);
    int checksum_enabled = 1;

    // 2. Compress
    zxc_compress_opts_t _co23 = {.level = 3, .checksum_enabled = checksum_enabled};
    int64_t compressed_size = zxc_compress(src, src_size, compressed, max_dst_size, &_co23);
    if (compressed_size <= 0) {
        printf("Failed: zxc_compress returned %lld\n", (long long)compressed_size);
        free(src);
        free(compressed);
        return 0;
    }
    printf("Compressed %zu bytes to %lld bytes\n", src_size, (long long)compressed_size);

    // 3. Decompress
    uint8_t* decompressed = malloc(src_size);
    zxc_decompress_opts_t _do24 = {.checksum_enabled = checksum_enabled};
    int64_t decompressed_size =
        zxc_decompress(compressed, (size_t)compressed_size, decompressed, src_size, &_do24);

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
    size_t small_capacity = (size_t)(compressed_size / 2);
    zxc_compress_opts_t _co25 = {.level = 3, .checksum_enabled = checksum_enabled};
    int64_t small_res = zxc_compress(src, src_size, compressed, small_capacity, &_co25);
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

// Test zxc_get_decompressed_size
int test_get_decompressed_size() {
    printf("=== TEST: Unit - zxc_get_decompressed_size ===\n");

    // 1. Compress some data, then check decompressed size
    size_t src_size = 64 * 1024;
    uint8_t* src = malloc(src_size);
    gen_lz_data(src, src_size);

    size_t max_dst = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = malloc(max_dst);

    zxc_compress_opts_t _co29 = {.level = 3, .checksum_enabled = 0};
    int64_t comp_size = zxc_compress(src, src_size, compressed, max_dst, &_co29);
    if (comp_size <= 0) {
        printf("Failed: Compression returned 0\n");
        free(src);
        free(compressed);
        return 0;
    }

    size_t reported = (size_t)zxc_get_decompressed_size(compressed, comp_size);
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

int test_buffer_error_codes() {
    printf("=== TEST: Unit - Buffer API Error Codes ===\n");

    /* ------------------------------------------------------------------ */
    /* zxc_compress error paths                                           */
    /* ------------------------------------------------------------------ */

    // 1. NULL src
    zxc_compress_opts_t _co30 = {.level = 3, .checksum_enabled = 0};
    int64_t r = zxc_compress(NULL, 100, (void*)1, 100, &_co30);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] NULL src: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT, (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_compress NULL src -> ZXC_ERROR_NULL_INPUT\n");

    // 2. NULL dst
    zxc_compress_opts_t _co31 = {.level = 3, .checksum_enabled = 0};
    r = zxc_compress((void*)1, 100, NULL, 100, &_co31);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] NULL dst: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT, (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_compress NULL dst -> ZXC_ERROR_NULL_INPUT\n");

    // 3. src_size == 0
    uint8_t dummy[16];
    zxc_compress_opts_t _co32 = {.level = 3, .checksum_enabled = 0};
    r = zxc_compress(dummy, 0, dummy, sizeof(dummy), &_co32);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] src_size==0: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT, (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_compress src_size==0 -> ZXC_ERROR_NULL_INPUT\n");

    // 4. dst_capacity == 0
    zxc_compress_opts_t _co33 = {.level = 3, .checksum_enabled = 0};
    r = zxc_compress(dummy, sizeof(dummy), dummy, 0, &_co33);
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
        zxc_compress_opts_t _co34 = {.level = 3, .checksum_enabled = 0};
        r = zxc_compress(src, sizeof(src), dst, sizeof(dst), &_co34);
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
        zxc_compress_opts_t _co35 = {.level = 3, .checksum_enabled = 0};
        r = zxc_compress(src, src_sz, dst, small_dst, &_co35);
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
        const size_t full_cap = (size_t)zxc_compress_bound(src_sz);
        uint8_t* full_dst = malloc(full_cap);
        zxc_compress_opts_t _co36 = {.level = 3, .checksum_enabled = 0};
        const int64_t full_sz = zxc_compress(src, src_sz, full_dst, full_cap, &_co36);
        if (full_sz <= 0) {
            printf("  [SKIP] Cannot prepare for EOF test\n");
            free(src);
            free(full_dst);
        } else {
            // EOF header(8) + footer(12) = 20 bytes at the end.
            // Try with a buffer that's just a few bytes too small.
            const size_t tight = (size_t)full_sz - 5;
            uint8_t* tight_dst = malloc(tight);
            zxc_compress_opts_t _co37 = {.level = 3, .checksum_enabled = 0};
            r = zxc_compress(src, src_sz, tight_dst, tight, &_co37);
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
    zxc_decompress_opts_t _do38 = {.checksum_enabled = 0};
    r = zxc_decompress(NULL, 100, (void*)1, 100, &_do38);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] decompress NULL src: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT,
               (long long)r);
        return 0;
    }
    printf("  [PASS] zxc_decompress NULL src -> ZXC_ERROR_NULL_INPUT\n");

    // 9. NULL dst
    zxc_decompress_opts_t _do39 = {.checksum_enabled = 0};
    r = zxc_decompress((void*)1, 100, NULL, 100, &_do39);
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
        zxc_decompress_opts_t _do40 = {.checksum_enabled = 0};
        r = zxc_decompress(tiny, sizeof(tiny), out, sizeof(out), &_do40);
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
        zxc_decompress_opts_t _do41 = {.checksum_enabled = 0};
        r = zxc_decompress(bad_src, sizeof(bad_src), out, sizeof(out), &_do41);
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
    const size_t comp_cap = (size_t)zxc_compress_bound(test_src_sz);
    uint8_t* comp_buf = malloc(comp_cap);
    zxc_compress_opts_t _co42 = {.level = 3, .checksum_enabled = 1};
    const int64_t comp_sz = zxc_compress(test_src, test_src_sz, comp_buf, comp_cap, &_co42);
    if (comp_sz <= 0) {
        printf("  [FAIL] Could not prepare compressed data\n");
        free(test_src);
        free(comp_buf);
        return 0;
    }

    // 12. Corrupt block header (damage the first block header byte after file header)
    {
        uint8_t* corrupt = malloc((size_t)comp_sz);
        memcpy(corrupt, comp_buf, (size_t)comp_sz);
        // Corrupt the block type byte at offset ZXC_FILE_HEADER_SIZE
        corrupt[ZXC_FILE_HEADER_SIZE] = 0xFF;  // Invalid block type
        uint8_t* out = malloc(test_src_sz);
        zxc_decompress_opts_t _do43 = {.checksum_enabled = 1};
        r = zxc_decompress(corrupt, (size_t)comp_sz, out, test_src_sz, &_do43);
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
        zxc_decompress_opts_t _do44 = {.checksum_enabled = 1};
        r = zxc_decompress(comp_buf, trunc_sz, out, test_src_sz, &_do44);
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
        uint8_t* corrupt = malloc((size_t)comp_sz);
        memcpy(corrupt, comp_buf, (size_t)comp_sz);
        // Footer is at end: last 12 bytes = [src_size(8)] + [global_hash(4)]
        // Corrupt the source size field (add 1 to the first byte)
        const size_t footer_offset = (size_t)comp_sz - ZXC_FILE_FOOTER_SIZE;
        corrupt[footer_offset] ^= 0x01;  // Flip a bit in the stored source size
        uint8_t* out = malloc(test_src_sz);
        zxc_decompress_opts_t _do45 = {.checksum_enabled = 1};
        r = zxc_decompress(corrupt, (size_t)comp_sz, out, test_src_sz, &_do45);
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
        uint8_t* corrupt = malloc((size_t)comp_sz);
        memcpy(corrupt, comp_buf, (size_t)comp_sz);
        // Global hash is the last 4 bytes of the file
        corrupt[comp_sz - 1] ^= 0xFF;
        uint8_t* out = malloc(test_src_sz);
        zxc_decompress_opts_t _do46 = {.checksum_enabled = 1};
        r = zxc_decompress(corrupt, (size_t)comp_sz, out, test_src_sz, &_do46);
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
        zxc_decompress_opts_t _do47 = {.checksum_enabled = 0};
        r = zxc_decompress(comp_buf, (size_t)comp_sz, out, test_src_sz / 4, &_do47);
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

// Tests the buffer API scratch buffer (work_buf) used to safely absorb
// zxc_copy32 wild-copy overshoot during decompression.
int test_buffer_api_scratch_buf() {
    printf("=== TEST: Unit - Buffer API Scratch Buffer (work_buf) ===\n");

    // 1. Small data roundtrip (177 bytes)
    {
        const size_t sz = 177;
        uint8_t src[177];
        gen_lz_data(src, sz);

        const size_t comp_cap = (size_t)zxc_compress_bound(sz);
        uint8_t* comp = malloc(comp_cap);
        zxc_compress_opts_t _co58 = {.level = 3, .checksum_enabled = 0};
        const int64_t comp_sz = zxc_compress(src, sz, comp, comp_cap, &_co58);
        if (comp_sz <= 0) {
            printf("  [FAIL] compress 177B\n");
            free(comp);
            return 0;
        }

        uint8_t dec[177];
        zxc_decompress_opts_t _do59 = {.checksum_enabled = 0};
        const int64_t dec_sz = zxc_decompress(comp, (size_t)comp_sz, dec, sz, &_do59);
        if (dec_sz != (int64_t)sz || memcmp(src, dec, sz) != 0) {
            printf("  [FAIL] roundtrip 177B\n");
            free(comp);
            return 0;
        }
        free(comp);
        printf("  [PASS] small data roundtrip (177 bytes)\n");
    }

    // 2. Exact-fit destination (dst_capacity == decompressed size, no slack)
    {
        const size_t sz = 1024;
        uint8_t* src = malloc(sz);
        gen_lz_data(src, sz);

        const size_t comp_cap = (size_t)zxc_compress_bound(sz);
        uint8_t* comp = malloc(comp_cap);
        zxc_compress_opts_t _co60 = {.level = 1, .checksum_enabled = 1};
        const int64_t comp_sz = zxc_compress(src, sz, comp, comp_cap, &_co60);
        if (comp_sz <= 0) {
            printf("  [FAIL] compress 1KB\n");
            free(src);
            free(comp);
            return 0;
        }

        uint8_t* dec = malloc(sz);  // exactly sz, no extra room
        zxc_decompress_opts_t _do61 = {.checksum_enabled = 1};
        const int64_t dec_sz = zxc_decompress(comp, (size_t)comp_sz, dec, sz, &_do61);
        if (dec_sz != (int64_t)sz || memcmp(src, dec, sz) != 0) {
            printf("  [FAIL] exact-fit 1KB\n");
            free(src);
            free(comp);
            free(dec);
            return 0;
        }
        free(src);
        free(comp);
        free(dec);
        printf("  [PASS] exact-fit destination (1KB)\n");
    }

    // 3. Tiny data (1 byte)
    {
        const uint8_t src = 0x42;
        const size_t comp_cap = (size_t)zxc_compress_bound(1);
        uint8_t* comp = malloc(comp_cap);
        zxc_compress_opts_t _co62 = {.level = 1, .checksum_enabled = 0};
        const int64_t comp_sz = zxc_compress(&src, 1, comp, comp_cap, &_co62);
        if (comp_sz <= 0) {
            printf("  [FAIL] compress 1B\n");
            free(comp);
            return 0;
        }

        uint8_t dec = 0;
        zxc_decompress_opts_t _do63 = {.checksum_enabled = 0};
        const int64_t dec_sz = zxc_decompress(comp, (size_t)comp_sz, &dec, 1, &_do63);
        if (dec_sz != 1 || dec != 0x42) {
            printf("  [FAIL] roundtrip 1B\n");
            free(comp);
            return 0;
        }
        free(comp);
        printf("  [PASS] tiny data roundtrip (1 byte)\n");
    }

    // 4. Malformed input must not crash (safe error return)
    {
        uint8_t garbage[64];
        for (int i = 0; i < 64; i++) garbage[i] = (uint8_t)(i * 37);
        uint8_t out[256];
        zxc_decompress_opts_t _do64 = {.checksum_enabled = 0};
        const int64_t r = zxc_decompress(garbage, sizeof(garbage), out, sizeof(out), &_do64);
        if (r >= 0) {
            printf("  [FAIL] malformed input should return < 0\n");
            return 0;
        }
        printf("  [PASS] malformed input -> error %lld (no crash)\n", (long long)r);
    }

    // 5. Destination too small
    {
        const size_t sz = 512;
        uint8_t* src = malloc(sz);
        gen_lz_data(src, sz);

        const size_t comp_cap = (size_t)zxc_compress_bound(sz);
        uint8_t* comp = malloc(comp_cap);
        zxc_compress_opts_t _co65 = {.level = 1, .checksum_enabled = 0};
        const int64_t comp_sz = zxc_compress(src, sz, comp, comp_cap, &_co65);
        if (comp_sz <= 0) {
            printf("  [FAIL] compress 512B\n");
            free(src);
            free(comp);
            return 0;
        }

        uint8_t tiny_dst[8];
        zxc_decompress_opts_t _do66 = {.checksum_enabled = 0};
        const int64_t r = zxc_decompress(comp, (size_t)comp_sz, tiny_dst, sizeof(tiny_dst), &_do66);
        if (r >= 0) {
            printf("  [FAIL] dst too small should return < 0\n");
            free(src);
            free(comp);
            return 0;
        }
        free(src);
        free(comp);
        printf("  [PASS] zxc_decompress dst too small -> negative\n");
    }

    printf("PASS\n\n");
    return 1;
}

// Tests that the two decompression paths in zxc_decompress() produce
// identical results:
//   - Fast path: rem_cap >= runtime_chunk_size + ZXC_PAD_SIZE
//     -> decompress directly into dst (enough padding for wild copies).
//   - Safe path: rem_cap < runtime_chunk_size + ZXC_PAD_SIZE
//     -> decompress into bounce buffer (work_buf), then memcpy exact result.
//
int test_decompress_fast_vs_safe_path() {
    printf("=== TEST: Unit - Decompress Fast Path vs Safe Path ===\n");

    // Use a multi-block input: ZXC_BLOCK_SIZE_DEFAULT + extra so we get at least 2 blocks.
    // Block size = 256KB (ZXC_BLOCK_SIZE_DEFAULT). Second block is small.
    const size_t src_sz = ZXC_BLOCK_SIZE_DEFAULT + 4096;  // 256KB + 4KB -> 2 blocks
    uint8_t* src = malloc(src_sz);
    if (!src) return 0;
    gen_lz_data(src, src_sz);

    const size_t comp_cap = (size_t)zxc_compress_bound(src_sz);
    uint8_t* comp = malloc(comp_cap);
    zxc_compress_opts_t _co67 = {.level = 3, .checksum_enabled = 1};
    const int64_t comp_sz = zxc_compress(src, src_sz, comp, comp_cap, &_co67);
    if (comp_sz <= 0) {
        printf("  [FAIL] compression failed\n");
        free(src);
        free(comp);
        return 0;
    }

    // ----- Sub-test 1: Fast path -----
    // Provide a very large dst buffer so all chunks decompress directly into
    // dst (rem_cap >= runtime_chunk_size + ZXC_PAD_SIZE at every iteration).
    {
        const size_t big_cap = src_sz + ZXC_BLOCK_SIZE_DEFAULT;  // way more than enough
        uint8_t* dst = malloc(big_cap);
        zxc_decompress_opts_t _do68 = {.checksum_enabled = 1};
        const int64_t dec_sz = zxc_decompress(comp, (size_t)comp_sz, dst, big_cap, &_do68);
        if (dec_sz != (int64_t)src_sz) {
            printf("  [FAIL] fast path size: expected %zu, got %lld\n", src_sz, (long long)dec_sz);
            free(dst);
            free(src);
            free(comp);
            return 0;
        }
        if (memcmp(src, dst, src_sz) != 0) {
            printf("  [FAIL] fast path content mismatch\n");
            free(dst);
            free(src);
            free(comp);
            return 0;
        }
        free(dst);
        printf("  [PASS] fast path (oversized dst)\n");
    }

    // ----- Sub-test 2: Safe path (exact-fit) -----
    // Provide dst_capacity == src_sz exactly. After the first 256KB block is
    // written, rem_cap for the second block (4KB) is exactly 4KB which is
    // < runtime_chunk_size (256KB) + ZXC_PAD_SIZE (32). This forces the
    // safe path (bounce buffer) for the second block.
    {
        uint8_t* dst = malloc(src_sz);  // no slack at all
        zxc_decompress_opts_t _do69 = {.checksum_enabled = 1};
        const int64_t dec_sz = zxc_decompress(comp, (size_t)comp_sz, dst, src_sz, &_do69);
        if (dec_sz != (int64_t)src_sz) {
            printf("  [FAIL] safe path size: expected %zu, got %lld\n", src_sz, (long long)dec_sz);
            free(dst);
            free(src);
            free(comp);
            return 0;
        }
        if (memcmp(src, dst, src_sz) != 0) {
            printf("  [FAIL] safe path content mismatch\n");
            free(dst);
            free(src);
            free(comp);
            return 0;
        }
        free(dst);
        printf("  [PASS] safe path (exact-fit dst)\n");
    }

    // ----- Sub-test 3: Boundary -----
    // dst_capacity = src_sz + ZXC_PAD_SIZE - 1 (just below the fast path
    // threshold for the LAST chunk). The last chunk should still fall into
    // the safe path here.
    {
        const size_t tight_cap = src_sz + ZXC_PAD_SIZE - 1;
        uint8_t* dst = malloc(tight_cap);
        zxc_decompress_opts_t _do70 = {.checksum_enabled = 1};
        const int64_t dec_sz = zxc_decompress(comp, (size_t)comp_sz, dst, tight_cap, &_do70);
        if (dec_sz != (int64_t)src_sz) {
            printf("  [FAIL] boundary size: expected %zu, got %lld\n", src_sz, (long long)dec_sz);
            free(dst);
            free(src);
            free(comp);
            return 0;
        }
        if (memcmp(src, dst, src_sz) != 0) {
            printf("  [FAIL] boundary content mismatch\n");
            free(dst);
            free(src);
            free(comp);
            return 0;
        }
        free(dst);
        printf("  [PASS] boundary (dst = src_sz + PAD - 1)\n");
    }

    // ----- Sub-test 4: Safe path with dst too small -----
    // The safe path detects that the decompressed chunk doesn't fit and
    // returns ZXC_ERROR_DST_TOO_SMALL (covers the res > rem_cap guard).
    {
        const size_t tiny_cap = ZXC_BLOCK_SIZE_DEFAULT / 2;  // Enough for half a block
        uint8_t* dst = malloc(tiny_cap);
        zxc_decompress_opts_t _do71 = {.checksum_enabled = 0};
        const int64_t dec_sz = zxc_decompress(comp, (size_t)comp_sz, dst, tiny_cap, &_do71);
        if (dec_sz >= 0) {
            printf("  [FAIL] safe path dst-too-small should fail, got %lld\n", (long long)dec_sz);
            free(dst);
            free(src);
            free(comp);
            return 0;
        }
        free(dst);
        printf("  [PASS] safe path dst too small -> negative\n");
    }

    free(src);
    free(comp);
    printf("PASS\n\n");
    return 1;
}
