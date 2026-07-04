/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/**
 * @file zxc_huffman.c
 * @brief Canonical, length-limited (ZXC_HUF_MAX_CODE_LEN_ULTRA) Huffman codec for the GLO literal
 *
 * Canonical, length-limited (ZXC_HUF_MAX_CODE_LEN_ULTRA) Huffman codec for the GLO literal
 * stream at compression level >= 6. Codes are emitted LSB-first; the
 * decoder uses a 2048-entry multi-symbol lookup table (11-bit lookup,
 * 1 or 2 symbols per lookup depending on the cumulative code length)
 * and a 4-way interleaved hot loop. Public declarations live in
 * zxc_internal.h; the rest is private to this translation unit.
 */

/*
 * Function Multi-Versioning Support
 * If ZXC_FUNCTION_SUFFIX is defined (e.g. _avx2, _neon), rename the public
 * entry points so each variant TU produces its own copy under a unique symbol
 * (e.g. zxc_huf_decode_section_avx2). The runtime dispatcher in
 * zxc_compress.c / zxc_decompress.c routes to the matching variant.
 *
 * The defines sit before zxc_internal.h so the header's prototypes are
 * rewritten with the same suffix as the definitions below.
 */
#ifdef ZXC_FUNCTION_SUFFIX
#define ZXC_CAT_IMPL(x, y) x##y
#define ZXC_CAT(x, y) ZXC_CAT_IMPL(x, y)
#define zxc_huf_build_code_lengths ZXC_CAT(zxc_huf_build_code_lengths, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_encode_section ZXC_CAT(zxc_huf_encode_section, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_decode_section ZXC_CAT(zxc_huf_decode_section, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_encode_section_dict ZXC_CAT(zxc_huf_encode_section_dict, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_decode_section_dict ZXC_CAT(zxc_huf_decode_section_dict, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_build_dec_table ZXC_CAT(zxc_huf_build_dec_table, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_pack_lengths ZXC_CAT(zxc_huf_pack_lengths, ZXC_FUNCTION_SUFFIX)
#define zxc_huf_unpack_lengths ZXC_CAT(zxc_huf_unpack_lengths, ZXC_FUNCTION_SUFFIX)
#define zxc_pivco_calc_size ZXC_CAT(zxc_pivco_calc_size, ZXC_FUNCTION_SUFFIX)
#define zxc_pivco_encode_section ZXC_CAT(zxc_pivco_encode_section, ZXC_FUNCTION_SUFFIX)
#define zxc_pivco_encode_section_dict ZXC_CAT(zxc_pivco_encode_section_dict, ZXC_FUNCTION_SUFFIX)
#define zxc_pivco_decode_section ZXC_CAT(zxc_pivco_decode_section, ZXC_FUNCTION_SUFFIX)
#define zxc_pivco_decode_section_dict ZXC_CAT(zxc_pivco_decode_section_dict, ZXC_FUNCTION_SUFFIX)
#endif

#include "../../include/zxc_error.h"
#include "zxc_internal.h"

/* The decoder lookup table entry type (zxc_huf_dec_entry_t) lives in
 * zxc_internal.h so the compression context can carry a prebuilt table for
 * the shared dictionary literal table. Bit layout recap:
 * sym1(0..7) | sym2(8..15) | len1(16..19) | n_extra(24) | len_total(28..31). */
#define ZXC_HUF_ENTRY(sym1, sym2, len1, len_total, n_extra)                  \
    ((uint32_t)(sym1) | ((uint32_t)(sym2) << 8) | ((uint32_t)(len1) << 16) | \
     ((uint32_t)(n_extra) << 24) | ((uint32_t)(len_total) << 28))

/* ===========================================================================
 * Length-limited Huffman: boundary package-merge
 * ===========================================================================
 *
 * Builds optimal length-limited Huffman code lengths (max length
 * ZXC_HUF_MAX_CODE_LEN_ULTRA) on 256-symbol alphabets. Package-merge is run for
 * ZXC_HUF_MAX_CODE_LEN_ULTRA levels; each level holds up to 2N items (leaves +
 * paired packages). Selection of the cheapest 2N - 2 items at level
 * ZXC_HUF_MAX_CODE_LEN_ULTRA gives the appearance count of each leaf, which is
 * its code length.
 */

typedef zxc_huf_pm_item_t pm_item_t;

typedef struct {
    uint32_t w;
    int16_t sym;
} pm_leaf_t;

typedef zxc_huf_pm_frame_t frame_t;

/**
 * @brief Sort `pm_leaf_t` array by ascending weight, ties broken by ascending symbol.
 *
 * Bucket sort on `floor(log2(weight))` (32 buckets), with insertion sort
 * inside each bucket. Replaces a libc `qsort` call: the comparator's
 * indirect call dominated, and frequency distributions cluster naturally
 * across ~10-14 magnitude buckets, so intra-bucket lists stay short and
 * insertion sort is branch-friendly. Deterministic tie-break on `sym` is
 * applied inside the insertion sort.
 *
 * Precondition: all weights are > 0 (zero-frequency symbols are filtered
 * by the caller before this runs).
 *
 * @param[in,out] leaves  Leaf array, sorted in place (ascending weight, then
 *                        ascending @c sym on ties).
 * @param[in]     n       Number of leaves; @c n < 2 is effectively a no-op.
 */
static void pm_leaves_sort(pm_leaf_t* RESTRICT leaves, const int n) {
    /* One bucket per possible value of floor(log2(weight)) for a 32-bit
     * weight, i.e. 32 buckets. */
    enum { NUM_BUCKETS = 32 };
    int count[NUM_BUCKETS];
    int offset[NUM_BUCKETS + 1]; /* +1 sentinel = n, avoids end-of-bucket branch. */
    uint8_t bucket_of[ZXC_HUF_NUM_SYMBOLS];
    pm_leaf_t tmp[ZXC_HUF_NUM_SYMBOLS];

    ZXC_MEMSET(count, 0, sizeof(count));
    for (int i = 0; i < n; i++) {
        const unsigned b = zxc_log2_u32(leaves[i].w);
        bucket_of[i] = (uint8_t)b;
        count[b]++;
    }

    int acc = 0;
    for (int b = 0; b < NUM_BUCKETS; b++) {
        offset[b] = acc;
        acc += count[b];
    }
    offset[NUM_BUCKETS] = n;

    int pos[NUM_BUCKETS];
    ZXC_MEMCPY(pos, offset, sizeof(pos));
    for (int i = 0; i < n; i++) {
        tmp[pos[bucket_of[i]]++] = leaves[i];
    }

    for (int b = 0; b < NUM_BUCKETS; b++) {
        if (count[b] < 2) continue;
        const int s = offset[b];
        const int e = offset[b + 1];
        for (int i = s + 1; i < e; i++) {
            const pm_leaf_t key = tmp[i];
            int j = i - 1;
            while (j >= s && (tmp[j].w > key.w || (tmp[j].w == key.w && tmp[j].sym > key.sym))) {
                tmp[j + 1] = tmp[j];
                j--;
            }
            tmp[j + 1] = key;
        }
    }

    ZXC_MEMCPY(leaves, tmp, (size_t)n * sizeof(pm_leaf_t));
}

/**
 * @brief Build length-limited canonical Huffman code lengths.
 *
 * Runs the boundary package-merge algorithm capped at `ZXC_HUF_MAX_CODE_LEN_ULTRA`.
 * Symbols with `freq[i] == 0` get `code_len[i] == 0`; every other symbol
 * receives a length in `[1, ZXC_HUF_MAX_CODE_LEN_ULTRA]`. The single-present-symbol
 * case is handled as a degenerate code of length 1.
 *
 * @param[in]  freq     Frequency table indexed by symbol (0..255).
 * @param[out] code_len Output code-length array, written in full.
 * @param[in]  scratch  Optional scratch of `ZXC_HUF_BUILD_SCRATCH_SIZE` bytes
 *                      (carved into items / counts / stack regions). If
 *                      `NULL`, the function allocates its own working memory
 *                      for the duration of the call.
 * @return `ZXC_OK` on success, `ZXC_ERROR_MEMORY` or `ZXC_ERROR_CORRUPT_DATA`
 *         on failure.
 */
int zxc_huf_build_code_lengths(const uint32_t* RESTRICT freq, uint8_t* RESTRICT code_len,
                               void* RESTRICT scratch, const int max_code_len) {
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

    pm_leaves_sort(leaves, n);

    /* Callers pass max_code_len >= 8, and n <= 256 <= 2^max_code_len, so the
     * length limit is always feasible (a full 8-bit code covers all 256 symbols). */
    const int max_per_level = 2 * n;

    /* Working buffers: either carve from caller-provided scratch (sized for
     * the worst-case alphabet) or fall back to per-call malloc/free. */
    pm_item_t* items;
    int* counts;
    frame_t* stack;
    pm_item_t* owned_items = NULL;
    int* owned_counts = NULL;
    frame_t* owned_stack = NULL;
    if (scratch) {
        uint8_t* p = (uint8_t*)scratch;
        items = (pm_item_t*)p;
        p +=
            (size_t)ZXC_HUF_MAX_CODE_LEN_ULTRA * (size_t)ZXC_HUF_PM_LEVEL_BOUND * sizeof(pm_item_t);
        p = (uint8_t*)(((uintptr_t)p + 7u) & ~(uintptr_t)7u);
        counts = (int*)p;
        ZXC_MEMSET(counts, 0, (size_t)ZXC_HUF_MAX_CODE_LEN_ULTRA * sizeof(int));
        p += (size_t)ZXC_HUF_MAX_CODE_LEN_ULTRA * sizeof(int);
        p = (uint8_t*)(((uintptr_t)p + 7u) & ~(uintptr_t)7u);
        stack = (frame_t*)p;
    } else {
        owned_items = (pm_item_t*)ZXC_MALLOC((size_t)ZXC_HUF_MAX_CODE_LEN_ULTRA *
                                             (size_t)max_per_level * sizeof(pm_item_t));
        owned_counts = (int*)ZXC_CALLOC((size_t)ZXC_HUF_MAX_CODE_LEN_ULTRA, sizeof(int));
        owned_stack = (frame_t*)ZXC_MALLOC((size_t)ZXC_HUF_MAX_CODE_LEN_ULTRA *
                                           (size_t)max_per_level * sizeof(frame_t));
        if (UNLIKELY(!owned_items || !owned_counts || !owned_stack)) {
            ZXC_FREE(owned_items);
            ZXC_FREE(owned_counts);
            ZXC_FREE(owned_stack);
            return ZXC_ERROR_MEMORY;
        }
        items = owned_items;
        counts = owned_counts;
        stack = owned_stack;
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

    /* Levels 1..max_code_len-1: merge sorted leaves with sorted packages from the previous
     * level. */
    for (int k = 1; k < max_code_len; k++) {
        const int prev = counts[k - 1];
        const int packs = prev / 2;
        int li = 0;
        int pi = 0;
        int n_lvl = 0;
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

    /* Step 3: take first 2n-2 items at level max_code_len-1; trace back, counting leaf
     * appearances. */
    int n_take = 2 * n - 2;
    if (n_take > counts[max_code_len - 1]) n_take = counts[max_code_len - 1];

    /* Worst case stack depth: (max_code_len * n_take) frames; bounded by
     * max_code_len * 2n <= ZXC_HUF_MAX_CODE_LEN_ULTRA * 2n. `stack` was set up earlier from
     * scratch (or the local malloc fallback), sized for the ceiling. */
    int sp = 0;
    for (int i = 0; i < n_take; i++) {
        stack[sp].lvl = (int8_t)(max_code_len - 1);
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

    if (owned_items) {
        ZXC_FREE(owned_items);
        ZXC_FREE(owned_counts);
        ZXC_FREE(owned_stack);
    }
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
 * Generates MSB-first canonical codes following RFC 1951 3.2.2, then
 * bit-reverses each so the encoder can emit them with a plain
 * `accum |= code << bits` step. Absent symbols (length 0) receive code 0.
 *
 * @param[in]  code_len Per-symbol code lengths.
 * @param[out] codes    Per-symbol LSB-first canonical codes.
 */
static void build_canonical_codes(const uint8_t* RESTRICT code_len, uint32_t* RESTRICT codes) {
    uint32_t bl_count[ZXC_HUF_MAX_CODE_LEN_ULTRA + 1] = {0};
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        bl_count[code_len[i]]++;
    }
    bl_count[0] = 0;

    uint32_t next_code[ZXC_HUF_MAX_CODE_LEN_ULTRA + 2] = {0};
    uint32_t code = 0;
    for (int k = 1; k <= ZXC_HUF_MAX_CODE_LEN_ULTRA + 1; k++) {
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
 * silently truncates any length > 15; callers must enforce the cap of
 * `ZXC_HUF_MAX_CODE_LEN_ULTRA` (<= 15) before calling.
 *
 * @param[in]  code_len Per-symbol code lengths (length `ZXC_HUF_NUM_SYMBOLS`).
 * @param[out] out      Output header buffer of `ZXC_HUF_TABLE_SIZE` bytes.
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
 * no length exceeds `ZXC_HUF_MAX_CODE_LEN_ULTRA`, and at least one symbol is
 * present.
 *
 * @param[in]  in       Input header buffer of `ZXC_HUF_TABLE_SIZE` bytes.
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
    if (UNLIKELY(max_len > ZXC_HUF_MAX_CODE_LEN_ULTRA || n_present == 0))
        return ZXC_ERROR_CORRUPT_DATA;
    return ZXC_OK;
}

/* ===========================================================================
 * Bit writer (LSB-first)
 * =========================================================================*/

/**
 * @brief LSB-first bit writer over a caller-owned output buffer.
 *
 * Codes are appended at the LSB end of @c accum (see ::bw_put) and flushed to
 * @c ptr one full byte at a time. A mid-stream overrun is not fatal: @c err
 * latches so callers check for failure once via ::bw_finish instead of after
 * every write.
 */
typedef struct {
    uint8_t* ptr;   /**< Next byte to write; advances as full bytes are flushed. */
    uint8_t* end;   /**< One past the last writable byte of the buffer. */
    uint64_t accum; /**< LSB-first accumulator of bits not yet flushed. */
    int bits;       /**< Count of valid low bits currently held in @c accum. */
    int err;        /**< Sticky overrun flag; ::bw_finish maps it to
                     *   @c ZXC_ERROR_DST_TOO_SMALL. */
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
 * @param[in]     len  Number of bits to emit (1..ZXC_HUF_MAX_CODE_LEN_ULTRA).
 */
static ZXC_ALWAYS_INLINE void bw_put(bit_writer_t* RESTRICT bw, const uint32_t code,
                                     const int len) {
    bw->accum |= ((uint64_t)code) << bw->bits;
    bw->bits += len;
    if (LIKELY((size_t)(bw->end - bw->ptr) >= sizeof(uint64_t))) {
        zxc_store_le64(bw->ptr, bw->accum);
    } else {
        if (UNLIKELY(bw->ptr >= bw->end)) {
            bw->err = 1;
            bw->bits = 0;
            return;
        }
        *bw->ptr = (uint8_t)bw->accum;
    }
    const int n = bw->bits >> 3; /* 0 or 1 full byte to flush */
    bw->ptr += n;
    bw->accum >>= n << 3;
    bw->bits &= 7;
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
 * @brief Shared encoder body: 6-byte sub-stream sizes header + 4 interleaved
 *        sub-streams, written at @p dst. The 128-byte lengths header, when
 *        wanted, is the caller's business (see the two public wrappers).
 *
 * @param[in]  literals    Source literal bytes (must not alias @p dst).
 * @param[in]  n_literals  Number of source bytes (must be > 0).
 * @param[in]  code_len    Per-symbol code lengths for the canonical codes.
 * @param[out] dst         Destination for the sizes header + sub-streams.
 * @param[in]  dst_cap     Capacity of @p dst in bytes.
 * @return Bytes written (>= ZXC_HUF_STREAM_SIZES_HEADER_SIZE) on success,
 *         negative `zxc_error_t` code on failure.
 */
static int zxc_huf_encode_streams(const uint8_t* RESTRICT literals, const size_t n_literals,
                                  const uint8_t* RESTRICT code_len, uint8_t* RESTRICT dst,
                                  const size_t dst_cap) {
    if (UNLIKELY(n_literals == 0)) return ZXC_ERROR_CORRUPT_DATA;
    if (UNLIKELY(dst_cap < (size_t)ZXC_HUF_STREAM_SIZES_HEADER_SIZE))
        return ZXC_ERROR_DST_TOO_SMALL;

    /* 1. Build canonical codes (LSB-first via bit-reversal). */
    uint32_t codes[ZXC_HUF_NUM_SYMBOLS];
    build_canonical_codes(code_len, codes);

    /* 2. Reserve 6 bytes for sub-stream sizes; encode 4 sub-streams after them. */
    uint8_t* const sizes_hdr = dst;
    uint8_t* const stream_base = sizes_hdr + ZXC_HUF_STREAM_SIZES_HEADER_SIZE;
    const uint8_t* const stream_end = dst + dst_cap;

    const size_t Q = (n_literals + ZXC_HUF_NUM_STREAMS - 1) / ZXC_HUF_NUM_STREAMS;

    bit_writer_t bw;
    uint8_t* p = stream_base;
    size_t s_sizes[ZXC_HUF_NUM_STREAMS];

    for (int s = 0; s < ZXC_HUF_NUM_STREAMS; s++) {
        const size_t start = (size_t)s * Q;
        size_t stop = start + Q;
        if (stop > n_literals) stop = n_literals;

        const uint8_t* const stream_start = p;
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

    /* 3. Persist the 3 explicit sub-stream sizes (s4 is implied). */
    for (int s = 0; s < ZXC_HUF_NUM_STREAMS - 1; s++) {
        if (UNLIKELY(s_sizes[s] > 0xFFFFu)) return ZXC_ERROR_DST_TOO_SMALL;
        zxc_store_le16(sizes_hdr + 2 * s, (uint16_t)s_sizes[s]);
    }

    return (int)(p - dst);
}

/**
 * @brief Encode the literal stream into a full Huffman section payload.
 *
 * Packs the 128-byte lengths header, then delegates to
 * @ref zxc_huf_encode_streams for the 6-byte sub-stream sizes and the 4
 * interleaved LSB-first bit-streams.
 *
 * @param[in]  literals    Source literal bytes (must not alias @p dst).
 * @param[in]  n_literals  Number of source bytes (must be > 0).
 * @param[in]  code_len    Per-symbol code lengths (see @ref zxc_huf_build_code_lengths).
 * @param[out] dst         Destination buffer for the section payload.
 * @param[in]  dst_cap     Capacity of @p dst in bytes.
 * @return Total bytes written on success, negative `zxc_error_t` on failure.
 */
int zxc_huf_encode_section(const uint8_t* RESTRICT literals, const size_t n_literals,
                           const uint8_t* RESTRICT code_len, uint8_t* RESTRICT dst,
                           const size_t dst_cap) {
    if (UNLIKELY(n_literals == 0)) return ZXC_ERROR_CORRUPT_DATA;
    if (UNLIKELY(dst_cap < ZXC_HUF_HEADER_SIZE)) return ZXC_ERROR_DST_TOO_SMALL;

    /* Pack the 128-byte length header, then the streams after it. */
    pack_lengths_header(code_len, dst);
    const int rc = zxc_huf_encode_streams(literals, n_literals, code_len, dst + ZXC_HUF_TABLE_SIZE,
                                          dst_cap - ZXC_HUF_TABLE_SIZE);
    return (rc < 0) ? rc : rc + ZXC_HUF_TABLE_SIZE;
}

/**
 * @brief Encode a literal section using supplied code lengths, WITHOUT the
 *        128-byte lengths header (shared dictionary table).
 *
 * Emits only the 6-byte sub-stream sizes header + 4 sub-streams (a thin pass
 * through @ref zxc_huf_encode_streams); the lengths live in the dictionary.
 *
 * @param[in]  literals    Source literal bytes (must not alias @p dst).
 * @param[in]  n_literals  Number of source bytes (must be > 0).
 * @param[in]  code_len    Per-symbol code lengths from the shared dict table.
 * @param[out] dst         Destination buffer for the section payload.
 * @param[in]  dst_cap     Capacity of @p dst in bytes.
 * @return Bytes written on success, negative `zxc_error_t` on failure
 *         (incl. `ZXC_ERROR_CORRUPT_DATA` if a literal has no code).
 */
int zxc_huf_encode_section_dict(const uint8_t* RESTRICT literals, const size_t n_literals,
                                const uint8_t* RESTRICT code_len, uint8_t* RESTRICT dst,
                                const size_t dst_cap) {
    return zxc_huf_encode_streams(literals, n_literals, code_len, dst, dst_cap);
}

/* ===========================================================================
 * Decoder table builder + 4-way interleaved decoder
 * =========================================================================*/

/**
 * @brief Build the 2048-entry multi-symbol decoder lookup table.
 *
 * Strategy: build a temporary single-symbol table (ZXC_HUF_SS_SIZE entries,
 * ZXC_HUF_MAX_CODE_LEN_ULTRA-bit index), then use it to populate the 2048-entry
 * (11-bit) multi-symbol table. For each 11-bit prefix p:
 *   1. (sym1, len1) = ss[p & ZXC_HUF_SS_MASK] -- always valid, 1 <= len1 <= 11.
 *   2. rem = 11 - len1 E [0, 10] bits remain after consuming the first code.
 *   3. (sym2_cand, len2_cand) = ss[(p >> len1) & ZXC_HUF_SS_MASK]. If len2_cand <= rem,
 *      both codes fit in 11 bits -> encode 2-symbol entry. Otherwise the
 *      second code's bit window extends past the lookup width -> keep only
 *      the first symbol and let the next iteration handle the rest.
 *
 * Validates Kraft equality (or the single-present-symbol degenerate case).
 *
 * @param[in]  code_len Per-symbol code lengths from the section header.
 * @param[out] table    Destination 2048-entry lookup table (caller-aligned).
 * @return `ZXC_OK` on success, `ZXC_ERROR_CORRUPT_DATA` on validation failure.
 */
static int build_decode_table(const uint8_t* RESTRICT code_len,
                              zxc_huf_dec_entry_t* RESTRICT table) {
    uint32_t bl_count[ZXC_HUF_MAX_CODE_LEN_ULTRA + 1] = {0};
    int n_present = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        const uint8_t l = code_len[i];
        if (UNLIKELY(l > ZXC_HUF_MAX_CODE_LEN_ULTRA)) return ZXC_ERROR_CORRUPT_DATA;
        bl_count[l]++;
        if (l) n_present++;
    }
    if (UNLIKELY(n_present == 0)) return ZXC_ERROR_CORRUPT_DATA;
    bl_count[0] = 0;

    /* Validate Kraft equality on the ZXC_HUF_MAX_CODE_LEN_ULTRA axis. */
    {
        uint64_t kraft = 0;
        for (int k = 1; k <= ZXC_HUF_MAX_CODE_LEN_ULTRA; k++) {
            kraft += (uint64_t)bl_count[k] << (ZXC_HUF_MAX_CODE_LEN_ULTRA - k);
        }
        /* Degenerate: single symbol with length 1 (Kraft sum =
         * 2^(ZXC_HUF_MAX_CODE_LEN_ULTRA-1)). Otherwise: full Kraft equality
         * on the ZXC_HUF_MAX_CODE_LEN_ULTRA axis. */
        const int kraft_ok = (n_present == 1)
                                 ? (bl_count[1] == 1)
                                 : (kraft == ((uint64_t)1 << ZXC_HUF_MAX_CODE_LEN_ULTRA));
        if (UNLIKELY(!kraft_ok)) return ZXC_ERROR_CORRUPT_DATA;
    }

    uint32_t next_code[ZXC_HUF_MAX_CODE_LEN_ULTRA + 2] = {0};
    {
        uint32_t code = 0;
        for (int k = 1; k <= ZXC_HUF_MAX_CODE_LEN_ULTRA + 1; k++) {
            code = (code + bl_count[k - 1]) << 1;
            next_code[k] = code;
        }
    }

    /* Single-symbol intermediate (ZXC_HUF_MAX_CODE_LEN_ULTRA-bit lookup). Layout:
     * low byte = sym, high byte = len. Filled by replicating each canonical
     * code across all ZXC_HUF_MAX_CODE_LEN_ULTRA-bit windows that share its low
     * `len` bits. */
#define ZXC_HUF_SS_SIZE (1u << ZXC_HUF_MAX_CODE_LEN_ULTRA)
#define ZXC_HUF_SS_MASK ((uint32_t)(ZXC_HUF_SS_SIZE - 1))
    uint16_t ss[ZXC_HUF_SS_SIZE] = {0};

    for (int sym = 0; sym < ZXC_HUF_NUM_SYMBOLS; sym++) {
        const int l = code_len[sym];
        if (l == 0) continue;
        const uint32_t msb_code = next_code[l]++;
        const uint32_t lsb_code = reverse_bits(msb_code, l);
        const uint16_t entry = (uint16_t)((unsigned)l << 8 | (unsigned)sym);
        const uint32_t step = (uint32_t)1 << l;
        for (uint32_t fill = lsb_code; fill < ZXC_HUF_SS_SIZE; fill += step) {
            ss[fill] = entry;
        }
    }

    /* Single-symbol degenerate (Kraft sum = 2^(ZXC_HUF_MAX_CODE_LEN_ULTRA-1)): replicate the one
     * valid entry across every slot. */
    if (UNLIKELY(n_present == 1)) {
        uint16_t valid = 0;
        for (uint32_t i = 0; i < ZXC_HUF_SS_SIZE; i++) {
            if (ss[i] != 0) {
                valid = ss[i];
                break;
            }
        }
        for (uint32_t i = 0; i < ZXC_HUF_SS_SIZE; i++) {
            if (ss[i] == 0) ss[i] = valid;
        }
    }

    /* Build the multi-symbol table. */
    for (uint32_t p = 0; p < ZXC_HUF_DEC_TABLE_SIZE; p++) {
        const uint16_t e1 = ss[p & ZXC_HUF_SS_MASK];
        const uint8_t sym1 = (uint8_t)e1;
        const int len1 = e1 >> 8;
        const int rem = ZXC_HUF_LOOKUP_BITS - len1;

        uint8_t sym2 = 0;
        int len_total = len1;
        int n_extra = 0;

        const uint16_t e2 = ss[(p >> len1) & ZXC_HUF_SS_MASK];
        const int len2 = e2 >> 8;
        if (len2 <= rem) {
            sym2 = (uint8_t)e2;
            len_total = len1 + len2;
            n_extra = 1;
        }

        table[p].entry = ZXC_HUF_ENTRY(sym1, sym2, len1, len_total, n_extra);
    }
#undef ZXC_HUF_SS_SIZE
#undef ZXC_HUF_SS_MASK

    return ZXC_OK;
}

/**
 * @brief Shared decoder body: parses the 6-byte sub-stream sizes header at
 *        @p payload and runs the 4-way interleaved decode with @p table.
 *        The 128-byte lengths header, when present, has already been consumed
 *        by the caller (see the two public wrappers).
 *
 * @param[in]  payload       Sizes header followed by the 4 sub-streams.
 * @param[in]  payload_size  Size of @p payload in bytes.
 * @param[out] dst           Destination for the decoded literals.
 * @param[in]  n_literals    Number of literals to decode (must be > 0).
 * @param[in]  table         Multi-symbol decode table built for this section.
 * @return @c ZXC_OK on success, @c ZXC_ERROR_CORRUPT_DATA on a malformed stream.
 */
static int zxc_huf_decode_streams(const uint8_t* RESTRICT payload, const size_t payload_size,
                                  uint8_t* RESTRICT dst, const size_t n_literals,
                                  const zxc_huf_dec_entry_t* RESTRICT table) {
    if (UNLIKELY(payload_size < (size_t)ZXC_HUF_STREAM_SIZES_HEADER_SIZE || n_literals == 0))
        return ZXC_ERROR_CORRUPT_DATA;

    /* Parse the 3 leading sub-stream sizes; the 4th is the remainder. */
    const size_t s1 = zxc_le16(payload + 0);
    const size_t s2 = zxc_le16(payload + 2);
    const size_t s3 = zxc_le16(payload + 4);
    const size_t streams_total = payload_size - (size_t)ZXC_HUF_STREAM_SIZES_HEADER_SIZE;
    const size_t s123 = s1 + s2 + s3;
    if (UNLIKELY(s123 > streams_total)) return ZXC_ERROR_CORRUPT_DATA;
    const size_t s4 = streams_total - s123;

    const uint8_t* const base = payload + ZXC_HUF_STREAM_SIZES_HEADER_SIZE;

    /* Slim per-stream hot state -- 3 values instead of the classic 6:
     *   aN   decode accumulator (LSB-first bit window);
     *   bpN  absolute bit pointer, (address * 8) + bit: replaces (ptr, nbits);
     *   dN   output cursor.
     * A refill is a fresh 8-byte load at bp>>3 shifted by bp&7 in [0,7]: it
     * does not depend on the previous accumulator (no OR-in carry chain, and
     * the shift count can never go negative). Guards live outside the hot
     * loop: each outer iteration computes how many batches are safe on both
     * the output side (dst headroom) and the input side (the 8-byte refill
     * load must stay inside the payload), then runs them guard-free. */
    const uint64_t base_bp = (uint64_t)(uintptr_t)base << 3;
    uint64_t bp0 = base_bp;
    uint64_t bp1 = base_bp + (s1 << 3);
    uint64_t bp2 = base_bp + ((s1 + s2) << 3);
    uint64_t bp3 = base_bp + (s123 << 3);
    uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;

    /* Each sub-stream owns a contiguous dst slice: stream s covers literal
     * indices [s*Q, min((s+1)*Q, N)) with Q = ceil(N/4). */
    const size_t Q = (n_literals + ZXC_HUF_NUM_STREAMS - 1) / ZXC_HUF_NUM_STREAMS;
    const size_t c1 = (Q < n_literals) ? Q : n_literals;
    const size_t c2 = (2 * Q < n_literals) ? 2 * Q : n_literals;
    const size_t c3 = (3 * Q < n_literals) ? 3 * Q : n_literals;
    uint8_t* d0 = dst;
    uint8_t* d1 = dst + c1;
    uint8_t* d2 = dst + c2;
    uint8_t* d3 = dst + c3;
    const uint8_t* const dend0 = dst + c1;
    const uint8_t* const dend1 = dst + c2;
    const uint8_t* const dend2 = dst + c3;
    const uint8_t* const dend3 = dst + n_literals;

    /* Streams other than the last may over-read into the following stream's
     * bytes near their end -- harmless: those bits are only ever consumed on
     * corrupt input, output stays bounded by dend, and the block checksum
     * catches the garbage. Memory safety only requires the 8-byte refill load
     * to stay inside the payload (bp <= bp_load_max). */
    if (streams_total >= sizeof(uint64_t)) {
        const uint64_t bp_load_max =
            ((uint64_t)(uintptr_t)(base + streams_total - sizeof(uint64_t))) << 3;
        for (;;) {
            /* Output budget: a batch advances a stream by at most 2*BATCH. */
            size_t min_out = (size_t)(dend0 - d0);
            size_t r = (size_t)(dend1 - d1);
            if (r < min_out) min_out = r;
            r = (size_t)(dend2 - d2);
            if (r < min_out) min_out = r;
            r = (size_t)(dend3 - d3);
            if (r < min_out) min_out = r;
            if (min_out < ZXC_HUF_SAFE_MARGIN) break;

            /* Input budget: a batch consumes at most ZXC_HUF_BATCH_BITS bits
             * per stream, refilled once at batch start. */
            if (UNLIKELY(bp0 > bp_load_max || bp1 > bp_load_max || bp2 > bp_load_max ||
                         bp3 > bp_load_max))
                break; /* a refill would read past the payload */
            uint64_t min_in = bp_load_max - bp0;
            uint64_t rin = bp_load_max - bp1;
            if (rin < min_in) min_in = rin;
            rin = bp_load_max - bp2;
            if (rin < min_in) min_in = rin;
            rin = bp_load_max - bp3;
            if (rin < min_in) min_in = rin;

            size_t nb = 1 + (min_out - ZXC_HUF_SAFE_MARGIN) / (2 * ZXC_HUF_BATCH);
            const size_t nb_in = (size_t)(min_in / ZXC_HUF_BATCH_BITS) + 1;
            if (nb_in < nb) nb = nb_in;

/* Decode one lookup per stream, phased (all loads, then advances, then
 * stores) so the 4 independent LUT loads issue together and overlap. Each
 * lookup speculatively writes 2 bytes and advances 1-2; ZXC_HUF_SAFE_MARGIN
 * headroom keeps the spec write inside the stream's slice. */
#define DECODE_ONE()                                           \
    do {                                                       \
        const uint32_t _e0 = table[ZXC_HUF_LUT_IDX(a0)].entry; \
        const uint32_t _e1 = table[ZXC_HUF_LUT_IDX(a1)].entry; \
        const uint32_t _e2 = table[ZXC_HUF_LUT_IDX(a2)].entry; \
        const uint32_t _e3 = table[ZXC_HUF_LUT_IDX(a3)].entry; \
        const int _t0 = (int)(_e0 >> 28);                      \
        const int _t1 = (int)(_e1 >> 28);                      \
        const int _t2 = (int)(_e2 >> 28);                      \
        const int _t3 = (int)(_e3 >> 28);                      \
        a0 >>= _t0;                                            \
        a1 >>= _t1;                                            \
        a2 >>= _t2;                                            \
        a3 >>= _t3;                                            \
        bp0 += (uint64_t)_t0;                                  \
        bp1 += (uint64_t)_t1;                                  \
        bp2 += (uint64_t)_t2;                                  \
        bp3 += (uint64_t)_t3;                                  \
        zxc_store_le16(d0, (uint16_t)_e0);                     \
        zxc_store_le16(d1, (uint16_t)_e1);                     \
        zxc_store_le16(d2, (uint16_t)_e2);                     \
        zxc_store_le16(d3, (uint16_t)_e3);                     \
        d0 += 1 + (int)((_e0 >> 24) & 1);                      \
        d1 += 1 + (int)((_e1 >> 24) & 1);                      \
        d2 += 1 + (int)((_e2 >> 24) & 1);                      \
        d3 += 1 + (int)((_e3 >> 24) & 1);                      \
    } while (0)

            while (nb--) {
                a0 = zxc_le64((const uint8_t*)(uintptr_t)(bp0 >> 3)) >> ((unsigned)bp0 & 7u);
                a1 = zxc_le64((const uint8_t*)(uintptr_t)(bp1 >> 3)) >> ((unsigned)bp1 & 7u);
                a2 = zxc_le64((const uint8_t*)(uintptr_t)(bp2 >> 3)) >> ((unsigned)bp2 & 7u);
                a3 = zxc_le64((const uint8_t*)(uintptr_t)(bp3 >> 3)) >> ((unsigned)bp3 & 7u);

                DECODE_ONE();
                DECODE_ONE();
                DECODE_ONE();
                DECODE_ONE();
                DECODE_ONE();
            }
        }
#undef DECODE_ONE
    }

/* Scalar per-stream tail (cold): single-symbol decode with byte-exact refills
 * bounded by the stream's logical end; no speculative write. */
#define TAIL_STREAM(bp, d, dend, logical_end)                               \
    do {                                                                    \
        const uint8_t* _p = (const uint8_t*)(uintptr_t)((bp) >> 3);         \
        const uint8_t* const _pend = (logical_end);                         \
        int _drop = (int)((bp) & 7u);                                       \
        uint64_t _acc = 0;                                                  \
        int _nbits = 0;                                                     \
        while ((d) < (dend)) {                                              \
            while (_nbits <= ZXC_HUF_ACCUM_BITS - CHAR_BIT && _p < _pend) { \
                _acc |= ((uint64_t)*_p++) << _nbits;                        \
                _nbits += CHAR_BIT;                                         \
            }                                                               \
            if (_drop) { /* initial mid-byte bit offset, applied once */    \
                _acc >>= _drop;                                             \
                _nbits -= _drop;                                            \
                _drop = 0;                                                  \
            }                                                               \
            const uint32_t _e = table[ZXC_HUF_LUT_IDX(_acc)].entry;         \
            *(d)++ = (uint8_t)_e;                                           \
            const int _l1 = (int)((_e >> 16) & 0xF);                        \
            _acc >>= _l1;                                                   \
            _nbits -= _l1;                                                  \
        }                                                                   \
    } while (0)

    TAIL_STREAM(bp0, d0, dend0, base + s1);
    TAIL_STREAM(bp1, d1, dend1, base + s1 + s2);
    TAIL_STREAM(bp2, d2, dend2, base + s123);
    TAIL_STREAM(bp3, d3, dend3, base + s123 + s4);
#undef TAIL_STREAM
    return ZXC_OK;
}

/**
 * @brief Decode a full Huffman literal section payload.
 *
 * Unpacks the 128-byte lengths header, builds the multi-symbol decode table,
 * then runs the 4-way interleaved decode, writing exactly @p n_literals bytes.
 *
 * @param[in]  payload       Section payload (lengths header + sizes + 4 sub-streams).
 * @param[in]  payload_size  Total payload length in bytes.
 * @param[out] dst           Destination buffer (must not alias @p payload).
 * @param[in]  n_literals    Expected number of decoded bytes.
 * @return `ZXC_OK` on success, negative `zxc_error_t` on failure.
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

    /* 2. Build the 2048-entry multi-symbol decode table. Cache-line
     * aligned: the LUT spans 128 lines (8 KB / 64 B) and is hammered every
     * symbol, landing it on a 64-byte boundary avoids any cross-line
     * load split on the per-iteration entry fetch. */
    ZXC_ALIGN(ZXC_CACHE_LINE_SIZE) zxc_huf_dec_entry_t table[ZXC_HUF_DEC_TABLE_SIZE];
    {
        const int rc = build_decode_table(code_len, table);
        if (UNLIKELY(rc != ZXC_OK)) return rc;
    }

    /* 3. Decode the 4 interleaved sub-streams. */
    return zxc_huf_decode_streams(payload + ZXC_HUF_TABLE_SIZE, payload_size - ZXC_HUF_TABLE_SIZE,
                                  dst, n_literals, table);
}

/**
 * @brief Decode a literal section that carries no lengths header, using a
 *        prebuilt decode table (shared dictionary table).
 *
 * @param[in]  payload       Section payload (6-byte sizes header + 4 sub-streams).
 * @param[in]  payload_size  Total payload length in bytes.
 * @param[out] dst           Destination buffer (must not alias @p payload).
 * @param[in]  n_literals    Expected number of decoded bytes.
 * @param[in]  table         Prebuilt @ref ZXC_HUF_DEC_TABLE_SIZE-entry decode table.
 * @return `ZXC_OK` on success, `ZXC_ERROR_CORRUPT_DATA` if @p table is NULL or
 *         the stream is malformed.
 */
int zxc_huf_decode_section_dict(const uint8_t* RESTRICT payload, const size_t payload_size,
                                uint8_t* RESTRICT dst, const size_t n_literals,
                                const zxc_huf_dec_entry_t* RESTRICT table) {
    if (UNLIKELY(table == NULL)) return ZXC_ERROR_CORRUPT_DATA;
    return zxc_huf_decode_streams(payload, payload_size, dst, n_literals, table);
}

/**
 * @brief Build the @ref ZXC_HUF_DEC_TABLE_SIZE-entry decode table from
 *        per-symbol code lengths. Validates Kraft equality.
 *
 * Public wrapper around @ref build_decode_table.
 *
 * @param[in]  code_len  Per-symbol code lengths.
 * @param[out] table     Destination decode table (caller-aligned).
 * @return `ZXC_OK` on success, `ZXC_ERROR_CORRUPT_DATA` on invalid lengths.
 */
int zxc_huf_build_dec_table(const uint8_t* RESTRICT code_len, zxc_huf_dec_entry_t* RESTRICT table) {
    return build_decode_table(code_len, table);
}

/**
 * @brief Pack per-symbol code lengths into the 128-byte (4-bit nibble) header.
 *
 * @param[in]  code_len  Per-symbol code lengths (one byte each).
 * @param[out] out       Destination 128-byte packed header.
 */
void zxc_huf_pack_lengths(const uint8_t* RESTRICT code_len, uint8_t* RESTRICT out) {
    pack_lengths_header(code_len, out);
}

/**
 * @brief Unpack and structurally validate a 128-byte packed lengths header.
 *
 * @param[in]  in        128-byte packed lengths header.
 * @param[out] code_len  Destination per-symbol code lengths.
 * @return `ZXC_OK` on success, `ZXC_ERROR_CORRUPT_DATA` on invalid lengths.
 */
int zxc_huf_unpack_lengths(const uint8_t* RESTRICT in, uint8_t* RESTRICT code_len) {
    return unpack_lengths_header(in, code_len);
}

/* ===========================================================================
 * PivCo-Huffman section codec (v7+ sections, enc 4/5)
 * ===========================================================================
 *
 * Layout: [128-byte packed code lengths (literal sections only)] then, for
 * every INTERNAL node of the canonical code tree in BFS order, that node's
 * branch bits: one bit per symbol routed through the node, in sequence order,
 * LSB-first within bytes, each node padded to a byte boundary. A 0 bit routes
 * the symbol to the left child, 1 to the right.
 *
 * The decoder recovers each node's symbol count for free: the root handles
 * n symbols, and popcounting a node's bits yields its right child's count.
 * Reconstruction runs bottom-up, one level at a time: leaves are runs of a
 * single symbol; an internal node MERGES its two children's sequences under
 * the control of its bits. Merges are branch-free shuffles (16 outputs per
 * step on NEON via a two-register TBL), which is what makes this layout
 * decode faster than the serial bit-chain of the classic 4-stream layout on
 * any target with a 16-byte shuffle.
 *
 * Level buffers ping-pong between `scratch` (odd depths) and `dst` (even
 * depths, final output at depth 0), so one n-sized scratch suffices. Both
 * buffers need read slack past `n` (speculative 16-byte kernel loads):
 * ZXC_PAD_SIZE on dst (already true for lit/token buffers), and
 * ZXC_PIVCO_SCRATCH_PAD on scratch.
 */

#include "zxc_pivco_tables.h"

#define ZXC_PIVCO_MAX_NODES (2 * ZXC_HUF_NUM_SYMBOLS - 1)

typedef struct {
    int16_t child[2]; /* node index, -1 = absent */
    int16_t sym;      /* >= 0: leaf symbol; -1: internal */
} zxc_pivco_node_t;

typedef struct {
    zxc_pivco_node_t nd[ZXC_PIVCO_MAX_NODES];
    int16_t bfs[ZXC_PIVCO_MAX_NODES]; /* node ids in BFS (== wire) order */
    int16_t lvl_start[ZXC_HUF_MAX_CODE_LEN_ULTRA + 2];
    int n_nodes;
    int max_depth;
    /* Flat-subtree fast path: flat_d[nid] = D (>= 2) when nid roots a MAXIMAL
     * complete subtree whose leaves all sit exactly D levels below it; such a
     * node's wire run is its symbols' packed D-bit residual codes instead of
     * D levels of partition bitmaps (same bit count, decode = unpack+lookup).
     * covered[nid] = 1 for every strict descendant of a flat root: those nodes
     * do not exist on the wire nor in the level buffers. Both sides derive
     * this from the code lengths alone, so nothing is signalled. */
    uint8_t flat_d[ZXC_PIVCO_MAX_NODES];
    uint8_t covered[ZXC_PIVCO_MAX_NODES];
} zxc_pivco_tree_t;

static ZXC_ALWAYS_INLINE int zxc_pivco_popcnt32(uint32_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
    return (int)__popcnt(v);
#else
    return __builtin_popcount(v);
#endif
}

static ZXC_ALWAYS_INLINE int zxc_pivco_popcnt64(uint64_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
    return (int)__popcnt64(v);
#else
    return __builtin_popcountll(v);
#endif
}

/**
 * @brief Build the canonical code tree (and codes) from per-symbol lengths.
 *
 * Canonical MSB-first assignment: symbols sorted by (length, symbol) receive
 * sequential codes. Insertion collisions or code-space overflow (malformed
 * lengths on the decode path) fail with a nonzero return.
 *
 * @param[in]  code_len Per-symbol code lengths (0 = absent).
 * @param[out] t        Tree + BFS ordering, fully initialised on success.
 * @param[out] codes    Optional per-symbol canonical codes (encoder only).
 * @return 0 on success, -1 on malformed lengths.
 */
static int zxc_pivco_tree_build(const uint8_t* RESTRICT code_len, zxc_pivco_tree_t* RESTRICT t,
                                uint32_t* RESTRICT codes) {
    uint32_t bl_count[ZXC_HUF_MAX_CODE_LEN_ULTRA + 1] = {0};
    int n_present = 0;
    for (int s = 0; s < ZXC_HUF_NUM_SYMBOLS; s++) {
        const int l = code_len[s];
        if (l == 0) continue;
        if (UNLIKELY(l > ZXC_HUF_MAX_CODE_LEN_ULTRA)) return -1;
        bl_count[l]++;
        n_present++;
    }
    if (UNLIKELY(n_present == 0)) return -1;

    uint32_t next_code[ZXC_HUF_MAX_CODE_LEN_ULTRA + 2] = {0};
    uint32_t code = 0;
    for (int l = 1; l <= ZXC_HUF_MAX_CODE_LEN_ULTRA; l++) {
        code = (code + bl_count[l - 1]) << 1;
        next_code[l] = code;
    }

    t->n_nodes = 1; /* root */
    t->nd[0].child[0] = t->nd[0].child[1] = -1;
    t->nd[0].sym = -1;
    int max_depth = 0;

    for (int s = 0; s < ZXC_HUF_NUM_SYMBOLS; s++) {
        const int l = code_len[s];
        if (l == 0) continue;
        const uint32_t c = next_code[l]++;
        if (UNLIKELY(c >> l)) return -1; /* code space overflow */
        if (codes) codes[s] = c;
        int cur = 0;
        for (int d = l - 1; d >= 0; d--) {
            if (UNLIKELY(t->nd[cur].sym >= 0)) return -1; /* prefix collision */
            const int bit = (int)((c >> d) & 1u);
            int nxt = t->nd[cur].child[bit];
            if (nxt < 0) {
                if (UNLIKELY(t->n_nodes >= ZXC_PIVCO_MAX_NODES)) return -1;
                nxt = t->n_nodes++;
                t->nd[nxt].child[0] = t->nd[nxt].child[1] = -1;
                t->nd[nxt].sym = -1;
                t->nd[cur].child[bit] = (int16_t)nxt;
            }
            cur = nxt;
        }
        if (UNLIKELY(t->nd[cur].child[0] >= 0 || t->nd[cur].child[1] >= 0)) return -1;
        t->nd[cur].sym = (int16_t)s;
        if (l > max_depth) max_depth = l;
    }
    t->max_depth = max_depth;

    /* BFS order (parents before children, left before right): this is both
     * the wire order of the node bit runs and the property that makes each
     * parent's children CONTIGUOUS in the next level's sequence buffer. */
    int head = 0, tail = 0;
    t->bfs[tail++] = 0;
    int depth_end = 1; /* index in bfs where the current depth ends */
    int depth = 0;
    t->lvl_start[0] = 0;
    while (head < tail) {
        if (head == depth_end) {
            depth++;
            t->lvl_start[depth] = (int16_t)head;
            depth_end = tail;
        }
        const int nid = t->bfs[head++];
        for (int b = 0; b < 2; b++) {
            const int ch = t->nd[nid].child[b];
            if (ch >= 0) t->bfs[tail++] = (int16_t)ch;
        }
    }
    for (int d = depth + 1; d <= max_depth + 1; d++) t->lvl_start[d] = (int16_t)tail;

    /* Flat-subtree detection: min/max leaf depth per node in one reverse-BFS
     * sweep, then maximality by masking descendants of the first flat node on
     * each root-to-leaf path. */
    {
        int8_t mn[ZXC_PIVCO_MAX_NODES];
        int8_t mx[ZXC_PIVCO_MAX_NODES];
        for (int i = t->n_nodes - 1; i >= 0; i--) {
            const int nid = t->bfs[i];
            const zxc_pivco_node_t* nd = &t->nd[nid];
            if (nd->sym >= 0) {
                mn[nid] = mx[nid] = 0;
            } else if (nd->child[0] >= 0 && nd->child[1] >= 0) {
                const int a = mn[nd->child[0]], b = mn[nd->child[1]];
                const int c = mx[nd->child[0]], d2 = mx[nd->child[1]];
                mn[nid] = (int8_t)(1 + (a < b ? a : b));
                mx[nid] = (int8_t)(1 + (c > d2 ? c : d2));
            } else { /* single-child (degenerate) : never flat */
                mn[nid] = 0;
                mx[nid] = (int8_t)ZXC_HUF_MAX_CODE_LEN_ULTRA;
            }
        }
        for (int i = 0; i < t->n_nodes; i++) {
            const int nid = t->bfs[i];
            t->flat_d[nid] = 0;
            if (i == 0) t->covered[nid] = 0; /* root */
            /* Flat only where it beats the merge cascade (FORMAT RULE, both
             * sides): D = 2/4 have single-TBL SIMD unpackers; D >= 7 replaces
             * enough merge levels that even the scalar unpacker wins. D = 3/5/6
             * stay on the (SIMD) merge path. */
            if (!t->covered[nid] && t->nd[nid].sym < 0 && mn[nid] == mx[nid] &&
                (mn[nid] == 2 || mn[nid] == 4 || mn[nid] >= 7))
                t->flat_d[nid] = (uint8_t)mn[nid];
            const int ch0 = t->nd[nid].child[0];
            const int ch1 = t->nd[nid].child[1];
            const uint8_t cov = (uint8_t)(t->covered[nid] || t->flat_d[nid]);
            if (ch0 >= 0) t->covered[ch0] = cov;
            if (ch1 >= 0) t->covered[ch1] = cov;
        }
    }
    return 0;
}

/**
 * @brief Per-node symbol counts from the histogram (encoder / size estimate).
 *
 * Leaf count = freq[sym]; internal count = sum of children, computed in one
 * reverse-BFS sweep (children precede their parent in that direction).
 */
static void zxc_pivco_counts(const zxc_pivco_tree_t* RESTRICT t, const uint32_t* RESTRICT freq,
                             uint32_t* RESTRICT count) {
    for (int i = t->n_nodes - 1; i >= 0; i--) {
        const int nid = t->bfs[i];
        const zxc_pivco_node_t* nd = &t->nd[nid];
        if (nd->sym >= 0) {
            count[nid] = freq[nd->sym];
        } else {
            const uint32_t c0 = nd->child[0] >= 0 ? count[nd->child[0]] : 0;
            const uint32_t c1 = nd->child[1] >= 0 ? count[nd->child[1]] : 0;
            count[nid] = c0 + c1;
        }
    }
}

size_t zxc_pivco_calc_size(const uint32_t* RESTRICT freq, const uint8_t* RESTRICT code_len,
                           const int with_header) {
    zxc_pivco_tree_t t;
    if (UNLIKELY(zxc_pivco_tree_build(code_len, &t, NULL) != 0)) return SIZE_MAX;
    uint32_t count[ZXC_PIVCO_MAX_NODES];
    zxc_pivco_counts(&t, freq, count);
    size_t total = with_header ? (size_t)ZXC_HUF_TABLE_SIZE : 0;
    for (int i = 0; i < t.n_nodes; i++) {
        const int nid = t.bfs[i];
        if (t.covered[nid] || t.nd[nid].sym >= 0) continue;
        total += t.flat_d[nid] ? ((size_t)count[nid] * t.flat_d[nid] + 7) / 8
                               : ((size_t)count[nid] + 7) / 8;
    }
    return total;
}

/**
 * @brief Shared PivCo section encoder body.
 *
 * Walks each input symbol root-to-leaf once, appending its branch bit to the
 * per-node write cursor; node bit runs land in BFS order, byte-aligned.
 */
static int zxc_pivco_encode_core(const uint8_t* RESTRICT literals, const size_t n_literals,
                                 const uint32_t* RESTRICT freq, const uint8_t* RESTRICT code_len,
                                 uint8_t* RESTRICT dst, const size_t dst_cap,
                                 const int with_header) {
    if (UNLIKELY(n_literals == 0)) return ZXC_ERROR_CORRUPT_DATA;
    zxc_pivco_tree_t t;
    uint32_t codes[ZXC_HUF_NUM_SYMBOLS];
    if (UNLIKELY(zxc_pivco_tree_build(code_len, &t, codes) != 0)) return ZXC_ERROR_CORRUPT_DATA;

    uint32_t count[ZXC_PIVCO_MAX_NODES];
    zxc_pivco_counts(&t, freq, count);

    /* Byte offsets of every wire-visible internal node's run, in BFS order:
     * bitmap runs (1 bit/symbol) or, for flat roots, packed code runs
     * (flat_d bits/symbol). Covered nodes have no run. */
    uint32_t bit_off[ZXC_PIVCO_MAX_NODES];
    size_t payload = 0;
    for (int i = 0; i < t.n_nodes; i++) {
        const int nid = t.bfs[i];
        if (t.covered[nid] || t.nd[nid].sym >= 0) continue;
        bit_off[nid] = (uint32_t)payload;
        payload += t.flat_d[nid] ? ((size_t)count[nid] * t.flat_d[nid] + 7) / 8
                                 : ((size_t)count[nid] + 7) / 8;
    }
    const size_t hdr = with_header ? (size_t)ZXC_HUF_TABLE_SIZE : 0;
    /* +2: the packed-code emitter uses a 3-byte read-modify-write that may
     * touch up to 2 bytes past the payload end. */
    if (UNLIKELY(hdr + payload + 2 > dst_cap)) return ZXC_ERROR_DST_TOO_SMALL;

    if (with_header) zxc_huf_pack_lengths(code_len, dst);
    uint8_t* const out = dst + hdr;
    ZXC_MEMSET(out, 0, payload + 2);

    /* Per-node bit cursors; emit MSB-first code bits while descending. At a
     * flat root, emit the symbol's packed D-bit residual (bit j = branch at
     * subtree level j) in one shot and stop descending. */
    uint32_t wpos[ZXC_PIVCO_MAX_NODES];
    ZXC_MEMSET(wpos, 0, (size_t)t.n_nodes * sizeof(uint32_t));
    for (size_t i = 0; i < n_literals; i++) {
        const uint8_t s = literals[i];
        const uint32_t c = codes[s];
        int cur = 0;
        for (int d = code_len[s] - 1; d >= 0; d--) {
            const int fd = t.flat_d[cur];
            if (fd) {
                /* residual = low fd bits of the canonical code, bit-reversed
                 * so that packed bit j is the branch taken at level j. */
                uint32_t r = 0;
                for (int j = 0; j < fd; j++) r |= ((c >> (fd - 1 - j)) & 1u) << j;
                const uint32_t p = wpos[cur];
                wpos[cur] += (uint32_t)fd;
                uint8_t* q = out + bit_off[cur] + (p >> 3);
                const uint32_t sh = p & 7u;
                uint32_t w = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16);
                w |= r << sh; /* fd <= 11, sh <= 7 -> fits 24 bits */
                q[0] = (uint8_t)w;
                q[1] = (uint8_t)(w >> 8);
                q[2] = (uint8_t)(w >> 16);
                break;
            }
            const uint32_t bit = (c >> d) & 1u;
            const uint32_t p = wpos[cur]++;
            out[bit_off[cur] + (p >> 3)] |= (uint8_t)(bit << (p & 7));
            cur = t.nd[cur].child[bit];
        }
    }
    return (int)(hdr + payload);
}

int zxc_pivco_encode_section(const uint8_t* RESTRICT literals, const size_t n_literals,
                             const uint32_t* RESTRICT freq, const uint8_t* RESTRICT code_len,
                             uint8_t* RESTRICT dst, const size_t dst_cap) {
    return zxc_pivco_encode_core(literals, n_literals, freq, code_len, dst, dst_cap, 1);
}

int zxc_pivco_encode_section_dict(const uint8_t* RESTRICT literals, const size_t n_literals,
                                  const uint32_t* RESTRICT freq, const uint8_t* RESTRICT code_len,
                                  uint8_t* RESTRICT dst, const size_t dst_cap) {
    return zxc_pivco_encode_core(literals, n_literals, freq, code_len, dst, dst_cap, 0);
}

/**
 * @brief Merge two adjacent child sequences under control bits.
 *
 * src[0..nl) is the left sequence, src[nl..nl+nr) the right one (children are
 * contiguous by construction). Control bits are LSB-first; bit 0 takes the
 * next left element, bit 1 the next right one. Speculative 16-byte loads read
 * up to 15 bytes past each cursor: callers guarantee read slack after the
 * level buffers. Writes stay within out[0..nl+nr).
 */
static ZXC_ALWAYS_INLINE void zxc_pivco_merge(uint8_t* RESTRICT out, const uint8_t* RESTRICT src,
                                              const size_t nl, const size_t nr,
                                              const uint8_t* RESTRICT bits) {
    const uint8_t* L = src;
    const uint8_t* R = src + nl;
    const size_t n = nl + nr;
    size_t i = 0, lp = 0, rp = 0;
#if defined(ZXC_USE_NEON64)
    /* 16 outputs per step: two-register TBL over {L[0..15], R[0..15]} with the
     * signed-index tables (see zxc_pivco_tables.h). */
    while (i + 16 <= n) {
        const uint8_t b0 = bits[i >> 3];
        const uint8_t b1 = bits[(i >> 3) + 1];
        uint8x16x2_t tb;
        tb.val[0] = vld1q_u8(L + lp);
        tb.val[1] = vld1q_u8(R + rp);
        const int pc0 = zxc_pivco_popcnt32(b0);
        const int8x8_t ia = vld1_s8(zxc_pivco_idxa[b0]);
        const int8x8_t ib = vadd_s8(vld1_s8(zxc_pivco_idxb[b1]), vdup_n_s8((int8_t)pc0));
        const uint8x16_t ix = vreinterpretq_u8_s8(vabsq_s8(vcombine_s8(ia, ib)));
        vst1q_u8(out + i, vqtbl2q_u8(tb, ix));
        const int pc = pc0 + zxc_pivco_popcnt32(b1);
        rp += (size_t)pc;
        lp += (size_t)(16 - pc);
        i += 16;
    }
#else /* x86 tiers */
#if defined(ZXC_USE_AVX512) && defined(__AVX512VBMI2__)
    /* 64 outputs per step: the merge IS a pair of byte expands. Bytes of L
     * fill the 0-bit lanes of the control word, bytes of R the 1-bit lanes
     * (merge-masked into the first result). Masked loads keep the speculative
     * reads fault-safe without extra buffer slack. */
    while (i + 64 <= n) {
        uint64_t ctrl;
        ZXC_MEMCPY(&ctrl, bits + (i >> 3), 8);
        const int pc = zxc_pivco_popcnt64(ctrl);
        const __mmask64 lmask = (pc == 0) ? ~(__mmask64)0 : (((__mmask64)1 << (64 - pc)) - 1u);
        const __mmask64 rmask = (pc == 64) ? ~(__mmask64)0 : (((__mmask64)1 << pc) - 1u);
        const __m512i vl = _mm512_maskz_loadu_epi8(lmask, (const void*)(L + lp));
        const __m512i vr = _mm512_maskz_loadu_epi8(rmask, (const void*)(R + rp));
        const __m512i outl = _mm512_maskz_expand_epi8((__mmask64)~ctrl, vl);
        const __m512i outv = _mm512_mask_expand_epi8(outl, (__mmask64)ctrl, vr);
        _mm512_storeu_si512((void*)(out + i), outv);
        rp += (size_t)pc;
        lp += (size_t)(64 - pc);
        i += 64;
    }
#endif
#if defined(ZXC_USE_AVX512) || defined(ZXC_USE_AVX2) || \
    (defined(ZXC_USE_SSE2) && defined(__SSSE3__))
    /* 16 outputs per step, SSSE3 two-register shuffle emulation: after the
     * signed-index + popcount-adjust + pabsb step (same tables as NEON), an
     * index ix selects L[ix] when ix < 16 and R[ix - 16] otherwise:
     *   pshufb(L, ix + 0x70): lanes with ix >= 16 overflow past 0x80 (zeroed),
     *                         lanes with ix < 16 keep bit 7 clear (selected);
     *   pshufb(R, ix - 16):   lanes with ix < 16 wrap to >= 0xF0 (zeroed).
     * OR-ing both shuffles yields the merged bytes. The 32-output loop is the
     * same step unrolled twice: the second step's cursors derive from the first's
     * popcounts alone, so both table-selects run independently (this ILP, not
     * wider vectors, is where 256-bit targets gain). */
    while (i + 32 <= n) {
        const uint8_t* cb = bits + (i >> 3);
        const int pcA0 = zxc_pivco_popcnt32(cb[0]);
        const int pcA = pcA0 + zxc_pivco_popcnt32(cb[1]);
        const int pcB0 = zxc_pivco_popcnt32(cb[2]);
        const int pcB = pcB0 + zxc_pivco_popcnt32(cb[3]);
        const size_t lpB = lp + (size_t)(16 - pcA);
        const size_t rpB = rp + (size_t)pcA;
        const __m128i vlA = _mm_loadu_si128((const __m128i*)(const void*)(L + lp));
        const __m128i vrA = _mm_loadu_si128((const __m128i*)(const void*)(R + rp));
        const __m128i vlB = _mm_loadu_si128((const __m128i*)(const void*)(L + lpB));
        const __m128i vrB = _mm_loadu_si128((const __m128i*)(const void*)(R + rpB));
        const __m128i iaA = _mm_loadl_epi64((const __m128i*)(const void*)zxc_pivco_idxa[cb[0]]);
        const __m128i ibA =
            _mm_add_epi8(_mm_loadl_epi64((const __m128i*)(const void*)zxc_pivco_idxb[cb[1]]),
                         _mm_set1_epi8((char)pcA0));
        const __m128i iaB = _mm_loadl_epi64((const __m128i*)(const void*)zxc_pivco_idxa[cb[2]]);
        const __m128i ibB =
            _mm_add_epi8(_mm_loadl_epi64((const __m128i*)(const void*)zxc_pivco_idxb[cb[3]]),
                         _mm_set1_epi8((char)pcB0));
        const __m128i ixA = _mm_abs_epi8(_mm_unpacklo_epi64(iaA, ibA));
        const __m128i ixB = _mm_abs_epi8(_mm_unpacklo_epi64(iaB, ibB));
        const __m128i selA =
            _mm_or_si128(_mm_shuffle_epi8(vlA, _mm_add_epi8(ixA, _mm_set1_epi8(0x70))),
                         _mm_shuffle_epi8(vrA, _mm_sub_epi8(ixA, _mm_set1_epi8(16))));
        const __m128i selB =
            _mm_or_si128(_mm_shuffle_epi8(vlB, _mm_add_epi8(ixB, _mm_set1_epi8(0x70))),
                         _mm_shuffle_epi8(vrB, _mm_sub_epi8(ixB, _mm_set1_epi8(16))));
        _mm_storeu_si128((__m128i*)(void*)(out + i), selA);
        _mm_storeu_si128((__m128i*)(void*)(out + i + 16), selB);
        lp = lpB + (size_t)(16 - pcB);
        rp = rpB + (size_t)pcB;
        i += 32;
    }
    while (i + 16 <= n) {
        const uint8_t b0 = bits[i >> 3];
        const uint8_t b1 = bits[(i >> 3) + 1];
        const __m128i vl = _mm_loadu_si128((const __m128i*)(const void*)(L + lp));
        const __m128i vr = _mm_loadu_si128((const __m128i*)(const void*)(R + rp));
        const int pc0 = zxc_pivco_popcnt32(b0);
        const __m128i ia = _mm_loadl_epi64((const __m128i*)(const void*)zxc_pivco_idxa[b0]);
        const __m128i ib0 = _mm_loadl_epi64((const __m128i*)(const void*)zxc_pivco_idxb[b1]);
        const __m128i ib = _mm_add_epi8(ib0, _mm_set1_epi8((char)pc0));
        const __m128i ix = _mm_abs_epi8(_mm_unpacklo_epi64(ia, ib));
        const __m128i sell = _mm_shuffle_epi8(vl, _mm_add_epi8(ix, _mm_set1_epi8(0x70)));
        const __m128i selr = _mm_shuffle_epi8(vr, _mm_sub_epi8(ix, _mm_set1_epi8(16)));
        _mm_storeu_si128((__m128i*)(void*)(out + i), _mm_or_si128(sell, selr));
        const int pc = pc0 + zxc_pivco_popcnt32(b1);
        rp += (size_t)pc;
        lp += (size_t)(16 - pc);
        i += 16;
    }
#endif
#endif /* x86 tiers */
    /* Portable 8-output path (plain byte selects from a 16-byte scratch). */
    while (i + 8 <= n) {
        const uint8_t b = bits[i >> 3];
        uint8_t comb[16];
        ZXC_MEMCPY(comb, L + lp, 8);
        ZXC_MEMCPY(comb + 8, R + rp, 8);
        const uint8_t* ix = zxc_pivco_idx8[b];
        for (int j = 0; j < 8; j++) out[i + (size_t)j] = comb[ix[j]];
        const int pc = zxc_pivco_popcnt32(b);
        rp += (size_t)pc;
        lp += (size_t)(8 - pc);
        i += 8;
    }
    while (i < n) {
        const int bit = (bits[i >> 3] >> (i & 7)) & 1;
        out[i++] = bit ? R[rp++] : L[lp++];
    }
}

/**
 * @brief Decode a flat subtree's packed D-bit code run into symbols.
 *
 * codes are packed LSB-first, D in [2, ZXC_HUF_MAX_CODE_LEN_ULTRA]; c2s maps
 * a packed path to its leaf symbol. SIMD paths cover the frequent D = 2 / 4
 * (in-register unpack + single 16-byte table lookup); the scalar bit-reader
 * handles every D.
 */
static void zxc_pivco_unpack_flat(uint8_t* RESTRICT out, const size_t n, const int D,
                                  const uint8_t* RESTRICT bits, const uint8_t* RESTRICT c2s) {
    size_t i = 0;
#if defined(ZXC_USE_NEON64)
    if (D == 4) {
        const uint8x16_t vc2s = vld1q_u8(c2s); /* 16 entries */
        static const uint8_t rep2[16] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7};
        static const int8_t sh4[16] = {0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4, 0, -4};
        const uint8x16_t vrep = vld1q_u8(rep2);
        const int8x16_t vsh = vld1q_s8(sh4);
        const uint8x16_t vmask = vdupq_n_u8(0x0F);
        while (i + 16 <= n) {
            uint8x16_t raw = vdupq_n_u8(0);
            raw = vreinterpretq_u8_u64(
                vsetq_lane_u64(*(const uint64_t*)(const void*)(bits + ((i * 4) >> 3)),
                               vreinterpretq_u64_u8(raw), 0));
            const uint8x16_t rep = vqtbl1q_u8(raw, vrep);
            const uint8x16_t codes = vandq_u8(vshlq_u8(rep, vsh), vmask);
            vst1q_u8(out + i, vqtbl1q_u8(vc2s, codes));
            i += 16;
        }
    } else if (D == 2) {
        uint8_t c2s16[16];
        for (int k = 0; k < 16; k++) c2s16[k] = c2s[k & 3];
        const uint8x16_t vc2s = vld1q_u8(c2s16);
        static const uint8_t rep4[16] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3};
        static const int8_t sh2[16] = {0, -2, -4, -6, 0, -2, -4, -6, 0, -2, -4, -6, 0, -2, -4, -6};
        const uint8x16_t vrep = vld1q_u8(rep4);
        const int8x16_t vsh = vld1q_s8(sh2);
        const uint8x16_t vmask = vdupq_n_u8(0x03);
        while (i + 16 <= n) {
            uint8x16_t raw = vdupq_n_u8(0);
            raw = vreinterpretq_u8_u32(
                vsetq_lane_u32(*(const uint32_t*)(const void*)(bits + ((i * 2) >> 3)),
                               vreinterpretq_u32_u8(raw), 0));
            const uint8x16_t rep = vqtbl1q_u8(raw, vrep);
            const uint8x16_t codes = vandq_u8(vshlq_u8(rep, vsh), vmask);
            vst1q_u8(out + i, vqtbl1q_u8(vc2s, codes));
            i += 16;
        }
    }
#elif defined(ZXC_USE_AVX512) || defined(ZXC_USE_AVX2) || \
    (defined(ZXC_USE_SSE2) && defined(__SSSE3__))
    if (D == 4) {
        const __m128i vc2s = _mm_loadu_si128((const __m128i*)(const void*)c2s);
        const __m128i vrep = _mm_setr_epi8(0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7);
        const __m128i vmask = _mm_set1_epi8(0x0F);
        /* even lanes keep low nibble, odd lanes take the high nibble: shift the
         * replicated bytes right by 4 via a 16-bit shift + odd-lane select. */
        const __m128i vodd = _mm_set1_epi16(0x0F00 - 0x0F00 + (short)0xFF00);
        while (i + 16 <= n) {
            const __m128i raw =
                _mm_loadl_epi64((const __m128i*)(const void*)(bits + ((i * 4) >> 3)));
            const __m128i rep = _mm_shuffle_epi8(raw, vrep);
            const __m128i hi = _mm_and_si128(_mm_srli_epi16(rep, 4), _mm_set1_epi8(0x0F));
            const __m128i lo = _mm_and_si128(rep, vmask);
            const __m128i codes = _mm_blendv_epi8(lo, hi, vodd);
            _mm_storeu_si128((__m128i*)(void*)(out + i), _mm_shuffle_epi8(vc2s, codes));
            i += 16;
        }
    }
#endif
    /* Generic scalar bit-reader (any D). */
    {
        uint64_t bitpos = i * (uint64_t)D;
        const uint32_t m = (1u << D) - 1u;
        while (i < n) {
            const size_t byte = (size_t)(bitpos >> 3);
            uint32_t w = bits[byte];
            w |= (uint32_t)bits[byte + 1] << 8;
            if (D > 8) w |= (uint32_t)bits[byte + 2] << 16;
            out[i++] = c2s[(w >> (bitpos & 7u)) & m];
            bitpos += (uint64_t)D;
        }
    }
}

/**
 * @brief Direct emission for a node whose two children are BOTH leaves.
 *
 * out[k] = bit_k ? sym1 : sym0, i.e. sym0 ^ (delta & mask(bit_k)). Skips
 * materialising the two child runs entirely (idea from the PivCo reference
 * implementation: sequential stores + bit-test/XOR-blend beat a table merge
 * of two constant runs by 2-3x).
 */
static ZXC_ALWAYS_INLINE void zxc_pivco_emit_leaf_pair(uint8_t* RESTRICT out, const size_t n,
                                                       const uint8_t sym0, const uint8_t sym1,
                                                       const uint8_t* RESTRICT bits) {
    const uint8_t delta = (uint8_t)(sym0 ^ sym1);
    size_t i = 0;
#if defined(ZXC_USE_NEON64)
    const uint8x16_t vsym0 = vdupq_n_u8(sym0);
    const uint8x16_t vdelta = vdupq_n_u8(delta);
    static const uint8_t rep_idx[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
    static const uint8_t bit_sel[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t vrep = vld1q_u8(rep_idx);
    const uint8x16_t vsel = vld1q_u8(bit_sel);
    while (i + 16 <= n) {
        uint8x16_t ctrl = vdupq_n_u8(0);
        ctrl = vsetq_lane_u8(bits[i >> 3], ctrl, 0);
        ctrl = vsetq_lane_u8(bits[(i >> 3) + 1], ctrl, 1);
        const uint8x16_t rep = vqtbl1q_u8(ctrl, vrep);
        const uint8x16_t mask = vtstq_u8(rep, vsel);
        vst1q_u8(out + i, veorq_u8(vsym0, vandq_u8(vdelta, mask)));
        i += 16;
    }
#elif defined(ZXC_USE_AVX512) || defined(ZXC_USE_AVX2) || \
    (defined(ZXC_USE_SSE2) && defined(__SSSE3__))
    const __m128i vsym0 = _mm_set1_epi8((char)sym0);
    const __m128i vdelta = _mm_set1_epi8((char)delta);
    const __m128i vrep = _mm_setr_epi8(0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1);
    const __m128i vsel =
        _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, (char)128, 1, 2, 4, 8, 16, 32, 64, (char)128);
    while (i + 16 <= n) {
        const __m128i ctrl = _mm_cvtsi32_si128(bits[i >> 3] | ((int)bits[(i >> 3) + 1] << 8));
        const __m128i rep = _mm_shuffle_epi8(ctrl, vrep);
        /* lanes where the selected bit is set -> 0xFF */
        const __m128i mask = _mm_cmpeq_epi8(_mm_and_si128(rep, vsel), vsel);
        _mm_storeu_si128((__m128i*)(void*)(out + i),
                         _mm_xor_si128(vsym0, _mm_and_si128(vdelta, mask)));
        i += 16;
    }
#endif
    while (i < n) {
        const int bit = (bits[i >> 3] >> (i & 7)) & 1;
        out[i++] = bit ? sym1 : sym0;
    }
}

/**
 * @brief Shared PivCo section decoder body (code lengths already unpacked).
 *
 * Pass 1 (sizing) walks the wire's BFS order once: each internal node's bit
 * run is located, bounds-checked and popcounted, which yields both children's
 * symbol counts. Pass 2 rebuilds sequences bottom-up, one level at a time,
 * ping-ponging between dst (even depths, depth 0 = final output) and scratch
 * (odd depths); a level's writes never alias its reads.
 */
static int zxc_pivco_decode_core(const uint8_t* RESTRICT payload, const size_t payload_size,
                                 uint8_t* RESTRICT dst, const size_t n,
                                 const uint8_t* RESTRICT code_len, uint8_t* RESTRICT scratch) {
    if (UNLIKELY(n == 0)) return ZXC_ERROR_CORRUPT_DATA;
    zxc_pivco_tree_t t;
    if (UNLIKELY(zxc_pivco_tree_build(code_len, &t, NULL) != 0)) return ZXC_ERROR_CORRUPT_DATA;

    /* Pass 1: node counts + bit-run pointers, straight from the wire order. */
    uint32_t count[ZXC_PIVCO_MAX_NODES];
    const uint8_t* bit_ptr[ZXC_PIVCO_MAX_NODES];
    count[0] = (uint32_t)n;
    const uint8_t* p = payload;
    const uint8_t* const pend = payload + payload_size;
    for (int i = 0; i < t.n_nodes; i++) {
        const int nid = t.bfs[i];
        const zxc_pivco_node_t* nd = &t.nd[nid];
        if (t.covered[nid] || nd->sym >= 0) continue;
        const uint32_t c = count[nid];
        if (t.flat_d[nid]) {
            /* Packed-code run: no partition below, nothing to popcount. */
            const size_t fbytes = ((size_t)c * t.flat_d[nid] + 7) / 8;
            if (UNLIKELY((size_t)(pend - p) < fbytes)) return ZXC_ERROR_CORRUPT_DATA;
            bit_ptr[nid] = p;
            p += fbytes;
            continue;
        }
        const size_t nbytes = ((size_t)c + 7) / 8;
        if (UNLIKELY((size_t)(pend - p) < nbytes)) return ZXC_ERROR_CORRUPT_DATA;
        bit_ptr[nid] = p;
        /* popcount of the c valid bits = right child's count */
        uint32_t ones = 0;
        size_t k = 0;
        for (; k + 8 <= nbytes; k += 8) {
            uint64_t w;
            ZXC_MEMCPY(&w, p + k, 8);
            ones += (uint32_t)zxc_pivco_popcnt64(w);
        }
        for (; k < nbytes; k++) {
            uint8_t last = p[k];
            if (k == nbytes - 1 && (c & 7u)) last &= (uint8_t)((1u << (c & 7u)) - 1u);
            ones += (uint32_t)zxc_pivco_popcnt32(last);
        }
        p += nbytes;
        if (UNLIKELY(ones > c)) return ZXC_ERROR_CORRUPT_DATA;
        const int ch0 = nd->child[0];
        const int ch1 = nd->child[1];
        if (ch1 >= 0)
            count[ch1] = ones;
        else if (UNLIKELY(ones != 0))
            return ZXC_ERROR_CORRUPT_DATA;
        if (ch0 >= 0)
            count[ch0] = c - ones;
        else if (UNLIKELY(c - ones != 0))
            return ZXC_ERROR_CORRUPT_DATA;
    }

    /* Per-level sequence offsets (BFS order => children contiguous). */
    uint32_t seq_off[ZXC_PIVCO_MAX_NODES];
    for (int d = 0; d <= t.max_depth; d++) {
        uint32_t off = 0;
        for (int i = t.lvl_start[d]; i < t.lvl_start[d + 1]; i++) {
            const int nid = t.bfs[i];
            if (t.covered[nid]) continue; /* not materialised anywhere */
            seq_off[nid] = off;
            off += count[nid];
        }
    }

    /* Leaf-pair parents emit both runs directly from their bits (XOR-blend),
     * so their children never need materialising: flag them for skipping. */
    uint8_t skip[ZXC_PIVCO_MAX_NODES];
    ZXC_MEMSET(skip, 0, (size_t)t.n_nodes);
    for (int i = 0; i < t.n_nodes; i++) {
        const zxc_pivco_node_t* nd = &t.nd[t.bfs[i]];
        if (nd->sym >= 0) continue;
        const int ch0 = nd->child[0];
        const int ch1 = nd->child[1];
        if (ch0 >= 0 && ch1 >= 0 && t.nd[ch0].sym >= 0 && t.nd[ch1].sym >= 0) {
            skip[ch0] = 1;
            skip[ch1] = 1;
        }
    }

    /* Pass 2: bottom-up level reconstruction. */
    for (int d = t.max_depth; d >= 0; d--) {
        uint8_t* const buf_d = (d & 1) ? scratch : dst;
        uint8_t* const buf_c = (d & 1) ? dst : scratch; /* children live at d+1 */
        for (int i = t.lvl_start[d]; i < t.lvl_start[d + 1]; i++) {
            const int nid = t.bfs[i];
            if (t.covered[nid]) continue; /* lives inside a flat root's run */
            const zxc_pivco_node_t* nd = &t.nd[nid];
            const uint32_t c = count[nid];
            if (c == 0 || skip[nid]) continue;
            if (nd->sym >= 0) {
                ZXC_MEMSET(buf_d + seq_off[nid], (uint8_t)nd->sym, c);
            } else if (t.flat_d[nid]) {
                /* Build the packed-path -> symbol table (complete subtree of
                 * depth D: 2^D leaves), then unpack the code run directly. */
                const int D = t.flat_d[nid];
                uint8_t c2s[1u << ZXC_HUF_MAX_CODE_LEN_ULTRA];
                int16_t stk_n[ZXC_HUF_MAX_CODE_LEN_ULTRA + 1];
                uint16_t stk_p[ZXC_HUF_MAX_CODE_LEN_ULTRA + 1];
                uint8_t stk_l[ZXC_HUF_MAX_CODE_LEN_ULTRA + 1];
                int sp = 0;
                stk_n[0] = (int16_t)nid;
                stk_p[0] = 0;
                stk_l[0] = 0;
                while (sp >= 0) {
                    const int cn = stk_n[sp];
                    const uint32_t cp = stk_p[sp];
                    const int cl = stk_l[sp];
                    sp--;
                    if (t.nd[cn].sym >= 0) {
                        c2s[cp] = (uint8_t)t.nd[cn].sym;
                        continue;
                    }
                    sp++;
                    stk_n[sp] = t.nd[cn].child[0];
                    stk_p[sp] = (uint16_t)cp;
                    stk_l[sp] = (uint8_t)(cl + 1);
                    sp++;
                    stk_n[sp] = t.nd[cn].child[1];
                    stk_p[sp] = (uint16_t)(cp | (1u << cl));
                    stk_l[sp] = (uint8_t)(cl + 1);
                }
                zxc_pivco_unpack_flat(buf_d + seq_off[nid], c, D, bit_ptr[nid], c2s);
            } else {
                const int ch0 = nd->child[0];
                const int ch1 = nd->child[1];
                if (ch0 >= 0 && ch1 >= 0 && t.nd[ch0].sym >= 0 && t.nd[ch1].sym >= 0) {
                    zxc_pivco_emit_leaf_pair(buf_d + seq_off[nid], c, (uint8_t)t.nd[ch0].sym,
                                             (uint8_t)t.nd[ch1].sym, bit_ptr[nid]);
                    continue;
                }
                const uint32_t nl = ch0 >= 0 ? count[ch0] : 0;
                const uint32_t src_off = ch0 >= 0 ? seq_off[ch0] : seq_off[nd->child[1]];
                zxc_pivco_merge(buf_d + seq_off[nid], buf_c + src_off, nl, c - nl, bit_ptr[nid]);
            }
        }
    }
    return ZXC_OK;
}

int zxc_pivco_decode_section(const uint8_t* RESTRICT payload, const size_t payload_size,
                             uint8_t* RESTRICT dst, const size_t n, uint8_t* RESTRICT scratch) {
    if (UNLIKELY(payload_size < (size_t)ZXC_HUF_TABLE_SIZE)) return ZXC_ERROR_CORRUPT_DATA;
    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    const int rc = zxc_huf_unpack_lengths(payload, code_len);
    if (UNLIKELY(rc != ZXC_OK)) return rc;
    return zxc_pivco_decode_core(payload + ZXC_HUF_TABLE_SIZE, payload_size - ZXC_HUF_TABLE_SIZE,
                                 dst, n, code_len, scratch);
}

int zxc_pivco_decode_section_dict(const uint8_t* RESTRICT payload, const size_t payload_size,
                                  uint8_t* RESTRICT dst, const size_t n,
                                  const uint8_t* RESTRICT packed_lengths,
                                  uint8_t* RESTRICT scratch) {
    uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
    const int rc = zxc_huf_unpack_lengths(packed_lengths, code_len);
    if (UNLIKELY(rc != ZXC_OK)) return rc;
    return zxc_pivco_decode_core(payload, payload_size, dst, n, code_len, scratch);
}
