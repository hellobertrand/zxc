/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Invalid-vector generator (maintainer tool).
 *
 * Writes conformance/<version>/invalid/<name>.zxc for every vector in invalid_cases.h,
 * which holds the recipes; test_conformance.c builds the same bytes from that
 * header and checks the committed files still match.
 *
 * Usage:
 *   zxc_invalid_gen [<output-dir>]    # defaults to "conformance/v8/invalid"
 *
 * After a format bump the corpus moves to a new conformance/v<N>/: create it,
 * generate into it, then write its FORMAT_VERSION and vectors.sha256. The old
 * directory stays frozen -- it records what archives of that version look like.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include <io.h>
#include <share.h>
#endif

#include "invalid_cases.h"

/* Owner-only (0600) binary write, mirroring tests/format/gen_golden.c. */
static FILE* open_restricted_wb(const char* path) {
#ifdef _MSC_VER
    int fd = -1;
    _sopen_s(&fd, path, _O_CREAT | _O_WRONLY | _O_TRUNC | _O_BINARY, _SH_DENYNO,
             _S_IREAD | _S_IWRITE);
    return fd >= 0 ? _fdopen(fd, "wb") : NULL;
#else
    const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    return fd >= 0 ? fdopen(fd, "wb") : NULL;
#endif
}

static int write_file(const char* dir, const char* name, const uint8_t* data, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.zxc", dir, name);
    FILE* f = open_restricted_wb(path);
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
    printf("  wrote %-24s %6zu bytes\n", name, size);
    return 0;
}

static int output_dir_is_safe(const char* dir) {
    return dir[0] != '\0' && strstr(dir, "..") == NULL;
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : "conformance/v8/invalid";
    if (!output_dir_is_safe(dir)) {
        fprintf(stderr, "refusing output directory '%s': must not be empty or contain '..'\n", dir);
        return EXIT_FAILURE;
    }

    printf("Generating %zu invalid vectors into %s/\n", (size_t)INVALID_GENERATED_COUNT, dir);

    invalid_bases_t bases = {0};
    int failures = 0;
    for (size_t i = 0; i < INVALID_GENERATED_COUNT; i++) {
        uint8_t* d = NULL;
        size_t n = 0;
        if (!build_invalid(&bases, INVALID_GENERATED[i], &d, &n)) {
            fprintf(stderr, "  FAILED to build %s\n", INVALID_GENERATED[i]);
            failures++;
            continue;
        }
        failures += write_file(dir, INVALID_GENERATED[i], d, n) < 0;
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
