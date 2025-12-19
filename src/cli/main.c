/*
 * Copyright (c) 2025, Bertrand Lebonnois
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
#include <io.h>
#include <sys/stat.h>
#include <windows.h>

// Map POSIX macros to MSVC equivalents
#define F_OK 0
#define access _access
#define isatty _isatty
#define fileno _fileno
#define unlink _unlink
#define stat _stat

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
            if (curr[2] != '\0')
                optarg = curr + 2;
            else if (optind < argc)
                optarg = argv[optind++];
            else
                return '?';
        }
        return c;
    }
    return -1;
}
#else
// POSIX / Linux / macOS Implementation
#include <getopt.h>
#include <sys/stat.h>
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

static void print_help(const char* app) {
    printf("Usage: %s [<options>] [<argument>]...\n\n", app);
    printf("Standard Modes:\n");
    printf("  -z, --compress    Compress FILE (default)\n");
    printf("  -d, --decompress  Decompress FILE (or stdin -> stdout)\n");
    printf("  -b, --bench       Benchmark in-memory\n\n");
    printf("Special Options:\n");
    printf("  --version         Show version information\n");
    printf("  --help            Show this help message\n\n");
    printf("Options:\n");
    printf("  -l, --level N     Compression level (1-9)\n");
    printf("  -t, --threads N   Number of threads (0=auto)\n");
    printf("  -C, --checksum    Enable checksum\n");
    printf("  -N, --no-checksum Disable checksum\n");
    printf("  -k, --keep        Keep input file\n");
    printf("  -f, --force       Force overwrite\n");
    printf("  -c, --stdout      Write to stdout\n");
    printf("  -v, --verbose     Verbose mode\n");
    printf("  -q, --quiet       Quiet mode\n");
}

static void print_version(void) {
    char sys_info[256];
#ifdef _WIN32
    snprintf(sys_info, sizeof(sys_info), "%s-%s", ZXC_ARCH, ZXC_OS);
#else
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        snprintf(sys_info, sizeof(sys_info), "%s-%s-%s", ZXC_ARCH, ZXC_OS, buffer.release);
    } else {
        snprintf(sys_info, sizeof(sys_info), "%s-%s", ZXC_ARCH, ZXC_OS);
    }
#endif
    printf("zxc %s (%s)\n", ZXC_LIB_VERSION_STR, sys_info);
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

    static const struct option long_options[] = {{"compress", no_argument, 0, 'z'},
                                                 {"decompress", no_argument, 0, 'd'},
                                                 {"bench", optional_argument, 0, 'b'},
                                                 {"level", required_argument, 0, 'l'},
                                                 {"threads", required_argument, 0, 't'},
                                                 {"keep", no_argument, 0, 'k'},
                                                 {"force", no_argument, 0, 'f'},
                                                 {"stdout", no_argument, 0, 'c'},
                                                 {"verbose", no_argument, 0, 'v'},
                                                 {"quiet", no_argument, 0, 'q'},
                                                 {"checksum", no_argument, 0, 'C'},
                                                 {"no-checksum", no_argument, 0, 'N'},
                                                 {"version", no_argument, 0, OPT_VERSION},
                                                 {"help", no_argument, 0, OPT_HELP},
                                                 {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "zdb::l:t:kfcvqCN", long_options, NULL)) != -1) {
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
            case 'l':
                level = atoi(optarg);
                break;
            case 't':
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
            case OPT_VERSION:
                print_version();
                return 0;
            case OPT_HELP:
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
        // Check for optional iterations argument
        if (optind + 1 < argc) {
            iterations = atoi(argv[optind + 1]);
        }
        struct stat st;
        if (stat(in_path, &st) != 0) return 1;
        size_t in_size = st.st_size;

        FILE* f_in = fopen(in_path, "rb");
        if (!f_in) return 1;
        uint8_t* ram = malloc(in_size);
        if (fread(ram, 1, in_size, f_in) != in_size) {
            fclose(f_in);
            free(ram);
            return 1;
        }
        fclose(f_in);

        printf("Input: %s (%zu bytes)\n", in_path, in_size);
        printf("Running %d iterations (Threads: %d)...\n", iterations, num_threads);

// Windows lacks fmemopen; fallback to temporary files for stream simulation
#ifdef _WIN32
        printf(
            "Note: fmemopen not available on Windows. Using tmpfile instead "
            "(slower).\n");
        FILE* fm = tmpfile();
        if (fm) {
            fwrite(ram, 1, in_size, fm);
        }
#else
        FILE* fm = fmemopen(ram, in_size, "rb");
#endif

        if (!fm) {
            free(ram);
            return 1;
        }

        double t0 = zxc_now();
        for (int i = 0; i < iterations; i++) {
            rewind(fm);
            zxc_stream_compress(fm, NULL, num_threads, level, checksum);
        }
        double dt_c = zxc_now() - t0;
        fclose(fm);

        size_t max_c = zxc_compress_bound(in_size);
        uint8_t* c_dat = malloc(max_c);

#ifdef _WIN32
        FILE* fm_in = tmpfile();
        fwrite(ram, 1, in_size, fm_in);
        rewind(fm_in);
        FILE* fm_out = tmpfile();
#else
        FILE* fm_in = fmemopen(ram, in_size, "rb");
        FILE* fm_out = fmemopen(c_dat, max_c, "wb");
#endif

        int c_sz = zxc_stream_compress(fm_in, fm_out, num_threads, level, checksum);

#ifdef _WIN32
        rewind(fm_out);
        // Read back from tmpfile to memory buffer for decompression bench
        fseek(fm_out, 0, SEEK_END);
        rewind(fm_out);
        fread(c_dat, 1, c_sz, fm_out);
#endif

        fclose(fm_in);
        fclose(fm_out);

#ifdef _WIN32
        FILE* fc = tmpfile();
        fwrite(c_dat, 1, c_sz, fc);
#else
        FILE* fc = fmemopen(c_dat, c_sz, "rb");
#endif

        t0 = zxc_now();
        for (int i = 0; i < iterations; i++) {
            rewind(fc);
            zxc_stream_decompress(fc, NULL, num_threads, checksum);
        }
        double dt_d = zxc_now() - t0;
        fclose(fc);

        printf("Compressed: %d bytes (ratio %.3f)\n", c_sz, ((double)in_size / c_sz));
        printf("Avg Compress  : %.3f MiB/s\n",
               ((double)in_size * iterations / (1024.0 * 1024.0)) / dt_c);
        printf("Avg Decompress: %.3f MiB/s\n",
               ((double)in_size * iterations / (1024.0 * 1024.0)) / dt_d);
        free(ram);
        free(c_dat);
        return 0;
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
        f_in = fopen(in_path, "rb");
        if (!f_in) {
            zxc_log("Error open input %s\n", in_path);
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

    // Open output file if not writing to stdout
    if (!use_stdout) {
        if (!force && access(out_path, F_OK) == 0) {
            zxc_log("Output exists. Use -f.\n");
            fclose(f_in);
            return 1;
        }
        f_out = fopen(out_path, "wb");
        if (!f_out) {
            zxc_log("Error open output %s\n", out_path);
            fclose(f_in);
            return 1;
        }
    }

    // Set large buffers for I/O performance
    char *b1 = malloc(1024 * 1024), *b2 = malloc(1024 * 1024);
    setvbuf(f_in, b1, _IOFBF, 1024 * 1024);
    setvbuf(f_out, b2, _IOFBF, 1024 * 1024);

    // Prevent writing binary data to the terminal unless forced
    if (use_stdout && isatty(fileno(stdout)) && mode == MODE_COMPRESS && !force) {
        zxc_log("Refusing to write binary to terminal.\n");
        fclose(f_in);
        return 1;
    }

    zxc_log_v("Starting... (Compression Level %d)\n", level);
    if (g_verbose) {
        zxc_log("Checksum: %s\n", checksum ? "enabled" : "disabled");
    }

    double t0 = zxc_now();
    int bytes = (mode == MODE_COMPRESS)
                    ? zxc_stream_compress(f_in, f_out, num_threads, level, checksum)
                    : zxc_stream_decompress(f_in, f_out, num_threads, checksum);
    double dt = zxc_now() - t0;

    if (!use_stdin) {
        fclose(f_in);
    } else {
        setvbuf(f_in, NULL, _IONBF, 0);
    }
    if (!use_stdout) {
        fclose(f_out);
    } else {
        setvbuf(f_out, NULL, _IONBF, 0);
    }
    free(b1);
    free(b2);

    if (bytes >= 0) {
        zxc_log_v("Processed %d bytes in %.3fs\n", bytes, dt);
        if (!use_stdin && !use_stdout && !keep_input) unlink(in_path);
    } else {
        zxc_log("Operation failed.\n");
        return 1;
    }
    return 0;
}