/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Golden-file generator.
 *
 * Regenerates every tests/format/golden/<name>.zxc from the deterministic
 * specifications in golden_cases.h. This is a MAINTAINER tool, not part of the
 * normal test run: CI never re-compresses the golden files, it only validates
 * and hashes the frozen bytes (see test_golden.c and .github/workflows/golden.yml).
 *
 * Usage:
 *   zxc_golden_gen <output-dir>     # defaults to "tests/format/golden"
 *
 * After regenerating, refresh the byte-stability manifest from the repo root:
 *   sha256sum tests/format/golden/[asterisk].zxc > tests/format/golden.sha256
 *
 * Review the resulting binary diff carefully: any change here is, by design, a
 * wire-format change that must be intentional.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_error.h"
#include "golden_cases.h"

static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
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
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "tests/format/golden";

    int failures = 0;
    printf("Generating %zu golden files into %s/\n", (size_t)GOLDEN_CASE_COUNT, dir);

    for (size_t i = 0; i < GOLDEN_CASE_COUNT; i++) {
        const golden_case_t *gc = &GOLDEN_CASES[i];

        uint8_t *input = NULL;
        size_t in_size = gc->make_input(&input);

        size_t cap = (size_t)zxc_compress_bound(in_size) + 4096;
        uint8_t *out = (uint8_t *)malloc(cap);
        if (!out) {
            fprintf(stderr, "  OOM for %s\n", gc->name);
            free(input);
            failures++;
            continue;
        }

        zxc_compress_opts_t opts = gc->opts;
        int64_t csize = zxc_compress(input, in_size, out, cap, &opts);
        if (csize <= 0) {
            fprintf(stderr, "  FAIL: compress(%s) -> %s\n", gc->name,
                    zxc_error_name((int)csize));
            failures++;
        } else {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s.zxc", dir, gc->name);
            if (write_file(path, out, (size_t)csize) == 0)
                printf("  wrote %-14s %8lld bytes\n", gc->name, (long long)csize);
            else
                failures++;
        }

        free(out);
        free(input);
    }

    if (failures) {
        fprintf(stderr, "Generation FAILED (%d error(s)).\n", failures);
        return 1;
    }
    printf("Done. Remember to refresh tests/format/golden.sha256.\n");
    return 0;
}
