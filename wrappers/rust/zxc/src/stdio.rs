/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! [`std::io::Read`] / [`std::io::Write`] adapters over the push streaming API.
//!
//! Mirror of the wrapper exposed by the Go binding: turns a [`CStream`] /
//! [`DStream`] pair into the standard streaming traits so ZXC can be plugged
//! into pipelines that expect them.

use std::io::{self, Read, Write};

use crate::{CStream, CompressOptions, DStream, DecompressOptions, Error};

/// Magic word identifying a ZXC file frame: little-endian `0x9CB02EF5`.
const ZXC_MAGIC_LE: [u8; 4] = [0xF5, 0x2E, 0xB0, 0x9C];

/// Reports whether `data` starts with the ZXC file magic word.
///
/// Useful for content-type sniffing in containers / object stores that need
/// to decide which decoder to dispatch.
/// The check is cheap and side-effect free; it does not validate the rest of
/// the header or the footer.
///
/// # Example
///
/// ```rust
/// use zxc::{compress, detect_zxc, Level};
///
/// let frame = compress(b"hello", Level::Default, None).unwrap();
/// assert!(detect_zxc(&frame));
/// assert!(!detect_zxc(b"not a zxc frame"));
/// ```
pub fn detect_zxc(data: &[u8]) -> bool {
    data.len() >= 4 && data[..4] == ZXC_MAGIC_LE
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

/// Streaming compressor implementing [`std::io::Write`].
///
/// Writes are forwarded through a [`CStream`] and the resulting ZXC frame is
/// flushed to the inner writer. The frame is finalised (residual block, EOF
/// marker, and footer) by [`Encoder::finish`]. [`Drop`] performs a best-effort
/// finish but cannot surface errors — prefer [`Encoder::finish`] when error
/// handling matters.
///
/// `Encoder` is single-threaded; one stream per writer.
///
/// # Example
///
/// ```rust,no_run
/// use std::io::Write;
/// use zxc::Encoder;
///
/// let sink = Vec::new();
/// let mut enc = Encoder::new(sink).unwrap();
/// enc.write_all(b"hello world").unwrap();
/// let compressed: Vec<u8> = enc.finish().unwrap();
/// ```
pub struct Encoder<W: Write> {
    inner: Option<W>,
    cs: Option<CStream>,
    out_buf: Vec<u8>,
}

impl<W: Write> Encoder<W> {
    /// Creates an encoder with default compression options.
    pub fn new(writer: W) -> Result<Self, Error> {
        Self::with_options(writer, None)
    }

    /// Creates an encoder honouring `opts`. `opts.seekable` is ignored (push
    /// API is single-threaded and never emits a seek table).
    pub fn with_options(writer: W, opts: Option<&CompressOptions>) -> Result<Self, Error> {
        let cs = CStream::new(opts)?;
        let cap = cs.out_size();
        Ok(Self {
            inner: Some(writer),
            cs: Some(cs),
            out_buf: vec![0u8; cap],
        })
    }

    /// Finalises the frame, drains the inner writer, and returns it.
    ///
    /// After this call the encoder is consumed; no further writes are
    /// possible.
    pub fn finish(mut self) -> io::Result<W> {
        self.do_finish()?;
        // Safety: do_finish leaves both fields populated on success.
        Ok(self.inner.take().expect("inner writer present"))
    }

    /// Returns a reference to the underlying writer.
    pub fn get_ref(&self) -> &W {
        self.inner.as_ref().expect("encoder not finished")
    }

    /// Returns a mutable reference to the underlying writer.
    pub fn get_mut(&mut self) -> &mut W {
        self.inner.as_mut().expect("encoder not finished")
    }

    fn do_finish(&mut self) -> io::Result<()> {
        let (Some(cs), Some(w)) = (self.cs.as_mut(), self.inner.as_mut()) else {
            return Ok(());
        };
        loop {
            let p = cs.end(&mut self.out_buf).map_err(map_err)?;
            if p.produced > 0 {
                w.write_all(&self.out_buf[..p.produced])?;
            }
            if p.pending == 0 {
                break;
            }
        }
        // Drop the CStream now so a later finish() / Drop is a no-op.
        self.cs = None;
        Ok(())
    }
}

impl<W: Write> Write for Encoder<W> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        if buf.is_empty() {
            return Ok(0);
        }
        let cs = self
            .cs
            .as_mut()
            .ok_or_else(|| io::Error::other("encoder finished"))?;
        let w = self
            .inner
            .as_mut()
            .ok_or_else(|| io::Error::other("encoder finished"))?;

        let mut total = 0;
        let mut input = buf;
        while !input.is_empty() {
            let p = cs.compress(input, &mut self.out_buf).map_err(map_err)?;
            if p.produced > 0 {
                w.write_all(&self.out_buf[..p.produced])?;
            }
            total += p.consumed;
            input = &input[p.consumed..];
            if p.consumed == 0 && p.produced == 0 && p.pending == 0 {
                // No forward progress is possible — bail out to avoid spin.
                break;
            }
        }
        Ok(total)
    }

    /// Flush is a no-op on the inner writer side: the ZXC frame is only valid
    /// once finalised, so partial flushes would produce a corrupted frame.
    /// Use [`Encoder::finish`] to complete the frame.
    fn flush(&mut self) -> io::Result<()> {
        if let Some(w) = self.inner.as_mut() {
            w.flush()
        } else {
            Ok(())
        }
    }
}

impl<W: Write> Drop for Encoder<W> {
    fn drop(&mut self) {
        // Best-effort: any error is silently swallowed because Drop cannot
        // surface failure. Callers who care about errors must call finish().
        let _ = self.do_finish();
    }
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

/// Streaming decompressor implementing [`std::io::Read`].
///
/// Pulls compressed bytes from the inner reader and yields decompressed
/// bytes. Returns [`io::ErrorKind::UnexpectedEof`] if the underlying reader
/// is drained before the ZXC footer is reached.
///
/// `Decoder` is single-threaded; one stream per reader.
///
/// # Example
///
/// ```rust,no_run
/// use std::io::Read;
/// use zxc::Decoder;
///
/// let frame: &[u8] = &[]; // a ZXC frame
/// let mut dec = Decoder::new(frame).unwrap();
/// let mut buf = Vec::new();
/// dec.read_to_end(&mut buf).unwrap();
/// ```
pub struct Decoder<R: Read> {
    inner: R,
    ds: DStream,
    in_buf: Vec<u8>,
    in_pos: usize,
    in_len: usize,
    eof: bool,
}

impl<R: Read> Decoder<R> {
    /// Creates a decoder with default decompression options.
    pub fn new(reader: R) -> Result<Self, Error> {
        Self::with_options(reader, None)
    }

    /// Creates a decoder honouring `opts`.
    pub fn with_options(reader: R, opts: Option<&DecompressOptions>) -> Result<Self, Error> {
        let ds = DStream::new(opts)?;
        let cap = ds.in_size();
        Ok(Self {
            inner: reader,
            ds,
            in_buf: vec![0u8; cap],
            in_pos: 0,
            in_len: 0,
            eof: false,
        })
    }

    /// Returns a reference to the underlying reader.
    pub fn get_ref(&self) -> &R {
        &self.inner
    }

    /// Returns a mutable reference to the underlying reader.
    pub fn get_mut(&mut self) -> &mut R {
        &mut self.inner
    }

    /// Consumes the decoder and returns the inner reader.
    pub fn into_inner(self) -> R {
        self.inner
    }

    /// Reports whether the decoder has reached and validated the file footer.
    pub fn finished(&self) -> bool {
        self.ds.finished()
    }
}

impl<R: Read> Read for Decoder<R> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if buf.is_empty() {
            return Ok(0);
        }
        loop {
            if self.ds.finished() {
                return Ok(0);
            }

            // Try to decompress whatever is currently buffered (or drain mode
            // when src is at EOF).
            if self.in_pos < self.in_len || self.eof {
                let p = self
                    .ds
                    .decompress(&self.in_buf[self.in_pos..self.in_len], buf)
                    .map_err(map_err)?;
                self.in_pos += p.consumed;
                if p.produced > 0 {
                    return Ok(p.produced);
                }
                if p.consumed == 0 {
                    if self.eof {
                        if self.ds.finished() {
                            return Ok(0);
                        }
                        return Err(io::Error::new(
                            io::ErrorKind::UnexpectedEof,
                            "zxc: input drained before footer",
                        ));
                    }
                    // fall through to refill
                } else {
                    // consumed > 0 but no output — loop and retry.
                    continue;
                }
            }

            // Refill: shift consumed bytes out, then read more from src.
            if self.in_pos > 0 {
                self.in_buf.copy_within(self.in_pos..self.in_len, 0);
                self.in_len -= self.in_pos;
                self.in_pos = 0;
            }
            let n = self.inner.read(&mut self.in_buf[self.in_len..])?;
            if n == 0 {
                self.eof = true;
            } else {
                self.in_len += n;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Error mapping
// ---------------------------------------------------------------------------

fn map_err(e: Error) -> io::Error {
    io::Error::other(e)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::compress;
    use crate::Level;
    use std::io::{Cursor, Read, Write};

    fn roundtrip(data: &[u8]) -> Vec<u8> {
        let mut enc = Encoder::new(Vec::new()).expect("encoder");
        enc.write_all(data).expect("write");
        let frame = enc.finish().expect("finish");

        let mut dec = Decoder::new(Cursor::new(frame)).expect("decoder");
        let mut out = Vec::new();
        dec.read_to_end(&mut out).expect("read_to_end");
        out
    }

    #[test]
    fn encoder_decoder_small_roundtrip() {
        let data = b"Hello std::io::Read / std::io::Write bridge over ZXC.";
        assert_eq!(roundtrip(data), data);
    }

    #[test]
    fn encoder_decoder_large_roundtrip() {
        let data: Vec<u8> = (0..2 * 1024 * 1024).map(|i| ((i * 13) % 251) as u8).collect();
        assert_eq!(roundtrip(&data), data);
    }

    #[test]
    fn encoder_many_small_writes() {
        let mut enc = Encoder::new(Vec::new()).unwrap();
        let mut want: Vec<u8> = Vec::with_capacity(64 * 1024);
        for i in 0u32..4096 {
            let chunk = [i as u8, (i >> 8) as u8, (i ^ 0x5A) as u8];
            want.extend_from_slice(&chunk);
            enc.write_all(&chunk).unwrap();
        }
        let frame = enc.finish().unwrap();
        let mut dec = Decoder::new(Cursor::new(frame)).unwrap();
        let mut got = Vec::new();
        dec.read_to_end(&mut got).unwrap();
        assert_eq!(got, want);
    }

    #[test]
    fn encoder_decoder_with_checksum() {
        use crate::{CompressOptions, DecompressOptions};
        let data: Vec<u8> = (0..32 * 1024).map(|i| i as u8).collect();
        let copts = CompressOptions {
            checksum: true,
            ..Default::default()
        };
        let dopts = DecompressOptions {
            verify_checksum: true,
        };
        let mut enc = Encoder::with_options(Vec::new(), Some(&copts)).unwrap();
        enc.write_all(&data).unwrap();
        let frame = enc.finish().unwrap();
        let mut dec = Decoder::with_options(Cursor::new(frame), Some(&dopts)).unwrap();
        let mut got = Vec::new();
        dec.read_to_end(&mut got).unwrap();
        assert_eq!(got, data);
    }

    #[test]
    fn drop_finishes_frame() {
        // Encoder is dropped without an explicit finish() — the resulting
        // frame must still be well-formed (best-effort flush in Drop).
        let mut sink: Vec<u8> = Vec::new();
        {
            let mut enc = Encoder::new(&mut sink).unwrap();
            enc.write_all(b"drop-flush").unwrap();
        }
        let mut dec = Decoder::new(Cursor::new(sink)).unwrap();
        let mut got = Vec::new();
        dec.read_to_end(&mut got).unwrap();
        assert_eq!(got, b"drop-flush");
    }

    #[test]
    fn decoder_truncated_frame_errors() {
        let frame = compress(&vec![b'A'; 32 * 1024], Level::Default, None).unwrap();
        let truncated = &frame[..frame.len() / 2];
        let mut dec = Decoder::new(Cursor::new(truncated)).unwrap();
        let mut got = Vec::new();
        let err = dec.read_to_end(&mut got).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::UnexpectedEof);
    }

    #[test]
    fn detect_zxc_basic() {
        let frame = compress(b"sniff me", Level::Default, None).unwrap();
        assert!(detect_zxc(&frame));

        let mut enc = Encoder::new(Vec::new()).unwrap();
        enc.write_all(b"hi").unwrap();
        let frame = enc.finish().unwrap();
        assert!(detect_zxc(&frame));

        assert!(!detect_zxc(&[]));
        assert!(!detect_zxc(&[0xF5, 0x2E, 0xB0])); // 3 bytes, too short
        assert!(!detect_zxc(&[0; 4]));
        assert!(!detect_zxc(b"not a zxc frame at all"));
    }
}
