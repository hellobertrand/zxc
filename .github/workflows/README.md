# GitHub Actions Workflows

This directory contains CI/CD workflows for the ZXC compression library.

## Maintenance branches

`release/X.Y` branches carry the patch releases of a published line (see [CONTRIBUTING.md](../CONTRIBUTING.md#branches-and-releases)). Wherever a trigger below says "push to main" or "pull requests", the same holds for `release/**`: pushes to a maintenance branch and pull requests against it run the same checks as `main`. Two workflows stay on `main` only:

- `pages.yml`: the site is built from `main` and the newest release, so a maintenance branch has nothing to deploy.
- `scorecard.yml`: Scorecard only publishes results for the default branch.

A patch on an older line (`v1.0.1` published after `v1.1.0`) changes nothing for the newest one: the site is not redeployed, npm publishes it under the `release-1.0` dist-tag instead of `latest`, and `build.yml` marks `v1.1.0` as the latest GitHub release again. PyPI, crates.io and the Go module proxy always resolve the highest version, whatever the publication order.

## Core Workflows

### build.yml - Build & Release
**Triggers:** Push to main or `release/**`, `v*` tags, pull requests, release published, manual dispatch

Builds and tests ZXC across multiple platforms (Linux x86_64/ARM64, macOS ARM64, Windows x64). On a published release, uploads the signed archives and their provenance, then dispatches every binding's publish job at the tag.

On a `v*` tag, `tag-placement` checks that `vX.Y.0` is reachable from `main` and `vX.Y.Z` (Z > 0) from `release/X.Y`; the asset upload depends on it. `latest-release` keeps the newest published version marked as the latest release.

### multiarch.yml - Multi-Architecture Build
**Triggers:** Push to main, pull requests, manual dispatch

Builds and tests ZXC across CPU architectures, Debian's included (GNU/Hurd aside: an OS, not a CPU), in three tiers:
- **Tier 1** (native runners, every supported image): Ubuntu 22.04/24.04/26.04 (x86_64, i386, arm64), Windows Server 2022/2025 (x86_64, i386), Windows 11 (arm64), macOS 15/26 (x86_64, arm64) and macOS 27 (arm64, preview image).
- **Tier 2** (Debian release architectures, cross-compiled in `debian:trixie`, tested under QEMU): armhf (with LTO), ppc64le, s390x, riscv64, loong64; plus x86_64 under Intel SDE, one job per SIMD dispatch tier (SSE2, AVX2, AVX-512).
- **Tier 3** (Debian ports and retired architectures, same setup): armv6, mipsel, powerpc, ppc64, sparc64, alpha, hppa, m68k, sh4; plus x86_64 APX under Intel SDE (Diamond Rapids model, the whole build compiled with `-mapxf`).

Tier 1 and Tier 2 are required checks on `main`; Tier 3 is not.

### downstream.yml - Downstream Builds
**Triggers:** Push to main, pull requests, manual dispatch

Builds and tests ZXC as downstream distributions build it, each in its own environment (libc, OS, compiler, packaging flags): Alpine (musl, x86_64 and arm64), Arch Linux and AlmaLinux 9 (RHEL 9) built with their distro packaging flags, Amazon Linux 2023 (arm64), and FreeBSD 14 and 15 in a VM. Warnings are errors at compile and at LTO link time.

### multicomp.yml - Compiler Compatibility
**Triggers:** Push to main, pull requests, manual dispatch

Tests the codebase against a wide range of compilers (various versions of GCC and Clang) to ensure compatibility and identify any compiler-specific issues or warnings.

### benchmark.yml - Performance Benchmark
**Triggers:** Push to main (src changes), pull requests, manual dispatch

Runs performance benchmarks using LZbench on Ubuntu and macOS. Integrates ZXC into the LZbench framework and tests compression/decompression performance against the Silesia corpus.

### pages.yml - Website
**Triggers:** Push to main and pull requests (`site/` changes), release published, manual dispatch

Builds libzxc.org from `site/` on `main` and the docs of the newest published release, then deploys it to Cloudflare from `main` or a release tag. A release that is not the newest one (a patch on an older line) does not redeploy the site.

## Quality & Security

### coverage.yml - Code Coverage
**Triggers:** Push to main, pull requests, manual dispatch

Builds the project with coverage instrumentation (`-DZXC_ENABLE_COVERAGE=ON`), runs unit and CLI tests, and generates a coverage report using `lcov`. The report is then uploaded to Codecov for analysis.

### fuzzing.yml - Fuzz Testing
**Triggers:** Pull requests, manual dispatch

Executes fuzz testing using ClusterFuzzLite with multiple sanitizers (address, undefined) on decompression and roundtrip fuzzers. Helps identify memory safety issues and edge cases.

### quality.yml - Code Quality
**Triggers:** Push to main, pull requests, manual dispatch

Performs static analysis using Cppcheck and Clang Static Analyzer. Runs memory leak detection with Valgrind to ensure code quality and identify potential bugs.

Also enforces formatting, one job per language, each running that ecosystem's canonical tool: `c-format` (clang-format, via `make format-check`), `rust-format` (`cargo fmt`, plus a warning-free `cargo doc`), `go-format` (`gofmt`), `python-format` (`black`) and `nodejs-format` (`prettier`, covering both the Node.js and WASM wrappers). Formatter versions are pinned so a new release of one cannot turn CI red on a commit that touched nothing.

### security.yml - Code Security
**Triggers:** Push to main, pull requests

Runs CodeQL security analysis to detect potential security vulnerabilities and coding errors in the C/C++ codebase.

### abi-check.yml - ABI Stability Check
**Triggers:** Pull requests (lib/header changes), push to main, manual dispatch

Builds `libzxc.so` with debug info, generates an ABI XML via [`abidw`](https://sourceware.org/libabigail/), and compares it against the committed baseline at [`docs/abi/libzxc-linux-x86_64.abi.xml`](../../docs/abi/libzxc-linux-x86_64.abi.xml) using `abidiff --no-added-syms`. Adding new symbols passes (MINOR bump); removing or changing existing symbols fails (MAJOR bump required + regenerate baseline). Run with `mode=regenerate` to produce a fresh baseline as a downloadable artifact.

The dump is taken with `--headers-dir include --drop-private-types`, so it records only what a consumer can reach. `src/lib`-only types never enter the baseline, and renaming or reshaping them is not an ABI event. The exported symbol set is already public-only by construction (`-fvisibility=hidden` plus `ZXC_EXPORT`); the flags keep the *documented* surface in step with the *binary* one.

### vector-stability.yml - Test Vector Stability
**Triggers:** Push to main, pull requests, manual dispatch

Two jobs, one per committed corpus, kept separate so a red check names which
contract broke.

**Golden files** runs `sha256sum -c` against [`tests/format/golden.sha256`](../../tests/format/golden.sha256), so a single changed byte under [`tests/format/golden/`](../../tests/format/golden/) fails it, and checks that the file set, the manifest and the annotated `.zxc.txt` dumps stay in sync. A failure means the *encoder's output* changed — not necessarily the format: an encoder improvement moves these bytes while still emitting a valid archive of the same version. Regenerate with `zxc_golden_gen`, refresh the manifest and the dumps in the same commit (see [`tests/format/README.md`](../../tests/format/README.md)).

**Conformance vectors** loops over every frozen corpus in `tests/conformance/v<N>/` and does the same against its `vectors.sha256`, which covers the archives, the `.expected` plaintexts and the `.zxd` dictionaries. These are what third-party decoders validate against, so a failure here is the grave one (see [`tests/conformance/README.md`](../../tests/conformance/README.md)).

The field-level structural validation runs separately, as the `format_golden` and `conformance` ctests in `build.yml`.

### kernel-linux.yml - Linux Kernel Module
**Triggers:** Changes to `contrib/linux-kernel/`, `include/` or `src/lib/`; weekly; manual dispatch

Builds [`contrib/linux-kernel`](../../contrib/linux-kernel/README.md) - the dependency header, the shims, the Kbuild and the self-test module - against Linux 4.19 (the minimum) and the runner kernel's headers, on x86_64 and arm64, under `-Werror`. The module's init round-trips a block and a frame at levels 3 and 7 and refuses to load on a mismatch. Off the PR path on purpose.

### kernel-windows.yml - Windows Kernel Library
**Triggers:** Changes to `contrib/windows-kernel/`, `include/` or `src/lib/`; weekly; manual dispatch

Builds [`contrib/windows-kernel`](../../contrib/windows-kernel/README.md) - the dependency header, the property sheets, the kernel-mode static library and the self-test driver that links it - for x64 and ARM64 at the Windows 10 2004 target, then asserts with `check-image.ps1` that the image imports `ntoskrnl.exe` only and is a Native image of the right machine and subsystem version. The runner image has Visual Studio 2026 with the WDK's Visual Studio extension but no kit, so the job installs the 28000 SDK and WDK, the kit line built for Visual Studio 2026, with Microsoft's installers first, each pinned to a versioned URL and checked against its SHA-256 and Authenticode signature. `check-image.ps1` also fails on any `ymm` or `zmm` register in the disassembly, so no AVX instruction reaches the image. Nothing is loaded; that is a manual step on a test-signed VM, as for the Linux module.

### scorecard.yml - OSSF Scorecard
**Triggers:** Push to main, scheduled (weekly), manual dispatch

Runs the [OSSF Scorecard](https://github.com/ossf/scorecard) analysis to evaluate the project against open source security best practices (branch protection, signed releases, dependency pinning, etc.). Results are published to the OpenSSF public dashboard and uploaded to GitHub's code scanning view as SARIF.

### vendors.yml - Vendor Maintenance
**Triggers:** Scheduled (weekly), manual dispatch

Automatically checks for and updates third-party dependencies (like `rapidhash.h`) to ensure the project uses the latest stable versions of its vendors.

### changelog.yml - CHANGELOG
**Triggers:** Push to `bump/**` or `release/**` branches, manual dispatch

Regenerates [`CHANGELOG`](../../CHANGELOG) with [`git-cliff`](https://git-cliff.org/), grouping all commits under their respective tags. On a `bump/vX.Y.Z` branch, cut from `main` or from `release/X.Y`, the version tag is taken from the branch name (the `v` is optional). A maintenance branch `release/X.Y` names no patch number, so its newest section is `Unreleased`, as on a manual dispatch. Only tags reachable from the branch count: a `v1.0.1` section holds the fixes cherry-picked onto `release/1.0`, and the CHANGELOG on `main` lists no patch release. The generated file is uploaded as a workflow artifact, it is **not** committed back to the repo. Configuration lives in [`cliff.toml`](../../cliff.toml).

## Language Bindings

### binding-rust.yml - Binding Rust
**Triggers:** Push to main or `release/**`, pull requests (binding changes), manual dispatch; on a published release, `build.yml` dispatches it at the tag to publish

Tests and publishes Rust crates to crates.io via trusted publishing. Verifies the version matches the release tag, runs tests across platforms, and publishes `zxc-compress-sys` (FFI bindings) followed by `zxc-compress` (safe wrapper).

### binding-python.yml - Binding Python
**Triggers:** Push to main or `release/**`, pull requests (binding changes), manual dispatch; on a published release, `build.yml` dispatches it at the tag to publish

Builds platform-specific wheels using `cibuildwheel` for Linux (x86_64, ARM64), macOS (ARM64, Intel), and Windows (AMD64, ARM64). Tests wheels against Python 3.12-3.14, then publishes to PyPI via trusted publishing.

### binding-wasm.yml - Binding WASM
**Triggers:** Push to main or `release/**`, manual dispatch; on a published release, `build.yml` dispatches it at the tag to publish

Builds the WebAssembly target using Emscripten SDK. Compiles the library with SIMD disabled (scalar codepath) and no threading, then runs a Node.js roundtrip test suite covering all compression levels, reusable contexts, and error handling. Uploads `zxc.js` + `zxc.wasm` as build artifacts. On tags, publishes the `zxc-wasm` package to npm via trusted publishing, under the `latest` dist-tag, or `release-X.Y` for a patch on an older line.

### binding-nodejs.yml - Binding Node.js
**Triggers:** Push to main or `release/**`, pull requests (binding changes), manual dispatch; on a published release, `build.yml` dispatches it at the tag to publish

Builds and publishes the Node.js package to npm via trusted publishing. Handles the compilation of native bindings and ensures the package is correctly versioned and distributed. A patch on an older line goes under the `release-X.Y` dist-tag, so `latest` stays on the newest line.

### binding-go.yml - Binding Go
**Triggers:** Push to main or `release/**`, pull requests (binding changes), manual dispatch; on a published release, `build.yml` dispatches it at the tag to publish

Runs comprehensive tests for the Go bindings across various platforms and architectures to ensure the Go package is stable and functional.
