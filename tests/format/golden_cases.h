/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Shared description of the golden-file conformance corpus.
 *
 * This header is the single source of truth for the byte-frozen golden files
 * under tests/format/golden/. It is included by BOTH:
 *
 *   - gen_golden.c   -> (re)produces every golden/<name>.zxc deterministically
 *   - test_golden.c  -> parses the committed bytes and validates every field
 *                       documented in docs/FORMAT.md against the expectations
 *                       declared here.
 *
 * Because the same input-generation functions and the same compression
 * options live here, the validator can regenerate each plaintext input on the
 * fly and confirm a byte-exact decompression round-trip without committing any
 * separate ".expected" blob.
 *
 * All input generators use a fixed, portable LCG (no rand(), no libc state) so
 * regeneration is deterministic across platforms and compilers. The golden
 * files themselves are static committed artifacts: CI never re-compresses them,
 * it only parses and hashes the frozen bytes.
 */

#ifndef ZXC_GOLDEN_CASES_H
#define ZXC_GOLDEN_CASES_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_buffer.h"

/* Block-type ids, mirrored from docs/FORMAT.md Sec 4.1 so this header stays
 * decoupled from the private src/lib/zxc_internal.h enum. */
#define GC_BLOCK_RAW 0U
#define GC_BLOCK_GLO 1U
#define GC_BLOCK_GHI 2U
#define GC_BLOCK_SEK 254U
#define GC_BLOCK_EOF 255U

/* Sentinel for "do not assert a specific data-block type". */
#define GC_ANY_TYPE 0xFEU

/* ------------------------------------------------------------------------- */
/* Deterministic input generators                                            */
/* ------------------------------------------------------------------------- */

/* Portable LCG (glibc rand() constants), kept local so output never depends on
 * the platform libc PRNG. */
static uint32_t gc_lcg_next(uint32_t *s) {
    *s = (*s * 1103515245U) + 12345U;
    return *s;
}

/* Empty input: exercises an archive with only an EOF block + footer. */
static size_t gc_make_empty(uint8_t **out) {
    *out = NULL;
    return 0;
}

/* Incompressible high-entropy bytes -> forces a RAW block (GHI/GLO expand). */
static size_t gc_make_raw(uint8_t **out) {
    const size_t n = 4096;
    uint8_t *b = (uint8_t *)malloc(n);
    uint32_t s = 0x1234567u;
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(gc_lcg_next(&s) >> 24);
    *out = b;
    return n;
}

/* Compressible English-like text with plenty of repeated substrings. */
static size_t gc_fill_text(uint8_t *b, size_t n) {
    static const char phrase[] =
        "the quick brown fox jumps over the lazy dog. ZXC compresses repeated "
        "patterns efficiently and decompresses them very fast. ";
    const size_t plen = sizeof(phrase) - 1;
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)phrase[i % plen];
    return n;
}

static size_t gc_make_text(uint8_t **out) {
    const size_t n = 8192;
    uint8_t *b = (uint8_t *)malloc(n);
    gc_fill_text(b, n);
    *out = b;
    return n;
}

/* Skewed, poorly-matching literal stream: after LZ parsing this leaves a large
 * number of literals with a non-uniform symbol distribution, which is what
 * triggers the Huffman literal section (enc_lit == 2) at level 6. The values
 * are shuffled by the LCG so the LZ matcher cannot collapse them into long
 * matches, yet only a small skewed alphabet is used. */
static size_t gc_make_huffman(uint8_t **out) {
    const size_t n = 16384;
    uint8_t *b = (uint8_t *)malloc(n);
    /* Heavily skewed 8-symbol alphabet (entropy ~2 bits/symbol). */
    static const uint8_t alpha[16] = {'a', 'a', 'a', 'a', 'a', 'a', 'b', 'b',
                                       'b', 'b', 'c', 'c', 'd', 'e', 'f', 'g'};
    uint32_t s = 0x0BADF00Du;
    for (size_t i = 0; i < n; i++) b[i] = alpha[(gc_lcg_next(&s) >> 16) & 0x0F];
    *out = b;
    return n;
}

/* Several block_size-worth of text -> multiple data blocks in one archive. */
static size_t gc_make_multiblock(uint8_t **out) {
    const size_t n = 5 * 4096 + 777; /* 5 full 4 KB blocks + a short tail */
    uint8_t *b = (uint8_t *)malloc(n);
    gc_fill_text(b, n);
    *out = b;
    return n;
}

/* ------------------------------------------------------------------------- */
/* Case table                                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char *name;               /* basename, no extension */
    size_t (*make_input)(uint8_t **out);
    zxc_compress_opts_t opts;       /* level / block_size / checksum / seekable */
    uint8_t expect_data_type;       /* every data block must equal this, or GC_ANY_TYPE */
    int expect_huffman_literals;    /* GLO block must use enc_lit == 2 */
    int min_data_blocks;            /* lower bound on data-block count */
    int expect_seek;                /* a SEK block must be present */
} golden_case_t;

/* The corpus. Each entry maps onto one or more sections of docs/FORMAT.md Sec 5. */
static const golden_case_t GOLDEN_CASES[] = {
    /* name                 input              {level, blk,   csum, seek}                       data type      huf min seek */
    { "01_empty_eof_only",     gc_make_empty,     { .level = 1 },                                  GC_ANY_TYPE,   0,  0, 0 },
    { "02_block_raw",          gc_make_raw,       { .level = 1 },                                  GC_BLOCK_RAW,  0,  1, 0 },
    { "03_block_ghi",          gc_make_text,      { .level = 1 },                                  GC_BLOCK_GHI,  0,  1, 0 },
    { "04_block_glo",          gc_make_text,      { .level = 3 },                                  GC_BLOCK_GLO,  0,  1, 0 },
    { "05_block_glo_huffman",  gc_make_huffman,   { .level = 6 },                                  GC_BLOCK_GLO,  1,  1, 0 },
    { "06_checksum_per_block", gc_make_text,      { .level = 3, .checksum_enabled = 1 },           GC_BLOCK_GLO,  0,  1, 0 },
    { "07_multiple_blocks",    gc_make_multiblock,{ .level = 3, .block_size = 4096, .checksum_enabled = 1 }, GC_BLOCK_GLO, 0, 5, 0 },
    { "08_seekable_table",     gc_make_multiblock,{ .level = 3, .block_size = 4096, .checksum_enabled = 1, .seekable = 1 }, GC_BLOCK_GLO, 0, 5, 1 },
};

#define GOLDEN_CASE_COUNT (sizeof(GOLDEN_CASES) / sizeof(GOLDEN_CASES[0]))

#endif /* ZXC_GOLDEN_CASES_H */
