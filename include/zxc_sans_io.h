/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_sans_io.h
 * @brief Low-level, sans-I/O compression primitives.
 *
 * This header exposes the building blocks used by the higher-level buffer and
 * streaming APIs.  It is intended for callers who need their own compression
 * context lifecycle - for example, to reuse a context across many blocks.
 *
 * Frame primitives (file header, block header, file footer) are intentionally
 * kept private while the on-disk layout is still allowed to evolve; use the
 * buffer or streaming APIs to produce/consume complete ZXC frames.
 */

#ifndef ZXC_SANS_IO_H
#define ZXC_SANS_IO_H

#include <stddef.h>
#include <stdint.h>

#include "zxc_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sans_io Sans-IO API
 * @brief Low-level primitives for building custom compression drivers.
 * @{
 */

/**
 * @struct zxc_cctx_t
 * @brief Compression Context structure.
 *
 * This structure holds the state and buffers required for the compression
 * process. It is designed to be reused across multiple blocks or calls to avoid
 * the overhead of repeated memory allocations.
 *
 * **Key Fields:**
 * - `hash_table`: Stores epoch-tagged positions (`ZXC_LZ_HASH_SIZE` * 4 bytes).
 * - `hash_tags`:  Stores 8-bit tags for fast rejection (`ZXC_LZ_HASH_SIZE` * 1 byte).
 * - `chain_table`: Handles collisions by storing the *previous* occurrence of a
 *   hash. This forms a linked list for each hash bucket, allowing us to
 * traverse history.
 * - `epoch`: Used for "Lazy Hash Table Invalidation". Instead of
 * `ZXC_MEMSET`ing the entire hash table (which is slow) for every block, we
 * store `(epoch << 16) | offset`. If the stored epoch doesn't match the current
 * `ctx->epoch`, the entry is considered invalid/empty.
 *
 * hash_table Pointer to the hash table used for LZ77 match finding.
 * chain_table Pointer to the chain table for collision resolution.
 * memory_block Pointer to the single allocation block containing all buffers.
 * epoch Current epoch counter for lazy hash table invalidation.
 * buf_extras Pointer to the buffer for extra lengths (LL >= 15 or ML >= 15).
 * buf_offsets Pointer to the buffer for offsets.
 * buf_tokens Pointer to the buffer for token sequences.
 * literals Pointer to the buffer for raw literal bytes.
 * lit_buffer Pointer to a scratch buffer for literal processing (e.g.,
 * RLE decoding).
 * lit_buffer_cap Current capacity of the literal scratch buffer.
 * checksum_enabled Flag indicating if checksums should be computed.
 * compression_level The configured compression level.
 */
typedef struct {
    /* Hot zone: random access / high frequency.
     * Kept at the start to ensure they reside in the first cache line (64 bytes). */
    uint32_t* hash_table;  /**< Hash table for LZ77 match positions (epoch|pos). */
    uint8_t* hash_tags;    /**< Split tag table for fast match rejection (8-bit tags). */
    uint16_t* chain_table; /**< Chain table for collision resolution. */
    void* memory_block;    /**< Single allocation block owner. */
    uint32_t epoch;        /**< Current epoch for lazy hash table invalidation. */

    /* Warm zone: sequential access per sequence. */
    uint32_t* buf_sequences; /**< Buffer for sequence records (packed: LL(8)|ML(8)|Offset(16)). */
    uint8_t* buf_tokens;     /**< Buffer for token sequences. */
    uint16_t* buf_offsets;   /**< Buffer for offsets. */
    uint8_t* buf_extras;     /**< Buffer for extra lengths (vbytes for LL/ML). */
    uint8_t* literals;       /**< Buffer for literal bytes. */

    /* Cold zone: configuration / scratch / resizeable. */
    uint8_t* lit_buffer;    /**< Scratch buffer for literals (RLE). */
    size_t lit_buffer_cap;  /**< Current capacity of the scratch buffer. */
    uint8_t* work_buf;      /**< Padded scratch buffer for buffer-API decompression. */
    size_t work_buf_cap;    /**< Capacity of the work buffer. */
    uint8_t* opt_scratch;   /**< Optimal-parser DP scratch (level >= 6 only,
                                 lazy-allocated, packs dp/parent_len/parent_off/actions).
                                 Also reused as transient scratch for the
                                 length-limited Huffman code-length builder. */
    size_t opt_scratch_cap; /**< Current capacity of opt_scratch in bytes. */
    int checksum_enabled;   /**< 1 if checksum calculation/verification is enabled. */
    int compression_level;  /**< Compression level. */

    /* Block-size derived parameters (computed once at init). */
    size_t chunk_size;    /**< Effective block size in bytes. */
    uint32_t offset_bits; /**< log2(chunk_size) - governs epoch_mark shift. */
    uint32_t offset_mask; /**< (1U << offset_bits) - 1 */
    uint32_t max_epoch;   /**< 1U << (32 - offset_bits) */
} zxc_cctx_t;

/**
 * @brief Initializes a ZXC compression context.
 *
 * Sets up the internal state required for compression operations, allocating
 * necessary buffers based on the chunk size and compression level.
 *
 * @param[out] ctx Pointer to the ZXC compression context structure to initialize.
 * @param[in] chunk_size The size of the data chunk to be compressed. This
 * determines the allocation size for various internal buffers.
 * @param[in] mode The operation mode (1 for compression, 0 for decompression).
 * @param[in] level The desired compression level to be stored in the context.
 * @param[in] checksum_enabled 1 to enable checksums, 0 to disable.
 * @return ZXC_OK on success, or a negative zxc_error_t code (e.g., ZXC_ERROR_MEMORY) if memory
 * allocation fails.
 */
ZXC_EXPORT int zxc_cctx_init(zxc_cctx_t* ctx, const size_t chunk_size, const int mode,
                             const int level, const int checksum_enabled);

/**
 * @brief Frees resources associated with a ZXC compression context.
 *
 * This function releases all internal buffers and tables associated with the
 * given ZXC compression context structure. It does not free the context pointer
 * itself, only its members.
 *
 * @param[in,out] ctx Pointer to the compression context to clean up.
 */
ZXC_EXPORT void zxc_cctx_free(zxc_cctx_t* ctx);

/** @} */ /* end of sans_io */

#ifdef __cplusplus
}
#endif

#endif  // ZXC_SANS_IO_H