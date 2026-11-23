// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file fileio.c
 * @brief File handling of the zxc command line tool: path validation, inputs,
 * outputs replaced atomically, attributes carried over, and the cleanup of a
 * partial output on a fatal signal.
 */

#include "cli.h"

/**
 * @brief Validates and resolves the input file path to prevent directory traversal
 * and ensure it is a regular file.
 *
 * @param[in] path The raw input path from command line.
 * @param[out] resolved_buffer Buffer to store resolved path (needs sufficient size).
 * @param[in] buffer_size Size of the resolved_buffer.
 * @return 0 on success, -1 on error.
 *
 * Security note (CWE-23): paths come from the CLI at the user's own privileges,
 * not from archive content, so traversal is intended (suppressed in .snyk).
 * True only while the format stores no path; an archive mode restoring embedded paths
 * would be a real zip slip risk: re-evaluate the ignore then.
 */
int zxc_validate_input_path(const char* path, char* resolved_buffer, size_t buffer_size) {
#ifdef _WIN32
    if (!_fullpath(resolved_buffer, path, buffer_size)) {
        return -1;
    }
    DWORD attr = GetFileAttributesA(resolved_buffer);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        // Not a valid file or is a directory
        errno = (attr == INVALID_FILE_ATTRIBUTES) ? ENOENT : EISDIR;
        return -1;
    }
    return 0;
#else
    char* const res = realpath(path, NULL);
    if (!res) {
        // realpath failed (e.g. file does not exist)
        return -1;
    }
    struct stat st;
    if (stat(res, &st) != 0) {
        free(res);
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        free(res);
        errno = EISDIR;  // Generic error for non-regular file
        return -1;
    }

    const size_t len = strlen(res);
    if (len >= buffer_size) {
        free(res);
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(resolved_buffer, res, len + 1);
    free(res);
    return 0;
#endif
}

/**
 * @brief Validates and resolves the output file path.
 *
 * @param[in] path The raw output path.
 * @param[out] resolved_buffer Buffer to store resolved path.
 * @param[in] buffer_size Size of the resolved_buffer.
 * @return 0 on success, -1 on error.
 */
int zxc_validate_output_path(const char* path, char* resolved_buffer, size_t buffer_size) {
#ifdef _WIN32
    if (!_fullpath(resolved_buffer, path, buffer_size)) return -1;
    DWORD attr = GetFileAttributesA(resolved_buffer);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        errno = EISDIR;
        return -1;
    }
    return 0;
#else
    // POSIX output path validation
    char* const temp_path = strdup(path);
    if (!temp_path) return -1;

    char* const temp_path2 = strdup(path);
    if (!temp_path2) {
        free(temp_path);
        return -1;
    }

    // Split into dir and base
    char* const dir = dirname(temp_path);  // Note: dirname may modify string or return static
    const char* const base = basename(temp_path2);

    char* const resolved_dir = realpath(dir, NULL);
    if (!resolved_dir) {
        // Parent directory must exist
        free(temp_path);
        free(temp_path2);
        return -1;
    }

    struct stat st;
    if (stat(resolved_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        free(resolved_dir);
        free(temp_path);
        free(temp_path2);
        errno = EISDIR;
        return -1;
    }

    // Reconstruct valid path: resolved_dir / base
    // Ensure we don't overflow buffer
    const int written = snprintf(resolved_buffer, buffer_size, "%s/%s", resolved_dir, base);
    free(resolved_dir);
    free(temp_path);
    free(temp_path2);

    if (written < 0 || (size_t)written >= buffer_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
#endif
}

// OS-specific helpers for directory checks
#ifdef _WIN32
int zxc_is_directory(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

// A link itself, as the walk sees its children; a trailing separator names the target.
int zxc_is_link(const char* path) {
    const DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    return fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK || fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT;
}
#else
int zxc_is_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return 0;
}

// A link itself, as the walk sees its children; a trailing slash names the target.
int zxc_is_link(const char* path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}
#endif

/** @brief The part of @p path after its last separator. */
const char* zxc_base_name(const char* path) {
    const char* slash = strrchr(path, '/');
#ifdef _WIN32
    const char* const bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    return slash ? slash + 1 : path;
}

#ifndef _WIN32
static char g_partial_path[sizeof(((zxc_output_t*)0)->tmp)];
static volatile sig_atomic_t g_partial_armed = 0;

static void zxc_fatal_signal(const int sig) {
    if (g_partial_armed) unlink(g_partial_path);
    signal(sig, SIG_DFL);
    raise(sig);
}

// Signals inherited as ignored stay ignored.
void zxc_install_signal_cleanup(void) {
    static const int sigs[] = {SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGPIPE, SIGXCPU, SIGXFSZ};
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = zxc_fatal_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        struct sigaction old;
        if (sigaction(sigs[i], NULL, &old) == 0 && old.sa_handler != SIG_IGN)
            sigaction(sigs[i], &sa, NULL);
    }
}

static void zxc_set_partial_output(const char* path) {
    g_partial_armed = 0;
    if (path) {
        snprintf(g_partial_path, sizeof(g_partial_path), "%s", path);
        g_partial_armed = 1;
    }
}
#else
// An open file cannot be removed on Windows.
void zxc_install_signal_cleanup(void) {}
static void zxc_set_partial_output(const char* path) { (void)path; }
#endif

static FILE* zxc_stream_from_fd(const int fd, const char* mode) {
    if (fd == -1) return NULL;
    FILE* const f = fdopen(fd, mode);
    if (!f) {
        const int err = errno;  // close() may clobber it
        close(fd);
        errno = err;
    }
    return f;
}

// O_EXCL: a link planted since is refused, not followed.
static FILE* zxc_open_new(const char* path, const int owner_only) {
#ifdef _WIN32
    (void)owner_only;
    const int fd = _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = open(path, O_CREAT | O_EXCL | O_WRONLY,
                        owner_only ? S_IRUSR | S_IWUSR : S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif
    FILE* const f = zxc_stream_from_fd(fd, "wb");
    if (!f && fd != -1) {
        const int err = errno;  // unlink() may clobber it
        unlink(path);
        errno = err;
    }
    return f;
}

static FILE* zxc_open_special(const char* path) {
#ifdef _WIN32
    return fopen(path, "wb");
#else
    // No O_TRUNC: the link may have been swapped since, so recheck the fd
    const int fd = open(path, O_WRONLY);
    struct stat opened;
    if (fd != -1 && (fstat(fd, &opened) != 0 || S_ISREG(opened.st_mode))) {
        close(fd);
        errno = EEXIST;
        return NULL;
    }
    return zxc_stream_from_fd(fd, "wb");
#endif
}

FILE* zxc_open_input(const char* path) {
#ifdef _WIN32
    return fopen(path, "rb");
#else
    // O_NONBLOCK: a FIFO must not hang
    const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY);
    struct stat opened;
    if (fd != -1 && (fstat(fd, &opened) != 0 || !S_ISREG(opened.st_mode))) {
        close(fd);
        errno = EINVAL;
        return NULL;
    }
    return zxc_stream_from_fd(fd, "rb");
#endif
}

/**
 * @brief Opens the output at @p path, which must outlive @p o.
 *
 * Anything there fails with EEXIST unless @p force replaces it, and only at
 * zxc_output_commit(). A device, FIFO or socket is written as is.
 *
 * @param owner_only 1 for 0600, 0 for 0644 (ignored on Windows).
 * @return 0, or -1 with errno set.
 */
int zxc_output_open(zxc_output_t* o, const char* path, const int force, const int owner_only) {
    o->path = path;
    o->tmp[0] = '\0';
#ifdef _WIN32
    struct _stati64 st;
    const int exists = access(path, F_OK) == 0;
    o->special = exists && (_stati64(path, &st) != 0 || (st.st_mode & _S_IFMT) != _S_IFREG);
#else
    struct stat st;
    const int exists = lstat(path, &st) == 0;  // stat fails on a dangling link: replaced
    o->special = exists && stat(path, &st) == 0 && !S_ISREG(st.st_mode);
#endif
    if (exists && !force) {
        errno = EEXIST;
        return -1;
    }
    if (exists && !o->special) {
        // Same directory for an atomic rename; short for names at the length limit.
        const int dir_len = (int)(zxc_base_name(path) - path);
        // Next name past what a killed process left under this pid.
        for (unsigned i = 0; i < 100; i++) {
            snprintf(o->tmp, sizeof(o->tmp), "%.*s" ZXC_TEMP_PREFIX "%ld-%u" ZXC_TEMP_SUFFIX,
                     dir_len, path, (long)getpid(), i);
            o->f = zxc_open_new(o->tmp, owner_only);
            if (o->f || errno != EEXIST) break;
        }
    } else {
        o->f = o->special ? zxc_open_special(path) : zxc_open_new(path, owner_only);
    }
    if (!o->f) return -1;
    if (!o->special) zxc_set_partial_output(o->tmp[0] ? o->tmp : path);
    return 0;
}

/** @brief Puts the finished output in place; 0, or -1 with errno set. */
int zxc_output_commit(const zxc_output_t* o) {
    // Armed until the rename: a signal then only finds the name gone.
    int rc = 0;
    if (o->tmp[0]) {
#ifdef _WIN32
        if (!MoveFileExA(o->tmp, o->path, MOVEFILE_REPLACE_EXISTING)) {
            errno = EACCES;
            rc = -1;
        }
#else
        rc = rename(o->tmp, o->path);
#endif
    }
    if (rc == 0) zxc_set_partial_output(NULL);
    return rc;
}

/** @brief Removes what zxc_output_open() created. */
void zxc_output_discard(const zxc_output_t* o) {
    if (!o->special) unlink(o->tmp[0] ? o->tmp : o->path);
    zxc_set_partial_output(NULL);
}

#ifdef _WIN32
/** @brief A file's identity: volume serial and file ID, tagged by how it was read. */
typedef struct {
    int kind;  // 1: FileIdInfo, 2: file index
    ULONGLONG volume;
    BYTE id[16];
} zxc_file_id_t;

/**
 * @brief Reads the identity of the file open as @p h.
 *
 * The 128-bit FileIdInfo first: the 64-bit file index is not unique on ReFS.
 * Both names of one file are on one filesystem, so they get the same kind.
 */
static int zxc_get_file_id(HANDLE h, zxc_file_id_t* fid) {
    memset(fid, 0, sizeof(*fid));
#if _WIN32_WINNT >= 0x0602
    FILE_ID_INFO fi;
    if (GetFileInformationByHandleEx(h, FileIdInfo, &fi, sizeof(fi))) {
        fid->kind = 1;
        fid->volume = fi.VolumeSerialNumber;
        memcpy(fid->id, &fi.FileId, sizeof(fi.FileId));
        return 1;
    }
#endif
    BY_HANDLE_FILE_INFORMATION bi;
    if (!GetFileInformationByHandle(h, &bi)) return 0;
    fid->kind = 2;
    fid->volume = bi.dwVolumeSerialNumber;
    memcpy(fid->id, &bi.nFileIndexLow, sizeof(bi.nFileIndexLow));
    memcpy(fid->id + 4, &bi.nFileIndexHigh, sizeof(bi.nFileIndexHigh));
    return 1;
}
#endif

/**
 * @brief Whether @p path already names the file open as @p in, whatever its
 *        spelling (letter case, 8.3 short name, a link on Windows).
 *
 * On Windows opening the output truncates whatever it resolves to, so any
 * identity counts. On POSIX the output's name is unlinked first, which only
 * loses the input when that name is its sole one: a hard link or a symlink
 * to the input is safely replaced.
 */
int zxc_is_input_file(FILE* in, const char* path) {
#ifdef _WIN32
    zxc_file_id_t a, b;
    const HANDLE hin = (HANDLE)_get_osfhandle(_fileno(in));
    if (hin == INVALID_HANDLE_VALUE || !zxc_get_file_id(hin, &a)) return 0;
    const HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;  // no output yet
    const int ok = zxc_get_file_id(h, &b);
    CloseHandle(h);
    return ok && memcmp(&a, &b, sizeof(a)) == 0;
#else
    struct stat a, b;
    return fstat(fileno(in), &a) == 0 && lstat(path, &b) == 0 && a.st_dev == b.st_dev &&
           a.st_ino == b.st_ino && b.st_nlink == 1;
#endif
}

/** @brief Records the attributes of @p f if it is a regular file. */
void zxc_get_file_attrs(FILE* f, zxc_file_attrs_t* a) {
#ifdef _WIN32
    const HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
    a->valid = h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_DISK &&
               GetFileTime(h, NULL, &a->atime, &a->mtime);
#else
    a->valid = fstat(fileno(f), &a->st) == 0 && S_ISREG(a->st.st_mode);
#endif
}

/**
 * @brief Copies recorded attributes to @p f, a flushed output file zxc created.
 * @return 0, or -1 if the mode or times could not be set (the owner and group
 *         are best effort).
 */
int zxc_set_file_attrs(FILE* f, const zxc_file_attrs_t* a) {
#ifdef _WIN32
    // Times only: no mode bits on Windows
    const HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
    if (h == INVALID_HANDLE_VALUE || GetFileType(h) != FILE_TYPE_DISK) return 0;  // NUL
    return SetFileTime(h, NULL, &a->atime, &a->mtime) ? 0 : -1;
#else
    const int fd = fileno(f);
    mode_t mode = a->st.st_mode & 0777;  // no setuid, setgid, sticky
    // Owner needs root; a group not kept loses the group bits
    if (fchown(fd, a->st.st_uid, a->st.st_gid) != 0 && fchown(fd, (uid_t)-1, a->st.st_gid) != 0)
        mode &= ~(mode_t)S_IRWXG;
#ifdef __APPLE__
    const struct timespec times[2] = {a->st.st_atimespec, a->st.st_mtimespec};
#else
    const struct timespec times[2] = {a->st.st_atim, a->st.st_mtim};
#endif
    const int rc_mode = fchmod(fd, mode);
    const int rc_times = futimens(fd, times);
    return (rc_mode == 0 && rc_times == 0) ? 0 : -1;
#endif
}
