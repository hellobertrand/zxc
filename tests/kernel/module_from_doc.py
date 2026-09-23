#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
"""Stages the out-of-tree kernel module docs/KERNEL.md describes.

    module_from_doc.py docs/KERNEL.md <out-dir>

Kbuild lines, dependency header and shims are cut from the doc's code blocks,
so building <out-dir> tests the recipe as published. The smoke module round-trips
a block and a frame, which pulls the six translation units in.
"""
import os
import re
import shutil
import sys

UNITS = "zxc_common zxc_pivco_tables zxc_dispatch zxc_compress zxc_decompress zxc_huffman".split()

SMOKE = r"""
#include <linux/module.h>
#include <linux/slab.h>
#include "zxc.h"

static int __init zxctest_init(void)
{
	static const char msg[] = "zxc kernel smoke test, zxc kernel smoke test, zxc kernel";
	zxc_compress_opts_t opts = { .level = 3 };
	size_t cap = (size_t)zxc_compress_bound(sizeof(msg)); /* >= the block bound */
	u8 *comp = kmalloc(cap, GFP_KERNEL), *back = kmalloc(sizeof(msg), GFP_KERNEL);
	zxc_cctx *cctx = zxc_create_cctx(&opts);
	zxc_dctx *dctx = zxc_create_dctx();
	int64_t n = -1, m = -1;

	int64_t fn = -1, fm = -1;

	if (comp && back && cctx && dctx) {
		n = zxc_compress_block(cctx, msg, sizeof(msg), comp, cap, &opts);
		if (n > 0)
			m = zxc_decompress_block_safe(dctx, comp, (size_t)n, back, sizeof(msg), NULL);
		fn = zxc_compress(msg, sizeof(msg), comp, cap, &opts);
		if (fn > 0)
			fm = zxc_decompress(comp, (size_t)fn, back, sizeof(msg), NULL);
	}
	pr_info("zxctest: block %lld -> %lld, frame %lld -> %lld\n", (long long)n, (long long)m,
		(long long)fn, (long long)fm);
	zxc_free_cctx(cctx);
	zxc_free_dctx(dctx);
	kfree(comp);
	kfree(back);
	return 0;
}

static void __exit zxctest_exit(void) {}

module_init(zxctest_init);
module_exit(zxctest_exit);
MODULE_LICENSE("Dual BSD/GPL");
"""


def main(doc_path, out):
    root = os.path.dirname(os.path.dirname(os.path.abspath(doc_path)))
    blocks = re.findall(r"```(\w*)\n(.*?)```", open(doc_path).read(), re.S)
    make = [b for lang, b in blocks if lang == "make"]
    deps = [b for lang, b in blocks if lang == "c" and "#define ZXC_DEPS_H" in b]
    shims = [b for lang, b in blocks if lang == "c" and b.startswith("/* shim/")]
    if len(make) != 1 or len(deps) != 1 or not shims:
        sys.exit(f"{doc_path}: expected one make block, one zxc_deps.h block and the shims")

    shutil.rmtree(out, ignore_errors=True)
    for d in ("include", "src/lib"):
        shutil.copytree(os.path.join(root, d), os.path.join(out, "zxc", d))
    with open(os.path.join(out, "zxc/src/lib/zxc_deps.h"), "w") as f:
        f.write(deps[0])
    os.mkdir(os.path.join(out, "shim"))
    for b in shims:
        name = b.split("*/", 1)[0].replace("/*", "").strip()  # "shim/stdint.h"
        with open(os.path.join(out, name), "w") as f:
            f.write(b)
    objs = " ".join(f"zxc/src/lib/{u}.o" for u in UNITS)
    with open(os.path.join(out, "Kbuild"), "w") as f:
        f.write(f"{make[0]}\nobj-m += zxctest.o\nzxctest-y := zxctest_main.o {objs}\n")
    with open(os.path.join(out, "zxctest_main.c"), "w") as f:
        f.write(SMOKE)
    print(f"staged {out}: {len(UNITS)} units, {len(shims)} shims")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
