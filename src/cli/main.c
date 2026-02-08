/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
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
#include "../lib/zxc_internal.h"

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
        const size_t name_len = name_end ? (size_t)(name_end - (curr + 2)) : strlen(curr + 2);
        const struct option* p = longopts;
        while (p && p->name) {
            const size_t opt_len = strlen(p->name);
            if (name_len == opt_len && strncmp(curr + 2, p->name, name_len) == 0) {
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
        "  -l, --list        List archive information\n"
        "  -t, --test        Test compressed FILE integrity\n"
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
    printf("zxc v%s (%s) by Bertrand Lebonnois & al.\nBSD 3-Clause License\n", ZXC_LIB_VERSION_STR,
           sys_info);
}

typedef enum {
    MODE_COMPRESS,
    MODE_DECOMPRESS,
    MODE_BENCHMARK,
    MODE_INTEGRITY,
    MODE_LIST
} zxc_mode_t;

enum { OPT_VERSION = 1000, OPT_HELP };

/**
 * @brief Formats a byte size into human-readable TB/GB/MB/KB/B format (Base 1000).
 */
static void format_size_decimal(uint64_t bytes, char* buf, size_t buf_size) {
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

/**
 * @brief Progress context for CLI progress bar display.
 */
typedef struct {
    double start_time;
    const char* operation;  // "Compressing" or "Decompressing"
    uint64_t total_size;    // Pre-determined total size (0 if unknown)
} progress_ctx_t;

/**
 * @brief Progress callback for CLI progress bar.
 *
 * Displays a real-time progress bar during compression/decompression.
 * Shows percentage, processed/total size, and throughput speed.
 *
 * Format: [==========>     ] 45% | 4.5 GB/10.0 GB | 156 MB/s
 */
static void cli_progress_callback(uint64_t bytes_processed, uint64_t bytes_total,
                                  const void* user_data) {
    const progress_ctx_t* pctx = (const progress_ctx_t*)user_data;

    if (!pctx) return;

    // Use pre-determined total size from context (not the parameter)
    uint64_t total = pctx->total_size;

    double now = zxc_now();
    double elapsed = now - pctx->start_time;

    // Calculate throughput speed
    double speed_mbps = 0.0;
    if (elapsed > 0.1)  // Avoid division by zero for very fast operations
        speed_mbps = (double)bytes_processed / (1000.0 * 1000.0) / elapsed;

    // Clear line and move cursor to beginning
    fprintf(stderr, "\r\033[K");

    if (total > 0) {
        // Known size: show percentage bar
        int percent = (int)((bytes_processed * 100) / total);
        if (percent > 100) percent = 100;

        const int bar_width = 20;
        int filled = (percent * bar_width) / 100;

        fprintf(stderr, "%s [", pctx->operation);
        for (int i = 0; i < bar_width; i++) {
            if (i < filled)
                fprintf(stderr, "=");
            else if (i == filled)
                fprintf(stderr, ">");
            else
                fprintf(stderr, " ");
        }
        fprintf(stderr, "] %d%% | ", percent);

        char proc_str[32], total_str[32];
        format_size_decimal(bytes_processed, proc_str, sizeof(proc_str));
        format_size_decimal(total, total_str, sizeof(total_str));
        fprintf(stderr, "%s/%s | %.1f MB/s", proc_str, total_str, speed_mbps);
    } else {
        // Unknown size (stdin): just show bytes processed
        char proc_str[32];
        format_size_decimal(bytes_processed, proc_str, sizeof(proc_str));
        fprintf(stderr, "%s [Processing...] %s | %.1f MB/s", pctx->operation, proc_str, speed_mbps);
    }

    fflush(stderr);
}

/**
 * @brief Lists the contents of a ZXC archive.
 *
 * Reads the file header and footer to display:
 * - Compressed size
 * - Uncompressed size
 * - Compression ratio
 * - Checksum method
 * - Filename
 *
 * In verbose mode, displays additional header information.
 *
 * @param[in] path Path to the ZXC archive file.
 * @return 0 on success, 1 on error.
 */
static int zxc_list_archive(const char* path) {
    char resolved_path[4096];
    if (zxc_validate_input_path(path, resolved_path, sizeof(resolved_path)) != 0) {
        fprintf(stderr, "Error: Invalid input file '%s': %s\n", path, strerror(errno));
        return 1;
    }

    FILE* f = fopen(resolved_path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    // Get file size
    if (fseeko(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "Error: Cannot seek in file\n");
        return 1;
    }
    long long file_size = ftello(f);

    // Use public API to get decompressed size
    int64_t uncompressed_size = zxc_stream_get_decompressed_size(f);
    if (uncompressed_size < 0) {
        fclose(f);
        fprintf(stderr, "Error: Not a valid ZXC archive\n");
        return 1;
    }

    // Read header for format info (rewind after API call)
    uint8_t header[ZXC_FILE_HEADER_SIZE];
    if (fseeko(f, 0, SEEK_SET) != 0 ||
        fread(header, 1, ZXC_FILE_HEADER_SIZE, f) != ZXC_FILE_HEADER_SIZE) {
        fclose(f);
        fprintf(stderr, "Error: Cannot read file header\n");
        return 1;
    }

    // Extract header fields
    const uint8_t format_version = header[4];
    const size_t block_units = header[5] ? header[5] : 64;  // Default 64 units = 256KB

    // Read footer for checksum info
    uint8_t footer[ZXC_FILE_FOOTER_SIZE];
    if (fseeko(f, file_size - ZXC_FILE_FOOTER_SIZE, SEEK_SET) != 0 ||
        fread(footer, 1, ZXC_FILE_FOOTER_SIZE, f) != ZXC_FILE_FOOTER_SIZE) {
        fclose(f);
        fprintf(stderr, "Error: Cannot read file footer\n");
        return 1;
    }
    fclose(f);

    // Parse checksum (if non-zero, checksum was enabled)
    uint32_t stored_checksum = footer[8] | ((uint32_t)footer[9] << 8) |
                               ((uint32_t)footer[10] << 16) | ((uint32_t)footer[11] << 24);
    const char* checksum_method = (stored_checksum != 0) ? "RapidHash" : "-";

    // Calculate ratio (uncompressed / compressed, e.g., 2.5 means 2.5x compression)
    double ratio = (file_size > 0) ? ((double)uncompressed_size / (double)file_size) : 0.0;

    // Format sizes
    char comp_str[32], uncomp_str[32];
    format_size_decimal((uint64_t)file_size, comp_str, sizeof(comp_str));
    format_size_decimal((uint64_t)uncompressed_size, uncomp_str, sizeof(uncomp_str));

    if (g_verbose) {
        // Verbose mode: detailed vertical layout
        printf(
            "\nFile: %s\n"
            "-----------------------\n"
            "Block Format: %u\n"
            "Block Units:  %zu (x 4KB)\n"
            "Checksum Method: %s\n",
            path, format_version, block_units, (stored_checksum != 0) ? "RapidHash" : "None");

        if (stored_checksum != 0) printf("Checksum Value:  0x%08X\n", stored_checksum);

        printf(
            "-----------------------\n"
            "Comp. Size:   %s\n"
            "Uncomp. Size: %s\n"
            "Ratio:        %.2f\n",
            comp_str, uncomp_str, ratio);
    } else {
        // Normal mode: table format
        printf("\n  %12s   %12s   %5s   %-10s   %s\n", "Compressed", "Uncompressed", "Ratio",
               "Checksum", "Filename");
        printf("  %12s   %12s   %5.2f   %-10s   %s\n", comp_str, uncomp_str, ratio, checksum_method,
               path);
    }

    return 0;
}

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
    int checksum = -1;
    int level = 3;

    static const struct option long_options[] = {
        {"compress", no_argument, 0, 'z'},    {"decompress", no_argument, 0, 'd'},
        {"list", no_argument, 0, 'l'},        {"test", no_argument, 0, 't'},
        {"bench", optional_argument, 0, 'b'}, {"threads", required_argument, 0, 'T'},
        {"keep", no_argument, 0, 'k'},        {"force", no_argument, 0, 'f'},
        {"stdout", no_argument, 0, 'c'},      {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},       {"checksum", no_argument, 0, 'C'},
        {"no-checksum", no_argument, 0, 'N'}, {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "12345b::cCdfhklNqT:tvVz", long_options, NULL)) != -1) {
        switch (opt) {
            case 'z':
                mode = MODE_COMPRESS;
                break;
            case 'd':
                mode = MODE_DECOMPRESS;
                break;
            case 'l':
                mode = MODE_LIST;
                break;
            case 't':
                mode = MODE_INTEGRITY;
                break;
            case 'b':
                mode = MODE_BENCHMARK;
                if (optarg) {
                    iterations = atoi(optarg);
                    if (iterations < 1 || iterations > 10000) {
                        fprintf(stderr, "Error: iterations must be between 1 and 10000\n");
                        return 1;
                    }
                }
                break;
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
                if (opt >= '1' && opt <= '5') level = opt - '0';
                break;
            case 'T':
                num_threads = atoi(optarg);
                if (num_threads < 0 || num_threads > 1024) {
                    fprintf(stderr, "Error: num_threads must be between 0 and 1024\n");
                    return 1;
                }
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
        } else if (strcmp(argv[optind], "l") == 0 || strcmp(argv[optind], "list") == 0) {
            mode = MODE_LIST;
            optind++;
        } else if (strcmp(argv[optind], "t") == 0 || strcmp(argv[optind], "test") == 0) {
            mode = MODE_INTEGRITY;
            optind++;
        } else if (strcmp(argv[optind], "b") == 0) {
            mode = MODE_BENCHMARK;
            optind++;
        }
    }

    if (checksum == -1) {
        checksum = (mode == MODE_INTEGRITY) ? 1 : 0;
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
        if (optind + 1 < argc) {
            iterations = atoi(argv[optind + 1]);
            if (iterations < 1 || iterations > 10000) {
                zxc_log("Error: iterations must be between 1 and 10000\n");
                return 1;
            }
        }

        if (num_threads < 0 || num_threads > 1024) {
            zxc_log("Error: num_threads must be between 0 and 1024\n");
            return 1;
        }

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
        fclose(fm_in);
        fclose(fm_out);
#else
        fclose(fm_in);
        fclose(fm_out);
#endif

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
     * List Mode
     * Displays archive information (compressed size, uncompressed size, ratio).
     */
    if (mode == MODE_LIST) {
        if (optind >= argc) {
            zxc_log("List mode requires input file.\n");
            return 1;
        }
        int ret = 0;
        for (int i = optind; i < argc; i++) {
            ret |= zxc_list_archive(argv[i]);
        }
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
    char resolved_in_path[4096] = {0};
    char out_path[1024] = {0};
    int use_stdin = 1, use_stdout = 0;

    if (optind < argc && strcmp(argv[optind], "-") != 0) {
        in_path = argv[optind];

        if (zxc_validate_input_path(in_path, resolved_in_path, sizeof(resolved_in_path)) != 0) {
            zxc_log("Error: Invalid input file '%s': %s\n", in_path, strerror(errno));
            return 1;
        }

        f_in = fopen(resolved_in_path, "rb");
        if (!f_in) {
            zxc_log("Error open input %s: %s\n", resolved_in_path, strerror(errno));
            return 1;
        }
        use_stdin = 0;
        optind++;  // Move past input file
    } else {
        use_stdin = 1;
        use_stdout = 1;  // Default to stdout if reading from stdin
    }

    if (mode == MODE_INTEGRITY) {
        use_stdout = 0;
        f_out = NULL;
    } else if (!use_stdin && optind < argc) {
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
    if (mode != MODE_INTEGRITY && !use_stdin && !use_stdout && strcmp(in_path, out_path) == 0) {
        zxc_log("Error: Input and output filenames are identical.\n");
        if (f_in) fclose(f_in);
        return 1;
    }

    // Open output file if not writing to stdout
    if (!use_stdout && mode != MODE_INTEGRITY) {
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

    // Determine if we should show progress bar and get file size
    // IMPORTANT: This must be done BEFORE setting large buffers with setvbuf
    // to avoid buffer inconsistency issues when reading the footer
    int show_progress = 0;
    uint64_t total_size = 0;

    if (!g_quiet && !use_stdout && !use_stdin && isatty(fileno(stderr))) {
        // Get file size based on mode
        if (mode == MODE_COMPRESS) {
            // Compression: get input file size
            long long saved_pos = ftello(f_in);
            if (saved_pos >= 0) {
                if (fseeko(f_in, 0, SEEK_END) == 0) {
                    long long size = ftello(f_in);
                    if (size > 0) total_size = (uint64_t)size;
                    fseeko(f_in, saved_pos, SEEK_SET);
                }
            }
        } else {
            // Decompression: get decompressed size from footer (BEFORE starting decompression)
            int64_t decomp_size = zxc_stream_get_decompressed_size(f_in);
            if (decomp_size > 0) {
                total_size = (uint64_t)decomp_size;
            }
        }

        // Only show progress for files > 1MB
        if (total_size > 1024 * 1024) {
            show_progress = 1;
        }
    }

    // Set large buffers for I/O performance (AFTER file size detection)
    char *b1 = malloc(1024 * 1024), *b2 = malloc(1024 * 1024);
    setvbuf(f_in, b1, _IOFBF, 1024 * 1024);
    if (f_out) setvbuf(f_out, b2, _IOFBF, 1024 * 1024);

    zxc_log_v("Starting... (Compression Level %d)\n", level);
    if (g_verbose) zxc_log("Checksum: %s\n", checksum ? "enabled" : "disabled");

    // Prepare progress context
    progress_ctx_t pctx = {.start_time = zxc_now(),
                           .operation = (mode == MODE_COMPRESS) ? "Compressing" : "Decompressing",
                           .total_size = total_size};

    double t0 = zxc_now();
    int64_t bytes;
    if (mode == MODE_COMPRESS) {
        bytes = zxc_stream_compress_ex(f_in, f_out, num_threads, level, checksum,
                                       show_progress ? cli_progress_callback : NULL, &pctx);
    } else {
        bytes = zxc_stream_decompress_ex(f_in, f_out, num_threads, checksum,
                                         show_progress ? cli_progress_callback : NULL, &pctx);
    }
    double dt = zxc_now() - t0;

    // Clear progress line on completion
    if (show_progress) {
        fprintf(stderr, "\r\033[K");
        fflush(stderr);
    }

    if (!use_stdin)
        fclose(f_in);
    else
        setvbuf(stdin, NULL, _IONBF, 0);

    if (!use_stdout) {
        if (f_out) fclose(f_out);
    } else {
        fflush(f_out);
        setvbuf(stdout, NULL, _IONBF, 0);
    }

    free(b1);
    free(b2);

    if (bytes >= 0) {
        if (mode == MODE_INTEGRITY) {
            // Test mode: show result
            if (g_verbose) {
                printf(
                    "%s: OK\n"
                    "  Checksum:     %s\n"
                    "  Time:         %.3fs\n",
                    in_path ? in_path : "<stdin>",
                    checksum ? "verified (RapidHash)" : "not verified", dt);
            } else {
                printf("%s: OK\n", in_path ? in_path : "<stdin>");
            }
        } else {
            zxc_log_v("Processed %lld bytes in %.3fs\n", (long long)bytes, dt);
        }
        if (!use_stdin && !use_stdout && !keep_input && mode != MODE_INTEGRITY)
            unlink(resolved_in_path);
    } else {
        if (mode == MODE_INTEGRITY) {
            fprintf(stderr, "%s: FAILED\n", in_path ? in_path : "<stdin>");
            if (g_verbose) {
                fprintf(stderr,
                        "  Reason: Integrity check failed (corrupted data or invalid checksum)\n");
            }
        } else {
            zxc_log("Operation failed.\n");
        }
        return 1;
    }
    return 0;
}
