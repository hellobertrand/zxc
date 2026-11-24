// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file cli.h
 * @brief Declarations shared by the sources of the zxc command line tool.
 */

#ifndef ZXC_CLI_H
#define ZXC_CLI_H

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>  // IWYU pragma: keep
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../../include/zxc_buffer.h"     // IWYU pragma: keep
#include "../../include/zxc_constants.h"  // IWYU pragma: keep
#include "../../include/zxc_dict.h"       // IWYU pragma: keep
#include "../../include/zxc_error.h"      // IWYU pragma: keep
#include "../../include/zxc_stream.h"     // IWYU pragma: keep

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>

// Map POSIX macros to MSVC equivalents
#define F_OK 0
#define access _access
#define isatty _isatty
#define fileno _fileno
#define unlink _unlink
#define close _close
#define fdopen _fdopen
#define getpid _getpid
#define fseeko _fseeki64
#define ftello _ftelli64
#else
#include <dirent.h>
#include <libgen.h>
#include <time.h>
#include <unistd.h>
#endif

/* Enables printf-style format/argument checking by the compiler on GCC/Clang
 * (catches format-vs-argument mismatches at every call site); no-op on MSVC. */
#if defined(__GNUC__) || defined(__clang__)
#define ZXC_PRINTF_FORMAT(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define ZXC_PRINTF_FORMAT(fmt_idx, args_idx)
#endif

// log.c: diagnostics on stderr and their colors

extern int g_quiet;
extern int g_verbose;

/* Color policy (--color): auto = only on a terminal, unless NO_COLOR is set or
 * TERM is "dumb"; always = force; never = plain text. Only diagnostics are
 * colored (Error:/Warning:, -t OK/FAILED). Decided per stream. */
enum { ZXC_COLOR_AUTO = 0, ZXC_COLOR_ALWAYS, ZXC_COLOR_NEVER };
extern int g_color_mode;
extern int g_color_err;  // emit ANSI colors on stderr
extern int g_color_out;  // emit ANSI colors on stdout

#define ZXC_ANSI_RESET "\033[0m"
#define ZXC_ANSI_RED "\033[1;31m"
#define ZXC_ANSI_GREEN "\033[1;32m"
#define ZXC_ANSI_YELLOW "\033[1;33m"

/* The escape sequence when colors are on for that stream, else "". */
#define ZXC_CE(code) (g_color_err ? (code) : "")
#define ZXC_CO(code) (g_color_out ? (code) : "")

// (Re)computes the per-stream color flags from g_color_mode and the env.
void zxc_color_init(void);
// The ZXC_COLOR_* mode named by a --color value, or -1.
int zxc_color_parse(const char* arg);
// Respects -q.
void zxc_log(const char* fmt, ...) ZXC_PRINTF_FORMAT(1, 2);
// Printed even with -q (errors and warnings).
void zxc_err(const char* fmt, ...) ZXC_PRINTF_FORMAT(1, 2);
// Only with -v.
void zxc_log_v(const char* fmt, ...) ZXC_PRINTF_FORMAT(1, 2);
// A byte size as TB/GB/MB/KB/B (base 1000).
void format_size_decimal(uint64_t bytes, char* buf, size_t buf_size);
// Prints s on stdout as a quoted JSON string.
void zxc_json_string(const char* s);

// fileio.c: paths, inputs, and outputs replaced atomically

#define ZXC_TEMP_PREFIX ".zxc-"
#define ZXC_TEMP_SUFFIX ".tmp"

/** @brief An output being written; the caller closes @c f before commit or discard. */
typedef struct {
    FILE* f;
    const char* path;
    char tmp[4096 + 64];  // Written instead of path when replacing it
    int special;          // Device, FIFO or socket: never removed
} zxc_output_t;

/** @brief Input attributes copied to the output, as gzip and zstd do. */
typedef struct {
    int valid;  // 0: input is not a regular file
#ifdef _WIN32
    FILETIME atime;
    FILETIME mtime;
#else
    struct stat st;
#endif
} zxc_file_attrs_t;

int zxc_validate_input_path(const char* path, char* resolved_buffer, size_t buffer_size);
int zxc_validate_output_path(const char* path, char* resolved_buffer, size_t buffer_size);
int zxc_is_directory(const char* path);
int zxc_is_link(const char* path);
const char* zxc_base_name(const char* path);
void zxc_install_signal_cleanup(void);
FILE* zxc_open_input(const char* path);
int zxc_output_open(zxc_output_t* o, const char* path, int force, int owner_only);
int zxc_output_commit(const zxc_output_t* o);
void zxc_output_discard(const zxc_output_t* o);
int zxc_is_input_file(FILE* in, const char* path);
void zxc_get_file_attrs(FILE* f, zxc_file_attrs_t* a);
int zxc_set_file_attrs(FILE* f, const zxc_file_attrs_t* a);

// list.c: -l, and the layout of a container

int zxc_list_archive(const char* path, int json_output, int show_name);
int zxc_archive_has_checksum(FILE* f);

#endif  // ZXC_CLI_H
