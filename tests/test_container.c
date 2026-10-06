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

/* Decodes @p arc every way; each must return @p want and, on success, produce
 * @p expect. The size queries must report @p want on success, nothing on failure.
 * A frame cut short is an I/O error to the FILE* decoder, which reads it as a
 * short read: @p want_file says what it returns. */
static int check2(const char* what, const uint8_t* arc, const size_t n,
                  const zxc_decompress_opts_t* opts, const int64_t want, const int64_t want_file,
                  const uint8_t* expect) {
    // Room for every frame before the fault, so the verdict is the fault's.
    const size_t cap = (want > 0 ? (size_t)want : 0) + n * 8 + (1 << 20);
    uint8_t* const o1 = malloc(cap);
    uint8_t* const o2 = malloc(cap);
    uint8_t* const o3 = malloc(cap);
    zxc_dctx* const dctx = zxc_create_dctx();
    int ok = o1 && o2 && o3 && dctx;
    if (!ok) printf("  [FAIL] %s: allocation\n", what);

    const int64_t r1 = ok ? zxc_decompress(arc, n, o1, cap, opts) : -1;
    const int64_t r2 = ok ? zxc_decompress_dctx(dctx, arc, n, o2, cap, opts) : -1;
    int64_t sq = -1;
    const int64_t r3 = ok ? file_decode(arc, n, o3, cap, opts, &sq) : -1;
    const uint64_t bq = zxc_get_decompressed_size(arc, n);

    if (ok && (r1 != want || r2 != want || r3 != want_file)) {
        printf("  [FAIL] %s: buffer %lld, dctx %lld, FILE* %lld (want %lld, %lld)\n", what,
               (long long)r1, (long long)r2, (long long)r3, (long long)want, (long long)want_file);
        ok = 0;
    }
    if (ok && want > 0 &&
        (memcmp(o1, expect, (size_t)want) || memcmp(o2, expect, (size_t)want) ||
         memcmp(o3, expect, (size_t)want))) {
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
    return ok;
}

static int check(const char* what, const uint8_t* arc, const size_t n,
                 const zxc_decompress_opts_t* opts, const int64_t want, const uint8_t* expect) {
    return check2(what, arc, n, opts, want, want, expect);
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
                      ZXC_ERROR_IO, NULL);
    for (size_t extra = 1; ok && extra <= 3; extra++) {
        const uint8_t z[3] = {0xF5, 0x2E, 0xB0}; /* a cut magic word */
        cbuf_t t = {0};
        ok = cb_put(&t, b.p, b.n) && cb_put(&t, z, extra);
        ok = ok &&
             check("cut magic after the last footer", t.p, t.n, NULL, ZXC_ERROR_CORRUPT_DATA, NULL);
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

/* Two seekable frames back to back are no seekable archive. Open reads only the
 * header, footer and EOF/SEK headers (tables are checked on access), so it may
 * succeed; reading the whole range touches the last group, whose end misses the
 * EOF block, and must fail rather than return the first frame's bytes. */
int test_container_seekable_refused(void) {
    printf("=== TEST: Container - seekable reader refuses concatenated frames ===\n");
    const size_t n = 5 * 4096;
    uint8_t* const src = malloc(n);
    uint8_t* const out = malloc(n);
    int ok = src && out;
    if (ok) gen_lz_data(src, n);
    const zxc_compress_opts_t co = {.level = 3, .block_size = 4096, .seekable = 1};
    cbuf_t b = {0};
    ok = ok && cb_frame(&b, src, n, &co) && cb_frame(&b, src, n, &co);
    zxc_seekable* const s = ok ? zxc_seekable_open(b.p, b.n) : NULL;
    const int64_t r = s ? zxc_seekable_decompress_range(s, out, n, 0, n) : ZXC_ERROR_CORRUPT_DATA;
    if (ok && r >= 0) {
        printf("  [FAIL] seekable read of two frames returned %lld\n", (long long)r);
        ok = 0;
    }
    zxc_seekable_free(s);
    free(b.p);
    free(out);
    free(src);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}
