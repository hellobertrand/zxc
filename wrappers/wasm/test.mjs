/**
 * ZXC WASM Roundtrip Test
 *
 * Validates compress → decompress correctness via Node.js.
 * Run: node wrappers/wasm/test.mjs
 *
 * Expects the built zxc.js + zxc.wasm to be in the build directory.
 * The BUILD_DIR environment variable can override the default path.
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

import { createRequire } from 'module';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
// Resolve BUILD_DIR relative to CWD (not the test file location)
import { resolve } from 'path';
const buildDir = process.env.BUILD_DIR
    ? resolve(process.cwd(), process.env.BUILD_DIR)
    : join(__dirname, '..', '..', 'build-wasm');

// Emscripten generates a CJS module; use createRequire to load it.
const require = createRequire(import.meta.url);
const ZXCModule = require(join(buildDir, 'zxc.js'));

let passed = 0;
let failed = 0;

function assert(cond, msg) {
    if (!cond) {
        console.error(`  ✗ FAIL: ${msg}`);
        failed++;
    } else {
        console.log(`  ✓ ${msg}`);
        passed++;
    }
}

function arraysEqual(a, b) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) {
        if (a[i] !== b[i]) return false;
    }
    return true;
}

async function main() {
    console.log('ZXC WASM Test Suite\n');

    // --- 1. Module initialisation ---
    console.log('1. Module Initialisation');
    const Module = await ZXCModule();
    assert(!!Module, 'Module loaded successfully');

    // Wrap core functions
    const _compress = Module.cwrap('zxc_compress', 'number',
        ['number', 'number', 'number', 'number', 'number']);
    const _decompress = Module.cwrap('zxc_decompress', 'number',
        ['number', 'number', 'number', 'number', 'number']);
    const _compress_bound = Module.cwrap('zxc_compress_bound', 'number', ['number']);
    const _get_decompressed_size = Module.cwrap('zxc_get_decompressed_size', 'number',
        ['number', 'number']);
    const _version_string = Module.cwrap('zxc_version_string', 'string', []);
    const _min_level = Module.cwrap('zxc_min_level', 'number', []);
    const _max_level = Module.cwrap('zxc_max_level', 'number', []);
    const _default_level = Module.cwrap('zxc_default_level', 'number', []);
    const _error_name = Module.cwrap('zxc_error_name', 'string', ['number']);

    // --- 2. Library info ---
    console.log('\n2. Library Info');
    const version = _version_string();
    assert(typeof version === 'string' && version.length > 0, `Version: ${version}`);
    assert(_min_level() === 1, `Min level: ${_min_level()}`);
    assert(_max_level() === 5, `Max level: ${_max_level()}`);
    assert(_default_level() === 3, `Default level: ${_default_level()}`);

    // --- 3. Compress bound ---
    console.log('\n3. Compress Bound');
    const bound1k = _compress_bound(1024);
    assert(bound1k > 1024, `Bound for 1KB: ${bound1k}`);
    const bound0 = _compress_bound(0);
    assert(bound0 > 0, `Bound for 0 bytes: ${bound0}`);

    // --- 4. Roundtrip: simple string ---
    console.log('\n4. Roundtrip: Simple String');
    {
        const input = new TextEncoder().encode('Hello, ZXC WebAssembly! '.repeat(100));
        const bound = _compress_bound(input.length);

        const srcPtr = Module._malloc(input.length);
        const dstPtr = Module._malloc(bound);
        Module.HEAPU8.set(input, srcPtr);

        const csize = _compress(srcPtr, input.length, dstPtr, bound, 0);
        assert(csize > 0, `Compressed ${input.length} → ${csize} bytes`);
        assert(csize < input.length, `Compression ratio: ${(input.length / csize).toFixed(2)}x`);

        // Read back compressed data
        const compressed = new Uint8Array(Module.HEAPU8.buffer, dstPtr, csize).slice();

        // Get decompressed size
        const csrcPtr = Module._malloc(compressed.length);
        Module.HEAPU8.set(compressed, csrcPtr);
        const origSize = _get_decompressed_size(csrcPtr, compressed.length);
        assert(origSize === input.length, `Decompressed size: ${origSize} (expected ${input.length})`);

        // Decompress
        const outPtr = Module._malloc(origSize);
        const dsize = _decompress(csrcPtr, compressed.length, outPtr, origSize, 0);
        assert(dsize === input.length, `Decompressed ${dsize} bytes`);

        const output = new Uint8Array(Module.HEAPU8.buffer, outPtr, dsize).slice();
        assert(arraysEqual(input, output), 'Roundtrip byte-exact match');

        Module._free(srcPtr);
        Module._free(dstPtr);
        Module._free(csrcPtr);
        Module._free(outPtr);
    }

    // --- 5. Roundtrip: all compression levels ---
    console.log('\n5. Roundtrip: All Compression Levels');
    {
        const input = new Uint8Array(4096);
        // Fill with semi-compressible pattern
        for (let i = 0; i < input.length; i++) {
            input[i] = (i * 7 + 13) & 0xFF;
        }
        const bound = _compress_bound(input.length);

        for (let level = 1; level <= 5; level++) {
            const srcPtr = Module._malloc(input.length);
            const dstPtr = Module._malloc(bound);
            Module.HEAPU8.set(input, srcPtr);

            // Build opts struct: {n_threads=0, level, block_size=0, checksum=1, ...}
            const optsPtr = Module._malloc(24);
            for (let i = 0; i < 24; i++) Module.HEAPU8[optsPtr + i] = 0;
            Module.HEAP32[(optsPtr >> 2) + 1] = level;  // level
            Module.HEAP32[(optsPtr >> 2) + 3] = 1;      // checksum_enabled

            const csize = _compress(srcPtr, input.length, dstPtr, bound, optsPtr);
            assert(csize > 0, `Level ${level}: compressed to ${csize} bytes`);

            // Decompress with checksum verification
            const compressed = new Uint8Array(Module.HEAPU8.buffer, dstPtr, csize).slice();
            const csrcPtr = Module._malloc(compressed.length);
            Module.HEAPU8.set(compressed, csrcPtr);

            const dOptsPtr = Module._malloc(16);
            for (let i = 0; i < 16; i++) Module.HEAPU8[dOptsPtr + i] = 0;
            Module.HEAP32[(dOptsPtr >> 2) + 1] = 1; // checksum_enabled

            const outPtr = Module._malloc(input.length);
            const dsize = _decompress(csrcPtr, compressed.length, outPtr, input.length, dOptsPtr);
            const output = new Uint8Array(Module.HEAPU8.buffer, outPtr, dsize).slice();
            assert(arraysEqual(input, output), `Level ${level}: roundtrip OK`);

            Module._free(srcPtr);
            Module._free(dstPtr);
            Module._free(optsPtr);
            Module._free(csrcPtr);
            Module._free(dOptsPtr);
            Module._free(outPtr);
        }
    }

    // --- 6. Reusable context API ---
    console.log('\n6. Reusable Context API');
    {
        const _create_cctx = Module.cwrap('zxc_create_cctx', 'number', ['number']);
        const _free_cctx = Module.cwrap('zxc_free_cctx', 'void', ['number']);
        const _compress_cctx = Module.cwrap('zxc_compress_cctx', 'number',
            ['number', 'number', 'number', 'number', 'number', 'number']);
        const _create_dctx = Module.cwrap('zxc_create_dctx', 'number', []);
        const _free_dctx = Module.cwrap('zxc_free_dctx', 'void', ['number']);
        const _decompress_dctx = Module.cwrap('zxc_decompress_dctx', 'number',
            ['number', 'number', 'number', 'number', 'number', 'number']);

        const cctx = _create_cctx(0);
        assert(cctx !== 0, 'Created compression context');

        const dctx = _create_dctx();
        assert(dctx !== 0, 'Created decompression context');

        // Compress two different payloads with same context
        for (let trial = 0; trial < 3; trial++) {
            const input = new TextEncoder().encode(`Trial ${trial}: ${'ABCDEFGH'.repeat(200)}`);
            const bound = _compress_bound(input.length);

            const srcPtr = Module._malloc(input.length);
            const dstPtr = Module._malloc(bound);
            Module.HEAPU8.set(input, srcPtr);

            const csize = _compress_cctx(cctx, srcPtr, input.length, dstPtr, bound, 0);
            assert(csize > 0, `Context compress trial ${trial}: ${csize} bytes`);

            const compressed = new Uint8Array(Module.HEAPU8.buffer, dstPtr, csize).slice();
            const csrcPtr = Module._malloc(compressed.length);
            Module.HEAPU8.set(compressed, csrcPtr);

            const outPtr = Module._malloc(input.length);
            const dsize = _decompress_dctx(dctx, csrcPtr, compressed.length, outPtr, input.length, 0);
            const output = new Uint8Array(Module.HEAPU8.buffer, outPtr, dsize).slice();
            assert(arraysEqual(input, output), `Context roundtrip trial ${trial}: OK`);

            Module._free(srcPtr);
            Module._free(dstPtr);
            Module._free(csrcPtr);
            Module._free(outPtr);
        }

        _free_cctx(cctx);
        _free_dctx(dctx);
        console.log('  Contexts freed.');
    }

    // --- 7. Error handling ---
    console.log('\n7. Error Handling');
    {
        const errorName = _error_name(-1);
        assert(typeof errorName === 'string', `Error name for -1: ${errorName}`);

        // Attempt to decompress garbage
        const garbage = new Uint8Array([0, 1, 2, 3, 4, 5, 6, 7]);
        const gPtr = Module._malloc(garbage.length);
        const outPtr = Module._malloc(1024);
        Module.HEAPU8.set(garbage, gPtr);
        const result = _decompress(gPtr, garbage.length, outPtr, 1024, 0);
        assert(result < 0, `Garbage decompression returns error: ${result} (${_error_name(result)})`);
        Module._free(gPtr);
        Module._free(outPtr);
    }

    // --- Summary ---
    console.log(`\n${'='.repeat(40)}`);
    console.log(`Results: ${passed} passed, ${failed} failed`);
    process.exit(failed > 0 ? 1 : 0);
}

main().catch(err => {
    console.error('Fatal error:', err);
    process.exit(1);
});
