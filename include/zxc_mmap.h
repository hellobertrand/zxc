/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_mmap.h
 * @brief Memory-mapped, zero-copy decompression of on-disk archives.
 *
 * This header turns the in-place decoder (@ref zxc_decompress_inplace) into a
 * one-call file API: the archive is *mapped* flush-right into a single region
 * and decoded left-to-right into the head of that same region. Nothing is
 * read() into a staging buffer and no second (output) allocation exists, so an
 * archive decompresses with one mapping and zero copies of the compressed
 * bytes.
 *
 * @par Typical usage
 * @code
 * zxc_map_t out;
 * const int64_t n = zxc_decompress_mmap("payload.zxc", &out, NULL);
 * if (n < 0) { fprintf(stderr, "%s\n", zxc_error_name((int)n)); return 1; }
 *
 * // out.data holds n bytes of decompressed, writable, page-aligned data.
 * consume(out.data, out.size);
 * zxc_mmap_close(&out);
 * @endcode
 *
 * @par Why this is cheaper than read() + zxc_decompress()
 * - No input buffer: compressed pages are faulted in straight from the page
 *   cache into the buffer the decoder reads from.
 * - No output buffer: the decompressed bytes land in the same region, in front
 *   of the compressed bytes still to be consumed.
 * - Peak footprint is @ref zxc_decompress_inplace_bound, and the region is
 *   trimmed back to the payload before the call returns.
 *
 * @par Platform support
 * POSIX (Linux, macOS, *BSD, illumos) is zero-copy as described, and so is
 * Windows 10 1803 / Server 2019 and later, which reaches the same placement
 * through placeholder mappings (@c VirtualAlloc2 + @c MapViewOfFile3, resolved
 * at run time). Older Windows falls back to a single copy of the archive into
 * the same one region: the decode is still in-place with no output allocation.
 * @ref zxc_mmap_is_zerocopy reports which route a given result took. Where the
 * platform has no mapping support at all (freestanding / Emscripten builds),
 * every entry point returns @ref ZXC_ERROR_UNSUPPORTED and
 * @ref zxc_mmap_supported returns 0.
 *
 * @see zxc_buffer.h for @ref zxc_decompress_inplace, the buffer-level primitive
 *      this API is built on.
 */

#ifndef ZXC_MMAP_H
#define ZXC_MMAP_H

#include <stddef.h>
#include <stdint.h>

#include "zxc_export.h"
#include "zxc_opts.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup mmap_api Memory-Mapped API
 * @brief Zero-copy, single-region decompression of files.
 * @{
 */

/**
 * @brief A library-owned memory region: the result of a mapping call.
 *
 * Only @c data and @c size are part of the contract; the remaining fields are
 * bookkeeping for @ref zxc_mmap_close and must not be modified. Release every
 * successfully filled @ref zxc_map_t with @ref zxc_mmap_close.
 */
typedef struct {
    void* data;  /**< First byte of the region (page-aligned), or NULL if empty. */
    size_t size; /**< Number of valid bytes at @c data. */

    /* --- private: owned by the library, do not touch --------------------- */
    void* map_base;   /**< Private: base address passed to the unmap call. */
    size_t map_size;  /**< Private: resident length of the mapping in bytes. */
    void* map_handle; /**< Private: platform mapping handle, or NULL. */
    int map_kind;     /**< Private: which release path @c map_base needs. */
} zxc_map_t;

/**
 * @brief Reports whether this build can map files.
 *
 * @return 1 when the mapping entry points are functional, 0 when they all
 *         return @ref ZXC_ERROR_UNSUPPORTED (freestanding / Emscripten).
 */
ZXC_EXPORT int zxc_mmap_supported(void);

/**
 * @brief Reports whether @p map holds its bytes without a copy having been made.
 *
 * Always 1 for a successful mapping on POSIX and on Windows 10 1803+; 0 on older
 * Windows, where @ref zxc_decompress_mmap copies the archive once into its
 * single region (see @ref zxc_map_t and the platform notes above). Useful to log
 * which route a deployment actually takes; the result of the call is identical
 * either way.
 *
 * @param[in] map  A map filled by this API, or NULL.
 * @return 1 when @p map is a live mapping that involved no copy, 0 otherwise
 *         (including for NULL, a closed, or an empty map).
 */
ZXC_EXPORT int zxc_mmap_is_zerocopy(const zxc_map_t* map);

/**
 * @brief Maps a file read-only, without decoding it.
 *
 * A zero-copy way to feed an on-disk archive to the buffer API
 * (@ref zxc_decompress, @ref zxc_get_decompressed_size) or to a
 * @c zxc_reader_t: the returned bytes are the file's bytes, faulted in on
 * demand. The mapping outlives the descriptor used to create it.
 *
 * @note The region is read-only; writing to it traps.
 * @note As with any file mapping, truncating the file underneath the mapping
 *       makes the vanished pages fatal to touch (@c SIGBUS).
 *
 * @param[in]  path  Path of the file to map (must be a regular file).
 * @param[out] out   Receives the mapping; zeroed first, so it is safe to
 *                   @ref zxc_mmap_close even after a failure.
 * @return @ref ZXC_OK, or a negative @ref zxc_error_t (@ref ZXC_ERROR_IO if
 *         the file cannot be opened, stat'ed, or mapped;
 *         @ref ZXC_ERROR_SRC_TOO_SMALL for an empty file).
 */
ZXC_EXPORT int zxc_mmap_open(const char* path, zxc_map_t* out);

/**
 * @brief Maps an already-open file read-only, without decoding it.
 *
 * Same contract as @ref zxc_mmap_open, for callers that already hold a
 * descriptor (inherited fd, @c fileno of a @c FILE*, a socketpair-passed fd,
 * ...). The descriptor is only used during the call and may be closed as soon
 * as it returns; the mapping stays valid.
 *
 * @param[in]  fd   Readable descriptor on a regular file. On Windows this is a
 *                  CRT file descriptor (as returned by @c _open / @c _fileno).
 * @param[out] out  Receives the mapping (zeroed first).
 * @return @ref ZXC_OK, or a negative @ref zxc_error_t.
 */
ZXC_EXPORT int zxc_mmap_open_fd(int fd, zxc_map_t* out);

/**
 * @brief Decompresses a file into a single mapped region, in place.
 *
 * Maps the archive flush-right into one region sized
 * @ref zxc_decompress_inplace_bound and decodes left-to-right into its head:
 * the compressed bytes are never copied into a staging buffer, and no separate
 * output buffer is allocated. Once decoding is done the region is trimmed back
 * to the payload, so the caller is left holding exactly the decompressed data.
 *
 * Dictionary archives are supported: pass the dictionary in @p opts exactly as
 * for @ref zxc_decompress (they decode through the context's own bounce
 * buffer, which does not alias the region).
 *
 * @note @c out->data is writable and page-aligned, and it is *not* the file's
 *       memory: the mapping is private, so decoding never modifies the archive
 *       on disk. Once the call has returned, nothing in the region is still
 *       backed by the file -- every byte handed back has been written by the
 *       decoder -- so the truncation hazard below does not outlive the call.
 * @note *During* the call the archive is mapped, so the usual file-mapping rule
 *       applies: truncating or overwriting the file from another process while
 *       this runs makes the vanished pages fatal to touch (@c SIGBUS), inside
 *       the library. Archives on a network share or a removable volume, and
 *       files another writer is still appending to, are the realistic cases;
 *       decode from a private copy if that is a possibility.
 * @note An empty frame (stored size 0) succeeds with @c out->data == NULL and
 *       @c out->size == 0; @ref zxc_mmap_close on it is a no-op.
 *
 * @param[in]  path  Path of the archive to decompress.
 * @param[out] out   Receives the decompressed region (zeroed first, so it is
 *                   safe to @ref zxc_mmap_close even after a failure).
 * @param[in]  opts  Decompression options (checksum verification, dictionary),
 *                   or NULL for defaults.
 * @return Decompressed size in bytes (>= 0), or a negative @ref zxc_error_t
 *         (@ref ZXC_ERROR_IO on open / map failure,
 *         @ref ZXC_ERROR_SRC_TOO_SMALL if the file is shorter than a frame,
 *         @ref ZXC_ERROR_BAD_HEADER if it is not a ZXC archive).
 */
ZXC_EXPORT int64_t zxc_decompress_mmap(const char* path, zxc_map_t* out,
                                       const zxc_decompress_opts_t* opts);

/**
 * @brief Decompresses an already-open file into a single mapped region.
 *
 * Same contract as @ref zxc_decompress_mmap, for callers that already hold a
 * descriptor. The whole file is taken to be the archive; the descriptor's file
 * position is irrelevant and left untouched.
 *
 * @param[in]  fd    Readable descriptor on a regular file. On Windows this is a
 *                   CRT file descriptor.
 * @param[out] out   Receives the decompressed region (zeroed first).
 * @param[in]  opts  Decompression options, or NULL for defaults.
 * @return Decompressed size in bytes (>= 0), or a negative @ref zxc_error_t.
 */
ZXC_EXPORT int64_t zxc_decompress_mmap_fd(int fd, zxc_map_t* out,
                                          const zxc_decompress_opts_t* opts);

/**
 * @brief Releases a region obtained from this API and clears @p map.
 *
 * Safe to call on a NULL pointer, on a zeroed @ref zxc_map_t, and on a map
 * whose producing call failed. Never call it twice on the same live mapping
 * (the clear makes a second call a no-op, not a double free).
 *
 * @param[in,out] map  Region to release; zeroed on return.
 */
ZXC_EXPORT void zxc_mmap_close(zxc_map_t* map);

/** @} */ /* end of mmap_api */

#ifdef __cplusplus
}
#endif

#endif /* ZXC_MMAP_H */
