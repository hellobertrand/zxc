// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file fuzz_inplace.c
 * @brief Fuzzer for the single-buffer decompressors (zxc_decompress_inplace,
 *        zxc_decompress_inplace_dctx).
 *
 * Input and output share one buffer: the write cursor must never catch up with
 * the read cursor. Each archive is decoded at its own bound, flush-right,
 * through a heap context reused across the run's archives, then again through
 * the one-shot entry point (which never verifies checksums) and through a
 * static context per block size: same code and bytes, or BAD_BLOCK_SIZE from a
 * static context sized for another block.
 *
 *  1. The raw input, as an archive.
 *  2. A payload compressed with header-chosen parameters, behind runs of zeros
 *     then noise (4 KiB units): the compressible-head shape short inputs never
 *     reach. It must decode bit-exact, also with a header bit that re-encodes it
 *     in blocks smaller than the header says (valid, uncounted by the bound).
 *  3. The same archive padded before the footer: it must be refused. The tail
 *     check alone would; ASan is the oracle for an overtaking write.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_constants.h"
#include "../include/zxc_error.h"
#include "../src/lib/zxc_internal.h"

#define FUZZ_INPLACE_MAX_INPUT (64 << 10) /* 64 KiB of fuzzer bytes */
#define FUZZ_INPLACE_UNIT 4096            /* zero and noise run granularity */
#define FUZZ_INPLACE_MAX_BUF (16 << 20)   /* a larger bound is skipped */

#define SKIPPED INT64_MAX /* never a decoded size */

#define N_SIZES 4
static const size_t kBlockSizes[N_SIZES] = {4096, 8192, 16384, 65536};

/* The run's contexts, all carrying the run's checksum setting. Static workspaces
 * are allocated once and re-initialised every run, so a crash reproduces from
 * its input alone. */
typedef struct {
    zxc_dctx* heap;
    zxc_dctx* fixed[N_SIZES];
    int checksum;
} run_ctx_t;

/* Applies the checksum setting; the context, or NULL when it refused it. */
static zxc_dctx* with_checksum(zxc_dctx* d, const int checksum) {
    if (d && zxc_dctx_set_param(d, ZXC_D_CHECKSUM, checksum) != ZXC_OK) return NULL;
    return d;
}

static void run_ctx_init(run_ctx_t* c, const int checksum) {
    static void* ws[N_SIZES];
    static size_t ws_size[N_SIZES];
    c->checksum = checksum;
    c->heap = with_checksum(zxc_create_dctx(), checksum);
    for (size_t k = 0; k < N_SIZES; k++) {
        if (!ws[k]) {
            ws_size[k] = (zxc_static_dctx_workspace_size(kBlockSizes[k]) + 63) & ~(size_t)63;
            ws[k] = aligned_alloc(64, ws_size[k]);
        }
        c->fixed[k] =
            ws[k] ? with_checksum(zxc_init_static_dctx(ws[k], ws_size[k], kBlockSizes[k]), checksum)
                  : NULL;
    }
}

/* Decodes @p arc through @p d from a fresh copy in @p alt; checks it against @p r
 * and @p ref. @p fixed is a static context's block size (0 for heap), @p bs the
 * header's when known (else 0). */
static void check_dctx(zxc_dctx* d, const size_t fixed, const size_t bs, const uint8_t* arc,
                       const size_t len, uint8_t* alt, const size_t cap, const int64_t r,
                       const uint8_t* ref) {
    if (!d) return;
    memcpy(alt + cap - len, arc, len);
    const int64_t r2 = zxc_decompress_inplace_dctx(d, alt, cap, len);
    // A static context refuses a foreign block size and a dictionary archive first.
    if (fixed && ((fixed != bs && r2 == ZXC_ERROR_BAD_BLOCK_SIZE) ||
                  (r == ZXC_ERROR_DICT_REQUIRED && r2 == ZXC_ERROR_DICT_UNSUPPORTED)))
        return;
    assert(r2 == r);
    assert(r <= 0 || memcmp(ref, alt, (size_t)r) == 0);
    (void)r2;
    (void)ref;
}

/* Decodes @p arc in place at its bound through the heap context; the result, or
 * SKIPPED. The one-shot entry point, which never verifies checksums, and the
 * static contexts must agree with it. @p bs is the header's block size when
 * known, else 0. */
static int64_t decode_inplace(const uint8_t* arc, const size_t len, uint8_t** out,
                              const run_ctx_t* c, const size_t bs) {
    if (!c->heap) return SKIPPED;
    const size_t cap = zxc_decompress_inplace_bound(arc, len);
    if (cap == 0 || cap > FUZZ_INPLACE_MAX_BUF) return SKIPPED;
    uint8_t* const buf = (uint8_t*)malloc(cap);
    uint8_t* const alt = (uint8_t*)malloc(cap);
    if (!buf || !alt) {
        free(buf);
        free(alt);
        return SKIPPED;
    }
    memcpy(buf + cap - len, arc, len);
    const int64_t r = zxc_decompress_inplace_dctx(c->heap, buf, cap, len);
    // Unverified decode: identical unless verification alone refused the archive.
    memcpy(alt + cap - len, arc, len);
    const int64_t r1 = zxc_decompress_inplace(alt, cap, len);
    if (!(c->checksum && r == ZXC_ERROR_BAD_CHECKSUM)) {
        assert(r1 == r);
        assert(r <= 0 || memcmp(buf, alt, (size_t)r) == 0);
    }
    (void)r1;
    for (size_t k = 0; k < N_SIZES; k++)
        check_dctx(c->fixed[k], kBlockSizes[k], bs, arc, len, alt, cap, r, buf);
    free(alt);
    if (out)
        *out = buf;
    else
        free(buf);
    return r;
}

static void fuzz_one(const uint8_t* data, size_t size, const run_ctx_t* c) {
    (void)decode_inplace(data, size, NULL, c, 0);

    const int level = (int)(data[0] % (unsigned)zxc_max_level()) + 1;
    const int checksum = c->checksum;
    const int seekable = (data[1] >> 1) & 1;
    const size_t block_size = kBlockSizes[(data[1] >> 2) & 3];
    // Blocks half the header's size; not with checksum or seek table, which
    // describe the real blocks.
    const size_t short_bs = (data[1] & 16) && !checksum && !seekable && block_size > kBlockSizes[0]
                                ? block_size / 2
                                : 0;
    const size_t zero_len = (size_t)(data[2] & 15) * FUZZ_INPLACE_UNIT;
    const size_t noise_len = (size_t)(data[2] >> 4) * FUZZ_INPLACE_UNIT;
    const size_t pad = ((size_t)data[3] << 8 | data[4]) + 1;

    data += 5;
    size -= 5;
    if (size > FUZZ_INPLACE_MAX_INPUT) return;
    const size_t n = zero_len + noise_len + size;
    if (n == 0) return;

    // [zeros][noise][fuzzer bytes]
    uint8_t* const src = (uint8_t*)malloc(n);
    if (!src) return;
    memset(src, 0, zero_len);
    uint64_t st = 0x9E3779B97F4A7C15ULL ^ n;
    for (size_t i = zero_len; i < zero_len + noise_len; i++) {
        st = st * 6364136223846793005ULL + 1442695040888963407ULL;
        src[i] = (uint8_t)(st >> 56);
    }
    if (size) memcpy(src + zero_len + noise_len, data, size);

    zxc_cctx* const cctx = zxc_create_cctx();
    const int configured =
        cctx && zxc_cctx_set_param(cctx, ZXC_C_LEVEL, level) == ZXC_OK &&
        zxc_cctx_set_param(cctx, ZXC_C_BLOCK_SIZE, (int64_t)block_size) == ZXC_OK &&
        zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, checksum) == ZXC_OK &&
        zxc_cctx_set_param(cctx, ZXC_C_SEEKABLE, seekable) == ZXC_OK;
    const size_t cbound = zxc_compress_bound(n);
    uint8_t* const arc = (uint8_t*)malloc(cbound + pad + ZXC_FILE_FOOTER_MAX_SIZE);
    int64_t csize = (arc && configured) ? zxc_compress_cctx(cctx, src, n, arc, cbound) : -1;
    zxc_free_cctx(cctx);
    if (csize > 0 && short_bs) {
        // Same header and EOF block; blocks re-encoded at short_bs, then the
        // footer they imply.
        zxc_cctx* cc = zxc_create_cctx();
        if (cc && (zxc_cctx_set_param(cc, ZXC_C_LEVEL, level) != ZXC_OK ||
                   zxc_cctx_set_param(cc, ZXC_C_BLOCK_SIZE, (int64_t)short_bs) != ZXC_OK)) {
            zxc_free_cctx(cc);
            cc = NULL;
        }
        const size_t tail = 8 + ZXC_FILE_FOOTER_MAX_SIZE; /* EOF block header, then footer */
        const zxc_block_header_t eof = {
            .block_type = ZXC_BLOCK_EOF, .block_flags = 0, .reserved = 0, .comp_size = 0};
        size_t pos = ZXC_FILE_HEADER_SIZE;
        for (size_t off = 0; cc && off < n && pos + tail <= cbound; off += short_bs) {
            const size_t take = n - off < short_bs ? n - off : short_bs;
            const int64_t w = zxc_compress_block(cc, src + off, take, arc + pos, cbound - pos);
            if (w <= 0) {
                pos = 0;
                break;
            }
            pos += (size_t)w;
        }
        zxc_free_cctx(cc);
        if (pos == 0)
            csize = -1;
        else {
            zxc_write_block_header(arc + pos, ZXC_BLOCK_HEADER_SIZE, &eof);
            pos += ZXC_BLOCK_HEADER_SIZE;
            const int fw = zxc_write_file_footer(arc + pos, cbound - pos, pos, n, 0, 0);
            csize = fw > 0 ? (int64_t)(pos + (size_t)fw) : -1;
        }
    }
    if (csize <= 0) {
        free(arc);
        free(src);
        return;
    }
    const size_t len = (size_t)csize;

    uint8_t* out = NULL;
    const int64_t d = decode_inplace(arc, len, &out, c, block_size);
    assert(d == SKIPPED || (d == (int64_t)n && memcmp(out, src, n) == 0));
    free(out);

    // Pad before the footer, re-signed to cover it as a forger would.
    uint64_t dsize = 0, frame = 0;
    size_t sizes = 0;
    if (zxc_parse_file_footer(arc + len, len, &dsize, &frame, &sizes) != ZXC_OK) abort();
    const size_t body = len - sizes - (checksum ? ZXC_FILE_DIGEST_SIZE : 0);
    const uint64_t digest = checksum ? zxc_le64(arc + body) : 0;
    memset(arc + body, 0xA5, pad);
    const int fw = zxc_write_file_footer(arc + body + pad, cbound + ZXC_FILE_FOOTER_MAX_SIZE - body,
                                         body + pad, dsize, digest, checksum);
    const int64_t p =
        fw > 0 ? decode_inplace(arc, body + pad + (size_t)fw, NULL, c, block_size) : -1;
    assert(p == SKIPPED || p < 0);
    (void)d;
    (void)p;

    free(arc);
    free(src);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 5) return 0;
    run_ctx_t c;
    run_ctx_init(&c, data[1] & 1);
    fuzz_one(data, size, &c);
    zxc_free_dctx(c.heap);
    return 0;
}
