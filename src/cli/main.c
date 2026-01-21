/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

/**
 * @file main.c
 * @brief Command Line Interface (CLI) entry point for the ZXC compression tool.
 *
 * This file handles argument parsing, file I/O setup, platform-specific
 * compatibility layers (specifically for Windows), and the execution of
 * compression, decompression, or benchmarking modes.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/zxc_buffer.h"
#include "../../include/zxc_constants.h"
#include "../../include/zxc_stream.h"

#if defined(_WIN32)
#define ZXC_OS "windows"
#elif defined(__APPLE__)
#define ZXC_OS "darwin"
#elif defined(__linux__)
#define ZXC_OS "linux"
#else
#define ZXC_OS "unknown"
#endif

#if defined(__x86_64__) || defined(_M_AMD64)
#define ZXC_ARCH "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ZXC_ARCH "arm64"
#else
#define ZXC_ARCH "unknown"
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>

// Map POSIX macros to MSVC equivalents
#define F_OK 0
#define access _access
#define isatty _isatty
#define fileno _fileno
#define unlink _unlink
#define fseeko _fseeki64
#define ftello _ftelli64

/**
 * @brief Returns the current monotonic time in seconds using Windows
 * Performance Counter.
 * @return double Time in seconds.
 */
static double zxc_now(void) {
    LARGE_INTEGER frequency;
    LARGE_INTEGER count;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / frequency.QuadPart;
}

struct option {
    const char* name;
    int has_arg;
    int* flag;
    int val;
};
#define no_argument 0
#define required_argument 1
#define optional_argument 2

char* optarg = NULL;
int optind = 1;
int optopt = 0;

/**
 * @brief Minimal implementation of getopt_long for Windows.
 * Handles long options (--option) and short options (-o).
 */
static int getopt_long(int argc, char* const argv[], const char* optstring,
                       const struct option* longopts, int* longindex) {
    if (optind >= argc) return -1;
    char* curr = argv[optind];
    if (curr[0] == '-' && curr[1] == '-') {
        char* name_end = strchr(curr + 2, '=');
        size_t name_len = name_end ? (size_t)(name_end - (curr + 2)) : strlen(curr + 2);
        const struct option* p = longopts;
        while (p && p->name) {
            if (strncmp(curr + 2, p->name, name_len) == 0 && p->name[name_len] == '\0') {
                optind++;
                if (p->has_arg == required_argument) {
                    if (name_end)
                        optarg = name_end + 1;
                    else if (optind < argc)
                        optarg = argv[optind++];
                    else
                        return '?';
                } else if (p->has_arg == optional_argument) {
                    if (name_end)
                        optarg = name_end + 1;
                    else
                        optarg = NULL;
                }
                if (p->flag) {
                    *p->flag = p->val;
                    return 0;
                }
                return p->val;
            }
            p++;
        }
        return '?';
    }
    if (curr[0] == '-') {
        char c = curr[1];
        optind++;
        const char* os = strchr(optstring, c);
        if (!os) return '?';

        if (os[1] == ':') {
            if (os[2] == ':') {
                // Optional argument (::)
                if (curr[2] != '\0')
                    optarg = curr + 2;
                else
                    optarg = NULL;
            } else {
                // Required argument (:)
                if (curr[2] != '\0')
                    optarg = curr + 2;
                else if (optind < argc)
                    optarg = argv[optind++];
                else
                    return '?';
            }
        }
        return c;
    }
    return -1;
}
#else
// POSIX / Linux / macOS Implementation
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/**
 * @brief Returns the current monotonic time in seconds using clock_gettime.
 * @return double Time in seconds.
 */
static double zxc_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
#endif

/**
 * @brief Validates and resolves the input file path to prevent directory traversal
 * and ensure it is a regular file.
 *
 * @param[in] path The raw input path from command line.
 * @param[out] resolved_buffer Buffer to store resolved path (needs sufficient size).
 * @param[in] buffer_size Size of the resolved_buffer.
 * @return 0 on success, -1 on error.
 */
static int zxc_validate_input_path(const char* path, char* resolved_buffer, size_t buffer_size) {
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
    const char* res = realpath(path, resolved_buffer);
    if (!res) {
        // realpath failed (e.g. file does not exist)
        return -1;
    }
    struct stat st;
    if (stat(resolved_buffer, &st) != 0) {
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        errno = EISDIR;  // Generic error for non-regular file
        return -1;
    }
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
static int zxc_validate_output_path(const char* path, char* resolved_buffer, size_t buffer_size) {
#ifdef _WIN32
    if (!_fullpath(resolved_buffer, path, buffer_size)) {
        return -1;
    }
    DWORD attr = GetFileAttributesA(resolved_buffer);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        errno = EISDIR;
        return -1;
    }
    return 0;
#else
    // POSIX output path validation
    char temp_path[4096];
    strncpy(temp_path, path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';

    // Split into dir and base
    char* dir = dirname(temp_path);  // Note: dirname may modify string or return static
    // We need another copy for basename as dirname might modify
    char temp_path2[4096];
    strncpy(temp_path2, path, sizeof(temp_path2) - 1);
    temp_path2[sizeof(temp_path2) - 1] = '\0';
    const char* base = basename(temp_path2);

    char resolved_dir[PATH_MAX];
    if (!realpath(dir, resolved_dir)) {
        // Parent directory must exist
        return -1;
    }

    struct stat st;
    if (stat(resolved_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        errno = EISDIR;
        return -1;
    }

    // Reconstruct valid path: resolved_dir / base
    // Ensure we don't overflow buffer
    int written = snprintf(resolved_buffer, buffer_size, "%s/%s", resolved_dir, base);
    if (written < 0 || (size_t)written >= buffer_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
#endif
}

// CLI Logging Helpers
static int g_quiet = 0;
static int g_verbose = 0;

/**
 * @brief Standard logging function. Respects the global quiet flag.
 */
static void zxc_log(const char* fmt, ...) {
    if (g_quiet) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

/**
 * @brief Verbose logging function. Only prints if verbose is enabled and quiet
 * is disabled.
 */
static void zxc_log_v(const char* fmt, ...) {
    if (!g_verbose || g_quiet) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void print_help(const char* app) {
    printf("Usage: %s [<options>] [<argument>]...\n\n", app);
    printf(
        "Standard Modes:\n"
        "  -z, --compress    Compress FILE {default}\n"
        "  -d, --decompress  Decompress FILE (or stdin -> stdout)\n"
        "  -b, --bench       Benchmark in-memory\n\n"
        "Special Options:\n"
        "  -V, --version     Show version information\n"
        "  -h, --help        Show this help message\n\n"
        "Options:\n"
        "  -1..-5            Compression level {3}\n"
        "  -T, --threads N   Number of threads (0=auto)\n"
        "  -C, --checksum    Enable checksum\n"
        "  -N, --no-checksum Disable checksum\n"
        "  -k, --keep        Keep input file\n"
        "  -f, --force       Force overwrite\n"
        "  -c, --stdout      Write to stdout\n"
        "  -v, --verbose     Verbose mode\n"
        "  -q, --quiet       Quiet mode\n");
}

void print_version(void) {
    char sys_info[256];
#ifdef _WIN32
    snprintf(sys_info, sizeof(sys_info), "%s-%s", ZXC_ARCH, ZXC_OS);
#else
    struct utsname buffer;
    if (uname(&buffer) == 0)
        snprintf(sys_info, sizeof(sys_info), "%s-%s-%s", ZXC_ARCH, ZXC_OS, buffer.release);
    else
        snprintf(sys_info, sizeof(sys_info), "%s-%s", ZXC_ARCH, ZXC_OS);

#endif
    printf("zxc %s\n", ZXC_LIB_VERSION_STR);
    printf("(%s)\n", sys_info);
}

typedef enum { MODE_COMPRESS, MODE_DECOMPRESS, MODE_BENCHMARK } zxc_mode_t;

enum { OPT_VERSION = 1000, OPT_HELP };

/**
 * @brief Main entry point.
 * Parses arguments and dispatches execution to Benchmark, Compress, or
 * Decompress modes.
 */
int main(int argc, char** argv) {
    zxc_mode_t mode = MODE_COMPRESS;
    int num_threads = 0;
    int keep_input = 0;
    int force = 0;
    int to_stdout = 0;
    int iterations = 5;
    int checksum = 0;
    int level = 3;

    static const struct option long_options[] = {
        {"compress", no_argument, 0, 'z'},    {"decompress", no_argument, 0, 'd'},
        {"bench", optional_argument, 0, 'b'}, {"threads", required_argument, 0, 'T'},
        {"keep", no_argument, 0, 'k'},        {"force", no_argument, 0, 'f'},
        {"stdout", no_argument, 0, 'c'},      {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},       {"checksum", no_argument, 0, 'C'},
        {"no-checksum", no_argument, 0, 'N'}, {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "12345b::cCdfhkl:NqT:vVz", long_options, NULL)) != -1) {
        switch (opt) {
            case 'z':
                mode = MODE_COMPRESS;
                break;
            case 'd':
                mode = MODE_DECOMPRESS;
                break;
            case 'b':
                mode = MODE_BENCHMARK;
                if (optarg) iterations = atoi(optarg);
                break;
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
                level = opt - '0';
                break;
            case 'T':
                num_threads = atoi(optarg);
                break;
            case 'k':
                keep_input = 1;
                break;
            case 'f':
                force = 1;
                break;
            case 'c':
                to_stdout = 1;
                break;
            case 'v':
                g_verbose = 1;
                break;
            case 'q':
                g_quiet = 1;
                break;
            case 'C':
                checksum = 1;
                break;
            case 'N':
                checksum = 0;
                break;
            case '?':
            case 'V':
                print_version();
                return 0;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                return 1;
        }
    }

    // Handle positional arguments for mode selection (e.g., "zxc z file")
    if (optind < argc && mode != MODE_BENCHMARK) {
        if (strcmp(argv[optind], "z") == 0) {
            mode = MODE_COMPRESS;
            optind++;
        } else if (strcmp(argv[optind], "d") == 0) {
            mode = MODE_DECOMPRESS;
            optind++;
        } else if (strcmp(argv[optind], "b") == 0) {
            mode = MODE_BENCHMARK;
            optind++;
        }
    }

    /*
     * Benchmark Mode
     * Loads the entire input file into RAM to measure raw algorithm throughput
     * without disk I/O bottlenecks.
     */
    if (mode == MODE_BENCHMARK) {
        if (optind >= argc) {
            zxc_log("Benchmark requires input file.\n");
            return 1;
        }
        const char* in_path = argv[optind];
        if (optind + 1 < argc) iterations = atoi(argv[optind + 1]);

        int ret = 1;
        uint8_t* ram = NULL;
        uint8_t* c_dat = NULL;
        char resolved_path[4096];
        if (zxc_validate_input_path(in_path, resolved_path, sizeof(resolved_path)) != 0) {
            zxc_log("Error: Invalid input file '%s': %s\n", in_path, strerror(errno));
            return 1;
        }

        FILE* f_in = fopen(resolved_path, "rb");
        if (!f_in) goto bench_cleanup;

        if (fseeko(f_in, 0, SEEK_END) != 0) goto bench_cleanup;
        long long fsize = ftello(f_in);
        if (fsize <= 0) goto bench_cleanup;
        size_t in_size = (size_t)fsize;
        if (fseeko(f_in, 0, SEEK_SET) != 0) goto bench_cleanup;

        ram = malloc(in_size);
        if (!ram) goto bench_cleanup;
        if (fread(ram, 1, in_size, f_in) != in_size) goto bench_cleanup;
        fclose(f_in);
        f_in = NULL;

        printf("Input: %s (%zu bytes)\n", in_path, in_size);
        printf("Running %d iterations (Threads: %d)...\n", iterations, num_threads);

#ifdef _WIN32
        printf("Note: Using tmpfile on Windows (slower than fmemopen).\n");
        FILE* fm = tmpfile();
        if (fm) {
            fwrite(ram, 1, in_size, fm);
            rewind(fm);
        }
#else
        FILE* fm = fmemopen(ram, in_size, "rb");
#endif
        if (!fm) goto bench_cleanup;

        double t0 = zxc_now();
        for (int i = 0; i < iterations; i++) {
            rewind(fm);
            zxc_stream_compress(fm, NULL, num_threads, level, checksum);
        }
        double dt_c = zxc_now() - t0;
        fclose(fm);

        size_t max_c = zxc_compress_bound(in_size);
        c_dat = malloc(max_c);
        if (!c_dat) goto bench_cleanup;

#ifdef _WIN32
        FILE* fm_in = tmpfile();
        FILE* fm_out = tmpfile();
        if (!fm_in || !fm_out) {
            if (fm_in) fclose(fm_in);
            if (fm_out) fclose(fm_out);
            goto bench_cleanup;
        }
        fwrite(ram, 1, in_size, fm_in);
        rewind(fm_in);
#else
        FILE* fm_in = fmemopen(ram, in_size, "rb");
        FILE* fm_out = fmemopen(c_dat, max_c, "wb");
        if (!fm_in || !fm_out) {
            if (fm_in) fclose(fm_in);
            if (fm_out) fclose(fm_out);
            goto bench_cleanup;
        }
#endif

        int64_t c_sz = zxc_stream_compress(fm_in, fm_out, num_threads, level, checksum);
        if (c_sz < 0) {
            fclose(fm_in);
            fclose(fm_out);
            goto bench_cleanup;
        }

#ifdef _WIN32
        rewind(fm_out);
        fread(c_dat, 1, (size_t)c_sz, fm_out);
#endif
        fclose(fm_in);
        fclose(fm_out);

#ifdef _WIN32
        FILE* fc = tmpfile();
        if (!fc) goto bench_cleanup;
        fwrite(c_dat, 1, (size_t)c_sz, fc);
        rewind(fc);
#else
        FILE* fc = fmemopen(c_dat, (size_t)c_sz, "rb");
        if (!fc) goto bench_cleanup;
#endif

        t0 = zxc_now();
        for (int i = 0; i < iterations; i++) {
            rewind(fc);
            zxc_stream_decompress(fc, NULL, num_threads, checksum);
        }
        double dt_d = zxc_now() - t0;
        fclose(fc);

        printf("Compressed: %lld bytes (ratio %.3f)\n", (long long)c_sz, (double)in_size / c_sz);
        printf("Avg Compress  : %.3f MiB/s\n",
               (double)in_size * iterations / (1024.0 * 1024.0) / dt_c);
        printf("Avg Decompress: %.3f MiB/s\n",
               (double)in_size * iterations / (1024.0 * 1024.0) / dt_d);
        ret = 0;

    bench_cleanup:
        if (f_in) fclose(f_in);
        free(ram);
        free(c_dat);
        return ret;
    }

    /*
     * File Processing Mode
     * Determines input/output paths. Defaults to stdin/stdout if not specified.
     * Handles output filename generation (.xc extension).
     */
    FILE* f_in = stdin;
    FILE* f_out = stdout;
    char* in_path = NULL;
    char out_path[1024] = {0};
    int use_stdin = 1, use_stdout = 0;

    if (optind < argc && strcmp(argv[optind], "-") != 0) {
        in_path = argv[optind];

        char resolved_path[4096];  // Sufficiently large buffer
        if (zxc_validate_input_path(in_path, resolved_path, sizeof(resolved_path)) != 0) {
            zxc_log("Error: Invalid input file '%s': %s\n", in_path, strerror(errno));
            return 1;
        }

        f_in = fopen(resolved_path, "rb");
        if (!f_in) {
            zxc_log("Error open input %s: %s\n", resolved_path, strerror(errno));
            return 1;
        }
        use_stdin = 0;
        optind++;  // Move past input file
    } else {
        use_stdin = 1;
        use_stdout = 1;  // Default to stdout if reading from stdin
    }

    // Check for optional output file argument
    if (!use_stdin && optind < argc) {
        strncpy(out_path, argv[optind], 1023);
        use_stdout = 0;
    } else if (to_stdout) {
        use_stdout = 1;
    } else if (!use_stdin) {
        // Auto-generate output filename if input is a file and no output specified
        if (mode == MODE_COMPRESS)
            snprintf(out_path, 1024, "%s.xc", in_path);
        else {
            size_t len = strlen(in_path);
            if (len > 3 && !strcmp(in_path + len - 3, ".xc"))
                strncpy(out_path, in_path, len - 3);
            else
                snprintf(out_path, 1024, "%s", in_path);
        }
        use_stdout = 0;
    }

    // Safety check: prevent overwriting input file
    if (!use_stdin && !use_stdout && strcmp(in_path, out_path) == 0) {
        zxc_log("Error: Input and output filenames are identical.\n");
        if (f_in) fclose(f_in);
        return 1;
    }

    // Open output file if not writing to stdout
    if (!use_stdout) {
        char resolved_out[4096];
        if (zxc_validate_output_path(out_path, resolved_out, sizeof(resolved_out)) != 0) {
            zxc_log("Error: Invalid output path '%s': %s\n", out_path, strerror(errno));
            if (f_in) fclose(f_in);
            return 1;
        }

        if (!force && access(resolved_out, F_OK) == 0) {
            zxc_log("Output exists. Use -f.\n");
            fclose(f_in);
            return 1;
        }

#ifdef _WIN32
        f_out = fopen(resolved_out, "wb");
#else
        // Restrict permissions to 0644
        int fd =
            open(resolved_out, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd == -1) {
            zxc_log("Error creating output %s: %s\n", resolved_out, strerror(errno));
            fclose(f_in);
            return 1;
        }
        f_out = fdopen(fd, "wb");
#endif

        if (!f_out) {
            zxc_log("Error open output %s: %s\n", resolved_out, strerror(errno));
            if (f_in) fclose(f_in);
#ifndef _WIN32
            if (fd != -1) close(fd);
#endif
            return 1;
        }
    }

    // Prevent writing binary data to the terminal unless forced
    if (use_stdout && isatty(fileno(stdout)) && mode == MODE_COMPRESS && !force) {
        zxc_log(
            "Refusing to write compressed data to terminal.\n"
            "For help, type: zxc -h\n");
        fclose(f_in);
        return 1;
    }

    // Set stdin/stdout to binary mode if using them
#ifdef _WIN32
    if (use_stdin) _setmode(_fileno(stdin), _O_BINARY);
    if (use_stdout) _setmode(_fileno(stdout), _O_BINARY);

#else
    // On POSIX systems, there's no text/binary distinction, but we ensure
    // no buffering issues occur by using freopen if needed
    if (use_stdin) {
        if (!freopen(NULL, "rb", stdin)) {
            zxc_log("Warning: Failed to reopen stdin in binary mode\n");
        }
    }
    if (use_stdout) {
        if (!freopen(NULL, "wb", stdout)) {
            zxc_log("Warning: Failed to reopen stdout in binary mode\n");
        }
    }
#endif

    // Set large buffers for I/O performance
    char *b1 = malloc(1024 * 1024), *b2 = malloc(1024 * 1024);
    setvbuf(f_in, b1, _IOFBF, 1024 * 1024);
    setvbuf(f_out, b2, _IOFBF, 1024 * 1024);

    zxc_log_v("Starting... (Compression Level %d)\n", level);
    if (g_verbose) zxc_log("Checksum: %s\n", checksum ? "enabled" : "disabled");

    double t0 = zxc_now();
    int64_t bytes = (mode == MODE_COMPRESS)
                        ? zxc_stream_compress(f_in, f_out, num_threads, level, checksum)
                        : zxc_stream_decompress(f_in, f_out, num_threads, checksum);
    double dt = zxc_now() - t0;

    if (!use_stdin)
        fclose(f_in);
    else
        setvbuf(stdin, NULL, _IONBF, 0);

    if (!use_stdout) {
        fclose(f_out);
    } else {
        fflush(f_out);
        setvbuf(stdout, NULL, _IONBF, 0);
    }

    free(b1);
    free(b2);

    if (bytes >= 0) {
        zxc_log_v("Processed %lld bytes in %.3fs\n", (long long)bytes, dt);
        if (!use_stdin && !use_stdout && !keep_input) unlink(in_path);
    } else {
        zxc_log("Operation failed.\n");
        return 1;
    }
    return 0;
}
