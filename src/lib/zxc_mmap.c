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
 * The whole subsystem is a thin OS layer over @ref zxc_decompress_inplace. That
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
 * @par Windows geometry (same, via placeholder mappings)
 * @c MapViewOfFile cannot land inside a plain @c VirtualAlloc reservation, so
 * the same placement is built from the Windows 10 1803 placeholder APIs: reserve
 * `[0, region)` as a placeholder, split it at @c off, then replace the two
 * halves with committed private memory and a @c PAGE_WRITECOPY view of the
 * archive. @c VirtualAlloc2 / @c MapViewOfFile3 are resolved at run time, and
 * anything older (or any file that cannot back a copy-on-write section) falls
 * back to one reservation plus a single copy of the archive -- still one region,
 * still no output allocation. @ref zxc_mmap_is_zerocopy tells the two apart.
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

#include "../../include/zxc_mmap.h"

#include <stddef.h>
#include <stdint.h>

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_constants.h"
#include "../../include/zxc_error.h"
#include "zxc_internal.h"

#if defined(ZXC_MMAP_POSIX)
#include <errno.h>
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
#elif defined(ZXC_MMAP_WIN32)
#include <io.h>     /* _get_osfhandle */
#include <stdlib.h> /* _set_thread_local_invalid_parameter_handler */
#include <windows.h>
#endif

/**
 * @brief Release path a @ref zxc_map_t base address needs, and whether the
 *        archive got there without a copy.
 *
 * The two decode kinds are what @ref zxc_mmap_is_zerocopy reads: every backend
 * that places the archive by mapping reports @ref ZXC_MAP_KIND_MAPPED, and only
 * the Win32 pre-1803 route, which copies the archive into its region, reports
 * @ref ZXC_MAP_KIND_COPIED. Defined outside @c ZXC_MMAP_ENABLED so the
 * unsupported-platform stubs can share @ref zxc_map_reset.
 */
enum {
    ZXC_MAP_KIND_NONE = 0,   /**< Nothing mapped. */
    ZXC_MAP_KIND_VIEW = 1,   /**< Read-only file view (@ref zxc_mmap_open). */
    ZXC_MAP_KIND_COPIED = 2, /**< Decode region, archive copied in (Win32 fallback). */
    ZXC_MAP_KIND_MAPPED = 3  /**< Decode region, archive mapped into it (zero-copy). */
};

/**
 * @brief Zeroes a caller's @ref zxc_map_t so a failed call still leaves a map
 *        that is safe to pass to @ref zxc_mmap_close.
 *
 * Every public entry point calls this before anything can fail, which is what
 * makes "close it even if the call errored" a valid contract.
 *
 * @param[out] m  Map to clear; must be non-NULL, and is left in the same state
 *                a zero-initialised @ref zxc_map_t would be in.
 */
static void zxc_map_reset(zxc_map_t* const m) {
    m->data = NULL;
    m->size = 0;
    m->map_base = NULL;
    m->map_size = 0;
    m->map_handle = NULL;
    m->map_kind = ZXC_MAP_KIND_NONE;
}

#if defined(ZXC_MMAP_ENABLED)

/**
 * @brief Rounds @p v up to the next multiple of the power-of-two @p pow2.
 *
 * @param[in] v     Value to round. Callers check beforehand that @p v is at
 *                  most `SIZE_MAX - (pow2 - 1)`, so the sum cannot wrap.
 * @param[in] pow2  Alignment; must be a power of two (a page size or an
 *                  allocation granularity, both of which always are).
 * @return @p v rounded up to a multiple of @p pow2.
 */
static size_t zxc_round_up_pow2(const size_t v, const size_t pow2) {
    return (v + (pow2 - 1)) & ~(pow2 - 1);
}

/**
 * @brief Computes the single-region geometry for an in-place decode.
 *
 * Two granularities, because the two ends answer to different rules: a file
 * mapping must *start* on an allocation-granularity boundary (64 KiB on Windows,
 * one page on POSIX), while the region it *spans* is page-granular. Sizing the
 * archive slot to exactly `roundup(comp_size, page)` is what lets Windows
 * replace that slot with a view of the archive, which insists the view and the
 * placeholder it replaces cover the same range.
 *
 * @param[in]  comp_size  Archive size in bytes.
 * @param[in]  need       @ref zxc_decompress_inplace_bound for that archive.
 * @param[in]  off_gran   Mapping-start granularity (power of two, multiple of
 *                        @p page).
 * @param[in]  page       Page size (power of two).
 * @param[out] off        Flush-right offset of the archive: aligned to
 *                        @p off_gran (so a file mapping can start there) and
 *                        >= @p need - @p comp_size (so the in-place margin
 *                        holds).
 * @param[out] capacity   Logical buffer capacity to hand
 *                        @ref zxc_decompress_inplace (@p off + @p comp_size).
 * @param[out] region     Total bytes to reserve: @p capacity rounded out to
 *                        whole pages, since a file mapping of @p comp_size
 *                        bytes occupies its last page in full.
 * @return @ref ZXC_OK, or @ref ZXC_ERROR_MEMORY if the geometry overflows
 *         @c size_t.
 */
static int zxc_map_geometry(const size_t comp_size, const size_t need, const size_t off_gran,
                            const size_t page, size_t* const off, size_t* const capacity,
                            size_t* const region) {
    const size_t gap = (need > comp_size) ? need - comp_size : 0;
    if (UNLIKELY(gap > SIZE_MAX - (off_gran - 1))) return ZXC_ERROR_MEMORY;
    const size_t o = zxc_round_up_pow2(gap, off_gran);

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

/** @brief Descriptor the backend works on (a file descriptor here). */
typedef int zxc_desc_t;

/**
 * @brief Opens @p path for the duration of a mapping call.
 *
 * @c O_CLOEXEC keeps the descriptor from leaking into a child forked while the
 * call runs; it degrades to 0 on systems that lack it (see the fallback above).
 *
 * @param[in] path  Path to open read-only.
 * @return An open descriptor, or -1 (see @ref zxc_desc_valid). Every successful
 *         open is closed by @ref zxc_desc_close before the entry point returns.
 */
static zxc_desc_t zxc_desc_open(const char* const path) { return open(path, O_RDONLY | O_CLOEXEC); }

/**
 * @brief Adopts a caller-owned descriptor (no ownership transfer).
 *
 * @param[in] fd  Descriptor supplied by the caller of an @c _fd entry point.
 * @return @p fd unchanged: on POSIX the backend descriptor *is* the file
 *         descriptor, so nothing is opened and nothing must be closed.
 */
static zxc_desc_t zxc_desc_from_fd(const int fd) { return fd; }

/**
 * @brief Reports whether @p d is a descriptor the backend can map.
 *
 * @param[in] d  Descriptor from @ref zxc_desc_open or @ref zxc_desc_from_fd.
 * @return Non-zero when @p d is usable, 0 when the open failed or the caller
 *         passed a negative descriptor (nothing to close in that case).
 */
static int zxc_desc_valid(const zxc_desc_t d) { return d >= 0; }

/**
 * @brief Closes a descriptor this TU opened; mappings outlive it.
 *
 * @param[in] d  Descriptor from a successful @ref zxc_desc_open. Never called on
 *               a caller-owned descriptor, which stays the caller's to close.
 */
static void zxc_desc_close(const zxc_desc_t d) { (void)close(d); }

/**
 * @brief Reports the two granularities the geometry needs.
 *
 * On POSIX both are the page size: @c mmap accepts any page-aligned address.
 *
 * @param[out] off_gran  Granularity a file mapping must start on.
 * @param[out] page      Page size.
 */
static void zxc_map_grains(size_t* const off_gran, size_t* const page) {
    const long ps = sysconf(_SC_PAGESIZE);
    *page = (ps > 0) ? (size_t)ps : 4096U;
    *off_gran = *page;
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

/**
 * @brief Backend of @ref zxc_mmap_open -- whole-file read-only private view.
 *
 * The mapping holds its own reference to the file, so the caller's descriptor
 * can be closed as soon as this returns.
 *
 * @param[in]  d    Descriptor to map.
 * @param[out] out  Receives the view; untouched unless the call succeeds (the
 *                  entry point has already reset it).
 * @return @ref ZXC_OK, the @ref zxc_desc_size error codes, or
 *         @ref ZXC_ERROR_IO if the file cannot be mapped.
 */
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
 * @brief Reads exactly @p len bytes at @p offset, short reads included.
 *
 * @c pread leaves the descriptor's file position alone, which is what lets the
 * @c _fd entry points promise they do not disturb it.
 *
 * @param[in]  d       Descriptor to read from.
 * @param[out] buf     Destination for @p len bytes.
 * @param[in]  len     Number of bytes to read.
 * @param[in]  offset  Absolute offset to read at.
 * @return @ref ZXC_OK, or @ref ZXC_ERROR_IO on a read error or end of file.
 */
static int zxc_desc_read_at(const zxc_desc_t d, void* const buf, const size_t len,
                            const uint64_t offset) {
    uint8_t* p = (uint8_t*)buf;
    size_t left = len;
    uint64_t at = offset;
    while (left > 0) {
        const ssize_t n = pread(d, p, left, (off_t)at);
        /* A handler without SA_RESTART interrupts pread on slow storage
         * (NFS, FUSE); that is not a failed archive. */
        if (UNLIKELY(n < 0 && errno == EINTR)) continue;  // LCOV_EXCL_LINE
        if (UNLIKELY(n <= 0)) return ZXC_ERROR_IO;        // LCOV_EXCL_LINE
        p += (size_t)n;
        at += (uint64_t)n;
        left -= (size_t)n;
    }
    return ZXC_OK;
}

/**
 * @brief Places the archive flush-right in a fresh single region.
 *
 * One anonymous reservation, then the file dropped over its tail pages with
 * @c MAP_FIXED: that replaces exactly the tail of our own reservation, and
 * @c MAP_PRIVATE makes the decoder's writes over consumed compressed bytes
 * copy-on-write, so the archive on disk is never modified.
 *
 * @param[in]  d          Descriptor of the archive.
 * @param[in]  comp_size  Archive size in bytes.
 * @param[in]  off        Flush-right offset from @ref zxc_map_geometry.
 * @param[in]  region     Total bytes to reserve.
 * @param[out] base       Receives the region base on success.
 * @param[out] handle     Receives the backend release token; always NULL here,
 *                        a POSIX region needing nothing beyond @c munmap.
 * @param[out] zerocopy   Receives 1: POSIX has no copying route.
 * @return @ref ZXC_OK, @ref ZXC_ERROR_MEMORY if the region cannot be reserved,
 *         or @ref ZXC_ERROR_IO if the file cannot be mapped over its tail.
 *         Nothing is left mapped on failure.
 */
static int zxc_map_place(const zxc_desc_t d, const size_t comp_size, const size_t off,
                         const size_t region, uint8_t** const base, void** const handle,
                         int* const zerocopy) {
    uint8_t* const p =
        (uint8_t*)mmap(NULL, region, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (UNLIKELY(p == MAP_FAILED)) return ZXC_ERROR_MEMORY;  // LCOV_EXCL_LINE

    if (UNLIKELY(mmap(p + off, comp_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, d, 0) ==
                 MAP_FAILED)) {
        // LCOV_EXCL_START
        (void)munmap(p, region);
        return ZXC_ERROR_IO;
        // LCOV_EXCL_STOP
    }
#if defined(MADV_SEQUENTIAL)
    /* The decoder walks the archive front to back exactly once. */
    (void)madvise(p + off, comp_size, MADV_SEQUENTIAL);
#endif

    *base = p;
    *handle = NULL;
    *zerocopy = 1;
    return ZXC_OK;
}

/**
 * @brief Releases a placement whose decode did not produce a region to keep.
 *
 * @param[in] base    Region base from @ref zxc_map_place.
 * @param[in] region  Its full size in bytes.
 * @param[in] handle  Backend token from @ref zxc_map_place (always NULL here).
 */
static void zxc_map_unplace(uint8_t* const base, const size_t region, void* const handle) {
    (void)handle;
    (void)munmap(base, region);
}

/**
 * @brief Gives back everything the decoded payload does not occupy.
 *
 * @param[in]     base    Region base from @ref zxc_map_place.
 * @param[in]     off     Flush-right offset (unused here: @c munmap can carve
 *                        the archive's own pages out of the region).
 * @param[in]     region  Full region size in bytes.
 * @param[in]     keep    Page-rounded payload size to keep.
 * @param[in,out] handle  Backend token; untouched here.
 * @return Bytes still mapped at @p base, i.e. @p keep.
 */
static size_t zxc_map_trim(uint8_t* const base, const size_t off, const size_t region,
                           const size_t keep, void** const handle) {
    (void)off;
    (void)handle;
    if (keep < region) (void)munmap(base + keep, region - keep);
    return keep;
}

/**
 * @brief Backend of @ref zxc_mmap_close (one call covers both map kinds).
 *
 * A read-only view and a decode region both release with a single @c munmap;
 * the kind only matters on Windows.
 *
 * @param[in] m  Live map; @c map_base / @c map_size describe exactly what is
 *               still mapped, the decode path having already trimmed the rest.
 */
static void zxc_map_release(zxc_map_t* const m) { (void)munmap(m->map_base, m->map_size); }

/*
 * ============================================================================
 * WIN32 BACKEND
 * ============================================================================
 */
#elif defined(ZXC_MMAP_WIN32)

/** @brief Descriptor the backend works on (an OS file handle here). */
typedef HANDLE zxc_desc_t;

// LCOV_EXCL_START - Win32 paths, not reachable on POSIX CI
/**
 * @brief Opens @p path for the duration of a mapping call.
 *
 * @c FILE_SHARE_READ lets other readers (and other mappings of the same
 * archive) proceed while this one runs; write sharing is deliberately withheld,
 * since a file truncated underneath a live view is fatal to touch.
 *
 * @param[in] path  Path to open read-only.
 * @return An open handle, or @c INVALID_HANDLE_VALUE (see @ref zxc_desc_valid).
 *         Every successful open is closed by @ref zxc_desc_close before the
 *         entry point returns.
 */
static zxc_desc_t zxc_desc_open(const char* const path) {
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

#if defined(_MSC_VER)
/**
 * @brief Swallows one CRT invalid-parameter report.
 *
 * @c _get_osfhandle does not merely fail on a descriptor the CRT does not know:
 * it reports an invalid parameter, and the *default* handler terminates the
 * process (the debug CRT asserts, the release CRT calls @c _invoke_watson). A
 * caller passing a stale-but-non-negative descriptor must get
 * @ref ZXC_ERROR_IO, exactly as on POSIX, not a dead process -- so the call is
 * made under this no-op handler.
 */
static void zxc_win_swallow_invalid_param(const wchar_t* expr, const wchar_t* func,
                                          const wchar_t* file, unsigned int line, uintptr_t res) {
    (void)expr;
    (void)func;
    (void)file;
    (void)line;
    (void)res;
}
#endif

/**
 * @brief Adopts a caller-owned CRT descriptor (no ownership transfer).
 *
 * The public API takes a CRT file descriptor on every platform, so the Windows
 * backend translates it to the OS handle the mapping calls need.
 * @c _get_osfhandle already returns an @c intptr_t, hence the single cast.
 *
 * @param[in] fd  CRT descriptor supplied by the caller of an @c _fd entry point.
 * @return The underlying OS handle, or @c INVALID_HANDLE_VALUE for a negative
 *         @p fd or one the CRT does not know. The handle belongs to the CRT
 *         descriptor and is never closed here.
 */
static zxc_desc_t zxc_desc_from_fd(const int fd) {
    if (fd < 0) return INVALID_HANDLE_VALUE;
#if defined(_MSC_VER)
    /* Thread-local, so a concurrent thread's diagnostics are left alone; the
     * previous handler is put back before returning. MinGW's CRT has no such
     * hook and simply returns -1 for an unknown descriptor. */
    const _invalid_parameter_handler prev =
        _set_thread_local_invalid_parameter_handler(zxc_win_swallow_invalid_param);
    const intptr_t h = _get_osfhandle(fd);
    (void)_set_thread_local_invalid_parameter_handler(prev);
#else
    const intptr_t h = _get_osfhandle(fd);
#endif
    if (h == -1 || h == -2) return INVALID_HANDLE_VALUE;
    return (HANDLE)h;
}

/**
 * @brief Reports whether @p d is a handle the backend can map.
 *
 * @param[in] d  Handle from @ref zxc_desc_open or @ref zxc_desc_from_fd.
 * @return Non-zero when @p d is usable, 0 for @c INVALID_HANDLE_VALUE or NULL
 *         (nothing to close in either case).
 */
static int zxc_desc_valid(const zxc_desc_t d) { return d != INVALID_HANDLE_VALUE && d != NULL; }

/**
 * @brief Closes a handle this TU opened; mappings outlive it.
 *
 * @param[in] d  Handle from a successful @ref zxc_desc_open. Never called on a
 *               handle borrowed from a caller's CRT descriptor.
 */
static void zxc_desc_close(const zxc_desc_t d) { (void)CloseHandle(d); }

/**
 * @brief Reports the two granularities the geometry needs.
 *
 * A file view must start on an *allocation*-granularity boundary (64 KiB), so
 * that is what the flush-right offset is aligned to; the archive slot itself is
 * sized in pages, which is the granularity a view and a @c MEM_DECOMMIT work at.
 *
 * @param[out] off_gran  Allocation granularity.
 * @param[out] page      Page size.
 */
static void zxc_map_grains(size_t* const off_gran, size_t* const page) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    *off_gran = si.dwAllocationGranularity ? (size_t)si.dwAllocationGranularity : 65536U;
    *page = si.dwPageSize ? (size_t)si.dwPageSize : 4096U;
}

/**
 * @brief Measures a mappable file.
 *
 * @param[in]  d     Handle to measure.
 * @param[out] size  Receives the file size in bytes.
 * @return @ref ZXC_OK, @ref ZXC_ERROR_IO if the size cannot be queried,
 *         @ref ZXC_ERROR_SRC_TOO_SMALL for an empty file, or
 *         @ref ZXC_ERROR_MEMORY if it exceeds the address space. No regular-file
 *         check is needed here: @c CreateFileMappingA rejects what cannot back a
 *         section, unlike @c mmap.
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
 *
 * @param[in]  d     Handle to map.
 * @param[out] view  Receives the view address; set only on success, and released
 *                   with @c UnmapViewOfFile.
 * @return @ref ZXC_OK, or @ref ZXC_ERROR_IO if the section or the view cannot be
 *         created.
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

/*
 * ----------------------------------------------------------------------------
 * Placeholder mappings: the zero-copy flush-right placement
 * ----------------------------------------------------------------------------
 * MapViewOfFile cannot land inside a plain VirtualAlloc reservation, which is
 * why the naive Win32 port has to copy the archive. The placeholder APIs
 * (Windows 10 1803 / Server 2019) lift exactly that restriction: a reservation
 * can be split into placeholders, and each placeholder replaced -- one by
 * private memory, one by a file view -- giving the same geometry as the POSIX
 * MAP_FIXED path with no copy at all.
 */

/* Flags defined locally so the file still builds against pre-1803 SDKs. */
#ifndef MEM_RESERVE_PLACEHOLDER
#define MEM_RESERVE_PLACEHOLDER 0x00040000
#endif
#ifndef MEM_REPLACE_PLACEHOLDER
#define MEM_REPLACE_PLACEHOLDER 0x00004000
#endif
#ifndef MEM_PRESERVE_PLACEHOLDER
#define MEM_PRESERVE_PLACEHOLDER 0x00000002
#endif

/* The MEM_EXTENDED_PARAMETER argument is always NULL here, so it is typed
 * void* rather than pulling the struct in from a newer SDK. */

/** @brief Signature of @c VirtualAlloc2 (process, base, size, type, prot, ext, count). */
typedef PVOID(WINAPI* zxc_virtual_alloc2_fn)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, void*, ULONG);
/** @brief Signature of @c MapViewOfFile3 (section, process, base, offset, size, ...). */
typedef PVOID(WINAPI* zxc_map_view_of_file3_fn)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG,
                                                ULONG, void*, ULONG);

/** @brief The two placeholder entry points, resolved at run time. */
typedef struct {
    zxc_virtual_alloc2_fn alloc2;  /**< VirtualAlloc2 */
    zxc_map_view_of_file3_fn map3; /**< MapViewOfFile3 */
} zxc_win_ext_t;

/**
 * @brief Resolves @c VirtualAlloc2 / @c MapViewOfFile3 dynamically, once.
 *
 * Dynamic resolution (rather than linking @c onecore.lib) keeps a single binary
 * running on Windows older than 1803, where the copy fallback takes over. The
 * @c FARPROC values are copied instead of cast so no toolchain objects to the
 * function-pointer conversion.
 *
 * The answer cannot change while the process lives, so it is cached: a caller
 * decompressing many small archives would otherwise take the loader lock three
 * times per file. Publishing it needs ordering, not just idempotency -- with
 * plain stores a reader can see the flag over a still-empty cache -- hence the
 * Interlocked pair, a full barrier at any @c _WIN32_WINNT and on every
 * toolchain. Two threads may both resolve and write identical bytes; harmless.
 *
 * @param[out] fns  Receives both entry points; cleared first, so the struct is
 *                  never left holding one resolved pointer and one stale value.
 * @return @ref ZXC_OK, or @ref ZXC_ERROR_UNSUPPORTED when this Windows lacks
 *         the placeholder APIs.
 */
static int zxc_win_ext_resolve(zxc_win_ext_t* const fns) {
    static zxc_win_ext_t g_cache;
    static volatile LONG g_state; /* 0 = not tried, 1 = available, -1 = unavailable */

    /* Acquire: a published state implies g_cache is written. */
    const LONG state = InterlockedCompareExchange(&g_state, 0, 0);
    if (state != 0) {
        *fns = g_cache;
        return (state > 0) ? ZXC_OK : ZXC_ERROR_UNSUPPORTED;
    }

    fns->alloc2 = NULL;
    fns->map3 = NULL;

    HMODULE mod = GetModuleHandleW(L"kernelbase.dll");
    if (!mod) mod = GetModuleHandleW(L"kernel32.dll");
    if (!mod) {
        (void)InterlockedExchange(&g_state, -1);
        return ZXC_ERROR_UNSUPPORTED;
    }

    const FARPROC p_alloc2 = GetProcAddress(mod, "VirtualAlloc2");
    const FARPROC p_map3 = GetProcAddress(mod, "MapViewOfFile3");
    if (!p_alloc2 || !p_map3) {
        (void)InterlockedExchange(&g_state, -1);
        return ZXC_ERROR_UNSUPPORTED;
    }

    ZXC_MEMCPY(&fns->alloc2, &p_alloc2, sizeof(fns->alloc2));
    ZXC_MEMCPY(&fns->map3, &p_map3, sizeof(fns->map3));
    g_cache = *fns;
    /* Release: publish only once g_cache is whole. */
    (void)InterlockedExchange(&g_state, 1);
    return ZXC_OK;
}

/**
 * @brief Creates a copy-on-write section over the whole file.
 *
 * @c PAGE_WRITECOPY needs only @c GENERIC_READ on the file and makes the
 * decoder's writes over consumed compressed bytes private, so the archive on
 * disk is never modified. The size arguments are 0 on purpose: an explicit size
 * beyond the file, with a write-capable protection, would *grow* the file.
 *
 * @param[in]  d        Handle of the archive.
 * @param[out] section  Receives the section handle; set only on success, and
 *                      closed by the caller once a view exists (a live view
 *                      keeps the section alive on its own).
 * @return @ref ZXC_OK, or @ref ZXC_ERROR_IO when the file cannot back a
 *         copy-on-write section, which sends the caller to the copying route.
 */
static int zxc_win_section_cow(const zxc_desc_t d, HANDLE* const section) {
    HANDLE s = CreateFileMappingA(d, NULL, PAGE_WRITECOPY, 0, 0, NULL);
    if (!s) return ZXC_ERROR_IO;
    *section = s;
    return ZXC_OK;
}

/**
 * @brief Places the archive flush-right in a split placeholder reservation.
 *
 *   1. reserve `[0, region)` as one placeholder;
 *   2. split it at @p off, leaving two independently replaceable placeholders;
 *   3. replace `[0, off)` with committed private memory (the decode output);
 *   4. replace `[off, region)` with a @c PAGE_WRITECOPY view of the archive.
 *
 * Every failure path leaves nothing allocated, so the caller can simply fall
 * back to the copying route.
 *
 * @param[in]  section    Copy-on-write section over the archive.
 * @param[in]  comp_size  Archive size in bytes (the view size).
 * @param[in]  off        Flush-right offset, a multiple of the allocation
 *                        granularity (see @ref zxc_map_grains).
 * @param[in]  region     Total reservation size in bytes. `region - off` is
 *                        exactly the page span of @p comp_size, which is what
 *                        lets a view replace that placeholder.
 * @param[out] out_base   Receives the reservation base on success.
 * @param[out] out_view   Receives the mapped view address on success.
 * @return @ref ZXC_OK, @ref ZXC_ERROR_UNSUPPORTED (no placeholder APIs, or this
 *         file cannot back a copy-on-write view), or @ref ZXC_ERROR_MEMORY.
 */
static int zxc_win_place(const HANDLE section, const size_t comp_size, const size_t off,
                         const size_t region, uint8_t** const out_base, void** const out_view) {
    zxc_win_ext_t fns;
    const int rc = zxc_win_ext_resolve(&fns);
    if (rc != ZXC_OK) return rc;
    /* The geometry always leaves room for the output ahead of the archive; bail
     * out rather than split a placeholder at one of its own edges. */
    if (off == 0 || off >= region) return ZXC_ERROR_UNSUPPORTED;

    uint8_t* const base = (uint8_t*)fns.alloc2(
        NULL, NULL, region, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
    if (!base) return ZXC_ERROR_MEMORY;

    /* Split, so the archive slot can be replaced by a view on its own. Both
     * halves stay reserved (MEM_PRESERVE_PLACEHOLDER) and become independently
     * replaceable and independently releasable. */
    if (!VirtualFree(base, off, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
        (void)VirtualFree(base, 0, MEM_RELEASE);
        return ZXC_ERROR_UNSUPPORTED;
    }

    /* [0, off): committed private memory, the decoder's output. */
    if (!fns.alloc2(NULL, base, off, MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
                    PAGE_READWRITE, NULL, 0)) {
        (void)VirtualFree(base, 0, MEM_RELEASE);
        (void)VirtualFree(base + off, 0, MEM_RELEASE);
        return ZXC_ERROR_MEMORY;
    }

    /* [off, region): the archive itself, faulted in from the page cache.
     *
     * Two view sizes are tried because the documented rule ("the mapped view
     * must be exactly the size of the placeholder it replaces") and the section
     * rule ("a view may not extend beyond the section") pull in opposite
     * directions whenever comp_size is not a whole number of pages, which is
     * the common case: the placeholder spans roundup(comp_size, page) while the
     * section is comp_size long. Whether the kernel rounds the requested size
     * up before comparing it against the placeholder is not something the
     * documentation settles, so ask for the file's own length first and, if
     * that is refused, for the placeholder's exact span. A failed map3 consumes
     * nothing, which is what makes the second attempt safe. */
    void* view = fns.map3(section, NULL, base + off, 0, comp_size, MEM_REPLACE_PLACEHOLDER,
                          PAGE_WRITECOPY, NULL, 0);
    if (!view && (region - off) != comp_size) {
        view = fns.map3(section, NULL, base + off, 0, region - off, MEM_REPLACE_PLACEHOLDER,
                        PAGE_WRITECOPY, NULL, 0);
    }
    if (!view) {
        (void)VirtualFree(base, 0, MEM_RELEASE);
        (void)VirtualFree(base + off, 0, MEM_RELEASE);
        return ZXC_ERROR_UNSUPPORTED;
    }

    *out_base = base;
    *out_view = view;
    return ZXC_OK;
}

/**
 * @brief Backend of @ref zxc_mmap_open -- whole-file read-only view.
 *
 * @param[in]  d    Handle to map.
 * @param[out] out  Receives the view; untouched unless the call succeeds.
 * @return @ref ZXC_OK, the @ref zxc_desc_size error codes, or
 *         @ref ZXC_ERROR_IO if the file cannot be mapped.
 */
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
 * @brief Reads exactly @p len bytes at @p offset, short reads included.
 *
 * An @c OVERLAPPED offset is not the Windows @c pread: on a synchronous handle
 * -- what @ref zxc_desc_open and a CRT descriptor both give -- the file pointer
 * still moves to the end of every transfer, which would reposition the caller's
 * descriptor. It is saved and restored instead. @c SetFilePointerEx fails on a
 * handle that has no position (a pipe), which is the guard.
 *
 * @param[in]  d       Handle to read from.
 * @param[out] buf     Destination for @p len bytes.
 * @param[in]  len     Number of bytes to read.
 * @param[in]  offset  Absolute offset to read at.
 * @return @ref ZXC_OK, or @ref ZXC_ERROR_IO on a read error or end of file.
 */
static int zxc_desc_read_at(const zxc_desc_t d, void* const buf, const size_t len,
                            const uint64_t offset) {
    LARGE_INTEGER saved;
    LARGE_INTEGER zero;
    saved.QuadPart = 0;
    zero.QuadPart = 0;
    const BOOL restore = SetFilePointerEx(d, zero, &saved, FILE_CURRENT);

    uint8_t* p = (uint8_t*)buf;
    size_t left = len;
    uint64_t at = offset;
    int rc = ZXC_OK;
    while (left > 0) {
        OVERLAPPED ov;
        ZXC_MEMSET(&ov, 0, sizeof(ov));
        ov.Offset = (DWORD)(at & 0xFFFFFFFFU);
        ov.OffsetHigh = (DWORD)(at >> 32);
        /* Clamped, not cast: a remaining length that is a multiple of 4 GiB
         * truncates to a 0-byte request, i.e. a spurious ZXC_ERROR_IO. */
        const DWORD want = (left > 0x40000000U) ? 0x40000000U : (DWORD)left;
        DWORD got = 0;
        if (UNLIKELY(!ReadFile(d, p, want, &got, &ov) || got == 0)) {
            rc = ZXC_ERROR_IO;
            break;
        }
        p += got;
        at += got;
        left -= got;
    }

    if (restore) (void)SetFilePointerEx(d, saved, NULL, FILE_BEGIN);
    return rc;
}

/**
 * @brief Places the archive flush-right in a fresh single region.
 *
 * Prefers the placeholder placement (@ref zxc_win_place, zero-copy). Older
 * Windows, or a file that cannot back a copy-on-write section, fall back to one
 * reservation plus a single copy of the archive: still one region, still no
 * output allocation. @ref zxc_mmap_is_zerocopy reports which route ran.
 *
 * @param[in]  d          Handle of the archive.
 * @param[in]  comp_size  Archive size in bytes.
 * @param[in]  off        Flush-right offset from @ref zxc_map_geometry.
 * @param[in]  region     Total bytes to reserve.
 * @param[out] base       Receives the region base on success.
 * @param[out] handle     Receives the archive view to unmap later, or NULL when
 *                        the copying route ran.
 * @param[out] zerocopy   Receives 1 for the placeholder route, 0 for the copy.
 * @return @ref ZXC_OK, @ref ZXC_ERROR_MEMORY if the region cannot be reserved,
 *         or @ref ZXC_ERROR_IO if the archive cannot be read into it. Nothing
 *         is left allocated on failure.
 */
static int zxc_map_place(const zxc_desc_t d, const size_t comp_size, const size_t off,
                         const size_t region, uint8_t** const base, void** const handle,
                         int* const zerocopy) {
    uint8_t* p = NULL;
    void* view = NULL;

    /* 1. Zero-copy: a copy-on-write view of the archive, placed flush-right in
     *    a split placeholder reservation. */
    HANDLE section = NULL;
    if (zxc_win_section_cow(d, &section) == ZXC_OK) {
        if (zxc_win_place(section, comp_size, off, region, &p, &view) != ZXC_OK) {
            p = NULL;
            view = NULL;
        }
        /* A mapped view keeps the section alive on its own. */
        (void)CloseHandle(section);
    }

    /* 2. Fallback: one reservation and a single copy of the archive into it. */
    if (!p) {
        p = (uint8_t*)VirtualAlloc(NULL, region, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (UNLIKELY(!p)) return ZXC_ERROR_MEMORY;
        const int rc = zxc_desc_read_at(d, p + off, comp_size, 0);
        if (UNLIKELY(rc != ZXC_OK)) {
            (void)VirtualFree(p, 0, MEM_RELEASE);
            return rc;
        }
    }

    *base = p;
    *handle = view;
    *zerocopy = (view != NULL);
    return ZXC_OK;
}

/**
 * @brief Releases a placement whose decode did not produce a region to keep.
 *
 * @param[in] base    Region base from @ref zxc_map_place.
 * @param[in] region  Its full size in bytes (unused: @c MEM_RELEASE takes the
 *                    whole reservation and insists on a zero size).
 * @param[in] handle  Archive view from @ref zxc_map_place, or NULL.
 */
static void zxc_map_unplace(uint8_t* const base, const size_t region, void* const handle) {
    (void)region;
    if (handle) (void)UnmapViewOfFile(handle);
    (void)VirtualFree(base, 0, MEM_RELEASE);
}

/**
 * @brief Gives back everything the decoded payload does not occupy.
 *
 * Windows has no partial @c UnmapViewOfFile, so the archive slot only goes back
 * when the payload ends before it begins: a payload reaching into the slot keeps
 * the whole region, since `[keep, region)` cannot go back while `[off, keep)`
 * holds decoded bytes. The copying route has no view and always decommits its
 * tail. What survives is released by @ref zxc_map_release.
 *
 * @param[in]     base    Region base from @ref zxc_map_place.
 * @param[in]     off     Flush-right offset: where the archive slot begins.
 * @param[in]     region  Full region size in bytes.
 * @param[in]     keep    Page-rounded payload size to keep.
 * @param[in,out] handle  Archive view; cleared if this call unmapped it.
 * @return Bytes still mapped at @p base: @p keep once the tail is back,
 *         @p region when the view could not be given up.
 */
static size_t zxc_map_trim(uint8_t* const base, const size_t off, const size_t region,
                           const size_t keep, void** const handle) {
    if (!*handle) {
        if (keep < region) (void)VirtualFree(base + keep, region - keep, MEM_DECOMMIT);
        return keep;
    }
    if (keep > off) return region;

    (void)UnmapViewOfFile(*handle);
    *handle = NULL;
    if (keep < off) (void)VirtualFree(base + keep, off - keep, MEM_DECOMMIT);
    return keep;
}

/**
 * @brief Backend of @ref zxc_mmap_close -- views unmap, reservations release.
 *
 * @param[in] m  Live map. @c map_kind selects the release path and @c map_handle
 *               carries the archive view still to unmap, if the trim could not
 *               already give it back.
 */
static void zxc_map_release(zxc_map_t* const m) {
    if (m->map_kind == ZXC_MAP_KIND_VIEW) {
        (void)UnmapViewOfFile(m->map_base);
        return;
    }
    /* Decode region: an archive view may still sit at its far end, and the
     * output part is a reservation of its own (a split placeholder replaced by
     * private memory releases exactly like a plain VirtualAlloc). */
    if (m->map_handle) (void)UnmapViewOfFile(m->map_handle);
    (void)VirtualFree(m->map_base, 0, MEM_RELEASE);
}

// LCOV_EXCL_STOP

#endif /* backend selection */

#if defined(ZXC_MMAP_ENABLED)

/**
 * @brief Answers @ref zxc_decompress_inplace_bound for an on-disk archive.
 *
 * The bound looks at the file header and the file footer and at nothing else,
 * so the two ends are read directly rather than mapping the whole archive just
 * to consult 28 bytes of it -- which also keeps the copying route on Windows
 * from building a second mapping of a file it is about to read anyway.
 *
 * @param[in]  d          Descriptor of the archive.
 * @param[in]  comp_size  Archive size in bytes; the caller has already checked
 *                        it covers a file header and a footer.
 * @param[out] need       Receives the in-place bound in bytes.
 * @return @ref ZXC_OK, @ref ZXC_ERROR_IO if the two ends cannot be read, or
 *         the verdict @ref zxc_inplace_bound_parts reached on an archive it
 *         refused (@ref ZXC_ERROR_BAD_MAGIC, @ref ZXC_ERROR_BAD_HEADER,
 *         @ref ZXC_ERROR_CORRUPT_DATA for a forged footer).
 */
static int zxc_map_bound(const zxc_desc_t d, const size_t comp_size, size_t* const need) {
    uint8_t head[ZXC_FILE_HEADER_SIZE];
    uint8_t foot[ZXC_FILE_FOOTER_SIZE];

    int rc = zxc_desc_read_at(d, head, sizeof(head), 0);
    if (UNLIKELY(rc != ZXC_OK)) return rc;
    rc = zxc_desc_read_at(d, foot, sizeof(foot), (uint64_t)comp_size - ZXC_FILE_FOOTER_SIZE);
    if (UNLIKELY(rc != ZXC_OK)) return rc;  // LCOV_EXCL_LINE

    return zxc_inplace_bound_parts(head, foot, comp_size, need);
}

/**
 * @brief Backend-independent body of @ref zxc_decompress_mmap.
 *
 * Measure the archive, size its region from the two ends of the file, let the
 * backend place it flush-right, decode into the head of that region, then trim
 * back to the payload. Only the placement, the trim and the unwind differ
 * between POSIX and Windows, and each is one backend hook; the sequence itself
 * -- and every check in it -- exists once.
 *
 * @param[in]  d     Descriptor of the archive to decode.
 * @param[out] out   Receives the decoded region; filled only on success, with
 *                   @c map_kind recording whether the archive was mapped or
 *                   copied into place.
 * @param[in]  opts  Decompression options, or NULL for defaults.
 * @return Decompressed size in bytes (> 0), 0 for an empty frame (nothing is
 *         mapped, @p out stays cleared), or a negative @c zxc_error_t:
 *         @ref ZXC_ERROR_SRC_TOO_SMALL if the file is shorter than a frame,
 *         @ref ZXC_ERROR_BAD_HEADER if it is not a ZXC archive,
 *         @ref ZXC_ERROR_IO on a read or mapping failure,
 *         @ref ZXC_ERROR_MEMORY if the region cannot be reserved, or whatever
 *         @ref zxc_decompress_inplace reports for a corrupt archive.
 */
static int64_t zxc_map_decompress(const zxc_desc_t d, zxc_map_t* const out,
                                  const zxc_decompress_opts_t* const opts) {
    size_t comp_size = 0;
    int rc = zxc_desc_size(d, &comp_size);
    if (UNLIKELY(rc != ZXC_OK)) return rc;
    if (UNLIKELY(comp_size < ZXC_FILE_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE))
        return ZXC_ERROR_SRC_TOO_SMALL;

    size_t need = 0;
    rc = zxc_map_bound(d, comp_size, &need);
    if (UNLIKELY(rc != ZXC_OK)) return rc;

    size_t off_gran = 0;
    size_t page = 0;
    zxc_map_grains(&off_gran, &page);
    size_t off = 0;
    size_t capacity = 0;
    size_t region = 0;
    rc = zxc_map_geometry(comp_size, need, off_gran, page, &off, &capacity, &region);
    if (UNLIKELY(rc != ZXC_OK)) return rc;  // LCOV_EXCL_LINE

    uint8_t* base = NULL;
    void* handle = NULL;
    int zerocopy = 0;
    rc = zxc_map_place(d, comp_size, off, region, &base, &handle, &zerocopy);
    if (UNLIKELY(rc != ZXC_OK)) return rc;

    const int64_t decoded = zxc_decompress_inplace(base, capacity, comp_size, opts);
    if (UNLIKELY(decoded <= 0)) {
        /* Errors and the empty frame both leave nothing worth handing back. */
        zxc_map_unplace(base, region, handle);
        return decoded;
    }

    const size_t keep = zxc_round_up_pow2((size_t)decoded, page);
    out->data = base;
    out->size = (size_t)decoded;
    out->map_base = base;
    out->map_size = zxc_map_trim(base, off, region, keep, &handle);
    out->map_handle = handle; /* non-NULL: a mapping still to release on close */
    out->map_kind = zerocopy ? ZXC_MAP_KIND_MAPPED : ZXC_MAP_KIND_COPIED;
    return decoded;
}

/*
 * ============================================================================
 * PUBLIC API
 * ============================================================================
 */

/**
 * @brief Reports whether this build can map files.
 *
 * Public API; see @c zxc_mmap.h.
 */
// cppcheck-suppress unusedFunction
int zxc_mmap_supported(void) { return 1; }

/**
 * @brief Reports whether @p map holds the archive without having copied it.
 *
 * Public API; full contract in @c zxc_mmap.h. Every route but one places the
 * archive by mapping it; @ref ZXC_MAP_KIND_COPIED marks the single exception,
 * the pre-1803 Windows fallback.
 */
// cppcheck-suppress unusedFunction
int zxc_mmap_is_zerocopy(const zxc_map_t* const map) {
    if (UNLIKELY(!map || !map->data)) return 0;
    return map->map_kind != ZXC_MAP_KIND_COPIED;
}

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

/** @brief Nothing can be mapped on this target, so nothing is ever zero-copy. */
// cppcheck-suppress unusedFunction
int zxc_mmap_is_zerocopy(const zxc_map_t* map) {
    (void)map;
    return 0;
}

/** @brief Unsupported on this target; see @ref zxc_mmap_supported. */
// cppcheck-suppress unusedFunction
int zxc_mmap_open(const char* path, zxc_map_t* out) {
    (void)path;
    if (out) zxc_map_reset(out);
    return ZXC_ERROR_UNSUPPORTED;
}

/** @brief Unsupported on this target; see @ref zxc_mmap_supported. */
// cppcheck-suppress unusedFunction
int zxc_mmap_open_fd(int fd, zxc_map_t* out) {
    (void)fd;
    if (out) zxc_map_reset(out);
    return ZXC_ERROR_UNSUPPORTED;
}

/** @brief Unsupported on this target; see @ref zxc_mmap_supported. */
// cppcheck-suppress unusedFunction
int64_t zxc_decompress_mmap(const char* path, zxc_map_t* out, const zxc_decompress_opts_t* opts) {
    (void)path;
    (void)opts;
    if (out) zxc_map_reset(out);
    return ZXC_ERROR_UNSUPPORTED;
}

/** @brief Unsupported on this target; see @ref zxc_mmap_supported. */
// cppcheck-suppress unusedFunction
int64_t zxc_decompress_mmap_fd(int fd, zxc_map_t* out, const zxc_decompress_opts_t* opts) {
    (void)fd;
    (void)opts;
    if (out) zxc_map_reset(out);
    return ZXC_ERROR_UNSUPPORTED;
}

/** @brief Nothing can be mapped on this target, so nothing can be released. */
// cppcheck-suppress unusedFunction
void zxc_mmap_close(zxc_map_t* map) {
    if (map) zxc_map_reset(map);
}

#endif /* ZXC_MMAP_ENABLED */
