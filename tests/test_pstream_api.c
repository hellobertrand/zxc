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
    const zxc_compress_opts_t opts = {.level = level, .checksum_enabled = checksum_enabled};
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
            const int64_t r = zxc_cstream_compress(cs, &out, &in);
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
    const zxc_decompress_opts_t opts = {.checksum_enabled = checksum_enabled};
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

        const int64_t r = zxc_dstream_decompress(ds, &out, &in);
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
    const int ok = do_roundtrip("default 64KiB chunks", src, size, 64 * 1024, 64 * 1024, 3, 1);
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
    const int ok = do_roundtrip("no csum", src, size, 32 * 1024, 32 * 1024, 3, 0);
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
    const int ok = do_roundtrip("tiny", src, size, 137, 53, 3, 1);
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
    const int ok = do_roundtrip("1B", src, size, 1, 4096, 3, 1);
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
    const int ok = do_roundtrip("1.5MiB / level5", src, size, 13 * 1024, 7 * 1024, 5, 1);
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
    const zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
    const int64_t dsz = zxc_decompress(comp, comp_size, dec, size, &dopts);
    const int ok = (dsz == (int64_t)size && memcmp(dec, src, size) == 0);
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
    const zxc_compress_opts_t copts = {.level = 3, .checksum_enabled = 1};
    const int64_t csz = zxc_compress(src, size, comp, (size_t)bound, &copts);
    if (csz <= 0) {
        free(src);
        free(comp);
        return 0;
    }
    /* Decode with the streaming push API (small chunks). */
    size_t dec_size = 0;
    uint8_t* dec = pstream_decompress_in_chunks(comp, (size_t)csz, 511, 7 * 1024, 1, &dec_size);
    const int ok = (dec && dec_size == size && memcmp(dec, src, size) == 0);
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
    const int ok = (dec == NULL) || (dec_size != size);
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
    const zxc_decompress_opts_t opts = {0};
    zxc_dstream* ds = zxc_dstream_create(&opts);
    if (!ds) return 0;
    uint8_t out_buf[64];
    zxc_outbuf_t out = {out_buf, sizeof out_buf, 0};
    zxc_inbuf_t in = {junk, sizeof junk, 0};
    const int64_t r = zxc_dstream_decompress(ds, &out, &in);
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

/* Decompress a SEEKABLE archive through the pstream API: after the EOF block
 * the decoder peeks 8 bytes, recognises a SEK block, and skips its payload
 * in DS_DRAIN_SEK_PAYLOAD before consuming the file footer. */
int test_pstream_decode_seekable_archive(void) {
    printf("=== TEST: PStream decodes seekable archive (DS_DRAIN_SEK_PAYLOAD) ===\n");
    const size_t size = 96 * 1024; /* > one default block to force >1 SEK entry */
    uint8_t* src = (uint8_t*)malloc(size);
    if (!src) return 0;
    gen_lz_data(src, size);

    /* Compress with seekable=1 via the buffer API (pstream cstream forces
     * seekable=0, so we need another producer to emit a SEK block). */
    const uint64_t bound = zxc_compress_bound(size);
    uint8_t* comp = (uint8_t*)malloc((size_t)bound);
    if (!comp) {
        free(src);
        return 0;
    }
    const zxc_compress_opts_t copts = {
        .level = 3, .checksum_enabled = 1, .seekable = 1, .block_size = 32 * 1024};
    const int64_t comp_size = zxc_compress(src, size, comp, (size_t)bound, &copts);
    if (comp_size <= 0) {
        printf("FAIL: zxc_compress(seekable=1) returned %lld\n", (long long)comp_size);
        free(src);
        free(comp);
        return 0;
    }

    /* Drive the seekable blob through zxc_dstream_decompress and check that
     * decoded bytes match the source. We feed the input in 4 KB chunks and
     * receive the output via 8 KB chunks: this stresses the SEK skip across
     * multiple calls (DS_DRAIN_SEK_PAYLOAD returns when the input is exhausted
     * mid-skip). */
    const zxc_decompress_opts_t dopts = {.checksum_enabled = 1};
    zxc_dstream* ds = zxc_dstream_create(&dopts);
    if (!ds) {
        free(src);
        free(comp);
        return 0;
    }
    uint8_t* dec = (uint8_t*)malloc(size);
    /* Zero-initialised: the decoder writes the first `out.pos` bytes per call
     * but cppcheck cannot see through the pointer chain in zxc_outbuf_t. */
    uint8_t out_chunk[8192] = {0};
    if (!dec) {
        zxc_dstream_free(ds);
        free(src);
        free(comp);
        return 0;
    }
    size_t dec_used = 0;
    size_t in_off = 0;
    const size_t in_step = 4096;
    int ok = 1;
    while (in_off < (size_t)comp_size && !zxc_dstream_finished(ds)) {
        const size_t n =
            ((size_t)comp_size - in_off) < in_step ? ((size_t)comp_size - in_off) : in_step;
        zxc_inbuf_t in = {comp + in_off, n, 0};
        zxc_outbuf_t out = {out_chunk, sizeof out_chunk, 0};
        const int64_t r = zxc_dstream_decompress(ds, &out, &in);
        if (r < 0) {
            printf("FAIL: zxc_dstream_decompress returned %lld\n", (long long)r);
            ok = 0;
            break;
        }
        if (dec_used + out.pos > size) {
            printf("FAIL: decoded bytes exceed source size\n");
            ok = 0;
            break;
        }
        memcpy(dec + dec_used, out_chunk, out.pos);
        dec_used += out.pos;
        in_off += in.pos;
        if (in.pos == 0 && out.pos == 0) break; /* no progress */
    }
    if (ok && !zxc_dstream_finished(ds)) {
        printf("FAIL: decoder did not finalise after consuming seekable input\n");
        ok = 0;
    }
    if (ok && (dec_used != size || memcmp(dec, src, size) != 0)) {
        printf("FAIL: decoded payload does not match source\n");
        ok = 0;
    }
    zxc_dstream_free(ds);
    free(src);
    free(comp);
    free(dec);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* Calling _compress() after _end() has started transitioning to a drain-tail
 * state must return ZXC_ERROR_NULL_INPUT. Covers the
 * `case CS_DRAIN_LAST/CS_DRAIN_EOF/CS_DRAIN_FOOTER/CS_ERRORED:` branch in
 * zxc_cstream_compress(). */
int test_pstream_compress_after_end_rejected(void) {
    printf("=== TEST: PStream _compress() after _end() -> ZXC_ERROR_NULL_INPUT ===\n");
    const zxc_compress_opts_t opts = {.level = 3};
    zxc_cstream* cs = zxc_cstream_create(&opts);
    if (!cs) return 0;

    /* Feed a few bytes so _end has actual residual data to flush. */
    uint8_t src[64];
    for (size_t i = 0; i < sizeof src; i++) src[i] = (uint8_t)i;
    uint8_t obuf[1024];
    zxc_inbuf_t in = {src, sizeof src, 0};
    zxc_outbuf_t out = {obuf, sizeof obuf, 0};
    if (zxc_cstream_compress(cs, &out, &in) < 0) {
        zxc_cstream_free(cs);
        return 0;
    }

    /* Tiny output buffer so _end returns >0 (still pending) and the stream
     * is parked in CS_DRAIN_LAST / CS_DRAIN_EOF / CS_DRAIN_FOOTER. */
    uint8_t tiny[4];
    zxc_outbuf_t tiny_out = {tiny, sizeof tiny, 0};
    const int64_t pending = zxc_cstream_end(cs, &tiny_out);
    if (pending <= 0) {
        printf("FAIL: expected _end() to return >0 with tiny output, got %lld\n",
               (long long)pending);
        zxc_cstream_free(cs);
        return 0;
    }

    /* Now state contains {CS_DRAIN_LAST, CS_DRAIN_EOF, CS_DRAIN_FOOTER}. _compress
     * must reject. */
    zxc_inbuf_t more = {src, sizeof src, 0};
    zxc_outbuf_t out2 = {obuf, sizeof obuf, 0};
    const int64_t r = zxc_cstream_compress(cs, &out2, &more);
    const int ok = (r == ZXC_ERROR_NULL_INPUT);
    if (!ok) printf("FAIL: expected ZXC_ERROR_NULL_INPUT (-12), got %lld\n", (long long)r);
    zxc_cstream_free(cs);
    if (ok) printf("PASS\n\n");
    return ok;
}

/* When _compress() fills a block but the caller's output buffer is too small
 * to drain it in one call, the next _compress() resumes at CS_DRAIN_BLOCK.
 * This test exercises that resume path inside zxc_cstream_compress (distinct
 * from the CS_DRAIN_BLOCK case in _end). */
int test_pstream_compress_drain_block_resume(void) {
    printf("=== TEST: PStream _compress() resumes CS_DRAIN_BLOCK across calls ===\n");
    /* Small block size so we trigger the block boundary quickly. */
    const zxc_compress_opts_t opts = {.level = 3, .block_size = 4096};
    zxc_cstream* cs = zxc_cstream_create(&opts);
    if (!cs) return 0;

    /* Enough input for one full block (+ a little) but no more. */
    const size_t src_size = 4096 + 256;
    uint8_t* src = (uint8_t*)malloc(src_size);
    if (!src) {
        zxc_cstream_free(cs);
        return 0;
    }
    gen_lz_data(src, src_size);

    /* Output buffer smaller than one compressed block: forces partial drains
     * and re-entries into CS_DRAIN_BLOCK. Zero-initialised to silence the
     * cppcheck false-positive that fires when an uninit array's address is
     * stored in zxc_outbuf_t.dst. */
    uint8_t obuf[37] = {0};
    zxc_inbuf_t in = {src, src_size, 0};
    int saw_pending_drain = 0;
    int ok = 1;
    /* Drive the loop until input is exhausted; if any call returns >0 it
     * means the next call will hit CS_DRAIN_BLOCK in the switch. */
    while (in.pos < in.size) {
        zxc_outbuf_t out = {obuf, sizeof obuf, 0};
        const int64_t r = zxc_cstream_compress(cs, &out, &in);
        if (r < 0) {
            printf("FAIL: _compress returned %lld\n", (long long)r);
            ok = 0;
            break;
        }
        if (r > 0) saw_pending_drain = 1;
    }
    if (ok && !saw_pending_drain) {
        printf("FAIL: expected at least one >0 return to exercise CS_DRAIN_BLOCK resume\n");
        ok = 0;
    }
    zxc_cstream_free(cs);
    free(src);
    if (ok) printf("PASS\n\n");
    return ok;
}
