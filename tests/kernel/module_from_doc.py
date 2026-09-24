#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
"""Stages the out-of-tree kernel module docs/KERNEL.md describes.

    module_from_doc.py docs/KERNEL.md <out-dir>

Kbuild lines, dependency header and shims are cut from the doc's code blocks,
so building <out-dir> tests the recipe as published. The smoke module round-trips
a block and a frame, which pulls the seven translation units in.
"""
import os
import re
import shutil
import sys

UNITS = "zxc_common zxc_pivco_tables zxc_dispatch zxc_dict zxc_compress zxc_decompress zxc_huffman".split()

SMOKE = r"""
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "zxc.h"

/* Round-trips one block and one frame at a fast and a dense level; a mismatch
 * refuses the load, so `insmod` is the test. */
static int __init zxctest_init(void)
{
	static const char msg[] = "zxc kernel smoke test, zxc kernel smoke test, zxc kernel";
	static const int levels[] = { 3, 7 };
	size_t cap = (size_t)zxc_compress_bound(sizeof(msg));
	u8 *comp = kmalloc(cap, GFP_KERNEL), *back = kmalloc(sizeof(msg), GFP_KERNEL);
	int rc = -ENOMEM, i;

	if (!comp || !back)
		goto out;
	for (i = 0; i < 2; i++) {
		zxc_compress_opts_t opts = { .level = levels[i] };
		zxc_cctx *cctx = zxc_create_cctx(&opts);
		zxc_dctx *dctx = zxc_create_dctx();
		int64_t n = -1, m = -1, fn = -1, fm = -1;

		rc = -ENOMEM;
		if (cctx && dctx) {
			n = zxc_compress_block(cctx, msg, sizeof(msg), comp, cap, &opts);
			if (n > 0)
				m = zxc_decompress_block_safe(dctx, comp, (size_t)n, back, sizeof(msg), NULL);
			rc = (m == sizeof(msg) && !memcmp(back, msg, sizeof(msg))) ? 0 : -EINVAL;
			memset(back, 0, sizeof(msg));
			fn = zxc_compress(msg, sizeof(msg), comp, cap, &opts);
			if (fn > 0)
				fm = zxc_decompress(comp, (size_t)fn, back, sizeof(msg), NULL);
			if (!rc && (fm != sizeof(msg) || memcmp(back, msg, sizeof(msg))))
				rc = -EINVAL;
		}
		pr_info("zxctest: level %d block %lld -> %lld, frame %lld -> %lld: %s\n", levels[i],
			(long long)n, (long long)m, (long long)fn, (long long)fm, rc ? "FAIL" : "ok");
		zxc_free_cctx(cctx);
		zxc_free_dctx(dctx);
		if (rc)
			break;
	}
out:
	kfree(comp);
	kfree(back);
	return rc;
}

static void __exit zxctest_exit(void) {}

module_init(zxctest_init);
module_exit(zxctest_exit);
MODULE_LICENSE("Dual BSD/GPL");
"""


def main(doc_path, out):
    root = os.path.dirname(os.path.dirname(os.path.abspath(doc_path)))
    with open(doc_path) as f:
        blocks = re.findall(r"```(\w*)\n(.*?)```", f.read(), re.S)
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
