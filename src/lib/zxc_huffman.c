/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Canonical, length-limited (L = 9) Huffman codec for the GLO literal
 * stream at compression level >= 6. Codes are emitted LSB-first; the
 * decoder uses a single 512-entry lookup table and a 4-way interleaved
 * hot loop. Public declarations (constants, function prototypes) live in
 * zxc_internal.h; the rest is private to this translation unit.
 */

#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_error.h"
#include "zxc_internal.h"

/* Private codec constants (only used inside this translation unit). */
#define ZXC_HUF_TABLE_LOG ZXC_HUF_MAX_CODE_LEN
#define ZXC_HUF_TABLE_SIZE (1u << ZXC_HUF_TABLE_LOG)
#define ZXC_HUF_LENGTHS_HEADER_SIZE 128
#define ZXC_HUF_STREAM_SIZES_HEADER_SIZE 6

/* 512-entry decoder lookup table entry: low byte = symbol, high byte = code length. */
typedef struct {
    uint16_t entry;
} zxc_huf_dec_entry_t;

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

/**
 * @brief qsort comparator for `pm_leaf_t` arrays.
 *
 * Orders leaves by ascending weight, breaking ties by ascending symbol value
 * so the resulting code-length assignment is deterministic across runs.
 *
 * @param[in] a Pointer to a `pm_leaf_t` (cast from `const void*`).
 * @param[in] b Pointer to a `pm_leaf_t` (cast from `const void*`).
 * @return Negative / zero / positive per the `qsort` convention.
 */
static int pm_leaf_cmp(const void* a, const void* b) {
    const pm_leaf_t* const la = (const pm_leaf_t*)a;
    const pm_leaf_t* const lb = (const pm_leaf_t*)b;
    if (la->w < lb->w) return -1;
    if (la->w > lb->w) return 1;
    return la->sym - lb->sym;
}

/**
 * @brief Build length-limited canonical Huffman code lengths.
 *
 * Runs the boundary package-merge algorithm capped at `ZXC_HUF_MAX_CODE_LEN`.
 * Symbols with `freq[i] == 0` get `code_len[i] == 0`; every other symbol
 * receives a length in `[1, ZXC_HUF_MAX_CODE_LEN]`. The single-present-symbol
 * case is handled as a degenerate code of length 1.
 *
 * @param[in]  freq     Frequency table indexed by symbol (0..255).
 * @param[out] code_len Output code-length array, written in full.
 * @return `ZXC_OK` on success, `ZXC_ERROR_MEMORY` or `ZXC_ERROR_CORRUPT_DATA`
 *         on failure.
 */
int zxc_huf_build_code_lengths(const uint32_t* RESTRICT freq, uint8_t* RESTRICT code_len) {
    ZXC_MEMSET(code_len, 0, ZXC_HUF_NUM_SYMBOLS);

    pm_leaf_t leaves[ZXC_HUF_NUM_SYMBOLS];
    int n = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (freq[i] > 0) {
            leaves[n].w = freq[i];
            leaves[n].sym = (int16_t)i;
            n++;
        }
    }
    if (UNLIKELY(n == 0)) return ZXC_ERROR_CORRUPT_DATA;
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
    if (UNLIKELY(!items || !counts)) {
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
    if (UNLIKELY(!stack)) {
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

/**
 * @brief Reverse the low @p n bits of @p v.
 *
 * Used to convert MSB-first canonical Huffman codes (the natural form
 * produced by the canonical-code construction) into LSB-first codes that
 * can be packed into the bit writer with a single shift-or.
 *
 * @param[in] v Value whose low @p n bits will be reversed.
 * @param[in] n Number of significant bits in @p v (1..32).
 * @return The bit-reversed value, with bits above position @p n set to 0.
 */
static uint32_t reverse_bits(uint32_t v, const int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

/**
 * @brief Build the canonical LSB-first Huffman codes for a length table.
 *
 * Generates MSB-first canonical codes following RFC 1951 §3.2.2, then
 * bit-reverses each so the encoder can emit them with a plain
 * `accum |= code << bits` step. Absent symbols (length 0) receive code 0.
 *
 * @param[in]  code_len Per-symbol code lengths.
 * @param[out] codes    Per-symbol LSB-first canonical codes.
 */
static void build_canonical_codes(const uint8_t* RESTRICT code_len, uint32_t* RESTRICT codes) {
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

/**
 * @brief Pack 256 4-bit code lengths into the 128-byte section header.
 *
 * The packing is little-endian within each byte: low nibble holds
 * `code_len[2*i]`, high nibble holds `code_len[2*i + 1]`. The function
 * silently truncates any length > 15 — callers must enforce the cap of
 * `ZXC_HUF_MAX_CODE_LEN` (≤ 15) before calling.
 *
 * @param[in]  code_len Per-symbol code lengths (length `ZXC_HUF_NUM_SYMBOLS`).
 * @param[out] out      Output header buffer of `ZXC_HUF_LENGTHS_HEADER_SIZE` bytes.
 */
static void pack_lengths_header(const uint8_t* RESTRICT code_len, uint8_t* RESTRICT out) {
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i += 2) {
        const uint8_t lo = code_len[i] & 0x0F;
        const uint8_t hi = code_len[i + 1] & 0x0F;
        out[i >> 1] = (uint8_t)(lo | (hi << 4));
    }
}

/**
 * @brief Decode the 128-byte length header back into 256 code lengths.
 *
 * Inverts ::pack_lengths_header and validates the two structural invariants:
 * no length exceeds `ZXC_HUF_MAX_CODE_LEN`, and at least one symbol is
 * present.
 *
 * @param[in]  in       Input header buffer of `ZXC_HUF_LENGTHS_HEADER_SIZE` bytes.
 * @param[out] code_len Output code-length array of length `ZXC_HUF_NUM_SYMBOLS`.
 * @return `ZXC_OK` on success, `ZXC_ERROR_CORRUPT_DATA` if a length is too
 *         large or the table is empty.
 */
static int unpack_lengths_header(const uint8_t* RESTRICT in, uint8_t* RESTRICT code_len) {
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
    if (UNLIKELY(max_len > ZXC_HUF_MAX_CODE_LEN)) return ZXC_ERROR_CORRUPT_DATA;
    if (UNLIKELY(n_present == 0)) return ZXC_ERROR_CORRUPT_DATA;
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

/**
 * @brief Initialise an LSB-first bit writer over a caller-owned buffer.
 *
 * @param[out] bw  Writer to initialise.
 * @param[out] dst Output buffer (writer takes no ownership).
 * @param[in]  cap Capacity of @p dst in bytes.
 */
static ZXC_ALWAYS_INLINE void bw_init(bit_writer_t* RESTRICT bw, uint8_t* RESTRICT dst,
                                      const size_t cap) {
    bw->ptr = dst;
    bw->end = dst + cap;
    bw->accum = 0;
    bw->bits = 0;
    bw->err = 0;
}

/**
 * @brief Append the low @p len bits of @p code to the writer's bitstream.
 *
 * Bits are consumed from the LSB end. When the internal accumulator has
 * accumulated 8 or more bits, full bytes are flushed to the output buffer.
 * If the buffer is exhausted mid-flush the writer's `err` flag is set;
 * subsequent ::bw_finish reports `ZXC_ERROR_DST_TOO_SMALL`.
 *
 * @param[in,out] bw   Writer state.
 * @param[in]     code Code bits to emit (the low @p len bits matter).
 * @param[in]     len  Number of bits to emit (1..ZXC_HUF_MAX_CODE_LEN).
 */
static ZXC_ALWAYS_INLINE void bw_put(bit_writer_t* RESTRICT bw, const uint32_t code,
                                     const int len) {
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

/**
 * @brief Flush any partial trailing byte and finalise the bit writer.
 *
 * Writes the (zero-padded) trailing byte if the accumulator holds any bits.
 *
 * @param[in,out] bw Writer state.
 * @return `ZXC_OK` on success, `ZXC_ERROR_DST_TOO_SMALL` if the buffer was
 *         exhausted at any point.
 */
static ZXC_ALWAYS_INLINE int bw_finish(bit_writer_t* RESTRICT bw) {
    if (bw->bits > 0) {
        if (UNLIKELY(bw->ptr >= bw->end)) return ZXC_ERROR_DST_TOO_SMALL;
        *bw->ptr++ = (uint8_t)bw->accum;
        bw->accum = 0;
        bw->bits = 0;
    }
    return UNLIKELY(bw->err) ? ZXC_ERROR_DST_TOO_SMALL : ZXC_OK;
}

/* ===========================================================================
 * Encoder
 * =========================================================================*/

/**
 * @copydoc zxc_huf_encode_section
 */
int zxc_huf_encode_section(const uint8_t* RESTRICT literals, const size_t n_literals,
                           const uint8_t* RESTRICT code_len, uint8_t* RESTRICT dst,
                           const size_t dst_cap) {
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
        if (UNLIKELY(s_sizes[s] > 0xFFFFu)) return ZXC_ERROR_DST_TOO_SMALL;
        sizes_hdr[2 * s] = (uint8_t)(s_sizes[s] & 0xFF);
        sizes_hdr[2 * s + 1] = (uint8_t)((s_sizes[s] >> 8) & 0xFF);
    }

    return (int)(p - dst);
}

/* ===========================================================================
 * Decoder table builder + 4-way interleaved decoder
 * =========================================================================*/

/**
 * @brief Build the 512-entry decoder lookup table from per-symbol code lengths.
 *
 * Validates the Kraft equality, generates the canonical LSB-first codes and
 * fills every table slot with the matching `(symbol, length)` pair packed
 * into a `uint16_t` (low byte = symbol, high byte = length). The single-
 * present-symbol degenerate case is handled by replicating the one valid
 * entry across the full table.
 *
 * @param[in]  code_len Per-symbol code lengths produced from the section header.
 * @param[out] table    Destination 512-entry lookup table (must be aligned by
 *                      the caller for hot-path performance).
 * @return `ZXC_OK` on success, `ZXC_ERROR_CORRUPT_DATA` if the lengths fail
 *         the Kraft check or yield an empty/invalid entry.
 */
static int build_decode_table(const uint8_t* RESTRICT code_len,
                              zxc_huf_dec_entry_t* RESTRICT table) {
    uint32_t bl_count[ZXC_HUF_MAX_CODE_LEN + 1] = {0};
    int n_present = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        const uint8_t l = code_len[i];
        if (UNLIKELY(l > ZXC_HUF_MAX_CODE_LEN)) return ZXC_ERROR_CORRUPT_DATA;
        bl_count[l]++;
        if (l) n_present++;
    }
    if (UNLIKELY(n_present == 0)) return ZXC_ERROR_CORRUPT_DATA;
    bl_count[0] = 0;

    /* Validate Kraft inequality (==): sum(2^(L - len)) must equal 2^L. */
    {
        uint64_t kraft = 0;
        for (int k = 1; k <= ZXC_HUF_MAX_CODE_LEN; k++) {
            kraft += (uint64_t)bl_count[k] << (ZXC_HUF_MAX_CODE_LEN - k);
        }
        /* Special case: a single present symbol with len 1 is the only valid degenerate code. */
        if (n_present == 1) {
            if (UNLIKELY(bl_count[1] != 1)) return ZXC_ERROR_CORRUPT_DATA;
        } else {
            if (UNLIKELY(kraft != ((uint64_t)1 << ZXC_HUF_MAX_CODE_LEN)))
                return ZXC_ERROR_CORRUPT_DATA;
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

    /* Final invariant: every entry must have a non-zero length byte so the
     * hot decode loop can skip its per-symbol length-validity check. */
    for (uint32_t i = 0; i < ZXC_HUF_TABLE_SIZE; i++) {
        if (UNLIKELY((table[i].entry >> 8) == 0)) return ZXC_ERROR_CORRUPT_DATA;
    }

    return ZXC_OK;
}

/**
 * @copydoc zxc_huf_decode_section
 */
int zxc_huf_decode_section(const uint8_t* RESTRICT payload, const size_t payload_size,
                           uint8_t* RESTRICT dst, const size_t n_literals) {
    if (UNLIKELY(payload_size < ZXC_HUF_HEADER_SIZE || n_literals == 0))
        return ZXC_ERROR_CORRUPT_DATA;

    /* 1. Parse length header. */
    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    {
        const int rc = unpack_lengths_header(payload, code_len);
        if (UNLIKELY(rc != ZXC_OK)) return rc;
    }

    /* 2. Build the 512-entry decode table. Cache-line aligned: the LUT
     * spans 16 lines and is hammered every symbol; landing it on a 64-byte
     * boundary avoids any cross-line load split. */
    __attribute__((aligned(ZXC_CACHE_LINE_SIZE))) zxc_huf_dec_entry_t table[ZXC_HUF_TABLE_SIZE];
    {
        const int rc = build_decode_table(code_len, table);
        if (UNLIKELY(rc != ZXC_OK)) return rc;
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

    /* Batched 4-way interleaved decode.
     *
     * Batch size is bounded by the byte-aligned bit stream: a refill grows
     * `bits` by an integer number of bytes, so from `bits = b` the post-
     * refill value is `b + 8 * floor((64 - b) / 8)`. For this to always
     * reach `bits >= K * L_max`, we need K * L_max <= 57. With L_max = 9
     * the largest safe batch is K = 6 (54 bits per refill, 24 symbols per
     * outer iteration).
     *
     * To minimise codegen overhead the four bit readers' hot fields
     * (accum / bits / ptr / end) are hoisted into local variables for the
     * full duration of the batched loop, the refill is inlined as a macro
     * (mirroring zxc_br_ensure), and the inner k-loop is manually unrolled
     * 6x. All entries in `table` are guaranteed by build_decode_table()
     * to have a non-zero length byte, so the per-symbol length check is
     * hoisted out of the hot path. */
#define ZXC_HUF_BATCH 6
#define ZXC_HUF_BATCH_BITS (ZXC_HUF_BATCH * ZXC_HUF_MAX_CODE_LEN) /* = 54 */
#define ZXC_HUF_TBL_MASK ((uint64_t)(ZXC_HUF_TABLE_SIZE - 1))

    uint8_t* d0 = s_dst[0];
    uint8_t* d1 = s_dst[1];
    uint8_t* d2 = s_dst[2];
    uint8_t* d3 = s_dst[3];

    const size_t batches = common / ZXC_HUF_BATCH;
    const size_t batched_syms = batches * ZXC_HUF_BATCH;

    /* Hoist all four bit-reader hot fields into locals.
     * They live in registers for the full duration of the batched loop. */
    uint64_t a0 = br[0].accum, a1 = br[1].accum, a2 = br[2].accum, a3 = br[3].accum;
    int bb0 = br[0].bits, bb1 = br[1].bits, bb2 = br[2].bits, bb3 = br[3].bits;
    const uint8_t *p0 = br[0].ptr, *p1 = br[1].ptr, *p2 = br[2].ptr, *p3 = br[3].ptr;
    const uint8_t* const e0 = br[0].end;
    const uint8_t* const e1 = br[1].end;
    const uint8_t* const e2 = br[2].end;
    const uint8_t* const e3 = br[3].end;

    /* Inline refill: identical semantics to zxc_br_ensure(., BATCH_BITS).
     * Hot path = single 8-byte LE load + shift-or; cold path handles the
     * tail near end-of-stream byte by byte. The byte count uses
     * `(64 - BB) >> 3` to add the maximum number of whole bytes that fit
     * in the remaining accumulator capacity.
     *
     * The accumulator's high bits (positions BB..63) are guaranteed zero
     * here: each DECODE_ONE() ends with `accum >>= len`, which is an
     * unsigned right-shift that zero-fills the top bits. No mask needed. */
#define REFILL(A, BB, P, E)                              \
    do {                                                 \
        if (LIKELY((BB) < ZXC_HUF_BATCH_BITS)) {         \
            if (LIKELY((P) + sizeof(uint64_t) <= (E))) { \
                (A) |= zxc_le64(P) << (BB);              \
                const int _n = (64 - (BB)) >> 3;         \
                (P) += _n;                               \
                (BB) += _n * 8;                          \
            } else {                                     \
                while ((BB) <= 56 && (P) < (E)) {        \
                    (A) |= ((uint64_t)*(P)++) << (BB);   \
                    (BB) += 8;                           \
                }                                        \
            }                                            \
        }                                                \
    } while (0)

    /* Decode 1 symbol from each of the 4 streams; advance pointers/state.
     * `bits` is intentionally NOT decremented here: the per-stream length
     * accumulators (sl0..sl3) sum the consumed bits and are folded back
     * into bb0..bb3 once at end of batch. This trades 4 sub/iter (24 per
     * batch) for 4 add/iter (24) + 4 sub/batch — same critical path on the
     * accum chain, fewer ALU µops on x86 with limited add/sub ports. */
#define DECODE_ONE()                                             \
    do {                                                         \
        const uint16_t _e0 = table[a0 & ZXC_HUF_TBL_MASK].entry; \
        const uint16_t _e1 = table[a1 & ZXC_HUF_TBL_MASK].entry; \
        const uint16_t _e2 = table[a2 & ZXC_HUF_TBL_MASK].entry; \
        const uint16_t _e3 = table[a3 & ZXC_HUF_TBL_MASK].entry; \
        *d0++ = (uint8_t)_e0;                                    \
        *d1++ = (uint8_t)_e1;                                    \
        *d2++ = (uint8_t)_e2;                                    \
        *d3++ = (uint8_t)_e3;                                    \
        const int _l0 = _e0 >> 8;                                \
        const int _l1 = _e1 >> 8;                                \
        const int _l2 = _e2 >> 8;                                \
        const int _l3 = _e3 >> 8;                                \
        a0 >>= _l0;                                              \
        a1 >>= _l1;                                              \
        a2 >>= _l2;                                              \
        a3 >>= _l3;                                              \
        sl0 += _l0;                                              \
        sl1 += _l1;                                              \
        sl2 += _l2;                                              \
        sl3 += _l3;                                              \
    } while (0)

    for (size_t b = 0; b < batches; b++) {
        REFILL(a0, bb0, p0, e0);
        REFILL(a1, bb1, p1, e1);
        REFILL(a2, bb2, p2, e2);
        REFILL(a3, bb3, p3, e3);

        int sl0 = 0, sl1 = 0, sl2 = 0, sl3 = 0;
        DECODE_ONE();
        DECODE_ONE();
        DECODE_ONE();
        DECODE_ONE();
        DECODE_ONE();
        DECODE_ONE();
        bb0 -= sl0;
        bb1 -= sl1;
        bb2 -= sl2;
        bb3 -= sl3;
    }

    /* Vectorised tail. Up to ZXC_HUF_BATCH-1 = 5 symbols per stream
     * remain in the common phase, plus up to 3 more on streams 0..2 when
     * n_literals is not a multiple of 4 (stream 3 is always the shortest,
     * so its count equals `common`). Bit-reader hot fields stay in the
     * same locals used by the batched loop — no per-symbol zxc_br_ensure
     * load/store of br->{accum,bits,ptr}. Phase A keeps 4-way ILP for
     * the common-phase remainder; Phase B finishes the few stragglers on
     * streams 0..2 sequentially. */

    /* Phase A: common-phase tail, 4-way interleaved (0..5 iterations). */
    const size_t common_tail = common - batched_syms;
    for (size_t i = 0; i < common_tail; i++) {
        REFILL(a0, bb0, p0, e0);
        REFILL(a1, bb1, p1, e1);
        REFILL(a2, bb2, p2, e2);
        REFILL(a3, bb3, p3, e3);

        const uint16_t _e0 = table[a0 & ZXC_HUF_TBL_MASK].entry;
        const uint16_t _e1 = table[a1 & ZXC_HUF_TBL_MASK].entry;
        const uint16_t _e2 = table[a2 & ZXC_HUF_TBL_MASK].entry;
        const uint16_t _e3 = table[a3 & ZXC_HUF_TBL_MASK].entry;
        *d0++ = (uint8_t)_e0;
        *d1++ = (uint8_t)_e1;
        *d2++ = (uint8_t)_e2;
        *d3++ = (uint8_t)_e3;
        const int _l0 = _e0 >> 8, _l1 = _e1 >> 8;
        const int _l2 = _e2 >> 8, _l3 = _e3 >> 8;
        a0 >>= _l0; a1 >>= _l1; a2 >>= _l2; a3 >>= _l3;
        bb0 -= _l0; bb1 -= _l1; bb2 -= _l2; bb3 -= _l3;
    }

    /* Phase B: extra symbols on streams 0..2 (≤ 3 each, when N % 4 != 0). */
#define TAIL_ONE(A, BB, P, E, D)                                  \
    do {                                                          \
        REFILL(A, BB, P, E);                                      \
        const uint16_t _e = table[(A) & ZXC_HUF_TBL_MASK].entry;  \
        const int _l = _e >> 8;                                   \
        *(D)++ = (uint8_t)_e;                                     \
        (A) >>= _l;                                               \
        (BB) -= _l;                                               \
    } while (0)

    for (size_t i = common; i < s_count[0]; i++) TAIL_ONE(a0, bb0, p0, e0, d0);
    for (size_t i = common; i < s_count[1]; i++) TAIL_ONE(a1, bb1, p1, e1, d1);
    for (size_t i = common; i < s_count[2]; i++) TAIL_ONE(a2, bb2, p2, e2, d2);

#undef TAIL_ONE
#undef DECODE_ONE
#undef REFILL
#undef ZXC_HUF_BATCH
#undef ZXC_HUF_BATCH_BITS
#undef ZXC_HUF_TBL_MASK
    return ZXC_OK;
}
