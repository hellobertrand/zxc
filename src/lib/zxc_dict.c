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

#include "zxc_internal.h"

/* -------------------------------------------------------------------------
 *  Dictionary ID
 * ------------------------------------------------------------------------- */

uint32_t zxc_dict_id(const void* dict, const size_t dict_size) {
    if (UNLIKELY(!dict || dict_size == 0)) return 0;
    return zxc_checksum(dict, dict_size, 0);
}

/* -------------------------------------------------------------------------
 *  .zxd format: save / load / bound
 *
 *  Layout (ZXC_DICT_HEADER_SIZE = 16 bytes + content):
 *    0x00  4  Magic   (0x9CB0D1C7 LE)
 *    0x04  1  Version (1)
 *    0x05  1  Flags   (reserved, 0)
 *    0x06  2  Content size (u16 LE)
 *    0x08  4  dict_id (u32 LE)
 *    0x0C  2  Header CRC16 (zxc_hash16, computed with bytes 0x0C-0x0F zeroed)
 *    0x0E  2  Reserved (0)
 *    0x10  N  Content bytes
 * ------------------------------------------------------------------------- */

uint32_t zxc_dict_get_id(const void* buf, const size_t buf_size) {
    if (UNLIKELY(!buf || buf_size < ZXC_DICT_HEADER_SIZE)) return 0;
    const uint8_t* p = (const uint8_t*)buf;
    if (UNLIKELY(zxc_le32(p) != ZXC_DICT_MAGIC)) return 0;
    return zxc_le32(p + 8);
}

size_t zxc_dict_save_bound(const size_t content_size) {
    return ZXC_DICT_HEADER_SIZE + content_size;
}

int64_t zxc_dict_save(const void* content, const size_t content_size, void* buf,
                      const size_t buf_capacity) {
    if (UNLIKELY(!content || content_size == 0)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(content_size > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;

    const size_t total = ZXC_DICT_HEADER_SIZE + content_size;
    if (UNLIKELY(buf_capacity < total)) return ZXC_ERROR_DST_TOO_SMALL;

    uint8_t* dst = (uint8_t*)buf;

    zxc_store_le32(dst + 0, ZXC_DICT_MAGIC);
    dst[4] = ZXC_DICT_VERSION;
    dst[5] = 0; /* flags: reserved */
    zxc_store_le16(dst + 6, (uint16_t)content_size);
    zxc_store_le32(dst + 8, zxc_dict_id(content, content_size));
    zxc_store_le16(dst + 12, 0);
    zxc_store_le16(dst + 14, 0);
    const uint16_t crc = zxc_hash16(dst);
    zxc_store_le16(dst + 12, crc);

    ZXC_MEMCPY(dst + ZXC_DICT_HEADER_SIZE, content, content_size);

    return (int64_t)total;
}

int zxc_dict_load(const void* buf, const size_t buf_size, const void** content_out,
                  size_t* content_size_out, uint32_t* dict_id_out) {
    if (UNLIKELY(!buf || !content_out || !content_size_out)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(buf_size < ZXC_DICT_HEADER_SIZE)) return ZXC_ERROR_SRC_TOO_SMALL;

    const uint8_t* src = (const uint8_t*)buf;

    if (zxc_le32(src) != ZXC_DICT_MAGIC) return ZXC_ERROR_BAD_MAGIC;
    if (src[4] != ZXC_DICT_VERSION) return ZXC_ERROR_BAD_VERSION;

    const size_t content_size = zxc_le16(src + 6);
    if (UNLIKELY(content_size == 0)) return ZXC_ERROR_CORRUPT_DATA;
    if (UNLIKELY(content_size > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;
    if (UNLIKELY(buf_size < ZXC_DICT_HEADER_SIZE + content_size)) return ZXC_ERROR_SRC_TOO_SMALL;

    uint8_t temp[ZXC_DICT_HEADER_SIZE];
    ZXC_MEMCPY(temp, src, ZXC_DICT_HEADER_SIZE);
    zxc_store_le16(temp + 12, 0);
    zxc_store_le16(temp + 14, 0);
    const uint16_t expected_crc = zxc_hash16(temp);
    if (UNLIKELY(zxc_le16(src + 12) != expected_crc)) return ZXC_ERROR_BAD_HEADER;

    /* Verify dict_id matches content */
    const uint8_t* content = src + ZXC_DICT_HEADER_SIZE;
    const uint32_t id = zxc_dict_id(content, content_size);
    if (UNLIKELY(zxc_le32(src + 8) != id)) return ZXC_ERROR_BAD_CHECKSUM;

    *content_out = content;
    *content_size_out = content_size;
    if (dict_id_out) *dict_id_out = id;

    return ZXC_OK;
}

/* -------------------------------------------------------------------------
 *  Dictionary training: k-gram frequency selection
 *
 *  Algorithm:
 *  1. Concatenate all samples into a corpus.
 *  2. For each position in the corpus, hash the k-gram (k = MIN_MATCH_LEN)
 *     and count occurrences in a fixed-size hash map.
 *  3. Walk the corpus a second time: for each position, look up the k-gram
 *     frequency and greedily select segments whose k-grams have the highest
 *     frequency x length score.
 *  4. The most frequent segments are placed at the END of the dictionary
 *     so they produce shorter offsets (closer to the block start).
 * ------------------------------------------------------------------------- */

#define ZXC_DICT_KGRAM_LEN ZXC_LZ_MIN_MATCH_LEN
#define ZXC_DICT_HT_BITS 16
#define ZXC_DICT_HT_SIZE (1U << ZXC_DICT_HT_BITS)
#define ZXC_DICT_HT_MASK (ZXC_DICT_HT_SIZE - 1U)

static uint32_t zxc_dict_hash(const uint8_t* p) {
    uint32_t v = zxc_le32(p);
    v ^= (uint32_t)p[4];
    return (v * ZXC_LZ_HASH_PRIME1) >> (32 - ZXC_DICT_HT_BITS);
}

/**
 * @brief Segment descriptor for dictionary training, scored by coverage.
 */
typedef struct {
    uint32_t offset;
    uint16_t length;
    uint16_t score;
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

int64_t zxc_train_dict(const void* const* samples, const size_t* sample_sizes,
                       const size_t n_samples, void* dict_buf, const size_t dict_capacity) {
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
    uint16_t* freq = (uint16_t*)ZXC_MALLOC(ZXC_DICT_HT_SIZE * sizeof(uint16_t));
    if (UNLIKELY(!freq)) {
        // LCOV_EXCL_START
        ZXC_FREE(corpus);
        return ZXC_ERROR_MEMORY;
        // LCOV_EXCL_STOP
    }
    ZXC_MEMSET(freq, 0, ZXC_DICT_HT_SIZE * sizeof(uint16_t));

    const size_t kgram_limit = corpus_size - ZXC_DICT_KGRAM_LEN + 1;
    for (size_t i = 0; i < kgram_limit; i++) {
        const uint32_t h = zxc_dict_hash(corpus + i);
        if (freq[h] < UINT16_MAX) freq[h]++;
    }

    /* Step 3: score segments: stride by k-gram length to avoid overlap,
     * collect top-scoring segments. */
    const size_t stride = ZXC_DICT_KGRAM_LEN;
    const size_t max_segs = corpus_size / stride;
    const size_t seg_alloc = (max_segs < 65536) ? max_segs : 65536;

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

        /* Extend the segment as long as the next k-gram is also frequent. */
        size_t end = i + ZXC_DICT_KGRAM_LEN;
        while (end + ZXC_DICT_KGRAM_LEN <= corpus_size && end - i < 255) {
            const uint16_t nf = freq[zxc_dict_hash(corpus + end)];
            if (nf < 2) break;
            end += ZXC_DICT_KGRAM_LEN;
        }

        segs[n_segs].offset = (uint32_t)i;
        segs[n_segs].length = (uint16_t)(end - i);
        segs[n_segs].score = f;
        n_segs++;
    }

    ZXC_FREE(freq);

    if (UNLIKELY(n_segs == 0)) {
        /* No frequent patterns. Use tail of corpus as dict. */
        const size_t copy = (corpus_size < dict_capacity) ? corpus_size : dict_capacity;
        ZXC_MEMCPY(dict_buf, corpus + corpus_size - copy, copy);
        ZXC_FREE(segs);
        ZXC_FREE(corpus);
        return (int64_t)copy;
    }

    /* Step 4: sort by score descending, fill dict from end (most frequent last
     * = shortest offsets from block start). */
    zxc_dict_sort_segs_desc(segs, n_segs);

    uint8_t* out = (uint8_t*)dict_buf;
    size_t filled = 0;

    for (size_t i = 0; i < n_segs && filled < dict_capacity; i++) {
        size_t copy = segs[i].length;
        if (copy > dict_capacity - filled) copy = dict_capacity - filled;
        ZXC_MEMCPY(out + filled, corpus + segs[i].offset, copy);
        filled += copy;
    }

    /* If we haven't filled the capacity, pad with tail of corpus. */
    if (filled < dict_capacity) {
        const size_t pad = dict_capacity - filled;
        const size_t tail = (corpus_size > pad) ? pad : corpus_size;
        ZXC_MEMCPY(out + filled, corpus + corpus_size - tail, tail);
        filled += tail;
    }

    ZXC_FREE(segs);
    ZXC_FREE(corpus);
    return (int64_t)filled;
}
