/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

/* Pins the encoder's output: round-trip tests accept any valid parse, so a
 * change that makes archives bigger, or merely different, passes them all.
 * Run on every CI platform, it also proves they all encode alike. A mismatch
 * is not a format break: when the change is deliberate, paste the table the
 * failure prints over k_pinned. */

#define PIN_INPUT_SIZE 400000 /* not a multiple of any block size */
#define PIN_N_INPUTS 3
#define PIN_N_LEVELS 7

typedef struct {
    uint64_t hash; /* rapidhash chained over every archive of the level, in order */
    size_t size;   /* their total size */
} pin_t;

static const pin_t k_pinned[PIN_N_LEVELS] = {
    {0x8F91B4E3B3E19B22ULL, 673055}, /* level 1 */
    {0x45A746801905758FULL, 672864}, /* level 2 */
    {0x403AF9679D8F9EA1ULL, 522430}, /* level 3 */
    {0xDD922640546F2517ULL, 515559}, /* level 4 */
    {0x6E31EBC85431F951ULL, 418887}, /* level 5 */
    {0x58E318AFB0110B9DULL, 385018}, /* level 6 */
    {0x25FDEB9D30D2B005ULL, 334682}, /* level 7 */
};

/* Generators write byte by byte from the test PRNG: same bytes on every
 * platform, whatever its endianness. */

/* Random words from a small vocabulary: matches and skewed literals. */
static void pin_gen_text(uint8_t* buf, const size_t n) {
    static const char* const words[16] = {
        "the",   "compression", "window", "offset", "literal", "match", "stream", "block",
        "table", "decode",      "fast",   "of",     "and",     "to",    "a",      "zxc"};
    for (size_t i = 0; i < n;) {
        for (const char* w = words[zxc_test_rand() & 15]; *w && i < n; w++) buf[i++] = (uint8_t)*w;
        if (i < n) buf[i++] = ' ';
    }
}

/* Fixed-layout records: short offsets, zero runs, a few noisy fields. */
static void pin_gen_records(uint8_t* buf, const size_t n) {
    uint32_t id = 0;
    for (size_t i = 0; i < n; id++) {
        uint8_t rec[48] = {0};
        const uint32_t r = zxc_test_rand();
        rec[0] = 0xA5;
        rec[1] = 0x5A;
        rec[2] = (uint8_t)id;
        rec[3] = (uint8_t)(id >> 8);
        rec[8] = (uint8_t)r;
        rec[9] = (uint8_t)(r >> 8);
        rec[10] = (uint8_t)((r >> 16) & 3);
        memcpy(rec + 16, "status=ok;", 10);
        rec[40] = (uint8_t)((r >> 24) & 7);
        const size_t k = (n - i < sizeof(rec)) ? n - i : sizeof(rec);
        memcpy(buf + i, rec, k);
        i += k;
    }
}

/* Noise, runs and copies of earlier data. Copy lengths reach 600, across the
 * 20 and 148 cost steps and the 256 long-match threshold. */
static void pin_gen_mixed(uint8_t* buf, const size_t n) {
    size_t i = 0;
    while (i < n) {
        const uint32_t r = zxc_test_rand();
        size_t len = 1 + (r >> 8) % 600;
        if ((r & 3) == 0) len = 1 + len % 48;
        if (len > n - i) len = n - i;
        if ((r & 3) == 0 || i == 0) {
            for (size_t k = 0; k < len; k++) buf[i++] = (uint8_t)zxc_test_rand();
        } else if ((r & 3) == 1) {
            memset(buf + i, (int)(r >> 24), len);
            i += len;
        } else {
            const size_t reach = (i < 70000) ? i : 70000;
            const size_t dist = 1 + zxc_test_rand() % reach;
            for (size_t k = 0; k < len; k++, i++) buf[i] = buf[i - dist]; /* may overlap */
        }
    }
}

int test_encoder_output_pinned(void) {
    printf("=== TEST: Encoder - compressed output pinned at every level ===\n");

    static const size_t k_block_sizes[] = {0 /* default */, 32 * 1024};
    const size_t n_block_sizes = sizeof(k_block_sizes) / sizeof(k_block_sizes[0]);
    const size_t cap = (size_t)zxc_compress_bound(PIN_INPUT_SIZE);

    uint8_t* const src = (uint8_t*)malloc((size_t)PIN_N_INPUTS * PIN_INPUT_SIZE);
    uint8_t* const comp = (uint8_t*)malloc(cap);
    uint8_t* const back = (uint8_t*)malloc(PIN_INPUT_SIZE);
    int ok = src && comp && back;
    if (!ok) printf("  [FAIL] allocation\n");

    if (ok) {
        zxc_test_srand(0x5A58432D50494E31ULL); /* own seed: independent of test order */
        pin_gen_text(src, PIN_INPUT_SIZE);
        pin_gen_records(src + PIN_INPUT_SIZE, PIN_INPUT_SIZE);
        pin_gen_mixed(src + 2 * (size_t)PIN_INPUT_SIZE, PIN_INPUT_SIZE);
    }

    pin_t got[PIN_N_LEVELS];
    for (int level = 1; ok && level <= PIN_N_LEVELS; level++) {
        pin_t p = {0, 0};
        for (size_t in = 0; ok && in < PIN_N_INPUTS; in++) {
            const uint8_t* const data = src + in * PIN_INPUT_SIZE;
            for (size_t b = 0; ok && b < n_block_sizes; b++) {
                const zxc_compress_opts_t co = {
                    .level = level, .block_size = k_block_sizes[b], .checksum_enabled = 1};
                const int64_t c = zxc_compress(data, PIN_INPUT_SIZE, comp, cap, &co);
                const zxc_decompress_opts_t dop = {.checksum_enabled = 1};
                if (c <= 0 ||
                    zxc_decompress(comp, (size_t)c, back, PIN_INPUT_SIZE, &dop) != PIN_INPUT_SIZE ||
                    memcmp(back, data, PIN_INPUT_SIZE) != 0) {
                    printf("  [FAIL] level %d, input %zu, block size %zu: round trip (%lld)\n",
                           level, in, k_block_sizes[b], (long long)c);
                    ok = 0;
                    break;
                }
                p.hash = rapidhash_withSeed(comp, (size_t)c, p.hash);
                p.size += (size_t)c;
            }
        }
        got[level - 1] = p;
    }

    int changed = 0;
    for (int l = 0; ok && l < PIN_N_LEVELS; l++) {
        if (got[l].hash == k_pinned[l].hash && got[l].size == k_pinned[l].size) continue;
        changed = 1;
        printf("  [FAIL] level %d: %zu -> %zu bytes (%+lld)\n", l + 1, k_pinned[l].size,
               got[l].size, (long long)got[l].size - (long long)k_pinned[l].size);
    }
    if (changed) {
        printf("  The encoder's output changed. If deliberate, replace k_pinned with:\n");
        for (int l = 0; l < PIN_N_LEVELS; l++)
            printf("    {0x%016llXULL, %zu}, /* level %d */\n", (unsigned long long)got[l].hash,
                   got[l].size, l + 1);
        ok = 0;
    }

    free(src);
    free(comp);
    free(back);
    if (ok) printf("PASS\n\n");
    return ok;
}
