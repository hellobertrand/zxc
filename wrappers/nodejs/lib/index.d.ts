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

export interface CompressOptions {
    /** Compression level (1-5). Defaults to LEVEL_DEFAULT. */
    level?: number;
    /** Enable checksum verification. Defaults to false. */
    checksum?: boolean;
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
