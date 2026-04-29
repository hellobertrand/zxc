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
 */

#include <stdint.h>
#include <stdlib.h>

#include "../../include/zxc_error.h"
#include "zxc_internal.h"

/* leq(a, b): lex compare of (a1,a2) vs (b1,b2) — true iff a <= b. */
static ZXC_ALWAYS_INLINE int dc3_leq2(int32_t a1, int32_t a2, int32_t b1, int32_t b2) {
    return (a1 < b1) || (a1 == b1 && a2 <= b2);
}

/* leq3(a, b): lex compare of (a1,a2,a3) vs (b1,b2,b3) — true iff a <= b. */
static ZXC_ALWAYS_INLINE int dc3_leq3(int32_t a1, int32_t a2, int32_t a3, int32_t b1, int32_t b2,
                                      int32_t b3) {
    return (a1 < b1) || (a1 == b1 && dc3_leq2(a2, a3, b2, b3));
}

/* Stable radix sort: a[0..n-1] -> b[0..n-1] using key r[a[i]].
 * Buckets cover [0, K]. Caller provides scratch buffer count[K+1]. */
static void dc3_radix_pass(const int32_t* a, int32_t* b, const int32_t* r, int32_t n, int32_t K,
                           int32_t* count) {
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

/* Recursive DC3 over an integer-alphabet string T[0..n-1] with three zero
 * sentinels at T[n..n+2]. Output: SA[0..n-1] sorted by suffix lex order.
 * K is the alphabet upper bound. Returns ZXC_OK or ZXC_ERROR_MEMORY. */
static int dc3_build(const int32_t* T, int32_t* SA, int32_t n, int32_t K) {
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

    int32_t* R = (int32_t*)malloc((size_t)(n02 + 3) * sizeof(int32_t));
    int32_t* SA12 = (int32_t*)malloc((size_t)(n02 + 3) * sizeof(int32_t));
    int32_t* R0 = (int32_t*)malloc((size_t)n0 * sizeof(int32_t));
    int32_t* SA0 = (int32_t*)malloc((size_t)n0 * sizeof(int32_t));
    int32_t* count =
        (int32_t*)malloc((size_t)((K > n02 ? K : n02) + 1) * sizeof(int32_t));
    if (UNLIKELY(!R || !SA12 || !R0 || !SA0 || !count)) {
        free(R);
        free(SA12);
        free(R0);
        free(SA0);
        free(count);
        return ZXC_ERROR_MEMORY;
    }
    R[n02] = R[n02 + 1] = R[n02 + 2] = 0;
    SA12[n02] = SA12[n02 + 1] = SA12[n02 + 2] = 0;

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
            const int32_t i =
                (SA12[t] < n0) ? SA12[t] * 3 + 1 : (SA12[t] - n0) * 3 + 2;
            const int32_t jj = SA0[p];
            int32_t s12_smaller;
            if (SA12[t] < n0) {
                s12_smaller = dc3_leq2(T[i], R[SA12[t] + n0], T[jj], R[jj / 3]);
            } else {
                s12_smaller = dc3_leq3(T[i], T[i + 1], R[SA12[t] - n0 + 1], T[jj], T[jj + 1],
                                       R[jj / 3 + n0]);
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

/* Top-level SA construction for byte input length n.
 * SA must be sized n+1; ISA must be sized n+1; work is unused (kept for
 * API consistency with previous implementations).
 *
 * Output:
 *   SA[0..n]  : positions sorted by suffix lexicographic order; SA[0]=n
 *               is always the (empty) sentinel suffix.
 *   ISA[0..n] : inverse SA — ISA[SA[k]] = k for all k in [0, n].
 *
 * Returns ZXC_OK or a negative error on allocation failure (caller may
 * fall back to a hash-chain finder for that block).
 */
void zxc_suffix_array_build(const uint8_t* T, int32_t* SA, int32_t* ISA, int32_t n,
                            int32_t* work) {
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

    /* Convert bytes to integer alphabet shifted by +1 so 0 stays free as
     * a strictly-smaller padding/sentinel value for DC3. Total length is
     * n + 3 to provide the three-element zero pad DC3 expects. */
    int32_t* T_int = (int32_t*)malloc((size_t)(n + 3) * sizeof(int32_t));
    int32_t* SA_real = (int32_t*)malloc((size_t)n * sizeof(int32_t));
    if (UNLIKELY(!T_int || !SA_real)) {
        free(T_int);
        free(SA_real);
        /* Fallback: identity SA so callers don't crash on the OOM path.
         * The match finder produces no useful matches but remains safe. */
        for (int32_t i = 0; i <= n; i++) SA[i] = i;
        for (int32_t i = 0; i <= n; i++) ISA[i] = i;
        return;
    }

    for (int32_t i = 0; i < n; i++) T_int[i] = (int32_t)T[i] + 1;
    T_int[n] = T_int[n + 1] = T_int[n + 2] = 0;

    if (UNLIKELY(dc3_build(T_int, SA_real, n, 256) != ZXC_OK)) {
        free(T_int);
        free(SA_real);
        for (int32_t i = 0; i <= n; i++) SA[i] = i;
        for (int32_t i = 0; i <= n; i++) ISA[i] = i;
        return;
    }

    /* Prepend the sentinel suffix at SA[0] and shift real SA into [1..n]. */
    SA[0] = n;
    for (int32_t i = 0; i < n; i++) SA[i + 1] = SA_real[i];

    /* Build ISA from final SA: ISA[SA[k]] = k for all k in [0, n]. */
    for (int32_t k = 0; k <= n; k++) ISA[SA[k]] = k;

    free(T_int);
    free(SA_real);
}

/* Compute inverse SA: ISA[SA[k]] = k for k in [0, n].
 * Provided for callers that already have a sorted SA (kept for API
 * compatibility; zxc_suffix_array_build leaves ISA correctly populated). */
void zxc_isa_build(const int32_t* SA, int32_t* ISA, int32_t n) {
    for (int32_t k = 0; k <= n; k++) {
        ISA[SA[k]] = k;
    }
}

/* Kasai's algorithm: compute LCP[k] = longest common prefix of suffixes
 * SA[k-1] and SA[k]. LCP[0] is undefined (set to 0). O(n) time.
 * Operates on the n real positions (sentinel suffix at SA[0]=n excluded). */
void zxc_lcp_kasai(const uint8_t* T, const int32_t* SA, const int32_t* ISA, int32_t* LCP,
                   int32_t n) {
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
