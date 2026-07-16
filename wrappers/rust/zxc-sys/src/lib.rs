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
use std::os::raw::{c_char, c_void};

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

/// High density: Huffman-coded literals on top of COMPACT,
/// price-based optimal LZ77 parser.
pub const ZXC_LEVEL_DENSITY: i32 = parse_i32(env!("ZXC_LEVEL_DENSITY"));

/// Maximum density: Huffman-coded literals *and* sequence tokens, deep parse.
/// Slowest compression, best ratio (level 7 / ULTRA).
pub const ZXC_LEVEL_ULTRA: i32 = parse_i32(env!("ZXC_LEVEL_ULTRA"));

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

/// Invalid block size
pub const ZXC_ERROR_BAD_BLOCK_SIZE: i32 = -14;

/// File requires a dictionary but none was provided
pub const ZXC_ERROR_DICT_REQUIRED: i32 = -15;

/// Provided dictionary ID does not match the file header
pub const ZXC_ERROR_DICT_MISMATCH: i32 = -16;

/// Dictionary exceeds maximum allowed size
pub const ZXC_ERROR_DICT_TOO_LARGE: i32 = -17;

/// Compression level out of range, or not supported by this context's workspace
pub const ZXC_ERROR_BAD_LEVEL: i32 = -18;

// =============================================================================
// Dictionary Constants
// =============================================================================

/// Maximum dictionary content size in bytes (65535).
pub const ZXC_DICT_SIZE_MAX: usize = (1usize << 16) - 1;

/// Fixed size of the `.zxd` file header in bytes.
pub const ZXC_DICT_HEADER_SIZE: usize = 16;

/// Size in bytes of the shared literal Huffman table carried by a `.zxd` file
/// (packed 4-bit code lengths for 256 symbols).
pub const ZXC_HUF_TABLE_SIZE: usize = 128;

// =============================================================================
// Options Structs (mirroring C API)
// =============================================================================

/// Compression options (mirrors `zxc_compress_opts_t` from C API).
#[repr(C)]
#[derive(Debug, Clone)]
pub struct zxc_compress_opts_t {
    /// Worker thread count (0 = auto-detect CPU cores).
    pub n_threads: c_int,
    /// Compression level 1-7 (0 = default).
    pub level: c_int,
    /// Block size in bytes (0 = default 512 KB). Must be power of 2, 4 KB – 2 MB.
    pub block_size: usize,
    /// 1 to enable per-block and global checksums, 0 to disable.
    pub checksum_enabled: c_int,
    /// 1 to append a seek table for random-access decompression, 0 to disable.
    pub seekable: c_int,
    /// Pre-trained dictionary content (NULL = none).
    pub dict: *const c_void,
    /// Dictionary size in bytes (0 = none, max [`ZXC_DICT_SIZE_MAX`]).
    pub dict_size: usize,
    /// Shared literal Huffman table: 128-byte packed code-lengths header
    /// (NULL = none; ignored without dict).
    pub dict_huf: *const c_void,
    /// Progress callback (NULL to disable).
    pub progress_cb: *const c_void,
    /// User context pointer passed to progress_cb.
    pub user_data: *mut c_void,
}

impl Default for zxc_compress_opts_t {
    fn default() -> Self {
        Self {
            n_threads: 0,
            level: 0,
            block_size: 0,
            checksum_enabled: 0,
            seekable: 0,
            dict: std::ptr::null(),
            dict_size: 0,
            dict_huf: std::ptr::null(),
            progress_cb: std::ptr::null(),
            user_data: std::ptr::null_mut(),
        }
    }
}

/// Decompression options (mirrors `zxc_decompress_opts_t` from C API).
#[repr(C)]
#[derive(Debug, Clone)]
pub struct zxc_decompress_opts_t {
    /// Worker thread count (0 = auto-detect CPU cores).
    pub n_threads: c_int,
    /// 1 to verify per-block and global checksums, 0 to skip.
    pub checksum_enabled: c_int,
    /// Pre-trained dictionary content (NULL = none).
    pub dict: *const c_void,
    /// Dictionary size in bytes (0 = none).
    pub dict_size: usize,
    /// Shared literal Huffman table: 128-byte packed code-lengths header
    /// (NULL = none; ignored without dict).
    pub dict_huf: *const c_void,
    /// Progress callback (NULL to disable).
    pub progress_cb: *const c_void,
    /// User context pointer passed to progress_cb.
    pub user_data: *mut c_void,
}

impl Default for zxc_decompress_opts_t {
    fn default() -> Self {
        Self {
            n_threads: 0,
            checksum_enabled: 0,
            dict: std::ptr::null(),
            dict_size: 0,
            dict_huf: std::ptr::null(),
            progress_cb: std::ptr::null(),
            user_data: std::ptr::null_mut(),
        }
    }
}

// =============================================================================
// Opaque Context Handles
// =============================================================================

/// Opaque compression context. Use [`zxc_create_cctx`] to create.
#[repr(C)]
pub struct zxc_cctx {
    _private: [u8; 0],
}

/// Opaque decompression context. Use [`zxc_create_dctx`] to create.
#[repr(C)]
pub struct zxc_dctx {
    _private: [u8; 0],
}

// =============================================================================
// Library Info Helpers
// =============================================================================

unsafe extern "C" {
    /// Returns the minimum supported compression level (currently 1).
    pub fn zxc_min_level() -> c_int;

    /// Returns the maximum supported compression level (currently 7).
    pub fn zxc_max_level() -> c_int;

    /// Returns the default compression level (currently 3).
    pub fn zxc_default_level() -> c_int;

    /// Returns the library version as a null-terminated string (e.g. "0.13.1").
    ///
    /// The returned pointer is a compile-time constant and must not be freed.
    pub fn zxc_version_string() -> *const c_char;
}

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
        opts: *const zxc_compress_opts_t,
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
        opts: *const zxc_decompress_opts_t,
    ) -> i64;

    /// Returns the decompressed size stored in a ZXC compressed buffer.
    ///
    /// Reads the file footer without performing decompression.
    ///
    /// # Returns
    ///
    /// Original uncompressed size in bytes, or 0 if invalid.
    pub fn zxc_get_decompressed_size(src: *const c_void, src_size: usize) -> u64;

    /// Returns the minimum single-buffer size for an in-place decompression.
    ///
    /// Reads `src`'s header and footer (no decoding) and returns the buffer
    /// size `zxc_decompress_inplace` needs: decompressed size plus a
    /// one-block and wild-copy safety margin.
    ///
    /// # Returns
    ///
    /// Required buffer size in bytes, or 0 if `src` is not a valid archive.
    pub fn zxc_decompress_inplace_bound(src: *const c_void, src_size: usize) -> usize;

    /// Decompresses in place inside a single caller-owned buffer.
    ///
    /// The compressed archive must sit flush-right in `buffer` (its last
    /// `comp_size` bytes); decoding runs left-to-right into `buffer[0..]`.
    /// `buffer_capacity` must be at least `zxc_decompress_inplace_bound`.
    ///
    /// # Returns
    ///
    /// Decompressed size (>0), 0 for an empty frame, or a negative error
    /// code (`ZXC_ERROR_DST_TOO_SMALL` if the margin is missing).
    pub fn zxc_decompress_inplace(
        buffer: *mut c_void,
        buffer_capacity: usize,
        comp_size: usize,
        opts: *const zxc_decompress_opts_t,
    ) -> i64;

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

    /// Returns `sizeof(zxc_compress_opts_t)` as compiled into the C library.
    ///
    /// Layout guard: the Rust [`zxc_compress_opts_t`] mirrors the C struct by
    /// hand; comparing sizes turns silent layout drift into a test failure
    /// (see the `opts_layout_matches_library` test).
    pub fn zxc_compress_opts_size() -> usize;

    /// Returns `sizeof(zxc_decompress_opts_t)` as compiled into the C library.
    /// See [`zxc_compress_opts_size`].
    pub fn zxc_decompress_opts_size() -> usize;

    /// Returns the dictionary ID a ZXC compressed buffer requires.
    ///
    /// Reads the file header without decompressing. Returns 0 if the file
    /// does not require a dictionary or the buffer is invalid.
    ///
    /// # Safety
    /// - `src` must be a valid pointer to `src_size` bytes.
    pub fn zxc_get_dict_id(src: *const c_void, src_size: usize) -> u32;
}

// =============================================================================
// Dictionary API
// =============================================================================

unsafe extern "C" {
    /// Trains a dictionary from a corpus of samples.
    ///
    /// # Safety
    /// - `samples` must point to `n_samples` valid pointers, each valid for
    ///   the corresponding `sample_sizes` entry.
    /// - `sample_sizes` must point to `n_samples` entries.
    /// - `dict_buf` must be valid for writes up to `dict_capacity` bytes.
    ///
    /// # Returns
    /// Size of the trained dictionary (>0), or a negative `zxc_error_t` code.
    pub fn zxc_train_dict(
        samples: *const *const c_void,
        sample_sizes: *const usize,
        n_samples: usize,
        dict_buf: *mut c_void,
        dict_capacity: usize,
    ) -> i64;

    /// Computes the dictionary ID: content-only when `huf_lengths` is null,
    /// else binding the (content, table) pair.
    ///
    /// # Safety
    /// - `dict` must be a valid pointer to `dict_size` bytes.
    /// - `huf_lengths` must be null or valid for 128 bytes (`ZXC_HUF_TABLE_SIZE`).
    ///
    /// Returns 0 if `dict` is NULL or `dict_size` is 0.
    pub fn zxc_dict_id(dict: *const c_void, dict_size: usize, huf_lengths: *const c_void) -> u32;

    /// Returns the dictionary ID stored in a `.zxd` file buffer.
    ///
    /// # Safety
    /// - `buf` must be a valid pointer to `buf_size` bytes.
    ///
    /// Returns 0 if the buffer is not a valid `.zxd` file.
    pub fn zxc_dict_get_id(buf: *const c_void, buf_size: usize) -> u32;

    /// Returns the `.zxd` file size for a given content size
    /// (= 16 + `content_size` + 128 for the shared Huffman table).
    pub fn zxc_dict_save_bound(content_size: usize) -> usize;

    /// Trains the shared literal Huffman table for an already-trained dictionary.
    ///
    /// # Safety
    /// - `samples`/`sample_sizes` as in [`zxc_train_dict`].
    /// - `dict` must be valid for `dict_size` bytes.
    /// - `huf_lengths_out` must be valid for writes of 128 bytes
    ///   (`ZXC_HUF_TABLE_SIZE`).
    ///
    /// # Returns
    /// `ZXC_OK` (0) on success, or a negative `zxc_error_t` code.
    pub fn zxc_train_dict_huf(
        samples: *const *const c_void,
        sample_sizes: *const usize,
        n_samples: usize,
        dict: *const c_void,
        dict_size: usize,
        huf_lengths_out: *mut u8,
    ) -> c_int;

    /// Serializes dictionary content plus its shared Huffman table to the
    /// `.zxd` file format. The table is mandatory.
    ///
    /// # Safety
    /// - `content` must be valid for `content_size` bytes.
    /// - `huf_lengths` must be valid for 128 bytes (`ZXC_HUF_TABLE_SIZE`).
    /// - `buf` must be valid for writes up to `buf_capacity` bytes.
    ///
    /// # Returns
    /// Bytes written (>0), or a negative `zxc_error_t` code.
    pub fn zxc_dict_save(
        content: *const c_void,
        content_size: usize,
        huf_lengths: *const c_void,
        buf: *mut c_void,
        buf_capacity: usize,
    ) -> i64;

    /// Returns a pointer to the 128-byte shared Huffman table inside a `.zxd`
    /// buffer (zero-copy; aims into `buf`), or NULL if `buf` is not a valid
    /// `.zxd` file.
    ///
    /// # Safety
    /// - `buf` must be valid for `buf_size` bytes.
    pub fn zxc_dict_huf(buf: *const c_void, buf_size: usize) -> *const c_void;

    /// Loads and validates a `.zxd` dictionary file from a memory buffer.
    ///
    /// On success, `content_out` and `huf_out` (when non-NULL) point INTO `buf`
    /// (zero-copy); the caller must keep `buf` alive while they are in use.
    ///
    /// # Safety
    /// - `buf` must be valid for `buf_size` bytes.
    /// - `content_out` and `content_size_out` must be valid pointers.
    /// - `huf_out` and `dict_id_out` may be NULL.
    ///
    /// # Returns
    /// `ZXC_OK` (0) on success, or a negative `zxc_error_t` code.
    pub fn zxc_dict_load(
        buf: *const c_void,
        buf_size: usize,
        content_out: *mut *const c_void,
        content_size_out: *mut usize,
        huf_out: *mut *const c_void,
        dict_id_out: *mut u32,
    ) -> c_int;

    /// One-call dictionary creation: trains content + shared table and
    /// serializes to ready-to-write `.zxd` bytes in `zxd_buf`.
    ///
    /// # Safety
    /// - `samples`/`sample_sizes` as in [`zxc_train_dict`].
    /// - `zxd_buf` must be valid for writes up to `zxd_capacity` bytes.
    ///
    /// # Returns
    /// Bytes written (>0), or a negative `zxc_error_t` code.
    pub fn zxc_dict_train(
        samples: *const *const c_void,
        sample_sizes: *const usize,
        n_samples: usize,
        zxd_buf: *mut c_void,
        zxd_capacity: usize,
    ) -> i64;
}

// =============================================================================
// Block API (single block, no file framing)
// =============================================================================

unsafe extern "C" {
    /// Returns the maximum compressed size for a single block (no file framing).
    pub fn zxc_compress_block_bound(input_size: usize) -> u64;

    /// Returns the minimum `dst_capacity` required by [`zxc_decompress_block`]
    /// for a block of `uncompressed_size` bytes. Accounts for the wild-copy
    /// tail pad used by the fast decoder.
    pub fn zxc_decompress_block_bound(uncompressed_size: usize) -> u64;

    /// Compresses a single block without file framing.
    ///
    /// Output format: `block_header(8B) + payload + optional checksum(4B)`.
    ///
    /// # Safety
    /// - `cctx` must be a valid pointer returned by [`zxc_create_cctx`].
    /// - `src`, `dst` must point to `src_size` / `dst_capacity` bytes.
    pub fn zxc_compress_block(
        cctx: *mut zxc_cctx,
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        opts: *const zxc_compress_opts_t,
    ) -> i64;

    /// Decompresses a single block produced by [`zxc_compress_block`].
    ///
    /// `dst_capacity` should be at least
    /// [`zxc_decompress_block_bound(uncompressed_size)`](zxc_decompress_block_bound)
    /// to enable the fast path.
    ///
    /// # Safety
    /// - `dctx` must be a valid pointer returned by [`zxc_create_dctx`].
    pub fn zxc_decompress_block(
        dctx: *mut zxc_dctx,
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        opts: *const zxc_decompress_opts_t,
    ) -> i64;

    /// Strict-sized variant of [`zxc_decompress_block`]: accepts
    /// `dst_capacity == uncompressed_size` exactly (no tail pad required).
    /// Slightly slower than the fast path; output is bit-identical.
    ///
    /// # Safety
    /// - `dctx` must be a valid pointer returned by [`zxc_create_dctx`].
    pub fn zxc_decompress_block_safe(
        dctx: *mut zxc_dctx,
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        opts: *const zxc_decompress_opts_t,
    ) -> i64;

    /// Estimates the memory reserved by a compression context for a single
    /// block of `src_size` bytes via [`zxc_compress_block`].
    ///
    /// Covers all per-chunk working buffers (chain table, literals,
    /// sequence/token/offset/extras buffers) plus the fixed hash tables and
    /// cache-line alignment padding. At `level >= 6` the value also accounts
    /// for the price-based optimal parser's transient DP scratch (~18 x
    /// `src_size` bytes), free'd at the end of each block.
    ///
    /// Returns the estimated peak memory usage in bytes, or 0 if `src_size == 0`.
    pub fn zxc_estimate_cctx_size(src_size: usize, level: c_int) -> u64;
}

// =============================================================================
// Reusable Context API (opaque, heap-allocated)
// =============================================================================

unsafe extern "C" {
    /// Creates a heap-allocated compression context.
    ///
    /// When `opts` is non-NULL, internal buffers are pre-allocated.
    /// When `opts` is NULL, allocation is deferred to first use.
    /// Returns NULL on allocation failure. Free with [`zxc_free_cctx`].
    pub fn zxc_create_cctx(opts: *const zxc_compress_opts_t) -> *mut zxc_cctx;

    /// Frees a compression context. Safe to call with a null pointer.
    ///
    /// # Safety
    /// - `cctx` must be a pointer returned by [`zxc_create_cctx`] (or null).
    pub fn zxc_free_cctx(cctx: *mut zxc_cctx);

    /// Compresses a full file-framed buffer using a reusable context.
    ///
    /// # Safety
    /// - `cctx` must be a valid pointer returned by [`zxc_create_cctx`].
    pub fn zxc_compress_cctx(
        cctx: *mut zxc_cctx,
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        opts: *const zxc_compress_opts_t,
    ) -> i64;

    /// Creates a heap-allocated decompression context.
    /// Returns NULL on allocation failure. Free with [`zxc_free_dctx`].
    pub fn zxc_create_dctx() -> *mut zxc_dctx;

    /// Frees a decompression context. Safe to call with a null pointer.
    ///
    /// # Safety
    /// - `dctx` must be a pointer returned by [`zxc_create_dctx`] (or null).
    pub fn zxc_free_dctx(dctx: *mut zxc_dctx);

    /// Decompresses a full file-framed buffer using a reusable context.
    ///
    /// # Safety
    /// - `dctx` must be a valid pointer returned by [`zxc_create_dctx`].
    pub fn zxc_decompress_dctx(
        dctx: *mut zxc_dctx,
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        opts: *const zxc_decompress_opts_t,
    ) -> i64;

    /// Returns the exact byte count required by a static compression
    /// workspace for the given `block_size` and `level`.
    ///
    /// # Returns
    ///
    /// Workspace size in bytes, or 0 if either argument is invalid.
    pub fn zxc_static_cctx_workspace_size(block_size: usize, level: c_int) -> usize;

    /// Initialises a compression context inside a caller-supplied workspace
    /// (no heap allocation).
    ///
    /// # Safety
    /// - `workspace` must be cache-line (64-byte) aligned, at least
    ///   `zxc_static_cctx_workspace_size` bytes, and outlive the handle.
    /// - `opts` must be non-null with `block_size` and `level` set.
    /// - `zxc_free_cctx` is a no-op on the returned handle.
    pub fn zxc_init_static_cctx(
        workspace: *mut c_void,
        workspace_size: usize,
        opts: *const zxc_compress_opts_t,
    ) -> *mut zxc_cctx;

    /// Returns the exact byte count required by a static decompression
    /// workspace for the given `block_size`.
    ///
    /// # Returns
    ///
    /// Workspace size in bytes, or 0 if `block_size` is invalid.
    pub fn zxc_static_dctx_workspace_size(block_size: usize) -> usize;

    /// Initialises a decompression context inside a caller-supplied
    /// workspace (no heap allocation).
    ///
    /// # Safety
    /// - `workspace` must be cache-line (64-byte) aligned, at least
    ///   `zxc_static_dctx_workspace_size` bytes, and outlive the handle.
    /// - `zxc_free_dctx` is a no-op on the returned handle.
    pub fn zxc_init_static_dctx(
        workspace: *mut c_void,
        workspace_size: usize,
        block_size: usize,
    ) -> *mut zxc_dctx;
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
    /// * `level` - Compression level (1-7)
    /// * `checksum_enabled` - If non-zero, enables checksum verification
    ///
    /// # Returns
    ///
    /// Total compressed bytes written, or -1 on error.
    pub fn zxc_stream_compress(
        f_in: *mut libc::FILE,
        f_out: *mut libc::FILE,
        opts: *const zxc_compress_opts_t,
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
        opts: *const zxc_decompress_opts_t,
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
// Seekable API (random-access decompression)
// =============================================================================

/// Opaque handle for a seekable ZXC archive.
///
/// Created by [`zxc_seekable_open`] or [`zxc_seekable_open_file`].
/// Must be freed with [`zxc_seekable_free`].
#[repr(C)]
pub struct zxc_seekable {
    _private: [u8; 0],
}

/// Storage-agnostic reader interface for seekable archives (mirrors
/// `zxc_reader_t` from `zxc_seekable.h`).
///
/// `read_at` is the only primitive the library calls on the backend. It must
/// return the number of bytes read (`== len` on success) or a negative
/// `zxc_error_t` on failure. Short reads are treated as errors by the library.
///
/// `read_at` MUST be safe to call concurrently from multiple threads when the
/// resulting handle is used with [`zxc_seekable_decompress_range_mt`]. The
/// single-threaded path makes no concurrent calls.
#[repr(C)]
pub struct zxc_reader_t {
    /// Positional read callback. NULL is rejected at open time.
    pub read_at: Option<
        unsafe extern "C" fn(ctx: *mut c_void, dst: *mut c_void, len: usize, offset: u64) -> i64,
    >,
    /// Opaque user context passed unchanged to `read_at`.
    pub ctx: *mut c_void,
    /// Total size of the compressed archive in bytes.
    pub size: u64,
}

unsafe extern "C" {
    /// Opens a seekable archive from a memory buffer.
    ///
    /// The buffer must remain valid for the lifetime of the handle.
    ///
    /// # Safety
    /// - `src` must be a valid pointer to `src_size` bytes.
    ///
    /// Returns NULL if the buffer is not a valid seekable archive.
    pub fn zxc_seekable_open(src: *const c_void, src_size: usize) -> *mut zxc_seekable;

    /// Opens a seekable archive from a `FILE*`. The file must be seekable
    /// (not stdin/pipe). The current file position is saved and restored.
    /// The FILE* must remain open for the lifetime of the handle.
    ///
    /// # Safety
    /// - `f` must be a valid FILE* opened in "rb" mode.
    pub fn zxc_seekable_open_file(f: *mut libc::FILE) -> *mut zxc_seekable;

    /// Opens a seekable archive through a user-supplied [`zxc_reader_t`].
    ///
    /// The reader is invoked to fetch the file header, footer, and seek table
    /// at open time (3 reads), then once per block during decompression.
    /// Use this entry point to back the seekable API with any storage that
    /// supports positional reads (mmap, HTTP `Range:`, S3, kernel
    /// `vfs_read()`, etc.).
    ///
    /// # Safety
    /// - `r` must point to a valid [`zxc_reader_t`] whose `read_at`,
    ///   `ctx`, and backing storage remain valid for the lifetime of the
    ///   returned handle.
    ///
    /// Returns NULL on any open-time error.
    pub fn zxc_seekable_open_reader(r: *const zxc_reader_t) -> *mut zxc_seekable;

    /// Attaches a dictionary to a seekable handle. The dictionary content
    /// must remain valid for the lifetime of the handle (or until replaced).
    ///
    /// # Safety
    /// - `s` must be a valid handle from [`zxc_seekable_open`] etc.
    /// - `dict` must be valid for `dict_size` bytes (or NULL with 0).
    /// - `dict_huf` must be NULL or valid for 128 bytes
    ///   (`ZXC_HUF_TABLE_SIZE`).
    ///
    /// Returns `ZXC_OK` (0) on success, or a negative `zxc_error_t` code.
    pub fn zxc_seekable_set_dict(
        s: *mut zxc_seekable,
        dict: *const c_void,
        dict_size: usize,
        dict_huf: *const c_void,
    ) -> c_int;

    /// Returns the total number of data blocks in the archive (excluding EOF).
    pub fn zxc_seekable_get_num_blocks(s: *const zxc_seekable) -> u32;

    /// Returns the total decompressed size of the archive in bytes.
    pub fn zxc_seekable_get_decompressed_size(s: *const zxc_seekable) -> u64;

    /// Returns the on-disk compressed size of a specific block
    /// (block header + payload + optional per-block checksum).
    ///
    /// Returns 0 if `block_idx` is out of range.
    pub fn zxc_seekable_get_block_comp_size(s: *const zxc_seekable, block_idx: u32) -> u32;

    /// Returns the decompressed size of a specific block.
    ///
    /// Returns 0 if `block_idx` is out of range.
    pub fn zxc_seekable_get_block_decomp_size(s: *const zxc_seekable, block_idx: u32) -> u32;

    /// Decompresses `len` bytes starting at byte `offset` in the original
    /// uncompressed data. Only the blocks overlapping the requested range
    /// are read and decompressed.
    ///
    /// # Safety
    /// - `s` must be a valid handle returned by [`zxc_seekable_open`].
    /// - `dst` must point to at least `dst_capacity` bytes.
    ///
    /// Returns `len` on success, or a negative `zxc_error_t` on failure.
    pub fn zxc_seekable_decompress_range(
        s: *mut zxc_seekable,
        dst: *mut c_void,
        dst_capacity: usize,
        offset: u64,
        len: usize,
    ) -> i64;

    /// Multi-threaded variant of [`zxc_seekable_decompress_range`].
    ///
    /// Each worker owns its own decompression context and reads via `pread()`
    /// (POSIX) or `ReadFile()` (Windows) for lock-free concurrent I/O.
    /// Falls back to single-threaded mode when `n_threads <= 1` or the range
    /// spans a single block.
    ///
    /// # Safety
    /// - `s` must be a valid handle returned by [`zxc_seekable_open`].
    /// - `dst` must point to at least `dst_capacity` bytes.
    pub fn zxc_seekable_decompress_range_mt(
        s: *mut zxc_seekable,
        dst: *mut c_void,
        dst_capacity: usize,
        offset: u64,
        len: usize,
        n_threads: c_int,
    ) -> i64;

    /// Frees a seekable handle and all associated resources.
    /// Safe to call with a null pointer.
    ///
    /// # Safety
    /// - `s` must be a pointer returned by [`zxc_seekable_open`] /
    ///   [`zxc_seekable_open_file`], or null.
    pub fn zxc_seekable_free(s: *mut zxc_seekable);

    /// Low-level: writes a seek table (block header + entries) to `dst`.
    ///
    /// # Safety
    /// - `dst` must point to at least `dst_capacity` bytes.
    /// - `comp_sizes` must point to at least `num_blocks` entries.
    ///
    /// Returns bytes written, or a negative `zxc_error_t` on failure.
    pub fn zxc_write_seek_table(
        dst: *mut u8,
        dst_capacity: usize,
        comp_sizes: *const u32,
        num_blocks: u32,
    ) -> i64;

    /// Returns the encoded byte size of a seek table for `num_blocks` blocks.
    pub fn zxc_seek_table_size(num_blocks: u32) -> usize;
}

// =============================================================================
// Push Streaming API (single-threaded, caller-driven)
// =============================================================================

/// Input buffer descriptor for push streaming
/// (mirrors `zxc_inbuf_t` from `zxc_pstream.h`).
#[repr(C)]
#[derive(Debug)]
pub struct zxc_inbuf_t {
    /// Caller-owned input bytes.
    pub src: *const c_void,
    /// Total bytes available in `src`.
    pub size: usize,
    /// Bytes already consumed by the library (in/out).
    pub pos: usize,
}

/// Output buffer descriptor for push streaming
/// (mirrors `zxc_outbuf_t` from `zxc_pstream.h`).
#[repr(C)]
#[derive(Debug)]
pub struct zxc_outbuf_t {
    /// Caller-owned output region.
    pub dst: *mut c_void,
    /// Total capacity available at `dst`.
    pub size: usize,
    /// Bytes already produced by the library (in/out).
    pub pos: usize,
}

/// Opaque push compression stream. Use [`zxc_cstream_create`] to create.
#[repr(C)]
pub struct zxc_cstream {
    _private: [u8; 0],
}

/// Opaque push decompression stream. Use [`zxc_dstream_create`] to create.
#[repr(C)]
pub struct zxc_dstream {
    _private: [u8; 0],
}

unsafe extern "C" {
    /// Creates a push compression stream. Returns NULL on allocation failure.
    /// Free with [`zxc_cstream_free`].
    pub fn zxc_cstream_create(opts: *const zxc_compress_opts_t) -> *mut zxc_cstream;

    /// Releases a push compression stream. Safe to call with null.
    ///
    /// # Safety
    /// - `cs` must be a pointer returned by [`zxc_cstream_create`] (or null).
    pub fn zxc_cstream_free(cs: *mut zxc_cstream);

    /// Pushes input bytes into the stream and drains compressed output.
    ///
    /// Returns 0 if `in` was fully consumed and no compressed bytes remain
    /// pending; >0 number of bytes still pending; <0 on error.
    ///
    /// # Safety
    /// - `cs` must be a valid stream from [`zxc_cstream_create`].
    /// - `out` and `in_` must point to valid `zxc_outbuf_t` / `zxc_inbuf_t`.
    pub fn zxc_cstream_compress(
        cs: *mut zxc_cstream,
        out: *mut zxc_outbuf_t,
        in_: *mut zxc_inbuf_t,
    ) -> i64;

    /// Finalises the stream: flushes pending data, writes EOF block + footer.
    ///
    /// Returns 0 when finalisation is complete; >0 bytes still pending; <0
    /// on error.
    ///
    /// # Safety
    /// - `cs` must be a valid stream from [`zxc_cstream_create`].
    /// - `out` must point to a valid `zxc_outbuf_t`.
    pub fn zxc_cstream_end(cs: *mut zxc_cstream, out: *mut zxc_outbuf_t) -> i64;

    /// Suggested input chunk size for best compressor throughput.
    pub fn zxc_cstream_in_size(cs: *const zxc_cstream) -> usize;

    /// Suggested output chunk size for the compressor.
    pub fn zxc_cstream_out_size(cs: *const zxc_cstream) -> usize;

    /// Creates a push decompression stream. Returns NULL on allocation
    /// failure. Free with [`zxc_dstream_free`].
    pub fn zxc_dstream_create(opts: *const zxc_decompress_opts_t) -> *mut zxc_dstream;

    /// Releases a push decompression stream. Safe to call with null.
    ///
    /// # Safety
    /// - `ds` must be a pointer returned by [`zxc_dstream_create`] (or null).
    pub fn zxc_dstream_free(ds: *mut zxc_dstream);

    /// Pushes compressed input and drains decompressed output.
    ///
    /// Returns >0 number of decompressed bytes written this call; 0 if the
    /// stream is complete (DONE) or no progress is possible; <0 on error.
    ///
    /// # Safety
    /// - `ds` must be a valid stream from [`zxc_dstream_create`].
    /// - `out` and `in_` must point to valid `zxc_outbuf_t` / `zxc_inbuf_t`.
    pub fn zxc_dstream_decompress(
        ds: *mut zxc_dstream,
        out: *mut zxc_outbuf_t,
        in_: *mut zxc_inbuf_t,
    ) -> i64;

    /// Returns 1 iff the decoder reached and validated the file footer.
    pub fn zxc_dstream_finished(ds: *const zxc_dstream) -> c_int;

    /// Suggested input chunk size for the decompressor.
    pub fn zxc_dstream_in_size(ds: *const zxc_dstream) -> usize;

    /// Suggested output chunk size for the decompressor.
    pub fn zxc_dstream_out_size(ds: *const zxc_dstream) -> usize;
}

// =============================================================================
// Tests
// =============================================================================

#[cfg(test)]
mod tests {
    /// The options structs above are hand-mirrored from zxc_opts.h. A C-side
    /// field change that is not replicated here silently shifts every later
    /// field (undefined behaviour at the FFI boundary); this test turns that
    /// drift into a hard failure.
    #[test]
    fn opts_layout_matches_library() {
        unsafe {
            assert_eq!(
                std::mem::size_of::<super::zxc_compress_opts_t>(),
                super::zxc_compress_opts_size(),
                "zxc_compress_opts_t layout drift: update the Rust mirror in zxc-sys"
            );
            assert_eq!(
                std::mem::size_of::<super::zxc_decompress_opts_t>(),
                super::zxc_decompress_opts_size(),
                "zxc_decompress_opts_t layout drift: update the Rust mirror in zxc-sys"
            );
        }
    }

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
            let copts = zxc_compress_opts_t {
                level: ZXC_LEVEL_DEFAULT,
                checksum_enabled: 1,
                ..Default::default()
            };
            let compressed_size = zxc_compress(
                input.as_ptr() as *const c_void,
                input.len(),
                compressed.as_mut_ptr() as *mut c_void,
                compressed.len(),
                &copts,
            );
            assert!(compressed_size > 0, "Compression failed");
            // Highly repetitive data should compress significantly
            assert!(
                (compressed_size as usize) < input.len() / 2,
                "Data should compress well"
            );

            // Get decompressed size
            let decompressed_size = zxc_get_decompressed_size(
                compressed.as_ptr() as *const c_void,
                compressed_size as usize,
            );
            assert_eq!(decompressed_size as usize, input.len());

            // Decompress
            let mut decompressed = vec![0u8; decompressed_size as usize];
            let dopts = zxc_decompress_opts_t {
                checksum_enabled: 1,
                ..Default::default()
            };
            let result_size = zxc_decompress(
                compressed.as_ptr() as *const c_void,
                compressed_size as usize,
                decompressed.as_mut_ptr() as *mut c_void,
                decompressed.len(),
                &dopts,
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
            ZXC_LEVEL_DENSITY,
            ZXC_LEVEL_ULTRA,
        ] {
            unsafe {
                let bound = zxc_compress_bound(input.len()) as usize;
                let mut compressed = vec![0u8; bound];

                let copts = zxc_compress_opts_t {
                    level,
                    checksum_enabled: 1,
                    ..Default::default()
                };
                let compressed_size = zxc_compress(
                    input.as_ptr() as *const c_void,
                    input.len(),
                    compressed.as_mut_ptr() as *mut c_void,
                    compressed.len(),
                    &copts,
                );
                assert!(compressed_size > 0, "Compression failed at level {}", level);
            }
        }
    }
}
