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

pub use zxc_sys::ZXC_DICT_SIZE_MAX;

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

    let ptrs: Vec<*const c_void> = samples.iter().map(|s| s.as_ptr() as *const c_void).collect();
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
    unsafe { zxc_sys::zxc_dict_id(content.as_ptr() as *const c_void, content.len()) }
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

/// Serializes raw dictionary content to the `.zxd` file format.
///
/// # Errors
///
/// Returns an [`Error`] if the content exceeds [`ZXC_DICT_SIZE_MAX`] or
/// serialization otherwise fails.
pub fn dict_save(content: &[u8]) -> Result<Vec<u8>> {
    let bound = unsafe { zxc_sys::zxc_dict_save_bound(content.len()) };
    let mut buf = vec![0u8; bound];
    let written = unsafe {
        zxc_sys::zxc_dict_save(
            content.as_ptr() as *const c_void,
            content.len(),
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

/// Parses a `.zxd` file buffer, returning owned dictionary content and its ID.
///
/// The C API hands back a pointer into the input buffer (zero-copy); this
/// wrapper copies the content into an owned [`Vec`] so the returned data is
/// independent of the input.
///
/// # Errors
///
/// Returns an [`Error`] if the buffer is not a valid `.zxd` file.
pub fn dict_load(zxd: &[u8]) -> Result<(Vec<u8>, u32)> {
    let mut content_ptr: *const c_void = std::ptr::null();
    let mut content_size: usize = 0;
    let mut id: u32 = 0;

    let rc = unsafe {
        zxc_sys::zxc_dict_load(
            zxd.as_ptr() as *const c_void,
            zxd.len(),
            &mut content_ptr,
            &mut content_size,
            &mut id,
        )
    };
    if rc != zxc_sys::ZXC_OK {
        return Err(error_from_code(rc as i64));
    }

    // The content pointer aliases into `zxd` (zero-copy); copy it out into an
    // owned buffer so the caller does not depend on `zxd` staying alive.
    let content = if content_ptr.is_null() || content_size == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(content_ptr as *const u8, content_size).to_vec() }
    };
    Ok((content, id))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        compress_with_options, decompress_with_options, CompressOptions, DecompressOptions,
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
        let zxd = dict_save(&dict).expect("dict_save failed");
        let id_zxd = dict_get_id(&zxd);

        assert_ne!(id_content, 0, "dict id must be non-zero");
        assert_eq!(id_content, id_archive, "content id == archive id");
        assert_eq!(id_content, id_zxd, "content id == .zxd id");
    }

    #[test]
    fn save_load_roundtrip() {
        let dict = trained();
        let zxd = dict_save(&dict).expect("dict_save failed");

        let (content, id) = dict_load(&zxd).expect("dict_load failed");
        assert_eq!(content, dict, "loaded content must equal original");
        assert_eq!(id, dict_id(&dict), "loaded id must match content id");
    }

    #[test]
    fn load_rejects_garbage() {
        let garbage = vec![0u8; 64];
        assert!(dict_load(&garbage).is_err());
    }
}
