/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Memory-mapped API: zero-copy in-place decode of an on-disk archive
 * (zxc_decompress_mmap / _fd) and the read-only archive mapping
 * (zxc_mmap_open / _fd).
 *
 * The interesting inputs are the high-entropy ones: their archive is nearly as
 * large as the payload, so the flush-right file mapping lands INSIDE the output
 * region and the write cursor sweeps straight through the compressed bytes -
 * the invariant the whole design rests on.
 */

#include "../include/zxc_mmap.h"
#include "test_common.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#ifdef _MSC_VER
#define zxc_test_fileno _fileno
#else
#define zxc_test_fileno fileno
#endif

/* Distinct per-test file names: the suite runs its cases in parallel. */
#define MMAP_RT_PATH "zxc_mmap_roundtrip.bin"
#define MMAP_ERR_PATH "zxc_mmap_errors.bin"
#define MMAP_DICT_PATH "zxc_mmap_dict.bin"

/* Writes `n` bytes to `path` in binary mode. Returns 1 on success. */
static int write_whole_file(const char* const path, const void* const data, const size_t n) {
    FILE* const f = fopen(path, "wb");
    if (!f) return 0;
    const size_t w = n ? fwrite(data, 1, n, f) : 0;
    return (fclose(f) == 0) && w == n;
}

/* Whether losing the zero-copy placement is a failure or merely a note.
 *
 * The placement IS the feature and the decode returns the right bytes either
 * way, so nothing else here would catch its loss. POSIX has no copying route,
 * hence the unconditional 1. On Windows the fallback is legitimate before
 * Win10 1803, so the answer comes from the OS -- the placement is available
 * exactly when the two placeholder entry points resolve. An opt-in env var
 * would have to be set by every job to assert anything at all. */
static int zerocopy_required(void) {
#if defined(_WIN32)
    /* An explicit answer still wins: a file that cannot back a copy-on-write
     * section is a legitimate fallback. */
    const char* const env = getenv("ZXC_TEST_REQUIRE_ZEROCOPY");
    if (env != NULL && env[0] != '\0') return env[0] != '0';

    HMODULE mod = GetModuleHandleW(L"kernelbase.dll");
    if (!mod) mod = GetModuleHandleW(L"kernel32.dll");
    if (!mod) return 0;
    return GetProcAddress(mod, "VirtualAlloc2") != NULL &&
           GetProcAddress(mod, "MapViewOfFile3") != NULL;
#else
    return 1;
#endif
}

/* One I/O-failure assertion. Chained with ||, the first failure would hide the
 * rest and skip the calls whose map still needs closing -- an unexpected
 * SUCCESS hands back a live region, hence the close on mismatch. */
static int expect_io(const char* const label, const int64_t got, zxc_map_t* const m) {
    if (got == ZXC_ERROR_IO) return 1;
    printf("Failed [%s]: expected ZXC_ERROR_IO, got %lld\n", label, (long long)got);
    zxc_mmap_close(m);
    return 0;
}

/* A closed map must be inert: cleared fields, and a second close is a no-op. */
static int check_closed(const char* const label, zxc_map_t* const m) {
    zxc_mmap_close(m);
    if (m->data != NULL || m->size != 0 || zxc_mmap_is_zerocopy(m)) {
        printf("Failed [%s]: close left data=%p size=%zu\n", label, (void*)m->data, m->size);
        return 0;
    }
    zxc_mmap_close(m); /* idempotent */
    return 1;
}

/* Compress `orig`, write the archive to disk, then decode it back through the
 * mapped path API, the mapped fd API, and a read-only mapping fed to
 * zxc_decompress. */
static int mmap_case(const char* const label, const uint8_t* const orig, const size_t n,
                     const int level, const int checksum, const size_t block_size,
                     const int seekable) {
    const size_t cbound = (size_t)zxc_compress_bound(n);
    uint8_t* const comp = (uint8_t*)malloc(cbound);
    if (!comp) return 0;

    const zxc_compress_opts_t co = {.level = level,
                                    .checksum_enabled = checksum,
                                    .block_size = block_size,
                                    .seekable = seekable};
    const int64_t c = zxc_compress(orig, n, comp, cbound, &co);
    if (c <= 0) {
        printf("Failed [%s]: compress -> %lld\n", label, (long long)c);
        free(comp);
        return 0;
    }
    const size_t csz = (size_t)c;

    if (!write_whole_file(MMAP_RT_PATH, comp, csz)) {
        printf("Failed [%s]: cannot write " MMAP_RT_PATH "\n", label);
        remove(MMAP_RT_PATH);
        free(comp);
        return 0;
    }

    const zxc_decompress_opts_t dop = {.checksum_enabled = checksum};
    int ok = 1;

    /* 1. Path API: one call, one mapping, no output allocation. */
    zxc_map_t m;
    const int64_t d = zxc_decompress_mmap(MMAP_RT_PATH, &m, &dop);
    /* 1 everywhere but pre-1803 Windows, which copies the archive in. */
    const int zero_copy = zxc_mmap_is_zerocopy(&m);
    if (d > 0 && !zero_copy) {
        printf("  [INFO] no zero-copy placement here: the archive was copied into the region\n");
        if (zerocopy_required()) {
            printf("Failed [%s]: zero-copy placement lost\n", label);
            ok = 0;
        }
    }
    if (d < 0 || (size_t)d != n || m.size != n || !m.data || memcmp(m.data, orig, n) != 0) {
        printf("Failed [%s]: decompress_mmap ret=%lld want=%zu%s\n", label, (long long)d, n,
               (d == (int64_t)n) ? " (MISMATCH)" : "");
        ok = 0;
    } else {
        /* The region is the caller's to write into (private mapping). */
        ((uint8_t*)m.data)[0] ^= 0xFFu;
        ((uint8_t*)m.data)[n - 1] ^= 0xFFu;
    }
    ok &= check_closed(label, &m);

    /* 2. Same through a caller-owned descriptor. */
    FILE* const f = fopen(MMAP_RT_PATH, "rb");
    if (!f) {
        printf("Failed [%s]: reopen\n", label);
        remove(MMAP_RT_PATH);
        free(comp);
        return 0;
    }
    /* The archive on disk must be untouched, even though decode #1 wrote over
     * the compressed bytes it had already consumed (private mapping / COW). */
    uint8_t* const reread = (uint8_t*)malloc(csz);
    if (reread) {
        const size_t got = fread(reread, 1, csz, f);
        if (got != csz || memcmp(reread, comp, csz) != 0) {
            printf("Failed [%s]: archive modified on disk by the in-place decode\n", label);
            ok = 0;
        }
        free(reread);
    }

    zxc_map_t mf;
    const int64_t df = zxc_decompress_mmap_fd(zxc_test_fileno(f), &mf, &dop);
    if (df < 0 || (size_t)df != n || !mf.data || memcmp(mf.data, orig, n) != 0) {
        printf("Failed [%s]: decompress_mmap_fd ret=%lld want=%zu\n", label, (long long)df, n);
        ok = 0;
    }
    ok &= check_closed(label, &mf);

    /* 3. Read-only archive mapping: the mapped bytes ARE the file's bytes, and
     *    they feed the ordinary buffer API with no input copy. */
    zxc_map_t ma;
    const int rc = zxc_mmap_open_fd(zxc_test_fileno(f), &ma);
    fclose(f); /* the mapping outlives the descriptor */
    if (rc != ZXC_OK || ma.size != csz || !ma.data || !zxc_mmap_is_zerocopy(&ma) ||
        memcmp(ma.data, comp, csz) != 0) {
        printf("Failed [%s]: mmap_open_fd rc=%d size=%zu want=%zu\n", label, rc, ma.size, csz);
        ok = 0;
    } else {
        uint8_t* const out = (uint8_t*)malloc(n ? n : 1);
        if (out) {
            const int64_t dv = zxc_decompress(ma.data, ma.size, out, n, &dop);
            if (dv != (int64_t)n || memcmp(out, orig, n) != 0) {
                printf("Failed [%s]: decompress from view ret=%lld\n", label, (long long)dv);
                ok = 0;
            }
            free(out);
        }
    }
    ok &= check_closed(label, &ma);

    /* 4. Path-based read-only mapping. */
    zxc_map_t mp;
    if (zxc_mmap_open(MMAP_RT_PATH, &mp) != ZXC_OK || mp.size != csz) {
        printf("Failed [%s]: mmap_open\n", label);
        ok = 0;
    }
    ok &= check_closed(label, &mp);

    remove(MMAP_RT_PATH);
    free(comp);
    if (ok)
        printf("  [PASS] %s (n=%zu, comp=%zu, %s, %s)\n", label, n, csz,
               csz > n ? "archive spans the whole region" : "archive in margin",
               zero_copy ? "zero-copy" : "archive copied in");
    return ok;
}

int test_mmap_roundtrip(void) {
    printf("=== TEST: Unit - Mapped zero-copy in-place decode ===\n");
    if (!zxc_mmap_supported()) {
        printf("  [SKIP] no file mapping on this platform\n");
        printf("PASS\n\n");
        return 1;
    }

    const size_t N = 2 * 1024 * 1024;
    uint8_t* const a = (uint8_t*)malloc(N);
    if (!a) return 0;
    int ok = 1;

    gen_lz_data(a, N);
    ok &= mmap_case("compressible L3", a, N, 3, 1, 0, 0);
    ok &= mmap_case("compressible L6", a, N, 6, 0, 0, 0);

    /* High-entropy: comp_size ~ N, so the flush-right archive overlaps the
     * output region and the decoder writes over bytes it has already read. */
    gen_random_data(a, N);
    ok &= mmap_case("random L1 (overlap)", a, N, 1, 0, 0, 0);
    ok &= mmap_case("random L7 (overlap)", a, N, 7, 1, 0, 0);

    /* Multi-block, and a size that is not a page multiple. */
    gen_binary_data(a, N);
    ok &= mmap_case("binary 1 MiB+1", a, 1024 * 1024 + 1, 3, 1, 0, 0);
    ok &= mmap_case("binary 4 KiB-3", a, 4093, 3, 0, 0, 0);
    ok &= mmap_case("tiny 1 byte", a, 1, 3, 1, 0, 0);

    /* Seekable archives put a seek table between the EOF block and the footer;
     * those bytes push the flush-right archive left, so the in-place bound has
     * to reserve them (it did not, and the mapped path silently lost its
     * margin). Worst shape: many small blocks over incompressible data. */
    free(a);
    uint8_t* const b = (uint8_t*)malloc(8 * 1024 * 1024);
    if (!b) return 0;
    gen_random_data(b, 8 * 1024 * 1024);
    ok &= mmap_case("seekable random, 4K blocks", b, 8 * 1024 * 1024, 1, 1, ZXC_BLOCK_SIZE_MIN, 1);
    gen_lz_data(b, 8 * 1024 * 1024);
    ok &= mmap_case("seekable text, 4K blocks", b, 8 * 1024 * 1024, 3, 1, ZXC_BLOCK_SIZE_MIN, 1);
    free(b);

    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Dictionary archives take the bounce-buffer route inside the decoder (the
 * bounce buffer does not alias the mapped region), so they need their own pass
 * through the mapped entry point - including the "dict missing" rejection. */
int test_mmap_dict(void) {
    printf("=== TEST: Unit - Mapped decode of a dictionary archive ===\n");
    if (!zxc_mmap_supported()) {
        printf("  [SKIP] no file mapping on this platform\n");
        printf("PASS\n\n");
        return 1;
    }

    static const uint8_t dict[] =
        "The quick brown fox jumps over the lazy dog. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Pack my box with five dozen liquor jugs.";
    const size_t dict_size = sizeof(dict) - 1;

    const size_t n = 64 * 1024;
    uint8_t* const src = (uint8_t*)malloc(n);
    uint8_t* const comp = (uint8_t*)malloc((size_t)zxc_compress_bound(n));
    if (!src || !comp) {
        free(src);
        free(comp);
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        src[i] = (i % 7 < 5) ? dict[(i * 31) % (dict_size - 5) + (i % 5)] : (uint8_t)(i ^ (i >> 8));
    }

    const zxc_compress_opts_t co = {
        .level = 3, .checksum_enabled = 1, .dict = dict, .dict_size = dict_size};
    const int64_t c = zxc_compress(src, n, comp, (size_t)zxc_compress_bound(n), &co);
    int ok = c > 0 && write_whole_file(MMAP_DICT_PATH, comp, (size_t)c);
    if (!ok) printf("Failed: dict compress/write (%lld)\n", (long long)c);

    if (ok) {
        const zxc_decompress_opts_t dop = {
            .checksum_enabled = 1, .dict = dict, .dict_size = dict_size};
        zxc_map_t m;
        const int64_t d = zxc_decompress_mmap(MMAP_DICT_PATH, &m, &dop);
        if (d != (int64_t)n || !m.data || memcmp(m.data, src, n) != 0) {
            printf("Failed: dict mapped decode ret=%lld want=%zu\n", (long long)d, n);
            ok = 0;
        }
        zxc_mmap_close(&m);

        /* Same archive without the dictionary must be refused, not decoded. */
        const int64_t nd = zxc_decompress_mmap(MMAP_DICT_PATH, &m, NULL);
        if (nd != ZXC_ERROR_DICT_REQUIRED) {
            printf("Failed: missing dict -> %lld (want %d)\n", (long long)nd,
                   ZXC_ERROR_DICT_REQUIRED);
            ok = 0;
        }
        zxc_mmap_close(&m);
        if (ok) printf("  [PASS] dict archive (n=%zu, comp=%lld)\n", n, (long long)c);
    }

    remove(MMAP_DICT_PATH);
    free(src);
    free(comp);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

int test_mmap_errors(void) {
    printf("=== TEST: Unit - Mapped decode error paths ===\n");
    if (!zxc_mmap_supported()) {
        printf("  [SKIP] no file mapping on this platform\n");
        printf("PASS\n\n");
        return 1;
    }

    int ok = 1;
    zxc_map_t m;

    /* NULL out-param is rejected before anything is opened. */
    if (zxc_decompress_mmap(MMAP_ERR_PATH, NULL, NULL) != ZXC_ERROR_NULL_INPUT ||
        zxc_mmap_open(MMAP_ERR_PATH, NULL) != ZXC_ERROR_NULL_INPUT) {
        printf("Failed: NULL out-param not rejected\n");
        ok = 0;
    }

    /* NULL path, with the map still cleared so closing it is safe. */
    m.data = (void*)0x1;
    m.map_base = (void*)0x1;
    if (zxc_decompress_mmap(NULL, &m, NULL) != ZXC_ERROR_NULL_INPUT || m.data != NULL ||
        m.map_base != NULL) {
        printf("Failed: NULL path not rejected / map not cleared\n");
        ok = 0;
    }
    zxc_mmap_close(&m);

    /* Missing file, and a bad descriptor. */
    if (!expect_io("missing file, decompress",
                   zxc_decompress_mmap("zxc_no_such_archive.bin", &m, NULL), &m))
        ok = 0;
    if (!expect_io("missing file, open", zxc_mmap_open("zxc_no_such_archive.bin", &m), &m)) ok = 0;
    if (!expect_io("bad fd, decompress", zxc_decompress_mmap_fd(-1, &m, NULL), &m)) ok = 0;
    if (!expect_io("bad fd, open", zxc_mmap_open_fd(-1, &m), &m)) ok = 0;

    /* Too short to hold a frame. */
    static const uint8_t stub[8] = {0};
    if (!write_whole_file(MMAP_ERR_PATH, stub, sizeof(stub))) {
        printf("Failed: cannot write stub\n");
        remove(MMAP_ERR_PATH);
        return 0;
    }
    if (zxc_decompress_mmap(MMAP_ERR_PATH, &m, NULL) != ZXC_ERROR_SRC_TOO_SMALL) {
        printf("Failed: short file not rejected\n");
        ok = 0;
    }

    /* Right length, wrong contents. */
    uint8_t junk[128];
    memset(junk, 0x5A, sizeof(junk));
    if (!write_whole_file(MMAP_ERR_PATH, junk, sizeof(junk))) {
        printf("Failed: cannot write junk\n");
        remove(MMAP_ERR_PATH);
        return 0;
    }
    const int64_t junk_rc = zxc_decompress_mmap(MMAP_ERR_PATH, &m, NULL);
    if (junk_rc != ZXC_ERROR_BAD_MAGIC) {
        printf("Failed: junk archive -> %lld, expected ZXC_ERROR_BAD_MAGIC\n", (long long)junk_rc);
        ok = 0;
    }

    /* Forged footer: the header is intact, so the verdict must blame the data. */
    uint8_t forged[256];
    const int64_t fc = zxc_compress(junk, sizeof(junk), forged, sizeof(forged), NULL);
    if (fc <= 0) {
        printf("Failed: cannot build archive to forge\n");
        ok = 0;
    } else {
        memset(forged + fc - ZXC_FILE_FOOTER_SIZE, 0xFF, 8); /* dsize = UINT64_MAX */
        if (!write_whole_file(MMAP_ERR_PATH, forged, (size_t)fc)) {
            printf("Failed: cannot write forged archive\n");
            ok = 0;
        } else {
            const int64_t d = zxc_decompress_mmap(MMAP_ERR_PATH, &m, NULL);
            if (d != ZXC_ERROR_CORRUPT_DATA) {
                printf("Failed: forged footer -> %lld, expected ZXC_ERROR_CORRUPT_DATA\n",
                       (long long)d);
                ok = 0;
            }
            zxc_mmap_close(&m);
        }
    }

    /* Empty frame: succeeds with nothing to hand back. */
    uint8_t empty[64];
    const int64_t ec = zxc_compress(NULL, 0, empty, sizeof(empty), NULL);
    if (ec <= 0 || !write_whole_file(MMAP_ERR_PATH, empty, (size_t)ec)) {
        printf("Failed: cannot build empty frame\n");
        ok = 0;
    } else {
        const int64_t d = zxc_decompress_mmap(MMAP_ERR_PATH, &m, NULL);
        if (d != 0 || m.data != NULL || m.size != 0) {
            printf("Failed: empty frame -> %lld (data=%p)\n", (long long)d, (void*)m.data);
            ok = 0;
        }
        zxc_mmap_close(&m);
    }

    /* Empty file: nothing to map. */
    if (!write_whole_file(MMAP_ERR_PATH, NULL, 0)) {
        printf("Failed: cannot truncate\n");
        ok = 0;
    } else if (zxc_mmap_open(MMAP_ERR_PATH, &m) != ZXC_ERROR_SRC_TOO_SMALL) {
        printf("Failed: empty file not rejected\n");
        ok = 0;
    }

    /* A directory is not mappable. */
    if (zxc_mmap_open(".", &m) != ZXC_ERROR_IO) {
        printf("Failed: directory not rejected\n");
        ok = 0;
    }

    if (zxc_mmap_is_zerocopy(NULL)) {
        printf("Failed: is_zerocopy(NULL) != 0\n");
        ok = 0;
    }

    /* zxc_mmap_close tolerates NULL and a zeroed map. */
    zxc_mmap_close(NULL);
    memset(&m, 0, sizeof(m));
    zxc_mmap_close(&m);

    remove(MMAP_ERR_PATH);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}
