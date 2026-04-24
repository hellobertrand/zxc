/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test_common.h"

typedef int (*test_fn_t)(void);

typedef struct {
    const char* name;
    test_fn_t fn;
} test_entry_t;

static const test_entry_t g_tests[] = {
    /* --- Streaming round-trip coverage (patterns x sizes x levels x checksum) --- */
    TEST_CASE(test_roundtrip_raw_random),
    TEST_CASE(test_roundtrip_ghi_text),
    TEST_CASE(test_roundtrip_glo_text),
    TEST_CASE(test_roundtrip_num_seq),
    TEST_CASE(test_roundtrip_num_zero),
    TEST_CASE(test_roundtrip_num_small),
    TEST_CASE(test_roundtrip_num_large),
    TEST_CASE(test_roundtrip_small_50),
    TEST_CASE(test_roundtrip_empty),
    TEST_CASE(test_roundtrip_1byte),
    TEST_CASE(test_roundtrip_1byte_crc),
    TEST_CASE(test_roundtrip_large_15mb_lz),
    TEST_CASE(test_roundtrip_large_15mb_num),
    TEST_CASE(test_roundtrip_checksum_off),
    TEST_CASE(test_roundtrip_checksum_on),
    TEST_CASE(test_roundtrip_level1),
    TEST_CASE(test_roundtrip_level2),
    TEST_CASE(test_roundtrip_level3),
    TEST_CASE(test_roundtrip_level4),
    TEST_CASE(test_roundtrip_level5),
    TEST_CASE(test_roundtrip_binary),
    TEST_CASE(test_roundtrip_binary_crc),
    TEST_CASE(test_roundtrip_binary_small),
    TEST_CASE(test_roundtrip_offset8_small),
    TEST_CASE(test_roundtrip_offset8_lvl5),
    TEST_CASE(test_roundtrip_offset16_large),
    TEST_CASE(test_roundtrip_offset16_lvl5),
    TEST_CASE(test_roundtrip_offset_mixed),

    /* --- Buffer API --- */
    TEST_CASE(test_buffer_api),
    TEST_CASE(test_buffer_api_scratch_buf),
    TEST_CASE(test_buffer_error_codes),
    TEST_CASE(test_get_decompressed_size),
    TEST_CASE(test_decompress_fast_vs_safe_path),
    TEST_CASE(test_max_compressed_size_logic),

    /* --- Block API --- */
    TEST_CASE(test_block_api),
    TEST_CASE(test_block_api_boundary_sizes),
    TEST_CASE(test_block_api_large_block_varint),
    TEST_CASE(test_decompress_block_bound),
    TEST_CASE(test_decompress_block_safe),

    /* --- Context API --- */
    TEST_CASE(test_opaque_context_api),
    TEST_CASE(test_estimate_cctx_size),

    /* --- Stream API --- */
    TEST_CASE(test_null_output_decompression),
    TEST_CASE(test_invalid_arguments),
    TEST_CASE(test_truncated_input),
    TEST_CASE(test_io_failures),
    TEST_CASE(test_thread_params),
    TEST_CASE(test_multithread_roundtrip),
    TEST_CASE(test_stream_get_decompressed_size_errors),
    TEST_CASE(test_stream_engine_errors),

    /* --- Format (on-disk) --- */
    TEST_CASE(test_bit_reader),
    TEST_CASE(test_bitpack),
    TEST_CASE(test_eof_block_structure),
    TEST_CASE(test_header_checksum),
    TEST_CASE(test_global_checksum_order),
    TEST_CASE(test_legacy_header),

    /* --- Misc --- */
    TEST_CASE(test_error_name),
    TEST_CASE(test_library_info_api),

    /* --- Seekable (single-threaded) --- */
    TEST_CASE(test_seekable_table_sizes),
    TEST_CASE(test_seekable_table_write),
    TEST_CASE(test_seekable_roundtrip),
    TEST_CASE(test_seekable_open_query),
    TEST_CASE(test_seekable_random_access),
    TEST_CASE(test_seekable_non_seekable_reject),
    TEST_CASE(test_seekable_single_block),
    TEST_CASE(test_seekable_all_levels),
    TEST_CASE(test_seekable_many_blocks),
    TEST_CASE(test_seekable_open_file),

    /* --- Seekable MT --- */
    TEST_CASE(test_seekable_mt_roundtrip),
    TEST_CASE(test_seekable_mt_single_block),
    TEST_CASE(test_seekable_mt_random_access),
    TEST_CASE(test_seekable_mt_full_file),

    /* --- Seekable edge cases --- */
    TEST_CASE(test_seekable_cross_boundary),
    TEST_CASE(test_seekable_truncated_input),
    TEST_CASE(test_seekable_corrupted_sek),
    TEST_CASE(test_seekable_range_out_of_bounds),
    TEST_CASE(test_seekable_dst_too_small),
    TEST_CASE(test_seekable_empty_file),
    TEST_CASE(test_seekable_no_checksum),
    TEST_CASE(test_seekable_with_checksum),
};

static const size_t g_tests_count = sizeof(g_tests) / sizeof(g_tests[0]);

static void print_usage(const char* argv0) {
    printf("Usage: %s [options] [filter]\n", argv0);
    printf("  filter          substring matched against test names (e.g. '%s block_api')\n", argv0);
    printf("  -e, --exact N   run only the test whose name exactly matches N\n");
    printf("  --list          print all test names and exit\n");
    printf("  -h, --help      show this help\n");
    printf("With no argument, runs every test.\n");
}

int main(int argc, char** argv) {
    srand(42);  // Fixed seed for reproducibility

    const char* filter = NULL;
    int exact = 0;

    {
        int ai = 1;
        while (ai < argc) {
            const char* a = argv[ai];
            if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
                print_usage(argv[0]);
                return 0;
            }
            if (strcmp(a, "--list") == 0) {
                for (size_t k = 0; k < g_tests_count; k++) printf("%s\n", g_tests[k].name);
                return 0;
            }
            if (strcmp(a, "-e") == 0 || strcmp(a, "--exact") == 0) {
                exact = 1;
                if (ai + 1 >= argc) {
                    printf("error: %s requires a test name\n", a);
                    return 1;
                }
                filter = argv[ai + 1];
                ai += 2;
                continue;
            }
            filter = a;
            ai += 1;
        }
    }

    int total_failures = 0;
    int ran = 0;

    for (size_t i = 0; i < g_tests_count; i++) {
        if (filter) {
            const int match = exact ? (strcmp(g_tests[i].name, filter) == 0)
                                    : (strstr(g_tests[i].name, filter) != NULL);
            if (!match) continue;
        }
        if (!g_tests[i].fn()) total_failures++;
        ran++;
    }

    if (filter && ran == 0) {
        printf("No tests matched filter \"%s\"%s.\n", filter, exact ? " (exact)" : "");
        return 1;
    }

    if (total_failures > 0) {
        printf("FAILED: %d tests failed.\n", total_failures);
        return 1;
    }

    printf("ALL TESTS PASSED SUCCESSFULLY.\n");
    return 0;
}
