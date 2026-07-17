/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

//! Build script for zxc-sys
//!
//! This script compiles the ZXC C library with Function Multi-Versioning (FMV)
//! to support runtime CPU feature detection and optimized code paths.
//!
//! On x86_64: Compiles `_default`, `_avx2`, and `_avx512` variants (SSE2 is
//! the x86-64 baseline, already active in `_default`).
//! On 32-bit ARM: `_default` and `_neon32` (NEON is optional there; the
//! dispatcher probes it at runtime).
//! Everywhere else (incl. AArch64, where NEON is baseline, and i686):
//! `_default` only.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};

/// Extract version constants from zxc_constants.h
fn extract_version(include_dir: &Path) -> (u32, u32, u32) {
    let header_path = include_dir.join("zxc_constants.h");
    let content = fs::read_to_string(&header_path).expect("Failed to read zxc_constants.h");

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
fn extract_compression_levels(include_dir: &Path) -> (i32, i32, i32, i32, i32, i32, i32) {
    let header_path = include_dir.join("zxc_constants.h");
    let content = fs::read_to_string(&header_path).expect("Failed to read zxc_constants.h");

    let mut fastest = None;
    let mut fast = None;
    let mut default = None;
    let mut balanced = None;
    let mut compact = None;
    let mut density = None;
    let mut ultra = None;

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
                    "ZXC_LEVEL_DENSITY" => density = value,
                    "ZXC_LEVEL_ULTRA" => ultra = value,
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
        density.expect("ZXC_LEVEL_DENSITY not found"),
        ultra.expect("ZXC_LEVEL_ULTRA not found"),
    )
}

/// Compiles one FMV variant of the three per-ISA translation units
/// (zxc_compress.c, zxc_decompress.c, zxc_huffman.c) with the given function
/// suffix and ISA flags.
fn compile_variant(include_dir: &Path, src_lib: &Path, suffix: &str, flags: &[&str]) {
    for unit in ["zxc_compress", "zxc_decompress", "zxc_huffman"] {
        let mut build = cc::Build::new();
        build
            .include(include_dir)
            .include(src_lib)
            .include(src_lib.join("vendors"))
            .define("ZXC_STATIC_DEFINE", None)
            .file(src_lib.join(format!("{unit}.c")))
            .define("ZXC_FUNCTION_SUFFIX", suffix)
            .opt_level(3)
            .warnings(false);
        for flag in flags {
            build.flag_if_supported(flag);
        }
        build.compile(&format!("{unit}{suffix}"));
    }
}

fn main() {
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
        let zxc_root = manifest_dir
            .join("../../..")
            .canonicalize()
            .expect("Failed to find ZXC root directory");
        (zxc_root.join("src/lib"), zxc_root.join("include"))
    };

    // Verify the headers exist: even the `system` feature needs them to
    // extract the version/level constants consumed by lib.rs's env!() calls.
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
    let (fastest, fast, default, balanced, compact, density, ultra) =
        extract_compression_levels(&include_dir);
    println!("cargo:rustc-env=ZXC_LEVEL_FASTEST={}", fastest);
    println!("cargo:rustc-env=ZXC_LEVEL_FAST={}", fast);
    println!("cargo:rustc-env=ZXC_LEVEL_DEFAULT={}", default);
    println!("cargo:rustc-env=ZXC_LEVEL_BALANCED={}", balanced);
    println!("cargo:rustc-env=ZXC_LEVEL_COMPACT={}", compact);
    println!("cargo:rustc-env=ZXC_LEVEL_DENSITY={}", density);
    println!("cargo:rustc-env=ZXC_LEVEL_ULTRA={}", ultra);

    // Use the system library instead of compiling from source
    if env::var("CARGO_FEATURE_SYSTEM").is_ok() {
        println!("cargo:rerun-if-env-changed=ZXC_LIB_DIR");
        if let Ok(dir) = env::var("ZXC_LIB_DIR") {
            println!("cargo:rustc-link-search=native={}", dir);
        }
        println!("cargo:rustc-link-lib=zxc");
        return;
    }

    assert!(
        src_lib.exists(),
        "ZXC source directory not found: {:?}",
        src_lib
    );

    let target = env::var("TARGET").unwrap_or_default();
    let is_x86_64 = target.contains("x86_64");
    let is_arm32 = (target.starts_with("arm") && !target.starts_with("arm64"))
        || target.starts_with("thumbv7");

    // =========================================================================
    // Core library files (common to all architectures)
    // =========================================================================
    let mut core_build = cc::Build::new();
    core_build
        .include(&include_dir)
        .include(&src_lib)
        .include(src_lib.join("vendors"))
        .define("ZXC_STATIC_DEFINE", None)
        .file(src_lib.join("zxc_common.c"))
        .file(src_lib.join("zxc_dict.c"))
        .file(src_lib.join("zxc_dispatch.c"))
        .file(src_lib.join("zxc_driver.c"))
        .file(src_lib.join("zxc_seekable.c"))
        .file(src_lib.join("zxc_pstream.c"))
        .opt_level(3)
        .warnings(false)
        .flag_if_supported("-pthread");

    core_build.compile("zxc_core");

    // =========================================================================
    // Function Multi-Versioning: Compile variants with different suffixes
    // =========================================================================

    // --- Default variant (baseline, always compiled) ---
    compile_variant(&include_dir, &src_lib, "_default", &[]);

    // --- Architecture-specific variants ---
    // The per-variant flags mirror zxc_add_variant() in CMakeLists.txt;
    // keep both in sync. MSVC ignores GCC-style -m flags silently, so it
    // needs its own /arch spellings plus the __BMI*__/__LZCNT__ macros the
    // sources test (cl.exe never defines them itself).
    let is_msvc = target.contains("msvc");
    if is_x86_64 && is_msvc {
        compile_variant(
            &include_dir,
            &src_lib,
            "_avx2",
            &["/arch:AVX2", "/D__BMI__", "/D__BMI2__", "/D__LZCNT__"],
        );
        compile_variant(
            &include_dir,
            &src_lib,
            "_avx512",
            &["/arch:AVX512", "/D__BMI__", "/D__BMI2__", "/D__LZCNT__"],
        );
    } else if is_x86_64 {
        compile_variant(
            &include_dir,
            &src_lib,
            "_avx2",
            &["-mavx2", "-mbmi", "-mbmi2", "-mlzcnt"],
        );
        compile_variant(
            &include_dir,
            &src_lib,
            "_avx512",
            &[
                "-mavx512f",
                "-mavx512bw",
                "-mavx512vbmi",
                "-mavx512vbmi2",
                "-mbmi",
                "-mbmi2",
                "-mlzcnt",
            ],
        );
    } else if is_arm32 {
        compile_variant(
            &include_dir,
            &src_lib,
            "_neon32",
            &["-march=armv7-a", "-mfpu=neon"],
        );
    }

    // PivCo tables: const data used only by the variant Huffman decoders, so it
    // must link LAST (zxc_core doesn't reference it, or the linker drops it).
    let mut pivco_tables = cc::Build::new();
    pivco_tables
        .include(&include_dir)
        .include(&src_lib)
        .include(src_lib.join("vendors"))
        .define("ZXC_STATIC_DEFINE", None)
        .file(src_lib.join("zxc_pivco_tables.c"))
        .opt_level(3)
        .warnings(false);
    pivco_tables.compile("zxc_pivco_tables");

    // Threading support (not needed on Windows, which uses kernel32)
    if !target.contains("windows") {
        println!("cargo:rustc-link-lib=pthread");
    }

    // Re-run build script if source files change
    println!("cargo:rerun-if-changed={}", src_lib.display());
    println!("cargo:rerun-if-changed={}", include_dir.display());
}
