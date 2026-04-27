/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ZXC_TEST_COMMON_H
#define ZXC_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include <io.h>
#include <share.h>
#endif

#include "../include/zxc_buffer.h"
#include "../include/zxc_error.h"
#include "../include/zxc_pstream.h"
#include "../include/zxc_sans_io.h"
#include "../include/zxc_seekable.h"
#include "../include/zxc_stream.h"
#include "../src/lib/zxc_internal.h"

/* Declarative test-table entry: TEST_CASE(test_foo) -> { "test_foo", test_foo } */
#define TEST_CASE(fn) { #fn, fn }

/* --- IO helper ---------------------------------------------------------- */

/* Creates a temporary file with restricted permissions (0600).
 * Returns a FILE* opened for writing, or NULL on failure. */
FILE* create_restricted_file(const char* path);

/* --- Data generators ---------------------------------------------------- */

void gen_random_data(uint8_t* buf, size_t size);
void gen_lz_data(uint8_t* buf, size_t size);
void gen_num_data(uint8_t* buf, size_t size);
void gen_num_data_zero(uint8_t* buf, size_t size);
void gen_num_data_small(uint8_t* buf, size_t size);
void gen_num_data_large(uint8_t* buf, size_t size);
void gen_binary_data(uint8_t* buf, size_t size);
void gen_small_offset_data(uint8_t* buf, size_t size);
void gen_large_offset_data(uint8_t* buf, size_t size);
void fill_seek_data(uint8_t* buf, size_t size, uint8_t seed);

/* Generic streaming round-trip check (compress, decompress, compare).
 * Returns 1 on success, 0 on failure. */
int test_round_trip(const char* test_name, const uint8_t* input, size_t size,
                    int level, int checksum_enabled);

/* --- Test function prototypes ------------------------------------------ */

/* Buffer API */
int test_buffer_api(void);
int test_buffer_api_scratch_buf(void);
int test_buffer_error_codes(void);
int test_get_decompressed_size(void);
int test_decompress_fast_vs_safe_path(void);
int test_max_compressed_size_logic(void);

/* Block API */
int test_block_api(void);
int test_block_api_boundary_sizes(void);
int test_block_api_large_block_varint(void);
int test_decompress_block_safe(void);
int test_decompress_block_bound(void);

/* Context API */
int test_opaque_context_api(void);
int test_estimate_cctx_size(void);

/* Stream API */
int test_null_output_decompression(void);
int test_invalid_arguments(void);
int test_truncated_input(void);
int test_io_failures(void);
int test_thread_params(void);
int test_multithread_roundtrip(void);
int test_stream_get_decompressed_size_errors(void);
int test_stream_engine_errors(void);

/* Push Streaming API (zxc_pstream.h) */
int test_pstream_roundtrip_basic(void);
int test_pstream_roundtrip_no_checksum(void);
int test_pstream_roundtrip_levels(void);
int test_pstream_tiny_chunks(void);
int test_pstream_drip_one_byte(void);
int test_pstream_empty_input(void);
int test_pstream_large_random(void);
int test_pstream_compatible_with_buffer_api(void);
int test_pstream_decompress_compatible_with_buffer_api(void);
int test_pstream_invalid_args(void);
int test_pstream_truncated_input(void);
int test_pstream_corrupted_magic(void);
int test_pstream_decode_seekable_archive(void);
int test_pstream_compress_after_end_rejected(void);
int test_pstream_compress_drain_block_resume(void);

/* Stream round-trip coverage (patterns x sizes x levels x checksum) */
int test_roundtrip_raw_random(void);
int test_roundtrip_ghi_text(void);
int test_roundtrip_glo_text(void);
int test_roundtrip_num_seq(void);
int test_roundtrip_num_zero(void);
int test_roundtrip_num_small(void);
int test_roundtrip_num_large(void);
int test_roundtrip_small_50(void);
int test_roundtrip_empty(void);
int test_roundtrip_1byte(void);
int test_roundtrip_1byte_crc(void);
int test_roundtrip_large_15mb_lz(void);
int test_roundtrip_large_15mb_num(void);
int test_roundtrip_checksum_off(void);
int test_roundtrip_checksum_on(void);
int test_roundtrip_level1(void);
int test_roundtrip_level2(void);
int test_roundtrip_level3(void);
int test_roundtrip_level4(void);
int test_roundtrip_level5(void);
int test_roundtrip_level6(void);
int test_roundtrip_binary(void);
int test_roundtrip_binary_crc(void);
int test_roundtrip_binary_small(void);
int test_roundtrip_offset8_small(void);
int test_roundtrip_offset8_lvl5(void);
int test_roundtrip_offset16_large(void);
int test_roundtrip_offset16_lvl5(void);
int test_roundtrip_offset_mixed(void);

/* Seekable (single-threaded) */
int test_seekable_table_sizes(void);
int test_seekable_table_write(void);
int test_seekable_roundtrip(void);
int test_seekable_open_query(void);
int test_seekable_random_access(void);
int test_seekable_non_seekable_reject(void);
int test_seekable_single_block(void);
int test_seekable_all_levels(void);
int test_seekable_many_blocks(void);
int test_seekable_open_file(void);
int test_seekable_cross_boundary(void);
int test_seekable_truncated_input(void);
int test_seekable_corrupted_sek(void);
int test_seekable_range_out_of_bounds(void);
int test_seekable_dst_too_small(void);
int test_seekable_empty_file(void);
int test_seekable_no_checksum(void);
int test_seekable_with_checksum(void);

/* Seekable (multi-threaded) */
int test_seekable_mt_roundtrip(void);
int test_seekable_mt_single_block(void);
int test_seekable_mt_random_access(void);
int test_seekable_mt_full_file(void);

/* Format (on-disk) */
int test_bit_reader(void);
int test_bitpack(void);
int test_huffman_codec(void);
int test_eof_block_structure(void);
int test_header_checksum(void);
int test_global_checksum_order(void);
int test_legacy_header(void);

/* Misc */
int test_error_name(void);
int test_library_info_api(void);

#endif /* ZXC_TEST_COMMON_H */
