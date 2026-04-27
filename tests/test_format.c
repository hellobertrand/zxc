/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"
#include "../src/lib/zxc_huffman.h"

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

/* Round-trip the Huffman codec over a few representative literal distributions. */
static int huf_roundtrip_case(const char* label, const uint8_t* literals, size_t n) {
    uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
    for (size_t i = 0; i < n; i++) freq[literals[i]]++;

    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    if (zxc_huf_build_code_lengths(freq, code_len) != ZXC_OK) {
        printf("Failed [%s]: build_code_lengths\n", label);
        return 0;
    }
    /* Validate the lengths-limit invariant. */
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (code_len[i] > ZXC_HUF_MAX_CODE_LEN) {
            printf("Failed [%s]: code_len[%d] = %d > %d\n", label, i, code_len[i],
                   ZXC_HUF_MAX_CODE_LEN);
            return 0;
        }
    }

    /* Worst-case payload size: 134-byte header + n bytes (RAW upper bound). */
    const size_t cap = ZXC_HUF_HEADER_SIZE + n + 64;
    uint8_t* enc = (uint8_t*)malloc(cap);
    uint8_t* dec = (uint8_t*)malloc(n);
    if (!enc || !dec) {
        free(enc);
        free(dec);
        printf("Failed [%s]: alloc\n", label);
        return 0;
    }

    const int written = zxc_huf_encode_section(literals, n, code_len, enc, cap);
    if (written < 0) {
        free(enc);
        free(dec);
        printf("Failed [%s]: encode_section -> %d\n", label, written);
        return 0;
    }

    const int rc = zxc_huf_decode_section(enc, (size_t)written, dec, n);
    if (rc != ZXC_OK) {
        free(enc);
        free(dec);
        printf("Failed [%s]: decode_section -> %d\n", label, rc);
        return 0;
    }

    if (memcmp(literals, dec, n) != 0) {
        free(enc);
        free(dec);
        printf("Failed [%s]: roundtrip mismatch\n", label);
        return 0;
    }

    free(enc);
    free(dec);
    printf("  [PASS] %s (n=%zu, encoded=%d B, ratio=%.1f%%)\n", label, n, written,
           100.0 * (double)written / (double)n);
    return 1;
}

int test_huffman_codec() {
    printf("=== TEST: Unit - Huffman Codec (build/encode/decode roundtrip) ===\n");

    const size_t N = 8192;
    uint8_t* buf = (uint8_t*)malloc(N);
    if (!buf) return 0;

    /* Case 1: heavily skewed (90% one byte, 10% noise). */
    for (size_t i = 0; i < N; i++) buf[i] = (rand() % 10 == 0) ? (uint8_t)(rand() & 0xFF) : 'A';
    if (!huf_roundtrip_case("Skewed (90% 'A')", buf, N)) {
        free(buf);
        return 0;
    }

    /* Case 2: uniform random — Huffman should be near no-op (~1 byte/sym). */
    for (size_t i = 0; i < N; i++) buf[i] = (uint8_t)(rand() & 0xFF);
    if (!huf_roundtrip_case("Uniform random", buf, N)) {
        free(buf);
        return 0;
    }

    /* Case 3: two-symbol alphabet — best case, ~1 bit/symbol. */
    for (size_t i = 0; i < N; i++) buf[i] = (rand() & 1) ? 'X' : 'Y';
    if (!huf_roundtrip_case("Two-symbol alphabet", buf, N)) {
        free(buf);
        return 0;
    }

    /* Case 4: single-symbol — degenerate but must still roundtrip. */
    for (size_t i = 0; i < N; i++) buf[i] = 'Z';
    if (!huf_roundtrip_case("Single-symbol", buf, N)) {
        free(buf);
        return 0;
    }

    /* Case 5: small block (just above the min-literals threshold). */
    for (size_t i = 0; i < ZXC_HUF_MIN_LITERALS; i++)
        buf[i] = (rand() % 4 == 0) ? (uint8_t)(rand() & 0xFF) : 'k';
    if (!huf_roundtrip_case("Small block at threshold", buf, ZXC_HUF_MIN_LITERALS)) {
        free(buf);
        return 0;
    }

    free(buf);
    printf("PASS\n\n");
    return 1;
}

// Checks that the EOF block is correctly appended
int test_eof_block_structure() {
    printf("=== TEST: Unit - EOF Block Structure ===\n");

    const char* input = "test";
    size_t src_size = 4;
    size_t max_dst_size = (size_t)zxc_compress_bound(src_size);
    uint8_t* compressed = malloc(max_dst_size);
    if (!compressed) return 0;

    zxc_compress_opts_t _co26 = {.level = 1, .checksum_enabled = 0};
    int64_t comp_size = zxc_compress(input, src_size, compressed, max_dst_size, &_co26);
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
    // ZXC_BLOCK_SIZE_DEFAULT is 256KB. We need > 256KB. Let's use 600KB.
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
    zxc_compress_opts_t _sco27 = {.n_threads = 1, .level = 1, .checksum_enabled = 1};
    zxc_stream_compress(f_in, f_comp, &_sco27);

    // 3. Read compressed data to memory
    long comp_sz = ftell(f_comp);
    rewind(f_comp);
    uint8_t* comp_buf = malloc((size_t)comp_sz);
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
    uint8_t* swapped_buf = malloc((size_t)comp_sz);

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
    fwrite(swapped_buf, 1, (size_t)comp_sz, f_bad);
    rewind(f_bad);

    // 7. Attempt Decompression
    FILE* f_out = tmpfile();
    zxc_decompress_opts_t _sdo28 = {.n_threads = 1, .checksum_enabled = 1};
    int64_t res = zxc_stream_decompress(f_bad, f_out, &_sdo28);

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

int test_legacy_header() {
    printf("=== TEST: Legacy header (chunk_size_code=64) ===\n");

    // Build a valid file header with legacy chunk_size_code = 64 (= 256 KB)
    uint8_t hdr[ZXC_FILE_HEADER_SIZE];
    memset(hdr, 0, sizeof(hdr));

    // Magic word (LE)
    hdr[0] = 0xF5;
    hdr[1] = 0x2E;
    hdr[2] = 0xB0;
    hdr[3] = 0x9C;
    // Version
    hdr[4] = ZXC_FILE_FORMAT_VERSION;
    // Legacy chunk size code
    hdr[5] = 64;
    // Flags: no checksum
    hdr[6] = 0;

    // Compute CRC16 (bytes 14-15 zeroed, then hash)
    hdr[14] = 0;
    hdr[15] = 0;
    uint16_t crc = zxc_hash16(hdr);
    hdr[14] = (uint8_t)(crc & 0xFF);
    hdr[15] = (uint8_t)(crc >> 8);

    size_t block_size = 0;
    int has_checksum = -1;
    int rc = zxc_read_file_header(hdr, sizeof(hdr), &block_size, &has_checksum);

    if (rc != ZXC_OK) {
        printf("  [FAIL] zxc_read_file_header returned %d (%s)\n", rc, zxc_error_name(rc));
        return 0;
    }
    if (block_size != 256 * 1024) {
        printf("  [FAIL] block_size = %zu, expected %d\n", block_size, 256 * 1024);
        return 0;
    }
    if (has_checksum != 0) {
        printf("  [FAIL] has_checksum = %d, expected 0\n", has_checksum);
        return 0;
    }
    printf("  [PASS] Legacy code 64 -> block_size = 256 KB\n");

    // Verify that invalid codes are rejected
    hdr[5] = 99;  // Not a valid exponent nor legacy value
    hdr[14] = 0;
    hdr[15] = 0;
    crc = zxc_hash16(hdr);
    hdr[14] = (uint8_t)(crc & 0xFF);
    hdr[15] = (uint8_t)(crc >> 8);

    rc = zxc_read_file_header(hdr, sizeof(hdr), &block_size, &has_checksum);
    if (rc != ZXC_ERROR_BAD_BLOCK_SIZE) {
        printf("  [FAIL] invalid code 99: expected %d, got %d\n", ZXC_ERROR_BAD_BLOCK_SIZE, rc);
        return 0;
    }
    printf("  [PASS] Invalid code 99 -> ZXC_ERROR_BAD_BLOCK_SIZE\n");

    printf("PASS\n\n");
    return 1;
}
