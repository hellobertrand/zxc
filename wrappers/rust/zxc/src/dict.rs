/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! Pre-trained dictionary support.
//!
//! Dictionaries improve compression ratio on small, similar payloads by
//! prefilling the LZ77 window with representative byte sequences. Train one
//! from a corpus with [`train_dict`], then pass the raw content to
//! [`CompressOptions::with_dict`](crate::CompressOptions::with_dict) and the
//! matching
//! [`DecompressOptions::with_dict`](crate::DecompressOptions::with_dict).
//!
//! Dictionaries can also be serialized to the `.zxd` file format with
//! [`dict_save`] and parsed back with [`dict_load`]. Every dictionary has a
//! deterministic 32-bit ID ([`dict_id`]) that the ZXC archive header records,
//! so a decoder can verify the right dictionary was supplied.
//!
//! # Example
//!
//! ```rust
//! use zxc::{train_dict, dict_id, get_dict_id, CompressOptions, DecompressOptions,
//!           compress_with_options, decompress_with_options, ZXC_DICT_SIZE_MAX};
//!
//! let samples: Vec<Vec<u8>> = (0..8)
//!     .map(|i| format!("{{\"event\":\"login\",\"user\":{i},\"ok\":true}}").into_bytes())
//!     .collect();
//! let refs: Vec<&[u8]> = samples.iter().map(|s| s.as_slice()).collect();
//!
//! let dict = train_dict(&refs, ZXC_DICT_SIZE_MAX)?;
//! assert!(!dict.is_empty());
//!
//! let copts = CompressOptions::default().with_dict(dict.clone());
//! let archive = compress_with_options(&samples[0], &copts)?;
//! assert_eq!(dict_id(&dict), get_dict_id(&archive));
//!
//! let dopts = DecompressOptions::default().with_dict(dict);
//! let restored = decompress_with_options(&archive, &dopts)?;
//! assert_eq!(restored, samples[0]);
//! # Ok::<(), zxc::Error>(())
//! ```

use std::ffi::c_void;

pub use zxc_sys::{ZXC_DICT_SIZE_MAX, ZXC_HUF_TABLE_SIZE};

use crate::error::error_from_code;
use crate::{Error, Result};

/// Trains a dictionary from a corpus of samples.
///
/// `samples` should be several representative payloads of the kind you intend
/// to compress (e.g. similar JSON records). `max_size` caps the trained
/// dictionary's content size in bytes; pass [`ZXC_DICT_SIZE_MAX`] for the
/// largest allowed dictionary.
///
/// Returns the raw dictionary content, ready to hand to
/// [`CompressOptions::with_dict`](crate::CompressOptions::with_dict).
///
/// # Errors
///
/// Returns an [`Error`] if training fails (e.g. no usable samples).
pub fn train_dict(samples: &[&[u8]], max_size: usize) -> Result<Vec<u8>> {
    let cap = max_size.clamp(1, ZXC_DICT_SIZE_MAX);

    let ptrs: Vec<*const c_void> = samples
        .iter()
        .map(|s| s.as_ptr() as *const c_void)
        .collect();
    let sizes: Vec<usize> = samples.iter().map(|s| s.len()).collect();

    let mut buf = vec![0u8; cap];
    let written = unsafe {
        zxc_sys::zxc_train_dict(
            ptrs.as_ptr(),
            sizes.as_ptr(),
            samples.len(),
            buf.as_mut_ptr() as *mut c_void,
            buf.len(),
        )
    };
    if written < 0 {
        return Err(error_from_code(written));
    }
    if written == 0 {
        return Err(Error::InvalidData);
    }
    buf.truncate(written as usize);
    Ok(buf)
}

/// Computes the deterministic 32-bit dictionary ID for raw content.
///
/// Returns 0 for empty content.
pub fn dict_id(content: &[u8]) -> u32 {
    unsafe {
        zxc_sys::zxc_dict_id(
            content.as_ptr() as *const c_void,
            content.len(),
            std::ptr::null(),
        )
    }
}

/// Returns the dictionary ID a ZXC `.zxc` archive requires.
///
/// Returns 0 if the archive was produced without a dictionary, or if the
/// buffer is not a valid archive.
pub fn get_dict_id(archive: &[u8]) -> u32 {
    unsafe { zxc_sys::zxc_get_dict_id(archive.as_ptr() as *const c_void, archive.len()) }
}

/// Returns the dictionary ID stored in a `.zxd` file buffer.
///
/// Returns 0 if the buffer is not a valid `.zxd` file.
pub fn dict_get_id(zxd: &[u8]) -> u32 {
    unsafe { zxc_sys::zxc_dict_get_id(zxd.as_ptr() as *const c_void, zxd.len()) }
}

/// Trains the shared literal Huffman table for an already-trained dictionary.
///
/// Compresses the samples with `dict` and derives canonical Huffman code
/// lengths from the real post-LZ literal distribution. The returned 128-byte
/// packed table is required by [`dict_save`] and can be attached to
/// [`CompressOptions::with_dict_huf`](crate::CompressOptions::with_dict_huf) /
/// [`DecompressOptions::with_dict_huf`](crate::DecompressOptions::with_dict_huf).
///
/// # Errors
///
/// Returns an [`Error`] if training fails (e.g. no usable samples).
pub fn train_dict_huf(samples: &[&[u8]], dict: &[u8]) -> Result<[u8; ZXC_HUF_TABLE_SIZE]> {
    let ptrs: Vec<*const c_void> = samples
        .iter()
        .map(|s| s.as_ptr() as *const c_void)
        .collect();
    let sizes: Vec<usize> = samples.iter().map(|s| s.len()).collect();

    let mut huf = [0u8; ZXC_HUF_TABLE_SIZE];
    let rc = unsafe {
        zxc_sys::zxc_train_dict_huf(
            ptrs.as_ptr(),
            sizes.as_ptr(),
            samples.len(),
            dict.as_ptr() as *const c_void,
            dict.len(),
            huf.as_mut_ptr(),
        )
    };
    if rc != zxc_sys::ZXC_OK {
        return Err(error_from_code(rc as i64));
    }
    Ok(huf)
}

/// Serializes dictionary content and its shared Huffman table to the `.zxd`
/// file format.
///
/// `huf_lengths` is the mandatory 128-byte packed table from
/// [`train_dict_huf`]. The stored dict_id covers both content and table.
///
/// # Errors
///
/// Returns an [`Error`] if `huf_lengths` is not exactly
/// [`ZXC_HUF_TABLE_SIZE`] bytes, if the content exceeds
/// [`ZXC_DICT_SIZE_MAX`], or if serialization otherwise fails.
pub fn dict_save(content: &[u8], huf_lengths: &[u8]) -> Result<Vec<u8>> {
    if huf_lengths.len() != ZXC_HUF_TABLE_SIZE {
        return Err(Error::InvalidData);
    }
    let bound = unsafe { zxc_sys::zxc_dict_save_bound(content.len()) };
    let mut buf = vec![0u8; bound];
    let written = unsafe {
        zxc_sys::zxc_dict_save(
            content.as_ptr() as *const c_void,
            content.len(),
            huf_lengths.as_ptr() as *const c_void,
            buf.as_mut_ptr() as *mut c_void,
            buf.len(),
        )
    };
    if written < 0 {
        return Err(error_from_code(written));
    }
    buf.truncate(written as usize);
    Ok(buf)
}

/// Returns an owned copy of the shared Huffman table stored in a `.zxd`
/// buffer, or `None` if the buffer is not a valid `.zxd` file.
pub fn dict_huf(zxd: &[u8]) -> Option<[u8; ZXC_HUF_TABLE_SIZE]> {
    let p = unsafe { zxc_sys::zxc_dict_huf(zxd.as_ptr() as *const c_void, zxd.len()) };
    if p.is_null() {
        return None;
    }
    let mut huf = [0u8; ZXC_HUF_TABLE_SIZE];
    // The pointer aims into `zxd` (zero-copy); copy out for an owned result.
    huf.copy_from_slice(unsafe { std::slice::from_raw_parts(p as *const u8, ZXC_HUF_TABLE_SIZE) });
    Some(huf)
}

/// Parses a `.zxd` file buffer, returning owned dictionary content and its ID.
///
/// The C API hands back pointers into the input buffer (zero-copy); this
/// wrapper copies the content into an owned [`Vec`] so the returned data is
/// independent of the input. Prefer [`Dictionary::load`] for the full
/// (content, table, id) bundle.
///
/// # Errors
///
/// Returns an [`Error`] if the buffer is not a valid `.zxd` file.
pub fn dict_load(zxd: &[u8]) -> Result<(Vec<u8>, u32)> {
    let d = Dictionary::load(zxd)?;
    Ok((d.content, d.id))
}

/// A trained dictionary: the LZ-window content and its shared literal Huffman
/// table, bundled as one object so callers never juggle the pair by hand.
///
/// Create one with [`Dictionary::train`] (from samples) or
/// [`Dictionary::load`] (from `.zxd` bytes); attach it with
/// [`CompressOptions::with_dictionary`](crate::CompressOptions::with_dictionary) /
/// [`DecompressOptions::with_dictionary`](crate::DecompressOptions::with_dictionary).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Dictionary {
    content: Vec<u8>,
    huf: [u8; ZXC_HUF_TABLE_SIZE],
    id: u32,
}

impl Dictionary {
    /// Trains a complete dictionary (content + shared table) from samples.
    pub fn train(samples: &[&[u8]]) -> Result<Self> {
        let ptrs: Vec<*const c_void> = samples
            .iter()
            .map(|s| s.as_ptr() as *const c_void)
            .collect();
        let sizes: Vec<usize> = samples.iter().map(|s| s.len()).collect();

        let cap = unsafe { zxc_sys::zxc_dict_save_bound(ZXC_DICT_SIZE_MAX) };
        let mut zxd = vec![0u8; cap];
        let written = unsafe {
            zxc_sys::zxc_dict_train(
                ptrs.as_ptr(),
                sizes.as_ptr(),
                samples.len(),
                zxd.as_mut_ptr() as *mut c_void,
                zxd.len(),
            )
        };
        if written <= 0 {
            return Err(if written < 0 {
                error_from_code(written)
            } else {
                Error::InvalidData
            });
        }
        zxd.truncate(written as usize);
        Self::load(&zxd)
    }

    /// Parses `.zxd` bytes into an owned dictionary.
    pub fn load(zxd: &[u8]) -> Result<Self> {
        let mut content_ptr: *const c_void = std::ptr::null();
        let mut content_size: usize = 0;
        let mut huf_ptr: *const c_void = std::ptr::null();
        let mut id: u32 = 0;

        let rc = unsafe {
            zxc_sys::zxc_dict_load(
                zxd.as_ptr() as *const c_void,
                zxd.len(),
                &mut content_ptr,
                &mut content_size,
                &mut huf_ptr,
                &mut id,
            )
        };
        if rc != zxc_sys::ZXC_OK {
            return Err(error_from_code(rc as i64));
        }

        // The pointers alias into `zxd` (zero-copy); copy out into owned
        // buffers so the result does not depend on `zxd` staying alive.
        let content = if content_ptr.is_null() || content_size == 0 {
            Vec::new()
        } else {
            unsafe { std::slice::from_raw_parts(content_ptr as *const u8, content_size).to_vec() }
        };
        let mut huf = [0u8; ZXC_HUF_TABLE_SIZE];
        if !huf_ptr.is_null() {
            huf.copy_from_slice(unsafe {
                std::slice::from_raw_parts(huf_ptr as *const u8, ZXC_HUF_TABLE_SIZE)
            });
        }
        Ok(Self { content, huf, id })
    }

    /// Serializes this dictionary back to `.zxd` bytes.
    pub fn save(&self) -> Result<Vec<u8>> {
        dict_save(&self.content, &self.huf)
    }

    /// The dictionary ID binding the (content, table) pair, as recorded in
    /// `.zxd` files and archive headers.
    pub fn id(&self) -> u32 {
        self.id
    }

    /// The raw LZ-window content bytes.
    pub fn content(&self) -> &[u8] {
        &self.content
    }

    /// The 128-byte shared literal Huffman table.
    pub fn huf(&self) -> &[u8; ZXC_HUF_TABLE_SIZE] {
        &self.huf
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        CompressOptions, DecompressOptions, compress_with_options, decompress_with_options,
    };

    fn sample_corpus() -> Vec<Vec<u8>> {
        (0..16)
            .map(|i| {
                format!(
                    "{{\"event\":\"login\",\"user_id\":{i},\"status\":\"ok\",\"region\":\"eu-west\"}}"
                )
                .into_bytes()
            })
            .collect()
    }

    fn trained() -> Vec<u8> {
        let corpus = sample_corpus();
        let refs: Vec<&[u8]> = corpus.iter().map(|s| s.as_slice()).collect();
        train_dict(&refs, ZXC_DICT_SIZE_MAX).expect("train_dict failed")
    }

    #[test]
    fn train_produces_nonempty() {
        let dict = trained();
        assert!(!dict.is_empty(), "trained dictionary must be non-empty");
        assert!(dict.len() <= ZXC_DICT_SIZE_MAX);
    }

    #[test]
    fn compress_decompress_with_dict_roundtrips() {
        let dict = trained();
        let sample = sample_corpus()[3].clone();

        let copts = CompressOptions::default().with_dict(dict.clone());
        let archive = compress_with_options(&sample, &copts).expect("compress with dict");

        let dopts = DecompressOptions {
            verify_checksum: true,
            dict: Some(dict),
            dict_huf: None,
        };
        let restored = decompress_with_options(&archive, &dopts).expect("decompress with dict");
        assert_eq!(restored, sample);
    }

    #[test]
    fn decompress_without_dict_fails() {
        let dict = trained();
        let sample = sample_corpus()[5].clone();

        let copts = CompressOptions::default().with_dict(dict);
        let archive = compress_with_options(&sample, &copts).expect("compress with dict");

        // No dict supplied: the decoder must refuse rather than silently
        // produce wrong output.
        let res = decompress_with_options(&archive, &DecompressOptions::default());
        assert!(res.is_err(), "decompress without dict must fail");
    }

    #[test]
    fn ids_are_consistent() {
        let dict = trained();
        let sample = sample_corpus()[0].clone();

        let copts = CompressOptions::default().with_dict(dict.clone());
        let archive = compress_with_options(&sample, &copts).expect("compress with dict");

        let id_content = dict_id(&dict);
        let id_archive = get_dict_id(&archive);

        assert_ne!(id_content, 0, "dict id must be non-zero");
        assert_eq!(id_content, id_archive, "content id == archive id");

        /* The .zxd id binds (content, table): it differs from the content id. */
        let corpus = sample_corpus();
        let refs: Vec<&[u8]> = corpus.iter().map(|s| s.as_slice()).collect();
        let huf = train_dict_huf(&refs, &dict).expect("train_dict_huf failed");
        let zxd = dict_save(&dict, &huf).expect("dict_save failed");
        let id_zxd = dict_get_id(&zxd);
        assert_ne!(id_zxd, 0, ".zxd id must be non-zero");
        assert_ne!(id_content, id_zxd, ".zxd id covers the table");
    }

    #[test]
    fn save_load_roundtrip() {
        let dict = trained();
        let corpus = sample_corpus();
        let refs: Vec<&[u8]> = corpus.iter().map(|s| s.as_slice()).collect();
        let huf = train_dict_huf(&refs, &dict).expect("train_dict_huf failed");
        let zxd = dict_save(&dict, &huf).expect("dict_save failed");

        let (content, id) = dict_load(&zxd).expect("dict_load failed");
        assert_eq!(content, dict, "loaded content must equal original");
        assert_eq!(id, dict_get_id(&zxd), "loaded id must match stored id");
        assert_eq!(
            dict_huf(&zxd).expect("table must be present"),
            huf,
            "loaded table must equal original"
        );
    }

    #[test]
    fn load_rejects_garbage() {
        let garbage = vec![0u8; 64];
        assert!(dict_load(&garbage).is_err());
    }

    #[test]
    fn dictionary_object_roundtrip() {
        let corpus = sample_corpus();
        let refs: Vec<&[u8]> = corpus.iter().map(|s| s.as_slice()).collect();

        // One-call train, save/load, and the bundle matches the primitives.
        let d = Dictionary::train(&refs).expect("Dictionary::train failed");
        assert!(!d.content().is_empty());
        assert_ne!(d.id(), 0);
        let zxd = d.save().expect("save failed");
        let d2 = Dictionary::load(&zxd).expect("Dictionary::load failed");
        assert_eq!(d, d2);
        assert_eq!(d.id(), dict_get_id(&zxd));

        // Options take the bundle in one call; round-trip + id binding.
        let sample = corpus[3].clone();
        let copts = crate::CompressOptions::with_level(crate::Level::Density).with_dictionary(&d);
        let archive = compress_with_options(&sample, &copts).expect("compress");
        let dopts = DecompressOptions::default().with_dictionary(&d);
        let restored = decompress_with_options(&archive, &dopts).expect("decompress");
        assert_eq!(restored, sample);

        // Without the table (content only), the id no longer matches.
        let bare = DecompressOptions::default().with_dict(d.content().to_vec());
        assert!(decompress_with_options(&archive, &bare).is_err());
    }
}
