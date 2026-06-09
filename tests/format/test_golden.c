/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Golden-file format conformance suite.
 *
 * Parses every byte-frozen golden/<name>.zxc and validates each field against
 * docs/FORMAT.md Sec 3-Sec 8:
 *
 *   - File header: magic, version, chunk-size code, flags, reserved bytes,
 *     and the 16-bit header CRC (zxc_hash16, Sec 3 / Sec 7.1).
 *   - Generic block container: type, flags, reserved, comp_size bounds and the
 *     8-bit header CRC (zxc_hash8, Sec 4 / Sec 7.1).
 *   - Every block type in Sec 5: RAW, GLO and GHI (header + section descriptors,
 *     incl. the Huffman literal section), EOF (zero comp_size) and the optional
 *     SEK seek table. (Type 2 is reserved/removed.)
 *   - Optional per-block checksum over the compressed payload (Sec 7.2).
 *   - The rolling global stream hash (Sec 7.3) reconstructed from per-block
 *     checksums and matched against the footer.
 *   - The 12-byte file footer: original source size and global hash (Sec 8).
 *
 * Each file is also round-tripped: decompressed and compared byte-for-byte
 * against its deterministically regenerated input (see golden_cases.h).
 *
 * Unlike the generator, this runs in CI on every platform. It only ever reads
 * the committed bytes; it never re-compresses, so it is fully deterministic.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_error.h"
/* Private header: provides zxc_hash8/16, zxc_checksum, zxc_hash_combine_rotate
 * and the little-endian load helpers used to recompute the on-disk integrity
 * fields. Header-only (static inline), so no extra linkage is required. */
#include "../../src/lib/zxc_internal.h"

#include "golden_cases.h"

/* ------------------------------------------------------------------------- */
/* Reporting helpers                                                         */
/* ------------------------------------------------------------------------- */

static int g_checks;  /* assertions performed in the current file */

#define CHECK(cond, ...)                                  \
    do {                                                  \
        g_checks++;                                       \
        if (!(cond)) {                                    \
            fprintf(stderr, "    FAIL [%s]: ", ctx);      \
            fprintf(stderr, __VA_ARGS__);                 \
            fprintf(stderr, "\n");                        \
            return 0;                                     \
        }                                                 \
    } while (0)

/* ------------------------------------------------------------------------- */
/* File IO                                                                   */
/* ------------------------------------------------------------------------- */

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)len ? (size_t)len : 1);
    if (buf && len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)len;
    return buf;
}

/* ------------------------------------------------------------------------- */
/* Per-payload sub-header validation (FORMAT.md Sec 5)                          */
/* ------------------------------------------------------------------------- */

/* Shared validator for the GLO (Sec 5.2) and GHI (Sec 5.3) section model: a 16-byte
 * header, then `n_sections` packed u64 descriptors (low32 = comp, high32 = raw),
 * then each section's bytes. The section sizes plus the headers must tile the
 * payload exactly. */
static int validate_lz_payload(const char *ctx, const uint8_t *p, uint32_t comp, int n_sections,
                               int expect_huffman) {
    uint32_t fixed = 16 + (uint32_t)n_sections * 8;
    CHECK(comp >= fixed, "LZ payload too small for header+descriptors (%u < %u)", comp, fixed);

    uint8_t enc_lit = p[8];
    uint8_t enc_off = p[11];
    CHECK(enc_lit <= 2, "enc_lit = %u out of range", enc_lit);
    CHECK(enc_off <= 1, "enc_off = %u out of range", enc_off);
    CHECK(zxc_le32(p + 12) == 0, "LZ header reserved u32 nonzero");
    if (expect_huffman)
        CHECK(enc_lit == 2, "expected Huffman literals (enc_lit==2), got %u", enc_lit);

    uint64_t sect_total = 0;
    for (int i = 0; i < n_sections; i++) {
        uint64_t desc = zxc_le64(p + 16 + (size_t)i * 8);
        uint32_t csz = (uint32_t)(desc & 0xFFFFFFFFu);
        sect_total += csz;
    }
    CHECK(fixed + sect_total == comp, "LZ sections do not tile payload (%u + %llu != %u)", fixed,
          (unsigned long long)sect_total, comp);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Whole-file structural validation                                          */
/* ------------------------------------------------------------------------- */

#define MAX_BLOCKS 256

static int validate_structure(const char *ctx, const golden_case_t *gc, const uint8_t *buf,
                              size_t size) {
    /* ---- File header (Sec 3) ---- */
    CHECK(size >= ZXC_FILE_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE, "file too small (%zu)", size);

    CHECK(zxc_le32(buf) == ZXC_MAGIC_WORD, "bad magic 0x%08X", zxc_le32(buf));
    CHECK(buf[4] == ZXC_FILE_FORMAT_VERSION, "version %u != %u", buf[4],
          (unsigned)ZXC_FILE_FORMAT_VERSION);

    uint8_t code = buf[5];
    CHECK(code >= 12 && code <= 21, "invalid chunk-size code %u", code);

    uint8_t flags = buf[6];
    int has_checksum = (flags & ZXC_FILE_FLAG_HAS_CHECKSUM) ? 1 : 0;
    CHECK((flags & 0x0Fu) == 0, "checksum algo id %u, expected 0", flags & 0x0Fu);
    CHECK((flags & 0x70u) == 0, "reserved flag bits set (0x%02X)", flags);
    CHECK(has_checksum == gc->opts.checksum_enabled, "HAS_CHECKSUM=%d, expected %d", has_checksum,
          gc->opts.checksum_enabled);

    for (int i = 7; i <= 13; i++) CHECK(buf[i] == 0, "header reserved byte 0x%02X nonzero", i);

    /* Sec 7.1 header CRC16: zxc_hash16 over the 16 header bytes with 0x0E..0x0F zeroed. */
    {
        uint8_t tmp[ZXC_FILE_HEADER_SIZE];
        memcpy(tmp, buf, ZXC_FILE_HEADER_SIZE);
        tmp[14] = tmp[15] = 0;
        uint16_t want = zxc_hash16(tmp);
        uint16_t got = zxc_le16(buf + 14);
        CHECK(got == want, "header CRC16 mismatch: got 0x%04X want 0x%04X", got, want);
    }

    /* ---- Block stream (Sec 4, Sec 5) ---- */
    size_t off = ZXC_FILE_HEADER_SIZE;
    uint32_t rolling = 0;            /* Sec 7.3 rolling global hash */
    int data_blocks = 0;
    uint32_t block_phys[MAX_BLOCKS]; /* physical size of each data block incl. checksum */

    for (;;) {
        CHECK(off + ZXC_BLOCK_HEADER_SIZE <= size, "block header overruns file at %zu", off);
        const uint8_t *bh = buf + off;
        uint8_t type = bh[0];
        uint8_t bflags = bh[1];
        uint8_t resv = bh[2];
        uint32_t comp = zxc_le32(bh + 3);

        /* Sec 7.1 block header CRC8: zxc_hash8 over the 8 header bytes with 0x07 zeroed. */
        {
            uint8_t tmp[ZXC_BLOCK_HEADER_SIZE];
            memcpy(tmp, bh, ZXC_BLOCK_HEADER_SIZE);
            tmp[7] = 0;
            uint8_t want = zxc_hash8(tmp);
            CHECK(bh[7] == want, "block CRC8 mismatch at %zu: got 0x%02X want 0x%02X", off, bh[7],
                  want);
        }
        CHECK(bflags == 0, "block flags nonzero (0x%02X) at %zu", bflags, off);
        CHECK(resv == 0, "block reserved nonzero (0x%02X) at %zu", resv, off);

        if (type == GC_BLOCK_EOF) {
            CHECK(comp == 0, "EOF comp_size = %u, must be 0", comp);
            off += ZXC_BLOCK_HEADER_SIZE;
            break;
        }

        /* Data block (RAW/GLO/GHI). */
        CHECK(type == GC_BLOCK_RAW || type == GC_BLOCK_GLO || type == GC_BLOCK_GHI,
              "unexpected block type %u at %zu", type, off);
        if (gc->expect_data_type != GC_ANY_TYPE)
            CHECK(type == gc->expect_data_type, "block type %u, expected %u at %zu", type,
                  gc->expect_data_type, off);
        CHECK(data_blocks < MAX_BLOCKS, "too many blocks");

        const uint8_t *payload = bh + ZXC_BLOCK_HEADER_SIZE;
        CHECK(off + ZXC_BLOCK_HEADER_SIZE + comp <= size, "payload overruns file at %zu", off);

        if (type == GC_BLOCK_GLO) {
            if (!validate_lz_payload(ctx, payload, comp, 4, gc->expect_huffman_literals)) return 0;
        } else if (type == GC_BLOCK_GHI) {
            if (!validate_lz_payload(ctx, payload, comp, 3, 0)) return 0;
        }

        size_t phys = ZXC_BLOCK_HEADER_SIZE + comp;
        off += phys;

        if (has_checksum) {
            /* Sec 7.2 per-block checksum over the compressed payload only. */
            CHECK(off + ZXC_BLOCK_CHECKSUM_SIZE <= size, "missing block checksum at %zu", off);
            uint32_t stored = zxc_le32(buf + off);
            uint32_t calc = zxc_checksum(payload, comp, ZXC_CHECKSUM_RAPIDHASH);
            CHECK(stored == calc, "block checksum mismatch at %zu: got 0x%08X calc 0x%08X", off,
                  stored, calc);
            rolling = zxc_hash_combine_rotate(rolling, stored);
            off += ZXC_BLOCK_CHECKSUM_SIZE;
            phys += ZXC_BLOCK_CHECKSUM_SIZE;
        }

        block_phys[data_blocks] = (uint32_t)phys;
        data_blocks++;
    }

    CHECK(data_blocks >= gc->min_data_blocks, "got %d data blocks, expected >= %d", data_blocks,
          gc->min_data_blocks);

    /* ---- Optional SEK block (Sec 5.5), located after EOF, before footer ---- */
    int seek_present = 0;
    if (off + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE <= size && buf[off] == GC_BLOCK_SEK) {
        const uint8_t *sh = buf + off;
        uint32_t comp = zxc_le32(sh + 3);
        uint8_t tmp[ZXC_BLOCK_HEADER_SIZE];
        memcpy(tmp, sh, ZXC_BLOCK_HEADER_SIZE);
        tmp[7] = 0;
        CHECK(sh[7] == zxc_hash8(tmp), "SEK header CRC8 mismatch at %zu", off);
        CHECK(comp == (uint32_t)data_blocks * 4u, "SEK comp_size %u != n_blocks*4 (%d)", comp,
              data_blocks * 4);
        const uint8_t *entries = sh + ZXC_BLOCK_HEADER_SIZE;
        CHECK(off + ZXC_BLOCK_HEADER_SIZE + comp + ZXC_FILE_FOOTER_SIZE <= size,
              "SEK entries overrun file");
        for (int i = 0; i < data_blocks; i++) {
            uint32_t entry = zxc_le32(entries + (size_t)i * 4);
            CHECK(entry == block_phys[i], "SEK entry %d = %u, expected %u", i, entry,
                  block_phys[i]);
        }
        off += ZXC_BLOCK_HEADER_SIZE + comp;
        seek_present = 1;
    }
    CHECK(seek_present == gc->expect_seek, "SEK present=%d, expected %d", seek_present,
          gc->expect_seek);

    /* ---- File footer (Sec 8): the trailing 12 bytes, with nothing after it ---- */
    CHECK(off + ZXC_FILE_FOOTER_SIZE == size, "footer not at end (off %zu, size %zu)", off, size);
    const uint8_t *footer = buf + size - ZXC_FILE_FOOTER_SIZE;
    uint64_t src_size = zxc_le64(footer);
    uint32_t global_hash = zxc_le32(footer + 8);

    uint64_t reported = zxc_get_decompressed_size(buf, size);
    CHECK(reported == src_size, "decoded-size query %llu != footer source size %llu",
          (unsigned long long)reported, (unsigned long long)src_size);

    if (has_checksum)
        CHECK(global_hash == rolling, "footer global hash 0x%08X != rolling 0x%08X", global_hash,
              rolling);
    else
        CHECK(global_hash == 0, "footer global hash must be 0 when checksums disabled (0x%08X)",
              global_hash);

    return 1;
}

/* Decompress and compare against the freshly regenerated deterministic input. */
static int validate_roundtrip(const char *ctx, const golden_case_t *gc, const uint8_t *buf,
                              size_t size) {
    uint8_t *input = NULL;
    size_t in_size = gc->make_input(&input);

    uint64_t dec_sz = zxc_get_decompressed_size(buf, size);
    CHECK(dec_sz == in_size, "decoded size %llu != original %zu",
          (unsigned long long)dec_sz, in_size);

    int ok = 1;
    if (in_size > 0) {
        uint8_t *out = (uint8_t *)malloc(in_size);
        int64_t r = zxc_decompress(buf, size, out, in_size, NULL);
        if (r < 0) {
            fprintf(stderr, "    FAIL [%s]: decompress -> %s\n", ctx, zxc_error_name((int)r));
            ok = 0;
        } else if ((size_t)r != in_size || memcmp(out, input, in_size) != 0) {
            fprintf(stderr, "    FAIL [%s]: round-trip content mismatch\n", ctx);
            ok = 0;
        }
        free(out);
    }
    g_checks++;
    free(input);
    return ok;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "tests/format/golden";

    int failed = 0;
    printf("=== Golden format conformance (%s) ===\n", dir);

    for (size_t i = 0; i < GOLDEN_CASE_COUNT; i++) {
        const golden_case_t *gc = &GOLDEN_CASES[i];
        const char *ctx = gc->name;

        char path[1024];
        snprintf(path, sizeof path, "%s/%s.zxc", dir, gc->name);

        size_t size = 0;
        uint8_t *buf = read_file(path, &size);
        if (!buf) {
            fprintf(stderr, "  FAIL: cannot read %s\n", path);
            failed++;
            continue;
        }

        g_checks = 0;
        int ok = validate_structure(ctx, gc, buf, size) && validate_roundtrip(ctx, gc, buf, size);
        if (ok)
            printf("  PASS: %-14s (%zu bytes, %d checks)\n", gc->name, size, g_checks);
        else
            failed++;

        free(buf);
    }

    printf("\n=== Summary ===\n");
    printf("Total: %zu  Passed: %zu  Failed: %d\n", (size_t)GOLDEN_CASE_COUNT,
           GOLDEN_CASE_COUNT - (size_t)failed, failed);
    if (failed) {
        printf("GOLDEN CONFORMANCE FAILED.\n");
        return 1;
    }
    printf("ALL GOLDEN CONFORMANCE TESTS PASSED.\n");
    return 0;
}
