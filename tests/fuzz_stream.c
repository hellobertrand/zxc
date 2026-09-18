/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fuzz_stream.c
 * @brief Fuzzer for the multi-threaded FILE* streaming API (zxc_stream_*).
 *
 * The FILE* engine (reader, worker threads, reordering writer) walks frames with
 * its own code. Streams live in memory (fmemopen / open_memstream), as in the
 * CLI benchmark.
 *
 *  1. Feed the raw input to zxc_stream_get_decompressed_size and
 *     zxc_stream_decompress.
 *  2. Compress the payload, tiled 1-8 times so short inputs still span several
 *     blocks; level, block size, threads, checksum, seek table and dictionary
 *     come from the header bytes. Decompress it back: bit-exact, footer size
 *     included.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_constants.h"
#include "../include/zxc_stream.h"

#define FUZZ_STREAM_MAX_INPUT (256 << 10) /* 256 KiB, before tiling */

static const size_t kBlockSizes[] = {4096, 8192, 16384, 65536};

static void on_progress(uint64_t done, uint64_t total, const void* user_data) {
    (void)done;
    (void)total;
    (void)user_data;
}

static void fuzz_raw(const uint8_t* src, const size_t n, const int n_threads, const int checksum) {
    FILE* const in = fmemopen((void*)src, n, "rb");
    if (!in) return;
    (void)zxc_stream_get_decompressed_size(in); /* restores the position */
    const zxc_decompress_opts_t o = {.n_threads = n_threads, .checksum_enabled = checksum};
    (void)zxc_stream_decompress(in, NULL, &o); /* no output: a bomb costs CPU, not memory */
    fclose(in);
}

static void roundtrip(uint8_t* plain, const size_t total, const zxc_compress_opts_t* co,
                      const zxc_decompress_opts_t* dopts) {
    char* comp = NULL;
    size_t comp_len = 0;
    FILE* in = fmemopen(plain, total, "rb");
    FILE* out = open_memstream(&comp, &comp_len);
    const int64_t csize = (in && out) ? zxc_stream_compress(in, out, co) : -1;
    if (in) fclose(in);
    if (out) fclose(out); /* comp / comp_len are final only now */
    if (csize < 0 || !comp) {
        free(comp);
        return;
    }
    assert((size_t)csize == comp_len);

    char* dec = NULL;
    size_t dec_len = 0;
    in = fmemopen(comp, comp_len, "rb");
    out = open_memstream(&dec, &dec_len);
    if (in && out) {
        const int64_t reported = zxc_stream_get_decompressed_size(in);
        const int64_t dsize = zxc_stream_decompress(in, out, dopts);
        fclose(out);
        out = NULL;
        assert(reported == (int64_t)total);
        assert(dsize == (int64_t)total);
        assert(dec_len == total && memcmp(dec, plain, total) == 0);
        (void)reported;
        (void)dsize;
    }
    if (in) fclose(in);
    if (out) fclose(out);
    free(dec);
    free(comp);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4) return 0;

    const int level = (int)(data[0] % (unsigned)zxc_max_level()) + 1;
    const int checksum = data[1] & 1;
    const int seekable = (data[1] >> 1) & 1;
    const int use_dict = (data[1] >> 2) & 1;
    const int n_threads = ((data[1] >> 3) & 1) + 1;
    const zxc_progress_callback_t cb = ((data[1] >> 4) & 1) ? on_progress : NULL;
    const size_t block_size = kBlockSizes[(data[1] >> 5) & 3];
    const size_t tiles = (size_t)(data[2] & 7) + 1;
    const size_t dict_cap = ((size_t)data[3] + 1) * 256;

    fuzz_raw(data, size, n_threads, checksum);

    data += 4;
    size -= 4;
    if (size == 0 || size > FUZZ_STREAM_MAX_INPUT) return 0;

    size_t dict_size = use_dict ? dict_cap : 0;
    if (dict_size > size) dict_size = size;
    if (dict_size > ZXC_DICT_SIZE_MAX) dict_size = ZXC_DICT_SIZE_MAX;

    const size_t total = size * tiles;
    uint8_t* const plain = (uint8_t*)malloc(total);
    if (!plain) return 0;
    for (size_t t = 0; t < tiles; t++) {
        memcpy(plain + t * size, data, size);
        plain[t * size] ^= (uint8_t)t; /* distinct blocks */
    }

    const zxc_compress_opts_t co = {.n_threads = n_threads,
                                    .level = level,
                                    .block_size = block_size,
                                    .checksum_enabled = checksum,
                                    .seekable = seekable,
                                    .dict = dict_size ? data : NULL,
                                    .dict_size = dict_size,
                                    .progress_cb = cb};
    const zxc_decompress_opts_t dopts = {.n_threads = n_threads,
                                         .checksum_enabled = checksum,
                                         .dict = co.dict,
                                         .dict_size = dict_size,
                                         .progress_cb = cb};
    roundtrip(plain, total, &co, &dopts);

    free(plain);
    return 0;
}
