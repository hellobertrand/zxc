/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "../include/zxc_buffer.h"
#include "../include/zxc_dict.h"
#include "../include/zxc_error.h"
#include "../include/zxc_seekable.h"
#include "../tests/vector_io.h"
#include "invalid_cases.h"
#include "valid_cases.h"

/* ---------- helpers ------------------------------------------------------ */

static int file_exists(const char* path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

static int has_suffix(const char* s, const char* suffix) {
    size_t slen = strlen(s), xlen = strlen(suffix);
    if (slen < xlen) return 0;
    return memcmp(s + slen - xlen, suffix, xlen) == 0;
}

/* ---------- valid vector test -------------------------------------------- */

/**
 * @brief Searches for a .zxd dictionary file in the same directory as @p zxc_path
 *        whose dict_id matches @p target_id. Returns the loaded content (caller frees)
 *        and, via @p huf_out, the shared literal Huffman table carried by the .zxd.
 */
static uint8_t* find_dict_for_id(const char* zxc_path, uint32_t target_id, const void** content_out,
                                 size_t* content_size_out, const void** huf_out) {
    /* Derive directory from zxc_path */
    char dir[512];
    size_t plen = strlen(zxc_path);
    if (plen >= sizeof(dir)) plen = sizeof(dir) - 1;
    memcpy(dir, zxc_path, plen);
    dir[plen] = '\0';
    char* sep = strrchr(dir, '/');
    if (sep)
        *(sep + 1) = '\0';
    else
        snprintf(dir, sizeof(dir), "./");

#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s*.zxd", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hf = FindFirstFileA(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return NULL;
    do {
        char path[1024];
        snprintf(path, sizeof(path), "%s%s", dir, fd.cFileName);
        size_t sz = 0;
        uint8_t* buf = vio_read_file(path, &sz);
        if (buf && zxc_dict_get_id(buf, sz) == target_id) {
            if (zxc_dict_load(buf, sz, content_out, content_size_out, huf_out, NULL) == 0) {
                FindClose(hf);
                return buf;
            }
        }
        free(buf);
    } while (FindNextFileA(hf, &fd));
    FindClose(hf);
#else
    DIR* dp = opendir(dir);
    if (!dp) return NULL;
    const struct dirent* ent;
    while ((ent = readdir(dp)) != NULL) {
        if (!has_suffix(ent->d_name, ".zxd")) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s%s", dir, ent->d_name);
        size_t sz = 0;
        uint8_t* buf = vio_read_file(path, &sz);
        if (buf && zxc_dict_get_id(buf, sz) == target_id) {
            if (zxc_dict_load(buf, sz, content_out, content_size_out, huf_out, NULL) == 0) {
                closedir(dp);
                return buf;
            }
        }
        free(buf);
    }
    closedir(dp);
#endif
    return NULL;
}

static int test_valid_vector(const char* zxc_path, const char* expected_path) {
    size_t comp_sz = 0, expected_sz = 0;
    uint8_t* comp = vio_read_file(zxc_path, &comp_sz);
    uint8_t* expected = vio_read_file(expected_path, &expected_sz);

    if (!comp) {
        fprintf(stderr, "FAIL: cannot read %s\n", zxc_path);
        free(expected);
        return 0;
    }
    if (!expected) {
        fprintf(stderr, "FAIL: cannot read %s\n", expected_path);
        free(comp);
        return 0;
    }

    /* Auto-detect dictionary: if the archive has a dict_id, find the .zxd */
    const void* dict = NULL;
    size_t dict_size = 0;
    const void* dict_huf = NULL;
    uint8_t* dict_buf = NULL;
    uint32_t did = zxc_get_dict_id(comp, comp_sz);
    if (did != 0) {
        dict_buf = find_dict_for_id(zxc_path, did, &dict, &dict_size, &dict_huf);
        if (!dict_buf) {
            fprintf(stderr, "FAIL: %s  requires dict 0x%08X but no matching .zxd found\n", zxc_path,
                    did);
            free(comp);
            free(expected);
            return 0;
        }
    }

    int ok = 1;

    uint64_t dec_sz = zxc_get_decompressed_size(comp, comp_sz);

    if (dec_sz != expected_sz) {
        fprintf(stderr, "FAIL: %s  size mismatch: got %llu, expected %zu\n", zxc_path,
                (unsigned long long)dec_sz, expected_sz);
        ok = 0;
    } else if (expected_sz == 0) {
        /* Nothing to decompress: size match is sufficient. */
    } else {
        uint8_t* output = malloc((size_t)dec_sz);
        if (!output) {
            fprintf(stderr, "FAIL: %s  OOM\n", zxc_path);
            ok = 0;
        } else {
            zxc_decompress_opts_t dopts = {0};
            if (dict) {
                dopts.dict = dict;
                dopts.dict_size = dict_size;
                dopts.dict_huf = dict_huf;
            }
            int64_t result = zxc_decompress(comp, comp_sz, output, (size_t)dec_sz, &dopts);
            if (result < 0) {
                fprintf(stderr, "FAIL: %s  decompress failed: %s\n", zxc_path,
                        zxc_error_name((int)result));
                ok = 0;
            } else if ((size_t)result != expected_sz) {
                fprintf(stderr, "FAIL: %s  output size %lld != expected %zu\n", zxc_path,
                        (long long)result, expected_sz);
                ok = 0;
            } else if (memcmp(output, expected, expected_sz) != 0) {
                fprintf(stderr, "FAIL: %s  output content mismatch\n", zxc_path);
                ok = 0;
            }
            free(output);
        }
    }

    free(dict_buf);
    free(comp);
    free(expected);
    return ok;
}

/* ---------- invalid vector test ------------------------------------------ */

/* Matches a vector path against the table by file stem. Returns 1 on hit. */
static const invalid_expect_t* expect_for(const char* zxc_path) {
    const char* base = strrchr(zxc_path, '/');
#ifdef _WIN32
    const char* bs = strrchr(zxc_path, '\\');
    if (bs && (!base || bs > base)) base = bs;
#endif
    base = base ? base + 1 : zxc_path;

    const size_t len = strlen(base);
    if (len < 4 || strcmp(base + len - 4, ".zxc") != 0) return NULL;
    const size_t stem = len - 4;

    for (size_t i = 0; i < INVALID_EXPECT_COUNT; i++) {
        if (strlen(INVALID_EXPECT[i].name) == stem &&
            strncmp(INVALID_EXPECT[i].name, base, stem) == 0)
            return &INVALID_EXPECT[i];
    }
    return NULL;
}

static int test_invalid_vector(const char* zxc_path, const char* valid_dir) {
    size_t comp_sz = 0;
    uint8_t* comp = vio_read_file(zxc_path, &comp_sz);
    if (!comp) {
        fprintf(stderr, "FAIL: cannot read %s\n", zxc_path);
        return 0;
    }

    const invalid_expect_t* exp = expect_for(zxc_path);
    if (!exp) {
        fprintf(stderr, "FAIL: %s  has no entry in INVALID_EXPECT\n", zxc_path);
        free(comp);
        return 0;
    }

    /* The seek table is advisory metadata that a sequential decode ignores, so
     * a forged entry can only be caught where the entries are consumed. */
    if (exp->via_seekable) {
        char good[2048];
        snprintf(good, sizeof good, "%s/seekable_4blocks.zxc", valid_dir);
        size_t good_n = 0;
        uint8_t* good_buf = vio_read_file(good, &good_n);
        zxc_seekable* control = good_buf ? zxc_seekable_open(good_buf, good_n) : NULL;
        free(good_buf);
        if (!control) {
            fprintf(stderr, "FAIL: %s  seekable open rejects a well-formed archive too\n",
                    zxc_path);
            free(comp);
            return 0;
        }
        zxc_seekable_free(control);

        zxc_seekable* s = zxc_seekable_open(comp, comp_sz);
        if (s) {
            fprintf(stderr, "FAIL: %s  seekable open accepted a forged seek table\n", zxc_path);
            zxc_seekable_free(s);
            free(comp);
            return 0;
        }
        free(comp);
        return 1;
    }

    int ok = 1;

    uint64_t dec_sz = zxc_get_decompressed_size(comp, comp_sz);
    const size_t scratch_cap = 1U << 20;
    const size_t out_cap = (dec_sz > 0 && dec_sz <= scratch_cap) ? (size_t)dec_sz : scratch_cap;

    uint8_t* output = malloc(out_cap);
    if (output) {
        /* Verify with checksum enabled so checksum/payload-corruption
         * vectors are caught (verification needs both the file flag and
         * this opt; see zxc_decompress_block in zxc_dispatch.c). */
        zxc_decompress_opts_t io = {.checksum_enabled = 1};

        /* Some defects sit past an earlier gate: a forged dict_id is only
         * reachable once a dictionary is actually offered. */
        uint8_t* dict_buf = NULL;
        if (exp->dict) {
            char dpath[2048];
            snprintf(dpath, sizeof dpath, "%s/%s", valid_dir, exp->dict);
            size_t dsz = 0;
            dict_buf = vio_read_file(dpath, &dsz);
            if (!dict_buf ||
                zxc_dict_load(dict_buf, dsz, &io.dict, &io.dict_size, &io.dict_huf, NULL) != 0) {
                fprintf(stderr, "FAIL: %s  cannot load %s\n", zxc_path, dpath);
                free(dict_buf);
                free(output);
                free(comp);
                return 0;
            }
        }

        int64_t result = zxc_decompress(comp, comp_sz, output, out_cap, &io);
        free(dict_buf);
        if (result >= 0) {
            fprintf(stderr, "FAIL: %s  should be rejected but decoded %lld bytes\n", zxc_path,
                    (long long)result);
            ok = 0;
        } else {
            const int want = exp->expected;
            if ((int)result != want) {
                fprintf(stderr, "FAIL: %s  rejected as %s, expected %s\n", zxc_path,
                        zxc_error_name((int)result), zxc_error_name(want));
                ok = 0;
            }
        }
        free(output);
    } else {
        fprintf(stderr, "FAIL: %s  out of memory\n", zxc_path);
        ok = 0;
    }

    free(comp);
    return ok;
}

/* ---------- portable directory scanner ----------------------------------- */

typedef struct {
    char** names;
    size_t count;
    size_t capacity;
} name_list_t;

static void name_list_add(name_list_t* l, const char* name) {
    if (l->count >= l->capacity) {
        l->capacity = l->capacity ? l->capacity * 2 : 64;
        l->names = realloc(l->names, l->capacity * sizeof(char*));
    }
#ifdef _WIN32
    l->names[l->count++] = _strdup(name);
#else
    l->names[l->count++] = strdup(name);
#endif
}

static int name_cmp(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static void name_list_sort(name_list_t* l) {
    if (l->count > 1) qsort(l->names, l->count, sizeof(char*), name_cmp);
}

static void name_list_free(name_list_t* l) {
    for (size_t i = 0; i < l->count; i++) free(l->names[i]);
    free(l->names);
    l->names = NULL;
    l->count = l->capacity = 0;
}

static int list_zxc_files(const char* dir, name_list_t* out) {
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof pattern, "%s\\*.zxc", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) name_list_add(out, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir);
    if (!d) return -1;
    const struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (has_suffix(ent->d_name, ".zxc")) name_list_add(out, ent->d_name);
    }
    closedir(d);
#endif
    name_list_sort(out);
    return 0;
}

/* Look up a valid vector's recipe by file stem, or NULL. */
static const valid_case_t* valid_case_for(const char* stem) {
    for (size_t i = 0; i < VALID_CASE_COUNT; i++)
        if (strcmp(VALID_CASES[i].name, stem) == 0) return &VALID_CASES[i];
    return NULL;
}

/* ---------- recipe reproduction ------------------------------------------ */

/* Neither other guard sees a corpus recut with the wrong options: the manifest
 * only says the bytes have not moved, and the test above only says the archive
 * decodes to its .expected, which stays true at any level. The vector named
 * "level5" would then silently stop testing level 5. */
static int test_recipe(const char* valid_dir, const valid_case_t* vc) {
    char path[2048];
    int ok = 0;

    snprintf(path, sizeof path, "%s/%s.expected", valid_dir, vc->name);
    size_t in_size = 0;
    uint8_t* input = vio_read_file(path, &in_size);
    if (!input) {
        fprintf(stderr, "FAIL: %s  cannot read %s\n", vc->name, path);
        return 0;
    }

    snprintf(path, sizeof path, "%s/%s.zxc", valid_dir, vc->name);
    size_t ref_size = 0;
    uint8_t* ref = vio_read_file(path, &ref_size);
    if (!ref) {
        fprintf(stderr, "FAIL: %s  declared in VALID_CASES but the archive is missing\n", vc->name);
        free(input);
        return 0;
    }

    /* The .zxd carries the content and its Huffman table; both feed the dict_id. */
    zxc_compress_opts_t opts = vc->opts;
    uint8_t* dict_buf = NULL;
    int dict_ok = 1;
    if (vc->dict) {
        snprintf(path, sizeof path, "%s/%s", valid_dir, vc->dict);
        size_t dict_file_size = 0;
        dict_buf = vio_read_file(path, &dict_file_size);
        dict_ok = dict_buf && zxc_dict_load(dict_buf, dict_file_size, &opts.dict, &opts.dict_size,
                                            &opts.dict_huf, NULL) == 0;
        if (!dict_ok) fprintf(stderr, "FAIL: %s  cannot load dictionary %s\n", vc->name, vc->dict);
    }

    if (dict_ok) {
        size_t cap = (size_t)zxc_compress_bound(in_size) + 4096;
        uint8_t* out = (uint8_t*)malloc(cap);
        if (!out) {
            fprintf(stderr, "FAIL: %s  OOM\n", vc->name);
        } else {
            int64_t csize = zxc_compress(input, in_size, out, cap, &opts);
            if (csize <= 0) {
                fprintf(stderr, "FAIL: %s  compress -> %s\n", vc->name, zxc_error_name((int)csize));
            } else if ((size_t)csize != ref_size || memcmp(out, ref, ref_size) != 0) {
                fprintf(stderr,
                        "FAIL: %s  does not match its recipe (level %d, block %zu KB%s%s):\n"
                        "        committed %zu bytes, recipe produces %lld\n"
                        "        regenerate with zxc_valid_gen, or fix valid_cases.h\n",
                        vc->name, vc->opts.level, vc->opts.block_size / 1024u,
                        vc->opts.checksum_enabled ? ", checksum" : "",
                        vc->opts.seekable ? ", seekable" : "", ref_size, (long long)csize);
            } else {
                ok = 1;
            }
            free(out);
        }
    }

    free(dict_buf);
    free(ref);
    free(input);
    return ok;
}

/* The error-code table says each vector still fails for its own reason; this
 * says each is still the archive its recipe describes. Without it they drifted:
 * the committed vectors were an older encoder's output. */
static int test_invalid_recipe(const char* invalid_dir, invalid_bases_t* bases, const char* name) {
    uint8_t* want = NULL;
    size_t want_n = 0;
    if (!build_invalid(bases, name, &want, &want_n)) {
        fprintf(stderr, "FAIL: %s  cannot be rebuilt from invalid_cases.h\n", name);
        return 0;
    }

    char path[2048];
    snprintf(path, sizeof path, "%s/%s.zxc", invalid_dir, name);
    size_t have_n = 0;
    uint8_t* have = vio_read_file(path, &have_n);
    int ok = 0;
    if (!have) {
        fprintf(stderr, "FAIL: %s  declared generated but missing\n", name);
    } else if (have_n != want_n || memcmp(have, want, want_n) != 0) {
        fprintf(stderr,
                "FAIL: %s  does not match its recipe (committed %zu bytes, recipe %zu)\n"
                "        regenerate with zxc_invalid_gen, or fix invalid_cases.h\n",
                name, have_n, want_n);
    } else {
        ok = 1;
    }
    free(have);
    free(want);
    return ok;
}

/* ---------- main --------------------------------------------------------- */

int main(int argc, char** argv) {
    /* One frozen corpus per format version, <root>/v<N>/. Composing it from the
     * constant means a bump looks for a corpus that does not exist yet, rather
     * than testing vectors this decoder could not decode anyway (Sec 10.3). */
    const char* root = "conformance";
    if (argc > 1) root = argv[1];

    char valid_dir[512], invalid_dir[512];
    snprintf(valid_dir, sizeof valid_dir, "%s/v%u/valid", root, (unsigned)ZXC_FILE_FORMAT_VERSION);
    snprintf(invalid_dir, sizeof invalid_dir, "%s/v%u/invalid", root,
             (unsigned)ZXC_FILE_FORMAT_VERSION);

    int passed = 0, failed = 0, total = 0;

    {
        char vpath[1024];
        snprintf(vpath, sizeof vpath, "%s/v%u/FORMAT_VERSION", root,
                 (unsigned)ZXC_FILE_FORMAT_VERSION);
        size_t vn = 0;
        uint8_t* vbuf = vio_read_file(vpath, &vn);
        const long declared = vbuf ? strtol((const char*)vbuf, NULL, 10) : -1;
        free(vbuf);
        total++;
        if (declared != (long)ZXC_FILE_FORMAT_VERSION) {
            fprintf(stderr, "FAIL: %s declares version %ld, expected %u\n", vpath, declared,
                    (unsigned)ZXC_FILE_FORMAT_VERSION);
            failed++;
        } else {
            passed++;
        }
    }

    /* --- Valid vectors --- */
    printf("=== Valid vectors (%s) ===\n", valid_dir);
    {
        name_list_t zxc_files = {0};
        if (list_zxc_files(valid_dir, &zxc_files) < 0) {
            fprintf(stderr, "Cannot open %s\n", valid_dir);
            return 1;
        }

        for (size_t i = 0; i < zxc_files.count; i++) {
            char zxc_path[1024], exp_path[1024];
            snprintf(zxc_path, sizeof zxc_path, "%s/%s", valid_dir, zxc_files.names[i]);

            char stem[256];
            snprintf(stem, sizeof stem, "%s", zxc_files.names[i]);
            stem[strlen(stem) - 4] = '\0';

            snprintf(exp_path, sizeof exp_path, "%s/%s.expected", valid_dir, stem);

            total++;
            /* Checked before the .expected lookup, so a stray archive cannot
             * slip past by simply not having one. */
            if (!valid_case_for(stem)) {
                fprintf(stderr, "FAIL: %s  has no entry in VALID_CASES\n", stem);
                failed++;
            } else if (!file_exists(exp_path)) {
                fprintf(stderr, "FAIL: %s  has no .expected file\n", stem);
                failed++;
            } else if (test_valid_vector(zxc_path, exp_path)) {
                printf("  PASS: %s\n", stem);
                passed++;
            } else {
                failed++;
            }
        }

        name_list_free(&zxc_files);
    }

    /* --- Invalid vectors --- */
    printf("\n=== Invalid vectors (%s) ===\n", invalid_dir);
    {
        name_list_t zxc_files = {0};
        if (list_zxc_files(invalid_dir, &zxc_files) < 0) {
            fprintf(stderr, "Cannot open %s\n", invalid_dir);
            return 1;
        }

        for (size_t i = 0; i < zxc_files.count; i++) {
            char zxc_path[1024];
            snprintf(zxc_path, sizeof zxc_path, "%s/%s", invalid_dir, zxc_files.names[i]);

            char stem[256];
            snprintf(stem, sizeof stem, "%s", zxc_files.names[i]);
            stem[strlen(stem) - 4] = '\0';

            total++;
            if (test_invalid_vector(zxc_path, valid_dir)) {
                printf("  PASS: %s  (correctly rejected)\n", stem);
                passed++;
            } else {
                failed++;
            }
        }

        name_list_free(&zxc_files);
    }

    /* --- Valid vector recipes --- */
    printf("\n=== Valid vector recipes (%s) ===\n", valid_dir);
    for (size_t i = 0; i < VALID_CASE_COUNT; i++) {
        total++;
        if (test_recipe(valid_dir, &VALID_CASES[i])) {
            printf("  PASS: %s\n", VALID_CASES[i].name);
            passed++;
        } else {
            failed++;
        }
    }

    /* --- Invalid vector recipes --- */
    printf("\n=== Invalid vector recipes (%s) ===\n", invalid_dir);
    {
        invalid_bases_t bases = {0};
        for (size_t i = 0; i < INVALID_EXPECT_COUNT; i++) {
            if (!INVALID_EXPECT[i].generated) continue;
            total++;
            if (test_invalid_recipe(invalid_dir, &bases, INVALID_EXPECT[i].name)) {
                printf("  PASS: %s\n", INVALID_EXPECT[i].name);
                passed++;
            } else {
                failed++;
            }
        }
        invalid_bases_free(&bases);
    }

    /* Every declared vector must exist */
    for (size_t i = 0; i < INVALID_EXPECT_COUNT; i++) {
        char p[1024];
        snprintf(p, sizeof p, "%s/%s.zxc", invalid_dir, INVALID_EXPECT[i].name);
        total++;
        if (file_exists(p)) {
            passed++;
        } else {
            fprintf(stderr, "FAIL: %s declared in INVALID_EXPECT but the vector is missing\n",
                    INVALID_EXPECT[i].name);
            failed++;
        }
    }

    /* --- Summary --- */
    printf("\n=== Summary ===\n");
    printf("Total: %d  Passed: %d  Failed: %d\n", total, passed, failed);

    if (failed > 0) {
        printf("CONFORMANCE TESTS FAILED.\n");
        return 1;
    }

    printf("ALL CONFORMANCE TESTS PASSED.\n");
    return 0;
}
