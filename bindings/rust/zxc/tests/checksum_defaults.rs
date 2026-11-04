// SPDX-License-Identifier: BSD-3-Clause
//! Compression writes no checksum by default; every frame read path verifies
//! one by default. Bare blocks are the exception: see the last tests.

use std::fs;
use std::io::{Cursor, Read, Write};
use std::path::PathBuf;

use zxc::seekable::ReadAt;
use zxc::*;

fn noise(len: usize) -> Vec<u8> {
    let mut rng: u32 = 0x2E5B_9A17;
    (0..len)
        .map(|_| {
            rng = rng.wrapping_mul(1_103_515_245).wrapping_add(12_345);
            (rng >> 16) as u8
        })
        .collect()
}

/// Flips a byte of block 0: a RAW block copies it through, so only the checksum objects.
fn corrupted(mut frame: Vec<u8>) -> Vec<u8> {
    assert_eq!(frame[16], 0, "block 0 must be RAW");
    frame[16 + 8 + 4] ^= 0xFF;
    frame
}

fn temp_dir(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("zxc_ck_{}_{name}", std::process::id()));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn io_code(e: std::io::Error) -> Error {
    e.get_ref()
        .and_then(|inner| inner.downcast_ref::<Error>())
        .cloned()
        .unwrap_or_else(|| panic!("not a zxc error: {e}"))
}

fn dstream(frame: &[u8], opts: Option<&DecompressOptions>) -> Result<Vec<u8>> {
    let mut ds = DStream::new(opts)?;
    let mut buf = vec![0u8; ds.out_size()];
    let mut out = Vec::new();
    let mut pos = 0;
    while pos < frame.len() {
        let p = ds.decompress(&frame[pos..], &mut buf)?;
        pos += p.consumed;
        out.extend_from_slice(&buf[..p.produced]);
    }
    assert!(ds.finished());
    Ok(out)
}

fn decoder(frame: &[u8], opts: Option<&DecompressOptions>) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    Decoder::with_options(Cursor::new(frame), opts)?
        .read_to_end(&mut out)
        .map_err(io_code)?;
    Ok(out)
}

fn decompress_to_vec(frame: &[u8], opts: &DecompressOptions, len: usize) -> Result<Vec<u8>> {
    let mut out = vec![0u8; len];
    let n = decompress_to(frame, &mut out, opts)?;
    out.truncate(n);
    Ok(out)
}

/// Every one-shot and stream read path at its default, by name.
fn default_paths(frame: &[u8], len: usize) -> Vec<(&'static str, Result<Vec<u8>>)> {
    let defaults = DecompressOptions::default();
    let mut new_decoder = Vec::new();
    let new_decoder = Decoder::new(Cursor::new(frame))
        .unwrap()
        .read_to_end(&mut new_decoder)
        .map_err(io_code)
        .map(|_| new_decoder);
    vec![
        ("decompress", decompress(frame)),
        (
            "decompress_with_options",
            decompress_with_options(frame, &defaults),
        ),
        ("decompress_to", decompress_to_vec(frame, &defaults, len)),
        ("Decoder::new", new_decoder),
        ("Decoder(None)", decoder(frame, None)),
        ("Decoder(default)", decoder(frame, Some(&defaults))),
        ("DStream(None)", dstream(frame, None)),
        ("DStream(default)", dstream(frame, Some(&defaults))),
    ]
}

fn file_decompress(
    dir: &std::path::Path,
    frame: &[u8],
    opts: Option<&StreamDecompressOptions>,
) -> std::result::Result<Vec<u8>, StreamError> {
    let (input, output) = (dir.join("arc"), dir.join("out"));
    fs::write(&input, frame).unwrap();
    match opts {
        Some(o) => decompress_file_with_options(&input, &output, o)?,
        None => decompress_file(&input, &output, Some(1))?,
    };
    Ok(fs::read(&output).unwrap())
}

struct VecReader(Vec<u8>);
impl ReadAt for VecReader {
    fn size(&self) -> u64 {
        self.0.len() as u64
    }
    fn read_at(&self, dst: &mut [u8], offset: u64) -> std::io::Result<()> {
        let off = offset as usize;
        dst.copy_from_slice(&self.0[off..off + dst.len()]);
        Ok(())
    }
}

/// The three ways to open a seekable archive, by name.
fn seekables(dir: &std::path::Path, frame: &[u8]) -> Vec<(&'static str, Seekable)> {
    let path = dir.join("seekable");
    fs::write(&path, frame).unwrap();
    vec![
        ("from_bytes", Seekable::from_bytes(frame.to_vec()).unwrap()),
        ("open", Seekable::open(&path).unwrap()),
        (
            "open_reader",
            Seekable::open_reader(VecReader(frame.to_vec())).unwrap(),
        ),
    ]
}

#[test]
fn compression_writes_no_checksum_by_default() {
    let data = b"default frame ".repeat(100);
    let has = |frame: &[u8]| frame_info(frame).unwrap().has_checksum;

    assert!(!has(&compress(&data, Level::Default, None).unwrap()));
    let defaults = CompressOptions::default();
    assert!(!has(&compress_with_options(&data, &defaults).unwrap()));
    let with_level = CompressOptions::with_level(Level::Fast);
    assert!(!has(&compress_with_options(&data, &with_level).unwrap()));
    let mut out = vec![0u8; compress_bound(data.len())];
    let n = compress_to(&data, &mut out, &defaults).unwrap();
    assert!(!has(&out[..n]));

    for opts in [None, Some(&defaults)] {
        let mut enc = Encoder::with_options(Vec::new(), opts).unwrap();
        enc.write_all(&data).unwrap();
        assert!(!has(&enc.finish().unwrap()));
    }
    let mut enc = Encoder::new(Vec::new()).unwrap();
    enc.write_all(&data).unwrap();
    assert!(!has(&enc.finish().unwrap()));

    let dir = temp_dir("compress");
    let (input, output) = (dir.join("in"), dir.join("out"));
    fs::write(&input, &data).unwrap();
    compress_file(&input, &output, Level::Default, Some(1), None).unwrap();
    assert!(!has(&fs::read(&output).unwrap()));
    let opts = StreamCompressOptions::default().threads(1);
    compress_file_with_options(&input, &output, &opts).unwrap();
    assert!(!has(&fs::read(&output).unwrap()));
    let _ = fs::remove_dir_all(&dir);

    // Asking still works.
    assert!(has(&compress(&data, Level::Default, Some(true)).unwrap()));
}

#[test]
fn every_default_read_path_rejects_a_corrupted_checksummed_frame() {
    let payload = noise(262_144);
    let frame = corrupted(compress(&payload, Level::Default, Some(true)).unwrap());

    for (name, result) in default_paths(&frame, payload.len()) {
        assert!(
            matches!(result, Err(Error::BadChecksum)),
            "{name}: {result:?}"
        );
    }

    let dir = temp_dir("corrupt");
    for opts in [None, Some(&StreamDecompressOptions::default().threads(1))] {
        assert!(matches!(
            file_decompress(&dir, &frame, opts),
            Err(StreamError::BufferError(Error::BadChecksum))
        ));
    }

    let opts = CompressOptions::default().with_seekable().with_checksum();
    let seekable = corrupted(compress_with_options(&payload, &opts).unwrap());
    let mut out = vec![0u8; 512];
    for (name, mut s) in seekables(&dir, &seekable) {
        let r = s.decompress_range(&mut out, 0, 512);
        assert!(matches!(r, Err(Error::BadChecksum)), "{name}: {r:?}");
    }
    let _ = fs::remove_dir_all(&dir);
}

#[test]
fn the_opt_out_does_not_report_a_bad_checksum() {
    let payload = noise(262_144);
    let frame = corrupted(compress(&payload, Level::Default, Some(true)).unwrap());
    let skip = DecompressOptions::skip_checksum();

    for (name, result) in [
        (
            "decompress_with_options",
            decompress_with_options(&frame, &skip),
        ),
        (
            "decompress_to",
            decompress_to_vec(&frame, &skip, payload.len()),
        ),
        ("Decoder", decoder(&frame, Some(&skip))),
        ("DStream", dstream(&frame, Some(&skip))),
    ] {
        let out = result.unwrap_or_else(|e| panic!("{name}: {e}"));
        assert_eq!(out.len(), payload.len(), "{name}");
        assert_ne!(out, payload, "{name}");
    }

    let dir = temp_dir("skip");
    let opts = StreamDecompressOptions::default()
        .threads(1)
        .skip_checksum();
    let out = file_decompress(&dir, &frame, Some(&opts)).unwrap();
    assert_eq!(out.len(), payload.len());
    assert_ne!(out, payload);

    let opts = CompressOptions::default().with_seekable().with_checksum();
    let seekable = corrupted(compress_with_options(&payload, &opts).unwrap());
    let mut out = vec![0u8; 512];
    for (name, mut s) in seekables(&dir, &seekable) {
        s.set_checksum(false).unwrap();
        assert_eq!(s.decompress_range(&mut out, 0, 512).unwrap(), 512, "{name}");
        assert_ne!(out, payload[..512], "{name}");
    }
    let _ = fs::remove_dir_all(&dir);
}

#[test]
fn a_frame_without_checksums_decodes_through_every_path() {
    let payload = b"frame without checksums ".repeat(40_000);
    let frame = compress(&payload, Level::Default, None).unwrap();
    assert!(!frame_info(&frame).unwrap().has_checksum);

    for (name, result) in default_paths(&frame, payload.len()) {
        assert_eq!(
            result.unwrap_or_else(|e| panic!("{name}: {e}")),
            payload,
            "{name}"
        );
    }

    let dir = temp_dir("plain");
    assert_eq!(file_decompress(&dir, &frame, None).unwrap(), payload);

    let opts = CompressOptions::default().with_seekable();
    let seekable = compress_with_options(&payload, &opts).unwrap();
    let mut out = vec![0u8; 4096];
    for (name, mut s) in seekables(&dir, &seekable) {
        s.decompress_range(&mut out, 100_000, 4096).unwrap();
        assert_eq!(out, payload[100_000..104_096], "{name}");
    }

    // A frame says for itself: mixed concatenations verify the ones that have them.
    let checked = compress(&payload, Level::Default, Some(true)).unwrap();
    let mixed = [frame.clone(), checked].concat();
    assert_eq!(
        decompress(&mixed).unwrap(),
        [&payload[..], &payload[..]].concat()
    );
    let _ = fs::remove_dir_all(&dir);
}

fn block_pair(checksum: bool) -> (Vec<u8>, Vec<u8>) {
    let block = noise(32 * 1024);
    let opts = CompressOptions {
        checksum,
        ..CompressOptions::default()
    };
    let mut comp = vec![0u8; compress_block_bound(block.len())];
    let n = Cctx::new(None)
        .unwrap()
        .compress_block(&block, &mut comp, &opts)
        .unwrap();
    comp.truncate(n);
    (block, comp)
}

fn verify(on: bool) -> DecompressOptions {
    DecompressOptions {
        verify_checksum: Some(on),
        ..Default::default()
    }
}

#[test]
fn block_defaults_agree_and_do_not_verify() {
    let (block, plain) = block_pair(false);
    let (_, checked) = block_pair(true);
    assert_eq!(checked.len(), plain.len() + 4);
    let mut dctx = Dctx::new().unwrap();
    let mut out = vec![0u8; decompress_block_bound(block.len())];
    let mut exact = vec![0u8; block.len()];
    let defaults = DecompressOptions::default();

    // The default pair, and a default decoder on a checksummed block.
    for comp in [&plain, &checked] {
        let n = dctx.decompress_block(comp, &mut out, &defaults).unwrap();
        assert_eq!(&out[..n], block.as_slice());
        let n = dctx
            .decompress_block_safe(comp, &mut exact, &defaults)
            .unwrap();
        assert_eq!(&exact[..n], block.as_slice());
    }

    // Not verified by default: a flipped byte in the RAW payload passes.
    let mut flipped = checked.clone();
    assert_eq!(flipped[0], 0, "must be RAW");
    flipped[8 + 4] ^= 0xFF;
    let n = dctx
        .decompress_block(&flipped, &mut out, &defaults)
        .unwrap();
    assert_ne!(&out[..n], block.as_slice());
    let opt_out = dctx.decompress_block(&flipped, &mut out, &verify(false));
    assert!(opt_out.is_ok());

    // Asked for, it catches it.
    let n = dctx
        .decompress_block(&checked, &mut out, &verify(true))
        .unwrap();
    assert_eq!(&out[..n], block.as_slice());
    let r = dctx.decompress_block(&flipped, &mut out, &verify(true));
    assert!(matches!(r, Err(Error::BadChecksum)), "{r:?}");

    // Both sides must agree: the documented failure.
    assert!(
        dctx.decompress_block(&plain, &mut out, &verify(true))
            .is_err()
    );
}
