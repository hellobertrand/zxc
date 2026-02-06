/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

//! Simple example demonstrating ZXC compression and decompression.
//!
//! Run with: `cargo run --example simple`

use zxc::{compress, decompress, decompressed_size, version_string, Level};

fn main() -> Result<(), zxc::Error> {
    println!("ZXC Rust Wrapper v{}\n", version_string());

    // Sample data with some repetition (compresses well)
    let original = b"Hello, ZXC! This is a demonstration of the Rust wrapper. \
                     Let's add some repetitive content to show compression: \
                     AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \
                     BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";

    println!("Original size: {} bytes", original.len());

    // Test all compression levels
    for level in Level::all() {
        let compressed = compress(original, *level, None)?;
        let ratio = (compressed.len() as f64 / original.len() as f64) * 100.0;

        println!(
            "  Level {:?}: {} bytes ({:.1}%)",
            level,
            compressed.len(),
            ratio
        );
    }

    // Full roundtrip demonstration
    println!("\nRoundtrip test:");
    let compressed = compress(original, Level::Default, None)?;

    // Query size before decompression
    let size = decompressed_size(&compressed).expect("valid compressed data");
    println!("  Reported decompressed size: {} bytes", size);

    let decompressed = decompress(&compressed)?;
    println!("  Actual decompressed size: {} bytes", decompressed.len());

    // Verify data integrity
    assert_eq!(&decompressed[..], &original[..]);
    println!("  ✓ Data integrity verified!");

    Ok(())
}
