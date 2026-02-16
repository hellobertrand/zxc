/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

'use strict';

const zxc = require('../lib/index');

// =============================================================================
// Roundtrip tests
// =============================================================================

describe('compress/decompress roundtrip', () => {
    const testCases = [
        { name: 'normal data',  data: Buffer.from('hello world'.repeat(10)) },
        { name: 'single byte',  data: Buffer.from('a') },
        { name: 'empty buffer', data: Buffer.alloc(0) },
        { name: 'large 10 MB',  data: Buffer.alloc(10_000_000, 0x42) },
    ];

    for (const { name, data } of testCases) {
        test(`roundtrip: ${name}`, () => {
            for (let level = zxc.LEVEL_FASTEST; level <= zxc.LEVEL_COMPACT; level++) {
                const compressed = zxc.compress(data, { level });
                const size = zxc.getDecompressedSize(compressed);
                const decompressed = zxc.decompress(compressed, { size });

                expect(decompressed.length).toBe(data.length);
                expect(Buffer.compare(decompressed, data)).toBe(0);
            }
        });
    }

    test('roundtrip with auto-size detection', () => {
        const data = Buffer.from('auto size detection test'.repeat(100));
        const compressed = zxc.compress(data);
        const decompressed = zxc.decompress(compressed);

        expect(decompressed.length).toBe(data.length);
        expect(Buffer.compare(decompressed, data)).toBe(0);
    });
});

// =============================================================================
// compressBound
// =============================================================================

describe('compressBound', () => {
    test('returns a value >= input size', () => {
        const bound = zxc.compressBound(1024);
        expect(bound).toBeGreaterThanOrEqual(1024);
    });

    test('returns 0-based bound for size 0', () => {
        const bound = zxc.compressBound(0);
        expect(bound).toBeGreaterThanOrEqual(0);
    });
});

// =============================================================================
// Corruption detection
// =============================================================================

describe('corruption detection', () => {
    test('detects corrupted data with checksum', () => {
        const data = Buffer.from('hello world'.repeat(10));
        const compressed = zxc.compress(data, { checksum: true });

        // Corrupt last byte
        const corrupted = Buffer.from(compressed);
        corrupted[corrupted.length - 1] ^= 0x01;

        expect(() => {
            zxc.decompress(corrupted, { size: data.length, checksum: true });
        }).toThrow();
    });

    test('throws on truncated compressed input', () => {
        expect(() => {
            zxc.decompress(Buffer.from([0x01, 0x02, 0x03]), { size: 100 });
        }).toThrow();
    });
});

// =============================================================================
// Invalid input types
// =============================================================================

describe('invalid input handling', () => {
    test('compress rejects non-Buffer', () => {
        expect(() => zxc.compress('string')).toThrow(TypeError);
        expect(() => zxc.compress(123)).toThrow(TypeError);
        expect(() => zxc.compress(null)).toThrow(TypeError);
    });

    test('decompress rejects non-Buffer', () => {
        expect(() => zxc.decompress('string')).toThrow(TypeError);
        expect(() => zxc.decompress(123)).toThrow(TypeError);
    });

    test('getDecompressedSize rejects non-Buffer', () => {
        expect(() => zxc.getDecompressedSize('string')).toThrow(TypeError);
    });

    test('compressBound rejects non-number', () => {
        expect(() => zxc.compressBound('abc')).toThrow(TypeError);
    });
});

// =============================================================================
// Constants
// =============================================================================

describe('constants', () => {
    test('compression levels are defined', () => {
        expect(zxc.LEVEL_FASTEST).toBe(1);
        expect(zxc.LEVEL_FAST).toBe(2);
        expect(zxc.LEVEL_DEFAULT).toBe(3);
        expect(zxc.LEVEL_BALANCED).toBe(4);
        expect(zxc.LEVEL_COMPACT).toBe(5);
    });
});
