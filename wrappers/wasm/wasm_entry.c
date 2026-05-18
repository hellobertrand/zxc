/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file wasm_entry.c
 * @brief Entry point and small JS-friendly shims for the WebAssembly build.
 *
 * Emscripten requires a main() symbol when producing a .js + .wasm pair
 * via add_executable(). Most public ZXC functionality is exported
 * directly from the library through -sEXPORTED_FUNCTIONS; this file
 * adds a couple of thin shims that adapt 64-bit C parameters to wasm32
 * so the JS wrapper can call them through cwrap('number', ...) without
 * relying on BigInt support.
 */

#include <stdint.h>

#include "zxc.h"
#include "zxc_seekable.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

int main(void) { return 0; }

/**
 * 32-bit-offset shim around zxc_seekable_decompress_range.
 *
 * The native function takes a `uint64_t` offset; passing an `i64` through
 * cwrap without `-sWASM_BIGINT=1` is not supported. The wasm32 heap is
 * itself bounded to 4 GiB, so a 32-bit offset is sufficient for any
 * archive the runtime could materialise in memory.
 *
 * Returns the number of bytes written, or a negative `zxc_error_t` code.
 * Returns truncated to int32_t (sufficient: max single-range length is
 * size_t which is i32 on wasm32).
 */
EMSCRIPTEN_KEEPALIVE
int32_t zxcw_seekable_decompress_range(zxc_seekable* s, void* dst, uint32_t dst_capacity,
                                       uint32_t offset, uint32_t len) {
    int64_t r =
        zxc_seekable_decompress_range(s, dst, (size_t)dst_capacity, (uint64_t)offset, (size_t)len);
    if (r > INT32_MAX) return INT32_MAX;
    if (r < INT32_MIN) return INT32_MIN;
    return (int32_t)r;
}
