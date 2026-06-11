/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! One-shot compress / decompress entry points and library version helpers.

use std::ffi::c_void;

use zxc_sys::{ZXC_VERSION_MAJOR, ZXC_VERSION_MINOR, ZXC_VERSION_PATCH};

use crate::error::error_from_code;
use crate::{CompressOptions, DecompressOptions, Error, Level, Result};

/// Returns the maximum compressed size for an input of the given size.
///
/// Use this to allocate a buffer before calling [`compress_to`].
///
/// # Example
///
/// ```rust
/// let bound = zxc::compress_bound(1024);
/// assert!(bound > 1024); // Accounts for headers and worst-case expansion
/// ```
#[inline]
pub fn compress_bound(input_size: usize) -> u64 {
    unsafe { zxc_sys::zxc_compress_bound(input_size) }
}

/// Compresses data with the specified level.
///
/// This is a convenience function that allocates the output buffer automatically.
/// For zero-allocation usage, see [`compress_to`].
///
/// # Arguments
///
/// * `data` - The data to compress
/// * `level` - Compression level
/// * `checksum` - Optional checksum for data integrity (`None` = disabled for maximum performance)
///
/// # Example
///
/// ```rust
/// use zxc::{compress, Level};
///
/// let data = b"Hello, world!";
///
/// // Maximum performance (no checksum)
/// let compressed = compress(data, Level::Default, None)?;
///
/// // With data integrity verification
/// let compressed = compress(data, Level::Default, Some(true))?;
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn compress(data: &[u8], level: Level, checksum: Option<bool>) -> Result<Vec<u8>> {
    let opts = CompressOptions {
        level,
        checksum: checksum.unwrap_or(false),
        seekable: false,
        dict: None,
        dict_huf: None,
    };
    compress_with_options(data, &opts)
}

/// Compresses data with full options control.
///
/// # Example
///
/// ```rust
/// use zxc::{compress_with_options, CompressOptions, Level};
///
/// let data = b"Hello, world!";
/// let opts = CompressOptions::with_level(Level::Compact).without_checksum();
/// let compressed = compress_with_options(data, &opts)?;
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn compress_with_options(data: &[u8], options: &CompressOptions) -> Result<Vec<u8>> {
    let bound = compress_bound(data.len()) as usize;
    let mut output = Vec::with_capacity(bound);

    let written =
        unsafe { impl_compress(data, output.as_mut_ptr(), output.capacity(), options)? };

    unsafe {
        output.set_len(written);
    }
    Ok(output)
}

/// Helper to handle the raw compression call.
///
/// # Safety
///
/// `dst_ptr` must be valid for writes up to `dst_cap` bytes.
#[inline(always)]
unsafe fn impl_compress(
    data: &[u8],
    dst_ptr: *mut u8,
    dst_cap: usize,
    options: &CompressOptions,
) -> Result<usize> {
    let written = unsafe {
        let (dict_ptr, dict_size) = match &options.dict {
            Some(d) if !d.is_empty() => (d.as_ptr() as *const c_void, d.len()),
            _ => (std::ptr::null(), 0),
        };
        let dict_huf_ptr = match &options.dict_huf {
            Some(h) if !h.is_empty() => h.as_ptr() as *const c_void,
            _ => std::ptr::null(),
        };
        let copts = zxc_sys::zxc_compress_opts_t {
            level: options.level as i32,
            checksum_enabled: options.checksum as i32,
            seekable: options.seekable as i32,
            dict: dict_ptr,
            dict_size,
            dict_huf: dict_huf_ptr,
            ..Default::default()
        };
        zxc_sys::zxc_compress(
            data.as_ptr() as *const c_void,
            data.len(),
            dst_ptr as *mut c_void,
            dst_cap,
            &copts,
        )
    };

    if written < 0 {
        return Err(error_from_code(written));
    }

    if written == 0 && !data.is_empty() {
        return Err(Error::InvalidData);
    }

    Ok(written as usize)
}

/// Compresses data into a pre-allocated buffer.
///
/// Returns the number of bytes written to `output`.
///
/// # Errors
///
/// Returns an [`Error`] if the output buffer is too small or an internal
/// error occurs.
///
/// # Example
///
/// ```rust
/// use zxc::{compress_to, compress_bound, CompressOptions};
///
/// let data = b"Hello, world!";
/// let mut output = vec![0u8; compress_bound(data.len()) as usize];
/// let size = compress_to(data, &mut output, &CompressOptions::default())?;
/// output.truncate(size);
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn compress_to(data: &[u8], output: &mut [u8], options: &CompressOptions) -> Result<usize> {
    unsafe { impl_compress(data, output.as_mut_ptr(), output.len(), options) }
}

/// Returns the original uncompressed size from compressed data.
///
/// This reads the footer without performing decompression.
/// Returns `None` if the data is invalid or truncated.
///
/// # Example
///
/// ```rust
/// use zxc::{compress, decompressed_size, Level};
///
/// let data = b"Hello, world!";
/// let compressed = compress(data, Level::Default, None)?;
/// let size = decompressed_size(&compressed);
/// assert_eq!(size, Some(data.len() as u64));
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn decompressed_size(compressed: &[u8]) -> Option<u64> {
    let size = unsafe {
        zxc_sys::zxc_get_decompressed_size(compressed.as_ptr() as *const c_void, compressed.len())
    };

    if size == 0 && !compressed.is_empty() {
        None
    } else {
        Some(size)
    }
}

/// Decompresses ZXC-compressed data.
///
/// This is a convenience function that queries the output size and allocates
/// the buffer automatically. For zero-allocation usage, see [`decompress_to`].
///
/// # Example
///
/// ```rust
/// use zxc::{compress, decompress, Level};
///
/// let data = b"Hello, world!";
/// let compressed = compress(data, Level::Default, None)?;
/// let decompressed = decompress(&compressed)?;
/// assert_eq!(&decompressed[..], &data[..]);
/// # Ok::<(), zxc::Error>(())
/// ```
pub fn decompress(compressed: &[u8]) -> Result<Vec<u8>> {
    decompress_with_options(compressed, &DecompressOptions::default())
}

/// Decompresses data with full options control.
pub fn decompress_with_options(
    compressed: &[u8],
    options: &DecompressOptions,
) -> Result<Vec<u8>> {
    // `decompressed_size` returns None for an ambiguous 0 (a valid empty-payload
    // archive or invalid input); fall back to 0 and let the C decoder validate
    // the frame (it returns a negative error code on genuinely corrupt input).
    let size = decompressed_size(compressed).unwrap_or(0) as usize;
    let mut output = Vec::with_capacity(size);

    let written =
        unsafe { impl_decompress(compressed, output.as_mut_ptr(), output.capacity(), options)? };

    if written != size {
        return Err(Error::InvalidData);
    }

    unsafe {
        output.set_len(written);
    }
    Ok(output)
}

/// Helper to handle the raw decompression call.
///
/// # Safety
///
/// `dst_ptr` must be valid for writes up to `dst_cap` bytes.
#[inline(always)]
unsafe fn impl_decompress(
    compressed: &[u8],
    dst_ptr: *mut u8,
    dst_cap: usize,
    options: &DecompressOptions,
) -> Result<usize> {
    let written = unsafe {
        let (dict_ptr, dict_size) = match &options.dict {
            Some(d) if !d.is_empty() => (d.as_ptr() as *const c_void, d.len()),
            _ => (std::ptr::null(), 0),
        };
        let dict_huf_ptr = match &options.dict_huf {
            Some(h) if !h.is_empty() => h.as_ptr() as *const c_void,
            _ => std::ptr::null(),
        };
        let dopts = zxc_sys::zxc_decompress_opts_t {
            checksum_enabled: if options.verify_checksum { 1 } else { 0 },
            dict: dict_ptr,
            dict_size,
            dict_huf: dict_huf_ptr,
            ..Default::default()
        };
        zxc_sys::zxc_decompress(
            compressed.as_ptr() as *const c_void,
            compressed.len(),
            dst_ptr as *mut c_void,
            dst_cap,
            &dopts,
        )
    };

    if written < 0 {
        return Err(error_from_code(written));
    }

    // A non-negative return is a success: `written == 0` is valid (empty payload).
    Ok(written as usize)
}

/// Decompresses data into a pre-allocated buffer.
///
/// Returns the number of bytes written to `output`.
///
/// # Errors
///
/// Returns an error if decompression fails due to invalid data, corruption,
/// or insufficient output buffer size.
pub fn decompress_to(
    compressed: &[u8],
    output: &mut [u8],
    options: &DecompressOptions,
) -> Result<usize> {
    unsafe { impl_decompress(compressed, output.as_mut_ptr(), output.len(), options) }
}

/// Returns the library version as a tuple (major, minor, patch).
pub fn version() -> (u32, u32, u32) {
    (ZXC_VERSION_MAJOR, ZXC_VERSION_MINOR, ZXC_VERSION_PATCH)
}

/// Returns the library version as a string.
///
/// This value is the compile-time version baked into the `zxc-sys` crate.
/// For the runtime-linked library version (useful to detect a mismatched
/// shared library), call [`runtime_version`].
pub fn version_string() -> String {
    format!(
        "{}.{}.{}",
        ZXC_VERSION_MAJOR, ZXC_VERSION_MINOR, ZXC_VERSION_PATCH
    )
}

/// Returns the version string reported by the linked native library
/// (e.g. `"0.10.0"`). Useful for verifying that the dynamically-linked
/// libzxc matches the version `zxc-sys` was built against.
pub fn runtime_version() -> &'static str {
    unsafe {
        let ptr = zxc_sys::zxc_version_string();
        std::ffi::CStr::from_ptr(ptr).to_str().unwrap_or("")
    }
}

/// Returns the minimum supported compression level (currently `1`).
///
/// Equivalent to [`Level::Fastest`] as an integer.
pub fn min_level() -> i32 {
    unsafe { zxc_sys::zxc_min_level() }
}

/// Returns the maximum supported compression level (currently `5`).
///
/// Equivalent to [`Level::Compact`] as an integer.
pub fn max_level() -> i32 {
    unsafe { zxc_sys::zxc_max_level() }
}

/// Returns the default compression level (currently `3`).
///
/// Equivalent to [`Level::Default`] as an integer.
pub fn default_level() -> i32 {
    unsafe { zxc_sys::zxc_default_level() }
}

#[cfg(test)]
mod tests {
    use crate::*;

    #[test]
    fn test_roundtrip() {
        let data = b"Hello, ZXC! This is a test of the safe Rust wrapper.";
        let compressed = compress(data, Level::Default, None).unwrap();
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn test_all_levels() {
        let data = b"Test data with repetition: CCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";

        for level in Level::all() {
            let compressed = compress(data, *level, None).unwrap();
            let decompressed = decompress(&compressed).unwrap();
            assert_eq!(
                &decompressed[..],
                &data[..],
                "Roundtrip failed at level {:?}",
                level
            );
        }
    }

    #[test]
    fn test_empty() {
        // Empty input is valid: it produces a well-formed (header + EOF + footer)
        // archive that round-trips back to empty.
        let data: &[u8] = b"";
        let compressed = compress(data, Level::Default, None).expect("compress empty");
        let decompressed = decompress(&compressed).expect("decompress empty");
        assert!(
            decompressed.is_empty(),
            "empty data must round-trip to empty"
        );
    }

    #[test]
    fn test_checksum_options() {
        let data = b"Test with and without checksum";

        // With checksum
        let opts_with = CompressOptions::with_level(Level::Default);
        let compressed = compress_with_options(data, &opts_with).unwrap();
        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(&decompressed[..], &data[..]);

        // Without checksum
        let opts_without = CompressOptions::with_level(Level::Fast).without_checksum();
        let compressed = compress_with_options(data, &opts_without).unwrap();
        let decompressed =
            decompress_with_options(&compressed, &DecompressOptions::skip_checksum()).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn test_decompressed_size() {
        let data = b"Hello, world! Testing decompressed_size function.";
        let compressed = compress(data, Level::Default, None).unwrap();
        let size = decompressed_size(&compressed);
        assert_eq!(size, Some(data.len() as u64));
    }

    #[test]
    fn test_version() {
        let (major, minor, patch) = version();

        let expected = format!("{}.{}.{}", major, minor, patch);
        assert_eq!(version_string(), expected);

        let cargo_version = env!("CARGO_PKG_VERSION");
        assert_eq!(expected, cargo_version);
    }

    #[test]
    fn test_invalid_data() {
        let garbage = b"not valid zxc data";
        let result = decompress(garbage);
        assert!(result.is_err());
    }

    #[test]
    fn test_specific_error_codes() {
        // Test invalid magic - size detection should fail first and return InvalidData
        let invalid_magic = b"INVALID_DATA_NOT_ZXC";
        let result = decompress(invalid_magic);
        assert!(result.is_err(), "Should fail on invalid data");

        // Test truncated data - must fail (the exact error code is a decoder
        // implementation detail, so we only require that it errors).
        let data = b"Hello, world! Testing error codes with enough data to compress well.";
        let compressed = compress(data, Level::Default, None).unwrap();
        let truncated = &compressed[..10]; // Too short to be valid
        assert!(
            decompress(truncated).is_err(),
            "Truncated data must fail to decompress"
        );
    }

    #[test]
    fn test_error_messages() {
        // Verify error messages are descriptive
        let errors = vec![
            (Error::Memory, "memory allocation failed"),
            (Error::BadChecksum, "checksum verification failed"),
            (Error::CorruptData, "corrupted compressed data"),
            (Error::DstTooSmall, "destination buffer too small"),
        ];

        for (error, expected_msg) in errors {
            let msg = error.to_string();
            assert!(
                msg.contains(expected_msg),
                "Error message '{}' should contain '{}'",
                msg,
                expected_msg
            );
        }
    }

    #[test]
    fn test_compress_to_buffer() {
        let data = b"Testing compress_to with pre-allocated buffer";
        let mut output = vec![0u8; compress_bound(data.len()) as usize];

        let size = compress_to(data, &mut output, &CompressOptions::default()).unwrap();
        output.truncate(size);

        let decompressed = decompress(&output).unwrap();
        assert_eq!(&decompressed[..], &data[..]);
    }

    #[test]
    fn test_large_data() {
        // 1 MB of random-ish but compressible data
        let data: Vec<u8> = (0..1024 * 1024)
            .map(|i| ((i % 256) ^ ((i / 256) % 256)) as u8)
            .collect();

        let compressed = compress(&data, Level::Default, None).unwrap();
        assert!(compressed.len() < data.len()); // Should compress

        let decompressed = decompress(&compressed).unwrap();
        assert_eq!(decompressed, data);
    }
}
