/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

int test_error_name() {
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

int test_library_info_api() {
    printf("=== TEST: Unit - Library Info API (zxc_min/max/default_level, zxc_version_string) ===\n");

    // 1. Min level must match compile-time constant
    int min = zxc_min_level();
    if (min != ZXC_LEVEL_FASTEST) {
        printf("Failed: zxc_min_level() returned %d, expected %d\n", min, ZXC_LEVEL_FASTEST);
        return 0;
    }
    printf("  [PASS] zxc_min_level() == %d\n", min);

    // 2. Max level must match compile-time constant
    int max = zxc_max_level();
    if (max != ZXC_LEVEL_COMPACT) {
        printf("Failed: zxc_max_level() returned %d, expected %d\n", max, ZXC_LEVEL_COMPACT);
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

    printf("PASS\n\n");
    return 1;
}
