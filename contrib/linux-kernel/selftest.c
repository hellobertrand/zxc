/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "zxc.h"

// One block, one frame and one in-place frame on a static context, at a fast and
// a dense level; a mismatch refuses the load.
static int __init zxc_selftest_init(void) {
    static const char msg[] = "zxc kernel smoke test, zxc kernel smoke test, zxc kernel";
    static const int levels[] = {3, 7};
    size_t cap = (size_t)zxc_compress_bound(sizeof(msg));
    size_t ws_sz = zxc_static_dctx_workspace_size(PAGE_SIZE);
    u8 *comp = kmalloc(cap, GFP_KERNEL), *back = kmalloc(sizeof(msg), GFP_KERNEL);
    void* ws = kvmalloc(ws_sz, GFP_KERNEL);
    zxc_dctx* sdctx = ws ? zxc_init_static_dctx(ws, ws_sz, PAGE_SIZE) : NULL;
    int rc = -ENOMEM, i;

    if (!comp || !back || !sdctx) goto out;
    for (i = 0; i < 2; i++) {
        zxc_compress_opts_t opts = {.level = levels[i]};
        zxc_cctx* cctx = zxc_create_cctx(&opts);
        zxc_dctx* dctx = zxc_create_dctx();
        int64_t n = -1, m = -1, fn = -1, fm = -1, pn = -1, pm = -1;

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

            // In place: the archive flush-right in one buffer, no allocation inside.
            zxc_compress_opts_t popts = {.level = levels[i], .block_size = PAGE_SIZE};
            u8* buf = NULL;
            size_t need = 0;
            pn = zxc_compress(msg, sizeof(msg), comp, cap, &popts);
            if (pn > 0) need = zxc_decompress_inplace_bound(comp, (size_t)pn);
            if (need) buf = kmalloc(need, GFP_KERNEL);
            if (buf) {
                memcpy(buf + need - (size_t)pn, comp, (size_t)pn);
                pm = zxc_decompress_inplace_dctx(sdctx, buf, need, (size_t)pn, NULL);
            }
            if (!rc && (pm != sizeof(msg) || memcmp(buf, msg, sizeof(msg)))) rc = -EINVAL;
            kfree(buf);
        }
        pr_info(
            "zxc_selftest: level %d block %lld -> %lld, frame %lld -> %lld, "
            "in place %lld -> %lld: %s\n",
            levels[i], (long long)n, (long long)m, (long long)fn, (long long)fm, (long long)pn,
            (long long)pm, rc ? "FAIL" : "ok");
        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
        if (rc) break;
    }
out:
    kfree(comp);
    kfree(back);
    kvfree(ws);
    return rc;
}

static void __exit zxc_selftest_exit(void) {}

module_init(zxc_selftest_init);
module_exit(zxc_selftest_exit);
MODULE_LICENSE("Dual BSD/GPL");
