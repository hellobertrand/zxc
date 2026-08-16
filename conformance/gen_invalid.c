/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Invalid-vector generator.
 *
 * Rebuilds conformance/invalid/<name>.zxc: each one is a well-formed archive of
 * the CURRENT format version with exactly one field corrupted, so a decoder has
 * to reach the condition the vector is named after in order to reject it.
 *
 * Why this tool exists: before it, the vectors were patched by hand and frozen.
 * A format bump then made every one of them fail on the version byte instead of
 * on its own defect, and the suite still printed "correctly rejected" for all of
 * them - coverage silently went to zero across the v6 and v7 bumps. Regenerating
 * from here after a bump keeps each vector testing what its name claims.
 *
 * Usage:
 *   zxc_invalid_gen <output-dir>    # defaults to "conformance/invalid"
 *
 * Any byte that a CRC covers is re-signed after patching, so the vector fails on
 * its own defect rather than on a stale checksum.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include <io.h>
#include <share.h>
#endif

#include "../include/zxc_buffer.h"
#include "../include/zxc_error.h"
/* Private header: zxc_hash8/zxc_hash16 for re-signing patched headers. */
#include "zxc_internal.h"

/* Owner-only (0600) binary write, mirroring tests/format/gen_golden.c. */
static FILE* open_restricted_wb(const char* path) {
#ifdef _MSC_VER
    int fd = -1;
    _sopen_s(&fd, path, _O_CREAT | _O_WRONLY | _O_TRUNC | _O_BINARY, _SH_DENYNO,
             _S_IREAD | _S_IWRITE);
    return fd >= 0 ? _fdopen(fd, "wb") : NULL;
#else
    const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    return fd >= 0 ? fdopen(fd, "wb") : NULL;
#endif
}

static int write_file(const char* dir, const char* name, const uint8_t* data, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.zxc", dir, name);
    FILE* f = open_restricted_wb(path);
    if (!f) {
        fprintf(stderr, "  cannot open %s for writing\n", path);
        return -1;
    }
    if (size && fwrite(data, 1, size, f) != size) {
        fprintf(stderr, "  short write to %s\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    printf("  wrote %-24s %6zu bytes\n", name, size);
    return 0;
}

/* Re-sign the 16-byte file header after patching any of its fields. */
static void resign_file_header(uint8_t* d) {
    d[14] = 0;
    d[15] = 0;
    const uint16_t crc = zxc_hash16(d);
    d[14] = (uint8_t)(crc & 0xFFU);
    d[15] = (uint8_t)(crc >> 8);
}

/* Re-sign an 8-byte block header at @p b after patching type or comp_size. */
static void resign_block_header(uint8_t* b) {
    uint8_t tmp[ZXC_BLOCK_HEADER_SIZE];
    memcpy(tmp, b, ZXC_BLOCK_HEADER_SIZE);
    tmp[7] = 0;
    b[7] = zxc_hash8(tmp);
}

/* Offset of the first block header; blocks follow the file header. */
#define BLK0 ZXC_FILE_HEADER_SIZE
/* Offset of the first block's payload (its GLO/GHI sub-header). */
#define PAY0 (BLK0 + ZXC_BLOCK_HEADER_SIZE)

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

/* Walk to the EOF block (type 255), returning its offset or 0 if not found. */
static size_t find_eof_block(const uint8_t* d, size_t n, int has_crc) {
    size_t p = BLK0;
    while (p + ZXC_BLOCK_HEADER_SIZE <= n) {
        const uint8_t t = d[p];
        if (t == ZXC_BLOCK_EOF) return p;
        const uint32_t comp = zxc_le32(d + p + 3);
        p += ZXC_BLOCK_HEADER_SIZE + comp + (has_crc ? ZXC_BLOCK_CHECKSUM_SIZE : 0);
    }
    return 0;
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : "conformance/invalid";
    int failures = 0;

    uint8_t* text = NULL;
    const size_t text_n = make_text(&text);

    /* Zero-initialised is the documented "safe defaults" form. */
    zxc_compress_opts_t plain = {0};
    plain.level = 3;
    plain.block_size = 4096;
    plain.checksum_enabled = 0;

    zxc_compress_opts_t chk = plain;
    chk.checksum_enabled = 1;

    zxc_compress_opts_t ghi = plain;
    ghi.level = 1; /* levels 1-2 emit GHI blocks */

    uint8_t* base = NULL;
    uint8_t* basec = NULL;
    uint8_t* baseg = NULL;
    const size_t n_plain = build_base(text, text_n, &plain, &base);
    const size_t n_chk = build_base(text, text_n, &chk, &basec);
    const size_t n_ghi = build_base(text, text_n, &ghi, &baseg);
    if (!n_plain || !n_chk || !n_ghi) return 1;

    /* Every GLO patch below addresses a fixed offset inside block 0's sub-header.
     * If a heuristic change ever made that block RAW, the patches would land on
     * literal bytes and the vectors would quietly stop testing anything. */
    if (base[BLK0] != ZXC_BLOCK_GLO || basec[BLK0] != ZXC_BLOCK_GLO) {
        fprintf(stderr, "  block 0 is type %u/%u, expected GLO - vectors would test nothing\n",
                base[BLK0], basec[BLK0]);
        return 1;
    }

    printf("Generating invalid vectors into %s/\n", dir);

    uint8_t* d = (uint8_t*)malloc(n_chk > n_plain ? n_chk : n_plain);
    if (!d) {
        fprintf(stderr, "  OOM\n");
        return 1;
    }

/* Each vector starts from a pristine copy of one base, patches one field, and
 * re-signs whatever CRC covers that field. */
#define FROM(src, len)   \
    do {                 \
        memcpy(d, src, len); \
    } while (0)

    /* --- File-header defects (CRC16 re-signed, except where the CRC IS the defect) */

    FROM(base, n_plain);
    d[5] = 31; /* block-size code outside [12,21] */
    resign_file_header(d);
    failures += write_file(dir, "bad_block_size_field", d, n_plain) < 0;

    FROM(base, n_plain);
    d[6] = (uint8_t)((d[6] & 0xF0U) | 0x0FU); /* checksum algorithm id != 0 */
    resign_file_header(d);
    failures += write_file(dir, "bad_checksum_algo", d, n_plain) < 0;

    FROM(base, n_plain);
    resign_file_header(d);
    d[14] ^= 0xFFU; /* the CRC itself is the defect: corrupt it last */
    failures += write_file(dir, "bad_header_crc", d, n_plain) < 0;

    FROM(base, n_plain);
    d[6] |= 0x40U; /* HAS_DICTIONARY with a non-zero id, but no dictionary supplied */
    d[7] = 0xEF;
    d[8] = 0xBE;
    d[9] = 0xAD;
    d[10] = 0xDE;
    resign_file_header(d);
    failures += write_file(dir, "dict_required", d, n_plain) < 0;

    /* --- Block-header defects (CRC8 re-signed) ---------------------------- */

    FROM(base, n_plain);
    d[BLK0] = 3; /* no such block type */
    resign_block_header(d + BLK0);
    failures += write_file(dir, "bad_block_type", d, n_plain) < 0;

    {
        FROM(base, n_plain);
        const size_t eof = find_eof_block(d, n_plain, 0);
        if (!eof) {
            fprintf(stderr, "  no EOF block found\n");
            failures++;
        } else {
            zxc_store_le32(d + eof + 3, 16); /* EOF must carry comp_size == 0 */
            resign_block_header(d + eof);
            failures += write_file(dir, "bad_eof_compsize", d, n_plain) < 0;
        }
    }

    /* --- Payload defects (no CRC over the sub-header when checksums are off) */

    FROM(base, n_plain);
    d[PAY0 + 8] = 9; /* enc_lit outside {0,1,2,3} */
    failures += write_file(dir, "bad_enc_lit", d, n_plain) < 0;

    FROM(base, n_plain);
    d[PAY0 + 11] = 7; /* enc_off outside {0,1} */
    failures += write_file(dir, "glo_forged_enc_off", d, n_plain) < 0;

    {
        /* Leave fewer than ZXC_BLOCK_LIT_SLACK bytes behind the literal section
         * by claiming more literals than the payload can carry behind them. */
        FROM(base, n_plain);
        const uint32_t comp = zxc_le32(d + BLK0 + 3);
        const uint32_t want = comp - ZXC_GLO_HEADER_BINARY_SIZE - (ZXC_BLOCK_LIT_SLACK - 1);
        zxc_store_le32(d + PAY0 + 4, want);
        failures += write_file(dir, "glo_insufficient_slack", d, n_plain) < 0;
    }

    {
        /* GHI sequence word: force the 16-bit offset field to reach far behind
         * the start of the output, which no decoder may follow. */
        uint8_t* g = (uint8_t*)malloc(n_ghi);
        if (!g) {
            fprintf(stderr, "  OOM\n");
            return 1;
        }
        memcpy(g, baseg, n_ghi);
        /* seq0 is derived from the wire, so check the block really is GHI, that
         * it has a sequence to forge, and that the word is inside the buffer -
         * otherwise the patch lands in the extras padding and the vector ships
         * decoding cleanly, i.e. testing nothing. */
        const size_t seq0 =
            PAY0 + ZXC_GHI_HEADER_BINARY_SIZE + zxc_le32(g + PAY0 + 4) /* n_literals, RAW */;
        if (g[BLK0] != ZXC_BLOCK_GHI || zxc_le32(g + PAY0) == 0 || seq0 + 4 > n_ghi) {
            fprintf(stderr, "  cannot forge a GHI offset (type %u, n_seq %u, seq0 %zu of %zu)\n",
                    g[BLK0], zxc_le32(g + PAY0), seq0, n_ghi);
            failures++;
        } else {
            uint32_t w = zxc_le32(g + seq0);
            w = (w & 0xFFFF0000U) | 0xFFFFU; /* max encodable distance */
            zxc_store_le32(g + seq0, w);
            failures += write_file(dir, "ghi_forged_offset", g, n_ghi) < 0;
        }
        free(g);
    }

    /* --- Checksum defects (need a checksummed base) ----------------------- */

    {
        FROM(basec, n_chk);
        const uint32_t comp = zxc_le32(d + BLK0 + 3);
        d[BLK0 + ZXC_BLOCK_HEADER_SIZE + comp] ^= 0xFFU; /* trailing block checksum */
        failures += write_file(dir, "bad_block_checksum", d, n_chk) < 0;
    }

    {
        FROM(basec, n_chk);
        d[PAY0 + ZXC_GLO_HEADER_BINARY_SIZE + 4] ^= 0xFFU; /* a literal byte */
        failures += write_file(dir, "corrupt_payload", d, n_chk) < 0;
    }

    /* --- Truncations ------------------------------------------------------ */

    FROM(base, n_plain);
    failures += write_file(dir, "truncated_header_only", d, ZXC_FILE_HEADER_SIZE) < 0;

    FROM(base, n_plain);
    failures += write_file(dir, "truncated_mid_block", d, PAY0 + 20) < 0;

#undef FROM

    free(d);
    free(base);
    free(basec);
    free(baseg);
    free(text);

    if (failures) {
        fprintf(stderr, "Generation FAILED (%d error(s)).\n", failures);
        return 1;
    }
    printf("Done. Re-run the conformance suite and check each vector's error code.\n");
    return 0;
}
