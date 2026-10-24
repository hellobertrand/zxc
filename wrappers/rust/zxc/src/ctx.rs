// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

//! Block API: reusable single-block compression / decompression contexts,
//! and the crate-private context handles every other API configures from
//! the option structs.

use std::ffi::{c_int, c_void};

use crate::error::error_from_code;
use crate::{CompressOptions, DecompressOptions, Error, Level, Result};

/// Maps a setter's return code to a `Result`.
fn check(rc: c_int) -> Result<()> {
    if rc < 0 {
        Err(error_from_code(rc as i64))
    } else {
        Ok(())
    }
}

/// Thread count for the `FILE*` pipeline: `None` is 0, auto-detect; a value
/// above `i64` is left to the library to refuse ([`Error::BadParam`]).
pub(crate) fn thread_param(threads: Option<usize>) -> i64 {
    threads.map_or(0, |n| i64::try_from(n).unwrap_or(i64::MAX))
}

/// Owned heap `zxc_cctx*`, freed on drop. The C context carries every
/// parameter: the option structs are translated into setter calls here, so
/// the one-shot, block, `FILE*` and push APIs share one translation.
pub(crate) struct RawCctx(*mut zxc_sys::zxc_cctx);

impl RawCctx {
    /// Creates a context with every parameter at its default.
    pub(crate) fn new() -> Result<Self> {
        let ptr = unsafe { zxc_sys::zxc_create_cctx() };
        if ptr.is_null() {
            Err(Error::Memory)
        } else {
            Ok(Self(ptr))
        }
    }

    pub(crate) fn as_ptr(&self) -> *mut zxc_sys::zxc_cctx {
        self.0
    }

    fn set_param(&mut self, param: zxc_sys::zxc_cparam_t, value: i64) -> Result<()> {
        check(unsafe { zxc_sys::zxc_cctx_set_param(self.0, param, value) })
    }

    /// Sets level, checksum, seek table and thread count. `threads` is read
    /// by the `FILE*` pipeline only; the other APIs ignore it.
    pub(crate) fn set_params(
        &mut self,
        level: Level,
        checksum: bool,
        seekable: bool,
        threads: i64,
    ) -> Result<()> {
        self.set_param(zxc_sys::ZXC_C_LEVEL, level as i64)?;
        self.set_param(zxc_sys::ZXC_C_CHECKSUM, checksum as i64)?;
        self.set_param(zxc_sys::ZXC_C_SEEKABLE, seekable as i64)?;
        self.set_param(zxc_sys::ZXC_C_THREADS, threads)
    }

    /// Attaches the dictionary slices from [`crate::dict_parts`] (empty
    /// detaches). The context keeps the pointers only: the slices must stay
    /// alive, unmoved, until the next `set_dict` or the last call using them.
    pub(crate) fn set_dict(&mut self, dict: &[u8], dict_huf: &[u8]) -> Result<()> {
        check(unsafe {
            zxc_sys::zxc_cctx_set_dict(
                self.0,
                crate::dict_ptr(dict),
                dict.len(),
                crate::dict_ptr(dict_huf),
            )
        })
    }

    /// Applies a whole [`CompressOptions`], dictionary included, plus the
    /// thread count the struct does not carry. The dictionary slices are
    /// borrowed from `opts`, so the caller keeps `opts` alive for the call.
    pub(crate) fn configure(&mut self, opts: &CompressOptions, threads: i64) -> Result<()> {
        let (dict, dict_huf) = crate::dict_parts(opts.dict.as_deref(), opts.dict_huf.as_deref())?;
        self.set_params(opts.level, opts.checksum, opts.seekable, threads)?;
        self.set_dict(dict, dict_huf)
    }
}

impl Drop for RawCctx {
    fn drop(&mut self) {
        unsafe { zxc_sys::zxc_free_cctx(self.0) };
    }
}

/// Owned heap `zxc_dctx*`, freed on drop; see [`RawCctx`].
pub(crate) struct RawDctx(*mut zxc_sys::zxc_dctx);

impl RawDctx {
    /// Creates a context with every parameter at its default.
    pub(crate) fn new() -> Result<Self> {
        let ptr = unsafe { zxc_sys::zxc_create_dctx() };
        if ptr.is_null() {
            Err(Error::Memory)
        } else {
            Ok(Self(ptr))
        }
    }

    pub(crate) fn as_ptr(&self) -> *mut zxc_sys::zxc_dctx {
        self.0
    }

    fn set_param(&mut self, param: zxc_sys::zxc_dparam_t, value: i64) -> Result<()> {
        check(unsafe { zxc_sys::zxc_dctx_set_param(self.0, param, value) })
    }

    /// Sets checksum verification and the thread count (`FILE*` pipeline only).
    pub(crate) fn set_params(&mut self, verify_checksum: bool, threads: i64) -> Result<()> {
        self.set_param(zxc_sys::ZXC_D_CHECKSUM, verify_checksum as i64)?;
        self.set_param(zxc_sys::ZXC_D_THREADS, threads)
    }

    /// Attaches the dictionary slices (empty detaches); same lifetime rule as
    /// [`RawCctx::set_dict`].
    pub(crate) fn set_dict(&mut self, dict: &[u8], dict_huf: &[u8]) -> Result<()> {
        check(unsafe {
            zxc_sys::zxc_dctx_set_dict(
                self.0,
                crate::dict_ptr(dict),
                dict.len(),
                crate::dict_ptr(dict_huf),
            )
        })
    }

    /// Applies a whole [`DecompressOptions`], dictionary included; the caller
    /// keeps `opts` alive for the call.
    pub(crate) fn configure(&mut self, opts: &DecompressOptions, threads: i64) -> Result<()> {
        let (dict, dict_huf) = crate::dict_parts(opts.dict.as_deref(), opts.dict_huf.as_deref())?;
        self.set_params(opts.verify_checksum, threads)?;
        self.set_dict(dict, dict_huf)
    }
}

impl Drop for RawDctx {
    fn drop(&mut self) {
        unsafe { zxc_sys::zxc_free_dctx(self.0) };
    }
}

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
/// let mut out = vec![0u8; zxc::compress_block_bound(block.len())];
/// let n = cctx.compress_block(block, &mut out, &opts)?;
/// ```
pub struct Cctx {
    inner: RawCctx,
    dict: Option<Vec<u8>>,
    dict_huf: Option<Vec<u8>>,
}

// SAFETY: the underlying handle is opaque and the library states contexts
// must not be shared between threads; `Send` is safe, `Sync` is not.
unsafe impl Send for Cctx {}

impl Cctx {
    /// Creates a new compression context.
    ///
    /// When `opts` is `Some`, its level, checksum and seek-table settings are
    /// set on the context (the working buffers are sized on the first call);
    /// `None` keeps the library defaults. A dictionary given here is validated
    /// and then applies to every call that omits one, as in the Go, Python and
    /// Node bindings. Each [`Cctx::compress_block`] call sets its own options
    /// on top.
    pub fn new(opts: Option<&CompressOptions>) -> Result<Self> {
        let mut inner = RawCctx::new()?;
        if let Some(o) = opts {
            crate::dict_parts(o.dict.as_deref(), o.dict_huf.as_deref())?;
            inner.set_params(o.level, o.checksum, o.seekable, 0)?;
        }
        Ok(Self {
            inner,
            dict: opts.and_then(|o| o.dict.clone()),
            dict_huf: opts.and_then(|o| o.dict_huf.clone()),
        })
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
        self.inner
            .set_params(opts.level, opts.checksum, opts.seekable, 0)?;
        self.inner.set_dict(dict, dict_huf)?;
        let res = unsafe {
            zxc_sys::zxc_compress_block(
                self.inner.as_ptr(),
                src.as_ptr() as *const c_void,
                src.len(),
                dst.as_mut_ptr() as *mut c_void,
                dst.len(),
            )
        };
        // The slices may belong to `opts`, gone after this call: detach them
        // so the context never holds a pointer past its owner.
        self.inner.set_dict(&[], &[])?;
        if res < 0 {
            Err(error_from_code(res))
        } else {
            Ok(res as usize)
        }
    }
}

/// Reusable decompression context for the Block API.
///
/// Internally wraps an opaque `zxc_dctx*` freed automatically on drop.
pub struct Dctx {
    inner: RawDctx,
}

unsafe impl Send for Dctx {}

impl Dctx {
    /// Creates a new decompression context.
    pub fn new() -> Result<Self> {
        Ok(Self {
            inner: RawDctx::new()?,
        })
    }

    /// Sets `opts` on the context, runs `call` on the raw handle and detaches
    /// the borrowed dictionary afterwards.
    fn with_options(
        &mut self,
        opts: &DecompressOptions,
        call: impl FnOnce(*mut zxc_sys::zxc_dctx) -> i64,
    ) -> Result<usize> {
        self.inner.configure(opts, 0)?;
        let res = call(self.inner.as_ptr());
        self.inner.set_dict(&[], &[])?;
        if res < 0 {
            Err(error_from_code(res))
        } else {
            Ok(res as usize)
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
        self.with_options(opts, |dctx| unsafe {
            zxc_sys::zxc_decompress_block(
                dctx,
                src.as_ptr() as *const c_void,
                src.len(),
                dst.as_mut_ptr() as *mut c_void,
                dst.len(),
            )
        })
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
        self.with_options(opts, |dctx| unsafe {
            zxc_sys::zxc_decompress_block_safe(
                dctx,
                src.as_ptr() as *const c_void,
                src.len(),
                dst.as_mut_ptr() as *mut c_void,
                dst.len(),
            )
        })
    }
}

/// Returns the maximum compressed size for a single block of `input_size`
/// bytes (no file framing).
pub fn compress_block_bound(input_size: usize) -> usize {
    unsafe { zxc_sys::zxc_compress_block_bound(input_size) }
}

/// Returns the minimum destination buffer size required by
/// [`Dctx::decompress_block`] for a block of `uncompressed_size` bytes.
///
/// Accounts for the wild-copy tail pad used by the fast decoder. For a
/// strictly-sized destination, use [`Dctx::decompress_block_safe`] instead
/// and size the buffer to exactly the uncompressed length.
pub fn decompress_block_bound(uncompressed_size: usize) -> usize {
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

        let mut inherited = vec![0u8; compress_block_bound(block.len())];
        let n_inherited = cctx
            .compress_block(block, &mut inherited, &CompressOptions::default())
            .expect("compress_block with inherited dict");

        // The same block through a dictionary-less context, for contrast.
        let mut plain_ctx = Cctx::new(None).expect("Cctx::new");
        let mut plain = vec![0u8; compress_block_bound(block.len())];
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
        let mut comp = vec![0u8; compress_block_bound(block.len())];
        let mut out = vec![0u8; decompress_block_bound(block.len())];
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
        let mut comp = vec![0u8; compress_block_bound(block.len())];
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
