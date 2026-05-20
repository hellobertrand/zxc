# GitHub Actions Workflows

This directory contains CI/CD workflows for the ZXC compression library.

## Core Workflows

### build.yml - Build & Release
**Triggers:** Push to main, tags, pull requests, manual dispatch

Builds and tests ZXC across multiple platforms (Linux x86_64/ARM64, macOS ARM64, Windows x64). Generates release artifacts and uploads binaries when tags are pushed.

### multiarch.yml - Multi-Architecture Build
**Triggers:** Push to main, pull requests, manual dispatch

Comprehensive build matrix testing across multiple architectures including 32-bit and 64-bit variants for Linux (x64, x86, ARM64, ARM) and Windows (x64, x86). Validates compilation compatibility across different platforms.

### multicomp.yml - Compiler Compatibility
**Triggers:** Push to main, pull requests, manual dispatch

Tests the codebase against a wide range of compilers (various versions of GCC and Clang) to ensure compatibility and identify any compiler-specific issues or warnings.

### benchmark.yml - Performance Benchmark
**Triggers:** Push to main (src changes), pull requests, manual dispatch

Runs performance benchmarks using LZbench on Ubuntu and macOS. Integrates ZXC into the LZbench framework and tests compression/decompression performance against the Silesia corpus.

## Quality & Security

### coverage.yml - Code Coverage
**Triggers:** Push to main, pull requests, manual dispatch

Builds the project with coverage instrumentation (`-DZXC_ENABLE_COVERAGE=ON`), runs unit and CLI tests, and generates a coverage report using `lcov`. The report is then uploaded to Codecov for analysis.

### fuzzing.yml - Fuzz Testing
**Triggers:** Pull requests, scheduled (every 3 days), manual dispatch

Executes fuzz testing using ClusterFuzzLite with multiple sanitizers (address, undefined) on decompression and roundtrip fuzzers. Helps identify memory safety issues and edge cases.

### quality.yml - Code Quality
**Triggers:** Push to main, pull requests, manual dispatch

Performs static analysis using Cppcheck and Clang Static Analyzer. Runs memory leak detection with Valgrind to ensure code quality and identify potential bugs.

### security.yml - Code Security
**Triggers:** Push to main, pull requests

Runs CodeQL security analysis to detect potential security vulnerabilities and coding errors in the C/C++ codebase.

### abi-check.yml - ABI Stability Check
**Triggers:** Pull requests (lib/header changes), push to main, manual dispatch

Builds `libzxc.so` with debug info, generates an ABI XML via [`abidw`](https://sourceware.org/libabigail/), and compares it against the committed baseline at [`docs/abi/libzxc-linux-x86_64.abi.xml`](../../docs/abi/libzxc-linux-x86_64.abi.xml) using `abidiff --no-added-syms`. Adding new symbols passes (MINOR bump); removing or changing existing symbols fails (MAJOR bump required + regenerate baseline). Run with `mode=regenerate` to produce a fresh baseline as a downloadable artifact.

### scorecard.yml - OSSF Scorecard
**Triggers:** Push to main, scheduled (weekly), manual dispatch

Runs the [OSSF Scorecard](https://github.com/ossf/scorecard) analysis to evaluate the project against open source security best practices (branch protection, signed releases, dependency pinning, etc.). Results are published to the OpenSSF public dashboard and uploaded to GitHub's code scanning view as SARIF.

### vendors.yml - Vendor Maintenance
**Triggers:** Scheduled (weekly), manual dispatch

Automatically checks for and updates third-party dependencies (like `rapidhash.h`) to ensure the project uses the latest stable versions of its vendors.

### changelog.yml - Generate CHANGELOG
**Triggers:** Push to `bump/**` or `release/**` branches, manual dispatch

Regenerates [`CHANGELOG.md`](../../CHANGELOG.md) with [`git-cliff`](https://git-cliff.org/), grouping all commits under their respective tags. On `bump/vX.Y.Z` and `release/vX.Y.Z` branches the version tag is auto-detected from the branch name; manual dispatch accepts an explicit `tag` input (empty = `Unreleased`). The generated file is uploaded as a workflow artifact, it is **not** committed back to the repo. Configuration lives in [`cliff.toml`](../../cliff.toml).

### rfc-draft.yml - Build RFC Draft
**Triggers:** Push to main (docs/FORMAT.md changes), pull requests, manual dispatch

Renders the kramdown-rfc source at [`docs/FORMAT.md`](../../docs/FORMAT.md) into IETF-publishable `.xml`, `.txt`, `.html`, and `.pdf` outputs using [`kramdown-rfc`](https://github.com/cabo/kramdown-rfc) and [`xml2rfc`](https://github.com/ietf-tools/xml2rfc). Artifacts are uploaded as `draft-lebonnois-zxc-format-00.*` for 30 days. The canonical `.txt` is what gets submitted to the IETF Datatracker.

## Language Bindings

### wrapper-rust-publish.yml - Publish Rust Crates
**Triggers:** Release published, manual dispatch

Tests and publishes Rust crates to crates.io. Verifies the version matches the release tag, runs tests across platforms, and publishes `zxc-compress-sys` (FFI bindings) followed by `zxc-compress` (safe wrapper).

### wrapper-python-publish.yml - Publish Python Package
**Triggers:** Release published, manual dispatch

Builds platform-specific wheels using `cibuildwheel` for Linux (x86_64, ARM64), macOS (ARM64, Intel), and Windows (AMD64, ARM64). Tests wheels against Python 3.12-3.13, then publishes to PyPI via trusted publishing.

### wrapper-wasm.yml - WASM Build & Test
**Triggers:** Release published, publish on main, manual dispatch

Builds the WebAssembly target using Emscripten SDK. Compiles the library with SIMD disabled (scalar codepath) and no threading, then runs a Node.js roundtrip test suite covering all compression levels, reusable contexts, and error handling. Uploads `zxc.js` + `zxc.wasm` as build artifacts.

### wrapper-nodejs-publish.yml - Publish Node.js Package
**Triggers:** Release published, manual dispatch

Builds and publishes the Node.js package to npm. Handles the compilation of native bindings and ensures the package is correctly versioned and distributed.

### wrapper-go-test.yml - Test Go Package
**Triggers:** Release published, manual dispatch

Runs comprehensive tests for the Go bindings across various platforms and architectures to ensure the Go package is stable and functional.
