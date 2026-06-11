/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fuzz_dict.c
 * @brief Fuzzer for the dictionary API in zxc_dict.c.
 *
 * Strategy (exercises the .zxd format and the trainer, not just the in-memory
 * dict bounce path):
 *
 *  Phase 0 -- Raw .zxd parser. Feed the untrusted bytes straight to
 *             zxc_dict_load() / zxc_dict_get_id(). Must never crash on garbage.
 *  Phase 1 -- Train. Split the input into samples and call zxc_train_dict().
 *  Phase 2 -- Serialize roundtrip. Save the trained dict to .zxd, load it back,
 *             and verify content / dict_id agree across save/load/get_id/id.
 *             Also drives the DST_TOO_SMALL path and single-byte corruption.
 *  Phase 3 -- Use the trained dict for a real compress -> decompress roundtrip.
 *
 * The control header carries level, sample count, dict capacity, and the
 * corruption position/mask, so the same input deterministically reaches the
 * deep validation branches that random bytes (which lack the .zxd magic) miss.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_constants.h"
#include "../include/zxc_dict.h"
#include "../include/zxc_error.h"

#define FUZZ_DICT_MAX_INPUT (256 << 10) /* 256 KiB */
#define FUZZ_DICT_CTRL 8                /* control-header bytes consumed below */
#define FUZZ_DICT_MAX_SAMPLES 8

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static uint8_t* dict_buf = NULL;  /* trained dict content (<= ZXC_DICT_SIZE_MAX) */
    static uint8_t* zxd_buf = NULL;   /* serialized .zxd (header + content)          */
    static void* comp_buf = NULL;
    static size_t comp_cap = 0;
    static void* decomp_buf = NULL;
    static size_t decomp_cap = 0;

    if (size > FUZZ_DICT_MAX_INPUT) return 0;

    /* ------------------------------------------------------------------ */
    /* Phase 0: raw .zxd parser on untrusted bytes (must not crash).      */
    /* zxc_dict_load tolerates buf_size < header, so any size is safe.    */
    /* ------------------------------------------------------------------ */
    {
        const void* content = NULL;
        size_t content_size = 0;
        uint32_t id = 0;
        const int rc = zxc_dict_load(data, size, &content, &content_size, NULL, &id);
        if (rc == ZXC_OK) {
            assert(content != NULL);
            assert(content_size > 0 && content_size <= ZXC_DICT_SIZE_MAX);
            /* A validated header round-trips through both ID accessors. */
            assert(zxc_dict_get_id(data, size) == id);
            assert(zxc_dict_id(content, content_size) == id);
        }
        (void)zxc_dict_get_id(data, size);
    }

    /* ------------------------------------------------------------------ */
    /* Parse the control header.                                          */
    /* ------------------------------------------------------------------ */
    if (size < FUZZ_DICT_CTRL) return 0;

    const int level = (int)(data[0] % 6) + 1;
    const size_t n_samples = (size_t)(data[1] % FUZZ_DICT_MAX_SAMPLES) + 1;
    size_t dict_cap = (size_t)(data[2] | (data[3] << 8));
    const size_t corrupt_pos = (size_t)(data[4] | (data[5] << 8));
    const uint8_t corrupt_mask = data[6];
    const int use_checksum = data[7] & 1;
    data += FUZZ_DICT_CTRL;
    size -= FUZZ_DICT_CTRL;

    if (size == 0) return 0;

    if (dict_cap == 0) dict_cap = 1;
    if (dict_cap > ZXC_DICT_SIZE_MAX) dict_cap = ZXC_DICT_SIZE_MAX;

    /* ------------------------------------------------------------------ */
    /* Phase 1: split input into samples and train.                       */
    /* ------------------------------------------------------------------ */
    const void* samples[FUZZ_DICT_MAX_SAMPLES];
    size_t sample_sizes[FUZZ_DICT_MAX_SAMPLES];
    const size_t chunk = size / n_samples;
    for (size_t i = 0; i < n_samples; i++) {
        samples[i] = data + chunk * i;
        sample_sizes[i] = (i + 1 < n_samples) ? chunk : (size - chunk * (n_samples - 1));
    }

    if (!dict_buf) {
        dict_buf = (uint8_t*)malloc(ZXC_DICT_SIZE_MAX);
        if (!dict_buf) return 0;
    }

    const int64_t dict_sz = zxc_train_dict(samples, sample_sizes, n_samples, dict_buf, dict_cap);
    if (dict_sz <= 0) return 0; /* corpus too small / no patterns: nothing to serialize */
    assert((size_t)dict_sz <= dict_cap);

    /* The .zxd format requires the shared literal table; train it on the same
     * samples (it needs the trained content for the post-LZ literal stats). */
    uint8_t huf[ZXC_DICT_HUF_TABLE_SIZE];
    if (zxc_train_dict_huf(samples, sample_sizes, n_samples, dict_buf, (size_t)dict_sz, huf) !=
        ZXC_OK)
        return 0;

    /* ------------------------------------------------------------------ */
    /* Phase 2: .zxd save / load roundtrip + corruption.                  */
    /* ------------------------------------------------------------------ */
    const size_t zxd_bound = zxc_dict_save_bound((size_t)dict_sz);
    if (!zxd_buf) {
        zxd_buf = (uint8_t*)malloc(ZXC_DICT_HEADER_SIZE + ZXC_DICT_SIZE_MAX + ZXC_DICT_HUF_TABLE_SIZE);
        if (!zxd_buf) return 0;
    }

    const int64_t zxd_sz = zxc_dict_save(dict_buf, (size_t)dict_sz, huf, zxd_buf, zxd_bound);
    assert(zxd_sz == (int64_t)zxd_bound);

    {
        const void* lc = NULL;
        size_t lcs = 0;
        const void* lh = NULL;
        uint32_t lid = 0;
        const int rc = zxc_dict_load(zxd_buf, (size_t)zxd_sz, &lc, &lcs, &lh, &lid);
        assert(rc == ZXC_OK);
        assert(lcs == (size_t)dict_sz);
        assert(memcmp(lc, dict_buf, (size_t)dict_sz) == 0);
        assert(lh != NULL && memcmp(lh, huf, ZXC_DICT_HUF_TABLE_SIZE) == 0);
        /* The stored id binds the (content, table) pair, not the content alone. */
        assert(zxc_dict_get_id(zxd_buf, (size_t)zxd_sz) == lid);
    }

    /* DST_TOO_SMALL: any capacity below the full file must be rejected. */
    {
        const size_t small_cap = corrupt_pos % (size_t)zxd_sz; /* in [0, zxd_sz) */
        const int64_t r = zxc_dict_save(dict_buf, (size_t)dict_sz, huf, zxd_buf, small_cap);
        assert(r < 0);
    }

    /* Re-save (the DST_TOO_SMALL attempt left zxd_buf untouched, but be safe). */
    assert(zxc_dict_save(dict_buf, (size_t)dict_sz, huf, zxd_buf, zxd_bound) == (int64_t)zxd_sz);

    /* Flip one byte and re-load: must not crash. A surviving ZXC_OK can only
     * come from a reserved-byte flip (offsets 12-13, zeroed before the CRC),
     * which cannot change the recovered content. */
    {
        const size_t pos = corrupt_pos % (size_t)zxd_sz;
        const uint8_t saved = zxd_buf[pos];
        zxd_buf[pos] ^= (uint8_t)(corrupt_mask | 1u); /* guaranteed to differ */

        const void* cc = NULL;
        size_t ccs = 0;
        uint32_t cid = 0;
        const int rc = zxc_dict_load(zxd_buf, (size_t)zxd_sz, &cc, &ccs, NULL, &cid);
        if (rc == ZXC_OK) {
            assert(ccs == (size_t)dict_sz);
            assert(memcmp(cc, dict_buf, (size_t)dict_sz) == 0);
        }
        zxd_buf[pos] = saved;
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3: real compress -> decompress roundtrip with the trained    */
    /* dict (also keeps the in-memory dict bounce path covered).          */
    /* ------------------------------------------------------------------ */
    const uint64_t bound64 = zxc_compress_bound(size);
    if (bound64 == 0 || bound64 > SIZE_MAX) return 0;
    const size_t bound = (size_t)bound64;
    if (bound > comp_cap) {
        void* nb = realloc(comp_buf, bound);
        if (!nb) return 0;
        comp_buf = nb;
        comp_cap = bound;
    }

    zxc_compress_opts_t copts = {
        .level = level,
        .checksum_enabled = use_checksum,
        .dict = dict_buf,
        .dict_size = (size_t)dict_sz,
    };
    const int64_t csize = zxc_compress(data, size, comp_buf, bound, &copts);
    if (csize < 0) return 0;

    if (size > decomp_cap) {
        void* nb = realloc(decomp_buf, size);
        if (!nb) return 0;
        decomp_buf = nb;
        decomp_cap = size;
    }

    zxc_decompress_opts_t dopts = {
        .checksum_enabled = use_checksum,
        .dict = dict_buf,
        .dict_size = (size_t)dict_sz,
    };
    const int64_t dsize = zxc_decompress(comp_buf, (size_t)csize, decomp_buf, size, &dopts);

    if (dsize >= 0) {
        assert((size_t)dsize == size);
        assert(memcmp(data, decomp_buf, size) == 0);
    }

    return 0;
}
