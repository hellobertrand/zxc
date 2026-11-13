// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/* Each vector kernel against a scalar definition of its result. The kernels
 * are static: the codec sources are compiled into this file, with its flags. */

#if defined(__GNUC__) || defined(__clang__)

#define ZXC_FUNCTION_SUFFIX _kt
#define ZXC_PRIMARY__kt 1
/* Every rename must precede the first inclusion of zxc_internal.h. */
#define ZXC_CAT_IMPL(x, y) x##y
#define ZXC_CAT(x, y) ZXC_CAT_IMPL(x, y)
#define zxc_decompress_chunk_wrapper ZXC_CAT(zxc_decompress_chunk_wrapper, ZXC_FUNCTION_SUFFIX)
#define zxc_decompress_chunk_wrapper_dict \
    ZXC_CAT(zxc_decompress_chunk_wrapper_dict, ZXC_FUNCTION_SUFFIX)
#define zxc_decompress_chunk_wrapper_safe \
    ZXC_CAT(zxc_decompress_chunk_wrapper_safe, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_decode_section ZXC_CAT(zxc_huf_decode_section, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_decode_section_dict ZXC_CAT(zxc_huf_decode_section_dict, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_pack_lengths ZXC_CAT(zxc_huf_pack_lengths, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_nudge_code_lengths zxc_huf_nudge_code_lengths_kt
#define zxc_huf_nudge_cost zxc_huf_nudge_cost_kt
#define zxc_huf_dict_tree_build zxc_huf_dict_tree_build_kt
#include <stdio.h>
#include <stdlib.h>

#include "../../src/lib/zxc_compress.c"
#include "../../src/lib/zxc_decompress.c"
#include "../../src/lib/zxc_huffman.c"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#define KT_HAVE_GUARD 1
#endif

#if defined(__linux__) && defined(__loongarch64)
#include <sys/auxv.h>
#endif

static uint64_t g_rng = 0x9E3779B97F4A7C15ULL;

static uint32_t kt_rand(void) {
    uint64_t z = (g_rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}

static void kt_fill(uint8_t* p, const size_t n) {
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)kt_rand();
}

static int g_failures;

#define KT_CHECK(cond, ...)      \
    do {                         \
        if (!(cond)) {           \
            printf("  [FAIL] "); \
            printf(__VA_ARGS__); \
            printf("\n");        \
            g_failures++;        \
            return;              \
        }                        \
    } while (0)

/* p[n + slack] is an unmapped page. */
typedef struct {
    uint8_t* p;
    void* base;
    size_t map_len;
} kt_buf_t;

static kt_buf_t kt_alloc(const size_t n, const size_t slack) {
    kt_buf_t b;
#ifdef KT_HAVE_GUARD
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    const size_t body = (n + slack + page - 1) / page * page + page; /* never empty */
    b.map_len = body + page;
    b.base = mmap(NULL, b.map_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (b.base == MAP_FAILED || mprotect((uint8_t*)b.base + body, page, PROT_NONE) != 0) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    b.p = (uint8_t*)b.base + body - n - slack;
#else
    b.map_len = n + slack + 1;
    b.base = malloc(b.map_len);
    if (!b.base) exit(EXIT_FAILURE);
    b.p = (uint8_t*)b.base;
#endif
    return b;
}

static void kt_free(const kt_buf_t b) {
#ifdef KT_HAVE_GUARD
    munmap(b.base, b.map_len);
#else
    free(b.base);
#endif
}

/* --- LZ decode: overlap copies and the byte fill --- */

#define KT_LZ_PRE 64

static void test_overlap_copies(void) {
    printf("overlap copies, off 2..31 x every length\n");
    for (uint32_t off = 2; off <= 31; off++) {
        for (size_t ml = 1; ml <= 200; ml++) {
            const size_t total = KT_LZ_PRE + ml + ZXC_PAD_SIZE;
            const kt_buf_t ref = kt_alloc(total, 0);
            const kt_buf_t got = kt_alloc(total, 0);
            kt_fill(ref.p, total);
            ZXC_MEMCPY(got.p, ref.p, total);
            for (size_t i = 0; i < ml; i++) ref.p[KT_LZ_PRE + i] = ref.p[KT_LZ_PRE + i - off];

            zxc_decode_copy_overlap_run32(got.p + KT_LZ_PRE, off, ml);
            KT_CHECK(memcmp(got.p, ref.p, KT_LZ_PRE + ml) == 0, "run32 off=%u ml=%zu", off, ml);

            if (ml <= 32) {
                kt_fill(got.p + KT_LZ_PRE, ml + ZXC_PAD_SIZE);
                ZXC_MEMCPY(got.p, ref.p, KT_LZ_PRE);
                zxc_decode_copy_overlap_short(got.p + KT_LZ_PRE, off, ml);
                KT_CHECK(memcmp(got.p, ref.p, KT_LZ_PRE + ml) == 0, "short off=%u ml=%zu", off, ml);
            }
            kt_free(ref);
            kt_free(got);
        }
    }
}

static void test_fill_run(void) {
    printf("byte fill (offset 1), every length\n");
    for (size_t ml = 1; ml <= 300; ml++) {
        const size_t total = KT_LZ_PRE + ml + ZXC_PAD_SIZE;
        const kt_buf_t got = kt_alloc(total, 0);
        kt_fill(got.p, total);
        const uint8_t byte = (uint8_t)kt_rand();
        const uint8_t before = got.p[KT_LZ_PRE - 1];
        zxc_decode_fill_run(got.p + KT_LZ_PRE, byte, ml);
        int ok = got.p[KT_LZ_PRE - 1] == before;
        for (size_t i = 0; i < ml; i++) ok &= got.p[KT_LZ_PRE + i] == byte;
        kt_free(got);
        KT_CHECK(ok, "fill ml=%zu", ml);
    }
}

static void test_wild_copies(void) {
    printf("zxc_copy16 / zxc_copy32\n");
    const kt_buf_t src = kt_alloc(32, 0);
    const kt_buf_t dst = kt_alloc(32, 0);
    kt_fill(src.p, 32);
    zxc_copy32(dst.p, src.p);
    const int ok32 = memcmp(dst.p, src.p, 32) == 0;
    kt_fill(dst.p, 32);
    zxc_copy16(dst.p + 16, src.p + 16);
    const int ok16 = memcmp(dst.p + 16, src.p + 16, 16) == 0;
    kt_free(src);
    kt_free(dst);
    KT_CHECK(ok32 && ok16, "wild copy");
}

/* --- PivCo: merge, flat unpack, leaf pair --- */

static void test_pivco_merge(void) {
    printf("PivCo merge, n 0..300\n");
    for (size_t n = 0; n <= 300; n++) {
        for (int bias = 0; bias < 4; bias++) {
            const size_t nbytes = (n + 7) / 8;
            const kt_buf_t bits = kt_alloc(nbytes, 0);
            size_t nr = 0;
            ZXC_MEMSET(bits.p, 0, nbytes);
            for (size_t i = 0; i < n; i++) {
                const uint32_t r = kt_rand() & 15;
                const int bit = bias == 0   ? (int)(r & 1)
                                : bias == 1 ? r == 0
                                : bias == 2 ? r != 0
                                            : 1;
                if (bit) {
                    bits.p[i >> 3] |= (uint8_t)(1U << (i & 7));
                    nr++;
                }
            }
            const size_t nl = n - nr;
            /* A 16-byte load at a cursor that may sit at the end. */
            const kt_buf_t src = kt_alloc(n, 16);
            const kt_buf_t out = kt_alloc(n, 0);
            const kt_buf_t ref = kt_alloc(n, 0);
            kt_fill(src.p, n + 16);
            size_t lp = 0;
            size_t rp = 0;
            for (size_t i = 0; i < n; i++) {
                const int bit = (bits.p[i >> 3] >> (i & 7)) & 1;
                ref.p[i] = bit ? src.p[nl + rp++] : src.p[lp++];
            }
            zxc_pivco_merge(out.p, src.p, nl, nr, bits.p);
            const int ok = memcmp(out.p, ref.p, n) == 0;
            kt_free(bits);
            kt_free(src);
            kt_free(out);
            kt_free(ref);
            KT_CHECK(ok, "merge n=%zu bias=%d", n, bias);
        }
    }
}

static void test_pivco_unpack_flat(void) {
    printf("PivCo flat unpack, D 2..11 x n 0..200\n");
    for (int D = 2; D <= ZXC_HUF_MAX_CODE_LEN_ULTRA; D++) {
        const size_t entries = (size_t)1 << D;
        const kt_buf_t c2s = kt_alloc(entries, 0);
        kt_fill(c2s.p, entries);
        for (size_t n = 0; n <= 200; n++) {
            const size_t nbytes = (n * (size_t)D + 7) / 8;
            const kt_buf_t bits = kt_alloc(nbytes, 0);
            const kt_buf_t out = kt_alloc(n, 0);
            const kt_buf_t ref = kt_alloc(n, 0);
            kt_fill(bits.p, nbytes);
            for (size_t i = 0; i < n; i++) {
                uint32_t code = 0;
                for (int k = 0; k < D; k++) {
                    const size_t bit = i * (size_t)D + (size_t)k;
                    code |= (uint32_t)((bits.p[bit >> 3] >> (bit & 7)) & 1) << k;
                }
                ref.p[i] = c2s.p[code];
            }
            zxc_pivco_unpack_flat(out.p, n, D, bits.p, c2s.p);
            const int ok = memcmp(out.p, ref.p, n) == 0;
            kt_free(bits);
            kt_free(out);
            kt_free(ref);
            if (!ok) kt_free(c2s);
            KT_CHECK(ok, "flat D=%d n=%zu", D, n);
        }
        kt_free(c2s);
    }
}

static void test_pivco_leaf_pair(void) {
    printf("PivCo leaf pair, n 0..300\n");
    for (size_t n = 0; n <= 300; n++) {
        const size_t nbytes = (n + 7) / 8;
        const kt_buf_t bits = kt_alloc(nbytes, 0);
        const kt_buf_t out = kt_alloc(n, 0);
        kt_fill(bits.p, nbytes);
        const uint8_t s0 = (uint8_t)kt_rand();
        const uint8_t s1 = (n % 7 == 0) ? s0 : (uint8_t)kt_rand();
        zxc_pivco_emit_leaf_pair(out.p, n, s0, s1, bits.p);
        int ok = 1;
        for (size_t i = 0; i < n; i++)
            ok &= out.p[i] == (((bits.p[i >> 3] >> (i & 7)) & 1) ? s1 : s0);
        kt_free(bits);
        kt_free(out);
        KT_CHECK(ok, "leaf pair n=%zu", n);
    }
}

/* --- Whole Huffman sections, degenerate tables included --- */

static void huf_roundtrip(const char* name, const uint8_t* lits, const size_t n,
                          const int max_len) {
    uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    for (size_t i = 0; i < n; i++) freq[lits[i]]++;
    KT_CHECK(zxc_huf_build_code_lengths(freq, code_len, NULL, max_len) == ZXC_OK, "%s: build",
             name);
    const size_t sz = zxc_huf_calc_size(freq, code_len, 1);
    if (sz == SIZE_MAX) return;
    uint8_t* const enc = (uint8_t*)malloc(sz + 64);
    uint8_t* const dec = (uint8_t*)malloc(n + ZXC_PAD_SIZE);
    uint8_t* const scratch = (uint8_t*)malloc(n + ZXC_PIVCO_SCRATCH_PAD);
    if (!enc || !dec || !scratch) exit(EXIT_FAILURE);
    const int w = zxc_huf_encode_section(lits, n, freq, code_len, enc, sz + 64);
    int ok = w > 0 && (size_t)w == sz;
    const kt_buf_t payload = kt_alloc(ok ? (size_t)w : 0, 0);
    if (ok) {
        ZXC_MEMCPY(payload.p, enc, (size_t)w);
        ok = zxc_huf_decode_section(payload.p, (size_t)w, dec, n, scratch) == ZXC_OK &&
             memcmp(dec, lits, n) == 0;
    }
    kt_free(payload);
    free(enc);
    free(dec);
    free(scratch);
    KT_CHECK(ok, "%s: n=%zu max_len=%d", name, n, max_len);
}

static void test_huf_sections(void) {
    printf("Huffman sections, degenerate tables\n");
    static const size_t sizes[] = {139, 140, 255, 256, 257, 1000, 4096, 65536, 70001};
    uint8_t* const lits = (uint8_t*)malloc(70001);
    if (!lits) exit(EXIT_FAILURE);
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        const size_t n = sizes[s];
        for (int max_len = ZXC_HUF_MAX_CODE_LEN_DENSITY; max_len <= ZXC_HUF_MAX_CODE_LEN_ULTRA;
             max_len += ZXC_HUF_MAX_CODE_LEN_ULTRA - ZXC_HUF_MAX_CODE_LEN_DENSITY) {
            ZXC_MEMSET(lits, 'x', n);
            huf_roundtrip("one symbol", lits, n, max_len);
            for (size_t i = 0; i < n; i++) lits[i] = (uint8_t)(kt_rand() & 1);
            huf_roundtrip("two symbols", lits, n, max_len);
            for (size_t i = 0; i < n; i++) lits[i] = (kt_rand() % 1000) ? 7 : 200;
            huf_roundtrip("two symbols, skewed", lits, n, max_len);
            for (size_t i = 0; i < n; i++) lits[i] = (uint8_t)(kt_rand() % 3);
            huf_roundtrip("three symbols", lits, n, max_len);
            for (size_t i = 0; i < n; i++) lits[i] = (uint8_t)(kt_rand() & 15);
            huf_roundtrip("16 symbols, flat", lits, n, max_len);
            for (size_t i = 0; i < n; i++) lits[i] = (uint8_t)(kt_rand() & 63);
            huf_roundtrip("64 symbols, flat", lits, n, max_len);
            for (size_t i = 0; i < n; i++) lits[i] = (uint8_t)kt_rand();
            huf_roundtrip("256 symbols, flat", lits, n, max_len);
            for (size_t i = 0; i < n; i++) lits[i] = (uint8_t)zxc_ctz32(kt_rand() | 0x80000000U);
            huf_roundtrip("geometric", lits, n, max_len);
            for (size_t i = 0; i < n; i++) lits[i] = (i < 256) ? (uint8_t)i : 0;
            huf_roundtrip("dominant + all", lits, n, max_len);
        }
    }
    free(lits);
}

/* --- Compressor kernels --- */

static void test_rle_pair_mask(void) {
#if defined(ZXC_RLE_WIN)
    printf("RLE pair mask, %d-byte window\n", ZXC_RLE_WIN);
    for (int iter = 0; iter < 20000; iter++) {
        const kt_buf_t b = kt_alloc(ZXC_RLE_WIN + 1, 0);
        const uint32_t alphabet = 1 + (kt_rand() % 3);
        for (int i = 0; i <= ZXC_RLE_WIN; i++) b.p[i] = (uint8_t)(kt_rand() % alphabet);
        const zxc_rle_mask_t m = zxc_rle_pair_mask(b.p);
        int ok = 1;
        for (int i = 0; i < ZXC_RLE_WIN; i++) {
            const zxc_rle_mask_t field =
                (m >> (i * ZXC_RLE_BPB)) & (((zxc_rle_mask_t)1 << ZXC_RLE_BPB) - 1);
            ok &= (field != 0) == (b.p[i] == b.p[i + 1]);
        }
        kt_free(b);
        KT_CHECK(ok, "pair mask iter=%d", iter);
    }
#else
    printf("RLE pair mask: no vector form on this tier\n");
#endif
}

static void test_dp_update(void) {
    printf("optimal-parse DP update, spans 0..70\n");
    enum { N = 128 };
    for (int iter = 0; iter < 20000; iter++) {
        const size_t p = kt_rand() % 8;
        const size_t L = 3 + kt_rand() % 8;
        const size_t L_end = L + kt_rand() % 71;
        const uint32_t nxt = (iter & 1) ? kt_rand() : 1000 + kt_rand() % 16;
        const uint16_t off = (uint16_t)kt_rand();
        const size_t cells = p + L_end;
        const kt_buf_t dp = kt_alloc(cells * sizeof(uint32_t), 0);
        const kt_buf_t pl = kt_alloc(cells * sizeof(uint16_t), 0);
        const kt_buf_t po = kt_alloc(cells * sizeof(uint16_t), 0);
        uint32_t rdp[N];
        uint16_t rpl[N];
        uint16_t rpo[N];
        for (size_t i = 0; i < cells; i++) {
            const uint32_t r = kt_rand() % 8;
            rdp[i] = r == 0 ? 0 : r == 1 ? UINT32_MAX : r == 2 ? nxt : nxt - 4 + kt_rand() % 8;
            rpl[i] = (uint16_t)kt_rand();
            rpo[i] = (uint16_t)kt_rand();
        }
        ZXC_MEMCPY(dp.p, rdp, cells * sizeof(uint32_t));
        ZXC_MEMCPY(pl.p, rpl, cells * sizeof(uint16_t));
        ZXC_MEMCPY(po.p, rpo, cells * sizeof(uint16_t));
        for (size_t l = L; l < L_end; l++) {
            if (nxt < rdp[p + l]) {
                rdp[p + l] = nxt;
                rpl[p + l] = (uint16_t)l;
                rpo[p + l] = off;
            }
        }
        const size_t done =
            zxc_opt_dp_update_const_cost((uint32_t*)(void*)dp.p, (uint16_t*)(void*)pl.p,
                                         (uint16_t*)(void*)po.p, p, L, L_end, nxt, off);
        const int ok = done == L_end && memcmp(dp.p, rdp, cells * sizeof(uint32_t)) == 0 &&
                       memcmp(pl.p, rpl, cells * sizeof(uint16_t)) == 0 &&
                       memcmp(po.p, rpo, cells * sizeof(uint16_t)) == 0;
        kt_free(dp);
        kt_free(pl);
        kt_free(po);
        KT_CHECK(ok, "dp update iter=%d p=%zu L=%zu L_end=%zu", iter, p, L, L_end);
    }
}

/* The match-length and run scans are inlined in the parser. */
static void test_long_matches(void) {
    printf("long matches and byte runs through the block codec\n");
    enum { CAP = 4096 };
    uint8_t* const src = (uint8_t*)malloc(CAP);
    uint8_t* const dec = (uint8_t*)malloc(CAP);
    const size_t bound = (size_t)zxc_compress_block_bound(CAP);
    uint8_t* const enc = (uint8_t*)malloc(bound);
    zxc_cctx* const cctx = zxc_create_cctx();
    zxc_dctx* const dctx = zxc_create_dctx();
    if (!src || !dec || !enc || !cctx || !dctx) exit(EXIT_FAILURE);
    int ok = 1;
    static const int levels[] = {1, 3, 5, 7};
    for (size_t len = 60; len <= 330 && ok; len++) {
        const size_t n = 700 + 2 * len;
        kt_fill(src, n);
        ZXC_MEMCPY(src + 300 + len + 40, src + 100, len);
        ZXC_MEMSET(src + n - len - 20, (int)(len & 0xFF), len);
        for (size_t l = 0; l < sizeof(levels) / sizeof(levels[0]) && ok; l++) {
            ok = zxc_cctx_set_param(cctx, ZXC_C_LEVEL, levels[l]) == ZXC_OK;
            const int64_t c = zxc_compress_block(cctx, src, n, enc, bound);
            ok = ok && c > 0 && zxc_decompress_block(dctx, enc, (size_t)c, dec, n) == (int64_t)n &&
                 memcmp(dec, src, n) == 0;
            if (!ok) printf("  [FAIL] long match len=%zu level=%d\n", len, levels[l]);
        }
    }
    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(src);
    free(dec);
    free(enc);
    if (!ok) g_failures++;
}

/* --- LoongArch instruction semantics the ports rely on --- */

#if defined(ZXC_USE_LSX)
static void test_lsx_semantics(void) {
    printf("LSX semantics: vshuf.b, vilvl.d, vpickev, vmskltz, vbitsel\n");
    uint8_t lo[16];
    uint8_t hi[16];
    uint8_t idx[16];
    uint8_t got[16];
    for (int i = 0; i < 16; i++) {
        lo[i] = (uint8_t)(0x40 + i);
        hi[i] = (uint8_t)(0x80 + i);
    }
    /* vshuf.b(a, b, idx): 0..15 from b, 16..31 from a. Undefined past 31
     * (QEMU wraps, hardware may zero), so not asserted. */
    for (int base = 0; base < 32; base += 16) {
        for (int i = 0; i < 16; i++) idx[i] = (uint8_t)(31 - (base + i));
        __lsx_vst(__lsx_vshuf_b(__lsx_vld(hi, 0), __lsx_vld(lo, 0), __lsx_vld(idx, 0)), got, 0);
        for (int i = 0; i < 16; i++) {
            const int k = idx[i];
            KT_CHECK(got[i] == (k < 16 ? lo[k] : hi[k - 16]), "vshuf.b idx=%d", k);
        }
    }
    __lsx_vst(__lsx_vilvl_d(__lsx_vld(hi, 0), __lsx_vld(lo, 0)), got, 0);
    KT_CHECK(memcmp(got, lo, 8) == 0 && memcmp(got + 8, hi, 8) == 0, "vilvl.d order");
    __lsx_vst(__lsx_vpickev_b(__lsx_vld(hi, 0), __lsx_vld(lo, 0)), got, 0);
    for (int i = 0; i < 8; i++)
        KT_CHECK(got[i] == lo[2 * i] && got[8 + i] == hi[2 * i], "vpickev.b order");
    for (int i = 0; i < 16; i++) idx[i] = (i & 1) ? 0xFF : 0;
    __lsx_vst(__lsx_vbitsel_v(__lsx_vld(lo, 0), __lsx_vld(hi, 0), __lsx_vld(idx, 0)), got, 0);
    for (int i = 0; i < 16; i++) KT_CHECK(got[i] == ((i & 1) ? hi[i] : lo[i]), "vbitsel.v");
    for (int i = 0; i < 16; i++) {
        ZXC_MEMSET(idx, 0x7F, 16);
        idx[i] = 0x80;
        KT_CHECK(zxc_lsx_movemask_b(__lsx_vld(idx, 0)) == (1U << i), "movemask bit %d", i);
    }
}
#endif

#if defined(ZXC_USE_LASX)
static void test_lasx_semantics(void) {
    printf("LASX semantics: xvshuf.b lanes, 32-bit movemask\n");
    uint8_t tab[32];
    uint8_t idx[32];
    uint8_t got[32];
    for (int i = 0; i < 32; i++) {
        tab[i] = (uint8_t)(0x40 + i);
        idx[i] = (uint8_t)(15 - (i & 15));
    }
    /* xvshuf.b stays within its 128-bit lane. */
    const __m256i t = __lasx_xvld(tab, 0);
    __lasx_xvst(__lasx_xvshuf_b(t, t, __lasx_xvld(idx, 0)), got, 0);
    for (int i = 0; i < 32; i++)
        KT_CHECK(got[i] == tab[(i & 16) + idx[i]], "xvshuf.b lane, byte %d", i);
    for (int i = 0; i < 32; i++) {
        ZXC_MEMSET(idx, 0x7F, 32);
        idx[i] = 0x80;
        KT_CHECK(zxc_lasx_movemask_b(__lasx_xvld(idx, 0)) == (1U << i), "movemask bit %d", i);
    }
}
#endif

int main(void) {
#if defined(ZXC_USE_LASX)
    const char* const tier = "LASX";
#if defined(__linux__)
    if (!(getauxval(AT_HWCAP) & (1U << 5))) {
        printf("SIMD kernels: LASX build on a CPU without LASX, skipped.\n");
        return 0;
    }
#endif
#elif defined(ZXC_USE_LSX)
    const char* const tier = "LSX";
#elif defined(ZXC_USE_AVX512)
    const char* const tier = "AVX-512";
#elif defined(ZXC_USE_AVX2)
    const char* const tier = "AVX2";
#elif defined(ZXC_USE_SSE2)
    const char* const tier = "SSE2";
#elif defined(ZXC_USE_NEON64)
    const char* const tier = "NEON64";
#elif defined(ZXC_USE_NEON32)
    const char* const tier = "NEON32";
#else
    const char* const tier = "scalar";
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== SIMD kernels against their scalar definition (%s) ===\n", tier);
#if defined(ZXC_USE_LSX)
    test_lsx_semantics();
#endif
#if defined(ZXC_USE_LASX)
    test_lasx_semantics();
#endif
    test_wild_copies();
    test_overlap_copies();
    test_fill_run();
    test_pivco_merge();
    test_pivco_unpack_flat();
    test_pivco_leaf_pair();
    test_huf_sections();
    test_rle_pair_mask();
    test_dp_update();
    test_long_matches();
    if (g_failures) {
        printf("FAILED: %d kernel checks.\n", g_failures);
        return 1;
    }
    printf("ALL SIMD KERNEL TESTS PASSED (%s).\n", tier);
    return 0;
}

#else /* not a GNU-compatible compiler */

#include <stdio.h>

int main(void) {
    printf("SIMD kernels: needs a GNU-compatible compiler, skipped.\n");
    return 0;
}

#endif
