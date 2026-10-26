// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

#include "test_common.h"

#if !defined(_WIN32)
#include <signal.h>
#include <unistd.h>
#endif

// A compression context for the FILE* pipeline with its usual parameters set; the
// caller frees it. NULL, which the pipeline reads as defaults, only on failure.
static zxc_cctx* cctx_for(const int n_threads, const int level, const int checksum) {
    zxc_cctx* const c = zxc_create_cctx();
    if (c && (zxc_cctx_set_param(c, ZXC_C_THREADS, n_threads) != ZXC_OK ||
              zxc_cctx_set_param(c, ZXC_C_LEVEL, level) != ZXC_OK ||
              zxc_cctx_set_param(c, ZXC_C_CHECKSUM, checksum) != ZXC_OK)) {
        printf("  [FAIL] cctx_for(%d, %d, %d)\n", n_threads, level, checksum);
        zxc_free_cctx(c);
        return NULL;
    }
    return c;
}

// The decompression counterpart of cctx_for().
static zxc_dctx* dctx_for(const int n_threads, const int checksum) {
    zxc_dctx* const d = zxc_create_dctx();
    if (d && (zxc_dctx_set_param(d, ZXC_D_THREADS, n_threads) != ZXC_OK ||
              zxc_dctx_set_param(d, ZXC_D_CHECKSUM, checksum) != ZXC_OK)) {
        printf("  [FAIL] dctx_for(%d, %d)\n", n_threads, checksum);
        zxc_free_dctx(d);
        return NULL;
    }
    return d;
}

// Checks that the stream decompression can accept NULL output (Integrity Check Mode)
int test_null_output_decompression(void) {
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
    zxc_cctx* const _sco3 = cctx_for(1, 3, 1);
    const int64_t _sco3_r = zxc_stream_compress(f_in, f_comp, _sco3);
    zxc_free_cctx(_sco3);
    if (_sco3_r < 0) {
        printf("Compression Failed!\n");
        fclose(f_in);
        fclose(f_comp);
        free(input);
        return 0;
    }
    fseek(f_comp, 0, SEEK_SET);

    // Decompress with NULL output
    // This should return the decompressed size but write nothing
    zxc_dctx* const _sdo4 = dctx_for(1, 1);
    int64_t d_sz = zxc_stream_decompress(f_comp, NULL, _sdo4);
    zxc_free_dctx(_sdo4);

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
int test_invalid_arguments(void) {
    printf("=== TEST: Unit - Invalid Arguments ===\n");

    FILE* const f = test_tmpfile();
    FILE* const f_valid = test_tmpfile();
    int ok = 0;

    // Prepare a valid compressed stream for decompression tests
    zxc_cctx* const _sco5 = cctx_for(1, 1, 0);
    zxc_stream_compress(f, f_valid, _sco5);
    zxc_free_cctx(_sco5);
    fseek(f_valid, 0, SEEK_SET);

    // 1. Input NULL -> Must fail
    zxc_cctx* const _sco6 = cctx_for(1, 5, 0);
    const int64_t _sco6_r = zxc_stream_compress(NULL, f, _sco6);
    zxc_free_cctx(_sco6);
    if (_sco6_r >= 0) {
        printf("Failed: Should return < 0 when Input is NULL\n");
        goto cleanup;
    }

    // 2. Output NULL -> Must SUCCEED (Benchmark / Dry-Run Mode)
    zxc_cctx* const _sco7 = cctx_for(1, 5, 0);
    const int64_t _sco7_r = zxc_stream_compress(f, NULL, _sco7);
    zxc_free_cctx(_sco7);
    if (_sco7_r < 0) {
        printf("Failed: Should allow NULL Output (Benchmark mode support)\n");
        goto cleanup;
    }

    // 3. Decompression Input NULL -> Must fail
    zxc_dctx* const _sdo8 = dctx_for(1, 0);
    const int64_t _sdo8_r = zxc_stream_decompress(NULL, f, _sdo8);
    zxc_free_dctx(_sdo8);
    if (_sdo8_r >= 0) {
        printf("Failed: Decompress should return < 0 when Input is NULL\n");
        goto cleanup;
    }

    // 3b. Decompression Output NULL -> Must SUCCEED (Benchmark mode)
    zxc_dctx* const _sdo9 = dctx_for(1, 0);
    const int64_t _sdo9_r = zxc_stream_decompress(f_valid, NULL, _sdo9);
    zxc_free_dctx(_sdo9);
    if (_sdo9_r < 0) {
        printf("Failed: Decompress should allow NULL Output (Benchmark mode support)\n");
        goto cleanup;
    }

    // 4. zxc_compress NULL checks
    if (zxc_compress(NULL, 100, TEST_POISON_PTR, 100, 3) >= 0) {
        printf("Failed: zxc_compress should return < 0 when src is NULL\n");
        goto cleanup;
    }
    if (zxc_compress(TEST_POISON_PTR, 100, NULL, 100, 3) >= 0) {
        printf("Failed: zxc_compress should return < 0 when dst is NULL\n");
        goto cleanup;
    }

    // 5. zxc_decompress NULL checks
    if (zxc_decompress(NULL, 100, TEST_POISON_PTR, 100) >= 0) {
        printf("Failed: zxc_decompress should return < 0 when src is NULL\n");
        goto cleanup;
    }
    if (zxc_decompress(TEST_POISON_PTR, 100, NULL, 100) >= 0) {
        printf("Failed: zxc_decompress should return < 0 when dst is NULL\n");
        goto cleanup;
    }

    // 6. zxc_compress_bound overflow check
    if (zxc_compress_bound(SIZE_MAX) != 0) {
        printf("Failed: zxc_compress_bound should return 0 on overflow\n");
        goto cleanup;
    }

    printf("PASS\n\n");
    ok = 1;
cleanup:
    fclose(f);
    fclose(f_valid);
    return ok;
}

// Checks behavior with truncated compressed input
int test_truncated_input(void) {
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

    int64_t comp_sz = test_compress_cfg(src, SRC_SIZE, compressed, cap, 3, 0, 1, 0);
    if (comp_sz <= 0) {
        printf("Prepare failed\n");
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    // Try decompressing with progressively cropped size
    // 1. Cut off the Footer
    const size_t flen = test_footer_len(compressed, (size_t)comp_sz);
    if (flen && (size_t)comp_sz > flen) {
        if (test_decompress_verified(compressed, (size_t)comp_sz - flen, decomp_buf, SRC_SIZE) >=
            0) {
            printf("Failed: Should fail when footer is missing\n");
            free(compressed);
            free(decomp_buf);
            return 0;
        }
    }

    // 2. Cut off half the file
    if (test_decompress_verified(compressed, (size_t)(comp_sz / 2), decomp_buf, SRC_SIZE) >= 0) {
        printf("Failed: Should fail when stream is truncated by half\n");
        free(compressed);
        free(decomp_buf);
        return 0;
    }

    // 3. Cut off just 1 byte
    if (test_decompress_verified(compressed, (size_t)(comp_sz - 1), decomp_buf, SRC_SIZE) >= 0) {
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
int test_io_failures(void) {
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
    zxc_cctx* const _sco18 = cctx_for(1, 5, 0);
    const int64_t _sco18_r = zxc_stream_compress(f_in, f_out, _sco18);
    zxc_free_cctx(_sco18);
    if (_sco18_r >= 0) {
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

    zxc_cctx* const copts = cctx_for(1, 0, 0);
    zxc_dctx* const dopts = dctx_for(1, 0);

    fseek(f_in, 0, SEEK_SET);
    const int64_t c_pipe = zxc_stream_compress(f_in, f_pipe_c, copts);
    fseek(f_in, 0, SEEK_SET);
    const int64_t c_ok = zxc_stream_compress(f_in, f_arc, copts);
    zxc_free_cctx(copts);
    fseek(f_arc, 0, SEEK_SET);
    const int64_t d_pipe = zxc_stream_decompress(f_arc, f_pipe_d, dopts);
    zxc_free_dctx(dopts);

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
int test_thread_params(void) {
    printf("=== TEST: Unit - Thread Parameters ===\n");

    FILE* f_in = tmpfile();
    FILE* f_out = tmpfile();
    if (!f_in || !f_out) {
        if (f_in) fclose(f_in);
        if (f_out) fclose(f_out);
        return 0;
    }

    // Test with 0 (Auto) and negative value - must not crash
    zxc_cctx* const _sco19 = cctx_for(0, 5, 0);
    zxc_stream_compress(f_in, f_out, _sco19);
    fseek(f_in, 0, SEEK_SET);
    fseek(f_out, 0, SEEK_SET);
    // A negative count is refused by the setter; the context keeps auto-detect.
    const int rc = zxc_cctx_set_param(_sco19, ZXC_C_THREADS, -5);
    zxc_stream_compress(f_in, f_out, _sco19);
    zxc_free_cctx(_sco19);
    if (rc != ZXC_ERROR_BAD_PARAM) {
        printf("Failed: n_threads = -5 gave %d, want ZXC_ERROR_BAD_PARAM\n", rc);
        fclose(f_in);
        fclose(f_out);
        return 0;
    }

    printf("PASS (No crash observed)\n\n");
    fclose(f_in);
    fclose(f_out);
    return 1;
}

// Multi-threaded round-trip test for TSan coverage
int test_multithread_roundtrip(void) {
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
        zxc_cctx* const _sco21 = cctx_for(num_threads, 3, 1);
        const int64_t _sco21_r = zxc_stream_compress(f_in, f_comp, _sco21);
        zxc_free_cctx(_sco21);
        if (_sco21_r < 0) {
            printf("Compression failed (threads=%d)!\n", num_threads);
            fclose(f_in);
            fclose(f_comp);
            fclose(f_decomp);
            goto cleanup;
        }

        fseek(f_comp, 0, SEEK_SET);

        zxc_dctx* const _sdo22 = dctx_for(num_threads, 1);
        const int64_t _sdo22_r = zxc_stream_decompress(f_comp, f_decomp, _sdo22);
        zxc_free_dctx(_sdo22);
        if (_sdo22_r < 0) {
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

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
// Reads endless zeros and seeks anywhere, but once at the end no longer tells where.
static int read_zeros(void* cookie, char* buf, const int len) {
    (void)cookie;
    memset(buf, 0, (size_t)len);
    return len;
}
static fpos_t tell_fails_after_end(void* cookie, const fpos_t off, const int whence) {
    int* const at_end = (int*)cookie;
    if (whence == SEEK_END) *at_end = 1;
    if (whence == SEEK_CUR && *at_end) return -1;
    return whence == SEEK_END ? 100 + off : off;
}
#endif

int test_stream_get_decompressed_size_errors(void) {
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

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    // 2b. A stream that seeks to its end but cannot tell where that is: an I/O
    //     error, never a read sized by the failed tell.
    {
        static int tell_after_end = 0;
        tell_after_end = 0;
        FILE* f = funopen(&tell_after_end, read_zeros, NULL, tell_fails_after_end, NULL);
        zxc_frame_info_t fi;
        const int rc = f ? zxc_stream_get_frame_info(f, &fi, sizeof(fi)) : ZXC_ERROR_IO;
        if (f) fclose(f);
        if (!f || rc != ZXC_ERROR_IO) {
            printf("  [FAIL] failed tell: expected %d, got %d\n", ZXC_ERROR_IO, rc);
            return 0;
        }
    }
    printf("  [PASS] failed tell -> ZXC_ERROR_IO\n");
#endif

    // 3. Bad magic word
    {
        FILE* f = tmpfile();
        if (!f) {
            printf("  [SKIP] tmpfile failed\n");
            return 0;
        }
        // Write enough bytes but with wrong magic
        uint8_t garbage[ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_MAX_SIZE];
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
    //     size: its block size caps the stored one. Same verdict as the
    //     decoders. A forged footer size is capped like the buffer API caps it.
    {
        const size_t src_sz = 4096;
        uint8_t* src = malloc(src_sz);
        gen_lz_data(src, src_sz);
        const size_t cap = zxc_compress_bound(src_sz);
        uint8_t* comp = malloc(cap);
        const int64_t comp_sz = test_compress_cfg(src, src_sz, comp, cap, 1, 0, 1, 0);
        if (comp_sz <= 0) {
            printf("  [SKIP] compress failed\n");
            free(src);
            free(comp);
            return 0;
        }
        // The header checksum flipped, then a well-formed footer storing 2^63 - 1
        // bytes, far beyond what the blocks hold.
        uint8_t* const forged = malloc(cap + ZXC_FILE_FOOTER_MAX_SIZE);
        const size_t forged_sz =
            forged ? test_forge_footer_size(comp, (size_t)comp_sz, forged,
                                            cap + ZXC_FILE_FOOTER_MAX_SIZE, INT64_MAX)
                   : 0;
        struct {
            const char* what;
            const uint8_t* arc;
            size_t len;
            size_t flip;
            int expect;
        } forge[] = {
            {"header checksum", comp, (size_t)comp_sz, 14, ZXC_ERROR_BAD_HEADER},
            {"footer size", forged, forged_sz, (size_t)-1, ZXC_ERROR_CORRUPT_DATA},
        };
        for (size_t k = 0; k < sizeof(forge) / sizeof(forge[0]); k++) {
            FILE* f = tmpfile();
            if (!f || forge[k].len == 0) {
                printf("  [SKIP] tmpfile or forge failed\n");
                if (f) fclose(f);
                free(forged);
                free(src);
                free(comp);
                return 0;
            }
            if (forge[k].flip != (size_t)-1) comp[forge[k].flip] ^= 0x7F;
            fwrite(forge[k].arc, 1, forge[k].len, f);
            if (forge[k].flip != (size_t)-1) comp[forge[k].flip] ^= 0x7F;
            fseek(f, 0, SEEK_SET);
            r = zxc_stream_get_decompressed_size(f);
            fclose(f);
            if (r != forge[k].expect) {
                printf("  [FAIL] forged %s: expected %s, got %lld\n", forge[k].what,
                       zxc_error_name(forge[k].expect), (long long)r);
                free(forged);
                free(src);
                free(comp);
                return 0;
            }
        }
        free(forged);
        free(src);
        free(comp);
    }
    printf("  [PASS] forged header -> BAD_HEADER, forged footer size -> CORRUPT_DATA\n");

    // 4. Valid file returns correct size
    {
        // Create a valid compressed file in memory
        const size_t src_sz = 512;
        uint8_t* src = test_malloc(src_sz);
        gen_lz_data(src, src_sz);
        const size_t cap = zxc_compress_bound(src_sz);
        uint8_t* comp = test_malloc(cap);
        int64_t comp_sz = zxc_compress(src, src_sz, comp, cap, 3);
        if (comp_sz <= 0) {
            printf("  [SKIP] compress failed\n");
            free(src);
            free(comp);
            return 0;
        }

        FILE* f = test_tmpfile();
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

int test_stream_engine_errors(void) {
    printf("=== TEST: Unit - Stream Engine Error Codes ===\n");

    // 1. zxc_stream_compress with NULL f_in
    {
        FILE* f_out = tmpfile();
        zxc_cctx* const _sco49 = cctx_for(1, 3, 0);
        int64_t r = zxc_stream_compress(NULL, f_out, _sco49);
        zxc_free_cctx(_sco49);
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
        zxc_dctx* const _sdo50 = dctx_for(1, 0);
        int64_t r = zxc_stream_decompress(NULL, f_out, _sdo50);
        zxc_free_dctx(_sdo50);
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

        zxc_dctx* const _sdo51 = dctx_for(1, 0);
        int64_t r = zxc_stream_decompress(f_in, f_out, _sdo51);
        zxc_free_dctx(_sdo51);
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
        uint8_t* src = test_malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = test_tmpfile();
        FILE* f_comp_out = test_tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);

        zxc_cctx* const _sco52 = cctx_for(1, 3, 1);
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, _sco52);
        zxc_free_cctx(_sco52);
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
        uint8_t* comp_data = test_malloc(comp_file_sz);
        fseek(f_comp_out, 0, SEEK_SET);
        if (fread(comp_data, 1, comp_file_sz, f_comp_out) != (size_t)comp_file_sz) {
            printf("  [FAIL] fread failed\n");
            fclose(f_comp_out);
            free(comp_data);
            free(src);
            return 0;
        }
        fclose(f_comp_out);

        // Corrupt the stored source size in the footer
        comp_data[test_footer_size_at(comp_data, (size_t)comp_file_sz)] ^= 0x01;

        FILE* f_corrupt = test_tmpfile();
        FILE* f_dec_out = test_tmpfile();
        fwrite(comp_data, 1, comp_file_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        zxc_dctx* const _sdo53 = dctx_for(1, 1);
        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, _sdo53);
        zxc_free_dctx(_sdo53);
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
        uint8_t* src = test_malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = test_tmpfile();
        FILE* f_comp_out = test_tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);

        zxc_cctx* const _sco54 = cctx_for(1, 3, 1);
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, _sco54);
        zxc_free_cctx(_sco54);
        fclose(f_comp_in);
        if (comp_sz <= 0) {
            printf("  [SKIP] stream compress failed\n");
            fclose(f_comp_out);
            free(src);
            return 0;
        }

        fseek(f_comp_out, 0, SEEK_END);
        const long comp_file_sz = ftell(f_comp_out);
        uint8_t* comp_data = test_malloc(comp_file_sz);
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

        FILE* f_corrupt = test_tmpfile();
        FILE* f_dec_out = test_tmpfile();
        fwrite(comp_data, 1, comp_file_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        zxc_dctx* const _sdo55 = dctx_for(1, 1);
        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, _sdo55);
        zxc_free_dctx(_sdo55);
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
        uint8_t* src = test_malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = test_tmpfile();
        FILE* f_comp_out = test_tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);

        zxc_cctx* const _sco56 = cctx_for(1, 3, 0);
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, _sco56);
        zxc_free_cctx(_sco56);
        fclose(f_comp_in);
        free(src);
        if (comp_sz <= 0) {
            printf("  [SKIP] stream compress failed\n");
            fclose(f_comp_out);
            return 0;
        }

        fseek(f_comp_out, 0, SEEK_END);
        const long comp_file_sz = ftell(f_comp_out);
        if (comp_file_sz <= 0) {
            printf("  [FAIL] ftell failed\n");
            fclose(f_comp_out);
            return 0;
        }
        uint8_t* comp_data = test_malloc((size_t)comp_file_sz);
        fseek(f_comp_out, 0, SEEK_SET);
        if (fread(comp_data, 1, (size_t)comp_file_sz, f_comp_out) != (size_t)comp_file_sz) {
            printf("  [FAIL] fread failed\n");
            fclose(f_comp_out);
            free(comp_data);
            return 0;
        }
        fclose(f_comp_out);
        // Truncate: remove the EOF block header + footer
        const long trunc_sz =
            comp_file_sz -
            (long)(ZXC_BLOCK_HEADER_SIZE + test_footer_len(comp_data, (size_t)comp_file_sz));
        if (trunc_sz <= 0) {
            printf("  [FAIL] archive too short to truncate\n");
            free(comp_data);
            return 0;
        }

        FILE* f_corrupt = test_tmpfile();
        FILE* f_dec_out = test_tmpfile();
        fwrite(comp_data, 1, trunc_sz, f_corrupt);
        fseek(f_corrupt, 0, SEEK_SET);
        free(comp_data);

        zxc_dctx* const _sdo57 = dctx_for(1, 0);
        int64_t r = zxc_stream_decompress(f_corrupt, f_dec_out, _sdo57);
        zxc_free_dctx(_sdo57);
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
        uint8_t* src = test_malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = test_tmpfile();
        FILE* f_comp_out = test_tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);

        zxc_cctx* const sco_mb = cctx_for(1, 3, 0);
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, sco_mb);
        zxc_free_cctx(sco_mb);
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
            uint8_t* comp_data = test_malloc(trunc_sz);
            fseek(f_comp_out, 0, SEEK_SET);
            if (fread(comp_data, 1, trunc_sz, f_comp_out) == (size_t)trunc_sz) {
                FILE* f_trunc = test_tmpfile();
                FILE* f_dec_out = test_tmpfile();
                fwrite(comp_data, 1, trunc_sz, f_trunc);
                fseek(f_trunc, 0, SEEK_SET);

                zxc_dctx* const sdo_mb = dctx_for(1, 0);
                int64_t r = zxc_stream_decompress(f_trunc, f_dec_out, sdo_mb);
                zxc_free_dctx(sdo_mb);
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
        uint8_t* src = test_malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = test_tmpfile();
        FILE* f_comp_out = test_tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);
        free(src);

        zxc_cctx* const sco_io = cctx_for(1, 1, 0);
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, sco_io);
        zxc_free_cctx(sco_io);
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
            zxc_dctx* const sdo_io = dctx_for(1, 0);
            int64_t r = zxc_stream_decompress(f_comp_out, f_bad_out, sdo_io);
            zxc_free_dctx(sdo_io);
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
        uint8_t* src = test_malloc(src_sz);
        gen_lz_data(src, src_sz);

        FILE* f_comp_in = test_tmpfile();
        FILE* f_comp_out = test_tmpfile();
        fwrite(src, 1, src_sz, f_comp_in);
        fseek(f_comp_in, 0, SEEK_SET);
        free(src);

        zxc_cctx* const sco_mt = cctx_for(4, 1, 0);
        int64_t comp_sz = zxc_stream_compress(f_comp_in, f_comp_out, sco_mt);
        zxc_free_cctx(sco_mt);
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
            zxc_dctx* const sdo_mt = dctx_for(4, 0);
            int64_t r = zxc_stream_decompress(f_comp_out, f_bad_out2, sdo_mt);
            zxc_free_dctx(sdo_mt);
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
RT_WRAPPER(test_roundtrip_pak_text, "PAK Block (Text Pattern)", gen_lz_data, RT_BUF, 2, 0)
RT_WRAPPER(test_roundtrip_spl_text, "SPL Block (Text Pattern)", gen_lz_data, RT_BUF, 4, 0)
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
RT_WRAPPER(test_roundtrip_checksum_on, "Checksum Enabled", gen_lz_data, RT_BUF, 3, 1)

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
 * @brief On the streaming path, a context refuses 99 and -5 (ZXC_ERROR_BAD_LEVEL)
 *        and keeps its level: ULTRA's bytes, then FASTEST's.
 */
int test_stream_level_clamp(void) {
    printf("=== TEST: Stream - Out-of-range level refused, context keeps ULTRA / FASTEST ===\n");

    const size_t size = 96 * 1024;
    uint8_t* input = malloc(size);
    if (!input) return 0;
    gen_lz_data(input, size);

    uint8_t* out[4] = {NULL, NULL, NULL, NULL};
    long sz[4] = {0, 0, 0, 0};
    const int levels[4] = {ZXC_LEVEL_ULTRA, 99, ZXC_LEVEL_FASTEST, -5};
    int ok = 1;

    for (int i = 0; i < 4 && ok; i++) {
        FILE* f_in = tmpfile();
        FILE* f_comp = tmpfile();
        if (!f_in || !f_comp) {
            printf("  [SKIP] tmpfile failed\n");
            if (f_in) fclose(f_in);
            if (f_comp) fclose(f_comp);
            for (int j = 0; j < 4; j++) free(out[j]);
            free(input);
            return 1;
        }
        fwrite(input, 1, size, f_in);
        fseek(f_in, 0, SEEK_SET);

        // Odd entries: the valid level set first, then the out-of-range one refused.
        zxc_cctx* const o = cctx_for(1, levels[i & ~1], 0);
        const int rc = i & 1 ? zxc_cctx_set_param(o, ZXC_C_LEVEL, levels[i]) : ZXC_OK;
        const int64_t c = zxc_stream_compress(f_in, f_comp, o);
        zxc_free_cctx(o);
        if ((i & 1) && rc != ZXC_ERROR_BAD_LEVEL) {
            printf("Failed: level %d gave %d, want ZXC_ERROR_BAD_LEVEL\n", levels[i], rc);
            ok = 0;
        } else if (c <= 0) {
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

    for (int i = 0; i < 4 && ok; i += 2) {
        if (sz[i] != sz[i + 1] || memcmp(out[i], out[i + 1], (size_t)sz[i]) != 0) {
            printf(
                "Failed: level %d produced a different stream than level %d (%ld vs %ld bytes)\n",
                levels[i + 1], levels[i], sz[i], sz[i + 1]);
            ok = 0;
        }
    }

    for (int i = 0; i < 4; i++) free(out[i]);
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
    zxc_cctx* const o = cctx_for(1, 3, 0);
    const int64_t csz = zxc_cctx_set_param(o, ZXC_C_BLOCK_SIZE, (int64_t)bs) == ZXC_OK
                            ? zxc_stream_compress(f_in, f_comp, o)
                            : ZXC_ERROR_BAD_BLOCK_SIZE;
    zxc_free_cctx(o);
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
        uint8_t* forged = malloc(keep + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_MAX_SIZE);
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
        const size_t at = keep + ZXC_BLOCK_HEADER_SIZE;
        const size_t flen =
            at + (size_t)zxc_write_file_footer(forged + at, ZXC_FILE_FOOTER_MAX_SIZE, at, bs, 0, 0);
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
        zxc_cctx* const o = cctx_for(1, 3, cases[i].checksum);
        if (zxc_cctx_set_param(o, ZXC_C_BLOCK_SIZE, 64 * 1024) != ZXC_OK ||
            zxc_cctx_set_param(o, ZXC_C_SEEKABLE, cases[i].seekable) != ZXC_OK) {
            printf("Failed: %s parameters\n", cases[i].name);
            zxc_free_cctx(o);
            free(input);
            return 0;
        }
        int64_t written = 0;
        int64_t measured = 0;

        for (int pass = 0; pass < 2 && ok; pass++) {
            FILE* f_in = tmpfile();
            FILE* f_out = pass == 0 ? tmpfile() : NULL;
            if (!f_in || (pass == 0 && !f_out)) {
                printf("  [SKIP] tmpfile failed\n");
                if (f_in) fclose(f_in);
                if (f_out) fclose(f_out);
                zxc_free_cctx(o);
                free(input);
                return 1;
            }
            fwrite(input, 1, size, f_in);
            fseek(f_in, 0, SEEK_SET);

            const int64_t rc = zxc_stream_compress(f_in, f_out, o);
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
        zxc_free_cctx(o);

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
        zxc_cctx* const co = cctx_for(1, 3, 0);
        if (!f_in) {
            printf("  [SKIP] tmpfile failed\n");
            zxc_free_cctx(co);
            fclose(f_arc);
            free(input);
            free(oversized);
            return 1;
        }
        fwrite(input, 1, size, f_in);
        fseek(f_in, 0, SEEK_SET);
        if (zxc_cctx_set_param(co, ZXC_C_BLOCK_SIZE, 16 * 1024) != ZXC_OK ||
            zxc_stream_compress(f_in, f_arc, co) <= 0) {
            printf("Failed: compress\n");
            ok = 0;
        }
        zxc_free_cctx(co);
        fclose(f_in);
    }

    /* Compression side: the guard now sits in the setter, it must still fire. */
    if (ok) {
        zxc_cctx* const co = cctx_for(1, 3, 0);
        const int rc = zxc_cctx_set_dict(co, oversized, ZXC_DICT_SIZE_MAX + 1, NULL);
        if (rc != ZXC_ERROR_DICT_TOO_LARGE) {
            printf("Failed: set_dict gave %d, want DICT_TOO_LARGE\n", rc);
            ok = 0;
        }
        zxc_free_cctx(co);
    }

    /* Decompression side, single- and multi-threaded: the setter refuses too. */
    for (int threads = 1; ok && threads <= 4; threads += 3) {
        zxc_dctx* const opts = dctx_for(threads, 0);
        const int rc = zxc_dctx_set_dict(opts, oversized, ZXC_DICT_SIZE_MAX + 1, NULL);
        if (rc != ZXC_ERROR_DICT_TOO_LARGE) {
            printf("Failed: set_dict with %d thread(s) gave %d, want DICT_TOO_LARGE\n", threads,
                   rc);
            ok = 0;
        }
        zxc_free_dctx(opts);
    }

    /* A dictionary exactly on the bound stays acceptable. */
    if (ok) {
        FILE* f_out = tmpfile();
        zxc_dctx* const opts = dctx_for(1, 0);
        if (f_out) {
            fseek(f_arc, 0, SEEK_SET);
            const int64_t rc = zxc_dctx_set_dict(opts, oversized, ZXC_DICT_SIZE_MAX, NULL) == ZXC_OK
                                   ? zxc_stream_decompress(f_arc, f_out, opts)
                                   : ZXC_ERROR_DICT_TOO_LARGE;
            if (rc != (int64_t)size) {
                printf("Failed: dict at the bound gave %lld, want %zu\n", (long long)rc, size);
                ok = 0;
            }
            fclose(f_out);
        }
        zxc_free_dctx(opts);
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
    zxc_cctx* const co = cctx_for(4, 3, 1);
    zxc_dctx* const verify = dctx_for(4, 1);
    FILE* const f_src = tmpfile();
    FILE* const f_arc = tmpfile();
    FILE* const f_out = tmpfile();
    int ok = 0;
    do {
        if (!f_src || !f_arc || !f_out || fwrite(src, 1, N, f_src) != N) {
            printf("  [FAIL] tmpfile\n");
            break;
        }
        if (zxc_cctx_set_param(co, ZXC_C_BLOCK_SIZE, BS) != ZXC_OK ||
            zxc_cctx_set_param(co, ZXC_C_SEEKABLE, 1) != ZXC_OK) {
            printf("  [FAIL] parameters\n");
            break;
        }
        fseek(f_src, 0, SEEK_SET);
        /* Multi-threaded stream writer, then the one-shot and seekable readers. */
        if (zxc_stream_compress(f_src, f_arc, co) <= 0) {
            printf("  [FAIL] stream compress\n");
            break;
        }
        fseek(f_arc, 0, SEEK_SET);
        const size_t n = fread(arc, 1, sizeof(arc), f_arc);
        const int64_t one = test_decompress_verified(arc, n, out, N);
        const int one_ok = one == N && memcmp(out, src, N) == 0;
        zxc_seekable* const s = zxc_seekable_open(arc, n);
        if (s) zxc_seekable_set_checksum(s, 1);
        const int64_t st = s ? zxc_seekable_decompress_range(s, out, N, 0, N) : -1;
        const int st_ok = st == N && memcmp(out, src, N) == 0;
        const int64_t mt = s ? zxc_seekable_decompress_range_mt(s, out, N, 0, N, 4) : -1;
        const int mt_ok = mt == N && memcmp(out, src, N) == 0;
        zxc_seekable_free(s);

        /* One-shot writer on the same context, then the multi-threaded stream reader. */
        const int64_t m = zxc_compress_cctx(co, src, N, arc, sizeof(arc));
        FILE* const f_one = tmpfile();
        const int wrote = f_one && m > 0 && fwrite(arc, 1, (size_t)m, f_one) == (size_t)m;
        if (f_one) fseek(f_one, 0, SEEK_SET);
        const int64_t stream = wrote ? zxc_stream_decompress(f_one, f_out, verify) : -1;
        if (f_one) fclose(f_one);
        fseek(f_out, 0, SEEK_SET);
        const int stream_ok =
            stream == N && fread(out, 1, N, f_out) == N && memcmp(out, src, N) == 0;

        if (!one_ok || !st_ok || !mt_ok || !stream_ok) {
            printf("  [FAIL] one-shot %lld, seekable st %lld / mt %lld, stream %lld\n",
                   (long long)one, (long long)st, (long long)mt, (long long)stream);
            break;
        }
        ok = 1;
    } while (0);
    zxc_free_cctx(co);
    zxc_free_dctx(verify);
    if (f_src) fclose(f_src);
    if (f_arc) fclose(f_arc);
    if (f_out) fclose(f_out);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* Decodes @p arc whole through a push stream that verifies checksums; 1 when it
 * validates the footer. */
static int dstream_finishes(const uint8_t* arc, size_t alen, uint8_t* out, size_t n) {
    zxc_dctx* const dopts = dctx_for(0, 1);
    zxc_dstream* const ds = zxc_dstream_create(dopts);
    zxc_free_dctx(dopts);
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
    const size_t n = 600U * 1024U; /* > 1 block at the 512 KB default */
    const char* const path = "zxc_size_then_decode.tmp";
    uint8_t* const src = malloc(n);
    uint8_t* const out = malloc(n);
    const size_t cap = zxc_compress_bound(n);
    uint8_t* const arc = malloc(cap);
    if (!src || !out || !arc) {
        free(src);
        free(out);
        free(arc);
        return 0;
    }
    gen_lz_data(src, n);

    const int64_t alen = test_compress_cfg(src, n, arc, cap, 3, 0, 1, 0);
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
            buf = malloc(1U << 16);
            if (buf) setvbuf(f_arc, buf, _IOFBF, 1U << 16);
        }
        FILE* const f_out = tmpfile();
        int64_t reported = -1, got = -1;
        if (f_arc && f_out) {
            reported = zxc_stream_get_decompressed_size(f_arc);
            zxc_dctx* const verify = dctx_for(1, 1);
            got = zxc_stream_decompress(f_arc, f_out, verify);
            zxc_free_dctx(verify);
        }
        if (reported != (int64_t)n || got != (int64_t)n) {
            printf("  [FAIL] buffered=%d: size %lld, decode %lld, want %zu\n", buffered,
                   (long long)reported, (long long)got, n);
            ok = 0;
        } else {
            fseek(f_out, 0, SEEK_SET);
            if (fread(out, 1, n, f_out) != n || memcmp(out, src, n) != 0) {
                printf("  [FAIL] buffered=%d: decoded bytes differ\n", buffered);
                ok = 0;
            }
        }
        if (f_out) fclose(f_out);
        if (f_arc) fclose(f_arc); /* the buffer stays alive until here */
        free(buf);
    }

    /* A stale error indicator the caller left on the stream (a write to a read-only
     * FILE*) is not a read error of the decode. */
    if (ok) {
        FILE* const f_arc = fopen(path, "rb");
        FILE* const f_out = tmpfile();
        int64_t got = -1;
        // Where the write does not set the indicator, there is nothing to test.
        // cppcheck-suppress writeReadOnlyFile ; the failing write is the point
        const int stale = f_arc && f_out && fputc('x', f_arc) == EOF && ferror(f_arc);
        if (stale && fseek(f_arc, 0, SEEK_SET) == 0)
            got = zxc_stream_decompress(f_arc, f_out, NULL);
        if (stale && got != (int64_t)n) {
            printf("  [FAIL] stale error indicator: decode %lld, want %zu\n", (long long)got, n);
            ok = 0;
        }
        if (f_out) fclose(f_out);
        if (f_arc) fclose(f_arc);
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
    /* Footer bytes with the SEK type and a valid checksum, but Flags/Reserved set. */
    uint64_t n = 0;
    for (uint64_t k = ZXC_BLOCK_SEK; k < (1U << 24) && !n; k += 256) {
        uint8_t b[8];
        zxc_store_le64(b, k);
        const uint8_t sum = b[7];
        b[7] = 0;
        if (sum == zxc_hash8(b)) n = k;
    }
    if (!n) {
        printf("  [FAIL] no such size below 16 MiB\n");
        return 0;
    }
    {
        uint8_t b[8];
        zxc_store_le64(b, n);
        zxc_block_header_t bh;
        if (zxc_read_block_header(b, sizeof(b), &bh) != ZXC_ERROR_BAD_HEADER) {
            printf("  [FAIL] size %llu parses as a SEK header\n", (unsigned long long)n);
            return 0;
        }
    }
    printf("  size %llu bytes hashes like a SEK header\n", (unsigned long long)n);

    uint8_t* const src = malloc((size_t)n);
    uint8_t* const out = malloc((size_t)n);
    const size_t acap = zxc_compress_bound((size_t)n) + 64;
    uint8_t* const arc = malloc(acap);
    int ok = 0;
    if (src && out && arc) {
        gen_lz_data(src, (size_t)n);
        ok = 1;
        for (int seekable = 0; seekable <= 1 && ok; seekable++) {
            zxc_cctx* const co = cctx_for(1, 3, 1);
            zxc_dctx* const verify = dctx_for(1, 1);
            FILE* const f_src = tmpfile();
            FILE* const f_arc = tmpfile();
            FILE* const f_out = tmpfile();
            int64_t st = -1;
            int push = 0;
            if (f_src && f_arc && f_out && fwrite(src, 1, (size_t)n, f_src) == n &&
                zxc_cctx_set_param(co, ZXC_C_SEEKABLE, seekable) == ZXC_OK) {
                fseek(f_src, 0, SEEK_SET);
                if (zxc_stream_compress(f_src, f_arc, co) > 0) {
                    fseek(f_arc, 0, SEEK_SET);
                    const int64_t alen = (int64_t)fread(arc, 1, acap, f_arc);
                    fseek(f_arc, 0, SEEK_SET);
                    st = zxc_stream_decompress(f_arc, f_out, verify);
                    push = alen > 0 && dstream_finishes(arc, (size_t)alen, out, (size_t)n);
                }
            }
            zxc_free_cctx(co);
            zxc_free_dctx(verify);
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

/* Every decoder must return @p want into @p out (2 * @p n bytes), the push one @p want_push:
 * it cannot tell input that stops inside a magic word from input still coming, and
 * reports that as not finished (SRC_TOO_SMALL here). Push stops at each frame's end, so
 * it is called until it errs or makes no progress, into its own buffer. */
static int trailing_verdict(const uint8_t* arc, const size_t total, const size_t n,
                            const uint8_t* src, uint8_t* out, const int64_t want,
                            const int64_t want_push, const char* what) {
    FILE* const f = tmpfile();
    int64_t rf = -1;
    if (f && fwrite(arc, 1, total, f) == total && fseek(f, 0, SEEK_SET) == 0)
        rf = zxc_stream_decompress(f, NULL, NULL);
    if (f) fclose(f);
    const int64_t rb = zxc_decompress(arc, total, out, 2 * n);
    int ok = rb == want && (rb < 0 || (memcmp(out, src, n) == 0 &&
                                       (rb == (int64_t)n || memcmp(out + n, src, n) == 0)));

    uint8_t* const pout = malloc(2 * n);
    zxc_dstream* const ds = pout ? zxc_dstream_create(NULL) : NULL;
    zxc_inbuf_t in = {arc, total, 0};
    zxc_outbuf_t ob = {pout, 2 * n, 0};
    int64_t rp = ds ? 0 : -1;
    while (rp >= 0) {
        const size_t before = in.pos;
        rp = zxc_dstream_decompress(ds, &ob, &in);
        if (rp == 0 && in.pos == before) break;
    }
    if (rp >= 0) rp = zxc_dstream_finished(ds) ? (int64_t)ob.pos : ZXC_ERROR_SRC_TOO_SMALL;
    zxc_dstream_free(ds);
    ok = ok && rf == want && rp == want_push &&
         (rp < 0 ||
          (memcmp(pout, src, n) == 0 && (rp == (int64_t)n || memcmp(pout + n, src, n) == 0)));
    free(pout);

    if (ok) return 1;
    printf("Failed: %s: FILE* %lld, buffer %lld (want %lld), push %lld (want %lld)\n", what,
           (long long)rf, (long long)rb, (long long)want, (long long)rp, (long long)want_push);
    return 0;
}

/* A frame then bytes that open no other, in one call: the call hands over the frame,
 * in->pos on its end, and only the next call reports the junk. */
static int push_frame_then_junk(const uint8_t* arc, const size_t len, const size_t n,
                                const uint8_t* src, uint8_t* out) {
    zxc_dstream* const ds = zxc_dstream_create(NULL);
    zxc_inbuf_t in = {arc, len + 4, 0};
    zxc_outbuf_t ob = {out, 2 * n, 0};
    const int64_t r1 = ds ? zxc_dstream_decompress(ds, &ob, &in) : -1;
    const int done = ds && zxc_dstream_finished(ds);
    const size_t at = in.pos;
    const int64_t r2 = ds ? zxc_dstream_decompress(ds, &ob, &in) : -1;
    zxc_dstream_free(ds);
    if (r1 == (int64_t)n && done && at == len && memcmp(out, src, n) == 0 &&
        r2 == ZXC_ERROR_CORRUPT_DATA)
        return 1;
    printf("Failed: frame then junk: first call %lld (done %d, at %zu of %zu), then %lld\n",
           (long long)r1, done, at, len, (long long)r2);
    return 0;
}

/* Bytes after the footer: another frame decodes after the first, anything else is
 * corrupt, for every decoder; push hands over the frame before it says so. */
int test_stream_trailing_bytes(void) {
    printf("=== TEST: Stream - bytes after the footer ===\n");
    const size_t n = 3 * 4096 + 5;
    const size_t cap = zxc_compress_bound(n);
    uint8_t* const src = malloc(n);
    uint8_t* const arc = malloc(2 * cap);
    uint8_t* const forged = malloc(cap + ZXC_FILE_DIGEST_SIZE + ZXC_FILE_FOOTER_MAX_SIZE);
    uint8_t* const out = malloc(2 * n);
    int ok = src && arc && forged && out;
    if (ok) gen_lz_data(src, n);

    for (int v = 0; ok && v < 4; v++) {
        const int checksum = v & 1, seekable = v >> 1;
        const int64_t len = test_compress_cfg(src, n, arc, cap, 3, 4096, checksum, seekable);
        if (len <= 0) {
            ok = 0;
            break;
        }
        char what[96];

        // No tail, one byte, four bytes that are no magic word, a second archive.
        memcpy(arc + len, arc, (size_t)len);
        const size_t tails[] = {0, 1, 4, (size_t)len};
        const int64_t wants[] = {(int64_t)n, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA,
                                 2 * (int64_t)n};
        uint8_t* const tail = arc + len;
        for (size_t t = 0; ok && t < sizeof(tails) / sizeof(tails[0]); t++) {
            snprintf(what, sizeof(what), "checksum %d, seekable %d, %zu trailing bytes", v & 1,
                     v >> 1, tails[t]);
            uint8_t saved[4];
            memcpy(saved, tail, sizeof(saved));
            if (tails[t] == 4) memset(tail, 0xA5, 4);
            ok = trailing_verdict(arc, (size_t)len + tails[t], n, src, out, wants[t],
                                  tails[t] == 1 ? ZXC_ERROR_SRC_TOO_SMALL : wants[t], what);
            if (ok && tails[t] == 4) ok = push_frame_then_junk(arc, (size_t)len, n, src, out);
            memcpy(tail, saved, sizeof(saved));
        }

        // Flag cleared, [EOF][valid footer][SEK][footer]: the first footer checks
        // out, only the end-of-input check catches the rest.
        if (ok && seekable) {
            const size_t old_fl = test_footer_len(arc, (size_t)len);
            const size_t sek = (size_t)len - old_fl - ZXC_BLOCK_HEADER_SIZE -
                               (size_t)zxc_seek_table_bytes(zxc_seek_block_count(n, 4096));
            const uint64_t digest = checksum ? zxc_le64(arc + (size_t)len - old_fl) : 0;
            memcpy(forged, arc, sek);
            const size_t fl = (size_t)zxc_write_file_footer(
                forged + sek, ZXC_FILE_DIGEST_SIZE + ZXC_FILE_FOOTER_MAX_SIZE, sek, n, digest,
                checksum);
            memcpy(forged + sek + fl, arc + sek, (size_t)len - sek);
            forged[6] &= (uint8_t)~ZXC_FILE_FLAG_HAS_SEEK_TABLE;
            zxc_file_header_sign(forged);
            snprintf(what, sizeof(what), "checksum %d, footer then an unannounced table", v & 1);
            ok = trailing_verdict(forged, (size_t)len + fl, n, src, out, ZXC_ERROR_CORRUPT_DATA,
                                  ZXC_ERROR_CORRUPT_DATA, what);
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

/* Progress callback: counts the jobs written, in a global the analysers see change. */
static int jobs_written;

static void count_jobs(const uint64_t done, const uint64_t total, const void* user_data) {
    (void)done;
    (void)total;
    (void)user_data;
    jobs_written++;
}

/* Fills @p src with blocks of @p bs bytes, each periodic and unlike its neighbours. */
static void gen_periodic_blocks(uint8_t* src, const size_t n, const size_t bs) {
    for (size_t i = 0; i < n; i++) src[i] = (uint8_t)("zxc stream input"[i % 16] + i / bs);
}

#if !defined(_WIN32)
/* Decodes @p f_in into @p out through @p f_out; returns the decoder's verdict. */
static int64_t decode_stream_into(FILE* f_in, FILE* f_out, uint8_t* out, const size_t cap,
                                  const int n_threads) {
    zxc_dctx* const o = dctx_for(n_threads, 1);
    fseek(f_out, 0, SEEK_SET);
    const int64_t r = zxc_stream_decompress(f_in, f_out, o);
    zxc_free_dctx(o);
    if (r <= 0) return r;
    fseek(f_out, 0, SEEK_SET);
    return ((size_t)r <= cap && fread(out, 1, (size_t)r, f_out) == (size_t)r) ? r : -1;
}

/* Rewrites @p f as a @p pad-byte hole then the archive, positioned on it. */
static int refill_file(FILE* f, const size_t pad, const uint8_t* arc, const size_t n) {
    fseek(f, 0, SEEK_SET);
    return ftruncate(fileno(f), 0) == 0 && fseek(f, (long)pad, SEEK_SET) == 0 &&
           fwrite(arc, 1, n, f) == n && fflush(f) == 0 && fseek(f, (long)pad, SEEK_SET) == 0;
}

/* A stream that is not a regular file: a pipe, or a memory stream past its capacity. */
static FILE* open_live_stream(const uint8_t* data, const size_t n) {
    if (n > 4096) return fmemopen((void*)(uintptr_t)data, n, "rb");
    int fds[2];
    if (pipe(fds) != 0) return NULL;
    const int fits =
        fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0 && (n == 0 || write(fds[1], data, n) == (ssize_t)n);
    close(fds[1]);
    FILE* const f = fits ? fdopen(fds[0], "rb") : NULL;
    if (!f) close(fds[0]);
    return f;
}

/* One archive, three inputs (file, archive mid-file, live stream): same verdict and bytes. */
static int input_kinds_agree(FILE* f_reg, FILE* f_mid, FILE* f_out, const uint8_t* arc,
                             const size_t n, uint8_t* out[3], const size_t cap, const int n_threads,
                             const int exact, int64_t* verdict) {
    FILE* const f_live = open_live_stream(arc, n);
    int64_t r[3] = {-1, -1, -1};
    const int ready = f_live && refill_file(f_reg, 0, arc, n) && refill_file(f_mid, 777, arc, n);
    if (ready) {
        r[0] = decode_stream_into(f_reg, f_out, out[0], cap, n_threads);
        r[1] = decode_stream_into(f_mid, f_out, out[1], cap, n_threads);
        r[2] = decode_stream_into(f_live, f_out, out[2], cap, n_threads);
    }
    if (f_live) fclose(f_live);
    *verdict = r[0];

    // A corrupted input can fail in the reader or a worker first: only the sign must match.
    int same = ready;
    for (int k = 1; same && k < 3; k++) {
        same = exact || r[0] >= 0 ? r[k] == r[0] : r[k] < 0;
        if (same && r[0] > 0) same = memcmp(out[k], out[0], (size_t)r[0]) == 0;
    }
    if (!same)
        printf("Failed: %zu-byte archive, %d threads: file %lld, mid-file %lld, stream %lld\n", n,
               n_threads, (long long)r[0], (long long)r[1], (long long)r[2]);
    return same;
}

/* Progress callback: empties the input file under the decoder. */
static void shrink_input(const uint64_t done, const uint64_t total, const void* user_data) {
    (void)done;
    (void)total;
    (void)!ftruncate(fileno((FILE*)(uintptr_t)user_data), 0);
}

/* Progress callback: overwrites the input file past its header, once. */
typedef struct {
    FILE* f;
    const uint8_t* image;
    size_t size;
    int fired;
} rewrite_t;

static void rewrite_input(const uint64_t done, const uint64_t total, const void* user_data) {
    (void)done;
    (void)total;
    rewrite_t* const w = (rewrite_t*)(uintptr_t)user_data;
    if (w->fired++) return;
    (void)!pwrite(fileno(w->f), w->image + ZXC_FILE_HEADER_SIZE, w->size - ZXC_FILE_HEADER_SIZE,
                  ZXC_FILE_HEADER_SIZE);
}

#endif

/* A regular file and a live stream must agree on every archive: intact, cut at any
 * byte, or with any byte corrupted. */
int test_stream_input_kinds(void) {
    printf("=== TEST: Stream - file and live stream inputs agree ===\n");
#if !defined(_WIN32)
    const size_t sizes[] = {4096, 64 * 1024};
    int ok = 1;

    for (size_t s = 0; ok && s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        const size_t bs = sizes[s];
        const size_t n = 3 * bs + 5;
        const size_t cap = zxc_compress_bound(n);
        uint8_t* const src = malloc(n);
        uint8_t* const arc = malloc(cap + 1);
        uint8_t* const bad = malloc(cap + 1);
        uint8_t* out[3] = {malloc(n), malloc(n), malloc(n)};
        FILE* const f_reg = tmpfile();
        FILE* const f_mid = tmpfile();
        FILE* const f_out = tmpfile();
        ok = src && arc && bad && out[0] && out[1] && out[2] && f_reg && f_mid && f_out;

        // Periodic blocks: the archive fits a pipe.
        if (ok) gen_periodic_blocks(src, n, bs);

        for (int v = 0; ok && v < 2; v++) {
            const int64_t len = test_compress_cfg(src, n, arc, cap, 3, bs, !v, v);
            int64_t r = 0;
            if (len <= 0 || len > 2048) {
                printf("Failed: archive of %lld bytes\n", (long long)len);
                ok = 0;
                break;
            }
            const size_t alen = (size_t)len;

            // Intact, then one trailing byte.
            for (int t = 1; ok && t <= 3; t++)
                ok = input_kinds_agree(f_reg, f_mid, f_out, arc, alen, out, n, t, 1, &r) &&
                     r == (int64_t)n && memcmp(out[0], src, n) == 0;
            arc[alen] = 0;
            ok = ok && input_kinds_agree(f_reg, f_mid, f_out, arc, alen + 1, out, n, 2, 1, &r) &&
                 r == ZXC_ERROR_CORRUPT_DATA;

            // Cut at every byte.
            for (size_t t = 0; ok && t < alen; t++)
                ok = input_kinds_agree(f_reg, f_mid, f_out, arc, t, out, n, 2 + t % 2, 1, &r) &&
                     r < 0;

            // Every byte corrupted in turn.
            for (size_t t = 0; ok && t < alen; t++) {
                memcpy(bad, arc, alen);
                bad[t] ^= 0xFF;
                ok = input_kinds_agree(f_reg, f_mid, f_out, bad, alen, out, n, 2 + t % 2, 0, &r);
            }
        }

        free(src);
        free(arc);
        free(bad);
        for (int k = 0; k < 3; k++) free(out[k]);
        if (f_reg) fclose(f_reg);
        if (f_mid) fclose(f_mid);
        if (f_out) fclose(f_out);
    }
    if (!ok) return 0;
    printf("PASS\n\n");
#else
    printf("  [SKIP] needs POSIX pipes\n\n");
#endif
    return 1;
}

/* A file truncated during the decode is an I/O error, in the reader or in a worker. */
int test_stream_input_shrinks(void) {
    printf("=== TEST: Stream - input file truncated during the decode ===\n");
#if !defined(_WIN32)
    const size_t bs = 64 * 1024;
    const size_t n = 256 * bs;
    const size_t cap = zxc_compress_bound(n);
    uint8_t* const src = malloc(n);
    uint8_t* const arc = malloc(cap);
    int ok = src && arc;

    // Incompressible: far more blocks than the ring holds when the first job is reported.
    int64_t len = 0;
    if (ok) {
        gen_random_data(src, n);
        len = test_compress_cfg(src, n, arc, cap, 1, bs, 1, 0);
        ok = len >= (int64_t)n;
    }

    for (int n_threads = 1; ok && n_threads <= 4; n_threads++) {
        FILE* const f_arc = tmpfile();
        ok = f_arc && fwrite(arc, 1, (size_t)len, f_arc) == (size_t)len && fflush(f_arc) == 0;
        if (ok) {
            zxc_dctx* const dopts = dctx_for(n_threads, 1);
            zxc_dctx_set_progress(dopts, shrink_input, f_arc);
            fseek(f_arc, 0, SEEK_SET);
            // A read sees the cut (I/O error), or the reader meets the end (truncation).
            const int64_t r = zxc_stream_decompress(f_arc, NULL, dopts);
            zxc_free_dctx(dopts);
            ok = r == ZXC_ERROR_IO || r == ZXC_ERROR_SRC_TOO_SMALL;
            if (!ok) printf("Failed: %d threads -> %lld\n", n_threads, (long long)r);
        }
        if (f_arc) fclose(f_arc);
    }

    free(src);
    free(arc);
    if (!ok) return 0;
    printf("PASS\n\n");
#else
    printf("  [SKIP] needs POSIX ftruncate\n\n");
#endif
    return 1;
}

/* A file rewritten during the decode, after the reader measured its jobs: blocks that
 * overflow a job's output, or overrun its input, must fail within the buffers. */
int test_stream_input_rewritten(void) {
    printf("=== TEST: Stream - input file rewritten during the decode ===\n");
#if !defined(_WIN32)
    const size_t bs = 64 * 1024;
    const size_t n = 256 * bs;
    const size_t cap = zxc_compress_bound(n);
    uint8_t* const src = malloc(n);
    uint8_t* const arc = malloc(cap);
    uint8_t* const evil = malloc(cap);
    uint8_t* const tiny = malloc(zxc_compress_bound(bs));
    int ok = src && arc && evil && tiny;

    // No checksums: the tiny block is valid wherever it lands.
    int64_t len = 0;
    zxc_block_header_t bh = {0};
    size_t tiny_sz = 0;
    if (ok) {
        gen_periodic_blocks(src, bs, bs);
        ok = test_compress_cfg(src, bs, tiny, zxc_compress_bound(bs), 1, bs, 0, 0) > 0 &&
             zxc_read_block_header(tiny + ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &bh) ==
                 ZXC_OK;
        tiny_sz = ZXC_BLOCK_HEADER_SIZE + bh.comp_size;
        gen_random_data(src, n);
        len = test_compress_cfg(src, n, arc, cap, 1, bs, 0, 0);
        ok = ok && len >= (int64_t)n && tiny_sz < 1024;
    }

    for (int kind = 0; ok && kind < 2; kind++) {
        // Per job (four blocks): copies of the tiny block, or a last block claiming
        // 2000 bytes past the job's end.
        memcpy(evil, arc, (size_t)len);
        size_t off = ZXC_FILE_HEADER_SIZE;
        for (size_t b = 0; ok && b < 256; b += 4) {
            const size_t start = off;
            size_t last = off;
            for (int k = 0; ok && k < 4; k++) {
                ok = zxc_read_block_header(arc + off, ZXC_BLOCK_HEADER_SIZE, &bh) == ZXC_OK;
                last = off;
                off += ZXC_BLOCK_HEADER_SIZE + bh.comp_size;
            }
            if (kind == 0) {
                for (size_t o = start; ok && o < off; o++)
                    evil[o] = tiny[ZXC_FILE_HEADER_SIZE + (o - start) % tiny_sz];
            } else {
                bh.comp_size += 2000;
                ok = ok && zxc_write_block_header(evil + last, ZXC_BLOCK_HEADER_SIZE, &bh) > 0;
            }
        }

        for (int n_threads = 2; ok && n_threads <= 4; n_threads++) {
            FILE* const f_arc = tmpfile();
            ok = f_arc && fwrite(arc, 1, (size_t)len, f_arc) == (size_t)len && fflush(f_arc) == 0;
            if (ok) {
                rewrite_t w = {f_arc, evil, (size_t)len, 0};
                zxc_dctx* const dopts = dctx_for(n_threads, 0);
                zxc_dctx_set_progress(dopts, rewrite_input, &w);
                fseek(f_arc, 0, SEEK_SET);
                const int64_t r = zxc_stream_decompress(f_arc, NULL, dopts);
                zxc_free_dctx(dopts);
                ok = r < 0;
                if (!ok)
                    printf("Failed: rewrite %d, %d threads -> %lld\n", kind, n_threads,
                           (long long)r);
            }
            if (f_arc) fclose(f_arc);
        }
    }

    free(src);
    free(arc);
    free(evil);
    free(tiny);
    if (!ok) return 0;
    printf("PASS\n\n");
#else
    printf("  [SKIP] needs POSIX pwrite\n\n");
#endif
    return 1;
}

/* A file's small blocks travel in batches, a live stream's one by one: same bytes,
 * same verdict when cut or corrupted where a batch ends. */
int test_stream_block_batches(void) {
    printf("=== TEST: Stream - small blocks decoded in batches from a file ===\n");
    const size_t bs = 4096;
    const size_t n = 200 * bs + 5;
    const size_t cap = zxc_compress_bound(n);
    uint8_t* const src = malloc(n);
    uint8_t* const arc = malloc(cap);
    uint8_t* const bad = malloc(cap);
    uint8_t* out[3] = {malloc(n), malloc(n), malloc(n)};
    FILE* const f_reg = tmpfile();
    FILE* const f_mid = tmpfile();
    FILE* const f_out = tmpfile();
    int ok = src && arc && bad && out[0] && out[1] && out[2] && f_reg && f_mid && f_out;

    int64_t len = 0;
    if (ok) {
        gen_periodic_blocks(src, n, bs);
        len = test_compress_cfg(src, n, arc, cap, 3, bs, 1, 0);
        ok = len > 0 && fwrite(arc, 1, (size_t)len, f_reg) == (size_t)len;
    }

    // From a file: the right bytes, in fewer jobs than the 201 blocks.
    for (int t = 1; ok && t <= 4; t++) {
        zxc_dctx* const dopts = dctx_for(t, 1);
        zxc_dctx_set_progress(dopts, count_jobs, NULL);
        jobs_written = 0;
        fseek(f_reg, 0, SEEK_SET);
        fseek(f_out, 0, SEEK_SET);
        const int64_t r = zxc_stream_decompress(f_reg, f_out, dopts);
        zxc_free_dctx(dopts);
        fseek(f_out, 0, SEEK_SET);
        ok = r == (int64_t)n && fread(out[0], 1, n, f_out) == n && memcmp(out[0], src, n) == 0 &&
             jobs_written >= 1 && jobs_written < 201;
        if (!ok) printf("Failed: %d threads -> %lld in %d jobs\n", t, (long long)r, jobs_written);
    }

#if !defined(_WIN32)
    const size_t alen = (size_t)len;
    // From a live stream: one job per block, the same bytes.
    if (ok) {
        int64_t r = 0;
        zxc_dctx* const dopts = dctx_for(2, 1);
        zxc_dctx_set_progress(dopts, count_jobs, NULL);
        jobs_written = 0;
        FILE* const f_live = open_live_stream(arc, alen);
        ok = f_live && zxc_stream_decompress(f_live, NULL, dopts) == (int64_t)n &&
             jobs_written == 201;
        if (!ok) printf("Failed: %d jobs from the live stream\n", jobs_written);
        zxc_free_dctx(dopts);
        if (f_live) fclose(f_live);
        for (int t = 1; ok && t <= 4; t++)
            ok = input_kinds_agree(f_reg, f_mid, f_out, arc, alen, out, n, t, 1, &r) &&
                 r == (int64_t)n;
    }

    // Cut, then corrupted, around the first bytes of blocks 64 and 128.
    size_t off = ZXC_FILE_HEADER_SIZE;
    for (size_t b = 0; ok && b <= 128; b++) {
        if (b == 64 || b == 128) {
            for (size_t t = off - 12; ok && t <= off + 12; t++) {
                int64_t r = 0;
                ok = input_kinds_agree(f_reg, f_mid, f_out, arc, t, out, n, 1 + t % 3, 1, &r) &&
                     r < 0;
                memcpy(bad, arc, alen);
                bad[t] ^= 0xFF;
                ok = ok &&
                     input_kinds_agree(f_reg, f_mid, f_out, bad, alen, out, n, 1 + t % 3, 0, &r);
            }
        }
        zxc_block_header_t bh;
        ok = ok && zxc_read_block_header(arc + off, ZXC_BLOCK_HEADER_SIZE, &bh) == ZXC_OK;
        off += ZXC_BLOCK_HEADER_SIZE + bh.comp_size + ZXC_BLOCK_CHECKSUM_SIZE;
    }
#endif

    free(src);
    free(arc);
    free(bad);
    for (int k = 0; k < 3; k++) free(out[k]);
    if (f_reg) fclose(f_reg);
    if (f_mid) fclose(f_mid);
    if (f_out) fclose(f_out);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* The FILE* path carries ZXC_C_FAVOR_DECODE_SPEED to its workers: level 1 is
 * identical, levels 3 and 7 differ, grow by little and decode back. */
int test_stream_favor_decode_speed(void) {
    printf("=== TEST: Stream - ZXC_C_FAVOR_DECODE_SPEED reaches the workers ===\n");

    const size_t size = 512 * 1024;
    uint8_t* const input = malloc(size);
    uint8_t* const back = malloc(size);
    uint8_t* out[2] = {NULL, NULL};
    int ok = 0;
    if (!input || !back) goto done;
    gen_split_data(input, size);

    static const int levels[] = {1, 3, 7};
    for (size_t li = 0; li < sizeof(levels) / sizeof(levels[0]); li++) {
        long sz[2] = {0, 0};
        for (int k = 0; k < 2; k++) {
            FILE* const f_in = tmpfile();
            FILE* const f_comp = tmpfile();
            FILE* const f_dec = tmpfile();
            if (!f_in || !f_comp || !f_dec) {
                printf("  [SKIP] tmpfile failed\n");
                if (f_in) fclose(f_in);
                if (f_comp) fclose(f_comp);
                if (f_dec) fclose(f_dec);
                ok = 1;
                goto done;
            }
            fwrite(input, 1, size, f_in);
            fseek(f_in, 0, SEEK_SET);
            zxc_cctx* const co = cctx_for(2, levels[li], 0);
            if (!co || zxc_cctx_set_param(co, ZXC_C_FAVOR_DECODE_SPEED,
                                          k ? ZXC_SPLIT_BUDGET_DEFAULT : 0) != ZXC_OK) {
                printf("  [FAIL] context setup\n");
                zxc_free_cctx(co);
                fclose(f_in);
                fclose(f_comp);
                fclose(f_dec);
                goto done;
            }
            const int64_t c = zxc_stream_compress(f_in, f_comp, co);
            zxc_free_cctx(co);
            fseek(f_comp, 0, SEEK_END);
            sz[k] = ftell(f_comp);
            fseek(f_comp, 0, SEEK_SET);
            free(out[k]);
            out[k] = malloc((size_t)sz[k]);
            const int read_ok = out[k] && fread(out[k], 1, (size_t)sz[k], f_comp) == (size_t)sz[k];
            fseek(f_comp, 0, SEEK_SET);
            const int64_t d = zxc_stream_decompress(f_comp, f_dec, NULL);
            fseek(f_dec, 0, SEEK_SET);
            const int back_ok = d == (int64_t)size && fread(back, 1, size, f_dec) == size &&
                                memcmp(back, input, size) == 0;
            fclose(f_in);
            fclose(f_comp);
            fclose(f_dec);
            if (c <= 0 || !read_ok || !back_ok) {
                printf("  [FAIL] level %d, flag %d: compress %lld, decompress %lld\n", levels[li],
                       k, (long long)c, (long long)d);
                goto done;
            }
        }
        const int same = sz[0] == sz[1] && memcmp(out[0], out[1], (size_t)sz[0]) == 0;
        if (levels[li] <= 2 ? !same : same) {
            printf("  [FAIL] level %d: streams %s (%ld vs %ld bytes)\n", levels[li],
                   same ? "identical" : "differ", sz[0], sz[1]);
            goto done;
        }
        if (sz[1] > sz[0] + sz[0] / 5) {
            printf("  [FAIL] level %d: the flag grew the stream by more than 20%% (%ld -> %ld)\n",
                   levels[li], sz[0], sz[1]);
            goto done;
        }
        printf("  [PASS] level %d: %ld -> %ld bytes\n", levels[li], sz[0], sz[1]);
    }
    ok = 1;
    printf("PASS\n\n");

done:
    free(out[0]);
    free(out[1]);
    free(input);
    free(back);
    return ok;
}
