/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fuzz_pstream.c
 * @brief Fuzzer for the push-based streaming API (zxc_cstream / zxc_dstream).
 *
 * The push API is a state machine driven entirely by caller-supplied byte
 * slices: each call may fully consume input, partially consume it, fill the
 * output buffer mid-block, or hit an error.  These resumption paths are
 * unique to pstream and are NOT exercised by fuzz_decompress.c (which feeds
 * the whole blob in a single call).
 *
 * Strategy: derive an input-chunk size and an output-drain size from the
 * fuzzer header bytes so libFuzzer explores byte content AND chunk
 * boundaries jointly.  Phases:
 *
 *  1. Compress fuzzed data via zxc_cstream_*, draining the output in small
 *     chunks (forces re-entry into the compressor state machine).
 *  2. Decompress the result via zxc_dstream_decompress, feeding the input
 *     one chunk at a time, again with a small output drain buffer.  Assert
 *     bit-exact roundtrip.
 *  3. Feed the *raw* fuzzer input directly to a fresh dstream, varying chunk
 *     sizes - this is the parser/state-machine fuzzing surface that catches
 *     malformed-frame, truncation, and sticky-error bugs.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_pstream.h"

/* Cap aligned with the maximum supported block size (2 MiB). */
#define FUZZ_PSTREAM_MAX_INPUT (2 << 20) /* 2 MiB */

/* Tiny output buffer: forces the compressor / decompressor into multi-round
 * draining, which is precisely the resumption code we want to fuzz. */
#define FUZZ_PSTREAM_OUT_CAP 256

static size_t derive_chunk_size(uint8_t b) {
    /* Map [0..255] -> [1..512] with a non-linear distribution biased toward
     * small values (small chunks stress the state machine the most). */
    if (b == 0) return 1;
    if (b < 64) return (size_t)b;            /* 1..63 */
    if (b < 192) return (size_t)b * 2;       /* 128..382 */
    return (size_t)b + 256;                  /* 448..511 */
}

/* ---------------------------------------------------------------- *
 *  Decompress `comp` into `out`, feeding input in chunks of `in_chunk` and
 *  draining output in chunks of FUZZ_PSTREAM_OUT_CAP.
 *
 *  Returns the total number of decompressed bytes written, or a negative
 *  zxc error code on failure.  Stops early (no error) if the output buffer
 *  is exhausted before the stream is finished.
 * ---------------------------------------------------------------- */
static int64_t drip_decompress(const uint8_t* comp, size_t csize, uint8_t* out, size_t out_cap,
                               size_t in_chunk, int checksum_enabled) {
    zxc_decompress_opts_t opts = {.checksum_enabled = checksum_enabled};
    zxc_dstream* ds = zxc_dstream_create(&opts);
    if (!ds) return -1;

    size_t in_pos = 0;
    size_t out_total = 0;

    while (in_pos < csize || !zxc_dstream_finished(ds)) {
        if (out_total >= out_cap) {
            /* Caller's buffer full - stop without asserting. */
            zxc_dstream_free(ds);
            return (int64_t)out_total;
        }
        const size_t feed = (csize - in_pos < in_chunk) ? (csize - in_pos) : in_chunk;
        size_t window = out_cap - out_total;
        if (window > FUZZ_PSTREAM_OUT_CAP) window = FUZZ_PSTREAM_OUT_CAP;
        zxc_inbuf_t in = {.src = comp + in_pos, .size = feed, .pos = 0};
        zxc_outbuf_t ob = {.dst = out + out_total, .size = window, .pos = 0};

        const size_t before_in = in.pos;
        const size_t before_out = ob.pos;
        const int64_t r = zxc_dstream_decompress(ds, &ob, &in);
        if (r < 0) {
            zxc_dstream_free(ds);
            return r;
        }

        out_total += ob.pos;
        in_pos += in.pos;

        /* No progress and no more input: prevent infinite loop on truncation. */
        if (in.pos == before_in && ob.pos == before_out && in_pos == csize) break;
    }

    zxc_dstream_free(ds);
    return (int64_t)out_total;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 3) return 0;

    /* Save raw input for the parser-fuzzing phase. */
    const uint8_t* const raw_data = data;
    const size_t raw_size = size;

    /* Header bytes drive pstream-specific axes. data[0] is reserved (the
     * compression level is intentionally fixed to 1, see copts below); it
     * is still consumed so corpus byte offsets stay stable. */
    const int checksum_enabled = data[1] & 1;
    const size_t in_chunk = derive_chunk_size(data[2]);
    data += 3;
    size -= 3;

    if (size == 0 || size > FUZZ_PSTREAM_MAX_INPUT) return 0;

    /* Persistent buffers - reused across iterations to reduce allocator
     * pressure (same pattern as fuzz_seekable.c). */
    static uint8_t* comp_buf = NULL;
    static size_t comp_cap = 0;
    static uint8_t* round_buf = NULL;
    static size_t round_cap = 0;

    /* ------------------------------------------------------------------ */
    /* Phase 1: Push compression in chunks                                */
    /* ------------------------------------------------------------------ */
    /* Level forced to 1: pstream's state-machine surface (framing, drain,
     * resumption, sticky errors) is independent of compression strength,
     * and buffer-API fuzzers (roundtrip, decompress) already exercise the
     * level dimension. Cheaper compression = more iterations/sec. */
    zxc_compress_opts_t copts = {
        .level = 1,
        .checksum_enabled = checksum_enabled,
    };
    zxc_cstream* cs = zxc_cstream_create(&copts);
    if (!cs) return 0;

    /* Sized so the compressor never runs out - bound is a safe upper limit. */
    /* Use the public buffer-API bound function via a local extern declaration
     * to avoid pulling zxc_buffer.h (kept narrow on purpose). */
    extern uint64_t zxc_compress_bound(size_t input_size);
    const uint64_t bound64 = zxc_compress_bound(size);
    if (bound64 == 0 || bound64 > SIZE_MAX) {
        zxc_cstream_free(cs);
        return 0;
    }
    const size_t bound = (size_t)bound64;
    if (bound > comp_cap) {
        void* nb = realloc(comp_buf, bound);
        if (!nb) {
            zxc_cstream_free(cs);
            return 0;
        }
        comp_buf = (uint8_t*)nb;
        comp_cap = bound;
    }

    size_t comp_len = 0;
    size_t in_pos = 0;

    /* Feed input in `in_chunk`-sized slices. */
    while (in_pos < size) {
        const size_t feed = (size - in_pos < in_chunk) ? (size - in_pos) : in_chunk;
        zxc_inbuf_t in = {.src = data + in_pos, .size = feed, .pos = 0};

        /* Drain repeatedly until this slice is fully consumed. */
        for (;;) {
            size_t window = bound - comp_len;
            if (window > FUZZ_PSTREAM_OUT_CAP) window = FUZZ_PSTREAM_OUT_CAP;
            if (window == 0) {
                /* Bound exhausted - shouldn't happen with zxc_compress_bound. */
                zxc_cstream_free(cs);
                return 0;
            }
            zxc_outbuf_t ob = {.dst = comp_buf + comp_len, .size = window, .pos = 0};
            const int64_t r = zxc_cstream_compress(cs, &ob, &in);
            if (r < 0) {
                zxc_cstream_free(cs);
                return 0;
            }
            comp_len += ob.pos;
            if (in.pos == in.size && r == 0) break;
        }
        in_pos += in.pos;
    }

    /* Finalize: zxc_cstream_end may report >0 pending bytes; drain until 0. */
    for (;;) {
        size_t window = bound - comp_len;
        if (window > FUZZ_PSTREAM_OUT_CAP) window = FUZZ_PSTREAM_OUT_CAP;
        if (window == 0) {
            zxc_cstream_free(cs);
            return 0;
        }
        zxc_outbuf_t ob = {.dst = comp_buf + comp_len, .size = window, .pos = 0};
        const int64_t pending = zxc_cstream_end(cs, &ob);
        if (pending < 0) {
            zxc_cstream_free(cs);
            return 0;
        }
        comp_len += ob.pos;
        if (pending == 0) break;
    }

    zxc_cstream_free(cs);

    /* ------------------------------------------------------------------ */
    /* Phase 2: Push decompression with chunked feeding + roundtrip       */
    /* ------------------------------------------------------------------ */
    if (size > round_cap) {
        void* nb = realloc(round_buf, size);
        if (!nb) return 0;
        round_buf = (uint8_t*)nb;
        round_cap = size;
    }

    const int64_t dsize =
        drip_decompress(comp_buf, comp_len, round_buf, size, in_chunk, checksum_enabled);
    if (dsize >= 0) {
        assert((size_t)dsize == size);
        assert(memcmp(data, round_buf, size) == 0);
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3: Parser fuzzing - feed raw input directly                  */
    /*                                                                    */
    /* This is where malformed-frame and truncation bugs live.  We don't  */
    /* care about the result, only that no UB / crash occurs.             */
    /* ------------------------------------------------------------------ */
    if (raw_size > 0) {
        /* Output is discarded - a single small stack buffer suffices and
         * keeps the parser's resumption code (small ob.size) under fuzz. */
        uint8_t scratch[FUZZ_PSTREAM_OUT_CAP];
        const size_t parser_chunk = derive_chunk_size(raw_data[raw_size - 1]);

        zxc_decompress_opts_t opts = {.checksum_enabled = checksum_enabled};
        zxc_dstream* ds = zxc_dstream_create(&opts);
        if (ds) {
            size_t pos = 0;
            int rounds = 0;
            while (pos < raw_size && rounds < 1024) {
                const size_t feed =
                    (raw_size - pos < parser_chunk) ? (raw_size - pos) : parser_chunk;
                zxc_inbuf_t in = {.src = raw_data + pos, .size = feed, .pos = 0};
                zxc_outbuf_t ob = {.dst = scratch, .size = sizeof scratch, .pos = 0};
                const size_t before_in = in.pos;
                const size_t before_out = ob.pos;
                const int64_t r = zxc_dstream_decompress(ds, &ob, &in);
                pos += in.pos;
                if (r < 0) break;
                if (in.pos == before_in && ob.pos == before_out) break;
                rounds++;
            }
            zxc_dstream_free(ds);
        }
    }

    return 0;
}
