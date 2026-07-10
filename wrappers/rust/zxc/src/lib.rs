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
//! ZXC provides 7 compression levels trading off speed vs ratio:
//!
//! | Level | Speed | Ratio | Use Case |
//! |-------|-------|-------|----------|
//! | `Fastest` | ★★★★★ | ★★☆☆☆ | Real-time, gaming |
//! | `Fast` | ★★★★☆ | ★★★☆☆ | Network, streaming |
//! | `Default` | ★★★☆☆ | ★★★★☆ | General purpose |
//! | `Balanced` | ★★☆☆☆ | ★★★★☆ | Archives |
//! | `Compact` | ★☆☆☆☆ | ★★★★★ | Storage, firmware |
//! | `Density` | ★☆☆☆☆ | ★★★★★ | High density (Huffman literals + optimal parser) |
//! | `Ultra` | ★☆☆☆☆ | ★★★★★ | Maximum density (Huffman literals + tokens, deep parse) |
//!
//! # Features
//!
//! - **Checksum verification**: Optional, disabled by default for maximum performance
//! - **Zero-copy decompression bound**: Query the output size before decompressing

#![warn(missing_docs)]
#![warn(rust_2018_idioms)]

pub use zxc_sys::{
    ZXC_LEVEL_BALANCED, ZXC_LEVEL_COMPACT, ZXC_LEVEL_DEFAULT, ZXC_LEVEL_DENSITY, ZXC_LEVEL_FAST,
    ZXC_LEVEL_FASTEST, ZXC_LEVEL_ULTRA, ZXC_VERSION_MAJOR, ZXC_VERSION_MINOR, ZXC_VERSION_PATCH,
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
/// Decompression speed is similar across most levels; levels 6-7 sit a notch
/// below the others because Huffman-coded literals (and, at level 7, tokens)
/// add a per-block decode cost relative to RAW/RLE literals.
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

    /// High density: Huffman-coded literals on top of COMPACT plus a
    /// price-based optimal LZ77 parser (level 6).
    Density = 6,

    /// Maximum density: Huffman-coded literals *and* sequence tokens with a
    /// deep parse. Slowest compression, best ratio (level 7 / ULTRA).
    Ultra = 7,
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
            Level::Ultra,
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

    /// Pre-trained dictionary content (default: `None`).
    ///
    /// Set to the raw dictionary content bytes (as returned by
    /// [`train_dict`] or [`dict_load`]). The decoder must be given the same
    /// dictionary to decompress the resulting archive.
    pub dict: Option<Vec<u8>>,

    /// Shared literal Huffman table (default: `None`; ignored without `dict`).
    ///
    /// The 128-byte packed code-lengths table from [`train_dict_huf`] or
    /// [`dict_huf`]. Becomes part of the archive's dictionary binding: the
    /// decoder must be given the same (dict, table) pair.
    pub dict_huf: Option<Vec<u8>>,
}

impl Default for CompressOptions {
    fn default() -> Self {
        Self {
            level: Level::Default,
            checksum: true,
            seekable: false,
            dict: None,
            dict_huf: None,
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

    /// Attach a pre-trained dictionary (raw content bytes).
    pub fn with_dict(mut self, dict: impl Into<Vec<u8>>) -> Self {
        self.dict = Some(dict.into());
        self
    }

    /// Attach the dictionary's shared literal Huffman table (128 bytes).
    pub fn with_dict_huf(mut self, huf: impl Into<Vec<u8>>) -> Self {
        self.dict_huf = Some(huf.into());
        self
    }

    /// Attach a [`Dictionary`] (content + shared table) in one call.
    pub fn with_dictionary(mut self, dictionary: &Dictionary) -> Self {
        self.dict = Some(dictionary.content().to_vec());
        self.dict_huf = Some(dictionary.huf().to_vec());
        self
    }
}

// =============================================================================
// Decompression Options
// =============================================================================

/// Options for decompression operations.
#[derive(Debug, Clone)]
pub struct DecompressOptions {
    /// Verify checksum during decompression (default: `true`)
    pub verify_checksum: bool,

    /// Pre-trained dictionary content (default: `None`).
    ///
    /// Must match the dictionary used at compression time. Required to
    /// decompress an archive that was produced with a dictionary.
    pub dict: Option<Vec<u8>>,

    /// Shared literal Huffman table (default: `None`; ignored without `dict`).
    ///
    /// Must match the table used at compression time (the archive's dict_id
    /// binds the (dict, table) pair).
    pub dict_huf: Option<Vec<u8>>,
}

impl Default for DecompressOptions {
    fn default() -> Self {
        Self {
            verify_checksum: true,
            dict: None,
            dict_huf: None,
        }
    }
}

impl DecompressOptions {
    /// Create options that skip checksum verification.
    pub fn skip_checksum() -> Self {
        Self {
            verify_checksum: false,
            ..Default::default()
        }
    }

    /// Attach a pre-trained dictionary (raw content bytes).
    pub fn with_dict(mut self, dict: impl Into<Vec<u8>>) -> Self {
        self.dict = Some(dict.into());
        self
    }

    /// Attach the dictionary's shared literal Huffman table (128 bytes).
    pub fn with_dict_huf(mut self, huf: impl Into<Vec<u8>>) -> Self {
        self.dict_huf = Some(huf.into());
        self
    }

    /// Attach a [`Dictionary`] (content + shared table) in one call.
    pub fn with_dictionary(mut self, dictionary: &Dictionary) -> Self {
        self.dict = Some(dictionary.content().to_vec());
        self.dict_huf = Some(dictionary.huf().to_vec());
        self
    }
}

// =============================================================================
// Submodules
// =============================================================================

mod ctx;
mod dict;
mod error;
mod file;
mod oneshot;
mod pstream;
pub mod seekable;
mod stdio;

pub use dict::{
    dict_get_id, dict_huf, dict_id, dict_load, dict_save, get_dict_id, train_dict, train_dict_huf,
    Dictionary,
};
pub use zxc_sys::{ZXC_HUF_TABLE_SIZE, ZXC_DICT_SIZE_MAX};

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
pub use seekable::{seek_table_size, write_seek_table, Seekable};
pub use stdio::{detect_zxc, Decoder, Encoder};
