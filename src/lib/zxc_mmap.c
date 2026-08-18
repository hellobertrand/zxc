/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_mmap.c
 * @brief Read-only whole-file mappings for the buffer and seekable APIs.
 *
 * A thin OS layer: @c mmap on POSIX, @c CreateFileMapping + @c MapViewOfFile on
 * Windows, behind one struct and one release call. The mapping is private and
 * read-only, and it holds its own reference to the file, so the descriptor used
 * to create it can be closed straight away.
 *
 * What it buys is an archive the buffer API can read without a copy -- and a
 * @c zxc_reader_t whose @c read_at is a @c memcpy out of the mapping instead of
 * a @c read() syscall per block, which is also reentrant, hence legal under
 * @ref zxc_seekable_decompress_range_mt.
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

#include "../../include/zxc_error.h"
#include "zxc_internal.h"

#if defined(ZXC_MMAP_POSIX)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif
#elif defined(ZXC_MMAP_WIN32)
#include <io.h>     /* _get_osfhandle */
#include <stdlib.h> /* _set_thread_local_invalid_parameter_handler */
#include <windows.h>
#endif

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
}

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
    return ZXC_OK;
}

/**
 * @brief Backend of @ref zxc_mmap_close.
 *
 * @param[in] m  Live map; @c map_base / @c map_size describe what is mapped.
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
    return ZXC_OK;
}

/**
 * @brief Backend of @ref zxc_mmap_close.
 *
 * @param[in] m  Live map; the view address is @c map_base, which is what
 *               @c UnmapViewOfFile wants (it takes no length).
 */
static void zxc_map_release(zxc_map_t* const m) { (void)UnmapViewOfFile(m->map_base); }

// LCOV_EXCL_STOP

#endif /* backend selection */

#if defined(ZXC_MMAP_ENABLED)

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

/** @brief Nothing can be mapped on this target, so nothing can be released. */
// cppcheck-suppress unusedFunction
void zxc_mmap_close(zxc_map_t* map) {
    if (map) zxc_map_reset(map);
}

#endif /* ZXC_MMAP_ENABLED */
