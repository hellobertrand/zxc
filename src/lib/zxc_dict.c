/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_dict.c
 * @brief Pre-trained dictionary: ID computation, .zxd serialization, and training.
 */

#include "../../include/zxc_dict.h"

#include "../../include/zxc_buffer.h"
#include "zxc_internal.h"

/* -------------------------------------------------------------------------
 *  Dictionary ID
 * ------------------------------------------------------------------------- */

uint32_t zxc_dict_id(const void* RESTRICT dict, const size_t dict_size,
                     const void* RESTRICT huf_lengths) {
    if (UNLIKELY(!dict || dict_size == 0)) return 0;
    /* One logical hash over the real bytes only: the content checksum seeds
     * the table checksum (content and table are not contiguous at the API
     * level, so chaining avoids both a concat copy and a synthetic buffer). */
    const uint32_t base = zxc_checksum(dict, dict_size, 0);
    if (huf_lengths == NULL) return base;
    return zxc_checksum_seed(huf_lengths, ZXC_HUF_TABLE_SIZE, base, 0);
}

/* -------------------------------------------------------------------------
 *  .zxd format: save / load / bound
 *
 *  Layout (ZXC_DICT_HEADER_SIZE = 16 bytes + content + Huffman table):
 *    0x00  4  Magic   (0x9CB0D1C7 LE)
 *    0x04  1  Version (1)
 *    0x05  1  Flags   (bits 0-3: checksum algo id, 0=RapidHash; bits 4-7 reserved)
 *    0x06  2  Content size (u16 LE)
 *    0x08  4  dict_id (u32 LE; covers content AND the Huffman table --
 *                      see zxc_dict_id)
 *    0x0C  2  Reserved (0)
 *    0x0E  2  Header CRC16 (zxc_hash16, computed with bytes 0x0C-0x0F zeroed)
 *    0x10  N  Content bytes
 *    +N    128 Packed Huffman code lengths (always present)
 * ------------------------------------------------------------------------- */

uint32_t zxc_dict_get_id(const void* buf, const size_t buf_size) {
    if (UNLIKELY(!buf || buf_size < ZXC_DICT_HEADER_SIZE)) return 0;
    const uint8_t* p = (const uint8_t*)buf;
    if (UNLIKELY(zxc_le32(p) != ZXC_DICT_MAGIC)) return 0;
    return zxc_le32(p + 8);
}

size_t zxc_dict_save_bound(const size_t content_size) {
    return ZXC_DICT_HEADER_SIZE + content_size + ZXC_HUF_TABLE_SIZE;
}

int64_t zxc_dict_save(const void* RESTRICT content, const size_t content_size,
                      const void* RESTRICT huf_lengths, void* RESTRICT buf,
                      const size_t buf_capacity) {
    if (UNLIKELY(!content || content_size == 0 || !huf_lengths)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(content_size > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;

    const size_t total = ZXC_DICT_HEADER_SIZE + content_size + ZXC_HUF_TABLE_SIZE;
    if (UNLIKELY(buf_capacity < total)) return ZXC_ERROR_DST_TOO_SMALL;

    uint8_t* dst = (uint8_t*)buf;

    zxc_store_le32(dst + 0, ZXC_DICT_MAGIC);
    dst[4] = ZXC_DICT_VERSION;
    dst[5] = 0; /* flags: reserved */
    zxc_store_le16(dst + 6, (uint16_t)content_size);
    zxc_store_le32(dst + 8, zxc_dict_id(content, content_size, (const uint8_t*)huf_lengths));
    zxc_store_le32(dst + 12, 0); /* reserved (0x0C) + CRC16 (0x0E), zeroed before CRC */
    const uint16_t crc = zxc_hash16(dst);
    zxc_store_le16(dst + 14, crc);

    ZXC_MEMCPY(dst + ZXC_DICT_HEADER_SIZE, content, content_size);
    ZXC_MEMCPY(dst + ZXC_DICT_HEADER_SIZE + content_size, huf_lengths, ZXC_HUF_TABLE_SIZE);

    return (int64_t)total;
}

int zxc_dict_load(const void* RESTRICT buf, const size_t buf_size,
                  const void** RESTRICT content_out, size_t* RESTRICT content_size_out,
                  const void** RESTRICT huf_out, uint32_t* RESTRICT dict_id_out) {
    if (UNLIKELY(!buf || !content_out || !content_size_out)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(buf_size < ZXC_DICT_HEADER_SIZE)) return ZXC_ERROR_SRC_TOO_SMALL;

    const uint8_t* src = (const uint8_t*)buf;

    if (UNLIKELY(zxc_le32(src) != ZXC_DICT_MAGIC)) return ZXC_ERROR_BAD_MAGIC;
    if (UNLIKELY(src[4] != ZXC_DICT_VERSION)) return ZXC_ERROR_BAD_VERSION;

    const size_t content_size = zxc_le16(src + 6);
    if (UNLIKELY(content_size == 0)) return ZXC_ERROR_CORRUPT_DATA;
    if (UNLIKELY(content_size > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;
    if (UNLIKELY(buf_size < ZXC_DICT_HEADER_SIZE + content_size + ZXC_HUF_TABLE_SIZE))
        return ZXC_ERROR_SRC_TOO_SMALL;  // LCOV_EXCL_LINE

    uint8_t temp[ZXC_DICT_HEADER_SIZE];
    ZXC_MEMCPY(temp, src, sizeof(temp));
    zxc_store_le32(temp + 12, 0); /* reserved (0x0C) + CRC16 (0x0E), zeroed before CRC */
    const uint16_t expected_crc = zxc_hash16(temp);
    if (UNLIKELY(zxc_le16(src + 14) != expected_crc)) return ZXC_ERROR_BAD_HEADER;

    const uint8_t* content = src + ZXC_DICT_HEADER_SIZE;
    const uint8_t* huf = content + content_size;

    /* Verify dict_id matches the (content, table) pair. */
    const uint32_t id = zxc_dict_id(content, content_size, huf);
    if (UNLIKELY(zxc_le32(src + 8) != id)) return ZXC_ERROR_BAD_CHECKSUM;

    *content_out = content;
    *content_size_out = content_size;
    if (huf_out) *huf_out = huf;
    if (dict_id_out) *dict_id_out = id;

    return ZXC_OK;
}

const void* zxc_dict_huf(const void* buf, const size_t buf_size) {
    if (UNLIKELY(!buf || buf_size < ZXC_DICT_HEADER_SIZE)) return NULL;
    const uint8_t* src = (const uint8_t*)buf;
    if (UNLIKELY(zxc_le32(src) != ZXC_DICT_MAGIC)) return NULL;
    if (UNLIKELY(src[4] != ZXC_DICT_VERSION)) return NULL;
    const size_t content_size = zxc_le16(src + 6);
    if (UNLIKELY(content_size == 0 ||
                 buf_size < ZXC_DICT_HEADER_SIZE + content_size + ZXC_HUF_TABLE_SIZE))
        return NULL;  // LCOV_EXCL_LINE
    return src + ZXC_DICT_HEADER_SIZE + content_size;
}

/* -------------------------------------------------------------------------
 *  Dictionary training: k-gram frequency selection
 *
 *  Algorithm:
 *  1. Concatenate all samples into a corpus.
 *  2. For each position in the corpus, hash the k-gram (k = MIN_MATCH_LEN)
 *     and count occurrences in a fixed-size hash map.
 *  3. Walk the corpus, building candidate segments: each starts at a frequent
 *     k-gram and extends while neighbours stay frequent. A segment's score is
 *     the summed frequency of its k-grams (its coverage of the corpus).
 *  4. Greedily fill the dictionary in descending coverage order, BUT account
 *     for overlap: once a pattern is placed, a single copy serves all future
 *     LZ matches, so its k-grams are zeroed in the frequency table. Segments
 *     whose coverage has since collapsed (mostly already in the dict) are
 *     skipped, so capacity goes to NEW patterns instead of redundant copies.
 * ------------------------------------------------------------------------- */

static uint32_t zxc_dict_hash(const uint8_t* p) {
    uint32_t v = zxc_le32(p);
    v ^= (uint32_t)p[4];
    return (v * ZXC_LZ_HASH_PRIME1) >> (32 - ZXC_DICT_HASH_BITS);
}

/**
 * @brief Segment descriptor for dictionary training, scored by coverage.
 */
typedef struct {
    uint32_t offset; /**< Offset of the segment in the corpus. */
    uint16_t length; /**< Length of the segment. */
    uint32_t score;  /**< Summed k-gram frequency (coverage) of the segment. */
} zxc_dict_seg_t;

/**
 * @brief Restore the min-heap property at @p root over the range @p a[0..n).
 *
 * Sinks @p a[root] down the binary heap (children at @c 2i+1 / @c 2i+2) until
 * both children are @c >= it, comparing on @ref zxc_dict_seg_t::score. The loop
 * is iterative (no recursion), so the call stack stays O(1) regardless of @p n.
 *
 * @param[in,out] a    Heap-ordered array; @p a[0..n) is treated as the heap.
 * @param[in]     root Index of the element to sift down. Must be @c < n.
 * @param[in]     n    Number of valid elements in the heap.
 *
 * @note Complexity O(log n).
 */
static void zxc_dict_sift_down(zxc_dict_seg_t* RESTRICT a, size_t root, const size_t n) {
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= n) break;
        if (child + 1 < n && a[child + 1].score < a[child].score) child++;
        if (a[root].score <= a[child].score) break;
        const zxc_dict_seg_t t = a[root];
        a[root] = a[child];
        a[child] = t;
        root = child;
    }
}

/**
 * @brief Sort @p a[0..n) by @ref zxc_dict_seg_t::score in descending order.
 *
 * In-place heapsort: a min-heap is built over the whole array, then each
 * extracted minimum is swapped to the shrinking tail. Because the smallest
 * scores accumulate at the end, the array is left in descending order
 * (largest score at index 0), as required by the dictionary fill step.
 *
 * Replaces a libc @c qsort call for two reasons:
 *  - **Freestanding/kernel-safe**: no dependency on @c qsort and no indirect
 *    comparator call (the @c score comparison is inlined in @ref
 *    zxc_dict_sift_down).
 *  - **Deterministic**: ordering is fixed by this code rather than by the
 *    platform's @c qsort, which matters for reproducible dictionary output
 *    across libc implementations.
 *
 * Equal scores keep an unspecified-but-deterministic relative order, matching
 * the previous comparator that returned 0 on ties (heapsort is not stable).
 *
 * @param[in,out] a Array of @p n segments, sorted in place.
 * @param[in]     n Number of segments. @c n < 2 is a no-op.
 *
 * @note Complexity O(n log n) worst case with no extra allocation. In practice
 *       this matches or beats @c qsort on the sizes seen here (up to ~65536
 *       segments): eliminating the per-comparison indirect call outweighs
 *       heapsort's weaker cache locality. This is a cold path (dictionary
 *       training), so absolute speed is not critical.
 */
static void zxc_dict_sort_segs_desc(zxc_dict_seg_t* RESTRICT a, const size_t n) {
    if (UNLIKELY(n < 2)) return;
    for (size_t i = n / 2; i-- > 0;) zxc_dict_sift_down(a, i, n);
    for (size_t end = n; end > 1;) {
        end--;
        const zxc_dict_seg_t t = a[0];
        a[0] = a[end];
        a[end] = t;
        zxc_dict_sift_down(a, 0, end);
    }
}

int64_t zxc_train_dict(const void* const* RESTRICT samples, const size_t* RESTRICT sample_sizes,
                       const size_t n_samples, void* RESTRICT dict_buf,
                       const size_t dict_capacity) {
    if (UNLIKELY(!samples || !sample_sizes || n_samples == 0 || !dict_buf || dict_capacity == 0))
        return ZXC_ERROR_NULL_INPUT;  // LCOV_EXCL_LINE
    if (UNLIKELY(dict_capacity > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;

    /* Step 1: concatenate samples */
    size_t corpus_size = 0;
    for (size_t i = 0; i < n_samples; i++) corpus_size += sample_sizes[i];
    if (UNLIKELY(corpus_size < ZXC_DICT_KGRAM_LEN)) return ZXC_ERROR_SRC_TOO_SMALL;

    uint8_t* corpus = (uint8_t*)ZXC_MALLOC(corpus_size);
    if (UNLIKELY(!corpus)) return ZXC_ERROR_MEMORY;
    {
        size_t pos = 0;
        for (size_t i = 0; i < n_samples; i++) {
            if (sample_sizes[i] > 0) ZXC_MEMCPY(corpus + pos, samples[i], sample_sizes[i]);
            pos += sample_sizes[i];
        }
    }

    /* Step 2: count k-gram frequencies */
    uint16_t* freq = (uint16_t*)ZXC_MALLOC(ZXC_DICT_HASH_SIZE * sizeof(uint16_t));
    if (UNLIKELY(!freq)) {
        // LCOV_EXCL_START
        ZXC_FREE(corpus);
        return ZXC_ERROR_MEMORY;
        // LCOV_EXCL_STOP
    }
    ZXC_MEMSET(freq, 0, ZXC_DICT_HASH_SIZE * sizeof(uint16_t));

    /* Count k-gram frequencies on a representative sample of positions, not all
     * of them: counting a large corpus in full saturates the 16-bit counters,
     * so the segment-extension test never stops and segments balloon into
     * filler. Sampling keeps counts unsaturated and spread across the corpus. */
    const size_t kgram_limit = corpus_size - ZXC_DICT_KGRAM_LEN + 1;
    size_t freq_stride = kgram_limit / ZXC_DICT_SAMPLE_TARGET;
    if (freq_stride < 1) freq_stride = 1;
    for (size_t i = 0; i < kgram_limit; i += freq_stride) {
        const uint32_t h = zxc_dict_hash(corpus + i);
        if (freq[h] < UINT16_MAX) freq[h]++;
    }

    /* Step 3: build candidate segments, each scored by its coverage. Spread the
     * candidate starts across the whole corpus: a fixed k-gram stride exhausts
     * the segment budget within the prefix, leaving a large input's later
     * content unseen. Segments still extend k-gram by k-gram, so they stay
     * contiguous. */
    const size_t max_segs = corpus_size / ZXC_DICT_KGRAM_LEN;
    const size_t seg_alloc = (max_segs < ZXC_DICT_MAX_SEGMENTS) ? max_segs : ZXC_DICT_MAX_SEGMENTS;
    size_t stride = ZXC_DICT_KGRAM_LEN;
    if (seg_alloc > 0 && corpus_size / seg_alloc > stride) stride = corpus_size / seg_alloc;

    zxc_dict_seg_t* segs = (zxc_dict_seg_t*)ZXC_MALLOC(seg_alloc * sizeof(zxc_dict_seg_t));
    if (UNLIKELY(!segs)) {
        // LCOV_EXCL_START
        ZXC_FREE(freq);
        ZXC_FREE(corpus);
        return ZXC_ERROR_MEMORY;
        // LCOV_EXCL_STOP
    }

    size_t n_segs = 0;
    for (size_t i = 0; i + ZXC_DICT_KGRAM_LEN <= corpus_size && n_segs < seg_alloc; i += stride) {
        const uint32_t h = zxc_dict_hash(corpus + i);
        const uint16_t f = freq[h];
        if (f < 2) continue;

        /* Extend the segment as long as the next k-gram is also frequent, and
         * accumulate coverage (summed k-gram frequency) as the score. */
        uint32_t coverage = f;
        size_t end = i + ZXC_DICT_KGRAM_LEN;
        while (end + ZXC_DICT_KGRAM_LEN <= corpus_size && end - i < 4096) {
            const uint16_t nf = freq[zxc_dict_hash(corpus + end)];
            if (nf < 2) break;
            coverage += nf;
            end += ZXC_DICT_KGRAM_LEN;
        }

        segs[n_segs].offset = (uint32_t)i;
        segs[n_segs].length = (uint16_t)(end - i);
        segs[n_segs].score = coverage;
        n_segs++;
    }

    if (UNLIKELY(n_segs == 0)) {
        /* No frequent patterns. Use tail of corpus as dict. */
        const size_t copy = (corpus_size < dict_capacity) ? corpus_size : dict_capacity;
        ZXC_MEMCPY(dict_buf, corpus + corpus_size - copy, copy);
        ZXC_FREE(freq);
        ZXC_FREE(segs);
        ZXC_FREE(corpus);
        return (int64_t)copy;
    }

    /* Step 4: pick segments greedily in descending-coverage order, zeroing each
     * pick's k-grams so overlapping patterns aren't copied twice. Picks are
     * compacted in place into segs[0..n_sel); placement is step 5. */
    zxc_dict_sort_segs_desc(segs, n_segs);

    uint8_t* out = (uint8_t*)dict_buf;
    size_t n_sel = 0;
    size_t total = 0;

    for (size_t i = 0; i < n_segs && total < dict_capacity; i++) {
        const size_t seg_off = segs[i].offset;
        const size_t seg_end = seg_off + segs[i].length;

        /* Recompute coverage from the decrementing table: skip the segment if
         * earlier picks have already covered more than half of its k-grams. */
        uint32_t cur = 0;
        for (size_t p = seg_off; p + ZXC_DICT_KGRAM_LEN <= seg_end; p += ZXC_DICT_KGRAM_LEN)
            cur += freq[zxc_dict_hash(corpus + p)];
        if (cur * 2 < segs[i].score) continue;

        size_t copy = segs[i].length;
        if (copy > dict_capacity - total) copy = dict_capacity - total;

        /* One copy in the dictionary serves all future matches: mark this
         * segment's k-grams as covered so later segments cover new ground. */
        for (size_t p = seg_off; p + ZXC_DICT_KGRAM_LEN <= seg_end; p += ZXC_DICT_KGRAM_LEN)
            freq[zxc_dict_hash(corpus + p)] = 0;

        /* Record the pick (n_sel <= i, so this never clobbers an unread entry). */
        segs[n_sel].offset = (uint32_t)seg_off;
        segs[n_sel].length = (uint16_t)copy;
        n_sel++;
        total += copy;
    }

    ZXC_FREE(freq);

    /* Step 5: emit picks in reverse order so the highest-coverage segment ends
     * up at the END of the dict. The dict sits just before the data, so bytes
     * nearer its end have the smallest match offset: cheapest to encode and the
     * last to leave the 16-bit (65535) offset window.
     *
     * No padding: if the picks don't fill the capacity, the dict is just
     * shorter. The old tail-padding only added low-value bytes that raised
     * offsets for everything after them. */
    size_t filled = 0;
    for (size_t i = n_sel; i-- > 0;) {
        ZXC_MEMCPY(out + filled, corpus + segs[i].offset, segs[i].length);
        filled += segs[i].length;
    }

    /* Nothing selected (every segment subsumed by earlier picks): fall back to
     * the corpus tail so the dict is never empty, like the n_segs == 0 path. */
    if (UNLIKELY(filled == 0)) {
        const size_t tail = (corpus_size < dict_capacity) ? corpus_size : dict_capacity;
        ZXC_MEMCPY(out, corpus + corpus_size - tail, tail);
        filled = tail;
    }

    ZXC_FREE(segs);
    ZXC_FREE(corpus);
    return (int64_t)filled;
}

/* -------------------------------------------------------------------------
 *  Shared literal Huffman table training (Tier-2)
 *
 *  Compresses the training samples with the freshly trained dictionary at
 *  level ZXC_LEVEL_DENSITY and accumulates the frequencies of the REAL
 *  post-LZ literals (via the cctx lit_freq_acc hook in the GLO encoder).
 *  Raw sample bytes are a poor proxy: LZ matches against the dictionary
 *  remove most repeated content, so the literal distribution differs
 *  substantially from the raw byte histogram.
 *
 *  Samples are sliced into ZXC_DICT_HUF_TRAIN_BLOCK-byte blocks: the small-
 *  block regime is where the shared table pays (per-block table headers are
 *  unaffordable there) and literal density is highest.
 * ------------------------------------------------------------------------- */

int zxc_train_dict_huf(const void* const* RESTRICT samples, const size_t* RESTRICT sample_sizes,
                       const size_t n_samples, const void* RESTRICT dict, const size_t dict_size,
                       uint8_t* RESTRICT huf_lengths_out) {
    if (UNLIKELY(!samples || !sample_sizes || n_samples == 0 || !dict || dict_size == 0 ||
                 !huf_lengths_out))
        return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(dict_size > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;

    const size_t eff_chunk = zxc_block_size_ceil(dict_size + ZXC_DICT_HUF_TRAIN_BLOCK);

    zxc_cctx_t cctx;
    if (UNLIKELY(zxc_cctx_init(&cctx, eff_chunk, 1, ZXC_LEVEL_DENSITY, 0, dict_size) != ZXC_OK))
        return ZXC_ERROR_MEMORY;  // LCOV_EXCL_LINE

    uint32_t freq[ZXC_HUF_NUM_SYMBOLS] = {0};
    cctx.lit_freq_acc = freq;

    const size_t out_cap = (size_t)zxc_compress_bound(eff_chunk);
    uint8_t* out_scratch = (uint8_t*)ZXC_MALLOC(out_cap);
    if (UNLIKELY(!out_scratch)) {
        // LCOV_EXCL_START
        zxc_cctx_free(&cctx);
        return ZXC_ERROR_MEMORY;
        // LCOV_EXCL_STOP
    }

    /* [dict | slice] concat scratch carved by zxc_cctx_init (mode 1). */
    uint8_t* const work = cctx.dict_buffer;
    ZXC_MEMCPY(work, dict, dict_size);

    /* Slice stride: process every slice while the corpus fits the budget,
     * else 1 slice out of `stride` spread evenly across all samples. */
    size_t corpus_total = 0;
    for (size_t s = 0; s < n_samples; s++) corpus_total += sample_sizes[s];
    const size_t stride =
        (corpus_total > ZXC_DICT_HUF_SAMPLE_BUDGET)
            ? (corpus_total + ZXC_DICT_HUF_SAMPLE_BUDGET - 1) / ZXC_DICT_HUF_SAMPLE_BUDGET
            : 1;

    int rc = ZXC_OK;
    size_t slice_idx = 0;
    for (size_t s = 0; s < n_samples && rc == ZXC_OK; s++) {
        const uint8_t* sample = (const uint8_t*)samples[s];
        const size_t sample_size = sample_sizes[s];
        if (!sample || sample_size == 0) continue;

        for (size_t off = 0; off < sample_size; off += ZXC_DICT_HUF_TRAIN_BLOCK, slice_idx++) {
            if (slice_idx % stride != 0) continue;
            const size_t slice = (sample_size - off < ZXC_DICT_HUF_TRAIN_BLOCK)
                                     ? (sample_size - off)
                                     : ZXC_DICT_HUF_TRAIN_BLOCK;
            ZXC_MEMCPY(work + dict_size, sample + off, slice);
            const int r =
                zxc_compress_chunk_wrapper(&cctx, work, dict_size + slice, out_scratch, out_cap);
            if (UNLIKELY(r < 0)) {
                rc = r;
                break;
            }
        }
    }

    if (rc == ZXC_OK) {
        /* No coverage smoothing: with ZXC_HUF_MAX_CODE_LEN == 8, a code over
         * all 256 symbols can only be the degenerate all-8-bit code (Kraft
         * equality), which compresses nothing. Symbols unseen in training stay
         * code-less; blocks containing one fall back to their per-block table
         * at compression time (the encoder's validity check). */
        uint8_t code_len[ZXC_HUF_NUM_SYMBOLS];
        rc = zxc_huf_build_code_lengths(freq, code_len, NULL);
        if (rc == ZXC_OK) zxc_huf_pack_lengths(code_len, huf_lengths_out);
    }

    ZXC_FREE(out_scratch);
    zxc_cctx_free(&cctx);
    return rc;
}

/* -------------------------------------------------------------------------
 *  All-in-one convenience: samples -> ready-to-write .zxd bytes
 * ------------------------------------------------------------------------- */

int64_t zxc_dict_train(const void* const* RESTRICT samples, const size_t* RESTRICT sample_sizes,
                       const size_t n_samples, void* RESTRICT zxd_buf, const size_t zxd_capacity) {
    if (UNLIKELY(!samples || !sample_sizes || n_samples == 0 || !zxd_buf || zxd_capacity == 0))
        return ZXC_ERROR_NULL_INPUT;

    /* Train the content into a temporary buffer (max-content sized), then the
     * shared table from that content, then serialize both into zxd_buf. The
     * two-phase training is a real data dependency (the table needs the trained
     * content to histogram post-LZ literals); this hides it behind one call. */
    uint8_t* content = (uint8_t*)ZXC_MALLOC(ZXC_DICT_SIZE_MAX);
    if (UNLIKELY(!content)) return ZXC_ERROR_MEMORY;

    int64_t out;
    const int64_t content_size =
        zxc_train_dict(samples, sample_sizes, n_samples, content, ZXC_DICT_SIZE_MAX);
    if (UNLIKELY(content_size <= 0)) {
        // LCOV_EXCL_START
        out = (content_size < 0) ? content_size : ZXC_ERROR_SRC_TOO_SMALL;
        goto done;
        // LCOV_EXCL_STOP
    }

    {
        uint8_t huf[ZXC_HUF_TABLE_SIZE];
        const int hrc = zxc_train_dict_huf(samples, sample_sizes, n_samples, content,
                                           (size_t)content_size, huf);
        if (UNLIKELY(hrc != ZXC_OK)) {
            // LCOV_EXCL_START
            out = hrc;
            goto done;
            // LCOV_EXCL_STOP
        }
        out = zxc_dict_save(content, (size_t)content_size, huf, zxd_buf, zxd_capacity);
    }

done:
    ZXC_FREE(content);
    return out;
}
