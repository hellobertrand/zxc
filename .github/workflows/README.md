# GitHub Actions Workflows

This directory contains CI/CD workflows for the ZXC compression library.

## Workflows

### build.yml - Build & Release
**Triggers:** Push to main, tags, pull requests, manual dispatch

Builds and tests ZXC across multiple platforms (Linux x86_64/ARM64, macOS ARM64, Windows x64). Generates release artifacts and uploads binaries when tags are pushed.

### build_all.yml - Multi-Architecture Build
**Triggers:** Push to main, pull requests, manual dispatch

Comprehensive build matrix testing across multiple architectures including 32-bit and 64-bit variants for Linux (x64, x86, ARM64, ARM) and Windows (x64, x86). Validates compilation compatibility across different platforms.

### benchmark.yml - Performance Benchmark
**Triggers:** Push to main (src changes), pull requests, manual dispatch

Runs performance benchmarks using LZbench on Ubuntu and macOS. Integrates ZXC into the LZbench framework and tests compression/decompression performance against the Silesia corpus.

### coverage.yml - Code Coverage
**Triggers:** Push to main, pull requests, manual dispatch

Builds the project with coverage instrumentation (`-DZXC_ENABLE_COVERAGE=ON`), runs unit and CLI tests, and generates a coverage report using `lcov`. The report is then uploaded to Codecov for analysis.

### fuzzing.yml - Fuzz Testing
**Triggers:** Pull requests, scheduled (every 3 days), manual dispatch

Executes fuzz testing using ClusterFuzzLite with multiple sanitizers (address, undefined) on decompression and roundtrip fuzzers. Helps identify memory safety issues and edge cases.

### quality.yml - Code Quality
**Triggers:** Push to main, pull requests, manual dispatch

Performs static analysis using Cppcheck and Clang Static Analyzer. Runs memory leak detection with Valgrind to ensure code quality and identify potential bugs.

### wrapper-rust-publish.yml - Publish Rust Crates
**Triggers:** Release published, manual dispatch

Tests and publishes Rust crates to crates.io. Verifies the version matches the release tag, runs tests across platforms, and publishes `zxc-compress-sys` (FFI bindings) followed by `zxc-compress` (safe wrapper).

### wrapper-python-publish.yml - Publish Python Package
**Triggers:** Release published, manual dispatch

Builds platform-specific wheels using `cibuildwheel` for Linux (x86_64, ARM64), macOS (ARM64, Intel), and Windows (AMD64, ARM64). Tests wheels against Python 3.12-3.13, then publishes to PyPI via trusted publishing.

### security.yml - Code Security
**Triggers:** Push to main, pull requests

Runs CodeQL security analysis to detect potential security vulnerabilities and coding errors in the C/C++ codebase.
