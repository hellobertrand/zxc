/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Valid-vector generator (maintainer tool). Recompresses each committed
 * <name>.expected with the options valid_cases.h declares. The .expected
 * plaintexts and the .zxd dictionaries are inputs, never written.
 *
 *   zxc_valid_gen [<vectors-dir>]    # defaults to conformance/v<current>/valid
 *
 * After a format bump the corpus moves to a new conformance/v<N>/: create it,
 * generate into it, then write its FORMAT_VERSION and vectors.sha256.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_dict.h"
#include "../include/zxc_error.h"
#include "../tests/vector_io.h"
#include "valid_cases.h"
#include "zxc_internal.h"

/* Regenerate one vector. Returns 0 on success. */
static int generate(const char* dir, const valid_case_t* vc) {
    char path[1024];
    int rc = -1;

    snprintf(path, sizeof path, "%s/%s.expected", dir, vc->name);
    size_t in_size = 0;
    uint8_t* input = vio_read_file(path, &in_size);
    if (!input) {
        fprintf(stderr, "  FAIL %s: cannot read %s\n", vc->name, path);
        return -1;
    }

    /* The .zxd carries the content and its Huffman table; both feed the dict_id. */
    zxc_compress_opts_t opts = vc->opts;
    uint8_t* dict_buf = NULL;
    if (vc->dict) {
        snprintf(path, sizeof path, "%s/%s", dir, vc->dict);
        size_t dict_file_size = 0;
        dict_buf = vio_read_file(path, &dict_file_size);
        if (!dict_buf) {
            fprintf(stderr, "  FAIL %s: cannot read %s\n", vc->name, path);
            free(input);
            return -1;
        }
        if (zxc_dict_load(dict_buf, dict_file_size, &opts.dict, &opts.dict_size, &opts.dict_huf,
                          NULL) != 0) {
            fprintf(stderr, "  FAIL %s: %s is not a valid dictionary\n", vc->name, path);
            free(dict_buf);
            free(input);
            return -1;
        }
    }

    const size_t cap = (size_t)zxc_compress_bound(in_size) + 4096;
    uint8_t* out = (uint8_t*)malloc(cap);
    if (!out) {
        fprintf(stderr, "  FAIL %s: out of memory\n", vc->name);
    } else {
        const int64_t csize = zxc_compress(input, in_size, out, cap, &opts);
        if (csize <= 0) {
            fprintf(stderr, "  FAIL %s: compress -> %s\n", vc->name, zxc_error_name((int)csize));
        } else {
            snprintf(path, sizeof path, "%s/%s.zxc", dir, vc->name);
            if (vio_write_file(path, out, (size_t)csize) == 0) {
                printf("  wrote %-20s %8lld bytes  (level %d, block %zu KB%s%s)\n", vc->name,
                       (long long)csize, vc->opts.level, vc->opts.block_size / 1024U,
                       vc->opts.checksum_enabled ? ", checksum" : "",
                       vc->opts.seekable ? ", seekable" : "");
                rc = 0;
            }
        }
    }

    free(out);
    free(dict_buf);
    free(input);
    return rc;
}

int main(int argc, char** argv) {
    char fallback[64];
    snprintf(fallback, sizeof fallback, "conformance/v%u/valid", (unsigned)ZXC_FILE_FORMAT_VERSION);
    const char* dir = (argc > 1) ? argv[1] : fallback;
    if (!vio_dir_is_safe(dir)) {
        fprintf(stderr, "refusing output directory '%s': must not be empty or contain '..'\n", dir);
        return EXIT_FAILURE;
    }

    printf("Regenerating %zu valid vectors in %s/\n", (size_t)VALID_CASE_COUNT, dir);

    int failures = 0;
    for (size_t i = 0; i < VALID_CASE_COUNT; i++)
        if (generate(dir, &VALID_CASES[i]) != 0) failures++;

    if (failures) {
        fprintf(stderr, "Generation FAILED (%d error(s)).\n", failures);
        return 1;
    }
    printf("Done. Re-run the conformance suite and review the diff.\n");
    return 0;
}
