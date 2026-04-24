/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

FILE* create_restricted_file(const char* path) {
#ifdef _MSC_VER
    int fd = -1;
    _sopen_s(&fd, path, _O_CREAT | _O_WRONLY | _O_TRUNC, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    return fd >= 0 ? _fdopen(fd, "w") : NULL;
#else
    const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    return fd >= 0 ? fdopen(fd, "w") : NULL;
#endif
}

// Generates a buffer of random data (To force RAW)
void gen_random_data(uint8_t* const buf, const size_t size) {
    for (size_t i = 0; i < size; i++) buf[i] = rand() & 0xFF;
}

// Generates repetitive data (To force GLO/GHI/LZ)
void gen_lz_data(uint8_t* const buf, const size_t size) {
    const char* const pattern =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
        "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
        "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
        "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
        "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
        "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
        "mollit anim id est laborum.";
    const size_t pat_len = strlen(pattern);
    for (size_t i = 0; i < size; i++) buf[i] = pattern[i % pat_len];
}

// Generates a regular numeric sequence (To force NUM)
void gen_num_data(uint8_t* const buf, const size_t size) {
    // Fill with 32-bit integers
    uint32_t* const ptr = (uint32_t*)buf;
    const size_t count = size / 4;
    uint32_t val = 0;
    for (size_t i = 0; i < count; i++) {
        // Arithmetic sequence: 0, 100, 200...
        // Deltas are constant (100), perfect for NUM
        ptr[i] = val;
        val += 100;
    }
}

// Generates numeric sequence with 0 deltas (all identical)
void gen_num_data_zero(uint8_t* const buf, const size_t size) {
    uint32_t* const ptr = (uint32_t*)buf;
    const size_t count = size / 4;
    for (size_t i = 0; i < count; i++) {
        ptr[i] = 42;
    }
}

// Generates numeric data with alternating small deltas (+1, -1)
void gen_num_data_small(uint8_t* const buf, const size_t size) {
    uint32_t* const ptr = (uint32_t*)buf;
    const size_t count = size / 4;
    uint32_t val = 1000;
    for (size_t i = 0; i < count; i++) {
        ptr[i] = val;
        val += (i % 2 == 0) ? 1 : -1;
    }
}

// Generates numeric data with very large deltas to maximize bit width
void gen_num_data_large(uint8_t* const buf, const size_t size) {
    uint32_t* const ptr = (uint32_t*)buf;
    const size_t count = size / 4;
    for (size_t i = 0; i < count; i++) {
        // Alternate between 0 and 0xFFFFFFFF (delta is huge)
        ptr[i] = (i % 2 == 0) ? 0 : 0xFFFFFFFF;
    }
}

void gen_binary_data(uint8_t* const buf, const size_t size) {
    // Pattern with problematic bytes that could be corrupted in text mode:
    // 0x0A (LF), 0x0D (CR), 0x00 (NULL), 0x1A (EOF/CTRL-Z), 0xFF
    const uint8_t pattern[] = {
        0x5A, 0x58, 0x43, 0x00,  // "ZXC" + NULL
        0x0A, 0x0D, 0x0A, 0x00,  // LF, CR, LF, NULL
        0xFF, 0xFE, 0x0A, 0x0D,  // High bytes + LF/CR
        0x1A, 0x00, 0x0A, 0x0D,  // EOF marker + NULL + LF/CR
        0x00, 0x00, 0x0A, 0x0A,  // Multiple NULLs and LFs
    };
    const size_t pat_len = sizeof(pattern);
    for (size_t i = 0; i < size; i++) {
        buf[i] = pattern[i % pat_len];
    }
}

// Generates data with small offsets (<=255 bytes) to force 1-byte offset encoding
// This creates short repeating patterns with matches very close to each other
void gen_small_offset_data(uint8_t* const buf, const size_t size) {
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
void gen_large_offset_data(uint8_t* const buf, const size_t size) {
    // First 300 bytes: unique random data (no matches possible)
    for (size_t i = 0; i < 300 && i < size; i++) {
        buf[i] = (uint8_t)((i * 7 + 13) % 256);
    }
    // Then: repeat patterns from the beginning (offset > 255)
    for (size_t i = 300; i < size; i++) {
        buf[i] = buf[i - 300];  // Offset of 300 bytes (requires 2-byte encoding)
    }
}

void fill_seek_data(uint8_t* buf, size_t size, uint8_t seed) {
    for (size_t i = 0; i < size; i++) {
        buf[i] = (uint8_t)(seed + (i * 17) + (i >> 8));
    }
}

// Generic Round-Trip test function (Compress -> Decompress -> Compare)
int test_round_trip(const char* test_name, const uint8_t* input, size_t size, int level,
                    int checksum_enabled) {
    printf("=== TEST: %s (Sz: %zu, Lvl: %d, CRC: %s) ===\n", test_name, size, level,
           checksum_enabled ? "Enabled" : "Disabled");

    FILE* const f_in = tmpfile();
    FILE* const f_comp = tmpfile();
    FILE* const f_decomp = tmpfile();

    if (!f_in || !f_comp || !f_decomp) {
        perror("tmpfile");
        if (f_in) fclose(f_in);
        if (f_comp) fclose(f_comp);
        if (f_decomp) fclose(f_decomp);
        return 0;
    }

    fwrite(input, 1, size, f_in);
    fseek(f_in, 0, SEEK_SET);

    zxc_compress_opts_t _sco1 = {
        .n_threads = 1, .level = level, .checksum_enabled = checksum_enabled};
    if (zxc_stream_compress(f_in, f_comp, &_sco1) < 0) {
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

    zxc_decompress_opts_t _sdo2 = {.n_threads = 1, .checksum_enabled = checksum_enabled};
    if (zxc_stream_decompress(f_comp, f_decomp, &_sdo2) < 0) {
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
