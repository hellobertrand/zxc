// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/* Containers: frames back to back. Every shape goes through the buffer,
 * reusable-context and FILE* decoders, and both size queries, which must
 * agree. */

#include "test_common.h"

/* A growable container under construction. */
typedef struct {
    uint8_t* p;
    size_t n;
    size_t cap;
} cbuf_t;

static int cb_put(cbuf_t* b, const void* src, const size_t n) {
    if (b->n + n > b->cap) {
        const size_t cap = (b->n + n) * 2;
        uint8_t* const p = realloc(b->p, cap);
        if (!p) return 0;
        b->p = p;
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->n, src, n);
    b->n += n;
    return 1;
}

/* Geometry of one frame; a zero field keeps the parameter's default. */
typedef struct {
    int level;
    size_t block_size;
    int checksum_enabled;
    int seekable;
    const void* dict;
    size_t dict_size;
} frame_cfg_t;

static int cb_frame(cbuf_t* b, const uint8_t* src, const size_t n, const frame_cfg_t* co) {
    const size_t cap = zxc_compress_bound(n);
    uint8_t* const arc = malloc(cap);
    zxc_cctx* const cctx = zxc_create_cctx();
    int64_t len = -1;
    if (arc && cctx && zxc_cctx_set_param(cctx, ZXC_C_LEVEL, co->level) == ZXC_OK &&
        zxc_cctx_set_param(cctx, ZXC_C_BLOCK_SIZE, (int64_t)co->block_size) == ZXC_OK &&
        zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, co->checksum_enabled) == ZXC_OK &&
        zxc_cctx_set_param(cctx, ZXC_C_SEEKABLE, co->seekable) == ZXC_OK &&
        zxc_cctx_set_dict(cctx, co->dict, co->dict_size, NULL) == ZXC_OK)
        len = zxc_compress_cctx(cctx, src, n, arc, cap);
    const int ok = len > 0 && cb_put(b, arc, (size_t)len);
    zxc_free_cctx(cctx);
    free(arc);
    return ok;
}

/* FILE* decode of @p arc; writes the output to @p out (capacity @p cap). */
static int64_t file_decode(const uint8_t* arc, const size_t n, uint8_t* out, const size_t cap,
                           const zxc_dctx* cfg, int64_t* size_query) {
    FILE* const fi = tmpfile();
    FILE* const fo = tmpfile();
    int64_t r = -1;
    *size_query = -1;
    if (fi && fo && fwrite(arc, 1, n, fi) == n && fseek(fi, 0, SEEK_SET) == 0) {
        *size_query = zxc_stream_get_decompressed_size(fi);
        r = zxc_stream_decompress(fi, fo, cfg);
        if (r > 0) {
            if ((size_t)r > cap || fseek(fo, 0, SEEK_SET) != 0 ||
                fread(out, 1, (size_t)r, fo) != (size_t)r)
                r = -100;
        }
    }
    if (fi) fclose(fi);
    if (fo) fclose(fo);
    return r;
}

/* Push decode of @p arc in 7-byte pieces, so frame boundaries fall anywhere. Input
 * that stops before a footer is not finished: SRC_TOO_SMALL here. */
static int64_t push_decode(const uint8_t* arc, const size_t n, uint8_t* out, const size_t cap,
                           const zxc_dctx* cfg) {
    zxc_dstream* const ds = zxc_dstream_create(cfg);
    if (!ds) return ZXC_ERROR_MEMORY;
    zxc_outbuf_t ob = {out, cap, 0};
    int64_t r = 0;
    for (size_t at = 0; at < n && r >= 0;) {
        zxc_inbuf_t in = {arc + at, n - at < 7 ? n - at : 7, 0};
        do r = zxc_dstream_decompress(ds, &ob, &in);
        while (r > 0);
        at += in.pos;
        if (r == 0 && in.pos == 0) break;  // no progress
    }
    if (r >= 0) r = zxc_dstream_finished(ds) ? (int64_t)ob.pos : ZXC_ERROR_SRC_TOO_SMALL;
    zxc_dstream_free(ds);
    return r;
}

/* Decodes @p arc every way, under @p cfg's parameters (NULL: defaults); each must
 * return @p want (@p want_file for the FILE* decoder, which sees a cut frame as a
 * short read; @p want_push for the push one, which cannot tell input that stopped
 * from input still coming) and produce @p expect. The size queries report @p want
 * on success, nothing on failure. The buffer decode is the one-shot call, or a
 * context used once when @p cfg asks for checksums, which the one-shot never
 * verifies; the context decode reuses @p cfg itself across calls. */
static int check2(const char* what, const uint8_t* arc, const size_t n, zxc_dctx* cfg,
                  const int64_t want, const int64_t want_file, const int64_t want_push,
                  const uint8_t* expect) {
    // Room for every frame before the fault, so the verdict is the fault's.
    const size_t cap = (want > 0 ? (size_t)want : 0) + n * 8 + (1 << 20);
    uint8_t* const o1 = malloc(cap);
    uint8_t* const o2 = malloc(cap);
    uint8_t* const o3 = malloc(cap);
    uint8_t* const o4 = malloc(cap);
    zxc_dctx* const once = zxc_create_dctx();
    int64_t checksum = 0, threads = 0;
    int ok = o1 && o2 && o3 && o4 && once;
    if (!ok) printf("  [FAIL] %s: allocation\n", what);
    if (ok && cfg) {
        ok = zxc_dctx_get_param(cfg, ZXC_D_CHECKSUM, &checksum) == ZXC_OK &&
             zxc_dctx_get_param(cfg, ZXC_D_THREADS, &threads) == ZXC_OK &&
             zxc_dctx_set_param(once, ZXC_D_CHECKSUM, checksum) == ZXC_OK &&
             zxc_dctx_set_param(once, ZXC_D_THREADS, threads) == ZXC_OK;
        if (!ok) printf("  [FAIL] %s: parameters\n", what);
    }

    const int64_t r1 = !ok        ? -1
                       : checksum ? zxc_decompress_dctx(once, arc, n, o1, cap)
                                  : zxc_decompress(arc, n, o1, cap);
    const int64_t r2 = ok ? zxc_decompress_dctx(cfg ? cfg : once, arc, n, o2, cap) : -1;
    int64_t sq = -1;
    const int64_t r3 = ok ? file_decode(arc, n, o3, cap, cfg, &sq) : -1;
    const int64_t r4 = ok ? push_decode(arc, n, o4, cap, cfg) : -1;
    const uint64_t bq = zxc_get_decompressed_size(arc, n);

    if (ok && (r1 != want || r2 != want || r3 != want_file || r4 != want_push)) {
        printf(
            "  [FAIL] %s: buffer %lld, dctx %lld, FILE* %lld, push %lld (want %lld, %lld, %lld)\n",
            what, (long long)r1, (long long)r2, (long long)r3, (long long)r4, (long long)want,
            (long long)want_file, (long long)want_push);
        ok = 0;
    }
    if (ok && want > 0 &&
        (memcmp(o1, expect, (size_t)want) || memcmp(o2, expect, (size_t)want) ||
         memcmp(o3, expect, (size_t)want) || memcmp(o4, expect, (size_t)want))) {
        printf("  [FAIL] %s: output differs\n", what);
        ok = 0;
    }
    if (ok && want >= 0 && (bq != (uint64_t)want || sq != want)) {
        printf("  [FAIL] %s: size queries %llu / %lld (want %lld)\n", what, (unsigned long long)bq,
               (long long)sq, (long long)want);
        ok = 0;
    }
    zxc_free_dctx(once);
    free(o1);
    free(o2);
    free(o3);
    free(o4);
    return ok;
}

static int check(const char* what, const uint8_t* arc, const size_t n, zxc_dctx* cfg,
                 const int64_t want, const uint8_t* expect) {
    return check2(what, arc, n, cfg, want, want, want, expect);
}

int test_container_concat(void) {
    printf("=== TEST: Container - concatenated frames ===\n");
    const size_t na = 3 * 4096 + 17, nb = 70000, nc = 1000;
    uint8_t* const src = malloc(na + nb + nc);
    cbuf_t b = {0};
    int ok = src != NULL;
    if (ok) gen_lz_data(src, na + nb + nc);

    /* Mixed geometry: small blocks with checksums, a seek table, an empty frame. */
    const frame_cfg_t ca = {.level = 3, .block_size = 4096, .checksum_enabled = 1};
    const frame_cfg_t cb = {.level = 5, .seekable = 1, .checksum_enabled = 1};
    const frame_cfg_t cc = {.level = 1, .block_size = 8192, .seekable = 1};
    ok = ok && cb_frame(&b, src, na, &ca) && cb_frame(&b, NULL, 0, &cc) &&
         cb_frame(&b, src + na, nb, &cb) && cb_frame(&b, src + na + nb, nc, &cc);
    zxc_dctx* const verify = zxc_create_dctx();
    ok = ok && verify && zxc_dctx_set_param(verify, ZXC_D_CHECKSUM, 1) == ZXC_OK;
    ok = ok && check("four frames", b.p, b.n, NULL, (int64_t)(na + nb + nc), src);
    ok = ok && check("four frames, checksums", b.p, b.n, verify, (int64_t)(na + nb + nc), src);

    /* A destination that ends inside the second payload. */
    if (ok) {
        uint8_t* const out = malloc(na + 100);
        const int64_t r = out ? zxc_decompress(b.p, b.n, out, na + 100) : -1;
        if (r != ZXC_ERROR_DST_TOO_SMALL) {
            printf("  [FAIL] short destination: %lld\n", (long long)r);
            ok = 0;
        }
        free(out);
    }

    /* A truncated last frame, and 1..3 stray bytes after the last footer. */
    ok = ok && check2("truncated last frame", b.p, b.n - 1, NULL, ZXC_ERROR_SRC_TOO_SMALL,
                      ZXC_ERROR_IO, ZXC_ERROR_SRC_TOO_SMALL, NULL);
    for (size_t extra = 1; ok && extra <= 3; extra++) {
        const uint8_t z[3] = {0xF5, 0x2E, 0xB0}; /* a cut magic word */
        cbuf_t t = {0};
        ok = cb_put(&t, b.p, b.n) && cb_put(&t, z, extra);
        ok = ok && check2("cut magic after the last footer", t.p, t.n, NULL, ZXC_ERROR_CORRUPT_DATA,
                          ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_SRC_TOO_SMALL, NULL);
        free(t.p);
    }

    /* An unknown magic word first is still a bad magic word. */
    if (ok) {
        uint8_t junk[64] = {0};
        junk[0] = 0x1A;
        ok = check("unknown first magic", junk, sizeof(junk), NULL, ZXC_ERROR_BAD_MAGIC, NULL);
    }

    /* In place stays single-frame. */
    if (ok) {
        const size_t cap = (size_t)(na + nb + nc) + b.n + 65536;
        uint8_t* const buf = malloc(cap);
        if (buf) memcpy(buf + cap - b.n, b.p, b.n);
        const int64_t r = buf ? zxc_decompress_inplace(buf, cap, b.n) : 0;
        if (r >= 0) {
            printf("  [FAIL] in-place decode of a multi-frame input: %lld\n", (long long)r);
            ok = 0;
        }
        free(buf);
    }

    zxc_free_dctx(verify);
    free(b.p);
    free(src);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Largest progress count seen. */
static void max_progress(const uint64_t done, const uint64_t total, const void* user_data) {
    (void)total;
    uint64_t* const seen = (uint64_t*)(uintptr_t)user_data;
    if (done > *seen) *seen = done;
}

/* The FILE* decoder keeps its engine across a run of one geometry, restarts it
 * between runs: output, input position, progress and faults. */
int test_container_engine_reuse(void) {
    printf("=== TEST: Container - FILE* engine reused across frames ===\n");
    const size_t n = 3 * 4096 + 17, nbig = 300000, total = 5 * n + 2 * nbig;
    uint8_t* const src = malloc(total);
    cbuf_t b = {0};
    size_t ends[7] = {0};
    int ok = src != NULL;
    if (ok) gen_lz_data(src, total);

    // Three small frames, two with large blocks (positioned reads), two small again.
    const frame_cfg_t small = {.level = 3, .block_size = 4096, .checksum_enabled = 1};
    const frame_cfg_t big = {.level = 2, .block_size = 65536, .checksum_enabled = 1};
    size_t at = 0;
    for (int i = 0; ok && i < 7; i++) {
        const int is_big = i == 3 || i == 4;
        const size_t len = is_big ? nbig : n;
        ok = cb_frame(&b, src + at, len, is_big ? &big : &small);
        at += len;
        ends[i] = b.n;
    }
    zxc_dctx* const verify = zxc_create_dctx();
    ok = ok && verify && zxc_dctx_set_param(verify, ZXC_D_CHECKSUM, 1) == ZXC_OK &&
         zxc_dctx_set_param(verify, ZXC_D_THREADS, 4) == ZXC_OK;
    ok = ok && check("seven frames, three runs", b.p, b.n, verify, (int64_t)at, src);

    // Positioned reads leave the FILE* where the input ends.
    if (ok) {
        FILE* const fi = tmpfile();
        int64_t r = -1;
        long pos = -1;
        if (fi && fwrite(b.p, 1, b.n, fi) == b.n && fseek(fi, 0, SEEK_SET) == 0) {
            r = zxc_stream_decompress(fi, NULL, verify);
            pos = ftell(fi);
        }
        if (r < 0 || (size_t)r != at || pos != (long)b.n) {
            printf("  [FAIL] input position: decoded %lld, at %ld (want %zu)\n", (long long)r, pos,
                   b.n);
            ok = 0;
        }
        if (fi) fclose(fi);
    }

    // Progress reaches the total across frames and engine restarts.
    if (ok) {
        uint64_t seen = 0;
        zxc_dctx* const po = zxc_create_dctx();
        uint8_t* const out = malloc(total);
        int64_t sq = 0;
        const int configured = po && zxc_dctx_set_param(po, ZXC_D_THREADS, 4) == ZXC_OK &&
                               zxc_dctx_set_progress(po, max_progress, &seen) == ZXC_OK;
        const int64_t r = out && configured ? file_decode(b.p, b.n, out, total, po, &sq) : -1;
        if (r < 0 || (size_t)r != at || seen != (uint64_t)at) {
            printf("  [FAIL] progress: decoded %lld, last progress %llu (want %zu)\n", (long long)r,
                   (unsigned long long)seen, at);
            ok = 0;
        }
        zxc_free_dctx(po);
        free(out);
    }

    // Faults in the third frame (reused engine): a payload byte, then the digest.
    for (int k = 0; ok && k < 2; k++) {
        cbuf_t t = {0};
        ok = cb_put(&t, b.p, b.n);
        if (ok && k == 0) t.p[ends[1] + ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + 4] ^= 0x40;
        if (ok && k == 1) {
            const uint8_t l = t.p[ends[2] - 1];
            t.p[ends[2] - 1 - ((l & 7) + 1) - ((l >> 4) + 1) - 8] ^= 0x01;
        }
        ok = ok &&
             check(k ? "digest fault in a reused engine" : "payload fault in a reused engine", t.p,
                   t.n, verify, k ? ZXC_ERROR_BAD_CHECKSUM : ZXC_ERROR_CORRUPT_DATA, NULL);
        free(t.p);
    }

    zxc_free_dctx(verify);
    free(b.p);
    free(src);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* The last frame's info, walked back to the first, buffer and FILE*: the sizes
 * each frame was written with, in reverse order. */
int test_container_frame_walk(void) {
    printf("=== TEST: Container - walking frames back from the end ===\n");
    const size_t sizes[] = {3 * 4096 + 17, 0, 70000, 1000};
    const frame_cfg_t opts[] = {{.level = 3, .block_size = 4096, .checksum_enabled = 1},
                                {.level = 1, .block_size = 8192, .seekable = 1},
                                {.level = 5, .seekable = 1, .checksum_enabled = 1},
                                {.level = 1, .block_size = 8192}};
    enum { K = 4 };
    uint8_t* const src = malloc(80000);
    cbuf_t b = {0};
    size_t ends[K];
    int ok = src != NULL;
    if (ok) gen_lz_data(src, 80000);
    for (size_t k = 0; ok && k < K; k++) {
        ok = cb_frame(&b, src, sizes[k], &opts[k]);
        ends[k] = b.n;
    }

    FILE* const f = tmpfile();
    ok = ok && f && fwrite(b.p, 1, b.n, f) == b.n && fseek(f, 7, SEEK_SET) == 0;
    size_t end = b.n;
    for (size_t k = K; ok && k-- > 0;) {
        zxc_frame_info_t a, c;
        const int ra = zxc_get_last_frame_info(b.p, end, &a, sizeof(a));
        const int rc = zxc_stream_get_last_frame_info(f, end, &c, sizeof(c));
        const size_t start = k ? ends[k - 1] : 0;
        if (ra != ZXC_OK || rc != ZXC_OK || end != ends[k] || a.compressed_size != end - start ||
            a.decompressed_size != sizes[k] || a.has_checksum != opts[k].checksum_enabled ||
            a.has_seek_table != opts[k].seekable || memcmp(&a, &c, sizeof(a)) != 0 ||
            ftell(f) != 7) {
            printf("  [FAIL] frame %zu: buffer %d, FILE* %d\n", k, ra, rc);
            ok = 0;
        }
        end -= (size_t)a.compressed_size;
    }
    ok = ok && end == 0;

    /* Failures: the struct untouched, and the code each input deserves. */
    zxc_frame_info_t keep;
    memset(&keep, 0xA5, sizeof(keep));
    const zxc_frame_info_t before = keep;
    if (ok) {
        const struct {
            const char* what;
            size_t end;
            int buffer, file;
        } bad[] = {
            {"inside a frame", ends[2] - 1, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA},
            {"past the input", b.n + 1, ZXC_ERROR_SRC_TOO_SMALL, ZXC_ERROR_SRC_TOO_SMALL},
            {"empty", 0, ZXC_ERROR_SRC_TOO_SMALL, ZXC_ERROR_SRC_TOO_SMALL},
        };
        for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
            /* The buffer API reads within src_size, so past the input is cut short. */
            const size_t n = bad[k].end > b.n ? 10 : bad[k].end;
            const int ra = zxc_get_last_frame_info(b.p, n, &keep, sizeof(keep));
            const int rc = zxc_stream_get_last_frame_info(f, bad[k].end, &keep, sizeof(keep));
            if (ra != bad[k].buffer || rc != bad[k].file ||
                memcmp(&keep, &before, sizeof(keep)) != 0) {
                printf("  [FAIL] %s: buffer %d, FILE* %d\n", bad[k].what, ra, rc);
                ok = 0;
            }
        }
        if (zxc_get_last_frame_info(NULL, b.n, &keep, sizeof(keep)) != ZXC_ERROR_NULL_INPUT ||
            zxc_get_last_frame_info(b.p, b.n, NULL, 0) != ZXC_ERROR_NULL_INPUT ||
            zxc_stream_get_last_frame_info(NULL, b.n, &keep, sizeof(keep)) !=
                ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] NULL arguments\n");
            ok = 0;
        }
    }
    if (f) fclose(f);
    free(b.p);
    free(src);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Seekable frames back to back read as one seekable archive: ranges across frame
 * boundaries; block sizes, checksums and dictionaries that differ per frame; an
 * empty frame; buffer and FILE*, one thread and several. A frame without a
 * table refuses the open. */
int test_container_seekable(void) {
    printf("=== TEST: Container - seekable reader over concatenated frames ===\n");
    const size_t sizes[] = {5 * 4096 + 100, 0, 70000, 1000};
    const frame_cfg_t opts[] = {
        {.level = 3, .block_size = 4096, .checksum_enabled = 1, .seekable = 1},
        {.level = 1, .block_size = 8192, .seekable = 1},
        {.level = 5, .seekable = 1},
        {.level = 1, .block_size = 4096, .checksum_enabled = 1, .seekable = 1}};
    enum { K = 4 };
    size_t total = 0;
    for (size_t k = 0; k < K; k++) total += sizes[k];
    uint8_t* const src = malloc(total);
    uint8_t* const out = malloc(total);
    cbuf_t b = {0};
    int ok = src && out;
    if (ok) gen_lz_data(src, total);
    uint64_t blocks = 0;
    for (size_t k = 0, at = 0; ok && k < K; at += sizes[k], k++) {
        const size_t bs = opts[k].block_size ? opts[k].block_size : ZXC_BLOCK_SIZE_DEFAULT;
        ok = cb_frame(&b, src + at, sizes[k], &opts[k]);
        blocks += (sizes[k] + bs - 1) / bs;
    }

    FILE* const fp = tmpfile();
    ok = ok && fp && fwrite(b.p, 1, b.n, fp) == b.n && fflush(fp) == 0;
    zxc_seekable* const sb = ok ? zxc_seekable_open(b.p, b.n) : NULL;
    zxc_seekable* const sf = ok ? zxc_seekable_open_file(fp) : NULL;
    if (ok && (!sb || !sf)) {
        printf("  [FAIL] open: buffer %p, FILE* %p\n", (void*)sb, (void*)sf);
        ok = 0;
    }
    if (ok) {
        zxc_seekable_set_checksum(sb, 1);
        zxc_seekable_set_checksum(sf, 1);
        uint64_t dsum = 0;
        for (uint64_t i = 0; i < blocks; i++) dsum += zxc_seekable_get_block_decomp_size(sb, i);
        if (zxc_seekable_get_num_blocks(sb) != blocks ||
            zxc_seekable_get_decompressed_size(sb) != total || dsum != total ||
            zxc_seekable_get_block_comp_size(sb, blocks - 1) == 0 ||
            zxc_seekable_get_block_comp_size(sb, blocks) != 0) {
            printf("  [FAIL] geometry: %llu blocks (want %llu), %llu bytes\n",
                   (unsigned long long)zxc_seekable_get_num_blocks(sb), (unsigned long long)blocks,
                   (unsigned long long)zxc_seekable_get_decompressed_size(sb));
            ok = 0;
        }
    }
    /* Ranges inside one frame, across each boundary, and the whole archive. */
    const size_t b1 = sizes[0], b2 = sizes[0] + sizes[2];
    const struct {
        size_t off, len;
    } ranges[] = {{0, total},     {b1 - 10, 20},  {b2 - 5000, 6000}, {100, b2},
                  {b1 + 4096, 3}, {total - 1, 1}, {4095, 2}};
    for (size_t r = 0; ok && r < sizeof(ranges) / sizeof(ranges[0]); r++) {
        const size_t off = ranges[r].off, len = ranges[r].len;
        for (int t = 0; ok && t < 3; t++) {
            zxc_seekable* const s = t == 2 ? sf : sb;
            memset(out, 0, len);
            const int64_t got = t == 1 ? zxc_seekable_decompress_range_mt(s, out, len, off, len, 4)
                                       : zxc_seekable_decompress_range(s, out, len, off, len);
            if (got != (int64_t)len || memcmp(out, src + off, len) != 0) {
                printf("  [FAIL] range [%zu, +%zu), path %d: %lld\n", off, len, t, (long long)got);
                ok = 0;
            }
        }
    }
    zxc_seekable_free(sb);
    zxc_seekable_free(sf);
    if (fp) fclose(fp);

    /* Level 7, 4 KB then 512 KB blocks, text so the entropy coder runs: the small
     * frame read first must not size its scratch (one thread, then several). */
    static const char* const words[] = {"the ",    "and ",   "of ",   "rabbit ", "said ", "Alice ",
                                        "little ", "queen ", "very ", "she ",    "a ",    "\n"};
    uint8_t* const text = malloc(80000);
    for (size_t i = 0, x = 12345; text && i < 80000;) {
        x = x * 1103515245U + 12345U;
        const char* w = words[(x >> 16) % 12];
        while (*w && i < 80000) text[i++] = (uint8_t)*w++;
    }
    for (int t = 0; ok && t < 2; t++) {
        const frame_cfg_t s7 = {.level = 7, .block_size = 4096, .seekable = 1};
        const frame_cfg_t l7 = {.level = 7, .seekable = 1};
        cbuf_t m = {0};
        ok = text && cb_frame(&m, text, 9000, &s7) && cb_frame(&m, text + 9000, 70000, &l7);
        zxc_seekable* const s = ok ? zxc_seekable_open(m.p, m.n) : NULL;
        const int64_t r1 = s ? zxc_seekable_decompress_range(s, out, 4096, 0, 4096) : -1;
        const int64_t r2 = s ? (t ? zxc_seekable_decompress_range_mt(s, out, 79000, 0, 79000, 2)
                                  : zxc_seekable_decompress_range(s, out, 70000, 9000, 70000))
                             : -1;
        if (ok && (r1 != 4096 || r2 != (t ? 79000 : 70000) ||
                   memcmp(out, text + (t ? 0 : 9000), (size_t)r2) != 0)) {
            printf("  [FAIL] level 7, small blocks first, path %d: %lld then %lld\n", t,
                   (long long)r1, (long long)r2);
            ok = 0;
        }
        zxc_seekable_free(s);
        free(m.p);
    }
    free(text);

    /* A forged SEK header before the last frame: the open succeeds, that frame
     * refuses its ranges, the other still reads. */
    if (ok) {
        const frame_cfg_t so = {.level = 3, .block_size = 4096, .seekable = 1};
        cbuf_t m = {0};
        ok = cb_frame(&m, src, 9000, &so);
        const size_t first = m.n;
        ok = ok && cb_frame(&m, src + 9000, 9000, &so);
        // [.. EOF 8][SEK 8][table: 1 group of 3 sizes = 20][footer]
        const size_t sek = first - test_footer_len(m.p, first) - 20 - ZXC_BLOCK_HEADER_SIZE;
        if (ok && m.p[sek] == ZXC_BLOCK_SEK) {
            m.p[sek + 2] = 1;  // reserved byte, header re-signed
            m.p[sek + 7] = 0;
            m.p[sek + 7] = zxc_hash8(m.p + sek);
        } else {
            ok = 0;
        }
        zxc_seekable* const s = ok ? zxc_seekable_open(m.p, m.n) : NULL;
        const int64_t r1 = s ? zxc_seekable_decompress_range(s, out, 100, 0, 100) : -1;
        const int64_t r2 = s ? zxc_seekable_decompress_range(s, out, 100, 9000, 100) : -1;
        if (ok && (!s || r1 != ZXC_ERROR_CORRUPT_DATA || r2 != 100 ||
                   memcmp(out, src + 9000, 100) != 0)) {
            printf("  [FAIL] forged SEK header in frame 1: open %p, reads %lld / %lld\n", (void*)s,
                   (long long)r1, (long long)r2);
            ok = 0;
        }
        zxc_seekable_free(s);
        free(m.p);
    }

    /* At most 2^20 frames: one more fails the open. */
    if (ok) {
        const frame_cfg_t so = {.level = 1, .seekable = 1};
        cbuf_t one = {0};
        const size_t fl = cb_frame(&one, (const uint8_t*)"x", 1, &so) ? one.n : 0;
        const size_t count = ((size_t)1 << 20) + 1;
        uint8_t* const many = fl > 0 ? malloc(count * fl) : NULL;
        for (size_t i = 0; many && i < count; i++) memcpy(many + i * fl, one.p, fl);
        zxc_seekable* const s = many ? zxc_seekable_open(many, count * fl) : NULL;
        zxc_seekable* const s1 = many ? zxc_seekable_open(many, fl) : NULL;
        if (!many || s || !s1) {
            printf("  [FAIL] 2^20 + 1 frames: opened %d, one frame opened %d\n", s != NULL,
                   s1 != NULL);
            ok = 0;
        }
        zxc_seekable_free(s);
        zxc_seekable_free(s1);
        free(many);
        free(one.p);
    }

    /* A frame without a table refuses the open. */
    if (ok) {
        const frame_cfg_t plain = {.level = 3};
        cbuf_t m = {0};
        ok = cb_put(&m, b.p, b.n) && cb_frame(&m, src, 3000, &plain);
        zxc_seekable* const s = ok ? zxc_seekable_open(m.p, m.n) : NULL;
        if (ok && s) {
            printf("  [FAIL] opened with a frame without a table\n");
            ok = 0;
        }
        zxc_seekable_free(s);
        free(m.p);
    }

    /* A dictionary per frame, of different sizes, or none; each cut from the data
     * of a frame using it, so decoding with another fails. */
    static uint8_t da[4096], db[1500], dc[512];
    if (ok) {
        memcpy(da, src + 35000, sizeof(da));
        memcpy(db, src + 15000, sizeof(db));
        memset(dc, 's', sizeof(dc));
    }
    const frame_cfg_t dopts[] = {
        {.level = 3, .seekable = 1, .dict = da, .dict_size = sizeof(da), .checksum_enabled = 1},
        {.level = 3, .block_size = 4096, .seekable = 1},
        {.level = 5, .block_size = 8192, .seekable = 1, .dict = db, .dict_size = sizeof(db)},
        {.level = 1, .seekable = 1, .dict = da, .dict_size = sizeof(da)}};
    const size_t dlen[] = {9000, 6000, 20000, 3000};
    cbuf_t m = {0};
    size_t dtotal = 0;
    for (size_t k = 0; ok && k < 4; dtotal += dlen[k], k++)
        ok = cb_frame(&m, src + dtotal, dlen[k], &dopts[k]);
    zxc_seekable* const sd = ok ? zxc_seekable_open(m.p, m.n) : NULL;
    if (ok && !sd) {
        printf("  [FAIL] frames on different dictionaries did not open\n");
        ok = 0;
    }
    if (ok) {
        zxc_seekable_set_checksum(sd, 1);
        // The plain frame reads without any; the others want theirs.
        const int64_t plain = zxc_seekable_decompress_range(sd, out, 100, 9000, 100);
        // Attached to an archive that needs none, a dictionary is ignored.
        zxc_seekable* const sp = zxc_seekable_open(b.p, b.n);
        if (!sp || zxc_seekable_set_dict(sp, da, sizeof(da), NULL) != ZXC_OK) {
            printf("  [FAIL] a dictionary on an archive that needs none was refused\n");
            ok = 0;
        }
        zxc_seekable_free(sp);
        const int64_t need = zxc_seekable_decompress_range_mt(sd, out, dtotal, 0, dtotal, 4);
        const int wrong = zxc_seekable_set_dict(sd, dc, sizeof(dc), NULL);
        if (plain != 100 || memcmp(out, src + 9000, 100) != 0 || need != ZXC_ERROR_DICT_REQUIRED ||
            wrong != ZXC_ERROR_DICT_MISMATCH ||
            zxc_seekable_set_dict(sd, da, sizeof(da), NULL) != ZXC_OK ||
            zxc_seekable_set_dict(sd, db, sizeof(db), NULL) != ZXC_OK) {
            printf("  [FAIL] dictionaries: plain %lld, none %lld, unused %d\n", (long long)plain,
                   (long long)need, wrong);
            ok = 0;
        }
        for (int t = 0; ok && t < 2; t++) {
            memset(out, 0, dtotal);
            const int64_t got = t ? zxc_seekable_decompress_range_mt(sd, out, dtotal, 0, dtotal, 4)
                                  : zxc_seekable_decompress_range(sd, out, dtotal, 0, dtotal);
            if (got != (int64_t)dtotal || memcmp(out, src, dtotal) != 0) {
                printf("  [FAIL] dictionaries, path %d: %lld\n", t, (long long)got);
                ok = 0;
            }
        }
    }
    zxc_seekable_free(sd);
    free(m.p);

    free(b.p);
    free(out);
    free(src);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Range reads against the output's bounds: in place single-threaded when the
 * 2112 B margin fits the range, through the worker's buffer threaded. 4 KB,
 * 64 KB and 4 KB frames with short last blocks, ranges on either side of the
 * margin, more threads than blocks, buffer and FILE* sources. Bytes past len
 * stay untouched; a corrupted block is reported. */
int test_container_seekable_inplace(void) {
    printf("=== TEST: Container - Seekable in-place range decoding ===\n");

    const size_t n1 = 4096 * 37 + 1000, n2 = 65536 * 9 + 3000, n3 = 4096 * 20;
    const size_t total = n1 + n2 + n3;
    const size_t guard = 4096;
    static const char* const words[] = {"alpha ", "beta ",  "gamma ", "delta ", "eps ",
                                        "zeta ",  "theta ", "iota ",  "kappa ", "\n"};
    uint8_t* const src = malloc(total);
    uint8_t* const out = malloc(total + guard);
    int ok = src && out;
    for (size_t i = 0, x = 777; ok && i < total;) {
        x = x * 1103515245U + 12345U;
        const char* w = words[(x >> 16) % 10];
        while (*w && i < total) src[i++] = (uint8_t)*w++;
    }

    const frame_cfg_t f1 = {.level = 3, .block_size = 4096, .seekable = 1};
    const frame_cfg_t f2 = {.level = 5, .block_size = 65536, .checksum_enabled = 1, .seekable = 1};
    const frame_cfg_t f3 = {.level = 1, .block_size = 4096, .seekable = 1};
    cbuf_t m = {0};
    ok = ok && cb_frame(&m, src, n1, &f1);
    const size_t f2_at = m.n;
    ok = ok && cb_frame(&m, src + n1, n2, &f2);
    ok = ok && cb_frame(&m, src + n1 + n2, n3, &f3);

    FILE* const fp = ok ? tmpfile() : NULL;
    ok = ok && fp && fwrite(m.p, 1, m.n, fp) == m.n && fflush(fp) == 0;
    zxc_seekable* const sb = ok ? zxc_seekable_open(m.p, m.n) : NULL;
    zxc_seekable* const sf = ok ? zxc_seekable_open_file(fp) : NULL;
    if (ok && (!sb || !sf)) {
        printf("  [FAIL] open\n");
        ok = 0;
    }
    if (ok) {
        zxc_seekable_set_checksum(sb, 1);
        zxc_seekable_set_checksum(sf, 1);
    }

    const size_t edge = 4096 * 30 + 2112; /* block 29 of frame 1 ends exactly a margin early */
    const struct {
        size_t off, len;
    } ranges[] = {{0, total},
                  {1, total - 1},
                  {0, n1},
                  {0, edge},
                  {0, edge - 1},
                  {4096, edge - 4096},
                  {n1 - 5000, 70000},
                  {n1 + 65536 * 3 - 7, 65536 * 4 + 2112},
                  {n1 + n2 - 2111, n3 + 2111},
                  {total - n3 - 3000, 3000 + 2112}};
    static const int threads[] = {1, 2, 3, 8, 64};
    for (size_t r = 0; ok && r < sizeof(ranges) / sizeof(ranges[0]); r++) {
        const size_t off = ranges[r].off, len = ranges[r].len;
        for (size_t t = 0; ok && t < sizeof(threads) / sizeof(threads[0]); t++) {
            for (int f = 0; ok && f < 2; f++) {
                zxc_seekable* const s = f ? sf : sb;
                memset(out, 0xA5, len + guard);
                const int64_t got =
                    zxc_seekable_decompress_range_mt(s, out, len + guard, off, len, threads[t]);
                int intact = 1;
                for (size_t g = 0; g < guard; g++) intact &= out[len + g] == 0xA5;
                if (got != (int64_t)len || memcmp(out, src + off, len) != 0 || !intact) {
                    printf("  [FAIL] range [%zu, +%zu), %d threads, %s: %lld%s\n", off, len,
                           threads[t], f ? "FILE*" : "buffer", (long long)got,
                           intact ? "" : ", wrote past len");
                    ok = 0;
                }
            }
        }
    }
    zxc_seekable_free(sb);
    zxc_seekable_free(sf);
    if (fp) fclose(fp);

    /* The stored checksum of frame 2's first block flipped: every path, buffer
     * and FILE*, reports it. */
    if (ok) {
        const uint64_t b0 = (n1 + 4095) / 4096; /* archive-wide index of that block */
        zxc_seekable* const probe = zxc_seekable_open(m.p, m.n);
        const uint32_t csz = probe ? zxc_seekable_get_block_comp_size(probe, b0) : 0;
        zxc_seekable_free(probe);
        if (csz) m.p[f2_at + ZXC_FILE_HEADER_SIZE + csz - 1] ^= 0x40;
        FILE* const fc = tmpfile();
        ok = csz && fc && fwrite(m.p, 1, m.n, fc) == m.n && fflush(fc) == 0;
        zxc_seekable* const cs[2] = {ok ? zxc_seekable_open(m.p, m.n) : NULL,
                                     ok ? zxc_seekable_open_file(fc) : NULL};
        for (int f = 0; ok && f < 2; f++) {
            if (!cs[f]) {
                printf("  [FAIL] open after corruption, %s\n", f ? "FILE*" : "buffer");
                ok = 0;
                break;
            }
            zxc_seekable_set_checksum(cs[f], 1);
            for (size_t t = 0; ok && t < sizeof(threads) / sizeof(threads[0]); t++) {
                const int64_t got =
                    zxc_seekable_decompress_range_mt(cs[f], out, total, 0, total, threads[t]);
                if (got != ZXC_ERROR_BAD_CHECKSUM) {
                    printf("  [FAIL] corrupted checksum, %d threads, %s: %lld\n", threads[t],
                           f ? "FILE*" : "buffer", (long long)got);
                    ok = 0;
                }
            }
        }
        zxc_seekable_free(cs[0]);
        zxc_seekable_free(cs[1]);
        if (fc) fclose(fc);
    }

    free(m.p);
    free(out);
    free(src);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}
