// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

#include "../include/zxc_dict.h"
#include "test_common.h"

/* Pins the encoder's output: round-trip tests accept any valid parse, so a
 * change that makes archives bigger, or merely different, passes them all.
 * Run on every CI platform, it also proves they all encode alike. A mismatch
 * is not a format break: when the change is deliberate, paste the tables the
 * failure prints over k_pinned and k_pinned_dict. */

#define PIN_INPUT_SIZE 130000 /* not a multiple of any block size */
#define PIN_N_INPUTS 3
#define PIN_N_LEVELS 7
#define PIN_DICT_CAP 16384
#define PIN_SAMPLE_SIZE 4096
#define PIN_SAMPLES_PER_INPUT 8

typedef struct {
    uint64_t hash; /* rapidhash chained over every archive of the level, in order */
    size_t size;   /* their total size */
} pin_t;

static const pin_t k_pinned[PIN_N_LEVELS] = {
    {0x99C6C1997DB628A9ULL, 223845}, /* level 1 */
    {0x91796E093EFA64ADULL, 222405}, /* level 2 */
    {0x06C22FC583B18C75ULL, 173876}, /* level 3 */
    {0x0E5382C65830FBBEULL, 171770}, /* level 4 */
    {0x45C16437087ADCDDULL, 139540}, /* level 5 */
    {0x3E2770F1157913D6ULL, 130413}, /* level 6 */
    {0xBE746F21D9A50B72ULL, 111924}, /* level 7 */
};

/* Same inputs in 4 KB blocks, with a dictionary trained on them. */
static const pin_t k_pinned_dict[PIN_N_LEVELS] = {
    {0x7B9F551034F95A75ULL, 119480}, /* level 1 */
    {0xFEDE0B894EB0F9F4ULL, 119250}, /* level 2 */
    {0x45AA6E12DBCD1E55ULL, 92521},  /* level 3 */
    {0x3DD0C1931482DB24ULL, 89763},  /* level 4 */
    {0x0A5281108179335AULL, 81045},  /* level 5 */
    {0x83F3B69BB3485BAFULL, 77428},  /* level 6 */
    {0x365B064EF9A02834ULL, 76643},  /* level 7 */
};

/* Own splitmix64: the shared test PRNG stays untouched for the tests that run
 * after this one in the same process. */
static uint64_t g_pin_rng;

static uint32_t pin_rand(void) {
    uint64_t z = (g_pin_rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}

/* Generators write byte by byte: same bytes on every platform, whatever its
 * endianness. */

/* Random words from a small vocabulary: matches and skewed literals. */
static void pin_gen_text(uint8_t* buf, const size_t n) {
    static const char* const words[16] = {
        "the",   "compression", "window", "offset", "literal", "match", "stream", "block",
        "table", "decode",      "fast",   "of",     "and",     "to",    "a",      "zxc"};
    for (size_t i = 0; i < n;) {
        for (const char* w = words[pin_rand() & 15]; *w && i < n; w++) buf[i++] = (uint8_t)*w;
        if (i < n) buf[i++] = ' ';
    }
}

/* Fixed-layout records: short offsets, zero runs, a few noisy fields. */
static void pin_gen_records(uint8_t* buf, const size_t n) {
    uint32_t id = 0;
    for (size_t i = 0; i < n; id++) {
        uint8_t rec[48] = {0};
        const uint32_t r = pin_rand();
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
        const uint32_t r = pin_rand();
        size_t len = 1 + (r >> 8) % 600;
        if ((r & 3) == 0) len = 1 + len % 48;
        if (len > n - i) len = n - i;
        if ((r & 3) == 0 || i == 0) {
            for (size_t k = 0; k < len; k++) buf[i++] = (uint8_t)pin_rand();
        } else if ((r & 3) == 1) {
            memset(buf + i, (int)(r >> 24), len);
            i += len;
        } else {
            const size_t reach = (i < 70000) ? i : 70000;
            const size_t dist = 1 + pin_rand() % reach;
            for (size_t k = 0; k < len; k++, i++) buf[i] = buf[i - dist]; /* may overlap */
        }
    }
}

/* Compresses every input at every level, checks each round trip, and folds
 * the archives of a level into its pin. */
static int pin_pass(const uint8_t* src, uint8_t* comp, const size_t cap, uint8_t* back,
                    const size_t* block_sizes, const size_t n_block_sizes, const void* dict,
                    const size_t dict_size, const void* dict_huf, pin_t got[PIN_N_LEVELS]) {
    const zxc_decompress_opts_t dop = {
        .checksum_enabled = 1, .dict = dict, .dict_size = dict_size, .dict_huf = dict_huf};
    for (int level = 1; level <= PIN_N_LEVELS; level++) {
        pin_t p = {0, 0};
        for (size_t in = 0; in < PIN_N_INPUTS; in++) {
            const uint8_t* const data = src + in * PIN_INPUT_SIZE;
            for (size_t b = 0; b < n_block_sizes; b++) {
                const zxc_compress_opts_t co = {.level = level,
                                                .block_size = block_sizes[b],
                                                .checksum_enabled = 1,
                                                .dict = dict,
                                                .dict_size = dict_size,
                                                .dict_huf = dict_huf};
                const int64_t c = zxc_compress(data, PIN_INPUT_SIZE, comp, cap, &co);
                if (c <= 0 ||
                    zxc_decompress(comp, (size_t)c, back, PIN_INPUT_SIZE, &dop) != PIN_INPUT_SIZE ||
                    memcmp(back, data, PIN_INPUT_SIZE) != 0) {
                    printf("  [FAIL] level %d, input %zu, block size %zu%s: round trip (%lld)\n",
                           level, in, block_sizes[b], dict ? ", dict" : "", (long long)c);
                    return 0;
                }
                p.hash = rapidhash_withSeed(comp, (size_t)c, p.hash);
                p.size += (size_t)c;
            }
        }
        got[level - 1] = p;
    }
    return 1;
}

/* Compares a pass with its table; on a mismatch, prints the table to paste. */
static int pin_check(const char* table, const pin_t* want, const pin_t* got) {
    int same = 1;
    for (int l = 0; l < PIN_N_LEVELS; l++) {
        if (got[l].hash == want[l].hash && got[l].size == want[l].size) continue;
        same = 0;
        printf("  [FAIL] %s, level %d: %zu -> %zu bytes (%+lld)\n", table, l + 1, want[l].size,
               got[l].size, (long long)got[l].size - (long long)want[l].size);
    }
    if (!same) {
        printf("  The encoder's output changed. If deliberate, replace %s with:\n", table);
        for (int l = 0; l < PIN_N_LEVELS; l++)
            printf("    {0x%016llXULL, %zu}, /* level %d */\n", (unsigned long long)got[l].hash,
                   got[l].size, l + 1);
    }
    return same;
}

int test_encoder_output_pinned(void) {
    printf("=== TEST: Encoder - compressed output pinned at every level ===\n");

    static const size_t k_block_sizes[] = {0 /* default */, 32 * 1024};
    static const size_t k_dict_block_size[] = {4096};
    const size_t cap = (size_t)zxc_compress_bound(PIN_INPUT_SIZE);

    uint8_t* const src = (uint8_t*)malloc((size_t)PIN_N_INPUTS * PIN_INPUT_SIZE);
    uint8_t* const comp = (uint8_t*)malloc(cap);
    uint8_t* const back = (uint8_t*)malloc(PIN_INPUT_SIZE);
    uint8_t* const dict = (uint8_t*)malloc(PIN_DICT_CAP);
    int ok = src && comp && back && dict;
    if (!ok) printf("  [FAIL] allocation\n");

    if (ok) {
        g_pin_rng = 0x5A58432D50494E31ULL;
        pin_gen_text(src, PIN_INPUT_SIZE);
        pin_gen_records(src + PIN_INPUT_SIZE, PIN_INPUT_SIZE);
        pin_gen_mixed(src + 2 * (size_t)PIN_INPUT_SIZE, PIN_INPUT_SIZE);
    }

    // A mismatch does not stop the dictionary pass: one run prints both tables.
    pin_t got[PIN_N_LEVELS];
    ok = ok && pin_pass(src, comp, cap, back, k_block_sizes,
                        sizeof(k_block_sizes) / sizeof(k_block_sizes[0]), NULL, 0, NULL, got);
    int same = ok && pin_check("k_pinned", k_pinned, got);

    // Trained here: its dict_id is in every archive, so the pin covers the
    // trainer too.
    enum { n_samples = PIN_N_INPUTS * PIN_SAMPLES_PER_INPUT };
    const void* samples[n_samples];
    size_t sample_sizes[n_samples];
    for (size_t k = 0; k < n_samples; k++) {
        samples[k] = src + (k / PIN_SAMPLES_PER_INPUT) * PIN_INPUT_SIZE +
                     (k % PIN_SAMPLES_PER_INPUT) * PIN_SAMPLE_SIZE;
        sample_sizes[k] = PIN_SAMPLE_SIZE;
    }
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    const int64_t dict_size =
        ok ? zxc_train_dict(samples, sample_sizes, n_samples, dict, PIN_DICT_CAP) : -1;
    if (ok && (dict_size <= 0 || zxc_train_dict_huf(samples, sample_sizes, n_samples, dict,
                                                    (size_t)dict_size, huf) != ZXC_OK)) {
        printf("  [FAIL] dictionary training (%lld)\n", (long long)dict_size);
        ok = 0;
    }
    ok = ok &&
         pin_pass(src, comp, cap, back, k_dict_block_size, 1, dict, (size_t)dict_size, huf, got);
    if (ok) same = pin_check("k_pinned_dict", k_pinned_dict, got) && same;

    free(src);
    free(comp);
    free(back);
    free(dict);
    if (ok && same) printf("PASS\n\n");
    return ok && same;
}
