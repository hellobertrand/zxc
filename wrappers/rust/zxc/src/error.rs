/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! Error types and code mapping shared across the crate.

use zxc_sys::{
    ZXC_ERROR_BAD_BLOCK_SIZE, ZXC_ERROR_BAD_BLOCK_TYPE, ZXC_ERROR_BAD_CHECKSUM,
    ZXC_ERROR_BAD_HEADER, ZXC_ERROR_BAD_MAGIC, ZXC_ERROR_BAD_OFFSET, ZXC_ERROR_BAD_VERSION,
    ZXC_ERROR_CORRUPT_DATA, ZXC_ERROR_DST_TOO_SMALL, ZXC_ERROR_IO, ZXC_ERROR_MEMORY,
    ZXC_ERROR_NULL_INPUT, ZXC_ERROR_OVERFLOW, ZXC_ERROR_SRC_TOO_SMALL,
};

/// Errors that can occur during ZXC operations.
#[derive(Debug, Clone, thiserror::Error)]
pub enum Error {
    /// Memory allocation failure
    #[error("memory allocation failed")]
    Memory,

    /// Destination buffer too small
    #[error("destination buffer too small")]
    DstTooSmall,

    /// Source buffer too small or truncated input
    #[error("source buffer too small or truncated")]
    SrcTooSmall,

    /// Invalid magic word in file header
    #[error("invalid magic word in header")]
    BadMagic,

    /// Unsupported file format version
    #[error("unsupported file format version")]
    BadVersion,

    /// Corrupted or invalid header (CRC mismatch)
    #[error("corrupted or invalid header")]
    BadHeader,

    /// Block or global checksum verification failed
    #[error("checksum verification failed")]
    BadChecksum,

    /// Corrupted compressed data
    #[error("corrupted compressed data")]
    CorruptData,

    /// Invalid match offset during decompression
    #[error("invalid match offset")]
    BadOffset,

    /// Buffer overflow detected during processing
    #[error("buffer overflow detected")]
    Overflow,

    /// Read/write/seek failure on file
    #[error("I/O error")]
    Io,

    /// Required input pointer is NULL
    #[error("null input pointer")]
    NullInput,

    /// Unknown or unexpected block type
    #[error("unknown block type")]
    BadBlockType,

    /// Invalid block size
    #[error("invalid block size")]
    BadBlockSize,

    /// The compressed data appears to be invalid or truncated
    #[error("invalid compressed data")]
    InvalidData,

    /// Unknown error code from C library
    #[error("unknown error (code: {0})")]
    Unknown(i32),
}

/// Convert a negative error code from the C library to a Rust [`Error`].
pub(crate) fn error_from_code(code: i64) -> Error {
    match code as i32 {
        ZXC_ERROR_MEMORY => Error::Memory,
        ZXC_ERROR_DST_TOO_SMALL => Error::DstTooSmall,
        ZXC_ERROR_SRC_TOO_SMALL => Error::SrcTooSmall,
        ZXC_ERROR_BAD_MAGIC => Error::BadMagic,
        ZXC_ERROR_BAD_VERSION => Error::BadVersion,
        ZXC_ERROR_BAD_HEADER => Error::BadHeader,
        ZXC_ERROR_BAD_CHECKSUM => Error::BadChecksum,
        ZXC_ERROR_CORRUPT_DATA => Error::CorruptData,
        ZXC_ERROR_BAD_OFFSET => Error::BadOffset,
        ZXC_ERROR_OVERFLOW => Error::Overflow,
        ZXC_ERROR_IO => Error::Io,
        ZXC_ERROR_NULL_INPUT => Error::NullInput,
        ZXC_ERROR_BAD_BLOCK_TYPE => Error::BadBlockType,
        ZXC_ERROR_BAD_BLOCK_SIZE => Error::BadBlockSize,
        _ => Error::Unknown(code as i32),
    }
}

/// Result type for ZXC operations.
pub type Result<T> = std::result::Result<T, Error>;
