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

// Re-export error constants
const ERROR_MEMORY         = native.ERROR_MEMORY;
const ERROR_DST_TOO_SMALL  = native.ERROR_DST_TOO_SMALL;
const ERROR_SRC_TOO_SMALL  = native.ERROR_SRC_TOO_SMALL;
const ERROR_BAD_MAGIC      = native.ERROR_BAD_MAGIC;
const ERROR_BAD_VERSION    = native.ERROR_BAD_VERSION;
const ERROR_BAD_HEADER     = native.ERROR_BAD_HEADER;
const ERROR_BAD_CHECKSUM   = native.ERROR_BAD_CHECKSUM;
const ERROR_CORRUPT_DATA   = native.ERROR_CORRUPT_DATA;
const ERROR_BAD_OFFSET     = native.ERROR_BAD_OFFSET;
const ERROR_OVERFLOW       = native.ERROR_OVERFLOW;
const ERROR_IO             = native.ERROR_IO;
const ERROR_NULL_INPUT     = native.ERROR_NULL_INPUT;
const ERROR_BAD_BLOCK_TYPE = native.ERROR_BAD_BLOCK_TYPE;

/**
 * Returns a human-readable name for a given error code.
 *
 * @param {number} code - ZXC error code.
 * @returns {string} Error name (e.g., "ZXC_ERROR_DST_TOO_SMALL").
 */
function errorName(code) {
    if (typeof code !== 'number') {
        throw new TypeError('code must be a number');
    }
    return native.errorName(code);
}

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

    // Error handling
    errorName,
    ERROR_MEMORY,
    ERROR_DST_TOO_SMALL,
    ERROR_SRC_TOO_SMALL,
    ERROR_BAD_MAGIC,
    ERROR_BAD_VERSION,
    ERROR_BAD_HEADER,
    ERROR_BAD_CHECKSUM,
    ERROR_CORRUPT_DATA,
    ERROR_BAD_OFFSET,
    ERROR_OVERFLOW,
    ERROR_IO,
    ERROR_NULL_INPUT,
    ERROR_BAD_BLOCK_TYPE,
};
