// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

#![doc = include_str!("../README.md")]
#![allow(non_camel_case_types)]
#![allow(non_upper_case_globals)]

use std::ffi::c_int;
use std::os::raw::{c_char, c_uint, c_void};

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

/// Corrupted or invalid header (checksum mismatch)
pub const ZXC_ERROR_BAD_HEADER: i32 = -6;

/// A block's decoded bytes fail its checksum, or the archive digest mismatches
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

/// Dictionary not supported by this context's workspace (static contexts)
pub const ZXC_ERROR_DICT_UNSUPPORTED: i32 = -19;

/// Unknown parameter, or value out of its bounds (`zxc_cctx_set_param`,
/// `zxc_dctx_set_param`)
pub const ZXC_ERROR_BAD_PARAM: i32 = -20;

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
// Context Parameters (mirroring zxc_opts.h)
// =============================================================================

/// Maximum worker thread count accepted by [`ZXC_C_THREADS`] / [`ZXC_D_THREADS`].
pub const ZXC_MAX_THREADS: i64 = 512;
/// The [`ZXC_C_FAVOR_DECODE_SPEED`] budget beyond which decoding barely speeds
/// up: what the CLI's bare `--favor-decode-speed` sets.
pub const ZXC_SPLIT_BUDGET_DEFAULT: i64 = 10;
/// Largest [`ZXC_C_FAVOR_DECODE_SPEED`] budget.
pub const ZXC_SPLIT_BUDGET_MAX: i64 = 100;

/// Compression parameter identifier (mirrors the C enum `zxc_cparam_t`).
///
/// Set on a [`zxc_cctx`] with [`zxc_cctx_set_param`]; the value 0 restores a
/// parameter's default, any other value must sit within [`zxc_cparam_bounds`].
pub type zxc_cparam_t = c_int;

/// Compression level, 1 to 7; default [`ZXC_LEVEL_DEFAULT`].
pub const ZXC_C_LEVEL: zxc_cparam_t = 1;
/// Block size in bytes, a power of two in 4 KB..2 MB; default 512 KB.
pub const ZXC_C_BLOCK_SIZE: zxc_cparam_t = 2;
/// Worker threads for the `FILE*` pipeline, at most [`ZXC_MAX_THREADS`];
/// default 0, auto-detect. The buffer, block and push APIs ignore it.
pub const ZXC_C_THREADS: zxc_cparam_t = 3;
/// 1 appends the block checksums and the archive digest; default 0.
pub const ZXC_C_CHECKSUM: zxc_cparam_t = 4;
/// 1 appends a seek table for random-access decompression; default 0.
pub const ZXC_C_SEEKABLE: zxc_cparam_t = 5;
/// Favors decode speed over size at levels 3-7: the block growth budget in
/// percent, 1 to [`ZXC_SPLIT_BUDGET_MAX`] ([`ZXC_SPLIT_BUDGET_DEFAULT`] is the
/// knee); default 0, off.
pub const ZXC_C_FAVOR_DECODE_SPEED: zxc_cparam_t = 6;

/// Decompression parameter identifier (mirrors the C enum `zxc_dparam_t`).
///
/// Set on a [`zxc_dctx`] with [`zxc_dctx_set_param`]; same rules as
/// [`zxc_cparam_t`], bounds from [`zxc_dparam_bounds`].
pub type zxc_dparam_t = c_int;

/// Worker threads for the `FILE*` pipeline, at most [`ZXC_MAX_THREADS`];
/// default 0, auto-detect.
pub const ZXC_D_THREADS: zxc_dparam_t = 1;
/// 1 verifies the block checksums and the archive digest when the archive
/// carries them; default 0, no verification.
pub const ZXC_D_CHECKSUM: zxc_dparam_t = 2;

/// Progress callback (mirrors `zxc_progress_callback_t`), called from the
/// writer thread of the `FILE*` pipeline after each block or batch of blocks.
///
/// Arguments: input bytes processed so far, total input bytes (0 if unknown),
/// and the `user_data` pointer given to [`zxc_cctx_set_progress`] /
/// [`zxc_dctx_set_progress`]. Keep it fast and non-blocking.
pub type zxc_progress_callback_t =
    Option<unsafe extern "C" fn(bytes_processed: u64, bytes_total: u64, user_data: *const c_void)>;

/// What a frame's header and footer declare (mirrors `zxc_frame_info_t`).
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct zxc_frame_info_t {
    /// Source bytes the frame decodes to.
    pub decompressed_size: u64,
    /// Compressed bytes of the frame, footer included.
    pub compressed_size: u64,
    /// Archive digest; 0 when `has_checksum` is 0.
    pub digest: u64,
    /// Block size, 4 KB to 2 MB.
    pub block_size: usize,
    /// Dictionary the frame needs; 0 for none.
    pub dict_id: u32,
    /// Format version of the frame.
    pub format_version: u8,
    /// 1 when blocks carry checksums and the footer a digest.
    pub has_checksum: u8,
    /// 1 when a seek table precedes the footer.
    pub has_seek_table: u8,
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

    /// Returns the library version as a null-terminated string.
    ///
    /// The returned pointer is a compile-time constant and must not be freed.
    pub fn zxc_version_string() -> *const c_char;

    /// Returns the library version as `MAJOR * 10000 + MINOR * 100 + PATCH`.
    ///
    /// Compare with the crate's `ZXC_VERSION_*` constants to detect a runtime
    /// library older than the one compiled against.
    pub fn zxc_version_number() -> c_uint;
}

// =============================================================================
// Buffer-Based API
// =============================================================================

unsafe extern "C" {
    /// Calculates the maximum compressed size for a given input.
    ///
    /// Useful for allocating output buffers before compression.
    pub fn zxc_compress_bound(input_size: usize) -> usize;

    /// Compresses a data buffer into a complete ZXC archive, at `level`.
    ///
    /// The simple form: one level, every other parameter at its default
    /// (512 KB blocks, no checksum, no seek table, no dictionary). For
    /// anything else, configure a [`zxc_cctx`] and call [`zxc_compress_cctx`].
    ///
    /// # Safety
    ///
    /// - `src` must be a valid pointer to `src_size` bytes.
    /// - `dst` must be a valid pointer to at least `dst_capacity` bytes.
    /// - The caller must ensure no data races on the buffers.
    ///
    /// # Returns
    ///
    /// Number of bytes written to `dst` (>0 on success), or a negative error
    /// code. `level` 0 selects the default; any other level outside 1..7 is
    /// `ZXC_ERROR_BAD_LEVEL`.
    pub fn zxc_compress(
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
        level: c_int,
    ) -> i64;

    /// Decompresses a complete ZXC archive.
    ///
    /// The simple form: no checksum verification and no dictionary. For
    /// either, configure a [`zxc_dctx`] and call [`zxc_decompress_dctx`].
    ///
    /// # Safety
    ///
    /// - `src` must be a valid pointer to `src_size` bytes of compressed data.
    /// - `dst` must be a valid pointer to at least `dst_capacity` bytes, or
    ///   NULL with `dst_capacity` 0 to probe the archive without decoding.
    ///
    /// # Returns
    ///
    /// Number of decompressed bytes written to `dst`, 0 for an empty archive,
    /// or a negative error code.
    pub fn zxc_decompress(
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
    ) -> i64;

    /// Returns the decompressed size stored in a ZXC compressed buffer.
    ///
    /// Reads the file footer without performing decompression.
    ///
    /// # Returns
    ///
    /// Original uncompressed size in bytes, or 0 if invalid.
    pub fn zxc_get_decompressed_size(src: *const c_void, src_size: usize) -> u64;

    /// Reads a frame's header and footer, without decoding.
    ///
    /// # Returns
    ///
    /// `ZXC_OK` with `*info` filled, or a negative error code.
    ///
    /// # Safety
    /// - `src` must be a valid pointer to `src_size` bytes.
    /// - `info` must be valid for writes of `info_size` bytes.
    pub fn zxc_get_frame_info(
        src: *const c_void,
        src_size: usize,
        info: *mut zxc_frame_info_t,
        info_size: usize,
    ) -> c_int;

    /// Returns `sizeof(zxc_frame_info_t)` as compiled into the C library.
    ///
    /// Layout guard: the Rust [`zxc_frame_info_t`] mirrors the C struct by
    /// hand; comparing sizes turns silent layout drift into a test failure
    /// (see the `frame_info_layout_matches_library` test).
    pub fn zxc_frame_info_size() -> usize;

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
    /// No checksum verification and no dictionary: for either, configure a
    /// [`zxc_dctx`] and call [`zxc_decompress_inplace_dctx`].
    ///
    /// # Returns
    ///
    /// Decompressed size (>0), 0 for an empty frame, or a negative error
    /// code (`ZXC_ERROR_DST_TOO_SMALL` if the margin is missing).
    pub fn zxc_decompress_inplace(
        buffer: *mut c_void,
        buffer_capacity: usize,
        comp_size: usize,
    ) -> i64;

    /// In-place decompression with a caller-supplied context.
    ///
    /// Same contract as `zxc_decompress_inplace`, with `dctx` as the decode
    /// context and its parameters. With a static context
    /// (`zxc_init_static_dctx`) the call allocates nothing; it rejects a
    /// foreign block size and any dictionary.
    pub fn zxc_decompress_inplace_dctx(
        dctx: *mut zxc_dctx,
        buffer: *mut c_void,
        buffer_capacity: usize,
        comp_size: usize,
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
    pub fn zxc_dict_train_content(
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
    pub fn zxc_dict_compute_id(
        dict: *const c_void,
        dict_size: usize,
        huf_lengths: *const c_void,
    ) -> u32;

    /// Returns the dictionary ID stored in a `.zxd` file buffer.
    ///
    /// # Safety
    /// - `buf` must be a valid pointer to `buf_size` bytes.
    ///
    /// Returns 0 if the buffer is not a valid `.zxd` file.
    pub fn zxc_dict_read_id(buf: *const c_void, buf_size: usize) -> u32;

    /// Returns the `.zxd` file size for a given content size
    /// (= 16 + `content_size` + 128 for the shared Huffman table).
    pub fn zxc_dict_save_bound(content_size: usize) -> usize;

    /// Trains the shared literal Huffman table for an already-trained dictionary.
    ///
    /// # Safety
    /// - `samples`/`sample_sizes` as in [`zxc_dict_train_content`].
    /// - `dict` must be valid for `dict_size` bytes.
    /// - `huf_lengths_out` must be valid for writes of 128 bytes
    ///   (`ZXC_HUF_TABLE_SIZE`).
    ///
    /// # Returns
    /// `ZXC_OK` (0) on success, or a negative `zxc_error_t` code.
    pub fn zxc_dict_train_huf(
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
    /// - `samples`/`sample_sizes` as in [`zxc_dict_train_content`].
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
    pub fn zxc_compress_block_bound(input_size: usize) -> usize;

    /// Returns the minimum `dst_capacity` required by [`zxc_decompress_block`]
    /// for a block of `uncompressed_size` bytes. Accounts for the wild-copy
    /// tail pad used by the fast decoder.
    pub fn zxc_decompress_block_bound(uncompressed_size: usize) -> usize;

    /// Compresses a single block without file framing, with the parameters
    /// in force on `cctx` (level, checksum, dictionary).
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
    ) -> i64;

    /// Decompresses a single block produced by [`zxc_compress_block`], with
    /// the parameters in force on `dctx` (checksum, dictionary).
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
    pub fn zxc_estimate_cctx_size(src_size: usize, level: c_int) -> usize;
}

// =============================================================================
// Reusable Context API (opaque, heap-allocated)
// =============================================================================

unsafe extern "C" {
    /// Creates a heap-allocated compression context, every parameter at its
    /// default (level 3, 512 KB blocks, no checksum, no seek table).
    ///
    /// Only the handle is allocated here; the working buffers come on the
    /// first call, sized from the parameters then in force. Configure it with
    /// [`zxc_cctx_set_param`], [`zxc_cctx_set_dict`] and
    /// [`zxc_cctx_set_progress`]. Returns NULL on allocation failure. Free
    /// with [`zxc_free_cctx`].
    pub fn zxc_create_cctx() -> *mut zxc_cctx;

    /// Frees a compression context. Safe to call with a null pointer.
    ///
    /// # Safety
    /// - `cctx` must be a pointer returned by [`zxc_create_cctx`] (or null).
    pub fn zxc_free_cctx(cctx: *mut zxc_cctx);

    /// Sets one parameter on `cctx`; 0 restores its default.
    ///
    /// Takes effect on the next call. A value outside [`zxc_cparam_bounds`]
    /// is refused and the parameter keeps its value: `ZXC_ERROR_BAD_LEVEL`
    /// for [`ZXC_C_LEVEL`], `ZXC_ERROR_BAD_BLOCK_SIZE` for
    /// [`ZXC_C_BLOCK_SIZE`] (a non-power-of-two included),
    /// `ZXC_ERROR_BAD_PARAM` otherwise, and for an unknown `param`. On a
    /// static context the block size is pinned, and a level raise into
    /// level 6 is refused when the workspace was carved below it.
    ///
    /// # Safety
    /// - `cctx` must be a valid pointer returned by [`zxc_create_cctx`] or
    ///   [`zxc_init_static_cctx`].
    pub fn zxc_cctx_set_param(cctx: *mut zxc_cctx, param: zxc_cparam_t, value: i64) -> c_int;

    /// Reads one parameter of `cctx` into `*value`.
    ///
    /// Returns `ZXC_OK`, `ZXC_ERROR_BAD_PARAM` for an unknown `param`, or
    /// `ZXC_ERROR_NULL_INPUT`.
    ///
    /// # Safety
    /// - `cctx` must be a valid context; `value` must be valid for writes.
    pub fn zxc_cctx_get_param(cctx: *const zxc_cctx, param: zxc_cparam_t, value: *mut i64)
    -> c_int;

    /// Reports the accepted range of a compression parameter.
    ///
    /// Returns `ZXC_OK`, or `ZXC_ERROR_BAD_PARAM` for an unknown `param`.
    ///
    /// # Safety
    /// - `min` and `max` must each be NULL or valid for writes.
    pub fn zxc_cparam_bounds(param: zxc_cparam_t, min: *mut i64, max: *mut i64) -> c_int;

    /// Attaches a pre-trained dictionary to `cctx`; a NULL `dict` detaches.
    ///
    /// The context keeps the pointers only: the bytes stay caller-owned and
    /// must outlive every call that uses them. `dict_huf`, when non-NULL, is
    /// the [`ZXC_HUF_TABLE_SIZE`]-byte shared literal table.
    ///
    /// Returns `ZXC_OK`; `ZXC_ERROR_DICT_TOO_LARGE` above
    /// [`ZXC_DICT_SIZE_MAX`]; `ZXC_ERROR_DICT_UNSUPPORTED` on a static context.
    ///
    /// # Safety
    /// - `cctx` must be a valid context.
    /// - `dict` must be NULL or valid for `dict_size` bytes for as long as it
    ///   stays attached; `dict_huf` NULL or valid for 128 bytes likewise.
    pub fn zxc_cctx_set_dict(
        cctx: *mut zxc_cctx,
        dict: *const c_void,
        dict_size: usize,
        dict_huf: *const c_void,
    ) -> c_int;

    /// Sets the progress callback; `None` disables it. Honoured by
    /// [`zxc_stream_compress`] only.
    ///
    /// # Safety
    /// - `cctx` must be a valid context; `user_data` is handed back to `cb`
    ///   unchanged and must stay valid while the callback is attached.
    pub fn zxc_cctx_set_progress(
        cctx: *mut zxc_cctx,
        cb: zxc_progress_callback_t,
        user_data: *mut c_void,
    ) -> c_int;

    /// Restores every parameter to its default, detaches the dictionary and
    /// the callback. The working buffers are kept. On a static context the
    /// pinned block size and level are the defaults.
    ///
    /// # Safety
    /// - `cctx` must be a valid context.
    pub fn zxc_cctx_reset(cctx: *mut zxc_cctx) -> c_int;

    /// Compresses a full file-framed buffer using a reusable context, with
    /// the parameters in force on it (level, block size, checksum, seek
    /// table, dictionary).
    ///
    /// # Safety
    /// - `cctx` must be a valid pointer returned by [`zxc_create_cctx`].
    pub fn zxc_compress_cctx(
        cctx: *mut zxc_cctx,
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
    ) -> i64;

    /// Creates a heap-allocated decompression context, every parameter at
    /// its default (no checksum verification, no dictionary).
    ///
    /// Configure it with [`zxc_dctx_set_param`], [`zxc_dctx_set_dict`] and
    /// [`zxc_dctx_set_progress`]. Returns NULL on allocation failure. Free
    /// with [`zxc_free_dctx`].
    pub fn zxc_create_dctx() -> *mut zxc_dctx;

    /// Frees a decompression context. Safe to call with a null pointer.
    ///
    /// # Safety
    /// - `dctx` must be a pointer returned by [`zxc_create_dctx`] (or null).
    pub fn zxc_free_dctx(dctx: *mut zxc_dctx);

    /// Sets one parameter on `dctx`; 0 restores its default.
    ///
    /// A value outside [`zxc_dparam_bounds`] is `ZXC_ERROR_BAD_PARAM`, as is
    /// an unknown `param`; the parameter keeps its value.
    ///
    /// # Safety
    /// - `dctx` must be a valid pointer returned by [`zxc_create_dctx`] or
    ///   [`zxc_init_static_dctx`].
    pub fn zxc_dctx_set_param(dctx: *mut zxc_dctx, param: zxc_dparam_t, value: i64) -> c_int;

    /// Reads one parameter of `dctx` into `*value`. See [`zxc_cctx_get_param`].
    ///
    /// # Safety
    /// - `dctx` must be a valid context; `value` must be valid for writes.
    pub fn zxc_dctx_get_param(dctx: *const zxc_dctx, param: zxc_dparam_t, value: *mut i64)
    -> c_int;

    /// Reports the accepted range of a decompression parameter. See
    /// [`zxc_cparam_bounds`].
    ///
    /// # Safety
    /// - `min` and `max` must each be NULL or valid for writes.
    pub fn zxc_dparam_bounds(param: zxc_dparam_t, min: *mut i64, max: *mut i64) -> c_int;

    /// Attaches the dictionary the archives were compressed with; NULL
    /// detaches. Same ownership rules as [`zxc_cctx_set_dict`]; the header's
    /// dict_id is checked against the (dict, table) pair at decode time.
    ///
    /// # Safety
    /// - `dctx` must be a valid context.
    /// - `dict` must be NULL or valid for `dict_size` bytes for as long as it
    ///   stays attached; `dict_huf` NULL or valid for 128 bytes likewise.
    pub fn zxc_dctx_set_dict(
        dctx: *mut zxc_dctx,
        dict: *const c_void,
        dict_size: usize,
        dict_huf: *const c_void,
    ) -> c_int;

    /// Sets the progress callback; `None` disables it. Honoured by
    /// [`zxc_stream_decompress`] only.
    ///
    /// # Safety
    /// - `dctx` must be a valid context; `user_data` is handed back to `cb`
    ///   unchanged and must stay valid while the callback is attached.
    pub fn zxc_dctx_set_progress(
        dctx: *mut zxc_dctx,
        cb: zxc_progress_callback_t,
        user_data: *mut c_void,
    ) -> c_int;

    /// Restores every parameter to its default, detaches the dictionary and
    /// the callback. The working buffers are kept.
    ///
    /// # Safety
    /// - `dctx` must be a valid context.
    pub fn zxc_dctx_reset(dctx: *mut zxc_dctx) -> c_int;

    /// Decompresses a full file-framed buffer using a reusable context, with
    /// the parameters in force on it (checksum, dictionary).
    ///
    /// # Safety
    /// - `dctx` must be a valid pointer returned by [`zxc_create_dctx`].
    pub fn zxc_decompress_dctx(
        dctx: *mut zxc_dctx,
        src: *const c_void,
        src_size: usize,
        dst: *mut c_void,
        dst_capacity: usize,
    ) -> i64;

    /// Returns the exact byte count required by a static compression
    /// workspace for the given `block_size` and `level`.
    ///
    /// # Returns
    ///
    /// Workspace size in bytes, or 0 if either argument is invalid.
    pub fn zxc_static_cctx_workspace_size(block_size: usize, level: c_int) -> usize;

    /// Initialises a compression context inside a caller-supplied workspace
    /// (no heap allocation), carved for `block_size` and `level` (1..7).
    ///
    /// Both are pinned: [`zxc_cctx_set_param`] refuses another block size
    /// and a level raise into level 6 when `level` was below it; checksum,
    /// seek table and lower levels are honoured. [`zxc_cctx_set_dict`]
    /// returns `ZXC_ERROR_DICT_UNSUPPORTED`. Returns NULL if the workspace
    /// is too small or a parameter is invalid.
    ///
    /// # Safety
    /// - `workspace` must be cache-line (64-byte) aligned, at least
    ///   `zxc_static_cctx_workspace_size(block_size, level)` bytes, and
    ///   outlive the handle.
    /// - `zxc_free_cctx` is a no-op on the returned handle.
    pub fn zxc_init_static_cctx(
        workspace: *mut c_void,
        workspace_size: usize,
        block_size: usize,
        level: c_int,
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
    /// - `cfg` must be NULL or a valid [`zxc_cctx`]
    ///
    /// # Arguments
    ///
    /// * `f_in` - Input file stream
    /// * `f_out` - Output file stream
    /// * `cfg` - Configured compression context, read for its parameters
    ///   (level, block size, threads, checksum, seek table, dictionary,
    ///   progress callback), or NULL for defaults. Its buffers are not used:
    ///   it is configuration only and may be freed after the call.
    ///
    /// # Returns
    ///
    /// Total compressed bytes written, or a negative error code.
    pub fn zxc_stream_compress(
        f_in: *mut libc::FILE,
        f_out: *mut libc::FILE,
        cfg: *const zxc_cctx,
    ) -> i64;

    /// Decompresses data from an input stream to an output stream.
    ///
    /// Uses the same pipeline architecture as compression for maximum throughput.
    ///
    /// # Safety
    ///
    /// - `f_in` must be a valid FILE* opened in "rb" mode
    /// - `f_out` must be a valid FILE* opened in "wb" mode
    /// - `cfg` must be NULL or a valid [`zxc_dctx`]
    ///
    /// # Arguments
    ///
    /// * `f_in` - Input file stream (compressed data)
    /// * `f_out` - Output file stream (decompressed data)
    /// * `cfg` - Configured decompression context, read for its parameters
    ///   (threads, checksum, dictionary, progress callback), or NULL for
    ///   defaults. Its buffers are not used.
    ///
    /// # Returns
    ///
    /// Total decompressed bytes written, or a negative error code.
    pub fn zxc_stream_decompress(
        f_in: *mut libc::FILE,
        f_out: *mut libc::FILE,
        cfg: *const zxc_dctx,
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
    /// The reader is invoked to fetch the file header, footer, and EOF/SEK
    /// block headers at open time (3 reads), then once per seek table group
    /// and once per block during decompression.
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

    pub fn zxc_seekable_set_checksum(s: *mut zxc_seekable, enabled: c_int) -> c_int;

    /// Returns the total number of data blocks in the archive (excluding EOF).
    ///
    /// Derived from the footer, never stored, so only the archive size bounds it.
    pub fn zxc_seekable_get_num_blocks(s: *const zxc_seekable) -> u64;

    /// Returns the total decompressed size of the archive in bytes.
    pub fn zxc_seekable_get_decompressed_size(s: *const zxc_seekable) -> u64;

    /// Returns the on-disk compressed size of a specific block
    /// (block header + payload + optional per-block checksum).
    ///
    /// Returns 0 if `block_idx` is out of range.
    pub fn zxc_seekable_get_block_comp_size(s: *const zxc_seekable, block_idx: u64) -> u32;

    /// Returns the decompressed size of a specific block.
    ///
    /// Returns 0 if `block_idx` is out of range.
    pub fn zxc_seekable_get_block_decomp_size(s: *const zxc_seekable, block_idx: u64) -> u32;

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
        num_blocks: u64,
    ) -> i64;

    /// Returns the encoded byte size of a seek table for `num_blocks` blocks,
    /// or 0 when that size does not fit a `usize`.
    pub fn zxc_seek_table_size(num_blocks: u64) -> usize;
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
    /// Creates a push compression stream.
    ///
    /// The parameters are copied from `cfg` at creation; the context may be
    /// freed or reused afterwards. Only [`ZXC_C_LEVEL`], [`ZXC_C_BLOCK_SIZE`]
    /// and [`ZXC_C_CHECKSUM`] are honoured; threads, seek table and the
    /// progress callback are ignored. A dictionary on `cfg` fails creation.
    /// Returns NULL on allocation failure or a dictionary on `cfg`. Free with
    /// [`zxc_cstream_free`].
    ///
    /// # Safety
    /// - `cfg` must be NULL (all defaults) or a valid [`zxc_cctx`].
    pub fn zxc_cstream_create(cfg: *const zxc_cctx) -> *mut zxc_cstream;

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

    /// Creates a push decompression stream.
    ///
    /// The parameters are copied from `cfg` at creation; only
    /// [`ZXC_D_CHECKSUM`] is honoured. A dictionary on `cfg` fails creation.
    /// Returns NULL on allocation failure or a dictionary on `cfg`. Free with
    /// [`zxc_dstream_free`].
    ///
    /// # Safety
    /// - `cfg` must be NULL (all defaults) or a valid [`zxc_dctx`].
    pub fn zxc_dstream_create(cfg: *const zxc_dctx) -> *mut zxc_dstream;

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
    use super::*;

    /// `zxc_frame_info_t` is hand-mirrored from zxc_buffer.h. A C-side field
    /// change that is not replicated here silently shifts every later field
    /// (undefined behaviour at the FFI boundary); this test turns that drift
    /// into a hard failure.
    #[test]
    fn frame_info_layout_matches_library() {
        unsafe {
            assert_eq!(
                std::mem::size_of::<zxc_frame_info_t>(),
                zxc_frame_info_size(),
                "zxc_frame_info_t layout drift: update the Rust mirror in zxc-sys"
            );
        }
    }

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
    fn test_version_number() {
        let header = ZXC_VERSION_MAJOR * 10000 + ZXC_VERSION_MINOR * 100 + ZXC_VERSION_PATCH;
        assert_eq!(unsafe { zxc_version_number() }, header);
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

            // Compress with checksums: that needs a configured context.
            let cctx = zxc_create_cctx();
            assert!(!cctx.is_null());
            assert_eq!(
                zxc_cctx_set_param(cctx, ZXC_C_LEVEL, ZXC_LEVEL_DEFAULT as i64),
                ZXC_OK
            );
            assert_eq!(zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, 1), ZXC_OK);
            let compressed_size = zxc_compress_cctx(
                cctx,
                input.as_ptr() as *const c_void,
                input.len(),
                compressed.as_mut_ptr() as *mut c_void,
                compressed.len(),
            );
            zxc_free_cctx(cctx);
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

            // Decompress, verifying the checksums.
            let mut decompressed = vec![0u8; decompressed_size as usize];
            let dctx = zxc_create_dctx();
            assert!(!dctx.is_null());
            assert_eq!(zxc_dctx_set_param(dctx, ZXC_D_CHECKSUM, 1), ZXC_OK);
            let result_size = zxc_decompress_dctx(
                dctx,
                compressed.as_ptr() as *const c_void,
                compressed_size as usize,
                decompressed.as_mut_ptr() as *mut c_void,
                decompressed.len(),
            );
            zxc_free_dctx(dctx);
            assert_eq!(result_size, input.len() as i64);
            assert_eq!(&decompressed[..], &input[..]);

            // The simple forms agree with the configured ones.
            let simple_size = zxc_compress(
                input.as_ptr() as *const c_void,
                input.len(),
                compressed.as_mut_ptr() as *mut c_void,
                compressed.len(),
                ZXC_LEVEL_DEFAULT,
            );
            assert!(simple_size > 0, "zxc_compress failed");
            let result_size = zxc_decompress(
                compressed.as_ptr() as *const c_void,
                simple_size as usize,
                decompressed.as_mut_ptr() as *mut c_void,
                decompressed.len(),
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
                let bound = zxc_compress_bound(input.len());
                let mut compressed = vec![0u8; bound];

                let compressed_size = zxc_compress(
                    input.as_ptr() as *const c_void,
                    input.len(),
                    compressed.as_mut_ptr() as *mut c_void,
                    compressed.len(),
                    level,
                );
                assert!(compressed_size > 0, "Compression failed at level {}", level);
            }
        }
    }

    /// The parameter contract the safe crate relies on: bounds, set/get, 0
    /// restores the default, out-of-range refused with the documented code,
    /// unknown identifier refused, reset.
    #[test]
    fn test_context_params() {
        unsafe {
            let (mut lo, mut hi) = (0i64, 0i64);
            assert_eq!(zxc_cparam_bounds(ZXC_C_LEVEL, &mut lo, &mut hi), ZXC_OK);
            assert_eq!((lo, hi), (ZXC_LEVEL_FASTEST as i64, ZXC_LEVEL_ULTRA as i64));
            assert_eq!(zxc_cparam_bounds(ZXC_C_THREADS, &mut lo, &mut hi), ZXC_OK);
            assert_eq!((lo, hi), (0, ZXC_MAX_THREADS));
            assert_eq!(
                zxc_cparam_bounds(99, std::ptr::null_mut(), std::ptr::null_mut()),
                ZXC_ERROR_BAD_PARAM
            );
            assert_eq!(zxc_dparam_bounds(ZXC_D_CHECKSUM, &mut lo, &mut hi), ZXC_OK);
            assert_eq!((lo, hi), (0, 1));
            assert_eq!(
                zxc_dparam_bounds(99, std::ptr::null_mut(), std::ptr::null_mut()),
                ZXC_ERROR_BAD_PARAM
            );

            let cctx = zxc_create_cctx();
            assert!(!cctx.is_null());
            let mut v = -1i64;
            assert_eq!(zxc_cctx_get_param(cctx, ZXC_C_LEVEL, &mut v), ZXC_OK);
            assert_eq!(v, ZXC_LEVEL_DEFAULT as i64);
            assert_eq!(zxc_cctx_get_param(cctx, ZXC_C_BLOCK_SIZE, &mut v), ZXC_OK);
            assert_eq!(v, 512 * 1024);

            assert_eq!(zxc_cctx_set_param(cctx, ZXC_C_LEVEL, 5), ZXC_OK);
            assert_eq!(zxc_cctx_get_param(cctx, ZXC_C_LEVEL, &mut v), ZXC_OK);
            assert_eq!(v, 5);
            assert_eq!(
                zxc_cctx_set_param(cctx, ZXC_C_LEVEL, 8),
                ZXC_ERROR_BAD_LEVEL
            );
            assert_eq!(
                zxc_cctx_set_param(cctx, ZXC_C_BLOCK_SIZE, 3000),
                ZXC_ERROR_BAD_BLOCK_SIZE
            );
            assert_eq!(
                zxc_cctx_set_param(cctx, ZXC_C_CHECKSUM, 2),
                ZXC_ERROR_BAD_PARAM
            );
            assert_eq!(zxc_cctx_set_param(cctx, 99, 1), ZXC_ERROR_BAD_PARAM);
            assert_eq!(zxc_cctx_get_param(cctx, ZXC_C_LEVEL, &mut v), ZXC_OK);
            assert_eq!(v, 5, "a refused value must not change the parameter");

            assert_eq!(zxc_cctx_set_param(cctx, ZXC_C_LEVEL, 0), ZXC_OK);
            assert_eq!(zxc_cctx_get_param(cctx, ZXC_C_LEVEL, &mut v), ZXC_OK);
            assert_eq!(v, ZXC_LEVEL_DEFAULT as i64, "0 restores the default");

            assert_eq!(zxc_cctx_set_param(cctx, ZXC_C_SEEKABLE, 1), ZXC_OK);
            assert_eq!(zxc_cctx_reset(cctx), ZXC_OK);
            assert_eq!(zxc_cctx_get_param(cctx, ZXC_C_SEEKABLE, &mut v), ZXC_OK);
            assert_eq!(v, 0, "reset restores the defaults");
            zxc_free_cctx(cctx);

            let dctx = zxc_create_dctx();
            assert!(!dctx.is_null());
            assert_eq!(zxc_dctx_set_param(dctx, ZXC_D_CHECKSUM, 1), ZXC_OK);
            assert_eq!(zxc_dctx_get_param(dctx, ZXC_D_CHECKSUM, &mut v), ZXC_OK);
            assert_eq!(v, 1);
            assert_eq!(
                zxc_dctx_set_param(dctx, ZXC_D_THREADS, ZXC_MAX_THREADS + 1),
                ZXC_ERROR_BAD_PARAM
            );
            assert_eq!(zxc_dctx_set_param(dctx, 99, 1), ZXC_ERROR_BAD_PARAM);
            assert_eq!(zxc_dctx_reset(dctx), ZXC_OK);
            assert_eq!(zxc_dctx_get_param(dctx, ZXC_D_CHECKSUM, &mut v), ZXC_OK);
            assert_eq!(v, 0);
            zxc_free_dctx(dctx);
        }
    }
}
