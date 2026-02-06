/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
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

pub const ZXC_VERSION_MAJOR: u32 = 0;
pub const ZXC_VERSION_MINOR: u32 = 6;
pub const ZXC_VERSION_PATCH: u32 = 0;

// =============================================================================
// Compression Levels
// =============================================================================

/// Fastest compression, best for real-time applications
pub const ZXC_LEVEL_FASTEST: c_int = 1;

/// Fast compression, good for real-time applications
pub const ZXC_LEVEL_FAST: c_int = 2;

/// Recommended: ratio > LZ4, decode speed > LZ4
pub const ZXC_LEVEL_DEFAULT: c_int = 3;

/// Good ratio, good decode speed
pub const ZXC_LEVEL_BALANCED: c_int = 4;

/// High density. Best for storage/firmware/assets.
pub const ZXC_LEVEL_COMPACT: c_int = 5;

// =============================================================================
// Buffer-Based API
// =============================================================================

unsafe extern "C" {
    /// Calculates the maximum compressed size for a given input.
    ///
    /// Useful for allocating output buffers before compression.
    pub fn zxc_compress_bound(input_size: usize) -> usize;

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
    /// Number of bytes written to `dst`, or 0 on error.
    pub fn zxc_compress(
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        level: c_int,
        checksum_enabled: c_int,
    ) -> usize;

    /// Decompresses a ZXC compressed buffer.
    ///
    /// # Safety
    ///
    /// - `src` must be a valid pointer to `src_size` bytes of compressed data.
    /// - `dst` must be a valid pointer to at least `dst_capacity` bytes.
    ///
    /// # Returns
    ///
    /// Number of decompressed bytes written to `dst`, or 0 on error.
    pub fn zxc_decompress(
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        checksum_enabled: c_int,
    ) -> usize;

    /// Returns the decompressed size stored in a ZXC compressed buffer.
    ///
    /// Reads the file footer without performing decompression.
    ///
    /// # Returns
    ///
    /// Original uncompressed size in bytes, or 0 if invalid.
    pub fn zxc_get_decompressed_size(src: *const c_void, src_size: usize) -> usize;
}

// =============================================================================
// Streaming API (FILE-based)
// =============================================================================

// Note: The streaming API uses FILE* pointers which are platform-specific.
// For now, we expose only the buffer-based API which is more portable.
// Streaming support can be added via Rust's std::io traits in the safe wrapper.

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
            let bound = zxc_compress_bound(input.len());
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
            assert!(compressed_size < input.len() / 2, "Data should compress well");

            // Get decompressed size
            let decompressed_size = zxc_get_decompressed_size(
                compressed.as_ptr() as *const c_void,
                compressed_size,
            );
            assert_eq!(decompressed_size, input.len());

            // Decompress
            let mut decompressed = vec![0u8; decompressed_size];
            let result_size = zxc_decompress(
                compressed.as_ptr() as *const c_void,
                compressed_size,
                decompressed.as_mut_ptr() as *mut c_void,
                decompressed.len(),
                1, // checksum enabled
            );
            assert_eq!(result_size, input.len());
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
                let bound = zxc_compress_bound(input.len());
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
