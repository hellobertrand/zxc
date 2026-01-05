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

### fuzzing.yml - Fuzz Testing
**Triggers:** Pull requests, scheduled (every 3 days), manual dispatch

Executes fuzz testing using ClusterFuzzLite with multiple sanitizers (address, undefined) on decompression and roundtrip fuzzers. Helps identify memory safety issues and edge cases.

### quality.yml - Code Quality
**Triggers:** Push to main, pull requests, manual dispatch

Performs static analysis using Cppcheck and Clang Static Analyzer. Runs memory leak detection with Valgrind to ensure code quality and identify potential bugs.

### security.yml - Code Security
**Triggers:** Push to main, pull requests

Runs CodeQL security analysis to detect potential security vulnerabilities and coding errors in the C/C++ codebase.
