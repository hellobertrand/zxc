// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

#include "test_common.h"

int test_error_name(void) {
    printf("--- Test: zxc_error_name ---\n");

    struct {
        int code;
        const char* expected;
    } cases[] = {
        {ZXC_OK, "ZXC_OK"},
        {ZXC_ERROR_MEMORY, "ZXC_ERROR_MEMORY"},
        {ZXC_ERROR_DST_TOO_SMALL, "ZXC_ERROR_DST_TOO_SMALL"},
        {ZXC_ERROR_SRC_TOO_SMALL, "ZXC_ERROR_SRC_TOO_SMALL"},
        {ZXC_ERROR_BAD_MAGIC, "ZXC_ERROR_BAD_MAGIC"},
        {ZXC_ERROR_BAD_VERSION, "ZXC_ERROR_BAD_VERSION"},
        {ZXC_ERROR_BAD_HEADER, "ZXC_ERROR_BAD_HEADER"},
        {ZXC_ERROR_BAD_CHECKSUM, "ZXC_ERROR_BAD_CHECKSUM"},
        {ZXC_ERROR_CORRUPT_DATA, "ZXC_ERROR_CORRUPT_DATA"},
        {ZXC_ERROR_BAD_OFFSET, "ZXC_ERROR_BAD_OFFSET"},
        {ZXC_ERROR_OVERFLOW, "ZXC_ERROR_OVERFLOW"},
        {ZXC_ERROR_IO, "ZXC_ERROR_IO"},
        {ZXC_ERROR_NULL_INPUT, "ZXC_ERROR_NULL_INPUT"},
        {ZXC_ERROR_BAD_BLOCK_TYPE, "ZXC_ERROR_BAD_BLOCK_TYPE"},
        {ZXC_ERROR_BAD_BLOCK_SIZE, "ZXC_ERROR_BAD_BLOCK_SIZE"},
        {ZXC_ERROR_DICT_REQUIRED, "ZXC_ERROR_DICT_REQUIRED"},
        {ZXC_ERROR_DICT_MISMATCH, "ZXC_ERROR_DICT_MISMATCH"},
        {ZXC_ERROR_DICT_TOO_LARGE, "ZXC_ERROR_DICT_TOO_LARGE"},
        {ZXC_ERROR_BAD_LEVEL, "ZXC_ERROR_BAD_LEVEL"},
        {ZXC_ERROR_DICT_UNSUPPORTED, "ZXC_ERROR_DICT_UNSUPPORTED"},
    };
    const int n = sizeof(cases) / sizeof(cases[0]);

    for (int i = 0; i < n; i++) {
        const char* name = zxc_error_name(cases[i].code);
        if (strcmp(name, cases[i].expected) != 0) {
            printf("  [FAIL] zxc_error_name(%d) = \"%s\", expected \"%s\"\n", cases[i].code, name,
                   cases[i].expected);
            return 0;
        }
    }
    printf("  [PASS] All %d known error codes\n", n);

    // Unknown codes should return "ZXC_UNKNOWN_ERROR"
    const char* unk = zxc_error_name(-999);
    if (strcmp(unk, "ZXC_UNKNOWN_ERROR") != 0) {
        printf("  [FAIL] zxc_error_name(-999) = \"%s\", expected \"ZXC_UNKNOWN_ERROR\"\n", unk);
        return 0;
    }
    unk = zxc_error_name(42);
    if (strcmp(unk, "ZXC_UNKNOWN_ERROR") != 0) {
        printf("  [FAIL] zxc_error_name(42) = \"%s\", expected \"ZXC_UNKNOWN_ERROR\"\n", unk);
        return 0;
    }
    printf("  [PASS] Unknown error codes\n");

    printf("PASS\n\n");
    return 1;
}

int test_library_info_api(void) {
    printf(
        "=== TEST: Unit - Library Info API (zxc_min/max/default_level, zxc_version_string/number) "
        "===\n");

    // 1. Min level must match compile-time constant
    int min = zxc_min_level();
    if (min != ZXC_LEVEL_FASTEST) {
        printf("Failed: zxc_min_level() returned %d, expected %d\n", min, ZXC_LEVEL_FASTEST);
        return 0;
    }
    printf("  [PASS] zxc_min_level() == %d\n", min);

    // 2. Max level must match compile-time constant
    int max = zxc_max_level();
    if (max != ZXC_LEVEL_ULTRA) {
        printf("Failed: zxc_max_level() returned %d, expected %d\n", max, ZXC_LEVEL_ULTRA);
        return 0;
    }
    printf("  [PASS] zxc_max_level() == %d\n", max);

    // 3. Default level must be within [min, max]
    int def = zxc_default_level();
    if (def < min || def > max) {
        printf("Failed: zxc_default_level() returned %d, not in [%d, %d]\n", def, min, max);
        return 0;
    }
    if (def != ZXC_LEVEL_DEFAULT) {
        printf("Failed: zxc_default_level() returned %d, expected %d\n", def, ZXC_LEVEL_DEFAULT);
        return 0;
    }
    printf("  [PASS] zxc_default_level() == %d\n", def);

    // 4. Version string must be non-NULL and match compile-time version
    const char* ver = zxc_version_string();
    if (!ver) {
        printf("Failed: zxc_version_string() returned NULL\n");
        return 0;
    }
    if (strcmp(ver, ZXC_LIB_VERSION_STR) != 0) {
        printf("Failed: zxc_version_string() returned \"%s\", expected \"%s\"\n", ver,
               ZXC_LIB_VERSION_STR);
        return 0;
    }
    printf("  [PASS] zxc_version_string() == \"%s\"\n", ver);

    // 5. Version number must match the header's packed triple
    const unsigned num = zxc_version_number();
    if (num != ZXC_VERSION_NUMBER) {
        printf("Failed: zxc_version_number() returned %u, expected %u\n", num,
               (unsigned)ZXC_VERSION_NUMBER);
        return 0;
    }
    printf("  [PASS] zxc_version_number() == %u\n", num);

    // 6. Code name must be a non-empty string matching the header
    const char* codename = zxc_version_codename();
    if (!codename || !*codename) {
        printf("Failed: zxc_version_codename() returned %s\n", codename ? "\"\"" : "NULL");
        return 0;
    }
    if (strcmp(codename, ZXC_VERSION_CODENAME) != 0) {
        printf("Failed: zxc_version_codename() returned \"%s\", expected \"%s\"\n", codename,
               ZXC_VERSION_CODENAME);
        return 0;
    }
    printf("  [PASS] zxc_version_codename() == \"%s\"\n", codename);

    printf("PASS\n\n");
    return 1;
}

/* The dictionary setters follow the build's dictionary support. */
int test_dict_build_support(void) {
    printf("=== TEST: Dict - setters follow the build's dictionary support ===\n");
#ifdef ZXC_NO_DICT
    const int want = ZXC_ERROR_DICT_UNSUPPORTED;
#else
    const int want = ZXC_OK;
#endif
    static const char dict[] = "a dictionary the setters are offered, long enough to count";
    const size_t dsz = sizeof(dict) - 1;
    const size_t n = 16 * 1024;
    uint8_t* src = malloc(n);
    uint8_t* comp = malloc(zxc_compress_bound(n));
    zxc_cctx* cctx = zxc_create_cctx();
    zxc_dctx* dctx = zxc_create_dctx();
    zxc_seekable* s = NULL;
    int ok = src && comp && cctx && dctx;

    if (ok) {
        gen_lz_data(src, n);
        const int64_t csz = zxc_cctx_set_param(cctx, ZXC_C_SEEKABLE, 1) == ZXC_OK
                                ? zxc_compress_cctx(cctx, src, n, comp, zxc_compress_bound(n))
                                : -1;
        s = csz > 0 ? zxc_seekable_open(comp, (size_t)csz) : NULL;
        ok = s != NULL;
        if (!ok) printf("  [FAIL] setup\n");
    }
    if (ok) {
        const int rc = zxc_cctx_set_dict(cctx, dict, dsz, NULL);
        const int rd = zxc_dctx_set_dict(dctx, dict, dsz, NULL);
        const int rs = zxc_seekable_set_dict(s, dict, dsz, NULL);
        ok = rc == want && rd == want && rs == want;
        if (!ok)
            printf("  [FAIL] cctx %s, dctx %s, seekable %s; expected %s\n", zxc_error_name(rc),
                   zxc_error_name(rd), zxc_error_name(rs), zxc_error_name(want));
    }
    if (ok) {
        ok = zxc_cctx_set_dict(cctx, NULL, 0, NULL) == ZXC_OK &&
             zxc_dctx_set_dict(dctx, NULL, 0, NULL) == ZXC_OK;
        if (!ok) printf("  [FAIL] detaching\n");
    }

    zxc_seekable_free(s);
    zxc_free_dctx(dctx);
    zxc_free_cctx(cctx);
    free(comp);
    free(src);
    if (!ok) return 0;
    printf("PASS\n\n");
    return 1;
}

int test_ctz_debruijn(void) {
    printf("--- Test: De Bruijn ctz fallbacks ---\n");

    // Each lowest set bit, alone and under set higher bits (the x & -x step).
    for (int i = 0; i < 64; i++) {
        const uint64_t bit = 1ULL << i;
        const uint64_t cases[2] = {bit, bit | ~((bit << 1) - 1)};
        for (int c = 0; c < 2; c++) {
            const int got = zxc_ctz64_debruijn(cases[c]);
            if (got != i) {
                printf("  [FAIL] zxc_ctz64_debruijn(0x%016llx) = %d, expected %d\n",
                       (unsigned long long)cases[c], got, i);
                return 0;
            }
            if (i < 32 && zxc_ctz32_debruijn((uint32_t)cases[c]) != i) {
                printf("  [FAIL] zxc_ctz32_debruijn(0x%08x) = %d, expected %d\n",
                       (unsigned)(uint32_t)cases[c], zxc_ctz32_debruijn((uint32_t)cases[c]), i);
                return 0;
            }
        }
    }
    printf("  [PASS] Every bit position, 32 and 64 bits\n");

    printf("PASS\n\n");
    return 1;
}
