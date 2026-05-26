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
#include <dirent.h>
#include <sys/stat.h>

#include "../include/zxc_buffer.h"
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

static int has_suffix(const char *s, const char *suffix)
{
    size_t slen = strlen(s), xlen = strlen(suffix);
    if (slen < xlen) return 0;
    return memcmp(s + slen - xlen, suffix, xlen) == 0;
}

/* ---------- valid vector test -------------------------------------------- */

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

    int ok = 1;

    uint64_t dec_sz = zxc_get_decompressed_size(comp, comp_sz);

    if (dec_sz != expected_sz) {
        fprintf(stderr, "FAIL: %s  size mismatch: got %llu, expected %zu\n",
                zxc_path, (unsigned long long)dec_sz, expected_sz);
        ok = 0;
    } else if (expected_sz == 0) {
        /* Nothing to decompress — size match is sufficient. */
    } else {
        uint8_t *output = malloc((size_t)dec_sz);
        if (!output) {
            fprintf(stderr, "FAIL: %s  OOM\n", zxc_path);
            ok = 0;
        } else {
            int64_t result = zxc_decompress(comp, comp_sz,
                                            output, (size_t)dec_sz, NULL);
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

    if (dec_sz > 0) {
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
    /* dec_sz == 0 means the size query rejected it — correct for invalid data. */

    free(comp);
    return ok;
}

/* ---------- directory scanner -------------------------------------------- */

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
    l->names[l->count++] = strdup(name);
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
    printf("=== Valid vectors (zxc: %s, expected: %s) ===\n", valid_dir, valid_dir);
    {
        DIR *d = opendir(valid_dir);
        if (!d) {
            fprintf(stderr, "Cannot open %s\n", valid_dir);
            return 1;
        }

        name_list_t zxc_files = {0};
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (has_suffix(ent->d_name, ".zxc"))
                name_list_add(&zxc_files, ent->d_name);
        }
        closedir(d);
        name_list_sort(&zxc_files);

        for (size_t i = 0; i < zxc_files.count; i++) {
            char zxc_path[1024], exp_path[1024];
            snprintf(zxc_path, sizeof zxc_path, "%s/%s", valid_dir, zxc_files.names[i]);

            char stem[512];
            snprintf(stem, sizeof stem, "%s", zxc_files.names[i]);
            stem[strlen(stem) - 4] = '\0';

            snprintf(exp_path, sizeof exp_path, "%s/%s.expected", valid_dir, stem);

            struct stat st;
            if (stat(exp_path, &st) != 0) {
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
        DIR *d = opendir(invalid_dir);
        if (!d) {
            fprintf(stderr, "Cannot open %s\n", invalid_dir);
            return 1;
        }

        name_list_t zxc_files = {0};
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (has_suffix(ent->d_name, ".zxc"))
                name_list_add(&zxc_files, ent->d_name);
        }
        closedir(d);
        name_list_sort(&zxc_files);

        for (size_t i = 0; i < zxc_files.count; i++) {
            char zxc_path[1024];
            snprintf(zxc_path, sizeof zxc_path, "%s/%s", invalid_dir, zxc_files.names[i]);

            char stem[512];
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
