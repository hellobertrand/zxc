/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fuzz_inplace.c
 * @brief Fuzzer for the single-buffer decompressor (zxc_decompress_inplace).
 *
 * Input and output share one buffer: the write cursor must never catch up with
 * the read cursor. Each archive is decoded at its own bound, flush-right.
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

#define FUZZ_INPLACE_MAX_INPUT (64 << 10) /* 64 KiB of fuzzer bytes */
#define FUZZ_INPLACE_UNIT 4096            /* zero and noise run granularity */
#define FUZZ_INPLACE_MAX_BUF (16 << 20)   /* a larger bound is skipped */

#define SKIPPED INT64_MAX /* never a decoded size */

static const size_t kBlockSizes[] = {4096, 8192, 16384, 65536};

/* Decodes @p arc in place at its bound; the result, or SKIPPED. */
static int64_t decode_inplace(const uint8_t* arc, const size_t len, uint8_t** out,
                              const int checksum) {
    const size_t cap = zxc_decompress_inplace_bound(arc, len);
    if (cap == 0 || cap > FUZZ_INPLACE_MAX_BUF) return SKIPPED;
    uint8_t* const buf = (uint8_t*)malloc(cap);
    if (!buf) return SKIPPED;
    memcpy(buf + cap - len, arc, len);
    const zxc_decompress_opts_t o = {.checksum_enabled = checksum};
    const int64_t r = zxc_decompress_inplace(buf, cap, len, &o);
    if (out)
        *out = buf;
    else
        free(buf);
    return r;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 5) return 0;
    (void)decode_inplace(data, size, NULL, data[1] & 1);

    const int level = (int)(data[0] % (unsigned)zxc_max_level()) + 1;
    const int checksum = data[1] & 1;
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
    if (size > FUZZ_INPLACE_MAX_INPUT) return 0;
    const size_t n = zero_len + noise_len + size;
    if (n == 0) return 0;

    // [zeros][noise][fuzzer bytes]
    uint8_t* const src = (uint8_t*)malloc(n);
    if (!src) return 0;
    memset(src, 0, zero_len);
    uint64_t st = 0x9E3779B97F4A7C15ULL ^ n;
    for (size_t i = zero_len; i < zero_len + noise_len; i++) {
        st = st * 6364136223846793005ULL + 1442695040888963407ULL;
        src[i] = (uint8_t)(st >> 56);
    }
    if (size) memcpy(src + zero_len + noise_len, data, size);

    const zxc_compress_opts_t co = {.level = level,
                                    .block_size = block_size,
                                    .checksum_enabled = checksum,
                                    .seekable = seekable};
    const size_t cbound = (size_t)zxc_compress_bound(n);
    uint8_t* const arc = (uint8_t*)malloc(cbound + pad);
    int64_t csize = arc ? zxc_compress(src, n, arc, cbound, &co) : -1;
    if (csize > 0 && short_bs) {
        // Same header, EOF block and footer; blocks re-encoded at short_bs.
        const zxc_compress_opts_t so = {.level = level, .block_size = short_bs};
        zxc_cctx* const cc = zxc_create_cctx(&so);
        const size_t tail = 8 + ZXC_FILE_FOOTER_SIZE; /* EOF block header, then footer */
        uint8_t end[8 + ZXC_FILE_FOOTER_SIZE];
        memcpy(end, arc + csize - tail, tail);
        size_t pos = ZXC_FILE_HEADER_SIZE;
        for (size_t off = 0; cc && off < n && pos + tail <= cbound; off += short_bs) {
            const size_t take = n - off < short_bs ? n - off : short_bs;
            const int64_t w = zxc_compress_block(cc, src + off, take, arc + pos, cbound - pos, &so);
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
            memcpy(arc + pos, end, tail);
            csize = (int64_t)(pos + tail);
        }
    }
    if (csize <= 0) {
        free(arc);
        free(src);
        return 0;
    }
    const size_t len = (size_t)csize;

    uint8_t* out = NULL;
    const int64_t d = decode_inplace(arc, len, &out, checksum);
    assert(d == SKIPPED || (d == (int64_t)n && memcmp(out, src, n) == 0));
    free(out);

    // Pad before the footer.
    const size_t footer = ZXC_FILE_FOOTER_SIZE + (checksum ? ZXC_FILE_DIGEST_SIZE : 0);
    memmove(arc + len - footer + pad, arc + len - footer, footer);
    memset(arc + len - footer, 0xA5, pad);
    const int64_t p = decode_inplace(arc, len + pad, NULL, checksum);
    assert(p == SKIPPED || p < 0);
    (void)d;
    (void)p;

    free(arc);
    free(src);
    return 0;
}
