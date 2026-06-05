/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! Push streaming API: single-threaded, caller-driven compression /
//! decompression streams. The safe Rust counterpart of `zxc_cstream` /
//! `zxc_dstream`.

use std::ffi::c_void;

use crate::error::error_from_code;
use crate::{CompressOptions, DecompressOptions, Error, Result};

/// Reports how a single [`CStream::compress`] / [`CStream::end`] call
/// progressed.
///
/// `pending == 0` means input was fully consumed and no output is staged
/// internally; positive values indicate the caller should drain `output`
/// and call again.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CStreamProgress {
    /// Bytes consumed from `input` this call.
    pub consumed: usize,
    /// Bytes written into `output` this call.
    pub produced: usize,
    /// Bytes still pending in the internal staging area; drain `output`
    /// and call again to make progress.
    pub pending: u64,
}

/// Reports how a single [`DStream::decompress`] call progressed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DStreamProgress {
    /// Bytes consumed from `input` this call.
    pub consumed: usize,
    /// Bytes written into `output` this call.
    pub produced: usize,
    /// `true` once the decoder has reached and validated the file footer.
    pub finished: bool,
}

/// Push compression stream — the safe Rust counterpart of `zxc_cstream`.
///
/// Use this when you cannot block on a `FILE*` (event loops, async runtimes,
/// network protocols, callback-driven libraries). The stream is single-
/// threaded; for parallel file-to-file compression, use [`crate::compress_file`].
///
/// # Example
///
/// ```rust,no_run
/// use zxc::{CStream, CompressOptions, Level};
///
/// let mut cs = CStream::new(Some(&CompressOptions::with_level(Level::Default)))?;
///
/// let mut out = vec![0u8; cs.out_size()];
/// let mut sink: Vec<u8> = Vec::new();
///
/// for chunk in source_chunks() {
///     let mut cursor = 0;
///     loop {
///         let p = cs.compress(&chunk[cursor..], &mut out)?;
///         cursor += p.consumed;
///         sink.extend_from_slice(&out[..p.produced]);
///         if p.pending == 0 && cursor == chunk.len() { break; }
///     }
/// }
/// loop {
///     let p = cs.end(&mut out)?;
///     sink.extend_from_slice(&out[..p.produced]);
///     if p.pending == 0 { break; }
/// }
/// # fn source_chunks() -> Vec<Vec<u8>> { vec![] }
/// # Ok::<(), zxc::Error>(())
/// ```
pub struct CStream {
    inner: *mut zxc_sys::zxc_cstream,
}

unsafe impl Send for CStream {}

impl CStream {
    /// Creates a new push compression stream.
    ///
    /// `opts.seekable` is ignored (the push API is single-threaded and does
    /// not emit a seek table). Pass `None` for all defaults.
    pub fn new(opts: Option<&CompressOptions>) -> Result<Self> {
        let c_opts = opts.map(|o| zxc_sys::zxc_compress_opts_t {
            level: o.level as i32,
            checksum_enabled: o.checksum as i32,
            seekable: 0,
            ..Default::default()
        });
        let ptr = unsafe {
            zxc_sys::zxc_cstream_create(
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

    /// Pushes `input` into the stream and writes compressed bytes to `output`.
    ///
    /// On return, `progress.consumed` and `progress.produced` describe the
    /// slice ranges that were processed; `progress.pending` is the number of
    /// bytes still staged inside the stream (drain `output` and call again).
    pub fn compress(&mut self, input: &[u8], output: &mut [u8]) -> Result<CStreamProgress> {
        let mut in_buf = zxc_sys::zxc_inbuf_t {
            src: input.as_ptr() as *const c_void,
            size: input.len(),
            pos: 0,
        };
        let mut out_buf = zxc_sys::zxc_outbuf_t {
            dst: output.as_mut_ptr() as *mut c_void,
            size: output.len(),
            pos: 0,
        };
        let r = unsafe { zxc_sys::zxc_cstream_compress(self.inner, &mut out_buf, &mut in_buf) };
        if r < 0 {
            return Err(error_from_code(r));
        }
        Ok(CStreamProgress {
            consumed: in_buf.pos,
            produced: out_buf.pos,
            pending: r as u64,
        })
    }

    /// Finalises the stream: flushes the residual block, then writes the
    /// EOF block and the file footer into `output`.
    ///
    /// Call repeatedly while `pending > 0`, draining `output` between calls.
    pub fn end(&mut self, output: &mut [u8]) -> Result<CStreamProgress> {
        let mut out_buf = zxc_sys::zxc_outbuf_t {
            dst: output.as_mut_ptr() as *mut c_void,
            size: output.len(),
            pos: 0,
        };
        let r = unsafe { zxc_sys::zxc_cstream_end(self.inner, &mut out_buf) };
        if r < 0 {
            return Err(error_from_code(r));
        }
        Ok(CStreamProgress {
            consumed: 0,
            produced: out_buf.pos,
            pending: r as u64,
        })
    }

    /// Suggested input chunk size for best throughput.
    pub fn in_size(&self) -> usize {
        unsafe { zxc_sys::zxc_cstream_in_size(self.inner) }
    }

    /// Suggested output chunk size to never trigger a partial drain.
    pub fn out_size(&self) -> usize {
        unsafe { zxc_sys::zxc_cstream_out_size(self.inner) }
    }
}

impl Drop for CStream {
    fn drop(&mut self) {
        unsafe { zxc_sys::zxc_cstream_free(self.inner) };
    }
}

/// Push decompression stream — the safe Rust counterpart of `zxc_dstream`.
///
/// # Example
///
/// ```rust,no_run
/// use zxc::{DStream, DecompressOptions};
///
/// let mut ds = DStream::new(None)?;
/// let mut out = vec![0u8; ds.out_size()];
/// let mut sink: Vec<u8> = Vec::new();
///
/// for chunk in compressed_chunks() {
///     let mut cursor = 0;
///     while cursor < chunk.len() && !ds.finished() {
///         let p = ds.decompress(&chunk[cursor..], &mut out)?;
///         cursor += p.consumed;
///         sink.extend_from_slice(&out[..p.produced]);
///         if p.consumed == 0 && p.produced == 0 { break; }
///     }
/// }
/// assert!(ds.finished());
/// # fn compressed_chunks() -> Vec<Vec<u8>> { vec![] }
/// # Ok::<(), zxc::Error>(())
/// ```
pub struct DStream {
    inner: *mut zxc_sys::zxc_dstream,
}

unsafe impl Send for DStream {}

impl DStream {
    /// Creates a new push decompression stream.
    pub fn new(opts: Option<&DecompressOptions>) -> Result<Self> {
        let c_opts = opts.map(|o| zxc_sys::zxc_decompress_opts_t {
            checksum_enabled: o.verify_checksum as i32,
            ..Default::default()
        });
        let ptr = unsafe {
            zxc_sys::zxc_dstream_create(
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

    /// Pushes compressed bytes from `input` and writes decompressed bytes
    /// to `output`. The state machine consumes file header, block headers,
    /// payloads, and footer transparently.
    pub fn decompress(&mut self, input: &[u8], output: &mut [u8]) -> Result<DStreamProgress> {
        let mut in_buf = zxc_sys::zxc_inbuf_t {
            src: input.as_ptr() as *const c_void,
            size: input.len(),
            pos: 0,
        };
        let mut out_buf = zxc_sys::zxc_outbuf_t {
            dst: output.as_mut_ptr() as *mut c_void,
            size: output.len(),
            pos: 0,
        };
        let r = unsafe { zxc_sys::zxc_dstream_decompress(self.inner, &mut out_buf, &mut in_buf) };
        if r < 0 {
            return Err(error_from_code(r));
        }
        Ok(DStreamProgress {
            consumed: in_buf.pos,
            produced: out_buf.pos,
            finished: unsafe { zxc_sys::zxc_dstream_finished(self.inner) } != 0,
        })
    }

    /// Returns `true` iff the decoder reached and validated the file footer.
    /// Useful to detect truncated streams after the input source is drained.
    pub fn finished(&self) -> bool {
        unsafe { zxc_sys::zxc_dstream_finished(self.inner) != 0 }
    }

    /// Suggested input chunk size for the decompressor.
    pub fn in_size(&self) -> usize {
        unsafe { zxc_sys::zxc_dstream_in_size(self.inner) }
    }

    /// Suggested output chunk size for the decompressor.
    pub fn out_size(&self) -> usize {
        unsafe { zxc_sys::zxc_dstream_out_size(self.inner) }
    }
}

impl Drop for DStream {
    fn drop(&mut self) {
        unsafe { zxc_sys::zxc_dstream_free(self.inner) };
    }
}

#[cfg(test)]
mod tests {
    use crate::*;

    fn pstream_roundtrip(
        data: &[u8],
        copts: Option<&CompressOptions>,
        dopts: Option<&DecompressOptions>,
    ) -> Vec<u8> {
        let mut cs = CStream::new(copts).expect("cstream alloc");
        let mut compressed: Vec<u8> = Vec::new();
        let mut out = vec![0u8; cs.out_size().max(64)];

        let mut cursor = 0;
        while cursor < data.len() {
            loop {
                let p = cs.compress(&data[cursor..], &mut out).unwrap();
                cursor += p.consumed;
                compressed.extend_from_slice(&out[..p.produced]);
                if p.pending == 0 && (p.consumed > 0 || cursor == data.len()) {
                    break;
                }
            }
        }
        loop {
            let p = cs.end(&mut out).unwrap();
            compressed.extend_from_slice(&out[..p.produced]);
            if p.pending == 0 {
                break;
            }
        }

        let mut ds = DStream::new(dopts).expect("dstream alloc");
        let mut decompressed: Vec<u8> = Vec::new();
        let mut dout = vec![0u8; 64 * 1024];
        let mut cursor = 0;
        while cursor < compressed.len() && !ds.finished() {
            let p = ds.decompress(&compressed[cursor..], &mut dout).unwrap();
            cursor += p.consumed;
            decompressed.extend_from_slice(&dout[..p.produced]);
            if p.consumed == 0 && p.produced == 0 {
                break;
            }
        }
        // Final drain in case we have nothing more to feed but staged output remains.
        loop {
            let p = ds.decompress(&[], &mut dout).unwrap();
            decompressed.extend_from_slice(&dout[..p.produced]);
            if p.produced == 0 {
                break;
            }
        }
        assert!(ds.finished(), "decoder did not finalise");
        decompressed
    }

    #[test]
    fn pstream_small_roundtrip() {
        let data = b"Hello pstream! This is a small message that round-trips through the push API.";
        let out = pstream_roundtrip(data, None, None);
        assert_eq!(out, data);
    }

    #[test]
    fn pstream_with_checksum() {
        let data: Vec<u8> = (0..32 * 1024).map(|i| (i % 251) as u8).collect();
        let copts = CompressOptions {
            level: Level::Default,
            checksum: true,
            seekable: false,
            ..Default::default()
        };
        let dopts = DecompressOptions {
            verify_checksum: true,
            ..Default::default()
        };
        let out = pstream_roundtrip(&data, Some(&copts), Some(&dopts));
        assert_eq!(out, data);
    }

    #[test]
    fn pstream_multi_block() {
        // Larger than one default block (512 KB) to force multiple blocks.
        let data: Vec<u8> = (0..1536 * 1024).map(|i| ((i * 7) % 256) as u8).collect();
        let out = pstream_roundtrip(&data, None, None);
        assert_eq!(out, data);
    }

    #[test]
    fn pstream_size_hints_nonzero() {
        let cs = CStream::new(None).unwrap();
        assert!(cs.in_size() > 0);
        assert!(cs.out_size() > 0);
        let ds = DStream::new(None).unwrap();
        assert!(ds.in_size() > 0);
        assert!(ds.out_size() > 0);
        assert!(!ds.finished());
    }
}
