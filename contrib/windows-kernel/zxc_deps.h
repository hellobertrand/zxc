/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Jean Gautier and contributors.
 */

/**
 * @file zxc_deps.h
 * @brief Windows kernel-mode replacement for src/lib/zxc_deps.h.
 *
 * Maps the libc seams onto the executive pool. Everything else the core
 * reaches for - <stdint.h>, <limits.h>, <stddef.h>, <string.h> - is
 * freestanding in a WDK build, and ntoskrnl exports the memory primitives,
 * so unlike contrib/linux-kernel no header shim is needed.
 *
 * Windows 10 2004 is the floor: allocations go through ExAllocatePool2,
 * whose non-paged pool is non-executable.
 */

#ifndef ZXC_DEPS_H
#define ZXC_DEPS_H

#ifndef _KERNEL_MODE
#error \
    "contrib/windows-kernel/zxc_deps.h: build with /kernel (the WindowsKernelModeDriver10.0 toolset)"
#endif

// The kernel toolset derives NTDDI_VERSION from TargetVersion; a hand-rolled
// build passes it. sdkddkver.h cannot be included ahead of ntddk.h to supply a
// default (it then defines DECLSPEC_DEPRECATED_DDK_WINXP before ntdef.h has
// defined what it expands to, and wdm.h fails), so the threshold is spelled
// out here and checked against the kit once ntddk.h is in.
#ifndef NTDDI_VERSION
#error "contrib/windows-kernel: define NTDDI_VERSION (0x0A000008 for Windows 10 2004)"
#endif
#define ZXC_KM_NTDDI_WIN10_VB 0x0A000008

#if NTDDI_VERSION < ZXC_KM_NTDDI_WIN10_VB
#error \
    "contrib/windows-kernel: Windows 10 2004 is the floor (NTDDI_VERSION is below NTDDI_WIN10_VB)"
#endif

#if !defined(_M_AMD64) && !defined(_M_ARM64)
#error "contrib/windows-kernel: x64 and ARM64 only"
#endif

#include <limits.h>
#include <ntddk.h>
#include <ntintsafe.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if ZXC_KM_NTDDI_WIN10_VB != NTDDI_WIN10_VB
#error "contrib/windows-kernel: the kit's NTDDI_WIN10_VB differs from the value spelled out above"
#endif

// <assert.h> is the hosted spelling of the keyword; zxc_internal.h checks its
// constants with the two-argument form.
#ifndef static_assert
#define static_assert(expr, msg) _Static_assert(expr, msg)
#endif

/** @brief Pool tag; pool dumps display tags reversed, so this reads "zxc ". */
#define ZXC_POOL_TAG ((ULONG)' cxz')

// Non-paged: a context built at PASSIVE_LEVEL must stay usable at
// DISPATCH_LEVEL, and the lazy allocations of a heap context may happen there.
// A zero size is rounded up to one byte: Driver Verifier flags zero-length
// allocations.
static inline void* zxc_km_alloc(const size_t size, const int zero) {
    return ExAllocatePool2(POOL_FLAG_NON_PAGED | (zero ? 0 : POOL_FLAG_UNINITIALIZED),
                           size ? size : 1, ZXC_POOL_TAG);
}

static inline void* zxc_km_calloc(const size_t nmemb, const size_t size) {
    size_t total;
    if (!NT_SUCCESS(RtlSizeTMult(nmemb, size, &total))) return NULL;
    return zxc_km_alloc(total, 1);
}

// ExFreePoolWithTag(NULL) bug-checks; libc free(NULL) is a no-op.
static inline void zxc_km_free(void* ptr) {
    if (ptr) ExFreePoolWithTag(ptr, ZXC_POOL_TAG);
}

#define ZXC_MALLOC(size) zxc_km_alloc((size), 0)
#define ZXC_CALLOC(nmemb, size) zxc_km_calloc((nmemb), (size))
#define ZXC_FREE(ptr) zxc_km_free(ptr)

// No ZXC_REALLOC: pool has no realloc, and these units never reallocate; a
// future use fails to build.

// Pool aligns to 16 bytes below a page and to a page above: align by hand for
// the cache-line workspaces, as the Linux header does over kvmalloc.
static inline void* zxc_km_aligned_alloc(const size_t size, size_t alignment) {
    void* mem;
    void* ptr;
    size_t total;

    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (!NT_SUCCESS(RtlSizeTAdd(size, alignment + sizeof(void*), &total))) return NULL;
    mem = zxc_km_alloc(total, 0);
    if (!mem) return NULL;
    ptr = (void*)(((uintptr_t)mem + sizeof(void*) + (alignment - 1)) & ~(uintptr_t)(alignment - 1));
    ((void**)ptr)[-1] = mem;
    return ptr;
}

static inline void zxc_km_aligned_free(void* ptr) {
    if (ptr) ExFreePoolWithTag(((void**)ptr)[-1], ZXC_POOL_TAG);
}

#define ZXC_ALIGNED_MALLOC(size, alignment) zxc_km_aligned_alloc((size), (alignment))
#define ZXC_ALIGNED_FREE(ptr) zxc_km_aligned_free(ptr)

// SWAR popcount: __popcnt emits POPCNT, which faults on CPUs without it, and
// nothing in a kernel build may assume that instruction.
static inline int zxc_km_popcount32(const uint32_t v) {
    uint32_t x = v - ((v >> 1) & 0x55555555U);
    x = (x & 0x33333333U) + ((x >> 2) & 0x33333333U);
    x = (x + (x >> 4)) & 0x0F0F0F0FU;
    return (int)((x * 0x01010101U) >> 24);
}
static inline int zxc_km_popcount64(const uint64_t v) {
    return zxc_km_popcount32((uint32_t)v) + zxc_km_popcount32((uint32_t)(v >> 32));
}
#define ZXC_POPCOUNT32(v) zxc_km_popcount32(v)
#define ZXC_POPCOUNT64(v) zxc_km_popcount64(v)

#endif /* ZXC_DEPS_H */
