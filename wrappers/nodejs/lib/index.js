/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

'use strict';

const native = require('../build/Release/zxc_nodejs.node');

// Re-export compression level constants
const LEVEL_FASTEST  = native.LEVEL_FASTEST;
const LEVEL_FAST     = native.LEVEL_FAST;
const LEVEL_DEFAULT  = native.LEVEL_DEFAULT;
const LEVEL_BALANCED = native.LEVEL_BALANCED;
const LEVEL_COMPACT  = native.LEVEL_COMPACT;

/**
 * Returns the maximum compressed size for a given input size.
 * Useful for pre-allocating output buffers.
 *
 * @param {number} inputSize - Size of the input data in bytes.
 * @returns {number} Maximum required buffer size in bytes.
 */
function compressBound(inputSize) {
    if (typeof inputSize !== 'number' || inputSize < 0) {
        throw new TypeError('inputSize must be a non-negative number');
    }
    return native.compressBound(inputSize);
}

/**
 * Compress a Buffer using the ZXC algorithm.
 *
 * @param {Buffer} data - Buffer to compress.
 * @param {object} [options] - Compression options.
 * @param {number} [options.level=LEVEL_DEFAULT] - Compression level (1-5).
 * @param {boolean} [options.checksum=false] - Enable checksum verification.
 * @returns {Buffer} Compressed data.
 */
function compress(data, options = {}) {
    if (!Buffer.isBuffer(data)) {
        throw new TypeError('data must be a Buffer');
    }

    const level = options.level !== undefined ? options.level : LEVEL_DEFAULT;
    const checksum = options.checksum !== undefined ? options.checksum : false;

    if (data.length === 0 && !checksum) {
        return Buffer.alloc(0);
    }

    return native.compress(data, level, checksum);
}

/**
 * Returns the original decompressed size from a ZXC compressed buffer.
 * Reads the footer without performing decompression.
 *
 * @param {Buffer} data - Compressed data buffer.
 * @returns {number} Original uncompressed size in bytes, or 0 if invalid.
 */
function getDecompressedSize(data) {
    if (!Buffer.isBuffer(data)) {
        throw new TypeError('data must be a Buffer');
    }
    return native.getDecompressedSize(data);
}

/**
 * Decompress a ZXC compressed Buffer.
 *
 * @param {Buffer} data - Compressed data.
 * @param {object} [options] - Decompression options.
 * @param {number} [options.size] - Expected decompressed size. If omitted, read from header.
 * @param {boolean} [options.checksum=false] - Enable checksum verification.
 * @returns {Buffer} Decompressed data.
 */
function decompress(data, options = {}) {
    if (!Buffer.isBuffer(data)) {
        throw new TypeError('data must be a Buffer');
    }

    const checksum = options.checksum !== undefined ? options.checksum : false;

    if (data.length === 0 && !checksum) {
        return Buffer.alloc(0);
    }

    let size = options.size;
    if (size === undefined) {
        size = getDecompressedSize(data);
        if (size === 0) {
            throw new Error('Invalid ZXC header or data too short to determine size');
        }
    }

    return native.decompress(data, size, checksum);
}

module.exports = {
    // Functions
    compress,
    decompress,
    compressBound,
    getDecompressedSize,

    // Constants
    LEVEL_FASTEST,
    LEVEL_FAST,
    LEVEL_DEFAULT,
    LEVEL_BALANCED,
    LEVEL_COMPACT,
};
