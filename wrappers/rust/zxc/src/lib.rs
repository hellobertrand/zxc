/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

//! Safe Rust bindings to the ZXC compression library.
//!
//! ZXC is a fast compression library optimized for high decompression speed.
//! This crate provides a safe, idiomatic Rust API.
//!
//! # Quick Start
//!
//! ```rust
//! use zxc::{compress, decompress, Level};
//!
//! // Compress some data
//! let data = b"Hello, ZXC! This is some data to compress.";
//! let compressed = compress(data, Level::Default).expect("compression failed");
//!
//! // Decompress it back
//! let decompressed = decompress(&compressed).expect("decompression failed");
//! assert_eq!(&decompressed[..], &data[..]);
//! ```
//!
//! # Compression Levels
//!
//! ZXC provides 5 compression levels trading off speed vs ratio:
//!
//! | Level | Speed | Ratio | Use Case |
//! |-------|-------|-------|----------|
//! | `Fastest` | ★★★★★ | ★★☆☆☆ | Real-time, gaming |
//! | `Fast` | ★★★★☆ | ★★★☆☆ | Network, streaming |
//! | `Default` | ★★★☆☆ | ★★★★☆ | General purpose |
//! | `Balanced` | ★★☆☆☆ | ★★★★☆ | Archives |
//! | `Compact` | ★☆☆☆☆ | ★★★★★ | Storage, firmware |
//!
//! # Features
//!
//! - **Checksum verification**: Enabled by default, can be disabled for performance
//! - **Zero-copy decompression bound**: Query the output size before decompressing

#![warn(missing_docs)]
#![warn(rust_2018_idioms)]

use std::ffi::c_void;

pub use zxc_sys::{
    ZXC_LEVEL_BALANCED, ZXC_LEVEL_COMPACT, ZXC_LEVEL_DEFAULT, ZXC_LEVEL_FAST, ZXC_LEVEL_FASTEST,
    ZXC_VERSION_MAJOR, ZXC_VERSION_MINOR, ZXC_VERSION_PATCH,
};

// =============================================================================
// Error Types
// =============================================================================

/// Errors that can occur during ZXC operations.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// Compression failed (output buffer too small or internal error)
    #[error("compression failed")]
    CompressionFailed,

    /// Decompression failed (invalid data, corruption, or buffer too small)
    #[error("decompression failed: {0}")]
    DecompressionFailed(&'static str),

    /// The compressed data appears to be invalid or truncated
    #[error("invalid compressed data")]
    InvalidData,
}

/// Result type for ZXC operations.
pub type Result<T> = std::result::Result<T, Error>;

// =============================================================================
// Compression Levels
// =============================================================================

/// Compression level presets.
///
/// Higher levels produce smaller output but compress more slowly.
/// Decompression speed is similar across all levels.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(i32)]
pub enum Level {
    /// Fastest compression, best for real-time applications (level 1)
    Fastest = 1,

    /// Fast compression, good for real-time applications (level 2)
    Fast = 2,

    /// Recommended default: ratio > LZ4, decode speed > LZ4 (level 3)
    #[default]
    Default = 3,

    /// Good ratio, good decode speed (level 4)
    Balanced = 4,

    /// Highest density. Best for storage/firmware/assets (level 5)
    Compact = 5,
}

impl Level {
    /// Returns all available compression levels.
    pub fn all() -> &'static [Level] {
        &[
            Level::Fastest,
            Level::Fast,
            Level::Default,
            Level::Balanced,
            Level::Compact,
        ]
    }
}

impl From<Level> for i32 {
    fn from(level: Level) -> i32 {
        level as i32
    }
}

// =============================================================================
// Compression Options
// =============================================================================

/// Options for compression operations.
#[derive(Debug, Clone)]
pub struct CompressOptions {
    /// Compression level (default: `Level::Default`)
    pub level: Level,

    /// Enable checksum for data integrity (default: `true`)
    pub checksum: bool,
}

impl Default for CompressOptions {
    fn default() -> Self {
        Self {
            level: Level::Default,
            checksum: true,
        }
    }
}

impl CompressOptions {
    /// Create options with the specified compression level.
    pub fn with_level(level: Level) -> Self {
        Self {
            level,
            ..Default::default()
        }
    }

    /// Disable checksum computation for faster compression.
    pub fn without_checksum(mut self) -> Self {
        self.checksum = false;
        self
    }
}

// =============================================================================
// Decompression Options
// =============================================================================

/// Options for decompression operations.
#[derive(Debug, Clone, Default)]
pub struct DecompressOptions {
    /// Verify checksum during decompression (default: `true`)
    pub verify_checksum: bool,
}

impl DecompressOptions {
    /// Create options that skip checksum verification.
    pub fn skip_checksum() -> Self {
        Self {
            verify_checksum: false,
        }
    }
}

// =============================================================================
// Public API
// =============================================================================

/// Returns the maximum compressed size for an input of the given size.
///
/// Use this to allocate a buffer before calling [`compress_to`].
///
/// # Example
///
/// ```rust
/// let bound = zxc::compress_bound(1024);
/// assert!(bound > 1024); // Accounts for headers and worst-case expansion
/// ```
#[inline]
pub fn compress_bound(input_size: usize) -> usize {
    unsafe { zxc_sys::zxc_compress_bound(input_size) }
}

/// Compresses data with the specified level.
///
/// This is a convenience function that allocates the output buffer automatically.
/// For zero-allocation usage, see [`compress_to`].
///
/// # Example
///
/// ```rust
/// use zxc::{compress, Level};
///
/// let data = b"Hello, world!";
/// let compressed = compress(data, Level::Default)?;
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn compress(data: &[u8], level: Level) -> Result<Vec<u8>> {
    compress_with_options(data, &CompressOptions::with_level(level))
}

/// Compresses data with full options control.
///
/// # Example
///
/// ```rust
/// use zxc::{compress_with_options, CompressOptions, Level};
///
/// let data = b"Hello, world!";
/// let opts = CompressOptions::with_level(Level::Compact).without_checksum();
/// let compressed = compress_with_options(data, &opts)?;
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn compress_with_options(data: &[u8], options: &CompressOptions) -> Result<Vec<u8>> {
    let bound = compress_bound(data.len());
    let mut output = vec![0u8; bound];

    let written = compress_to(data, &mut output, options)?;
    output.truncate(written);
    Ok(output)
}

/// Compresses data into a pre-allocated buffer.
///
/// Returns the number of bytes written to `output`.
///
/// # Errors
///
/// Returns [`Error::CompressionFailed`] if the output buffer is too small
/// or an internal error occurs.
///
/// # Example
///
/// ```rust
/// use zxc::{compress_to, compress_bound, CompressOptions};
///
/// let data = b"Hello, world!";
/// let mut output = vec![0u8; compress_bound(data.len())];
/// let size = compress_to(data, &mut output, &CompressOptions::default())?;
/// output.truncate(size);
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn compress_to(data: &[u8], output: &mut [u8], options: &CompressOptions) -> Result<usize> {
    let written = unsafe {
        zxc_sys::zxc_compress(
            data.as_ptr() as *const c_void,
            data.len(),
            output.as_mut_ptr() as *mut c_void,
            output.len(),
            options.level as i32,
            options.checksum as i32,
        )
    };

    if written == 0 && !data.is_empty() {
        return Err(Error::CompressionFailed);
    }

    Ok(written)
}

/// Returns the original uncompressed size from compressed data.
///
/// This reads the footer without performing decompression.
/// Returns `None` if the data is invalid or truncated.
///
/// # Example
///
/// ```rust
/// use zxc::{compress, decompressed_size, Level};
///
/// let data = b"Hello, world!";
/// let compressed = compress(data, Level::Default)?;
/// let size = decompressed_size(&compressed);
/// assert_eq!(size, Some(data.len()));
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn decompressed_size(compressed: &[u8]) -> Option<usize> {
    let size =
        unsafe { zxc_sys::zxc_get_decompressed_size(compressed.as_ptr() as *const c_void, compressed.len()) };

    if size == 0 && !compressed.is_empty() {
        None
    } else {
        Some(size)
    }
}

/// Decompresses ZXC-compressed data.
///
/// This is a convenience function that queries the output size and allocates
/// the buffer automatically. For zero-allocation usage, see [`decompress_to`].
///
/// # Example
///
/// ```rust
/// use zxc::{compress, decompress, Level};
///
/// let data = b"Hello, world!";
/// let compressed = compress(data, Level::Default)?;
/// let decompressed = decompress(&compressed)?;
/// assert_eq!(&decompressed[..], &data[..]);
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn decompress(compressed: &[u8]) -> Result<Vec<u8>> {
    decompress_with_options(compressed, &DecompressOptions::default())
}

/// Decompresses data with full options control.
pub fn decompress_with_options(compressed: &[u8], options: &DecompressOptions) -> Result<Vec<u8>> {
    let size = decompressed_size(compressed).ok_or(Error::InvalidData)?;

    let mut output = vec![0u8; size];
    let written = decompress_to(compressed, &mut output, options)?;

    if written != size {
        return Err(Error::DecompressionFailed("size mismatch"));
    }

    Ok(output)
}

/// Decompresses data into a pre-allocated buffer.
///
/// Returns the number of bytes written to `output`.
///
/// # Errors
///
/// Returns an error if decompression fails due to invalid data, corruption,
/// or insufficient output buffer size.
pub fn decompress_to(
    compressed: &[u8],
    output: &mut [u8],
    options: &DecompressOptions,
) -> Result<usize> {
    let written = unsafe {
        zxc_sys::zxc_decompress(
            compressed.as_ptr() as *const c_void,
            compressed.len(),
            output.as_mut_ptr() as *mut c_void,
            output.len(),
            if options.verify_checksum { 1 } else { 0 },
        )
    };

    if written == 0 && !compressed.is_empty() {
        return Err(Error::DecompressionFailed("invalid data or buffer too small"));
    }

    Ok(written)
}

/// Returns the library version as a tuple (major, minor, patch).
pub fn version() -> (u32, u32, u32) {
    (ZXC_VERSION_MAJOR, ZXC_VERSION_MINOR, ZXC_VERSION_PATCH)
}

/// Returns the library version as a string.
pub fn version_string() -> String {
    format!(
        "{}.{}.{}",
        ZXC_VERSION_MAJOR, ZXC_VERSION_MINOR, ZXC_VERSION_PATCH
    )
}

// =============================================================================
// Tests
// =============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_roundtrip() {
        let data = b"Hello, ZXC! This is a test of the safe Rust wrapper.";
        let compressed = compress(data, Level::Default).unwrap();
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn test_all_levels() {
        let data = b"Test data with repetition: CCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";

        for level in Level::all() {
            let compressed = compress(data, *level).unwrap();
            let decompressed = decompress(&compressed).unwrap();
            assert_eq!(
                &decompressed[..],
                &data[..],
                "Roundtrip failed at level {:?}",
                level
            );
        }
    }

    #[test]
    fn test_empty() {
        let data: &[u8] = b"";
        let compressed = compress(data, Level::Default).unwrap();
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(decompressed.len(), 0);
    }

    #[test]
    fn test_checksum_options() {
        let data = b"Test with and without checksum";

        // With checksum
        let opts_with = CompressOptions::with_level(Level::Default);
        let compressed = compress_with_options(data, &opts_with).unwrap();
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed[..], &data[..]);

        // Without checksum
        let opts_without = CompressOptions::with_level(Level::Fast).without_checksum();
        let compressed = compress_with_options(data, &opts_without).unwrap();
        let decompressed =
            decompress_with_options(&compressed, &DecompressOptions::skip_checksum()).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn test_decompressed_size() {
        let data = b"Hello, world! Testing decompressed_size function.";
        let compressed = compress(data, Level::Default).unwrap();
        let size = decompressed_size(&compressed);
        assert_eq!(size, Some(data.len()));
    }

    #[test]
    fn test_version() {
        let (major, minor, patch) = version();
        assert_eq!(major, 0);
        assert_eq!(minor, 6);
        assert_eq!(patch, 0);

        assert_eq!(version_string(), "0.6.0");
    }

    #[test]
    fn test_invalid_data() {
        let garbage = b"not valid zxc data";
        let result = decompress(garbage);
        assert!(result.is_err());
    }

    #[test]
    fn test_compress_to_buffer() {
        let data = b"Testing compress_to with pre-allocated buffer";
        let mut output = vec![0u8; compress_bound(data.len())];

        let size = compress_to(data, &mut output, &CompressOptions::default()).unwrap();
        output.truncate(size);

        let decompressed = decompress(&output).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn test_large_data() {
        // 1 MB of random-ish but compressible data
        let data: Vec<u8> = (0..1024 * 1024)
            .map(|i| ((i % 256) ^ ((i / 256) % 256)) as u8)
            .collect();

        let compressed = compress(&data, Level::Default).unwrap();
        assert!(compressed.len() < data.len()); // Should compress

        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(decompressed, data);
    }
}
