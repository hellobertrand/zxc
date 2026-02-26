/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! Low-level FFI bindings to the ZXC compression library.
//!
//! This crate provides raw, unsafe bindings to the ZXC C library.
//! For a safe, idiomatic Rust API, use the `zxc` crate instead.
//!
//! # Example
//!
//! ```rust,ignore
//! use zxc_sys::*;
//!
//! unsafe {
//!     let bound = zxc_compress_bound(1024);
//!     // ... allocate buffer and compress
//! }
//! ```

#![allow(non_camel_case_types)]
#![allow(non_upper_case_globals)]

use std::ffi::c_int;
use std::os::raw::c_void;

// =============================================================================
// ZXC Version Constants
// =============================================================================

// Version constants - automatically extracted from zxc_constants.h by build.rs
const fn parse_version(s: &str) -> u32 {
    let bytes = s.as_bytes();
    let mut result = 0u32;
    let mut i = 0;
    while i < bytes.len() {
        let digit = bytes[i];
        if digit >= b'0' && digit <= b'9' {
            result = result * 10 + (digit - b'0') as u32;
        }
        i += 1;
    }
    result
}

pub const ZXC_VERSION_MAJOR: u32 = parse_version(env!("ZXC_VERSION_MAJOR"));
pub const ZXC_VERSION_MINOR: u32 = parse_version(env!("ZXC_VERSION_MINOR"));
pub const ZXC_VERSION_PATCH: u32 = parse_version(env!("ZXC_VERSION_PATCH"));

// =============================================================================
// Compression Levels
// =============================================================================

// Helper function to parse integer from string literal at compile time
const fn parse_i32(s: &str) -> i32 {
    let bytes = s.as_bytes();
    let mut result = 0i32;
    let mut i = 0;
    let mut sign = 1;

    if i < bytes.len() && bytes[i] == b'-' {
        sign = -1;
        i += 1;
    }

    while i < bytes.len() {
        let digit = bytes[i];
        if digit >= b'0' && digit <= b'9' {
            result = result * 10 + (digit - b'0') as i32;
        }
        i += 1;
    }
    result * sign
}

// Compression level constants - automatically extracted from zxc_constants.h by build.rs
/// Fastest compression, best for real-time applications
pub const ZXC_LEVEL_FASTEST: i32 = parse_i32(env!("ZXC_LEVEL_FASTEST"));

/// Fast compression, good for real-time applications
pub const ZXC_LEVEL_FAST: i32 = parse_i32(env!("ZXC_LEVEL_FAST"));

/// Recommended: ratio > LZ4, decode speed > LZ4
pub const ZXC_LEVEL_DEFAULT: i32 = parse_i32(env!("ZXC_LEVEL_DEFAULT"));

/// Good ratio, good decode speed
pub const ZXC_LEVEL_BALANCED: i32 = parse_i32(env!("ZXC_LEVEL_BALANCED"));

/// High density. Best for storage/firmware/assets.
pub const ZXC_LEVEL_COMPACT: i32 = parse_i32(env!("ZXC_LEVEL_COMPACT"));

// =============================================================================
// Error Codes
// =============================================================================

/// Success (no error)
pub const ZXC_OK: i32 = 0;

/// Memory allocation failure
pub const ZXC_ERROR_MEMORY: i32 = -1;

/// Destination buffer too small
pub const ZXC_ERROR_DST_TOO_SMALL: i32 = -2;

/// Source buffer too small or truncated input
pub const ZXC_ERROR_SRC_TOO_SMALL: i32 = -3;

/// Invalid magic word in file header
pub const ZXC_ERROR_BAD_MAGIC: i32 = -4;

/// Unsupported file format version
pub const ZXC_ERROR_BAD_VERSION: i32 = -5;

/// Corrupted or invalid header (CRC mismatch)
pub const ZXC_ERROR_BAD_HEADER: i32 = -6;

/// Block or global checksum verification failed
pub const ZXC_ERROR_BAD_CHECKSUM: i32 = -7;

/// Corrupted compressed data
pub const ZXC_ERROR_CORRUPT_DATA: i32 = -8;

/// Invalid match offset during decompression
pub const ZXC_ERROR_BAD_OFFSET: i32 = -9;

/// Buffer overflow detected during processing
pub const ZXC_ERROR_OVERFLOW: i32 = -10;

/// Read/write/seek failure on file
pub const ZXC_ERROR_IO: i32 = -11;

/// Required input pointer is NULL
pub const ZXC_ERROR_NULL_INPUT: i32 = -12;

/// Unknown or unexpected block type
pub const ZXC_ERROR_BAD_BLOCK_TYPE: i32 = -13;

// =============================================================================
// Buffer-Based API
// =============================================================================

unsafe extern "C" {
    /// Calculates the maximum compressed size for a given input.
    ///
    /// Useful for allocating output buffers before compression.
    pub fn zxc_compress_bound(input_size: usize) -> u64;

    /// Compresses a data buffer using the ZXC algorithm.
    ///
    /// # Safety
    ///
    /// - `src` must be a valid pointer to `src_size` bytes.
    /// - `dst` must be a valid pointer to at least `dst_capacity` bytes.
    /// - The caller must ensure no data races on the buffers.
    ///
    /// # Returns
    ///
    /// Number of bytes written to `dst` (>0 on success), or a negative error code.
    pub fn zxc_compress(
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        level: c_int,
        checksum_enabled: c_int,
    ) -> i64;

    /// Decompresses a ZXC compressed buffer.
    ///
    /// # Safety
    ///
    /// - `src` must be a valid pointer to `src_size` bytes of compressed data.
    /// - `dst` must be a valid pointer to at least `dst_capacity` bytes.
    ///
    /// # Returns
    ///
    /// Number of decompressed bytes written to `dst` (>0 on success), or a negative error code.
    pub fn zxc_decompress(
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        checksum_enabled: c_int,
    ) -> i64;

    /// Returns the decompressed size stored in a ZXC compressed buffer.
    ///
    /// Reads the file footer without performing decompression.
    ///
    /// # Returns
    ///
    /// Original uncompressed size in bytes, or 0 if invalid.
    pub fn zxc_get_decompressed_size(src: *const c_void, src_size: usize) -> u64;

    /// Returns a human-readable name for the given error code.
    ///
    /// # Arguments
    ///
    /// * `code` - An error code from zxc_error_t (or any integer)
    ///
    /// # Returns
    ///
    /// A constant string such as "ZXC_OK" or "ZXC_ERROR_MEMORY".
    /// Returns "ZXC_UNKNOWN_ERROR" for unrecognized codes.
    pub fn zxc_error_name(code: c_int) -> *const std::os::raw::c_char;
}

// =============================================================================
// Streaming API (FILE-based)
// =============================================================================

unsafe extern "C" {
    /// Compresses data from an input stream to an output stream.
    ///
    /// Uses a multi-threaded pipeline architecture for high throughput
    /// on large files.
    ///
    /// # Safety
    ///
    /// - `f_in` must be a valid FILE* opened in "rb" mode
    /// - `f_out` must be a valid FILE* opened in "wb" mode
    /// - File handles must remain valid for the duration of the call
    ///
    /// # Arguments
    ///
    /// * `f_in` - Input file stream
    /// * `f_out` - Output file stream  
    /// * `n_threads` - Number of worker threads (0 = auto-detect CPU cores)
    /// * `level` - Compression level (1-5)
    /// * `checksum_enabled` - If non-zero, enables checksum verification
    ///
    /// # Returns
    ///
    /// Total compressed bytes written, or -1 on error.
    pub fn zxc_stream_compress(
        f_in: *mut libc::FILE,
        f_out: *mut libc::FILE,
        n_threads: c_int,
        level: c_int,
        checksum_enabled: c_int,
    ) -> i64;

    /// Decompresses data from an input stream to an output stream.
    ///
    /// Uses the same pipeline architecture as compression for maximum throughput.
    ///
    /// # Safety
    ///
    /// - `f_in` must be a valid FILE* opened in "rb" mode
    /// - `f_out` must be a valid FILE* opened in "wb" mode
    ///
    /// # Arguments
    ///
    /// * `f_in` - Input file stream (compressed data)
    /// * `f_out` - Output file stream (decompressed data)
    /// * `n_threads` - Number of worker threads (0 = auto-detect)
    /// * `checksum_enabled` - If non-zero, verifies checksums
    ///
    /// # Returns
    ///
    /// Total decompressed bytes written, or -1 on error.
    pub fn zxc_stream_decompress(
        f_in: *mut libc::FILE,
        f_out: *mut libc::FILE,
        n_threads: c_int,
        checksum_enabled: c_int,
    ) -> i64;

    /// Returns the decompressed size stored in a ZXC compressed file.
    ///
    /// Reads the file footer to extract the original size without decompressing.
    /// The file position is restored after reading.
    ///
    /// # Safety
    ///
    /// - `f_in` must be a valid FILE* opened in "rb" mode
    ///
    /// # Returns
    ///
    /// Original uncompressed size in bytes, or -1 on error.
    pub fn zxc_stream_get_decompressed_size(f_in: *mut libc::FILE) -> i64;
}

// =============================================================================
// Tests
// =============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compress_bound() {
        unsafe {
            let bound = zxc_compress_bound(1024);
            // Should return a reasonable bound (input + overhead)
            assert!(bound > 1024);
            assert!(bound < 1024 * 2); // Should not be excessively large
        }
    }

    #[test]
    fn test_roundtrip() {
        // Use highly repetitive data that definitely compresses well
        let input: Vec<u8> = (0..4096)
            .map(|i| ((i % 16) as u8).wrapping_add(b'A'))
            .collect();

        unsafe {
            // Allocate compression buffer
            let bound = zxc_compress_bound(input.len()) as usize;
            let mut compressed = vec![0u8; bound];

            // Compress
            let compressed_size = zxc_compress(
                input.as_ptr() as *const c_void,
                input.len(),
                compressed.as_mut_ptr() as *mut c_void,
                compressed.len(),
                ZXC_LEVEL_DEFAULT,
                1, // checksum enabled
            );
            assert!(compressed_size > 0, "Compression failed");
            // Highly repetitive data should compress significantly
            assert!((compressed_size as usize) < input.len() / 2, "Data should compress well");

            // Get decompressed size
            let decompressed_size = zxc_get_decompressed_size(
                compressed.as_ptr() as *const c_void,
                compressed_size as usize,
            );
            assert_eq!(decompressed_size as usize, input.len());

            // Decompress
            let mut decompressed = vec![0u8; decompressed_size as usize];
            let result_size = zxc_decompress(
                compressed.as_ptr() as *const c_void,
                compressed_size as usize,
                decompressed.as_mut_ptr() as *mut c_void,
                decompressed.len(),
                1, // checksum enabled
            );
            assert_eq!(result_size, input.len() as i64);
            assert_eq!(&decompressed[..], &input[..]);
        }
    }

    #[test]
    fn test_all_levels() {
        let input = b"Test data for all compression levels - with some repetition \
                      to ensure compression works: BBBBBBBBBBBBBBBBBBBBBBBBBBBBB";

        for level in [
            ZXC_LEVEL_FASTEST,
            ZXC_LEVEL_FAST,
            ZXC_LEVEL_DEFAULT,
            ZXC_LEVEL_BALANCED,
            ZXC_LEVEL_COMPACT,
        ] {
            unsafe {
                let bound = zxc_compress_bound(input.len()) as usize;
                let mut compressed = vec![0u8; bound];

                let compressed_size = zxc_compress(
                    input.as_ptr() as *const c_void,
                    input.len(),
                    compressed.as_mut_ptr() as *mut c_void,
                    compressed.len(),
                    level,
                    1,
                );
                assert!(
                    compressed_size > 0,
                    "Compression failed at level {}",
                    level
                );
            }
        }
    }
}
