/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ZXC - High-performance lossless compression
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/* Compression recipe of a conformance vector, and how it reaches a context.
 * A plain struct, so the case tables stay designated initialisers. */

#ifndef ZXC_CONFORMANCE_RECIPE_H
#define ZXC_CONFORMANCE_RECIPE_H

#include <stddef.h>
#include <stdint.h>

#include "../include/zxc_buffer.h"
#include "../include/zxc_error.h"

typedef struct {
    int level;
    size_t block_size;
    int checksum_enabled;
    int seekable;
} recipe_t;

/* Configures @p c from @p r. Returns ZXC_OK, or the refusing setter's error. */
static inline int recipe_apply(zxc_cctx* c, const recipe_t* r) {
    int rc = zxc_cctx_set_param(c, ZXC_C_LEVEL, r->level);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_BLOCK_SIZE, (int64_t)r->block_size);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_CHECKSUM, r->checksum_enabled);
    if (rc == ZXC_OK) rc = zxc_cctx_set_param(c, ZXC_C_SEEKABLE, r->seekable);
    return rc;
}

/* Compresses @p in with @p r and an optional dictionary into @p out.
 * Returns the compressed size, or a negative zxc_error_t. */
static inline int64_t recipe_compress(const recipe_t* r, const void* dict, size_t dict_size,
                                      const void* dict_huf, const void* in, size_t in_size,
                                      void* out, size_t cap) {
    zxc_cctx* const c = zxc_create_cctx();
    if (!c) return ZXC_ERROR_MEMORY;
    int rc = recipe_apply(c, r);
    if (rc == ZXC_OK) rc = zxc_cctx_set_dict(c, dict, dict_size, dict_huf);
    const int64_t n = rc == ZXC_OK ? zxc_compress_cctx(c, in, in_size, out, cap) : rc;
    zxc_free_cctx(c);
    return n;
}

#endif /* ZXC_CONFORMANCE_RECIPE_H */
