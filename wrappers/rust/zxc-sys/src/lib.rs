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

// =============================================================================
// Options Structs (mirroring C API)
// =============================================================================

/// Compression options (mirrors `zxc_compress_opts_t` from C API).
#[repr(C)]
#[derive(Debug, Clone)]
pub struct zxc_compress_opts_t {
    /// Worker thread count (0 = auto-detect CPU cores).
    pub n_threads: c_int,
    /// Compression level 1-5 (0 = default).
    pub level: c_int,
    /// Block size in bytes (0 = default 256 KB). Must be power of 2, 4 KB – 2 MB.
    pub block_size: usize,
    /// 1 to enable per-block and global checksums, 0 to disable.
    pub checksum_enabled: c_int,
    /// 1 to append a seek table for random-access decompression, 0 to disable.
    pub seekable: c_int,
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

    /// Returns the maximum supported compression level (currently 5).
    pub fn zxc_max_level() -> c_int;

    /// Returns the default compression level (currently 3).
    pub fn zxc_default_level() -> c_int;

    /// Returns the library version as a null-terminated string (e.g. "0.10.0").
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
    /// cache-line alignment padding.
    ///
    /// Returns the estimated memory usage in bytes, or 0 if `src_size == 0`.
    pub fn zxc_estimate_cctx_size(src_size: usize) -> u64;
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
// Sans-IO API (low-level primitives for building custom drivers)
// =============================================================================

/// Compression context used by the sans-IO primitives (mirrors
/// `zxc_cctx_t` from `zxc_sans_io.h`). Fields are public for advanced
/// integrators; most users should prefer the opaque [`zxc_cctx`] /
/// [`zxc_dctx`] handles.
#[repr(C)]
pub struct zxc_cctx_t {
    /// Hash table for LZ77 match positions (epoch|pos).
    pub hash_table: *mut u32,
    /// Split tag table for fast match rejection (8-bit tags).
    pub hash_tags: *mut u8,
    /// Chain table for collision resolution.
    pub chain_table: *mut u16,
    /// Single allocation block owner.
    pub memory_block: *mut c_void,
    /// Current epoch for lazy hash table invalidation.
    pub epoch: u32,

    /// Buffer for sequence records (packed: LL|ML|Offset).
    pub buf_sequences: *mut u32,
    /// Buffer for token sequences.
    pub buf_tokens: *mut u8,
    /// Buffer for offsets.
    pub buf_offsets: *mut u16,
    /// Buffer for extra lengths (vbytes for LL/ML).
    pub buf_extras: *mut u8,
    /// Buffer for literal bytes.
    pub literals: *mut u8,

    /// Scratch buffer for literals (RLE).
    pub lit_buffer: *mut u8,
    /// Current capacity of the scratch buffer.
    pub lit_buffer_cap: usize,
    /// Padded scratch buffer for buffer-API decompression.
    pub work_buf: *mut u8,
    /// Capacity of the work buffer.
    pub work_buf_cap: usize,
    /// 1 if checksum calculation/verification is enabled.
    pub checksum_enabled: c_int,
    /// Compression level.
    pub compression_level: c_int,

    /// Effective block size in bytes.
    pub chunk_size: usize,
    /// `log2(chunk_size)` - governs epoch_mark shift.
    pub offset_bits: u32,
    /// `(1 << offset_bits) - 1`.
    pub offset_mask: u32,
    /// `1 << (32 - offset_bits)`.
    pub max_epoch: u32,
}

/// On-disk block header (8 bytes, little-endian).
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct zxc_block_header_t {
    /// Block type (see FORMAT.md).
    pub block_type: u8,
    /// Flags (e.g., checksum presence).
    pub block_flags: u8,
    /// Reserved for future protocol extensions.
    pub reserved: u8,
    /// 1-byte header CRC.
    pub header_crc: u8,
    /// Compressed payload size (excluding this header).
    pub comp_size: u32,
}

unsafe extern "C" {
    /// Initializes a ZXC compression context.
    ///
    /// # Safety
    /// - `ctx` must point to a valid (uninitialised) `zxc_cctx_t`.
    ///
    /// Returns `ZXC_OK` on success, or a negative error code.
    pub fn zxc_cctx_init(
        ctx: *mut zxc_cctx_t,
        chunk_size: usize,
        mode: c_int,
        level: c_int,
        checksum_enabled: c_int,
    ) -> c_int;

    /// Frees internal buffers owned by a `zxc_cctx_t`.
    ///
    /// Does NOT free the context pointer itself.
    ///
    /// # Safety
    /// - `ctx` must be a context previously initialised with
    ///   [`zxc_cctx_init`] (or null).
    pub fn zxc_cctx_free(ctx: *mut zxc_cctx_t);

    /// Writes the standard ZXC file header to `dst`.
    ///
    /// # Safety
    /// - `dst` must point to at least `dst_capacity` bytes.
    ///
    /// Returns `ZXC_FILE_HEADER_SIZE` on success, or `ZXC_ERROR_DST_TOO_SMALL`.
    pub fn zxc_write_file_header(
        dst: *mut u8,
        dst_capacity: usize,
        chunk_size: usize,
        has_checksum: c_int,
    ) -> c_int;

    /// Validates and parses the ZXC file header from `src`.
    ///
    /// `out_block_size` and `out_has_checksum` may be null.
    ///
    /// # Safety
    /// - `src` must point to at least `src_size` bytes.
    pub fn zxc_read_file_header(
        src: *const u8,
        src_size: usize,
        out_block_size: *mut usize,
        out_has_checksum: *mut c_int,
    ) -> c_int;

    /// Serialises a block header into `dst` (8 bytes, little-endian).
    ///
    /// # Safety
    /// - `dst` must point to at least `dst_capacity` bytes.
    /// - `bh` must be non-null.
    ///
    /// Returns `ZXC_BLOCK_HEADER_SIZE` on success, or `ZXC_ERROR_DST_TOO_SMALL`.
    pub fn zxc_write_block_header(
        dst: *mut u8,
        dst_capacity: usize,
        bh: *const zxc_block_header_t,
    ) -> c_int;

    /// Parses a block header from `src` (endianness conversion included).
    ///
    /// # Safety
    /// - `src` must point to at least `src_size` bytes.
    /// - `bh` must be a valid (possibly uninitialised) output pointer.
    pub fn zxc_read_block_header(
        src: *const u8,
        src_size: usize,
        bh: *mut zxc_block_header_t,
    ) -> c_int;

    /// Writes the 12-byte file footer (original size + optional global hash).
    ///
    /// # Safety
    /// - `dst` must point to at least `dst_capacity` bytes.
    ///
    /// Returns bytes written, or `ZXC_ERROR_DST_TOO_SMALL`.
    pub fn zxc_write_file_footer(
        dst: *mut u8,
        dst_capacity: usize,
        src_size: u64,
        global_hash: u32,
        checksum_enabled: c_int,
    ) -> c_int;
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
            assert!((compressed_size as usize) < input.len() / 2, "Data should compress well");

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
                assert!(
                    compressed_size > 0,
                    "Compression failed at level {}",
                    level
                );
            }
        }
    }
}
