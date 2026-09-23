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

/* Expected rejection reason, per vector.
 *
 * Asserting only "was rejected" is not enough: across the v6 and v7 bumps every
 * vector started failing on the version byte instead of its own defect, and the
 * suite kept printing PASS. Pinning the code is what makes that visible. A
 * vector missing from this table is a failure. */
typedef struct {
    const char* name; /* file stem, without the .zxc */
    int expected;     /* zxc_error_t the decoder must return */
    const char* dict; /* .zxd to offer, a basename in valid/, or NULL */
    int via_seekable; /* defect only visible to the seekable reader: the block's access must refuse
                       */
    int generated;    /* built by build_invalid(); the rest are static files */
} invalid_expect_t;

static const invalid_expect_t INVALID_EXPECT[] = {
    {"all_0xff_garbage", ZXC_ERROR_BAD_MAGIC},
    {"bad_block_checksum", ZXC_ERROR_BAD_CHECKSUM, .generated = 1},
    {"bad_block_size_field", ZXC_ERROR_BAD_BLOCK_SIZE, .generated = 1},
    {"bad_block_type", ZXC_ERROR_BAD_BLOCK_TYPE, .generated = 1},
    {"bad_checksum_algo", ZXC_ERROR_BAD_HEADER, .generated = 1},
    {"bad_enc_lit", ZXC_ERROR_CORRUPT_DATA, .generated = 1},
    {"bad_eof_compsize", ZXC_ERROR_BAD_HEADER, .generated = 1},
    {"bad_header_checksum", ZXC_ERROR_BAD_HEADER, .generated = 1},
    {"bad_magic", ZXC_ERROR_BAD_MAGIC},
    {"bad_version", ZXC_ERROR_BAD_VERSION},
    {"corrupt_payload", ZXC_ERROR_BAD_CHECKSUM, .generated = 1},
    {"dict_required", ZXC_ERROR_DICT_REQUIRED, .generated = 1},
    {"ghi_forged_offset", ZXC_ERROR_BAD_OFFSET, .generated = 1},
    {"glo_forged_enc_off", ZXC_ERROR_CORRUPT_DATA, .generated = 1},
    {"glo_insufficient_slack", ZXC_ERROR_CORRUPT_DATA, .generated = 1},
    {"magic_then_zeros", ZXC_ERROR_BAD_VERSION},
    {"too_short_4bytes", ZXC_ERROR_SRC_TOO_SMALL},
    {"truncated_header_only", ZXC_ERROR_SRC_TOO_SMALL, .generated = 1},
    {"truncated_mid_block", ZXC_ERROR_SRC_TOO_SMALL, .generated = 1},
    {"zero_length", ZXC_ERROR_SRC_TOO_SMALL},
    {"sek_forged_entry", 0, NULL, 1, .generated = 1},
    {"sek_flag_no_table", ZXC_ERROR_CORRUPT_DATA, .generated = 1},
    {"sek_table_no_flag", ZXC_ERROR_CORRUPT_DATA, .generated = 1},
    {"bad_block_header_checksum", ZXC_ERROR_BAD_HEADER, .generated = 1},
    {"bad_footer_size", ZXC_ERROR_CORRUPT_DATA, .generated = 1},
    {"bad_footer_digest", ZXC_ERROR_BAD_CHECKSUM, .generated = 1},
    {"glo_forged_offset", ZXC_ERROR_BAD_OFFSET, .generated = 1},
    {"glo_output_overflow", ZXC_ERROR_OVERFLOW, .generated = 1},
    {"varint_too_long", ZXC_ERROR_CORRUPT_DATA, .generated = 1},
    {"dict_id_mismatch", ZXC_ERROR_DICT_MISMATCH, "dict_http.zxd", 0, .generated = 1},
};
#define INVALID_EXPECT_COUNT (sizeof INVALID_EXPECT / sizeof INVALID_EXPECT[0])

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
    if (L->extras + 8 > PAY0 + comp || L->extras + 8 > n) return 0;
    for (uint32_t i = 0; i < n_seq; i++) {
        const uint8_t tok = d[L->tok + i];
        if ((tok >> 4) == 15u || (tok & 15u) == 15u) return 1;
    }
    return 0;
}

/* The four well-formed archives every vector is patched from. Built once. */
typedef struct {
    uint8_t *plain, *chk, *ghi, *seek;
    size_t n_plain, n_chk, n_ghi, n_seek;
    int state; /* 0 unbuilt, 1 ready, -1 failed (do not retry, do not leak) */
} invalid_bases_t;

static void invalid_bases_free(invalid_bases_t* b);

static int invalid_bases(invalid_bases_t* b) {
    if (b->state) return b->state > 0;

    uint8_t* text = NULL;
    const size_t text_n = make_text(&text);
    if (!text_n) {
        b->state = -1;
        return 0;
    }

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

    if (!b->n_plain || !b->n_chk || !b->n_ghi || !b->n_seek) {
        invalid_bases_free(b);
        b->state = -1;
        return 0;
    }

    /* Every GLO patch below addresses a fixed offset inside block 0's sub-header.
     * If a heuristic change ever made that block RAW, the patches would land on
     * literal bytes and the vectors would quietly stop testing anything. */
    if (b->plain[BLK0] != ZXC_BLOCK_GLO || b->chk[BLK0] != ZXC_BLOCK_GLO) {
        fprintf(stderr, "  block 0 is type %u/%u, expected GLO - vectors would test nothing\n",
                b->plain[BLK0], b->chk[BLK0]);
        invalid_bases_free(b);
        b->state = -1;
        return 0;
    }
    b->state = 1;
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
        !strcmp(name, "bad_footer_digest")) {
        n = b->n_chk;
        src = b->chk;
    } else if (!strcmp(name, "ghi_forged_offset")) {
        n = b->n_ghi;
        src = b->ghi;
    } else if (!strcmp(name, "sek_forged_entry") || !strcmp(name, "sek_table_no_flag")) {
        n = b->n_seek;
        src = b->seek;
    }

    if (n < ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE) {
        fprintf(stderr, "  base archive for '%s' is only %zu bytes\n", name, n);
        return 0;
    }

    uint8_t* d = (uint8_t*)malloc(n);
    if (!d) return 0;
    memcpy(d, src, n);
    size_t len = n;
    int ok = 1;

    /* --- File-header defects (checksum re-signed, except where it IS the defect) */
    if (!strcmp(name, "bad_block_size_field")) {
        d[5] = 31; /* block-size code outside [12,21] */
        zxc_file_header_sign(d);
    } else if (!strcmp(name, "bad_checksum_algo")) {
        d[6] = (uint8_t)((d[6] & 0xF0U) | 0x0FU); /* checksum algorithm id != 0 */
        zxc_file_header_sign(d);
    } else if (!strcmp(name, "bad_header_checksum")) {
        zxc_file_header_sign(d);
        d[14] ^= 0xFFU; /* the checksum itself is the defect: corrupt it last */
    } else if (!strcmp(name, "dict_required")) {
        d[6] |= 0x40U; /* HAS_DICTIONARY with a non-zero id, but none supplied */
        d[7] = 0xEF;
        d[8] = 0xBE;
        d[9] = 0xAD;
        d[10] = 0xDE;
        zxc_file_header_sign(d);
    } else if (!strcmp(name, "sek_flag_no_table")) {
        /* Sec 3.1: the flag announces a seek table between EOF and the footer.
         * Set on an archive that carries none, the tail no longer parses. */
        d[6] |= ZXC_FILE_FLAG_HAS_SEEK_TABLE;
        zxc_file_header_sign(d);
    } else if (!strcmp(name, "sek_table_no_flag")) {
        /* The mirror case: a real table, but the flag says the footer follows
         * EOF. Reading the table as a footer must fail, never half-succeed. */
        d[6] &= (uint8_t)~ZXC_FILE_FLAG_HAS_SEEK_TABLE;
        zxc_file_header_sign(d);
    } else if (!strcmp(name, "dict_id_mismatch")) {
        /* Same shape, with an id matching no committed .zxd: offered a real
         * dictionary, the decoder must reject the binding rather than decode
         * with the wrong one. A different id keeps it distinct byte for byte. */
        d[6] |= 0x40U;
        d[7] = 0x78;
        d[8] = 0x56;
        d[9] = 0x34;
        d[10] = 0x12;
        zxc_file_header_sign(d);

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
        /* Force the 16-bit offset far behind the output start. seq0 is derived
         * from the wire, so guard the shape: a patch landing in the extras
         * padding would ship a vector that decodes cleanly. */
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
        /* Advisory: a sequential decode never reads the table, so this only
         * surfaces in the seekable reader, when the block is accessed. */
        const size_t eof = find_eof_block(d, len, 0);
        const size_t sek = eof ? eof + ZXC_BLOCK_HEADER_SIZE : 0;
        if (!sek || sek + ZXC_BLOCK_HEADER_SIZE + ZXC_SEEK_ANCHOR_SIZE > len ||
            d[sek] != ZXC_BLOCK_SEK) {
            fprintf(stderr, "  no SEK block found - the seekable base changed shape\n");
            ok = 0;
        } else {
            d[sek + ZXC_BLOCK_HEADER_SIZE] ^= 0xFFU; /* group 0's anchor only */
        }

        /* --- Checksum defects (checksummed base) ---------------------------- */
    } else if (!strcmp(name, "bad_block_checksum")) {
        const size_t comp = zxc_le32(d + BLK0 + 3);
        const size_t at = BLK0 + ZXC_BLOCK_HEADER_SIZE + comp;
        if (at >= len) {
            fprintf(stderr, "  block 0 claims %zu bytes, past the archive\n", comp);
            ok = 0;
        } else {
            d[at] ^= 0xFFU; /* trailing block checksum */
        }
    } else if (!strcmp(name, "corrupt_payload")) {
        /* A raw literal, past tok_comp when present (Sec 5.2): only the checksum catches it. */
        const size_t lit = PAY0 + ZXC_GLO_HEADER_BINARY_SIZE +
                           (d[PAY0 + 9] == ZXC_SECTION_ENCODING_HUFFMAN ? 4U : 0U);
        if (d[BLK0] != ZXC_BLOCK_GLO || d[PAY0 + 8] != ZXC_SECTION_ENCODING_RAW ||
            zxc_le32(d + PAY0 + 4) <= 4 || lit + 4 >= len) {
            fprintf(stderr, "  block 0 is not GLO with raw literals\n");
            ok = 0;
        } else {
            d[lit + 4] ^= 0xFFU; /* a raw literal byte */
        }

        /* --- Truncations ---------------------------------------------------- */
    } else if (!strcmp(name, "truncated_header_only")) {
        len = ZXC_FILE_HEADER_SIZE;
    } else if (!strcmp(name, "truncated_mid_block")) {
        len = PAY0 + 20;

        /* --- Remaining rows of the error table (FORMAT.md Sec 11.1) -------- */
    } else if (!strcmp(name, "bad_block_header_checksum")) {
        d[BLK0 + 7] ^= 0xFFU; /* left wrong: the header checksum is the defect */
    } else if (!strcmp(name, "bad_footer_size")) {
        d[len - ZXC_FILE_FOOTER_SIZE] ^= 0xFFU; /* declared source size */
    } else if (!strcmp(name, "bad_footer_digest")) {
        /* Checksummed base: the digest is the footer's last 8 bytes (Sec 8). */
        d[len - ZXC_FILE_DIGEST_SIZE] ^= 0xFFU;
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
