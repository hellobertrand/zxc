/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// The footer cap: real archives clear it, forged sizes do not.

#include "test_common.h"

// The cap sits exactly on the format's floor: a denser encoder would break it.
int test_footer_floor_density(void) {
    printf("=== TEST: Unit - Footer cap accepts the densest real archives ===\n");
    const size_t block_sizes[] = {ZXC_BLOCK_SIZE_MIN, 65536, ZXC_BLOCK_SIZE_MAX};
    int ok = 1;
    long checked = 0;

    for (size_t b = 0; b < sizeof(block_sizes) / sizeof(block_sizes[0]); b++) {
        const size_t bs = block_sizes[b];
        const size_t sizes[] = {1, 43, 44, 45, bs - 1, bs, bs + 1, bs + 44, 3 * bs + 7};
        for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
            const size_t n = sizes[si];
            uint8_t* const in = (uint8_t*)calloc(1, n); /* zeros: densest blocks */
            uint8_t* const out = (uint8_t*)malloc((size_t)zxc_compress_bound(n));
            if (!in || !out) {
                free(in);
                free(out);
                printf("  [SKIP] allocation failed\n");
                return 1;
            }
            for (int lvl = 1; lvl <= zxc_max_level() && ok; lvl++)
                for (int cs = 0; cs < 2 && ok; cs++)
                    for (int sk = 0; sk < 2 && ok; sk++) {
                        zxc_compress_opts_t o = {
                            .level = lvl, .block_size = bs, .checksum_enabled = cs, .seekable = sk};
                        const int64_t c =
                            zxc_compress(in, n, out, (size_t)zxc_compress_bound(n), &o);
                        const uint64_t got = c > 0 ? zxc_get_decompressed_size(out, (size_t)c) : 0;
                        checked++;
                        if (c <= 0 || got != n) {
                            printf(
                                "  [FAIL] bs=%zu n=%zu L%d cs=%d sk=%d: archive %lld, size %llu\n",
                                bs, n, lvl, cs, sk, (long long)c, (unsigned long long)got);
                            ok = 0;
                        }
                    }
            free(in);
            free(out);
        }
    }
    if (ok) printf("  [PASS] %ld archives, none refused\n", checked);
    if (ok) printf("PASS\n\n");
    return ok;
}

// The u64 getter reports a valid empty archive and a rejected one the same way.
int test_decompressed_size(void) {
    printf("=== TEST: Unit - zxc_decompressed_size ===\n");
    const size_t n = 64 * 1024;
    uint8_t* const in = (uint8_t*)malloc(n);
    uint8_t* const arc = (uint8_t*)malloc((size_t)zxc_compress_bound(n));
    if (!in || !arc) {
        free(in);
        free(arc);
        printf("  [SKIP] allocation failed\n");
        return 1;
    }
    gen_lz_data(in, n);
    const zxc_compress_opts_t co = {.level = 3};
    const int64_t csize = zxc_compress(in, n, arc, (size_t)zxc_compress_bound(n), &co);
    int ok = csize > 0;

    if (ok && zxc_decompressed_size(arc, (size_t)csize) != (int64_t)n) {
        printf("  [FAIL] payload size\n");
        ok = 0;
    }
    if (ok) {
        uint8_t empty[64];
        const int64_t c = zxc_compress(NULL, 0, empty, sizeof(empty), &co);
        if (c <= 0 || zxc_decompressed_size(empty, (size_t)c) != 0) {
            printf("  [FAIL] empty archive\n");
            ok = 0;
        }
    }
    if (ok) {
        const struct {
            size_t size;
            int64_t want;
            const char* what;
        } bad[] = {{4, ZXC_ERROR_SRC_TOO_SMALL, "truncated"}, {64, ZXC_ERROR_BAD_MAGIC, "junk"}};
        uint8_t junk[64] = {0};
        for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
            const int64_t got = zxc_decompressed_size(junk, bad[k].size);
            if (got != bad[k].want) {
                printf("  [FAIL] %s: %lld, want %lld\n", bad[k].what, (long long)got,
                       (long long)bad[k].want);
                ok = 0;
            }
        }
    }
    if (ok) {
        // A forged footer stays refused.
        size_t fn = 0;
        uint8_t* const forged =
            make_dense_frame(4, ZXC_BLOCK_SIZE_MAX, 0, 5ULL * ZXC_BLOCK_SIZE_MAX, &fn);
        if (!forged || zxc_decompressed_size(forged, fn) != ZXC_ERROR_CORRUPT_DATA) {
            printf("  [FAIL] forged footer\n");
            ok = 0;
        }
        free(forged);
    }

    free(in);
    free(arc);
    if (ok) printf("PASS\n\n");
    return ok;
}

// Same cap through the FILE* reader, where has_cs drives both of its terms.
int test_stream_footer_cap(void) {
    printf("=== TEST: Unit - zxc_stream_get_decompressed_size cap ===\n");
    int ok = 1;
    for (int cs = 0; cs < 2 && ok; cs++) {
        const uint64_t nb = 16;
        const uint64_t at = nb * ZXC_BLOCK_SIZE_MAX + 1;
        const struct {
            uint64_t claim;
            int64_t want;
        } cases[] = {{at, (int64_t)at}, {at + 1, ZXC_ERROR_CORRUPT_DATA}};

        for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]) && ok; k++) {
            size_t n = 0;
            uint8_t* const arc = make_dense_frame(nb, ZXC_BLOCK_SIZE_MAX, cs, cases[k].claim, &n);
            FILE* const f = tmpfile();
            if (!arc || !f) {
                printf("  [SKIP] fixture\n");
                free(arc);
                if (f) fclose(f);
                return 1;
            }
            fwrite(arc, 1, n, f);
            fseek(f, 0, SEEK_SET);
            const int64_t got = zxc_stream_get_decompressed_size(f);
            if (got != cases[k].want) {
                printf("  [FAIL] cs=%d claim %llu: got %lld, want %lld\n", cs,
                       (unsigned long long)cases[k].claim, (long long)got,
                       (long long)cases[k].want);
                ok = 0;
            }
            fclose(f);
            free(arc);
        }
    }
    if (ok) printf("PASS\n\n");
    return ok;
}
