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
use std::path::PathBuf;

fn main() {
    // Check if we should use system library instead of compiling from source
    if env::var("CARGO_FEATURE_SYSTEM").is_ok() {
        println!("cargo:rustc-link-lib=zxc");
        return;
    }

    // Path to ZXC source files (relative to wrappers/rust/zxc-sys/)
    let zxc_root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../../..")
        .canonicalize()
        .expect("Failed to find ZXC root directory");

    let src_lib = zxc_root.join("src/lib");
    let include_dir = zxc_root.join("include");

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
