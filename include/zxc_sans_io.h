/*
 * Copyright (c) 2025, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef ZXC_SANS_IO_H
#define ZXC_SANS_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * ZXC Compression Library - Sans-IO API - For you to build your own driver
 * ============================================================================
 */

/**
 * @typedef zxc_cctx_t
 * @brief Compression Context structure.
 *
 * This structure holds the state and buffers required for the compression
 * process. It is designed to be reused across multiple blocks or calls to avoid
 * the overhead of repeated memory allocations.
 *
 * **Key Fields:**
 * - `hash_table`: Stores indices of 4-byte sequences. Size is `2 *
 * ZXC_LZ_HASH_SIZE` to reduce collisions (load factor < 0.5).
 * - `chain_table`: Handles collisions by storing the *previous* occurrence of a
 *   hash. This forms a linked list for each hash bucket, allowing us to
 * traverse history.
 * - `epoch`: Used for "Lazy Hash Table Invalidation". Instead of
 * `ZXC_MEMSET`ing the entire hash table (which is slow) for every block, we
 * store `(epoch << 16) | offset`. If the stored epoch doesn't match the current
 * `ctx->epoch`, the entry is considered invalid/empty.
 *
 * @field hash_table Pointer to the hash table used for LZ77 match finding.
 * @field chain_table Pointer to the chain table for collision resolution.
 * @field buf_ll Pointer to the buffer for literal length codes.
 * @field buf_ml Pointer to the buffer for match length codes.
 * @field buf_off Pointer to the buffer for offset codes.
 * @field literals Pointer to the buffer for raw literal bytes.
 * @field epoch Current epoch counter for lazy hash table invalidation.
 * @field checksum_enabled Flag indicating if checksums should be computed.
 * @field compression_level The configured compression level.
 * @field lit_buffer Pointer to a scratch buffer for literal processing (e.g.,
 * RLE decoding).
 * @field lit_buffer_cap Current capacity of the literal scratch buffer.
 */
typedef struct {
    uint32_t* hash_table;   // Hash table for LZ77
    uint32_t* chain_table;  // Chain table for collision resolution
    uint32_t* buf_ll;       // Buffer for literal lengths
    uint32_t* buf_ml;       // Buffer for match lengths
    uint32_t* buf_off;      // Buffer for offsets
    uint8_t* literals;      // Buffer for literal bytes
    uint32_t epoch;         // Current epoch for hash table
    int checksum_enabled;   // Checksum enabled flag
    int compression_level;  // Compression level
    uint8_t* lit_buffer;    // Buffer scratch for literals (RLE)
    size_t lit_buffer_cap;  // Current capacity of this buffer
    void* memory_block;     // Single allocation block
} zxc_cctx_t;

 /**
 * @brief Initializes a ZXC compression context.
 *
 * Sets up the internal state required for compression operations, allocating
 * necessary buffers based on the chunk size and compression level.
 *
 * @param ctx Pointer to the compression context structure to initialize.
 * @param chunk_size The size of the data chunks to process.
 * @param mode The compression mode (e.g., fast, high compression).
 * @param level The specific compression level (1-9).
 * @param checksum_enabled
 * @return 0 on success, or a negative error code on failure.
 */
int zxc_cctx_init(zxc_cctx_t* ctx, size_t chunk_size, int mode, int level, int checksum_enabled);

/**
 * @brief Frees resources associated with a ZXC compression context.
 *
 * Releases memory allocated during initialization and resets the context state.
 *
 * @param ctx Pointer to the compression context to free.
 */
void zxc_cctx_free(zxc_cctx_t* ctx);


/**
 * @brief Validates and reads the ZXC file header from a source buffer.
 *
 * Checks for the correct magic bytes and version number.
 *
 * @param src Pointer to the start of the file data.
 * @param src_size Size of the available source data (must be at least header
 * size).
 * @return The size of the header in bytes on success, or a negative error code.
 */
int zxc_read_file_header(const uint8_t* src, size_t src_size);

/**
 * @brief Writes the standard ZXC file header to a destination buffer.
 *
 * Writes the magic bytes and version information.
 *
 * @param dst Pointer to the destination buffer.
 * @param dst_capacity Maximum capacity of the destination buffer.
 * @return The number of bytes written on success, or a negative error code.
 */
int zxc_write_file_header(uint8_t* dst, size_t dst_capacity);

/**
 * @struct zxc_block_header_t
 * @brief Represents the on-disk header structure for a ZXC block.
 *
 * This structure contains metadata required to parse and decompress a block.
 *
 * @var zxc_block_header_t::block_type
 * The type of the block (see zxc_block_type_t).
 * @var zxc_block_header_t::block_flags
 * Bit flags indicating properties like checksum presence.
 * @var zxc_block_header_t::reserved
 * Reserved bytes for future protocol extensions.
 * @var zxc_block_header_t::comp_size
 * The size of the compressed data payload in bytes (excluding this header).
 * @var zxc_block_header_t::raw_size
 * The size of the data after decompression.
 */
typedef struct {
    uint8_t block_type;   // Block type (e.g., RAW, GNR, NUM)
    uint8_t block_flags;  // Flags (e.g., checksum presence)
    uint16_t reserved;    // Reserved for future use
    uint32_t comp_size;   // Compressed size excluding header
    uint32_t raw_size;    // Decompressed size
} zxc_block_header_t;


/**
 * @brief Parses a block header from the source stream.
 *
 * Decodes the block size, compression type, and checksum flags into the
 * provided block header structure.
 *
 * @param src Pointer to the current position in the source stream.
 * @param src_size Available bytes remaining in the source stream.
 * @param bh Pointer to a block header structure to populate.
 * @return The number of bytes read (header size) on success, or a negative
 * error code.
 */
int zxc_read_block_header(const uint8_t* src, size_t src_size, zxc_block_header_t* bh);

/**
 * @brief Encodes a block header into the destination buffer.
 *
 * Serializes the information contained in the block header structure (size,
 * flags, etc.) into the binary format expected by the decoder.
 *
 * @param dst Pointer to the destination buffer.
 * @param dst_capacity Maximum capacity of the destination buffer.
 * @param bh Pointer to the block header structure containing the metadata.
 * @return The number of bytes written on success, or a negative error code.
 */
int zxc_write_block_header(uint8_t* dst, size_t dst_capacity, const zxc_block_header_t* bh);

#ifdef __cplusplus
}
#endif

#endif  // ZXC_SANS_IO_H