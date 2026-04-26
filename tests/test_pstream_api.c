/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Tests for the push-based streaming API (zxc_pstream.h).
 *
 * Coverage focuses on:
 *  - Round-trip correctness across patterns / sizes / chunk granularities.
 *  - Wire-compatibility with the buffer API (push-compressed blob must be
 *    decodable by zxc_decompress(), and vice-versa).
 *  - Pathological caller behaviour (one-byte chunks, empty input, tiny
 *    output buffers).
 *  - Error handling on truncated / corrupted streams.
 */

#include "test_common.h"

/* ---- helpers ---------------------------------------------------------- */

/* Push-compress src/src_size with given chunk sizes through the pstream
 * API and return a malloc'd blob (caller frees) of size *out_size.
 * Returns NULL on failure. */
static uint8_t* pstream_compress_in_chunks(const uint8_t* src, size_t src_size, size_t in_chunk,
                                           size_t out_chunk, int level, int checksum_enabled,
                                           size_t* out_size) {
    zxc_compress_opts_t opts = {0};
    opts.level = level;
    opts.checksum_enabled = checksum_enabled;
    zxc_cstream* cs = zxc_cstream_create(&opts);
    if (!cs) return NULL;

    size_t cap = 1024;
    size_t used = 0;
    uint8_t* blob = (uint8_t*)malloc(cap);
    uint8_t* obuf = (uint8_t*)malloc(out_chunk);
    if (!blob || !obuf) {
        free(blob);
        free(obuf);
        zxc_cstream_free(cs);
        return NULL;
    }

    /* Append helper */
#define APPEND_FROM(buf, n)                             \
    do {                                                \
        if (used + (n) > cap) {                         \
            while (used + (n) > cap) cap *= 2;          \
            uint8_t* nb = (uint8_t*)realloc(blob, cap); \
            if (!nb) {                                  \
                free(blob);                             \
                free(obuf);                             \
                zxc_cstream_free(cs);                   \
                return NULL;                            \
            }                                           \
            blob = nb;                                  \
        }                                               \
        memcpy(blob + used, (buf), (n));                \
        used += (n);                                    \
    } while (0)

    /* Phase 1: feed input in fixed chunks, draining as needed. */
    size_t off = 0;
    zxc_outbuf_t out = {obuf, out_chunk, 0};
    while (off < src_size) {
        const size_t n = (src_size - off) < in_chunk ? (src_size - off) : in_chunk;
        zxc_inbuf_t in = {src + off, n, 0};
        while (in.pos < in.size) {
            int64_t r = zxc_cstream_compress(cs, &out, &in);
            if (r < 0) {
                free(blob);
                free(obuf);
                zxc_cstream_free(cs);
                return NULL;
            }
            if (out.pos > 0) {
                APPEND_FROM(obuf, out.pos);
                out.pos = 0;
            }
        }
        off += n;
    }
    /* Phase 2: finalise. */
    int64_t pending;
    do {
        pending = zxc_cstream_end(cs, &out);
        if (pending < 0) {
            free(blob);
            free(obuf);
            zxc_cstream_free(cs);
            return NULL;
        }
        if (out.pos > 0) {
            APPEND_FROM(obuf, out.pos);
            out.pos = 0;
        }
    } while (pending > 0);

#undef APPEND_FROM
    free(obuf);
    zxc_cstream_free(cs);
    *out_size = used;
    return blob;
}

/* Push-decompress an entire blob in given chunk sizes; returns malloc'd
 * decoded buffer (caller frees) of size *out_size, or NULL on failure. */
static uint8_t* pstream_decompress_in_chunks(const uint8_t* src, size_t src_size, size_t in_chunk,
                                             size_t out_chunk, int checksum_enabled,
                                             size_t* out_size) {
    zxc_decompress_opts_t opts = {0};
    opts.checksum_enabled = checksum_enabled;
    zxc_dstream* ds = zxc_dstream_create(&opts);
    if (!ds) return NULL;

    size_t cap = 1024;
    size_t used = 0;
    uint8_t* dec = (uint8_t*)malloc(cap);
    uint8_t* obuf = (uint8_t*)malloc(out_chunk);
    if (!dec || !obuf) {
        free(dec);
        free(obuf);
        zxc_dstream_free(ds);
        return NULL;
    }

    /* Drive the loop until we have nothing more to feed *and* the decoder
     * makes no further progress (output drained, no input left to consume). */
    size_t off = 0;
    zxc_outbuf_t out = {obuf, out_chunk, 0};
    for (;;) {
        const size_t remaining = src_size - off;
        const size_t n = remaining < in_chunk ? remaining : in_chunk;
        zxc_inbuf_t in = {n ? src + off : NULL, n, 0};

        int64_t r = zxc_dstream_decompress(ds, &out, &in);
        if (r < 0) {
            free(dec);
            free(obuf);
            zxc_dstream_free(ds);
            return NULL;
        }
        if (out.pos > 0) {
            if (used + out.pos > cap) {
                while (used + out.pos > cap) cap *= 2;
                uint8_t* nb = (uint8_t*)realloc(dec, cap);
                if (!nb) {
                    free(dec);
                    free(obuf);
                    zxc_dstream_free(ds);
                    return NULL;
                }
                dec = nb;
            }
            memcpy(dec + used, obuf, out.pos);
            used += out.pos;
            out.pos = 0;
        }
        off += in.pos;
        /* Termination: no input left to feed and decoder produced nothing
         * AND consumed nothing -> it is either DONE or stalled. */
        if (off >= src_size && in.pos == 0 && r == 0) break;
    }
    /* Reject truncated streams: if we ran out of input but the parser never
     * reached the validated footer, treat it as failure. */
    if (!zxc_dstream_finished(ds)) {
        free(dec);
        free(obuf);
        zxc_dstream_free(ds);
        return NULL;
    }

    free(obuf);
    zxc_dstream_free(ds);
    *out_size = used;
    return dec;
}

/* End-to-end roundtrip with given chunk sizes; asserts the decoded blob
 * matches the input byte-for-byte.  Returns 1 on success. */
static int do_roundtrip(const char* label, const uint8_t* src, size_t size, size_t in_chunk,
                        size_t out_chunk, int level, int checksum_enabled) {
    size_t comp_size = 0;
    uint8_t* comp = pstream_compress_in_chunks(src, size, in_chunk, out_chunk, level,
                                               checksum_enabled, &comp_size);
    if (!comp) {
        printf("  [%s] compress failed\n", label);
        return 0;
    }
    size_t dec_size = 0;
    uint8_t* dec = pstream_decompress_in_chunks(comp, comp_size, in_chunk, out_chunk,
                                                checksum_enabled, &dec_size);
    if (!dec) {
        printf("  [%s] decompress failed (comp_size=%zu)\n", label, comp_size);
        free(comp);
        return 0;
    }
    if (dec_size != size || (size > 0 && memcmp(dec, src, size) != 0)) {
        printf("  [%s] mismatch (orig=%zu, dec=%zu)\n", label, size, dec_size);
        free(comp);
        free(dec);
        return 0;
    }
    free(comp);
    free(dec);
    return 1;
}

/* ---- tests ------------------------------------------------------------ */

int test_pstream_roundtrip_basic(void) {
    printf("=== TEST: PStream Roundtrip Basic (lz_data, 64 KiB) ===\n");
    const size_t size = 64 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    int ok = do_roundtrip("default 64KiB chunks", src, size, 64 * 1024, 64 * 1024, 3, 1);
    free(src);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_roundtrip_no_checksum(void) {
    printf("=== TEST: PStream Roundtrip (checksum disabled) ===\n");
    const size_t size = 80 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    int ok = do_roundtrip("no csum", src, size, 32 * 1024, 32 * 1024, 3, 0);
    free(src);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_roundtrip_levels(void) {
    printf("=== TEST: PStream Roundtrip across levels 1..5 ===\n");
    const size_t size = 70 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    int ok = 1;
    char label[32];
    for (int lvl = 1; lvl <= 5 && ok; lvl++) {
        snprintf(label, sizeof label, "level=%d csum=1", lvl);
        ok &= do_roundtrip(label, src, size, 16 * 1024, 16 * 1024, lvl, 1);
    }
    free(src);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_tiny_chunks(void) {
    printf("=== TEST: PStream tiny IO chunks (in=137, out=53) ===\n");
    /* Stresses the state machine with output buffers so small they force
     * partial drains in the middle of every block. */
    const size_t size = 40 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    int ok = do_roundtrip("tiny", src, size, 137, 53, 3, 1);
    free(src);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_drip_one_byte(void) {
    printf("=== TEST: PStream 1-byte input chunks (worst-case feeder) ===\n");
    const size_t size = 8 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    int ok = do_roundtrip("1B", src, size, 1, 4096, 3, 1);
    free(src);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_empty_input(void) {
    printf("=== TEST: PStream empty input -> valid empty file ===\n");
    size_t comp_size = 0;
    uint8_t* comp = pstream_compress_in_chunks(NULL, 0, 4096, 4096, 3, 1, &comp_size);
    if (!comp) {
        printf("compress NULL/0 failed\n");
        return 0;
    }
    /* Should at least be: file header (16) + EOF block (8) + footer (12) = 36 bytes. */
    if (comp_size < 36) {
        printf("expected >=36 bytes, got %zu\n", comp_size);
        free(comp);
        return 0;
    }
    size_t dec_size = 0;
    uint8_t* dec = pstream_decompress_in_chunks(comp, comp_size, 4096, 4096, 1, &dec_size);
    if (!dec) {
        printf("decompress failed\n");
        free(comp);
        return 0;
    }
    if (dec_size != 0) {
        printf("expected 0 decoded bytes, got %zu\n", dec_size);
        free(comp);
        free(dec);
        return 0;
    }
    free(comp);
    free(dec);
    printf("PASS (comp_size=%zu)\n\n", comp_size);
    return 1;
}

int test_pstream_large_random(void) {
    printf("=== TEST: PStream large mixed (1.5 MiB) ===\n");
    const size_t size = 1500 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    int ok = do_roundtrip("1.5MiB / level5", src, size, 13 * 1024, 7 * 1024, 5, 1);
    free(src);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_compatible_with_buffer_api(void) {
    printf("=== TEST: pstream-compressed blob decodable by zxc_decompress() ===\n");
    const size_t size = 100 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    size_t comp_size = 0;
    uint8_t* comp = pstream_compress_in_chunks(src, size, 8192, 8192, 3, 1, &comp_size);
    if (!comp) {
        free(src);
        return 0;
    }
    /* Decode with the one-shot buffer API. */
    uint8_t* dec = (uint8_t*)malloc(size);
    if (!dec) {
        free(src);
        free(comp);
        return 0;
    }
    zxc_decompress_opts_t dopts = {0};
    dopts.checksum_enabled = 1;
    int64_t dsz = zxc_decompress(comp, comp_size, dec, size, &dopts);
    int ok = (dsz == (int64_t)size && memcmp(dec, src, size) == 0);
    if (!ok) {
        printf("FAIL: dsz=%lld, expected %zu\n", (long long)dsz, size);
    }
    free(src);
    free(comp);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_decompress_compatible_with_buffer_api(void) {
    printf("=== TEST: buffer-API blob decodable by zxc_dstream ===\n");
    const size_t size = 100 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    /* Compress with the one-shot buffer API. */
    const uint64_t bound = zxc_compress_bound(size);
    uint8_t* comp = (uint8_t*)malloc((size_t)bound);
    if (!comp) {
        free(src);
        return 0;
    }
    zxc_compress_opts_t copts = {0};
    copts.level = 3;
    copts.checksum_enabled = 1;
    int64_t csz = zxc_compress(src, size, comp, (size_t)bound, &copts);
    if (csz <= 0) {
        free(src);
        free(comp);
        return 0;
    }
    /* Decode with the streaming push API (small chunks). */
    size_t dec_size = 0;
    uint8_t* dec = pstream_decompress_in_chunks(comp, (size_t)csz, 511, 7 * 1024, 1, &dec_size);
    int ok = (dec && dec_size == size && memcmp(dec, src, size) == 0);
    if (!ok) {
        printf("FAIL: dec_size=%zu, expected %zu\n", dec_size, size);
    }
    free(src);
    free(comp);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_invalid_args(void) {
    printf("=== TEST: PStream invalid arguments ===\n");
    /* NULL inputs must return ZXC_ERROR_NULL_INPUT, never crash. */
    if (zxc_cstream_compress(NULL, NULL, NULL) != ZXC_ERROR_NULL_INPUT) return 0;
    if (zxc_cstream_end(NULL, NULL) != ZXC_ERROR_NULL_INPUT) return 0;
    if (zxc_dstream_decompress(NULL, NULL, NULL) != ZXC_ERROR_NULL_INPUT) return 0;
    if (zxc_cstream_in_size(NULL) != 0) return 0;
    if (zxc_cstream_out_size(NULL) != 0) return 0;
    if (zxc_dstream_in_size(NULL) != 0) return 0;
    if (zxc_dstream_out_size(NULL) != 0) return 0;
    /* free(NULL) is a no-op, must not crash. */
    zxc_cstream_free(NULL);
    zxc_dstream_free(NULL);
    printf("PASS\n\n");
    return 1;
}

int test_pstream_truncated_input(void) {
    printf("=== TEST: PStream truncated input -> error ===\n");
    const size_t size = 64 * 1024;
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);
    size_t comp_size = 0;
    uint8_t* comp = pstream_compress_in_chunks(src, size, 4096, 4096, 3, 1, &comp_size);
    if (!comp || comp_size < 30) {
        free(src);
        free(comp);
        return 0;
    }
    /* Cut off the last 5 bytes (truncated footer). */
    const size_t trunc_size = comp_size - 5;
    size_t dec_size = 0;
    uint8_t* dec = pstream_decompress_in_chunks(comp, trunc_size, 1024, 1024, 1, &dec_size);
    /* Either the helper returns NULL (error during decode), or it returns a
     * partial buffer that doesn't match, in both cases, NOT a clean success. */
    int ok = (dec == NULL) || (dec_size != size);
    free(src);
    free(comp);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

int test_pstream_corrupted_magic(void) {
    printf("=== TEST: PStream corrupted file magic -> ZXC_ERROR_BAD_MAGIC ===\n");
    /* Hand-craft 16 bogus bytes of "file header". */
    uint8_t junk[16];
    for (int i = 0; i < 16; i++) junk[i] = (uint8_t)(0xAA ^ i);
    zxc_decompress_opts_t opts = {0};
    zxc_dstream* ds = zxc_dstream_create(&opts);
    if (!ds) return 0;
    uint8_t out_buf[64];
    zxc_outbuf_t out = {out_buf, sizeof out_buf, 0};
    zxc_inbuf_t in = {junk, sizeof junk, 0};
    int64_t r = zxc_dstream_decompress(ds, &out, &in);
    int ok = (r == ZXC_ERROR_BAD_MAGIC);
    if (!ok) printf("FAIL: expected ZXC_ERROR_BAD_MAGIC (-4), got %lld\n", (long long)r);
    /* Sticky error: subsequent call returns the same code. */
    zxc_inbuf_t empty = {NULL, 0, 0};
    if (ok && zxc_dstream_decompress(ds, &out, &empty) != ZXC_ERROR_BAD_MAGIC) {
        printf("FAIL: error not sticky\n");
        ok = 0;
    }
    zxc_dstream_free(ds);
    if (ok) printf("PASS\n\n");
    return ok;
}
