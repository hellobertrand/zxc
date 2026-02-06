/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

//! Build script for zxc-sys
//!
//! This script compiles the ZXC C library with Function Multi-Versioning (FMV)
//! to support runtime CPU feature detection and optimized code paths.
//!
//! On ARM64: Compiles `_default` and `_neon` variants
//! On x86_64: Compiles `_default`, `_avx2`, and `_avx512` variants

use std::env;
use std::fs;
use std::path::{Path, PathBuf};

/// Extract version constants from zxc_constants.h
fn extract_version(include_dir: &Path) -> (u32, u32, u32) {
    let header_path = include_dir.join("zxc_constants.h");
    let content = fs::read_to_string(&header_path)
        .expect("Failed to read zxc_constants.h");
    
    let mut major = None;
    let mut minor = None;
    let mut patch = None;

    for line in content.lines() {
        let trimmed = line.trim();
        
        // Parse lines like: #define ZXC_VERSION_MAJOR 0
        if trimmed.starts_with("#define") {
            let parts: Vec<&str> = trimmed.split_whitespace().collect();
            if parts.len() >= 3 {
                match parts[1] {
                    "ZXC_VERSION_MAJOR" => major = parts[2].parse().ok(),
                    "ZXC_VERSION_MINOR" => minor = parts[2].parse().ok(),
                    "ZXC_VERSION_PATCH" => patch = parts[2].parse().ok(),
                    _ => {}
                }
            }
        }
    }

    (
        major.expect("ZXC_VERSION_MAJOR not found"),
        minor.expect("ZXC_VERSION_MINOR not found"),
        patch.expect("ZXC_VERSION_PATCH not found"),
    )
}

/// Extract compression level constants from zxc_constants.h
fn extract_compression_levels(include_dir: &Path) -> (i32, i32, i32, i32, i32) {
    let header_path = include_dir.join("zxc_constants.h");
    let content = fs::read_to_string(&header_path)
        .expect("Failed to read zxc_constants.h");

    let mut fastest = None;
    let mut fast = None;
    let mut default = None;
    let mut balanced = None;
    let mut compact = None;

    for line in content.lines() {
        let trimmed = line.trim();
        
        // Parse lines like: ZXC_LEVEL_FASTEST = 1,
        if trimmed.starts_with("ZXC_LEVEL_") {
            let parts: Vec<&str> = trimmed.split('=').collect();
            if parts.len() >= 2 {
                let name = parts[0].trim();
                // Extract number, removing comma and comments
                let value_str = parts[1].split(&[',', '/'][..]).next().unwrap().trim();
                let value: Option<i32> = value_str.parse().ok();
                
                match name {
                    "ZXC_LEVEL_FASTEST" => fastest = value,
                    "ZXC_LEVEL_FAST" => fast = value,
                    "ZXC_LEVEL_DEFAULT" => default = value,
                    "ZXC_LEVEL_BALANCED" => balanced = value,
                    "ZXC_LEVEL_COMPACT" => compact = value,
                    _ => {}
                }
            }
        }
    }

    (
        fastest.expect("ZXC_LEVEL_FASTEST not found"),
        fast.expect("ZXC_LEVEL_FAST not found"),
        default.expect("ZXC_LEVEL_DEFAULT not found"),
        balanced.expect("ZXC_LEVEL_BALANCED not found"),
        compact.expect("ZXC_LEVEL_COMPACT not found"),
    )
}

fn main() {
    // Check if we should use system library instead of compiling from source
    if env::var("CARGO_FEATURE_SYSTEM").is_ok() {
        println!("cargo:rustc-link-lib=zxc");
        return;
    }

    // Path to ZXC source files
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    
    // We use mirrored symlinks under zxc/ to preserve relative include paths
    // expected by the C source code (../../include/...).
    // During `cargo publish`, these symlinks are followed and real files are packaged.
    let src_lib = manifest_dir.join("zxc/src/lib");
    let include_dir = manifest_dir.join("zxc/include");

    // Fallback for local development if symlinks are broken or missing
    let (src_lib, include_dir) = if src_lib.exists() && include_dir.exists() {
        (src_lib, include_dir)
    } else {
        // Try direct path from workspace root
        let zxc_root = manifest_dir.join("../../..").canonicalize()
            .expect("Failed to find ZXC root directory");
        (zxc_root.join("src/lib"), zxc_root.join("include"))
    };

    // Verify paths exist
    assert!(
        src_lib.exists(),
        "ZXC source directory not found: {:?}",
        src_lib
    );
    assert!(
        include_dir.exists(),
        "ZXC include directory not found: {:?}",
        include_dir
    );

    // Extract version from header and make it available to lib.rs
    let (major, minor, patch) = extract_version(&include_dir);
    println!("cargo:rustc-env=ZXC_VERSION_MAJOR={}", major);
    println!("cargo:rustc-env=ZXC_VERSION_MINOR={}", minor);
    println!("cargo:rustc-env=ZXC_VERSION_PATCH={}", patch);

    // Extract compression levels from header and make them available to lib.rs
    let (fastest, fast, default, balanced, compact) = extract_compression_levels(&include_dir);
    println!("cargo:rustc-env=ZXC_LEVEL_FASTEST={}", fastest);
    println!("cargo:rustc-env=ZXC_LEVEL_FAST={}", fast);
    println!("cargo:rustc-env=ZXC_LEVEL_DEFAULT={}", default);
    println!("cargo:rustc-env=ZXC_LEVEL_BALANCED={}", balanced);
    println!("cargo:rustc-env=ZXC_LEVEL_COMPACT={}", compact);

    let target = env::var("TARGET").unwrap_or_default();
    let is_arm64 = target.contains("aarch64") || target.contains("arm64");
    let is_x86_64 = target.contains("x86_64") || target.contains("i686");

    // =========================================================================
    // Core library files (common to all architectures)
    // =========================================================================
    let mut core_build = cc::Build::new();
    core_build
        .include(&include_dir)
        .include(&src_lib)
        .file(src_lib.join("zxc_common.c"))
        .file(src_lib.join("zxc_driver.c"))
        .file(src_lib.join("zxc_dispatch.c"))
        .opt_level(3)
        .warnings(false)
        .flag_if_supported("-pthread");

    // =========================================================================
    // Function Multi-Versioning: Compile variants with different suffixes
    // =========================================================================

    // --- Default variant (baseline, always compiled) ---
    let mut default_compress = cc::Build::new();
    default_compress
        .include(&include_dir)
        .include(&src_lib)
        .file(src_lib.join("zxc_compress.c"))
        .define("ZXC_FUNCTION_SUFFIX", "_default")
        .opt_level(3)
        .warnings(false);

    let mut default_decompress = cc::Build::new();
    default_decompress
        .include(&include_dir)
        .include(&src_lib)
        .file(src_lib.join("zxc_decompress.c"))
        .define("ZXC_FUNCTION_SUFFIX", "_default")
        .opt_level(3)
        .warnings(false);

    // Compile defaults
    default_compress.compile("zxc_compress_default");
    default_decompress.compile("zxc_decompress_default");

    // --- Architecture-specific variants ---
    if is_arm64 {
        // NEON variant for ARM64
        let mut neon_compress = cc::Build::new();
        neon_compress
            .include(&include_dir)
            .include(&src_lib)
            .file(src_lib.join("zxc_compress.c"))
            .define("ZXC_FUNCTION_SUFFIX", "_neon")
            .flag_if_supported("-march=armv8-a+simd")
            .opt_level(3)
            .warnings(false);

        let mut neon_decompress = cc::Build::new();
        neon_decompress
            .include(&include_dir)
            .include(&src_lib)
            .file(src_lib.join("zxc_decompress.c"))
            .define("ZXC_FUNCTION_SUFFIX", "_neon")
            .flag_if_supported("-march=armv8-a+simd")
            .opt_level(3)
            .warnings(false);

        neon_compress.compile("zxc_compress_neon");
        neon_decompress.compile("zxc_decompress_neon");

        // Add ARM CRC extension for core build
        core_build.flag_if_supported("-march=armv8-a+crc");
    } else if is_x86_64 {
        // AVX2 variant
        let mut avx2_compress = cc::Build::new();
        avx2_compress
            .include(&include_dir)
            .include(&src_lib)
            .file(src_lib.join("zxc_compress.c"))
            .define("ZXC_FUNCTION_SUFFIX", "_avx2")
            .flag_if_supported("-mavx2")
            .flag_if_supported("-mfma")
            .flag_if_supported("-mbmi2")
            .opt_level(3)
            .warnings(false);

        let mut avx2_decompress = cc::Build::new();
        avx2_decompress
            .include(&include_dir)
            .include(&src_lib)
            .file(src_lib.join("zxc_decompress.c"))
            .define("ZXC_FUNCTION_SUFFIX", "_avx2")
            .flag_if_supported("-mavx2")
            .flag_if_supported("-mfma")
            .flag_if_supported("-mbmi2")
            .opt_level(3)
            .warnings(false);

        avx2_compress.compile("zxc_compress_avx2");
        avx2_decompress.compile("zxc_decompress_avx2");

        // AVX512 variant
        let mut avx512_compress = cc::Build::new();
        avx512_compress
            .include(&include_dir)
            .include(&src_lib)
            .file(src_lib.join("zxc_compress.c"))
            .define("ZXC_FUNCTION_SUFFIX", "_avx512")
            .flag_if_supported("-mavx512f")
            .flag_if_supported("-mavx512bw")
            .flag_if_supported("-mbmi2")
            .opt_level(3)
            .warnings(false);

        let mut avx512_decompress = cc::Build::new();
        avx512_decompress
            .include(&include_dir)
            .include(&src_lib)
            .file(src_lib.join("zxc_decompress.c"))
            .define("ZXC_FUNCTION_SUFFIX", "_avx512")
            .flag_if_supported("-mavx512f")
            .flag_if_supported("-mavx512bw")
            .flag_if_supported("-mbmi2")
            .opt_level(3)
            .warnings(false);

        avx512_compress.compile("zxc_compress_avx512");
        avx512_decompress.compile("zxc_decompress_avx512");

        // Add x86 extensions for core build
        core_build.flag_if_supported("-msse4.2");
        core_build.flag_if_supported("-mpclmul");
    }

    // Compile core library
    core_build.compile("zxc_core");

    // Threading support
    println!("cargo:rustc-link-lib=pthread");

    // Re-run build script if source files change
    println!("cargo:rerun-if-changed={}", src_lib.display());
    println!("cargo:rerun-if-changed={}", include_dir.display());
}
