/*
 * Copyright (c) 2025, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_sans_io.h"
#include "zxc_internal.h"

/*
 * ============================================================================
 * CONTEXT MANAGEMENT
 * ============================================================================
 */

/**
 * @brief Allocates aligned memory in a cross-platform manner.
 *
 * This function provides a unified interface for allocating memory with a specific
 * alignment requirement. It wraps `_aligned_malloc` for Windows
 * environments and `posix_memalign` for POSIX-compliant systems.
 *
 * @param[in] size The size of the memory block to allocate, in bytes.
 * @param[in] alignment The alignment value, which must be a power of two and a multiple
 *                  of `sizeof(void *)`.
 * @return A pointer to the allocated memory block, or NULL if the allocation fails.
 *         The returned pointer must be freed using the corresponding aligned free function.
 */
void* zxc_aligned_malloc(size_t size, size_t alignment) {
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) return NULL;
    return ptr;
#endif
}

/**
 * @brief Frees memory previously allocated with an aligned allocation function.
 *
 * This function provides a cross-platform wrapper for freeing aligned memory.
 * On Windows, it calls `_aligned_free`.
 * On other platforms, it falls back to the standard `free` function.
 *
 * @param[in] ptr A pointer to the memory block to be freed. If ptr is NULL, no operation is
 * performed.
 */
void zxc_aligned_free(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/**
 * @brief Initializes the ZXC compression context.
 *
 * This function allocates memory for internal buffers and structures required
 * for compression based on the provided chunk size and compression level. It
 * sets up hash tables, chain tables, sequence buffers, and literal buffers.
 *
 * **Memory Allocation Strategy:**
 * - **Hash Table:** Size is fixed at `2 * ZXC_LZ_HASH_SIZE`. We use a larger
 * table to reduce collisions.
 * - **Chain Table:** Sized to `chunk_size` to store the previous position for
 * every byte in the chunk, allowing us to traverse the history of matches.
 * - **Sequences & Buffers:** Allocated based on `chunk_size / 4 + 256` to
 * handle the worst-case scenario where we have many small matches.
 *
 * @param[out] ctx Pointer to the ZXC compression context structure to initialize.
 * @param[in] chunk_size The size of the data chunk to be compressed. This
 * determines the allocation size for various internal buffers.
 * @param[in] mode The operation mode (1 for compression, 0 for decompression).
 * @param[in] level The desired compression level to be stored in the context.
 * @param[in] checksum_enabled
 * @return 0 on success, or -1 if memory allocation fails for any of the
 * internal buffers.
 */
int zxc_cctx_init(zxc_cctx_t* ctx, size_t chunk_size, int mode, int level, int checksum_enabled) {
    ZXC_MEMSET(ctx, 0, sizeof(zxc_cctx_t));

    if (mode == 0) return 0;

    size_t max_seq = chunk_size / 4 + 256;
    size_t sz_hash = 2 * ZXC_LZ_HASH_SIZE * sizeof(uint32_t);
    size_t sz_chain = chunk_size * sizeof(uint32_t);
    size_t sz_ll = max_seq * sizeof(uint32_t);
    size_t sz_ml = sz_ll;
    size_t sz_off = sz_ll;
    size_t sz_lit = chunk_size;

    // Calculate sizes with alignment padding (64 bytes for cache line alignment)
    size_t total_size = 0;
    size_t off_hash = total_size;
    total_size += (sz_hash + 63) & ~63;
    size_t off_chain = total_size;
    total_size += (sz_chain + 63) & ~63;
    size_t off_ll = total_size;
    total_size += (sz_ll + 63) & ~63;
    size_t off_ml = total_size;
    total_size += (sz_ml + 63) & ~63;
    size_t off_off = total_size;
    total_size += (sz_off + 63) & ~63;
    size_t off_lit = total_size;
    total_size += (sz_lit + 63) & ~63;

    uint8_t* mem = (uint8_t*)zxc_aligned_malloc(total_size, 64);
    if (UNLIKELY(!mem)) return -1;

    ctx->memory_block = mem;
    ctx->hash_table = (uint32_t*)(mem + off_hash);
    ctx->chain_table = (uint32_t*)(mem + off_chain);
    ctx->buf_ll = (uint32_t*)(mem + off_ll);
    ctx->buf_ml = (uint32_t*)(mem + off_ml);
    ctx->buf_off = (uint32_t*)(mem + off_off);
    ctx->literals = (uint8_t*)(mem + off_lit);

    ctx->epoch = 1;
    ctx->compression_level = level;
    ctx->checksum_enabled = checksum_enabled;

    ZXC_MEMSET(ctx->hash_table, 0, sz_hash);
    return 0;
}

/**
 * @brief Frees the memory allocated for a compression context.
 *
 * This function releases all internal buffers and tables associated with the
 * given ZXC compression context structure. It does not free the context pointer
 * itself, only its members.
 *
 * @param[in,out] ctx Pointer to the compression context to clean up.
 */
void zxc_cctx_free(zxc_cctx_t* ctx) {
    if (ctx->memory_block) {
        zxc_aligned_free(ctx->memory_block);
        ctx->memory_block = NULL;
    }

    if (ctx->lit_buffer) {
        free(ctx->lit_buffer);
        ctx->lit_buffer = NULL;
    }

    ctx->hash_table = NULL;
    ctx->chain_table = NULL;
    ctx->buf_ll = NULL;
    ctx->buf_ml = NULL;
    ctx->buf_off = NULL;
    ctx->literals = NULL;

    ctx->lit_buffer_cap = 0;
}

/*
 * ============================================================================
 * CHECKSUM IMPLEMENTATION (XXH3)
 * ============================================================================
 * Uses XXH3 (64-bit) for extreme performance (> 30GB/s).
 */

#define XXH_INLINE_ALL
#include "../../include/xxhash.h"

uint64_t zxc_checksum(const void* data, size_t len) { return XXH3_64bits(data, len); }

/*
 * ============================================================================
 * HEADER I/O
 * ============================================================================
 * Serialization and deserialization of file and block headers.
 */

/**
 * @brief Writes the ZXC file header to the destination buffer.
 *
 * This function stores the magic word (little-endian) and the version number
 * into the provided buffer. It ensures the buffer has sufficient capacity
 * before writing.
 *
 * @param[out] dst The destination buffer where the header will be written.
 * @param[in] dst_capacity The total capacity of the destination buffer in bytes.
 * @return The number of bytes written (ZXC_FILE_HEADER_SIZE) on success,
 *         or -1 if the destination capacity is insufficient.
 */
int zxc_write_file_header(uint8_t* dst, size_t dst_capacity) {
    if (UNLIKELY(dst_capacity < ZXC_FILE_HEADER_SIZE)) return -1;

    zxc_store_le32(dst, ZXC_MAGIC_WORD);
    dst[4] = ZXC_FILE_FORMAT_VERSION;
    dst[5] = 0;
    dst[6] = 0;
    dst[7] = 0;
    return ZXC_FILE_HEADER_SIZE;
}

/**
 * @brief Reads and validates the ZXC file header from a source buffer.
 *
 * This function checks if the provided source buffer is large enough to contain
 * a ZXC file header and verifies that the magic word and version number match
 * the expected ZXC format specifications.
 *
 * @param[in] src Pointer to the source buffer containing the file data.
 * @param[in] src_size Size of the source buffer in bytes.
 * @return 0 if the header is valid, -1 otherwise (e.g., buffer too small,
 * invalid magic word, or incorrect version).
 */
int zxc_read_file_header(const uint8_t* src, size_t src_size) {
    if (UNLIKELY(src_size < ZXC_FILE_HEADER_SIZE)) return -1;
    if (UNLIKELY(zxc_le32(src) != ZXC_MAGIC_WORD || src[4] != ZXC_FILE_FORMAT_VERSION)) return -1;
    return 0;
}

/**
 * @brief Writes a ZXC block header to a destination buffer.
 *
 * This function serializes the contents of a `zxc_block_header_t` structure
 * into a byte array in little-endian format. It ensures the destination buffer
 * has sufficient capacity before writing.
 *
 * @param[out] dst Pointer to the destination buffer where the header will be
 * written.
 * @param[in] dst_capacity The total size of the destination buffer in bytes.
 * @param[in] bh Pointer to the source block header structure containing the data to
 * write.
 *
 * @return The number of bytes written (ZXC_BLOCK_HEADER_SIZE) on success,
 *         or -1 if the destination buffer capacity is insufficient.
 */
int zxc_write_block_header(uint8_t* dst, size_t dst_capacity, const zxc_block_header_t* bh) {
    if (UNLIKELY(dst_capacity < ZXC_BLOCK_HEADER_SIZE)) return -1;

    dst[0] = bh->block_type;
    dst[1] = bh->block_flags;
    zxc_store_le16(dst + 2, bh->reserved);
    zxc_store_le32(dst + 4, bh->comp_size);
    zxc_store_le32(dst + 8, bh->raw_size);
    return ZXC_BLOCK_HEADER_SIZE;
}

/**
 * @brief Read and parses a ZXC block header from a source buffer.
 *
 * This function extracts the block type, flags, reserved fields, compressed
 * size, and raw size from the first `ZXC_BLOCK_HEADER_SIZE` bytes of the source
 * buffer. It handles endianness conversion for multi-byte fields (Little
 * Endian).
 *
 * @param[in] src       Pointer to the source buffer containing the block data.
 * @param[in] src_size  The size of the source buffer in bytes.
 * @param[out] bh        Pointer to a `zxc_block_header_t` structure where the parsed
 *                  header information will be stored.
 *
 * @return 0 on success, or -1 if the source buffer is smaller than the
 *         required block header size.
 */
int zxc_read_block_header(const uint8_t* src, size_t src_size, zxc_block_header_t* bh) {
    if (UNLIKELY(src_size < ZXC_BLOCK_HEADER_SIZE)) return -1;

    bh->block_type = src[0];
    bh->block_flags = src[1];
    bh->reserved = zxc_le16(src + 2);
    bh->comp_size = zxc_le32(src + 4);
    bh->raw_size = zxc_le32(src + 8);
    return 0;
}

/*
 * ============================================================================
 * BITPACKING UTILITIES
 * ============================================================================
 */

/**
 * @brief Initializes the bit reader structure.
 *
 * This function sets up the bit reader state to read from the specified source
 * buffer. It initializes the internal pointers and loads the initial bits into
 * the accumulator. If the source buffer is large enough (>= 8 bytes), it
 * preloads a full 64-bit word using little-endian ordering. Otherwise, it loads
 * the available bytes one by one.
 *
 * @param[out] br Pointer to the bit reader structure to initialize.
 * @param[in] src Pointer to the source buffer containing the data to read.
 * @param[in] size The size of the source buffer in bytes.
 */
void zxc_br_init(zxc_bit_reader_t* br, const uint8_t* src, size_t size) {
    br->ptr = src;
    br->end = src + size;
    br->accum = zxc_le64(br->ptr);
    br->ptr += 8;
    br->bits = 64;
}

// Packs an array of 32-bit integers into a bitstream using 'bits' bits per
// integer.
/**
 * @brief Packs an array of 32-bit integers into a byte stream using a specified
 * bit width.
 *
 * This function compresses a sequence of 32-bit integers by storing only the
 * specified number of least significant bits for each integer. The resulting
 * bits are concatenated contiguously into the destination buffer.
 *
 * @param[in] src      Pointer to the source array of 32-bit integers.
 * @param[in] count    The number of integers to pack.
 * @param[out] dst      Pointer to the destination byte buffer where packed data will
 * be written. The buffer is zero-initialized before writing.
 * @param[in] dst_cap  The capacity of the destination buffer in bytes.
 * @param[in] bits     The number of bits to use for each integer (bit width).
 *                 Must be between 1 and 32.
 *
 * @return The number of bytes written to the destination buffer on success,
 *         or -1 if the destination capacity is insufficient.
 */
int zxc_bitpack_stream_32(const uint32_t* restrict src, size_t count, uint8_t* restrict dst,
                          size_t dst_cap, uint8_t bits) {
    size_t out_bytes = ((count * bits) + 7) / 8;

    if (UNLIKELY(dst_cap < out_bytes)) return -1;

    size_t bit_pos = 0;
    ZXC_MEMSET(dst, 0, out_bytes);

    for (size_t i = 0; i < count; i++) {
        uint64_t v = (uint64_t)src[i] << (bit_pos % 8);
        size_t byte_idx = bit_pos / 8;
        dst[byte_idx] |= (uint8_t)v;
        if (bits + (bit_pos % 8) > 8) dst[byte_idx + 1] |= (uint8_t)(v >> 8);
        if (bits + (bit_pos % 8) > 16) dst[byte_idx + 2] |= (uint8_t)(v >> 16);
        if (bits + (bit_pos % 8) > 24) dst[byte_idx + 3] |= (uint8_t)(v >> 24);
        if (bits + (bit_pos % 8) > 32) dst[byte_idx + 4] |= (uint8_t)(v >> 32);
        bit_pos += bits;
    }
    return (int)out_bytes;
}

/**
 * @brief Writes the numeric header structure to a binary buffer.
 *
 * This function serializes the contents of a `zxc_num_header_t` structure into
 * the provided destination buffer in little-endian format. It ensures that the
 * buffer has sufficient remaining space before writing.
 *
 * The binary layout written is as follows:
 * - Offset 0: Number of values (64-bit, Little Endian)
 * - Offset 8: Frame size (16-bit, Little Endian)
 * - Offset 10: Reserved/Padding (16-bit, set to 0)
 * - Offset 12: Reserved/Padding (32-bit, set to 0)
 *
 * @param[out] dst Pointer to the destination buffer where the header will be
 * written.
 * @param[in] rem The remaining size available in the destination buffer.
 * @param[in] nh Pointer to the source numeric header structure containing the
 * values to write.
 *
 * @return The number of bytes written (ZXC_NUM_HEADER_BINARY_SIZE) on success,
 *         or -1 if the remaining buffer size is insufficient.
 */
int zxc_write_num_header(uint8_t* dst, size_t rem, const zxc_num_header_t* nh) {
    if (UNLIKELY(rem < ZXC_NUM_HEADER_BINARY_SIZE)) return -1;

    zxc_store_le64(dst, nh->n_values);
    zxc_store_le16(dst + 8, nh->frame_size);
    zxc_store_le16(dst + 10, 0);
    zxc_store_le32(dst + 12, 0);
    return ZXC_NUM_HEADER_BINARY_SIZE;
}

/**
 * @brief Reads the numerical header from a binary source.
 *
 * This function parses the header information from the provided source buffer
 * and populates the given `zxc_num_header_t` structure. It expects the source
 * to contain at least `ZXC_NUM_HEADER_BINARY_SIZE` bytes.
 *
 * The header structure in the binary format is expected to be:
 * - 8 bytes: Number of values (Little Endian 64-bit integer)
 * - 2 bytes: Frame size (Little Endian 16-bit integer)
 *
 * @param[in] src Pointer to the source buffer containing the binary header data.
 * @param[in] src_size The size of the source buffer in bytes.
 * @param[out] nh Pointer to the `zxc_num_header_t` structure to be populated.
 *
 * @return 0 on success, or -1 if the source buffer is smaller than the required
 * header size.
 */
int zxc_read_num_header(const uint8_t* src, size_t src_size, zxc_num_header_t* nh) {
    if (UNLIKELY(src_size < ZXC_NUM_HEADER_BINARY_SIZE)) return -1;

    nh->n_values = zxc_le64(src);
    nh->frame_size = zxc_le16(src + 8);
    return 0;
}

/**
 * @brief Writes the general header and section descriptors to a destination
 * buffer.
 *
 * This function serializes the provided general header structure
 * (`zxc_gnr_header_t`) and an array of four section descriptors
 * (`zxc_section_desc_t`) into a binary format at the specified destination
 * memory location.
 *
 * The binary layout written is as follows:
 * - **General Header (16 bytes):**
 *   - 4 bytes: Number of sequences (Little Endian)
 *   - 4 bytes: Number of literals (Little Endian)
 *   - 1 byte:  Literal encoding type
 *   - 1 byte:  Literal length encoding type
 *   - 1 byte:  Match length encoding type
 *   - 1 byte:  Offset encoding type
 *   - 4 bytes: Reserved/Padding (set to 0)
 * - **Section Descriptors (4 entries * 12 bytes each):**
 *   - 4 bytes: Compressed size (Little Endian)
 *   - 4 bytes: Raw size (Little Endian)
 *   - 4 bytes: Reserved/Padding (set to 0)
 *
 * @param[out] dst   Pointer to the destination buffer where data will be written.
 * @param[in] rem   Remaining size in bytes available in the destination buffer.
 * @param[in] gh    Pointer to the source general header structure.
 * @param[in] desc  Array of 4 section descriptors to be written.
 *
 * @return The total number of bytes written on success, or -1 if the remaining
 *         buffer space (`rem`) is insufficient.
 */
int zxc_write_gnr_header_and_desc(uint8_t* dst, size_t rem, const zxc_gnr_header_t* gh,
                                  const zxc_section_desc_t desc[4]) {
    size_t needed = ZXC_GNR_HEADER_BINARY_SIZE + 4 * ZXC_SECTION_DESC_BINARY_SIZE;

    if (UNLIKELY(rem < needed)) return -1;

    zxc_store_le32(dst, gh->n_sequences);
    zxc_store_le32(dst + 4, gh->n_literals);

    dst[8] = gh->enc_lit;
    dst[9] = gh->enc_litlen;
    dst[10] = gh->enc_mlen;
    dst[11] = gh->enc_off;

    zxc_store_le32(dst + 12, 0);
    uint8_t* p = dst + ZXC_GNR_HEADER_BINARY_SIZE;

    for (int i = 0; i < 4; i++) {
        zxc_store_le64(p, desc[i].sizes);
        p += ZXC_SECTION_DESC_BINARY_SIZE;
    }

    return (int)needed;
}

/**
 * Reads the general header and section descriptors from a binary source buffer.
 *
 * This function parses the initial bytes of a ZXC compressed stream to populate
 * the general header structure and an array of four section descriptors. It
 * verifies that the source buffer is large enough to contain the required
 * binary data before reading.
 *
 * @param[in] src   Pointer to the source buffer containing the binary data.
 * @param[in] len   Length of the source buffer in bytes.
 * @param[out] gh    Pointer to the `zxc_gnr_header_t` structure to be populated.
 * @param[out] desc  Array of 4 `zxc_section_desc_t` structures to be populated with
 * section details.
 *
 * @return 0 on success, or -1 if the source buffer length is insufficient.
 */
int zxc_read_gnr_header_and_desc(const uint8_t* src, size_t len, zxc_gnr_header_t* gh,
                                 zxc_section_desc_t desc[4]) {
    size_t needed = ZXC_GNR_HEADER_BINARY_SIZE + 4 * ZXC_SECTION_DESC_BINARY_SIZE;

    if (UNLIKELY(len < needed)) return -1;

    gh->n_sequences = zxc_le32(src);
    gh->n_literals = zxc_le32(src + 4);
    gh->enc_lit = src[8];
    gh->enc_litlen = src[9];
    gh->enc_mlen = src[10];
    gh->enc_off = src[11];

    const uint8_t* p = src + ZXC_GNR_HEADER_BINARY_SIZE;

    for (int i = 0; i < 4; i++) {
        desc[i].sizes = zxc_le64(p);
        p += ZXC_SECTION_DESC_BINARY_SIZE;
    }
    return 0;
}

/**
 * @brief Calculates the maximum possible size of the compressed output.
 *
 * This function estimates the worst-case scenario for the compressed size based
 * on the input size. It accounts for the file header, block headers, and
 * potential overhead for each chunk, ensuring the destination buffer is large
 * enough to hold the result even if the data is incompressible.
 *
 * @param[in] input_size The size of the uncompressed input data in bytes.
 * @return The maximum potential size of the compressed data in bytes.
 */
size_t zxc_compress_bound(size_t input_size) {
    if (input_size > SIZE_MAX - (SIZE_MAX >> 10)) return 0;

    size_t n = (input_size + ZXC_CHUNK_SIZE - 1) / ZXC_CHUNK_SIZE;
    if (n == 0) n = 1;
    return ZXC_FILE_HEADER_SIZE + (n * (ZXC_BLOCK_HEADER_SIZE + ZXC_BLOCK_CHECKSUM_SIZE + 64)) +
           input_size;
}
