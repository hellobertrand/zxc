/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_mmap.c
 * @brief Memory-mapped, zero-copy in-place decompression of files.
 *
 * The whole subsystem is a thin OS layer over @ref zxc_decompress_inplace: the
 * buffer-level decoder already tolerates a flush-right archive overlapping its
 * own output, so all that is missing to decode a file without copying it is a
 * way to *place* the archive at the right end of a single region.
 *
 * @par POSIX geometry (true zero-copy)
 * @code
 *   base                                     base+off              base+region
 *     |<---------- decoded output ----------->|<--- archive ------->|
 *     [ anonymous, private, zero-filled       | MAP_FIXED file view ]
 * @endcode
 * One anonymous reservation is taken, then the file is mapped over its tail
 * pages with @c MAP_FIXED. @c off is page-aligned (mapping requirement) and at
 * least `bound - comp_size`, so the logical capacity `off + comp_size` always
 * satisfies @ref zxc_decompress_inplace_bound and the write cursor provably
 * never overtakes the read cursor. The file view is @c MAP_PRIVATE, so the
 * decoder writing over already-consumed compressed bytes is copy-on-write and
 * the archive on disk is never modified.
 *
 * @par Windows geometry (single region, one input copy)
 * @c MapViewOfFile cannot land inside a @c VirtualAlloc reservation without the
 * Win10 placeholder APIs, so the archive is copied once from a read-only view
 * into the same flush-right slot. The decode stays in-place and no output
 * allocation is made.
 *
 * Platforms with no mapping at all (Emscripten, freestanding) compile to stubs
 * returning @ref ZXC_ERROR_UNSUPPORTED, keeping the ABI identical everywhere.
 */

/* glibc/musl hide the POSIX mapping interfaces behind a feature-test macro when
 * the compiler runs in strict mode (-std=cNN, which the stock CMake and Meson
 * builds pair with -D_GNU_SOURCE). Setting the default profile here keeps
 * ad-hoc consumers (a bare `cc -std=c17` over the sources) building. Must precede every
 * include; scoped to Linux because on macOS and the BSDs defining a feature
 * macro *narrows* what the system headers expose. */
#if defined(__linux__) && !defined(_GNU_SOURCE) && !defined(_DEFAULT_SOURCE) && \
    !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include "../../include/zxc_mmap.h"

#include <stddef.h>
#include <stdint.h>

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_constants.h"
#include "../../include/zxc_error.h"
#include "zxc_internal.h"

/*
 * ============================================================================
 * PLATFORM SELECTION
 * ============================================================================
 */
#if defined(_WIN32)
#define ZXC_MMAP_WIN32 1
#elif defined(__EMSCRIPTEN__)
/* Emscripten has no MAP_FIXED file mappings: fall through to the stubs. */
#elif defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
#define ZXC_MMAP_POSIX 1
#endif

#if defined(ZXC_MMAP_POSIX) || defined(ZXC_MMAP_WIN32)
#define ZXC_MMAP_ENABLED 1
#endif

#if defined(ZXC_MMAP_ENABLED)

/** @brief Release path a @ref zxc_map_t base address needs. */
enum {
    ZXC_MAP_KIND_NONE = 0,  /**< Nothing mapped. */
    ZXC_MAP_KIND_VIEW = 1,  /**< Read-only file view (@ref zxc_mmap_open). */
    ZXC_MAP_KIND_REGION = 2 /**< Decode region (@ref zxc_decompress_mmap). */
};

/**
 * @brief Zeroes a caller's @ref zxc_map_t so a failed call still leaves a map
 *        that is safe to pass to @ref zxc_mmap_close.
 */
static void zxc_map_reset(zxc_map_t* const m) {
    m->data = NULL;
    m->size = 0;
    m->map_base = NULL;
    m->map_size = 0;
    m->map_handle = NULL;
    m->map_kind = ZXC_MAP_KIND_NONE;
}

/** @brief Rounds @p v up to the next multiple of the power-of-two @p pow2. */
static size_t zxc_round_up_pow2(const size_t v, const size_t pow2) {
    return (v + (pow2 - 1)) & ~(pow2 - 1);
}

/**
 * @brief Computes the single-region geometry for an in-place decode.
 *
 * @param[in]  comp_size  Archive size in bytes.
 * @param[in]  need       @ref zxc_decompress_inplace_bound for that archive.
 * @param[in]  page       Page size (power of two).
 * @param[out] off        Flush-right offset of the archive: page-aligned (so a
 *                        file mapping can start there) and >= @p need -
 *                        @p comp_size (so the in-place margin holds).
 * @param[out] capacity   Logical buffer capacity to hand
 *                        @ref zxc_decompress_inplace (@p off + @p comp_size).
 * @param[out] region     Total bytes to reserve: @p capacity rounded out to
 *                        whole pages, since a file mapping of @p comp_size
 *                        bytes occupies its last page in full.
 * @return @ref ZXC_OK, or @ref ZXC_ERROR_MEMORY if the geometry overflows
 *         @c size_t.
 */
static int zxc_map_geometry(const size_t comp_size, const size_t need, const size_t page,
                            size_t* const off, size_t* const capacity, size_t* const region) {
    const size_t gap = (need > comp_size) ? need - comp_size : 0;
    if (UNLIKELY(gap > SIZE_MAX - (page - 1))) return ZXC_ERROR_MEMORY;
    const size_t o = zxc_round_up_pow2(gap, page);

    if (UNLIKELY(comp_size > SIZE_MAX - o)) return ZXC_ERROR_MEMORY;
    if (UNLIKELY(comp_size > SIZE_MAX - (page - 1))) return ZXC_ERROR_MEMORY;
    const size_t tail = zxc_round_up_pow2(comp_size, page);
    if (UNLIKELY(tail > SIZE_MAX - o)) return ZXC_ERROR_MEMORY;

    *off = o;
    *capacity = o + comp_size;
    *region = o + tail;
    return ZXC_OK;
}

#endif /* ZXC_MMAP_ENABLED */

/*
 * ============================================================================
 * POSIX BACKEND
 * ============================================================================
 */
#if defined(ZXC_MMAP_POSIX)

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#if !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif

/** @brief Descriptor the backend works on (a file descriptor here). */
typedef int zxc_desc_t;

/** @brief Opens @p path for the duration of a mapping call. */
static zxc_desc_t zxc_desc_open(const char* const path) { return open(path, O_RDONLY | O_CLOEXEC); }

/** @brief Adopts a caller-owned descriptor (no ownership transfer). */
static zxc_desc_t zxc_desc_from_fd(const int fd) { return fd; }

/** @brief True when @p d can be mapped. */
static int zxc_desc_valid(const zxc_desc_t d) { return d >= 0; }

/** @brief Closes a descriptor this TU opened; mappings outlive it. */
static void zxc_desc_close(const zxc_desc_t d) { (void)close(d); }

/** @brief Returns the platform page size (mapping granularity). */
static size_t zxc_page_size(void) {
    const long ps = sysconf(_SC_PAGESIZE);
    return (ps > 0) ? (size_t)ps : 4096u;
}

/**
 * @brief Measures a mappable file.
 *
 * @param[in]  d     Descriptor to measure.
 * @param[out] size  Receives the file size in bytes.
 * @return @ref ZXC_OK, @ref ZXC_ERROR_IO (stat failure or not a regular file:
 *         pipes and sockets cannot be mapped), @ref ZXC_ERROR_SRC_TOO_SMALL for
 *         an empty file, or @ref ZXC_ERROR_MEMORY if it exceeds the address
 *         space.
 */
static int zxc_desc_size(const zxc_desc_t d, size_t* const size) {
    struct stat st;
    if (UNLIKELY(fstat(d, &st) != 0)) return ZXC_ERROR_IO;
    if (UNLIKELY(!S_ISREG(st.st_mode))) return ZXC_ERROR_IO;
    if (UNLIKELY(st.st_size <= 0)) return ZXC_ERROR_SRC_TOO_SMALL;
    if (UNLIKELY((uint64_t)st.st_size > (uint64_t)SIZE_MAX)) return ZXC_ERROR_MEMORY;
    *size = (size_t)st.st_size;
    return ZXC_OK;
}

/** @brief Backend of @ref zxc_mmap_open: whole-file read-only private view. */
static int zxc_map_readonly(const zxc_desc_t d, zxc_map_t* const out) {
    size_t size = 0;
    const int rc = zxc_desc_size(d, &size);
    if (UNLIKELY(rc != ZXC_OK)) return rc;

    void* const view = mmap(NULL, size, PROT_READ, MAP_PRIVATE, d, 0);
    if (UNLIKELY(view == MAP_FAILED)) return ZXC_ERROR_IO;  // LCOV_EXCL_LINE

    out->data = view;
    out->size = size;
    out->map_base = view;
    out->map_size = size;
    out->map_kind = ZXC_MAP_KIND_VIEW;
    return ZXC_OK;
}

/**
 * @brief Backend of @ref zxc_decompress_mmap: reserve, map flush-right, decode,
 *        trim.
 */
static int64_t zxc_map_decompress(const zxc_desc_t d, zxc_map_t* const out,
                                  const zxc_decompress_opts_t* const opts) {
    size_t comp_size = 0;
    int rc = zxc_desc_size(d, &comp_size);
    if (UNLIKELY(rc != ZXC_OK)) return rc;
    if (UNLIKELY(comp_size < ZXC_FILE_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE))
        return ZXC_ERROR_SRC_TOO_SMALL;

    /* Probe pass: the bound only reads the file header and footer, so a
     * throw-away read-only view answers it without copying anything. */
    void* const probe = mmap(NULL, comp_size, PROT_READ, MAP_PRIVATE, d, 0);
    if (UNLIKELY(probe == MAP_FAILED)) return ZXC_ERROR_IO;  // LCOV_EXCL_LINE
    const size_t need = zxc_decompress_inplace_bound(probe, comp_size);
    (void)munmap(probe, comp_size);
    if (UNLIKELY(need == 0)) return ZXC_ERROR_BAD_HEADER;

    const size_t page = zxc_page_size();
    size_t off = 0, capacity = 0, region = 0;
    rc = zxc_map_geometry(comp_size, need, page, &off, &capacity, &region);
    if (UNLIKELY(rc != ZXC_OK)) return rc;  // LCOV_EXCL_LINE

    uint8_t* const base =
        (uint8_t*)mmap(NULL, region, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (UNLIKELY(base == MAP_FAILED)) return ZXC_ERROR_MEMORY;  // LCOV_EXCL_LINE

    /* Land the archive flush-right: MAP_FIXED replaces exactly the tail pages
     * of our own reservation, and MAP_PRIVATE makes the decoder's writes over
     * consumed compressed bytes copy-on-write. */
    if (UNLIKELY(mmap(base + off, comp_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, d,
                      0) == MAP_FAILED)) {
        // LCOV_EXCL_START
        (void)munmap(base, region);
        return ZXC_ERROR_IO;
        // LCOV_EXCL_STOP
    }
#if defined(MADV_SEQUENTIAL)
    /* The decoder walks the archive front to back exactly once. */
    (void)madvise(base + off, comp_size, MADV_SEQUENTIAL);
#endif

    const int64_t decoded = zxc_decompress_inplace(base, capacity, comp_size, opts);
    if (UNLIKELY(decoded <= 0)) {
        /* Errors and the empty frame both leave nothing worth handing back. */
        (void)munmap(base, region);
        return decoded;
    }

    /* Trim: give back the margin and the file-backed tail, so what the caller
     * keeps resident is the payload rather than the in-place bound. */
    const size_t keep = zxc_round_up_pow2((size_t)decoded, page);
    if (keep < region) (void)munmap(base + keep, region - keep);

    out->data = base;
    out->size = (size_t)decoded;
    out->map_base = base;
    out->map_size = keep;
    out->map_kind = ZXC_MAP_KIND_REGION;
    return decoded;
}

/** @brief Backend of @ref zxc_mmap_close (one call covers both map kinds). */
static void zxc_map_release(zxc_map_t* const m) { (void)munmap(m->map_base, m->map_size); }

/*
 * ============================================================================
 * WIN32 BACKEND
 * ============================================================================
 */
#elif defined(ZXC_MMAP_WIN32)

#include <io.h> /* _get_osfhandle */
#include <windows.h>

/** @brief Descriptor the backend works on (an OS file handle here). */
typedef HANDLE zxc_desc_t;

// LCOV_EXCL_START - Win32 paths, not reachable on POSIX CI
/** @brief Opens @p path for the duration of a mapping call. */
static zxc_desc_t zxc_desc_open(const char* const path) {
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

/** @brief Adopts a caller-owned CRT descriptor (no ownership transfer). */
static zxc_desc_t zxc_desc_from_fd(const int fd) {
    if (fd < 0) return INVALID_HANDLE_VALUE;
    return (HANDLE)(intptr_t)_get_osfhandle(fd);
}

/** @brief True when @p d can be mapped. */
static int zxc_desc_valid(const zxc_desc_t d) { return d != INVALID_HANDLE_VALUE && d != NULL; }

/** @brief Closes a handle this TU opened; mappings outlive it. */
static void zxc_desc_close(const zxc_desc_t d) { (void)CloseHandle(d); }

/** @brief Returns the platform page size (VirtualFree decommit granularity). */
static size_t zxc_page_size(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize ? (size_t)si.dwPageSize : 4096u;
}

/**
 * @brief Measures a mappable file.
 * @see the POSIX twin above for the return-code contract.
 */
static int zxc_desc_size(const zxc_desc_t d, size_t* const size) {
    LARGE_INTEGER li;
    if (UNLIKELY(!GetFileSizeEx(d, &li))) return ZXC_ERROR_IO;
    if (UNLIKELY(li.QuadPart <= 0)) return ZXC_ERROR_SRC_TOO_SMALL;
    if (UNLIKELY((uint64_t)li.QuadPart > (uint64_t)SIZE_MAX)) return ZXC_ERROR_MEMORY;
    *size = (size_t)li.QuadPart;
    return ZXC_OK;
}

/**
 * @brief Maps the whole file read-only.
 *
 * The section handle is closed immediately: the returned view keeps it (and the
 * file) alive, which is what lets the descriptor be closed straight after.
 */
static int zxc_win_view(const zxc_desc_t d, void** const view) {
    HANDLE section = CreateFileMappingA(d, NULL, PAGE_READONLY, 0, 0, NULL);
    if (UNLIKELY(!section)) return ZXC_ERROR_IO;
    void* const p = MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0);
    (void)CloseHandle(section);
    if (UNLIKELY(!p)) return ZXC_ERROR_IO;
    *view = p;
    return ZXC_OK;
}

/** @brief Backend of @ref zxc_mmap_open: whole-file read-only view. */
static int zxc_map_readonly(const zxc_desc_t d, zxc_map_t* const out) {
    size_t size = 0;
    int rc = zxc_desc_size(d, &size);
    if (UNLIKELY(rc != ZXC_OK)) return rc;

    void* view = NULL;
    rc = zxc_win_view(d, &view);
    if (UNLIKELY(rc != ZXC_OK)) return rc;

    out->data = view;
    out->size = size;
    out->map_base = view;
    out->map_size = size;
    out->map_kind = ZXC_MAP_KIND_VIEW;
    return ZXC_OK;
}

/**
 * @brief Backend of @ref zxc_decompress_mmap.
 *
 * Same geometry as POSIX, but the archive is copied once from a read-only view
 * into the flush-right slot: a view cannot be placed inside a @c VirtualAlloc
 * reservation without the Win10-only placeholder APIs. Still a single region
 * and no output allocation.
 */
static int64_t zxc_map_decompress(const zxc_desc_t d, zxc_map_t* const out,
                                  const zxc_decompress_opts_t* const opts) {
    size_t comp_size = 0;
    int rc = zxc_desc_size(d, &comp_size);
    if (UNLIKELY(rc != ZXC_OK)) return rc;
    if (UNLIKELY(comp_size < ZXC_FILE_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE))
        return ZXC_ERROR_SRC_TOO_SMALL;

    void* view = NULL;
    rc = zxc_win_view(d, &view);
    if (UNLIKELY(rc != ZXC_OK)) return rc;

    const size_t need = zxc_decompress_inplace_bound(view, comp_size);
    if (UNLIKELY(need == 0)) {
        (void)UnmapViewOfFile(view);
        return ZXC_ERROR_BAD_HEADER;
    }

    const size_t page = zxc_page_size();
    size_t off = 0, capacity = 0, region = 0;
    rc = zxc_map_geometry(comp_size, need, page, &off, &capacity, &region);
    if (UNLIKELY(rc != ZXC_OK)) {
        (void)UnmapViewOfFile(view);
        return rc;
    }

    uint8_t* const base =
        (uint8_t*)VirtualAlloc(NULL, region, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (UNLIKELY(!base)) {
        (void)UnmapViewOfFile(view);
        return ZXC_ERROR_MEMORY;
    }
    ZXC_MEMCPY(base + off, view, comp_size);
    (void)UnmapViewOfFile(view);

    const int64_t decoded = zxc_decompress_inplace(base, capacity, comp_size, opts);
    if (UNLIKELY(decoded <= 0)) {
        (void)VirtualFree(base, 0, MEM_RELEASE);
        return decoded;
    }

    /* Trim: decommit the margin (the reservation itself can only be released
     * whole, which zxc_mmap_close does). */
    const size_t keep = zxc_round_up_pow2((size_t)decoded, page);
    if (keep < region) (void)VirtualFree(base + keep, region - keep, MEM_DECOMMIT);

    out->data = base;
    out->size = (size_t)decoded;
    out->map_base = base;
    out->map_size = keep;
    out->map_kind = ZXC_MAP_KIND_REGION;
    return decoded;
}

/** @brief Backend of @ref zxc_mmap_close: views unmap, reservations release. */
static void zxc_map_release(zxc_map_t* const m) {
    if (m->map_kind == ZXC_MAP_KIND_VIEW)
        (void)UnmapViewOfFile(m->map_base);
    else
        (void)VirtualFree(m->map_base, 0, MEM_RELEASE);
}
// LCOV_EXCL_STOP

#endif /* backend selection */

/*
 * ============================================================================
 * PUBLIC API
 * ============================================================================
 */

#if defined(ZXC_MMAP_ENABLED)

/**
 * @brief Reports whether this build can map files.
 *
 * Public API; see @c zxc_mmap.h.
 */
// cppcheck-suppress unusedFunction
int zxc_mmap_supported(void) { return 1; }

/**
 * @brief Maps a file read-only, without decoding it.
 *
 * Public API; full contract in @c zxc_mmap.h. Opens @p path, delegates to the
 * platform backend, then drops the descriptor: the mapping owns what it needs.
 */
// cppcheck-suppress unusedFunction
int zxc_mmap_open(const char* const path, zxc_map_t* const out) {
    if (UNLIKELY(!out)) return ZXC_ERROR_NULL_INPUT;
    zxc_map_reset(out);
    if (UNLIKELY(!path)) return ZXC_ERROR_NULL_INPUT;

    const zxc_desc_t d = zxc_desc_open(path);
    if (UNLIKELY(!zxc_desc_valid(d))) return ZXC_ERROR_IO;
    const int rc = zxc_map_readonly(d, out);
    zxc_desc_close(d);
    return rc;
}

/**
 * @brief Maps an already-open file read-only, without decoding it.
 *
 * Public API; full contract in @c zxc_mmap.h. The descriptor stays the
 * caller's; only the mapping created from it is handed over.
 */
// cppcheck-suppress unusedFunction
int zxc_mmap_open_fd(const int fd, zxc_map_t* const out) {
    if (UNLIKELY(!out)) return ZXC_ERROR_NULL_INPUT;
    zxc_map_reset(out);

    const zxc_desc_t d = zxc_desc_from_fd(fd);
    if (UNLIKELY(!zxc_desc_valid(d))) return ZXC_ERROR_IO;
    return zxc_map_readonly(d, out);
}

/**
 * @brief Decompresses a file into a single mapped region, in place.
 *
 * Public API; full contract in @c zxc_mmap.h.
 */
// cppcheck-suppress unusedFunction
int64_t zxc_decompress_mmap(const char* const path, zxc_map_t* const out,
                            const zxc_decompress_opts_t* const opts) {
    if (UNLIKELY(!out)) return ZXC_ERROR_NULL_INPUT;
    zxc_map_reset(out);
    if (UNLIKELY(!path)) return ZXC_ERROR_NULL_INPUT;

    const zxc_desc_t d = zxc_desc_open(path);
    if (UNLIKELY(!zxc_desc_valid(d))) return ZXC_ERROR_IO;
    const int64_t rc = zxc_map_decompress(d, out, opts);
    zxc_desc_close(d);
    return rc;
}

/**
 * @brief Decompresses an already-open file into a single mapped region.
 *
 * Public API; full contract in @c zxc_mmap.h.
 */
// cppcheck-suppress unusedFunction
int64_t zxc_decompress_mmap_fd(const int fd, zxc_map_t* const out,
                               const zxc_decompress_opts_t* const opts) {
    if (UNLIKELY(!out)) return ZXC_ERROR_NULL_INPUT;
    zxc_map_reset(out);

    const zxc_desc_t d = zxc_desc_from_fd(fd);
    if (UNLIKELY(!zxc_desc_valid(d))) return ZXC_ERROR_IO;
    return zxc_map_decompress(d, out, opts);
}

/**
 * @brief Releases a region obtained from this API and clears @p map.
 *
 * Public API; full contract in @c zxc_mmap.h.
 */
// cppcheck-suppress unusedFunction
void zxc_mmap_close(zxc_map_t* const map) {
    if (!map || !map->map_base) return;
    zxc_map_release(map);
    zxc_map_reset(map);
}

#else /* !ZXC_MMAP_ENABLED: no mapping primitives on this target */

/** @brief Reports whether this build can map files (it cannot). */
// cppcheck-suppress unusedFunction
int zxc_mmap_supported(void) { return 0; }

/** @brief Unsupported on this target; see @ref zxc_mmap_supported. */
// cppcheck-suppress unusedFunction
int zxc_mmap_open(const char* path, zxc_map_t* out) {
    (void)path;
    if (out) ZXC_MEMSET(out, 0, sizeof(*out));
    return ZXC_ERROR_UNSUPPORTED;
}

/** @brief Unsupported on this target; see @ref zxc_mmap_supported. */
// cppcheck-suppress unusedFunction
int zxc_mmap_open_fd(int fd, zxc_map_t* out) {
    (void)fd;
    if (out) ZXC_MEMSET(out, 0, sizeof(*out));
    return ZXC_ERROR_UNSUPPORTED;
}

/** @brief Unsupported on this target; see @ref zxc_mmap_supported. */
// cppcheck-suppress unusedFunction
int64_t zxc_decompress_mmap(const char* path, zxc_map_t* out, const zxc_decompress_opts_t* opts) {
    (void)path;
    (void)opts;
    if (out) ZXC_MEMSET(out, 0, sizeof(*out));
    return ZXC_ERROR_UNSUPPORTED;
}

/** @brief Unsupported on this target; see @ref zxc_mmap_supported. */
// cppcheck-suppress unusedFunction
int64_t zxc_decompress_mmap_fd(int fd, zxc_map_t* out, const zxc_decompress_opts_t* opts) {
    (void)fd;
    (void)opts;
    if (out) ZXC_MEMSET(out, 0, sizeof(*out));
    return ZXC_ERROR_UNSUPPORTED;
}

/** @brief Nothing can be mapped on this target, so nothing can be released. */
// cppcheck-suppress unusedFunction
void zxc_mmap_close(zxc_map_t* map) {
    if (map) ZXC_MEMSET(map, 0, sizeof(*map));
}

#endif /* ZXC_MMAP_ENABLED */
