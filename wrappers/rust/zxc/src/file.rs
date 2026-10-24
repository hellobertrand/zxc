// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

//! File-based multi-threaded streaming API.

use std::fs::{self, File, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};

#[cfg(unix)]
use std::os::unix::io::AsRawFd;

use crate::ctx::{RawCctx, RawDctx, thread_param};
use crate::error::error_from_code;
use crate::{Error, Level};

/// Destination of a file pipeline. A regular or new output is written to a
/// sibling temporary file renamed over it on success, so a failed call leaves
/// the previous content intact. Any other output (device, FIFO) is written in
/// place.
struct Output {
    /// Taken on commit, so the file is closed before the rename.
    file: Option<File>,
    /// Temporary path and its rename target; `None` once in place.
    pending: Option<(PathBuf, PathBuf)>,
}

static TMP_SEQ: AtomicU32 = AtomicU32::new(0);

impl Output {
    fn create(path: &Path) -> io::Result<Self> {
        // Replace a link's target, not the link.
        let path = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
        // Opening the existing output fails where File::create would, tells a
        // device or FIFO apart, and gives the mode the replacement keeps.
        let mode = match OpenOptions::new().write(true).open(&path) {
            Ok(file) => {
                let meta = file.metadata()?;
                if !meta.is_file() {
                    return Ok(Self {
                        file: Some(file),
                        pending: None,
                    });
                }
                Some(meta.permissions())
            }
            Err(e) if e.kind() == io::ErrorKind::NotFound => None,
            Err(e) => return Err(e),
        };
        let (file, tmp) = loop {
            let mut tmp = path.clone().into_os_string();
            tmp.push(format!(
                ".{}-{}.zxc-tmp",
                std::process::id(),
                TMP_SEQ.fetch_add(1, Ordering::Relaxed)
            ));
            match OpenOptions::new().write(true).create_new(true).open(&tmp) {
                Ok(file) => break (file, PathBuf::from(tmp)),
                Err(e) if e.kind() == io::ErrorKind::AlreadyExists => continue,
                Err(e) => return Err(e),
            }
        };
        let out = Self {
            file: Some(file),
            pending: Some((tmp, path)),
        };
        if let Some(mode) = mode {
            out.file().set_permissions(mode)?; // drop removes the temporary file
        }
        Ok(out)
    }

    fn file(&self) -> &File {
        self.file.as_ref().expect("output is open")
    }

    /// Closes the output and, for a temporary file, renames it into place.
    fn commit(mut self) -> io::Result<()> {
        self.file = None;
        match self.pending.take() {
            Some((tmp, target)) => fs::rename(&tmp, target).inspect_err(|_| {
                let _ = fs::remove_file(&tmp);
            }),
            None => Ok(()),
        }
    }
}

impl Drop for Output {
    /// Removes the temporary file of an uncommitted output.
    fn drop(&mut self) {
        self.file = None;
        if let Some((tmp, _)) = self.pending.take() {
            let _ = fs::remove_file(tmp);
        }
    }
}

/// Options for streaming compression operations.
#[derive(Debug, Clone)]
pub struct StreamCompressOptions {
    /// Compression level (default: `Level::Default`)
    pub level: Level,
    /// Number of worker threads (default: `None` = auto-detect CPU cores).
    /// At most the library maximum (512); more is [`Error::BadParam`].
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
    /// Number of worker threads (default: `None` = auto-detect CPU cores).
    /// At most the library maximum (512); more is [`Error::BadParam`].
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

/// Closes the output FILE*, reporting a failure: close() can fail on its own
/// (e.g. deferred write errors on NFS), after the library already flushed.
unsafe fn close_output(f: *mut libc::FILE) -> io::Result<()> {
    if unsafe { libc::fclose(f) } == 0 {
        return Ok(());
    }
    Err(close_error())
}

#[cfg(unix)]
fn close_error() -> io::Error {
    let err = io::Error::last_os_error();
    if err.raw_os_error() == Some(0) {
        return io::Error::other("failed to close output file");
    }
    err
}

/// fclose() reports through the CRT errno, not GetLastError(), so
/// last_os_error() would return an unrelated Win32 value. CRT errno values are
/// not Win32 codes either, so they cannot go through from_raw_os_error().
#[cfg(windows)]
fn close_error() -> io::Error {
    unsafe extern "C" {
        fn _errno() -> *mut libc::c_int;
    }
    let code = unsafe { *_errno() };
    if code == 0 {
        return io::Error::other("failed to close output file");
    }
    let msg = unsafe { std::ffi::CStr::from_ptr(libc::strerror(code)) };
    io::Error::other(format!(
        "failed to close output file: {}",
        msg.to_string_lossy()
    ))
}

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
    compress_file_with_options(
        input,
        output,
        &StreamCompressOptions {
            level,
            threads,
            checksum: checksum.unwrap_or(false),
            seekable: false,
        },
    )
}

/// Compresses a file using multi-threaded streaming, honouring `opts`.
///
/// Unlike [`compress_file`], this exposes every [`StreamCompressOptions`]
/// field — including `seekable`, which appends a seek table for
/// random-access decompression via [`crate::seekable::Seekable`].
///
/// # Example
///
/// ```rust,no_run
/// use zxc::{compress_file_with_options, Level, StreamCompressOptions};
///
/// let opts = StreamCompressOptions::with_level(Level::Compact)
///     .threads(4)
///     .with_seekable();
/// let bytes = compress_file_with_options("input.bin", "output.zxc", &opts)?;
/// # Ok::<(), zxc::StreamError>(())
/// ```
pub fn compress_file_with_options<P: AsRef<Path>>(
    input: P,
    output: P,
    opts: &StreamCompressOptions,
) -> StreamResult<u64> {
    // The pipeline reads its parameters from a configured context; validate
    // them before touching the files.
    let mut cfg = RawCctx::new()?;
    cfg.set_params(
        opts.level,
        opts.checksum,
        opts.seekable,
        thread_param(opts.threads),
    )?;

    let f_in = File::open(input)?;
    let f_out = Output::create(output.as_ref())?;

    unsafe {
        let c_in = file_to_c_file_read(&f_in);
        let c_out = file_to_c_file_write(f_out.file());

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

        let result = zxc_sys::zxc_stream_compress(c_in, c_out, cfg.as_ptr());

        // Always close C FILE handles (they own duplicated fds)
        let closed = close_output(c_out);
        libc::fclose(c_in);

        if result < 0 {
            return Err(StreamError::BufferError(error_from_code(result)));
        }
        closed?;
        f_out.commit()?;
        Ok(result as u64)
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
    decompress_file_with_options(
        input,
        output,
        &StreamDecompressOptions {
            threads,
            verify_checksum: true,
        },
    )
}

/// Decompresses a file using multi-threaded streaming, honouring `opts`.
///
/// Unlike [`decompress_file`], this exposes every
/// [`StreamDecompressOptions`] field — including `verify_checksum`, which
/// can be disabled to skip integrity verification.
///
/// # Example
///
/// ```rust,no_run
/// use zxc::{decompress_file_with_options, StreamDecompressOptions};
///
/// let opts = StreamDecompressOptions::default().skip_checksum();
/// let bytes = decompress_file_with_options("compressed.zxc", "output.bin", &opts)?;
/// # Ok::<(), zxc::StreamError>(())
/// ```
pub fn decompress_file_with_options<P: AsRef<Path>>(
    input: P,
    output: P,
    opts: &StreamDecompressOptions,
) -> StreamResult<u64> {
    let mut cfg = RawDctx::new()?;
    cfg.set_params(opts.verify_checksum, thread_param(opts.threads))?;

    let f_in = File::open(input)?;
    let f_out = Output::create(output.as_ref())?;

    unsafe {
        let c_in = file_to_c_file_read(&f_in);
        let c_out = file_to_c_file_write(f_out.file());

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

        let result = zxc_sys::zxc_stream_decompress(c_in, c_out, cfg.as_ptr());

        // Always close C FILE handles (they own duplicated fds)
        let closed = close_output(c_out);
        libc::fclose(c_in);

        if result < 0 {
            return Err(StreamError::BufferError(error_from_code(result)));
        }
        closed?;
        f_out.commit()?;
        Ok(result as u64)
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

    /// A failed call, refused option or bad data, leaves an existing output
    /// untouched and no temporary file behind; a successful one replaces it.
    #[test]
    fn test_file_failure_keeps_output() {
        let dir = std::path::PathBuf::from(temp_path("failure_keeps_output"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let (input, arc, corrupt, out) = (
            dir.join("in"),
            dir.join("arc"),
            dir.join("corrupt"),
            dir.join("out"),
        );
        let payload = b"file pipeline input ".repeat(500);
        fs::write(&input, &payload).unwrap();
        compress_file(&input, &arc, Level::Default, None, Some(true)).unwrap();
        let mut bad = fs::read(&arc).unwrap();
        let mid = bad.len() / 2;
        bad[mid] ^= 0xFF;
        fs::write(&corrupt, &bad).unwrap();
        let keep = b"previous content must survive";
        fs::write(&out, keep).unwrap();

        assert!(compress_file(&input, &out, Level::Default, Some(1000), None).is_err());
        assert!(decompress_file(&corrupt, &out, None).is_err());
        assert_eq!(fs::read(&out).unwrap(), keep);
        assert_eq!(
            fs::read_dir(&dir).unwrap().count(),
            4,
            "temporary file left behind"
        );

        decompress_file(&arc, &out, None).unwrap();
        assert_eq!(fs::read(&out).unwrap(), payload);
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&out, fs::Permissions::from_mode(0o600)).unwrap();
            decompress_file(&arc, &out, None).unwrap();
            let mode = fs::metadata(&out).unwrap().permissions().mode();
            assert_eq!(mode & 0o777, 0o600, "output mode not kept");
            decompress_file(&arc, &std::path::PathBuf::from("/dev/null"), None).unwrap();
        }
        let _ = fs::remove_dir_all(&dir);
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

    // A small output stays in the stdio buffer until the final flush, the only
    // write that reaches /dev/full (ENOSPC): it must still surface as an error.
    #[cfg(target_os = "linux")]
    #[test]
    fn test_file_write_failure_at_flush() {
        let input_path = temp_path("flush_input.bin");
        let compressed_path = temp_path("flush_compressed.zxc");

        let data: Vec<u8> = (0..1000).map(|i| (i % 7) as u8).collect();
        fs::write(&input_path, &data).unwrap();
        compress_file(&input_path, &compressed_path, Level::Default, None, None).unwrap();

        let r = compress_file(input_path.as_str(), "/dev/full", Level::Default, None, None);
        assert!(r.is_err(), "compress to /dev/full reported {r:?}");
        let r = decompress_file(compressed_path.as_str(), "/dev/full", None);
        assert!(r.is_err(), "decompress to /dev/full reported {r:?}");

        let _ = fs::remove_file(&input_path);
        let _ = fs::remove_file(&compressed_path);
    }
}
