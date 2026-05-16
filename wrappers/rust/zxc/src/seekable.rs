/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! Seekable API: random-access decompression (single-threaded).
//!
//! A seekable ZXC archive ships with a side seek table that maps
//! uncompressed offsets to the blocks containing them. This lets a
//! decoder fetch only the blocks overlapping a requested byte range,
//! which is much cheaper than decompressing the whole archive.
//!
//! Build a seekable archive by setting [`CompressOptions::seekable`]
//! to `true` before passing the options to the streaming or file APIs.
//!
//! # Example
//!
//! ```rust,ignore
//! use zxc::seekable::Seekable;
//!
//! let mut s = Seekable::open("archive.zxc")?;
//! let total = s.decompressed_size();
//! let mut buf = vec![0u8; 1024];
//! let n = s.decompress_range(&mut buf, 0, 1024)?;
//! assert_eq!(n, 1024);
//! ```
//!
//! Multi-threaded range decompression is intentionally not exposed
//! by this crate yet; the underlying `zxc_seekable_decompress_range_mt`
//! symbol is reserved for a future addition.

use std::ffi::{c_void, CString};
use std::path::Path;
use std::ptr::NonNull;

use crate::error::error_from_code;
use crate::{Error, Result};

/// Handle to a seekable ZXC archive.
///
/// Created with [`Seekable::open`] (file-backed) or
/// [`Seekable::from_bytes`] (in-memory). The handle automatically
/// releases its underlying C resources on drop.
pub struct Seekable {
    inner: NonNull<zxc_sys::zxc_seekable>,
    /// When opened via `open`, we own the `FILE*` and must `fclose` it
    /// after the handle is freed.
    file: Option<*mut libc::FILE>,
    /// When opened via `from_bytes`, we own the source buffer for the
    /// lifetime of the handle.
    _buf: Option<Vec<u8>>,
}

// SAFETY: the underlying handle is opaque and the library guarantees
// per-handle thread affinity. `Send` is safe (move the handle to another
// thread is fine); `Sync` is not (no concurrent calls on the same handle).
unsafe impl Send for Seekable {}

impl Seekable {
    /// Opens a seekable archive from an owned in-memory buffer.
    ///
    /// The buffer is held alive for the lifetime of the returned handle.
    /// Use this when the archive is already in memory.
    pub fn from_bytes(data: Vec<u8>) -> Result<Self> {
        let ptr = unsafe {
            zxc_sys::zxc_seekable_open(data.as_ptr() as *const c_void, data.len())
        };
        let inner = NonNull::new(ptr).ok_or(Error::InvalidData)?;
        Ok(Self {
            inner,
            file: None,
            _buf: Some(data),
        })
    }

    /// Opens a seekable archive from a file path.
    ///
    /// The file is opened in binary read mode and remains open for the
    /// lifetime of the returned handle.
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self> {
        let path_bytes = path_to_cstring(path.as_ref())?;
        let mode = CString::new("rb").map_err(|_| Error::Io)?;
        // SAFETY: both pointers point to valid NUL-terminated strings.
        let f = unsafe { libc::fopen(path_bytes.as_ptr(), mode.as_ptr()) };
        if f.is_null() {
            return Err(Error::Io);
        }
        let ptr = unsafe { zxc_sys::zxc_seekable_open_file(f) };
        let inner = match NonNull::new(ptr) {
            Some(p) => p,
            None => {
                // SAFETY: f was just returned by fopen and not yet freed.
                unsafe { libc::fclose(f) };
                return Err(Error::InvalidData);
            }
        };
        Ok(Self {
            inner,
            file: Some(f),
            _buf: None,
        })
    }

    /// Total number of data blocks (excludes the EOF marker block).
    pub fn num_blocks(&self) -> u32 {
        unsafe { zxc_sys::zxc_seekable_get_num_blocks(self.inner.as_ptr()) }
    }

    /// Total decompressed size of the archive in bytes.
    pub fn decompressed_size(&self) -> u64 {
        unsafe { zxc_sys::zxc_seekable_get_decompressed_size(self.inner.as_ptr()) }
    }

    /// On-disk compressed size of a specific block (block header +
    /// payload + optional per-block checksum).
    ///
    /// Returns `None` if `block_idx` is out of range.
    pub fn block_compressed_size(&self, block_idx: u32) -> Option<u32> {
        if block_idx >= self.num_blocks() {
            return None;
        }
        let sz = unsafe {
            zxc_sys::zxc_seekable_get_block_comp_size(self.inner.as_ptr(), block_idx)
        };
        Some(sz)
    }

    /// Decompressed size of a specific block.
    ///
    /// Returns `None` if `block_idx` is out of range.
    pub fn block_decompressed_size(&self, block_idx: u32) -> Option<u32> {
        if block_idx >= self.num_blocks() {
            return None;
        }
        let sz = unsafe {
            zxc_sys::zxc_seekable_get_block_decomp_size(self.inner.as_ptr(), block_idx)
        };
        Some(sz)
    }

    /// Decompresses `len` bytes starting at `offset` (in the original
    /// uncompressed byte stream) into `dst`.
    ///
    /// Only the blocks overlapping the requested range are read and
    /// decompressed. Returns the number of bytes actually written.
    pub fn decompress_range(
        &mut self,
        dst: &mut [u8],
        offset: u64,
        len: usize,
    ) -> Result<usize> {
        let res = unsafe {
            zxc_sys::zxc_seekable_decompress_range(
                self.inner.as_ptr(),
                dst.as_mut_ptr() as *mut c_void,
                dst.len(),
                offset,
                len,
            )
        };
        if res < 0 {
            Err(error_from_code(res))
        } else {
            Ok(res as usize)
        }
    }
}

impl Drop for Seekable {
    fn drop(&mut self) {
        // SAFETY: inner was created by zxc_seekable_open / _open_file
        // and has not been freed yet.
        unsafe { zxc_sys::zxc_seekable_free(self.inner.as_ptr()) };
        if let Some(f) = self.file.take() {
            // SAFETY: f was returned by libc::fopen above.
            unsafe { libc::fclose(f) };
        }
    }
}

/// Encoded byte size of a seek table covering `num_blocks` data blocks.
///
/// Use this to size a destination buffer for [`write_seek_table`].
pub fn seek_table_size(num_blocks: u32) -> usize {
    unsafe { zxc_sys::zxc_seek_table_size(num_blocks) }
}

/// Low-level: writes a seek table (header + entries) into `dst`.
///
/// `comp_sizes` is the slice of per-block on-disk compressed sizes, in
/// order. Most callers do not need this directly - the streaming and
/// file APIs already emit a seek table when
/// [`CompressOptions::seekable`] is set.
pub fn write_seek_table(dst: &mut [u8], comp_sizes: &[u32]) -> Result<usize> {
    let res = unsafe {
        zxc_sys::zxc_write_seek_table(
            dst.as_mut_ptr(),
            dst.len(),
            comp_sizes.as_ptr(),
            comp_sizes.len() as u32,
        )
    };
    if res < 0 {
        Err(error_from_code(res))
    } else {
        Ok(res as usize)
    }
}

#[cfg(unix)]
fn path_to_cstring(path: &Path) -> Result<CString> {
    use std::os::unix::ffi::OsStrExt;
    CString::new(path.as_os_str().as_bytes()).map_err(|_| Error::Io)
}

#[cfg(windows)]
fn path_to_cstring(path: &Path) -> Result<CString> {
    // libc::fopen on Windows expects an ANSI path. Round-trip through
    // the lossy UTF-8 representation - paths with characters outside
    // the active code page are not supported by this convenience entry
    // point; use `from_bytes` after reading the file yourself if you
    // need full Unicode path support.
    let s = path.to_str().ok_or(Error::Io)?;
    CString::new(s).map_err(|_| Error::Io)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{compress_with_options, CompressOptions, Level};

    fn build_archive(data: &[u8]) -> Vec<u8> {
        let opts = CompressOptions {
            level: Level::Default,
            checksum: true,
            seekable: true,
        };
        compress_with_options(data, &opts).expect("compression failed")
    }

    #[test]
    fn from_bytes_roundtrip() {
        let payload: Vec<u8> = (0..32_768).map(|i| (i as u8).wrapping_mul(31)).collect();
        let compressed = build_archive(&payload);

        let mut s = Seekable::from_bytes(compressed).expect("open failed");
        assert_eq!(s.decompressed_size(), payload.len() as u64);
        assert!(s.num_blocks() >= 1);

        // Block accessors must round-trip on the first block.
        assert!(s.block_compressed_size(0).unwrap() > 0);
        assert!(s.block_decompressed_size(0).unwrap() > 0);
        assert!(s.block_compressed_size(s.num_blocks()).is_none());

        // Full-range decompression.
        let mut out = vec![0u8; payload.len()];
        let n = s
            .decompress_range(&mut out, 0, payload.len())
            .expect("decompress_range failed");
        assert_eq!(n, payload.len());
        assert_eq!(out, payload);
    }

    #[test]
    fn partial_range() {
        let payload: Vec<u8> = (0..16_384).map(|i| (i as u8) ^ 0xA5).collect();
        let compressed = build_archive(&payload);

        let mut s = Seekable::from_bytes(compressed).expect("open failed");
        let start = 1024usize;
        let len = 4096usize;
        let mut out = vec![0u8; len];
        let n = s
            .decompress_range(&mut out, start as u64, len)
            .expect("decompress_range failed");
        assert_eq!(n, len);
        assert_eq!(out, payload[start..start + len]);
    }

    #[test]
    fn invalid_buffer_fails() {
        let garbage = vec![0u8; 32];
        assert!(Seekable::from_bytes(garbage).is_err());
    }

    #[test]
    fn seek_table_size_matches_write() {
        let comp_sizes = [128u32, 256, 200, 4];
        let expected = seek_table_size(comp_sizes.len() as u32);
        let mut buf = vec![0u8; expected];
        let written = write_seek_table(&mut buf, &comp_sizes).expect("write failed");
        assert_eq!(written, expected);
    }
}
