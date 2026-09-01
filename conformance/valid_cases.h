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
    /* Basic: degenerate sizes, full byte-value coverage. */
    {"empty", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"one_byte", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"all_256_values", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"all_zeros_4k", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"all_zeros_64k", NULL, {.level = 3, .block_size = VC_KB(512)}},

    /* Text: compressible input. */
    {"text_1k", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"text_1k_checksum", NULL, {.level = 3, .block_size = VC_KB(512), .checksum_enabled = 1}},
    {"text_64k", NULL, {.level = 3, .block_size = VC_KB(512)}},

    /* Random: incompressible, must land in RAW blocks. */
    {"random_256", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"random_4k", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"random_4k_checksum", NULL, {.level = 3, .block_size = VC_KB(512), .checksum_enabled = 1}},
    {"random_64k", NULL, {.level = 3, .block_size = VC_KB(512)}},

    /* Match patterns: long, short, maximum offset distance. */
    {"long_match_64k", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"long_match_checksum", NULL, {.level = 3, .block_size = VC_KB(512), .checksum_enabled = 1}},
    {"short_matches_4k", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"max_offset_128k", NULL, {.level = 3, .block_size = VC_KB(512)}},

    /* Levels 1/2 and 3/4 currently emit identical archives on this input; each
     * entry records the level it is named for, not the lowest that matches. */
    {"text_64k_level1", NULL, {.level = 1, .block_size = VC_KB(512)}},
    {"text_64k_level2", NULL, {.level = 2, .block_size = VC_KB(512)}},
    {"text_64k_level3", NULL, {.level = 3, .block_size = VC_KB(512)}},
    {"text_64k_level4", NULL, {.level = 4, .block_size = VC_KB(512)}},
    {"text_64k_level5", NULL, {.level = 5, .block_size = VC_KB(512)}},
    {"text_64k_level6", NULL, {.level = 6, .block_size = VC_KB(512)}},

    /* Level 7 (ULTRA): PivCo literal section, enc_lit = 2. */
    {"glo_pivco_wide_l7", NULL, {.level = 7, .block_size = VC_KB(512)}},

    /* Block-size variants. */
    {"text_8k_bs4k", NULL, {.level = 3, .block_size = VC_KB(4)}},
    {"text_64k_bs2m", NULL, {.level = 3, .block_size = VC_MB(2)}},

    /* Checksums on a compressible payload. */
    {"zeros_4k_checksum", NULL, {.level = 3, .block_size = VC_KB(512), .checksum_enabled = 1}},

    /* Multi-block: 4 KB blocks over a 64 KB input. */
    {"multiblock_text", NULL, {.level = 3, .block_size = VC_KB(4)}},
    {"multiblock_mixed", NULL, {.level = 3, .block_size = VC_KB(4)}},

    /* Seekable: seek table appended. */
    {"seekable_1block", NULL, {.level = 3, .block_size = VC_KB(512), .seekable = 1}},
    {"seekable_4blocks", NULL, {.level = 3, .block_size = VC_KB(8), .seekable = 1}},
    {"seekable_checksum",
     NULL,
     {.level = 3, .block_size = VC_KB(512), .checksum_enabled = 1, .seekable = 1}},

    /* Dictionary: the .zxd carries the content and its Huffman table. */
    {"dict_http", "dict_http.zxd", {.level = 3, .block_size = VC_KB(512), .checksum_enabled = 1}},
    {"dict_seekable_l7",
     "dict_text.zxd",
     {.level = 7, .block_size = VC_KB(512), .checksum_enabled = 1, .seekable = 1}},
};

#define VALID_CASE_COUNT (sizeof(VALID_CASES) / sizeof(VALID_CASES[0]))

#endif /* ZXC_VALID_CASES_H */
