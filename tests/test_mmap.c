/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Memory-mapped API: read-only whole-file mappings (zxc_mmap_open / _fd) and
 * the composition that motivates them -- the mapped bytes fed straight to the
 * buffer API, with no input copy.
 */

#include "../include/zxc_mmap.h"
#include "test_common.h"

#ifdef _MSC_VER
#define zxc_test_fileno _fileno
#else
#define zxc_test_fileno fileno
#endif

/* Distinct per-test file names: the suite runs its cases in parallel. */
#define MMAP_RT_PATH "zxc_mmap_roundtrip.bin"
#define MMAP_ERR_PATH "zxc_mmap_errors.bin"

/* Writes `n` bytes to `path` in binary mode. Returns 1 on success. */
static int write_whole_file(const char* const path, const void* const data, const size_t n) {
    FILE* const f = fopen(path, "wb");
    if (!f) return 0;
    const size_t w = n ? fwrite(data, 1, n, f) : 0;
    return (fclose(f) == 0) && w == n;
}

/* One I/O-failure assertion. Chained with ||, the first failure would hide the
 * rest and skip the calls whose map still needs closing -- an unexpected
 * SUCCESS hands back a live mapping, hence the close on mismatch. */
static int expect_io(const char* const label, const int got, zxc_map_t* const m) {
    if (got == ZXC_ERROR_IO) return 1;
    printf("Failed [%s]: expected ZXC_ERROR_IO, got %d\n", label, got);
    zxc_mmap_close(m);
    return 0;
}

/* A closed map must be inert: cleared fields, and a second close is a no-op. */
static int check_closed(const char* const label, zxc_map_t* const m) {
    zxc_mmap_close(m);
    if (m->data != NULL || m->size != 0) {
        printf("Failed [%s]: close left data=%p size=%zu\n", label, (const void*)m->data, m->size);
        return 0;
    }
    zxc_mmap_close(m); /* idempotent */
    return 1;
}

/* Compress `orig`, write the archive to disk, then map it back -- by fd and by
 * path -- and check the mapped bytes are the file's bytes and decode correctly
 * through the ordinary buffer API. */
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

    /* 1. By descriptor. The mapping outlives it, and the file position the
     *    caller left behind is untouched. */
    FILE* const f = fopen(MMAP_RT_PATH, "rb");
    if (!f) {
        printf("Failed [%s]: cannot reopen archive\n", label);
        remove(MMAP_RT_PATH);
        free(comp);
        return 0;
    }
    zxc_map_t ma;
    const int rc = zxc_mmap_open_fd(zxc_test_fileno(f), &ma);
    const long pos = ftell(f);
    fclose(f);
    if (rc != ZXC_OK || ma.size != csz || !ma.data || memcmp(ma.data, comp, csz) != 0) {
        printf("Failed [%s]: mmap_open_fd rc=%d size=%zu want=%zu\n", label, rc, ma.size, csz);
        ok = 0;
    } else if (pos != 0) {
        printf("Failed [%s]: mmap_open_fd moved the file position to %ld\n", label, pos);
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

    /* 2. By path, and the size the footer advertises is readable through it. */
    zxc_map_t mp;
    if (zxc_mmap_open(MMAP_RT_PATH, &mp) != ZXC_OK || mp.size != csz) {
        printf("Failed [%s]: mmap_open\n", label);
        ok = 0;
    } else if (zxc_get_decompressed_size(mp.data, mp.size) != (uint64_t)n) {
        printf("Failed [%s]: get_decompressed_size through the mapping\n", label);
        ok = 0;
    }
    ok &= check_closed(label, &mp);

    remove(MMAP_RT_PATH);
    free(comp);
    if (ok) printf("  [PASS] %s (n=%zu, comp=%zu)\n", label, n, csz);
    return ok;
}

int test_mmap_open(void) {
    printf("=== TEST: Unit - Read-only archive mapping ===\n");
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

    gen_random_data(a, N);
    ok &= mmap_case("random L1", a, N, 1, 0, 0, 0);

    /* Multi-block, and sizes that are not page multiples. */
    gen_binary_data(a, N);
    ok &= mmap_case("binary 1 MiB+1", a, 1024 * 1024 + 1, 3, 1, 0, 0);
    ok &= mmap_case("binary 4 KiB-3", a, 4093, 3, 0, 0, 0);
    ok &= mmap_case("tiny 1 byte", a, 1, 3, 1, 0, 0);
    free(a);

    /* A seekable archive maps like any other; the seek table is just bytes. */
    uint8_t* const b = (uint8_t*)malloc(8 * 1024 * 1024);
    if (!b) return 0;
    gen_random_data(b, 8 * 1024 * 1024);
    ok &= mmap_case("seekable random, 4K blocks", b, 8 * 1024 * 1024, 1, 1, ZXC_BLOCK_SIZE_MIN, 1);
    free(b);

    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

int test_mmap_errors(void) {
    printf("=== TEST: Unit - Read-only mapping error paths ===\n");
    if (!zxc_mmap_supported()) {
        printf("  [SKIP] no file mapping on this platform\n");
        printf("PASS\n\n");
        return 1;
    }

    int ok = 1;
    zxc_map_t m;

    /* NULL out-param is rejected before anything is opened. */
    if (zxc_mmap_open(MMAP_ERR_PATH, NULL) != ZXC_ERROR_NULL_INPUT ||
        zxc_mmap_open_fd(0, NULL) != ZXC_ERROR_NULL_INPUT) {
        printf("Failed: NULL out-param not rejected\n");
        ok = 0;
    }

    /* NULL path, with the map still cleared so closing it is safe. */
    m.data = (void*)0x1;
    m.map_base = (void*)0x1;
    if (zxc_mmap_open(NULL, &m) != ZXC_ERROR_NULL_INPUT || m.data != NULL || m.map_base != NULL) {
        printf("Failed: NULL path not rejected / map not cleared\n");
        ok = 0;
    }
    zxc_mmap_close(&m);

    /* Missing file, and a bad descriptor. */
    if (!expect_io("missing file", zxc_mmap_open("zxc_no_such_archive.bin", &m), &m)) ok = 0;
    if (!expect_io("bad fd", zxc_mmap_open_fd(-1, &m), &m)) ok = 0;

    /* Empty file: nothing to map. */
    if (!write_whole_file(MMAP_ERR_PATH, NULL, 0)) {
        printf("Failed: cannot write empty file\n");
        ok = 0;
    } else if (zxc_mmap_open(MMAP_ERR_PATH, &m) != ZXC_ERROR_SRC_TOO_SMALL) {
        printf("Failed: empty file not rejected\n");
        ok = 0;
    }

    /* A directory is not mappable. */
    if (!expect_io("directory", zxc_mmap_open(".", &m), &m)) ok = 0;

    /* Junk maps fine -- it is only bytes; the buffer API is what rejects it. */
    uint8_t junk[128];
    memset(junk, 0x5A, sizeof(junk));
    if (!write_whole_file(MMAP_ERR_PATH, junk, sizeof(junk))) {
        printf("Failed: cannot write junk\n");
        ok = 0;
    } else if (zxc_mmap_open(MMAP_ERR_PATH, &m) != ZXC_OK) {
        printf("Failed: junk file not mappable\n");
        ok = 0;
    } else {
        if (zxc_get_decompressed_size(m.data, m.size) != 0) {
            printf("Failed: junk accepted as an archive\n");
            ok = 0;
        }
        ok &= check_closed("junk", &m);
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
