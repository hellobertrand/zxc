/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Suffix array construction by DC3 / Skew algorithm (Karkkainen-Sanders 2003)
 * plus Kasai LCP and inverse-SA helpers. Used by the level-6 match finder.
 *
 * Algorithm reference (patent-free, published 2003):
 *   J. Karkkainen, P. Sanders, "Simple linear work suffix array
 *   construction", in Proc. ICALP 2003. The implementation below mirrors
 *   the canonical recursive structure with three radix-sort passes per
 *   level. Runs in O(n) and produces SA + ISA over n+1 positions
 *   (real bytes 0..n-1 plus a virtual sentinel at SA[0] = n).
 *
 * Memory: per-recursion scratch (R, SA12, R0, SA0, count) is malloc'd
 * inside dc3_build and freed before return. The top-level call avoids
 * one extra allocation by writing the real SA directly into SA[1..n+1)
 * and stamping the sentinel at SA[0] = n at the end. On allocation
 * failure the function falls back to an identity permutation so callers
 * never crash; the resulting block degrades to a literals-heavy encoding.
 */

#include <stdint.h>
#include <stdlib.h>

#include "../../include/zxc_error.h"
#include "zxc_internal.h"

/**
 * @brief Lexicographic less-or-equal comparison of two 2-tuples.
 *
 * Returns true iff @c (a1,a2) lexically precedes or equals @c (b1,b2).
 * Used by the merge step of DC3 to compare S1 (mod-1) suffixes against
 * S0 (mod-0) suffixes via two-character keys.
 *
 * @param[in] a1 First element of left tuple.
 * @param[in] a2 Second element of left tuple.
 * @param[in] b1 First element of right tuple.
 * @param[in] b2 Second element of right tuple.
 * @return Non-zero if @c (a1,a2) <= @c (b1,b2) lexicographically, else 0.
 */
static ZXC_ALWAYS_INLINE int dc3_leq2(const int32_t a1, const int32_t a2, const int32_t b1,
                                      const int32_t b2) {
    return (a1 < b1) || (a1 == b1 && a2 <= b2);
}

/**
 * @brief Lexicographic less-or-equal comparison of two 3-tuples.
 *
 * Returns true iff @c (a1,a2,a3) lexically precedes or equals @c (b1,b2,b3).
 * Used by the merge step of DC3 to compare S2 (mod-2) suffixes against
 * S0 (mod-0) suffixes via three-character keys.
 *
 * @param[in] a1 First element of left tuple.
 * @param[in] a2 Second element of left tuple.
 * @param[in] a3 Third element of left tuple.
 * @param[in] b1 First element of right tuple.
 * @param[in] b2 Second element of right tuple.
 * @param[in] b3 Third element of right tuple.
 * @return Non-zero if @c (a1,a2,a3) <= @c (b1,b2,b3) lexicographically, else 0.
 */
static ZXC_ALWAYS_INLINE int dc3_leq3(const int32_t a1, const int32_t a2, const int32_t a3,
                                      const int32_t b1, const int32_t b2, const int32_t b3) {
    return (a1 < b1) || (a1 == b1 && dc3_leq2(a2, a3, b2, b3));
}

/**
 * @brief Stable LSD radix-sort pass over a permutation array.
 *
 * Sorts @c a[0..n-1] into @c b[0..n-1] by the key @c r[a[i]] using a
 * single counting-sort pass with @c K+1 buckets. The pass is stable, so
 * three successive calls with shifted key arrays @c (T+2, T+1, T) form
 * a complete LSD radix sort over triples.
 *
 * The @c a, @c b, @c r and @c count buffers are disjoint at every call
 * site and are marked @c RESTRICT to enable aggressive vectorisation.
 *
 * @param[in]  a     Source permutation, size @p n.
 * @param[out] b     Destination permutation, size @p n.
 * @param[in]  r     Key lookup array indexed by elements of @p a.
 * @param[in]  n     Number of elements to sort.
 * @param[in]  K     Maximum key value (buckets cover [0, K]).
 * @param[out] count Scratch bucket-count array, size @c K+1.
 */
static void dc3_radix_pass(const int32_t* RESTRICT a, int32_t* RESTRICT b,
                           const int32_t* RESTRICT r, const int32_t n, const int32_t K,
                           int32_t* RESTRICT count) {
    ZXC_MEMSET(count, 0, (size_t)(K + 1) * sizeof(int32_t));
    for (int32_t i = 0; i < n; i++) count[r[a[i]]]++;
    int32_t sum = 0;
    for (int32_t i = 0; i <= K; i++) {
        int32_t t = count[i];
        count[i] = sum;
        sum += t;
    }
    for (int32_t i = 0; i < n; i++) b[count[r[a[i]]]++] = a[i];
}

/**
 * @brief Recursive DC3 / Skew suffix-array builder for an integer alphabet.
 *
 * Builds @p SA[0..n-1] in lexicographic order of the suffixes of
 * @p T[0..n-1]. The three zero sentinels at @c T[n..n+2] are required
 * by the algorithm so that triple comparisons read past the input safely.
 *
 * The recursion follows Karkkainen-Sanders 2003: rank S12 (mod-1 and
 * mod-2) suffixes via three radix passes on triples, recurse on the
 * rank string when triples are not unique, then sort S0 from the
 * recursive ranks and merge S0 with S12.
 *
 * @p T and @p SA must point to disjoint buffers. Recursive calls pass
 * freshly-malloc'd R (input) and SA12 (output) buffers, so the disjoint
 * invariant always holds. Per-recursion scratch (R, SA12, R0, SA0,
 * count) is malloc'd at entry and freed before return.
 *
 * @param[in]  T  Input string of length @p n with 3 zero sentinels at
 *                positions n, n+1, n+2.
 * @param[out] SA Sorted suffix array of size @p n.
 * @param[in]  n  Input length.
 * @param[in]  K  Alphabet upper bound (max value present in @p T).
 * @return @c ZXC_OK on success, or @c ZXC_ERROR_MEMORY if any internal
 *         malloc fails (state is fully unwound and freed).
 */
static int dc3_build(const int32_t* RESTRICT T, int32_t* RESTRICT SA, const int32_t n,
                     const int32_t K) {
    if (n == 0) return ZXC_OK;
    if (n == 1) {
        SA[0] = 0;
        return ZXC_OK;
    }
    if (n == 2) {
        if (dc3_leq2(T[0], T[1], T[1], T[2])) {
            SA[0] = 0;
            SA[1] = 1;
        } else {
            SA[0] = 1;
            SA[1] = 0;
        }
        return ZXC_OK;
    }

    const int32_t n0 = (n + 2) / 3;
    const int32_t n1 = (n + 1) / 3;
    const int32_t n2 = n / 3;
    const int32_t n02 = n0 + n2;

    int32_t* const R = (int32_t*)malloc((size_t)(n02 + 3) * sizeof(int32_t));
    int32_t* const SA12 = (int32_t*)malloc((size_t)(n02 + 3) * sizeof(int32_t));
    int32_t* const R0 = (int32_t*)malloc((size_t)n0 * sizeof(int32_t));
    int32_t* const SA0 = (int32_t*)malloc((size_t)n0 * sizeof(int32_t));
    int32_t* const count = (int32_t*)malloc((size_t)((K > n02 ? K : n02) + 1) * sizeof(int32_t));
    if (UNLIKELY(!R || !SA12 || !R0 || !SA0 || !count)) {
        free(R);
        free(SA12);
        free(R0);
        free(SA0);
        free(count);
        return ZXC_ERROR_MEMORY;
    }
    R[n02] = R[n02 + 1] = R[n02 + 2] = 0;

    /* === Step 1: rank S12 substrings ============================== */

    /* Generate positions of mod-1 and mod-2 suffixes. The (n0 - n1)
     * adjustment pads with one virtual position when n % 3 == 1 so
     * |S12| == n02 always. */
    int32_t j = 0;
    for (int32_t i = 0; i < n + (n0 - n1); i++) {
        if (i % 3 != 0) R[j++] = i;
    }

    /* LSD radix sort triples T[i..i+2] in three passes. */
    dc3_radix_pass(R, SA12, T + 2, n02, K, count);
    dc3_radix_pass(SA12, R, T + 1, n02, K, count);
    dc3_radix_pass(R, SA12, T, n02, K, count);

    /* Find lexicographic names of triples. */
    int32_t name = 0, c0 = -1, c1 = -1, c2 = -1;
    for (int32_t i = 0; i < n02; i++) {
        if (T[SA12[i]] != c0 || T[SA12[i] + 1] != c1 || T[SA12[i] + 2] != c2) {
            name++;
            c0 = T[SA12[i]];
            c1 = T[SA12[i] + 1];
            c2 = T[SA12[i] + 2];
        }
        if (SA12[i] % 3 == 1) {
            R[SA12[i] / 3] = name; /* S1 indices [0, n1) */
        } else {
            R[SA12[i] / 3 + n0] = name; /* S2 indices [n0, n0 + n2) */
        }
    }

    /* Recurse if names are not unique. */
    if (name < n02) {
        const int rc = dc3_build(R, SA12, n02, name);
        if (UNLIKELY(rc != ZXC_OK)) {
            free(R);
            free(SA12);
            free(R0);
            free(SA0);
            free(count);
            return rc;
        }
        /* Store the unique names back in R using SA12. */
        for (int32_t i = 0; i < n02; i++) R[SA12[i]] = i + 1;
    } else {
        /* Names already unique: derive SA12 directly from R. */
        for (int32_t i = 0; i < n02; i++) SA12[R[i] - 1] = i;
    }

    /* === Step 2: sort mod-0 suffixes via S12 order ================ */

    /* Pull out positions of mod-0 suffixes from sorted SA12. */
    {
        int32_t p = 0;
        for (int32_t i = 0; i < n02; i++) {
            if (SA12[i] < n0) R0[p++] = 3 * SA12[i];
        }
    }
    /* Stable sort by leading character. */
    dc3_radix_pass(R0, SA0, T, n0, K, count);

    /* === Step 3: merge SA0 and SA12 into SA ======================== */

    /* `t` walks S12 (with offset n0 - n1 padding compensation), `p` walks S0,
     * `k` is the output index. Comparisons use precomputed S12 ranks via R. */
    {
        int32_t p = 0, t = n0 - n1, k = 0;
        while (k < n) {
            const int32_t i = (SA12[t] < n0) ? SA12[t] * 3 + 1 : (SA12[t] - n0) * 3 + 2;
            const int32_t jj = SA0[p];
            int32_t s12_smaller;
            if (SA12[t] < n0) {
                s12_smaller = dc3_leq2(T[i], R[SA12[t] + n0], T[jj], R[jj / 3]);
            } else {
                s12_smaller =
                    dc3_leq3(T[i], T[i + 1], R[SA12[t] - n0 + 1], T[jj], T[jj + 1], R[jj / 3 + n0]);
            }
            if (s12_smaller) {
                SA[k++] = i;
                t++;
                if (t == n02) {
                    /* S12 exhausted; copy remaining S0. */
                    while (p < n0) SA[k++] = SA0[p++];
                }
            } else {
                SA[k++] = jj;
                p++;
                if (p == n0) {
                    /* S0 exhausted; copy remaining S12. */
                    while (t < n02) {
                        SA[k++] = (SA12[t] < n0) ? SA12[t] * 3 + 1 : (SA12[t] - n0) * 3 + 2;
                        t++;
                    }
                }
            }
        }
    }

    free(R);
    free(SA12);
    free(R0);
    free(SA0);
    free(count);
    return ZXC_OK;
}

/**
 * @brief Top-level suffix-array + inverse-SA builder for a byte input.
 *
 * Computes @p SA and @p ISA over @c n+1 positions: the @c n real bytes
 * @p T[0..n-1] plus a virtual sentinel suffix at position @c n that is
 * lexicographically smaller than any real byte and ends up at @c SA[0].
 *
 * Internally:
 * - Bytes are widened to int32 with a +1 shift so 0 stays free as the
 *   strict-min padding value required by DC3.
 * - DC3 writes the real SA directly into @p SA[1..n+1) (no intermediate
 *   buffer); the sentinel slot @p SA[0] is stamped with @c n at the end.
 * - @p ISA is rebuilt from the final @p SA so @c ISA[SA[k]] == k holds.
 *
 * On allocation failure (the T_int malloc here, or the per-recursion
 * scratch inside @c dc3_build), the function falls back to the identity
 * permutation so callers never see a corrupt SA — the match finder
 * simply produces no matches and the block degrades to a literals-heavy
 * encoding.
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
        SA[0] = 1; /* sentinel */
        SA[1] = 0;
        ISA[0] = 1;
        ISA[1] = 0;
        return;
    }

    int32_t* const T_int = (int32_t*)malloc((size_t)(n + 3) * sizeof(int32_t));
    if (UNLIKELY(!T_int)) {
        for (int32_t i = 0; i <= n; i++) SA[i] = i;
        for (int32_t i = 0; i <= n; i++) ISA[i] = i;
        return;
    }

    /* Convert bytes to integer alphabet shifted by +1 so 0 stays free as
     * a strictly-smaller padding value for DC3. */
    for (int32_t i = 0; i < n; i++) T_int[i] = (int32_t)T[i] + 1;
    T_int[n] = T_int[n + 1] = T_int[n + 2] = 0;

    /* Write directly into SA[1..n+1); skip the slot reserved for the
     * sentinel which we stamp afterwards. */
    if (UNLIKELY(dc3_build(T_int, SA + 1, n, 256) != ZXC_OK)) {
        free(T_int);
        for (int32_t i = 0; i <= n; i++) SA[i] = i;
        for (int32_t i = 0; i <= n; i++) ISA[i] = i;
        return;
    }

    SA[0] = n; /* sentinel suffix at SA[0] */

    /* Build ISA from final SA: ISA[SA[k]] = k for all k in [0, n]. */
    for (int32_t k = 0; k <= n; k++) ISA[SA[k]] = k;

    free(T_int);
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
 * The classic Kasai algorithm walks the input by suffix-position order
 * (not SA order) using @p ISA to locate the right neighbour, and
 * amortises comparisons via the @c h-1 lower bound after each shift.
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
            /* Previous suffix is the sentinel: LCP is 0. */
            LCP[k] = 0;
            if (h > 0) h--;
            continue;
        }
        while (i + h < n && j + h < n && T[i + h] == T[j + h]) h++;
        LCP[k] = h;
        if (h > 0) h--;
    }
    /* Sentinel suffix at SA[0]: LCP[0]=0 already; LCP[1] for first real
     * suffix: its predecessor in SA is the sentinel, so LCP is 0. */
    if (UNLIKELY(n > 0)) LCP[1] = 0;
}
