/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_mmap.h
 * @brief Read-only, zero-copy mappings of on-disk archives.
 *
 * Maps an archive file into memory and hands back the bytes as they are on
 * disk, faulted in on demand. The point is to feed the buffer API
 * (@ref zxc_decompress, @ref zxc_get_decompressed_size) or a @c zxc_reader_t
 * without reading the file into a buffer first.
 *
 * @par Typical usage
 * @code
 * zxc_map_t archive;
 * if (zxc_mmap_open("payload.zxc", &archive) != ZXC_OK) return 1;
 *
 * const uint64_t n = zxc_get_decompressed_size(archive.data, archive.size);
 * void* dst = n ? malloc((size_t)n) : NULL;
 * if (dst) zxc_decompress(archive.data, archive.size, dst, (size_t)n, NULL);
 *
 * zxc_mmap_close(&archive);   // the mapping outlived the descriptor
 * @endcode
 *
 * @par Why this composes with the seekable API
 * A @c read_at that @c memcpy's out of the mapping needs no @c read() syscall
 * per block and is reentrant, so it is legal on
 * @c zxc_seekable_decompress_range_mt too. See docs/API.md section 7b.
 *
 * @par Platform support
 * POSIX (Linux, macOS, *BSD, illumos) and Windows. Where the platform has no
 * mapping support at all (freestanding / Emscripten builds), every entry point
 * returns @ref ZXC_ERROR_UNSUPPORTED and @ref zxc_mmap_supported returns 0.
 *
 * @note To decompress a file with a minimal memory footprint, the buffer-level
 *       @ref zxc_decompress_inplace decodes into a single buffer that holds the
 *       archive flush-right; see @c zxc_buffer.h. It is not built on this
 *       header, and it does not carry the truncation hazard a live mapping does.
 */

#ifndef ZXC_MMAP_H
#define ZXC_MMAP_H

#include <stddef.h>
#include <stdint.h>

#include "zxc_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup mmap_api Memory-Mapped API
 * @brief Zero-copy read-only mappings of archive files.
 * @{
 */

/**
 * @brief A library-owned mapping: the result of a mapping call.
 *
 * Only @c data and @c size are part of the contract; the rest is bookkeeping
 * for @ref zxc_mmap_close and must not be modified. Release every successfully
 * filled @ref zxc_map_t with @ref zxc_mmap_close.
 */
typedef struct {
    const void* data; /**< First byte of the mapping (page-aligned), or NULL. */
    size_t size;      /**< Number of valid bytes at @c data. */

    /* --- private: owned by the library, do not touch --------------------- */
    void* map_base;  /**< Private: base address passed to the unmap call. */
    size_t map_size; /**< Private: length of the mapping in bytes. */
} zxc_map_t;

/**
 * @brief Reports whether this build can map files.
 *
 * @return 1 when the mapping entry points are functional, 0 when they all
 *         return @ref ZXC_ERROR_UNSUPPORTED (freestanding / Emscripten).
 */
ZXC_EXPORT int zxc_mmap_supported(void);

/**
 * @brief Maps a file read-only.
 *
 * The mapping outlives the descriptor used to create it.
 *
 * @note The region is read-only; writing to it traps.
 * @note As with any file mapping, truncating the file underneath the mapping
 *       makes the vanished pages fatal to touch (@c SIGBUS). The realistic
 *       cases are network shares, removable volumes, and files a writer is
 *       still appending to.
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
 * @brief Maps an already-open file read-only.
 *
 * Same contract as @ref zxc_mmap_open, for callers that already hold a
 * descriptor (inherited fd, @c fileno of a @c FILE*, a socketpair-passed fd,
 * ...). The descriptor is only used during the call and may be closed as soon
 * as it returns; the mapping stays valid, and its file position is untouched.
 *
 * @param[in]  fd   Readable descriptor on a regular file. On Windows this is a
 *                  CRT file descriptor (as returned by @c _open / @c _fileno).
 * @param[out] out  Receives the mapping (zeroed first).
 * @return @ref ZXC_OK, or a negative @ref zxc_error_t.
 */
ZXC_EXPORT int zxc_mmap_open_fd(int fd, zxc_map_t* out);

/**
 * @brief Releases a mapping obtained from this API and clears @p map.
 *
 * Safe to call on a NULL pointer, on a zeroed @ref zxc_map_t, and on a map
 * whose producing call failed. Never call it twice on the same live mapping
 * (the clear makes a second call a no-op, not a double free).
 *
 * @param[in,out] map  Mapping to release; zeroed on return.
 */
ZXC_EXPORT void zxc_mmap_close(zxc_map_t* map);

/** @} */ /* end of mmap_api */

#ifdef __cplusplus
}
#endif

#endif /* ZXC_MMAP_H */
