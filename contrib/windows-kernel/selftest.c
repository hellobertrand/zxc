// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/*
 * The reference consumer of zxc_km.lib: a legacy software driver whose
 * DriverEntry round-trips one block, one frame and one in-place frame on a
 * static context, at a fast and a dense level, like the Linux self-test
 * module. A mismatch fails the load.
 *
 * Load it on a test-signed machine: sc create zxc_selftest type= kernel
 * binPath= <path>\zxc_selftest.sys, then sc start zxc_selftest. One line per
 * level reaches the kernel debugger or DebugView.
 */

// The kernel toolset derives NTDDI_VERSION from TargetVersion; a hand-rolled
// build passes it. sdkddkver.h cannot supply a default ahead of ntddk.h (see
// zxc_deps.h), so the Windows 10 2004 threshold is spelled out here.
#ifndef NTDDI_VERSION
#error "zxc_selftest: define NTDDI_VERSION (the kernel toolset sets it from TargetVersion)"
#endif

// Plan B (below Windows 10 2004, NTDDI_WIN10_VB): opt into NX non-paged pool
// at run time; the kernel honours it from Windows 8. The library's allocations
// follow this setting too, through the kit's shared ExDefaultNonPagedPoolType.
#if NTDDI_VERSION < 0x0A000008
#define POOL_NX_OPTIN 1
#endif

#include <ntddk.h>
#include <string.h>

#include "zxc.h"

#define ZXC_SELFTEST_TAG ((ULONG)'tsxz')

// The dense level. Compressing at levels 6 and 7 peaks past 12 KiB of stack,
// which is the whole x86 kernel stack; levels 1 to 5 stay under 6 KiB.
#if defined(_M_IX86)
#define ZXC_SELFTEST_DENSE 5
#else
#define ZXC_SELFTEST_DENSE 7
#endif
_Static_assert(ZXC_SELFTEST_DENSE <= 5 || sizeof(void*) == 8,
               "levels 6 and 7 do not fit the 12 KB x86 kernel stack");

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD zxc_selftest_unload;

static void* st_alloc(const size_t n) {
#if NTDDI_VERSION >= NTDDI_WIN10_VB
    return ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, n, ZXC_SELFTEST_TAG);
#else
    return ExAllocatePoolUninitialized(NonPagedPool, n, ZXC_SELFTEST_TAG);
#endif
}

static void st_free(void* p) {
    if (p) ExFreePoolWithTag(p, ZXC_SELFTEST_TAG);
}

_Use_decl_annotations_ void zxc_selftest_unload(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
}

_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject,
                                            PUNICODE_STRING RegistryPath) {
    static const char msg[] = "zxc kernel smoke test, zxc kernel smoke test, zxc kernel";
    static const int levels[] = {3, ZXC_SELFTEST_DENSE};
    size_t cap, ws_sz;
    uint8_t *comp, *back;
    void* ws;
    zxc_dctx* sdctx;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    int i;

    UNREFERENCED_PARAMETER(RegistryPath);
#if POOL_NX_OPTIN
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);  // before the first allocation
#endif
    DriverObject->DriverUnload = zxc_selftest_unload;

    cap = (size_t)zxc_compress_bound(sizeof(msg));
    ws_sz = zxc_static_dctx_workspace_size(PAGE_SIZE);
    comp = st_alloc(cap);
    back = st_alloc(sizeof(msg));
    ws = st_alloc(ws_sz);
    sdctx = ws ? zxc_init_static_dctx(ws, ws_sz, PAGE_SIZE) : NULL;

    if (!comp || !back || !sdctx) goto out;
    for (i = 0; i < 2; i++) {
        zxc_compress_opts_t opts = {.level = levels[i]};
        zxc_cctx* cctx = zxc_create_cctx(&opts);
        zxc_dctx* dctx = zxc_create_dctx();
        int64_t n = -1, m = -1, fn = -1, fm = -1, pn = -1, pm = -1;

        status = STATUS_INSUFFICIENT_RESOURCES;
        if (cctx && dctx) {
            n = zxc_compress_block(cctx, msg, sizeof(msg), comp, cap, &opts);
            if (n > 0)
                m = zxc_decompress_block_safe(dctx, comp, (size_t)n, back, sizeof(msg), NULL);
            status = (m == sizeof(msg) && !memcmp(back, msg, sizeof(msg))) ? STATUS_SUCCESS
                                                                           : STATUS_DATA_ERROR;
            memset(back, 0, sizeof(msg));
            fn = zxc_compress(msg, sizeof(msg), comp, cap, &opts);
            if (fn > 0) fm = zxc_decompress(comp, (size_t)fn, back, sizeof(msg), NULL);
            if (NT_SUCCESS(status) && (fm != sizeof(msg) || memcmp(back, msg, sizeof(msg))))
                status = STATUS_DATA_ERROR;

            // In place: the archive flush-right in one buffer, no allocation inside.
            zxc_compress_opts_t popts = {.level = levels[i], .block_size = PAGE_SIZE};
            uint8_t* buf = NULL;
            size_t need = 0;
            pn = zxc_compress(msg, sizeof(msg), comp, cap, &popts);
            if (pn > 0) need = zxc_decompress_inplace_bound(comp, (size_t)pn);
            if (need) buf = st_alloc(need);
            if (buf) {
                memcpy(buf + need - (size_t)pn, comp, (size_t)pn);
                pm = zxc_decompress_inplace_dctx(sdctx, buf, need, (size_t)pn, NULL);
            }
            if (need && !buf) {
                status = STATUS_INSUFFICIENT_RESOURCES;
            } else if (NT_SUCCESS(status) &&
                       (pm != sizeof(msg) || !buf || memcmp(buf, msg, sizeof(msg)))) {
                status = STATUS_DATA_ERROR;
            }
            // The library's own lazy allocations surface as ZXC_ERROR_MEMORY: an
            // allocation failure, not a codec mismatch.
            if (n == ZXC_ERROR_MEMORY || m == ZXC_ERROR_MEMORY || fn == ZXC_ERROR_MEMORY ||
                fm == ZXC_ERROR_MEMORY || pn == ZXC_ERROR_MEMORY || pm == ZXC_ERROR_MEMORY)
                status = STATUS_INSUFFICIENT_RESOURCES;
            st_free(buf);
        }
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "zxc_selftest: level %d block %I64d -> %I64d, frame %I64d -> %I64d, "
                   "in place %I64d -> %I64d: %s\n",
                   levels[i], n, m, fn, fm, pn, pm, NT_SUCCESS(status) ? "ok" : "FAIL");
        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
        if (!NT_SUCCESS(status)) break;
    }
out:
    st_free(comp);
    st_free(back);
    st_free(ws);
    return status;
}
