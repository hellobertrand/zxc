/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
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
//! let compressed = compress(data, Level::Default, None).expect("compression failed");
//!
//! // Decompress it back
//! let decompressed = decompress(&compressed).expect("decompression failed");
//! assert_eq!(&decompressed[..], &data[..]);
//! ```
//!
//! # Compression Levels
//!
//! ZXC provides 6 compression levels trading off speed vs ratio:
//!
//! | Level | Speed | Ratio | Use Case |
//! |-------|-------|-------|----------|
//! | `Fastest` | ★★★★★ | ★★☆☆☆ | Real-time, gaming |
//! | `Fast` | ★★★★☆ | ★★★☆☆ | Network, streaming |
//! | `Default` | ★★★☆☆ | ★★★★☆ | General purpose |
//! | `Balanced` | ★★☆☆☆ | ★★★★☆ | Archives |
//! | `Compact` | ★☆☆☆☆ | ★★★★★ | Storage, firmware |
//! | `Density` | ★☆☆☆☆ | ★★★★★ | Maximum density (Huffman literals + optimal parser) |
//!
//! # Features
//!
//! - **Checksum verification**: Optional, disabled by default for maximum performance
//! - **Zero-copy decompression bound**: Query the output size before decompressing

#![warn(missing_docs)]
#![warn(rust_2018_idioms)]

pub use zxc_sys::{
    ZXC_LEVEL_BALANCED, ZXC_LEVEL_COMPACT, ZXC_LEVEL_DEFAULT, ZXC_LEVEL_DENSITY, ZXC_LEVEL_FAST,
    ZXC_LEVEL_FASTEST, ZXC_VERSION_MAJOR, ZXC_VERSION_MINOR, ZXC_VERSION_PATCH,
    // Error codes
    ZXC_OK, ZXC_ERROR_MEMORY, ZXC_ERROR_DST_TOO_SMALL, ZXC_ERROR_SRC_TOO_SMALL,
    ZXC_ERROR_BAD_MAGIC, ZXC_ERROR_BAD_VERSION, ZXC_ERROR_BAD_HEADER,
    ZXC_ERROR_BAD_CHECKSUM, ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_BAD_OFFSET,
    ZXC_ERROR_OVERFLOW, ZXC_ERROR_IO, ZXC_ERROR_NULL_INPUT, ZXC_ERROR_BAD_BLOCK_TYPE,
    ZXC_ERROR_BAD_BLOCK_SIZE,
};

// =============================================================================
// Compression Levels
// =============================================================================

/// Compression level presets.
///
/// Higher levels produce smaller output but compress more slowly.
/// Decompression speed is similar across most levels; level 6 sits a notch
/// below the others because Huffman-coded literals add a per-block decode
/// cost relative to RAW/RLE literals.
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

    /// High density: storage / firmware / assets (level 5)
    Compact = 5,

    /// Maximum density: Huffman-coded literals on top of COMPACT plus a
    /// price-based optimal LZ77 parser. Slowest compression, best ratio
    /// (level 6).
    Density = 6,
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
            Level::Density,
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

    /// Enable seek table for random-access decompression (default: `false`)
    pub seekable: bool,
}

impl Default for CompressOptions {
    fn default() -> Self {
        Self {
            level: Level::Default,
            checksum: true,
            seekable: false,
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

    /// Enable seek table for random-access decompression.
    pub fn with_seekable(mut self) -> Self {
        self.seekable = true;
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
// Submodules
// =============================================================================

mod ctx;
mod error;
mod file;
mod oneshot;
mod pstream;
mod stdio;

pub use error::{Error, Result};
pub use oneshot::{
    compress, compress_bound, compress_to, compress_with_options, decompress, decompress_to,
    decompress_with_options, decompressed_size, default_level, max_level, min_level,
    runtime_version, version, version_string,
};
pub use ctx::{compress_block_bound, decompress_block_bound, Cctx, Dctx};
pub use file::{
    compress_file, decompress_file, file_decompressed_size, StreamCompressOptions,
    StreamDecompressOptions, StreamError, StreamResult,
};
pub use pstream::{CStream, CStreamProgress, DStream, DStreamProgress};
pub use stdio::{detect_zxc, Decoder, Encoder};
