/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Push-based, single-threaded streaming driver.  See zxc_pstream.h for the
 * public contract.  The implementation composes the public block API
 * (zxc_compress_block / zxc_decompress_block) with the public sans-IO header
 * helpers (zxc_write_file_header / footer, zxc_read_*); the only internal
 * dependency is on shared constants and the global-hash combine inline,
 * pulled from zxc_internal.h.
 */

#include <stdlib.h>
#include <string.h>

#include "zxc_buffer.h" /* zxc_create_cctx etc. */
#include "zxc_constants.h"
#include "zxc_error.h"    /* ZXC_OK, ZXC_ERROR_* */
#include "zxc_internal.h" /* ZXC_BLOCK_*, ZXC_*_SIZE, zxc_hash_combine_rotate, zxc_le32 */
#include "zxc_sans_io.h"
#include "zxc_pstream.h"

/* ===================================================================== */
/*  Compression                                                          */
/* ===================================================================== */

typedef enum {
    CS_INIT = 0,     /* nothing emitted yet                                       */
    CS_DRAIN_HEADER, /* draining file header -> goes to ACCUMULATE                 */
    CS_ACCUMULATE,   /* copy input into block accumulator                         */
    CS_DRAIN_BLOCK,  /* draining a compressed data block -> back to ACCUMULATE     */
    CS_DRAIN_LAST,   /* draining the final partial block (in _end) -> goes to EOF  */
    CS_DRAIN_EOF,    /* draining EOF block -> goes to FOOTER                       */
    CS_DRAIN_FOOTER, /* draining file footer -> goes to DONE                       */
    CS_DONE,
    CS_ERRORED
} cstream_state_t;

struct zxc_cstream_s {
    zxc_compress_opts_t opts;
    zxc_cctx* cctx;
    size_t block_size;

    /* Input accumulator: one full block */
    uint8_t* in_block;
    size_t in_used;

    /* Pending output staging: file header / one compressed block / EOF / footer */
    uint8_t* pending;
    size_t pending_cap; /* allocated capacity                                    */
    size_t pending_len; /* total bytes in pending [0..pending_cap]               */
    size_t pending_pos; /* bytes already copied to caller out [0..pending_len]   */

    /* Bookkeeping for footer */
    uint64_t total_in;
    uint32_t global_hash;

    cstream_state_t state;
    int error_code; /* sticky */
};

/* Latches a sticky error on the compression stream: stores `code` in
 * cs->error_code, transitions the state to CS_ERRORED, and returns `code`.
 * Once errored, subsequent _compress / _end calls return the same code
 * without performing further work. */
static int cs_set_error(zxc_cstream* cs, const int code) {
    cs->error_code = code;
    cs->state = CS_ERRORED;
    return code;
}

/* Compress the contents of cs->in_block into cs->pending and update bookkeeping.
 * cs->in_used must be > 0.  Returns ZXC_OK on success, negative on failure. */
static int cs_compress_one_block(zxc_cstream* cs) {
    const uint64_t bound = zxc_compress_block_bound(cs->in_used);
    // LCOV_EXCL_START
    if (UNLIKELY(bound == 0 || bound > SIZE_MAX)) return ZXC_ERROR_OVERFLOW;
    if (UNLIKELY(bound > cs->pending_cap)) {
        uint8_t* nb = (uint8_t*)realloc(cs->pending, (size_t)bound);
        if (UNLIKELY(!nb)) return ZXC_ERROR_MEMORY;
        cs->pending = nb;
        cs->pending_cap = (size_t)bound;
    }
    // LCOV_EXCL_STOP
    const int64_t csize = zxc_compress_block(cs->cctx, cs->in_block, cs->in_used, cs->pending,
                                             cs->pending_cap, &cs->opts);
    if (UNLIKELY(csize < 0)) return (int)csize;  // LCOV_EXCL_LINE

    cs->pending_len = (size_t)csize;
    cs->pending_pos = 0;
    cs->total_in += cs->in_used;
    cs->in_used = 0;

    /* If checksums are on, the block trailer is the last 4 bytes of pending;
     * fold it into the rolling global hash. */
    if (cs->opts.checksum_enabled && cs->pending_len >= 4) {
        const uint32_t bh = zxc_le32(cs->pending + cs->pending_len - 4);
        cs->global_hash = zxc_hash_combine_rotate(cs->global_hash, bh);
    }
    return ZXC_OK;
}

/* Copy from cs->pending[pending_pos..pending_len) into out.  Returns true once
 * the pending buffer is fully drained. */
static int cs_drain_pending(zxc_cstream* cs, zxc_outbuf_t* out) {
    const size_t avail_out = out->size - out->pos;
    const size_t avail_pen = cs->pending_len - cs->pending_pos;
    const size_t n = avail_out < avail_pen ? avail_out : avail_pen;
    if (n) {
        ZXC_MEMCPY((uint8_t*)out->dst + out->pos, cs->pending + cs->pending_pos, n);
        out->pos += n;
        cs->pending_pos += n;
    }
    return cs->pending_pos == cs->pending_len;
}

zxc_cstream* zxc_cstream_create(const zxc_compress_opts_t* opts) {
    zxc_cstream* cs = (zxc_cstream*)calloc(1, sizeof(*cs));
    if (UNLIKELY(!cs)) return NULL;  // LCOV_EXCL_LINE

    if (opts) cs->opts = *opts;
    if (cs->opts.level == 0) cs->opts.level = ZXC_LEVEL_DEFAULT;
    if (cs->opts.block_size == 0) cs->opts.block_size = ZXC_BLOCK_SIZE_DEFAULT;
    /* n_threads is ignored on this single-threaded path. */
    cs->opts.n_threads = 0;
    cs->opts.progress_cb = NULL;
    cs->opts.user_data = NULL;
    cs->opts.seekable = 0;
    cs->block_size = cs->opts.block_size;

    cs->cctx = zxc_create_cctx(&cs->opts);
    // LCOV_EXCL_START
    if (UNLIKELY(!cs->cctx)) {
        free(cs);
        return NULL;
    }
    cs->in_block = (uint8_t*)malloc(cs->block_size);
    if (UNLIKELY(!cs->in_block)) {
        zxc_free_cctx(cs->cctx);
        free(cs);
        return NULL;
    }
    // LCOV_EXCL_STOP
    /* Pre-size pending so the file header path never needs realloc. */
    cs->pending_cap =
        ZXC_FILE_HEADER_SIZE > ZXC_FILE_FOOTER_SIZE ? ZXC_FILE_HEADER_SIZE : ZXC_FILE_FOOTER_SIZE;
    cs->pending = (uint8_t*)malloc(cs->pending_cap);
    // LCOV_EXCL_START
    if (UNLIKELY(!cs->pending)) {
        free(cs->in_block);
        zxc_free_cctx(cs->cctx);
        free(cs);
        return NULL;
    }
    // LCOV_EXCL_STOP
    cs->state = CS_INIT;
    return cs;
}

/* Stages the file header into pending.  Returns ZXC_OK or negative on error. */
static int cs_stage_file_header(zxc_cstream* cs) {
    const int w = zxc_write_file_header(cs->pending, cs->pending_cap, cs->block_size,
                                        cs->opts.checksum_enabled);
    if (UNLIKELY(w < 0)) return w;  // LCOV_EXCL_LINE
    cs->pending_len = (size_t)w;
    cs->pending_pos = 0;
    return ZXC_OK;
}

/* Stages an EOF block (8 bytes) into pending. */
static int cs_stage_eof(zxc_cstream* cs) {
    // LCOV_EXCL_START
    if (UNLIKELY(ZXC_BLOCK_HEADER_SIZE > cs->pending_cap)) {
        uint8_t* nb = (uint8_t*)realloc(cs->pending, ZXC_BLOCK_HEADER_SIZE);
        if (UNLIKELY(!nb)) return ZXC_ERROR_MEMORY;
        cs->pending = nb;
        cs->pending_cap = ZXC_BLOCK_HEADER_SIZE;
    }
    // LCOV_EXCL_STOP
    const zxc_block_header_t eof = {
        .block_type = (uint8_t)ZXC_BLOCK_EOF,
        .block_flags = 0,
        .reserved = 0,
        .header_crc = 0,
        .comp_size = 0,
    };
    const int w = zxc_write_block_header(cs->pending, cs->pending_cap, &eof);
    if (UNLIKELY(w < 0)) return w;  // LCOV_EXCL_LINE
    cs->pending_len = (size_t)w;
    cs->pending_pos = 0;
    return ZXC_OK;
}

/* Stages the file footer (12 bytes) into pending. */
static int cs_stage_footer(zxc_cstream* cs) {
    // LCOV_EXCL_START
    if (UNLIKELY(ZXC_FILE_FOOTER_SIZE > cs->pending_cap)) {
        uint8_t* nb = (uint8_t*)realloc(cs->pending, ZXC_FILE_FOOTER_SIZE);
        if (UNLIKELY(!nb)) return ZXC_ERROR_MEMORY;
        cs->pending = nb;
        cs->pending_cap = ZXC_FILE_FOOTER_SIZE;
    }
    // LCOV_EXCL_STOP
    const int w = zxc_write_file_footer(cs->pending, cs->pending_cap, cs->total_in, cs->global_hash,
                                        cs->opts.checksum_enabled);
    if (UNLIKELY(w < 0)) return w;  // LCOV_EXCL_LINE
    cs->pending_len = (size_t)w;
    cs->pending_pos = 0;
    return ZXC_OK;
}

void zxc_cstream_free(zxc_cstream* cs) {
    if (!cs) return;
    free(cs->pending);
    free(cs->in_block);
    zxc_free_cctx(cs->cctx);
    free(cs);
}

size_t zxc_cstream_in_size(const zxc_cstream* cs) { return cs ? cs->block_size : 0; }

size_t zxc_cstream_out_size(const zxc_cstream* cs) {
    if (!cs) return 0;
    const uint64_t b = zxc_compress_block_bound(cs->block_size);
    return (b == 0 || b > SIZE_MAX) ? cs->block_size : (size_t)b;
}

int64_t zxc_cstream_compress(zxc_cstream* cs, zxc_outbuf_t* out, zxc_inbuf_t* in) {
    if (UNLIKELY(!cs || !out || !in || in->pos > in->size || out->pos > out->size ||
                 (in->size > in->pos && !in->src) || (out->size > out->pos && !out->dst) ||
                 cs->state == CS_DONE)) {
        return ZXC_ERROR_NULL_INPUT;
    }
    if (UNLIKELY(cs->state == CS_ERRORED)) return cs->error_code;

    for (;;) {
        switch (cs->state) {
            case CS_INIT: {
                const int rc = cs_stage_file_header(cs);
                if (UNLIKELY(rc < 0)) return cs_set_error(cs, rc);  // LCOV_EXCL_LINE
                cs->state = CS_DRAIN_HEADER;
                break;
            }

            case CS_DRAIN_HEADER: {
                if (!cs_drain_pending(cs, out)) return (int64_t)(cs->pending_len - cs->pending_pos);
                cs->state = CS_ACCUMULATE;
                break;
            }

            case CS_DRAIN_BLOCK: {
                if (!cs_drain_pending(cs, out)) return (int64_t)(cs->pending_len - cs->pending_pos);
                cs->state = CS_ACCUMULATE;
                break;
            }

            case CS_ACCUMULATE: {
                const size_t avail_in = in->size - in->pos;
                const size_t room = cs->block_size - cs->in_used;
                const size_t n = avail_in < room ? avail_in : room;
                if (n) {
                    ZXC_MEMCPY(cs->in_block + cs->in_used, (const uint8_t*)in->src + in->pos, n);
                    in->pos += n;
                    cs->in_used += n;
                }

                if (cs->in_used == cs->block_size) {
                    const int rc = cs_compress_one_block(cs);
                    if (UNLIKELY(rc < 0)) return cs_set_error(cs, rc);  // LCOV_EXCL_LINE
                    cs->state = CS_DRAIN_BLOCK;
                    break;
                }
                /* Block not yet full either in is empty or we made no progress. */
                return 0;
            }

            case CS_DRAIN_LAST:
            case CS_DRAIN_EOF:
            case CS_DRAIN_FOOTER:
            case CS_DONE:
            case CS_ERRORED:
                /* These states are owned by _end(). */
                return ZXC_ERROR_NULL_INPUT;
        }
    }
}

int64_t zxc_cstream_end(zxc_cstream* cs, zxc_outbuf_t* out) {
    if (UNLIKELY(!cs || !out)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(cs->state == CS_DONE)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(cs->state == CS_ERRORED)) return cs->error_code;

    for (;;) {
        switch (cs->state) {
            case CS_INIT: {
                /* _end before any input, still need to emit file header. */
                const int rc = cs_stage_file_header(cs);
                if (UNLIKELY(rc < 0)) return cs_set_error(cs, rc);  // LCOV_EXCL_LINE
                cs->state = CS_DRAIN_HEADER;
                break;
            }

            case CS_DRAIN_HEADER: {
                if (!cs_drain_pending(cs, out)) return (int64_t)(cs->pending_len - cs->pending_pos);
                cs->state = CS_ACCUMULATE;
                break;
            }

            case CS_DRAIN_BLOCK: {
                /* This drain came from a full block compressed during _compress. */
                if (!cs_drain_pending(cs, out)) return (int64_t)(cs->pending_len - cs->pending_pos);
                cs->state = CS_ACCUMULATE;
                break;
            }

            case CS_ACCUMULATE: {
                /* Compress the residual partial block (if any), then EOF + footer. */
                if (cs->in_used > 0) {
                    const int rc = cs_compress_one_block(cs);
                    if (UNLIKELY(rc < 0)) return cs_set_error(cs, rc);  // LCOV_EXCL_LINE
                    cs->state = CS_DRAIN_LAST;
                    break;
                }
                /* No residual data: go straight to EOF. */
                {
                    const int rc = cs_stage_eof(cs);
                    if (UNLIKELY(rc < 0)) return cs_set_error(cs, rc);  // LCOV_EXCL_LINE
                    cs->state = CS_DRAIN_EOF;
                    break;
                }
            }

            case CS_DRAIN_LAST: {
                if (!cs_drain_pending(cs, out)) return (int64_t)(cs->pending_len - cs->pending_pos);
                /* After last data block -> EOF. */
                const int rc = cs_stage_eof(cs);
                if (UNLIKELY(rc < 0)) return cs_set_error(cs, rc);  // LCOV_EXCL_LINE
                cs->state = CS_DRAIN_EOF;
                break;
            }

            case CS_DRAIN_EOF: {
                if (!cs_drain_pending(cs, out)) return (int64_t)(cs->pending_len - cs->pending_pos);
                const int rc = cs_stage_footer(cs);
                if (UNLIKELY(rc < 0)) return cs_set_error(cs, rc);  // LCOV_EXCL_LINE
                cs->state = CS_DRAIN_FOOTER;
                break;
            }

            case CS_DRAIN_FOOTER: {
                if (!cs_drain_pending(cs, out)) return (int64_t)(cs->pending_len - cs->pending_pos);
                cs->state = CS_DONE;
                return 0;
            }

            case CS_DONE:
            case CS_ERRORED:
                return cs->state == CS_ERRORED ? cs->error_code : 0;
        }
    }
}

/* ===================================================================== */
/*  Decompression                                                        */
/* ===================================================================== */

typedef enum {
    DS_NEED_FILE_HEADER = 0,
    DS_NEED_BLOCK_HEADER,
    DS_NEED_BLOCK_PAYLOAD,
    DS_DECODE_BLOCK,
    DS_EMIT_DECODED,
    DS_PEEK_TAIL,         /* reading 8 bytes after EOF block to sniff SEK vs footer */
    DS_DRAIN_SEK_PAYLOAD, /* skipping SEK payload bytes                              */
    DS_NEED_FOOTER_FULL,  /* need 12 footer bytes (post-SEK)                         */
    DS_NEED_FOOTER_REST,  /* peek already gave us 8 bytes; need 4 more               */
    DS_VALIDATE_FOOTER,
    DS_DONE,
    DS_ERRORED
} dstream_state_t;

struct zxc_dstream_s {
    zxc_decompress_opts_t opts;
    zxc_cctx_t inner;
    int inner_initialized;
    size_t block_size;     /* learned from file header */
    int file_has_checksum; /* learned from file header */

    /* Generic accumulator for fixed-size headers/footers/peek. */
    uint8_t scratch[32]; /* big enough for max(file_header=16, footer=12, block_header=8) */
    size_t scratch_used;
    size_t scratch_need;

    /* Variable-size block payload accumulator (allocated when block_size known) */
    uint8_t* payload;
    size_t payload_cap;
    size_t payload_used;
    size_t payload_need; /* comp_size + (file_has_checksum ? 4 : 0) */

    /* Decoded block staging */
    uint8_t* decoded;
    size_t decoded_cap;
    size_t decoded_size;
    size_t decoded_pos;

    zxc_block_header_t cur_bh;
    size_t sek_remaining;

    uint64_t total_out;
    uint32_t global_hash;

    dstream_state_t state;
    int error_code;
};

/* Latches a sticky error on the decompression stream: stores `code` in
 * ds->error_code, transitions the state to DS_ERRORED, and returns `code`.
 * Once errored, subsequent _decompress calls return the same code without
 * performing further work. */
static int ds_set_error(zxc_dstream* ds, const int code) {
    ds->error_code = code;
    ds->state = DS_ERRORED;
    return code;
}

/* Copy up to (need - have) bytes from in into ds->scratch. Returns 1 if fully filled. */
static int ds_pull_scratch(zxc_dstream* ds, zxc_inbuf_t* in) {
    const size_t want = ds->scratch_need - ds->scratch_used;
    const size_t avail = in->size - in->pos;
    const size_t n = want < avail ? want : avail;
    if (n) {
        ZXC_MEMCPY(ds->scratch + ds->scratch_used, (const uint8_t*)in->src + in->pos, n);
        in->pos += n;
        ds->scratch_used += n;
    }
    return ds->scratch_used == ds->scratch_need;
}

/* Same but pulls into a heap buffer (block payload). */
static int ds_pull_payload(zxc_dstream* ds, zxc_inbuf_t* in) {
    const size_t want = ds->payload_need - ds->payload_used;
    const size_t avail = in->size - in->pos;
    const size_t n = want < avail ? want : avail;
    if (n) {
        ZXC_MEMCPY(ds->payload + ds->payload_used, (const uint8_t*)in->src + in->pos, n);
        in->pos += n;
        ds->payload_used += n;
    }
    return ds->payload_used == ds->payload_need;
}

zxc_dstream* zxc_dstream_create(const zxc_decompress_opts_t* opts) {
    zxc_dstream* ds = (zxc_dstream*)calloc(1, sizeof(*ds));
    if (UNLIKELY(!ds)) return NULL;  // LCOV_EXCL_LINE
    if (opts) ds->opts = *opts;
    ds->opts.n_threads = 0;
    ds->opts.progress_cb = NULL;
    ds->opts.user_data = NULL;
    ds->state = DS_NEED_FILE_HEADER;
    ds->scratch_need = ZXC_FILE_HEADER_SIZE;
    return ds;
}

void zxc_dstream_free(zxc_dstream* ds) {
    if (!ds) return;
    free(ds->payload);
    free(ds->decoded);
    if (ds->inner_initialized) zxc_cctx_free(&ds->inner);
    free(ds);
}

int zxc_dstream_finished(const zxc_dstream* ds) { return (ds && ds->state == DS_DONE) ? 1 : 0; }

size_t zxc_dstream_in_size(const zxc_dstream* ds) {
    if (!ds) return 0;
    if (ds->block_size == 0) return ZXC_BLOCK_SIZE_DEFAULT;
    const uint64_t b = zxc_compress_block_bound(ds->block_size);
    return (b == 0 || b > SIZE_MAX) ? ds->block_size : (size_t)b;
}

size_t zxc_dstream_out_size(const zxc_dstream* ds) {
    if (!ds) return 0;
    return ds->block_size == 0 ? ZXC_BLOCK_SIZE_DEFAULT : ds->block_size;
}

/* Drain ds->decoded[decoded_pos..decoded_size) into out. Returns 1 if drained. */
static int ds_drain_decoded(zxc_dstream* ds, zxc_outbuf_t* out, size_t* produced) {
    const size_t avail_out = out->size - out->pos;
    const size_t avail_dec = ds->decoded_size - ds->decoded_pos;
    const size_t n = avail_out < avail_dec ? avail_out : avail_dec;
    if (n) {
        ZXC_MEMCPY((uint8_t*)out->dst + out->pos, ds->decoded + ds->decoded_pos, n);
        out->pos += n;
        ds->decoded_pos += n;
        ds->total_out += n;
        if (produced) *produced += n;
    }
    return ds->decoded_pos == ds->decoded_size;
}

static int ds_handle_need_file_header(zxc_dstream* ds, zxc_inbuf_t* in) {
    if (!ds_pull_scratch(ds, in)) return 1;

    size_t bs = 0;
    int has_csum = 0;
    const int rc = zxc_read_file_header(ds->scratch, ds->scratch_used, &bs, &has_csum);
    if (UNLIKELY(rc != ZXC_OK)) return ds_set_error(ds, rc);  // LCOV_EXCL_LINE
    ds->block_size = bs;
    ds->file_has_checksum = has_csum;

    /* Allocate payload + decoded buffers now that block_size is known. */
    const uint64_t pb = zxc_compress_block_bound(ds->block_size);
    // LCOV_EXCL_START
    if (UNLIKELY(pb == 0 || pb > SIZE_MAX)) return ds_set_error(ds, ZXC_ERROR_OVERFLOW);
    // LCOV_EXCL_STOP
    ds->payload_cap = (size_t)pb;
    ds->payload = (uint8_t*)malloc(ds->payload_cap);

    /* Decoded buffer is sized for the wild-copy fast path: block_size +
     * ZXC_PAD_SIZE; same pattern as the buffer API uses internally.  Real
     * decoded payload lives in [0..decoded_size); the trailing PAD bytes
     * hold wild-copy overflow we never emit. */
    ds->decoded_cap = ds->block_size + ZXC_PAD_SIZE;
    ds->decoded = (uint8_t*)malloc(ds->decoded_cap);
    // LCOV_EXCL_START
    if (UNLIKELY(!ds->payload || !ds->decoded)) return ds_set_error(ds, ZXC_ERROR_MEMORY);

    if (UNLIKELY(zxc_cctx_init(&ds->inner, ds->block_size, 0, 0,
                               ds->file_has_checksum && ds->opts.checksum_enabled) != ZXC_OK)) {
        return ds_set_error(ds, ZXC_ERROR_MEMORY);
    }
    // LCOV_EXCL_STOP
    ds->inner_initialized = 1;

    ds->state = DS_NEED_BLOCK_HEADER;
    ds->scratch_used = 0;
    ds->scratch_need = ZXC_BLOCK_HEADER_SIZE;
    return 0;
}

static int ds_handle_need_block_header(zxc_dstream* ds, zxc_inbuf_t* in) {
    if (!ds_pull_scratch(ds, in)) return 1;

    const int rc = zxc_read_block_header(ds->scratch, ds->scratch_used, &ds->cur_bh);
    if (UNLIKELY(rc != ZXC_OK)) return ds_set_error(ds, rc);  // LCOV_EXCL_LINE

    if (ds->cur_bh.block_type == (uint8_t)ZXC_BLOCK_EOF) {
        /* EOF block: comp_size must be 0; no payload, no checksum. */
        if (UNLIKELY(ds->cur_bh.comp_size != 0)) return ds_set_error(ds, ZXC_ERROR_BAD_BLOCK_SIZE);
        ds->state = DS_PEEK_TAIL;
        ds->scratch_used = 0;
        ds->scratch_need = ZXC_BLOCK_HEADER_SIZE; /* sniff */
        return 0;
    }

    /* Normal data block: read comp_size [+ 4 if file-level checksums]. */
    const uint64_t need = (uint64_t)ds->cur_bh.comp_size + (ds->file_has_checksum ? 4u : 0u);
    if (UNLIKELY(need > ds->payload_cap)) return ds_set_error(ds, ZXC_ERROR_BAD_BLOCK_SIZE);

    /* Feed the full block (header + payload + opt csum) to zxc_decompress_block,
     * so prefix with the 8-byte header we just parsed. */
    ZXC_MEMCPY(ds->payload, ds->scratch, ZXC_BLOCK_HEADER_SIZE);
    ds->payload_used = ZXC_BLOCK_HEADER_SIZE;
    ds->payload_need = (size_t)need + ZXC_BLOCK_HEADER_SIZE;
    // LCOV_EXCL_START
    if (UNLIKELY(ds->payload_need > ds->payload_cap)) {
        /* grow */
        uint8_t* nb = (uint8_t*)realloc(ds->payload, ds->payload_need);
        if (UNLIKELY(!nb)) return ds_set_error(ds, ZXC_ERROR_MEMORY);
        ds->payload = nb;
        ds->payload_cap = ds->payload_need;
    }
    // LCOV_EXCL_STOP
    ds->state = DS_NEED_BLOCK_PAYLOAD;
    return 0;
}

int64_t zxc_dstream_decompress(zxc_dstream* ds, zxc_outbuf_t* out, zxc_inbuf_t* in) {
    if (UNLIKELY(!ds || !out || !in || in->pos > in->size || out->pos > out->size ||
                 (in->size > in->pos && !in->src) || (out->size > out->pos && !out->dst))) {
        return ZXC_ERROR_NULL_INPUT;
    }
    if (UNLIKELY(ds->state == DS_ERRORED)) return ds->error_code;
    if (UNLIKELY(ds->state == DS_DONE)) return 0;

    size_t produced = 0;

    for (;;) {
        switch (ds->state) {
            case DS_NEED_FILE_HEADER: {
                const int rc = ds_handle_need_file_header(ds, in);
                if (rc == 1) return (int64_t)produced;
                if (rc < 0) return rc;
                break;
            }

            case DS_NEED_BLOCK_HEADER: {
                const int rc = ds_handle_need_block_header(ds, in);
                if (rc == 1) return (int64_t)produced;
                if (rc < 0) return rc;
                break;
            }

            case DS_NEED_BLOCK_PAYLOAD: {
                if (!ds_pull_payload(ds, in)) return (int64_t)produced;
                ds->state = DS_DECODE_BLOCK;
                break;
            }

            case DS_DECODE_BLOCK: {
                const int dsz = zxc_decompress_chunk_wrapper(
                    &ds->inner, ds->payload, ds->payload_used, ds->decoded, ds->decoded_cap);
                if (UNLIKELY(dsz < 0)) return ds_set_error(ds, dsz);
                ds->decoded_size = (size_t)dsz;
                ds->decoded_pos = 0;

                /* If file-level checksum verification is enabled, fold this
                 * block's trailer into the rolling global hash (last 4 bytes
                 * of the *raw* block). */
                if (ds->opts.checksum_enabled && ds->file_has_checksum && ds->payload_used >= 4) {
                    const uint32_t bh = zxc_le32(ds->payload + ds->payload_used - 4);
                    ds->global_hash = zxc_hash_combine_rotate(ds->global_hash, bh);
                }
                ds->state = DS_EMIT_DECODED;
                break;
            }

            case DS_EMIT_DECODED: {
                const int done = ds_drain_decoded(ds, out, &produced);
                if (!done) return (int64_t)produced;
                ds->state = DS_NEED_BLOCK_HEADER;
                ds->scratch_used = 0;
                ds->scratch_need = ZXC_BLOCK_HEADER_SIZE;
                break;
            }

            case DS_PEEK_TAIL: {
                if (!ds_pull_scratch(ds, in)) return (int64_t)produced;
                /* Try to interpret as a block header (SEK). */
                zxc_block_header_t peek;
                const int sek_rc = zxc_read_block_header(ds->scratch, ds->scratch_used, &peek);
                if (sek_rc == ZXC_OK && peek.block_type == (uint8_t)ZXC_BLOCK_SEK) {
                    /* SEK block: skip its payload (peek.comp_size bytes). */
                    ds->sek_remaining = (size_t)peek.comp_size;
                    ds->state = DS_DRAIN_SEK_PAYLOAD;
                    break;
                }
                /* Not SEK -> these 8 bytes are the first 8 of the 12-byte footer. */
                ds->state = DS_NEED_FOOTER_REST;
                ds->scratch_need = ZXC_FILE_FOOTER_SIZE; /* keep first 8, want 4 more */
                break;
            }

            case DS_DRAIN_SEK_PAYLOAD: {
                const size_t avail = in->size - in->pos;
                const size_t n = avail < ds->sek_remaining ? avail : ds->sek_remaining;
                in->pos += n;
                ds->sek_remaining -= n;
                if (ds->sek_remaining > 0) return (int64_t)produced;
                ds->state = DS_NEED_FOOTER_FULL;
                ds->scratch_used = 0;
                ds->scratch_need = ZXC_FILE_FOOTER_SIZE;
                break;
            }

            case DS_NEED_FOOTER_REST:
            case DS_NEED_FOOTER_FULL: {
                if (!ds_pull_scratch(ds, in)) return (int64_t)produced;
                ds->state = DS_VALIDATE_FOOTER;
                break;
            }

            case DS_VALIDATE_FOOTER: {
                const uint64_t declared = zxc_le64(ds->scratch);
                if (UNLIKELY(declared != ds->total_out))
                    return ds_set_error(ds, ZXC_ERROR_CORRUPT_DATA);
                if (ds->opts.checksum_enabled && ds->file_has_checksum) {
                    const uint32_t fh = zxc_le32(ds->scratch + sizeof(uint64_t));
                    if (UNLIKELY(fh != ds->global_hash))
                        return ds_set_error(ds, ZXC_ERROR_BAD_CHECKSUM);
                }
                ds->state = DS_DONE;
                return (int64_t)produced;
            }

            case DS_DONE:
            case DS_ERRORED:
                return ds->state == DS_ERRORED ? ds->error_code : (int64_t)produced;
        }
    }
}
