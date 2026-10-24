// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

#include "test_common.h"

int test_opaque_context_api(void) {
    printf("=== TEST: Opaque Context API (zxc_create_cctx / zxc_create_dctx) ===\n");

    /* 1. NULL context -> ZXC_ERROR_NULL_INPUT */
    {
        uint8_t d[64] = {0};
        if (zxc_compress_cctx(NULL, d, sizeof(d), d, sizeof(d)) != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] compress_cctx NULL ctx\n");
            return 0;
        }
        if (zxc_decompress_dctx(NULL, d, sizeof(d), d, sizeof(d)) != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] decompress_dctx NULL ctx\n");
            return 0;
        }
        printf("  [PASS] NULL context -> ZXC_ERROR_NULL_INPUT\n");
    }

    /* 2. Create, multi-call reuse, free */
    zxc_cctx* cctx = zxc_create_cctx();
    zxc_dctx* dctx = zxc_create_dctx();
    if (!cctx || !dctx) {
        printf("  [FAIL] create returned NULL\n");
        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
        return 0;
    }

    const size_t src_sz = 8192;
    uint8_t* src = malloc(src_sz);
    const size_t comp_cap = zxc_compress_bound(src_sz);
    uint8_t* comp = malloc(comp_cap);
    uint8_t* dec = malloc(src_sz);

    /* 3. Three calls with the SAME cctx: level 1, 3, 5; checksums on at 3 */
    for (int lvl = 1; lvl <= 5; lvl += 2) {
        gen_lz_data(src, src_sz);
        if (zxc_cctx_set_param(cctx, ZXC_C_LEVEL, lvl) != ZXC_OK ||
            zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, lvl == 3) != ZXC_OK ||
            zxc_dctx_set_param(dctx, ZXC_D_CHECKSUM, lvl == 3) != ZXC_OK) {
            printf("  [FAIL] set_param level %d\n", lvl);
            goto fail;
        }
        const int64_t csz = zxc_compress_cctx(cctx, src, src_sz, comp, comp_cap);
        if (csz <= 0) {
            printf("  [FAIL] compress_cctx level %d returned %lld\n", lvl, (long long)csz);
            goto fail;
        }

        const int64_t dsz = zxc_decompress_dctx(dctx, comp, (size_t)csz, dec, src_sz);
        if (dsz != (int64_t)src_sz || memcmp(src, dec, src_sz) != 0) {
            printf("  [FAIL] roundtrip level %d (dsz=%lld)\n", lvl, (long long)dsz);
            goto fail;
        }
    }
    printf("  [PASS] Multi-call reuse (level 1, 3, 5)\n");

    /* 4. Free is safe to call multiple times / on NULL */
    zxc_free_cctx(cctx);
    cctx = NULL;
    zxc_free_cctx(NULL); /* no-op */
    zxc_free_dctx(dctx);
    dctx = NULL;
    zxc_free_dctx(NULL);
    printf("  [PASS] Free + double-free + NULL safety\n");

    free(src);
    free(comp);
    free(dec);
    printf("PASS\n\n");
    return 1;

fail:
    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(src);
    free(comp);
    free(dec);
    return 0;
}

/* Regression: a cctx whose buffers were carved below ZXC_LEVEL_DENSITY has no
 * opt_scratch; a level raise into the optimal-parser tier must re-init the
 * inner buffers instead of dereferencing the NULL scratch (crash before the
 * fix). Also pins where levels are clamped (the one-shot) and where they are
 * refused (the setter, ZXC_ERROR_BAD_LEVEL). */
int test_cctx_level_raise_reinit(void) {
    printf("=== TEST: Opaque Context API - level raise re-init + level validation ===\n");

    const size_t src_sz = 8192;
    uint8_t* src = malloc(src_sz);
    const size_t comp_cap = zxc_compress_bound(src_sz);
    uint8_t* comp = malloc(comp_cap);
    uint8_t* dec = malloc(src_sz);
    zxc_cctx* cctx = NULL;
    zxc_dctx* dctx = NULL;
    if (!src || !comp || !dec) goto fail;
    gen_lz_data(src, src_sz);

    /* 1. Buffers carved at level 3 (no opt_scratch) by a first call, then
     *    raised to 7 in place. */
    cctx = zxc_create_cctx();
    dctx = zxc_create_dctx();
    if (!cctx || !dctx) {
        printf("  [FAIL] create returned NULL\n");
        goto fail;
    }
    if (zxc_compress_cctx(cctx, src, src_sz, comp, comp_cap) <= 0) {
        printf("  [FAIL] level-3 call to carve the buffers\n");
        goto fail;
    }
    const int64_t csz = zxc_cctx_set_param(cctx, ZXC_C_LEVEL, ZXC_LEVEL_ULTRA) == ZXC_OK
                            ? zxc_compress_cctx(cctx, src, src_sz, comp, comp_cap)
                            : ZXC_ERROR_BAD_LEVEL;
    if (csz <= 0) {
        printf("  [FAIL] level-7 raise on level-3 cctx returned %lld\n", (long long)csz);
        goto fail;
    }
    const int64_t dsz = zxc_decompress_dctx(dctx, comp, (size_t)csz, dec, src_sz);
    if (dsz != (int64_t)src_sz || memcmp(src, dec, src_sz) != 0) {
        printf("  [FAIL] roundtrip after level raise (dsz=%lld)\n", (long long)dsz);
        goto fail;
    }
    printf("  [PASS] level 3 -> 7 raise re-inits and roundtrips\n");

    /* 2. Same raise through the block API on a fresh low-level cctx. */
    zxc_free_cctx(cctx);
    cctx = zxc_create_cctx();
    if (!cctx) {
        printf("  [FAIL] re-create returned NULL\n");
        goto fail;
    }
    if (zxc_compress_block(cctx, src, src_sz, comp, comp_cap) <= 0) {
        printf("  [FAIL] level-3 block to carve the buffers\n");
        goto fail;
    }
    const int64_t bsz = zxc_cctx_set_param(cctx, ZXC_C_LEVEL, ZXC_LEVEL_DENSITY) == ZXC_OK
                            ? zxc_compress_block(cctx, src, src_sz, comp, comp_cap)
                            : ZXC_ERROR_BAD_LEVEL;
    if (bsz <= 0) {
        printf("  [FAIL] level-6 block raise returned %lld\n", (long long)bsz);
        goto fail;
    }
    printf("  [PASS] level 3 -> 6 raise through the block API\n");

    /* 3. Out-of-range levels: the one-shot and the setter both refuse them,
     *    the setter keeps its level and the context stays usable. 0 means the
     *    default: the one-shot must produce the exact level-3 archive. */
    const int bad_levels[] = {99, ZXC_LEVEL_ULTRA + 1, -5, -1};
    int64_t lvl = 0;
    for (size_t i = 0; i < sizeof(bad_levels) / sizeof(bad_levels[0]); i++) {
        const int bad = bad_levels[i];
        if (zxc_compress(src, src_sz, comp, comp_cap, bad) != ZXC_ERROR_BAD_LEVEL ||
            zxc_cctx_set_param(cctx, ZXC_C_LEVEL, bad) != ZXC_ERROR_BAD_LEVEL ||
            zxc_cctx_get_param(cctx, ZXC_C_LEVEL, &lvl) != ZXC_OK || lvl != ZXC_LEVEL_DENSITY) {
            printf("  [FAIL] level %d must be refused everywhere, setter level kept (now %lld)\n",
                   bad, (long long)lvl);
            goto fail;
        }
    }
    if (zxc_compress_block(cctx, src, src_sz, comp, comp_cap) <= 0) {
        printf("  [FAIL] context unusable after refused levels\n");
        goto fail;
    }
    uint8_t* comp3 = malloc(comp_cap);
    if (!comp3) goto fail;
    const int64_t c3 = zxc_compress(src, src_sz, comp3, comp_cap, ZXC_LEVEL_DEFAULT);
    const int64_t c0 = zxc_compress(src, src_sz, comp, comp_cap, 0);
    const int same = c3 > 0 && c0 == c3 && memcmp(comp, comp3, (size_t)c3) == 0;
    free(comp3);
    if (!same) {
        printf("  [FAIL] zxc_compress(level=0) must mean the default (c3=%lld c0=%lld)\n",
               (long long)c3, (long long)c0);
        goto fail;
    }
    printf("  [PASS] out-of-range levels refused by one-shot and setter, 0 is the default\n");

    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(src);
    free(comp);
    free(dec);
    printf("PASS\n\n");
    return 1;

fail:
    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(src);
    free(comp);
    free(dec);
    return 0;
}

static void noop_progress(uint64_t done, uint64_t total, const void* user) {
    (void)done;
    (void)total;
    (void)user;
}

/* One compression parameter: identifier, bounds and default. */
typedef struct {
    zxc_cparam_t p;
    const char* name;
    int64_t lo, hi, def;
} cparam_spec_t;

/* The cctx parameter contract: bounds, defaults, set/get round trips, 0 as
 * the default, refusals that name the parameter and leave its value alone,
 * NULL arguments, the dictionary and callback setters, reset. */
int test_cctx_param_contract(void) {
    printf("=== TEST: Context API - cctx parameter contract ===\n");
    static const cparam_spec_t tab[] = {
        {ZXC_C_LEVEL, "level", ZXC_LEVEL_FASTEST, ZXC_LEVEL_ULTRA, ZXC_LEVEL_DEFAULT},
        {ZXC_C_BLOCK_SIZE, "block_size", ZXC_BLOCK_SIZE_MIN, ZXC_BLOCK_SIZE_MAX,
         ZXC_BLOCK_SIZE_DEFAULT},
        {ZXC_C_THREADS, "threads", 0, ZXC_MAX_THREADS, 0},
        {ZXC_C_CHECKSUM, "checksum", 0, 1, 0},
        {ZXC_C_SEEKABLE, "seekable", 0, 1, 0},
    };
    const size_t n_tab = sizeof(tab) / sizeof(tab[0]);
    zxc_cctx* const c = zxc_create_cctx();
    if (!c) {
        printf("  [FAIL] zxc_create_cctx\n");
        return 0;
    }
    int fails = 0;

    for (size_t i = 0; i < n_tab; i++) {
        const cparam_spec_t* t = &tab[i];
        int64_t lo = -1, hi = -1, v = -1;
        if (zxc_cparam_bounds(t->p, &lo, &hi) != ZXC_OK || lo != t->lo || hi != t->hi ||
            zxc_cparam_bounds(t->p, NULL, NULL) != ZXC_OK) {
            printf("  [FAIL] %s: bounds [%lld, %lld], want [%lld, %lld]\n", t->name, (long long)lo,
                   (long long)hi, (long long)t->lo, (long long)t->hi);
            fails++;
        }
        if (zxc_cctx_get_param(c, t->p, &v) != ZXC_OK || v != t->def) {
            printf("  [FAIL] %s: fresh context reads %lld, want %lld\n", t->name, (long long)v,
                   (long long)t->def);
            fails++;
        }
        int64_t got_lo = -1, got_hi = -1, got_0 = -1;
        if (zxc_cctx_set_param(c, t->p, t->lo) != ZXC_OK ||
            zxc_cctx_get_param(c, t->p, &got_lo) != ZXC_OK || got_lo != t->lo ||
            zxc_cctx_set_param(c, t->p, t->hi) != ZXC_OK ||
            zxc_cctx_get_param(c, t->p, &got_hi) != ZXC_OK || got_hi != t->hi) {
            printf("  [FAIL] %s: round trip at the bounds read %lld / %lld\n", t->name,
                   (long long)got_lo, (long long)got_hi);
            fails++;
        }
        if (zxc_cctx_set_param(c, t->p, 0) != ZXC_OK ||
            zxc_cctx_get_param(c, t->p, &got_0) != ZXC_OK || got_0 != t->def) {
            printf("  [FAIL] %s: 0 must restore the default %lld, reads %lld\n", t->name,
                   (long long)t->def, (long long)got_0);
            fails++;
        }
    }
    if (!fails) printf("  [PASS] bounds, defaults, round trips, 0 = default\n");

    /* Refusals: the code names the parameter, the value in force stays. */
    static const struct {
        zxc_cparam_t p;
        int64_t v;
        int want;
    } bad[] = {
        {ZXC_C_LEVEL, ZXC_LEVEL_ULTRA + 1, ZXC_ERROR_BAD_LEVEL},
        {ZXC_C_LEVEL, 99, ZXC_ERROR_BAD_LEVEL},
        {ZXC_C_LEVEL, -5, ZXC_ERROR_BAD_LEVEL},
        {ZXC_C_BLOCK_SIZE, 10000, ZXC_ERROR_BAD_BLOCK_SIZE}, /* in range, not a power of 2 */
        {ZXC_C_BLOCK_SIZE, ZXC_BLOCK_SIZE_MIN / 2, ZXC_ERROR_BAD_BLOCK_SIZE},
        {ZXC_C_BLOCK_SIZE, 2 * (int64_t)ZXC_BLOCK_SIZE_MAX, ZXC_ERROR_BAD_BLOCK_SIZE},
        {ZXC_C_BLOCK_SIZE, -4096, ZXC_ERROR_BAD_BLOCK_SIZE},
        {ZXC_C_THREADS, ZXC_MAX_THREADS + 1, ZXC_ERROR_BAD_PARAM},
        {ZXC_C_THREADS, -1, ZXC_ERROR_BAD_PARAM},
        {ZXC_C_CHECKSUM, 2, ZXC_ERROR_BAD_PARAM},
        {ZXC_C_CHECKSUM, -1, ZXC_ERROR_BAD_PARAM},
        {ZXC_C_SEEKABLE, 2, ZXC_ERROR_BAD_PARAM},
        {(zxc_cparam_t)0, 1, ZXC_ERROR_BAD_PARAM},
        {(zxc_cparam_t)6, 0, ZXC_ERROR_BAD_PARAM},
        {(zxc_cparam_t)99, 1, ZXC_ERROR_BAD_PARAM},
    };
    /* Non-default values first, so a refusal that resets would show. */
    static const int64_t custom[] = {5, 65536, 2, 1, 1};
    for (size_t i = 0; i < n_tab; i++) {
        if (zxc_cctx_set_param(c, tab[i].p, custom[i]) != ZXC_OK) {
            printf("  [FAIL] %s: custom value %lld refused\n", tab[i].name, (long long)custom[i]);
            fails++;
        }
    }
    int refused = 0;
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        int64_t before = -1, after = -1;
        const int known = zxc_cctx_get_param(c, bad[k].p, &before) == ZXC_OK;
        const int rc = zxc_cctx_set_param(c, bad[k].p, bad[k].v);
        if (known) zxc_cctx_get_param(c, bad[k].p, &after);
        if (rc != bad[k].want || (known && after != before)) {
            printf("  [FAIL] set(param %d, %lld): %d, want %d; value %lld -> %lld\n", (int)bad[k].p,
                   (long long)bad[k].v, rc, bad[k].want, (long long)before, (long long)after);
            fails++;
        } else {
            refused++;
        }
    }
    int64_t lo = 0, hi = 0, v = 0;
    if (zxc_cctx_get_param(c, (zxc_cparam_t)99, &v) != ZXC_ERROR_BAD_PARAM ||
        zxc_cparam_bounds((zxc_cparam_t)99, &lo, &hi) != ZXC_ERROR_BAD_PARAM ||
        zxc_cparam_bounds((zxc_cparam_t)0, &lo, &hi) != ZXC_ERROR_BAD_PARAM) {
        printf("  [FAIL] unknown parameter on get / bounds must be ZXC_ERROR_BAD_PARAM\n");
        fails++;
    }
    if (!fails) printf("  [PASS] %d refusals leave the value in force\n", refused);

    /* NULL arguments. */
    static uint8_t dict[1024];
    memset(dict, 'd', sizeof(dict));
    int marker = 0;
    if (zxc_cctx_set_param(NULL, ZXC_C_LEVEL, 1) != ZXC_ERROR_NULL_INPUT ||
        zxc_cctx_get_param(NULL, ZXC_C_LEVEL, &v) != ZXC_ERROR_NULL_INPUT ||
        zxc_cctx_get_param(c, ZXC_C_LEVEL, NULL) != ZXC_ERROR_NULL_INPUT ||
        zxc_cctx_set_dict(NULL, dict, sizeof(dict), NULL) != ZXC_ERROR_NULL_INPUT ||
        zxc_cctx_set_progress(NULL, noop_progress, &marker) != ZXC_ERROR_NULL_INPUT ||
        zxc_cctx_reset(NULL) != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] NULL arguments must be ZXC_ERROR_NULL_INPUT\n");
        fails++;
    }

    /* Dictionary and callback setters; an oversized dictionary is refused and
     * the attached one stays. */
    const zxc_cparams_t* const p = zxc_cctx_params(c);
    if (zxc_cctx_set_dict(c, dict, sizeof(dict), NULL) != ZXC_OK || p->dict != dict ||
        p->dict_size != sizeof(dict) ||
        zxc_cctx_set_dict(c, dict, (size_t)ZXC_DICT_SIZE_MAX + 1, NULL) !=
            ZXC_ERROR_DICT_TOO_LARGE ||
        p->dict != dict || p->dict_size != sizeof(dict)) {
        printf("  [FAIL] dictionary setter: attach %p/%zu after an oversized refusal\n", p->dict,
               p->dict_size);
        fails++;
    }
    if (zxc_cctx_set_progress(c, noop_progress, &marker) != ZXC_OK ||
        p->progress_cb != noop_progress || p->user_data != &marker ||
        zxc_cctx_set_progress(c, NULL, &marker) != ZXC_OK || p->progress_cb != NULL ||
        p->user_data != NULL || zxc_cctx_set_progress(c, noop_progress, &marker) != ZXC_OK) {
        printf("  [FAIL] progress setter\n");
        fails++;
    }
    if (zxc_cctx_set_dict(c, NULL, 0, NULL) != ZXC_OK || p->dict != NULL || p->dict_size != 0 ||
        zxc_cctx_set_dict(c, dict, sizeof(dict), NULL) != ZXC_OK) {
        printf("  [FAIL] NULL dictionary must detach\n");
        fails++;
    }
    if (!fails) printf("  [PASS] NULL arguments, dictionary and callback setters\n");

    /* Reset: every parameter back to its default, dictionary and callback
     * detached, and the next archive is the level-3 one-shot's. */
    if (zxc_cctx_reset(c) != ZXC_OK) {
        printf("  [FAIL] zxc_cctx_reset\n");
        fails++;
    }
    for (size_t i = 0; i < n_tab; i++) {
        if (zxc_cctx_get_param(c, tab[i].p, &v) != ZXC_OK || v != tab[i].def) {
            printf("  [FAIL] %s after reset: %lld, want %lld\n", tab[i].name, (long long)v,
                   (long long)tab[i].def);
            fails++;
        }
    }
    if (p->dict != NULL || p->dict_size != 0 || p->dict_huf != NULL || p->progress_cb != NULL ||
        p->user_data != NULL) {
        printf("  [FAIL] reset must detach the dictionary and the callback\n");
        fails++;
    }
    static const char text[] = "a reset context writes what the default one-shot writes";
    uint8_t a[256], b[256];
    const int64_t na = zxc_compress_cctx(c, text, sizeof(text) - 1, a, sizeof(a));
    const int64_t nb = zxc_compress(text, sizeof(text) - 1, b, sizeof(b), ZXC_LEVEL_DEFAULT);
    if (na <= 0 || nb != na || memcmp(a, b, (size_t)na) != 0 ||
        zxc_get_dict_id(a, (size_t)na) != 0) {
        printf("  [FAIL] after reset: context %lld, one-shot %lld\n", (long long)na, (long long)nb);
        fails++;
    }
    if (!fails) printf("  [PASS] reset restores the defaults and detaches dict and callback\n");

    zxc_free_cctx(c);
    if (fails) return 0;
    printf("PASS\n\n");
    return 1;
}

/* The dctx parameter contract, and what it changes: verification of a
 * checksummed archive, the dictionary an archive is bound to. */
int test_dctx_param_contract(void) {
    printf("=== TEST: Context API - dctx parameter contract ===\n");
    static const struct {
        zxc_dparam_t p;
        const char* name;
        int64_t lo, hi;
    } tab[] = {
        {ZXC_D_THREADS, "threads", 0, ZXC_MAX_THREADS},
        {ZXC_D_CHECKSUM, "checksum", 0, 1},
    };
    const size_t n_tab = sizeof(tab) / sizeof(tab[0]);
    zxc_dctx* const d = zxc_create_dctx();
    if (!d) {
        printf("  [FAIL] zxc_create_dctx\n");
        return 0;
    }
    int fails = 0;

    for (size_t i = 0; i < n_tab; i++) {
        int64_t lo = -1, hi = -1, v = -1, got_lo = -1, got_hi = -1, got_0 = -1;
        if (zxc_dparam_bounds(tab[i].p, &lo, &hi) != ZXC_OK || lo != tab[i].lo || hi != tab[i].hi ||
            zxc_dparam_bounds(tab[i].p, NULL, NULL) != ZXC_OK) {
            printf("  [FAIL] %s: bounds [%lld, %lld], want [%lld, %lld]\n", tab[i].name,
                   (long long)lo, (long long)hi, (long long)tab[i].lo, (long long)tab[i].hi);
            fails++;
        }
        if (zxc_dctx_get_param(d, tab[i].p, &v) != ZXC_OK || v != 0) {
            printf("  [FAIL] %s: fresh context reads %lld, want 0\n", tab[i].name, (long long)v);
            fails++;
        }
        if (zxc_dctx_set_param(d, tab[i].p, tab[i].lo) != ZXC_OK ||
            zxc_dctx_get_param(d, tab[i].p, &got_lo) != ZXC_OK || got_lo != tab[i].lo ||
            zxc_dctx_set_param(d, tab[i].p, tab[i].hi) != ZXC_OK ||
            zxc_dctx_get_param(d, tab[i].p, &got_hi) != ZXC_OK || got_hi != tab[i].hi ||
            zxc_dctx_set_param(d, tab[i].p, 0) != ZXC_OK ||
            zxc_dctx_get_param(d, tab[i].p, &got_0) != ZXC_OK || got_0 != 0) {
            printf("  [FAIL] %s: round trip reads %lld / %lld, after 0: %lld\n", tab[i].name,
                   (long long)got_lo, (long long)got_hi, (long long)got_0);
            fails++;
        }
    }
    static const struct {
        zxc_dparam_t p;
        int64_t v;
    } bad[] = {
        {ZXC_D_THREADS, ZXC_MAX_THREADS + 1},
        {ZXC_D_THREADS, -1},
        {ZXC_D_CHECKSUM, 2},
        {ZXC_D_CHECKSUM, -1},
        {(zxc_dparam_t)0, 1},
        {(zxc_dparam_t)3, 1},
        {(zxc_dparam_t)99, 0},
    };
    if (zxc_dctx_set_param(d, ZXC_D_THREADS, 3) != ZXC_OK ||
        zxc_dctx_set_param(d, ZXC_D_CHECKSUM, 1) != ZXC_OK) {
        printf("  [FAIL] custom values refused\n");
        fails++;
    }
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) {
        int64_t before = -1, after = -1;
        const int known = zxc_dctx_get_param(d, bad[k].p, &before) == ZXC_OK;
        const int rc = zxc_dctx_set_param(d, bad[k].p, bad[k].v);
        if (known) zxc_dctx_get_param(d, bad[k].p, &after);
        if (rc != ZXC_ERROR_BAD_PARAM || (known && after != before)) {
            printf("  [FAIL] set(param %d, %lld): %d, want %d; value %lld -> %lld\n", (int)bad[k].p,
                   (long long)bad[k].v, rc, ZXC_ERROR_BAD_PARAM, (long long)before,
                   (long long)after);
            fails++;
        }
    }
    int64_t lo = 0, hi = 0, v = 0;
    int marker = 0;
    static uint8_t dict[2048];
    memset(dict, 'q', sizeof(dict));
    if (zxc_dctx_get_param(d, (zxc_dparam_t)99, &v) != ZXC_ERROR_BAD_PARAM ||
        zxc_dparam_bounds((zxc_dparam_t)99, &lo, &hi) != ZXC_ERROR_BAD_PARAM ||
        zxc_dctx_set_param(NULL, ZXC_D_CHECKSUM, 1) != ZXC_ERROR_NULL_INPUT ||
        zxc_dctx_get_param(NULL, ZXC_D_CHECKSUM, &v) != ZXC_ERROR_NULL_INPUT ||
        zxc_dctx_get_param(d, ZXC_D_CHECKSUM, NULL) != ZXC_ERROR_NULL_INPUT ||
        zxc_dctx_set_dict(NULL, dict, sizeof(dict), NULL) != ZXC_ERROR_NULL_INPUT ||
        zxc_dctx_set_progress(NULL, noop_progress, &marker) != ZXC_ERROR_NULL_INPUT ||
        zxc_dctx_reset(NULL) != ZXC_ERROR_NULL_INPUT) {
        printf("  [FAIL] unknown parameter / NULL arguments\n");
        fails++;
    }
    if (!fails) printf("  [PASS] bounds, defaults, round trips, refusals, NULL arguments\n");

    /* Dictionary and callback setters, then reset detaches both. */
    const zxc_dparams_t* const p = zxc_dctx_params(d);
    if (zxc_dctx_set_dict(d, dict, sizeof(dict), NULL) != ZXC_OK || p->dict != dict ||
        p->dict_size != sizeof(dict) ||
        zxc_dctx_set_dict(d, dict, (size_t)ZXC_DICT_SIZE_MAX + 1, NULL) !=
            ZXC_ERROR_DICT_TOO_LARGE ||
        p->dict != dict || p->dict_size != sizeof(dict) ||
        zxc_dctx_set_dict(d, NULL, 0, NULL) != ZXC_OK || p->dict != NULL || p->dict_size != 0 ||
        zxc_dctx_set_progress(d, noop_progress, &marker) != ZXC_OK ||
        p->progress_cb != noop_progress || p->user_data != &marker ||
        zxc_dctx_set_progress(d, NULL, NULL) != ZXC_OK || p->progress_cb != NULL) {
        printf("  [FAIL] dictionary / progress setters\n");
        fails++;
    }
    if (zxc_dctx_set_dict(d, dict, sizeof(dict), NULL) != ZXC_OK ||
        zxc_dctx_set_progress(d, noop_progress, &marker) != ZXC_OK ||
        zxc_dctx_set_param(d, ZXC_D_THREADS, 4) != ZXC_OK || zxc_dctx_reset(d) != ZXC_OK ||
        zxc_dctx_get_param(d, ZXC_D_THREADS, &v) != ZXC_OK || v != 0 ||
        zxc_dctx_get_param(d, ZXC_D_CHECKSUM, &v) != ZXC_OK || v != 0 || p->dict != NULL ||
        p->dict_size != 0 || p->dict_huf != NULL || p->progress_cb != NULL ||
        p->user_data != NULL) {
        printf("  [FAIL] reset must restore the defaults and detach dict and callback\n");
        fails++;
    }
    if (!fails) printf("  [PASS] setters, reset detaches the dictionary and the callback\n");

    /* What the parameters change. A flipped digest: caught with ZXC_D_CHECKSUM,
     * ignored after a reset. A dictionary-bound archive: decodes with the
     * dictionary attached, ZXC_ERROR_DICT_REQUIRED after a reset. */
    enum { N = 20000 };
    uint8_t* const src = malloc(N);
    const size_t cap = zxc_compress_bound(N);
    uint8_t* const arc = malloc(cap);
    uint8_t* const out = malloc(N);
    if (!src || !arc || !out) {
        printf("  [FAIL] malloc\n");
        fails++;
    } else {
        gen_lz_data(src, N);
        const int64_t n = test_compress_cfg(src, N, arc, cap, 3, 0, 1, 0);
        const size_t digest_at = n > 0 ? (size_t)n - test_footer_len(arc, (size_t)n) : 0;
        if (n <= 0 || digest_at == 0) {
            printf("  [FAIL] checksummed archive: %lld\n", (long long)n);
            fails++;
        } else {
            arc[digest_at] ^= 0xFF;
            const int64_t r1 = zxc_dctx_set_param(d, ZXC_D_CHECKSUM, 1) == ZXC_OK
                                   ? zxc_decompress_dctx(d, arc, (size_t)n, out, N)
                                   : ZXC_ERROR_BAD_PARAM;
            const int64_t r2 = zxc_dctx_reset(d) == ZXC_OK
                                   ? zxc_decompress_dctx(d, arc, (size_t)n, out, N)
                                   : ZXC_ERROR_NULL_INPUT;
            if (r1 != ZXC_ERROR_BAD_CHECKSUM || r2 != N || memcmp(out, src, N) != 0) {
                printf("  [FAIL] flipped digest: verified %lld, after reset %lld\n", (long long)r1,
                       (long long)r2);
                fails++;
            }
        }
        zxc_cctx* const c = zxc_create_cctx();
        int64_t dn = c ? ZXC_OK : ZXC_ERROR_MEMORY;
        if (dn == ZXC_OK) dn = zxc_cctx_set_dict(c, dict, sizeof(dict), NULL);
        if (dn == ZXC_OK) dn = zxc_compress_cctx(c, src, N, arc, cap);
        zxc_free_cctx(c);
        const int64_t r3 = dn > 0 && zxc_dctx_set_dict(d, dict, sizeof(dict), NULL) == ZXC_OK
                               ? zxc_decompress_dctx(d, arc, (size_t)dn, out, N)
                               : dn;
        const int64_t r4 = dn > 0 && zxc_dctx_reset(d) == ZXC_OK
                               ? zxc_decompress_dctx(d, arc, (size_t)dn, out, N)
                               : dn;
        if (dn <= 0 || r3 != N || memcmp(out, src, N) != 0 || r4 != ZXC_ERROR_DICT_REQUIRED) {
            printf("  [FAIL] dictionary archive %lld: with dict %lld, after reset %lld\n",
                   (long long)dn, (long long)r3, (long long)r4);
            fails++;
        }
        if (!fails) printf("  [PASS] checksum and dictionary take effect, reset drops them\n");
    }
    free(src);
    free(arc);
    free(out);
    zxc_free_dctx(d);
    if (fails) return 0;
    printf("PASS\n\n");
    return 1;
}

/* Block size declared by archive @p arc (@p n bytes); 0 if unreadable. */
static size_t frame_block_size(const uint8_t* arc, const int64_t n) {
    zxc_frame_info_t info = {0};
    if (n <= 0 || zxc_get_frame_info(arc, (size_t)n, &info, sizeof(info)) != ZXC_OK) return 0;
    return info.block_size;
}

/* What a configured context writes: with only the level set, the one-shot's
 * bytes at every level; with checksum, seek table and block size, an archive
 * that carries them all and that a fresh context configured alike reproduces;
 * a parameter changed between calls takes effect on the next one; the block
 * API remembers the block size asked for, not the carve. */
int test_cctx_configured_archives(void) {
    printf("=== TEST: Context API - configured archives ===\n");
    enum { N = 100000, BS = 4096, SRC = 3000, BIG = 600000 };
    const size_t cap = zxc_compress_bound(BIG);
    const size_t bcap = zxc_compress_block_bound(BIG);
    uint8_t* const src = malloc(BIG);
    uint8_t* const a = malloc(cap);
    uint8_t* const b = malloc(cap);
    uint8_t* const blk = malloc(bcap);
    uint8_t* const out = malloc(N);
    zxc_cctx* const c = zxc_create_cctx();
    zxc_cctx* const fresh = zxc_create_cctx();
    int ok = 0;
    if (!src || !a || !b || !blk || !out || !c || !fresh) {
        printf("  [FAIL] setup\n");
        goto done;
    }
    gen_lz_data(src, BIG);

    /* 1. Level only: the one-shot's bytes, at every level. */
    for (int lvl = ZXC_LEVEL_FASTEST; lvl <= ZXC_LEVEL_ULTRA; lvl++) {
        const int64_t na = zxc_cctx_set_param(c, ZXC_C_LEVEL, lvl) == ZXC_OK
                               ? zxc_compress_cctx(c, src, N, a, cap)
                               : ZXC_ERROR_BAD_LEVEL;
        const int64_t nb = zxc_compress(src, N, b, cap, lvl);
        if (na <= 0 || nb != na || memcmp(a, b, (size_t)na) != 0) {
            printf("  [FAIL] level %d: context %lld, one-shot %lld\n", lvl, (long long)na,
                   (long long)nb);
            goto done;
        }
    }
    printf("  [PASS] level only: the one-shot's bytes at levels 1-7\n");

    /* 2. Checksum + seek table + 4 KiB blocks at level 5: the archive carries
     *    all three, a fresh context configured alike writes the same bytes,
     *    the seekable reader opens it and a verifying decode round-trips. */
    const zxc_cparam_t ps[] = {ZXC_C_LEVEL, ZXC_C_BLOCK_SIZE, ZXC_C_CHECKSUM, ZXC_C_SEEKABLE};
    const int64_t vs[] = {5, BS, 1, 1};
    for (size_t k = 0; k < 4; k++) {
        if (zxc_cctx_set_param(c, ps[k], vs[k]) != ZXC_OK ||
            zxc_cctx_set_param(fresh, ps[k], vs[k]) != ZXC_OK) {
            printf("  [FAIL] set_param %d\n", (int)ps[k]);
            goto done;
        }
    }
    const int64_t na = zxc_compress_cctx(c, src, N, a, cap);
    const int64_t nb = zxc_compress_cctx(fresh, src, N, b, cap);
    zxc_frame_info_t fi;
    memset(&fi, 0, sizeof(fi));
    if (na <= 0 || nb != na || memcmp(a, b, (size_t)na) != 0 ||
        zxc_get_frame_info(a, (size_t)na, &fi, sizeof(fi)) != ZXC_OK || !fi.has_checksum ||
        !fi.has_seek_table || fi.block_size != BS || fi.decompressed_size != N) {
        printf("  [FAIL] configured archive: reused %lld, fresh %lld, flags %d/%d, block %u\n",
               (long long)na, (long long)nb, fi.has_checksum, fi.has_seek_table,
               (unsigned)fi.block_size);
        goto done;
    }
    zxc_seekable* const s = zxc_seekable_open(a, (size_t)na);
    const uint64_t blocks = s ? zxc_seekable_get_num_blocks(s) : 0;
    const int64_t r = s ? zxc_seekable_decompress_range(s, out, N, N / 2, 1000) : -1;
    zxc_seekable_free(s);
    if (!s || blocks != (N + BS - 1) / BS || r != 1000 || memcmp(out, src + N / 2, 1000) != 0 ||
        test_decompress_verified(a, (size_t)na, out, N) != N || memcmp(out, src, N) != 0) {
        printf("  [FAIL] configured archive: %llu blocks, range %lld\n", (unsigned long long)blocks,
               (long long)r);
        goto done;
    }
    printf("  [PASS] checksum + seek table + 4 KiB blocks: carried, reproducible, readable\n");

    /* 3. A parameter changed between calls takes effect on the next call: the
     *    seek table goes, the rest stays. */
    const int64_t nc = zxc_cctx_set_param(c, ZXC_C_SEEKABLE, 0) == ZXC_OK
                           ? zxc_compress_cctx(c, src, N, a, cap)
                           : ZXC_ERROR_BAD_PARAM;
    const int64_t nd = test_compress_cfg(src, N, b, cap, 5, BS, 1, 0);
    if (nc <= 0 || nd != nc || memcmp(a, b, (size_t)nc) != 0 ||
        (a[6] & ZXC_FILE_FLAG_HAS_SEEK_TABLE) || !(a[6] & ZXC_FILE_FLAG_HAS_CHECKSUM)) {
        printf("  [FAIL] seekable off: %lld, fresh %lld, flags 0x%02x\n", (long long)nc,
               (long long)nd, a[6]);
        goto done;
    }
    printf("  [PASS] a parameter changed between calls applies to the next call\n");

    /* 4. Block API on a 4 KiB context: a 600 KB block is carved at 1 MiB, the
     *    next frame still declares 4 KiB, and a small block then encodes in
     *    that carve as on a fresh context. */
    if (zxc_cctx_reset(c) != ZXC_OK || zxc_cctx_reset(fresh) != ZXC_OK ||
        zxc_cctx_set_param(c, ZXC_C_LEVEL, ZXC_LEVEL_FASTEST) != ZXC_OK ||
        zxc_cctx_set_param(c, ZXC_C_BLOCK_SIZE, BS) != ZXC_OK ||
        zxc_cctx_set_param(fresh, ZXC_C_LEVEL, ZXC_LEVEL_FASTEST) != ZXC_OK ||
        zxc_cctx_set_param(fresh, ZXC_C_BLOCK_SIZE, BS) != ZXC_OK) {
        printf("  [FAIL] reconfigure for the block API\n");
        goto done;
    }
    int64_t n = zxc_compress_block(c, src, BIG, blk, bcap);
    if (n > 0) n = zxc_compress_cctx(c, src, SRC, a, cap);
    if (frame_block_size(a, n) != BS) {
        printf("  [FAIL] 600 KB block then frame: %lld, declares %zu, want %d\n", (long long)n,
               frame_block_size(a, n), (int)BS);
        goto done;
    }
    n = zxc_compress_block(c, src, SRC, blk, bcap);
    const int64_t f = zxc_compress_block(fresh, src, SRC, b, cap);
    if (n <= 0 || n != f || memcmp(blk, b, (size_t)n) != 0) {
        printf("  [FAIL] block in a 1 MiB carve: %lld, fresh context %lld\n", (long long)n,
               (long long)f);
        goto done;
    }
    printf("  [PASS] block API: block size remembered as asked, larger carve reused\n");
    ok = 1;
    printf("PASS\n\n");

done:
    zxc_free_cctx(c);
    zxc_free_cctx(fresh);
    free(src);
    free(a);
    free(b);
    free(blk);
    free(out);
    return ok;
}

int test_estimate_cctx_size(void) {
    printf("=== TEST: Unit - zxc_estimate_cctx_size ===\n");

    const int LVL = 3;

    /* 1. Zero input returns zero. */
    if (zxc_estimate_cctx_size(0, LVL) != 0) {
        printf("  [FAIL] estimate(0) must return 0\n");
        return 0;
    }
    printf("  [PASS] estimate(0) == 0\n");

    /* 2. Non-zero input returns non-zero estimate. */
    const uint64_t e1k = zxc_estimate_cctx_size(1024, LVL);
    if (e1k == 0) {
        printf("  [FAIL] estimate(1 KiB) must be > 0\n");
        return 0;
    }
    printf("  [PASS] estimate(1 KiB) = %llu bytes\n", (unsigned long long)e1k);

    /* 3. Sizes below ZXC_BLOCK_SIZE_MIN collapse to the same estimate. */
    if (zxc_estimate_cctx_size(512, LVL) != e1k || zxc_estimate_cctx_size(4096, LVL) != e1k) {
        printf("  [FAIL] estimates below MIN must round to ZXC_BLOCK_SIZE_MIN\n");
        return 0;
    }
    printf("  [PASS] estimate rounds sub-MIN inputs to the same value\n");

    /* 4. Monotonic: estimate grows with src_size across block_size tiers. */
    const uint64_t e64k = zxc_estimate_cctx_size(64 * 1024, LVL);
    const uint64_t e1m = zxc_estimate_cctx_size(1024 * 1024, LVL);
    const uint64_t e8m = zxc_estimate_cctx_size(8 * 1024 * 1024, LVL);
    if (!(e1k <= e64k && e64k <= e1m && e1m <= e8m)) {
        printf("  [FAIL] estimates must be monotonic: %llu, %llu, %llu, %llu\n",
               (unsigned long long)e1k, (unsigned long long)e64k, (unsigned long long)e1m,
               (unsigned long long)e8m);
        return 0;
    }
    printf("  [PASS] monotonic: 1K=%llu, 64K=%llu, 1M=%llu, 8M=%llu\n", (unsigned long long)e1k,
           (unsigned long long)e64k, (unsigned long long)e1m, (unsigned long long)e8m);

    /* 5. Sanity: estimate for a large block must exceed the block itself. */
    if (e1m < 1024 * 1024) {
        printf("  [FAIL] estimate(1 MiB)=%llu should exceed 1 MiB\n", (unsigned long long)e1m);
        return 0;
    }
    printf("  [PASS] estimate exceeds raw block size (context overhead present)\n");

    /* 6. Sub-linear scaling: doubling src_size must roughly double the estimate,
     *    bounded above by 10x factor (chain 2x, everything else 1x). */
    if (e8m < 4 * e1m || e8m > 12 * e1m) {
        printf("  [FAIL] 8x src_size yields %.2fx memory (expected ~8x)\n",
               (double)e8m / (double)e1m);
        return 0;
    }
    printf("  [PASS] scaling: 8x src_size -> %.2fx memory\n", (double)e8m / (double)e1m);

    /* 7. Level 6 includes the optimal-parser scratch peak (~18 x chunk_size)
     *    on top of the persistent cctx, so it must exceed the level-3 figure
     *    by at least one chunk_size worth of bytes. */
    const uint64_t e1m_l3 = zxc_estimate_cctx_size(1024 * 1024, 3);
    const uint64_t e1m_l6 = zxc_estimate_cctx_size(1024 * 1024, 6);
    if (e1m_l6 <= e1m_l3 + (1024 * 1024)) {
        printf("  [FAIL] level 6 must add optimal-parser scratch: l3=%llu l6=%llu\n",
               (unsigned long long)e1m_l3, (unsigned long long)e1m_l6);
        return 0;
    }
    printf("  [PASS] level 6 vs level 3 at 1 MiB: l3=%llu l6=%llu (delta=%llu)\n",
           (unsigned long long)e1m_l3, (unsigned long long)e1m_l6,
           (unsigned long long)(e1m_l6 - e1m_l3));

    printf("PASS\n\n");
    return 1;
}

/* A seek table sits between the EOF block and the footer. The one-shot reads
 * the footer from the end of the archive; the context path used to read it
 * straight after the EOF block and parse the seek table as the footer, so it
 * refused archives zxc_decompress() accepts. */
int test_context_api_seekable_frame(void) {
    printf("=== TEST: Context API - seekable archives decode through a context ===\n");
    static uint8_t body[16384], arc[24576], out[16384];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)('a' + (i % 23));

    const int64_t n = test_compress_cfg(body, sizeof(body), arc, sizeof(arc), 3, 4096, 0, 1);
    if (n <= 0) {
        printf("  [FAIL] seekable compress returned %lld\n", (long long)n);
        return 0;
    }
    zxc_dctx* dctx = zxc_create_dctx();
    if (!dctx) {
        printf("  [FAIL] zxc_create_dctx\n");
        return 0;
    }
    const int64_t d1 = zxc_decompress(arc, (size_t)n, out, sizeof(out));
    memset(out, 0, sizeof(out));
    const int64_t d2 = zxc_decompress_dctx(dctx, arc, (size_t)n, out, sizeof(out));
    const int same = (d2 == (int64_t)sizeof(body)) && memcmp(out, body, sizeof(body)) == 0;
    const int64_t p1 = zxc_decompress(arc, (size_t)n, NULL, 0);
    const int64_t p2 = zxc_decompress_dctx(dctx, arc, (size_t)n, NULL, 0);
    zxc_free_dctx(dctx);

    if (d1 != (int64_t)sizeof(body) || !same || p1 != ZXC_ERROR_DST_TOO_SMALL || p1 != p2) {
        printf("  [FAIL] one-shot %lld, context %lld (bytes %s), probes %lld %lld\n", (long long)d1,
               (long long)d2, same ? "ok" : "differ", (long long)p1, (long long)p2);
        return 0;
    }
    printf("  [PASS] seek table between EOF and footer: both entry points agree\n");
    printf("PASS\n\n");
    return 1;
}

/* A seekable configuration on @p c: 4 KiB blocks, @p level, @p checksum, and
 * @p dict (NULL detaches). ZXC_OK or the first refusal. */
static int configure_seekable(zxc_cctx* c, const int level, const int checksum, const void* dict,
                              const size_t dict_size) {
    int rc = zxc_cctx_set_param(c, ZXC_C_LEVEL, level);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_BLOCK_SIZE, 4096);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_CHECKSUM, checksum);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_SEEKABLE, 1);
    if (rc == ZXC_OK) rc = zxc_cctx_set_dict(c, dict, dict_size, NULL);
    return rc;
}

/* @p cctx, as its caller configured it, and a fresh context configured with
 * (@p level, @p checksum, @p dict) must write the same bytes, which must open
 * as seekable and serve a range across blocks. */
static int cctx_seekable_matches(const char* what, zxc_cctx* cctx, const uint8_t* src,
                                 const size_t n, const int level, const int checksum,
                                 const void* dict, const size_t dict_size) {
    const size_t cap = zxc_compress_bound(n);
    uint8_t* const a = malloc(cap);
    uint8_t* const b = malloc(cap);
    uint8_t* const out = malloc(n + 1);
    zxc_cctx* const ref = zxc_create_cctx();
    int ok = 0;
    if (!a || !b || !out || !ref) {
        printf("  [FAIL] %s: malloc\n", what);
        goto done;
    }
    const int64_t na = configure_seekable(ref, level, checksum, dict, dict_size) == ZXC_OK
                           ? zxc_compress_cctx(ref, src, n, a, cap)
                           : ZXC_ERROR_BAD_PARAM;
    const int64_t nb = zxc_compress_cctx(cctx, src, n, b, cap);
    if (na <= 0 || nb != na || memcmp(a, b, (size_t)na) != 0) {
        printf("  [FAIL] %s: fresh context %lld, context under test %lld\n", what, (long long)na,
               (long long)nb);
        goto done;
    }
    zxc_seekable* const s = zxc_seekable_open(b, (size_t)nb);
    if (!s) {
        printf("  [FAIL] %s: zxc_seekable_open refused the context's archive\n", what);
        goto done;
    }
    if (dict) {
        const int rc = zxc_seekable_set_dict(s, dict, dict_size, NULL);
        if (rc != ZXC_OK) {
            printf("  [FAIL] %s: zxc_seekable_set_dict returned %d\n", what, rc);
            zxc_seekable_free(s);
            goto done;
        }
    }
    const size_t off = n / 3, len = n / 3;
    const int64_t r = len ? zxc_seekable_decompress_range(s, out, n, off, len) : 0;
    zxc_seekable_free(s);
    if (r != (int64_t)len || memcmp(out, src + off, len) != 0) {
        printf("  [FAIL] %s: range [%zu, +%zu) returned %lld\n", what, off, len, (long long)r);
        goto done;
    }
    ok = 1;
done:
    zxc_free_cctx(ref);
    free(a);
    free(b);
    free(out);
    return ok;
}

/* 1 if every data block of @p arc is RAW, 0 otherwise or on a malformed walk. */
static int all_blocks_raw(const uint8_t* arc, const size_t n, const int checksum) {
    size_t off = ZXC_FILE_HEADER_SIZE, blocks = 0;
    zxc_block_header_t bh;
    while (zxc_read_block_header(arc + off, n - off, &bh) == ZXC_OK) {
        if (bh.block_type == ZXC_BLOCK_EOF) return blocks > 0;
        if (bh.block_type != ZXC_BLOCK_RAW) return 0;
        off += ZXC_BLOCK_HEADER_SIZE + bh.comp_size + (checksum ? ZXC_BLOCK_CHECKSUM_SIZE : 0);
        blocks++;
    }
    return 0;
}

/* The context API used to drop .seekable; it now writes the table without
 * allocating, static contexts included. */
int test_context_api_seekable_compress(void) {
    printf("=== TEST: Context API - seekable compression across configurations ===\n");
    /* 74 blocks of 4 KB: two seek-table groups, the second one partial. */
    const size_t n = 74 * 4096 - 123;
    uint8_t* const src = malloc(n);
    static uint8_t dict[2048];
    if (!src) {
        printf("  [FAIL] malloc\n");
        return 0;
    }
    uint32_t x = 12345;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245U + 12345U;
        src[i] = (uint8_t)((i % 97 < 60) ? 'a' + (i % 13) : (x >> 24));
    }
    for (size_t i = 0; i < sizeof(dict); i++) dict[i] = (uint8_t)('a' + (i % 13));

    int fails = 0;
    zxc_cctx* cctx = zxc_create_cctx();
    if (!cctx) {
        printf("  [FAIL] zxc_create_cctx\n");
        free(src);
        return 0;
    }
    /* One context reconfigured between calls: nothing leaks from one
     * configuration into the next. */
    fails += configure_seekable(cctx, 3, 0, NULL, 0) != ZXC_OK;
    fails += !cctx_seekable_matches("level 3", cctx, src, n, 3, 0, NULL, 0);
    fails += configure_seekable(cctx, 5, 1, NULL, 0) != ZXC_OK;
    fails += !cctx_seekable_matches("checksums", cctx, src, n, 5, 1, NULL, 0);
    fails += !cctx_seekable_matches("reused, unchanged", cctx, src, n, 5, 1, NULL, 0);
    fails += configure_seekable(cctx, 5, 1, dict, sizeof(dict)) != ZXC_OK;
    fails += !cctx_seekable_matches("dictionary", cctx, src, n, 5, 1, dict, sizeof(dict));
    fails += configure_seekable(cctx, 3, 0, NULL, 0) != ZXC_OK;
    fails += !cctx_seekable_matches("dictionary detached", cctx, src, n, 3, 0, NULL, 0);
    fails += !cctx_seekable_matches("one block", cctx, src, 1000, 3, 0, NULL, 0);
    fails += configure_seekable(cctx, 5, 1, NULL, 0) != ZXC_OK;
    fails += !cctx_seekable_matches("empty input", cctx, src, 0, 5, 1, NULL, 0);

    /* Incompressible input: every data block is RAW, whose comp_size the table
     * reads back like any other. */
    {
        const size_t rn = 5 * 4096 + 77;
        const size_t rcap = zxc_compress_bound(rn);
        uint8_t* const noise = malloc(rn);
        uint8_t* const rarc = malloc(rcap);
        if (noise && rarc) {
            zxc_test_srand(0x5EEDU);
            for (size_t i = 0; i < rn; i++) noise[i] = (uint8_t)zxc_test_rand();
            const int64_t rl = zxc_compress_cctx(cctx, noise, rn, rarc, rcap);
            if (rl <= 0 || !all_blocks_raw(rarc, (size_t)rl, 1)) {
                printf("  [FAIL] incompressible fixture is not all RAW (%lld)\n", (long long)rl);
                fails++;
            }
            fails += !cctx_seekable_matches("all RAW, checksums", cctx, noise, rn, 5, 1, NULL, 0);
        } else {
            fails++;
        }
        free(noise);
        free(rarc);
    }
    zxc_free_cctx(cctx);

    /* A static context cannot allocate: the table must still be written. */
    const size_t ws_size = zxc_static_cctx_workspace_size(4096, 3);
    void* const ws = test_aligned_alloc(64, ws_size);
    cctx = ws ? zxc_init_static_cctx(ws, ws_size, 4096, 3) : NULL;
    fails += !cctx || zxc_cctx_set_param(cctx, ZXC_C_SEEKABLE, 1) != ZXC_OK ||
             !cctx_seekable_matches("static context", cctx, src, n, 3, 0, NULL, 0);

    /* A cut through the seek table or the footer: heap and static contexts
     * refuse it alike. */
    zxc_cctx* const heap = zxc_create_cctx();
    if (cctx && heap && configure_seekable(heap, 3, 0, NULL, 0) == ZXC_OK) {
        const size_t cap = zxc_compress_bound(n);
        uint8_t* const arc = malloc(cap);
        const int64_t full = arc ? zxc_compress_cctx(heap, src, n, arc, cap) : -1;
        const size_t table = zxc_seek_table_size(74);
        const size_t fl = full > 0 ? test_footer_len(arc, (size_t)full) : 0;
        const size_t cuts[] = {1, fl, fl + 1, fl + table - 1, fl + table};
        for (size_t i = 0; full > 0 && i < sizeof(cuts) / sizeof(cuts[0]); i++) {
            const size_t c = (size_t)full - cuts[i];
            const int64_t r1 = zxc_compress_cctx(heap, src, n, arc, c);
            const int64_t r2 = zxc_compress_cctx(cctx, src, n, arc, c);
            if (r1 != ZXC_ERROR_DST_TOO_SMALL || r2 != r1) {
                printf("  [FAIL] capacity %zu (archive %lld): heap %lld, static %lld\n", c,
                       (long long)full, (long long)r1, (long long)r2);
                fails++;
            }
        }
        if (full <= 0) fails++;
        free(arc);
    } else {
        fails++;
    }
    zxc_free_cctx(heap);
    test_aligned_free(ws);
    free(src);

    if (fails) return 0;
    printf("  [PASS] heap, reused, dictionary, empty and static contexts\n");
    printf("PASS\n\n");
    return 1;
}

/* Pins both halves of the no-destination contract: what the probe answers,
 * and what a real decode of the same bytes answers. They agree on every
 * archive that reports an empty payload. On one that stores data the probe
 * decodes nothing and can only say "give me a buffer", so the two verdicts are
 * pinned separately rather than assumed equal. A plain configuration (no
 * checksum, no dictionary) goes through both entry points, which must agree;
 * the one-shot has no checksum verification and no dictionary, so a configured
 * decode goes through the context alone. */
static int probe_and_decode(const char* what, const uint8_t* arc, const size_t n,
                            const int checksum, const void* dict, const size_t dict_size,
                            const int64_t want_probe, const int64_t want_decode) {
    static uint8_t out[8192];
    zxc_dctx* dctx = zxc_create_dctx();
    if (!dctx || zxc_dctx_set_param(dctx, ZXC_D_CHECKSUM, checksum) != ZXC_OK ||
        zxc_dctx_set_dict(dctx, dict, dict_size, NULL) != ZXC_OK) {
        printf("  [FAIL] %s: dctx setup\n", what);
        zxc_free_dctx(dctx);
        return 0;
    }
    const int plain = !checksum && !dict;
    const int64_t p2 = zxc_decompress_dctx(dctx, arc, n, NULL, 0);
    const int64_t d2 = zxc_decompress_dctx(dctx, arc, n, out, sizeof(out));
    const int64_t p1 = plain ? zxc_decompress(arc, n, NULL, 0) : p2;
    const int64_t d1 = plain ? zxc_decompress(arc, n, out, sizeof(out)) : d2;
    zxc_free_dctx(dctx);
    if (p1 == want_probe && p2 == want_probe && d1 == want_decode && d2 == want_decode) return 1;
    printf("  [FAIL] %s: probe %lld %lld (want %lld), decode %lld %lld (want %lld)\n", what,
           (long long)p1, (long long)p2, (long long)want_probe, (long long)d1, (long long)d2,
           (long long)want_decode);
    return 0;
}

/* The FILE* writer on an empty input, configured by @p cfg: the independent
 * writer of the empty archive, read back into @p out. */
static int64_t stream_empty_archive(const zxc_cctx* cfg, uint8_t* out, const size_t cap) {
    FILE* const fi = tmpfile();
    FILE* const fo = tmpfile();
    int64_t n = (fi && fo) ? zxc_stream_compress(fi, fo, cfg) : ZXC_ERROR_IO;
    if (n > 0 && ((size_t)n > cap || fseek(fo, 0, SEEK_SET) != 0 ||
                  fread(out, 1, (size_t)n, fo) != (size_t)n))
        n = ZXC_ERROR_IO;
    if (fi) fclose(fi);
    if (fo) fclose(fo);
    return n;
}

/* An empty source is a valid frame: the reusable context must write the same
 * archive as the one-shot entry point, and stay usable afterwards. */
int test_context_api_empty_input(void) {
    printf("=== TEST: Context API - empty input matches the one-shot ===\n");
    uint8_t one_shot[64], from_ctx[64];
    zxc_cctx* cctx = zxc_create_cctx();
    int ok = 0;
    do {
        if (!cctx) {
            printf("  [FAIL] zxc_create_cctx\n");
            break;
        }
        /* Footer of an empty frame: size 0, compressed size 27, lengths byte. */
        const int64_t expected =
            ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_MIN_SIZE;
        const int64_t n1 = zxc_compress(NULL, 0, one_shot, sizeof(one_shot), 3);
        const int64_t n2 = zxc_compress_cctx(cctx, NULL, 0, from_ctx, sizeof(from_ctx));
        if (n1 != expected || n2 != n1 || memcmp(one_shot, from_ctx, (size_t)n1) != 0) {
            printf("  [FAIL] one-shot %lld, context %lld (expected %lld)\n", (long long)n1,
                   (long long)n2, (long long)expected);
            break;
        }
        /* The other accepted shape: a real pointer with a zero size. */
        uint8_t probe = 0;
        const int64_t n2b = zxc_compress_cctx(cctx, &probe, 0, from_ctx, sizeof(from_ctx));
        if (n2b != n1 || memcmp(one_shot, from_ctx, (size_t)n1) != 0) {
            printf("  [FAIL] non-NULL source with a zero size: %lld\n", (long long)n2b);
            break;
        }
        /* Every shape below is checked both ways. Verdicts are tallied rather
         * than tested one by one, so a regression shows every shape it broke. */
        const uint8_t junk[36] = {0};
        int disagreed = 0;
        disagreed += !probe_and_decode("empty archive", from_ctx, (size_t)n2, 0, NULL, 0, 0, 0);
        disagreed += !probe_and_decode("zeroed junk", junk, sizeof(junk), 0, NULL, 0,
                                       ZXC_ERROR_BAD_MAGIC, ZXC_ERROR_BAD_MAGIC);
        disagreed += !probe_and_decode("header without footer", from_ctx, ZXC_FILE_HEADER_SIZE, 0,
                                       NULL, 0, ZXC_ERROR_SRC_TOO_SMALL, ZXC_ERROR_SRC_TOO_SMALL);

        /* Crafted block headers, each rewritten through zxc_write_block_header
         * so its hash8 stays valid: a broken hash masks the shape check behind
         * it. */
        uint8_t bad[256];
        zxc_block_header_t bh;
        memcpy(bad, from_ctx, (size_t)n2);
        if (zxc_read_block_header(bad + ZXC_FILE_HEADER_SIZE, (size_t)n2 - ZXC_FILE_HEADER_SIZE,
                                  &bh) != ZXC_OK) {
            printf("  [FAIL] could not read the empty archive's EOF block\n");
            break;
        }
        const zxc_block_header_t eof = bh;

        bh.comp_size = 7; /* EOF carries no payload */
        zxc_write_block_header(bad + ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &bh);
        disagreed += !probe_and_decode("EOF with a payload size", bad, (size_t)n2, 0, NULL, 0,
                                       ZXC_ERROR_BAD_HEADER, ZXC_ERROR_BAD_HEADER);

        bh = eof;
        bh.block_type = ZXC_BLOCK_RAW; /* an empty archive stores no data block */
        zxc_write_block_header(bad + ZXC_FILE_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &bh);
        disagreed += !probe_and_decode("data block where EOF belongs", bad, (size_t)n2, 0, NULL, 0,
                                       ZXC_ERROR_BAD_HEADER, ZXC_ERROR_BAD_HEADER);

        /* A corrupted block checksum: the probe stops on capacity, the decode names it. */
        static const char ck_text[] = "a block checksum only the decode reaches";
        const int64_t cn =
            test_compress_cfg(ck_text, sizeof(ck_text) - 1, bad, sizeof(bad), 3, 0, 1, 0);
        const int64_t ck_at =
            cn > 0 ? cn - (int64_t)(test_footer_len(bad, (size_t)cn) + ZXC_BLOCK_HEADER_SIZE) - 1
                   : -1;
        if (ck_at < 0) {
            printf("  [FAIL] block+checksum setup: %lld\n", (long long)cn);
            break;
        }
        bad[ck_at] ^= 0xFF; /* its checksum */
        disagreed += !probe_and_decode("corrupted block checksum", bad, (size_t)cn, 1, NULL, 0,
                                       ZXC_ERROR_DST_TOO_SMALL, ZXC_ERROR_BAD_CHECKSUM);

        /* An unsupplied dictionary is only caught by the frame walk, so the
         * probe has to reach it. */
        static uint8_t dict[8192];
        memset(dict, 'x', sizeof(dict));
        if (zxc_cctx_set_dict(cctx, dict, sizeof(dict), NULL) != ZXC_OK) {
            printf("  [FAIL] zxc_cctx_set_dict\n");
            break;
        }
        const int64_t dn = zxc_compress_cctx(cctx, NULL, 0, bad, sizeof(bad));
        if (dn <= (int64_t)ZXC_FILE_FOOTER_MIN_SIZE) {
            printf("  [FAIL] empty+dict setup: %lld\n", (long long)dn);
            break;
        }
        disagreed += !probe_and_decode("dictionary not supplied", bad, (size_t)dn, 0, NULL, 0,
                                       ZXC_ERROR_DICT_REQUIRED, ZXC_ERROR_DICT_REQUIRED);

        /* A forged stored size is corrupt data, not "destination too small". */
        static const char payload[] = "a forged footer must read as corrupt data, not as size";
        if (zxc_cctx_set_dict(cctx, NULL, 0, NULL) != ZXC_OK) {
            printf("  [FAIL] zxc_cctx_set_dict(NULL)\n");
            break;
        }
        const int64_t fn = zxc_compress_cctx(cctx, payload, sizeof(payload) - 1, bad, sizeof(bad));
        uint8_t forged[256];
        const size_t forged_n =
            fn > 0 ? test_forge_footer_size(bad, (size_t)fn, forged, sizeof(forged), UINT64_MAX)
                   : 0;
        if (forged_n == 0) {
            printf("  [FAIL] forged footer setup: compress returned %lld\n", (long long)fn);
            break;
        }
        disagreed += !probe_and_decode("forged stored size", forged, forged_n, 0, NULL, 0,
                                       ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_CORRUPT_DATA);

        /* Archives that store data: the probe has no buffer to decode into, so
         * it reports DST_TOO_SMALL where the decode names the real fault. Both
         * sides are pinned: neither half follows from the other. */
        const int64_t pn = zxc_compress_cctx(cctx, payload, sizeof(payload) - 1, bad, sizeof(bad));
        if (pn <= (int64_t)ZXC_FILE_FOOTER_MIN_SIZE) {
            printf("  [FAIL] payload archive setup: %lld\n", (long long)pn);
            break;
        }
        disagreed += !probe_and_decode("intact payload", bad, (size_t)pn, 0, NULL, 0,
                                       ZXC_ERROR_DST_TOO_SMALL, (int64_t)(sizeof(payload) - 1));
        /* A footer claiming an empty payload the blocks contradict. */
        uint8_t zeroed[256];
        const size_t zn = test_forge_footer_size(bad, (size_t)pn, zeroed, sizeof(zeroed), 0);
        disagreed += !probe_and_decode("payload behind a zeroed size", zeroed, zn, 0, NULL, 0,
                                       ZXC_ERROR_DST_TOO_SMALL, ZXC_ERROR_CORRUPT_DATA);
        /* Same, with the dictionary the archive needs: the probe still refuses
         * on capacity, through the dictionary bounce path this time. */
        if (zxc_cctx_set_dict(cctx, dict, sizeof(dict), NULL) != ZXC_OK) {
            printf("  [FAIL] zxc_cctx_set_dict\n");
            break;
        }
        const int64_t pdn = zxc_compress_cctx(cctx, payload, sizeof(payload) - 1, bad, sizeof(bad));
        if (pdn <= (int64_t)ZXC_FILE_FOOTER_MIN_SIZE) {
            printf("  [FAIL] dict payload archive setup: %lld\n", (long long)pdn);
            break;
        }
        disagreed += !probe_and_decode("payload behind a dictionary", bad, (size_t)pdn, 0, dict,
                                       sizeof(dict), ZXC_ERROR_DST_TOO_SMALL,
                                       (int64_t)(sizeof(payload) - 1));

        /* A dictionary size the library cannot honour is a caller error: the
         * setter refuses it before any archive is read, and the context goes
         * on decoding without it. */
        zxc_dctx* nd = zxc_create_dctx();
        if (!nd) {
            printf("  [FAIL] zxc_create_dctx\n");
            break;
        }
        const int huge = zxc_dctx_set_dict(nd, dict, (size_t)ZXC_DICT_SIZE_MAX + 1, NULL);
        const int64_t after_huge = zxc_decompress_dctx(nd, from_ctx, (size_t)n2, NULL, 0);
        if (huge != ZXC_ERROR_DICT_TOO_LARGE || after_huge != 0) {
            printf("  [FAIL] oversized dictionary: setter %d, probe after %lld\n", huge,
                   (long long)after_huge);
            zxc_free_dctx(nd);
            break;
        }

        /* A NULL destination with a non-zero capacity is a caller mistake, not
         * a probe, and both entry points have to say so. */
        const int64_t z1 = zxc_decompress(from_ctx, (size_t)n2, NULL, 64);
        const int64_t z2 = zxc_decompress_dctx(nd, from_ctx, (size_t)n2, NULL, 64);
        zxc_free_dctx(nd);
        if (z1 != ZXC_ERROR_NULL_INPUT || z2 != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] NULL destination with capacity 64: %lld %lld\n", (long long)z1,
                   (long long)z2);
            break;
        }
        if (disagreed) break;
        printf("  [PASS] %lld-byte empty archive: same bytes and same codes as the one-shot\n",
               (long long)n2);

        /* The context keeps working for a normal payload. */
        static const char text[] = "the quick brown fox jumps over the lazy dog, twice over";
        const size_t len = sizeof(text) - 1;
        uint8_t comp[256], back[256];
        if (zxc_cctx_set_dict(cctx, NULL, 0, NULL) != ZXC_OK) {
            printf("  [FAIL] zxc_cctx_set_dict(NULL)\n");
            break;
        }
        const int64_t n3 = zxc_compress_cctx(cctx, text, len, comp, sizeof(comp));
        const int64_t back_len = n3 > 0 ? zxc_decompress(comp, (size_t)n3, back, sizeof(back)) : n3;
        if (back_len != (int64_t)len || memcmp(back, text, len) != 0) {
            printf("  [FAIL] next call: %lld -> %lld\n", (long long)n3, (long long)back_len);
            break;
        }
        printf("  [PASS] the context still compresses after an empty call\n");

        /* An archive with a payload is the one case where the two answers must
         * differ: nothing is wrong with it, there is just nowhere to put it. */
        if (zxc_decompress(comp, (size_t)n3, NULL, 0) != ZXC_ERROR_DST_TOO_SMALL) {
            printf("  [FAIL] probing a non-empty archive should be DST_TOO_SMALL\n");
            break;
        }

        /* The empty-source fast path skips the parameter plumbing a normal call
         * runs. Pin what it must still produce: the FILE* writer's bytes for
         * the same configuration, and a dictionary left intact for the next
         * call. */
        if (zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, 1) != ZXC_OK) {
            printf("  [FAIL] set_param(checksum)\n");
            break;
        }
        const int64_t e1 = stream_empty_archive(cctx, one_shot, sizeof(one_shot));
        const int64_t e2 = zxc_compress_cctx(cctx, NULL, 0, bad, sizeof(bad));
        if (e1 <= 0 || e2 != e1 || memcmp(one_shot, bad, (size_t)e1) != 0 ||
            !(bad[6] & ZXC_FILE_FLAG_HAS_CHECKSUM)) {
            printf("  [FAIL] empty + checksum: FILE* %lld, context %lld\n", (long long)e1,
                   (long long)e2);
            break;
        }
        if (zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, 0) != ZXC_OK ||
            zxc_cctx_set_dict(cctx, dict, sizeof(dict), NULL) != ZXC_OK) {
            printf("  [FAIL] reconfigure for empty + dictionary\n");
            break;
        }
        const int64_t e3 = stream_empty_archive(cctx, one_shot, sizeof(one_shot));
        const int64_t e4 = zxc_compress_cctx(cctx, NULL, 0, bad, sizeof(bad));
        if (e3 <= 0 || e4 != e3 || memcmp(one_shot, bad, (size_t)e3) != 0 ||
            zxc_get_dict_id(bad, (size_t)e4) == 0) {
            printf("  [FAIL] empty + dictionary: FILE* %lld, context %lld\n", (long long)e3,
                   (long long)e4);
            break;
        }
        zxc_dctx* const dd = zxc_create_dctx();
        const int64_t dc = zxc_compress_cctx(cctx, text, len, comp, sizeof(comp));
        const int64_t dr = dd && dc > 0 && zxc_dctx_set_dict(dd, dict, sizeof(dict), NULL) == ZXC_OK
                               ? zxc_decompress_dctx(dd, comp, (size_t)dc, back, sizeof(back))
                               : dc;
        zxc_free_dctx(dd);
        if (dr != (int64_t)len || memcmp(back, text, len) != 0) {
            printf("  [FAIL] dictionary lost across the empty call: %lld -> %lld\n", (long long)dc,
                   (long long)dr);
            break;
        }
        printf("  [PASS] the empty fast path keeps checksum and dictionary parameters\n");

        /* The block API keeps its documented [1, MAX] contract: a real pointer
         * with a zero size must be refused on the size alone. */
        if (zxc_compress_block(cctx, text, 0, comp, sizeof(comp)) != ZXC_ERROR_NULL_INPUT) {
            printf("  [FAIL] the block API should refuse an empty block\n");
            break;
        }
        printf("  [PASS] the block API still refuses an empty block\n");
        ok = 1;
    } while (0);
    zxc_free_cctx(cctx);
    if (ok) printf("PASS\n\n");
    return ok;
}
