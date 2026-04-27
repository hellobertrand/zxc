/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Canonical, length-limited (L = 11) Huffman codec for the GLO literal
 * stream at compression level >= 6. Codes are emitted LSB-first; the
 * decoder uses a single 2048-entry lookup table and a 4-way interleaved
 * hot loop. See zxc_huffman.h for the on-disk layout.
 */

#include "zxc_huffman.h"

#include <stdlib.h>
#include <string.h>

#include "zxc_error.h"
#include "zxc_internal.h"

/* ===========================================================================
 * Length-limited Huffman: boundary package-merge
 * ===========================================================================
 *
 * Builds optimal length-limited Huffman code lengths (max length L) on
 * 256-symbol alphabets. Package-merge is run for L levels; each level holds
 * up to 2N items (leaves + paired packages). Selection of the cheapest
 * 2N - 2 items at level L gives the appearance count of each leaf, which is
 * its code length.
 */

typedef struct {
    uint32_t weight;
    int16_t left, right;
    int16_t sym;
} pm_item_t;

typedef struct {
    uint32_t w;
    int16_t sym;
} pm_leaf_t;

static int pm_leaf_cmp(const void* a, const void* b) {
    const pm_leaf_t* la = (const pm_leaf_t*)a;
    const pm_leaf_t* lb = (const pm_leaf_t*)b;
    if (la->w < lb->w) return -1;
    if (la->w > lb->w) return 1;
    return la->sym - lb->sym;
}

int zxc_huf_build_code_lengths(const uint32_t freq[ZXC_HUF_NUM_SYMBOLS],
                               uint8_t code_len[ZXC_HUF_NUM_SYMBOLS]) {
    memset(code_len, 0, ZXC_HUF_NUM_SYMBOLS);

    pm_leaf_t leaves[ZXC_HUF_NUM_SYMBOLS];
    int n = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (freq[i] > 0) {
            leaves[n].w = freq[i];
            leaves[n].sym = (int16_t)i;
            n++;
        }
    }
    if (n == 0) return ZXC_ERROR_CORRUPT_DATA;
    if (n == 1) {
        code_len[leaves[0].sym] = 1;
        return ZXC_OK;
    }

    qsort(leaves, (size_t)n, sizeof(pm_leaf_t), pm_leaf_cmp);

    /* n <= 256 <= 2^L for L = 11, so length-limit is always feasible. */
    const int L = ZXC_HUF_MAX_CODE_LEN;
    const int max_per_level = 2 * n;

    pm_item_t* items = (pm_item_t*)malloc((size_t)L * (size_t)max_per_level * sizeof(pm_item_t));
    int* counts = (int*)calloc((size_t)L, sizeof(int));
    if (!items || !counts) {
        free(items);
        free(counts);
        return ZXC_ERROR_MEMORY;
    }
#define ITEM(k, i) items[(size_t)(k) * (size_t)max_per_level + (size_t)(i)]

    /* Level 0 (logical level 1): the leaves themselves, already sorted. */
    for (int i = 0; i < n; i++) {
        ITEM(0, i).weight = leaves[i].w;
        ITEM(0, i).left = -1;
        ITEM(0, i).right = -1;
        ITEM(0, i).sym = leaves[i].sym;
    }
    counts[0] = n;

    /* Levels 1..L-1: merge sorted leaves with sorted packages from the previous level. */
    for (int k = 1; k < L; k++) {
        const int prev = counts[k - 1];
        const int packs = prev / 2;
        int li = 0, pi = 0, n_lvl = 0;
        while (li < n || pi < packs) {
            const uint32_t wl = (li < n) ? leaves[li].w : UINT32_MAX;
            const uint32_t wp =
                (pi < packs)
                    ? (uint32_t)(ITEM(k - 1, 2 * pi).weight + ITEM(k - 1, 2 * pi + 1).weight)
                    : UINT32_MAX;
            if (wl <= wp && li < n) {
                ITEM(k, n_lvl).weight = wl;
                ITEM(k, n_lvl).left = -1;
                ITEM(k, n_lvl).right = -1;
                ITEM(k, n_lvl).sym = leaves[li].sym;
                li++;
            } else {
                ITEM(k, n_lvl).weight = wp;
                ITEM(k, n_lvl).left = (int16_t)(2 * pi);
                ITEM(k, n_lvl).right = (int16_t)(2 * pi + 1);
                ITEM(k, n_lvl).sym = -1;
                pi++;
            }
            n_lvl++;
        }
        counts[k] = n_lvl;
    }

    /* Step 3: take first 2n-2 items at level L-1; trace back, counting leaf appearances. */
    int n_take = 2 * n - 2;
    if (n_take > counts[L - 1]) n_take = counts[L - 1];

    typedef struct {
        int8_t lvl;
        int16_t idx;
    } frame_t;
    /* Worst case stack depth: (L * n_take) frames; bounded by L * 2n. */
    frame_t* stack = (frame_t*)malloc((size_t)L * (size_t)max_per_level * sizeof(frame_t));
    if (!stack) {
        free(items);
        free(counts);
        return ZXC_ERROR_MEMORY;
    }
    int sp = 0;
    for (int i = 0; i < n_take; i++) {
        stack[sp].lvl = (int8_t)(L - 1);
        stack[sp].idx = (int16_t)i;
        sp++;
    }
    while (sp > 0) {
        frame_t f = stack[--sp];
        const pm_item_t* it = &ITEM(f.lvl, f.idx);
        if (it->sym >= 0) {
            code_len[it->sym]++;
        } else {
            stack[sp].lvl = (int8_t)(f.lvl - 1);
            stack[sp].idx = it->left;
            sp++;
            stack[sp].lvl = (int8_t)(f.lvl - 1);
            stack[sp].idx = it->right;
            sp++;
        }
    }

    free(stack);
    free(items);
    free(counts);
#undef ITEM
    return ZXC_OK;
}

/* ===========================================================================
 * Canonical code construction (LSB-first by bit-reversing canonical MSB codes)
 * =========================================================================*/

static uint32_t reverse_bits(uint32_t v, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

static void build_canonical_codes(const uint8_t code_len[ZXC_HUF_NUM_SYMBOLS],
                                  uint32_t codes[ZXC_HUF_NUM_SYMBOLS]) {
    uint32_t bl_count[ZXC_HUF_MAX_CODE_LEN + 1] = {0};
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        bl_count[code_len[i]]++;
    }
    bl_count[0] = 0;

    uint32_t next_code[ZXC_HUF_MAX_CODE_LEN + 2] = {0};
    uint32_t code = 0;
    for (int k = 1; k <= ZXC_HUF_MAX_CODE_LEN + 1; k++) {
        code = (code + bl_count[k - 1]) << 1;
        next_code[k] = code;
    }

    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        const int l = code_len[i];
        if (l == 0) {
            codes[i] = 0;
        } else {
            const uint32_t msb_code = next_code[l]++;
            codes[i] = reverse_bits(msb_code, l);
        }
    }
}

/* ===========================================================================
 * 128-byte length header: 256 x 4-bit lengths, low nibble first.
 * =========================================================================*/

static void pack_lengths_header(const uint8_t code_len[ZXC_HUF_NUM_SYMBOLS],
                                uint8_t out[ZXC_HUF_LENGTHS_HEADER_SIZE]) {
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i += 2) {
        const uint8_t lo = code_len[i] & 0x0F;
        const uint8_t hi = code_len[i + 1] & 0x0F;
        out[i >> 1] = (uint8_t)(lo | (hi << 4));
    }
}

static int unpack_lengths_header(const uint8_t in[ZXC_HUF_LENGTHS_HEADER_SIZE],
                                 uint8_t code_len[ZXC_HUF_NUM_SYMBOLS]) {
    int max_len = 0;
    int n_present = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i += 2) {
        const uint8_t b = in[i >> 1];
        const uint8_t lo = b & 0x0F;
        const uint8_t hi = (uint8_t)(b >> 4);
        code_len[i] = lo;
        code_len[i + 1] = hi;
        if (lo > max_len) max_len = lo;
        if (hi > max_len) max_len = hi;
        if (lo) n_present++;
        if (hi) n_present++;
    }
    if (max_len > ZXC_HUF_MAX_CODE_LEN) return ZXC_ERROR_CORRUPT_DATA;
    if (n_present == 0) return ZXC_ERROR_CORRUPT_DATA;
    return ZXC_OK;
}

/* ===========================================================================
 * Bit writer (LSB-first)
 * =========================================================================*/

typedef struct {
    uint8_t* ptr;
    uint8_t* end;
    uint64_t accum;
    int bits;
    int err;
} bit_writer_t;

static ZXC_ALWAYS_INLINE void bw_init(bit_writer_t* bw, uint8_t* dst, size_t cap) {
    bw->ptr = dst;
    bw->end = dst + cap;
    bw->accum = 0;
    bw->bits = 0;
    bw->err = 0;
}

static ZXC_ALWAYS_INLINE void bw_put(bit_writer_t* bw, uint32_t code, int len) {
    bw->accum |= ((uint64_t)code) << bw->bits;
    bw->bits += len;
    while (bw->bits >= 8) {
        if (UNLIKELY(bw->ptr >= bw->end)) {
            bw->err = 1;
            bw->bits = 0;
            return;
        }
        *bw->ptr++ = (uint8_t)bw->accum;
        bw->accum >>= 8;
        bw->bits -= 8;
    }
}

static ZXC_ALWAYS_INLINE int bw_finish(bit_writer_t* bw) {
    if (bw->bits > 0) {
        if (UNLIKELY(bw->ptr >= bw->end)) return ZXC_ERROR_DST_TOO_SMALL;
        *bw->ptr++ = (uint8_t)bw->accum;
        bw->accum = 0;
        bw->bits = 0;
    }
    return bw->err ? ZXC_ERROR_DST_TOO_SMALL : ZXC_OK;
}

/* ===========================================================================
 * Estimator + encoder
 * =========================================================================*/

size_t zxc_huf_estimate_size(const uint32_t freq[ZXC_HUF_NUM_SYMBOLS],
                             const uint8_t code_len[ZXC_HUF_NUM_SYMBOLS], size_t n_literals) {
    (void)n_literals;
    uint64_t total_bits = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        total_bits += (uint64_t)freq[i] * (uint64_t)code_len[i];
    }
    /* Per sub-stream tail-byte slack: 1 byte / sub-stream. */
    const size_t bytes = (size_t)((total_bits + 7) / 8) + ZXC_HUF_NUM_STREAMS;
    return ZXC_HUF_HEADER_SIZE + bytes;
}

int zxc_huf_encode_section(const uint8_t* literals, size_t n_literals,
                           const uint8_t code_len[ZXC_HUF_NUM_SYMBOLS], uint8_t* dst,
                           size_t dst_cap) {
    if (UNLIKELY(n_literals == 0)) return ZXC_ERROR_CORRUPT_DATA;
    if (UNLIKELY(dst_cap < ZXC_HUF_HEADER_SIZE)) return ZXC_ERROR_DST_TOO_SMALL;

    /* 1. Pack the 128-byte length header. */
    pack_lengths_header(code_len, dst);

    /* 2. Build canonical codes (LSB-first via bit-reversal). */
    uint32_t codes[ZXC_HUF_NUM_SYMBOLS];
    build_canonical_codes(code_len, codes);

    /* 3. Reserve 6 bytes for sub-stream sizes; encode 4 sub-streams after them. */
    uint8_t* const sizes_hdr = dst + ZXC_HUF_LENGTHS_HEADER_SIZE;
    uint8_t* const stream_base = sizes_hdr + ZXC_HUF_STREAM_SIZES_HEADER_SIZE;
    uint8_t* const stream_end = dst + dst_cap;

    const size_t Q = (n_literals + ZXC_HUF_NUM_STREAMS - 1) / ZXC_HUF_NUM_STREAMS;

    bit_writer_t bw;
    uint8_t* p = stream_base;
    size_t s_sizes[ZXC_HUF_NUM_STREAMS];

    for (int s = 0; s < ZXC_HUF_NUM_STREAMS; s++) {
        const size_t start = (size_t)s * Q;
        size_t stop = start + Q;
        if (stop > n_literals) stop = n_literals;

        uint8_t* const stream_start = p;
        bw_init(&bw, p, (size_t)(stream_end - p));
        for (size_t i = start; i < stop; i++) {
            const uint8_t sym = literals[i];
            const int len = code_len[sym];
            if (UNLIKELY(len == 0)) return ZXC_ERROR_CORRUPT_DATA; /* symbol absent from table */
            bw_put(&bw, codes[sym], len);
        }
        const int rc = bw_finish(&bw);
        if (UNLIKELY(rc != ZXC_OK)) return rc;
        s_sizes[s] = (size_t)(bw.ptr - stream_start);
        p = bw.ptr;
    }

    /* 4. Persist the 3 explicit sub-stream sizes (s4 is implied). */
    for (int s = 0; s < ZXC_HUF_NUM_STREAMS - 1; s++) {
        if (s_sizes[s] > 0xFFFFu) return ZXC_ERROR_DST_TOO_SMALL;
        sizes_hdr[2 * s] = (uint8_t)(s_sizes[s] & 0xFF);
        sizes_hdr[2 * s + 1] = (uint8_t)((s_sizes[s] >> 8) & 0xFF);
    }

    return (int)(p - dst);
}

/* ===========================================================================
 * Decoder table builder + 4-way interleaved decoder
 * =========================================================================*/

static int build_decode_table(const uint8_t code_len[ZXC_HUF_NUM_SYMBOLS],
                              zxc_huf_dec_entry_t table[ZXC_HUF_TABLE_SIZE]) {
    uint32_t bl_count[ZXC_HUF_MAX_CODE_LEN + 1] = {0};
    int n_present = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        const uint8_t l = code_len[i];
        if (l > ZXC_HUF_MAX_CODE_LEN) return ZXC_ERROR_CORRUPT_DATA;
        bl_count[l]++;
        if (l) n_present++;
    }
    if (n_present == 0) return ZXC_ERROR_CORRUPT_DATA;
    bl_count[0] = 0;

    /* Validate Kraft inequality (==): sum(2^(L - len)) must equal 2^L. */
    {
        uint64_t kraft = 0;
        for (int k = 1; k <= ZXC_HUF_MAX_CODE_LEN; k++) {
            kraft += (uint64_t)bl_count[k] << (ZXC_HUF_MAX_CODE_LEN - k);
        }
        /* Special case: a single present symbol with len 1 is the only valid degenerate code. */
        if (n_present == 1) {
            if (bl_count[1] != 1) return ZXC_ERROR_CORRUPT_DATA;
        } else {
            if (kraft != ((uint64_t)1 << ZXC_HUF_MAX_CODE_LEN)) return ZXC_ERROR_CORRUPT_DATA;
        }
    }

    uint32_t next_code[ZXC_HUF_MAX_CODE_LEN + 2] = {0};
    uint32_t code = 0;
    for (int k = 1; k <= ZXC_HUF_MAX_CODE_LEN + 1; k++) {
        code = (code + bl_count[k - 1]) << 1;
        next_code[k] = code;
    }

    /* Initialise the table to "invalid" so corrupt streams that read an unused
     * index get rejected by the high-byte length check at decode time. */
    for (uint32_t i = 0; i < ZXC_HUF_TABLE_SIZE; i++) table[i].entry = 0;

    for (int sym = 0; sym < ZXC_HUF_NUM_SYMBOLS; sym++) {
        const int l = code_len[sym];
        if (l == 0) continue;
        const uint32_t msb_code = next_code[l]++;
        const uint32_t lsb_code = reverse_bits(msb_code, l);
        const uint16_t entry = (uint16_t)((unsigned)l << 8 | (unsigned)sym);
        const uint32_t step = (uint32_t)1 << l;
        for (uint32_t fill = lsb_code; fill < ZXC_HUF_TABLE_SIZE; fill += step) {
            table[fill].entry = entry;
        }
    }

    /* Single-symbol degenerate code: only half the table was filled because
     * Kraft sum is 2^(L-1) instead of 2^L. Replicate the one valid entry
     * across every slot so any padded-zero peek decodes to the same symbol. */
    if (n_present == 1) {
        uint16_t valid = 0;
        for (uint32_t i = 0; i < ZXC_HUF_TABLE_SIZE; i++) {
            if (table[i].entry != 0) {
                valid = table[i].entry;
                break;
            }
        }
        for (uint32_t i = 0; i < ZXC_HUF_TABLE_SIZE; i++) {
            if (table[i].entry == 0) table[i].entry = valid;
        }
    }

    return ZXC_OK;
}

int zxc_huf_decode_section(const uint8_t* payload, size_t payload_size, uint8_t* dst,
                           size_t n_literals) {
    if (UNLIKELY(payload_size < ZXC_HUF_HEADER_SIZE)) return ZXC_ERROR_CORRUPT_DATA;
    if (UNLIKELY(n_literals == 0)) return ZXC_ERROR_CORRUPT_DATA;

    /* 1. Parse length header. */
    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    {
        const int rc = unpack_lengths_header(payload, code_len);
        if (rc != ZXC_OK) return rc;
    }

    /* 2. Build the 2048-entry decode table. */
    zxc_huf_dec_entry_t table[ZXC_HUF_TABLE_SIZE];
    {
        const int rc = build_decode_table(code_len, table);
        if (rc != ZXC_OK) return rc;
    }

    /* 3. Parse sub-stream sizes. */
    const uint8_t* const sizes_hdr = payload + ZXC_HUF_LENGTHS_HEADER_SIZE;
    const uint16_t s1 = (uint16_t)(sizes_hdr[0] | ((uint16_t)sizes_hdr[1] << 8));
    const uint16_t s2 = (uint16_t)(sizes_hdr[2] | ((uint16_t)sizes_hdr[3] << 8));
    const uint16_t s3 = (uint16_t)(sizes_hdr[4] | ((uint16_t)sizes_hdr[5] << 8));

    const size_t streams_total = payload_size - ZXC_HUF_HEADER_SIZE;
    const size_t s123 = (size_t)s1 + (size_t)s2 + (size_t)s3;
    if (UNLIKELY(s123 > streams_total)) return ZXC_ERROR_CORRUPT_DATA;
    const size_t s4 = streams_total - s123;

    const uint8_t* const stream_base = payload + ZXC_HUF_HEADER_SIZE;
    const size_t off[ZXC_HUF_NUM_STREAMS] = {0, s1, (size_t)s1 + s2, s123};
    const size_t sz[ZXC_HUF_NUM_STREAMS] = {s1, s2, s3, s4};

    /* 4. Initialise 4 bit readers. */
    zxc_bit_reader_t br[ZXC_HUF_NUM_STREAMS];
    for (int s = 0; s < ZXC_HUF_NUM_STREAMS; s++) {
        zxc_br_init(&br[s], stream_base + off[s], sz[s]);
    }

    /* 5. 4-way interleaved decode. Each sub-stream owns a contiguous slice
     * of dst: stream s covers literal indices [s*Q, min((s+1)*Q, N)). With
     * Q = ceil(N/4) the first 3 streams have exactly Q symbols and stream 3
     * has `N - 3Q` symbols (always the shortest, possibly 0). */
    const size_t Q = (n_literals + ZXC_HUF_NUM_STREAMS - 1) / ZXC_HUF_NUM_STREAMS;
    size_t s_count[ZXC_HUF_NUM_STREAMS];
    uint8_t* s_dst[ZXC_HUF_NUM_STREAMS];
    for (int s = 0; s < ZXC_HUF_NUM_STREAMS; s++) {
        size_t start = (size_t)s * Q;
        size_t stop = start + Q;
        if (start > n_literals) start = n_literals;
        if (stop > n_literals) stop = n_literals;
        s_count[s] = stop - start;
        s_dst[s] = dst + start;
    }
    size_t common = s_count[0];
    for (int s = 1; s < ZXC_HUF_NUM_STREAMS; s++)
        if (s_count[s] < common) common = s_count[s];

    for (size_t i = 0; i < common; i++) {
        zxc_br_ensure(&br[0], ZXC_HUF_MAX_CODE_LEN);
        zxc_br_ensure(&br[1], ZXC_HUF_MAX_CODE_LEN);
        zxc_br_ensure(&br[2], ZXC_HUF_MAX_CODE_LEN);
        zxc_br_ensure(&br[3], ZXC_HUF_MAX_CODE_LEN);

        const uint32_t i0 = (uint32_t)br[0].accum & (ZXC_HUF_TABLE_SIZE - 1);
        const uint32_t i1 = (uint32_t)br[1].accum & (ZXC_HUF_TABLE_SIZE - 1);
        const uint32_t i2 = (uint32_t)br[2].accum & (ZXC_HUF_TABLE_SIZE - 1);
        const uint32_t i3 = (uint32_t)br[3].accum & (ZXC_HUF_TABLE_SIZE - 1);

        const uint16_t e0 = table[i0].entry;
        const uint16_t e1 = table[i1].entry;
        const uint16_t e2 = table[i2].entry;
        const uint16_t e3 = table[i3].entry;

        s_dst[0][i] = (uint8_t)(e0 & 0xFF);
        s_dst[1][i] = (uint8_t)(e1 & 0xFF);
        s_dst[2][i] = (uint8_t)(e2 & 0xFF);
        s_dst[3][i] = (uint8_t)(e3 & 0xFF);

        const int l0 = (int)(e0 >> 8);
        const int l1 = (int)(e1 >> 8);
        const int l2 = (int)(e2 >> 8);
        const int l3 = (int)(e3 >> 8);
        if (UNLIKELY((l0 | l1 | l2 | l3) == 0)) return ZXC_ERROR_CORRUPT_DATA;

        br[0].accum >>= l0;
        br[0].bits -= l0;
        br[1].accum >>= l1;
        br[1].bits -= l1;
        br[2].accum >>= l2;
        br[2].bits -= l2;
        br[3].accum >>= l3;
        br[3].bits -= l3;
    }

    /* Tail phase: per spec only the first 3 streams may carry more than
     * `common` symbols (when N is not a multiple of 4). Decode them scalar. */
    for (int s = 0; s < ZXC_HUF_NUM_STREAMS; s++) {
        for (size_t i = common; i < s_count[s]; i++) {
            zxc_br_ensure(&br[s], ZXC_HUF_MAX_CODE_LEN);
            const uint32_t idx = (uint32_t)br[s].accum & (ZXC_HUF_TABLE_SIZE - 1);
            const uint16_t e = table[idx].entry;
            const int l = (int)(e >> 8);
            if (UNLIKELY(l == 0)) return ZXC_ERROR_CORRUPT_DATA;
            s_dst[s][i] = (uint8_t)(e & 0xFF);
            br[s].accum >>= l;
            br[s].bits -= l;
        }
    }

    return ZXC_OK;
}
