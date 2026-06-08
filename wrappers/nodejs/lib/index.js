/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

'use strict';

const { Transform } = require('node:stream');
const native = require('../build/Release/zxc_nodejs.node');

// Re-export compression level constants
const LEVEL_FASTEST  = native.LEVEL_FASTEST;
const LEVEL_FAST     = native.LEVEL_FAST;
const LEVEL_DEFAULT  = native.LEVEL_DEFAULT;
const LEVEL_BALANCED = native.LEVEL_BALANCED;
const LEVEL_COMPACT  = native.LEVEL_COMPACT;
const LEVEL_DENSITY  = native.LEVEL_DENSITY;

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
const ERROR_BAD_BLOCK_SIZE = native.ERROR_BAD_BLOCK_SIZE;

/**
 * Returns the minimum supported compression level (currently 1).
 * @returns {number}
 */
function minLevel() {
    return native.minLevel();
}

/**
 * Returns the maximum supported compression level (currently 6).
 * @returns {number}
 */
function maxLevel() {
    return native.maxLevel();
}

/**
 * Returns the default compression level (currently 3).
 * @returns {number}
 */
function defaultLevel() {
    return native.defaultLevel();
}

/**
 * Returns the version string reported by the linked native libzxc
 * (e.g. "0.11.0"). Distinct from the npm package version.
 * @returns {string}
 */
function libraryVersion() {
    return native.libraryVersion();
}

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
 * Normalize an optional dictionary value into a Buffer (or undefined).
 * Accepts a Buffer or Uint8Array; rejects other truthy types.
 * @param {Buffer|Uint8Array|undefined|null} dict
 * @returns {Buffer|undefined}
 */
function toDictBuffer(dict) {
    if (dict === undefined || dict === null) return undefined;
    if (Buffer.isBuffer(dict)) return dict;
    if (dict instanceof Uint8Array) return Buffer.from(dict.buffer, dict.byteOffset, dict.byteLength);
    throw new TypeError('dict must be a Buffer or Uint8Array');
}

/**
 * Train a pre-trained dictionary from a corpus of sample Buffers.
 *
 * @param {Array<Buffer|Uint8Array>} samples - Non-empty array of sample buffers.
 * @param {number} [maxSize=65535] - Maximum dictionary content size in bytes.
 * @returns {Buffer} Raw dictionary content suitable for `options.dict`.
 */
function trainDict(samples, maxSize = native.DICT_SIZE_MAX) {
    if (!Array.isArray(samples) || samples.length === 0) {
        throw new TypeError('samples must be a non-empty array of Buffers');
    }
    const bufs = samples.map((s) => {
        if (Buffer.isBuffer(s)) return s;
        if (s instanceof Uint8Array) return Buffer.from(s.buffer, s.byteOffset, s.byteLength);
        throw new TypeError('samples entries must be Buffers or Uint8Arrays');
    });
    return native.trainDict(bufs, maxSize);
}

/**
 * Compute the 32-bit dictionary ID for raw dictionary content.
 * @param {Buffer} content
 * @returns {number}
 */
function dictId(content) {
    if (!Buffer.isBuffer(content)) {
        throw new TypeError('content must be a Buffer');
    }
    return native.dictId(content);
}

/**
 * Returns the dictionary ID referenced by a `.zxc` archive, or 0 if none.
 * @param {Buffer} archive
 * @returns {number}
 */
function getDictId(archive) {
    if (!Buffer.isBuffer(archive)) {
        throw new TypeError('archive must be a Buffer');
    }
    return native.getDictId(archive);
}

/**
 * Returns the dictionary ID stored in a `.zxd` dictionary file, or 0 if invalid.
 * @param {Buffer} zxd
 * @returns {number}
 */
function dictGetId(zxd) {
    if (!Buffer.isBuffer(zxd)) {
        throw new TypeError('zxd must be a Buffer');
    }
    return native.dictGetId(zxd);
}

/**
 * Serialize raw dictionary content into the `.zxd` file format.
 * @param {Buffer} content
 * @returns {Buffer} The encoded `.zxd` file.
 */
function dictSave(content) {
    if (!Buffer.isBuffer(content)) {
        throw new TypeError('content must be a Buffer');
    }
    return native.dictSave(content);
}

/**
 * Load and validate a `.zxd` dictionary file.
 * @param {Buffer} zxd
 * @returns {{ content: Buffer, id: number }}
 */
function dictLoad(zxd) {
    if (!Buffer.isBuffer(zxd)) {
        throw new TypeError('zxd must be a Buffer');
    }
    return native.dictLoad(zxd);
}

/**
 * Compress a Buffer using the ZXC algorithm.
 *
 * @param {Buffer} data - Buffer to compress.
 * @param {object} [options] - Compression options.
 * @param {number} [options.level=LEVEL_DEFAULT] - Compression level (1-6).
 * @param {boolean} [options.checksum=false] - Enable checksum verification.
 * @param {boolean} [options.seekable=false] - Enable seek table for random-access decompression.
 * @param {Buffer|Uint8Array} [options.dict] - Pre-trained dictionary content (raw bytes).
 * @returns {Buffer} Compressed data.
 */
function compress(data, options = {}) {
    if (!Buffer.isBuffer(data)) {
        throw new TypeError('data must be a Buffer');
    }

    const level = options.level !== undefined ? options.level : LEVEL_DEFAULT;
    const checksum = options.checksum !== undefined ? options.checksum : false;
    const seekable = options.seekable !== undefined ? options.seekable : false;
    const dict = toDictBuffer(options.dict);

    return native.compress(data, level, checksum, seekable, dict);
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
 * @param {Buffer|Uint8Array} [options.dict] - Pre-trained dictionary content (raw bytes).
 * @returns {Buffer} Decompressed data.
 */
function decompress(data, options = {}) {
    if (!Buffer.isBuffer(data)) {
        throw new TypeError('data must be a Buffer');
    }

    const checksum = options.checksum !== undefined ? options.checksum : false;
    const dict = toDictBuffer(options.dict);

    let size = options.size;
    if (size === undefined) {
        size = getDecompressedSize(data);
    }

    return native.decompress(data, size, checksum, dict);
}

/**
 * Push-based, single-threaded compression stream.
 *
 * The Node.js counterpart of the C `zxc_cstream`. Use it when you cannot
 * block on a `FILE*` (event loops, web frameworks, network protocols).
 *
 * @example
 *   const cs = new zxc.CStream({ level: zxc.LEVEL_DEFAULT, checksum: true });
 *   const chunks = [];
 *   for await (const part of source) chunks.push(cs.compress(part));
 *   chunks.push(cs.end());
 *   cs.close();
 *   sink.write(Buffer.concat(chunks));
 */
const CStream = native.CStream;

/**
 * Push-based, single-threaded decompression stream.
 *
 * @example
 *   const ds = new zxc.DStream({ checksum: true });
 *   for await (const part of compressed) sink.write(ds.decompress(part));
 *   if (!ds.finished()) throw new Error('truncated');
 *   ds.close();
 */
const DStream = native.DStream;

/**
 * Handle on a seekable ZXC archive, suitable for random-access
 * decompression of byte ranges.
 *
 * The constructor takes a compressed Buffer (must carry an embedded seek
 * table - produce one by passing `{ seekable: true }` to `compress`). The
 * Buffer is copied into the addon's heap and kept alive for the lifetime
 * of the handle; call `.close()` to release both the native handle and
 * the copy.
 *
 * @example
 *   const s = new zxc.Seekable(compressedBuf);
 *   try {
 *     const slice = s.decompressRange(1024, 4096);
 *     console.log(`first 16 bytes: ${slice.subarray(0, 16).toString('hex')}`);
 *   } finally {
 *     s.close();
 *   }
 */
const Seekable = native.Seekable;

/**
 * Encoded byte size of a seek table covering `numBlocks` data blocks.
 * Use this to size a destination buffer for {@link writeSeekTable}.
 * @param {number} numBlocks
 * @returns {number}
 */
function seekTableSize(numBlocks) {
    if (typeof numBlocks !== 'number' || numBlocks < 0) {
        throw new TypeError('numBlocks must be a non-negative number');
    }
    return native.seekTableSize(numBlocks);
}

/**
 * Low-level: write a seek table (header + entries) for the given
 * per-block on-disk compressed sizes.
 *
 * Most callers do not need this directly; the buffer and streaming APIs
 * emit a seek table when `seekable: true` is set.
 *
 * @param {number[]} compSizes - Per-block compressed sizes, in order.
 * @returns {Buffer} The encoded seek table.
 */
function writeSeekTable(compSizes) {
    if (!Array.isArray(compSizes) || compSizes.length === 0) {
        throw new TypeError('compSizes must be a non-empty array of numbers');
    }
    return native.writeSeekTable(compSizes);
}

// =============================================================================
// stream.Transform adapters over the push streaming API
// =============================================================================


/**
 * Returns true if `buf` starts with the ZXC file magic word.
 *
 * Useful for content-type sniffing in containers / object stores that need
 * to dispatch on media type (e.g. OCI). Cheap and side-effect free; does
 * not validate the rest of the header or the footer.
 *
 * Magic word identifying a ZXC file frame: little-endian 0x9CB02EF5.
 * 
 * @param {Buffer|Uint8Array} buf
 * @returns {boolean}
 */
function detectZxc(buf) {
    if (!buf || buf.length < 4) return false;
    return buf[0] === 0xF5 && buf[1] === 0x2E && buf[2] === 0xB0 && buf[3] === 0x9C;
}

/**
 * A Node.js `stream.Transform` that compresses bytes through a ZXC frame.
 *
 * @example
 *   const fs = require('node:fs');
 *   const { pipeline } = require('node:stream/promises');
 *   await pipeline(
 *     fs.createReadStream('input.bin'),
 *     zxc.createCompressStream({ level: zxc.LEVEL_DEFAULT }),
 *     fs.createWriteStream('output.zxc'),
 *   );
 */
class CompressStream extends Transform {
    constructor(options = {}) {
        const { level, checksum, blockSize, ...transformOpts } = options;
        super(transformOpts);
        this._cs = new CStream({
            ...(level !== undefined ? { level } : {}),
            ...(checksum !== undefined ? { checksum } : {}),
            ...(blockSize !== undefined ? { blockSize } : {}),
        });
    }

    _transform(chunk, encoding, callback) {
        try {
            const buf = Buffer.isBuffer(chunk)
                ? chunk
                : Buffer.from(chunk, typeof encoding === 'string' ? encoding : 'utf8');
            const out = this._cs.compress(buf);
            if (out.length > 0) this.push(out);
            callback();
        } catch (err) {
            callback(err);
        }
    }

    _flush(callback) {
        try {
            const tail = this._cs.end();
            if (tail.length > 0) this.push(tail);
            this._cs.close();
            callback();
        } catch (err) {
            callback(err);
        }
    }

    _destroy(err, callback) {
        try { this._cs.close(); } catch (_) { /* idempotent */ }
        callback(err);
    }
}

/**
 * A Node.js `stream.Transform` that decompresses a ZXC frame.
 *
 * Emits `'error'` with code `'ZXC_TRUNCATED'` if the input ends before the
 * footer is reached.
 *
 * @example
 *   const fs = require('node:fs');
 *   const { pipeline } = require('node:stream/promises');
 *   await pipeline(
 *     fs.createReadStream('input.zxc'),
 *     zxc.createDecompressStream(),
 *     fs.createWriteStream('output.bin'),
 *   );
 */
class DecompressStream extends Transform {
    constructor(options = {}) {
        const { checksum, ...transformOpts } = options;
        super(transformOpts);
        this._ds = new DStream({
            ...(checksum !== undefined ? { checksum } : {}),
        });
    }

    _transform(chunk, encoding, callback) {
        try {
            const buf = Buffer.isBuffer(chunk)
                ? chunk
                : Buffer.from(chunk, typeof encoding === 'string' ? encoding : 'utf8');
            const out = this._ds.decompress(buf);
            if (out.length > 0) this.push(out);
            callback();
        } catch (err) {
            callback(err);
        }
    }

    _flush(callback) {
        try {
            if (!this._ds.finished()) {
                const err = new Error('zxc: input drained before footer (truncated frame)');
                err.code = 'ZXC_TRUNCATED';
                this._ds.close();
                callback(err);
                return;
            }
            this._ds.close();
            callback();
        } catch (err) {
            callback(err);
        }
    }

    _destroy(err, callback) {
        try { this._ds.close(); } catch (_) { /* idempotent */ }
        callback(err);
    }
}

/**
 * Factory matching the `zlib.createGzip()` convention.
 * @param {object} [options]
 * @returns {CompressStream}
 */
function createCompressStream(options) {
    return new CompressStream(options);
}

/**
 * Factory matching the `zlib.createGunzip()` convention.
 * @param {object} [options]
 * @returns {DecompressStream}
 */
function createDecompressStream(options) {
    return new DecompressStream(options);
}

module.exports = {
    // Functions
    compress,
    decompress,
    compressBound,
    getDecompressedSize,

    // Dictionary API
    trainDict,
    dictId,
    getDictId,
    dictGetId,
    dictSave,
    dictLoad,

    // Push streaming classes
    CStream,
    DStream,

    // Seekable random-access decompression
    Seekable,
    seekTableSize,
    writeSeekTable,

    // stream.Transform adapters
    CompressStream,
    DecompressStream,
    createCompressStream,
    createDecompressStream,
    detectZxc,

    // Library info helpers
    minLevel,
    maxLevel,
    defaultLevel,
    libraryVersion,

    // Constants
    LEVEL_FASTEST,
    LEVEL_FAST,
    LEVEL_DEFAULT,
    LEVEL_BALANCED,
    LEVEL_COMPACT,
    LEVEL_DENSITY,

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
    ERROR_BAD_BLOCK_SIZE,
};
