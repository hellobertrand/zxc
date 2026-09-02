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
 *     and the 16-bit header checksum (zxc_hash16, Sec 3 / Sec 7.1).
 *   - Generic block container: type, flags, reserved, comp_size bounds and the
 *     8-bit header checksum (zxc_hash8, Sec 4 / Sec 7.1).
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
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include <io.h>
#include <share.h>
#endif

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_error.h"
/* Private header: provides zxc_hash8/16, zxc_checksum, zxc_hash_combine_rotate
 * and the little-endian load helpers used to recompute the on-disk integrity
 * fields. Header-only (static inline), so no extra linkage is required. */
#include "../../src/lib/zxc_internal.h"
#include "../vector_io.h"
#include "golden_cases.h"

/* ------------------------------------------------------------------------- */
/* Reporting helpers                                                         */
/* ------------------------------------------------------------------------- */

static int g_checks; /* assertions performed in the current file */

/* Annotated field dump, built in memory so the same bytes serve --dump and the
 * comparison. Each EMIT sits beside the CHECK reading that field, so there is
 * one parser of the wire format, not two. */
static char* g_dump;
static size_t g_dump_len, g_dump_cap;
/* Sticky: a dropped line would make a correct dump look stale, and the advice
 * to regenerate would then overwrite it with the truncated text. */
static int g_dump_failed;

static void dump_reset(void) {
    g_dump_len = 0;
    g_dump_failed = 0;
}

static void dump_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) {
        g_dump_failed = 1;
        return;
    }
    if (g_dump_len + (size_t)need + 1 > g_dump_cap) {
        size_t cap = g_dump_cap ? g_dump_cap * 2 : 4096;
        while (cap < g_dump_len + (size_t)need + 1) cap *= 2;
        char* grown = (char*)realloc(g_dump, cap);
        if (!grown) {
            g_dump_failed = 1;
            return;
        }
        g_dump = grown;
        g_dump_cap = cap;
    }
    va_start(ap, fmt);
    vsnprintf(g_dump + g_dump_len, g_dump_cap - g_dump_len, fmt, ap);
    va_end(ap);
    g_dump_len += (size_t)need;
}

#define EMIT(...) dump_printf(__VA_ARGS__)

/* Raw header bytes, so the wire bytes sit next to their decoded meaning. */
static void emit_hex(const char* label, const uint8_t* p, size_t n) {
    dump_printf("%-18s", label);
    for (size_t i = 0; i < n; i++) dump_printf("%s%02X", i ? " " : "", p[i]);
    dump_printf("\n");
}

#define CHECK(cond, ...)                             \
    do {                                             \
        g_checks++;                                  \
        if (!(cond)) {                               \
            fprintf(stderr, "    FAIL [%s]: ", ctx); \
            fprintf(stderr, __VA_ARGS__);            \
            fprintf(stderr, "\n");                   \
            return 0;                                \
        }                                            \
    } while (0)

/* ------------------------------------------------------------------------- */
/* File IO                                                                   */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* Per-payload sub-header validation (FORMAT.md Sec 5)                          */
/* ------------------------------------------------------------------------- */

/* Shared validator for the GLO (Sec 5.2) and GHI (Sec 5.3) section model: a
 * 12-byte header, GLO's 0/4/8-byte section descriptors (GHI has none), then the
 * sections. Sizes absent from the table come from the header, and extras take
 * the residue - so the tiling check is an inequality, plus the
 * ZXC_BLOCK_LIT_SLACK guarantee behind the literals. */
static int validate_lz_payload(const char* ctx, const uint8_t* p, uint32_t comp, int is_glo,
                               int expect_enc_lit) {
    const uint32_t hdr = ZXC_GLO_HEADER_BINARY_SIZE;
    CHECK(comp >= hdr + ZXC_BLOCK_LIT_SLACK, "LZ payload too small for header+slack (%u)", comp);

    uint32_t n_sequences = zxc_le32(p);
    uint32_t n_literals = zxc_le32(p + 4);
    uint8_t enc_lit = p[8];
    uint8_t enc_tok = p[9];
    uint8_t enc_off = p[11];
    CHECK(enc_lit <= 3, "enc_lit = %u out of range", enc_lit);
    /* GLO: offset stream width, 0 or 1. GHI has no offset stream and the encoder
     * pins the field to 0 -- decoders must ignore it there (FORMAT.md 5.3). */
    if (!is_glo)
        CHECK(enc_off == 0, "GHI enc_off = %u, expected 0", enc_off);
    else
        CHECK(enc_off <= 1, "enc_off = %u out of range", enc_off);
    /* enc_mlen is reserved: match lengths ride the token byte. Frozen at 0 so a
     * future use can tell old encoders apart. */
    CHECK(p[10] == 0, "enc_mlen = %u, expected 0 (reserved)", p[10]);
    if (expect_enc_lit >= 0)
        CHECK(enc_lit == (uint8_t)expect_enc_lit, "expected enc_lit == %d, got %u", expect_enc_lit,
              enc_lit);

    emit_hex("  raw:", p, hdr);
    EMIT("  n_sequences:    %u\n", n_sequences);
    EMIT("  n_literals:     %u\n", n_literals);
    EMIT("  enc_lit:        %u\n", enc_lit);
    EMIT("  enc_tok:        %u\n", enc_tok);
    EMIT("  enc_mlen:       %u  (reserved)\n", p[10]);
    EMIT("  enc_off:        %u\n", enc_off);

    uint32_t table = 0;
    uint64_t sect_total = 0;
    uint32_t lit_comp = n_literals;
    if (is_glo) {
        /* Only the sizes the header cannot imply are on the wire. */
        uint32_t tok_comp = n_sequences;
        if (enc_lit != 0) {
            CHECK(comp >= hdr + table + 4, "GLO table truncated");
            lit_comp = zxc_le32(p + hdr + table);
            table += 4;
        }
        if (enc_tok == 2) {
            CHECK(comp >= hdr + table + 4, "GLO table truncated");
            tok_comp = zxc_le32(p + hdr + table);
            table += 4;
        } else {
            CHECK(enc_tok == 0, "GLO enc_tok = %u out of range", enc_tok);
        }
        sect_total = (uint64_t)lit_comp + tok_comp + (uint64_t)n_sequences * (enc_off ? 1U : 2U);
    } else {
        CHECK(enc_lit == 0, "GHI enc_lit = %u, expected RAW", enc_lit);
        sect_total = (uint64_t)n_literals + (uint64_t)n_sequences * 4U;
    }

    EMIT("  lit_comp:       %u\n", lit_comp);
    EMIT("  sect_total:     %llu\n", (unsigned long long)sect_total);

    uint64_t fixed = (uint64_t)hdr + table + sect_total;
    CHECK(fixed <= comp, "LZ sections overrun payload (%llu > %u)", (unsigned long long)fixed,
          comp);

    /* Sec 5.2/5.3: what follows the literal section must cover the decoder's
     * wild-copy overshoot. The padding's contents are unconstrained. */
    uint64_t behind_lit = (uint64_t)comp - hdr - table - lit_comp;
    CHECK(behind_lit >= ZXC_BLOCK_LIT_SLACK, "only %llu bytes behind the literal section",
          (unsigned long long)behind_lit);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Whole-file structural validation                                          */
/* ------------------------------------------------------------------------- */

#define MAX_BLOCKS 256

static int validate_structure(const char* ctx, const golden_case_t* gc, const uint8_t* buf,
                              size_t size) {
    /* ---- File header (Sec 3) ---- */
    CHECK(size >= ZXC_FILE_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE, "file too small (%zu)", size);

    CHECK(zxc_le32(buf) == ZXC_MAGIC_WORD, "bad magic 0x%08X", zxc_le32(buf));
    CHECK(buf[4] == ZXC_FILE_FORMAT_VERSION, "version %u != %u", buf[4],
          (unsigned)ZXC_FILE_FORMAT_VERSION);

    uint8_t code = buf[5];
    CHECK(code >= 12 && code <= 21, "invalid chunk-size code %u", code);

    EMIT("size:             %zu bytes\n\n[file header]\n", size);
    emit_hex("raw:", buf, ZXC_FILE_HEADER_SIZE);
    EMIT("magic:            0x%08X\n", zxc_le32(buf));
    EMIT("version:          %u\n", buf[4]);
    EMIT("chunk_code:       %u  (%u bytes)\n", code, 1U << code);

    uint8_t flags = buf[6];
    int has_checksum = (flags & ZXC_FILE_FLAG_HAS_CHECKSUM) ? 1 : 0;
    int has_dict = (flags & ZXC_FILE_FLAG_HAS_DICTIONARY) ? 1 : 0;
    const int want_dict = (gc->opts.dict != NULL && gc->opts.dict_size > 0);
    CHECK((flags & 0x0FU) == 0, "checksum algo id %u, expected 0", flags & 0x0FU);
    CHECK((flags & 0x30U) == 0, "reserved flag bits set (0x%02X)",
          flags); /* bit 6 = HAS_DICTIONARY */
    CHECK(has_checksum == gc->opts.checksum_enabled, "HAS_CHECKSUM=%d, expected %d", has_checksum,
          gc->opts.checksum_enabled);
    CHECK(has_dict == want_dict, "HAS_DICTIONARY=%d, expected %d", has_dict, want_dict);

    if (want_dict) {
        /* bytes 0x07..0x0A = dict_id (non-zero); 0x0B..0x0D reserved (zero). */
        CHECK(zxc_le32(buf + 7) != 0, "dict_id is zero on a dictionary archive");
        for (int i = 11; i <= 13; i++) CHECK(buf[i] == 0, "header reserved byte 0x%02X nonzero", i);
    } else {
        for (int i = 7; i <= 13; i++) CHECK(buf[i] == 0, "header reserved byte 0x%02X nonzero", i);
    }

    /* Sec 7.1 file header checksum: zxc_hash16 over the 16 header bytes with 0x0E..0x0F zeroed. */
    {
        uint8_t tmp[ZXC_FILE_HEADER_SIZE];
        memcpy(tmp, buf, ZXC_FILE_HEADER_SIZE);
        tmp[14] = tmp[15] = 0;
        uint16_t want = zxc_hash16(tmp);
        uint16_t got = zxc_le16(buf + 14);
        CHECK(got == want, "file header checksum mismatch: got 0x%04X want 0x%04X", got, want);
        EMIT("flags:            0x%02X  (checksum=%d dict=%d)\n", flags, has_checksum, has_dict);
        if (has_dict) EMIT("dict_id:          0x%08X\n", zxc_le32(buf + 7));
        EMIT("header_checksum:  0x%04X\n", got);
    }

    /* ---- Block stream (Sec 4, Sec 5) ---- */
    size_t off = ZXC_FILE_HEADER_SIZE;
    uint32_t rolling = 0; /* Sec 7.3 rolling global hash */
    int data_blocks = 0;
    uint32_t block_phys[MAX_BLOCKS]; /* physical size of each data block incl. checksum */

    for (;;) {
        CHECK(off + ZXC_BLOCK_HEADER_SIZE <= size, "block header overruns file at %zu", off);
        const uint8_t* bh = buf + off;
        uint8_t type = bh[0];
        uint8_t bflags = bh[1];
        uint8_t resv = bh[2];
        uint32_t comp = zxc_le32(bh + 3);

        /* Sec 7.1 block header checksum: zxc_hash8 over the 8 header bytes with 0x07 zeroed. */
        {
            uint8_t tmp[ZXC_BLOCK_HEADER_SIZE];
            memcpy(tmp, bh, ZXC_BLOCK_HEADER_SIZE);
            tmp[7] = 0;
            uint8_t want = zxc_hash8(tmp);
            CHECK(bh[7] == want, "block header checksum mismatch at %zu: got 0x%02X want 0x%02X",
                  off, bh[7], want);
        }
        EMIT("\n[block %d @%zu]\n", data_blocks, off);
        emit_hex("raw:", bh, ZXC_BLOCK_HEADER_SIZE);
        EMIT("type:             %s (%u)\n",
             type == GC_BLOCK_EOF   ? "EOF"
             : type == GC_BLOCK_RAW ? "RAW"
             : type == GC_BLOCK_GLO ? "GLO"
             : type == GC_BLOCK_GHI ? "GHI"
                                    : "?",
             type);
        if (type != GC_BLOCK_EOF) EMIT("comp_size:        %u\n", comp);
        EMIT("header_checksum:  0x%02X\n", bh[7]);
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

        const uint8_t* payload = bh + ZXC_BLOCK_HEADER_SIZE;
        CHECK(off + ZXC_BLOCK_HEADER_SIZE + comp <= size, "payload overruns file at %zu", off);

        if (type == GC_BLOCK_GLO) {
            if (!validate_lz_payload(ctx, payload, comp, 1, gc->expect_enc_lit)) return 0;
        } else if (type == GC_BLOCK_GHI) {
            if (!validate_lz_payload(ctx, payload, comp, 0, -1)) return 0;
        }

        /* The fields above describe the payload; this covers its bytes, so a
         * rewrite leaving comp_size and the counts alone still shows up. */
        EMIT("payload_hash:     0x%08X\n", zxc_checksum(payload, comp, ZXC_CHECKSUM_RAPIDHASH));

        size_t phys = ZXC_BLOCK_HEADER_SIZE + comp;
        off += phys;

        if (has_checksum) {
            /* Sec 7.2 per-block checksum over the compressed payload only. */
            CHECK(off + ZXC_BLOCK_CHECKSUM_SIZE <= size, "missing block checksum at %zu", off);
            uint32_t stored = zxc_le32(buf + off);
            uint32_t calc = zxc_checksum(payload, comp, ZXC_CHECKSUM_RAPIDHASH);
            CHECK(stored == calc, "block checksum mismatch at %zu: got 0x%08X calc 0x%08X", off,
                  stored, calc);
            EMIT("block_checksum:   0x%08X\n", stored);
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
        const uint8_t* sh = buf + off;
        uint32_t comp = zxc_le32(sh + 3);
        uint8_t tmp[ZXC_BLOCK_HEADER_SIZE];
        memcpy(tmp, sh, ZXC_BLOCK_HEADER_SIZE);
        tmp[7] = 0;
        CHECK(sh[7] == zxc_hash8(tmp), "SEK header checksum mismatch at %zu", off);
        CHECK(comp == (uint32_t)data_blocks * 4U, "SEK comp_size %u != n_blocks*4 (%d)", comp,
              data_blocks * 4);
        const uint8_t* entries = sh + ZXC_BLOCK_HEADER_SIZE;
        CHECK(off + ZXC_BLOCK_HEADER_SIZE + comp + ZXC_FILE_FOOTER_SIZE <= size,
              "SEK entries overrun file");
        for (int i = 0; i < data_blocks; i++) {
            uint32_t entry = zxc_le32(entries + (size_t)i * 4);
            CHECK(entry == block_phys[i], "SEK entry %d = %u, expected %u", i, entry,
                  block_phys[i]);
        }
        EMIT("\n[seek table @%zu]\n", off);
        emit_hex("raw:", sh, ZXC_BLOCK_HEADER_SIZE);
        EMIT("type:             SEK (%u)\n", sh[0]);
        EMIT("comp_size:        %u\n", comp);
        EMIT("header_checksum:  0x%02X\n", sh[7]);
        EMIT("entries:          %d\n", data_blocks);
        for (int i = 0; i < data_blocks; i++)
            EMIT("  block[%d]:       %u bytes\n", i, zxc_le32(entries + (size_t)i * 4));
        off += ZXC_BLOCK_HEADER_SIZE + comp;
        seek_present = 1;
    }
    CHECK(seek_present == gc->expect_seek, "SEK present=%d, expected %d", seek_present,
          gc->expect_seek);

    /* ---- File footer (Sec 8): the trailing 12 bytes, with nothing after it ---- */
    CHECK(off + ZXC_FILE_FOOTER_SIZE == size, "footer not at end (off %zu, size %zu)", off, size);
    const uint8_t* footer = buf + size - ZXC_FILE_FOOTER_SIZE;
    uint64_t src_size = zxc_le64(footer);
    uint32_t global_hash = zxc_le32(footer + 8);

    EMIT("\n[footer]\n");
    emit_hex("raw:", footer, ZXC_FILE_FOOTER_SIZE);
    EMIT("src_size:         %llu\n", (unsigned long long)src_size);
    EMIT("global_hash:      0x%08X\n", global_hash);

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
static int validate_roundtrip(const char* ctx, const golden_case_t* gc, const uint8_t* buf,
                              size_t size) {
    uint8_t* input = NULL;
    size_t in_size = gc->make_input(&input);

    uint64_t dec_sz = zxc_get_decompressed_size(buf, size);
    CHECK(dec_sz == in_size, "decoded size %llu != original %zu", (unsigned long long)dec_sz,
          in_size);

    int ok = 1;
    if (in_size > 0) {
        uint8_t* out = (uint8_t*)malloc(in_size);
        /* Dictionary cases must be decoded with their dictionary (and, for the
         * shared-table case, its Huffman table -- dict_id binds the pair). */
        zxc_decompress_opts_t dopts = {0};
        dopts.dict = gc->opts.dict;
        dopts.dict_size = gc->opts.dict_size;
        if (gc->use_dict_huf) dopts.dict_huf = gc_dict_huf_table();
        int64_t r = zxc_decompress(buf, size, out, in_size, &dopts);
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

/* golden.sha256 proves the bytes have not moved; only this proves the encoder
 * still produces them. 11_glo_rle drifted four commits unnoticed without it. */
static int validate_recipe(const char* ctx, const golden_case_t* gc, const uint8_t* have,
                           size_t have_size) {
    g_checks++;
    uint8_t* input = NULL;
    const size_t in_size = gc->make_input(&input);
    const size_t cap = (size_t)zxc_compress_bound(in_size) + 4096;
    uint8_t* out = (uint8_t*)malloc(cap);
    if (!out) {
        fprintf(stderr, "    FAIL [%s]: out of memory\n", ctx);
        free(input);
        return 0;
    }

    zxc_compress_opts_t opts = gc->opts;
    if (gc->use_dict_huf) opts.dict_huf = gc_dict_huf_table();
    const int64_t csize = zxc_compress(input, in_size, out, cap, &opts);

    int ok = 0;
    if (csize <= 0) {
        fprintf(stderr, "    FAIL [%s]: compress -> %s\n", ctx, zxc_error_name((int)csize));
    } else if ((size_t)csize != have_size || memcmp(out, have, have_size) != 0) {
        fprintf(stderr,
                "    FAIL [%s]: does not match its recipe (committed %zu bytes, recipe %lld)\n"
                "      regenerate: zxc_golden_gen, then refresh golden.sha256 and the dumps\n",
                ctx, have_size, (long long)csize);
    } else {
        ok = 1;
    }
    free(out);
    free(input);
    return ok;
}

/* --dump: (re)write the committed annotated dump for one case. */
static int write_dump(const char* ctx, const char* path) {
    if (g_dump_failed) {
        fprintf(stderr, "    FAIL [%s]: dump incomplete, refusing to write %s\n", ctx, path);
        return 0;
    }
    /* "wb": the dumps are compared byte for byte, so no newline translation. */
    FILE* f = vio_open_write(path);
    if (!f) {
        fprintf(stderr, "    FAIL [%s]: cannot write %s\n", ctx, path);
        return 0;
    }
    const int ok = fwrite(g_dump, 1, g_dump_len, f) == g_dump_len;
    fclose(f);
    if (!ok) fprintf(stderr, "    FAIL [%s]: short write on %s\n", ctx, path);
    return ok;
}

/* Default mode: the committed dump must match what the walk just produced. */
static int check_dump(const char* ctx, const char* path) {
    g_checks++;
    if (g_dump_failed) {
        fprintf(stderr, "    FAIL [%s]: dump could not be built (out of memory)\n", ctx);
        return 0;
    }
    size_t n = 0;
    uint8_t* have = vio_read_file(path, &n);
    if (!have) {
        fprintf(stderr, "    FAIL [%s]: missing %s\n", ctx, path);
        fprintf(stderr, "      regenerate: zxc_format_golden_test --dump tests/format/golden\n");
        return 0;
    }
    if (n == g_dump_len && memcmp(have, g_dump, n) == 0) {
        free(have);
        return 1;
    }
    /* Name the first differing line, so the failure points at the field. */
    size_t i = 0, line = 1, sol = 0;
    while (i < n && i < g_dump_len && have[i] == (uint8_t)g_dump[i]) {
        if (g_dump[i] == '\n') {
            line++;
            sol = i + 1;
        }
        i++;
    }
    size_t eol = sol;
    while (eol < g_dump_len && g_dump[eol] != '\n') eol++;
    fprintf(stderr, "    FAIL [%s]: %s is stale (first difference at line %zu)\n", ctx, path, line);
    fprintf(stderr, "      walk says: %.*s\n", (int)(eol - sol), g_dump + sol);
    fprintf(stderr, "      regenerate: zxc_format_golden_test --dump tests/format/golden\n");
    free(have);
    return 0;
}

int main(int argc, char** argv) {
    const char* dir = "tests/format/golden";
    const char* dump_dir = NULL; /* --dump <outdir>: also write <name>.zxc.txt */
    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--dump") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--dump needs a directory\n");
                return EXIT_FAILURE;
            }
            dump_dir = argv[i + 1];
            i += 2;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option '%s'\n", argv[i]);
            return EXIT_FAILURE;
        } else {
            dir = argv[i];
            i++;
        }
    }

    int failed = 0;
    printf("=== Golden format conformance (%s) ===\n", dir);

    for (size_t i = 0; i < GOLDEN_CASE_COUNT; i++) {
        const golden_case_t* gc = &GOLDEN_CASES[i];
        const char* ctx = gc->name;

        char path[1024];
        snprintf(path, sizeof path, "%s/%s.zxc", dir, gc->name);

        size_t size = 0;
        uint8_t* buf = vio_read_file(path, &size);
        if (!buf) {
            fprintf(stderr, "  FAIL: cannot read %s\n", path);
            failed++;
            continue;
        }

        dump_reset();
        EMIT("file:             %s\n", gc->name);

        g_checks = 0;
        int ok = validate_structure(ctx, gc, buf, size) && validate_roundtrip(ctx, gc, buf, size) &&
                 validate_recipe(ctx, gc, buf, size);

        if (ok) {
            char dump_path[1024];
            snprintf(dump_path, sizeof dump_path, "%s/%s.zxc.txt", dump_dir ? dump_dir : dir,
                     gc->name);
            ok = dump_dir ? write_dump(ctx, dump_path) : check_dump(ctx, dump_path);
        }
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
