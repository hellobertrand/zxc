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
