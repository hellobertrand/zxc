/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/** Fastest compression, best for real-time applications. */
export const LEVEL_FASTEST: number;
/** Fast compression, good for real-time applications. */
export const LEVEL_FAST: number;
/** Recommended: ratio > LZ4, decode speed > LZ4. */
export const LEVEL_DEFAULT: number;
/** Good ratio, good decode speed. */
export const LEVEL_BALANCED: number;
/** High density. Best for storage/firmware/assets. */
export const LEVEL_COMPACT: number;
/** Maximum density: Huffman-coded literals on top of COMPACT. */
export const LEVEL_DENSITY: number;

/** Memory allocation failure. */
export const ERROR_MEMORY: number;
/** Destination buffer too small. */
export const ERROR_DST_TOO_SMALL: number;
/** Source buffer too small or truncated input. */
export const ERROR_SRC_TOO_SMALL: number;
/** Invalid magic word in file header. */
export const ERROR_BAD_MAGIC: number;
/** Unsupported file format version. */
export const ERROR_BAD_VERSION: number;
/** Corrupted or invalid header (CRC mismatch). */
export const ERROR_BAD_HEADER: number;
/** Block or global checksum verification failed. */
export const ERROR_BAD_CHECKSUM: number;
/** Corrupted compressed data. */
export const ERROR_CORRUPT_DATA: number;
/** Invalid match offset during decompression. */
export const ERROR_BAD_OFFSET: number;
/** Buffer overflow detected during processing. */
export const ERROR_OVERFLOW: number;
/** Read/write/seek failure on file. */
export const ERROR_IO: number;
/** Required input pointer is NULL. */
export const ERROR_NULL_INPUT: number;
/** Unknown or unexpected block type. */
export const ERROR_BAD_BLOCK_TYPE: number;
/** Invalid block size. */
export const ERROR_BAD_BLOCK_SIZE: number;

export interface CompressOptions {
    /** Compression level (1-6). Defaults to LEVEL_DEFAULT. */
    level?: number;
    /** Enable checksum verification. Defaults to false. */
    checksum?: boolean;
    /** Enable seek table for random-access decompression. Defaults to false. */
    seekable?: boolean;
}

export interface DecompressOptions {
    /** Expected decompressed size. If omitted, read from header. */
    size?: number;
    /** Enable checksum verification. Defaults to false. */
    checksum?: boolean;
}

/**
 * Returns the maximum compressed size for a given input size.
 * Useful for pre-allocating output buffers.
 */
export function compressBound(inputSize: number): number;

/**
 * Compress a Buffer using the ZXC algorithm.
 */
export function compress(data: Buffer, options?: CompressOptions): Buffer;

/**
 * Returns the original decompressed size from a ZXC compressed buffer.
 * Reads the footer without performing decompression.
 */
export function getDecompressedSize(data: Buffer): number;

/**
 * Decompress a ZXC compressed Buffer.
 */
export function decompress(data: Buffer, options?: DecompressOptions): Buffer;

/**
 * Returns a human-readable name for a given error code.
 */
export function errorName(code: number): string;

/** Returns the minimum supported compression level (currently 1). */
export function minLevel(): number;

/** Returns the maximum supported compression level (currently 6). */
export function maxLevel(): number;

/** Returns the default compression level (currently 3). */
export function defaultLevel(): number;

/**
 * Returns the version string reported by the linked native libzxc
 * (e.g. "0.11.0"). Distinct from the npm package version.
 */
export function libraryVersion(): string;

export interface CStreamOptions {
    /** Compression level (1-6). Defaults to LEVEL_DEFAULT. */
    level?: number;
    /** Enable per-block and global checksums. Defaults to false. */
    checksum?: boolean;
    /** Block size in bytes (0 = default 512 KB). Power of 2, 4 KB – 2 MB. */
    blockSize?: number;
}

/**
 * Push-based, single-threaded compression stream.
 *
 * The Node.js counterpart of the C `zxc_cstream`. Each call to
 * `compress(buf)` returns the compressed bytes produced from that input
 * (may be empty if the bytes fit in the internal block accumulator).
 * Always call `end()` to flush the residual block, EOF marker and footer.
 */
export class CStream {
    constructor(options?: CStreamOptions);
    /** Push input and return any compressed bytes produced this call. */
    compress(data: Buffer): Buffer;
    /** Finalise the stream: residual block + EOF + file footer. */
    end(): Buffer;
    /** Release native resources. Idempotent. */
    close(): void;
    /** Suggested input chunk size in bytes. */
    inSize(): number;
    /** Suggested output chunk size in bytes. */
    outSize(): number;
}

export interface DStreamOptions {
    /** Verify per-block and global checksums when present. Defaults to false. */
    checksum?: boolean;
}

/**
 * Push-based, single-threaded decompression stream.
 */
export class DStream {
    constructor(options?: DStreamOptions);
    /** Push compressed bytes and return any decompressed bytes produced. */
    decompress(data: Buffer): Buffer;
    /** True once the decoder has reached and validated the file footer. */
    finished(): boolean;
    /** Release native resources. Idempotent. */
    close(): void;
    /** Suggested input chunk size in bytes. */
    inSize(): number;
    /** Suggested output chunk size in bytes. */
    outSize(): number;
}

// ---------- stream.Transform adapters ----------

import { Transform, TransformOptions } from 'node:stream';

/** Returns true if `buf` starts with the ZXC file magic word. */
export function detectZxc(buf: Buffer | Uint8Array): boolean;

export interface CompressStreamOptions extends TransformOptions, CStreamOptions {}
export interface DecompressStreamOptions extends TransformOptions, DStreamOptions {}

/**
 * `stream.Transform` that compresses bytes through a ZXC frame. Designed to
 * be piped between any Node Readable/Writable (fs, http, tar-stream, OCI
 * registry clients, etc.). Mirrors `zlib.createGzip()` ergonomics.
 */
export class CompressStream extends Transform {
    constructor(options?: CompressStreamOptions);
}

/**
 * `stream.Transform` that decompresses a ZXC frame. Emits `'error'` with
 * `code === 'ZXC_TRUNCATED'` if the input ends before the footer.
 */
export class DecompressStream extends Transform {
    constructor(options?: DecompressStreamOptions);
}

export function createCompressStream(options?: CompressStreamOptions): CompressStream;
export function createDecompressStream(options?: DecompressStreamOptions): DecompressStream;

// ---------- Seekable random-access decompression ----------

/**
 * Storage-agnostic reader interface for {@link Seekable.constructor}.
 *
 * Supply this when the archive is too large to fit in a Buffer, or when
 * it lives behind a custom backend (file descriptor, HTTP range, S3, etc.).
 * `readAt` is invoked synchronously on the calling thread; do not perform
 * async I/O here. Use `fs.readSync(fd, buf, 0, buf.length, offset)` or
 * similar.
 */
export interface SeekableReader {
    /** Total size of the compressed archive in bytes. */
    size: number;
    /**
     * Fill `buf` with `buf.length` bytes starting at `offset` in the
     * archive. Throwing causes the surrounding Seekable operation to
     * fail with an I/O error.
     */
    readAt(buf: Buffer, offset: number): void;
}

/**
 * Handle on a seekable ZXC archive.
 *
 * Two source modes:
 * - **Buffer**: pass a compressed Buffer carrying an embedded seek table
 *   (produced by `compress(..., { seekable: true })`). The Buffer is
 *   copied into the addon's heap.
 * - **Reader callback**: pass a {@link SeekableReader} to back the archive
 *   with any storage that supports positional reads (file descriptor,
 *   HTTP range, S3, custom VFS). `readAt` runs synchronously on the
 *   calling thread; this binding does not support multi-threaded
 *   decompression when a reader callback is used.
 *
 * Call `close()` to release native resources.
 *
 * A Seekable handle is single-threaded - do not share it across worker
 * threads.
 */
export class Seekable {
    constructor(compressed: Buffer);
    constructor(reader: SeekableReader);
    /** Total number of data blocks (excluding the EOF marker block). */
    numBlocks(): number;
    /** Total decompressed size of the archive in bytes. */
    decompressedSize(): number;
    /**
     * On-disk compressed size of a specific block (block header +
     * payload + optional per-block checksum). Returns `null` if
     * `blockIdx` is out of range.
     */
    blockCompressedSize(blockIdx: number): number | null;
    /**
     * Decompressed size of a specific block, or `null` if `blockIdx` is
     * out of range.
     */
    blockDecompressedSize(blockIdx: number): number | null;
    /**
     * Decompress `length` bytes starting at `offset` (in the original
     * uncompressed byte stream). Only the blocks overlapping the
     * requested range are read.
     */
    decompressRange(offset: number, length: number): Buffer;
    /** Release native resources. Idempotent. */
    close(): void;
}

/**
 * Encoded byte size of a seek table covering `numBlocks` data blocks.
 * Use this to size a destination buffer for {@link writeSeekTable}.
 */
export function seekTableSize(numBlocks: number): number;

/**
 * Low-level: write a seek table (header + entries) for the given
 * per-block on-disk compressed sizes. Most callers do not need this -
 * the buffer and streaming APIs emit a seek table automatically when
 * `seekable: true` is set.
 */
export function writeSeekTable(compSizes: number[]): Buffer;
