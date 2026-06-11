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
#include "../../include/zxc_dict.h"

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

/* A pseudo-random 1 KB block repeated -> matches at distance 1024 (> 256),
 * forcing 16-bit offsets (enc_off == 0). The other GLO/GHI cases use small
 * offsets (enc_off == 1), so this freezes the 16-bit offset path. */
static size_t gc_make_offset16(uint8_t **out) {
    const size_t period = 1024;
    const size_t n = 8 * period;
    uint8_t *b = (uint8_t *)malloc(n);
    uint32_t s = 0x5EED1234u;
    for (size_t i = 0; i < period; i++) b[i] = (uint8_t)(gc_lcg_next(&s) >> 24);
    for (size_t i = period; i < n; i++) b[i] = b[i % period];
    *out = b;
    return n;
}

/* A varying byte followed by a 4-byte run of one value, repeated. The runs are
 * shorter than the LZ minimum match, so they survive LZ as literals; RLE then
 * beats raw literals and the GLO block uses RLE literal encoding (enc_lit == 1).
 * The other GLO cases use raw (0) or Huffman (2) literals. */
static size_t gc_make_rle_literals(uint8_t **out) {
    const size_t n = 16384;
    uint8_t *b = (uint8_t *)malloc(n);
    uint32_t s = 0x1357BD13u;
    for (size_t i = 0; i < n; i += 5) {
        b[i] = (uint8_t)(gc_lcg_next(&s) >> 24);
        for (size_t k = 1; k < 5 && i + k < n; k++) b[i + k] = 0xAA;
    }
    *out = b;
    return n;
}

/* Fixed dictionary content -> stable dict_id. Two distinct dictionary archive
 * flavours exist on the wire, and each gets its own golden case:
 *   - 09_block_dict: RAW IN-MEMORY dictionary (opts.dict only, no shared
 *     Huffman table) -- the buffer-API path. The header dict_id is the
 *     content-only binding: checksum(content). No enc_lit == 3 block can
 *     appear in such an archive.
 *   - 12_glo_huffman_dict: dictionary WITH the shared literal Huffman table
 *     (the .zxd / CLI path, where the table is always present). The header
 *     dict_id binds the (content, table) pair (FORMAT.md Sec 12.3) and blocks
 *     may use enc_lit == 3 (Sec 5.2.2).
 * Both flavours are decoder-reachable and must stay frozen. */
static const uint8_t gc_dict_content[] =
    "GET /api/v1/users/ HTTP/1.1\r\nHost: api.example.com\r\n"
    "Accept: application/json\r\nUser-Agent: zxc-client\r\n";
#define GC_DICT_SIZE (sizeof(gc_dict_content) - 1) /* exclude the trailing NUL */

/* Payload that reuses the dictionary's phrases, so the block is encoded against
 * the dictionary (the file header then carries HAS_DICTIONARY + dict_id). */
static size_t gc_make_dict_payload(uint8_t **out) {
    const size_t n = 4096;
    uint8_t *b = (uint8_t *)malloc(n);
    static const char req[] =
        "GET /api/v1/users/4242/profile HTTP/1.1\r\nHost: api.example.com\r\n"
        "Accept: application/json\r\nUser-Agent: zxc-client\r\n\r\n";
    const size_t plen = sizeof(req) - 1;
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)req[i % plen];
    *out = b;
    return n;
}

/* Dictionary payload with LCG-varied request paths: enough dictionary matches
 * to engage the dict, enough skewed literals (digits, lowercase ids) for the
 * shared dictionary Huffman table (enc_lit == 3, FORMAT.md Sec 5.2.2) to beat
 * both the per-block table (no 128-byte header) and RAW/RLE. */
static size_t gc_make_huffman_dict_payload(uint8_t **out) {
    const size_t cap = 4096;
    uint8_t *b = (uint8_t *)malloc(cap);
    uint32_t st = 0x5EEDCAFEu;
    size_t n = 0;
    while (n + 160 < cap) {
        char line[192];
        int len = snprintf(line, sizeof line,
                           "GET /api/v1/users/%u/profile?session=%08x&page=%u HTTP/1.1\r\n"
                           "Host: api.example.com\r\nAccept: application/json\r\n"
                           "User-Agent: zxc-client\r\n\r\n",
                           gc_lcg_next(&st) % 100000u, gc_lcg_next(&st), gc_lcg_next(&st) % 64u);
        memcpy(b + n, line, (size_t)len);
        n += (size_t)len;
    }
    *out = b;
    return n;
}

/* Shared dictionary Huffman table for the enc_lit == 3 golden case. Trained
 * once, deterministically (the trainer has no randomness or time inputs), from
 * the same fixed dictionary + payload pair used by the case -- both the
 * generator and the validator resolve the exact same 128 bytes. */
static const uint8_t *gc_dict_huf_table(void) {
    static uint8_t huf[128];
    static int init = 0;
    if (!init) {
        uint8_t *payload = NULL;
        size_t n = gc_make_huffman_dict_payload(&payload);
        const void *samples[1];
        size_t sizes[1];
        samples[0] = payload;
        sizes[0] = n;
        if (zxc_train_dict_huf(samples, sizes, 1, gc_dict_content, GC_DICT_SIZE, huf) != ZXC_OK) {
            fprintf(stderr, "FATAL: gc_dict_huf_table training failed\n");
            exit(1);
        }
        free(payload);
        init = 1;
    }
    return huf;
}

/* ------------------------------------------------------------------------- */
/* Case table                                                                */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char *name;               /* basename, no extension */
    size_t (*make_input)(uint8_t **out);
    zxc_compress_opts_t opts;       /* level / block_size / checksum / seekable */
    uint8_t expect_data_type;       /* every data block must equal this, or GC_ANY_TYPE */
    int expect_enc_lit;             /* GLO literal encoding (-1 = no constraint;
                                       2 = HUFFMAN, 3 = HUFFMAN_DICT) */
    int min_data_blocks;            /* lower bound on data-block count */
    int expect_seek;                /* a SEK block must be present */
    int use_dict_huf;               /* attach gc_dict_huf_table() to the opts */
} golden_case_t;

/* The corpus. Each entry maps onto one or more sections of docs/FORMAT.md Sec 5. */
static const golden_case_t GOLDEN_CASES[] = {
    /* name                 input              {level, blk,   csum, seek}                       data type    enc_lit min seek dhuf */
    { "01_empty_eof_only",     gc_make_empty,     { .level = 1 },                                  GC_ANY_TYPE,  -1,  0, 0, 0 },
    { "02_block_raw",          gc_make_raw,       { .level = 1 },                                  GC_BLOCK_RAW, -1,  1, 0, 0 },
    { "03_block_ghi",          gc_make_text,      { .level = 1 },                                  GC_BLOCK_GHI, -1,  1, 0, 0 },
    { "04_block_glo",          gc_make_text,      { .level = 3 },                                  GC_BLOCK_GLO, -1,  1, 0, 0 },
    { "05_block_glo_huffman",  gc_make_huffman,   { .level = 6 },                                  GC_BLOCK_GLO,  2,  1, 0, 0 },
    { "06_checksum_per_block", gc_make_text,      { .level = 3, .checksum_enabled = 1 },           GC_BLOCK_GLO, -1,  1, 0, 0 },
    { "07_multiple_blocks",    gc_make_multiblock,{ .level = 3, .block_size = 4096, .checksum_enabled = 1 }, GC_BLOCK_GLO, -1, 5, 0, 0 },
    { "08_seekable_table",     gc_make_multiblock,{ .level = 3, .block_size = 4096, .checksum_enabled = 1, .seekable = 1 }, GC_BLOCK_GLO, -1, 5, 1, 0 },
    { "09_block_dict",         gc_make_dict_payload, { .level = 3, .dict = gc_dict_content, .dict_size = GC_DICT_SIZE }, GC_BLOCK_GLO, -1, 1, 0, 0 },
    { "10_glo_offset16",       gc_make_offset16,  { .level = 3 },                                  GC_BLOCK_GLO, -1,  1, 0, 0 },
    { "11_glo_rle",            gc_make_rle_literals, { .level = 3 },                                GC_BLOCK_GLO,  1,  1, 0, 0 },
    { "12_glo_huffman_dict",   gc_make_huffman_dict_payload,
      { .level = 6, .dict = gc_dict_content, .dict_size = GC_DICT_SIZE },                          GC_BLOCK_GLO,  3,  1, 0, 1 },
};

#define GOLDEN_CASE_COUNT (sizeof(GOLDEN_CASES) / sizeof(GOLDEN_CASES[0]))

#endif /* ZXC_GOLDEN_CASES_H */
