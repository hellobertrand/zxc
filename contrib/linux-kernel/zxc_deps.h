/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

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

// zxc spells these the libc way; the kernel ships U*_MAX.
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

// Without FP/SIMD the popcount builtins are unexported libgcc calls.
#define ZXC_POPCOUNT32(v) hweight32(v)
#define ZXC_POPCOUNT64(v) hweight64(v)

// A no-I/O scope keeps reclaim out of the I/O path; GFP_NOIO would cost kvmalloc
// its vmalloc fallback before 5.17.
static inline void* zxc_kernel_alloc(size_t size, gfp_t zero) {
    unsigned int noio = memalloc_noio_save();
    void* p = kvmalloc(size, GFP_KERNEL | __GFP_NOWARN | zero);

    memalloc_noio_restore(noio);
    return p;
}

#define ZXC_MALLOC(size) zxc_kernel_alloc((size), 0)
#define ZXC_CALLOC(nmemb, size) zxc_kernel_alloc(array_size((nmemb), (size)), __GFP_ZERO)
#define ZXC_FREE(ptr) kvfree(ptr)

// No ZXC_REALLOC: krealloc() wants kmalloc'd memory, and these units never
// reallocate; a future use fails to build.

// kmalloc need not reach a cache line: align by hand over kvmalloc.
static inline void* zxc_kernel_aligned_alloc(size_t size, size_t alignment) {
    void *mem, *ptr;
    size_t total;

    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (check_add_overflow(size, alignment + sizeof(void*), &total)) return NULL;
    mem = zxc_kernel_alloc(total, 0);
    if (!mem) return NULL;
    ptr = (void*)ALIGN((uintptr_t)mem + sizeof(void*), alignment);
    ((void**)ptr)[-1] = mem;
    return ptr;
}

static inline void zxc_kernel_aligned_free(void* ptr) {
    if (ptr) kvfree(((void**)ptr)[-1]);
}

#define ZXC_ALIGNED_MALLOC(size, alignment) zxc_kernel_aligned_alloc((size), (alignment))
#define ZXC_ALIGNED_FREE(ptr) zxc_kernel_aligned_free(ptr)

#endif  // ZXC_DEPS_H
