/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* File I/O shared by the vector tools (tests/format/ and conformance/). */

#ifndef ZXC_VECTOR_IO_H
#define ZXC_VECTOR_IO_H

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _MSC_VER
#include <io.h>
#include <share.h>
#else
#include <unistd.h> /* close() on the fdopen failure path */
#endif

/* Needed on MinGW too, not just MSVC: without it open() translates newlines and
 * a dump written on Windows never matches one written on Linux. */
#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Owner-only (0600) */
static inline FILE* vio_open_write(const char* path) {
#ifdef _MSC_VER
    int fd = -1;
    _sopen_s(&fd, path, _O_CREAT | _O_WRONLY | _O_TRUNC | _O_BINARY, _SH_DENYNO,
             _S_IREAD | _S_IWRITE);
    if (fd < 0) return NULL;
    FILE* f = _fdopen(fd, "wb");
    if (!f) _close(fd);
#else
    const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
    if (fd < 0) return NULL;
    FILE* f = fdopen(fd, "wb");
    if (!f) close(fd);
#endif
    return f;
}

/* NULL on any failure; on success non-NULL even for an empty file, with
 * *out_size set. Caller frees. */
static inline uint8_t* vio_read_file(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    const long len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)len + 1); /* +1: never NULL when empty */
    if (buf && len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[len] = 0;
        *out_size = (size_t)len;
    }
    return buf;
}

static inline int vio_write_file(const char* path, const uint8_t* data, size_t size) {
    FILE* f = vio_open_write(path);
    if (!f) {
        fprintf(stderr, "  cannot open %s for writing\n", path);
        return -1;
    }
    const int short_write = size && fwrite(data, 1, size, f) != size;
    const int close_failed = fclose(f) != 0;
    if (short_write || close_failed) {
        fprintf(stderr, "  write to %s failed\n", path);
        return -1;
    }
    return 0;
}

static inline int vio_dir_is_safe(const char* dir) {
    return dir[0] != '\0' && strstr(dir, "..") == NULL;
}

#endif /* ZXC_VECTOR_IO_H */
