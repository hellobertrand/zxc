/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file wasm_entry.c
 * @brief Minimal entry point for the WebAssembly build.
 *
 * Emscripten requires a main() symbol when producing a .js + .wasm pair
 * via add_executable().  All useful functionality is exported directly
 * from the library through -sEXPORTED_FUNCTIONS; this file simply
 * provides the mandatory entry point.
 */

int main(void) { return 0; }
