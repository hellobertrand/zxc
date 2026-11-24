// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file main.c
 * @brief Command Line Interface (CLI) entry point for the ZXC compression tool.
 *
 * This file handles argument parsing, the directory walk, and the execution
 * of compression, decompression, or benchmarking modes. Diagnostics are in
 * log.c, file handling in fileio.c and listing in list.c.
 */

#include "cli.h"

#define ZXC_STDIO_BUFFER_SIZE (1024 * 1024)

// The archive extension, appended on compression and stripped on decompression.
#define ZXC_ARCHIVE_EXT ".zxc"
#define ZXC_ARCHIVE_EXT_LEN (sizeof(ZXC_ARCHIVE_EXT) - 1)

#ifdef _WIN32
#define ZXC_PATH_SEP '\\'
#else
#define ZXC_PATH_SEP '/'
#endif

/** @brief Target architecture shown by -V. */
#if defined(_M_ARM64EC) || defined(__arm64ec__)  // also defines x86_64 macros: test first
#define ZXC_CLI_ARCH "arm64ec"
#elif defined(__x86_64__) && defined(__ILP32__)
#define ZXC_CLI_ARCH "x32"
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define ZXC_CLI_ARCH "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
#define ZXC_CLI_ARCH "x86"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define ZXC_CLI_ARCH "arm64"
#elif defined(__arm__) || defined(_M_ARM)
#define ZXC_CLI_ARCH "arm"
#elif defined(__riscv)
#if __riscv_xlen == 64
#define ZXC_CLI_ARCH "riscv64"
#else
#define ZXC_CLI_ARCH "riscv32"
#endif
#elif defined(__powerpc64__) || defined(__ppc64__)
#define ZXC_CLI_ARCH "ppc64"
#elif defined(__powerpc__) || defined(__ppc__)
#define ZXC_CLI_ARCH "ppc"
#elif defined(__s390x__)
#define ZXC_CLI_ARCH "s390x"
#elif defined(__loongarch64)
#define ZXC_CLI_ARCH "loongarch64"
#elif defined(__loongarch__)
#define ZXC_CLI_ARCH "loongarch32"
#elif defined(__mips64)
#define ZXC_CLI_ARCH "mips64"
#elif defined(__mips__)
#define ZXC_CLI_ARCH "mips"
#elif defined(__sparc__) && defined(__arch64__)
#define ZXC_CLI_ARCH "sparc64"
#elif defined(__sparc__)
#define ZXC_CLI_ARCH "sparc"
#elif defined(__alpha__)
#define ZXC_CLI_ARCH "alpha"
#elif defined(__hppa__)
#define ZXC_CLI_ARCH "hppa"
#elif defined(__ia64__)
#define ZXC_CLI_ARCH "ia64"
#elif defined(__m68k__)
#define ZXC_CLI_ARCH "m68k"
#elif defined(__sh__)
#define ZXC_CLI_ARCH "sh"
#elif defined(__wasm32__)
#define ZXC_CLI_ARCH "wasm32"
#elif defined(__wasm64__)
#define ZXC_CLI_ARCH "wasm64"
#else
#define ZXC_CLI_ARCH "unknown"
#endif

/** @brief Byte order shown by -V. */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define ZXC_CLI_ENDIAN "LE"
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ZXC_CLI_ENDIAN "BE"
#elif defined(__LITTLE_ENDIAN__) || defined(_WIN32)
#define ZXC_CLI_ENDIAN "LE"
#elif defined(__BIG_ENDIAN__)
#define ZXC_CLI_ENDIAN "BE"
#else
#define ZXC_CLI_ENDIAN "unknown"
#endif

#ifdef _WIN32
// Windows Implementation

#ifdef __MINGW32__
int _dowildcard = -1;  // MinGW-w64 globs argv, as setargv.obj does for MSVC
#endif

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
int opterr = 1;
#define ZXC_GETOPT_REWIND 1

/**
 * @brief Minimal implementation of getopt_long for Windows.
 * Handles long options (--option[=value]), grouped short options (-dcf),
 * attached or separate option arguments (-T4 / -T 4), the "--" end-of-options
 * marker, and a bare "-" left as a positional argument.
 */
static int getopt_long(int argc, char* const argv[], const char* optstring,
                       const struct option* longopts, const int* longindex) {
    (void)longindex;
    static int shortpos = 0;  // >0: position within a grouped short-option argv element

    optarg = NULL;
    if (optind >= argc) return -1;
    char* const curr = argv[optind];

    if (shortpos == 0) {
        if (curr[0] != '-' || curr[1] == '\0') return -1;  // positional (including "-")
        if (curr[1] == '-') {
            if (curr[2] == '\0') {  // "--": end of options
                optind++;
                return -1;
            }
            char* const name_end = strchr(curr + 2, '=');
            const size_t name_len = name_end ? (size_t)(name_end - (curr + 2)) : strlen(curr + 2);
            for (const struct option* p = longopts; p && p->name; p++) {
                if (name_len != strlen(p->name) || strncmp(curr + 2, p->name, name_len) != 0)
                    continue;
                optind++;
                if (p->has_arg == required_argument) {
                    if (name_end)
                        optarg = name_end + 1;
                    else if (optind < argc)
                        optarg = argv[optind++];
                    else
                        return '?';
                } else if (p->has_arg == optional_argument && name_end) {
                    optarg = name_end + 1;
                } else if (name_end) {  // "--rm=false" is an error, not --rm
                    if (opterr)
                        fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", argv[0],
                                p->name);
                    return '?';
                }
                if (p->flag) {
                    *p->flag = p->val;
                    return 0;
                }
                return p->val;
            }
            optind++;
            return '?';
        }
        shortpos = 1;
    }

    const char c = curr[shortpos];
    const char* const os = (c != ':') ? strchr(optstring, c) : NULL;
    if (!os) {
        optopt = c;
        if (curr[shortpos + 1] != '\0') {
            shortpos++;
        } else {
            shortpos = 0;
            optind++;
        }
        return '?';
    }
    if (os[1] == ':') {
        // Option takes an argument: the rest of this element, or (if required)
        // the next argv element. Optional (::) never consumes a separate element.
        char* const attached = (curr[shortpos + 1] != '\0') ? curr + shortpos + 1 : NULL;
        shortpos = 0;
        optind++;
        if (attached) {
            optarg = attached;
        } else if (os[2] != ':') {
            if (optind < argc) {
                optarg = argv[optind++];
            } else {
                optopt = c;
                return '?';
            }
        }
        return c;
    }
    // Flag option: continue within the cluster if more characters follow
    if (curr[shortpos + 1] != '\0') {
        shortpos++;
    } else {
        shortpos = 0;
        optind++;
    }
    return c;
}
#else
// POSIX / Linux / macOS Implementation
#include <getopt.h>
// optind = 0 restarts getopt_long in glibc, musl and the BSD implementations
#define ZXC_GETOPT_REWIND 0

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

/* Progress display policy (--progress): auto = tty-only heuristic, always =
 * force (one line per update off-tty), never = disable. -q suppresses all. */
enum { ZXC_PROGRESS_AUTO = 0, ZXC_PROGRESS_ALWAYS, ZXC_PROGRESS_NEVER };
static int g_progress_mode = ZXC_PROGRESS_AUTO;
/* Shared literal Huffman table from the -D .zxd file (malloc'd copy), set on
 * the contexts with the dictionary. NULL when the dict carries none. */
static void* g_dict_huf = NULL;

/* Builds a compression context from the command line. ZXC_OK and *out set, or
 * the refusing setter's error with *out NULL. */
static int cli_cctx(zxc_cctx** out, int threads, int level, size_t block_size, int checksum,
                    int seekable, int decode_speed_budget, const void* dict, size_t dict_size,
                    const void* dict_huf, zxc_progress_callback_t cb, void* user_data) {
    *out = NULL;
    zxc_cctx* const c = zxc_create_cctx();
    if (!c) return ZXC_ERROR_MEMORY;
    int rc = zxc_cctx_set_param(c, ZXC_C_THREADS, threads);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_LEVEL, level);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_BLOCK_SIZE, (int64_t)block_size);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_CHECKSUM, checksum != 0);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_SEEKABLE, seekable != 0);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_FAVOR_DECODE_SPEED, decode_speed_budget);
    if (rc == ZXC_OK) rc = zxc_cctx_set_dict(c, dict, dict_size, dict_huf);
    if (rc == ZXC_OK) rc = zxc_cctx_set_progress(c, cb, user_data);
    if (rc != ZXC_OK) {
        zxc_free_cctx(c);
        return rc;
    }
    *out = c;
    return ZXC_OK;
}

/* Decompression twin of cli_cctx(). */
static int cli_dctx(zxc_dctx** out, int threads, int checksum, const void* dict, size_t dict_size,
                    const void* dict_huf, zxc_progress_callback_t cb, void* user_data) {
    *out = NULL;
    zxc_dctx* const d = zxc_create_dctx();
    if (!d) return ZXC_ERROR_MEMORY;
    int rc = zxc_dctx_set_param(d, ZXC_D_THREADS, threads);
    if (rc == ZXC_OK) rc = zxc_dctx_set_param(d, ZXC_D_CHECKSUM, checksum != 0);
    if (rc == ZXC_OK) rc = zxc_dctx_set_dict(d, dict, dict_size, dict_huf);
    if (rc == ZXC_OK) rc = zxc_dctx_set_progress(d, cb, user_data);
    if (rc != ZXC_OK) {
        zxc_free_dctx(d);
        return rc;
    }
    *out = d;
    return ZXC_OK;
}
// --no-preserve clears it
static int g_preserve = 1;
// -r: entries go by name in the directory walked
static int g_walking = 0;
// --favor-decode-speed[=PCT]: ZXC_C_FAVOR_DECODE_SPEED, the budget being PCT or
// ZXC_FAVOR_DECODE_SPEED_DEFAULT
static int g_favor_decode_speed = 0;
static int g_split_budget_pct = 0;

// argv in the order typed; getopt_long reorders the live one. NULL: not kept.
static char** g_args_given = NULL;

// The next argument, consumed as an optional option's value when it is all
// digits: "-b 5" or "--favor-decode-speed 25", while "5samples.bin" stays a file.
static const char* cli_number_arg(const int argc, char** argv) {
    if (optind < 1 || optind >= argc || argv[optind][0] < '0' || argv[optind][0] > '9') return NULL;
    // musl has by now moved the option ahead of the files before it: "zxc 25 -b"
    // must not take the file 25, which was typed before -b, for its value
    if (g_args_given != NULL) {
        int i = 1;
        while (i < argc && g_args_given[i] != argv[optind - 1]) i++;
        if (i + 1 >= argc || g_args_given[i + 1] != argv[optind]) return NULL;
    }
    const char* p = argv[optind] + 1;
    while (*p >= '0' && *p <= '9') p++;
    return *p == '\0' ? argv[optind++] : NULL;
}

typedef enum {
    MODE_COMPRESS,
    MODE_DECOMPRESS,
    MODE_BENCHMARK,
    MODE_INTEGRITY,
    MODE_LIST,
    MODE_TRAIN_DICT
} zxc_mode_t;

enum {
    OPT_VERSION = 1000,
    OPT_HELP,
    OPT_TRAIN_DICT,
    OPT_PROGRESS,
    OPT_NO_PRESERVE,
    OPT_RM,
    OPT_FAVOR_DECODE_SPEED,
    OPT_COLOR
};

// Whether path ends with the archive extension.
static int zxc_has_archive_ext(const char* path) {
    const size_t len = strlen(path);
    return len >= ZXC_ARCHIVE_EXT_LEN &&
           strcmp(path + len - ZXC_ARCHIVE_EXT_LEN, ZXC_ARCHIVE_EXT) == 0;
}

// dir + separator + name in a malloc'd string, so no fixed limit; NULL if out of memory.
static char* zxc_path_join(const char* dir, const char* name) {
    const size_t dir_len = strlen(dir);
    const size_t name_len = strlen(name);
    char* const path = malloc(dir_len + 1 + name_len + 1);
    if (!path) return NULL;
    memcpy(path, dir, dir_len);
    path[dir_len] = ZXC_PATH_SEP;
    memcpy(path + dir_len + 1, name, name_len + 1);
    return path;
}

// Whether f starts with the magic word, position kept. A file checks all 4 bytes; stdin
// only the first, 0xF5 (ungetc() pushes back one), which starts no UTF-8 text.
static int zxc_starts_like_archive(FILE* f, const int seekable) {
    const long long pos = seekable ? ftello(f) : -1;
    if (pos >= 0) {
        uint8_t m[4];
        const size_t n = fread(m, 1, sizeof(m), f);
        if (fseeko(f, pos, SEEK_SET) != 0) return 1;  // let the decoder report it
        return n == sizeof(m) && ((uint32_t)m[0] | (uint32_t)m[1] << 8 | (uint32_t)m[2] << 16 |
                                  (uint32_t)m[3] << 24) == ZXC_MAGIC_WORD;
    }
    const int c = getc(f);
    if (c == EOF) return 0;
    ungetc(c, f);
    return c == (int)(ZXC_MAGIC_WORD & 0xFFU);
}

// -dcf on a non-archive: copy as is, in buffer-sized chunks that skip stdio's copy.
static int64_t zxc_copy_stream(FILE* in, FILE* out) {
    char* const buf = malloc(ZXC_STDIO_BUFFER_SIZE);
    if (!buf) return ZXC_ERROR_MEMORY;
    int64_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, ZXC_STDIO_BUFFER_SIZE, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            free(buf);
            return ZXC_ERROR_IO;
        }
        total += (int64_t)n;
    }
    free(buf);
    return ferror(in) ? ZXC_ERROR_IO : total;
}

// Forward declaration for recursive mode
static int process_single_file(const char* in_path, const char* out_path_override, zxc_mode_t mode,
                               int num_threads, int keep_input, int force, int to_stdout,
                               int checksum, int level, size_t block_size, int json_output,
                               int seekable, const void* dict, size_t dict_size);

// What a killed process left while replacing an output.
static int zxc_is_temp_output(const char* path) {
    const char* const base = zxc_base_name(path);
    const size_t len = strlen(base);
    const size_t sfx = strlen(ZXC_TEMP_SUFFIX);
    return strncmp(base, ZXC_TEMP_PREFIX, strlen(ZXC_TEMP_PREFIX)) == 0 && len >= sfx &&
           strcmp(base + len - sfx, ZXC_TEMP_SUFFIX) == 0;
}

// Compression skips archives; decompression and -t take archives only, since a
// tree keeps its sources.
static int zxc_walk_takes(zxc_mode_t mode, const char* path) {
    if (zxc_is_temp_output(path)) return 0;
    const int archive = zxc_has_archive_ext(path);
    if (mode == MODE_COMPRESS) return !archive;
    if (mode == MODE_DECOMPRESS || mode == MODE_INTEGRITY) return archive;
    return 1;
}

// Forward declaration for processing directory
static int process_directory(const char* dir_path, zxc_mode_t mode, int num_threads, int keep_input,
                             int force, int to_stdout, int checksum, int level, size_t block_size,
                             int json_output, int seekable, const void* dict, size_t dict_size);

// OS-specific implementation of directory processing
static int process_directory(const char* dir_path, zxc_mode_t mode, int num_threads, int keep_input,
                             int force, int to_stdout, int checksum, int level, size_t block_size,
                             int json_output, int seekable, const void* dict, size_t dict_size) {
    int overall_ret = 0;
#ifdef _WIN32
    char* const search_path = zxc_path_join(dir_path, "*");
    if (!search_path) {
        zxc_log("Error: cannot allocate memory for path in directory '%s'\n", dir_path);
        return 1;
    }

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    free(search_path);

    if (hFind == INVALID_HANDLE_VALUE) {
        zxc_log("Error: cannot open directory '%s'\n", dir_path);
        return 1;
    }

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        char* const full_path = zxc_path_join(dir_path, find_data.cFileName);
        if (!full_path) {
            zxc_log("Error: cannot allocate memory for path in directory '%s'\n", dir_path);
            overall_ret = 1;
            continue;
        }

        // Links stay out of the walk; other reparse points
        // (OneDrive placeholders) are files.
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            (find_data.dwReserved0 == IO_REPARSE_TAG_SYMLINK ||
             find_data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT)) {
            zxc_log("Warning: '%s' is a link, ignored\n", full_path);
        } else if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            overall_ret |= process_directory(full_path, mode, num_threads, keep_input, force,
                                             to_stdout, checksum, level, block_size, json_output,
                                             seekable, dict, dict_size);
        } else if (zxc_walk_takes(mode, full_path)) {
            overall_ret |= process_single_file(full_path, NULL, mode, num_threads, keep_input,
                                               force, to_stdout, checksum, level, block_size,
                                               json_output, seekable, dict, dict_size);
        }
        free(full_path);
    } while (FindNextFileA(hFind, &find_data) != 0);
    const DWORD walk_err = GetLastError();
    if (walk_err != ERROR_NO_MORE_FILES) {
        zxc_log("Error: cannot read directory '%s' (error %lu)\n", dir_path,
                (unsigned long)walk_err);
        overall_ret = 1;
    }

    FindClose(hFind);
#else
    const int nested = g_walking;
    const int back = open(".", O_RDONLY | O_DIRECTORY);
    const int fd =
        open(nested ? zxc_base_name(dir_path) : dir_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    DIR* const dir = (fd != -1) ? fdopendir(fd) : NULL;
    if (back == -1 || !dir || fchdir(fd) != 0) {
        zxc_log("Error: cannot open directory '%s': %s\n", dir_path, strerror(errno));
        if (dir)
            closedir(dir);
        else if (fd != -1)
            close(fd);
        if (back != -1) close(back);
        return 1;
    }
    g_walking = 1;

    for (;;) {
        errno = 0;  // readdir signals an error only through errno
        const struct dirent* const entry = readdir(dir);
        if (!entry) {
            if (errno != 0) {
                zxc_log("Error: cannot read directory '%s': %s\n", dir_path, strerror(errno));
                overall_ret = 1;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char* const full_path = zxc_path_join(dir_path, entry->d_name);
        if (!full_path) {
            zxc_log("Error: cannot allocate memory for path in directory '%s'\n", dir_path);
            overall_ret = 1;
            continue;
        }

        // lstat: links stay out of the walk.
        struct stat st;
        if (lstat(entry->d_name, &st) == 0) {
            if (S_ISLNK(st.st_mode)) {
                zxc_log("Warning: '%s' is a link, ignored\n", full_path);
            } else if (S_ISDIR(st.st_mode)) {
                overall_ret |= process_directory(full_path, mode, num_threads, keep_input, force,
                                                 to_stdout, checksum, level, block_size,
                                                 json_output, seekable, dict, dict_size);
            } else if (S_ISREG(st.st_mode) && zxc_walk_takes(mode, full_path)) {
                overall_ret |= process_single_file(full_path, NULL, mode, num_threads, keep_input,
                                                   force, to_stdout, checksum, level, block_size,
                                                   json_output, seekable, dict, dict_size);
            }
        } else if (errno != ENOENT) {  // ENOENT: removed during the walk
            zxc_log("Error: cannot access '%s': %s\n", full_path, strerror(errno));
            overall_ret = 1;
        }
        free(full_path);
    }
    closedir(dir);
    g_walking = nested;
    if (fchdir(back) != 0) {
        zxc_log("Error: cannot leave directory '%s': %s\n", dir_path, strerror(errno));
        exit(1);
    }
    close(back);
#endif
    return overall_ret;
}

static void print_help(const char* app) {
    printf("Usage: %s [<options>] [<argument>]...\n\n", app);
    printf(
        "Standard Modes:\n"
        "  -z, --compress    Compress FILE {default}\n"
        "  -d, --decompress  Decompress FILE (or stdin -> stdout)\n"
        "  -l, --list        List archive or dictionary info\n"
        "  -t, --test        Test compressed FILE integrity\n"
        "  -b, --bench [N]   Benchmark in-memory (N=seconds, default 5)\n"
        "  --train           Train a dictionary from input FILEs (training samples).\n"
        "                    Output via -o (default: ./dictionary_<dict_id>.zxd)\n\n"
        "Batch Processing:\n"
        "  -m, --multiple    Multiple input files\n"
        "  -r, --recursive   Operate recursively on directories\n\n"
        "Special Options:\n"
        "  -V, --version     Show version information\n"
        "  -h, --help        Show this help message\n\n"
        "Options:\n"
        "  -1..-7            Compression level {3}\n"
        "  -B, --block-size  Block size: 4K..2M, power of 2 {512K}\n"
        "  -T, --threads N   Number of worker threads (0=auto)\n"
        "  -C, --checksum    Enable checksum {default}\n"
        "  -N, --no-checksum Disable checksum\n"
        "  -D, --dict FILE   Use pre-trained dictionary (.zxd). Required to decompress\n"
        "                    an archive that was compressed with a dictionary\n"
        "  -S, --seekable    Append seek table for random-access decompression\n"
        "  --favor-decode-speed [PCT]\n"
        "                    Favor decode speed over size (levels 3-7); PCT = how much\n"
        "                    a block may grow for it, 1-100 {25}\n"
        "  -o, --output FILE Write output to FILE (else derived from input;\n"
        "                    for --train: ./dictionary_<dict_id>.zxd, or a directory)\n"
        "  -k, --keep        Keep input file {default}\n"
        "  --rm              Remove input file once its output is written; ignored\n"
        "                    with -c, -o, an OUTPUT-FILE, a device, -t or -l\n"
        "  --no-preserve     Do not copy the input's mode, times and group (owner as\n"
        "                    root) to the output\n"
        "  -f, --force       Force overwrite; when decompressing to stdout, copy\n"
        "                    non-ZXC input as is\n"
        "  -c, --stdout      Write to stdout\n"
        "  -v, --verbose     Verbose mode\n"
        "  -q, --quiet       Quiet mode\n"
        "  -j, --json        JSON output (benchmark mode)\n"
        "  --progress MODE   Progress display: auto, always, never {auto}\n"
        "  --color MODE      Color errors, warnings and -t results: auto, always,\n"
        "                    never {auto} (auto: terminal only; NO_COLOR disables it)\n");
}

static void print_version(void) {
    printf("ZXC CLI v%s \"%s\" (build " ZXC_CLI_ARCH " %zu-bit " ZXC_CLI_ENDIAN
           "), by Bertrand Lebonnois\n",
           zxc_version_string(), zxc_version_codename(), sizeof(void*) * CHAR_BIT);
}

/**
 * @brief Progress context for CLI progress bar display.
 */
typedef struct {
    double start_time;
    const char* operation;  // "Compressing", "Decompressing" or "Testing"
    uint64_t total_size;    // Pre-determined total size (0 if unknown)
    int to_tty;             // stderr is a terminal: rewrite in place; else one line per update
    double last_draw;       // Timestamp of the last repaint (0 = nothing drawn yet)
    size_t last_len;        // Visible length of the last in-place line (for erasing)
} progress_ctx_t;

/**
 * @brief Erases an in-place progress line without ANSI escapes.
 *
 * Plain "\r" + spaces works on every terminal, including legacy Windows
 * consoles where "\033[K" prints garbage unless VT processing is enabled.
 */
static void zxc_progress_clear(size_t len) {
    fprintf(stderr, "\r%*s\r", (int)len, "");
    fflush(stderr);
}

/**
 * @brief Benchmark progress line, throttled like the file progress bar:
 *        rewritten in place every 100 ms on a tty, else one line per second
 *        (only reached with --progress always).
 *
 * @p last holds the elapsed time of the last line, negative before the first.
 */
static void zxc_bench_progress(const char* op, int iters, double elapsed, int to_tty,
                               double* last) {
    if (*last >= 0.0 && elapsed - *last < (to_tty ? 0.1 : 1.0)) return;
    *last = elapsed;
    fprintf(stderr, to_tty ? "\r%s... %d iters (%.1fs)" : "%s... %d iters (%.1fs)\n", op, iters,
            elapsed);
}

/**
 * @brief Progress callback for CLI progress bar.
 *
 * The library fires this once per block -- at multi-GB/s rates that is far
 * more often than a terminal can display, so frames are throttled (100 ms on
 * a tty, 1 s otherwise) and each frame is emitted as a single write (stderr
 * is unbuffered: per-character output would be one syscall per character).
 *
 * Format: Compressing [=====>     ] 45% | 4.5 GB/10.0 GB | 156.0 MB/s | ETA 0:35
 */
static void cli_progress_callback(uint64_t bytes_processed, uint64_t bytes_total,
                                  const void* user_data) {
    (void)bytes_total; /* required by zxc_progress_callback_t */
    progress_ctx_t* const pctx = (progress_ctx_t*)(uintptr_t)user_data;

    if (!pctx) return;

    const double now = zxc_now();
    const double interval = pctx->to_tty ? 0.1 : 1.0;
    if (pctx->last_draw != 0.0 && now - pctx->last_draw < interval) return;
    pctx->last_draw = now;

    // Use pre-determined total size from context (not the parameter)
    const uint64_t total = pctx->total_size;
    const double elapsed = now - pctx->start_time;

    // Cumulative throughput
    double speed_mbps = 0.0;
    if (elapsed > 0.1)  // Avoid division by zero for very fast operations
        speed_mbps = (double)bytes_processed / (1000.0 * 1000.0) / elapsed;

    char proc_str[32];
    format_size_decimal(bytes_processed, proc_str, sizeof(proc_str));

    char text[160];
    int n;
    if (total > 0) {
        // Known size: percentage bar
        int percent = (int)((bytes_processed * 100) / total);
        if (percent > 100) percent = 100;

        enum { BAR_WIDTH = 20 };
        char bar[BAR_WIDTH + 1];
        const int filled = (percent * BAR_WIDTH) / 100;
        for (int i = 0; i < BAR_WIDTH; i++) {
            if (i < filled)
                bar[i] = '=';
            else if (i == filled)
                bar[i] = '>';
            else
                bar[i] = ' ';
        }
        bar[BAR_WIDTH] = '\0';

        // Estimated time to completion, from the cumulative throughput
        char eta[40] = "";
        if (speed_mbps > 0.0 && total > bytes_processed) {
            const long secs = (long)((double)(total - bytes_processed) / (speed_mbps * 1e6));
            if (secs >= 3600)
                snprintf(eta, sizeof(eta), " | ETA %ld:%02ld:%02ld", secs / 3600, (secs / 60) % 60,
                         secs % 60);
            else
                snprintf(eta, sizeof(eta), " | ETA %ld:%02ld", secs / 60, secs % 60);
        }

        char total_str[32];
        format_size_decimal(total, total_str, sizeof(total_str));
        n = snprintf(text, sizeof(text), "%s [%s] %d%% | %s/%s | %.1f MB/s%s", pctx->operation, bar,
                     percent, proc_str, total_str, speed_mbps, eta);
    } else {
        // Unknown size (stdin): just show bytes processed
        n = snprintf(text, sizeof(text), "%s %s | %.1f MB/s", pctx->operation, proc_str,
                     speed_mbps);
    }
    if (n < 0) return;
    const size_t tlen = ((size_t)n < sizeof(text)) ? (size_t)n : sizeof(text) - 1;

    char frame[352];
    size_t flen = 0;
    if (pctx->to_tty) {
        frame[flen++] = '\r';
        memcpy(frame + flen, text, tlen);
        flen += tlen;
        // Pad with spaces to erase any residue from a longer previous line
        size_t visible = tlen;
        while (visible < pctx->last_len && flen < sizeof(frame)) {
            frame[flen++] = ' ';
            visible++;
        }
        pctx->last_len = tlen;
    } else {
        // Off-tty (--progress=always): plain newline-terminated updates
        memcpy(frame, text, tlen);
        flen = tlen;
        frame[flen++] = '\n';
    }
    fwrite(frame, 1, flen, stderr);
    fflush(stderr);
}

// -t -j over several files prints one array, as -l -j does.
static int g_test_json_array = 0;
static int g_test_json_count = 0;

static void zxc_test_json_separator(void) {
    if (g_test_json_array && g_test_json_count++) printf(",\n");
}

static void zxc_test_json_cannot_open(const char* path) {
    zxc_test_json_separator();
    printf("{\n  \"filename\": ");
    zxc_json_string(path);
    printf(",\n  \"status\": \"failed\",\n  \"error\": \"cannot open file\"\n}\n");
}

static int process_single_file(const char* in_path, const char* out_path_override, zxc_mode_t mode,
                               int num_threads, int keep_input, int force, int to_stdout,
                               int checksum_enabled, int level, size_t block_size, int json_output,
                               int seekable, const void* dict, size_t dict_size) {
    FILE* f_in = stdin;
    FILE* f_out = stdout;
    char resolved_in_path[4096] = {0};
    char out_path[4096] = {0};
    char resolved_out_path[4096] = {0};
    int use_stdin = 1;
    int use_stdout = 0;
    int created_out_file = 0;
    int out_special = 0;  // output.special, set only once an output is open
    zxc_output_t output;
    int overall_ret = 0;
    zxc_file_attrs_t in_attrs = {0};

    if (in_path && strcmp(in_path, "-") != 0) {
        if (g_walking) {
            snprintf(resolved_in_path, sizeof(resolved_in_path), "%s", zxc_base_name(in_path));
        } else if (zxc_validate_input_path(in_path, resolved_in_path, sizeof(resolved_in_path)) !=
                   0) {
            zxc_log("Error: Invalid input file '%s': %s\n", in_path, strerror(errno));
            if (mode == MODE_INTEGRITY && json_output) zxc_test_json_cannot_open(in_path);
            return 1;
        }

        f_in = zxc_open_input(resolved_in_path);
        if (!f_in) {
            zxc_log("Error: cannot open input %s: %s\n", in_path, strerror(errno));
            if (mode == MODE_INTEGRITY && json_output) zxc_test_json_cannot_open(in_path);
            return 1;
        }
        use_stdin = 0;
    } else {
        use_stdin = 1;
        use_stdout = 1;  // Default to stdout if reading from stdin
        in_path = NULL;
    }

    if (mode == MODE_INTEGRITY) {
        use_stdout = 0;
        f_out = NULL;
    } else if (to_stdout) {
        use_stdout = 1;
    } else if (out_path_override) {
        // Explicit -o / positional output: honored for file and stdin input alike
        const int n = snprintf(out_path, sizeof(out_path), "%s", out_path_override);
        if (n < 0 || (size_t)n >= sizeof(out_path)) {
            zxc_log("Error: Output path too long\n");
            if (!use_stdin) fclose(f_in);
            return 1;
        }
        use_stdout = 0;
    } else if (!use_stdin) {
        // Auto-generate output filename from the input filename
        if (mode == MODE_COMPRESS) {
            const int n = snprintf(out_path, sizeof(out_path), "%s" ZXC_ARCHIVE_EXT, in_path);
            if (n < 0 || (size_t)n >= sizeof(out_path)) {
                zxc_log("Error: Output path too long\n");
                fclose(f_in);
                return 1;
            }
        } else {
            const size_t len = strlen(in_path);
            // A bare extension has no base name to write to.
            if (len > ZXC_ARCHIVE_EXT_LEN && zxc_has_archive_ext(in_path)) {
                const size_t base_len = len - ZXC_ARCHIVE_EXT_LEN;
                if (base_len >= sizeof(out_path)) {
                    zxc_log("Error: Output path too long\n");
                    fclose(f_in);
                    return 1;
                }
                memcpy(out_path, in_path, base_len);
                out_path[base_len] = '\0';
            } else {
                zxc_log(
                    "Error: Cannot determine output filename: '%s' does not end "
                    "with " ZXC_ARCHIVE_EXT "\n",
                    in_path);
                fclose(f_in);
                return 1;
            }
        }
    }

    // Open output file if not writing to stdout
    if (!use_stdout && mode != MODE_INTEGRITY) {
        if (g_walking) {
            snprintf(resolved_out_path, sizeof(resolved_out_path), "%s", zxc_base_name(out_path));
        } else if (zxc_validate_output_path(out_path, resolved_out_path,
                                            sizeof(resolved_out_path)) != 0) {
            zxc_log("Error: Invalid output path '%s': %s\n", out_path, strerror(errno));
            if (!use_stdin) fclose(f_in);
            return 1;
        }

        // Safety check on resolved paths: opening the output with O_TRUNC would
        // destroy the input if both names refer to the same file
        if (!use_stdin && (strcmp(resolved_in_path, resolved_out_path) == 0 ||
                           zxc_is_input_file(f_in, resolved_out_path))) {
            zxc_log("Error: Input and output files are identical for '%s'.\n", in_path);
            fclose(f_in);
            return 1;
        }

        if (g_preserve && !use_stdin) zxc_get_file_attrs(f_in, &in_attrs);
        // Private while written if the input's mode is restored at the end
        if (zxc_output_open(&output, resolved_out_path, force, in_attrs.valid) != 0) {
            if (errno == EEXIST && !force)
                zxc_log("Error: output exists. Use -f to overwrite '%s'.\n", out_path);
            else
                zxc_log("Error: cannot create output %s: %s\n", out_path, strerror(errno));
            if (!use_stdin) fclose(f_in);
            return 1;
        }
        f_out = output.f;
        out_special = output.special;
        if (out_special) in_attrs.valid = 0;
        created_out_file = 1;
    }

    // Prevent writing binary data to the terminal unless forced
    if (use_stdout && isatty(fileno(stdout)) && mode == MODE_COMPRESS && !force) {
        zxc_log(
            "Error: refusing to write compressed data to terminal.\n"
            "For help, type: zxc -h\n");
        if (!use_stdin) fclose(f_in);
        return 1;
    }

    // POSIX has no binary mode; reopening would truncate stdout and rewind stdin.
#ifdef _WIN32
    if (use_stdin) _setmode(_fileno(stdin), _O_BINARY);
    if (use_stdout) _setmode(_fileno(stdout), _O_BINARY);
#endif

    // Determine if we should show progress bar and get file size
    // IMPORTANT: This must be done BEFORE setting large buffers with setvbuf
    // to avoid buffer inconsistency issues when reading the footer
    int show_progress = 0;
    uint64_t total_size = 0;
    const int stderr_tty = isatty(fileno(stderr)) != 0;

    char* b1 = malloc(ZXC_STDIO_BUFFER_SIZE);
    char* b2 = malloc(ZXC_STDIO_BUFFER_SIZE);
    if (b1) setvbuf(f_in, b1, _IOFBF, ZXC_STDIO_BUFFER_SIZE);
    if (f_out && b2) setvbuf(f_out, b2, _IOFBF, ZXC_STDIO_BUFFER_SIZE);

    // To stdout, -f passes non-archives through
    const int pass_through = mode == MODE_DECOMPRESS && force && use_stdout &&
                             !zxc_starts_like_archive(f_in, !use_stdin);

    // The frame walks seek once per frame: on f_in each would refill its 1 MB buffer.
    FILE* const f_walk = (!use_stdin && mode != MODE_COMPRESS && !pass_through)
                             ? zxc_open_input(resolved_in_path)
                             : NULL;
    FILE* const f_meta = f_walk ? f_walk : f_in;

    if (!g_quiet && g_progress_mode != ZXC_PROGRESS_NEVER &&
        (g_progress_mode == ZXC_PROGRESS_ALWAYS || (!use_stdout && !use_stdin && stderr_tty))) {
        // Get the total size based on mode (only knowable for seekable file input)
        if (!use_stdin && !pass_through) {
            if (mode == MODE_COMPRESS) {
                // Compression: get input file size
                const long long saved_pos = ftello(f_in);
                if (saved_pos >= 0 && fseeko(f_in, 0, SEEK_END) == 0) {
                    const long long size = ftello(f_in);
                    if (size > 0) total_size = (uint64_t)size;
                    fseeko(f_in, saved_pos, SEEK_SET);
                }
            } else {
                // Decompression: get decompressed size from footer (BEFORE starting decompression)
                const int64_t decomp_size = zxc_stream_get_decompressed_size(f_meta);
                if (decomp_size > 0) total_size = (uint64_t)decomp_size;
            }
        }

        // auto: only show progress for files > 1MB; always: unconditionally
        if (g_progress_mode == ZXC_PROGRESS_ALWAYS || total_size > ZXC_STDIO_BUFFER_SIZE)
            show_progress = 1;
    }

    // -t reports the checksum the archive carries; stdin can't be re-read for it
    int archive_has_checksum = -1;  // -1 unknown, 0 none declared, 1 declared
    if (mode == MODE_INTEGRITY && !use_stdin)
        archive_has_checksum = zxc_archive_has_checksum(f_meta);
    if (f_walk) fclose(f_walk);

    if (mode == MODE_COMPRESS)
        zxc_log_v("Processing %s... (Compression Level %d)\n", in_path ? in_path : "<stdin>",
                  level);
    else
        zxc_log_v("Processing %s...\n", in_path ? in_path : "<stdin>");
    // Not in -t: its result line reports the archive's checksum instead
    if (g_verbose && mode != MODE_INTEGRITY)
        zxc_log("Checksum: %s\n", checksum_enabled ? "enabled" : "disabled");
    if (g_verbose && seekable) zxc_log("Seekable: enabled\n");
    if (g_verbose && g_favor_decode_speed && mode == MODE_COMPRESS && level > ZXC_LEVEL_FAST)
        zxc_log("Priority: decode speed, budget %d%%\n", g_split_budget_pct);

    // Prepare progress context
    progress_ctx_t pctx = {.start_time = zxc_now(),
                           .operation = (mode == MODE_COMPRESS)    ? "Compressing"
                                        : (mode == MODE_INTEGRITY) ? "Testing"
                                                                   : "Decompressing",
                           .total_size = total_size,
                           .to_tty = stderr_tty,
                           .last_draw = 0.0,
                           .last_len = 0};

    const double t0 = zxc_now();
    int64_t bytes;
    const zxc_progress_callback_t progress_cb = show_progress ? &cli_progress_callback : NULL;
    if (mode == MODE_COMPRESS) {
        zxc_cctx* cctx = NULL;
        bytes = cli_cctx(&cctx, num_threads, level, block_size, checksum_enabled, seekable,
                         g_favor_decode_speed ? g_split_budget_pct : 0, dict, dict_size, g_dict_huf,
                         progress_cb, &pctx);
        if (bytes == ZXC_OK) bytes = zxc_stream_compress(f_in, f_out, cctx);
        zxc_free_cctx(cctx);
    } else if (pass_through) {
        bytes = zxc_copy_stream(f_in, f_out);
    } else {
        zxc_dctx* dctx = NULL;
        bytes = cli_dctx(&dctx, num_threads, checksum_enabled, dict, dict_size, g_dict_huf,
                         progress_cb, &pctx);
        if (bytes == ZXC_OK) bytes = zxc_stream_decompress(f_in, f_out, dctx);
        zxc_free_dctx(dctx);
    }
    const double dt = zxc_now() - t0;

    // Clear the in-place progress line on completion (off-tty lines end in '\n')
    if (show_progress && pctx.to_tty) zxc_progress_clear(pctx.last_len);

    if (!use_stdin)
        fclose(f_in);
    else
        setvbuf(stdin, NULL, _IONBF, 0);

    // stdio defers write errors to flush/close: a failure here means the
    // output is truncated even though the codec reported success
    int write_error = 0;
    int attrs_lost = 0;
    if (created_out_file) {
        write_error = (fflush(f_out) != 0);
        // After the last write, which would bump the mtime
        attrs_lost = !write_error && bytes >= 0 && in_attrs.valid &&
                     zxc_set_file_attrs(f_out, &in_attrs) != 0;
        write_error |= (fclose(f_out) != 0);
    } else if (use_stdout) {
        write_error = (fflush(stdout) != 0);
        setvbuf(stdout, NULL, _IONBF, 0);
    }

    free(b1);
    free(b2);

    if (bytes >= 0 && write_error) {
        zxc_log("Error: %s: write failed: %s\n", created_out_file ? out_path : "<stdout>",
                strerror(errno));
        if (created_out_file) zxc_output_discard(&output);
        return 1;
    }

    if (bytes >= 0 && created_out_file && zxc_output_commit(&output) != 0) {
        zxc_log("Error: cannot replace %s: %s\n", out_path, strerror(errno));
        zxc_output_discard(&output);
        return 1;
    }

    if (bytes >= 0) {
        if (mode == MODE_INTEGRITY) {
            // Test mode: show result
            // "checksum" uses the same spellings as -l
            const int verified = (archive_has_checksum == 1) && checksum_enabled;
            const char* state = (archive_has_checksum == 1)   ? "enabled"
                                : (archive_has_checksum == 2) ? "mixed"
                                : (archive_has_checksum == 0) ? "disabled"
                                                              : "unknown";
            const char* summary = (archive_has_checksum == 0)   ? "not verified (archive has none)"
                                  : !checksum_enabled           ? "not verified (skipped by -N)"
                                  : (archive_has_checksum == 1) ? "verified"
                                  : (archive_has_checksum == 2)
                                      ? "partly verified (some frames have none)"
                                  : use_stdin ? "unknown (streamed input)"
                                              : "unknown (frame unreadable)";
            if (json_output) {
                zxc_test_json_separator();
                printf("{\n  \"filename\": ");
                zxc_json_string(in_path ? in_path : "<stdin>");
                printf(
                    ",\n"
                    "  \"status\": \"ok\",\n"
                    "  \"checksum_verified\": %s,\n"
                    "  \"checksum\": \"%s\",\n"
                    "  \"time_seconds\": %.6f\n"
                    "}\n",
                    verified ? "true" : "false", state, dt);
            } else if (g_verbose) {
                printf(
                    "%s: %sOK%s\n"
                    "  Checksum:     %s\n"
                    "  Time:         %.3fs\n",
                    in_path ? in_path : "<stdin>", ZXC_CO(ZXC_ANSI_GREEN), ZXC_CO(ZXC_ANSI_RESET),
                    summary, dt);
            } else {
                printf("%s: %sOK%s\n", in_path ? in_path : "<stdin>", ZXC_CO(ZXC_ANSI_GREEN),
                       ZXC_CO(ZXC_ANSI_RESET));
            }
        } else {
            zxc_log_v("Processed %lld bytes in %.3fs\n", (long long)bytes, dt);
        }
        const int remove_input = !use_stdin && !use_stdout && !keep_input && !out_path_override &&
                                 mode != MODE_INTEGRITY && !out_special;
        // The input is the only copy of what could not be carried over
        if (attrs_lost)
            zxc_log("Warning: %s: could not copy the mode and times of %s%s\n", out_path, in_path,
                    remove_input ? "; input kept" : "");
        else if (remove_input)
            // The name given: through a symlink the target stays
            unlink(g_walking ? resolved_in_path : in_path);
    } else {
        if (mode == MODE_INTEGRITY) {
            const int err_code = (int)bytes;
            const char* reason = zxc_error_name(err_code);
            const int needs_dict =
                (err_code == ZXC_ERROR_DICT_REQUIRED || err_code == ZXC_ERROR_DICT_MISMATCH);
            if (json_output) {
                zxc_test_json_separator();
                printf("{\n  \"filename\": ");
                zxc_json_string(in_path ? in_path : "<stdin>");
                printf(
                    ",\n"
                    "  \"status\": \"failed\",\n"
                    "  \"error\": \"%s\"\n"
                    "}\n",
                    reason);
            } else {
                fprintf(stderr, "%s: %sFAILED%s (%s)\n", in_path ? in_path : "<stdin>",
                        ZXC_CE(ZXC_ANSI_RED), ZXC_CE(ZXC_ANSI_RESET), reason);
                if (needs_dict)
                    fprintf(stderr,
                            "  This archive was compressed with a dictionary; pass it with -D.\n");
            }
        } else {
            zxc_log("Error: %s: %s\n", in_path ? in_path : "<stdin>", zxc_error_name((int)bytes));
            if (created_out_file) zxc_output_discard(&output);
        }
        overall_ret = 1;
    }

    return overall_ret;
}

/**
 * @brief Main entry point.
 * Parses arguments and dispatches execution to Benchmark, Compress, or
 * Decompress modes.
 */
// Applies the last --color of the command line before the options are
// processed, so that an error raised by an earlier option is colored as asked.
// A silent first getopt pass: invalid values are left to the option loop.
// It runs on a copy of argv, which getopt_long reorders: "zxc 25 -b" must not
// reach the option loop as "zxc -b 25".
static void zxc_color_prescan(const int argc, char** argv, const char* short_options,
                              const struct option* long_options) {
    char** const args = malloc(((size_t)argc + 1) * sizeof(*args));
    if (args != NULL) {
        memcpy(args, argv, (size_t)argc * sizeof(*args));
        args[argc] = NULL;
        int opt;
        opterr = 0;
        while ((opt = getopt_long(argc, args, short_options, long_options, NULL)) != -1) {
            if (opt == OPT_COLOR) {
                const int color_mode = zxc_color_parse(optarg);
                if (color_mode >= 0) g_color_mode = color_mode;
            } else if ((opt == 'b' || opt == OPT_FAVOR_DECODE_SPEED) && optarg == NULL) {
                (void)cli_number_arg(argc, args);  // as the option loop does
            }
        }
        opterr = 1;
        optind = ZXC_GETOPT_REWIND;
        free(args);
    }
    zxc_color_init();
}

int main(int argc, char** argv) {
    zxc_mode_t mode = MODE_COMPRESS;
    int to_stdout = 0;
    int cat_mode = 0;

#ifndef _WIN32
    // Reader gone (zxc -dc f | head): die quietly, even if the parent ignored SIGPIPE
    signal(SIGPIPE, SIG_DFL);
#endif
    zxc_install_signal_cleanup();
    zxc_color_init();

    /* As "unzxc", default to decompression; as "zxccat", to zxc -dcf on every
     * argument. An explicit mode flag below still wins. */
    {
        const char* prog = (argc > 0 && argv[0]) ? argv[0] : "zxc";
        const char* slash = strrchr(prog, '/');
#ifdef _WIN32
        const char* bslash = strrchr(prog, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
        const char* base = slash ? slash + 1 : prog;
        if (strstr(base, "unzxc")) mode = MODE_DECOMPRESS;
        if (strstr(base, "zxccat")) {
            mode = MODE_DECOMPRESS;
            to_stdout = cat_mode = 1;
        }
    }

    int num_threads = 0;
    int keep_input = 0;
    int rm_input = 0;
    int force = 0;
    int bench_seconds = 5;
    int checksum = -1;
    int level = ZXC_LEVEL_DEFAULT;
    int json_output = 0;
    size_t block_size = 0;
    int seekable = 0;
    const char* dict_path = NULL;
    const char* output_path = NULL;

    static const char short_options[] = "1234567b::B:cCdD:fho:jklmrNqST:tvVz";
    static const struct option long_options[] = {
        {"train", no_argument, 0, OPT_TRAIN_DICT},
        {"progress", required_argument, 0, OPT_PROGRESS},
        {"color", required_argument, 0, OPT_COLOR},
        {"no-preserve", no_argument, 0, OPT_NO_PRESERVE},
        {"favor-decode-speed", optional_argument, 0, OPT_FAVOR_DECODE_SPEED},
        {"output", required_argument, 0, 'o'},
        {"dict", required_argument, 0, 'D'},
        {"compress", no_argument, 0, 'z'},
        {"decompress", no_argument, 0, 'd'},
        {"list", no_argument, 0, 'l'},
        {"test", no_argument, 0, 't'},
        {"bench", optional_argument, 0, 'b'},
        {"threads", required_argument, 0, 'T'},
        {"keep", no_argument, 0, 'k'},
        {"rm", no_argument, 0, OPT_RM},
        {"force", no_argument, 0, 'f'},
        {"stdout", no_argument, 0, 'c'},
        {"verbose", no_argument, 0, 'v'},
        {"quiet", no_argument, 0, 'q'},
        {"checksum", no_argument, 0, 'C'},
        {"no-checksum", no_argument, 0, 'N'},
        {"json", no_argument, 0, 'j'},
        {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},
        {"multiple", no_argument, 0, 'm'},
        {"recursive", no_argument, 0, 'r'},
        {"block-size", required_argument, 0, 'B'},
        {"seekable", no_argument, 0, 'S'},
        {0, 0, 0, 0}};

    int opt;
    int multiple_mode = 0;
    int recursive_mode = 0;
    g_args_given = malloc(((size_t)argc + 1) * sizeof(*g_args_given));
    if (g_args_given != NULL) memcpy(g_args_given, argv, ((size_t)argc + 1) * sizeof(*argv));
    zxc_color_prescan(argc, argv, short_options, long_options);
    while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
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
            case 'b': {
                mode = MODE_BENCHMARK;
                const char* const bench_arg = optarg ? optarg : cli_number_arg(argc, argv);
                if (bench_arg) {
                    char* end = NULL;
                    const long secs = strtol(bench_arg, &end, 10);
                    if (end == bench_arg || *end != '\0' || secs < 1 || secs > 3600) {
                        zxc_err("Error: duration must be between 1 and 3600 seconds\n");
                        return 1;
                    }
                    bench_seconds = (int)secs;
                }
                break;
            }
            case '1':
                level = 1;
                break;
            case '2':
                level = 2;
                break;
            case '3':
                level = 3;
                break;
            case '4':
                level = 4;
                break;
            case '5':
                level = 5;
                break;
            case '6':
                level = 6;
                break;
            case '7':
                level = 7;
                break;
            case 'T': {
                char* end = NULL;
                const long threads = strtol(optarg, &end, 10);
                if (end == optarg || *end != '\0' || threads < 0 || threads > ZXC_MAX_THREADS) {
                    zxc_err("Error: num_threads must be between 0 and %d\n", ZXC_MAX_THREADS);
                    return 1;
                }
                num_threads = (int)threads;
                break;
            }
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
            case 'j':
                json_output = 1;
                break;
            case 'm':
                multiple_mode = 1;
                break;
            case 'S':
                seekable = 1;
                break;
            case 'D':
                dict_path = optarg;
                break;
            case OPT_TRAIN_DICT:
                mode = MODE_TRAIN_DICT;
                break;
            case OPT_NO_PRESERVE:
                g_preserve = 0;
                break;
            case OPT_RM:
                rm_input = 1;
                break;
            case OPT_FAVOR_DECODE_SPEED: {
                g_favor_decode_speed = 1;
                g_split_budget_pct = ZXC_FAVOR_DECODE_SPEED_DEFAULT;
                const char* const pct_arg = optarg ? optarg : cli_number_arg(argc, argv);
                if (pct_arg == NULL) break;
                char* end = NULL;
                const long pct = strtol(pct_arg, &end, 10);
                if (end == pct_arg || *end != '\0' || pct < 1 || pct > ZXC_FAVOR_DECODE_SPEED_MAX) {
                    zxc_err("Error: --favor-decode-speed budget must be 1 to %d\n",
                            ZXC_FAVOR_DECODE_SPEED_MAX);
                    return 1;
                }
                g_split_budget_pct = (int)pct;
                break;
            }
            case OPT_PROGRESS:
                if (optarg != NULL && strcmp(optarg, "auto") == 0)
                    g_progress_mode = ZXC_PROGRESS_AUTO;
                else if (optarg != NULL && strcmp(optarg, "always") == 0)
                    g_progress_mode = ZXC_PROGRESS_ALWAYS;
                else if (optarg != NULL && strcmp(optarg, "never") == 0)
                    g_progress_mode = ZXC_PROGRESS_NEVER;
                else {
                    zxc_err("Error: --progress must be 'auto', 'always' or 'never'\n");
                    return 1;
                }
                break;
            case OPT_COLOR:
                // Applied by zxc_color_prescan(); only the value is checked here
                if (zxc_color_parse(optarg) < 0) {
                    zxc_err("Error: --color must be 'auto', 'always' or 'never'\n");
                    return 1;
                }
                break;
            case 'o':
                output_path = optarg;
                break;
            case 'r':
                recursive_mode = 1;
                multiple_mode = 1;  // Recursive implies multiple mode for files processing
                break;
            case 'B': {
                char* end = NULL;
                const long long bs_val = strtoll(optarg, &end, 10);
                long long multiplier = 1;
                if (*end == 'k' || *end == 'K') {
                    multiplier = 1024;
                    end++;
                    if (*end == 'b' || *end == 'B') end++;  // optional "B" in "KB"
                } else if (*end == 'm' || *end == 'M') {
                    multiplier = 1024 * 1024;
                    end++;
                    if (*end == 'b' || *end == 'B') end++;  // optional "B" in "MB"
                }
                // Bound the value before multiplying to avoid signed overflow
                const long long bs =
                    (bs_val > 0 && bs_val <= (long long)ZXC_BLOCK_SIZE_MAX / multiplier)
                        ? bs_val * multiplier
                        : 0;
                if (end == optarg || *end != '\0' || bs < ZXC_BLOCK_SIZE_MIN ||
                    (bs & (bs - 1)) != 0) {
                    zxc_err(
                        "Error: block-size must be a power of 2 between 4K and 2M\n"
                        "  Examples: -B 4K, -B 128K, -B 1M, -B 2M\n");
                    return 1;
                }
                block_size = (size_t)bs;
                break;
            }
            case '?':
                print_help(argv[0]);
                return 1;
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

    if (checksum == -1) {
        checksum = (mode == MODE_BENCHMARK) ? 0 : 1;
    }

    // The input stays unless --rm asks otherwise; -k wins over --rm
    if (!rm_input) keep_input = 1;

    if (g_favor_decode_speed && level <= ZXC_LEVEL_FAST) {
        if (mode == MODE_COMPRESS || mode == MODE_BENCHMARK)
            zxc_log("Warning: --favor-decode-speed has no effect at levels 1 and 2\n");
        // Nothing applies at PAK levels: -j reports it off
        g_favor_decode_speed = 0;
        g_split_budget_pct = 0;
    }

    // -f only if no mode flag took over
    if (cat_mode && mode == MODE_DECOMPRESS) force = 1;

    /* Load dictionary file (.zxd) if requested */
    void* dict = NULL;
    size_t dict_size = 0;
    if (dict_path) {
        char resolved_dict[4096];
        if (zxc_validate_input_path(dict_path, resolved_dict, sizeof(resolved_dict)) != 0) {
            zxc_err("Error: invalid dictionary path '%s': %s\n", dict_path, strerror(errno));
            return 1;
        }
        FILE* f_dict = fopen(resolved_dict, "rb");
        if (!f_dict) {
            zxc_err("Error: cannot open dictionary '%s': %s\n", dict_path, strerror(errno));
            return 1;
        }
        fseeko(f_dict, 0, SEEK_END);
        const long long fsize = ftello(f_dict);
        fseeko(f_dict, 0, SEEK_SET);
        if (fsize <= 0 ||
            (size_t)fsize > ZXC_DICT_SIZE_MAX + ZXC_DICT_HEADER_SIZE + ZXC_HUF_TABLE_SIZE) {
            zxc_err("Error: dictionary file '%s' has invalid size\n", dict_path);
            fclose(f_dict);
            return 1;
        }
        uint8_t* zxd_buf = (uint8_t*)malloc((size_t)fsize);
        if (!zxd_buf || fread(zxd_buf, 1, (size_t)fsize, f_dict) != (size_t)fsize) {
            zxc_err("Error: failed to read dictionary '%s'\n", dict_path);
            free(zxd_buf);
            fclose(f_dict);
            return 1;
        }
        fclose(f_dict);

        const void* content = NULL;
        size_t content_size = 0;
        const void* huf = NULL;
        const int rc = zxc_dict_load(zxd_buf, (size_t)fsize, &content, &content_size, &huf, NULL);
        if (rc != ZXC_OK) {
            zxc_err("Error: invalid dictionary '%s': %s\n", dict_path, zxc_error_name(rc));
            free(zxd_buf);
            return 1;
        }
        /* content_size is a file-derived length; zxc_dict_load already
         * validates it, but re-check the untrusted size at the alloc/copy
         * boundary so the bound governing memcpy is explicit at the sink. */
        if (content_size == 0 || content_size > ZXC_DICT_SIZE_MAX) {
            zxc_err("Error: invalid dictionary '%s'\n", dict_path);
            free(zxd_buf);
            return 1;
        }
        dict = malloc(content_size);
        if (!dict) {
            free(zxd_buf);
            return 1;
        }
        memcpy(dict, content, content_size);
        dict_size = content_size;

        /* Shared literal Huffman table (zero-copy into zxd_buf; .zxd always
         * carries one, so huf is non-NULL after a successful load). */
        if (huf) {
            g_dict_huf = malloc(ZXC_HUF_TABLE_SIZE);
            if (!g_dict_huf) {
                free(dict);
                free(zxd_buf);
                return 1;
            }
            memcpy(g_dict_huf, huf, ZXC_HUF_TABLE_SIZE);
        }
        free(zxd_buf);
    }

    /*
     * Train Dictionary Mode
     * Reads input files as samples, trains a dictionary, saves as .zxd.
     */
    if (mode == MODE_TRAIN_DICT) {
        if (optind >= argc) {
            zxc_err("Error: --train requires input files as training samples.\n");
            free(dict);
            return 1;
        }
        const int n_files = argc - optind;
        void** samples = (void**)malloc((size_t)n_files * sizeof(void*));
        size_t* sample_sizes = (size_t*)malloc((size_t)n_files * sizeof(size_t));
        if (!samples || !sample_sizes) {
            zxc_err("Error: memory allocation failed\n");
            free(samples);
            free(sample_sizes);
            free(dict);
            return 1;
        }
        int n_loaded = 0;
        for (int i = optind; i < argc; i++) {
            char resolved[4096];
            if (zxc_validate_input_path(argv[i], resolved, sizeof(resolved)) != 0) {
                zxc_err("Warning: invalid path '%s', skipping\n", argv[i]);
                continue;
            }
            FILE* sf = fopen(resolved, "rb");
            if (!sf) {
                zxc_err("Warning: cannot open '%s', skipping\n", argv[i]);
                continue;
            }
            fseeko(sf, 0, SEEK_END);
            size_t sz = (size_t)ftello(sf);
            fseeko(sf, 0, SEEK_SET);
            if (sz == 0) {
                fclose(sf);
                continue;
            }
            uint8_t* buf = (uint8_t*)malloc(sz);
            if (!buf) {
                fclose(sf);
                continue;
            }
            const size_t rd = fread(buf, 1, sz, sf);
            fclose(sf);
            if (rd != sz) {
                zxc_err("Warning: short read on '%s', skipping\n", resolved);
                free(buf);
                continue;
            }
            samples[n_loaded] = buf;
            sample_sizes[n_loaded] = sz;
            n_loaded++;
        }
        if (n_loaded == 0) {
            zxc_err("Error: no valid samples loaded\n");
            free(samples);
            free(sample_sizes);
            free(dict);
            return 1;
        }

        size_t dict_cap = ZXC_DICT_SIZE_MAX;
        if (block_size > 0 && block_size < dict_cap) dict_cap = block_size;
        uint8_t* dict_buf = (uint8_t*)malloc(dict_cap);
        if (!dict_buf) {
            zxc_err("Error: memory allocation failed\n");
            for (int i = 0; i < n_loaded; i++) free(samples[i]);
            free(samples);
            free(sample_sizes);
            free(dict);
            return 1;
        }

        int64_t dict_sz = zxc_dict_train_content((const void* const*)samples, sample_sizes,
                                                 (size_t)n_loaded, dict_buf, dict_cap);

        /* Train the shared literal Huffman table on the same samples (needs
         * the trained dict for the post-LZ literal distribution). The .zxd
         * format always carries the table, so a failure here is fatal. */
        uint8_t huf_lengths[ZXC_HUF_TABLE_SIZE];
        int huf_rc = ZXC_ERROR_NULL_INPUT;
        if (dict_sz > 0) {
            huf_rc = zxc_dict_train_huf((const void* const*)samples, sample_sizes, (size_t)n_loaded,
                                        dict_buf, (size_t)dict_sz, huf_lengths);
        }

        for (int i = 0; i < n_loaded; i++) free(samples[i]);
        free(samples);
        free(sample_sizes);

        if (dict_sz <= 0) {
            zxc_err("Error: training failed: %s\n", zxc_error_name((int)dict_sz));
            free(dict_buf);
            free(dict);
            return 1;
        }
        if (huf_rc != ZXC_OK) {
            zxc_err("Error: literal table training failed: %s\n", zxc_error_name(huf_rc));
            free(dict_buf);
            free(dict);
            return 1;
        }

        const size_t zxd_bound = zxc_dict_save_bound((size_t)dict_sz);
        uint8_t* zxd = (uint8_t*)malloc(zxd_bound);
        if (!zxd) {
            zxc_err("Error: memory allocation failed\n");
            free(dict_buf);
            free(dict);
            return 1;
        }
        const int64_t zxd_sz =
            zxc_dict_save(dict_buf, (size_t)dict_sz, huf_lengths, zxd, zxd_bound);
        free(dict_buf);
        if (zxd_sz <= 0) {
            zxc_err("Error: dict save failed: %s\n", zxc_error_name((int)zxd_sz));
            free(zxd);
            free(dict);
            return 1;
        }

        /*
         * Resolve the output path (from -o, optional). With no -o, write the
         * content-addressable name dictionary_{dict_id:08x}.zxd in the current
         * directory. If -o names a directory (or ends with a separator), use that
         * name inside it; otherwise write to the -o path verbatim. The id must be
         * computed before opening the file so it can name it.
         */
        const uint32_t trained_id = zxc_dict_read_id(zxd, (size_t)zxd_sz);
        char final_path[4096];
        if (!output_path) {
            snprintf(final_path, sizeof(final_path), "dictionary_%08x.zxd", trained_id);
        } else {
            const size_t op_len = strlen(output_path);
            const int is_dir_target =
                zxc_is_directory(output_path) ||
                (op_len > 0 && (output_path[op_len - 1] == '/' || output_path[op_len - 1] == '\\'));
            if (is_dir_target) {
                const int has_sep = op_len > 0 && (output_path[op_len - 1] == '/' ||
                                                   output_path[op_len - 1] == '\\');
                snprintf(final_path, sizeof(final_path), "%s%sdictionary_%08x.zxd", output_path,
                         has_sep ? "" : "/", trained_id);
            } else {
                snprintf(final_path, sizeof(final_path), "%s", output_path);
            }
        }

        zxc_output_t out;
        if (zxc_output_open(&out, final_path, force, 0) != 0) {
            if (errno == EEXIST && !force)
                zxc_err("Error: output exists. Use -f to overwrite '%s'.\n", final_path);
            else
                zxc_err("Error: cannot create '%s': %s\n", final_path, strerror(errno));
            free(zxd);
            free(dict);
            return 1;
        }
        const size_t nwritten = fwrite(zxd, 1, (size_t)zxd_sz, out.f);
        const int close_rc = fclose(out.f);
        free(zxd);
        if (nwritten != (size_t)zxd_sz || close_rc != 0) {
            zxc_err("Error: failed to write '%s': %s\n", final_path, strerror(errno));
            zxc_output_discard(&out);
            free(dict);
            return 1;
        }
        if (zxc_output_commit(&out) != 0) {
            zxc_err("Error: cannot replace '%s': %s\n", final_path, strerror(errno));
            zxc_output_discard(&out);
            free(dict);
            return 1;
        }

        fprintf(stderr, "Trained dictionary: %lld bytes from %d samples -> %s (dict_id: 0x%08X)\n",
                (long long)dict_sz, n_loaded, final_path, trained_id);

        free(dict);
        return 0;
    }

    /*
     * Benchmark Mode
     * Loads the entire input file into RAM to measure raw algorithm throughput
     * without disk I/O bottlenecks.
     */
    if (mode == MODE_BENCHMARK) {
        if (optind >= argc) {
            zxc_log("Error: benchmark requires input file.\n");
            free(dict);
            return 1;
        }
        const char* in_path = argv[optind];
        int ret = 1;
        uint8_t* ram = NULL;
        uint8_t* c_dat = NULL;
        FILE* fm = NULL;

        char resolved_path[4096];
        if (zxc_validate_input_path(in_path, resolved_path, sizeof(resolved_path)) != 0) {
            zxc_log("Error: Invalid input file '%s': %s\n", in_path, strerror(errno));
            free(dict);
            return 1;
        }

        // Same policy as the file progress bar: auto shows it on a terminal only.
        const int stderr_tty = isatty(fileno(stderr)) != 0;
        const int show_progress = !json_output && !g_quiet &&
                                  g_progress_mode != ZXC_PROGRESS_NEVER &&
                                  (g_progress_mode == ZXC_PROGRESS_ALWAYS || stderr_tty);
        double last_line = -1.0;

        zxc_cctx* bench_cctx = NULL;
        zxc_dctx* bench_dctx = NULL;
        FILE* f_in = NULL;
        int bench_rc = cli_cctx(&bench_cctx, num_threads, level, block_size, checksum, 0,
                                g_favor_decode_speed ? g_split_budget_pct : 0, dict, dict_size,
                                g_dict_huf, NULL, NULL);
        if (bench_rc == ZXC_OK)
            bench_rc = cli_dctx(&bench_dctx, num_threads, checksum, dict, dict_size, g_dict_huf,
                                NULL, NULL);
        if (bench_rc != ZXC_OK) {
            zxc_log("Error: Cannot configure the benchmark: %s\n", zxc_error_name(bench_rc));
            goto bench_cleanup;
        }

        f_in = fopen(resolved_path, "rb");
        if (!f_in) {
            zxc_log("Error: Cannot open '%s': %s\n", in_path, strerror(errno));
            goto bench_cleanup;
        }

        if (fseeko(f_in, 0, SEEK_END) != 0) {
            zxc_log("Error: Cannot seek in '%s': %s\n", in_path, strerror(errno));
            goto bench_cleanup;
        }
        const long long fsize = ftello(f_in);
        if (fsize <= 0) {
            zxc_log("Error: '%s' is empty or not a regular file\n", in_path);
            goto bench_cleanup;
        }
        const size_t in_size = (size_t)fsize;
        if (fseeko(f_in, 0, SEEK_SET) != 0) {
            zxc_log("Error: Cannot seek in '%s': %s\n", in_path, strerror(errno));
            goto bench_cleanup;
        }

        ram = malloc(in_size);
        if (!ram) {
            zxc_log("Error: Out of memory (%llu bytes)\n", (unsigned long long)in_size);
            goto bench_cleanup;
        }
        if (fread(ram, 1, in_size, f_in) != in_size) {
            zxc_log("Error: Short read on '%s'\n", in_path);
            goto bench_cleanup;
        }
        fclose(f_in);
        f_in = NULL;

        if (!json_output)
            printf(
                "Input: %s (%zu bytes)\n"
                "Running for %d seconds (threads: %d)...\n",
                in_path, in_size, bench_seconds, num_threads);

#ifdef _WIN32
        if (!json_output) printf("Note: Using tmpfile on Windows (slower than fmemopen).\n");
        fm = tmpfile();
        if (fm) {
            fwrite(ram, 1, in_size, fm);
            fseek(fm, 0, SEEK_SET);
        }
#else
        fm = fmemopen(ram, in_size, "rb");
#endif
        if (!fm) {
            zxc_log("Error: Cannot create the in-memory stream: %s\n", strerror(errno));
            goto bench_cleanup;
        }

        double best_compress = 1e30;
        int compress_iters = 0;
        const double compress_deadline = zxc_now() + (double)bench_seconds;
        const double compress_start = zxc_now();
        while (zxc_now() < compress_deadline) {
            fseek(fm, 0, SEEK_SET);
            const double t0 = zxc_now();
            zxc_stream_compress(fm, NULL, bench_cctx);
            const double dt = zxc_now() - t0;
            if (dt < best_compress) best_compress = dt;
            compress_iters++;
            if (show_progress)
                zxc_bench_progress("Compressing", compress_iters, zxc_now() - compress_start,
                                   stderr_tty, &last_line);
        }
        if (show_progress && stderr_tty) zxc_progress_clear(64);
        fclose(fm);
        fm = NULL;

        const uint64_t max_c = zxc_compress_bound(in_size);
        c_dat = malloc((size_t)max_c);
        if (!c_dat) {
            zxc_log("Error: Out of memory (%llu bytes)\n", (unsigned long long)max_c);
            goto bench_cleanup;
        }

#ifdef _WIN32
        FILE* fm_in = tmpfile();
        FILE* fm_out = tmpfile();
        if (!fm_in || !fm_out) {
            zxc_log("Error: Cannot create the in-memory stream: %s\n", strerror(errno));
            if (fm_in) fclose(fm_in);
            if (fm_out) fclose(fm_out);
            goto bench_cleanup;
        }
        fwrite(ram, 1, in_size, fm_in);
        fseek(fm_in, 0, SEEK_SET);
#else
        FILE* fm_in = fmemopen(ram, in_size, "rb");
        FILE* fm_out = fmemopen(c_dat, max_c, "wb");
        if (!fm_in || !fm_out) {
            zxc_log("Error: Cannot create the in-memory stream: %s\n", strerror(errno));
            if (fm_in) fclose(fm_in);
            if (fm_out) fclose(fm_out);
            goto bench_cleanup;
        }
#endif

        const int64_t c_sz = zxc_stream_compress(fm_in, fm_out, bench_cctx);
        if (c_sz < 0) {
            zxc_log("Error: Compression failed: %s\n", zxc_error_name((int)c_sz));
            fclose(fm_in);
            fclose(fm_out);
            fm_in = NULL;
            fm_out = NULL;
            goto bench_cleanup;
        }

#ifdef _WIN32
        if (fseek(fm_out, 0, SEEK_SET) != 0 ||
            fread(c_dat, 1, (size_t)c_sz, fm_out) != (size_t)c_sz) {
            zxc_log("Error: Short read on the compressed stream\n");
            fclose(fm_in);
            fclose(fm_out);
            fm_in = NULL;
            fm_out = NULL;
            goto bench_cleanup;
        }
        fclose(fm_in);
        fclose(fm_out);
#else
        fclose(fm_in);
        fclose(fm_out);
#endif

#ifdef _WIN32
        FILE* fc = tmpfile();
        if (!fc) {
            zxc_log("Error: Cannot create the in-memory stream: %s\n", strerror(errno));
            goto bench_cleanup;
        }
        fwrite(c_dat, 1, (size_t)c_sz, fc);
        fseek(fc, 0, SEEK_SET);
#else
        FILE* fc = fmemopen(c_dat, (size_t)c_sz, "rb");
        if (!fc) {
            zxc_log("Error: Cannot create the in-memory stream: %s\n", strerror(errno));
            goto bench_cleanup;
        }
#endif

        double best_decompress = 1e30;
        int decompress_iters = 0;
        const double decompress_deadline = zxc_now() + (double)bench_seconds;
        const double decompress_start = zxc_now();
        last_line = -1.0;
        while (zxc_now() < decompress_deadline) {
            fseek(fc, 0, SEEK_SET);
            const double t0 = zxc_now();
            zxc_stream_decompress(fc, NULL, bench_dctx);
            const double dt = zxc_now() - t0;
            if (dt < best_decompress) best_decompress = dt;
            decompress_iters++;
            if (show_progress)
                zxc_bench_progress("Decompressing", decompress_iters, zxc_now() - decompress_start,
                                   stderr_tty, &last_line);
        }
        if (show_progress && stderr_tty) zxc_progress_clear(64);
        fclose(fc);

        const double compress_speed_mbps = (double)in_size / (1000.0 * 1000.0) / best_compress;
        const double decompress_speed_mbps = (double)in_size / (1000.0 * 1000.0) / best_decompress;
        const double ratio = (c_sz > 0) ? ((double)in_size / c_sz) : 0.0;

        if (json_output) {
            printf("{\n  \"input_file\": ");
            zxc_json_string(in_path);
            printf(
                ",\n"
                "  \"input_size_bytes\": %zu,\n"
                "  \"compressed_size_bytes\": %lld,\n"
                "  \"compression_ratio\": %.3f,\n"
                "  \"duration_seconds\": %d,\n"
                "  \"compress_iterations\": %d,\n"
                "  \"decompress_iterations\": %d,\n"
                "  \"threads\": %d,\n"
                "  \"level\": %d,\n"
                "  \"checksum_enabled\": %s,\n"
                "  \"favor_decode_speed\": %s,\n"
                "  \"split_budget_pct\": %d,\n"
                "  \"compress_speed_mbps\": %.3f,\n"
                "  \"decompress_speed_mbps\": %.3f,\n"
                "  \"compress_time_seconds\": %.6f,\n"
                "  \"decompress_time_seconds\": %.6f\n"
                "}\n",
                in_size, (long long)c_sz, ratio, bench_seconds, compress_iters, decompress_iters,
                num_threads, level, checksum ? "true" : "false",
                g_favor_decode_speed ? "true" : "false", g_split_budget_pct, compress_speed_mbps,
                decompress_speed_mbps, best_compress, best_decompress);
        } else {
            printf(
                "Compressed: %lld bytes (ratio %.3f)\n"
                "Compress  : %.3f MB/s (%d iters)\n"
                "Decompress: %.3f MB/s (%d iters)\n",
                (long long)c_sz, ratio, compress_speed_mbps, compress_iters, decompress_speed_mbps,
                decompress_iters);
        }
        ret = 0;

    bench_cleanup:
        zxc_free_cctx(bench_cctx);
        zxc_free_dctx(bench_dctx);
        if (fm) fclose(fm);
        if (f_in) fclose(f_in);
        free(ram);
        free(c_dat);
        free(dict);
        return ret;
    }

    /*
     * List Mode
     * Displays archive information (compressed size, uncompressed size, ratio).
     */
    if (mode == MODE_LIST) {
        free(dict);
        if (optind >= argc) {
            zxc_log("Error: list mode requires input file.\n");
            return 1;
        }
        int ret = 0;
        const int num_files = argc - optind;

        if (json_output && num_files > 1) printf("[\n");

        for (int i = optind; i < argc; i++) {
            const int r = zxc_list_archive(argv[i], json_output, num_files > 1);
            // Keep the JSON array well-formed: a failed entry prints nothing,
            // so emit an error object in its place
            if (r != 0 && json_output) {
                printf("{\n  \"filename\": ");
                zxc_json_string(argv[i]);
                printf(",\n  \"error\": \"cannot list file\"\n}\n");
            }
            ret |= r;
            if (json_output && num_files > 1 && i < argc - 1) {
                printf(",\n");
            }
        }

        if (json_output && num_files > 1) {
            printf("]\n");
        }

        return ret;
    }

    if (multiple_mode && to_stdout) {
        zxc_log("Error: cannot write to stdout when using multiple files mode (-m).\n");
        free(dict);
        return 1;
    }

    if (multiple_mode && output_path) {
        zxc_log("Error: cannot use -o with multiple files mode (-m/-r).\n");
        free(dict);
        return 1;
    }

    /*
     * File Processing Mode
     * Loops over files and determines input/output paths.
     */
    int overall_ret = 0;
    const int start_optind = optind;

    // If no files passed but we aren't using stdin, or mode expects files:
    if (optind >= argc && mode == MODE_INTEGRITY) {
        zxc_log("Error: test mode requires at least one input file.\n");
        free(dict);
        return 1;
    }

    if (multiple_mode && optind >= argc) {
        zxc_log("Error: multiple files mode requires at least one input file.\n");
        free(dict);
        return 1;
    }

    // No output name to tell apart: every operand is an input.
    const int all_inputs = multiple_mode || cat_mode || to_stdout || mode == MODE_INTEGRITY;
    if (!all_inputs && argc - optind > (output_path ? 1 : 2)) {
        zxc_log("Error: too many file operands; use -m for several input files.\n");
        free(dict);
        return 1;
    }

    // Default to processing at least once (for stdin) if no files are passed and not in a mode that
    // strictly needs files
    const int num_files_to_process = (optind < argc) ? (argc - optind) : 1;

    g_test_json_array =
        mode == MODE_INTEGRITY && json_output && (num_files_to_process > 1 || recursive_mode);
    if (g_test_json_array) printf("[\n");

    for (int file_idx = 0; file_idx < num_files_to_process; file_idx++) {
        const char* current_arg = (optind < argc) ? argv[start_optind + file_idx] : NULL;

        if (recursive_mode && current_arg && strcmp(current_arg, "-") != 0 &&
            zxc_is_link(current_arg)) {
            zxc_log("Warning: '%s' is a link, ignored\n", current_arg);
        } else if (recursive_mode && current_arg && strcmp(current_arg, "-") != 0 &&
                   zxc_is_directory(current_arg)) {
            overall_ret |= process_directory(current_arg, mode, num_threads, keep_input, force,
                                             to_stdout, checksum, level, block_size, json_output,
                                             seekable, dict, dict_size);
        } else {
            // -o takes precedence over a positional OUTPUT-FILE.
            const char* explicit_out_path = NULL;
            if (!all_inputs) {
                if (output_path)
                    explicit_out_path = output_path;
                else if (optind + 1 < argc && current_arg &&
                         (strcmp(current_arg, "-") != 0 ||
                          strcmp(argv[start_optind + 1], "-") != 0))
                    explicit_out_path = argv[start_optind + 1];
            }

            overall_ret |= process_single_file(current_arg, explicit_out_path, mode, num_threads,
                                               keep_input, force, to_stdout, checksum, level,
                                               block_size, json_output, seekable, dict, dict_size);
        }

        if (!all_inputs) break;
    }
    if (g_test_json_array) printf("]\n");
    free(dict);
    return overall_ret;
}
