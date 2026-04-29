/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Suffix array construction by prefix doubling (Manber-Myers, 1990) plus
 * Kasai LCP and inverse-SA helpers. Used by the level-6 match finder.
 *
 * Algorithm reference (patent-free, published 1990):
 *   U. Manber, G. Myers, "Suffix arrays: a new method for on-line string
 *   searches", in Proc. SODA 1990. The implementation below uses two-pass
 *   LSD radix sort over (rank[i], rank[i+h]) pairs and re-ranks groups in
 *   place. SA has length n+1 with the virtual sentinel suffix at SA[0]=n.
 */

#include <stdint.h>
#include <string.h>

#include "zxc_internal.h"

/* In-bound rank lookup: returns 0 (sentinel rank) when i > n. */
#define RANK_AT(ISA, i, n) (((i) > (n)) ? 0 : (ISA)[i])

/* Top-level SA construction for byte input length n.
 * SA must be sized n+1; ISA must be sized n+1; work must be sized at
 * least 2*(n+1) int32 (used for radix tmp + count arrays).
 *
 * Output:
 *   SA[0..n]  : positions sorted by suffix lexicographic order; SA[0]=n
 *               is always the (empty) sentinel suffix.
 *   ISA[0..n] : inverse SA — ISA[SA[k]] = k for all k in [0, n].
 */
void zxc_suffix_array_build(const uint8_t* T, int32_t* SA, int32_t* ISA, int32_t n,
                            int32_t* work) {
    if (n == 0) {
        SA[0] = 0;
        ISA[0] = 0;
        return;
    }

    int32_t* const tmp = work;
    int32_t* const count = work + (n + 1);

    /* Step 1: initial rank by T[i]. Sentinel at position n gets rank 0;
     * real bytes at positions 0..n-1 get rank = T[i] + 1, in [1, 256]. */
    ISA[n] = 0;
    for (int32_t i = 0; i < n; i++) ISA[i] = (int32_t)T[i] + 1;

    /* Initial SA: bucket-sort positions 0..n by their initial rank. */
    {
        int32_t bcnt[258];
        memset(bcnt, 0, sizeof(bcnt));
        for (int32_t i = 0; i <= n; i++) bcnt[ISA[i] + 1]++;
        for (int32_t c = 1; c < 258; c++) bcnt[c] += bcnt[c - 1];
        for (int32_t i = 0; i <= n; i++) SA[bcnt[ISA[i]]++] = i;
    }

    /* Re-rank ISA based on group-number (start index of each equal-rank
     * run in SA). This gives ISA[i] in [0, n]. */
    {
        int32_t prev = -1;
        int32_t r = 0;
        for (int32_t i = 0; i <= n; i++) {
            int32_t k = ISA[SA[i]];
            if (k != prev) {
                prev = k;
                r = i;
            }
            tmp[SA[i]] = r;
        }
        for (int32_t i = 0; i <= n; i++) ISA[i] = tmp[i];
    }

    /* Step 2: iterative prefix doubling. At each iteration the SA is
     * sorted by 2*h-prefix; we then double h. Stops as soon as all ranks
     * are unique (every position has a distinct rank). */
    int32_t h = 1;
    int32_t all_unique = 0;
    while (!all_unique) {
        /* Two-pass LSD radix sort of SA by (ISA[SA[i]], RANK_AT(SA[i] + h)).
         * Buckets cover ranks 0..n+1 (n+2 buckets). */
        const int32_t K = n + 2;

        /* Pass 1: stable sort by secondary key RANK_AT(SA[i] + h). */
        memset(count, 0, (size_t)K * sizeof(int32_t));
        for (int32_t i = 0; i <= n; i++) {
            int32_t key = RANK_AT(ISA, SA[i] + h, n);
            count[key + 1]++;
        }
        for (int32_t c = 1; c < K; c++) count[c] += count[c - 1];
        for (int32_t i = 0; i <= n; i++) {
            int32_t key = RANK_AT(ISA, SA[i] + h, n);
            tmp[count[key]++] = SA[i];
        }

        /* Pass 2: stable sort tmp by primary key ISA[tmp[i]]. */
        memset(count, 0, (size_t)K * sizeof(int32_t));
        for (int32_t i = 0; i <= n; i++) count[ISA[tmp[i]] + 1]++;
        for (int32_t c = 1; c < K; c++) count[c] += count[c - 1];
        for (int32_t i = 0; i <= n; i++) {
            int32_t key = ISA[tmp[i]];
            SA[count[key]++] = tmp[i];
        }

        /* Re-rank ISA from sorted SA using paired keys. Track number of
         * distinct rank groups so we can detect termination precisely. */
        {
            int32_t prev_pri = -1, prev_sec = -1;
            int32_t r = -1;
            int32_t distinct = 0;
            for (int32_t i = 0; i <= n; i++) {
                int32_t pri = ISA[SA[i]];
                int32_t sec = RANK_AT(ISA, SA[i] + h, n);
                if (pri != prev_pri || sec != prev_sec) {
                    r = i;
                    distinct++;
                    prev_pri = pri;
                    prev_sec = sec;
                }
                tmp[SA[i]] = r;
            }
            for (int32_t i = 0; i <= n; i++) ISA[i] = tmp[i];
            all_unique = (distinct == n + 1);
        }

        if (all_unique) break;
        if (h > n) break; /* safety: shouldn't happen for well-formed input */
        h *= 2;
    }

    /* Final ISA: rebuild from SA so ISA[SA[k]] = k exactly. */
    for (int32_t k = 0; k <= n; k++) ISA[SA[k]] = k;
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
    if (n > 0) LCP[1] = 0;
}
