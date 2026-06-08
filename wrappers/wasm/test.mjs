/**
 * ZXC WASM Roundtrip Test
 *
 * Validates compress -> decompress correctness via Node.js.
 * Run: node wrappers/wasm/test.mjs
 *
 * Expects the built zxc.js + zxc.wasm to be in the build directory.
 * The BUILD_DIR environment variable can override the default path.
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

import { createRequire } from 'module';
import { join, dirname, resolve } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
// Resolve BUILD_DIR relative to CWD (not the test file location)
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
    assert(_max_level() === 6, `Max level: ${_max_level()}`);
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
        assert(csize > 0, `Compressed ${input.length} -> ${csize} bytes`);
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

        for (let level = 1; level <= 6; level++) {
            const srcPtr = Module._malloc(input.length);
            const dstPtr = Module._malloc(bound);
            Module.HEAPU8.set(input, srcPtr);

            // Build opts struct: {n_threads=0, level, block_size=0, checksum=1, seekable=0, ...}
            const optsPtr = Module._malloc(28);
            for (let i = 0; i < 28; i++) Module.HEAPU8[optsPtr + i] = 0;
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

    // --- 8. Push streaming API (high-level wrapper) -----------------------
    console.log('\n8. Push Streaming API (CStream / DStream)');
    {
        const { default: createZXC } = await import('./zxc_wasm.js');
        const zxc = await createZXC({}, ZXCModule);

        const cs = zxc.createCStream({ checksum: true });
        assert(cs.inSize() > 0 && cs.outSize() > 0, `cstream size hints: in=${cs.inSize()} out=${cs.outSize()}`);

        // Multi-block payload to exercise > 1 block.
        const data = new Uint8Array(512 * 1024);
        for (let i = 0; i < data.length; i++) data[i] = (i * 7) & 0xff;

        const chunks = [];
        let totalC = 0;
        const step = 17_000;
        for (let i = 0; i < data.length; i += step) {
            const c = cs.compress(data.subarray(i, Math.min(i + step, data.length)));
            chunks.push(c);
            totalC += c.length;
        }
        const tail = cs.end();
        chunks.push(tail);
        totalC += tail.length;
        cs.free();

        const compressed = new Uint8Array(totalC);
        let off = 0;
        for (const c of chunks) { compressed.set(c, off); off += c.length; }
        assert(compressed.length > 0, `pstream compressed ${data.length} -> ${compressed.length} bytes`);

        const ds = zxc.createDStream({ checksum: true });
        const outChunks = [];
        let totalD = 0;
        const dstep = 31_000;
        for (let i = 0; i < compressed.length; i += dstep) {
            const o = ds.decompress(compressed.subarray(i, Math.min(i + dstep, compressed.length)));
            outChunks.push(o);
            totalD += o.length;
        }
        assert(ds.finished(), 'dstream finished after full input');
        ds.free();

        const decoded = new Uint8Array(totalD);
        off = 0;
        for (const c of outChunks) { decoded.set(c, off); off += c.length; }
        assert(decoded.length === data.length, `pstream decoded ${decoded.length} bytes (expected ${data.length})`);
        assert(arraysEqual(data, decoded), 'pstream roundtrip byte-exact match');
    }

    // --- 9. WHATWG TransformStream adapters -------------------------------
    console.log('\n9. WHATWG TransformStream adapters + detectZxc');
    {
        const { default: createZXC, detectZxc } = await import('./zxc_wasm.js');
        const zxc = await createZXC({}, ZXCModule);

        // detectZxc: works without WASM init too (pure JS), but check both
        // call sites as they share the same implementation.
        assert(detectZxc === zxc.detectZxc, 'detectZxc exported at module + API level');
        assert(!detectZxc(null), 'detectZxc(null) === false');
        assert(!detectZxc(new Uint8Array([0xF5, 0x2E, 0xB0])), 'detectZxc rejects 3-byte input');
        assert(!detectZxc(new Uint8Array(4)), 'detectZxc rejects zero buffer');

        // Build a frame with the buffer API and sniff it.
        const frame = zxc.compress(new Uint8Array([1, 2, 3, 4, 5]));
        assert(detectZxc(frame), 'detectZxc accepts compress() output');
        assert(detectZxc(frame.buffer), 'detectZxc accepts ArrayBuffer');

        // TransformStream roundtrip via pipeThrough().
        const data = new Uint8Array(64 * 1024);
        for (let i = 0; i < data.length; i++) data[i] = (i ^ 0x5A) & 0xFF;

        const sourceCompressed = new ReadableStream({
            start(c) { c.enqueue(data); c.close(); }
        }).pipeThrough(zxc.createCompressTransformStream());

        const compressedChunks = [];
        let cTotal = 0;
        for await (const c of sourceCompressed) {
            compressedChunks.push(c);
            cTotal += c.length;
        }
        const compressed = new Uint8Array(cTotal);
        {
            let off = 0;
            for (const c of compressedChunks) { compressed.set(c, off); off += c.length; }
        }
        assert(detectZxc(compressed), 'TransformStream output is a valid ZXC frame');

        const sourceDecompressed = new ReadableStream({
            start(c) { c.enqueue(compressed); c.close(); }
        }).pipeThrough(zxc.createDecompressTransformStream());

        const outChunks = [];
        let dTotal = 0;
        for await (const c of sourceDecompressed) {
            outChunks.push(c);
            dTotal += c.length;
        }
        const decoded = new Uint8Array(dTotal);
        {
            let off = 0;
            for (const c of outChunks) { decoded.set(c, off); off += c.length; }
        }
        assert(decoded.length === data.length, `TransformStream decoded ${decoded.length} bytes (expected ${data.length})`);
        assert(arraysEqual(data, decoded), 'TransformStream roundtrip byte-exact match');

        // Truncated frame must error the decode stream.
        const truncated = compressed.subarray(0, Math.floor(compressed.length / 2));
        const truncSource = new ReadableStream({
            start(c) { c.enqueue(truncated); c.close(); }
        }).pipeThrough(zxc.createDecompressTransformStream());

        let didThrow = false;
        let errorCode;
        try {
            for await (const _ of truncSource) { /* drain */ }
        } catch (e) {
            didThrow = true;
            errorCode = e?.code;
        }
        assert(didThrow, 'truncated frame throws an error');
        assert(errorCode === 'ZXC_TRUNCATED', 'truncated frame error code is ZXC_TRUNCATED');
    }

    // --- 10. Seekable API (random-access decompression) -------------------
    console.log('\n10. Seekable API');
    {
        const { default: createZXC } = await import('./zxc_wasm.js');
        const zxc = await createZXC({}, ZXCModule);

        // Build a payload that exercises both block-level and intra-block ranges.
        const payload = new Uint8Array(64 * 1024);
        for (let i = 0; i < payload.length; i++) payload[i] = (i * 31) & 0xFF;

        const compressed = zxc.compress(payload, { seekable: true, checksum: true });
        assert(compressed.length > 0, `seekable archive compressed to ${compressed.length} bytes`);

        const s = zxc.createSeekable(compressed);
        try {
            assert(s.decompressedSize() === payload.length,
                `decompressedSize=${s.decompressedSize()} (expected ${payload.length})`);
            assert(s.numBlocks() >= 1, `numBlocks=${s.numBlocks()}`);

            const cs0 = s.blockCompressedSize(0);
            const ds0 = s.blockDecompressedSize(0);
            assert(typeof cs0 === 'number' && cs0 > 0, `block 0 compressed size=${cs0}`);
            assert(typeof ds0 === 'number' && ds0 > 0, `block 0 decompressed size=${ds0}`);
            assert(s.blockCompressedSize(s.numBlocks()) === null,
                'out-of-range block index returns null');

            // Full-range round-trip.
            const full = s.decompressRange(0, payload.length);
            assert(full.length === payload.length, `full range length=${full.length}`);
            assert(arraysEqual(full, payload), 'seekable full-range byte-exact match');

            // Mid-range slice.
            const off = 1024, len = 8192;
            const mid = s.decompressRange(off, len);
            assert(mid.length === len, `mid range length=${mid.length}`);
            assert(arraysEqual(mid, payload.subarray(off, off + len)),
                'seekable mid-range byte-exact match');
        } finally {
            s.free();
        }

        // Invalid archive throws.
        let didThrow = false;
        try {
            zxc.createSeekable(new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]));
        } catch (_) {
            didThrow = true;
        }
        assert(didThrow, 'createSeekable on garbage throws');

        // Low-level seek table helpers.
        const compSizes = [128, 256, 200, 4];
        const tableSize = zxc.seekTableSize(compSizes.length);
        const table = zxc.writeSeekTable(compSizes);
        assert(table.length === tableSize, `writeSeekTable bytes=${table.length} (expected ${tableSize})`);
    }

    // --- 11. Dictionary API ----------------------------------------------
    console.log('\n11. Dictionary API');
    {
        const { default: createZXC } = await import('./zxc_wasm.js');
        const zxc = await createZXC({}, ZXCModule);

        // Build a corpus of similar JSON-like samples so training has shared
        // structure to extract.
        const samples = [];
        for (let k = 0; k < 16; k++) {
            const s = `{"id":${k},"type":"event","payload":{"user":"alice","action":"login","region":"eu-west"}}`;
            samples.push(new TextEncoder().encode(s));
        }

        const dict = zxc.trainDict(samples);
        assert(dict instanceof Uint8Array && dict.length > 0,
            `trainDict produced ${dict.length}-byte dictionary`);

        // Compress a fresh similar payload with the dict, decompress with it.
        const payload = new TextEncoder().encode(
            '{"id":999,"type":"event","payload":{"user":"alice","action":"login","region":"eu-west"}}');
        const compressed = zxc.compress(payload, { dict });
        assert(compressed.length > 0, `dict-compressed to ${compressed.length} bytes`);

        const decoded = zxc.decompress(compressed, { dict });
        assert(arraysEqual(decoded, payload), 'dict compress/decompress byte-exact roundtrip');

        // Decompressing a dict archive WITHOUT the dict must fail.
        let threwNoDict = false;
        try {
            zxc.decompress(compressed);
        } catch (_) {
            threwNoDict = true;
        }
        assert(threwNoDict, 'decompress of dict archive without dict throws');

        // ID consistency: dictId(content) == getDictId(archive) == dictGetId(.zxd).
        const idContent = zxc.dictId(dict);
        const idArchive = zxc.getDictId(compressed);
        assert(idContent !== 0, `dictId(content)=${idContent}`);
        assert(idContent === idArchive,
            `dictId == getDictId(archive) (${idContent} == ${idArchive})`);

        // dictSave -> dictGetId / dictLoad roundtrip.
        const zxd = zxc.dictSave(dict);
        assert(zxd instanceof Uint8Array && zxd.length > dict.length,
            `dictSave produced ${zxd.length}-byte .zxd`);
        const idZxd = zxc.dictGetId(zxd);
        assert(idContent === idZxd,
            `dictId == dictGetId(dictSave(content)) (${idContent} == ${idZxd})`);

        const loaded = zxc.dictLoad(zxd);
        assert(loaded.id === idContent, `dictLoad id matches (${loaded.id})`);
        assert(arraysEqual(loaded.content, dict), 'dictSave -> dictLoad content roundtrip');

        // Seekable + setDict + range roundtrip on a dict-compressed archive.
        const bigPayload = new Uint8Array(48 * 1024);
        {
            const unit = new TextEncoder().encode(
                '{"user":"alice","action":"login","region":"eu-west"}');
            for (let i = 0; i < bigPayload.length; i++) bigPayload[i] = unit[i % unit.length];
        }
        const seekArchive = zxc.compress(bigPayload, { dict, seekable: true });
        const s = zxc.createSeekable(seekArchive);
        try {
            s.setDict(dict);
            const full = s.decompressRange(0, bigPayload.length);
            assert(arraysEqual(full, bigPayload), 'seekable+dict full-range byte-exact');
            const off = 4096, len = 8192;
            const mid = s.decompressRange(off, len);
            assert(arraysEqual(mid, bigPayload.subarray(off, off + len)),
                'seekable+dict mid-range byte-exact');
        } finally {
            s.free();
        }
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
