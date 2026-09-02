/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Recipe for the generated invalid vectors.
 *
 */

#ifndef ZXC_INVALID_CASES_H
#define ZXC_INVALID_CASES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_error.h"
/* Private header: zxc_hash8/zxc_hash16 to re-sign patched headers. */
#include "zxc_internal.h"

/* Offset of the first block header; blocks follow the file header. */
#define BLK0 ZXC_FILE_HEADER_SIZE
/* Offset of the first block's payload (its GLO/GHI sub-header). */
#define PAY0 (BLK0 + ZXC_BLOCK_HEADER_SIZE)

/* Every generated vector, in the order gen_invalid.c reports them. */
static const char* const INVALID_GENERATED[] = {
    "bad_block_size_field", "bad_checksum_algo",      "bad_header_checksum",
    "dict_required",        "dict_id_mismatch",       "bad_block_type",
    "bad_eof_compsize",     "bad_enc_lit",            "glo_forged_enc_off",
    "glo_insufficient_slack", "ghi_forged_offset",    "sek_forged_entry",
    "bad_block_checksum",   "corrupt_payload",        "truncated_header_only",
    "truncated_mid_block",   "bad_block_header_checksum",   "bad_footer_size",
    "bad_footer_hash",       "glo_forged_offset",      "glo_output_overflow",
    "varint_too_long",
};
#define INVALID_GENERATED_COUNT (sizeof(INVALID_GENERATED) / sizeof(INVALID_GENERATED[0]))

/* Re-sign the 16-byte file header after patching any of its fields. */
static void resign_file_header(uint8_t* d) {
    d[14] = 0;
    d[15] = 0;
    const uint16_t sum = zxc_hash16(d);
    d[14] = (uint8_t)(sum & 0xFFU);
    d[15] = (uint8_t)(sum >> 8);
}

/* Re-sign an 8-byte block header at @p b after patching type or comp_size. */
static void resign_block_header(uint8_t* b) {
    uint8_t tmp[ZXC_BLOCK_HEADER_SIZE];
    memcpy(tmp, b, ZXC_BLOCK_HEADER_SIZE);
    tmp[7] = 0;
    b[7] = zxc_hash8(tmp);
}

static size_t find_eof_block(const uint8_t* d, size_t n, int has_checksum) {
    size_t p = BLK0;
    while (p + ZXC_BLOCK_HEADER_SIZE <= n) {
        const uint8_t t = d[p];
        if (t == ZXC_BLOCK_EOF) return p;
        const uint32_t comp = zxc_le32(d + p + 3);
        p += ZXC_BLOCK_HEADER_SIZE + comp + (has_checksum ? ZXC_BLOCK_CHECKSUM_SIZE : 0);
    }
    return 0;
}

/* Compressible, deterministic, and long enough to make a GLO block. */
static size_t make_text(uint8_t** out) {
    static const char unit[] = "the quick brown fox jumps over the lazy dog. ";
    const size_t ulen = sizeof(unit) - 1, reps = 40, n = ulen * reps;
    uint8_t* p = (uint8_t*)malloc(n);
    if (!p) {
        *out = NULL;
        return 0;
    }
    for (size_t i = 0; i < reps; i++) memcpy(p + i * ulen, unit, ulen);
    *out = p;
    return n;
}

/* Compress @p in with @p opts into a fresh buffer. Returns size, 0 on failure. */
static size_t build_base(const uint8_t* in, size_t in_size, const zxc_compress_opts_t* opts,
                         uint8_t** out) {
    const size_t cap = (size_t)zxc_compress_bound(in_size) + 4096;
    uint8_t* buf = (uint8_t*)malloc(cap);
    if (!buf) return 0;
    const int64_t n = zxc_compress(in, in_size, buf, cap, opts);
    if (n <= 0) {
        fprintf(stderr, "  compress failed: %s\n", zxc_error_name((int)n));
        free(buf);
        return 0;
    }
    *out = buf;
    return (size_t)n;
}

/* Total length of a prefix varint, known from its first byte alone (Sec 6). */
static size_t varint_len(uint8_t b0) {
    if (b0 < 0x80U) return 1;
    if (b0 < 0xC0U) return 2;
    if (b0 < 0xE0U) return 3;
    return 4; /* out of spec: no legitimate value needs it */
}

/* Section offsets inside block 0's GLO payload, read off the wire. Returns 0
 * unless the block has the shape the patches assume, so a heuristic change
 * makes the generator complain instead of shipping vectors that test nothing. */
typedef struct {
    size_t tok, off, extras;
} glo_layout_t;

static int glo_layout(const uint8_t* d, size_t n, glo_layout_t* L) {
    if (d[BLK0] != ZXC_BLOCK_GLO) return 0;
    const uint32_t comp = zxc_le32(d + BLK0 + 3);
    const uint32_t n_seq = zxc_le32(d + PAY0);
    const uint32_t n_lit = zxc_le32(d + PAY0 + 4);
    const uint8_t enc_lit = d[PAY0 + 8], enc_tok = d[PAY0 + 9], enc_off = d[PAY0 + 11];
    if (enc_lit != 0 || enc_tok != 0 || n_seq == 0) return 0; /* RAW sections only */
    L->tok = PAY0 + ZXC_GLO_HEADER_BINARY_SIZE + n_lit;
    L->off = L->tok + n_seq;
    L->extras = L->off + (size_t)n_seq * (enc_off ? 1u : 2u);
    /* Both extras varints must be inside the payload for the patches to land. */
    return L->extras + 8 <= PAY0 + comp && L->extras + 8 <= n;
}

/* The four well-formed archives every vector is patched from. Built once. */
typedef struct {
    uint8_t *plain, *chk, *ghi, *seek;
    size_t n_plain, n_chk, n_ghi, n_seek;
    int ready;
} invalid_bases_t;

static int invalid_bases(invalid_bases_t* b) {
    if (b->ready) return 1;

    uint8_t* text = NULL;
    const size_t text_n = make_text(&text);
    if (!text_n) return 0;

    /* Zero-initialised is the documented "safe defaults" form. */
    zxc_compress_opts_t plain = {0};
    plain.level = 3;
    plain.block_size = 4096;
    plain.checksum_enabled = 0;

    zxc_compress_opts_t chk = plain;
    chk.checksum_enabled = 1;

    zxc_compress_opts_t ghi = plain;
    ghi.level = 1; /* levels 1-2 emit GHI blocks */

    zxc_compress_opts_t seek = plain;
    seek.seekable = 1; /* appends the SEK block sek_forged_entry patches */

    b->n_plain = build_base(text, text_n, &plain, &b->plain);
    b->n_chk = build_base(text, text_n, &chk, &b->chk);
    b->n_ghi = build_base(text, text_n, &ghi, &b->ghi);
    b->n_seek = build_base(text, text_n, &seek, &b->seek);
    free(text);

    if (!b->n_plain || !b->n_chk || !b->n_ghi || !b->n_seek) return 0;

    /* Every GLO patch below addresses a fixed offset inside block 0's sub-header.
     * If a heuristic change ever made that block RAW, the patches would land on
     * literal bytes and the vectors would quietly stop testing anything. */
    if (b->plain[BLK0] != ZXC_BLOCK_GLO || b->chk[BLK0] != ZXC_BLOCK_GLO) {
        fprintf(stderr, "  block 0 is type %u/%u, expected GLO - vectors would test nothing\n",
                b->plain[BLK0], b->chk[BLK0]);
        return 0;
    }
    b->ready = 1;
    return 1;
}

/* Build one named vector into a fresh buffer. Returns 0 on failure.
 *
 * Each vector starts from a pristine copy of one base, patches one field, and
 * re-signs whatever checksum covers that field. */
static int build_invalid(invalid_bases_t* b, const char* name, uint8_t** out, size_t* out_n) {
    if (!invalid_bases(b)) return 0;

    size_t n = b->n_plain;
    const uint8_t* src = b->plain;
    if (!strcmp(name, "bad_block_checksum") || !strcmp(name, "corrupt_payload") ||
        !strcmp(name, "bad_footer_hash")) {
        n = b->n_chk;
        src = b->chk;
    } else if (!strcmp(name, "ghi_forged_offset")) {
        n = b->n_ghi;
        src = b->ghi;
    } else if (!strcmp(name, "sek_forged_entry")) {
        n = b->n_seek;
        src = b->seek;
    }

    uint8_t* d = (uint8_t*)malloc(n ? n : 1);
    if (!d) return 0;
    memcpy(d, src, n);
    size_t len = n;
    int ok = 1;

    /* --- File-header defects (checksum re-signed, except where it IS the defect) */
    if (!strcmp(name, "bad_block_size_field")) {
        d[5] = 31; /* block-size code outside [12,21] */
        resign_file_header(d);
    } else if (!strcmp(name, "bad_checksum_algo")) {
        d[6] = (uint8_t)((d[6] & 0xF0U) | 0x0FU); /* checksum algorithm id != 0 */
        resign_file_header(d);
    } else if (!strcmp(name, "bad_header_checksum")) {
        resign_file_header(d);
        d[14] ^= 0xFFU; /* the checksum itself is the defect: corrupt it last */
    } else if (!strcmp(name, "dict_required")) {
        d[6] |= 0x40U; /* HAS_DICTIONARY with a non-zero id, but none supplied */
        d[7] = 0xEF;
        d[8] = 0xBE;
        d[9] = 0xAD;
        d[10] = 0xDE;
        resign_file_header(d);
    } else if (!strcmp(name, "dict_id_mismatch")) {
        /* Same shape, with an id matching no committed .zxd: offered a real
         * dictionary, the decoder must reject the binding rather than decode
         * with the wrong one. A different id keeps it distinct byte for byte. */
        d[6] |= 0x40U;
        d[7] = 0x78;
        d[8] = 0x56;
        d[9] = 0x34;
        d[10] = 0x12;
        resign_file_header(d);

        /* --- Block-header defects (checksum re-signed) ---------------------- */
    } else if (!strcmp(name, "bad_block_type")) {
        d[BLK0] = 3; /* no such block type */
        resign_block_header(d + BLK0);
    } else if (!strcmp(name, "bad_eof_compsize")) {
        const size_t eof = find_eof_block(d, len, 0);
        if (!eof) {
            fprintf(stderr, "  no EOF block found\n");
            ok = 0;
        } else {
            zxc_store_le32(d + eof + 3, 16); /* EOF must carry comp_size == 0 */
            resign_block_header(d + eof);
        }

        /* --- Payload defects (nothing covers the sub-header without checksums) */
    } else if (!strcmp(name, "bad_enc_lit")) {
        d[PAY0 + 8] = 9; /* enc_lit outside {0,1,2,3} */
    } else if (!strcmp(name, "glo_forged_enc_off")) {
        d[PAY0 + 11] = 7; /* enc_off outside {0,1} */
    } else if (!strcmp(name, "glo_insufficient_slack")) {
        /* Leave fewer than ZXC_BLOCK_LIT_SLACK bytes behind the literal section
         * by claiming more literals than the payload can carry behind them. */
        const uint32_t comp = zxc_le32(d + BLK0 + 3);
        const uint32_t want = comp - ZXC_GLO_HEADER_BINARY_SIZE - (ZXC_BLOCK_LIT_SLACK - 1);
        zxc_store_le32(d + PAY0 + 4, want);
    } else if (!strcmp(name, "ghi_forged_offset")) {
        /* GHI sequence word: force the 16-bit offset field to reach far behind
         * the start of the output, which no decoder may follow. seq0 is derived
         * from the wire, so check the block really is GHI, that it has a
         * sequence to forge, and that the word is inside the buffer - otherwise
         * the patch lands in the extras padding and the vector ships decoding
         * cleanly, i.e. testing nothing. */
        const size_t seq0 = PAY0 + ZXC_GHI_HEADER_BINARY_SIZE + zxc_le32(d + PAY0 + 4);
        if (d[BLK0] != ZXC_BLOCK_GHI || zxc_le32(d + PAY0) == 0 || seq0 + 4 > len) {
            fprintf(stderr, "  cannot forge a GHI offset (type %u, n_seq %u, seq0 %zu of %zu)\n",
                    d[BLK0], zxc_le32(d + PAY0), seq0, len);
            ok = 0;
        } else {
            uint32_t w = zxc_le32(d + seq0);
            w = (w & 0xFFFF0000U) | 0xFFFFU; /* max encodable distance */
            zxc_store_le32(d + seq0, w);
        }

        /* --- Seek table (Sec 5.5) ------------------------------------------- */
    } else if (!strcmp(name, "sek_forged_entry")) {
        /* The seek table is advisory: a sequential decode never reads it, so
         * this defect only surfaces through the seekable reader, which
         * validates each entry against the block it claims to describe. */
        const size_t eof = find_eof_block(d, len, 0);
        const size_t sek = eof ? eof + ZXC_BLOCK_HEADER_SIZE : 0;
        if (!sek || sek + ZXC_BLOCK_HEADER_SIZE > len || d[sek] != ZXC_BLOCK_SEK) {
            fprintf(stderr, "  no SEK block found - the seekable base changed shape\n");
            ok = 0;
        } else {
            d[sek + ZXC_BLOCK_HEADER_SIZE] ^= 0xFFU; /* first entry only */
        }

        /* --- Checksum defects (checksummed base) ---------------------------- */
    } else if (!strcmp(name, "bad_block_checksum")) {
        const uint32_t comp = zxc_le32(d + BLK0 + 3);
        d[BLK0 + ZXC_BLOCK_HEADER_SIZE + comp] ^= 0xFFU; /* trailing block checksum */
    } else if (!strcmp(name, "corrupt_payload")) {
        d[PAY0 + ZXC_GLO_HEADER_BINARY_SIZE + 4] ^= 0xFFU; /* a literal byte */

        /* --- Truncations ---------------------------------------------------- */
    } else if (!strcmp(name, "truncated_header_only")) {
        len = ZXC_FILE_HEADER_SIZE;
    } else if (!strcmp(name, "truncated_mid_block")) {
        len = PAY0 + 20;

        /* --- Remaining rows of the error table (FORMAT.md Sec 12) ---------- */
    } else if (!strcmp(name, "bad_block_header_checksum")) {
        d[BLK0 + 7] ^= 0xFFU; /* left wrong: the header checksum is the defect */
    } else if (!strcmp(name, "bad_footer_size")) {
        d[len - ZXC_FILE_FOOTER_SIZE] ^= 0xFFU; /* declared source size */
    } else if (!strcmp(name, "bad_footer_hash")) {
        d[len - ZXC_FILE_FOOTER_SIZE + 8] ^= 0xFFU; /* rolling global hash */
    } else if (!strcmp(name, "glo_forged_offset")) {
        /* GHI has its own vector. The first sequence has only its literal run
         * behind it, so any large offset reaches before the output start. */
        glo_layout_t L;
        if (!glo_layout(d, len, &L)) {
            fprintf(stderr, "  block 0 is not a RAW-section GLO - cannot forge an offset\n");
            ok = 0;
        } else {
            d[L.off] = 0xFFU;
        }
    } else if (!strcmp(name, "glo_output_overflow")) {
        /* Saturated LL reads a varint from extras; this legal 3-byte one
         * (Sec 6's own example, 100003) asks for more than the chunk holds. */
        glo_layout_t L;
        if (!glo_layout(d, len, &L)) {
            fprintf(stderr, "  block 0 is not a RAW-section GLO - cannot forge extras\n");
            ok = 0;
        } else {
            d[L.extras] = 0xC3;
            d[L.extras + 1] = 0x35;
            d[L.extras + 2] = 0x0C;
        }
    } else if (!strcmp(name, "varint_too_long")) {
        /* A first byte >= 0xE0 announces four bytes, out of spec per Sec 6.
         * Patch the ML varint: LL and the offset stay valid, so the read gets
         * this far instead of failing earlier. */
        glo_layout_t L;
        if (!glo_layout(d, len, &L)) {
            fprintf(stderr, "  block 0 is not a RAW-section GLO - cannot forge extras\n");
            ok = 0;
        } else {
            const size_t ml = L.extras + varint_len(d[L.extras]);
            d[ml] = 0xE0;
            d[ml + 1] = 0;
            d[ml + 2] = 0;
            d[ml + 3] = 0;
        }
    } else {
        fprintf(stderr, "  unknown invalid vector '%s'\n", name);
        ok = 0;
    }

    if (!ok) {
        free(d);
        return 0;
    }
    *out = d;
    *out_n = len;
    return 1;
}

static void invalid_bases_free(invalid_bases_t* b) {
    free(b->plain);
    free(b->chk);
    free(b->ghi);
    free(b->seek);
    memset(b, 0, sizeof *b);
}

#endif /* ZXC_INVALID_CASES_H */
