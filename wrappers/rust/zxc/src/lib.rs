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
//! - **Checksum verification**: Optional, disabled by default for maximum performance
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
/// # Arguments
///
/// * `data` - The data to compress
/// * `level` - Compression level
/// * `checksum` - Optional checksum for data integrity (`None` = disabled for maximum performance)
///
/// # Example
///
/// ```rust
/// use zxc::{compress, Level};
///
/// let data = b"Hello, world!";
///
/// // Maximum performance (no checksum)
/// let compressed = compress(data, Level::Default, None)?;
///
/// // With data integrity verification
/// let compressed = compress(data, Level::Default, Some(true))?;
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn compress(data: &[u8], level: Level, checksum: Option<bool>) -> Result<Vec<u8>> {
    let opts = CompressOptions {
        level,
        checksum: checksum.unwrap_or(false),
    };
    compress_with_options(data, &opts)
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
    let mut output = Vec::with_capacity(bound);

    let written = unsafe {
        impl_compress(
            data,
            output.as_mut_ptr(),
            output.capacity(),
            options
        )?
    };

    unsafe { output.set_len(written); }
    Ok(output)
}

/// Helper to handle the raw compression call.
///
/// # Safety
///
/// `dst_ptr` must be valid for writes up to `dst_cap` bytes.
#[inline(always)]
unsafe fn impl_compress(
    data: &[u8],
    dst_ptr: *mut u8,
    dst_cap: usize,
    options: &CompressOptions,
) -> Result<usize> {
    let written = unsafe {
        zxc_sys::zxc_compress(
            data.as_ptr() as *const c_void,
            data.len(),
            dst_ptr as *mut c_void,
            dst_cap,
            options.level as i32,
            options.checksum as i32,
        )
    };

    if written == 0 && !data.is_empty() {
        return Err(Error::CompressionFailed);
    }

    Ok(written)
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
    unsafe {
        impl_compress(data, output.as_mut_ptr(), output.len(), options)
    }
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
/// let compressed = compress(data, Level::Default, None)?;
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
/// let compressed = compress(data, Level::Default, None)?;
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
    let mut output = Vec::with_capacity(size);

    let written = unsafe {
        impl_decompress(
            compressed,
            output.as_mut_ptr(),
            output.capacity(),
            options
        )?
    };

    if written != size {
        return Err(Error::DecompressionFailed("size mismatch"));
    }

    unsafe { output.set_len(written); }
    Ok(output)
}

/// Helper to handle the raw decompression call.
///
/// # Safety
///
/// `dst_ptr` must be valid for writes up to `dst_cap` bytes.
#[inline(always)]
unsafe fn impl_decompress(
    compressed: &[u8],
    dst_ptr: *mut u8,
    dst_cap: usize,
    options: &DecompressOptions,
) -> Result<usize> {
    let written = unsafe {
        zxc_sys::zxc_decompress(
            compressed.as_ptr() as *const c_void,
            compressed.len(),
            dst_ptr as *mut c_void,
            dst_cap,
            if options.verify_checksum { 1 } else { 0 },
        )
    };

    if written == 0 && !compressed.is_empty() {
        return Err(Error::DecompressionFailed("invalid data or buffer too small"));
    }

    Ok(written)
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
    unsafe {
        impl_decompress(compressed, output.as_mut_ptr(), output.len(), options)
    }
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
// Streaming API (File-based)
// =============================================================================

use std::path::Path;
use std::fs::File;
use std::io;

#[cfg(unix)]
use std::os::unix::io::AsRawFd;


/// Options for streaming compression operations.
#[derive(Debug, Clone)]
pub struct StreamCompressOptions {
    /// Compression level (default: `Level::Default`)
    pub level: Level,
    /// Number of worker threads (default: `None` = auto-detect CPU cores)
    pub threads: Option<usize>,
    /// Enable checksum for data integrity (default: `true`)
    pub checksum: bool,
}

impl Default for StreamCompressOptions {
    fn default() -> Self {
        Self {
            level: Level::Default,
            threads: None,
            checksum: true,
        }
    }
}

impl StreamCompressOptions {
    /// Create options with the specified compression level.
    pub fn with_level(level: Level) -> Self {
        Self {
            level,
            ..Default::default()
        }
    }

    /// Set the number of worker threads.
    pub fn threads(mut self, n: usize) -> Self {
        self.threads = Some(n);
        self
    }

    /// Disable checksum computation.
    pub fn without_checksum(mut self) -> Self {
        self.checksum = false;
        self
    }
}

/// Options for streaming decompression operations.
#[derive(Debug, Clone, Default)]
pub struct StreamDecompressOptions {
    /// Number of worker threads (default: `None` = auto-detect CPU cores)
    pub threads: Option<usize>,
    /// Verify checksum during decompression (default: `true`)
    pub verify_checksum: bool,
}

impl StreamDecompressOptions {
    /// Set the number of worker threads.
    pub fn threads(mut self, n: usize) -> Self {
        self.threads = Some(n);
        self
    }

    /// Skip checksum verification.
    pub fn skip_checksum(mut self) -> Self {
        self.verify_checksum = false;
        self
    }
}

/// Extend Error enum for I/O errors
#[derive(Debug, thiserror::Error)]
pub enum StreamError {
    /// I/O error during file operations
    #[error("I/O error: {0}")]
    Io(#[from] io::Error),

    /// Streaming compression failed
    #[error("stream compression failed")]
    CompressionFailed,

    /// Streaming decompression failed
    #[error("stream decompression failed")]
    DecompressionFailed,

    /// Invalid compressed file
    #[error("invalid compressed file")]
    InvalidFile,
}

/// Result type for streaming operations.
pub type StreamResult<T> = std::result::Result<T, StreamError>;

/// Convert a Rust File to a C FILE* for read operations.
/// 
/// This function duplicates the file descriptor before passing it to fdopen,
/// so the returned FILE* owns its own fd and must be closed with fclose().
#[cfg(unix)]
unsafe fn file_to_c_file_read(file: &File) -> *mut libc::FILE {
    let fd = file.as_raw_fd();
    // Duplicate the fd so C FILE* has its own ownership
    let dup_fd = unsafe { libc::dup(fd) };
    if dup_fd < 0 {
        return std::ptr::null_mut();
    }
    
    let file_ptr = unsafe { libc::fdopen(dup_fd, c"rb".as_ptr()) };
    if file_ptr.is_null() {
        // fdopen failed, close the duplicated fd to avoid leak
        unsafe { libc::close(dup_fd); }
    }
    file_ptr
}

/// Convert a Rust File to a C FILE* for write operations.
///
/// This function duplicates the file descriptor before passing it to fdopen,
/// so the returned FILE* owns its own fd and must be closed with fclose().
#[cfg(unix)]
unsafe fn file_to_c_file_write(file: &File) -> *mut libc::FILE {
    let fd = file.as_raw_fd();
    // Duplicate the fd so C FILE* has its own ownership
    let dup_fd = unsafe { libc::dup(fd) };
    if dup_fd < 0 {
        return std::ptr::null_mut();
    }
    
    let file_ptr = unsafe { libc::fdopen(dup_fd, c"wb".as_ptr()) };
    if file_ptr.is_null() {
        // fdopen failed, close the duplicated fd to avoid leak
        unsafe { libc::close(dup_fd); }
    }
    file_ptr
}

/// Convert a Rust File to a C FILE* for read operations (Windows).
///
/// This function duplicates the file handle before passing it to the C runtime,
/// so the returned FILE* owns its own handle and must be closed with fclose().
#[cfg(windows)]
unsafe fn file_to_c_file_read(file: &File) -> *mut libc::FILE {
    use std::os::windows::io::AsRawHandle;
    
    let handle = file.as_raw_handle();
    
    // Duplicate the handle so C FILE* has its own ownership
    let mut dup_handle: *mut std::ffi::c_void = std::ptr::null_mut();
    let result = unsafe {
        windows_sys::Win32::Foundation::DuplicateHandle(
            windows_sys::Win32::System::Threading::GetCurrentProcess(),
            handle as *mut std::ffi::c_void,
            windows_sys::Win32::System::Threading::GetCurrentProcess(),
            &mut dup_handle,
            0,
            0,
            windows_sys::Win32::Foundation::DUPLICATE_SAME_ACCESS,
        )
    };
    
    if result == 0 {
        return std::ptr::null_mut();
    }
    
    let fd = libc::open_osfhandle(dup_handle as libc::intptr_t, libc::O_RDONLY);
    if fd < 0 {
        // open_osfhandle failed, close the duplicated handle to avoid leak
        unsafe { windows_sys::Win32::Foundation::CloseHandle(dup_handle); }
        return std::ptr::null_mut();
    }
    
    let file_ptr = libc::fdopen(fd, c"rb".as_ptr());
    if file_ptr.is_null() {
        // fdopen failed, close the fd (which will close the handle)
        unsafe { libc::close(fd); }
    }
    file_ptr
}

/// Convert a Rust File to a C FILE* for write operations (Windows).
///
/// This function duplicates the file handle before passing it to the C runtime,
/// so the returned FILE* owns its own handle and must be closed with fclose().
#[cfg(windows)]
unsafe fn file_to_c_file_write(file: &File) -> *mut libc::FILE {
    use std::os::windows::io::AsRawHandle;
    
    let handle = file.as_raw_handle();
    
    // Duplicate the handle so C FILE* has its own ownership
    let mut dup_handle: *mut std::ffi::c_void = std::ptr::null_mut();
    let result = unsafe {
        windows_sys::Win32::Foundation::DuplicateHandle(
            windows_sys::Win32::System::Threading::GetCurrentProcess(),
            handle as *mut std::ffi::c_void,
            windows_sys::Win32::System::Threading::GetCurrentProcess(),
            &mut dup_handle,
            0,
            0,
            windows_sys::Win32::Foundation::DUPLICATE_SAME_ACCESS,
        )
    };
    
    if result == 0 {
        return std::ptr::null_mut();
    }
    
    let fd = libc::open_osfhandle(dup_handle as libc::intptr_t, libc::O_WRONLY);
    if fd < 0 {
        // open_osfhandle failed, close the duplicated handle to avoid leak
        unsafe { windows_sys::Win32::Foundation::CloseHandle(dup_handle); }
        return std::ptr::null_mut();
    }
    
    let file_ptr = libc::fdopen(fd, c"wb".as_ptr());
    if file_ptr.is_null() {
        // fdopen failed, close the fd (which will close the handle)
        unsafe { libc::close(fd); }
    }
    file_ptr
}

/// Compresses a file using multi-threaded streaming.
///
/// This is the recommended method for compressing large files, as it:
/// - Processes data in chunks without loading the entire file into memory
/// - Uses multiple CPU cores for parallel compression
/// - Provides better throughput for files larger than a few MB
///
/// # Arguments
///
/// * `input` - Path to the input file
/// * `output` - Path to the output file
/// * `level` - Compression level
/// * `threads` - Number of threads (`None` = auto-detect CPU cores)
/// * `checksum` - Optional checksum for data integrity (`None` = disabled for maximum performance)
///
/// # Example
///
/// ```rust,no_run
/// use zxc::{compress_file, Level};
///
/// // Maximum performance (no checksum, auto threads)
/// let bytes = compress_file("input.bin", "output.zxc", Level::Default, None, None)?;
///
/// // With data integrity verification
/// let bytes = compress_file("input.bin", "output.zxc", Level::Default, None, Some(true))?;
///
/// // Custom configuration
/// let bytes = compress_file("input.bin", "output.zxc", Level::Compact, Some(4), Some(true))?;
/// # Ok::<(), zxc::StreamError>(())
/// ```
pub fn compress_file<P: AsRef<Path>>(
    input: P,
    output: P,
    level: Level,
    threads: Option<usize>,
    checksum: Option<bool>,
) -> StreamResult<u64> {
    let f_in = File::open(input)?;
    let f_out = File::create(output)?;

    let n_threads = threads.unwrap_or(0) as i32;
    let checksum_enabled = if checksum.unwrap_or(false) { 1 } else { 0 };

    unsafe {
        let c_in = file_to_c_file_read(&f_in);
        let c_out = file_to_c_file_write(&f_out);

        // Check for errors and cleanup on failure
        if c_in.is_null() {
            if !c_out.is_null() {
                libc::fclose(c_out);
            }
            return Err(StreamError::Io(io::Error::last_os_error()));
        }
        if c_out.is_null() {
            libc::fclose(c_in);
            return Err(StreamError::Io(io::Error::last_os_error()));
        }

        let result = zxc_sys::zxc_stream_compress(
            c_in,
            c_out,
            n_threads,
            level as i32,
            checksum_enabled,
        );

        // Always close C FILE handles (they own duplicated fds)
        libc::fclose(c_in);
        libc::fclose(c_out);

        if result < 0 {
            Err(StreamError::CompressionFailed)
        } else {
            Ok(result as u64)
        }
    }
}

/// Decompresses a file using multi-threaded streaming.
///
/// # Example
///
/// ```rust,no_run
/// use zxc::decompress_file;
///
/// // Decompress with auto-detected thread count
/// let bytes = decompress_file("compressed.zxc", "output.bin", None)?;
/// println!("Decompressed {} bytes", bytes);
/// # Ok::<(), zxc::StreamError>(())
/// ```
pub fn decompress_file<P: AsRef<Path>>(
    input: P,
    output: P,
    threads: Option<usize>,
) -> StreamResult<u64> {
    let f_in = File::open(input)?;
    let f_out = File::create(output)?;

    let n_threads = threads.unwrap_or(0) as i32;
    let checksum_enabled = 1; // Default to verify

    unsafe {
        let c_in = file_to_c_file_read(&f_in);
        let c_out = file_to_c_file_write(&f_out);

        // Check for errors and cleanup on failure
        if c_in.is_null() {
            if !c_out.is_null() {
                libc::fclose(c_out);
            }
            return Err(StreamError::Io(io::Error::last_os_error()));
        }
        if c_out.is_null() {
            libc::fclose(c_in);
            return Err(StreamError::Io(io::Error::last_os_error()));
        }

        let result = zxc_sys::zxc_stream_decompress(
            c_in,
            c_out,
            n_threads,
            checksum_enabled,
        );

        // Always close C FILE handles (they own duplicated fds)
        libc::fclose(c_in);
        libc::fclose(c_out);

        if result < 0 {
            Err(StreamError::DecompressionFailed)
        } else {
            Ok(result as u64)
        }
    }
}

/// Returns the decompressed size stored in a compressed file.
///
/// This reads the file footer without performing decompression,
/// useful for pre-allocating buffers or showing progress.
///
/// # Example
///
/// ```rust,no_run
/// use zxc::file_decompressed_size;
///
/// let size = file_decompressed_size("compressed.zxc")?;
/// println!("Original size: {} bytes", size);
/// # Ok::<(), zxc::StreamError>(())
/// ```
pub fn file_decompressed_size<P: AsRef<Path>>(path: P) -> StreamResult<u64> {
    let f = File::open(path)?;

    unsafe {
        let c_file = file_to_c_file_read(&f);

        if c_file.is_null() {
            return Err(StreamError::Io(io::Error::last_os_error()));
        }

        let result = zxc_sys::zxc_stream_get_decompressed_size(c_file);

        if result < 0 {
            Err(StreamError::InvalidFile)
        } else {
            Ok(result as u64)
        }
    }
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
        let compressed = compress(data, Level::Default, None).unwrap();
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn test_all_levels() {
        let data = b"Test data with repetition: CCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";

        for level in Level::all() {
            let compressed = compress(data, *level, None).unwrap();
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
        let compressed = compress(data, Level::Default, None).unwrap();
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
        let compressed = compress(data, Level::Default, None).unwrap();
        let size = decompressed_size(&compressed);
        assert_eq!(size, Some(data.len()));
    }

    #[test]
    fn test_version() {
        let (major, minor, patch) = version();

        let expected = format!("{}.{}.{}", major, minor, patch);
        assert_eq!(version_string(), expected);

        let cargo_version = env!("CARGO_PKG_VERSION");
        assert_eq!(expected, cargo_version);
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

        let compressed = compress(&data, Level::Default, None).unwrap();
        assert!(compressed.len() < data.len()); // Should compress

        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(decompressed, data);
    }
}

// =============================================================================
// Streaming API Tests
// =============================================================================

#[cfg(test)]
mod streaming_tests {
    use super::*;
    use std::fs;
    use std::io::Write;

    fn temp_path(name: &str) -> String {
        format!("/tmp/zxc_test_{}", name)
    }

    #[test]
    fn test_file_roundtrip() {
        let input_path = temp_path("roundtrip_input.bin");
        let compressed_path = temp_path("roundtrip_compressed.zxc");
        let output_path = temp_path("roundtrip_output.bin");

        // Create test data
        let data: Vec<u8> = (0..64 * 1024) // 64 KB
            .map(|i| ((i % 256) ^ ((i / 256) % 256)) as u8)
            .collect();

        // Write test file
        {
            let mut f = fs::File::create(&input_path).unwrap();
            f.write_all(&data).unwrap();
        }

        // Compress
        let compressed_size = compress_file(&input_path, &compressed_path, Level::Default, None, None).unwrap();
        assert!(compressed_size > 0);

        // Decompress
        let decompressed_size = decompress_file(&compressed_path, &output_path, None).unwrap();
        assert_eq!(decompressed_size, data.len() as u64);

        // Verify content
        let result = fs::read(&output_path).unwrap();
        assert_eq!(result, data);

        // Cleanup
        let _ = fs::remove_file(&input_path);
        let _ = fs::remove_file(&compressed_path);
        let _ = fs::remove_file(&output_path);
    }

    #[test]
    fn test_file_decompressed_size_query() {
        let input_path = temp_path("size_input.bin");
        let compressed_path = temp_path("size_compressed.zxc");

        // Create test data
        let data: Vec<u8> = (0..128 * 1024) // 128 KB
            .map(|i| (i % 256) as u8)
            .collect();

        // Write and compress
        {
            let mut f = fs::File::create(&input_path).unwrap();
            f.write_all(&data).unwrap();
        }
        compress_file(&input_path, &compressed_path, Level::Default, None, None).unwrap();

        // Query size
        let reported_size = file_decompressed_size(&compressed_path).unwrap();
        assert_eq!(reported_size, data.len() as u64);

        // Cleanup
        let _ = fs::remove_file(&input_path);
        let _ = fs::remove_file(&compressed_path);
    }

    #[test]
    fn test_file_all_levels() {
        let input_path = temp_path("levels_input.bin");

        // Create test data
        let data: Vec<u8> = (0..32 * 1024) // 32 KB
            .map(|i| ((i % 256) ^ ((i / 256) % 256)) as u8)
            .collect();

        {
            let mut f = fs::File::create(&input_path).unwrap();
            f.write_all(&data).unwrap();
        }

        for level in Level::all() {
            let compressed_path = temp_path(&format!("levels_{:?}.zxc", level));
            let output_path = temp_path(&format!("levels_{:?}_out.bin", level));

            // Compress with this level
            compress_file(&input_path, &compressed_path, *level, Some(2), None).unwrap();

            // Decompress
            decompress_file(&compressed_path, &output_path, Some(2)).unwrap();

            // Verify
            let result = fs::read(&output_path).unwrap();
            assert_eq!(result, data, "Data mismatch at level {:?}", level);

            // Cleanup
            let _ = fs::remove_file(&compressed_path);
            let _ = fs::remove_file(&output_path);
        }

        let _ = fs::remove_file(&input_path);
    }

    #[test]
    fn test_file_multithreaded() {
        let input_path = temp_path("mt_input.bin");
        let compressed_path = temp_path("mt_compressed.zxc");
        let output_path = temp_path("mt_output.bin");

        // Create larger test data (1 MB)
        let data: Vec<u8> = (0..1024 * 1024)
            .map(|i| ((i % 256) ^ ((i / 256) % 256)) as u8)
            .collect();

        {
            let mut f = fs::File::create(&input_path).unwrap();
            f.write_all(&data).unwrap();
        }

        // Test with different thread counts
        for threads in [1, 2, 4] {
            // Compress
            compress_file(&input_path, &compressed_path, Level::Default, Some(threads), None).unwrap();

            // Decompress
            let size = decompress_file(&compressed_path, &output_path, Some(threads)).unwrap();
            assert_eq!(size, data.len() as u64);

            // Verify
            let result = fs::read(&output_path).unwrap();
            assert_eq!(result, data, "Mismatch with {} threads", threads);
        }

        // Cleanup
        let _ = fs::remove_file(&input_path);
        let _ = fs::remove_file(&compressed_path);
        let _ = fs::remove_file(&output_path);
    }
}
