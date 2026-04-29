/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Suffix array construction by the original Itoh-Tanaka two-stage induced
 * sort with Bentley-Sedgewick multi-key quicksort, plus Kasai LCP and
 * inverse-SA helpers. Used by the level-6 SA-walk match finder.
 *
 * Algorithm reference (patent-free, published 1999):
 *   H. Itoh, H. Tanaka, "An efficient method for in memory construction of
 *   suffix arrays", in Proc. SPIRE 1999.
 *
 * Pipeline:
 *   (1) Classify each position as type A (T[i] >= T[i+1] under inheritance)
 *       or type B (T[i] < T[i+1] under inheritance). The virtual sentinel
 *       after T[n-1] is strictly smallest, so T[n-1] is always type A.
 *   (2) Collect all type-B positions and sort them lexicographically by
 *       full suffix. Sub-step 2a buckets them by their first 2 characters
 *       (256x256 buckets, single radix pass), writing positions directly.
 *       Sub-step 2b sorts within each bucket via Bentley-Sedgewick
 *       multi-key quicksort starting at depth 2.
 *   (3) Place sorted type-B positions into SA at bucket-end slots in
 *       reverse lex order. After this, every type-B suffix is in its
 *       final SA slot.
 *   (4) Stamp the sentinel at SA[0] = n.
 *   (5) Induce-sort all type-A suffixes by a single left-to-right scan
 *       of SA: for each j = SA[k] with j > 0 and is_B[j-1] == 0, place
 *       j-1 at SA[bkt_start[T[j-1]]++].
 *
 * Sorting cost: the multi-key quicksort reads each input byte at most
 * once per recursion level (rather than once per pairwise comparison),
 * giving O(n log n) byte work. On real compression data with long common
 * prefixes this is the difference between sub-second SA construction
 * and a ~100x slower naive comparison sort.
 *
 * Memory: per-call scratch (is_B, pos_B, sorted_B, bkt2) is malloc'd at
 * entry and freed before return. Transient peak ~5n + 256 KB for n bytes
 * of input. On allocation failure the function falls back to an identity
 * permutation so callers never crash; the resulting block degrades to a
 * literals-heavy encoding.
 *
 * SA layout: SA[0..n] covers n+1 entries. SA[0] is reserved for the
 * sentinel suffix at position n. SA[1..n+1) holds the sorted real
 * suffixes. ISA[SA[k]] == k for all k in [0, n].
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_error.h"
#include "zxc_internal.h"

/**
 * @brief Returns the byte at @p depth into the suffix starting at @p pos,
 *        or @c -1 if the suffix is exhausted.
 *
 * The virtual sentinel after @c T[n-1] is strictly smaller than any
 * real byte, so an exhausted suffix sorts before any non-exhausted one.
 *
 * @param[in] T     Input bytes.
 * @param[in] n     Input length.
 * @param[in] pos   Suffix start position.
 * @param[in] depth Byte offset from @p pos.
 * @return Byte value @c [0, 255], or @c -1 if @c pos + depth >= n.
 */
static ZXC_ALWAYS_INLINE int32_t it_byte_at(const uint8_t* RESTRICT T, const int32_t n,
                                            const int32_t pos, const int32_t depth) {
    const int32_t p = pos + depth;
    return (p < n) ? (int32_t)T[p] : -1;
}

/**
 * @brief Depth-aware suffix comparator on raw positions.
 *
 * Compares @c suffix(p1) and @c suffix(p2) starting from byte offset
 * @p depth, returning negative / zero / positive. Used by the
 * insertion-sort base case of @ref it_mkq where the caller guarantees
 * that the first @p depth bytes of the two suffixes are already known
 * to be equal.
 *
 * @param[in] T     Input bytes.
 * @param[in] n     Input length.
 * @param[in] p1    First suffix position.
 * @param[in] p2    Second suffix position.
 * @param[in] depth Number of leading bytes already known equal.
 * @return Negative / zero / positive comparison result.
 */
static int it_cmp_at_depth(const uint8_t* RESTRICT T, const int32_t n, const int32_t p1,
                           const int32_t p2, const int32_t depth) {
    const int32_t a1 = p1 + depth;
    const int32_t a2 = p2 + depth;
    const int32_t l1 = n - a1;
    const int32_t l2 = n - a2;
    const int32_t lim = (l1 < l2) ? l1 : l2;
    int32_t d = 0;
    /* Batch 8 bytes per iteration: read both suffixes as uint64, compare
     * via byte-swapped values (memory-order = big-endian = lex order).
     * On real text data this skips 7 of every 8 bytes in long shared
     * prefixes. */
    while (d + 8 <= lim) {
        uint64_t u1, u2;
        ZXC_MEMCPY(&u1, T + a1 + d, 8);
        ZXC_MEMCPY(&u2, T + a2 + d, 8);
        if (u1 != u2) {
            const uint64_t b1 = __builtin_bswap64(u1);
            const uint64_t b2 = __builtin_bswap64(u2);
            return (b1 < b2) ? -1 : 1;
        }
        d += 8;
    }
    for (; d < lim; d++) {
        const int a = (int)T[a1 + d];
        const int b = (int)T[a2 + d];
        if (a != b) return a - b;
    }
    return l1 - l2;
}

/**
 * @brief Swaps two int32_t values in place.
 *
 * @param[in,out] a First operand.
 * @param[in,out] b Second operand.
 */
static ZXC_ALWAYS_INLINE void it_swap(int32_t* RESTRICT a, int32_t* RESTRICT b) {
    const int32_t t = *a;
    *a = *b;
    *b = t;
}

/**
 * @brief Insertion sort on a small position range using the depth-aware
 *        comparator.
 *
 * Used as the base case of @ref it_mkq for ranges of length @c <= 16.
 * The caller guarantees that the first @p depth bytes are already
 * equal across the range, so the comparator skips them.
 *
 * @param[in]     T     Input bytes.
 * @param[in]     n     Input length.
 * @param[in,out] arr   Position array (mutated in place).
 * @param[in]     lo    Inclusive start of range.
 * @param[in]     hi    Exclusive end of range.
 * @param[in]     depth Bytes already known equal.
 */
static void it_isort_mkq(const uint8_t* RESTRICT T, const int32_t n, int32_t* RESTRICT arr,
                         const int32_t lo, const int32_t hi, const int32_t depth) {
    for (int32_t i = lo + 1; i < hi; i++) {
        const int32_t v = arr[i];
        int32_t j = i;
        while (j > lo && it_cmp_at_depth(T, n, arr[j - 1], v, depth) > 0) {
            arr[j] = arr[j - 1];
            j--;
        }
        arr[j] = v;
    }
}

/**
 * @brief Bentley-Sedgewick multi-key quicksort on suffix positions.
 *
 * Sorts @c arr[lo..hi) by the lex order of their suffixes, looking
 * only at the byte at @p depth in each three-way partition pass. The
 * caller guarantees that the first @p depth bytes of every suffix in
 * the range are already known to be equal (typically because @p arr
 * came from a pre-bucketing radix pass).
 *
 * Algorithm at each level:
 *   1. Median-of-3 pivot selection on the byte at @p depth.
 *   2. Dutch-flag three-way partition by that byte:
 *      @c [lo, lt) < pv, @c [lt, gt+1) == pv, @c (gt, hi) > pv.
 *   3. Iterate on the largest sub-range to keep stack depth at
 *      O(log n). The @c == sub-range uses @c depth+1; the @c < and
 *      @c > sub-ranges keep @p depth.
 *   4. If the pivot is the virtual sentinel (@c pv == -1), the @c ==
 *      sub-range has size at most 1 (only one position can be exhausted
 *      at any given depth) and is trivially sorted.
 *
 * Total byte work across all levels is bounded by @c O(n log n) rather
 * than @c O(n^2) of a naive comparison sort, because each byte is read
 * at most once per recursion level, not per comparison.
 *
 * @param[in]     T     Input bytes.
 * @param[in]     n     Input length.
 * @param[in,out] arr   Position array (mutated in place).
 * @param[in]     lo    Inclusive start of range.
 * @param[in]     hi    Exclusive end of range.
 * @param[in]     depth Bytes already known equal across the range.
 */
static void it_mkq(const uint8_t* RESTRICT T, const int32_t n, int32_t* RESTRICT arr, int32_t lo,
                   int32_t hi, int32_t depth) {
    while (hi - lo > 16) {
        const int32_t mid = lo + ((hi - lo) >> 1);

        /* Median-of-3 pivot on byte at depth. */
        int32_t b_lo = it_byte_at(T, n, arr[lo], depth);
        int32_t b_mid = it_byte_at(T, n, arr[mid], depth);
        int32_t b_hi = it_byte_at(T, n, arr[hi - 1], depth);
        if (b_lo > b_mid) {
            it_swap(&arr[lo], &arr[mid]);
            const int32_t t = b_lo;
            b_lo = b_mid;
            b_mid = t;
        }
        if (b_lo > b_hi) {
            it_swap(&arr[lo], &arr[hi - 1]);
            const int32_t t = b_lo;
            b_lo = b_hi;
            b_hi = t;
        }
        if (b_mid > b_hi) {
            it_swap(&arr[mid], &arr[hi - 1]);
            const int32_t t = b_mid;
            b_mid = b_hi;
            b_hi = t;
        }
        const int32_t pv = b_mid;

        /* Three-way Dutch-flag partition by byte at depth. */
        int32_t lt = lo;
        int32_t gt = hi - 1;
        int32_t i = lo;
        while (i <= gt) {
            const int32_t b = it_byte_at(T, n, arr[i], depth);
            if (b < pv) {
                it_swap(&arr[i], &arr[lt]);
                lt++;
                i++;
            } else if (b > pv) {
                it_swap(&arr[i], &arr[gt]);
                gt--;
            } else {
                i++;
            }
        }

        /* Sub-range sizes. */
        const int32_t sz_lt = lt - lo;
        const int32_t sz_eq = gt + 1 - lt;
        const int32_t sz_gt = hi - gt - 1;

        /* Iterate on the largest, recurse on the two smaller. */
        if (sz_lt >= sz_eq && sz_lt >= sz_gt) {
            if (pv >= 0 && sz_eq > 1) it_mkq(T, n, arr, lt, gt + 1, depth + 1);
            if (sz_gt > 1) it_mkq(T, n, arr, gt + 1, hi, depth);
            hi = lt;
        } else if (sz_eq >= sz_gt) {
            if (sz_lt > 1) it_mkq(T, n, arr, lo, lt, depth);
            if (sz_gt > 1) it_mkq(T, n, arr, gt + 1, hi, depth);
            if (pv < 0) break; /* sentinel: == has size 1 */
            lo = lt;
            hi = gt + 1;
            depth++;
        } else {
            if (sz_lt > 1) it_mkq(T, n, arr, lo, lt, depth);
            if (pv >= 0 && sz_eq > 1) it_mkq(T, n, arr, lt, gt + 1, depth + 1);
            lo = gt + 1;
        }
    }
    it_isort_mkq(T, n, arr, lo, hi, depth);
}

/**
 * @brief Classifies positions A/B and collects type-B positions.
 *
 * Walks @c T backward in a single pass, filling @p is_B (1 byte per
 * position: 1 = type B, 0 = type A) and recording type-B positions in
 * @p pos_B in ascending order. The virtual sentinel after @c T[n-1] is
 * strictly smallest, so @c T[n-1] is always type A.
 *
 * @param[in]  T     Input bytes, size @p n.
 * @param[in]  n     Input length (must be @c >= 1).
 * @param[out] is_B  Type bitmap, size @p n. After return: 1 = B, 0 = A.
 * @param[out] pos_B Type-B positions in ascending order, size at least
 *                   @c n.
 * @return Number of type-B positions (always @c <= n-1, since
 *         @c T[n-1] is type A).
 */
static int32_t it_classify(const uint8_t* RESTRICT T, const int32_t n, uint8_t* RESTRICT is_B,
                           int32_t* RESTRICT pos_B) {
    is_B[n - 1] = 0;
    for (int32_t i = n - 2; i >= 0; i--) {
        if (T[i] < T[i + 1]) {
            is_B[i] = 1;
        } else if (T[i] > T[i + 1]) {
            is_B[i] = 0;
        } else {
            is_B[i] = is_B[i + 1];
        }
    }
    int32_t m = 0;
    for (int32_t i = 0; i < n; i++) {
        if (is_B[i]) pos_B[m++] = i;
    }
    return m;
}

/**
 * @brief Buckets type-B positions by their first two characters.
 *
 * Single radix pass: counts pairs @c (T[p], T[p+1]) for each type-B
 * position @c p, computes exclusive prefix sums to derive bucket
 * starts, then writes positions directly into @p sorted_B at their
 * respective bucket slots. @c T[p+1] is always in bounds because
 * @c is_B[p] == 1 implies @c p < n-1.
 *
 * After this routine, @p bkt2 holds inclusive end pointers per pair
 * bucket: @c bkt2[i] is the position one past the last entry of bucket
 * @c i in @p sorted_B. Bucket starts are recovered as @c bkt2[i-1] (or
 * 0 for @c i==0).
 *
 * @param[in]  T         Input bytes.
 * @param[in]  n         Input length.
 * @param[in]  pos_B     Type-B positions in ascending order.
 * @param[in]  m         Number of type-B positions.
 * @param[out] bkt2      Per-pair bucket end pointers, size @c 256*256.
 * @param[out] sorted_B  Positions placed into pair buckets, size @c m.
 */
static void it_bucket_B_by_pair(const uint8_t* RESTRICT T, const int32_t n,
                                const int32_t* RESTRICT pos_B, const int32_t m,
                                int32_t* RESTRICT bkt2, int32_t* RESTRICT sorted_B) {
    (void)n;
    ZXC_MEMSET(bkt2, 0, (size_t)(256 * 256) * sizeof(int32_t));

    for (int32_t k = 0; k < m; k++) {
        const int32_t p = pos_B[k];
        const uint32_t c0 = (uint32_t)T[p];
        const uint32_t c1 = (uint32_t)T[p + 1];
        bkt2[(c0 << 8) | c1]++;
    }
    int32_t sum = 0;
    for (int32_t i = 0; i < 256 * 256; i++) {
        const int32_t t = bkt2[i];
        bkt2[i] = sum;
        sum += t;
    }
    for (int32_t k = 0; k < m; k++) {
        const int32_t p = pos_B[k];
        const uint32_t c0 = (uint32_t)T[p];
        const uint32_t c1 = (uint32_t)T[p + 1];
        sorted_B[bkt2[(c0 << 8) | c1]++] = p;
    }
}

/**
 * @brief Sorts type-B positions within each pair bucket by full suffix.
 *
 * For each non-trivial pair bucket of size @c > 1:
 * - Size @c <= IT_RADIX3_THRESHOLD: run @ref it_mkq from depth 2.
 * - Size @c > IT_RADIX3_THRESHOLD: do a third-byte radix sub-bucketing
 *   pass (256 buckets indexed by @c T[p+2]) using @p tmp as scratch,
 *   then run @ref it_mkq from depth 3 on each non-trivial sub-bucket.
 *
 * The third-byte radix replaces ~5 levels of @ref it_mkq partitioning
 * (each mkq partition only 3-ways the bucket) with a single sequential
 * O(G) pass that 256-ways the bucket. For text data with many entries
 * sharing first 2 bytes, this is a large win.
 *
 * @param[in]     T         Input bytes.
 * @param[in]     n         Input length.
 * @param[in]     bkt2      Per-pair bucket end pointers from
 *                          @ref it_bucket_B_by_pair.
 * @param[in,out] sorted_B  Position array; mutated in place.
 * @param[in,out] tmp       Scratch buffer of size @c >= max bucket size.
 */
#define IT_RADIX3_THRESHOLD 64

static void it_sort_within_pairs(const uint8_t* RESTRICT T, const int32_t n,
                                 const int32_t* RESTRICT bkt2, int32_t* RESTRICT sorted_B,
                                 int32_t* RESTRICT tmp) {
    int32_t cnt3[257];
    int32_t prev = 0;
    for (int32_t i = 0; i < 256 * 256; i++) {
        const int32_t end = bkt2[i];
        const int32_t G = end - prev;
        if (G <= 1) {
            prev = end;
            continue;
        }
        if (G <= IT_RADIX3_THRESHOLD) {
            it_mkq(T, n, sorted_B, prev, end, 2);
            prev = end;
            continue;
        }

        /* Third-byte radix. Bin 0 reserved for the virtual sentinel
         * (suffix exhausted at depth 2: p + 2 == n), which sorts before
         * any real byte. Real bytes use bins 1..256. */
        ZXC_MEMSET(cnt3, 0, sizeof(cnt3));
        for (int32_t k = prev; k < end; k++) {
            const int32_t p = sorted_B[k];
            const int bin = (p + 2 < n) ? (int)T[p + 2] + 1 : 0;
            cnt3[bin]++;
        }
        /* Exclusive prefix sum within the bucket-local offset space. */
        int32_t sum = 0;
        for (int c = 0; c < 257; c++) {
            const int32_t t = cnt3[c];
            cnt3[c] = sum;
            sum += t;
        }
        /* Place into tmp and copy back. */
        for (int32_t k = prev; k < end; k++) {
            const int32_t p = sorted_B[k];
            const int bin = (p + 2 < n) ? (int)T[p + 2] + 1 : 0;
            tmp[cnt3[bin]++] = p;
        }
        ZXC_MEMCPY(&sorted_B[prev], tmp, (size_t)G * sizeof(int32_t));

        /* cnt3[c] now points one past the last entry of sub-bucket c
         * within tmp/sorted_B. Sub-bucket c spans
         * [cnt3[c-1], cnt3[c]). Sort each non-trivial sub-bucket from
         * depth 3. The sentinel sub-bucket (c == 0) has at most one
         * entry (only one position p can satisfy p + 2 == n) and is
         * trivially sorted. */
        int32_t sub_prev = 0;
        for (int c = 0; c < 257; c++) {
            const int32_t sub_end = cnt3[c];
            if (c > 0 && sub_end - sub_prev > 1) {
                it_mkq(T, n, sorted_B, prev + sub_prev, prev + sub_end, 3);
            }
            sub_prev = sub_end;
        }
        prev = end;
    }
}

/**
 * @brief Computes per-character bucket end pointers in SA.
 *
 * SA is laid out so that @c SA[0] is reserved for the sentinel suffix
 * at position @c n, and @c SA[1..n+1) holds real suffixes partitioned
 * by their first character. For each character @c c, the bucket spans
 * @c [bkt_start[c], bkt_end[c]) in SA.
 *
 * @param[in]  T         Input bytes, size @p n.
 * @param[in]  n         Input length.
 * @param[out] bkt_start Bucket-start pointers, size @c 256.
 * @param[out] bkt_end   Bucket-end (exclusive) pointers, size @c 256.
 */
static void it_compute_buckets(const uint8_t* RESTRICT T, const int32_t n,
                               int32_t* RESTRICT bkt_start, int32_t* RESTRICT bkt_end) {
    int32_t count[256];
    ZXC_MEMSET(count, 0, sizeof(count));
    for (int32_t i = 0; i < n; i++) count[T[i]]++;
    int32_t s = 1;
    for (int c = 0; c < 256; c++) {
        bkt_start[c] = s;
        s += count[c];
        bkt_end[c] = s;
    }
}

/**
 * @brief Places sorted type-B positions at bucket-end slots of SA.
 *
 * Iterates @p sorted_B_pos in reverse lex order; for each type-B
 * position @c p, writes @c SA[--bkt_end_w[T[p]]] = p. After this, every
 * type-B suffix sits at its final SA slot in the bucket-end region.
 * The mutated @p bkt_end_w pointers are scratch and not reused.
 *
 * @param[in]     T              Input bytes, size @p n.
 * @param[in]     sorted_B_pos   Type-B positions in ascending lex order,
 *                               size @p m.
 * @param[in]     m              Number of type-B positions.
 * @param[in,out] SA             Suffix array, size @c n+1.
 * @param[in,out] bkt_end_w      Working copy of bucket-end pointers,
 *                               size @c 256; mutated.
 */
static void it_place_B(const uint8_t* RESTRICT T, const int32_t* RESTRICT sorted_B_pos,
                       const int32_t m, int32_t* RESTRICT SA, int32_t* RESTRICT bkt_end_w) {
    for (int32_t k = m - 1; k >= 0; k--) {
        const int32_t p = sorted_B_pos[k];
        SA[--bkt_end_w[T[p]]] = p;
    }
}

/**
 * @brief Induce-sorts type-A suffixes by a single left-to-right scan.
 *
 * Scans @p SA from @c k=0 to @c n. For each entry @c j = SA[k] with
 * @c j > 0 and @c is_B[j-1] == 0 (predecessor is type A), writes
 * @c SA[bkt_start_w[T[j-1]]++] = j-1. The @c j == 0 case is skipped
 * (no predecessor) and @c j == n (the sentinel) has predecessor
 * @c j-1 = n-1 which is always type A.
 *
 * The mutated @p bkt_start_w pointers are scratch and not reused.
 *
 * @param[in]     T            Input bytes, size @p n.
 * @param[in]     n            Input length.
 * @param[in]     is_B         Type bitmap from @ref it_classify.
 * @param[in,out] SA           Suffix array, size @c n+1.
 * @param[in,out] bkt_start_w  Working copy of bucket-start pointers,
 *                             size @c 256; mutated.
 */
static void it_induce_A(const uint8_t* RESTRICT T, const int32_t n, const uint8_t* RESTRICT is_B,
                        int32_t* RESTRICT SA, int32_t* RESTRICT bkt_start_w) {
    for (int32_t k = 0; k <= n; k++) {
        const int32_t j = SA[k];
        if (j > 0 && !is_B[j - 1]) {
            SA[bkt_start_w[T[j - 1]]++] = j - 1;
        }
    }
}

/**
 * @brief End-to-end Itoh-Tanaka suffix-array builder.
 *
 * Allocates per-call scratch (is_B, pos_B, sorted_B, bkt2), runs the
 * five-phase pipeline (classify → bucket → sort → place → induce A),
 * and writes the final sorted suffix array into @p SA.
 *
 * @p SA must point to a buffer of @c n+1 entries; @c SA[0] is reserved
 * for the sentinel and stamped before the induce pass. @c SA[1..n+1)
 * is filled with the sorted real suffixes.
 *
 * @param[in]  T  Input bytes, size @p n.
 * @param[out] SA Suffix array, size @c n+1. After return: SA[0] = n;
 *                SA[1..n+1) is sorted.
 * @param[in]  n  Input length (must be @c >= 1).
 * @return @c ZXC_OK on success, @c ZXC_ERROR_MEMORY if any allocation
 *         fails (state fully unwound; SA contents undefined).
 */
static int it_build(const uint8_t* RESTRICT T, int32_t* RESTRICT SA, const int32_t n) {
    uint8_t* const is_B = (uint8_t*)malloc((size_t)n);
    int32_t* const pos_B = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    int32_t* const sorted_B = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    int32_t* const bkt2 = (int32_t*)malloc((size_t)(256 * 256) * sizeof(int32_t));
    if (UNLIKELY(!is_B || !pos_B || !sorted_B || !bkt2)) {
        free(is_B);
        free(pos_B);
        free(sorted_B);
        free(bkt2);
        return ZXC_ERROR_MEMORY;
    }

    /* === Phase 1: classify A/B, collect type-B positions === */
    const int32_t m = it_classify(T, n, is_B, pos_B);

    /* === Phase 2: sort type-B positions by full suffix === */
    if (m > 0) {
        /* 2a: bucket by first 2 chars (sorted_B holds positions). */
        it_bucket_B_by_pair(T, n, pos_B, m, bkt2, sorted_B);

        /* 2b: third-byte radix (large buckets) + multi-key quicksort.
         * pos_B is no longer needed past this point, so we reuse it as
         * scratch space for the third-byte radix placement. */
        it_sort_within_pairs(T, n, bkt2, sorted_B, pos_B);
    }

    /* === Phase 3: bucket layout and B placement === */
    int32_t bkt_start[256];
    int32_t bkt_end[256];
    int32_t bkt_w[256];
    it_compute_buckets(T, n, bkt_start, bkt_end);

    /* SA layout: SA[0] = sentinel (n); SA[1..n+1) holds real suffixes.
     * Initialise unfilled slots to -1 so the induce-A scan recognises
     * them and skips (j > 0 check naturally rules out -1). */
    for (int32_t i = 1; i <= n; i++) SA[i] = -1;
    SA[0] = n;

    ZXC_MEMCPY(bkt_w, bkt_end, sizeof(bkt_w));
    it_place_B(T, sorted_B, m, SA, bkt_w);

    /* === Phase 4: induce-sort all type-A suffixes === */
    ZXC_MEMCPY(bkt_w, bkt_start, sizeof(bkt_w));
    it_induce_A(T, n, is_B, SA, bkt_w);

    free(is_B);
    free(pos_B);
    free(sorted_B);
    free(bkt2);
    return ZXC_OK;
}

/**
 * @brief Top-level suffix-array + inverse-SA builder for a byte input.
 *
 * Computes @p SA and @p ISA over @c n+1 positions: the @c n real bytes
 * @p T[0..n-1] plus a virtual sentinel suffix at position @c n that is
 * lexicographically smaller than any real byte and ends up at @c SA[0].
 *
 * On allocation failure inside @ref it_build, the function falls back
 * to the identity permutation so callers never see a corrupt SA — the
 * match finder simply produces no matches and the block degrades to a
 * literals-heavy encoding.
 *
 * @param[in]  T    Input bytes, size @p n.
 * @param[out] SA   Suffix array, size @c n+1. @c SA[0] receives the
 *                  sentinel position @p n; @c SA[1..n+1) holds the
 *                  sorted real positions.
 * @param[out] ISA  Inverse SA, size @c n+1. After return:
 *                  @c ISA[SA[k]] == k for all k in [0, n].
 * @param[in]  n    Input length in bytes.
 * @param[in]  work Unused. Kept for ABI stability with earlier
 *                  mono-arena variants of this function; pass @c NULL.
 */
void zxc_suffix_array_build(const uint8_t* RESTRICT T, int32_t* RESTRICT SA, int32_t* RESTRICT ISA,
                            const int32_t n, int32_t* work) {
    (void)work;
    if (n == 0) {
        SA[0] = 0;
        ISA[0] = 0;
        return;
    }
    if (n == 1) {
        SA[0] = 1;
        SA[1] = 0;
        ISA[0] = 1;
        ISA[1] = 0;
        return;
    }

    if (UNLIKELY(it_build(T, SA, n) != ZXC_OK)) {
        for (int32_t i = 0; i <= n; i++) SA[i] = i;
        for (int32_t i = 0; i <= n; i++) ISA[i] = i;
        return;
    }

    for (int32_t k = 0; k <= n; k++) ISA[SA[k]] = k;
}

/**
 * @brief Builds the inverse suffix array from a sorted SA.
 *
 * Computes @p ISA such that @c ISA[SA[k]] == k for all @c k in [0, n].
 * Callers of @ref zxc_suffix_array_build already get @p ISA filled by
 * that function; this entry point exists for callers that have an SA
 * from a different source and need only the inverse.
 *
 * @param[in]  SA  Sorted suffix array, size @c n+1.
 * @param[out] ISA Inverse SA, size @c n+1.
 * @param[in]  n   Number of real input positions (SA covers @c n+1
 *                 entries including the sentinel at @c SA[0]).
 */
void zxc_isa_build(const int32_t* RESTRICT SA, int32_t* RESTRICT ISA, const int32_t n) {
    for (int32_t k = 0; k <= n; k++) {
        ISA[SA[k]] = k;
    }
}

/**
 * @brief Kasai's linear-time longest-common-prefix builder.
 *
 * Computes @c LCP[k] = LCP(suffix at @c SA[k-1], suffix at @c SA[k]) for
 * @c k in [1, n]. @c LCP[0] is undefined and set to 0 by convention.
 *
 * The sentinel suffix at @c SA[0] = n is excluded from comparisons:
 * @c LCP[1] (between sentinel and the lex-smallest real suffix) is
 * stamped to 0 explicitly at the end.
 *
 * @param[in]  T   Input bytes, size @p n.
 * @param[in]  SA  Sorted suffix array, size @c n+1.
 * @param[in]  ISA Inverse SA, size @c n+1.
 * @param[out] LCP Output LCP array, size @c n+1. @c LCP[0] = 0,
 *                 @c LCP[k] = LCP(SA[k-1], SA[k]) for @c k in [1, n].
 * @param[in]  n   Number of real input positions.
 */
void zxc_lcp_kasai(const uint8_t* RESTRICT T, const int32_t* RESTRICT SA,
                   const int32_t* RESTRICT ISA, int32_t* RESTRICT LCP, const int32_t n) {
    int32_t h = 0;
    LCP[0] = 0;
    for (int32_t i = 0; i < n; i++) {
        int32_t k = ISA[i];
        if (k == 0) continue;
        int32_t j = SA[k - 1];
        if (j == n) {
            LCP[k] = 0;
            if (h > 0) h--;
            continue;
        }
        while (i + h < n && j + h < n && T[i + h] == T[j + h]) h++;
        LCP[k] = h;
        if (h > 0) h--;
    }
    if (UNLIKELY(n > 0)) LCP[1] = 0;
}
