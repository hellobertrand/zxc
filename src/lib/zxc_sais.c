/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Suffix-array construction (SA-IS) plus Kasai LCP and inverse-SA helpers.
 * Used by the level-6 match finder. The implementation follows
 * Nong, Zhang, Chan (2009) "Linear Suffix Array Construction by Almost
 * Pure Induced-Sorting" with the standard sentinel handling: SA has length
 * n+1 where position n is the virtual sentinel suffix (smaller than any
 * real byte).
 */

#include <stdint.h>
#include <string.h>

#include "../../include/zxc_error.h"
#include "zxc_internal.h"

#define SAIS_S_TYPE 1
#define SAIS_L_TYPE 0

/* True iff position i is an LMS (left-most-S) position: t[i]==S and
 * (i==0 with sentinel rule rejects it; we only call this for 1..n). */
#define SAIS_IS_LMS(t, i) ((i) > 0 && (t)[i] == SAIS_S_TYPE && (t)[(i)-1] == SAIS_L_TYPE)

/* Compute bucket end positions: bkt[c] = exclusive index just past the
 * last entry of bucket c (cumulative count). For uint8_t input. */
static void sais_bkt_ends_byte(const uint8_t* T, int32_t n, int32_t K, int32_t* bkt) {
    memset(bkt, 0, (size_t)K * sizeof(int32_t));
    for (int32_t i = 0; i < n; i++) bkt[T[i]]++;
    int32_t sum = 1; /* +1 because sentinel suffix is at SA[0] */
    for (int32_t c = 0; c < K; c++) {
        int32_t tmp = bkt[c];
        sum += tmp;
        bkt[c] = sum;
    }
}

/* Compute bucket start positions: bkt[c] = first index of bucket c. */
static void sais_bkt_starts_byte(const uint8_t* T, int32_t n, int32_t K, int32_t* bkt) {
    memset(bkt, 0, (size_t)K * sizeof(int32_t));
    for (int32_t i = 0; i < n; i++) bkt[T[i]]++;
    int32_t sum = 1; /* +1 for sentinel at SA[0] */
    for (int32_t c = 0; c < K; c++) {
        int32_t tmp = bkt[c];
        bkt[c] = sum;
        sum += tmp;
    }
}

/* Same as above but for int32_t input (used by recursion). */
static void sais_bkt_ends_int(const int32_t* T, int32_t n, int32_t K, int32_t* bkt) {
    memset(bkt, 0, (size_t)K * sizeof(int32_t));
    for (int32_t i = 0; i < n; i++) bkt[T[i]]++;
    int32_t sum = 0;
    for (int32_t c = 0; c < K; c++) {
        int32_t tmp = bkt[c];
        sum += tmp;
        bkt[c] = sum;
    }
}

static void sais_bkt_starts_int(const int32_t* T, int32_t n, int32_t K, int32_t* bkt) {
    memset(bkt, 0, (size_t)K * sizeof(int32_t));
    for (int32_t i = 0; i < n; i++) bkt[T[i]]++;
    int32_t sum = 0;
    for (int32_t c = 0; c < K; c++) {
        int32_t tmp = bkt[c];
        bkt[c] = sum;
        sum += tmp;
    }
}

/* Induce L-type positions left-to-right. SA already has LMS placed.
 * For each i, if T[SA[i]-1] is L-type, append it at start of its bucket. */
static void sais_induce_L_byte(const uint8_t* T, int32_t* SA, const uint8_t* t, int32_t n,
                               int32_t K, int32_t* bkt) {
    sais_bkt_starts_byte(T, n, K, bkt);
    /* Sentinel (position n) at SA[0]: T[n-1] is always L-type, so place it. */
    int32_t j = (SA[0] > 0) ? SA[0] - 1 : -1;
    if (j >= 0 && t[j] == SAIS_L_TYPE) SA[bkt[T[j]]++] = j;
    for (int32_t i = 1; i <= n; i++) {
        j = SA[i] - 1;
        if (j >= 0 && t[j] == SAIS_L_TYPE) SA[bkt[T[j]]++] = j;
    }
}

static void sais_induce_S_byte(const uint8_t* T, int32_t* SA, const uint8_t* t, int32_t n,
                               int32_t K, int32_t* bkt) {
    sais_bkt_ends_byte(T, n, K, bkt);
    for (int32_t i = n; i >= 1; i--) {
        int32_t j = SA[i] - 1;
        if (j >= 0 && t[j] == SAIS_S_TYPE) SA[--bkt[T[j]]] = j;
    }
}

static void sais_induce_L_int(const int32_t* T, int32_t* SA, const uint8_t* t, int32_t n,
                              int32_t K, int32_t* bkt) {
    sais_bkt_starts_int(T, n, K, bkt);
    for (int32_t i = 0; i < n; i++) {
        int32_t j = SA[i] - 1;
        if (j >= 0 && t[j] == SAIS_L_TYPE) SA[bkt[T[j]]++] = j;
    }
}

static void sais_induce_S_int(const int32_t* T, int32_t* SA, const uint8_t* t, int32_t n,
                              int32_t K, int32_t* bkt) {
    sais_bkt_ends_int(T, n, K, bkt);
    for (int32_t i = n - 1; i >= 0; i--) {
        int32_t j = SA[i] - 1;
        if (j >= 0 && t[j] == SAIS_S_TYPE) SA[--bkt[T[j]]] = j;
    }
}

/* Forward declaration for recursion. */
static void sais_int_main(const int32_t* T, int32_t* SA, int32_t n, int32_t K, int32_t* work);

/* Top-level SA-IS for byte input, length n.
 * SA must be sized n+1. SA[0] receives the sentinel suffix (position n).
 * work must be sized at least n + 256 int32_t (used for bucket array
 * + LMS rank scratch + recursion). */
void zxc_sais_build(const uint8_t* T, int32_t* SA, int32_t n, int32_t* work) {
    if (n == 0) {
        SA[0] = 0;
        return;
    }
    if (n == 1) {
        SA[0] = 1; /* sentinel */
        SA[1] = 0;
        return;
    }

    /* Lay out work buffer: bkt[256], type[n+1] (as int32 for simplicity,
     * we only need 1 bit but reuse int32 alignment). For memory pressure,
     * we use uint8_t for type allocated as a slice of work. */
    int32_t* const bkt = work;
    uint8_t* const t = (uint8_t*)(work + 256);
    /* type uses (n+1) bytes; the rest of work (work + 256 + ((n+1)+3)/4)
     * is available for recursion. */
    const int32_t type_words = ((n + 1) + 3) / 4;
    int32_t* const work_rec = work + 256 + type_words;

    /* Step 1: classify types. Position n is sentinel (S-type). T[n-1] > sentinel ⇒ L-type. */
    t[n] = SAIS_S_TYPE;
    t[n - 1] = SAIS_L_TYPE;
    for (int32_t i = n - 2; i >= 0; i--) {
        if (T[i] < T[i + 1])
            t[i] = SAIS_S_TYPE;
        else if (T[i] > T[i + 1])
            t[i] = SAIS_L_TYPE;
        else
            t[i] = t[i + 1];
    }

    /* Step 2: stage-1 induced sort.
     * Place LMS positions at end of their buckets (in input order), then
     * induce L and S. The result is correct for non-LMS positions and an
     * approximation for LMS positions. */
    sais_bkt_ends_byte(T, n, 256, bkt);
    for (int32_t i = 0; i <= n; i++) SA[i] = -1;
    SA[0] = n; /* sentinel */
    for (int32_t i = 1; i < n; i++) {
        if (SAIS_IS_LMS(t, i)) {
            SA[--bkt[T[i]]] = i;
        }
    }

    sais_induce_L_byte(T, SA, t, n, 256, bkt);
    sais_induce_S_byte(T, SA, t, n, 256, bkt);

    /* Step 3: compact LMS positions to the front of SA, then derive ranks
     * by comparing LMS substrings. Identical LMS substrings get the same
     * rank; this rank sequence is the reduced string. */
    int32_t n1 = 0;
    for (int32_t i = 0; i <= n; i++) {
        if (SAIS_IS_LMS(t, SA[i])) SA[n1++] = SA[i];
    }
    /* Clear the suffix area used for ranks. */
    for (int32_t i = n1; i <= n; i++) SA[i] = -1;

    /* Assign ranks based on LMS substring equality. Names go into
     * SA[n1 .. n] indexed by SA[i]/2 (since LMS positions are non-adjacent,
     * SA[i]/2 are unique). */
    int32_t name = 0;
    int32_t prev = -1;
    for (int32_t i = 0; i < n1; i++) {
        int32_t pos = SA[i];
        int32_t diff = 0;
        if (prev == -1) {
            diff = 1;
        } else {
            for (int32_t d = 0;; d++) {
                int32_t a = pos + d, b = prev + d;
                /* The LMS substring extends until the next LMS boundary. */
                if (a > n || b > n || T[a] != T[b] || t[a] != t[b]) {
                    /* Compare using sentinel rule: position n is smallest. */
                    if (d > 0 && (SAIS_IS_LMS(t, a) || SAIS_IS_LMS(t, b))) {
                        if (a > n || b > n)
                            diff = 1;
                        else if (T[a] != T[b])
                            diff = 1;
                        else if (t[a] != t[b])
                            diff = 1;
                    } else {
                        diff = 1;
                    }
                    break;
                }
                if (d > 0 && (SAIS_IS_LMS(t, a) || SAIS_IS_LMS(t, b))) {
                    /* End of LMS substring without diff. */
                    diff = 0;
                    break;
                }
            }
        }
        if (diff) name++;
        prev = pos;
        SA[n1 + pos / 2] = name - 1;
    }
    /* Pack the ranks into the second half of SA contiguously. */
    int32_t j = n;
    for (int32_t i = n; i >= n1; i--) {
        if (SA[i] >= 0) SA[j--] = SA[i];
    }

    /* Step 4: recurse if we have duplicate names; otherwise the SA of the
     * reduced string is trivially identity. */
    int32_t* const s1 = SA + n - n1 + 1; /* recursive input */
    int32_t* const sa1 = SA;             /* recursive output */
    if (name < n1) {
        sais_int_main(s1, sa1, n1, name, work_rec);
    } else {
        for (int32_t i = 0; i < n1; i++) sa1[s1[i]] = i;
    }

    /* Step 5: stage-2 induced sort with LMS in correct order. */
    /* Recover LMS positions from the original input. */
    j = 0;
    for (int32_t i = 1; i < n; i++) {
        if (SAIS_IS_LMS(t, i)) s1[j++] = i;
    }
    for (int32_t i = 0; i < n1; i++) sa1[i] = s1[sa1[i]];

    /* Reset SA[n1 .. n] = -1, place sorted LMS at bucket ends. */
    for (int32_t i = n1; i <= n; i++) SA[i] = -1;
    sais_bkt_ends_byte(T, n, 256, bkt);
    /* Iterate sorted LMS in reverse so they go to the END of their bucket. */
    for (int32_t i = n1 - 1; i >= 0; i--) {
        int32_t pos = sa1[i];
        sa1[i] = -1;
        SA[--bkt[T[pos]]] = pos;
    }
    /* Sentinel at SA[0]. */
    SA[0] = n;

    sais_induce_L_byte(T, SA, t, n, 256, bkt);
    sais_induce_S_byte(T, SA, t, n, 256, bkt);
}

/* Recursive int-input variant. Same algorithm, no sentinel handling
 * (the input is guaranteed to have a unique smallest character at the
 * end already, by virtue of being a rank sequence). */
static void sais_int_main(const int32_t* T, int32_t* SA, int32_t n, int32_t K, int32_t* work) {
    if (n == 1) {
        SA[0] = 0;
        return;
    }

    int32_t* const bkt = work;
    uint8_t* const t = (uint8_t*)(work + K);
    const int32_t type_words = (n + 3) / 4;
    int32_t* const work_rec = work + K + type_words;

    /* Classify types. */
    t[n - 1] = SAIS_S_TYPE; /* unique smallest char */
    if (n >= 2) {
        t[n - 2] = (T[n - 2] < T[n - 1]) ? SAIS_S_TYPE
                  : (T[n - 2] > T[n - 1]) ? SAIS_L_TYPE
                                          : t[n - 1];
    }
    for (int32_t i = n - 3; i >= 0; i--) {
        if (T[i] < T[i + 1])
            t[i] = SAIS_S_TYPE;
        else if (T[i] > T[i + 1])
            t[i] = SAIS_L_TYPE;
        else
            t[i] = t[i + 1];
    }

    /* Stage 1: place LMS at bucket ends, induce L, induce S. */
    sais_bkt_ends_int(T, n, K, bkt);
    for (int32_t i = 0; i < n; i++) SA[i] = -1;
    for (int32_t i = 1; i < n; i++) {
        if (SAIS_IS_LMS(t, i)) SA[--bkt[T[i]]] = i;
    }

    sais_induce_L_int(T, SA, t, n, K, bkt);
    sais_induce_S_int(T, SA, t, n, K, bkt);

    /* Compact LMS to front, name them. */
    int32_t n1 = 0;
    for (int32_t i = 0; i < n; i++) {
        if (SAIS_IS_LMS(t, SA[i])) SA[n1++] = SA[i];
    }
    for (int32_t i = n1; i < n; i++) SA[i] = -1;

    int32_t name = 0;
    int32_t prev = -1;
    for (int32_t i = 0; i < n1; i++) {
        int32_t pos = SA[i];
        int32_t diff = 0;
        if (prev == -1) {
            diff = 1;
        } else {
            for (int32_t d = 0;; d++) {
                int32_t a = pos + d, b = prev + d;
                if (a >= n || b >= n || T[a] != T[b] || t[a] != t[b]) {
                    diff = 1;
                    break;
                }
                if (d > 0 && (SAIS_IS_LMS(t, a) || SAIS_IS_LMS(t, b))) {
                    diff = 0;
                    break;
                }
            }
        }
        if (diff) name++;
        prev = pos;
        SA[n1 + pos / 2] = name - 1;
    }
    int32_t j = n - 1;
    for (int32_t i = n - 1; i >= n1; i--) {
        if (SA[i] >= 0) SA[j--] = SA[i];
    }

    int32_t* const s1 = SA + n - n1; /* recursive input */
    int32_t* const sa1 = SA;
    if (name < n1) {
        sais_int_main(s1, sa1, n1, name, work_rec);
    } else {
        for (int32_t i = 0; i < n1; i++) sa1[s1[i]] = i;
    }

    /* Stage 2 */
    j = 0;
    for (int32_t i = 1; i < n; i++) {
        if (SAIS_IS_LMS(t, i)) s1[j++] = i;
    }
    for (int32_t i = 0; i < n1; i++) sa1[i] = s1[sa1[i]];

    for (int32_t i = n1; i < n; i++) SA[i] = -1;
    sais_bkt_ends_int(T, n, K, bkt);
    for (int32_t i = n1 - 1; i >= 0; i--) {
        int32_t pos = sa1[i];
        sa1[i] = -1;
        SA[--bkt[T[pos]]] = pos;
    }

    sais_induce_L_int(T, SA, t, n, K, bkt);
    sais_induce_S_int(T, SA, t, n, K, bkt);
}

/* Compute inverse SA: isa[sa[k]] = k for k in [0, n].
 * SA is length n+1 (includes sentinel at SA[0]=n). */
void zxc_isa_build(const int32_t* SA, int32_t* ISA, int32_t n) {
    for (int32_t k = 0; k <= n; k++) {
        ISA[SA[k]] = k;
    }
}

/* Kasai's algorithm: compute LCP[k] = longest common prefix of suffixes
 * SA[k-1] and SA[k]. LCP[0] is undefined (set to 0). O(n) time.
 * Operates on the n real positions (sentinel suffix at SA[0] excluded). */
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
