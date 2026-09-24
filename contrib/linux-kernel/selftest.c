/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "zxc.h"

// One block and one frame, at a fast and a dense level; a mismatch refuses the load.
static int __init zxc_selftest_init(void) {
    static const char msg[] = "zxc kernel smoke test, zxc kernel smoke test, zxc kernel";
    static const int levels[] = {3, 7};
    size_t cap = (size_t)zxc_compress_bound(sizeof(msg));
    u8 *comp = kmalloc(cap, GFP_KERNEL), *back = kmalloc(sizeof(msg), GFP_KERNEL);
    int rc = -ENOMEM, i;

    if (!comp || !back) goto out;
    for (i = 0; i < 2; i++) {
        zxc_compress_opts_t opts = {.level = levels[i]};
        zxc_cctx* cctx = zxc_create_cctx(&opts);
        zxc_dctx* dctx = zxc_create_dctx();
        int64_t n = -1, m = -1, fn = -1, fm = -1;

        rc = -ENOMEM;
        if (cctx && dctx) {
            n = zxc_compress_block(cctx, msg, sizeof(msg), comp, cap, &opts);
            if (n > 0)
                m = zxc_decompress_block_safe(dctx, comp, (size_t)n, back, sizeof(msg), NULL);
            rc = (m == sizeof(msg) && !memcmp(back, msg, sizeof(msg))) ? 0 : -EINVAL;
            memset(back, 0, sizeof(msg));
            fn = zxc_compress(msg, sizeof(msg), comp, cap, &opts);
            if (fn > 0) fm = zxc_decompress(comp, (size_t)fn, back, sizeof(msg), NULL);
            if (!rc && (fm != sizeof(msg) || memcmp(back, msg, sizeof(msg)))) rc = -EINVAL;
        }
        pr_info("zxc_selftest: level %d block %lld -> %lld, frame %lld -> %lld: %s\n", levels[i],
                (long long)n, (long long)m, (long long)fn, (long long)fm, rc ? "FAIL" : "ok");
        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
        if (rc) break;
    }
out:
    kfree(comp);
    kfree(back);
    return rc;
}

static void __exit zxc_selftest_exit(void) {}

module_init(zxc_selftest_init);
module_exit(zxc_selftest_exit);
MODULE_LICENSE("Dual BSD/GPL");
