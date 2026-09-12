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
    dict: Option<Vec<u8>>,
    dict_huf: Option<Vec<u8>>,
}

// SAFETY: the underlying handle is opaque and the library states contexts
// must not be shared between threads; `Send` is safe, `Sync` is not.
unsafe impl Send for Cctx {}

impl Cctx {
    /// Creates a new compression context.
    ///
    /// When `opts` is `Some`, internal buffers are pre-allocated for its level
    /// and block size; `None` defers allocation. A dictionary given here is
    /// validated and then applies to every call that omits one, as in the Go,
    /// Python and Node bindings.
    pub fn new(opts: Option<&CompressOptions>) -> Result<Self> {
        if let Some(o) = opts {
            crate::dict_parts(o.dict.as_deref(), o.dict_huf.as_deref())?;
        }
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
            Ok(Self {
                inner: ptr,
                dict: opts.and_then(|o| o.dict.clone()),
                dict_huf: opts.and_then(|o| o.dict_huf.clone()),
            })
        }
    }

    /// Compresses a single block (no file framing).
    ///
    /// Output format: 8-byte block header + payload (+ optional 4-byte checksum).
    /// Use [`compress_block_bound`] to size `dst`. A block carries no dictionary
    /// id: give the decoder the same dictionary options.
    pub fn compress_block(
        &mut self,
        src: &[u8],
        dst: &mut [u8],
        opts: &CompressOptions,
    ) -> Result<usize> {
        let (dict, dict_huf) = crate::dict_parts(
            opts.dict.as_deref().or(self.dict.as_deref()),
            opts.dict_huf.as_deref().or(self.dict_huf.as_deref()),
        )?;
        let copts = zxc_sys::zxc_compress_opts_t {
            level: opts.level as i32,
            checksum_enabled: opts.checksum as i32,
            seekable: opts.seekable as i32,
            dict: crate::dict_ptr(dict),
            dict_size: dict.len(),
            dict_huf: crate::dict_ptr(dict_huf),
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
    /// `dst` should be at least [`decompress_block_bound`]`(uncompressed_size)`
    /// to enable the fast path. For strictly-sized
    /// destinations, use [`Dctx::decompress_block_safe`]. A block carries no
    /// dictionary id: pass the same dictionary options as at compression.
    pub fn decompress_block(
        &mut self,
        src: &[u8],
        dst: &mut [u8],
        opts: &DecompressOptions,
    ) -> Result<usize> {
        let (dict, dict_huf) = crate::dict_parts(opts.dict.as_deref(), opts.dict_huf.as_deref())?;
        let dopts = zxc_sys::zxc_decompress_opts_t {
            checksum_enabled: opts.verify_checksum as i32,
            dict: crate::dict_ptr(dict),
            dict_size: dict.len(),
            dict_huf: crate::dict_ptr(dict_huf),
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
    /// Slightly slower than the fast path; output is bit-identical. Same
    /// dictionary options as [`Dctx::decompress_block`]; a dictionary decodes
    /// through the bounce path, so `dst` may hold more than the strict tail.
    pub fn decompress_block_safe(
        &mut self,
        src: &[u8],
        dst: &mut [u8],
        opts: &DecompressOptions,
    ) -> Result<usize> {
        let (dict, dict_huf) = crate::dict_parts(opts.dict.as_deref(), opts.dict_huf.as_deref())?;
        let dopts = zxc_sys::zxc_decompress_opts_t {
            checksum_enabled: opts.verify_checksum as i32,
            dict: crate::dict_ptr(dict),
            dict_size: dict.len(),
            dict_huf: crate::dict_ptr(dict_huf),
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::dict::{train_dict, train_dict_huf};
    use crate::{Error, Level};

    fn corpus() -> Vec<Vec<u8>> {
        (0..48)
            .map(|i| {
                format!(
                    "{{\"id\":{i},\"user\":\"alice_{i}\",\"mail\":\"alice{i}@example.com\",\"role\":\"member\",\"active\":true}}"
                )
                .into_bytes()
            })
            .collect()
    }

    fn trained() -> (Vec<Vec<u8>>, Vec<u8>, Vec<u8>) {
        let corpus = corpus();
        let samples: Vec<&[u8]> = corpus.iter().map(|s| s.as_slice()).collect();
        let dict = train_dict(&samples, 4096).expect("train_dict");
        let huf = train_dict_huf(&samples, &dict)
            .expect("train_dict_huf")
            .to_vec();
        (corpus, dict, huf)
    }

    #[test]
    fn creation_time_dictionary_applies_to_later_calls() {
        let (corpus, dict, huf) = trained();
        let block = &corpus[7];

        let create = CompressOptions::with_level(Level::Default)
            .with_dict(dict.clone())
            .with_dict_huf(huf.clone());
        let mut cctx = Cctx::new(Some(&create)).expect("Cctx::new");

        let mut inherited = vec![0u8; compress_block_bound(block.len()) as usize];
        let n_inherited = cctx
            .compress_block(block, &mut inherited, &CompressOptions::default())
            .expect("compress_block with inherited dict");

        // The same block through a dictionary-less context, for contrast.
        let mut plain_ctx = Cctx::new(None).expect("Cctx::new");
        let mut plain = vec![0u8; compress_block_bound(block.len()) as usize];
        let n_plain = plain_ctx
            .compress_block(block, &mut plain, &CompressOptions::default())
            .expect("compress_block without dict");

        assert_ne!(
            inherited[..n_inherited],
            plain[..n_plain],
            "the creation-time dictionary was dropped"
        );

        // And it must decode with that dictionary.
        let dopts = DecompressOptions::default()
            .with_dict(dict.clone())
            .with_dict_huf(huf.clone());
        let mut dctx = Dctx::new().expect("Dctx::new");
        let mut out = vec![0u8; block.len() + 64];
        let n = dctx
            .decompress_block(&inherited[..n_inherited], &mut out, &dopts)
            .expect("decompress_block");
        assert_eq!(&out[..n], &block[..]);
    }

    #[test]
    fn block_roundtrip_with_dict_and_table() {
        let (corpus, dict, huf) = trained();
        let block = &corpus[7];
        let mut cctx = Cctx::new(None).unwrap();
        let mut dctx = Dctx::new().unwrap();
        let mut comp = vec![0u8; compress_block_bound(block.len()) as usize];
        let mut out = vec![0u8; decompress_block_bound(block.len()) as usize];
        let mut exact = vec![0u8; block.len()];

        for (level, table) in [(Level::Default, false), (Level::Ultra, true)] {
            let mut copts = CompressOptions::with_level(level).with_dict(dict.clone());
            let mut dopts = DecompressOptions::default().with_dict(dict.clone());
            if table {
                copts = copts.with_dict_huf(huf.clone());
                dopts = dopts.with_dict_huf(huf.clone());
            }
            let n = cctx
                .compress_block(block, &mut comp, &copts)
                .expect("compress_block");
            let m = dctx
                .decompress_block(&comp[..n], &mut out, &dopts)
                .expect("decompress_block");
            assert_eq!(&out[..m], block.as_slice());
            let k = dctx
                .decompress_block_safe(&comp[..n], &mut exact, &dopts)
                .expect("decompress_block_safe");
            assert_eq!(&exact[..k], block.as_slice());
            // Undecodable without the dictionary.
            assert!(
                dctx.decompress_block(&comp[..n], &mut out, &DecompressOptions::default())
                    .is_err()
            );
        }
    }

    #[test]
    fn wrong_table_length_is_rejected_before_reaching_c() {
        let corpus = corpus();
        let samples: Vec<&[u8]> = corpus.iter().map(|s| s.as_slice()).collect();
        let dict = train_dict(&samples, 4096).expect("train_dict");
        let block = &corpus[3];
        let mut cctx = Cctx::new(None).unwrap();
        let mut comp = vec![0u8; compress_block_bound(block.len()) as usize];
        let copts = CompressOptions::default()
            .with_dict(dict.clone())
            .with_dict_huf(vec![0u8; 7]);
        assert!(matches!(
            cctx.compress_block(block, &mut comp, &copts),
            Err(Error::BadHufTable)
        ));
        assert!(matches!(
            crate::compress_with_options(block, &copts),
            Err(Error::BadHufTable)
        ));
        // A wrong length is an error even with no dictionary to attach it to.
        let no_dict = CompressOptions::default().with_dict_huf(vec![0u8; 7]);
        assert!(matches!(
            crate::compress_with_options(block, &no_dict),
            Err(Error::BadHufTable)
        ));
        assert!(matches!(Cctx::new(Some(&no_dict)), Err(Error::BadHufTable)));
    }
}
