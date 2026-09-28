/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fuzz_block.c
 * @brief Fuzzer for the block API (zxc_decompress_block, zxc_decompress_block_safe).
 *
 * Destinations are allocated to their exact capacity, so ASan catches any write
 * past it: the fast decoder promises never to write beyond dst_capacity, the
 * safe one takes a capacity equal to the output.
 *
 *  1. The raw input, as a block, through both decoders on a heap context and
 *     through the safe one on a static context per block size. When two succeed
 *     they must agree byte for byte.
 *  2. The input compressed as one block at header-chosen parameters, then
 *     decoded by every entry point: bit-exact, also into an exactly-sized
 *     destination. A truncated copy must be refused.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"

#define FUZZ_BLOCK_MAX_INPUT (256 << 10) /* 256 KiB of fuzzer bytes */
#define FUZZ_BLOCK_CTRL 4                /* control bytes consumed below */
#define N_SIZES 3

static const size_t kBlockSizes[N_SIZES] = {4096, 65536, 262144};

/* Static contexts: workspaces allocated once, re-initialised every run so a
 * crash reproduces from its input alone. */
static zxc_dctx* static_dctx(const size_t k) {
    static void* ws[N_SIZES];
    static size_t ws_size[N_SIZES];
    if (!ws[k]) {
        ws_size[k] = (zxc_static_dctx_workspace_size(kBlockSizes[k]) + 63) & ~(size_t)63;
        ws[k] = aligned_alloc(64, ws_size[k]);
        if (!ws[k]) return NULL;
    }
    return zxc_init_static_dctx(ws[k], ws_size[k], kBlockSizes[k]);
}

/* Decodes @p src into a fresh exactly-sized buffer; the result, the bytes in @p out. */
static int64_t decode(zxc_dctx* d, const int safe, const uint8_t* src, const size_t len,
                      const size_t cap, const zxc_decompress_opts_t* o, uint8_t** out) {
    uint8_t* const dst = (uint8_t*)malloc(cap);
    if (!dst) return INT64_MIN;
    const int64_t r = safe ? zxc_decompress_block_safe(d, src, len, dst, cap, o)
                           : zxc_decompress_block(d, src, len, dst, cap, o);
    assert(r < 0 || (size_t)r <= cap);
    *out = dst;
    return r;
}

/* Both succeeded: same size, same bytes. */
static void agree(const int64_t r1, const uint8_t* b1, const int64_t r2, const uint8_t* b2) {
    if (r1 > 0 && r2 > 0) {
        assert(r1 == r2);
        assert(memcmp(b1, b2, (size_t)r1) == 0);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < FUZZ_BLOCK_CTRL || size > FUZZ_BLOCK_MAX_INPUT) return 0;
    const int level = (int)(data[0] % (unsigned)zxc_max_level()) + 1;
    const int checksum = data[1] & 1;
    const size_t k = (size_t)((data[1] >> 1) % N_SIZES);
    const size_t bs = kBlockSizes[k];
    // Raw-decode capacity, 1 .. 2^18.
    const size_t raw_cap = ((size_t)data[2] << 10 | (size_t)data[3] << 2 | (data[1] >> 6)) + 1;
    data += FUZZ_BLOCK_CTRL;
    size -= FUZZ_BLOCK_CTRL;
    if (size == 0) return 0;

    const zxc_decompress_opts_t o = {.checksum_enabled = checksum};
    zxc_dctx* const heap = zxc_create_dctx();
    if (!heap) return 0;

    /* 1. Raw input as a block. */
    {
        uint8_t *fast = NULL, *safe = NULL, *stat = NULL;
        const int64_t rf = decode(heap, 0, data, size, raw_cap, &o, &fast);
        const int64_t rs = decode(heap, 1, data, size, raw_cap, &o, &safe);
        agree(rf, fast, rs, safe);
        zxc_dctx* const sd = static_dctx(k);
        if (sd) {
            const int64_t rt = decode(sd, 1, data, size, raw_cap, &o, &stat);
            agree(rs, safe, rt, stat);
        }
        free(fast);
        free(safe);
        free(stat);
    }

    /* 2. Round trip of one block of at most bs bytes. */
    const size_t n = size < bs ? size : bs;
    const zxc_compress_opts_t co = {.level = level, .checksum_enabled = checksum, .block_size = bs};
    const size_t cb = (size_t)zxc_compress_block_bound(n);
    zxc_cctx* const cctx = zxc_create_cctx(&co);
    uint8_t* const comp = cb ? (uint8_t*)malloc(cb) : NULL;
    const int64_t c = (cctx && comp) ? zxc_compress_block(cctx, data, n, comp, cb, &co) : -1;
    if (c > 0) {
        const size_t len = (size_t)c;
        const size_t padded = (size_t)zxc_decompress_block_bound(n);
        uint8_t* out = NULL;
        int64_t r;

        r = decode(heap, 0, comp, len, padded, &o, &out);
        assert(r == (int64_t)n && memcmp(out, data, n) == 0);
        free(out);
        r = decode(heap, 0, comp, len, n, &o, &out);
        assert(r == (int64_t)n && memcmp(out, data, n) == 0);
        free(out);
        r = decode(heap, 1, comp, len, n, &o, &out);
        assert(r == (int64_t)n && memcmp(out, data, n) == 0);
        free(out);
        zxc_dctx* const sd = static_dctx(k);
        if (sd) {
            r = decode(sd, 1, comp, len, n, &o, &out);
            assert(r == (int64_t)n && memcmp(out, data, n) == 0);
            free(out);
        }

        // Truncated: a block's length is part of its header, so any cut fails.
        r = decode(heap, 1, comp, len - 1, n, &o, &out);
        assert(r < 0);
        free(out);
        (void)r;
    }
    free(comp);
    zxc_free_cctx(cctx);
    zxc_free_dctx(heap);
    return 0;
}
