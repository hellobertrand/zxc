// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file fuzz_seekable.c
 * @brief Fuzzer for the seekable random-access decompression API.
 *
 * Strategy: compress fuzzed input with seekable=1, then exercise the full
 * seekable read path (open, metadata getters, single-threaded decompress,
 * multi-threaded decompress) and verify data integrity.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_error.h"
#include "../../include/zxc_seekable.h"

#define FUZZ_SEEKABLE_MAX_INPUT (4 << 20) /* 4 MiB */

/* Phases 1-6 and 8; @p flags is the control byte. */
static void seekable_phases(const uint8_t* data, const size_t size, const int level,
                            const uint8_t flags, uint8_t* comp_buf, const size_t bound,
                            uint8_t* decomp_buf) {
    const int use_checksum = flags & 1;
    const int use_mt = flags & 2;
    const int small_blocks = flags & 0x40; /* many blocks: in place, several batches */

    /* ------------------------------------------------------------------ */
    /* Phase 1: Compress with seekable=1                                  */
    /* ------------------------------------------------------------------ */
    zxc_cctx* const cctx = zxc_create_cctx();
    if (!cctx) return;
    int64_t csize = -1;
    if (zxc_cctx_set_param(cctx, ZXC_C_LEVEL, level) == ZXC_OK &&
        zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, use_checksum) == ZXC_OK &&
        zxc_cctx_set_param(cctx, ZXC_C_SEEKABLE, 1) == ZXC_OK &&
        (!small_blocks || zxc_cctx_set_param(cctx, ZXC_C_BLOCK_SIZE, 4096) == ZXC_OK)) {
        csize = zxc_compress_cctx(cctx, data, size, comp_buf, bound);
    }
    zxc_free_cctx(cctx);
    if (csize < 0) return;

    /* ------------------------------------------------------------------ */
    /* Phase 2: Open seekable handle                                       */
    /* ------------------------------------------------------------------ */
    zxc_seekable* s = zxc_seekable_open(comp_buf, (size_t)csize);
    if (!s) return;

    /* ------------------------------------------------------------------ */
    /* Phase 3: Exercise metadata getters                                 */
    /* ------------------------------------------------------------------ */
    const uint64_t num_blocks = zxc_seekable_get_num_blocks(s);
    const uint64_t total_decomp = zxc_seekable_get_decompressed_size(s);
    assert(total_decomp == size);

    for (uint64_t i = 0; i < num_blocks; i++) {
        const uint32_t csz = zxc_seekable_get_block_comp_size(s, i);
        const uint32_t dsz = zxc_seekable_get_block_decomp_size(s, i);
        assert(csz > 0);
        assert(dsz > 0);
        (void)csz;
        (void)dsz;
    }
    /* Out-of-range access should return 0 */
    assert(zxc_seekable_get_block_comp_size(s, num_blocks) == 0);
    assert(zxc_seekable_get_block_decomp_size(s, num_blocks) == 0);

    /* ------------------------------------------------------------------ */
    /* Phase 4: Full decompression via seekable range                     */
    /* ------------------------------------------------------------------ */
    int64_t dec_result;
    if (use_mt && size > 4096) {
        dec_result = zxc_seekable_decompress_range_mt(s, decomp_buf, size, 0, size, 2);
    } else {
        dec_result = zxc_seekable_decompress_range(s, decomp_buf, size, 0, size);
    }
    assert(dec_result == (int64_t)size);
    (void)dec_result;
    assert(memcmp(data, decomp_buf, size) == 0);

    /* ------------------------------------------------------------------ */
    /* Phase 5: Partial range decompression (sub-block extraction)        */
    /* ------------------------------------------------------------------ */
    if (size >= 4) {
        /* Extract a range from the middle */
        const size_t off = size / 4;
        const size_t len = size / 2;
        uint8_t* const part = (uint8_t*)malloc(len);
        if (part) {
            dec_result = zxc_seekable_decompress_range(s, part, len, off, len);
            assert(dec_result == (int64_t)len);
            assert(memcmp(data + off, part, len) == 0);
            free(part);
        }
    }

    /* ------------------------------------------------------------------ */
    /* Phase 6: Edge cases                                                */
    /* ------------------------------------------------------------------ */
    /* Zero-length read */
    dec_result = zxc_seekable_decompress_range(s, decomp_buf, size, 0, 0);
    assert(dec_result == 0);

    /* Out-of-bounds read */
    dec_result = zxc_seekable_decompress_range(s, decomp_buf, size, total_decomp, 1);
    assert(dec_result < 0);

    /* NULL handle */
    assert(zxc_seekable_get_num_blocks(NULL) == 0);
    assert(zxc_seekable_get_decompressed_size(NULL) == 0);

    /* ------------------------------------------------------------------ */
    /* Phase 8: Corrupted blocks, decoded into an exact buffer            */
    /* ------------------------------------------------------------------ */
    /* Bytes flipped inside the blocks, the seek table and footer intact so that it
     * opens: no decode may write past len. */
    size_t blocks_end = ZXC_FILE_HEADER_SIZE;
    for (uint64_t i = 0; i < num_blocks; i++) blocks_end += zxc_seekable_get_block_comp_size(s, i);
    if (blocks_end > ZXC_FILE_HEADER_SIZE && blocks_end <= (size_t)csize) {
        uint8_t* const bad = (uint8_t*)malloc((size_t)csize);
        uint8_t* const out = (uint8_t*)malloc(size);
        if (bad && out) {
            memcpy(bad, comp_buf, (size_t)csize);
            const size_t span = blocks_end - ZXC_FILE_HEADER_SIZE;
            const int flips = 1 + ((flags >> 2) & 3);
            for (int k = 0; k < flips; k++) {
                const uint8_t a = data[(size_t)k % size], b = data[(size_t)(k * 7 + 3) % size];
                bad[ZXC_FILE_HEADER_SIZE + ((size_t)a * 257 + b) * 2654435761U % span] ^=
                    (uint8_t)(1U << (b & 7));
            }
            zxc_seekable* const sc = zxc_seekable_open(bad, (size_t)csize);
            if (sc) {
                zxc_seekable_decompress_range_mt(sc, out, size, 0, size, 2 + ((flags >> 4) & 3));
                zxc_seekable_decompress_range(sc, out, size, 0, size);
                zxc_seekable_free(sc);
            }
        }
        free(bad);
        free(out);
    }

    /* ------------------------------------------------------------------ */
    /* Cleanup                                                            */
    /* ------------------------------------------------------------------ */
    zxc_seekable_free(s);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return 0;

    /* Save original input for phase 7 (raw parser fuzzing) */
    const uint8_t* const raw_data = data;
    const size_t raw_size = size;

    /* Use first byte as level, second as flags */
    const int level = (data[0] % 5) + 1;
    const uint8_t flags = data[1];
    data += 2;
    size -= 2;

    if (size == 0 || size > FUZZ_SEEKABLE_MAX_INPUT) return 0;

    const size_t bound = zxc_compress_bound(size);
    if (bound == 0) return 0;
    /* Exact sizes: ASan sees overruns. */
    uint8_t* const comp_buf = (uint8_t*)malloc(bound);
    uint8_t* const decomp_buf = (uint8_t*)malloc(size);
    if (comp_buf && decomp_buf)
        seekable_phases(data, size, level, flags, comp_buf, bound, decomp_buf);
    free(comp_buf);
    free(decomp_buf);

    /* ------------------------------------------------------------------ */
    /* Phase 7: Fuzz the parser with raw data (malformed seekable archives) */
    /* ------------------------------------------------------------------ */
    zxc_seekable* s2 = zxc_seekable_open(raw_data, raw_size);
    if (s2) {
        /* If it parsed, try a read - should not crash */
        const uint64_t td = zxc_seekable_get_decompressed_size(s2);
        if (td > 0 && td <= FUZZ_SEEKABLE_MAX_INPUT) {
            uint8_t* tmp = (uint8_t*)malloc((size_t)td);
            if (tmp) {
                zxc_seekable_decompress_range(s2, tmp, (size_t)td, 0, (size_t)td);
                free(tmp);
            }
        }
        zxc_seekable_free(s2);
    }

    return 0;
}
