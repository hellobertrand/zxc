/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! Block API: reusable single-block compression / decompression contexts.

use std::ffi::c_void;

use crate::error::error_from_code;
use crate::{CompressOptions, DecompressOptions, Error, Result};

/// Reusable compression context for the Block API.
///
/// Eliminates per-call allocation overhead when compressing many blocks.
/// Internally wraps an opaque `zxc_cctx*` freed automatically on drop.
///
/// # Example
///
/// ```rust,ignore
/// use zxc::{Cctx, CompressOptions, Level};
///
/// let mut cctx = Cctx::new(None)?;
/// let opts = CompressOptions::default().with_level(Level::Default);
/// let mut out = vec![0u8; zxc::compress_block_bound(block.len()) as usize];
/// let n = cctx.compress_block(block, &mut out, &opts)?;
/// ```
pub struct Cctx {
    inner: *mut zxc_sys::zxc_cctx,
}

// SAFETY: the underlying handle is opaque and the library states contexts
// must not be shared between threads; `Send` is safe, `Sync` is not.
unsafe impl Send for Cctx {}

impl Cctx {
    /// Creates a new compression context.
    ///
    /// When `opts` is `Some`, internal buffers are pre-allocated with those
    /// parameters. When `None`, allocation is deferred to first use.
    pub fn new(opts: Option<&CompressOptions>) -> Result<Self> {
        let c_opts = opts.map(|o| zxc_sys::zxc_compress_opts_t {
            level: o.level as i32,
            checksum_enabled: o.checksum as i32,
            seekable: o.seekable as i32,
            ..Default::default()
        });
        let ptr = unsafe {
            zxc_sys::zxc_create_cctx(
                c_opts
                    .as_ref()
                    .map(|o| o as *const _)
                    .unwrap_or(std::ptr::null()),
            )
        };
        if ptr.is_null() {
            Err(Error::Memory)
        } else {
            Ok(Self { inner: ptr })
        }
    }

    /// Compresses a single block (no file framing).
    ///
    /// Output format: 8-byte block header + payload (+ optional 4-byte checksum).
    /// Use [`compress_block_bound`] to size `dst`.
    pub fn compress_block(
        &mut self,
        src: &[u8],
        dst: &mut [u8],
        opts: &CompressOptions,
    ) -> Result<usize> {
        let copts = zxc_sys::zxc_compress_opts_t {
            level: opts.level as i32,
            checksum_enabled: opts.checksum as i32,
            seekable: opts.seekable as i32,
            ..Default::default()
        };
        let res = unsafe {
            zxc_sys::zxc_compress_block(
                self.inner,
                src.as_ptr() as *const c_void,
                src.len(),
                dst.as_mut_ptr() as *mut c_void,
                dst.len(),
                &copts,
            )
        };
        if res < 0 {
            Err(error_from_code(res))
        } else {
            Ok(res as usize)
        }
    }
}

impl Drop for Cctx {
    fn drop(&mut self) {
        unsafe { zxc_sys::zxc_free_cctx(self.inner) };
    }
}

/// Reusable decompression context for the Block API.
///
/// Internally wraps an opaque `zxc_dctx*` freed automatically on drop.
pub struct Dctx {
    inner: *mut zxc_sys::zxc_dctx,
}

unsafe impl Send for Dctx {}

impl Dctx {
    /// Creates a new decompression context.
    pub fn new() -> Result<Self> {
        let ptr = unsafe { zxc_sys::zxc_create_dctx() };
        if ptr.is_null() {
            Err(Error::Memory)
        } else {
            Ok(Self { inner: ptr })
        }
    }

    /// Decompresses a single block produced by [`Cctx::compress_block`].
    ///
    /// `dst` should be at least [`decompress_block_bound(uncompressed_size)`]
    /// (`decompress_block_bound`) to enable the fast path. For strictly-sized
    /// destinations, use [`Dctx::decompress_block_safe`].
    pub fn decompress_block(
        &mut self,
        src: &[u8],
        dst: &mut [u8],
        opts: &DecompressOptions,
    ) -> Result<usize> {
        let dopts = zxc_sys::zxc_decompress_opts_t {
            checksum_enabled: opts.verify_checksum as i32,
            ..Default::default()
        };
        let res = unsafe {
            zxc_sys::zxc_decompress_block(
                self.inner,
                src.as_ptr() as *const c_void,
                src.len(),
                dst.as_mut_ptr() as *mut c_void,
                dst.len(),
                &dopts,
            )
        };
        if res < 0 {
            Err(error_from_code(res))
        } else {
            Ok(res as usize)
        }
    }

    /// Strict-sized variant of [`Dctx::decompress_block`]: accepts
    /// `dst.len() == uncompressed_size` exactly (no tail pad required).
    /// Slightly slower than the fast path; output is bit-identical.
    pub fn decompress_block_safe(
        &mut self,
        src: &[u8],
        dst: &mut [u8],
        opts: &DecompressOptions,
    ) -> Result<usize> {
        let dopts = zxc_sys::zxc_decompress_opts_t {
            checksum_enabled: opts.verify_checksum as i32,
            ..Default::default()
        };
        let res = unsafe {
            zxc_sys::zxc_decompress_block_safe(
                self.inner,
                src.as_ptr() as *const c_void,
                src.len(),
                dst.as_mut_ptr() as *mut c_void,
                dst.len(),
                &dopts,
            )
        };
        if res < 0 {
            Err(error_from_code(res))
        } else {
            Ok(res as usize)
        }
    }
}

impl Drop for Dctx {
    fn drop(&mut self) {
        unsafe { zxc_sys::zxc_free_dctx(self.inner) };
    }
}

/// Returns the maximum compressed size for a single block of `input_size`
/// bytes (no file framing).
pub fn compress_block_bound(input_size: usize) -> u64 {
    unsafe { zxc_sys::zxc_compress_block_bound(input_size) }
}

/// Returns the minimum destination buffer size required by
/// [`Dctx::decompress_block`] for a block of `uncompressed_size` bytes.
///
/// Accounts for the wild-copy tail pad used by the fast decoder. For a
/// strictly-sized destination, use [`Dctx::decompress_block_safe`] instead
/// and size the buffer to exactly the uncompressed length.
pub fn decompress_block_bound(uncompressed_size: usize) -> u64 {
    unsafe { zxc_sys::zxc_decompress_block_bound(uncompressed_size) }
}
