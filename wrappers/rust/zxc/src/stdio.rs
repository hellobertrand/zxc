// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
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
    /// `out_buf[out_pos..out_len]` is output the writer has not taken yet.
    out_pos: usize,
    out_len: usize,
}

impl<W: Write> Encoder<W> {
    /// Creates an encoder with default compression options: no checksum.
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
            out_pos: 0,
            out_len: 0,
        })
    }

    /// Finalises the frame, drains the inner writer, and returns it.
    ///
    /// After this call the encoder is consumed; no further writes are
    /// possible.
    pub fn finish(mut self) -> io::Result<W> {
        if let Err(e) = self.do_finish() {
            // The caller gets no way to retry: Drop must not write to this writer again.
            self.cs = None;
            self.out_pos = self.out_len;
            return Err(e);
        }
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

    fn drain(&mut self) -> io::Result<()> {
        while self.out_pos < self.out_len {
            let Some(w) = self.inner.as_mut() else {
                return Ok(());
            };
            match w.write(&self.out_buf[self.out_pos..self.out_len]) {
                Ok(0) => return Err(io::ErrorKind::WriteZero.into()),
                Ok(n) => self.out_pos += n,
                Err(e) if e.kind() == io::ErrorKind::Interrupted => {}
                Err(e) => return Err(e),
            }
        }
        Ok(())
    }

    fn do_finish(&mut self) -> io::Result<()> {
        loop {
            self.drain()?;
            let Some(cs) = self.cs.as_mut() else {
                return Ok(());
            };
            let p = cs.end(&mut self.out_buf).map_err(map_err)?;
            (self.out_pos, self.out_len) = (0, p.produced);
            if p.pending == 0 {
                self.cs = None;
            }
        }
    }
}

impl<W: Write> Write for Encoder<W> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        if buf.is_empty() {
            return Ok(0);
        }
        self.drain()?;

        let mut total = 0;
        while total < buf.len() {
            let cs = self
                .cs
                .as_mut()
                .ok_or_else(|| io::Error::other("encoder finished"))?;
            let p = cs
                .compress(&buf[total..], &mut self.out_buf)
                .map_err(map_err)?;
            total += p.consumed;
            (self.out_pos, self.out_len) = (0, p.produced);
            if let Err(e) = self.drain() {
                // Err would mean nothing was consumed. The output is kept, so the
                // next call retries it and reports a lasting failure.
                return if total > 0 { Ok(total) } else { Err(e) };
            }
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
        self.drain()?;
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
/// bytes. Reads until the inner reader ends, so concatenated archives
/// decode as one stream; on a source that stays open after one frame,
/// set [`Decoder::multistream`] to `false`. Returns
/// [`io::ErrorKind::UnexpectedEof`] if the inner reader is drained before the
/// ZXC footer is reached.
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
    multistream: bool,
}

impl<R: Read> Decoder<R> {
    /// Creates a decoder with default decompression options: checksums are
    /// verified when a frame carries them.
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
            multistream: true,
        })
    }

    /// Decodes concatenated archives as one stream (`true`, the default), or
    /// stops after the first frame, as on a source that stays open after it.
    /// Bytes read ahead past that frame are then lost to the caller.
    pub fn multistream(mut self, enabled: bool) -> Self {
        self.multistream = enabled;
        self
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

    /// Reports whether the input so far ends on a validated footer.
    pub fn finished(&self) -> bool {
        self.ds.finished()
    }
}

impl<R: Read> Read for Decoder<R> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if buf.is_empty() {
            return Ok(0);
        }
        // Stop when src ends, or after the first frame without multistream.
        loop {
            if !self.multistream && self.ds.finished() {
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
    use crate::Level;
    use crate::{compress, decompress};
    use std::io::{Cursor, Read, Write};
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};

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
        let data: Vec<u8> = (0..2 * 1024 * 1024)
            .map(|i| ((i * 13) % 251) as u8)
            .collect();
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
            verify_checksum: Some(true),
            ..Default::default()
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

    /// Fails once on the `fail_at`-th write, and takes at most `max_write` bytes.
    struct FlakySink {
        data: Vec<u8>,
        writes: Arc<AtomicUsize>,
        fail_at: usize,
        max_write: usize,
    }
    impl FlakySink {
        fn new(fail_at: usize, max_write: usize) -> Self {
            Self {
                data: Vec::new(),
                writes: Arc::default(),
                fail_at,
                max_write,
            }
        }
    }
    impl Write for FlakySink {
        fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
            if self.writes.fetch_add(1, Ordering::Relaxed) == self.fail_at {
                return Err(io::ErrorKind::WouldBlock.into());
            }
            let n = buf.len().min(self.max_write);
            self.data.extend_from_slice(&buf[..n]);
            Ok(n)
        }
        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }

    fn noise(len: usize) -> Vec<u8> {
        let mut rng: u32 = 0x2E5B_9A17;
        (0..len)
            .map(|_| {
                rng = rng.wrapping_mul(1_103_515_245).wrapping_add(12_345);
                (rng >> 16) as u8
            })
            .collect()
    }

    #[test]
    fn encoder_keeps_output_across_a_sink_error() {
        let payload = noise(600 * 1024);
        // The first write carries the header and the first block.
        for (fail_at, max_write) in [(0, usize::MAX), (0, 4096), (1, 4096), (9, 4096)] {
            let mut enc = Encoder::new(FlakySink::new(fail_at, max_write)).unwrap();
            let mut done = 0;
            let mut errors = 0;
            while done < payload.len() {
                match enc.write(&payload[done..]) {
                    Ok(n) => done += n,
                    Err(e) => {
                        assert_eq!(e.kind(), io::ErrorKind::WouldBlock);
                        errors += 1;
                    }
                }
            }
            assert!(errors <= 1);
            enc.flush().ok();
            let sink = enc.finish().unwrap();
            assert_eq!(decompress(&sink.data).unwrap(), payload);
        }
    }

    #[test]
    fn failed_finish_stops_writing() {
        // The header goes out on the first write, the rest fails in finish().
        let sink = FlakySink::new(1, usize::MAX);
        let writes = sink.writes.clone();
        let mut enc = Encoder::new(sink).unwrap();
        enc.write_all(b"never reaches the sink").unwrap();
        assert_eq!(writes.load(Ordering::Relaxed), 1);
        assert!(enc.finish().is_err());
        assert_eq!(writes.load(Ordering::Relaxed), 2, "Drop wrote again");
    }

    #[test]
    fn lasting_sink_failure_is_reported() {
        struct Dead;
        impl Write for Dead {
            fn write(&mut self, _: &[u8]) -> io::Result<usize> {
                Err(io::Error::other("dead"))
            }
            fn flush(&mut self) -> io::Result<()> {
                Ok(())
            }
        }
        let mut enc = Encoder::new(Dead).unwrap();
        let chunk = noise(1 << 20);
        // Output of a call that consumed its input fails on the next one.
        assert!((0..2).try_for_each(|_| enc.write_all(&chunk)).is_err());
        assert!(enc.flush().is_err());

        let mut enc = Encoder::new(Dead).unwrap();
        enc.write_all(b"small").unwrap();
        assert!(enc.finish().is_err());
    }

    /// Hands out one byte per read, so a read ends on every frame boundary.
    struct OneByte<'a>(&'a [u8]);
    impl Read for OneByte<'_> {
        fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
            if self.0.is_empty() || buf.is_empty() {
                return Ok(0);
            }
            buf[0] = self.0[0];
            self.0 = &self.0[1..];
            Ok(1)
        }
    }

    #[test]
    fn decoder_concatenated_frames() {
        let a = b"first frame ".repeat(5000);
        let b = b"second frame ".repeat(7000);
        let fa = compress(&a, Level::Default, None).unwrap();
        let joined = [
            fa.clone(),
            compress(&b, Level::Default, Some(true)).unwrap(),
        ]
        .concat();
        let mut got = Vec::new();
        Decoder::new(OneByte(&joined))
            .unwrap()
            .read_to_end(&mut got)
            .unwrap();
        assert_eq!(got, [a.clone(), b].concat());

        // Without multistream, the decoder stops after the first frame.
        let mut first = Vec::new();
        Decoder::new(OneByte(&joined))
            .unwrap()
            .multistream(false)
            .read_to_end(&mut first)
            .unwrap();
        assert_eq!(first, a);

        let junk = [fa, b"junk".to_vec()].concat();
        let mut dec = Decoder::new(Cursor::new(junk)).unwrap();
        assert!(dec.read_to_end(&mut Vec::new()).is_err());
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
