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

static int cb_frame(cbuf_t* b, const uint8_t* src, const size_t n, const zxc_compress_opts_t* co) {
    const size_t cap = (size_t)zxc_compress_bound(n);
    uint8_t* const arc = malloc(cap);
    const int64_t len = arc ? zxc_compress(src, n, arc, cap, co) : -1;
    const int ok = len > 0 && cb_put(b, arc, (size_t)len);
    free(arc);
    return ok;
}

/* FILE* decode of @p arc; writes the output to @p out (capacity @p cap). */
static int64_t file_decode(const uint8_t* arc, const size_t n, uint8_t* out, const size_t cap,
                           const zxc_decompress_opts_t* opts, int64_t* size_query) {
    FILE* const fi = tmpfile();
    FILE* const fo = tmpfile();
    int64_t r = -1;
    *size_query = -1;
    if (fi && fo && fwrite(arc, 1, n, fi) == n && fseek(fi, 0, SEEK_SET) == 0) {
        *size_query = zxc_stream_get_decompressed_size(fi);
        r = zxc_stream_decompress(fi, fo, opts);
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
                           const zxc_decompress_opts_t* opts) {
    zxc_dstream* const ds = zxc_dstream_create(opts);
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

/* Decodes @p arc every way; each must return @p want (@p want_file for the FILE*
 * decoder, which sees a cut frame as a short read; @p want_push for the push one,
 * which cannot tell input that stopped from input still coming) and produce
 * @p expect. The size queries report @p want on success, nothing on failure. */
static int check2(const char* what, const uint8_t* arc, const size_t n,
                  const zxc_decompress_opts_t* opts, const int64_t want, const int64_t want_file,
                  const int64_t want_push, const uint8_t* expect) {
    // Room for every frame before the fault, so the verdict is the fault's.
    const size_t cap = (want > 0 ? (size_t)want : 0) + n * 8 + (1 << 20);
    uint8_t* const o1 = malloc(cap);
    uint8_t* const o2 = malloc(cap);
    uint8_t* const o3 = malloc(cap);
    uint8_t* const o4 = malloc(cap);
    zxc_dctx* const dctx = zxc_create_dctx();
    int ok = o1 && o2 && o3 && o4 && dctx;
    if (!ok) printf("  [FAIL] %s: allocation\n", what);

    const int64_t r1 = ok ? zxc_decompress(arc, n, o1, cap, opts) : -1;
    const int64_t r2 = ok ? zxc_decompress_dctx(dctx, arc, n, o2, cap, opts) : -1;
    int64_t sq = -1;
    const int64_t r3 = ok ? file_decode(arc, n, o3, cap, opts, &sq) : -1;
    const int64_t r4 = ok ? push_decode(arc, n, o4, cap, opts) : -1;
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
    zxc_free_dctx(dctx);
    free(o1);
    free(o2);
    free(o3);
    free(o4);
    return ok;
}

static int check(const char* what, const uint8_t* arc, const size_t n,
                 const zxc_decompress_opts_t* opts, const int64_t want, const uint8_t* expect) {
    return check2(what, arc, n, opts, want, want, want, expect);
}

int test_container_concat(void) {
    printf("=== TEST: Container - concatenated frames ===\n");
    const size_t na = 3 * 4096 + 17, nb = 70000, nc = 1000;
    uint8_t* const src = malloc(na + nb + nc);
    cbuf_t b = {0};
    int ok = src != NULL;
    if (ok) gen_lz_data(src, na + nb + nc);

    /* Mixed geometry: small blocks with checksums, a seek table, an empty frame. */
    const zxc_compress_opts_t ca = {.level = 3, .block_size = 4096, .checksum_enabled = 1};
    const zxc_compress_opts_t cb = {.level = 5, .seekable = 1, .checksum_enabled = 1};
    const zxc_compress_opts_t cc = {.level = 1, .block_size = 8192, .seekable = 1};
    ok = ok && cb_frame(&b, src, na, &ca) && cb_frame(&b, NULL, 0, &cc) &&
         cb_frame(&b, src + na, nb, &cb) && cb_frame(&b, src + na + nb, nc, &cc);
    const zxc_decompress_opts_t verify = {.checksum_enabled = 1};
    ok = ok && check("four frames", b.p, b.n, NULL, (int64_t)(na + nb + nc), src);
    ok = ok && check("four frames, checksums", b.p, b.n, &verify, (int64_t)(na + nb + nc), src);

    /* A destination that ends inside the second payload. */
    if (ok) {
        uint8_t* const out = malloc(na + 100);
        const int64_t r = out ? zxc_decompress(b.p, b.n, out, na + 100, NULL) : -1;
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
        const int64_t r = buf ? zxc_decompress_inplace(buf, cap, b.n, NULL) : 0;
        if (r >= 0) {
            printf("  [FAIL] in-place decode of a multi-frame input: %lld\n", (long long)r);
            ok = 0;
        }
        free(buf);
    }

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
    const size_t n = 3 * 4096 + 17, nbig = 300000;
    uint8_t* const src = malloc(5 * n + 2 * nbig);
    cbuf_t b = {0};
    size_t ends[7] = {0};
    int ok = src != NULL;
    if (ok) gen_lz_data(src, 5 * n + 2 * nbig);

    // Three small frames, two with large blocks (positioned reads), two small again.
    const zxc_compress_opts_t small = {.level = 3, .block_size = 4096, .checksum_enabled = 1};
    const zxc_compress_opts_t big = {.level = 2, .block_size = 65536, .checksum_enabled = 1};
    size_t at = 0;
    for (int i = 0; ok && i < 7; i++) {
        const int is_big = i == 3 || i == 4;
        const size_t len = is_big ? nbig : n;
        ok = cb_frame(&b, src + at, len, is_big ? &big : &small);
        at += len;
        ends[i] = b.n;
    }
    const zxc_decompress_opts_t verify = {.checksum_enabled = 1, .n_threads = 4};
    ok = ok && check("seven frames, three runs", b.p, b.n, &verify, (int64_t)at, src);

    // Positioned reads leave the FILE* where the input ends.
    if (ok) {
        FILE* const fi = tmpfile();
        int64_t r = -1;
        long pos = -1;
        if (fi && fwrite(b.p, 1, b.n, fi) == b.n && fseek(fi, 0, SEEK_SET) == 0) {
            r = zxc_stream_decompress(fi, NULL, &verify);
            pos = ftell(fi);
        }
        if (r != (int64_t)at || pos != (long)b.n) {
            printf("  [FAIL] input position: decoded %lld, at %ld (want %zu)\n", (long long)r, pos,
                   b.n);
            ok = 0;
        }
        if (fi) fclose(fi);
    }

    // Progress reaches the total across frames and engine restarts.
    if (ok) {
        uint64_t seen = 0;
        const zxc_decompress_opts_t po = {
            .n_threads = 4, .progress_cb = max_progress, .user_data = &seen};
        uint8_t* const out = malloc(at);
        int64_t sq = 0;
        const int64_t r = out ? file_decode(b.p, b.n, out, at, &po, &sq) : -1;
        if (r != (int64_t)at || seen != (uint64_t)at) {
            printf("  [FAIL] progress: decoded %lld, last progress %llu (want %zu)\n", (long long)r,
                   (unsigned long long)seen, at);
            ok = 0;
        }
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
                   t.n, &verify, k ? ZXC_ERROR_BAD_CHECKSUM : ZXC_ERROR_CORRUPT_DATA, NULL);
        free(t.p);
    }

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
    const zxc_compress_opts_t opts[] = {{.level = 3, .block_size = 4096, .checksum_enabled = 1},
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
    const zxc_compress_opts_t opts[] = {
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
        x = x * 1103515245u + 12345u;
        const char* w = words[(x >> 16) % 12];
        while (*w && i < 80000) text[i++] = (uint8_t)*w++;
    }
    for (int t = 0; ok && t < 2; t++) {
        const zxc_compress_opts_t s7 = {.level = 7, .block_size = 4096, .seekable = 1};
        const zxc_compress_opts_t l7 = {.level = 7, .seekable = 1};
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
        const zxc_compress_opts_t so = {.level = 3, .block_size = 4096, .seekable = 1};
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
        const zxc_compress_opts_t so = {.level = 1, .seekable = 1};
        uint8_t one[128];
        const int64_t fl = zxc_compress("x", 1, one, sizeof(one), &so);
        const size_t count = ((size_t)1 << 20) + 1;
        uint8_t* const many = fl > 0 ? malloc(count * (size_t)fl) : NULL;
        for (size_t i = 0; many && i < count; i++) memcpy(many + i * (size_t)fl, one, (size_t)fl);
        zxc_seekable* const s = many ? zxc_seekable_open(many, count * (size_t)fl) : NULL;
        zxc_seekable* const s1 = many ? zxc_seekable_open(many, (size_t)fl) : NULL;
        if (!many || s || !s1) {
            printf("  [FAIL] 2^20 + 1 frames: opened %d, one frame opened %d\n", s != NULL,
                   s1 != NULL);
            ok = 0;
        }
        zxc_seekable_free(s);
        zxc_seekable_free(s1);
        free(many);
    }

    /* A frame without a table refuses the open. */
    if (ok) {
        const zxc_compress_opts_t plain = {.level = 3};
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
    const zxc_compress_opts_t dopts[] = {
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
