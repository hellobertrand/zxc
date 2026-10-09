// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/*
 * Downstream consumer smoke test for an *installed* ZXC, built by
 * .github/workflows/packaging.yml via find_package, pkg-config and raw -I/-l.
 * Touches enough of the API to make the link step meaningful: a mismatched
 * zxc_export.h shows up here as unresolved __imp_zxc_* on Windows. The FILE*
 * roundtrip checks that, on Windows, both sides share one C runtime.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <string.h>
#include <zxc.h>
#include <zxc_stream.h>

int main(void) {
    static char input[64 * 1024];
    for (size_t i = 0; i < sizeof(input); i++) {
        input[i] = (char)('a' + (i % 23));
    }

    const uint64_t bound = zxc_compress_bound(sizeof(input));
    static char compressed[128 * 1024];
    if (bound > sizeof(compressed)) {
        fprintf(stderr, "consumer: bound %llu exceeds the test buffer\n",
                (unsigned long long)bound);
        return 1;
    }

    const int64_t csize = zxc_compress(input, sizeof(input), compressed, sizeof(compressed), NULL);
    if (csize <= 0) {
        fprintf(stderr, "consumer: zxc_compress failed: %s\n", zxc_error_name((int)csize));
        return 1;
    }

    if (zxc_get_decompressed_size(compressed, (size_t)csize) != sizeof(input)) {
        fprintf(stderr, "consumer: zxc_get_decompressed_size disagrees with the input\n");
        return 1;
    }

    static char output[sizeof(input)];
    const int64_t dsize = zxc_decompress(compressed, (size_t)csize, output, sizeof(output), NULL);
    if (dsize != (int64_t)sizeof(input)) {
        fprintf(stderr, "consumer: zxc_decompress failed: %s\n", zxc_error_name((int)dsize));
        return 1;
    }

    if (memcmp(input, output, sizeof(input)) != 0) {
        fprintf(stderr, "consumer: roundtrip mismatch\n");
        return 1;
    }

    // FILE* roundtrip on streams opened here; the failing step is named, so an
    // unwritable directory is not taken for a C runtime mismatch.
    const char* failed = NULL;
    FILE* const f_in = fopen("consumer_in.tmp", "w+b");
    FILE* const f_arc = fopen("consumer_arc.tmp", "w+b");
    FILE* const f_out = fopen("consumer_out.tmp", "w+b");
    if (!f_in || !f_arc || !f_out)
        failed = "creating temporary files in the working directory";
    else if (fwrite(input, 1, sizeof(input), f_in) != sizeof(input) || fseek(f_in, 0, SEEK_SET))
        failed = "writing the input file";
    else if (zxc_stream_compress(f_in, f_arc, NULL) <= 0 || fseek(f_arc, 0, SEEK_SET))
        failed = "zxc_stream_compress";
    else if (zxc_stream_decompress(f_arc, f_out, NULL) != (int64_t)sizeof(input) ||
             fseek(f_out, 0, SEEK_SET))
        failed = "zxc_stream_decompress";
    else if (fread(output, 1, sizeof(output), f_out) != sizeof(output) ||
             memcmp(input, output, sizeof(input)) != 0)
        failed = "comparing the FILE* roundtrip output";
    if (f_in) fclose(f_in);
    if (f_arc) fclose(f_arc);
    if (f_out) fclose(f_out);
    remove("consumer_in.tmp");
    remove("consumer_arc.tmp");
    remove("consumer_out.tmp");
    if (failed) {
        fprintf(stderr, "consumer: FILE* roundtrip failed: %s\n", failed);
        return 1;
    }

    printf("consumer: zxc %s roundtrip OK (%zu -> %lld bytes)\n", zxc_version_string(),
           sizeof(input), (long long)csize);
    return 0;
}
