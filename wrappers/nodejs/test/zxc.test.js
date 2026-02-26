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

// =============================================================================
// Error Handling
// =============================================================================

describe('error handling', () => {
    test('error constants are defined', () => {
        expect(zxc.ERROR_MEMORY).toBe(-1);
        expect(zxc.ERROR_DST_TOO_SMALL).toBe(-2);
        expect(zxc.ERROR_SRC_TOO_SMALL).toBe(-3);
        expect(zxc.ERROR_BAD_MAGIC).toBe(-4);
        expect(zxc.ERROR_BAD_VERSION).toBe(-5);
        expect(zxc.ERROR_BAD_HEADER).toBe(-6);
        expect(zxc.ERROR_BAD_CHECKSUM).toBe(-7);
        expect(zxc.ERROR_CORRUPT_DATA).toBe(-8);
        expect(zxc.ERROR_BAD_OFFSET).toBe(-9);
        expect(zxc.ERROR_OVERFLOW).toBe(-10);
        expect(zxc.ERROR_IO).toBe(-11);
        expect(zxc.ERROR_NULL_INPUT).toBe(-12);
        expect(zxc.ERROR_BAD_BLOCK_TYPE).toBe(-13);
    });

    test('errorName works', () => {
        expect(zxc.errorName(zxc.ERROR_DST_TOO_SMALL)).toBe('ZXC_ERROR_DST_TOO_SMALL');
        expect(zxc.errorName(-999)).toBe('ZXC_UNKNOWN_ERROR');
    });

    test('thrown errors have code property', () => {
        try {
            zxc.decompress(Buffer.from([0x01, 0x02, 0x03]), { size: 100 });
            throw new Error('Should have thrown');
        } catch (err) {
            expect(err.code).toBe(zxc.ERROR_NULL_INPUT);
            expect(err.message).toBe('ZXC_ERROR_NULL_INPUT');
        }
    });

    test('detects bad magic with correct code', () => {
        try {
            // Provide enough bytes (16 for header) but fake magic
            const fakeHeader = Buffer.alloc(32);
            fakeHeader.write('FAKE', 0, 'utf8');
            zxc.decompress(fakeHeader, { size: 100 });
            throw new Error('Should have thrown');
        } catch (err) {
            expect(err.code).toBe(zxc.ERROR_BAD_HEADER);
        }
    });
});
