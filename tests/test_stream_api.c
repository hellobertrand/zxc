/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

#if !defined(_WIN32)
#include <signal.h>
#include <unistd.h>
#endif

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

#if !defined(_WIN32)
// A writable FILE* whose reader is gone: every write to it fails (EPIPE).
static FILE* open_closed_pipe(void) {
    int fds[2];
    if (pipe(fds) != 0) return NULL;
    close(fds[0]);
    FILE* f = fdopen(fds[1], "wb");
    if (!f) close(fds[1]);
    return f;
}
#endif

// Checks that a write error deferred by stdio buffering still fails the call.
// A small output never leaves the FILE buffer before the library returns, so
// every fwrite succeeds and only the final flush reaches the closed pipe.
int test_io_deferred_write_failure(void) {
    printf("=== TEST: Unit - Deferred Write Failure (closed pipe) ===\n");
#if !defined(_WIN32)
    uint8_t src[1000];
    gen_lz_data(src, sizeof(src));

    // Report EPIPE instead of killing the process with SIGPIPE.
    void (*prev_sigpipe)(int) = signal(SIGPIPE, SIG_IGN);

    FILE* f_in = tmpfile();
    FILE* f_arc = tmpfile();
    FILE* f_pipe_c = open_closed_pipe();
    FILE* f_pipe_d = open_closed_pipe();
    if (!f_in || !f_arc || !f_pipe_c || !f_pipe_d) {
        printf("  [SKIP] pipe or tmpfile unavailable\n\n");
        if (f_in) fclose(f_in);
        if (f_arc) fclose(f_arc);
        if (f_pipe_c) fclose(f_pipe_c);
        if (f_pipe_d) fclose(f_pipe_d);
        signal(SIGPIPE, prev_sigpipe);
        return 1;
    }
    fwrite(src, 1, sizeof(src), f_in);

    zxc_compress_opts_t copts = {.n_threads = 1};
    zxc_decompress_opts_t dopts = {.n_threads = 1};

    rewind(f_in);
    const int64_t c_pipe = zxc_stream_compress(f_in, f_pipe_c, &copts);
    rewind(f_in);
    const int64_t c_ok = zxc_stream_compress(f_in, f_arc, &copts);
    rewind(f_arc);
    const int64_t d_pipe = zxc_stream_decompress(f_arc, f_pipe_d, &dopts);

    fclose(f_in);
    fclose(f_arc);
    fclose(f_pipe_c);
    fclose(f_pipe_d);
    signal(SIGPIPE, prev_sigpipe);

    if (c_pipe != ZXC_ERROR_IO || c_ok <= 0 || d_pipe != ZXC_ERROR_IO) {
        printf(
            "Failed: compress->pipe %lld, compress->tmpfile %lld, "
            "decompress->pipe %lld (expected %d, >0, %d)\n",
            (long long)c_pipe, (long long)c_ok, (long long)d_pipe, ZXC_ERROR_IO, ZXC_ERROR_IO);
        return 0;
    }
    printf("PASS\n\n");
#else
    printf("  [SKIP] needs POSIX pipes\n\n");
#endif
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

    // 3b. A header only the magic word of which is intact must not yield a
    //     size: the flag byte that places the footer is unverified, and the 8
    //     bytes it points at may be anything. Same verdict as the decoders.
    //     A forged footer size is capped like the buffer API caps it.
    {
        const size_t src_sz = 4096;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);
        const size_t cap = (size_t)zxc_compress_bound(src_sz);
        uint8_t* comp = malloc(cap);
        zxc_compress_opts_t co = {.level = 1, .checksum_enabled = 1};
        const int64_t comp_sz = zxc_compress(src, src_sz, comp, cap, &co);
        if (comp_sz <= 0) {
            printf("  [SKIP] compress failed\n");
            free(src);
            free(comp);
            return 0;
        }
        struct {
            const char* what;
            size_t at;
            int expect;
        } forge[] = {
            {"header checksum", 14, ZXC_ERROR_BAD_HEADER},
            {"footer size", (size_t)comp_sz - ZXC_FILE_FOOTER_SIZE - ZXC_FILE_DIGEST_SIZE + 7,
             ZXC_ERROR_CORRUPT_DATA},
        };
        for (size_t k = 0; k < sizeof(forge) / sizeof(forge[0]); k++) {
            FILE* f = tmpfile();
            if (!f) {
                printf("  [SKIP] tmpfile failed\n");
                free(src);
                free(comp);
                return 0;
            }
            comp[forge[k].at] ^= 0x7F; /* the size stays positive as an int64 */
            fwrite(comp, 1, (size_t)comp_sz, f);
            comp[forge[k].at] ^= 0x7F;
            fseek(f, 0, SEEK_SET);
            r = zxc_stream_get_decompressed_size(f);
            fclose(f);
            if (r != forge[k].expect) {
                printf("  [FAIL] forged %s: expected %s, got %lld\n", forge[k].what,
                       zxc_error_name(forge[k].expect), (long long)r);
                free(src);
                free(comp);
                return 0;
            }
        }
        free(src);
        free(comp);
    }
    printf("  [PASS] forged header -> BAD_HEADER, forged footer size -> CORRUPT_DATA\n");

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
#define RT_WRAPPER(fn_name, label, gen, size_expr, level_val, checksum_val)               \
    int fn_name(void) {                                                                   \
        const size_t _sz = (size_expr);                                                   \
        uint8_t* _buf = malloc(_sz > 0 ? _sz : 1);                                        \
        if (!_buf) return 0;                                                              \
        gen(_buf, _sz);                                                                   \
        const int _ok = test_round_trip((label), _buf, _sz, (level_val), (checksum_val)); \
        free(_buf);                                                                       \
        return _ok;                                                                       \
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
RT_WRAPPER(test_roundtrip_1byte_checksum, "1-byte Input (with checksum)", gen_random_data, 1, 3, 1)
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
RT_WRAPPER(test_roundtrip_binary_checksum, "Binary Data with Checksum", gen_binary_data, RT_BUF, 3,
           1)
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

/*
 * ZXC_DICT_SIZE_MAX is a contract, not a hint: an oversized dictionary must be
 * refused by every entry point with the same code. The stream engine used to
 * check it on the compression side only, so zxc_stream_decompress either
 * reported DICT_MISMATCH (the dict_id check firing first, hiding the real
 * fault) or accepted the call outright when the archive carried no dict_id.
 */
int test_stream_oversized_dict(void) {
    printf("=== TEST: Stream - oversized dictionary refused both ways ===\n");

    const size_t size = 64 * 1024;
    uint8_t* input = malloc(size);
    uint8_t* oversized = malloc(ZXC_DICT_SIZE_MAX + 1);
    if (!input || !oversized) {
        free(input);
        free(oversized);
        return 0;
    }
    gen_lz_data(input, size);
    memset(oversized, 'x', ZXC_DICT_SIZE_MAX + 1);

    int ok = 1;
    FILE* f_arc = tmpfile();
    if (!f_arc) {
        printf("  [SKIP] tmpfile failed\n");
        free(input);
        free(oversized);
        return 1;
    }

    /* A plain archive: no dict_id, so no binding check stands in the way. */
    {
        FILE* f_in = tmpfile();
        zxc_compress_opts_t co = {.n_threads = 1, .level = 3, .block_size = 16 * 1024};
        if (!f_in) {
            printf("  [SKIP] tmpfile failed\n");
            fclose(f_arc);
            free(input);
            free(oversized);
            return 1;
        }
        fwrite(input, 1, size, f_in);
        fseek(f_in, 0, SEEK_SET);
        if (zxc_stream_compress(f_in, f_arc, &co) <= 0) {
            printf("Failed: compress\n");
            ok = 0;
        }
        fclose(f_in);
    }

    /* Compression side: the guard moved into the engine, it must still fire. */
    if (ok) {
        FILE* f_in = tmpfile();
        FILE* f_out = tmpfile();
        zxc_compress_opts_t co = {
            .n_threads = 1, .level = 3, .dict = oversized, .dict_size = ZXC_DICT_SIZE_MAX + 1};
        if (f_in && f_out) {
            fwrite(input, 1, size, f_in);
            fseek(f_in, 0, SEEK_SET);
            const int64_t rc = zxc_stream_compress(f_in, f_out, &co);
            if (rc != ZXC_ERROR_DICT_TOO_LARGE) {
                printf("Failed: compress gave %lld, want DICT_TOO_LARGE\n", (long long)rc);
                ok = 0;
            }
        }
        if (f_in) fclose(f_in);
        if (f_out) fclose(f_out);
    }

    /* Decompression side, single- and multi-threaded. */
    for (int threads = 1; ok && threads <= 4; threads += 3) {
        FILE* f_out = tmpfile();
        if (!f_out) break;
        fseek(f_arc, 0, SEEK_SET);
        zxc_decompress_opts_t opts = {
            .n_threads = threads, .dict = oversized, .dict_size = ZXC_DICT_SIZE_MAX + 1};
        const int64_t rc = zxc_stream_decompress(f_arc, f_out, &opts);
        if (rc != ZXC_ERROR_DICT_TOO_LARGE) {
            printf("Failed: decompress with %d thread(s) gave %lld, want DICT_TOO_LARGE\n", threads,
                   (long long)rc);
            ok = 0;
        }
        fclose(f_out);
    }

    /* A dictionary exactly on the bound stays acceptable. */
    if (ok) {
        FILE* f_out = tmpfile();
        if (f_out) {
            fseek(f_arc, 0, SEEK_SET);
            zxc_decompress_opts_t opts = {
                .n_threads = 1, .dict = oversized, .dict_size = ZXC_DICT_SIZE_MAX};
            const int64_t rc = zxc_stream_decompress(f_arc, f_out, &opts);
            if (rc != (int64_t)size) {
                printf("Failed: dict at the bound gave %lld, want %zu\n", (long long)rc, size);
                ok = 0;
            }
            fclose(f_out);
        }
    }

    fclose(f_arc);
    free(input);
    free(oversized);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Checksums seeded by one path must verify through the others: a path alone
 * round-trips its own mistake. */
int test_stream_checksum_cross_paths(void) {
    printf("=== TEST: Stream - block checksums agree across writers and readers ===\n");
    enum { BS = 4096, N = 16 * BS };
    static uint8_t src[N], arc[2 * N], out[N];
    gen_lz_data(src, N);
    const zxc_compress_opts_t co = {
        .n_threads = 4, .level = 3, .block_size = BS, .checksum_enabled = 1, .seekable = 1};
    const zxc_decompress_opts_t verify = {.n_threads = 4, .checksum_enabled = 1};
    FILE* const f_src = tmpfile();
    FILE* const f_arc = tmpfile();
    FILE* const f_out = tmpfile();
    int ok = 0;
    do {
        if (!f_src || !f_arc || !f_out || fwrite(src, 1, N, f_src) != N) {
            printf("  [FAIL] tmpfile\n");
            break;
        }
        rewind(f_src);
        /* Multi-threaded stream writer, then the one-shot and seekable readers. */
        if (zxc_stream_compress(f_src, f_arc, &co) <= 0) {
            printf("  [FAIL] stream compress\n");
            break;
        }
        rewind(f_arc);
        const size_t n = fread(arc, 1, sizeof(arc), f_arc);
        const int64_t one = zxc_decompress(arc, n, out, N, &verify);
        const int one_ok = one == N && memcmp(out, src, N) == 0;
        zxc_seekable* const s = zxc_seekable_open(arc, n);
        if (s) zxc_seekable_set_checksum(s, 1);
        const int64_t st = s ? zxc_seekable_decompress_range(s, out, N, 0, N) : -1;
        const int st_ok = st == N && memcmp(out, src, N) == 0;
        const int64_t mt = s ? zxc_seekable_decompress_range_mt(s, out, N, 0, N, 4) : -1;
        const int mt_ok = mt == N && memcmp(out, src, N) == 0;
        zxc_seekable_free(s);

        /* One-shot writer, then the multi-threaded stream reader. */
        const int64_t m = zxc_compress(src, N, arc, sizeof(arc), &co);
        FILE* const f_one = tmpfile();
        const int wrote = f_one && m > 0 && fwrite(arc, 1, (size_t)m, f_one) == (size_t)m;
        if (f_one) rewind(f_one);
        const int64_t stream = wrote ? zxc_stream_decompress(f_one, f_out, &verify) : -1;
        if (f_one) fclose(f_one);
        rewind(f_out);
        const int stream_ok =
            stream == N && fread(out, 1, N, f_out) == N && memcmp(out, src, N) == 0;

        if (!one_ok || !st_ok || !mt_ok || !stream_ok) {
            printf("  [FAIL] one-shot %lld, seekable st %lld / mt %lld, stream %lld\n",
                   (long long)one, (long long)st, (long long)mt, (long long)stream);
            break;
        }
        ok = 1;
    } while (0);
    if (f_src) fclose(f_src);
    if (f_arc) fclose(f_arc);
    if (f_out) fclose(f_out);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* Decodes @p arc whole through a push stream; 1 when it validates the footer. */
static int dstream_finishes(const uint8_t* arc, size_t alen, uint8_t* out, size_t n) {
    const zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
    zxc_dstream* const ds = zxc_dstream_create(&dopts);
    if (!ds) return 0;
    zxc_inbuf_t in = {arc, alen, 0};
    zxc_outbuf_t ob = {out, n, 0};
    int ok = 1;
    for (int i = 0; i < 4 && ok && !zxc_dstream_finished(ds); i++)
        ok = zxc_dstream_decompress(ds, &ob, &in) >= 0;
    ok = ok && zxc_dstream_finished(ds) && ob.pos == n;
    zxc_dstream_free(ds);
    return ok;
}

/* One source size in ~65536 reads back, as the footer's u64, like a valid SEK
 * header. The tail readers must tell it from a real table, with or without one. */
/* The CLI's `-t --progress` path asks for the stored size on the very stream it
 * then decodes: the lookup seeks to the footer and back, and the decode has to
 * start from a stream still in step.
 *
 * The second round adds a caller buffer, installed before the lookup as the CLI
 * does it: setvbuf is defined only before a stream's first operation and its
 * buffer must outlive the stream, hence one FILE per round freed after fclose.
 * That order the other way round is what made Windows read a truncated frame. */
int test_stream_size_then_decompress(void) {
    printf("=== TEST: Stream - stored-size lookup then decode, same stream ===\n");
    const size_t n = 600u * 1024u; /* > 1 block at the 512 KB default */
    const char* const path = "zxc_size_then_decode.tmp";
    uint8_t* const src = malloc(n);
    uint8_t* const out = malloc(n);
    const size_t cap = (size_t)zxc_compress_bound(n);
    uint8_t* const arc = malloc(cap);
    if (!src || !out || !arc) {
        free(src);
        free(out);
        free(arc);
        return 0;
    }
    gen_lz_data(src, n);

    const zxc_compress_opts_t co = {.level = 3, .checksum_enabled = 1};
    const int64_t alen = zxc_compress(src, n, arc, cap, &co);
    int ok = alen > 0;
    if (!ok) printf("  [FAIL] compress returned %lld\n", (long long)alen);

    if (ok) {
        FILE* const f = create_restricted_file(path);
        ok = f && fwrite(arc, 1, (size_t)alen, f) == (size_t)alen;
        if (f && fclose(f) != 0) ok = 0;
        if (!ok) printf("  [FAIL] could not stage %s\n", path);
    }
    if (ok) {
        FILE* const f = fopen(path, "rb");
        long staged = -1;
        if (f && fseek(f, 0, SEEK_END) == 0) staged = ftell(f);
        if (f) fclose(f);
        if (staged != (long)alen) {
            printf("  [FAIL] staged %ld bytes, wrote %lld: the file is not binary\n", staged,
                   (long long)alen);
            ok = 0;
        }
    }

    for (int buffered = 0; buffered <= 1 && ok; buffered++) {
        FILE* const f_arc = fopen(path, "rb");
        char* buf = NULL;
        if (f_arc && buffered) {
            buf = malloc(1u << 16);
            if (buf) setvbuf(f_arc, buf, _IOFBF, 1u << 16);
        }
        FILE* const f_out = tmpfile();
        int64_t reported = -1, got = -1;
        if (f_arc && f_out) {
            reported = zxc_stream_get_decompressed_size(f_arc);
            const zxc_decompress_opts_t verify = {.n_threads = 1, .checksum_enabled = 1};
            got = zxc_stream_decompress(f_arc, f_out, &verify);
        }
        if (reported != (int64_t)n || got != (int64_t)n) {
            printf("  [FAIL] buffered=%d: size %lld, decode %lld, want %zu\n", buffered,
                   (long long)reported, (long long)got, n);
            ok = 0;
        } else {
            rewind(f_out);
            if (fread(out, 1, n, f_out) != n || memcmp(out, src, n) != 0) {
                printf("  [FAIL] buffered=%d: decoded bytes differ\n", buffered);
                ok = 0;
            }
        }
        if (f_out) fclose(f_out);
        if (f_arc) fclose(f_arc); /* the buffer stays alive until here */
        free(buf);
    }

    remove(path);
    free(src);
    free(out);
    free(arc);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_stream_footer_looks_like_sek(void) {
    printf("=== TEST: Stream - a footer that parses as a SEK header ===\n");
    uint64_t n = 0;
    for (uint64_t k = ZXC_BLOCK_SEK; k < (1u << 24) && !n; k += 256) {
        uint8_t b[8];
        zxc_store_le64(b, k);
        zxc_block_header_t bh;
        if (zxc_read_block_header(b, sizeof(b), &bh) == ZXC_OK && bh.block_type == ZXC_BLOCK_SEK)
            n = k;
    }
    if (!n) {
        printf("  [FAIL] no such size below 16 MiB\n");
        return 0;
    }
    printf("  size %llu bytes parses as a SEK header\n", (unsigned long long)n);

    uint8_t* const src = malloc((size_t)n);
    uint8_t* const out = malloc((size_t)n);
    const size_t acap = (size_t)zxc_compress_bound((size_t)n) + 64;
    uint8_t* const arc = malloc(acap);
    int ok = 0;
    if (src && out && arc) {
        gen_lz_data(src, (size_t)n);
        ok = 1;
        for (int seekable = 0; seekable <= 1 && ok; seekable++) {
            const zxc_compress_opts_t co = {
                .n_threads = 1, .level = 3, .checksum_enabled = 1, .seekable = seekable};
            const zxc_decompress_opts_t verify = {.n_threads = 1, .checksum_enabled = 1};
            FILE* const f_src = tmpfile();
            FILE* const f_arc = tmpfile();
            FILE* const f_out = tmpfile();
            int64_t st = -1;
            int push = 0;
            if (f_src && f_arc && f_out && fwrite(src, 1, (size_t)n, f_src) == n) {
                rewind(f_src);
                if (zxc_stream_compress(f_src, f_arc, &co) > 0) {
                    rewind(f_arc);
                    const int64_t alen = (int64_t)fread(arc, 1, acap, f_arc);
                    rewind(f_arc);
                    st = zxc_stream_decompress(f_arc, f_out, &verify);
                    push = alen > 0 && dstream_finishes(arc, (size_t)alen, out, (size_t)n);
                }
            }
            if (f_src) fclose(f_src);
            if (f_arc) fclose(f_arc);
            if (f_out) fclose(f_out);
            if (st != (int64_t)n || !push || memcmp(out, src, (size_t)n) != 0) {
                printf("  [FAIL] seekable=%d: stream reader -> %lld, push stream %s\n", seekable,
                       (long long)st, push ? "finished" : "did not finish");
                ok = 0;
            }
        }
    }
    free(src);
    free(out);
    free(arc);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* FILE* and buffer decoders must return @p want; push decodes @p n bytes, stops at @p push_end. */
static int trailing_verdict(const uint8_t* arc, const size_t total, const size_t n,
                            const uint8_t* src, uint8_t* out, const int64_t want,
                            const size_t push_end, const char* what) {
    FILE* const f = tmpfile();
    int64_t rf = -1;
    if (f && fwrite(arc, 1, total, f) == total && fseek(f, 0, SEEK_SET) == 0)
        rf = zxc_stream_decompress(f, NULL, NULL);
    if (f) fclose(f);
    const int64_t rb = zxc_decompress(arc, total, out, n, NULL);

    zxc_dstream* const ds = zxc_dstream_create(NULL);
    zxc_inbuf_t in = {arc, total, 0};
    zxc_outbuf_t ob = {out, n, 0};
    const int64_t rp = ds ? zxc_dstream_decompress(ds, &ob, &in) : -1;
    const int push_ok = rp == (int64_t)n && zxc_dstream_finished(ds) && in.pos == push_end &&
                        memcmp(out, src, n) == 0;
    zxc_dstream_free(ds);

    if (rf == want && rb == want && push_ok) return 1;
    printf("Failed: %s: FILE* %lld, buffer %lld (want %lld), push stopped at %zu (want %zu)\n",
           what, (long long)rf, (long long)rb, (long long)want, in.pos, push_end);
    return 0;
}

/* Bytes after the footer: corrupt for the FILE* and buffer decoders; the push API
 * stops at the footer and leaves them to the caller. */
int test_stream_trailing_bytes(void) {
    printf("=== TEST: Stream - bytes after the footer ===\n");
    const size_t n = 3 * 4096 + 5;
    const size_t cap = (size_t)zxc_compress_bound(n);
    uint8_t* const src = malloc(n);
    uint8_t* const arc = malloc(2 * cap);
    uint8_t* const forged = malloc(cap + ZXC_FILE_FOOTER_SIZE + ZXC_FILE_DIGEST_SIZE);
    uint8_t* const out = malloc(n);
    int ok = src && arc && forged && out;
    if (ok) gen_lz_data(src, n);

    for (int v = 0; ok && v < 4; v++) {
        const zxc_compress_opts_t co = {
            .level = 3, .block_size = 4096, .checksum_enabled = v & 1, .seekable = v >> 1};
        const int64_t len = zxc_compress(src, n, arc, cap, &co);
        if (len <= 0) {
            ok = 0;
            break;
        }
        char what[96];

        // No tail, one byte, a second archive.
        memcpy(arc + len, arc, (size_t)len);
        const size_t tails[] = {0, 1, (size_t)len};
        for (size_t t = 0; ok && t < sizeof(tails) / sizeof(tails[0]); t++) {
            snprintf(what, sizeof(what), "checksum %d, seekable %d, %zu trailing bytes", v & 1,
                     v >> 1, tails[t]);
            ok =
                trailing_verdict(arc, (size_t)len + tails[t], n, src, out,
                                 tails[t] ? ZXC_ERROR_CORRUPT_DATA : (int64_t)n, (size_t)len, what);
        }

        // Flag cleared, [EOF][valid footer][SEK][footer]: the first footer checks
        // out, only the end-of-input check catches the rest.
        if (ok && co.seekable) {
            const size_t fl = zxc_footer_bytes(co.checksum_enabled);
            const size_t sek = (size_t)len - fl - ZXC_BLOCK_HEADER_SIZE -
                               (size_t)zxc_seek_table_bytes(zxc_seek_block_count(n, 4096));
            memcpy(forged, arc, sek);
            memcpy(forged + sek, arc + len - fl, fl);
            memcpy(forged + sek + fl, arc + sek, (size_t)len - sek);
            forged[6] &= (uint8_t)~ZXC_FILE_FLAG_HAS_SEEK_TABLE;
            zxc_file_header_sign(forged);
            snprintf(what, sizeof(what), "checksum %d, footer then an unannounced table", v & 1);
            ok = trailing_verdict(forged, (size_t)len + fl, n, src, out, ZXC_ERROR_CORRUPT_DATA,
                                  sek + fl, what);
        }
    }

    free(src);
    free(arc);
    free(forged);
    free(out);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}
