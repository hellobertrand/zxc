/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Recipe for the archives under conformance/valid: the options each vector was
 * produced with. Rebuild with zxc_valid_gen; test_conformance.c checks that
 * every committed archive still matches its entry here.
 *
 * The input is the committed <name>.expected, not a generator: this corpus is
 * published for third-party decoders, which need that plaintext as their
 * reference, so using it as the input too leaves exactly one copy of it.
 *
 * Fields are explicit even where they match today's defaults, so a changed
 * default shows up as a corpus diff instead of silently re-cutting vectors.
 */

#ifndef ZXC_VALID_CASES_H
#define ZXC_VALID_CASES_H

#include <stddef.h>

#include "../include/zxc_opts.h"

#define VC_KB(n) ((size_t)(n) * 1024u)
#define VC_MB(n) ((size_t)(n) * 1024u * 1024u)

typedef struct {
    const char* name; /* <name>.expected -> <name>.zxc */
    const char* dict; /* .zxd basename in the same directory, or NULL */
    zxc_compress_opts_t opts;
} valid_case_t;

/* Grouped as in README.md "Vector coverage"; keep the two in step. */
static const valid_case_t VALID_CASES[] = {
    /* One vector per decoder-visible trait. Two of them earn their place on
     * payload shape rather than on any header field, so a coverage count over
     * header traits alone would wrongly call them redundant: all_zeros_4k is
     * the offset-1 overlap run, max_offset_128k the maximum back-reference
     * distance. Both are paths this decoder has had real bugs in. */
    {"empty", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"all_256_values", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"all_zeros_4k", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"max_offset_128k", NULL, {.level = 3, .block_size = VC_KB(512)}},

    /* Block types and literal encodings: the only GHI, the only RLE literals
     * (enc_lit = 1), the only PivCo literals (enc_lit = 2). */
    {"text_64k_level1", NULL, {.level = 1, .block_size = VC_KB(512)}},
    {"glo_rle_4k", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"glo_pivco_wide_l7", NULL, {.level = 7, .block_size = VC_KB(512)}},

    /* Checksums on their own, so a failure here is not confounded with the
     * dictionary vectors below (the only other place they are exercised). */
    {"random_4k_checksum", NULL, {.level = 3, .block_size = VC_KB(512), .checksum_enabled = 1}},

    /* Chunk sizes, block counts, seek table. */
    {"multiblock_mixed", NULL, {.level = 3, .block_size = VC_KB(4)}},
    {"text_64k_bs2m", NULL, {.level = 3, .block_size = VC_MB(2)}},
    {"seekable_4blocks", NULL, {.level = 3, .block_size = VC_KB(8), .seekable = 1}},

    /* Both dictionary modes: without a shared Huffman table (enc_lit = 0) and
     * with one (enc_lit = 3). A decoder can pass one and fail the other.
     * dict_no_checksum is dict_http's input and dictionary with checksums off,
     * so a decoder failing one and not the other has named its own bug. */
    {"dict_http", "dict_http.zxd", {.level = 3, .block_size = VC_KB(512), .checksum_enabled = 1}},
    {"dict_no_checksum", "dict_http.zxd", {.level = 3, .block_size = VC_KB(512)}},
    {"dict_seekable_l7",
     "dict_text.zxd",
     {.level = 7, .block_size = VC_KB(512), .checksum_enabled = 1, .seekable = 1}},
};

#define VALID_CASE_COUNT (sizeof(VALID_CASES) / sizeof(VALID_CASES[0]))

#endif /* ZXC_VALID_CASES_H */
