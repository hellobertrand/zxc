/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Invalid-vector generator (maintainer tool). Writes every vector marked
 * generated in invalid_cases.h, which holds the recipes; test_conformance.c
 * rebuilds the same bytes from that header and checks the committed files.
 *
 *   zxc_invalid_gen [<output-dir>]    # defaults to conformance/v<current>/invalid
 *
 * After a format bump the corpus moves to a new conformance/v<N>/: create it,
 * generate into it, then write its FORMAT_VERSION and vectors.sha256.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../tests/vector_io.h"
#include "invalid_cases.h"

static int write_file(const char* dir, const char* name, const uint8_t* data, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.zxc", dir, name);
    if (vio_write_file(path, data, size) != 0) return -1;
    printf("  wrote %-24s %6zu bytes\n", name, size);
    return 0;
}

int main(int argc, char** argv) {
    char fallback[64];
    snprintf(fallback, sizeof fallback, "conformance/v%u/invalid",
             (unsigned)ZXC_FILE_FORMAT_VERSION);
    const char* dir = (argc > 1) ? argv[1] : fallback;
    if (!vio_dir_is_safe(dir)) {
        fprintf(stderr, "refusing output directory '%s': must not be empty or contain '..'\n", dir);
        return EXIT_FAILURE;
    }

    printf("Generating invalid vectors into %s/\n", dir);

    invalid_bases_t bases = {0};
    int failures = 0;
    for (size_t i = 0; i < INVALID_EXPECT_COUNT; i++) {
        if (!INVALID_EXPECT[i].generated) continue; /* static preamble cases */
        const char* name = INVALID_EXPECT[i].name;
        uint8_t* d = NULL;
        size_t n = 0;
        if (!build_invalid(&bases, name, &d, &n)) {
            fprintf(stderr, "  FAILED to build %s\n", name);
            failures++;
            continue;
        }
        failures += write_file(dir, name, d, n) < 0;
        free(d);
    }
    invalid_bases_free(&bases);

    if (failures) {
        fprintf(stderr, "Generation FAILED (%d error(s)).\n", failures);
        return 1;
    }
    printf("Done. Re-run the conformance suite and check each vector's error code.\n");
    return 0;
}
