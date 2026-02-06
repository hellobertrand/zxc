/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

//! Example demonstrating file-based streaming compression and decompression.
//!
//! Run with: `cargo run --example file_compression`

use std::fs;
use std::io::Write;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("ZXC File Streaming Example\n");

    // Create a test file with compressible data
    let test_data: Vec<u8> = (0..1024 * 1024) // 1 MB
        .map(|i| ((i % 256) ^ ((i / 256) % 256)) as u8)
        .collect();

    let input_path = "/tmp/zxc_test_input.bin";
    let compressed_path = "/tmp/zxc_test_compressed.zxc";
    let output_path = "/tmp/zxc_test_output.bin";

    // Write test data to file
    {
        let mut file = fs::File::create(input_path)?;
        file.write_all(&test_data)?;
    }

    // Get original file size
    let original_size = fs::metadata(input_path)?.len();
    println!("Original file size: {} bytes", original_size);

    // Compress the file with multi-threading
    println!("\nCompressing with auto-detected threads...");
    let compressed_bytes = zxc::compress_file(
        input_path,
        compressed_path,
        zxc::Level::Default,
        None, // Auto-detect CPU cores
        None, // Maximum performance (no checksum)
    )?;
    println!("  Compressed bytes written: {}", compressed_bytes);

    // Get compressed file size
    let compressed_size = fs::metadata(compressed_path)?.len();
    let ratio = 100.0 * compressed_size as f64 / original_size as f64;
    println!("  Compressed file size: {} bytes ({:.1}%)", compressed_size, ratio);

    // Query the decompressed size from the file
    let reported_size = zxc::file_decompressed_size(compressed_path)?;
    println!("\n  Reported decompressed size: {} bytes", reported_size);

    // Decompress the file
    println!("\nDecompressing...");
    let decompressed_bytes = zxc::decompress_file(
        compressed_path,
        output_path,
        Some(4), // Use 4 threads
    )?;
    println!("  Decompressed bytes written: {}", decompressed_bytes);

    // Verify the result
    let output_data = fs::read(output_path)?;
    if output_data == test_data {
        println!("\n✓ Data integrity verified! Files match.");
    } else {
        println!("\n✗ ERROR: Data mismatch!");
    }

    // Cleanup
    fs::remove_file(input_path)?;
    fs::remove_file(compressed_path)?;
    fs::remove_file(output_path)?;

    println!("\nDone.");
    Ok(())
}
