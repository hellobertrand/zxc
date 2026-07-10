/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! File-based multi-threaded streaming API.

use std::fs::File;
use std::io;
use std::path::Path;

#[cfg(unix)]
use std::os::unix::io::AsRawFd;

use crate::error::error_from_code;
use crate::{Error, Level};

/// Options for streaming compression operations.
#[derive(Debug, Clone)]
pub struct StreamCompressOptions {
    /// Compression level (default: `Level::Default`)
    pub level: Level,
    /// Number of worker threads (default: `None` = auto-detect CPU cores)
    pub threads: Option<usize>,
    /// Enable checksum for data integrity (default: `true`)
    pub checksum: bool,
    /// Enable seek table for random-access decompression (default: `false`)
    pub seekable: bool,
}

impl Default for StreamCompressOptions {
    fn default() -> Self {
        Self {
            level: Level::Default,
            threads: None,
            checksum: true,
            seekable: false,
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

    /// Enable seek table for random-access decompression.
    pub fn with_seekable(mut self) -> Self {
        self.seekable = true;
        self
    }
}

/// Options for streaming decompression operations.
#[derive(Debug, Clone)]
pub struct StreamDecompressOptions {
    /// Number of worker threads (default: `None` = auto-detect CPU cores)
    pub threads: Option<usize>,
    /// Verify checksum during decompression (default: `true`)
    pub verify_checksum: bool,
}

impl Default for StreamDecompressOptions {
    fn default() -> Self {
        Self {
            threads: None,
            verify_checksum: true,
        }
    }
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

/// Errors specific to the streaming file API.
#[derive(Debug, thiserror::Error)]
pub enum StreamError {
    /// I/O error during file operations
    #[error("I/O error: {0}")]
    Io(#[from] io::Error),

    /// Error from buffer operations
    #[error("buffer error: {0}")]
    BufferError(#[from] Error),

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
        unsafe {
            libc::close(dup_fd);
        }
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
        unsafe {
            libc::close(dup_fd);
        }
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

    let fd = unsafe { libc::open_osfhandle(dup_handle as libc::intptr_t, libc::O_RDONLY) };
    if fd < 0 {
        // open_osfhandle failed, close the duplicated handle to avoid leak
        unsafe {
            windows_sys::Win32::Foundation::CloseHandle(dup_handle);
        }
        return std::ptr::null_mut();
    }

    let file_ptr = unsafe { libc::fdopen(fd, c"rb".as_ptr()) };
    if file_ptr.is_null() {
        // fdopen failed, close the fd (which will close the handle)
        unsafe {
            libc::close(fd);
        }
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

    let fd = unsafe { libc::open_osfhandle(dup_handle as libc::intptr_t, libc::O_WRONLY) };
    if fd < 0 {
        // open_osfhandle failed, close the duplicated handle to avoid leak
        unsafe {
            windows_sys::Win32::Foundation::CloseHandle(dup_handle);
        }
        return std::ptr::null_mut();
    }

    let file_ptr = unsafe { libc::fdopen(fd, c"wb".as_ptr()) };
    if file_ptr.is_null() {
        // fdopen failed, close the fd (which will close the handle)
        unsafe {
            libc::close(fd);
        }
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
            &zxc_sys::zxc_compress_opts_t {
                n_threads,
                level: level as i32,
                checksum_enabled,
                ..Default::default()
            },
        );

        // Always close C FILE handles (they own duplicated fds)
        libc::fclose(c_in);
        libc::fclose(c_out);

        if result < 0 {
            Err(StreamError::BufferError(error_from_code(result)))
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
            &zxc_sys::zxc_decompress_opts_t {
                n_threads,
                checksum_enabled,
                ..Default::default()
            },
        );

        // Always close C FILE handles (they own duplicated fds)
        libc::fclose(c_in);
        libc::fclose(c_out);

        if result < 0 {
            Err(StreamError::BufferError(error_from_code(result)))
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

        // The FILE* owns a dup'd fd; it must be closed here or every call
        // leaks one file descriptor.
        libc::fclose(c_file);

        if result < 0 {
            Err(StreamError::InvalidFile)
        } else {
            Ok(result as u64)
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::*;
    use std::fs;
    use std::io::Write;

    fn temp_path(name: &str) -> String {
        let dir_name = format!("zxc_test_{}", std::process::id());

        // Try the system temp directory first
        let mut path = std::env::temp_dir();
        path.push(&dir_name);

        // If we can't create the directory in the system temp, fall back
        // to a subdirectory relative to the current working directory.
        // This happens on some Windows CI runners where TEMP is missing or
        // points to a path the process cannot access.
        if fs::create_dir_all(&path).is_err() {
            path = std::env::current_dir().expect("cannot determine current directory");
            path.push(&dir_name);
            fs::create_dir_all(&path).expect("failed to create temp directory in current dir");
        }

        path.push(name);
        path.to_string_lossy().into_owned()
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
        let compressed_size =
            compress_file(&input_path, &compressed_path, Level::Default, None, None).unwrap();
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
            compress_file(
                &input_path,
                &compressed_path,
                Level::Default,
                Some(threads),
                None,
            )
            .unwrap();

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
