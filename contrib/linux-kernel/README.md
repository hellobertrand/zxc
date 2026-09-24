# ZXC in the Linux kernel

Everything an out-of-tree module needs: the dependency header, two header
shims, a Kbuild and a self-test module, plus this guide. `make` here stages the
library sources next to the Kbuild and builds `build/zxc_selftest.ko` against
the running kernel's headers (`KDIR=` for another tree). Loading it round-trips
a block and a frame at levels 3 and 7 and refuses to load on a mismatch.

```
zxc_deps.h     the kernel replacement for src/lib/zxc_deps.h
shim/          <stdint.h> and <string.h> over their kernel counterparts
Kbuild         flags and object list
selftest.c     the module's init: the round trips
Makefile       stages the sources into build/ and calls the kernel's make
```

Porting ZXC to a host with no libc: a bootloader or firmware unpacking its
payload, a read-only filesystem image, an initramfs. ZXC compresses slowly and
decompresses fast: data written once, read many times. So compress in userspace,
once and at any level - the image builder, the initramfs generator, the firmware
toolchain - and decompress in the kernel. A backend compressing on every page
write (zram, zswap) would pay the slow side on its hot path.

The core uses no floating point, no VLA and no `alloca`, and calls the C
library only through `src/lib/zxc_deps.h`.
Two libc headers need a shim over their kernel counterpart (`shim/`):
`<stdint.h>` (public headers) and `<string.h>` (`rapidhash.h`, for `memcpy`).
The compiler's `<stdint.h>` will not do: GCC's reaches for libc's, and its LP64
`uint64_t` is `unsigned long`, the kernel's `unsigned long long`. `<stddef.h>`
and `<stdalign.h>` stay the compiler's.

Decoding peaks at 4.5 KiB of stack whatever level the archive was written at,
inside a 16 KiB kernel stack and an 8 KiB one on 32-bit. Compressing in the
kernel is the exception; it allocates nothing per block and peaks at 12.3 KiB at
levels 6-7 (see [Contexts](#contexts)).

## What to build

Seven translation units:

```
zxc_common.c  zxc_pivco_tables.c  zxc_dispatch.c  zxc_dict.c
zxc_compress.c  zxc_decompress.c  zxc_huffman.c
```

They carry the whole buffer API: frame, blocks, dictionaries. Left out:
`zxc_driver.c` (`<stdio.h>`), `zxc_seekable.c` (the random-access reader,
threaded) and `zxc_pstream.c` (the push streams grow their buffers with
`ZXC_REALLOC`, which `zxc_deps.h` leaves undefined).

The `Kbuild` flags, and why:

- `-DZXC_STATIC_DEFINE -DZXC_DISABLE_SIMD`: no export decorations, scalar code
  (see [No FPU region](#no-fpu-region)).
- `-DZXC_FUNCTION_SUFFIX=_default`: the dispatcher binds the per-ISA sources by
  this suffix; the other units ignore it.
- `-std=gnu11 -Wno-declaration-after-statement`: kernels before 5.18 build
  `-std=gnu89` with C90 declaration checks.
- `-I` order: the shims, then the sources, then the compiler's freestanding
  headers (`-isystem $(CC) -print-file-name=include`).
- `-Wframe-larger-than=8192`: a few frames pass `CONFIG_FRAME_WARN` (2 KiB on
  64-bit), an error under `CONFIG_WERROR`. What bounds the stack is the whole
  path, measured below.

CI builds this module against the runner's kernel under `-Werror`
(`.github/workflows/kernel.yml`) when this directory or the library change, and
weekly.

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
| 4 KiB   | 15 872 B  | 314 496 B         | 532 736 B         |
| 64 KiB  | 212 480 B | 500 480 B         | 1 033 216 B       |
| 512 KiB | 1.60 MiB  | 1.79 MiB          | 5.85 MiB          |

- `kvmalloc`, not `kmalloc`: a 314 KB request lands in a 512 KB slab, and an
  order-7 contiguous allocation is fragile under memory pressure.
- Never put a workspace on the stack.
- A heap context (`zxc_create_dctx`) allocates on the first decode, and again on
  the first entropy-coded block. With preemption disabled, that is a
  `BUG: scheduling while atomic`.
- Stack, measured with a painted thread stack and with GCC's `-fstack-usage`
  worst path, both under the flags above: decoding 4.5 KiB at every level,
  compression 5.9 KiB up to level 5 and 12.3 KiB at 6-7. On an 8 KiB 32-bit
  stack, compress at levels 1-5 only.
- Nothing is allocated per block at any level: the code-length builder and its
  nudge work in the context's scratch, which is what makes the level 6-7
  workspace larger at 4 KiB blocks.

## Exactly-sized destinations

`zxc_decompress_block()` wants `ZXC_DECOMPRESS_TAIL_PAD` (2112 bytes) of slack
past the output for its wild copies, which a page-sized buffer does not have.
`zxc_decompress_block_safe()` takes a destination sized to the exact output.

## The dependency header

`zxc_deps.h` here replaces `src/lib/zxc_deps.h` (the Makefile copies it over).
What it maps, and why:

- `ZXC_MALLOC` / `ZXC_CALLOC` / `ZXC_FREE`: `kvmalloc` under a no-I/O scope
  (`memalloc_noio_save`), not `GFP_NOIO`, which costs `kvmalloc` its vmalloc
  fallback before 5.17. Reclaim still stays out of the I/O path.
- No `ZXC_REALLOC`: `krealloc()` only accepts kmalloc'd memory, and these units
  never reallocate - undefined, a future use fails to build.
- `ZXC_ALIGNED_MALLOC` / `ZXC_ALIGNED_FREE`: `kmalloc` need not reach a cache
  line, so the header aligns by hand over `kvmalloc`, which also keeps the big
  workspaces off the power-of-two slabs.
- `ZXC_POPCOUNT32` / `ZXC_POPCOUNT64`: `hweight32` / `hweight64`. Without
  FP/SIMD registers the popcount builtins are libgcc calls the kernel does not
  export.
- `CHAR_BIT`, `UINT16_MAX`, `UINT32_MAX`, `UINT64_MAX`: the kernel spells them
  `U*_MAX`.

The stock `posix_memalign` / `_aligned_malloc` helpers are `static inline` in
`zxc_deps.h` itself, so a replacement defines `ZXC_ALIGNED_MALLOC` and
`ZXC_ALIGNED_FREE` both, as here; one without the other is a compile error.
`ZXC_NOINLINE` needs no local edit.

## Licensing

BSD-3-Clause. A module calling `EXPORT_SYMBOL_GPL` symbols declares
`MODULE_LICENSE("Dual BSD/GPL")`; BSD-3 is GPL-compatible.
