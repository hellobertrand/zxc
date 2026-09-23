# Kernel and freestanding integration

Porting zxc to a host with no libc: a bootloader or firmware unpacking its
payload, a read-only filesystem image, an initramfs. ZXC compresses slowly and
decompresses fast: data written once, read many times. A backend compressing on
every page write (zram, zswap) would pay the slow side on its hot path.

The core uses no floating point, no VLA and no `alloca`, and calls the C
library only through `src/lib/zxc_deps.h`.
Two libc headers need a shim over their kernel counterpart: `<stdint.h>` (public
headers) and `<string.h>` (`rapidhash.h`, for `memcpy`). The compiler's
`<stdint.h>` will not do: GCC's reaches for libc's, and its LP64 `uint64_t` is
`unsigned long`, the kernel's `unsigned long long`. `<stddef.h>` and
`<stdalign.h>` stay the compiler's.

```c
/* shim/stdint.h */
#include <linux/types.h>
```

```c
/* shim/string.h */
#include <linux/string.h>
```

Decoding peaks at 4.5 KiB of stack, inside a 16 KiB kernel stack and an 8 KiB
one on 32-bit.
Compression is the level-dependent one (see [Contexts](#contexts)).

## What to build

Seven translation units:

```
zxc_common.c  zxc_pivco_tables.c  zxc_dispatch.c  zxc_dict.c
zxc_compress.c  zxc_decompress.c  zxc_huffman.c
```

They carry the whole buffer API, frame and dictionaries included. Left out:
`zxc_driver.c` (`<stdio.h>`) and `zxc_seekable.c` (the random-access reader,
threaded).

```make
ccflags-y += -DZXC_STATIC_DEFINE -DZXC_DISABLE_SIMD
# The dispatcher binds the per-ISA sources by this suffix; the rest ignore it.
ccflags-y += -DZXC_FUNCTION_SUFFIX=_default
ccflags-y += -std=gnu11 -Wno-declaration-after-statement
# Shims first, then the sources, then the compiler's freestanding headers.
ccflags-y += -I$(src)/shim
ccflags-y += -I$(src)/zxc/include -I$(src)/zxc/src/lib -I$(src)/zxc/src/lib/vendors
ccflags-y += -isystem $(shell $(CC) -print-file-name=include)
# Several frames pass CONFIG_FRAME_WARN (2 KiB on 64-bit), an error under
# CONFIG_WERROR. What bounds the stack is the whole path, measured below.
ccflags-y += -Wframe-larger-than=12288
```

CI builds a module from this page's own snippets and round-trips a block and a
frame with it (`tests/kernel/module_from_doc.py`, job `kernel-module` in
`.github/workflows/packaging.yml`).

`-std=gnu11` because kernels before 5.18 build `-std=gnu89` with C90 declaration
checks.

## No FPU region

Do not wrap the SIMD in `kernel_fpu_begin()` / `kernel_neon_begin()`. Build
scalar, with `ZXC_DISABLE_SIMD`.

- The SIMD is inline helpers called per match, so a region per call would save
  and restore FPU state every few dozen bytes.
- The only other seam is one region per block, which holds a whole block with
  preemption disabled.
- The calls are arch-specific and `EXPORT_SYMBOL_GPL`.
- `irq_fpu_usable()` can refuse, so a scalar path is needed anyway.

## Frame or block

A payload, an image or an initramfs is a `.zxc` frame: `zxc_decompress()`, or
`zxc_decompress_dctx()` on a static context. A host that works page by page
(a zram-like backend) uses `zxc_compress_block()` and
`zxc_decompress_block_safe()` instead: the frame's header, footer and per-frame
setup would be paid on every page for nothing.

## Contexts

Static contexts: the host allocates one workspace per CPU at init, the library
carves everything out of it, and decoding then allocates nothing.

```c
size_t ws_sz = zxc_static_dctx_workspace_size(PAGE_SIZE);
void  *ws    = kvmalloc(ws_sz, GFP_KERNEL);       /* once, at init, per CPU */
zxc_dctx *dctx = zxc_init_static_dctx(ws, ws_sz, PAGE_SIZE);
```

Workspace sizes; levels 6-7 add the optimal-parser scratch:

| blocks  | dctx      | cctx (levels 1-5) | cctx (levels 6-7) |
|---------|-----------|-------------------|-------------------|
| 4 KiB   | 15 872 B  | 314 496 B         | 404 672 B         |
| 64 KiB  | 212 480 B | 500 480 B         | 1 033 216 B       |
| 512 KiB | 1.60 MiB  | 1.79 MiB          | 5.85 MiB          |

- `kvmalloc`, not `kmalloc`: a 314 KB request lands in a 512 KB slab, and an
  order-7 contiguous allocation is fragile under memory pressure.
- Never put a workspace on the stack.
- A heap context (`zxc_create_dctx`) allocates on the first decode, and again on
  the first entropy-coded block. With preemption disabled, that is a
  `BUG: scheduling while atomic`.
- Compression allocates per block from `ZXC_LEVEL_DENSITY` (6) upward: the joint
  Huffman nudge sizes a scratch pool from the data. Stay below 6.
- Stack, measured with a painted thread stack and with GCC's `-fstack-usage`
  worst path, both under the flags above: decoding 4.5 KiB, compression 5.9 KiB
  up to level 5 (tight on an 8 KiB 32-bit stack). Level 6 and 7 reach 21 KiB in
  the nudge, past a 16 KiB kernel stack - a second reason to stay below 6.

## Exactly-sized destinations

`zxc_decompress_block()` wants `ZXC_DECOMPRESS_TAIL_PAD` (2112 bytes) of slack
past the output for its wild copies, which a page-sized buffer does not have.
`zxc_decompress_block_safe()` takes a destination sized to the exact output.

## The dependency header

Vendor a replacement for `src/lib/zxc_deps.h`:

```c
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef ZXC_DEPS_H
#define ZXC_DEPS_H

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/overflow.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

/* zxc spells these the libc way; the kernel ships the U*_MAX family. */
#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif
#ifndef UINT16_MAX
#define UINT16_MAX U16_MAX
#endif
#ifndef UINT32_MAX
#define UINT32_MAX U32_MAX
#endif
#ifndef UINT64_MAX
#define UINT64_MAX U64_MAX
#endif

/* Without FP/SIMD the popcount builtins are unexported libgcc calls. */
#define ZXC_POPCOUNT32(v) hweight32(v)
#define ZXC_POPCOUNT64(v) hweight64(v)

/* Reclaim must not recurse into the I/O path: a no-I/O scope around
 * GFP_KERNEL, not GFP_NOIO, which costs kvmalloc its vmalloc fallback
 * before 5.17. */
static inline void *zxc_kernel_alloc(size_t size, gfp_t zero)
{
	unsigned int noio = memalloc_noio_save();
	void *p = kvmalloc(size, GFP_KERNEL | __GFP_NOWARN | zero);

	memalloc_noio_restore(noio);
	return p;
}

#define ZXC_MALLOC(size)          zxc_kernel_alloc((size), 0)
#define ZXC_CALLOC(nmemb, size)   zxc_kernel_alloc(array_size((nmemb), (size)), __GFP_ZERO)
#define ZXC_FREE(ptr)             kvfree(ptr)

/* No ZXC_REALLOC: krealloc() only accepts kmalloc'd memory, and the block-only
 * subset never reallocates - undefined, a future use fails to build. */

/* kmalloc need not reach a cache line, so align by hand; kvmalloc keeps the
 * big workspaces off the power-of-two slabs. */
static inline void *zxc_kernel_aligned_alloc(size_t size, size_t alignment)
{
	void *mem, *ptr;
	size_t total;

	if (alignment < sizeof(void *))
		alignment = sizeof(void *);
	if (check_add_overflow(size, alignment + sizeof(void *), &total))
		return NULL;
	mem = zxc_kernel_alloc(total, 0);
	if (!mem)
		return NULL;
	ptr = (void *)ALIGN((uintptr_t)mem + sizeof(void *), alignment);
	((void **)ptr)[-1] = mem;
	return ptr;
}

static inline void zxc_kernel_aligned_free(void *ptr)
{
	if (ptr)
		kvfree(((void **)ptr)[-1]);
}

#define ZXC_ALIGNED_MALLOC(size, alignment) zxc_kernel_aligned_alloc((size), (alignment))
#define ZXC_ALIGNED_FREE(ptr)               zxc_kernel_aligned_free(ptr)

#endif /* ZXC_DEPS_H */
```

The stock `posix_memalign` / `_aligned_malloc` helpers are `static inline` in
`zxc_deps.h` itself, so a replacement defines `ZXC_ALIGNED_MALLOC` and
`ZXC_ALIGNED_FREE` both, as above; one without the other is a compile error.
`ZXC_NOINLINE` needs no local edit.

## Licensing

BSD-3-Clause. A module calling `EXPORT_SYMBOL_GPL` symbols declares
`MODULE_LICENSE("Dual BSD/GPL")`; BSD-3 is GPL-compatible.
