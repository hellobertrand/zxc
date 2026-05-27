/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "../include/zxc_buffer.h"
#include "../include/zxc_dict.h"
#include "../include/zxc_error.h"

/* ---------- helpers ------------------------------------------------------ */

static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len < 0) { fclose(f); return NULL; }
    if (len == 0) {
        fclose(f);
        *out_size = 0;
        return calloc(1, 1);
    }

    uint8_t *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); fclose(f); return NULL;
    }

    fclose(f);
    *out_size = (size_t)len;
    return buf;
}

static int file_exists(const char *path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path, &st) == 0;
#endif
}

static int has_suffix(const char *s, const char *suffix)
{
    size_t slen = strlen(s), xlen = strlen(suffix);
    if (slen < xlen) return 0;
    return memcmp(s + slen - xlen, suffix, xlen) == 0;
}

/* ---------- valid vector test -------------------------------------------- */

/**
 * @brief Searches for a .zxd dictionary file in the same directory as @p zxc_path
 *        whose dict_id matches @p target_id. Returns the loaded content (caller frees).
 */
static uint8_t *find_dict_for_id(const char *zxc_path, uint32_t target_id,
                                 const void **content_out, size_t *content_size_out)
{
    /* Derive directory from zxc_path */
    char dir[512];
    strncpy(dir, zxc_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *sep = strrchr(dir, '/');
    if (sep) *(sep + 1) = '\0'; else strcpy(dir, "./");

#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s*.zxd", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hf = FindFirstFileA(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return NULL;
    do {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", dir, fd.cFileName);
        size_t sz = 0;
        uint8_t *buf = read_file(path, &sz);
        if (buf && zxc_dict_get_id(buf, sz) == target_id) {
            if (zxc_dict_load(buf, sz, content_out, content_size_out, NULL) == 0) {
                FindClose(hf);
                return buf;
            }
        }
        free(buf);
    } while (FindNextFileA(hf, &fd));
    FindClose(hf);
#else
    DIR *dp = opendir(dir);
    if (!dp) return NULL;
    const struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        if (!has_suffix(ent->d_name, ".zxd")) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s%s", dir, ent->d_name);
        size_t sz = 0;
        uint8_t *buf = read_file(path, &sz);
        if (buf && zxc_dict_get_id(buf, sz) == target_id) {
            if (zxc_dict_load(buf, sz, content_out, content_size_out, NULL) == 0) {
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

static int test_valid_vector(const char *zxc_path, const char *expected_path)
{
    size_t comp_sz = 0, expected_sz = 0;
    uint8_t *comp = read_file(zxc_path, &comp_sz);
    uint8_t *expected = read_file(expected_path, &expected_sz);

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
    const void *dict = NULL;
    size_t dict_size = 0;
    uint8_t *dict_buf = NULL;
    uint32_t did = zxc_get_dict_id(comp, comp_sz);
    if (did != 0) {
        dict_buf = find_dict_for_id(zxc_path, did, &dict, &dict_size);
        if (!dict_buf) {
            fprintf(stderr, "FAIL: %s  requires dict 0x%08X but no matching .zxd found\n",
                    zxc_path, did);
            free(comp); free(expected);
            return 0;
        }
    }

    int ok = 1;

    uint64_t dec_sz = zxc_get_decompressed_size(comp, comp_sz);

    if (dec_sz != expected_sz) {
        fprintf(stderr, "FAIL: %s  size mismatch: got %llu, expected %zu\n",
                zxc_path, (unsigned long long)dec_sz, expected_sz);
        ok = 0;
    } else if (expected_sz == 0) {
        /* Nothing to decompress: size match is sufficient. */
    } else {
        uint8_t *output = malloc((size_t)dec_sz);
        if (!output) {
            fprintf(stderr, "FAIL: %s  OOM\n", zxc_path);
            ok = 0;
        } else {
            zxc_decompress_opts_t dopts = {0};
            if (dict) { dopts.dict = dict; dopts.dict_size = dict_size; }
            int64_t result = zxc_decompress(comp, comp_sz,
                                            output, (size_t)dec_sz, &dopts);
            if (result < 0) {
                fprintf(stderr, "FAIL: %s  decompress failed: %s\n",
                        zxc_path, zxc_error_name((int)result));
                ok = 0;
            } else if ((size_t)result != expected_sz) {
                fprintf(stderr, "FAIL: %s  output size %lld != expected %zu\n",
                        zxc_path, (long long)result, expected_sz);
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

static int test_invalid_vector(const char *zxc_path)
{
    size_t comp_sz = 0;
    uint8_t *comp = read_file(zxc_path, &comp_sz);
    if (!comp) {
        fprintf(stderr, "FAIL: cannot read %s\n", zxc_path);
        return 0;
    }

    int ok = 1;

    uint64_t dec_sz = zxc_get_decompressed_size(comp, comp_sz);

    if (dec_sz > 0 && dec_sz <= (1 << 20)) {
        uint8_t *output = malloc((size_t)dec_sz);
        if (output) {
            int64_t result = zxc_decompress(comp, comp_sz,
                                            output, (size_t)dec_sz, NULL);
            if (result >= 0) {
                fprintf(stderr, "FAIL: %s  should be rejected but decoded %lld bytes\n",
                        zxc_path, (long long)result);
                ok = 0;
            }
            free(output);
        }
    }

    free(comp);
    return ok;
}

/* ---------- portable directory scanner ----------------------------------- */

typedef struct {
    char **names;
    size_t count;
    size_t capacity;
} name_list_t;

static void name_list_add(name_list_t *l, const char *name)
{
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

static int name_cmp(const void *a, const void *b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static void name_list_sort(name_list_t *l) {
    if (l->count > 1) qsort(l->names, l->count, sizeof(char*), name_cmp);
}

static void name_list_free(name_list_t *l) {
    for (size_t i = 0; i < l->count; i++) free(l->names[i]);
    free(l->names);
    l->names = NULL;
    l->count = l->capacity = 0;
}

static int list_zxc_files(const char *dir, name_list_t *out)
{
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof pattern, "%s\\*.zxc", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            name_list_add(out, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return -1;
    const struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (has_suffix(ent->d_name, ".zxc"))
            name_list_add(out, ent->d_name);
    }
    closedir(d);
#endif
    name_list_sort(out);
    return 0;
}

/* ---------- main --------------------------------------------------------- */

int main(int argc, char **argv)
{
    const char *base = "conformance";
    if (argc > 1) base = argv[1];

    char valid_dir[512], invalid_dir[512];
    snprintf(valid_dir, sizeof valid_dir, "%s/valid", base);
    snprintf(invalid_dir, sizeof invalid_dir, "%s/invalid", base);

    int passed = 0, failed = 0, total = 0;

    /* --- Valid vectors --- */
    printf("=== Valid vectors (%s) ===\n", valid_dir);
    {
        name_list_t zxc_files = {0};
        if (list_zxc_files(valid_dir, &zxc_files) < 0) {
            fprintf(stderr, "Cannot open %s\n", valid_dir);
            return 1;
        }

        for (size_t i = 0; i < zxc_files.count; i++) {
            char zxc_path[2048], exp_path[2048];
            snprintf(zxc_path, sizeof zxc_path, "%s/%s", valid_dir, zxc_files.names[i]);

            char stem[1024];
            snprintf(stem, sizeof stem, "%s", zxc_files.names[i]);
            stem[strlen(stem) - 4] = '\0';

            snprintf(exp_path, sizeof exp_path, "%s/%s.expected", valid_dir, stem);

            if (!file_exists(exp_path)) {
                fprintf(stderr, "SKIP: %s  (no .expected file)\n", zxc_files.names[i]);
                continue;
            }

            total++;
            if (test_valid_vector(zxc_path, exp_path)) {
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

            char stem[1024];
            snprintf(stem, sizeof stem, "%s", zxc_files.names[i]);
            stem[strlen(stem) - 4] = '\0';

            total++;
            if (test_invalid_vector(zxc_path)) {
                printf("  PASS: %s  (correctly rejected)\n", stem);
                passed++;
            } else {
                failed++;
            }
        }

        name_list_free(&zxc_files);
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
