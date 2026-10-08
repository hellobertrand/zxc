// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

#include "test_common.h"

// Checks that the utility function calculates a sufficient size
int test_max_compressed_size_logic(void) {
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
int test_buffer_api(void) {
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
int test_get_decompressed_size(void) {
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

    // 4. Forged footer: an implausible size (far beyond what the archive's
    //    block count could decode to) must return 0, not drive a huge
    //    allocation in callers that size buffers from this value.
    uint8_t* forged = malloc((size_t)comp_size + ZXC_FILE_FOOTER_MAX_SIZE);
    const size_t forged_sz =
        test_forge_footer_size(compressed, (size_t)comp_size, forged,
                               (size_t)comp_size + ZXC_FILE_FOOTER_MAX_SIZE, UINT64_MAX);
    if (forged_sz == 0 || zxc_get_decompressed_size(forged, forged_sz) != 0) {
        printf("Failed: Should return 0 for a forged (implausible) footer size\n");
        free(forged);
        free(src);
        free(compressed);
        return 0;
    }
    free(forged);
    printf("  [PASS] Forged footer size rejected\n");

    printf("PASS\n\n");
    free(src);
    free(compressed);
    return 1;
}

int test_buffer_error_codes(void) {
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

    // 3. src_size == 0 produces a valid empty frame (header + EOF + footer)
    uint8_t dummy[16];
    uint8_t empty_dst[64];
    zxc_compress_opts_t _co32 = {.level = 3, .checksum_enabled = 0};
    r = zxc_compress(NULL, 0, empty_dst, sizeof(empty_dst), &_co32);
    if (r <= 0) {
        printf("  [FAIL] src_size==0: expected valid frame, got %lld\n", (long long)r);
        return 0;
    }
    uint64_t orig = zxc_get_decompressed_size(empty_dst, (size_t)r);
    if (orig != 0) {
        printf("  [FAIL] src_size==0: decompressed size %llu != 0\n", (unsigned long long)orig);
        return 0;
    }
    printf("  [PASS] zxc_compress src_size==0 -> valid empty frame (%lld bytes)\n", (long long)r);

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
            // The tail: EOF block (8), then a footer of 3 to 17 bytes.
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

    // 10. src too small for file header. Truncation, not a NULL pointer.
    {
        uint8_t tiny[4] = {0};
        uint8_t out[64];
        zxc_decompress_opts_t _do40 = {.checksum_enabled = 0};
        r = zxc_decompress(tiny, sizeof(tiny), out, sizeof(out), &_do40);
        if (r != ZXC_ERROR_SRC_TOO_SMALL) {
            printf("  [FAIL] src too small: expected %d, got %lld\n", ZXC_ERROR_SRC_TOO_SMALL,
                   (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_decompress src too small -> ZXC_ERROR_SRC_TOO_SMALL\n");

    // 10b. Too small for its own footer: [header][EOF] alone has none. Read
    //      from the end regardless, the "footer" would be EOF bytes; the walk
    //      and the no-destination probe both say short.
    {
        uint8_t arc[ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE];
        const zxc_block_header_t eof = {
            .block_type = ZXC_BLOCK_EOF, .block_flags = 0, .reserved = 0, .comp_size = 0};
        if (zxc_write_file_header(arc, ZXC_FILE_HEADER_SIZE, 4096, 1, 0, 0) < 0 ||
            zxc_write_block_header(arc + ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &eof) < 0) {
            printf("  [FAIL] fixture headers\n");
            return 0;
        }
        uint8_t out[64];
        zxc_decompress_opts_t o = {.checksum_enabled = 0};
        const int64_t walk = zxc_decompress(arc, sizeof(arc), out, sizeof(out), &o);
        const int64_t probe = zxc_decompress(arc, sizeof(arc), NULL, 0, &o);
        if (walk != ZXC_ERROR_SRC_TOO_SMALL || probe != ZXC_ERROR_SRC_TOO_SMALL) {
            printf("  [FAIL] short of its footer: walk %lld, probe %lld, want %d\n",
                   (long long)walk, (long long)probe, ZXC_ERROR_SRC_TOO_SMALL);
            return 0;
        }
    }
    printf("  [PASS] zxc_decompress short of its footer -> ZXC_ERROR_SRC_TOO_SMALL\n");

    // 11. Bad file header (invalid magic). The header reader's verdict is
    //     forwarded, so this reports the magic, not a catch-all.
    {
        uint8_t bad_src[64];
        memset(bad_src, 0, sizeof(bad_src));
        uint8_t out[64];
        zxc_decompress_opts_t _do41 = {.checksum_enabled = 0};
        r = zxc_decompress(bad_src, sizeof(bad_src), out, sizeof(out), &_do41);
        if (r != ZXC_ERROR_BAD_MAGIC) {
            printf("  [FAIL] bad magic: expected %d, got %lld\n", ZXC_ERROR_BAD_MAGIC,
                   (long long)r);
            return 0;
        }
    }
    printf("  [PASS] zxc_decompress bad magic -> ZXC_ERROR_BAD_MAGIC\n");

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
        // Cut most of the footer
        const size_t trunc_sz = (size_t)comp_sz - test_footer_len(comp_buf, (size_t)comp_sz) + 2;
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
        // Exact code: aimed at the digest instead, this passes on BAD_CHECKSUM
        // without testing the size.
        corrupt[test_footer_size_at(comp_buf, (size_t)comp_sz)] ^= 0x01;
        uint8_t* out = malloc(test_src_sz);
        zxc_decompress_opts_t _do45 = {.checksum_enabled = 1};
        r = zxc_decompress(corrupt, (size_t)comp_sz, out, test_src_sz, &_do45);
        if (r != ZXC_ERROR_CORRUPT_DATA) {
            printf("  [FAIL] size mismatch: expected %d, got %lld\n", ZXC_ERROR_CORRUPT_DATA,
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
    printf("  [PASS] zxc_decompress stored size mismatch -> negative\n");

    // 15. Block checksum failure (corrupt the last block's trailing checksum)
    {
        uint8_t* corrupt = malloc((size_t)comp_sz);
        memcpy(corrupt, comp_buf, (size_t)comp_sz);
        // Last byte before the EOF block: the last block's checksum.
        corrupt[(size_t)comp_sz - test_footer_len(comp_buf, (size_t)comp_sz) -
                ZXC_BLOCK_HEADER_SIZE - 1] ^= 0xFF;
        uint8_t* out = malloc(test_src_sz);
        zxc_decompress_opts_t _do46 = {.checksum_enabled = 1};
        r = zxc_decompress(corrupt, (size_t)comp_sz, out, test_src_sz, &_do46);
        if (r != ZXC_ERROR_BAD_CHECKSUM) {
            printf("  [FAIL] bad block checksum: expected %d, got %lld\n", ZXC_ERROR_BAD_CHECKSUM,
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
    printf("  [PASS] zxc_decompress block checksum -> ZXC_ERROR_BAD_CHECKSUM\n");

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

// Tests the dst=NULL / dst_capacity=0 short-circuit in zxc_decompress:
// allowed only when the compressed frame's stored size is 0.
int test_decompress_empty_frame_null_dst(void) {
    printf("=== TEST: Unit - Decompress empty frame with NULL/zero dst ===\n");

    /* 1. Produce a valid empty frame via zxc_compress(NULL, 0, ...). */
    uint8_t empty_frame[64];
    int64_t comp_sz = zxc_compress(NULL, 0, empty_frame, sizeof(empty_frame), NULL);
    if (comp_sz <= 0) {
        printf("  [FAIL] empty compress: got %lld\n", (long long)comp_sz);
        return 0;
    }

    /* 2. dst=NULL, dst_capacity=0 on empty frame -> 0 */
    int64_t r = zxc_decompress(empty_frame, (size_t)comp_sz, NULL, 0, NULL);
    if (r != 0) {
        printf("  [FAIL] NULL dst + 0 cap on empty frame: expected 0, got %lld\n", (long long)r);
        return 0;
    }
    printf("  [PASS] NULL dst + 0 cap on empty frame -> 0\n");

    /* 3. dst=valid, dst_capacity=0 on empty frame -> 0 */
    uint8_t dummy;
    r = zxc_decompress(empty_frame, (size_t)comp_sz, &dummy, 0, NULL);
    if (r != 0) {
        printf("  [FAIL] valid dst + 0 cap on empty frame: expected 0, got %lld\n", (long long)r);
        return 0;
    }
    printf("  [PASS] valid dst + 0 cap on empty frame -> 0\n");

    /* 4. dst=NULL, dst_capacity=0 on non-empty frame -> ZXC_ERROR_DST_TOO_SMALL */
    uint8_t payload[128] = "hello";
    uint8_t non_empty_frame[256];
    int64_t ne_sz =
        zxc_compress(payload, sizeof(payload), non_empty_frame, sizeof(non_empty_frame), NULL);
    if (ne_sz <= 0) {
        printf("  [FAIL] non-empty compress: got %lld\n", (long long)ne_sz);
        return 0;
    }
    r = zxc_decompress(non_empty_frame, (size_t)ne_sz, NULL, 0, NULL);
    if (r != ZXC_ERROR_DST_TOO_SMALL) {
        printf("  [FAIL] NULL dst + 0 cap on non-empty frame: expected %d, got %lld\n",
               ZXC_ERROR_DST_TOO_SMALL, (long long)r);
        return 0;
    }
    printf("  [PASS] NULL dst + 0 cap on non-empty frame -> DST_TOO_SMALL\n");

    /* 5. dst=NULL, dst_capacity=0 on bad magic. The pointers are fine here; what
     *    is wrong is the archive, so the magic check is what must be reported. */
    uint8_t bad_magic[64];
    memcpy(bad_magic, empty_frame, (size_t)comp_sz);
    bad_magic[0] ^= 0xFF;
    r = zxc_decompress(bad_magic, (size_t)comp_sz, NULL, 0, NULL);
    if (r != ZXC_ERROR_BAD_MAGIC) {
        printf("  [FAIL] NULL dst + 0 cap + bad magic: expected %d, got %lld\n",
               ZXC_ERROR_BAD_MAGIC, (long long)r);
        return 0;
    }
    printf("  [PASS] NULL dst + 0 cap + bad magic -> BAD_MAGIC\n");

    /* 6. Caller bug: dst=NULL with non-zero capacity -> ZXC_ERROR_NULL_INPUT */
    r = zxc_decompress(empty_frame, (size_t)comp_sz, NULL, 100, NULL);
    if (r != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] NULL dst + cap>0: expected %d, got %lld\n", ZXC_ERROR_NULL_INPUT,
               (long long)r);
        return 0;
    }
    printf("  [PASS] NULL dst + cap>0 -> NULL_INPUT\n");

    printf("PASS\n\n");
    return 1;
}

// Tests the buffer API scratch buffer (work_buf) used to safely absorb
// zxc_copy32 wild-copy overshoot during decompression.
int test_buffer_api_scratch_buf(void) {
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
int test_decompress_fast_vs_safe_path(void) {
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

/* In-place decompression: compressed placed flush-right in one buffer, decoded
 * left-to-right into the same buffer. Covers a compressible input, a
 * high-entropy input whose flush-right archive OVERLAPS the output region
 * (the core in-place invariant), and the too-small-buffer rejection. */
static int inplace_case(const char* label, const uint8_t* orig, size_t n, int level, int checksum,
                        size_t block_size, int seekable) {
    const size_t cbound = (size_t)zxc_compress_bound(n);
    uint8_t* comp = (uint8_t*)malloc(cbound);
    if (!comp) return 0;
    zxc_compress_opts_t co = {.level = level,
                              .checksum_enabled = checksum,
                              .block_size = block_size,
                              .seekable = seekable};
    const int64_t c = zxc_compress(orig, n, comp, cbound, &co);
    if (c <= 0) {
        printf("Failed [%s]: compress -> %lld\n", label, (long long)c);
        free(comp);
        return 0;
    }
    const size_t csz = (size_t)c;

    const size_t need = zxc_decompress_inplace_bound(comp, csz);
    if (need == 0 || need < n || need < csz) {
        printf("Failed [%s]: bound %zu (n=%zu, comp=%zu)\n", label, need, n, csz);
        free(comp);
        return 0;
    }
    uint8_t* buf = (uint8_t*)malloc(need);
    if (!buf) {
        free(comp);
        return 0;
    }
    memset(buf, 0xCC, need);
    memcpy(buf + (need - csz), comp, csz); /* flush-right */

    zxc_decompress_opts_t dop = {.checksum_enabled = checksum};
    const int64_t d = zxc_decompress_inplace(buf, need, csz, &dop);
    if (d < 0 || (size_t)d != n || memcmp(buf, orig, n) != 0) {
        printf("Failed [%s]: inplace ret=%lld want=%zu%s\n", label, (long long)d, n,
               (d == (int64_t)n) ? " (MISMATCH)" : "");
        free(comp);
        free(buf);
        return 0;
    }

    /* one byte short of the required margin must be rejected, never corrupt. */
    const size_t tight = need - 1;
    if (tight >= csz) {
        uint8_t* b2 = (uint8_t*)malloc(tight);
        if (b2) {
            memcpy(b2 + (tight - csz), comp, csz);
            const int64_t r = zxc_decompress_inplace(b2, tight, csz, &dop);
            if (r != ZXC_ERROR_DST_TOO_SMALL) {
                printf("Failed [%s]: undersized buffer not rejected (%lld)\n", label, (long long)r);
                free(b2);
                free(comp);
                free(buf);
                return 0;
            }
            free(b2);
        }
    }
    const size_t comp_start = need - csz;
    printf("  [PASS] %s (n=%zu, comp=%zu, comp@%zu %s)\n", label, n, csz, comp_start,
           comp_start < n ? "OVERLAPS output" : "in margin");
    free(comp);
    free(buf);
    return 1;
}

/* A footer size the archive cannot possibly hold must not become the caller's
 * allocation: one flipped byte in a 354-byte archive used to answer a 68 GB
 * bound, and the decoder blamed the header for it. */
static int inplace_forged_footer(void) {
    uint8_t in[4096];
    for (size_t i = 0; i < sizeof(in); i++) in[i] = (uint8_t)(i * 7);
    uint8_t comp[8192];
    const int64_t c = zxc_compress(in, sizeof(in), comp, sizeof(comp), NULL);
    if (c <= 0) {
        printf("Failed [forged footer]: compress -> %lld\n", (long long)c);
        return 0;
    }
    const size_t csz = (size_t)c;
    const size_t need = zxc_decompress_inplace_bound(comp, csz);
    uint8_t* const buf = (uint8_t*)malloc(need);
    if (!buf) return 0;
    uint8_t bad[8192];
    int ok = 1;

    /* A size no archive of this length can reach. */
    size_t bsz = test_forge_footer_size(comp, csz, bad, sizeof(bad), 0x1000000000ULL); /* ~68 GB */
    const size_t inflated = bsz ? zxc_decompress_inplace_bound(bad, bsz) : 1;
    if (inflated != 0) {
        printf("Failed [forged footer]: bound %zu, want 0\n", inflated);
        ok = 0;
    }

    /* The verdict must name the footer, not the header, which is intact. */
    bsz = test_forge_footer_size(comp, csz, bad, sizeof(bad), UINT64_MAX);
    int64_t d = ZXC_ERROR_MEMORY;
    if (bsz && bsz <= need) {
        memcpy(buf + need - bsz, bad, bsz);
        d = zxc_decompress_inplace(buf, need, bsz, NULL);
    }
    if (d != ZXC_ERROR_CORRUPT_DATA) {
        printf("Failed [forged footer]: inplace %lld, want %d\n", (long long)d,
               ZXC_ERROR_CORRUPT_DATA);
        ok = 0;
    }

    memcpy(bad, comp, csz);
    bad[0] ^= 0xFF;
    memcpy(buf + need - csz, bad, csz);
    d = zxc_decompress_inplace(buf, need, csz, NULL);
    if (d != ZXC_ERROR_BAD_MAGIC) {
        printf("Failed [forged footer]: bad magic %lld, want %d\n", (long long)d,
               ZXC_ERROR_BAD_MAGIC);
        ok = 0;
    }

    free(buf);
    if (ok) printf("  [PASS] forged footer refused, verdict names the footer\n");
    return ok;
}

/* Padding between the EOF block and the footer is refused: only the SEK block
 * belongs there (Sec 5.5). The frame loop used to skip it, which accepted hidden
 * bytes and let each one slide the in-place read/write separation closer to the
 * output, the archive size being attacker-controlled. The bound is still asserted:
 * it must stay conservative on a padded input, which the decode then refuses.
 *
 * All-RAW is the worst case for the margin; a compressible head then RAW blocks is
 * the worst for padding: the first RAW block used to start inside the write window,
 * its memcpy overlapping itself. */
static int inplace_padded_archive(void) {
    const size_t N = 64 * 1024;
    uint8_t* const orig = (uint8_t*)malloc(N);
    const size_t cbound = (size_t)zxc_compress_bound(N);
    uint8_t* const comp = (uint8_t*)malloc(cbound);
    if (!orig || !comp) {
        free(orig);
        free(comp);
        return 0;
    }

    int ok = 1;
    for (int shape = 0; shape < 2 && ok; shape++) {
        const char* const what = shape ? "mixed" : "all-RAW";
        gen_random_data(orig, N);
        if (shape) memset(orig, 0, N / 4);

        const zxc_compress_opts_t co = {.level = 1, .block_size = ZXC_BLOCK_SIZE_MIN};
        const int64_t c = zxc_compress(orig, N, comp, cbound, &co);
        if (c <= 0) {
            printf("Failed [padded %s]: compress -> %lld\n", what, (long long)c);
            ok = 0;
            break;
        }
        const size_t csz = (size_t)c;

        const size_t pads[] = {1, 4096, 20000, 100000};
        for (size_t i = 0; i < sizeof(pads) / sizeof(pads[0]) && ok; i++) {
            const size_t pad = pads[i];
            // Padding, then a footer re-signed to cover it, as a forger would.
            const size_t body = csz - test_footer_len(comp, csz);
            uint8_t* const a = (uint8_t*)malloc(csz + pad + ZXC_FILE_FOOTER_MAX_SIZE);
            if (!a) {
                ok = 0;
                break;
            }
            memcpy(a, comp, body);
            memset(a + body, 0xAA, pad);
            const int fw = zxc_write_file_footer(a + body + pad, ZXC_FILE_FOOTER_MAX_SIZE,
                                                 body + pad, N, 0, 0);
            const size_t c2 = body + pad + (fw > 0 ? (size_t)fw : 0);

            const size_t need = zxc_decompress_inplace_bound(a, c2);
            if (need < c2) {
                printf("Failed [padded %s]: pad=%zu bound %zu < archive %zu\n", what, pad, need,
                       c2);
                ok = 0;
            } else {
                uint8_t* const buf = (uint8_t*)malloc(need);
                if (buf) {
                    memcpy(buf + need - c2, a, c2);
                    const int64_t d = zxc_decompress_inplace(buf, need, c2, NULL);
                    if (d != ZXC_ERROR_CORRUPT_DATA) {
                        printf("Failed [padded %s]: pad=%zu inplace %lld want %d\n", what, pad,
                               (long long)d, ZXC_ERROR_CORRUPT_DATA);
                        ok = 0;
                    }
                    free(buf);
                }
            }
            free(a);
        }

        /* The bound an authentic archive gets must not have moved. */
        const size_t plain = zxc_decompress_inplace_bound(comp, csz);
        uint8_t* const buf = (uint8_t*)malloc(plain);
        if (buf) {
            memcpy(buf + plain - csz, comp, csz);
            if (zxc_decompress_inplace(buf, plain, csz, NULL) != (int64_t)N ||
                memcmp(buf, orig, N) != 0) {
                printf("Failed [padded %s]: unpadded archive regressed\n", what);
                ok = 0;
            }
            free(buf);
        }
    }

    free(comp);
    free(orig);
    if (ok) printf("  [PASS] padded archive keeps its read/write separation\n");
    return ok;
}

/* The margin counts the seek table exactly when HAS_SEEK_TABLE announces it. A
 * flag cleared over a table leaves those bytes uncounted, like inserted padding:
 * the write cursor may then run into input it has not read, and the decode must
 * refuse the archive, never return it. Mixed data (a compressible head, then
 * incompressible blocks) is the shape where the uncounted bytes bite. */
static int inplace_seek_flag_margin(void) {
    const size_t bs = ZXC_BLOCK_SIZE_MIN;
    const size_t nb = 256;
    const size_t n = nb * bs;
    const size_t cbound = (size_t)zxc_compress_bound(n);
    uint8_t* const src = (uint8_t*)malloc(n);
    uint8_t* const plain = (uint8_t*)malloc(cbound);
    uint8_t* const seek = (uint8_t*)malloc(cbound);
    int ok = src && plain && seek;
    if (ok) {
        gen_random_data(src, n);
        memset(src, 0, 8 * bs);
    }
    for (int cs = 0; cs <= 1 && ok; cs++) {
        zxc_compress_opts_t co = {.level = 1, .block_size = bs, .checksum_enabled = cs};
        const int64_t pl = zxc_compress(src, n, plain, cbound, &co);
        co.seekable = 1;
        const int64_t sl = zxc_compress(src, n, seek, cbound, &co);
        const size_t pb = pl > 0 ? zxc_decompress_inplace_bound(plain, (size_t)pl) : 0;
        const size_t sb = sl > 0 ? zxc_decompress_inplace_bound(seek, (size_t)sl) : 0;
        const size_t table = ZXC_BLOCK_HEADER_SIZE + (size_t)zxc_seek_table_bytes(nb);
        if (!pb || sb != pb + table) {
            printf("Failed [seek flag margin]: cs=%d bounds plain %zu, seekable %zu, want +%zu\n",
                   cs, pb, sb, table);
            ok = 0;
            break;
        }

        seek[6] &= (uint8_t)~ZXC_FILE_FLAG_HAS_SEEK_TABLE;
        zxc_file_header_sign(seek);
        const size_t lb = zxc_decompress_inplace_bound(seek, (size_t)sl);
        uint8_t* const buf = (uint8_t*)malloc(lb);
        if (!buf) {
            ok = 0;
            break;
        }
        memcpy(buf + lb - (size_t)sl, seek, (size_t)sl);
        const zxc_decompress_opts_t dop = {.checksum_enabled = 1};
        const int64_t r = zxc_decompress_inplace(buf, lb, (size_t)sl, &dop);
        free(buf);
        if (lb != pb || r >= 0) {
            printf(
                "Failed [seek flag margin]: cs=%d cleared flag: bound %zu (plain %zu), "
                "inplace %lld\n",
                cs, lb, pb, (long long)r);
            ok = 0;
        }
    }
    free(src);
    free(plain);
    free(seek);
    if (ok) printf("  [PASS] margin counts the seek table only when announced\n");
    return ok;
}

/* Blocks shorter than the header's block_size are valid and the bound counts full
 * ones: 1 MiB in 4 KiB blocks under a 64 KiB header, zero head then noise, must
 * still decode at its bound. */
static int inplace_short_blocks(void) {
    const size_t hdr_bs = 64 * 1024, sub = ZXC_BLOCK_SIZE_MIN, n = 1 << 20;
    const size_t cap = (size_t)zxc_compress_bound(n);
    uint8_t* const src = (uint8_t*)malloc(n);
    uint8_t* const ref = (uint8_t*)malloc(cap);
    uint8_t* const arc = (uint8_t*)malloc(cap);
    uint8_t* const out = (uint8_t*)malloc(n);
    const zxc_compress_opts_t bo = {.level = 3, .block_size = sub};
    zxc_cctx* const cc = zxc_create_cctx(&bo);
    int ok = src && ref && arc && out && cc;
    if (ok) {
        gen_random_data(src, n);
        memset(src, 0, 4 * sub);
        // Header, EOF block and footer from a real archive of the same bytes.
        const zxc_compress_opts_t ho = {.level = 3, .block_size = hdr_bs};
        const int64_t rl = zxc_compress(src, n, ref, cap, &ho);
        ok = rl > 0;
        size_t pos = ZXC_FILE_HEADER_SIZE;
        if (ok) memcpy(arc, ref, pos);
        for (size_t off = 0; ok && off < n; off += sub) {
            const int64_t w = zxc_compress_block(cc, src + off, sub, arc + pos, cap - pos, &bo);
            ok = w > 0;
            pos += ok ? (size_t)w : 0;
        }
        if (ok) {
            // The EOF block from the reference, then the footer these blocks imply.
            const size_t eof_at =
                (size_t)rl - test_footer_len(ref, (size_t)rl) - ZXC_BLOCK_HEADER_SIZE;
            memcpy(arc + pos, ref + eof_at, ZXC_BLOCK_HEADER_SIZE);
            pos += ZXC_BLOCK_HEADER_SIZE;
            const int fw = zxc_write_file_footer(arc + pos, cap - pos, pos, n, 0, 0);
            ok = fw > 0;
            pos += ok ? (size_t)fw : 0;
            const int64_t two = zxc_decompress(arc, pos, out, n, NULL);
            const size_t need = zxc_decompress_inplace_bound(arc, pos);
            uint8_t* const buf = (uint8_t*)malloc(need);
            int64_t one = -1;
            if (buf) {
                memcpy(buf + need - pos, arc, pos);
                one = zxc_decompress_inplace(buf, need, pos, NULL);
            }
            ok = two == (int64_t)n && memcmp(out, src, n) == 0 && one == (int64_t)n && buf &&
                 memcmp(buf, src, n) == 0;
            if (!ok)
                printf("Failed [short blocks]: two-buffer %lld, in-place at bound %zu: %lld\n",
                       (long long)two, need, (long long)one);
            free(buf);
        }
    }
    zxc_free_cctx(cc);
    free(out);
    free(arc);
    free(ref);
    free(src);
    if (ok) printf("  [PASS] short blocks under a larger header block size decode in place\n");
    return ok;
}

int test_decompress_inplace(void) {
    printf("=== TEST: Unit - In-place decompression (single buffer) ===\n");
    const size_t N = 2 * 1024 * 1024;
    uint8_t* a = (uint8_t*)malloc(N);
    if (!a) return 0;
    int ok = 1;

    gen_lz_data(a, N);
    ok &= inplace_case("compressible L3", a, N, 3, 1, 0, 0);
    ok &= inplace_case("compressible L6", a, N, 6, 0, 0, 0);

    /* high-entropy: comp_size ~ N so the flush-right archive sits INSIDE the
     * output region -> the write cursor sweeps through the compressed bytes. */
    gen_random_data(a, N);
    ok &= inplace_case("random L1 (overlap)", a, N, 1, 0, 0, 0);
    ok &= inplace_case("random L7 (overlap)", a, N, 7, 1, 0, 0);

    /* Seekable archives carry a seek table between the EOF block and the footer.
     * Those bytes sit to the RIGHT of the read cursor, so they push the
     * flush-right archive left, into the write cursor's path -- the margin has
     * to reserve them. The biting shape is many small blocks (a large table)
     * over incompressible data (no slack): the bound used to land below
     * comp_size there, so the documented flush-right memcpy underflowed. */
    free(a);
    const size_t M = 8 * 1024 * 1024;
    a = (uint8_t*)malloc(M);
    if (!a) return 0;
    gen_lz_data(a, M);
    ok &= inplace_case("seekable text, 4K blocks", a, M, 3, 1, ZXC_BLOCK_SIZE_MIN, 1);
    gen_random_data(a, M);
    ok &= inplace_case("seekable random, 4K blocks", a, M, 1, 1, ZXC_BLOCK_SIZE_MIN, 1);
    ok &= inplace_case("seekable random, 64K blocks", a, M, 1, 0, 64 * 1024, 1);
    ok &= inplace_case("seekable random, default blocks", a, M, 3, 1, 0, 1);

    /* Compressible head, incompressible tail: the separation is tightest where
     * the first incompressible block starts, for both layouts. */
    memset(a, 0, M / 8);
    ok &= inplace_case("mixed, 4K blocks", a, M, 1, 0, ZXC_BLOCK_SIZE_MIN, 0);
    ok &= inplace_case("mixed seekable, 4K blocks", a, M, 1, 1, ZXC_BLOCK_SIZE_MIN, 1);

    /* bound on garbage must be 0. */
    uint8_t junk[64];
    memset(junk, 0x5A, sizeof(junk));
    if (zxc_decompress_inplace_bound(junk, sizeof(junk)) != 0) {
        printf("Failed: bound on garbage != 0\n");
        ok = 0;
    }

    ok &= inplace_forged_footer();
    ok &= inplace_padded_archive();
    ok &= inplace_seek_flag_margin();
    ok &= inplace_short_blocks();

    free(a);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

// No public switch exposes the floor, so the test goes at the decision itself,
// then round-trips both shapes to confirm it stays invisible to the decoder.
#define MINDIST_VOCAB 40

// A small vocabulary in pseudo-random order: matches the fast tiers can index,
// at the 8-9 % short-distance hit rate of ordinary prose.
static void fill_text_like(uint8_t* b, size_t n) {
    uint32_t st = 12345U;
    uint8_t vocab[MINDIST_VOCAB][9];
    size_t vlen[MINDIST_VOCAB];
    for (size_t v = 0; v < MINDIST_VOCAB; v++) {
        st = st * 1103515245U + 12345U;
        vlen[v] = 3U + ((st >> 16) % 6U);
        for (size_t k = 0; k < vlen[v]; k++) {
            st = st * 1103515245U + 12345U;
            vocab[v][k] = (uint8_t)('a' + ((st >> 16) % 26U));
        }
    }
    for (size_t i = 0; i < n;) {
        st = st * 1103515245U + 12345U;
        const size_t v = (size_t)(st >> 8) % MINDIST_VOCAB;
        for (size_t k = 0; k < vlen[v] && i < n; k++) b[i++] = vocab[v][k];
        if (i < n) b[i++] = ' ';
    }
}

int test_min_dist_policy(void) {
    printf("=== TEST: Unit - short match distance floor ===\n");

    const size_t n = 256 * 1024;
    uint8_t* text = malloc(n);
    uint8_t* per = malloc(n);
    int ok = 0;
    if (!text || !per) goto done;

    fill_text_like(text, n);
    // Every position repeats 20 bytes back: the shape the floor would ruin.
    for (size_t i = 0; i < n; i++) per[i] = (uint8_t)((i % 20) + (i / 4096));

    // Sizes straddling the probe's clamp boundaries, where the planned sample
    // count switches between its floor, the proportional band, and its cap.
    // These pin the verdict across all three regimes; they are far enough from
    // the threshold that they would not catch an off-by-one in that count.
#if ZXC_LZ_MINDIST > 1
    static const size_t sizes[] = {4096, 16384, 40960, 65536, 256 * 1024};
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        const unsigned long sz = (unsigned long)sizes[k];
        if (zxc_block_is_short_dist_bound(text, sizes[k])) {
            printf("Failed: probe vetoed text-like data at %lu bytes\n", sz);
            goto done;
        }
        if (!zxc_block_is_short_dist_bound(per, sizes[k])) {
            printf("Failed: probe did not veto periodic data at %lu bytes\n", sz);
            goto done;
        }
    }
    // A block too small to sample is also too small to gain: keep every distance.
    if (!zxc_block_is_short_dist_bound(text, ZXC_LZ_MINDIST)) {
        printf("Failed: probe judged a block too small to sample\n");
        goto done;
    }
#else
    // Floor off (ZXC_LZ_MINDIST == 1): nothing is "short", so the probe has no
    // verdict to give; only the round trips below apply.
    printf("(distance floor off: probe checks skipped)\n");
#endif
    // Whichever way it goes, the archive stays ordinary at every level.
    for (int lvl = ZXC_LEVEL_FASTEST; lvl <= ZXC_LEVEL_ULTRA; lvl++) {
        if (!test_round_trip("mindist text", text, n, lvl, 0)) goto done;
        if (!test_round_trip("mindist periodic", per, n, lvl, 0)) goto done;
    }

    printf("PASS\n\n");
    ok = 1;
done:
    free(text);
    free(per);
    return ok;
}

// Round trips through zxc_glo_split_block, where a rewrite regression shows:
// escaped matches (20-38 bytes) mixed irregularly with inline ones (6-19 bytes),
// which levels 3-5 split within their caps.
int test_glo_match_split(void) {
    printf("=== TEST: Unit - GLO match splitting round trip ===\n");
    const size_t cap = 512 * 1024;
    uint8_t* buf = malloc(cap);
    int ok = 0;
    if (!buf) goto done;

    uint32_t st = 0x9E3779B9U;
    uint8_t src[96];  // shared match source; back-references clear the distance floor
    for (size_t k = 0; k < sizeof(src); k++) {
        st = st * 1103515245U + 12345U;
        src[k] = (uint8_t)(st >> 16);
    }
    size_t n = 0;
    for (size_t k = 0; k < sizeof(src) && n < cap; k++) buf[n++] = src[k];
    while (n + sizeof(src) + 64 < cap) {
        st = st * 1103515245U + 12345U;
        const size_t lit = (st >> 16) % 8U;  // 0-7 literals, LL mostly inline
        for (size_t k = 0; k < lit && n < cap; k++) {
            st = st * 1103515245U + 12345U;
            buf[n++] = (uint8_t)(st >> 16);
        }
        st = st * 1103515245U + 12345U;
        // ~1 in 4 matches escapes the ML field (20-38 bytes); the rest stay inline.
        const size_t m =
            ((st >> 16) % 4U == 0U) ? 20U + ((st >> 18) % 19U) : 6U + ((st >> 18) % 14U);
        for (size_t k = 0; k < m && n < cap; k++) buf[n++] = src[k % sizeof(src)];
    }

    // Levels 3-5 run the split; 1-2 (GHI) and 6-7 must round-trip cleanly too.
    for (int lvl = ZXC_LEVEL_FASTEST; lvl <= ZXC_LEVEL_ULTRA; lvl++)
        if (!test_round_trip("glo split", buf, n, lvl, 0)) goto done;
    // Text-like data reaches the split by a different route (irregular escapes).
    fill_text_like(buf, cap);
    for (int lvl = ZXC_LEVEL_DEFAULT; lvl <= ZXC_LEVEL_DENSITY; lvl++)
        if (!test_round_trip("glo split text", buf, cap, lvl, 0)) goto done;

    printf("PASS\n\n");
    ok = 1;
done:
    free(buf);
    return ok;
}

/* zxc_get_frame_info and its FILE* twin: every field against what the archive
 * was built with, the same answer from both, and nothing written on failure. */
/* Every byte of @p x is a field or zero: no stack bytes leak through the padding. */
static int frame_info_padding_zero(const zxc_frame_info_t* x) {
    zxc_frame_info_t e;
    memset(&e, 0, sizeof(e));
    e.decompressed_size = x->decompressed_size;
    e.compressed_size = x->compressed_size;
    e.digest = x->digest;
    e.block_size = x->block_size;
    e.dict_id = x->dict_id;
    e.format_version = x->format_version;
    e.has_checksum = x->has_checksum;
    e.has_seek_table = x->has_seek_table;
    return memcmp(&e, x, sizeof(e)) == 0;
}

static int frame_info_same(const zxc_frame_info_t* a, const zxc_frame_info_t* b) {
    return a->decompressed_size == b->decompressed_size &&
           a->compressed_size == b->compressed_size && a->digest == b->digest &&
           a->block_size == b->block_size && a->dict_id == b->dict_id &&
           a->format_version == b->format_version && a->has_checksum == b->has_checksum &&
           a->has_seek_table == b->has_seek_table;
}

int test_frame_info(void) {
    printf("=== TEST: Frame info (buffer and FILE*) ===\n");
    enum { N = 100000 };
    uint8_t* const src = malloc(N);
    const size_t cap = (size_t)zxc_compress_bound(N) + 1;
    uint8_t* const arc = malloc(cap);
    int ok = src && arc && zxc_frame_info_size() == sizeof(zxc_frame_info_t);
    if (ok) gen_lz_data(src, N);

    static uint8_t dict[4096];
    memset(dict, 'z', sizeof(dict));
    for (int v = 0; ok && v < 3; v++) {
        zxc_compress_opts_t co = {.level = 3, .block_size = 8192};
        if (v >= 1) co.checksum_enabled = co.seekable = 1;
        if (v == 2) {
            co.dict = dict;
            co.dict_size = sizeof(dict);
        }
        const int64_t n = zxc_compress(src, N, arc, cap, &co);
        zxc_frame_info_t a, b;
        memset(&a, 0xFF, sizeof(a));
        memset(&b, 0xFF, sizeof(b));
        const int ra = n > 0 ? zxc_get_frame_info(arc, (size_t)n, &a, sizeof(a)) : -1;
        FILE* const f = tmpfile();
        int rb = -1;
        long pos = -1;
        if (f && n > 0 && fwrite(arc, 1, (size_t)n, f) == (size_t)n && fseek(f, 5, SEEK_SET) == 0) {
            rb = zxc_stream_get_frame_info(f, &b, sizeof(b));
            pos = ftell(f);
        }
        if (f) fclose(f);
        const uint64_t digest =
            co.checksum_enabled && n > 0 ? zxc_le64(arc + n - test_footer_len(arc, (size_t)n)) : 0;
        const uint32_t did = v == 2 ? zxc_get_dict_id(arc, (size_t)n) : 0;
        if (ra != ZXC_OK || rb != ZXC_OK || pos != 5 || !frame_info_same(&a, &b) ||
            !frame_info_padding_zero(&a) || !frame_info_padding_zero(&b) ||
            a.decompressed_size != N || a.compressed_size != (uint64_t)n || a.digest != digest ||
            a.block_size != 8192 || a.dict_id != did || (v == 2 && did == 0) ||
            a.format_version != ZXC_FILE_FORMAT_VERSION || a.has_checksum != (v >= 1) ||
            a.has_seek_table != (v >= 1)) {
            printf("  [FAIL] variant %d: buffer %d, FILE* %d, position %ld\n", v, ra, rb, pos);
            ok = 0;
        }
    }

    /* Failures: the code, and the struct left as it was. */
    const zxc_compress_opts_t co = {.level = 3};
    const int64_t n = ok ? zxc_compress(src, N, arc, cap, &co) : -1;
    zxc_frame_info_t keep;
    memset(&keep, 0xA5, sizeof(keep));
    const zxc_frame_info_t before = keep;
    if (ok && n > 0) {
        arc[n] = 0; /* one byte past the frame: the footer no longer spans the input */
        const struct {
            const char* what;
            const void* p;
            size_t len;
            int want;
        } bad[] = {
            {"NULL source", NULL, (size_t)n, ZXC_ERROR_NULL_INPUT},
            {"header only", arc, ZXC_FILE_HEADER_SIZE, ZXC_ERROR_SRC_TOO_SMALL},
            {"trailing byte", arc, (size_t)n + 1, ZXC_ERROR_CORRUPT_DATA},
            {"cut footer", arc, (size_t)n - 1, ZXC_ERROR_CORRUPT_DATA},
        };
        for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
            const int r = zxc_get_frame_info(bad[k].p, bad[k].len, &keep, sizeof(keep));
            if (r != bad[k].want || memcmp(&keep, &before, sizeof(keep)) != 0) {
                printf("  [FAIL] %s: %d, want %d (struct %s)\n", bad[k].what, r, bad[k].want,
                       memcmp(&keep, &before, sizeof(keep)) ? "written" : "intact");
                ok = 0;
            }
        }
        arc[0] ^= 0xFF;
        if (zxc_get_frame_info(arc, (size_t)n, &keep, sizeof(keep)) != ZXC_ERROR_BAD_MAGIC ||
            zxc_get_frame_info(arc, (size_t)n, NULL, sizeof(keep)) != ZXC_ERROR_NULL_INPUT ||
            zxc_stream_get_frame_info(NULL, &keep, sizeof(keep)) != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] bad magic or NULL output\n");
            ok = 0;
        }
    }
    free(src);
    free(arc);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Every decoder of one archive: buffer, in place, FILE* and push. */
static int all_decoders_say(const char* what, const uint8_t* arc, const size_t n,
                            const int64_t want) {
    enum { CAP = 1 << 16 };
    static uint8_t out[CAP];
    const int64_t rb = zxc_decompress(arc, n, out, CAP, NULL);

    // In place needs a block of room ahead of the archive: the largest block.
    const size_t room = ZXC_BLOCK_SIZE_MAX + ZXC_DECOMPRESS_TAIL_PAD;
    uint8_t* const buf = (uint8_t*)malloc(room + n);
    int64_t ri = ZXC_ERROR_MEMORY;
    if (buf) {
        memcpy(buf + room, arc, n);
        ri = zxc_decompress_inplace(buf, room + n, n, NULL);
        free(buf);
    }

    int64_t rf = ZXC_ERROR_IO;
    FILE* const f = tmpfile();
    if (f && fwrite(arc, 1, n, f) == n && fseek(f, 0, SEEK_SET) == 0)
        rf = zxc_stream_decompress(f, NULL, NULL);
    if (f) fclose(f);

    int64_t rp = ZXC_ERROR_MEMORY;
    zxc_dstream* const ds = zxc_dstream_create(NULL);
    if (ds) {
        zxc_inbuf_t in = {arc, n, 0};
        zxc_outbuf_t ob = {out, CAP, 0};
        rp = zxc_dstream_decompress(ds, &ob, &in);
        // All input given: a decoder still waiting has not answered.
        if (rp >= 0 && !zxc_dstream_finished(ds)) rp = ZXC_ERROR_SRC_TOO_SMALL;
        zxc_dstream_free(ds);
    }
    if (rb == want && ri == want && rf == want && rp == want) return 1;
    printf("  [FAIL] %s: buffer %lld, in place %lld, FILE* %lld, push %lld (want %lld)\n", what,
           (long long)rb, (long long)ri, (long long)rf, (long long)rp, (long long)want);
    return 0;
}

/* The footer has one valid encoding, and a forged one reads as corrupt data
 * everywhere: never as truncation, an I/O error or a stream left waiting. */
int test_footer_strictness(void) {
    printf("=== TEST: Footer - one encoding, forgeries are corrupt data ===\n");
    int ok = 1;
    uint8_t src[300];
    uint8_t arc[1024], bad[1024];
    gen_random_data(src, sizeof(src));

    /* 1. A compressed size on one byte more than it needs. A RAW frame of 220
     *    bytes is 255 bytes, the most one byte holds: re-encoded on two, 256
     *    fits exactly, so only the minimal-length rule refuses it. */
    for (int seek = 0; seek <= 1 && ok; seek++) {
        const zxc_compress_opts_t co = {.level = 1, .seekable = seek};
        const size_t len = seek ? 200 : 220; /* the seek table adds 20 bytes */
        const int64_t n = zxc_compress(src, len, arc, sizeof(arc), &co);
        const size_t body = n > 0 ? (size_t)n - test_footer_len(arc, (size_t)n) : 0;
        if (n != 255 || arc[n - 1] != 0x00) {
            printf("  [FAIL] seek %d: expected a 255-byte frame, got %lld\n", seek, (long long)n);
            ok = 0;
            break;
        }
        memcpy(bad, arc, body);
        bad[body] = (uint8_t)len;            /* original size, 1 byte */
        zxc_store_le16(bad + body + 1, 256); /* compressed size, 2 bytes */
        bad[body + 3] = 0x10;                /* L: nd = 1, nf = 2 */
        const size_t bn = body + 4;
        zxc_frame_info_t fi;
        zxc_seekable* const s = zxc_seekable_open(bad, bn);
        if (zxc_get_frame_info(bad, bn, &fi, sizeof(fi)) != ZXC_ERROR_CORRUPT_DATA ||
            zxc_get_decompressed_size(bad, bn) != 0 || zxc_decompress_inplace_bound(bad, bn) != 0 ||
            s != NULL) {
            printf("  [FAIL] seek %d: a non-minimal footer was accepted\n", seek);
            ok = 0;
        }
        zxc_seekable_free(s);
        ok = ok && all_decoders_say("non-minimal footer", bad, bn, ZXC_ERROR_CORRUPT_DATA);
    }

    /* 2. A well-formed footer storing a size shorter to encode than the real
     *    one: the decoders expect a longer footer than the input holds. */
    const zxc_compress_opts_t plain = {.level = 3};
    const int64_t n = zxc_compress(src, sizeof(src), arc, sizeof(arc), &plain);
    const uint64_t lies[] = {0, 255};
    for (size_t k = 0; ok && n > 0 && k < 2; k++) {
        const size_t bn = test_forge_footer_size(arc, (size_t)n, bad, sizeof(bad), lies[k]);
        ok = all_decoders_say(lies[k] ? "size 255 for 300" : "size 0 for 300", bad, bn,
                              ZXC_ERROR_CORRUPT_DATA);
    }

    /* 3. HAS_SEEK_TABLE set with no table: corrupt data, with or without the
     *    digest that makes the footer longer than a SEK header. 254 bytes start
     *    the footer with 0xFE, the SEK block type. */
    static const char* const lie[] = {"flag without table", "flag without table, checksums",
                                      "flag without table, footer 0xFE"};
    uint8_t runs[254];
    memset(runs, 'a', sizeof(runs));
    for (int k = 0; k < 3 && ok; k++) {
        const zxc_compress_opts_t co = {.level = 3, .checksum_enabled = k == 1};
        const int64_t m = k < 2 ? zxc_compress(src, sizeof(src), bad, sizeof(bad), &co)
                                : zxc_compress(runs, sizeof(runs), bad, sizeof(bad), &co);
        if (m <= 0 || (k == 2 && bad[m - 3] != ZXC_BLOCK_SEK)) {
            printf("  [FAIL] %s: unexpected frame\n", lie[k]);
            ok = 0;
            break;
        }
        bad[6] |= ZXC_FILE_FLAG_HAS_SEEK_TABLE;
        zxc_file_header_sign(bad);
        ok = all_decoders_say(lie[k], bad, (size_t)m, ZXC_ERROR_CORRUPT_DATA);
    }

    /* 3b. EOF or SEK header with Block Flags or Reserved set, hash recomputed:
     *     each has one valid form, every reader refuses the others. */
    static const char* const odd[] = {"EOF flags", "EOF reserved", "SEK flags", "SEK reserved"};
    for (int k = 0; k < 4 && ok; k++) {
        const zxc_compress_opts_t co = {.level = 3, .seekable = 1};
        const int64_t m = zxc_compress(src, sizeof(src), bad, sizeof(bad), &co);
        const size_t sek = m > 0 ? (size_t)m - test_footer_len(bad, (size_t)m) - 20 : 0;
        const size_t hdr = k < 2 ? sek - ZXC_BLOCK_HEADER_SIZE : sek;
        if (m <= 0 || bad[sek] != ZXC_BLOCK_SEK ||
            bad[hdr] != (k < 2 ? ZXC_BLOCK_EOF : ZXC_BLOCK_SEK)) {
            printf("  [FAIL] no EOF and SEK headers where expected\n");
            ok = 0;
            break;
        }
        bad[hdr + 1 + (k & 1)] = 1;
        bad[hdr + 7] = 0;
        bad[hdr + 7] = zxc_hash8(bad + hdr);
        zxc_seekable* const s = zxc_seekable_open(bad, (size_t)m);
        if (s) {
            printf("  [FAIL] %s set: the seekable reader opened it\n", odd[k]);
            ok = 0;
        }
        zxc_seekable_free(s);
        ok = ok && all_decoders_say(odd[k], bad, (size_t)m,
                                    k < 2 ? ZXC_ERROR_BAD_HEADER : ZXC_ERROR_CORRUPT_DATA);
    }

    /* 4. Cut shorter than the smallest frame: decode and probe agree. */
    if (ok && n > 0) {
        static uint8_t out[1024];
        zxc_dctx* const dctx = zxc_create_dctx();
        const int64_t r[] = {zxc_decompress(arc, 20, out, sizeof(out), NULL),
                             zxc_decompress(arc, 20, NULL, 0, NULL),
                             zxc_decompress_dctx(dctx, arc, 20, out, sizeof(out), NULL),
                             zxc_decompress_dctx(dctx, arc, 20, NULL, 0, NULL)};
        zxc_free_dctx(dctx);
        for (size_t k = 0; k < 4; k++)
            if (r[k] != ZXC_ERROR_SRC_TOO_SMALL) {
                printf("  [FAIL] 20 bytes, call %zu: %lld\n", k, (long long)r[k]);
                ok = 0;
            }
    }

    /* 5. Short junk: the header speaks first, from a buffer or a file, and to the
     *    decoders as to the frame-info readers. */
    for (size_t len = 16; ok && len <= 26; len += 5) {
        const uint8_t junk[26] = {0};
        zxc_frame_info_t fi;
        FILE* const f = tmpfile();
        int rf = ZXC_ERROR_IO;
        if (f && fwrite(junk, 1, len, f) == len && fseek(f, 0, SEEK_SET) == 0)
            rf = zxc_stream_get_frame_info(f, &fi, sizeof(fi));
        if (f) fclose(f);
        uint8_t out[64];
        zxc_dctx* const dctx = zxc_create_dctx();
        const int64_t rd = zxc_decompress(junk, len, out, sizeof(out), NULL);
        const int64_t rx = dctx ? zxc_decompress_dctx(dctx, junk, len, out, sizeof(out), NULL) : 0;
        zxc_free_dctx(dctx);
        if (zxc_get_frame_info(junk, len, &fi, sizeof(fi)) != ZXC_ERROR_BAD_MAGIC ||
            rf != ZXC_ERROR_BAD_MAGIC || rd != ZXC_ERROR_BAD_MAGIC || rx != ZXC_ERROR_BAD_MAGIC) {
            printf("  [FAIL] %zu junk bytes: FILE* %d, decode %lld / %lld, want BAD_MAGIC\n", len,
                   rf, (long long)rd, (long long)rx);
            ok = 0;
        }
    }

    /* 6. A footer longer than its own frame is forged, not cut: 3 bytes claiming a
     *    27-byte frame with checksums leave no room for the digest. */
    if (ok) {
        const zxc_compress_opts_t cs = {.level = 1, .checksum_enabled = 1};
        uint8_t f27[64];
        zxc_frame_info_t fi;
        const int64_t m = zxc_compress("", 0, f27, sizeof(f27), &cs);
        const size_t body = ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE;
        f27[body] = 0;      /* original size 0, 1 byte */
        f27[body + 1] = 27; /* compressed size 27, 1 byte */
        f27[body + 2] = 0;  /* L: nd = nf = 1 */
        const int r = m > 0 ? zxc_get_frame_info(f27, body + 3, &fi, sizeof(fi)) : -1;
        if (r != ZXC_ERROR_CORRUPT_DATA) {
            printf("  [FAIL] footer longer than its frame: %d, want CORRUPT_DATA\n", r);
            ok = 0;
        }
    }
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}
