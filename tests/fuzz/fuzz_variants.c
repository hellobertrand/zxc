// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file fuzz_variants.c
 * @brief Fuzzer for the per-ISA variants of the block codec.
 *
 * The other targets go through the dispatcher, which is built `_default` only.
 * This one calls the variant entry points directly and lets the input pick
 * among those the CPU supports, so one binary covers every tier and never
 * runs an instruction the machine lacks.
 *
 *  1. The raw input, as a block, through the fast and the strict decoder of
 *     every available variant: same verdict per decoder, same bytes on success.
 *  2. The input compressed by a chosen variant: the bytes must match the
 *     `_default` encoder's, and a chosen variant must decode them bit-exact.
 *
 * Destinations are allocated to their exact capacity, so ASan sees any overrun.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_error.h"
#include "../../src/lib/zxc_internal.h"

#if defined(FUZZ_X86_VARIANTS)
#include <cpuid.h>
#endif

#define FUZZ_VARIANTS_MAX_INPUT (256 << 10) /* 256 KiB of fuzzer bytes */
#define FUZZ_VARIANTS_CTRL 3                /* control bytes consumed below */

typedef int (*decode_fn)(const zxc_cctx_t* RESTRICT, const uint8_t* RESTRICT, const size_t,
                         uint8_t* RESTRICT, const size_t, const uint64_t);
typedef int (*encode_fn)(zxc_cctx_t* RESTRICT, const uint8_t* RESTRICT, const size_t,
                         uint8_t* RESTRICT, const size_t, const uint64_t);

#define DECLARE_VARIANT(sfx)                                                                   \
    int zxc_compress_chunk_wrapper##sfx(zxc_cctx_t* RESTRICT, const uint8_t* RESTRICT,         \
                                        const size_t, uint8_t* RESTRICT, const size_t,         \
                                        const uint64_t);                                       \
    int zxc_decompress_chunk_wrapper##sfx(const zxc_cctx_t* RESTRICT, const uint8_t* RESTRICT, \
                                          const size_t, uint8_t* RESTRICT, const size_t,       \
                                          const uint64_t);                                     \
    int zxc_decompress_chunk_wrapper_safe##sfx(const zxc_cctx_t* RESTRICT,                     \
                                               const uint8_t* RESTRICT, const size_t,          \
                                               uint8_t* RESTRICT, const size_t, const uint64_t)

DECLARE_VARIANT(_default);
#if defined(FUZZ_X86_VARIANTS)
DECLARE_VARIANT(_avx2);
DECLARE_VARIANT(_avx512);
#endif

typedef struct {
    encode_fn encode;
    decode_fn decode;
    decode_fn decode_safe;
} variant_t;

#define VARIANT(sfx)                                                     \
    {zxc_compress_chunk_wrapper##sfx, zxc_decompress_chunk_wrapper##sfx, \
     zxc_decompress_chunk_wrapper_safe##sfx}

#define MAX_VARIANTS 3
static variant_t g_variants[MAX_VARIANTS] = {VARIANT(_default)};
static size_t g_n_variants = 1;

/* Same gates as zxc_detect_cpu_features(): each variant is compiled with
 * BMI1/BMI2/LZCNT on top of its vector set. The builtin checks XCR0 too. */
int LLVMFuzzerInitialize(int* argc, char*** argv) {
    (void)argc;
    (void)argv;
#if defined(FUZZ_X86_VARIANTS)
    __builtin_cpu_init();
    unsigned a = 0, b = 0, c = 0, d = 0;
    const int lzcnt = __get_cpuid(0x80000001U, &a, &b, &c, &d) && ((c >> 5) & 1U);
    const int bmi = lzcnt && __builtin_cpu_supports("bmi") && __builtin_cpu_supports("bmi2");
    if (bmi && __builtin_cpu_supports("avx2")) {
        const variant_t v = VARIANT(_avx2);
        g_variants[g_n_variants++] = v;
    }
    if (bmi && __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") &&
        __builtin_cpu_supports("avx512vbmi") && __builtin_cpu_supports("avx512vbmi2")) {
        const variant_t v = VARIANT(_avx512);
        g_variants[g_n_variants++] = v;
    }
#endif
    return 0;
}

/* Decodes @p src into a fresh buffer of exactly @p cap bytes; the result, the bytes in @p out. */
static int decode(const decode_fn fn, const zxc_cctx_t* ctx, const uint8_t* src, const size_t len,
                  const size_t cap, uint8_t** out) {
    *out = (uint8_t*)malloc(cap);
    if (!*out) return ZXC_ERROR_MEMORY;
    return fn(ctx, src, len, *out, cap, 0);
}

/* Phase 1: @p src as an untrusted block through every variant, fast and strict. */
static void decode_untrusted(const uint8_t* src, const size_t len, const size_t block_size,
                             const int checksum) {
    if (len < ZXC_BLOCK_HEADER_SIZE) return;
    zxc_cctx_t ctx;
    if (zxc_cctx_init(&ctx, block_size, 0, 0, checksum, 0) != ZXC_OK) return;

    uint8_t* ref = NULL;
    int ref_n = -1;
    /* First variant's verdict per decoder; error codes may differ. */
    int accepted[2] = {0, 0};
    int have_verdict[2] = {0, 0};
    for (size_t i = 0; i < g_n_variants; i++) {
        /* The strict decoder is not the RAW path: the block API routes around it. */
        for (int safe = 0; safe < (src[0] == ZXC_BLOCK_RAW ? 1 : 2); safe++) {
            uint8_t* out = NULL;
            const int n = safe ? decode(g_variants[i].decode_safe, &ctx, src, len, block_size, &out)
                               : decode(g_variants[i].decode, &ctx, src, len,
                                        block_size + ZXC_DECOMPRESS_TAIL_PAD, &out);
            if (!out) continue; /* allocation failed */
            if (have_verdict[safe]) {
                assert((n > 0) == accepted[safe]);
            } else {
                accepted[safe] = n > 0;
                have_verdict[safe] = 1;
            }
            if (n > 0 && ref_n > 0) {
                assert(n == ref_n);
                assert(memcmp(out, ref, (size_t)n) == 0);
            }
            if (n > 0 && ref_n <= 0) {
                ref = out;
                ref_n = n;
            } else {
                free(out);
            }
        }
    }
    free(ref);
    zxc_cctx_free(&ctx);
}

/* Phase 2: @p data through encoder @p enc, checked against `_default`, then
 * decoded by @p dec, fast and strict. */
static void roundtrip(const uint8_t* data, const size_t size, const int level, const int checksum,
                      const variant_t* enc, const variant_t* dec) {
    const size_t block_size = zxc_block_size_ceil(size);
    const size_t bound = (size_t)zxc_compress_block_bound(size);
    zxc_cctx_t cctx, dctx;
    if (zxc_cctx_init(&cctx, block_size, 1, level, checksum, 0) != ZXC_OK) return;
    if (zxc_cctx_init(&dctx, block_size, 0, 0, checksum, 0) != ZXC_OK) {
        zxc_cctx_free(&cctx);
        return;
    }
    uint8_t* const comp = (uint8_t*)malloc(bound);
    uint8_t* const comp_ref = (uint8_t*)malloc(bound);
    if (comp && comp_ref) {
        const int c = enc->encode(&cctx, data, size, comp, bound, 0);
        assert(c > 0);
        if (enc != &g_variants[0]) {
            const int c_ref = g_variants[0].encode(&cctx, data, size, comp_ref, bound, 0);
            assert(c_ref == c);
            assert(memcmp(comp, comp_ref, (size_t)c) == 0);
        }

        uint8_t* out = NULL;
        int n =
            decode(dec->decode, &dctx, comp, (size_t)c, block_size + ZXC_DECOMPRESS_TAIL_PAD, &out);
        if (out) {
            assert(n == (int)size);
            assert(memcmp(out, data, size) == 0);
            free(out);
        }
        if (comp[0] != ZXC_BLOCK_RAW) {
            n = decode(dec->decode_safe, &dctx, comp, (size_t)c, size, &out);
            if (out) {
                assert(n == (int)size);
                assert(memcmp(out, data, size) == 0);
                free(out);
            }
        }
    }
    free(comp);
    free(comp_ref);
    zxc_cctx_free(&dctx);
    zxc_cctx_free(&cctx);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < FUZZ_VARIANTS_CTRL || size > FUZZ_VARIANTS_MAX_INPUT) return 0;

    const int level = (int)(data[0] % (unsigned)zxc_max_level()) + 1;
    const int checksum = data[1] & 1;
    const size_t block_size = (size_t)ZXC_BLOCK_SIZE_MIN << ((data[1] >> 1) % 7U); /* 4K..256K */
    const variant_t* const enc = &g_variants[(data[2] & 15U) % g_n_variants];
    const variant_t* const dec = &g_variants[(data[2] >> 4) % g_n_variants];
    data += FUZZ_VARIANTS_CTRL;
    size -= FUZZ_VARIANTS_CTRL;

    decode_untrusted(data, size, block_size, checksum);
    if (size > 0) roundtrip(data, size, level, checksum, enc, dec);
    return 0;
}
