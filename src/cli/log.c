// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file log.c
 * @brief Diagnostics of the zxc command line tool: quiet and verbose levels,
 * colored Error:/Warning: prefixes, size formatting.
 */

#include "cli.h"

int g_quiet = 0;
int g_verbose = 0;

int g_color_mode = ZXC_COLOR_AUTO;
int g_color_err = 0;
int g_color_out = 0;

static int zxc_env_set(const char* name) {
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' && strcmp(v, "0") != 0;
}

/* Legacy Windows consoles print escapes verbatim unless VT processing is on;
 * turn it on, and report whether the console understands escapes. */
static int zxc_enable_vt(FILE* f) {
#ifdef _WIN32
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
    HANDLE h = (HANDLE)_get_osfhandle(fileno(f));
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return 1;  // pipe/file: as asked
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return 1;
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    (void)f;
    return 1;
#endif
}

static int zxc_stream_wants_color(FILE* f) {
    if (g_color_mode == ZXC_COLOR_NEVER) return 0;
    if (g_color_mode == ZXC_COLOR_AUTO) {
        if (zxc_env_set("NO_COLOR") || !isatty(fileno(f))) return 0;
        const char* term = getenv("TERM");
        if (term && strcmp(term, "dumb") == 0) return 0;
    }
    return zxc_enable_vt(f);
}

/* (Re)computes the per-stream color flags from g_color_mode and the env. */
void zxc_color_init(void) {
    g_color_err = zxc_stream_wants_color(stderr);
    g_color_out = zxc_stream_wants_color(stdout);
}

// The ZXC_COLOR_* mode named by a --color value, or -1.
int zxc_color_parse(const char* arg) {
    if (arg == NULL) return -1;
    if (strcmp(arg, "auto") == 0) return ZXC_COLOR_AUTO;
    if (strcmp(arg, "always") == 0) return ZXC_COLOR_ALWAYS;
    if (strcmp(arg, "never") == 0) return ZXC_COLOR_NEVER;
    return -1;
}

/* Prints to stderr, coloring a leading "Error:" (red) or "Warning:" (yellow). */
static void zxc_vemit(const char* fmt, va_list args) ZXC_PRINTF_FORMAT(1, 0);
static void zxc_vemit(const char* fmt, va_list args) {
    static const struct {
        const char* tag;
        const char* color;
    } tags[] = {{"Error:", ZXC_ANSI_RED}, {"Warning:", ZXC_ANSI_YELLOW}};
    if (g_color_err) {
        for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
            const size_t n = strlen(tags[i].tag);
            if (strncmp(fmt, tags[i].tag, n) == 0) {
                fprintf(stderr, "%s%s" ZXC_ANSI_RESET, tags[i].color, tags[i].tag);
                fmt += n;
                break;
            }
        }
    }
    vfprintf(stderr, fmt, args);
}

/**
 * @brief Standard logging function. Respects the global quiet flag.
 */
void zxc_log(const char* fmt, ...) {
    if (g_quiet) return;
    va_list args;
    va_start(args, fmt);
    zxc_vemit(fmt, args);
    va_end(args);
}

/**
 * @brief Diagnostic on stderr, printed even with -q (errors and warnings).
 */
void zxc_err(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    zxc_vemit(fmt, args);
    va_end(args);
}

/**
 * @brief Verbose logging function. Only prints if verbose is enabled and quiet
 * is disabled.
 */
void zxc_log_v(const char* fmt, ...) {
    if (!g_verbose || g_quiet) return;
    va_list args;
    va_start(args, fmt);
    zxc_vemit(fmt, args);
    va_end(args);
}

/**
 * @brief Formats a byte size into human-readable TB/GB/MB/KB/B format (Base 1000).
 */
void format_size_decimal(uint64_t bytes, char* buf, size_t buf_size) {
    const double TB = 1000.0 * 1000.0 * 1000.0 * 1000.0;
    const double GB = 1000.0 * 1000.0 * 1000.0;
    const double MB = 1000.0 * 1000.0;
    const double KB = 1000.0;

    if ((double)bytes >= TB)
        snprintf(buf, buf_size, "%.1f TB", (double)bytes / TB);
    else if ((double)bytes >= GB)
        snprintf(buf, buf_size, "%.1f GB", (double)bytes / GB);
    else if ((double)bytes >= MB)
        snprintf(buf, buf_size, "%.1f MB", (double)bytes / MB);
    else if ((double)bytes >= KB)
        snprintf(buf, buf_size, "%.1f KB", (double)bytes / KB);
    else
        snprintf(buf, buf_size, "%llu B", (unsigned long long)bytes);
}

// Bytes of 0x80 and above pass as they are: a name in UTF-8 stays readable.
void zxc_json_string(const char* s) {
    putchar('"');
    for (; *s != '\0'; s++) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            printf("\\%c", c);
        else if (c < 0x20)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}
